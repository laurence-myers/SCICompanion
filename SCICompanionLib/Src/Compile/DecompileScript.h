/***************************************************************************
	Copyright (c) 2020 Philip Fortier

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

#include <list>
#include <string>
#include <vector>
#include "scii.h"

namespace sci
{
	class Script;
}
class CompiledScript;
class DecompileLookups;
class ILookupNames;
class GameFolderHelper;
struct Vocab000;

// A decompile runs in three phases:
//  1. DecompileToAst: the objects, functions, script variables and exports, with
//     variables under their standard names (globalN, localN, tempN, paramN),
//     except that a global already named in main's .sco on disk gets that name.
//     This is the expensive phase.
//  2. Naming (VariableNamer in AutoDetectVariableNames.h): names variables from
//     the way they are used, and pushes the names it finds for globals to main's
//     .sco. What it can name depends on which globals already have names, so a
//     batch of scripts runs this to a fixpoint over all of them (DecompileBatch.h).
//  3. FinishDecompiledScript: procedure-call resolution, value substitution,
//     headers, usings and class defs. Independent of the variable names.
// Decompile() runs all three for one script against the .sco files on disk,
// saving this script's .sco and main's.
std::unique_ptr<sci::Script> DecompileToAst(const GameFolderHelper &helper, const CompiledScript &compiledScript, DecompileLookups &lookups, const Vocab000 *pWords);
void FinishDecompiledScript(const GameFolderHelper &helper, sci::Script &script, const CompiledScript &compiledScript, DecompileLookups &lookups);
sci::Script *Decompile(const GameFolderHelper &helper, const CompiledScript &compiledScript, DecompileLookups &lookups, const Vocab000 *pWords);

class IDecompilerConfig;
class IDecompilerResults;
class GlobalCompiledScriptLookups;
class CResourceMap;
class SelectorTable;

// Decompiles one script with Decompile(), and prepares it for output in SCI
// syntax. The resource map gives the game (its helper), the script's text
// resource and vocab.000.
std::unique_ptr<sci::Script> DecompileScript(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, CResourceMap &resourceMap, uint16_t wScript, CompiledScript &compiledScript, IDecompilerResults &results, bool debugControlFlow = false, bool debugInstConsumption = false, PCSTR pszDebugFilter = nullptr, bool decompileAsm = false, bool substituteTextTuples = false);
// Gives objects that share a name, an instance (not public) with the name of a property of an object of the
// script, and an instance with the name of a keyword, distinct names (name_a, name_b, ...); the text keeps the original as the name property.
void FixDuplicateObjectNames(CompiledScript &compiledScript, GlobalCompiledScriptLookups &lookups);

// The code of one function of a compiled script (ReadScriptFunctions).
struct FunctionCode
{
	FunctionCode() = default;
	FunctionCode(FunctionCode &&) = default;
	FunctionCode &operator=(FunctionCode &&) = default;
	// A copy of the code keeps its branch targets in the code of the source.
	FunctionCode(const FunctionCode &) = delete;
	FunctionCode &operator=(const FunctionCode &) = delete;

	// A method: the name of its object, and its selector.
	bool method = false;
	std::string objectName;
	// The key of the object for the meaning check: its name, or its species
	// for a class with a made-up name (CompiledObject::HasMadeUpName: the
	// position of a class in the script can change when it compiles again).
	std::string objectKey;
	uint16_t selector = 0;
	// A procedure: the index of its export; -1 for an internal procedure
	// (and for a method).
	int exportIndex = -1;
	// The address of the code.
	uint16_t offset = 0;
	// The decode found whole instructions.
	bool read = false;
	// The instructions (ReadFunctionCode), and the guess that a ret reads
	// the accumulator.
	std::list<scii> code;
	bool returnsValue = false;
};

// The functions of the script, as the decompiler finds them: the methods of
// each object, the exported procedures, then the internal procedures (in
// address order). pWords gives the text of the said strings.
std::vector<FunctionCode> ReadScriptFunctions(const CompiledScript &compiledScript, DecompileLookups &lookups, const Vocab000 *pWords);
