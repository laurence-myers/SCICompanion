#include "stdafx.h"
#include "FileWrite.h"

sci::Status WriteBytesToFile(const std::string &path, const void *data, size_t size)
{
    const std::string what = "Writing " + path;
    if (size > MAXDWORD)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, what + ": the data is too large");
    }
    // Share the file as the ofstream that this replaced did: a program that
    // has the file open with read and write sharing does not stop the write
    // (review of S1).
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
