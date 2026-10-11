/***************************************************************************
	Copyright (c) 2020 Philip Fortier

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
***************************************************************************/
#pragma once

#include "Components.h"
#include "PicCommands.h"
#include "Result.h"

struct PicTraits
{
	PicTraits(const PicTraits &src) = delete;
	PicTraits& operator=(const PicTraits &src) = delete;

	bool IsVGA;
	bool AllowMultipleBitmaps;
	bool CanChangePriorityLines;
	bool SixteenBitPri;
	bool SupportsPenCommands;
	bool SupportsControlScreen;
	bool SupportsVectorCommands;
	bool ContinuousPriority;
};

struct PicComponent : ResourceComponent
{
	PicComponent();
	PicComponent(const PicComponent &src) = default;
	PicComponent& operator=(const PicComponent &src) = default;
	PicComponent(const PicTraits *traits);
	ResourceComponent *Clone() const override
	{
		return new PicComponent(*this);
	}

	std::vector<PicCommand> commands;
	size16 Size;
	const PicTraits *Traits;

	int UniqueId;
};

bool IsSame(const PicComponent *one, const PicComponent *two);

// A warning of the checks before a pic is saved (a missing palette or
// priority bars command). The GUI installs a handler: a dialog with a "do
// not show again" box. With no handler, the warning goes to the core log.
using PicCheckWarningHandler = void (*)(const std::string &text);
// Installs the handler (null removes it) and returns the handler before it.
PicCheckWarningHandler SetPicCheckWarningHandler(PicCheckWarningHandler handler);

template<typename TPoint>
TPoint GameResolutionToScreenResolution(TPoint point, NativeResolution resolution)
{
	switch (resolution)
	{
		case NativeResolution::Res320x200:
			break;
		case NativeResolution::Res640x400:
			point.x *= 2;
			point.y *= 2;
			break;
		case NativeResolution::Res640x480:
			point.x *= 2;
			point.y = point.y * 12 / 5;
			break;
	}
	return point;
}

template<typename TPoint>
TPoint ScreenResolutionToGameResolution(TPoint point, NativeResolution resolution)
{
	switch (resolution)
	{
		case NativeResolution::Res320x200:
			break;
		case NativeResolution::Res640x400:
			point.x /= 2;
			point.y /= 2;
			break;
		case NativeResolution::Res640x480:
			point.x /= 2;
			point.y = point.y * 5 / 12;
			break;
	}
	return point;
}


ResourceEntity *CreatePicResource(SCIVersion version);
ResourceEntity *CreateDefaultPicResource(SCIVersion version);

// Where a cel of an SCI2 pic (PicFormat::VGA2) is in the bytes of its
// resource.
struct PicCelLayoutVGA2
{
	uint16_t width = 0;
	uint16_t height = 0;
	// The placement of the cel in the pic, as the cel header has it (a game
	// of 640 x 480 has it in the units of a screen of 320 x 200).
	int16_t x = 0;
	int16_t y = 0;
	int16_t priority = 0;
	uint8_t skipColor = 0;
	// The cel has run-length data. pixelOffset and pixelSize are then 0.
	bool compressed = false;
	// The pixels of a cel that is not compressed: width * height bytes, the
	// top row first, with no padding.
	uint32_t pixelOffset = 0;
	uint32_t pixelSize = 0;
};

struct PicLayoutVGA2
{
	// The size that the pic header has: 0 in a game of 320 x 200.
	uint16_t width = 0;
	uint16_t height = 0;
	std::vector<PicCelLayoutVGA2> cels;
	// 0: the pic has no palette.
	uint32_t paletteOffset = 0;
};

// Reads the header and the cel headers of an SCI2 pic from the bytes of its
// resource. Format when a header is not all in the data, when the sizes of
// the headers are not those of the format, when the pixels of a cel that is
// not compressed are not all in the data after the headers, and when the
// palette offset is not in the data after the headers.
sci::Result<PicLayoutVGA2> ReadPicLayoutVGA2(const uint8_t *data, size_t size);

// Writes new pixels (width * height bytes, the top row first) over the
// pixels of a cel of an SCI2 pic, in the bytes of its resource. No other
// byte changes, and the size of the data stays. Usage for a cel that the
// pic does not have, and for a count of pixels that is not that of the cel;
// Unsupported for a pic that has a compressed cel (this cel or another),
// and for a cel whose pixel bytes are also bytes of another cel or of the
// palette; the errors of ReadPicLayoutVGA2.
sci::Status PatchPicCelPixelsVGA2(std::vector<uint8_t> &data, int cel, const std::vector<uint8_t> &pixels);

// The offset of the palette in the bytes of an SCI1.1 pic
// (PicFormat::VGA1_1); 0: the pic has no palette. Format when the header is
// not all in the data, when its size is not that of the format, and when
// the palette offset is not in the data after the header.
sci::Result<uint32_t> ReadPicPaletteOffsetVGA11(const uint8_t *data, size_t size);
