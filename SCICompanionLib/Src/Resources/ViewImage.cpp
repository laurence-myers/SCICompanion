#include "stdafx.h"
#include "ViewImage.h"
#include "View.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceUtil.h"
#include "ResourceSourceFlags.h"
#include "GameFolderHelper.h"
#include "CoreLog.h"
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
