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
#include "PhonemeMap.h"
#include "Sync.h"
#include <sphelper.h>
#include "sapi_lipsync.h"
#include "LipSyncUtil.h"
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #71: CreateLipSyncComponentFromPhonemes tested the uint16_t silence cel
    // against -1, which is never true, so a phoneme map with no "x" entry wrote
    // 0xffff (the sync end-marker) as a real cel. It also called back() on a
    // possibly empty per-word timing vector.
    TEST_CLASS(TestLipSyncUtil)
    {
    public:
        // A path under the temp folder that does not exist: PathFileExists fails
        // at once (no unmapped-drive or network timeout).
        static std::string NoSuchMapPath()
        {
            char tempPath[MAX_PATH] = {};
            GetTempPathA(ARRAYSIZE(tempPath), tempPath);
            return std::string(tempPath) + "scicompanion-no-such-folder\\phonemes.ini";
        }

        TEST_METHOD(NoSilencePhonemeInMap_UsesCelZero_NotTheEndMarker)
        {
            // A map file that does not exist gives an empty map: every lookup,
            // including "x", returns 0xffff.
            PhonemeMap emptyMap(NoSuchMapPath());
            Assert::AreEqual((uint16_t)0xffff, emptyMap.PhonemeToCel("x"), L"setup: the empty map has no silence cel");

            std::vector<alignment_result> alignments;
            std::unique_ptr<SyncComponent> sync = CreateLipSyncComponentFromPhonemes(emptyMap, alignments);

            Assert::IsTrue(sync != nullptr);
            Assert::IsTrue(sync->Entries.size() >= 1, L"the component always starts with silence");
            Assert::AreEqual((uint16_t)0, sync->Entries.front().Cel, L"silence must fall back to cel 0, not the 0xffff end-marker");
            for (const SyncEntry &entry : sync->Entries)
            {
                Assert::AreNotEqual((uint16_t)0xffff, entry.Cel, L"no entry may carry the end-marker as a cel");
            }
        }

        TEST_METHOD(AlignmentWithoutPhonemeTimings_DoesNotReadPastAnEmptyVector)
        {
            PhonemeMap emptyMap(NoSuchMapPath());

            alignment_result word;
            word.m_msStart = 0;
            word.m_msEnd = 120;
            word.m_orthography = L"hi";
            word.m_phonemes.push_back(L"h");
            // m_phonemeEndTimes deliberately left empty: back() on it was undefined.
            std::vector<alignment_result> alignments;
            alignments.push_back(word);

            std::unique_ptr<SyncComponent> sync = CreateLipSyncComponentFromPhonemes(emptyMap, alignments);
            Assert::IsTrue(sync != nullptr);
            // Only the leading and closing silence entries: the phoneme is not in the map.
            Assert::AreEqual((size_t)2, sync->Entries.size());
        }
    };
}
