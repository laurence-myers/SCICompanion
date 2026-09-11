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

bool CompileFixture(uint16_t scriptNumber, const std::string &fixtureName)
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
    return ok && !log.HasErrors() && SUCCEEDED(hr);
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
