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
	DecompileEngine engine = DecompileEngine::Scope;	// the engine that was asked for
	// The output of the function: "classic" or "scope" (the engine whose
	// source it has), "asm" (the disassembly), "corrupt" (the end of its
	// code is not known), "error" (its decompile threw, so the script
	// failed), or "stale" (an export that points into the code of another
	// function: no function starts there, and it has no source).
	std::string output;
	// The result of each engine: empty when it did not run, "ok", or why it
	// failed. Scope: "[scope:<stage>:<id>]"; with the classic engine, the
	// control-flow stages of the scope engine run in shadow mode, and "ok"
	// means that the parser and the verify stage accept the function.
	// Classic: the stage ("graph" or "consumption"), ": ", and the message
	// of the failure.
	std::string scope;
	std::string classic;
	// With the debug dumps of the control flow: the region tree of the scope
	// parser after the verify stage (scope::Dump), or the error of the stage
	// that failed. Empty otherwise.
	std::string scopeTree;
	// The order of the function in the decompile of its script, from 0. Two
	// export slots of one procedure are two functions with one offset.
	int index = 0;
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
