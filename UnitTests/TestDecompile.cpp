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
#include "Helper.h"
#include "DecompileHelper.h"
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

        // Family 3: an and/or value materialised through branches. NOT fixed.
        // A correct fix needs a new "condition value" structure so the boolean
        // is not mis-valued; see the plan. This pins the current fallback and
        // proves the asm round-trips. Flip to a clean-decompile assert when
        // Family 3 lands.
        TEST_METHOD(Family3_ValueJoin)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F3_ValueJoin", 903);
            LogWarnings("F3", out);
            Assert::IsTrue(out.fallbacks >= 1, L"expected a fallback (Family 3 not fixed yet)");
            Assert::IsTrue(out.HasWarningContaining("Exit needs two predecessors"),
                L"expected the Family 3 control-flow warning");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
        }

        // Family 4: a compound condition whose else edge is the loop exit, next
        // to a break. NOT fixed. The fix needs delicate surgery on the
        // break/continue restructuring, which affects every loop; see the plan.
        // This pins the current fallback and proves the asm round-trips.
        TEST_METHOD(Family4_BreakElseEdge)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F4_BreakElseEdge", 904);
            LogWarnings("F4", out);
            Assert::IsTrue(out.fallbacks >= 1, L"expected a fallback (Family 4 not fixed yet)");
            Assert::IsTrue(out.HasWarningContaining("Exit needs two predecessors"),
                L"expected the Family 4 control-flow warning");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
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

        // Family 7: a class opcode names a species that is not in the table.
        // A bare number has no class-opcode form and would emit the wrong
        // opcode, so the function correctly stays as asm to round-trip. The fix
        // replaces the cryptic "Unexpected opcode" with a clear message.
        TEST_METHOD(Family7_UnknownClass)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F7_UnknownClass", 907);
            LogWarnings("F7", out);
            Assert::IsTrue(out.fallbacks >= 1, L"expected a fallback");
            Assert::IsTrue(out.HasWarningContaining("has no name"),
                L"expected the clear unknown-class message");
            Assert::IsFalse(out.HasWarningContaining("Unexpected opcode"),
                L"the cryptic message should be gone");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
        }

        // Regression guard: total assembly fallbacks across the template game
        // must not grow, and no new script may fall back. Lower BASELINE and
        // shrink the allowlist deliberately when a fix helps.
        TEST_METHOD(TemplateGame_FallbackBaseline)
        {
            _gameFolder = SetUpGameSCI11();
            std::vector<std::string> failed;
            int processed = 0;
            int fallbacks = CountFallbacksAllScripts(&failed, &processed);
            std::string msg = fmt::format("Template: {0} scripts, {1} fallbacks in {2}", processed, fallbacks, failed.size());
            for (const std::string &name : failed)
            {
                msg += "\n  " + name;
            }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());

            // Floor: the guard is meaningless if no scripts decompiled. The
            // template has more than 80 scripts.
            Assert::IsTrue(processed >= 80, L"too few scripts decompiled; check the template game data");

            const int BASELINE = 5;   // was 7; Family 1 and Family 6 fixes each removed one. Lower again when a fix helps.
            Assert::IsTrue(fallbacks <= BASELINE, L"template fallbacks grew beyond baseline");

            // The set of scripts that fall back. A new failure is caught even
            // when a fix removes a different one. Remove entries as fixes land.
            std::set<std::string> allowed = {
                "ScrollableInventory", "SaveRestoreDialog", "Gauge", "System" };
            for (const std::string &name : failed)
            {
                Assert::IsTrue(allowed.count(name) == 1,
                    std::wstring(L"new fallback script: ").append(name.begin(), name.end()).c_str());
            }
        }

        // Regression guard: the decompiled text of every template script
        // recompiles. Catches a newly-emitted construct the compiler rejects.
        // Two scripts have pre-existing round-trip defects unrelated to this
        // work (Main emits a name the parser rejects; SaveRestoreDialog falls
        // back to asm that does not re-parse). They are allowlisted; shrink the
        // list when they are fixed. A script not on the list must recompile.
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
            std::set<std::string> allowed = { "Main", "SaveRestoreDialog" };
            for (const std::string &name : failed)
            {
                Assert::IsTrue(allowed.count(name) == 1,
                    std::wstring(L"decompiled script no longer recompiles: ").append(name.begin(), name.end()).c_str());
            }
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
}
