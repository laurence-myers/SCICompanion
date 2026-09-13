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
#include "StructuralCompare.h"
#include "AppState.h"
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
        TEST_METHOD(Compiler_ValueAndOr)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("C1_ValueAndOr", 908);
            LogWarnings("C1", out);
            Assert::AreEqual(0, out.fallbacks, L"value and/or should not fall back");
            Assert::IsFalse(out.ContainsAsm(), L"value and/or should have no asm");
        }

        // Compiler: a compound assignment to an indexed variable with a simple
        // indexer compiles to Sierra's sequence, so the text round-trips.
        TEST_METHOD(Compiler_IndexedMathAssign)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("C2_IndexedMathAssign", 920);
        }

        // A break out of a loop from inside a switch case. The structurer
        // gathers the case body that jumps to the loop exit into the case and
        // resolves it as a break.
        TEST_METHOD(BreakInSwitchCase)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F9_BreakInSwitchCase", 921);
        }

        // A mid-body continue creates a second back edge and a common latch.
        // The structurer resolves the mid-body jump to the head as a continue.
        TEST_METHOD(MidBodyContinue)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F10_MidBodyContinue", 922);
        }

        // A chained comparison compiled with a pprev folds back into one n-ary
        // comparison; a comparison with no shared operand stays an and.
        TEST_METHOD(ChainedComparison)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("N1_ChainedCompare", 923);
        }

        // A bare "jmp head" shared by several branches inside a loop body folds
        // into the common latch, so the ifs that end there structure.
        TEST_METHOD(LatchTrampoline)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F11_LatchTrampoline", 924);
        }

        // A break at the end of an if's else, followed by a statement that
        // another branch also reaches: the break edge moves to the if's follow,
        // so the if does not gather the shared statement.
        TEST_METHOD(BreakJoin)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F12_BreakJoin", 925);
        }

        // Return values take the golden shape: an if whose branches return is
        // not itself returned; a value-shaped if at the end of the function
        // is; a ++ before the final ret is not a return value.
        TEST_METHOD(ReturnShapes)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("R1_ReturnShapes", 927);
        }

        // Sierra reuses the accumulator: a store, then the pushes of a send
        // whose target or pushed argument is that variable, with no load. The
        // store is a statement of its own; the send reads the variable.
        TEST_METHOD(ReusedAccumulator)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("A1_ReusedAcc", 928);
        }

        // Plain SCI Companion source: nested conds in a loop body. The text is
        // its own oracle, so the compiler's jump dialect round-trips stably.
        TEST_METHOD(Plain_CondInLoop)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("P2_CondInLoop", 926);
        }

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
        TEST_METHOD(Family3_ValueIfReturn)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F3_ValueIfReturn", 909);
        }
        TEST_METHOD(Family3_OrThreeTerms)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F3_OrThreeTerms", 910);
        }
        TEST_METHOD(Family3_OrAndOr)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F3_OrAndOr", 911);
        }
        TEST_METHOD(Family3_AndOr)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F3_AndOr", 912);
        }
        TEST_METHOD(Family3_IfValueWithElse)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F3_IfValueWithElse", 913);
        }
        TEST_METHOD(Family3_AndAsArgument)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F3_AndAsArgument", 914);
        }

        // A shared-then shape ((or (not X) Y) with a synthesized not) is not
        // a Sierra compiler output. The structurer must refuse it, not merge
        // it as an and with the wrong value. The asm fallback round-trips.
        TEST_METHOD(Unstructured_SharedThenBranch)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("X_SharedThenBranch", 903);
            LogWarnings("X", out);
            Assert::IsTrue(out.fallbacks >= 1, L"expected a clean fallback");
            Assert::IsTrue(out.HasWarningContaining("Unstructured branches"),
                L"expected the structurer to refuse the shape");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
        }

        // Family 4: a "bnt" to the loop exit inside the body. Fixed: it becomes
        // an if with a synthesized else-break, and the loop cleanup passes
        // fold the breaks back into the idiomatic shape.
        TEST_METHOD(Family4_BreakElseEdge)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F4_BreakElseEdge", 904);
        }
        TEST_METHOD(Family4_WhileAnd)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F4_WhileAnd", 915);
        }
        TEST_METHOD(Family4_WhileOr)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F4_WhileOr", 916);
        }

        // Compound conditions compiled by SCI Companion's own compiler (its
        // "bt" targets the then block). The decompiled text must equal the
        // source, which covers the unchain fixup end to end.
        TEST_METHOD(Plain_CompoundConditions)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("P1_CompoundConditions", 917);
        }

        // Family 8: a statement shares the block with the test of an if that
        // a "ret" consumes as a value. Fixed: the lift pass climbs out of the
        // first operand of an instruction, so the statement moves before the
        // return.
        TEST_METHOD(Family8_AssignBeforeCondInRet)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F8_AssignBeforeCondInRet", 918);
        }
        TEST_METHOD(Family8_DeadValueStatement)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F8_DeadValueStatement", 919);
        }

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
        TEST_METHOD(Family7_UnknownClass)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F7_UnknownClass", 907);
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
            StructuralCompareResult result = CompareStructural(expected, actual, out ? out : "");
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
