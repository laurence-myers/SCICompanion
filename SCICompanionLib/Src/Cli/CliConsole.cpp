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

    LogFile::LogFile(const std::string &path) : _file(path, std::ios::out | std::ios::trunc)
    {
    }

    void LogFile::Write(const std::string &text)
    {
        _file << text;
        _file.flush();
    }
}
