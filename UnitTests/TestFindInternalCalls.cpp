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
#include "Version.h"
#include <set>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #124: FindInternalCallsInCodeSection walks a code section, sizing each
    // instruction so it can find CALL targets. It used to advance by the opcode-only
    // size (scii::GetInstructionArgumentSize), which has no operand bytes and so
    // sizes the SCI2 Filename opcode (otDEBUGSTRING, a variable-length string) as
    // zero. The walk then decoded the filename string's bytes as instructions; a
    // string byte that decodes as CALL (raw 0x40/0x41 -> Opcode::CALL) inserted a
    // bogus internal-call offset, degrading SCI2 decompile fidelity. The walk now
    // sizes operands from the actual bytes (GetOperandSize), so the string is
    // stepped over as data. sciVersion2 is used because RawToOpcode/GetOperandTypes
    // only recognise the Filename opcode for the SCI2 package format.
    TEST_CLASS(TestFindInternalCalls)
    {
    public:
        // A Filename opcode (0x7d) whose string operand is "A" -- the byte 0x41,
        // which decodes as a byte CALL. Before the fix the zero-sized Filename left
        // the walk on the 0x41 byte, decoding it as CALL and inserting a spurious
        // offset. Now the string (0x41 then the 0x00 terminator) is measured and
        // skipped, so no call is found.
        TEST_METHOD(Filename_StringByteThatLooksLikeCall_IsNotDecoded)
        {
            // 0x7d Filename, "A\0" string, 0x00 trailing (bnot, no operands).
            const BYTE code[] = { 0x7d, 0x41, 0x00, 0x00 };
            std::set<uint16_t> offsets;
            FindInternalCallsInCodeSection(sciVersion2, code, code + ARRAYSIZE(code), 0, offsets);
            Assert::AreEqual(size_t(0), offsets.size(),
                L"the filename string must be skipped, not decoded as a CALL");
        }

        // A genuine byte CALL (0x41) with an otLABEL byte operand (0x00) and an
        // otUINT16 frame-size operand. The target is the post-instruction pc plus
        // the (zero) relative offset: 1 (opcode) + 3 (operands) + 0 = 4. Guards that
        // the byte-aware walk still finds real calls.
        TEST_METHOD(RealCall_IsStillFound)
        {
            const BYTE code[] = { 0x41, 0x00, 0x00, 0x00 };
            std::set<uint16_t> offsets;
            FindInternalCallsInCodeSection(sciVersion2, code, code + ARRAYSIZE(code), 0, offsets);
            Assert::AreEqual(size_t(1), offsets.size(), L"the real CALL must be found");
            Assert::AreEqual(uint16_t(4), *offsets.begin(), L"the CALL target offset");
        }

        // A Filename opcode followed by a genuine CALL. The walk must skip the
        // string and then find the real call at its true offset (7), not the bogus
        // offset (5) the old walk produced by decoding the string byte as a CALL.
        TEST_METHOD(RealCallAfterFilename_IsFoundAtTrueOffset)
        {
            // 0x7d Filename, "A\0" string, then 0x41 CALL with three operand bytes.
            const BYTE code[] = { 0x7d, 0x41, 0x00, 0x41, 0x00, 0x00, 0x00 };
            std::set<uint16_t> offsets;
            FindInternalCallsInCodeSection(sciVersion2, code, code + ARRAYSIZE(code), 0, offsets);
            Assert::AreEqual(size_t(1), offsets.size(), L"exactly the real CALL, no phantom from the string");
            Assert::AreEqual(uint16_t(7), *offsets.begin(), L"the real CALL target offset, post-filename");
        }
    };
}
