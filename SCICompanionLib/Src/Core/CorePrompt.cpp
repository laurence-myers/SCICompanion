#include "stdafx.h"
#include "CorePrompt.h"
#include "CoreLog.h"
#include <atomic>

namespace
{
    std::atomic<MessageBoxHandler> g_messageBoxHandler(nullptr);
}

MessageBoxHandler SetMessageBoxHandler(MessageBoxHandler handler)
{
    return g_messageBoxHandler.exchange(handler);
}

int SafeMessageBox(const std::string &text, unsigned int type)
{
    MessageBoxHandler handler = g_messageBoxHandler.load();
    if (handler != nullptr)
    {
        int answer = handler(text, type);
        if (answer != 0)
        {
            return answer;
        }
    }
    // No GUI: send the whole text to the core log, instead of a modal dialog
    // that would block a headless run (the command line, unit tests) or appear
    // as a stray window. Return the non-destructive default so a yes/no prompt
    // does not "proceed" unattended.
    LogLevel level = LogLevel::Info;
    switch (type & MB_ICONMASK)
    {
        case MB_ICONERROR:
            level = LogLevel::Error;
            break;
        case MB_ICONWARNING:
            level = LogLevel::Warning;
            break;
    }
    CoreLog(level, text);
    switch (type & MB_TYPEMASK)
    {
        case MB_OKCANCEL:
        case MB_YESNOCANCEL:
        case MB_RETRYCANCEL:
        case MB_CANCELTRYCONTINUE:
            return IDCANCEL;
        case MB_YESNO:
            return IDNO;
        case MB_ABORTRETRYIGNORE:
            return IDABORT;
        default:
            return IDOK;
    }
}
