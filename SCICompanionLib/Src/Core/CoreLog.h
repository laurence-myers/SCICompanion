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

// Sends the text to the sink. Never throws.
void CoreLog(LogLevel level, const std::string &text);

// The same, with printf formatting, for the old LogInfo call sites. The text
// has no length limit.
void CoreLogFormat(LogLevel level, const char *format, ...);

// Installs a sink for the life of this object, then puts back the one before.
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
