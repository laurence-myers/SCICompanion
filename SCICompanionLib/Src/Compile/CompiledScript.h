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
#include "interfaces.h"
#include "Vocab99x.h"
#include "SCO.h"
#include "DisassembleHelper.h"
#include "CompileCommon.h"

enum OperandType : uint8_t;
struct Vocab000;

namespace sci
{
	enum class ValueType : uint32_t;
	class Script;
	class FunctionBase;
}
class DecompileLookups;
class GameFolderHelper;
class ResourceBlob;
class SelectorTable;

//
// Information gleaned from the actual script resources.
//
class ICompiledScriptLookups
{
public:
	virtual std::string LookupSelectorName(uint16_t wIndex) = 0;
	virtual std::string LookupKernelName(uint16_t wIndex) = 0;
	virtual std::string LookupClassName(uint16_t wIndex) = 0;
	virtual bool LookupSpeciesPropertyList(uint16_t wIndex, std::vector<uint16_t> &props) = 0;
	virtual bool LookupSpeciesPropertyListAndValues(uint16_t wIndex, std::vector<uint16_t> &props, std::vector<CompiledVarValue> &values) = 0;
};

class ICompiledScriptSpecificLookups
{
public:
	enum ObjectType
	{
		ObjectTypeSaid,
		ObjectTypeString,
		ObjectTypeClass,
	};
	virtual bool LookupObjectName(uint16_t wOffset, ObjectType &type, std::string &name) const = 0;
};

class IPrivateSpeciesLookups
{
public:
	virtual std::string LookupClassName(uint16_t wIndex) = 0;
	virtual bool LookupSpeciesPropertyList(uint16_t wIndex, std::vector<uint16_t> &props) = 0;
	virtual bool LookupSpeciesPropertyListAndValues(uint16_t wIndex, std::vector<uint16_t> &props, std::vector<CompiledVarValue> &values) = 0;
};


//
// Information gleaned from the .sco object files that the compiler generates for linking.
//
class IObjectFileScriptLookups
{
public:
	virtual std::string ReverseLookupGlobalVariableName(uint16_t wIndex) = 0;
	virtual std::string ReverseLookupPublicExportName(uint16_t wScript, uint16_t wIndex) = 0;
};

class GlobalCompiledScriptLookups : public ICompiledScriptLookups
{
public:
	GlobalCompiledScriptLookups() {}
	GlobalCompiledScriptLookups(const GlobalCompiledScriptLookups &other) = delete;
	GlobalCompiledScriptLookups &operator=(const GlobalCompiledScriptLookups &other) = delete;

	bool Load(const GameFolderHelper &helper);
	// Load, with the reason for a failure (see CheckVocabTables).
	sci::Status TryLoad(const GameFolderHelper &helper);
	std::string LookupSelectorName(uint16_t wIndex);
	std::string LookupKernelName(uint16_t wIndex);
	std::string LookupClassName(uint16_t wIndex);
	bool LookupSpeciesPropertyList(uint16_t wIndex, std::vector<uint16_t> &props);
	bool LookupSpeciesPropertyListAndValues(uint16_t wIndex, std::vector<uint16_t> &props, std::vector<CompiledVarValue> &values);

	SelectorTable &GetSelectorTable() { return _selectors; }
	const SelectorTable &GetSelectorTable() const { return _selectors; }
	GlobalClassTable &GetGlobalClassTable() { return _classes; }

	// The selectors some object in the game uses as a property, and those some
	// object uses as a method. The decompiler consults them to tell a property
	// read from a method call. They come from the class table, so they are the
	// same for every script: built once, on first use, and shared. (Each
	// script's lookups used to build its own copy, which a whole-game batch
	// held 285 times over.)
	const std::unordered_set<uint16_t> &GetPropertySelectors();
	const std::unordered_set<uint16_t> &GetMethodSelectors();

private:
	void _EnsureSelectorCategories();

	SelectorTable _selectors;
	KernelTable	_kernels;
	GlobalClassTable _classes;
	bool _selectorCategoriesValid = false;
	std::unordered_set<uint16_t> _propertySelectors;
	std::unordered_set<uint16_t> _methodSelectors;
};

class ObjectFileScriptLookups : public IObjectFileScriptLookups
{
public:
	ObjectFileScriptLookups(const GameFolderHelper &helper, const SelectorTable &selectors) : _helper(helper), _selectors(selectors) {}
	std::string ReverseLookupGlobalVariableName(uint16_t wIndex);
	std::string ReverseLookupPublicExportName(uint16_t wScript, uint16_t wIndex);

	// Drops the cached .sco files. A later lookup reads them from disk again.
	void ClearCache() { _mapScriptToObject.clear(); }

private:
	bool _GetSCOFile(uint16_t wScript, CSCOFile &scoFile);
	bool _LoadSCOFile(uint16_t wScript);
	std::unordered_map<uint16_t, CSCOFile> _mapScriptToObject;

	const GameFolderHelper &_helper;
	const SelectorTable &_selectors;
};

// The name that a property with no name gets: past the end of the object, or read in a
// procedure of no class. The text cannot have it.
extern const char UnknownPropertyName[];
extern const char PropertyInNonMethodName[];
bool IsPlaceholderPropertyName(const std::string &name);

class ILookupPropertyName
{
public:
	virtual std::string LookupPropertyName(ICompiledScriptLookups *pLookup, uint16_t wPropertyIndex) const = 0;
};

class CompiledScript;

// CompiledObject includes all properties, including the 4 (SCI0-1) or 9 (SCI1.1) reserved ones.
class CompiledObject : public ILookupPropertyName
{
public:
	CompiledObject() { _fInstance = false; IsPublic = false; }
	bool IsInstance() const { return _fInstance; }
	bool Create_SCI0(const CompiledScript &compiledScript, SCIVersion version, sci::istream &stream, BOOL fClass, uint16_t *pwOffset, int classIndex);
	bool Create_SCI1_1(const CompiledScript &compiledScript, SCIVersion version, sci::istream scriptStream, sci::istream &heapStream, uint16_t *pwOffset, int classIndex, uint16_t *endOfObjectInScript);
	std::string GetName() const { return _strName; }
	void SetName(PCTSTR pszName) { _strName = pszName; }
	uint16_t GetSuperClass() const { return _wSuperClass; }
	uint16_t GetSpecies() const
	{
		// In SCI0, and SCI1, _wSpecies and _wSuperClass are the same for instances.
		// In SCI11, _wSpecies is 0xffff for instances. So I think it's reasonable to
		// assume that we can just always use _wSuperClass if _fInstance is true.
		return _fInstance ? _wSuperClass : _wSpeciesIfClass;
	}
	uint16_t GetSpeciesIfClass() const { return _wSpeciesIfClass; }
	uint16_t GetInfo() const { return _wInfo; }

	// The name of the object in the text; the original name stays the string of its
	// name property (GetOriginalName).
	void AdjustName(const std::string &newCodeName) { if (_originalName.empty()) { _originalName = _strName; } _strName = newCodeName; }
	// The name before AdjustName; empty when the name was not adjusted.
	const std::string &GetOriginalName() const { return _originalName; }
	// The name is made up from the script and the position of the object: it has no
	// name string, or its string is such a name (the text of a decompile, compiled).
	bool HasMadeUpName() const { return _madeUpName; }
	const std::vector<uint16_t> &GetProperties() const { return _propertySelectors; }
	const std::vector<uint16_t> &GetMethods() const { return _functionSelectors; }
	const std::vector<CompiledVarValue> &GetPropertyValues() const{ return _propertyValues; }
	const std::vector<uint16_t> &GetMethodCodePointersTO() const { return _functionOffsetsTO; }

	// ILookupPropertyName
	std::string LookupPropertyName(ICompiledScriptLookups *pLookup, uint16_t wPropertyIndex) const override;

	uint16_t GetPosInResource() const { return _wPosInResource; }

	int GetNumberOfDefaultSelectors(const std::vector<uint16_t> &propSelectorsToExamine, uint16_t nameSelector) const;
	int GetNumberOfDefaultSelectors() const;

	SCIVersion GetVersion() const { return _version; }

	bool IsPublic;

private:
	friend class CompiledScript;
	// The value of the name slot when it points to a string, else 0.
	uint16_t _NameValue(size_t slot) const;
	// The name from the string of the name slot (empty: none): a made-up name
	// when it has no letter.
	void _SetName(const std::string &nameString, uint16_t scriptNumber);

	uint16_t _wSpeciesIfClass = 0;
	int _classIndex = 0;
	// The reader found the name slot by the selector of name.
	bool _nameSlotKnown = false;
	uint16_t _wSuperClass = 0;
	std::string _strName;
	std::string _originalName;
	bool _madeUpName = false;
	uint16_t _wInfo = 0;
	// These start from the 4th position (e.g. leave out species, superclass, --info-- and name)
	std::vector<uint16_t> _propertySelectors;
	std::vector<CompiledVarValue> _propertyValues;
	std::vector<uint16_t> _functionSelectors;	  // selectors for the methods
	std::vector<uint16_t> _functionOffsetsTO;
	bool _fInstance;
	uint16_t _wPosInResource = 0;
	SCIVersion _version;
};

struct ScriptSection
{
	uint16_t offset;
	uint16_t type;
	uint16_t length;
};

// A section of a known damaged script (SCI0 and SCI1) that declares more
// bytes than its resource has. The reader cuts the section at the end of
// the resource, and reads no section after it. The raw bytes of the script
// (GetRawBytes) then end with a 0 word, the end marker of the section list
// that the resource does not have.
struct TruncatedScriptSection
{
	uint16_t offset;			// the offset of the section header
	uint16_t type;
	uint16_t declaredLength;	// the length that the header gives, with the header
	uint16_t length;			// the length in the resource, with the header
};

enum class CompiledScriptFlags
{
	None = 0,
	DontLoadExports = 0x00000001,
	RemoveBadExports = 0x00000002,
};
DEFINE_ENUM_FLAGS(CompiledScriptFlags, int)

extern const uint16_t KQ5CD_BadExport;

//
// This represents all the information in a compiled script resources
//
class CompiledScript : public IPrivateSpeciesLookups, public ICompiledScriptSpecificLookups
{
public:
	CompiledScript(const CompiledScript &src) = delete;
	CompiledScript(uint16_t wScript, CompiledScriptFlags flags = CompiledScriptFlags::None) { _wScript = wScript; _flags = flags; }
	// Call these before Load. The name of an object is the string of its name
	// slot: the slot of the selector "name" in the slots of its class (a class
	// with no superclass can have that slot later, or not have it). classes
	// gives the slots of a class of another script, for an instance. Without
	// the selector, and for an instance whose class is not known, the name is
	// the string of the slot after --info--. A reader whose names go into
	// text, or must agree with the names of another reader, calls it.
	// The overload with the table gives no selector when the table has no
	// "name": a guessed number (NameSelectorOf) would take away the names of
	// the objects of a game whose table does not load.
	void SetNameSelector(uint16_t nameSelector, ICompiledScriptLookups *classes = nullptr);
	void SetNameSelector(const SelectorTable &selectors, ICompiledScriptLookups *classes = nullptr);
	// The slots of the class of a species; false when the class is not known.
	using SpeciesSlots = std::function<bool(uint16_t species, std::vector<uint16_t> &slots)>;
	// For a reader that reads the classes of the game with the instances (the
	// class table): after Load, each instance whose class Load did not know
	// gets the name of its name slot, from the slots of its class.
	void ResolveInstanceNames(const SpeciesSlots &classes);
	static constexpr size_t NoNameSlot = SIZE_MAX;
	// The name slot of an object with these slots (selectors: the slots of a
	// class; empty for an instance). False when it is not known; NoNameSlot
	// when the object has no name slot.
	bool FindNameSlot(bool isInstance, uint16_t species, const std::vector<uint16_t> &selectors, size_t slotCount, size_t &slot) const;
	bool Load(const GameFolderHelper &helper, SCIVersion version, int iScriptNumber);
	bool Load(const GameFolderHelper &helper, SCIVersion version, int iScriptNumber, sci::istream &byteStream, sci::istream *heapStream = nullptr);
	// Loads the most recent script resource (and in SCI1.1 its heap) of the
	// game. NotFound when there is none; Format when its data cannot be read,
	// with the resource in the location. A read past the end of the data is an
	// error here, not a zero.
	sci::Status TryLoad(const GameFolderHelper &helper, SCIVersion version, int iScriptNumber);
	// The same, from the script and heap resources that the caller found (a loop
	// over the game's scripts finds them all in one pass, which is faster).
	sci::Status TryLoad(const GameFolderHelper &helper, SCIVersion version, int iScriptNumber, const ResourceBlob &scriptBlob, const ResourceBlob *heapBlob);
	std::vector<std::unique_ptr<CompiledObject>> &GetObjects() { return _objects; }
	const std::vector<std::unique_ptr<CompiledObject>> &GetObjects() const { return _objects; }
	uint16_t GetScriptNumber() const { return _wScript; }

	static bool DetectIfExportsAreWide(const SCIVersion &version, sci::istream &byteStream);

	// ICompiledScriptSpecificLookups
	bool LookupObjectName(uint16_t wOffset, ObjectType &type, std::string &name) const;

	// IPrivateSpeciesLookups
	std::string LookupClassName(uint16_t wIndex);
	bool LookupSpeciesPropertyList(uint16_t wIndex, std::vector<uint16_t> &props);
	bool LookupSpeciesPropertyListAndValues(uint16_t wIndex, std::vector<uint16_t> &props, std::vector<CompiledVarValue> &values);

	const std::vector<uint8_t> &GetRawBytes() const { return _scriptResource; }
	const uint8_t *GetEndOfRawBytes() const { return &_scriptResource[0] + _scriptResource.size(); }
	bool IsExportAnObject(uint16_t wOffset) const;
	bool IsExportAProcedure(uint16_t wOffset, int *exportIndex = nullptr) const;
	std::vector<uint16_t> GetExports() const;
	// The section that the reader cut at the end of the resource; nullptr
	// when the script is complete.
	const TruncatedScriptSection *GetTruncatedSection() const { return _hasTruncatedSection ? &_truncatedSection : nullptr; }
	CompiledObject *GetObjectForExport(uint16_t exportPointer) const;
	std::set<uint16_t> FindInternalCallsTO() const;

	void PopulateSaidStrings(const Vocab000 *pWords) const;

	bool IsStringPointerSCI1_1(uint16_t) const;
	std::string GetStringOrSaidFromOffset(uint16_t, sci::ValueType &type) const;

	std::vector<std::vector<uint16_t>> GetSaids() const;
	std::unordered_map<uint16_t, std::vector<uint16_t>> GetSynonyms() const;

	// TODO: Make these have public names
	std::vector<CompiledVarValue> _localVars;
	std::vector<std::unique_ptr<CompiledObject>> _objects;
	std::vector<uint16_t> _objectsOffsetTO;
	// When loading, we'll also just provide the raw data for stuff. Size, etc...
	std::vector<ScriptSection> _rawScriptSections;
	std::vector<uint16_t> _exportsTO;
	mutable std::vector<std::string> _saidStrings;
	std::vector<std::vector<uint16_t> > _saids;
	std::vector<std::string> _strings;
	std::vector<uint16_t> _stringsOffset;
	std::vector<uint16_t> _saidsOffset;
	// Mapping of mainword to its synonyms
	std::unordered_map<uint16_t, std::vector<uint16_t>> _synonyms;
	std::vector<CodeSection> _codeSections;
	std::vector<uint16_t> _exportedObjectInstances;
	SCIVersion GetVersion() const { return _version; }

private:
	bool _LoadSCI0_SCI1(int iScriptNumber, sci::istream &byteStream);
	bool _LoadSCI1_1(const GameFolderHelper &helper, int iScriptNumber, sci::istream &byteStream, sci::istream *heapStream);
	void _LoadStringOffsetsSCI1_1(uint16_t offset, sci::istream heapStream);
	// sectionSize: the size of an SCI0 export section; 0 when it is not known.
	bool _ReadExports(sci::istream &stream, uint16_t sectionSize = 0);
	bool _ReadStrings(sci::istream &stream, uint16_t wDataSize);
	bool _ReadSaids(sci::istream &stream, uint16_t wDataSize);
	CompiledObject *_FindObjectWithSpecies(uint16_t wIndex);
	bool _FindNameSlotIn(const std::vector<uint16_t> &slots, size_t slotCount, size_t &slot) const;

	uint16_t _wScript;
	BOOL _fPreloadText;
	std::vector<BYTE> _scriptResource;
	std::vector<uint16_t> _stringPointerOffsetsSCI1_1;
	SCIVersion _version;
	CompiledScriptFlags _flags;
	bool _hasTruncatedSection = false;
	TruncatedScriptSection _truncatedSection = {};
	bool _hasNameSelector = false;
	uint16_t _nameSelector = 0;
	ICompiledScriptLookups *_nameSlotClasses = nullptr;
};

int GetOperandSize(BYTE bOpcode, OperandType operandType, const uint8_t *pNext, const uint8_t *pEnd);
uint16_t CalcOffset(const SCIVersion &version, uint16_t wOperandStart, uint16_t wRelOffset, bool bByte, BYTE bRawOpcode);
// Scans one span of raw bytecode [pBegin, pEnd) for CALL instructions and inserts
// each call target (a text offset) into wOffsets. baseOffsetTO is the text offset
// of pBegin. Each instruction is sized from its actual operand bytes, so a
// variable-length Filename (otDEBUGSTRING) operand is stepped over as data (#124).
void FindInternalCallsInCodeSection(const SCIVersion &version, const BYTE *pBegin, const BYTE *pEnd, uint16_t baseOffsetTO, std::set<uint16_t> &wOffsets);
