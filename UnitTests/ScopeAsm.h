#pragma once

#include <list>
#include <map>
#include <string>
#include "Version.h"
#include "scii.h"

// The instructions of one function, from a text in the syntax of an asm
// block: one instruction or one label ("name:") on each line, and ';' for a
// comment. A branch takes a label; each other operand is a number. The
// address of an instruction is its index, so a dump shows the index.
class ScopeAsm
{
public:
	explicit ScopeAsm(const std::string &text);

	std::list<scii> code;

	// The index of the instruction at a label. Asserts that the label exists.
	int At(const std::string &label) const;

private:
	std::map<std::string, int> _labels;
};
