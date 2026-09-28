#include "stdafx.h"
#include "CliConsole.h"
#include "CliVersion.h"
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

    bool IsLogHeader(const std::string &text)
    {
        size_t end = text.find('\n');
        if (end == std::string::npos)
        {
            return false;
        }
        std::string line = text.substr(0, end);
        if (!line.empty() && (line.back() == '\r'))
        {
            line.pop_back();
        }
        const std::string start = "scic ";
        const std::string finish = " log";
        if ((line.size() <= start.size() + finish.size()) || (line.compare(0, start.size(), start) != 0) ||
            (line.compare(line.size() - finish.size(), finish.size(), finish) != 0))
        {
            return false;
        }
        std::string version = line.substr(start.size(), line.size() - start.size() - finish.size());
        return version.find_first_of(" \t") == std::string::npos;
    }

    LogFile::LogFile(const std::string &path) : _file(path, std::ios::out | std::ios::trunc)
    {
        Write("scic " SCIC_VERSION_TEXT " log\n");
    }

    void LogFile::Write(const std::string &text)
    {
        _file << text;
        _file.flush();
    }
}
