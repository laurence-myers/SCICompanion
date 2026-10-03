#pragma once

// toml++ (vcpkg tomlplusplus) reads the TOML configuration files: Decompiler.ini
// and the phoneme maps of the lip sync folder. The vcpkg port compiles it into
// tomlplusplus.lib, so the header gives only the declarations. Include toml++
// through this header.

#define TOML_HEADER_ONLY 0
#include <toml++/toml.hpp>

// Reads and parses the TOML file at path (a path in the ANSI code page).
// NotFound when the file cannot be opened, Format when it is not valid TOML:
// the error then has the line and the column. The stream reports a read
// error as the end of the file, so the text before the error is parsed.
sci::Result<toml::table> ParseTomlFile(const std::string &path);
