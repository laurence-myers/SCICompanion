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
#include "OracleHelper.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "GameFolderHelper.h"
#include "CompiledScript.h"
#include "format.h"
#include <cstdlib>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    static std::wstring W(const std::string &s) { return std::wstring(s.begin(), s.end()); }

    static std::wstring JoinLines(const std::vector<std::string> &lines, const std::string &header)
    {
        std::string msg = header;
        for (const std::string &l : lines)
        {
            msg += "\n  " + l;
        }
        return W(msg);
    }

    TEST_CLASS(TestBytecodeOracle)
    {
    public:
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

        // Golden emitted-bytecode snapshot for the template. The existing text
        // snapshot pins decompiled text; this pins the bytes the compiler emits,
        // catching changes (operand width, padding, section layout) that leave
        // the text identical. Refresh intended changes with RunTests.ps1
        // -UpdateSnapshots.
        TEST_METHOD(TemplateGame_BytecodeSnapshot)
        {
            _gameFolder = SetUpGameSCI11();
            BytecodeSnapshotResult r = CompareTemplateBytecodeSnapshots();
            Assert::IsTrue(r.processed >= 80,
                W(fmt::format("expected at least 80 scripts, processed {0}", r.processed)).c_str());
            Assert::IsTrue(r.missingExpected.empty(),
                JoinLines(r.missingExpected, "no committed bytecode golden (run RunTests.ps1 -UpdateSnapshots):").c_str());
            Assert::IsTrue(r.mismatched.empty(),
                JoinLines(r.mismatched, "emitted bytecode changed (review, then RunTests.ps1 -UpdateSnapshots):").c_str());
        }

        // The template's own output survives decompile -> recompile -> decompile
        // -> recompile with byte-identical bytecode (a fixpoint). This is the
        // strongest achievable form of the round-trip guarantee.
        TEST_METHOD(TemplateGame_BytecodeIdempotence)
        {
            _gameFolder = SetUpGameSCI11();
            OracleResult r = RunIdempotenceOracle();
            Logger::WriteMessage(W(fmt::format(
                "idempotence: processed={0} settledAfterFirst={1} compileFailures={2} idempotenceFailures={3}",
                r.processed, r.settledAfterFirst, r.compileFailures.size(), r.idempotenceFailures.size())).c_str());
            Assert::IsTrue(r.processed > 0, L"no scripts were recompiled");
            Assert::IsTrue(r.compileFailures.empty(),
                JoinLines(r.compileFailures, "decompiled template did not recompile:").c_str());
            Assert::IsTrue(r.idempotenceFailures.empty(),
                JoinLines(r.idempotenceFailures, "round trip is not a fixpoint (emitted bytecode drifted):").c_str());
        }

        // Opt-in oracle for a real Sierra game. Set SCICOMP_ORACLE_GAME to the
        // folder that holds resource.map (not a variant parent). SKIPPED (and so
        // green) without it, so it adds no CI coverage; it is a developer tool.
        // The game is COPIED to a temp folder first, so the original on disk is
        // never modified. SCI2+ games are load/parse only.
        TEST_METHOD(Oracle_ExistingGame)
        {
            const char *game = getenv("SCICOMP_ORACLE_GAME");
            if (!game)
            {
                Logger::WriteMessage(L"Skipped: set SCICOMP_ORACLE_GAME to a game's resource.map folder.");
                return;
            }
            _gameFolder = SetUpExistingGameCopy(game);

            if (!IsFullRoundTripEligible())
            {
                CResourceMap &rm = appState->GetResourceMap();
                const GameFolderHelper &helper = rm.Helper();
                std::vector<ScriptId> scripts;
                rm.GetAllScripts(scripts);
                int loaded = 0;
                for (ScriptId &scriptId : scripts)
                {
                    CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
                    if (compiled.Load(helper, helper.Version, scriptId.GetResourceNumber()))
                    {
                        loaded++;
                    }
                }
                Logger::WriteMessage(W(fmt::format("load-only (SCI2+): {0}/{1} scripts loaded",
                    loaded, scripts.size())).c_str());
                Assert::IsTrue(loaded > 0, L"no scripts loaded");
                return;
            }

            OracleResult r = RunIdempotenceOracle();
            Logger::WriteMessage(W(fmt::format(
                "oracle: processed={0} compileFailures={1} idempotenceFailures={2}",
                r.processed, r.compileFailures.size(), r.idempotenceFailures.size())).c_str());
            Assert::IsTrue(r.processed > 0, L"no scripts were recompiled");
            Assert::IsTrue(r.compileFailures.empty(),
                JoinLines(r.compileFailures, "decompiled game did not recompile:").c_str());
            Assert::IsTrue(r.idempotenceFailures.empty(),
                JoinLines(r.idempotenceFailures, "round trip is not a fixpoint (emitted bytecode drifted):").c_str());
        }

    private:
        std::string _gameFolder;
    };
}
