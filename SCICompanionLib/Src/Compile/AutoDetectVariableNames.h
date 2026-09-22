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
class CSCOFile;
class IDecompilerConfig;
class SelectorTable;

class RenameContext;

// Names the variables of one decompiled script from the way they are used. Hold
// one of these per script for as long as the script's variables may still be
// named: Run() can be called again whenever main's .sco gained global names from
// another script, and it carries on from the names it chose before. Building a
// fresh namer for an already-named script is not safe: it maps the old .sco's
// local names onto the script's variables by position and would rename them
// back to their standard forms.
//
// mainSCO is where the names of globals live, and where the names this namer
// finds for them are pushed. It may be null (no global names are then recorded).
// scriptSCO is this script's previous .sco, whose local names are reused. For
// script 0 pass null for scriptSCO (its variables are the globals), or the same
// object as mainSCO.
class VariableNamer
{
public:
	VariableNamer(sci::Script &script, const IDecompilerConfig *config, CSCOFile *mainSCO, CSCOFile *scriptSCO);
	~VariableNamer();
	VariableNamer(const VariableNamer &) = delete;
	VariableNamer &operator=(const VariableNamer &) = delete;

	// Picks up any global names that reached mainSCO since the last run, then
	// names what it can and applies the names to the script. Returns the globals
	// this run named (standard name, new name), which it also wrote to mainSCO.
	std::vector<std::pair<std::string, std::string>> Run();

private:
	sci::Script &_script;
	const IDecompilerConfig *_config;
	std::unique_ptr<RenameContext> _context;
};

// For the decompiler. One-shot: names the script's variables in place, and
// returns the globals it named in mainDirty (see VariableNamer).
void AutoDetectVariableNames(sci::Script &script, const IDecompilerConfig *config, CSCOFile *mainSCO, CSCOFile *scriptSCO, std::vector<std::pair<std::string, std::string>> &mainDirty);
