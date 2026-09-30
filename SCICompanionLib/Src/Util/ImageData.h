#pragma once

// Helpers for 8 bpp image data, with no GDI+ (ImageUtil.h has the GDI+ ones).

struct Cel;

// Swaps the rows top to bottom (a bottom-up bitmap to top-down, and back).
void FlipImageData(uint8_t *data, int cx, int cy, int stride);
// Sets used[i] for each palette index that the cels use (used has 256
// entries), and returns the count of used indices.
int CountActualUsedColors(const Cel &cel, bool *used);
int CountActualUsedColors(const std::vector<const Cel*> &cels, bool *used);

// Color helpers (util.cpp).
RGBQUAD _ToSRGB(RGBQUAD color);
RGBQUAD _ToLinear(RGBQUAD color);
RGBQUAD _CombineGamma(RGBQUAD color1, RGBQUAD color2);