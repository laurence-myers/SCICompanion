#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "Cli.h"
#include "CliConsole.h"
#include "CliVersion.h"
#include "ExitCodes.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "DecompileRun.h"
#include "Helper.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // Runs a test with no AppState, as scic does.
    struct NoAppStateForCli
    {
        AppState *saved;
        NoAppStateForCli() : saved(appState) { appState = nullptr; }
        ~NoAppStateForCli() { appState = saved; }
    };

    std::wstring WideForCli(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::string ReadFileText(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    sci::Error ErrorWith(sci::ErrorCode code)
    {
        sci::Error error;
        error.code = code;
        error.message = "x";
        return error;
    }

    // Each file of a folder tree: its size and its last write time.
    std::map<std::string, std::pair<uintmax_t, long long>> Snapshot(const std::string &folder)
    {
        std::map<std::string, std::pair<uintmax_t, long long>> files;
        for (const auto &entry : fs::recursive_directory_iterator(folder))
        {
            std::string key = entry.path().string();
            if (entry.is_regular_file())
            {
                files[key] = { entry.file_size(), entry.last_write_time().time_since_epoch().count() };
            }
            else
            {
                files[key] = { 0, 0 };
            }
        }
        return files;
    }
}

namespace UnitTests
{
    // Plan step C1: scic.exe's command line (plan sections 4, 7 and 8), run
    // in this process through RunCli.
    TEST_CLASS(TestCli)
    {
        std::string _copyFolder;

        void RemoveCopy()
        {
            if (!_copyFolder.empty())
            {
                std::error_code ec;
                fs::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

        void CopyTemplate(const char *templateFolder, bool bare = false)
        {
            RemoveCopy();
            _copyFolder = CopyGameFromModuleFolder(templateFolder);
            if (bare)
            {
                std::error_code ec;
                fs::remove(fs::path(_copyFolder) / "game.ini", ec);
                fs::remove_all(fs::path(_copyFolder) / "src", ec);
            }
        }

        // RunCli with the data folder of the tests.
        int Run(std::vector<std::string> args, cli::StringConsole &console)
        {
            args.push_back("--data-dir");
            args.push_back(GetTestModuleDirectory());
            return cli::RunCli(args, console);
        }

        static std::vector<std::string> Lines(const std::string &text)
        {
            std::vector<std::string> lines;
            std::istringstream stream(text);
            std::string line;
            while (std::getline(stream, line))
            {
                lines.push_back(line);
            }
            return lines;
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        // Plan section 8: the mapping of an error before the first script.
        TEST_METHOD(ExitCodes_StartErrors)
        {
            Assert::AreEqual(2, (int)cli::ExitCodeForStartError(ErrorWith(sci::ErrorCode::Usage)));
            Assert::AreEqual(8, (int)cli::ExitCodeForStartError(ErrorWith(sci::ErrorCode::WriteRefused)));
            Assert::AreEqual(1, (int)cli::ExitCodeForStartError(ErrorWith(sci::ErrorCode::Internal)));
            Assert::AreEqual(7, (int)cli::ExitCodeForStartError(ErrorWith(sci::ErrorCode::Cancelled)));
            for (sci::ErrorCode code : { sci::ErrorCode::NotFound, sci::ErrorCode::Format, sci::ErrorCode::Io, sci::ErrorCode::Unsupported, sci::ErrorCode::Compile })
            {
                Assert::AreEqual(3, (int)cli::ExitCodeForStartError(ErrorWith(code)), L"every other code is 3");
            }
        }

        // Plan section 8: the highest code that applies, 1 > 9 > 8 > 7 > 5 >
        // 6 > 0, a row at a time.
        TEST_METHOD(ExitCodes_TheOrderOfAReport)
        {
            cli::ReportFacts none;
            Assert::AreEqual(0, (int)cli::ExitCodeForFacts(none));
            cli::ReportFacts facts;
            facts.otherFailures = true;
            Assert::AreEqual(6, (int)cli::ExitCodeForFacts(facts));
            facts.compileErrors = true;
            Assert::AreEqual(5, (int)cli::ExitCodeForFacts(facts), L"5 before 6");
            facts.cancelled = true;
            Assert::AreEqual(7, (int)cli::ExitCodeForFacts(facts), L"7 before 5");
            facts.writeRefused = true;
            Assert::AreEqual(8, (int)cli::ExitCodeForFacts(facts), L"8 before 7");
            facts.writeFailed = true;
            Assert::AreEqual(9, (int)cli::ExitCodeForFacts(facts), L"9 before 8");
            facts.internal = true;
            Assert::AreEqual(1, (int)cli::ExitCodeForFacts(facts), L"1 before 9");
        }

        // The reports give the facts: each status to its code.
        TEST_METHOD(ExitCodes_FromTheReports)
        {
            CompileReport compiled;
            compiled.scripts.resize(1);
            Assert::AreEqual(0, (int)cli::ExitCodeForReport(compiled));
            compiled.scripts[0].status = sci::Fail(sci::ErrorCode::Compile, "errors");
            Assert::AreEqual(5, (int)cli::ExitCodeForReport(compiled));
            compiled.scripts[0].status = sci::Fail(sci::ErrorCode::NotFound, "no source");
            Assert::AreEqual(6, (int)cli::ExitCodeForReport(compiled));
            compiled.cancelled = true;
            Assert::AreEqual(7, (int)cli::ExitCodeForReport(compiled));
            compiled.commit = sci::Fail(sci::ErrorCode::WriteRefused, "a patch file hides it");
            Assert::AreEqual(8, (int)cli::ExitCodeForReport(compiled));
            compiled.tables = sci::Fail(sci::ErrorCode::Io, "the disk is full");
            Assert::AreEqual(9, (int)cli::ExitCodeForReport(compiled));
            compiled.scripts[0].status = sci::Fail(sci::ErrorCode::Internal, "a bug");
            Assert::AreEqual(1, (int)cli::ExitCodeForReport(compiled));
            CompileReport moved;
            moved.moves = sci::Fail(sci::ErrorCode::Io, "a patch file did not move");
            Assert::AreEqual(9, (int)cli::ExitCodeForReport(moved));

            DecompileReport decompiled;
            decompiled.scripts.resize(1);
            Assert::AreEqual(0, (int)cli::ExitCodeForReport(decompiled));
            decompiled.scripts[0].status = sci::Fail(sci::ErrorCode::Format, "a damaged script");
            Assert::AreEqual(6, (int)cli::ExitCodeForReport(decompiled));
            decompiled.gameIni = sci::Fail(sci::ErrorCode::Io, "game.ini is read-only");
            Assert::AreEqual(9, (int)cli::ExitCodeForReport(decompiled));
        }

        // script list on both templates, as text and as tsv (plan section 4.3).
        TEST_METHOD(List_BothTemplates_TextAndTsv)
        {
            NoAppStateForCli noAppState;
            for (const char *templateFolder : { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" })
            {
                CopyTemplate(templateFolder);
                cli::StringConsole text;
                Assert::AreEqual(0, Run({ "script", "list", _copyFolder }, text), WideForCli(text.err).c_str());
                std::vector<std::string> lines = Lines(text.out);
                Assert::IsTrue(lines.size() > 20, WideForCli(text.out).c_str());
                Assert::IsTrue(lines[0].find("No.") != std::string::npos && lines[0].find("Name from") != std::string::npos, WideForCli(lines[0]).c_str());
                Assert::IsTrue((lines[1].find("    0  Main") == 0) && (lines[1].find("game.ini") != std::string::npos), WideForCli(lines[1]).c_str());

                cli::StringConsole tsv;
                Assert::AreEqual(0, Run({ "script", "list", _copyFolder, "--format", "tsv" }, tsv), WideForCli(tsv.err).c_str());
                std::vector<std::string> rows = Lines(tsv.out);
                Assert::AreEqual(std::string("number\tname\tname_from\tin_game\tsrc\tsco\terror"), rows[0]);
                Assert::IsTrue(rows[1].find("0\tMain\tgame.ini\t") == 0, WideForCli(rows[1]).c_str());
                Assert::AreEqual(lines.size(), rows.size(), L"the same rows");
            }
        }

        // Selectors choose the rows; --derived adds the column.
        TEST_METHOD(List_SelectorsAndTheDerivedColumn)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole console;
            Assert::AreEqual(0, Run({ "script", "list", _copyFolder, "0", "door", "--format", "tsv", "--derived" }, console), WideForCli(console.err).c_str());
            std::vector<std::string> rows = Lines(console.out);
            Assert::AreEqual((size_t)3, rows.size(), WideForCli(console.out).c_str());
            Assert::IsTrue(rows[0].find("\tderived\t") != std::string::npos);
            Assert::IsTrue(rows[2].find("974\tDoor\t") == 0, WideForCli(rows[2]).c_str());
        }

        // Plan section 8: a usage error is exit code 2, before any game opens.
        TEST_METHOD(UsageErrors_ExitWith2)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            std::vector<std::vector<std::string>> cases = {
                {},
                { "nosuchcommand" },
                { "script", "list" },
                { "script", "list", _copyFolder, "--format", "xml" },
                { "script", "list", _copyFolder, "--nosuchoption" },
                { "script", "list", _copyFolder, "s1nosuchscript" },
                { "help", "nosuchtopic" },
            };
            for (const std::vector<std::string> &args : cases)
            {
                cli::StringConsole console;
                std::string text;
                for (const std::string &arg : args)
                {
                    text += arg + " ";
                }
                Assert::AreEqual(2, Run(args, console), WideForCli(text + "\n" + console.err).c_str());
                Assert::IsTrue(console.err.find("scic: error:") != std::string::npos, WideForCli(text).c_str());
            }
        }

        // Plan section 8: a game that does not open, or a data folder with no
        // include\sci.sh, is exit code 3.
        TEST_METHOD(BadFolderOrDataFolder_ExitsWith3)
        {
            NoAppStateForCli noAppState;
            cli::StringConsole badGame;
            Assert::AreEqual(3, Run({ "script", "list", (fs::temp_directory_path() / "S1NoSuchGameFolder").string() }, badGame), WideForCli(badGame.err).c_str());
            Assert::IsTrue(badGame.err.find("cannot open the game") != std::string::npos, WideForCli(badGame.err).c_str());

            CopyTemplate("\\TemplateGame\\SCI0");
            fs::path empty = fs::temp_directory_path() / "S1EmptyDataFolder";
            fs::create_directories(empty);
            cli::StringConsole badData;
            int code = cli::RunCli({ "script", "list", _copyFolder, "--data-dir", empty.string() }, badData);
            std::error_code ec;
            fs::remove_all(empty, ec);
            Assert::AreEqual(3, code, WideForCli(badData.err).c_str());
            Assert::IsTrue(badData.err.find("include\\sci.sh") != std::string::npos, WideForCli(badData.err).c_str());
        }

        // Plan section 4.3: list writes nothing, also in a game folder with no
        // game.ini and no src folder.
        TEST_METHOD(List_WritesNothing)
        {
            NoAppStateForCli noAppState;
            for (bool bare : { false, true })
            {
                CopyTemplate("\\TemplateGame\\SCI0", bare);
                auto before = Snapshot(_copyFolder);
                cli::StringConsole console;
                Assert::AreEqual(0, Run({ "script", "list", _copyFolder, "--derived" }, console), WideForCli(console.err).c_str());
                Assert::IsTrue(before == Snapshot(_copyFolder), bare ? L"no file changes (no game.ini)" : L"no file changes");
            }
        }

        // Plan section 4.3: a compiled script that list reads and cannot read
        // is listed with its error, and the exit code is 6.
        TEST_METHOD(List_UnreadableScript_ExitsWith6)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            {
                // Script 905 has no name, so list reads it to derive one.
                std::ofstream patch((fs::path(_copyFolder) / "script.905").string(), std::ios::binary);
                const char bytes[] = { (char)(0x80 | 2), 0, 5, 0, 1 };
                patch.write(bytes, sizeof(bytes));
            }
            cli::StringConsole console;
            Assert::AreEqual(6, Run({ "script", "list", _copyFolder }, console), WideForCli(console.out + console.err).c_str());
            Assert::IsTrue(console.out.find("(unreadable:") != std::string::npos, WideForCli(console.out).c_str());
        }

        // --version, help, and "scic script" (plan section 4.1). The version
        // is the version of SCICompanionLib.rc.
        TEST_METHOD(VersionAndHelp)
        {
            cli::StringConsole version;
            Assert::AreEqual(0, cli::RunCli({ "--version" }, version));
            Assert::AreEqual(std::string("scic " SCIC_VERSION_TEXT "\n"), version.out);
            std::string rc = ReadFileText(GetTestModuleDirectory() + "\\..\\SCICompanionLib\\SCICompanionLib.rc");
            Assert::IsFalse(rc.empty(), L"setup: SCICompanionLib.rc");
            std::string fileVersion = std::string(" FILEVERSION ") + std::to_string(SCIC_VERSION_MAJOR) + "," + std::to_string(SCIC_VERSION_MINOR) + "," +
                std::to_string(SCIC_VERSION_PATCH) + ",";
            Assert::IsTrue(rc.find(fileVersion) != std::string::npos, L"CliVersion.h and SCICompanionLib.rc have the same version");
            Assert::IsTrue(rc.find("VALUE \"FileVersion\", \"" SCIC_VERSION_TEXT "\"") != std::string::npos, L"the same version text");

            cli::StringConsole help;
            Assert::AreEqual(0, cli::RunCli({ "help" }, help));
            Assert::IsTrue(help.out.find("Usage: scic") != std::string::npos && help.out.find("script") != std::string::npos, WideForCli(help.out).c_str());
            cli::StringConsole helpList;
            Assert::AreEqual(0, cli::RunCli({ "help", "script", "list" }, helpList));
            Assert::IsTrue(helpList.out.find("Usage: scic script list") != std::string::npos, WideForCli(helpList.out).c_str());
            cli::StringConsole dashH;
            Assert::AreEqual(0, cli::RunCli({ "script", "list", "-h" }, dashH));
            Assert::IsTrue(dashH.out.find("Usage: scic script list") != std::string::npos, WideForCli(dashH.out).c_str());
            cli::StringConsole group;
            Assert::AreEqual(0, cli::RunCli({ "script" }, group));
            Assert::IsTrue(group.out.find("list") != std::string::npos, WideForCli(group.out).c_str());
        }

        // --log writes the messages to a file too; --quiet shows errors only.
        TEST_METHOD(LogFileAndQuiet)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string log = _copyFolder + "\\..\\S1ScicLog.txt";
            cli::StringConsole console;
            Assert::AreEqual(0, Run({ "script", "list", _copyFolder, "--log", log }, console), WideForCli(console.err).c_str());
            std::string logged = ReadFileText(log);
            std::error_code ec;
            fs::remove(log, ec);
            Assert::IsTrue(logged.find("Main") != std::string::npos, WideForCli(logged).c_str());
            cli::StringConsole noLog;
            Assert::AreEqual(3, Run({ "script", "list", _copyFolder, "--log", _copyFolder + "\\NoSuchFolder\\log.txt" }, noLog), L"a log file that cannot open");
            Assert::IsTrue(noLog.err.find("cannot open the log file") != std::string::npos, WideForCli(noLog.err).c_str());

            {
                // A conflict is a warning; --quiet leaves it out.
                std::ofstream file((fs::path(_copyFolder) / "src" / "S1Other.sc").string(), std::ios::binary);
                file << "(script# 961)\n";
                std::ofstream second((fs::path(_copyFolder) / "src" / "S1Second.sc").string(), std::ios::binary);
                second << "(script# 961)\n";
            }
            cli::StringConsole loud;
            Assert::AreEqual(0, Run({ "script", "list", _copyFolder }, loud));
            Assert::IsTrue(loud.err.find("scic: warning:") != std::string::npos, WideForCli(loud.err).c_str());
            cli::StringConsole quiet;
            Assert::AreEqual(0, Run({ "script", "list", _copyFolder, "-q" }, quiet));
            Assert::IsTrue(quiet.err.empty(), WideForCli(quiet.err).c_str());
        }
    };
}
