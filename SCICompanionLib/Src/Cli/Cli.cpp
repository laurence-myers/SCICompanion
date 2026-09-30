#include "stdafx.h"
#include "Cli.h"
#include "CliCommands.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "CliVersion.h"
#include "ExitCodes.h"
#include "GameSession.h"
#include "CoreLog.h"
#include "DecompileEngine.h"
#include "format.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
// CLI11 (from vcpkg, BSD-3-Clause), the argument parser, with no
// warnings of its own. It calls std::numeric_limits<T>::max(), which the
// min and max macros of windows.h break, so they are off for its text.
#pragma warning(push, 0)
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <CLI/CLI.hpp>
#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma warning(pop)

namespace fs = std::filesystem;

namespace cli
{
    namespace
    {
        // The core log of the engine: an error, a warning, and a message
        // with --verbose. CliOutput locks: codecs log from worker threads.
        class ConsoleLogSink : public ILogSink
        {
        public:
            explicit ConsoleLogSink(CliOutput &output) : _output(output) {}
            ~ConsoleLogSink() { RemoveCoreLogSink(this); }

            void Write(LogLevel level, const std::string &text) override
            {
                switch (level)
                {
                case LogLevel::Info:
                    _output.Detail(text);
                    break;
                case LogLevel::Warning:
                    _output.Warning(text);
                    break;
                default:
                    // An error shows also with -q.
                    _output.Error(text);
                    break;
                }
            }

        private:
            CliOutput &_output;
        };

        std::string ExeFolder()
        {
            char path[MAX_PATH] = {};
            DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if ((length == 0) || (length >= MAX_PATH))
            {
                return std::string();
            }
            return fs::path(path).parent_path().string();
        }

        // Plan section 3.5: --data-dir, else SCIC_DATA_DIR, else the folder of
        // scic.exe.
        std::string DataFolderOf(const CommonOptions &common)
        {
            if (!common.dataFolder.empty())
            {
                return common.dataFolder;
            }
            // A value of any length, also one longer than MAX_PATH.
            DWORD length = GetEnvironmentVariableA("SCIC_DATA_DIR", nullptr, 0);
            if (length > 1)
            {
                std::string value(length, '\0');
                DWORD copied = GetEnvironmentVariableA("SCIC_DATA_DIR", &value[0], length);
                if ((copied > 0) && (copied < length))
                {
                    value.resize(copied);
                    return value;
                }
            }
            return ExeFolder();
        }

        // True when --log may write over the file: there is no such file, the
        // file is empty, or it is a log of scic (its first line is the header
        // of LogFile). So --log <game>\resource.map, or a .log file that a
        // game ships, is refused. A name that is not a file (a folder, NUL)
        // goes to the open.
        bool MayOverwriteLog(const std::string &path)
        {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec) || (fs::file_size(path, ec) == 0))
            {
                return true;
            }
            std::ifstream file(path, std::ios::binary);
            char start[64] = {};
            file.read(start, sizeof(start) - 1);
            return IsLogHeader(std::string(start, (size_t)file.gcount()));
        }

        const char *CommonFooter =
            "Options of every command: -q (--quiet), -v (--verbose), --log <file>,\n"
            "--data-dir <folder>, --dry-run. See \"scic help\".";

        // A reversed copy: CLI11 parses a vector from its end.
        std::vector<std::string> Reversed(const std::vector<std::string> &args)
        {
            return std::vector<std::string>(args.rbegin(), args.rend());
        }

        // The help of one command, with its full name in the usage line
        // (App::help() gives the help of the chosen subcommand instead).
        std::string HelpOf(const CLI::App *command, const std::string &fullName)
        {
            return command->get_formatter()->make_help(command, fullName, CLI::AppFormatMode::Normal);
        }

        // The command that the arguments chose (the deepest subcommand), and
        // its full name.
        const CLI::App *ChosenCommand(const CLI::App &app, std::string &fullName)
        {
            const CLI::App *current = &app;
            fullName = "scic";
            for (;;)
            {
                std::vector<const CLI::App *> chosen = current->get_subcommands([](const CLI::App *) { return true; });
                std::vector<const CLI::App *> parsed;
                for (const CLI::App *sub : chosen)
                {
                    if (sub->parsed())
                    {
                        parsed.push_back(sub);
                    }
                }
                if (parsed.empty())
                {
                    return current;
                }
                current = parsed[0];
                fullName += " " + current->get_name();
            }
        }
    }

    int RunCli(const std::vector<std::string> &args, ICliConsole &console)
    {
        CommonOptions common;
        bool version = false;
        ScriptListOptions listOptions;
        ScriptDecompileOptions decompileOptions;
        ScriptScoOptions scoOptions;
        ScriptCompileOptions compileOptions;

        CLI::App app("scic: the command line of SCI Companion. Commands: script list, script decompile, script sco, script compile. \"scic help <group> <command>\" shows a command.", "scic");
        // "/games/sq3" is a path, not an option.
        app.allow_windows_style_options(false);
        // One command: the words after it are its own (for example the topic of
        // help), not another command.
        app.require_subcommand(0, 1);
        app.add_flag("-q,--quiet", common.quiet, "Show errors only.");
        app.add_flag("-v,--verbose", common.verbose, "Show more detail.");
        CLI::Option *logOption = app.add_option("--log", common.logFile, "Also write all messages to a file: a new file, an empty file, or a log of scic.");
        CLI::Option *dataFolderOption = app.add_option("--data-dir", common.dataFolder, "The folder that holds include\\ and Decompiler\\. Default: SCIC_DATA_DIR, or the folder of scic.exe.");
        app.add_flag("--dry-run", common.dryRun, "Do the work in memory, and write nothing.");
        app.add_flag("--version", version, "Show the version.");

        CLI::App *help = app.add_subcommand("help", "Show the help of a group or of a command: scic help script list.");
        // The words after "help" are its topic, not commands to run.
        help->prefix_command();

        CLI::App *script = app.add_subcommand("script", "The script commands: list, decompile, sco, compile.");
        script->fallthrough();
        script->require_subcommand(0, 1);
        script->footer(CommonFooter);

        CLI::App *list = script->add_subcommand("list", "Show the number and the name of each script. It writes nothing.");
        list->fallthrough();
        list->footer(CommonFooter);
        list->add_option("game-folder", listOptions.gameFolder, "The game folder.")->required();
        list->add_option("scripts", listOptions.selectors, "Numbers, ranges (100-199) or names. Default: every script.");
        list->add_option("--format", listOptions.format, "text (default) or tsv.")->check(CLI::IsMember({ "text", "tsv" }));
        list->add_flag("--derived", listOptions.derived, "Add the derived name of each script.");

        CLI::App *decompile = script->add_subcommand("decompile", "Decompile scripts into src\\<name>.sc and src\\<name>.sco, as the Decompile dialog does.");
        decompile->fallthrough();
        decompile->footer(CommonFooter);
        decompile->add_option("game-folder", decompileOptions.gameFolder, "The game folder.")->required();
        decompile->add_option("scripts", decompileOptions.selectors, "Numbers, ranges (100-199) or names. Or give --all.");
        decompile->add_flag("--all", decompileOptions.all, "Every script of the game.");
        decompile->add_option("--game-ini", decompileOptions.gameIni, "update (default): write the names into game.ini when it exists; create: also create it; none: never write it.")
            ->check(CLI::IsMember({ "update", "create", "none" }));
        decompile->add_flag("--reset-names", decompileOptions.resetNames, "Give each script its derived name, also a script that has a name.");
        decompile->add_flag("--update-stale", decompileOptions.updateStale, "Also decompile the scripts that use a renamed global by its old name.");
        decompile->add_flag("--stdout", decompileOptions.toStdout, "One script: print its source, and write nothing.");
        decompile->add_flag("--text-tuples", decompileOptions.textTuples, "Replace text resource tuples with strings.");
        decompile->add_flag("--asm-only", decompileOptions.asmOnly, "Disassemble only.");
        decompile->add_flag("--debug-control-flow", decompileOptions.debugControlFlow, "Show the control flow (decompiler debug output).");
        decompile->add_flag("--debug-instructions", decompileOptions.debugInstructions, "Show the use of the instructions (decompiler debug output).");
        decompile->add_option("--debug-filter", decompileOptions.debugFilter, "The debug output only for this function.");
        decompile->add_option("--engine", decompileOptions.engine, "classic: the control-flow graph; scope: the scope parser; auto: scope, then classic for a function that scope cannot do. Default: SCIC_DECOMPILE_ENGINE, else classic.")
            ->check(CLI::IsMember({ "classic", "scope", "auto" }));
        CLI::Option *functionReportOption = decompile->add_option("--function-report", decompileOptions.functionReport,
            "Also write a line for each function into this file (tab-separated): its script, class, name, offset and bytes, the engine, the output (classic, scope, asm or corrupt), and the result of each engine.");

        CLI::App *sco = script->add_subcommand("sco", "Make src\\<name>.sco from src\\<name>.sc and the compiled script, for source from another tool.");
        sco->fallthrough();
        sco->footer(CommonFooter);
        sco->add_option("game-folder", scoOptions.gameFolder, "The game folder.")->required();
        sco->add_option("scripts", scoOptions.selectors, "Numbers, ranges (100-199) or names. Or give --all.");
        sco->add_flag("--all", scoOptions.all, "Every script with a source file and a compiled script.");

        CLI::App *compile = script->add_subcommand("compile", "Compile scripts, as SCI Companion does, into patch files (default) or the package, as one batch.");
        compile->fallthrough();
        compile->footer(CommonFooter);
        compile->add_option("game-folder", compileOptions.gameFolder, "The game folder.")->required();
        compile->add_option("scripts", compileOptions.selectors, "Numbers, ranges (100-199), names or .sc files. Or give --all.");
        compile->add_flag("--all", compileOptions.all, "Every script that has a source file.");
        CLI::Option *toOption = compile->add_option("--to", compileOptions.to, "patch (default): patch files in the game folder; package: the resource package.")
            ->check(CLI::IsMember({ "patch", "package" }));
        compile->add_flag("--into-volume", compileOptions.intoVolume, "The same as --to package.");
        compile->add_flag("--replace-patches", compileOptions.replacePatches, "With --to package: move the patch files that would hide the new resources to replaced-patches\\<time>.");
        CLI::Option *outDirOption = compile->add_option("--out-dir", compileOptions.outDir, "Write the patch files into this folder; the game's resources do not change (its .sco files do).");
        compile->add_flag("--raw", compileOptions.raw, "With --out-dir: the plain resource data, as script.110.bin.");
        CLI::Option *passesOption = compile->add_option("--passes", compileOptions.passes, "With --all: the most passes (default 5). A pass that changes no .sco file is the last.")
            ->check(CLI::Range(1, 100));
        compile->add_flag("--fail-fast", compileOptions.failFast, "Stop after the first script that fails.");
        compile->add_flag("--no-warn-unused", compileOptions.noWarnUnused, "No warning for an instance that is not used.");

        CliOutput output(console, common);
        try
        {
            app.parse(Reversed(args));
        }
        catch (const CLI::CallForHelp &)
        {
            // The help of the command that was given.
            std::string fullName;
            const CLI::App *command = ChosenCommand(app, fullName);
            output.Help(HelpOf(command, fullName));
            return (int)ExitCode::Success;
        }
        catch (const CLI::ParseError &error)
        {
            // Not in the log: the log file is an option too.
            output.Error(error.what());
            output.HelpAfterError("Run \"scic help\" for the commands and their options.\n");
            return (int)ExitCode::Usage;
        }

        // An error of --log itself does not go into a log.
        if ((logOption->count() > 0) && common.logFile.empty())
        {
            output.Error("--log needs a file");
            return (int)ExitCode::Usage;
        }
        if (!common.logFile.empty() && !MayOverwriteLog(common.logFile))
        {
            output.Error("--log would overwrite " + common.logFile + ", which is not a log of scic (its first line is not \"scic <version> log\"); give a new file, an empty file, or a log of scic");
            return (int)ExitCode::Usage;
        }

        // The other usage errors that need no game (plan section 8). They are
        // found before the log opens, so that a log that cannot open does not
        // change their exit code; the log then gets them.
        compileOptions.passesGiven = (passesOption->count() > 0);
        compileOptions.toGiven = (toOption->count() > 0);
        std::string helpAfterUsageError;
        CLI::App *helpTopic = &app;
        std::string helpTopicName = "scic";
        auto findUsageError = [&]() -> std::string
        {
            if ((dataFolderOption->count() > 0) && common.dataFolder.empty())
            {
                // An empty --data-dir is an error: the folder of scic.exe does
                // not take its place.
                return "--data-dir needs a folder";
            }
            if ((outDirOption->count() > 0) && compileOptions.outDir.empty())
            {
                // An empty --out-dir (for example an unset variable in a build
                // script) is an error, so that the compile does not write into
                // the game, as with no --out-dir.
                return "--out-dir needs a folder";
            }
            // Plan section 4.2: decompile, sco and compile take --all or one or
            // more scripts, not both and not neither.
            for (const auto &command : { std::make_pair(decompile, std::make_pair(decompileOptions.all, !decompileOptions.selectors.empty())),
                std::make_pair(sco, std::make_pair(scoOptions.all, !scoOptions.selectors.empty())),
                std::make_pair(compile, std::make_pair(compileOptions.all, !compileOptions.selectors.empty())) })
            {
                if (command.first->parsed() && (command.second.first == command.second.second))
                {
                    return command.second.first ? "give --all or scripts, not both" : "give --all, or one or more scripts";
                }
            }
            if (decompile->parsed() && decompileOptions.toStdout && decompileOptions.all)
            {
                return "--stdout takes one script, not --all";
            }
            if ((functionReportOption->count() > 0) && decompileOptions.functionReport.empty())
            {
                return "--function-report needs a file";
            }
            if (decompile->parsed() && !decompileOptions.functionReport.empty() && !MayOverwriteFunctionReport(decompileOptions.functionReport))
            {
                return "--function-report would overwrite " + decompileOptions.functionReport + ", which is not a function report; give a new file, an empty file, or a function report";
            }
            if (decompile->parsed() && decompileOptions.engine.empty())
            {
                // An unknown engine in the variable would stop each script.
                std::string value = DecompileEngineVariableValue();
                DecompileEngine engine;
                if (!value.empty() && !ParseDecompileEngine(value, engine))
                {
                    return fmt::format("{0} is \"{1}\"; give classic, scope or auto, or give --engine", DecompileEngineVariable, value);
                }
            }
            if (version)
            {
                return std::string();
            }
            if (help->parsed())
            {
                for (const std::string &name : help->remaining())
                {
                    CLI::App *child = nullptr;
                    try
                    {
                        child = helpTopic->get_subcommand(name);
                    }
                    catch (const CLI::OptionNotFound &)
                    {
                    }
                    // "scic help help" shows the help of help: the root help
                    // lists help as a command.
                    if (!child)
                    {
                        return "no help for \"" + name + "\"";
                    }
                    helpTopic = child;
                    helpTopicName += " " + name;
                }
                return std::string();
            }
            if (!script->parsed())
            {
                helpAfterUsageError = HelpOf(&app, "scic");
                return "give a command";
            }
            return std::string();
        };
        std::string usageError = findUsageError();

        std::unique_ptr<LogFile> logFile;
        if (!common.logFile.empty())
        {
            logFile = std::make_unique<LogFile>(common.logFile);
            if (!logFile->IsOpen())
            {
                output.Error("cannot open the log file " + common.logFile);
                if (usageError.empty())
                {
                    // Plan section 8: an error before the first script that
                    // is not a usage error.
                    return (int)ExitCode::CannotStart;
                }
                logFile.reset();
            }
        }
        CliOutput logged(console, common, logFile.get());
        if (!usageError.empty())
        {
            logged.Error(usageError);
            if (!helpAfterUsageError.empty())
            {
                logged.HelpAfterError(helpAfterUsageError);
            }
            return (int)ExitCode::Usage;
        }

        if (version)
        {
            logged.Result("scic " SCIC_VERSION_TEXT "\n");
            return (int)ExitCode::Success;
        }
        if (help->parsed())
        {
            logged.Help(HelpOf(helpTopic, helpTopicName));
            return (int)ExitCode::Success;
        }
        if (!list->parsed() && !decompile->parsed() && !sco->parsed() && !compile->parsed())
        {
            // Plan section 4.1: "scic script" lists the script commands.
            logged.Help(HelpOf(script, "scic script"));
            return (int)ExitCode::Success;
        }

        // A command that opens a game (plan section 7). An absolute data
        // folder, as for the game folder below: the diagnostics of the
        // headers then have absolute paths.
        std::string dataFolder = AbsolutePath(DataFolderOf(common));
        std::error_code ec;
        if (dataFolder.empty() || !fs::exists(fs::path(dataFolder) / "include" / "sci.sh", ec))
        {
            logged.Error(fmt::format("the data folder \"{0}\" has no include\\sci.sh. Give the folder that holds include\\ and Decompiler\\ with --data-dir, or set SCIC_DATA_DIR.", dataFolder));
            return (int)ExitCode::CannotStart;
        }
        logged.Detail("The data folder: " + dataFolder);
        ConsoleLogSink sink(logged);
        ScopedCoreLogSink scopedSink(sink);
        SessionOptions sessionOptions;
        sessionOptions.dataFolder = dataFolder;
        GameSession session(sessionOptions);
        // An absolute folder: the paths of the report are then absolute, as
        // the VS Code problem matcher needs (the matcher cannot open a
        // relative path such as ".\src\rm001.sc").
        std::string gameFolder = decompile->parsed() ? decompileOptions.gameFolder :
            (sco->parsed() ? scoOptions.gameFolder : (compile->parsed() ? compileOptions.gameFolder : listOptions.gameFolder));
        sci::Status opened = session.Open(AbsolutePath(gameFolder));
        if (!opened)
        {
            // Plan section 8: 3, but 2 for a usage error (for example an
            // empty game folder).
            logged.Error("cannot open the game: " + opened.error().ToString());
            return (int)ExitCodeForStartError(opened.error());
        }

        sci::Result<ExitCode> ran = sci::Guard("running the command", [&]() -> sci::Result<ExitCode>
        {
            if (decompile->parsed())
            {
                return RunScriptDecompile(session, decompileOptions, common, logged);
            }
            if (sco->parsed())
            {
                return RunScriptSco(session, scoOptions, common, logged);
            }
            if (compile->parsed())
            {
                return RunScriptCompile(session, compileOptions, common, logged);
            }
            return RunScriptList(session, listOptions, logged);
        });
        if (!ran)
        {
            logged.Error(ran.error().ToString());
            return (int)ExitCodeForStartError(ran.error());
        }
        return (int)*ran;
    }

    int CliMain(int argc, char *argv[])
    {
        InstallCrashHandling();
        InstallCancelHandler();
        CrashForATestIfAsked();
        std::vector<std::string> args;
        for (int i = 1; i < argc; i++)
        {
            args.push_back(argv[i]);
        }
        StdConsole console;
        int code = (int)ExitCode::Internal;
        sci::Status ran = sci::Guard("scic", [&]() -> sci::Status
        {
            code = RunCli(args, console);
            return sci::Ok();
        });
        if (!ran)
        {
            console.Err("scic: error: " + ran.error().ToString() + "\n");
            return (int)ExitCode::Internal;
        }
        return code;
    }
}
