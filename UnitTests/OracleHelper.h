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
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The bytecode round-trip oracle.
//
// You cannot compare re-emitted bytecode to a Sierra original byte-for-byte:
// SCI Companion emits sections in a fixed order with its own padding, folds
// Sierra's hand-optimised idioms to a canonical form, and regenerates
// relocation and species tables. So the oracle asserts a weaker but real
// invariant: IDEMPOTENCE. Decompile a script and recompile it (pass A), then
// decompile that and recompile again (pass B). Pass A and pass B must emit the
// same bytes. If the decompiler faithfully inverts the compiler, the round trip
// reaches a fixpoint after one pass; any drift is a real defect.

// Outcome of RunIdempotenceOracle.
struct OracleResult
{
    int processed = 0;                            // scripts that loaded on the first pass
    int settledAfterFirst = 0;                    // scripts still changing after pass 0->1 (informational)
    std::vector<std::string> compileFailures;     // "<pass> <title>: <error>"
    std::vector<std::string> idempotenceFailures; // "<n>: scr ..." / "<n>: hep ..." (last two passes)
};

// Runs the idempotence oracle over the CURRENT game (set it up first). Both
// passes recompile the whole game as one consistent world, like
// RecompileAllDecompiledScripts.
OracleResult RunIdempotenceOracle();

// True when the current game's package format is one the compiler can emit
// (SCI0..SCI1.1). SCI2+ games are load/parse only, not round-trippable.
bool IsFullRoundTripEligible();

// Outcome of CompareTemplateBytecodeSnapshots.
struct BytecodeSnapshotResult
{
    int processed = 0;
    std::vector<std::string> mismatched;      // "<title>.scr"/"<title>.hep" whose bytes changed
    std::vector<std::string> missingExpected; // snapshots with no committed golden
    // Scripts that did not recompile in the RecompileAllDecompiledScripts pass.
    // Such a script keeps its ORIGINAL bytecode on disk, so comparing it against
    // the golden is a comparison of the original with itself and always
    // matches; the test must fail on this list instead (#79).
    std::vector<std::string> failedRecompile;
};

// Recompiles every template script as one consistent world, then compares each
// script's EMITTED bytecode (.scr, and .hep when the version has a separate
// heap) against the committed golden hex under
// Files\Decompile\Snapshots\Bytecode\SCI1.1. The existing text snapshot pins
// decompiled text only; a change that alters emitted bytes but not text (operand
// width, padding, section layout) is invisible to it and caught here.
BytecodeSnapshotResult CompareTemplateBytecodeSnapshots();

// Canonical hex dump (lowercase, 32 bytes per line) for golden comparison.
std::string ToHexDump(const std::vector<uint8_t> &bytes);
