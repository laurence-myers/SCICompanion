#pragma once

// The image data of a view: the byte layout of its headers, what the view
// has (its loops, its cels and its palette), and the pixels of a cel. The
// functions take the bytes of the view resource. They read the view formats
// whose headers have the layout of SCI1.1 (ViewFormat::VGA1_1) and SCI2
// (ViewFormat::VGA2); another format gives Unsupported.
//
// The layout of such a view:
//  - a view header: its size at offset 0 (2 bytes; the header has 2 bytes
//    more), the count of loops (2), the scale flags (3), a byte that is 1
//    in most views (4), the resolution of an SCI2 view (5), the offset of
//    the palette (8, 4 bytes; 0: no palette), the size of a loop header
//    (12) and of a cel header (13), and the native width and height (14
//    and 16, 2 bytes each; SCI Companion shows the view in that size);
//  - a loop header for each loop, after the view header: the loop that it
//    mirrors (0, 0xff: no mirror), the flag that the game flips it with
//    (1), the count of its cels (2; a loop that is not a mirror can have no
//    cel), and the offset of its first cel header (12, 4 bytes);
//  - the cel headers of a loop that is not a mirror, one after the other:
//    the width (0), the height (2), x (4) and y (6) as signed words, the
//    skip colour (8), the compression byte (9), the offsets of the
//    run-length data (24) and of the literal data (28; 0: the cel is raw,
//    and its pixels are at the offset of the run-length data), and the
//    offset of the row table of an SCI2 cel (32).
//
// A mirror is a loop that shows the cels of another loop, as the reader of
// the views of SCI Companion reads it: byte 0 of its header is not 0xff.
// The SCI32 interpreter flips such a loop only when byte 1 of its header is
// 1, and takes the count of the cels from the header of the other loop.

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
    uint8_t byte4 = 0;
    uint8_t resolution = 0;
    uint32_t paletteOffset = 0;
    uint8_t loopHeaderSize = 0;
    uint8_t celHeaderSize = 0;
    uint16_t nativeWidth = 0;
    uint16_t nativeHeight = 0;
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
    // The cels of the loop. A mirror has the count of the header of the loop
    // that it mirrors.
    int cels = 0;
};

struct ViewDescription
{
    ViewFormat format = ViewFormat::EGA;
    // Bytes 3, 4 and 5 of the view header, and its native size.
    uint8_t scaleFlags = 0;
    uint8_t byte4 = 0;
    uint8_t resolution = 0;
    uint16_t nativeWidth = 0;
    uint16_t nativeHeight = 0;
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
// the reader of the views gives them, and its palette. A loop that is not a
// mirror and whose header has no cel has no cel here (the reader gives it a
// 1 x 1 cel). Unsupported for a view format that these functions do not
// read; else the errors of ReadViewLayout and ParseViewResource; Format when
// the reader repairs damaged data (it logs a warning or an error and puts
// data of its own in the place of the damaged data, for example a cel for a
// mirror loop that gets no cel), when the reader gives other loops or cels
// than the headers have, and for a palette that cannot be read.
sci::Result<ViewDescription> DescribeView(const SCIVersion &version, const std::vector<uint8_t> &data);

// The pixels of a cel: width * height bytes, the top row first, with no
// padding. Usage for a cel that the view does not store (also a cel of a
// mirror, and of a loop whose header has no cel); else as DescribeView.
sci::Result<std::vector<uint8_t>> GetViewCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, int loop, int cel);

// New pixels for one stored cel, the top row first: width * height bytes of
// the cel, or, with newSize, of the new size of the cel.
struct ViewCelPixels
{
    int loop = 0;
    int cel = 0;
    std::vector<uint8_t> pixels;
    // The cel gets width, height, x and y in its header.
    bool newSize = false;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
};

// The run-length data of the pixels of a cel (width * height bytes, the top
// row first), as Sierra's views of SCI1.1 and SCI2 have it: each row is its
// own runs (a run of the skip colour, a run of one colour, or literal
// pixels; 63 pixels or fewer), and the row table gives the start of each
// row in the run-length data and in the literal data.
struct EncodedViewCel
{
    std::vector<uint8_t> rle;
    std::vector<uint8_t> literal;
    // For each row: the offset of its first byte in rle, then in literal.
    std::vector<uint32_t> rleRows;
    std::vector<uint32_t> literalRows;
};
// fillRun is the shortest run of one colour (not the skip colour) that gets
// a run code; a shorter run is literal pixels. Sierra's encoders used 2 or
// 3. Usage when the pixels are not width * height bytes, or fillRun is not
// 2 or more.
sci::Result<EncodedViewCel> EncodeViewCel(int width, int height, uint8_t skipColor, const std::vector<uint8_t> &pixels, int fillRun = 3);

// The bytes of the view with new pixels in some of its stored cels. The view
// changes in place, so that the bytes that the readers of SCI Companion do
// not read stay as Sierra wrote them:
//  - a cel whose pixels, size and placement do not change keeps its bytes;
//  - a cel that gets another size or placement (newSize) gets them in bytes
//    0 to 7 of its header; a cel with only another placement keeps its
//    data;
//  - a changed cel with run-length data whose data no other data of the
//    view has, whose size does not change, and whose new pixels have the
//    runs of its codes (each run
//    of the skip colour is still the skip colour, each run of one colour
//    is still one colour that is not the skip colour, and a literal pixel
//    has the skip colour only where the old pixel has it; for example two
//    colours that swap), keeps its codes, its row table and its sizes:
//    only its literal data changes, in its place;
//  - another changed cel whose data no other data of the view has (the
//    headers, the palette, the data of another cel header) takes the
//    place of its old data when it fits there: the pixels of a raw cel, or
//    the run-length data, the literal data and the row table of an SCI2
//    cel whose header has one (EncodeViewCel, with runs of 3, 2 or 4
//    pixels and more as run codes: the first form that fits) that are not
//    longer than the old ones (a row table of no more rows);
//  - each other changed cel gets its new data (the shortest form) after
//    the last byte of the view, and its cel header gets the new offsets
//    (24, 28 and, for an SCI2 cel with a row table, 32). The old data
//    stays: another cel header can point at it.
// The header of a changed cel with new run-length codes gets the new sizes
// (12: the run-length and the literal data; 16: the run-length data), and
// that of a raw cel of another size the new size of its pixels (12). No
// other byte changes. So a view that is imported again from its own patch file
// can grow, when the new data of a cel is longer than the data before it.
// The new bytes must then read back (DescribeView) with the same loops, the
// sizes and placements of the cels (the new ones of newSize), the same skip
// colours, the new pixels in the changed cels and the old pixels in the
// others, the same pixels through the row tables of SCI2, and the same
// palette bytes. A mirror loop shows the cels of the loop that it mirrors,
// so it shows the new cels too.
//
// Usage for a cel that the view does not store, for two pixel sets for one
// cel, for a new size of 0 or more than 65535 pixels in a direction, for a
// new placement that is not in -32768 to 32767, and for pixels that are not
// width * height bytes; Unsupported for a
// changed cel whose header is also the header of another cel (two loops
// that point at the same cel headers), for a changed cel whose header has
// the bit 0x80 in byte 10 (the interpreters then take the skip and remap
// colours of the pixels from that byte), and for an SCI1.1 view that would
// have more than 65535 bytes (Sierra's interpreter of SCI1.1 reads 16-bit
// sizes and offsets); Internal when the new bytes do not read back as they
// must; else the errors of DescribeView and of ReadPaletteLayout.
sci::Result<std::vector<uint8_t>> ReplaceViewCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, const std::vector<ViewCelPixels> &cels);

// The pixels of each stored cel, in the order of ViewDescription::cels (the
// loops that are not mirrors, in order), as GetViewCelPixels gives them. The
// errors of DescribeView.
sci::Result<std::vector<std::vector<uint8_t>>> GetAllViewCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data);

// A cel of a new view: its pixels are width * height bytes, the top row
// first.
struct NewViewCel
{
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    uint8_t skipColor = 0;
    std::vector<uint8_t> pixels;
    // The pixels are stored as they are, with no run-length data (Sierra's
    // magnifier cels).
    bool raw = false;
};

struct NewViewLoop
{
    // The loop that this loop mirrors (it can be a mirror too); -1 for a
    // loop with cels of its own.
    int mirrorOf = -1;
    // The cels of a loop that is not a mirror (some of Sierra's views have
    // such loops with no cel); none for a mirror.
    std::vector<NewViewCel> cels;
    // For a mirror: the cel count of its own header. The interpreters give
    // it to a mirror of this mirror (Sierra's views have 0 there, so such a
    // mirror shows no cel). -1: the count of the loop at the end of its
    // chain.
    int headerCels = -1;
};

// The loop that loop shows the cels of: the loop itself, else the end of its
// chain of mirrors (mirrorOf has the loop that each loop mirrors, -1 for a
// loop that is not a mirror). -1 for a mirror of a loop that is not there or
// of itself, and for a chain with a cycle.
int MirroredLoop(const std::vector<int> &mirrorOf, size_t loop);

// A new view (scic view add).
struct NewView
{
    // Bytes 3, 4 and 5 of the view header.
    uint8_t scaleFlags = 0;
    uint8_t byte4 = 1;
    uint8_t resolution = 0;
    // Bytes 14 to 17 of the view header (many of Sierra's views have 0).
    uint16_t nativeWidth = 0;
    uint16_t nativeHeight = 0;
    std::vector<NewViewLoop> loops;
    // The palette of the view (MakePaletteBytes); not present: no palette.
    PicPaletteDescription palette;
};

// The bytes of a new view of SCI1.1 or SCI2. The view writer of SCI Companion
// writes the headers and the cels (ViewWriteToVGA11 or ViewWriteToVGA2: 0x0a
// or 0x8a as byte 9 of each cel header, and a row table for each SCI2 cel);
// then bytes 3, 4, 5 and 14 to 17 of the view header get the values of the
// view. The pixels of a raw cel go after the last byte, and its header (where
// the writer has a cel of 1 pixel) gets them as Sierra's raw cels have
// them: 0 as byte 9 and as the offsets of the literal data and of the row
// table, the offset of the pixels as the offset of the run-length data,
// width * height as the size of its data (12), and 0 as the size of its
// run-length data (16).
// Then the palette (MakePaletteBytes) goes after the last byte, at the
// offset of bytes 8 to 11. A mirror loop gets the flag 1 in byte 1 of its
// header (the SCI32 interpreter flips it), the cel offset (bytes 12 to 15)
// of the loop at the end of its chain of mirrors (MirroredLoop), and as its
// cel count (byte 2) its headerCels, else the count of that loop: the
// interpreters read the count and the offset from the header of the loop
// that a mirror names, which can be a mirror too. The bytes must read back
// (DescribeView, GetAllViewCelPixels) with the loops, the mirrors and their
// counts, flags and cel offsets, the cels (sizes, placements, skip colours,
// raw or not, compression bytes, pixels, also through the row table of each
// SCI2 cel that is not raw), the header bytes and the palette of the view.
//
// Usage for no loop or more than 255 loops; for a mirror whose chain does
// not end at a loop that is not a mirror (a loop that is not there, the
// mirror itself, a cycle), or that has cels; for a headerCels of a loop
// that is not a mirror, or of more than the count of the loop at the end of
// the chain; for a loop with more than 255 cels; for a cel with a
// width or a height of 0 or more than 65535 pixels, a placement that is not
// in -32768 to 32767, or pixels that are not width * height bytes; and the
// errors of MakePaletteBytes. Unsupported for a view format that is not
// vga11 or vga2, for a cel too wide for the writer, and for an SCI1.1 view
// that would have more than 65535 bytes. Internal when the bytes do not
// read back as they must.
sci::Result<std::vector<uint8_t>> MakeNewViewBytes(const SCIVersion &version, const NewView &view);
