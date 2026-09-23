#pragma once

// Whole-file writes that give a Status (plan step S1, problem P10: the .sco,
// .scd and .sc writes ignored their errors).

#include "Result.h"
#include <cstdint>
#include <string>
#include <vector>

// Creates or replaces the file with the bytes. NotFound when the folder does
// not exist; Io for another failure (for example, a read-only file). The
// message names the file.
sci::Status WriteBytesToFile(const std::string &path, const void *data, size_t size);
sci::Status WriteBytesToFile(const std::string &path, const std::vector<uint8_t> &data);

// The same for text. Each '\n' becomes CR LF, as in a file opened in text
// mode, which the old writers used.
sci::Status WriteTextToFile(const std::string &path, const std::string &text);
