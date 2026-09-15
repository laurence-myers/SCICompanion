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
#include "PMachine.h"
#include "scii.h"
#include "Version.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // GetOperandTypes indexes a global [TOTAL_OPCODES] (128) table by the opcode
    // byte. Opcodes at or past 128 have no row: INDETERMINATE (130) for an
    // unrecognised instruction, and the SCI2 debug pseudo-opcodes Filename (128)
    // and LineNumber (129) when they reach the SCI0 path. Indexing the table with
    // them read past its end -- a global-buffer-overflow reachable during a normal
    // decompile (e.g. from _IsVariableUse). It must instead return an empty row.
    TEST_CLASS(TestPMachine)
    {
    public:
        TEST_METHOD(GetOperandTypes_OpcodePastTable_ReturnsEmptyRowNotOverflow)
        {
            // INDETERMINATE (130) is past the table for every package format.
            const OperandType *sci0 = GetOperandTypes(sciVersion0, Opcode::INDETERMINATE);
            Assert::AreEqual((int)otEMPTY, (int)sci0[0], L"SCI0 INDETERMINATE operand 0");
            Assert::AreEqual((int)otEMPTY, (int)sci0[1], L"SCI0 INDETERMINATE operand 1");
            Assert::AreEqual((int)otEMPTY, (int)sci0[2], L"SCI0 INDETERMINATE operand 2");

            const OperandType *sci2 = GetOperandTypes(sciVersion2, Opcode::INDETERMINATE);
            Assert::AreEqual((int)otEMPTY, (int)sci2[0], L"SCI2 INDETERMINATE operand 0");
            Assert::AreEqual((int)otEMPTY, (int)sci2[1], L"SCI2 INDETERMINATE operand 1");
            Assert::AreEqual((int)otEMPTY, (int)sci2[2], L"SCI2 INDETERMINATE operand 2");

            // Filename (128) and LineNumber (129) are past the SCI0 table.
            const OperandType *fn = GetOperandTypes(sciVersion0, Opcode::Filename);
            Assert::AreEqual((int)otEMPTY, (int)fn[0], L"SCI0 Filename operand 0");
            const OperandType *ln = GetOperandTypes(sciVersion0, Opcode::LineNumber);
            Assert::AreEqual((int)otEMPTY, (int)ln[0], L"SCI0 LineNumber operand 0");

            // Note: in a plain build the pre-fix out-of-bounds read lands in the
            // adjacent global table, whose bytes at these offsets happen to be
            // otEMPTY, so the assertions above only reliably fail without the fix
            // under the AddressSanitizer leg (#42). The in-range check below is the
            // self-contained half: it proves the bounds check did not disturb the
            // normal 0..127 path.
        }

        // A real, in-range opcode must still return its true operand row. This
        // guards the "no-op for opcodes 0..127" property of the bounds check.
        TEST_METHOD(GetOperandTypes_InRangeOpcode_ReturnsRealRow)
        {
            // SCI0 call: {otLABEL, otUINT8, otEMPTY}.
            const OperandType *sci0Call = GetOperandTypes(sciVersion0, Opcode::CALL);
            Assert::AreEqual((int)otLABEL, (int)sci0Call[0], L"SCI0 call operand 0");
            Assert::AreEqual((int)otUINT8, (int)sci0Call[1], L"SCI0 call operand 1");
            Assert::AreEqual((int)otEMPTY, (int)sci0Call[2], L"SCI0 call operand 2");

            // SCI2 call: {otLABEL, otUINT16, otEMPTY}.
            const OperandType *sci2Call = GetOperandTypes(sciVersion2, Opcode::CALL);
            Assert::AreEqual((int)otLABEL, (int)sci2Call[0], L"SCI2 call operand 0");
            Assert::AreEqual((int)otUINT16, (int)sci2Call[1], L"SCI2 call operand 1");
        }
    };
}
