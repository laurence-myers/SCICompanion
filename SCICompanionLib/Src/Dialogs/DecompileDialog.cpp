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
#include "stdafx.h"
#include "AppState.h"
#include "DecompileDialog.h"
#include "CompiledScript.h"
#include "DecompilerCore.h"
#include "DecompileRun.h"
#include "SCO.h"
#include "DecompilerResults.h"
#include "GameFolderHelper.h"
#include "GameSession.h"
#include "ScriptNameMap.h"
#include "format.h"
#include "ResourceContainer.h"

using namespace std;

// Use the XP toolset, PBS_MARQUEE is not defined...
#ifndef PBS_MARQUEE
#define PBS_MARQUEE 0x08
#endif

#define UWM_UPDATESTATUS (WM_APP + 1)
#define CHEKCDONE_TIMER 3456

// Because only get individual item state changes from listview (instead of a "selection is done"),
// multiselect can result in tons of state changes. Instead of updating on that, we'll update after
// a certain amount of time.
#define SELECTION_TIMER 4567	 

DecompileDialog::DecompileDialog(CWnd* pParent /*=NULL*/)
	: CExtResizableDialog(DecompileDialog::IDD, pParent), previousSelection(-1), _inScriptListLabelEdit(false), _inSCOLabelEdit(false), initialized(false), _helper(appState->GetResourceMap().Helper()), _syncSelection(false)
{
}

DecompileDialog::~DecompileDialog()
{
	// Join the worker before any member is torn down. The worker uses
	// _decompileResults and the options of the dialog while the run goes on,
	// so member destruction must not free them before _future's own blocking
	// dtor joins the worker -- a use-after-free. Abort first so the wait is
	// short, then wait here, while every member the worker uses is still
	// alive. (#53)
	if (_decompileResults)
	{
		_decompileResults->SetAborted();
	}
	if (_future && _future->valid())
	{
		_future->wait();
	}
}

BOOL DecompileDialog::OnInitDialog()
{
	BOOL fRet = __super::OnInitDialog();
	//ShowSizeGrip(FALSE);
	SetTimer(CHEKCDONE_TIMER, 100, nullptr);
	SetTimer(SELECTION_TIMER, 30, nullptr);

	return fRet;
}

void DecompileDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDITSCRIPT, m_wndScript);
	DDX_Control(pDX, IDC_DECOMPILATIONRESULTS, m_wndResults);
	DDX_Control(pDX, IDC_LISTSCRIPTS, m_wndListScripts);
	DDX_Control(pDX, IDCANCEL, m_wndCancel);
	DDX_Control(pDX, IDC_DECOMPILE, m_wndDecompile);
	DDX_Control(pDX, IDC_CLEARSCO, m_wndClearSCO);
	DDX_Control(pDX, IDC_ASSIGNFILENAMES, m_wndSetFilenames);
	DDX_Control(pDX, IDC_DECOMPILECANCEL, m_wndDecomileCancel);
	DDX_Control(pDX, IDC_DECOMPILESTATUS, m_wndStatus);
	DDX_Control(pDX, IDC_CHECKCONTROLFLOW, m_wndDebugControlFlow);
	DDX_Control(pDX, IDC_CHECKINSTRUCTIONCONSUMPTION, m_wndDebugInstConsumption);
	DDX_Control(pDX, IDC_CHECKASM, m_wndAsm);
	DDX_Control(pDX, IDC_EDITDEBUGMATCH, m_wndDebugFunctionMatch);
	m_wndDebugFunctionMatch.SetWindowTextA("*");
	DDX_Control(pDX, IDC_GROUPDEBUG, m_wndGroupDebug);
	DDX_Control(pDX, IDC_CHECKSELECTALL, m_wndSelectAll);
	DDX_Control(pDX, IDC_CHECKREDECOMPILE, m_wndRedecompile);
	m_wndRedecompile.SetCheck(BST_CHECKED);
	DDX_Control(pDX, IDC_CHECKTEXTTUPLES, m_wndTextTuples);
	m_wndTextTuples.SetCheck(BST_UNCHECKED);
	DDX_Control(pDX, IDC_GROUPOPTIONS, m_wndGroupOptions);
	DDX_Control(pDX, IDC_INSTRUCTIONS, m_wndSCOLabel);

	m_wndScript.SetWindowText(
		"Start with \"Reset filenames\" to give scripts meaningful names based upon their contents (or this will be done automatically upon first decompile if no script filenames have been set yet).\r\n"
		"\r\n"
		"Select a script on the left to decompile it. You may rename scripts as you wish, although scripts dependent on that script will then need to be recompiled.\r\n"
		"\r\n"
		"Decompiling a script will also generate a .sco file. The .sco file tracks procedure and variable names which are not present in the compiled script. By default they are given names such as \"local4\"\r\n"
		"You may edit the names to make them more meaningful, and they will be picked up the next time you decompile the script.\r\n"
		"\r\n"
		"You may delete the .sco file if you wish to clear out the variable and procedure names you have given."
		);
	
	DDX_Control(pDX, IDC_PROGRESS1, m_wndProgress);
	// Set this here instead of the rc file, due to the xp toolset issue mentioned above.
	m_wndProgress.ModifyStyle(0, PBS_MARQUEE, 0);
	// For some reason this seems necessary, even though I'm using a marquee progress bar:
	m_wndProgress.SetRange(0, 100);
	m_wndProgress.SetPos(1);

	DDX_Control(pDX, IDC_TREESCO, m_wndTreeSCO);

	if (!initialized)
	{
		// The src folder, and the files of the Decompiler folder when
		// src\Decompiler.ini does not exist (plan step S4: a plain copy that
		// never overwrites a file; before, the shell copied them, and could
		// ask to replace a file of the game).
		sci::Status prepared = PrepareDecompileFolder(_helper, appState->GetResourceMap().GetDecompilerFolder());
		if (!prepared)
		{
			AfxMessageBox(prepared.error().ToString().c_str(), MB_OK | MB_APPLMODAL);
		}

		_InitScriptList();
		_PopulateScripts();
		// A whole-game decompile is the common case, and the batch names the
		// globals across everything it is given, so start with every script
		// selected.
		_SelectAll(true);
		m_wndSelectAll.SetCheck(BST_CHECKED);
		initialized = true;
	}
}

const int NameColumn = 0;
const int NumberColumn = 1;
const int SCOColumn = 2;
const int SourceColumn = 3;

struct
{
	char *text;
	int width;
	int id;
}
c_DecompileColumns[] =
{
	{ "Name", 90, NameColumn },
	{ "Num", 45, NumberColumn },
	{ "SCO", 35, SCOColumn },
	{ "Src", 30, SourceColumn },
};

void DecompileDialog::_InitScriptList()
{
	for (int i = 0; i < ARRAYSIZE(c_DecompileColumns); i++)
	{
		LVCOLUMN col = { 0 };
		col.mask = LVCF_FMT | LVCF_ORDER | LVCF_SUBITEM | LVCF_TEXT | LVCF_WIDTH;
		col.iOrder = i;
		col.iSubItem = c_DecompileColumns[i].id;
		col.pszText = c_DecompileColumns[i].text;
		col.cx = c_DecompileColumns[i].width;
		col.fmt = LVCFMT_LEFT;
		m_wndListScripts.InsertColumn(i, &col);
	}
}

char c_Yes[] = "Yes";
char c_Empty[] = "";

void DecompileDialog::_UpdateScripts(set<uint16_t> updatedScripts)
{
	for (int i = 0; i < m_wndListScripts.GetItemCount(); i++)
	{
		uint16_t scriptNum = (uint16_t)m_wndListScripts.GetItemData(i);
		if (contains(updatedScripts, scriptNum))
		{
			string name = _helper.FigureOutName(ResourceType::Script, scriptNum, NoBase36);
			LVITEM item = {};
			item.iItem = i;

			item.mask = LVIF_TEXT;
			item.iSubItem = SCOColumn;
			string scoFilename = _helper.GetScriptObjectFileName(name);
			item.pszText = PathFileExists(scoFilename.c_str()) ? c_Yes : c_Empty;
			m_wndListScripts.SetItem(&item);

			item.mask = LVIF_TEXT;
			item.iSubItem = SourceColumn;
			string scriptFilename = _helper.GetScriptFileName(name);
			item.pszText = PathFileExists(scriptFilename.c_str()) ? c_Yes : c_Empty;
			m_wndListScripts.SetItem(&item);
		}
	}
	_SyncSelection();
}

int CALLBACK _SortListByNumber(LPARAM lpOne, LPARAM lpTwo, LPARAM lpArg)
{
	return ((int)lpOne - (int)lpTwo);
}

void DecompileDialog::_PopulateScripts()
{
	SetRedraw(FALSE);
	m_wndListScripts.DeleteAllItems();

	auto scriptResources = _helper.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::NameLookups);
	int itemNumber = 0;
	for (auto &blob : *scriptResources)
	{
		string name = blob->GetName();
		LVITEM item = {};
		item.mask = LVIF_TEXT | LVIF_PARAM;
		item.pszText = const_cast<LPSTR>(name.c_str());
		item.iItem = itemNumber;
		item.iSubItem = NameColumn;
		item.lParam = blob->GetNumber();	// associated data is the script number
		m_wndListScripts.InsertItem(&item);

		item.mask = LVIF_TEXT;
		item.iSubItem = NumberColumn;
		string scriptNumString = fmt::format("{0}", blob->GetNumber());
		item.pszText = const_cast<LPSTR>(scriptNumString.c_str());
		m_wndListScripts.SetItem(&item);

		item.mask = LVIF_TEXT;
		item.iSubItem = SCOColumn;
		string scoFilename = _helper.GetScriptObjectFileName(name);
		item.pszText = PathFileExists(scoFilename.c_str()) ? c_Yes : c_Empty;
		m_wndListScripts.SetItem(&item);

		item.mask = LVIF_TEXT;
		item.iSubItem = SourceColumn;
		string scriptFilename = _helper.GetScriptFileName(name);
		item.pszText = PathFileExists(scriptFilename.c_str()) ? c_Yes : c_Empty;
		m_wndListScripts.SetItem(&item);

		itemNumber++;
	}
	m_wndListScripts.SortItems(_SortListByNumber, 0);
	SetRedraw(TRUE);
}

LPARAM _IndexToParam(bool isVariable, int index)
{
	return index | (isVariable ? 0x80000000 : 0);
}
void _ParamToIndex(LPARAM param, int &index, bool &isVariable)
{
	isVariable = (param & 0x80000000) != 0;
	index = param & ~0x80000000;
}

void DecompileDialog::_PopulateSCOTree()
{
	m_wndTreeSCO.DeleteAllItems();
	if (_sco)
	{
		TVINSERTSTRUCT exportRoot = {};
		exportRoot.hParent = TVI_ROOT;
		exportRoot.hInsertAfter = TVI_FIRST;
		exportRoot.item.pszText = "Public procedures";
		exportRoot.item.mask = TVIF_TEXT;
		HTREEITEM hExportRoot = m_wndTreeSCO.InsertItem(&exportRoot);

		TVINSERTSTRUCT varRoot = {};
		varRoot.hParent = TVI_ROOT;
		varRoot.hInsertAfter = hExportRoot;
		varRoot.item.pszText = "Variables";
		varRoot.item.mask = TVIF_TEXT;
		HTREEITEM hVarRoot = m_wndTreeSCO.InsertItem(&varRoot);

		HTREEITEM hPrevious = TVI_FIRST;
		int index = 0;
		for (auto &publicProc : _scoPublicProcIndices)
		{
			TVINSERTSTRUCT item = {};
			item.hParent = hExportRoot;
			item.hInsertAfter = hPrevious;
			string name = _sco->GetExports()[publicProc].GetName();
			item.item.pszText = const_cast<PSTR>(name.c_str());
			item.item.mask = TVIF_TEXT | TVIF_PARAM;
			item.item.lParam = _IndexToParam(false, index);
			hPrevious = m_wndTreeSCO.InsertItem(&item);
			index++;
		}

		hPrevious = TVI_FIRST;
		index = 0;
		for (auto &variable : _sco->GetVariables())
		{
			// Empty variable names in the SCO file are used to indicate the previous one was an array
			// (and its size)
			if (!variable.GetName().empty())
			{
				TVINSERTSTRUCT item = {};
				item.hParent = hVarRoot;
				item.hInsertAfter = hPrevious;
				item.item.pszText = const_cast<PSTR>(variable.GetName().c_str());
				item.item.mask = TVIF_TEXT | TVIF_PARAM;
				item.item.lParam = _IndexToParam(true, index);
				hPrevious = m_wndTreeSCO.InsertItem(&item);
			}
			index++;
		}

		m_wndTreeSCO.Expand(hExportRoot, TVE_EXPAND);
		m_wndTreeSCO.Expand(hVarRoot, TVE_EXPAND);
	}
}

void DecompileDialog::_SyncSelection(bool force)
{
	UINT selectedCount = m_wndListScripts.GetSelectedCount();
	bool selected = (selectedCount == 1);

	_SyncButtonState();

	if (selected)
	{
		POSITION pos = m_wndListScripts.GetFirstSelectedItemPosition();
		int selectedItem = m_wndListScripts.GetNextSelectedItem(pos);
		if (force || (selectedItem != previousSelection))
		{
			LPARAM param = m_wndListScripts.GetItemData(selectedItem);
			string sourceFileName = _helper.GetScriptFileName((uint16_t)param);
			std::ifstream scriptFile(sourceFileName.c_str());
			if (scriptFile.is_open())
			{
				std::string scriptText;
				std::string line;
				while (std::getline(scriptFile, line))
				{
					scriptText += line;
					scriptText += "\r\n";
				}
				m_wndScript.SetWindowText(scriptText.c_str());
			}
			else
			{
				m_wndScript.SetWindowTextA("Source not found.");
			}

			// Load the .sco file
			_sco.reset(nullptr);
			_scoPublicProcIndices.clear();
			_sco = GetExistingSCOFromScriptNumber(_helper, (uint16_t)param, appState->GetResourceMap().GetCompiledScriptLookups()->GetSelectorTable());
			if (_sco)
			{
				// Detect which exports are not procedures by seeing if its name
				// matches a public instance in the compiled script (which should be sync'd with the SCO)
				CompiledScript compiledScript((uint16_t)param);
				compiledScript.Load(_helper, _helper.Version, param);
				int exportIndex = 0;
				for (auto &publicExport : _sco->GetExports())
				{
					bool isPublicProc = true;
					for (auto &object : compiledScript.GetObjects())
					{
						if (object->IsPublic && (object->GetName() == publicExport.GetName()))
						{
							isPublicProc = false;   // It's an object
							break;
						}
					}
					if (isPublicProc)
					{
						_scoPublicProcIndices.push_back(exportIndex);
					}
					exportIndex++;
				}
			}
			_PopulateSCOTree();

			previousSelection = selectedItem;
		}
	}
	else
	{
		m_wndScript.SetWindowTextA("");
		m_wndTreeSCO.DeleteAllItems();
		_sco.reset(nullptr);
		_scoPublicProcIndices.clear();
	}

	// TODO Sync single selection...
}

BEGIN_MESSAGE_MAP(DecompileDialog, CExtResizableDialog)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LISTSCRIPTS, &DecompileDialog::OnLvnItemchangedListscripts)
	ON_NOTIFY(TVN_BEGINLABELEDIT, IDC_TREESCO, &DecompileDialog::OnTvnBeginlabeleditTreesco)
	ON_NOTIFY(TVN_ENDLABELEDIT, IDC_TREESCO, &DecompileDialog::OnTvnEndlabeleditTreesco)
	ON_NOTIFY(LVN_BEGINLABELEDIT, IDC_LISTSCRIPTS, &DecompileDialog::OnLvnBeginlabeleditListscripts)
	ON_NOTIFY(LVN_ENDLABELEDIT, IDC_LISTSCRIPTS, &DecompileDialog::OnLvnEndlabeleditListscripts)
	ON_BN_CLICKED(IDC_DECOMPILE, &DecompileDialog::OnBnClickedDecompile)
	ON_BN_CLICKED(IDC_ASSIGNFILENAMES, &DecompileDialog::OnBnClickedAssignfilenames)
	ON_BN_CLICKED(IDC_DECOMPILECANCEL, &DecompileDialog::OnBnClickedDecompilecancel)
	ON_WM_TIMER()
	ON_MESSAGE(UWM_UPDATESTATUS, UpdateStatus)
	ON_BN_CLICKED(IDC_CLEARSCO, &DecompileDialog::OnBnClickedClearsco)
	ON_BN_CLICKED(IDC_CHECKSELECTALL, &DecompileDialog::OnBnClickedCheckselectall)
END_MESSAGE_MAP()

#define VK_A		65

// All this to handle the user pressing enter on a listview item.
BOOL DecompileDialog::PreTranslateMessage(MSG* pMsg)
{
	if (_inSCOLabelEdit || _inScriptListLabelEdit)
	{
		if (WM_KEYDOWN == pMsg->message &&
			(VK_ESCAPE == pMsg->wParam || VK_RETURN == pMsg->wParam))
		{
			if (_inSCOLabelEdit)
			{
				m_wndTreeSCO.SendMessage(TVM_ENDEDITLABELNOW, (VK_ESCAPE == pMsg->wParam));
			}
			else
			{
				if (TranslateMessage(pMsg))
				{
					DispatchMessage(pMsg);
				}
			}
			return TRUE;
		}
	}
	else
	{
		if (WM_KEYDOWN == pMsg->message)
		{
			if (VK_F2 == pMsg->wParam)
			{
				if (GetFocus()->GetSafeHwnd() == m_wndListScripts.GetSafeHwnd())
				{
					int selectedItem = m_wndListScripts.GetSelectionMark();
					if (selectedItem != -1)
					{
						m_wndListScripts.EditLabel(selectedItem);
					}
					return TRUE;
				}
				else if (GetFocus()->GetSafeHwnd() == m_wndTreeSCO.GetSafeHwnd())
				{
					HTREEITEM hTreeItem = m_wndTreeSCO.GetSelectedItem();
					if (hTreeItem != nullptr)
					{
						m_wndTreeSCO.EditLabel(hTreeItem);
					}
					return TRUE;
				}
			}
			else if ((VK_A == pMsg->wParam) && (GetKeyState(VK_CONTROL) & 0x8000))
			{
				// REVIEW: Check if focus is in scripts list?
				_SelectAll(true);
				m_wndSelectAll.SetCheck(BST_CHECKED);
				return TRUE;
			}
		}
	}
	return __super::PreTranslateMessage(pMsg);
}

void DecompileDialog::OnLvnItemchangedListscripts(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	BOOL bSelectedNow = (pNMLV->uNewState  & LVIS_SELECTED);
	BOOL bSelectedBefore = (pNMLV->uOldState  & LVIS_SELECTED);
	if (bSelectedNow != bSelectedBefore)
	{
		_syncSelection = true;
	}
	*pResult = 0;
}


void DecompileDialog::OnTvnBeginlabeleditTreesco(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTVDISPINFO pTVDispInfo = reinterpret_cast<LPNMTVDISPINFO>(pNMHDR);
	if (m_wndTreeSCO.GetParentItem(pTVDispInfo->item.hItem) != nullptr)
	{
		_inSCOLabelEdit = true;
	}
	*pResult = !_inSCOLabelEdit;
}


void DecompileDialog::OnTvnEndlabeleditTreesco(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTVDISPINFO pTVDispInfo = reinterpret_cast<LPNMTVDISPINFO>(pNMHDR);
	if (pTVDispInfo->item.pszText)
	{
		// Label was edited. Reject it if it doesn't match our standards, or is a dupe of another
		// TODO: Validate against variable standard (e.g. alphanum, etc...)
		// TODO: check it's not dupe of another
		bool isVariable;
		int index;
		_ParamToIndex(pTVDispInfo->item.lParam, index, isVariable);
		// Note that the array index does not correspond to the actual export index. That's ok though.
		if (isVariable)
		{
			_sco->GetVariables()[index].SetName(pTVDispInfo->item.pszText);
		}
		else
		{
			_sco->GetExports()[_scoPublicProcIndices[index]].SetName(pTVDispInfo->item.pszText);
		}
		*pResult = 1;
		sci::Status saved = SaveSCOFile(_helper, *_sco);
		std::string status = saved ?
			fmt::format("Saved changes to {0}", PathFindFileName(_helper.GetScriptObjectFileName(_sco->GetScriptNumber()).c_str())) :
			("Could not save the changes: " + saved.error().ToString());
		m_wndStatus.SetWindowTextA(status.c_str());
	}
	else
	{
		*pResult = 0;
	}
	_inSCOLabelEdit = false;
}


void DecompileDialog::OnLvnBeginlabeleditListscripts(NMHDR *pNMHDR, LRESULT *pResult)
{
	NMLVDISPINFO *pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	_inScriptListLabelEdit = true;
	*pResult = 0;
}


void DecompileDialog::OnLvnEndlabeleditListscripts(NMHDR *pNMHDR, LRESULT *pResult)
{
	NMLVDISPINFO *pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);

	if (pDispInfo->item.pszText)
	{
		uint16_t scriptNumber = (uint16_t)pDispInfo->item.lParam;
		// Rename the .sco and .sc files
		string scOld = _helper.GetScriptFileName(scriptNumber);
		string scoOld = _helper.GetScriptObjectFileName(scriptNumber);
		appState->GetResourceMap().AssignName(ResourceType::Script, scriptNumber, NoBase36, pDispInfo->item.pszText);
		
		// And move them.
		try
		{
			movefile(scOld, _helper.GetScriptFileName(scriptNumber));
			movefile(scoOld, _helper.GetScriptObjectFileName(scriptNumber));
		}
		catch (std::exception &e)
		{
			AfxMessageBox(e.what(), MB_ICONWARNING | MB_OK);
		}

		*pResult = 1;
	}
	else
	{
		*pResult = 0;
	}

	_inScriptListLabelEdit = false;
}

void DecompileDialog::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == SELECTION_TIMER)
	{
		if (_syncSelection)
		{
			_SyncSelection();
			_syncSelection = false;
		}
	}
	else if (nIDEvent == CHEKCDONE_TIMER)
	{
		if (_future && (future_status::ready == _future->wait_for(std::chrono::seconds(0))))
		{
			vector<pair<string, string>> updatedGlobalsList = _decompileResults->GetUpdatedGlobalsList();
			set<uint16_t> staleScripts = _decompileResults->GetStaleScripts();

			_decompileResults.reset(nullptr);
			_SyncButtonState();
			_future.reset(nullptr); // Don't need this anymore
			m_wndStatus.SetWindowTextA("");
			_UpdateScripts(_scriptNumbers);
			_SyncSelection(true);

			for (uint16_t scriptNumber : _scriptNumbers)
			{
				appState->ReopenScriptDocument(scriptNumber);
			}

			if (!updatedGlobalsList.empty())
			{
				// The scripts of this pass were named together, so they all use
				// the new names. A script decompiled in an earlier pass still
				// refers to a renamed global by its old name, and needs to go
				// again; only those, not the whole game.
				if (!staleScripts.empty())
				{
					bool redecompile = (m_wndRedecompile.GetCheck() == BST_CHECKED);
					if (!redecompile)
					{
						string message = fmt::format("{0} global variable(s) had their name updated during this pass.\n{1} -> {2}, ...\n\n{3} previously decompiled script(s) refer to them by their old names and need to be decompiled again. Decompile them now?",
							updatedGlobalsList.size(),
							updatedGlobalsList[0].first,
							updatedGlobalsList[0].second,
							staleScripts.size()
							);

						redecompile = (IDYES == AfxMessageBox(message.c_str(), MB_YESNO | MB_ICONINFORMATION));
					}
					if (redecompile)
					{
						_SelectScripts(staleScripts);
						OnBnClickedDecompile();
					}
				}
			}
		}
	}
	else
	{
		__super::OnTimer(nIDEvent);
	}
}

void DecompileDialog::OnBnClickedDecompile()
{
	m_wndResults.SetWindowTextA("");

	_debugControlFlow = m_wndDebugControlFlow.GetCheck() != 0;
	_debugInstConsumption = m_wndDebugInstConsumption.GetCheck() != 0;
	_debugAsm = m_wndAsm.GetCheck() != 0;
	m_wndDebugFunctionMatch.GetWindowTextA(_debugFunctionMatch);
	_substituteTextTuples = m_wndTextTuples.GetCheck() != 0;

	// Get a list of scripts to decompile
	if (m_wndListScripts.GetSelectedCount() == 0)
	{
		if (IDYES == AfxMessageBox("No scripts have been selected to decompile.\nDecompile all scripts?", MB_YESNO))
		{
			// Select all
			m_wndListScripts.SetRedraw(FALSE);
			for (int i = 0; i < m_wndListScripts.GetItemCount(); i++)
			{
				m_wndListScripts.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
			}
			m_wndListScripts.SetRedraw(TRUE);
		}
	}
	_scriptNumbers.clear();
	POSITION pos = m_wndListScripts.GetFirstSelectedItemPosition();
	while (pos != nullptr)
	{
		int selectedItem = m_wndListScripts.GetNextSelectedItem(pos);
		uint16_t scriptNumber = (uint16_t)m_wndListScripts.GetItemData(selectedItem);
		_scriptNumbers.insert(scriptNumber);
	}

	// If filenames have not been set, set them now. Do this *after* we get selection, since assigning filenames
	// clears selection.
	if (!_helper.DoesSectionExistWithEntries("Script"))
	{
		_AssignFilenames();
	}

	if (!_scriptNumbers.empty())
	{
		if (!_future)
		{
			_decompileResults = make_unique<DecompilerDialogResults>(this->GetSafeHwnd());
			_SyncButtonState();
			_session = &appState->GetSession();
			try
			{
				_future = std::make_unique<std::future<void>>(std::async(std::launch::async, s_DecompileThreadWorker, this));
			}
			catch (std::system_error)
			{
				_decompileResults.reset(nullptr);
				_SyncButtonState();
			}
		}
	}
}

const int MarqueeMilliseconds = 30;

void DecompileDialog::_SyncButtonState()
{
	UINT selectedCount = m_wndListScripts.GetSelectedCount();
	bool selected = (selectedCount > 0);
	bool decompiling = (_decompileResults != nullptr);

	m_wndCancel.EnableWindow(!decompiling);
	m_wndDecompile.EnableWindow(!decompiling);
	m_wndClearSCO.EnableWindow(!decompiling && selected);
	m_wndSetFilenames.EnableWindow(!decompiling);
	m_wndDecomileCancel.EnableWindow(decompiling);
	m_wndProgress.ShowWindow(decompiling ? SW_SHOW : SW_HIDE);
	m_wndProgress.SendMessage(PBM_SETMARQUEE, decompiling, MarqueeMilliseconds);
}

void DecompileDialog::OnBnClickedAssignfilenames()
{
	_AssignFilenames();
}

void DecompileDialog::_AssignFilenames()
{
	GlobalCompiledScriptLookups *lookups = appState->GetResourceMap().GetCompiledScriptLookups();
	if (lookups)
	{
		// The naming rule of the command line too: the scripts go in number
		// order, so the "_N" suffix of a duplicate name follows the number.
		std::vector<ScriptObjectsForNaming> scripts;
		for (CompiledScript *script : lookups->GetGlobalClassTable().GetAllScripts())
		{
			ScriptObjectsForNaming forNaming;
			forNaming.number = script->GetScriptNumber();
			for (const auto &object : script->GetObjects())
			{
				forNaming.objects.push_back({ object->GetName(), !object->IsInstance(), object->IsPublic });
			}
			scripts.push_back(std::move(forNaming));
		}
		for (const auto &name : SuggestScriptNames(std::move(scripts)))
		{
			appState->GetResourceMap().AssignName(ResourceType::Script, name.first, NoBase36, name.second.c_str());
		}

		_PopulateScripts();
		// For some reason assigning names doesn't cause it to repaint, so invalidate:
		m_wndListScripts.Invalidate(FALSE);
	}
}

void DecompileDialog::OnBnClickedDecompilecancel()
{
	if (_decompileResults)
	{
		_decompileResults->SetAborted();
	}
}

void DecompileDialog::OnCancel()
{
	// The user is closing the dialog (Escape, the close box, or Cancel). Abort a
	// running decompile so the worker stops at its next check, rather than the
	// window's destruction blocking on -- and losing status posts to -- a worker
	// that keeps running to the end. (#53)
	if (_decompileResults)
	{
		_decompileResults->SetAborted();
	}
	__super::OnCancel();
}

void DecompileDialog::s_DecompileThreadWorker(DecompileDialog *pThis)
{
	// Plan step S4: the run of the command line. The dialog names the scripts
	// in game.ini itself (_AssignFilenames), and asks about the stale scripts,
	// so the run does neither.
	DecompileRunOptions options;
	options.engine.DebugControlFlow = pThis->_debugControlFlow;
	options.engine.DebugInstructionConsumption = pThis->_debugInstConsumption;
	options.engine.DebugFunctionMatch = (PCSTR)pThis->_debugFunctionMatch;
	options.engine.DecompileAsm = pThis->_debugAsm;
	options.engine.SubstituteTextTuples = pThis->_substituteTextTuples;
	options.names = NameAssignment::None;
	options.gameIni = GameIniNames::None;
	set<uint16_t> scriptNumbers = pThis->_scriptNumbers;
	DecompilerDialogResults &results = *pThis->_decompileResults;

	results.AddResult(DecompilerResultType::Update, "Creating script lookups...");
	DecompileStats stats;
	sci::Result<DecompileReport> report = RunDecompile(*pThis->_session, scriptNumbers, options, results);
	if (report)
	{
		// Found on the worker, so the UI thread does not read every source
		// file of the game. After a Cancel, the dialog offers no stale
		// script: a new run would start at once (the report lists the
		// scripts that the abort kept from a second write).
		results.SetStaleScripts(report->cancelled ? std::set<uint16_t>() : report->stale);
		stats = report->stats;
		if (report->cancelled)
		{
			results.AddResult(DecompilerResultType::Warning, "Decompile aborted");
		}
	}
	else
	{
		// Show the failure in the results; the stats below still run.
		results.AddResult(DecompilerResultType::Error, report.error().ToString());
	}

	// Stats reporting
	int divisor = max(1, (stats.functions + stats.fallbacks));	 // avoid / by zero
	int successPercentage = stats.functions * 100 / divisor;
	int divisorBytes = max(1, (stats.functionBytes + stats.fallbackBytes));	 // avoid / by zero
	int successBytesPercentage = stats.functionBytes * 100 / divisorBytes;
	results.AddResult(DecompilerResultType::Important,
		fmt::format("Decompiled {0} of {1} functions successfully ({2}%).", stats.functions, (stats.functions + stats.fallbacks), successPercentage)
		);

	results.AddResult(DecompilerResultType::Important,
		fmt::format("Overall bytecount success rate: {0}%.", successBytesPercentage)
		);

	if (stats.fallbacks)
	{
		results.AddResult(DecompilerResultType::Important, "Fell back to assembly for the remaining functions.");
	}
}

void DecompilerDialogResults::AddResult(DecompilerResultType type, const std::string &message)
{
	std::string *ptrToString = new std::string(message);
	// UpdateStatus deletes the string when it handles the message. If the window
	// is gone (the dialog was closed while the worker ran), the post fails and the
	// message is never handled, so delete the string here to avoid a leak. (#53)
	if (!::PostMessage(_hwnd, UWM_UPDATESTATUS, static_cast<WPARAM>(type), reinterpret_cast<LPARAM>(ptrToString)))
	{
		delete ptrToString;
	}
}


LRESULT DecompileDialog::UpdateStatus(WPARAM wParam, LPARAM lParam)
{
	DecompilerResultType type = static_cast<DecompilerResultType>(wParam);
	std::string *stringPtr = reinterpret_cast<std::string*>(lParam);

	std::string preamble;
	if (type == DecompilerResultType::Warning)
	{
		preamble = "WARNING: ";
	}
	else if (type == DecompilerResultType::Error)
	{
		preamble = "ERROR: ";
	}
	std::string display = fmt::format("{0}{1}",
		preamble,
		*stringPtr
		);

	if (type == DecompilerResultType::Update)
	{
		m_wndStatus.SetWindowTextA(display.c_str());
	}
	else
	{
		CString str;
		m_wndResults.GetWindowTextA(str);
		if (!str.IsEmpty())
		{
			str.Append("\r\n");
		}
		str.Append(display.c_str());
		m_wndResults.SetWindowTextA(str);
		m_wndResults.LineScroll(m_wndResults.GetLineCount());
	}
	delete stringPtr;
	return 0;
}


void DecompileDialog::OnBnClickedClearsco()
{
	// Get a list of scripts to clear SCOs
	vector<string> scosDeleted;
	set<uint16_t> scriptsToUpdate;
	POSITION pos = m_wndListScripts.GetFirstSelectedItemPosition();
	while (pos != nullptr)
	{
		int selectedItem = m_wndListScripts.GetNextSelectedItem(pos);
		uint16_t scriptNumber = (uint16_t)m_wndListScripts.GetItemData(selectedItem);
		string scoFilename = _helper.GetScriptObjectFileName(scriptNumber);
		try
		{
			deletefile(scoFilename);
			scosDeleted.push_back(scoFilename);
			scriptsToUpdate.insert(scriptNumber);
		}
		catch (std::exception &e)
		{
			AfxMessageBox(e.what(), MB_OK | MB_ICONWARNING);
			break;
		}
	}

	if (!scosDeleted.empty())
	{
		if (scosDeleted.size() == 1)
		{
			m_wndResults.SetWindowTextA(fmt::format("Deleted {0}", scosDeleted[0]).c_str());
		}
		else
		{
			m_wndResults.SetWindowTextA(fmt::format("Deleted {0} .sco files", scosDeleted.size()).c_str());
		}
		_SyncSelection(true);
	}
	_UpdateScripts(scriptsToUpdate);
}

void DecompileDialog::_SelectAll(bool select)
{
	m_wndListScripts.SetRedraw(FALSE);
	int itemCount = m_wndListScripts.GetItemCount();
	for (int i = 0; i < itemCount; i++)
	{
		m_wndListScripts.SetItemState(i, select ? LVIS_SELECTED : 0, LVIS_SELECTED);
	}
	m_wndListScripts.SetRedraw(TRUE);
}

void DecompileDialog::_SelectScripts(const std::set<uint16_t> &scriptNumbers)
{
	// A partial selection: keep the "select all" box in step with it.
	m_wndSelectAll.SetCheck(BST_UNCHECKED);
	m_wndListScripts.SetRedraw(FALSE);
	int itemCount = m_wndListScripts.GetItemCount();
	for (int i = 0; i < itemCount; i++)
	{
		uint16_t scriptNumber = (uint16_t)m_wndListScripts.GetItemData(i);
		bool select = (scriptNumbers.find(scriptNumber) != scriptNumbers.end());
		m_wndListScripts.SetItemState(i, select ? LVIS_SELECTED : 0, LVIS_SELECTED);
	}
	m_wndListScripts.SetRedraw(TRUE);
}

void DecompileDialog::OnBnClickedCheckselectall()
{
	_SelectAll(m_wndSelectAll.GetCheck() == BST_CHECKED);
}
