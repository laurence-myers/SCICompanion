#include "stdafx.h"
#include "CppUnitTest.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "CompiledScript.h"
#include "Vocab99x.h"
#include "Helper.h"
#include "TestSupport.h"
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    // The species of the classes of a compiled script, in the script's order.
    std::vector<uint16_t> ClassSpeciesInOrder(const CompiledScript &script)
    {
        std::vector<uint16_t> species;
        for (const auto &object : script.GetObjects())
        {
            if (!object->IsInstance())
            {
                species.push_back(object->GetSpecies());
            }
        }
        return species;
    }
}

namespace UnitTests
{
    // vocab.996 gives the script of each species, but not the order of a
    // script's classes. The compiler and the .sco number a script's classes
    // in the order of the source, which for a decompiled script is the order
    // of the compiled script. So the table orders a script's species as the
    // classes are in the compiled script, not in number order: when a game's
    // compiled script has its classes in another order (LB2 script 0), the
    // number order would give two classes each other's species.
    TEST_CLASS(TestSpeciesTable)
    {
        NoAppState _noAppState;
        GameCopy _game;

    public:
        TEST_METHOD(SpeciesOrder_FollowsTheCompiledClassOrder)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0, false, SessionOptions());
            const GameFolderHelper &helper = session.Helper();

            // A script with two or more classes.
            int scriptNumber = -1;
            std::unique_ptr<CompiledScript> compiled;
            auto container = helper.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly);
            for (auto &blob : *container)
            {
                auto candidate = std::make_unique<CompiledScript>((uint16_t)blob->GetNumber());
                if (candidate->TryLoad(helper, helper.Version, blob->GetNumber()) && (ClassSpeciesInOrder(*candidate).size() >= 2))
                {
                    scriptNumber = blob->GetNumber();
                    compiled = std::move(candidate);
                    break;
                }
            }
            Assert::IsTrue(scriptNumber >= 0, L"setup: the SCI0 template has a script with two classes");

            // Swap the species of its first two classes in the compiled bytes. In
            // SCI0, a class's species is its first property value, 6 bytes after
            // the object's magic word (the local variable offset, the method
            // offset and the property count come first).
            std::vector<const CompiledObject *> classes;
            for (const auto &object : compiled->GetObjects())
            {
                if (!object->IsInstance())
                {
                    classes.push_back(object.get());
                }
            }
            uint16_t first = classes[0]->GetSpecies();
            uint16_t second = classes[1]->GetSpecies();
            Assert::IsTrue(first < second, L"setup: the template's classes are in number order");
            std::unique_ptr<ResourceBlob> original = helper.MostRecentResource(ResourceType::Script, scriptNumber, ResourceEnumFlags::None);
            std::vector<uint8_t> data(original->GetData(), original->GetData() + original->GetLength());
            size_t firstAt = classes[0]->GetPosInResource() + 6;
            size_t secondAt = classes[1]->GetPosInResource() + 6;
            Assert::IsTrue((data[firstAt] | (data[firstAt + 1] << 8)) == first, L"setup: the first species is where the SCI0 format puts it");
            Assert::IsTrue((data[secondAt] | (data[secondAt + 1] << 8)) == second, L"setup: the second species is where the SCI0 format puts it");
            data[firstAt] = (uint8_t)(second & 0xff);
            data[firstAt + 1] = (uint8_t)(second >> 8);
            data[secondAt] = (uint8_t)(first & 0xff);
            data[secondAt + 1] = (uint8_t)(first >> 8);
            ResourceBlob patched(helper, nullptr, ResourceType::Script, data, helper.Version.DefaultVolumeFile, scriptNumber, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
            AssertOk(session.ResourceMap().WriteResource(patched));

            SpeciesTable table;
            Assert::IsTrue(table.Load(helper));

            // The first class in the compiled script now has the higher species.
            SpeciesIndex atFirst;
            SpeciesIndex atSecond;
            Assert::IsTrue(table.GetSpeciesIndex((uint16_t)scriptNumber, 0, atFirst));
            Assert::IsTrue(table.GetSpeciesIndex((uint16_t)scriptNumber, 1, atSecond));
            Assert::AreEqual((int)second, (int)atFirst.Type(), L"class 0 of the compiled script keeps its own species");
            Assert::AreEqual((int)first, (int)atSecond.Type(), L"class 1 of the compiled script keeps its own species");

            // And the other way: each species is at its class's place.
            uint16_t scriptOfSpecies = 0;
            uint16_t placeInScript = 0;
            Assert::IsTrue(table.GetSpeciesLocation(SpeciesIndex(second), scriptOfSpecies, placeInScript));
            Assert::AreEqual(0, (int)placeInScript);
        }

        // A compiled class whose species the table gives another script (a
        // leftover class: KQ5 script 992 has Rev, species 24 of script 978)
        // keeps its species and its place, so the class after it keeps its
        // species too; the table does not change. Load aligns the script, and
        // so does AlignScript (the compile calls it for each script).
        TEST_METHOD(SpeciesOrder_KeepsALeftoverClass)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0, false, SessionOptions());
            const GameFolderHelper &helper = session.Helper();

            int scriptNumber = -1;
            std::unique_ptr<CompiledScript> compiled;
            uint16_t other = 0;
            bool foundOther = false;
            auto container = helper.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly);
            for (auto &blob : *container)
            {
                auto candidate = std::make_unique<CompiledScript>((uint16_t)blob->GetNumber());
                if (!candidate->TryLoad(helper, helper.Version, blob->GetNumber()))
                {
                    continue;
                }
                std::vector<uint16_t> species = ClassSpeciesInOrder(*candidate);
                if ((scriptNumber < 0) && (species.size() >= 2))
                {
                    scriptNumber = blob->GetNumber();
                    compiled = std::move(candidate);
                }
                else if (!foundOther && !species.empty())
                {
                    other = species[0];
                    foundOther = true;
                }
            }
            Assert::IsTrue((scriptNumber >= 0) && foundOther, L"setup: the SCI0 template has a script with two classes, and another script with a class");

            std::vector<const CompiledObject *> classes;
            for (const auto &object : compiled->GetObjects())
            {
                if (!object->IsInstance())
                {
                    classes.push_back(object.get());
                }
            }
            uint16_t first = classes[0]->GetSpecies();
            uint16_t second = classes[1]->GetSpecies();
            std::unique_ptr<ResourceBlob> original = helper.MostRecentResource(ResourceType::Script, scriptNumber, ResourceEnumFlags::None);
            std::vector<uint8_t> data(original->GetData(), original->GetData() + original->GetLength());
            size_t firstAt = classes[0]->GetPosInResource() + 6;
            Assert::IsTrue((data[firstAt] | (data[firstAt + 1] << 8)) == first, L"setup: the first species is where the SCI0 format puts it");
            data[firstAt] = (uint8_t)(other & 0xff);
            data[firstAt + 1] = (uint8_t)(other >> 8);
            ResourceBlob patched(helper, nullptr, ResourceType::Script, data, helper.Version.DefaultVolumeFile, scriptNumber, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
            AssertOk(session.ResourceMap().WriteResource(patched));

            for (bool alignOnLoad : { true, false })
            {
                SpeciesTable table;
                Assert::IsTrue(table.Load(helper, alignOnLoad));
                table.AlignScript(helper, (uint16_t)scriptNumber);
                SpeciesIndex atFirst;
                SpeciesIndex atSecond;
                Assert::IsTrue(table.GetSpeciesIndex((uint16_t)scriptNumber, 0, atFirst));
                Assert::IsTrue(table.GetSpeciesIndex((uint16_t)scriptNumber, 1, atSecond));
                Assert::AreEqual((int)other, (int)atFirst.Type(), L"the leftover class keeps its species");
                Assert::AreEqual((int)second, (int)atSecond.Type(), L"the class after it keeps its species");
                // The table still gives the species to the other script.
                uint16_t scriptOfSpecies = 0;
                uint16_t placeInScript = 0;
                Assert::IsTrue(table.GetSpeciesLocation(SpeciesIndex(other), scriptOfSpecies, placeInScript));
                Assert::AreNotEqual(scriptNumber, (int)scriptOfSpecies);
                Assert::IsFalse(table.IsDirty());
            }
        }

        // Real games (LB2, "The Dagger of Amon Ra", has the case): each class of
        // each script keeps its species. Opt-in: set SCICOMP_SPECIES_GAME to one
        // game folder, or to several separated by ';'. The test only reads the
        // games.
        TEST_METHOD(OptIn_SpeciesOrder_RealGame)
        {
            char folders[16384] = {};
            if (!GetEnvironmentVariableA("SCICOMP_SPECIES_GAME", folders, ARRAYSIZE(folders)))
            {
                // An opt-in test fails when its input is missing (#79).
                Assert::Fail(L"SCICOMP_SPECIES_GAME is not set. Set it to a game folder, or to several separated by ';'; this opt-in test must not pass without running.");
            }
            std::string mismatches;
            int mismatchCount = 0;
            std::set<std::string> mismatchedScripts;
            int leftoverScripts = 0;
            std::string list = folders;
            size_t start = 0;
            while (start < list.size())
            {
                size_t end = list.find(';', start);
                if (end == std::string::npos)
                {
                    end = list.size();
                }
                std::string folder = list.substr(start, end - start);
                start = end + 1;
                if (folder.empty())
                {
                    continue;
                }
                GameSession session;
                AssertOk(session.Open(folder));
                const GameFolderHelper &helper = session.Helper();
                SpeciesTable table;
                if (!table.Load(helper))
                {
                    continue;   // A game with no class table.
                }
                auto container = helper.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly);
                for (auto &blob : *container)
                {
                    CompiledScript script((uint16_t)blob->GetNumber());
                    if (!script.TryLoad(helper, helper.Version, blob->GetNumber()))
                    {
                        continue;
                    }
                    std::vector<uint16_t> species = ClassSpeciesInOrder(script);
                    // A leftover class (its species belongs to another script in
                    // the table, or to none) keeps its positional numbering: a
                    // known gap, so such a script is only counted.
                    bool allInTable = true;
                    for (uint16_t classSpecies : species)
                    {
                        uint16_t owner = 0;
                        uint16_t place = 0;
                        if (!table.GetSpeciesLocation(SpeciesIndex(classSpecies), owner, place) || (owner != (uint16_t)blob->GetNumber()))
                        {
                            allInTable = false;
                        }
                    }
                    if (!allInTable)
                    {
                        leftoverScripts++;
                        continue;
                    }
                    for (size_t i = 0; i < species.size(); i++)
                    {
                        SpeciesIndex fromTable;
                        if (table.GetSpeciesIndex((uint16_t)blob->GetNumber(), (uint16_t)i, fromTable) && (fromTable.Type() != species[i]))
                        {
                            // One log line for each mismatch: the text of a
                            // failed assert is cut, and on a run over many
                            // games it would hide mismatches.
                            std::string line = folder + ": script " + std::to_string(blob->GetNumber()) + " class " + std::to_string(i) + ": table " + std::to_string(fromTable.Type()) + ", compiled " + std::to_string(species[i]);
                            Logger::WriteMessage(Wide(line).c_str());
                            mismatchCount++;
                            mismatchedScripts.insert(folder + ": script " + std::to_string(blob->GetNumber()));
                            if (mismatches.size() < 1000)
                            {
                                mismatches += line + "\n";
                            }
                        }
                    }
                }
            }
            Logger::WriteMessage(Wide("Scripts with a leftover class (not compared): " + std::to_string(leftoverScripts)).c_str());
            for (const std::string &script : mismatchedScripts)
            {
                Logger::WriteMessage(Wide("Mismatched " + script).c_str());
            }
            Assert::AreEqual(0, mismatchCount, Wide(std::to_string(mismatchCount) + " mismatches in " + std::to_string(mismatchedScripts.size()) +
                " scripts; the test log has one line for each. The first ones:\n" + mismatches).c_str());
        }
    };
}
