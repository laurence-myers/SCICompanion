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
#include "Version.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #59: the branch form of scicode::inst returns false when a branch has no
    // matching block (an internal codegen error, e.g. an else with no if), but
    // every call site ignored it. The branch then stayed undetermined, and
    // calc_size silently retargeted it to the code start, emitting wrong byte code
    // with no error. has_undetermined_branch() lets the caller detect this and
    // report a compile error instead.
    TEST_CLASS(TestScicodeBranch)
    {
    public:
        TEST_METHOD(UndeterminedBranch_InstReturnsFalse_AndIsDetected)
        {
            scicode code(sciVersion0);
            code.inst(0, Opcode::LDI, (uint16_t)0);

            // A branch to the "undetermined" marker with no enclosing branch block
            // is the "else with no if" case: inst reports it by returning false.
            bool ok = code.inst(0, Opcode::BNT, code.get_undetermined());
            Assert::IsFalse(ok, L"inst returns false for a branch with no matching block");

            // Before the fix this went unnoticed; now the caller can detect it.
            Assert::IsTrue(code.has_undetermined_branch(),
                L"an undetermined branch must be detected");
        }

        TEST_METHOD(DeterminedCode_HasNoUndeterminedBranch)
        {
            scicode code(sciVersion0);
            code.inst(0, Opcode::LDI, (uint16_t)7);
            code.inst(0, Opcode::RET);

            Assert::IsFalse(code.has_undetermined_branch(),
                L"code with no unresolved branch reports none");
        }
    };
}
