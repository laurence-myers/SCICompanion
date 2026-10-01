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
			Verify(model, *root);
			return BuildValues(model, *root, a.code, returnsValue);
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

		// Dead code has no fact: the accumulator of the code before a ret
		// does not go into it.
		TEST_METHOD(Values_DeadCodeHasNoFact)
		{
			AssertValuesFail(R"(
				ldi 5
				ret
				push1
				push
				callk 5 2
				ret
			)", "acc-no-fact", 3);
		}

		// A structure that no path reaches is dead code: its test has no
		// value, and its branches give no node.
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
			)", "lal ret ldi ret");
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

		// A statement in the middle of an expression has no text.
		TEST_METHOD(Values_AStatementInAnExpressionFails)
		{
			AssertValuesFail(R"(
				push1
				ldi 5
				sal 0
				lal 1
				add
				ret
			)", "statement-in-expression", -1);
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

		// The second operand of an or is a value: a statement in it fails.
		TEST_METHOD(Values_AStatementInAnOrFails)
		{
			AssertValuesFail(R"(
				lal 0
				bt end
				ldi 1
				sal 1
				lal 2
			end:
				sal 3
				ret
			)", "or-statement", 1);
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
	};
}
