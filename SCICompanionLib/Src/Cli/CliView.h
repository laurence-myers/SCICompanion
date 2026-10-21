#pragma once

// The parts of the view commands (plan sections 4.10 to 4.13): a row of view
// list from the bytes of a view, the two tables of view list, the files of
// view export, the new bytes of a view for view import, and the new view of
// view add, which need no game; and the write of the patch file of a view.
// The commands (CliViewCommands.cpp) read the views of a game and call them;
// a test calls the parts that need no game with the bytes of a view.

#include "CliPic.h"
#include "Result.h"
#include "Version.h"
#include "ViewImage.h"
#include "ViewSidecar.h"
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
        // The [[cel]] of the image in a .toml file: its size (0: that of the
        // image) and its placement.
        bool hasSidecarCel = false;
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
    };

    // The checks of view import that an option makes a warning or turns off.
    struct ViewImportAllow
    {
        // A pixel with an entry that is not used is a warning.
        bool unusedEntries = false;
        // A pixel can change to or from the skip colour.
        bool skipChanges = false;
        // An image can have another size than its cel.
        bool sizeChanges = false;
    };

    // The bytes of a view with the pixels of the images in its cels, and,
    // when the palette says "replace", the colours of the images in the
    // entries of the range that the palette of the view stores and marks
    // as used (ReplacedPaletteColors and ReplaceViewPaletteColors; a stored
    // entry with no mark keeps its colour). The cels change in place
    // (ReplaceViewCelPixels). Each image is checked before the view
    // changes:
    //  - the view stores the cel (a loop that mirrors another loop stores
    //    none), and one image is for one cel;
    //  - the image has the width and the height of the cel. With
    //    allow.sizeChanges, the image needs its [[cel]] (hasSidecarCel)
    //    instead, and a width and a height there that are not 0 must be the
    //    size of the image: the cel gets the size of the image and the
    //    placement (x, y) of the [[cel]] (ViewCelPixels::newSize), and the
    //    loops that mirror its loop show it so too;
    //  - a pixel has the skip colour of the cel where the cel has it, and
    //    nowhere else (CheckImportPixels). This check is off with
    //    allow.skipChanges, and for an image of another size than its cel;
    //  - each other pixel is an entry of usedEntries (the entries that the
    //    view or palette 999 marks as used). With allow.unusedEntries this
    //    is a warning;
    //  - with "replace": the colour table of the image has the entries of
    //    the range, and the images have the same colours for the entries
    //    that get them.
    // An image that fails a check gives Usage. With "replace", a palette
    // that stores only a part of the range gives a warning, and so does an
    // image with another colour than palette.exportColors for a stored
    // entry with no mark; a view with no
    // palette gives Unsupported. Then the errors of ReplaceViewPaletteColors and
    // ReplaceViewCelPixels. A warning is a text with no view number.
    sci::Result<std::vector<uint8_t>> MakeViewImportData(const SCIVersion &version, const std::vector<uint8_t> &data, const ViewDescription &description, const std::array<bool, 256> &usedEntries,
        const std::vector<ViewImportImage> &images, const PicImportPalette &palette, const ViewImportAllow &allow, std::vector<std::string> &warnings);

    // The new view (MakeNewViewBytes) of a .toml file of the form of view
    // export and the images of its cels (one ViewImportImage for each
    // [[cel]] with a file). It reads these keys:
    //  - [view]: scale_flags, byte_4, resolution, native_width and
    //    native_height. loops and cels, when they are not 0, must be the
    //    counts of the file.
    //  - [palette]: with present, the palette stores the entries
    //    stored_first to stored_first + stored_count - 1, with entries of
    //    entry_size bytes (0: 4), and the flags of the runs of flags (an
    //    entry with no run: 1). An entry gets the colour of the colour
    //    tables of the images, which must have the stored entries, and must
    //    agree for each entry with a flag that is not 0. With no [palette]
    //    or no present, the view has no palette.
    //  - [[loop]]: index (0 to n - 1, each one time), mirror_of (a chain of
    //    mirrors ends at a loop that is not a mirror: MirroredLoop), and
    //    cels (when it is not 0: the count of the cels of the loop, or of
    //    the loop at the end of the chain of a mirror). For a mirror of a
    //    mirror, cels (also 0) is the count of the header of the mirror
    //    that it names (NewViewLoop::headerCels), as export gives it. When
    //    the loop at the end of the chain has cels, 0 gives a warning for
    //    each such mirror of a mirror: it shows no cel.
    //  - [[cel]]: loop, cel (0 to n - 1 in a loop, each one time), file, x,
    //    y, skip_color and raw. width and height come from the image; when
    //    they are not 0, they must be its size. A mirror loop has no [[cel]];
    //    a loop that is not a mirror can have none, as some of Sierra's
    //    views have.
    // Each pixel that is not the skip colour of its cel must be an entry that
    // the new palette (a flag that is not 0) or palette999Used marks; with
    // allowUnusedEntries this is a warning. Usage for a file or an image
    // that fails a check (the message names it).
    sci::Result<NewView> MakeAddedView(const ViewSidecar &sidecar, const std::vector<ViewImportImage> &images, const std::array<bool, 256> &palette999Used, bool allowUnusedEntries,
        std::vector<std::string> &warnings);
}
