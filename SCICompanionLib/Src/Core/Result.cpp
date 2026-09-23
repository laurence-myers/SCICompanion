#include "stdafx.h"
#include "Result.h"

namespace sci
{
    const char *ErrorCodeName(ErrorCode code)
    {
        switch (code)
        {
            case ErrorCode::Format: return "format";
            case ErrorCode::Unsupported: return "unsupported";
            case ErrorCode::NotFound: return "not-found";
            case ErrorCode::Io: return "io";
            case ErrorCode::Compile: return "compile";
            case ErrorCode::WriteRefused: return "write-refused";
            case ErrorCode::Usage: return "usage";
            case ErrorCode::Cancelled: return "cancelled";
            case ErrorCode::Internal: return "internal";
        }
        return "internal";
    }

    std::string Error::ToString() const
    {
        std::string text;
        for (auto it = context.rbegin(); it != context.rend(); ++it)
        {
            text += *it;
            text += ": ";
        }
        text += message;

        std::string location;
        if (!where.file.empty())
        {
            location = where.file;
            if (where.line > 0)
            {
                location += "(" + std::to_string(where.line);
                if (where.column > 0)
                {
                    location += "," + std::to_string(where.column);
                }
                location += ")";
            }
        }
        if (!where.resource.empty())
        {
            if (!location.empty())
            {
                location += ", ";
            }
            location += where.resource;
        }
        if (where.offset >= 0)
        {
            if (!location.empty())
            {
                location += ", ";
            }
            location += "offset " + std::to_string(where.offset);
        }
        if (!location.empty())
        {
            text += " (" + location + ")";
        }
        text += " [";
        text += ErrorCodeName(code);
        text += "]";
        return text;
    }

    namespace
    {
        std::string SystemMessage(DWORD messageId)
        {
            char buffer[512] = {};
            DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, messageId, 0, buffer, (DWORD)ARRAYSIZE(buffer), nullptr);
            std::string text(buffer, length);
            // FormatMessage ends the text with a line break (and often a full stop).
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '.'))
            {
                text.pop_back();
            }
            if (text.empty())
            {
                char code[32];
                StringCchPrintfA(code, ARRAYSIZE(code), "error 0x%08lx", (unsigned long)messageId);
                text = code;
            }
            return text;
        }

        std::string Join(const std::string &what, const std::string &detail)
        {
            return what.empty() ? detail : (what + ": " + detail);
        }
    }

    Error FromWin32(unsigned long win32Error, const std::string &what)
    {
        Error error;
        switch (win32Error)
        {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_DRIVE:
            case ERROR_BAD_NETPATH:
                error.code = ErrorCode::NotFound;
                break;
            default:
                error.code = ErrorCode::Io;
                break;
        }
        error.message = Join(what, SystemMessage(win32Error));
        return error;
    }

    Error FromHResult(long hr, const std::string &what)
    {
        if (HRESULT_FACILITY(hr) == FACILITY_WIN32)
        {
            return FromWin32(HRESULT_CODE(hr), what);
        }
        Error error;
        error.code = ErrorCode::Io;
        error.message = Join(what, SystemMessage((DWORD)hr));
        return error;
    }

    Error FromLastError(const std::string &what)
    {
        return FromWin32(GetLastError(), what);
    }

    Error ErrorFromCurrentException(const std::string &context)
    {
        Error error;
        try
        {
            throw;
        }
        catch (const DataError &e)
        {
            error.code = e.code();
            error.message = e.what();
        }
        catch (const InvariantViolation &e)
        {
            error.code = ErrorCode::Internal;
            error.message = std::string("invariant violation: ") + e.what();
        }
        catch (const tl::bad_expected_access<Error> &e)
        {
            // A caller used .value() on an error. Keep the original error text.
            error.code = ErrorCode::Internal;
            error.message = "a Result was read while it held an error: " + e.error().ToString();
        }
        catch (const std::bad_alloc &)
        {
            error.code = ErrorCode::Internal;
            error.message = "out of memory";
        }
        catch (const std::exception &e)
        {
            error.code = ErrorCode::Internal;
            error.message = e.what();
        }
        catch (CException *e)
        {
            TCHAR text[512] = {};
            BOOL hasText = e->GetErrorMessage(text, ARRAYSIZE(text));
            error.code = e->IsKindOf(RUNTIME_CLASS(CFileException)) ? ErrorCode::Io : ErrorCode::Internal;
            error.message = (hasText && text[0]) ? std::string(text) : std::string("MFC exception (no text)");
            e->Delete();
        }
        catch (...)
        {
            error.code = ErrorCode::Internal;
            error.message = "unknown exception";
        }
        if (error.message.empty())
        {
            error.message = "exception with no text";
        }
        if (!context.empty())
        {
            error.context.push_back(context);
        }
        return error;
    }
}
