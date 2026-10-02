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
// RemoveScriptDialog.cpp : implementation file
//

#include "stdafx.h"
#include "AppState.h"
#include "RemoveScriptDialog.h"
#include "ResourceBlob.h"
#include "ResourceMapOperations.h"


// CRemoveScriptDialog dialog

CRemoveScriptDialog::CRemoveScriptDialog(WORD wScript, CWnd* pParent /*=NULL*/)
	: CExtResizableDialog(CRemoveScriptDialog::IDD, pParent), _wScript(wScript), _fAlsoDelete(true)
{
}

CRemoveScriptDialog::~CRemoveScriptDialog()
{
}

void CRemoveScriptDialog::DoDataExchange(CDataExchange* pDX)
{
	__super::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_CHECKDELETE, m_wndCheckDelete);
	DDX_Control(pDX, IDC_STATICQUESTION, m_wndStaticQuestion);

	// Visuals
	DDX_Control(pDX, IDOK, m_wndYes);
	DDX_Control(pDX, IDCANCEL, m_wndNo);
}

BOOL CRemoveScriptDialog::OnInitDialog()
{
	BOOL fRet = __super::OnInitDialog();
	
	// Ask the question
	CResourceMap &rm = AppResourceMap();
	std::string scriptTitle = rm.Helper().GetIniString("Script", default_reskey(_wScript, NoBase36));
	ScriptId script = rm.Helper().GetScriptId(scriptTitle);
	std::stringstream ss;
	ss << "Do you want to remove " << script.GetTitle() << " from the game completely?";
	m_wndStaticQuestion.SetWindowText(ss.str().c_str());

	// And if the file exists, ask if it should be deleted.
	if (PathFileExists(script.GetFullPath().c_str()))
	{
		std::stringstream ss;
		ss << "Also delete script file: " << script.GetFullPath();
		m_wndCheckDelete.SetWindowText(ss.str().c_str());
	}
	else
	{
		m_wndCheckDelete.ShowWindow(SW_HIDE);
	}

	ShowSizeGrip(FALSE);
	return fRet;
}

BEGIN_MESSAGE_MAP(CRemoveScriptDialog, CExtResizableDialog)
	ON_COMMAND(IDOK, OnOK)
END_MESSAGE_MAP()


// CRemoveScriptDialog message handlers
void CRemoveScriptDialog::OnOK()
{
	_fAlsoDelete = (m_wndCheckDelete.GetCheck() != 0);
	__super::OnOK();
}

// Asks whether to remove the script from game.ini, and to delete its heap
// and its source file too. AppState calls it when the last copy of a script
// resource was deleted.
void AskToRemoveScript(CResourceMap &resourceMap, const ResourceBlob &data)
{
	const GameFolderHelper &helper = resourceMap.Helper();
	CRemoveScriptDialog dialog(static_cast<WORD>(data.GetNumber()));
	if (IDOK == dialog.DoModal())
	{
		// Remove it from the ini
		std::string iniKey = default_reskey(data.GetNumber(), data.GetHeader().Base36Number);
		std::string scriptTitle = helper.GetIniString("Script", iniKey);
		ScriptId scriptId = resourceMap.Helper().GetScriptId(scriptTitle);
		// First, remove from the [Script] section
		WritePrivateProfileString("Script", iniKey.c_str(), nullptr, helper.GetGameIniFileName().c_str());
		// Second, remove from the [Language] section
		WritePrivateProfileString("Language", scriptTitle.c_str(), nullptr, helper.GetGameIniFileName().c_str());
		if (dialog.AlsoDelete())
		{
			// Remove the heap too
			std::unique_ptr<ResourceBlob> theHeapOne = resourceMap.MostRecentResource(ResourceType::Heap, data.GetNumber(), false);
			if (theHeapOne)
			{
				DeleteResource(resourceMap, *theHeapOne);
			}
			if (!DeleteFile(scriptId.GetFullPath().c_str()))
			{
				char szMessage[MAX_PATH * 2];
				char szReason[200];
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, (DWORD)GetLastError(), 0, szReason, ARRAYSIZE(szReason), nullptr);
				StringCchPrintf(szMessage, ARRAYSIZE(szMessage), "Couldn't delete script file.\n%s", szReason);
				AfxMessageBox(szMessage, MB_OK | MB_ICONEXCLAMATION | MB_APPLMODAL);
			}
		}
	}
}
