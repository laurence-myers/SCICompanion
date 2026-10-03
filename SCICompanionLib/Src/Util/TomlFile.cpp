#include "stdafx.h"
#include "TomlFile.h"
#include <fstream>
#include <sstream>

sci::Result<toml::table> ParseTomlFile(const std::string &path)
{
    return sci::Guard("reading " + path, [&]() -> sci::Result<toml::table>
    {
        // toml::parse_file takes a UTF-8 path; this path is in the ANSI code
        // page, so the file is read here.
        std::ifstream file(path, std::ios_base::in | std::ios_base::binary);
        if (!file.is_open())
        {
            return sci::Fail(sci::ErrorCode::NotFound, path + " could not be opened");
        }
        std::ostringstream contents;
        contents << file.rdbuf();

        try
        {
            return toml::parse(contents.str(), path);
        }
        catch (const toml::parse_error &e)
        {
            sci::ErrorLocation where;
            where.file = path;
            where.line = (int)e.source().begin.line;
            where.column = (int)e.source().begin.column;
            return sci::Fail(sci::ErrorCode::Format, std::string(e.description()), where);
        }
    });
}
