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

        // Script 950 assigns global3 from global40; script 951 assigns global40
        // from gEgo. In script order, 950 sees global40 with no name and can
        // name nothing. The batch names global40 from 951, goes round again,
        // and names global3 from 950's use of the new name. Before, that took
        // a second full decompile of every script.
        TEST_METHOD(Batch_NamesGlobalsAcrossScriptsToFixpoint)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("BatchGlobalsA");
            AddFixtureScript("BatchGlobalsB");
            std::string error;
            Assert::IsTrue(CompileFixture(950, "BatchGlobalsA", &error), ToW("compile of BatchGlobalsA failed: " + error).c_str());
            Assert::IsTrue(CompileFixture(951, "BatchGlobalsB", &error), ToW("compile of BatchGlobalsB failed: " + error).c_str());

            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(helper), L"lookups should load");
            uint16_t dummy;
            lookups.GetSelectorTable().ReverseLookup("", dummy);
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

            // The fixtures rely on the template's Main leaving these two slots
            // under their standard names, so the decompiler treats them as
            // unnamed. If the template changes, pick two other unused slots.
            {
                std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(helper, 0, lookups.GetSelectorTable());
                Assert::IsNotNull(mainSCO.get(), L"the template game should have Main.sco");
                Assert::IsTrue(mainSCO->GetVariables().size() > 40, L"Main should have more than 40 globals");
                Assert::AreEqual(std::string("global3"), mainSCO->GetVariableName(3), L"slot 3 should be unnamed in the template");
                Assert::AreEqual(std::string("global40"), mainSCO->GetVariableName(40), L"slot 40 should be unnamed in the template");
            }

            TestDecompilerResults results;
            DecompileBatch batch(config.get(), lookups, helper, results);
            batch.Run({ 950, 951 });

            Assert::AreEqual(2, (int)batch.GetWrittenScripts().size(), L"both scripts should be written");
            Assert::AreEqual(0, results.fallbacks, L"the fixtures should not fall back");

            bool named3 = false;
            bool named40 = false;
            for (const auto &rename : batch.GetGlobalRenames())
            {
                named3 = named3 || (rename.first == "global3");
                named40 = named40 || (rename.first == "global40");
            }
            Assert::IsTrue(named40, L"global40 should be named from its assignment in script 951");
            Assert::IsTrue(named3, L"global3 should be named from global40's new name, which takes a second naming round");

            // Main's .sco carries the names.
            std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(helper, 0, lookups.GetSelectorTable());
            Assert::IsNotNull(mainSCO.get(), L"Main.sco should still exist");
            std::string name3 = mainSCO->GetVariableName(3);
            std::string name40 = mainSCO->GetVariableName(40);
            Assert::AreNotEqual(std::string("global3"), name3, L"Main.sco should hold the new name of slot 3");
            Assert::AreNotEqual(std::string("global40"), name40, L"Main.sco should hold the new name of slot 40");
            Assert::IsFalse(name3.empty());
            Assert::IsFalse(name40.empty());

            // And the written sources use them, with no old name left behind.
            std::string textA = ReadTextFile(helper.GetScriptFileName(950));
            Assert::IsFalse(ContainsIdentifier(textA, "global3"), ToW("script 950 still uses global3:\n" + textA).c_str());
            Assert::IsFalse(ContainsIdentifier(textA, "global40"), ToW("script 950 still uses global40:\n" + textA).c_str());
            Assert::IsTrue(ContainsIdentifier(textA, name3), ToW("script 950 should use " + name3 + ":\n" + textA).c_str());
            Assert::IsTrue(ContainsIdentifier(textA, name40), ToW("script 950 should use " + name40 + ":\n" + textA).c_str());
            std::string textB = ReadTextFile(helper.GetScriptFileName(951));
            Assert::IsFalse(ContainsIdentifier(textB, "global40"), ToW("script 951 still uses global40:\n" + textB).c_str());
            Assert::IsTrue(ContainsIdentifier(textB, name40), ToW("script 951 should use " + name40 + ":\n" + textB).c_str());

            // Each script got its own .sco too.
            Assert::IsNotNull(GetExistingSCOFromScriptNumber(helper, 950, lookups.GetSelectorTable()).get(), L"script 950 should have an .sco");
            Assert::IsNotNull(GetExistingSCOFromScriptNumber(helper, 951, lookups.GetSelectorTable()).get(), L"script 951 should have an .sco");
        }

        // A second batch over the same scripts finds nothing new to name and
        // writes the same text: the names are stable.
        TEST_METHOD(Batch_SecondRunIsStable)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("BatchGlobalsA");
            AddFixtureScript("BatchGlobalsB");
            std::string error;
            Assert::IsTrue(CompileFixture(950, "BatchGlobalsA", &error), ToW(error).c_str());
            Assert::IsTrue(CompileFixture(951, "BatchGlobalsB", &error), ToW(error).c_str());

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
            AddFixtureScript("BatchGlobalsA"); // uses global3 and global40
            AddFixtureScript("BatchGlobalsB"); // uses global40
            CResourceMap &rm = appState->GetResourceMap();
            rm.AssignName(ResourceType::Script, 950, NoBase36, "BatchGlobalsA");
            rm.AssignName(ResourceType::Script, 951, NoBase36, "BatchGlobalsB");
            const GameFolderHelper &helper = rm.Helper();

            std::vector<std::pair<std::string, std::string>> renames = { { "global3", "gSomething" } };
            std::set<uint16_t> stale = FindScriptsReferencingGlobals(helper, { 950, 951, 999 }, renames);
            Assert::AreEqual(1, (int)stale.size(), L"only script 950 uses global3");
            Assert::IsTrue(stale.count(950) == 1, L"script 950 uses global3");

            renames = { { "global40", "gOther" } };
            stale = FindScriptsReferencingGlobals(helper, { 950, 951 }, renames);
            Assert::AreEqual(2, (int)stale.size(), L"both scripts use global40");

            // A global whose standard name is a prefix of another's is not a match.
            renames = { { "global4", "gFour" } };
            stale = FindScriptsReferencingGlobals(helper, { 950, 951 }, renames);
            Assert::IsTrue(stale.empty(), L"global4 is not global40");

            Assert::IsTrue(FindScriptsReferencingGlobals(helper, { 950, 951 }, {}).empty(), L"no renames, nothing stale");
        }
    };
}
