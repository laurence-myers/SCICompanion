#include "stdafx.h"
#include "FileWrite.h"
#include "format.h"

sci::Status WriteBytesToFile(const std::string &path, const void *data, size_t size)
{
    const std::string what = "Writing " + path;
    if (size > MAXDWORD)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, what + ": the data is too large");
    }
    // Share read and write, as an ofstream does: a program that has the file
    // open with read and write sharing does not stop the write.
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        return sci::Fail(sci::FromWin32(error, what));
    }
    DWORD written = 0;
    BOOL wrote = (size == 0) || WriteFile(file, data, (DWORD)size, &written, nullptr);
    DWORD error = GetLastError();
    BOOL closed = CloseHandle(file);
    if (!wrote)
    {
        return sci::Fail(sci::FromWin32(error, what));
    }
    if (written != (DWORD)size)
    {
        return sci::Fail(sci::ErrorCode::Io, what + ": not all of the data was written");
    }
    if (!closed)
    {
        DWORD closeError = GetLastError();
        return sci::Fail(sci::FromWin32(closeError, what));
    }
    return sci::Ok();
}

sci::Status WriteBytesToFile(const std::string &path, const std::vector<uint8_t> &data)
{
    return WriteBytesToFile(path, data.data(), data.size());
}

size_t FullPathLength(const std::string &path)
{
    int length = MultiByteToWideChar(CP_ACP, 0, path.c_str(), (int)path.size(), nullptr, 0);
    if (length <= 0)
    {
        return path.size();
    }
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), (int)path.size(), &wide[0], length);
    // The size with the terminating null, for a buffer that is too small.
    DWORD needed = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
    {
        return wide.size();
    }
    std::wstring full(needed, L'\0');
    DWORD fullLength = GetFullPathNameW(wide.c_str(), needed, &full[0], nullptr);
    return ((fullLength > 0) && (fullLength < needed)) ? (size_t)fullLength : wide.size();
}

sci::Status CheckFileCanBeReplaced(const std::string &path, unsigned long shareMode)
{
    // The write goes through the ANSI functions, so it cannot write a path
    // whose full form (a relative path joins the current folder) has
    // MAX_PATH characters or more: the check refuses it too. It counts
    // characters, not the bytes of the ANSI code page.
    size_t characters = FullPathLength(path);
    if (characters >= MAX_PATH)
    {
        return sci::Fail(sci::ErrorCode::Io, fmt::format("Writing {0}: the full path has {1} characters, and a write takes at most {2}", path, characters, MAX_PATH - 1));
    }
    DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return sci::Ok();
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        return sci::Fail(sci::ErrorCode::Io, "Writing " + path + ": a folder has this name");
    }
    if (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
    {
        return sci::Fail(sci::ErrorCode::Io, "Writing " + path + ": the file is hidden or a system file, which the write cannot replace");
    }
    HANDLE handle = CreateFileA(path.c_str(), GENERIC_WRITE, shareMode, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return sci::Fail(sci::FromWin32(GetLastError(), "Writing " + path));
    }
    CloseHandle(handle);
    return sci::Ok();
}

sci::Status WriteTextToFile(const std::string &path, const std::string &text)
{
    std::string crlf;
    crlf.reserve(text.size() + text.size() / 16);
    for (char ch : text)
    {
        if (ch == '\n')
        {
            crlf += '\r';
        }
        crlf += ch;
    }
    return WriteBytesToFile(path, crlf.data(), crlf.size());
}
