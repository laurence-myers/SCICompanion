#include "stdafx.h"
#include "CliConsole.h"
#include "CliVersion.h"
#include <cstdio>

namespace cli
{
    std::wstring AnsiToWide(const std::string &text)
    {
        if (text.empty())
        {
            return std::wstring();
        }
        int length = MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), &wide[0], length);
        return wide;
    }

    namespace
    {
        // The text has the ANSI code page, and a console shows its own code
        // page (often the OEM one): the console gets the characters. A file
        // or a pipe gets the bytes. False when the handle is not a console.
        bool WriteToConsole(DWORD which, FILE *stream, const std::string &text)
        {
            HANDLE handle = GetStdHandle(which);
            DWORD mode;
            if ((handle == nullptr) || (handle == INVALID_HANDLE_VALUE) || !GetConsoleMode(handle, &mode))
            {
                return false;
            }
            // Text that another part (the help of CLI11) wrote to the stream
            // comes first.
            fflush(stream);
            // CR LF, as the text mode of the C runtime writes it: a console
            // with DISABLE_NEWLINE_AUTO_RETURN needs the CR.
            std::wstring wide;
            for (wchar_t ch : AnsiToWide(text))
            {
                if (ch == L'\n')
                {
                    wide += L'\r';
                }
                wide += ch;
            }
            for (size_t done = 0; done < wide.size();)
            {
                DWORD written = 0;
                DWORD chunk = (DWORD)std::min<size_t>(wide.size() - done, 16384);
                if (!WriteConsoleW(handle, wide.data() + done, chunk, &written, nullptr) || (written == 0))
                {
                    // Nothing went to the console: the bytes go to the
                    // stream. After a part, the rest is lost.
                    return done > 0;
                }
                done += written;
            }
            return true;
        }
    }

    void StdConsole::Out(const std::string &text)
    {
        if (!WriteToConsole(STD_OUTPUT_HANDLE, stdout, text))
        {
            fwrite(text.data(), 1, text.size(), stdout);
            fflush(stdout);
        }
    }

    void StdConsole::Err(const std::string &text)
    {
        if (!WriteToConsole(STD_ERROR_HANDLE, stderr, text))
        {
            fwrite(text.data(), 1, text.size(), stderr);
            fflush(stderr);
        }
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
