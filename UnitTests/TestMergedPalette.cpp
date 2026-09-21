/***************************************************************************
    Copyright (c) 2026 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
***************************************************************************/
#include "stdafx.h"
#include "CppUnitTest.h"
#include "Helper.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "ResourceContainer.h"
#include "ResourceEntity.h"
#include "PaletteOperations.h"
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #97: GetMergedPalette(resource, 999) reads shared resource-map state
    // (GetPalette999 lazily creates a cached member; CreateResourceFromNumber walks
    // the map), which the raster render workers called on their own thread -- a data
    // race with the UI thread. The fix splits the method: the new
    // GetMergedPalette(resource, const PaletteComponent*) overload takes a global
    // palette the UI thread precomputed, so it touches no shared mutable map state
    // and a worker can call it (the worker's race is removed by inspection -- it no
    // longer calls the map-reading overload). These tests give the resource a
    // controlled embedded palette and pass a controlled global, so they verify the
    // new overload's merge semantics exactly.
    TEST_CLASS(TestMergedPalette)
    {
        std::string _gameFolder;

    public:
        TEST_METHOD_CLEANUP(CleanUpMergedPalette)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // The merge rule (PaletteComponent::MergeFromOther): a global colour wins
        // only where the embedded colour's rgbReserved is 0. The thread-safe overload
        // must apply it: global wins at an rgbReserved==0 slot, the embedded value
        // survives at an rgbReserved!=0 slot.
        TEST_METHOD(MergedPalette_ThreadSafeOverload_MergesByReservedPriority)
        {
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = appState->GetResourceMap();

            std::unique_ptr<ResourceEntity> view = _FirstView(rm);
            Assert::IsNotNull(view.get(), L"the SCI1.1 template must contain a view resource");
            _SetEmbeddedPalette(*view);

            PaletteComponent global;
            memset(global.Colors, 0, sizeof(global.Colors));
            global.Colors[0].rgbBlue = 100; global.Colors[0].rgbReserved = 1;
            global.Colors[1].rgbBlue = 200; global.Colors[1].rgbReserved = 1;

            std::unique_ptr<PaletteComponent> merged = rm.GetMergedPalette(*view, &global);
            Assert::IsNotNull(merged.get(), L"the thread-safe overload returned a palette");
            Assert::AreEqual(100, (int)merged->Colors[0].rgbBlue,
                L"global must win where the embedded rgbReserved is 0");
            Assert::AreEqual(22, (int)merged->Colors[1].rgbBlue,
                L"the embedded value must survive where its rgbReserved is not 0");
        }

        // A null global palette is a no-op merge (the path a game with no global
        // palette takes): the result is the resource's own embedded palette.
        TEST_METHOD(MergedPalette_NullGlobal_LeavesEmbeddedUnchanged)
        {
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = appState->GetResourceMap();

            std::unique_ptr<ResourceEntity> view = _FirstView(rm);
            Assert::IsNotNull(view.get(), L"the SCI1.1 template must contain a view resource");
            _SetEmbeddedPalette(*view);

            std::unique_ptr<PaletteComponent> merged = rm.GetMergedPalette(*view, nullptr);
            Assert::IsNotNull(merged.get(), L"the thread-safe overload returned a palette");
            Assert::AreEqual(11, (int)merged->Colors[0].rgbBlue, L"a null global leaves the embedded palette unchanged");
            Assert::AreEqual(22, (int)merged->Colors[1].rgbBlue, L"a null global leaves the embedded palette unchanged");
        }

    private:
        // A controlled embedded palette: slot 0 open to the global (rgbReserved 0),
        // slot 1 owned by the embedded (rgbReserved != 0).
        static void _SetEmbeddedPalette(ResourceEntity &view)
        {
            auto embedded = std::make_unique<PaletteComponent>();
            memset(embedded->Colors, 0, sizeof(embedded->Colors));
            embedded->Colors[0].rgbBlue = 11; embedded->Colors[0].rgbReserved = 0;
            embedded->Colors[1].rgbBlue = 22; embedded->Colors[1].rgbReserved = 3;
            view.RemoveComponent<PaletteComponent>();
            view.AddComponent<PaletteComponent>(std::move(embedded));
        }

        static std::unique_ptr<ResourceEntity> _FirstView(CResourceMap &rm)
        {
            auto container = rm.Resources(ResourceTypeFlags::View, ResourceEnumFlags::AddInDefaultEnumFlags);
            for (auto &blob : *container)
            {
                std::unique_ptr<ResourceEntity> view = CreateResourceFromResourceData(*blob, true);
                if (view)
                {
                    return view;
                }
            }
            return nullptr;
        }
    };

    // #147: PaletteComponent::operator== used memcmp over the whole object, which
    // read the base class vtable pointer and relied on the struct having no padding.
    // The fix compares the members instead. On the current layout there is no padding
    // and both operands share a vtable, so old and new behave identically here -- a
    // test cannot show that difference on this build. These tests are instead a
    // forward guard that every value-bearing member (Mapping, Colors, Compression,
    // EntryType) still takes part in equality, so a later change that drops a member
    // from the comparison is caught. They need no game folder.
    TEST_CLASS(TestPaletteEquality)
    {
    public:
        TEST_METHOD(PaletteEquality_IdenticalMembers_AreEqual)
        {
            PaletteComponent a; _Fill(a);
            PaletteComponent b; _Fill(b);
            Assert::IsTrue(a == b, L"palettes with identical members must compare equal");
            Assert::IsFalse(a != b, L"operator!= must be the negation of operator==");
        }

        TEST_METHOD(PaletteEquality_DifferByOneMember_AreNotEqual)
        {
            {
                PaletteComponent a; _Fill(a);
                PaletteComponent b; _Fill(b);
                b.Mapping[42] ^= 0xFF;
                Assert::IsTrue(a != b, L"a difference in Mapping must be detected");
            }
            {
                PaletteComponent a; _Fill(a);
                PaletteComponent b; _Fill(b);
                b.Colors[7].rgbReserved ^= 0xFF;
                Assert::IsTrue(a != b, L"a difference in a Colors entry must be detected");
            }
            {
                PaletteComponent a; _Fill(a);
                PaletteComponent b; _Fill(b);
                b.Compression = PaletteCompression::Header;
                Assert::IsTrue(a != b, L"a difference in Compression must be detected");
            }
            {
                PaletteComponent a; _Fill(a);
                PaletteComponent b; _Fill(b);
                b.EntryType = PaletteEntryType::FourByte;
                Assert::IsTrue(a != b, L"a difference in EntryType must be detected");
            }
        }

    private:
        // Populate every member to a known state, including the Colors array the
        // default constructor leaves uninitialised, so the comparison reads no
        // indeterminate bytes.
        static void _Fill(PaletteComponent &p)
        {
            for (size_t i = 0; i < sizeof(p.Mapping); i++) { p.Mapping[i] = (uint8_t)i; }
            memset(p.Colors, 0, sizeof(p.Colors));
            for (size_t i = 0; i < sizeof(p.Colors) / sizeof(p.Colors[0]); i++) { p.Colors[i].rgbReserved = (uint8_t)(i & 3); }
            p.Compression = PaletteCompression::None;
            p.EntryType = PaletteEntryType::ThreeByte;
        }
    };
}
