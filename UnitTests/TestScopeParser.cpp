#include "stdafx.h"
#include "CppUnitTest.h"
#include "DecompileHelper.h"
#include "Helper.h"
#include "ScopeAsm.h"
#include "ScopeCode.h"
#include "ScopeParser.h"
#include "ScopeRegion.h"
#include "ScopeShapes.h"
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

		// The parser gives the tree, and the tree verifies.
		void AssertParses(const std::string &code, const std::string &tree)
		{
			ScopeAsm a(code);
			CodeModel model(a.code);
			std::unique_ptr<Region> root;
			try
			{
				root = Parse(model);
			}
			catch (const ScopeError &e)
			{
				Assert::Fail(Wide(fmt::format("{0} at {1:04x}: {2}\n{3}", e.what(), e.Offset(), e.Detail(), model.Dump())).c_str());
			}
			Assert::AreEqual(tree, Dump(model, *root));
			try
			{
				Verify(model, *root);
			}
			catch (const ScopeError &e)
			{
				Assert::Fail(Wide(fmt::format("{0} at {1:04x}: {2}", e.what(), e.Offset(), e.Detail())).c_str());
			}
		}

		// The parser stops with the id, at the address.
		void AssertParseFails(const std::string &code, const std::string &id, int offset)
		{
			ScopeAsm a(code);
			CodeModel model(a.code);
			try
			{
				Parse(model);
			}
			catch (const ScopeError &e)
			{
				std::string text = fmt::format("{0} at {1:04x}: {2}", e.what(), e.Offset(), e.Detail());
				Assert::AreEqual(std::string("parse"), e.Stage(), Wide(text).c_str());
				Assert::AreEqual(id, e.Id(), Wide(text).c_str());
				Assert::AreEqual(offset, e.Offset(), Wide(text).c_str());
				return;
			}
			Assert::Fail(L"the code parses");
		}
	}

	TEST_CLASS(TestScopeParser)
	{
	public:
		TEST_METHOD_CLEANUP(CleanUp)
		{
			if (!_gameFolder.empty())
			{
				CleanUpGame(_gameFolder);
				_gameFolder.clear();
			}
		}

		// Each shape gives one tree for each dialect: the tree of the verify
		// tests.
		TEST_METHOD(Shapes_IfAndOr)
		{
			AssertParses(shapes::NestedIfPlain, shapes::NestedIfTree);
			AssertParses(shapes::NestedIfSierra, shapes::NestedIfTree);
			AssertParses(shapes::AndPlain, shapes::AndTree);
			AssertParses(shapes::AndThreaded, shapes::AndTree);
			AssertParses(shapes::OrSierra, shapes::OrTree);
			AssertParses(shapes::OrCompanion, shapes::OrTree);
			AssertParses(shapes::OrAndSierra, shapes::OrAndTree);
			AssertParses(shapes::OrAndCompanion, shapes::OrAndTree);
			AssertParses(shapes::ValueOrAnd, shapes::ValueOrAndTree);
			AssertParses(shapes::NaryPlain, shapes::NaryTree);
			AssertParses(shapes::NarySierra, shapes::NaryTree);
			AssertParses(shapes::DeadCode, shapes::DeadCodeTree);
		}

		// (if c else Y): an empty then-part, and the jmp before the else-part.
		TEST_METHOD(If_EmptyThenPartWithElse)
		{
			AssertParses(R"(
				lap 1
				bnt else
				jmp end
			else:
				ldi 2
				sat 0
			end:
				ret
			)",
				"code 0000\n"
				"if 0001\n"
				"  then\n"
				"  else 0002\n"
				"    code 0003-0004\n"
				"code 0005\n");
		}

		// (if (and a b b) X else Y) as Sierra's compiler emits a nested and:
		// the third test repeats the second. Each is one more and-term.
		TEST_METHOD(If_AndTermsWithAnEmptyTerm)
		{
			AssertParses(R"(
				lap 1
				bnt else
				lap 2
				bnt else
				bnt else
				ldi 1
				jmp end
			else:
				ldi 2
			end:
				ret
			)",
				"code 0000\n"
				"if 0001 0003 0004\n"
				"  term\n"
				"    code 0002\n"
				"  term\n"
				"  then\n"
				"    code 0005\n"
				"  else 0006\n"
				"    code 0007\n"
				"code 0008\n");
		}

		// (if a X else Y) Z: the jmp before the else-part goes to the code
		// after the if, inside the sequence.
		TEST_METHOD(If_TheElseEndsInsideTheSequence)
		{
			AssertParses(R"(
				lap 1
				bnt else
				ldi 1
				jmp after
			else:
				ldi 2
				ret
			after:
				ldi 3
				ret
			)",
				"code 0000\n"
				"if 0001\n"
				"  then\n"
				"    code 0002\n"
				"  else 0003\n"
				"    code 0004-0005\n"
				"code 0006-0007\n");
		}

		// Negative check of the else rule: a jmp before the target that goes
		// out of the enclosing sequence is no else marker, and it goes to no
		// place of a scope.
		TEST_METHOD(If_AJmpOutOfTheSequenceIsNoElse)
		{
			AssertParseFails(R"(
				lap 1
				bnt out
				lap 2
				bnt skip
				ldi 1
				jmp far
			skip:
				ldi 2
			out:
				ret
			far:
				ldi 3
				ret
			)",
				"no-scope-for-target", 0x0005);
		}

		// Shared-then (or (not X) Y) with a synthesized not: a bnt into the
		// then-part of another if. No scope has that place.
		TEST_METHOD(Unstructured_SharedThenBranch)
		{
			AssertParseFails(R"(
				lat 0
				bnt isTrue
				lat 1
				bnt isFalse
			isTrue:
				ldi 1
				jmp store
			isFalse:
				ldi 0
			store:
				sat 4
				ret
			)",
				"no-scope-for-target", 0x0003);
		}

		// A while, with a break, a continue, and an if whose bnt Sierra's
		// compiler threads to the head; and a while in a while. The jmp of the
		// continue goes to the end of the body, so it is the else marker of
		// its if: the rest of the body is the else-part (the tree of the
		// verify tests, with the continue as a statement, has the same
		// control flow).
		TEST_METHOD(Loops_WhileBreakContinue)
		{
			const std::string tree =
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
				"          else 0007\n"
				"            code 0008\n"
				"            if 0009\n"
				"              then\n"
				"                code 000a\n"
				"      else break 1\n"
				"code 000c\n";
			AssertParses(shapes::LoopPlain, tree);
			AssertParses(shapes::LoopSierra, tree);
			AssertParses(shapes::NestedLoops, shapes::NestedLoopsTree);
		}

		// breakif: a bt to the exit. A bt to the head at the end of the body
		// goes to the end of the sequence: an or over the rest of the body
		// (the presentation can make it a contif).
		TEST_METHOD(Loops_BreakIfAndContIf)
		{
			AssertParses(R"(
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
				"    or 0003\n"
				"      code 0004\n"
				"code 0006\n");
			// A contif that is not at the end of the sequence.
			AssertParses(R"(
			head:
				lap 1
				bnt exit
				lap 2
				bnt skip
				lap 3
				bt head
				+at 0
			skip:
				+at 1
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 0008\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        code 0002\n"
				"        if 0003\n"
				"          then\n"
				"            code 0004\n"
				"            contif 1 0005\n"
				"            code 0006\n"
				"        code 0007\n"
				"      else break 1\n"
				"code 0009\n");
		}

		// A while that is the first statement of a repeat (King's Quest V,
		// setControls::doit). As one loop, the bt to the instruction after
		// the jmp of the while has no place; the loop is split at that jmp.
		TEST_METHOD(Loops_SharedHeadIsSplit)
		{
			AssertParses(R"(
			head:
				lst 0
				ldi 10
				lt?
				bnt whileDone
				lap 1
				bt whileDone
				+at 0
				lat 1
				bnt head
				+at 1
				jmp head
			whileDone:
				lst 1
				ldi 5
				gt?
				bt done
				+at 1
				jmp head
			done:
				ret
			)",
				"loop 0000 latch 0010\n"
				"  body\n"
				"    loop 0000 latch 000a\n"
				"      body\n"
				"        code 0000-0002\n"
				"        if 0003\n"
				"          then\n"
				"            code 0004\n"
				"            breakif 1 0005\n"
				"            code 0006-0007\n"
				"            if 0008\n"
				"              then\n"
				"                code 0009\n"
				"          else break 1\n"
				"    code 000b-000d\n"
				"    breakif 1 000e\n"
				"    code 000f\n"
				"code 0011\n");
		}

		// (while c A (if d (continue)) B (return)): the latch after the
		// return is dead. The continue is the else marker of the if.
		TEST_METHOD(Loops_DeadLatch)
		{
			AssertParses(R"(
			head:
				lap 1
				bnt exit
				+at 0
				lap 2
				bnt skip
				jmp head
			skip:
				+at 1
				ret
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 0008\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        code 0002-0003\n"
				"        if 0004\n"
				"          then\n"
				"          else 0005\n"
				"            code 0006-0007\n"
				"      else break 1\n"
				"code 0009\n");
		}

		// A for loop with a continue in an inner if: the jmp to the step has
		// no place, so the loop is parsed again with the step as a scope.
		TEST_METHOD(Loops_ForStepForAContinue)
		{
			AssertParses(R"(
				ldi 0
				sat 0
			head:
				lst 0
				ldi 10
				lt?
				bnt exit
				lap 1
				bnt s1
				lap 2
				bnt s2
				jmp step
			s2:
				+at 2
			s1:
				+at 1
			step:
				+at 0
				jmp head
			exit:
				ret
			)",
				"code 0000-0001\n"
				"loop 0002 latch 000e\n"
				"  body\n"
				"    code 0002-0004\n"
				"    if 0005\n"
				"      then\n"
				"        code 0006\n"
				"        if 0007\n"
				"          then\n"
				"            code 0008\n"
				"            if 0009\n"
				"              then\n"
				"                continue 1 000a\n"
				"            code 000b\n"
				"        code 000c\n"
				"      else break 1\n"
				"  step\n"
				"    code 000d\n"
				"code 000f\n");
		}

		// (switch x (1 A) (2 B) (else C)).
		TEST_METHOD(Switches_CasesAndElse)
		{
			AssertParses(shapes::Switch, shapes::SwitchTree);
		}

		// A switch in a loop: the jmp at the end of the case goes to the
		// toss (here it does nothing), and a case breaks out of the loop
		// with the switch value on the stack.
		TEST_METHOD(Switches_InALoop)
		{
			AssertParses(shapes::SwitchInLoop,
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
				"code 000b\n");
			AssertParses(R"(
			head:
				lap 1
				bnt exit
				lsp 2
				dup
				ldi 1
				eq?
				bnt case2
				jmp exit
				jmp done
			case2:
				dup
				ldi 2
				eq?
				bnt done
				+at 0
			done:
				toss
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 000f\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        switch 0002 toss 000e\n"
				"          case bnt 0006 jmp 0008\n"
				"            value\n"
				"              code 0003-0005\n"
				"            body\n"
				"              break 1 0007\n"
				"          case bnt 000c\n"
				"            value\n"
				"              code 0009-000b\n"
				"            body\n"
				"              code 000d\n"
				"      else break 1\n"
				"code 0010\n");
		}

		// A switch in the body of a case.
		TEST_METHOD(Switches_Nested)
		{
			AssertParses(R"(
				lsp 1
				dup
				ldi 1
				eq?
				bnt done
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
			)",
				"switch 0000 toss 000c\n"
				"  case bnt 0004\n"
				"    value\n"
				"      code 0001-0003\n"
				"    body\n"
				"      switch 0005 toss 000b\n"
				"        case bnt 0009\n"
				"          value\n"
				"            code 0006-0008\n"
				"          body\n"
				"            code 000a\n"
				"code 000d\n");
		}

		// Negative check of the case rule: a dup at the switch depth whose
		// eq? is another compare is no case value.
		TEST_METHOD(Switches_ACaseWithNoEqFails)
		{
			AssertParseFails(R"(
				lsp 1
				dup
				ldi 1
				eq?
				bnt case2
				ldi 5
				jmp done
			case2:
				dup
				ldi 2
				lt?
				bnt done
				ldi 7
			done:
				toss
				ret
			)",
				"case-value", 0x0007);
		}

		// A switch with only an else, whose body starts with a dup (Sierra's
		// optimiser: a dup in place of "pushi 1" when the switch value is 1).
		TEST_METHOD(Switches_OnlyAnElseThatStartsWithADup)
		{
			AssertParses(R"(
				push1
				dup
				pushi 3
				callk 5 2
				toss
				ret
			)",
				"switch 0000 toss 0004\n"
				"  case\n"
				"    body\n"
				"      code 0001-0003\n"
				"code 0005\n");
		}

		// This repository's compiler, (while c (if a (breakif b)) X): the
		// dialect pass moves the bnt onto the bt before its target, but that
		// bt is a breakif, so the parser keeps the target of the bnt.
		TEST_METHOD(Dialect_ABntPastABreakIfKeepsItsTarget)
		{
			AssertParses(R"(
			head:
				lap 1
				bnt exit
				lap 2
				bnt skip
				lap 3
				bt exit
			skip:
				+at 0
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 0007\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        code 0002\n"
				"        if 0003\n"
				"          then\n"
				"            code 0004\n"
				"            breakif 1 0005\n"
				"        code 0006\n"
				"      else break 1\n"
				"code 0008\n");
		}

		// A contif to the step of a for loop, just before the target of a
		// bnt: the bnt keeps its target once the step is known (a continue in
		// a nested if gives the step).
		TEST_METHOD(Dialect_ABntPastAContIfToTheStepKeepsItsTarget)
		{
			AssertParses(R"(
				ldi 0
				sat 0
			head:
				lst 0
				ldi 9
				lt?
				bnt exit
				lap 4
				bnt s0
				lap 5
				bnt s1
				jmp step
			s1:
				+at 2
			s0:
				lap 1
				bnt skip
				lap 2
				bt step
			skip:
				+at 3
			step:
				+at 0
				jmp head
			exit:
				ret
			)",
				"code 0000-0001\n"
				"loop 0002 latch 0012\n"
				"  body\n"
				"    code 0002-0004\n"
				"    if 0005\n"
				"      then\n"
				"        code 0006\n"
				"        if 0007\n"
				"          then\n"
				"            code 0008\n"
				"            if 0009\n"
				"              then\n"
				"                continue 1 000a\n"
				"            code 000b\n"
				"        code 000c\n"
				"        if 000d\n"
				"          then\n"
				"            code 000e\n"
				"            contif 1 000f\n"
				"        code 0010\n"
				"      else break 1\n"
				"  step\n"
				"    code 0011\n"
				"code 0013\n");
		}

		// This repository's compiler: the bnt of an empty last case goes to
		// the toss just after it (it does nothing). In a loop, the switch
		// must stay one region.
		TEST_METHOD(Switches_EmptyLastCaseWithANoOpBnt)
		{
			AssertParses(R"(
			head:
				lst 0
				lap 1
				lt?
				bnt exit
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
				bnt done
			done:
				toss
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 0011\n"
				"  body\n"
				"    code 0000-0002\n"
				"    if 0003\n"
				"      then\n"
				"        switch 0004 toss 0010\n"
				"          case bnt 0008 jmp 000b\n"
				"            value\n"
				"              code 0005-0007\n"
				"            body\n"
				"              code 0009-000a\n"
				"          case bnt 000f\n"
				"            value\n"
				"              code 000c-000e\n"
				"            body\n"
				"      else break 1\n"
				"code 0012\n");
		}

		// Each case returns: the toss is dead, and the switch is still one
		// region.
		TEST_METHOD(Switches_EachCaseReturns)
		{
			AssertParses(R"(
				lsp 1
				dup
				ldi 1
				eq?
				bnt case2
				ldi 5
				ret
				jmp done
			case2:
				dup
				ldi 2
				eq?
				bnt caseElse
				ldi 7
				ret
				jmp done
			caseElse:
				ldi 6
				ret
			done:
				toss
				ret
			)",
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
				"code 0012\n");
		}

		// Sierra's (while c (if q (if b (bar) (if d X)) (break) else Z) Y):
		// the bnts of the inner ifs are threaded to the loop exit. They are
		// nested ifs with a break as the else, not and-terms: a term would
		// take in the statement (bar).
		TEST_METHOD(Loops_AThreadedBntToTheExitIsNoAndTerm)
		{
			AssertParses(R"(
			head:
				lap 1
				bnt exit
				lap 9
				bnt else
				lap 3
				bnt exit
				+at 4
				lap 5
				bnt exit
				+at 6
				jmp exit
				jmp done
			else:
				+at 8
			done:
				+at 7
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 000e\n"
				"  body\n"
				"    code 0000\n"
				"    if 0001\n"
				"      then\n"
				"        code 0002\n"
				"        if 0003\n"
				"          then\n"
				"            code 0004\n"
				"            if 0005\n"
				"              then\n"
				"                code 0006-0007\n"
				"                if 0008\n"
				"                  then\n"
				"                    code 0009\n"
				"                    break 1 000a\n"
				"                  else break 1\n"
				"              else break 1\n"
				"          else 000b\n"
				"            code 000c\n"
				"        code 000d\n"
				"      else break 1\n"
				"code 000f\n");
		}

		// (if a (for ((= t 1)) (<= t 4) ...) ...) as an and-term (Hoyle
		// Classic Card Games, b1::doit): the exit of the loop is a dead bnt,
		// and the test of the loop is threaded past it. The dead bnt is a
		// test with its place, so the break resolves through it.
		TEST_METHOD(DeadCode_ABntAtTheExitOfALoop)
		{
			AssertParses(R"(
				lap 1
				bnt end
				ldi 1
				sat 1
			head:
				lst 1
				ldi 4
				le?
				bnt end
				+at 1
				jmp head
				bnt end
				lat 1
			end:
				sat 0
				ret
			)",
				"code 0000\n"
				"if 0001\n"
				"  then\n"
				"    code 0002-0003\n"
				"    loop 0004 latch 0009\n"
				"      body\n"
				"        code 0004-0006\n"
				"        if 0007\n"
				"          then\n"
				"            code 0008\n"
				"          else break 1\n"
				"    if 000a\n"
				"      then\n"
				"        code 000b\n"
				"code 000c-000d\n");
		}

		// Police Quest 1 VGA, disguise::doVerb: the bnt of an if goes to its
		// own fall-through, so the else-part is dead, and the jmp at the end of
		// the then-part goes to the toss. It is an exit of the case body.
		TEST_METHOD(DeadCode_AJmpToTheEndBeforeDeadCode)
		{
			AssertParses(R"(
				lsp 1
				dup
				ldi 4
				eq?
				bnt case2
				lsg 211
				ldi 91
				eq?
				bnt next
			next:
				+ag 5
				jmp done
				+ag 6
				jmp done
			case2:
				dup
				ldi 1
				eq?
				bnt done
				+ag 7
			done:
				toss
				ret
			)",
				"switch 0000 toss 0012\n"
				"  case bnt 0004 jmp 000c\n"
				"    value\n"
				"      code 0001-0003\n"
				"    body\n"
				"      code 0005-0009\n"
				"      exit 000a\n"
				"      code 000b\n"
				"  case bnt 0010\n"
				"    value\n"
				"      code 000d-000f\n"
				"    body\n"
				"      code 0011\n"
				"code 0013\n");
		}

		// Hoyle Official Book of Games 3, BGPlayer::numberOfRolls: in a for
		// loop, (if a (if b (++ t1) (continue)) (if c (continue))) (++ t2).
		// The continue at the end of the then-part looks like an else marker;
		// the reading with an else fails, and the reading with no else
		// parses. An and-term that holds a continue is no value.
		TEST_METHOD(If_AContinueBeforeTheStatementsAfterTheIf)
		{
			AssertParses(R"(
				ldi 0
				sat 0
			head:
				+at 0
				push
				ldi 6
				le?
				bnt exit
				lap 1
				bnt after
				lap 2
				bnt skip
				+at 1
				jmp step
			skip:
				lap 3
				bnt after
				jmp step
			after:
				+at 2
			step:
				ldi 1
				jmp head
			exit:
				ret
			)",
				"code 0000-0001\n"
				"loop 0002 latch 0012\n"
				"  body\n"
				"    code 0002-0005\n"
				"    if 0006\n"
				"      then\n"
				"        code 0007\n"
				"        if 0008\n"
				"          then\n"
				"            code 0009\n"
				"            if 000a\n"
				"              then\n"
				"                code 000b\n"
				"                continue 1 000c\n"
				"            code 000d\n"
				"            if 000e\n"
				"              then\n"
				"                continue 1 000f\n"
				"        code 0010\n"
				"      else break 1\n"
				"  step\n"
				"    code 0011\n"
				"code 0013\n");
		}

		// The Island of Dr. Brain, weightsPuzzle::buyClue: in a for loop,
		// (cond ((< a 0) (= t 0)) (b (continue))) (++ t3). Both readings of
		// the first if fail before the step is known; the error of the reading
		// with an else (the continue) gives the step, not the error of the jmp
		// of the else as a statement.
		TEST_METHOD(If_TheErrorOfTheElseReadingGivesTheStep)
		{
			AssertParses(R"(
				ldi 0
				sat 0
			head:
				lst 0
				ldi 3
				lt?
				bnt exit
				lap 1
				bnt case2
				ldi 0
				sat 9
				jmp after
			case2:
				lap 2
				bnt after
				jmp step
			after:
				+at 3
			step:
				+at 0
				jmp head
			exit:
				ret
			)",
				"code 0000-0001\n"
				"loop 0002 latch 0010\n"
				"  body\n"
				"    code 0002-0004\n"
				"    if 0005\n"
				"      then\n"
				"        code 0006\n"
				"        if 0007\n"
				"          then\n"
				"            code 0008-0009\n"
				"          else 000a\n"
				"            code 000b\n"
				"            if 000c\n"
				"              then\n"
				"                continue 1 000d\n"
				"        code 000e\n"
				"      else break 1\n"
				"  step\n"
				"    code 000f\n"
				"code 0011\n");
		}

		// The shared-then shape in a loop: a bnt with no place gives no step
		// of a for loop (a continue is a jmp, a contif a bt), so the parse
		// stops at that bnt.
		TEST_METHOD(Loops_ABntGivesNoStep)
		{
			AssertParseFails(R"(
				ldi 0
				sat 0
			head:
				lst 0
				ldi 3
				lt?
				bnt exit
				lat 1
				bnt isTrue
				lat 2
				bnt isFalse
			isTrue:
				ldi 1
				jmp store
			isFalse:
				ldi 0
			store:
				sat 4
				+at 0
				jmp head
			exit:
				ret
			)",
				"no-scope-for-target", 0x0009);
		}

		// King's Quest IV dev, Gauge::doit: (repeat (if (or a b) X (break)))
		// in the "or" form of this repository's compiler. The bt goes past a
		// bnt that goes back to the head, and the break jumps over the dead
		// latch: it leaves the loop, so it is no jmp that does nothing.
		TEST_METHOD(Loops_ABreakOverTheDeadLatch)
		{
			AssertParses(R"(
			head:
				lap 1
				bt yes
				lap 2
				bnt head
			yes:
				+at 0
				jmp exit
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 0006\n"
				"  body\n"
				"    code 0000\n"
				"    or 0001\n"
				"      code 0002\n"
				"    if 0003\n"
				"      then\n"
				"        code 0004\n"
				"        break 1 0005\n"
				"code 0007\n");
		}

		// Police Quest 2, phoneNumber::changeState: a repeat in a case whose
		// last statement is a break. The latch after it is dead, and the break
		// is threaded through the dead jmp of the case to the toss: the latch
		// still ends the loop.
		TEST_METHOD(Loops_ADeadLatchWhoseExitIsThreaded)
		{
			AssertParses(R"(
				lsp 1
				dup
				ldi 1
				eq?
				bnt done
			head:
				+at 0
				lap 2
				bnt head
				jmp done
				jmp head
				jmp done
			done:
				toss
				ret
			)",
				"switch 0000 toss 000b\n"
				"  case bnt 0004 jmp 000a\n"
				"    value\n"
				"      code 0001-0003\n"
				"    body\n"
				"      loop 0005 latch 0009\n"
				"        body\n"
				"          code 0005-0006\n"
				"          if 0007\n"
				"            then\n"
				"              break 1 0008\n"
				"code 000c\n");
		}

		// Space Quest 3, proc984_0: (repeat X (contif a) (contif b) (break)).
		// The rest of the body after each bt is no value (it breaks), so no
		// or: each bt to the head is a contif.
		TEST_METHOD(Loops_ContIfWhenTheOrJumpsOut)
		{
			AssertParses(R"(
			head:
				+at 0
				lap 1
				bt head
				lap 2
				bt head
				jmp exit
				jmp head
			exit:
				ret
			)",
				"loop 0000 latch 0006\n"
				"  body\n"
				"    code 0000-0001\n"
				"    contif 1 0002\n"
				"    code 0003\n"
				"    contif 1 0004\n"
				"    break 1 0005\n"
				"code 0007\n");
		}

		// The switch fixtures.
		TEST_METHOD(Fixtures_Switches)
		{
			_gameFolder = SetUpGameSCI11();
			AssertRegionsMatchExpected("S4_EmptyLastCaseInLoop", 948);
			AssertRegionsMatchExpected("S5_SwitchAllReturn", 949);
			AssertRegionsMatchExpected("F9_BreakInSwitchCase", 921);
			AssertRegionsMatchExpected("F18_SwitchHeadContinue", 942);
			AssertRegionsMatchExpected("S1_SwitchValue", 945);
			AssertRegionsMatchExpected("S2_CaseValueBranch", 946);
			AssertRegionsMatchExpected("S3_EmptyLastCase", 947);
		}

		// The other decompiler fixtures.
		TEST_METHOD(Fixtures_Others)
		{
			_gameFolder = SetUpGameSCI11();
			AssertRegionsMatchExpected("A1_ReusedAcc", 928);
			AssertRegionsMatchExpected("A2_ReusedSelector", 930);
			AssertRegionsMatchExpected("R1_ReturnShapes", 927);
			AssertRegionsMatchExpected("F7_UnknownClass", 907);
			AssertRegionsMatchExpected("F8_AssignBeforeCondInRet", 918);
			AssertRegionsMatchExpected("F8_DeadValueStatement", 919);
			AssertRegionsMatchExpected("C2_IndexedMathAssign", 920);
			AssertRegionsMatchExpected("C3_SierraIndexedMathAssign", 932);
		}

		// The fixtures of the loop families.
		TEST_METHOD(Fixtures_Loops)
		{
			_gameFolder = SetUpGameSCI11();
			AssertRegionsMatchExpected("F1_LoopHeadContinue", 900);
			AssertRegionsMatchExpected("F4_BreakElseEdge", 904);
			AssertRegionsMatchExpected("F4_WhileAnd", 915);
			AssertRegionsMatchExpected("F4_WhileOr", 916);
			AssertRegionsMatchExpected("F5_EmptyLeadingWhile", 905);
			AssertRegionsMatchExpected("F6_EmptyTrailingFor", 906);
			AssertRegionsMatchExpected("F10_MidBodyContinue", 922);
			AssertRegionsMatchExpected("F11_LatchTrampoline", 924);
			AssertRegionsMatchExpected("F12_BreakJoin", 925);
			AssertRegionsMatchExpected("F14_BreakPastLatch", 936);
			AssertRegionsMatchExpected("F15_SharedLoopHead", 937);
			AssertRegionsMatchExpected("F16_SharedHeadOneLoop", 938);
			AssertRegionsMatchExpected("F19_ValueIfInAnd", 943);
			AssertRegionsMatchExpected("F20_OrAndLoopHead", 944);
			AssertRegionsMatchExpected("P2_CondInLoop", 926);
			AssertRegionsMatchExpected("N1_ChainedCompare", 923);
			AssertRegionsMatchExpected("P1_CompoundConditions", 917);
		}

		// The fixtures of the families of if, and, or: the region tree of
		// each function after the verify stage.
		TEST_METHOD(Fixtures_IfAndOr)
		{
			_gameFolder = SetUpGameSCI11();
			AssertRegionsMatchExpected("F3_AndAsArgument", 914);
			AssertRegionsMatchExpected("F3_AndOr", 912);
			AssertRegionsMatchExpected("F3_IfValueWithElse", 913);
			AssertRegionsMatchExpected("F3_OrAndOr", 911);
			AssertRegionsMatchExpected("F3_OrThreeTerms", 910);
			AssertRegionsMatchExpected("F3_ValueIfReturn", 909);
			AssertRegionsMatchExpected("F13_ValueIfArgument", 931);
			AssertRegionsMatchExpected("F17_ThreadedOrJoin", 939);
			AssertRegionsMatchExpected("B1_DeadBranch", 929);
			AssertRegionsMatchExpected("N2_SierraChainedCompare", 933);
			AssertRegionsMatchExpected("C1_ValueAndOr", 908);
			AssertRegionsMatchExpected("X_SharedThenBranch", 903);
		}

	private:
		std::string _gameFolder;
	};
}
