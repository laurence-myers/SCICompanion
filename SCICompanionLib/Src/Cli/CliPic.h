#pragma once

// The parts of the pic commands (plan section 4.7) that need no game: a row
// of pic list from the bytes of a pic, the two tables of pic list, the
// files of pic export, and the new bytes of a pic for pic import. The
// commands (CliPicCommands.cpp) read the pics of a game and call them; a
// test calls them with the bytes of a pic.

#include "IndexedBmp.h"
#include "PicImage.h"
#include "Result.h"
#include "Version.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct PaletteComponent;

namespace cli
{
    // One row of pic list.
    struct PicListRow
    {
        int number = 0;
        // PicFormatName of the format of the game.
        std::string format;
        // "package" or "patch"; empty when the pic was not read.
        std::string source;
        size_t bytes = 0;
        uint32_t crc32 = 0;
        // The pic has an image that scic reads, and description has it.
        bool described = false;
        PicDescription description;
        // Why the pic cannot be read; empty for a pic that was read.
        std::string error;
    };

    // The row of a pic from the bytes of its resource: its source, its size
    // and its CRC-32, and its description. A pic of a format with no image
    // that scic reads (ega, vga1) has no description. A pic that cannot be
    // described has the error in its row.
    PicListRow MakePicListRow(const SCIVersion &version, int number, const std::vector<uint8_t> &data, bool fromPatch);

    // The text table of pic list: a header line, then a line for each row
    // with aligned columns and no space at its end. The row of a pic that
    // cannot be read ends with "(unreadable: <error>)".
    std::string PicListTextTable(const std::vector<PicListRow> &rows);

    // The tsv table of pic list: PicListTsvHeader, then a line for each row.
    // A cell with no value is empty.
    std::string PicListTsvTable(const std::vector<PicListRow> &rows);

    // A file of pic export, made in memory.
    struct PicExportFile
    {
        // The name of the file in the output folder.
        std::string name;
        // The bytes of a BMP file.
        std::vector<uint8_t> bytes;
        // The text of the .toml file: each line end becomes CR LF in the
        // file.
        std::string text;
        bool isText = false;
    };

    // The files of pic export for one pic, in the order of the cels, the
    // .toml file last: <number>.bmp for the background cel (the cel with the
    // largest area), <number>.cel<index>.bmp for each other cel when
    // everyCel is set, and <number>.toml. The pixels of a BMP file are the
    // bytes of the cel, and its colour table is "colors" (the palette of the
    // pic over palette 999); the .toml file gets the used entries of
    // "colors" and of palette999 (null: the game has none). Unsupported for
    // a pic with no cel, and for a cel with run-length data in an SCI2 pic
    // that would get a file; the errors of GetCelPixels.
    sci::Result<std::vector<PicExportFile>> MakePicExportFiles(const SCIVersion &version, int number, const std::vector<uint8_t> &data, const PicDescription &description, bool fromPatch,
        const PaletteComponent &colors, const PaletteComponent *palette999, bool everyCel);

    // The image of one cel for pic import.
    struct PicImportImage
    {
        // Below 0: the background cel of the pic (the cel with the largest
        // area).
        int cel = -1;
        // The name of the image in a message: the path of its file.
        std::string name;
        IndexedBmp image;
    };

    // The palette part of pic import. With "replace", the colours of the
    // entries first to last of the pic come from the colour tables of the
    // images.
    struct PicImportPalette
    {
        bool replace = false;
        int first = 0;
        int last = 0;
    };

    // The bytes of a pic with the pixels of the images in its cels, and with
    // the colours of the palette range when the palette says "replace". Each
    // image is checked before the pic changes:
    //  - the pic has the cel, one image is for one cel, and the cel is not
    //    compressed (Unsupported);
    //  - the image has the width and the height of the cel;
    //  - a pixel has the skip colour of the cel where the cel has it, and
    //    nowhere else;
    //  - each other pixel is an entry of usedEntries (the entries that the
    //    pic or palette 999 marks as used). With allowUnusedEntries this is
    //    a warning;
    //  - with "replace": the colour table of the image has the entries of
    //    the range, and the images have the same colours in the range.
    // An image that fails a check gives Usage. Then the errors of
    // ReplacePaletteRange and ReplaceCelPixels. A warning is a text with no
    // pic number; the warnings of a pic that fails are also there.
    sci::Result<std::vector<uint8_t>> MakePicImportData(const SCIVersion &version, const std::vector<uint8_t> &data, const PicDescription &description, const std::array<bool, 256> &usedEntries,
        const std::vector<PicImportImage> &images, const PicImportPalette &palette, bool allowUnusedEntries, std::vector<std::string> &warnings);
}
