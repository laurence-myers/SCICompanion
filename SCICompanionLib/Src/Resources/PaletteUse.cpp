#include "stdafx.h"
#include "PaletteUse.h"
#include "PaletteOperations.h"
#include "View.h"

std::array<bool, 256> UsedPaletteEntries(const PaletteComponent &palette)
{
    std::array<bool, 256> used = {};
    for (size_t i = 0; i < used.size(); i++)
    {
        used[i] = (palette.Colors[i].rgbReserved != 0);
    }
    return used;
}

ViewPaletteUse DescribeViewPaletteUse(const RasterComponent &raster, const PaletteComponent *palette)
{
    ViewPaletteUse use;
    use.loops = (int)raster.Loops.size();
    use.hasPalette = (palette != nullptr);
    if (palette != nullptr)
    {
        use.paletteEntries = UsedPaletteEntries(*palette);
    }
    for (const Loop &loop : raster.Loops)
    {
        use.cels += (int)loop.Cels.size();
        for (const Cel &cel : loop.Cels)
        {
            const size_t stride = cel.GetStride();
            for (size_t y = 0; y < (size_t)cel.size.cy; y++)
            {
                for (size_t x = 0; x < (size_t)cel.size.cx; x++)
                {
                    const size_t at = (y * stride) + x;
                    if (at >= cel.Data.size())
                    {
                        break;
                    }
                    const uint8_t entry = cel.Data[at];
                    if (entry != cel.TransparentColor)
                    {
                        use.pixelEntries[entry] = true;
                    }
                }
            }
        }
    }
    return use;
}
