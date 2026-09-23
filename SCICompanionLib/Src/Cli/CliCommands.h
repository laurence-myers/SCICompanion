#pragma once

// The commands of scic (plan section 4), for Cli.cpp: each command gets an
// open session and its options, and returns its exit code.

#include "CliConsole.h"
#include "ExitCodes.h"
#include "Result.h"
#include <mutex>
#include <string>
#include <vector>

class GameSession;

namespace cli
{
    // The options of every command (plan section 4.1).
    struct CommonOptions
    {
        bool quiet = false;
        bool verbose = false;
        std::string logFile;
        std::string dataFolder;
        bool dryRun = false;
    };

    enum class DiagnosticLevel
    {
        Error,
        Warning,
        Message,
    };

    // What a command writes: a result to stdout; an error, a warning, a
    // message and a detail to stderr, as the verbosity allows. The log file
    // (--log) gets every message (plan sections 4.1 and 7). One lock covers
    // both: codecs log from worker threads (C1 review: before, the lock was
    // in the core-log sink only).
    class CliOutput
    {
    public:
        CliOutput(ICliConsole &console, const CommonOptions &options, LogFile *log = nullptr) : _console(console), _options(options), _log(log) {}
        CliOutput(const CliOutput &) = delete;
        CliOutput &operator=(const CliOutput &) = delete;

        // To stdout, always.
        void Result(const std::string &text) { _Write(text, true, true); }
        // To stderr, always.
        void Error(const std::string &text) { _Write("scic: error: " + text + "\n", true, false); }
        // To stderr, unless --quiet.
        void Warning(const std::string &text) { _Write("scic: warning: " + text + "\n", !_options.quiet, false); }
        void Message(const std::string &text) { _Write(text + "\n", !_options.quiet, false); }
        // To stderr, with --verbose only.
        void Detail(const std::string &text) { _Write(text + "\n", _options.verbose && !_options.quiet, false); }
        // Help and lists of commands, to stdout.
        void Help(const std::string &text) { _Write(text, true, true); }
        // A compiler diagnostic in the MSBuild format (plan section 4.5), to
        // stderr: an error always, a warning unless --quiet, a message with
        // --verbose only.
        void Diagnostic(DiagnosticLevel level, const std::string &text)
        {
            bool show = (level == DiagnosticLevel::Error) || ((level == DiagnosticLevel::Warning) && !_options.quiet) ||
                ((level == DiagnosticLevel::Message) && _options.verbose && !_options.quiet);
            _Write(text + "\n", show, false);
        }

    private:
        void _Write(const std::string &text, bool toConsole, bool toStdout)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (toConsole)
            {
                if (toStdout)
                {
                    _console.Out(text);
                }
                else
                {
                    _console.Err(text);
                }
            }
            if (_log)
            {
                _log->Write(text);
            }
        }

        ICliConsole &_console;
        const CommonOptions &_options;
        LogFile *_log;
        std::mutex _mutex;
    };

    // scic script list (plan section 4.3).
    struct ScriptListOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        std::string format = "text";
        bool derived = false;
    };

    // Prints the scripts. Success, or PartialFailure when a compiled script
    // that it read cannot be read. Fails for a bad selector (Usage).
    sci::Result<ExitCode> RunScriptList(GameSession &session, const ScriptListOptions &options, CliOutput &output);

    // scic script decompile (plan section 4.4).
    struct ScriptDecompileOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        bool all = false;
        std::string gameIni = "update";     // update, create or none
        bool resetNames = false;
        bool updateStale = false;
        bool toStdout = false;
        bool textTuples = false;
        bool asmOnly = false;
        bool debugControlFlow = false;
        bool debugInstructions = false;
        std::string debugFilter;
    };

    // Decompiles the scripts (RunDecompile) and prints the report: the
    // messages of the decompiler as they come, then the summary. --stdout
    // prints the source and writes nothing; --dry-run writes nothing and
    // lists the files that a run would write. The exit code of the report
    // (plan section 8). Fails when the run cannot start: a bad selector,
    // or --stdout with more than one script (Usage).
    sci::Result<ExitCode> RunScriptDecompile(GameSession &session, const ScriptDecompileOptions &options, const CommonOptions &common, CliOutput &output);

    // scic script sco (plan section 4.6).
    struct ScriptScoOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        bool all = false;
    };

    // Makes the .sco files (GenerateObjectFiles) and prints each script
    // that was skipped or failed, then the summary. The exit code: 6 when a
    // script failed (also for a syntax error, plan section 4.6), 9 when a
    // .sco could not be written, 7 after Ctrl+C, 1 for a bug. Fails for a
    // bad selector (Usage).
    sci::Result<ExitCode> RunScriptSco(GameSession &session, const ScriptScoOptions &options, const CommonOptions &common, CliOutput &output);
}
