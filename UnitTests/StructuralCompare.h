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
#include <vector>
#include <set>

namespace sci
{
    class Script;
    class FunctionBase;
}

// Structural compare of decompiled scripts against golden sources (sluicebox's
// QfG4 output). Both sides are parsed with the real parser, normalized with
// AST passes to one shape, and compared per function as text:
//   - cond -> nested if; for -> init + while with the step at the end of the
//     body; breakif/contif are already an if with a break/continue after
//     parsing; unsigned compares -> signed;
//   - the decompiler's own passes (nested if -> and, double not, (= a (op a b))
//     -> (op= a b), loop cleanup), so equivalent shapes converge;
//   - every value, variable, define, string and selector literal -> one token;
//     send targets (other than self and super) -> one token; procedure and
//     kernel call names -> one token; a send param's ':' and '?' spelling is
//     dropped. Selector names stay: they come from the game on both sides.
// The function header (name, parameter and temp names) is not compared.
// The game must be set up first (SetUpGameSCI11): the parser needs it.

struct StructuralFunction
{
    std::string key;      // pairs a function across the two sides
    std::string display;  // Class::method or the procedure name, for reports
    std::string text;     // the normalized body
};

struct StructuralCompareResult
{
    int commonFiles = 0;
    int functionsCompared = 0;
    std::vector<std::string> onlyExpected;   // file names
    std::vector<std::string> onlyActual;
    std::vector<std::string> unparsed;       // "file (expected|actual): error"
    std::vector<std::string> differences;    // "file :: function"

    std::string Report() const;
};

// Normalizes every function of a parsed script and returns their bodies.
// skipProcedures: names of procedures to leave out (golden dead code, see
// UnusedProcedureNames).
std::vector<StructuralFunction> NormalizeScriptForCompare(sci::Script &script, const std::set<std::string> *skipProcedures = nullptr);

// The procedures a golden script marks "; UNUSED" on their header line.
std::set<std::string> UnusedProcedureNames(const std::string &text);

// Compares two script texts. Returns the display names of the functions that
// differ (or "<unparsed>" when a side does not parse). outDetail, if given,
// receives the two normalized texts of each difference.
std::vector<std::string> CompareScriptTexts(const std::string &expectedText, const std::string &actualText, std::string *outDetail = nullptr);

// Compares two folders of .sc files paired by name. When outDir is not empty,
// writes <outDir>\_structural.txt (the report) and, per difference,
// <outDir>\<file>.<function>.diff.txt with both normalized texts.
StructuralCompareResult CompareStructural(const std::string &expectedDir, const std::string &actualDir, const std::string &outDir);
