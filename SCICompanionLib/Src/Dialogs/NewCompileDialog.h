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
#include "CompileBatchGui.h"
#include <atomic>
#include <vector>

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

	// The scripts that compiled.
	const std::vector<ScriptId> &CompiledScripts() const { return _compiled; }

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
	std::vector<ScriptId> _compiled;

	// Visuals
	CExtButton m_wndCancel;
};
