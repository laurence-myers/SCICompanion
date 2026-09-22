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

// ---------------------------------------------------------------------------
// The naming skeleton.
//
// The variable namer (AutoDetectVariableNames) finds names in one place only:
// an assignment whose one side is named and whose other side is not. It looks
// at the top node of each side (a value, an lvalue, or a send of one
// parameterless selector), and it renames in place so that a name found in
// one round can suggest another in the next. Everything else in the tree is
// dead weight to it. The skeleton keeps just what it reads: the script
// variables (their names and sizes, for the old .sco's names), each class and
// method (for the config's parameter names and "self" suggestions), each
// procedure, and, in each function, one assignment per assignment in the full
// tree, with each side reduced to its top node.
// ---------------------------------------------------------------------------
namespace
{
	class AssignmentCollector : public IExploreNode
	{
	public:
		void ExploreNode(SyntaxNode &node, ExploreNodeState state) override
		{
			if ((state == ExploreNodeState::Pre) && (node.GetNodeType() == NodeTypeAssignment))
			{
				Found.push_back(static_cast<Assignment*>(&node));
			}
		}
		vector<Assignment*> Found;
	};

	// The reduced form of one side of an assignment: enough to suggest a
	// name, to be renamed, or (any other node) to do neither.
	unique_ptr<SyntaxNode> _SkeletonOperand(const SyntaxNode *node)
	{
		if (node)
		{
			switch (node->GetNodeType())
			{
				case NodeTypeValue:
					return make_unique<PropertyValue>(*static_cast<const PropertyValue*>(node));
				case NodeTypeComplexValue:
					// The indexer plays no part; the value's name is what matters.
					return make_unique<PropertyValue>(*static_cast<const ComplexPropertyValue*>(node));
				case NodeTypeLValue:
					return make_unique<LValue>(static_cast<const LValue*>(node)->GetName());
				case NodeTypeSendCall:
				{
					const SendCall *send = static_cast<const SendCall*>(node);
					unique_ptr<SendCall> copy = make_unique<SendCall>();
					// Keep the object by the same route, so a variable object
					// follows the renames as it does in the full tree.
					if (!send->GetTargetName().empty())
					{
						copy->SetName(send->GetTargetName());
					}
					else if (!send->GetObject().empty())
					{
						copy->SetLValue(make_unique<LValue>(send->GetObject()));
					}
					else
					{
						copy->SetStatement1(make_unique<PropertyValue>((uint16_t)0));
					}
					for (const auto &param : send->GetParams())
					{
						unique_ptr<SendParam> paramCopy = make_unique<SendParam>(param->GetSelectorName(), true);
						if (!param->GetSelectorParams().empty())
						{
							// Only the count matters: a selector with arguments suggests nothing.
							paramCopy->AddStatement(make_unique<PropertyValue>((uint16_t)0));
						}
						copy->AddSendParam(move(paramCopy));
					}
					return copy;
				}
				default:
					break;
			}
		}
		return make_unique<PropertyValue>((uint16_t)0);
	}

	void _AddSkeletonAssignments(FunctionBase &target, FunctionBase &source)
	{
		AssignmentCollector collector;
		source.Traverse(collector);
		for (Assignment *assignment : collector.Found)
		{
			unique_ptr<Assignment> copy = make_unique<Assignment>();
			copy->SetVariable(make_unique<LValue>(assignment->_variable ? assignment->_variable->GetName() : ""));
			copy->SetStatement1(_SkeletonOperand(assignment->GetStatement1()));
			target.AddStatement(move(copy));
		}
	}
}

unique_ptr<Script> BuildNamingSkeleton(Script &script)
{
	unique_ptr<Script> skeleton = make_unique<Script>();
	skeleton->SetScriptId(script.GetScriptId());
	for (const auto &var : script.GetScriptVariables())
	{
		unique_ptr<VariableDecl> copy = make_unique<VariableDecl>(var->GetName());
		copy->SetSize(var->GetSize());
		copy->SetScript(skeleton.get());
		skeleton->AddVariable(move(copy));
	}
	for (auto &proc : script.GetProceduresNC())
	{
		unique_ptr<ProcedureDefinition> copy = make_unique<ProcedureDefinition>();
		copy->SetName(proc->GetName());
		copy->SetScript(skeleton.get());
		_AddSkeletonAssignments(*copy, *proc);
		skeleton->AddProcedure(move(copy));
	}
	for (auto &classDef : script.GetClassesNC())
	{
		unique_ptr<ClassDefinition> copy = make_unique<ClassDefinition>();
		copy->SetName(classDef->GetName());
		copy->SetSuperClass(classDef->GetSuperClass());
		copy->SetInstance(classDef->IsInstance());
		copy->SetScript(skeleton.get());
		for (auto &method : classDef->GetMethodsNC())
		{
			unique_ptr<MethodDefinition> methodCopy = make_unique<MethodDefinition>();
			methodCopy->SetName(method->GetName());
			methodCopy->SetOwnerClass(copy.get());
			methodCopy->SetScript(skeleton.get());
			_AddSkeletonAssignments(*methodCopy, *method);
			copy->AddMethod(move(methodCopy));
		}
		skeleton->AddClass(move(copy));
	}
	return skeleton;
}

// ---------------------------------------------------------------------------
// The batch.
// ---------------------------------------------------------------------------
namespace
{
	// Passes the results through, except that in quiet mode it drops the
	// progress lines and function statistics: the second decompile of a script
	// would otherwise report every function twice. Errors always go through.
	class PassThroughResults : public IDecompilerResults
	{
	public:
		PassThroughResults(IDecompilerResults &inner, bool quiet) : _inner(inner), _quiet(quiet) {}
		void AddResult(DecompilerResultType type, const std::string &message) override
		{
			if (!_quiet || (type == DecompilerResultType::Error))
			{
				_inner.AddResult(type, message);
			}
		}
		bool IsAborted() override { return _inner.IsAborted(); }
		void InformStats(bool functionSuccessful, int byteCount) override
		{
			if (!_quiet)
			{
				_inner.InformStats(functionSuccessful, byteCount);
			}
		}
		void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &renames) override
		{
			_inner.SetGlobalVarsUpdated(renames);
		}
	private:
		IDecompilerResults &_inner;
		bool _quiet;
	};

	// Everything one decompile of a script needs, for as long as its tree is
	// in use. DecompileLookups points at the other members.
	struct DecompileState
	{
		DecompileState(const GameFolderHelper &helper, const SelectorTable &selectorTable) :
			compiledScript(0, CompiledScriptFlags::RemoveBadExports),
			objectFileLookups(helper, selectorTable)
		{
		}
		CompiledScript compiledScript;
		ObjectFileScriptLookups objectFileLookups;
		unique_ptr<ResourceEntity> textResource;
		unique_ptr<DecompileLookups> lookups;
		unique_ptr<Script> script;
	};
}

// One script of the batch. Between the passes it holds only its naming
// skeleton and the namer that carries its names from round to round.
class DecompileBatch::Item
{
public:
	Item(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, const GameFolderHelper &helper, uint16_t scriptNumber, IDecompilerResults &results, const DecompileOptions &options) :
		_config(config),
		_scriptLookups(scriptLookups),
		_helper(helper),
		_number(scriptNumber),
		_results(results),
		_options(options)
	{
	}

	uint16_t GetNumber() const { return _number; }

	// Pass 1. Decompiles the script and keeps its naming skeleton. When this
	// is script 0 and there is no Main.sco yet, the .sco built from the tree
	// becomes mainSCO, as it does when script 0 is decompiled first on its own.
	// Returns false if the script does not load.
	bool DecompileForNaming(unique_ptr<CSCOFile> &mainSCO)
	{
		DecompileState state(_helper, _scriptLookups.GetSelectorTable());
		if (!state.compiledScript.Load(_helper, _helper.Version, _number))
		{
			return false;
		}
		_Decompile(state, _results);
		if (_results.IsAborted())
		{
			// Only partly decompiled: no skeleton.
			return true;
		}
		if ((_number == 0) && !mainSCO)
		{
			mainSCO = SCOFromScriptAndCompiledScript(*state.script, state.compiledScript);
		}
		_skeleton = BuildNamingSkeleton(*state.script);
		return true;
	}

	bool HasSkeleton() const { return !!_skeleton; }

	// The naming rounds. mainSCO holds the global names shared by the batch; it
	// must outlive this item. Returns the globals this round named.
	vector<pair<string, string>> NameVariables(CSCOFile *mainSCO)
	{
		if (!_namer)
		{
			// This script's previous .sco supplies local names. For script 0
			// the globals are its variables, and mainSCO covers them.
			unique_ptr<CSCOFile> oldSCO;
			if (_number != 0)
			{
				oldSCO = GetExistingSCOFromScriptNumber(_helper, _number, _scriptLookups.GetSelectorTable());
			}
			// The namer copies the names it wants from the old .sco; it only
			// keeps a pointer to mainSCO.
			_namer = make_unique<VariableNamer>(*_skeleton, _config, mainSCO, oldSCO.get());
		}
		return _namer->Run();
	}

	// Pass 2. Decompiles the script again, names it against the final global
	// names, finishes it, and writes its .sc and .sco. Returns the globals the
	// naming found beyond those in mainSCO (expected: none).
	vector<pair<string, string>> DecompileAndWrite(CSCOFile *mainSCO)
	{
		_namer.reset();
		_skeleton.reset();

		DecompileState state(_helper, _scriptLookups.GetSelectorTable());
		if (!state.compiledScript.Load(_helper, _helper.Version, _number))
		{
			throw std::exception("the script did not load the second time");
		}
		// The first decompile reported this script's progress and statistics.
		PassThroughResults quiet(_results, true);
		_Decompile(state, quiet);
		if (_results.IsAborted())
		{
			return vector<pair<string, string>>();
		}

		vector<pair<string, string>> renames;
		{
			unique_ptr<CSCOFile> oldSCO;
			if (_number != 0)
			{
				oldSCO = GetExistingSCOFromScriptNumber(_helper, _number, _scriptLookups.GetSelectorTable());
			}
			VariableNamer namer(*state.script, _config, mainSCO, oldSCO.get());
			renames = namer.Run();
		}

		FinishDecompiledScript(_helper, *state.script, state.compiledScript, *state.lookups);

		ConvertToSCISyntaxHelper(*state.script, &_scriptLookups);

		// Decompiling always generates an SCO. Any pertinent info from the old SCO should be transfered
		// to the new one based extracting info from the script.
		unique_ptr<CSCOFile> scoFile = SCOFromScriptAndCompiledScript(*state.script, state.compiledScript);
		SaveSCOFile(_helper, *scoFile);

		// Dump it to the .sc file
		// TODO: If it already exists, we might want to ask for confirmation.
		std::stringstream ss;
		SourceCodeWriter out(ss, state.script.get());
		state.script->OutputSourceCode(out);
		string sourceFilename = _helper.GetScriptFileName(_number);
		MakeTextFile(ss.str().c_str(), sourceFilename);
		_results.AddResult(DecompilerResultType::Important, fmt::format("Generated {0}", sourceFilename));
		return renames;
	}

private:
	// The same steps as DecompileScript (ScriptDocument.cpp).
	void _Decompile(DecompileState &state, IDecompilerResults &results)
	{
		// Ok if this fails (and is null)
		state.textResource = appState->GetResourceMap().CreateResourceFromNumber(ResourceType::Text, _number);
		TextComponent *pText = state.textResource ? state.textResource->TryGetComponent<TextComponent>() : nullptr;

		FixDuplicateObjectNames(state.compiledScript, _config->GetSelectorTable());

		state.lookups = make_unique<DecompileLookups>(_config, _helper, _number, &_scriptLookups, &state.objectFileLookups, &state.compiledScript, pText, &state.compiledScript, results);
		state.lookups->DebugControlFlow = _options.DebugControlFlow;
		state.lookups->DebugInstructionConsumption = _options.DebugInstructionConsumption;
		state.lookups->pszDebugFilter = _options.DebugFunctionMatch.c_str();
		state.lookups->DecompileAsm = _options.DecompileAsm;
		state.lookups->SubstituteTextTuples = _options.SubstituteTextTuples;

		state.script = DecompileToAst(_helper, state.compiledScript, *state.lookups, appState->GetResourceMap().GetVocab000());
	}

	const IDecompilerConfig *_config;
	GlobalCompiledScriptLookups &_scriptLookups;
	const GameFolderHelper &_helper;
	uint16_t _number;
	IDecompilerResults &_results;
	DecompileOptions _options; // Our own copy: the lookups point into DebugFunctionMatch.

	// _namer points at _skeleton; members are destroyed in reverse order.
	unique_ptr<Script> _skeleton;
	unique_ptr<VariableNamer> _namer;
};

DecompileBatch::DecompileBatch(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, const GameFolderHelper &helper, IDecompilerResults &results, const DecompileOptions &options) :
	_config(config), _scriptLookups(scriptLookups), _helper(helper), _results(results), _options(options)
{
}

DecompileBatch::~DecompileBatch() {}

// Report the process working set at each phase, so a whole-game decompile
// shows what the batch costs and where the peak is.
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
	// namer keeps a pointer to it. If script 0 is in the batch and there is no
	// Main.sco yet, its first decompile supplies it.
	unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(_helper, 0, _scriptLookups.GetSelectorTable());
	vector<unique_ptr<Item>> items;

	// Pass 1: decompile each script and keep its naming skeleton.
	for (uint16_t scriptNumber : scriptNumbers)
	{
		if (_results.IsAborted())
		{
			break;
		}
		_results.AddResult(DecompilerResultType::Important, fmt::format("Decompiling script {0}", scriptNumber));
		unique_ptr<Item> item = make_unique<Item>(_config, _scriptLookups, _helper, scriptNumber, _results, _options);
		try
		{
			if (!item->DecompileForNaming(mainSCO))
			{
				continue;
			}
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
		if (item->HasSkeleton())
		{
			items.push_back(move(item));
		}
	}

	_ReportMemory(_results, fmt::format("after decompiling {0} script(s) for naming", items.size()).c_str(), memoryAtStart);

	if (_results.IsAborted())
	{
		// Writing needs each script decompiled again, which an abort forbids.
		_results.AddResult(DecompilerResultType::Important, "Decompile aborted: nothing written");
		return;
	}
	if (items.empty())
	{
		return;
	}

	// The naming rounds, over the skeletons of all the scripts together. A
	// global named in one script lets the scripts before it name more, so go
	// round again until a round names nothing new. The rounds are cheap, and
	// the names only ever accumulate, so this ends. A script that throws here
	// is reported and dropped (its files are not written); the rest go on.
	_results.AddResult(DecompilerResultType::Update, "Naming variables...");
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

	// Pass 2: decompile each script again, name it against the final global
	// names, and write its files. One script's tree is in memory at a time.
	// An abort stops the pass; the scripts already written stay written.
	for (auto &item : items)
	{
		if (!item)
		{
			continue;
		}
		if (_results.IsAborted())
		{
			break;
		}
		_results.AddResult(DecompilerResultType::Update, fmt::format("Writing script {0}...", item->GetNumber()));
		try
		{
			vector<pair<string, string>> renames = item->DecompileAndWrite(mainSCO.get());
			if (!_results.IsAborted())
			{
				_written.insert(item->GetNumber());
				_globalRenames.insert(_globalRenames.end(), renames.begin(), renames.end());
			}
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
		// on disk gets the names now, so the scripts written here agree with it.
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
