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
