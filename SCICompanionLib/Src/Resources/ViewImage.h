#pragma once

// The image data of a view: the byte layout of its headers, what the view
// has (its loops, its cels and its palette), and the pixels of a cel. The
// functions take the bytes of the view resource. They read the view formats
// whose headers have the layout of SCI1.1 (ViewFormat::VGA1_1) and SCI2
// (ViewFormat::VGA2); another format gives Unsupported.
//
// The layout of such a view:
//  - a view header: its size at offset 0 (2 bytes; the header has 2 bytes
//    more), the count of loops (2), the scale flags (3), the resolution of
//    an SCI2 view (5), the offset of the palette (8, 4 bytes; 0: no
//    palette), the size of a loop header (12) and of a cel header (13);
//  - a loop header for each loop, after the view header: the loop that it
//    mirrors (0, 0xff: no mirror), the count of its cels (2), and the
//    offset of its first cel header (12, 4 bytes);
//  - the cel headers of a loop that is not a mirror, one after the other:
//    the width (0), the height (2), x (4) and y (6) as signed words, the
//    skip colour (8), the compression byte (9), the offsets of the
//    run-length data (24) and of the literal data (28; 0: the cel is raw,
//    and its pixels are at the offset of the run-length data), and the
//    offset of the row table of an SCI2 cel (32).

#include "PicImage.h"
#include "Result.h"
#include "Version.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ResourceEntity;

// The header of one cel.
struct ViewCelLayout
{
    // The offset of the cel header in the data.
    uint32_t headerOffset = 0;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    uint8_t skipColor = 0;
    // Byte 9 of the header: 0x0a or 0x8a for a cel with run-length data.
    uint8_t compression = 0;
    uint32_t rleOffset = 0;
    uint32_t literalOffset = 0;
    uint32_t rowTableOffset = 0;

    // A cel with no literal data: its pixels are stored as they are.
    bool IsRaw() const { return literalOffset == 0; }
};

struct ViewLoopLayout
{
    // The offset of the loop header in the data.
    uint32_t headerOffset = 0;
    // The loop that this loop mirrors; -1 for a loop that is not a mirror.
    int mirrorOf = -1;
    // The count of the cels of the loop header. A mirror has no cel headers
    // of its own: the reader gives it the cels of the loop that it mirrors.
    int celCount = 0;
    uint32_t celOffset = 0;
    // The cel headers of a loop that is not a mirror.
    std::vector<ViewCelLayout> cels;
};

struct ViewLayout
{
    // The bytes of the view header: the size word + 2.
    uint32_t headerSize = 0;
    uint8_t scaleFlags = 0;
    uint8_t resolution = 0;
    uint32_t paletteOffset = 0;
    uint8_t loopHeaderSize = 0;
    uint8_t celHeaderSize = 0;
    std::vector<ViewLoopLayout> loops;
};

// The layout of the headers of a view of the form of SCI1.1 and SCI2, as the
// reader of the views finds them. Format when a header is not in the data,
// when a loop header is smaller than 16 bytes, when a cel header is smaller
// than 36 bytes, and when a mirror names a loop that the view does not
// have; Unsupported for a view with the form of an
// SCI1 view (a loop header of more than 32 bytes, which the reader gives to
// the reader of SCI1 views).
sci::Result<ViewLayout> ReadViewLayout(const uint8_t *data, size_t size);

struct ViewCelDescription
{
    int loop = 0;
    int cel = 0;
    int width = 0;
    int height = 0;
    // The placement of the cel, as its header has it.
    int x = 0;
    int y = 0;
    uint8_t skipColor = 0;
    // The cel has no run-length data (ViewCelLayout::IsRaw).
    bool raw = false;
    // For each palette entry, the count of the pixels that have it.
    std::array<uint32_t, 256> pixelCounts = {};

    // The count of the pixels that have the skip colour.
    uint32_t SkipPixels() const { return pixelCounts[skipColor]; }
    // The entries that a pixel has, without the skip colour.
    std::array<bool, 256> PixelEntries() const;
};

struct ViewLoopDescription
{
    int index = 0;
    // The loop that this loop mirrors; -1 for a loop that is not a mirror.
    int mirrorOf = -1;
    // The cels of the loop; a mirror has those of the loop that it mirrors.
    int cels = 0;
};

struct ViewDescription
{
    ViewFormat format = ViewFormat::EGA;
    // Byte 3 and byte 5 of the view header.
    uint8_t scaleFlags = 0;
    uint8_t resolution = 0;
    std::vector<ViewLoopDescription> loops;
    // The cels of the loops that are not mirrors, in the order of the loops.
    std::vector<ViewCelDescription> cels;
    // The palette of the view. A stored 3-byte entry has the flag byte of
    // the palette header, as the SCI32 interpreter reads it.
    PicPaletteDescription palette;

    // The count of the loops that are mirrors.
    int MirrorLoops() const;
    // The count of the raw cels.
    int RawCels() const;
    // The entries of the pixels of all cels, without the skip colour of
    // each cel.
    std::array<bool, 256> PixelEntries() const;
    // The cel of the loop and cel numbers; null for a cel that the view
    // does not store (also each cel of a mirror).
    const ViewCelDescription *Find(int loop, int cel) const;
};

// "ega", "vga1", "vga11" or "vga2".
const char *ViewFormatName(ViewFormat format);

// True for the view formats whose image these functions read (vga11 and
// vga2).
bool CanReadViewImage(ViewFormat format);

// The view as SCI Companion reads it. Format for data that is not a view
// (the error has no resource number: the caller names the view).
sci::Result<std::unique_ptr<ResourceEntity>> ParseViewResource(const SCIVersion &version, const std::vector<uint8_t> &data);

// What the view has: the layout of its headers, the pixels of its cels as
// the reader of the views gives them, and its palette. Unsupported for a
// view format that these functions do not read; else the errors of
// ReadViewLayout and ParseViewResource; Format when the reader gives other
// loops or cels than the headers have, and for a palette that cannot be
// read.
sci::Result<ViewDescription> DescribeView(const SCIVersion &version, const std::vector<uint8_t> &data);

// The pixels of a cel: width * height bytes, the top row first, with no
// padding. Usage for a cel that the view does not store (also a cel of a
// mirror); else as DescribeView.
sci::Result<std::vector<uint8_t>> GetViewCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, int loop, int cel);
