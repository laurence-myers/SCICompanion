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
#include "AppState.h"
#include "ResourceMap.h"
#include "ResourceContainer.h"
#include "ResourceBlob.h"
#include "CompiledScript.h"
#include "Helper.h"
#include "Stream.h"
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #65: CompiledObject left _wSpeciesIfClass, _wSuperClass, _wInfo and
    // _wPosInResource without a value, and the SCI1.1 loader never set
    // _wPosInResource at all. The scalars now default to 0, and Create_SCI1_1
    // records the object's offset in the heap resource.
    TEST_CLASS(TestCompiledObjectInit)
    {
        std::string _gameFolder;

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        TEST_METHOD(DefaultConstructed_ScalarsAreZero)
        {
            // Allocate on the heap after a scribbled buffer so a stale value is
            // more likely to show through if a member is left uninitialised.
            {
                auto scribble = std::make_unique<uint8_t[]>(sizeof(CompiledObject) * 4);
                memset(scribble.get(), 0xAB, sizeof(CompiledObject) * 4);
            }
            auto obj = std::make_unique<CompiledObject>();
            Assert::AreEqual((uint16_t)0, obj->GetSpeciesIfClass());
            Assert::AreEqual((uint16_t)0, obj->GetSuperClass());
            Assert::AreEqual((uint16_t)0, obj->GetInfo());
            Assert::AreEqual((uint16_t)0, obj->GetPosInResource());
        }

        TEST_METHOD(SCI11_PosInResource_IsTheHeapOffsetOfTheObject)
        {
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = appState->GetResourceMap();

            auto container = rm.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::AddInDefaultEnumFlags);
            int checked = 0;
            for (auto &blob : *container)
            {
                CompiledScript compiledScript(blob->GetNumber());
                sci::istream scriptStream = blob->GetReadStream();
                if (!compiledScript.Load(rm.Helper(), appState->GetVersion(), blob->GetNumber(), scriptStream))
                {
                    continue;
                }
                if (compiledScript.GetObjects().empty())
                {
                    continue;
                }
                std::unique_ptr<ResourceBlob> heap = rm.MostRecentResource(ResourceType::Heap, blob->GetNumber(), false);
                Assert::IsTrue(heap != nullptr, L"an SCI1.1 script with objects must have a heap resource");
                sci::istream heapStream = heap->GetReadStream();
                const uint8_t *heapBytes = heapStream.GetInternalPointer();
                uint32_t heapSize = heapStream.getBytesRemaining();

                for (const auto &object : compiledScript.GetObjects())
                {
                    uint16_t pos = object->GetPosInResource();
                    // Every SCI1.1 object starts with the magic word 0x1234.
                    Assert::IsTrue((uint32_t)pos + 2 <= heapSize, L"the object position must lie inside the heap");
                    uint16_t magic = (uint16_t)(heapBytes[pos] | (heapBytes[pos + 1] << 8));
                    Assert::AreEqual((uint16_t)0x1234, magic, L"the object position must point at the object's magic word in the heap");
                    checked++;
                }
                if (checked >= 5)
                {
                    break;
                }
            }
            Assert::IsTrue(checked > 0, L"setup: at least one SCI1.1 object must be checked");
        }
    };
}
