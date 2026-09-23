#pragma once

#include "Result.h"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sci { class Script; }
class IDecompilerConfig;
class IDecompilerResults;
class CSCOFile;
class GlobalCompiledScriptLookups;
class GameFolderHelper;
class CResourceMap;

struct DecompileOptions
{
	bool DebugControlFlow = false;
	bool DebugInstructionConsumption = false;
	std::string DebugFunctionMatch;
	bool DecompileAsm = false;
	bool SubstituteTextTuples = false;
};

// Instead of the files: the source of each script (the command line's
// --stdout; plan step S4). With it, the batch writes no .sc and no .sco file,
// and not main's .sco. A script that the batch decompiles again (pass 2)
// gives its source again; the last one counts.
class IDecompileOutput
{
public:
	virtual ~IDecompileOutput() = default;
	virtual void OnSource(uint16_t scriptNumber, const std::string &source) = 0;
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
// This keeps one script's syntax tree in memory at a time, and decompiles a
// script a second time only when it has to:
//
//  1. Decompile each script, name it against the global names known so far,
//     and write it. If it still refers to a global by its standard name
//     (globalN), keep its naming skeleton (see BuildNamingSkeleton); either
//     way, let the tree go.
//  2. Run the (cheap) naming pass over the skeletons, round after round, until
//     a round names no more globals.
//  3. Decompile and write again only the scripts a later round changed
//     something in: a global they referred to as globalN gained a name, or a
//     global gained a name they already use for something else.
//
// With the game's globals already named, no script is decompiled twice.
class DecompileBatch
{
public:
	// The resource map gives the game (its helper), the text resources and
	// vocab.000.
	DecompileBatch(const IDecompilerConfig *config, GlobalCompiledScriptLookups &scriptLookups, CResourceMap &resourceMap, IDecompilerResults &results, const DecompileOptions &options = DecompileOptions(),
		IDecompileOutput *output = nullptr);
	~DecompileBatch();
	DecompileBatch(const DecompileBatch &) = delete;
	DecompileBatch &operator=(const DecompileBatch &) = delete;

	// Decompiles the scripts, names their variables together, and writes each
	// one's .sc and .sco, plus main's .sco when a global gained a name (unless
	// script 0 is in the batch, whose own .sco then carries the names).
	// An abort stops the batch where it is; the scripts already written stay.
	// A script that fails to decompile is reported and dropped, and the rest
	// go on. Each script runs inside an exception boundary (plan step S4).
	void Run(const std::set<uint16_t> &scriptNumbers);

	// The globals this run named: (standard name, new name).
	const std::vector<std::pair<std::string, std::string>> &GetGlobalRenames() const { return _globalRenames; }
	// The scripts whose files this run wrote.
	const std::set<uint16_t> &GetWrittenScripts() const { return _written; }
	// Of those, the scripts decompiled and written a second time because a
	// later naming round changed something they can see.
	const std::set<uint16_t> &GetRewrittenScripts() const { return _rewritten; }
	// The scripts that failed, and why: the compiled script did not load, an
	// exception in the decompiler or the naming, or a .sc or .sco file that
	// could not be written (the first error of the script). A script can be
	// in GetWrittenScripts too: a file of it was written.
	const std::map<uint16_t, sci::Error> &GetFailedScripts() const { return _failed; }
	// The write of main's .sco with the new global names at the end of the
	// run: Ok, also when it was not needed (S4 review: before, a failure was
	// a message only).
	const sci::Status &GetMainObjectFileStatus() const { return _mainObjectFile; }
	// The scripts that needed a second write with the new global names, and
	// that an abort stopped before it: their files still use the old names.
	const std::set<uint16_t> &GetSkippedRewrites() const { return _skippedRewrites; }
	// A global gained a name that script 0's own .sco does not carry: the
	// batch wrote main's .sco with the names (with an output, a batch that
	// writes files would write it; review of 11106215).
	bool MainObjectFileNeeded() const { return _mainObjectFileNeeded; }

	// With an output (a dry run of several groups): the main .sco that Run
	// starts from instead of the file (null: the file), and the one that it
	// ended with, so that the next group sees the names of this one, as it
	// reads them from the file after a run that writes (review of
	// 11106215).
	void SetMainObjectFile(std::unique_ptr<CSCOFile> mainSCO);
	std::unique_ptr<CSCOFile> TakeMainObjectFile();

private:
	class Item;

	const IDecompilerConfig *_config;
	GlobalCompiledScriptLookups &_scriptLookups;
	CResourceMap &_resourceMap;
	const GameFolderHelper &_helper;
	IDecompilerResults &_results;
	DecompileOptions _options;
	IDecompileOutput *_output;

	std::vector<std::pair<std::string, std::string>> _globalRenames;
	std::set<uint16_t> _written;
	std::set<uint16_t> _rewritten;
	std::map<uint16_t, sci::Error> _failed;
	sci::Status _mainObjectFile;
	std::set<uint16_t> _skippedRewrites;
	bool _mainObjectFileNeeded = false;
	std::unique_ptr<CSCOFile> _mainSCO;
};

// The naming skeleton of a decompiled script: what the variable namer reads
// and nothing else. The script variables (names and sizes), each class with its
// methods, each procedure, and in each function one assignment per assignment
// of the full tree, each side reduced to its top node (a value, an lvalue, or
// a send of one selector). Running the namer over it names the same globals
// as running it over the full tree, at a fraction of the memory.
std::unique_ptr<sci::Script> BuildNamingSkeleton(sci::Script &script);

// True if identifier occurs in text as a whole word: not as part of a longer
// identifier (so "global3" is not found in "global30").
bool ContainsIdentifier(const std::string &text, const std::string &identifier);

// Of the candidate scripts, those whose .sc file on disk refers to any of the
// renamed globals by its old (standard) name. They were decompiled before the
// global was named and need decompiling again. A script with no source file is
// never stale. A script in sources is read from there, not from its file (a
// dry run: the source that a run that writes would have written).
std::set<uint16_t> FindScriptsReferencingGlobals(const GameFolderHelper &helper, const std::set<uint16_t> &candidates, const std::vector<std::pair<std::string, std::string>> &renames,
	const std::map<uint16_t, std::string> *sources = nullptr);
