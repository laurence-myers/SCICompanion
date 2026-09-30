#pragma once

#include "ScopeCode.h"
#include "ScopeRegion.h"

// The verify stage of the scope engine (docs\decompiler-scope-parser\plan.md,
// section 3.4). It emits the branch skeleton of the region tree with the
// plain templates of the compiler, and compares, for each live instruction
// that is not a branch, where control goes next in the skeleton and in the
// bytecode. Both sides resolve their branches with the value in the
// accumulator (section 3.1), so a threaded branch agrees with its plain
// template.
namespace scope
{
	// Throws a ScopeError (stage "verify") with the address of the first
	// instruction that does not agree. The ids:
	// - layout: the instructions of the tree in layout order are not the
	//   instructions of the function in address order;
	// - opcode: a node owns an instruction of another kind (for example an
	//   if test that is not a bnt, or an inert bnt of an n-ary compare);
	// - loop-head: the latch of a loop does not go to the head of the loop;
	// - branch-in-code: a code region has a branch of the control flow;
	// - level: a break or continue names a loop that does not enclose it;
	// - entry: at the start of the function, control goes to another place
	//   in the tree than in the bytecode;
	// - successor: after an instruction, control goes to another place in
	//   the tree than in the bytecode.
	// The detail of the error tells the two places.
	void Verify(const CodeModel &model, const Region &root);
}
