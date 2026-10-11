#include "stdafx.h"
#include "PicSidecar.h"
#include "TomlFile.h"
#include "format.h"
#include <sstream>

namespace
{
    toml::array RunsArray(const std::vector<IndexRun> &runs)
    {
        toml::array array;
        for (const IndexRun &run : runs)
        {
            array.push_back(toml::array{ (int64_t)run.first, (int64_t)run.last });
        }
        return array;
    }
}

PicSidecar MakePicSidecar(const PicDescription &description)
{
    PicSidecar sidecar;
    sidecar.format = PicFormatName(description.format);
    sidecar.width = description.width;
    sidecar.height = description.height;
    sidecar.backgroundCel = description.backgroundCel;
    sidecar.hasPalette = description.palette.present;
    sidecar.storedFirst = description.palette.firstEntry;
    sidecar.storedCount = description.palette.entryCount;
    sidecar.entrySize = description.palette.entrySize;
    for (int i = 0; i < description.palette.entryCount; i++)
    {
        const int entry = description.palette.firstEntry + i;
        const int flag = description.palette.flags[entry];
        if (!sidecar.flags.empty() && (sidecar.flags.back().last == (entry - 1)) && (sidecar.flags.back().flag == flag))
        {
            sidecar.flags.back().last = entry;
        }
        else
        {
            PicSidecarFlagRun run;
            run.first = entry;
            run.last = entry;
            run.flag = flag;
            sidecar.flags.push_back(run);
        }
    }
    for (const PicCelDescription &cel : description.cels)
    {
        PicSidecarCel sidecarCel;
        sidecarCel.index = cel.index;
        sidecarCel.width = cel.width;
        sidecarCel.height = cel.height;
        sidecarCel.x = cel.x;
        sidecarCel.y = cel.y;
        sidecarCel.priority = cel.priority;
        sidecarCel.skipColor = cel.skipColor;
        sidecarCel.compressed = cel.compressed;
        std::array<bool, 256> entries = cel.UsedEntries();
        entries[cel.skipColor] = false;
        sidecarCel.entries = IndexRunsOf(entries);
        sidecarCel.skipPixels = cel.SkipPixels();
        sidecar.cels.push_back(sidecarCel);
    }
    return sidecar;
}

std::string PicSidecarText(const PicSidecar &sidecar)
{
    toml::table source;
    source.insert("pic", (int64_t)sidecar.pic);
    source.insert("format", sidecar.format);
    source.insert("from", sidecar.from);
    source.insert("size", (int64_t)sidecar.size);
    toml::value<int64_t> crc32((int64_t)sidecar.crc32);
    crc32.flags(toml::value_flags::format_as_hexadecimal);
    source.insert("crc32", std::move(crc32));

    toml::table pic;
    pic.insert("width", (int64_t)sidecar.width);
    pic.insert("height", (int64_t)sidecar.height);
    pic.insert("background_cel", (int64_t)sidecar.backgroundCel);

    toml::table palette;
    palette.insert("present", sidecar.hasPalette);
    palette.insert("stored_first", (int64_t)sidecar.storedFirst);
    palette.insert("stored_count", (int64_t)sidecar.storedCount);
    palette.insert("entry_size", (int64_t)sidecar.entrySize);
    toml::array flags;
    for (const PicSidecarFlagRun &run : sidecar.flags)
    {
        flags.push_back(toml::array{ (int64_t)run.first, (int64_t)run.last, (int64_t)run.flag });
    }
    palette.insert("flags", std::move(flags));
    palette.insert("palette_999", RunsArray(sidecar.palette999));
    palette.insert("used", RunsArray(sidecar.used));

    toml::array cels;
    const bool hasPriority = (sidecar.format == PicFormatName(PicFormat::VGA2));
    for (const PicSidecarCel &cel : sidecar.cels)
    {
        toml::table table;
        table.insert("index", (int64_t)cel.index);
        if (!cel.file.empty())
        {
            table.insert("file", cel.file);
        }
        table.insert("width", (int64_t)cel.width);
        table.insert("height", (int64_t)cel.height);
        table.insert("x", (int64_t)cel.x);
        table.insert("y", (int64_t)cel.y);
        if (hasPriority)
        {
            table.insert("priority", (int64_t)cel.priority);
        }
        table.insert("skip_color", (int64_t)cel.skipColor);
        table.insert("compressed", cel.compressed);
        table.insert("entries", RunsArray(cel.entries));
        table.insert("skip_pixels", (int64_t)cel.skipPixels);
        cels.push_back(std::move(table));
    }

    toml::table root;
    root.insert("source", std::move(source));
    root.insert("pic", std::move(pic));
    root.insert("palette", std::move(palette));
    root.insert("cel", std::move(cels));
    std::ostringstream text;
    text << fmt::format("# scic pic export: pic {0}. The keys are in docs\\scic-cli\\plan.md, section 4.7.\n", sidecar.pic);
    text << root << "\n";
    return text.str();
}
