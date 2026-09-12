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

#include <string>
#include <vector>
#include "DecompilerResults.h"

// Result of a decompile: the source text, the warning/error messages, and the
// count of functions that fell back to assembly.
struct DecompileOutput
{
    std::string text;
    std::vector<std::string> warnings;
    int fallbacks = 0;

    bool HasWarningContaining(const std::string &needle) const;
    bool ContainsAsm() const;
};

// Collects decompiler diagnostics for a test to assert on.
class TestDecompilerResults : public IDecompilerResults
{
public:
    void AddResult(DecompilerResultType type, const std::string &message) override;
    bool IsAborted() override { return false; }
    void InformStats(bool functionSuccessful, int byteCount) override;
    void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &) override {}

    std::vector<std::string> warnings;
    int fallbacks = 0;
};

// Copies a fixture "<name>.sc" from TestFiles\Decompile\SCI1.1 into the game
// src folder. The game must be set up first (SetUpGameSCI11).
void AddFixtureScript(const std::string &fixtureName);

// Compiles the script "<name>" (already in the game src folder) as resource
// number scriptNumber. Returns true if the compile reports no errors. On
// failure, writes the first error message to outError when it is not null.
bool CompileFixture(uint16_t scriptNumber, const std::string &fixtureName, std::string *outError = nullptr);

// Decompiles the compiled script resource to source text plus diagnostics.
DecompileOutput DecompileToText(uint16_t scriptNumber, bool debugChunks = false);

// Compiles the fixture, decompiles it, recompiles the decompiled text, and
// decompiles again. Asserts the second decompile matches the first, so the
// script survives a decompile, recompile, decompile round trip. This proves
// the round trip is stable. It does not prove the source is faithful to the
// original. Returns the first decompile.
DecompileOutput DecompileAndRoundTrip(const std::string &fixtureName, uint16_t scriptNumber);

// Decompiles every script in the game. Returns the total number of functions
// that fell back to assembly. Appends the names of scripts with any fallback
// to outFailedScripts when it is not null. Writes the count of scripts that
// loaded to outProcessed when it is not null. Loads lookups once.
int CountFallbacksAllScripts(std::vector<std::string> *outFailedScripts = nullptr,
    int *outProcessed = nullptr);

// Compiles a fixture, decompiles it, and compares the decompiled text with the
// expected file "<name>.expected.sc" in TestFiles\Decompile\SCI1.1. Asserts no
// fallback, no asm, an exact match after whitespace normalization, and a stable
// round trip. On mismatch it writes the actual text to TestResults so a diff is
// easy. This tests fidelity, not just round-trip stability.
void AssertDecompileMatchesExpected(const std::string &fixtureName, uint16_t scriptNumber);

// Result of the template snapshot comparison.
struct SnapshotResult
{
    int processed = 0;                        // scripts that decompiled
    std::vector<std::string> mismatched;      // titles whose text changed
    std::vector<std::string> missingExpected; // titles with no committed snapshot
};

// Decompiles every template script, writes each one to
// TestResults\Snapshots\SCI1.1\<title>.sc, and compares it with the committed
// snapshot in TestFiles\Decompile\Snapshots\SCI1.1\<title>.sc. Records
// mismatches and missing snapshots. Loads lookups once.
SnapshotResult CompareTemplateSnapshots();

// Decompiles every template script and recompiles the text as one consistent
// world (all sources written, then all recompiled). Appends the title of every
// script whose decompiled text does not recompile to outFailed. This guards
// that the decompiler emits code the compiler accepts. It does not check text
// stability; the snapshot test pins output text. Returns the count processed.
int RecompileAllDecompiledScripts(std::vector<std::string> *outFailed, int *outProcessed = nullptr);
