#include "stdafx.h"
#include "PicImage.h"
#include "Pic.h"
#include "PaletteOperations.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceUtil.h"
#include "ResourceSourceFlags.h"
#include "GameFolderHelper.h"
#include "View.h"
#include "format.h"

namespace
{
    tl::unexpected<sci::Error> NoImage(PicFormat format)
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("the image of a pic of the format {0} cannot be read; only the formats vga11 and vga2 can", PicFormatName(format)));
    }

    tl::unexpected<sci::Error> NoSuchCel(int cel, size_t celCount)
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("the pic has no cel {0}; it has {1} cel{2}", cel, celCount, (celCount == 1) ? "" : "s"));
    }

    void CountPixels(const uint8_t *pixels, size_t count, std::array<uint32_t, 256> &counts)
    {
        for (size_t i = 0; i < count; i++)
        {
            counts[pixels[i]]++;
        }
    }

    // The first cel with the largest area; -1 when there is no cel.
    int LargestCel(const std::vector<PicCelDescription> &cels)
    {
        int largest = -1;
        int64_t largestArea = -1;
        for (const PicCelDescription &cel : cels)
        {
            int64_t area = (int64_t)cel.width * (int64_t)cel.height;
            if (area > largestArea)
            {
                largest = cel.index;
                largestArea = area;
            }
        }
        return largest;
    }

    sci::Result<PicPaletteDescription> DescribePalette(const std::vector<uint8_t> &data, uint32_t paletteOffset)
    {
        PicPaletteDescription palette;
        if (paletteOffset == 0)
        {
            return palette;
        }
        SCI_TRY_ASSIGN(PaletteLayout layout, ReadPaletteLayout(data.data(), data.size(), paletteOffset));
        palette.present = true;
        palette.firstEntry = layout.firstEntry;
        palette.entryCount = layout.entryCount;
        palette.entrySize = layout.entrySize;
        for (int i = 0; i < layout.entryCount; i++)
        {
            const uint8_t *entry = data.data() + layout.entriesOffset + ((size_t)i * (size_t)layout.entrySize);
            const int index = layout.firstEntry + i;
            // A 4-byte entry has its flag byte first.
            const uint8_t *color = entry + ((layout.entrySize == 4) ? 1 : 0);
            palette.flags[index] = (layout.entrySize == 4) ? entry[0] : (uint8_t)1;
            palette.colors[index].red = color[0];
            palette.colors[index].green = color[1];
            palette.colors[index].blue = color[2];
        }
        return palette;
    }

    // The pic as SCI Companion reads it. An SCI1.1 pic needs it: its cel is
    // run-length data.
    sci::Result<std::unique_ptr<ResourceEntity>> ParsePic(const SCIVersion &version, const std::vector<uint8_t> &data)
    {
        GameFolderHelper helper;
        // The blob has a name, so that it does not look for one in a game.
        ResourceBlob blob(helper, "pic", ResourceType::Pic, data, version.DefaultVolumeFile, -1, NoBase36, version, ResourceSourceFlags::PatchFile);
        sci::Result<std::unique_ptr<ResourceEntity>> pic = TryCreateResourceFromResourceData(blob);
        if (!pic)
        {
            // The number of the pic is not known here: the caller names it.
            pic.error().where.resource.clear();
        }
        else if ((*pic)->TryGetComponent<PicComponent>() == nullptr)
        {
            return sci::Fail(sci::ErrorCode::Format, "the resource is not a pic");
        }
        return pic;
    }

    // The cel of the first bitmap command of a pic; null when it has none.
    Cel *BitmapCelOf(const ResourceEntity &pic)
    {
        for (const PicCommand &command : pic.GetComponent<PicComponent>().commands)
        {
            if (command.type == PicCommand::CommandType::DrawBitmap)
            {
                return command.drawVisualBitmap.pCel;
            }
        }
        return nullptr;
    }

    // A cel as SCI Companion holds it has the bottom row first, and each row
    // has the bytes of its stride. Format when the cel has fewer bytes.
    sci::Status CheckCelData(const Cel &cel)
    {
        const size_t needed = StrideOf(cel) * (size_t)cel.size.cy;
        if (cel.Data.size() < needed)
        {
            return sci::Fail(sci::ErrorCode::Format, fmt::format("the cel of the pic has {0} bytes; {1} x {2} pixels need {3}", cel.Data.size(), cel.size.cx, cel.size.cy, needed));
        }
        return sci::Ok();
    }

    // The pixels of the cel as rows from the top, with no padding.
    sci::Result<std::vector<uint8_t>> TopDownPixels(const Cel &cel)
    {
        SCI_TRY(CheckCelData(cel));
        const size_t width = cel.size.cx;
        const size_t height = cel.size.cy;
        const size_t stride = StrideOf(cel);
        std::vector<uint8_t> pixels(width * height);
        // A cel of no width has no bytes to point at.
        for (size_t y = 0; (width > 0) && (y < height); y++)
        {
            const uint8_t *row = &cel.Data[(height - 1 - y) * stride];
            std::copy(row, row + width, pixels.begin() + (y * width));
        }
        return pixels;
    }

    // Puts rows from the top (width * height bytes) into the cel. The
    // padding bytes of the cel stay.
    sci::Status SetTopDownPixels(Cel &cel, const std::vector<uint8_t> &pixels)
    {
        SCI_TRY(CheckCelData(cel));
        const size_t width = cel.size.cx;
        const size_t height = cel.size.cy;
        const size_t stride = StrideOf(cel);
        for (size_t y = 0; (width > 0) && (y < height); y++)
        {
            const uint8_t *row = pixels.data() + (y * width);
            std::copy(row, row + width, &cel.Data[(height - 1 - y) * stride]);
        }
        return sci::Ok();
    }

    // The two cels have the same size, placement, skip colour and pixels.
    bool SameCel(const Cel &one, const Cel &two)
    {
        if ((one.size != two.size) || (one.placement != two.placement) || (one.TransparentColor != two.TransparentColor))
        {
            return false;
        }
        sci::Result<std::vector<uint8_t>> onePixels = TopDownPixels(one);
        sci::Result<std::vector<uint8_t>> twoPixels = TopDownPixels(two);
        return onePixels && twoPixels && (*onePixels == *twoPixels);
    }

    // The data that two commands point at is the same, or neither has data.
    template<typename T, typename TSame>
    bool SameData(const T *one, const T *two, TSame same)
    {
        return ((one == nullptr) || (two == nullptr)) ? (one == two) : same(*one, *two);
    }

    // The two commands have the same type and the same values. A command
    // that points at data (an EGA palette, the priority bars, a cel) is
    // compared by its values and that data. Each other command is compared
    // by its bytes: PicCommand() sets them all to 0 before the values.
    bool SameCommand(const PicCommand &one, const PicCommand &two)
    {
        if (one.type != two.type)
        {
            return false;
        }
        switch (one.type)
        {
        case PicCommand::SetPalette:
            return (one.setPalette.bPaletteNumber == two.setPalette.bPaletteNumber) &&
                SameData(one.setPalette.pPalette, two.setPalette.pPalette, [](const EGACOLOR &a, const EGACOLOR &b)
                {
                    return memcmp(&a, &b, sizeof(EGACOLOR) * PALETTE_SIZE) == 0;
                });
        case PicCommand::SetPriorityBars:
            return (one.setPriorityBars.is16Bit == two.setPriorityBars.is16Bit) && (one.setPriorityBars.isVGA == two.setPriorityBars.isVGA) &&
                SameData(one.setPriorityBars.pPriorityLines, two.setPriorityBars.pPriorityLines, [](const uint16_t &a, const uint16_t &b)
                {
                    return memcmp(&a, &b, sizeof(uint16_t) * NumPriorityBars) == 0;
                });
        case PicCommand::DrawBitmap:
            return (one.drawVisualBitmap.priority == two.drawVisualBitmap.priority) && (one.drawVisualBitmap.mirrored == two.drawVisualBitmap.mirrored) &&
                (one.drawVisualBitmap.isVGA == two.drawVisualBitmap.isVGA) && SameData(one.drawVisualBitmap.pCel, two.drawVisualBitmap.pCel, SameCel);
        default:
            return memcmp(&one, &two, sizeof(PicCommand)) == 0;
        }
    }

    // What of the two pics is not the same: the size, a command (the cel
    // and the priority bars are commands) or the palette. Empty when all of
    // them are the same.
    std::string DifferenceOf(const ResourceEntity &one, const ResourceEntity &two)
    {
        const PicComponent &onePic = one.GetComponent<PicComponent>();
        const PicComponent &twoPic = two.GetComponent<PicComponent>();
        if (onePic.Size != twoPic.Size)
        {
            return "the size of the pic";
        }
        if (onePic.commands.size() != twoPic.commands.size())
        {
            return fmt::format("the count of the commands of the pic ({0}, then {1})", onePic.commands.size(), twoPic.commands.size());
        }
        for (size_t i = 0; i < onePic.commands.size(); i++)
        {
            if (!SameCommand(onePic.commands[i], twoPic.commands[i]))
            {
                return fmt::format("command {0} of the pic", i);
            }
        }
        const PaletteComponent *onePalette = one.TryGetComponent<PaletteComponent>();
        const PaletteComponent *twoPalette = two.TryGetComponent<PaletteComponent>();
        if (!SameData(onePalette, twoPalette, [](const PaletteComponent &a, const PaletteComponent &b)
            {
                return memcmp(a.Colors, b.Colors, sizeof(a.Colors)) == 0;
            }))
        {
            return "the palette of the pic";
        }
        return std::string();
    }

    sci::Result<PicDescription> DescribeVGA2(const std::vector<uint8_t> &data)
    {
        SCI_TRY_ASSIGN(PicLayoutVGA2 layout, ReadPicLayoutVGA2(data.data(), data.size()));
        PicDescription description;
        description.format = PicFormat::VGA2;
        // As PicReadFromVGA2: a pic is 320 x 200 or more, and not smaller
        // than its largest cel.
        description.width = (std::max)((int)layout.width, 320);
        description.height = (std::max)((int)layout.height, 200);
        for (size_t i = 0; i < layout.cels.size(); i++)
        {
            const PicCelLayoutVGA2 &cel = layout.cels[i];
            PicCelDescription celDescription;
            celDescription.index = (int)i;
            celDescription.width = cel.width;
            celDescription.height = cel.height;
            celDescription.x = cel.x;
            celDescription.y = cel.y;
            celDescription.priority = cel.priority;
            celDescription.skipColor = cel.skipColor;
            celDescription.compressed = cel.compressed;
            if (!cel.compressed)
            {
                CountPixels(data.data() + cel.pixelOffset, cel.pixelSize, celDescription.pixelCounts);
            }
            description.width = (std::max)(description.width, celDescription.width);
            description.height = (std::max)(description.height, celDescription.height);
            description.cels.push_back(celDescription);
        }
        description.backgroundCel = LargestCel(description.cels);
        SCI_TRY_ASSIGN(description.palette, DescribePalette(data, layout.paletteOffset));
        return description;
    }

    sci::Result<PicDescription> DescribeVGA11(const SCIVersion &version, const std::vector<uint8_t> &data)
    {
        SCI_TRY_ASSIGN(uint32_t paletteOffset, ReadPicPaletteOffsetVGA11(data.data(), data.size()));
        SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> pic, ParsePic(version, data));
        PicDescription description;
        description.format = PicFormat::VGA1_1;
        const PicComponent &component = pic->GetComponent<PicComponent>();
        description.width = component.Size.cx;
        description.height = component.Size.cy;
        const Cel *cel = BitmapCelOf(*pic);
        if (cel != nullptr)
        {
            PicCelDescription celDescription;
            celDescription.index = 0;
            celDescription.width = cel->size.cx;
            celDescription.height = cel->size.cy;
            celDescription.x = cel->placement.x;
            celDescription.y = cel->placement.y;
            celDescription.skipColor = cel->TransparentColor;
            SCI_TRY_ASSIGN(std::vector<uint8_t> pixels, TopDownPixels(*cel));
            CountPixels(pixels.data(), pixels.size(), celDescription.pixelCounts);
            description.cels.push_back(celDescription);
        }
        description.backgroundCel = LargestCel(description.cels);
        SCI_TRY_ASSIGN(description.palette, DescribePalette(data, paletteOffset));
        return description;
    }
}

std::vector<IndexRun> IndexRunsOf(const std::array<bool, 256> &entries)
{
    std::vector<IndexRun> runs;
    for (int i = 0; i < (int)entries.size(); i++)
    {
        if (!entries[i])
        {
            continue;
        }
        if (!runs.empty() && (runs.back().last == (i - 1)))
        {
            runs.back().last = i;
        }
        else
        {
            IndexRun run;
            run.first = i;
            run.last = i;
            runs.push_back(run);
        }
    }
    return runs;
}

std::string IndexRunsText(const std::vector<IndexRun> &runs)
{
    std::string text;
    for (const IndexRun &run : runs)
    {
        text += text.empty() ? "" : ",";
        text += (run.first == run.last) ? fmt::format("{0}", run.first) : fmt::format("{0}-{1}", run.first, run.last);
    }
    return text;
}

std::array<bool, 256> PicCelDescription::UsedEntries() const
{
    std::array<bool, 256> used = {};
    for (size_t i = 0; i < used.size(); i++)
    {
        used[i] = (pixelCounts[i] != 0);
    }
    return used;
}

std::array<bool, 256> PicPaletteDescription::UsedEntries() const
{
    std::array<bool, 256> used = {};
    for (size_t i = 0; i < used.size(); i++)
    {
        used[i] = (flags[i] != 0);
    }
    return used;
}

const char *PicFormatName(PicFormat format)
{
    switch (format)
    {
    case PicFormat::EGA:
        return "ega";
    case PicFormat::VGA1:
        return "vga1";
    case PicFormat::VGA1_1:
        return "vga11";
    case PicFormat::VGA2:
        return "vga2";
    }
    return "unknown";
}

bool CanReadPicImage(PicFormat format)
{
    return (format == PicFormat::VGA1_1) || (format == PicFormat::VGA2);
}

sci::Result<PicDescription> DescribePic(const SCIVersion &version, const std::vector<uint8_t> &data)
{
    switch (version.PicFormat)
    {
    case PicFormat::VGA2:
        return DescribeVGA2(data);
    case PicFormat::VGA1_1:
        return DescribeVGA11(version, data);
    default:
        return NoImage(version.PicFormat);
    }
}

sci::Result<std::vector<uint8_t>> GetCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, int cel)
{
    switch (version.PicFormat)
    {
    case PicFormat::VGA2:
    {
        SCI_TRY_ASSIGN(PicLayoutVGA2 layout, ReadPicLayoutVGA2(data.data(), data.size()));
        if ((cel < 0) || ((size_t)cel >= layout.cels.size()))
        {
            return NoSuchCel(cel, layout.cels.size());
        }
        const PicCelLayoutVGA2 &target = layout.cels[cel];
        if (target.compressed)
        {
            return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("cel {0} is compressed; only the pixels of a cel that is not compressed can be read", cel));
        }
        return std::vector<uint8_t>(data.begin() + target.pixelOffset, data.begin() + target.pixelOffset + target.pixelSize);
    }
    case PicFormat::VGA1_1:
    {
        SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> pic, ParsePic(version, data));
        const Cel *target = BitmapCelOf(*pic);
        if ((cel != 0) || (target == nullptr))
        {
            return NoSuchCel(cel, (target == nullptr) ? 0 : 1);
        }
        return TopDownPixels(*target);
    }
    default:
        return NoImage(version.PicFormat);
    }
}

sci::Result<std::vector<uint8_t>> ReplaceCelPixels(const SCIVersion &version, const std::vector<uint8_t> &data, int cel, const std::vector<uint8_t> &pixels)
{
    switch (version.PicFormat)
    {
    case PicFormat::VGA2:
    {
        std::vector<uint8_t> patched = data;
        SCI_TRY(PatchPicCelPixelsVGA2(patched, cel, pixels));
        return patched;
    }
    case PicFormat::VGA1_1:
    {
        SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> pic, ParsePic(version, data));
        Cel *target = BitmapCelOf(*pic);
        if ((cel != 0) || (target == nullptr))
        {
            return NoSuchCel(cel, (target == nullptr) ? 0 : 1);
        }
        const size_t pixelCount = (size_t)target->size.cx * (size_t)target->size.cy;
        if (pixels.size() != pixelCount)
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("cel {0} has {1} x {2} pixels ({3} bytes), and the new pixels are {4} bytes", cel, target->size.cx, target->size.cy, pixelCount, pixels.size()));
        }
        if (StrideOf(*target) != (size_t)target->GetStride())
        {
            // The writer works with the 16-bit stride of the cel.
            return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("a cel of {0} pixels wide cannot be written", target->size.cx));
        }
        SCI_TRY(SetTopDownPixels(*target, pixels));
        // The exception boundary of the write of the pic.
        SCI_TRY_ASSIGN(std::vector<uint8_t> written, sci::Guard("writing the pic", [&]() -> sci::Result<std::vector<uint8_t>>
        {
            sci::ostream stream;
            std::map<BlobKey, uint32_t> propertyBag;
            // Not a full write: that also writes the polygon file of the pic.
            pic->WriteTo(stream, false, pic->ResourceNumber, propertyBag);
            const uint8_t *bytes = stream.GetInternalPointer();
            return std::vector<uint8_t>(bytes, bytes + stream.GetDataSize());
        }));
        // The writer does not keep each pic (for example, it writes only the
        // first priority table of a pic). So the new bytes must read back as
        // the pic with the new pixels.
        SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> readBack, ParsePic(version, written));
        const std::string difference = DifferenceOf(*pic, *readBack);
        if (!difference.empty())
        {
            return sci::Fail(sci::ErrorCode::Unsupported, "SCI Companion cannot write this pic again with no other change: " + difference + " is different after the write");
        }
        return written;
    }
    default:
        return NoImage(version.PicFormat);
    }
}

sci::Result<std::vector<uint8_t>> ReplacePaletteRange(const SCIVersion &version, const std::vector<uint8_t> &data, int first, int last, const std::array<IndexedBmpColor, 256> &colors)
{
    RGBQUAD quads[256] = {};
    for (size_t i = 0; i < colors.size(); i++)
    {
        quads[i].rgbRed = colors[i].red;
        quads[i].rgbGreen = colors[i].green;
        quads[i].rgbBlue = colors[i].blue;
    }
    std::vector<uint8_t> patched = data;
    switch (version.PicFormat)
    {
    case PicFormat::VGA2:
        SCI_TRY(PatchPicPaletteColorsVGA2(patched, first, last, quads));
        return patched;
    case PicFormat::VGA1_1:
    {
        SCI_TRY_ASSIGN(uint32_t paletteOffset, ReadPicPaletteOffsetVGA11(data.data(), data.size()));
        if (paletteOffset == 0)
        {
            return sci::Fail(sci::ErrorCode::Usage, "the pic has no palette, so no entry can get a new colour");
        }
        SCI_TRY(PatchPaletteColors(patched, paletteOffset, first, last, quads));
        return patched;
    }
    default:
        return NoImage(version.PicFormat);
    }
}
