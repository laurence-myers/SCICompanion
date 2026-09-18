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
#include "ScriptOM.h"
#include "AstPassHelper.h"
#include "Helper.h"
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #58: the parser passed a signed char to isspace/isalpha/isalnum/isdigit/
    // isxdigit. This build is MBCS, so a byte >= 0x80 is a negative int. The C
    // standard only allows 0..255 and EOF, and the Debug CRT asserts on any other
    // value. A script comment or string that holds a Latin-1 / code-page byte
    // (for example an accented letter) therefore aborted a Debug build. Every
    // call now casts to unsigned char first.
    //
    // The Release UCRT returns 0 for an out-of-range value, so this test cannot
    // observe the old fault under Release; it pins the behaviour (a high-bit byte
    // is not whitespace and not an identifier character) and guards the Debug
    // assert when the suite runs there.
    TEST_CLASS(TestParserCtype)
    {
        std::string _gameFolder;

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            _gameFolder = SetUpGameSCI0();
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        TEST_METHOD(HighBitBytesInCommentAndString_ParseCleanly)
        {
            // 0xE9 is 'e-acute' in Windows-1252 / Latin-1. It sits in a comment
            // (EatWhitespaceAndComments) and in a string literal (the quoted-
            // string parser), and the identifier scanner (SelectorP) meets it at
            // the end of the comment line.
            std::string text =
                ";;; Sierra Script 1.0 - (do not remove this comment)\n"
                "(script# 970)\n"
                "; caf\xE9 latte \xE9\xE9\n"
                "(procedure (ProcWithHighBit &tmp t)\n"
                "\t(= t \"caf\xE9\")\n"
                "\t(return t)\n"
                ")\n";
            std::string error;
            std::unique_ptr<sci::Script> script = TryParseSierraScript(text, &error);
            Assert::IsTrue(script != nullptr,
                std::wstring(L"A script with a high-bit byte in a comment and a string must parse: ").append(error.begin(), error.end()).c_str());
            Assert::AreEqual((size_t)1, script->GetProcedures().size(), L"the procedure after the high-bit comment must be parsed");
            Assert::AreEqual(std::string("ProcWithHighBit"), script->GetProcedures()[0]->GetName());
        }
    };
}
