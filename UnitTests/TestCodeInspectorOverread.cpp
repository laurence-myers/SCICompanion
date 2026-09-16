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
#include "CodeInspector.h"
#include "PMachine.h"
#include "Version.h"
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #116: the InspectCode / DisassembleCode walkers captured a raw operand before
    // the end-bound check, so an operand truncated at the end of a code section was
    // read past the operand region. When the section end coincides with the end of
    // the resource buffer, that is a real past-allocation read. These tests place
    // the code so its end lands exactly on an inaccessible guard page, so any read
    // at or past pEnd faults; the fix's end-bound guard must stop before that read.
    //
    // InspectCode (CodeInspector.h) exercises the same guard logic. DisassembleCode
    // has its own additional read sites and is tested separately in
    // TestDisassembleOverread.cpp.
    TEST_CLASS(TestCodeInspectorOverread)
    {
        // Runs InspectCode over `code` positioned so that pEnd is the first byte of
        // an inaccessible page. Returns the opcodes the walker reported. If the
        // walker reads at pEnd (the bug), the process faults here.
        static std::vector<Opcode> RunOnGuardedBuffer(const std::vector<uint8_t> &code)
        {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            const SIZE_T pageSize = si.dwPageSize;
            Assert::IsTrue(code.size() <= pageSize, L"test code must fit in one page");

            uint8_t *base = static_cast<uint8_t *>(
                VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            Assert::IsNotNull(base, L"VirtualAlloc failed");
            DWORD oldProtect = 0;
            Assert::IsTrue(VirtualProtect(base + pageSize, pageSize, PAGE_NOACCESS, &oldProtect) != FALSE,
                L"VirtualProtect failed");

            // Place the code so its last byte is the last byte of the accessible
            // page; pEnd is then the start of the inaccessible page.
            uint8_t *pBegin = base + pageSize - code.size();
            uint8_t *pEnd = base + pageSize;
            memcpy(pBegin, code.data(), code.size());

            std::vector<Opcode> seen;
            // Read every reported operand into a sink the compiler cannot discard.
            // Otherwise, because these tests ignore operand values, the optimizer
            // would dead-store-eliminate the walker's `wOperandsRaw[i] = *pCur`
            // capture -- the very read the guard-page is meant to trap -- and a
            // missing guard would not fault. In production the operands are used, so
            // the read is live there; this makes it live here too.
            volatile uint32_t operandSink = 0;
            InspectCode(sciVersion2, pBegin, pEnd, 0,
                [&seen, &operandSink](Opcode opcode, const uint16_t *operands, uint16_t) -> bool
                {
                    seen.push_back(opcode);
                    operandSink = operandSink + operands[0] + operands[1] + operands[2];
                    return true;
                });

            VirtualFree(base, 0, MEM_RELEASE);
            return seen;
        }

    public:
        // A Filename opcode (otDEBUGSTRING) as the last byte: its string operand is
        // truncated to nothing, GetOperandSize returns 1, and the old walker read
        // *pEnd. The guard must stop first.
        TEST_METHOD(InspectCode_TruncatedFilenameOperand_DoesNotOverread)
        {
            std::vector<Opcode> seen = RunOnGuardedBuffer({ 0x7d });
            Assert::AreEqual(size_t(1), seen.size(), L"the opcode itself is still reported");
            Assert::IsTrue(seen[0] == Opcode::Filename, L"the last opcode is Filename");
        }

        // A pushi word opcode (raw 0x38, otINT -> 2-byte operand) as the last byte:
        // the fixed 2-byte operand is truncated, so the old walker read *(uint16_t*)pEnd.
        TEST_METHOD(InspectCode_TruncatedFixedOperand_DoesNotOverread)
        {
            std::vector<Opcode> seen = RunOnGuardedBuffer({ 0x38 });
            Assert::AreEqual(size_t(1), seen.size(), L"the opcode itself is still reported");
            Assert::IsTrue(seen[0] == Opcode::PUSHI, L"the last opcode is pushi");
        }

        // A COMPLETE Filename instruction whose operand ends exactly at the buffer
        // end: 0x7d then "A\0" (2 operand bytes). This must be read in full without
        // faulting and without the guard falsely flagging it as truncated.
        TEST_METHOD(InspectCode_CompleteInstructionAtBoundary_IsRead)
        {
            std::vector<Opcode> seen = RunOnGuardedBuffer({ 0x7d, 'A', 0x00 });
            Assert::AreEqual(size_t(1), seen.size(), L"the complete instruction is reported once");
            Assert::IsTrue(seen[0] == Opcode::Filename, L"the opcode is Filename");
        }
    };
}
