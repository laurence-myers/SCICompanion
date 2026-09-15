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
#include "AppState.h"
#include "sci.h"
#include "ImageUtil.h"
#include "View.h"
#include "PaletteOperations.h"
#include "DebuggerThread.h"
#include <vector>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    TEST_CLASS(TestUtilHardening)
    {
        bool _createdAppState = false;

    public:
        TEST_METHOD_INITIALIZE(SetUpUtil)
        {
            if (appState == nullptr)
            {
                appState = new AppState(nullptr);
                _createdAppState = true;
            }
        }

        TEST_METHOD_CLEANUP(TearDownUtil)
        {
            if (_createdAppState)
            {
                delete appState;
                appState = nullptr;
                _createdAppState = false;
            }
        }

        // replacefile must atomically replace the destination (MoveFileEx with
        // MOVEFILE_REPLACE_EXISTING), whether or not it already exists, and remove
        // the source. This primitive is what makes the resource save (#66) never
        // leave a volume or patch file missing.
        TEST_METHOD(ReplaceFile_ReplacesExistingOrCreates_AndRemovesSource)
        {
            char tempDir[MAX_PATH] = { 0 };
            GetTempPathA(ARRAYSIZE(tempDir), tempDir);
            std::string dst = std::string(tempDir) + "scic_replacefile_dst.tmp";
            std::string src = std::string(tempDir) + "scic_replacefile_src.tmp";

            auto writeFile = [](const std::string &path, const std::string &content)
            {
                ScopedFile f(path, GENERIC_WRITE, 0, CREATE_ALWAYS);
                f.Write(reinterpret_cast<const uint8_t *>(content.data()), (uint32_t)content.size());
            };
            auto readFile = [](const std::string &path) -> std::string
            {
                ScopedFile f(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
                DWORD size = GetFileSize(f.hFile, nullptr);
                std::string out(size, '\0');
                DWORD read = 0;
                ReadFile(f.hFile, &out[0], size, &read, nullptr);
                out.resize(read);
                return out;
            };
            auto exists = [](const std::string &path)
            {
                return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
            };

            DeleteFileA(dst.c_str());
            DeleteFileA(src.c_str());

            // Destination exists -> its contents are replaced, source removed.
            writeFile(dst, "old-destination-contents");
            writeFile(src, "new-content");
            replacefile(src, dst);
            Assert::AreEqual(std::string("new-content"), readFile(dst), L"destination is replaced");
            Assert::IsFalse(exists(src), L"source is moved away");

            // Destination absent -> replacefile still creates it.
            DeleteFileA(dst.c_str());
            writeFile(src, "fresh");
            replacefile(src, dst);
            Assert::AreEqual(std::string("fresh"), readFile(dst), L"destination created when absent");
            Assert::IsFalse(exists(src), L"source is moved away (absent-destination case)");

            DeleteFileA(dst.c_str());
        }

        // (d1) A GIF whose file cannot be opened: DGifOpenFileName returns null,
        // and DGifSlurp(null) immediately writes through the null pointer. The
        // loader must return false before calling DGifSlurp.
        TEST_METHOD(GetCelsAndPaletteFromGIFFile_OpenFailure_ReturnsFalse)
        {
            std::vector<Cel> cels;
            std::vector<PaletteComponent> palettes;
            PaletteComponent globalPalette;

            bool ok = GetCelsAndPaletteFromGIFFile("Z:\\does\\not\\exist_12345.gif",
                cels, palettes, globalPalette);

            Assert::IsFalse(ok, L"a GIF that cannot be opened must fail, not null-deref");
        }

        // (e) The debugger line splitter reassembles lines across reads and
        // carries a trailing partial line to the next call. The old code put the
        // terminator at cbRead, which lands inside carried-over data and injects
        // a NUL that truncates a reassembled line. The terminator must land at the
        // true end of the valid data.
        TEST_METHOD(ExtractDebugLines_ReassemblesChunkedInput)
        {
            char szBuffer[1024];
            size_t valid = 0;

            // Chunk 1: "abc\nde" -> one line "abc", "de" carried over.
            const char *c1 = "abc\nde";
            memcpy(szBuffer + valid, c1, 6);
            std::vector<std::string> l1 = ExtractDebugLines(szBuffer, sizeof(szBuffer), valid, 6);
            Assert::AreEqual((size_t)1, l1.size(), L"chunk 1 must yield one complete line");
            Assert::AreEqual(std::string("abc"), l1[0]);
            Assert::AreEqual((size_t)2, valid, L"the partial line 'de' must be carried over");

            // Chunk 2: "fg\nhij\n" -> "defg" and "hij".
            const char *c2 = "fg\nhij\n";
            memcpy(szBuffer + valid, c2, 7);
            std::vector<std::string> l2 = ExtractDebugLines(szBuffer, sizeof(szBuffer), valid, 7);
            Assert::AreEqual((size_t)2, l2.size(), L"chunk 2 must yield two complete lines");
            Assert::AreEqual(std::string("defg"), l2[0], L"the carried 'de' must join 'fg'");
            Assert::AreEqual(std::string("hij"), l2[1], L"the second line must not be truncated by a stray NUL");
            Assert::AreEqual((size_t)3, l2[1].length(), L"no embedded NUL in the reassembled line");
            Assert::AreEqual((size_t)0, valid, L"no partial line remains");
        }

        // (c) DeleteDirectory copied the folder into a MAX_PATH stack buffer, then
        // wrote a second NUL at the UNtruncated length, overflowing the buffer for
        // a long path. It must refuse a path too long to double-null-terminate.
        // NOTE: hardening guard. The IsFalse assertion holds both before and after
        // the fix; the pre-fix out-of-bounds write is only reliably caught under a
        // checked build (/RTC or AddressSanitizer). This documents the contract.
        TEST_METHOD(DeleteDirectory_LongPath_NoStackOverflow)
        {
            std::string longPath(300, 'a'); // longer than MAX_PATH - 1

            bool ok = DeleteDirectory(nullptr, longPath);

            Assert::IsFalse(ok, L"a path too long to double-null-terminate must be refused");
        }
    };
}
