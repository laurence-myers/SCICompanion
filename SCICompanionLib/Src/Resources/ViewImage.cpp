#include "stdafx.h"
#include "ViewImage.h"
#include "View.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceUtil.h"
#include "ResourceSourceFlags.h"
#include "GameFolderHelper.h"
#include "CoreLog.h"
#include "PaletteOperations.h"
#include "format.h"

namespace
{
    // The bytes of the view header, of a loop header and of a cel header
    // that the reader of the views reads.
    const size_t ViewHeaderBytes = 18;
    const size_t LoopHeaderBytes = 16;
    const size_t CelHeaderBytes = 36;

    uint16_t WordAt(const uint8_t *data, size_t at)
    {
        return (uint16_t)(data[at] | (data[at + 1] << 8));
    }

    uint32_t DwordAt(const uint8_t *data, size_t at)
    {
        return (uint32_t)data[at] | ((uint32_t)data[at + 1] << 8) | ((uint32_t)data[at + 2] << 16) | ((uint32_t)data[at + 3] << 24);
    }

    // "1 cel", "2 cels".
    std::string CountText(size_t count, const char *word)
    {
        return fmt::format("{0} {1}{2}", count, word, (count == 1) ? "" : "s");
    }

    tl::unexpected<sci::Error> NoImage(ViewFormat format)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("the image of a view of the format {0} cannot be read; only the formats vga11 and vga2 can", ViewFormatName(format)));
    }

    // The view as its headers and as the reader of the views give it. The
    // reader must log no repair, and it must give the loops and the cels of
    // the headers. A loop that is not a mirror and whose header has no cel
    // gets a 1 x 1 cel from the reader.
    struct ViewRead
    {
        ViewLayout layout;
        std::unique_ptr<ResourceEntity> view;
    };

    sci::Result<ViewRead> ReadView(const SCIVersion &version, const std::vector<uint8_t> &data)
    {
        if (!CanReadViewImage(version.ViewFormat))
        {
            return NoImage(version.ViewFormat);
        }
        ViewRead read;
        SCI_TRY_ASSIGN(read.layout, ReadViewLayout(data.data(), data.size()));
        RepairLogSink repairs;
        sci::Result<std::unique_ptr<ResourceEntity>> view = [&]()
        {
            ScopedCoreLogSink scopedSink(repairs);
            return ParseViewResource(version, data);
        }();
        const std::string repaired = repairs.Text();
        if (!repaired.empty())
        {
            return sci::Fail(sci::ErrorCode::Format, "the reader repaired damaged data: " + repaired);
        }
        SCI_TRY_ASSIGN(read.view, std::move(view));
        const RasterComponent &raster = read.view->GetComponent<RasterComponent>();
        if (raster.Loops.size() != read.layout.loops.size())
        {
            return sci::Fail(sci::ErrorCode::Format, fmt::format("the reader of the views gives {0}, and the view header has {1}", CountText(raster.Loops.size(), "loop"), read.layout.loops.size()));
        }
        for (size_t i = 0; i < raster.Loops.size(); i++)
        {
            const ViewLoopLayout &loop = read.layout.loops[i];
            const size_t expected = loop.cels.empty() ? 1 : loop.cels.size();
            if ((loop.mirrorOf < 0) && (raster.Loops[i].Cels.size() != expected))
            {
                return sci::Fail(sci::ErrorCode::Format, fmt::format("loop {0}: the reader of the views gives {1}, and the loop header has {2}", i, CountText(raster.Loops[i].Cels.size(), "cel"), loop.cels.size()));
            }
        }
        return std::move(read);
    }
}

sci::Result<ViewLayout> ReadViewLayout(const uint8_t *data, size_t size)
{
    if (size < ViewHeaderBytes)
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("the view has {0} bytes; its header needs {1}", size, ViewHeaderBytes));
    }
    ViewLayout layout;
    layout.headerSize = (uint32_t)WordAt(data, 0) + 2;
    const int loopCount = data[2];
    layout.scaleFlags = data[3];
    layout.byte4 = data[4];
    layout.resolution = data[5];
    layout.paletteOffset = DwordAt(data, 8);
    layout.loopHeaderSize = data[12];
    layout.celHeaderSize = data[13];
    layout.nativeWidth = WordAt(data, 14);
    layout.nativeHeight = WordAt(data, 16);
    if (layout.loopHeaderSize > 32)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("the view has the form of an SCI1 view (its loop headers have {0} bytes)", (int)layout.loopHeaderSize));
    }
    if (layout.loopHeaderSize < LoopHeaderBytes)
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("the loop headers of the view have {0} bytes; a loop header needs {1}", (int)layout.loopHeaderSize, LoopHeaderBytes));
    }
    if (layout.celHeaderSize < CelHeaderBytes)
    {
        return sci::Fail(sci::ErrorCode::Format, fmt::format("the cel headers of the view have {0} bytes; a cel header needs {1}", (int)layout.celHeaderSize, CelHeaderBytes));
    }
    for (int i = 0; i < loopCount; i++)
    {
        const uint64_t at = (uint64_t)layout.headerSize + ((uint64_t)layout.loopHeaderSize * (uint64_t)i);
        if ((at + LoopHeaderBytes) > size)
        {
            return sci::Fail(sci::ErrorCode::Format, fmt::format("the header of loop {0} is not in the {1} bytes of the view", i, size));
        }
        ViewLoopLayout loop;
        loop.headerOffset = (uint32_t)at;
        const uint8_t mirrorInfo = data[at];
        loop.mirrorOf = (mirrorInfo == 0xff) ? -1 : (int)mirrorInfo;
        loop.celCount = data[at + 2];
        loop.celOffset = DwordAt(data, (size_t)at + 12);
        if (loop.mirrorOf >= loopCount)
        {
            return sci::Fail(sci::ErrorCode::Format, fmt::format("loop {0} mirrors loop {1}, and the view has {2} loops", i, loop.mirrorOf, loopCount));
        }
        for (int c = 0; (loop.mirrorOf < 0) && (c < loop.celCount); c++)
        {
            const uint64_t celAt = (uint64_t)loop.celOffset + ((uint64_t)layout.celHeaderSize * (uint64_t)c);
            if ((celAt + CelHeaderBytes) > size)
            {
                return sci::Fail(sci::ErrorCode::Format, fmt::format("the header of cel {0} of loop {1} is not in the {2} bytes of the view", c, i, size));
            }
            const size_t celHeader = (size_t)celAt;
            ViewCelLayout cel;
            cel.headerOffset = (uint32_t)celHeader;
            cel.width = WordAt(data, celHeader);
            cel.height = WordAt(data, celHeader + 2);
            cel.x = (int16_t)WordAt(data, celHeader + 4);
            cel.y = (int16_t)WordAt(data, celHeader + 6);
            cel.skipColor = data[celHeader + 8];
            cel.compression = data[celHeader + 9];
            cel.rleOffset = DwordAt(data, celHeader + 24);
            cel.literalOffset = DwordAt(data, celHeader + 28);
            cel.rowTableOffset = DwordAt(data, celHeader + 32);
            loop.cels.push_back(cel);
        }
        layout.loops.push_back(loop);
    }
    return layout;
}

std::array<bool, 256> ViewCelDescription::PixelEntries() const
{
    std::array<bool, 256> entries = {};
    for (size_t i = 0; i < entries.size(); i++)
    {
        entries[i] = (pixelCounts[i] != 0) && (i != skipColor);
    }
    return entries;
}

int ViewDescription::MirrorLoops() const
{
    return (int)std::count_if(loops.begin(), loops.end(), [](const ViewLoopDescription &loop) { return loop.mirrorOf >= 0; });
}

int ViewDescription::RawCels() const
{
    return (int)std::count_if(cels.begin(), cels.end(), [](const ViewCelDescription &cel) { return cel.raw; });
}

std::array<bool, 256> ViewDescription::PixelEntries() const
{
    std::array<bool, 256> entries = {};
    for (const ViewCelDescription &cel : cels)
    {
        const std::array<bool, 256> ofCel = cel.PixelEntries();
        for (size_t i = 0; i < entries.size(); i++)
        {
            entries[i] = entries[i] || ofCel[i];
        }
    }
    return entries;
}

const ViewCelDescription *ViewDescription::Find(int loop, int cel) const
{
    for (const ViewCelDescription &description : cels)
    {
        if ((description.loop == loop) && (description.cel == cel))
        {
            return &description;
        }
    }
    return nullptr;
}

const char *ViewFormatName(ViewFormat format)
{
    switch (format)
    {
    case ViewFormat::EGA:
        return "ega";
    case ViewFormat::VGA1:
        return "vga1";
    case ViewFormat::VGA1_1:
        return "vga11";
    case ViewFormat::VGA2:
        return "vga2";
    }
    return "unknown";
}

bool CanReadViewImage(ViewFormat format)
{
    return (format == ViewFormat::VGA1_1) || (format == ViewFormat::VGA2);
}

sci::Result<std::unique_ptr<ResourceEntity>> ParseViewResource(const SCIVersion &version, const std::vector<uint8_t> &data)
{
    GameFolderHelper helper;
    // The blob has a name, so that it does not look for one in a game.
    ResourceBlob blob(helper, "view", ResourceType::View, data, version.DefaultVolumeFile, -1, NoBase36, version, ResourceSourceFlags::PatchFile);
    sci::Result<std::unique_ptr<ResourceEntity>> view = TryCreateResourceFromResourceData(blob);
    if (!view)
    {
        // The number of the view is not known here: the caller names it.
        view.error().where.resource.clear();
    }
    else if ((*view)->TryGetComponent<RasterComponent>() == nullptr)
    {
        return sci::Fail(sci::ErrorCode::Format, "the resource is not a view");
    }
    return view;
}

sci::Result<ViewDescription> DescribeView(const SCIVersion &version, const std::vector<uint8_t> &data)
{
    SCI_TRY_ASSIGN(ViewRead read, ReadView(version, data));
    const RasterComponent &raster = read.view->GetComponent<RasterComponent>();
    ViewDescription description;
    description.format = version.ViewFormat;
    description.scaleFlags = read.layout.scaleFlags;
    description.byte4 = read.layout.byte4;
    description.resolution = read.layout.resolution;
    description.nativeWidth = read.layout.nativeWidth;
    description.nativeHeight = read.layout.nativeHeight;
    for (size_t i = 0; i < read.layout.loops.size(); i++)
    {
        const ViewLoopLayout &loopLayout = read.layout.loops[i];
        ViewLoopDescription loop;
        loop.index = (int)i;
        loop.mirrorOf = loopLayout.mirrorOf;
        // A mirror has the cels of the loop that it mirrors: the count of the
        // header of that loop, as the SCI32 interpreter takes it.
        loop.cels = (loopLayout.mirrorOf < 0) ? (int)loopLayout.cels.size() : read.layout.loops[loopLayout.mirrorOf].celCount;
        description.loops.push_back(loop);
        for (size_t c = 0; c < loopLayout.cels.size(); c++)
        {
            const ViewCelLayout &celLayout = loopLayout.cels[c];
            ViewCelDescription cel;
            cel.loop = (int)i;
            cel.cel = (int)c;
            cel.width = celLayout.width;
            cel.height = celLayout.height;
            cel.x = celLayout.x;
            cel.y = celLayout.y;
            cel.skipColor = celLayout.skipColor;
            cel.raw = celLayout.IsRaw();
            SCI_TRY_ASSIGN(std::vector<uint8_t> pixels, CelTopDownPixels(raster.Loops[i].Cels[c]));
            for (uint8_t pixel : pixels)
            {
                cel.pixelCounts[pixel]++;
            }
            description.cels.push_back(cel);
        }
    }
    SCI_TRY_ASSIGN(description.palette, DescribePaletteAt(data, read.layout.paletteOffset, true));
    return std::move(description);
}

sci::Result<std::vector<uint8_t>> GetViewCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, int loop, int cel)
{
    SCI_TRY_ASSIGN(ViewRead read, ReadView(version, data));
    const std::vector<ViewLoopLayout> &loops = read.layout.loops;
    if ((loop < 0) || ((size_t)loop >= loops.size()))
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("the view has no loop {0}; it has {1}", loop, CountText(loops.size(), "loop")));
    }
    if (loops[loop].mirrorOf >= 0)
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("loop {0} mirrors loop {1}: it stores no cel", loop, loops[loop].mirrorOf));
    }
    const size_t cels = loops[loop].cels.size();
    if ((cel < 0) || ((size_t)cel >= cels))
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("loop {0} has no cel {1}; it has {2}", loop, cel, CountText(cels, "cel")));
    }
    return CelTopDownPixels(read.view->GetComponent<RasterComponent>().Loops[loop].Cels[cel]);
}

sci::Result<EncodedViewCel> EncodeViewCel(int width, int height, uint8_t skipColor, const std::vector<uint8_t> &pixels)
{
    if ((width < 0) || (height < 0) || (pixels.size() != ((size_t)width * (size_t)height)))
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("a cel of {0} x {1} pixels needs {2} bytes of pixels, not {3}", width, height, (int64_t)width * height, pixels.size()));
    }
    // The longest run: the count has 6 bits.
    const int MaxRun = 63;
    EncodedViewCel encoded;
    for (int y = 0; y < height; y++)
    {
        encoded.rleRows.push_back((uint32_t)encoded.rle.size());
        encoded.literalRows.push_back((uint32_t)encoded.literal.size());
        const uint8_t *row = pixels.data() + ((size_t)y * (size_t)width);
        // The count of the pixels from x that have the colour of x.
        auto sameFrom = [&](int x)
        {
            int count = 1;
            while (((x + count) < width) && (row[x + count] == row[x]) && (count < MaxRun))
            {
                count++;
            }
            return count;
        };
        int x = 0;
        while (x < width)
        {
            const int same = sameFrom(x);
            if (row[x] == skipColor)
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
                // Literal pixels, up to the skip colour or a run of 3.
                int count = 0;
                while (((x + count) < width) && (count < MaxRun) && (row[x + count] != skipColor) && (sameFrom(x + count) < 3))
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

namespace
{
    void PutDwordAt(std::vector<uint8_t> &data, size_t at, uint32_t value)
    {
        for (int i = 0; i < 4; i++)
        {
            data[at + i] = (uint8_t)((value >> (8 * i)) & 0xff);
        }
    }

    uint32_t OffsetAtEnd(const std::vector<uint8_t> &data)
    {
        return (uint32_t)data.size();
    }

    // The bytes first to end - 1 of the view.
    struct ByteRange
    {
        uint64_t first = 0;
        uint64_t end = 0;
    };

    bool Overlap(const ByteRange &a, const ByteRange &b)
    {
        return (a.first < b.end) && (b.first < a.end) && (a.first < a.end) && (b.first < b.end);
    }

    bool OverlapAny(const std::vector<ByteRange> &ranges, const std::vector<ByteRange> &others)
    {
        for (const ByteRange &range : ranges)
        {
            for (const ByteRange &other : others)
            {
                if (Overlap(range, other))
                {
                    return true;
                }
            }
        }
        return false;
    }

    // The bytes of run-length data that a reader takes for the pixels of a
    // cel: those of the run-length codes and those of the literal data.
    // False when the codes go past the end of the data.
    bool RunLengthSizes(const std::vector<uint8_t> &data, uint32_t rle, uint32_t literal, size_t pixels, uint64_t &rleBytes, uint64_t &literalBytes)
    {
        uint64_t at = rle;
        uint64_t done = 0;
        uint64_t literalCount = 0;
        while (done < pixels)
        {
            if (at >= data.size())
            {
                return false;
            }
            const uint8_t code = data[(size_t)at++];
            uint64_t count = code & 0x3f;
            switch (code >> 6)
            {
            case 1:
                count += 64;
                literalCount += count;
                break;
            case 0:
                literalCount += count;
                break;
            case 2:
                literalCount += 1;
                break;
            default:
                break;
            }
            done += count;
        }
        rleBytes = at - rle;
        literalBytes = literalCount;
        return ((uint64_t)literal + literalCount) <= data.size();
    }

    // The bytes of the data of a cel: its pixels (a raw cel), or its
    // run-length data, its literal data and its row table (SCI2). Data that
    // a reader cannot measure takes the rest of the view.
    std::vector<ByteRange> DataRangesOf(const std::vector<uint8_t> &data, const ViewCelLayout &cel, bool sci2)
    {
        const uint64_t pixels = (uint64_t)cel.width * (uint64_t)cel.height;
        if (cel.IsRaw())
        {
            return { ByteRange{ cel.rleOffset, cel.rleOffset + pixels } };
        }
        std::vector<ByteRange> ranges;
        uint64_t rleBytes = 0;
        uint64_t literalBytes = 0;
        if (RunLengthSizes(data, cel.rleOffset, cel.literalOffset, (size_t)pixels, rleBytes, literalBytes))
        {
            ranges.push_back(ByteRange{ cel.rleOffset, cel.rleOffset + rleBytes });
            ranges.push_back(ByteRange{ cel.literalOffset, cel.literalOffset + literalBytes });
        }
        else
        {
            ranges.push_back(ByteRange{ cel.rleOffset, (std::max)((uint64_t)data.size(), (uint64_t)cel.rleOffset + 1) });
            ranges.push_back(ByteRange{ cel.literalOffset, (std::max)((uint64_t)data.size(), (uint64_t)cel.literalOffset + 1) });
        }
        if (sci2 && (cel.rowTableOffset != 0))
        {
            ranges.push_back(ByteRange{ cel.rowTableOffset, cel.rowTableOffset + (8 * (uint64_t)cel.height) });
        }
        return ranges;
    }

    // The pixels of a run-length cel as an SCI2 interpreter draws it: each
    // row from the offsets of the row table, with the width of the cel.
    // False for a row table or data that a row cannot be read from.
    bool DecodeByRowTable(const std::vector<uint8_t> &data, const ViewCelLayout &cel, std::vector<uint8_t> &pixels)
    {
        pixels.clear();
        const uint64_t table = cel.rowTableOffset;
        if ((table + (8 * (uint64_t)cel.height)) > data.size())
        {
            return false;
        }
        for (int y = 0; y < cel.height; y++)
        {
            uint64_t r = (uint64_t)cel.rleOffset + DwordAt(data.data(), (size_t)(table + (4 * (uint64_t)y)));
            uint64_t l = (uint64_t)cel.literalOffset + DwordAt(data.data(), (size_t)(table + (4 * ((uint64_t)cel.height + y))));
            int x = 0;
            while (x < cel.width)
            {
                if (r >= data.size())
                {
                    return false;
                }
                const uint8_t code = data[(size_t)r++];
                int count = code & 0x3f;
                if ((code >> 6) == 3)
                {
                    pixels.insert(pixels.end(), (size_t)count, cel.skipColor);
                }
                else if ((code >> 6) == 2)
                {
                    if (l >= data.size())
                    {
                        return false;
                    }
                    pixels.insert(pixels.end(), (size_t)count, data[(size_t)l++]);
                }
                else
                {
                    count += ((code >> 6) == 1) ? 64 : 0;
                    if ((l + count) > data.size())
                    {
                        return false;
                    }
                    pixels.insert(pixels.end(), data.begin() + (size_t)l, data.begin() + (size_t)l + count);
                    l += count;
                }
                if (count == 0)
                {
                    return false;
                }
                x += count;
            }
            if (x != cel.width)
            {
                return false;
            }
        }
        return true;
    }
}

sci::Result<std::vector<uint8_t>> ReplaceViewCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, const std::vector<ViewCelPixels> &cels)
{
    SCI_TRY_ASSIGN(ViewDescription before, DescribeView(version, data));
    SCI_TRY_ASSIGN(std::vector<std::vector<uint8_t>> oldPixels, GetAllViewCelPixels(version, data));
    SCI_TRY_ASSIGN(ViewLayout layout, ReadViewLayout(data.data(), data.size()));
    const bool sci2 = (version.ViewFormat == ViewFormat::VGA2);

    // The index in before.cels of each changed cel, and its new pixels.
    std::vector<std::pair<size_t, const std::vector<uint8_t> *>> changes;
    for (const ViewCelPixels &cel : cels)
    {
        const ViewCelDescription *found = before.Find(cel.loop, cel.cel);
        if (found == nullptr)
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("the view stores no cel {0} of loop {1}", cel.cel, cel.loop));
        }
        const size_t index = (size_t)(found - before.cels.data());
        if (std::any_of(changes.begin(), changes.end(), [&](const std::pair<size_t, const std::vector<uint8_t> *> &change) { return change.first == index; }))
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("two pixel sets are for cel {0} of loop {1}", cel.cel, cel.loop));
        }
        const size_t count = (size_t)found->width * (size_t)found->height;
        if (cel.pixels.size() != count)
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("cel {0} of loop {1} has {2} x {3} pixels ({4} bytes), and the new pixels are {5} bytes", cel.cel, cel.loop, found->width, found->height, count, cel.pixels.size()));
        }
        changes.emplace_back(index, &cel.pixels);
    }

    // The cel headers in the order of before.cels.
    std::vector<const ViewCelLayout *> headers;
    for (const ViewLoopLayout &loop : layout.loops)
    {
        for (const ViewCelLayout &cel : loop.cels)
        {
            headers.push_back(&cel);
        }
    }

    // The bytes that a cel must not take: the headers, the palette, and the
    // data of each other cel header.
    uint64_t headersEnd = (uint64_t)layout.headerSize + ((uint64_t)layout.loopHeaderSize * layout.loops.size());
    for (const ViewCelLayout *header : headers)
    {
        headersEnd = (std::max)(headersEnd, (uint64_t)header->headerOffset + layout.celHeaderSize);
    }
    std::vector<ByteRange> fixed = { ByteRange{ 0, headersEnd } };
    ByteRange paletteRange;
    if (layout.paletteOffset != 0)
    {
        SCI_TRY_ASSIGN(PaletteLayout palette, ReadPaletteLayout(data.data(), data.size(), layout.paletteOffset));
        paletteRange = ByteRange{ layout.paletteOffset, palette.entriesOffset + ((uint64_t)palette.entryCount * (uint64_t)palette.entrySize) };
        fixed.push_back(paletteRange);
    }

    std::vector<uint8_t> patched = data;
    std::vector<size_t> changedRunLength;
    for (const auto &change : changes)
    {
        const std::vector<uint8_t> &pixels = *change.second;
        if (pixels == oldPixels[change.first])
        {
            continue;
        }
        const ViewCelLayout &header = *headers[change.first];
        const ViewCelDescription &cel = before.cels[change.first];
        // Two loops can point at the same cel headers: such a header is one
        // cel for each of them.
        for (size_t other = 0; other < headers.size(); other++)
        {
            if ((other != change.first) && (headers[other]->headerOffset == header.headerOffset))
            {
                const ViewCelDescription &twin = before.cels[other];
                return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("cel {0} of loop {1} has the cel header of cel {2} of loop {3}, so it cannot get pixels of its own", cel.cel, cel.loop, twin.cel, twin.loop));
            }
        }
        // With the bit 0x80, byte 10 of the header tells the interpreters
        // whether the pixels have the skip colour and remap colours.
        if (data[header.headerOffset + 10] & 0x80)
        {
            return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("byte 10 of the header of cel {0} of loop {1} describes its pixels (the bit 0x80 is set), and new pixels can make it wrong", cel.cel, cel.loop));
        }
        std::vector<ByteRange> others = fixed;
        for (const ViewCelLayout *other : headers)
        {
            if (other->headerOffset != header.headerOffset)
            {
                const std::vector<ByteRange> ranges = DataRangesOf(data, *other, sci2);
                others.insert(others.end(), ranges.begin(), ranges.end());
            }
        }
        const std::vector<ByteRange> own = DataRangesOf(data, header, sci2);
        const bool alone = !OverlapAny(own, others);
        if (header.IsRaw())
        {
            const bool inData = ((uint64_t)header.rleOffset + pixels.size()) <= data.size();
            if (alone && inData)
            {
                std::copy(pixels.begin(), pixels.end(), patched.begin() + header.rleOffset);
            }
            else
            {
                PutDwordAt(patched, header.headerOffset + 24, OffsetAtEnd(patched));
                patched.insert(patched.end(), pixels.begin(), pixels.end());
            }
            continue;
        }
        SCI_TRY_ASSIGN(EncodedViewCel encoded, EncodeViewCel(cel.width, cel.height, cel.skipColor, pixels));
        changedRunLength.push_back(change.first);
        PutDwordAt(patched, header.headerOffset + 12, (uint32_t)(encoded.rle.size() + encoded.literal.size()));
        PutDwordAt(patched, header.headerOffset + 16, (uint32_t)encoded.rle.size());
        // The new data takes the place of the old data when it fits there
        // and no other data has those bytes.
        uint64_t rleBytes = 0;
        uint64_t literalBytes = 0;
        const bool fits = alone && RunLengthSizes(data, header.rleOffset, header.literalOffset, pixels.size(), rleBytes, literalBytes) &&
            (encoded.rle.size() <= rleBytes) && (encoded.literal.size() <= literalBytes) &&
            (!sci2 || ((header.rowTableOffset != 0) && (((uint64_t)header.rowTableOffset + (8 * (uint64_t)cel.height)) <= data.size())));
        uint32_t rleAt = header.rleOffset;
        uint32_t literalAt = header.literalOffset;
        uint32_t tableAt = header.rowTableOffset;
        if (!fits)
        {
            rleAt = OffsetAtEnd(patched);
            literalAt = rleAt + (uint32_t)encoded.rle.size();
            tableAt = literalAt + (uint32_t)encoded.literal.size();
            patched.resize(patched.size() + encoded.rle.size() + encoded.literal.size() + (sci2 ? (8 * (size_t)cel.height) : 0));
            PutDwordAt(patched, header.headerOffset + 24, rleAt);
            PutDwordAt(patched, header.headerOffset + 28, literalAt);
            if (sci2)
            {
                PutDwordAt(patched, header.headerOffset + 32, tableAt);
            }
        }
        std::copy(encoded.rle.begin(), encoded.rle.end(), patched.begin() + rleAt);
        std::copy(encoded.literal.begin(), encoded.literal.end(), patched.begin() + literalAt);
        if (sci2)
        {
            for (int y = 0; y < cel.height; y++)
            {
                PutDwordAt(patched, tableAt + (4 * (size_t)y), encoded.rleRows[y]);
                PutDwordAt(patched, tableAt + (4 * ((size_t)cel.height + y)), encoded.literalRows[y]);
            }
        }
    }
    if ((version.ViewFormat == ViewFormat::VGA1_1) && (patched.size() > 0xffff) && (patched.size() > data.size()))
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("the view would have {0} bytes; Sierra's interpreter of SCI1.1 reads a view of 65535 bytes or fewer", patched.size()));
    }

    // The new bytes must read back as the view with the new pixels: in the
    // reader of SCI Companion, and in the row tables of SCI2. The palette
    // keeps its bytes.
    SCI_TRY_ASSIGN(ViewDescription after, DescribeView(version, patched));
    SCI_TRY_ASSIGN(std::vector<std::vector<uint8_t>> newPixels, GetAllViewCelPixels(version, patched));
    SCI_TRY_ASSIGN(ViewLayout newLayout, ReadViewLayout(patched.data(), patched.size()));
    bool same = (after.loops.size() == before.loops.size()) && (after.cels.size() == before.cels.size()) &&
        std::equal(data.begin() + (size_t)paletteRange.first, data.begin() + (size_t)paletteRange.end, patched.begin() + (size_t)paletteRange.first);
    for (size_t i = 0; same && (i < before.loops.size()); i++)
    {
        same = (after.loops[i].mirrorOf == before.loops[i].mirrorOf) && (after.loops[i].cels == before.loops[i].cels);
    }
    std::vector<const ViewCelLayout *> newHeaders;
    for (const ViewLoopLayout &loop : newLayout.loops)
    {
        for (const ViewCelLayout &cel : loop.cels)
        {
            newHeaders.push_back(&cel);
        }
    }
    for (size_t i = 0; same && (i < before.cels.size()); i++)
    {
        const ViewCelDescription &a = before.cels[i];
        const ViewCelDescription &b = after.cels[i];
        same = (a.loop == b.loop) && (a.cel == b.cel) && (a.width == b.width) && (a.height == b.height) && (a.x == b.x) && (a.y == b.y) && (a.skipColor == b.skipColor);
        auto change = std::find_if(changes.begin(), changes.end(), [&](const std::pair<size_t, const std::vector<uint8_t> *> &c) { return c.first == i; });
        const std::vector<uint8_t> &expected = (change != changes.end()) ? *change->second : oldPixels[i];
        same = same && (newPixels[i] == expected);
        if (same && sci2 && (std::find(changedRunLength.begin(), changedRunLength.end(), i) != changedRunLength.end()))
        {
            std::vector<uint8_t> rows;
            same = DecodeByRowTable(patched, *newHeaders[i], rows) && (rows == expected);
        }
    }
    if (!same)
    {
        return sci::Fail(sci::ErrorCode::Internal, "the view with the new pixels does not read back as the view with these pixels");
    }
    return patched;
}

sci::Result<std::vector<std::vector<uint8_t>>> GetAllViewCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data)
{
    SCI_TRY_ASSIGN(ViewRead read, ReadView(version, data));
    const RasterComponent &raster = read.view->GetComponent<RasterComponent>();
    std::vector<std::vector<uint8_t>> cels;
    for (size_t i = 0; i < read.layout.loops.size(); i++)
    {
        for (size_t c = 0; c < read.layout.loops[i].cels.size(); c++)
        {
            SCI_TRY_ASSIGN(std::vector<uint8_t> pixels, CelTopDownPixels(raster.Loops[i].Cels[c]));
            cels.push_back(std::move(pixels));
        }
    }
    return cels;
}
