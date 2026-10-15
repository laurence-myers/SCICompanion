#pragma once

// The parts that the .toml files of scic pic export and scic view export
// share (PicSidecar.h, ViewSidecar.h): a run of palette entries as a TOML
// array, and the readers of the keys of a table. A reader of a key keeps the
// value when the table has no such key, and gives Format, with the line and
// the column of the key, for a value of another type or out of its range.

#include "PicImage.h"
#include "PicSidecar.h"
#include "Result.h"
#include "TomlFile.h"
#include "format.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sidecartoml
{
    // The runs as an array of [first, last] arrays.
    toml::array RunsArray(const std::vector<IndexRun> &runs);

    // The flag bytes of the entries that the palette stores, as runs of
    // entries with the same flag.
    std::vector<PicSidecarFlagRun> FlagRunsOf(const PicPaletteDescription &palette);

    // The flag runs as an array of [first, last, flag] arrays.
    toml::array FlagRunsArray(const std::vector<PicSidecarFlagRun> &runs);

    // An array of [first, last, flag] runs.
    sci::Status ReadFlagRuns(const toml::table &table, const std::string &name, const char *key, std::vector<PicSidecarFlagRun> &runs);

    // The error of a key, with the line and the column of the key.
    tl::unexpected<sci::Error> BadKey(const toml::node &node, const std::string &message);

    // The whole number of a key, from low to high. name is the table in a
    // message ("[source]", "[[cel]] 3").
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

    sci::Status ReadText(const toml::table &table, const std::string &name, const char *key, std::string &value);
    sci::Status ReadFlag(const toml::table &table, const std::string &name, const char *key, bool &value);

    // The numbers of an array of arrays of palette entries: each inner array
    // has "count" numbers of 0 to 255, and its first is not more than its
    // second.
    sci::Result<std::vector<std::vector<int>>> ReadEntryArrays(const toml::table &table, const std::string &name, const char *key, size_t count);

    // An array of [first, last] runs of palette entries.
    sci::Status ReadRuns(const toml::table &table, const std::string &name, const char *key, std::vector<IndexRun> &runs);

    // A file name with no folder (no separator, no drive, not "." or ".."),
    // in the ANSI code page as a path has it; the text of a TOML file is
    // UTF-8. Format for a name with a folder or with a character that the
    // ANSI code page does not have. The value stays when there is no key.
    sci::Status ReadFileName(const toml::table &table, const std::string &name, const char *key, std::string &value);

    // The table of a key of the root; null when the root has no such key.
    // Format for a key that is not a table.
    sci::Result<const toml::table *> TableOf(const toml::table &root, const char *key);

    // The tables of an array of tables of the root ([[key]]); none when the
    // root has no such key. Format for a key that is not an array of tables.
    sci::Result<std::vector<const toml::table *>> TablesOf(const toml::table &root, const char *key);

    // Parses the text of a TOML file, and reads it with read. The exception
    // boundary of the TOML parser; a parse error is Format with its line
    // and column.
    template<typename T, typename TRead>
    sci::Result<T> ParseSidecarText(const std::string &text, const char *what, TRead read)
    {
        return sci::Guard(std::string("reading the sidecar of ") + what, [&]() -> sci::Result<T>
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
            return read(root);
        });
    }

    // Reads the TOML file (a path in the ANSI code page) with read. NotFound
    // when the file cannot be opened; Format as ParseSidecarText; each error
    // has the file as its location.
    template<typename T, typename TRead>
    sci::Result<T> ReadSidecarFile(const std::string &path, TRead read)
    {
        // The exception boundary of the walk of the tables.
        sci::Result<T> value = sci::Guard("reading " + path, [&]() -> sci::Result<T>
        {
            SCI_TRY_ASSIGN(toml::table root, ParseTomlFile(path));
            return read(root);
        });
        if (!value && value.error().where.file.empty())
        {
            value.error().where.file = path;
        }
        return value;
    }
}
