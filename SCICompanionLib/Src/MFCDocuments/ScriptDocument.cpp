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

// The CompileLog functions are in CompileScript.cpp.

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
		CompileLog log;
		_ClearErrorCount();
		// Plan step S2: a batch of one script. It saves the tables when the
		// script compiled, and writes the resources in one commit. It asks
		// before a package save that a patch file would hide.
		CompileOptions options;
		options.askShadows = AskAboutShadowingPatches;
		sci::Result<std::unique_ptr<CompileBatch>> batch = CompileBatch::Start(session, { _scriptId }, options);
		CompileReport report;
		if (batch)
		{
			std::atomic<bool> abort(false);
			ICompileEvents events;
			{
				// The class browser's background reload parses the same scripts and
				// reads the game, so hold its lock for the compile.
				ClassBrowserLock lock(appState->GetClassBrowser());
				lock.Lock();
				while ((*batch)->Step(abort, events))
				{
				}
			}
			report = (*batch)->Finish();
		}
		else
		{
			log.ReportResult(StartFailureLine(batch.error()));
		}
		bool fSuccess = !report.scripts.empty() && report.scripts[0].status.has_value();
		// The user stopped it in the question: not an error (review of S2c).
		bool stopped = batch ? (!report.commit && (report.commit.error().code == sci::ErrorCode::Cancelled)) :
			(batch.error().code == sci::ErrorCode::Cancelled);
		if (fSuccess && report.commit)
		{
			// The script is written: it is no longer out of date (review of
			// S2c: before, it was cleared before the commit).
			appState->GetDependencyTracker().ClearScript(_scriptId);
		}
		CompileStats stats;
		if (!report.scripts.empty())
		{
			for (const CompileResult &result : report.scripts[0].diagnostics)
			{
				log.ReportResult(result);
			}
			stats = report.scripts[0].stats;
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
		str << (stopped ? " was stopped." : (fSuccess ? " succeeded." : " failed."));
		log.ReportResult(CompileResult(c_szLine));
		log.ReportResult(CompileResult(str.str()));

		if (!report.scripts.empty())
		{
			// No sizes when no script compiled (review of S2c).
			string info = fmt::format(
				"Object data: {0} bytes   Code: {1} bytes   Script vars: {2} bytes   Strings: {3} bytes	Saids: {4} bytes",
				stats.Objects,
				stats.Code,
				stats.Locals,
				stats.Strings,
				stats.Saids
			);
			log.ReportResult(CompileResult(info));
		}

		ReportCompileBatch(report, log, "There was a problem writing the compiled script: ");
		// The counts of every error and warning above.
		log.CalculateErrors();
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

// DecompileScript and FixDuplicateObjectNames are in Src\Compile\DecompileScript.cpp.

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
		ConvertToSCISyntaxHelper(script, appState->GetResourceMap().Helper());

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

void CScriptDocument::_DoErrorSummary(CompileLog &log)
{
	log.SummarizeAndReportErrors();
	if (log.HasErrors() && appState->_fPlayCompileErrorSound)
	{
		// Play a sound.
		PlaySound((LPCSTR)SND_ALIAS_SYSTEMEXCLAMATION, NULL, SND_ALIAS_ID | SND_ASYNC);
	}
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
