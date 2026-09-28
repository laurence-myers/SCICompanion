#include "stdafx.h"
#include "ImageData.h"
#include "View.h"

void FlipImageData(uint8_t *data, int cx, int cy, int stride)
{
	sci::array<uint8_t> buffer(cx);
	for (int y = 0; y < (cy / 2); y++)
	{
		int line1 = y;
		int line2 = cy - 1 - y;
		memcpy(&buffer[0], data + (line1 * stride), cx);
		memcpy(data + line1 * stride, data + line2 * stride, cx);
		memcpy(data + line2 * stride, &buffer[0], cx);
	}
}

int CountActualUsedColorsWorker(const Cel &cel, bool *used)
{
	const uint8_t *data = &cel.Data[0];
	for (int y = cel.size.cy - 1; y >= 0; y--)
	{
		int line = y * CX_ACTUAL(cel.size.cx);
		for (int x = cel.size.cx - 1; x >= 0; x--)
		{
			used[data[line + x]] = true;
		}
	}
	return std::count(used, used + 256, true);
}

int CountActualUsedColors(const Cel &cel, bool *used)
{
	memset(used, 0, 256);
	return CountActualUsedColorsWorker(cel, used);
}

int CountActualUsedColors(const std::vector<const Cel*> &cels, bool *used)
{
	memset(used, 0, 256);
	for (const Cel *cel : cels)
	{
		CountActualUsedColorsWorker(*cel, used);
	}
	return std::count(used, used + 256, true);
}
