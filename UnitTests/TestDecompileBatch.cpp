#include "stdafx.h"
#include "CppUnitTest.h"
#include "Helper.h"
#include "DecompileHelper.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "CompiledScript.h"
#include "ScriptOMAll.h" // DecompilerConfig.h uses sci:: node types declared here
#include "DecompilerConfig.h"
#include "DecompileBatch.h"
#include "SCO.h"
#include "GameFolderHelper.h"
#include "ResourceContainer.h"
#include "DecompilerCore.h"
#include "DecompileScript.h"
#include "AutoDetectVariableNames.h"
#include "format.h"
#include <fstream>
#include <sstream>
#include <memory>
#include <set>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    static std::wstring ToW(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    static std::string ReadTextFile(const std::string &path)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        Assert::IsTrue(!!file, ToW("could not read " + path).c_str());
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    // The batch decompile (the Decompile dialog's path) decompiles every
    // script once, then names the globals across all of them, round after
    // round, and only then writes. Script by script, each against main's .sco
    // on disk, a global named by a later script left every earlier script with
    // the old name, and the whole game had to be decompiled again.
    TEST_CLASS(TestDecompileBatch)
    {
        std::string _gameFolder;

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            Assert::IsNull(appState, L"appState leaked from a prior test");
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // Copies and compiles the two fixtures. Before compiling, renames slot 5
        // of Main.sco (gCast in the template) to its standard name, global5, so
        // the fixtures can refer to it and the decompiler sees it as unnamed.
        // Slot 3 is already global3 (an unused slot in the template's Main).
        // The .sco is what the compiler resolves (use Main) globals against, and
        // what the decompiler reads global names from, so Main.sc is left alone.
        void PrepareBatchFixtures()
        {
            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            {
                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.Load(helper), L"lookups should load");
                std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(helper, 0, lookups.GetSelectorTable());
                Assert::IsNotNull(mainSCO.get(), L"the template game should have Main.sco");
                Assert::IsTrue(mainSCO->GetVariables().size() > 5, L"Main should have more than 5 globals");
                Assert::AreEqual(std::string("global3"), mainSCO->GetVariableName(3), L"slot 3 should be unnamed in the template");
                mainSCO->GetVariables()[5].SetName("global5");
                SaveSCOFile(helper, *mainSCO);
            }

            AddFixtureScript("BatchGlobalsA");
            AddFixtureScript("BatchGlobalsB");
            std::string error;
            Assert::IsTrue(CompileFixture(950, "BatchGlobalsA", &error), ToW("compile of BatchGlobalsA failed: " + error).c_str());
            Assert::IsTrue(CompileFixture(951, "BatchGlobalsB", &error), ToW("compile of BatchGlobalsB failed: " + error).c_str());
        }

        TEST_METHOD(ContainsIdentifier_WholeWordOnly)
        {
            Assert::IsTrue(ContainsIdentifier("(= global3 gEgo)", "global3"));
            Assert::IsTrue(ContainsIdentifier("global3", "global3"));
            Assert::IsTrue(ContainsIdentifier("[global3 2]", "global3"));
            Assert::IsTrue(ContainsIdentifier("global30 global3", "global3"), L"a later whole-word match counts");
            Assert::IsFalse(ContainsIdentifier("(= global39 gEgo)", "global3"), L"a prefix of a longer identifier is not a match");
            Assert::IsFalse(ContainsIdentifier("(= xglobal3 gEgo)", "global3"), L"a suffix of a longer identifier is not a match");
            Assert::IsFalse(ContainsIdentifier("(= my_global3 gEgo)", "global3"), L"an underscore joins identifiers");
            Assert::IsFalse(ContainsIdentifier("", "global3"));
            Assert::IsFalse(ContainsIdentifier("global3", ""));
        }

        // Script 950 assigns global3 from global5; script 951 assigns global5
        // from gEgo. In script order, 950 sees global5 with no name and can
        // name nothing. The batch names global5 from 951, goes round again,
        // and names global3 from 950's use of the new name. Before, that took
        // a second full decompile of every script.
        TEST_METHOD(Batch_NamesGlobalsAcrossScriptsToFixpoint)
        {
            _gameFolder = SetUpGameSCI11();
            PrepareBatchFixtures();

            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(helper), L"lookups should load");
            uint16_t dummy;
            lookups.GetSelectorTable().ReverseLookup("", dummy);
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

            TestDecompilerResults results;
            DecompileBatch batch(config.get(), lookups, helper, results);
            batch.Run({ 950, 951 });

            Assert::AreEqual(2, (int)batch.GetWrittenScripts().size(), L"both scripts should be written");
            Assert::AreEqual(0, results.fallbacks, L"the fixtures should not fall back");

            bool named3 = false;
            bool named5 = false;
            for (const auto &rename : batch.GetGlobalRenames())
            {
                named3 = named3 || (rename.first == "global3");
                named5 = named5 || (rename.first == "global5");
            }
            Assert::IsTrue(named5, L"global5 should be named from its assignment in script 951");
            Assert::IsTrue(named3, L"global3 should be named from global5's new name, which takes a second naming round");

            // Main's .sco carries the names.
            std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(helper, 0, lookups.GetSelectorTable());
            Assert::IsNotNull(mainSCO.get(), L"Main.sco should still exist");
            std::string name3 = mainSCO->GetVariableName(3);
            std::string name5 = mainSCO->GetVariableName(5);
            Assert::AreNotEqual(std::string("global3"), name3, L"Main.sco should hold the new name of slot 3");
            Assert::AreNotEqual(std::string("global5"), name5, L"Main.sco should hold the new name of slot 5");
            Assert::IsFalse(name3.empty());
            Assert::IsFalse(name5.empty());

            // And the written sources use them, with no old name left behind.
            std::string textA = ReadTextFile(helper.GetScriptFileName(950));
            Assert::IsFalse(ContainsIdentifier(textA, "global3"), ToW("script 950 still uses global3:\n" + textA).c_str());
            Assert::IsFalse(ContainsIdentifier(textA, "global5"), ToW("script 950 still uses global5:\n" + textA).c_str());
            Assert::IsTrue(ContainsIdentifier(textA, name3), ToW("script 950 should use " + name3 + ":\n" + textA).c_str());
            Assert::IsTrue(ContainsIdentifier(textA, name5), ToW("script 950 should use " + name5 + ":\n" + textA).c_str());
            std::string textB = ReadTextFile(helper.GetScriptFileName(951));
            Assert::IsFalse(ContainsIdentifier(textB, "global5"), ToW("script 951 still uses global5:\n" + textB).c_str());
            Assert::IsTrue(ContainsIdentifier(textB, name5), ToW("script 951 should use " + name5 + ":\n" + textB).c_str());

            // Each script got its own .sco too.
            Assert::IsNotNull(GetExistingSCOFromScriptNumber(helper, 950, lookups.GetSelectorTable()).get(), L"script 950 should have an .sco");
            Assert::IsNotNull(GetExistingSCOFromScriptNumber(helper, 951, lookups.GetSelectorTable()).get(), L"script 951 should have an .sco");
        }

        // A second batch over the same scripts finds nothing new to name and
        // writes the same text: the names are stable.
        TEST_METHOD(Batch_SecondRunIsStable)
        {
            _gameFolder = SetUpGameSCI11();
            PrepareBatchFixtures();

            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(helper), L"lookups should load");
            uint16_t dummy;
            lookups.GetSelectorTable().ReverseLookup("", dummy);
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

            TestDecompilerResults results;
            {
                DecompileBatch batch(config.get(), lookups, helper, results);
                batch.Run({ 950, 951 });
                Assert::IsFalse(batch.GetGlobalRenames().empty(), L"the first run should name globals");
            }
            std::string firstA = ReadTextFile(helper.GetScriptFileName(950));
            std::string firstB = ReadTextFile(helper.GetScriptFileName(951));

            {
                DecompileBatch batch(config.get(), lookups, helper, results);
                batch.Run({ 950, 951 });
                Assert::IsTrue(batch.GetGlobalRenames().empty(), L"the second run should find every global already named");
            }
            Assert::AreEqual(firstA, ReadTextFile(helper.GetScriptFileName(950)), L"script 950 should decompile the same the second time");
            Assert::AreEqual(firstB, ReadTextFile(helper.GetScriptFileName(951)), L"script 951 should decompile the same the second time");
        }

        // The dialog's follow-up: of the scripts not in the batch, only those
        // whose source still uses a renamed global's old name need to go again.
        TEST_METHOD(FindScriptsReferencingGlobals_FindsOldNamesOnly)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("BatchGlobalsA"); // uses global3 and global5
            AddFixtureScript("BatchGlobalsB"); // uses global5
            CResourceMap &rm = appState->GetResourceMap();
            rm.AssignName(ResourceType::Script, 950, NoBase36, "BatchGlobalsA");
            rm.AssignName(ResourceType::Script, 951, NoBase36, "BatchGlobalsB");
            const GameFolderHelper &helper = rm.Helper();

            std::vector<std::pair<std::string, std::string>> renames = { { "global3", "gSomething" } };
            std::set<uint16_t> stale = FindScriptsReferencingGlobals(helper, { 950, 951, 999 }, renames);
            Assert::AreEqual(1, (int)stale.size(), L"only script 950 uses global3");
            Assert::IsTrue(stale.count(950) == 1, L"script 950 uses global3");

            renames = { { "global5", "gOther" } };
            stale = FindScriptsReferencingGlobals(helper, { 950, 951 }, renames);
            Assert::AreEqual(2, (int)stale.size(), L"both scripts use global5");

            // A global neither script mentions.
            renames = { { "global50", "gFifty" } };
            stale = FindScriptsReferencingGlobals(helper, { 950, 951 }, renames);
            Assert::IsTrue(stale.empty(), L"global50 is used nowhere; global5 is not a match for it");

            Assert::IsTrue(FindScriptsReferencingGlobals(helper, { 950, 951 }, {}).empty(), L"no renames, nothing stale");
        }

        // The batch runs its naming rounds over each script's naming skeleton,
        // not its full tree, so the two must name the same globals: script 951
        // names global5 from gEgo, and script 950 then names global3 from it.
        TEST_METHOD(NamingSkeleton_NamesTheSameGlobalsAsTheFullTree)
        {
            _gameFolder = SetUpGameSCI11();
            PrepareBatchFixtures();

            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(helper), L"lookups should load");
            uint16_t dummy;
            lookups.GetSelectorTable().ReverseLookup("", dummy);
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

            for (uint16_t number : { (uint16_t)951, (uint16_t)950 })
            {
                CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
                Assert::IsTrue(compiled.Load(helper, helper.Version, number), L"the fixture should load");
                FixDuplicateObjectNames(compiled, config->GetSelectorTable());
                ObjectFileScriptLookups objectFileLookups(helper, lookups.GetSelectorTable());
                TestDecompilerResults results;
                DecompileLookups decompileLookups(config.get(), helper, number, &lookups, &objectFileLookups, &compiled, nullptr, &compiled, results);
                std::unique_ptr<sci::Script> full = DecompileToAst(helper, compiled, decompileLookups, appState->GetResourceMap().GetVocab000());
                std::unique_ptr<sci::Script> skeleton = BuildNamingSkeleton(*full);

                std::unique_ptr<CSCOFile> mainForFull = GetExistingSCOFromScriptNumber(helper, 0, lookups.GetSelectorTable());
                Assert::IsNotNull(mainForFull.get(), L"Main.sco should exist");
                CSCOFile mainForSkeleton = *mainForFull;
                std::unique_ptr<CSCOFile> oldSCO = GetExistingSCOFromScriptNumber(helper, number, lookups.GetSelectorTable());

                std::vector<std::pair<std::string, std::string>> fromFull, fromSkeleton;
                {
                    VariableNamer namer(*full, config.get(), mainForFull.get(), oldSCO.get());
                    fromFull = namer.Run();
                }
                {
                    VariableNamer namer(*skeleton, config.get(), &mainForSkeleton, oldSCO.get());
                    fromSkeleton = namer.Run();
                }
                Assert::IsFalse(fromFull.empty(), ToW(fmt::format("script {0} should name a global", number)).c_str());
                Assert::IsTrue(fromFull == fromSkeleton, ToW(fmt::format("script {0}: the skeleton should name the same globals as the full tree", number)).c_str());
                for (size_t i = 0; i < mainForFull->GetVariables().size(); i++)
                {
                    Assert::AreEqual(mainForFull->GetVariableName(i), mainForSkeleton.GetVariableName(i), L"Main.sco should end up the same either way");
                }
                // Script 950 can only name global3 once global5 has its name.
                SaveSCOFile(helper, *mainForFull);
            }
        }

        // Runs one batch over every script of a real game, as the Decompile
        // dialog does, and records the batch's memory lines and its elapsed
        // time. For measuring the batch on a whole game and for diffing its
        // output between two builds. Opt-in, driven by environment variables
        // so no local path is in the source:
        //   SCICOMP_BATCH_GAME  game folder (the batch WRITES its src folder,
        //                       so point this at a copy)
        //   SCICOMP_BATCH_OUT   file to write the report to
        TEST_METHOD(OptIn_BatchExistingGame)
        {
            const char *game = getenv("SCICOMP_BATCH_GAME");
            const char *outPath = getenv("SCICOMP_BATCH_OUT");
            Assert::IsTrue(game && outPath, L"set SCICOMP_BATCH_GAME and SCICOMP_BATCH_OUT");
            SetUpExistingGame(game);

            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(helper), L"lookups should load");
            uint16_t dummy;
            lookups.GetSelectorTable().ReverseLookup("", dummy);
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

            std::set<uint16_t> numbers;
            {
                auto container = appState->GetResourceMap().Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::AddInDefaultEnumFlags);
                for (auto &blob : *container)
                {
                    numbers.insert((uint16_t)blob->GetNumber());
                }
            }

            class MemoryLines : public TestDecompilerResults
            {
            public:
                void AddResult(DecompilerResultType type, const std::string &message) override
                {
                    if (message.compare(0, 7, "Memory ") == 0)
                    {
                        lines.push_back(message);
                    }
                    TestDecompilerResults::AddResult(type, message);
                }
                std::vector<std::string> lines;
            } results;

            ULONGLONG start = GetTickCount64();
            {
                DecompileBatch batch(config.get(), lookups, helper, results);
                batch.Run(numbers);
                results.lines.push_back(fmt::format("Wrote {0} of {1} scripts; {2} globals named", batch.GetWrittenScripts().size(), numbers.size(), batch.GetGlobalRenames().size()));
            }
            results.lines.push_back(fmt::format("Elapsed: {0} s", (GetTickCount64() - start) / 1000));

            std::ofstream out(outPath, std::ios::binary);
            for (const std::string &line : results.lines)
            {
                out << line << "\n";
            }
            CleanUpExistingGame();
        }
    };
}
