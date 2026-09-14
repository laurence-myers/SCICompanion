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
#include "AstRewrite.h"
#include "ScriptOMAll.h"
#include "Operators.h"
#include "DecompilerResults.h"
#include "format.h"

using namespace sci;
using namespace std;

//
// Context
//

bool AstContext::IsBooleanContext() const
{
	if (_frames.empty())
	{
		return false;
	}
	size_t i = _frames.size() - 1;
	while (_frames[i].kind == SlotKind::AndOrOperand)
	{
		if (i == 0)
		{
			return false;
		}
		i--;
	}
	SlotKind k = _frames[i].kind;
	return (k == SlotKind::IfCondition) || (k == SlotKind::WhileCondition) ||
		(k == SlotKind::DoCondition) || (k == SlotKind::NotOperand);
}

//
// The walker
//

namespace
{
	class Walker
	{
	public:
		Walker(AstPass &pass, AstContext &ctx) : _pass(pass), _ctx(ctx) {}
		bool Changed() const { return _changed; }

		void VisitFunctionBody(FunctionBase &func)
		{
			VisitList(func.GetStatements(), SlotKind::Statement);
		}

	private:
		AstPass &_pass;
		AstContext &_ctx;
		bool _changed = false;

		// Applies the pass to a fixed (non-list) slot after its subtree has been
		// visited. Re-scans and re-applies when the pass replaces the node.
		RewriteResult ApplyFixed(unique_ptr<SyntaxNode> &slot)
		{
			int guard = 0;
			RewriteResult r;
			for (;;)
			{
				r = _pass.Rewrite(slot, _ctx);
				if (r == RewriteResult::Removed)
				{
					// Only a statement in a list can be removed; a fixed slot
					// keeps its node, so nothing changed.
					assert(false && "AST pass removed a fixed slot");
					r = RewriteResult::None;
				}
				if (r != RewriteResult::None)
				{
					_changed = true;
				}
				if (r == RewriteResult::Replaced)
				{
					if (++guard > 8)
					{
						assert(false && "AST pass kept replacing the same slot");
						break;
					}
					_ctx._frames.back().node = slot.get();
					VisitChildren(*slot);
					continue;
				}
				break;
			}
			return r;
		}

		void VisitFixed(unique_ptr<SyntaxNode> &slot, SlotKind kind)
		{
			if (!slot)
			{
				return;
			}
			_ctx._frames.push_back({ slot.get(), kind, nullptr, 0 });
			VisitChildren(*slot);
			ApplyFixed(slot);
			_ctx._frames.pop_back();
		}

		void VisitList(SyntaxNodeVector &list, SlotKind kind)
		{
			for (size_t i = 0; i < list.size(); )
			{
				_ctx._frames.push_back({ list[i].get(), kind, &list, i });
				VisitChildren(*list[i]);
				// Apply, re-fetching list[i] each call: a pass may insert into the
				// list and reallocate it.
				int guard = 0;
				RewriteResult r;
				for (;;)
				{
					r = _pass.Rewrite(list[i], _ctx);
					if (r != RewriteResult::None)
					{
						_changed = true;
					}
					if (r == RewriteResult::Replaced)
					{
						if (++guard > 8)
						{
							assert(false && "AST pass kept replacing the same slot");
							break;
						}
						_ctx._frames.back().node = list[i].get();
						VisitChildren(*list[i]);
						continue;
					}
					break;
				}
				_ctx._frames.pop_back();
				if (r == RewriteResult::Removed)
				{
					list.erase(list.begin() + i);
				}
				else
				{
					i++;
				}
			}
		}

		// Recurses into a typed child that a pass never replaces (a send param, a
		// case, an assignment target). Its own children may still be rewritten.
		void VisitTyped(SyntaxNode *node, SlotKind kind)
		{
			if (!node)
			{
				return;
			}
			_ctx._frames.push_back({ node, kind, nullptr, 0 });
			VisitChildren(*node);
			_ctx._frames.pop_back();
		}

		static bool HasCondition(ConditionNode &owner)
		{
			return owner.GetCondition() && !owner.GetCondition()->GetStatements().empty();
		}

		void VisitConditionOf(ConditionNode &owner, SlotKind kind)
		{
			if (HasCondition(owner))
			{
				VisitFixed(ConditionSlot(owner), kind);
			}
		}

		void VisitChildren(SyntaxNode &node)
		{
			switch (node.GetNodeType())
			{
			case NodeTypeCodeBlock:
			{
				CodeBlock *cb = SafeSyntaxNode<CodeBlock>(&node);
				VisitList(cb->GetStatements(), SlotKind::Statement);
				break;
			}
			case NodeTypeIf:
			{
				IfStatement *if_ = SafeSyntaxNode<IfStatement>(&node);
				VisitConditionOf(*if_, SlotKind::IfCondition);
				VisitFixed(if_->GetStatement1Internal(), SlotKind::IfThen);
				VisitFixed(if_->GetStatement2Internal(), SlotKind::IfElse);
				break;
			}
			case NodeTypeWhileLoop:
			{
				WhileLoop *loop = SafeSyntaxNode<WhileLoop>(&node);
				VisitConditionOf(*loop, SlotKind::WhileCondition);
				VisitList(loop->GetStatements(), SlotKind::Statement);
				break;
			}
			case NodeTypeDoLoop:
			{
				DoLoop *loop = SafeSyntaxNode<DoLoop>(&node);
				VisitConditionOf(*loop, SlotKind::DoCondition);
				VisitList(loop->GetStatements(), SlotKind::Statement);
				break;
			}
			// A for and a cond come from parsed source (the decompiler emits
			// neither); the structural compare walks them.
			case NodeTypeForLoop:
			{
				ForLoop *loop = SafeSyntaxNode<ForLoop>(&node);
				if (loop->GetInitializer())
				{
					VisitList(loop->GetInitializer()->GetStatements(), SlotKind::Statement);
				}
				VisitConditionOf(*loop, SlotKind::WhileCondition);
				VisitList(loop->GetStatements(), SlotKind::Statement);
				if (loop->_looper)
				{
					VisitList(loop->_looper->GetStatements(), SlotKind::Statement);
				}
				break;
			}
			case NodeTypeCond:
			{
				CondStatement *cond = SafeSyntaxNode<CondStatement>(&node);
				VisitFixed(cond->GetStatement1Internal(), SlotKind::Statement);
				break;
			}
			case NodeTypeBinaryOperation:
			{
				BinaryOp *op = SafeSyntaxNode<BinaryOp>(&node);
				SlotKind kind = ((op->Operator == BinaryOperator::LogicalAnd) ||
					(op->Operator == BinaryOperator::LogicalOr)) ? SlotKind::AndOrOperand : SlotKind::Operand;
				VisitFixed(op->GetStatement1Internal(), kind);
				VisitFixed(op->GetStatement2Internal(), kind);
				break;
			}
			case NodeTypeUnaryOperation:
			{
				UnaryOp *op = SafeSyntaxNode<UnaryOp>(&node);
				SlotKind kind = (op->Operator == UnaryOperator::LogicalNot) ? SlotKind::NotOperand : SlotKind::Operand;
				VisitFixed(op->GetStatement1Internal(), kind);
				break;
			}
			case NodeTypeNaryOperation:
			{
				NaryOp *op = SafeSyntaxNode<NaryOp>(&node);
				VisitList(op->GetStatements(), SlotKind::Operand);
				break;
			}
			case NodeTypeReturn:
			{
				ReturnStatement *ret = SafeSyntaxNode<ReturnStatement>(&node);
				VisitFixed(ret->GetStatement1Internal(), SlotKind::ReturnValue);
				break;
			}
			case NodeTypeAssignment:
			{
				Assignment *assign = SafeSyntaxNode<Assignment>(&node);
				VisitTyped(assign->_variable.get(), SlotKind::AssignTarget);
				VisitFixed(assign->GetStatement1Internal(), SlotKind::AssignValue);
				break;
			}
			case NodeTypeProcedureCall:
			{
				ProcedureCall *call = SafeSyntaxNode<ProcedureCall>(&node);
				VisitList(call->GetStatements(), SlotKind::ProcArg);
				break;
			}
			case NodeTypeSendCall:
			{
				SendCall *send = SafeSyntaxNode<SendCall>(&node);
				for (const unique_ptr<SendParam> &param : send->GetParams())
				{
					VisitTyped(param.get(), SlotKind::SendParamNode);
				}
				if (send->GetTargetName().empty())
				{
					if (!send->_object3 || send->_object3->GetName().empty())
					{
						VisitFixed(send->GetStatement1Internal(), SlotKind::SendTarget);
					}
					else
					{
						VisitTyped(send->_object3.get(), SlotKind::SendTarget);
					}
				}
				break;
			}
			case NodeTypeSendParam:
			{
				SendParam *param = SafeSyntaxNode<SendParam>(&node);
				VisitList(param->GetStatements(), SlotKind::SendArg);
				break;
			}
			case NodeTypeSwitch:
			{
				SwitchStatement *sw = SafeSyntaxNode<SwitchStatement>(&node);
				VisitFixed(sw->GetStatement1Internal(), SlotKind::SwitchValue);
				for (const unique_ptr<CaseStatement> &c : sw->_cases)
				{
					VisitTyped(c.get(), SlotKind::CaseNode);
				}
				break;
			}
			case NodeTypeCase:
			{
				CaseStatement *c = SafeSyntaxNode<CaseStatement>(&node);
				if (!c->IsDefault())
				{
					VisitFixed(c->GetStatement1Internal(), SlotKind::CaseValue);
				}
				VisitList(c->GetCodeSegments(), SlotKind::Statement);
				break;
			}
			case NodeTypeCast:
			{
				Cast *cast = SafeSyntaxNode<Cast>(&node);
				VisitFixed(cast->GetStatement1Internal(), SlotKind::CastValue);
				break;
			}
			default:
				// A leaf for rewriting: values, lvalues, rest, break, continue,
				// comments, asm, and declarations. Their children (if any) hold
				// nothing a pass rewrites.
				break;
			}
		}
	};
}

bool RunPassOnce(FunctionBase &func, AstPass &pass)
{
	AstContext ctx;
	Walker walker(pass, ctx);
	// A frame for the function itself, so its statements' Parent(0) is the function.
	ctx._frames.push_back({ &func, SlotKind::Statement, nullptr, 0 });
	walker.VisitFunctionBody(func);
	ctx._frames.pop_back();
	return walker.Changed();
}

void RunPassesToFixpoint(FunctionBase &func, const vector<AstPass *> &passes,
	int maxSweeps, IDecompilerResults *results)
{
	int sweeps = 0;
	bool changed = true;
	while (changed)
	{
		changed = false;
		std::string stillChanging;
		for (AstPass *pass : passes)
		{
			if (RunPassOnce(func, *pass))
			{
				changed = true;
				stillChanging += std::string(" ") + pass->Name();
			}
		}
		if (++sweeps >= maxSweeps)
		{
			if (changed && results)
			{
				results->AddResult(DecompilerResultType::Warning,
					fmt::format("AST passes did not converge in {0}:{1}", func.GetName(), stillChanging));
			}
			break;
		}
	}
}

//
// Helpers
//

CodeBlock *AsCodeBlock(SyntaxNode *node)
{
	return node ? SafeSyntaxNode<CodeBlock>(node) : nullptr;
}

SyntaxNode *SingleStatement(SyntaxNode *block)
{
	CodeBlock *cb = AsCodeBlock(block);
	if (!cb)
	{
		return nullptr;
	}
	SyntaxNode *found = nullptr;
	for (const unique_ptr<SyntaxNode> &s : cb->GetStatements())
	{
		Comment *comment = SafeSyntaxNode<Comment>(s.get());
		if (comment)
		{
			continue;
		}
		if (found)
		{
			return nullptr; // more than one
		}
		found = s.get();
	}
	return found;
}

bool IsSingleLevelBreak(const SyntaxNode *node)
{
	if (!node)
	{
		return false;
	}
	const BreakStatement *b = SafeSyntaxNode<BreakStatement>(const_cast<SyntaxNode *>(node));
	return b && (b->Levels == 1);
}

bool IsSingleLevelContinue(const SyntaxNode *node)
{
	if (!node)
	{
		return false;
	}
	const ContinueStatement *c = SafeSyntaxNode<ContinueStatement>(const_cast<SyntaxNode *>(node));
	return c && (c->Levels == 1);
}

bool IsTrueCondition(const ConditionNode &conditionOwner)
{
	const unique_ptr<ConditionalExpression> &cond = conditionOwner.GetCondition();
	if (!cond || (cond->GetStatements().size() != 1))
	{
		return false;
	}
	PropertyValue *pv = SafeSyntaxNode<PropertyValue>(cond->GetStatements()[0].get());
	if (!pv)
	{
		return false;
	}
	if ((pv->GetType() == ValueType::Token) && (pv->GetStringValue() == "TRUE"))
	{
		return true;
	}
	return (pv->GetType() == ValueType::Number) && (pv->GetNumberValue() == 1);
}

unique_ptr<SyntaxNode> &ConditionSlot(ConditionNode &conditionOwner)
{
	return conditionOwner.GetCondition()->GetStatements()[0];
}

bool HasConditionExpression(const ConditionNode &conditionOwner)
{
	const unique_ptr<ConditionalExpression> &cond = conditionOwner.GetCondition();
	return cond && !cond->GetStatements().empty() && cond->GetStatements()[0];
}

void SetConditionExpression(ConditionNode &conditionOwner, unique_ptr<SyntaxNode> expr)
{
	conditionOwner.SetCondition(make_unique<ConditionalExpression>(move(expr)));
}

static unique_ptr<SyntaxNode> MakeLogical(BinaryOperator kind, unique_ptr<SyntaxNode> a, unique_ptr<SyntaxNode> b, const SyntaxNode *posSource)
{
	auto op = make_unique<BinaryOp>();
	op->Operator = kind;
	if (posSource)
	{
		op->SetPosition(posSource->GetPosition());
	}
	op->SetStatement1(move(a));
	op->SetStatement2(move(b));
	return op;
}

unique_ptr<SyntaxNode> MakeAnd(unique_ptr<SyntaxNode> a, unique_ptr<SyntaxNode> b, const SyntaxNode *posSource)
{
	return MakeLogical(BinaryOperator::LogicalAnd, move(a), move(b), posSource);
}

unique_ptr<SyntaxNode> MakeOr(unique_ptr<SyntaxNode> a, unique_ptr<SyntaxNode> b, const SyntaxNode *posSource)
{
	return MakeLogical(BinaryOperator::LogicalOr, move(a), move(b), posSource);
}

bool StructEqual(const SyntaxNode *a, const SyntaxNode *b)
{
	if (a == b)
	{
		return true;
	}
	if (!a || !b || (a->GetNodeType() != b->GetNodeType()))
	{
		return false;
	}
	switch (a->GetNodeType())
	{
	case NodeTypeValue:
		return static_cast<const PropertyValueBase &>(*a) == static_cast<const PropertyValueBase &>(*b);
	case NodeTypeComplexValue:
	{
		const ComplexPropertyValue *ca = static_cast<const ComplexPropertyValue *>(a);
		const ComplexPropertyValue *cb = static_cast<const ComplexPropertyValue *>(b);
		return (static_cast<const PropertyValueBase &>(*ca) == static_cast<const PropertyValueBase &>(*cb)) &&
			StructEqual(ca->GetIndexer(), cb->GetIndexer());
	}
	case NodeTypeLValue:
	{
		const LValue *la = static_cast<const LValue *>(a);
		const LValue *lb = static_cast<const LValue *>(b);
		return (la->GetName() == lb->GetName()) && StructEqual(la->GetIndexer(), lb->GetIndexer());
	}
	case NodeTypeBinaryOperation:
	{
		const BinaryOp *ba = static_cast<const BinaryOp *>(a);
		const BinaryOp *bb = static_cast<const BinaryOp *>(b);
		return (ba->Operator == bb->Operator) &&
			StructEqual(ba->GetStatement1(), bb->GetStatement1()) &&
			StructEqual(ba->GetStatement2(), bb->GetStatement2());
	}
	case NodeTypeUnaryOperation:
	{
		const UnaryOp *ua = static_cast<const UnaryOp *>(a);
		const UnaryOp *ub = static_cast<const UnaryOp *>(b);
		return (ua->Operator == ub->Operator) &&
			StructEqual(ua->GetStatement1(), ub->GetStatement1());
	}
	case NodeTypeNaryOperation:
	{
		const NaryOp *na = static_cast<const NaryOp *>(a);
		const NaryOp *nb = static_cast<const NaryOp *>(b);
		if ((na->Operator != nb->Operator) || (na->GetStatements().size() != nb->GetStatements().size()))
		{
			return false;
		}
		for (size_t i = 0; i < na->GetStatements().size(); i++)
		{
			if (!StructEqual(na->GetStatements()[i].get(), nb->GetStatements()[i].get()))
			{
				return false;
			}
		}
		return true;
	}
	default:
		return false;
	}
}

bool IsSameVariable(const LValue &target, const SyntaxNode &read)
{
	string targetName = target.GetName();
	string readName;
	const SyntaxNode *readIndex = nullptr;
	SyntaxNode *readMutable = const_cast<SyntaxNode *>(&read);
	if (ComplexPropertyValue *cpv = SafeSyntaxNode<ComplexPropertyValue>(readMutable))
	{
		if (cpv->GetType() != ValueType::Token)
		{
			return false;
		}
		readName = cpv->GetStringValue();
		readIndex = cpv->GetIndexer();
	}
	else if (PropertyValue *pv = SafeSyntaxNode<PropertyValue>(readMutable))
	{
		if (pv->GetType() != ValueType::Token)
		{
			return false;
		}
		readName = pv->GetStringValue();
	}
	else if (LValue *lv = SafeSyntaxNode<LValue>(readMutable))
	{
		readName = lv->GetName();
		readIndex = lv->GetIndexer();
	}
	else
	{
		return false;
	}
	if (targetName != readName)
	{
		return false;
	}
	const SyntaxNode *targetIndex = target.GetIndexer();
	if (!targetIndex && !readIndex)
	{
		return true;
	}
	if (!targetIndex || !readIndex)
	{
		return false;
	}
	return StructEqual(targetIndex, readIndex);
}

namespace
{
	class LoopFinder : public AstPass
	{
	public:
		bool found = false;
		const char *Name() const override { return "LoopFinder"; }
		RewriteResult Rewrite(unique_ptr<SyntaxNode> &slot, const AstContext &) override
		{
			NodeType t = slot->GetNodeType();
			if ((t == NodeTypeWhileLoop) || (t == NodeTypeDoLoop) || (t == NodeTypeForLoop))
			{
				found = true;
			}
			return RewriteResult::None;
		}
	};
}

bool FunctionHasLoop(FunctionBase &func)
{
	LoopFinder finder;
	RunPassOnce(func, finder);
	return finder.found;
}
