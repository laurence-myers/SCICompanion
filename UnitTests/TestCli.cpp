#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "Cli.h"
#include "CliCommands.h"
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
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "format.h"
#include "Helper.h"
#include "TestSupport.h"
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
    sci::Error ErrorWith(sci::ErrorCode code)
    {
        sci::Error error;
        error.code = code;
        error.message = "x";
        return error;
    }

    // Records the crash items (RecordCurrentItems) for a scope. The
    // destructor ends the record, also when an assert throws.
    struct RecordedItems
    {
        std::vector<std::string> items;
        RecordedItems() { cli::RecordCurrentItems(&items); }
        ~RecordedItems()
        {
            cli::RecordCurrentItems(nullptr);
            cli::SetCurrentItem("");
        }
        RecordedItems(const RecordedItems &) = delete;
        RecordedItems &operator=(const RecordedItems &) = delete;

        std::string Text() const
        {
            std::string text;
            for (const std::string &item : items)
            {
                text += item + " | ";
            }
            return text;
        }
    };

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
    // scic.exe's command line (plan sections 4, 7 and 8), run in this
    // process through RunCli.
    TEST_CLASS(TestCli)
    {
        NoAppState _noAppState;
        GameCopy _game;
        std::string _copyFolder;

        void CopyTemplate(const char *templateFolder, bool bare = false)
        {
            _copyFolder = _game.Make(templateFolder, bare);
        }

        // RunCli with the data folder of the tests.
        int Run(std::vector<std::string> args, cli::StringConsole &console)
        {
            args.push_back("--data-dir");
            args.push_back(GetTestModuleDirectory());
            return cli::RunCli(args, console);
        }

        // Run, and assert the exit code; the message is the output.
        cli::StringConsole Expect(int exitCode, std::vector<std::string> args)
        {
            cli::StringConsole console;
            int code = Run(std::move(args), console);
            Assert::AreEqual(exitCode, code, Wide(console.out + console.err).c_str());
            return console;
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

        // The fields of a line of a report, an empty last field too.
        static std::vector<std::string> TabFields(const std::string &line)
        {
            std::vector<std::string> fields;
            size_t start = 0;
            for (;;)
            {
                size_t tab = line.find('\t', start);
                fields.push_back(line.substr(start, (tab == std::string::npos) ? std::string::npos : tab - start));
                if (tab == std::string::npos)
                {
                    return fields;
                }
                start = tab + 1;
            }
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

        // The files of the lines that start with prefix: "would write <file>"
        // (a dry run) or "wrote <file>" (--verbose), and "<file> and <file>"
        // for a script.
        static std::set<std::string> ListedFiles(const std::string &text, const std::string &prefix)
        {
            std::set<std::string> files;
            for (const std::string &line : Lines(text))
            {
                if (line.rfind(prefix, 0) == 0)
                {
                    std::string rest = line.substr(prefix.size());
                    size_t split = rest.find(" and ");
                    files.insert(rest.substr(0, split));
                    if (split != std::string::npos)
                    {
                        files.insert(rest.substr(split + 5));
                    }
                }
            }
            return files;
        }

        static bool HasFileNamed(const std::set<std::string> &files, const char *name)
        {
            return std::any_of(files.begin(), files.end(), [name](const std::string &file) { return _stricmp(fs::path(file).filename().string().c_str(), name) == 0; });
        }

        // The stale fixtures, and _copyFolder for the args of a command.
        void PrepareStaleFixtures()
        {
            ::PrepareStaleFixtures(_game);
            _copyFolder = _game.Folder();
        }

        // A patch file of the script: the script of the package, with 1 added
        // to its byte at index.
        void WriteChangedScript(uint16_t number, size_t index)
        {
            std::vector<uint8_t> bytes;
            {
                std::unique_ptr<ResourceBlob> blob = _game.Open().Helper().MostRecentResource(ResourceType::Script, number, ResourceEnumFlags::None);
                Assert::IsNotNull(blob.get(), L"setup: the script");
                bytes.assign(blob->GetData(), blob->GetData() + blob->GetLength());
                _game.CloseSessions();
            }
            Assert::IsTrue(index < bytes.size(), L"setup: the byte");
            bytes[index]++;
            // The header of an SCI0 patch file.
            bytes.insert(bytes.begin(), { (uint8_t)(0x80 | (uint8_t)ResourceType::Script), 0 });
            WriteFileBytes((fs::path(_copyFolder) / ("script." + std::to_string(number))).string(), bytes);
        }

    public:
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
            Assert::AreEqual(9, (int)cli::ExitCodeForReport(mainFailed), L"the write of main's .sco");
            // An error of the decompiler in a script that it wrote (a function
            // whose code it cannot find) is a partial failure.
            DecompileReport written;
            written.scripts.resize(1);
            Assert::AreEqual(0, (int)cli::ExitCodeForReport(written, 0));
            Assert::AreEqual(6, (int)cli::ExitCodeForReport(written, 1), L"a decompiler error");
            written.cancelled = true;
            Assert::AreEqual(7, (int)cli::ExitCodeForReport(written, 1), L"the order of plan section 8");
            // A batch that threw, with every script written: an exception
            // is Internal (1); the code of a sci::DataError gives what it
            // gives in the status of a script.
            DecompileReport batchThrew;
            batchThrew.scripts.resize(1);
            batchThrew.batch = sci::Fail(sci::ErrorCode::Internal, "an exception");
            Assert::AreEqual(1, (int)cli::ExitCodeForReport(batchThrew), L"a batch that threw");
            batchThrew.batch = sci::Fail(sci::ErrorCode::Format, "a damaged resource");
            Assert::AreEqual(6, (int)cli::ExitCodeForReport(batchThrew), L"a sci::DataError of the batch");
            Assert::IsFalse(batchThrew.Succeeded(), L"a report whose batch threw does not succeed");

            // A step that writes and fails is a failed write (9), with any
            // code but Internal, WriteRefused and Cancelled; also with Format
            // or NotFound, which give 6 in the status of a script.
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
            for (const char *templateFolder : { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" })
            {
                CopyTemplate(templateFolder);
                cli::StringConsole text = Expect(0, { "script", "list", _copyFolder });
                std::vector<std::string> lines = Lines(text.out);
                Assert::IsTrue(lines.size() > 20, Wide(text.out).c_str());
                Assert::IsTrue(lines[0].find("No.") != std::string::npos && lines[0].find("Name from") != std::string::npos, Wide(lines[0]).c_str());
                Assert::IsTrue((lines[1].find("    0  Main") == 0) && (lines[1].find("game.ini") != std::string::npos), Wide(lines[1]).c_str());
                for (const std::string &line : lines)
                {
                    Assert::IsTrue(line.empty() || (line.back() != ' '), Wide("a space at the end: [" + line + "]").c_str());
                }

                cli::StringConsole tsv = Expect(0, { "script", "list", _copyFolder, "--format", "tsv" });
                std::vector<std::string> rows = Lines(tsv.out);
                Assert::AreEqual(std::string("number\tname\tname_from\tin_game\tsrc\tsco\terror"), rows[0]);
                Assert::IsTrue(rows[1].find("0\tMain\tgame.ini\t") == 0, Wide(rows[1]).c_str());
                Assert::AreEqual(lines.size(), rows.size(), L"the same rows");
            }
        }

        // A script that game.ini names and the game has not compiled is
        // "(not compiled)" (plan section 4.3).
        TEST_METHOD(List_AScriptThatIsNotCompiled)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            Assert::IsTrue(WritePrivateProfileString("Script", "n901", "C1Ghost", (fs::path(_copyFolder) / "game.ini").string().c_str()) != 0);
            cli::StringConsole console = Expect(0, { "script", "list", _copyFolder, "901" });
            std::vector<std::string> lines = Lines(console.out);
            Assert::AreEqual((size_t)2, lines.size(), Wide(console.out).c_str());
            Assert::IsTrue((lines[1].find("C1Ghost") != std::string::npos) && (lines[1].find("(not compiled)") != std::string::npos), Wide(lines[1]).c_str());
            // Its src and sco columns are "-", padded: the line must not end
            // with the padding.
            Assert::IsTrue(!lines[1].empty() && (lines[1].back() != ' '), Wide("a space at the end: [" + lines[1] + "]").c_str());
        }

        // Selectors choose the rows; --derived adds the column.
        TEST_METHOD(List_SelectorsAndTheDerivedColumn)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole console = Expect(0, { "script", "list", _copyFolder, "0", "door", "--format", "tsv", "--derived" });
            std::vector<std::string> rows = Lines(console.out);
            Assert::AreEqual((size_t)3, rows.size(), Wide(console.out).c_str());
            Assert::IsTrue(rows[0].find("\tderived\t") != std::string::npos);
            Assert::IsTrue(rows[2].find("974\tDoor\t") == 0, Wide(rows[2]).c_str());
        }

        // Plan section 8: a usage error is exit code 2, before any game opens.
        // An empty game folder, an empty --data-dir, an empty --log, and a
        // --log that would overwrite a file of the game (also a .txt file)
        // are usage errors too.
        TEST_METHOD(UsageErrors_ExitWith2)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string map = (fs::path(_copyFolder) / "resource.map").string();
            std::string mapBefore = ReadFileText(map);
            std::string gameText = (fs::path(_copyFolder) / "game.txt").string();
            std::string gameTextBefore = ReadFileText(gameText);
            Assert::IsFalse(gameTextBefore.empty(), L"setup: the SCI0 template has game.txt");
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
                { "script", "list", _copyFolder, "--log", gameText },
                { "script", "list", _copyFolder, "--log", "" },
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
                Assert::AreEqual(2, code, Wide(text + "\n" + console.err).c_str());
                Assert::IsTrue(console.err.find("scic: error:") != std::string::npos, Wide(text).c_str());
            }
            Assert::AreEqual(mapBefore, ReadFileText(map), L"--log did not write over resource.map");
            Assert::AreEqual(gameTextBefore, ReadFileText(gameText), L"--log did not write over game.txt");

            cli::StringConsole emptyData;
            int code = cli::RunCli({ "script", "list", _copyFolder, "--data-dir", "" }, emptyData);
            Assert::AreEqual(2, code, Wide(emptyData.err).c_str());
            Assert::IsTrue(emptyData.err.find("--data-dir needs a folder") != std::string::npos, Wide(emptyData.err).c_str());
        }

        // Plan section 8: a game that does not open, or a data folder with no
        // include\sci.sh, is exit code 3. SCIC_DATA_DIR gives the data folder
        // when --data-dir does not (plan section 3.5).
        TEST_METHOD(BadFolderOrDataFolder_ExitsWith3)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole badGame = Expect(3, { "script", "list", _copyFolder + "\\NoSuchGame" });
            Assert::IsTrue(badGame.err.find("cannot open the game") != std::string::npos, Wide(badGame.err).c_str());

            fs::path empty = fs::path(_copyFolder) / "EmptyDataFolder";
            fs::create_directories(empty);
            cli::StringConsole badData;
            int code = cli::RunCli({ "script", "list", _copyFolder, "--data-dir", empty.string() }, badData);
            Assert::AreEqual(3, code, Wide(badData.err).c_str());
            Assert::IsTrue(badData.err.find("include\\sci.sh") != std::string::npos, Wide(badData.err).c_str());

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
            // A value longer than MAX_PATH also gives the data folder: the
            // folder of the program does not take its place.
            std::string longFolder = empty.string() + "\\" + std::string(250, 'x');
            SetEnvironmentVariableA("SCIC_DATA_DIR", longFolder.c_str());
            cli::StringConsole longEnvironment;
            int longEnvironmentCode = cli::RunCli({ "script", "list", _copyFolder }, longEnvironment);
            SetEnvironmentVariableA("SCIC_DATA_DIR", (savedLength > 0) ? saved : nullptr);
            Assert::AreEqual(0, fromEnvironmentCode, Wide(fromEnvironment.err).c_str());
            Assert::AreEqual(3, emptyEnvironmentCode, Wide(emptyEnvironment.err).c_str());
            Assert::IsTrue(emptyEnvironment.err.find(empty.string()) != std::string::npos, Wide(emptyEnvironment.err).c_str());
            Assert::AreEqual(3, longEnvironmentCode, Wide(longEnvironment.err).c_str());
            Assert::IsTrue(longEnvironment.err.find(longFolder) != std::string::npos, Wide("the long folder is the data folder:\n" + longEnvironment.err).c_str());

            // An empty resource.map: the game does not open.
            WriteFileText((fs::path(_copyFolder) / "resource.map").string(), "");
            cli::StringConsole emptyMap = Expect(3, { "script", "list", _copyFolder });
            Assert::IsTrue(emptyMap.out.empty() && (emptyMap.err.find("cannot open the game: resource.map is empty") != std::string::npos), Wide(emptyMap.out + emptyMap.err).c_str());
        }

        // A good map whose volume file is not there: the error names the
        // volume, not a damaged map. A map whose one entry no volume holds
        // says "its only entry", and a map with only the SCI0 terminator
        // has no entry.
        TEST_METHOD(OpenErrors_NameTheVolumeOrTheEntries)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::path map = fs::path(_copyFolder) / "resource.map";
            fs::path volume = fs::path(_copyFolder) / "resource.001";
            fs::path moved = fs::path(_copyFolder) / "moved.001";
            fs::rename(volume, moved);
            cli::StringConsole noVolume = Expect(3, { "script", "list", _copyFolder });
            Assert::IsTrue(noVolume.err.find("does not have the volume files that resource.map names: resource.001") != std::string::npos, Wide(noVolume.err).c_str());
            fs::rename(moved, volume);

            // Script 1 at offset 0 of volume 1 (the header there is another
            // resource), then the terminator.
            WriteFileBytes(map.string(), { 0x01, 0x10, 0x00, 0x00, 0x00, 0x04, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff });
            cli::StringConsole oneEntry = Expect(3, { "script", "list", _copyFolder });
            Assert::IsTrue(oneEntry.err.find("no volume file holds its only entry") != std::string::npos, Wide(oneEntry.err).c_str());

            WriteFileBytes(map.string(), { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff });
            cli::StringConsole terminatorOnly = Expect(3, { "script", "list", _copyFolder });
            Assert::IsTrue(terminatorOnly.err.find("it has no entry") != std::string::npos, Wide(terminatorOnly.err).c_str());
        }

        // Ctrl+C while list reads the scripts: no table, and exit code 7.
        TEST_METHOD(List_CtrlC_ExitsWith7)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::CancelFlag().store(true);
            cli::StringConsole console;
            int code = Run({ "script", "list", _copyFolder }, console);
            cli::CancelFlag().store(false);
            Assert::AreEqual(7, code, Wide(console.err).c_str());
            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
        }

        // A Ctrl+C that comes after list started: the console sets the flag
        // when list prints its first warning (a name conflict), before it
        // reads the scripts. No table, and exit code 7.
        TEST_METHOD(List_CtrlCAfterTheStart_ExitsWith7)
        {
            struct CancelOnWarning : public cli::StringConsole
            {
                void Err(const std::string &text) override
                {
                    StringConsole::Err(text);
                    if (text.find("scic: warning:") != std::string::npos)
                    {
                        cli::CancelFlag().store(true);
                    }
                }
            };
            CopyTemplate("\\TemplateGame\\SCI0");
            WriteFileText((fs::path(_copyFolder) / "src" / "S1Other.sc").string(), "(script# 961)\n");
            WriteFileText((fs::path(_copyFolder) / "src" / "S1Second.sc").string(), "(script# 961)\n");
            CancelOnWarning console;
            int code = Run({ "script", "list", _copyFolder }, console);
            bool flagged = cli::CancelFlag().exchange(false);
            Assert::IsTrue(flagged, Wide("setup: a warning sets the flag:\n" + console.err).c_str());
            Assert::AreEqual(7, code, Wide(console.err).c_str());
            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
        }

        // Plan section 4.3: list writes nothing, also in a game folder with no
        // game.ini and no src folder.
        TEST_METHOD(List_WritesNothing)
        {
            for (bool bare : { false, true })
            {
                CopyTemplate("\\TemplateGame\\SCI0", bare);
                auto before = Snapshot(_copyFolder);
                cli::StringConsole console = Expect(0, { "script", "list", _copyFolder, "--derived" });
                Assert::IsTrue(before == Snapshot(_copyFolder), bare ? L"no file changes (no game.ini)" : L"no file changes");
            }
        }

        // Plan section 4.3: a compiled script that list reads and cannot read
        // is listed with its error, and the exit code is 6.
        TEST_METHOD(List_UnreadableScript_ExitsWith6)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            // Script 905 has no name, so list reads it to derive one.
            WriteUnreadableScript(_game, 905);
            cli::StringConsole console;
            int code = Run({ "script", "list", _copyFolder }, console);
            Assert::AreEqual(6, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(console.out.find("(unreadable:") != std::string::npos, Wide(console.out).c_str());
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
            Assert::IsTrue(help.out.find("\nscic [OPTIONS]") != std::string::npos && help.out.find("script") != std::string::npos, Wide(help.out).c_str());
            cli::StringConsole helpList;
            Assert::AreEqual(0, cli::RunCli({ "help", "script", "list" }, helpList));
            Assert::IsTrue(helpList.out.find("\nscic script list [OPTIONS]") != std::string::npos, Wide(helpList.out).c_str());
            cli::StringConsole dashH;
            Assert::AreEqual(0, cli::RunCli({ "script", "list", "-h" }, dashH));
            Assert::IsTrue(dashH.out.find("\nscic script list [OPTIONS]") != std::string::npos, Wide(dashH.out).c_str());
            cli::StringConsole group;
            Assert::AreEqual(0, cli::RunCli({ "script" }, group));
            Assert::IsTrue(group.out.find("list") != std::string::npos, Wide(group.out).c_str());
            // The root help lists help, so it has a help too.
            cli::StringConsole helpHelp;
            int code = cli::RunCli({ "help", "help" }, helpHelp);
            Assert::AreEqual(0, code, Wide(helpHelp.err).c_str());
            Assert::IsTrue(helpHelp.out.find("\nscic help [OPTIONS]") != std::string::npos, Wide(helpHelp.out).c_str());
        }

        // --log gets every message, whatever -q and -v say, also a usage
        // error after the log opens; --quiet shows errors only; -v shows the
        // details. A second run writes its .log in the game folder again. A
        // log file that cannot open is exit code 3.
        TEST_METHOD(LogFileQuietAndVerbose)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            {
                // A conflict is a warning.
                std::ofstream file((fs::path(_copyFolder) / "src" / "S1Other.sc").string(), std::ios::binary);
                file << "(script# 961)\n";
                std::ofstream second((fs::path(_copyFolder) / "src" / "S1Second.sc").string(), std::ios::binary);
                second << "(script# 961)\n";
            }
            std::string log = _copyFolder + "\\scic-test.log";
            cli::StringConsole quiet = Expect(0, { "script", "list", _copyFolder, "-q", "--log", log });
            Assert::IsTrue(quiet.err.empty(), Wide(quiet.err).c_str());
            Assert::IsTrue(quiet.out.find("Main") != std::string::npos, L"the table goes to stdout");
            std::string logged = ReadFileText(log);
            Assert::IsTrue(logged.find("Main") != std::string::npos, Wide(logged).c_str());
            Assert::IsTrue(logged.find("scic: warning:") != std::string::npos, Wide("the log gets the warning with -q:\n" + logged).c_str());
            Assert::IsTrue(logged.find("The data folder:") != std::string::npos, Wide("the log gets the details with no -v:\n" + logged).c_str());
            cli::StringConsole again = Expect(0, { "script", "list", _copyFolder, "-q", "--log", log });

            cli::StringConsole noScripts = Expect(2, { "script", "decompile", _copyFolder, "--log", log });
            logged = ReadFileText(log);
            Assert::IsTrue(logged.find("scic: error: give --all, or one or more scripts") != std::string::npos, Wide("a usage error goes into the log:\n" + logged).c_str());
            cli::StringConsole noCommand = Expect(2, { "--log", log });
            Assert::IsTrue(noCommand.out.empty() && (noCommand.err.find("\nscic [OPTIONS]") != std::string::npos), Wide("the help goes to stderr:\n" + noCommand.err).c_str());
            logged = ReadFileText(log);
            Assert::IsTrue((logged.find("scic: error: give a command") != std::string::npos) && (logged.find("\nscic [OPTIONS]") != std::string::npos),
                Wide("the error and the help go into the log:\n" + logged).c_str());

            cli::StringConsole loud = Expect(0, { "script", "list", _copyFolder });
            Assert::IsTrue(loud.err.find("scic: warning:") != std::string::npos, Wide(loud.err).c_str());
            Assert::IsTrue(loud.err.find("The data folder:") == std::string::npos, L"no details with no -v");
            cli::StringConsole verbose = Expect(0, { "script", "list", _copyFolder, "-v" });
            Assert::IsTrue(verbose.err.find("The data folder:") != std::string::npos, Wide(verbose.err).c_str());

            cli::StringConsole noLog = Expect(3, { "script", "list", _copyFolder, "--log", _copyFolder + "\\NoSuchFolder\\log.txt" });
            Assert::IsTrue(noLog.err.find("cannot open the log file") != std::string::npos, Wide(noLog.err).c_str());
        }

        // The log starts with "scic <version> log". --log writes over a file
        // only when it is empty or a log of scic, in any folder: not a .log
        // file that a game ships, and not another file with no game folder.
        TEST_METHOD(Log_OverwritesOnlyALogOfScic)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string gameLog = _copyFolder + "\\CDW900E.LOG";
            WriteFileText(gameLog, "a log that the game ships\r\n");
            cli::StringConsole shipped = Expect(2, { "script", "list", _copyFolder, "--log", gameLog });
            Assert::IsTrue(shipped.err.find("not a log of scic") != std::string::npos, Wide(shipped.err).c_str());
            Assert::AreEqual(std::string("a log that the game ships\r\n"), ReadFileText(gameLog));

            // With no game folder.
            fs::path outside = fs::path(_copyFolder) / "notes.txt";
            WriteFileText(outside.string(), "notes\r\n");
            cli::StringConsole noGame = Expect(2, { "--version", "--log", outside.string() });
            Assert::IsTrue(noGame.out.empty(), Wide(noGame.out).c_str());
            Assert::AreEqual(std::string("notes\r\n"), ReadFileText(outside.string()));
            // A log of another version of scic, and an empty file, are written over.
            WriteFileText(outside.string(), "scic 3.9.1 log\r\nold text\r\n");
            Expect(0, { "--version", "--log", outside.string() });
            std::string logged = ReadFileText(outside.string());
            Assert::AreEqual(std::string("scic " SCIC_VERSION_TEXT " log\r\n"), logged.substr(0, logged.find('\n') + 1), Wide(logged).c_str());
            Assert::IsTrue(logged.find("old text") == std::string::npos, Wide(logged).c_str());
            WriteFileText(outside.string(), "");
            Expect(0, { "--version", "--log", outside.string() });
            Assert::IsTrue(cli::IsLogHeader(ReadFileText(outside.string())), Wide(ReadFileText(outside.string())).c_str());

            Assert::IsTrue(cli::IsLogHeader("scic 4.0.0 log\r\n") && cli::IsLogHeader("scic x log\n"));
            Assert::IsFalse(cli::IsLogHeader("scic 4.0.0 log") || cli::IsLogHeader("scic  log\n") || cli::IsLogHeader("scic 4 0 log\n") || cli::IsLogHeader("scic log\n") ||
                cli::IsLogHeader(" scic 4.0.0 log\n") || cli::IsLogHeader("scic 4.0.0 logs\n"));
        }

        // The usage errors are found before the log opens: with a log that
        // cannot open, a usage error is still exit code 2. The log gets each
        // usage error that needs no game.
        TEST_METHOD(Log_UsageErrorsBeforeTheLogOpens)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string badLog = _copyFolder + "\\NoSuchFolder\\scic.log";
            cli::StringConsole both = Expect(2, { "script", "decompile", _copyFolder, "--log", badLog });
            Assert::IsTrue((both.err.find("cannot open the log file") != std::string::npos) && (both.err.find("give --all, or one or more scripts") != std::string::npos),
                Wide(both.err).c_str());
            Expect(2, { "--log", badLog, "help", "nosuchtopic" });
            Expect(2, { "--log", badLog });

            std::string log = _copyFolder + "\\scic.log";
            const std::vector<std::pair<std::vector<std::string>, std::string>> cases = {
                { { "script", "decompile", _copyFolder, "--all", "--stdout" }, "scic: error: --stdout takes one script, not --all" },
                { { "script", "compile", _copyFolder, "rm001", "--out-dir", "" }, "scic: error: --out-dir needs a folder" },
                { { "script", "sco", _copyFolder, "rm001", "--all" }, "scic: error: give --all or scripts, not both" },
                { { "help", "nosuchtopic" }, "scic: error: no help for \"nosuchtopic\"" },
            };
            for (const auto &entry : cases)
            {
                // Before the command: the words after "help" are its topic.
                std::vector<std::string> args = { "--log", log };
                args.insert(args.end(), entry.first.begin(), entry.first.end());
                Expect(2, args);
                std::string logged = ReadFileText(log);
                Assert::IsTrue(logged.find(entry.second) != std::string::npos, Wide(entry.second + "\n" + logged).c_str());
            }
            cli::StringConsole emptyData;
            int emptyDataCode = cli::RunCli({ "script", "list", _copyFolder, "--data-dir", "", "--log", log }, emptyData);
            Assert::AreEqual(2, emptyDataCode, Wide(emptyData.err).c_str());
            Assert::IsTrue(ReadFileText(log).find("scic: error: --data-dir needs a folder") != std::string::npos, Wide(ReadFileText(log)).c_str());
        }

        // A game that SCI Companion never opened (no game.ini, no src):
        // decompile --all writes the derived names and creates no game.ini; a
        // second run finds the same names.
        TEST_METHOD(Decompile_ABareGame_DerivedNamesAndNoGameIni)
        {
            CopyTemplate("\\TemplateGame\\SCI0", true);
            cli::StringConsole first = Expect(0, { "script", "decompile", _copyFolder, "--all" });
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "game.ini"), L"no game.ini");
            std::set<std::string> sources = SourcesOf(_copyFolder);
            Assert::IsTrue((sources.count("Door.sc") == 1) && (sources.count("Main.sc") == 1) && (sources.size() > 25), Wide(first.err).c_str());
            Assert::IsTrue(first.err.find("Decompiled and wrote") != std::string::npos, Wide(first.err).c_str());

            cli::StringConsole second = Expect(0, { "script", "decompile", _copyFolder, "--all" });
            Assert::IsTrue(sources == SourcesOf(_copyFolder), L"the second run finds the same names");
        }

        // An individual run writes src\<name>.sc of the script; the summary
        // counts it.
        TEST_METHOD(Decompile_OneScript)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string door = (fs::path(_copyFolder) / "src" / "door.sc").string();
            fs::remove(door);
            cli::StringConsole console = Expect(0, { "script", "decompile", _copyFolder, "door" });
            Assert::IsTrue(ReadFileText(door).find("(script# 974)") != std::string::npos, L"src\\door.sc");
            Assert::IsTrue(console.err.find("Decompiled and wrote 1 of 1 scripts.") != std::string::npos, Wide(console.err).c_str());
        }

        // --stdout prints the source of one script, and --dry-run lists the
        // files; neither writes a file (no src folder, no game.ini).
        TEST_METHOD(Decompile_StdoutAndDryRun_WriteNothing)
        {
            CopyTemplate("\\TemplateGame\\SCI0", true);
            auto before = Snapshot(_copyFolder);
            cli::StringConsole toStdout = Expect(0, { "script", "decompile", _copyFolder, "974", "--stdout", "--game-ini", "create" });
            Assert::IsTrue(toStdout.out.find("(script# 974)") != std::string::npos, Wide(toStdout.out).c_str());
            Assert::IsTrue(before == Snapshot(_copyFolder), L"--stdout writes nothing");

            cli::StringConsole dryRun = Expect(0, { "script", "decompile", _copyFolder, "--all", "--dry-run" });
            Assert::IsTrue(dryRun.out.empty(), L"no source on stdout");
            Assert::IsTrue(dryRun.err.find("would write") != std::string::npos, Wide(dryRun.err).c_str());
            Assert::IsTrue(before == Snapshot(_copyFolder), L"--dry-run writes nothing");
        }

        // A run on some scripts reports the stale scripts; --update-stale
        // decompiles them too.
        TEST_METHOD(Decompile_StaleScripts_ReportedOrUpdated)
        {
            PrepareStaleFixtures();
            cli::StringConsole reported = Expect(0, { "script", "decompile", _copyFolder, "960" });
            Assert::IsTrue((reported.err.find("by its old name") != std::string::npos) && (reported.err.find("959") != std::string::npos), Wide(reported.err).c_str());

            PrepareStaleFixtures();
            cli::StringConsole updated = Expect(0, { "script", "decompile", _copyFolder, "960", "--update-stale" });
            Assert::IsTrue(updated.err.find("by its old name") == std::string::npos, Wide(updated.err).c_str());
            // 959 is decompiled too (and a later group can make another
            // script stale, which the run then decompiles as well).
            std::string first = ReadFileText((fs::path(_copyFolder) / "src" / "BatchGlobalsA.sc").string());
            Assert::IsFalse(ContainsIdentifier(first, "global5"), Wide(updated.err + "\n" + first).c_str());
        }

        // Plan section 8: a script that fails (a truncated script in a copy)
        // is exit code 6, and the others are written.
        TEST_METHOD(Decompile_OneScriptFails_ExitsWith6)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            WriteUnreadableScript(_game, 905);
            cli::StringConsole console = Expect(6, { "script", "decompile", _copyFolder, "905", "974" });
            Assert::IsTrue(console.err.find("Failed: 905") != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("Decompiled and wrote 1 of 2 scripts.") != std::string::npos, Wide(console.err).c_str());
        }

        // --engine, else SCIC_DECOMPILE_ENGINE, else scope: the engine
        // column of the function report. Each engine gives the same text for
        // this script.
        TEST_METHOD(Decompile_Engine_TheOptionThenTheVariable)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string report = (fs::path(_copyFolder) / "functions.tsv").string();
            // The text, and the engine of the first function of the report.
            auto decompile = [&](std::vector<std::string> options, std::string &engine)
            {
                std::vector<std::string> args = { "script", "decompile", _copyFolder, "974", "--stdout", "--function-report", report };
                args.insert(args.end(), options.begin(), options.end());
                std::string text = Expect(0, args).out;
                std::vector<std::string> lines = Lines(ReadFileText(report));
                Assert::IsTrue(lines.size() > 1, Wide(ReadFileText(report)).c_str());
                std::vector<std::string> fields = TabFields(lines[1]);
                Assert::IsTrue(fields.size() > 5, Wide(lines[1]).c_str());
                engine = fields[5];
                return text;
            };
            std::string engine;
            std::string classic;
            {
                ScopedEnvironmentVariable variable("SCIC_DECOMPILE_ENGINE", nullptr);
                classic = decompile({}, engine);
                Assert::IsTrue((classic.find("(script# 974)") != std::string::npos) && (classic.find("(asm") == std::string::npos), Wide(classic).c_str());
                Assert::AreEqual(std::string("scope"), engine, L"no option, no variable");
                for (const char *name : { "classic", "auto", "scope" })
                {
                    Assert::AreEqual(classic, decompile({ "--engine", name }, engine), Wide(name).c_str());
                    Assert::AreEqual(std::string(name), engine, Wide(name).c_str());
                }
            }
            {
                ScopedEnvironmentVariable variable("SCIC_DECOMPILE_ENGINE", "scope");
                decompile({}, engine);
                Assert::AreEqual(std::string("scope"), engine, L"the variable gives the engine");
                decompile({ "--engine", "classic" }, engine);
                Assert::AreEqual(std::string("classic"), engine, L"--engine before the variable");
            }
        }

        // An unknown engine, an empty report file, and a report file that is
        // not a function report are usage errors. The variable is not
        // checked when --engine gives the engine.
        TEST_METHOD(Decompile_Engine_UsageErrors)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            ScopedEnvironmentVariable cleared("SCIC_DECOMPILE_ENGINE", nullptr);
            Expect(2, { "script", "decompile", _copyFolder, "974", "--stdout", "--engine", "scoop" });
            cli::StringConsole empty = Expect(2, { "script", "decompile", _copyFolder, "974", "--stdout", "--function-report", "" });
            Assert::IsTrue(empty.err.find("--function-report needs a file") != std::string::npos, Wide(empty.err).c_str());
            std::string map = (fs::path(_copyFolder) / "resource.map").string();
            auto before = ReadFileBytes(map);
            cli::StringConsole notAReport = Expect(2, { "script", "decompile", _copyFolder, "974", "--stdout", "--function-report", map });
            Assert::IsTrue(notAReport.err.find("which is not a function report") != std::string::npos, Wide(notAReport.err).c_str());
            Assert::IsTrue(before == ReadFileBytes(map), L"resource.map does not change");

            ScopedEnvironmentVariable variable("SCIC_DECOMPILE_ENGINE", "scoop");
            cli::StringConsole bad = Expect(2, { "script", "decompile", _copyFolder, "974", "--stdout" });
            Assert::IsTrue(bad.err.find("SCIC_DECOMPILE_ENGINE is \"scoop\"") != std::string::npos, Wide(bad.err).c_str());
            Expect(0, { "script", "decompile", _copyFolder, "974", "--stdout", "--engine", "classic" });
        }

        // --function-report: the header, then a line of 9 fields for each
        // function, in the order of the offsets. A dry run writes it too, a
        // second run writes over it, and a report that cannot be written is
        // exit code 9.
        TEST_METHOD(Decompile_FunctionReport)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string report = (fs::path(_copyFolder) / "functions.tsv").string();
            for (const char *dryRun : { "", "--dry-run" })
            {
                std::vector<std::string> args = { "script", "decompile", _copyFolder, "974", "--engine", "auto", "--function-report", report };
                if (*dryRun)
                {
                    args.push_back(dryRun);
                }
                // A report of another run: the run writes over it.
                WriteFileText(report, std::string(cli::FunctionReportHeader) + "\n999\tx\ty\t0000\t1\tclassic\tasm\t\tgraph: old\n");
                Expect(0, args);
                std::string text = ReadFileText(report);
                Assert::IsTrue(text.find("999\t") == std::string::npos, Wide(text).c_str());
                std::vector<std::string> lines = Lines(text);
                Assert::IsTrue(lines.size() > 2, Wide(ReadFileText(report)).c_str());
                Assert::AreEqual(std::string(cli::FunctionReportHeader), lines[0]);
                int lastOffset = -1;
                for (size_t i = 1; i < lines.size(); i++)
                {
                    std::vector<std::string> fields = TabFields(lines[i]);
                    Assert::AreEqual((size_t)9, fields.size(), Wide(lines[i]).c_str());
                    Assert::AreEqual(std::string("974"), fields[0], Wide(lines[i]).c_str());
                    int offset = std::stoi(fields[3], nullptr, 16);
                    Assert::IsTrue(offset > lastOffset, L"in the order of the offsets");
                    lastOffset = offset;
                    Assert::IsTrue(std::stoi(fields[4]) > 0, Wide(lines[i]).c_str());
                    Assert::AreEqual(std::string("auto"), fields[5], Wide(lines[i]).c_str());
                    // With auto, the scope engine gives the function, or the
                    // classic engine when the scope engine fails.
                    if (fields[6] == "scope")
                    {
                        Assert::AreEqual(std::string("ok"), fields[7], Wide(lines[i]).c_str());
                        Assert::AreEqual(std::string(), fields[8], Wide(lines[i]).c_str());
                    }
                    else
                    {
                        Assert::AreEqual(std::string("classic"), fields[6], Wide(lines[i]).c_str());
                        Assert::IsTrue(fields[7].rfind("[scope:", 0) == 0, Wide(lines[i]).c_str());
                        Assert::AreEqual(std::string("ok"), fields[8], Wide(lines[i]).c_str());
                    }
                }
            }

            MakeReadOnly(report);
            cli::StringConsole readOnly = Expect(9, { "script", "decompile", _copyFolder, "974", "--stdout", "--function-report", report });
            Assert::IsTrue(readOnly.err.find("the function report") != std::string::npos, Wide(readOnly.err).c_str());
        }

        // Two export slots of one procedure are two functions with one offset:
        // two lines. Before the game opens, a report folder that does not
        // exist stops the command; a usage error leaves the report as it is;
        // a game that does not open leaves a report with no functions, not
        // the old report.
        TEST_METHOD(Decompile_FunctionReport_TwoSlotsAndEarlyWrite)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            WriteFileText((fs::path(_copyFolder) / "src" / "TwoSlots.sc").string(),
                "(script# 950)\r\n(public\r\n\ttwoSlots 0\r\n\ttwoSlots 1\r\n)\r\n\r\n(procedure (twoSlots)\r\n\t(return 1)\r\n)\r\n");
            Expect(0, { "script", "compile", _copyFolder, "950" });
            std::string report = (fs::path(_copyFolder) / "functions.tsv").string();
            Expect(0, { "script", "decompile", _copyFolder, "950", "--stdout", "--function-report", report });
            std::vector<std::string> lines = Lines(ReadFileText(report));
            Assert::AreEqual((size_t)3, lines.size(), Wide(ReadFileText(report)).c_str());
            Assert::IsTrue(lines[1].rfind("950\t\ttwoSlots\t", 0) == 0, Wide(lines[1]).c_str());
            Assert::IsTrue(lines[2].rfind("950\t\ttwoSlots\t", 0) == 0, Wide(lines[2]).c_str());

            std::string noFolder = (fs::path(_copyFolder) / "none" / "functions.tsv").string();
            cli::StringConsole missing = Expect(9, { "script", "decompile", _copyFolder, "950", "--stdout", "--function-report", noFolder });
            Assert::IsTrue(missing.err.find("the function report") != std::string::npos, Wide(missing.err).c_str());
            Assert::IsTrue(missing.out.empty(), L"no decompile");

            // A usage error leaves the file as it is.
            std::string before = ReadFileText(report);
            Expect(2, { "script", "decompile", _copyFolder, "0", "950", "--stdout", "--function-report", report });
            Assert::AreEqual(before, ReadFileText(report), L"a usage error keeps the report");

            std::string noGame = (fs::path(_copyFolder) / "nogame").string();
            Expect(3, { "script", "decompile", noGame, "950", "--stdout", "--function-report", report });
            std::vector<std::string> empty = Lines(ReadFileText(report));
            Assert::AreEqual((size_t)1, empty.size(), L"a report with no functions");
            Assert::AreEqual(std::string(cli::FunctionReportHeader), empty[0]);
        }

        // scic dev compare-structure: the decompile of a template script
        // against itself in a file of another name (paired by number), and
        // as the baseline. It opens no game, and the help
        // does not show dev.
        TEST_METHOD(Dev_CompareStructure)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::path root = fs::path(_copyFolder) / "compare";
            fs::create_directories(root / "expected");
            std::string source = Expect(0, { "script", "decompile", _copyFolder, "974", "--stdout" }).out;
            WriteFileText((root / "expected" / "other.sc").string(), source);
            fs::create_directories(root / "actual");
            WriteFileText((root / "actual" / "Door.sc").string(), source);

            std::string table = (root / "table.tsv").string();
            cli::StringConsole compared = Expect(0, { "dev", "compare-structure", (root / "expected").string(), (root / "actual").string(),
                "--baseline", (root / "actual").string(), "--out", table });
            std::vector<std::string> lines = Lines(ReadFileText(table));
            Assert::IsTrue(lines.size() > 2, Wide(ReadFileText(table)).c_str());
            Assert::AreEqual(std::string(cli::CompareStructureHeader), lines[0]);
            for (size_t i = 1; i < lines.size(); i++)
            {
                Assert::IsTrue(lines[i].rfind("974\t", 0) == 0, Wide(lines[i]).c_str());
                // The same text on both sides: SAME, and as the baseline.
                Assert::IsTrue(lines[i].find("\tSAME\tSAME\t") != std::string::npos, Wide(lines[i]).c_str());
                // The baseline is the actual folder: no change.
                Assert::IsTrue(lines[i].back() == '\t', Wide(lines[i]).c_str());
            }

            // Another text of the actual side: the functions that changed are
            // not SAME, and the change from the baseline is known.
            fs::create_directories(root / "changed");
            std::string changed = source;
            size_t method = changed.find("(method (");
            size_t bodyStart = (method != std::string::npos) ? changed.find('\n', method) : std::string::npos;
            Assert::IsTrue(bodyStart != std::string::npos, Wide("setup: a method to change\n" + changed).c_str());
            changed.insert(bodyStart + 1, "\t\t(if global1 (= global1 7))\r\n");
            WriteFileText((root / "changed" / "Door.sc").string(), changed);
            std::string changedTable = Expect(0, { "dev", "compare-structure", (root / "expected").string(), (root / "changed").string(),
                "--baseline", (root / "actual").string() }).out;
            Assert::IsTrue(changedTable.find("\tDIFF\tSAME\tCHANGED") != std::string::npos, Wide(changedTable).c_str());

            // --scripts: only these scripts; another number gives no row.
            std::vector<std::string> only = Lines(Expect(0, { "dev", "compare-structure", (root / "expected").string(), (root / "actual").string(), "--scripts", "974,1" }).out);
            Assert::AreEqual(lines.size(), only.size());
            std::vector<std::string> none = Lines(Expect(0, { "dev", "compare-structure", (root / "expected").string(), (root / "actual").string(), "--scripts", "1" }).out);
            Assert::AreEqual((size_t)1, none.size(), L"the header only");

            // It opens no game and reads no data folder.
            {
                ScopedEnvironmentVariable noData("SCIC_DATA_DIR", (root / "no-data").string().c_str());
                cli::StringConsole noGame;
                Assert::AreEqual(0, cli::RunCli({ "dev", "compare-structure", (root / "expected").string(), (root / "actual").string() }, noGame), Wide(noGame.err).c_str());
            }
            Assert::IsTrue(compared.err.find("Verdicts: ") != std::string::npos, Wide(compared.err).c_str());
            Assert::IsTrue(compared.err.find("Changes from the baseline: none.") != std::string::npos, Wide(compared.err).c_str());

            // To stdout; a file that does not parse is a warning and exit 6.
            WriteFileText((root / "actual" / "broken.sc").string(), ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 975)\r\n(procedure (p)\r\n");
            cli::StringConsole broken = Expect(6, { "dev", "compare-structure", (root / "expected").string(), (root / "actual").string() });
            Assert::IsTrue(broken.out.rfind(cli::CompareStructureHeader, 0) == 0, Wide(broken.out).c_str());
            Assert::IsTrue(broken.err.find("scic: warning: broken.sc (actual): ") != std::string::npos, Wide(broken.err).c_str());

            Expect(3, { "dev", "compare-structure", (root / "none").string(), (root / "actual").string() });
            Expect(3, { "dev", "compare-structure", "", (root / "actual").string() });
            Expect(2, { "dev", "compare-structure", (root / "expected").string(), (root / "actual").string(), "--baseline", "" });
            Expect(2, { "dev", "compare-structure", (root / "expected").string() });
            // RunCli itself: Run adds --data-dir, which help takes as a topic.
            cli::StringConsole help;
            Assert::AreEqual(0, cli::RunCli({ "help" }, help));
            Assert::IsTrue(help.out.find("Tools for the development") == std::string::npos, Wide(help.out).c_str());
        }

        // scic dev compare-meaning: a template script against the script that
        // a compile of its decompiled text makes (--out-dir --raw); a text
        // with another meaning gives DIFF, and a script with no compiled file
        // gives UNCOMPARED.
        TEST_METHOD(Dev_CompareMeaning)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::path root = fs::path(_copyFolder) / "meaning";
            fs::create_directories(root / "same");
            Expect(0, { "script", "decompile", _copyFolder, "974", "--engine", "scope" });
            Expect(0, { "script", "compile", _copyFolder, "974", "--out-dir", (root / "same").string(), "--raw" });
            std::string table = (root / "table.tsv").string();
            cli::StringConsole compared = Expect(0, { "dev", "compare-meaning", _copyFolder, (root / "same").string(), "--scripts", "974", "--out", table });
            std::vector<std::string> lines = Lines(ReadFileText(table));
            Assert::IsTrue(lines.size() > 2, Wide(ReadFileText(table)).c_str());
            Assert::AreEqual(std::string(cli::CompareMeaningHeader), lines[0]);
            for (size_t i = 1; i < lines.size(); i++)
            {
                Assert::IsTrue(lines[i].rfind("974\t", 0) == 0, Wide(lines[i]).c_str());
                Assert::IsTrue(lines[i].find("\tSAME\t") != std::string::npos, Wide(lines[i]).c_str());
            }
            Assert::IsTrue(compared.err.find("Verdicts: SAME") != std::string::npos, Wide(compared.err).c_str());

            // Another meaning: a store to a property at the start of a method.
            fs::path source = fs::path(_copyFolder) / "src" / "Door.sc";
            std::string changed = ReadFileText(source.string());
            size_t method = changed.find("(method (");
            size_t bodyStart = (method != std::string::npos) ? changed.find('\n', method) : std::string::npos;
            Assert::IsTrue(bodyStart != std::string::npos, Wide("setup: a method to change\n" + changed).c_str());
            changed.insert(bodyStart + 1, "\t\t(= x 7)\r\n");
            WriteFileText(source.string(), changed);
            fs::create_directories(root / "changed");
            Expect(0, { "script", "compile", _copyFolder, "974", "--out-dir", (root / "changed").string(), "--raw" });
            std::string changedTable = Expect(0, { "dev", "compare-meaning", _copyFolder, (root / "changed").string(), "--scripts", "974" }).out;
            Assert::IsTrue(changedTable.find("\tDIFF\t") != std::string::npos, Wide(changedTable).c_str());
            Assert::IsTrue(changedTable.find("| store P") != std::string::npos, Wide(changedTable).c_str());

            // No compiled file: each function is UNCOMPARED.
            fs::create_directories(root / "empty");
            std::vector<std::string> empty = Lines(Expect(0, { "dev", "compare-meaning", _copyFolder, (root / "empty").string(), "--scripts", "974" }).out);
            Assert::AreEqual(lines.size(), empty.size());
            for (size_t i = 1; i < empty.size(); i++)
            {
                Assert::IsTrue(empty[i].find("\tUNCOMPARED\tnot-recompiled") != std::string::npos, Wide(empty[i]).c_str());
            }

            // The scripts are the script resources: game.ini can be missing.
            fs::remove(fs::path(_copyFolder) / "game.ini");
            Assert::AreEqual(lines.size(), Lines(Expect(0, { "dev", "compare-meaning", _copyFolder, (root / "same").string(), "--scripts", "974" }).out).size());

            Expect(3, { "dev", "compare-meaning", _copyFolder, (root / "none").string() });
            Expect(2, { "dev", "compare-meaning", _copyFolder, (root / "same").string(), "--out", "" });
            Expect(2, { "dev", "compare-meaning", _copyFolder });
        }

        // Plan section 4.6: script sco makes the .sco files of both templates
        // from their sources. The SCI1.1 template's Main and DebugHandler
        // get a warning in the MSBuild format: their compiled scripts export
        // slots that their public blocks do not list.
        TEST_METHOD(Sco_BothTemplates)
        {
            for (const char *templateFolder : { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" })
            {
                CopyTemplate(templateFolder);
                size_t removed = RemoveObjectFiles(_copyFolder);
                Assert::IsTrue(removed > 20, L"setup: the .sco files");
                cli::StringConsole console = Expect(0, { "script", "sco", _copyFolder, "--all" });
                Assert::AreEqual(removed, CountObjectFiles(_copyFolder), Wide(console.err).c_str());
                bool sci11 = (std::string(templateFolder) == "\\TemplateGame\\SCI1.1");
                size_t warnings = 0;
                for (const std::string &line : Lines(console.err))
                {
                    if (line.find(": warning : The public block has the slots") != std::string::npos)
                    {
                        warnings++;
                        Assert::IsTrue((line.find("Main.sc(") != std::string::npos) || (line.find("DebugHandler.sc(") != std::string::npos), Wide(line).c_str());
                    }
                }
                Assert::AreEqual(sci11 ? (size_t)2 : (size_t)0, warnings, Wide(console.err).c_str());
            }
        }

        // A public block that the compiler refuses, and a syntax error, fail
        // the script (exit code 6), in the MSBuild format, and write no .sco:
        // the .sco builder checks the public block as the compiler does.
        TEST_METHOD(Sco_ThePublicBlockAndSyntaxErrors_ExitWith6)
        {
            // The position: the end of the entry, 1-based, and the path as it
            // was given, with the case of its folder.
            struct Case { const char *replacement; const char *error; const char *position; };
            for (const Case &broken : { Case{ "rm001 0 rm001 0", ": error : Export slot 0 has already been used.", "(23,17)" },
                Case{ "rm001 0 s2NoSuch 1", ": error : Unknown export s2NoSuch in slot 1.", "(23,20)" }, Case{ "rm001 0) (procedure (s2Broken) (= ", ": error : ", "" } })
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
                cli::StringConsole console = Expect(6, { "script", "sco", _copyFolder, "rm001" });
                Assert::IsTrue((console.err.find("rm001.sc(") != std::string::npos) && (console.err.find(broken.error) != std::string::npos), Wide(console.err).c_str());
                if (*broken.position)
                {
                    std::string expected = source + broken.position + broken.error;
                    Assert::IsTrue(console.err.find(expected) != std::string::npos, Wide(expected + "\n" + console.err).c_str());
                }
                Assert::AreEqual(objectBefore, ReadFileText(objectFile), L"no .sco for a script that failed");
            }
        }

        // --dry-run makes the .sco files, lists them, and writes none.
        TEST_METHOD(Sco_DryRun_WritesNothing)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            RemoveObjectFiles(_copyFolder);
            auto before = Snapshot(_copyFolder);
            cli::StringConsole console = Expect(0, { "script", "sco", _copyFolder, "--all", "--dry-run" });
            Assert::IsTrue(console.err.find("would write") != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(before == Snapshot(_copyFolder), L"--dry-run writes nothing");
        }

        // A dry run lists the files that a run writes: main's .sco, the
        // decompiler files of src, game.ini and, with --update-stale, the
        // scripts of each later group. Without --update-stale, it gives the
        // stale warning. The dry run writes nothing, so the same copy then
        // takes the run.
        TEST_METHOD(Decompile_DryRun_ListsWhatARunWrites)
        {
            for (int caseIndex = 0; caseIndex < 3; caseIndex++)
            {
                std::vector<std::string> args;
                if (caseIndex < 2)
                {
                    // The stale fixtures: 960 names a global that 959 uses.
                    PrepareStaleFixtures();
                    args = { "script", "decompile", _copyFolder, "960" };
                    if (caseIndex == 1)
                    {
                        args.push_back("--update-stale");
                    }
                }
                else
                {
                    // A game that SCI Companion never opened.
                    CopyTemplate("\\TemplateGame\\SCI0", true);
                    args = { "script", "decompile", _copyFolder, "974", "--game-ini", "create" };
                }
                auto before = Snapshot(_copyFolder);
                std::vector<std::string> dryArgs = args;
                dryArgs.push_back("--dry-run");
                cli::StringConsole dryRun = Expect(0, dryArgs);
                Assert::IsTrue(before == Snapshot(_copyFolder), Wide("the dry run writes nothing: " + dryRun.err).c_str());
                std::vector<std::string> runArgs = args;
                runArgs.push_back("-v");
                cli::StringConsole run = Expect(0, runArgs);
                std::set<std::string> wouldWrite = ListedFiles(dryRun.err, "would write ");
                std::set<std::string> wrote = ListedFiles(run.err, "wrote ");
                std::string facts = "dry run:\n" + dryRun.err + "\nrun:\n" + run.err;
                Assert::IsFalse(wrote.empty(), Wide(facts).c_str());
                Assert::IsTrue(wouldWrite == wrote, Wide(facts).c_str());
                // One line for each file: the batch's "Generated" line does
                // not print, also with --verbose.
                Assert::IsTrue(run.err.find("Generated ") == std::string::npos, Wide(run.err).c_str());
                if (caseIndex < 2)
                {
                    Assert::IsTrue(HasFileNamed(wouldWrite, "Main.sco"), Wide("main's .sco with the new name: " + facts).c_str());
                    Assert::AreEqual(caseIndex == 1, HasFileNamed(wouldWrite, "BatchGlobalsA.sc"), Wide("the stale script: " + facts).c_str());
                    if (caseIndex == 0)
                    {
                        Assert::IsTrue(dryRun.err.find("would use a global of the run by its old name: 959") != std::string::npos, Wide(facts).c_str());
                    }
                }
                else
                {
                    Assert::IsTrue(HasFileNamed(wouldWrite, "Decompiler.ini") && HasFileNamed(wouldWrite, "game.ini"), Wide(facts).c_str());
                }
            }
        }

        // --reset-names gives a script its derived name, and --game-ini
        // decides what game.ini gets. A dry run says "would keep", and
        // --stdout says nothing of the old files, as neither writes a file.
        TEST_METHOD(Decompile_ResetNamesAndTheGameIniModes)
        {
            // Script 974 is OldDoor in game.ini, with its files.
            auto prepare = [this]()
            {
                CopyTemplate("\\TemplateGame\\SCI0");
                fs::path src = fs::path(_copyFolder) / "src";
                fs::rename(src / "door.sc", src / "OldDoor.sc");
                fs::rename(src / "door.sco", src / "OldDoor.sco");
                Assert::IsTrue(WritePrivateProfileStringA("Script", "n974", "OldDoor", (fs::path(_copyFolder) / "game.ini").string().c_str()) != 0, L"setup: game.ini");
            };
            auto nameInGameIni = [this]()
            {
                char value[64] = {};
                GetPrivateProfileStringA("Script", "n974", "", value, sizeof(value), (fs::path(_copyFolder) / "game.ini").string().c_str());
                return std::string(value);
            };
            auto source = [this](const char *name) { return ReadFileText((fs::path(_copyFolder) / "src" / name).string()); };

            prepare();
            cli::StringConsole keep = Expect(0, { "script", "decompile", _copyFolder, "974" });
            Assert::IsTrue(source("OldDoor.sc").find("(script# 974)") != std::string::npos, Wide(keep.err).c_str());
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "src" / "Door.sc"), L"without --reset-names, the name stays");

            prepare();
            cli::StringConsole reset = Expect(0, { "script", "decompile", _copyFolder, "974", "--reset-names" });
            Assert::IsTrue(source("Door.sc").find("(script# 974)") != std::string::npos, Wide(reset.err).c_str());
            Assert::AreEqual(std::string("Door"), nameInGameIni(), L"update: game.ini gets the new name");
            Assert::IsTrue(reset.err.find("OldDoor.sc keeps its old name: script 974 is now Door") != std::string::npos, Wide(reset.err).c_str());

            prepare();
            cli::StringConsole none = Expect(0, { "script", "decompile", _copyFolder, "974", "--reset-names", "--game-ini", "none" });
            Assert::IsTrue(fs::exists(fs::path(_copyFolder) / "src" / "Door.sc"), Wide(none.err).c_str());
            Assert::AreEqual(std::string("OldDoor"), nameInGameIni(), L"none: game.ini does not change");

            prepare();
            cli::StringConsole dryRun = Expect(0, { "script", "decompile", _copyFolder, "974", "--reset-names", "--dry-run" });
            Assert::IsTrue(dryRun.err.find("OldDoor.sc would keep its old name: script 974 would be Door") != std::string::npos, Wide(dryRun.err).c_str());
            cli::StringConsole toStdout = Expect(0, { "script", "decompile", _copyFolder, "974", "--reset-names", "--stdout" });
            Assert::IsTrue(toStdout.err.find("old name") == std::string::npos, Wide(toStdout.err).c_str());
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "src" / "Door.sc"), L"the dry run and --stdout write nothing");

            // create: a game with no game.ini gets one.
            CopyTemplate("\\TemplateGame\\SCI0", true);
            cli::StringConsole create = Expect(0, { "script", "decompile", _copyFolder, "974", "--game-ini", "create" });
            Assert::AreEqual(std::string("Door"), nameInGameIni(), L"create: game.ini with the name");
        }

        // Ctrl+C stops decompile and sco, with exit code 7.
        TEST_METHOD(DecompileAndSco_CtrlC_ExitWith7)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            struct CtrlC
            {
                CtrlC() { cli::CancelFlag().store(true); }
                ~CtrlC() { cli::CancelFlag().store(false); }
            } ctrlC;
            // The text for one script, and for two.
            cli::StringConsole decompile = Expect(7, { "script", "decompile", _copyFolder, "974" });
            Assert::IsTrue(decompile.err.find("Stopped by Ctrl+C: 1 script was not decompiled.") != std::string::npos, Wide(decompile.err).c_str());
            cli::StringConsole decompileTwo = Expect(7, { "script", "decompile", _copyFolder, "974", "rm001" });
            Assert::IsTrue(decompileTwo.err.find("Stopped by Ctrl+C: 2 scripts were not decompiled.") != std::string::npos, Wide(decompileTwo.err).c_str());
            cli::StringConsole sco = Expect(7, { "script", "sco", _copyFolder, "rm001" });
            Assert::IsTrue(sco.err.find("Stopped by Ctrl+C: 1 script was not done.") != std::string::npos, Wide(sco.err).c_str());
            cli::StringConsole scoTwo = Expect(7, { "script", "sco", _copyFolder, "rm001", "974" });
            Assert::IsTrue(scoTwo.err.find("Stopped by Ctrl+C: 2 scripts were not done.") != std::string::npos, Wide(scoTwo.err).c_str());
        }

        // Ctrl+C after --stdout decompiled its script (at the memory line of
        // -v): no source, the summary counts the script as not decompiled,
        // and exit code 7.
        TEST_METHOD(Decompile_StdoutCtrlCAfterTheScript_NoSource)
        {
            struct CancelAfterTheScript : public cli::StringConsole
            {
                void Err(const std::string &text) override
                {
                    StringConsole::Err(text);
                    if (text.find("Memory after decompiling") != std::string::npos)
                    {
                        cli::CancelFlag().store(true);
                    }
                }
            };
            CopyTemplate("\\TemplateGame\\SCI0");
            CancelAfterTheScript console;
            int code = Run({ "script", "decompile", _copyFolder, "974", "--stdout", "-v" }, console);
            bool flagged = cli::CancelFlag().exchange(false);
            Assert::IsTrue(flagged, Wide("setup: the memory line sets the flag:\n" + console.err).c_str());
            Assert::AreEqual(7, code, Wide(console.err).c_str());
            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
            Assert::IsTrue(console.err.find("Decompiled 0 of 1 scripts. Stopped by Ctrl+C: 1 script was not decompiled.") != std::string::npos, Wide(console.err).c_str());
        }

        // A .sco that cannot be written is exit code 9, also in a dry run,
        // which checks the file; a dry run says "would not change" for a .sco
        // that has the bytes.
        TEST_METHOD(Sco_AReadOnlyObjectFile_ExitsWith9_AlsoInADryRun)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole first = Expect(0, { "script", "sco", _copyFolder, "rm001" });
            cli::StringConsole same = Expect(0, { "script", "sco", _copyFolder, "rm001", "--dry-run" });
            Assert::IsTrue(same.err.find("would write") == std::string::npos, Wide(same.err).c_str());
            Assert::IsTrue(same.err.find("Would write 0 .sco files (1 would not change)") != std::string::npos, Wide(same.err).c_str());

            std::string objectFile = (fs::path(_copyFolder) / "src" / "rm001.sco").string();
            WriteReadOnlyFile(objectFile, "not the new object file");
            cli::StringConsole dryRun;
            int dryRunCode = Run({ "script", "sco", _copyFolder, "rm001", "--dry-run" }, dryRun);
            cli::StringConsole run = Expect(9, { "script", "sco", _copyFolder, "rm001" });
            Assert::AreEqual(9, dryRunCode, Wide(dryRun.err).c_str());
        }

        // With --all, sco lists the scripts that it skips: a source with no
        // compiled script, and a name with no source.
        TEST_METHOD(Sco_All_ListsTheSkippedScripts)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::path src = fs::path(_copyFolder) / "src";
            {
                std::ofstream file((src / "NewRoom.sc").string(), std::ios::binary | std::ios::trunc);
                file << "(script# 200)\r\n(include sci.sh)\r\n(include game.sh)\r\n(use main)\r\n";
            }
            fs::remove(src / "door.sc");
            cli::StringConsole console = Expect(0, { "script", "sco", _copyFolder, "--all" });
            Assert::IsTrue(console.err.find("skipped 200 (NewRoom): the game has no compiled script 200") != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("skipped 974 (Door): the script has no source file") != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("; 2 scripts skipped.") != std::string::npos, Wide(console.err).c_str());
        }

        // A source with no public block, whose compiled script exports, gets a
        // warning at the start of the file.
        TEST_METHOD(Sco_NoPublicBlock_Warns)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string source = (fs::path(_copyFolder) / "src" / "rm001.sc").string();
            std::string text = ReadFileText(source);
            std::string block = "(public\r\n\trm001 0\r\n)\r\n";
            size_t at = text.find(block);
            Assert::IsTrue(at != std::string::npos, L"setup: the public block of rm001");
            text.erase(at, block.size());
            {
                std::ofstream file(source, std::ios::binary | std::ios::trunc);
                file << text;
            }
            cli::StringConsole console = Expect(0, { "script", "sco", _copyFolder, "rm001" });
            std::string expected = source + "(1,1): warning : The source has no public block, and compiled script 1 exports the slots 0.";
            Assert::IsTrue(console.err.find(expected) != std::string::npos, Wide(expected + "\n" + console.err).c_str());
        }

        // An export of a procedure of an include that is not a header fails
        // the script, as the compile fails it ("needs to be marked public").
        TEST_METHOD(Sco_AnExportFromAnInclude_FailsAsTheCompileDoes)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::path src = fs::path(_copyFolder) / "src";
            // A .shp include is in the poly folder (a polygon file: not a header).
            fs::create_directories(fs::path(_copyFolder) / "poly");
            {
                std::ofstream file((fs::path(_copyFolder) / "poly" / "c2inc.shp").string(), std::ios::binary | std::ios::trunc);
                file << "(procedure (c2IncProc)\r\n    (return 1)\r\n)\r\n";
            }
            std::string source = (src / "rm001.sc").string();
            std::string text = ReadFileText(source);
            for (const auto &edit : std::vector<std::pair<std::string, std::string>>{ { "(include game.sh)", "(include game.sh)\r\n(include c2inc.shp)" }, { "\trm001 0", "\trm001 0\r\n\tc2IncProc 1" } })
            {
                size_t at = text.find(edit.first);
                Assert::IsTrue(at != std::string::npos, L"setup: rm001.sc");
                text.replace(at, edit.first.size(), edit.second);
            }
            {
                std::ofstream file(source, std::ios::binary | std::ios::trunc);
                file << text;
            }
            std::string message = "c2IncProc needs to be marked public in order to be exported.";
            cli::StringConsole console = Expect(6, { "script", "sco", _copyFolder, "rm001" });
            Assert::IsTrue(console.err.find(": error : " + message) != std::string::npos, Wide(console.err).c_str());

            // The compile refuses it with the same message.
            SessionOptions sessionOptions;
            sessionOptions.dataFolder = GetTestModuleDirectory();
            GameSession session(sessionOptions);
            Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
            ScriptId rm001(source.c_str());
            rm001.SetResourceNumber(1);
            CompileOptions options;
            options.write.writeResources = false;
            options.write.writeObjectFile = false;
            options.write.writeDebugInfo = false;
            std::atomic<bool> abort(false);
            ICompileEvents events;
            auto compiled = CompileScripts(session, { rm001 }, options, abort, events);
            Assert::IsTrue(compiled.has_value() && !compiled->scripts[0].status.has_value(), L"the compile fails");
            bool same = false;
            for (const CompileResult &result : compiled->scripts[0].diagnostics)
            {
                same = same || (result.GetRawMessage() == message);
            }
            Assert::IsTrue(same, L"the compile gives the same message");
        }

        // The crash line of decompile and of sco names each step of the run:
        // each script (also its second decompile), the naming, main's .sco,
        // the stale check, game.ini, and the report.
        TEST_METHOD(DecompileAndSco_TheCrashItemFollowsTheSteps)
        {
            PrepareStaleFixtures();
            {
                RecordedItems recorded;
                cli::StringConsole decompile = Expect(0, { "script", "decompile", _copyFolder, "959", "960" });
                std::vector<std::string> expected = { "decompiling script 959", "decompiling script 960", "naming the variables of the decompiled scripts",
                    "decompiling script 959 again with the new global names", "writing main's .sco", "finding the stale scripts", "writing game.ini", "printing the report" };
                Assert::IsTrue(recorded.items == expected, Wide(recorded.Text() + "\n" + decompile.err).c_str());
            }
            RecordedItems recorded;
            cli::StringConsole sco = Expect(0, { "script", "sco", _copyFolder, "BatchGlobalsA", "BatchGlobalsB" });
            std::vector<std::string> expected = { "making the .sco of script 959 (BatchGlobalsA)", "making the .sco of script 960 (BatchGlobalsB)", "printing the report" };
            Assert::IsTrue(recorded.items == expected, Wide(recorded.Text() + "\n" + sco.err).c_str());
        }

        // The dumps of a debug option print plainly, not as warnings, so
        // --quiet does not hide them.
        TEST_METHOD(Decompile_DebugDumps_PrintPlainly)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            for (bool quiet : { false, true })
            {
                std::vector<std::string> args = { "script", "decompile", _copyFolder, "974", "--stdout", "--debug-control-flow", "--engine", "classic" };
                if (quiet)
                {
                    args.push_back("-q");
                }
                cli::StringConsole console = Expect(0, args);
                Assert::IsTrue(console.err.find("graph (raw):") != std::string::npos, Wide(console.err.substr(0, 2000)).c_str());
                Assert::IsTrue(console.err.find("scic: warning: ") == std::string::npos, quiet ? L"-q: a dump is not a warning" : L"a dump is not a warning");
            }
        }

        // Plan section 8: an error of the decompiler in a script that it
        // wrote is exit code 6. Here byte 7 of script 974 (in a patch file)
        // gives a branch a target that is not an instruction, so the second
        // try of the function (a tighter bound) fails too: "Invalid branch
        // target.", once (the first try fails with no message), with the
        // script in its text. The script is written.
        TEST_METHOD(Decompile_ADecompilerError_ExitsWith6)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            WriteChangedScript(974, 7);
            cli::StringConsole console = Expect(6, { "script", "decompile", _copyFolder, "974" });
            size_t errors = 0;
            for (const std::string &line : Lines(console.err))
            {
                errors += (line == "scic: error: Script 974: Invalid branch target.") ? 1 : 0;
            }
            Assert::AreEqual((size_t)1, errors, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("Decompiled and wrote 1 of 1 scripts.") != std::string::npos, Wide(console.err).c_str());
        }

        // A dry run checks each write as the run does, and gives the exit
        // code of the run: a read-only .sc, a read-only .sco that would
        // change, a read-only main's .sco or game.ini (9), and a file named
        // src (3); a hidden game.ini is no error, as the run writes it (0).
        // The dry run writes nothing.
        TEST_METHOD(Decompile_DryRun_ChecksTheWrites)
        {
            struct Case { const char *what; int exitCode; };
            std::string failures;
            for (const Case &check : { Case{ "sc", 9 }, Case{ "sco", 9 }, Case{ "main sco", 9 }, Case{ "game.ini", 9 }, Case{ "hidden game.ini", 0 }, Case{ "src", 3 } })
            {
                std::vector<std::string> args;
                std::string what = check.what;
                if (what == "main sco")
                {
                    // 960 names a global: main's .sco gets the name.
                    PrepareStaleFixtures();
                    std::string mainSco = (fs::path(_copyFolder) / "src" / "Main.sco").string();
                    Assert::IsTrue(SetFileAttributesA(mainSco.c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: Main.sco");
                    args = { "script", "decompile", _copyFolder, "960" };
                }
                else
                {
                    CopyTemplate("\\TemplateGame\\SCI0");
                    fs::path src = fs::path(_copyFolder) / "src";
                    args = { "script", "decompile", _copyFolder, "974" };
                    if (what == "sc")
                    {
                        Assert::IsTrue(SetFileAttributesA((src / "door.sc").string().c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: door.sc");
                    }
                    else if (what == "sco")
                    {
                        WriteReadOnlyFile((src / "door.sco").string(), "not the new object file");
                    }
                    else if ((what == "game.ini") || (what == "hidden game.ini"))
                    {
                        std::string ini = (fs::path(_copyFolder) / "game.ini").string();
                        Assert::IsTrue(WritePrivateProfileStringA("Script", "n974", nullptr, ini.c_str()) != 0, L"setup: game.ini needs n974");
                        Assert::IsTrue(SetFileAttributesA(ini.c_str(), (what == "game.ini") ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_HIDDEN) != 0, L"setup: game.ini");
                    }
                    else
                    {
                        fs::remove_all(src);
                        WriteFileText(src.string(), "a file named src");
                    }
                }
                auto before = Snapshot(_copyFolder);
                std::vector<std::string> dryArgs = args;
                dryArgs.push_back("--dry-run");
                cli::StringConsole dryRun;
                int dryRunCode = Run(dryArgs, dryRun);
                Assert::IsTrue(before == Snapshot(_copyFolder), Wide(what + ": the dry run writes nothing: " + dryRun.err).c_str());
                cli::StringConsole run;
                int runCode = Run(args, run);
                std::string facts = what + "\ndry run:\n" + dryRun.err + "\nrun:\n" + run.err;
                Assert::AreEqual(check.exitCode, runCode, Wide("setup: the exit code of the run: " + facts).c_str());
                if (dryRunCode != check.exitCode)
                {
                    // One short line for each case: the assert text has a
                    // limit.
                    failures += fmt::format("{0}: the dry run gives {1}; its last line: {2}\n", what, dryRunCode, Lines(dryRun.err).empty() ? std::string() : Lines(dryRun.err).back());
                }
            }
            Assert::AreEqual(std::string(), failures, Wide(failures).c_str());
        }

        // A "wrote" or "would write" line names a .sco only when its bytes
        // change: the write does not change a .sco that has them already.
        // Here the second decompile of 974 gives the .sco of the first.
        TEST_METHOD(Decompile_TheFileLines_OnlyAChangedObjectFile)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            Expect(0, { "script", "decompile", _copyFolder, "974" });
            std::string objectFile = (fs::path(_copyFolder) / "src" / "door.sco").string();
            auto written = fs::last_write_time(objectFile);
            cli::StringConsole dryRun = Expect(0, { "script", "decompile", _copyFolder, "974", "--dry-run" });
            cli::StringConsole run = Expect(0, { "script", "decompile", _copyFolder, "974", "-v" });
            std::string facts = "dry run:\n" + dryRun.err + "\nrun:\n" + run.err;
            Assert::IsTrue(written == fs::last_write_time(objectFile), Wide("setup: the run does not write the .sco: " + facts).c_str());
            std::set<std::string> wouldWrite = ListedFiles(dryRun.err, "would write ");
            std::set<std::string> wrote = ListedFiles(run.err, "wrote ");
            Assert::IsTrue(HasFileNamed(wrote, "door.sc") && HasFileNamed(wouldWrite, "door.sc"), Wide(facts).c_str());
            Assert::IsFalse(HasFileNamed(wrote, "door.sco") || HasFileNamed(wouldWrite, "door.sco"), Wide(facts).c_str());
        }

        // compile --all on a template copy with no game.ini compiles every
        // src\*.sc into patch files, and the package does not change.
        TEST_METHOD(Compile_All_NoGameIni_PatchFiles)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::remove(fs::path(_copyFolder) / "game.ini");
            std::string map = ReadFileText((fs::path(_copyFolder) / "resource.map").string());
            size_t sources = SourcesOf(_copyFolder).size();
            cli::StringConsole console = Expect(0, { "script", "compile", _copyFolder, "--all" });
            Assert::IsTrue(console.err.find(fmt::format("Compiled and wrote {0} of {0} scripts as patch files", sources)) != std::string::npos, Wide(console.err).c_str());
            size_t patches = 0;
            for (const auto &entry : fs::directory_iterator(_copyFolder))
            {
                patches += (entry.path().filename().string().rfind("script.", 0) == 0) ? 1 : 0;
            }
            Assert::AreEqual(sources, patches, L"a patch file for each script");
            Assert::AreEqual(map, ReadFileText((fs::path(_copyFolder) / "resource.map").string()), L"the package does not change");
        }

        // --to package writes the package. The script bytes are the same for
        // both destinations, and the same as the GUI's path (CompileScripts
        // with the game's setting).
        TEST_METHOD(Compile_ToPackage_TheSameBytesAsPatchesAndTheGui)
        {
            SessionOptions sessionOptions;
            sessionOptions.dataFolder = GetTestModuleDirectory();
            std::vector<std::vector<uint8_t>> scripts;
            for (int way = 0; way < 3; way++)
            {
                CopyTemplate("\\TemplateGame\\SCI0");
                std::string map = ReadFileText((fs::path(_copyFolder) / "resource.map").string());
                if (way < 2)
                {
                    cli::StringConsole console;
                    std::vector<std::string> args = { "script", "compile", _copyFolder, "rm001" };
                    if (way == 1)
                    {
                        args.push_back("--to");
                        args.push_back("package");
                    }
                    int code = Run(args, console);
                    Assert::AreEqual(0, code, Wide(console.err).c_str());
                    Assert::AreEqual(way == 0, fs::exists(fs::path(_copyFolder) / "script.001"), Wide(console.err).c_str());
                    Assert::AreEqual(way == 0, map == ReadFileText((fs::path(_copyFolder) / "resource.map").string()), L"only --to package changes the package");
                }
                else
                {
                    GameSession session(sessionOptions);
                    Assert::IsTrue(session.Open(_copyFolder).has_value());
                    ScriptId rm001((fs::path(_copyFolder) / "src" / "rm001.sc").string().c_str());
                    rm001.SetResourceNumber(1);
                    std::atomic<bool> abort(false);
                    ICompileEvents events;
                    auto compiled = CompileScripts(session, { rm001 }, CompileOptions(), abort, events);
                    Assert::IsTrue(compiled.has_value() && compiled->Succeeded(), L"the GUI's path");
                }
                GameSession session(sessionOptions);
                Assert::IsTrue(session.Open(_copyFolder).has_value());
                std::unique_ptr<ResourceBlob> blob = session.Helper().MostRecentResource(ResourceType::Script, 1, ResourceEnumFlags::None);
                Assert::IsNotNull(blob.get());
                scripts.emplace_back(blob->GetData(), blob->GetData() + blob->GetLength());
            }
            Assert::IsTrue((scripts[0] == scripts[1]) && (scripts[1] == scripts[2]), L"the same script bytes");
        }

        // Plan section 5: a patch file that would hide the package write
        // refuses the compile (exit 8), and --replace-patches moves it aside;
        // a game that keeps its resources in patch files refuses the package.
        TEST_METHOD(Compile_ThePatchFileRules)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole patch = Expect(0, { "script", "compile", _copyFolder, "rm001" });
            Assert::IsTrue(fs::exists(fs::path(_copyFolder) / "script.001"), L"setup: the patch file");
            cli::StringConsole refused = Expect(8, { "script", "compile", _copyFolder, "rm001", "--to", "package" });
            Assert::IsTrue(refused.err.find("script.001") != std::string::npos, Wide(refused.err).c_str());
            cli::StringConsole replaced = Expect(0, { "script", "compile", _copyFolder, "rm001", "--into-volume", "--replace-patches" });
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "script.001"), Wide(replaced.err).c_str());
            Assert::IsTrue(fs::exists(fs::path(_copyFolder) / "replaced-patches"), L"the patch file moved aside");
            Assert::IsTrue(replaced.err.find("moved the patch file") != std::string::npos, Wide(replaced.err).c_str());

            Assert::IsTrue(WritePrivateProfileStringA("Game", "SaveToPatchFiles", "true", (fs::path(_copyFolder) / "game.ini").string().c_str()) != 0);
            cli::StringConsole patchMode = Expect(8, { "script", "compile", _copyFolder, "rm001", "--to", "package" });
            Assert::IsTrue(patchMode.err.find("SaveToPatchFiles") != std::string::npos, Wide(patchMode.err).c_str());
        }

        // Plan section 4.5: --dry-run writes nothing (no resource, table, .sco
        // or .scd), and lists what a run would write.
        TEST_METHOD(Compile_DryRun_WritesNothing)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            auto before = Snapshot(_copyFolder);
            cli::StringConsole console = Expect(0, { "script", "compile", _copyFolder, "--all", "--dry-run" });
            Assert::IsTrue(before == Snapshot(_copyFolder), Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("would write " + (fs::path(_copyFolder) / "script.001").string()) != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("a run would write them as patch files") != std::string::npos, Wide(console.err).c_str());
        }

        // Plan section 8: a script with an error is exit code 5, and the
        // others are written. The error is in the MSBuild format, with the
        // path as it was given and a 1-based column.
        TEST_METHOD(Compile_OneBrokenScript_ExitsWith5)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string source = (fs::path(_copyFolder) / "src" / "rm001.sc").string();
            std::string text = ReadFileText(source);
            std::string block = "(public\r\n\trm001 0\r\n)\r\n";
            size_t at = text.find(block);
            Assert::IsTrue(at != std::string::npos, L"setup: the public block of rm001");
            // Line 25: (procedure (c3Broken) (return c3Undeclared))
            text.insert(at + block.size(), "(procedure (c3Broken) (return c3Undeclared))\r\n");
            {
                std::ofstream file(source, std::ios::binary | std::ios::trunc);
                file << text;
            }
            cli::StringConsole console = Expect(5, { "script", "compile", _copyFolder, "--all" });
            Assert::IsTrue(console.err.find(source + "(25,") != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("): error : ") != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("Failed: 1 (rm001).") != std::string::npos, Wide(console.err).c_str());
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "script.001"), L"the broken script is not written");
            Assert::IsTrue(fs::exists(fs::path(_copyFolder) / "script.974"), L"the others are written");
        }

        // A round trip on the SCI1.1 template: decompile --all, then compile
        // --all, with 0 errors.
        TEST_METHOD(Compile_RoundTrip_Sci11)
        {
            CopyTemplate("\\TemplateGame\\SCI1.1");
            cli::StringConsole decompile = Expect(0, { "script", "decompile", _copyFolder, "--all" });
            // One pass is not enough after a decompile of every script (plan
            // section 4.5): --passes 1 says so.
            cli::StringConsole onePass = Expect(0, { "script", "compile", _copyFolder, "--all", "--passes", "1" });
            Assert::IsTrue(onePass.err.find("pass 1, the last, still changed a .sco file") != std::string::npos, Wide(onePass.err).c_str());
            cli::StringConsole compile = Expect(0, { "script", "compile", _copyFolder, "--all" });
            Assert::IsTrue(compile.err.find("(0 errors,") != std::string::npos, Wide(compile.err).c_str());
            Assert::IsTrue(compile.err.find("still changed a .sco file") == std::string::npos, Wide(compile.err).c_str());
        }

        // A round trip on the SCI0 template: decompile --all, then compile
        // --all, with 0 errors. Its .sco files give names that the decompile
        // must place by export slot (Obj.sc) and by local index (SysWindow.sc).
        TEST_METHOD(Compile_RoundTrip_Sci0)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole decompile;
            int decompiled = Run({ "script", "decompile", _copyFolder, "--all" }, decompile);
            Assert::AreEqual(0, decompiled, Wide(decompile.err).c_str());
            cli::StringConsole compile;
            int compiled = Run({ "script", "compile", _copyFolder, "--all" }, compile);
            Assert::AreEqual(0, compiled, Wide(compile.err).c_str());
            Assert::IsTrue(compile.err.find("(0 errors,") != std::string::npos, Wide(compile.err).c_str());
        }

        // A decompile names each exported procedure by the slot of its
        // export. A name from the .sco can have the form of a generated name
        // for another slot: the SCI0 template's Obj.sco names slot 1
        // "proc999_2" and slot 2 "proc999_3".
        TEST_METHOD(Decompile_Sci0Obj_NamesEachExportBySlot)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole console = Expect(0, { "script", "decompile", _copyFolder, "999", "--stdout" });
            Assert::IsTrue(console.out.find("\tproc999_2 1") != std::string::npos, Wide(console.out).c_str());
            Assert::IsTrue(console.out.find("(procedure (proc999_2 param1") != std::string::npos, Wide(console.out).c_str());
            Assert::IsTrue(console.out.find("(procedure (proc999_3 param1") != std::string::npos, Wide(console.out).c_str());
            Assert::AreEqual((size_t)1, CountOf(console.out, "(procedure (EqualsAny"), Wide(console.out).c_str());
        }

        // A decompile gives each local name of the old .sco to the local at
        // the same index. The SCI0 template's SysWindow.sco has two arrays of
        // four locals (local5 at index 5, localA at index 10), which the
        // decompiled script declares one index at a time: local9 names index
        // 9 only, and index 6 keeps its decompiled name.
        TEST_METHOD(Decompile_Sci0SysWindow_LocalNamesByIndex)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            cli::StringConsole console = Expect(0, { "script", "decompile", _copyFolder, "990", "--stdout" });
            std::vector<std::string> lines = Lines(console.out);
            size_t local9 = std::count(lines.begin(), lines.end(), std::string("\tlocal9"));
            Assert::AreEqual((size_t)1, local9, Wide(console.out).c_str());
            Assert::IsTrue(std::find(lines.begin(), lines.end(), std::string("\tlocal6")) != lines.end(), Wide(console.out).c_str());
            Assert::IsTrue(std::find(lines.begin(), lines.end(), std::string("\tlocalA")) != lines.end(), Wide(console.out).c_str());
        }

        // A .sco that loads only in part (Obj.sco cut after its exports)
        // still names each exported procedure by its slot, once: the name of
        // one slot does not rename the procedure of another. The decompiled
        // script compiles.
        TEST_METHOD(Decompile_Sci0Obj_ACutObjectFile_NamesEachExportOnce)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string objectFile = (fs::path(_copyFolder) / "src" / "Obj.sco").string();
            std::string bytes = ReadFileText(objectFile);
            Assert::IsTrue(bytes.size() > 90, L"setup: Obj.sco");
            WriteFileText(objectFile, bytes.substr(0, 90));
            cli::StringConsole source = Expect(0, { "script", "decompile", _copyFolder, "999", "--stdout" });
            Assert::AreEqual((size_t)1, CountOf(source.out, "(procedure (EqualsAny"), Wide(source.out).c_str());
            Assert::IsTrue(source.out.find("\tproc999_2 1") != std::string::npos, Wide(source.out).c_str());
            Assert::IsTrue(source.out.find("(procedure (proc999_2 param1") != std::string::npos, Wide(source.out).c_str());
            Assert::IsTrue(source.out.find("(procedure (proc999_3 param1") != std::string::npos, Wide(source.out).c_str());
            cli::StringConsole decompile = Expect(0, { "script", "decompile", _copyFolder, "999" });
            cli::StringConsole compile = Expect(0, { "script", "compile", _copyFolder, "999", "--dry-run" });
        }

        // A .sco can name two locals alike (here local9 at indices 6 and 9 of
        // SysWindow.sco). The name goes to one local only, and the decompiled
        // script compiles.
        TEST_METHOD(Decompile_Sci0SysWindow_TwoLikeNamesInTheObjectFile)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            {
                SessionOptions sessionOptions;
                sessionOptions.dataFolder = GetTestModuleDirectory();
                GameSession session(sessionOptions);
                Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
                std::unique_ptr<CSCOFile> sysWindow = GetExistingSCOFromScriptNumber(session.Helper(), 990, lookups.GetSelectorTable());
                Assert::IsNotNull(sysWindow.get(), L"setup: SysWindow.sco");
                Assert::IsTrue((sysWindow->GetVariables().size() > 9) && (sysWindow->GetVariables()[9].GetName() == "local9"), L"setup: SysWindow.sco names index 9 local9");
                sysWindow->GetVariables()[6].SetName("local9");
                Assert::IsTrue(SaveSCOFile(session.Helper(), *sysWindow).has_value());
            }
            cli::StringConsole source = Expect(0, { "script", "decompile", _copyFolder, "990", "--stdout" });
            std::vector<std::string> lines = Lines(source.out);
            Assert::AreEqual((size_t)1, (size_t)std::count(lines.begin(), lines.end(), std::string("\tlocal9")), Wide(source.out).c_str());
            cli::StringConsole decompile = Expect(0, { "script", "decompile", _copyFolder, "990" });
            cli::StringConsole compile = Expect(0, { "script", "compile", _copyFolder, "990", "--dry-run" });
        }

        // A .sco can give two indices a name that no declaration has (here
        // c3Twin at indices 6 and 9 of SysWindow.sco). The earlier index
        // takes it, and the decompiled script compiles.
        TEST_METHOD(Decompile_Sci0SysWindow_ANameOfTwoIndices)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            {
                SessionOptions sessionOptions;
                sessionOptions.dataFolder = GetTestModuleDirectory();
                GameSession session(sessionOptions);
                Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
                std::unique_ptr<CSCOFile> sysWindow = GetExistingSCOFromScriptNumber(session.Helper(), 990, lookups.GetSelectorTable());
                Assert::IsNotNull(sysWindow.get(), L"setup: SysWindow.sco");
                Assert::IsTrue(sysWindow->GetVariables().size() > 9, L"setup: SysWindow.sco has index 9");
                sysWindow->GetVariables()[6].SetName("c3Twin");
                sysWindow->GetVariables()[9].SetName("c3Twin");
                Assert::IsTrue(SaveSCOFile(session.Helper(), *sysWindow).has_value());
            }
            cli::StringConsole source = Expect(0, { "script", "decompile", _copyFolder, "990", "--stdout" });
            std::vector<std::string> lines = Lines(source.out);
            Assert::AreEqual((size_t)1, (size_t)std::count(lines.begin(), lines.end(), std::string("\tc3Twin")), Wide(source.out).c_str());
            Assert::AreEqual((size_t)1, (size_t)std::count(lines.begin(), lines.end(), std::string("\tlocal9")), Wide(source.out).c_str());
            cli::StringConsole decompile = Expect(0, { "script", "decompile", _copyFolder, "990" });
            cli::StringConsole compile = Expect(0, { "script", "compile", _copyFolder, "990", "--dry-run" });
        }

        // Plan section 4.5: --out-dir and --raw write the plain data into the
        // folder, and the game does not change.
        TEST_METHOD(Compile_OutDirRaw)
        {
            CopyTemplate("\\TemplateGame\\SCI1.1");
            fs::path out = fs::path(_copyFolder) / "c3out";
            fs::create_directories(out);
            std::string map = ReadFileText((fs::path(_copyFolder) / "resource.map").string());
            cli::StringConsole raw = Expect(0, { "script", "compile", _copyFolder, "Main", "--out-dir", out.string(), "--raw" });
            Assert::IsTrue(fs::exists(out / "script.0.bin") && fs::exists(out / "heap.0.bin"), Wide(raw.err).c_str());
            Assert::AreEqual(map, ReadFileText((fs::path(_copyFolder) / "resource.map").string()), L"the game does not change");
        }

        // Plan section 4.5: --no-warn-unused turns off the "unused instance"
        // warning (on by default, as in the GUI).
        TEST_METHOD(Compile_NoWarnUnused)
        {
            CopyTemplate("\\TemplateGame\\SCI1.1");
            cli::StringConsole warnings = Expect(0, { "script", "compile", _copyFolder, "Main", "--dry-run" });
            Assert::IsTrue(warnings.err.find("is not used anywhere") != std::string::npos, Wide(warnings.err).c_str());
            cli::StringConsole noWarnings = Expect(0, { "script", "compile", _copyFolder, "Main", "--dry-run", "--no-warn-unused" });
            Assert::IsTrue(noWarnings.err.find("is not used anywhere") == std::string::npos, Wide(noWarnings.err).c_str());
        }

        // Plan section 4.5: with two broken scripts, --fail-fast stops after
        // the first.
        TEST_METHOD(Compile_FailFast)
        {
            CopyTemplate("\\TemplateGame\\SCI1.1");
            for (const char *name : { "rm110.sc", "TitleScreen.sc" })
            {
                std::string source = (fs::path(_copyFolder) / "src" / name).string();
                std::ofstream file(source, std::ios::binary | std::ios::app);
                file << "\r\n(procedure (c3Broken) (return c3Undeclared))\r\n";
            }
            cli::StringConsole failFast = Expect(5, { "script", "compile", _copyFolder, "rm110", "TitleScreen", "--fail-fast", "--dry-run" });
            Assert::IsTrue(failFast.err.find("--fail-fast stopped the run: 1 script was not compiled.") != std::string::npos, Wide(failFast.err).c_str());
        }

        // The usage errors of compile (exit code 2): no scripts and no --all,
        // --replace-patches with patch files, --passes with named scripts,
        // --raw with no --out-dir, a header file, an empty --out-dir, and --to
        // patch with --into-volume. None writes a file.
        TEST_METHOD(Compile_UsageErrors)
        {
            CopyTemplate("\\TemplateGame\\SCI1.1");
            auto before = Snapshot(_copyFolder);
            for (const auto &args : std::vector<std::vector<std::string>>{
                { "script", "compile", _copyFolder },
                { "script", "compile", _copyFolder, "Main", "--replace-patches" },
                { "script", "compile", _copyFolder, "Main", "--passes", "3" },
                { "script", "compile", _copyFolder, "Main", "--raw" },
                { "script", "compile", _copyFolder, (fs::path(_copyFolder) / "src" / "game.sh").string() },
                { "script", "compile", _copyFolder, "Main", "--out-dir", "" },
                { "script", "compile", _copyFolder, "Main", "--to", "patch", "--into-volume" } })
            {
                cli::StringConsole usage;
                int code = Run(args, usage);
                Assert::AreEqual(2, code, Wide(args.back() + ": " + usage.err).c_str());
            }
            Assert::IsTrue(before == Snapshot(_copyFolder), L"a usage error writes nothing");
        }

        // Ctrl+C stops the compile (exit code 7), and it writes nothing.
        TEST_METHOD(Compile_CtrlC_ExitsWith7)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            struct CtrlC
            {
                CtrlC() { cli::CancelFlag().store(true); }
                ~CtrlC() { cli::CancelFlag().store(false); }
            } ctrlC;
            cli::StringConsole console = Expect(7, { "script", "compile", _copyFolder, "--all" });
            Assert::IsTrue(console.err.find("Stopped by Ctrl+C") != std::string::npos, Wide(console.err).c_str());
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "script.001"), L"nothing is written");
        }

        // An error of an include (a header that does not parse, or a file
        // that is not there) fails the script, as an error of the script
        // does, and the text names the include.
        TEST_METHOD(Compile_AnIncludeWithErrors_ExitsWith5)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string header = (fs::path(_copyFolder) / "src" / "game.sh").string();
            std::string headerText = ReadFileText(header);
            ReplaceFirst(header, "; Base Scripts", "(define C3BROKEN");
            cli::StringConsole broken = Expect(5, { "script", "compile", _copyFolder, "rm001" });
            Assert::IsTrue(broken.err.find(header + "(") != std::string::npos, Wide(broken.err).c_str());
            Assert::IsTrue(broken.err.find("Failed: 1 (rm001).") != std::string::npos, Wide(broken.err).c_str());
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "script.001"), Wide("the script is not written: " + broken.err).c_str());

            // One error line for the include: sci.sh includes keys.sh, so the
            // includes are read again, and a failed one is not tried again.
            Assert::AreEqual((size_t)1, CountOf(broken.err, "Parsing errors while loading"), Wide(broken.err).c_str());

            WriteFileText(header, headerText);
            // A read error names the include, and its path keeps its case.
            HANDLE held = CreateFileA(header.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(held != INVALID_HANDLE_VALUE, L"setup: game.sh is held");
            cli::StringConsole unreadable;
            int unreadableCode = Run({ "script", "compile", _copyFolder, "rm001", "--dry-run" }, unreadable);
            CloseHandle(held);
            Assert::AreEqual(5, unreadableCode, Wide(unreadable.err).c_str());
            Assert::IsTrue(unreadable.err.find("Unable to load the include file game.sh: Opening " + header + ":") != std::string::npos, Wide(header + " | " + unreadable.err).c_str());
            Assert::AreEqual((size_t)1, CountOf(unreadable.err, "Unable to load the include file"), Wide(unreadable.err).c_str());

            ReplaceFirst((fs::path(_copyFolder) / "src" / "rm001.sc").string(), "(include game.sh)", "(include game.sh)\r\n(include c3missing.sh)");
            cli::StringConsole missing = Expect(5, { "script", "compile", _copyFolder, "rm001" });
            Assert::IsTrue(missing.err.find("The include file c3missing.sh is not in the include folder or in src.") != std::string::npos, Wide(missing.err).c_str());
            Assert::AreEqual((size_t)1, CountOf(missing.err, "The include file c3missing.sh"), Wide(missing.err).c_str());
            Assert::IsTrue(missing.err.find("Compiled 0 of 1 scripts, and wrote none (") != std::string::npos, Wide(missing.err).c_str());
            Assert::IsFalse(fs::exists(fs::path(_copyFolder) / "script.001"), Wide("the script is not written: " + missing.err).c_str());
            cli::StringConsole dryRun = Expect(5, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            Assert::IsTrue(dryRun.err.find("Compiled 0 of 1 scripts, and a run would write none (") != std::string::npos, Wide(dryRun.err).c_str());
        }

        // A message of the parser with a line prints as an "info" line in the
        // MSBuild format, also without -v (the GUI always shows it), and
        // --quiet hides it.
        TEST_METHOD(Compile_AParserMessage_PrintsUnlessQuiet)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string source = (fs::path(_copyFolder) / "src" / "rm001.sc").string();
            ReplaceFirst(source, "(public\r\n\trm001 0\r\n)\r\n",
                "(public\r\n\trm001 0\r\n)\r\n(procedure (c3Each &tmp [arr 3])\r\n\t(foreach x arr\r\n\t\t(= [x 0] 1)\r\n\t)\r\n)\r\n");
            cli::StringConsole console;
            Run({ "script", "compile", _copyFolder, "rm001", "--dry-run" }, console);
            Assert::IsTrue(console.err.find(source + "(27,1): info : An iteration variable can not be indexed.") != std::string::npos, Wide(console.err).c_str());
            cli::StringConsole quiet;
            Run({ "script", "compile", _copyFolder, "rm001", "--dry-run", "-q" }, quiet);
            Assert::IsTrue(quiet.err.find("iteration variable") == std::string::npos, Wide(quiet.err).c_str());
        }

        // A compile of one pass prints the diagnostics of each script when
        // the script is done, not at the end of the run.
        TEST_METHOD(Compile_OnePass_DiagnosticsAsTheyCome)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            // Door (974) compiles first.
            std::string source = (fs::path(_copyFolder) / "src" / "door.sc").string();
            WriteFileText(source, ReadFileText(source) + "\r\n(procedure (c3Undeclared)\r\n\t(return c3NoSuchName)\r\n)\r\n");
            cli::StringConsole console;
            Run({ "script", "compile", _copyFolder, "rm001", "door", "--dry-run", "-v" }, console);
            size_t error = console.err.find("c3NoSuchName");
            size_t first = console.err.find("] Compiling ");
            size_t second = (first == std::string::npos) ? std::string::npos : console.err.find("] Compiling ", first + 1);
            Assert::IsTrue((error != std::string::npos) && (second != std::string::npos) && (first < error) && (error < second), Wide(console.err).c_str());
        }

        // A script that uses a script in a name conflict gets a warning: the
        // .sco of the name can be the file of the other script.
        TEST_METHOD(Compile_AUseOfANameInAConflict_IsAWarning)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            // With no game.ini, the two sources of script 974 give it two
            // names.
            fs::remove(fs::path(_copyFolder) / "game.ini");
            fs::copy_file(fs::path(_copyFolder) / "src" / "door.sc", fs::path(_copyFolder) / "src" / "DoorCopy.sc");
            cli::StringConsole console = Expect(0, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            Assert::IsTrue(console.err.find("warning : door is in a name conflict") != std::string::npos, Wide(console.err).c_str());
        }

        // An else clause that is not the last clause of a cond is a warning,
        // with its line and column: the parser drops it and the clauses
        // before it. The summary counts it; --quiet hides it.
        TEST_METHOD(Compile_AnElseClauseNotLast_IsAWarning)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string source = (fs::path(_copyFolder) / "src" / "rm001.sc").string();
            ReplaceFirst(source, "(public\r\n\trm001 0\r\n)\r\n",
                "(public\r\n\trm001 0\r\n)\r\n(procedure (c3Cond x)\r\n\t(cond\r\n\t\t((== x 1) (return 1))\r\n\t\t(else (return 2))\r\n\t\t((== x 3) (return 3))\r\n\t)\r\n)\r\n");
            cli::StringConsole console = Expect(0, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            std::string line = source + "(28,4): warning : The else clause must be the last clause in a cond; it and the clauses before it are not compiled.";
            Assert::IsTrue(console.err.find(line) != std::string::npos, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("(0 errors, 1 warning)") != std::string::npos, Wide(console.err).c_str());
            cli::StringConsole quiet = Expect(0, { "script", "compile", _copyFolder, "rm001", "--dry-run", "-q" });
            Assert::IsTrue(quiet.err.find("The else clause") == std::string::npos, Wide(quiet.err).c_str());
        }

        // A syntax error prints the path as it was given, with the case of
        // its folder.
        TEST_METHOD(Compile_ASyntaxError_KeepsTheCaseOfItsPath)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            std::string source = (fs::path(_copyFolder) / "src" / "rm001.sc").string();
            // Line 25: (procedure (c3Broken) (= )
            ReplaceFirst(source, "(public\r\n\trm001 0\r\n)\r\n", "(public\r\n\trm001 0\r\n)\r\n(procedure (c3Broken) (= )\r\n");
            cli::StringConsole console = Expect(5, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            Assert::IsTrue(console.err.find(source + "(25,") != std::string::npos, Wide(source + " | " + console.err).c_str());
        }

        // A script whose .sco cannot be written prints its error once (exit
        // code 9), also in a dry run.
        TEST_METHOD(Compile_AWriteError_PrintsOnce)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            ReplaceFirst((fs::path(_copyFolder) / "src" / "rm001.sc").string(), "(public\r\n\trm001 0\r\n)\r\n", "(public\r\n\trm001 0\r\n)\r\n(local c3NewLocal)\r\n");
            std::string sco = (fs::path(_copyFolder) / "src" / "rm001.sco").string();
            Assert::IsTrue(SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: a read-only .sco");
            cli::StringConsole console = Expect(9, { "script", "compile", _copyFolder, "rm001" });
            Assert::AreEqual((size_t)1, CountOf(console.err, "Access is denied"), Wide(console.err).c_str());
            // A dry run checks the .sco as the write would.
            cli::StringConsole dryRun = Expect(9, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            Assert::AreEqual((size_t)1, CountOf(dryRun.err, "Access is denied"), Wide(dryRun.err).c_str());
        }

        // A dry run of a script whose .sco would change writes no .sco.
        // (Compile_DryRun_WritesNothing uses the SCI0 template, whose .sco
        // files do not change, so it cannot see this.)
        TEST_METHOD(Compile_DryRun_AChangedObjectFileIsNotWritten)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            ReplaceFirst((fs::path(_copyFolder) / "src" / "rm001.sc").string(), "(public\r\n\trm001 0\r\n)\r\n", "(public\r\n\trm001 0\r\n)\r\n(local c3NewLocal)\r\n");
            auto before = Snapshot(_copyFolder);
            cli::StringConsole console = Expect(0, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            Assert::IsTrue(before == Snapshot(_copyFolder), Wide(console.err).c_str());
            // The same compile, not dry, changes the .sco: the setup works.
            cli::StringConsole run = Expect(0, { "script", "compile", _copyFolder, "rm001" });
            Assert::IsTrue(before.at((fs::path(_copyFolder) / "src" / "rm001.sco").string()) != Snapshot(_copyFolder).at((fs::path(_copyFolder) / "src" / "rm001.sco").string()),
                L"setup: a run changes the .sco");
        }

        // A console gets the characters of the ANSI code page, not its bytes:
        // in code page 1252, the byte E9 is e with an acute accent.
        TEST_METHOD(AnsiToWide_GivesTheCharacters)
        {
            if (GetACP() != 1252)
            {
                Logger::WriteMessage(L"skipped: the ANSI code page is not 1252");
                return;
            }
            Assert::IsTrue(std::wstring(L"caf\x00E9") == cli::AnsiToWide("caf\xE9"));
            Assert::IsTrue(cli::AnsiToWide("").empty());
        }

        // AbsolutePath removes a separator at the end, but not the one of a
        // root: a drive, a device path, a volume or a UNC share.
        TEST_METHOD(AbsolutePath_KeepsTheSeparatorOfARoot)
        {
            Assert::AreEqual(std::string("C:\\"), cli::AbsolutePath("C:\\"));
            Assert::AreEqual(std::string("\\\\?\\C:\\"), cli::AbsolutePath("\\\\?\\C:\\"));
            Assert::AreEqual(std::string("\\\\.\\C:\\"), cli::AbsolutePath("\\\\.\\C:\\"));
            Assert::AreEqual(std::string("\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\"), cli::AbsolutePath("\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\"));
            Assert::AreEqual(std::string("C:\\game"), cli::AbsolutePath("C:\\game\\\\"));
            Assert::AreEqual(std::string("C:\\game{1}"), cli::AbsolutePath("C:\\game{1}\\"));
            Assert::AreEqual(std::string("\\\\?\\C:\\game"), cli::AbsolutePath("\\\\?\\C:\\game\\"));
            Assert::AreEqual(std::string("\\\\server\\share\\"), cli::AbsolutePath("\\\\server\\share\\"));
            Assert::AreEqual(std::string("\\\\?\\UNC\\server\\share\\"), cli::AbsolutePath("\\\\?\\UNC\\server\\share\\"));
            Assert::AreEqual(std::string("\\\\server\\share\\game"), cli::AbsolutePath("\\\\server\\share\\game\\"));
            Assert::AreEqual(std::string("\\\\?\\UNC\\server\\share\\game"), cli::AbsolutePath("\\\\?\\UNC\\server\\share\\game\\"));
            Assert::AreEqual(std::string("\\\\.\\UNC\\server\\share\\"), cli::AbsolutePath("\\\\.\\UNC\\server\\share\\"));
            Assert::AreEqual(std::string("\\\\.\\UNC\\server\\share\\game"), cli::AbsolutePath("\\\\.\\UNC\\server\\share\\game\\"));
            Assert::AreEqual(std::string("\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\"), cli::AbsolutePath("\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\"));
            Assert::AreEqual(std::string("\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\game"), cli::AbsolutePath("\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\game\\"));
        }

        // A relative game folder gives absolute paths in the MSBuild lines
        // (the VS Code problem matcher needs them), and so does a relative
        // data folder. A game folder with a "\" at the end gives no "\\".
        TEST_METHOD(Compile_ARelativeGameFolder_PrintsAbsolutePaths)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            ReplaceFirst((fs::path(_copyFolder) / "src" / "rm001.sc").string(), "(public\r\n\trm001 0\r\n)\r\n", "(public\r\n\trm001 0\r\n)\r\n(procedure (c3Broken) (return c3Undeclared))\r\n");
            std::string expected = (fs::path(_copyFolder) / "src" / "rm001.sc").string() + "(25,";
            fs::path saved = fs::current_path();
            fs::current_path(_copyFolder);
            cli::StringConsole console;
            int code = Run({ "script", "compile", ".", "rm001", "--dry-run" }, console);
            fs::current_path(saved);
            Assert::AreEqual(5, code, Wide(console.err).c_str());
            Assert::IsTrue(console.err.find(expected) != std::string::npos, Wide(console.err).c_str());

            cli::StringConsole endSeparator = Expect(5, { "script", "compile", _copyFolder + "\\", "rm001", "--dry-run" });
            Assert::IsTrue(endSeparator.err.find(expected) != std::string::npos, Wide(endSeparator.err).c_str());

            fs::path dataFolder = GetTestModuleDirectory();
            fs::current_path(dataFolder.parent_path());
            cli::StringConsole relativeData;
            code = cli::RunCli({ "script", "list", _copyFolder, "0", "-v", "--data-dir", dataFolder.filename().string() }, relativeData);
            fs::current_path(saved);
            Assert::AreEqual(0, code, Wide(relativeData.err).c_str());
            Assert::IsTrue(relativeData.err.find("The data folder: " + dataFolder.string() + "\n") != std::string::npos, Wide(relativeData.err).c_str());
        }

        // The crash line names the step of a compile: the selection, the
        // start, each script, the writes and the report.
        TEST_METHOD(Compile_TheCrashItemFollowsTheSteps)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            RecordedItems recorded;
            cli::StringConsole console = Expect(0, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            std::vector<std::string> expected = { "selecting the scripts", "starting the compile", "compiling script 1 (rm001)", "writing the compiled resources", "printing the report" };
            Assert::IsTrue(recorded.items == expected, Wide(recorded.Text()).c_str());
        }

        // The lines of the report. With -v, "wrote" names each file that the
        // run wrote (the script and the heap of an SCI1.1 script). A compile
        // of named scripts whose .sco changed warns, and the summary counts
        // each warning that printed. A dry run gives the warning for a patch
        // file with another name. A commit that failed lists no file.
        TEST_METHOD(Compile_TheReportLines)
        {
            CopyTemplate("\\TemplateGame\\SCI1.1");
            auto before = Snapshot(_copyFolder);
            cli::StringConsole verbose = Expect(0, { "script", "compile", _copyFolder, "Main", "-v" });
            size_t newFiles = 0;
            for (const auto &file : Snapshot(_copyFolder))
            {
                if ((fs::path(file.first).parent_path() == fs::path(_copyFolder)) && (before.find(file.first) == before.end()))
                {
                    newFiles++;
                    Assert::IsTrue(verbose.err.find(file.first) != std::string::npos, Wide("not listed: " + file.first + " | " + verbose.err).c_str());
                }
            }
            Assert::IsTrue(newFiles >= 2, Wide("setup: Main writes its script and its heap | " + verbose.err).c_str());

            CopyTemplate("\\TemplateGame\\SCI0");
            ReplaceFirst((fs::path(_copyFolder) / "src" / "rm001.sc").string(), "(public\r\n\trm001 0\r\n)\r\n", "(public\r\n\trm001 0\r\n)\r\n(local c3NewLocal)\r\n");
            cli::StringConsole changed = Expect(0, { "script", "compile", _copyFolder, "rm001" });
            Assert::IsTrue(changed.err.find("a .sco file changed, so the scripts that use it can be out of date") != std::string::npos, Wide(changed.err).c_str());
            size_t warnings = CountOf(changed.err, ": warning : ") + CountOf(changed.err, "scic: warning: ");
            Assert::IsTrue(changed.err.find(fmt::format(", {0} warning{1}", warnings, (warnings == 1) ? "" : "s")) != std::string::npos, Wide(changed.err).c_str());

            // script.001 is there now; 1.scr is another name for script 1.
            fs::copy_file(fs::path(_copyFolder) / "script.001", fs::path(_copyFolder) / "1.scr");
            cli::StringConsole dryRun = Expect(0, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            Assert::IsTrue(dryRun.err.find("1.scr is a patch file with another name for a compiled resource") != std::string::npos, Wide(dryRun.err).c_str());
            // The count has the warning of the batch.
            size_t dryRunWarnings = CountOf(dryRun.err, ": warning : ") + CountOf(dryRun.err, "scic: warning: ");
            Assert::IsTrue(dryRun.err.find(fmt::format(", {0} warning{1}", dryRunWarnings, (dryRunWarnings == 1) ? "" : "s")) != std::string::npos, Wide(dryRun.err).c_str());

            // A commit that fails after the scripts compiled (a read-only file
            // in the output folder) lists no file, and a dry run of it says
            // that a run would write none.
            fs::path out = fs::path(_copyFolder) / "c3out";
            fs::create_directories(out);
            WriteReadOnlyFile((out / "script.001").string(), "an old file");
            cli::StringConsole failed = Expect(9, { "script", "compile", _copyFolder, "rm001", "--out-dir", out.string(), "-v" });
            Assert::IsTrue(failed.err.find("Compiled 1 of 1 scripts, and wrote none") != std::string::npos, Wide(failed.err).c_str());
            Assert::IsTrue(ListedFiles(failed.err, "wrote ").empty(), Wide(failed.err).c_str());
            cli::StringConsole failedDryRun = Expect(9, { "script", "compile", _copyFolder, "rm001", "--out-dir", out.string(), "--dry-run" });
            Assert::IsTrue(failedDryRun.err.find("Compiled 1 of 1 scripts, and a run would write none") != std::string::npos, Wide(failedDryRun.err).c_str());
            Assert::IsTrue(ListedFiles(failedDryRun.err, "would write ").empty(), Wide(failedDryRun.err).c_str());
        }

        // The summary counts every warning that printed: also a warning of
        // the selection (a script of game.ini with no source).
        TEST_METHOD(Compile_TheSummaryCountsTheWarningsOfTheSelection)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::remove(fs::path(_copyFolder) / "src" / "rm001.sc");
            cli::StringConsole console = Expect(0, { "script", "compile", _copyFolder, "--all", "--dry-run" });
            size_t warnings = CountOf(console.err, ": warning : ") + CountOf(console.err, "scic: warning: ");
            Assert::IsTrue(console.err.find("scic: warning: ") != std::string::npos, Wide("setup: the selection warns: " + console.err).c_str());
            Assert::IsTrue(console.err.find(fmt::format(", {0} warning{1}", warnings, (warnings == 1) ? "" : "s")) != std::string::npos, Wide(console.err).c_str());
        }

        // A relative --out-dir gives absolute paths in the report.
        TEST_METHOD(Compile_ARelativeOutDir_PrintsAbsolutePaths)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::create_directories(fs::path(_copyFolder) / "c3out");
            fs::path saved = fs::current_path();
            fs::current_path(_copyFolder);
            cli::StringConsole console;
            int code = Run({ "script", "compile", _copyFolder, "rm001", "--out-dir", "c3out", "--dry-run" }, console);
            fs::current_path(saved);
            Assert::AreEqual(0, code, Wide(console.err).c_str());
            std::string expected = "would write " + (fs::path(_copyFolder) / "c3out" / "script.001").string();
            Assert::IsTrue(console.err.find(expected) != std::string::npos, Wide(expected + " | " + console.err).c_str());
        }

        // A dry run gives the warnings of a run in its own words: the tables
        // "would be written" as patch files of a game that keeps its
        // resources in the package, and a .sco that a run would change.
        TEST_METHOD(Compile_DryRun_TheWarningsOfARun)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            ReplaceFirst((fs::path(_copyFolder) / "src" / "rm001.sc").string(), "(public\r\n\trm001 0\r\n)\r\n",
                "(public\r\n\trm001 0\r\n)\r\n(class C3Widget of Obj\r\n\t(properties\r\n\t\tc3WidgetSize 0\r\n\t)\r\n)\r\n");
            cli::StringConsole dryRun = Expect(0, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            Assert::IsTrue(dryRun.err.find("the class and selector tables would be written as patch files") != std::string::npos, Wide(dryRun.err).c_str());
            Assert::IsTrue(dryRun.err.find("a run would change a .sco file, so the scripts that use it would be out of date") != std::string::npos, Wide(dryRun.err).c_str());
            cli::StringConsole run = Expect(0, { "script", "compile", _copyFolder, "rm001" });
            Assert::IsTrue(run.err.find("the class and selector tables were written as patch files") != std::string::npos, Wide(run.err).c_str());
            Assert::IsTrue(run.err.find("a .sco file changed, so the scripts that use it can be out of date") != std::string::npos, Wide(run.err).c_str());
        }

        // The text of a Windows error has no line break of its own, so no
        // blank line comes after "Unable to open".
        TEST_METHOD(Compile_AMissingObjectFile_NoBlankLine)
        {
            CopyTemplate("\\TemplateGame\\SCI0");
            fs::remove(fs::path(_copyFolder) / "src" / "Main.sco");
            cli::StringConsole console = Expect(5, { "script", "compile", _copyFolder, "rm001", "--dry-run" });
            size_t at = console.err.find("Unable to open '");
            Assert::IsTrue(at != std::string::npos, Wide(console.err).c_str());
            size_t end = console.err.find('\n', at);
            Assert::IsTrue((end != std::string::npos) && (console.err[end - 1] != '\r'), Wide(console.err).c_str());
            Assert::IsTrue(console.err.find("\n\n") == std::string::npos, Wide(console.err).c_str());
        }
    };
}
