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
#include "ResourceContainer.h"
#include "format.h"
#include <filesystem>
#include <map>

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
        // The first error. When no result is an error, the status and every
        // message, so the failure is not silent.
        for (const CompileResult &r : log.Results())
        {
            if (r.IsError())
            {
                *outError = r.GetMessage();
                break;
            }
        }
        if (outError->empty())
        {
            *outError = fmt::format("(compiled={0} commit={1:#x})", ok, (unsigned)hr);
            for (const CompileResult &r : log.Results())
            {
                *outError += "\n  " + r.GetMessage();
            }
        }
    }
    return success;
}

DecompileOutput DecompileToText(uint16_t scriptNumber, bool debugChunks, bool debugControlFlow)
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
        debugControlFlow, debugChunks, nullptr, false, false);

    std::stringstream ss;
    sci::SourceCodeWriter writer(ss, helper.GetDefaultGameLanguage(), pScript.get());
    pScript->OutputSourceCode(writer);

    out.text = ss.str();
    out.warnings = results.warnings;
    out.fallbacks = results.fallbacks;
    return out;
}

bool DecompileTemplateScriptByTitle(const std::string &title, DecompileOutput &out)
{
    std::vector<ScriptId> scripts;
    appState->GetResourceMap().GetAllScripts(scripts);
    for (ScriptId &scriptId : scripts)
    {
        if (scriptId.GetTitle() == title)
        {
            bool debugChunks = (getenv("SCICOMP_DEBUG_CHUNKS") != nullptr);
            out = DecompileToText(scriptId.GetResourceNumber(), debugChunks, true);
            return true;
        }
    }
    return false;
}

DecompileOutput DecompileAndRoundTrip(const std::string &fixtureName, uint16_t scriptNumber)
{
    AddFixtureScript(fixtureName);
    // Compile first: the message argument is built before the call otherwise.
    std::string compileError;
    bool compiled = CompileFixture(scriptNumber, fixtureName, &compileError);
    Assert::IsTrue(compiled, ToWString("Initial compile failed: " + fixtureName + ": " + compileError).c_str());

    // The control-flow dump is added to the warnings only when analysis fails.
    // Set SCICOMP_DEBUG_CHUNKS to also get the chunk-tree dump.
    bool debugChunks = (getenv("SCICOMP_DEBUG_CHUNKS") != nullptr);
    DecompileOutput first = DecompileToText(scriptNumber, debugChunks, true);

    std::string path = appState->GetResourceMap().Helper().GetScriptFileName(fixtureName);
    WriteTextFile(path, first.text);
    compileError.clear();
    if (!CompileFixture(scriptNumber, fixtureName, &compileError))
    {
        Logger::WriteMessage(ToWString("Decompiled text:\n" + first.text).c_str());
        Assert::Fail(ToWString("Recompile of decompiled text failed: " + fixtureName + ": " + compileError).c_str());
    }

    DecompileOutput second = DecompileToText(scriptNumber);
    Assert::AreEqual(Normalize(first.text), Normalize(second.text),
        ToWString("Round trip not stable: " + fixtureName).c_str());

    return first;
}

int CountFallbacksAllScripts(std::vector<std::string> *outFailedScripts, int *outProcessed, std::vector<std::string> *outWarnings)
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
            if (outWarnings)
            {
                for (const std::string &w : results.warnings)
                {
                    outWarnings->push_back(scriptId.GetTitle() + ": " + w);
                }
            }
        }
    }
    if (outProcessed)
    {
        *outProcessed = processed;
    }
    return totalFallbacks;
}

// Maps script numbers to file names by reading each "(script# N)" header in
// the folder. A number with no file is not in the map.
static std::map<int, std::string> ReadScriptNameMap(const std::string &dir)
{
    std::map<int, std::string> names;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || (entry.path().extension() != ".sc"))
        {
            continue;
        }
        std::string text;
        if (!ReadTextFile(entry.path().string(), text))
        {
            continue;
        }
        size_t pos = text.find("(script# ");
        if (pos != std::string::npos)
        {
            int number = atoi(text.c_str() + pos + 9);
            names[number] = entry.path().filename().string();
        }
    }
    return names;
}

int DumpAllScripts(const std::string &outDir, const std::string &nameMapDir,
    std::vector<std::string> *outWarnings, int *outProcessed)
{
    CResourceMap &rm = appState->GetResourceMap();
    const GameFolderHelper &helper = rm.Helper();

    GlobalCompiledScriptLookups lookups;
    lookups.Load(helper);
    uint16_t dummy;
    lookups.GetSelectorTable().ReverseLookup("", dummy);
    std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(helper, lookups.GetSelectorTable());

    std::map<int, std::string> names;
    if (!nameMapDir.empty())
    {
        names = ReadScriptNameMap(nameMapDir);
    }
    MakeDirs(outDir);

    int totalFallbacks = 0;
    int processed = 0;
    auto scriptResources = rm.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly);
    for (auto &blob : *scriptResources)
    {
        int number = blob->GetNumber();
        CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
        if (!compiled.Load(helper, helper.Version, static_cast<uint16_t>(number)))
        {
            continue;
        }
        processed++;
        TestDecompilerResults results;
        std::unique_ptr<sci::Script> pScript = DecompileScript(
            config.get(), lookups, helper, static_cast<uint16_t>(number), compiled, results,
            false, false, nullptr, false, false);
        std::stringstream ss;
        sci::SourceCodeWriter writer(ss, helper.GetDefaultGameLanguage(), pScript.get());
        pScript->OutputSourceCode(writer);

        auto it = names.find(number);
        std::string name = (it != names.end()) ? it->second : fmt::format("{0}.sc", number);
        WriteTextFile(outDir + "\\" + name, ss.str());

        totalFallbacks += results.fallbacks;
        if (outWarnings)
        {
            for (const std::string &w : results.warnings)
            {
                outWarnings->push_back(name + ": " + w);
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
    if (first.fallbacks != 0)
    {
        std::string msg = fixtureName + " warnings:";
        for (const std::string &w : first.warnings)
        {
            msg += "\n" + w;
        }
        Logger::WriteMessage(ToWString(msg).c_str());
    }
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
                outFailed->push_back(error.empty() ? e.title : (e.title + ": " + error));
            }
        }
    }

    if (outProcessed)
    {
        *outProcessed = processed;
    }
    return processed;
}

std::string DumpSelectorTable()
{
    GlobalCompiledScriptLookups lookups;
    lookups.Load(appState->GetResourceMap().Helper());
    const std::vector<std::string> &names = lookups.GetSelectorTable().GetNames();
    std::string out = fmt::format("names: {0}\n", names.size());
    for (uint16_t i = 0; i < 4096; i++)
    {
        std::string name = lookups.GetSelectorTable().Lookup(i);
        if (!name.empty())
        {
            out += fmt::format("{0} {1}\n", i, name);
        }
    }
    return out;
}
