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
#include "Task.h"
#include <atomic>
#include <memory>
#include <chrono>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace IntegrationHarness;

namespace UnitTests
{
    // #53: CWndTaskSink used std::async, whose future blocked the UI thread in its
    // destructor, and Abandon() was a no-op -- so a closed dialog's worker kept
    // running and posted to a destroyed window. It now runs a detached worker with
    // heap-owned state and a gated post: the destructor does not block, and a
    // worker that finishes after Abandon does not post.
    TEST_CLASS(TestWndTaskSinkIntegration)
    {
        static const UINT UWM_TESTDONE = WM_APP + 200;

    public:
        // Normal completion: the worker runs, posts its completion message, and
        // GetResponse returns its result.
        TEST_METHOD(WndTaskSink_CompletesPostsAndReturnsResult)
        {
            MessageOnlyWindow win;
            CWnd *pWnd = CWnd::FromHandle(win.Handle());
            CWndTaskSink<int> sink(pWnd, UWM_TESTDONE);
            sink.StartTask([]() { return 42; });

            bool got = false;
            for (int i = 0; (i < 400) && !got; i++)
            {
                win.Pump();
                got = win.ReceivedMessage(UWM_TESTDONE);
                if (!got) { ::Sleep(5); }
            }
            Assert::IsTrue(got, L"the worker must post its completion message");
            Assert::AreEqual(42, sink.GetResponse(), L"GetResponse returns the worker's result");
        }

        // Abandon (as the destructor does when the dialog closes) must stop the
        // worker from posting, even though the window is still alive. Before the
        // fix, Abandon was a no-op and the worker posted regardless.
        TEST_METHOD(WndTaskSink_AbandonSuppressesPost)
        {
            MessageOnlyWindow win;
            CWnd *pWnd = CWnd::FromHandle(win.Handle());
            auto started = std::make_shared<std::atomic<bool>>(false);
            auto release = std::make_shared<std::atomic<bool>>(false);

            CWndTaskSink<int> sink(pWnd, UWM_TESTDONE);
            sink.StartTask([started, release]()
            {
                started->store(true);
                while (!release->load()) { ::Sleep(1); }
                return 7;
            });

            for (int i = 0; (i < 2000) && !started->load(); i++) { ::Sleep(1); }
            Assert::IsTrue(started->load(), L"the worker started");

            sink.Abandon();       // null the hwnd (the window is still alive)
            release->store(true); // let the worker finish; it must not post

            for (int i = 0; i < 200; i++) { win.Pump(); ::Sleep(5); }
            Assert::IsFalse(win.ReceivedMessage(UWM_TESTDONE),
                L"an abandoned worker must not post, even to a live window");
        }

        // The destructor must not block until the worker finishes (the UI-freeze
        // bug). The worker sleeps 2s; the sink is destroyed while it runs; the
        // destructor must return promptly. Before the fix, the future's destructor
        // blocked for the full 2s.
        TEST_METHOD(WndTaskSink_DestructorDoesNotBlock)
        {
            MessageOnlyWindow win;
            CWnd *pWnd = CWnd::FromHandle(win.Handle());
            auto started = std::make_shared<std::atomic<bool>>(false);

            auto t0 = std::chrono::steady_clock::now();
            {
                CWndTaskSink<int> sink(pWnd, UWM_TESTDONE);
                sink.StartTask([started]()
                {
                    started->store(true);
                    ::Sleep(2000);
                    return 7;
                });
                for (int i = 0; (i < 2000) && !started->load(); i++) { ::Sleep(1); }
                Assert::IsTrue(started->load(), L"the worker started");
                // The sink is destroyed here while the worker still runs.
            }
            long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();

            Assert::IsTrue(elapsedMs < 1500,
                L"the destructor must not block until the worker finishes");
        }
    };
}
