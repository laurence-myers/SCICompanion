#pragma once

// The parts of the view commands (plan sections 4.10 to 4.12): a row of view
// list from the bytes of a view, the two tables of view list, the files of
// view export, and the new bytes of a view for view import, which need no
// game; and the write of the patch file of a view. The commands
// (CliViewCommands.cpp) read the views of a game and call them; a test
// calls the parts that need no game with the bytes of a view.

#include "CliPic.h"
#include "Result.h"
#include "Version.h"
#include "ViewImage.h"
#include <cstdint>
#include <string>
#include <vector>

class GameSession;

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

    class CliOutput;

    // Writes the bytes of a view as its patch file at path (the header 80 80
    // and 24 bytes of 0; ResourceBlob::SaveToFile). Unsupported for a view
    // that is too large for a resource (CheckResourceSize). A file that is
    // there already must open for writing. A dry run writes nothing and
    // gives "Would write <path>". A write that fails leaves no file.
    // writeFailed is set when the file cannot be written.
    sci::Status WriteViewPatchFile(GameSession &session, int number, const std::vector<uint8_t> &data, const std::string &path, bool dryRun, CliOutput &output, bool &writeFailed);

    // The image of one cel for view import.
    struct ViewImportImage
    {
        int loop = 0;
        int cel = 0;
        // The name of the image in a message: the path of its file.
        std::string name;
        IndexedBmp image;
    };

    // The bytes of a view with the pixels of the images in its cels, and,
    // when the palette says "replace", the colours of the images in the
    // entries of the range that the palette of the view stores and marks
    // as used (ReplaceViewPaletteColors; a stored entry with no mark keeps
    // its colour). The cels change in place
    // (ReplaceViewCelPixels). Each image is checked before the view
    // changes:
    //  - the view stores the cel (a loop that mirrors another loop stores
    //    none), and one image is for one cel;
    //  - the image has the width and the height of the cel;
    //  - a pixel has the skip colour of the cel where the cel has it, and
    //    nowhere else (CheckImportPixels);
    //  - each other pixel is an entry of usedEntries (the entries that the
    //    view or palette 999 marks as used). With allowUnusedEntries this
    //    is a warning;
    //  - with "replace": the colour table of the image has the entries of
    //    the range, and the images have the same colours in the range.
    // An image that fails a check gives Usage. With "replace", a palette
    // that stores only a part of the range gives a warning; a view with no
    // palette gives Unsupported. Then the errors of ReplaceViewPaletteColors and
    // ReplaceViewCelPixels. A warning is a text with no view number.
    sci::Result<std::vector<uint8_t>> MakeViewImportData(const SCIVersion &version, const std::vector<uint8_t> &data, const ViewDescription &description, const std::array<bool, 256> &usedEntries,
        const std::vector<ViewImportImage> &images, const PicImportPalette &palette, bool allowUnusedEntries, std::vector<std::string> &warnings);
}
