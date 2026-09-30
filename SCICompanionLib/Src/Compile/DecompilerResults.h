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

#include "DecompileEngine.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

enum class DecompilerResultType
{
	Update, // A minor update
	Important, // Important status
	Warning,
	Error,
	// A dump that a debug option asked for (the control-flow graphs, the
	// instruction chunks). Not a warning: the command line prints it
	// plainly.
	Debug
};

// What the decompiler did with one function (the function report).
struct DecompiledFunction
{
	uint16_t script = 0;
	std::string className;		// empty for a procedure
	std::string name;
	uint16_t offset = 0;		// the offset of its code in the script
	int byteCount = 0;			// 0 when the end of its code is not known
	DecompileEngine engine = DecompileEngine::Classic;	// the engine that was asked for
	// The output of the function: "classic" or "scope" (the engine whose
	// source it has), "asm" (the disassembly), or "corrupt" (the end of its
	// code is not known).
	std::string output;
	// The result of each engine: empty when it did not run, "ok", or why it
	// failed. Scope: "[scope:<stage>:<id>]". Classic: the stage ("graph" or
	// "consumption"), ": ", and the message of the failure.
	std::string scope;
	std::string classic;
};

class IDecompilerResults
{
public:
	virtual void AddResult(DecompilerResultType type, const std::string &message) = 0;
	virtual bool IsAborted() = 0;
	virtual void InformStats(bool functionSuccessful, int byteCount) = 0;
	// Once for each function that the decompiler finished (not after an
	// abort).
	virtual void InformFunction(const DecompiledFunction &function) = 0;
	virtual void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &mainDirtyRenames) = 0;
};
