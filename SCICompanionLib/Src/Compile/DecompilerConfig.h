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

class CResourceMap;
class SelectorTable;

namespace sci
{
	class ClassDefinition;
	class SendParam;
}
class SelectorTable;

class IDecompilerConfig
{
public:
	// The names of the parameters of the method, in order. An empty name
	// keeps the position of a parameter that has no name.
	virtual std::vector<std::string> GetParameterNamesFor(sci::ClassDefinition *classDef, const std::string &methodName) const = 0;
	virtual void ResolveMethodCallParameterTypes(sci::SendParam &sendParam) const = 0;
	virtual void ResolveProcedureCallParameterTypes(sci::ProcedureCall &procCall) const = 0;
	virtual void ResolveSwitchStatementValues(sci::SwitchStatement &switchStatement) const = 0;
	virtual void ResolveBinaryOpValues(sci::BinaryOp &binaryOp) const = 0;
	virtual bool IsBitfieldProperty(const std::string &propertyName) const = 0;
	virtual bool IsTextResourceTupleProcedure(const std::string &procName) const = 0;
	virtual const SelectorTable &GetSelectorTable() const = 0;
	virtual ~IDecompilerConfig() {}

	std::string error;
	// A sci.sh or keys.sh that cannot be read or parsed: the decompiler then
	// has none of its enum names.
	std::vector<std::string> headerWarnings;
};

// Reads Decompiler.ini from the game's src folder, and sci.sh and keys.sh from
// the include folder of the resource map (its data folder). A Decompiler.ini
// that cannot be read or parsed is in the result's error; a sci.sh or keys.sh
// that cannot be read or parsed is in headerWarnings.
std::unique_ptr<IDecompilerConfig> CreateDecompilerConfig(const CResourceMap &resourceMap, const SelectorTable &selectorTable);
// The same, with this Decompiler.ini (RunDecompile reads the one of the
// data folder when the game has none in src).
std::unique_ptr<IDecompilerConfig> CreateDecompilerConfig(const CResourceMap &resourceMap, const SelectorTable &selectorTable, const std::string &decompilerIniPath);

