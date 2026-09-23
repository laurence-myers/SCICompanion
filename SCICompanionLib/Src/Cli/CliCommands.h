#pragma once

// The commands of scic (plan section 4), for Cli.cpp: each command gets an
// open session and its options, and returns its exit code.

#include "CliConsole.h"
#include "ExitCodes.h"
#include "Result.h"
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

    // What a command writes: a result to stdout; an error, a warning, a
    // message and a detail to stderr, as the verbosity allows.
    class CliOutput
    {
    public:
        CliOutput(ICliConsole &console, const CommonOptions &options) : _console(console), _options(options) {}

        // To stdout, always.
        void Result(const std::string &text) { _console.Out(text); }
        // To stderr, always.
        void Error(const std::string &text) { _console.Err("scic: error: " + text + "\n"); }
        // To stderr, unless --quiet.
        void Warning(const std::string &text)
        {
            if (!_options.quiet)
            {
                _console.Err("scic: warning: " + text + "\n");
            }
        }
        void Message(const std::string &text)
        {
            if (!_options.quiet)
            {
                _console.Err(text + "\n");
            }
        }
        // To stderr, with --verbose only.
        void Detail(const std::string &text)
        {
            if (_options.verbose && !_options.quiet)
            {
                _console.Err(text + "\n");
            }
        }
        // Help and lists of commands, to stdout.
        void Help(const std::string &text) { _console.Out(text); }

    private:
        ICliConsole &_console;
        const CommonOptions &_options;
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
}
