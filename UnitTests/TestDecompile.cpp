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
#include "ScriptOMAll.h"
#include "DecompileScript.h"
#include "DecompilerConfig.h"
#include "ResourceContainer.h"
#include "format.h"
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Each family fixture has the shape of a QfG4 function. The fixture uses
// hand-written asm to match Sierra's exact bytecode: SCI Companion's own
// compiler gives other code. Each test asserts a clean decompile (no
// fallback, no asm). See UnitTests\README.md.

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

// A test of a fixture whose decompiled text equals its expected file.
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

        // An export that points into the code of the function before it
        // (Sierra left such stale exports, for example QfG3 script 7) is
        // no procedure: the decompiler leaves it out, with a warning, and the
        // meaning check reads no function there.
        TEST_METHOD(StaleExport_IsLeftOut)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("X3_StaleExport");
            std::string error;
            Assert::IsTrue(CompileFixture(964, "X3_StaleExport", &error), Wide(error).c_str());
            const GameFolderHelper &helper = appState->GetResourceMap().Helper();
            CompiledScript compiled(964, CompiledScriptFlags::RemoveBadExports);
            Assert::IsTrue(compiled.Load(helper, helper.Version, 964), L"setup: the script loads");
            std::vector<uint8_t> script = compiled.GetRawBytes();
            std::unique_ptr<ResourceBlob> heapBlob = helper.MostRecentResource(ResourceType::Heap, 964, ResourceEnumFlags::None);
            Assert::IsNotNull(heapBlob.get(), L"setup: the heap");
            sci::istream heapRead = heapBlob->GetReadStream();
            std::vector<uint8_t> heap(heapRead.GetDataSize());
            heapRead.read_data(heap.data(), (uint32_t)heap.size());
            // SCI1.1: the count of the exports at 6, the exports from 8.
            Assert::AreEqual((uint16_t)2, (uint16_t)(script[6] | (script[7] << 8)), L"setup: two exports");
            uint16_t first = (uint16_t)(script[8] | (script[9] << 8));
            uint16_t inside = first + 4;    // the second ldi of staleFirst
            script[10] = (uint8_t)(inside & 0xff);
            script[11] = (uint8_t)(inside >> 8);

            // The meaning check reads no function at the stale export.
            GlobalCompiledScriptLookups lookups;
            lookups.Load(helper);
            sci::Result<std::vector<meaning::Function>> functions = meaning::ReadScriptData(helper, lookups, appState->GetResourceMap().GetVocab000(), 964, script, &heap);
            Assert::IsTrue(functions.has_value(), L"the patched script reads");
            for (const meaning::Function &function : *functions)
            {
                Assert::AreNotEqual(std::string("export 1"), function.key, L"no function at the stale export");
            }

            // The decompile leaves the export out.
            CompiledScript patched(964, CompiledScriptFlags::RemoveBadExports);
            sci::istream scriptStream(script.data(), (uint32_t)script.size());
            sci::istream heapStream(heap.data(), (uint32_t)heap.size());
            Assert::IsTrue(patched.Load(helper, helper.Version, 964, scriptStream, &heapStream), L"the patched script loads");
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(appState->GetResourceMap(), lookups.GetSelectorTable());
            TestDecompilerResults results;
            std::unique_ptr<sci::Script> decompiled = DecompileScript(config.get(), lookups, appState->GetResourceMap(), 964, patched, results);
            std::stringstream text;
            sci::SourceCodeWriter writer(text, decompiled.get());
            decompiled->OutputSourceCode(writer);
            std::string source = text.str();
            Assert::IsTrue(source.find("(procedure (staleFirst") != std::string::npos, Wide(source).c_str());
            Assert::IsTrue(source.find("staleSecond") == std::string::npos, Wide(source).c_str());
            bool warned = false;
            for (const std::string &warning : results.warnings)
            {
                warned = warned || (warning.find("Export 1 points into the code of another function") != std::string::npos);
            }
            Assert::IsTrue(warned, L"a warning names the export");
            // The function report has a line for it.
            bool reported = false;
            for (const DecompiledFunction &function : results.functions)
            {
                reported = reported || ((function.output == "stale") && (function.offset == inside));
            }
            Assert::IsTrue(reported, L"the function report has the stale export");
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

        // A break out of a loop from inside a switch case.
        FIXTURE_TEST(BreakInSwitchCase, "F9_BreakInSwitchCase", 921)

        // A mid-body continue: a second jump to the loop head.
        FIXTURE_TEST(MidBodyContinue, "F10_MidBodyContinue", 922)

        // A chained comparison compiled with a pprev folds back into one n-ary
        // comparison; a comparison with no shared operand stays an and.
        FIXTURE_TEST(ChainedComparison, "N1_ChainedCompare", 923)

        // Sierra's own shape for a chained comparison, a variable last.
        FIXTURE_TEST(SierraChainedComparison, "N2_SierraChainedCompare", 933)

        // A bare "jmp head" that several branches inside a loop body share.
        FIXTURE_TEST(LatchTrampoline, "F11_LatchTrampoline", 924)

        // A break at the end of an if's else, followed by a statement that
        // another branch also reaches: the if does not hold the shared
        // statement.
        FIXTURE_TEST(BreakJoin, "F12_BreakJoin", 925)

        // A repeat whose break jumps past the latch, to the end of the loop,
        // with a second repeat (which holds a while) between the latch and
        // the end. The first repeat holds the other two loops.
        FIXTURE_TEST(BreakPastLatch, "F14_BreakPastLatch", 936)

        // A while that is the first statement of a repeat: the two loops
        // share their head. With a breakif in the while, the decompiler makes
        // them nested loops. A while with a continue stays one loop. There is
        // no warning.
        TEST_METHOD(SharedLoopHead)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = AssertDecompileMatchesExpected("F15_SharedLoopHead", 937);
            LogWarnings("F15", out);
            Assert::IsTrue(out.warnings.empty(), L"expected no warning");
        }

        // A while that is the first statement of a repeat, with no break in
        // the while: one loop, whose text is a cond in the repeat.
        FIXTURE_TEST(SharedLoopHead_OneLoopStructures, "F16_SharedHeadOneLoop", 938)

        // An or that ends where the and around it ends: Sierra's compiler
        // sends the or's "bt" past that join, to the outer or's end. No round
        // trip: the compiler of this repository gives the or another shape.
        TEST_METHOD(ThreadedOrJoin)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F17_ThreadedOrJoin", 939, false);
        }

        // A loop whose body starts with a switch, with a "jmp" to the loop
        // head that only branches reach.
        FIXTURE_TEST(SwitchHeadContinue, "F18_SwitchHeadContinue", 942)

        // An if with an else, used as a value in a compare that is an operand
        // of an and, or the first operand of an or: the compare takes the push
        // before the if. The join of the if can also store the value.
        FIXTURE_TEST(ValueIfInAnd, "F19_ValueIfInAnd", 943)

        // An or that is the test of an if at the end of a loop body, with an
        // and as its last operand: the and's "bnt" goes past the if's "bnt"
        // to the loop head.
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
        // is unchanged): the compare before it is not cloned into a second
        // operand.
        FIXTURE_TEST(DeadBranch, "B1_DeadBranch", 929)

        // Plain SCI Companion source: nested conds in a loop body. The text is
        // its own oracle, so the compiler's jump dialect round-trips stably.
        FIXTURE_TEST(Plain_CondInLoop, "P2_CondInLoop", 926)

        // Family 1: a conditional branch to the loop head.
        TEST_METHOD(Family1_LoopHeadContinue)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F1_LoopHeadContinue", 900);
            LogWarnings("F1", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsTrue(out.text.find("(if temp1") != std::string::npos,
                L"the if at the end of the loop body should reconstruct");
        }

        // Family 3: a short-circuit and/or value that is joined and then
        // consumed. Each fixture is pinned to its Sierra-shaped expected text.
        FIXTURE_TEST(Family3_ValueIfReturn, "F3_ValueIfReturn", 909)
        FIXTURE_TEST(Family3_OrThreeTerms, "F3_OrThreeTerms", 910)
        FIXTURE_TEST(Family3_OrAndOr, "F3_OrAndOr", 911)
        FIXTURE_TEST(Family3_AndOr, "F3_AndOr", 912)
        FIXTURE_TEST(Family3_IfValueWithElse, "F3_IfValueWithElse", 913)
        FIXTURE_TEST(Family3_AndAsArgument, "F3_AndAsArgument", 914)

        // A shared-then shape ((or (not X) Y) with a synthesized not) is not
        // a Sierra compiler output. The scope parser must refuse it, not
        // merge it as an and with the wrong value. The asm fallback
        // round-trips.
        TEST_METHOD(Unstructured_SharedThenBranch)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("X_SharedThenBranch", 903);
            LogWarnings("X", out);
            Assert::IsTrue(out.fallbacks >= 1, L"expected a clean fallback");
            Assert::IsTrue(out.HasWarningContaining("[scope:parse:no-scope-for-target]"),
                L"expected the scope parser to refuse the shape");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
            // The function report has the refusal.
            Assert::AreEqual((size_t)1, out.functions.size());
            const DecompiledFunction &function = out.functions[0];
            Assert::AreEqual((uint16_t)903, function.script);
            Assert::AreEqual(std::string(), function.className);
            Assert::AreEqual(std::string("asm"), function.output);
            Assert::IsTrue(function.byteCount > 0, L"the bytes of the function");
            Assert::AreEqual(std::string("[scope:parse:no-scope-for-target]"), function.scope);
        }

        // Code that the text cannot have (a super in a procedure, a property
        // past the end of the object): each function falls back to asm, which
        // compiles to the same code.
        TEST_METHOD(Unstructured_NoTextForTheCode)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("X3_NoTextForTheCode", 971);
            LogWarnings("X3", out);
            Assert::AreEqual(3, out.fallbacks, L"expected three fallbacks");
            Assert::IsTrue(out.HasWarningContaining("A super in a procedure."));
            Assert::IsTrue(out.HasWarningContaining("A property with no name."));
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
            for (const DecompiledFunction &function : out.functions)
            {
                Assert::AreEqual(std::string("asm"), function.output);
                Assert::AreEqual(std::string("[scope:values:syntax]"), function.scope);
            }
        }

        // Family 4: a "bnt" to the loop exit inside the body.
        FIXTURE_TEST(Family4_BreakElseEdge, "F4_BreakElseEdge", 904)
        FIXTURE_TEST(Family4_WhileAnd, "F4_WhileAnd", 915)
        FIXTURE_TEST(Family4_WhileOr, "F4_WhileOr", 916)

        // Compound conditions compiled by SCI Companion's own compiler (its
        // "bt" targets the then block). The decompiled text must equal the
        // source, which covers the unchain fixup end to end.
        FIXTURE_TEST(Plain_CompoundConditions, "P1_CompoundConditions", 917)

        // Family 8: a statement shares the block with the test of an if that
        // a "ret" consumes as a value: the statement comes before the return.
        FIXTURE_TEST(Family8_AssignBeforeCondInRet, "F8_AssignBeforeCondInRet", 918)
        FIXTURE_TEST(Family8_DeadValueStatement, "F8_DeadValueStatement", 919)

        // Family 5: an empty leading while, then another loop.
        TEST_METHOD(Family5_EmptyLeadingWhile)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F5_EmptyLeadingWhile", 905);
            LogWarnings("F5", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsTrue(out.text.find("(while") != std::string::npos,
                L"expected the two while loops to reconstruct");
        }

        // Family 6: an empty trailing for, with a dead back-jump.
        TEST_METHOD(Family6_EmptyTrailingFor)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F6_EmptyTrailingFor", 906);
            LogWarnings("F6", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsTrue(out.text.find("(while") != std::string::npos,
                L"expected the outer while to reconstruct");
        }

        // Family 7: a class opcode names a species whose defining script is
        // not in the game. The decompiler synthesizes Unknown_Class_<species>
        // and emits a classdef, so it decompiles and round-trips with no asm.
        FIXTURE_TEST(Family7_UnknownClass, "F7_UnknownClass", 907)

        // Values that the optimiser reuses across a branch.
        FIXTURE_TEST(ReuseAcrossBranch, "V1_ReuseAcrossBranch", 953)
        // The operands of a mul that the optimiser swapped.
        FIXTURE_TEST(SwappedOperands, "V2_SwappedOperands", 965)
        // A store that the push of an argument count or a selector takes.
        FIXTURE_TEST(StoreInSlot, "V3_StoreInSlot", 966)
        // A loop whose value is the test of an if.
        FIXTURE_TEST(LoopValue, "V4_LoopValue", 967)
        // An instance with the name of a property, and a &rest before the last
        // argument.
        FIXTURE_TEST(ObjectNamedLikeAProperty, "O1_ObjectNamedLikeAProperty", 968)
        FIXTURE_TEST(RestBeforeTheLastArgument, "R2_RestBeforeTheLastArgument", 969)
        // Sends that the compiler warns about.
        FIXTURE_TEST(CompilerWarnings, "C5_CompilerWarnings", 970)
        // Classes with no superclass: their properties in the order of the text.
        FIXTURE_TEST(RootClasses, "R3_RootClasses", 972)

        // The meaning check keys the methods of a class with a made-up name (no
        // name string, or a made-up name as its string) by its species: the
        // position of the class, and so its made-up name, can change when the
        // text compiles again (Castle of Dr. Brain script 943).
        TEST_METHOD(RootClasses_MadeUpNameKeyedBySpecies)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("R3_RootClasses");
            std::string error;
            Assert::IsTrue(CompileFixture(972, "R3_RootClasses", &error), Wide(error).c_str());
            bool madeUp = false;
            bool madeUpString = false;
            bool named = false;
            for (const meaning::Function &function : ReadMeaningFunctions(972))
            {
                if (function.display == "Class_972_0::doit")
                {
                    madeUp = true;
                    Assert::IsTrue(function.key.rfind("class ", 0) == 0, Wide(function.key).c_str());
                    Assert::IsTrue(function.key.find("::doit") != std::string::npos, Wide(function.key).c_str());
                }
                if (function.display == "Class_972_9::doit")
                {
                    madeUpString = true;
                    Assert::IsTrue(function.key.rfind("class ", 0) == 0, Wide(function.key).c_str());
                }
                if (function.display == "r3NamedRoot::doit")
                {
                    named = true;
                    Assert::AreEqual(std::string("r3NamedRoot::doit"), function.key);
                }
            }
            Assert::IsTrue(madeUp && madeUpString && named, L"the three methods");
        }

        // Switches: a switch as a value, a case value with a branch, an
        // empty last case (also in a loop), and cases that all return.
        FIXTURE_TEST(SwitchValue, "S1_SwitchValue", 945)
        FIXTURE_TEST(CaseValueBranch, "S2_CaseValueBranch", 946)
        FIXTURE_TEST(EmptyLastCase, "S3_EmptyLastCase", 947)
        FIXTURE_TEST(EmptyLastCaseInLoop, "S4_EmptyLastCaseInLoop", 948)
        FIXTURE_TEST(SwitchAllReturn, "S5_SwitchAllReturn", 949)
        // A case whose test does nothing is the else case.
        FIXTURE_TEST(NoOpCaseTest, "S6_NoOpCaseTest", 963)
        // Dead breaks after the jmp of a then-part: no statement.
        FIXTURE_TEST(DeadBreak, "D1_DeadBreak", 956)
        // Dead code after a jmp and after a break: no statement.
        FIXTURE_TEST(DeadCode, "D2_DeadCode", 958)
        // A continue of a for from an inner while (the compiler makes its
        // jmp to the step of the for).
        FIXTURE_TEST(ContinueTwoInFor, "L2_ContinueTwoInFor", 962)
        // A break of level 2, and a continue in a do loop. No round trip:
        // the compiler of this repository gives the repeat another shape.
        TEST_METHOD(LoopLevels)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("L1_LoopLevels", 954, false);
        }

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

            // One script, with the control-flow and instruction dumps on, for
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
