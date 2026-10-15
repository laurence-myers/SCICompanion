#pragma once

// The parts of the view commands (plan sections 4.10 and 4.11) that need no
// game: a row of view list from the bytes of a view, the two tables of view
// list, and the files of view export. The commands (CliViewCommands.cpp)
// read the views of a game and call them; a test calls them with the bytes
// of a view.

#include "CliPic.h"
#include "Result.h"
#include "Version.h"
#include "ViewImage.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cli
{
    // One row of view list.
    struct ViewListRow
    {
        int number = 0;
        // ViewFormatName of the format of the game.
        std::string format;
        // "package" or "patch"; empty when the view was not read.
        std::string source;
        size_t bytes = 0;
        uint32_t crc32 = 0;
        // The view has an image that scic reads, and description has it.
        bool described = false;
        ViewDescription description;
        // Why the view cannot be read; empty for a view that was read.
        std::string error;
    };

    // The first line of the tsv table of view list: the names of its
    // columns.
    extern const char *const ViewListTsvHeader;

    // The row of a view from the bytes of its resource: its source, its size
    // and its CRC-32, and its description. A view of a format with no image
    // that scic reads (ega, vga1) has no description. A view that cannot be
    // described has the error in its row.
    ViewListRow MakeViewListRow(const SCIVersion &version, int number, const std::vector<uint8_t> &data, bool fromPatch);

    // The text table of view list: a header line, then a line for each row
    // with aligned columns and no space at its end (AlignedTextTable).
    std::string ViewListTextTable(const std::vector<ViewListRow> &rows);

    // The tsv table of view list: ViewListTsvHeader, then a line for each
    // row. A cell with no value is empty.
    std::string ViewListTsvTable(const std::vector<ViewListRow> &rows);

    // The name of the BMP file of a cel: <view>.l<loop>.c<cel>.bmp.
    std::string ViewCelFileName(int view, int loop, int cel);

    // The files of view export for one view: a BMP file for each stored cel
    // (ViewCelFileName), in the order of the cels, then <view>.toml. The
    // pixels of a BMP file are the bytes of the cel, and its colour table is
    // "colors" (the palette of the view over palette 999); the .toml file
    // gets the used entries of "colors" and of palette999 (null: the game
    // has none). Unsupported for a view with no stored cel; the errors of
    // GetAllViewCelPixels and of EncodeIndexedBmp.
    sci::Result<std::vector<PicExportFile>> MakeViewExportFiles(const SCIVersion &version, int number, const std::vector<uint8_t> &data, const ViewDescription &description, bool fromPatch,
        const PaletteComponent &colors, const PaletteComponent *palette999);
}
