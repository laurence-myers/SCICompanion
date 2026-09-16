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
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace IntegrationHarness;

// These tests prove the integration harness end to end. The test CLASS name
// carries "Integration", which is how RunTests.ps1 filters them: the default
// unit run excludes FullyQualifiedName~Integration and -Integration runs only
// it. (The TestCategory trait on each method is for Test Explorer grouping; the
// C++ adapter does not filter vstest on it.) They are green: each exercises a
// harness capability a real per-issue test (for #46-#50, #53, #66) builds on.

namespace UnitTests
{
    TEST_CLASS(TestIntegrationHarness)
    {
    public:
        // The watchdog reports completion for a prompt body and a timeout for a
        // blocked one. This is the piece that stops a deadlock test from wedging
        // the whole run. The blocked body waits on a gate the test releases, so
        // the worker finishes cleanly and nothing leaks.
        BEGIN_TEST_METHOD_ATTRIBUTE(Deadline_ReportsCompletionAndTimeout)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(Deadline_ReportsCompletionAndTimeout)
        {
            DeadlineRunner fast;
            bool finishedFast = fast.Run(2000, []() { /* returns at once */ });
            Assert::IsTrue(finishedFast, L"a prompt body must be reported as finished");
            fast.Join();

            std::atomic<bool> release{ false };
            DeadlineRunner slow;
            bool finishedSlow = slow.Run(150, [&release]()
            {
                while (!release.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            });
            Assert::IsFalse(finishedSlow, L"a blocked body must be reported as a timeout, not a hang");

            release.store(true); // let the worker finish so we can join it cleanly
            slow.Join();
        }

        // Launch a child process, read its stdout to end-of-file on a worker, and
        // assert the reader terminates within the deadline. This is the #48
        // capability: a correct reader closes the parent write end and reaches
        // EOF; the bug it will catch is a reader that never does and times out.
        BEGIN_TEST_METHOD_ATTRIBUTE(ChildProcessPipe_ReachesEofWithinDeadline)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(ChildProcessPipe_ReachesEofWithinDeadline)
        {
            ChildOutput out = RunChildReadStdout("cmd.exe /c echo hello_integration_harness", 5000);

            Assert::IsTrue(out.launched, L"the child process must launch");
            Assert::IsTrue(out.reachedEof, L"the reader must reach EOF within the deadline, not hang");
            Assert::IsTrue(out.text.find("hello_integration_harness") != std::string::npos,
                L"the child's stdout must be captured");
        }

        // A message-only window records a posted application message once pumped,
        // and a post after the window is destroyed fails instead of crashing.
        // This is the #53 capability: a worker posting to a closed dialog.
        BEGIN_TEST_METHOD_ATTRIBUTE(MessageOnlyWindow_ReceivesAndSurvivesDestroy)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(MessageOnlyWindow_ReceivesAndSurvivesDestroy)
        {
            MessageOnlyWindow window;
            Assert::IsNotNull(window.Handle(), L"the message-only window must be created");

            const UINT kResult = WM_USER + 7;
            bool posted = window.Post(kResult, 0, 0);
            Assert::IsTrue(posted, L"a post to a live window must succeed");
            window.Pump();
            Assert::IsTrue(window.ReceivedMessage(kResult), L"the pumped message must be recorded");

            window.Destroy();
            bool postedAfterDestroy = window.Post(kResult, 0, 0);
            Assert::IsFalse(postedAfterDestroy, L"a post to a destroyed window must fail, not crash");
            window.Pump(); // must not crash
        }

        // A body that throws must not crash the run. Run() catches the exception,
        // still reports the work as finished, and records it via ThrewException().
        // A body that returns normally leaves ThrewException() false. Without the
        // catch, an exception escaping the worker thread is an unconditional
        // std::terminate that takes down the whole test process.
        BEGIN_TEST_METHOD_ATTRIBUTE(Deadline_CatchesAThrowingBody)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(Deadline_CatchesAThrowingBody)
        {
            DeadlineRunner throwing;
            bool finished = throwing.Run(2000, []() { throw std::runtime_error("boom"); });
            Assert::IsTrue(finished, L"a throwing body must still be reported as finished, not a hang");
            throwing.Join();
            Assert::IsTrue(throwing.ThrewException(), L"the thrown exception must be recorded");

            DeadlineRunner clean;
            bool finishedClean = clean.Run(2000, []() { /* returns normally */ });
            Assert::IsTrue(finishedClean, L"a normal body finishes");
            clean.Join();
            Assert::IsFalse(clean.ThrewException(), L"a normal body records no exception");
        }
    };
}
