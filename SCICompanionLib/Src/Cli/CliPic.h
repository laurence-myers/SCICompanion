#pragma once

// The parts of the pic commands (plan section 4.7) that need no game: a row
// of pic list from the bytes of a pic, the two tables of pic list, and the
// files of pic export. The commands (CliPicCommands.cpp) read the pics of a
// game and call them; a test calls them with the bytes of a pic.

#include "PicImage.h"
#include "Result.h"
#include "Version.h"
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
}
