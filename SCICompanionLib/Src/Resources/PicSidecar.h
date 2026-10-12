#pragma once

// The .toml file that scic pic export writes next to the BMP files of a pic
// (plan section 4.7 of docs\scic-cli\plan.md): where the pic came from, its
// size, what its palette stores, and its cels with their files. A tool that
// changes the BMP files reads it (Python: tomllib), and scic pic import
// reads it to find the pic and the file of each cel.

#include "PicImage.h"
#include "Result.h"
#include <cstdint>
#include <string>
#include <vector>

struct PicSidecarCel
{
    int index = 0;
    // The BMP file of the cel, in the folder of the sidecar. Empty: the
    // export wrote no file for the cel.
    std::string file;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    int priority = 0;
    int skipColor = 0;
    bool compressed = false;
    // The palette entries that the pixels of the cel have, without the skip
    // colour.
    std::vector<IndexRun> entries;
    uint32_t skipPixels = 0;
};

// A run of palette entries with the same flag byte.
struct PicSidecarFlagRun
{
    int first = 0;
    int last = 0;
    int flag = 0;
};

struct PicSidecar
{
    // [source]
    int pic = 0;
    std::string format;         // PicFormatName
    std::string from;           // "package" or "patch"
    uint32_t size = 0;          // the bytes of the pic resource
    uint32_t crc32 = 0;         // the CRC-32 of those bytes
    // False for a file that was read and has no crc32 key.
    bool hasCrc32 = true;
    // [pic]
    int width = 0;
    int height = 0;
    int backgroundCel = -1;
    // [palette]
    bool hasPalette = false;
    int storedFirst = 0;
    int storedCount = 0;
    int entrySize = 0;
    // The flag bytes of the entries that the pic stores.
    std::vector<PicSidecarFlagRun> flags;
    // The entries that palette 999 of the game marks as used.
    std::vector<IndexRun> palette999;
    // The entries that the pic or palette 999 marks as used: the colours
    // that the game has for the pic.
    std::vector<IndexRun> used;
    // [[cel]]
    std::vector<PicSidecarCel> cels;
};

// The parts of the sidecar that the description of the pic gives: the
// format, the size, the background cel, the palette of the pic, and each
// cel with no file. The caller sets the source, palette999, used, and the
// files.
PicSidecar MakePicSidecar(const PicDescription &description);

// The text of the TOML file: the tables [source], [pic] and [palette], and
// one [[cel]] for each cel. A run of entries is an array [first, last]; a
// flag run is [first, last, flag]. A cel with no file has no "file" key, and
// only a cel of an SCI2 pic has a "priority" key.
std::string PicSidecarText(const PicSidecar &sidecar);

// Reads the sidecar from the text of a TOML file. It needs the pic number
// ([source] pic, 0 to 65535) and the index of each [[cel]]; a key that is
// not there keeps the value of a new PicSidecar. The file of a cel must be a
// file name with no folder; the text of the TOML file is UTF-8, and the
// sidecar has the name in the ANSI code page, as a path has it. Format for
// text that is not TOML (the error has the line and the column), for a key
// of another type (also source, pic or palette that is not a table), for a
// number that is not in its range, for a file with a folder, and for a file
// with a character that the ANSI code page does not have.
sci::Result<PicSidecar> ParsePicSidecar(const std::string &text);

// Reads the sidecar file (a path in the ANSI code page). NotFound when the
// file cannot be opened; the errors of ParsePicSidecar, with the file as
// their location.
sci::Result<PicSidecar> ReadPicSidecarFile(const std::string &path);
