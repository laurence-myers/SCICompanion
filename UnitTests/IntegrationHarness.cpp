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
#include "IntegrationHarness.h"
#include <memory>

namespace IntegrationHarness
{
    bool DeadlineRunner::Run(unsigned timeoutMs, std::function<void()> body)
    {
        // Reclaim any previous worker so a reused runner never move-assigns onto
        // a joinable thread, which would call std::terminate.
        if (_thread.joinable())
        {
            if (_done && _done->load()) { _thread.join(); }
            else { _thread.detach(); }
        }

        auto done = std::make_shared<std::atomic<bool>>(false);
        auto threw = std::make_shared<std::atomic<bool>>(false);
        _done = done;
        _threw = threw;
        _thread = std::thread([done, threw, body]()
        {
            // Catch anything the body throws. An exception escaping a std::thread's
            // function is an unconditional std::terminate that would kill the whole
            // test run; record it instead and still mark the work done so Run()
            // returns rather than timing out.
            try
            {
                body();
            }
            catch (...)
            {
                threw->store(true);
            }
            done->store(true); // heap-owned: safe even if the runner was destroyed
        });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!done->load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return done->load();
    }

    void DeadlineRunner::Join()
    {
        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    DeadlineRunner::~DeadlineRunner()
    {
        if (_thread.joinable())
        {
            if (_done && _done->load())
            {
                _thread.join();
            }
            else
            {
                // The body did not finish and cannot be killed in process. Detach
                // so this object's destruction does not block the whole run. The
                // completion flag is heap-owned, so the detached worker writing it
                // later is safe. A true deadlock is caught by vstest --blame-hang.
                _thread.detach();
            }
        }
    }

    // The project is MBCS, so the window APIs resolve to the ANSI variants and
    // take LPCSTR; keep the class name narrow.
    static const char *kWindowClass = "SCIC_IntegrationMessageWindow";

    LRESULT CALLBACK MessageOnlyWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            auto cs = reinterpret_cast<CREATESTRUCT *>(lParam);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }
        auto self = reinterpret_cast<MessageOnlyWindow *>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (self && (msg >= WM_USER))
        {
            // Record only application messages, so system traffic is not noise.
            self->_received.push_back(msg);
            self->_receivedParams.push_back(std::make_pair(msg, wParam));
        }
        return ::DefWindowProc(hwnd, msg, wParam, lParam);
    }

    MessageOnlyWindow::MessageOnlyWindow()
    {
        WNDCLASSEX wc = { sizeof(wc) };
        wc.lpfnWndProc = &MessageOnlyWindow::WndProc;
        wc.hInstance = ::GetModuleHandle(nullptr);
        wc.lpszClassName = kWindowClass;
        // Returns 0 with ERROR_CLASS_ALREADY_EXISTS on a repeat registration,
        // which is fine: CreateWindowEx still finds the class.
        ::RegisterClassEx(&wc);
        _hwnd = ::CreateWindowEx(0, kWindowClass, "", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, this);
    }

    MessageOnlyWindow::~MessageOnlyWindow()
    {
        Destroy();
    }

    void MessageOnlyWindow::Destroy()
    {
        if (_hwnd && !_destroyed)
        {
            ::DestroyWindow(_hwnd);
            _destroyed = true;
            // Keep the (now stale) handle value so a post-after-destroy exercises
            // the real failure: PostMessage to a destroyed window returns FALSE.
        }
    }

    bool MessageOnlyWindow::Post(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (_destroyed)
        {
            // Deterministic: a post after the window is torn down fails, without
            // relying on the stale handle value (which Windows may recycle and
            // hand to an unrelated window).
            return false;
        }
        return ::PostMessage(_hwnd, msg, wParam, lParam) != FALSE;
    }

    void MessageOnlyWindow::Pump()
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    bool MessageOnlyWindow::ReceivedMessage(UINT msg) const
    {
        for (UINT m : _received)
        {
            if (m == msg)
            {
                return true;
            }
        }
        return false;
    }

    WPARAM MessageOnlyWindow::WParamOf(UINT msg) const
    {
        // Most recent match wins.
        for (auto it = _receivedParams.rbegin(); it != _receivedParams.rend(); ++it)
        {
            if (it->first == msg)
            {
                return it->second;
            }
        }
        return 0;
    }

    namespace
    {
        // Heap-owned so a timed-out reader thread never touches freed stack.
        struct ReaderState
        {
            HANDLE readEnd = nullptr;
            std::string text;
            std::atomic<bool> eof{ false };
        };
    }

    ChildOutput RunChildReadStdout(const std::string &commandLine, unsigned timeoutMs)
    {
        ChildOutput out;

        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE readEnd = nullptr, writeEnd = nullptr;
        if (!::CreatePipe(&readEnd, &writeEnd, &sa, 0))
        {
            return out;
        }
        // The parent's read end must not be inherited by the child.
        ::SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = writeEnd;
        si.hStdError = writeEnd;
        si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION pi = {};
        std::string mutableCmd = commandLine; // CreateProcessA may write to the buffer
        BOOL launched = ::CreateProcessA(nullptr, mutableCmd.empty() ? nullptr : &mutableCmd[0],
            nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

        // Close the parent copy of the write end BEFORE reading, so the read sees
        // EOF once the child exits. This is exactly the close PostBuildThread omits.
        ::CloseHandle(writeEnd);

        if (!launched)
        {
            ::CloseHandle(readEnd);
            return out;
        }
        out.launched = true;

        auto state = std::make_shared<ReaderState>();
        state->readEnd = readEnd;

        DeadlineRunner runner;
        bool finished = runner.Run(timeoutMs, [state]()
        {
            char buffer[512];
            DWORD read = 0;
            while (::ReadFile(state->readEnd, buffer, sizeof(buffer), &read, nullptr) && (read > 0))
            {
                state->text.append(buffer, read);
            }
            // The reader owns the read end and closes it here, so it is released
            // on both the normal and the timed-out (detached) path.
            ::CloseHandle(state->readEnd);
            state->eof.store(true); // ReadFile failed or returned 0: the pipe is closed
        });

        if (finished)
        {
            runner.Join();
            out.reachedEof = state->eof.load();
            out.text = state->text;
        }
        // If not finished, the reader is left running on heap-owned state, closes
        // its own handle when the pipe finally drains, and the runner detaches it.
        // That path is the #48 hang, backstopped by --blame-hang.

        if (::WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0)
        {
            ::GetExitCodeProcess(pi.hProcess, &out.exitCode);
        }
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        return out;
    }
}
