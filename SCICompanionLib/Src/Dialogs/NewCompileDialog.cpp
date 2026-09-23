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
// CompileDialog.cpp : implementation file
//

#include "stdafx.h"
#include "AppState.h"
#include "NewCompileDialog.h"
#include "WindowsUtil.h"
#include "DependencyTracker.h"
#include "ClassBrowser.h"

#define UWM_STARTCOMPILE (WM_APP + 0)

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// CNewCompileDialog dialog

CNewCompileDialog::CNewCompileDialog(CompileBatch &batch, CWnd* pParent /*=NULL*/)
	: CExtResizableDialog(CNewCompileDialog::IDD, pParent), _batch(batch), _abort(false)
{
	_fDone = false;
}

CNewCompileDialog::~CNewCompileDialog()
{
}

void CNewCompileDialog::OnScriptStart(size_t index, size_t count, const ScriptId &script)
{
	_current = script;
	m_wndProgress.SetPos((int)index);
	// Update the edit control with the current scripts name.
	m_wndDisplay.SetWindowText(script.GetTitle().c_str());
}

void CNewCompileDialog::OnScriptDone(const ScriptOutcome &outcome)
{
	if (outcome.status)
	{
		// The caller clears it in the dependency tracker after the commit
		// (review of S2c: before, a commit that wrote nothing left it clear).
		_compiled.push_back(_current);
	}
	// The compile is done.  Post the results.
	std::vector<CompileResult> results = outcome.diagnostics;
	appState->OutputAddBatch(OutputPaneType::Compile, results);
}

LRESULT CNewCompileDialog::CompileAll(WPARAM wParam, LPARAM lParam)
{
	ShowWindow(SW_SHOW);

	// Pump paint and input so the display and progress controls repaint and the
	// Cancel button stays responsive, but dispatch only this dialog's own
	// messages. Dispatching a foreign command mid-compile could re-enter the
	// resource map while the compile batches appends. A plain modal loop cannot
	// keep Cancel alive here, because the self-reposted UWM_STARTCOMPILE outranks
	// queued input; pumping PM_QS_INPUT is what pulls the Cancel click out. Stop
	// if a quit is pending, instead of dispatching (and losing) the WM_QUIT. (#55)
	if (PumpCompileDialogMessagesQuitPending(GetSafeHwnd()))
	{
		_fDone = true;
		return 0;
	}

	// One script of the batch. False after the last script, or when Cancel
	// set the abort flag.
	bool more = false;
	{
		// The class browser's background reload parses the same scripts and
		// reads the game, so hold its lock for the compile.
		ClassBrowserLock lock(appState->GetClassBrowser());
		lock.Lock();
		more = _batch.Step(_abort, *this);
	}

	if (more)
	{
		PostMessage(UWM_STARTCOMPILE, 0, 0); // Start another compile
	}
	else
	{
		_fDone = true;
		if (_abort)
		{
			OnCancel();
		}
		else
		{
			// Change the text to close:
			SetDlgItemText(IDCANCEL, "Close");
			// Actually, just close ourselves
			PostMessage(WM_CLOSE, 0, 0);
		}
	}
	return 0;
}

void CNewCompileDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);

	DDX_Control(pDX, IDC_PROGRESS, m_wndProgress);
	DDX_Control(pDX, IDC_EDIT, m_wndDisplay);

	// Visuals
	DDX_Control(pDX, IDCANCEL, m_wndCancel);
}

BOOL CNewCompileDialog::OnInitDialog()
{
	BOOL fRet = __super::OnInitDialog();
	ShowSizeGrip(FALSE);
	// Set the range of the progress control.
	m_wndProgress.SetRange32(0, (int)_batch.Count());
	PostMessage(UWM_STARTCOMPILE, 0, 0);
	return fRet;
}

void CNewCompileDialog::OnCancel()
{
	if (!_fDone)
	{
		// We're still doing stuff.  Signal ourself to close.
		_abort = true;
	}
	else
	{
		__super::OnCancel();
	}
}

BEGIN_MESSAGE_MAP(CNewCompileDialog, CExtResizableDialog)
	ON_MESSAGE(UWM_STARTCOMPILE, CompileAll)
END_MESSAGE_MAP()


// CNewCompileDialog message handlers
