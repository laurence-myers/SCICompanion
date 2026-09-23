#include "stdafx.h"
#include "CliHost.h"
#include <cstdio>
#include <cstdlib>

namespace cli
{
    namespace
    {
        std::atomic<bool> g_cancel(false);

        // The text of the item, for the crash filter. A fixed buffer: the
        // filter must not allocate.
        thread_local char t_currentItem[256] = {};

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

        LONG WINAPI CrashFilter(EXCEPTION_POINTERS *exception)
        {
            char line[512];
            DWORD code = (exception && exception->ExceptionRecord) ? exception->ExceptionRecord->ExceptionCode : 0;
            int length = t_currentItem[0] ?
                _snprintf_s(line, _TRUNCATE, "scic: crash 0x%08lX while %s\n", code, t_currentItem) :
                _snprintf_s(line, _TRUNCATE, "scic: crash 0x%08lX\n", code);
            if (length > 0)
            {
                WriteToStderr(line, (size_t)length);
            }
            TerminateProcess(GetCurrentProcess(), 1);
            return EXCEPTION_EXECUTE_HANDLER;
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
    }

    void SetCurrentItem(const std::string &item)
    {
        strncpy_s(t_currentItem, item.c_str(), _TRUNCATE);
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
