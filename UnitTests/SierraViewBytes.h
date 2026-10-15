#pragma once

// Views made by hand in the form of Sierra's views of SCI1.1 and SCI2, for
// the tests of the view functions: a view header of 18 bytes, loop headers
// of 16 bytes, cel headers of 36 or 52 bytes, the palette, then the data of
// each cel (its run-length data, its literal data and, for SCI2, its row
// table). The view writer of SCI Companion makes another layout.

#include <array>
#include <cstdint>
#include <vector>

namespace sierraview
{
    struct MadeCel
    {
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
        uint8_t skip = 0;
        // width * height bytes, the top row first.
        std::vector<uint8_t> pixels;
        // No run-length data: the pixels are stored as they are, and the
        // offset of the literal data is 0.
        bool raw = false;
        // The cel has the data of the cel before it in the same loop (its
        // header points at the same bytes). Its size and pixels must be
        // those of that cel.
        bool sharesDataWithPrevious = false;
    };

    struct MadeLoop
    {
        // The loop that this loop mirrors; -1: not a mirror.
        int mirrorOf = -1;
        std::vector<MadeCel> cels;
    };

    struct MadeView
    {
        std::vector<MadeLoop> loops;
        int celHeaderSize = 52;
        // SCI2: each cel has a row table, and the compression byte is 0x8a
        // (0x0a for SCI1.1).
        bool sci2 = true;
        uint8_t scaleFlags = 0;
        uint8_t resolution = 1;
        bool hasPalette = true;
        int paletteFirst = 0;
        int paletteCount = 256;
        // 4: a flag byte and a colour for each entry; 3: a colour, and the
        // header has the flag of all entries (sharedFlag).
        int entrySize = 4;
        uint8_t sharedFlag = 1;
        // The flag of each entry of 4 bytes, by entry number.
        std::array<uint8_t, 256> flags = {};
        // The colour of each entry, by entry number: red, green, blue.
        std::array<std::array<uint8_t, 3>, 256> colors = {};
        // The bytes 16 to 51 of a cel header of 52 bytes.
        uint8_t celHeaderFiller = 0x5a;
    };

    inline void PutWord(std::vector<uint8_t> &bytes, size_t at, uint32_t value)
    {
        bytes[at] = (uint8_t)(value & 0xff);
        bytes[at + 1] = (uint8_t)((value >> 8) & 0xff);
    }

    inline void PutDword(std::vector<uint8_t> &bytes, size_t at, uint32_t value)
    {
        PutWord(bytes, at, value & 0xffff);
        PutWord(bytes, at + 2, value >> 16);
    }

    // The run-length and the literal data of a cel: each row is its own
    // runs (a run of the skip colour, a run of one colour, or a run of
    // literal pixels), and the start of each row is in the row table.
    struct EncodedCel
    {
        std::vector<uint8_t> rle;
        std::vector<uint8_t> literal;
        std::vector<uint32_t> rleRows;
        std::vector<uint32_t> literalRows;
    };

    inline EncodedCel EncodeCel(const MadeCel &cel)
    {
        EncodedCel encoded;
        for (int y = 0; y < cel.height; y++)
        {
            encoded.rleRows.push_back((uint32_t)encoded.rle.size());
            encoded.literalRows.push_back((uint32_t)encoded.literal.size());
            const uint8_t *row = cel.pixels.data() + ((size_t)y * cel.width);
            int x = 0;
            while (x < cel.width)
            {
                int same = 1;
                while (((x + same) < cel.width) && (row[x + same] == row[x]) && (same < 63))
                {
                    same++;
                }
                if (row[x] == cel.skip)
                {
                    encoded.rle.push_back((uint8_t)(0xc0 | same));
                    x += same;
                }
                else if (same >= 3)
                {
                    encoded.rle.push_back((uint8_t)(0x80 | same));
                    encoded.literal.push_back(row[x]);
                    x += same;
                }
                else
                {
                    int count = 1;
                    while (((x + count) < cel.width) && (row[x + count] != cel.skip) && (count < 63))
                    {
                        count++;
                    }
                    encoded.rle.push_back((uint8_t)count);
                    encoded.literal.insert(encoded.literal.end(), row + x, row + x + count);
                    x += count;
                }
            }
        }
        return encoded;
    }

    // The offsets of the data of each stored cel, in the order of the loops.
    struct CelPlace
    {
        size_t header = 0;
        size_t rle = 0;
        size_t literal = 0;
        size_t rowTable = 0;
    };

    inline std::vector<uint8_t> MakeSierraView(const MadeView &view, std::vector<CelPlace> *places = nullptr)
    {
        const size_t loopCount = view.loops.size();
        size_t storedCels = 0;
        for (const MadeLoop &loop : view.loops)
        {
            storedCels += (loop.mirrorOf < 0) ? loop.cels.size() : 0;
        }
        const size_t loopHeaders = 18;
        const size_t celHeaders = loopHeaders + (16 * loopCount);
        const size_t palette = celHeaders + (view.celHeaderSize * storedCels);
        const size_t paletteSize = view.hasPalette ? (37 + ((size_t)view.paletteCount * view.entrySize)) : 0;
        std::vector<uint8_t> bytes(palette + paletteSize, 0);

        PutWord(bytes, 0, 16);
        bytes[2] = (uint8_t)loopCount;
        bytes[3] = view.scaleFlags;
        bytes[4] = 1;
        bytes[5] = view.resolution;
        PutWord(bytes, 6, (uint32_t)storedCels);
        PutDword(bytes, 8, view.hasPalette ? (uint32_t)palette : 0);
        bytes[12] = 16;
        bytes[13] = (uint8_t)view.celHeaderSize;
        PutWord(bytes, 14, 640);
        PutWord(bytes, 16, 480);

        if (view.hasPalette)
        {
            bytes[palette] = 0x0e;
            bytes[palette + 10] = 1;
            PutWord(bytes, palette + 13, (uint32_t)(22 + (view.paletteCount * view.entrySize)));
            bytes[palette + 25] = (uint8_t)view.paletteFirst;
            PutWord(bytes, palette + 29, (uint32_t)view.paletteCount);
            bytes[palette + 31] = view.sharedFlag;
            bytes[palette + 32] = (view.entrySize == 3) ? 1 : 0;
            for (int i = 0; i < view.paletteCount; i++)
            {
                const int entry = view.paletteFirst + i;
                size_t at = palette + 37 + ((size_t)i * view.entrySize);
                if (view.entrySize == 4)
                {
                    bytes[at++] = view.flags[entry];
                }
                bytes[at] = view.colors[entry][0];
                bytes[at + 1] = view.colors[entry][1];
                bytes[at + 2] = view.colors[entry][2];
            }
        }

        size_t celHeader = celHeaders;
        CelPlace previous;
        for (size_t l = 0; l < loopCount; l++)
        {
            const MadeLoop &loop = view.loops[l];
            const size_t loopHeader = loopHeaders + (16 * l);
            bytes[loopHeader] = (loop.mirrorOf < 0) ? 0xff : (uint8_t)loop.mirrorOf;
            bytes[loopHeader + 1] = (loop.mirrorOf < 0) ? 0 : 1;
            bytes[loopHeader + 2] = (uint8_t)((loop.mirrorOf < 0) ? loop.cels.size() : view.loops[loop.mirrorOf].cels.size());
            bytes[loopHeader + 3] = 0xff;
            PutDword(bytes, loopHeader + 4, 0x03ffffff);
            PutDword(bytes, loopHeader + 12, (uint32_t)celHeader);
            if (loop.mirrorOf >= 0)
            {
                continue;
            }
            for (const MadeCel &cel : loop.cels)
            {
                CelPlace place;
                place.header = celHeader;
                if (cel.sharesDataWithPrevious)
                {
                    place.rle = previous.rle;
                    place.literal = previous.literal;
                    place.rowTable = previous.rowTable;
                }
                else if (cel.raw)
                {
                    place.rle = bytes.size();
                    bytes.insert(bytes.end(), cel.pixels.begin(), cel.pixels.end());
                }
                else
                {
                    const EncodedCel encoded = EncodeCel(cel);
                    place.rle = bytes.size();
                    bytes.insert(bytes.end(), encoded.rle.begin(), encoded.rle.end());
                    place.literal = bytes.size();
                    bytes.insert(bytes.end(), encoded.literal.begin(), encoded.literal.end());
                    if (view.sci2)
                    {
                        place.rowTable = bytes.size();
                        bytes.resize(bytes.size() + (8 * (size_t)cel.height));
                        for (int y = 0; y < cel.height; y++)
                        {
                            PutDword(bytes, place.rowTable + (4 * (size_t)y), encoded.rleRows[y]);
                            PutDword(bytes, place.rowTable + (4 * ((size_t)cel.height + y)), encoded.literalRows[y]);
                        }
                    }
                }
                PutWord(bytes, celHeader, (uint32_t)cel.width);
                PutWord(bytes, celHeader + 2, (uint32_t)cel.height);
                PutWord(bytes, celHeader + 4, (uint32_t)(uint16_t)(int16_t)cel.x);
                PutWord(bytes, celHeader + 6, (uint32_t)(uint16_t)(int16_t)cel.y);
                bytes[celHeader + 8] = cel.skip;
                bytes[celHeader + 9] = cel.raw ? 0 : (view.sci2 ? 0x8a : 0x0a);
                PutDword(bytes, celHeader + 24, (uint32_t)place.rle);
                PutDword(bytes, celHeader + 28, (uint32_t)place.literal);
                PutDword(bytes, celHeader + 32, (uint32_t)place.rowTable);
                for (int i = 36; i < view.celHeaderSize; i++)
                {
                    bytes[celHeader + i] = view.celHeaderFiller;
                }
                if (places != nullptr)
                {
                    places->push_back(place);
                }
                previous = place;
                celHeader += view.celHeaderSize;
            }
        }
        return bytes;
    }

    // A cel of the size whose pixel at (x, y) is first + ((x + y * width) %
    // span), with the skip colour at the pixels of skipAt.
    inline MadeCel PatternCel(int width, int height, uint8_t skip, int first, int span, const std::vector<int> &skipAt = {})
    {
        MadeCel cel;
        cel.width = width;
        cel.height = height;
        cel.skip = skip;
        for (int i = 0; i < width * height; i++)
        {
            cel.pixels.push_back((uint8_t)(first + (i % span)));
        }
        for (int at : skipAt)
        {
            cel.pixels[at] = skip;
        }
        return cel;
    }

    // A view of 3 loops: loop 0 has 2 cels (the second with the skip colour
    // in it), loop 1 mirrors loop 0, and loop 2 has 1 raw cel. A palette of
    // 4-byte entries 0 to 255 marks the entries 10 to 99.
    inline MadeView ThreeLoopView()
    {
        MadeView view;
        MadeLoop first;
        MadeCel a = PatternCel(5, 3, 0, 10, 7);
        a.x = -2;
        a.y = 3;
        first.cels.push_back(a);
        first.cels.push_back(PatternCel(4, 2, 255, 20, 5, { 0, 1, 7 }));
        view.loops.push_back(first);
        MadeLoop mirror;
        mirror.mirrorOf = 0;
        view.loops.push_back(mirror);
        MadeLoop rawLoop;
        MadeCel raw = PatternCel(3, 2, 0, 40, 6);
        raw.raw = true;
        rawLoop.cels.push_back(raw);
        view.loops.push_back(rawLoop);
        for (int entry = 0; entry < 256; entry++)
        {
            view.flags[entry] = ((entry >= 10) && (entry <= 99)) ? 1 : 0;
            view.colors[entry] = { (uint8_t)entry, (uint8_t)(255 - entry), (uint8_t)(entry / 2) };
        }
        return view;
    }
}
