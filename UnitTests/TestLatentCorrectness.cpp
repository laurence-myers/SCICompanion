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
#include "StlUtil.h"
#include "sci.h"
#include <set>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #74: latent correctness defects in the service layer.
    TEST_CLASS(TestLatentCorrectness)
    {
        bool _createdAppState = false;

    public:
        TEST_METHOD_INITIALIZE(SetUp)
        {
            if (appState == nullptr)
            {
                appState = new AppState(nullptr);
                _createdAppState = true;
            }
        }

        TEST_METHOD_CLEANUP(TearDown)
        {
            if (_createdAppState)
            {
                delete appState;
                appState = nullptr;
                _createdAppState = false;
            }
        }

        // sci::array copied _size BYTES, not _size elements, so a copy of an
        // array with a multi-byte element type lost the tail.
        TEST_METHOD(SciArray_CopyOfMultiByteElements_CopiesEveryElement)
        {
            sci::array<uint16_t> source(4);
            source[0] = 0x1111;
            source[1] = 0x2222;
            source[2] = 0x3333;
            source[3] = 0x4444;

            sci::array<uint16_t> copied(source);
            Assert::AreEqual((size_t)4, copied.size());
            Assert::AreEqual((uint16_t)0x1111, copied[0]);
            Assert::AreEqual((uint16_t)0x2222, copied[1]);
            Assert::AreEqual((uint16_t)0x3333, copied[2], L"copy constructor: the third element is past the old 4-byte copy");
            Assert::AreEqual((uint16_t)0x4444, copied[3], L"copy constructor: the fourth element is past the old 4-byte copy");

            sci::array<uint16_t> assigned(1);
            assigned = source;
            Assert::AreEqual((size_t)4, assigned.size());
            Assert::AreEqual((uint16_t)0x3333, assigned[2], L"copy assignment: the third element is past the old 4-byte copy");
            Assert::AreEqual((uint16_t)0x4444, assigned[3], L"copy assignment: the fourth element is past the old 4-byte copy");
        }

        // ScriptId::operator< was not a strict weak ordering: it returned
        // (folder1 < folder2) only when (filename1 < filename2) was already
        // true. A = {a.sc, folder z} and B = {b.sc, folder a} then compared as
        // neither less than the other, so a std::set treated them as equal and
        // dropped one.
        TEST_METHOD(ScriptId_LessThan_IsAStrictWeakOrdering)
        {
            ScriptId a(TEXT("a.sc"), TEXT("z"));
            ScriptId b(TEXT("b.sc"), TEXT("a"));

            Assert::IsTrue(a < b, L"a.sc sorts before b.sc whatever the folders are");
            Assert::IsFalse(b < a);

            std::set<ScriptId> ids;
            ids.insert(a);
            ids.insert(b);
            Assert::AreEqual((size_t)2, ids.size(), L"two scripts with different names must both survive in a set");

            // Same name: the folder decides, consistently with operator==.
            ScriptId c(TEXT("same.sc"), TEXT("one"));
            ScriptId d(TEXT("same.sc"), TEXT("two"));
            Assert::IsTrue(c < d);
            Assert::IsFalse(d < c);
            Assert::IsFalse(c < c);
        }

        // AppState::GetGameName built a std::string from an uninitialised buffer
        // when no game was loaded, because _GetGameStringProperty does not
        // write the buffer on failure.
        TEST_METHOD(GetGameName_NoGameLoaded_ReturnsEmpty)
        {
            Assert::IsFalse(appState->GetResourceMap().IsGameLoaded(), L"setup: no game loaded");
            std::string name = appState->GetGameName();
            Assert::IsTrue(name.empty(), L"with no game loaded the name must be empty, not stack garbage");
        }
    };
}
