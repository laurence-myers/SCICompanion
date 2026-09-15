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
#include "Codec.h"
#include "CodecAlt.h"
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Feed the freesci-derived decompressors crafted / truncated input and prove
// they stop inside their declared output buffer and declared packed length
// instead of walking off the ends with raw pointers. Where the correct output
// is knowable it is asserted exactly.
//
// Some out-of-bounds *reads* cannot be observed portably from a plain debug
// build (they touch adjacent memory and do not crash); those tests assert the
// hardened contract (a graceful error return / no crash) and are ALSO meant to
// be run under the debug CRT heap / Application Verifier (PageHeap), where the
// pre-fix access faults. Such tests say so in a comment.

namespace UnitTests
{
    TEST_CLASS(TestCodec)
    {
        bool _createdAppState = false;

    public:
        // Several error paths call appState->LogInfo, so a global is required.
        TEST_METHOD_INITIALIZE(SetUpCodec)
        {
            if (appState == nullptr)
            {
                appState = new AppState(nullptr);
                _createdAppState = true;
            }
        }

        TEST_METHOD_CLEANUP(TearDownCodec)
        {
            if (_createdAppState)
            {
                delete appState;
                appState = nullptr;
                _createdAppState = false;
            }
        }

        // (d) LZW overflow branch used to advance destctr twice per loop,
        // dropping every other byte. Stream decodes to a run of 'A'; declaring
        // the output one byte short forces the last copy into that branch.
        TEST_METHOD(LZW_OverflowBranch_DoesNotDropBytes)
        {
            // literal 'A'(0x41), token 0x102, token 0x103, terminator 0x101,
            // packed LSB-first as 9-bit tokens.
            BYTE src[] = { 0x41, 0x04, 0x0E, 0x0C, 0x08 };
            BYTE dest[6];
            memset(dest, 0x00, sizeof(dest));
            dest[5] = 0xCD; // canary just past the declared 5-byte output

            int result = decompressLZW(dest, src, 5, (int)sizeof(src));

            Assert::AreEqual(0, result, L"decompressLZW should succeed");
            for (int i = 0; i < 5; i++)
                Assert::AreEqual((int)'A', (int)dest[i], L"every decoded byte must be 'A' (no dropped bytes)");
            Assert::AreEqual((int)0xCD, (int)dest[5], L"must not write past declared size");
        }

        // (b)+(a) STAC/LZS: putByte had no output bound. Declare the unpacked
        // size smaller than the literal run the stream produces; the surplus
        // literals must be dropped, not written past the buffer.
        TEST_METHOD(LZS_DeclaredOutputTooSmall_DoesNotOverrun)
        {
            // 5 literals (0x11,0x22,0x33,0x44,0x55) then the end marker, packed
            // MSB-first. Trailing zero padding so a pre-fix read-ahead past
            // packedSize stays inside the allocation in a normal build.
            BYTE packed[16] = { 0x08, 0x88, 0x86, 0x64, 0x42, 0xAE, 0x00 };
            const uint32_t packedSize = 7;

            BYTE dest[10];
            memset(dest, 0xCD, sizeof(dest)); // 0xCD guard everywhere
            const uint32_t declared = 2;

            bool ok = decompressLZS(dest, packed, declared, packedSize);

            Assert::IsTrue(ok, L"should report full (bounded) output");
            Assert::AreEqual((int)0x11, (int)dest[0]);
            Assert::AreEqual((int)0x22, (int)dest[1]);
            for (int i = 2; i < 10; i++) // pre-fix wrote 0x33,0x44,0x55 here
                Assert::AreEqual((int)0xCD, (int)dest[i], L"putByte wrote past the declared unpacked size");
            // Under Application Verifier / PageHeap, size 'packed' to exactly
            // packedSize to also catch the pre-fix readByte overrun (defect a).
        }

        // (b) STAC/LZS copyComp: hpos = _dwWrote - offs went negative for a
        // back-reference before the start of output. The first op here is a
        // 7-bit-offset back-reference at output position 0, so pre-fix it read
        // dest[-1] and copied it into dest[0].
        TEST_METHOD(LZS_BackReferenceBeforeStart_DoesNotUnderrun)
        {
            // [compressed][7-bit offset=1][len code 00 => 2][end marker], MSB-first.
            BYTE packed[12] = { 0xC0, 0x98, 0x00 };
            const uint32_t packedSize = 3;

            BYTE raw[1 + 4 + 4];
            memset(raw, 0xCD, sizeof(raw));
            raw[0] = 0xAA;             // this byte is physically dest[-1]
            BYTE *dest = raw + 1;
            memset(dest, 0x55, 4);     // dest[0..3] = 0x55
            const uint32_t declared = 4;

            bool ok = decompressLZS(dest, packed, declared, packedSize);

            // After the fix copyComp skips the bad back-reference, so dest[0]
            // keeps its prefill. Pre-fix it became 0xAA (copied from dest[-1]).
            Assert::AreEqual((int)0x55, (int)dest[0], L"negative back-reference must not be copied");
            Assert::IsFalse(ok, L"a corrupt stream must not report success");
        }

        // (c) Huffman reads src[0]/src[1] before any length check.
        // NOTE: PageHeap-gated. In a plain build the existing BoundsCheckedArray
        // guard already turns the follow-on access into the same
        // SCI_ERROR_DECOMPRESSION_OVERFLOW, so this passes both before and after
        // the fix. It only faults pre-fix under Application Verifier / PageHeap.
        TEST_METHOD(Huffman_HeaderShorterThanTwo_Fails)
        {
            std::vector<BYTE> src(1, 0x00); // complength 1: src[1] is OOB
            BYTE dest[16] = { 0 };

            int result = decompressHuffman(dest, src.data(), (int)sizeof(dest), 1);

            Assert::AreEqual(SCI_ERROR_DECOMPRESSION_OVERFLOW, result,
                L"a <2-byte Huffman resource must fail without reading src[1]");
            // PageHeap: the pre-fix src[1] read faults; the fix returns first.
        }

        // (c) Huffman: the tree walk read src[*bytectr] with no complength
        // guard. One node whose branch byte is non-zero, bit data truncated at
        // complength, so the first walk step needs an out-of-range byte.
        // NOTE: PageHeap-gated (see Huffman_HeaderShorterThanTwo_Fails). Passes
        // both before and after the fix in a plain build.
        TEST_METHOD(Huffman_TreeWalkPastEnd_Fails)
        {
            // numnodes=1 => bytectr starts at 2+2 = 4 == complength(4).
            // node = src[2..3]; node[1]=0xFF (!=0) forces the walk to read src[4].
            BYTE src[4] = { 0x01, 0x00, 0x00, 0xFF };
            BYTE dest[16] = { 0 };

            int result = decompressHuffman(dest, src, (int)sizeof(dest), (int)sizeof(src));

            Assert::AreEqual(SCI_ERROR_DECOMPRESSION_OVERFLOW, result,
                L"a truncated Huffman bit stream must fail, not read past complength");
            // PageHeap catches the pre-fix src[4] read.
        }

        // (e) reorderView: lh_present (a file byte 0..255) is memcpy'd into a
        // 100-byte stack array. 200 smashes the stack pre-fix.
        TEST_METHOD(ReorderView_TooManyLoopHeaders_NoStackOverflow)
        {
            BYTE src[256];
            memset(src, 0, sizeof(src));
            src[2] = 0;    // loopheaders = 0
            src[3] = 200;  // lh_present = 200 (> sizeof(celcounts)==100)
            // cellengths offset, lh_mask, unknown, pal_offset, cel_total all 0

            BYTE dest[64];
            memset(dest, 0, sizeof(dest));
            BoundsCheckedArray<BYTE> destArr(dest, (int)sizeof(dest));

            reorderView(src, destArr); // pre-fix: 200-byte memcpy into char[100]
            // Reaching here (no /GS or /RTC1 abort) is the pass condition.
            Assert::IsTrue(true);
        }

        // (e) reorderView: a loop claiming more cels than cel_total wrote past
        // the cc_pos[] heap array. One present loop, cel_total=1, cel count=5.
        // NOTE: PageHeap-gated for the pre-fix fault; post-fix the guard returns
        // cleanly.
        TEST_METHOD(ReorderView_CelCountExceedsTotal_NoHeapOverflow)
        {
            BYTE src[64];
            memset(src, 0, sizeof(src));
            // cellengths = src+2; loopheaders=1; lh_present=1; lh_mask=0;
            src[2] = 1;
            src[3] = 1;
            src[10] = 1; src[11] = 0; // cel_total = 1
            src[12] = 5;              // celcounts[0] = 5 (> cel_total)

            BYTE dest[512];
            memset(dest, 0, sizeof(dest));
            BoundsCheckedArray<BYTE> destArr(dest, (int)sizeof(dest));

            reorderView(src, destArr); // pre-fix: cc_pos[1..4] written OOB
            Assert::IsTrue(true);
            // Pre-fix corrupts the cc_pos heap block (caught on free by the
            // debug CRT heap, or immediately under PageHeap).
        }

        // (e) reorderPic: view_start < PAL_SIZE+2 made (view_start-PAL_SIZE-2)
        // negative, i.e. a ~4GB memcpy size. The fix skips the copy.
        TEST_METHOD(ReorderPic_ViewStartTooSmall_NoHugeMemcpy)
        {
            std::vector<BYTE> src(4096, 0);
            src[0] = 0x09; src[1] = 0x04; // view_size  = 1033
            src[2] = 0xE8; src[3] = 0x03; // view_start = 1000 (< PAL_SIZE+2 == 1286)
            src[4] = 0x04; src[5] = 0x00; // cdata_size = 4

            const int dsize = 2048;       // 1000 + 15 + 1033 == 2048 -> trailing copy skipped
            std::vector<BYTE> dest(dsize, 0);

            reorderPic(src.data(), dest.data(), dsize); // pre-fix: negative-size memcpy -> crash
            Assert::IsTrue(true);
        }
    };
}
