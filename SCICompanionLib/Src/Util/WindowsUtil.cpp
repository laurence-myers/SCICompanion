#include "stdafx.h"
#include "WindowsUtil.h"

// The helpers of util.cpp that need a window. They are in the GUI library.

#define VK_A		65
#define VK_C		67
#define VK_V		86
#define VK_X		88
#define VK_Z		90

//
// Given a stringstream, put it into a temporary text file and show it.
//
void ShowTextFile(PCSTR pszContent, const std::string &filename)
{
	std::string actualPath = MakeTextFile(pszContent, filename);
	ShowFile(actualPath);
}

void ShowFile(const std::string &actualPath)
{
	if (!actualPath.empty())
	{
		bool fError = true;
		if (((INT_PTR)ShellExecute(AfxGetMainWnd()->GetSafeHwnd(), "open", actualPath.c_str(), NULL, NULL, SW_SHOWNORMAL)) > 32)
		{
			fError = false;
		}

		if (fError)
		{
			char szMsg[200];
			DWORD_PTR arg = (DWORD_PTR)actualPath.c_str();
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY, 0, GetLastError(), 0, szMsg, ARRAYSIZE(szMsg), (va_list*)&arg);
			AfxMessageBox(szMsg, MB_OK | MB_APPLMODAL);
		}
	}
}

BOOL HandleEditBoxCommands(MSG* pMsg, CEdit &wndEdit)
{
	BOOL fRet = FALSE;
	if (!fRet)
	{
		if ((pMsg->message >= WM_KEYFIRST) && (pMsg->message <= WM_KEYLAST))
		{
			// Fwd the delete key to the edit control
			if ((pMsg->message != WM_CHAR) && (pMsg->wParam == VK_DELETE))
			{
				::SendMessage(wndEdit.GetSafeHwnd(), pMsg->message, pMsg->wParam, pMsg->lParam);
				fRet = TRUE; // Don't dispatch message, we handled it.
			}
		}
	}
	if (!fRet)
	{
		if (pMsg->message == WM_KEYDOWN)
		{
			if (GetKeyState(VK_CONTROL) & 0x8000)
			{
				if (pMsg->wParam == VK_C)
				{
					wndEdit.Copy();
					fRet = TRUE;
				}
				if (pMsg->wParam == VK_V)
				{
					wndEdit.Paste();
					fRet = TRUE;
				}
				if (pMsg->wParam == VK_X)
				{
					wndEdit.Cut();
					fRet = TRUE;
				}
				if (pMsg->wParam == VK_Z)
				{
					wndEdit.Undo();
					fRet = TRUE;
				}
				if (pMsg->wParam == VK_A)
				{
					wndEdit.SetSel(0xffff0000);
					fRet = TRUE;
				}
			}
		}
	}
	return fRet;
}

// Decide whether the compile-dialog pump should dispatch a pumped message.
// Always dispatch paint: an undispatched WM_PAINT is returned again and again
// (it clears only when the window validates), so skipping one would spin the
// pump. Otherwise dispatch only the dialog's own input, so its Cancel button
// stays live; drop input aimed at other windows, whose command handlers could
// re-enter the resource map while the compile batches appends. (#55)
bool ShouldDispatchCompilePumpMessage(const MSG &msg, HWND hDialog)
{
	if (msg.message == WM_PAINT)
	{
		return true;
	}
	if (hDialog == NULL)
	{
		return false;
	}
	return (msg.hwnd == hDialog) || (::IsChild(hDialog, msg.hwnd) != FALSE);
}

// Pump paint and input while a compile runs, but dispatch only the messages
// ShouldDispatchCompilePumpMessage allows.
//
// Compile All drives itself with a self-reposted UWM_STARTCOMPILE. A posted
// message outranks queued hardware input in GetMessage, so the modal loop would
// service the repost forever and never dispatch a Cancel click -- the operation
// would be uncancellable. A PeekMessage that includes PM_QS_INPUT pulls that
// input out of the queue regardless of the pending posted message, which is what
// keeps Cancel responsive. Dispatch is gated so a foreign command cannot run
// re-entrantly. Dropping foreign input is safe: DoModal disables the owner, so
// the dialog is the only window the user can drive.
//
// A PeekMessage filtered to paint and input does not surface WM_QUIT (it is in
// neither category), so a pending quit simply stays in the queue for the modal
// loop, which ends DoModal -- it is not dispatched and lost. The WM_QUIT branch
// is therefore defensive: on any platform that does surface WM_QUIT here, repost
// it with PostQuitMessage and return true so the caller stops. Returns false when
// the queue drains normally. (#55)
bool PumpCompileDialogMessagesQuitPending(HWND hDialog)
{
	MSG msg;
	while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_QS_PAINT | PM_QS_INPUT))
	{
		if (msg.message == WM_QUIT)
		{
			::PostQuitMessage((int)msg.wParam);
			return true;
		}
		if (ShouldDispatchCompilePumpMessage(msg, hDialog))
		{
			::DispatchMessage(&msg);
		}
	}
	return false;
}
