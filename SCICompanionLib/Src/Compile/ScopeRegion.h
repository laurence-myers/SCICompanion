#pragma once

#include <memory>
#include <string>
#include <vector>
#include "ScopeCode.h"

// The region tree of the scope engine (docs\decompiler-scope-parser\plan.md,
// section 3.3): the control structure of one function. Each instruction of
// the function is in the tree one time, and a walk of the tree in layout
// order (see Layout) gives the instructions in address order. The tree has
// no values: the forward value stage reads the accumulator at each test.
namespace scope
{
	enum class RegionKind
	{
		Sequence,	// items, in address order
		Code,		// the instructions first..last, with no branch of the control flow
		If,			// tests, terms, then-part, else
		Or,			// a bt over the second operand (body) to the end of the or
		Loop,		// body, step (a for loop), latch
		Switch,		// head, cases, toss
		Case,		// a case of a switch: value, test, body, jmp
		Break,		// a jmp to the exit of a loop
		Continue,	// a jmp to the continue point of a loop
		BreakIf,	// a bt to the exit of a loop
		ContIf,		// a bt to the continue point of a loop
	};

	// Where the tests of an if go for a false value.
	enum class ElseKind
	{
		None,		// to the end of the if
		Else,		// to the else-part, after the jmp at the end of the then-part
		Break,		// to the exit of a loop
		Continue,	// to the continue point of a loop
	};

	struct Region
	{
		explicit Region(RegionKind kind) : kind(kind) {}

		RegionKind kind;

		// Sequence: the items, in address order.
		std::vector<std::unique_ptr<Region>> items;

		// Code: the instructions first..last. They can be dead, a branch
		// that does nothing, or an inert bnt of an n-ary compare.
		int first = NoIndex;
		int last = NoIndex;

		// If: the bnt of each test, in address order. The first test takes
		// the accumulator at the start of the if; terms[k] (a sequence) is
		// between tests[k] and tests[k + 1].
		std::vector<int> tests;
		std::vector<std::unique_ptr<Region>> terms;
		std::unique_ptr<Region> thenPart;
		ElseKind elseKind = ElseKind::None;
		std::unique_ptr<Region> elsePart;

		// Or, BreakIf, ContIf: the bt. Break, Continue: the jmp. If: the jmp
		// before the else-part. Loop: the latch (the last back branch).
		// Case: the bnt of the test, or NoIndex.
		int branch = NoIndex;
		// Case: the jmp at the end of the body, or NoIndex.
		int caseJmp = NoIndex;
		// Break, Continue, BreakIf, ContIf, and an If with ElseKind Break or
		// Continue: the loop, 1 for the innermost one.
		int level = 0;

		// Or: the second operand. Loop: the body. Case: the body.
		std::unique_ptr<Region> body;
		// Loop: the step of a for loop (the continue point), or null.
		std::unique_ptr<Region> step;
		// Case: the dup, the case value and the eq?; null for an else case.
		std::unique_ptr<Region> value;

		// Loop: the head (the target of the latch). Switch: the push of the
		// switch value.
		int head = NoIndex;
		// Switch: the toss.
		int toss = NoIndex;
		// Switch: the cases, in address order.
		std::vector<std::unique_ptr<Region>> cases;
	};

	std::unique_ptr<Region> MakeSequence();
	std::unique_ptr<Region> MakeCode(int first, int last);

	// The instructions of the tree in layout order: each owned instruction
	// (a code instruction, a branch of a node, the head and the toss of a
	// switch) as the templates of the compiler put it.
	std::vector<int> Layout(const Region &root);

	// A text dump: one line for each region, with the addresses of its
	// instructions, and two more spaces of indent for each level. The root
	// sequence gives its items with no indent.
	std::string Dump(const CodeModel &model, const Region &root);
}
