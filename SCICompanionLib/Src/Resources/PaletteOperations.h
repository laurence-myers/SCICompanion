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
#include "Result.h"
#include <array>

class GameFolderHelper;

enum class PaletteChangeHint
{
	None = 0,
	Changed = 0x00000100,
};
DEFINE_ENUM_FLAGS(PaletteChangeHint, uint32_t)

enum class PaletteCompression
{
	Unknown,
	None,
	Header,
};

enum class PaletteEntryType
{
	Unknown,
	ThreeByte,
	FourByte,
};


struct PaletteComponent : public ResourceComponent
{
	PaletteComponent();
	PaletteComponent(const PaletteComponent &src) = default;
	PaletteComponent& operator=(const PaletteComponent &src) = default;
	ResourceComponent *Clone() const override
	{
		return new PaletteComponent(*this);
	}

	bool operator==(const PaletteComponent &src) const;
	bool operator!=(const PaletteComponent &src) const;

	~PaletteComponent()
	{
	}

	uint8_t Mapping[256];
	// The rgbReserved component of RGBQUAD appears to represent
	// "priority" for palette colors (for when palettes are combined).
	// The global palette typically has 0x1 set for its values.
	// Individual palettes for pics/views tend to have 0x0 set for values they
	// don't use (to be overriden by the global palette), or 0x3 for values
	// they do use.
	RGBQUAD Colors[256];

	PaletteCompression Compression;
	PaletteEntryType EntryType;
	void MergeFromOther(const PaletteComponent *globalPalette);
};

extern PaletteComponent g_egaDummyPalette;

// Where the entries of a palette are in the bytes that hold it: a palette
// resource, or the palette of a pic or of a view.
struct PaletteLayout
{
	// The entries that the bytes store: firstEntry to
	// firstEntry + entryCount - 1. entryCount is 0 for bytes that are too
	// short for a palette header (ReadPalette then gives black entries).
	int firstEntry = 0;
	int entryCount = 0;
	// 4: a flag byte, then red, green and blue. 3: red, green and blue. 0
	// when entryCount is 0.
	int entrySize = 0;
	// The offset of the first stored entry, from the start of the data.
	size_t entriesOffset = 0;
	// The palette has a header (the form of SCI1.1 and SCI2). False for the
	// form with a mapping table and no header, and for bytes that are too
	// short for a header.
	bool hasHeader = false;
	// For the 3-byte entries of a palette with a header: the flag byte of
	// the header, which is the flag of each stored entry. 0 for other
	// palettes.
	uint8_t sharedFlag = 0;
};

// The layout of the palette at paletteOffset of the data, as ReadPalette
// reads it. Format when the offset or the entries are not in the data, when
// the palette stores entries past 255, and for a palette format that is not
// known.
sci::Result<PaletteLayout> ReadPaletteLayout(const uint8_t *data, size_t size, size_t paletteOffset);

// Writes the red, the green and the blue of the entries first to last of
// colors (256 colours, by entry number) into the bytes of the palette at
// paletteOffset. No other byte changes: a flag byte keeps its value, and the
// palette stores the same entries. Usage when first to last is not a range
// of entries that the palette stores; the errors of ReadPaletteLayout.
sci::Status PatchPaletteColors(std::vector<uint8_t> &data, size_t paletteOffset, int first, int last, const RGBQUAD *colors);

// What ClearPaletteMarks changed in a palette.
struct PaletteMarksChange
{
	// The entries that lost their mark.
	std::array<bool, 256> entries = {};
	// The palette stores fewer entries, and the size word of its header did
	// not have the value of its entries (22 + the count x the entry size):
	// the word keeps its value.
	bool sizeWordKept = false;
};

// Takes the "used" mark from the entries first to last in the bytes of the
// palette at paletteOffset, and gives the entries that lost it. An entry of
// the range that the palette does not store, or does not mark, does not
// change.
//  - 4-byte entries: the flag byte of each stored entry of the range becomes
//    0. The entries that lost their mark are those whose flag byte was not 0.
//  - 3-byte entries, which have one flag for all of them in the header.
//    With the flag 0 the palette marks no entry, and nothing changes. Else
//    the palette stores fewer entries: the count of the header becomes
//    smaller, so that the first entry of the range is the first entry that
//    the palette does not store, and the size word of the header (at offset
//    13: 22 + the count x the entry size) gets the value of the new count.
//    A size word with another value stays as it is. The bytes of the
//    entries after the new count stay where they are; a reader does not
//    read them. The entries that lost their mark are those that the palette
//    stores no more.
// No other byte changes, and the size of the data stays. Usage when first to
// last is not a range of palette entries. Unsupported when the range has
// marked 3-byte entries but not the last stored entry (the count cannot take
// an entry out of the middle), and when the palette would store no entry
// and its first byte is 0: a reader takes such bytes as a palette with no
// header. The errors of ReadPaletteLayout.
sci::Result<PaletteMarksChange> ClearPaletteMarks(std::vector<uint8_t> &data, size_t paletteOffset, int first, int last);

void ReadPalette(PaletteComponent &palette, sci::istream &byteStream);
void WritePalette(sci::ostream &byteStream, const PaletteComponent &palette);
void WritePaletteShortForm(sci::ostream &byteStream, const PaletteComponent &palette);
void WritePaletteSCI2(sci::ostream &byteStream, const PaletteComponent &palette);

HBITMAP CreateBitmapFromPaletteResource(const ResourceEntity *prb, SCIBitmapInfo *pbmi, uint8_t **ppBitsDest, COLORREF *transparentColor = nullptr, const std::vector<const Cel*> *imageCels = nullptr);
HBITMAP CreateBitmapFromPaletteComponent(const PaletteComponent &palette, SCIBitmapInfo *pbmi, uint8_t **ppBitsDest, COLORREF *transparentColor = nullptr, const std::vector<const Cel*> *imageCels = nullptr);
ResourceEntity *CreatePaletteResource(SCIVersion version);

void LoadPALFile(const std::string &filename, PaletteComponent &palette, int startIndex);
void SavePALFile(const std::string &filename, PaletteComponent &palette, int startIndex, int count);
