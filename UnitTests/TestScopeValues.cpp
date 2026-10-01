#include "stdafx.h"
#include "CppUnitTest.h"
#include "ScopeAsm.h"
#include "ScopeCode.h"
#include "ScopeParser.h"
#include "ScopeRegion.h"
#include "ScopeValues.h"
#include "ScopeVerify.h"
#include "ConsumptionNode.h"
#include "PMachine.h"
#include "format.h"
#include <fstream>
#include <sstream>

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

		// A short text of a chunk: the opcode or the chunk type, "*" for a
		// copy, and the children in parentheses.
		std::string Chunk(ConsumptionNode &node)
		{
			std::string text = node._hasPos ? OpcodeToName(node.GetCode()->get_opcode(), 0) : chunkTypeNames[(int)node.GetType()];
			if (((node.GetType() == ChunkType::Break) || (node.GetType() == ChunkType::Continue)) && (node._level != 1))
			{
				text += std::to_string(node._level);
			}
			if (node._copy)
			{
				text += "*";
			}
			if (node.GetChildCount() > 0)
			{
				text += "(";
				for (size_t i = 0; i < node.GetChildCount(); ++i)
				{
					text += ((i > 0) ? " " : "") + Chunk(*node.Child((int)i));
				}
				text += ")";
			}
			return text;
		}

		std::unique_ptr<ConsumptionNode> Build(ScopeAsm &a, bool returnsValue)
		{
			CodeModel model(a.code);
			std::unique_ptr<Region> root = Parse(model);
			std::set<int> passed;
			Verify(model, *root, &passed);
			return BuildValues(model, *root, a.code, returnsValue, passed);
		}

		// The statements of the function, each as Chunk gives it.
		void AssertValues(const std::string &code, const std::string &statements, bool returnsValue = false)
		{
			ScopeAsm a(code);
			std::unique_ptr<ConsumptionNode> body;
			try
			{
				body = Build(a, returnsValue);
			}
			catch (const ScopeError &e)
			{
				Assert::Fail(Wide(fmt::format("{0} at {1:04x}: {2}", e.what(), e.Offset(), e.Detail())).c_str());
			}
			std::string text;
			for (size_t i = 0; i < body->GetChildCount(); ++i)
			{
				text += ((i > 0) ? " " : "") + Chunk(*body->Child((int)i));
			}
			Assert::AreEqual(statements, text);
		}

		// The value stage stops with the id, at the address.
		void AssertValuesFail(const std::string &code, const std::string &id, int offset, bool returnsValue = false)
		{
			ScopeAsm a(code);
			try
			{
				Build(a, returnsValue);
			}
			catch (const ScopeError &e)
			{
				std::string text = fmt::format("{0} at {1:04x}: {2}", e.what(), e.Offset(), e.Detail());
				Assert::AreEqual(std::string("values"), e.Stage(), Wide(text).c_str());
				Assert::AreEqual(id, e.Id(), Wide(text).c_str());
				Assert::AreEqual(offset, e.Offset(), Wide(text).c_str());
				return;
			}
			Assert::Fail(L"the value stage accepts the code");
		}
	}

	// Not a check: the chunk tree of the code in the file of SCOPE_ASM_FILE,
	// in the format of the code dump of --debug-control-flow (one
	// instruction on each line: the address, the opcode and its operands; a
	// branch has the address of its target). With SCOPE_ASM_RETURNS set, a
	// ret reads the accumulator. Skipped when the variable is not set.
	TEST_CLASS(ScopeValuesDiagnostic)
	{
	public:
		TEST_METHOD(Diagnostic_ValuesOfACodeDump)
		{
			const char *path = getenv("SCOPE_ASM_FILE");
			if (!path)
			{
				Logger::WriteMessage(L"Skipped: set SCOPE_ASM_FILE.");
				return;
			}
			std::ifstream file(path);
			std::string line;
			std::string text;
			while (std::getline(file, line))
			{
				std::istringstream words(line);
				std::string address;
				std::string op;
				if (!(words >> address >> op) || (address.size() != 4) || (address.find_first_not_of("0123456789abcdef") != std::string::npos))
				{
					continue;
				}
				text += "L" + address + ":\n" + op;
				bool branch = (op == "bt") || (op == "bnt") || (op == "jmp");
				std::string operand;
				while ((words >> operand) && (operand[0] != 'd'))
				{
					text += " " + (branch ? ("L" + operand) : operand);
				}
				text += "\n";
			}
			ScopeAsm a(text);
			try
			{
				std::unique_ptr<ConsumptionNode> body = Build(a, getenv("SCOPE_ASM_RETURNS") != nullptr);
				std::stringstream ss;
				body->Print(ss, 0);
				Logger::WriteMessage(Wide(ss.str()).c_str());
			}
			catch (const ScopeError &e)
			{
				Logger::WriteMessage(Wide(fmt::format("{0} at {1:04x}: {2}", e.what(), e.Offset(), e.Detail())).c_str());
			}
		}
	};

	TEST_CLASS(TestScopeValues)
	{
	public:
		// An instruction takes its stack operands and then the node of the
		// accumulator, in address order.
		TEST_METHOD(Values_AnInstructionTakesItsOperands)
		{
			AssertValues(R"(
				push1
				lal 0
				push
				lal 1
				send 4
				sat 2
				ret
			)", "sat(send(push1 push(lal) lal)) ret");
		}

		// A node that was made before the pushes of the reader is no
		// operand of it: it is a statement, and the reader has a copy from
		// the fact (Sierra's optimiser deleted the load).
		TEST_METHOD(Values_ANodeBeforeThePushesIsAStatement)
		{
			AssertValues(R"(
				ldi 5
				sat 0
				pushi 1
				push0
				send 4
				ldi 6
				pushi 7
				push1
				push
				lat 1
				send 6
				ret
			)", "sat(ldi) send(pushi push0 sat*) ldi send(pushi push1 push(ldi*) lat) ret");
		}

		// The then-part starts with the facts of the branch: a push of the
		// tested variable is a copy of it.
		TEST_METHOD(Values_TheThenPartHasTheFactsOfTheBranch)
		{
			AssertValues(R"(
				lal 0
				bnt end
				push1
				push
				callk 5 2
			end:
				ret
			)", "If(Condition(lal) Then(callk(push1 push(lal*)))) ret");
		}

		// The else entry is a label: no fact.
		TEST_METHOD(Values_TheElseEntryHasNoFact)
		{
			AssertValuesFail(R"(
				lal 0
				bnt elseBranch
				ldi 1
				sal 1
				jmp end
			elseBranch:
				push1
				push
				callk 5 2
			end:
				ret
			)", "acc-no-fact", 6);
		}

		// The join is a label: no fact. The value of the if is its node; a
		// push after other pushes is no operand of it.
		TEST_METHOD(Values_TheJoinHasNoFact)
		{
			AssertValuesFail(R"(
				lal 0
				bnt end
				ldi 1
				sal 1
			end:
				push1
				push
				callk 5 2
				ret
			)", "acc-no-fact", 5);
		}

		// An if whose value an instruction after the join takes.
		TEST_METHOD(Values_AValueIf)
		{
			AssertValues(R"(
				push1
				lal 0
				bnt elseBranch
				ldi 1
				jmp end
			elseBranch:
				ldi 2
			end:
				push
				callk 5 2
				ret
			)", "callk(push1 push(If(Condition(lal) Then(ldi) Else(ldi)))) ret");
		}

		// Dead code after a jmp gives no statement: as text, it would run.
		// The live code after it starts at a label: no fact, unless the jmp
		// does nothing and is the only branch there (the accumulator keeps its
		// value). Dead code right after a ret stays as statements (with no
		// fact): no text gets to it either.
		TEST_METHOD(Values_DeadCodeGivesNoStatement)
		{
			AssertValues(R"(
				ldi 5
				ret
				ldi 9
				sal 1
				ret
			)", "ldi ret sal(ldi) ret");
			AssertValuesFail(R"(
				ldi 5
				ret
				push1
				push
				callk 5 2
				ret
			)", "acc-no-fact", 3);
			AssertValues(R"(
				ldi 5
				jmp next
				ldi 9
			next:
				push1
				push
				callk 5 2
				ret
			)", "ldi callk(push1 push(ldi*)) ret");
			// Another branch to the label: no fact.
			AssertValuesFail(R"(
				lal 0
				bnt next
				ldi 5
				jmp next
				ldi 9
			next:
				push1
				push
				callk 5 2
				ret
			)", "acc-no-fact", 6);
		}

		// A structure that no path reaches is dead code: its test has no
		// value, and it gives no statement (the ret that only it reaches is
		// dead too).
		TEST_METHOD(Values_ADeadIfIsDeadCode)
		{
			AssertValues(R"(
				lal 0
				ret
				bnt end
				ldi 4
				jmp end
			end:
				ret
			)", "lal ret");
		}

		// The facts of Sierra's optimiser: a store to a property keeps the
		// fact of the value (a variable here).
		TEST_METHOD(Values_AStoreToAPropertyKeepsTheFact)
		{
			AssertValues(R"(
				lap 1
				aTop 4
				push1
				push
				callk 5 2
				ret
			)", "aTop(lap) callk(push1 push(lap*)) ret");
		}

		// A number and a property are facts too.
		TEST_METHOD(Values_ANumberAndAPropertyAreFacts)
		{
			AssertValues(R"(
				ldi 6
				push1
				push
				callk 5 2
				pToa 4
				push1
				push
				callk 5 2
				ret
			)", "ldi callk(push1 push(ldi*)) pToa callk(push1 push(pToa*)) ret");
		}

		// A store of the stack to the variable of the fact changes the
		// variable, not the accumulator: no fact.
		TEST_METHOD(Values_AStackStoreToTheVariableEndsTheFact)
		{
			AssertValuesFail(R"(
				lat 0
				pushi 3
				sst 0
				push1
				push
				callk 5 2
				ret
			)", "acc-no-fact", 4);
		}

		// The same for an increment of the variable to the stack, and of the
		// property of the fact.
		TEST_METHOD(Values_AnIncrementToTheStackEndsTheFact)
		{
			AssertValuesFail(R"(
				lat 0
				push1
				+st 0
				push
				callk 5 4
				ret
			)", "acc-no-fact", 3);
			AssertValuesFail(R"(
				pToa 4
				push1
				ipTos 4
				push
				callk 5 4
				ret
			)", "acc-no-fact", 3);
		}

		// An indexed store to the stack can change any variable of its kind
		// (temp[5 + temp0] is temp0 when temp0 is -5): it ends the fact of a
		// variable of that kind.
		TEST_METHOD(Values_AnIndexedStackStoreEndsTheFact)
		{
			AssertValuesFail(R"(
				lat 0
				pushi 3
				ssti 5
				push1
				push
				callk 5 2
				ret
			)", "acc-no-fact", 4);
		}

		// A store to another variable keeps the fact.
		TEST_METHOD(Values_AStackStoreToAnotherVariableKeepsTheFact)
		{
			AssertValues(R"(
				lat 0
				pushi 3
				sst 1
				push1
				push
				callk 5 2
				ret
			)", "lat sst(pushi) callk(push1 push(lat*)) ret");
		}

		// An indexed load has no fact.
		TEST_METHOD(Values_AnIndexedLoadHasNoFact)
		{
			AssertValuesFail(R"(
				ldi 1
				lati 0
				push1
				push
				callk 5 2
				ret
			)", "acc-no-fact", 3);
		}

		// A statement in the middle of an expression has no text, unless only
		// pushes of numbers come before it (QfG4 CD script 81, ant::cue) or it
		// has no effect (LSL3 script 460, LightScript::changeState): it comes
		// before the expression.
		TEST_METHOD(Values_AStatementInAnExpressionFails)
		{
			AssertValues(R"(
				push1
				ldi 5
				sal 0
				lal 1
				add
				ret
			)", "sal(ldi) add(push1 lal) ret");
			AssertValues(R"(
				push0
				callk 60 0
				push
				lsl 2
				ldi 2
				div
				lal 1
				add
				ret
			)", "div(lsl ldi) add(push(callk(push0)) lal) ret");
			// The variable before the store is one that the store changes.
			AssertValuesFail(R"(
				lsl 0
				ldi 5
				sal 0
				lal 1
				add
				ret
			)", "statement-in-expression", 2);
		}

		// A sequence does not take a value that was pushed before it.
		TEST_METHOD(Values_AThenPartDoesNotTakeAValueFromBeforeTheIf)
		{
			AssertValuesFail(R"(
				push1
				lal 0
				bnt end
				ldi 2
				add
				sal 1
			end:
				ret
			)", "stack-underflow", 4);
		}

		// A &rest is an argument of a call only.
		TEST_METHOD(Values_ARestOutsideACallFails)
		{
			AssertValuesFail(R"(
				pushi 1
				&rest 1
				ldi 2
				add
				ret
			)", "rest-outside-call", 3);
		}

		// A send takes the &rest among its values.
		TEST_METHOD(Values_ASendTakesTheRest)
		{
			AssertValues(R"(
				pushi 7
				push1
				pushSelf
				&rest 2
				lal 0
				send 6
				ret
			)", "send(pushi push1 pushSelf &rest lal) ret");
		}

		// A dup outside a switch is a copy of a value on the stack.
		TEST_METHOD(Values_ADupCopiesAPureValue)
		{
			AssertValues(R"(
				pushi 2
				pushi 3
				dup
				lal 0
				send 6
				ret
			)", "send(pushi pushi pushi* lal) ret");
			// A value that was pushed before the then-part.
			AssertValues(R"(
				push1
				lal 0
				bnt elseBranch
				dup
				lal 1
				push
				callk 5 2
				jmp end
			elseBranch:
				ldi 7
			end:
				push
				callk 6 2
				ret
			)", "callk(push1 push(If(Condition(lal) Then(callk(push1* push(lal))) Else(ldi)))) ret");
			AssertValuesFail(R"(
				lsl 0
				lal 1
				add
				push
				dup
				callk 5 2
				ret
			)", "dup-no-value", 4);
		}

		// An or whose second operand has statements: as a statement, (or c
		// X) is (if (not c) X). A reader of its value has no text.
		TEST_METHOD(Values_AnOrWithStatements)
		{
			AssertValues(R"(
				lal 0
				bt end
				ldi 1
				sal 1
				lal 2
			end:
				ret
			)", "If(Condition(Invert(lal)) Then(sal(ldi) lal)) ret");
			AssertValuesFail(R"(
				lal 0
				bt end
				ldi 1
				sal 1
				lal 2
			end:
				sal 3
				ret
			)", "acc-no-fact", 5);
		}

		// The statement form of an or has another value in the accumulator
		// than the code: an if with it is no value, and a bare ret after it
		// (in a function that returns a value) has no text.
		TEST_METHOD(Values_AnOrWithStatementsGivesNoValue)
		{
			AssertValuesFail(R"(
				lal 0
				bnt elseBranch
				lal 1
				bt orEnd
				ldi 1
				sal 2
				lal 3
			orEnd:
				jmp join
			elseBranch:
				ldi 5
			join:
				sal 4
				ret
			)", "acc-no-fact", 9);
			AssertValuesFail(R"(
				lal 0
				bt end
				ldi 1
				sal 1
				lal 2
			end:
				ret
			)", "acc-differs", 5, true);
		}

		// An if that is a value keeps its form; an if with an empty then-part
		// whose else is a continue is (contif (not c)).
		TEST_METHOD(Values_TheBreakIfFormIsForStatements)
		{
			AssertValues(R"(
			head:
				lal 0
				bnt elseBranch
				jmp join
			elseBranch:
				jmp exit
			join:
				sal 1
				jmp head
			exit:
				ret
			)", "While(Condition(TrueNode) LoopBody(sal(If(Condition(lal) Then Else(Break))))) ret");
			AssertValues(R"(
			head:
				lal 0
				bnt join
				ldi 1
				sal 1
				lal 2
				bnt head
			join:
				ldi 3
				sal 3
				jmp head
			)", "While(Condition(TrueNode) LoopBody(If(Condition(lal) Then(sal(ldi) If(Condition(Invert(lal)) Then(Continue)))) sal(ldi)))");
		}

		// The value of an or and of the tests of an if with an else.
		TEST_METHOD(Values_OrAndAnd)
		{
			AssertValues(R"(
				lal 0
				bt orEnd
				lal 1
			orEnd:
				sal 2
				lal 0
				bnt elseBranch
				lal 1
				bnt elseBranch
				ldi 1
				jmp end
			elseBranch:
				ldi 2
			end:
				sal 3
				ret
			)", "sal(Or(First(lal) Second(lal))) sal(If(Condition(And(First(lal) Second(lal))) Then(ldi) Else(ldi))) ret");
		}

		// A bnt right after a bnt to the same place adds no term: it does
		// nothing.
		TEST_METHOD(Values_ARepeatedTestAddsNoTerm)
		{
			AssertValues(R"(
				lal 0
				bnt elseBranch
				lal 1
				bnt elseBranch
				bnt elseBranch
				ldi 1
				jmp end
			elseBranch:
				ldi 2
			end:
				sal 3
				ret
			)", "sal(If(Condition(And(First(lal) Second(lal))) Then(ldi) Else(ldi))) ret");
		}

		// A ret of a function that returns a value takes the node of the
		// accumulator; when the test of an if took it, the ret is bare.
		TEST_METHOD(Values_ARetTakesTheValueOrIsBare)
		{
			AssertValues(R"(
				lal 0
				bnt end
				ret
			end:
				ldi 1
				ret
			)", "If(Condition(lal) Then(ret)) ret(ldi)", true);
		}

		// A loop whose body is one if whose else is the break of the loop
		// is a while; its test is the test of the if.
		TEST_METHOD(Values_AWhileLoop)
		{
			AssertValues(R"(
			head:
				lal 0
				bnt exit
				ldi 1
				sal 1
				jmp head
			exit:
				ret
			)", "While(Condition(lal) LoopBody(sal(ldi))) ret");
		}

		// A bt to the exit of the outer loop is a breakif of level 2.
		TEST_METHOD(Values_ABreakOfTheOuterLoop)
		{
			AssertValues(R"(
			outer:
				lal 0
				bnt done
			inner:
				lal 1
				bnt innerExit
				lal 2
				bt done
				ldi 1
				sal 3
				jmp inner
			innerExit:
				jmp outer
			done:
				ret
			)", "While(Condition(lal) LoopBody(While(Condition(lal) LoopBody(If(Condition(lal) Then(Break2)) sal(ldi))))) ret");
		}

		// A dead break that no path of the tree goes through is no statement:
		// after a jmp that does nothing (QfG1 TalkObj::messages: the else-break
		// of a cond, after the bnt went straight to the exit), also after more
		// dead jmps, or after an exit. (F14_BreakPastLatch has a dead break
		// that a path goes through: the inner break leaves the outer loop.)
		// A then-part that ends with a jmp that does nothing, then a dead jmp
		// to the loop head: the dead jmp is no else marker. The code after it
		// runs after the then-part (Dr. Brain, script 245, localproc_10cb).
		TEST_METHOD(Values_ADeadJmpAfterANoOpJmpIsNoElse)
		{
			AssertValues(R"(
			head:
				lal 0
				bnt exit
				lal 1
				bnt next
				ldi 1
				sal 2
				jmp next
				jmp head
			next:
				lal 3
				bnt skip
				ldi 2
				sal 4
			skip:
				jmp head
			exit:
				ret
			)", "While(Condition(lal) LoopBody(If(Condition(lal) Then(sal(ldi))) If(Condition(lal) Then(sal(ldi))))) ret");
		}

		// A case whose bnt goes to the next instruction (it does nothing):
		// the code after it runs for each value, also for the value of the
		// case. It is the else case, and the compare is a statement of it
		// (PQ1 VGA, script 141, uniform::doVerb).
		TEST_METHOD(Values_ACaseWithANoOpBntIsTheElse)
		{
			AssertValues(R"(
				lsp 1
				dup
				ldi 4
				eq?
				bnt case2
				ldi 5
				sat 0
				jmp done
			case2:
				dup
				ldi 1
				eq?
				bnt body
			body:
				ldi 7
				sat 0
			done:
				toss
				ret
			)", "Switch(SwitchValue(lsp) Case(CaseCondition(ldi) CaseBody(sat(ldi))) Case(CaseBody(eq?(lsp* ldi) sat(ldi)))) ret");
		}

		// The operands of an operation that can change places, in the other
		// order: the call, then "pushi 6", then mul. The node of the
		// accumulator is an operand, before the constant (the SQ4 copy in a
		// "patch" folder, script 381, roboClerkWelcome::changeState).
		TEST_METHOD(Values_SwappedOperandsOfAMul)
		{
			AssertValues(R"(
				push2
				pushi 8
				pushi 15
				callk 60 4
				pushi 6
				mul
				aTop 32
				ret
			)", "aTop(mul(callk(push2 pushi pushi) pushi)) ret");
			// Not for an operation whose operands cannot change places.
			AssertValuesFail(R"(
				push2
				pushi 8
				pushi 15
				callk 60 4
				pushi 6
				sub
				aTop 32
				ret
			)", "acc-no-fact", 5);
		}

		// The push of the argument count of a call takes a store of the
		// number that the accumulator holds (SQ1 VGA script 34,
		// egoDropOratPart::changeState). The store is a statement before
		// the call; the slot and the argument are copies of the number.
		TEST_METHOD(Values_StoreInTheArgumentCountOfACall)
		{
			AssertValues(R"(
				ldi 3
				aTop 32
				push
				push
				push1
				pushi 61
				callb 1 6
				ret
			)", "aTop(ldi) callb(push(ldi*) push(ldi*) push1 pushi) ret");
			// A store to a variable.
			AssertValues(R"(
				ldi 2
				sat 0
				push
				push
				push1
				callk 60 4
				ret
			)", "sat(ldi) callk(push(ldi*) push(sat*) push1) ret");
		}

		// The push of a selector of a send takes a store of the selector
		// number (KQ6 floppy script 370, AzurePrint::init).
		TEST_METHOD(Values_StoreInTheSelectorOfASend)
		{
			AssertValues(R"(
				ldi 110
				aTop 32
				push
				push0
				super 166 4
				ret
			)", "aTop(ldi) super(push(ldi*) push0) ret");
			// The argument count of the second message: the store comes
			// after the pushes of the first message, so it cannot come
			// before the send in the text.
			AssertValuesFail(R"(
				pushi 110
				push0
				pushi 111
				ldi 1
				aTop 32
				push
				push2
				selfID
				send 10
				ret
			)", "slot-order", 8);
			// An argument before the slot reads the variable that the store
			// sets: in the text, it would read the new value.
			AssertValuesFail(R"(
				pushi 51
				push1
				lst 0
				pushi 110
				ldi 1
				sat 0
				push
				push0
				lap 1
				send 12
				ret
			)", "slot-order", 9);
			// The argument count reads back a variable that a store of a
			// number set (the optimiser deleted the load): the slot is the
			// number, so the send keeps its arguments.
			AssertValues(R"(
				ldi 3
				sat 0
				pushi 110
				push
				pushi 1
				pushi 2
				pushi 3
				lap 1
				send 10
				ret
			)", "sat(ldi) send(pushi push(ldi*) pushi pushi pushi lap) ret");
			// A selector that a variable holds, from a call (Hoyle Classic
			// script 700, BridgeHand::bid): the push reads the variable back.
			AssertValues(R"(
				pushi 111
				push0
				lag 1
				send 4
				sat 6
				push
				push0
				lat 5
				send 4
				ret
			)", "sat(send(pushi push0 lag)) send(push(sat*) push0 lat) ret");
		}

		// A dup in the value of a case copies the value of the switch (QfG4
		// floppy script 670, pMainDoor::doVerb: the case value is an and
		// whose first term compares the switch value).
		TEST_METHOD(Values_ADupInTheValueOfACase)
		{
			AssertValues(R"(
				lsp 1
				dup
				dup
				ldi 4
				eq?
				bnt no
				lal 4
				not
			no:
				eq?
				bnt other
				ldi 1
				sal 1
				jmp done
			other:
				ldi 2
				sal 1
			done:
				toss
				ret
			)", "Switch(SwitchValue(lsp) Case(CaseCondition(If(Condition(eq?(lsp* ldi)) Then(not(lal)))) CaseBody(sal(ldi))) Case(CaseBody(sal(ldi)))) ret");
		}

		// A jmp over dead code that does nothing keeps the value of the
		// accumulator for the code after it (the KQ4 copy in "patch\NEW",
		// script 996, User::getInput).
		TEST_METHOD(Values_AValueOverDeadCode)
		{
			AssertValues(R"(
				lal 1
				bnt else
				ldi 2
				jmp join
			else:
				ldi 3
			join:
				jmp store
				push1
				lal 5
				send 2
			store:
				sat 0
				ret
			)", "sat(If(Condition(lal) Then(ldi) Else(ldi))) ret");
		}

		// A push leaves its value in the accumulator: a reader of the
		// accumulator after it gets a copy of the pushed node, when the node
		// has no effect (Longbow script 893, Table::at: "push; add").
		TEST_METHOD(Values_AReaderAfterAPushGetsACopy)
		{
			AssertValues(R"(
				lsp 1
				lap 2
				add
				push
				add
				aTop 32
				ret
			)", "aTop(add(push(add(lsp lap)) add*(lsp* lap*))) ret");
			// A node with an effect: no copy.
			AssertValuesFail(R"(
				push0
				callk 60 0
				push
				add
				aTop 32
				ret
			)", "acc-no-fact", 3);
			// A store to the stack between the push and the reader can change
			// a variable that the node reads.
			AssertValuesFail(R"(
				lsp 1
				lap 2
				add
				push
				pushi 3
				ssp 2
				add
				aTop 32
				ret
			)", "acc-no-fact", 6);
		}


		// An operation on a value that no node has, whose result no
		// instruction reads, is no code (LB2 script 250: "bnt L; bnot; bnot;
		// L: ldi 1"; PQ3 script 994, Game::doit).
		TEST_METHOD(Values_ADeadOperationOnATestedValue)
		{
			AssertValues(R"(
				push0
				callk 60 0
				bnt end
				bnot
				bnot
			end:
				ldi 1
				aTop 32
				ret
			)", "If(Condition(callk(push0)) Then) aTop(ldi) ret");
			// A store reads the result.
			AssertValuesFail(R"(
				push0
				callk 60 0
				bnt end
				bnot
			end:
				aTop 32
				ret
			)", "acc-no-fact", 3);
		}

		// Values that a sequence pushed and that no instruction takes are no
		// code when they have no effect (SQ1 VGA script 40, pinkShip::doVerb:
		// a case body "pushi 40; ldi 16" before the toss of the switch).
		TEST_METHOD(Values_PushesThatNoInstructionTakes)
		{
			AssertValues(R"(
				lsp 1
				dup
				ldi 3
				eq?
				bnt other
				push2
				pushi 40
				pushi 13
				callk 5 4
				jmp done
			other:
				pushi 40
				ldi 16
			done:
				toss
				ret
			)", "ret(Switch(SwitchValue(lsp) Case(CaseCondition(ldi) CaseBody(callk(push2 pushi pushi))) Case(CaseBody(ldi))))", true);
			// A pushed call result: the call is an effect.
			AssertValuesFail(R"(
				lsp 1
				dup
				ldi 3
				eq?
				bnt other
				ldi 2
				jmp done
			other:
				push0
				callk 5 0
				push
				ldi 16
			done:
				toss
				ret
			)", "stack-unbalanced", 11, true);
		}


		// On the fall-through of the bnt of a compare, the accumulator is 1:
		// the body of a case, and the then-part of an if whose test is a
		// compare (QfG3 script 471, uhuraCompete::changeState: "bnt; pushi 3;
		// push1; push").
		TEST_METHOD(Values_TheAccumulatorIsOneAfterACompare)
		{
			AssertValues(R"(
				lsp 1
				dup
				ldi 3
				eq?
				bnt done
				pushi 3
				push1
				push
				lap 2
				send 6
			done:
				toss
				ret
			)", "Switch(SwitchValue(lsp) Case(CaseCondition(ldi) CaseBody(send(pushi push1 push(TrueNode) lap)))) ret");
			AssertValues(R"(
				lsp 1
				ldi 3
				eq?
				bnt done
				pushi 3
				push1
				push
				lap 2
				send 6
			done:
				ret
			)", "If(Condition(eq?(lsp ldi)) Then(send(pushi push1 push(TrueNode) lap))) ret");
		}


		// A bnt right after a bnt to the same place, that another branch
		// reaches (a bt makes it a no-op: LiveCode_ABtToTheSecondTestIsLikeThe
		// FallThrough). From a bnt, the value is false: the target equivalence
		// takes the bnt to the place of the second one. From a jmp, any value:
		// the test of the second one is an if with a value. Neither shape gives
		// an and-term with no code (empty-term).
		TEST_METHOD(Values_ABranchToTheSecondOfTwoTests)
		{
			AssertValues(R"(
				lal 0
				bnt second
				lal 1
				bnt else
			second:
				bnt else
				ldi 1
				sal 2
			else:
				ret
			)", "If(Condition(lal) Then(If(Condition(lal) Then(If(Condition(lal*) Then(sal(ldi))))))) ret");
			// A jmp to it: any value.
			AssertValues(R"(
				lal 0
				bnt next
				lal 3
				jmp second
			next:
				lal 1
				bnt else
			second:
				bnt else
				ldi 1
				sal 2
			else:
				ret
			)", "If(Condition(If(Condition(lal) Then(lal) Else(If(Condition(lal) Then)))) Then(sal(ldi))) ret");
		}


		// Shapes that the rules of the refusals must not accept: an or as a
		// statement in a loop whose value a test reads (the text leaves
		// another value at the exit), a toss that takes a value that a case
		// leaves (the switch value stays, and add reads it), and a statement
		// in the operands of another statement in the operands of the next
		// statement (it would come before the call that reads the old value).
		TEST_METHOD(Values_ShapesThatTheRefusalRulesDoNotAccept)
		{
			AssertValuesFail(R"(
			h:
				lat 0
				bnt x
				lap 1
				bnt l
				lat 1
				bt b
				+at 1
				+at 0
			b:
				jmp x
			l:
				jmp h
			x:
				bnt e
				ldi 1
				ret
			e:
				ldi 2
				ret
			)", "acc-no-fact", 10, true);
			AssertValuesFail(R"(
				lst 0
				lap 1
				push
				dup
				ldi 1
				eq?
				bnt c
				pushi 40
				ldi 16
				jmp t
			c:
				ldi 5
			t:
				toss
				add
				ret
			)", "stack-unbalanced", 9, true);
			AssertValuesFail(R"(
				pushi 7
				pushi 1
				lst 0
				ldi 5
				sat 0
				callk 61 2
				ldi 3
				add
				ret
			)", "statement-in-expression", 4, true);
		}


		// Another effect in the slot of a call: a call result as the
		// argument count.
		TEST_METHOD(Values_AnEffectInTheArgumentCountOfACall)
		{
			AssertValuesFail(R"(
				push0
				callk 60 0
				push
				callb 1 0
				ret
			)", "slot-effect", 3);
			// A store in a slot after an argument with an effect: in the
			// text, the store would come before the call of the argument.
			AssertValuesFail(R"(
				pushi 110
				push1
				push0
				callk 60 0
				push
				pushi 111
				ldi 0
				aTop 32
				push
				selfID
				send 10
				ret
			)", "slot-order", 10);
		}

		TEST_METHOD(Values_ADeadBreak)
		{
			AssertValues(R"(
			head:
				lal 0
				bnt exit
				lal 1
				bnt exit
				ldi 1
				sal 1
				jmp join
				jmp exit
			join:
				ldi 2
				sal 2
				jmp head
			exit:
				ret
			)", "While(Condition(lal) LoopBody(If(Condition(lal) Then(sal(ldi) sal(ldi)) Else(Break)))) ret");
			// Two dead jmps after the jmp that does nothing.
			AssertValues(R"(
			head:
				lal 0
				bnt exit
				lal 1
				bnt exit
				ldi 1
				sal 1
				jmp join
				jmp exit
				jmp exit
			join:
				ldi 2
				sal 2
				jmp head
			exit:
				ret
			)", "While(Condition(lal) LoopBody(If(Condition(lal) Then(sal(ldi) sal(ldi)) Else(Break)))) ret");
			// A dead break after the jmp of an exit: (if a (if b X else
			// (break)) else W) Z.
			AssertValues(R"(
			head:
				lal 0
				bnt else0
				lal 1
				bnt exit
				ldi 1
				sal 1
				jmp join0
				jmp exit
				jmp join0
			else0:
				ldi 2
				sal 2
			join0:
				ldi 3
				sal 3
				jmp head
			exit:
				ret
			)", "While(Condition(TrueNode) LoopBody(If(Condition(lal) Then(If(Condition(lal) Then(sal(ldi)) Else(Break))) Else(sal(ldi))) sal(ldi))) ret");
		}

		// The if of the second test of a while stays an if with an else
		// break: the AST passes make (while (and a b)) of it.
		TEST_METHOD(Values_TheSecondTestOfAWhile)
		{
			AssertValues(R"(
			head:
				lal 0
				bnt exit
				lal 1
				bnt exit
				jmp head
			exit:
				ret
			)", "While(Condition(lal) LoopBody(If(Condition(lal) Then Else(Break)))) ret");
		}

		// Statements before the test: a repeat, with the if in it. An if with an
		// empty then-part whose else is a break is (breakif (not c)).
		TEST_METHOD(Values_ARepeatLoop)
		{
			AssertValues(R"(
			head:
				ldi 1
				sal 1
				lal 0
				bnt exit
				jmp head
			exit:
				ret
			)", "While(Condition(TrueNode) LoopBody(sal(ldi) If(Condition(Invert(lal)) Then(Break)))) ret");
		}

		// A bt latch is a do loop; a bnt latch a do loop of the inverted test.
		TEST_METHOD(Values_ADoLoop)
		{
			AssertValues(R"(
			head:
				ldi 1
				sal 1
				lal 0
				bt head
				ret
			)", "Do(LoopBody(sal(ldi)) Condition(lal)) ret");
			AssertValues(R"(
			head:
				ldi 1
				sal 1
				lal 0
				bnt head
				ret
			)", "Do(LoopBody(sal(ldi)) Condition(Invert(lal))) ret");
		}

		// A continue to the step of a for loop. The step starts at a label:
		// no fact.
		TEST_METHOD(Values_AForLoop)
		{
			AssertValues(R"(
			head:
				lst 0
				ldi 5
				lt?
				bnt exit
				lal 0
				bnt skip
				lal 1
				bnt skipInner
				jmp step
			skipInner:
				ldi 3
				sal 3
			skip:
				ldi 1
				sal 1
			step:
				+at 0
				jmp head
			exit:
				ret
			)", "For(Condition(lt?(lst ldi)) LoopBody(If(Condition(lal) Then(If(Condition(lal) Then(Continue)) sal(ldi))) sal(ldi)) Step(+at)) ret");
			// A for loop with no test, whose body ends with code.
			AssertValuesFail(R"(
			head:
				lal 0
				bnt skip
				lal 1
				bnt skipInner
				jmp step
			skipInner:
				ldi 3
				sal 3
			skip:
				ldi 1
				sal 1
			step:
				push1
				push
				callk 5 2
				jmp head
			)", "acc-no-fact", 10);
		}

		// The head of a loop is a label: no fact.
		TEST_METHOD(Values_TheHeadOfALoopHasNoFact)
		{
			AssertValuesFail(R"(
				lal 0
			head:
				push1
				push
				callk 5 2
				jmp head
			)", "acc-no-fact", 2);
		}

		// The first case keeps the facts of the head of the switch; each
		// other case starts at a label.
		TEST_METHOD(Values_TheFirstCaseHasTheFactsOfTheHead)
		{
			AssertValues(R"(
				ldi 5
				push
				dup
				eq?
				bnt done
				ldi 1
				sal 1
			done:
				toss
				ret
			)", "Switch(SwitchValue(push(ldi)) Case(CaseCondition(ldi*) CaseBody(sal(ldi)))) ret");
			AssertValuesFail(R"(
				ldi 5
				push
				dup
				ldi 4
				eq?
				bnt second
				ldi 1
				sal 1
				jmp done
			second:
				dup
				eq?
				bnt done
				ldi 2
				sal 1
			done:
				toss
				ret
			)", "acc-no-fact", 10);
		}

		// A dup of the value of a switch in the body of a switch with only
		// an else: a copy of the value.
		TEST_METHOD(Values_ADupOfTheSwitchValue)
		{
			AssertValues(R"(
				lsl 0
				dup
				push1
				lal 1
				send 4
				toss
				ret
			)", "Switch(SwitchValue(lsl) Case(CaseBody(send(lsl* push1 lal)))) ret");
		}

		// Sierra's (< a b c d): one n-ary compare. Another operator after the
		// pprev has no text.
		TEST_METHOD(Values_AnNaryCompare)
		{
			AssertValues(R"(
				lsl 0
				lal 1
				lt?
				bnt end
				pprev
				lal 2
				lt?
				bnt end
				pprev
				lal 3
				lt?
			end:
				sal 4
				ret
			)", "sal(Nary(lt?(lsl lal lal lal))) ret");
			AssertValuesFail(R"(
				lsl 0
				lal 1
				lt?
				bnt end
				pprev
				lal 2
				le?
			end:
				sal 4
				ret
			)", "nary-mixed", 6);
		}
	};
}
