#include "stdafx.h"
#include "CppUnitTest.h"
#include "TestSupport.h"
#include "IndexedBmp.h"
#include "Helper.h"
#include <climits>
#include <filesystem>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    namespace
    {
        const size_t FileHeaderSize = 14;
        const size_t InfoHeaderSize = 40;
        // The offset of the pixels in a file that EncodeIndexedBmp makes.
        const size_t EncodedPixelOffset = FileHeaderSize + InfoHeaderSize + (256 * 4);

        void PutU16(std::vector<uint8_t> &bytes, uint16_t value)
        {
            bytes.push_back((uint8_t)(value & 0xff));
            bytes.push_back((uint8_t)(value >> 8));
        }

        void PutU32(std::vector<uint8_t> &bytes, uint32_t value)
        {
            PutU16(bytes, (uint16_t)(value & 0xffff));
            PutU16(bytes, (uint16_t)(value >> 16));
        }

        uint16_t U16At(const std::vector<uint8_t> &bytes, size_t offset)
        {
            return (uint16_t)(bytes[offset] | (bytes[offset + 1] << 8));
        }

        uint32_t U32At(const std::vector<uint8_t> &bytes, size_t offset)
        {
            return (uint32_t)U16At(bytes, offset) | ((uint32_t)U16At(bytes, offset + 2) << 16);
        }

        void SetU32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
        {
            for (size_t i = 0; i < 4; i++)
            {
                bytes[offset + i] = (uint8_t)(value >> (8 * i));
            }
        }

        // The colour of entry i of the tables of the tests: each entry is
        // different, and so is each of its three parts.
        IndexedBmpColor TestColor(size_t i)
        {
            IndexedBmpColor color;
            color.red = (uint8_t)i;
            color.green = (uint8_t)(255 - i);
            color.blue = (uint8_t)(i ^ 0x55);
            return color;
        }

        // A BMP file that a test builds field by field, so that a test of
        // the reader does not depend on the writer.
        struct RawBmp
        {
            int32_t width = 3;
            int32_t height = 2;             // below 0: the rows go from the top down
            uint16_t planes = 1;
            uint16_t bitCount = 8;
            uint32_t compression = 0;
            uint32_t colorsUsed = 0;        // the field of the header
            size_t tableEntries = 256;      // the entries that the file has
            uint32_t headerSize = (uint32_t)InfoHeaderSize;
            // Bytes between the table and the pixels, which are not pixels.
            size_t gapBeforePixels = 0;
            // The offset of the pixels that the file header gives; below 0:
            // the offset where the pixels are.
            int64_t pixelOffsetField = -1;
            // The rows as the file has them, with their padding. The default
            // is 3 x 2 pixels, from the bottom up: the top row is 1, 2, 3.
            std::vector<uint8_t> pixelData = { 4, 5, 6, 0, 1, 2, 3, 0 };
        };

        std::vector<uint8_t> BytesOf(const RawBmp &raw)
        {
            std::vector<uint8_t> bytes;
            const uint32_t pixelOffset = (uint32_t)(FileHeaderSize + raw.headerSize + (raw.tableEntries * 4) + raw.gapBeforePixels);
            bytes.push_back('B');
            bytes.push_back('M');
            PutU32(bytes, pixelOffset + (uint32_t)raw.pixelData.size());
            PutU16(bytes, 0);
            PutU16(bytes, 0);
            PutU32(bytes, (raw.pixelOffsetField >= 0) ? (uint32_t)raw.pixelOffsetField : pixelOffset);
            PutU32(bytes, raw.headerSize);
            PutU32(bytes, (uint32_t)raw.width);
            PutU32(bytes, (uint32_t)raw.height);
            PutU16(bytes, raw.planes);
            PutU16(bytes, raw.bitCount);
            PutU32(bytes, raw.compression);
            PutU32(bytes, (uint32_t)raw.pixelData.size());
            PutU32(bytes, 0);
            PutU32(bytes, 0);
            PutU32(bytes, raw.colorsUsed);
            PutU32(bytes, 0);
            // The fields of a later header (V4, V5) are zero.
            bytes.resize(FileHeaderSize + raw.headerSize, 0);
            for (size_t i = 0; i < raw.tableEntries; i++)
            {
                IndexedBmpColor color = TestColor(i);
                bytes.push_back(color.blue);
                bytes.push_back(color.green);
                bytes.push_back(color.red);
                bytes.push_back(0);
            }
            bytes.insert(bytes.end(), raw.gapBeforePixels, 0xee);
            bytes.insert(bytes.end(), raw.pixelData.begin(), raw.pixelData.end());
            return bytes;
        }

        // The image as Windows reads the file (LoadImage, into a DIB
        // section): a reader of the format that is not IndexedBmp.cpp.
        // False when Windows does not read it as an 8-bit image.
        bool LoadWithWindows(const std::string &path, IndexedBmp &image)
        {
            HBITMAP bitmap = (HBITMAP)LoadImageA(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
            if (bitmap == nullptr)
            {
                return false;
            }
            DIBSECTION section = {};
            bool ok = (GetObjectA(bitmap, sizeof(section), &section) == sizeof(section)) && (section.dsBmih.biBitCount == 8) && (section.dsBm.bmBits != nullptr);
            if (ok)
            {
                image.width = section.dsBmih.biWidth;
                image.height = abs(section.dsBmih.biHeight);
                const bool bottomUp = (section.dsBmih.biHeight > 0);
                const size_t width = (size_t)image.width;
                const size_t height = (size_t)image.height;
                const size_t stride = (width + 3) & ~(size_t)3;
                const uint8_t *bits = (const uint8_t *)section.dsBm.bmBits;
                image.pixels.resize(width * height);
                for (size_t y = 0; y < height; y++)
                {
                    const uint8_t *row = bits + ((bottomUp ? (height - 1 - y) : y) * stride);
                    std::copy(row, row + width, image.pixels.begin() + (y * width));
                }
                HDC dc = CreateCompatibleDC(nullptr);
                HGDIOBJ before = SelectObject(dc, bitmap);
                RGBQUAD table[256] = {};
                UINT count = GetDIBColorTable(dc, 0, 256, table);
                SelectObject(dc, before);
                DeleteDC(dc);
                image.colorCount = (int)count;
                for (UINT i = 0; i < count; i++)
                {
                    image.colors[i].red = table[i].rgbRed;
                    image.colors[i].green = table[i].rgbGreen;
                    image.colors[i].blue = table[i].rgbBlue;
                }
            }
            DeleteObject(bitmap);
            return ok;
        }

        // Where two pixel vectors differ first, for an assert message.
        std::string FirstDifference(const std::vector<uint8_t> &expected, const std::vector<uint8_t> &actual)
        {
            if (expected.size() != actual.size())
            {
                return "the sizes are " + std::to_string(expected.size()) + " and " + std::to_string(actual.size());
            }
            for (size_t i = 0; i < expected.size(); i++)
            {
                if (expected[i] != actual[i])
                {
                    return "pixel " + std::to_string(i) + " is " + std::to_string(actual[i]) + ", not " + std::to_string(expected[i]);
                }
            }
            return "no difference";
        }

        void AssertEncodeIsUsage(const IndexedBmp &image, const std::string &textInMessage)
        {
            sci::Result<std::vector<uint8_t>> encoded = EncodeIndexedBmp(image);
            Assert::IsFalse(encoded.has_value(), Wide("the encode must fail: " + textInMessage).c_str());
            Assert::IsTrue(encoded.error().code == sci::ErrorCode::Usage, Wide(encoded.error().ToString()).c_str());
            Assert::IsTrue(encoded.error().message.find(textInMessage) != std::string::npos, Wide(encoded.error().ToString()).c_str());
        }

        // No handle is open on the file: an open that shares nothing
        // succeeds.
        void AssertNotOpen(const std::string &path)
        {
            HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(file != INVALID_HANDLE_VALUE, Wide("a handle is still open on " + path).c_str());
            CloseHandle(file);
        }

        // An image whose pixels and colours are all different from their
        // neighbours.
        IndexedBmp TestImage(int width, int height)
        {
            IndexedBmp image;
            image.width = width;
            image.height = height;
            image.pixels.resize((size_t)width * (size_t)height);
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    image.pixels[((size_t)y * width) + x] = (uint8_t)((x * 7) + (y * 13) + width);
                }
            }
            for (size_t i = 0; i < image.colors.size(); i++)
            {
                image.colors[i] = TestColor(i);
            }
            return image;
        }

        void AssertFails(const sci::Result<IndexedBmp> &image, sci::ErrorCode code, const std::string &textInMessage)
        {
            Assert::IsFalse(image.has_value(), L"the read must fail");
            Assert::IsTrue(image.error().code == code, Wide(image.error().ToString()).c_str());
            Assert::IsTrue(image.error().message.find(textInMessage) != std::string::npos, Wide(image.error().ToString()).c_str());
        }
    }

    // The 8-bit BMP reader and writer (IndexedBmp.h).
    TEST_CLASS(TestIndexedBmp)
    {
        std::string _folder;

    public:
        TEST_METHOD_INITIALIZE(SetUp)
        {
            _folder = (std::filesystem::temp_directory_path() / ("scicompanion-bmp-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()))).string();
            std::filesystem::create_directories(_folder);
        }

        TEST_METHOD_CLEANUP(TearDown)
        {
            RemoveFolder(_folder);
        }

        // Widths 1 to 4 have each count of padding bytes; 639 is a wide row
        // with padding.
        TEST_METHOD(EncodeThenDecode_GivesTheSameImage)
        {
            for (int width : { 1, 2, 3, 4, 639 })
            {
                const int height = 5;
                IndexedBmp image = TestImage(width, height);

                sci::Result<std::vector<uint8_t>> bytes = EncodeIndexedBmp(image);
                AssertOk(bytes, "encode, width " + std::to_string(width));
                const size_t rowSize = ((size_t)width + 3) & ~(size_t)3;
                Assert::AreEqual(EncodedPixelOffset + (rowSize * height), bytes->size(), Wide("the size of the file, width " + std::to_string(width)).c_str());

                sci::Result<IndexedBmp> decoded = DecodeIndexedBmp(*bytes);
                AssertOk(decoded, "decode, width " + std::to_string(width));
                Assert::AreEqual(width, decoded->width);
                Assert::AreEqual(height, decoded->height);
                Assert::AreEqual(256, decoded->colorCount);
                Assert::IsTrue(image.pixels == decoded->pixels, Wide("the pixels, width " + std::to_string(width)).c_str());
                Assert::IsTrue(image.colors == decoded->colors, Wide("the colours, width " + std::to_string(width)).c_str());
            }
        }

        TEST_METHOD(Encode_WritesTheHeadersOfAnEightBitFile)
        {
            IndexedBmp image = TestImage(3, 2);
            image.pixels = { 1, 2, 3, 4, 5, 6 };

            sci::Result<std::vector<uint8_t>> encoded = EncodeIndexedBmp(image);

            AssertOk(encoded, "encode");
            const std::vector<uint8_t> &bytes = *encoded;
            Assert::AreEqual((int)'B', (int)bytes[0]);
            Assert::AreEqual((int)'M', (int)bytes[1]);
            Assert::AreEqual((uint32_t)bytes.size(), U32At(bytes, 2), L"bfSize is the size of the file");
            Assert::AreEqual(0, (int)U16At(bytes, 6), L"bfReserved1");
            Assert::AreEqual(0, (int)U16At(bytes, 8), L"bfReserved2");
            Assert::AreEqual((uint32_t)EncodedPixelOffset, U32At(bytes, 10), L"bfOffBits");
            Assert::AreEqual((uint32_t)InfoHeaderSize, U32At(bytes, 14), L"biSize");
            Assert::AreEqual((uint32_t)3, U32At(bytes, 18), L"biWidth");
            Assert::AreEqual((uint32_t)2, U32At(bytes, 22), L"biHeight");
            Assert::AreEqual(1, (int)U16At(bytes, 26), L"biPlanes");
            Assert::AreEqual(8, (int)U16At(bytes, 28), L"biBitCount");
            Assert::AreEqual((uint32_t)0, U32At(bytes, 30), L"biCompression");
            Assert::AreEqual((uint32_t)8, U32At(bytes, 34), L"biSizeImage");
            Assert::AreEqual((uint32_t)0, U32At(bytes, 38), L"biXPelsPerMeter");
            Assert::AreEqual((uint32_t)0, U32At(bytes, 42), L"biYPelsPerMeter");
            Assert::AreEqual((uint32_t)256, U32At(bytes, 46), L"biClrUsed");
            Assert::AreEqual((uint32_t)0, U32At(bytes, 50), L"biClrImportant");
        }

        TEST_METHOD(Encode_WritesTheTableAsBlueGreenRed)
        {
            IndexedBmp image = TestImage(3, 2);

            sci::Result<std::vector<uint8_t>> encoded = EncodeIndexedBmp(image);

            AssertOk(encoded, "encode");
            const size_t entry = FileHeaderSize + InfoHeaderSize + (200 * 4);
            Assert::AreEqual((int)TestColor(200).blue, (int)(*encoded)[entry]);
            Assert::AreEqual((int)TestColor(200).green, (int)(*encoded)[entry + 1]);
            Assert::AreEqual((int)TestColor(200).red, (int)(*encoded)[entry + 2]);
            Assert::AreEqual(0, (int)(*encoded)[entry + 3]);
        }

        TEST_METHOD(Encode_WritesTheRowsFromTheBottomUpWithPadding)
        {
            IndexedBmp image = TestImage(3, 2);
            image.pixels = { 1, 2, 3, 4, 5, 6 };

            sci::Result<std::vector<uint8_t>> encoded = EncodeIndexedBmp(image);

            AssertOk(encoded, "encode");
            std::vector<uint8_t> rows(encoded->begin() + EncodedPixelOffset, encoded->end());
            Assert::IsTrue(rows == std::vector<uint8_t>({ 4, 5, 6, 0, 1, 2, 3, 0 }));
        }

        TEST_METHOD(Encode_BadSize_IsUsage)
        {
            // No pixels are the correct count for a width of 0: only the size
            // is bad.
            IndexedBmp noWidth = TestImage(3, 2);
            noWidth.width = 0;
            noWidth.pixels.clear();
            AssertEncodeIsUsage(noWidth, "cannot have 0 x 2 pixels");

            IndexedBmp noHeight = TestImage(3, 2);
            noHeight.height = 0;
            noHeight.pixels.clear();
            AssertEncodeIsUsage(noHeight, "cannot have 3 x 0 pixels");

            // The pixels are the correct count for these sizes too.
            IndexedBmp tooWide = TestImage(IndexedBmpMaxDimension + 1, 1);
            AssertEncodeIsUsage(tooWide, "cannot have 65536 x 1 pixels");

            IndexedBmp tooHigh = TestImage(1, IndexedBmpMaxDimension + 1);
            AssertEncodeIsUsage(tooHigh, "cannot have 1 x 65536 pixels");
        }

        TEST_METHOD(Encode_WrongPixelCount_IsUsage)
        {
            IndexedBmp shortPixels = TestImage(3, 2);
            shortPixels.pixels.pop_back();
            AssertEncodeIsUsage(shortPixels, "needs 6 bytes of pixels, and it has 5");

            IndexedBmp longPixels = TestImage(3, 2);
            longPixels.pixels.push_back(0);
            AssertEncodeIsUsage(longPixels, "needs 6 bytes of pixels, and it has 7");
        }

        // 1024 x 65535 pixels are 1078 bytes more than the limit of 64 MB.
        // The size is checked before the pixels, so the test needs none.
        TEST_METHOD(Encode_FileOverTheLimit_IsUnsupported)
        {
            IndexedBmp image;
            image.width = 1024;
            image.height = 65535;

            sci::Result<std::vector<uint8_t>> encoded = EncodeIndexedBmp(image);

            Assert::IsFalse(encoded.has_value());
            Assert::IsTrue(encoded.error().code == sci::ErrorCode::Unsupported, Wide(encoded.error().ToString()).c_str());
        }

        TEST_METHOD(Decode_BottomUpFile_GivesTheTopRowFirst)
        {
            RawBmp raw;

            sci::Result<IndexedBmp> image = DecodeIndexedBmp(BytesOf(raw));

            AssertOk(image, "decode");
            Assert::AreEqual(3, image->width);
            Assert::AreEqual(2, image->height);
            Assert::IsTrue(image->pixels == std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6 }));
            Assert::AreEqual(256, image->colorCount);
            for (size_t i = 0; i < image->colors.size(); i++)
            {
                Assert::IsTrue(TestColor(i) == image->colors[i], Wide("colour " + std::to_string(i)).c_str());
            }
        }

        TEST_METHOD(Decode_TopDownFile_GivesTheTopRowFirst)
        {
            RawBmp raw;
            raw.height = -2;
            raw.pixelData = { 1, 2, 3, 0, 4, 5, 6, 0 };

            sci::Result<IndexedBmp> image = DecodeIndexedBmp(BytesOf(raw));

            AssertOk(image, "decode");
            Assert::AreEqual(2, image->height);
            Assert::IsTrue(image->pixels == std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6 }));
        }

        // A file of a paint program that saved 16 colours: biClrUsed is 16,
        // and the pixels start after 16 entries.
        TEST_METHOD(Decode_SixteenColourTable_GivesBlackForTheOtherEntries)
        {
            RawBmp raw;
            raw.colorsUsed = 16;
            raw.tableEntries = 16;

            sci::Result<IndexedBmp> image = DecodeIndexedBmp(BytesOf(raw));

            AssertOk(image, "decode");
            Assert::AreEqual(16, image->colorCount);
            Assert::IsTrue(image->pixels == std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6 }));
            for (size_t i = 0; i < image->colors.size(); i++)
            {
                IndexedBmpColor expected = (i < 16) ? TestColor(i) : IndexedBmpColor();
                Assert::IsTrue(expected == image->colors[i], Wide("colour " + std::to_string(i)).c_str());
            }
        }

        // The table of a V5 header starts after its 124 bytes.
        TEST_METHOD(Decode_V5Header_ReadsTheTableAfterTheHeader)
        {
            RawBmp raw;
            raw.headerSize = 124;

            sci::Result<IndexedBmp> image = DecodeIndexedBmp(BytesOf(raw));

            AssertOk(image, "decode");
            Assert::IsTrue(image->pixels == std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6 }));
            Assert::IsTrue(TestColor(200) == image->colors[200]);
        }

        TEST_METHOD(Decode_Rle8_IsUnsupported)
        {
            RawBmp raw;
            raw.compression = 1;    // BI_RLE8

            AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Unsupported, "compressed");
        }

        TEST_METHOD(Decode_TwentyFourBit_IsUnsupported)
        {
            RawBmp raw;
            raw.bitCount = 24;
            raw.tableEntries = 0;
            raw.pixelData.assign(24, 0);

            AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Unsupported, "24-bit");
        }

        TEST_METHOD(Decode_FourBit_IsUnsupported)
        {
            RawBmp raw;
            raw.bitCount = 4;
            raw.colorsUsed = 16;
            raw.tableEntries = 16;

            AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Unsupported, "4-bit");
        }

        // The header gives 256 entries and the offset of the pixels after
        // them, and the file ends in the table.
        TEST_METHOD(Decode_TableCutByTheEndOfTheFile_IsFormat)
        {
            std::vector<uint8_t> bytes = BytesOf(RawBmp());
            bytes.resize(FileHeaderSize + InfoHeaderSize + (16 * 4));

            AssertFails(DecodeIndexedBmp(bytes), sci::ErrorCode::Format, "colour table");
        }

        // The header gives 256 entries, and the pixels start after 16: the
        // file is long enough for 256 entries, but they are pixels.
        TEST_METHOD(Decode_TableThatRunsIntoThePixels_IsFormat)
        {
            RawBmp raw;
            raw.width = 40;
            raw.height = 40;
            raw.tableEntries = 16;
            raw.pixelData.assign(40 * 40, 7);

            AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Format, "colour table");
        }

        TEST_METHOD(Decode_TableOfTooManyEntries_IsFormat)
        {
            RawBmp oneTooMany;
            oneTooMany.colorsUsed = 257;
            oneTooMany.tableEntries = 257;
            AssertFails(DecodeIndexedBmp(BytesOf(oneTooMany)), sci::ErrorCode::Format, "257 entries");

            RawBmp raw;
            raw.colorsUsed = 300;
            raw.tableEntries = 300;
            AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Format, "300 entries");
        }

        // A header that gives 256 and not 0 for a table with all entries.
        TEST_METHOD(Decode_TableOf256EntriesInTheHeader_IsRead)
        {
            RawBmp raw;
            raw.colorsUsed = 256;

            sci::Result<IndexedBmp> image = DecodeIndexedBmp(BytesOf(raw));

            AssertOk(image, "decode");
            Assert::AreEqual(256, image->colorCount);
            Assert::IsTrue(TestColor(255) == image->colors[255]);
        }

        // The pixels are where the file header says, not at the end of the
        // table.
        TEST_METHOD(Decode_GapBeforeThePixels_ReadsThePixelsAtTheirOffset)
        {
            RawBmp raw;
            raw.gapBeforePixels = 6;

            sci::Result<IndexedBmp> image = DecodeIndexedBmp(BytesOf(raw));

            AssertOk(image, "decode");
            Assert::IsTrue(image->pixels == std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6 }));
        }

        // The header gives 16 entries, and the pixels start after the room of
        // 256: the other 240 entries of the file are not in the table.
        TEST_METHOD(Decode_SixteenColoursAndPixelsAfter256Entries_ReadsSixteen)
        {
            RawBmp raw;
            raw.colorsUsed = 16;

            sci::Result<IndexedBmp> image = DecodeIndexedBmp(BytesOf(raw));

            AssertOk(image, "decode");
            Assert::AreEqual(16, image->colorCount);
            Assert::IsTrue(image->pixels == std::vector<uint8_t>({ 1, 2, 3, 4, 5, 6 }));
            Assert::IsTrue(TestColor(15) == image->colors[15]);
            Assert::IsTrue(IndexedBmpColor() == image->colors[16]);
        }

        // An offset of the pixels near the end of the 32-bit range: the sum
        // of the offset and the size of the pixels does not fit 32 bits.
        TEST_METHOD(Decode_PixelOffsetPastTheFile_IsFormat)
        {
            for (int64_t offset : { 0xfffffff8LL, 0xfffffffcLL, 0x7ffffffcLL })
            {
                RawBmp raw;
                raw.pixelOffsetField = offset;

                AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Format, "pixels are not all in the file");
            }
        }

        // The file is one byte short. The buffer keeps that byte, so that a
        // reader with no check reads memory that the test owns.
        TEST_METHOD(Decode_ShortPixels_IsFormat)
        {
            std::vector<uint8_t> bytes = BytesOf(RawBmp());

            AssertFails(DecodeIndexedBmp(bytes.data(), bytes.size() - 1), sci::ErrorCode::Format, "pixels are not all in the file");
        }

        TEST_METHOD(Decode_TwoPlanes_IsFormat)
        {
            RawBmp raw;
            raw.planes = 2;

            AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Format, "2 planes");
        }

        TEST_METHOD(Decode_BadSize_IsFormat)
        {
            // The text of the size error, not of the error for pixels that
            // are not all in the file (which has the size too).
            RawBmp noWidth;
            noWidth.width = 0;
            AssertFails(DecodeIndexedBmp(BytesOf(noWidth)), sci::ErrorCode::Format, "a BMP file of 0 x 2 pixels");

            RawBmp negativeWidth;
            negativeWidth.width = -3;
            AssertFails(DecodeIndexedBmp(BytesOf(negativeWidth)), sci::ErrorCode::Format, "a BMP file of -3 x 2 pixels");

            RawBmp noHeight;
            noHeight.height = 0;
            AssertFails(DecodeIndexedBmp(BytesOf(noHeight)), sci::ErrorCode::Format, "a BMP file of 3 x 0 pixels");

            RawBmp tooWide;
            tooWide.width = IndexedBmpMaxDimension + 1;
            AssertFails(DecodeIndexedBmp(BytesOf(tooWide)), sci::ErrorCode::Format, "a BMP file of 65536 x 2 pixels");

            RawBmp tooHigh;
            tooHigh.height = IndexedBmpMaxDimension + 1;
            AssertFails(DecodeIndexedBmp(BytesOf(tooHigh)), sci::ErrorCode::Format, "a BMP file of 3 x 65536 pixels");

            RawBmp tooHighTopDown;
            tooHighTopDown.height = -(IndexedBmpMaxDimension + 1);
            AssertFails(DecodeIndexedBmp(BytesOf(tooHighTopDown)), sci::ErrorCode::Format, "a BMP file of 3 x -65536 pixels");

            // The height that has no positive value.
            RawBmp lowestHeight;
            lowestHeight.height = INT_MIN;
            AssertFails(DecodeIndexedBmp(BytesOf(lowestHeight)), sci::ErrorCode::Format, "a BMP file of 3 x -2147483648 pixels");
        }

        TEST_METHOD(Decode_NotABmp_IsFormat)
        {
            std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 3 };
            AssertFails(DecodeIndexedBmp(png), sci::ErrorCode::Format, "not a BMP file");

            AssertFails(DecodeIndexedBmp(std::vector<uint8_t>()), sci::ErrorCode::Format, "not a BMP file");

            // The signature and no more.
            AssertFails(DecodeIndexedBmp(std::vector<uint8_t>({ 'B', 'M' })), sci::ErrorCode::Format, "not a BMP file");

            // A good file with another second letter.
            std::vector<uint8_t> otherLetter = BytesOf(RawBmp());
            otherLetter[1] = 'X';
            AssertFails(DecodeIndexedBmp(otherLetter), sci::ErrorCode::Format, "not a BMP file");

            // The file ends before the size field of the header: 17 bytes.
            // The buffer keeps the rest, as in Decode_ShortPixels_IsFormat.
            std::vector<uint8_t> good = BytesOf(RawBmp());
            AssertFails(DecodeIndexedBmp(good.data(), FileHeaderSize + 3), sci::ErrorCode::Format, "not a BMP file");
        }

        // A header size that is not one of the format. The fields of a
        // header are 36 bytes after its size field, so the reader must not
        // read them from a file that has only the size field (18 bytes; the
        // buffer keeps the rest, as in Decode_ShortPixels_IsFormat).
        TEST_METHOD(Decode_HeaderSizeBelowForty_IsFormat)
        {
            for (uint32_t headerSize : { 0u, 16u, 39u })
            {
                std::vector<uint8_t> bytes = BytesOf(RawBmp());
                SetU32(bytes, FileHeaderSize, headerSize);
                std::string text = "a BMP header of " + std::to_string(headerSize) + " bytes";

                AssertFails(DecodeIndexedBmp(bytes), sci::ErrorCode::Format, text);
                AssertFails(DecodeIndexedBmp(bytes.data(), FileHeaderSize + 4), sci::ErrorCode::Format, text);
            }
        }

        // The file ends in the header. The buffer keeps the rest, as in
        // Decode_ShortPixels_IsFormat.
        TEST_METHOD(Decode_HeaderCutByTheEndOfTheFile_IsFormat)
        {
            std::vector<uint8_t> bytes = BytesOf(RawBmp());

            AssertFails(DecodeIndexedBmp(bytes.data(), FileHeaderSize + 20), sci::ErrorCode::Format, "header is not all in the file");
        }

        // The two OS/2 headers: 12 bytes (BITMAPCOREHEADER) and 64 bytes.
        TEST_METHOD(Decode_Os2Header_IsUnsupported)
        {
            for (uint32_t headerSize : { 12u, 64u })
            {
                RawBmp raw;
                raw.headerSize = headerSize;

                AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Unsupported, "OS/2");
            }
        }

        TEST_METHOD(WriteThenReadFile_GivesTheSameImage)
        {
            std::string path = _folder + "\\image.bmp";
            IndexedBmp image = TestImage(639, 7);

            AssertOk(WriteIndexedBmpFile(path, image), "write");
            sci::Result<IndexedBmp> read = ReadIndexedBmpFile(path);

            AssertOk(read, "read");
            Assert::AreEqual(639, read->width);
            Assert::AreEqual(7, read->height);
            Assert::IsTrue(image.pixels == read->pixels, L"the pixels");
            Assert::IsTrue(image.colors == read->colors, L"the colours");
            std::vector<uint8_t> bytes = ReadFileBytes(path);
            Assert::AreEqual((uint32_t)std::filesystem::file_size(path), U32At(bytes, 2), L"bfSize is the size of the file");
            AssertNotOpen(path);
        }

        // The file that the writer makes, as Windows reads it.
        TEST_METHOD(WriteFile_IsWhatWindowsReads)
        {
            std::string path = _folder + "\\image.bmp";
            IndexedBmp image = TestImage(639, 7);
            AssertOk(WriteIndexedBmpFile(path, image), "write");

            IndexedBmp windows;
            Assert::IsTrue(LoadWithWindows(path, windows), L"Windows reads the file as an 8-bit image");

            Assert::AreEqual(639, windows.width);
            Assert::AreEqual(7, windows.height);
            Assert::AreEqual(256, windows.colorCount);
            Assert::IsTrue(image.pixels == windows.pixels, Wide("the pixels: " + FirstDifference(image.pixels, windows.pixels)).c_str());
            Assert::IsTrue(image.colors == windows.colors, L"the colours");
        }

        // Files that another program wrote (the pic images of the pic
        // tests): the reader gives what Windows reads. Windows gives a pixel
        // the first table entry that has its colour (pic 220 has the same
        // colour in entries 207 and 227), so the test compares the colours
        // of the pixels, not their indices.
        TEST_METHOD(ReadFile_FileOfAnotherProgram_IsWhatWindowsReads)
        {
            for (const char *name : { "SCI2\\220.p56-vis.bmp", "SCI1.1\\100.p56-vis.bmp", "SCI0\\pic.006-vis.bmp" })
            {
                std::string path = GetTestFileDirectory("Pics") + "\\" + name;
                IndexedBmp windows;
                Assert::IsTrue(LoadWithWindows(path, windows), Wide("Windows reads " + path).c_str());

                sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);

                AssertOk(image, path);
                Assert::AreEqual(windows.width, image->width, Wide(path).c_str());
                Assert::AreEqual(windows.height, image->height, Wide(path).c_str());
                Assert::AreEqual(windows.colorCount, image->colorCount, Wide(path).c_str());
                Assert::IsTrue(windows.colors == image->colors, Wide("the colour table of " + path).c_str());
                Assert::AreEqual(windows.pixels.size(), image->pixels.size(), Wide(path).c_str());
                for (size_t i = 0; i < windows.pixels.size(); i++)
                {
                    if (windows.colors[windows.pixels[i]] != image->colors[image->pixels[i]])
                    {
                        Assert::Fail(Wide("the colour of pixel " + std::to_string(i) + " of " + path + ": the reader gives entry " + std::to_string(image->pixels[i]) +
                            ", and Windows gives entry " + std::to_string(windows.pixels[i])).c_str());
                    }
                }
            }
        }

        TEST_METHOD(ReadFile_MissingFile_IsNotFound)
        {
            std::string path = _folder + "\\missing.bmp";

            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);

            Assert::IsFalse(image.has_value());
            Assert::IsTrue(image.error().code == sci::ErrorCode::NotFound, Wide(image.error().ToString()).c_str());
            Assert::IsTrue(image.error().message.find(path) != std::string::npos, L"the error names the file");
            Assert::AreEqual(path, image.error().where.file);
        }

        TEST_METHOD(ReadFile_Folder_IsIo)
        {
            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(_folder);

            Assert::IsFalse(image.has_value());
            Assert::IsTrue(image.error().code == sci::ErrorCode::Io, Wide(image.error().ToString()).c_str());
            Assert::AreEqual(_folder, image.error().where.file);
        }

        // A file that another program has open for writing is not complete.
        TEST_METHOD(ReadFile_FileOpenForWriting_IsIo)
        {
            std::string path = _folder + "\\image.bmp";
            AssertOk(WriteIndexedBmpFile(path, TestImage(3, 2)), "write");
            HANDLE writer = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(writer != INVALID_HANDLE_VALUE, L"the test opens the file for writing");

            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);
            CloseHandle(writer);

            Assert::IsFalse(image.has_value());
            Assert::IsTrue(image.error().code == sci::ErrorCode::Io, Wide(image.error().ToString()).c_str());
        }

        TEST_METHOD(ReadFile_BadFile_NamesTheFile)
        {
            std::string path = _folder + "\\rle.bmp";
            RawBmp raw;
            raw.compression = 1;
            WriteFileBytes(path, BytesOf(raw));

            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);

            AssertFails(image, sci::ErrorCode::Unsupported, "compressed");
            Assert::AreEqual(path, image.error().where.file);
            AssertNotOpen(path);
        }

        // A file of one byte more than the limit. Its size is set with no
        // write of its bytes, and the reader refuses it before it reads them.
        TEST_METHOD(ReadFile_FileOverTheLimit_IsUnsupported)
        {
            std::string path = _folder + "\\large.bmp";
            HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(file != INVALID_HANDLE_VALUE, L"the test creates the file");
            LARGE_INTEGER size = {};
            size.QuadPart = (LONGLONG)IndexedBmpMaxFileSize + 1;
            BOOL sized = SetFilePointerEx(file, size, nullptr, FILE_BEGIN) && SetEndOfFile(file);
            CloseHandle(file);
            Assert::IsTrue(sized != FALSE, L"the test sets the size of the file");

            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);

            Assert::IsFalse(image.has_value());
            Assert::IsTrue(image.error().code == sci::ErrorCode::Unsupported, Wide(image.error().ToString()).c_str());
            Assert::AreEqual(path, image.error().where.file);
        }

        TEST_METHOD(WriteFile_MissingFolder_IsNotFound)
        {
            std::string path = _folder + "\\no-such-folder\\image.bmp";

            sci::Status written = WriteIndexedBmpFile(path, TestImage(3, 2));

            Assert::IsFalse(written.has_value());
            Assert::IsTrue(written.error().code == sci::ErrorCode::NotFound, Wide(written.error().ToString()).c_str());
        }
    };
}
