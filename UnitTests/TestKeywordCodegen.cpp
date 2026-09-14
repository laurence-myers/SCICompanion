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
#include <fstream>
#include "Helper.h"
#include "DecompileHelper.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "CompiledScript.h"
#include "format.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Codegen tests for the language extensions that were merged into the default
// build (foreach, verbs, &exists). Each keyword is pure sugar: the compiler
// rewrites it into ordinary AST before code generation, so it must emit only
// bytecode a stock Sierra SCI interpreter runs. These tests prove that by
// compiling a small script that uses the keyword and then decompiling it: the
// decompiler only understands standard SCI opcodes, so a clean decompile (no
// assembly fallback, and no trace of the keyword) is evidence the emitted
// bytecode is standard. For &exists the test is stronger still: the emitted
// bytes are compared for exact equality against the hand-written expansion.

namespace UnitTests
{
    static const size_t npos = std::string::npos;

    static std::wstring W(const std::string &s)
    {
        return std::wstring(s.begin(), s.end());
    }

    // A minimal Sierra-syntax script header for resource number 902 (free in the
    // SCI1.1 template; each test is isolated, so any collision is harmless).
    static std::string Header()
    {
        return
            ";;; Sierra Script 1.0 - (do not remove this comment)\n"
            "(script# 902)\n"
            "(include sci.sh)\n";
    }

    // Writes source into the game's src folder as <name>.sc and compiles it as
    // the given resource number. Returns the compiler's success; on failure the
    // first error message is put in outError.
    static bool CompileSource(uint16_t number, const std::string &name, const std::string &source, std::string &outError)
    {
        std::string path = appState->GetResourceMap().Helper().GetScriptFileName(name);
        {
            std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
            file << source;
        }
        return CompileFixture(number, name, &outError);
    }

    // Loads the compiled script resource and returns its raw bytes.
    static std::vector<uint8_t> LoadCompiledBytes(uint16_t number)
    {
        const GameFolderHelper &helper = appState->GetResourceMap().Helper();
        CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
        Assert::IsTrue(compiled.Load(helper, helper.Version, number),
            L"could not load the compiled script resource");
        return compiled.GetRawBytes();
    }

    TEST_CLASS(TestKeywordCodegen)
    {
    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            Assert::IsNull(appState, L"appState leaked from a prior test");
        }

        TEST_METHOD_CLEANUP(Clean)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // &exists <param> must compile to exactly the same bytecode as the
        // hand-written argument-count check it stands for.
        TEST_METHOD(Exists_LowersToArgcCompare)
        {
            _gameFolder = SetUpGameSCI11();

            std::string keyword = Header() +
                "(public\n\tkTest 0\n)\n"
                "(procedure (kTest a b c)\n"
                "\t(if (&exists c) (return c))\n"
                "\t(if (&exists b) (return b))\n"
                "\t(return a)\n"
                ")\n";

            std::string manual = Header() +
                "(public\n\tkTest 0\n)\n"
                "(procedure (kTest a b c)\n"
                "\t(if (> argc 2) (return c))\n"
                "\t(if (> argc 1) (return b))\n"
                "\t(return a)\n"
                ")\n";

            std::string error;
            Assert::IsTrue(CompileSource(902, "kTest", keyword, error),
                W("&exists did not compile: " + error).c_str());
            std::vector<uint8_t> keywordBytes = LoadCompiledBytes(902);

            DecompileOutput decompiled = DecompileToText(902);
            Assert::AreEqual(0, decompiled.fallbacks, L"&exists fell back to assembly");
            Assert::IsFalse(decompiled.ContainsAsm(), L"&exists produced an assembly block");
            Assert::IsTrue(decompiled.text.find("&exists") == npos,
                L"&exists survived into the decompiled output (it is not a real opcode)");
            Assert::IsTrue(decompiled.text.find("argc") != npos,
                L"&exists should decompile to an argc comparison");

            error.clear();
            Assert::IsTrue(CompileSource(902, "kTest", manual, error),
                W("the manual (> argc N) form did not compile: " + error).c_str());
            std::vector<uint8_t> manualBytes = LoadCompiledBytes(902);

            Assert::IsTrue(keywordBytes == manualBytes,
                L"&exists emitted different bytecode than its (> argc N) expansion");
        }

        // foreach over an array must compile to an ordinary loop over standard
        // opcodes, with no assembly fallback and no trace of the keyword.
        TEST_METHOD(ForEach_LowersToStandardLoop)
        {
            _gameFolder = SetUpGameSCI11();

            std::string source = Header() +
                "(public\n\tkTest 0\n)\n"
                "(local\n\t[arr 5]\n\tsum\n)\n"
                "(procedure (kTest)\n"
                "\t(= sum 0)\n"
                "\t(foreach n arr\n"
                "\t\t(= sum (+ sum n))\n"
                "\t)\n"
                "\t(return sum)\n"
                ")\n";

            std::string error;
            Assert::IsTrue(CompileSource(902, "kTest", source, error),
                W("foreach did not compile: " + error).c_str());

            DecompileOutput decompiled = DecompileToText(902);
            Assert::AreEqual(0, decompiled.fallbacks, L"foreach fell back to assembly");
            Assert::IsFalse(decompiled.ContainsAsm(), L"foreach produced an assembly block");
            Assert::IsTrue(decompiled.text.find("foreach") == npos,
                L"foreach survived into the decompiled output (it is not a real opcode)");
        }

        // A verbs block must compile to an ordinary doVerb method (a switch on
        // the verb with a super doVerb: default), over standard opcodes.
        TEST_METHOD(Verbs_LowersToDoVerb)
        {
            _gameFolder = SetUpGameSCI11();

            std::string source = Header() +
                "(include Verbs.sh)\n"
                "(use Main)\n"
                "(use Game)\n"
                "(use System)\n"
                "(public\n\tkVerbs 0\n)\n"
                "(instance kVerbs of Room\n"
                "\t(verbs\n"
                "\t\t(V_DO (return 1))\n"
                "\t\t(V_LOOK (return 2))\n"
                "\t)\n"
                ")\n";

            std::string error;
            Assert::IsTrue(CompileSource(902, "kVerbs", source, error),
                W("verbs did not compile: " + error).c_str());

            DecompileOutput decompiled = DecompileToText(902);
            Assert::AreEqual(0, decompiled.fallbacks, L"verbs fell back to assembly");
            Assert::IsFalse(decompiled.ContainsAsm(), L"verbs produced an assembly block");
            Assert::IsTrue(decompiled.text.find("(verbs") == npos,
                L"the verbs block survived into the decompiled output (it is not real bytecode)");
            Assert::IsTrue(decompiled.text.find("doVerb") != npos,
                L"verbs should decompile to a doVerb method");
        }

        // Reserved code-level keywords must be rejected as identifiers, so a
        // script that uses one as a variable name gets a clean compile error
        // instead of silently mis-parsing. (Before the SCIKeywords comma fix,
        // cond/for/if/mod/super were missing from the list and were wrongly
        // accepted as names.)
        TEST_METHOD(ReservedWords_RejectedAsIdentifiers)
        {
            _gameFolder = SetUpGameSCI11();

            auto varProc = [](const std::string &name)
            {
                return Header() +
                    "(public\n\tkTest 0\n)\n"
                    "(procedure (kTest &tmp " + name + ")\n"
                    "\t(= " + name + " 1)\n"
                    "\t(return " + name + ")\n"
                    ")\n";
            };

            // Positive control: a non-keyword name compiles, so any failure
            // below is due to the name being reserved, not the surrounding code.
            std::string error;
            Assert::IsTrue(CompileSource(902, "kTest", varProc("notAKeyword"), error),
                W("control (non-keyword variable) failed to compile: " + error).c_str());

            const char *reserved[] = { "for", "if", "cond", "mod", "super" };
            for (const char *name : reserved)
            {
                error.clear();
                bool compiled = CompileSource(902, "kTest", varProc(name), error);
                Assert::IsFalse(compiled,
                    W(std::string("'") + name + "' was accepted as a variable name but is a reserved keyword").c_str());
            }
        }

    private:
        std::string _gameFolder;
    };
}
