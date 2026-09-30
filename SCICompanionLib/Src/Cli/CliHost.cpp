#include "stdafx.h"
#include "CliHost.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace cli
{
    namespace
    {
        std::atomic<bool> g_cancel(false);

        // The text of the item, for the crash filter. A fixed buffer: the
        // filter must not allocate.
        thread_local char t_currentItem[256] = {};
        thread_local std::vector<std::string> *t_recordedItems = nullptr;

        // Writes with WriteFile only: the process state is not known.
        void WriteToStderr(const char *text, size_t length)
        {
            HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
            if ((handle != nullptr) && (handle != INVALID_HANDLE_VALUE))
            {
                DWORD written = 0;
                WriteFile(handle, text, (DWORD)length, &written, nullptr);
            }
        }

        std::atomic<bool> g_crashed(false);
        thread_local bool t_inCrashLine = false;

        // One line, then exit code 1 (plan section 6.6). Only the first crash
        // prints: a crash of another thread waits for it to end the process
        // (at most 10 s). A fault inside this function ends the process at
        // once.
        void CrashLine(const char *what)
        {
            if (t_inCrashLine)
            {
                TerminateProcess(GetCurrentProcess(), 1);
            }
            t_inCrashLine = true;
            if (g_crashed.exchange(true))
            {
                Sleep(10000);
                TerminateProcess(GetCurrentProcess(), 1);
            }
            char line[512];
            int length = t_currentItem[0] ?
                _snprintf_s(line, _TRUNCATE, "scic: crash %s while %s\n", what, t_currentItem) :
                _snprintf_s(line, _TRUNCATE, "scic: crash %s\n", what);
            if (length > 0)
            {
                WriteToStderr(line, (size_t)length);
            }
            TerminateProcess(GetCurrentProcess(), 1);
        }

        LONG WINAPI CrashFilter(EXCEPTION_POINTERS *exception)
        {
            char code[16];
            _snprintf_s(code, _TRUNCATE, "0x%08lX", (exception && exception->ExceptionRecord) ? exception->ExceptionRecord->ExceptionCode : 0);
            CrashLine(code);
            return EXCEPTION_EXECUTE_HANDLER;
        }

        // abort(), and std::terminate(), which calls it: one line, and exit
        // code 1. With no handler, abort() ends the process with exit code 3
        // and no line (InstallCrashHandling clears _CALL_REPORTFAULT), and 3
        // is the exit code of "cannot open the game".
        void __cdecl AbortHandler(int)
        {
            // The C runtime sets SIGABRT back to its default before it calls
            // the handler, so the handler installs itself again for a later
            // abort of another thread. An abort of two threads at the same
            // moment can still end the process with exit code 3.
            signal(SIGABRT, AbortHandler);
            CrashLine("(abort)");
        }

        // A C runtime function got a bad parameter. The default handler of
        // the C runtime would end the process with 0xC0000409 and no line.
        void __cdecl InvalidParameterHandler(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
        {
            CrashLine("(invalid parameter)");
        }

        void __cdecl PureCallHandler()
        {
            CrashLine("(pure virtual call)");
        }

        BOOL WINAPI CancelHandler(DWORD type)
        {
            if ((type != CTRL_C_EVENT) && (type != CTRL_BREAK_EVENT))
            {
                return FALSE;
            }
            if (g_cancel.exchange(true))
            {
                // The second Ctrl+C: end at once.
                const char text[] = "scic: stopped\n";
                WriteToStderr(text, sizeof(text) - 1);
                TerminateProcess(GetCurrentProcess(), 7);
            }
            const char text[] = "scic: stopping after the current item (Ctrl+C again to stop now)\n";
            WriteToStderr(text, sizeof(text) - 1);
            return TRUE;
        }
    }

    void InstallCrashHandling()
    {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetUnhandledExceptionFilter(CrashFilter);
        signal(SIGABRT, AbortHandler);
        _set_invalid_parameter_handler(InvalidParameterHandler);
        _set_purecall_handler(PureCallHandler);
    }

    void CrashForATestIfAsked()
    {
        char kind[32] = {};
        if (GetEnvironmentVariableA("SCIC_TEST_CRASH", kind, (DWORD)sizeof(kind)) == 0)
        {
            return;
        }
        SetCurrentItem("the crash test");
        if (strcmp(kind, "access") == 0)
        {
            volatile int *nowhere = nullptr;
            *nowhere = 1;
        }
        else if (strcmp(kind, "abort") == 0)
        {
            abort();
        }
        else if (strcmp(kind, "terminate") == 0)
        {
            std::terminate();
        }
        else if (strcmp(kind, "invalid") == 0)
        {
            char tooSmall[2];
            const char *text = "too long";
            strcpy_s(tooSmall, text);
        }
        SetCurrentItem("");
    }

    void SetCurrentItem(const std::string &item)
    {
        strncpy_s(t_currentItem, item.c_str(), _TRUNCATE);
        if (t_recordedItems && !item.empty())
        {
            t_recordedItems->push_back(item);
        }
    }

    void RecordCurrentItems(std::vector<std::string> *items)
    {
        t_recordedItems = items;
    }

    std::string CurrentItem()
    {
        return t_currentItem;
    }

    void InstallCancelHandler()
    {
        SetConsoleCtrlHandler(CancelHandler, TRUE);
    }

    std::atomic<bool> &CancelFlag()
    {
        return g_cancel;
    }
}
