#pragma once

#include <functional>
#include <list>
#include <string>
#include <vector>
#include "PMachine.h"
#include "Result.h"
#include "Version.h"
#include "scii.h"

class CompiledScript;
class DecompileLookups;
class GameFolderHelper;
class GlobalCompiledScriptLookups;
struct Vocab000;

// The meaning check (docs\decompiler-scope-parser\plan.md, section 3.6). It
// compares a function of the original bytecode with the same function of
// the bytecode that the compiler makes from the decompiled text. The bytes
// differ, so the check compares the effects: a symbolic walk of each
// function gives the calls, the stores, the tests and the returns, with an
// expression for each value; a walk of the two functions side by side
// (a bisimulation) matches each effect with an effect of the same kind and
// equal expressions, and each outcome of a test with the same outcome.
namespace meaning
{
	// An instruction, with the operands that the check reads.
	struct Instruction
	{
		Opcode op = Opcode::INDETERMINATE;
		uint16_t operands[3] = { 0, 0, 0 };
		uint16_t offset = 0;
		// The index of the target of a branch; -1 for another instruction.
		int target = -1;
		// lofsa, lofss: the thing at the address ("object Name", "string
		// text", "said text"; empty when there is none). call: the key of the
		// procedure.
		std::string text;
	};

	// One function, for the check.
	struct Function
	{
		// The key pairs a function of the original script with one of the
		// recompiled script: "<object>::<selector>", "export <n>" or
		// "local <n>" (the internal procedures in address order).
		std::string key;
		// The name for a report.
		std::string display;
		// The address of the code.
		uint16_t offset = 0;
		// A ret reads the accumulator (the guess of the decompiler for the
		// original function).
		bool returnsValue = false;
		// A function of script 0: its local variables are the global
		// variables.
		bool localsAreGlobals = false;
		std::vector<Instruction> code;
		// The stack depth before each instruction (scope::CodeModel: the
		// smaller depth of two paths); empty when the code model cannot read
		// the function.
		std::vector<int> depth;
		// Not empty: the check cannot read the function, and why.
		std::string unreadable;
	};

	enum class Verdict
	{
		Same,
		Diff,
		Uncompared,
	};

	// SAME, DIFF or UNCOMPARED.
	const char *VerdictName(Verdict verdict);

	struct Outcome
	{
		Verdict verdict = Verdict::Uncompared;
		// DIFF: the first place that differs, with the two effects;
		// UNCOMPARED: the reason.
		std::string detail;
	};

	// The function of the instructions of the decompiler (the branch targets
	// point into code; the placeholder Opcode::INDETERMINATE is left out).
	// addressText gives the thing at the address of a lofsa or lofss (empty
	// when there is none); procedureKey gives the key of the procedure at the
	// address of a call.
	Function MakeFunction(const std::string &key, const std::string &display, std::list<scii> &code, bool returnsValue, const SCIVersion &version,
		const std::function<std::string(uint16_t)> &addressText, const std::function<std::string(uint16_t)> &procedureKey);

	// The effects of the two functions match. The returns of the recompiled
	// function read the accumulator when the original's do. A path with a
	// form that the check does not read stops there: the verdict is DIFF
	// when another path differs, else UNCOMPARED.
	Outcome Compare(const Function &original, const Function &recompiled);

	// The outcome of one function of a script.
	struct FunctionOutcome
	{
		std::string key;
		std::string display;
		// The address of the code of the original function.
		uint16_t offset = 0;
		Outcome outcome;
	};

	// The functions of two scripts, paired by key, in the order of the
	// original, then the recompiled functions that the original has not. A
	// function with no partner is DIFF ("no-recompiled-function" or
	// "no-original-function").
	std::vector<FunctionOutcome> CompareFunctions(const std::vector<Function> &original, const std::vector<Function> &recompiled);

	// The functions of a compiled script (ReadScriptFunctions), for the check.
	// The object names must be unique (FixDuplicateObjectNames).
	std::vector<Function> ReadFunctions(const CompiledScript &script, DecompileLookups &lookups, const Vocab000 *pWords);

	// The functions of a script of the game, for the check. Fails when the
	// compiled script cannot be loaded.
	sci::Result<std::vector<Function>> ReadScript(const GameFolderHelper &helper, GlobalCompiledScriptLookups &lookups, const Vocab000 *pWords, uint16_t scriptNumber);

	// The functions of a script from the data of its resources (heap: for a
	// version with a separate heap), for the check: a script that a compile
	// wrote into a folder (scic script compile --out-dir --raw). Fails when
	// the data cannot be read.
	sci::Result<std::vector<Function>> ReadScriptData(const GameFolderHelper &helper, GlobalCompiledScriptLookups &lookups, const Vocab000 *pWords, uint16_t scriptNumber,
		const std::vector<uint8_t> &script, const std::vector<uint8_t> *heap);
}
