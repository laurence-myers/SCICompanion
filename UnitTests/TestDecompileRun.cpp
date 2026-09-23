#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
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
#include "Helper.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // Runs a test with no AppState, as the command line does.
    struct NoAppStateForRun
    {
        AppState *saved;
        NoAppStateForRun() : saved(appState) { appState = nullptr; }
        ~NoAppStateForRun() { appState = saved; }
    };

    std::wstring WideForRun(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::string ReadAllText(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    void WriteAllText(const std::string &path, const std::string &text)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << text;
    }

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
        }
        bool IsAborted() override { return (abortAfter >= 0) && (started > abortAfter); }
        void InformStats(bool functionSuccessful, int byteCount) override {}
        void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &renames) override {}

        std::vector<std::string> problems;
        int abortAfter = -1;
        int started = 0;
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

    // The files of a folder, and their text.
    std::map<std::string, std::string> FilesOf(const std::string &folder)
    {
        std::map<std::string, std::string> files;
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && (it != end); it.increment(ec))
        {
            if (it->is_regular_file())
            {
                files[it->path().filename().string()] = ReadAllText(it->path().string());
            }
        }
        return files;
    }
}

namespace UnitTests
{
    // Plan step S4. Before it, the decompile of the GUI was in the Decompile
    // dialog: it named the scripts in game.ini (it created game.ini), copied
    // the Decompiler folder with the shell, dropped a script that did not
    // load with no message, and had no status for each script.
    TEST_CLASS(TestDecompileRun)
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

        // A copy of the template, with no game.ini and no src folder when
        // bare is set.
        void CopyTemplate(const char *templateFolder, bool bare)
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

        void Open(GameSession &session)
        {
            Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
        }

        std::string GameFile(const std::string &name) const
        {
            return (fs::path(_copyFolder) / name).string();
        }

        std::string IniEntry(const std::string &key) const
        {
            char value[200] = {};
            GetPrivateProfileString("Script", key.c_str(), "", value, (DWORD)ARRAYSIZE(value), GameFile("game.ini").c_str());
            return value;
        }

        static SessionOptions TestSessionOptions()
        {
            SessionOptions options;
            options.dataFolder = GetTestModuleDirectory();
            return options;
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

        // The SCI1.1 template with the two scripts of TestDecompileBatch, as
        // scripts 959 and 960 (the template has 950 and 951): 959 uses
        // global5 (unnamed), and 960 names it. Slot 5 of Main.sco is renamed
        // to its standard name first.
        void PrepareStaleFixtures()
        {
            CopyTemplate("\\TemplateGame\\SCI1.1", false);
            {
                GameSession session(TestSessionOptions());
                Open(session);
                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
                std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
                Assert::IsNotNull(mainSCO.get(), L"setup: Main.sco");
                mainSCO->GetVariables()[5].SetName("global5");
                Assert::IsTrue(SaveSCOFile(session.Helper(), *mainSCO).has_value());
            }
            for (const auto &fixture : std::vector<std::pair<std::string, std::string>>{ { "BatchGlobalsA", "950" }, { "BatchGlobalsB", "951" } })
            {
                std::string text = ReadAllText(GetTestFileDirectory("Decompile\\SCI1.1") + "\\" + fixture.first + ".sc");
                std::string declared = "(script# " + fixture.second + ")";
                size_t at = text.find(declared);
                Assert::IsTrue(at != std::string::npos, L"setup: the fixture declares its number");
                text.replace(at, declared.size(), (fixture.second == "950") ? "(script# 959)" : "(script# 960)");
                WriteAllText(GameFile("src\\" + fixture.first + ".sc"), text);
            }
            GameSession session(TestSessionOptions());
            Open(session);
            ScriptId a(GameFile("src\\BatchGlobalsA.sc").c_str());
            a.SetResourceNumber(959);
            ScriptId b(GameFile("src\\BatchGlobalsB.sc").c_str());
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

        // With no game.ini, the names come from the compiled scripts, the run
        // writes src\<name>.sc, and nothing creates game.ini (plan section
        // 3.4). The Decompile dialog wrote the names into game.ini, and so
        // created it.
        TEST_METHOD(NoGameIni_NothingCreatesIt)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", true);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 974 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value(), WideForRun(report ? std::string() : report.error().ToString()).c_str());
            Assert::IsTrue(report->Succeeded(), WideForRun(DescribeRun(*report)).c_str());
            Assert::AreEqual(std::string("Door"), report->scripts[0].name, L"the derived name");
            Assert::IsTrue(fs::exists(GameFile("src\\Door.sc")) && fs::exists(GameFile("src\\Door.sco")));
            Assert::IsTrue(ReadAllText(GameFile("src\\Door.sc")).find("(script# 974)") != std::string::npos);
            Assert::IsFalse(fs::exists(GameFile("game.ini")), L"nothing creates game.ini");
            Assert::IsTrue(fs::exists(GameFile("src\\Decompiler.ini")), L"the src folder gets Decompiler.ini");
            Assert::IsTrue(report->stats.functions > 0, L"the statistics count the functions");
        }

        // --game-ini: Update adds an entry for a written script that game.ini
        // does not name; Create also creates game.ini; None writes nothing.
        TEST_METHOD(GameIni_UpdateCreateNone)
        {
            NoAppStateForRun noAppState;
            for (GameIniNames mode : { GameIniNames::Update, GameIniNames::None })
            {
                CopyTemplate("\\TemplateGame\\SCI0", false);
                // Script 974 keeps its name from src\door.sc (rule 2), with no
                // game.ini entry.
                Assert::IsTrue(WritePrivateProfileString("Script", "n974", nullptr, GameFile("game.ini").c_str()) != 0);
                Assert::AreEqual(std::string(), IniEntry("n974"), L"setup: no entry");
                std::string mainEntry = IniEntry("n000");
                GameSession session(TestSessionOptions());
                Open(session);
                RunResults results;
                DecompileRunOptions options;
                options.gameIni = mode;
                auto report = RunDecompile(session, { 974 }, options, results);
                Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
                Assert::AreEqual((mode == GameIniNames::Update) ? std::string("door") : std::string(), IniEntry("n974"));
                Assert::AreEqual(mainEntry, IniEntry("n000"), L"an entry that game.ini has does not change");
            }

            CopyTemplate("\\TemplateGame\\SCI0", true);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            DecompileRunOptions options;
            options.gameIni = GameIniNames::Create;
            auto report = RunDecompile(session, { 974 }, options, results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::IsTrue(fs::exists(GameFile("game.ini")), L"Create creates game.ini");
            Assert::AreEqual(std::string("Door"), IniEntry("n974"));
        }

        // --reset-names: every script gets its derived name, also one that
        // game.ini names; the old files keep their names, and a warning lists
        // them. Update writes the new name into game.ini, so the GUI finds the
        // new file.
        TEST_METHOD(ResetNames_TakesTheDerivedNames)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            Assert::IsTrue(WritePrivateProfileString("Script", "n974", "OldDoor", GameFile("game.ini").c_str()) != 0);
            fs::rename(GameFile("src\\door.sc"), GameFile("src\\OldDoor.sc"));
            fs::rename(GameFile("src\\door.sco"), GameFile("src\\OldDoor.sco"));
            std::string oldText = ReadAllText(GameFile("src\\OldDoor.sc"));
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            DecompileRunOptions options;
            options.names = NameAssignment::All;
            auto report = RunDecompile(session, { 974 }, options, results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual(std::string("Door"), report->scripts[0].name);
            Assert::IsTrue(fs::exists(GameFile("src\\Door.sc")), L"the new file has the derived name");
            Assert::AreEqual(oldText, ReadAllText(GameFile("src\\OldDoor.sc")), L"the old file keeps its name and its text");
            std::string warnings;
            for (const std::string &warning : report->warnings)
            {
                warnings += warning + "\n";
            }
            Assert::IsTrue((warnings.find("OldDoor.sc") != std::string::npos) && (warnings.find("OldDoor.sco") != std::string::npos), WideForRun(warnings).c_str());
            Assert::AreEqual(std::string("Door"), IniEntry("n974"), L"game.ini has the new name");
        }

        // The src folder: made when it does not exist; Decompiler.ini and the
        // other files of the Decompiler folder are copied once, and a file of
        // the game is never overwritten.
        TEST_METHOD(PrepareDecompileFolder_CopiesOnceAndNeverOverwrites)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            fs::path decompiler = fs::path(_copyFolder) / "DecompilerData";
            fs::create_directories(decompiler);
            WriteAllText((decompiler / "Decompiler.ini").string(), "[first]\n");
            WriteAllText((decompiler / "game.sh").string(), "; not the game's\n");
            WriteAllText((decompiler / "extra.txt").string(), "extra\n");
            std::string gameHeader = ReadAllText(GameFile("src\\game.sh"));
            GameSession session(TestSessionOptions());
            Open(session);

            Assert::IsTrue(PrepareDecompileFolder(session.Helper(), decompiler.string()).has_value());
            Assert::AreEqual(std::string("[first]\n"), ReadAllText(GameFile("src\\Decompiler.ini")));
            Assert::AreEqual(std::string("extra\n"), ReadAllText(GameFile("src\\extra.txt")));
            Assert::AreEqual(gameHeader, ReadAllText(GameFile("src\\game.sh")), L"the game's game.sh is not overwritten");

            WriteAllText((decompiler / "Decompiler.ini").string(), "[second]\n");
            fs::remove(GameFile("src\\extra.txt"));
            Assert::IsTrue(PrepareDecompileFolder(session.Helper(), decompiler.string()).has_value());
            Assert::AreEqual(std::string("[first]\n"), ReadAllText(GameFile("src\\Decompiler.ini")), L"copied once");
            Assert::IsFalse(fs::exists(GameFile("src\\extra.txt")), L"nothing is copied while Decompiler.ini exists");

            fs::remove_all(GameFile("src"));
            Assert::IsTrue(PrepareDecompileFolder(session.Helper(), (fs::path(_copyFolder) / "NoSuchFolder").string()).has_value());
            Assert::IsTrue(fs::is_directory(GameFile("src")), L"the src folder is made");
        }

        // An output (--stdout) gets the source, and the run writes nothing: no
        // src folder, no .sc, no .sco, no game.ini.
        TEST_METHOD(Output_WritesNoFile)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", true);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            CollectSources output;
            DecompileRunOptions options;
            options.gameIni = GameIniNames::Create;
            auto report = RunDecompile(session, { 974 }, options, results, &output);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual((size_t)1, output.sources.size());
            Assert::IsTrue(output.sources[0].first == 974);
            Assert::IsTrue(output.sources[0].second.find("(script# 974)") != std::string::npos, WideForRun(output.sources[0].second).c_str());
            Assert::IsFalse(fs::exists(GameFile("src")), L"no src folder");
            Assert::IsFalse(fs::exists(GameFile("game.ini")), L"no game.ini, also with Create");

            // With a src folder, its files do not change.
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::map<std::string, std::string> before = FilesOf(GameFile("src"));
            GameSession withSource(TestSessionOptions());
            Open(withSource);
            CollectSources output2;
            auto report2 = RunDecompile(withSource, { 0, 974 }, DecompileRunOptions(), results, &output2);
            Assert::IsTrue(report2.has_value() && report2->Succeeded(), report2 ? WideForRun(DescribeRun(*report2)).c_str() : L"no report");
            Assert::AreEqual((size_t)2, output2.sources.size());
            Assert::IsTrue(before == FilesOf(GameFile("src")), L"no file of src changes");
        }

        // A script that does not load gets its error in the report, and the
        // others are written. Before, the batch dropped it with no message.
        TEST_METHOD(FailedScript_HasItsStatus_TheOthersAreWritten)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            {
                std::ofstream patch(GameFile("script.905"), std::ios::binary | std::ios::trunc);
                const char bytes[] = { (char)(0x80 | (uint8_t)ResourceType::Script), 0, 5, 0, 1 };
                patch.write(bytes, sizeof(bytes));
            }
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 905, 974 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value(), WideForRun(report ? std::string() : report.error().ToString()).c_str());
            const DecompileOutcome *bad = OutcomeOf(*report, 905);
            const DecompileOutcome *good = OutcomeOf(*report, 974);
            Assert::IsTrue((bad != nullptr) && (good != nullptr), WideForRun(DescribeRun(*report)).c_str());
            Assert::IsFalse(bad->status.has_value());
            Assert::IsTrue(bad->status.error().code != sci::ErrorCode::Cancelled, WideForRun(DescribeRun(*report)).c_str());
            Assert::IsTrue(good->status.has_value(), WideForRun(DescribeRun(*report)).c_str());
            Assert::AreEqual((size_t)1, report->FailedCount());
            bool reported = false;
            for (const std::string &problem : results.problems)
            {
                reported = reported || (problem.find("Script 905") != std::string::npos);
            }
            Assert::IsTrue(reported, L"the failure is in the messages too");
        }

        // A .sc file that cannot be written fails its script. Before, the
        // batch gave only a message, and the script counted as written.
        TEST_METHOD(WriteError_FailsTheScript)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::string source = GameFile("src\\door.sc");
            Assert::IsTrue(SetFileAttributes(source.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 974, 983 }, DecompileRunOptions(), results);
            // Writable again, so that the clean-up can remove the copy.
            SetFileAttributes(source.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value(), WideForRun(report ? std::string() : report.error().ToString()).c_str());
            const DecompileOutcome *door = OutcomeOf(*report, 974);
            Assert::IsTrue((door != nullptr) && !door->status, WideForRun(DescribeRun(*report)).c_str());
            Assert::IsTrue(door->status.error().code != sci::ErrorCode::Cancelled, WideForRun(DescribeRun(*report)).c_str());
            Assert::IsTrue(OutcomeOf(*report, 983)->status.has_value(), WideForRun(DescribeRun(*report)).c_str());
        }

        // An abort stops the run: the report is cancelled, and a script that
        // the run did not finish is Cancelled.
        TEST_METHOD(Abort_CancelsTheRest)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            results.abortAfter = 1;
            auto report = RunDecompile(session, { 974, 983 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(report->cancelled);
            Assert::IsTrue(OutcomeOf(*report, 974)->status.has_value(), WideForRun(DescribeRun(*report)).c_str());
            Assert::IsTrue(!OutcomeOf(*report, 983)->status && (OutcomeOf(*report, 983)->status.error().code == sci::ErrorCode::Cancelled), WideForRun(DescribeRun(*report)).c_str());
        }

        // S4b, script sco (plan section 4.6): with no .sco file in src, the
        // .sco files made from the sources and the compiled scripts let a
        // compile of every script start. Without them, each (use ...) fails.
        TEST_METHOD(ObjectFiles_LetACompileStartFromTheSources)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            for (const auto &entry : fs::directory_iterator(GameFile("src")))
            {
                if (_stricmp(entry.path().extension().string().c_str(), ".sco") == 0)
                {
                    fs::remove(entry.path());
                }
            }
            GameSession session(TestSessionOptions());
            Open(session);
            auto selection = SelectAllScripts(session, SelectorMode::Sco);
            Assert::IsTrue(selection.has_value() && (selection->scripts.size() > 20), L"setup: the scripts with a source");
            auto outcomes = GenerateObjectFiles(session, selection->scripts);
            Assert::IsTrue(outcomes.has_value(), WideForRun(outcomes ? std::string() : outcomes.error().ToString()).c_str());
            for (const ObjectFileOutcome &outcome : *outcomes)
            {
                Assert::IsTrue(outcome.status.has_value() && outcome.skipped.empty(),
                    WideForRun(outcome.name + ": " + (outcome.status ? outcome.skipped : outcome.status.error().ToString())).c_str());
            }
            Assert::IsTrue(fs::exists(GameFile("src\\Main.sco")) && fs::exists(GameFile("src\\door.sco")));

            GameSession compileSession(TestSessionOptions());
            Open(compileSession);
            auto compileSelection = SelectAllScripts(compileSession, SelectorMode::Compile);
            Assert::IsTrue(compileSelection.has_value());
            std::atomic<bool> abort(false);
            ICompileEvents events;
            auto report = CompileScripts(compileSession, compileSelection->scripts, CompileOptions(), abort, events);
            Assert::IsTrue(report.has_value(), WideForRun(report ? std::string() : report.error().ToString()).c_str());
            std::string errors;
            for (const ScriptOutcome &outcome : report->scripts)
            {
                for (const CompileResult &result : outcome.diagnostics)
                {
                    if (result.IsError())
                    {
                        errors += result.GetMessage() + "\n";
                    }
                }
            }
            Assert::IsTrue(report->Succeeded(), WideForRun(errors).c_str());
        }

        // S4b: a script with no source file, or with no compiled script, is
        // skipped; a source with a syntax error fails its script and writes
        // no .sco; the others are written.
        TEST_METHOD(ObjectFiles_SkipsAndFails)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            WriteAllText(GameFile("src\\S4Extra.sc"), "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n");
            std::string doorSco = ReadAllText(GameFile("src\\door.sco"));
            std::string door = ReadAllText(GameFile("src\\door.sc"));
            WriteAllText(GameFile("src\\door.sc"), door + "\n(procedure (s4Broken)\n    (= )\n)\n");
            fs::remove(GameFile("src\\wander.sco"));
            GameSession session(TestSessionOptions());
            Open(session);
            std::vector<ScriptId> scripts;
            for (const auto &entry : std::vector<std::pair<std::string, uint16_t>>{ { "src\\S4NoSuchFile.sc", 905 }, { "src\\S4Extra.sc", 906 }, { "src\\door.sc", 974 }, { "src\\wander.sc", 983 } })
            {
                ScriptId script(GameFile(entry.first).c_str());
                script.SetResourceNumber(entry.second);
                scripts.push_back(script);
            }
            auto outcomes = GenerateObjectFiles(session, scripts);
            Assert::IsTrue(outcomes.has_value() && (outcomes->size() == 4));
            const std::vector<ObjectFileOutcome> &o = *outcomes;
            Assert::IsTrue(o[0].status.has_value() && (o[0].skipped.find("no source file") != std::string::npos), WideForRun(o[0].skipped).c_str());
            Assert::IsTrue(o[1].status.has_value() && (o[1].skipped.find("no compiled script") != std::string::npos), WideForRun(o[1].skipped).c_str());
            Assert::IsFalse(o[2].status.has_value(), L"a syntax error fails the script");
            Assert::IsTrue(o[2].status.error().code == sci::ErrorCode::Compile, WideForRun(o[2].status.error().ToString()).c_str());
            Assert::IsFalse(o[2].diagnostics.empty(), L"the syntax error is in the diagnostics");
            Assert::AreEqual(doorSco, ReadAllText(GameFile("src\\door.sco")), L"no .sco is written for a script that failed");
            Assert::IsTrue(o[3].status.has_value() && o[3].skipped.empty(), WideForRun(o[3].status ? o[3].skipped : o[3].status.error().ToString()).c_str());
            Assert::IsTrue(fs::exists(GameFile("src\\wander.sco")));
        }

        // A run on some of the scripts reports the scripts that still use a
        // renamed global by its old name; with updateStale, it decompiles
        // them too, and again until no script is stale.
        TEST_METHOD(StaleScripts_ReportedOrUpdated)
        {
            NoAppStateForRun noAppState;
            {
                PrepareStaleFixtures();
                GameSession session(TestSessionOptions());
                Open(session);
                RunResults results;
                auto report = RunDecompile(session, { 960 }, DecompileRunOptions(), results);
                Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
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
                Assert::IsTrue(report->stale.count(959) == 1, WideForRun("959 still uses global5: " + renames).c_str());
                Assert::AreEqual((size_t)1, report->scripts.size(), L"without updateStale, 959 is only reported");
            }

            PrepareStaleFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            DecompileRunOptions options;
            options.updateStale = true;
            auto report = RunDecompile(session, { 960 }, options, results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::IsTrue(report->stale.empty(), L"no script is stale at the end");
            Assert::IsNotNull(OutcomeOf(*report, 959), WideForRun(DescribeRun(*report)).c_str());
            std::set<uint16_t> attempted;
            for (const DecompileOutcome &outcome : report->scripts)
            {
                attempted.insert(outcome.number);
            }
            std::set<uint16_t> others;
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
            for (CompiledScript *compiled : lookups.GetGlobalClassTable().GetAllScripts())
            {
                if (attempted.count(compiled->GetScriptNumber()) == 0)
                {
                    others.insert(compiled->GetScriptNumber());
                }
            }
            Assert::IsTrue(FindScriptsReferencingGlobals(session.Helper(), others, report->globalRenames).empty(), L"the run stops when no script is stale");
        }
    };
}
