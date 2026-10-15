#include "stdafx.h"
#include "SidecarToml.h"

namespace sidecartoml
{
    namespace
    {
        // A file name with no folder: no separator, no drive, and not "." or
        // "..".
        bool IsPlainFileName(const std::string &name)
        {
            return !name.empty() && (name != ".") && (name != "..") && (name.find_first_of("\\/:") == std::string::npos);
        }

        // The text of a TOML file (UTF-8) in the ANSI code page, as a path has
        // it. False for a character that the code page does not have.
        bool AnsiOfUtf8(const std::string &utf8, std::string &ansi)
        {
            ansi.clear();
            if (utf8.empty())
            {
                return true;
            }
            int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), nullptr, 0);
            if (wideLength <= 0)
            {
                return false;
            }
            std::wstring wide(wideLength, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), (int)utf8.size(), &wide[0], wideLength);
            BOOL usedDefault = FALSE;
            int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), wideLength, nullptr, 0, nullptr, &usedDefault);
            if ((length <= 0) || usedDefault)
            {
                return false;
            }
            ansi.assign(length, '\0');
            WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), wideLength, &ansi[0], length, nullptr, nullptr);
            return true;
        }
    }

    toml::array RunsArray(const std::vector<IndexRun> &runs)
    {
        toml::array array;
        for (const IndexRun &run : runs)
        {
            array.push_back(toml::array{ (int64_t)run.first, (int64_t)run.last });
        }
        return array;
    }

    std::vector<PicSidecarFlagRun> FlagRunsOf(const PicPaletteDescription &palette)
    {
        std::vector<PicSidecarFlagRun> runs;
        for (int i = 0; i < palette.entryCount; i++)
        {
            const int entry = palette.firstEntry + i;
            const int flag = palette.flags[entry];
            if (!runs.empty() && (runs.back().last == (entry - 1)) && (runs.back().flag == flag))
            {
                runs.back().last = entry;
            }
            else
            {
                PicSidecarFlagRun run;
                run.first = entry;
                run.last = entry;
                run.flag = flag;
                runs.push_back(run);
            }
        }
        return runs;
    }

    toml::array FlagRunsArray(const std::vector<PicSidecarFlagRun> &runs)
    {
        toml::array array;
        for (const PicSidecarFlagRun &run : runs)
        {
            array.push_back(toml::array{ (int64_t)run.first, (int64_t)run.last, (int64_t)run.flag });
        }
        return array;
    }

    sci::Status ReadFlagRuns(const toml::table &table, const std::string &name, const char *key, std::vector<PicSidecarFlagRun> &runs)
    {
        if (table.get(key) == nullptr)
        {
            return sci::Ok();
        }
        SCI_TRY_ASSIGN(std::vector<std::vector<int>> arrays, ReadEntryArrays(table, name, key, 3));
        runs.clear();
        for (const std::vector<int> &numbers : arrays)
        {
            PicSidecarFlagRun run;
            run.first = numbers[0];
            run.last = numbers[1];
            run.flag = numbers[2];
            runs.push_back(run);
        }
        return sci::Ok();
    }

    tl::unexpected<sci::Error> BadKey(const toml::node &node, const std::string &message)
    {
        sci::ErrorLocation where;
        where.line = (int)node.source().begin.line;
        where.column = (int)node.source().begin.column;
        return sci::Fail(sci::ErrorCode::Format, message, where);
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
        if (table.get(key) == nullptr)
        {
            return sci::Ok();
        }
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

    sci::Status ReadFileName(const toml::table &table, const std::string &name, const char *key, std::string &value)
    {
        const toml::node *file = table.get(key);
        if (file == nullptr)
        {
            return sci::Ok();
        }
        std::string text;
        SCI_TRY(ReadText(table, name, key, text));
        if (!AnsiOfUtf8(text, value))
        {
            return BadKey(*file, fmt::format("{0}: \"{1}\" has a character that the ANSI code page of Windows does not have", name, key));
        }
        if (!IsPlainFileName(value))
        {
            return BadKey(*file, fmt::format("{0}: \"{1}\" must be a file name with no folder", name, key));
        }
        return sci::Ok();
    }

    sci::Result<const toml::table *> TableOf(const toml::table &root, const char *key)
    {
        const toml::node *node = root.get(key);
        if (node == nullptr)
        {
            return (const toml::table *)nullptr;
        }
        const toml::table *table = node->as_table();
        if (table == nullptr)
        {
            return BadKey(*node, fmt::format("\"{0}\" is not a table ([{0}])", key));
        }
        return table;
    }

    sci::Result<std::vector<const toml::table *>> TablesOf(const toml::table &root, const char *key)
    {
        std::vector<const toml::table *> tables;
        const toml::node *node = root.get(key);
        if (node == nullptr)
        {
            return tables;
        }
        const toml::array *array = node->as_array();
        if (array == nullptr)
        {
            return BadKey(*node, fmt::format("\"{0}\" is not an array of tables ([[{0}]])", key));
        }
        for (const toml::node &element : *array)
        {
            const toml::table *table = element.as_table();
            if (table == nullptr)
            {
                return BadKey(element, fmt::format("an element of \"{0}\" is not a table ([[{0}]])", key));
            }
            tables.push_back(table);
        }
        return tables;
    }
}
