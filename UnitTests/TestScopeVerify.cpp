#include "stdafx.h"
#include "CppUnitTest.h"
#include "ScopeAsm.h"
#include "ScopeCode.h"
#include "ScopeRegion.h"
#include "ScopeVerify.h"
#include "format.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace scope;

namespace UnitTests
{
	namespace
	{
		std::wstring Wide(const std::string &text)
		{
			return std::wstring(text.begin(), text.end());
		}

		// The tree verifies against the code, and its dump is its text.
		void AssertVerifies(const std::string &code, const std::string &tree)
		{
			ScopeAsm a(code);
			CodeModel model(a.code);
			std::unique_ptr<Region> root = ParseRegions(tree);
			Assert::AreEqual(tree, Dump(model, *root), L"the dump of the tree");
			try
			{
				Verify(model, *root);
			}
			catch (const ScopeError &e)
			{
				Assert::Fail(Wide(fmt::format("{0} at {1:04x}: {2}\n{3}", e.what(), e.Offset(), e.Detail(), model.Dump())).c_str());
			}
		}

		// Verify rejects the tree with the id, at the address.
		void AssertRejects(const std::string &code, const std::string &tree, const std::string &id, int offset)
		{
			ScopeAsm a(code);
			CodeModel model(a.code);
			std::unique_ptr<Region> root = ParseRegions(tree);
			try
			{
				Verify(model, *root);
			}
			catch (const ScopeError &e)
			{
				std::string text = fmt::format("{0} at {1:04x}: {2}", e.what(), e.Offset(), e.Detail());
				Assert::AreEqual(std::string("verify"), e.Stage(), Wide(text).c_str());
				Assert::AreEqual(id, e.Id(), Wide(text).c_str());
				Assert::AreEqual(offset, e.Offset(), Wide(text).c_str());
				return;
			}
			Assert::Fail(L"the tree verifies");
		}

		// (if a (if b X) else Y). Sierra's compiler threads the bnt of the
		// inner if to the end of the outer if.
		const char *NestedIfPlain = R"(
				lap 1
				bnt else
				lap 2
				bnt innerEnd
				ldi 1
				sat 0
			innerEnd:
				jmp end
			else:
				ldi 2
				sat 0
			end:
				ret
			)";
		const char *NestedIfSierra = R"(
				lap 1
				bnt else
				lap 2
				bnt end
				ldi 1
				sat 0
				jmp end
			else:
				ldi 2
				sat 0
			end:
				ret
			)";
		const std::string NestedIfTree =
			"code 0000\n"
			"if 0001\n"
			"  then\n"
			"    code 0002\n"
			"    if 0003\n"
			"      then\n"
			"        code 0004-0005\n"
			"  else 0006\n"
			"    code 0007-0008\n"
			"code 0009\n";

		// (if (and a b) X else Y): plain, the first bnt goes to the second.
		const char *AndPlain = R"(
				lap 1
				bnt join
				lap 2
			join:
				bnt else
				ldi 1
				jmp end
			else:
				ldi 2
			end:
				ret
			)";
		const char *AndThreaded = R"(
				lap 1
				bnt else
				lap 2
				bnt else
				ldi 1
				jmp end
			else:
				ldi 2
			end:
				ret
			)";
		const std::string AndTree =
			"code 0000\n"
			"if 0001 0003\n"
			"  term\n"
			"    code 0002\n"
			"  then\n"
			"    code 0004\n"
			"  else 0005\n"
			"    code 0006\n"
			"code 0007\n";

		// (if (or a b) X): Sierra's bt goes to the bnt of the if; this
		// repository's compiler's bt goes past it, to the then-part.
		const char *OrSierra = R"(
				lap 1
				bt test
				lap 2
			test:
				bnt end
				ldi 1
			end:
				ret
			)";
		const char *OrCompanion = R"(
				lap 1
				bt then
				lap 2
				bnt end
			then:
				ldi 1
			end:
				ret
			)";
		const std::string OrTree =
			"code 0000\n"
			"or 0001\n"
			"  code 0002\n"
			"if 0003\n"
			"  then\n"
			"    code 0004\n"
			"code 0005\n";

		// (if (or (and a b) c) X): Sierra's bnt goes to the bt of the or;
		// this repository's compiler's bnt goes past it, to c.
		const char *OrAndSierra = R"(
				lap 1
				bnt orBt
				lap 2
			orBt:
				bt test
				lap 3
			test:
				bnt end
				ldi 1
			end:
				ret
			)";
		const char *OrAndCompanion = R"(
				lap 1
				bnt c
				lap 2
				bt then
			c:
				lap 3
				bnt end
			then:
				ldi 1
			end:
				ret
			)";
		const std::string OrAndTree =
			"code 0000\n"
			"if 0001\n"
			"  then\n"
			"    code 0002\n"
			"or 0003\n"
			"  code 0004\n"
			"if 0005\n"
			"  then\n"
			"    code 0006\n"
			"code 0007\n";

		// (= t (or a (and b c))): both branches go to the store.
		const char *ValueOrAnd = R"(
				lap 1
				bt store
				lap 2
				bnt store
				lap 3
			store:
				sat 0
				ret
			)";
		const std::string ValueOrAndTree =
			"code 0000\n"
			"or 0001\n"
			"  code 0002\n"
			"  if 0003\n"
			"    then\n"
			"      code 0004\n"
			"code 0005-0006\n";

		// (while a (if b (break)) (if c (continue)) (if d X)): Sierra's
		// compiler threads the bnt of the last if to the head.
		const char *LoopPlain = R"(
			head:
				lap 1
				bnt exit
				lap 2
				bnt noBreak
				jmp exit
			noBreak:
				lap 3
				bnt noContinue
				jmp head
			noContinue:
				lap 4
				bnt latch
				+at 0
			latch:
				jmp head
			exit:
				ret
			)";
		const char *LoopSierra = R"(
			head:
				lap 1
				bnt exit
				lap 2
				bnt noBreak
				jmp exit
			noBreak:
				lap 3
				bnt noContinue
				jmp head
			noContinue:
				lap 4
				bnt head
				+at 0
				jmp head
			exit:
				ret
			)";
		const std::string LoopTree =
			"loop 0000 latch 000b\n"
			"  body\n"
			"    code 0000\n"
			"    if 0001\n"
			"      then\n"
			"        code 0002\n"
			"        if 0003\n"
			"          then\n"
			"            break 1 0004\n"
			"        code 0005\n"
			"        if 0006\n"
			"          then\n"
			"            continue 1 0007\n"
			"        code 0008\n"
			"        if 0009\n"
			"          then\n"
			"            code 000a\n"
			"      else break 1\n"
			"code 000c\n";

		// (switch x (1 A) (2 B) (else C)).
		const char *Switch = R"(
				lsp 1
				dup
				ldi 1
				eq?
				bnt case2
				ldi 5
				sat 0
				jmp done
			case2:
				dup
				ldi 2
				eq?
				bnt caseElse
				ldi 6
				sat 0
				jmp done
			caseElse:
				ldi 7
				sat 0
			done:
				toss
				ret
			)";
		const std::string SwitchTree =
			"switch 0000 toss 0011\n"
			"  case bnt 0004 jmp 0007\n"
			"    value\n"
			"      code 0001-0003\n"
			"    body\n"
			"      code 0005-0006\n"
			"  case bnt 000b jmp 000e\n"
			"    value\n"
			"      code 0008-000a\n"
			"    body\n"
			"      code 000c-000d\n"
			"  case\n"
			"    body\n"
			"      code 000f-0010\n"
			"code 0012\n";

		// (if (< a 5 10) X): the bnt inside the chain is inert. Sierra's
		// compiler threads it to the end of the if; plain, it goes to the
		// bnt of the if.
		const char *NaryPlain = R"(
				lsp 1
				ldi 5
				lt?
				bnt test
				pprev
				ldi 10
				lt?
			test:
				bnt end
				+at 0
			end:
				ret
			)";
		const char *NarySierra = R"(
				lsp 1
				ldi 5
				lt?
				bnt end
				pprev
				ldi 10
				lt?
				bnt end
				+at 0
			end:
				ret
			)";
		const std::string NaryTree =
			"code 0000-0006\n"
			"if 0007\n"
			"  then\n"
			"    code 0008\n"
			"code 0009\n";

		// A while in a while; the inner one breaks out of both.
		const char *NestedLoops = R"(
			outer:
				lap 1
				bnt outerExit
			inner:
				lap 2
				bnt innerExit
				lap 3
				bnt skip
				jmp outerExit
			skip:
				jmp inner
			innerExit:
				jmp outer
			outerExit:
				ret
			)";
		const std::string NestedLoopsTree =
			"loop 0000 latch 0008\n"
			"  body\n"
			"    code 0000\n"
			"    if 0001\n"
			"      then\n"
			"        loop 0002 latch 0007\n"
			"          body\n"
			"            code 0002\n"
			"            if 0003\n"
			"              then\n"
			"                code 0004\n"
			"                if 0005\n"
			"                  then\n"
			"                    break 2 0006\n"
			"              else break 1\n"
			"      else break 1\n"
			"code 0009\n";

		// A switch in a loop whose first case jumps to the toss.
		const char *SwitchInLoop = R"(
			head:
				lap 1
				bnt exit
				lsp 2
				dup
				ldi 1
				eq?
				bnt done
				+at 0
				jmp done
			done:
				toss
				jmp head
			exit:
				ret
			)";
	}

	TEST_CLASS(TestScopeVerify)
	{
	public:
		// Each shape verifies with one tree against the bytecode of each
		// dialect: the templates, Sierra's threaded branches, and the "or"
		// forms of this repository's compiler.
		TEST_METHOD(Shapes_OneTreeForEachDialect)
		{
			AssertVerifies(NestedIfPlain, NestedIfTree);
			AssertVerifies(NestedIfSierra, NestedIfTree);
			AssertVerifies(AndPlain, AndTree);
			AssertVerifies(AndThreaded, AndTree);
			AssertVerifies(OrSierra, OrTree);
			AssertVerifies(OrCompanion, OrTree);
			AssertVerifies(OrAndSierra, OrAndTree);
			AssertVerifies(OrAndCompanion, OrAndTree);
			AssertVerifies(ValueOrAnd, ValueOrAndTree);
			AssertVerifies(LoopPlain, LoopTree);
			AssertVerifies(LoopSierra, LoopTree);
			AssertVerifies(Switch, SwitchTree);
			AssertVerifies(NaryPlain, NaryTree);
			AssertVerifies(NarySierra, NaryTree);
			AssertVerifies(NestedLoops, NestedLoopsTree);
		}

		// A for loop: the step is the continue point.
		TEST_METHOD(Shapes_ForLoopWithContinue)
		{
			AssertVerifies(R"(
				ldi 0
				sat 0
			head:
				lst 0
				ldi 10
				lt?
				bnt exit
				lap 1
				bnt step
				+at 1
			step:
				+at 0
				jmp head
			exit:
				ret
			)",
				"code 0000-0001\n"
				"loop 0002 latch 000a\n"
				"  body\n"
				"    code 0002-0004\n"
				"    if 0005\n"
				"      then\n"
				"        code 0006\n"
				"        if 0007\n"
				"          then\n"
				"            code 0008\n"
				"      else break 1\n"
				"  step\n"
				"    code 0009\n"
				"code 000b\n");
		}

		// breakif and contif: a bt to the exit and to the head.
		TEST_METHOD(Shapes_BreakIfAndContIf)
		{
			AssertVerifies(R"(
			head:
				lap 1
				bt exit
				lap 2
				bt head
				+at 0
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 0005\n"
				"  body\n"
				"    code 0000\n"
				"    breakif 1 0001\n"
				"    code 0002\n"
				"    contif 1 0003\n"
				"    code 0004\n"
				"code 0006\n");
		}

		// Dead code stays in the tree: the jmp before the else is dead after
		// a ret, and the code after the last ret is dead.
		TEST_METHOD(Shapes_DeadCodeIsInTheTree)
		{
			AssertVerifies(R"(
				lap 1
				bnt else
				ldi 1
				ret
				jmp end
			else:
				ldi 2
				ret
			end:
				ldi 3
				ret
			)",
				"code 0000\n"
				"if 0001\n"
				"  then\n"
				"    code 0002-0003\n"
				"  else 0004\n"
				"    code 0005-0006\n"
				"code 0007-0008\n");
		}

		// Negative check: the then-part and the else-part change places.
		TEST_METHOD(Reject_SwappedArms)
		{
			AssertRejects(NestedIfSierra,
				"code 0000\n"
				"if 0001\n"
				"  then\n"
				"    code 0007-0008\n"
				"  else 0006\n"
				"    code 0002\n"
				"    if 0003\n"
				"      then\n"
				"        code 0004-0005\n"
				"code 0009\n",
				"layout", 0x0002);
		}

		// Negative check: a code region is not in the tree.
		TEST_METHOD(Reject_DroppedLeaf)
		{
			AssertRejects(AndThreaded,
				"code 0000\n"
				"if 0001 0003\n"
				"  term\n"
				"    code 0002\n"
				"  then\n"
				"  else 0005\n"
				"    code 0006\n"
				"code 0007\n",
				"layout", 0x0004);
		}

		// Negative check: a break of the inner loop where the bytecode leaves
		// both loops. The test before it goes to another place.
		TEST_METHOD(Reject_WrongBreakLevel)
		{
			AssertRejects(NestedLoops,
				"loop 0000 latch 0008\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        loop 0002 latch 0007\n"
				"          body\n"
				"            code 0002\n"
				"            if 0003\n"
				"              then\n"
				"                code 0004\n"
				"                if 0005\n"
				"                  then\n"
				"                    break 1 0006\n"
				"              else break 1\n"
				"      else break 1\n"
				"code 0009\n",
				"successor", 0x0004);
		}

		// Negative check: a break of a loop that is not there.
		TEST_METHOD(Reject_BreakWithNoLoop)
		{
			AssertRejects(NestedLoops,
				"loop 0000 latch 0008\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        loop 0002 latch 0007\n"
				"          body\n"
				"            code 0002\n"
				"            if 0003\n"
				"              then\n"
				"                code 0004\n"
				"                if 0005\n"
				"                  then\n"
				"                    break 3 0006\n"
				"              else break 1\n"
				"      else break 1\n"
				"code 0009\n",
				"level", 0x0006);
		}

		// Negative check: the jmp to the toss is the end of a case, not a
		// break of the loop.
		TEST_METHOD(Reject_BreakToTheToss)
		{
			const std::string right =
				"loop 0000 latch 000a\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        switch 0002 toss 0009\n"
				"          case bnt 0006 jmp 0008\n"
				"            value\n"
				"              code 0003-0005\n"
				"            body\n"
				"              code 0007\n"
				"      else break 1\n"
				"code 000b\n";
			AssertVerifies(SwitchInLoop, right);
			AssertRejects(SwitchInLoop,
				"loop 0000 latch 000a\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        switch 0002 toss 0009\n"
				"          case bnt 0006\n"
				"            value\n"
				"              code 0003-0005\n"
				"            body\n"
				"              code 0007\n"
				"              break 1 0008\n"
				"      else break 1\n"
				"code 000b\n",
				"successor", 0x0007);
		}

		// Negative check: an or where the bytecode has an and. The layout is
		// right; the opcode of the branch is not.
		TEST_METHOD(Reject_WrongOpcode)
		{
			AssertRejects(AndThreaded,
				"code 0000\n"
				"or 0001\n"
				"  code 0002\n"
				"if 0003\n"
				"  then\n"
				"    code 0004\n"
				"  else 0005\n"
				"    code 0006\n"
				"code 0007\n",
				"opcode", 0x0001);
		}

		// Negative check: a branch of the control flow inside a code region.
		TEST_METHOD(Reject_BranchInCode)
		{
			AssertRejects(OrSierra,
				"code 0000-0002\n"
				"if 0003\n"
				"  then\n"
				"    code 0004\n"
				"code 0005\n",
				"branch-in-code", 0x0001);
		}

		// Negative check: a nested if where the bytecode has an and with an
		// else. The false value of the second test goes to the else-part, not
		// to the end of the then-part.
		TEST_METHOD(Reject_NestedIfForAnAndWithElse)
		{
			AssertRejects(AndThreaded,
				"code 0000\n"
				"if 0001\n"
				"  then\n"
				"    code 0002\n"
				"    if 0003\n"
				"      then\n"
				"        code 0004\n"
				"  else 0005\n"
				"    code 0006\n"
				"code 0007\n",
				"successor", 0x0002);
		}

		// A test at the start of the function: the entry is checked too.
		TEST_METHOD(Entry_ATestAtTheStart)
		{
			const char *code = R"(
				bnt end
				ldi 1
				sat 0
			end:
				ret
			)";
			AssertVerifies(code,
				"if 0000\n"
				"  then\n"
				"    code 0001-0002\n"
				"code 0003\n");
			AssertRejects(code,
				"if 0000\n"
				"  then\n"
				"code 0001-0003\n",
				"entry", 0x0000);
		}

		// A jmp over dead code does nothing: in a code region, control goes
		// past the dead code after it, as in the bytecode.
		TEST_METHOD(Shapes_NoOpJmpOverDeadCode)
		{
			AssertVerifies(R"(
			top:
				lap 1
				bnt exit
				+at 0
				jmp exit
				jmp top
			exit:
				ret
			)",
				"code 0000\n"
				"if 0001\n"
				"  then\n"
				"    code 0002-0004\n"
				"code 0005\n");
		}

		// Negative check: an if whose test is the inert bnt of an n-ary
		// compare. The control flow is the same, but the compare is one value.
		TEST_METHOD(Reject_InertBntAsATest)
		{
			AssertRejects(NarySierra,
				"code 0000-0002\n"
				"if 0003\n"
				"  then\n"
				"    code 0004-0006\n"
				"    if 0007\n"
				"      then\n"
				"        code 0008\n"
				"code 0009\n",
				"opcode", 0x0003);
		}

		// Negative check: the head of a loop is not the target of its latch.
		TEST_METHOD(Reject_WrongLoopHead)
		{
			std::string tree = LoopTree;
			tree.replace(tree.find("loop 0000"), 9, "loop 0005");
			AssertRejects(LoopSierra, tree, "loop-head", 0x000b);
		}
	};
}
