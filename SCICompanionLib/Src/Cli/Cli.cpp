#include "stdafx.h"
#include "Cli.h"
#include "CliCommands.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "CliVersion.h"
#include "ExitCodes.h"
#include "GameSession.h"
#include "CoreLog.h"
#include "format.h"
#include <cstdlib>
#include <filesystem>
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
                    // C1 review: before, an error was a warning, and -q hid it.
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
            // Any length (C1 review: before, a value longer than MAX_PATH
            // was ignored with no message).
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

        // An existing file in the game folder, at any depth, that --log would
        // overwrite: "" when there is none. A .log or .txt file is not one
        // (C1 review: --log <game>\resource.map truncated the map).
        std::string GameFileOf(const std::string &logFile, const std::string &gameFolder)
        {
            std::error_code ec;
            if (gameFolder.empty() || !fs::is_regular_file(logFile, ec))
            {
                return std::string();
            }
            std::string extension = fs::path(logFile).extension().string();
            if ((_stricmp(extension.c_str(), ".log") == 0) || (_stricmp(extension.c_str(), ".txt") == 0))
            {
                return std::string();
            }
            fs::path file = fs::absolute(logFile, ec);
            for (fs::path folder = file.parent_path(); !folder.empty(); folder = folder.parent_path())
            {
                if (fs::equivalent(folder, gameFolder, ec))
                {
                    return file.string();
                }
                if (folder == folder.root_path())
                {
                    break;
                }
            }
            return std::string();
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
        std::vector<std::string> helpTopic;
        ScriptListOptions listOptions;

        CLI::App app("scic: the command line of SCI Companion. Commands: script list. \"scic help <group> <command>\" shows a command.", "scic");
        // "/games/sq3" is a path, not an option.
        app.allow_windows_style_options(false);
        // One command: the words after it are its own (for example the topic of
        // help), not another command.
        app.require_subcommand(0, 1);
        app.add_flag("-q,--quiet", common.quiet, "Show errors only.");
        app.add_flag("-v,--verbose", common.verbose, "Show more detail.");
        app.add_option("--log", common.logFile, "Also write all messages to a file.");
        CLI::Option *dataFolderOption = app.add_option("--data-dir", common.dataFolder, "The folder that holds include\\ and Decompiler\\. Default: SCIC_DATA_DIR, or the folder of scic.exe.");
        app.add_flag("--dry-run", common.dryRun, "Do the work in memory, and write nothing.");
        app.add_flag("--version", version, "Show the version.");

        CLI::App *help = app.add_subcommand("help", "Show the help of a group or of a command: scic help script list.");
        // The words after "help" are its topic, not commands to run.
        help->prefix_command();

        CLI::App *script = app.add_subcommand("script", "The script commands: list.");
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
            output.Error(error.what());
            console.Err("Run \"scic help\" for the commands and their options.\n");
            return (int)ExitCode::Usage;
        }

        if ((dataFolderOption->count() > 0) && common.dataFolder.empty())
        {
            // C1 review: before, the exe folder took its place with no message.
            output.Error("--data-dir needs a folder");
            return (int)ExitCode::Usage;
        }
        std::unique_ptr<LogFile> logFile;
        if (!common.logFile.empty())
        {
            std::string gameFile = GameFileOf(common.logFile, listOptions.gameFolder);
            if (!gameFile.empty())
            {
                output.Error("--log would overwrite " + gameFile + ", a file of the game; give another log file");
                return (int)ExitCode::Usage;
            }
            logFile = std::make_unique<LogFile>(common.logFile);
            if (!logFile->IsOpen())
            {
                // Plan section 8: an error before the first script that is
                // not a usage error.
                output.Error("cannot open the log file " + common.logFile);
                return (int)ExitCode::CannotStart;
            }
        }
        CliOutput logged(console, common, logFile.get());

        if (version)
        {
            logged.Result("scic " SCIC_VERSION_TEXT "\n");
            return (int)ExitCode::Success;
        }
        if (help->parsed())
        {
            helpTopic = help->remaining();
            CLI::App *topic = &app;
            std::string fullName = "scic";
            for (const std::string &name : helpTopic)
            {
                CLI::App *child = nullptr;
                try
                {
                    child = topic->get_subcommand(name);
                }
                catch (const CLI::OptionNotFound &)
                {
                }
                // "scic help help" shows the help of help (C1 review: the root
                // help lists help as a command).
                if (!child)
                {
                    logged.Error("no help for \"" + name + "\"");
                    return (int)ExitCode::Usage;
                }
                topic = child;
                fullName += " " + name;
            }
            logged.Help(HelpOf(topic, fullName));
            return (int)ExitCode::Success;
        }
        if (!script->parsed())
        {
            logged.Error("give a command");
            console.Err(HelpOf(&app, "scic"));
            return (int)ExitCode::Usage;
        }
        if (!list->parsed())
        {
            // Plan section 4.1: "scic script" lists the script commands.
            logged.Help(HelpOf(script, "scic script"));
            return (int)ExitCode::Success;
        }

        // A command that opens a game (plan section 7).
        std::string dataFolder = DataFolderOf(common);
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
        sci::Status opened = session.Open(listOptions.gameFolder);
        if (!opened)
        {
            // Plan section 8: 3, but 2 for a usage error (C1 review: an
            // empty game folder is Usage).
            logged.Error("cannot open the game: " + opened.error().ToString());
            return (int)ExitCodeForStartError(opened.error());
        }

        sci::Result<ExitCode> ran = sci::Guard("running the command", [&]() -> sci::Result<ExitCode>
        {
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
