#include "stdafx.h"
#include "ViewSidecar.h"
#include "SidecarToml.h"
#include "format.h"
#include <sstream>

using namespace sidecartoml;

ViewSidecar MakeViewSidecar(const ViewDescription &description)
{
    ViewSidecar sidecar;
    sidecar.format = ViewFormatName(description.format);
    sidecar.loopCount = (int)description.loops.size();
    sidecar.celCount = (int)description.cels.size();
    sidecar.scaleFlags = description.scaleFlags;
    sidecar.resolution = description.resolution;
    sidecar.hasPalette = description.palette.present;
    sidecar.storedFirst = description.palette.firstEntry;
    sidecar.storedCount = description.palette.entryCount;
    sidecar.entrySize = description.palette.entrySize;
    sidecar.flags = FlagRunsOf(description.palette);
    for (const ViewLoopDescription &loop : description.loops)
    {
        ViewSidecarLoop sidecarLoop;
        sidecarLoop.index = loop.index;
        sidecarLoop.cels = loop.cels;
        sidecarLoop.mirrorOf = loop.mirrorOf;
        sidecar.loops.push_back(sidecarLoop);
    }
    for (const ViewCelDescription &cel : description.cels)
    {
        ViewSidecarCel sidecarCel;
        sidecarCel.loop = cel.loop;
        sidecarCel.cel = cel.cel;
        sidecarCel.width = cel.width;
        sidecarCel.height = cel.height;
        sidecarCel.x = cel.x;
        sidecarCel.y = cel.y;
        sidecarCel.skipColor = cel.skipColor;
        sidecarCel.raw = cel.raw;
        sidecarCel.entries = IndexRunsOf(cel.PixelEntries());
        sidecarCel.skipPixels = cel.SkipPixels();
        sidecar.cels.push_back(sidecarCel);
    }
    return sidecar;
}

std::string ViewSidecarText(const ViewSidecar &sidecar)
{
    toml::table source;
    source.insert("view", (int64_t)sidecar.view);
    source.insert("format", sidecar.format);
    source.insert("from", sidecar.from);
    source.insert("size", (int64_t)sidecar.size);
    toml::value<int64_t> crc32((int64_t)sidecar.crc32);
    crc32.flags(toml::value_flags::format_as_hexadecimal);
    source.insert("crc32", std::move(crc32));

    toml::table view;
    view.insert("loops", (int64_t)sidecar.loopCount);
    view.insert("cels", (int64_t)sidecar.celCount);
    view.insert("scale_flags", (int64_t)sidecar.scaleFlags);
    view.insert("resolution", (int64_t)sidecar.resolution);

    toml::table palette;
    palette.insert("present", sidecar.hasPalette);
    palette.insert("stored_first", (int64_t)sidecar.storedFirst);
    palette.insert("stored_count", (int64_t)sidecar.storedCount);
    palette.insert("entry_size", (int64_t)sidecar.entrySize);
    palette.insert("flags", FlagRunsArray(sidecar.flags));
    palette.insert("palette_999", RunsArray(sidecar.palette999));
    palette.insert("used", RunsArray(sidecar.used));

    toml::array loops;
    for (const ViewSidecarLoop &loop : sidecar.loops)
    {
        toml::table table;
        table.insert("index", (int64_t)loop.index);
        table.insert("cels", (int64_t)loop.cels);
        if (loop.mirrorOf >= 0)
        {
            table.insert("mirror_of", (int64_t)loop.mirrorOf);
        }
        loops.push_back(std::move(table));
    }

    toml::array cels;
    for (const ViewSidecarCel &cel : sidecar.cels)
    {
        toml::table table;
        table.insert("loop", (int64_t)cel.loop);
        table.insert("cel", (int64_t)cel.cel);
        if (!cel.file.empty())
        {
            table.insert("file", cel.file);
        }
        table.insert("width", (int64_t)cel.width);
        table.insert("height", (int64_t)cel.height);
        table.insert("x", (int64_t)cel.x);
        table.insert("y", (int64_t)cel.y);
        table.insert("skip_color", (int64_t)cel.skipColor);
        table.insert("raw", cel.raw);
        table.insert("entries", RunsArray(cel.entries));
        table.insert("skip_pixels", (int64_t)cel.skipPixels);
        cels.push_back(std::move(table));
    }

    toml::table root;
    root.insert("source", std::move(source));
    root.insert("view", std::move(view));
    root.insert("palette", std::move(palette));
    root.insert("loop", std::move(loops));
    root.insert("cel", std::move(cels));
    std::ostringstream text;
    text << fmt::format("# scic view export: view {0}. The keys are in docs\\scic-cli\\plan.md, section 4.11.\n", sidecar.view);
    text << root << "\n";
    return text.str();
}

namespace
{
    sci::Result<ViewSidecar> SidecarOf(const toml::table &root)
    {
        ViewSidecar sidecar;
        SCI_TRY_ASSIGN(const toml::table *source, TableOf(root, "source"));
        sidecar.hasView = (source != nullptr) && (source->get("view") != nullptr);
        sidecar.hasCrc32 = (source != nullptr) && (source->get("crc32") != nullptr);
        if (source != nullptr)
        {
            SCI_TRY(ReadNumber(*source, "[source]", "view", 0, 65535, sidecar.view));
            SCI_TRY(ReadText(*source, "[source]", "format", sidecar.format));
            SCI_TRY(ReadText(*source, "[source]", "from", sidecar.from));
            SCI_TRY(ReadNumber(*source, "[source]", "size", 0, 0xffffffffLL, sidecar.size));
            SCI_TRY(ReadNumber(*source, "[source]", "crc32", 0, 0xffffffffLL, sidecar.crc32));
        }

        SCI_TRY_ASSIGN(const toml::table *view, TableOf(root, "view"));
        if (view != nullptr)
        {
            SCI_TRY(ReadNumber(*view, "[view]", "loops", 0, 255, sidecar.loopCount));
            SCI_TRY(ReadNumber(*view, "[view]", "cels", 0, 65535, sidecar.celCount));
            SCI_TRY(ReadNumber(*view, "[view]", "scale_flags", 0, 255, sidecar.scaleFlags));
            SCI_TRY(ReadNumber(*view, "[view]", "resolution", 0, 255, sidecar.resolution));
        }

        SCI_TRY_ASSIGN(const toml::table *palette, TableOf(root, "palette"));
        if (palette != nullptr)
        {
            SCI_TRY(ReadFlag(*palette, "[palette]", "present", sidecar.hasPalette));
            SCI_TRY(ReadNumber(*palette, "[palette]", "stored_first", 0, 255, sidecar.storedFirst));
            SCI_TRY(ReadNumber(*palette, "[palette]", "stored_count", 0, 256, sidecar.storedCount));
            SCI_TRY(ReadNumber(*palette, "[palette]", "entry_size", 0, 4, sidecar.entrySize));
            SCI_TRY(ReadFlagRuns(*palette, "[palette]", "flags", sidecar.flags));
            SCI_TRY(ReadRuns(*palette, "[palette]", "palette_999", sidecar.palette999));
            SCI_TRY(ReadRuns(*palette, "[palette]", "used", sidecar.used));
        }

        SCI_TRY_ASSIGN(std::vector<const toml::table *> loops, TablesOf(root, "loop"));
        for (const toml::table *loop : loops)
        {
            if (loop->get("index") == nullptr)
            {
                return BadKey(*loop, "a [[loop]] has no index");
            }
            ViewSidecarLoop sidecarLoop;
            SCI_TRY(ReadNumber(*loop, "[[loop]]", "index", 0, 255, sidecarLoop.index));
            const std::string name = fmt::format("[[loop]] {0}", sidecarLoop.index);
            SCI_TRY(ReadNumber(*loop, name, "cels", 0, 255, sidecarLoop.cels));
            SCI_TRY(ReadNumber(*loop, name, "mirror_of", 0, 255, sidecarLoop.mirrorOf));
            sidecar.loops.push_back(sidecarLoop);
        }

        SCI_TRY_ASSIGN(std::vector<const toml::table *> cels, TablesOf(root, "cel"));
        for (const toml::table *cel : cels)
        {
            if ((cel->get("loop") == nullptr) || (cel->get("cel") == nullptr))
            {
                return BadKey(*cel, "a [[cel]] has no loop or no cel");
            }
            ViewSidecarCel sidecarCel;
            SCI_TRY(ReadNumber(*cel, "[[cel]]", "loop", 0, 255, sidecarCel.loop));
            SCI_TRY(ReadNumber(*cel, "[[cel]]", "cel", 0, 255, sidecarCel.cel));
            const std::string name = fmt::format("[[cel]] {0} {1}", sidecarCel.loop, sidecarCel.cel);
            SCI_TRY(ReadFileName(*cel, name, "file", sidecarCel.file));
            SCI_TRY(ReadNumber(*cel, name, "width", 0, 65535, sidecarCel.width));
            SCI_TRY(ReadNumber(*cel, name, "height", 0, 65535, sidecarCel.height));
            SCI_TRY(ReadNumber(*cel, name, "x", -32768, 32767, sidecarCel.x));
            SCI_TRY(ReadNumber(*cel, name, "y", -32768, 32767, sidecarCel.y));
            SCI_TRY(ReadNumber(*cel, name, "skip_color", 0, 255, sidecarCel.skipColor));
            SCI_TRY(ReadFlag(*cel, name, "raw", sidecarCel.raw));
            SCI_TRY(ReadRuns(*cel, name, "entries", sidecarCel.entries));
            SCI_TRY(ReadNumber(*cel, name, "skip_pixels", 0, 0xffffffffLL, sidecarCel.skipPixels));
            sidecar.cels.push_back(sidecarCel);
        }
        return sidecar;
    }
}

sci::Result<ViewSidecar> ParseViewSidecar(const std::string &text)
{
    return ParseSidecarText<ViewSidecar>(text, "a view", SidecarOf);
}

sci::Result<ViewSidecar> ReadViewSidecarFile(const std::string &path)
{
    return ReadSidecarFile<ViewSidecar>(path, SidecarOf);
}
