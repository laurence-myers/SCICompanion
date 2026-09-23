#pragma once

// Script and header text for the parser, with no editor buffer
// (CrystalEdit) and no MFC type.

#include "Result.h"
#include <string>
#include <vector>

// A position in script text, 0-based. The parser stream uses it in place of
// the MFC CPoint (x is the column, y is the line).
struct TextPos
{
    int line = 0;
    int column = 0;
};

// The lines of a script or header file, without their line breaks.
struct ScriptText
{
    std::vector<std::string> lines; // At least one line.
};

// Splits file contents into lines as the script editor does
// (CCrystalTextBuffer::LoadFromFile), so that line numbers in diagnostics
// match the editor:
// - The first line feed in the first 32768 bytes picks one line-break
//   style: CR LF when a CR comes before it, LF CR when a CR comes after
//   it, else LF alone. With no line feed there, the style is CR LF.
// - Only that style ends a line. A CR alone never ends a line.
// - After a partial match, the character that broke it is not tested
//   again as the start of a break: in CR LF style, "CR CR LF" ends no line.
// - A NUL ends the text of its line (the rest of that line is lost).
// - The text after the last break is the last line, also when it is empty.
ScriptText SplitScriptText(const std::string &contents);

// Reads the file and splits it as SplitScriptText does. NotFound when the
// file does not exist, Io when it cannot be read.
sci::Result<ScriptText> LoadScriptText(const std::string &path);
