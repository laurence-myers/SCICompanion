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
#include "scii.h"
#include "ScriptOM.h"
#include <cstdint>
#include "CompileCommon.h"

// fwd decl
namespace sci
{
	class FunctionBase;
	class SyntaxNode;
	class RestStatement;
}


//
// Represents whether an instruction consumes or generates stack or accumulator
// In general, values will be 0 or 1, except for cStackConsume which could be
// much larger (e.g. for send or call instruction)
//
struct Consumption
{
	Consumption()
	{
		cAccConsume = 0;
		cStackConsume = 0;
		cAccGenerate = 0;
		cStackGenerate = 0;
	}

	int cAccConsume;
	int cStackConsume;
	int cAccGenerate;
	int cStackGenerate;
};

Consumption _GetInstructionConsumption(scii &inst, DecompileLookups *lookups = nullptr);

enum class BinaryOperator;
enum class UnaryOperator;

BinaryOperator GetBinaryOperatorForInstruction(Opcode b);
UnaryOperator GetUnaryOperatorForInstruction(Opcode b);

// A synthesized name for a class whose defining script is not in the game.
std::string GetUnknownClassName(uint16_t species);

enum class VarScope : std::uint8_t
{
	Global = 0x00,
	Local = 0x01,
	Temp = 0x02,
	Param = 0x03
};

struct FunctionDecompileHints
{
	FunctionDecompileHints() { Reset(); }

	void Reset()
	{
		ReturnsValue = false;
	}

	bool ReturnsValue;
};

class IDecompilerResults;
class IDecompilerConfig;

// The code of a script, for the decode of a function: its bytes, and its code
// sections. A branch out of the function to that code makes the decode read
// the code there too.
struct OutsideCode
{
	const BYTE *pScript;
	const std::vector<CodeSection> *sections;
};

class DecompileLookups : public ICompiledScriptLookups
{
public:
	DecompileLookups(
		const IDecompilerConfig *config,
		const GameFolderHelper &helper,
		uint16_t wScript,
		GlobalCompiledScriptLookups *pLookups,
		IObjectFileScriptLookups *pOFLookups,
		ICompiledScriptSpecificLookups *pScriptThings,
		ILookupNames *pTextResource,
		IPrivateSpeciesLookups *pPrivateSpecies,
		IDecompilerResults &results);

	const SCIVersion &GetVersion();

	// ICompiledScriptLookups
	std::string LookupSelectorName(uint16_t wIndex);
	std::string LookupKernelName(uint16_t wIndex);
	std::string LookupClassName(uint16_t wIndex);
	bool LookupSpeciesPropertyList(uint16_t wIndex, std::vector<uint16_t> &props);
	bool LookupSpeciesPropertyListAndValues(uint16_t wIndex, std::vector<uint16_t> &props, std::vector<CompiledVarValue> &values);
	uint16_t GetNameSelector() const;

	// IObjectFileScriptLookups
	std::string ReverseLookupGlobalVariableName(uint16_t wIndex);
	std::string ReverseLookupPublicExportName(uint16_t wScript, uint16_t wIndex);

	// ILookupPropertyName
	std::string LookupPropertyName(uint16_t wPropertyIndex);

	// Fails if no property context
	bool LookupPropertyName(uint16_t wPropertyIndex, std::string &name);

	bool LookupScriptThing(uint16_t wName, ICompiledScriptSpecificLookups::ObjectType &type, std::string &name) const;

	std::string LookupParameterName(uint16_t wIndex); // 1-based

	bool GetSpeciesScriptNumber(uint16_t species, uint16_t &scriptNumber);

	uint16_t GetScriptNumber() const { return _wScript; }
	std::string LookupTextResource(uint16_t wIndex) const;

	void SetPosition(sci::SyntaxNode *pNode);

	void EndowWithProperties(const ILookupPropertyName *pPropertyNames) { _pPropertyNames = pPropertyNames; _requestedProperty = false; }
	void EndowWithFunction(sci::FunctionBase *pFunc);

	// Tracking variable usage
	void TrackVariableUsage(VarScope varScope, uint16_t wIndex, bool isIndexed);
	const std::map<uint16_t, bool> &GetLocalUsage() { return _localVarUsage; }
	const std::map<uint16_t, bool> &GetTempUsage(const std::string &functionMatchName) { return _tempVarUsage[functionMatchName]; }
	void TrackRestStatement(sci::RestStatement *rest, uint16_t paramIndex);
	void ResolveRestStatements();

	// Track local proc calls so we know if they're part of an object
	void TrackProcedureCall(uint16_t offset);
	const ILookupPropertyName *GetPossiblePropertiesForProc(uint16_t localProcOffset);
	bool WasPropertyRequested() { return _requestedProperty; }

	void TrackUsingScript(uint16_t scriptNumber) { if (_wScript != scriptNumber) _usings.insert(scriptNumber); }
	std::set<uint16_t> GetValidUsings();

	// A species with no name: its defining script is not in the game.
	void TrackUnknownSpecies(uint16_t species) { _unknownSpecies.insert(species); }
	const std::set<uint16_t> &GetUnknownSpecies() const { return _unknownSpecies; }

	FunctionDecompileHints FunctionDecompileHints;

	const sci::ClassDefinition *DecompileLookups::GetClassContext() const;

	bool PreferLValue;

	bool IsPropertySelectorOnly(uint16_t selector) const;

	IDecompilerResults &DecompileResults() { return _results; }

	const IDecompilerConfig *GetDecompilerConfig() { return _config; }

	bool DoesExportExist(uint16_t script, uint16_t theExport) const;

	const GameFolderHelper &Helper;

	bool DebugControlFlow;
	bool DebugInstructionConsumption;
	bool DecompileAsm = false;
	bool SubstituteTextTuples = false;
	PCSTR pszDebugFilter = nullptr;
	// The count of the functions that DecompileRaw began in this script.
	int FunctionCount = 0;

	// The code of the script, for the decode of its functions (a branch out
	// of a function to it makes the decode read that code too). Before it is
	// set, the decode reads only the code of the function.
	void SetOutsideCode(const BYTE *pScript, const std::vector<CodeSection> &sections) { _outside = { pScript, &sections }; }
	const OutsideCode *GetOutsideCode() const { return _outside.pScript ? &_outside : nullptr; }

	const SelectorTable& GetSelectorTable() const;

	void ResetOnFailure();

	// Frees the state the instruction decompile built up and the later phases
	// (naming, procedure-call resolution, usings, class defs) do not read: the
	// variable usage, the rest-statement and local-procedure tracking, and the
	// function hints. For a batch that holds every script's lookups until the
	// end. Not for use between functions of one script.
	void ReleaseDecompileState();

private:
	OutsideCode _outside = { nullptr, nullptr };
	uint16_t _wScript;
	const IDecompilerConfig *_config;
	GlobalCompiledScriptLookups *_pLookups;
	IObjectFileScriptLookups *_pOFLookups;
	ICompiledScriptSpecificLookups *_pScriptThings;
	ILookupNames *_pTextResource;
	const ILookupPropertyName *_pPropertyNames = nullptr;
	IPrivateSpeciesLookups *_pPrivateSpecies;
	sci::FunctionBase *_pFunc = nullptr;
	std::string _functionTrackingName;
	LineCol _fakePosition;
	IDecompilerResults &_results;
	std::unordered_set<uint32_t> _scriptExportExistance;
	std::unordered_set<uint16_t> _scriptExistance;

	// Variable usage
	// Need to use map here, because they have to be in order.
	std::map<uint16_t, bool> _localVarUsage;
	// Then per function
	std::unordered_map<std::string, std::map<uint16_t, bool>> _tempVarUsage;
	// Rest statements --- these need to be RestStaetment/index pairs. index 1 means first, index 2 means second.
	std::vector<std::pair<sci::RestStatement*, uint16_t>> _restStatementTrack;

	std::map<uint16_t, const ILookupPropertyName*> _localProcToPropLookups;
	bool _requestedProperty = false;

	std::set<uint16_t> _usings;
	std::set<uint16_t> _unknownSpecies;
};

void DecompileRaw(sci::FunctionBase &func, DecompileLookups &lookups, const BYTE *pBegin, const BYTE *pEnd, const BYTE *pScriptResourceEnd, uint16_t wBaseOffset);

// The instructions of one function, as DecompileRaw reads them: the decode
// to the end of the script, else to pEstimatedMaxEnd. The placeholder
// (Opcode::INDETERMINATE) comes first, and the branch targets point into
// code. returnsValue gets the guess of the decompiler: a ret reads the
// accumulator. False when no bound gives whole instructions.
bool ReadFunctionCode(DecompileLookups &lookups, const BYTE *pBegin, const BYTE *pEstimatedMaxEnd, const BYTE *pScriptResourceEnd, uint16_t wBaseOffset, std::list<scii> &code, bool &returnsValue);

// The bytes of the code of the function at pBegin, by the first decode of
// DecompileRaw (to the end of the script: it ends at a ret that no branch
// goes past); -1 when that decode fails.
int FunctionCodeLength(const SCIVersion &version, const BYTE *pBegin, const BYTE *pScriptResourceEnd, uint16_t wBaseOffset);

struct VariableRange
{
	uint16_t index;
	uint16_t arraySize;
};
void CalculateVariableRanges(const std::map<uint16_t, bool> &usage, uint16_t variableCount, std::vector<VariableRange> &varRanges);
void AddLocalVariablesToScript(sci::Script &script, const CompiledScript &compiledScript, DecompileLookups &lookups, const std::vector<CompiledVarValue> &localVars);

std::string _GetProcNameFromScriptOffset(uint16_t wOffset);
sci::ValueType _ScriptObjectTypeToPropertyValueType(ICompiledScriptSpecificLookups::ObjectType type);



class PreferLValue
{
public:
	PreferLValue(DecompileLookups &lookups, bool prefer) : _lookups(lookups)
	{
		_preferLValueOld = lookups.PreferLValue;
		lookups.PreferLValue = true;
	}
	~PreferLValue()
	{
		_lookups.PreferLValue = _preferLValueOld;
	}
private:
	bool _preferLValueOld;
	DecompileLookups &_lookups;
};

std::string _GetPublicProcedureName(uint16_t wScript, uint16_t wIndex);
// The name of a call to an export: __procN_M when the export has no procedure
// (DecompileLookups::DoesExportExist), else procN_M.
std::string _GetPossiblyMissingPublicProcedureName(DecompileLookups &lookups, uint16_t script, uint16_t theExport);
void MassageProcedureCalls(DecompileLookups &lookups, sci::Script &script);
std::string _GetVariableNameFromCodePos(const scii &inst, DecompileLookups &lookups, VarScope *pVarType = nullptr, uint16_t *pwIndexOut = nullptr);
bool _IsVOIncremented(Opcode bOpcode);
bool _IsVODecremented(Opcode bOpcode);
bool _IsVOStack(Opcode bOpcode);
bool _IsVOPureStack(Opcode bOpcode);
bool _IsVOIndexed(Opcode bOpcode);
bool _IsVOStoreOperation(Opcode bOpcode);

