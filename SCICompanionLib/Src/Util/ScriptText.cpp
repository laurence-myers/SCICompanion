#include "stdafx.h"
#include "ScriptText.h"
#include "format.h"

namespace
{
    // CCrystalTextBuffer::LoadFromFile looks for the style in its first read,
    // which is this many bytes.
    const size_t StyleDetectionBytes = 32768;

    const char *DetectLineBreak(const std::string &contents)
    {
        size_t searchEnd = (contents.size() < StyleDetectionBytes) ? contents.size() : StyleDetectionBytes;
        size_t lineFeed = contents.find('\n');
        if ((lineFeed == std::string::npos) || (lineFeed >= searchEnd))
        {
            return "\r\n";
        }
        if ((lineFeed > 0) && (contents[lineFeed - 1] == '\r'))
        {
            return "\r\n";
        }
        if (((lineFeed + 1) < searchEnd) && (contents[lineFeed + 1] == '\r'))
        {
            return "\n\r";
        }
        return "\n";
    }

    void AddLine(ScriptText &text, const std::string &line)
    {
        // The editor copies each line as a C string, so a NUL ends it.
        size_t nul = line.find('\0');
        text.lines.push_back((nul == std::string::npos) ? line : line.substr(0, nul));
        if ((text.firstOtherBreakLine < 0) && (text.lines.back().find_first_of("\r\n") != std::string::npos))
        {
            text.firstOtherBreakLine = (int)text.lines.size() - 1;
        }
    }
}

ScriptText SplitScriptText(const std::string &contents)
{
    ScriptText text;
    const char *lineBreak = DetectLineBreak(contents);
    std::string line;
    size_t matched = 0;
    for (char c : contents)
    {
        line += c;
        if (c == lineBreak[matched])
        {
            matched++;
            if (lineBreak[matched] == 0)
            {
                line.resize(line.size() - matched);
                AddLine(text, line);
                line.clear();
                matched = 0;
            }
        }
        else
        {
            // As the editor does: this character is not tested again as
            // the start of a break.
            matched = 0;
        }
    }
    AddLine(text, line);
    return text;
}

sci::Result<ScriptText> LoadScriptText(const std::string &path)
{
    return sci::Guard("reading " + path, [&]() -> sci::Result<ScriptText>
    {
        ScopedHandle file;
        file.hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file.hFile == INVALID_HANDLE_VALUE)
        {
            DWORD lastError = GetLastError();
            return sci::Fail(sci::FromWin32(lastError, "Opening " + path));
        }
        LARGE_INTEGER size = {};
        if (!GetFileSizeEx(file.hFile, &size))
        {
            DWORD lastError = GetLastError();
            return sci::Fail(sci::FromWin32(lastError, "Reading " + path));
        }
        if (size.QuadPart > MaxScriptTextBytes)
        {
            // A 32-bit process cannot hold a much bigger file as text, and no
            // script comes near this size.
            return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("{0} is too big for a script ({1} bytes; the limit is {2} bytes)", path, size.QuadPart, MaxScriptTextBytes));
        }
        std::string contents((size_t)size.QuadPart, '\0');
        DWORD total = 0;
        while (total < contents.size())
        {
            DWORD read = 0;
            if (!ReadFile(file.hFile, &contents[total], (DWORD)contents.size() - total, &read, nullptr))
            {
                DWORD lastError = GetLastError();
                return sci::Fail(sci::FromWin32(lastError, "Reading " + path));
            }
            if (read == 0)
            {
                // The file became shorter while we read it.
                break;
            }
            total += read;
        }
        contents.resize(total);
        return SplitScriptText(contents);
    });
}
