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
#include "ResourceMap.h"
#include "format.h"
#include <filesystem>
#include <regex>

using namespace std::filesystem;

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
		appState->GetDependencyTracker().ClearScript(_current);
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

std::vector<ScriptId> ScriptsToCompile(CResourceMap &resourceMap, const std::unordered_set<std::string> &titles)
{
	std::vector<ScriptId> scripts;
	std::vector<ScriptId> all;
	resourceMap.GetAllScripts(all);
	std::copy_if(all.begin(), all.end(), std::back_inserter(scripts),
		[&](const ScriptId &scriptId)
	{
		return titles.empty() || (titles.find(scriptId.GetTitleLower()) != titles.end());
	}
	);

	if (scripts.empty())
	{
		if (IDYES == AfxMessageBox("Error finding scripts to compile.\nDo you want to try scanning the src folder for scripts?", MB_YESNO | MB_APPLMODAL | MB_ICONEXCLAMATION))
		{
			path enumPath = resourceMap.Helper().GetSrcFolder();
			auto matchRSTRegex = std::regex("(\\w+)\\.sc$");
			std::error_code ec;
			for (auto it = directory_iterator(enumPath, ec); !ec && (it != directory_iterator()); it.increment(ec))
			{
				const auto &file = it->path();
				std::smatch sm;
				std::string temp = file.filename().string();
				std::error_code notADirectory;
				if (!it->is_directory(notADirectory) && std::regex_search(temp, sm, matchRSTRegex) && (sm.size() > 1))
				{
					scripts.push_back(ScriptId(file.string()));
				}
			}
			if (scripts.empty())
			{
				AfxMessageBox("Could not find any .sc files.", MB_OK | MB_ICONERROR);
			}
		}
	}
	return scripts;
}

ShadowPolicy AskAboutShadowingPatches(const std::vector<std::string> &files)
{
	const size_t shownFiles = 10;
	std::string list;
	for (size_t i = 0; (i < files.size()) && (i < shownFiles); i++)
	{
		list += files[i] + "\n";
	}
	if (files.size() > shownFiles)
	{
		list += fmt::format("(and {0} more)\n", files.size() - shownFiles);
	}
	std::string text = fmt::format(
		"These patch files would hide the compiled resources in the package. The game and SCI Companion read a patch file before the package.\n\n"
		"{0}\n"
		"Yes: move the patch files to the folder replaced-patches in the game folder.\n"
		"No: keep the patch files. The compiled resources in the package stay hidden.\n"
		"Cancel: stop, and write nothing.",
		list);
	switch (AfxMessageBox(text.c_str(), MB_YESNOCANCEL | MB_ICONWARNING | MB_APPLMODAL))
	{
	case IDYES:
		return ShadowPolicy::Replace;
	case IDNO:
		return ShadowPolicy::Ignore;
	default:
		return ShadowPolicy::Refuse;
	}
}

void ReportCompileBatch(const CompileReport &report, ICompileLog &log, const std::string &writeProblem)
{
	if (!report.tables)
	{
		log.ReportResult(CompileResult("There was a problem saving the class and selector tables: " + report.tables.error().ToString(), CompileResult::CRT_Error));
	}
	if (!report.commit)
	{
		log.ReportResult(CompileResult(writeProblem + report.commit.error().ToString(), CompileResult::CRT_Error));
	}
	for (const std::string &moved : report.movedPatches)
	{
		log.ReportResult(CompileResult("Moved the patch file " + moved));
	}
	for (const std::string &warning : report.warnings)
	{
		log.ReportResult(CompileResult("Warning: " + warning, CompileResult::CRT_Warning));
	}
}
