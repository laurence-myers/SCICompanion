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
#include <thread>

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

        // The abort must stop the whole process tree, not just the top process.
        // The post-build command launches a detached grandchild (start /b, which
        // does not break away from the Job) that waits ~4s then writes a marker
        // file, while the top cmd waits ~9s. A signaller thread sets the abort
        // ~1.5s in, while the grandchild is still waiting. With the Job-object
        // fix the whole tree is terminated, so the marker is never written.
        // Before the fix the abort left the grandchild running and it wrote the
        // marker (#127).
        BEGIN_TEST_METHOD_ATTRIBUTE(PostBuild_Abort_TerminatesTheChildTree)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(PostBuild_Abort_TerminatesTheChildTree)
        {
            // A unique marker path under the temp folder, in 8.3 short form so it
            // has no spaces (the temp path can contain a space) and needs no extra
            // quoting inside the cmd redirect.
            char tempLong[MAX_PATH] = {};
            GetTempPathA(ARRAYSIZE(tempLong), tempLong);
            char tempShort[MAX_PATH] = {};
            DWORD shortLen = GetShortPathNameA(tempLong, tempShort, ARRAYSIZE(tempShort));
            std::string tempDir = (shortLen > 0 && shortLen < ARRAYSIZE(tempShort)) ? std::string(tempShort) : std::string(tempLong);
            Assert::IsTrue(tempDir.find(' ') == std::string::npos, L"setup: the temp path must be space-free for the cmd redirect");
            std::string marker = tempDir + "scicompanion-postbuild-tree-" +
                std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()) + ".marker";
            DeleteFileA(marker.c_str());

            auto cap = std::make_shared<Captured>();
            cap->hAbort.hFile = CreateEvent(nullptr, TRUE, FALSE, nullptr); // manual-reset, not signalled yet
            Assert::IsNotNull(cap->hAbort.hFile, L"create abort event");

            std::string command = "cmd.exe /c start \"\" /b cmd.exe /c \"ping -n 5 127.0.0.1 >nul & echo done>" +
                marker + "\" & ping -n 10 127.0.0.1 >nul";

            // Signal the abort ~1.5s in, while RunPostBuildProcess is still running
            // and the grandchild is still waiting to write its marker.
            std::thread signaller([cap]()
            {
                Sleep(1500);
                SetEvent(cap->hAbort.hFile);
            });

            DeadlineRunner runner;
            bool finished = runner.Run(20000, [cap, command]()
            {
                PostBuildRunResult r = RunPostBuildProcess(
                    "",
                    command,
                    "",
                    cap->hAbort.hFile,
                    std::function<void()>(),
                    std::function<void(const std::string &)>());
                cap->launched.store(r.launched);
                cap->aborted.store(r.aborted);
            });
            signaller.join();

            Assert::IsTrue(finished, L"RunPostBuildProcess must return after the abort");
            runner.Join();
            Assert::IsTrue(cap->launched.load(), L"the child process must launch");
            Assert::IsTrue(cap->aborted.load(), L"the result must report the abort");

            // Wait past the grandchild's ~4s delay. If the tree was terminated the
            // marker is never written.
            Sleep(6000);
            bool markerExists = (GetFileAttributesA(marker.c_str()) != INVALID_FILE_ATTRIBUTES);
            DeleteFileA(marker.c_str());
            Assert::IsFalse(markerExists, L"the aborted post-build step's detached grandchild must be terminated, not left to write its marker");
        }
    };
}
