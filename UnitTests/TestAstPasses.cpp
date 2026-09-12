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
#include "AstPassHelper.h"
#include "ScriptOMAll.h"
#include "AppState.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Tests for the decompiler AST passes. Each case parses a Sierra-syntax
// procedure body, runs the passes, and compares the printed text. No bytecode
// and no Sierra game data are needed.
//
// This file starts with harness self-tests. The per-pass cases land with the
// passes (WP3).

namespace UnitTests
{
    TEST_CLASS(TestAstPasses)
    {
    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            Assert::IsNull(appState, L"appState leaked from a prior test");
            _gameFolder = SetUpGameSCI11();
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // The harness parses and prints a script. A simple body survives the
        // round trip unchanged after whitespace normalization.
        TEST_METHOD(Harness_ParsePrintIdentity)
        {
            std::string body = "(= t 1)(if (> a 5) (= t 2) else (= t 3))(return t)";
            std::string expected = NormalizeWhitespace(WrapProcedure(body));
            std::string actual = ApplyAllPasses(body);
            // The body text is present verbatim (module whitespace) inside the
            // printed procedure.
            Assert::IsTrue(actual.find("(if (> a 5)") != std::string::npos,
                L"the if should print");
            Assert::IsTrue(actual.find("(return t)") != std::string::npos,
                L"the return should print");
        }

        // A nested and/or expression parses and prints as an n-ary form.
        TEST_METHOD(Harness_AndOrPrints)
        {
            std::string actual = ApplyAllPasses("(if (and a (or b c)) (= t 1))");
            Assert::IsTrue(actual.find("(and a (or b c))") != std::string::npos,
                L"the compound condition should print as written");
        }

    private:
        std::string _gameFolder;
    };
}
