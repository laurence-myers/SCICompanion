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
#include "ResourceMap.h"
#include "CompiledScript.h"
#include "CompileContext.h"
#include "ScriptOMAll.h"
#include "DecompilerCore.h"
#include "DecompilerConfig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// CppUnitTest messages are wide strings.
static std::wstring ToWString(const std::string &text)
{
    return std::wstring(text.begin(), text.end());
}

bool DecompileOutput::HasWarningContaining(const std::string &needle) const
{
    for (const std::string &w : warnings)
    {
        if (w.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

bool DecompileOutput::ContainsAsm() const
{
    // A function can also fail without an asm fallback. If the decompiler
    // cannot find the code bounds, it emits a CorruptFunction token instead.
    return (text.find("(asm") != std::string::npos) ||
        (text.find("CorruptFunction") != std::string::npos);
}

void TestDecompilerResults::AddResult(DecompilerResultType type, const std::string &message)
{
    if ((type == DecompilerResultType::Warning) || (type == DecompilerResultType::Error))
    {
        warnings.push_back(message);
    }
}

void TestDecompilerResults::InformStats(bool functionSuccessful, int)
{
    if (!functionSuccessful)
    {
        fallbacks++;
    }
}

static void WriteTextFile(const std::string &path, const std::string &text)
{
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    file << text;
}

static bool ReadTextFile(const std::string &path, std::string &out)
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

// Creates every directory in a path, ignoring ones that already exist.
static void MakeDirs(const std::string &path)
{
    std::string partial;
    for (size_t i = 0; i < path.size(); i++)
    {
        char c = path[i];
        partial.push_back(c);
        if ((c == '\\' || c == '/') && partial.size() > 3)
        {
            CreateDirectory(partial.c_str(), nullptr);
        }
    }
    CreateDirectory(path.c_str(), nullptr);
}

// Normalise line endings so a round-trip comparison ignores CRLF vs LF.
static std::string Normalize(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
    {
        if (c != '\r')
        {
            out.push_back(c);
        }
    }
    return out;
}

void AddFixtureScript(const std::string &fixtureName)
{
    std::string src = GetTestFileDirectory("Decompile\\SCI1.1") + "\\" + fixtureName + ".sc";
    std::string dst = appState->GetResourceMap().Helper().GetScriptFileName(fixtureName);
    Assert::IsTrue(CopyFile(src.c_str(), dst.c_str(), FALSE) != 0,
        ToWString("Could not copy fixture: " + src).c_str());
}

bool CompileFixture(uint16_t scriptNumber, const std::string &fixtureName, std::string *outError)
{
    CResourceMap &rm = appState->GetResourceMap();
    rm.AssignName(ResourceType::Script, scriptNumber, NoBase36, fixtureName.c_str());

    ScriptId scriptId(rm.Helper().GetScriptFileName(fixtureName).c_str());
    scriptId.SetResourceNumber(scriptNumber);

    DeferResourceAppend defer(rm);
    CompileLog log;
    CompileTables tables;
    tables.Load(appState->GetVersion());
    PrecompiledHeaders headers(rm);
    CompileResults results(log);
    bool ok = NewCompileScript(results, log, tables, headers, scriptId);
    if (ok)
    {
        tables.Save();
    }
    // Commit persists the compiled resource. A failure here must fail the
    // compile, or a later decompile reads a stale resource.
    HRESULT hr = defer.Commit();
    bool success = ok && !log.HasErrors() && SUCCEEDED(hr);
    if (!success && outError)
    {
        for (const CompileResult &r : log.Results())
        {
            if (r.IsError())
            {
                *outError = r.GetMessage();
                break;
            }
        }
    }
    return success;
}

DecompileOutput DecompileToText(uint16_t scriptNumber, bool debugChunks)
{
    CResourceMap &rm = appState->GetResourceMap();
    const GameFolderHelper &helper = rm.Helper();

    GlobalCompiledScriptLookups lookups;
    lookups.Load(helper);
    uint16_t dummy;
    lookups.GetSelectorTable().ReverseLookup("", dummy);

    std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

    DecompileOutput out;
    CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
    if (!compiled.Load(helper, helper.Version, scriptNumber))
    {
        out.text = "<<compiled script load failed>>";
        return out;
    }

    TestDecompilerResults results;
    std::unique_ptr<sci::Script> pScript = DecompileScript(
        config.get(), lookups, helper, scriptNumber, compiled, results,
        false, debugChunks, nullptr, false, false);

    std::stringstream ss;
    sci::SourceCodeWriter writer(ss, helper.GetDefaultGameLanguage(), pScript.get());
    pScript->OutputSourceCode(writer);

    out.text = ss.str();
    out.warnings = results.warnings;
    out.fallbacks = results.fallbacks;
    return out;
}

DecompileOutput DecompileAndRoundTrip(const std::string &fixtureName, uint16_t scriptNumber)
{
    AddFixtureScript(fixtureName);
    Assert::IsTrue(CompileFixture(scriptNumber, fixtureName),
        ToWString("Initial compile failed: " + fixtureName).c_str());

    DecompileOutput first = DecompileToText(scriptNumber);

    std::string path = appState->GetResourceMap().Helper().GetScriptFileName(fixtureName);
    WriteTextFile(path, first.text);
    Assert::IsTrue(CompileFixture(scriptNumber, fixtureName),
        ToWString("Recompile of decompiled text failed: " + fixtureName).c_str());

    DecompileOutput second = DecompileToText(scriptNumber);
    Assert::AreEqual(Normalize(first.text), Normalize(second.text),
        ToWString("Round trip not stable: " + fixtureName).c_str());

    return first;
}

int CountFallbacksAllScripts(std::vector<std::string> *outFailedScripts, int *outProcessed)
{
    CResourceMap &rm = appState->GetResourceMap();
    const GameFolderHelper &helper = rm.Helper();

    GlobalCompiledScriptLookups lookups;
    lookups.Load(helper);
    uint16_t dummy;
    lookups.GetSelectorTable().ReverseLookup("", dummy);
    std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

    std::vector<ScriptId> scripts;
    rm.GetAllScripts(scripts);

    int totalFallbacks = 0;
    int processed = 0;
    for (ScriptId &scriptId : scripts)
    {
        uint16_t number = scriptId.GetResourceNumber();
        CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
        if (!compiled.Load(helper, helper.Version, number))
        {
            continue;
        }
        processed++;
        TestDecompilerResults results;
        std::unique_ptr<sci::Script> pScript = DecompileScript(
            config.get(), lookups, helper, number, compiled, results,
            false, false, nullptr, false, false);
        std::stringstream ss;
        sci::SourceCodeWriter writer(ss, helper.GetDefaultGameLanguage(), pScript.get());
        pScript->OutputSourceCode(writer);
        // A CorruptFunction token is a failure that does not raise the fallback
        // stat, so count it too.
        bool corrupt = ss.str().find("CorruptFunction") != std::string::npos;
        if ((results.fallbacks > 0) || corrupt)
        {
            totalFallbacks += (results.fallbacks > 0) ? results.fallbacks : 1;
            if (outFailedScripts)
            {
                outFailedScripts->push_back(scriptId.GetTitle());
            }
        }
    }
    if (outProcessed)
    {
        *outProcessed = processed;
    }
    return totalFallbacks;
}

// Collapses runs of whitespace to a single space and trims, so a comparison
// ignores indentation and line breaks.
static std::string NormalizeWs(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    bool inSpace = false;
    for (char c : text)
    {
        if (c == '\r')
        {
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n')
        {
            inSpace = true;
            continue;
        }
        if (inSpace && !out.empty())
        {
            out.push_back(' ');
        }
        inSpace = false;
        out.push_back(c);
    }
    return out;
}

void AssertDecompileMatchesExpected(const std::string &fixtureName, uint16_t scriptNumber)
{
    DecompileOutput first = DecompileAndRoundTrip(fixtureName, scriptNumber);
    Assert::AreEqual(0, first.fallbacks,
        ToWString(fixtureName + ": expected no fallback").c_str());
    Assert::IsFalse(first.ContainsAsm(),
        ToWString(fixtureName + ": expected no asm").c_str());

    std::string expectedPath = GetTestFileDirectory("Decompile\\SCI1.1") + "\\" + fixtureName + ".expected.sc";
    std::string expected;
    if (!ReadTextFile(expectedPath, expected))
    {
        // Bootstrap aid: no expected file yet. Write the actual so a developer
        // can review it and commit it as the oracle.
        std::string outDir = GetTestModuleDirectory() + "\\SnapshotActuals\\Expected";
        MakeDirs(outDir);
        WriteTextFile(outDir + "\\" + fixtureName + ".expected.sc", first.text);
        Assert::Fail(ToWString(fixtureName + ": no expected file. Wrote actual to " + outDir).c_str());
    }

    if (NormalizeWs(expected) != NormalizeWs(first.text))
    {
        std::string outDir = GetTestModuleDirectory() + "\\SnapshotActuals\\Expected";
        MakeDirs(outDir);
        WriteTextFile(outDir + "\\" + fixtureName + ".actual.sc", first.text);
        Assert::Fail(ToWString(fixtureName + ": decompiled text does not match expected. Actual in " + outDir).c_str());
    }
}

SnapshotResult CompareTemplateSnapshots()
{
    CResourceMap &rm = appState->GetResourceMap();
    const GameFolderHelper &helper = rm.Helper();

    GlobalCompiledScriptLookups lookups;
    lookups.Load(helper);
    uint16_t dummy;
    lookups.GetSelectorTable().ReverseLookup("", dummy);
    std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

    std::string expectedDir = GetTestFileDirectory("Decompile\\Snapshots\\SCI1.1");
    std::string actualDir = GetTestModuleDirectory() + "\\SnapshotActuals\\SCI1.1";
    MakeDirs(actualDir);

    std::vector<ScriptId> scripts;
    rm.GetAllScripts(scripts);

    SnapshotResult result;
    for (ScriptId &scriptId : scripts)
    {
        uint16_t number = scriptId.GetResourceNumber();
        CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
        if (!compiled.Load(helper, helper.Version, number))
        {
            continue;
        }
        result.processed++;

        TestDecompilerResults results;
        std::unique_ptr<sci::Script> pScript = DecompileScript(
            config.get(), lookups, helper, number, compiled, results,
            false, false, nullptr, false, false);
        std::stringstream ss;
        sci::SourceCodeWriter writer(ss, helper.GetDefaultGameLanguage(), pScript.get());
        pScript->OutputSourceCode(writer);
        std::string actual = Normalize(ss.str());

        std::string title = scriptId.GetTitle();
        WriteTextFile(actualDir + "\\" + title + ".sc", actual);

        std::string expected;
        if (!ReadTextFile(expectedDir + "\\" + title + ".sc", expected))
        {
            result.missingExpected.push_back(title);
        }
        else if (Normalize(expected) != actual)
        {
            result.mismatched.push_back(title);
        }
    }
    return result;
}

int RecompileAllDecompiledScripts(std::vector<std::string> *outFailed, int *outProcessed)
{
    CResourceMap &rm = appState->GetResourceMap();
    const GameFolderHelper &helper = rm.Helper();

    std::vector<ScriptId> scripts;
    rm.GetAllScripts(scripts);

    // Recompile the whole game as one consistent world: decompile every script
    // from the pristine game, write every source, then compile every script so
    // each one compiles against the full set of decompiled sources.
    struct Entry { uint16_t number; std::string title; std::string text; };
    std::vector<Entry> entries;

    int processed = 0;
    for (ScriptId &scriptId : scripts)
    {
        uint16_t number = scriptId.GetResourceNumber();
        CompiledScript probe(0, CompiledScriptFlags::RemoveBadExports);
        if (!probe.Load(helper, helper.Version, number))
        {
            continue;
        }
        processed++;
        DecompileOutput out = DecompileToText(number);
        entries.push_back({ number, scriptId.GetTitle(), out.text });
    }

    for (const Entry &e : entries)
    {
        WriteTextFile(helper.GetScriptFileName(e.title), e.text);
    }
    for (const Entry &e : entries)
    {
        std::string error;
        if (!CompileFixture(e.number, e.title, &error))
        {
            if (outFailed)
            {
                outFailed->push_back(e.title);
            }
        }
    }

    if (outProcessed)
    {
        *outProcessed = processed;
    }
    return processed;
}
