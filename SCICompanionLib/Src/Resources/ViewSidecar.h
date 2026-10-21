#pragma once

// The .toml file that scic view export writes next to the BMP files of a
// view (plan section 4.11 of docs\scic-cli\plan.md): where the view came
// from, its loops, what its palette stores, and its cels with their files.
// A tool that changes the BMP files reads it (Python: tomllib), and scic
// view import and scic view add read it to find the view and the file of
// each cel.

#include "PicSidecar.h"
#include "Result.h"
#include "ViewImage.h"
#include <cstdint>
#include <string>
#include <vector>

struct ViewSidecarLoop
{
    int index = 0;
    int cels = 0;
    // The loop that this loop mirrors; -1: not a mirror (the file has no
    // mirror_of key).
    int mirrorOf = -1;
};

struct ViewSidecarCel
{
    int loop = 0;
    int cel = 0;
    // The BMP file of the cel, in the folder of the sidecar. Empty: no file.
    std::string file;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    // False for a [[cel]] that was read and has no x key, or no y key.
    bool hasX = true;
    bool hasY = true;
    int skipColor = 0;
    bool raw = false;
    // The palette entries that the pixels of the cel have, without the skip
    // colour.
    std::vector<IndexRun> entries;
    uint32_t skipPixels = 0;
};

struct ViewSidecar
{
    // [source]
    int view = 0;
    // False for a file that was read and has no view key.
    bool hasView = true;
    std::string format;         // ViewFormatName
    std::string from;           // "package" or "patch"
    uint32_t size = 0;          // the bytes of the view resource
    uint32_t crc32 = 0;         // the CRC-32 of those bytes
    // False for a file that was read and has no crc32 key.
    bool hasCrc32 = true;
    // [view]
    int loopCount = 0;
    int celCount = 0;
    // Bytes 3, 4 and 5 of the view header, and its native size (bytes 14
    // to 17).
    int scaleFlags = 0;
    int byte4 = 1;
    int resolution = 0;
    int nativeWidth = 0;
    int nativeHeight = 0;
    // [palette]
    bool hasPalette = false;
    int storedFirst = 0;
    int storedCount = 0;
    int entrySize = 0;
    // The flag bytes of the entries that the view stores; a stored 3-byte
    // entry has the flag of the palette header.
    std::vector<PicSidecarFlagRun> flags;
    // The entries that palette 999 of the game marks as used.
    std::vector<IndexRun> palette999;
    // The entries that the view or palette 999 marks as used: the colours
    // that the game has for the view.
    std::vector<IndexRun> used;
    // [[loop]] and [[cel]]
    std::vector<ViewSidecarLoop> loops;
    std::vector<ViewSidecarCel> cels;
};

// The parts of the sidecar that the description of the view gives: the
// format, the loops, the bytes of the view header, the palette of the view,
// and each stored cel with no file. The caller sets the source,
// palette999, used, and the files.
ViewSidecar MakeViewSidecar(const ViewDescription &description);

// The text of the TOML file: the tables [source], [view] and [palette], one
// [[loop]] for each loop, and one [[cel]] for each stored cel. A run of
// entries is an array [first, last]; a flag run is [first, last, flag]. A
// loop that is not a mirror has no "mirror_of" key, and a cel with no file
// has no "file" key.
std::string ViewSidecarText(const ViewSidecar &sidecar);

// Reads the sidecar from the text of a TOML file. It needs the index of each
// [[loop]], and the loop and the cel of each [[cel]]; a key that is not
// there keeps the value of a new ViewSidecar (hasView, hasCrc32, and hasX
// and hasY of a [[cel]], are then false). The file of a cel must be a file name with no folder, in
// the ANSI code page. Format for text that is not TOML (the error has the
// line and the column), for a key of another type (also a table that is
// not a table), for a number that is not in its range, and for a bad file
// name.
sci::Result<ViewSidecar> ParseViewSidecar(const std::string &text);

// Reads the sidecar file (a path in the ANSI code page). NotFound when the
// file cannot be opened; the errors of ParseViewSidecar, with the file as
// their location.
sci::Result<ViewSidecar> ReadViewSidecarFile(const std::string &path);
