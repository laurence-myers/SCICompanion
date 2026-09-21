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

        // Loading a resource with no GUI must not crash. AudioComponentFromWaveFile
        // reports wave-conversion warnings through appState->OutputResults, which
        // reaches the main-frame output pane. Headless (AppState is built with a
        // null CWinApp, as in the unit tests and any command-line use), there is no
        // main window; before the fix OutputResults dereferenced _pApp->m_pMainWnd
        // and faulted with C0000005 (seen loading Space Quest VI audio under ASan).
        // The Output* methods must now no-op without a GUI. (#180)
        TEST_METHOD(OutputPane_Headless_DoesNotCrash)
        {
            std::vector<CompileResult> results;
            results.emplace_back("a conversion warning", CompileResult::CRT_Warning);
            appState->OutputResults(OutputPaneType::Compile, results);
            appState->ShowOutputPane(OutputPaneType::Compile);
            appState->OutputClearResults(OutputPaneType::Compile);
            appState->OutputAddBatch(OutputPaneType::Compile, results);
            appState->OutputFinishAdd(OutputPaneType::Compile);
            Assert::IsTrue(true, L"the output-pane methods returned without crashing headless");
        }

        // The other GUI entry points that reach the main-frame window must also
        // tolerate a headless AppState (null _pApp). They are latent siblings of
        // the output-pane crash: reachable from non-GUI code and any future
        // command-line tool. Each now no-ops without a GUI instead of
        // dereferencing _pApp->m_pMainWnd. (#180)
        TEST_METHOD(HeadlessGuiEntryPoints_DoNotCrash)
        {
            appState->OpenScriptAtLine(ScriptId(), 1);
            appState->OpenMostRecentResource(ResourceType::View, 0);
            appState->ReopenScriptDocument(0);
            appState->OpenMostRecentResourceAt(ResourceType::Vocab, 0, 0);
            appState->NotifyChangeShowTabs();
            appState->NotifyChangeAspectRatio();
            Assert::IsTrue(true, L"the GUI entry points returned without crashing headless");
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

        // Writes GIF bytes to a temp file for the loader tests below; the caller
        // deletes it. (#41, giflib 5.2.2 re-vendor.)
        std::string WriteTempGif(const std::vector<uint8_t> &bytes, const char *tag)
        {
            char tempDir[MAX_PATH] = { 0 };
            GetTempPathA(ARRAYSIZE(tempDir), tempDir);
            std::string path = std::string(tempDir) + "scic_gif_" + tag + ".gif";
            ScopedFile f(path, GENERIC_WRITE, 0, CREATE_ALWAYS);
            if (!bytes.empty())
            {
                f.Write(bytes.data(), (uint32_t)bytes.size());
            }
            return path;
        }

        // (#41) A decodable GIF still loads after the giflib 5.2.2 update: a
        // canonical 1x1 GIF89a, which exercises the LZW decode path in dgif_lib.
        TEST_METHOD(GetCelsAndPaletteFromGIFFile_ValidGif_Succeeds)
        {
            const std::vector<uint8_t> gif = {
                0x47,0x49,0x46,0x38,0x39,0x61,                 // "GIF89a"
                0x01,0x00, 0x01,0x00, 0x80, 0x00, 0x00,        // screen 1x1, global table of 2 colors
                0x00,0x00,0x00,  0xFF,0xFF,0xFF,               // color table: black, white
                0x21,0xF9,0x04,0x01,0x00,0x00,0x00,0x00,       // graphic control extension
                0x2C,0x00,0x00,0x00,0x00, 0x01,0x00, 0x01,0x00, 0x00, // image descriptor 1x1
                0x02,0x02,0x44,0x01,0x00,                      // LZW: min code size 2, one sub-block
                0x3B                                           // trailer
            };
            std::string path = WriteTempGif(gif, "valid");
            std::vector<Cel> cels;
            std::vector<PaletteComponent> palettes;
            PaletteComponent globalPalette;
            bool ok = GetCelsAndPaletteFromGIFFile(path.c_str(), cels, palettes, globalPalette);
            DeleteFileA(path.c_str());
            Assert::IsTrue(ok, L"a valid 1x1 GIF must decode after the giflib 5.2.2 update");
            Assert::AreEqual((size_t)1, cels.size(), L"one image yields one cel");
            Assert::AreEqual(1, (int)cels[0].size.cx, L"decoded cel width");
            Assert::AreEqual(1, (int)cels[0].size.cy, L"decoded cel height");
        }

        // (#41) The vendored giflib 5.1.1 had a DEAD DGifSlurp overflow guard
        // (Width < 0 && Height < 0, never true for unsigned-parsed dimensions), so
        // an image descriptor whose Width*Height overflows int reached the
        // allocation. The 5.2.2 guard (Width <= 0 || Height <= 0 ||
        // Width > INT_MAX/Height) rejects it, so the loader returns false instead
        // of over-allocating or overflowing.
        TEST_METHOD(GetCelsAndPaletteFromGIFFile_OversizeDimensions_ReturnsFalse)
        {
            const std::vector<uint8_t> gif = {
                0x47,0x49,0x46,0x38,0x39,0x61,                 // "GIF89a"
                0x01,0x00, 0x01,0x00, 0x00, 0x00, 0x00,        // screen 1x1, no global table
                0x2C,0x00,0x00,0x00,0x00, 0xFF,0xFF, 0xFF,0xFF, 0x00, // image descriptor 65535x65535
                0x08,                                          // LZW min code size: makes the image
                                                               // header parse fully, so DGifSlurp
                                                               // reaches the Width*Height guard rather
                                                               // than failing earlier on truncation
            };
            std::string path = WriteTempGif(gif, "oversize");
            std::vector<Cel> cels;
            std::vector<PaletteComponent> palettes;
            PaletteComponent globalPalette;
            bool ok = GetCelsAndPaletteFromGIFFile(path.c_str(), cels, palettes, globalPalette);
            DeleteFileA(path.c_str());
            Assert::IsFalse(ok, L"a GIF whose image dimensions overflow must be rejected, not allocated");
        }

        // (#41) Round-trip: export cels+palette to a GIF and read them back, to
        // exercise the updated egif_lib (encode, plus the re-applied SColorMap
        // leak fix) together with dgif_lib.
        TEST_METHOD(SaveThenLoadGIF_RoundTrips)
        {
            char tempDir[MAX_PATH] = { 0 };
            GetTempPathA(ARRAYSIZE(tempDir), tempDir);
            std::string path = std::string(tempDir) + "scic_gif_roundtrip.gif";

            Cel cel(size16(4, 4), point16(0, 0), (uint8_t)0xFF);
            cel.Data.allocate(cel.GetDataSize());
            for (size_t i = 0; i < cel.Data.size(); i++)
            {
                cel.Data[i] = (uint8_t)(i & 1);
            }
            std::vector<Cel> cels{ cel };

            RGBQUAD colors[2] = {};
            colors[1].rgbRed = 255; colors[1].rgbGreen = 255; colors[1].rgbBlue = 255;
            uint8_t paletteMapping[2] = { 0, 1 };

            SaveCelsAndPaletteToGIFFile(path.c_str(), cels, 2, colors, paletteMapping,
                (uint8_t)0xFF, GIFConfiguration());

            std::vector<Cel> loadedCels;
            std::vector<PaletteComponent> palettes;
            PaletteComponent globalPalette;
            bool ok = GetCelsAndPaletteFromGIFFile(path.c_str(), loadedCels, palettes, globalPalette);
            DeleteFileA(path.c_str());
            Assert::IsTrue(ok, L"a GIF written by the app must read back after the update");
            Assert::AreEqual((size_t)1, loadedCels.size(), L"one cel round-trips");
            Assert::AreEqual(4, (int)loadedCels[0].size.cx, L"width round-trips");
            Assert::AreEqual(4, (int)loadedCels[0].size.cy, L"height round-trips");
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
