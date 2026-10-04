#include "stdafx.h"
#include "CppUnitTest.h"
#include "ScopeAsm.h"
#include "ScopeCode.h"
#include "PMachine.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace scope;

namespace UnitTests
{
	TEST_CLASS(TestScopeCode)
	{
	public:
		// resF follows jmp, takes bnt and falls through bt; resT takes bt and
		// falls through bnt; resJ follows jmp only. Sierra's threading moves
		// a bnt to a bnt of the same sense: the two targets are equal for a
		// false value only.
		TEST_METHOD(Resolve_FollowsTheBranchesForTheArrival)
		{
			ScopeAsm a(R"(
				lap 1
				bnt else
				lap 2
				bnt mid
				ldi 1
			mid:
				bnt else
				ldi 2
			orJoin:
				bt end
			orFalse:
				jmp else
			else:
				ldi 3
			end:
				ret
			)");
			CodeModel model(a.code);
			int mid = a.At("mid");
			Assert::AreEqual(a.At("else"), model.Resolve(mid, Arrival::False));
			Assert::AreEqual(mid + 1, model.Resolve(mid, Arrival::True));
			Assert::AreEqual(mid, model.Resolve(mid, Arrival::Jump));
			Assert::IsTrue(model.SameTarget(mid, a.At("else"), Arrival::False));
			Assert::IsFalse(model.SameTarget(mid, a.At("else"), Arrival::True));

			int orJoin = a.At("orJoin");
			Assert::AreEqual(a.At("end"), model.Resolve(orJoin, Arrival::True));
			// A false value falls through the bt onto the jmp, which goes to else.
			Assert::AreEqual(a.At("else"), model.Resolve(orJoin, Arrival::False));
			Assert::AreEqual(orJoin, model.Resolve(orJoin, Arrival::Jump));
			Assert::AreEqual(a.At("else"), model.Resolve(a.At("orFalse"), Arrival::Jump));
			Assert::AreEqual(model.Size(), model.Resolve(model.Size(), Arrival::Jump), L"the end of the function");
		}

		// Two jmps that go to each other: no place. A circle is not equal to
		// another circle.
		TEST_METHOD(Resolve_ACircleIsNoIndex)
		{
			ScopeAsm a(R"(
				jmp one
			one:
				jmp two
			two:
				jmp one
			)");
			CodeModel model(a.code);
			Assert::AreEqual(NoIndex, model.Resolve(a.At("one"), Arrival::Jump));
			Assert::IsFalse(model.SameTarget(a.At("one"), a.At("two"), Arrival::Jump));
		}

		// A bnt (or a bt) right after a bnt (or a bt) to the same place does
		// nothing: control gets to it with a value that does not branch. Also
		// with a jmp that does nothing between them. Not when another branch
		// goes to it, and not after a branch of the other kind.
		TEST_METHOD(LiveCode_ASecondTestOfTheSameValueIsANoOp)
		{
			ScopeAsm a(R"(
				lap 1
				bnt else
			second:
				bnt else
				lap 2
				bt end
			secondBt:
				bt end
				lap 3
				bnt else
			label:
				bnt else
				lap 4
				bt else
			otherKind:
				bnt else
				lap 5
				bnt else
				jmp afterJmp
			afterJmp:
				bnt else
				jmp label
			else:
				ldi 2
			end:
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.IsNoOp(a.At("second")), L"a second bnt");
			Assert::IsTrue(model.IsNoOp(a.At("secondBt")), L"a second bt");
			Assert::IsTrue(model.IsNoOp(a.At("afterJmp")), L"after a jmp that does nothing");

			// A branch to the jmp between them: control gets there with
			// another value.
			ScopeAsm toJmp(R"(
				lal 2
				bnt between
				lal 0
				bnt end
			between:
				jmp test
			test:
				bnt end
				ldi 7
				sal 1
			end:
				ret
			)");
			CodeModel toJmpModel(toJmp.code);
			Assert::IsFalse(toJmpModel.IsNoOp(toJmp.At("test")), L"a branch goes to the jmp before it");

			// The first test does nothing: the second one is the test.
			ScopeAsm firstNoOp(R"(
				lal 0
				bnt between
			between:
				jmp test
			test:
				bnt end
				ldi 7
				sal 1
			end:
				ret
			)");
			CodeModel firstNoOpModel(firstNoOp.code);
			Assert::IsTrue(firstNoOpModel.IsNoOp(1), L"a bnt to its fall-through");
			Assert::IsFalse(firstNoOpModel.IsNoOp(firstNoOp.At("test")), L"the test after a bnt that does nothing");

			// Three tests of one value: the second and the third do nothing.
			ScopeAsm three(R"(
				lal 0
				bnt end
				bnt end
				bnt end
				ldi 7
				sal 1
			end:
				ret
			)");
			CodeModel threeModel(three.code);
			Assert::IsFalse(threeModel.IsNoOp(1));
			Assert::IsTrue(threeModel.IsNoOp(2));
			Assert::IsTrue(threeModel.IsNoOp(3));
			Assert::IsFalse(model.IsNoOp(a.At("label")), L"a jmp goes to it");
			Assert::IsFalse(model.IsNoOp(a.At("otherKind")), L"after a bt");
		}

		// A bt to the second of two bnts to one place arrives with a true
		// value: the second bnt does nothing for it either (PQ4 CD script
		// 505, soundScript::changeState; LSL6 CD script 330, arcScr::doit). A
		// bnt to it arrives with a false value: the second bnt branches.
		TEST_METHOD(LiveCode_ABtToTheSecondTestIsLikeTheFallThrough)
		{
			ScopeAsm a(R"(
				lal 4
				bt second
				lap 1
				bnt else
			second:
				bnt else
				ldi 1
				sal 2
			else:
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.IsNoOp(a.At("second")));
			ScopeAsm b(R"(
				lal 4
				bnt second
				lap 1
				bnt else
			second:
				bnt else
				ldi 1
				sal 2
			else:
				ret
			)");
			CodeModel modelB(b.code);
			Assert::IsFalse(modelB.IsNoOp(b.At("second")));
		}

		// Two switches whose tosses are dead (each case returns), one inside a
		// case of the other: each toss gets the head of its own switch (QfG3
		// script 160, localproc_0482).
		TEST_METHOD(Depths_DeadTossesOfNestedSwitches)
		{
			ScopeAsm a(R"(
			outerHead:
				lsg 125
				dup
				ldi 0
				eq?
				bnt case1
				+al 1
			innerHead:
				push
				dup
				ldi 1
				eq?
				bnt inner2
				ldi 20
				ret
				jmp innerDone
			inner2:
				ldi 16
				ret
			innerDone:
				toss
				jmp outerDone
			case1:
				dup
				ldi 1
				eq?
				bnt other
				ldi 11
				ret
				jmp outerDone
			other:
				ldi 15
				ret
			outerDone:
				toss
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(a.At("innerHead"), model.SwitchHead(a.At("innerDone")));
			Assert::AreEqual(a.At("outerHead"), model.SwitchHead(a.At("outerDone")));
		}


		// Code after a ret that no branch goes to is dead. A jmp over nothing
		// or over dead code does nothing, and so does a bnt to its
		// fall-through. A dead branch gives no label.
		TEST_METHOD(LiveCode_DeadCodeAndNoOpBranches)
		{
			ScopeAsm a(R"(
				lap 1
				bnt else
				ldi 1
				ret
			deadJmp:
				jmp end
			else:
				ldi 2
			overNothing:
				jmp next
			next:
				jmp afterDead
			deadLoad:
				ldi 5
			afterDead:
				bnt end
			end:
				ret
			)");
			CodeModel model(a.code);
			Assert::IsFalse(model.IsLive(a.At("deadJmp")));
			Assert::IsFalse(model.IsLive(a.At("deadLoad")));
			Assert::IsTrue(model.IsLive(a.At("else")));
			Assert::IsTrue(model.IsNoOp(a.At("overNothing")));
			Assert::IsTrue(model.IsNoOp(a.At("next")), L"a jmp over dead code");
			Assert::IsTrue(model.IsNoOp(a.At("afterDead")), L"a bnt to its fall-through");
			Assert::IsFalse(model.IsNoOp(1), L"the bnt to else does something");
			Assert::IsFalse(model.IsFlowBranch(a.At("next")));
			Assert::IsTrue(model.IsFlowBranch(1));
			Assert::AreEqual(a.At("else"), model.NextLive(a.At("deadJmp")));

			// The dead jmp to end is not a source of end.
			const std::vector<int> &sources = model.Sources(a.At("end"));
			Assert::AreEqual((size_t)1, sources.size());
			Assert::AreEqual(a.At("afterDead"), sources[0]);
			Assert::IsTrue(model.IsLabel(a.At("else")));
			Assert::IsFalse(model.IsLabel(a.At("deadLoad")));
		}

		// Sierra's (<= a (b x?) c): the bnt inside the chain goes where the
		// end of the chain goes for a false value, so it is part of the value.
		// The bnt after the chain is a branch of the control flow.
		TEST_METHOD(NaryCompare_SierraChainIsInert)
		{
			ScopeAsm a(R"(
				lsp 2
				pushi 5
				push0
				lap 1
				send 4
				le?
			chainBnt:
				bnt fail
				pprev
				lap 3
				le?
			ifBnt:
				bnt fail
				ldi 1
				sat 0
			fail:
				lat 0
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.IsInert(a.At("chainBnt")));
			Assert::IsFalse(model.IsFlowBranch(a.At("chainBnt")));
			Assert::IsFalse(model.IsInert(a.At("ifBnt")));
			Assert::IsTrue(model.IsFlowBranch(a.At("ifBnt")));
		}

		// A value chain (= t (< a 5 10)): the bnt goes to the end of the chain.
		// Three operands give two inert bnts.
		TEST_METHOD(NaryCompare_ValueChainIsInert)
		{
			ScopeAsm a(R"(
				lsp 1
				ldi 5
				lt?
			first:
				bnt end
				pprev
				ldi 10
				lt?
			second:
				bnt end
				pprev
				ldi 20
				lt?
			end:
				sat 0
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.IsInert(a.At("first")));
			Assert::IsTrue(model.IsInert(a.At("second")));
		}

		// Negative check of the n-ary rule: a bnt that goes to another place
		// than the end of the chain is a branch of the control flow, and the
		// other bnt of the chain stays a branch too.
		TEST_METHOD(NaryCompare_ABntToAnotherPlaceIsNotInert)
		{
			ScopeAsm a(R"(
				lsp 1
				ldi 5
				lt?
			first:
				bnt other
				pprev
				ldi 10
				lt?
			second:
				bnt end
				pprev
				ldi 20
				lt?
			end:
				sat 0
				ret
			other:
				ldi 0
				ret
			)");
			CodeModel model(a.code);
			Assert::IsFalse(model.IsInert(a.At("first")));
			Assert::IsFalse(model.IsInert(a.At("second")));
		}

		// The depth before each instruction, and the head of each switch: the
		// push of the value that its toss takes. A switch inside a case has
		// its own head.
		TEST_METHOD(Switches_TheTossFindsItsHead)
		{
			ScopeAsm a(R"(
			outerHead:
				lsp 1
				dup
				ldi 1
				eq?
				bnt c2
				pushi 0
				callk 5 0
				jmp done
			c2:
				dup
				ldi 2
				eq?
				bnt done
			innerHead:
				lsp 2
				dup
				ldi 3
				eq?
				bnt innerDone
				ldi 7
			innerDone:
				toss
			done:
				toss
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(0, model.DepthBefore(a.At("outerHead")));
			Assert::AreEqual(1, model.DepthBefore(a.At("c2")));
			Assert::AreEqual(2, model.DepthBefore(a.At("innerDone")));
			Assert::AreEqual(1, model.DepthBefore(a.At("done")));
			Assert::AreEqual(0, model.DepthAfter(a.At("done")));
			Assert::AreEqual(a.At("innerHead"), model.SwitchHead(a.At("innerDone")));
			Assert::AreEqual(a.At("outerHead"), model.SwitchHead(a.At("done")));
			Assert::AreEqual((size_t)2, model.Tosses().size());
			Assert::IsFalse(model.HasStackUnderflow());
		}

		// A break out of a switch leaves the switch value on the stack: the
		// exit of the loop has two depths, and the depth is the smaller one.
		// The loop has one head; its latch is the last back branch.
		TEST_METHOD(Depths_ABreakOutOfASwitchGivesTheSmallerDepth)
		{
			ScopeAsm a(R"(
			head:
				lap 1
				bnt exit
			switchHead:
				lsp 2
				dup
				ldi 1
				eq?
				bnt switchDone
				jmp exit
			switchDone:
				toss
			latch:
				jmp head
			exit:
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(0, model.DepthBefore(a.At("exit")));
			Assert::IsTrue(model.HasDepthConflict(a.At("exit")));
			Assert::IsFalse(model.HasDepthConflict(a.At("switchDone")));
			Assert::AreEqual(a.At("switchHead"), model.SwitchHead(a.At("switchDone")));
			Assert::AreEqual((size_t)1, model.LoopHeads().size());
			Assert::AreEqual(a.At("head"), model.LoopHeads()[0]);
			Assert::AreEqual(a.At("latch"), model.Latch(a.At("head")));
			Assert::AreEqual(NoIndex, model.Latch(a.At("exit")));
		}

		// The break out of the switch gets to the exit first (depth 1), the
		// breakif after the toss later (depth 0): the depth is the smaller one
		// also when it comes last.
		TEST_METHOD(Depths_TheSmallerDepthWinsWhenItComesLast)
		{
			ScopeAsm a(R"(
			head:
				lsp 2
				dup
				ldi 1
				eq?
				bnt switchDone
				jmp exit
			switchDone:
				toss
				lap 1
				bt exit
				jmp head
			exit:
				ldi 5
			last:
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(0, model.DepthBefore(a.At("exit")));
			Assert::IsTrue(model.HasDepthConflict(a.At("exit")));
			// Only the merge point has two paths.
			Assert::IsFalse(model.HasDepthConflict(a.At("last")));
		}

		// The entry of the function is a path too: a loop head at the start
		// that a back branch reaches with another depth has a conflict.
		TEST_METHOD(Depths_TheEntryIsAPath)
		{
			ScopeAsm a(R"(
			head:
				lap 1
				push
				lap 2
				bnt head
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.HasDepthConflict(a.At("head")));
		}

		// A toss with nothing on the stack.
		TEST_METHOD(Depths_AnUnderflowIsRecorded)
		{
			ScopeAsm a(R"(
				toss
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.HasStackUnderflow());
			Assert::AreEqual(NoIndex, model.SwitchHead(0));
		}

		// Camelot script 40, Rm40::handleEvent: after the toss of a switch, a
		// bnt goes back into the arguments of a call, past the push of the
		// argument count. It gets there with one value less than the
		// fall-through: it is stray, a branch to the next instruction.
		TEST_METHOD(Stray_ABntIntoTheArgumentsOfACallGoesToTheNextInstruction)
		{
			ScopeAsm a(R"(
				lal 0
				bnt other
				push0
				callk 1 0
				jmp end
			other:
				push1
			args:
				lal 1
				push
				callk 5 2
				bnt end
				lsp 2
				dup
				ldi 1
				eq?
				bnt caseElse
				push0
				callk 2 0
				jmp switchDone
			caseElse:
				push0
				callk 3 0
			switchDone:
				toss
			stray:
				bnt args
			end:
				ret
			)");
			CodeModel model(a.code);
			int stray = a.At("stray");
			Assert::IsTrue(model.IsStray(stray));
			Assert::AreEqual(a.At("end"), model.Target(stray));
			Assert::AreEqual(a.At("end"), model.ParseTarget(stray));
			Assert::AreEqual(a.At("args"), model.BytecodeTarget(stray));
			Assert::IsTrue(model.IsNoOp(stray));
			Assert::AreEqual(a.At("end"), model.Resolve(stray, Arrival::False));
			Assert::IsFalse(model.IsLabel(a.At("args")));
			Assert::IsFalse(model.HasDepthConflict(a.At("args")));
			Assert::IsFalse(model.HasStackUnderflow());
			Assert::IsTrue(model.LoopHeads().empty());
			Assert::AreEqual(1, model.DepthBefore(a.At("args")));
			for (int i = 0; i < model.Size(); ++i)
			{
				Assert::AreEqual(i == stray, model.IsStray(i));
			}
		}

		// In a function with a stray branch, a branch that gets to its target
		// with more values than the fall-through (a break out of a switch) is
		// no stray.
		TEST_METHOD(Stray_ABranchWithMoreValuesIsNoStray)
		{
			ScopeAsm a(R"(
				push1
			args:
				lal 1
				push
				callk 5 2
				lsp 2
				dup
				ldi 1
				eq?
				bnt switchDone
				lap 1
			brk:
				bt exit
			switchDone:
				toss
			stray:
				bnt args
				ldi 3
			exit:
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.IsStray(a.At("stray")));
			Assert::IsFalse(model.IsStray(a.At("brk")));
			Assert::AreEqual(a.At("exit"), model.Target(a.At("brk")));
			Assert::IsTrue(model.HasDepthConflict(a.At("exit")));
			Assert::IsFalse(model.HasStackUnderflow());
		}

		// The Camelot shape in the first case of an outer switch: the test of
		// the if after the call goes to the toss of the outer switch (Sierra
		// threads it through the jmp at the end of the case). The stray
		// branch lowers the depth of the call, so the test gets to the toss
		// with fewer values than the fall-through too. Only the branch that
		// the code needs for no underflow is stray: the test stays.
		TEST_METHOD(Stray_OnlyTheBranchesThatTheCodeNeedsAreStray)
		{
			ScopeAsm a(R"(
				lsp 0
				dup
				ldi 1
				eq?
				bnt case2
				push1
			args:
				lal 1
				push
				callk 5 2
			test:
				bnt outerDone
				lsp 2
				dup
				ldi 1
				eq?
				bnt innerElse
				push0
				callk 2 0
				jmp innerDone
			innerElse:
				push0
				callk 3 0
			innerDone:
				toss
			stray:
				bnt args
				jmp outerDone
			case2:
				dup
				ldi 2
				eq?
				bnt outerDone
				push0
				callk 4 0
			outerDone:
				toss
				ret
			)");
			CodeModel model(a.code);
			Assert::IsTrue(model.IsStray(a.At("stray")));
			Assert::IsFalse(model.IsStray(a.At("test")));
			Assert::AreEqual(a.At("outerDone"), model.Target(a.At("test")));
			Assert::IsFalse(model.HasStackUnderflow());
		}

		// A branch to code that no fall-through gets to is no candidate,
		// also when the code there underflows and is dead without the branch.
		TEST_METHOD(Stray_ABranchToCodeThatNoFallThroughGetsToIsNoStray)
		{
			ScopeAsm a(R"(
				lap 0
			test:
				bnt bad
				ret
			bad:
				toss
				ret
			)");
			CodeModel model(a.code);
			Assert::IsFalse(model.IsStray(a.At("test")));
			Assert::AreEqual(a.At("bad"), model.Target(a.At("test")));
			Assert::IsTrue(model.IsLive(a.At("bad")));
			Assert::IsTrue(model.HasStackUnderflow());
		}

		// The fall-through leaves values that nothing takes (Quest for Glory
		// IV CD script 10, wisps::init: a send frame smaller than its pushes):
		// no underflow, so the test of the if is no stray.
		TEST_METHOD(Stray_ExtraValuesThatNothingTakesGiveNoStray)
		{
			ScopeAsm a(R"(
				lsp 0
				ldi 2
				lt?
			test:
				bnt end
				push1
				push1
				push1
				pushSelf
				self 2
			end:
				ret
			)");
			CodeModel model(a.code);
			Assert::IsFalse(model.IsStray(a.At("test")));
			Assert::AreEqual(a.At("end"), model.Target(a.At("test")));
			Assert::IsTrue(model.HasDepthConflict(a.At("end")));
			Assert::IsFalse(model.HasStackUnderflow());
		}

		// With an underflow that the candidates do not remove, no branch is
		// stray.
		TEST_METHOD(Stray_NoStrayWhenTheUnderflowStays)
		{
			ScopeAsm a(R"(
				push1
			args:
				lal 1
				push
				callk 5 2
			test:
				bnt args
				toss
				ret
			)");
			CodeModel model(a.code);
			Assert::IsFalse(model.IsStray(a.At("test")));
			Assert::AreEqual(a.At("args"), model.Target(a.At("test")));
			Assert::IsTrue(model.HasStackUnderflow());
			Assert::IsTrue(model.IsLabel(a.At("args")));
		}

		// A while that is the first statement of a repeat (King's Quest V,
		// setControls::doit): three back branches to one head, in address
		// order. The latch is the last one.
		TEST_METHOD(Loops_BackBranchesOfASharedHead)
		{
			ScopeAsm a(R"(
			head:
				lst 0
				ldi 10
				lt?
				bnt whileDone
				lap 1
				bt whileDone
				+at 0
				lat 1
			ifBack:
				bnt head
				+at 1
			whileLatch:
				jmp head
			whileDone:
				lst 1
				ldi 5
				gt?
				bt done
				+at 1
			repeatLatch:
				jmp head
			done:
				ret
			)");
			CodeModel model(a.code);
			const std::vector<int> &back = model.BackBranches(a.At("head"));
			Assert::AreEqual((size_t)3, back.size());
			Assert::AreEqual(a.At("ifBack"), back[0]);
			Assert::AreEqual(a.At("whileLatch"), back[1]);
			Assert::AreEqual(a.At("repeatLatch"), back[2]);
			Assert::AreEqual(a.At("repeatLatch"), model.Latch(a.At("head")));
			Assert::IsTrue(model.IsBackBranch(a.At("ifBack")));
			Assert::IsFalse(model.IsBackBranch(a.At("repeatLatch") - 2), L"the bt to done goes forward");
		}

		// (while c A (if d (continue)) B (return)): the latch after the
		// return is dead, and it is still the latch of the loop.
		TEST_METHOD(Loops_ADeadLatchEndsTheLoop)
		{
			ScopeAsm a(R"(
			head:
				lap 1
				bnt exit
				+at 0
				lap 2
				bnt skip
			continue:
				jmp head
			skip:
				+at 1
				ret
			latch:
				jmp head
			exit:
				ret
			)");
			CodeModel model(a.code);
			Assert::IsFalse(model.IsLive(a.At("latch")));
			Assert::AreEqual(a.At("latch"), model.Latch(a.At("head")));
			Assert::AreEqual((size_t)2, model.BackBranches(a.At("head")).size());
			Assert::AreEqual((size_t)1, model.LoopHeads().size());
		}

		// This repository's compiler: (if (or a b) X) gives a bt past the
		// bnt of the if. The parser reads the bt as a bt to that bnt.
		TEST_METHOD(Dialect_ABtPastABntGoesToTheBnt)
		{
			ScopeAsm a(R"(
				lap 1
			orBt:
				bt then
				lap 2
			ifBnt:
				bnt else
			then:
				ldi 1
				sat 0
			else:
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(a.At("then"), model.Target(a.At("orBt")));
			Assert::AreEqual(a.At("ifBnt"), model.ParseTarget(a.At("orBt")));
			Assert::AreEqual(a.At("else"), model.ParseTarget(a.At("ifBnt")));
			Assert::IsTrue(model.SameTarget(model.Target(a.At("orBt")), model.ParseTarget(a.At("orBt")), Arrival::True));
		}

		// This repository's compiler: (if (or (and a b) c) X) gives a bnt past
		// the bt of the or. The parser reads the bnt as a bnt to that bt.
		TEST_METHOD(Dialect_ABntPastABtGoesToTheBt)
		{
			ScopeAsm a(R"(
				lap 1
			andBnt:
				bnt c
				lap 2
			orBt:
				bt then
			c:
				lap 3
			ifBnt:
				bnt end
			then:
				ldi 1
			end:
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(a.At("orBt"), model.ParseTarget(a.At("andBnt")));
			Assert::AreEqual(a.At("ifBnt"), model.ParseTarget(a.At("orBt")));
		}

		// The dialect pass moves a bnt onto any forward bt just before its
		// target, and keeps the threaded target: the parser, which knows the
		// loops, takes the threaded target back when the bt is a breakif
		// (TestScopeParser::Dialect_ABntPastABreakIfKeepsItsTarget).
		TEST_METHOD(Dialect_ABntPastABreakIfHasBothTargets)
		{
			ScopeAsm a(R"(
			head:
				lap 1
				bnt exit
				lap 2
			ifBnt:
				bnt skip
				lap 3
				bt exit
			skip:
				+at 0
				jmp head
			exit:
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(a.At("skip") - 1, model.ParseTarget(a.At("ifBnt")));
			Assert::AreEqual(a.At("skip"), model.ThreadedTarget(a.At("ifBnt")));
		}

		// Dead code that jumps back into a loop, and that is not where the
		// loop ends (no branch of the loop goes after it), is no latch.
		TEST_METHOD(Loops_ADeadBackBranchElsewhereIsNoLatch)
		{
			ScopeAsm a(R"(
			head:
				lap 1
				bnt exit
				+at 0
			latch:
				jmp head
			exit:
				ret
				ldi 1
				jmp head
				ldi 2
				ret
			)");
			CodeModel model(a.code);
			Assert::AreEqual(a.At("latch"), model.Latch(a.At("head")));
			Assert::AreEqual((size_t)1, model.BackBranches(a.At("head")).size());
		}

		// A branch to an instruction that is not in the function stops the
		// code model with a stable id.
		TEST_METHOD(Construct_ABadBranchTargetThrows)
		{
			std::list<scii> code;
			code.push_back(scii(sciVersion1_1, Opcode::INDETERMINATE, -1));
			code.push_back(scii(sciVersion1_1, Opcode::JMP, code.begin(), false, -1));
			code.back().set_offset_and_size(0x20, 3);
			code.push_back(scii(sciVersion1_1, Opcode::RET, -1));
			bool thrown = false;
			try
			{
				CodeModel model(code);
			}
			catch (const ScopeError &e)
			{
				thrown = (e.Stage() == "code") && (e.Id() == "bad-branch-target") && (e.Offset() == 0x20) &&
					(std::string(e.what()) == "[scope:code:bad-branch-target]");
			}
			Assert::IsTrue(thrown);
		}

		// The dump has one line for each instruction, with the depth and the
		// flags.
		TEST_METHOD(Dump_OneLineForEachInstruction)
		{
			ScopeAsm a(R"(
				lap 1
				bnt end
				ldi 7
			end:
				ret
			)");
			CodeModel model(a.code);
			std::string expected =
				"0000 lap    1                  d0\n"
				"0001 bnt    0003               d0\n"
				"0002 ldi    7                  d0\n"
				"0003 ret                       d0 label\n";
			Assert::AreEqual(expected, model.Dump());
		}

		// A stray branch has the next instruction as its target, and the
		// target of the bytecode in its flags.
		TEST_METHOD(Dump_AStrayBranchHasTheTargetOfTheBytecode)
		{
			ScopeAsm a(R"(
				push1
			args:
				lal 1
				push
				callk 5 2
				bnt args
				ret
			)");
			CodeModel model(a.code);
			std::string dump = model.Dump();
			std::string line = "0004 bnt    0005               d0 no-op stray 0001\n";
			Assert::IsTrue(dump.find(line) != std::string::npos, std::wstring(dump.begin(), dump.end()).c_str());
		}
	};
}
