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
#include "AstPassHelper.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "ScriptOMAll.h"
#include "SyntaxParser.h"
#include "CompiledScript.h"
#include "CompileContext.h"
#include "CrystalScriptStream.h"
#include "CCrystalTextBuffer.h"
#include "DecompilerAstPasses.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

static std::wstring AstToWString(const std::string &text)
{
    return std::wstring(text.begin(), text.end());
}

std::string WrapProcedure(const std::string &body)
{
    // The parser needs the Sierra marker on the first line to pick the SCI
    // grammar. Declare a few parameters and temps so cases can use them.
    std::string text;
    text += ";;; Sierra Script 1.0 - (do not remove this comment)\n";
    text += "(script# 990)\n";
    text += "(include sci.sh)\n";
    text += "(procedure (astCase a b c &tmp t u)\n";
    text += body;
    text += "\n)\n";
    return text;
}

std::unique_ptr<sci::Script> ParseSierraScript(const std::string &text)
{
    // Write the text to a script file in the temporary game, then parse it the
    // same way the compiler parses a header (see CompileContext.cpp).
    std::string path = appState->GetResourceMap().Helper().GetScriptFileName("AstPassCase");
    {
        std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
        file << text;
    }

    ScriptId scriptId(path.c_str());
    auto script = std::make_unique<sci::Script>(scriptId);

    CCrystalTextBuffer buffer;
    Assert::IsTrue(buffer.LoadFromFile(path.c_str()) != FALSE, L"Could not load parse buffer");
    CScriptStreamLimiter limiter(&buffer);
    CCrystalScriptStream stream(&limiter);

    CompileLog log;
    bool ok = SyntaxParser_Parse(*script, stream,
        PreProcessorDefinesFromSCIVersion(appState->GetVersion()), &log);
    buffer.FreeAll();

    if (!ok)
    {
        std::string message = "Parse failed:";
        for (const CompileResult &r : log.Results())
        {
            message += "\n  " + r.GetMessage();
        }
        Assert::Fail(AstToWString(message).c_str());
    }
    return script;
}

std::string ScriptToText(const sci::Script &script)
{
    std::stringstream ss;
    sci::SourceCodeWriter writer(ss, LangSyntaxSCI, const_cast<sci::Script *>(&script));
    script.OutputSourceCode(writer);
    return ss.str();
}

std::string NormalizeWhitespace(const std::string &text)
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

std::string ApplyAllPasses(const std::string &body)
{
    std::unique_ptr<sci::Script> script = ParseSierraScript(WrapProcedure(body));
    AstPassOptions options;
    for (auto &proc : script->GetProceduresNC())
    {
        RunDecompilerAstPasses(*proc, options, nullptr);
    }
    return NormalizeWhitespace(ScriptToText(*script));
}
