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
			if (!if_->HasElse())
			{
				vector<SyntaxNode *> thenStmts = MeaningfulStatements(if_->GetStatement1());
				if (thenStmts.size() == 1)
				{
					IfStatement *inner = SafeSyntaxNode<IfStatement>(thenStmts[0]);
					if (inner && !inner->HasElse())
					{
						unique_ptr<SyntaxNode> outerCond = move(ConditionSlot(*if_));
						unique_ptr<SyntaxNode> innerCond = move(ConditionSlot(*inner));
						SetConditionExpression(*if_, MakeAnd(move(outerCond), move(innerCond), if_));
						if_->SetStatement1(move(inner->GetStatement1Internal()));
						return RewriteResult::Changed;
					}
				}
			}

			// Value-position rules.
			if (IsValueSlot(ctx.Kind()))
			{
				if (!if_->HasElse())
				{
					// (if A B) -> (and A B)
					vector<SyntaxNode *> thenStmts = MeaningfulStatements(if_->GetStatement1());
					if ((thenStmts.size() == 1) && !IsControlFlow(thenStmts[0]))
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
						if ((elseStmts.size() == 1) && !IsControlFlow(elseStmts[0]))
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

	// A pure read that can be evaluated twice with the same result, so the two
	// halves of a chained comparison may share it. A send or call is excluded.
	bool IsSideEffectFreeOperand(const SyntaxNode *node)
	{
		if (!node)
		{
			return false;
		}
		switch (node->GetNodeType())
		{
		case NodeTypeValue:
			return true;
		case NodeTypeLValue:
			return IsSideEffectFreeOperand(static_cast<const LValue *>(node)->GetIndexer()) ||
				!static_cast<const LValue *>(node)->GetIndexer();
		case NodeTypeComplexValue:
			return IsSideEffectFreeOperand(static_cast<const ComplexPropertyValue *>(node)->GetIndexer()) ||
				!static_cast<const ComplexPropertyValue *>(node)->GetIndexer();
		default:
			return false;
		}
	}

	// (and (< a b) (< b c)) -> (< a b c), and the same for a longer chain. The
	// shared middle must be identical and side-effect-free, so evaluating it
	// once (as the n-ary form does) matches evaluating it on each side.
	// Sierra's compiler produced the n-ary form with a pprev; the decompiler
	// currently clones the middle into two comparisons, which this restores.
	class ChainedComparison : public AstPass
	{
	public:
		const char *Name() const override { return "ChainedComparison"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
		{
			BinaryOp *andOp = SafeSyntaxNode<BinaryOp>(slot.get());
			if (!andOp || (andOp->Operator != BinaryOperator::LogicalAnd))
			{
				return RewriteResult::None;
			}
			BinaryOp *right = SafeSyntaxNode<BinaryOp>(andOp->GetStatement2());
			if (!right || !IsRelational(right->Operator))
			{
				return RewriteResult::None;
			}

			// The left side is either a comparison (a two-term chain) or an
			// n-ary comparison already folded from an inner and (a longer chain).
			BinaryOp *leftCompare = SafeSyntaxNode<BinaryOp>(andOp->GetStatement1());
			NaryOp *leftNary = SafeSyntaxNode<NaryOp>(andOp->GetStatement1());

			if (leftCompare && IsRelational(leftCompare->Operator) &&
				(leftCompare->Operator == right->Operator) &&
				IsSideEffectFreeOperand(leftCompare->GetStatement2()) &&
				StructEqual(leftCompare->GetStatement2(), right->GetStatement1()))
			{
				unique_ptr<NaryOp> nary = make_unique<NaryOp>();
				nary->Operator = right->Operator;
				nary->SetPosition(andOp->GetPosition());
				nary->GetStatements().push_back(move(leftCompare->GetStatement1Internal()));
				nary->GetStatements().push_back(move(leftCompare->GetStatement2Internal()));
				nary->GetStatements().push_back(move(right->GetStatement2Internal()));
				slot = move(nary);
				return RewriteResult::Replaced;
			}

			if (leftNary && (leftNary->Operator == right->Operator) && !leftNary->GetStatements().empty())
			{
				SyntaxNode *lastTerm = leftNary->GetStatements().back().get();
				if (IsSideEffectFreeOperand(lastTerm) && StructEqual(lastTerm, right->GetStatement1()))
				{
					unique_ptr<NaryOp> nary(static_cast<NaryOp *>(andOp->GetStatement1Internal().release()));
					nary->GetStatements().push_back(move(right->GetStatement2Internal()));
					slot = move(nary);
					return RewriteResult::Replaced;
				}
			}

			return RewriteResult::None;
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
			if (!loop || IsTrueCondition(*loop))
			{
				return RewriteResult::None;
			}
			vector<SyntaxNode *> body = MeaningfulStatements2(loop->GetStatements());
			if (body.size() != 1)
			{
				return RewriteResult::None;
			}
			IfStatement *if_ = SafeSyntaxNode<IfStatement>(body[0]);
			if (!if_ || !if_->HasElse())
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
			loop->GetStatements().swap(newBody);
			return RewriteResult::Changed;
		}

	private:
		static vector<SyntaxNode *> MeaningfulStatements2(const SyntaxNodeVector &list)
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
				while (thenB->GetStatements().size() > 1 && IsMatch(thenB->GetStatements().back().get(), wantBreak))
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
		FinalBreakContinueTrim finalTrim;
		vector<AstPass *> loopPasses = { &absorber, &factorOut, &trim, &finalTrim };
		RunPassesToFixpoint(func, loopPasses, options.maxSweeps, results);
	}

	// Fold nested and value-position ifs into and/or, collapse double nots, and
	// fold a chained comparison's two halves into one n-ary comparison.
	{
		IfThenToAnd ifThenToAnd;
		DoubleNot doubleNot;
		ChainedComparison chainedComparison;
		vector<AstPass *> condPasses = { &ifThenToAnd, &doubleNot, &chainedComparison };
		RunPassesToFixpoint(func, condPasses, options.maxSweeps, results);
	}

	// (= a (op a b)) -> (op= a b).
	{
		MathAssignment mathAssignment;
		RunPassOnce(func, mathAssignment);
	}
}
