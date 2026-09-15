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
#include "scii.h"
#include "PMachine.h"
#include "DecompilerCore.h"
#include "Version.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #62: DecompileRaw keeps originalCode = code so the disassembly fallback
    // can read the pre-dead-branch instructions. A list assignment copies each
    // scii by value, so a copied branch instruction still holds a target
    // iterator into `code`. _RemoveDeadBranches then erases nodes from `code`,
    // leaving those copied iterators dangling; the fallback (CalcBranchLabels
    // and DisassembleFallback) dereferences them -- a use-after-free.
    // RepointBranchTargetsIntoCopy repoints the copy's branch targets into the
    // copy itself so it no longer depends on `code`.
    TEST_CLASS(TestDecompilerDangling)
    {
    public:
        TEST_METHOD(RepointBranchTargetsIntoCopy_MakesCopySelfContained)
        {
            // [ LDI, JMP -> LDI, RET ]  (a backward branch to the LDI)
            std::list<scii> source;
            source.push_back(scii(sciVersion0, Opcode::LDI, (uint16_t)0x1234, -1));
            source.push_back(scii(sciVersion0, Opcode::JMP, source.end(), true, -1));
            source.push_back(scii(sciVersion0, Opcode::RET, -1));

            code_pos ldi = source.begin();
            code_pos jmp = ldi; ++jmp;
            code_pos ret = jmp; ++ret;
            ldi->set_offset_and_size(0, 2);
            jmp->set_offset_and_size(2, 3);
            ret->set_offset_and_size(5, 1);
            jmp->set_branch_target(ldi, false); // backward: the JMP sits after the LDI

            // A list copy duplicates each scii by value, so the copy's JMP still
            // targets the LDI node inside `source`. This is the #62 hazard.
            std::list<scii> copy = source;
            code_pos copyJmp = copy.begin(); ++copyJmp;
            Assert::IsTrue(copyJmp->get_branch_target() == ldi,
                L"precondition: the raw copy shares source's branch iterator");

            RepointBranchTargetsIntoCopy(source, copy);

            // The copy's branch target must now live inside `copy`.
            code_pos copyTarget = copyJmp->get_branch_target();
            Assert::IsFalse(copyTarget == ldi,
                L"the copy must no longer point at source's node");
            bool insideCopy = false;
            for (code_pos it = copy.begin(); it != copy.end(); ++it)
            {
                if (it == copyTarget) { insideCopy = true; break; }
            }
            Assert::IsTrue(insideCopy, L"the copy's branch target must point inside the copy");
            Assert::IsTrue(copyTarget == copy.begin(),
                L"it must map to the copy's LDI (same position as source)");
            Assert::IsTrue(copyTarget->get_opcode() == Opcode::LDI, L"target is the LDI");

            // Erasing the node from `source` (as _RemoveDeadBranches does) must
            // not disturb the copy. Before the fix this freed the very node the
            // copy's iterator pointed at, so the read below was a use-after-free.
            source.erase(ldi);
            Assert::IsTrue(copyJmp->get_branch_target()->get_opcode() == Opcode::LDI,
                L"after erasing source's node, the copy's target is still valid");
            Assert::AreEqual((int)0, (int)copyJmp->get_branch_target()->get_final_offset(),
                L"the copy's target still reports the LDI's offset");
        }

        // Forward and backward branches, and more than one branch, must all be
        // remapped -- the copy must be fully independent of `source`.
        TEST_METHOD(RepointBranchTargetsIntoCopy_ForwardAndBackwardAndMultiple)
        {
            // [ BNT -> RET (forward), LDI, JMP -> BNT (backward), RET ]
            std::list<scii> source;
            source.push_back(scii(sciVersion0, Opcode::BNT, source.end(), true, -1));
            source.push_back(scii(sciVersion0, Opcode::LDI, (uint16_t)7, -1));
            source.push_back(scii(sciVersion0, Opcode::JMP, source.end(), true, -1));
            source.push_back(scii(sciVersion0, Opcode::RET, -1));

            code_pos bnt = source.begin();
            code_pos ldi = bnt; ++ldi;
            code_pos jmp = ldi; ++jmp;
            code_pos ret = jmp; ++ret;
            bnt->set_offset_and_size(0, 2);
            ldi->set_offset_and_size(2, 2);
            jmp->set_offset_and_size(4, 3);
            ret->set_offset_and_size(7, 1);
            bnt->set_branch_target(ret, true);  // forward to RET
            jmp->set_branch_target(bnt, false); // backward to BNT

            std::list<scii> copy = source;
            RepointBranchTargetsIntoCopy(source, copy);

            code_pos copyBnt = copy.begin();
            code_pos copyJmp = copy.begin(); ++copyJmp; ++copyJmp;
            code_pos copyRet = copy.begin(); ++copyRet; ++copyRet; ++copyRet;

            Assert::IsTrue(copyBnt->get_branch_target() == copyRet,
                L"the BNT (forward) must target the copy's RET");
            Assert::IsTrue(copyBnt->is_forward_branch(),
                L"the BNT stays a forward branch");
            Assert::IsTrue(copyJmp->get_branch_target() == copyBnt,
                L"the JMP (backward) must target the copy's BNT");
            Assert::IsFalse(copyJmp->is_forward_branch(),
                L"the JMP stays a backward branch");

            // Independence: clearing `source` leaves both copy targets valid.
            source.clear();
            Assert::IsTrue(copyBnt->get_branch_target()->get_opcode() == Opcode::RET,
                L"BNT target survives source destruction");
            Assert::IsTrue(copyJmp->get_branch_target()->get_opcode() == Opcode::BNT,
                L"JMP target survives source destruction");
        }
    };
}
