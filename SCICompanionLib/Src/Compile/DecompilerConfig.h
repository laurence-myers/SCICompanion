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

namespace cpptoml
{
	class table;
}
namespace sci
{
	class ClassDefinition;
	class SendParam;
}
class SelectorTable;

class IDecompilerConfig
{
public:
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
};

// Reads Decompiler.ini from the game's src folder, and sci.sh and keys.sh from
// the include folder of the resource map (its data folder). A Decompiler.ini
// that cannot be read or parsed is in the result's error. A missing sci.sh or
// keys.sh is not reported: the enum names are then lost.
std::unique_ptr<IDecompilerConfig> CreateDecompilerConfig(const CResourceMap &resourceMap, const SelectorTable &selectorTable);
// The same, with this Decompiler.ini (plan step S4: RunDecompile reads the
// one of the data folder when the game has none in src).
std::unique_ptr<IDecompilerConfig> CreateDecompilerConfig(const CResourceMap &resourceMap, const SelectorTable &selectorTable, const std::string &decompilerIniPath);

