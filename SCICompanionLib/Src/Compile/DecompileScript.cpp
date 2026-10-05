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
#include "stdafx.h"
#include "DecompileScript.h"
#include "CompiledScript.h"
#include "DecompilerCore.h"
#include "ScriptOMAll.h"
#include "AutoDetectVariableNames.h"
#include "SCO.h"
#include "GameFolderHelper.h"
#include "DecompilerResults.h"
#include "format.h"
#include "DecompilerConfig.h"
#include "Vocab000.h"
#include "ResourceMap.h"
#include "ResourceEntity.h"
#include "Text.h"
#include "OutputCodeHelper.h"
#include <unordered_map>

using namespace sci;
using namespace std;

const char InvalidLookupError[] = "LOOKUP_ERROR";

void DecompileObject(const CompiledObject &object,
	sci::Script &script,
	DecompileLookups &lookups,
	const std::vector<BYTE> &scriptResource,
	const std::vector<CodeSection> &codeSections,
	const std::set<uint16_t> &codePointersTO)
{
	lookups.EndowWithProperties(&object);

	uint16_t superClassScriptNum;
	if (lookups.GetSpeciesScriptNumber(object.GetSuperClass(), superClassScriptNum))
	{
		lookups.TrackUsingScript(superClassScriptNum);
	}

	unique_ptr<ClassDefinition> pClass = std::make_unique<ClassDefinition>();
	pClass->SetScript(&script);
	pClass->SetInstance(object.IsInstance());
	pClass->SetName(object.GetName());
	pClass->SetSuperClass(lookups.LookupClassName(object.GetSuperClass()));
	pClass->SetPublic(object.IsPublic);
	vector<uint16_t> propertySelectorList;
	vector<CompiledVarValue> speciesPropertyValueList;
	bool fSuccess = lookups.LookupSpeciesPropertyListAndValues(object.GetSpecies(), propertySelectorList, speciesPropertyValueList);
	if (!fSuccess && !object.IsInstance())
	{
		// We're a class - our species is ourself
		propertySelectorList = object.GetProperties();
		speciesPropertyValueList = object.GetPropertyValues();
		fSuccess = true;
	}
	// The slots after --info-- (name first, if the class has a name slot).
	size_t firstSlotAfterInfo = (size_t)object.GetNumberOfDefaultSelectors() - 1;
	// The superclass has a name slot after --info-- (or it is not known).
	bool superHasNameSlot = true;
	if (fSuccess && !object.IsInstance())
	{
		if (pClass->GetSuperClass().empty())
		{
			// The compiler gives a class with no superclass and no properties a
			// name slot. A class with no slot after --info-- gets &layout.
			pClass->SetExplicitLayout(propertySelectorList.size() <= firstSlotAfterInfo);
		}
		else
		{
			// The compiler gives a class the slots of its superclass, then the new
			// properties of the text. A class whose slots do not start with the
			// slots of its superclass gets &layout: its text then has all its slots
			// after --info--, in their order.
			vector<uint16_t> superSelectorList;
			vector<CompiledVarValue> superValueList;
			if (lookups.LookupSpeciesPropertyListAndValues(object.GetSuperClass(), superSelectorList, superValueList))
			{
				bool startsWithSuper = (superSelectorList.size() <= propertySelectorList.size());
				for (size_t i = firstSlotAfterInfo; startsWithSuper && (i < superSelectorList.size()); i++)
				{
					startsWithSuper = (superSelectorList[i] == propertySelectorList[i]);
				}
				pClass->SetExplicitLayout(!startsWithSuper);
				superHasNameSlot = (superSelectorList.size() > firstSlotAfterInfo) && (superSelectorList[firstSlotAfterInfo] == lookups.GetNameSelector());
			}
		}
	}
	size_t numberOfProps = 0;
	size_t firstProperty = 0;
	// The name slot that the property loop writes, if any.
	size_t nameSlotWritten = CompiledScript::NoNameSlot;
	// The name slot of an instance that has the name of the instance: the
	// compiler fills it, so the loop does not write it.
	size_t ownNameSlot = CompiledScript::NoNameSlot;
	if (fSuccess)
	{
		assert(propertySelectorList.size() == speciesPropertyValueList.size());
		size_t size1 = propertySelectorList.size();
		size_t size2 = object.GetPropertyValues().size();
		numberOfProps = min(size1, size2);
		if (size1 != size2)
		{
			// TODO: Output a warning... mismatched prop sizes.
		}

		firstProperty = object.GetNumberOfDefaultSelectors(propertySelectorList, lookups.GetNameSelector());
		if (pClass->HasExplicitLayout())
		{
			firstProperty = firstSlotAfterInfo;
		}
		else if (!object.IsInstance() && object.GetOriginalName().empty() &&
			(firstProperty > 0) && (propertySelectorList[firstProperty - 1] == lookups.GetNameSelector()) &&
			((pClass->GetSuperClass().empty() && (firstProperty < numberOfProps)) || !superHasNameSlot))
		{
			// A class with no superclass that declares other properties, and a
			// class whose superclass has no name slot, have a name slot only when
			// their text declares name, so the name property is written.
			firstProperty--;
		}
		// A name slot that is not right after --info-- (a class with no
		// superclass can have its name slot after other properties).
		for (size_t i = firstProperty; i < numberOfProps; i++)
		{
			if (propertySelectorList[i] == lookups.GetNameSelector())
			{
				const CompiledVarValue &nameValue = object.GetPropertyValues()[i];
				ICompiledScriptSpecificLookups::ObjectType type;
				std::string nameString;
				if (object.IsInstance() && object.GetOriginalName().empty() && nameValue.isObjectOrString &&
					lookups.LookupScriptThing(nameValue.value, type, nameString) &&
					(type == ICompiledScriptSpecificLookups::ObjectTypeString) && (nameString == object.GetName()))
				{
					ownNameSlot = i;
				}
				else if (!object.IsInstance() || (nameValue.value != speciesPropertyValueList[i].value))
				{
					nameSlotWritten = i;
				}
				break;
			}
		}
	}
	if (!object.GetOriginalName().empty() && !pClass->HasExplicitLayout() && (nameSlotWritten == CompiledScript::NoNameSlot))
	{
		// The object has another name in the text (FixDuplicateObjectNames): its name
		// property keeps the original string. With &layout, or when the property loop
		// writes the name slot, the name slot gives it.
		unique_ptr<ClassProperty> nameProperty = make_unique<ClassProperty>();
		nameProperty->SetName("name");
		PropertyValue nameValue;
		nameValue.SetValue(object.GetOriginalName(), ValueType::String);
		nameProperty->SetValue(nameValue);
		pClass->AddProperty(move(nameProperty));
	}
	if (fSuccess)
	{
		for (size_t i = firstProperty; i < numberOfProps; i++)
		{
			if (i == ownNameSlot)
			{
				continue;
			}
			const CompiledVarValue &propValue = object.GetPropertyValues()[i];
			// If this is an instance, look up the species values, and only
			// include those that are different.
			if (!object.IsInstance() || (propValue.value != speciesPropertyValueList[i].value))
			{
				unique_ptr<ClassProperty> prop = make_unique<ClassProperty>();
				prop->SetName(lookups.LookupSelectorName(propertySelectorList[i]));
				PropertyValue value;
				ICompiledScriptSpecificLookups::ObjectType type;
				std::string saidOrString;
				if (!propValue.isObjectOrString || !lookups.LookupScriptThing(propValue.value, type, saidOrString))
				{
					assert(!propValue.isObjectOrString && "We should have resolved some token for this property value");

					// Just give it a number
					uint16_t number = propValue.value;
					value.SetValue(number);
					if (lookups.GetDecompilerConfig()->IsBitfieldProperty(prop->GetName()))
					{
						value._fHex = true;
					}
					else if (number >= 32768)
					{
						// A good bet that it's negative
						value.Negate();
					}
				}
				else
				{
					// REVIEW: we could provide a hit here when we shouldn't... oh well.
					// Use ValueType::Token, since the ' or " is already provided in the string.
					value.SetValue(saidOrString, _ScriptObjectTypeToPropertyValueType(type));
				}
				prop->SetValue(value);
				pClass->AddProperty(move(prop));
			}
		}
	} // else make ERROR

	// Methods
	const vector<uint16_t> &functionSelectors = object.GetMethods();
	const vector<uint16_t> &functionOffsetsTO = object.GetMethodCodePointersTO();
	assert(functionSelectors.size() == functionOffsetsTO.size());
	for (size_t i = 0; i < functionSelectors.size() && !lookups.DecompileResults().IsAborted(); i++)
	{
		// Now the code.
		set<uint16_t>::const_iterator functionIndex = find(codePointersTO.begin(), codePointersTO.end(), functionOffsetsTO[i]);
		if (functionIndex != codePointersTO.end())
		{
			CodeSection section;
			if (FindStartEndCode(functionIndex, codePointersTO, codeSections, section))
			{
				const BYTE *pStartCode = &scriptResource[section.begin];
				const BYTE *pEndCode = &scriptResource[section.end];
				const BYTE *pEndScript = &scriptResource[0] + scriptResource.size();
				std::unique_ptr<MethodDefinition> pMethod = std::make_unique<MethodDefinition>();
				pMethod->SetOwnerClass(pClass.get());
				pMethod->SetScript(&script);
				pMethod->SetName(lookups.LookupSelectorName(functionSelectors[i]));
				DecompileRaw(*pMethod, lookups, pStartCode, pEndCode, pEndScript, functionOffsetsTO[i]);
				pClass->AddMethod(std::move(pMethod));
			}
		}
	}

	script.AddClass(std::move(pClass));
	lookups.EndowWithProperties(nullptr);
}

void DecompileFunction(const CompiledScript &compiledScript, ProcedureDefinition &func, DecompileLookups &lookups, uint16_t wCodeOffsetTO, const set<uint16_t> &sortedCodePointersTO)
{
	lookups.EndowWithProperties(lookups.GetPossiblePropertiesForProc(wCodeOffsetTO));
	set<uint16_t>::const_iterator codeStartIt = sortedCodePointersTO.find(wCodeOffsetTO);
	assert(codeStartIt != sortedCodePointersTO.end());
	bool isValidFunctionPointer = (*codeStartIt < compiledScript.GetRawBytes().size());
	CodeSection section;
	if (isValidFunctionPointer && FindStartEndCode(codeStartIt, sortedCodePointersTO, compiledScript._codeSections, section))
	{
		const BYTE *pBegin = &compiledScript.GetRawBytes()[section.begin];
		const BYTE *pEnd = &compiledScript.GetRawBytes()[section.end];
		const BYTE *pEndScript = compiledScript.GetEndOfRawBytes();
		DecompileRaw(func, lookups, pBegin, pEnd, pEndScript, wCodeOffsetTO);
		if (lookups.WasPropertyRequested() && lookups.GetPossiblePropertiesForProc(wCodeOffsetTO))
		{
			const CompiledObject *object = static_cast<const CompiledObject *>(lookups.GetPossiblePropertiesForProc(wCodeOffsetTO));
			// This procedure is "of" this object
			func.SetClass(object->GetName());
		}
	}
	else
	{
		// Make a function stub
		func.AddSignature(make_unique<FunctionSignature>());
		lookups.DecompileResults().AddResult(DecompilerResultType::Warning, fmt::format("Invalid function offset: {0:04x}", *codeStartIt));
	}
	lookups.EndowWithProperties(nullptr);
}

void InsertHeaders(Script &script)
{
	// For decompiling, we don't need game.sh yet (since we're not creating it is still TBD)
	script.AddInclude("sci.sh");
}

// For each species with no name (its defining script is not in the game),
// emit a classdef forward declaration. It carries the species number so the
// synthesized name compiles back to a class opcode, but writes nothing to the
// game resources.
void InsertClassDefs(Script &script, DecompileLookups &lookups)
{
	for (uint16_t species : lookups.GetUnknownSpecies())
	{
		std::unique_ptr<ClassDefDeclaration> classDef = std::make_unique<ClassDefDeclaration>();
		classDef->SetName(GetUnknownClassName(species));
		classDef->ClassNumber = species;
		// ScriptNumber, SuperNumber and File stay unset: they are unknown and
		// the compiler does not need them for a class reference.
		script.ClassDefs.push_back(std::move(classDef));
	}
}

void DetermineAndInsertUsings(const GameFolderHelper &helper, Script &script, DecompileLookups &lookups)
{
	for (uint16_t usingScript : lookups.GetValidUsings())
	{
		// game.ini, or the session's script-name map.
		script.AddUse(helper.GetScriptTitle(usingScript));
	}
}

// e.g. of the form "proc255_3", or "localproc_0b2a", where localproc_0b2a is actually an exported procedure.
// If true, returns the script number and export index so we can look up the real name.
static bool _AllDigits(const std::string &s)
{
	if (s.empty()) { return false; }
	for (char c : s) { if (c < '0' || c > '9') { return false; } }
	return true;
}
static bool _AllHexDigits(const std::string &s)
{
	if (s.empty()) { return false; }
	for (char c : s)
	{
		bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!isHex) { return false; }
	}
	return true;
}
bool _IsUndeterminedPublicProc(const CompiledScript &compiledScript, const std::string &procName, uint16_t &script, uint16_t &index)
{
	script = 0;
	index = 0;
	if (0 == procName.compare(0, 4, "proc"))
	{
		string rest = procName.substr(4, string::npos);
		// Must be of the form [number]_[number], and short enough to fit our
		// generated names (script/index are uint16_t). A real public proc whose
		// name merely starts with "proc" (e.g. "procFoo") is not one of ours; bail
		// out rather than let stoi throw into the batch's catch(...).
		size_t underscore = rest.find('_');
		if (underscore != string::npos)
		{
			string scriptPart = rest.substr(0, underscore);
			string indexPart = rest.substr(underscore + 1, string::npos);
			if (_AllDigits(scriptPart) && _AllDigits(indexPart) && (scriptPart.size() <= 5) && (indexPart.size() <= 5))
			{
				script = (uint16_t)stoi(scriptPart);
				index = (uint16_t)stoi(indexPart);
				return true;
			}
		}
	}
	else if (0 == procName.compare(0, 10, "localproc_"))
	{
		string rest = procName.substr(10, string::npos);
		if (_AllHexDigits(rest) && (rest.size() <= 4))
		{
			int offset = stoi(rest, nullptr, 16);
			// Find the offset of this proc. If it's also a public export, count it as so.
			int indexNumber;
			if (compiledScript.IsExportAProcedure((uint16_t)offset, &indexNumber) && !compiledScript.IsExportOutsideCode((uint16_t)offset))
			{
				script = compiledScript.GetScriptNumber();
				index = (uint16_t)indexNumber;
				return true;
			}
		}
	}
	return false;
}

class ResolveProcedureCalls : public IExploreNode
{
public:
	ResolveProcedureCalls(const GameFolderHelper &helper, DecompileLookups &lookups, const CompiledScript &compiledScript, unordered_map<int, unique_ptr<CSCOFile>> &scoMap) :
		_scoMap(scoMap), _compiledScript(compiledScript), _helper(helper), _lookups(lookups) {}

	void ExploreNode(SyntaxNode &node, ExploreNodeState state) override
	{
		if (state == ExploreNodeState::Pre)
		{
			ProcedureCall *procCall = SafeSyntaxNode<ProcedureCall>(&node);
			if (procCall)
			{
				uint16_t scriptNumber, index;
				if (_IsUndeterminedPublicProc(_compiledScript, procCall->GetName(), scriptNumber, index))
				{
					CSCOFile *sco = _EnsureSCO(scriptNumber);
					if (sco)
					{
						string newProcName = sco->GetExportName(index);
						// A stale or renamed .sco may not carry this export any more, so the name
						// can be empty. The guard below keeps the generated name in that case; do
						// not assert (a Debug whole-game decompile would abort, #44).
						if (!newProcName.empty())
						{
							procCall->SetName(newProcName);
						}
					}
					else
					{
						// In the case when we don't have an .sco file yet (first compile), if this
						// was actually a local call instruction (but is an exported public proc, as we now know),
						// use the public proc name for it temporarily
						if (procCall->GetName() != _GetPublicProcedureName(scriptNumber, index))
						{
							procCall->SetName(_GetPublicProcedureName(scriptNumber, index));
						}
					}
				}
			}

			// We need to handle this for ASM too
			Asm *asmStatement = SafeSyntaxNode<Asm>(&node);
			if (asmStatement)
			{
				string instruction = asmStatement->GetName();
				if (instruction == "call" || instruction == "callb" || instruction == "calle")
				{
					SyntaxNode *procNameNode = asmStatement->GetStatements()[0].get();
					PropertyValue *value = SafeSyntaxNode<PropertyValue>(procNameNode);
					uint16_t scriptNumber, index;
					assert(value->GetType() == ValueType::Token);
					if (_IsUndeterminedPublicProc(_compiledScript, value->GetStringValue(), scriptNumber, index))
					{
						CSCOFile *sco = _EnsureSCO(scriptNumber);
						if (sco)
						{
							string newProcName = sco->GetExportName(index);
							// A stale or renamed .sco may not carry this export any more, so the name
							// can be empty. The guard below keeps the generated name in that case; do
							// not assert (a Debug whole-game decompile would abort, #44).
							if (!newProcName.empty())
							{
								value->SetValue(newProcName, ValueType::Token);
							}
						}
					}
				}
			}
		}
	}

private:
	CSCOFile *_EnsureSCO(uint16_t script)
	{
		if (_scoMap.find(script) == _scoMap.end())
		{
			_scoMap[script] = move(GetExistingSCOFromScriptNumber(_helper, script, _lookups.GetSelectorTable()));
		}
		return _scoMap.at(script).get();
	}
		
	DecompileLookups &_lookups;
	const GameFolderHelper &_helper;
	unordered_map<int, unique_ptr<CSCOFile>> &_scoMap;
	const CompiledScript &_compiledScript;
};

// This pulls in the required .sco files to find the public procedure names
void ResolvePublicProcedureCalls(DecompileLookups &lookups, const GameFolderHelper &helper, Script &script, const CompiledScript &compiledScript)
{
	unordered_map<int, unique_ptr<CSCOFile>> scoMap;
	scoMap[script.GetScriptNumber()] = move(GetExistingSCOFromScriptNumber(helper, script.GetScriptNumber(), lookups.GetSelectorTable()));

	// First the exports. The decompile names each export from the lookups,
	// which take a .sco only when it loads in full. A .sco that loads only in
	// part leaves the generated names (procN_i); then an exported procedure
	// with the generated name of its own slot gets the name of that slot in
	// this .sco. The slot comes from the export table, not from the name: a
	// name from a .sco can have the form of a generated name for another slot
	// (the SCI0 template's Obj.sco names slot 1 "proc999_2"). So the
	// procedure of each slot is found first, and the renames come after. The
	// export entry gets the same name, so that the public block agrees with
	// the procedure.
	CSCOFile *thisSCO = scoMap.at(script.GetScriptNumber()).get();
	if (thisSCO)
	{
		vector<pair<ProcedureDefinition *, string>> renames;
		for (auto &exportEntry : script.GetExports())
		{
			string generatedName = _GetPublicProcedureName(script.GetScriptNumber(), (uint16_t)exportEntry->Slot);
			if (exportEntry->Name != generatedName)
			{
				continue;
			}
			string newProcName = thisSCO->GetExportName((uint16_t)exportEntry->Slot);
			// A stale .sco may not carry this export; keep the generated name
			// rather than blanking it.
			if (newProcName.empty())
			{
				continue;
			}
			for (auto &proc : script.GetProceduresNC())
			{
				if (proc->IsPublic() && (proc->GetName() == generatedName))
				{
					renames.emplace_back(proc.get(), newProcName);
				}
			}
			exportEntry->Name = newProcName;
		}
		for (const auto &rename : renames)
		{
			rename.first->SetName(rename.second);
		}
	}

	// Now the actual calls, which could be to any script
	ResolveProcedureCalls resolveProcCalls(helper, lookups, compiledScript, scoMap);
	script.Traverse(resolveProcCalls);
}

class ResolveVariableValues : public IExploreNode
{
public:
	ResolveVariableValues(const IDecompilerConfig &config) : _config(config) {}

	void ExploreNode(SyntaxNode &node, ExploreNodeState state) override
	{
		if (state == ExploreNodeState::Pre)
		{
			SwitchStatement *switchStatement = SafeSyntaxNode<SwitchStatement>(&node);
			if (switchStatement)
			{
				_config.ResolveSwitchStatementValues(*switchStatement);
			}
			BinaryOp *binaryOp = SafeSyntaxNode<BinaryOp>(&node);
			if (binaryOp)
			{
				_config.ResolveBinaryOpValues(*binaryOp);
			}
		}
	}

private:
	const IDecompilerConfig &_config;
};

// The start of each function of the script (codePointersTO): the methods,
// the exported procedures and the internal procedures; the internal
// procedures that are not exported (internalProcOffsetsTO); and the exports
// that point into the code of the function before them (staleExportsTO:
// Sierra left such exports, for example in QfG3 script 7; no procedure
// starts there, and Snuffer leaves them out too); and the exports that point
// outside the code of the script (outsideExportsTO: for example ICEMAN script
// 0, whose exports 6 to 29 are f9ff; Snuffer leaves them out too).
static void _FindCodePointers(const CompiledScript &compiledScript, set<uint16_t> &codePointersTO, set<uint16_t> &internalProcOffsetsTO, set<uint16_t> &staleExportsTO, set<uint16_t> &outsideExportsTO)
{
	// Make an index of code pointers by looking at the object methods
	set<uint16_t> methodPointersAll;
	for (auto &object : compiledScript._objects)
	{
		const vector<uint16_t> &methodPointersTO = object->GetMethodCodePointersTO();
		codePointersTO.insert(methodPointersTO.begin(), methodPointersTO.end());
		methodPointersAll.insert(methodPointersTO.begin(), methodPointersTO.end());
	}

	// and the exported procedures
	for (size_t i = 0; i < compiledScript._exportsTO.size(); i++)
	{
		uint16_t wCodeOffset = compiledScript._exportsTO[i];
		// Export offsets could point to objects too - we're only interested in code pointers, so
		// check that it's not an object
		if (compiledScript.IsExportAProcedure(wCodeOffset))
		{
			if (compiledScript.IsExportOutsideCode(wCodeOffset))
			{
				outsideExportsTO.insert(wCodeOffset);
			}
			else
			{
				codePointersTO.insert(wCodeOffset);
			}
		}
	}

	// and finally, the most difficult of all, we'll need to scan though for any call calls...
	// those would be our internal procs
	internalProcOffsetsTO = compiledScript.FindInternalCallsTO();
	// A call goes to the start of a function: an export that a call targets
	// is not stale.
	const set<uint16_t> callTargetsTO = internalProcOffsetsTO;
	// Before adding these though, remove any exports from the internalProcOffsets.
	for (const auto &exporty : compiledScript._exportsTO)
	{
		if (compiledScript.IsExportAProcedure(exporty) && (outsideExportsTO.count(exporty) == 0)) // Exported objects can have the same address as a proc, we need to make sure we don't omit a proc because of that.
		{
			set<uint16_t>::iterator internalsIndex = find(internalProcOffsetsTO.begin(), internalProcOffsetsTO.end(), exporty);
			if (internalsIndex != internalProcOffsetsTO.end())
			{
				// Remove this guy.
				internalProcOffsetsTO.erase(internalsIndex);
			}
		}
	}
	// Now add the internal guys to the full list
	codePointersTO.insert(internalProcOffsetsTO.begin(), internalProcOffsetsTO.end());

	// An export whose address the code of the function before it reaches
	// (its decode goes past the address) is stale.
	const std::vector<BYTE> &bytes = compiledScript.GetRawBytes();
	set<uint16_t> exportPointers;
	for (uint16_t exportPointer : compiledScript._exportsTO)
	{
		if (compiledScript.IsExportAProcedure(exportPointer) && (outsideExportsTO.count(exportPointer) == 0) && (methodPointersAll.count(exportPointer) == 0) && (callTargetsTO.count(exportPointer) == 0))
		{
			exportPointers.insert(exportPointer);
		}
	}
	for (uint16_t exportPointer : exportPointers)
	{
		auto it = codePointersTO.find(exportPointer);
		if ((it == codePointersTO.end()) || (it == codePointersTO.begin()))
		{
			continue;
		}
		uint16_t before = *std::prev(it);
		if ((before < bytes.size()) && (exportPointer < bytes.size()))
		{
			int length = FunctionCodeLength(compiledScript.GetVersion(), &bytes[before], compiledScript.GetEndOfRawBytes(), before);
			if ((length > 0) && ((int)before + length > (int)exportPointer))
			{
				codePointersTO.erase(it);
				staleExportsTO.insert(exportPointer);
			}
		}
	}
	// Now we know the length of each code segment (assuming none overlap)
}

std::set<uint16_t> FindExportsWithNoProcedure(const CompiledScript &compiledScript)
{
	set<uint16_t> codePointersTO;
	set<uint16_t> internalProcOffsetsTO;
	set<uint16_t> staleExportsTO;
	set<uint16_t> outsideExportsTO;
	_FindCodePointers(compiledScript, codePointersTO, internalProcOffsetsTO, staleExportsTO, outsideExportsTO);
	staleExportsTO.insert(outsideExportsTO.begin(), outsideExportsTO.end());
	return staleExportsTO;
}

unique_ptr<Script> DecompileToAst(const GameFolderHelper &helper, const CompiledScript &compiledScript, DecompileLookups &lookups, const Vocab000 *pWords)
{
	unique_ptr<Script> pScript = std::make_unique<Script>();
	ScriptId scriptId;
	scriptId.SetResourceNumber(compiledScript.GetScriptNumber());
	pScript->SetScriptId(scriptId);

	if (const TruncatedScriptSection *truncated = compiledScript.GetTruncatedSection())
	{
		std::string message = fmt::format(
			"The script resource is truncated: the section at {0:04x} declares {1} bytes, but the resource has only {2} of them. The rest of that section and all the sections after it are missing.",
			truncated->offset, truncated->declaredLength, truncated->length);
		lookups.DecompileResults().AddResult(DecompilerResultType::Warning, message);
		pScript->AddHeaderComment("WARNING: " + message);
	}

	compiledScript.PopulateSaidStrings(pWords);

	// Synonyms
	if (compiledScript._synonyms.size() > 0)
	{
		for (const auto &syn : compiledScript._synonyms)
		{
			unique_ptr<Synonym> pSynonym = std::make_unique<Synonym>();
			pSynonym->MainWord = _FindPreferredWord(pWords->Lookup(syn.first));
			for (auto &theSyn : syn.second)
			{
				pSynonym->Synonyms.push_back(_FindPreferredWord(pWords->Lookup(theSyn)));
			}
			pScript->AddSynonym(std::move(pSynonym));
		}
	}

	// Now its time for code.
	set<uint16_t> codePointersTO;
	set<uint16_t> internalProcOffsetsTO;
	set<uint16_t> staleExportsTO;
	set<uint16_t> outsideExportsTO;
	_FindCodePointers(compiledScript, codePointersTO, internalProcOffsetsTO, staleExportsTO, outsideExportsTO);
	lookups.SetOutsideCode(compiledScript.GetRawBytes().data(), compiledScript._codeSections);

	// Spit out code segments:
	// First, the objects (instances, classes)
	for (auto &object : compiledScript._objects)
	{

		DecompileObject(*object, *pScript, lookups, compiledScript.GetRawBytes(), compiledScript._codeSections, codePointersTO);
		if (lookups.DecompileResults().IsAborted())
		{
			break;
		}
	}

	map<int, string> exportSlotToName;

	// An export with no procedure (_FindCodePointers) has a warning and a line
	// in the function report.
	auto leaveOutExport = [&](size_t i, uint16_t exportPointer, const std::string &warning, const char *output)
	{
		lookups.DecompileResults().AddResult(DecompilerResultType::Warning, warning);
		DecompiledFunction report;
		report.script = compiledScript.GetScriptNumber();
		report.name = lookups.ReverseLookupPublicExportName(compiledScript.GetScriptNumber(), (uint16_t)i);
		report.offset = exportPointer;
		report.index = lookups.FunctionCount++;
		report.output = output;
		lookups.DecompileResults().InformFunction(report);
	};

	// Now the exported procedures.
	for (size_t i = 0; i < compiledScript._exportsTO.size() && !lookups.DecompileResults().IsAborted(); i++)
	{
		// _exportsTO, in addition to containing code pointers for public procedures, also
		// contains the Rm/Room class.  Filter these out by ignoring code pointers which point outside
		// the codesegment.
		uint16_t exportPointer = compiledScript._exportsTO[i];
		if (staleExportsTO.count(exportPointer) != 0)
		{
			leaveOutExport(i, exportPointer, fmt::format("Export {0} points into the code of another function ({1:04x}): it is left out.", i, exportPointer), "stale");
		}
		else if (outsideExportsTO.count(exportPointer) != 0)
		{
			leaveOutExport(i, exportPointer, fmt::format("Export {0} points outside the code of the script ({1:04x}): it is left out.", i, exportPointer), "outside");
		}
		else if (compiledScript.IsExportAProcedure(exportPointer))
		{
			std::unique_ptr<ProcedureDefinition> pProc = std::make_unique<ProcedureDefinition>();
			pProc->SetScript(pScript.get());
			pProc->SetName(lookups.ReverseLookupPublicExportName(compiledScript.GetScriptNumber(), (uint16_t)i));
			exportSlotToName[i] = pProc->GetName();
			pProc->SetPublic(true);
			DecompileFunction(compiledScript, *pProc, lookups, exportPointer, codePointersTO);
			pScript->AddProcedure(std::move(pProc));
		}
		else if ((exportPointer == 0) || (exportPointer == KQ5CD_BadExport))
		{
			// Valid.
		}
		else 
		{
			// It should be an object
			CompiledObject *object = compiledScript.GetObjectForExport(exportPointer);
			if (object)
			{
				exportSlotToName[i] = object->GetName();
			}
			assert(object);
		}
	}

	// Now the internal procedures (REVIEW - possibly overlap with exported ones)
	for (uint16_t offset : internalProcOffsetsTO)
	{
		if (lookups.DecompileResults().IsAborted())
		{
			break;
		}
		std::unique_ptr<ProcedureDefinition> pProc = make_unique<ProcedureDefinition>();
		pProc->SetScript(pScript.get());
		pProc->SetName(_GetProcNameFromScriptOffset(offset));
		pProc->SetPublic(false);
		DecompileFunction(compiledScript, *pProc, lookups, offset, codePointersTO);
		pScript->AddProcedure(std::move(pProc));
	}

	if (!lookups.DecompileResults().IsAborted())
	{
		std::vector<CompiledVarValue> localVars = compiledScript._localVars;
		const std::map<uint16_t, bool> &localUsage = lookups.GetLocalUsage();
		if (compiledScript.GetTruncatedSection() && !localUsage.empty() && (localUsage.rbegin()->first >= localVars.size()))
		{
			// The locals section is one of the missing sections: the script
			// declares the locals that its code uses, with the value 0.
			uint16_t highest = localUsage.rbegin()->first;
			localVars.resize((size_t)highest + 1, CompiledVarValue{ 0, false });
			std::string message = fmt::format(
				"The code uses locals that the resource does not declare (up to {0}): the locals section is missing. The script declares them with the value 0. Their real initial values are not known.",
				_GetLocalVariableName(highest, compiledScript.GetScriptNumber()));
			lookups.DecompileResults().AddResult(DecompilerResultType::Warning, message);
			pScript->AddHeaderComment("WARNING: " + message);
		}
		AddLocalVariablesToScript(*pScript, compiledScript, lookups, localVars);

		for (auto &pair : exportSlotToName)
		{
			unique_ptr<ExportEntry> entry = make_unique<ExportEntry>(pair.first, pair.second);
			pScript->GetExports().push_back(move(entry));
		}
	}
	return pScript;
}

void FinishDecompiledScript(const GameFolderHelper &helper, Script &script, const CompiledScript &compiledScript, DecompileLookups &lookups)
{
	ResolvePublicProcedureCalls(lookups, helper, script, compiledScript);

	MassageProcedureCalls(lookups, script);

	if (lookups.GetDecompilerConfig())
	{
		ResolveVariableValues resolveVariableValues(*lookups.GetDecompilerConfig());
		script.Traverse(resolveVariableValues);
	}

	InsertHeaders(script);

	DetermineAndInsertUsings(helper, script, lookups);

	InsertClassDefs(script, lookups);
}

Script *Decompile(const GameFolderHelper &helper, const CompiledScript &compiledScript, DecompileLookups &lookups, const Vocab000 *pWords)
{
	unique_ptr<Script> pScript = DecompileToAst(helper, compiledScript, lookups, pWords);

	if (!lookups.DecompileResults().IsAborted())
	{
		// Load this script's SCO, and main's SCO (assuming this isn't main)
		unique_ptr<CSCOFile> mainSCO;
		if (compiledScript.GetScriptNumber() != 0)
		{
			mainSCO = GetExistingSCOFromScriptNumber(helper, 0, lookups.GetSelectorTable());
		}
		unique_ptr<CSCOFile> oldScriptSCO = GetExistingSCOFromScriptNumber(helper, compiledScript.GetScriptNumber(), lookups.GetSelectorTable());

		vector<pair<string, string>> mainDirtyRenames;
		AutoDetectVariableNames(*pScript, lookups.GetDecompilerConfig(), mainSCO.get(), oldScriptSCO.get(), mainDirtyRenames);

		FinishDecompiledScript(helper, *pScript, compiledScript, lookups);

		// Decompiling always generates an SCO. Any pertinent info from the old SCO should be transfered
		// to the new one based extracting info from the script.
		std::unique_ptr<CSCOFile> scoFile = SCOFromScriptAndCompiledScript(*pScript, compiledScript, NameSelectorOf(lookups.GetSelectorTable(), helper.Version.SeparateHeapResources));
		sci::Status wroteObjectFile = SaveSCOFile(helper, *scoFile);
		if (!wroteObjectFile)
		{
			lookups.DecompileResults().AddResult(DecompilerResultType::Error, wroteObjectFile.error().ToString());
		}

		// We may have added some global info to main's SCO. Save that now.
		if (!mainDirtyRenames.empty())
		{
			lookups.DecompileResults().AddResult(DecompilerResultType::Important, "Updating global variables in script 0");
			lookups.DecompileResults().SetGlobalVarsUpdated(mainDirtyRenames);
			sci::Status wroteMain = SaveSCOFile(helper, *mainSCO);
			if (!wroteMain)
			{
				lookups.DecompileResults().AddResult(DecompilerResultType::Error, wroteMain.error().ToString());
			}
		}
	}
	return pScript.release();
}

void FixDuplicateObjectNames(CompiledScript &compiledScript, GlobalCompiledScriptLookups &lookups)
{
	// Occasionally a script will have objects with duplicate names. Rather than a bug, this indicates that there were two separate objects that had
	// their name property explicitly provided. An example is _MapInSection.sc in QFG2.
	// Such objects get a unique name (name_a, name_b, ...), and the text keeps the original
	// string as an explicit name property (CompiledObject::GetOriginalName).
	// A class of the class table takes the name that the table gives it (two classes of
	// the table can have one name: GlobalClassTable gives each one its own), and keeps
	// it: the text of other scripts refers to the class by that name. Of two classes with
	// one species (King's Quest V script 764 has two SaveIcon classes), the first is the
	// class of the table.
	GlobalClassTable &classTable = lookups.GetGlobalClassTable();
	unordered_set<const CompiledObject*> tableClasses;
	unordered_set<uint16_t> tableSpecies;
	for (auto &object : compiledScript.GetObjects())
	{
		uint16_t scriptNumber;
		if (!object->IsInstance() && classTable.GetSpeciesScriptNumber(object->GetSpecies(), scriptNumber) &&
			(scriptNumber == compiledScript.GetScriptNumber()) && tableSpecies.insert(object->GetSpecies()).second)
		{
			tableClasses.insert(object.get());
			std::string tableName = classTable.Lookup(object->GetSpecies());
			if (!tableName.empty() && (tableName != object->GetName()))
			{
				object->AdjustName(tableName);
			}
		}
	}

	unordered_map<string, int> countOfNames;
	unordered_map<string, char> suffixes;
	for (const auto &object : compiledScript.GetObjects())
	{
		countOfNames[object->GetName()]++;
		suffixes[object->GetName()] = 'a';
	}

	// The names of the properties of the objects of the script (their species). In a
	// method, the compiler reads such a name as the property: an instance with that
	// name (Rm::init of many SCI0 games: (= controls controls)) gets another name too.
	// A public instance keeps its name: other scripts refer to it by its name.
	unordered_set<string> propertyNames;
	for (const auto &object : compiledScript.GetObjects())
	{
		vector<uint16_t> properties;
		if (!lookups.LookupSpeciesPropertyList(object->GetSpecies(), properties) && !object->IsInstance())
		{
			// A class that the class table does not have (a private class): its own list.
			properties = object->GetProperties();
		}
		for (uint16_t selector : properties)
		{
			propertyNames.insert(lookups.LookupSelectorName(selector));
		}
	}

	for (auto &object : compiledScript.GetObjects())
	{
		const std::string name = object->GetName();
		bool shadowed = object->IsInstance() && !object->IsPublic && (propertyNames.count(name) > 0);
		// An instance with the name of a keyword of the syntax does not compile (Pepper
		// script 350 and KQ7 have an instance named string).
		bool keyword = object->IsInstance() && IsSCIKeyword(name);
		// An instance with the name of a class of the table: the text would mean the class
		// (Pepper script 110 has an Actor named twisty, the name of the game class).
		uint16_t species;
		bool className = object->IsInstance() && !object->IsPublic && classTable.LookupSpeciesCompiledName(name, species);
		if ((tableClasses.count(object.get()) == 0) && ((countOfNames[name] > 1) || shadowed || keyword || className))
		{
			std::string newName;
			do
			{
				newName = fmt::format("{0}_{1}", name, suffixes[name]++);
			} while (countOfNames.count(newName) || propertyNames.count(newName) || classTable.LookupSpeciesCompiledName(newName, species));
			object->AdjustName(newName);
		}
	}
}

std::unique_ptr<sci::Script> DecompileScript(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, CResourceMap &resourceMap, uint16_t wScript, CompiledScript &compiledScript, IDecompilerResults &results, bool debugControlFlow, bool debugInstConsumption, PCSTR pszDebugFilter, bool decompileAsm, bool substituteTextTuples, BadBranchPolicy badBranches)
{
	const GameFolderHelper &helper = resourceMap.Helper();
	unique_ptr<sci::Script> pScript;
	ObjectFileScriptLookups objectFileLookups(helper, scriptLookups.GetSelectorTable());
	// Ok if pText fails (and is NULL)
	unique_ptr<ResourceEntity> textResource = resourceMap.CreateResourceFromNumber(ResourceType::Text, wScript);
	TextComponent *pText = nullptr;
	if (textResource)
	{
		pText = textResource->TryGetComponent<TextComponent>();
	}

	FixDuplicateObjectNames(compiledScript, scriptLookups);

	DecompileLookups decompileLookups(config, helper, wScript, &scriptLookups, &objectFileLookups, &compiledScript, pText, &compiledScript, results);
	decompileLookups.DebugControlFlow = debugControlFlow;
	decompileLookups.DebugInstructionConsumption = debugInstConsumption;
	decompileLookups.pszDebugFilter = pszDebugFilter;
	decompileLookups.DecompileAsm = decompileAsm;
	decompileLookups.SubstituteTextTuples = substituteTextTuples;
	decompileLookups.BadBranches = badBranches;
	pScript.reset(Decompile(helper, compiledScript, decompileLookups, resourceMap.GetVocab000()));

	ConvertToSCISyntaxHelper(*pScript, &scriptLookups);

	return pScript;
}

std::vector<FunctionCode> ReadScriptFunctions(const CompiledScript &compiledScript, DecompileLookups &lookups, const Vocab000 *pWords)
{
	compiledScript.PopulateSaidStrings(pWords);
	set<uint16_t> codePointersTO;
	set<uint16_t> internalProcOffsetsTO;
	set<uint16_t> staleExportsTO;
	set<uint16_t> outsideExportsTO;
	_FindCodePointers(compiledScript, codePointersTO, internalProcOffsetsTO, staleExportsTO, outsideExportsTO);
	lookups.SetOutsideCode(compiledScript.GetRawBytes().data(), compiledScript._codeSections);
	const std::vector<BYTE> &bytes = compiledScript.GetRawBytes();
	const BYTE *pEndScript = compiledScript.GetEndOfRawBytes();

	std::vector<FunctionCode> functions;
	auto read = [&](FunctionCode &function)
	{
		set<uint16_t>::const_iterator start = codePointersTO.find(function.offset);
		CodeSection section;
		// As DecompileFunction: a procedure past the end of the script has no code.
		bool validProcedure = (function.offset < bytes.size());
		if ((start != codePointersTO.end()) && (function.method || validProcedure) &&
			FindStartEndCode(start, codePointersTO, compiledScript._codeSections, section))
		{
			function.read = ReadFunctionCode(lookups, &bytes[section.begin], &bytes[section.end], pEndScript, function.offset, function.code, function.returnsValue);
		}
	};
	for (const auto &object : compiledScript._objects)
	{
		const vector<uint16_t> &selectors = object->GetMethods();
		const vector<uint16_t> &offsets = object->GetMethodCodePointersTO();
		for (size_t i = 0; (i < selectors.size()) && (i < offsets.size()); i++)
		{
			functions.emplace_back();
			FunctionCode &function = functions.back();
			function.method = true;
			function.objectName = object->GetName();
			function.objectKey = (object->HasMadeUpName() && !object->IsInstance()) ? fmt::format("class {0}", object->GetSpecies()) : object->GetName();
			function.selector = selectors[i];
			function.offset = offsets[i];
			read(function);
		}
	}
	for (size_t i = 0; i < compiledScript._exportsTO.size(); i++)
	{
		uint16_t offset = compiledScript._exportsTO[i];
		if (compiledScript.IsExportAProcedure(offset) && (staleExportsTO.count(offset) == 0) && (outsideExportsTO.count(offset) == 0))
		{
			functions.emplace_back();
			FunctionCode &function = functions.back();
			function.exportIndex = (int)i;
			function.offset = offset;
			read(function);
		}
	}
	for (uint16_t offset : internalProcOffsetsTO)
	{
		functions.emplace_back();
		FunctionCode &function = functions.back();
		function.offset = offset;
		read(function);
	}
	return functions;
}
