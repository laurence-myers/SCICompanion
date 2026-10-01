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
#include <fstream>
#include "Helper.h"
#include "DecompileHelper.h"
#include "TestSupport.h"
#include "StructuralCompare.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "SCO.h"
#include "CompiledScript.h"
#include "GameFolderHelper.h"
#include "format.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Each family fixture reproduces one QfG4 decompilation failure. The fixture
// uses hand-written asm to match Sierra's exact bytecode. SCI Companion's own
// compiler emits cleaner code that decompiles fine, so a fixture must bypass
// the compiler with an asm block.
//
// Each test pins the current (broken) behaviour. The function falls back to
// assembly with the family's warning. When the fix lands, flip the block
// marked "PART B" to assert a clean decompile (fallbacks == 0, no asm).
//
// Families 1, 2, 5, 6, 7 are fixed. Families 3 and 4 are pinned (reproduced,
// not yet fixed). Family 8 has no minimal fixture. Families 3, 4, 8 share one
// root cause and need an AST-transform pipeline; see UnitTests\README.md.

namespace UnitTests
{
    static void LogWarnings(const std::string &label, const DecompileOutput &out)
    {
        std::string msg = fmt::format("{0}: fallbacks={1}", label, out.fallbacks);
        for (const std::string &w : out.warnings)
        {
            msg += "\n  " + w;
        }
        Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());
    }

// A test of a fixture whose decompiled text equals its expected file. The
// fixture tests of TestDecompile use the default engine; those of
// TestDecompileScope use the scope engine.
#define FIXTURE_TEST(name, fixture, number) \
        TEST_METHOD(name) \
        { \
            _gameFolder = SetUpGameSCI11(); \
            AssertDecompileMatchesExpected(fixture, number); \
        }

    TEST_CLASS(TestDecompile)
    {
    public:
        // A prior test that failed before cleanup would leak the global app
        // state. Catch that here.
        TEST_METHOD_INITIALIZE(Setup)
        {
            Assert::IsNull(appState, L"appState leaked from a prior test");
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // The harness works. A plain script round-trips with no fallback.
        TEST_METHOD(Harness_SmokeRoundTrip)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("D0_Plain", 901);
            Assert::AreEqual(0, out.fallbacks, L"plain fixture should not fall back");
            Assert::IsFalse(out.ContainsAsm(), L"plain fixture should have no asm");
        }

        // The value-and/or compiler fix: a logical and/or used for its value
        // compiles to Sierra-semantic bytecode (the last evaluated operand is
        // left in the accumulator) and round-trips. Fidelity to (and a b) comes
        // with the AST passes; here we only require a stable round trip with no
        // fallback.
        // A public procedure whose real name starts with "proc" but is not a
        // generated procN_i name. Before the fix, ResolvePublicProcedureCalls ->
        // _IsUndeterminedPublicProc -> stoi("Foo") threw std::invalid_argument,
        // which escaped to the batch's catch(...) and killed the run. Here it
        // escapes to the test (DecompileToText does not catch), failing it.
        TEST_METHOD(PublicProcNamedProc_NoThrow)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("PublicProcFoo");
            std::string error;
            Assert::IsTrue(CompileFixture(940, "PublicProcFoo", &error),
                std::wstring(error.begin(), error.end()).c_str());
            DecompileOutput out = DecompileToText(940);
            Assert::IsTrue(out.text.find("procFoo") != std::string::npos,
                L"the public procedure procFoo should decompile with its name intact");
        }

        // A stale .sco with fewer exports than the compiled script. The proc at
        // the missing export index gets the generated proc952_1 name. Before the
        // fix the inverted condition (if (name.empty()) SetName(name)) blanked
        // it; after the fix the generated name survives.
        TEST_METHOD(StaleSco_KeepsProcName)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("StaleScoProcs");
            std::string error;
            Assert::IsTrue(CompileFixture(952, "StaleScoProcs", &error),
                std::wstring(error.begin(), error.end()).c_str());

            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            GlobalCompiledScriptLookups lookups;
            lookups.Load(helper);
            std::unique_ptr<CSCOFile> sco = GetExistingSCOFromScriptNumber(helper, 952, lookups.GetSelectorTable());
            Assert::IsNotNull(sco.get(), L"compile should have written an .sco");
            Assert::IsTrue(sco->GetExports().size() >= 2, L"fixture should export two procs");

            // Drop export index 1 explicitly to simulate a stale .sco.
            std::vector<CSCOPublicExport> &exports = sco->GetExports();
            for (auto it = exports.begin(); it != exports.end(); ++it)
            {
                if (it->GetIndex() == 1)
                {
                    exports.erase(it);
                    break;
                }
            }
            Assert::IsTrue(SaveSCOFile(helper, *sco).has_value(), L"setup: could not write the .sco");

            DecompileOutput out = DecompileToText(952);
            Assert::IsTrue(out.text.find("proc952_1") != std::string::npos,
                L"the undetermined public proc name must survive a stale .sco, not be blanked");
        }

        FIXTURE_TEST(Compiler_ValueAndOr, "C1_ValueAndOr", 908)

        // Sierra's own sequence for an indexed compound assignment loads to
        // the accumulator and pushes ("lati; push"), where SCI Companion
        // emits "lsti". The decompiler folds both.
        FIXTURE_TEST(SierraIndexedMathAssign, "C3_SierraIndexedMathAssign", 932)

        // Compiler: a compound assignment to an indexed variable with a simple
        // indexer compiles to Sierra's sequence, so the text round-trips. Used
        // as a value, it gives the new value.
        FIXTURE_TEST(Compiler_IndexedMathAssign, "C2_IndexedMathAssign", 920)

        // Compiler: a classdef that names the species of a real class does
        // not turn the selector check off for that class.
        TEST_METHOD(Compiler_ClassDefRealClassIsChecked)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("C4_ClassDefRealClass");
            std::string error;
            bool ok = CompileFixture(934, "C4_ClassDefRealClass", &error);
            Assert::IsFalse(ok, L"a bogus selector on a real class must not compile");
            Assert::IsTrue(error.find("c4BogusSelector") != std::string::npos,
                std::wstring(error.begin(), error.end()).c_str());
        }

        // Compiler: an indexer with a side effect in an indexed compound
        // assignment runs twice, and the compiler says so; a plain expression
        // gets no warning.
        TEST_METHOD(Compiler_IndexerSideEffectWarns)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("C5_IndexerSideEffect");
            std::string error;
            std::vector<std::string> warnings;
            bool ok = CompileFixture(935, "C5_IndexerSideEffect", &error, &warnings);
            Assert::IsTrue(ok, std::wstring(error.begin(), error.end()).c_str());
            int twice = 0;
            for (const std::string &w : warnings)
            {
                if (w.find("evaluated twice") != std::string::npos)
                {
                    twice++;
                }
            }
            Assert::AreEqual(1, twice, L"exactly one indexer has a side effect");
        }

        // A break out of a loop from inside a switch case. The structurer
        // gathers the case body that jumps to the loop exit into the case and
        // resolves it as a break.
        FIXTURE_TEST(BreakInSwitchCase, "F9_BreakInSwitchCase", 921)

        // A mid-body continue creates a second back edge and a common latch.
        // The structurer resolves the mid-body jump to the head as a continue.
        FIXTURE_TEST(MidBodyContinue, "F10_MidBodyContinue", 922)

        // A chained comparison compiled with a pprev folds back into one n-ary
        // comparison; a comparison with no shared operand stays an and.
        FIXTURE_TEST(ChainedComparison, "N1_ChainedCompare", 923)

        // Sierra's own shape for a chained comparison, a variable last.
        FIXTURE_TEST(SierraChainedComparison, "N2_SierraChainedCompare", 933)

        // A bare "jmp head" shared by several branches inside a loop body folds
        // into the common latch, so the ifs that end there structure.
        FIXTURE_TEST(LatchTrampoline, "F11_LatchTrampoline", 924)

        // A break at the end of an if's else, followed by a statement that
        // another branch also reaches: the break edge moves to the if's follow,
        // so the if does not gather the shared statement.
        FIXTURE_TEST(BreakJoin, "F12_BreakJoin", 925)

        // A repeat whose break jumps past the latch, to the loop's follow node,
        // with a second repeat (which holds a while) between the latch and the
        // follow node. The first repeat holds the other two loops, so they are
        // built first, as any nested loop is.
        FIXTURE_TEST(BreakPastLatch, "F14_BreakPastLatch", 936)

        // A while that is the first statement of a repeat: the two loops
        // share their head. With a breakif in the while they do not structure
        // as one loop, so the decompiler makes them nested loops. A while with
        // a continue stays one loop. The messages are those of the analysis
        // that is used, so the failed first analysis leaves no warning.
        TEST_METHOD(SharedLoopHead)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = AssertDecompileMatchesExpected("F15_SharedLoopHead", 937);
            LogWarnings("F15", out);
            Assert::IsTrue(out.warnings.empty(), L"expected no warning");
        }

        // A while that is the first statement of a repeat, with no break in
        // the while: as one loop it structures, so its text (a cond in the
        // repeat) stays, and no second analysis runs.
        FIXTURE_TEST(SharedLoopHead_OneLoopStructures, "F16_SharedHeadOneLoop", 938)

        // An or that ends where the and around it ends: Sierra's compiler
        // sends the or's "bt" past that join, to the outer or's end. The
        // structurer moves the "bt" back onto the join, so the or builds.
        // No round trip: SCI Companion's compiler also sends the "bnt" of the
        // and past that join, and the structurer does not build that shape.
        TEST_METHOD(ThreadedOrJoin)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F17_ThreadedOrJoin", 939, false);
        }

        // A loop whose body starts with a switch: a "jmp" to the loop head
        // that only branches reach folds into the common latch, as it does
        // when the head is plain code.
        FIXTURE_TEST(SwitchHeadContinue, "F18_SwitchHeadContinue", 942)

        // An if with an else, used as a value in a compare that is an operand
        // of an and, or the first operand of an or: the if is built before
        // the and or the or, and the compare takes the push before the if.
        // When the join of the if stores the value, the if at the join is
        // built first, as before.
        FIXTURE_TEST(ValueIfInAnd, "F19_ValueIfInAnd", 943)

        // An or that is the test of an if at the end of a loop body, with an
        // and as its last operand: the and's "bnt" goes past the if's "bnt"
        // to the loop head. The structurer moves it back onto the if's "bnt".
        FIXTURE_TEST(OrAndLoopHead, "F20_OrAndLoopHead", 944)

        // Return values take the golden shape: an if whose branches return is
        // not itself returned; a value-shaped if at the end of the function
        // is; a ++ before the final ret is not a return value.
        FIXTURE_TEST(ReturnShapes, "R1_ReturnShapes", 927)

        // Sierra reuses the accumulator: a store, then the pushes of a send
        // whose target or pushed argument is that variable, with no load. The
        // store is a statement of its own; the send reads the variable.
        FIXTURE_TEST(ReusedAccumulator, "A1_ReusedAcc", 928)

        // A selector pushed as "push" after an ldi of its number (the
        // optimizer's reuse), and a literal argument repeated with dup.
        FIXTURE_TEST(ReusedSelector, "A2_ReusedSelector", 930)

        // A send whose last argument is a value if, with earlier pushes before
        // the if: the pushes belong to the send across the join.
        FIXTURE_TEST(ValueIfArgument, "F13_ValueIfArgument", 931)

        // A bnt right after a bnt to the same target is dead (the accumulator
        // is unchanged). It is deleted before control-flow analysis, so the
        // compare before it is not cloned into a second operand.
        FIXTURE_TEST(DeadBranch, "B1_DeadBranch", 929)

        // Plain SCI Companion source: nested conds in a loop body. The text is
        // its own oracle, so the compiler's jump dialect round-trips stably.
        FIXTURE_TEST(Plain_CondInLoop, "P2_CondInLoop", 926)

        // Family 1: a conditional branch to the loop head. Fixed: the common
        // latch resolves to the loop head, so the if reconstructs.
        TEST_METHOD(Family1_LoopHeadContinue)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F1_LoopHeadContinue", 900);
            LogWarnings("F1", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsFalse(out.HasWarningContaining("Inconsistent then/else"),
                L"the Family 1 warning should be gone");
            Assert::IsTrue(out.text.find("(if temp1") != std::string::npos,
                L"the if at the end of the loop body should reconstruct");
        }

        // Family 3: a short-circuit and/or value that is joined and then
        // consumed. Fixed: the structurer builds ifs and ors from the immediate
        // post-dominators, the chunk stage treats an if as a value, and the
        // IfThenToAnd pass gives the and/or text. Each fixture is pinned to
        // its Sierra-shaped expected text.
        FIXTURE_TEST(Family3_ValueIfReturn, "F3_ValueIfReturn", 909)
        FIXTURE_TEST(Family3_OrThreeTerms, "F3_OrThreeTerms", 910)
        FIXTURE_TEST(Family3_OrAndOr, "F3_OrAndOr", 911)
        FIXTURE_TEST(Family3_AndOr, "F3_AndOr", 912)
        FIXTURE_TEST(Family3_IfValueWithElse, "F3_IfValueWithElse", 913)
        FIXTURE_TEST(Family3_AndAsArgument, "F3_AndAsArgument", 914)

        // A shared-then shape ((or (not X) Y) with a synthesized not) is not
        // a Sierra compiler output. The structurer (or, with the scope
        // engine, the scope parser) must refuse it, not merge it as an and
        // with the wrong value. The asm fallback round-trips.
        TEST_METHOD(Unstructured_SharedThenBranch)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("X_SharedThenBranch", 903);
            LogWarnings("X", out);
            Assert::IsTrue(out.fallbacks >= 1, L"expected a clean fallback");
            Assert::IsTrue(out.HasWarningContaining("Unstructured branches") || out.HasWarningContaining("[scope:parse:no-scope-for-target]"),
                L"expected the structurer (classic) or the scope parser to refuse the shape");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
        }

        // The engine of SCIC_DECOMPILE_ENGINE, on a script that both engines
        // decompile to the same text. With classic, the control-flow stages
        // of the scope engine run in shadow mode: the scope column has their
        // result. With auto and with scope, the scope engine gives each
        // function.
        TEST_METHOD(Engine_TheVariableChoosesTheEngine)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("F3_AndOr");
            std::string error;
            Assert::IsTrue(CompileFixture(912, "F3_AndOr", &error), Wide(error).c_str());

            DecompileOutput classic;
            {
                ScopedEnvironmentVariable engine("SCIC_DECOMPILE_ENGINE", "classic");
                classic = DecompileToText(912);
            }
            Assert::IsFalse(classic.ContainsAsm(), Wide(classic.text).c_str());
            Assert::IsFalse(classic.functions.empty(), L"a report of each function");
            for (const DecompiledFunction &function : classic.functions)
            {
                Assert::IsTrue(function.engine == DecompileEngine::Classic, Wide(function.name).c_str());
                Assert::AreEqual(std::string("classic"), function.output, Wide(function.name).c_str());
                Assert::AreEqual(std::string("ok"), function.classic, Wide(function.name).c_str());
                Assert::AreEqual(std::string("ok"), function.scope, Wide(function.name).c_str());
            }

            DecompileOutput automatic;
            {
                ScopedEnvironmentVariable engine("SCIC_DECOMPILE_ENGINE", "auto");
                automatic = DecompileToText(912);
            }
            Assert::AreEqual(classic.text, automatic.text, L"auto gives the same text");
            Assert::AreEqual(classic.functions.size(), automatic.functions.size());
            for (const DecompiledFunction &function : automatic.functions)
            {
                Assert::IsTrue(function.engine == DecompileEngine::ScopeThenClassic, Wide(function.name).c_str());
                Assert::AreEqual(std::string("scope"), function.output, Wide(function.name).c_str());
                Assert::AreEqual(std::string("ok"), function.scope, Wide(function.name).c_str());
                Assert::AreEqual(std::string(), function.classic, Wide(function.name).c_str());
            }

            DecompileOutput scope;
            {
                ScopedEnvironmentVariable engine("SCIC_DECOMPILE_ENGINE", "scope");
                scope = DecompileToText(912);
            }
            Assert::AreEqual(classic.text, scope.text, L"scope gives the same text");
            for (const DecompiledFunction &function : scope.functions)
            {
                Assert::IsTrue(function.engine == DecompileEngine::Scope, Wide(function.name).c_str());
                Assert::AreEqual(std::string("scope"), function.output, Wide(function.name).c_str());
            }
        }

        // A function that the scope engine cannot decompile: with auto, the
        // classic engine runs too (here it fails as well), and the scope
        // failure is no warning; with scope, the classic engine does not run,
        // and the scope failure is a warning.
        TEST_METHOD(Engine_AutoRunsClassicWhenScopeFails)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("X_SharedThenBranch");
            std::string error;
            Assert::IsTrue(CompileFixture(903, "X_SharedThenBranch", &error), Wide(error).c_str());

            DecompileOutput automatic;
            {
                ScopedEnvironmentVariable engine("SCIC_DECOMPILE_ENGINE", "auto");
                automatic = DecompileToText(903);
            }
            Assert::AreEqual((size_t)1, automatic.functions.size());
            Assert::AreEqual(std::string("asm"), automatic.functions[0].output);
            Assert::AreEqual(std::string("[scope:parse:no-scope-for-target]"), automatic.functions[0].scope);
            Assert::IsTrue(automatic.functions[0].classic.rfind("graph: ", 0) == 0, Wide(automatic.functions[0].classic).c_str());
            Assert::IsFalse(automatic.HasWarningContaining("[scope:"), L"with auto, a scope failure is no warning");

            DecompileOutput scope;
            {
                ScopedEnvironmentVariable engine("SCIC_DECOMPILE_ENGINE", "scope");
                scope = DecompileToText(903);
            }
            Assert::AreEqual((size_t)1, scope.functions.size());
            Assert::AreEqual(std::string("asm"), scope.functions[0].output);
            Assert::AreEqual(std::string(), scope.functions[0].classic);
            Assert::IsTrue(scope.HasWarningContaining("[scope:parse:no-scope-for-target]"), L"with scope, a failure is a warning");
        }

        // An unknown engine in SCIC_DECOMPILE_ENGINE stops the decompile: a
        // wrong name must not give the classic engine silently.
        TEST_METHOD(Engine_AnUnknownVariableValueThrows)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("F3_AndOr");
            std::string error;
            Assert::IsTrue(CompileFixture(912, "F3_AndOr", &error), Wide(error).c_str());
            ScopedEnvironmentVariable engine("SCIC_DECOMPILE_ENGINE", "scoop");
            bool thrown = false;
            try
            {
                DecompileToText(912);
            }
            catch (const sci::DataError &e)
            {
                thrown = (e.code() == sci::ErrorCode::Usage) && (std::string(e.what()).find("scoop") != std::string::npos);
            }
            Assert::IsTrue(thrown, L"a usage error that names the value");
        }

        // The function report of a function that the classic engine cannot
        // structure: the output is asm, and the classic column has the stage
        // and the message. The scope parser in shadow mode refuses it too.
        TEST_METHOD(FunctionReport_AClassicFailure_HasTheStageAndTheMessage)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("X_SharedThenBranch");
            std::string error;
            Assert::IsTrue(CompileFixture(903, "X_SharedThenBranch", &error), Wide(error).c_str());
            ScopedEnvironmentVariable engine("SCIC_DECOMPILE_ENGINE", nullptr);
            DecompileOutput out = DecompileToText(903);
            Assert::AreEqual((size_t)1, out.functions.size());
            const DecompiledFunction &function = out.functions[0];
            Assert::AreEqual((uint16_t)903, function.script);
            Assert::AreEqual(std::string(), function.className);
            Assert::AreEqual(std::string("asm"), function.output);
            Assert::IsTrue(function.byteCount > 0, L"the bytes of the function");
            Assert::IsTrue(function.classic.rfind("graph: Unstructured branches", 0) == 0, Wide(function.classic).c_str());
            Assert::AreEqual(std::string("[scope:parse:no-scope-for-target]"), function.scope);
        }

        // Family 4: a "bnt" to the loop exit inside the body. Fixed: it becomes
        // an if with a synthesized else-break, and the loop cleanup passes
        // fold the breaks back into the idiomatic shape.
        FIXTURE_TEST(Family4_BreakElseEdge, "F4_BreakElseEdge", 904)
        FIXTURE_TEST(Family4_WhileAnd, "F4_WhileAnd", 915)
        FIXTURE_TEST(Family4_WhileOr, "F4_WhileOr", 916)

        // Compound conditions compiled by SCI Companion's own compiler (its
        // "bt" targets the then block). The decompiled text must equal the
        // source, which covers the unchain fixup end to end.
        FIXTURE_TEST(Plain_CompoundConditions, "P1_CompoundConditions", 917)

        // Family 8: a statement shares the block with the test of an if that
        // a "ret" consumes as a value. Fixed: the lift pass climbs out of the
        // first operand of an instruction, so the statement moves before the
        // return.
        FIXTURE_TEST(Family8_AssignBeforeCondInRet, "F8_AssignBeforeCondInRet", 918)
        FIXTURE_TEST(Family8_DeadValueStatement, "F8_DeadValueStatement", 919)

        // Family 5: an empty leading while swallows the next loop. Fixed: child
        // collection is bounded to the loop's address range.
        TEST_METHOD(Family5_EmptyLeadingWhile)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F5_EmptyLeadingWhile", 905);
            LogWarnings("F5", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsFalse(out.HasWarningContaining("Unable to replace node in follow nodes"),
                L"the Family 5 warning should be gone");
            Assert::IsTrue(out.text.find("(while") != std::string::npos,
                L"expected the two while loops to reconstruct");
        }

        // Family 6: an empty trailing for leaves a pruned dead back-jump. Fixed:
        // the folded exit is retargeted to the dead jump, giving a real exit.
        TEST_METHOD(Family6_EmptyTrailingFor)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F6_EmptyTrailingFor", 906);
            LogWarnings("F6", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsFalse(out.HasWarningContaining("Can't find follow node"),
                L"the Family 6 warning should be gone");
            Assert::IsTrue(out.text.find("(while") != std::string::npos,
                L"expected the outer while to reconstruct");
        }

        // Family 7: a class opcode names a species whose defining script is
        // not in the game. The decompiler synthesizes Unknown_Class_<species>
        // and emits a classdef, so it decompiles and round-trips with no asm.
        FIXTURE_TEST(Family7_UnknownClass, "F7_UnknownClass", 907)

        // Regression guard: total assembly fallbacks across the template game
        // must not grow, and no new script may fall back. Lower BASELINE and
        // shrink the allowlist deliberately when a fix helps.
        TEST_METHOD(TemplateGame_FallbackBaseline)
        {
            _gameFolder = SetUpGameSCI11();
            std::vector<std::string> failed;
            std::vector<std::string> warnings;
            int processed = 0;
            int fallbacks = CountFallbacksAllScripts(&failed, &processed, &warnings);
            std::string msg = fmt::format("Template: {0} scripts, {1} fallbacks in {2}", processed, fallbacks, failed.size());
            for (const std::string &name : failed)
            {
                msg += "\n  " + name;
            }
            for (const std::string &w : warnings)
            {
                msg += "\n    " + w;
            }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());

            // Floor: the guard is meaningless if no scripts decompiled. The
            // template has more than 80 scripts.
            Assert::IsTrue(processed >= 80, L"too few scripts decompiled; check the template game data");

            const int BASELINE = 0;   // was 7; every template script now decompiles. Keep at 0.
            Assert::IsTrue(fallbacks <= BASELINE, L"template fallbacks grew beyond baseline");

            // The set of scripts that fall back. A new failure is caught even
            // when a fix removes a different one. Empty: nothing falls back.
            std::set<std::string> allowed = {};
            for (const std::string &name : failed)
            {
                Assert::IsTrue(allowed.count(name) == 1,
                    std::wstring(L"new fallback script: ").append(name.begin(), name.end()).c_str());
            }
        }

        // Regression guard: the decompiled text of every template script
        // recompiles. Catches a newly-emitted construct the compiler rejects.
        // Every script must recompile; a failure entry carries the compiler
        // error for diagnosis.
        TEST_METHOD(TemplateGame_Recompiles)
        {
            _gameFolder = SetUpGameSCI11();
            std::vector<std::string> failed;
            int processed = 0;
            RecompileAllDecompiledScripts(&failed, &processed);
            std::string msg = fmt::format("Recompile: {0} scripts, {1} failed", processed, failed.size());
            for (const std::string &name : failed)
            {
                msg += "\n  " + name;
            }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());

            Assert::IsTrue(processed >= 80, L"too few scripts processed; check the template game data");
            Assert::AreEqual((size_t)0, failed.size(),
                std::wstring(msg.begin(), msg.end()).c_str());
        }

        // Regression guard: the decompiled text of every template script matches
        // its committed snapshot. Any output change fails here. Accept an
        // intended change with RunTests.ps1 -UpdateSnapshots, then commit the
        // snapshot change with the code.
        TEST_METHOD(TemplateGame_Snapshot)
        {
            _gameFolder = SetUpGameSCI11();
            SnapshotResult r = CompareTemplateSnapshots();
            std::string msg = fmt::format("Snapshot: {0} scripts, {1} changed, {2} missing",
                r.processed, r.mismatched.size(), r.missingExpected.size());
            for (const std::string &name : r.mismatched) { msg += "\n  changed: " + name; }
            for (const std::string &name : r.missingExpected) { msg += "\n  missing: " + name; }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());

            Assert::IsTrue(r.processed >= 80, L"too few scripts; check the template game data");
            Assert::IsTrue(r.missingExpected.empty(),
                L"a snapshot is missing; run RunTests.ps1 -UpdateSnapshots to create it");
            Assert::IsTrue(r.mismatched.empty(),
                L"a snapshot changed; review then run RunTests.ps1 -UpdateSnapshots");
        }

    private:
        std::string _gameFolder;
    };

    // The fixtures with the scope engine: the text equals the expected file
    // of the fixture (its .scope.expected.sc when it has one).
    TEST_CLASS(TestDecompileScope)
    {
    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            Assert::IsNull(appState, L"appState leaked from a prior test");
            _engine = std::make_unique<ScopedEnvironmentVariable>("SCIC_DECOMPILE_ENGINE", "scope");
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
            _engine.reset();
        }

        FIXTURE_TEST(Compiler_ValueAndOr, "C1_ValueAndOr", 908)
        FIXTURE_TEST(Compiler_IndexedMathAssign, "C2_IndexedMathAssign", 920)
        FIXTURE_TEST(SierraIndexedMathAssign, "C3_SierraIndexedMathAssign", 932)
        FIXTURE_TEST(ReusedAccumulator, "A1_ReusedAcc", 928)
        FIXTURE_TEST(ReusedSelector, "A2_ReusedSelector", 930)
        FIXTURE_TEST(DeadBranch, "B1_DeadBranch", 929)
        FIXTURE_TEST(ValueIfArgument, "F13_ValueIfArgument", 931)
        FIXTURE_TEST(ValueIfInAnd, "F19_ValueIfInAnd", 943)
        FIXTURE_TEST(ReturnShapes, "R1_ReturnShapes", 927)
        FIXTURE_TEST(Family3_ValueIfReturn, "F3_ValueIfReturn", 909)
        FIXTURE_TEST(Family3_OrThreeTerms, "F3_OrThreeTerms", 910)
        FIXTURE_TEST(Family3_OrAndOr, "F3_OrAndOr", 911)
        FIXTURE_TEST(Family3_AndOr, "F3_AndOr", 912)
        FIXTURE_TEST(Family3_IfValueWithElse, "F3_IfValueWithElse", 913)
        FIXTURE_TEST(Family3_AndAsArgument, "F3_AndAsArgument", 914)
        FIXTURE_TEST(Family7_UnknownClass, "F7_UnknownClass", 907)
        FIXTURE_TEST(Family8_AssignBeforeCondInRet, "F8_AssignBeforeCondInRet", 918)
        FIXTURE_TEST(Family8_DeadValueStatement, "F8_DeadValueStatement", 919)
        // Values that the optimiser reuses across a branch.
        FIXTURE_TEST(ReuseAcrossBranch, "V1_ReuseAcrossBranch", 953)
        FIXTURE_TEST(ChainedComparison, "N1_ChainedCompare", 923)
        FIXTURE_TEST(SierraChainedComparison, "N2_SierraChainedCompare", 933)
        FIXTURE_TEST(Family4_BreakElseEdge, "F4_BreakElseEdge", 904)
        FIXTURE_TEST(Family4_WhileAnd, "F4_WhileAnd", 915)
        FIXTURE_TEST(Family4_WhileOr, "F4_WhileOr", 916)
        FIXTURE_TEST(Plain_CompoundConditions, "P1_CompoundConditions", 917)
        FIXTURE_TEST(BreakInSwitchCase, "F9_BreakInSwitchCase", 921)
        FIXTURE_TEST(MidBodyContinue, "F10_MidBodyContinue", 922)
        FIXTURE_TEST(LatchTrampoline, "F11_LatchTrampoline", 924)
        FIXTURE_TEST(BreakJoin, "F12_BreakJoin", 925)
        FIXTURE_TEST(BreakPastLatch, "F14_BreakPastLatch", 936)
        FIXTURE_TEST(SharedLoopHead, "F15_SharedLoopHead", 937)
        FIXTURE_TEST(SharedLoopHead_OneLoopStructures, "F16_SharedHeadOneLoop", 938)
        FIXTURE_TEST(SwitchHeadContinue, "F18_SwitchHeadContinue", 942)
        FIXTURE_TEST(OrAndLoopHead, "F20_OrAndLoopHead", 944)
        FIXTURE_TEST(Plain_CondInLoop, "P2_CondInLoop", 926)
        // Switches: a switch as a value, a case value with a branch, an
        // empty last case (also in a loop), and cases that all return.
        FIXTURE_TEST(SwitchValue, "S1_SwitchValue", 945)
        FIXTURE_TEST(CaseValueBranch, "S2_CaseValueBranch", 946)
        FIXTURE_TEST(EmptyLastCase, "S3_EmptyLastCase", 947)
        FIXTURE_TEST(EmptyLastCaseInLoop, "S4_EmptyLastCaseInLoop", 948)
        FIXTURE_TEST(SwitchAllReturn, "S5_SwitchAllReturn", 949)

        // The fixtures with no expected file: each round-trips with no asm.
        TEST_METHOD(RoundTrips)
        {
            for (const auto &fixture : { std::make_pair("F1_LoopHeadContinue", 900), std::make_pair("F5_EmptyLeadingWhile", 905), std::make_pair("F6_EmptyTrailingFor", 906) })
            {
                _gameFolder = SetUpGameSCI11();
                DecompileOutput out = DecompileAndRoundTrip(fixture.first, (uint16_t)fixture.second);
                Assert::AreEqual(0, out.fallbacks, Wide(fixture.first).c_str());
                Assert::IsFalse(out.ContainsAsm(), Wide(fixture.first).c_str());
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }
        // No round trip: the compiler of this repository gives the or
        // another shape.
        TEST_METHOD(ThreadedOrJoin)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F17_ThreadedOrJoin", 939, false);
        }

    private:
        std::string _gameFolder;
        std::unique_ptr<ScopedEnvironmentVariable> _engine;
    };

    // Not in the default run. Decompiles named template scripts with the
    // control-flow dump on and logs every warning, to diagnose a failure:
    //   RunTests.ps1 -Filter "FullyQualifiedName~DiagnosticDumps"
    TEST_CLASS(DiagnosticDumps)
    {
    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
            if (_existingGame)
            {
                CleanUpExistingGame();
                _existingGame = false;
            }
        }

        // Decompiles every script of an existing game (read-only) to a folder,
        // for the golden diff with Tools\CompareDecompile.ps1. Driven by
        // environment variables so no local path is in the source:
        //   SCICOMP_DUMP_GAME   game folder (required; the test skips without it)
        //   SCICOMP_DUMP_OUT    output folder (required)
        //   SCICOMP_DUMP_NAMES  golden folder used to name the files (optional)
        // Warnings go to <out>\_warnings.txt.
        TEST_METHOD(Dump_ExistingGame)
        {
            const char *game = getenv("SCICOMP_DUMP_GAME");
            const char *out = getenv("SCICOMP_DUMP_OUT");
            const char *namesDir = getenv("SCICOMP_DUMP_NAMES");
            if (!game || !out)
            {
                Logger::WriteMessage(L"Skipped: set SCICOMP_DUMP_GAME and SCICOMP_DUMP_OUT.");
                return;
            }
            SetUpExistingGame(game);
            _existingGame = true;

            // One script, with the control-flow and chunk-tree dumps on, for
            // diagnosing a failure. SCICOMP_DUMP_SCRIPT is the script number.
            if (const char *one = getenv("SCICOMP_DUMP_SCRIPT"))
            {
                uint16_t number = static_cast<uint16_t>(atoi(one));
                DecompileOutput single = DecompileToText(number, true, true);
                std::string report = single.text + "\n\n===== warnings =====\n";
                for (const std::string &w : single.warnings)
                {
                    report += w + "\n";
                }
                CreateDirectoryA(out, nullptr);
                std::ofstream file(fmt::format("{0}\\_script_{1}.txt", out, number), std::ios::binary);
                file << report;
                std::ofstream selectors(fmt::format("{0}\\_selectors.txt", out), std::ios::binary);
                selectors << DumpSelectorTable();
                Logger::WriteMessage(L"Wrote single-script dump.");
                return;
            }

            std::vector<std::string> warnings;
            int processed = 0;
            int fallbacks = DumpAllScripts(out, namesDir ? namesDir : "", &warnings, &processed);

            std::string report = fmt::format("Dump: {0} scripts, {1} fallbacks\n", processed, fallbacks);
            for (const std::string &w : warnings)
            {
                report += w + "\n";
            }
            std::ofstream file(std::string(out) + "\\_warnings.txt", std::ios::binary);
            file << report;
            Logger::WriteMessage(std::wstring(report.begin(), report.end()).c_str());
        }

        // The structural compare of a dump against golden sources, per
        // function: SCICOMP_COMPARE_EXPECTED (golden folder),
        // SCICOMP_COMPARE_ACTUAL (the dump), SCICOMP_COMPARE_OUT (report and
        // per-difference files). The template game only provides the parser's
        // context.
        TEST_METHOD(Compare_Structural)
        {
            const char *expected = getenv("SCICOMP_COMPARE_EXPECTED");
            const char *actual = getenv("SCICOMP_COMPARE_ACTUAL");
            const char *out = getenv("SCICOMP_COMPARE_OUT");
            if (!expected || !actual)
            {
                Logger::WriteMessage(L"Skipped: set SCICOMP_COMPARE_EXPECTED and SCICOMP_COMPARE_ACTUAL.");
                return;
            }
            _gameFolder = SetUpGameSCI11();
            StructuralCompareResult result = CompareStructural(expected, actual, out ? out : "", appState->GetVersion());
            std::string report = result.Report();
            Logger::WriteMessage(std::wstring(report.begin(), report.end()).c_str());
        }

        TEST_METHOD(Dump_FailingTemplateScripts)
        {
            _gameFolder = SetUpGameSCI11();
            // SCICOMP_DUMP_TITLES overrides the list: comma-separated titles.
            std::vector<std::string> titles = { "ScrollableInventory", "SaveRestoreDialog", "Gauge" };
            if (const char *env = getenv("SCICOMP_DUMP_TITLES"))
            {
                titles.clear();
                std::string list = env;
                size_t start = 0;
                while (start <= list.size())
                {
                    size_t comma = list.find(',', start);
                    if (comma == std::string::npos)
                    {
                        comma = list.size();
                    }
                    if (comma > start)
                    {
                        titles.push_back(list.substr(start, comma - start));
                    }
                    start = comma + 1;
                }
            }
            for (const std::string &title : titles)
            {
                DecompileOutput out;
                if (DecompileTemplateScriptByTitle(title, out))
                {
                    std::string msg = std::string("##### ") + title + "\n";
                    for (const std::string &w : out.warnings)
                    {
                        msg += w + "\n";
                    }
                    Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());
                }
            }
        }

    private:
        std::string _gameFolder;
        bool _existingGame = false;
    };
}
