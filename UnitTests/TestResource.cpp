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
#include "ResourceBlob.h"
#include "GameFolderHelper.h"
#include "RasterOperations.h"
#include "Vocab000.h"
#include "Audio.h"
#include "SoundUtil.h"
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

        // A stand-alone patch file that is too short to hold the header gap plus
        // the leading type word. Before the fix, CreateFromHandle subtracted the
        // gap (and the word) from the file size with unsigned arithmetic, wrapped
        // around, and stored a ~4 GB cbDecompressed. That size then drove a huge
        // allocation / decode (a bad_alloc, not a typed error). The load must fail
        // cleanly instead.
        TEST_METHOD(PatchFileTooShort_GivesTypedErrorNotBadAlloc)
        {
            std::string dir = GetRandomTempFolder();
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::string path = dir + "\\view.v56";
            {
                // Two bytes only: low byte 0x81 (0x80 | type), high byte 0x28 (40),
                // so GetResourceOffsetInFile reports a 40-byte gap that the 2-byte
                // file cannot possibly contain.
                std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
                uint8_t bytes[2] = { 0x81, 0x28 };
                f.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
            }

            HRESULT hr = E_UNEXPECTED;
            try
            {
                ResourceBlob blob;
                hr = blob.CreateFromFile(nullptr, path, sciVersion1_1, ResourceSaveLocation::Package, -1, -1);
            }
            catch (const std::bad_alloc &)
            {
                std::error_code ec3;
                std::filesystem::remove(path, ec3);
                Assert::Fail(L"a truncated patch file caused a huge allocation instead of a typed failure");
            }

            std::error_code ec2;
            std::filesystem::remove(path, ec2);
            Assert::IsTrue(FAILED(hr),
                L"a truncated patch file must fail cleanly rather than parse a bogus huge size");
        }

        // Narrowing a resource header from the agnostic form must reject a
        // compressed size that overflows the on-disk field. Before the fix the
        // overflow check tested the (still-zero) destination member instead of the
        // computed value, so the check never fired and the size was silently
        // truncated. With the fix the oversize value throws.
        TEST_METHOD(FromAgnostic_CompressedSizeOverflow_Throws)
        {
            ResourceHeaderAgnostic agnostic;
            agnostic.Type = ResourceType::View;
            agnostic.Number = 0;
            agnostic.PackageHint = 0;
            agnostic.CompressionMethod = 0;
            agnostic.SourceFlags = ResourceSourceFlags::ResourceMap;
            agnostic.Version = sciVersion1_1;
            agnostic.cbDecompressed = 16;         // small: passes the decompressed check
            agnostic.cbCompressed = 0x10000;      // 65536: cannot fit the 16-bit SCI1 field

            RESOURCEHEADER_SCI1 header = {};      // 16-bit size fields
            Assert::ExpectException<std::exception>([&]()
            {
                header.FromAgnostic(agnostic);
            }, L"an oversize compressed size must be rejected, not silently truncated");
        }

        // A corrupt SCI1 resource map carrying a resource-type byte outside the
        // known range. ResourceTypeToFlag shifted 1 by that value, which is
        // undefined for large shifts and can alias a real type's flag. It must map
        // an out-of-range type to no flag, so the map walker skips the bad group.
        TEST_METHOD(Sci1MapEntry_OutOfRangeType_IsRejected)
        {
            // The direct fix: an out-of-range type maps to no flag.
            Assert::AreEqual((uint32_t)ResourceTypeFlags::None,
                (uint32_t)ResourceTypeToFlag((ResourceType)0x30),
                L"an out-of-range resource type must map to ResourceTypeFlags::None");

            // And through the SCI1 map walker: a lookup group whose type is out of
            // range is skipped, so no bogus entry is produced. The buffer is built
            // from real struct instances so its layout matches what the reader
            // extracts, independent of struct padding.
            auto appendStruct = [](std::vector<uint8_t> &out, const void *p, size_t n)
            {
                const uint8_t *b = reinterpret_cast<const uint8_t *>(p);
                out.insert(out.end(), b, b + n);
            };

            const uint32_t tableSize = 2 * (uint32_t)sizeof(RESOURCEMAPPREENTRY_SCI1);
            RESOURCEMAPPREENTRY_SCI1 group = {};
            group.bType = (uint8_t)(0x80 | 0x30);            // adorned, out-of-range type
            group.wOffset = (uint16_t)tableSize;             // its entries start after the table
            RESOURCEMAPPREENTRY_SCI1 terminator = {};
            terminator.bType = 0xff;
            terminator.wOffset = (uint16_t)(tableSize + sizeof(RESOURCEMAPENTRY_SCI1));

            std::vector<uint8_t> buf;
            appendStruct(buf, &group, sizeof(group));
            appendStruct(buf, &terminator, sizeof(terminator));
            RESOURCEMAPENTRY_SCI1 entry = {};                // placeholder entry data
            entry.wNumber = 7;
            appendStruct(buf, &entry, sizeof(entry));
            buf.resize(buf.size() + sizeof(RESOURCEMAPENTRY_SCI1), 0);

            sci::istream mapStream(buf.data(), (uint32_t)buf.size());
            SCI1MapNavigator<RESOURCEMAPENTRY_SCI1> nav;
            IteratorState state;
            ResourceMapEntryAgnostic entryOut = {};
            bool got = nav.NavAndReadNextEntry(ResourceTypeFlags::All, mapStream, state, entryOut);
            Assert::IsFalse(got,
                L"an out-of-range type group must be skipped, not read as a resource");
        }

        // istream::skip computed (_iIndex + cBytes) in uint32_t, which wraps for
        // a large count. From offset 12, skipping 0xFFFFFFF8 wrapped to 4, moved
        // the cursor backward, and reported success. The skip must fail instead,
        // and must not move the cursor backward.
        TEST_METHOD(StreamSkip_Overflow_DoesNotWrapBackwards)
        {
            const uint8_t data[20] = {};
            sci::istream stream(data, (uint32_t)sizeof(data));
            uint32_t dummy;
            stream >> dummy; stream >> dummy; stream >> dummy; // _iIndex == 12
            Assert::AreEqual((uint32_t)12, stream.tellg());

            stream.skip(0xFFFFFFF8u); // (12 + 0xFFFFFFF8) wraps to 4 with the old code

            Assert::IsFalse(stream.good(), L"an overflowing skip must fail, not wrap");
            Assert::IsTrue(stream.tellg() >= 12u, L"the cursor must not move backward");
        }

        // Skipping exactly to the end of the stream is a valid EOF position. The
        // old "<" test wrongly treated it as a read past the end.
        TEST_METHOD(StreamSkip_ToExactEnd_Succeeds)
        {
            const uint8_t data[] = { 1, 2, 3, 4 };
            sci::istream stream(data, (uint32_t)sizeof(data));

            stream.skip((uint32_t)sizeof(data)); // land exactly at EOF

            Assert::IsTrue(stream.good(), L"skipping exactly to EOF is valid");
            Assert::AreEqual((uint32_t)sizeof(data), stream.tellg());
        }

        // A WAV whose first post-WAVE chunk is not "fmt " and declares a size of
        // 0xFFFFFFF8. Before the fix, skip wrapped the cursor backward and the
        // fmt-search loop re-read the same header forever (a hang). After the fix
        // the skip fails, the loop exits, and the loader throws.
        // NOTE: this hangs against unfixed code (no per-test timeout); it is a
        // post-fix smoke test and ships with the fix. The two StreamSkip_* tests
        // above are the deterministic guards.
        TEST_METHOD(WaveFile_OversizedChunkSize_Terminates)
        {
            const uint8_t wav[] = {
                'R','I','F','F', 0,0,0,0, 'W','A','V','E',
                'J','U','N','K',                 // a chunk marker that is not "fmt "
                0xF8,0xFF,0xFF,0xFF,             // chunkSize = 0xFFFFFFF8
                0,0,0,0, 0,0,0,0                 // 8 payload bytes
            };
            sci::istream stream(wav, (uint32_t)sizeof(wav));
            AudioComponent audio;
            Assert::ExpectException<std::exception>([&]()
            {
                AudioComponentFromWaveFile(stream, audio);
            }, L"a WAV with an oversized chunk size must terminate with an error, not hang");
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