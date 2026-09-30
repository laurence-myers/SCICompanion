#include "stdafx.h"
#include "DecompileBatch.h"
#include "ResourceMap.h"
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
#include "FileWrite.h"
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
// What a written script can still be affected by.
//
// After a script is named against the global names known so far, the only
// things a later round can change in it are (a) a global it still refers to
// by its standard name, globalN, gaining a name, and (b) a global gaining a
// name the script already uses for something else, which the namer would
// then have to suffix. Record both sets, so the writing pass can tell whether
// the script has to be decompiled again.
// ---------------------------------------------------------------------------
namespace
{
	class NameCollector : public IExploreNode
	{
	public:
		void ExploreNode(SyntaxNode &node, ExploreNodeState state) override
		{
			if (state != ExploreNodeState::Pre)
			{
				return;
			}
			switch (node.GetNodeType())
			{
				case NodeTypeValue:
				case NodeTypeComplexValue:
				{
					const PropertyValueBase &value = static_cast<const PropertyValueBase&>(node);
					if ((value.GetType() == ValueType::Token) || (value.GetType() == ValueType::Pointer))
					{
						_Add(value.GetStringValue());
					}
				}
				break;
				case NodeTypeLValue:
					_Add(static_cast<const LValue&>(node).GetName());
					break;
				case NodeTypeSendCall:
					_Add(static_cast<const SendCall&>(node).GetObject());
					break;
				case NodeTypeSendParam:
					_Add(static_cast<const SendParam&>(node).GetName());
					break;
				case NodeTypeRest:
					_Add(static_cast<const RestStatement&>(node).GetName());
					break;
				case NodeTypeFunctionParameter:
					_Add(static_cast<const FunctionParameter&>(node).GetName());
					break;
				case NodeTypeVariableDeclaration:
					_Add(static_cast<const VariableDecl&>(node).GetName());
					break;
				default:
					break;
			}
		}

		set<string> UndeterminedGlobals; // "globalN" names still in use
		set<string> Names;               // every identifier in use

	private:
		void _Add(const string &name)
		{
			if (!name.empty())
			{
				Names.insert(name);
				if (_IsUndeterminedGlobalScope(name))
				{
					UndeterminedGlobals.insert(name);
				}
			}
		}
	};

	vector<string> _GlobalNames(const CSCOFile *mainSCO)
	{
		vector<string> names;
		if (mainSCO)
		{
			for (const CSCOLocalVariable &var : mainSCO->GetVariables())
			{
				names.push_back(var.GetName());
			}
		}
		return names;
	}

	// The script of a .sco, as SaveSCOFile(helper, sco) finds it: its name
	// from game.ini or from the session's script-name map.
	ScriptId _ObjectFileScript(const GameFolderHelper &helper, const CSCOFile &sco)
	{
		return helper.GetScriptId(helper.GetScriptTitle(sco.GetScriptNumber()));
	}
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

	// The messages of one script: an error or a warning names the script
	// ("Script 974: Invalid branch target."), unless its text names it.
	class ScriptResults : public IDecompilerResults
	{
	public:
		ScriptResults(IDecompilerResults &inner, uint16_t number) : _inner(inner), _prefix(fmt::format("Script {0}", number)) {}
		void AddResult(DecompilerResultType type, const std::string &message) override
		{
			bool named = (message.rfind(_prefix, 0) == 0) && ((message.size() == _prefix.size()) || !isdigit((unsigned char)message[_prefix.size()]));
			if (((type == DecompilerResultType::Error) || (type == DecompilerResultType::Warning)) && !named)
			{
				_inner.AddResult(type, _prefix + ": " + message);
			}
			else
			{
				_inner.AddResult(type, message);
			}
		}
		bool IsAborted() override { return _inner.IsAborted(); }
		void InformStats(bool functionSuccessful, int byteCount) override { _inner.InformStats(functionSuccessful, byteCount); }
		void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &renames) override { _inner.SetGlobalVarsUpdated(renames); }
	private:
		IDecompilerResults &_inner;
		std::string _prefix;
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

// One script of the batch. Between the passes it holds only what the naming
// rounds need (its skeleton and namer, if it still refers to an unnamed
// global) and what tells whether it must be written again.
class DecompileBatch::Item
{
public:
	Item(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, CResourceMap &resourceMap, uint16_t scriptNumber, IDecompilerResults &results, const DecompileOptions &options,
		IDecompileOutput *output) :
		_config(config),
		_scriptLookups(scriptLookups),
		_resourceMap(resourceMap),
		_helper(resourceMap.Helper()),
		_number(scriptNumber),
		_scriptResults(results, scriptNumber),
		_results(_scriptResults),
		_options(options),
		_output(output)
	{
	}

	uint16_t GetNumber() const { return _number; }
	bool NeedsNamingRounds() const { return !!_skeleton; }
	// The first .sc or .sco file of this script that could not be written.
	const sci::Status &WriteStatus() const { return _writeStatus; }
	// The last decompile of the script reached its write: the writes of
	// its files (with an output, of its source) were made, also when they
	// failed (WriteStatus). An abort that comes after the write does not
	// undo it.
	bool Wrote() const { return _wrote; }
	// The last decompile of the script wrote its .sco with new bytes (with
	// an output that checks the writes: would write it).
	bool ObjectFileChanged() const { return _objectFileChanged; }
	// The globals that the last naming of the script (pass 1, a naming
	// round or pass 2) found. They are in mainSCO already, also when the
	// script failed after its naming or in it.
	const vector<pair<string, string>> &LastRenames() const { return _lastRenames; }

	// Pass 1. Decompiles the script, names it against the global names known
	// so far, and writes it. Keeps its naming skeleton if it still refers to
	// an unnamed global, and records what a later round could change in it.
	// When this is script 0 and there is no Main.sco yet, the .sco built from
	// the tree becomes mainSCO, as it does when script 0 is decompiled first on
	// its own. Fails if the script does not load. LastRenames gives the
	// globals that its naming found. A file that cannot be written is in
	// WriteStatus.
	sci::Status DecompileNameAndWrite(unique_ptr<CSCOFile> &mainSCO)
	{
		_wrote = false;
		_objectFileChanged = false;
		_lastRenames.clear();
		DecompileState state(_helper, _scriptLookups.GetSelectorTable());
		SCI_TRY(state.compiledScript.TryLoad(_helper, _helper.Version, _number));
		_Decompile(state, _results);
		if (_results.IsAborted())
		{
			// Only partly decompiled: not written.
			return sci::Ok();
		}
		if ((_number == 0) && !mainSCO)
		{
			mainSCO = SCOFromScriptAndCompiledScript(*state.script, state.compiledScript);
		}

		// The skeleton comes from the tree before it is named, so that the
		// rounds start from the same point the full tree's naming did.
		unique_ptr<Script> skeleton = BuildNamingSkeleton(*state.script);

		_NameAndWrite(state, mainSCO.get());

		NameCollector collector;
		state.script->Traverse(collector);
		if (!collector.UndeterminedGlobals.empty())
		{
			_skeleton = move(skeleton);
		}
		_undeterminedGlobals = move(collector.UndeterminedGlobals);
		_namesInUse = move(collector.Names);
		_globalNamesWhenWritten = _GlobalNames(mainSCO.get());
		return sci::Ok();
	}

	// The naming rounds, over the skeleton. mainSCO holds the global names
	// shared by the batch; it must outlive this item. LastRenames gives the
	// globals this round named.
	void NameVariables(CSCOFile *mainSCO)
	{
		_lastRenames.clear();
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
		_RunNamer(*_namer);
	}

	// True if a global gained a name since this script was written that the
	// script can see: one it referred to as globalN, or one whose new name
	// the script already uses for something else.
	bool NeedsRewrite(const CSCOFile *mainSCO) const
	{
		vector<string> now = _GlobalNames(mainSCO);
		for (size_t i = 0; i < now.size(); i++)
		{
			const string &before = (i < _globalNamesWhenWritten.size()) ? _globalNamesWhenWritten[i] : string();
			if (now[i] != before)
			{
				if (_undeterminedGlobals.count(fmt::format("global{0}", i)) || _namesInUse.count(now[i]))
				{
					return true;
				}
			}
		}
		return false;
	}

	// Pass 2, for the scripts that need it. Decompiles the script again, names
	// it against the final global names, finishes it, and writes it.
	// LastRenames gives the globals that the naming found beyond those in
	// mainSCO (expected: none).
	void DecompileAndRewrite(CSCOFile *mainSCO)
	{
		_namer.reset();
		_skeleton.reset();
		// The files of this pass replace those of pass 1.
		_writeStatus = sci::Ok();
		_wrote = false;
		_objectFileChanged = false;
		_lastRenames.clear();

		DecompileState state(_helper, _scriptLookups.GetSelectorTable());
		sci::Status loaded = state.compiledScript.TryLoad(_helper, _helper.Version, _number);
		if (!loaded)
		{
			throw sci::DataError("the script did not load the second time: " + loaded.error().ToString(), sci::ErrorCode::Internal);
		}
		// The first decompile reported this script's progress and statistics.
		PassThroughResults quiet(_results, true);
		_Decompile(state, quiet);
		if (_results.IsAborted())
		{
			return;
		}
		_NameAndWrite(state, mainSCO);
	}

private:
	// The same steps as DecompileScript (DecompileScript.cpp).
	void _Decompile(DecompileState &state, IDecompilerResults &results)
	{
		// Ok if this fails (and is null)
		state.textResource = _resourceMap.CreateResourceFromNumber(ResourceType::Text, _number);
		TextComponent *pText = state.textResource ? state.textResource->TryGetComponent<TextComponent>() : nullptr;

		FixDuplicateObjectNames(state.compiledScript, _config->GetSelectorTable());

		state.lookups = make_unique<DecompileLookups>(_config, _helper, _number, &_scriptLookups, &state.objectFileLookups, &state.compiledScript, pText, &state.compiledScript, results);
		state.lookups->DebugControlFlow = _options.DebugControlFlow;
		state.lookups->DebugInstructionConsumption = _options.DebugInstructionConsumption;
		state.lookups->pszDebugFilter = _options.DebugFunctionMatch.c_str();
		state.lookups->DecompileAsm = _options.DecompileAsm;
		state.lookups->SubstituteTextTuples = _options.SubstituteTextTuples;

		state.script = DecompileToAst(_helper, state.compiledScript, *state.lookups, _resourceMap.GetVocab000());
	}

	// Names the tree against mainSCO and this script's previous .sco, then
	// finishes it and writes its .sc and .sco (with an output, it gives the
	// source to the output instead, and checks the writes when the output
	// asks for it). The globals that it named go to _lastRenames at once:
	// the namer wrote them into mainSCO, and a failure after it must not
	// lose them.
	void _NameAndWrite(DecompileState &state, CSCOFile *mainSCO)
	{
		{
			unique_ptr<CSCOFile> oldSCO;
			if (_number != 0)
			{
				oldSCO = GetExistingSCOFromScriptNumber(_helper, _number, _scriptLookups.GetSelectorTable());
			}
			VariableNamer namer(*state.script, _config, mainSCO, oldSCO.get());
			_RunNamer(namer);
		}

		FinishDecompiledScript(_helper, *state.script, state.compiledScript, *state.lookups);

		ConvertToSCISyntaxHelper(*state.script, &_scriptLookups);

		std::stringstream ss;
		SourceCodeWriter out(ss, state.script.get());
		state.script->OutputSourceCode(out);
		// With an output, the source goes to the output, and no file is
		// written.
		bool checkOnly = _output && _output->ChecksTheWrites();
		if (_output && !checkOnly)
		{
			_output->OnSource(_number, ss.str());
			_wrote = true;
			return;
		}

		// Decompiling always generates an SCO. Any pertinent info from the old SCO should be transfered
		// to the new one based extracting info from the script.
		unique_ptr<CSCOFile> scoFile = SCOFromScriptAndCompiledScript(*state.script, state.compiledScript);
		ScriptId objectFileScript = _ObjectFileScript(_helper, *scoFile);
		string sourceFilename = _helper.GetScriptFileName(_number);
		sci::Status objectFile;
		sci::Status sourceFile;
		bool changed = false;
		if (checkOnly)
		{
			// A dry run: the checks that the writes make, and no write.
			sci::Result<bool> wouldChange = SCOFileWouldChange(_helper, *scoFile, objectFileScript);
			if (wouldChange)
			{
				changed = *wouldChange;
			}
			else
			{
				objectFile = sci::Fail(wouldChange.error());
			}
			// WriteTextToFile shares read and write.
			sourceFile = CheckFileCanBeReplaced(sourceFilename, FILE_SHARE_READ | FILE_SHARE_WRITE);
			if (sourceFile)
			{
				// Only the source that a run would write: when the write of
				// the .sc would fail, the old file stays.
				_output->OnSource(_number, ss.str());
			}
		}
		else
		{
			objectFile = SaveSCOFile(_helper, *scoFile, objectFileScript, &changed);
			// Dump it to the .sc file
			// TODO: If it already exists, we might want to ask for confirmation.
			sourceFile = WriteTextToFile(sourceFilename, ss.str());
		}
		// The writes are made. This comes before the messages, which can
		// fail too.
		_wrote = true;
		_objectFileChanged = changed;
		_KeepFirstWriteError(objectFile);
		_KeepFirstWriteError(sourceFile);
		if (!objectFile)
		{
			_results.AddResult(DecompilerResultType::Error, objectFile.error().ToString());
		}
		if (!sourceFile)
		{
			_results.AddResult(DecompilerResultType::Error, sourceFile.error().ToString());
		}
		else if (!checkOnly)
		{
			_results.AddResult(DecompilerResultType::Important, fmt::format("Generated {0}", sourceFilename));
		}
	}

	// Runs the namer. _lastRenames gets the globals that it named, also when
	// it throws: it wrote each of them into mainSCO when it found it.
	void _RunNamer(VariableNamer &namer)
	{
		try
		{
			_lastRenames = namer.Run();
		}
		catch (...)
		{
			_lastRenames = namer.GlobalRenames();
			throw;
		}
	}

	void _KeepFirstWriteError(const sci::Status &written)
	{
		if (_writeStatus)
		{
			_writeStatus = written;
		}
	}

	const IDecompilerConfig *_config;
	GlobalCompiledScriptLookups &_scriptLookups;
	CResourceMap &_resourceMap;
	const GameFolderHelper &_helper;
	uint16_t _number;
	ScriptResults _scriptResults;
	// The messages of the item go through _scriptResults.
	IDecompilerResults &_results;
	DecompileOptions _options; // Our own copy: the lookups point into DebugFunctionMatch.
	IDecompileOutput *_output;
	sci::Status _writeStatus;
	bool _wrote = false;
	bool _objectFileChanged = false;
	vector<pair<string, string>> _lastRenames;

	// _namer points at _skeleton; members are destroyed in reverse order.
	unique_ptr<Script> _skeleton;
	unique_ptr<VariableNamer> _namer;

	// As written in pass 1.
	set<string> _undeterminedGlobals;
	set<string> _namesInUse;
	vector<string> _globalNamesWhenWritten;
};

DecompileBatch::DecompileBatch(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, CResourceMap &resourceMap, IDecompilerResults &results, const DecompileOptions &options,
	IDecompileOutput *output) :
	_config(config), _scriptLookups(scriptLookups), _resourceMap(resourceMap), _helper(resourceMap.Helper()), _results(results), _options(options), _output(output)
{
}

DecompileBatch::~DecompileBatch() {}

void DecompileBatch::SetMainObjectFile(std::unique_ptr<CSCOFile> mainSCO)
{
	_mainSCO = move(mainSCO);
}

std::unique_ptr<CSCOFile> DecompileBatch::TakeMainObjectFile()
{
	return move(_mainSCO);
}

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

sci::Status DecompileBatch::Run(const set<uint16_t> &scriptNumbers)
{
	_globalRenames.clear();
	_written.clear();
	_changedObjectFiles.clear();
	_rewritten.clear();
	_failed.clear();
	_mainObjectFile = sci::Ok();
	_skippedRewrites.clear();
	_mainObjectFileChanged = false;

	// mainSCO outlives the items of the passes: each item's namer keeps a
	// pointer to it.
	unique_ptr<CSCOFile> mainSCO;
	// The exception boundary of the batch: an exception outside the
	// boundary of a script (a message between two scripts, say) ends the
	// passes.
	sci::Status ran = sci::Guard("", [&]() -> sci::Status
	{
		_RunPasses(scriptNumbers, mainSCO);
		return sci::Ok();
	});
	// Also after such an exception: the names found before it are in
	// mainSCO, and a script that the batch wrote can use them.
	sci::Status updated = sci::Guard("", [&]() -> sci::Status
	{
		return _UpdateMainObjectFile(mainSCO.get());
	});
	// With an output, the next group of a dry run starts from it
	// (SetMainObjectFile). A batch that writes reads the file again.
	if (_output)
	{
		_mainSCO = move(mainSCO);
	}
	return ran ? updated : ran;
}

void DecompileBatch::_RunPasses(const set<uint16_t> &scriptNumbers, unique_ptr<CSCOFile> &mainSCO)
{
	MemoryUsage memoryAtStart = _GetMemoryUsage();

	// If script 0 is in the batch and there is no Main.sco yet, its
	// decompile supplies it.
	mainSCO = _mainSCO ? move(_mainSCO) : GetExistingSCOFromScriptNumber(_helper, 0, _scriptLookups.GetSelectorTable());
	vector<unique_ptr<Item>> items;

	// Pass 1: decompile, name and write each script, one tree at a time.
	size_t forNaming = 0;
	for (uint16_t scriptNumber : scriptNumbers)
	{
		if (_results.IsAborted())
		{
			break;
		}
		_results.AddResult(DecompilerResultType::Important, fmt::format("Decompiling script {0}", scriptNumber));
		unique_ptr<Item> item = make_unique<Item>(_config, _scriptLookups, _resourceMap, scriptNumber, _results, _options, _output);
		// The exception boundary of the script. No context: the message and
		// the report name the script.
		sci::Status decompiled = sci::Guard("", [&]() -> sci::Status
		{
			return item->DecompileNameAndWrite(mainSCO);
		});
		// The globals that its naming found are in mainSCO: they count also
		// when the script failed after its naming. So main's .sco gets them,
		// and a script that the run wrote with one of them compiles.
		_globalRenames.insert(_globalRenames.end(), item->LastRenames().begin(), item->LastRenames().end());
		if (!decompiled)
		{
			_failed[scriptNumber] = decompiled.error();
			_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed to decompile: {1}", scriptNumber, decompiled.error().ToString()));
			continue;
		}
		if (item->Wrote())
		{
			// Its files are out: the script counts as written, not as
			// Cancelled, also when the abort came after the write.
			if (!item->WriteStatus())
			{
				_failed[scriptNumber] = item->WriteStatus().error();
			}
			_written.insert(scriptNumber);
			if (item->ObjectFileChanged())
			{
				_changedObjectFiles.insert(scriptNumber);
			}
		}
		if (_results.IsAborted())
		{
			break;
		}
		if (item->NeedsNamingRounds())
		{
			forNaming++;
		}
		items.push_back(move(item));
	}

	_ReportMemory(_results, fmt::format("after decompiling and writing {0} script(s), {1} still referring to an unnamed global", items.size(), forNaming).c_str(), memoryAtStart);

	// The naming rounds, over the skeletons of the scripts that still refer
	// to an unnamed global. A global named in one script lets the scripts
	// before it name more, so go round again until a round names nothing
	// new. The rounds are cheap, and the names only ever accumulate, so this
	// ends. A script that throws here is reported and left as written.
	if (!_results.IsAborted() && (forNaming > 0))
	{
		_results.AddResult(DecompilerResultType::Update, "Naming variables...");
		bool namedSomething;
		do
		{
			namedSomething = false;
			for (auto &item : items)
			{
				if (!item || !item->NeedsNamingRounds())
				{
					continue;
				}
				uint16_t number = item->GetNumber();
				sci::Status named = sci::Guard("", [&]() -> sci::Status
				{
					item->NameVariables(mainSCO.get());
					return sci::Ok();
				});
				// As in pass 1: the names are in mainSCO, also when the
				// namer threw.
				const vector<pair<string, string>> &renames = item->LastRenames();
				_globalRenames.insert(_globalRenames.end(), renames.begin(), renames.end());
				namedSomething = namedSomething || !renames.empty();
				if (!named)
				{
					// The script stays as pass 1 wrote it.
					_failed[number] = named.error();
					_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed while naming variables: {1}", number, named.error().ToString()));
					item.reset();
				}
			}
		} while (namedSomething);
		_ReportMemory(_results, "after naming variables", memoryAtStart);
	}

	// Pass 2: the scripts a later round changed something in are decompiled
	// and written again, one at a time. An abort stops the pass; what is on
	// disk is what pass 1 wrote, or pass 2 where it got that far. The scripts
	// that it did not write again are in _skippedRewrites.
	for (auto &item : items)
	{
		if (!item)
		{
			continue;
		}
		if (_results.IsAborted())
		{
			// Its files keep the old global names: GetSkippedRewrites lists
			// the script, for the report.
			if (item->NeedsRewrite(mainSCO.get()))
			{
				_skippedRewrites.insert(item->GetNumber());
			}
			item.reset();
			continue;
		}
		if (!item->NeedsRewrite(mainSCO.get()))
		{
			item.reset();
			continue;
		}
		uint16_t number = item->GetNumber();
		_results.AddResult(DecompilerResultType::Important, fmt::format("Decompiling script {0} again with the new global names", number));
		sci::Status rewritten = sci::Guard("", [&]() -> sci::Status
		{
			item->DecompileAndRewrite(mainSCO.get());
			return sci::Ok();
		});
		// As in pass 1: the names are in mainSCO.
		_globalRenames.insert(_globalRenames.end(), item->LastRenames().begin(), item->LastRenames().end());
		if (item->Wrote() && item->ObjectFileChanged())
		{
			_changedObjectFiles.insert(number);
		}
		if (!rewritten)
		{
			// The file on disk is what pass 1 wrote, or what pass 2 wrote
			// when the failure came after its write (Wrote()).
			_failed[number] = rewritten.error();
			_results.AddResult(DecompilerResultType::Error, fmt::format("Script {0} failed to write again: {1}", number, rewritten.error().ToString()));
		}
		else if (item->Wrote())
		{
			// Written again, also when the abort came after the write.
			_rewritten.insert(number);
			// The files of pass 2 replace those of pass 1, and so does their
			// write status.
			_failed.erase(number);
			if (!item->WriteStatus())
			{
				_failed[number] = item->WriteStatus().error();
			}
		}
		else
		{
			// The abort came during its second decompile: nothing was written.
			_skippedRewrites.insert(number);
		}
		item.reset();
	}
	items.clear();

	_ReportMemory(_results, fmt::format("after writing {0} script(s) again", _rewritten.size()).c_str(), memoryAtStart);
}

sci::Status DecompileBatch::_UpdateMainObjectFile(CSCOFile *mainSCO)
{
	if (_globalRenames.empty())
	{
		return sci::Ok();
	}
	sci::Status announced = sci::Ok();
	// Script 0's .sco was written from its own script, names included,
	// unless a later round named a global it does not itself refer to.
	// Otherwise main's .sco on disk gets the names now, so the scripts
	// written here agree with it.
	bool mainWrittenLast = (_rewritten.find(0) != _rewritten.end());
	bool checkOnly = _output && _output->ChecksTheWrites();
	if (mainSCO && !mainWrittenLast && (!_output || checkOnly))
	{
		ScriptId script = _ObjectFileScript(_helper, *mainSCO);
		if (checkOnly)
		{
			// A dry run: the check that the write makes, and no write.
			sci::Result<bool> wouldChange = SCOFileWouldChange(_helper, *mainSCO, script);
			if (wouldChange)
			{
				_mainObjectFileChanged = *wouldChange;
			}
			else
			{
				_mainObjectFile = sci::Fail(wouldChange.error());
			}
		}
		else
		{
			// The message names the step (the crash line of scic). The
			// write comes also when the message throws.
			announced = sci::Guard("", [&]() -> sci::Status
			{
				_results.AddResult(DecompilerResultType::Important, "Updating global variables in script 0");
				return sci::Ok();
			});
			_mainObjectFile = SaveSCOFile(_helper, *mainSCO, script, &_mainObjectFileChanged);
		}
		if (!_mainObjectFile)
		{
			_results.AddResult(DecompilerResultType::Error, _mainObjectFile.error().ToString());
		}
	}
	_results.SetGlobalVarsUpdated(_globalRenames);
	return announced;
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

set<uint16_t> FindScriptsReferencingGlobals(const GameFolderHelper &helper, const set<uint16_t> &candidates, const vector<pair<string, string>> &renames,
	const map<uint16_t, string> *sources)
{
	set<uint16_t> stale;
	if (renames.empty())
	{
		return stale;
	}
	for (uint16_t scriptNumber : candidates)
	{
		string text;
		auto source = sources ? sources->find(scriptNumber) : map<uint16_t, string>::const_iterator();
		if (sources && (source != sources->end()))
		{
			text = source->second;
		}
		else
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
			text.assign(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
		}
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
