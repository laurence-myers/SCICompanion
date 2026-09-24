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
#include "Stream.h"
#include "Helper.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
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
            if (!abortOnMessage.empty() && (message.rfind(abortOnMessage, 0) == 0) && (++abortMatches >= abortOnMatch))
            {
                abortSeen = true;
            }
            if (onMessage)
            {
                onMessage(message);
            }
            if (!throwOnMessage.empty() && (message.rfind(throwOnMessage, 0) == 0) && (++throwMatches >= throwOnMatch))
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
        // Throw from AddResult when a message starts with this text, at its
        // throwOnMatch-th message. At a message outside the exception boundary
        // of a script ("Decompiling script N"), the batch throws; at one
        // inside it ("Generated ..."), the script fails.
        std::string throwOnMessage;
        int throwOnMatch = 1;
        int throwMatches = 0;
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

    std::string UpperName(std::string text)
    {
        for (char &ch : text)
        {
            ch = (char)toupper((unsigned char)ch);
        }
        return text;
    }

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
                files[it->path().filename().string()] = ReadAllText(it->path().string());
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

        // The SCI1.1 template with two scripts (965 and 966 are free there):
        // 965 has (if global3 (= global5 gEgo)), and 966 has (= global3
        // global5). Slots 3 and 5 of Main.sco get their standard names first.
        // A run of 965 with updateStale names global5 in group 1; group 2
        // (966) names global3, which 965 uses; group 3 is 0 and 965.
        void PrepareEarlierGroupFixtures()
        {
            CopyTemplate("\\TemplateGame\\SCI1.1", false);
            {
                GameSession session(TestSessionOptions());
                Open(session);
                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
                std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
                Assert::IsNotNull(mainSCO.get(), L"setup: Main.sco");
                mainSCO->GetVariables()[3].SetName("global3");
                mainSCO->GetVariables()[5].SetName("global5");
                Assert::IsTrue(SaveSCOFile(session.Helper(), *mainSCO).has_value());
            }
            WriteAllText(GameFile("src\\StaleFirst.sc"), ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 965)\r\n(include sci.sh)\r\n(use Main)\r\n\r\n(public\r\n\tstaleFirst 0\r\n)\r\n\r\n(procedure (staleFirst)\r\n\t(if global3\r\n\t\t(= global5 gEgo)\r\n\t)\r\n)\r\n");
            WriteAllText(GameFile("src\\StaleSecond.sc"), ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 966)\r\n(include sci.sh)\r\n(use Main)\r\n\r\n(public\r\n\tstaleSecond 0\r\n)\r\n\r\n(procedure (staleSecond)\r\n\t(= global3 global5)\r\n)\r\n");
            GameSession session(TestSessionOptions());
            Open(session);
            ScriptId a(GameFile("src\\StaleFirst.sc").c_str());
            a.SetResourceNumber(965);
            ScriptId b(GameFile("src\\StaleSecond.sc").c_str());
            b.SetResourceNumber(966);
            std::atomic<bool> abort(false);
            ICompileEvents events;
            Assert::AreEqual(std::string(), CompileErrorsOf(CompileScripts(session, { a, b }, CompileOptions(), abort, events)), L"setup: the fixtures compile");
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        // With no game.ini, the names come from the compiled scripts, the run
        // writes src\<name>.sc, and nothing creates game.ini (plan section
        // 3.4).
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

        // --reset-names: each script of the run gets its derived name, also
        // one that game.ini names; the old files keep their names, and a
        // warning lists them. Update writes the new name into game.ini, so
        // the GUI finds the new file.
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
        // others are written.
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

        // A .sc file that cannot be written fails its script: the script
        // does not count as written.
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

        // script sco (plan section 4.6): with no .sco file in src, the .sco
        // files made from the sources and the compiled scripts have what the
        // compiler's .sco files have, and they let a compile of every script
        // start; on both templates. As the compiler does, a class gets its
        // name in the source, not its name string ("Blk" for Block), and a
        // source gets the locals of its .shp include (110.shp).
        TEST_METHOD(ObjectFiles_MatchTheCompiler_BothTemplates)
        {
            NoAppStateForRun noAppState;
            for (const char *templateFolder : { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" })
            {
                CopyTemplate(templateFolder, false);
                std::atomic<bool> abort(false);
                ICompileEvents events;
                {
                    // The .sco files of the compiler.
                    GameSession session(TestSessionOptions());
                    Open(session);
                    auto selection = SelectAllScripts(session, SelectorMode::Compile);
                    Assert::IsTrue(selection.has_value());
                    CompileOptions options;
                    options.passes = 3;
                    auto report = CompileScripts(session, selection->scripts, options, abort, events);
                    Assert::AreEqual(std::string(), CompileErrorsOf(report), L"setup: the template compiles");
                }
                std::map<std::string, std::string> compiler;
                for (const auto &entry : fs::directory_iterator(GameFile("src")))
                {
                    if (_stricmp(entry.path().extension().string().c_str(), ".sco") == 0)
                    {
                        compiler[UpperName(entry.path().filename().string())] = ReadAllText(entry.path().string());
                        fs::remove(entry.path());
                    }
                }
                Assert::IsTrue(compiler.size() > 20, L"setup: the .sco files of the compiler");

                GameSession session(TestSessionOptions());
                Open(session);
                auto selection = SelectAllScripts(session, SelectorMode::Sco);
                Assert::IsTrue(selection.has_value() && (selection->scripts.size() > 20), L"setup: the scripts with a source");
                auto outcomes = GenerateObjectFiles(session, selection->scripts);
                Assert::IsTrue(outcomes.has_value(), WideForRun(outcomes ? std::string() : outcomes.error().ToString()).c_str());
                // No diagnostic: the compile above made each export table from
                // its public block. (The shipped SCI1.1 template's Main and
                // DebugHandler export slots that their sources do not list;
                // TestCli::Sco_BothTemplates sees those warnings.)
                for (const ObjectFileOutcome &outcome : *outcomes)
                {
                    std::string warnings;
                    for (const CompileResult &diagnostic : outcome.diagnostics)
                    {
                        warnings += diagnostic.GetMessage() + "\n";
                    }
                    Assert::IsTrue(outcome.status.has_value() && outcome.skipped.empty() && warnings.empty(),
                        WideForRun(outcome.name + ": " + (outcome.status ? outcome.skipped : outcome.status.error().ToString()) + "\n" + warnings).c_str());
                }
                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
                std::string differences;
                for (const auto &file : compiler)
                {
                    std::string made = ReadAllText(GameFile("src\\" + file.first));
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
                Assert::AreEqual(std::string(), differences, WideForRun(templateFolder).c_str());

                GameSession compileSession(TestSessionOptions());
                Open(compileSession);
                auto compileSelection = SelectAllScripts(compileSession, SelectorMode::Compile);
                Assert::IsTrue(compileSelection.has_value());
                auto report = CompileScripts(compileSession, compileSelection->scripts, CompileOptions(), abort, events);
                Assert::AreEqual(std::string(), CompileErrorsOf(report), WideForRun(std::string("a compile from the made .sco files: ") + templateFolder).c_str());
            }
        }

        // A source whose classes differ in number from the compiled script
        // gets the names of the compiled script, and a warning.
        TEST_METHOD(ObjectFiles_OtherClassCount_Warns)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            WriteAllText(GameFile("src\\door.sc"), ReadAllText(GameFile("src\\door.sc")) + "\n(class S4Extra of Obj\n)\n");
            GameSession session(TestSessionOptions());
            Open(session);
            ScriptId door(GameFile("src\\door.sc").c_str());
            door.SetResourceNumber(974);
            auto outcomes = GenerateObjectFiles(session, { door });
            Assert::IsTrue(outcomes.has_value() && (outcomes->size() == 1));
            const ObjectFileOutcome &outcome = (*outcomes)[0];
            Assert::IsTrue(outcome.status.has_value(), WideForRun(outcome.status ? std::string() : outcome.status.error().ToString()).c_str());
            bool warned = false;
            for (const CompileResult &diagnostic : outcome.diagnostics)
            {
                warned = warned || ((diagnostic.GetMessage().find("classes") != std::string::npos) && !diagnostic.IsError());
            }
            Assert::IsTrue(warned, L"a warning for the class count");
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
            std::unique_ptr<CSCOFile> made = GetExistingSCOFromScriptNumber(session.Helper(), 974, lookups.GetSelectorTable());
            Assert::IsTrue(made && (made->GetObjects().size() == 1) && (made->GetObjects()[0].GetName() == "Door"), L"the names of the compiled script");
        }

        // A script with no source file, or with no compiled script, is
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

        // A reset name is never the file title of another script: a reset of
        // script 979 (its first class is MenuBar) names it "MenuBar_979", so
        // the run does not write over menubar.sc, the source of script 997.
        TEST_METHOD(ResetNames_NeverTakeTheFileOfAnotherScript)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::string menuBar = ReadAllText(GameFile("src\\menubar.sc"));
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            DecompileRunOptions options;
            options.names = NameAssignment::All;
            options.gameIni = GameIniNames::None;
            auto report = RunDecompile(session, { 979 }, options, results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual(std::string("MenuBar_979"), session.Helper().GetScriptTitle(979), L"the suffix of the naming rule");
            Assert::AreEqual(std::string("MenuBar"), session.Helper().GetScriptTitle(997), L"997 keeps its name");
            Assert::AreEqual(menuBar, ReadAllText(GameFile("src\\menubar.sc")), L"the source of 997 does not change");
            Assert::IsTrue(ReadAllText(GameFile("src\\MenuBar_979.sc")).find("(script# 979)") != std::string::npos);
            std::string warnings;
            for (const std::string &warning : report->warnings)
            {
                warnings += warning + "\n";
            }
            Assert::IsTrue(warnings.find("Controls.sc keeps its old name: script 979 is now MenuBar_979") != std::string::npos, WideForRun(warnings).c_str());
            // It also goes to the results, when it is found.
            bool inResults = false;
            for (const std::string &problem : results.problems)
            {
                inResults = inResults || (problem.find("Controls.sc keeps its old name") != std::string::npos);
            }
            Assert::IsTrue(inResults, L"the warning goes to the results");
        }

        // A dry run writes nothing, and its report lists the files that a run
        // writes besides the scripts' own: the decompiler files of src and
        // game.ini. A reset says "would keep".
        TEST_METHOD(DryRun_ListsTheOtherFilesAndWritesNothing)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::map<std::string, uintmax_t> before;
            for (const auto &entry : fs::recursive_directory_iterator(_copyFolder))
            {
                before[entry.path().string()] = entry.is_regular_file() ? entry.file_size() : 0;
            }
            std::string gameIni = ReadAllText(GameFile("game.ini"));
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            DecompileRunOptions options;
            options.names = NameAssignment::All;
            options.dryRun = true;
            auto report = RunDecompile(session, { 979 }, options, results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            std::map<std::string, uintmax_t> after;
            for (const auto &entry : fs::recursive_directory_iterator(_copyFolder))
            {
                after[entry.path().string()] = entry.is_regular_file() ? entry.file_size() : 0;
            }
            Assert::IsTrue(before == after, L"a dry run writes no file");
            Assert::AreEqual(gameIni, ReadAllText(GameFile("game.ini")), L"nor game.ini");
            std::string files;
            for (const std::string &file : report->files)
            {
                files += file + "\n";
            }
            Assert::IsTrue(files.find("\\src\\Decompiler.ini\n") != std::string::npos, WideForRun(files).c_str());
            Assert::IsTrue(files.find("\\game.ini\n") != std::string::npos, WideForRun("n979=MenuBar_979: " + files).c_str());
            std::string warnings;
            for (const std::string &warning : report->warnings)
            {
                warnings += warning + "\n";
            }
            Assert::IsTrue(warnings.find("Controls.sc would keep its old name: script 979 would be MenuBar_979") != std::string::npos, WideForRun(warnings).c_str());
        }

        // A reset name is never the title of the source of a script that the
        // game has not compiled, alone or in a conflict: script 974 gets
        // "Door_974", so the run does not write over Door.sc.
        TEST_METHOD(ResetNames_KeepTheFileOfAnUncompiledScript)
        {
            NoAppStateForRun noAppState;
            for (bool conflict : { false, true })
            {
                CopyTemplate("\\TemplateGame\\SCI0", false);
                Assert::IsTrue(WritePrivateProfileString("Script", "n974", "OldDoor", GameFile("game.ini").c_str()) != 0);
                fs::rename(GameFile("src\\door.sc"), GameFile("src\\OldDoor.sc"));
                fs::rename(GameFile("src\\door.sco"), GameFile("src\\OldDoor.sco"));
                const std::string uncompiled = "(script# 961)\r\n";
                WriteAllText(GameFile("src\\Door.sc"), uncompiled);
                if (conflict)
                {
                    WriteAllText(GameFile("src\\DoorCopy.sc"), uncompiled);
                }
                GameSession session(TestSessionOptions());
                Open(session);
                RunResults results;
                DecompileRunOptions options;
                options.names = NameAssignment::All;
                options.gameIni = GameIniNames::None;
                auto report = RunDecompile(session, { 974 }, options, results);
                Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
                Assert::AreEqual(std::string("Door_974"), session.Helper().GetScriptTitle(974), conflict ? L"a file in a conflict" : L"a file of an uncompiled script");
                Assert::AreEqual(uncompiled, ReadAllText(GameFile("src\\Door.sc")), L"Door.sc does not change");
            }
        }

        // A reset of some scripts resets only them. The other scripts keep
        // their names, also in memory, so the sources agree with game.ini:
        // Door.sc gets (use OldCycle), as game.ini has n992=OldCycle, and it
        // compiles.
        TEST_METHOD(ResetNames_OnlyTheChosenScripts)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            Assert::IsTrue(WritePrivateProfileString("Script", "n992", "OldCycle", GameFile("game.ini").c_str()) != 0);
            fs::rename(GameFile("src\\cycle.sc"), GameFile("src\\OldCycle.sc"));
            fs::rename(GameFile("src\\cycle.sco"), GameFile("src\\OldCycle.sco"));
            {
                GameSession session(TestSessionOptions());
                Open(session);
                RunResults results;
                DecompileRunOptions options;
                options.names = NameAssignment::All;
                auto report = RunDecompile(session, { 974 }, options, results);
                Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
                Assert::AreEqual(std::string("OldCycle"), session.Helper().GetScriptTitle(992), L"992 is not reset");
            }
            std::string door = ReadAllText(GameFile("src\\door.sc"));
            Assert::IsTrue(door.find("(use OldCycle)") != std::string::npos, WideForRun(door.substr(0, 400)).c_str());
            Assert::AreEqual(std::string("OldCycle"), IniEntry("n992"));

            GameSession session(TestSessionOptions());
            Open(session);
            ScriptId doorScript(GameFile("src\\door.sc").c_str());
            doorScript.SetResourceNumber(974);
            std::atomic<bool> abort(false);
            ICompileEvents events;
            auto compiled = CompileScripts(session, { doorScript }, CompileOptions(), abort, events);
            Assert::AreEqual(std::string(), CompileErrorsOf(compiled), L"the reset source compiles");
        }

        // With updateStale, a later group that names a global makes a script
        // of an earlier group stale, and the run decompiles it again: 965
        // uses global3, which group 2 names, and after the run 965 compiles.
        TEST_METHOD(UpdateStale_AlsoAScriptOfAnEarlierGroup)
        {
            NoAppStateForRun noAppState;
            PrepareEarlierGroupFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            DecompileRunOptions options;
            options.updateStale = true;
            auto report = RunDecompile(session, { 965 }, options, results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            std::string renames;
            for (const auto &rename : report->globalRenames)
            {
                renames += rename.first + "->" + rename.second + " ";
            }
            Assert::IsTrue(renames.find("global3->") != std::string::npos, WideForRun("setup: a later group names global3: " + renames).c_str());
            std::string first = ReadAllText(session.Helper().GetScriptFileName((WORD)965));
            Assert::IsFalse(ContainsIdentifier(first, "global3") || ContainsIdentifier(first, "global5"), WideForRun(first).c_str());
            size_t outcomes965 = 0;
            for (const DecompileOutcome &outcome : report->scripts)
            {
                outcomes965 += (outcome.number == 965) ? 1 : 0;
            }
            Assert::AreEqual((size_t)1, outcomes965, L"one outcome for a script that two groups decompiled");

            GameSession next(TestSessionOptions());
            Open(next);
            ScriptId a(session.Helper().GetScriptFileName((WORD)965).c_str());
            a.SetResourceNumber(965);
            std::atomic<bool> abort(false);
            ICompileEvents events;
            Assert::AreEqual(std::string(), CompileErrorsOf(CompileScripts(next, { a }, CompileOptions(), abort, events)), L"965 compiles after the run");
        }

        // An abort before a later group reaches a script that an earlier
        // group wrote keeps its written outcome, game.ini gets its name, and
        // the stale list has it (its file still uses a global that a later
        // group named).
        TEST_METHOD(Abort_KeepsTheScriptsThatAGroupWrote)
        {
            NoAppStateForRun noAppState;
            PrepareEarlierGroupFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            // Group 1 is 965, group 2 is 966 (it names global3), and group 3
            // starts with script 0: the abort comes there.
            results.abortOnMessage = "Decompiling script 0";
            DecompileRunOptions options;
            options.updateStale = true;
            auto report = RunDecompile(session, { 965 }, options, results);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(report->cancelled, WideForRun("setup: the abort came:\n" + DescribeRun(*report)).c_str());
            const DecompileOutcome *first = OutcomeOf(*report, 965);
            Assert::IsTrue((first != nullptr) && first->status.has_value(), WideForRun(DescribeRun(*report)).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(965), WideForRun(DescribeRun(*report)).c_str());
            Assert::AreEqual(std::string("StaleFirst"), IniEntry("n965"), L"game.ini gets the name of the written script");
        }

        // A reset never gives two scripts one name. A chosen script that gets
        // no derived name (978 has procedures only, or it cannot be read)
        // keeps its name, and no other chosen script takes it: 974 gets
        // "Door_974", and the two scripts write different files.
        TEST_METHOD(ResetNames_TwoScriptsNeverShareAName)
        {
            NoAppStateForRun noAppState;
            for (bool unreadable : { false, true })
            {
                CopyTemplate("\\TemplateGame\\SCI0", false);
                Assert::IsTrue(WritePrivateProfileString("Script", "n974", "OldDoor", GameFile("game.ini").c_str()) != 0);
                Assert::IsTrue(WritePrivateProfileString("Script", "n978", "Door", GameFile("game.ini").c_str()) != 0);
                fs::rename(GameFile("src\\door.sc"), GameFile("src\\OldDoor.sc"));
                fs::rename(GameFile("src\\door.sco"), GameFile("src\\OldDoor.sco"));
                if (unreadable)
                {
                    std::ofstream patch(GameFile("script.978"), std::ios::binary | std::ios::trunc);
                    const char bytes[] = { (char)(0x80 | (uint8_t)ResourceType::Script), 0, 5, 0, 1 };
                    patch.write(bytes, sizeof(bytes));
                }
                GameSession session(TestSessionOptions());
                Open(session);
                RunResults results;
                DecompileRunOptions options;
                options.names = NameAssignment::All;
                options.gameIni = GameIniNames::None;
                auto report = RunDecompile(session, { 974, 978 }, options, results);
                Assert::IsTrue(report.has_value(), WideForRun(report ? std::string() : report.error().ToString()).c_str());
                Assert::AreEqual(std::string("Door"), session.Helper().GetScriptTitle(978), unreadable ? L"978 cannot be read and keeps its name" : L"978 has no derived name and keeps its name");
                Assert::AreEqual(std::string("Door_974"), session.Helper().GetScriptTitle(974), L"974 does not take the name of 978");
                Assert::IsTrue(ReadAllText(GameFile("src\\Door_974.sc")).find("(script# 974)") != std::string::npos, WideForRun(DescribeRun(*report)).c_str());
            }
        }

        // A reset of two scripts where one has the file of the name that the
        // other derives. The title of a file belongs to its script: 997 keeps
        // menubar.sc, and 979 gets the suffix.
        TEST_METHOD(ResetNames_TwoChosenScripts_OneKeepsItsFile)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::string menuBar = ReadAllText(GameFile("src\\menubar.sc"));
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            DecompileRunOptions options;
            options.names = NameAssignment::All;
            options.gameIni = GameIniNames::None;
            auto report = RunDecompile(session, { 979, 997 }, options, results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual(std::string("MenuBar_979"), session.Helper().GetScriptTitle(979));
            Assert::AreEqual(std::string("TheMenuBar"), session.Helper().GetScriptTitle(997));
            Assert::AreEqual(menuBar, ReadAllText(GameFile("src\\menubar.sc")), L"the old file of 997 does not change");
        }

        // With no [Script] entry in game.ini, the names of every script go
        // into game.ini, but not the names of a conflict (the GUI does not
        // check game.ini for one). Here door.sco has the objects of Wander
        // (983), so "door" is the name of 974 and of 983.
        TEST_METHOD(GameIni_EveryName_NotAConflict)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            Assert::IsTrue(WritePrivateProfileString("Script", nullptr, nullptr, GameFile("game.ini").c_str()) != 0, L"setup: no [Script] section");
            fs::copy_file(GameFile("src\\wander.sco"), GameFile("src\\door.sco"), fs::copy_options::overwrite_existing);
            fs::remove(GameFile("src\\wander.sco"));
            fs::remove(GameFile("src\\wander.sc"));
            GameSession session(TestSessionOptions());
            Open(session);
            Assert::IsFalse(session.Helper().ScriptNames->ConflictsOf(974).empty(), L"setup: a conflict");
            RunResults results;
            auto report = RunDecompile(session, { 985 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual(std::string("Main"), IniEntry("n000"), L"the other names");
            Assert::AreEqual(std::string(), IniEntry("n974"), L"no name of the conflict");
            Assert::AreEqual(std::string(), IniEntry("n983"), L"no name of the conflict");
        }

        // An include that is not there names itself in the error of script
        // sco.
        TEST_METHOD(ObjectFiles_AMissingInclude_NamesIt)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::string text = ReadAllText(GameFile("src\\door.sc"));
            text.insert(text.find('\n') + 1, "(include s2NoSuchHeader.sh)\r\n");
            WriteAllText(GameFile("src\\door.sc"), text);
            GameSession session(TestSessionOptions());
            Open(session);
            ScriptId door(GameFile("src\\door.sc").c_str());
            door.SetResourceNumber(974);
            auto outcomes = GenerateObjectFiles(session, { door });
            Assert::IsTrue(outcomes.has_value() && (outcomes->size() == 1));
            const ObjectFileOutcome &outcome = (*outcomes)[0];
            Assert::IsFalse(outcome.status.has_value());
            Assert::IsTrue((outcome.status.error().code == sci::ErrorCode::NotFound) && (outcome.status.error().ToString().find("s2NoSuchHeader.sh") != std::string::npos),
                WideForRun(outcome.status.error().ToString()).c_str());
        }

        // A main .sco that cannot be written is in the report, and the run
        // does not succeed.
        TEST_METHOD(MainObjectFileWriteError_IsInTheReport)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            std::string mainSco;
            {
                GameSession probe(TestSessionOptions());
                Open(probe);
                mainSco = probe.Helper().GetScriptObjectFileName((WORD)0);
            }
            Assert::IsTrue(SetFileAttributes(mainSco.c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: a read-only Main.sco");
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 960 }, DecompileRunOptions(), results);
            SetFileAttributes(mainSco.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            Assert::IsFalse(report->globalRenames.empty(), L"setup: 960 names a global");
            Assert::IsFalse(report->mainObjectFile.has_value(), WideForRun(DescribeRun(*report)).c_str());
            Assert::IsTrue(report->mainObjectFile.error().code == sci::ErrorCode::Io, WideForRun(report->mainObjectFile.error().ToString()).c_str());
            Assert::IsFalse(report->Succeeded());
        }

        // A .sco file that cannot be written fails its script.
        TEST_METHOD(ObjectFileWriteError_FailsTheScript)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::string objectFile = GameFile("src\\door.sco");
            // A .sco is written only when its bytes change.
            WriteAllText(objectFile, "not a .sco file");
            Assert::IsTrue(SetFileAttributes(objectFile.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 974 }, DecompileRunOptions(), results);
            SetFileAttributes(objectFile.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            const DecompileOutcome *door = OutcomeOf(*report, 974);
            Assert::IsTrue((door != nullptr) && !door->status && (door->status.error().code == sci::ErrorCode::Io), WideForRun(DescribeRun(*report)).c_str());
        }

        // A .sc file that pass 1 and pass 2 cannot write fails its script.
        TEST_METHOD(Pass2WriteError_FailsTheScript)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            std::string source = GameFile("src\\BatchGlobalsA.sc");
            Assert::IsTrue(SetFileAttributes(source.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results);
            SetFileAttributes(source.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            const DecompileOutcome *first = OutcomeOf(*report, 959);
            Assert::IsTrue((first != nullptr) && !first->status && (first->status.error().code == sci::ErrorCode::Io), WideForRun(DescribeRun(*report)).c_str());
            Assert::IsTrue(OutcomeOf(*report, 960)->status.has_value(), WideForRun(DescribeRun(*report)).c_str());
        }

        // With an output, main's .sco does not change, also when the run
        // names a global, and the output gets the source of pass 2.
        TEST_METHOD(Output_KeepsMainSco_AndGetsThePass2Source)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            std::string mainSco;
            {
                GameSession probe(TestSessionOptions());
                Open(probe);
                mainSco = probe.Helper().GetScriptObjectFileName((WORD)0);
            }
            std::string before = ReadAllText(mainSco);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            CollectSources output;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results, &output);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::IsFalse(report->globalRenames.empty(), L"setup: the run names a global");
            Assert::AreEqual(before, ReadAllText(mainSco), L"main's .sco does not change with an output");
            std::string first;
            for (const auto &source : output.sources)
            {
                if (source.first == 959)
                {
                    first = source.second;
                }
            }
            Assert::IsFalse(first.empty(), L"the output gets 959");
            Assert::IsFalse(ContainsIdentifier(first, "global5"), WideForRun(first).c_str());
        }

        // A game.ini that cannot be written fails the run.
        TEST_METHOD(GameIniWriteError_FailsTheRun)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            std::string ini = GameFile("game.ini");
            Assert::IsTrue(WritePrivateProfileString("Script", "n974", nullptr, ini.c_str()) != 0);
            Assert::IsTrue(SetFileAttributes(ini.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 974 }, DecompileRunOptions(), results);
            SetFileAttributes(ini.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(OutcomeOf(*report, 974)->status.has_value(), WideForRun(DescribeRun(*report)).c_str());
            Assert::IsFalse(report->gameIni.has_value(), WideForRun(DescribeRun(*report)).c_str());
            Assert::IsFalse(report->Succeeded());
        }

        // The default name nNNN needs no entry in game.ini, and Create makes
        // no game.ini for it.
        TEST_METHOD(GameIni_DefaultNameNeedsNoEntry)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", true);
            GameSession session(TestSessionOptions());
            Open(session);
            Assert::IsTrue(WriteScriptNamesToGameIni(session.Helper(), { { 5, "n005" } }, GameIniNames::Create).has_value());
            Assert::IsFalse(fs::exists(GameFile("game.ini")), L"no entry is needed, so no game.ini");
            Assert::IsTrue(WriteScriptNamesToGameIni(session.Helper(), { { 5, "n005" }, { 974, "Door" } }, GameIniNames::Create).has_value());
            Assert::AreEqual(std::string("Door"), IniEntry("n974"));
            Assert::AreEqual(std::string(), IniEntry("n005"));
        }

        // When game.ini has no [Script] entry, the run writes the name of
        // every script, as the Decompile dialog does before its first run, so
        // the dialog then names the other scripts too.
        TEST_METHOD(GameIni_NoScriptSection_GetsEveryName)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            Assert::IsTrue(WritePrivateProfileString("Script", nullptr, nullptr, GameFile("game.ini").c_str()) != 0, L"setup: no [Script] section");
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, { 974 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual(std::string("door"), IniEntry("n974"));
            Assert::AreEqual(std::string("Main"), IniEntry("n000"));
            Assert::AreEqual(std::string("MENUBAR"), UpperName(IniEntry("n997")), L"a script that the run did not write");
        }

        // An output run on a game with no src folder uses the Decompiler.ini
        // of the data folder, so it gives the source that a file run gives.
        // The default settings give other names: global3 for gNewSpeed, 133
        // for #check, param1 for pEvent.
        TEST_METHOD(Output_SameSourceAsAFileRun)
        {
            NoAppStateForRun noAppState;
            std::set<uint16_t> scripts = { 0, 974, 994, 999 };
            std::map<uint16_t, std::string> fromOutput;
            CopyTemplate("\\TemplateGame\\SCI0", true);
            {
                GameSession session(TestSessionOptions());
                Open(session);
                RunResults results;
                CollectSources output;
                auto report = RunDecompile(session, scripts, DecompileRunOptions(), results, &output);
                Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
                Assert::IsTrue(report->warnings.empty(), WideForRun(DescribeRun(*report)).c_str());
                for (const auto &source : output.sources)
                {
                    fromOutput[source.first] = source.second;
                }
            }
            CopyTemplate("\\TemplateGame\\SCI0", true);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            auto report = RunDecompile(session, scripts, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            for (uint16_t number : scripts)
            {
                std::string file = WithoutCR(ReadAllText(session.Helper().GetScriptFileName((WORD)number)));
                Assert::IsFalse(file.empty());
                Assert::IsTrue(file == WithoutCR(fromOutput[number]), WideForRun("script " + std::to_string(number)).c_str());
            }
        }

        // An abort in pass 2 leaves a written script with an old global name;
        // the report lists it as stale.
        TEST_METHOD(Abort_InPass2_TheScriptIsStale)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            // Two starts in pass 1; the abort comes at the first start of pass 2.
            results.abortAfter = 2;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(report->cancelled, WideForRun(DescribeRun(*report)).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(959), WideForRun(DescribeRun(*report)).c_str());
            Assert::IsTrue(ContainsIdentifier(ReadAllText(session.Helper().GetScriptFileName((WORD)959)), "global5"), L"setup: 959 still has the old name");
        }

        // An abort that comes just after the write of a script (here, at its
        // "Generated" message) leaves the script written: it counts, with its
        // renames, main's .sco gets the names (960's file uses them), and the
        // stale check after the abort finds 959.
        TEST_METHOD(Abort_JustAfterAWrite_TheScriptCounts)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            results.abortOnMessage = "Generated " + session.Helper().GetScriptFileName((WORD)960);
            auto report = RunDecompile(session, { 960 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value());
            std::string facts = DescribeRun(*report);
            Assert::IsTrue(report->cancelled && results.abortSeen, WideForRun("setup: the abort came after the write:\n" + facts).c_str());
            const DecompileOutcome *second = OutcomeOf(*report, 960);
            Assert::IsTrue((second != nullptr) && second->status.has_value(), WideForRun(facts).c_str());
            Assert::IsFalse(report->globalRenames.empty(), WideForRun("the renames of 960: " + facts).c_str());
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
            std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
            Assert::IsNotNull(mainSCO.get());
            Assert::AreNotEqual(std::string("global5"), mainSCO->GetVariables()[5].GetName(), L"main's .sco has the name of the global");
            Assert::AreEqual((size_t)1, report->stale.count(959), WideForRun(facts).c_str());
        }

        // In pass 2, an abort that comes just after the second write of a
        // script leaves it written again with the new names: it is not stale.
        TEST_METHOD(Abort_JustAfterASecondWrite_TheScriptIsNotStale)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            // 959 is written in pass 1 (with global5), and again in pass 2.
            results.abortOnMessage = "Generated " + session.Helper().GetScriptFileName((WORD)959);
            results.abortOnMatch = 2;
            auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value());
            std::string facts = DescribeRun(*report);
            Assert::IsTrue(report->cancelled && results.abortSeen, WideForRun("setup: the abort came after the second write:\n" + facts).c_str());
            Assert::IsFalse(ContainsIdentifier(ReadAllText(session.Helper().GetScriptFileName((WORD)959)), "global5"), L"setup: 959 has the new name");
            Assert::AreEqual((size_t)0, report->stale.count(959), WideForRun(facts).c_str());
        }

        // With no [Script] entry in game.ini, the run writes every name; a
        // script that a reset renamed and that no group wrote keeps its name
        // from before the reset, which its files have: a cancelled reset of
        // 979 writes n979=Controls, so the next session finds Controls.sc.
        TEST_METHOD(ResetNames_AScriptThatNoGroupWrote_KeepsItsNameInGameIni)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            Assert::IsTrue(WritePrivateProfileString("Script", nullptr, nullptr, GameFile("game.ini").c_str()) != 0, L"setup: no [Script] section");
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            // The abort comes in the first script.
            results.abortAfter = 0;
            DecompileRunOptions options;
            options.names = NameAssignment::All;
            auto report = RunDecompile(session, { 979 }, options, results);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(report->cancelled && !OutcomeOf(*report, 979)->status.has_value(), WideForRun("setup: 979 is not written:\n" + DescribeRun(*report)).c_str());
            Assert::AreEqual(std::string("Controls"), IniEntry("n979"), L"the name from before the reset");
            Assert::AreEqual(0, _stricmp("MenuBar", IniEntry("n997").c_str()), L"the other names");
        }

        // A later group that stopped before a script keeps the outcome of the
        // earlier group. Here group 1 fails 965 (its .sc is read-only), and
        // the abort comes in group 3 before 965: 965 keeps its Io error, not
        // Cancelled, so plan section 8 gives exit code 9, not 7.
        TEST_METHOD(Abort_AFailureOfAnEarlierGroupStays)
        {
            NoAppStateForRun noAppState;
            PrepareEarlierGroupFixtures();
            std::string first = GameFile("src\\StaleFirst.sc");
            Assert::IsTrue(SetFileAttributesA(first.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            results.abortOnMessage = "Decompiling script 0";
            DecompileRunOptions options;
            options.updateStale = true;
            auto report = RunDecompile(session, { 965 }, options, results);
            // Writable again, so that the clean-up can remove the copy.
            SetFileAttributesA(first.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            std::string facts = DescribeRun(*report);
            Assert::IsTrue(report->cancelled && results.abortSeen, WideForRun("setup: the abort came in group 3:\n" + facts).c_str());
            const DecompileOutcome *stale = OutcomeOf(*report, 965);
            Assert::IsTrue((stale != nullptr) && !stale->status.has_value(), WideForRun(facts).c_str());
            Assert::IsTrue(stale->status.error().code == sci::ErrorCode::Io, WideForRun(facts).c_str());
        }

        // A batch that throws in a later group keeps the written outcome of an
        // earlier group; the scripts of the group that it did not reach get
        // its error.
        TEST_METHOD(BatchThrowsInALaterGroup_TheWrittenOutcomeStays)
        {
            NoAppStateForRun noAppState;
            PrepareEarlierGroupFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            // Group 3 starts with script 0.
            results.throwOnMessage = "Decompiling script 0";
            DecompileRunOptions options;
            options.updateStale = true;
            auto report = RunDecompile(session, { 965 }, options, results);
            Assert::IsTrue(report.has_value());
            std::string facts = DescribeRun(*report);
            const DecompileOutcome *main = OutcomeOf(*report, 0);
            Assert::IsTrue((main != nullptr) && !main->status.has_value(), WideForRun("setup: the batch threw in group 3:\n" + facts).c_str());
            const DecompileOutcome *first = OutcomeOf(*report, 965);
            Assert::IsTrue((first != nullptr) && first->status.has_value(), WideForRun(facts).c_str());
            // The report keeps the error of the batch (it does not succeed),
            // and the stale check of an abort runs: 965's file still uses
            // global3, which group 2 named.
            Assert::IsFalse(report->batch.has_value(), WideForRun(facts).c_str());
            Assert::IsFalse(report->Succeeded(), WideForRun(facts).c_str());
            Assert::AreEqual((size_t)1, report->stale.count(965), WideForRun(facts).c_str());
        }

        // After an abort, a run with an output makes no stale list; and
        // game.ini keeps the name of a script that group 1 wrote when a later
        // group fails it.
        TEST_METHOD(Abort_Output_NoStaleList_AndAWrittenNameStays)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            {
                GameSession session(TestSessionOptions());
                Open(session);
                RunResults results;
                // Two starts in pass 1; the abort comes at the first start of pass 2.
                results.abortAfter = 2;
                CollectSources sources;
                auto report = RunDecompile(session, { 959, 960 }, DecompileRunOptions(), results, &sources);
                Assert::IsTrue(report.has_value() && report->cancelled, report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
                Assert::IsTrue(report->stale.empty(), WideForRun("an output writes nothing, so nothing is stale:\n" + DescribeRun(*report)).c_str());
                // And no source goes to the output, also not a source of
                // pass 1.
                Assert::IsTrue(sources.sources.empty(), L"no source after an abort");
            }

            PrepareEarlierGroupFixtures();
            Assert::IsTrue(WritePrivateProfileString("Script", "n965", nullptr, GameFile("game.ini").c_str()) != 0, L"setup: no n965 entry");
            std::string first = GameFile("src\\StaleFirst.sc");
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            // Group 2 is 966: then 965's file becomes read-only, and group 3 fails it.
            results.onMessage = [first](const std::string &message)
            {
                if (message.rfind("Decompiling script 966", 0) == 0)
                {
                    SetFileAttributesA(first.c_str(), FILE_ATTRIBUTE_READONLY);
                }
            };
            DecompileRunOptions options;
            options.updateStale = true;
            auto report = RunDecompile(session, { 965 }, options, results);
            SetFileAttributesA(first.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            std::string facts = DescribeRun(*report);
            const DecompileOutcome *stale = OutcomeOf(*report, 965);
            Assert::IsTrue((stale != nullptr) && !stale->status.has_value(), WideForRun("setup: group 3 fails 965:\n" + facts).c_str());
            Assert::AreEqual(std::string("StaleFirst"), IniEntry("n965"), WideForRun("the name that group 1 wrote:\n" + facts).c_str());
        }

        // A script that fails after its naming keeps the global names that it
        // found (the namer puts them into main's .sco). Here 960 fails at its
        // "Generated" message: main's .sco gets the new name of global5, and
        // pass 2 writes 957 with it.
        TEST_METHOD(AScriptFailsAfterItsNaming_TheNamesStay)
        {
            NoAppStateForRun noAppState;
            PrepareStaleFixtures();
            WriteAllText(GameFile("src\\Stale957.sc"), ";;; Sierra Script 1.0 - (do not remove this comment)\r\n(script# 957)\r\n(include sci.sh)\r\n(use Main)\r\n\r\n(public\r\n\tstale957 0\r\n)\r\n\r\n(procedure (stale957)\r\n\t(return global5)\r\n)\r\n");
            {
                GameSession session(TestSessionOptions());
                Open(session);
                ScriptId script(GameFile("src\\Stale957.sc").c_str());
                script.SetResourceNumber(957);
                std::atomic<bool> abort(false);
                ICompileEvents events;
                Assert::AreEqual(std::string(), CompileErrorsOf(CompileScripts(session, { script }, CompileOptions(), abort, events)), L"setup: 957 compiles");
            }
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            results.throwOnMessage = "Generated " + session.Helper().GetScriptFileName((WORD)960);
            auto report = RunDecompile(session, { 957, 960 }, DecompileRunOptions(), results);
            Assert::IsTrue(report.has_value());
            std::string facts = DescribeRun(*report);
            const DecompileOutcome *second = OutcomeOf(*report, 960);
            Assert::IsTrue((second != nullptr) && !second->status.has_value(), WideForRun("setup: 960 fails:\n" + facts).c_str());
            Assert::IsFalse(report->globalRenames.empty(), WideForRun("the names of 960: " + facts).c_str());
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.TryLoad(session.Helper()).has_value());
            std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
            Assert::IsNotNull(mainSCO.get());
            std::string name = mainSCO->GetVariables()[5].GetName();
            Assert::AreNotEqual(std::string("global5"), name, L"main's .sco has the name");
            std::string first = ReadAllText(session.Helper().GetScriptFileName((WORD)957));
            Assert::IsTrue(ContainsIdentifier(first, name) && !ContainsIdentifier(first, "global5"), WideForRun(name + "\n" + first).c_str());
        }

        // With staleAfterAbort false (the Decompile dialog), an abort makes no
        // stale check.
        TEST_METHOD(Abort_StaleAfterAbortFalse_NoStaleCheck)
        {
            NoAppStateForRun noAppState;
            PrepareEarlierGroupFixtures();
            GameSession session(TestSessionOptions());
            Open(session);
            RunResults results;
            results.abortOnMessage = "Decompiling script 0";
            DecompileRunOptions options;
            options.updateStale = true;
            options.staleAfterAbort = false;
            auto report = RunDecompile(session, { 965 }, options, results);
            Assert::IsTrue(report.has_value() && report->cancelled, report ? WideForRun(DescribeRun(*report)).c_str() : L"no report");
            Assert::AreEqual((size_t)0, report->stale.count(965), WideForRun(DescribeRun(*report)).c_str());
        }

        // In a name conflict, the script that the name map finds first (the
        // lowest number) owns the title of the file, so the --derived column
        // gives it the plain name: 979 gets MenuBar, not MenuBar_979.
        TEST_METHOD(ResetNames_ANameConflict_TheLowestNumberOwnsTheTitle)
        {
            NoAppStateForRun noAppState;
            CopyTemplate("\\TemplateGame\\SCI0", false);
            Assert::IsTrue(WritePrivateProfileString("Script", "n979", "MenuBar", GameFile("game.ini").c_str()) != 0);
            GameSession session(TestSessionOptions());
            Open(session);
            auto rows = ListScripts(session, true);
            Assert::IsTrue(rows.has_value(), rows ? L"" : WideForRun(rows.error().ToString()).c_str());
            std::string derived;
            for (const ScriptRow &row : *rows)
            {
                if (row.number == 979)
                {
                    derived = row.derivedName;
                }
            }
            Assert::AreEqual(std::string("MenuBar"), derived);
        }
    };
}
