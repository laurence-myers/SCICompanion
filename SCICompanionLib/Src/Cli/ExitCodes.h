#pragma once

// The exit codes of scic (plan section 8), and the one mapping from a
// result to a code. Each row of the mapping has a unit test.

#include "Result.h"

struct CompileReport;
struct DecompileReport;

namespace cli
{
    enum class ExitCode : int
    {
        Success = 0,
        Internal = 1,       // a bug, or an escaped exception
        Usage = 2,          // a bad option, an unknown script, a header file given to compile
        CannotStart = 3,    // the game does not open, the batch does not start, or the data folder is missing
        CompileErrors = 5,
        PartialFailure = 6, // some scripts failed for a reason that is not a compile error
        Cancelled = 7,
        WriteRefused = 8,
        WriteFailed = 9,    // Io during a write
    };

    // An error before the first script: Usage gives 2, WriteRefused 8,
    // Internal 1, Cancelled 7 (Ctrl+C before the first script), and every
    // other code 3.
    ExitCode ExitCodeForStartError(const sci::Error &error);

    // What a report says, for the order of plan section 8.
    struct ReportFacts
    {
        bool internal = false;      // a status is Internal
        bool writeFailed = false;   // the commit, the tables, a move or a file write failed (Io)
        bool writeRefused = false;  // a write was refused (WriteRefused)
        bool cancelled = false;
        bool compileErrors = false; // a script has compile errors
        bool otherFailures = false; // a script failed for another reason
    };

    // The highest code that applies: 1 > 9 > 8 > 7 > 5 > 6 > 0.
    ExitCode ExitCodeForFacts(const ReportFacts &facts);
    ExitCode ExitCodeForReport(const CompileReport &report);
    ExitCode ExitCodeForReport(const DecompileReport &report);
}
