#pragma once

#include <array>

struct RasterComponent;
struct PaletteComponent;

// The palette entries of a view: those that its own palette marks as used,
// and those that its pixels have.
struct ViewPaletteUse
{
    int loops = 0;
    int cels = 0;
    // The view has a palette of its own.
    bool hasPalette = false;
    // The entries that the palette of the view marks as used; none for a
    // view with no palette.
    std::array<bool, 256> paletteEntries = {};
    // The entries of the pixels of the cels, without the transparent colour
    // of each cel.
    std::array<bool, 256> pixelEntries = {};
};

// The entries of a palette that are marked as used (their flag is not 0).
std::array<bool, 256> UsedPaletteEntries(const PaletteComponent &palette);

// A null palette: the view has none. The bytes after the width of a row of
// a cel (its stride) are not pixels. A cel with fewer bytes than its size
// needs gives the pixels that it has.
ViewPaletteUse DescribeViewPaletteUse(const RasterComponent &raster, const PaletteComponent *palette);
