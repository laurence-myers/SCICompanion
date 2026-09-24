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
        // A message with a script and a line (for example "The else clause
        // must be the last clause in a cond."): shown unless --quiet, as the
        // GUI shows it.
        Info,
        // A message with no line: with --verbose only.
        Message,
    };

    // What a command writes: a result to stdout; an error, a warning, a
    // message and a detail to stderr, as the verbosity allows. The log file
    // (--log) gets every message (plan sections 4.1 and 7). One lock covers
    // both: codecs log from worker threads.
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
        // A debug dump that an option asked for, to stderr, always (also with
        // --quiet), with no "scic:" prefix: it is not a warning.
        void Dump(const std::string &text) { _Write(text + "\n", true, false); }
        // A compiler diagnostic in the MSBuild format (plan section 4.5), to
        // stderr: an error always, a warning or an info line unless --quiet,
        // a message with --verbose only.
        void Diagnostic(DiagnosticLevel level, const std::string &text)
        {
            bool show = (level == DiagnosticLevel::Error) || (((level == DiagnosticLevel::Warning) || (level == DiagnosticLevel::Info)) && !_options.quiet) ||
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
    // that it read cannot be read. Fails for a bad selector (Usage), and
    // after Ctrl+C (Cancelled).
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
    // lists the files that a run would write, with the stale scripts
    // (DecompileRunOptions::dryRun). The exit code of the report (plan
    // section 8); an error of the decompiler in a script that it wrote
    // (a function whose code it cannot find) is 6. Fails when the run
    // cannot start: a bad selector, or --stdout with more than one script
    // (Usage).
    sci::Result<ExitCode> RunScriptDecompile(GameSession &session, const ScriptDecompileOptions &options, const CommonOptions &common, CliOutput &output);

    // scic script sco (plan section 4.6).
    struct ScriptScoOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        bool all = false;
    };

    // Makes the .sco files (GenerateObjectFiles) and prints each script
    // that was skipped or failed, then the summary. With --all, a script
    // with a source file and no compiled script, or with a name in
    // game.ini and no source file, is skipped and listed. The exit code:
    // 6 when a script failed (also for a syntax error, plan section 4.6), 9
    // when a .sco could not be written (a dry run: when it could not be), 7
    // after Ctrl+C, 1 for a bug. Fails for a bad selector (Usage).
    sci::Result<ExitCode> RunScriptSco(GameSession &session, const ScriptScoOptions &options, const CommonOptions &common, CliOutput &output);

    // scic script compile (plan section 4.5).
    struct ScriptCompileOptions
    {
        std::string gameFolder;
        std::vector<std::string> selectors;
        bool all = false;
        std::string to = "patch";   // patch or package
        bool toGiven = false;
        bool intoVolume = false;    // another spelling of --to package
        bool replacePatches = false;
        std::string outDir;
        bool raw = false;
        int passes = 5;             // with --all
        bool passesGiven = false;
        bool failFast = false;
        bool noWarnUnused = false;
    };

    // Compiles the scripts as one batch (CompileScripts) and prints the
    // report: the diagnostics of the last pass in the MSBuild format (plan
    // section 4.5), the progress with --verbose, then what went where and
    // the totals. --dry-run writes nothing and lists what a run would
    // write. The exit code of the report (plan section 8). Fails when the
    // batch cannot start (the code of ExitCodeForStartError: 1, 2, 3, 7 or 8).
    sci::Result<ExitCode> RunScriptCompile(GameSession &session, const ScriptCompileOptions &options, const CommonOptions &common, CliOutput &output);

    // The full path of a folder or a file, from GetFullPathName: a relative
    // path starts at the current folder, and the case of the path stays. An
    // empty path, or one that GetFullPathName cannot take, stays as it is.
    std::string AbsolutePath(const std::string &path);
}
