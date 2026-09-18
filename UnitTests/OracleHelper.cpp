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
#include "OracleHelper.h"
#include "Helper.h"
#include "DecompileHelper.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "ResourceContainer.h"
#include "ResourceBlob.h"
#include "GameFolderHelper.h"
#include "CompiledScript.h"
#include "format.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

// Local file helpers (DecompileHelper.cpp has its own file-static copies; these
// mirror them so this unit does not depend on that unit's internals).
static void WriteTextFileLocal(const std::string &path, const std::string &text)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    file << text;
}

static bool ReadTextFileLocal(const std::string &path, std::string &out)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

static void MakeDirsLocal(const std::string &path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
}

bool IsFullRoundTripEligible()
{
    return appState->GetVersion().PackageFormat <= ResourcePackageFormat::SCI11;
}

std::string ToHexDump(const std::vector<uint8_t> &bytes)
{
    std::string out;
    out.reserve(bytes.size() * 3);
    for (size_t i = 0; i < bytes.size(); i++)
    {
        out += fmt::format("{:02x}", bytes[i]);
        out += ((i % 32) == 31) ? '\n' : ' ';
    }
    if (!out.empty() && out.back() == ' ')
    {
        out.back() = '\n';
    }
    return out;
}

// The emitted bytecode of one script: the script resource and, for versions
// with a separate heap, the heap resource. Read from the decompressed payload,
// never the on-disk slice, so compression never affects the comparison.
struct ScriptBytes
{
    std::vector<uint8_t> scr;
    std::vector<uint8_t> hep;
};

static std::vector<uint8_t> ReadResourceBytes(const std::unique_ptr<ResourceBlob> &blob)
{
    std::vector<uint8_t> out;
    if (blob)
    {
        sci::istream stream = blob->GetReadStream();
        out.resize(stream.GetDataSize());
        if (!out.empty())
        {
            stream.read_data(&out[0], (int)out.size());
        }
    }
    return out;
}

static bool LoadScriptBytes(const GameFolderHelper &helper, bool separateHeap, uint16_t number, ScriptBytes &out)
{
    CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
    if (!compiled.Load(helper, helper.Version, number))
    {
        return false;
    }
    out.scr = compiled.GetRawBytes();
    if (separateHeap)
    {
        out.hep = ReadResourceBytes(helper.MostRecentResource(ResourceType::Heap, number, ResourceEnumFlags::None));
    }
    return true;
}

static std::map<uint16_t, ScriptBytes> CaptureAllScriptBytes()
{
    CResourceMap &rm = appState->GetResourceMap();
    const GameFolderHelper &helper = rm.Helper();
    bool separateHeap = appState->GetVersion().SeparateHeapResources;

    std::vector<ScriptId> scripts;
    rm.GetAllScripts(scripts);

    std::map<uint16_t, ScriptBytes> out;
    for (ScriptId &scriptId : scripts)
    {
        uint16_t number = scriptId.GetResourceNumber();
        ScriptBytes bytes;
        if (LoadScriptBytes(helper, separateHeap, number, bytes))
        {
            out[number] = std::move(bytes);
        }
    }
    return out;
}

OracleResult RunIdempotenceOracle()
{
    // Decompile and recompile the whole game repeatedly. The FIRST pass derives
    // from the game's original (Sierra) bytecode; later passes derive from our
    // own emitted output. Because our compiler folds Sierra's idioms to a
    // canonical form, the first re-emission can still differ slightly from the
    // original's encoding and settle on the next pass. The invariant we assert
    // is CONVERGENCE: once we are recompiling our own output, the bytes stop
    // changing. So we run three passes and require the last two to be identical
    // (a fixpoint). The pass-0-to-1 delta is only settling and is reported, not
    // asserted.
    const int kPasses = 3;
    OracleResult result;
    std::vector<std::map<uint16_t, ScriptBytes>> snapshots;
    for (int pass = 0; pass < kPasses; pass++)
    {
        std::vector<std::string> failures;
        int processed = 0;
        RecompileAllDecompiledScripts(&failures, &processed);
        if (pass == 0)
        {
            result.processed = processed;
        }
        char label = static_cast<char>('A' + pass);
        for (const std::string &f : failures)
        {
            result.compileFailures.push_back(std::string(1, label) + " " + f);
        }
        snapshots.push_back(CaptureAllScriptBytes());
    }

    // Report the settling delta between the first two passes (informational).
    result.settledAfterFirst = 0;
    for (const auto &entry : snapshots[0])
    {
        auto it = snapshots[1].find(entry.first);
        if (it == snapshots[1].end() || entry.second.scr != it->second.scr || entry.second.hep != it->second.hep)
        {
            result.settledAfterFirst++;
        }
    }

    // Assert the last two passes are a fixpoint.
    const std::map<uint16_t, ScriptBytes> &prev = snapshots[kPasses - 2];
    const std::map<uint16_t, ScriptBytes> &last = snapshots[kPasses - 1];
    for (const auto &entry : prev)
    {
        auto it = last.find(entry.first);
        if (it == last.end())
        {
            result.idempotenceFailures.push_back(fmt::format("{0}: vanished on the final pass", entry.first));
            continue;
        }
        if (entry.second.scr != it->second.scr)
        {
            result.idempotenceFailures.push_back(
                fmt::format("{0}: scr differs ({1} vs {2} bytes)", entry.first, entry.second.scr.size(), it->second.scr.size()));
        }
        if (entry.second.hep != it->second.hep)
        {
            result.idempotenceFailures.push_back(
                fmt::format("{0}: hep differs ({1} vs {2} bytes)", entry.first, entry.second.hep.size(), it->second.hep.size()));
        }
    }
    return result;
}

// Remove carriage returns so the comparison is line-ending agnostic. The
// committed goldens are stored as LF but git (core.autocrlf) checks them out as
// CRLF on Windows, while the actuals are written as LF; without this every file
// would appear to differ. (The text snapshot test dodges this via Normalize.)
static std::string StripCR(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c != '\r')
        {
            out.push_back(c);
        }
    }
    return out;
}

static void CompareOneSnapshot(const std::string &name, const std::string &actual,
    const std::string &expectedDir, const std::string &actualDir, BytecodeSnapshotResult &result)
{
    WriteTextFileLocal(actualDir + "\\" + name, actual);
    std::string expected;
    if (!ReadTextFileLocal(expectedDir + "\\" + name, expected))
    {
        result.missingExpected.push_back(name);
    }
    else if (StripCR(expected) != StripCR(actual))
    {
        result.mismatched.push_back(name);
    }
}

BytecodeSnapshotResult CompareTemplateBytecodeSnapshots()
{
    // Recompile the whole template as one consistent world (Sierra bytecode ->
    // our bytecode), so the goldens are our compiler's own faithful output.
    std::vector<std::string> failed;
    RecompileAllDecompiledScripts(&failed, nullptr);

    std::string expectedDir = GetTestFileDirectory("Decompile\\Snapshots\\Bytecode\\SCI1.1");
    std::string actualDir = GetTestModuleDirectory() + "\\SnapshotActuals\\Bytecode\\SCI1.1";
    MakeDirsLocal(actualDir);

    CResourceMap &rm = appState->GetResourceMap();
    const GameFolderHelper &helper = rm.Helper();
    bool separateHeap = appState->GetVersion().SeparateHeapResources;

    std::vector<ScriptId> scripts;
    rm.GetAllScripts(scripts);

    BytecodeSnapshotResult result;
    // A script that did not recompile still has its original bytecode on
    // disk, so the comparison below would pass vacuously for it (#79).
    result.failedRecompile = failed;
    for (ScriptId &scriptId : scripts)
    {
        uint16_t number = scriptId.GetResourceNumber();
        ScriptBytes bytes;
        if (!LoadScriptBytes(helper, separateHeap, number, bytes))
        {
            continue;
        }
        result.processed++;
        std::string title = scriptId.GetTitle();
        CompareOneSnapshot(title + ".scr.hex", ToHexDump(bytes.scr), expectedDir, actualDir, result);
        if (separateHeap)
        {
            CompareOneSnapshot(title + ".hep.hex", ToHexDump(bytes.hep), expectedDir, actualDir, result);
        }
    }
    return result;
}
