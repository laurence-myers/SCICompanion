/***************************************************************************
	Copyright (c) 2020 Philip Fortier

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
***************************************************************************/

// ScriptDocument.cpp : implementation file
//

#include "stdafx.h"
#include "AppState.h"
#include "ScriptDocument.h"
#include "FindAllDialog.h"
#include "NewCompileDialog.h"
#include "SCO.h"
#include "CrystalScriptStream.h"
#include "ClassBrowserDialog.h"
#include "CompiledScript.h"
#include "SyntaxParser.h"
#include "CompileContext.h"
#include "ScriptOMAll.h"
#include "DecompilerCore.h"
#include <ctime>
#include "CObjectWrap.h"
#include "Text.h"
#include "ResourceEntity.h"
#include "Disassembler.h"
#include "SummarizeScript.h"
#include "DecompileScript.h"
#include "DecompilerResults.h"
#include "GameFolderHelper.h"
#include "DecompilerConfig.h"
#include "format.h"
#include "ResourceBlob.h"
#include "DependencyTracker.h"
#include "OutputCodeHelper.h"
#include <filesystem>

using namespace std;

// CompileLog::HasErrors and CalculateErrors are in CompileScript.cpp. This
// one plays a sound, so it stays with the GUI.
void CompileLog::SummarizeAndReportErrors()
{
	stringstream summaryMessage;
	summaryMessage << _cErrors << " errors, " << _cWarnings << " warnings.";
	ReportResult(CompileResult(summaryMessage.str()));

	if (_cErrors && appState->_fPlayCompileErrorSound)
	{
		// Play a sound.
		PlaySound((LPCSTR)SND_ALIAS_SYSTEMEXCLAMATION, NULL, SND_ALIAS_ID | SND_ASYNC);
	}
}

// CScriptDocument

IMPLEMENT_DYNCREATE(CScriptDocument, CDocument)

CScriptDocument::CScriptDocument() : _buffer(*this), _dependencyTracker(nullptr)
{
}

CScriptDocument::~CScriptDocument()
{
	_buffer.FreeAll();
}

void CScriptDocument::SetDependencyTracker(DependencyTracker &tracker)
{
	_dependencyTracker = &tracker;
}

BEGIN_MESSAGE_MAP(CScriptDocument, CDocument)
	ON_COMMAND(ID_FILE_SAVE, OnFileSave)
	ON_COMMAND(ID_FILE_SAVE_AS, OnFileSaveAs)
	ON_COMMAND(ID_COMPILE, OnCompile)
	ON_COMMAND(ID_SCRIPT_DISASSEMBLE, OnDisassemble)
	ON_COMMAND(ID_SCRIPT_VIEWOBJECTFILE, OnViewObjectFile)
	ON_COMMAND(ID_SCRIPT_VIEWSCRIPTRESOURCE, OnViewScriptResource)
	ON_COMMAND(ID_SCRIPT_VIEWSYNTAXTREE, OnViewSyntaxTree)
	ON_COMMAND(ID_DEBUGROOM, OnDebugRoom)
	ON_UPDATE_COMMAND_UI(ID_COMPILE, OnUpdateIsScript)
	ON_UPDATE_COMMAND_UI(ID_SCRIPT_DISASSEMBLE, OnUpdateIsScript)
	ON_UPDATE_COMMAND_UI(ID_SCRIPT_VIEWOBJECTFILE, OnUpdateIsScript)
	ON_UPDATE_COMMAND_UI(ID_SCRIPT_VIEWSCRIPTRESOURCE, OnUpdateIsScript)
	ON_UPDATE_COMMAND_UI(ID_DEBUGROOM, OnUpdateIsScript)
	ON_UPDATE_COMMAND_UI(ID_INDICATOR_LINECOUNT, OnUpdateLineCount)
END_MESSAGE_MAP()



CCrystalTextBuffer *CScriptDocument::GetTextBuffer()
{
	return &_buffer;
}

void CScriptDocument::OnUpdateIsScript(CCmdUI *pCmdUI)
{
	pCmdUI->Enable(!_scriptId.IsHeader());
}

const char c_szLine[] = "--------------------------------------------------------";
void CScriptDocument::OnCompile()
{
	if (_scriptId.IsHeader())
	{
		AfxMessageBox("Header files can not be compiled.", MB_OK | MB_ICONEXCLAMATION | MB_APPLMODAL, 0);
	}
	else
	{
		// Check if the script is modified, and if so, save it.
		if (_buffer.IsModified())
		{
			OnFileSave();
		}

		GameSession &session = appState->GetSession();
		DeferResourceAppend defer(session.ResourceMap());
		CompileLog log;
		_ClearErrorCount();
		CompileTables tables;
		tables.Load(session.ResourceMap());
		PrecompiledHeaders headers(session.ResourceMap());
		CompileResults results(log, session.Version());
		bool fSuccess = false;
		{
			// The class browser's background reload parses the same scripts and
			// reads the game, so hold its lock for the compile.
			ClassBrowserLock lock(appState->GetClassBrowser());
			lock.Lock();
			fSuccess = NewCompileScript(session, results, log, tables, headers, _scriptId);
			if (fSuccess)
			{
				appState->GetDependencyTracker().ClearScript(_scriptId);
			}
		}
		if (fSuccess)
		{
			tables.Save(session.ResourceMap());
		}

		// put a timestamp in.
		char sz[100];
		struct tm time;
#pragma warning (push)
#pragma warning (disable: 4996)
		_getsystime(&time);
#pragma warning (pop)

		strftime(sz, ARRAYSIZE(sz), "%I:%M:%S%p", &time);
		log.ReportResult(CompileResult(sz));

		stringstream str;
		str << "Compiling " << _scriptId.GetFileName();
		str << (fSuccess ? " succeeded." : " failed.");
		log.ReportResult(CompileResult(c_szLine));
		log.ReportResult(CompileResult(str.str()));

		string info = fmt::format(
			"Object data: {0} bytes   Code: {1} bytes   Script vars: {2} bytes   Strings: {3} bytes	Saids: {4} bytes",
			results.Stats.Objects,
			results.Stats.Code,
			results.Stats.Locals,
			results.Stats.Strings,
			results.Stats.Saids
		);
		log.ReportResult(CompileResult(info));

		sci::Status committed = defer.Commit();
		if (!committed)
		{
			log.ReportResult(CompileResult("There was a problem writing the compiled script: " + committed.error().ToString(), CompileResult::CRT_Error));
			log.CalculateErrors();
		}
		_DoErrorSummary(log);

		appState->OutputResults(OutputPaneType::Compile, log.Results());
	}
}

// SimpleCompile and NewCompileScript are in Src\Compile\CompileScript.cpp.

void DisassembleScript(WORD wScript)
{
	CompiledScript compiledScript(0);
	if (compiledScript.Load(appState->GetResourceMap().Helper(), appState->GetVersion(), wScript))
	{
		// Write some crap.
		GlobalCompiledScriptLookups scriptLookups;
		ObjectFileScriptLookups objectFileLookups(appState->GetResourceMap().Helper(), appState->GetResourceMap().GetCompiledScriptLookups()->GetSelectorTable());
		if (scriptLookups.Load(appState->GetResourceMap().Helper()))
		{
			std::stringstream out;
			::DisassembleScript(compiledScript, out, &scriptLookups, &objectFileLookups, appState->GetResourceMap().GetVocab000());
			ShowTextFile(out.str().c_str(), "script.sca.txt");
		}
	}
}

void CScriptDocument::OnDisassemble()
{
	WORD wScript;
	// Make the compiled script...
	if (SUCCEEDED(appState->GetResourceMap().GetScriptNumber(_scriptId, wScript)))
	{
		DisassembleScript(wScript);
	}
}

void DecompileScript(const GameFolderHelper &helper, WORD wScript, IDecompilerResults &results)
{
	CompiledScript compiledScript(0);
	if (compiledScript.Load(helper, appState->GetVersion(), wScript))
	{
		unique_ptr<sci::Script> pScript = DecompileScript(nullptr, *appState->GetResourceMap().GetCompiledScriptLookups(), helper, wScript, compiledScript, results);
		std::stringstream ss;
		sci::SourceCodeWriter out(ss, pScript.get());
		pScript->OutputSourceCode(out);
		ShowTextFile(ss.str().c_str(), "script.scp.txt");
	}
}

void FixDuplicateObjectNames(CompiledScript &compiledScript, const SelectorTable &selectorTable)
{
	// Occasionally a script will have objects with duplicate names. Rather than a bug, this indicates that there were two separate objects that had
	// their name property explicitly provided. An example is _MapInSection.sc in QFG2.
	// There are a few ways to address it, but we'll try the following here:
	//  Check for any name dupes in the objects.
	//  If so, change their name to some unique name
	//  Then add a name property with a value pointing to the original string.
	unordered_map<string, int> countOfNames;
	unordered_map<string, char> suffixes;
	for (const auto &object : compiledScript.GetObjects())
	{
		countOfNames[object->GetName()]++;
		suffixes[object->GetName()] = 'a';
	}

	for (auto &object : compiledScript.GetObjects())
	{
		int count = countOfNames[object->GetName()];
		if (count > 1)
		{
			// This is a multiple named one.
			std::string newName = fmt::format("{0}_{1}", object->GetName(), suffixes[object->GetName()]++);
			object->AdjustName(newName); // This will track the old name so we can explicitly list it
		}
	}
}

std::unique_ptr<sci::Script> DecompileScript(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, const GameFolderHelper &helper, WORD wScript, CompiledScript &compiledScript, IDecompilerResults &results, bool debugControlFlow, bool debugInstConsumption, PCSTR pszDebugFilter, bool decompileAsm, bool substituteTextTuples)
{
	unique_ptr<sci::Script> pScript;
	ObjectFileScriptLookups objectFileLookups(helper, scriptLookups.GetSelectorTable());
	// Ok if pText fails (and is NULL)
	unique_ptr<ResourceEntity> textResource = appState->GetResourceMap().CreateResourceFromNumber(ResourceType::Text, wScript);
	TextComponent *pText = nullptr;
	if (textResource)
	{
		pText = textResource->TryGetComponent<TextComponent>();
	}

	FixDuplicateObjectNames(compiledScript, config->GetSelectorTable());

	DecompileLookups decompileLookups(config, helper, wScript, &scriptLookups, &objectFileLookups, &compiledScript, pText, &compiledScript, results);
	decompileLookups.DebugControlFlow = debugControlFlow;
	decompileLookups.DebugInstructionConsumption = debugInstConsumption;
	decompileLookups.pszDebugFilter = pszDebugFilter;
	decompileLookups.DecompileAsm = decompileAsm;
	decompileLookups.SubstituteTextTuples = substituteTextTuples;
	pScript.reset(Decompile(helper, compiledScript, decompileLookups, appState->GetResourceMap().GetVocab000()));

	ConvertToSCISyntaxHelper(*pScript, &scriptLookups);

	return pScript;
}

void CScriptDocument::OnViewObjectFile()
{
	if (!_scriptId.IsHeader())
	{
		// Need the script name.
		string objectFileName = appState->GetResourceMap().Helper().GetScriptObjectFileName(_scriptId.GetTitle());
		if (!objectFileName.empty())
		{
			HANDLE hFile = CreateFile(objectFileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (hFile)
			{
				sci::streamOwner streamOwner(hFile);
				CSCOFile scoFile;
				SelectorTable selectorTable;
				selectorTable.Load(appState->GetResourceMap().Helper());
				if (scoFile.Load(streamOwner.getReader(), selectorTable))
				{
					stringstream out;
					scoFile.DebugOut(out);
					string strFileName = _scriptId.GetTitle();
					strFileName += ".sco.txt";
					ShowTextFile(out.str().c_str(), strFileName);
				}
				CloseHandle(hFile);
			}
		}
	}
}
void CScriptDocument::OnViewScriptResource()
{
	if (!_scriptId.IsHeader())
	{
		WORD wScript;
		// Make the compiled script...
		if (SUCCEEDED(appState->GetResourceMap().GetScriptNumber(_scriptId, wScript)))
		{
			CompiledScript compiledScript(0);
			if (compiledScript.Load(appState->GetResourceMap().Helper(), appState->GetVersion(), wScript))
			{
				// Write some crap.
				std::stringstream out;
				DebugOut(compiledScript, out);
				ShowTextFile(out.str().c_str(), "scriptresource.txt");
			}
		}
	}
}

unique_ptr<sci::Script> _ParseScript(ScriptId id)
{
	sci::Result<ScriptText> text = LoadScriptText(id.GetFullPath());
	if (text)
	{
		CScriptStreamLimiter limiter(*text);
		CCrystalScriptStream stream(&limiter);

		std::unique_ptr<sci::Script> pScript = std::make_unique<sci::Script>(id);
		CompileLog log;
		bool result = SyntaxParser_Parse(*pScript, stream, PreProcessorDefinesFromSCIVersion(appState->GetVersion()), &log);
		if (result)
		{
			return pScript;
		}
	}
	return nullptr;
}

void CScriptDocument::OnViewSyntaxTree()
{
	// What we do
	// 1) Parse this script and generate a syntax tree.
	// 2) If successful, write to the file.
	// 3) Reload
	CScriptStreamLimiter limiter(&_buffer);
	CCrystalScriptStream stream(&limiter);
	//SCIClassBrowser &browser = appState->GetClassBrowser(); 
	//browser.Lock();
	// 1)
	sci::Script script(_scriptId);
	CompileLog log;
	bool fCompile = SyntaxParser_Parse(script, stream, PreProcessorDefinesFromSCIVersion(appState->GetVersion()), &log);;
	if (fCompile)
	{
		ConvertToSCISyntaxHelper(script);

		std::stringstream out;
		sci::SourceCodeWriter theCode(out, &script);
		theCode.indentChar = '\t';
		theCode.indentAmount = 1;
		script.OutputSourceCode(theCode);

		ShowTextFile(out.str().c_str(), "syntaxtree.txt");
	}


#if NOT_NEEDED
	// Repurposing it for something else: calculating the dependency tree
	set<string> complete;
	stack<ScriptId> toProcess;
	toProcess.push(_scriptId);
	while (!toProcess.empty())
	{
		ScriptId id = toProcess.top();
		toProcess.pop();
		string title = id.GetTitle();
		ToUpper(title);
		complete.insert(title);
		unique_ptr<sci::Script> parsedScript = _ParseScript(id);
		if (parsedScript)
		{
			for (auto use : parsedScript->GetUses())
			{
				string USE = use;
				ToUpper(USE);
				if (complete.find(USE) == complete.end())
				{
					toProcess.push(appState->GetResourceMap().Helper().GetScriptId(use));
				}
			}
		}
	}
#endif
}

void CScriptDocument::OnDebugRoom()
{
	int resourceNumber = _scriptId.GetResourceNumber();
	if (resourceNumber != -1)
	{
		appState->RunGame(true, resourceNumber);
	}
}

void CScriptDocument::OnUpdateLineCount(CCmdUI *pCmdUI)
{
	pCmdUI->SetText(fmt::format("{0} lines.", _buffer.GetLineCount()).c_str());
}

void CScriptDocument::_DoErrorSummary(ICompileLog &log)
{
	log.SummarizeAndReportErrors();
}

void CScriptDocument::_ClearErrorCount()
{
}

// CScriptDocument diagnostics

#ifdef _DEBUG
void CScriptDocument::AssertValid() const
{
	CDocument::AssertValid();
}

void CScriptDocument::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG

// CScriptDocument serialization

void CScriptDocument::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}

// We don't want the user editing these files. They should use the message editor instead.
bool IsReadOnly(LPCTSTR pathName)
{
	return (0 == lstrcmp(PathFindExtension(pathName), ".shm")) ||
		(0 == lstrcmp(PathFindExtension(pathName), ".shp")) ||
		(0 == lstrcmpi(PathFindFileName(pathName), "Verbs.sh")) ||
		(0 == lstrcmpi(PathFindFileName(pathName), "Talkers.sh")) ||
		(0 == lstrcmpi(PathFindFileName(pathName), "keys.sh")) ||
		(0 == lstrcmpi(PathFindFileName(pathName), "sci.sh"));
}

// Use this instead of OnOpenDocument in order to properly set scriptNumber
BOOL CScriptDocument::OnOpenDocument(LPCTSTR lpszPathName, uint16_t scriptNumber)
{
	if (!__super::OnOpenDocument(lpszPathName))
		return FALSE;
	_scriptId = ScriptId(lpszPathName);
	_scriptId.SetResourceNumber(scriptNumber);
	_buffer.FreeAll();
	BOOL result = _buffer.LoadFromFile(lpszPathName);
	_buffer.SetReadOnly(IsReadOnly(lpszPathName));
	return result;
}

BOOL CScriptDocument::OnOpenDocument(LPCTSTR lpszPathName) 
{
	if (!__super::OnOpenDocument(lpszPathName))
		return FALSE;
	_scriptId = ScriptId(lpszPathName);
	_buffer.FreeAll();
	BOOL result = _buffer.LoadFromFile(lpszPathName);
	_buffer.SetReadOnly(IsReadOnly(lpszPathName));
	return result;
}
BOOL CScriptDocument::OnSaveDocument(LPCTSTR lpszPathName) 
{
	_buffer.SaveToFile(lpszPathName);
	_scriptId = ScriptId(lpszPathName);
	return TRUE;	//	Note - we didn't call inherited member!
}
BOOL CScriptDocument::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;
	return _buffer.InitNew();
}
void CScriptDocument::SaveIfModified()
{
	if (IsModified())
	{
		OnFileSave();
	}
}
void CScriptDocument::SetNameAndContent(ScriptId scriptId, int iResourceNumber, std::string &text)
{
	_scriptId = scriptId;
	int nEndLine = 0;
	int nEndChar = 0;
	_buffer.InsertText(NULL, 0, 0, text.c_str(), nEndLine, nEndChar);
	_buffer.SetModified();
	appState->GetResourceMap().AssignName(ResourceType::Script, iResourceNumber, NoBase36, scriptId.GetTitle().c_str());
	// Since we're assigning a name, we'd better save it too.
	OnFileSave();
}

void CScriptDocument::OnFileSave()
{
	std::string path = _scriptId.GetFullPath();
	// Get the current active script, and save it.
	_buffer.SaveToFile(path.c_str());
	// Update the classbrowser...
	SCIClassBrowser &browser = appState->GetClassBrowser();
	browser.TriggerReloadScript(path.c_str());

	if (_dependencyTracker)
	{
		_dependencyTracker->NotifyScriptFileChanged(_scriptId);
	}

	// Notify clients *after* we have updated the class browser.
	UpdateAllViewsAndNonViews(nullptr, 0, &WrapObject(ScriptChangeHint::Saved, this));
}

const TCHAR g_rgszScriptFilter[] = TEXT("Script (*.sc)|*.sc;|Header (*.sh)|*.sh|All Files (*.*)|*.*||");
const TCHAR g_rgszHeaderFilter[] = TEXT("Header (*.sh)|*.sh|Script (*.sc)|*.sc;|All Files (*.*)|*.*||");


void CScriptDocument::OnFileSaveAs()
{
	// Set up the save dialog.
	CString strTitle = _scriptId.GetTitle().c_str();
	CFileDialog fileDialog(FALSE,
						   _scriptId.IsHeader() ? ".sh" : ".sc",
						   strTitle,
						   OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR,
						   _scriptId.IsHeader() ? g_rgszHeaderFilter : g_rgszScriptFilter);
	CString strFolder = _scriptId.GetFolder().c_str();
	fileDialog.m_pOFN->lpstrInitialDir = strFolder;
	if (IDOK == fileDialog.DoModal())
	{
		// We have a new filename.
		CString strFileName = fileDialog.GetPathName();
		// But save it under the new name.
		if (_buffer.SaveToFile(strFileName))
		{
			// We have a new identity
			_scriptId = ScriptId(strFileName);
		}
	}
}

void CScriptDocument::UpdateModified()
{
	SetModifiedFlag(_buffer.IsModified());
	_OnUpdateTitle();
}

void CScriptDocument::_OnUpdateTitle()
{
	SetTitle(_scriptId.GetFileNameOrig().c_str());
}
