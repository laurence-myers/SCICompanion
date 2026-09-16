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
#include "CompiledScript.h"
#include "PMachine.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #63: GetOperandSize measured the otDEBUGSTRING (Filename) operand with lstrlen
    // and no end bound, so a string with no null terminator before the end of the
    // code was read past the buffer. It now takes an end pointer and scans only up
    // to it; an unterminated string returns (end - start) + 1 so the caller's
    // end-bound check flags the instruction as truncated.
    TEST_CLASS(TestGetOperandSize)
    {
    public:
        TEST_METHOD(DebugString_Terminated_ReturnsLengthPlusNull)
        {
            // "ab\0" within bounds: two chars + the null = 3.
            const uint8_t buf[] = { 'a', 'b', '\0', 'x' };
            int size = GetOperandSize(0, otDEBUGSTRING, buf, buf + ARRAYSIZE(buf));
            Assert::AreEqual(3, size, L"a terminated string is its length plus the null");
        }

        TEST_METHOD(DebugString_UnterminatedBeforeEnd_IsBounded)
        {
            // No null in the first 3 bytes; the null is PAST the end pointer. The old
            // code's lstrlen would scan to index 6 (returning 7); the bounded scan
            // must stop at the end and return (3 - 0) + 1 == 4, so the caller sees a
            // truncated operand instead of an over-read.
            const uint8_t buf[] = { 'a', 'b', 'c', 'd', 'e', 'f', '\0' };
            int size = GetOperandSize(0, otDEBUGSTRING, buf, buf + 3);
            Assert::AreEqual(4, size, L"an unterminated string is bounded to (end - start) + 1");
        }

        TEST_METHOD(DebugString_OperandAtEnd_IsTruncated)
        {
            // The operand starts exactly at the end (no bytes) -> size 1 (> 0 remaining),
            // which the caller's (pEnd - pCur) < cIncr check treats as truncated.
            const uint8_t buf[] = { 'a' };
            int size = GetOperandSize(0, otDEBUGSTRING, buf + 1, buf + 1);
            Assert::AreEqual(1, size, L"a string operand with no bytes left is flagged truncated");
        }

        TEST_METHOD(FixedOperands_Unchanged)
        {
            const uint8_t buf[] = { 0, 0, 0, 0 };
            Assert::AreEqual(2, GetOperandSize(0, otUINT16, buf, buf + ARRAYSIZE(buf)), L"UINT16 is 2 bytes");
            Assert::AreEqual(1, GetOperandSize(0, otUINT8, buf, buf + ARRAYSIZE(buf)), L"UINT8 is 1 byte");
            Assert::AreEqual(0, GetOperandSize(0, otEMPTY, buf, buf + ARRAYSIZE(buf)), L"EMPTY is 0 bytes");
        }
    };
}
