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
#include "View.h"
#include "Stream.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    TEST_CLASS(TestViewRle)
    {
    public:
        // #67: a VGA literal run that crosses a scanline must continue from the
        // correct byte on the next line. The cross-scanline branch used to copy
        // from the run start (scratchBuffer) and not advance the read pointer, so
        // it repeated the run's first bytes on each new line -- wrong for a literal
        // run (distinct bytes). Decode a 4x2 cel whose single 8-byte literal run
        // spans both lines and check the top line is the run's continuation.
        TEST_METHOD(ReadImageData_LiteralRunCrossingScanline_ContinuesNotRepeats)
        {
            // VGA RLE control byte 0x08: top two bits 0 => "copy the next 8 literal
            // bytes"; the 8 literal bytes are distinct so a repeat is detectable.
            uint8_t rle[] = { 0x08 };
            uint8_t literal[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
            sci::istream rleStream(rle, sizeof(rle));
            sci::istream literalStream(literal, sizeof(literal));

            Cel cel(size16(4, 2), point16(0, 0), 0);
            ReadImageData(rleStream, cel, true, literalStream);

            // Stored bottom-up; width 4 rounds to stride 4. Bottom line (offset 4)
            // holds the run's first four bytes; the top line (offset 0) must hold
            // the next four. With the bug the top line repeated 1,2,3,4.
            Assert::AreEqual(1, (int)cel.Data[4], L"bottom line byte 0");
            Assert::AreEqual(4, (int)cel.Data[7], L"bottom line byte 3");
            Assert::AreEqual(5, (int)cel.Data[0], L"top line must continue the run (was 1 with the bug)");
            Assert::AreEqual(6, (int)cel.Data[1], L"top line byte 1");
            Assert::AreEqual(7, (int)cel.Data[2], L"top line byte 2");
            Assert::AreEqual(8, (int)cel.Data[3], L"top line byte 3");
        }
    };
}
