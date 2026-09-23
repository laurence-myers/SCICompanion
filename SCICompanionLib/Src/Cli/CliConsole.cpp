#include "stdafx.h"
#include "CliConsole.h"
#include <cstdio>

namespace cli
{
    void StdConsole::Out(const std::string &text)
    {
        fwrite(text.data(), 1, text.size(), stdout);
        fflush(stdout);
    }

    void StdConsole::Err(const std::string &text)
    {
        fwrite(text.data(), 1, text.size(), stderr);
        fflush(stderr);
    }

    LogFileConsole::LogFileConsole(ICliConsole &inner, const std::string &path) : _inner(inner), _file(path, std::ios::out | std::ios::trunc)
    {
    }

    void LogFileConsole::Out(const std::string &text)
    {
        _inner.Out(text);
        _file << text;
        _file.flush();
    }

    void LogFileConsole::Err(const std::string &text)
    {
        _inner.Err(text);
        _file << text;
        _file.flush();
    }
}
