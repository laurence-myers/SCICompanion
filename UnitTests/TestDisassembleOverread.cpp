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
#include "Disassembler.h"
#include "PMachine.h"
#include "Version.h"
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #116: DisassembleCode read a raw operand before the end-bound check in three
    // places -- the STATE_CALCBRANCHES branch-offset read, the main operand loop,
    // and the hex-display pre-loop -- so an operand truncated at the end of a code
    // section was read past pEnd. These tests position the code so its end lands on
    // an inaccessible guard page, so any read at or past pEnd faults. Null lookups
    // are safe here: a truncated instruction stops before the lookup-using switch and
    // comment section, and a complete Filename opcode uses no lookups.
    TEST_CLASS(TestDisassembleCodeOverread)
    {
        // Runs DisassembleCode over `code` positioned so pEnd is the first byte of an
        // inaccessible page. Returns the disassembly text. A read at pEnd faults here.
        static std::string DisassembleOnGuardedBuffer(const std::vector<uint8_t> &code)
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

            uint8_t *pBegin = base + pageSize - code.size();
            uint8_t *pEnd = base + pageSize;
            memcpy(pBegin, code.data(), code.size());

            std::ostringstream out;
            DisassembleCode(sciVersion2, out, nullptr, nullptr, nullptr, nullptr, pBegin, pEnd, 0, nullptr);
            std::string text = out.str();

            VirtualFree(base, 0, MEM_RELEASE);
            return text;
        }

    public:
        // A branch opcode (BNT word, raw 0x30, otLABEL) as the last byte. Its branch
        // offset is truncated. The old STATE_CALCBRANCHES read *(uint16_t*)pEnd via
        // CalcOffset (a std::set insert, so not optimized away). The guard must skip it.
        TEST_METHOD(DisassembleCode_TruncatedBranchOperand_DoesNotOverread)
        {
            std::string text = DisassembleOnGuardedBuffer({ 0x30 });
            Assert::IsTrue(text.find("truncated") != std::string::npos,
                L"a truncated branch operand is reported, not read past the end");
        }

        // A pushi word opcode (raw 0x38, otINT -> 2-byte operand) as the last byte:
        // the main operand loop's raw capture would read *(uint16_t*)pEnd.
        TEST_METHOD(DisassembleCode_TruncatedFixedOperand_DoesNotOverread)
        {
            std::string text = DisassembleOnGuardedBuffer({ 0x38 });
            Assert::IsTrue(text.find("truncated") != std::string::npos,
                L"a truncated fixed operand is reported, not read past the end");
        }

        // A COMPLETE Filename instruction (0x7d then "A\0") whose operand ends exactly
        // at the buffer end must be disassembled in full without faulting and without
        // a false truncation. Filename uses no lookups, so null lookups are safe.
        TEST_METHOD(DisassembleCode_CompleteInstructionAtBoundary_IsRead)
        {
            std::string text = DisassembleOnGuardedBuffer({ 0x7d, 'A', 0x00 });
            Assert::IsTrue(text.find("truncated") == std::string::npos,
                L"a complete instruction at the boundary must not be flagged truncated");
            Assert::IsTrue(text.find("A") != std::string::npos, L"the filename string is shown");
        }
    };
}
