#include "stdafx.h"
#include "CppUnitTest.h"
#include "TestSupport.h"
#include "IndexedBmp.h"
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
            // The rows as the file has them, with their padding. The default
            // is 3 x 2 pixels, from the bottom up: the top row is 1, 2, 3.
            std::vector<uint8_t> pixelData = { 4, 5, 6, 0, 1, 2, 3, 0 };
        };

        std::vector<uint8_t> BytesOf(const RawBmp &raw)
        {
            std::vector<uint8_t> bytes;
            const uint32_t pixelOffset = (uint32_t)(FileHeaderSize + raw.headerSize + (raw.tableEntries * 4));
            bytes.push_back('B');
            bytes.push_back('M');
            PutU32(bytes, pixelOffset + (uint32_t)raw.pixelData.size());
            PutU16(bytes, 0);
            PutU16(bytes, 0);
            PutU32(bytes, pixelOffset);
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
            bytes.insert(bytes.end(), raw.pixelData.begin(), raw.pixelData.end());
            return bytes;
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

    // The 8-bit BMP reader and writer of the pic commands.
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
            Assert::AreEqual((uint32_t)EncodedPixelOffset, U32At(bytes, 10), L"bfOffBits");
            Assert::AreEqual((uint32_t)InfoHeaderSize, U32At(bytes, 14), L"biSize");
            Assert::AreEqual((uint32_t)3, U32At(bytes, 18), L"biWidth");
            Assert::AreEqual((uint32_t)2, U32At(bytes, 22), L"biHeight");
            Assert::AreEqual(1, (int)U16At(bytes, 26), L"biPlanes");
            Assert::AreEqual(8, (int)U16At(bytes, 28), L"biBitCount");
            Assert::AreEqual((uint32_t)0, U32At(bytes, 30), L"biCompression");
            Assert::AreEqual((uint32_t)8, U32At(bytes, 34), L"biSizeImage");
            Assert::AreEqual((uint32_t)256, U32At(bytes, 46), L"biClrUsed");
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
            sci::Result<std::vector<uint8_t>> encoded = EncodeIndexedBmp(noWidth);
            Assert::IsFalse(encoded.has_value());
            Assert::IsTrue(encoded.error().code == sci::ErrorCode::Usage, Wide(encoded.error().ToString()).c_str());

            IndexedBmp shortPixels = TestImage(3, 2);
            shortPixels.pixels.pop_back();
            encoded = EncodeIndexedBmp(shortPixels);
            Assert::IsFalse(encoded.has_value());
            Assert::IsTrue(encoded.error().code == sci::ErrorCode::Usage, Wide(encoded.error().ToString()).c_str());
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
            RawBmp raw;
            raw.colorsUsed = 300;
            raw.tableEntries = 300;

            AssertFails(DecodeIndexedBmp(BytesOf(raw)), sci::ErrorCode::Format, "300 entries");
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
            RawBmp noWidth;
            noWidth.width = 0;
            AssertFails(DecodeIndexedBmp(BytesOf(noWidth)), sci::ErrorCode::Format, "0 x 2 pixels");

            RawBmp negativeWidth;
            negativeWidth.width = -3;
            AssertFails(DecodeIndexedBmp(BytesOf(negativeWidth)), sci::ErrorCode::Format, "-3 x 2 pixels");

            RawBmp noHeight;
            noHeight.height = 0;
            AssertFails(DecodeIndexedBmp(BytesOf(noHeight)), sci::ErrorCode::Format, "3 x 0 pixels");

            // The height that has no positive value.
            RawBmp lowestHeight;
            lowestHeight.height = INT_MIN;
            AssertFails(DecodeIndexedBmp(BytesOf(lowestHeight)), sci::ErrorCode::Format, "pixels");
        }

        TEST_METHOD(Decode_NotABmp_IsFormat)
        {
            std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 3 };
            AssertFails(DecodeIndexedBmp(png), sci::ErrorCode::Format, "not a BMP file");

            AssertFails(DecodeIndexedBmp(std::vector<uint8_t>()), sci::ErrorCode::Format, "not a BMP file");

            // The signature and no more.
            AssertFails(DecodeIndexedBmp(std::vector<uint8_t>({ 'B', 'M' })), sci::ErrorCode::Format, "not a BMP file");
        }

        // The file ends in the header. The buffer keeps the rest, as in
        // Decode_ShortPixels_IsFormat.
        TEST_METHOD(Decode_HeaderCutByTheEndOfTheFile_IsFormat)
        {
            std::vector<uint8_t> bytes = BytesOf(RawBmp());

            AssertFails(DecodeIndexedBmp(bytes.data(), FileHeaderSize + 20), sci::ErrorCode::Format, "header is not all in the file");
        }

        TEST_METHOD(Decode_Os2Header_IsUnsupported)
        {
            RawBmp raw;
            raw.headerSize = 12;
            std::vector<uint8_t> bytes = BytesOf(raw);

            AssertFails(DecodeIndexedBmp(bytes), sci::ErrorCode::Unsupported, "OS/2");
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
        }

        TEST_METHOD(ReadFile_MissingFile_IsNotFound)
        {
            std::string path = _folder + "\\missing.bmp";

            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);

            Assert::IsFalse(image.has_value());
            Assert::IsTrue(image.error().code == sci::ErrorCode::NotFound, Wide(image.error().ToString()).c_str());
            Assert::IsTrue(image.error().message.find(path) != std::string::npos, L"the error names the file");
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
