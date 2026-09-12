/***************************************************************************
    Copyright (c) 2026 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
***************************************************************************/
#pragma once

#include <string>
#include <memory>

namespace sci
{
    class Script;
}

// Test helpers for the decompiler AST passes. They parse a small Sierra-syntax
// script, run passes on it, and print it back to text. No bytecode and no
// Sierra game data are needed. The game must be set up first (SetUpGameSCI11),
// so the grammars and appState exist.

// Wraps a procedure body in a minimal Sierra-syntax script. The body is the
// text inside "(procedure (name ...) <body>)". The parameters a, b, c and the
// temps t, u are always declared so cases can use them.
std::string WrapProcedure(const std::string &body);

// Parses Sierra-syntax script text into a Script. Asserts on a parse error.
std::unique_ptr<sci::Script> ParseSierraScript(const std::string &text);

// Prints a Script back to Sierra-syntax text.
std::string ScriptToText(const sci::Script &script);

// Collapses runs of whitespace to a single space and trims, so a comparison
// ignores indentation and line breaks. Strips carriage returns.
std::string NormalizeWhitespace(const std::string &text);

// Parses a wrapped procedure body, runs every decompiler AST pass on the one
// procedure, prints it, and returns the normalized text of the procedure.
std::string ApplyAllPasses(const std::string &body);
