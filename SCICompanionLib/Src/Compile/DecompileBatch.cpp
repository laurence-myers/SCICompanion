#include "stdafx.h"
#include "DecompileBatch.h"
#include "AppState.h"
#include "AutoDetectVariableNames.h"
#include "CompiledScript.h"
#include "DecompilerCore.h"
#include "DecompilerResults.h"
#include "DecompileScript.h"
#include "DecompilerConfig.h"
#include "GameFolderHelper.h"
#include "OutputCodeHelper.h"
#include "ResourceEntity.h"
#include "SCO.h"
#include "ScriptOMAll.h"
#include "Text.h"
#include "format.h"
#include <fstream>

using namespace sci;
using namespace std;

// One script of the batch: the compiled script, the lookups that decompiling it
// accumulates (the finishing phase needs them), the syntax tree, and the namer
// that carries the script's variable names from round to round.
class DecompileBatch::Item
{
public:
	Item(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, const GameFolderHelper &helper, uint16_t scriptNumber, IDecompilerResults &results, const DecompileOptions &options) :
		_config(config),
		_scriptLookups(scriptLookups),
		_helper(helper),
		_number(scriptNumber),
		_results(results),
		_options(options),
		_compiledScript(0, CompiledScriptFlags::RemoveBadExports),
		_objectFileLookups(helper, scriptLookups.GetSelectorTable())
	{
	}

	uint16_t GetNumber() const { return _number; }
	Script &GetScript() { return *_script; }
	const CompiledScript &GetCompiledScript() const { return _compiledScript; }

	bool Load()
	{
		return _compiledScript.Load(_helper, _helper.Version, _number);
	}

	// Phase 1. The same steps as DecompileScript (ScriptDocument.cpp), except
	// that the lookups and the text resource live on for the later phases.
	void Decompile()
	{
		// Ok if this fails (and is null)
		_textResource = appState->GetResourceMap().CreateResourceFromNumber(ResourceType::Text, _number);
		TextComponent *pText = _textResource ? _textResource->TryGetComponent<TextComponent>() : nullptr;

		FixDuplicateObjectNames(_compiledScript, _config->GetSelectorTable());

		_lookups = make_unique<DecompileLookups>(_config, _helper, _number, &_scriptLookups, &_objectFileLookups, &_compiledScript, pText, &_compiledScript, _results);
		_lookups->DebugControlFlow = _options.DebugControlFlow;
		_lookups->DebugInstructionConsumption = _options.DebugInstructionConsumption;
		_lookups->pszDebugFilter = _options.DebugFunctionMatch.c_str();
		_lookups->DecompileAsm = _options.DecompileAsm;
		_lookups->SubstituteTextTuples = _options.SubstituteTextTuples;

		_script = DecompileToAst(_helper, _compiledScript, *_lookups, appState->GetResourceMap().GetVocab000());
	}

	// Phase 2. mainSCO holds the global names shared by the batch; it must
	// outlive this item. Returns the globals this round named.
	vector<pair<string, string>> NameVariables(CSCOFile *mainSCO)
	{
		if (!_namer)
		{
			// This script's previous .sco supplies local names. For script 0
			// the globals are its variables, and mainSCO covers them.
			if (_number != 0)
			{
				_oldSCO = GetExistingSCOFromScriptNumber(_helper, _number, _scriptLookups.GetSelectorTable());
			}
			_namer = make_unique<VariableNamer>(*_script, _config, mainSCO, _oldSCO.get());
		}
		return _namer->Run();
	}

	// Phase 3, then the files.
	void FinishAndWrite()
	{
		FinishDecompiledScript(_helper, *_script, _compiledScript, *_lookups);

		ConvertToSCISyntaxHelper(*_script, &_scriptLookups);

		// Decompiling always generates an SCO. Any pertinent info from the old SCO should be transfered
		// to the new one based extracting info from the script.
		unique_ptr<CSCOFile> scoFile = SCOFromScriptAndCompiledScript(*_script, _compiledScript);
		SaveSCOFile(_helper, *scoFile);

		// Dump it to the .sc file
		// TODO: If it already exists, we might want to ask for confirmation.
		std::stringstream ss;
		SourceCodeWriter out(ss, _script.get());
		_script->OutputSourceCode(out);
		string sourceFilename = _helper.GetScriptFileName(_number);
		MakeTextFile(ss.str().c_str(), sourceFilename);
		_results.AddResult(DecompilerResultType::Important, fmt::format("Generated {0}", sourceFilename));
	}

private:
	const IDecompilerConfig *_config;
	GlobalCompiledScriptLookups &_scriptLookups;
	const GameFolderHelper &_helper;
	uint16_t _number;
	IDecompilerResults &_results;
	DecompileOptions _options; // Our own copy: _lookups points into DebugFunctionMatch.

	// In dependency order. _lookups points at the members above it, and _namer
	// at _script and _oldSCO; members are destroyed in reverse order.
	CompiledScript _compiledScript;
	ObjectFileScriptLookups _objectFileLookups;
	unique_ptr<ResourceEntity> _textResource;
	unique_ptr<DecompileLookups> _lookups;
	unique_ptr<Script> _script;
	unique_ptr<CSCOFile> _oldSCO;
	unique_ptr<VariableNamer> _namer;
};

DecompileBatch::DecompileBatch(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, const GameFolderHelper &helper, IDecompilerResults &results, const DecompileOptions &options) :
	_config(config), _scriptLookups(scriptLookups), _helper(helper), _results(results), _options(options)
{
}

DecompileBatch::~DecompileBatch() {}

void DecompileBatch::Run(const set<uint16_t> &scriptNumbers)
{
	_globalRenames.clear();
	_written.clear();

	// mainSCO is declared before the items so it outlives them: each item's
	// namer keeps a pointer to it.
	unique_ptr<CSCOFile> mainSCO;
	vector<unique_ptr<Item>> items;

	// Phase 1: decompile each script once. This is the expensive part.
	for (uint16_t scriptNumber : scriptNumbers)
	{
		if (_results.IsAborted())
		{
			break;
		}
		_results.AddResult(DecompilerResultType::Important, fmt::format("Decompiling script {0}", scriptNumber));
		unique_ptr<Item> item = make_unique<Item>(_config, _scriptLookups, _helper, scriptNumber, _results, _options);
		if (!item->Load())
		{
			continue;
		}
		try
		{
			item->Decompile();
		}
		catch (std::exception &e)
		{
			_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed to decompile: {1}", scriptNumber, e.what()));
			continue;
		}
		catch (...)
		{
			_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed to decompile.", scriptNumber));
			continue;
		}
		if (_results.IsAborted())
		{
			// Only partly decompiled. Drop it rather than write half a script.
			break;
		}
		items.push_back(move(item));
	}

	if (items.empty())
	{
		return;
	}

	// Phase 2: name the variables of all the scripts together.
	_results.AddResult(DecompilerResultType::Update, "Naming variables...");

	// The set is ordered, so script 0 is first if it is here at all.
	Item *mainItem = (items.front()->GetNumber() == 0) ? items.front().get() : nullptr;
	mainSCO = GetExistingSCOFromScriptNumber(_helper, 0, _scriptLookups.GetSelectorTable());
	if (!mainSCO && mainItem)
	{
		// No main .sco yet: this batch's decompile of script 0 supplies it, as
		// it does when script 0 is decompiled first on its own.
		mainSCO = SCOFromScriptAndCompiledScript(mainItem->GetScript(), mainItem->GetCompiledScript());
	}

	// A global named in one script lets the scripts before it name more, so
	// go round again until a round names nothing new. The rounds are cheap
	// (tree walks), and the names only ever accumulate, so this ends.
	bool namedSomething;
	do
	{
		namedSomething = false;
		for (auto &item : items)
		{
			vector<pair<string, string>> renames = item->NameVariables(mainSCO.get());
			if (!renames.empty())
			{
				namedSomething = true;
				_globalRenames.insert(_globalRenames.end(), renames.begin(), renames.end());
			}
		}
	} while (namedSomething);

	// Phase 3: finish each script and write its files.
	for (auto &item : items)
	{
		item->FinishAndWrite();
		_written.insert(item->GetNumber());
	}

	if (!_globalRenames.empty())
	{
		_results.SetGlobalVarsUpdated(_globalRenames);
		// Script 0's .sco was just written from its own script, names included.
		// Otherwise main's .sco on disk gets the names now.
		if (!mainItem && mainSCO)
		{
			_results.AddResult(DecompilerResultType::Important, "Updating global variables in script 0");
			SaveSCOFile(_helper, *mainSCO);
		}
	}
}

static bool _IsIdentifierChar(char c)
{
	return isalnum((unsigned char)c) || (c == '_');
}

bool ContainsIdentifier(const string &text, const string &identifier)
{
	if (identifier.empty())
	{
		return false;
	}
	size_t pos = text.find(identifier);
	while (pos != string::npos)
	{
		bool startsWord = (pos == 0) || !_IsIdentifierChar(text[pos - 1]);
		size_t end = pos + identifier.size();
		bool endsWord = (end >= text.size()) || !_IsIdentifierChar(text[end]);
		if (startsWord && endsWord)
		{
			return true;
		}
		pos = text.find(identifier, pos + 1);
	}
	return false;
}

set<uint16_t> FindScriptsReferencingGlobals(const GameFolderHelper &helper, const set<uint16_t> &candidates, const vector<pair<string, string>> &renames)
{
	set<uint16_t> stale;
	if (renames.empty())
	{
		return stale;
	}
	for (uint16_t scriptNumber : candidates)
	{
		string filename = helper.GetScriptFileName(scriptNumber);
		if (filename.empty())
		{
			continue;
		}
		ifstream file(filename.c_str(), ios::in | ios::binary);
		if (!file)
		{
			continue;
		}
		std::stringstream ss;
		ss << file.rdbuf();
		string text = ss.str();
		for (const auto &rename : renames)
		{
			if (ContainsIdentifier(text, rename.first))
			{
				stale.insert(scriptNumber);
				break;
			}
		}
	}
	return stale;
}
