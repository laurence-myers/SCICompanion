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

// Whether a write that replaces the file (CREATE_ALWAYS and
// FILE_ATTRIBUTE_NORMAL, as WriteBytesToFile and a patch file) can do it,
// before the write: Ok when the file does not exist; Io for a folder with
// its name, for a hidden or system file (which such a write cannot
// replace), and for a file that does not open for writing with this
// sharing (read-only, or another program holds it). WriteBytesToFile
// shares read and write. For a dry run, and for the check before a
// commit (reviews of 4247f34c and 11106215).
sci::Status CheckFileCanBeReplaced(const std::string &path, unsigned long shareMode);
