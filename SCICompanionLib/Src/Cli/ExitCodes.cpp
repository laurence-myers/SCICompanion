#include "stdafx.h"
#include "ExitCodes.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "DecompileRun.h"

namespace cli
{
    namespace
    {
        // A failed status: its code says which fact it is. An Io error is a
        // write that failed (a script's source that cannot be read is Io
        // too; it is rare, and the batch reads before it writes).
        void AddStatus(ReportFacts &facts, const sci::Status &status)
        {
            if (status)
            {
                return;
            }
            switch (status.error().code)
            {
            case sci::ErrorCode::Internal:
                facts.internal = true;
                break;
            case sci::ErrorCode::Io:
                facts.writeFailed = true;
                break;
            case sci::ErrorCode::WriteRefused:
                facts.writeRefused = true;
                break;
            case sci::ErrorCode::Cancelled:
                facts.cancelled = true;
                break;
            case sci::ErrorCode::Compile:
                facts.compileErrors = true;
                break;
            default:
                facts.otherFailures = true;
                break;
            }
        }

        // A step that writes (the commit, the tables, the moves, the .sco
        // files that go back, game.ini, main's .sco): its failure is a failed
        // write (9) whatever its code, but Internal, WriteRefused and
        // Cancelled. So a commit that fails with Format is 9, not 6 ("some
        // scripts failed").
        void AddWriteStatus(ReportFacts &facts, const sci::Status &status)
        {
            if (status)
            {
                return;
            }
            switch (status.error().code)
            {
            case sci::ErrorCode::Internal:
            case sci::ErrorCode::WriteRefused:
            case sci::ErrorCode::Cancelled:
                AddStatus(facts, status);
                break;
            default:
                facts.writeFailed = true;
                break;
            }
        }
    }

    ExitCode ExitCodeForStartError(const sci::Error &error)
    {
        switch (error.code)
        {
        case sci::ErrorCode::Usage:
            return ExitCode::Usage;
        case sci::ErrorCode::WriteRefused:
            return ExitCode::WriteRefused;
        case sci::ErrorCode::Internal:
            return ExitCode::Internal;
        case sci::ErrorCode::Cancelled:
            return ExitCode::Cancelled;
        default:
            return ExitCode::CannotStart;
        }
    }

    ExitCode ExitCodeForFacts(const ReportFacts &facts)
    {
        if (facts.internal)
        {
            return ExitCode::Internal;
        }
        if (facts.writeFailed)
        {
            return ExitCode::WriteFailed;
        }
        if (facts.writeRefused)
        {
            return ExitCode::WriteRefused;
        }
        if (facts.cancelled)
        {
            return ExitCode::Cancelled;
        }
        if (facts.compileErrors)
        {
            return ExitCode::CompileErrors;
        }
        if (facts.otherFailures)
        {
            return ExitCode::PartialFailure;
        }
        return ExitCode::Success;
    }

    ExitCode ExitCodeForReport(const CompileReport &report)
    {
        ReportFacts facts;
        for (const ScriptOutcome &outcome : report.scripts)
        {
            AddStatus(facts, outcome.status);
        }
        AddWriteStatus(facts, report.tables);
        AddWriteStatus(facts, report.commit);
        AddWriteStatus(facts, report.moves);
        AddWriteStatus(facts, report.objectFiles);
        facts.cancelled = facts.cancelled || report.cancelled;
        return ExitCodeForFacts(facts);
    }

    ExitCode ExitCodeForReport(const DecompileReport &report, size_t decompilerErrors)
    {
        ReportFacts facts;
        facts.otherFailures = (decompilerErrors > 0);
        for (const DecompileOutcome &outcome : report.scripts)
        {
            AddStatus(facts, outcome.status);
        }
        AddWriteStatus(facts, report.mainObjectFile);
        AddWriteStatus(facts, report.gameIni);
        // A batch that threw: Internal gives 1.
        AddStatus(facts, report.batch);
        facts.cancelled = facts.cancelled || report.cancelled;
        return ExitCodeForFacts(facts);
    }
}
