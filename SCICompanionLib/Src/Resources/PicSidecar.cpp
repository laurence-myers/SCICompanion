#include "stdafx.h"
#include "PicSidecar.h"
#include "SidecarToml.h"
#include "format.h"
#include <sstream>

using namespace sidecartoml;

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
    sidecar.flags = FlagRunsOf(description.palette);
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
    palette.insert("flags", FlagRunsArray(sidecar.flags));
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

namespace
{
    sci::Result<PicSidecar> SidecarOf(const toml::table &root)
    {
        PicSidecar sidecar;
        SCI_TRY_ASSIGN(const toml::table *source, TableOf(root, "source"));
        if ((source == nullptr) || (source->get("pic") == nullptr))
        {
            return sci::Fail(sci::ErrorCode::Format, "the file has no pic number ([source] pic): it is not a file of scic pic export");
        }
        SCI_TRY(ReadNumber(*source, "[source]", "pic", 0, 65535, sidecar.pic));
        SCI_TRY(ReadText(*source, "[source]", "format", sidecar.format));
        SCI_TRY(ReadText(*source, "[source]", "from", sidecar.from));
        SCI_TRY(ReadNumber(*source, "[source]", "size", 0, 0xffffffffLL, sidecar.size));
        sidecar.hasCrc32 = (source->get("crc32") != nullptr);
        SCI_TRY(ReadNumber(*source, "[source]", "crc32", 0, 0xffffffffLL, sidecar.crc32));

        SCI_TRY_ASSIGN(const toml::table *pic, TableOf(root, "pic"));
        if (pic != nullptr)
        {
            SCI_TRY(ReadNumber(*pic, "[pic]", "width", 0, 65535, sidecar.width));
            SCI_TRY(ReadNumber(*pic, "[pic]", "height", 0, 65535, sidecar.height));
            SCI_TRY(ReadNumber(*pic, "[pic]", "background_cel", -1, 255, sidecar.backgroundCel));
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

        if (const toml::node *cels = root.get("cel"))
        {
            const toml::array *array = cels->as_array();
            if (array == nullptr)
            {
                return BadKey(*cels, "\"cel\" is not an array of tables ([[cel]])");
            }
            for (const toml::node &element : *array)
            {
                const toml::table *cel = element.as_table();
                if ((cel == nullptr) || (cel->get("index") == nullptr))
                {
                    return BadKey(element, "a [[cel]] has no index");
                }
                PicSidecarCel sidecarCel;
                SCI_TRY(ReadNumber(*cel, "[[cel]]", "index", 0, 255, sidecarCel.index));
                const std::string name = fmt::format("[[cel]] {0}", sidecarCel.index);
                SCI_TRY(ReadFileName(*cel, name, "file", sidecarCel.file));
                SCI_TRY(ReadNumber(*cel, name, "width", 0, 65535, sidecarCel.width));
                SCI_TRY(ReadNumber(*cel, name, "height", 0, 65535, sidecarCel.height));
                SCI_TRY(ReadNumber(*cel, name, "x", -32768, 32767, sidecarCel.x));
                SCI_TRY(ReadNumber(*cel, name, "y", -32768, 32767, sidecarCel.y));
                SCI_TRY(ReadNumber(*cel, name, "priority", -32768, 32767, sidecarCel.priority));
                SCI_TRY(ReadNumber(*cel, name, "skip_color", 0, 255, sidecarCel.skipColor));
                SCI_TRY(ReadFlag(*cel, name, "compressed", sidecarCel.compressed));
                SCI_TRY(ReadRuns(*cel, name, "entries", sidecarCel.entries));
                SCI_TRY(ReadNumber(*cel, name, "skip_pixels", 0, 0xffffffffLL, sidecarCel.skipPixels));
                sidecar.cels.push_back(sidecarCel);
            }
        }
        return sidecar;
    }
}

sci::Result<PicSidecar> ParsePicSidecar(const std::string &text)
{
    return ParseSidecarText<PicSidecar>(text, "a pic", SidecarOf);
}

sci::Result<PicSidecar> ReadPicSidecarFile(const std::string &path)
{
    return ReadSidecarFile<PicSidecar>(path, SidecarOf);
}
