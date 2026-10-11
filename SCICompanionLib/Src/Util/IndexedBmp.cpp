#include "stdafx.h"
#include "IndexedBmp.h"
#include "FileWrite.h"
#include "format.h"

namespace
{
    // The parts of the file. The fields are read and written one byte at a
    // time, so the layout does not depend on the packing of a struct.
    const size_t FileHeaderSize = 14;       // BITMAPFILEHEADER
    const size_t InfoHeaderSize = 40;       // BITMAPINFOHEADER
    const size_t Os2HeaderSize = 12;        // BITMAPCOREHEADER
    const size_t Os2Header2Size = 64;       // OS22XBITMAPHEADER
    const size_t TableEntrySize = 4;        // blue, green, red, reserved
    const size_t TableEntries = 256;
    const uint32_t NotCompressed = 0;       // BI_RGB

    uint16_t ReadU16(const uint8_t *bytes)
    {
        return (uint16_t)(bytes[0] | (bytes[1] << 8));
    }

    uint32_t ReadU32(const uint8_t *bytes)
    {
        return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    }

    void WriteU16(std::vector<uint8_t> &out, uint16_t value)
    {
        out.push_back((uint8_t)(value & 0xff));
        out.push_back((uint8_t)(value >> 8));
    }

    void WriteU32(std::vector<uint8_t> &out, uint32_t value)
    {
        WriteU16(out, (uint16_t)(value & 0xffff));
        WriteU16(out, (uint16_t)(value >> 16));
    }

    // The bytes of a row in the file: the width, padded to 4 bytes.
    uint64_t RowSize(int width)
    {
        return ((uint64_t)width + 3) & ~(uint64_t)3;
    }
}

sci::Result<std::vector<uint8_t>> EncodeIndexedBmp(const IndexedBmp &image)
{
    if ((image.width < 1) || (image.width > IndexedBmpMaxDimension) || (image.height < 1) || (image.height > IndexedBmpMaxDimension))
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("a BMP image cannot have {0} x {1} pixels", image.width, image.height));
    }
    const uint64_t pixelCount = (uint64_t)image.width * (uint64_t)image.height;
    if ((uint64_t)image.pixels.size() != pixelCount)
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("an image of {0} x {1} pixels needs {2} bytes of pixels, and it has {3}", image.width, image.height, pixelCount, image.pixels.size()));
    }
    const uint64_t rowSize = RowSize(image.width);
    const uint64_t imageSize = rowSize * (uint64_t)image.height;
    const uint64_t pixelOffset = FileHeaderSize + InfoHeaderSize + (TableEntries * TableEntrySize);
    const uint64_t fileSize = pixelOffset + imageSize;
    if (fileSize > IndexedBmpMaxFileSize)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("a BMP file of {0} x {1} pixels is larger than {2} bytes", image.width, image.height, IndexedBmpMaxFileSize));
    }

    std::vector<uint8_t> out;
    out.reserve((size_t)fileSize);
    // BITMAPFILEHEADER
    out.push_back('B');
    out.push_back('M');
    WriteU32(out, (uint32_t)fileSize);
    WriteU16(out, 0);
    WriteU16(out, 0);
    WriteU32(out, (uint32_t)pixelOffset);
    // BITMAPINFOHEADER
    WriteU32(out, (uint32_t)InfoHeaderSize);
    WriteU32(out, (uint32_t)image.width);
    WriteU32(out, (uint32_t)image.height);  // more than 0: the rows go from the bottom up
    WriteU16(out, 1);                       // planes
    WriteU16(out, 8);                       // bits for each pixel
    WriteU32(out, NotCompressed);
    WriteU32(out, (uint32_t)imageSize);
    WriteU32(out, 0);                       // pixels for each metre, x
    WriteU32(out, 0);                       // pixels for each metre, y
    WriteU32(out, (uint32_t)TableEntries);  // colours in the table
    WriteU32(out, 0);                       // important colours: all
    for (const IndexedBmpColor &color : image.colors)
    {
        out.push_back(color.blue);
        out.push_back(color.green);
        out.push_back(color.red);
        out.push_back(0);
    }
    const size_t width = (size_t)image.width;
    const size_t padding = (size_t)rowSize - width;
    for (int y = image.height - 1; y >= 0; y--)
    {
        const uint8_t *row = image.pixels.data() + ((size_t)y * width);
        out.insert(out.end(), row, row + width);
        out.insert(out.end(), padding, 0);
    }
    return out;
}

sci::Result<IndexedBmp> DecodeIndexedBmp(const uint8_t *data, size_t size)
{
    // The signature, and the size field of the header after the file header.
    if ((size < FileHeaderSize + 4) || (data[0] != 'B') || (data[1] != 'M'))
    {
        return sci::Fail(sci::ErrorCode::Format, "not a BMP file");
    }
    const uint64_t pixelOffset = ReadU32(data + 10);
    const uint8_t *info = data + FileHeaderSize;
    const uint64_t headerSize = ReadU32(info);
    if ((headerSize == Os2HeaderSize) || (headerSize == Os2Header2Size))
    {
        return sci::Fail(sci::ErrorCode::Unsupported, "a BMP file with an OS/2 header; the file must have a Windows header (BITMAPINFOHEADER)");
    }
    if (headerSize < InfoHeaderSize)
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("a BMP header of {0} bytes", headerSize));
    }
    if ((uint64_t)size < FileHeaderSize + headerSize)
    {
        return sci::Fail(sci::ErrorCode::Format, "the BMP header is not all in the file");
    }
    const int64_t width = (int32_t)ReadU32(info + 4);
    const int64_t signedHeight = (int32_t)ReadU32(info + 8);
    const uint16_t planes = ReadU16(info + 12);
    const uint16_t bitCount = ReadU16(info + 14);
    const uint32_t compression = ReadU32(info + 16);
    const uint32_t colorsUsed = ReadU32(info + 32);
    if (bitCount != 8)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("a {0}-bit BMP file; the file must be 8-bit, with a colour table", bitCount));
    }
    if (compression != NotCompressed)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("a compressed BMP file (compression {0}); the file must not be compressed", compression));
    }
    if (planes != 1)
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("a BMP file with {0} planes", planes));
    }
    // A height below 0: the rows go from the top down.
    const bool topDown = (signedHeight < 0);
    const int64_t height = topDown ? -signedHeight : signedHeight;
    if ((width < 1) || (width > IndexedBmpMaxDimension) || (height < 1) || (height > IndexedBmpMaxDimension))
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("a BMP file of {0} x {1} pixels", width, signedHeight));
    }
    if (colorsUsed > TableEntries)
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("a BMP colour table of {0} entries; an 8-bit file has at most {1}", colorsUsed, TableEntries));
    }
    // 0: the table has all of its entries.
    const size_t colorCount = (colorsUsed == 0) ? TableEntries : (size_t)colorsUsed;
    const uint64_t tableOffset = FileHeaderSize + headerSize;
    const uint64_t tableEnd = tableOffset + (colorCount * TableEntrySize);
    if ((tableEnd > (uint64_t)size) || (pixelOffset < tableEnd))
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("the BMP colour table ({0} entries) is not all in the file before the pixels", colorCount));
    }
    const uint64_t rowSize = RowSize((int)width);
    const uint64_t imageSize = rowSize * (uint64_t)height;
    if (pixelOffset + imageSize > (uint64_t)size)
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("the BMP pixels are not all in the file: {0} x {1} pixels need {2} bytes from offset {3}, and the file has {4} bytes", width, height, imageSize, pixelOffset, size));
    }

    IndexedBmp image;
    image.width = (int)width;
    image.height = (int)height;
    image.colorCount = (int)colorCount;
    const uint8_t *table = data + (size_t)tableOffset;
    for (size_t i = 0; i < colorCount; i++)
    {
        const uint8_t *entry = table + (i * TableEntrySize);
        image.colors[i].blue = entry[0];
        image.colors[i].green = entry[1];
        image.colors[i].red = entry[2];
    }
    const size_t rowWidth = (size_t)width;
    image.pixels.resize(rowWidth * (size_t)height);
    const uint8_t *rows = data + (size_t)pixelOffset;
    for (size_t y = 0; y < (size_t)height; y++)
    {
        const size_t fileRow = topDown ? y : ((size_t)height - 1 - y);
        const uint8_t *row = rows + (fileRow * (size_t)rowSize);
        std::copy(row, row + rowWidth, image.pixels.begin() + (y * rowWidth));
    }
    return image;
}

sci::Result<IndexedBmp> DecodeIndexedBmp(const std::vector<uint8_t> &data)
{
    return DecodeIndexedBmp(data.data(), data.size());
}

sci::Status WriteIndexedBmpFile(const std::string &path, const IndexedBmp &image)
{
    SCI_TRY_ASSIGN(std::vector<uint8_t> bytes, EncodeIndexedBmp(image));
    return WriteBytesToFile(path, bytes);
}

sci::Result<IndexedBmp> ReadIndexedBmpFile(const std::string &path)
{
    const std::string what = "Reading " + path;
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        return sci::Fail(sci::FromWin32(error, what));
    }
    std::vector<uint8_t> bytes;
    LARGE_INTEGER fileSize = {};
    DWORD error = ERROR_SUCCESS;
    bool tooLarge = false;
    if (!GetFileSizeEx(file, &fileSize))
    {
        error = GetLastError();
    }
    else if ((uint64_t)fileSize.QuadPart > IndexedBmpMaxFileSize)
    {
        tooLarge = true;
    }
    else
    {
        bytes.resize((size_t)fileSize.QuadPart);
        DWORD read = 0;
        if (!bytes.empty() && !ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr))
        {
            error = GetLastError();
        }
        // A file that became shorter after the size was read.
        bytes.resize(read);
    }
    CloseHandle(file);
    if (error != ERROR_SUCCESS)
    {
        return sci::Fail(sci::FromWin32(error, what));
    }
    sci::ErrorLocation where;
    where.file = path;
    if (tooLarge)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("the file is larger than {0} bytes", IndexedBmpMaxFileSize), where);
    }
    sci::Result<IndexedBmp> image = DecodeIndexedBmp(bytes);
    if (!image)
    {
        image.error().where = where;
    }
    return image;
}
