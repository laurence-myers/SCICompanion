#include "stdafx.h"
#include "CppUnitTest.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "GameFolderHelper.h"
#include "CompiledScript.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "DecompileRun.h"
#include "DecompilerResults.h"
#include "ScriptNameMap.h"
#include "ScriptCatalog.h"
#include "SCO.h"
#include "Stream.h"
#include "Helper.h"
#include "TestSupport.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // Every status of the report, for an assert text.
    std::string DescribeRun(const DecompileReport &report)
    {
        std::string text;
        for (const DecompileOutcome &outcome : report.scripts)
        {
            text += std::to_string(outcome.number) + " " + outcome.name + ": " + (outcome.status ? std::string("ok") : outcome.status.error().ToString()) + "\n";
        }
        for (const std::string &warning : report.warnings)
        {
            text += "warning: " + warning + "\n";
        }
        if (!report.gameIni)
        {
            text += "game.ini: " + report.gameIni.error().ToString() + "\n";
        }
        return text;
    }

    // Asserts that the run has a report and that the report succeeded; the
    // message is the report.
    void AssertSucceeded(const sci::Result<DecompileReport> &report)
    {
        Assert::IsTrue(report.has_value() && report->Succeeded(), report ? Wide(DescribeRun(*report)).c_str() : L"no report");
    }

    // The messages of a run. It can stop the run in the script after the
    // first abortAfter scripts.
    class RunResults : public IDecompilerResults
    {
    public:
        void AddResult(DecompilerResultType type, const std::string &message) override
        {
            if ((type == DecompilerResultType::Error) || (type == DecompilerResultType::Warning))
            {
                problems.push_back(message);
            }
            if (message.rfind("Decompiling script ", 0) == 0)
            {
                started++;
            }
            if (!abortOnMessage.empty() && (message.rfind(abortOnMessage, 0) == 0) && (++abortMatches >= abortOnMatch))
            {
                abortSeen = true;
            }
            if (onMessage)
            {
                onMessage(message);
            }
            if (!throwOnMessage.empty() && (message.rfind(throwOnMessage, 0) == 0))
            {
                throw std::runtime_error("an injected fault");
            }
        }
        bool IsAborted() override { return ((abortAfter >= 0) && (started > abortAfter)) || abortSeen; }
        void InformStats(bool functionSuccessful, int byteCount) override {}
        void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &renames) override {}

        std::vector<std::string> problems;
        int abortAfter = -1;
        int started = 0;
        // Abort when a message starts with this text, at its abortOnMatch-th
        // message.
        std::string abortOnMessage;
        int abortOnMatch = 1;
        int abortMatches = 0;
        bool abortSeen = false;
        // Throw from AddResult when a message starts with this text. At a
        // message outside the exception boundary of a script ("Decompiling
        // script N"), the batch throws; at one inside it ("Generated ..."),
        // the script fails.
        std::string throwOnMessage;
        // Called with each message.
        std::function<void(const std::string &)> onMessage;
    };

    class CollectSources : public IDecompileOutput
    {
    public:
        void OnSource(uint16_t scriptNumber, const std::string &source) override
        {
            sources.push_back({ scriptNumber, source });
        }
        std::vector<std::pair<uint16_t, std::string>> sources;
    };

    // Text without CR, to compare a file with an output.
    std::string WithoutCR(const std::string &text)
    {
        std::string result;
        for (char ch : text)
        {
            if (ch != '\r')
            {
                result.push_back(ch);
            }
        }
        return result;
    }

    // True when one of the texts contains "what".
    bool AnyContains(const std::vector<std::string> &texts, const std::string &what)
    {
        return std::any_of(texts.begin(), texts.end(), [&what](const std::string &text) { return text.find(what) != std::string::npos; });
    }

    // The options of a run that resets the names (--reset-names), with this
    // mode for game.ini.
    DecompileRunOptions ResetOptions(GameIniNames gameIni)
    {
        DecompileRunOptions options;
        options.names = NameAssignment::All;
        options.gameIni = gameIni;
        return options;
    }

    // The options of a run that also decompiles the stale scripts.
    DecompileRunOptions UpdateStaleOptions()
    {
        DecompileRunOptions options;
        options.updateStale = true;
        return options;
    }

    // A compile with no abort and no events.
    sci::Result<CompileReport> Compile(GameSession &session, const std::vector<ScriptId> &scripts, const CompileOptions &options = CompileOptions())
    {
        std::atomic<bool> abort(false);
        ICompileEvents events;
        return CompileScripts(session, scripts, options, abort, events);
    }

    // The errors of a compile, for an assert text: "" when it compiled.
    std::string CompileErrorsOf(const sci::Result<CompileReport> &report)
    {
        if (!report)
        {
            return "the compile did not start: " + report.error().ToString() + "\n";
        }
        std::string errors;
        for (const ScriptOutcome &outcome : report->scripts)
        {
            if (!outcome.status)
            {
                errors += outcome.name + ": " + outcome.status.error().ToString() + "\n";
            }
            for (const CompileResult &result : outcome.diagnostics)
            {
                if (result.IsError())
                {
                    errors += "  " + result.GetMessage() + "\n";
                }
            }
        }
        if (!report->commit)
        {
            errors += "commit: " + report->commit.error().ToString() + "\n";
        }
        return errors;
    }

    // The parts of a .sco that a compile reads: the classes (name, species,
    // superclass, property selectors, methods), the locals and the exports.
    // Not the property values: the compiler writes temporary string tokens
    // there, and the compiled script has the offsets of the strings.
    std::string DescribeObjectFile(const std::string &bytes, const SelectorTable &selectors)
    {
        sci::streamOwner owner((const uint8_t *)bytes.data(), (uint32_t)bytes.size());
        sci::istream reader = owner.getReader();
        CSCOFile sco;
        sco.Load(reader, selectors);
        std::string text = "script " + std::to_string(sco.GetScriptNumber()) + "\n";
        for (const CSCOObjectClass &object : sco.GetObjects())
        {
            text += "class " + object.GetName() + " species " + std::to_string(object.GetSpecies()) + " super " + std::to_string(object.GetSuperClass()) + " properties";
            for (const CSCOObjectProperty &property : object.GetProperties())
            {
                text += " " + std::to_string(property.GetSelector());
            }
            text += " methods";
            for (uint16_t method : object.GetMethods())
            {
                text += " " + std::to_string(method);
            }
            text += "\n";
        }
        text += "locals";
        for (const CSCOLocalVariable &variable : sco.GetVariables())
        {
            text += " [" + variable.GetName() + "]";
        }
        text += "\nexports";
        for (const CSCOPublicExport &entry : sco.GetExports())
        {
            text += " " + entry.GetName() + "@" + std::to_string(entry.GetIndex());
        }
        return text + "\n";
    }

    // The files of a folder, and their text.
    std::map<std::string, std::string> FilesOf(const std::string &folder)
    {
        std::map<std::string, std::string> files;
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && (it != end); it.increment(ec))
        {
            if (it->is_regular_file())
            {
                files[it->path().filename().string()] = ReadFileText(it->path().string());
            }
        }
        return files;
    }
}

namespace UnitTests
{
    // The decompile run (RunDecompile) of the command line and of the
    // Decompile dialog: the names of the scripts (only Create creates
    // game.ini), the src folder, and a status for each script, also for a
    // script that does not load. Also script sco (GenerateObjectFiles).
    TEST_CLASS(TestDecompileRun)
    {
        // The framework makes a new instance of the class for each test
        // method: each test runs with no AppState, on its own copy of a game.
        NoAppState _noAppState;
        GameCopy _game;

        std::string IniEntry(const std::string &key) const
        {
            char value[200] = {};
            GetPrivateProfileString("Script", key.c_str(), "", value, (DWORD)ARRAYSIZE(value), _game.Path("game.ini").c_str());
            return value;
        }

        static const DecompileOutcome *OutcomeOf(const DecompileReport &report, uint16_t number)
        {
            for (const DecompileOutcome &outcome : report.scripts)
            {
                if (outcome.number == number)
                {
                    return &outcome;
                }
            }
            return nullptr;
        }

        // The path of Main.sco, from a session that closes before the test
        // continues.
        std::string MainObjectFile()
        {
            std::string path = _game.Open().Helper().GetScriptObjectFileName((WORD)0);
            _game.CloseSessions();
            return path;
        }

        // The SCI1.1 template with two scripts (965 and 966 are free there):
        // 965 has (if global3 (= global5 gEgo)), and 966 has (= global3
        // global5). Slots 3 and 5 of Main.sco get their standard names first.
        // A run of 965 with updateStale names global5 in group 1; group 2
        // (966) names global3, which 965 uses; group 3 is 0 and 965.
        void PrepareEarlierGroupFixtures()
        {
            _game.Make(TemplateSci11);
            NameMainGlobals(_game, { 3, 5 });
            WriteFileText(_game.Src("StaleFirst.sc"), ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 965)\r\n(include sci.sh)\r\n(use Main)\r\n\r\n(public\r\n\tstaleFirst 0\r\n)\r\n\r\n(procedure (staleFirst)\r\n\t(if global3\r\n\t\t(= global5 gEgo)\r\n\t)\r\n)\r\n");
            WriteFileText(_game.Src("StaleSecond.sc"), ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 966)\r\n(include sci.sh)\r\n(use Main)\r\n\r\n(public\r\n\tstaleSecond 0\r\n)\r\n\r\n(procedure (staleSecond)\r\n\t(= global3 global5)\r\n)\r\n");
            auto compiled = Compile(_game.Open(), { ScriptAt(_game.Src("StaleFirst.sc"), 965), ScriptAt(_game.Src("StaleSecond.sc"), 966) });
            Assert::AreEqual(std::string(), CompileErrorsOf(compiled), L"setup: the fixtures compile");
            _game.CloseSessions();
        }

    public:
        // With no game.ini, the names come from the compiled scripts, the run
        // writes src\<name>.sc, and nothing creates game.ini (plan section
        // 3.4).
        TEST_METHOD(NoGameIni_NothingCreatesIt)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0, true);
            RunResults results;
            auto report = RunDecompile(session, { 974 }, DecompileRunOptions(), results);
            AssertOk(report);
            Assert::IsTrue(report->Succeeded(), Wide(DescribeRun(*report)).c_str());
            Assert::AreEqual(std::string("Door"), report->scripts[0].name, L"the derived name");
            Assert::IsTrue(fs::exists(_game.Src("Door.sc")) && fs::exists(_game.Src("Door.sco")));
            Assert::IsTrue(ReadFileText(_game.Src("Door.sc")).find("(script# 974)") != std::string::npos);
            Assert::IsFalse(fs::exists(_game.Path("game.ini")), L"nothing creates game.ini");
            Assert::IsTrue(fs::exists(_game.Src("Decompiler.ini")), L"the src folder gets Decompiler.ini");
            Assert::IsTrue(report->stats.functions > 0, L"the statistics count the functions");
        }

        // --game-ini: Update adds an entry for a written script that game.ini
        // does not name; Create also creates game.ini; None writes nothing.
        TEST_METHOD(GameIni_UpdateCreateNone)
        {
            for (GameIniNames mode : { GameIniNames::Update, GameIniNames::None })
            {
                _game.Make(TemplateSci0);
                // Script 974 keeps its name from src\door.sc (rule 2), with no
                // game.ini entry.
                Assert::IsTrue(WritePrivateProfileString("Script", "n974", nullptr, _game.Path("game.ini").c_str()) != 0);
                Assert::AreEqual(std::string(), IniEntry("n974"), L"setup: no entry");
                std::string mainEntry = IniEntry("n000");
                GameSession &session = _game.Open();
                RunResults results;
                DecompileRunOptions options;
                options.gameIni = mode;
                AssertSucceeded(RunDecompile(session, { 974 }, options, results));
                Assert::AreEqual((mode == GameIniNames::Update) ? std::string("door") : std::string(), IniEntry("n974"));
                Assert::AreEqual(mainEntry, IniEntry("n000"), L"an entry that game.ini has does not change");
            }

            GameSession &session = _game.OpenCopy(TemplateSci0, true);
            RunResults results;
            DecompileRunOptions options;
            options.gameIni = GameIniNames::Create;
            AssertSucceeded(RunDecompile(session, { 974 }, options, results));
            Assert::IsTrue(fs::exists(_game.Path("game.ini")), L"Create creates game.ini");
            Assert::AreEqual(std::string("Door"), IniEntry("n974"));
        }

        // --reset-names: each script of the run gets its derived name, also
        // one that game.ini names; the old files keep their names, and a
        // warning lists them. Update writes the new name into game.ini, so
        // the GUI finds the new file.
        TEST_METHOD(ResetNames_TakesTheDerivedNames)
        {
            _game.Make(TemplateSci0);
            Assert::IsTrue(WritePrivateProfileString("Script", "n974", "OldDoor", _game.Path("game.ini").c_str()) != 0);
            fs::rename(_game.Src("door.sc"), _game.Src("OldDoor.sc"));
            fs::rename(_game.Src("door.sco"), _game.Src("OldDoor.sco"));
            std::string oldText = ReadFileText(_game.Src("OldDoor.sc"));
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 974 }, ResetOptions(GameIniNames::Update), results);
            AssertSucceeded(report);
            Assert::AreEqual(std::string("Door"), report->scripts[0].name);
            Assert::IsTrue(fs::exists(_game.Src("Door.sc")), L"the new file has the derived name");
            Assert::AreEqual(oldText, ReadFileText(_game.Src("OldDoor.sc")), L"the old file keeps its name and its text");
            std::string warnings = JoinLines(report->warnings);
            Assert::IsTrue((warnings.find("OldDoor.sc") != std::string::npos) && (warnings.find("OldDoor.sco") != std::string::npos), Wide(warnings).c_str());
            Assert::AreEqual(std::string("Door"), IniEntry("n974"), L"game.ini has the new name");
        }

        // The src folder: made when it does not exist; Decompiler.ini and the
        // other files of the Decompiler folder are copied once, and a file of
        // the game is never overwritten.
        TEST_METHOD(PrepareDecompileFolder_CopiesOnceAndNeverOverwrites)
        {
            _game.Make(TemplateSci0);
            fs::path decompiler = _game.Path("DecompilerData");
            fs::create_directories(decompiler);
            WriteFileText((decompiler / "Decompiler.ini").string(), "[first]\n");
            WriteFileText((decompiler / "game.sh").string(), "; not the game's\n");
            WriteFileText((decompiler / "extra.txt").string(), "extra\n");
            std::string gameHeader = ReadFileText(_game.Src("game.sh"));
            GameSession &session = _game.Open();

            AssertOk(PrepareDecompileFolder(session.Helper(), decompiler.string()));
            Assert::AreEqual(std::string("[first]\n"), ReadFileText(_game.Src("Decompiler.ini")));
            Assert::AreEqual(std::string("extra\n"), ReadFileText(_game.Src("extra.txt")));
            Assert::AreEqual(gameHeader, ReadFileText(_game.Src("game.sh")), L"the game's game.sh is not overwritten");

            WriteFileText((decompiler / "Decompiler.ini").string(), "[second]\n");
            fs::remove(_game.Src("extra.txt"));
            AssertOk(PrepareDecompileFolder(session.Helper(), decompiler.string()));
            Assert::AreEqual(std::string("[first]\n"), ReadFileText(_game.Src("Decompiler.ini")), L"copied once");
            Assert::IsFalse(fs::exists(_game.Src("extra.txt")), L"nothing is copied while Decompiler.ini exists");

            fs::remove_all(_game.Path("src"));
            AssertOk(PrepareDecompileFolder(session.Helper(), _game.Path("NoSuchFolder")));
            Assert::IsTrue(fs::is_directory(_game.Path("src")), L"the src folder is made");
        }

        // An output (--stdout) gets the source, and the run writes nothing: no
        // src folder, no .sc, no .sco, no game.ini.
        TEST_METHOD(Output_WritesNoFile)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0, true);
            RunResults results;
            CollectSources output;
            DecompileRunOptions options;
            options.gameIni = GameIniNames::Create;
            AssertSucceeded(RunDecompile(session, { 974 }, options, results, &output));
            Assert::AreEqual((size_t)1, output.sources.size());
            Assert::IsTrue(output.sources[0].first == 974);
            Assert::IsTrue(output.sources[0].second.find("(script# 974)") != std::string::npos, Wide(output.sources[0].second).c_str());
            Assert::IsFalse(fs::exists(_game.Path("src")), L"no src folder");
            Assert::IsFalse(fs::exists(_game.Path("game.ini")), L"no game.ini, also with Create");

            // With a src folder, its files do not change.
            _game.Make(TemplateSci0);
            std::map<std::string, std::string> before = FilesOf(_game.Path("src"));
            GameSession &withSource = _game.Open();
            CollectSources output2;
            AssertSucceeded(RunDecompile(withSource, { 0, 974 }, DecompileRunOptions(), results, &output2));
            Assert::AreEqual((size_t)2, output2.sources.size());
            Assert::IsTrue(before == FilesOf(_game.Path("src")), L"no file of src changes");
        }

        // A script that does not load gets its error in the report, and the
        // others are written.
        TEST_METHOD(FailedScript_HasItsStatus_TheOthersAreWritten)
        {
            _game.Make(TemplateSci0);
            WriteUnreadableScript(_game, 905);
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 905, 974 }, DecompileRunOptions(), results);
            AssertOk(report);
            const DecompileOutcome *bad = OutcomeOf(*report, 905);
            const DecompileOutcome *good = OutcomeOf(*report, 974);
            Assert::IsTrue((bad != nullptr) && (good != nullptr), Wide(DescribeRun(*report)).c_str());
            Assert::IsFalse(bad->status.has_value());
            Assert::IsTrue(bad->status.error().code != sci::ErrorCode::Cancelled, Wide(DescribeRun(*report)).c_str());
            Assert::IsTrue(good->status.has_value(), Wide(DescribeRun(*report)).c_str());
            Assert::AreEqual((size_t)1, report->FailedCount());
            Assert::IsTrue(AnyContains(results.problems, "Script 905"), L"the failure is in the messages too");
        }

        // A .sc file that cannot be written fails its script: the script
        // does not count as written.
        TEST_METHOD(WriteError_FailsTheScript)
        {
            _game.Make(TemplateSci0);
            Assert::IsTrue(SetFileAttributes(_game.Src("door.sc").c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 974, 983 }, DecompileRunOptions(), results);
            AssertOk(report);
            const DecompileOutcome *door = OutcomeOf(*report, 974);
            Assert::IsTrue((door != nullptr) && !door->status, Wide(DescribeRun(*report)).c_str());
            Assert::IsTrue(door->status.error().code != sci::ErrorCode::Cancelled, Wide(DescribeRun(*report)).c_str());
            Assert::IsTrue(OutcomeOf(*report, 983)->status.has_value(), Wide(DescribeRun(*report)).c_str());
        }

        // An abort stops the run: the report is cancelled, and a script that
        // the run did not finish is Cancelled.
        TEST_METHOD(Abort_CancelsTheRest)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0);
            RunResults results;
            results.abortAfter = 1;
            auto report = RunDecompile(session, { 974, 983 }, DecompileRunOptions(), results);
            AssertOk(report);
            Assert::IsTrue(report->cancelled);
            Assert::IsTrue(OutcomeOf(*report, 974)->status.has_value(), Wide(DescribeRun(*report)).c_str());
            Assert::IsTrue(!OutcomeOf(*report, 983)->status && (OutcomeOf(*report, 983)->status.error().code == sci::ErrorCode::Cancelled), Wide(DescribeRun(*report)).c_str());
        }

        // script sco (plan section 4.6): with no .sco file in src, the .sco
        // files made from the sources and the compiled scripts have what the
        // compiler's .sco files have, and they let a compile of every script
        // start; on both templates. As the compiler does, a class gets its
        // name in the source, not its name string ("Blk" for Block), and a
        // source gets the locals of its .shp include (110.shp).
        TEST_METHOD(ObjectFiles_MatchTheCompiler_BothTemplates)
        {
            for (const char *templateFolder : { TemplateSci0, TemplateSci11 })
            {
                {
                    // The .sco files of the compiler.
                    GameSession &session = _game.OpenCopy(templateFolder);
                    auto selection = SelectAllScripts(session, SelectorMode::Compile);
                    AssertOk(selection);
                    CompileOptions options;
                    options.passes = 3;
                    Assert::AreEqual(std::string(), CompileErrorsOf(Compile(session, selection->scripts, options)), L"setup: the template compiles");
                }
                _game.CloseSessions();
                std::map<std::string, std::string> compiler;
                for (const auto &entry : fs::directory_iterator(_game.Path("src")))
                {
                    if (_stricmp(entry.path().extension().string().c_str(), ".sco") == 0)
                    {
                        compiler[Upper(entry.path().filename().string())] = ReadFileText(entry.path().string());
                        fs::remove(entry.path());
                    }
                }
                Assert::IsTrue(compiler.size() > 20, L"setup: the .sco files of the compiler");

                GameSession &session = _game.Open();
                auto selection = SelectAllScripts(session, SelectorMode::Sco);
                Assert::IsTrue(selection.has_value() && (selection->scripts.size() > 20), L"setup: the scripts with a source");
                auto outcomes = GenerateObjectFiles(session, selection->scripts);
                // No diagnostic: the compile above made each export table from
                // its public block. (The shipped SCI1.1 template's Main and
                // DebugHandler export slots that their sources do not list;
                // TestCli::Sco_BothTemplates sees those warnings.)
                for (const ObjectFileOutcome &outcome : ValueOf(outcomes))
                {
                    std::string warnings;
                    for (const CompileResult &diagnostic : outcome.diagnostics)
                    {
                        warnings += diagnostic.GetMessage() + "\n";
                    }
                    Assert::IsTrue(outcome.status.has_value() && outcome.skipped.empty() && warnings.empty(),
                        Wide(outcome.name + ": " + (outcome.status ? outcome.skipped : outcome.status.error().ToString()) + "\n" + warnings).c_str());
                }
                GlobalCompiledScriptLookups lookups;
                AssertOk(lookups.TryLoad(session.Helper()));
                std::string differences;
                for (const auto &file : compiler)
                {
                    std::string made = ReadFileText(_game.Src(file.first));
                    if (made.empty())
                    {
                        differences += file.first + ": not made\n";
                        continue;
                    }
                    std::string expected = DescribeObjectFile(file.second, lookups.GetSelectorTable());
                    std::string actual = DescribeObjectFile(made, lookups.GetSelectorTable());
                    if (expected != actual)
                    {
                        differences += file.first + ":\n  compiler: " + expected + "  made:     " + actual;
                    }
                }
                Assert::AreEqual(std::string(), differences, Wide(templateFolder).c_str());

                GameSession &compileSession = _game.Open();
                auto compileSelection = SelectAllScripts(compileSession, SelectorMode::Compile);
                AssertOk(compileSelection);
                Assert::AreEqual(std::string(), CompileErrorsOf(Compile(compileSession, compileSelection->scripts)), Wide(std::string("a compile from the made .sco files: ") + templateFolder).c_str());
            }
        }

        // A source whose classes differ in number from the compiled script
        // gets the names of the compiled script, and a warning.
        TEST_METHOD(ObjectFiles_OtherClassCount_Warns)
        {
            _game.Make(TemplateSci0);
            WriteFileText(_game.Src("door.sc"), ReadFileText(_game.Src("door.sc")) + "\n(class S4Extra of Obj\n)\n");
            GameSession &session = _game.Open();
            auto outcomes = GenerateObjectFiles(session, { ScriptAt(_game.Src("door.sc"), 974) });
            Assert::IsTrue(outcomes.has_value() && (outcomes->size() == 1));
            const ObjectFileOutcome &outcome = (*outcomes)[0];
            AssertOk(outcome.status);
            bool warned = false;
            for (const CompileResult &diagnostic : outcome.diagnostics)
            {
                warned = warned || ((diagnostic.GetMessage().find("classes") != std::string::npos) && !diagnostic.IsError());
            }
            Assert::IsTrue(warned, L"a warning for the class count");
            GlobalCompiledScriptLookups lookups;
            AssertOk(lookups.TryLoad(session.Helper()));
            std::unique_ptr<CSCOFile> made = GetExistingSCOFromScriptNumber(session.Helper(), 974, lookups.GetSelectorTable());
            Assert::IsTrue(made && (made->GetObjects().size() == 1) && (made->GetObjects()[0].GetName() == "Door"), L"the names of the compiled script");
        }

        // A script with no source file, or with no compiled script, is
        // skipped; a source with a syntax error fails its script and writes
        // no .sco; the others are written.
        TEST_METHOD(ObjectFiles_SkipsAndFails)
        {
            _game.Make(TemplateSci0);
            WriteFileText(_game.Src("S4Extra.sc"), "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n");
            std::string doorSco = ReadFileText(_game.Src("door.sco"));
            std::string door = ReadFileText(_game.Src("door.sc"));
            WriteFileText(_game.Src("door.sc"), door + "\n(procedure (s4Broken)\n    (= )\n)\n");
            fs::remove(_game.Src("wander.sco"));
            GameSession &session = _game.Open();
            auto outcomes = GenerateObjectFiles(session, { ScriptAt(_game.Src("S4NoSuchFile.sc"), 905), ScriptAt(_game.Src("S4Extra.sc"), 906),
                ScriptAt(_game.Src("door.sc"), 974), ScriptAt(_game.Src("wander.sc"), 983) });
            Assert::IsTrue(outcomes.has_value() && (outcomes->size() == 4));
            const std::vector<ObjectFileOutcome> &o = *outcomes;
            Assert::IsTrue(o[0].status.has_value() && (o[0].skipped.find("no source file") != std::string::npos), Wide(o[0].skipped).c_str());
            Assert::IsTrue(o[1].status.has_value() && (o[1].skipped.find("no compiled script") != std::string::npos), Wide(o[1].skipped).c_str());
            Assert::IsFalse(o[2].status.has_value(), L"a syntax error fails the script");
            Assert::IsTrue(o[2].status.error().code == sci::ErrorCode::Compile, Wide(o[2].status.error().ToString()).c_str());
            Assert::IsFalse(o[2].diagnostics.empty(), L"the syntax error is in the diagnostics");
            Assert::AreEqual(doorSco, ReadFileText(_game.Src("door.sco")), L"no .sco is written for a script that failed");
            Assert::IsTrue(o[3].status.has_value() && o[3].skipped.empty(), Wide(o[3].status ? o[3].skipped : o[3].status.error().ToString()).c_str());
            Assert::IsTrue(fs::exists(_game.Src("wander.sco")));
        }

        // A run on some of the scripts reports the scripts that still use a
        // renamed global by its old name; with updateStale, it decompiles
        // them too, and again until no script is stale.
        TEST_METHOD(StaleScripts_ReportedOrUpdated)
        {
            {
                PrepareStaleFixtures(_game);
                GameSession &session = _game.Open();
                RunResults results;
                auto report = RunDecompile(session, { 960 }, DecompileRunOptions(), results);
                AssertSucceeded(report);
                Assert::IsFalse(report->globalRenames.empty(), L"960 names global5");
                std::string renames;
                for (const auto &rename : report->globalRenames)
                {
                    renames += rename.first + "->" + rename.second + " ";
                }
                for (uint16_t stale : report->stale)
                {
                    renames += "stale:" + std::to_string(stale) + " ";
                }
                Assert::IsTrue(report->stale.count(959) == 1, Wide("959 still uses global5: " + renames).c_str());
                Assert::AreEqual((size_t)1, report->scripts.size(), L"without updateStale, 959 is only reported");
            }

            PrepareStaleFixtures(_game);
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 960 }, UpdateStaleOptions(), results);
            AssertSucceeded(report);
            Assert::IsTrue(report->stale.empty(), L"no script is stale at the end");
            Assert::IsNotNull(OutcomeOf(*report, 959), Wide(DescribeRun(*report)).c_str());
            std::set<uint16_t> attempted;
            for (const DecompileOutcome &outcome : report->scripts)
            {
                attempted.insert(outcome.number);
            }
            std::set<uint16_t> others;
            GlobalCompiledScriptLookups lookups;
            AssertOk(lookups.TryLoad(session.Helper()));
            for (CompiledScript *compiled : lookups.GetGlobalClassTable().GetAllScripts())
            {
                if (attempted.count(compiled->GetScriptNumber()) == 0)
                {
                    others.insert(compiled->GetScriptNumber());
                }
            }
            Assert::IsTrue(FindScriptsReferencingGlobals(session.Helper(), others, report->globalRenames).empty(), L"the run stops when no script is stale");
        }

        // A reset name is never the file title of another script: a reset of
        // script 979 (its first class is MenuBar) names it "MenuBar_979", so
        // the run does not write over menubar.sc, the source of script 997.
        TEST_METHOD(ResetNames_NeverTakeTheFileOfAnotherScript)
        {
            _game.Make(TemplateSci0);
            std::string menuBar = ReadFileText(_game.Src("menubar.sc"));
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 979 }, ResetOptions(GameIniNames::None), results);
            AssertSucceeded(report);
            Assert::AreEqual(std::string("MenuBar_979"), session.Helper().GetScriptTitle(979), L"the suffix of the naming rule");
            Assert::AreEqual(std::string("MenuBar"), session.Helper().GetScriptTitle(997), L"997 keeps its name");
            Assert::AreEqual(menuBar, ReadFileText(_game.Src("menubar.sc")), L"the source of 997 does not change");
            Assert::IsTrue(ReadFileText(_game.Src("MenuBar_979.sc")).find("(script# 979)") != std::string::npos);
            std::string warnings = JoinLines(report->warnings);
            Assert::IsTrue(warnings.find("Controls.sc keeps its old name: script 979 is now MenuBar_979") != std::string::npos, Wide(warnings).c_str());
            // It also goes to the results, when it is found.
            Assert::IsTrue(AnyContains(results.problems, "Controls.sc keeps its old name"), L"the warning goes to the results");
        }

        // A dry run writes nothing, and its report lists the files that a run
        // writes besides the scripts' own: the decompiler files of src and
        // game.ini. A reset says "would keep".
        TEST_METHOD(DryRun_ListsTheOtherFilesAndWritesNothing)
        {
            _game.Make(TemplateSci0);
            std::map<std::string, uintmax_t> before;
            for (const auto &entry : fs::recursive_directory_iterator(_game.Folder()))
            {
                before[entry.path().string()] = entry.is_regular_file() ? entry.file_size() : 0;
            }
            std::string gameIni = ReadFileText(_game.Path("game.ini"));
            GameSession &session = _game.Open();
            RunResults results;
            DecompileRunOptions options = ResetOptions(GameIniNames::Update);
            options.dryRun = true;
            auto report = RunDecompile(session, { 979 }, options, results);
            AssertSucceeded(report);
            std::map<std::string, uintmax_t> after;
            for (const auto &entry : fs::recursive_directory_iterator(_game.Folder()))
            {
                after[entry.path().string()] = entry.is_regular_file() ? entry.file_size() : 0;
            }
            Assert::IsTrue(before == after, L"a dry run writes no file");
            Assert::AreEqual(gameIni, ReadFileText(_game.Path("game.ini")), L"nor game.ini");
            std::string files = JoinLines(report->files);
            Assert::IsTrue(files.find("\\src\\Decompiler.ini\n") != std::string::npos, Wide(files).c_str());
            Assert::IsTrue(files.find("\\game.ini\n") != std::string::npos, Wide("n979=MenuBar_979: " + files).c_str());
            std::string warnings = JoinLines(report->warnings);
            Assert::IsTrue(warnings.find("Controls.sc would keep its old name: script 979 would be MenuBar_979") != std::string::npos, Wide(warnings).c_str());
        }

        // A reset name is never the title of the source of a script that the
        // game has not compiled, alone or in a conflict: script 974 gets
        // "Door_974", so the run does not write over Door.sc.
        TEST_METHOD(ResetNames_KeepTheFileOfAnUncompiledScript)
        {
            for (bool conflict : { false, true })
            {
                _game.Make(TemplateSci0);
                Assert::IsTrue(WritePrivateProfileString("Script", "n974", "OldDoor", _game.Path("game.ini").c_str()) != 0);
                fs::rename(_game.Src("door.sc"), _game.Src("OldDoor.sc"));
                fs::rename(_game.Src("door.sco"), _game.Src("OldDoor.sco"));
                const std::string uncompiled = "(script# 961)\r\n";
                WriteFileText(_game.Src("Door.sc"), uncompiled);
                if (conflict)
                {
                    WriteFileText(_game.Src("DoorCopy.sc"), uncompiled);
                }
                GameSession &session = _game.Open();
                RunResults results;
                AssertSucceeded(RunDecompile(session, { 974 }, ResetOptions(GameIniNames::None), results));
                Assert::AreEqual(std::string("Door_974"), session.Helper().GetScriptTitle(974), conflict ? L"a file in a conflict" : L"a file of an uncompiled script");
                Assert::AreEqual(uncompiled, ReadFileText(_game.Src("Door.sc")), L"Door.sc does not change");
            }
        }

        // A reset of some scripts resets only them. The other scripts keep
        // their names, also in memory, so the sources agree with game.ini:
        // Door.sc gets (use OldCycle), as game.ini has n992=OldCycle, and it
        // compiles.
        TEST_METHOD(ResetNames_OnlyTheChosenScripts)
        {
            _game.Make(TemplateSci0);
            Assert::IsTrue(WritePrivateProfileString("Script", "n992", "OldCycle", _game.Path("game.ini").c_str()) != 0);
            fs::rename(_game.Src("cycle.sc"), _game.Src("OldCycle.sc"));
            fs::rename(_game.Src("cycle.sco"), _game.Src("OldCycle.sco"));
            {
                GameSession &session = _game.Open();
                RunResults results;
                AssertSucceeded(RunDecompile(session, { 974 }, ResetOptions(GameIniNames::Update), results));
                Assert::AreEqual(std::string("OldCycle"), session.Helper().GetScriptTitle(992), L"992 is not reset");
            }
            _game.CloseSessions();
            std::string door = ReadFileText(_game.Src("door.sc"));
            Assert::IsTrue(door.find("(use OldCycle)") != std::string::npos, Wide(door.substr(0, 400)).c_str());
            Assert::AreEqual(std::string("OldCycle"), IniEntry("n992"));

            Assert::AreEqual(std::string(), CompileErrorsOf(Compile(_game.Open(), { ScriptAt(_game.Src("door.sc"), 974) })), L"the reset source compiles");
        }

        // With updateStale, a later group that names a global makes a script
        // of an earlier group stale, and the run decompiles it again: 965
        // uses global3, which group 2 names, and after the run 965 compiles.
        TEST_METHOD(UpdateStale_AlsoAScriptOfAnEarlierGroup)
        {
            PrepareEarlierGroupFixtures();
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 965 }, UpdateStaleOptions(), results);
            AssertSucceeded(report);
            std::string renames;
            for (const auto &rename : report->globalRenames)
            {
                renames += rename.first + "->" + rename.second + " ";
            }
            Assert::IsTrue(renames.find("global3->") != std::string::npos, Wide("setup: a later group names global3: " + renames).c_str());
            std::string first = ReadFileText(session.Helper().GetScriptFileName((WORD)965));
            Assert::IsFalse(ContainsIdentifier(first, "global3") || ContainsIdentifier(first, "global5"), Wide(first).c_str());
            size_t outcomes965 = 0;
            for (const DecompileOutcome &outcome : report->scripts)
            {
                outcomes965 += (outcome.number == 965) ? 1 : 0;
            }
            Assert::AreEqual((size_t)1, outcomes965, L"one outcome for a script that two groups decompiled");

            auto compiled = Compile(_game.Open(), { ScriptAt(session.Helper().GetScriptFileName((WORD)965), 965) });
            Assert::AreEqual(std::string(), CompileErrorsOf(compiled), L"965 compiles after the run");
        }

        // An abort before a later group reaches a script that an earlier
        // group wrote keeps its written outcome, game.ini gets its name, and
        // the stale list has it (its file still uses a global that a later
        // group named).
        TEST_METHOD(Abort_KeepsTheScriptsThatAGroupWrote)
        {
            PrepareEarlierGroupFixtures();
            GameSession &session = _game.Open();
            RunResults results;
            // Group 1 is 965, group 2 is 966 (it names global3), and group 3
            // starts with script 0: the abort comes there.
            results.abortOnMessage = "Decompiling script 0";
            auto report = RunDecompile(session, { 965 }, UpdateStaleOptions(), results);
            AssertOk(report);
            Assert::IsTrue(report->cancelled, Wide("setup: the abort came:\n" + DescribeRun(*report)).c_str());
            const DecompileOutcome *first = OutcomeOf(*report, 965);
            Assert::IsTrue((first != nullptr) && first->status.has_value(), Wide(DescribeRun(*report)).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(965), Wide(DescribeRun(*report)).c_str());
            Assert::AreEqual(std::string("StaleFirst"), IniEntry("n965"), L"game.ini gets the name of the written script");
        }

        // A reset never gives two scripts one name. A chosen script that gets
        // no derived name (978 has procedures only, or it cannot be read)
        // keeps its name, and no other chosen script takes it: 974 gets
        // "Door_974", and the two scripts write different files.
        TEST_METHOD(ResetNames_TwoScriptsNeverShareAName)
        {
            for (bool unreadable : { false, true })
            {
                _game.Make(TemplateSci0);
                Assert::IsTrue(WritePrivateProfileString("Script", "n974", "OldDoor", _game.Path("game.ini").c_str()) != 0);
                Assert::IsTrue(WritePrivateProfileString("Script", "n978", "Door", _game.Path("game.ini").c_str()) != 0);
                fs::rename(_game.Src("door.sc"), _game.Src("OldDoor.sc"));
                fs::rename(_game.Src("door.sco"), _game.Src("OldDoor.sco"));
                if (unreadable)
                {
                    WriteUnreadableScript(_game, 978);
                }
                GameSession &session = _game.Open();
                RunResults results;
                auto report = RunDecompile(session, { 974, 978 }, ResetOptions(GameIniNames::None), results);
                AssertOk(report);
                Assert::AreEqual(std::string("Door"), session.Helper().GetScriptTitle(978), unreadable ? L"978 cannot be read and keeps its name" : L"978 has no derived name and keeps its name");
                Assert::AreEqual(std::string("Door_974"), session.Helper().GetScriptTitle(974), L"974 does not take the name of 978");
                Assert::IsTrue(ReadFileText(_game.Src("Door_974.sc")).find("(script# 974)") != std::string::npos, Wide(DescribeRun(*report)).c_str());
            }
        }

        // A reset of two scripts where one has the file of the name that the
        // other derives. The title of a file belongs to its script: 997 keeps
        // menubar.sc, and 979 gets the suffix.
        TEST_METHOD(ResetNames_TwoChosenScripts_OneKeepsItsFile)
        {
            _game.Make(TemplateSci0);
            std::string menuBar = ReadFileText(_game.Src("menubar.sc"));
            GameSession &session = _game.Open();
            RunResults results;
            AssertSucceeded(RunDecompile(session, { 979, 997 }, ResetOptions(GameIniNames::None), results));
            Assert::AreEqual(std::string("MenuBar_979"), session.Helper().GetScriptTitle(979));
            Assert::AreEqual(std::string("TheMenuBar"), session.Helper().GetScriptTitle(997));
            Assert::AreEqual(menuBar, ReadFileText(_game.Src("menubar.sc")), L"the old file of 997 does not change");
        }

        // With no [Script] entry in game.ini, the names of every script go
        // into game.ini, but not the names of a conflict (the GUI does not
        // check game.ini for one). Here door.sco has the objects of Wander
        // (983), so "door" is the name of 974 and of 983.
        TEST_METHOD(GameIni_EveryName_NotAConflict)
        {
            _game.Make(TemplateSci0);
            Assert::IsTrue(WritePrivateProfileString("Script", nullptr, nullptr, _game.Path("game.ini").c_str()) != 0, L"setup: no [Script] section");
            fs::copy_file(_game.Src("wander.sco"), _game.Src("door.sco"), fs::copy_options::overwrite_existing);
            fs::remove(_game.Src("wander.sco"));
            fs::remove(_game.Src("wander.sc"));
            GameSession &session = _game.Open();
            Assert::IsFalse(session.Helper().ScriptNames->ConflictsOf(974).empty(), L"setup: a conflict");
            RunResults results;
            AssertSucceeded(RunDecompile(session, { 985 }, DecompileRunOptions(), results));
            Assert::AreEqual(std::string("Main"), IniEntry("n000"), L"the other names");
            Assert::AreEqual(std::string(), IniEntry("n974"), L"no name of the conflict");
            Assert::AreEqual(std::string(), IniEntry("n983"), L"no name of the conflict");
        }

        // An include that is not there names itself in the error of script
        // sco.
        TEST_METHOD(ObjectFiles_AMissingInclude_NamesIt)
        {
            _game.Make(TemplateSci0);
            std::string text = ReadFileText(_game.Src("door.sc"));
            text.insert(text.find('\n') + 1, "(include s2NoSuchHeader.sh)\r\n");
            WriteFileText(_game.Src("door.sc"), text);
            GameSession &session = _game.Open();
            auto outcomes = GenerateObjectFiles(session, { ScriptAt(_game.Src("door.sc"), 974) });
            Assert::IsTrue(outcomes.has_value() && (outcomes->size() == 1));
            const ObjectFileOutcome &outcome = (*outcomes)[0];
            Assert::IsFalse(outcome.status.has_value());
            Assert::IsTrue((outcome.status.error().code == sci::ErrorCode::NotFound) && (outcome.status.error().ToString().find("s2NoSuchHeader.sh") != std::string::npos),
                Wide(outcome.status.error().ToString()).c_str());
        }

        // A main .sco that cannot be written is in the report, and the run
        // does not succeed.
        TEST_METHOD(MainObjectFileWriteError_IsInTheReport)
        {
            PrepareStaleFixtures(_game);
            std::string mainSco = MainObjectFile();
            Assert::IsTrue(SetFileAttributes(mainSco.c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: a read-only Main.sco");
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 960 }, DecompileRunOptions(), results);
            AssertOk(report);
            Assert::IsFalse(report->globalRenames.empty(), L"setup: 960 names a global");
            Assert::IsFalse(report->mainObjectFile.has_value(), Wide(DescribeRun(*report)).c_str());
            Assert::IsTrue(report->mainObjectFile.error().code == sci::ErrorCode::Io, Wide(report->mainObjectFile.error().ToString()).c_str());
            Assert::IsFalse(report->Succeeded());
        }

        // A .sco file that cannot be written fails its script.
        TEST_METHOD(ObjectFileWriteError_FailsTheScript)
        {
            _game.Make(TemplateSci0);
            std::string objectFile = _game.Src("door.sco");
            // A .sco is written only when its bytes change.
            WriteReadOnlyFile(objectFile, "not a .sco file");
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 974 }, DecompileRunOptions(), results);
            AssertOk(report);
            const DecompileOutcome *door = OutcomeOf(*report, 974);
            Assert::IsTrue((door != nullptr) && !door->status && (door->status.error().code == sci::ErrorCode::Io), Wide(DescribeRun(*report)).c_str());
        }

        // A .sc file that pass 1 and pass 2 cannot write fails its script.
        TEST_METHOD(Pass2WriteError_FailsTheScript)
        {
            PrepareStaleFixtures(_game);
            Assert::IsTrue(SetFileAttributes(_game.Src("BatchGlobalsA.sc").c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results);
            AssertOk(report);
            const DecompileOutcome *first = OutcomeOf(*report, 959);
            Assert::IsTrue((first != nullptr) && !first->status && (first->status.error().code == sci::ErrorCode::Io), Wide(DescribeRun(*report)).c_str());
            Assert::IsTrue(OutcomeOf(*report, 960)->status.has_value(), Wide(DescribeRun(*report)).c_str());
        }

        // With an output, main's .sco does not change, also when the run
        // names a global, and the output gets the source of pass 2.
        TEST_METHOD(Output_KeepsMainSco_AndGetsThePass2Source)
        {
            PrepareStaleFixtures(_game);
            std::string mainSco = MainObjectFile();
            std::string before = ReadFileText(mainSco);
            GameSession &session = _game.Open();
            RunResults results;
            CollectSources output;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results, &output);
            AssertSucceeded(report);
            Assert::IsFalse(report->globalRenames.empty(), L"setup: the run names a global");
            Assert::AreEqual(before, ReadFileText(mainSco), L"main's .sco does not change with an output");
            std::string first;
            for (const auto &source : output.sources)
            {
                if (source.first == 959)
                {
                    first = source.second;
                }
            }
            Assert::IsFalse(first.empty(), L"the output gets 959");
            Assert::IsFalse(ContainsIdentifier(first, "global5"), Wide(first).c_str());
        }

        // With an output, a batch that throws after pass 1 (at the naming)
        // gives no source: the source of pass 1 is not the source of the
        // run. Each script gets the error of the batch.
        TEST_METHOD(Output_BatchThrowsAfterPass1_NoSource)
        {
            PrepareStaleFixtures(_game);
            GameSession &session = _game.Open();
            RunResults results;
            results.throwOnMessage = "Naming variables";
            CollectSources output;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results, &output);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            Assert::IsFalse(report->batch.has_value(), Wide("setup: the batch threw:\n" + facts).c_str());
            Assert::IsTrue(output.sources.empty(), Wide("no source of pass 1:\n" + facts).c_str());
            Assert::AreEqual((size_t)2, report->scripts.size(), Wide(facts).c_str());
            for (const DecompileOutcome &outcome : report->scripts)
            {
                Assert::IsTrue(!outcome.status.has_value() && (outcome.status.error().message == report->batch.error().message), Wide(facts).c_str());
            }
        }

        // A game.ini that cannot be written fails the run.
        TEST_METHOD(GameIniWriteError_FailsTheRun)
        {
            _game.Make(TemplateSci0);
            std::string ini = _game.Path("game.ini");
            Assert::IsTrue(WritePrivateProfileString("Script", "n974", nullptr, ini.c_str()) != 0);
            Assert::IsTrue(SetFileAttributes(ini.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 974 }, DecompileRunOptions(), results);
            AssertOk(report);
            Assert::IsTrue(OutcomeOf(*report, 974)->status.has_value(), Wide(DescribeRun(*report)).c_str());
            Assert::IsFalse(report->gameIni.has_value(), Wide(DescribeRun(*report)).c_str());
            Assert::IsFalse(report->Succeeded());
        }

        // The default name nNNN needs no entry in game.ini, and Create makes
        // no game.ini for it.
        TEST_METHOD(GameIni_DefaultNameNeedsNoEntry)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0, true);
            AssertOk(WriteScriptNamesToGameIni(session.Helper(), { { 5, "n005" } }, GameIniNames::Create));
            Assert::IsFalse(fs::exists(_game.Path("game.ini")), L"no entry is needed, so no game.ini");
            AssertOk(WriteScriptNamesToGameIni(session.Helper(), { { 5, "n005" }, { 974, "Door" } }, GameIniNames::Create));
            Assert::AreEqual(std::string("Door"), IniEntry("n974"));
            Assert::AreEqual(std::string(), IniEntry("n005"));
        }

        // When game.ini has no [Script] entry, the run writes the name of
        // every script, as the Decompile dialog does before its first run, so
        // the dialog then names the other scripts too.
        TEST_METHOD(GameIni_NoScriptSection_GetsEveryName)
        {
            _game.Make(TemplateSci0);
            Assert::IsTrue(WritePrivateProfileString("Script", nullptr, nullptr, _game.Path("game.ini").c_str()) != 0, L"setup: no [Script] section");
            GameSession &session = _game.Open();
            RunResults results;
            AssertSucceeded(RunDecompile(session, { 974 }, DecompileRunOptions(), results));
            Assert::AreEqual(std::string("door"), IniEntry("n974"));
            Assert::AreEqual(std::string("Main"), IniEntry("n000"));
            Assert::AreEqual(std::string("MENUBAR"), Upper(IniEntry("n997")), L"a script that the run did not write");
        }

        // An output run on a game with no src folder uses the Decompiler.ini
        // of the data folder, so it gives the source that a file run gives.
        // The default settings give other names: global3 for gNewSpeed, 133
        // for #check, param1 for pEvent.
        TEST_METHOD(Output_SameSourceAsAFileRun)
        {
            std::set<uint16_t> scripts = { 0, 974, 994, 999 };
            std::map<uint16_t, std::string> fromOutput;
            {
                GameSession &session = _game.OpenCopy(TemplateSci0, true);
                RunResults results;
                CollectSources output;
                auto report = RunDecompile(session, scripts, DecompileRunOptions(), results, &output);
                AssertSucceeded(report);
                Assert::IsTrue(report->warnings.empty(), Wide(DescribeRun(*report)).c_str());
                for (const auto &source : output.sources)
                {
                    fromOutput[source.first] = source.second;
                }
            }
            GameSession &session = _game.OpenCopy(TemplateSci0, true);
            RunResults results;
            AssertSucceeded(RunDecompile(session, scripts, DecompileRunOptions(), results));
            for (uint16_t number : scripts)
            {
                std::string file = WithoutCR(ReadFileText(session.Helper().GetScriptFileName((WORD)number)));
                Assert::IsFalse(file.empty());
                Assert::IsTrue(file == WithoutCR(fromOutput[number]), Wide("script " + std::to_string(number)).c_str());
            }
        }

        // An abort in pass 2 leaves a written script with an old global name;
        // the report lists it as stale.
        TEST_METHOD(Abort_InPass2_TheScriptIsStale)
        {
            PrepareStaleFixtures(_game);
            GameSession &session = _game.Open();
            RunResults results;
            // Two starts in pass 1; the abort comes at the first start of pass 2.
            results.abortAfter = 2;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results);
            AssertOk(report);
            Assert::IsTrue(report->cancelled, Wide(DescribeRun(*report)).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(959), Wide(DescribeRun(*report)).c_str());
            Assert::IsTrue(ContainsIdentifier(ReadFileText(session.Helper().GetScriptFileName((WORD)959)), "global5"), L"setup: 959 still has the old name");
        }

        // An abort that comes just after the write of a script (here, at its
        // "Generated" message) leaves the script written: it counts, with its
        // renames, main's .sco gets the names (960's file uses them), and the
        // stale check after the abort finds 959.
        TEST_METHOD(Abort_JustAfterAWrite_TheScriptCounts)
        {
            PrepareStaleFixtures(_game);
            GameSession &session = _game.Open();
            RunResults results;
            results.abortOnMessage = "Generated " + session.Helper().GetScriptFileName((WORD)960);
            auto report = RunDecompile(session, { 960 }, DecompileRunOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            Assert::IsTrue(report->cancelled && results.abortSeen, Wide("setup: the abort came after the write:\n" + facts).c_str());
            const DecompileOutcome *second = OutcomeOf(*report, 960);
            Assert::IsTrue((second != nullptr) && second->status.has_value(), Wide(facts).c_str());
            Assert::IsFalse(report->globalRenames.empty(), Wide("the renames of 960: " + facts).c_str());
            GlobalCompiledScriptLookups lookups;
            AssertOk(lookups.TryLoad(session.Helper()));
            std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
            Assert::IsNotNull(mainSCO.get());
            Assert::AreNotEqual(std::string("global5"), mainSCO->GetVariables()[5].GetName(), L"main's .sco has the name of the global");
            Assert::AreEqual((size_t)1, report->stale.count(959), Wide(facts).c_str());
        }

        // In pass 2, an abort that comes just after the second write of a
        // script leaves it written again with the new names: it is not stale.
        TEST_METHOD(Abort_JustAfterASecondWrite_TheScriptIsNotStale)
        {
            PrepareStaleFixtures(_game);
            GameSession &session = _game.Open();
            RunResults results;
            // 959 is written in pass 1 (with global5), and again in pass 2.
            results.abortOnMessage = "Generated " + session.Helper().GetScriptFileName((WORD)959);
            results.abortOnMatch = 2;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            Assert::IsTrue(report->cancelled && results.abortSeen, Wide("setup: the abort came after the second write:\n" + facts).c_str());
            Assert::IsFalse(ContainsIdentifier(ReadFileText(session.Helper().GetScriptFileName((WORD)959)), "global5"), L"setup: 959 has the new name");
            Assert::AreEqual((size_t)0, report->stale.count(959), Wide(facts).c_str());
        }

        // With no [Script] entry in game.ini, the run writes every name; a
        // script that a reset renamed and that no group wrote keeps its name
        // from before the reset, which its files have: a cancelled reset of
        // 979 writes n979=Controls, so the next session finds Controls.sc.
        TEST_METHOD(ResetNames_AScriptThatNoGroupWrote_KeepsItsNameInGameIni)
        {
            _game.Make(TemplateSci0);
            Assert::IsTrue(WritePrivateProfileString("Script", nullptr, nullptr, _game.Path("game.ini").c_str()) != 0, L"setup: no [Script] section");
            GameSession &session = _game.Open();
            RunResults results;
            // The abort comes in the first script.
            results.abortAfter = 0;
            auto report = RunDecompile(session, { 979 }, ResetOptions(GameIniNames::Update), results);
            AssertOk(report);
            Assert::IsTrue(report->cancelled && !OutcomeOf(*report, 979)->status.has_value(), Wide("setup: 979 is not written:\n" + DescribeRun(*report)).c_str());
            Assert::AreEqual(std::string("Controls"), IniEntry("n979"), L"the name from before the reset");
            Assert::AreEqual(0, _stricmp("MenuBar", IniEntry("n997").c_str()), L"the other names");
        }

        // A later group that stopped before a script keeps the outcome of the
        // earlier group. Here group 1 fails 965 (its .sc is read-only), and
        // the abort comes in group 3 before 965: 965 keeps its Io error, not
        // Cancelled, so plan section 8 gives exit code 9, not 7.
        TEST_METHOD(Abort_AFailureOfAnEarlierGroupStays)
        {
            PrepareEarlierGroupFixtures();
            Assert::IsTrue(SetFileAttributesA(_game.Src("StaleFirst.sc").c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession &session = _game.Open();
            RunResults results;
            results.abortOnMessage = "Decompiling script 0";
            auto report = RunDecompile(session, { 965 }, UpdateStaleOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            Assert::IsTrue(report->cancelled && results.abortSeen, Wide("setup: the abort came in group 3:\n" + facts).c_str());
            const DecompileOutcome *stale = OutcomeOf(*report, 965);
            Assert::IsTrue((stale != nullptr) && !stale->status.has_value(), Wide(facts).c_str());
            Assert::IsTrue(stale->status.error().code == sci::ErrorCode::Io, Wide(facts).c_str());
        }

        // A batch that throws in a later group keeps the written outcome of an
        // earlier group; the scripts of the group that it did not reach get
        // its error.
        TEST_METHOD(BatchThrowsInALaterGroup_TheWrittenOutcomeStays)
        {
            PrepareEarlierGroupFixtures();
            GameSession &session = _game.Open();
            RunResults results;
            // Group 3 starts with script 0.
            results.throwOnMessage = "Decompiling script 0";
            auto report = RunDecompile(session, { 965 }, UpdateStaleOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            const DecompileOutcome *main = OutcomeOf(*report, 0);
            Assert::IsTrue((main != nullptr) && !main->status.has_value(), Wide("setup: the batch threw in group 3:\n" + facts).c_str());
            const DecompileOutcome *first = OutcomeOf(*report, 965);
            Assert::IsTrue((first != nullptr) && first->status.has_value(), Wide(facts).c_str());
            // The report keeps the error of the batch (it does not succeed),
            // and the stale check of an abort runs: 965's file still uses
            // global3, which group 2 named.
            Assert::IsFalse(report->batch.has_value(), Wide(facts).c_str());
            Assert::IsFalse(report->Succeeded(), Wide(facts).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(965), Wide(facts).c_str());
        }

        // After an abort, a run with an output makes no stale list; and
        // game.ini keeps the name of a script that group 1 wrote when a later
        // group fails it.
        TEST_METHOD(Abort_Output_NoStaleList_AndAWrittenNameStays)
        {
            PrepareStaleFixtures(_game);
            {
                GameSession &session = _game.Open();
                RunResults results;
                // Two starts in pass 1; the abort comes at the first start of pass 2.
                results.abortAfter = 2;
                CollectSources sources;
                auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results, &sources);
                Assert::IsTrue(report.has_value() && report->cancelled, report ? Wide(DescribeRun(*report)).c_str() : L"no report");
                Assert::IsTrue(report->stale.empty(), Wide("an output writes nothing, so nothing is stale:\n" + DescribeRun(*report)).c_str());
                // And no source goes to the output, also not a source of
                // pass 1.
                Assert::IsTrue(sources.sources.empty(), L"no source after an abort");
            }

            PrepareEarlierGroupFixtures();
            Assert::IsTrue(WritePrivateProfileString("Script", "n965", nullptr, _game.Path("game.ini").c_str()) != 0, L"setup: no n965 entry");
            std::string first = _game.Src("StaleFirst.sc");
            GameSession &session = _game.Open();
            RunResults results;
            // Group 2 is 966: then 965's file becomes read-only, and group 3 fails it.
            results.onMessage = [first](const std::string &message)
            {
                if (message.rfind("Decompiling script 966", 0) == 0)
                {
                    SetFileAttributesA(first.c_str(), FILE_ATTRIBUTE_READONLY);
                }
            };
            auto report = RunDecompile(session, { 965 }, UpdateStaleOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            const DecompileOutcome *stale = OutcomeOf(*report, 965);
            Assert::IsTrue((stale != nullptr) && !stale->status.has_value(), Wide("setup: group 3 fails 965:\n" + facts).c_str());
            Assert::AreEqual(std::string("StaleFirst"), IniEntry("n965"), Wide("the name that group 1 wrote:\n" + facts).c_str());
        }

        // A script that fails after its naming keeps the global names that it
        // found (the namer puts them into main's .sco). Here 960 fails at its
        // "Generated" message: main's .sco gets the new name of global5, and
        // pass 2 writes 957 with it.
        TEST_METHOD(AScriptFailsAfterItsNaming_TheNamesStay)
        {
            PrepareStaleFixtures(_game);
            WriteFileText(_game.Src("Stale957.sc"), ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 957)\r\n(include sci.sh)\r\n(use Main)\r\n\r\n(public\r\n\tstale957 0\r\n)\r\n\r\n(procedure (stale957)\r\n\t(return global5)\r\n)\r\n");
            Assert::AreEqual(std::string(), CompileErrorsOf(Compile(_game.Open(), { ScriptAt(_game.Src("Stale957.sc"), 957) })), L"setup: 957 compiles");
            _game.CloseSessions();
            GameSession &session = _game.Open();
            RunResults results;
            results.throwOnMessage = "Generated " + session.Helper().GetScriptFileName((WORD)960);
            auto report = RunDecompile(session, { 957, 960 }, DecompileRunOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            const DecompileOutcome *second = OutcomeOf(*report, 960);
            Assert::IsTrue((second != nullptr) && !second->status.has_value(), Wide("setup: 960 fails:\n" + facts).c_str());
            Assert::IsFalse(report->globalRenames.empty(), Wide("the names of 960: " + facts).c_str());
            GlobalCompiledScriptLookups lookups;
            AssertOk(lookups.TryLoad(session.Helper()));
            std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
            Assert::IsNotNull(mainSCO.get());
            std::string name = mainSCO->GetVariables()[5].GetName();
            Assert::AreNotEqual(std::string("global5"), name, L"main's .sco has the name");
            std::string first = ReadFileText(session.Helper().GetScriptFileName((WORD)957));
            Assert::IsTrue(ContainsIdentifier(first, name) && !ContainsIdentifier(first, "global5"), Wide(name + "\n" + first).c_str());
        }

        // With staleAfterAbort false (the Decompile dialog), an abort makes no
        // stale check.
        TEST_METHOD(Abort_StaleAfterAbortFalse_NoStaleCheck)
        {
            PrepareEarlierGroupFixtures();
            GameSession &session = _game.Open();
            RunResults results;
            results.abortOnMessage = "Decompiling script 0";
            DecompileRunOptions options = UpdateStaleOptions();
            options.staleAfterAbort = false;
            auto report = RunDecompile(session, { 965 }, options, results);
            Assert::IsTrue(report.has_value() && report->cancelled, report ? Wide(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual((size_t)0, report->stale.count(965), Wide(DescribeRun(*report)).c_str());
        }

        // In a name conflict, the script that the name map finds first (the
        // lowest number) owns the title of the file, so the --derived column
        // gives it the plain name: 979 gets MenuBar, not MenuBar_979. The
        // spelling of the file does not change that (MENUBAR.sc).
        TEST_METHOD(ResetNames_ANameConflict_TheLowestNumberOwnsTheTitle)
        {
            for (const char *title : { "menubar", "MENUBAR" })
            {
                _game.Make(TemplateSci0);
                Assert::IsTrue(WritePrivateProfileString("Script", "n979", "MenuBar", _game.Path("game.ini").c_str()) != 0);
                fs::rename(_game.Src("menubar.sc"), _game.Src(std::string(title) + ".sc"));
                fs::rename(_game.Src("menubar.sco"), _game.Src(std::string(title) + ".sco"));
                GameSession &session = _game.Open();
                auto rows = ListScripts(session, true);
                std::string derived;
                for (const ScriptRow &row : ValueOf(rows))
                {
                    if (row.number == 979)
                    {
                        derived = row.derivedName;
                    }
                }
                Assert::AreEqual(std::string("MenuBar"), derived, Wide(title).c_str());
            }
        }

        // A batch that throws outside the exception boundary of a script,
        // after a naming, still writes main's .sco with the names: 960 uses
        // the new name of global5. The throw comes at the message of main's
        // .sco, or in pass 2 before 959. The report does not succeed (the
        // batch part of Succeeded), also when every script was written.
        TEST_METHOD(BatchThrowsAfterANaming_MainScoGetsTheNames)
        {
            std::string failures;
            for (const char *message : { "Updating global variables in script 0", "Decompiling script 959 again" })
            {
                PrepareStaleFixtures(_game);
                GameSession &session = _game.Open();
                RunResults results;
                results.throwOnMessage = message;
                auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results);
                AssertOk(report);
                std::string facts = std::string(message) + ":\n" + DescribeRun(*report);
                Assert::IsFalse(report->batch.has_value(), Wide("setup: the batch threw:\n" + facts).c_str());
                Assert::AreEqual((size_t)0, report->FailedCount(), Wide(facts).c_str());
                Assert::IsFalse(report->Succeeded(), Wide(facts).c_str());
                Assert::IsTrue(report->mainObjectFile.has_value(), Wide(facts).c_str());
                GlobalCompiledScriptLookups lookups;
                AssertOk(lookups.TryLoad(session.Helper()));
                std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
                Assert::IsNotNull(mainSCO.get());
                std::string name = mainSCO->GetVariables()[5].GetName();
                if ((name == "global5") || !ContainsIdentifier(ReadFileText(session.Helper().GetScriptFileName((WORD)960)), name))
                {
                    failures += std::string(message) + ": main's .sco has " + name + ", and 960 does not use it\n";
                }
                _game.CloseSessions();
            }
            Assert::AreEqual(std::string(), failures, Wide(failures).c_str());
        }

        // report.files lists main's .sco when it changes and the line of
        // script 0 does not name it. Here the .sco of script 0 keeps its
        // bytes in pass 1, 994 names globals, the batch throws before the
        // second decompile of script 0, and main's .sco then gets the names.
        TEST_METHOD(MainObjectFile_ListedWhenTheLineOfScript0DoesNotNameIt)
        {
            _game.Make(TemplateSci0);
            for (const auto &entry : fs::directory_iterator(_game.Src("")))
            {
                if (_stricmp(entry.path().extension().string().c_str(), ".sco") == 0)
                {
                    fs::remove(entry.path());
                }
            }
            {
                RunResults results;
                AssertSucceeded(RunDecompile(_game.Open(), { 0 }, DecompileRunOptions(), results));
                _game.CloseSessions();
            }
            std::string mainSco = MainObjectFile();
            std::string before = ReadFileText(mainSco);
            GameSession &session = _game.Open();
            RunResults results;
            results.throwOnMessage = "Decompiling script 0 again";
            auto report = RunDecompile(session, { 0, 994 }, DecompileRunOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report) + "files:\n" + JoinLines(report->files);
            const DecompileOutcome *main = OutcomeOf(*report, 0);
            Assert::IsTrue(!report->batch.has_value() && (main != nullptr) && main->status.has_value() && !main->objectFileChanged, Wide("setup: the batch threw after pass 1:\n" + facts).c_str());
            Assert::AreNotEqual(before, ReadFileText(mainSco), Wide("setup: main's .sco changed:\n" + facts).c_str());
            Assert::IsTrue(Upper(JoinLines(report->files)).find("\\MAIN.SCO\n") != std::string::npos, Wide(facts).c_str());
        }

        // The stale check after a group also reads the scripts of the group
        // that failed: 960 names global5 and fails (its .sc is read-only),
        // and its old file still uses global5.
        TEST_METHOD(StaleCheck_AFailedScriptOfTheGroup)
        {
            PrepareStaleFixtures(_game);
            Assert::IsTrue(SetFileAttributesA(_game.Src("BatchGlobalsB.sc").c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession &session = _game.Open();
            RunResults results;
            auto report = RunDecompile(session, { 960 }, DecompileRunOptions(), results);
            AssertOk(report);
            std::string facts = DescribeRun(*report);
            const DecompileOutcome *second = OutcomeOf(*report, 960);
            Assert::IsTrue((second != nullptr) && !second->status.has_value(), Wide("setup: 960 fails:\n" + facts).c_str());
            Assert::IsFalse(report->globalRenames.empty(), Wide("setup: 960 names global5:\n" + facts).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(960), Wide(facts).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(959), Wide(facts).c_str());
        }
    };
}
