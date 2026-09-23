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
        AddStatus(facts, report.tables);
        AddStatus(facts, report.commit);
        AddStatus(facts, report.moves);
        facts.cancelled = facts.cancelled || report.cancelled;
        return ExitCodeForFacts(facts);
    }

    ExitCode ExitCodeForReport(const DecompileReport &report)
    {
        ReportFacts facts;
        for (const DecompileOutcome &outcome : report.scripts)
        {
            AddStatus(facts, outcome.status);
        }
        AddStatus(facts, report.mainObjectFile);
        AddStatus(facts, report.gameIni);
        facts.cancelled = facts.cancelled || report.cancelled;
        return ExitCodeForFacts(facts);
    }
}
