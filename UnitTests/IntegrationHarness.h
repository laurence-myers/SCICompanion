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
#pragma once

//
// Integration-test harness (Phase 1 of the test-harness plan).
//
// These building blocks let a test drive a whole component across threads,
// a child process, or a window, and assert that it finishes, stays consistent,
// and does not corrupt memory -- the class of defect a unit test cannot see.
//
// Everything here is test-support code. It makes no production change.
//
#include <windows.h>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace IntegrationHarness
{
    //
    // Runs a body on a worker thread and waits up to a deadline. Run() returns
    // true if the body finished in time, false on timeout. CppUnitTest has no
    // per-test timeout, so a concurrency test that fails by hanging would wedge
    // the whole run; this gives it a soft deadline instead.
    //
    // Contract on timeout: the worker is still running and cannot be killed in
    // process, so it is detached rather than joined. The completion flag is
    // heap-owned (a shared_ptr the worker holds by value), so a detached worker
    // that finishes later writes only to memory that outlives this object --
    // there is no use-after-free. A body that shares OTHER state with the test
    // must likewise keep it alive (a gate the test releases, or heap). Run()
    // reclaims a prior worker, so a runner is reusable. The real backstop for a
    // true deadlock is vstest --blame-hang.
    //
    class DeadlineRunner
    {
    public:
        DeadlineRunner() = default;
        ~DeadlineRunner();
        DeadlineRunner(const DeadlineRunner &) = delete;
        DeadlineRunner &operator=(const DeadlineRunner &) = delete;

        bool Run(unsigned timeoutMs, std::function<void()> body);
        bool Finished() const { return _done && _done->load(); }
        // True if the last body threw. Run() catches any exception the body throws
        // and records it here instead of letting it escape the worker thread, which
        // would be an unconditional std::terminate. The flag is heap-owned, so a
        // detached worker that throws after a timeout still records it safely. A
        // test that must know its body did not throw can assert !ThrewException().
        bool ThrewException() const { return _threw && _threw->load(); }
        void Join();

    private:
        std::thread _thread;
        std::shared_ptr<std::atomic<bool>> _done;
        std::shared_ptr<std::atomic<bool>> _threw;
    };

    //
    // A message-only (HWND_MESSAGE) window that records the application messages
    // it receives. A test uses it to assert that a worker posted a result, and
    // to simulate a closed dialog: after Destroy(), a Post() must fail rather
    // than crash (the #53 scenario). Messages are recorded on the thread that
    // Pump()s, which owns the window.
    //
    class MessageOnlyWindow
    {
    public:
        MessageOnlyWindow();
        ~MessageOnlyWindow();
        MessageOnlyWindow(const MessageOnlyWindow &) = delete;
        MessageOnlyWindow &operator=(const MessageOnlyWindow &) = delete;

        HWND Handle() const { return _hwnd; }
        bool Post(UINT msg, WPARAM wParam, LPARAM lParam);   // false once the window is gone
        void Pump();                                          // drain and dispatch this thread's queue
        void Destroy();
        bool ReceivedMessage(UINT msg) const;
        size_t ReceivedCount() const { return _received.size(); }
        // wParam of the most recent received message equal to msg (0 if none).
        WPARAM WParamOf(UINT msg) const;

    private:
        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        HWND _hwnd = nullptr;
        bool _destroyed = false;
        std::vector<UINT> _received;
        std::vector<std::pair<UINT, WPARAM>> _receivedParams;
    };

    //
    // Result of running a child process and reading its stdout to end-of-file.
    //
    struct ChildOutput
    {
        std::string text;
        bool reachedEof = false;   // the reader saw EOF: all write handles were closed
        bool launched = false;
        DWORD exitCode = 0;
    };

    //
    // Launch a command line with its stdout piped, close the parent's copy of the
    // write end (the step PostBuildThread omits in #48), and read stdout to EOF on
    // a worker bounded by a deadline. reachedEof == false means the reader did not
    // terminate in time -- the exact symptom of the #48 hang. The reader's shared
    // state is heap-owned, so a timed-out reader is safe to leave running.
    //
    ChildOutput RunChildReadStdout(const std::string &commandLine, unsigned timeoutMs);
}
