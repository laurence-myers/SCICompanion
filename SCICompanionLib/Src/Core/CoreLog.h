#pragma once

// The core log: information for people. A failure is not a log line; it is
// a sci::Error (see Result.h).
//
// Engine code logs through CoreLog. The host installs the sink: the GUI
// (AppState) writes to its log file, the command line to stderr, a test to a
// list. With no sink, CoreLog does nothing.
//
// Codecs log from worker threads, so a sink must be thread-safe. The host
// must keep its sink alive while any thread can log.

#include <sal.h>
#include <cstdarg>
#include <string>

enum class LogLevel
{
    Info,
    Warning,
    Error,
};

// "info", "warning" or "error".
const char *LogLevelName(LogLevel level);

class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Write(LogLevel level, const std::string &text) = 0;
};

// Installs the sink (null removes it) and returns the sink before it.
ILogSink *SetCoreLogSink(ILogSink *sink);
ILogSink *GetCoreLogSink();
// Removes the sink only if it is the one installed now. A sink calls this
// in its destructor, so that the pointer never outlives the object.
void RemoveCoreLogSink(ILogSink *sink);

// Sends the text to the sink. Never throws.
void CoreLog(LogLevel level, const std::string &text);

// The same, with printf formatting. The text has no length limit. Never
// throws.
void CoreLogFormat(LogLevel level, _Printf_format_string_ const char *format, ...);
void CoreLogFormatV(LogLevel level, const char *format, va_list args);

// Call only inside a catch block: logs the exception in flight as a
// warning, with the text of its error (sci::ErrorFromCurrentException) after
// the context. For a boundary that goes on after a failure (a worker
// thread, a best-effort read). Never throws.
void CoreLogCurrentException(const std::string &context);

// Installs a sink for the life of this object, then puts back the one before.
// The sink before must live longer than this object.
class ScopedCoreLogSink
{
public:
    explicit ScopedCoreLogSink(ILogSink &sink) : _previous(SetCoreLogSink(&sink)) {}
    ~ScopedCoreLogSink() { SetCoreLogSink(_previous); }
    ScopedCoreLogSink(const ScopedCoreLogSink &) = delete;
    ScopedCoreLogSink &operator=(const ScopedCoreLogSink &) = delete;

private:
    ILogSink *_previous;
};
