#include "stdafx.h"
#include "CoreLog.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace
{
    std::atomic<ILogSink*> g_coreLogSink(nullptr);

    std::string FormatV(const char *format, va_list args)
    {
        va_list measure;
        va_copy(measure, args);
        int length = std::vsnprintf(nullptr, 0, format, measure);
        va_end(measure);
        if (length <= 0)
        {
            return std::string();
        }
        std::string text((size_t)length, '\0');
        // Also writes the terminating null into text[length], which is allowed.
        std::vsnprintf(&text[0], (size_t)length + 1, format, args);
        return text;
    }
}

const char *LogLevelName(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Info: return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error: return "error";
    }
    return "info";
}

ILogSink *SetCoreLogSink(ILogSink *sink)
{
    return g_coreLogSink.exchange(sink);
}

ILogSink *GetCoreLogSink()
{
    return g_coreLogSink.load();
}

void RemoveCoreLogSink(ILogSink *sink)
{
    ILogSink *expected = sink;
    g_coreLogSink.compare_exchange_strong(expected, nullptr);
}

void CoreLog(LogLevel level, const std::string &text)
{
    ILogSink *sink = g_coreLogSink.load();
    if (sink == nullptr)
    {
        return;
    }
    try
    {
        sink->Write(level, text);
    }
    catch (...)
    {
        // A log line is not worth a failure. Tell a debugger, if one listens.
        OutputDebugStringA("CoreLog: the log sink threw an exception\n");
    }
}

void CoreLogFormatV(LogLevel level, const char *format, va_list args)
{
    if (g_coreLogSink.load() == nullptr)
    {
        return;
    }
    std::string text;
    try
    {
        text = FormatV(format, args);
    }
    catch (...)
    {
        OutputDebugStringA("CoreLog: could not format a log line\n");
    }
    if (!text.empty())
    {
        CoreLog(level, text);
    }
}

void CoreLogFormat(LogLevel level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    CoreLogFormatV(level, format, args);
    va_end(args);
}
