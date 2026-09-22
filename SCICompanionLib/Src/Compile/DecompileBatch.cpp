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
#include <iterator>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

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

		// The later phases work on the tree. Let go of what the instruction
		// decompile built up (variable usage, the .sco cache), since the batch
		// holds every script until the end and it all adds up.
		_lookups->ReleaseDecompileState();
		_objectFileLookups.ClearCache();
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
			// The namer copies the names it wants from the old .sco; it only
			// keeps a pointer to mainSCO. Let the old .sco go.
			_oldSCO.reset();
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
	// at _script; members are destroyed in reverse order.
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

// The batch holds every script's syntax tree until the end, so its memory
// use is the sum of the batch where it used to be one script's. Report the
// process working set at each phase, so a whole-game decompile shows what
// the batch costs and where the peak is.
namespace
{
	struct MemoryUsage
	{
		size_t workingSet = 0;
		size_t peakWorkingSet = 0;
		bool valid = false;
	};

	MemoryUsage _GetMemoryUsage()
	{
		MemoryUsage usage;
		PROCESS_MEMORY_COUNTERS counters = {};
		counters.cb = sizeof(counters);
		if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		{
			usage.workingSet = counters.WorkingSetSize;
			usage.peakWorkingSet = counters.PeakWorkingSetSize;
			usage.valid = true;
		}
		return usage;
	}

	double _ToMB(size_t bytes)
	{
		return bytes / (1024.0 * 1024.0);
	}

	void _ReportMemory(IDecompilerResults &results, const char *stage, const MemoryUsage &start)
	{
		MemoryUsage now = _GetMemoryUsage();
		if (now.valid && start.valid)
		{
			double delta = _ToMB(now.workingSet) - _ToMB(start.workingSet);
			results.AddResult(DecompilerResultType::Important, fmt::format("Memory {0}: working set {1:.1f} MB ({2:+.1f} MB since the batch started), process peak {3:.1f} MB",
				stage, _ToMB(now.workingSet), delta, _ToMB(now.peakWorkingSet)));
		}
	}
}

void DecompileBatch::Run(const set<uint16_t> &scriptNumbers)
{
	_globalRenames.clear();
	_written.clear();

	MemoryUsage memoryAtStart = _GetMemoryUsage();

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

	if (_results.IsAborted() && !items.empty())
	{
		// The work done so far is kept: name and write what was decompiled.
		_results.AddResult(DecompilerResultType::Important, fmt::format("Decompile aborted: naming and writing the {0} script(s) already decompiled", items.size()));
	}

	_ReportMemory(_results, fmt::format("after decompiling {0} script(s)", items.size()).c_str(), memoryAtStart);

	if (items.empty())
	{
		return;
	}

	// Phase 2: name the variables of all the scripts together.
	_results.AddResult(DecompilerResultType::Update, "Naming variables...");

	// The set is ordered, so script 0 is first if it is here at all.
	bool mainInBatch = (items.front()->GetNumber() == 0);
	mainSCO = GetExistingSCOFromScriptNumber(_helper, 0, _scriptLookups.GetSelectorTable());
	if (!mainSCO && mainInBatch)
	{
		// No main .sco yet: this batch's decompile of script 0 supplies it, as
		// it does when script 0 is decompiled first on its own.
		mainSCO = SCOFromScriptAndCompiledScript(items.front()->GetScript(), items.front()->GetCompiledScript());
	}

	// A global named in one script lets the scripts before it name more, so
	// go round again until a round names nothing new. The rounds are cheap
	// (tree walks), and the names only ever accumulate, so this ends.
	// A script that throws in either phase is reported and dropped (its files
	// are not written); the rest of the batch goes on.
	bool namedSomething;
	do
	{
		namedSomething = false;
		for (auto &item : items)
		{
			if (!item)
			{
				continue;
			}
			try
			{
				vector<pair<string, string>> renames = item->NameVariables(mainSCO.get());
				if (!renames.empty())
				{
					namedSomething = true;
					_globalRenames.insert(_globalRenames.end(), renames.begin(), renames.end());
				}
			}
			catch (std::exception &e)
			{
				_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed while naming variables: {1}", item->GetNumber(), e.what()));
				item.reset();
			}
			catch (...)
			{
				_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed while naming variables.", item->GetNumber()));
				item.reset();
			}
		}
	} while (namedSomething);

	_ReportMemory(_results, "after naming variables", memoryAtStart);

	// Phase 3: finish each script and write its files. Each script is released
	// once written, so memory falls as the phase goes on.
	for (auto &item : items)
	{
		if (!item)
		{
			continue;
		}
		try
		{
			item->FinishAndWrite();
			_written.insert(item->GetNumber());
		}
		catch (std::exception &e)
		{
			_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed to write: {1}", item->GetNumber(), e.what()));
		}
		catch (...)
		{
			_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed to write.", item->GetNumber()));
		}
		item.reset();
	}
	items.clear();

	_ReportMemory(_results, "after writing", memoryAtStart);

	if (!_globalRenames.empty())
	{
		_results.SetGlobalVarsUpdated(_globalRenames);
		// Script 0's .sco was just written from its own script, names included.
		// Otherwise (script 0 not in the batch, or its write failed) main's .sco
		// on disk gets the names now.
		if (mainSCO && (_written.find(0) == _written.end()))
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
		string text((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
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
