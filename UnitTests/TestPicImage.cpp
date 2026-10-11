#include "stdafx.h"
#include "CppUnitTest.h"
#include "TestSupport.h"
#include "Helper.h"
#include "GameFolderHelper.h"
#include "IndexedBmp.h"
#include "PaletteOperations.h"
#include "Pic.h"
#include "PicImage.h"
#include "ResourceBlob.h"
#include "ResourceSourceFlags.h"
#include <numeric>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    namespace
    {
        // The pic files of the pic tests (UnitTests\Files\Pics).
        const char *const Sci2Pic220 = "SCI2\\220.p56";        // 640 x 480, 4 cels, palette entries 0 to 254
        const char *const Sci2Pic1150 = "SCI2\\1150.p56";      // 640 x 480, 5 cels, palette entries 104 to 235
        const char *const Sci2Pic901 = "SCI2\\901.p56";        // 320 x 200, 8 compressed cels
        const char *const Sci11Pic100 = "SCI1.1\\100.p56";     // one cel of 320 x 199, 256 palette entries of 4 bytes

        // The layout of an SCI2 pic: a header of 14 bytes, then cel headers
        // of 42 bytes.
        const size_t PaletteOffsetField = 6;
        const size_t CelHeadersAt = 14;
        const size_t CelHeaderSize = 42;
        const size_t CelWidthField = 0;
        const size_t CelControlOffsetField = 24;
        // A palette has a header of 37 bytes before its entries.
        const size_t PaletteHeaderSize = 37;
        const size_t PaletteFirstEntryField = 25;
        const size_t PaletteEntryCountField = 29;
        const size_t PaletteFormatField = 32;

        // Where pic 220 has its parts.
        const size_t Pic220PaletteOffset = 188;
        const size_t Pic220Cel0Offset = 996;
        const size_t Pic220Cel1Offset = 214756;
        const size_t Pic220Cel2Offset = 218160;
        const size_t Pic220Cel2Size = 30 * 74;
        // Where pic 100 of SCI1.1 has its palette.
        const size_t Pic100PaletteOffset = 57139;

        std::string PicPath(const char *name)
        {
            return GetTestFileDirectory("Pics") + "\\" + name;
        }

        // The data of a pic patch file: the bytes after its patch header.
        std::vector<uint8_t> PicData(const char *name, const SCIVersion &version)
        {
            ResourceBlob blob;
            HRESULT hr = blob.CreateFromFile("pic", PicPath(name), version, ResourceSaveLocation::Patch, version.DefaultVolumeFile);
            Assert::IsTrue(SUCCEEDED(hr), Wide("reading " + PicPath(name)).c_str());
            return std::vector<uint8_t>(blob.GetData(), blob.GetData() + blob.GetLength());
        }

        void SetU16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value)
        {
            bytes[offset] = (uint8_t)(value & 0xff);
            bytes[offset + 1] = (uint8_t)(value >> 8);
        }

        void SetU32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
        {
            SetU16(bytes, offset, (uint16_t)(value & 0xffff));
            SetU16(bytes, offset + 2, (uint16_t)(value >> 16));
        }

        size_t CelField(int cel, size_t field)
        {
            return CelHeadersAt + ((size_t)cel * CelHeaderSize) + field;
        }

        // The offsets where the two vectors (of one size) are different.
        std::vector<size_t> ChangedOffsets(const std::vector<uint8_t> &before, const std::vector<uint8_t> &after)
        {
            Assert::AreEqual(before.size(), after.size(), L"the size of the data stays");
            std::vector<size_t> changed;
            for (size_t i = 0; i < before.size(); i++)
            {
                if (before[i] != after[i])
                {
                    changed.push_back(i);
                }
            }
            return changed;
        }

        // Each pixel with another value.
        std::vector<uint8_t> OtherPixels(const std::vector<uint8_t> &pixels)
        {
            std::vector<uint8_t> other = pixels;
            for (uint8_t &pixel : other)
            {
                pixel ^= 1;
            }
            return other;
        }

        // Colours that no pic of the tests has: entry i is (i, 255 - i, 77).
        std::array<IndexedBmpColor, 256> NewColors()
        {
            std::array<IndexedBmpColor, 256> colors = {};
            for (size_t i = 0; i < colors.size(); i++)
            {
                colors[i].red = (uint8_t)i;
                colors[i].green = (uint8_t)(255 - i);
                colors[i].blue = 77;
            }
            return colors;
        }

        template<typename R>
        void AssertFails(const R &result, sci::ErrorCode code, const std::string &textInMessage)
        {
            Assert::IsFalse(result.has_value(), Wide("the call must fail: " + textInMessage).c_str());
            Assert::IsTrue(result.error().code == code, Wide(result.error().ToString()).c_str());
            Assert::IsTrue(result.error().message.find(textInMessage) != std::string::npos, Wide(result.error().ToString()).c_str());
        }

        uint32_t Sum(const std::array<uint32_t, 256> &counts)
        {
            return std::accumulate(counts.begin(), counts.end(), (uint32_t)0);
        }

        // How many pixels of a row of the cel are the pixels of a row of the
        // image at the same x. A pixel of the skip colour is not counted in
        // "compared".
        size_t MatchingPixels(const std::vector<uint8_t> &cel, int celWidth, int celRow, const IndexedBmp &image, int imageRow, uint8_t skipColor, size_t &compared)
        {
            size_t matching = 0;
            compared = 0;
            for (int x = 0; x < celWidth; x++)
            {
                uint8_t pixel = cel[((size_t)celRow * celWidth) + x];
                if (pixel == skipColor)
                {
                    continue;
                }
                compared++;
                if (pixel == image.pixels[((size_t)imageRow * image.width) + x])
                {
                    matching++;
                }
            }
            return matching;
        }
    }

    // The image data of VGA pics (PicImage.h), and the layout and patch
    // functions under it (Pic.h, PaletteOperations.h).
    TEST_CLASS(TestPicImage)
    {
    public:
        TEST_METHOD(DescribePic_Sci2_GivesTheCelsAndThePalette)
        {
            sci::Result<PicDescription> pic = DescribePic(sciVersion2, PicData(Sci2Pic220, sciVersion2));

            AssertOk(pic, "describe");
            Assert::IsTrue(pic->format == PicFormat::VGA2);
            Assert::AreEqual(640, pic->width);
            Assert::AreEqual(480, pic->height);
            Assert::AreEqual((size_t)4, pic->cels.size());
            Assert::AreEqual(0, pic->backgroundCel);

            const PicCelDescription &background = pic->cels[0];
            Assert::AreEqual(0, background.index);
            Assert::AreEqual(640, background.width);
            Assert::AreEqual(334, background.height);
            Assert::AreEqual(0, background.x);
            Assert::AreEqual(0, background.y);
            Assert::AreEqual(0, background.priority);
            Assert::AreEqual(255, (int)background.skipColor);
            Assert::IsFalse(background.compressed);

            const PicCelDescription &overlay = pic->cels[1];
            Assert::AreEqual(1, overlay.index);
            Assert::AreEqual(46, overlay.width);
            Assert::AreEqual(74, overlay.height);
            Assert::AreEqual(113, overlay.x);
            Assert::AreEqual(0, overlay.y);
            Assert::AreEqual(40, overlay.priority);

            Assert::IsTrue(pic->palette.present);
            Assert::AreEqual(0, pic->palette.firstEntry);
            Assert::AreEqual(255, pic->palette.entryCount);
            Assert::AreEqual(3, pic->palette.entrySize);
            // The first entries of the palette of pic 220.
            Assert::IsTrue(IndexedBmpColor{ 16, 0, 0 } == pic->palette.colors[0]);
            Assert::IsTrue(IndexedBmpColor{ 40, 25, 25 } == pic->palette.colors[1]);
            Assert::IsTrue(IndexedBmpColor{ 65, 49, 49 } == pic->palette.colors[2]);
            // A 3-byte entry counts as used; entry 255 is not stored.
            Assert::AreEqual(1, (int)pic->palette.flags[0]);
            Assert::AreEqual(1, (int)pic->palette.flags[254]);
            Assert::AreEqual(0, (int)pic->palette.flags[255]);
            Assert::IsTrue(IndexedBmpColor() == pic->palette.colors[255]);
        }

        TEST_METHOD(DescribePic_Sci2_CountsThePixelsOfEachCel)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            sci::Result<PicDescription> pic = DescribePic(sciVersion2, data);

            AssertOk(pic, "describe");
            for (const PicCelDescription &cel : pic->cels)
            {
                Assert::AreEqual((uint32_t)(cel.width * cel.height), Sum(cel.pixelCounts), Wide("cel " + std::to_string(cel.index)).c_str());
            }
            // Cel 2 against a count of its bytes in the data.
            std::array<uint32_t, 256> expected = {};
            for (size_t i = 0; i < Pic220Cel2Size; i++)
            {
                expected[data[Pic220Cel2Offset + i]]++;
            }
            Assert::IsTrue(expected == pic->cels[2].pixelCounts, L"the counts of cel 2");
            Assert::AreEqual(expected[255], pic->cels[2].SkipPixels());
            for (size_t i = 0; i < expected.size(); i++)
            {
                Assert::AreEqual(expected[i] != 0, pic->cels[2].UsedEntries()[i], Wide("entry " + std::to_string(i)).c_str());
            }
        }

        TEST_METHOD(DescribePic_Sci2_PaletteThatStoresARange)
        {
            sci::Result<PicDescription> pic = DescribePic(sciVersion2, PicData(Sci2Pic1150, sciVersion2));

            AssertOk(pic, "describe");
            Assert::AreEqual((size_t)5, pic->cels.size());
            Assert::AreEqual(104, pic->palette.firstEntry);
            Assert::AreEqual(132, pic->palette.entryCount);
            Assert::AreEqual(3, pic->palette.entrySize);
            std::vector<IndexRun> used = IndexRunsOf(pic->palette.UsedEntries());
            Assert::AreEqual((size_t)1, used.size());
            Assert::IsTrue(IndexRun{ 104, 235 } == used[0]);
            Assert::IsTrue(IndexedBmpColor() == pic->palette.colors[103]);
            Assert::IsTrue(IndexedBmpColor() == pic->palette.colors[236]);
        }

        TEST_METHOD(DescribePic_Sci2_CompressedCels)
        {
            sci::Result<PicDescription> pic = DescribePic(sciVersion2, PicData(Sci2Pic901, sciVersion2));

            AssertOk(pic, "describe");
            // The header of the pic has no size: a game of 320 x 200.
            Assert::AreEqual(320, pic->width);
            Assert::AreEqual(200, pic->height);
            Assert::AreEqual((size_t)8, pic->cels.size());
            Assert::AreEqual(0, pic->backgroundCel);
            for (const PicCelDescription &cel : pic->cels)
            {
                Assert::IsTrue(cel.compressed, Wide("cel " + std::to_string(cel.index)).c_str());
                Assert::AreEqual((uint32_t)0, Sum(cel.pixelCounts), Wide("cel " + std::to_string(cel.index)).c_str());
            }
            Assert::AreEqual(320, pic->cels[0].width);
            Assert::AreEqual(124, pic->cels[0].height);
            Assert::AreEqual(22, pic->cels[0].y);
            Assert::AreEqual(-1000, pic->cels[0].priority);
            Assert::IsTrue(pic->palette.present);
        }

        // Cel 0 gets a width of 1, so cel 3 (264 x 122) is the largest.
        TEST_METHOD(DescribePic_BackgroundCelIsTheLargest)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
            SetU16(data, CelField(0, CelWidthField), 1);

            sci::Result<PicDescription> pic = DescribePic(sciVersion2, data);

            AssertOk(pic, "describe");
            Assert::AreEqual(3, pic->backgroundCel);
        }

        TEST_METHOD(DescribePic_Sci2_NoPalette)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
            SetU32(data, PaletteOffsetField, 0);

            sci::Result<PicDescription> pic = DescribePic(sciVersion2, data);

            AssertOk(pic, "describe");
            Assert::IsFalse(pic->palette.present);
            Assert::AreEqual(0, pic->palette.entryCount);
            AssertFails(ReplacePaletteRange(sciVersion2, data, 10, 12, NewColors()), sci::ErrorCode::Usage, "no palette");
        }

        TEST_METHOD(GetCelPixels_Sci2_GivesTheBytesOfTheCel)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(sciVersion2, data, 1);

            AssertOk(pixels, "pixels");
            std::vector<uint8_t> expected(data.begin() + Pic220Cel1Offset, data.begin() + Pic220Cel1Offset + (46 * 74));
            Assert::IsTrue(expected == *pixels);
        }

        // The image of the pic that the pic tests check (220.p56-vis.bmp)
        // has cel 0 at its top left. Rows 200 and 333 of the cel are below
        // the other cels, which are at most 122 rows high. The same rows of
        // the cel in the other direction are not the rows of the image.
        TEST_METHOD(GetCelPixels_Sci2_FirstRowIsTheTopRow)
        {
            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(PicPath("SCI2\\220.p56-vis.bmp"));
            AssertOk(image, "the image of the pic");

            sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(sciVersion2, PicData(Sci2Pic220, sciVersion2), 0);

            AssertOk(pixels, "pixels");
            for (int row : { 200, 333 })
            {
                size_t compared = 0;
                size_t matching = MatchingPixels(*pixels, 640, row, *image, row, 255, compared);
                Assert::IsTrue(compared > 600, Wide("row " + std::to_string(row) + " has pixels to compare").c_str());
                Assert::AreEqual(compared, matching, Wide("row " + std::to_string(row)).c_str());
                size_t flipped = MatchingPixels(*pixels, 640, 333 - row, *image, row, 255, compared);
                Assert::IsTrue(flipped < (compared / 2), Wide("row " + std::to_string(333 - row) + " is not row " + std::to_string(row) + " of the image").c_str());
            }
        }

        TEST_METHOD(GetCelPixels_BadCel_IsUsage)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            AssertFails(GetCelPixels(sciVersion2, data, 4), sci::ErrorCode::Usage, "no cel 4; it has 4 cels");
            AssertFails(GetCelPixels(sciVersion2, data, -1), sci::ErrorCode::Usage, "no cel -1");
        }

        TEST_METHOD(GetCelPixels_CompressedCel_IsUnsupported)
        {
            AssertFails(GetCelPixels(sciVersion2, PicData(Sci2Pic901, sciVersion2), 0), sci::ErrorCode::Unsupported, "cel 0 is compressed");
        }

        TEST_METHOD(ReplaceCelPixels_Sci2_OwnPixels_GivesIdenticalBytes)
        {
            for (const char *name : { Sci2Pic220, Sci2Pic1150 })
            {
                std::vector<uint8_t> data = PicData(name, sciVersion2);
                sci::Result<PicDescription> pic = DescribePic(sciVersion2, data);
                AssertOk(pic, name);
                for (const PicCelDescription &cel : pic->cels)
                {
                    sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(sciVersion2, data, cel.index);
                    AssertOk(pixels, name);

                    sci::Result<std::vector<uint8_t>> replaced = ReplaceCelPixels(sciVersion2, data, cel.index, *pixels);

                    AssertOk(replaced, name);
                    Assert::IsTrue(data == *replaced, Wide(std::string(name) + ", cel " + std::to_string(cel.index)).c_str());
                }
            }
        }

        TEST_METHOD(ReplaceCelPixels_Sci2_ChangesOnlyTheBytesOfTheCel)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
            sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(sciVersion2, data, 2);
            AssertOk(pixels, "pixels");
            std::vector<uint8_t> newPixels = OtherPixels(*pixels);

            sci::Result<std::vector<uint8_t>> replaced = ReplaceCelPixels(sciVersion2, data, 2, newPixels);

            AssertOk(replaced, "replace");
            // Each byte of the cel changed, and no other byte.
            std::vector<size_t> changed = ChangedOffsets(data, *replaced);
            Assert::AreEqual(Pic220Cel2Size, changed.size());
            Assert::AreEqual(Pic220Cel2Offset, changed.front());
            Assert::AreEqual(Pic220Cel2Offset + Pic220Cel2Size - 1, changed.back());
            sci::Result<std::vector<uint8_t>> readBack = GetCelPixels(sciVersion2, *replaced, 2);
            AssertOk(readBack, "read back");
            Assert::IsTrue(newPixels == *readBack);
        }

        TEST_METHOD(ReplaceCelPixels_WrongPixelCount_IsUsage)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
            std::vector<uint8_t> tooFew((30 * 74) - 1);
            std::vector<uint8_t> tooMany((30 * 74) + 1);

            AssertFails(ReplaceCelPixels(sciVersion2, data, 2, tooFew), sci::ErrorCode::Usage, "cel 2 has 30 x 74 pixels (2220 bytes), and the new pixels are 2219 bytes");
            AssertFails(ReplaceCelPixels(sciVersion2, data, 2, tooMany), sci::ErrorCode::Usage, "the new pixels are 2221 bytes");
        }

        TEST_METHOD(ReplaceCelPixels_BadCel_IsUsage)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
            std::vector<uint8_t> pixels(30 * 74);

            AssertFails(ReplaceCelPixels(sciVersion2, data, 4, pixels), sci::ErrorCode::Usage, "no cel 4");
            AssertFails(ReplaceCelPixels(sciVersion2, data, -1, pixels), sci::ErrorCode::Usage, "no cel -1");
        }

        TEST_METHOD(ReplaceCelPixels_CompressedCel_IsUnsupported)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic901, sciVersion2);
            std::vector<uint8_t> pixels(320 * 124);

            AssertFails(ReplaceCelPixels(sciVersion2, data, 0, pixels), sci::ErrorCode::Unsupported, "cel 0 is compressed");
        }

        // Cel 0 is made a cel with plain pixels (its colour offset is 0),
        // and the other cels stay compressed: their data can be anywhere.
        TEST_METHOD(PatchPicCelPixels_PicWithAnotherCompressedCel_IsUnsupported)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic901, sciVersion2);
            const size_t celColorOffsetField = 28;
            SetU32(data, CelField(0, celColorOffsetField), 0);
            SetU16(data, CelField(0, CelWidthField), 4);
            std::vector<uint8_t> pixels(4 * 124);

            AssertFails(PatchPicCelPixelsVGA2(data, 0, pixels), sci::ErrorCode::Unsupported, "cel 1 of the pic is compressed");
        }

        // Cel 1 gets the offset of cel 2, so the two have bytes in common.
        TEST_METHOD(PatchPicCelPixels_SharedPixelBytes_IsUnsupported)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
            SetU32(data, CelField(1, CelControlOffsetField), (uint32_t)Pic220Cel2Offset);
            std::vector<uint8_t> before = data;

            AssertFails(PatchPicCelPixelsVGA2(data, 2, std::vector<uint8_t>(30 * 74)), sci::ErrorCode::Unsupported, "cel 2 and cel 1 have pixel bytes in common");
            AssertFails(PatchPicCelPixelsVGA2(data, 1, std::vector<uint8_t>(46 * 74)), sci::ErrorCode::Unsupported, "cel 1 and cel 2 have pixel bytes in common");
            Assert::IsTrue(before == data, L"a refused patch changes nothing");
        }

        // The palette of pic 220 is the bytes 188 to 989 (a header of 37
        // bytes and 255 entries of 3 bytes). A cel that starts at 989 has
        // the last byte of the palette; a cel that starts at 990 has none.
        TEST_METHOD(PatchPicCelPixels_BytesOfThePalette_IsUnsupported)
        {
            const size_t paletteEnd = Pic220PaletteOffset + PaletteHeaderSize + (255 * 3);
            std::vector<uint8_t> pixels(640 * 334, 9);

            std::vector<uint8_t> inPalette = PicData(Sci2Pic220, sciVersion2);
            SetU32(inPalette, CelField(0, CelControlOffsetField), (uint32_t)(paletteEnd - 1));
            AssertFails(PatchPicCelPixelsVGA2(inPalette, 0, pixels), sci::ErrorCode::Unsupported, "cel 0 and the palette of the pic have bytes in common");

            std::vector<uint8_t> afterPalette = PicData(Sci2Pic220, sciVersion2);
            SetU32(afterPalette, CelField(0, CelControlOffsetField), (uint32_t)paletteEnd);
            AssertOk(PatchPicCelPixelsVGA2(afterPalette, 0, pixels), "a cel after the palette");
            Assert::AreEqual(9, (int)afterPalette[paletteEnd]);
        }

        TEST_METHOD(ReadPicLayoutVGA2_GivesTheOffsets)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            sci::Result<PicLayoutVGA2> layout = ReadPicLayoutVGA2(data.data(), data.size());

            AssertOk(layout, "layout");
            Assert::AreEqual(640, (int)layout->width);
            Assert::AreEqual(480, (int)layout->height);
            Assert::AreEqual((uint32_t)Pic220PaletteOffset, layout->paletteOffset);
            Assert::AreEqual((size_t)4, layout->cels.size());
            Assert::AreEqual((uint32_t)Pic220Cel0Offset, layout->cels[0].pixelOffset);
            Assert::AreEqual((uint32_t)(640 * 334), layout->cels[0].pixelSize);
            Assert::AreEqual((uint32_t)Pic220Cel2Offset, layout->cels[2].pixelOffset);
            Assert::AreEqual((uint32_t)Pic220Cel2Size, layout->cels[2].pixelSize);
            Assert::AreEqual(73, (int)layout->cels[2].x);
            Assert::AreEqual(43, (int)layout->cels[2].priority);
        }

        // Each call gets the whole buffer and a smaller size, so that a
        // reader with no check reads memory that the test owns.
        TEST_METHOD(ReadPicLayoutVGA2_DataThatIsCut_IsFormat)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            AssertFails(ReadPicLayoutVGA2(data.data(), 0), sci::ErrorCode::Format, "pic header is not all in the resource");
            AssertFails(ReadPicLayoutVGA2(data.data(), 13), sci::ErrorCode::Format, "pic header is not all in the resource");
            // The headers of 3 of the 4 cels, and 41 of the 42 bytes of the last.
            AssertFails(ReadPicLayoutVGA2(data.data(), CelHeadersAt + (4 * CelHeaderSize) - 1), sci::ErrorCode::Format, "the 4 cel headers of the pic are not all in the resource");
            // The last cel ends at the end of the data.
            AssertFails(ReadPicLayoutVGA2(data.data(), data.size() - 1), sci::ErrorCode::Format, "the pixels of cel 3");
        }

        TEST_METHOD(ReadPicLayoutVGA2_HeaderSizesOfAnotherFormat_IsFormat)
        {
            std::vector<uint8_t> headerSize = PicData(Sci2Pic220, sciVersion2);
            SetU16(headerSize, 0, 16);
            AssertFails(ReadPicLayoutVGA2(headerSize.data(), headerSize.size()), sci::ErrorCode::Format, "a header of 16 bytes and cel headers of 42 bytes");

            std::vector<uint8_t> celHeaderSize = PicData(Sci2Pic220, sciVersion2);
            SetU16(celHeaderSize, 4, 44);
            AssertFails(ReadPicLayoutVGA2(celHeaderSize.data(), celHeaderSize.size()), sci::ErrorCode::Format, "a header of 14 bytes and cel headers of 44 bytes");
        }

        TEST_METHOD(ReadPicLayoutVGA2_PixelOffsetOutsideTheData_IsFormat)
        {
            // In the headers; at the end of the data; near the end of the
            // 32-bit range, where the sum of the offset and the size does
            // not fit 32 bits.
            for (uint32_t offset : { (uint32_t)(CelHeadersAt + (4 * CelHeaderSize) - 1), (uint32_t)252588, 0xfffffff0u })
            {
                std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
                SetU32(data, CelField(2, CelControlOffsetField), offset);

                AssertFails(ReadPicLayoutVGA2(data.data(), data.size()), sci::ErrorCode::Format, "the pixels of cel 2 (30 x 74, at offset " + std::to_string(offset) + ")");
            }
        }

        TEST_METHOD(ReadPicLayoutVGA2_PaletteOffsetOutsideTheData_IsFormat)
        {
            // In the headers; the first byte after the data.
            for (uint32_t offset : { (uint32_t)(CelHeadersAt + (4 * CelHeaderSize) - 1), (uint32_t)252588 })
            {
                std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
                SetU32(data, PaletteOffsetField, offset);

                AssertFails(ReadPicLayoutVGA2(data.data(), data.size()), sci::ErrorCode::Format, "the palette offset of the pic (" + std::to_string(offset) + ")");
            }
        }

        TEST_METHOD(ReadPicPaletteOffsetVGA11_GivesTheOffset)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);

            sci::Result<uint32_t> offset = ReadPicPaletteOffsetVGA11(data.data(), data.size());

            AssertOk(offset, "offset");
            Assert::AreEqual((uint32_t)Pic100PaletteOffset, *offset);
        }

        TEST_METHOD(ReadPicPaletteOffsetVGA11_BadHeader_IsFormat)
        {
            const size_t paletteOffsetField = 28;
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);

            // The header has 40 bytes. The buffer keeps the rest.
            AssertFails(ReadPicPaletteOffsetVGA11(data.data(), 39), sci::ErrorCode::Format, "pic header is not all in the resource");

            std::vector<uint8_t> headerSize = data;
            SetU16(headerSize, 0, 0x28);
            AssertFails(ReadPicPaletteOffsetVGA11(headerSize.data(), headerSize.size()), sci::ErrorCode::Format, "a header of 40 bytes; an SCI1.1 pic has 38");

            // In the header; the first byte after the data.
            for (uint32_t offset : { 39u, (uint32_t)data.size() })
            {
                std::vector<uint8_t> paletteOffset = data;
                SetU32(paletteOffset, paletteOffsetField, offset);
                AssertFails(ReadPicPaletteOffsetVGA11(paletteOffset.data(), paletteOffset.size()), sci::ErrorCode::Format, "the palette offset of the pic (" + std::to_string(offset) + ")");
            }

            std::vector<uint8_t> noPalette = data;
            SetU32(noPalette, paletteOffsetField, 0);
            sci::Result<uint32_t> none = ReadPicPaletteOffsetVGA11(noPalette.data(), noPalette.size());
            AssertOk(none, "a pic with no palette");
            Assert::AreEqual((uint32_t)0, *none);
        }

        TEST_METHOD(ReadPaletteLayout_ThreeByteEntries)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            sci::Result<PaletteLayout> layout = ReadPaletteLayout(data.data(), data.size(), Pic220PaletteOffset);

            AssertOk(layout, "layout");
            Assert::AreEqual(0, layout->firstEntry);
            Assert::AreEqual(255, layout->entryCount);
            Assert::AreEqual(3, layout->entrySize);
            Assert::AreEqual(Pic220PaletteOffset + PaletteHeaderSize, layout->entriesOffset);
        }

        TEST_METHOD(ReadPaletteLayout_FourByteEntries)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);

            sci::Result<PaletteLayout> layout = ReadPaletteLayout(data.data(), data.size(), Pic100PaletteOffset);

            AssertOk(layout, "layout");
            Assert::AreEqual(0, layout->firstEntry);
            Assert::AreEqual(256, layout->entryCount);
            Assert::AreEqual(4, layout->entrySize);
            Assert::AreEqual(Pic100PaletteOffset + PaletteHeaderSize, layout->entriesOffset);
        }

        // The form with no header: a mapping table (0, 1, 2 ...), 4 bytes,
        // then 256 entries of 4 bytes. It is 5 bytes into the data here.
        TEST_METHOD(ReadPaletteLayout_FormWithNoHeader)
        {
            const size_t paletteOffset = 5;
            std::vector<uint8_t> data(paletteOffset, 0xee);
            for (int i = 0; i < 256; i++)
            {
                data.push_back((uint8_t)i);
            }
            data.insert(data.end(), 4 + (256 * 4), 0);

            sci::Result<PaletteLayout> layout = ReadPaletteLayout(data.data(), data.size(), paletteOffset);

            AssertOk(layout, "layout");
            Assert::AreEqual(0, layout->firstEntry);
            Assert::AreEqual(256, layout->entryCount);
            Assert::AreEqual(4, layout->entrySize);
            Assert::AreEqual(paletteOffset + 260, layout->entriesOffset);

            // One byte less: the last entry is not all in the data.
            AssertFails(ReadPaletteLayout(data.data(), data.size() - 1, paletteOffset), sci::ErrorCode::Format, "the 256 entries of the palette are not all in the data");
        }

        // ReadPalette reads no entry from fewer than 37 bytes.
        TEST_METHOD(ReadPaletteLayout_FewerBytesThanAHeader_StoresNoEntry)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            sci::Result<PaletteLayout> layout = ReadPaletteLayout(data.data(), Pic220PaletteOffset + PaletteHeaderSize - 1, Pic220PaletteOffset);

            AssertOk(layout, "layout");
            Assert::AreEqual(0, layout->entryCount);

            // A palette offset at the end of the data (500 bytes here; the
            // buffer keeps the rest).
            sci::Result<PaletteLayout> atTheEnd = ReadPaletteLayout(data.data(), 500, 500);
            AssertOk(atTheEnd, "a palette offset at the end of the data");
            Assert::AreEqual(0, atTheEnd->entryCount);
        }

        TEST_METHOD(ReadPaletteLayout_BadPalette_IsFormat)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);

            // A palette offset past the end of the data (100 bytes here; the
            // buffer keeps the rest).
            AssertFails(ReadPaletteLayout(data.data(), 100, 101), sci::ErrorCode::Format, "the palette offset (101) is not in the data (100 bytes)");

            // The header and 100 of the 255 entries. The buffer keeps the rest.
            AssertFails(ReadPaletteLayout(data.data(), Pic220PaletteOffset + PaletteHeaderSize + (100 * 3), Pic220PaletteOffset), sci::ErrorCode::Format,
                "the 255 entries of the palette are not all in the data");
            // One byte less than the 255 entries.
            AssertFails(ReadPaletteLayout(data.data(), Pic220PaletteOffset + PaletteHeaderSize + (255 * 3) - 1, Pic220PaletteOffset), sci::ErrorCode::Format,
                "the 255 entries of the palette are not all in the data");
            AssertOk(ReadPaletteLayout(data.data(), Pic220PaletteOffset + PaletteHeaderSize + (255 * 3), Pic220PaletteOffset), "the data ends after the last entry");

            // The entries 200 to 299.
            std::vector<uint8_t> past255 = data;
            past255[Pic220PaletteOffset + PaletteFirstEntryField] = 200;
            SetU16(past255, Pic220PaletteOffset + PaletteEntryCountField, 100);
            AssertFails(ReadPaletteLayout(past255.data(), past255.size(), Pic220PaletteOffset), sci::ErrorCode::Format, "the entries 200 to 299");

            // The entries 200 to 255 are the last that a palette has.
            std::vector<uint8_t> to255 = data;
            to255[Pic220PaletteOffset + PaletteFirstEntryField] = 200;
            SetU16(to255, Pic220PaletteOffset + PaletteEntryCountField, 56);
            sci::Result<PaletteLayout> last = ReadPaletteLayout(to255.data(), to255.size(), Pic220PaletteOffset);
            AssertOk(last, "the entries 200 to 255");
            Assert::AreEqual(200, last->firstEntry);
            Assert::AreEqual(56, last->entryCount);

            std::vector<uint8_t> format = data;
            format[Pic220PaletteOffset + PaletteFormatField] = 2;
            AssertFails(ReadPaletteLayout(format.data(), format.size(), Pic220PaletteOffset), sci::ErrorCode::Format, "a format that is not known (2)");
        }

        TEST_METHOD(PatchPaletteColors_ThreeByteEntries_ChangesOnlyTheirBytes)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic220, sciVersion2);
            std::vector<uint8_t> before = data;
            RGBQUAD colors[256] = {};
            for (int i = 0; i < 256; i++)
            {
                colors[i].rgbRed = (uint8_t)i;
                colors[i].rgbGreen = (uint8_t)(255 - i);
                colors[i].rgbBlue = 77;
            }

            AssertOk(PatchPaletteColors(data, Pic220PaletteOffset, 10, 12, colors), "patch");

            const size_t entries = Pic220PaletteOffset + PaletteHeaderSize;
            std::vector<uint8_t> expected = { 10, 245, 77, 11, 244, 77, 12, 243, 77 };
            Assert::IsTrue(expected == std::vector<uint8_t>(data.begin() + entries + (10 * 3), data.begin() + entries + (13 * 3)));
            std::vector<size_t> changed = ChangedOffsets(before, data);
            Assert::IsFalse(changed.empty());
            Assert::IsTrue(changed.front() >= entries + (10 * 3));
            Assert::IsTrue(changed.back() < entries + (13 * 3));
        }

        TEST_METHOD(PatchPaletteColors_FourByteEntries_KeepsTheFlagBytes)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);
            const size_t entries = Pic100PaletteOffset + PaletteHeaderSize;
            // Flag bytes that the patch must keep.
            data[entries + (5 * 4)] = 0x33;
            data[entries + (6 * 4)] = 0x44;
            std::vector<uint8_t> before = data;
            RGBQUAD colors[256] = {};
            colors[5].rgbRed = 201;
            colors[5].rgbGreen = 202;
            colors[5].rgbBlue = 203;
            colors[6].rgbRed = 211;
            colors[6].rgbGreen = 212;
            colors[6].rgbBlue = 213;

            AssertOk(PatchPaletteColors(data, Pic100PaletteOffset, 5, 6, colors), "patch");

            std::vector<uint8_t> expected = { 0x33, 201, 202, 203, 0x44, 211, 212, 213 };
            Assert::IsTrue(expected == std::vector<uint8_t>(data.begin() + entries + (5 * 4), data.begin() + entries + (7 * 4)));
            std::vector<size_t> changed = ChangedOffsets(before, data);
            Assert::AreEqual((size_t)6, changed.size());
            Assert::AreEqual(entries + (5 * 4) + 1, changed.front());
            Assert::AreEqual(entries + (6 * 4) + 3, changed.back());
        }

        // Pic 1150 stores the entries 104 to 235.
        TEST_METHOD(PatchPaletteColors_RangeOutsideTheStoredEntries_IsUsage)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic1150, sciVersion2);
            const size_t paletteOffset = 230;
            std::vector<uint8_t> before = data;
            RGBQUAD colors[256] = {};
            for (RGBQUAD &color : colors)
            {
                color.rgbRed = 1;
            }

            AssertFails(PatchPaletteColors(data, paletteOffset, 103, 110, colors), sci::ErrorCode::Usage, "stores the entries 104-235; the range 103-110 is not inside them");
            AssertFails(PatchPaletteColors(data, paletteOffset, 230, 236, colors), sci::ErrorCode::Usage, "the range 230-236 is not inside them");
            AssertFails(PatchPaletteColors(data, paletteOffset, 110, 109, colors), sci::ErrorCode::Usage, "110-109 is not a range of palette entries");
            AssertFails(PatchPaletteColors(data, paletteOffset, -1, 110, colors), sci::ErrorCode::Usage, "-1-110 is not a range of palette entries");
            AssertFails(PatchPaletteColors(data, paletteOffset, 110, 256, colors), sci::ErrorCode::Usage, "110-256 is not a range of palette entries");
            Assert::IsTrue(before == data, L"a refused patch changes nothing");

            // The first and the last stored entry can change.
            AssertOk(PatchPaletteColors(data, paletteOffset, 104, 235, colors), "the whole stored range");
            const size_t entries = paletteOffset + PaletteHeaderSize;
            for (size_t i = 0; i < 132; i++)
            {
                Assert::AreEqual(1, (int)data[entries + (i * 3)], Wide("the red of entry " + std::to_string(104 + i)).c_str());
                Assert::AreEqual(0, (int)data[entries + (i * 3) + 1], Wide("the green of entry " + std::to_string(104 + i)).c_str());
            }
            std::vector<size_t> changed = ChangedOffsets(before, data);
            Assert::IsFalse(changed.empty());
            Assert::IsTrue(changed.front() >= entries);
            Assert::IsTrue(changed.back() < entries + (132 * 3));
        }

        TEST_METHOD(PatchPaletteColors_PaletteWithNoEntry_IsUsage)
        {
            // Fewer bytes than a palette header. The vector has room for a
            // header, so that a reader with no check reads memory that the
            // test owns.
            std::vector<uint8_t> data;
            data.reserve(64);
            data.resize(20, 0);
            RGBQUAD colors[256] = {};

            AssertFails(PatchPaletteColors(data, 0, 0, 0, colors), sci::ErrorCode::Usage, "stores no entry");
        }

        TEST_METHOD(ReplacePaletteRange_Sci2_ChangesOnlyTheColoursOfTheRange)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic1150, sciVersion2);
            sci::Result<PicDescription> before = DescribePic(sciVersion2, data);
            AssertOk(before, "describe");
            std::array<IndexedBmpColor, 256> colors = NewColors();

            sci::Result<std::vector<uint8_t>> replaced = ReplacePaletteRange(sciVersion2, data, 104, 110, colors);

            AssertOk(replaced, "replace");
            sci::Result<PicDescription> after = DescribePic(sciVersion2, *replaced);
            AssertOk(after, "describe the new pic");
            for (int i = 0; i < 256; i++)
            {
                IndexedBmpColor expected = ((i >= 104) && (i <= 110)) ? colors[i] : before->palette.colors[i];
                Assert::IsTrue(expected == after->palette.colors[i], Wide("colour " + std::to_string(i)).c_str());
            }
            Assert::IsTrue(before->palette.flags == after->palette.flags, L"the flags");
            Assert::AreEqual(before->palette.firstEntry, after->palette.firstEntry);
            Assert::AreEqual(before->palette.entryCount, after->palette.entryCount);
            // The bytes of the 7 entries, and no other byte.
            const size_t entries = 230 + PaletteHeaderSize;
            std::vector<size_t> changed = ChangedOffsets(data, *replaced);
            Assert::IsFalse(changed.empty());
            Assert::IsTrue(changed.front() >= entries);
            Assert::IsTrue(changed.back() < entries + (7 * 3));
        }

        TEST_METHOD(ReplacePaletteRange_OutsideTheStoredEntries_IsUsage)
        {
            std::vector<uint8_t> data = PicData(Sci2Pic1150, sciVersion2);

            AssertFails(ReplacePaletteRange(sciVersion2, data, 80, 110, NewColors()), sci::ErrorCode::Usage, "the range 80-110 is not inside them");
        }

        TEST_METHOD(DescribePic_Sci11_GivesTheCelAndThePalette)
        {
            sci::Result<PicDescription> pic = DescribePic(sciVersion1_1, PicData(Sci11Pic100, sciVersion1_1));

            AssertOk(pic, "describe");
            Assert::IsTrue(pic->format == PicFormat::VGA1_1);
            Assert::AreEqual(320, pic->width);
            Assert::AreEqual(199, pic->height);
            Assert::AreEqual((size_t)1, pic->cels.size());
            Assert::AreEqual(0, pic->backgroundCel);
            const PicCelDescription &cel = pic->cels[0];
            Assert::AreEqual(0, cel.index);
            Assert::AreEqual(320, cel.width);
            Assert::AreEqual(199, cel.height);
            Assert::AreEqual(0, cel.x);
            Assert::AreEqual(0, cel.y);
            Assert::AreEqual(255, (int)cel.skipColor);
            Assert::IsFalse(cel.compressed);
            Assert::AreEqual((uint32_t)(320 * 199), Sum(cel.pixelCounts));
            Assert::IsTrue(pic->palette.present);
            Assert::AreEqual(0, pic->palette.firstEntry);
            Assert::AreEqual(256, pic->palette.entryCount);
            Assert::AreEqual(4, pic->palette.entrySize);
        }

        // The image of the pic that the pic tests check (100.p56-vis.bmp) is
        // the cel with the drawing commands of the pic on it, so most pixels
        // of a row of the image are the pixels of that row of the cel, and
        // not of the same row in the other direction.
        TEST_METHOD(GetCelPixels_Sci11_FirstRowIsTheTopRow)
        {
            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(PicPath("SCI1.1\\100.p56-vis.bmp"));
            AssertOk(image, "the image of the pic");

            sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(sciVersion1_1, PicData(Sci11Pic100, sciVersion1_1), 0);

            AssertOk(pixels, "pixels");
            Assert::AreEqual((size_t)(320 * 199), pixels->size());
            for (int row : { 20, 180 })
            {
                size_t compared = 0;
                size_t matching = MatchingPixels(*pixels, 320, row, *image, row, 255, compared);
                Assert::IsTrue((compared > 200) && ((matching * 10) >= (compared * 9)),
                    Wide("row " + std::to_string(row) + ": " + std::to_string(matching) + " of " + std::to_string(compared) + " pixels are the pixels of the image").c_str());
                size_t flipped = MatchingPixels(*pixels, 320, 198 - row, *image, row, 255, compared);
                Assert::IsTrue(flipped < (compared / 2),
                    Wide("row " + std::to_string(198 - row) + " against row " + std::to_string(row) + " of the image: " + std::to_string(flipped) + " of " + std::to_string(compared)).c_str());
            }
        }

        TEST_METHOD(ReplaceCelPixels_Sci11_GivesThePixelsBackAndKeepsThePalette)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);
            sci::Result<PicDescription> before = DescribePic(sciVersion1_1, data);
            AssertOk(before, "describe");
            sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(sciVersion1_1, data, 0);
            AssertOk(pixels, "pixels");
            std::vector<uint8_t> newPixels = OtherPixels(*pixels);

            sci::Result<std::vector<uint8_t>> replaced = ReplaceCelPixels(sciVersion1_1, data, 0, newPixels);

            AssertOk(replaced, "replace");
            sci::Result<std::vector<uint8_t>> readBack = GetCelPixels(sciVersion1_1, *replaced, 0);
            AssertOk(readBack, "read back");
            Assert::IsTrue(newPixels == *readBack, L"the new pixels");
            sci::Result<PicDescription> after = DescribePic(sciVersion1_1, *replaced);
            AssertOk(after, "describe the new pic");
            Assert::AreEqual(before->width, after->width);
            Assert::AreEqual(before->height, after->height);
            Assert::AreEqual((size_t)1, after->cels.size());
            Assert::AreEqual(before->cels[0].width, after->cels[0].width);
            Assert::AreEqual(before->cels[0].height, after->cels[0].height);
            Assert::AreEqual((int)before->cels[0].skipColor, (int)after->cels[0].skipColor);
            Assert::IsTrue(before->palette.colors == after->palette.colors, L"the colours of the palette");
            Assert::IsTrue(before->palette.UsedEntries() == after->palette.UsedEntries(), L"the used entries of the palette");
        }

        // A pic that SCI Companion wrote once does not change when it gets
        // its own pixels again.
        TEST_METHOD(ReplaceCelPixels_Sci11_OwnPixels_IsStable)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);
            sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(sciVersion1_1, data, 0);
            AssertOk(pixels, "pixels");

            sci::Result<std::vector<uint8_t>> once = ReplaceCelPixels(sciVersion1_1, data, 0, *pixels);
            AssertOk(once, "the first write");
            sci::Result<std::vector<uint8_t>> twice = ReplaceCelPixels(sciVersion1_1, *once, 0, *pixels);
            AssertOk(twice, "the second write");

            Assert::IsTrue(*once == *twice, L"the second write gives the bytes of the first");
            sci::Result<std::vector<uint8_t>> readBack = GetCelPixels(sciVersion1_1, *once, 0);
            AssertOk(readBack, "read back");
            Assert::IsTrue(*pixels == *readBack, L"the pixels stay");
        }

        TEST_METHOD(ReplaceCelPixels_Sci11_BadCelOrPixelCount_IsUsage)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);
            std::vector<uint8_t> pixels(320 * 199);

            AssertFails(ReplaceCelPixels(sciVersion1_1, data, 1, pixels), sci::ErrorCode::Usage, "no cel 1; it has 1 cel");
            AssertFails(GetCelPixels(sciVersion1_1, data, 1), sci::ErrorCode::Usage, "no cel 1; it has 1 cel");
            pixels.pop_back();
            AssertFails(ReplaceCelPixels(sciVersion1_1, data, 0, pixels), sci::ErrorCode::Usage, "cel 0 has 320 x 199 pixels (63680 bytes), and the new pixels are 63679 bytes");
        }

        TEST_METHOD(ReplacePaletteRange_Sci11_ChangesOnlyTheColoursOfTheRange)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);
            sci::Result<PicDescription> before = DescribePic(sciVersion1_1, data);
            AssertOk(before, "describe");
            std::array<IndexedBmpColor, 256> colors = NewColors();

            sci::Result<std::vector<uint8_t>> replaced = ReplacePaletteRange(sciVersion1_1, data, 64, 66, colors);

            AssertOk(replaced, "replace");
            sci::Result<PicDescription> after = DescribePic(sciVersion1_1, *replaced);
            AssertOk(after, "describe the new pic");
            for (int i = 0; i < 256; i++)
            {
                IndexedBmpColor expected = ((i >= 64) && (i <= 66)) ? colors[i] : before->palette.colors[i];
                Assert::IsTrue(expected == after->palette.colors[i], Wide("colour " + std::to_string(i)).c_str());
            }
            Assert::IsTrue(before->palette.flags == after->palette.flags, L"the flags");
            const size_t entries = Pic100PaletteOffset + PaletteHeaderSize;
            std::vector<size_t> changed = ChangedOffsets(data, *replaced);
            Assert::IsFalse(changed.empty());
            Assert::IsTrue(changed.front() >= entries + (64 * 4) + 1);
            Assert::IsTrue(changed.back() <= entries + (66 * 4) + 3);
        }

        TEST_METHOD(DescribePic_Sci11_DataThatIsNotAPic_IsFormat)
        {
            std::vector<uint8_t> data = PicData(Sci11Pic100, sciVersion1_1);

            // The header, and not the palette that it points at.
            std::vector<uint8_t> cut(data.begin(), data.begin() + 1000);
            AssertFails(DescribePic(sciVersion1_1, cut), sci::ErrorCode::Format, "the palette offset of the pic");

            std::vector<uint8_t> noise(100, 0xff);
            AssertFails(DescribePic(sciVersion1_1, noise), sci::ErrorCode::Format, "an SCI1.1 pic has 38");
            AssertFails(GetCelPixels(sciVersion2, noise, 0), sci::ErrorCode::Format, "an SCI2 pic has 14 and 42");
        }

        TEST_METHOD(PicFormatWithNoCels_IsUnsupported)
        {
            std::vector<uint8_t> data = PicData("SCI0\\pic.006", sciVersion0);
            std::vector<uint8_t> pixels(320 * 190);

            AssertFails(DescribePic(sciVersion0, data), sci::ErrorCode::Unsupported, "the format ega");
            AssertFails(GetCelPixels(sciVersion0, data, 0), sci::ErrorCode::Unsupported, "the format ega");
            AssertFails(ReplaceCelPixels(sciVersion0, data, 0, pixels), sci::ErrorCode::Unsupported, "the format ega");
            AssertFails(ReplacePaletteRange(sciVersion0, data, 0, 1, NewColors()), sci::ErrorCode::Unsupported, "the format ega");
        }

        TEST_METHOD(IndexRuns_OfEntriesAndAsText)
        {
            std::array<bool, 256> entries = {};
            Assert::IsTrue(IndexRunsOf(entries).empty());
            Assert::AreEqual(std::string(), IndexRunsText(IndexRunsOf(entries)));

            for (int entry : { 0, 1, 2, 7, 254, 255 })
            {
                entries[entry] = true;
            }
            std::vector<IndexRun> runs = IndexRunsOf(entries);
            Assert::AreEqual((size_t)3, runs.size());
            Assert::IsTrue(IndexRun{ 0, 2 } == runs[0]);
            Assert::IsTrue(IndexRun{ 7, 7 } == runs[1]);
            Assert::IsTrue(IndexRun{ 254, 255 } == runs[2]);
            Assert::AreEqual(std::string("0-2,7,254-255"), IndexRunsText(runs));

            entries.fill(true);
            Assert::AreEqual(std::string("0-255"), IndexRunsText(IndexRunsOf(entries)));
        }

        TEST_METHOD(PicFormatName_GivesTheNames)
        {
            Assert::AreEqual("ega", PicFormatName(PicFormat::EGA));
            Assert::AreEqual("vga1", PicFormatName(PicFormat::VGA1));
            Assert::AreEqual("vga11", PicFormatName(PicFormat::VGA1_1));
            Assert::AreEqual("vga2", PicFormatName(PicFormat::VGA2));
        }
    };
}
