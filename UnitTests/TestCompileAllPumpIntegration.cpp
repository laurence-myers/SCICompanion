/***************************************************************************
    Copyright (c) 2026 Philip Fortier

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
#include "CppUnitTest.h"
#include "IntegrationHarness.h"
#include "WindowsUtil.h"
#include <atomic>
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace IntegrationHarness;

namespace UnitTests
{
    // #55: Compile All pumped the message queue with PM_QS_PAINT | PM_QS_INPUT and
    // dispatched everything, so a command routed to another window ran mid-compile
    // and re-entered the resource map. The naive fix (pump PM_QS_PAINT only) fixed
    // that but broke cancellation: Compile All drives itself with a self-reposted
    // UWM_STARTCOMPILE, a posted message that outranks queued input in GetMessage,
    // so a plain modal loop never dispatches the Cancel click -- only a pump that
    // includes PM_QS_INPUT pulls it out. The real fix pumps paint AND input but
    // dispatches only the dialog's own messages (ShouldDispatchCompilePumpMessage):
    // Cancel stays live, foreign commands are dropped.
    TEST_CLASS(TestCompileAllPumpIntegration)
    {
    public:
        // The dispatch decision is the heart of the fix. Foreign input must be
        // dropped (the #55 re-entrancy fix); the dialog's own input must be
        // dispatched (what keeps Cancel alive -- the half the paint-only pump lost);
        // paint must always be dispatched, or an un-validated WM_PAINT would be
        // returned forever and spin the pump.
        // Destroys a top-level window on scope exit, so a failing Assert does not
        // leak the test's windows (CppUnitTest throws out of the assert).
        struct WindowGuard
        {
            HWND h;
            ~WindowGuard() { if (h) { ::DestroyWindow(h); } }
        };

        TEST_METHOD(ShouldDispatch_OwnInputAndPaint_DropsForeign)
        {
            HWND hDlg = ::CreateWindowEx(0, TEXT("STATIC"), TEXT("dlg"), WS_OVERLAPPED,
                0, 0, 10, 10, NULL, NULL, NULL, NULL);
            WindowGuard dlgGuard{ hDlg };               // also destroys its child
            HWND hChild = ::CreateWindowEx(0, TEXT("STATIC"), TEXT("child"), WS_CHILD,
                0, 0, 1, 1, hDlg, NULL, NULL, NULL);
            HWND hForeign = ::CreateWindowEx(0, TEXT("STATIC"), TEXT("foreign"), WS_OVERLAPPED,
                0, 0, 10, 10, NULL, NULL, NULL, NULL);
            WindowGuard foreignGuard{ hForeign };
            Assert::IsNotNull(hDlg, L"setup: dialog window");
            Assert::IsNotNull(hChild, L"setup: child window");
            Assert::IsNotNull(hForeign, L"setup: foreign window");

            MSG msg;
            ZeroMemory(&msg, sizeof(msg));

            // Foreign input is dropped: a command routed elsewhere must not run
            // mid-compile and re-enter the resource map.
            msg.hwnd = hForeign; msg.message = WM_LBUTTONDOWN;
            Assert::IsFalse(ShouldDispatchCompilePumpMessage(msg, hDlg),
                L"input for another window must not be dispatched");

            // The dialog's own input is dispatched: this is what keeps Cancel live.
            msg.hwnd = hDlg; msg.message = WM_KEYDOWN;
            Assert::IsTrue(ShouldDispatchCompilePumpMessage(msg, hDlg),
                L"the dialog's own input must be dispatched");

            // A child control's input (the Cancel button is a child) is dispatched.
            msg.hwnd = hChild; msg.message = WM_LBUTTONUP;
            Assert::IsTrue(ShouldDispatchCompilePumpMessage(msg, hDlg),
                L"a child control's input must be dispatched");

            // Paint is always dispatched, even for a foreign window.
            msg.hwnd = hForeign; msg.message = WM_PAINT;
            Assert::IsTrue(ShouldDispatchCompilePumpMessage(msg, hDlg),
                L"paint must always be dispatched, or the pump spins");

            // A null dialog (no window yet) drops all non-paint messages.
            msg.hwnd = hForeign; msg.message = WM_KEYDOWN;
            Assert::IsFalse(ShouldDispatchCompilePumpMessage(msg, NULL),
                L"with no dialog, non-paint input is dropped");
            // Windows are destroyed by the WindowGuards on scope exit.
        }

        // A pending WM_QUIT must not be lost by the pump. It runs on a
        // DeadlineRunner worker thread whose message queue is private, so posting a
        // quit there cannot affect the test runner; shared state is heap-owned so a
        // worker detached on timeout still writes only to live memory.
        TEST_METHOD(PumpCompile_WmQuit_NotLost)
        {
            struct Shared
            {
                std::atomic<bool> preserved{ false };
                std::atomic<int> quitCode{ -1 };
            };
            auto shared = std::make_shared<Shared>();

            DeadlineRunner runner;
            bool finished = runner.Run(3000, [shared]()
            {
                ::PostQuitMessage(4242);
                bool quitPending = PumpCompileDialogMessagesQuitPending(NULL);
                // WM_QUIT survives either way: reposted by the pump (if this Windows
                // surfaces it through the filter) or still queued for the modal loop
                // (current Windows does not surface it). The old code lost it.
                MSG msg;
                bool stillQueued = (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) != FALSE) &&
                    (msg.message == WM_QUIT);
                shared->preserved = quitPending || stillQueued;
                shared->quitCode = stillQueued ? (int)msg.wParam : (quitPending ? 4242 : -1);
            });

            Assert::IsTrue(finished, L"the pump body must finish within the deadline");
            Assert::IsFalse(runner.ThrewException(), L"the pump body must not throw");
            Assert::IsTrue(shared->preserved.load(), L"a pending WM_QUIT must not be lost by the pump");
            Assert::AreEqual(4242, shared->quitCode.load(), L"the quit exit code must be preserved");
        }
    };
}
