#pragma once

#include <list>
#include <memory>
#include <string>
#include "ScopeCode.h"
#include "ScopeRegion.h"

// The scope parser of the scope engine (docs\decompiler-scope-parser\plan.md,
// section 3.3). It reads the instructions in address order. Each open
// construct is a scope with a known end, and each branch goes to a place
// that an enclosing scope gives: the end of the sequence, the else entry of
// the if, or the exit or the continue point of a loop. Two places are one
// place when they resolve to the same instruction (section 3.1).
namespace scope
{
	// The region tree of the function. Throws a ScopeError (stage "parse")
	// with the address of a branch that goes to no place of a scope. With a
	// dead latch, it runs the verify stage on the tree of the live latches
	// (see Parse in ScopeParser.cpp); the caller still verifies the tree
	// that it returns.
	std::unique_ptr<Region> Parse(const CodeModel &model);

	// For a debug dump: the region tree of the instructions, after the
	// verify stage, as a text; or the message, the address and the detail
	// of the error of the stage that failed.
	std::string ParseForDump(const std::list<scii> &code);

	// For a debug dump: the code model of the instructions (CodeModel::Dump),
	// or the error of the code model.
	std::string CodeForDump(const std::list<scii> &code);
}
