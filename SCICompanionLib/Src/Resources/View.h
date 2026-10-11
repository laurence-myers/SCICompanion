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

class ResourceEntity;

// rawDataStream is used if the image bits are stored separately from the RLE encoding opcodes.
void ReadImageData(sci::istream &byteStream, Cel &cel, bool isVGA);
void ReadImageData(sci::istream &byteStreamRLE, Cel &cel, bool isVGA, sci::istream &byteStreamLiteral);
void WriteImageData(sci::ostream &byteStream, const Cel &cel, bool isVGA, bool isEmbeddedView);
void WriteImageData(sci::ostream &rleStream, const Cel &cel, bool isVGA, sci::ostream &literalStream, bool writeZero);
void ReadCelFromVGA11(sci::istream &byteStream, Cel &cel, bool isPic);

extern uint8_t g_vgaPaletteMapping[256];

struct Cel
{
	Cel(size16 size, point16 placement, uint8_t transparentColor) : size(size), placement(placement)
	{
		this->TransparentColor = transparentColor;
		this->Stride32 = true;
	}

	Cel() : Stride32(true) {}
	Cel(const Cel &cel) = default;
	Cel &operator=(const Cel &cel) = default;

	uint16_t GetStride() const { return Stride32 ? CX_ACTUAL(size.cx) : size.cx; }
	size_t GetDataSize() const
	{
		return (GetStride() * size.cy);
	}

	// REVIEW: std::vector is 16 bytes. We could change this to 4 bytes if we wish
	// (thus reducing size of a cel from 24 to 12?) 
	// But we'd need to write a manual copy/assignment constructor.
	sci::array<uint8_t> Data;
	size16 size;
	point16 placement;
	uint8_t TransparentColor;
	bool Stride32;  // 32 bit stride
};

// The bytes of a row of a cel as SCI Companion holds it. Cel::GetStride
// gives 16 bits, which the stride of a cel of 65533 pixels or more does
// not fit.
inline size_t StrideOf(const Cel &cel)
{
	const size_t width = cel.size.cx;
	return cel.Stride32 ? ((width + 3) & ~(size_t)3) : width;
}

// The offset of the palette in the data of a view, as the reader of the
// views of SCI1.1 and SCI2 finds it. 0 for a view with no palette, for data
// that is too short for the header of a view, and for a view that this
// reader gives to the reader of SCI1 views.
uint32_t ReadViewPaletteOffsetVGA11(const uint8_t *data, size_t size);

struct Loop
{
	Loop() : UnknownData(0), IsMirror(false), MirrorOf(0xff) {}
	Loop(const Loop &loop) = default;
	Loop &operator=(const Loop &loop) = default;
	int GetMirrorOf() const
	{
		return IsMirror ? (int)MirrorOf : -1;

	}

	std::vector<Cel> Cels;
	uint16_t UnknownData;
	bool IsMirror;
	uint8_t MirrorOf;
};


enum class RasterCaps : uint16_t
{
	None = 0x0000,
	Transparency = 0x0001,
	Placement = 0x0002,
	Resize = 0x0004,
	Reorder = 0x0008,
	Mirror = 0x0010,
	Animate = 0x0020,
	SCI0CursorPlacement = 0x0040,
	EightBitPlacement = 0x0080,
};
DEFINE_ENUM_FLAGS(RasterCaps, uint16_t)

enum class OriginStyle : uint16_t
{
	None,
	BottomCenter,
	TopLeft,
};


enum class PaletteType : uint8_t
{
	EGA_Two,
	EGA_Four,
	EGA_Sixteen,
	VGA_256,
};


struct RasterTraits
{
	// Instances of these should be unique global objects, and people should
	// only hold onto references to them.
	RasterTraits(const RasterTraits &src) = delete;
	RasterTraits& operator=(const RasterTraits &src) = delete;

	RasterCaps Caps;
	OriginStyle OriginStyle;
	uint16_t MaxWidth;
	uint16_t MaxHeight;
	PaletteType PaletteType;
	const uint8_t *PaletteMapping;
	const RGBQUAD *Palette;
	int PreviewCel;
	GetItemLabelFuncPtr GetItemLabelFunc;
	bool SupportsScaling;
	uint8_t DefaultEditColor;
	uint8_t DefaultEditAltColor;
};

struct RasterSettings
{
	// Instances of these should be unique global objects, and people should
	// only hold onto references to them.
	RasterSettings(const RasterSettings &src) = delete;
	RasterSettings& operator=(const RasterSettings &src) = delete;

	int DefaultZoom;
};

union CelIndex
{
	CelIndex(const CelIndex& celIndex) = default;
	CelIndex() = default;
	CelIndex(int loopIn, int celIn)
	{
		loop = (uint16_t)loopIn;
		cel = (uint16_t)celIn;
	}
	CelIndex(DWORD indexIn)
	{
		index = indexIn;
	}

	struct
	{
		uint16_t loop;
		uint16_t cel;
	};
	DWORD index;
};



struct RasterComponent : ResourceComponent
{
	RasterComponent();
	RasterComponent(const RasterComponent &src) = default;
	RasterComponent(const RasterTraits &traits, RasterSettings &settings) : Traits(traits), Settings(settings), UnknownData(0), ScaleFlags(0), Resolution(NativeResolution::Res320x200)  {}
	ResourceComponent *Clone() const override
	{
		return new RasterComponent(*this);
	}

	// Helper functions. None of these are bounds checked.
	int LoopCount() const { return (int)Loops.size(); }
	int CelCount(int nLoop) const { return (int)Loops[nLoop].Cels.size(); }
	Cel& GetCel(CelIndex index);
	const Cel& GetCel(CelIndex index) const;
	const Cel& GetCelFallback(CelIndex index) const;
	void ValidateCelIndex(int &loop, int &cel, bool wrap) const;

	static RasterComponent CreateDegenerate();

	const RasterTraits &Traits;
	RasterSettings &Settings;
	std::vector<Loop> Loops;
	uint32_t UnknownData;
	uint8_t ScaleFlags;			 // Scaling flags for VGA 1.1 views (0x0 is scalable, 0x1 is not)
	NativeResolution Resolution;	// Used for views
};

#include <pshpack1.h>
struct CelHeader_VGA11
{
	size16 size;
	point16 placement;
	uint8_t transparentColor;
	uint8_t always_0xa;
	uint8_t temp2, temp3;
	//KAWA: the above are actually
	//uint8_t compressType;
	//uint16_t compRemapCount
	//compressType matters for Magnifier cels-- the engine will Panic if it's nonzero.
	uint32_t totalCelDataSize;
	uint32_t rleCelDataSize;
	uint32_t paletteOffset;	 // Used for pic cels only, apparently.
	uint32_t offsetRLE;
	uint32_t offsetLiteral;
	uint32_t perRowOffsets;	 // SCI2 needs this.
	//KAWA: perRowOffset may be compressRemapOffste in SCI11.
};
#include <poppack.h>

ResourceEntity *CreateViewResource(SCIVersion version);
ResourceEntity *CreateDefaultViewResource(SCIVersion version);
