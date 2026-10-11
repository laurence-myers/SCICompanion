#pragma once

// The image data of a VGA pic: what the pic has (its cels and its palette),
// the pixels of a cel, and a copy of the resource with new pixels or new
// colours. The functions take and give the bytes of the pic resource. They
// read the pic formats that hold cels: SCI1.1 (PicFormat::VGA1_1, one cel)
// and SCI2 (PicFormat::VGA2); another format gives Unsupported.
//
// An SCI2 pic changes in place: the new bytes are the old bytes, with other
// values only in the pixels of the cel or in the colours of the palette
// range. That needs a pic whose parts each have their own bytes: a pic with
// a compressed cel, or with a cel whose pixel bytes are also bytes of
// another cel or of the palette, gives Unsupported.
//
// An SCI1.1 pic keeps its cel as run-length data, so new pixels make a new
// resource, which SCI Companion writes as it saves a pic. Other bytes can
// change (the form of the palette, the encoding of the drawing commands),
// but the new resource must read back as the same pic: the same size,
// commands, priority bars, cel and palette. A pic that the writer does not
// keep gives Unsupported.

#include "IndexedBmp.h"
#include "Result.h"
#include "Version.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// A run of palette entries: first to last.
struct IndexRun
{
    int first = 0;
    int last = 0;
};

inline bool operator==(const IndexRun &a, const IndexRun &b)
{
    return (a.first == b.first) && (a.last == b.last);
}

// The runs of the entries that are set, from the lowest.
std::vector<IndexRun> IndexRunsOf(const std::array<bool, 256> &entries);
// "0-12,80-235"; one entry is "7"; no entry is "".
std::string IndexRunsText(const std::vector<IndexRun> &runs);

struct PicCelDescription
{
    int index = 0;
    int width = 0;
    int height = 0;
    // The placement of the cel in the pic, as its header has it.
    int x = 0;
    int y = 0;
    // The priority of an SCI2 cel; 0 for the cel of an SCI1.1 pic.
    int priority = 0;
    uint8_t skipColor = 0;
    // An SCI2 cel with run-length data: these functions do not read or
    // change its pixels, and pixelCounts is all 0.
    bool compressed = false;
    // For each palette entry, the count of the pixels that have it.
    std::array<uint32_t, 256> pixelCounts = {};

    // The count of the pixels that have the skip colour.
    uint32_t SkipPixels() const { return pixelCounts[skipColor]; }
    // The entries that a pixel has, the skip colour too.
    std::array<bool, 256> UsedEntries() const;
};

struct PicPaletteDescription
{
    // False: the pic has no palette, and the other members are 0.
    bool present = false;
    // The entries that the pic stores: firstEntry to
    // firstEntry + entryCount - 1.
    int firstEntry = 0;
    int entryCount = 0;
    // The bytes of a stored entry: 4 (a flag byte and a colour) or 3 (a
    // colour).
    int entrySize = 0;
    // For each entry, its flag byte: 0 for an entry that the pic does not
    // store, and for a stored entry that the pic does not use (the game
    // then takes its colour from another palette). A 3-byte entry has no
    // flag byte and counts as 1.
    std::array<uint8_t, 256> flags = {};
    // The colour of each stored entry; black for the others.
    std::array<IndexedBmpColor, 256> colors = {};

    // The entries whose flag is not 0.
    std::array<bool, 256> UsedEntries() const;
};

struct PicDescription
{
    PicFormat format = PicFormat::EGA;
    // The size of the pic, as SCI Companion shows it.
    int width = 0;
    int height = 0;
    std::vector<PicCelDescription> cels;
    // The cel with the largest area (the first of them); -1: no cel.
    int backgroundCel = -1;
    PicPaletteDescription palette;
};

// "ega", "vga1", "vga11" or "vga2".
const char *PicFormatName(PicFormat format);

// What the pic has. Unsupported for a pic format that these functions do
// not read; Format for data that is not a pic of the format of the version,
// and for a pic whose palette cannot be read.
sci::Result<PicDescription> DescribePic(const SCIVersion &version, const std::vector<uint8_t> &data);

// The pixels of a cel: width * height bytes, the top row first, with no
// padding. Each byte is a palette entry. Usage for a cel that the pic does
// not have; Unsupported for a compressed SCI2 cel; Format for data that is
// not a pic.
sci::Result<std::vector<uint8_t>> GetCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, int cel);

// The bytes of the pic with new pixels in a cel (width * height bytes, the
// top row first). Usage for a cel that the pic does not have, and for a
// count of pixels that is not that of the cel; Unsupported for a pic that
// cannot change as the top of this file says; else as DescribePic.
sci::Result<std::vector<uint8_t>> ReplaceCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, int cel, const std::vector<uint8_t> &pixels);

// The bytes of the pic with the colours of the entries first to last in its
// palette. Only the red, the green and the blue of those entries change:
// the flag bytes and the count of the stored entries stay. Usage when the
// pic has no palette, and when first to last is not a range of entries that
// the pic stores; Unsupported for an SCI2 pic that cannot change in place;
// else as DescribePic.
sci::Result<std::vector<uint8_t>> ReplacePaletteRange(const SCIVersion &version, const std::vector<uint8_t> &data, int first, int last, const std::array<IndexedBmpColor, 256> &colors);
