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

        // #104: symmetric to the above on the successor axis. A node with two
        // successors passed to GetFirstSuccOrNull only asserted "at most one" (a
        // no-op in Release) and returned the first, silently dropping the other
        // successor's blocks. It must throw so OutputNewStructure falls back.
        TEST_METHOD(GetFirstSuccOrNull_MultipleSuccessors_Throws)
        {
            // InsertPredecessor adds the reverse edge, so this gives `fork` two
            // successors (succA and succB).
            ExitNode fork(1), succA(2), succB(3);
            succA.InsertPredecessor(&fork);
            succB.InsertPredecessor(&fork);
            Assert::AreEqual((size_t)2, fork.Successors().size(), L"setup: two successors");

            Assert::ExpectException<ControlFlowException>(
                [&]() { GetFirstSuccOrNull(&fork); },
                L"a block with two successors must throw, not drop a block");
        }

        // The single-successor and no-successor paths must be unchanged.
        TEST_METHOD(GetFirstSuccOrNull_SingleOrNone_ReturnsExpected)
        {
            ExitNode node(1), succ(2);
            Assert::IsNull(GetFirstSuccOrNull(&node),
                L"no successors returns null");

            succ.InsertPredecessor(&node);
            Assert::IsTrue(GetFirstSuccOrNull(&node) == &succ,
                L"one successor returns that successor");
        }
    };
}
