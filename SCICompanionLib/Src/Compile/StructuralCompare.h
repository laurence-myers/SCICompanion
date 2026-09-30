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

#include "Version.h"
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sci
{
    class Script;
    class FunctionBase;
}

// Structural compare of decompiled scripts against other sources (for
// example sluicebox's output). Both sides are parsed with the real parser,
// normalized with AST passes to one shape, and compared per function as
// text:
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

struct StructuralFunction
{
    std::string key;        // pairs a function across the two sides
    std::string display;    // Class::method or the procedure name, for reports
    std::string text;       // the normalized body
    std::string exactText;  // the body after the shape passes, with its names
    std::string rawText;    // the body as parsed, before any pass
    std::string skeleton;   // the control statements of text (StructureSkeleton)
    bool isAsm = false;     // the body is an asm block (ReplaceAsmBlocks)
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

// Collapses runs of whitespace to a single space and trims, so a comparison
// ignores indentation and line breaks. Strips carriage returns.
std::string NormalizeWhitespace(const std::string &text);

// Parses Sierra-syntax script text, with the defines of the version. Null on
// a parse error, with the messages in outError.
std::unique_ptr<sci::Script> ParseScriptText(const std::string &text, SCIVersion version, std::string *outError);

// The name of the procedure that ReplaceAsmBlocks puts in place of an asm
// block.
extern const char *const AsmBlockMarker;

// The start of the key of a method: "class:<class name>#<count of the
// classes of that name before it>::<method>".
extern const char *const ClassKeyPrefix;

// The text with each (asm ...) block replaced by a call of AsmBlockMarker,
// so that a function that a tool left as asm parses, and its body is known
// to be asm. Strings, {} strings and ; comments are skipped.
std::string ReplaceAsmBlocks(const std::string &text);

// The text with the parentheses of each group that holds only one
// parenthesized or indexed expression removed: "((= a b))" gives
// " (= a b) " and "(([p i]) foo:)" gives "( [p i]  foo:)" (spaces keep the
// lines and columns). Snuffer writes such groups; the parser of this
// repository does not take them. Strings, {} strings and ; comments are
// skipped, and so are the groups that are syntax, not one expression: the
// init and the step of a for, and the clauses of a cond or a switch. The
// compare uses this text only for a script that does not parse as it is.
std::string UnwrapGroupedExpressions(const std::string &text);

// The control statements of a normalized body, with their nesting: each
// if, while, repeat, switch (and each case in it), break, continue,
// return, and, or and not, and each else. The values and the other
// statements are left out.
std::string StructureSkeleton(const std::string &normalizedText);

// Normalizes every function of a parsed script and returns their bodies.
// skipProcedures: names of procedures to leave out (golden dead code, see
// UnusedProcedureNames).
std::vector<StructuralFunction> NormalizeScriptForCompare(sci::Script &script, const std::set<std::string> *skipProcedures = nullptr);

// The procedures a golden script marks "; UNUSED" on their header line.
std::set<std::string> UnusedProcedureNames(const std::string &text);

// Compares two script texts. Returns the display names of the functions that
// differ (or "<unparsed>" when a side does not parse). outDetail, if given,
// receives the two normalized texts of each difference.
std::vector<std::string> CompareScriptTexts(const std::string &expectedText, const std::string &actualText, SCIVersion version, std::string *outDetail = nullptr);

// Compares two folders of .sc files paired by name. When outDir is not empty,
// writes <outDir>\_structural.txt (the report) and, per difference,
// <outDir>\<file>.<function>.diff.txt with both normalized texts.
StructuralCompareResult CompareStructural(const std::string &expectedDir, const std::string &actualDir, const std::string &outDir, SCIVersion version);

// How a function of the actual side compares with the function of the
// expected side.
enum class StructureVerdict
{
    Same,           // both source; equal after the shape passes, names included
    Names,          // both source; equal when the names are masked (text)
    Shape,          // both source; the same control statements (skeleton)
    Diff,           // both source; other control statements
    Asm,            // actual asm, expected source
    Source,         // actual source, expected asm
    BothAsm,
    OnlyExpected,   // the actual side has no such function (or script)
    OnlyActual,     // the expected side has no such function (or script)
    Neither,        // neither side has the function (only the baseline has it)
    Unparsed,       // the script of the expected side does not parse: no verdict
};

// SAME, NAMES, SHAPE, DIFF, ASM, SOURCE, BOTH-ASM, ONLY-EXPECTED, ONLY-ACTUAL,
// NEITHER, UNPARSED.
const char *StructureVerdictName(StructureVerdict verdict);

// How the actual side of a function changed from the baseline side (an
// earlier decompile of the same game).
enum class StructureChange
{
    None,           // the same text (or asm on both sides)
    Fixed,          // baseline asm, actual source
    Changed,        // both source, another text
    Regressed,      // baseline source, actual asm
    Added,          // no baseline function
    Removed,        // no actual function
};

// "", FIXED, CHANGED, REGRESSED, ADDED, REMOVED.
const char *StructureChangeName(StructureChange change);

struct FunctionCompareRow
{
    uint16_t script = 0;
    std::string key;
    std::string display;
    StructureVerdict verdict = StructureVerdict::Diff;
    // With a baseline: the verdict of the baseline function, and the change.
    bool hasBaseline = false;
    StructureVerdict baselineVerdict = StructureVerdict::Diff;
    StructureChange change = StructureChange::None;
};

struct FolderCompareResult
{
    std::vector<FunctionCompareRow> rows;   // by script number, then as the functions come
    // A file that could not be read or parsed, or a script number that two
    // files of a folder have: "<file> (expected|actual|baseline): <error>".
    // Such a script of the actual side has no rows; of the expected side,
    // its rows have the verdict Unparsed; of the baseline side, the
    // baseline verdict Unparsed and no change.
    std::vector<std::string> errors;
};

// Compares the .sc files of the actual folder with those of the expected
// folder, and with those of the baseline folder when it is not empty. The
// files are paired by the number of their (script# N) line, not by name. A
// script that only one side has gives OnlyExpected or OnlyActual rows. The
// rows are the functions of the three sides: a function that only the
// baseline has is Neither, and Removed. With onlyScripts, the other
// scripts are not read.
FolderCompareResult CompareScriptFolders(const std::string &expectedDir, const std::string &actualDir, const std::string &baselineDir, SCIVersion version,
    const std::set<uint16_t> *onlyScripts = nullptr);
