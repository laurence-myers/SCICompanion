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
#include "PhonemeMap.h"
#include "TalkerToViewMap.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #73: the parsers for user-editable configuration files crashed or
    // truncated instead of reporting.
    TEST_CLASS(TestConfigParsers)
    {
        std::filesystem::path _folder;

        static std::filesystem::path MakeTempFolder()
        {
            std::filesystem::path folder = std::filesystem::temp_directory_path() / ("scicompanion-config-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()));
            std::filesystem::create_directories(folder);
            return folder;
        }

        static void WriteFile(const std::filesystem::path &path, const std::string &text)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << text;
        }

    public:
        TEST_METHOD_INITIALIZE(SetUp)
        {
            _folder = MakeTempFolder();
        }

        TEST_METHOD_CLEANUP(TearDown)
        {
            std::error_code ec;
            std::filesystem::remove_all(_folder, ec);
        }

        // PhonemeMap dereferenced as<int64_t>() without a null check. A value
        // that is not an integer is not a parse error, so the catch did not
        // see it, and the null dereference crashed.
        TEST_METHOD(PhonemeMap_NonIntegerValue_IsReportedAndSkipped)
        {
            std::filesystem::path path = _folder / "phonemes.ini";
            WriteFile(path,
                "[phoneme_to_cel]\n"
                "x = 0\n"
                "ah = \"three\"\n"
                "eh = 4\n");

            PhonemeMap map(path.string());

            Assert::AreEqual((uint16_t)0, map.PhonemeToCel("x"), L"an integer entry before the bad one is kept");
            Assert::AreEqual((uint16_t)4, map.PhonemeToCel("eh"), L"an integer entry after the bad one is kept");
            Assert::AreEqual((uint16_t)0xffff, map.PhonemeToCel("ah"), L"the non-integer entry is skipped");
            Assert::IsTrue(map.HasErrors(), L"the bad entry must be reported");
            Assert::IsTrue(map.GetErrors().find("ah") != std::string::npos, L"the report names the entry");
        }

        // TalkerToViewMap called stoi on every line. A blank or comment line
        // threw std::invalid_argument, and the catch outside the loop ended the
        // read, so every entry after that line was lost.
        TEST_METHOD(TalkerToViewMap_BlankAndCommentLines_DoNotStopTheRead)
        {
            WriteFile(_folder / "talker_to_view.ini",
                "; talker = view loop\n"
                "1 = 100 2\n"
                "\n"
                "# another comment\n"
                "   \n"
                "2 = 200 3\n"
                "not a number\n"
                "3 = 300 4\n");

            TalkerToViewMap map(_folder.string());

            uint16_t view = 0, loop = 0;
            Assert::IsTrue(map.TalkerToViewLoop(1, view, loop));
            Assert::AreEqual((uint16_t)100, view);
            Assert::AreEqual((uint16_t)2, loop);
            Assert::IsTrue(map.TalkerToViewLoop(2, view, loop), L"the entry after the blank and comment lines must be read");
            Assert::AreEqual((uint16_t)200, view);
            Assert::AreEqual((uint16_t)3, loop);
            Assert::IsTrue(map.TalkerToViewLoop(3, view, loop), L"the entry after a malformed line must be read");
            Assert::AreEqual((uint16_t)300, view);
            Assert::AreEqual((uint16_t)4, loop);
        }
    };
}
