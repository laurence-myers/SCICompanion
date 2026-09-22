#pragma once

class IDecompilerConfig;
class IDecompilerResults;
class GlobalCompiledScriptLookups;
class GameFolderHelper;

struct DecompileOptions
{
	bool DebugControlFlow = false;
	bool DebugInstructionConsumption = false;
	std::string DebugFunctionMatch;
	bool DecompileAsm = false;
	bool SubstituteTextTuples = false;
};

// Decompiles a set of scripts and writes their .sc and .sco files.
//
// The names of global variables are found from the way the globals are used, in
// whichever script uses them, and a name found in one script lets other scripts
// name more (a global assigned from a newly named global, say). Decompiling the
// scripts one at a time, each against main's .sco on disk, leaves every script
// decompiled before a name was found with the old name, so the whole game had
// to be decompiled again, and again, until a pass named nothing new.
//
// This does the expensive part once: it decompiles each script to its syntax
// tree, then runs the (cheap) naming pass over all the trees, round after round,
// until a round names no more globals, and only then finishes and writes every
// script. The trees of the whole batch are held in memory meanwhile.
class DecompileBatch
{
public:
	DecompileBatch(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, const GameFolderHelper &helper, IDecompilerResults &results, const DecompileOptions &options = DecompileOptions());
	~DecompileBatch();
	DecompileBatch(const DecompileBatch &) = delete;
	DecompileBatch &operator=(const DecompileBatch &) = delete;

	// Decompiles the scripts, names their variables together, and writes each
	// one's .sc and .sco, plus main's .sco when a global gained a name (unless
	// script 0 is in the batch, whose own .sco then carries the names).
	// An abort drops the script being decompiled at that moment; the scripts
	// already decompiled are still named and written. A script that fails to
	// decompile is reported and dropped, and the rest go on.
	void Run(const std::set<uint16_t> &scriptNumbers);

	// The globals this run named: (standard name, new name).
	const std::vector<std::pair<std::string, std::string>> &GetGlobalRenames() const { return _globalRenames; }
	// The scripts whose files this run wrote.
	const std::set<uint16_t> &GetWrittenScripts() const { return _written; }

private:
	class Item;

	const IDecompilerConfig *_config;
	GlobalCompiledScriptLookups &_scriptLookups;
	const GameFolderHelper &_helper;
	IDecompilerResults &_results;
	DecompileOptions _options;

	std::vector<std::pair<std::string, std::string>> _globalRenames;
	std::set<uint16_t> _written;
};

// True if identifier occurs in text as a whole word: not as part of a longer
// identifier (so "global3" is not found in "global30").
bool ContainsIdentifier(const std::string &text, const std::string &identifier);

// Of the candidate scripts, those whose .sc file on disk refers to any of the
// renamed globals by its old (standard) name. They were decompiled before the
// global was named and need decompiling again. A script with no source file is
// never stale.
std::set<uint16_t> FindScriptsReferencingGlobals(const GameFolderHelper &helper, const std::set<uint16_t> &candidates, const std::vector<std::pair<std::string, std::string>> &renames);
