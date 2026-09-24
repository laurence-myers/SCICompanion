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
#include "ResourceContainer.h"
#include "CompiledScript.h"
#include "WordEnumString.h"
#include "format.h"
#include "TestSupport.h"

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
    // first error message is put in outError. The warnings go to outWarnings,
    // and every error to outErrors, when they are not null.
    static bool CompileSource(uint16_t number, const std::string &name, const std::string &source, std::string &outError, std::vector<std::string> *outWarnings = nullptr,
        std::vector<std::string> *outErrors = nullptr)
    {
        std::string path = appState->GetResourceMap().Helper().GetScriptFileName(name);
        {
            std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
            file << source;
        }
        return CompileFixture(number, name, &outError, outWarnings, outErrors);
    }

    // Script 902 with the public procedure kTest: its parameters and its
    // body. The uses go before the public block.
    static std::string KTest(const std::string &params, const std::string &body, const std::string &uses = std::string())
    {
        return Header() + uses + "(public\n\tkTest 0\n)\n(procedure (kTest" + (params.empty() ? "" : " " + params) + ")\n" + body + ")\n";
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

        // Compiles the keyword form and the hand-written asm form of the same
        // procedure, and asserts that their bytes are equal.
        void AssertSameBytes(const std::string &keyword, const std::string &manual, const wchar_t *what)
        {
            // Compile first: the arguments of Assert::IsTrue are evaluated in
            // no fixed order, so the text must not read error in the same call.
            std::string error;
            bool compiled = CompileSource(902, "kTest", keyword, error);
            Assert::IsTrue(compiled, W("the expression did not compile: " + error).c_str());
            std::vector<uint8_t> keywordBytes = LoadCompiledBytes(902);
            error.clear();
            compiled = CompileSource(902, "kTest", manual, error);
            Assert::IsTrue(compiled, W("the asm form did not compile: " + error).c_str());
            std::vector<uint8_t> manualBytes = LoadCompiledBytes(902);
            Assert::IsTrue(keywordBytes == manualBytes, what);
        }

        // An and/or used for its value gives the operand that decides it, not
        // 1 or 0, as Sierra's sc does (MakeAnd, MakeOr): "a; bnt E; b; E:"
        // for and, and bt for or.
        TEST_METHOD(ValueAndOr_GiveTheDecidingOperand)
        {
            _gameFolder = SetUpGameSCI11();
            std::string keyword = KTest("a b &tmp t",
                "\t(= t (and a b))\n"
                "\t(= t (or a b))\n"
                "\t(return t)\n");
            std::string manual = KTest("a b &tmp t",
                "\t(asm\n"
                "\t\tlap a\n"
                "\t\tbnt andEnd\n"
                "\t\tlap b\n"
                "\tandEnd:\n"
                "\t\tsat t\n"
                "\t\tlap a\n"
                "\t\tbt orEnd\n"
                "\t\tlap b\n"
                "\torEnd:\n"
                "\t\tsat t\n"
                "\t)\n"
                "\t(return t)\n");
            AssertSameBytes(keyword, manual, L"a value and/or must compile to Sierra's short-circuit shape");
        }

        // The same in a push context (a call argument). Both exits of the
        // short circuit must join before the push; a join after it skips the
        // push on the short path, and the call gets a wrong stack.
        TEST_METHOD(ValueAndOr_InACallArgument_JoinBeforeThePush)
        {
            _gameFolder = SetUpGameSCI11();
            std::string keyword = KTest("a b &tmp t",
                "\t(= t (Abs (and a b)))\n"
                "\t(= t (Abs (or a b)))\n"
                "\t(return t)\n");
            std::string manual = KTest("a b &tmp t",
                "\t(asm\n"
                "\t\tpush1\n"
                "\t\tlap a\n"
                "\t\tbnt andEnd\n"
                "\t\tlap b\n"
                "\tandEnd:\n"
                "\t\tpush\n"
                "\t\tcallk Abs, 2\n"
                "\t\tsat t\n"
                "\t\tpush1\n"
                "\t\tlap a\n"
                "\t\tbt orEnd\n"
                "\t\tlap b\n"
                "\torEnd:\n"
                "\t\tpush\n"
                "\t\tcallk Abs, 2\n"
                "\t\tsat t\n"
                "\t)\n"
                "\t(return t)\n");
            AssertSameBytes(keyword, manual, L"a value and/or in a call argument must join before the push");
        }

        // In a condition, and/or branch to the if's else. A condition does not
        // use the value path, so this test pins the condition shape; it is
        // not a negative check for the value shape.
        TEST_METHOD(ConditionAndOr_BranchToTheElse)
        {
            _gameFolder = SetUpGameSCI11();
            std::string keyword = KTest("a b &tmp t",
                "\t(if (and a b)\n"
                "\t\t(= t 1)\n"
                "\telse\n"
                "\t\t(= t 2)\n"
                "\t)\n"
                "\t(if (or a b)\n"
                "\t\t(= t 3)\n"
                "\telse\n"
                "\t\t(= t 4)\n"
                "\t)\n"
                "\t(return t)\n");
            std::string manual = KTest("a b &tmp t",
                "\t(asm\n"
                "\t\tlap a\n"
                "\t\tbnt andElse\n"
                "\t\tlap b\n"
                "\t\tbnt andElse\n"
                "\t\tldi 1\n"
                "\t\tsat t\n"
                "\t\tjmp andEnd\n"
                "\tandElse:\n"
                "\t\tldi 2\n"
                "\t\tsat t\n"
                "\tandEnd:\n"
                "\t\tlap a\n"
                "\t\tbt orThen\n"
                "\t\tlap b\n"
                "\t\tbnt orElse\n"
                "\torThen:\n"
                "\t\tldi 3\n"
                "\t\tsat t\n"
                "\t\tjmp orEnd\n"
                "\torElse:\n"
                "\t\tldi 4\n"
                "\t\tsat t\n"
                "\torEnd:\n"
                "\t\tlat t\n"
                "\t\tret\n"
                "\t)\n");
            AssertSameBytes(keyword, manual, L"an and/or in a condition must branch to the else of the if");
        }

        // A call to proc<N>_<M> that no name resolves, in a game with no
        // script N (Sierra removed script 911 from KQ6), compiles to
        // "calle N M" with a warning.
        TEST_METHOD(MissingScriptProc_CompilesToCalleWithAWarning)
        {
            _gameFolder = SetUpGameSCI11();
            Assert::IsTrue(nullptr == appState->GetResourceMap().Helper().MostRecentResource(ResourceType::Script, 911, ResourceEnumFlags::None),
                L"the template has no script 911");
            std::string source = KTest("", "\t(proc911_0 5)\n");
            std::string error;
            std::vector<std::string> warnings;
            // Compile first: the arguments of Assert::IsTrue are evaluated in
            // no fixed order, so the text must not read error in the same call.
            bool compiled = CompileSource(902, "kTest", source, error, &warnings);
            Assert::IsTrue(compiled, W("proc911_0 did not compile: " + error).c_str());
            std::string allWarnings = JoinLines(warnings);
            Assert::IsTrue(allWarnings.find("no script 911") != npos, W("expected a warning about script 911, got: " + allWarnings).c_str());

            // The decompiler writes a call to an export that is not in the game
            // as __proc<N>_<M>, so this shows "calle 911 0".
            DecompileOutput decompiled = DecompileToText(902);
            Assert::IsTrue(decompiled.text.find("(__proc911_0 5)") != npos, W(decompiled.text).c_str());
        }

        // A Said string in a game with no vocabulary resource gives one
        // compile error that names the resource.
        TEST_METHOD(SaidWithNoVocabulary_IsAnErrorThatNamesTheResource)
        {
            _gameFolder = SetUpGameSCI0();
            CResourceMap &resourceMap = appState->GetResourceMap();
            std::unique_ptr<ResourceBlob> vocabulary = resourceMap.Helper().MostRecentResource(ResourceType::Vocab, 0, ResourceEnumFlags::None);
            Assert::IsTrue(vocabulary != nullptr, L"the SCI0 template has vocab.000");
            resourceMap.DeleteResource(vocabulary.get());
            // The resource map keeps the vocabulary that it read.
            resourceMap.ClearVocab000();
            Assert::IsTrue(nullptr == resourceMap.GetVocab000(), L"setup: no vocabulary");

            // Two Said strings: the error comes once for the compile, not once
            // for each Said string or word.
            std::string source = KTest("",
                "\t(if (Said 'look/door')\n"
                "\t\t(return 1)\n"
                "\t)\n"
                "\t(if (Said 'open/door')\n"
                "\t\t(return 2)\n"
                "\t)\n"
                "\t(return 0)\n");
            std::string error;
            std::vector<std::string> errors;
            bool compiled = CompileSource(902, "kTest", source, error, nullptr, &errors);
            Assert::IsFalse(compiled, L"a Said string needs the vocabulary");
            Assert::AreEqual((size_t)1, errors.size(), W("expected one error, got:\n" + JoinLines(errors)).c_str());
            Assert::IsTrue(errors[0].find("vocab 0") != npos, W("expected an error that names vocab 0, got: " + errors[0]).c_str());
        }

        // The main vocabulary of an SCI1.1 game with no vocab 0 is vocab 900,
        // and the error names it. The SCI1.1 template has neither.
        TEST_METHOD(SaidWithNoVocabulary_SCI11_NamesVocab900)
        {
            _gameFolder = SetUpGameSCI11();
            CResourceMap &resourceMap = appState->GetResourceMap();
            Assert::AreEqual(900, (int)resourceMap.Helper().Version.MainVocabResource, L"setup: the main vocabulary is vocab 900");
            Assert::IsTrue(nullptr == resourceMap.GetVocab000(), L"setup: no vocabulary");

            std::string source = KTest("",
                "\t(if (Said 'look/door')\n"
                "\t\t(return 1)\n"
                "\t)\n"
                "\t(return 0)\n");
            std::string error;
            bool compiled = CompileSource(902, "kTest", source, error);
            Assert::IsFalse(compiled, L"a Said string needs the vocabulary");
            Assert::IsTrue(error.find("vocab 900") != npos, W("expected an error that names vocab 900, got: " + error).c_str());
        }

        // The auto-complete word list of the script editor's "Add as synonym
        // of" dialog is empty in a game with no vocabulary.
        TEST_METHOD(WordList_GameWithNoVocabulary_IsEmpty)
        {
            _gameFolder = SetUpGameSCI11();
            Assert::IsTrue(nullptr == appState->GetResourceMap().GetVocab000(), L"setup: no vocabulary");

            IEnumString *words = nullptr;
            HRESULT hr = CWordEnumString_CreateInstance(IID_IEnumString, (void **)&words);
            Assert::IsTrue(SUCCEEDED(hr) && (words != nullptr), L"the word list was not made");
            LPOLESTR word = nullptr;
            ULONG fetched = 0;
            hr = words->Next(1, &word, &fetched);
            words->Release();
            Assert::AreEqual(0, (int)fetched, L"a game with no vocabulary has no words");
            Assert::IsTrue(hr == S_FALSE, L"Next must give S_FALSE when no word is left");
        }

        // When the game has script N, an unresolved proc<N>_<M> stays an
        // error. Script 0 of the template has no export 99.
        TEST_METHOD(MissingScriptProc_ScriptThatExists_StaysAnError)
        {
            _gameFolder = SetUpGameSCI11();
            std::string source = KTest("", "\t(proc0_99 5)\n");
            std::string error;
            Assert::IsFalse(CompileSource(902, "kTest", source, error), L"proc0_99 must not compile: the game has script 0");
            Assert::IsTrue(error.find("proc0_99") != npos, W("expected an error for proc0_99, got: " + error).c_str());
        }

        // __proc<N>_<M> is what the decompiler writes for a call to an export
        // that is not in the game. The numbers follow the prefix, so
        // __proc911_0 gives "calle 911 0" (not "calle 0 11"), and such a
        // decompiled script round-trips.
        TEST_METHOD(UnderscoreProc_GivesItsScriptAndExport)
        {
            _gameFolder = SetUpGameSCI11();
            std::string escaped = KTest("", "\t(__proc911_0 5)\n");
            std::string plain = KTest("", "\t(proc911_0 5)\n");
            AssertSameBytes(escaped, plain, L"__proc911_0 must compile to calle 911 0");
        }

        // An asm calle of proc<N>_<M> of a missing script compiles too, with
        // the warning, to the bytes of the call. The asm fallback of the
        // decompiler writes this form (DecompilerFallback.cpp).
        TEST_METHOD(MissingScriptProc_InAsm_CompilesToCalleWithAWarning)
        {
            _gameFolder = SetUpGameSCI11();
            std::string call = KTest("", "\t(proc911_0)\n");
            std::string manual = KTest("",
                "\t(asm\n"
                "\t\tpush0\n"
                "\t\tcalle proc911_0, 0\n"
                "\t)\n");
            std::string error;
            std::vector<std::string> warnings;
            bool compiled = CompileSource(902, "kTest", manual, error, &warnings);
            Assert::IsTrue(compiled, W("the asm calle did not compile: " + error).c_str());
            Assert::IsTrue(JoinLines(warnings).find("no script 911") != npos, W("expected a warning about script 911, got:\n" + JoinLines(warnings)).c_str());
            AssertSameBytes(call, manual, L"the asm calle must give the bytes of the call");
        }

        // proc<N>_<M> of a missing script is a procedure only in a call. As a
        // value it is an undeclared name, and not "The '(' character must
        // immediately follow the function call".
        TEST_METHOD(MissingScriptProc_AsAValue_IsAnUndeclaredName)
        {
            _gameFolder = SetUpGameSCI11();
            std::string source = KTest("&tmp t",
                "\t(= t proc911_0)\n"
                "\t(return t)\n");
            std::string error;
            bool compiled = CompileSource(902, "kTest", source, error);
            Assert::IsFalse(compiled, L"proc911_0 is not a value");
            Assert::IsTrue(error.find("Undeclared identifier") != npos, W("expected an undeclared-identifier error, got: " + error).c_str());
        }

        // The decompiler writes no leading zero, so a number with one is a
        // typo, and the name stays an error.
        TEST_METHOD(MissingScriptProc_LeadingZero_StaysAnError)
        {
            _gameFolder = SetUpGameSCI11();
            for (const std::string name : { "proc0911_0", "proc911_00" })
            {
                std::string source = KTest("", "\t(" + name + " 5)\n");
                std::string error;
                bool compiled = CompileSource(902, "kTest", source, error);
                Assert::IsFalse(compiled, W(name + " must not compile").c_str());
                Assert::IsTrue(error.find(name) != npos, W("expected an error for " + name + ", got: " + error).c_str());
            }
        }

        // The decompiler writes a callb to an export that main does not have
        // as __proc0_<M>. It compiles back to callb, as a call of a main
        // procedure by its name does, not to calle 0 M, which is one byte
        // longer, or two when the operands are words.
        TEST_METHOD(UnderscoreProc_OfMain_CompilesToCallb)
        {
            _gameFolder = SetUpGameSCI11();
            auto Source = [](const std::string &body) { return KTest("", body, "(use Main)\n"); };
            // Export 1 of the template's main script is Btest.
            AssertSameBytes(Source("\t(__proc0_1 5)\n"), Source("\t(Btest 5)\n"), L"__proc0_1 must compile to the callb of (Btest 5)");
            // Main has no export 99; the call is a callb all the same.
            AssertSameBytes(Source("\t(__proc0_99 5)\n"),
                Source("\t(asm\n\t\tpush1\n\t\tpushi 5\n\t\tcallb __proc0_99, 2\n\t)\n"),
                L"__proc0_99 must compile to callb 99");
        }
        // _file_ / _line_ are SCI2-only debug pseudo-opcodes. Using one in an
        // asm block in a non-SCI2 (here SCI1.1) game must be a compile error, not
        // an out-of-bounds read of the operand table that emits a corrupt opcode.
        TEST_METHOD(AsmFileOpcode_RejectedInSCI11)
        {
            _gameFolder = SetUpGameSCI11();
            std::string source = Header() +
                "(public\n\tkTest 0\n)\n"
                "(procedure (kTest)\n"
                "\t(asm\n"
                "\t\t_file_ 0\n"
                "\t\tret\n"
                "\t)\n"
                ")\n";
            std::string error;
            bool compiled = CompileSource(902, "kTest", source, error);
            Assert::IsFalse(compiled,
                L"_file_ was accepted in a non-SCI2 game but is an SCI2-only pseudo-opcode");
            // Assert the SPECIFIC guard error, not just any failure: before the
            // fix the out-of-bounds operand row can yield a spurious "too many
            // arguments" error, which would make a bare IsFalse pass vacuously.
            Assert::IsTrue(error.find("SCI2") != npos,
                W("expected an SCI2-only error, got: " + error).c_str());
        }

        // A string table larger than 64 KB cannot be represented in the 16-bit
        // section size. The compiler must report an error, not silently wrap it.
        TEST_METHOD(StringTableOverflow_YieldsError)
        {
            _gameFolder = SetUpGameSCI11();
            std::string body;
            for (int i = 0; i < 400; i++)
            {
                std::string s = "s" + std::to_string(i) + "_";   // distinct prefix (avoids dedup)
                s.append(200 - s.size(), 'x');                    // pad to 200 chars
                body += "\t(= bigStr \"" + s + "\")\n";           // ~400 * 201 = ~80KB > 0xFFFF
            }
            std::string source = Header() +
                "(public\n\tkTest 0\n)\n"
                "(local bigStr)\n"
                "(procedure (kTest)\n" + body +
                "\t(return bigStr)\n"
                ")\n";
            std::string error;
            bool compiled = CompileSource(902, "kTest", source, error);
            Assert::IsFalse(compiled,
                L"a >64KB string table compiled without error (the 16-bit section size wrapped)");
            Assert::IsTrue(error.find("too large") != npos,
                W("expected a string-table-too-large error, got: " + error).c_str());
        }

        // A constant binary expression must fold to the value the SCI runtime
        // would compute. The regression: (mod a b) folded to (a & b), and the
        // shifts had undefined behaviour for a count of 16 or more.
        TEST_METHOD(ConstantFold_BinaryOperators)
        {
            _gameFolder = SetUpGameSCI11();

            auto Proc = [](const std::string &expr) {
                return Header() +
                    "(public\n\tkTest 0\n)\n"
                    "(procedure (kTest)\n\t(return " + expr + ")\n)\n";
            };

            struct Case { const char *expr; const char *expected; };
            const Case cases[] = {
                { "(mod 7 3)", "1" },       // regression: the folder did (a & b) == 3
                { "(mod -7 3)", "2" },      // SCI modulo is Euclidean, not C's -1
                { "(mod 7 -3)", "1" },      // the divisor magnitude only
                { "(+ 7 3)", "10" }, { "(- 7 3)", "4" }, { "(* 7 3)", "21" }, { "(/ 7 3)", "2" },
                { "(& 6 3)", "2" }, { "(| 6 3)", "7" }, { "(^ 6 3)", "5" },
                { "(>> 16 2)", "4" }, { "(<< 3 2)", "12" },
                { "(>> 65535 40)", "0" },   // regression: was UB (shift count >= 16)
                { "(<< 1 40)", "0" },       // regression: was UB
                { "(== 7 3)", "0" }, { "(!= 7 3)", "1" }, { "(< 3 7)", "1" }, { "(<= 7 7)", "1" },
                { "(> 7 3)", "1" }, { "(>= 3 7)", "0" },
            };

            for (const Case &c : cases)
            {
                std::string error;
                Assert::IsTrue(CompileSource(902, "kTest", Proc(c.expr), error),
                    W(std::string("expr failed: ") + c.expr + " : " + error).c_str());
                std::vector<uint8_t> exprBytes = LoadCompiledBytes(902);

                error.clear();
                Assert::IsTrue(CompileSource(902, "kTest", Proc(c.expected), error),
                    W(std::string("literal failed: ") + c.expected + " : " + error).c_str());
                std::vector<uint8_t> litBytes = LoadCompiledBytes(902);

                Assert::IsTrue(exprBytes == litBytes,
                    W(std::string("fold of ") + c.expr + " != literal " + c.expected).c_str());
            }
        }

        // A foreach nested inside another foreach must lower BOTH loops. The
        // regression: the outer loop's lowering moved its body (with the inner
        // foreach) into FinalCode, which the lowering traversal never visited,
        // so the inner loop was dropped and emitted nothing.
        TEST_METHOD(NestedForEach_LowersBothLoops)
        {
            _gameFolder = SetUpGameSCI11();

            std::string source = Header() +
                "(public\n\tkTest 0\n)\n"
                "(local\n\t[arr1 5]\n\t[arr2 3]\n\tsum\n)\n"
                "(procedure (kTest)\n"
                "\t(= sum 0)\n"
                "\t(foreach a arr1\n"
                "\t\t(foreach b arr2\n"
                "\t\t\t(= sum (+ sum b))\n"
                "\t\t)\n"
                "\t)\n"
                "\t(return sum)\n"
                ")\n";

            std::string error;
            Assert::IsTrue(CompileSource(902, "kTest", source, error),
                W("nested foreach did not compile: " + error).c_str());

            DecompileOutput decompiled = DecompileToText(902);
            Assert::AreEqual(0, decompiled.fallbacks, L"nested foreach fell back to assembly");
            Assert::IsFalse(decompiled.ContainsAsm(), L"nested foreach produced an assembly block");
            Assert::IsTrue(decompiled.text.find("foreach") == npos,
                L"foreach survived into the decompiled output (it is not a real opcode)");

            // Both foreachs must lower to a loop. The decompiler renders these
            // array-bounded loops as (while ...). Before the fix the inner loop
            // is dropped and only the outer loop is emitted (one while).
            size_t loops = 0;
            for (size_t p = decompiled.text.find("(while"); p != npos; p = decompiled.text.find("(while", p + 1))
            {
                loops++;
            }
            Assert::IsTrue(loops >= 2,
                W(fmt::format("expected two nested loops, found {0} (while) construct(s):\n{1}",
                    loops, decompiled.text)).c_str());
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
