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

#include "CompiledScript.h"

enum class Opcode : uint8_t;

//void AnalyzeInstruction(uint8_t opcode, const uint16_t *operands);
typedef void(*AnalyzeInstructionPtr)(Opcode opcode, const uint16_t *operands, uint16_t currentPCOffset);

// Disassembles one span of code to `out`. Normally reached via DisassembleObject /
// DisassembleScript; declared here so the end-bound guards (#116) can be tested
// directly.
void DisassembleCode(SCIVersion version, std::ostream &out, ICompiledScriptLookups *pLookups, IObjectFileScriptLookups *pOFLookups, const ICompiledScriptSpecificLookups *pScriptThings, const ILookupPropertyName *pPropertyNames, const BYTE *pBegin, const BYTE *pEnd, uint16_t wBaseOffset, AnalyzeInstructionPtr analyzeInstruction = nullptr);


void DisassembleObject(const CompiledScript &script, 
	const CompiledObject &object,
	std::ostream &out,
	ICompiledScriptLookups *pLookups,
	IObjectFileScriptLookups *pOFLookups,
	const ICompiledScriptSpecificLookups *pScriptThings,
	const std::vector<uint8_t> &scriptResource,
	const std::vector<CodeSection> &codeSections,
	std::set<uint16_t> &codePointers,
	AnalyzeInstructionPtr analyzeInstruction = nullptr);

struct Vocab000;
void DisassembleScript(const CompiledScript &script, std::ostream &out, ICompiledScriptLookups *pLookups, IObjectFileScriptLookups *pOFLookups, const Vocab000 *pWords, const std::string *pMessage = nullptr, AnalyzeInstructionPtr analyzeInstruction = nullptr);
