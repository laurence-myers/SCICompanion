/***************************************************************************
	Copyright (c) 2026 Philip Fortier

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
***************************************************************************/
#pragma once

#include <memory>
#include <vector>

namespace sci
{
	class SyntaxNode;
	class FunctionBase;
	class ConditionNode;
	class CodeBlock;
	class LValue;
}
class IDecompilerResults;

//
// A small framework for rewriting the decompiler's sci:: AST in passes.
//
// The sci:: AST has no parent pointers and no generic child list, so this
// walks the tree per node type (see AstRewrite.cpp), keeps a stack of the
// nodes above the current one, and lets a pass replace, edit, or remove the
// node at the current slot. Passes run depth-first, children before parents,
// which is what lets a chain like (if A (if B (if C D))) collapse in one
// sweep. See DecompilerAstPasses for the passes that use this.
//

// The role a child node plays in its parent. A pass reads this to decide if,
// for example, an if is used as a value (a value slot) or a statement.
enum class SlotKind
{
	Statement,          // an element of a statement list (function body, code block, loop body, case body)
	IfCondition,
	WhileCondition,
	DoCondition,
	IfThen,
	IfElse,
	AndOrOperand,       // an operand of a logical and/or
	NotOperand,         // the operand of a logical not
	Operand,            // an operand of any other operator (arithmetic, compare)
	SendTarget,         // the object of a send, when it is an expression
	SendArg,            // an argument in a send parameter
	ProcArg,            // an argument to a procedure/kernel call
	ReturnValue,
	AssignValue,        // the value of an assignment
	SwitchValue,
	CaseValue,
	CastValue,
	// Typed children, recursed into but never replaced by a pass.
	AssignTarget,
	SendParamNode,
	CaseNode,
};

// What a pass did to its slot. The walker uses this to keep traversal correct.
enum class RewriteResult
{
	None,       // no change
	Changed,    // edited the node in place, or edited its children or later siblings
	Replaced,   // *slot now holds a different node
	Removed,    // the slot was erased from its list (only valid for a Statement slot)
};

class AstContext;

// A single rewrite pass. Rewrite is called on each node after its subtree has
// been visited. A pass may replace *slot, edit the node or its children, insert
// statements after the current one, or (for a Statement slot) erase later
// siblings. It must not touch ancestors or earlier siblings. It must not throw.
class AstPass
{
public:
	virtual ~AstPass() {}
	virtual const char *Name() const = 0;
	virtual RewriteResult Rewrite(std::unique_ptr<sci::SyntaxNode> &slot, const AstContext &context) = 0;
};

// The nodes above the current slot, innermost last. Read-only for passes.
class AstContext
{
public:
	struct Frame
	{
		sci::SyntaxNode *node;
		SlotKind kind;
		std::vector<std::unique_ptr<sci::SyntaxNode>> *list; // non-null when the slot is a list element
		size_t index;
	};

	size_t Depth() const { return _frames.size(); }
	const Frame &Current() const { return _frames.back(); }
	SlotKind Kind() const { return _frames.back().kind; }
	std::vector<std::unique_ptr<sci::SyntaxNode>> *List() const { return _frames.back().list; }
	size_t Index() const { return _frames.back().index; }

	// The node up levels above the current one. Parent(0) is the immediate
	// parent. Returns nullptr past the top.
	const sci::SyntaxNode *Parent(size_t up = 0) const
	{
		// The current slot is the last frame. Its parent is the frame before it.
		size_t want = up + 2;
		if (_frames.size() < want)
		{
			return nullptr;
		}
		return _frames[_frames.size() - want].node;
	}

	// True when the current node is used as a boolean: it is a condition, a not
	// operand, or an and/or operand that itself sits in a boolean position.
	bool IsBooleanContext() const;

	// Internal: the walker pushes and pops frames.
	std::vector<Frame> _frames;
};

// Runs one depth-first sweep of the pass over the function body. Returns true
// if anything changed.
bool RunPassOnce(sci::FunctionBase &func, AstPass &pass);

// Runs the passes over the function body until a sweep makes no change, or
// maxSweeps is reached (then it reports a warning through results, if given).
void RunPassesToFixpoint(sci::FunctionBase &func, const std::vector<AstPass *> &passes,
	int maxSweeps, IDecompilerResults *results);

//
// Helpers shared by the passes.
//

// The node as a CodeBlock, or nullptr.
sci::CodeBlock *AsCodeBlock(sci::SyntaxNode *node);

// The single meaningful statement in a code block, or nullptr if it does not
// hold exactly one. Empty comment nodes do not count.
sci::SyntaxNode *SingleStatement(sci::SyntaxNode *block);

// True if the node is a break or continue with no explicit level.
bool IsSingleLevelBreak(const sci::SyntaxNode *node);
bool IsSingleLevelContinue(const sci::SyntaxNode *node);

// True if the condition is the constant TRUE (a repeat loop's test).
bool IsTrueCondition(const sci::ConditionNode &conditionOwner);

// The single mutable statement inside a ConditionNode's inner expression.
std::unique_ptr<sci::SyntaxNode> &ConditionSlot(sci::ConditionNode &conditionOwner);

// Wraps a bare expression in a ConditionalExpression, for a ConditionNode.
void SetConditionExpression(sci::ConditionNode &conditionOwner, std::unique_ptr<sci::SyntaxNode> expr);

// Builds (and a b) / (or a b), copying position from a source node.
std::unique_ptr<sci::SyntaxNode> MakeAnd(std::unique_ptr<sci::SyntaxNode> a, std::unique_ptr<sci::SyntaxNode> b, const sci::SyntaxNode *posSource);
std::unique_ptr<sci::SyntaxNode> MakeOr(std::unique_ptr<sci::SyntaxNode> a, std::unique_ptr<sci::SyntaxNode> b, const sci::SyntaxNode *posSource);

// True if the two nodes read the same variable or property, comparing an
// assignment target (an LValue) with a value expression (a token, an indexed
// value, or an LValue).
bool IsSameVariable(const sci::LValue &target, const sci::SyntaxNode &read);

// True if the two nodes are structurally identical (same type, same scalar
// value, children equal in order).
bool StructEqual(const sci::SyntaxNode *a, const sci::SyntaxNode *b);

// True if the function body contains a while, do, or for loop.
bool FunctionHasLoop(sci::FunctionBase &func);
