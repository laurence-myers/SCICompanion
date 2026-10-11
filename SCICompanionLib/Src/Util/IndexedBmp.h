#pragma once

// An 8-bit BMP file: each pixel is an index into a colour table. The pic
// commands of scic move the pixels of a cel out of a game and back with
// these files, so the indices must stay exact: the reader takes only a file
// with 8 bits for each pixel and no compression, and it changes no index.

#include "Result.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct IndexedBmpColor
{
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

inline bool operator==(const IndexedBmpColor &a, const IndexedBmpColor &b)
{
    return (a.red == b.red) && (a.green == b.green) && (a.blue == b.blue);
}
inline bool operator!=(const IndexedBmpColor &a, const IndexedBmpColor &b)
{
    return !(a == b);
}

struct IndexedBmp
{
    int width = 0;
    int height = 0;
    // One byte for each pixel, with no padding: the top row first, and each
    // row from the left. The size is width * height.
    std::vector<uint8_t> pixels;
    // The colour table. The entries that a file does not have are black.
    std::array<IndexedBmpColor, 256> colors = {};
    // The count of table entries of the file that was read (1 to 256). The
    // writer does not use it: it writes 256 entries.
    int colorCount = 256;
};

// The largest width and height of an image.
const int IndexedBmpMaxDimension = 65535;
// The largest file that the reader takes.
const size_t IndexedBmpMaxFileSize = 64 * 1024 * 1024;

// The bytes of a BMP file for the image: a BITMAPFILEHEADER with the size of
// the file, a BITMAPINFOHEADER, 256 table entries, then the rows from the
// bottom up, each padded to 4 bytes. Usage when the width or the height is
// not 1 to IndexedBmpMaxDimension, or the pixels are not width * height
// bytes; Unsupported when the file would be larger than
// IndexedBmpMaxFileSize.
sci::Result<std::vector<uint8_t>> EncodeIndexedBmp(const IndexedBmp &image);

// The image of the bytes of a BMP file. It takes a file with a
// BITMAPINFOHEADER or a later header (V4, V5), rows from the bottom up or
// from the top down, and a table of 1 to 256 entries. Unsupported for a
// file that is not 8-bit (the message gives its bit count), for a
// compressed file (RLE8), and for an OS/2 header; Format for bytes that are
// not a BMP file, for a table or pixels that are not all in the file, and
// for a bad size. The reader does not compare a pixel with the count of
// table entries.
sci::Result<IndexedBmp> DecodeIndexedBmp(const uint8_t *data, size_t size);
sci::Result<IndexedBmp> DecodeIndexedBmp(const std::vector<uint8_t> &data);

// Creates or replaces the file (a path in the ANSI code page) with the
// image. The errors of EncodeIndexedBmp and of WriteBytesToFile.
sci::Status WriteIndexedBmpFile(const std::string &path, const IndexedBmp &image);

// Reads the file. NotFound when there is no such file; Io when it cannot be
// read; Unsupported when it is larger than IndexedBmpMaxFileSize; else the
// errors of DecodeIndexedBmp. The error has the file as its location.
sci::Result<IndexedBmp> ReadIndexedBmpFile(const std::string &path);
