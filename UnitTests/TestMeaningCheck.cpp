#include "stdafx.h"
#include "CppUnitTest.h"
#include "ScopeAsm.h"
#include "MeaningCheck.h"
#include "format.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
	namespace
	{
		std::wstring Wide(const std::string &text)
		{
			return std::wstring(text.begin(), text.end());
		}

		// The function of an asm text. A lofsa gives "object o<address>"; a
		// call gives the procedure "local <address>".
		meaning::Function Fn(const std::string &code, bool returnsValue = false)
		{
			ScopeAsm a(code);
			return meaning::MakeFunction("f", "f", a.code, returnsValue, sciVersion1_1,
				[](uint16_t address) { return fmt::format("object o{0}", address); },
				[](uint16_t address) { return fmt::format("local {0}", address); });
		}

		meaning::Outcome Check(const std::string &original, const std::string &recompiled, bool returnsValue = false)
		{
			return meaning::Compare(Fn(original, returnsValue), Fn(recompiled, returnsValue));
		}

		void AssertVerdict(meaning::Verdict expected, const meaning::Outcome &outcome)
		{
			Assert::AreEqual(std::string(meaning::VerdictName(expected)), std::string(meaning::VerdictName(outcome.verdict)), Wide(outcome.detail).c_str());
		}
	}

	TEST_CLASS(TestMeaningCheck)
	{
	public:
		TEST_METHOD(Meaning_TheSameCodeIsSame)
		{
			const char *code = R"(
				lal 0
				bnt other
				push0
				callk 1 0
				jmp end
			other:
				push0
				callk 2 0
			end:
				ret
			)";
			AssertVerdict(meaning::Verdict::Same, Check(code, code));
		}

		// A bt is a bnt of the not, with the outcomes swapped.
		TEST_METHOD(Meaning_BtIsBntOfTheNot)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				lal 0
				bt other
				push0
				callk 1 0
				jmp end
			other:
				push0
				callk 2 0
			end:
				ret
			)", R"(
				lal 0
				not
				bnt other
				push0
				callk 1 0
				jmp end
			other:
				push0
				callk 2 0
			end:
				ret
			)"));
		}

		// The arms of an if in the other order are another meaning.
		TEST_METHOD(Meaning_SwappedArmsDiffer)
		{
			meaning::Outcome outcome = Check(R"(
				lal 0
				bnt other
				push0
				callk 1 0
				jmp end
			other:
				push0
				callk 2 0
			end:
				ret
			)", R"(
				lal 0
				bnt other
				push0
				callk 2 0
				jmp end
			other:
				push0
				callk 1 0
			end:
				ret
			)");
			AssertVerdict(meaning::Verdict::Diff, outcome);
			Assert::IsTrue(outcome.detail.find("callk 1") != std::string::npos, Wide(outcome.detail).c_str());
		}

		// A load that the optimiser deleted (the accumulator has the value of
		// the store) gives the value of a plain load.
		TEST_METHOD(Meaning_ADeletedLoadIsTheLoad)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				push1
				ldi 5
				sal 0
				push
				callk 3 2
				ret
			)", R"(
				push1
				ldi 5
				sal 0
				lsl 0
				callk 3 2
				ret
			)"));
		}

		// A load before a store reads the old value; a load after it the new
		// one.
		TEST_METHOD(Meaning_ALoadMovedPastAStoreDiffers)
		{
			AssertVerdict(meaning::Verdict::Diff, Check(R"(
				push2
				lsl 0
				ldi 3
				sal 0
				push
				callk 1 4
				ret
			)", R"(
				ldi 3
				sal 0
				push2
				lsl 0
				lsl 0
				callk 1 4
				ret
			)"));
		}

		// A threaded or (Sierra's bt over the second test) and the plain form
		// (a bnt after the join) mean the same: the second bnt reads a value
		// that the bt tested.
		TEST_METHOD(Meaning_ThreadedOrIsThePlainOr)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				lal 0
				bt then
				lal 1
				bnt end
			then:
				push0
				callk 1 0
			end:
				ret
			)", R"(
				lal 0
				bt test
				lal 1
			test:
				bnt end
				push0
				callk 1 0
			end:
				ret
			)"));
		}

		// A test of a constant has no outcome to compare.
		TEST_METHOD(Meaning_ATestOfAConstantIsNoTest)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				ldi 1
				bnt end
				push0
				callk 1 0
			end:
				ret
			)", R"(
				push0
				callk 1 0
				ret
			)"));
		}

		// A test whose two outcomes go to the same effect has no meaning (a
		// last case with no body has a compare and no branch).
		TEST_METHOD(Meaning_ATestWithOneOutcomeIsNoTest)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				lsp 1
				dup
				ldi 2
				eq?
				toss
				ret
			)", R"(
				lsp 1
				dup
				ldi 2
				eq?
				bnt end
			end:
				toss
				ret
			)"));
		}

		// The two outcomes of a test differ only in a value of the
		// accumulator that no instruction reads: the test has no meaning.
		TEST_METHOD(Meaning_ADeadAccumulatorDoesNotMakeATest)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				lal 0
				bnt skip
				ldi 5
			skip:
				push0
				callk 1 0
				ret
			)", R"(
				push0
				callk 1 0
				ret
			)"));
		}

		// A break of the inner loop is not a break of the outer loop.
		TEST_METHOD(Meaning_ABreakLevelDiffers)
		{
			const char *outer = R"(
			outer:
				lal 0
				bnt done
			inner:
				lal 1
				bnt innerEnd
				lal 2
				bnt skip
				jmp TARGET
			skip:
				push0
				callk 1 0
				jmp inner
			innerEnd:
				push0
				callk 2 0
				jmp outer
			done:
				push0
				callk 3 0
				ret
			)";
			std::string breakTwo = outer;
			breakTwo.replace(breakTwo.find("TARGET"), 6, "done");
			std::string breakOne = outer;
			breakOne.replace(breakOne.find("TARGET"), 6, "innerEnd");
			AssertVerdict(meaning::Verdict::Same, Check(breakTwo, breakTwo));
			AssertVerdict(meaning::Verdict::Diff, Check(breakTwo, breakOne));
		}

		// The value of a ret counts when the function returns a value.
		TEST_METHOD(Meaning_TheReturnValueCountsWhenTheFunctionReturnsOne)
		{
			AssertVerdict(meaning::Verdict::Diff, Check("ldi 1\nret", "ldi 2\nret", true));
			AssertVerdict(meaning::Verdict::Same, Check("ldi 1\nret", "ldi 2\nret", false));
		}

		// Code that no path reaches is no effect.
		TEST_METHOD(Meaning_DeadCodeIsNoEffect)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				push0
				callk 1 0
				ret
				push0
				callk 2 0
				ret
			)", R"(
				push0
				callk 1 0
				ret
			)"));
		}

		// A loop with no end, and a loop that counts: the walk ends.
		TEST_METHOD(Meaning_LoopsEnd)
		{
			const char *forever = R"(
			top:
				push0
				callk 1 0
				jmp top
			)";
			AssertVerdict(meaning::Verdict::Same, Check(forever, forever));
			const char *counting = R"(
				ldi 0
				sat 0
			top:
				pushi 10
				lat 0
				lt?
				bnt end
				push1
				lst 0
				callk 1 2
				+at 0
				jmp top
			end:
				ret
			)";
			AssertVerdict(meaning::Verdict::Same, Check(counting, counting));
		}

		// A continue out of a switch leaves the switch value on the stack on
		// each turn of the loop; no instruction reads it, and the walk ends.
		TEST_METHOD(Meaning_AContinueOutOfASwitchEnds)
		{
			const char *code = R"(
			top:
				push0
				callk 1 0
				push
				dup
				ldi 1
				eq?
				bnt other
				jmp top
			other:
				toss
				ret
			)";
			AssertVerdict(meaning::Verdict::Same, Check(code, code));
		}

		// A switch: each case compares the value that the switch pushed.
		TEST_METHOD(Meaning_SwitchCasesCompareTheValue)
		{
			const char *code = R"(
				push0
				callk 1 0
				push
				dup
				ldi 1
				eq?
				bnt case2
				push0
				callk 2 0
				jmp end
			case2:
				dup
				ldi 2
				eq?
				bnt end
				push0
				callk 3 0
			end:
				toss
				ret
			)";
			AssertVerdict(meaning::Verdict::Same, Check(code, code));
			std::string other = code;
			other.replace(other.find("ldi 2"), 5, "ldi 3");
			AssertVerdict(meaning::Verdict::Diff, Check(code, other));
		}

		// A send takes the frames of its messages.
		TEST_METHOD(Meaning_SendMessages)
		{
			const char *code = R"(
				pushi 10
				push1
				pushi 5
				pushi 11
				push0
				lofsa 7
				send 10
				ret
			)";
			AssertVerdict(meaning::Verdict::Same, Check(code, code));
			std::string other = code;
			other.replace(other.find("pushi 5"), 7, "pushi 6");
			AssertVerdict(meaning::Verdict::Diff, Check(code, other));
		}

		// The locals of script 0 are its globals.
		TEST_METHOD(Meaning_TheLocalsOfScript0AreGlobals)
		{
			meaning::Function local = Fn("lal 3\nret", true);
			meaning::Function global = Fn("lag 3\nret", true);
			AssertVerdict(meaning::Verdict::Diff, meaning::Compare(local, global));
			local.localsAreGlobals = true;
			global.localsAreGlobals = true;
			AssertVerdict(meaning::Verdict::Same, meaning::Compare(local, global));
		}

		// The numbers of a sum fold into one, in any group and order; adding
		// 0 is no operation.
		TEST_METHOD(Meaning_TheNumbersOfASumFold)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				pushi 1
				lsl 0
				ldi 1
				add
				add
				ret
			)", R"(
				lsl 0
				ldi 2
				add
				ret
			)", true));
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				lsl 0
				ldi 0
				add
				ret
			)", "lal 0\nret", true));
			AssertVerdict(meaning::Verdict::Diff, Check("lsl 0\nldi 1\nadd\nret", "lsl 0\nldi 2\nadd\nret", true));
		}

		// Code that runs past the end of the function: the end of the
		// function is wrong.
		TEST_METHOD(Meaning_CodeThatRunsPastTheEndIsUncompared)
		{
			meaning::Outcome outcome = Check("push0\ncallk 1 0", "push0\ncallk 1 0\nret");
			AssertVerdict(meaning::Verdict::Uncompared, outcome);
			Assert::AreEqual(std::string("original: runs-past-end"), outcome.detail);
		}

		// A store keeps the truth of a tested value: a load of the variable
		// gets it, so a test of it after the load is no test.
		TEST_METHOD(Meaning_ALoadGetsTheTruthOfTheStoredValue)
		{
			AssertVerdict(meaning::Verdict::Same, Check(R"(
				lal 1
				bnt end
				sal 2
				bnt end
				lal 2
				bnt end
				push0
				callk 1 0
			end:
				ret
			)", R"(
				lal 1
				bnt end
				sal 2
				lal 2
				bnt end
				push0
				callk 1 0
			end:
				ret
			)"));
		}

		// A send with no message is no effect.
		TEST_METHOD(Meaning_ASendWithNoMessageIsNoEffect)
		{
			AssertVerdict(meaning::Verdict::Same, Check("lat 0\nsend 0\npush0\ncallk 1 0\nret", "push0\ncallk 1 0\nret"));
		}

		// A form that the check does not read.
		TEST_METHOD(Meaning_AStackUnderflowIsUncompared)
		{
			meaning::Outcome outcome = Check("toss\nret", "toss\nret");
			AssertVerdict(meaning::Verdict::Uncompared, outcome);
			Assert::AreEqual(std::string("original: stack-underflow"), outcome.detail);
		}

		// The functions pair by key; one with no partner is UNCOMPARED.
		TEST_METHOD(Meaning_FunctionsPairByKey)
		{
			meaning::Function a = Fn("push0\ncallk 1 0\nret");
			meaning::Function b = a;
			b.key = "g";
			std::vector<meaning::FunctionOutcome> rows = meaning::CompareFunctions({ a, b }, { a });
			Assert::AreEqual((size_t)2, rows.size());
			AssertVerdict(meaning::Verdict::Same, rows[0].outcome);
			AssertVerdict(meaning::Verdict::Uncompared, rows[1].outcome);
		}
	};
}
