#pragma once

// The palette of a view in the bytes of its resource: a copy of the resource
// with new colours in a range of palette entries, or with no "used" mark in
// a range of palette entries. The functions take the views whose palette
// has the place that ReadViewPaletteOffsetVGA11 gives: SCI1.1
// (ViewFormat::VGA1_1) and SCI2 (ViewFormat::VGA2); another view format
// gives Unsupported.
//
// A view changes in place: the new bytes are the old bytes, with other
// values only in the bytes of the palette that each function names. The
// size of the resource stays, and the view reads back with the same cels.
//
// In an SCI2 game, a view that the game draws gives each entry that its
// palette marks as used to the palette of the screen. So a view with the
// old colours of a room puts them back into a pic that got new colours.

#include "IndexedBmp.h"
#include "PaletteOperations.h"
#include "Result.h"
#include "Version.h"
#include <array>
#include <cstdint>
#include <vector>

struct RasterComponent;

// True when the two views have the same loops, and each cel has the same
// size, placement, transparent colour and pixels. The bytes of a row after
// its width (the padding of its stride) are not pixels: the reader of the
// views gives them no value in the cels of a mirrored loop, so two reads of
// one view can have other bytes there.
bool SameViewCels(const RasterComponent &one, const RasterComponent &two);

// A view with a change of its palette.
struct ViewPaletteEdit
{
    // The bytes of the view resource after the change.
    std::vector<uint8_t> data;
    // The entries that changed. ReplaceViewPaletteColors: the entries that
    // got the new colour (also when it is the colour that the entry had).
    // ClearViewPaletteMarks: the entries of ClearPaletteMarks.
    std::array<bool, 256> entries = {};
    // The entries that the palette marked as used before the change
    // (ReadUsedPaletteEntries).
    std::array<bool, 256> usedBefore = {};
    // What the palette stores, before and after the change.
    PaletteLayout before;
    PaletteLayout after;

    // True when an entry changed.
    bool AnyEntry() const;
};

// The view with the colours of the entries first to last in its palette, for
// each entry of the range that the palette stores. Only the red, the green
// and the blue of those entries change: the flag bytes and the count of the
// stored entries stay. A range with no stored entry changes nothing.
//
// Usage when the view has no palette, and when first to last is not a range
// of palette entries; Unsupported for a view format that these functions do
// not read, for a view of SCI1.1 or SCI2 whose data has the form of an SCI1
// view, and for a view whose cels are not the same after the change (a cel
// and the palette have bytes in common); Format for data that is not a view,
// and for a palette that cannot be read.
sci::Result<ViewPaletteEdit> ReplaceViewPaletteColors(const SCIVersion &version, const std::vector<uint8_t> &data, int first, int last, const std::array<IndexedBmpColor, 256> &colors);

// The view with no "used" mark for the entries first to last of its palette
// (ClearPaletteMarks): the flag bytes of 4-byte entries become 0, and a
// palette with 3-byte entries stores fewer entries. The colours stay.
//
// Usage when a pixel of the view, other than the transparent colour of its
// cel, has an entry that changes: the view would lose the colour of that
// pixel. Else the errors of ReplaceViewPaletteColors and of
// ClearPaletteMarks.
sci::Result<ViewPaletteEdit> ClearViewPaletteMarks(const SCIVersion &version, const std::vector<uint8_t> &data, int first, int last);
