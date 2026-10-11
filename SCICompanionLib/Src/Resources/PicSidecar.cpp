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

namespace
{
    // The error of a key, with the line of the key in the file.
    tl::unexpected<sci::Error> BadKey(const toml::node &node, const std::string &message)
    {
        sci::ErrorLocation where;
        where.line = (int)node.source().begin.line;
        where.column = (int)node.source().begin.column;
        return sci::Fail(sci::ErrorCode::Format, message, where);
    }

    // The whole number of a key, from low to high. The value stays when the
    // table has no such key.
    template<typename T>
    sci::Status ReadNumber(const toml::table &table, const std::string &name, const char *key, int64_t low, int64_t high, T &value)
    {
        const toml::node *node = table.get(key);
        if (node == nullptr)
        {
            return sci::Ok();
        }
        std::optional<int64_t> number = node->value_exact<int64_t>();
        if (!number)
        {
            return BadKey(*node, fmt::format("{0}: \"{1}\" is not a whole number", name, key));
        }
        if ((*number < low) || (*number > high))
        {
            return BadKey(*node, fmt::format("{0}: \"{1}\" is {2}; it must be {3} to {4}", name, key, *number, low, high));
        }
        value = (T)*number;
        return sci::Ok();
    }

    sci::Status ReadText(const toml::table &table, const std::string &name, const char *key, std::string &value)
    {
        const toml::node *node = table.get(key);
        if (node == nullptr)
        {
            return sci::Ok();
        }
        std::optional<std::string> text = node->value_exact<std::string>();
        if (!text)
        {
            return BadKey(*node, fmt::format("{0}: \"{1}\" is not a text", name, key));
        }
        value = *text;
        return sci::Ok();
    }

    sci::Status ReadFlag(const toml::table &table, const std::string &name, const char *key, bool &value)
    {
        const toml::node *node = table.get(key);
        if (node == nullptr)
        {
            return sci::Ok();
        }
        std::optional<bool> flag = node->value_exact<bool>();
        if (!flag)
        {
            return BadKey(*node, fmt::format("{0}: \"{1}\" is not true or false", name, key));
        }
        value = *flag;
        return sci::Ok();
    }

    // The numbers of an array of arrays of palette entries: each inner array
    // has "count" numbers of 0 to 255, and its first is not more than its
    // second.
    sci::Result<std::vector<std::vector<int>>> ReadEntryArrays(const toml::table &table, const std::string &name, const char *key, size_t count)
    {
        std::vector<std::vector<int>> arrays;
        const toml::node *node = table.get(key);
        if (node == nullptr)
        {
            return arrays;
        }
        const toml::array *outer = node->as_array();
        if (outer == nullptr)
        {
            return BadKey(*node, fmt::format("{0}: \"{1}\" is not an array", name, key));
        }
        for (const toml::node &element : *outer)
        {
            const toml::array *inner = element.as_array();
            std::vector<int> numbers;
            if ((inner != nullptr) && (inner->size() == count))
            {
                for (const toml::node &number : *inner)
                {
                    std::optional<int64_t> entry = number.value_exact<int64_t>();
                    if (entry && (*entry >= 0) && (*entry <= 255))
                    {
                        numbers.push_back((int)*entry);
                    }
                }
            }
            if ((numbers.size() != count) || (numbers[0] > numbers[1]))
            {
                return BadKey(element, fmt::format("{0}: an element of \"{1}\" is not {2} numbers of 0 to 255 with the first not more than the second", name, key, count));
            }
            arrays.push_back(numbers);
        }
        return arrays;
    }

    sci::Status ReadRuns(const toml::table &table, const std::string &name, const char *key, std::vector<IndexRun> &runs)
    {
        SCI_TRY_ASSIGN(std::vector<std::vector<int>> arrays, ReadEntryArrays(table, name, key, 2));
        runs.clear();
        for (const std::vector<int> &numbers : arrays)
        {
            IndexRun run;
            run.first = numbers[0];
            run.last = numbers[1];
            runs.push_back(run);
        }
        return sci::Ok();
    }

    // A file name with no folder: no separator, no drive, and not "." or
    // "..".
    bool IsPlainFileName(const std::string &name)
    {
        return !name.empty() && (name != ".") && (name != "..") && (name.find_first_of("\\/:") == std::string::npos);
    }

    sci::Result<PicSidecar> SidecarOf(const toml::table &root)
    {
        PicSidecar sidecar;
        const toml::table *source = root["source"].as_table();
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

        if (const toml::table *pic = root["pic"].as_table())
        {
            SCI_TRY(ReadNumber(*pic, "[pic]", "width", 0, 65535, sidecar.width));
            SCI_TRY(ReadNumber(*pic, "[pic]", "height", 0, 65535, sidecar.height));
            SCI_TRY(ReadNumber(*pic, "[pic]", "background_cel", -1, 255, sidecar.backgroundCel));
        }

        if (const toml::table *palette = root["palette"].as_table())
        {
            SCI_TRY(ReadFlag(*palette, "[palette]", "present", sidecar.hasPalette));
            SCI_TRY(ReadNumber(*palette, "[palette]", "stored_first", 0, 255, sidecar.storedFirst));
            SCI_TRY(ReadNumber(*palette, "[palette]", "stored_count", 0, 256, sidecar.storedCount));
            SCI_TRY(ReadNumber(*palette, "[palette]", "entry_size", 0, 4, sidecar.entrySize));
            SCI_TRY_ASSIGN(std::vector<std::vector<int>> flags, ReadEntryArrays(*palette, "[palette]", "flags", 3));
            for (const std::vector<int> &numbers : flags)
            {
                PicSidecarFlagRun run;
                run.first = numbers[0];
                run.last = numbers[1];
                run.flag = numbers[2];
                sidecar.flags.push_back(run);
            }
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
                SCI_TRY(ReadText(*cel, name, "file", sidecarCel.file));
                if ((cel->get("file") != nullptr) && !IsPlainFileName(sidecarCel.file))
                {
                    return BadKey(*cel->get("file"), fmt::format("{0}: \"file\" must be a file name with no folder", name));
                }
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
    // The exception boundary of the TOML parser.
    return sci::Guard("reading the sidecar of a pic", [&]() -> sci::Result<PicSidecar>
    {
        toml::table root;
        try
        {
            root = toml::parse(text);
        }
        catch (const toml::parse_error &error)
        {
            sci::ErrorLocation where;
            where.line = (int)error.source().begin.line;
            where.column = (int)error.source().begin.column;
            return sci::Fail(sci::ErrorCode::Format, std::string(error.description()), where);
        }
        return SidecarOf(root);
    });
}

sci::Result<PicSidecar> ReadPicSidecarFile(const std::string &path)
{
    // The exception boundary of the walk of the tables.
    sci::Result<PicSidecar> sidecar = sci::Guard("reading " + path, [&]() -> sci::Result<PicSidecar>
    {
        SCI_TRY_ASSIGN(toml::table root, ParseTomlFile(path));
        return SidecarOf(root);
    });
    if (!sidecar && sidecar.error().where.file.empty())
    {
        sidecar.error().where.file = path;
    }
    return sidecar;
}
