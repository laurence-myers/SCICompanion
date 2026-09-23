#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "Cli.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "CliVersion.h"
#include "ExitCodes.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "CompiledScript.h"
#include "DecompileRun.h"
#include "GameSession.h"
#include "SCO.h"
#include "Helper.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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
    // Plan steps C1 and C2: scic.exe's command line (plan sections 4, 7 and
    // 8), run in this process through RunCli.
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
                if (!line.empty() && (line.back() == '\r'))
                {
                    line.pop_back();
                }
                lines.push_back(line);
            }
            return lines;
        }

        // The names of the files in src with this extension.
        static std::set<std::string> FilesOf(const std::string &game, const char *extension)
        {
            std::set<std::string> names;
            std::error_code ec;
            for (fs::directory_iterator it(fs::path(game) / "src", ec), end; !ec && (it != end); it.increment(ec))
            {
                if (_stricmp(it->path().extension().string().c_str(), extension) == 0)
                {
                    names.insert(it->path().filename().string());
                }
            }
            return names;
        }

        static std::set<std::string> SourcesOf(const std::string &game)
        {
            return FilesOf(game, ".sc");
        }

        static size_t CountObjectFiles(const std::string &game)
        {
            return FilesOf(game, ".sco").size();
        }

        static size_t RemoveObjectFiles(const std::string &game)
        {
            std::set<std::string> objectFiles = FilesOf(game, ".sco");
            for (const std::string &name : objectFiles)
            {
                fs::remove(fs::path(game) / "src" / name);
            }
            return objectFiles.size();
        }

        // The SCI1.1 template with the two scripts of TestDecompileBatch as
        // scripts 959 and 960 (as TestDecompileRun does): 959 uses global5,
        // and a decompile of 960 names it. Slot 5 of Main.sco gets its
        // standard name first.
        void PrepareStaleFixtures()
        {
            CopyTemplate("\\TemplateGame\\SCI1.1");
            SessionOptions sessionOptions;
            sessionOptions.dataFolder = GetTestModuleDirectory();
            {
                GameSession session(sessionOptions);
                Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
                std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
                Assert::IsNotNull(mainSCO.get(), L"setup: Main.sco");
                mainSCO->GetVariables()[5].SetName("global5");
                Assert::IsTrue(SaveSCOFile(session.Helper(), *mainSCO).has_value());
            }
            for (const auto &fixture : std::vector<std::pair<std::string, std::string>>{ { "BatchGlobalsA", "959" }, { "BatchGlobalsB", "960" } })
            {
                std::string text = ReadFileText(GetTestFileDirectory("Decompile\\SCI1.1") + "\\" + fixture.first + ".sc");
                std::string declared = (fixture.first == "BatchGlobalsA") ? "(script# 950)" : "(script# 951)";
                size_t at = text.find(declared);
                Assert::IsTrue(at != std::string::npos, L"setup: the fixture declares its number");
                text.replace(at, declared.size(), "(script# " + fixture.second + ")");
                std::ofstream file((fs::path(_copyFolder) / "src" / (fixture.first + ".sc")).string(), std::ios::binary | std::ios::trunc);
                file << text;
            }
            GameSession session(sessionOptions);
            Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
            ScriptId a((fs::path(_copyFolder) / "src" / "BatchGlobalsA.sc").string().c_str());
            a.SetResourceNumber(959);
            ScriptId b((fs::path(_copyFolder) / "src" / "BatchGlobalsB.sc").string().c_str());
            b.SetResourceNumber(960);
            std::atomic<bool> abort(false);
            ICompileEvents events;
            auto compiled = CompileScripts(session, { a, b }, CompileOptions(), abort, events);
            Assert::IsTrue(compiled.has_value() && compiled->Succeeded(), L"setup: the fixtures must compile");
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
            DecompileReport mainFailed;
            mainFailed.mainObjectFile = sci::Fail(sci::ErrorCode::Io, "Main.sco is read-only");
            Assert::AreEqual(9, (int)cli::ExitCodeForReport(mainFailed), L"the write of main's .sco (S4 review)");

            // C1 review: a step that writes and fails is a failed write, with
            // any code but Internal, WriteRefused and Cancelled (before: 6
            // for Format or NotFound).
            for (sci::ErrorCode code : { sci::ErrorCode::Format, sci::ErrorCode::NotFound, sci::ErrorCode::Unsupported })
            {
                CompileReport commit;
                commit.commit = sci::Fail(code, "x");
                CompileReport tables;
                tables.tables = sci::Fail(code, "x");
                CompileReport moves;
                moves.moves = sci::Fail(code, "x");
                DecompileReport gameIni;
                gameIni.gameIni = sci::Fail(code, "x");
                DecompileReport mainObjectFile;
                mainObjectFile.mainObjectFile = sci::Fail(code, "x");
                Assert::AreEqual(9, (int)cli::ExitCodeForReport(commit), L"the commit");
                Assert::AreEqual(9, (int)cli::ExitCodeForReport(tables), L"the tables");
                Assert::AreEqual(9, (int)cli::ExitCodeForReport(moves), L"the moves");
                Assert::AreEqual(9, (int)cli::ExitCodeForReport(gameIni), L"game.ini");
                Assert::AreEqual(9, (int)cli::ExitCodeForReport(mainObjectFile), L"main's .sco");
            }
            CompileReport internalCommit;
            internalCommit.commit = sci::Fail(sci::ErrorCode::Internal, "a bug");
            Assert::AreEqual(1, (int)cli::ExitCodeForReport(internalCommit));
            CompileReport cancelledCommit;
            cancelledCommit.commit = sci::Fail(sci::ErrorCode::Cancelled, "the answer was Cancel");
            Assert::AreEqual(7, (int)cli::ExitCodeForReport(cancelledCommit));
        }

        // script list on both templates, as text and as tsv (plan section
        // 4.3). No line of the text ends with a space.
        TEST_METHOD(List_BothTemplates_TextAndTsv)
        {
            NoAppStateForCli noAppState;
            for (const char *templateFolder : { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" })
            {
                CopyTemplate(templateFolder);
                cli::StringConsole text;
                int code = Run({ "script", "list", _copyFolder }, text);
                Assert::AreEqual(0, code, WideForCli(text.err).c_str());
                std::vector<std::string> lines = Lines(text.out);
                Assert::IsTrue(lines.size() > 20, WideForCli(text.out).c_str());
                Assert::IsTrue(lines[0].find("No.") != std::string::npos && lines[0].find("Name from") != std::string::npos, WideForCli(lines[0]).c_str());
                Assert::IsTrue((lines[1].find("    0  Main") == 0) && (lines[1].find("game.ini") != std::string::npos), WideForCli(lines[1]).c_str());
                for (const std::string &line : lines)
                {
                    Assert::IsTrue(line.empty() || (line.back() != ' '), WideForCli("a space at the end: [" + line + "]").c_str());
                }

                cli::StringConsole tsv;
                code = Run({ "script", "list", _copyFolder, "--format", "tsv" }, tsv);
                Assert::AreEqual(0, code, WideForCli(tsv.err).c_str());
                std::vector<std::string> rows = Lines(tsv.out);
                Assert::AreEqual(std::string("number\tname\tname_from\tin_game\tsrc\tsco\terror"), rows[0]);
                Assert::IsTrue(rows[1].find("0\tMain\tgame.ini\t") == 0, WideForCli(rows[1]).c_str());
                Assert::AreEqual(lines.size(), rows.size(), L"the same rows");
            }
        }

        // A script that game.ini names and the game has not compiled is
        // "(not compiled)" (plan section 4.3).
        TEST_METHOD(List_AScriptThatIsNotCompiled)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            Assert::IsTrue(WritePrivateProfileString("Script", "n901", "C1Ghost", (fs::path(_copyFolder) / "game.ini").string().c_str()) != 0);
            cli::StringConsole console;
            int code = Run({ "script", "list", _copyFolder, "901" }, console);
            Assert::AreEqual(0, code, WideForCli(console.err).c_str());
            std::vector<std::string> lines = Lines(console.out);
            Assert::AreEqual((size_t)2, lines.size(), WideForCli(console.out).c_str());
            Assert::IsTrue((lines[1].find("C1Ghost") != std::string::npos) && (lines[1].find("(not compiled)") != std::string::npos), WideForCli(lines[1]).c_str());
            // Its src and sco columns are "-", padded: the line must not end
            // with the padding.
            Assert::IsTrue(!lines[1].empty() && (lines[1].back() != ' '), WideForCli("a space at the end: [" + lines[1] + "]").c_str());
        }

        // Selectors choose the rows; --derived adds the column.
        TEST_METHOD(List_SelectorsAndTheDerivedColumn)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole console;
            int code = Run({ "script", "list", _copyFolder, "0", "door", "--format", "tsv", "--derived" }, console);
            Assert::AreEqual(0, code, WideForCli(console.err).c_str());
            std::vector<std::string> rows = Lines(console.out);
            Assert::AreEqual((size_t)3, rows.size(), WideForCli(console.out).c_str());
            Assert::IsTrue(rows[0].find("\tderived\t") != std::string::npos);
            Assert::IsTrue(rows[2].find("974\tDoor\t") == 0, WideForCli(rows[2]).c_str());
        }

        // Plan section 8: a usage error is exit code 2, before any game opens.
        // C1 review: an empty game folder, an empty --data-dir, and a --log
        // that would overwrite a file of the game are usage errors too.
        TEST_METHOD(UsageErrors_ExitWith2)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string map = (fs::path(_copyFolder) / "resource.map").string();
            std::string mapBefore = ReadFileText(map);
            std::vector<std::vector<std::string>> cases = {
                {},
                { "nosuchcommand" },
                { "script", "list" },
                { "script", "list", _copyFolder, "--format", "xml" },
                { "script", "list", _copyFolder, "--nosuchoption" },
                { "script", "list", _copyFolder, "s1nosuchscript" },
                { "help", "nosuchtopic" },
                { "script", "list", "" },
                { "script", "list", _copyFolder, "--log", map },
                // Plan section 4.2: --all or scripts, not both and not neither.
                { "script", "decompile", _copyFolder },
                { "script", "decompile", _copyFolder, "974", "--all" },
                { "script", "decompile", _copyFolder, "--all", "--stdout" },
                { "script", "decompile", _copyFolder, "0", "974", "--stdout" },
                { "script", "decompile", _copyFolder, "974", "--game-ini", "xml" },
                { "script", "decompile", _copyFolder, "s1nosuchscript" },
                { "script", "sco", _copyFolder },
                { "script", "sco", _copyFolder, "rm001", "--all" },
            };
            for (const std::vector<std::string> &args : cases)
            {
                cli::StringConsole console;
                std::string text;
                for (const std::string &arg : args)
                {
                    text += "[" + arg + "] ";
                }
                int code = Run(args, console);
                Assert::AreEqual(2, code, WideForCli(text + "\n" + console.err).c_str());
                Assert::IsTrue(console.err.find("scic: error:") != std::string::npos, WideForCli(text).c_str());
            }
            Assert::AreEqual(mapBefore, ReadFileText(map), L"--log did not write over resource.map");

            cli::StringConsole emptyData;
            int code = cli::RunCli({ "script", "list", _copyFolder, "--data-dir", "" }, emptyData);
            Assert::AreEqual(2, code, WideForCli(emptyData.err).c_str());
            Assert::IsTrue(emptyData.err.find("--data-dir needs a folder") != std::string::npos, WideForCli(emptyData.err).c_str());
        }

        // Plan section 8: a game that does not open, or a data folder with no
        // include\sci.sh, is exit code 3. SCIC_DATA_DIR gives the data folder
        // when --data-dir does not (plan section 3.5).
        TEST_METHOD(BadFolderOrDataFolder_ExitsWith3)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole badGame;
            int code = Run({ "script", "list", _copyFolder + "\\NoSuchGame" }, badGame);
            Assert::AreEqual(3, code, WideForCli(badGame.err).c_str());
            Assert::IsTrue(badGame.err.find("cannot open the game") != std::string::npos, WideForCli(badGame.err).c_str());

            fs::path empty = fs::path(_copyFolder) / "EmptyDataFolder";
            fs::create_directories(empty);
            cli::StringConsole badData;
            code = cli::RunCli({ "script", "list", _copyFolder, "--data-dir", empty.string() }, badData);
            Assert::AreEqual(3, code, WideForCli(badData.err).c_str());
            Assert::IsTrue(badData.err.find("include\\sci.sh") != std::string::npos, WideForCli(badData.err).c_str());

            // The test process has no include\sci.sh next to it, so without
            // --data-dir only SCIC_DATA_DIR can give the data folder.
            char saved[1024] = {};
            DWORD savedLength = GetEnvironmentVariableA("SCIC_DATA_DIR", saved, (DWORD)sizeof(saved));
            SetEnvironmentVariableA("SCIC_DATA_DIR", GetTestModuleDirectory().c_str());
            cli::StringConsole fromEnvironment;
            int fromEnvironmentCode = cli::RunCli({ "script", "list", _copyFolder }, fromEnvironment);
            SetEnvironmentVariableA("SCIC_DATA_DIR", empty.string().c_str());
            cli::StringConsole emptyEnvironment;
            int emptyEnvironmentCode = cli::RunCli({ "script", "list", _copyFolder }, emptyEnvironment);
            // C1 review: a value longer than MAX_PATH was ignored, and the
            // folder of the program took its place with no message.
            std::string longFolder = empty.string() + "\\" + std::string(250, 'x');
            SetEnvironmentVariableA("SCIC_DATA_DIR", longFolder.c_str());
            cli::StringConsole longEnvironment;
            int longEnvironmentCode = cli::RunCli({ "script", "list", _copyFolder }, longEnvironment);
            SetEnvironmentVariableA("SCIC_DATA_DIR", (savedLength > 0) ? saved : nullptr);
            Assert::AreEqual(0, fromEnvironmentCode, WideForCli(fromEnvironment.err).c_str());
            Assert::AreEqual(3, emptyEnvironmentCode, WideForCli(emptyEnvironment.err).c_str());
            Assert::IsTrue(emptyEnvironment.err.find(empty.string()) != std::string::npos, WideForCli(emptyEnvironment.err).c_str());
            Assert::AreEqual(3, longEnvironmentCode, WideForCli(longEnvironment.err).c_str());
            Assert::IsTrue(longEnvironment.err.find(longFolder) != std::string::npos, WideForCli("the long folder is the data folder:\n" + longEnvironment.err).c_str());
        }

        // Ctrl+C while list reads the scripts: no table, and exit code 7 (C1
        // review: before, list printed the table and exited with 0).
        TEST_METHOD(List_CtrlC_ExitsWith7)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::CancelFlag().store(true);
            cli::StringConsole console;
            int code = Run({ "script", "list", _copyFolder }, console);
            cli::CancelFlag().store(false);
            Assert::AreEqual(7, code, WideForCli(console.err).c_str());
            Assert::IsTrue(console.out.empty(), WideForCli(console.out).c_str());
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
                int code = Run({ "script", "list", _copyFolder, "--derived" }, console);
                Assert::AreEqual(0, code, WideForCli(console.err).c_str());
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
            int code = Run({ "script", "list", _copyFolder }, console);
            Assert::AreEqual(6, code, WideForCli(console.out + console.err).c_str());
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
            // C1 review: the root help lists help, so it has a help too.
            cli::StringConsole helpHelp;
            int code = cli::RunCli({ "help", "help" }, helpHelp);
            Assert::AreEqual(0, code, WideForCli(helpHelp.err).c_str());
            Assert::IsTrue(helpHelp.out.find("Usage: scic help") != std::string::npos, WideForCli(helpHelp.out).c_str());
        }

        // --log gets every message, whatever -q and -v say (C1 review:
        // before, it got only what the console showed); --quiet shows errors
        // only; -v shows the details. A log file that cannot open is exit
        // code 3.
        TEST_METHOD(LogFileQuietAndVerbose)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            {
                // A conflict is a warning.
                std::ofstream file((fs::path(_copyFolder) / "src" / "S1Other.sc").string(), std::ios::binary);
                file << "(script# 961)\n";
                std::ofstream second((fs::path(_copyFolder) / "src" / "S1Second.sc").string(), std::ios::binary);
                second << "(script# 961)\n";
            }
            std::string log = _copyFolder + "\\scic-test.log";
            cli::StringConsole quiet;
            int code = Run({ "script", "list", _copyFolder, "-q", "--log", log }, quiet);
            Assert::AreEqual(0, code, WideForCli(quiet.err).c_str());
            Assert::IsTrue(quiet.err.empty(), WideForCli(quiet.err).c_str());
            Assert::IsTrue(quiet.out.find("Main") != std::string::npos, L"the table goes to stdout");
            std::string logged = ReadFileText(log);
            Assert::IsTrue(logged.find("Main") != std::string::npos, WideForCli(logged).c_str());
            Assert::IsTrue(logged.find("scic: warning:") != std::string::npos, WideForCli("the log gets the warning with -q:\n" + logged).c_str());
            Assert::IsTrue(logged.find("The data folder:") != std::string::npos, WideForCli("the log gets the details with no -v:\n" + logged).c_str());

            cli::StringConsole loud;
            code = Run({ "script", "list", _copyFolder }, loud);
            Assert::AreEqual(0, code, WideForCli(loud.err).c_str());
            Assert::IsTrue(loud.err.find("scic: warning:") != std::string::npos, WideForCli(loud.err).c_str());
            Assert::IsTrue(loud.err.find("The data folder:") == std::string::npos, L"no details with no -v");
            cli::StringConsole verbose;
            code = Run({ "script", "list", _copyFolder, "-v" }, verbose);
            Assert::AreEqual(0, code, WideForCli(verbose.err).c_str());
            Assert::IsTrue(verbose.err.find("The data folder:") != std::string::npos, WideForCli(verbose.err).c_str());

            cli::StringConsole noLog;
            code = Run({ "script", "list", _copyFolder, "--log", _copyFolder + "\\NoSuchFolder\\log.txt" }, noLog);
            Assert::AreEqual(3, code, L"a log file that cannot open");
            Assert::IsTrue(noLog.err.find("cannot open the log file") != std::string::npos, WideForCli(noLog.err).c_str());
        }

        // Plan step C2 and its test row: a game that SCI Companion never
        // opened (no game.ini, no src). decompile --all writes the derived
        // names and creates no game.ini; a second run finds the same names.
        TEST_METHOD(Decompile_ABareGame_DerivedNamesAndNoGameIni)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", true);
            cli::StringConsole first;
            int code = Run({ "script", "decompile", _copyFolder, "--all" }, first);
            Assert::AreEqual(0, code, WideForCli(first.err).c_str());
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "game.ini"), L"no game.ini");
            std::set<std::string> sources = SourcesOf(_copyFolder);
            Assert::IsTrue((sources.count("Door.sc") == 1) && (sources.count("Main.sc") == 1) && (sources.size() > 25), WideForCli(first.err).c_str());
            Assert::IsTrue(first.err.find("Decompiled and wrote") != std::string::npos, WideForCli(first.err).c_str());

            cli::StringConsole second;
            code = Run({ "script", "decompile", _copyFolder, "--all" }, second);
            Assert::AreEqual(0, code, WideForCli(second.err).c_str());
            Assert::IsTrue(sources == SourcesOf(_copyFolder), L"the second run finds the same names");
        }

        // An individual run writes src\<name>.sc of the script; the summary
        // counts it.
        TEST_METHOD(Decompile_OneScript)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string door = (fs::path(_copyFolder) / "src" / "door.sc").string();
            fs::remove(door);
            cli::StringConsole console;
            int code = Run({ "script", "decompile", _copyFolder, "door" }, console);
            Assert::AreEqual(0, code, WideForCli(console.err).c_str());
            Assert::IsTrue(ReadFileText(door).find("(script# 974)") != std::string::npos, L"src\\door.sc");
            Assert::IsTrue(console.err.find("Decompiled and wrote 1 of 1 scripts.") != std::string::npos, WideForCli(console.err).c_str());
        }

        // --stdout prints the source of one script, and --dry-run lists the
        // files; neither writes a file (no src folder, no game.ini).
        TEST_METHOD(Decompile_StdoutAndDryRun_WriteNothing)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", true);
            auto before = Snapshot(_copyFolder);
            cli::StringConsole toStdout;
            int code = Run({ "script", "decompile", _copyFolder, "974", "--stdout", "--game-ini", "create" }, toStdout);
            Assert::AreEqual(0, code, WideForCli(toStdout.err).c_str());
            Assert::IsTrue(toStdout.out.find("(script# 974)") != std::string::npos, WideForCli(toStdout.out).c_str());
            Assert::IsTrue(before == Snapshot(_copyFolder), L"--stdout writes nothing");

            cli::StringConsole dryRun;
            code = Run({ "script", "decompile", _copyFolder, "--all", "--dry-run" }, dryRun);
            Assert::AreEqual(0, code, WideForCli(dryRun.err).c_str());
            Assert::IsTrue(dryRun.out.empty(), L"no source on stdout");
            Assert::IsTrue(dryRun.err.find("would write") != std::string::npos, WideForCli(dryRun.err).c_str());
            Assert::IsTrue(before == Snapshot(_copyFolder), L"--dry-run writes nothing");
        }

        // A run on some scripts reports the stale scripts; --update-stale
        // decompiles them too.
        TEST_METHOD(Decompile_StaleScripts_ReportedOrUpdated)
        {
            NoAppStateForCli noAppState;
            PrepareStaleFixtures();
            cli::StringConsole reported;
            int code = Run({ "script", "decompile", _copyFolder, "960" }, reported);
            Assert::AreEqual(0, code, WideForCli(reported.err).c_str());
            Assert::IsTrue((reported.err.find("by its old name") != std::string::npos) && (reported.err.find("959") != std::string::npos), WideForCli(reported.err).c_str());

            PrepareStaleFixtures();
            cli::StringConsole updated;
            code = Run({ "script", "decompile", _copyFolder, "960", "--update-stale" }, updated);
            Assert::AreEqual(0, code, WideForCli(updated.err).c_str());
            Assert::IsTrue(updated.err.find("by its old name") == std::string::npos, WideForCli(updated.err).c_str());
            // 959 is decompiled too (and a later group can make another
            // script stale, which the run then decompiles as well).
            std::string first = ReadFileText((fs::path(_copyFolder) / "src" / "BatchGlobalsA.sc").string());
            Assert::IsFalse(ContainsIdentifier(first, "global5"), WideForCli(updated.err + "\n" + first).c_str());
        }

        // Plan section 8: a script that fails (a truncated script in a copy)
        // is exit code 6, and the others are written.
        TEST_METHOD(Decompile_OneScriptFails_ExitsWith6)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            {
                std::ofstream patch((fs::path(_copyFolder) / "script.905").string(), std::ios::binary);
                const char bytes[] = { (char)(0x80 | 2), 0, 5, 0, 1 };
                patch.write(bytes, sizeof(bytes));
            }
            cli::StringConsole console;
            int code = Run({ "script", "decompile", _copyFolder, "905", "974" }, console);
            Assert::AreEqual(6, code, WideForCli(console.err).c_str());
            Assert::IsTrue(console.err.find("Failed: 905") != std::string::npos, WideForCli(console.err).c_str());
            Assert::IsTrue(console.err.find("Decompiled and wrote 1 of 2 scripts.") != std::string::npos, WideForCli(console.err).c_str());
        }

        // Plan section 4.6: script sco makes the .sco files of both templates
        // from their sources. The SCI1.1 template's Main and DebugHandler
        // get a warning in the MSBuild format: their compiled scripts export
        // slots that their public blocks do not list.
        TEST_METHOD(Sco_BothTemplates)
        {
            NoAppStateForCli noAppState;
            for (const char *templateFolder : { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" })
            {
                CopyTemplate(templateFolder);
                size_t removed = RemoveObjectFiles(_copyFolder);
                Assert::IsTrue(removed > 20, L"setup: the .sco files");
                cli::StringConsole console;
                int code = Run({ "script", "sco", _copyFolder, "--all" }, console);
                Assert::AreEqual(0, code, WideForCli(console.err).c_str());
                Assert::AreEqual(removed, CountObjectFiles(_copyFolder), WideForCli(console.err).c_str());
                bool sci11 = (std::string(templateFolder) == "\\TemplateGame\\SCI1.1");
                size_t warnings = 0;
                for (const std::string &line : Lines(console.err))
                {
                    if (line.find(": warning : The public block has the slots") != std::string::npos)
                    {
                        warnings++;
                        Assert::IsTrue((line.find("Main.sc(") != std::string::npos) || (line.find("DebugHandler.sc(") != std::string::npos), WideForCli(line).c_str());
                    }
                }
                Assert::AreEqual(sci11 ? (size_t)2 : (size_t)0, warnings, WideForCli(console.err).c_str());
            }
        }

        // Plan step C2: a public block that the compiler refuses, and a
        // syntax error, fail the script (exit code 6), in the MSBuild format,
        // and write no .sco. Before, the .sco builder took the public block.
        TEST_METHOD(Sco_ThePublicBlockAndSyntaxErrors_ExitWith6)
        {
            NoAppStateForCli noAppState;
            struct Case { const char *replacement; const char *error; };
            for (const Case &broken : { Case{ "rm001 0 rm001 0", ": error : Export slot 0 has already been used." }, Case{ "rm001 0 s2NoSuch 1", ": error : Unknown export s2NoSuch in slot 1." },
                Case{ "rm001 0) (procedure (s2Broken) (= ", ": error : " } })
            {
                CopyTemplate("\\TemplateGame\\SCI0");
                std::string source = (fs::path(_copyFolder) / "src" / "rm001.sc").string();
                std::string text = ReadFileText(source);
                size_t at = text.find("rm001 0");
                Assert::IsTrue(at != std::string::npos, L"setup: the public block of rm001");
                text.replace(at, 7, broken.replacement);
                {
                    std::ofstream file(source, std::ios::binary | std::ios::trunc);
                    file << text;
                }
                std::string objectFile = (fs::path(_copyFolder) / "src" / "rm001.sco").string();
                std::string objectBefore = ReadFileText(objectFile);
                cli::StringConsole console;
                int code = Run({ "script", "sco", _copyFolder, "rm001" }, console);
                Assert::AreEqual(6, code, WideForCli(console.err).c_str());
                Assert::IsTrue((console.err.find("rm001.sc(") != std::string::npos) && (console.err.find(broken.error) != std::string::npos), WideForCli(console.err).c_str());
                Assert::AreEqual(objectBefore, ReadFileText(objectFile), L"no .sco for a script that failed");
            }
        }

        // --dry-run makes the .sco files, lists them, and writes none.
        TEST_METHOD(Sco_DryRun_WritesNothing)
        {
            NoAppStateForCli noAppState;
            CopyTemplate("\\TemplateGame\\SCI0");
            RemoveObjectFiles(_copyFolder);
            auto before = Snapshot(_copyFolder);
            cli::StringConsole console;
            int code = Run({ "script", "sco", _copyFolder, "--all", "--dry-run" }, console);
            Assert::AreEqual(0, code, WideForCli(console.err).c_str());
            Assert::IsTrue(console.err.find("would write") != std::string::npos, WideForCli(console.err).c_str());
            Assert::IsTrue(before == Snapshot(_copyFolder), L"--dry-run writes nothing");
        }
    };
}
