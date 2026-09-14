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
#include "stdafx.h"
#include "DecompilerAstPasses.h"
#include "AstRewrite.h"
#include "ScriptOMAll.h"
#include "Operators.h"

using namespace sci;
using namespace std;

namespace
{
	// A value slot is one whose value is consumed as a boolean or an argument,
	// where an if can safely become an and/or. Assignment values are excluded:
	// Sierra source keeps (= x (if a b)) as an if, per the golden decompilations.
	bool IsValueSlot(SlotKind kind)
	{
		switch (kind)
		{
		case SlotKind::IfCondition:
		case SlotKind::WhileCondition:
		case SlotKind::DoCondition:
		case SlotKind::AndOrOperand:
		case SlotKind::NotOperand:
		case SlotKind::CaseValue:
		case SlotKind::SendArg:
		case SlotKind::ProcArg:
		case SlotKind::ReturnValue:
			return true;
		default:
			return false;
		}
	}

	bool EndsInReturn(SyntaxNode *node);
	std::vector<SyntaxNode *> MeaningfulStatements(SyntaxNode *block);

	// A slot whose value is used, other than as a boolean.
	bool IsPlainValueSlot(SlotKind kind)
	{
		switch (kind)
		{
		case SlotKind::AssignValue:
		case SlotKind::SendArg:
		case SlotKind::ProcArg:
		case SlotKind::ReturnValue:
		case SlotKind::Operand:
		case SlotKind::CaseValue:
		case SlotKind::SwitchValue:
		case SlotKind::SendTarget:
		case SlotKind::CastValue:
			return true;
		default:
			return false;
		}
	}

	// True if the current node's value is used: its slot is a value slot, or
	// it is the last statement of a branch of an if whose value is used (a
	// cond case body is such a branch). boolean: a boolean slot counts too.
	bool IsValueContext(const AstContext &ctx, bool boolean)
	{
		size_t i = ctx._frames.size();
		while (i > 0)
		{
			const AstContext::Frame &frame = ctx._frames[i - 1];
			if (IsPlainValueSlot(frame.kind) || (boolean && IsValueSlot(frame.kind)))
			{
				return true;
			}
			if ((frame.kind == SlotKind::IfThen) || (frame.kind == SlotKind::IfElse))
			{
				i--;	// the if itself
				continue;
			}
			if ((frame.kind == SlotKind::Statement) && frame.list && (i >= 2))
			{
				const AstContext::Frame &block = ctx._frames[i - 2];
				if ((block.node->GetNodeType() != NodeTypeCodeBlock) ||
					((block.kind != SlotKind::IfThen) && (block.kind != SlotKind::IfElse)))
				{
					return false;
				}
				// The sole statement of the branch: a case body with several
				// statements keeps its last if as an if (the golden text does).
				vector<SyntaxNode *> statements = MeaningfulStatements(block.node);
				if ((statements.size() != 1) || (statements[0] != frame.node))
				{
					return false;
				}
				i -= 2;	// the block, then the if
				continue;
			}
			return false;
		}
		return false;
	}

	// True if the current node is the sole statement of the then of an if
	// with no else: the nested-if merge takes that shape, and the golden
	// text prefers the merge to an and in the case body.
	bool IsSoleThenOfIfWithoutElse(const AstContext &ctx)
	{
		size_t i = ctx._frames.size();
		if (i < 2)
		{
			return false;
		}
		const AstContext::Frame &frame = ctx._frames[i - 1];
		const AstContext::Frame *ifFrame = nullptr;
		if (frame.kind == SlotKind::IfThen)
		{
			ifFrame = &ctx._frames[i - 2];
		}
		else if ((frame.kind == SlotKind::Statement) && frame.list && (i >= 3))
		{
			const AstContext::Frame &block = ctx._frames[i - 2];
			if ((block.node->GetNodeType() == NodeTypeCodeBlock) && (block.kind == SlotKind::IfThen) &&
				(MeaningfulStatements(block.node).size() == 1))
			{
				ifFrame = &ctx._frames[i - 3];
			}
		}
		if (!ifFrame)
		{
			return false;
		}
		IfStatement *parent = SafeSyntaxNode<IfStatement>(ifFrame->node);
		return parent && !parent->HasElse();
	}

	// True if the current if is the else of another if (a cond case).
	bool IsElseIf(const AstContext &ctx)
	{
		size_t i = ctx._frames.size();
		if (i == 0)
		{
			return false;
		}
		const AstContext::Frame &frame = ctx._frames[i - 1];
		if (frame.kind == SlotKind::IfElse)
		{
			return true;
		}
		if ((frame.kind == SlotKind::Statement) && frame.list && (i >= 2))
		{
			const AstContext::Frame &block = ctx._frames[i - 2];
			return (block.node->GetNodeType() == NodeTypeCodeBlock) && (block.kind == SlotKind::IfElse) &&
				(MeaningfulStatements(block.node).size() == 1);
		}
		return false;
	}

	// The meaningful statements of an if branch, skipping empty comment nodes.
	// An if branch is a CodeBlock from the decompiler, but the parser can leave
	// a single statement bare, so a non-block node counts as one statement.
	vector<SyntaxNode *> MeaningfulStatements(SyntaxNode *block)
	{
		vector<SyntaxNode *> result;
		CodeBlock *cb = AsCodeBlock(block);
		if (cb)
		{
			for (const unique_ptr<SyntaxNode> &s : cb->GetStatements())
			{
				if (!SafeSyntaxNode<Comment>(s.get()))
				{
					result.push_back(s.get());
				}
			}
		}
		else if (block && !SafeSyntaxNode<Comment>(block))
		{
			result.push_back(block);
		}
		return result;
	}

	// The meaningful statements of a list, skipping comment nodes.
	vector<SyntaxNode *> MeaningfulStatementsOf(const SyntaxNodeVector &list)
	{
		vector<SyntaxNode *> result;
		for (const unique_ptr<SyntaxNode> &s : list)
		{
			if (!SafeSyntaxNode<Comment>(s.get()))
			{
				result.push_back(s.get());
			}
		}
		return result;
	}

	bool IsEmptyBlock(SyntaxNode *block)
	{
		return MeaningfulStatements(block).empty();
	}

	// A control-flow statement is not a value, so it must never be pulled into
	// an and/or operand (e.g. (if A (return 1)) is not (and A (return 1))).
	bool IsControlFlow(SyntaxNode *node)
	{
		if (!node)
		{
			return false;
		}
		NodeType t = node->GetNodeType();
		return (t == NodeTypeReturn) || (t == NodeTypeBreak) || (t == NodeTypeContinue);
	}

	// A property value (PropertyValue or ComplexPropertyValue), or null. The
	// decompiler emits numbers as PropertyValue; the parser can emit them as
	// ComplexPropertyValue, so check the shared base.
	PropertyValueBase *AsValue(SyntaxNode *node)
	{
		if (node && ((node->GetNodeType() == NodeTypeValue) || (node->GetNodeType() == NodeTypeComplexValue)))
		{
			return static_cast<PropertyValueBase *>(node);
		}
		return nullptr;
	}


	// Moves the single meaningful statement out of an if branch and returns it.
	// Handles a bare (non-block) branch by moving the whole slot.
	unique_ptr<SyntaxNode> TakeSingleStatement(unique_ptr<SyntaxNode> &blockSlot)
	{
		CodeBlock *cb = AsCodeBlock(blockSlot.get());
		if (!cb)
		{
			return move(blockSlot);
		}
		for (unique_ptr<SyntaxNode> &s : cb->GetStatements())
		{
			if (!SafeSyntaxNode<Comment>(s.get()))
			{
				return move(s);
			}
		}
		return nullptr;
	}

	//
	// IfThenToAnd: nested-if merge, and value-position if -> and/or.
	//
	class IfThenToAnd : public AstPass
	{
	public:
		const char *Name() const override { return "IfThenToAnd"; }

		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(slot.get());
			if (!if_)
			{
				return RewriteResult::None;
			}

			// Rule 1 (any position): (if A (if B C...)) -> (if (and A B) C...).
			// The outer has no else, and its then is exactly one if with no else.
			if (!if_->HasElse() && HasConditionExpression(*if_))
			{
				vector<SyntaxNode *> thenStmts = MeaningfulStatements(if_->GetStatement1());
				if (thenStmts.size() == 1)
				{
					IfStatement *inner = SafeSyntaxNode<IfStatement>(thenStmts[0]);
					if (inner && !inner->HasElse() && HasConditionExpression(*inner))
					{
						unique_ptr<SyntaxNode> outerCond = move(ConditionSlot(*if_));
						unique_ptr<SyntaxNode> innerCond = move(ConditionSlot(*inner));
						SetConditionExpression(*if_, MakeAnd(move(outerCond), move(innerCond), if_));
						if_->SetStatement1(move(inner->GetStatement1Internal()));
						return RewriteResult::Changed;
					}
				}
			}

			// Value-position rules. A branch with a return inside is never an
			// operand. An assignment's own value stays an if, (= x (if a b)),
			// as the golden text writes it; a value if inside one is folded.
			// A cond case (an else-if) stays a case. The sole if in the then of
			// an if without an else is left for the merge above (the parent is
			// visited after its children).
			if (IsValueContext(ctx, true) && (ctx.Kind() != SlotKind::AssignValue) && !IsElseIf(ctx) &&
				!(if_->HasElse() ? false : IsSoleThenOfIfWithoutElse(ctx)))
			{
				if (!if_->HasElse())
				{
					// (if A B) -> (and A B)
					vector<SyntaxNode *> thenStmts = MeaningfulStatements(if_->GetStatement1());
					if ((thenStmts.size() == 1) && !IsControlFlow(thenStmts[0]) && !EndsInReturn(thenStmts[0]))
					{
						unique_ptr<SyntaxNode> cond = move(ConditionSlot(*if_));
						unique_ptr<SyntaxNode> thenExpr = TakeSingleStatement(if_->GetStatement1Internal());
						slot = MakeAnd(move(cond), move(thenExpr), if_);
						return RewriteResult::Replaced;
					}
				}
				else
				{
					// (if A <empty> else B) -> (or A B)
					if (IsEmptyBlock(if_->GetStatement1()))
					{
						vector<SyntaxNode *> elseStmts = MeaningfulStatements(if_->GetStatement2());
						if ((elseStmts.size() == 1) && !IsControlFlow(elseStmts[0]) && !EndsInReturn(elseStmts[0]))
						{
							unique_ptr<SyntaxNode> cond = move(ConditionSlot(*if_));
							unique_ptr<SyntaxNode> elseExpr = TakeSingleStatement(if_->GetStatement2Internal());
							slot = MakeOr(move(cond), move(elseExpr), if_);
							return RewriteResult::Replaced;
						}
					}
				}
			}
			return RewriteResult::None;
		}
	};

	//
	// CopyValue: an if whose value is used, with an else and an empty then,
	// tests a variable or an assignment. Sierra's compiler did not load the
	// variable again for the then, so the then is that variable: the golden
	// text writes (= x (if a a else b)) and (= x (if (= t y) t else b)).
	//
	class CopyValue : public AstPass
	{
	public:
		const char *Name() const override { return "CopyValue"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			if (!IsValueContext(ctx, false))
			{
				return RewriteResult::None;
			}
			// A value (or a b) with a variable first: the golden text writes
			// (if a a else b) for it, as for the if with an empty then.
			BinaryOp *op = SafeSyntaxNode<BinaryOp>(slot.get());
			if (op && (op->Operator == BinaryOperator::LogicalOr) && op->GetStatement1() && op->GetStatement2())
			{
				unique_ptr<PropertyValue> copy = CopyOf(op->GetStatement1());
				if (!copy)
				{
					return RewriteResult::None;
				}
				unique_ptr<IfStatement> made = make_unique<IfStatement>();
				made->SetPosition(op->GetPosition());
				SetConditionExpression(*made, move(op->GetStatement1Internal()));
				copy->SetPosition(op->GetPosition());
				unique_ptr<CodeBlock> then = make_unique<CodeBlock>();
				then->AddStatement(move(copy));
				made->SetStatement1(move(then));
				unique_ptr<CodeBlock> elseB = make_unique<CodeBlock>();
				elseB->AddStatement(move(op->GetStatement2Internal()));
				made->SetStatement2(move(elseB));
				slot = move(made);
				return RewriteResult::Replaced;
			}
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(slot.get());
			if (!if_ || !if_->HasElse() || !IsEmptyBlock(if_->GetStatement1()) || !HasConditionExpression(*if_))
			{
				return RewriteResult::None;
			}
			SyntaxNode *cond = ConditionSlot(*if_).get();
			unique_ptr<PropertyValue> copy = CopyOf(cond);
			if (!copy)
			{
				return RewriteResult::None;
			}
			copy->SetPosition(if_->GetPosition());
			unique_ptr<CodeBlock> then = make_unique<CodeBlock>();
			then->AddStatement(move(copy));
			if_->SetStatement1(move(then));
			return RewriteResult::Changed;
		}

	private:
		static bool IsIndexed(SyntaxNode *node)
		{
			ComplexPropertyValue *complex = SafeSyntaxNode<ComplexPropertyValue>(node);
			return complex && complex->GetIndexer();
		}

		// The variable a test leaves in the accumulator: the tested variable,
		// or the target of a tested assignment. Null for anything else.
		static unique_ptr<PropertyValue> CopyOf(SyntaxNode *cond)
		{
			PropertyValueBase *value = AsValue(cond);
			Assignment *assign = SafeSyntaxNode<Assignment>(cond);
			if (value && (value->GetType() == ValueType::Token) && !IsIndexed(cond))
			{
				return make_unique<PropertyValue>(*value);
			}
			if (assign && assign->_variable && !assign->_variable->HasIndexer())
			{
				return make_unique<PropertyValue>(assign->_variable->GetName(), ValueType::Token);
			}
			return nullptr;
		}
	};

	//
	// DoubleNot: (not (not x)) -> x, only where the value is used as a boolean.
	//
	class DoubleNot : public AstPass
	{
	public:
		const char *Name() const override { return "DoubleNot"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			UnaryOp *outer = SafeSyntaxNode<UnaryOp>(slot.get());
			if (!outer || (outer->Operator != UnaryOperator::LogicalNot) || !ctx.IsBooleanContext())
			{
				return RewriteResult::None;
			}
			UnaryOp *inner = SafeSyntaxNode<UnaryOp>(outer->GetStatement1());
			if (!inner || (inner->Operator != UnaryOperator::LogicalNot))
			{
				return RewriteResult::None;
			}
			slot = move(inner->GetStatement1Internal());
			return RewriteResult::Replaced;
		}
	};

	//
	// MathAssignment: (= a (op a b)) -> (op= a b).
	//
	AssignmentOperator BinaryToAssignment(BinaryOperator op)
	{
		switch (op)
		{
		case BinaryOperator::Add: return AssignmentOperator::Add;
		case BinaryOperator::Subtract: return AssignmentOperator::Subtract;
		case BinaryOperator::Multiply: return AssignmentOperator::Multiply;
		case BinaryOperator::Divide: return AssignmentOperator::Divide;
		case BinaryOperator::Mod: return AssignmentOperator::Mod;
		case BinaryOperator::BinaryAnd: return AssignmentOperator::BinaryAnd;
		case BinaryOperator::BinaryOr: return AssignmentOperator::BinaryOr;
		case BinaryOperator::ExclusiveOr: return AssignmentOperator::ExclusiveOr;
		case BinaryOperator::ShiftRight: return AssignmentOperator::ShiftRight;
		case BinaryOperator::ShiftLeft: return AssignmentOperator::ShiftLeft;
		default: return AssignmentOperator::None;
		}
	}

	class MathAssignment : public AstPass
	{
	public:
		const char *Name() const override { return "MathAssignment"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
		{
			Assignment *assign = SafeSyntaxNode<Assignment>(slot.get());
			if (!assign || (assign->Operator != AssignmentOperator::Assign) || !assign->_variable)
			{
				return RewriteResult::None;
			}
			BinaryOp *value = SafeSyntaxNode<BinaryOp>(assign->GetStatement1());
			if (!value)
			{
				return RewriteResult::None;
			}
			AssignmentOperator mapped = BinaryToAssignment(value->Operator);
			if (mapped == AssignmentOperator::None)
			{
				return RewriteResult::None;
			}
			// The first operand must be a read of the assignment target. Only the
			// two-operand form: a longer chain would change the bytecode.
			if (!value->GetStatement1() || !IsSameVariable(*assign->_variable, *value->GetStatement1()))
			{
				return RewriteResult::None;
			}
			assign->Operator = mapped;
			assign->SetStatement1(move(value->GetStatement2Internal()));
			return RewriteResult::Changed;
		}
	};

	//
	// Loop cleanup. These fold the else-break shapes the loop-exit synthesis
	// produces back into loop tests and factored-out breaks.
	//

	// (while a (if b ... else (break))) -> (while (and a b) ...)
	class LoopTestAbsorber : public AstPass
	{
	public:
		const char *Name() const override { return "LoopTestAbsorber"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
		{
			WhileLoop *loop = SafeSyntaxNode<WhileLoop>(slot.get());
			if (!loop || IsTrueCondition(*loop) || !HasConditionExpression(*loop))
			{
				return RewriteResult::None;
			}
			vector<SyntaxNode *> body = MeaningfulStatementsOf(loop->GetStatements());
			if (body.size() != 1)
			{
				return RewriteResult::None;
			}
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(body[0]);
			if (!if_ || !if_->HasElse() || !HasConditionExpression(*if_))
			{
				return RewriteResult::None;
			}
			vector<SyntaxNode *> elseStmts = MeaningfulStatements(if_->GetStatement2());
			if ((elseStmts.size() != 1) || !IsSingleLevelBreak(elseStmts[0]))
			{
				return RewriteResult::None;
			}
			// cond = (and loopcond ifcond)
			unique_ptr<SyntaxNode> loopCond = move(ConditionSlot(*loop));
			unique_ptr<SyntaxNode> ifCond = move(ConditionSlot(*if_));
			SetConditionExpression(*loop, MakeAnd(move(loopCond), move(ifCond), loop));
			// Replace the body with the if's then statements.
			CodeBlock *thenBlock = AsCodeBlock(if_->GetStatement1());
			SyntaxNodeVector newBody;
			if (thenBlock)
			{
				for (unique_ptr<SyntaxNode> &s : thenBlock->GetStatements())
				{
					newBody.push_back(move(s));
				}
			}
			else if (if_->GetStatement1())
			{
				// The parser can leave a single statement bare.
				newBody.push_back(move(if_->GetStatement1Internal()));
			}
			loop->GetStatements().swap(newBody);
			return RewriteResult::Changed;
		}
	};

	// Last statement of both then and else is (break) -> one (break) after the if.
	// Same for continue.
	class BreakContinueFactorOut : public AstPass
	{
	public:
		const char *Name() const override { return "BreakContinueFactorOut"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(slot.get());
			if (!if_ || !if_->HasElse() || !ctx.List())
			{
				return RewriteResult::None;
			}
			CodeBlock *thenB = AsCodeBlock(if_->GetStatement1());
			CodeBlock *elseB = AsCodeBlock(if_->GetStatement2());
			if (!thenB || !elseB || thenB->GetStatements().empty() || elseB->GetStatements().empty())
			{
				return RewriteResult::None;
			}
			SyntaxNode *lastThen = thenB->GetStatements().back().get();
			SyntaxNode *lastElse = elseB->GetStatements().back().get();
			bool isBreak = IsSingleLevelBreak(lastThen) && IsSingleLevelBreak(lastElse);
			bool isContinue = IsSingleLevelContinue(lastThen) && IsSingleLevelContinue(lastElse);
			if (!isBreak && !isContinue)
			{
				return RewriteResult::None;
			}
			if (MeaningfulStatementsOf(thenB->GetStatements()).size() == 1)
			{
				// The then would be empty: (if c (break) else ...) keeps its shape.
				return RewriteResult::None;
			}
			thenB->GetStatements().pop_back();
			elseB->GetStatements().pop_back();
			unique_ptr<SyntaxNode> hoisted = isBreak
				? unique_ptr<SyntaxNode>(new BreakStatement())
				: unique_ptr<SyntaxNode>(new ContinueStatement());
			hoisted->SetPosition(if_->GetPosition());
			ctx.List()->insert(ctx.List()->begin() + ctx.Index() + 1, move(hoisted));
			if (elseB->GetStatements().empty())
			{
				if_->SetStatement2(nullptr);
			}
			return RewriteResult::Changed;
		}
	};

	// An if followed by a (break)/(continue) drops redundant trailing ones of
	// the same kind inside its branches (never emptying the then).
	class IfThenElseBreakContinueTrim : public AstPass
	{
	public:
		const char *Name() const override { return "IfThenElseBreakContinueTrim"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(slot.get());
			if (!if_ || !ctx.List())
			{
				return RewriteResult::None;
			}
			size_t next = ctx.Index() + 1;
			if (next >= ctx.List()->size())
			{
				return RewriteResult::None;
			}
			SyntaxNode *following = (*ctx.List())[next].get();
			bool wantBreak = IsSingleLevelBreak(following);
			bool wantContinue = IsSingleLevelContinue(following);
			if (!wantBreak && !wantContinue)
			{
				return RewriteResult::None;
			}
			bool changed = false;
			Process(if_, wantBreak, changed);
			return changed ? RewriteResult::Changed : RewriteResult::None;
		}

	private:
		void Process(IfStatement *if_, bool wantBreak, bool &changed)
		{
			CodeBlock *thenB = AsCodeBlock(if_->GetStatement1());
			if (thenB)
			{
				while ((MeaningfulStatementsOf(thenB->GetStatements()).size() > 1) && IsMatch(thenB->GetStatements().back().get(), wantBreak))
				{
					thenB->GetStatements().pop_back();
					changed = true;
				}
				if (!thenB->GetStatements().empty())
				{
					IfStatement *nested = SafeSyntaxNode<IfStatement>(thenB->GetStatements().back().get());
					if (nested)
					{
						Process(nested, wantBreak, changed);
					}
				}
			}
			CodeBlock *elseB = AsCodeBlock(if_->GetStatement2());
			if (elseB)
			{
				while (!elseB->GetStatements().empty() && IsMatch(elseB->GetStatements().back().get(), wantBreak))
				{
					elseB->GetStatements().pop_back();
					changed = true;
				}
				if (elseB->GetStatements().empty())
				{
					if_->SetStatement2(nullptr);
					changed = true;
				}
			}
		}
		static bool IsMatch(SyntaxNode *node, bool wantBreak)
		{
			return wantBreak ? IsSingleLevelBreak(node) : IsSingleLevelContinue(node);
		}
	};

	// The statement list of a loop, or null.
	SyntaxNodeVector *LoopBody(SyntaxNode *node)
	{
		switch (node->GetNodeType())
		{
		case NodeTypeWhileLoop:
			return &SafeSyntaxNode<WhileLoop>(node)->GetStatements();
		case NodeTypeDoLoop:
			return &SafeSyntaxNode<DoLoop>(node)->GetStatements();
		case NodeTypeForLoop:
			return &SafeSyntaxNode<ForLoop>(node)->GetStatements();
		default:
			return nullptr;
		}
	}

	// A continue at the end of a loop body is redundant, also at the end of
	// the last if's branches there. Delete it, and an else it empties.
	class ContinueTrim : public AstPass
	{
	public:
		const char *Name() const override { return "ContinueTrim"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
		{
			SyntaxNodeVector *body = LoopBody(slot.get());
			if (!body)
			{
				return RewriteResult::None;
			}
			bool changed = false;
			Trim(*body, changed);
			return changed ? RewriteResult::Changed : RewriteResult::None;
		}

	private:
		static void Trim(SyntaxNodeVector &list, bool &changed, bool isThen = false)
		{
			for (;;)
			{
				size_t index = list.size();
				while ((index > 0) && SafeSyntaxNode<Comment>(list[index - 1].get()))
				{
					index--;
				}
				if (index == 0)
				{
					return;
				}
				SyntaxNode *last = list[index - 1].get();
				if (IsSingleLevelContinue(last))
				{
					if (isThen && (MeaningfulStatementsOf(list).size() == 1))
					{
						// The then would be empty: (if c (continue)) keeps its shape.
						return;
					}
					list.erase(list.begin() + index - 1);
					changed = true;
					continue;
				}
				IfStatement *if_ = SafeSyntaxNode<IfStatement>(last);
				if (if_)
				{
					CodeBlock *thenB = AsCodeBlock(if_->GetStatement1());
					if (thenB)
					{
						Trim(thenB->GetStatements(), changed, true);
					}
					CodeBlock *elseB = AsCodeBlock(if_->GetStatement2());
					if (elseB)
					{
						Trim(elseB->GetStatements(), changed);
						if (IsEmptyBlock(elseB))
						{
							if_->SetStatement2(nullptr);
							changed = true;
						}
					}
				}
				return;
			}
		}
	};

	// In a loop body, an if with no else whose then ends by leaving (a
	// continue, a break, or a return), followed by more of the body: the rest
	// of the body becomes the else, and a continue at the end of the then is
	// dropped. The then is then treated the same way. This gives the
	// if-else-if chains the golden text has for loops that end in a cond.
	class IfContinueRefactor : public AstPass
	{
	public:
		const char *Name() const override { return "IfContinueRefactor"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			bool changed = false;
			SyntaxNodeVector *body = LoopBody(slot.get());
			if (body)
			{
				Process(*body, true, changed);
			}
			else
			{
				// A block anywhere inside a loop: the break and return forms
				// (the golden text has them at any depth; a continue only at
				// the body level).
				CodeBlock *block = AsCodeBlock(slot.get());
				if (block && InsideLoop(ctx))
				{
					Process(block->GetStatements(), false, changed);
				}
			}
			return changed ? RewriteResult::Changed : RewriteResult::None;
		}

	private:
		static bool InsideLoop(const AstContext &ctx)
		{
			for (size_t i = ctx._frames.size(); i > 0; i--)
			{
				NodeType t = ctx._frames[i - 1].node->GetNodeType();
				if ((t == NodeTypeWhileLoop) || (t == NodeTypeDoLoop) || (t == NodeTypeForLoop))
				{
					return true;
				}
				if (t == NodeTypeFunction)
				{
					return false;
				}
			}
			return false;
		}

		static void Process(SyntaxNodeVector &list, bool allowContinue, bool &changed)
		{
			for (size_t i = list.size(); i > 1; i--)
			{
				size_t ifIndex = i - 2;
				IfStatement *if_ = SafeSyntaxNode<IfStatement>(list[ifIndex].get());
				if (!if_ || if_->HasElse())
				{
					continue;
				}
				CodeBlock *thenB = AsCodeBlock(if_->GetStatement1());
				if (!thenB)
				{
					continue;
				}
				vector<SyntaxNode *> thenStmts = MeaningfulStatements(thenB);
				if (thenStmts.empty())
				{
					continue;
				}
				SyntaxNode *last = thenStmts.back();
				bool leaves = (allowContinue && IsSingleLevelContinue(last)) || IsSingleLevelBreak(last) || (last->GetNodeType() == NodeTypeReturn);
				if (!leaves)
				{
					continue;
				}
				// Nothing meaningful after the if: nothing to move.
				bool restMeaningful = false;
				for (size_t j = ifIndex + 1; j < list.size(); j++)
				{
					if (!SafeSyntaxNode<Comment>(list[j].get()))
					{
						restMeaningful = true;
					}
				}
				if (!restMeaningful)
				{
					continue;
				}
				if (IsSingleLevelContinue(last))
				{
					SyntaxNodeVector &thenList = thenB->GetStatements();
					for (size_t k = thenList.size(); k > 0; k--)
					{
						if (thenList[k - 1].get() == last)
						{
							thenList.erase(thenList.begin() + k - 1);
							break;
						}
					}
				}
				// Empty comment nodes (dead jumps) stay out of the else, or the
				// printed shape depends on them.
				unique_ptr<CodeBlock> elseB = make_unique<CodeBlock>();
				for (size_t j = ifIndex + 1; j < list.size(); j++)
				{
					if (!SafeSyntaxNode<Comment>(list[j].get()))
					{
						elseB->AddStatement(move(list[j]));
					}
				}
				list.erase(list.begin() + ifIndex + 1, list.end());
				if_->SetStatement2(move(elseB));
				changed = true;
				Process(thenB->GetStatements(), allowContinue, changed);
			}
		}
	};

	// Delete a break/continue that immediately follows a return, break, or
	// continue (it can never run).
	class FinalBreakContinueTrim : public AstPass
	{
	public:
		const char *Name() const override { return "FinalBreakContinueTrim"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			NodeType t = slot->GetNodeType();
			if ((t != NodeTypeReturn) && (t != NodeTypeBreak) && (t != NodeTypeContinue))
			{
				return RewriteResult::None;
			}
			if (!ctx.List())
			{
				return RewriteResult::None;
			}
			bool changed = false;
			size_t next = ctx.Index() + 1;
			while (next < ctx.List()->size())
			{
				SyntaxNode *n = (*ctx.List())[next].get();
				if (IsSingleLevelBreak(n) || IsSingleLevelContinue(n))
				{
					ctx.List()->erase(ctx.List()->begin() + next);
					changed = true;
				}
				else if (SafeSyntaxNode<Comment>(n))
				{
					// An empty comment (a dead jump after the return) is not code.
					next++;
				}
				else
				{
					break;
				}
			}
			return changed ? RewriteResult::Changed : RewriteResult::None;
		}
	};

	//
	// ReturnCleanup: the shape of return values.
	//
	// A function returns whatever the accumulator holds, so a ret after a
	// statement is ambiguous: the source may or may not have returned the
	// statement. These rules follow the golden decompilations (sluicebox's
	// ReturnCleaner):
	//  - (return S) where S is a loop, or an if/switch with a return inside,
	//    becomes S followed by a bare (return). Source never returns such a
	//    statement; a branch left its value in the accumulator.
	//  - A statement that a bare (return) follows (the function's final ret
	//    follows the last statement of the body) is absorbed into the return
	//    when it looks like a value: a number, a variable, a string, a
	//    comparison, math, or an if/switch whose branches end in one. When
	//    the function returns a value elsewhere, any statement is absorbed.
	//    A loop, control flow, or a structure with a return inside never is.
	//  - A bare (return) at the end of the function body is deleted.
	// Functions named onMe/onTarget always return a value. handleEvent,
	// changeState and init absorb only unmistakable values (a comparison,
	// math, an indexed variable): stray values are common there.
	//

	bool IsLoop(const SyntaxNode *node)
	{
		if (!node)
		{
			return false;
		}
		NodeType t = node->GetNodeType();
		return (t == NodeTypeWhileLoop) || (t == NodeTypeDoLoop) || (t == NodeTypeForLoop);
	}

	// The last meaningful statement of a list, and the one before it.
	SyntaxNode *LastOf(const SyntaxNodeVector &list, SyntaxNode **prev)
	{
		SyntaxNode *last = nullptr;
		*prev = nullptr;
		for (const unique_ptr<SyntaxNode> &s : list)
		{
			if (!SafeSyntaxNode<Comment>(s.get()))
			{
				*prev = last;
				last = s.get();
			}
		}
		return last;
	}

	// The same for an if branch: a code block, or a bare statement.
	SyntaxNode *LastOfBranch(SyntaxNode *branch, SyntaxNode **prev)
	{
		CodeBlock *cb = AsCodeBlock(branch);
		if (cb)
		{
			return LastOf(cb->GetStatements(), prev);
		}
		*prev = nullptr;
		return (branch && !SafeSyntaxNode<Comment>(branch)) ? branch : nullptr;
	}

	// True if a branch of the node ends in a return statement.
	bool EndsInReturn(SyntaxNode *node)
	{
		if (!node)
		{
			return false;
		}
		SyntaxNode *prev;
		switch (node->GetNodeType())
		{
		case NodeTypeReturn:
			return true;
		case NodeTypeCodeBlock:
			return EndsInReturn(LastOf(AsCodeBlock(node)->GetStatements(), &prev));
		case NodeTypeIf:
		{
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(node);
			return EndsInReturn(LastOfBranch(if_->GetStatement1(), &prev)) ||
				EndsInReturn(LastOfBranch(if_->GetStatement2(), &prev));
		}
		case NodeTypeSwitch:
		{
			SwitchStatement *sw = SafeSyntaxNode<SwitchStatement>(node);
			for (const unique_ptr<CaseStatement> &c : sw->_cases)
			{
				if (EndsInReturn(LastOf(c->GetCodeSegments(), &prev)))
				{
					return true;
				}
			}
			return false;
		}
		default:
			return false;
		}
	}

	// A value that is pointless as a statement, so it must be a return value:
	// an indexed variable, an address-of, a not or negation, math, a
	// comparison, an and/or.
	bool IsUnmistakableValue(SyntaxNode *node)
	{
		if (!node)
		{
			return false;
		}
		switch (node->GetNodeType())
		{
		case NodeTypeBinaryOperation:
		case NodeTypeNaryOperation:
			return true;
		case NodeTypeUnaryOperation:
		{
			UnaryOperator op = SafeSyntaxNode<UnaryOp>(node)->Operator;
			return (op == UnaryOperator::LogicalNot) || (op == UnaryOperator::BinaryNot) || (op == UnaryOperator::Negate);
		}
		case NodeTypeValue:
		case NodeTypeComplexValue:
		{
			ComplexPropertyValue *indexed = SafeSyntaxNode<ComplexPropertyValue>(node);
			return (indexed && indexed->GetIndexer()) || (AsValue(node)->GetType() == ValueType::Pointer);
		}
		default:
			return false;
		}
	}

	// An if whose else is a single if: an if-else-if chain (a cond).
	bool IsIfChain(IfStatement *if_)
	{
		SyntaxNode *prev;
		SyntaxNode *elseLast = LastOfBranch(if_->GetStatement2(), &prev);
		return elseLast && !prev && (elseLast->GetNodeType() == NodeTypeIf);
	}

	bool LooksLikeValue(SyntaxNode *node, bool noZero, SyntaxNode *prev);

	bool BranchLooksLikeValue(SyntaxNode *branch, bool noZero)
	{
		SyntaxNode *prev;
		SyntaxNode *last = LastOfBranch(branch, &prev);
		return LooksLikeValue(last, noZero, prev);
	}

	// True if the node looks like a value the function returns. noZero: a 0
	// does not count (the cases of a cond or switch end in stray zeros).
	// prev: the statement before it; a number after another number is a stray
	// value too.
	bool LooksLikeValue(SyntaxNode *node, bool noZero, SyntaxNode *prev)
	{
		if (!node)
		{
			return false;
		}
		if (IsUnmistakableValue(node))
		{
			return true;
		}
		switch (node->GetNodeType())
		{
		case NodeTypeValue:
		case NodeTypeComplexValue:
		{
			PropertyValueBase *value = AsValue(node);
			if (value->GetType() == ValueType::Number)
			{
				if (noZero && (value->GetNumberValue() == 0))
				{
					return false;
				}
				PropertyValueBase *before = AsValue(prev);
				return !(before && (before->GetType() == ValueType::Number));
			}
			return value->GetType() != ValueType::None;
		}
		case NodeTypeCodeBlock:
		{
			SyntaxNode *before;
			SyntaxNode *last = LastOf(AsCodeBlock(node)->GetStatements(), &before);
			return LooksLikeValue(last, noZero, before);
		}
		case NodeTypeIf:
		{
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(node);
			bool chainNoZero = noZero || IsIfChain(if_);
			return BranchLooksLikeValue(if_->GetStatement1(), chainNoZero) ||
				BranchLooksLikeValue(if_->GetStatement2(), chainNoZero);
		}
		case NodeTypeSwitch:
		{
			SwitchStatement *sw = SafeSyntaxNode<SwitchStatement>(node);
			for (const unique_ptr<CaseStatement> &c : sw->_cases)
			{
				SyntaxNode *before;
				SyntaxNode *last = LastOf(c->GetCodeSegments(), &before);
				if (LooksLikeValue(last, true, before))
				{
					return true;
				}
			}
			return false;
		}
		default:
			return false;
		}
	}

	// The index of the next meaningful statement after index, or the size.
	size_t NextMeaningful(const SyntaxNodeVector &list, size_t index)
	{
		size_t next = index + 1;
		while ((next < list.size()) && SafeSyntaxNode<Comment>(list[next].get()))
		{
			next++;
		}
		return next;
	}

	bool IsBareReturn(SyntaxNode *node)
	{
		ReturnStatement *ret = SafeSyntaxNode<ReturnStatement>(node);
		return ret && !ret->GetStatement1();
	}

	// Records whether the function has a return with a value.
	class ReturnValueScan : public AstPass
	{
	public:
		bool found = false;
		const char *Name() const override { return "ReturnValueScan"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
		{
			ReturnStatement *ret = SafeSyntaxNode<ReturnStatement>(slot.get());
			if (ret && ret->GetStatement1())
			{
				found = true;
			}
			return RewriteResult::None;
		}
	};

	class ReturnCleanup : public AstPass
	{
	public:
		ReturnCleanup(const string &functionName, bool returnsValue)
			: _returnsValue(returnsValue || (functionName == "onMe") || (functionName == "onTarget")),
			_cautious((functionName == "handleEvent") || (functionName == "changeState") || (functionName == "init")) {}
		const char *Name() const override { return "ReturnCleanup"; }

		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &ctx) override
		{
			SlotKind kind = ctx.Kind();
			if ((kind != SlotKind::Statement) && (kind != SlotKind::IfThen) && (kind != SlotKind::IfElse))
			{
				return RewriteResult::None;
			}
			ReturnStatement *ret = SafeSyntaxNode<ReturnStatement>(slot.get());
			if (ret)
			{
				return ret->GetStatement1() ? Unwrap(slot, ctx) : DropFinal(ctx);
			}
			return Absorb(slot, ctx);
		}

	private:
		bool _returnsValue;   // a ret in this function returns a value, so every ret does
		bool _cautious;       // absorb only unmistakable values

		// True when nothing meaningful follows the slot in the function body:
		// it is last in its list, and so is every plain code block above it,
		// up to the function (the root frame, see RunPassOnce).
		static bool AtFunctionEnd(const AstContext &ctx)
		{
			for (size_t i = ctx._frames.size(); i > 1; i--)
			{
				const AstContext::Frame &frame = ctx._frames[i - 1];
				if ((frame.kind != SlotKind::Statement) || !frame.list ||
					(NextMeaningful(*frame.list, frame.index) != frame.list->size()))
				{
					return false;
				}
				NodeType parent = ctx._frames[i - 2].node->GetNodeType();
				if ((parent != NodeTypeCodeBlock) && (parent != NodeTypeFunction))
				{
					return false;
				}
			}
			return ctx._frames.size() > 1;
		}

		// A send, a call, or an assignment: a statement whose value a cautious
		// function does not return (the golden decompilations never do).
		static bool IsSideEffectStatement(const SyntaxNode *node)
		{
			NodeType t = node->GetNodeType();
			return (t == NodeTypeSendCall) || (t == NodeTypeProcedureCall) || (t == NodeTypeAssignment);
		}

		// (return S) with a loop or a return inside S, or a side-effect
		// statement in a cautious function: S, then a bare return.
		RewriteResult Unwrap(unique_ptr<SyntaxNode> &slot, const AstContext &ctx)
		{
			ReturnStatement *ret = static_cast<ReturnStatement *>(slot.get());
			SyntaxNode *value = ret->GetStatement1();
			if (!IsLoop(value) && !EndsInReturn(value) && !(_cautious && IsSideEffectStatement(value)))
			{
				return RewriteResult::None;
			}
			unique_ptr<SyntaxNode> statement = move(ret->GetStatement1Internal());
			unique_ptr<SyntaxNode> bare = make_unique<ReturnStatement>();
			if (ctx.List())
			{
				slot = move(statement);
				ctx.List()->insert(ctx.List()->begin() + ctx.Index() + 1, move(bare));
			}
			else
			{
				// A bare if branch (parsed source). Make it a block.
				unique_ptr<CodeBlock> block = make_unique<CodeBlock>();
				block->AddStatement(move(statement));
				block->AddStatement(move(bare));
				slot = move(block);
			}
			return RewriteResult::Replaced;
		}

		static RewriteResult DropFinal(const AstContext &ctx)
		{
			return AtFunctionEnd(ctx) ? RewriteResult::Removed : RewriteResult::None;
		}

		// A statement that a bare return follows becomes the return value.
		RewriteResult Absorb(unique_ptr<SyntaxNode> &slot, const AstContext &ctx)
		{
			if (!ctx.List())
			{
				return RewriteResult::None;
			}
			SyntaxNodeVector &list = *ctx.List();
			size_t index = ctx.Index();
			size_t next = NextMeaningful(list, index);
			bool bareReturnFollows = (next < list.size()) && IsBareReturn(list[next].get());
			if (!bareReturnFollows && !AtFunctionEnd(ctx))
			{
				return RewriteResult::None;
			}
			// The value's tail: through code blocks (parsed source can hold
			// one at statement level), the last meaningful statement.
			SyntaxNode *value = slot.get();
			SyntaxNode *tail = value;
			while (tail && AsCodeBlock(tail))
			{
				SyntaxNode *prevInBlock;
				tail = LastOf(AsCodeBlock(tail)->GetStatements(), &prevInBlock);
			}
			if (!tail || SafeSyntaxNode<Comment>(tail) || IsControlFlow(tail) || IsLoop(tail) || EndsInReturn(value))
			{
				return RewriteResult::None;
			}
			SyntaxNode *prev = (index > 0) ? list[index - 1].get() : nullptr;
			bool absorb = _cautious ? IsUnmistakableValue(value) : (_returnsValue || LooksLikeValue(value, false, prev));
			if (!absorb)
			{
				return RewriteResult::None;
			}
			unique_ptr<ReturnStatement> ret = make_unique<ReturnStatement>();
			ret->SetStatement1(move(slot));
			slot = move(ret);
			if (bareReturnFollows)
			{
				list.erase(list.begin() + next);
			}
			_returnsValue = true;
			return RewriteResult::Replaced;
		}
	};
}

void RunDecompilerAstPasses(FunctionBase &func, const AstPassOptions &options, IDecompilerResults *results)
{
	// Loop cleanup first, so break/continue shapes settle before conditions are
	// folded. Only functions with a loop can have these shapes.
	if (FunctionHasLoop(func))
	{
		LoopTestAbsorber absorber;
		BreakContinueFactorOut factorOut;
		IfThenElseBreakContinueTrim trim;
		ContinueTrim continueTrim;
		IfContinueRefactor refactor;
		FinalBreakContinueTrim finalTrim;
		vector<AstPass *> loopPasses = { &absorber, &factorOut, &trim, &continueTrim, &refactor, &finalTrim };
		RunPassesToFixpoint(func, loopPasses, options.maxSweeps, results);
	}

	// Fold nested and value-position ifs into and/or, and collapse double nots.
	// An n-ary comparison is built at instruction consumption from its pprev,
	// so no pass folds one from two comparisons. A copied then value comes
	// first, so (if a a else b) stays an if and does not become (or a b).
	{
		CopyValue copyValue;
		IfThenToAnd ifThenToAnd;
		DoubleNot doubleNot;
		vector<AstPass *> condPasses = { &copyValue, &ifThenToAnd, &doubleNot };
		RunPassesToFixpoint(func, condPasses, options.maxSweeps, results);
	}

	// (= a (op a b)) -> (op= a b).
	{
		MathAssignment mathAssignment;
		RunPassOnce(func, mathAssignment);
	}

	// Return values take the golden shape (see ReturnCleanup). Last, as it
	// reads the settled statement shapes.
	{
		ReturnValueScan scan;
		RunPassOnce(func, scan);
		ReturnCleanup returnCleanup(func.GetName(), scan.found);
		vector<AstPass *> returnPasses = { &returnCleanup };
		RunPassesToFixpoint(func, returnPasses, options.maxSweeps, results);
	}
}
