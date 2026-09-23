#pragma once

// Failure handling: failures are values, not exceptions.
//
// A function that can fail returns sci::Result<T>, or sci::Status when it has
// no value. The error is a structured sci::Error. Diagnostics about the user's
// input (compile errors and warnings) are not errors of the call: they go to a
// diagnostics sink. Bugs are broken invariants; the exception boundary
// (sci::Guard) turns an escaped exception into an Internal error.
// See docs/scic-cli/plan.md, section 6.
//
// Rules:
// - Do not call .value(): it throws tl::bad_expected_access. Use SCI_TRY,
//   SCI_TRY_ASSIGN, or test the result before *r.
// - Use the std::expected names only (and_then, or_else, transform,
//   transform_error), so a later move to C++23 std::expected is a rename.
// - A discarded Result is a build error (the class is [[nodiscard]] and the
//   projects set /we4834).

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sci
{
    // A broken invariant: a bug, never an expected failure.
    class InvariantViolation : public std::logic_error
    {
    public:
        using std::logic_error::logic_error;
    };
}

// A wrong access to a Result (*r on an error, r.error() on a value) is a bug.
// Check it in Release too, instead of undefined behaviour. The exception boundary
// reports the InvariantViolation as an Internal error.
#ifndef TL_ASSERT
#define TL_ASSERT(x) do { if (!(x)) { throw ::sci::InvariantViolation("Result accessed in the wrong state: " #x); } } while (false)
#endif
#include <tl/expected.hpp>

namespace sci
{
    // The same families as the error codes of the .NET rewrite plan, plus
    // Usage, Cancelled and Internal.
    enum class ErrorCode
    {
        Format,         // corrupt or truncated data
        Unsupported,    // SCI version or feature not supported
        NotFound,       // file, resource or script missing
        Io,             // a read or write failed
        Compile,        // the script has compile errors (details in the diagnostics)
        WriteRefused,   // a write that must not happen (a patch file would hide it)
        Usage,          // a bad selector or option
        Cancelled,      // Ctrl+C or the Cancel button
        Internal,       // a bug: an unexpected exception or a broken invariant
    };

    // A short lower-case name, for example "format" or "write-refused".
    const char *ErrorCodeName(ErrorCode code);

    struct ErrorLocation
    {
        std::string file;       // a game file or a source file
        std::string resource;   // for example "script 110" or "heap 110"
        int64_t offset = -1;
        int line = 0;           // 1-based; 0 when unknown
        int column = 0;         // 1-based; 0 when unknown
    };

    struct Error
    {
        ErrorCode code = ErrorCode::Internal;
        std::string message;                // one line, for people
        ErrorLocation where;
        std::vector<std::string> context;   // innermost first; each caller adds an outer line

        // "<outer context>: ... : <inner context>: <message> (<location>) [<code>]"
        std::string ToString() const;
    };

    template<typename T>
    using Result = tl::expected<T, Error>;
    using Status = Result<void>;

    inline Status Ok() { return Status(); }

    inline tl::unexpected<Error> Fail(ErrorCode code, std::string message, ErrorLocation where = ErrorLocation())
    {
        Error error;
        error.code = code;
        error.message = std::move(message);
        error.where = std::move(where);
        return tl::unexpected<Error>(std::move(error));
    }

    inline tl::unexpected<Error> Fail(Error error)
    {
        return tl::unexpected<Error>(std::move(error));
    }

    // Adds a context line on the error path only.
    template<typename T>
    Result<T> WithContext(Result<T> result, const std::string &context)
    {
        if (!result)
        {
            result.error().context.push_back(context);
        }
        return result;
    }

    // Bad data found deep in a reader. Deep code throws it; the exception boundary turns
    // it into an Error with its code.
    class DataError : public std::runtime_error
    {
    public:
        explicit DataError(const std::string &message, ErrorCode code = ErrorCode::Format)
            : std::runtime_error(message), _code(code) {}
        ErrorCode code() const { return _code; }
    private:
        ErrorCode _code;
    };

    // Converts old failure codes at a boundary. NotFound for a missing file or
    // path, Io otherwise. The message is "<what>: <system text>".
    Error FromWin32(unsigned long win32Error, const std::string &what);
    Error FromHResult(long hr, const std::string &what);
    Error FromLastError(const std::string &what);

    // Call only inside a catch block. Turns the exception in flight into an
    // Error. DataError keeps its code; a CFileException gives Io; any other
    // exception gives Internal.
    Error ErrorFromCurrentException(const std::string &context);

    // The exception boundary. Runs fn, which returns a Result or a Status. If
    // an exception escapes fn, returns it as an Error instead.
    template<typename TFunc>
    auto Guard(const std::string &context, TFunc &&fn) -> decltype(fn())
    {
        try
        {
            return std::forward<TFunc>(fn)();
        }
        catch (...)
        {
            return tl::unexpected<Error>(ErrorFromCurrentException(context));
        }
    }
}

#define SCI_CONCAT_INNER_(a, b) a##b
#define SCI_CONCAT_(a, b) SCI_CONCAT_INNER_(a, b)

// Returns early with the error if expr (a Result or a Status) failed.
#define SCI_TRY(expr)                                                                   \
    do                                                                                  \
    {                                                                                   \
        auto &&sci_try_result_ = (expr);                                                \
        if (!sci_try_result_)                                                           \
        {                                                                               \
            return ::tl::unexpected<::sci::Error>(std::move(sci_try_result_.error()));   \
        }                                                                               \
    } while (false)

// Returns early with the error if expr failed; otherwise moves the value into
// decl, for example SCI_TRY_ASSIGN(auto lines, LoadScriptText(path)).
// decl must not contain a top-level comma.
#define SCI_TRY_ASSIGN(decl, expr) SCI_TRY_ASSIGN_IMPL_(SCI_CONCAT_(sci_try_result_, __LINE__), decl, expr)
#define SCI_TRY_ASSIGN_IMPL_(tmp, decl, expr)                                           \
    auto tmp = (expr);                                                                  \
    if (!tmp)                                                                           \
    {                                                                                   \
        return ::tl::unexpected<::sci::Error>(std::move(tmp.error()));                   \
    }                                                                                   \
    decl = std::move(*tmp)
