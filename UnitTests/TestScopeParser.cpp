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

		// The fixtures of the families of this step: the region tree of each
		// function after the verify stage.
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
