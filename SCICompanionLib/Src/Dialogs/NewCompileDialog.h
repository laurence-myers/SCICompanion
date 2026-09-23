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
#pragma once

#include "CompileBatch.h"
#include <atomic>
#include <string>
#include <unordered_set>
#include <vector>

class CResourceMap;

// The progress of a compile batch (plan step S2): one script for each
// UWM_STARTCOMPILE message, so the window paints and Cancel works. The caller
// starts the batch before the dialog and finishes it after the dialog.

class CNewCompileDialog : public CExtResizableDialog, public ICompileEvents
{
public:
	CNewCompileDialog(CompileBatch &batch, CWnd* pParent = NULL);   // standard constructor
	virtual ~CNewCompileDialog();
	virtual void OnCancel();

	// ICompileEvents
	void OnScriptStart(size_t index, size_t count, const ScriptId &script) override;
	void OnScriptDone(const ScriptOutcome &outcome) override;

// Dialog Data
	enum { IDD = IDD_COMPILEDIALOG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	LRESULT CompileAll(WPARAM wParam, LPARAM lParam);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()

	CExtProgressWnd m_wndProgress;
	CExtEdit m_wndDisplay;
	CompileBatch &_batch;
	std::atomic<bool> _abort;
	bool _fDone;
	// The script that compiles now.
	ScriptId _current;

	// Visuals
	CExtButton m_wndCancel;
};

// The scripts of game.ini, or the ones with these lower-case titles. When
// there is none, it offers to scan the src folder for .sc files.
std::vector<ScriptId> ScriptsToCompile(CResourceMap &resourceMap, const std::unordered_set<std::string> &titles);

// Plan step S2: the GUI asks before a package save that a patch file would
// hide. Yes: move the patch files aside (Replace). No: keep them (Ignore).
// Cancel: stop, and write nothing (Refuse).
ShadowPolicy AskAboutShadowingPatches(const std::vector<std::string> &files);

// The lines of a finished batch after the lines of its scripts: the table
// save, the commit, the moved patch files and the warnings.
void ReportCompileBatch(const CompileReport &report, ICompileLog &log, const std::string &writeProblem);
