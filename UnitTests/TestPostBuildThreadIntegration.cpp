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
#include "PostBuildThread.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace IntegrationHarness;

namespace UnitTests
{
    // Drives RunPostBuildProcess -- the pipe/process core extracted from
    // PostBuildThread::_Main -- against a real short-lived child. It is wrapped in
    // a DeadlineRunner so the #48 defect (the parent never closes the pipe write
    // end, so the final ReadFile never sees EOF) shows up as a timeout instead of
    // wedging the whole test run. All state the worker touches is heap-owned, so a
    // timed-out (and therefore detached) worker never writes to a freed test stack.
    //
    // The class name must contain "Integration": RunTests.ps1 selects the unit and
    // integration legs by FullyQualifiedName substring (see TestIntegrationHarness.cpp),
    // so renaming it away from "Integration" would silently move this into the unit leg.
    TEST_CLASS(TestPostBuildThreadIntegration)
    {
        struct Captured
        {
            std::atomic<int> starts{ 0 };
            std::atomic<bool> launched{ false };
            std::atomic<bool> aborted{ false };
            std::mutex mutex;
            std::string text;
            ScopedHandle hAbort;   // heap-owned so a timed-out detached worker never dangles it
        };

    public:
        // A normal post-build run: the child writes one line and exits. With the
        // parent write end closed, the read loop reaches EOF and RunPostBuildProcess
        // returns within the deadline. Before the #48 fix it blocks forever on the
        // final read, which this test would see as a timeout (finished == false).
        BEGIN_TEST_METHOD_ATTRIBUTE(PostBuild_ChildStdout_ReachesEofAndReturns)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(PostBuild_ChildStdout_ReachesEofAndReturns)
        {
            auto cap = std::make_shared<Captured>();
            auto onStart = [cap]() { cap->starts.fetch_add(1); };
            auto onOutput = [cap](const std::string &chunk)
            {
                std::lock_guard<std::mutex> guard(cap->mutex);
                cap->text += chunk;
            };

            DeadlineRunner runner;
            bool finished = runner.Run(10000, [cap, onStart, onOutput]()
            {
                PostBuildRunResult r = RunPostBuildProcess(
                    "",                                        // no application name
                    "cmd.exe /c echo postbuild_marker_line",   // command line
                    "",                                        // inherit the working dir
                    nullptr,                                   // no abort handle
                    onStart,
                    onOutput);
                cap->launched.store(r.launched);
                cap->aborted.store(r.aborted);
            });

            Assert::IsTrue(finished, L"the post-build read loop must reach EOF and return, not hang");
            runner.Join();

            Assert::IsTrue(cap->launched.load(), L"the child process must launch");
            Assert::IsFalse(cap->aborted.load(), L"a normal exit is not an abort");
            Assert::AreEqual(1, cap->starts.load(), L"onStart fires exactly once, on launch");

            std::lock_guard<std::mutex> guard(cap->mutex);
            Assert::IsTrue(cap->text.find("postbuild_marker_line") != std::string::npos,
                L"the child's stdout must be captured");
        }

        // A caller that only wants the launched/aborted result may pass empty
        // callbacks. RunPostBuildProcess must still drain and discard the child
        // output without throwing, so the read loop reaches EOF (both onStart and
        // onOutput are null-guarded). Before the guards, the first output chunk
        // threw std::bad_function_call on the worker thread.
        BEGIN_TEST_METHOD_ATTRIBUTE(PostBuild_NullSinks_DrainsAndReturns)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(PostBuild_NullSinks_DrainsAndReturns)
        {
            auto cap = std::make_shared<Captured>();
            DeadlineRunner runner;
            bool finished = runner.Run(10000, [cap]()
            {
                std::function<void()> noStart;                     // empty
                std::function<void(const std::string &)> noOutput; // empty
                PostBuildRunResult r = RunPostBuildProcess(
                    "",
                    "cmd.exe /c echo postbuild_marker_line",
                    "",
                    nullptr,
                    noStart,
                    noOutput);
                cap->launched.store(r.launched);
            });

            Assert::IsTrue(finished, L"empty callbacks must not stop the read loop from reaching EOF");
            runner.Join();
            Assert::IsTrue(cap->launched.load(), L"the child process must launch");
        }

        // #83: a child that runs for several seconds but writes nothing to our pipe.
        // The abort event is signalled, so RunPostBuildProcess must return promptly
        // (reporting the abort) instead of blocking in ReadFile until the child
        // exits. Before the fix the blocking read ignores the abort while the child
        // is silent, so the worker overruns the deadline (finished == false).
        BEGIN_TEST_METHOD_ATTRIBUTE(PostBuild_AbortDuringSilentChild_ReturnsPromptly)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(PostBuild_AbortDuringSilentChild_ReturnsPromptly)
        {
            auto cap = std::make_shared<Captured>();
            // Manual-reset, pre-signalled: the abort is already set when the read
            // loop starts, so honouring it must not depend on the (silent) child.
            cap->hAbort.hFile = CreateEvent(nullptr, TRUE, TRUE, nullptr);
            Assert::IsNotNull(cap->hAbort.hFile, L"create abort event");

            DeadlineRunner runner;
            bool finished = runner.Run(4000, [cap]()
            {
                PostBuildRunResult r = RunPostBuildProcess(
                    "",
                    "cmd.exe /c ping -n 6 127.0.0.1 > nul",   // ~5s, writes nothing to our pipe
                    "",
                    cap->hAbort.hFile,
                    std::function<void()>(),
                    std::function<void(const std::string &)>());
                cap->launched.store(r.launched);
                cap->aborted.store(r.aborted);
            });

            Assert::IsTrue(finished, L"the abort must be honoured while a silent child runs, not deferred until it exits");
            runner.Join();
            Assert::IsTrue(cap->launched.load(), L"the child process must launch");
            Assert::IsTrue(cap->aborted.load(), L"the result must report the abort");
        }
    };
}
