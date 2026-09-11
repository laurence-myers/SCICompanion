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
// Families 2, 3, 4 and 8 are not covered here. A minimal asm fixture does not
// reproduce them. They need the enclosing structure of the larger original
// functions. Author those fixtures with their fix (test-first).

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

        // Family 1: a conditional branch to the loop head.
        TEST_METHOD(Family1_LoopHeadContinue)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F1_LoopHeadContinue", 900);
            LogWarnings("F1", out);
            // PART B: flip to Assert::AreEqual(0, out.fallbacks) once fixed.
            Assert::IsTrue(out.fallbacks >= 1, L"expected a fallback");
            Assert::IsTrue(out.HasWarningContaining("Inconsistent then/else branches"),
                L"expected the Family 1 control-flow warning");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
        }

        // Family 5: an empty leading while swallows the next loop.
        TEST_METHOD(Family5_EmptyLeadingWhile)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F5_EmptyLeadingWhile", 905);
            LogWarnings("F5", out);
            // PART B: flip once child collection is bounded.
            Assert::IsTrue(out.fallbacks >= 1, L"expected a fallback");
            Assert::IsTrue(out.HasWarningContaining("Unable to replace node in follow nodes"),
                L"expected the Family 5 control-flow warning");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
        }

        // Family 6: an empty trailing for leaves a pruned dead back-jump.
        TEST_METHOD(Family6_EmptyTrailingFor)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F6_EmptyTrailingFor", 906);
            LogWarnings("F6", out);
            // PART B: flip once the branch fixup handles the folded for exit.
            Assert::IsTrue(out.fallbacks >= 1, L"expected a fallback");
            Assert::IsTrue(out.HasWarningContaining("Can't find follow node"),
                L"expected the Family 6 control-flow warning");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
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
        // must not grow. Lower BASELINE deliberately when a fix helps.
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

            const int BASELINE = 7;   // ScrollableInventory, SaveRestoreDialog, Controls, Gauge, System; lower when a fix helps
            Assert::IsTrue(fallbacks <= BASELINE, L"template fallbacks grew beyond baseline");
        }

    private:
        std::string _gameFolder;
    };
}
