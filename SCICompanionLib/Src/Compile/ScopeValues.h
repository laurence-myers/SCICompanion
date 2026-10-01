#pragma once

#include <list>
#include <memory>
#include "ScopeCode.h"
#include "ScopeRegion.h"

struct ConsumptionNode;

// The forward value stage of the scope engine (plan section 3.5, in
// docs\decompiler-scope-parser\plan.md): it walks the region tree forwards
// with a symbolic stack, an accumulator and a statement list, and builds the
// chunk tree that the syntax stage reads.
namespace scope
{
	// The chunk tree of the function (a FunctionBody node). code is the list
	// of instructions that the model was made from: the nodes point into it.
	// With returnsValue, a ret reads the accumulator.
	// Throws a ScopeError (stage "values") when a value has no source, or when
	// an invariant of the chunk tree fails: each instruction is in the tree
	// one time (copies are marked), the stack is balanced at each statement
	// and at the end of each sequence, each read of the accumulator has a
	// node or a fact, and the children of a node are in address order.
	std::unique_ptr<ConsumptionNode> BuildValues(const CodeModel &model, const Region &root, std::list<scii> &code, bool returnsValue);
}
