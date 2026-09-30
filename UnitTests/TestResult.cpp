#include "stdafx.h"
#include "CppUnitTest.h"
#include "Result.h"
#include "TestSupport.h"
#include "MfcExceptionMapper.h"
#include "AppState.h"
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace sci;

namespace
{
    void AssertCode(ErrorCode expected, const Error &error)
    {
        Assert::AreEqual(std::string(ErrorCodeName(expected)), std::string(ErrorCodeName(error.code)), Wide("error was: " + error.ToString()).c_str());
    }

    bool Contains(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }

    Status Step(bool fail, const char *name)
    {
        if (fail)
        {
            return Fail(ErrorCode::Io, std::string(name) + " failed");
        }
        return Ok();
    }

    Status TwoSteps(bool failFirst, int &secondRan)
    {
        SCI_TRY(Step(failFirst, "first"));
        ++secondRan;
        SCI_TRY(Step(false, "second"));
        return Ok();
    }

    Result<std::string> MakeText(bool ok)
    {
        if (!ok)
        {
            return Fail(ErrorCode::NotFound, "no text");
        }
        return std::string("hello");
    }

    Result<size_t> TextLength(bool ok)
    {
        SCI_TRY_ASSIGN(auto text, MakeText(ok));
        return text.size();
    }

    // SCI_TRY on a named Result: it must copy the error, not move it out.
    Status Propagate(Status &stored)
    {
        SCI_TRY(stored);
        return Ok();
    }

    // Two SCI_TRY_ASSIGN on one line compile only if their names differ.
    Result<size_t> TwoOnOneLine()
    {
        SCI_TRY_ASSIGN(auto first, MakeText(true)); SCI_TRY_ASSIGN(auto second, MakeText(true));
        return first.size() + second.size();
    }
}

namespace UnitTests
{
    TEST_CLASS(TestResult)
    {
    public:
        TEST_METHOD(Ok_IsSuccess)
        {
            Status status = Ok();
            AssertOk(status);
        }

        TEST_METHOD(Guard_PassesValueThrough)
        {
            Result<int> result = Guard("ctx", []() -> Result<int> { return 42; });
            Assert::AreEqual(42, ValueOf(result));
        }

        TEST_METHOD(Guard_PassesErrorThroughUnchanged)
        {
            Result<int> result = Guard("ctx", []() -> Result<int> { return Fail(ErrorCode::Usage, "bad option"); });
            Assert::IsFalse(result.has_value());
            AssertCode(ErrorCode::Usage, result.error());
            Assert::AreEqual(std::string("bad option"), result.error().message);
            // Guard adds its context only to an exception it caught.
            Assert::IsTrue(result.error().context.empty());
        }

        TEST_METHOD(Guard_DataError_KeepsItsCode)
        {
            Status status = Guard("reading script 5", []() -> Status { throw DataError("unknown header", ErrorCode::Unsupported); });
            Assert::IsFalse(status.has_value());
            AssertCode(ErrorCode::Unsupported, status.error());
            Assert::AreEqual(std::string("unknown header"), status.error().message);
            Assert::AreEqual(size_t(1), status.error().context.size());
            Assert::AreEqual(std::string("reading script 5"), status.error().context[0]);
        }

        TEST_METHOD(Guard_DataError_DefaultsToFormat)
        {
            Status status = Guard("", []() -> Status { throw DataError("truncated"); });
            AssertCode(ErrorCode::Format, status.error());
        }

        // Code that cannot return a Result throws DataError(error) for a failed
        // one. The exception boundary gives back the whole error: code,
        // location and context, with its own context line added outside.
        TEST_METHOD(Guard_DataErrorWithAnError_KeepsTheWholeError)
        {
            Status status = Guard("writing 2 resources to patch files", []() -> Status
            {
                Error error;
                error.code = ErrorCode::Io;
                error.message = "Access is denied.";
                error.where.file = "text.911";
                error.context.push_back("replacing the file");
                throw DataError(std::move(error));
            });
            Assert::IsFalse(status.has_value());
            AssertCode(ErrorCode::Io, status.error());
            Assert::AreEqual(std::string("Access is denied."), status.error().message);
            Assert::AreEqual(std::string("text.911"), status.error().where.file);
            Assert::AreEqual(size_t(2), status.error().context.size());
            Assert::AreEqual(std::string("replacing the file"), status.error().context[0]);
            Assert::AreEqual(std::string("writing 2 resources to patch files"), status.error().context[1]);
        }

        TEST_METHOD(Guard_StdException_IsInternal)
        {
            Status status = Guard("", []() -> Status { throw std::runtime_error("boom"); });
            AssertCode(ErrorCode::Internal, status.error());
            Assert::AreEqual(std::string("boom"), status.error().message);
        }

        TEST_METHOD(Guard_LegacyMsvcException_KeepsItsText)
        {
            // std::exception("...") with a text is a Microsoft extension.
            Status status = Guard("", []() -> Status { throw std::exception("legacy failure"); });
            AssertCode(ErrorCode::Internal, status.error());
            Assert::AreEqual(std::string("legacy failure"), status.error().message);
        }

        TEST_METHOD(Guard_BadAlloc_IsOutOfMemory)
        {
            Status status = Guard("", []() -> Status { throw std::bad_alloc(); });
            AssertCode(ErrorCode::Internal, status.error());
            Assert::AreEqual(std::string("out of memory"), status.error().message);
        }

        TEST_METHOD(Guard_InvariantViolation_IsInternal)
        {
            Status status = Guard("", []() -> Status { throw InvariantViolation("list not sorted"); });
            AssertCode(ErrorCode::Internal, status.error());
            Assert::IsTrue(Contains(status.error().message, "list not sorted"));
        }

        // Installs a foreign exception mapper for the life of the object.
        struct ScopedMapper
        {
            explicit ScopedMapper(ForeignExceptionMapper mapper) : previous(SetForeignExceptionMapper(mapper)) {}
            ~ScopedMapper() { SetForeignExceptionMapper(previous); }
            ForeignExceptionMapper previous;
        };

        // A mapper for the tests: it knows an int.
        static bool MapInt(Error &error)
        {
            try
            {
                throw;
            }
            catch (int value)
            {
                error.code = ErrorCode::Unsupported;
                error.message = "int " + std::to_string(value);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        TEST_METHOD(Guard_ForeignMapper_MapsItsException)
        {
            ScopedMapper mapper(MapInt);
            Status status = Guard("reading", []() -> Status { throw 7; });
            AssertCode(ErrorCode::Unsupported, status.error());
            Assert::AreEqual(std::string("int 7"), status.error().message);
            Assert::AreEqual(std::string("reading"), status.error().context.at(0));
        }

        TEST_METHOD(Guard_ForeignMapper_LeavesOtherExceptions)
        {
            // An exception that the mapper does not know, and a std::exception
            // (the core maps it before the mapper), keep their texts.
            ScopedMapper mapper(MapInt);
            Status unknown = Guard("", []() -> Status { throw 'c'; });
            AssertCode(ErrorCode::Internal, unknown.error());
            Assert::AreEqual(std::string("unknown exception"), unknown.error().message);
            Status standard = Guard("", []() -> Status { throw std::runtime_error("broken"); });
            AssertCode(ErrorCode::Internal, standard.error());
            Assert::AreEqual(std::string("broken"), standard.error().message);
        }

        // The MFC exceptions: the GUI library installs MapMfcException.
        TEST_METHOD(Guard_CFileException_IsIo)
        {
            ScopedMapper mapper(MapMfcException);
            Status status = Guard("", []() -> Status { AfxThrowFileException(CFileException::fileNotFound, -1, _T("missing.txt")); });
            AssertCode(ErrorCode::Io, status.error());
            Assert::IsFalse(status.error().message.empty());
        }

        // The GUI installs the mapper: AppState's constructor does it.
        TEST_METHOD(AppState_InstallsTheMfcMapper)
        {
            ScopedMapper none(nullptr);
            std::unique_ptr<AppState> app = std::make_unique<AppState>(nullptr);
            Status status = Guard("", []() -> Status { AfxThrowFileException(CFileException::fileNotFound, -1, _T("missing.txt")); });
            AssertCode(ErrorCode::Io, status.error());
        }

        TEST_METHOD(Guard_CUserException_IsInternal)
        {
            // SetGameFolder throws this one, with no text.
            ScopedMapper mapper(MapMfcException);
            Status status = Guard("", []() -> Status { AfxThrowUserException(); return Ok(); });
            AssertCode(ErrorCode::Internal, status.error());
            Assert::AreEqual(std::string("MFC exception (no text)"), status.error().message);
        }

        TEST_METHOD(Guard_CMemoryException_IsOutOfMemory)
        {
            // In an MFC program, a failed new throws CMemoryException*, not
            // std::bad_alloc.
            ScopedMapper mapper(MapMfcException);
            Status status = Guard("reading a big file", []() -> Status { AfxThrowMemoryException(); return Ok(); });
            AssertCode(ErrorCode::Internal, status.error());
            Assert::AreEqual(std::string("out of memory"), status.error().message);
            Assert::AreEqual(std::string("reading a big file"), status.error().context.at(0));
        }

        TEST_METHOD(Guard_UnknownException_IsInternal)
        {
            Status status = Guard("", []() -> Status { throw 42; });
            AssertCode(ErrorCode::Internal, status.error());
            Assert::AreEqual(std::string("unknown exception"), status.error().message);
        }

        TEST_METHOD(Guard_ValueOnError_KeepsTheOriginalText)
        {
            Result<int> result = Guard("", []() -> Result<int>
            {
                Result<int> failed = Fail(ErrorCode::NotFound, "no such script");
                return failed.value(); // A bug: .value() on an error throws.
            });
            AssertCode(ErrorCode::Internal, result.error());
            Assert::IsTrue(Contains(result.error().message, "no such script"));
        }

        // Without the TL_ASSERT override in Result.h, *r on an error is
        // undefined behaviour in Release (assert is off), and this test fails.
        TEST_METHOD(WrongAccess_ThrowsInvariantViolation)
        {
            Result<int> failed = Fail(ErrorCode::Io, "read failed");
            Assert::ExpectException<InvariantViolation>([&failed]() { int value = *failed; (void)value; });

            Result<int> succeeded = 7;
            Assert::ExpectException<InvariantViolation>([&succeeded]() { const Error &error = succeeded.error(); (void)error; });
        }

        TEST_METHOD(SciTry_ReturnsEarly)
        {
            int secondRan = 0;
            Status failed = TwoSteps(true, secondRan);
            Assert::IsFalse(failed.has_value());
            Assert::AreEqual(0, secondRan);
            Assert::AreEqual(std::string("first failed"), failed.error().message);

            Status succeeded = TwoSteps(false, secondRan);
            AssertOk(succeeded);
            Assert::AreEqual(1, secondRan);
        }

        TEST_METHOD(SciTryAssign_MovesTheValue)
        {
            Result<size_t> length = TextLength(true);
            Assert::AreEqual(size_t(5), ValueOf(length));

            Result<size_t> failed = TextLength(false);
            AssertCode(ErrorCode::NotFound, failed.error());
        }

        TEST_METHOD(SciTry_NamedResult_KeepsItsError)
        {
            Status stored = Fail(ErrorCode::Io, "disk full");
            Status first = Propagate(stored);
            Assert::AreEqual(std::string("disk full"), first.error().message);
            Assert::AreEqual(std::string("disk full"), stored.error().message, L"SCI_TRY must not move the error out of a named Result");
            Status second = Propagate(stored);
            Assert::AreEqual(std::string("disk full"), second.error().message);

            Result<size_t> both = TwoOnOneLine();
            Assert::AreEqual(size_t(10), *both);
        }

        TEST_METHOD(WithContext_AddsOnlyOnError)
        {
            Result<int> ok = WithContext(Result<int>(3), "script 1");
            AssertOk(ok);

            Result<int> failed = WithContext(Result<int>(Fail(ErrorCode::Io, "disk full")), "writing 1.hep");
            failed = WithContext(std::move(failed), "script 1 (Main)");
            Assert::AreEqual(size_t(2), failed.error().context.size());
            Assert::AreEqual(std::string("script 1 (Main): writing 1.hep: disk full [io]"), failed.error().ToString());
        }

        TEST_METHOD(ToString_IncludesTheLocation)
        {
            Error error;
            error.code = ErrorCode::Format;
            error.message = "truncated heap";
            error.where.file = "C:\\game\\resource.000";
            error.where.resource = "heap 110";
            error.where.offset = 1234;
            Assert::AreEqual(std::string("truncated heap (C:\\game\\resource.000, heap 110, offset 1234) [format]"), error.ToString());

            Error sourceError;
            sourceError.code = ErrorCode::Compile;
            sourceError.message = "3 errors";
            sourceError.where.file = "src\\rm110.sc";
            sourceError.where.line = 12;
            sourceError.where.column = 5;
            Assert::AreEqual(std::string("3 errors (src\\rm110.sc(12,5)) [compile]"), sourceError.ToString());
        }

        TEST_METHOD(FromWin32_MapsMissingFilesToNotFound)
        {
            Error missing = FromWin32(ERROR_FILE_NOT_FOUND, "opening resource.map");
            AssertCode(ErrorCode::NotFound, missing);
            Assert::IsTrue(Contains(missing.message, "opening resource.map: "));
            Assert::IsTrue(missing.message.size() > std::string("opening resource.map: ").size());

            Error denied = FromWin32(ERROR_ACCESS_DENIED, "writing 1.scr");
            AssertCode(ErrorCode::Io, denied);
        }

        TEST_METHOD(FromHResult_UsesTheWin32Code)
        {
            Error missing = FromHResult(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND), "src");
            AssertCode(ErrorCode::NotFound, missing);
            Error other = FromHResult(E_FAIL, "");
            AssertCode(ErrorCode::Io, other);
            Assert::IsFalse(other.message.empty());
        }
    };
}
