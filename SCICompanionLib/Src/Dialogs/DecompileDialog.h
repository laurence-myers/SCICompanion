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
#include "afxwin.h"
#include "GameFolderHelper.h"
#include "DecompilerResults.h"
#include <future>
#include <atomic>

class CSCOFile;
class IDecompilerConfig;

class DecompilerDialogResults : public IDecompilerResults
{
public:
	DecompilerDialogResults(HWND hwnd) : _aborted(false), _hwnd(hwnd), _successCount(0), _fallbackCount(0), _successBytes(0), _fallbackBytes(0), _globalsUpdated(false) {}
	void AddResult(DecompilerResultType type, const std::string &message) override;
	void InformStats(bool functionSuccessful, int byteCount) override;
	bool IsAborted() override { return _aborted; }
	void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &mainDirtyRenames) { _globalsUpdated = mainDirtyRenames; };

	void SetAborted() { _aborted = true; }

	std::vector<std::pair<std::string, std::string>> GetUpdatedGlobalsList() { return _globalsUpdated; }

	// The scripts decompiled in an earlier pass that still use a renamed global
	// by its old name. The worker finds them; the UI thread reads them once the
	// worker is done.
	void SetStaleScripts(const std::set<uint16_t> &staleScripts) { _staleScripts = staleScripts; }
	const std::set<uint16_t> &GetStaleScripts() const { return _staleScripts; }

	// Stats
	int _successCount;
	int _fallbackCount;
	int _successBytes;
	int _fallbackBytes;

private:
	// Written by the UI thread (SetAborted) and read by the worker thread
	// (IsAborted), so it must be atomic. (#53)
	std::atomic<bool> _aborted;
	HWND _hwnd;
	std::vector<std::pair<std::string, std::string>> _globalsUpdated;
	std::set<uint16_t> _staleScripts;
};

class DecompileDialog : public CExtResizableDialog
{
public:
	DecompileDialog(CWnd* pParent = nullptr);   // standard constructor
	~DecompileDialog();                         // joins the worker before members are torn down (#53)

	// Dialog Data
	enum { IDD = IDD_DECOMPILER };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	BOOL PreTranslateMessage(MSG* pMsg) override;
	BOOL OnInitDialog() override;
	void OnCancel() override; // aborts a running decompile before closing (#53)

	DECLARE_MESSAGE_MAP()
private:
	CExtEdit m_wndScript;
	CExtEdit m_wndResults;
	CExtEdit m_wndStatus;
	CListCtrl m_wndListScripts;

	// Visuals
	CExtButton m_wndCancel;
	CExtButton m_wndDecompile;
	CExtButton m_wndDecomileCancel;
	CExtButton m_wndClearSCO;
	CExtButton m_wndSetFilenames;
	CProgressCtrl m_wndProgress;

	CExtCheckBox m_wndDebugControlFlow;
	CExtCheckBox m_wndDebugInstConsumption;
	CExtEdit m_wndDebugFunctionMatch;
	CExtLabel m_wndSCOLabel;

	CExtCheckBox m_wndAsm;

	CExtCheckBox m_wndSelectAll;
	CExtCheckBox m_wndRedecompile;
	CExtCheckBox m_wndTextTuples;
	CExtGroupBox m_wndGroupOptions;
	CExtGroupBox m_wndGroupDebug;

	// Our own copy of this
	GameFolderHelper _helper;

	CTreeCtrl m_wndTreeSCO;
	std::unique_ptr<CSCOFile> _sco;
	std::vector<int> _scoPublicProcIndices; // Since sco exports include both instances and procs, but we can only edit proc names.
	bool _inSCOLabelEdit;
	bool _inScriptListLabelEdit;
	bool _needsSetFilenames;

	int previousSelection;
	bool initialized;

	void _SelectAll(bool select);
	void _SelectScripts(const std::set<uint16_t> &scriptNumbers);
	void _PopulateScripts();
	void _UpdateScripts(std::set<uint16_t> updatedScripts);
	void _PopulateSCOTree();
	void _InitScriptList();
	void _SyncSelection(bool force = false);
	void _SyncButtonState();

	static void s_DecompileThreadWorker(DecompileDialog *pThis);
	void OnTimer(UINT_PTR nIDEvent);
	LRESULT UpdateStatus(WPARAM wParam, LPARAM lParam);
	std::unique_ptr<DecompilerDialogResults> _decompileResults;
	std::unique_ptr<std::future<void>> _future;

	std::set<uint16_t> _scriptNumbers;
	bool _debugControlFlow;
	bool _debugInstConsumption;
	bool _debugAsm;
	bool _substituteTextTuples;
	CString _debugFunctionMatch;
	std::unique_ptr<GlobalCompiledScriptLookups> _lookups;
	std::unique_ptr<IDecompilerConfig> _decompilerConfig;
	void _AssignFilenames();

	bool _syncSelection;

public:
	afx_msg void OnLvnItemchangedListscripts(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnTvnBeginlabeleditTreesco(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnTvnEndlabeleditTreesco(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnBeginlabeleditListscripts(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnEndlabeleditListscripts(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnBnClickedDecompile();
	afx_msg void OnBnClickedAssignfilenames();
	afx_msg void OnBnClickedDecompilecancel();
	afx_msg void OnBnClickedClearsco();
	afx_msg void OnBnClickedCheckselectall();
};
