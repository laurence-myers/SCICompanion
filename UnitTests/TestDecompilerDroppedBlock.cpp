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
#include "ControlFlowNode.h"
#include "DecompilerCore.h"
#include "DecompilerNew.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #64: the main-chunk output walk (EnumerateCodeChunks::Visit(const MainNode&))
    // follows a single predecessor back toward the head. A block reached by two
    // one-way jmps has two predecessors. GetFirstPredecessorOrNull only asserted
    // "at most one" -- a no-op in a Release build -- and then returned the first,
    // so the walk silently dropped the other predecessor's blocks. It must instead
    // throw ControlFlowException, so OutputNewStructure falls back to disassembly
    // for that function rather than emitting a function with code missing.
    TEST_CLASS(TestDecompilerDroppedBlock)
    {
    public:
        TEST_METHOD(GetFirstPredecessorOrNull_MultiplePredecessors_Throws)
        {
            // Two one-way jmps reaching one block: the merge has two predecessors.
            ExitNode predA(1), predB(2), merge(3);
            merge.InsertPredecessor(&predA);
            merge.InsertPredecessor(&predB);
            Assert::AreEqual((size_t)2, merge.Predecessors().size(), L"setup: two predecessors");

            // Before the fix this returned predA and dropped predB's blocks in a
            // Release build. It must now throw so the caller falls back.
            Assert::ExpectException<ControlFlowException>(
                [&]() { GetFirstPredecessorOrNull(&merge); },
                L"a block with two predecessors must throw, not drop a block");
        }

        // The single-predecessor and no-predecessor paths must be unchanged: the
        // walk still steps back through a linear chain and stops at the head.
        TEST_METHOD(GetFirstPredecessorOrNull_SingleOrNone_ReturnsExpected)
        {
            ExitNode head(1), body(2);
            Assert::IsNull(GetFirstPredecessorOrNull(&body),
                L"no predecessors returns null (the walk stops at the head)");

            body.InsertPredecessor(&head);
            Assert::IsTrue(GetFirstPredecessorOrNull(&body) == &head,
                L"one predecessor returns that predecessor");
        }
    };
}
