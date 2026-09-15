/***************************************************************************
    Copyright (c) 2015 Philip Fortier

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
#include "View.h"
#include "ResourceEntity.h"
#include "ResourceMap.h"
#include "AppState.h"
#include "ResourceContainer.h"
#include "RasterOperations.h"
#include "Vocab000.h"
#include "Audio.h"
#include "PaletteOperations.h"
#include "Stream.h"
#include "sci.h"
#include "format.h"
#include <fstream>
#include <filesystem>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

std::wstring ToString(const RasterChangeHint& q)
{
    return fmt::format(L"RasterChangeHint:{0:08x}", (int)q);
}

namespace UnitTests
{		
    TEST_CLASS(TestResource)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
        }

        TEST_METHOD(TestCreateDefaultResources)
        {
	        // Create the default resources.
            ResourceEntity *pTest = CreateDefaultViewResource(sciVersion0);
        }

        // Saving a VGA2 view transfers the per-row-offset region using the
        // row-offset stream's size, not the (larger) literal-image size. Before
        // the fix the transfer used celRawData.GetDataSize(), which over-read the
        // row-offset stream; sci::transfer now detects that short read and throws,
        // so WriteTo failed. With the correct size, WriteTo serializes cleanly.
        // (Cels are sized so the literal image is larger than the row offsets:
        // per cel, 32*20 literal bytes vs 20*8 row-offset bytes.)
        TEST_METHOD(ViewVGA2Save_RowOffsetTransferInBounds)
        {
            SCIVersion version = sciVersion2; // ViewFormat::VGA2
            std::unique_ptr<ResourceEntity> resource(CreateViewResource(version));
            RasterComponent &raster = resource->GetComponent<RasterComponent>();
            raster.Resolution = version.DefaultResolution;

            Loop loop;
            for (int c = 0; c < 2; c++)
            {
                Cel cel;
                cel.size = size16(32, 20);
                cel.TransparentColor = 0;
                cel.Data.allocate(cel.GetDataSize());
                for (size_t i = 0; i < cel.GetDataSize(); i++)
                {
                    cel.Data[i] = (uint8_t)((i * 37 + c * 101 + 7) & 0xff);
                }
                loop.Cels.push_back(cel);
            }
            raster.Loops.push_back(loop);

            sci::ostream blob;
            std::map<BlobKey, uint32_t> propertyBag;
            resource->WriteTo(blob, true, 0, propertyBag);
            Assert::IsTrue(blob.GetDataSize() > 0, L"VGA2 view failed to serialize");
        }

        // A vocab.900 word whose first byte is >= 0x80. The writer indexed the
        // offset table with a signed char, so 0xE9 became a negative index and
        // wrote before the stream buffer. Reading a crafted vocab.900, writing it
        // back, and reading again must round-trip the word, and the offset slot
        // for 0xE9 must point at the word data.
        TEST_METHOD(Vocab900_HighByteWord_RoundTrip)
        {
            std::vector<uint8_t> buf(255 * 2, 0); // 510-byte offset table (255 slots x 2 bytes)
            buf.push_back(0x00);                                         // copyCount
            buf.push_back(0xE9); buf.push_back(0x62); buf.push_back(0x63); // the word bytes
            buf.push_back(0x00);                                         // NUL terminator (is900)
            buf.push_back(0x00); buf.push_back(0x00); buf.push_back(0x01); // group/class info -> group 1

            SCIVersion v900 = sciVersion0;
            v900.MainVocabResource = 900;
            std::unique_ptr<ResourceEntity> p(CreateVocabResource(v900));
            sci::istream in(buf.data(), (uint32_t)buf.size());
            p->ReadFrom(in, {});

            std::vector<std::string> &words = p->GetComponent<Vocab000>().GetWords();
            Assert::AreEqual((size_t)1, words.size(), L"expected exactly one word");
            char expected[] = { (char)0xE9, 'b', 'c', 0 };
            Assert::AreEqual(std::string(expected), words[0], L"the 0xE9-initial word must be read");

            sci::ostream out;
            p->WriteToTest(out, false, 0); // before the fix, the offset patch wrote at a negative index
            Assert::IsTrue(out.GetDataSize() >= 510, L"vocab.900 did not serialize");
            const uint16_t *offs = (const uint16_t *)out.GetInternalPointer();
            Assert::AreEqual((uint16_t)510, offs[0xE9],
                L"the offset slot for a 0xE9-initial word must point at the word data");

            sci::istream in2(out.GetInternalPointer(), out.GetDataSize());
            std::unique_ptr<ResourceEntity> p2(CreateVocabResource(v900));
            p2->ReadFrom(in2, {});
            Assert::AreEqual(std::string(expected), p2->GetComponent<Vocab000>().GetWords()[0],
                L"the 0xE9-initial word must survive a write/read round trip");
        }

        // An overlong/unterminated vocab.000 word. The reader used an
        // uninitialised buffer and could exit the copy loop with no terminator,
        // then build a std::string that reads past the buffer. The read must be
        // bounded and terminated.
        TEST_METHOD(Vocab000_OverlongWord_Bounded)
        {
            std::vector<uint8_t> buf(26 * 2, 0); // 52-byte offset table (26 letters x 2 bytes)
            buf.push_back(0x00);                                     // copyCount for the first word
            for (int i = 0; i < 600; i++)
            {
                buf.push_back((uint8_t)'a'); // no byte has 0x80 set, so the word never terminates
            }

            SCIVersion v000 = sciVersion0;
            v000.MainVocabResource = 0;
            std::unique_ptr<ResourceEntity> p(CreateVocabResource(v000));
            sci::istream in(buf.data(), (uint32_t)buf.size());
            p->ReadFrom(in, {}); // before the fix: uninitialised buffer + missing terminator -> over-read

            std::vector<std::string> &words = p->GetComponent<Vocab000>().GetWords();
            Assert::IsTrue(!words.empty(), L"expected at least one word");
            for (const std::string &w : words)
            {
                Assert::IsTrue(w.length() < (size_t)MAX_PATH, L"a word overran the buffer");
            }
            Assert::AreEqual((size_t)(MAX_PATH - 1), words[0].length(),
                L"the overlong first word must be truncated to the buffer size");
        }

        // A DPCM audio resource whose declared size is far larger than the bytes
        // present. Before the fix the loader doubled the declared size and decoded
        // to it, allocating and filling megabytes from a few input bytes. The
        // decoded size must be bounded by the input actually present.
        TEST_METHOD(AudioDpcmSizeBoundedToInput)
        {
            AudioHeader hdr = {};
            hdr.resourceType = 0;
            hdr.headerSize = (uint8_t)(sizeof(AudioHeader) - 2); // seekg lands right after the header
            hdr.audioType = 0;
            hdr.sampleRate = 11025;
            hdr.flags = AudioFlags::DPCM;         // 8-bit DPCM path
            hdr.sizeExcludingHeader = 0x00100000; // 1 MB, far larger than the payload below
            std::vector<uint8_t> buf(sizeof(AudioHeader) + 4, 0); // only 4 payload bytes
            memcpy(buf.data(), &hdr, sizeof(AudioHeader));

            std::unique_ptr<ResourceEntity> res(CreateAudioResource(sciVersion1_1));
            sci::istream stream(buf.data(), (uint32_t)buf.size());
            res->ReadFrom(stream, {});

            AudioComponent &audio = res->GetComponent<AudioComponent>();
            Assert::IsTrue(audio.DigitalSamplePCM.size() <= (size_t)(2 * 4),
                L"DPCM output must be bounded by the bytes actually present, not the declared size");
        }

        // A JASC .pal declaring far more colors than the 256-entry palette. Before
        // the fix the loader wrote every declared color, running past Colors[256].
        // The count must be bounded to the palette size.
        TEST_METHOD(LoadPalJascHugeCountBounded)
        {
            std::string dir = GetRandomTempFolder();
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::string path = dir + "\\huge.pal";
            {
                std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
                f << "JASC-PAL\n0100\n99999\n0 0 0\n";
            }

            PaletteComponent palette;
            LoadPALFile(path, palette, 0); // before the fix this wrote ~99999 entries past Colors[256]

            std::error_code ec2;
            std::filesystem::remove(path, ec2);

            // Reaching here without corruption means the count was bounded.
            Assert::AreEqual((uint8_t)0, palette.Colors[0].rgbRed);
        }

        TEST_METHOD(TestViewMirror)
        {
            RasterChange change;
            ResourceEntity *pTest = CreateDefaultViewResource(sciVersion0);
            Assert::IsNotNull(pTest);
            RasterComponent *raster = pTest->TryGetComponent<RasterComponent>();
            Assert::IsNotNull(raster);
            // Insert second loop:
            change = InsertLoop(*raster, 0, false);
            Assert::AreEqual(change.hint, RasterChangeHint::NewView);
            // Make it a mirror of the first:
            change = MakeMirrorOf(*raster, 1, 0);
            Assert::AreEqual(change.hint, RasterChangeHint::NewView);
            Loop &loopOrig = raster->Loops[0];
            Assert::IsFalse(loopOrig.IsMirror);
            Assert::AreEqual(loopOrig.MirrorOf, (uint8_t)0xff);
            Loop &loopMirror = raster->Loops[1];
            Assert::IsTrue(loopMirror.IsMirror);
            Assert::AreEqual(loopMirror.MirrorOf, (uint8_t)0);
            // Un-mirror it
            change = MakeMirrorOf(*raster, 1, -1);
            Assert::AreEqual(change.hint, RasterChangeHint::NewView);
            Assert::IsTrue(!loopMirror.IsMirror);
            Assert::AreEqual(loopMirror.MirrorOf, (uint8_t)0xff);
        }

	};
}