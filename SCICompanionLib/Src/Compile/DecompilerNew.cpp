/***************************************************************************
	Copyright (c) 2020 Philip Fortier

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
#include "DecompilerCore.h"
#include "DecompilerNew.h"
#include "ConsumptionNode.h"
#include "ScopeValues.h"
#include "ScriptOMAll.h"
#include "GameFolderHelper.h"
#include "format.h"
#include "DecompilerResults.h"
#include "PMachine.h"
#include "Operators.h"

using namespace std;
using namespace sci;

std::string _indent2(int iIndent)
{
	std::string theFill;
	theFill.insert(theFill.begin(), iIndent, ' ');
	return theFill;
}


// For debugging purposes
const char *chunkTypeNames[] =
{
	"None",
	"If",
	"Then",
	"Else",
	"Condition",
	"Do",
	"While",
	"LoopBody",
	"And",
	"Or",
	"First",
	"Second",
	"Invert",
	"Switch",
	"Case",
	"CaseCondition",
	"CaseBody",
	"SwitchValue",
	"Break",
	"Continue",
	"TrueNode",
	"ShortCircuitInstruction",
	"FunctionBody",
	"Nary",
	"For",
	"Step",
};

// fwd decl
std::unique_ptr<SyntaxNode> _CodeNodeToSyntaxNode(ConsumptionNode &node, DecompileLookups &lookups);

void _ApplySyntaxNodeToCodeNode(ConsumptionNode &node, StatementsNode &statementsNode, DecompileLookups &lookups)
{
	statementsNode.AddStatement(_CodeNodeToSyntaxNode(node, lookups));
}

void _ApplySyntaxNodeToCodeNode1(ConsumptionNode &node, OneStatementNode &statementsNode, DecompileLookups &lookups)
{
	statementsNode.SetStatement1(_CodeNodeToSyntaxNode(node, lookups));
}

void _ApplySyntaxNodeToCodeNode2(ConsumptionNode &node, TwoStatementNode &statementsNode, DecompileLookups &lookups)
{
	statementsNode.SetStatement2(_CodeNodeToSyntaxNode(node, lookups));
}

void _ApplySyntaxNodeToCodeNodeConditionNode(ConsumptionNode &node, ConditionNode &conditionNode, DecompileLookups &lookups)
{
	unique_ptr<ConditionalExpression> condExp = make_unique<ConditionalExpression>();
	_ApplySyntaxNodeToCodeNode(node, *condExp, lookups);
	conditionNode.SetCondition(move(condExp));
}

void _ApplyChildren(ConsumptionNode &node, StatementsNode &statementsNode, DecompileLookups &lookups)
{
	for (auto &child : node.Children())
	{
		_ApplySyntaxNodeToCodeNode(*child, statementsNode, lookups);
	}
}


std::unique_ptr<SyntaxNode> _CodeNodeToSyntaxNode2(ConsumptionNode &node, DecompileLookups &lookups)
{
	switch (node._chunkType)
	{
		case ChunkType::Break:
		{
			unique_ptr<BreakStatement> breakStatement = make_unique<BreakStatement>();
			breakStatement->Levels = (uint16_t)node._level;
			return unique_ptr<SyntaxNode>(move(breakStatement));
		}

		case ChunkType::Continue:
		{
			unique_ptr<ContinueStatement> continueStatement = make_unique<ContinueStatement>();
			continueStatement->Levels = (uint16_t)node._level;
			return unique_ptr<SyntaxNode>(move(continueStatement));
		}

		case ChunkType::For:
		{
			unique_ptr<ForLoop> forLoop = make_unique<ForLoop>();
			forLoop->SetCodeBlock(make_unique<CodeBlock>());
			_ApplySyntaxNodeToCodeNodeConditionNode(*node.GetChild(ChunkType::Condition), *forLoop, lookups);
			_ApplyChildren(*node.GetChild(ChunkType::LoopBody), *forLoop, lookups);
			unique_ptr<CodeBlock> step = make_unique<CodeBlock>();
			_ApplyChildren(*node.GetChild(ChunkType::Step), *step, lookups);
			forLoop->SetLooper(move(step));
			return unique_ptr<SyntaxNode>(move(forLoop));
		}

		case ChunkType::If:
		{
			unique_ptr<IfStatement> ifStatement = make_unique<IfStatement>();
			_ApplySyntaxNodeToCodeNodeConditionNode(*node.GetChild(ChunkType::Condition), *ifStatement, lookups);
			_ApplySyntaxNodeToCodeNode1(*node.GetChild(ChunkType::Then), *ifStatement, lookups);
			if (node.GetChild(ChunkType::Else))
			{
				_ApplySyntaxNodeToCodeNode2(*node.GetChild(ChunkType::Else), *ifStatement, lookups);
			}
			return unique_ptr<SyntaxNode>(move(ifStatement));
		}

		case ChunkType::Condition:
			if (node.GetChildCount() != 1)
			{
				throw ConsumptionNodeException(&node, "Condition has too many children. Could be due to bad partition before if.");
			}
			return _CodeNodeToSyntaxNode(*node.Child(0), lookups);

		case ChunkType::And:
		case ChunkType::Or:
		{
			unique_ptr<BinaryOp> binaryOp = make_unique<BinaryOp>();
			ConsumptionNode *first = node.GetChild(ChunkType::First);
			ConsumptionNode *second = node.GetChild(ChunkType::Second);
			if (!first || !second)
			{
				throw ConsumptionNodeException(&node, "Compound condition is missing an operand.");
			}
			if (first->GetChildCount() != 1)
			{
				throw ConsumptionNodeException(first, "Too many children for condition node.");
			}
			_ApplySyntaxNodeToCodeNode1(*first->Child(0), *binaryOp, lookups);

			if (second->GetChildCount() != 1)
			{
				throw ConsumptionNodeException(second, "Too many children for condition node.");
			}
			_ApplySyntaxNodeToCodeNode2(*second->Child(0), *binaryOp, lookups);

			binaryOp->Operator = ((node._chunkType == ChunkType::And) ? BinaryOperator::LogicalAnd : BinaryOperator::LogicalOr);
			return unique_ptr<SyntaxNode>(move(binaryOp));
		}

		case ChunkType::Nary:
		{
			// Should be one child:
			ConsumptionNode *naryChild = node.Child(0);
			unique_ptr<NaryOp> naryOp = make_unique<NaryOp>();
			naryOp->Operator = GetBinaryOperatorForInstruction(naryChild->pos->get_opcode());
			_ApplyChildren(*naryChild, *naryOp, lookups);
			return unique_ptr<SyntaxNode>(move(naryOp));
		}
		
		case ChunkType::Then:
		case ChunkType::Else:
		case ChunkType::CaseBody:
		{
			unique_ptr<CodeBlock> codeBlock = std::make_unique<CodeBlock>();
			_ApplyChildren(node, *codeBlock, lookups);
			return unique_ptr<SyntaxNode>(move(codeBlock));
		}
			
		case ChunkType::While:
		{
			unique_ptr<WhileLoop> whileLoop = make_unique<WhileLoop>();
			_ApplySyntaxNodeToCodeNodeConditionNode(*node.GetChild(ChunkType::Condition), *whileLoop, lookups);
			_ApplyChildren(*node.GetChild(ChunkType::LoopBody), *whileLoop, lookups);
			return unique_ptr<SyntaxNode>(move(whileLoop));
		}

		case ChunkType::Do:
		{
			unique_ptr<DoLoop> doLoop = make_unique<DoLoop>();
			_ApplySyntaxNodeToCodeNodeConditionNode(*node.GetChild(ChunkType::Condition), *doLoop, lookups);
			_ApplyChildren(*node.GetChild(ChunkType::LoopBody), *doLoop, lookups);
			return unique_ptr<SyntaxNode>(move(doLoop));
		}

		case ChunkType::Invert:
		{
			unique_ptr<UnaryOp> unaryOp = make_unique<UnaryOp>();
			unaryOp->Operator = UnaryOperator::LogicalNot;
			if (node.GetChildCount() != 1)
			{
				throw ConsumptionNodeException(&node, "Too many children for Invert node.");
			}
			_ApplySyntaxNodeToCodeNode1(*node.Child(0), *unaryOp, lookups);
			return unique_ptr<SyntaxNode>(move(unaryOp));
		}

		case ChunkType::Switch:
		{
			unique_ptr<SwitchStatement> switchStatement = make_unique<SwitchStatement>();
			
			for (auto &child : node.Children())
			{
				if (child->GetType() == ChunkType::Case)
				{
					// A case should have two children. The case body and (optionally) a condition
					unique_ptr<CaseStatement> caseStatement = make_unique<CaseStatement>();
					ConsumptionNode *cond = child->GetChild(ChunkType::CaseCondition);
					if (cond)
					{
						assert(cond->GetChildCount() == 1);
						_ApplySyntaxNodeToCodeNode1(*cond->Child(0), *caseStatement, lookups);
					}
					else
					{
						caseStatement->SetDefault(true);
					}
					ConsumptionNode *body = child->GetChild(ChunkType::CaseBody);
					_ApplySyntaxNodeToCodeNode(*body, *caseStatement, lookups);
					switchStatement->AddCase(move(caseStatement));
				}
				else if (child->GetType() == ChunkType::SwitchValue)
				{
					assert(child->GetChildCount() == 1);
					_ApplySyntaxNodeToCodeNode1(*child->Child(0), *switchStatement, lookups);
				}
				else
				{
					assert(false && "Unexpected child of switch chunk.");
				}
			}

			return unique_ptr<SyntaxNode>(move(switchStatement));
		}


		case ChunkType::TrueNode:
		{
			// This could mean TRUE
			unique_ptr<PropertyValue> valueTemp = make_unique<PropertyValue>();
			valueTemp->SetValue("TRUE", ValueType::Token);
			return unique_ptr<SyntaxNode>(move(valueTemp));
		}
	}

	assert(false);
	std::unique_ptr<Comment> comment = std::make_unique<Comment>(CommentType::None);
	comment->SetName("UNIMPLEMENTED CONTROL STRUCTURE");
	return unique_ptr<SyntaxNode>(comment.release());
}

// True if Sierra's optimizer keeps its "accumulator holds n" fact across
// this instruction (see the sc optimizer: pushes and dup, a property store,
// a stack load, rest, toss, selfID push). A branch ends the walk too: the
// fact is reset at a label.
static bool _LeavesAccumulatorFact(Opcode op)
{
	switch (op)
	{
		case Opcode::PUSHI:
		case Opcode::PUSH0:
		case Opcode::PUSH1:
		case Opcode::PUSH2:
		case Opcode::PUSH:
		case Opcode::DUP:
		case Opcode::PUSHSELF:
		case Opcode::ATOP:
		case Opcode::PTOS:
		case Opcode::REST:
		case Opcode::TOSS:
			return true;
	}
	if ((op >= Opcode::LAG) && (op <= Opcode::LastLoadStore))
	{
		return _IsVOPureStack(op) && !_IsVOStoreOperation(op) && !_IsVOIncremented(op) && !_IsVODecremented(op);
	}
	return false;
}

WORD _GetImmediateFromCodeNode(ConsumptionNode &node, ConsumptionNode *pNodePrevious = nullptr, bool assertIfNone = false, bool *foundOut = nullptr)
{
	bool found = true;
	WORD w = 0;
	if (node._hasPos)
	{
		code_pos pos = node.pos;
		switch (pos->get_opcode())
		{
			case Opcode::LDI:
				w = pos->get_first_operand();
				break;

			case Opcode::PUSHI:
				w = pos->get_first_operand();
				break;

			case Opcode::PUSH0:
				w = 0;
				break;

			case Opcode::PUSH1:
				w = 1;
				break;

			case Opcode::PUSH2:
				w = 2;
				break;

			case Opcode::PUSH:
				// Sierra's optimizer turns "pushi n" into "push" when the
				// accumulator already holds n (a selector pushed right after
				// a stray "ldi n", as at the start of a switch case). The push's
				// child is then the value it pushes: a clone of that ldi. A
				// child that is not a number (a variable that holds a
				// selector) gives no immediate.
				if (node.GetChildCount() == 1)
				{
					bool childFound = false;
					WORD childValue = _GetImmediateFromCodeNode(*node.Child(0), nullptr, false, &childFound);
					found = childFound;
					w = childFound ? childValue : 0;
					assert(!assertIfNone || found);
					break;
				}

				// Otherwise the optimizer knew the accumulator held n: walk back
				// over the instructions that leave its value alone (pushes, a
				// property store, stack loads, &rest) to the ldi that set it.
				// A load, a variable store, arithmetic, a call, or a branch
				// changes what the optimizer knew, and stops the walk.
				{
					code_pos back = pos;
					for (int steps = 0; steps < 64; steps++)
					{
						--back;
						Opcode op = back->get_opcode();
						if (op == Opcode::LDI)
						{
							w = back->get_first_operand();
							break;
						}
						if (!_LeavesAccumulatorFact(op))
						{
							break;
						}
					}
					if (w != 0 || (back->get_opcode() == Opcode::LDI))
					{
						break;
					}
				}

				// REVIEW, hits too often..... ldi jmp ldi push
				//assert(node.GetChildCount());

				//HACK: LSL3DEMO uses "ldi 57, push" to push an "init" selector, but this isn't properly decompiled
				//unlike the "proper" method, "pushi 57". So IF the previous node was an LDI, return *its* first operand instead.
				//This is *almost* the same as Opcode::DUP down below but I specifically want to use LDI/PUSH pairs only.
				if (pNodePrevious && pNodePrevious->_hasPos)
				{
					if (pNodePrevious->pos->get_opcode() == Opcode::LDI)
					{
						w = _GetImmediateFromCodeNode(*pNodePrevious);
						break;
					}
				}
				//else
				{
					code_pos posFlatPrevious = pos;
					--posFlatPrevious;
					if (posFlatPrevious->get_opcode() == Opcode::LDI)
					{
						ConsumptionNode nodeTemp;
						nodeTemp.SetPos(posFlatPrevious);
						w = _GetImmediateFromCodeNode(nodeTemp);
					}
				}
				break;

			case Opcode::DUP:
				if (pNodePrevious)
				{
					w = _GetImmediateFromCodeNode(*pNodePrevious);
				}
				else
				{
					code_pos posFlatPrevious = pos;
					--posFlatPrevious;
					// REVIEW: could crash...
					ConsumptionNode nodeTemp;
					nodeTemp.SetPos(posFlatPrevious);
					w = _GetImmediateFromCodeNode(nodeTemp);
				}
				break;

			default:
				found = false;
				assert(!assertIfNone);
				break;
		}
	}
	if (foundOut)
	{
		*foundOut = found;
	}
	return w;
}

const char SelfToken[] = "self";
const char InvalidLookupError[] = "LOOKUP_ERROR";
const char RestParamName[] = "params";


// Looks for a rest instruction at index (and tracks it and adds to the SendParam). Returns true if it found one.
bool _MaybeConsumeRestInstruction(SendParam *pSendParam, int index, ConsumptionNode &node, DecompileLookups &lookups)
{
	if (index >= (int)node.GetChildCount())
	{
		return false;
	}
	if (!node.Child(index)->_hasPos)
	{
		return false;
	}

	code_pos restInstruction = node.Child(index)->GetCode();
	bool foundRest = (restInstruction->get_opcode() == Opcode::REST);
	if (foundRest)
	{
		std::unique_ptr<RestStatement> rest = std::make_unique<RestStatement>();
		// rest->SetName(RestParamName);
		// We'll set the name at a later time, because it might already have a name.

		// We need to tell the function that
		// someone used a rest instruction with this index:
		uint16_t parameterIndex = restInstruction->get_first_operand();
		assert(parameterIndex > 0); // otherwise, I don't know what I'm doing.
		// The rest statement will take on its name if so. Otherwise we'll use "params"
		lookups.TrackRestStatement(rest.get(), parameterIndex);

		pSendParam->AddStatement(std::move(rest));
	}
	return foundRest;
}

void _ProcessLEA(PropertyValueBase &value, const scii &inst, DecompileLookups &lookups)
{
	VarScope varScope;
	WORD wVarIndex;
	value.SetValue(_GetVariableNameFromCodePos(inst, lookups, &varScope, &wVarIndex), ValueType::Pointer);
	lookups.TrackVariableUsage(varScope, wVarIndex, true);  // Let's consider this indexed.
}

Consumption _GetInstructionConsumption(ConsumptionNode &node, DecompileLookups &lookups)
{
	if (node._hasPos)
	{
		return _GetInstructionConsumption(*node.GetCode(), &lookups);
	}
	else
	{
		Consumption consumption = {};
		// A control structure. Only certain types of control
		// structures should "generate acc".
		switch (node.GetType())
		{
			case ChunkType::If:
			case ChunkType::Switch:
			case ChunkType::And:
			case ChunkType::Or:
			case ChunkType::Nary:	   // Since we explicitly set things up like this...
				// REVIEW: Add loops?
				consumption.cAccGenerate = 1;
				break;
		}
		return consumption;
	}
}

void MorphInstructionIntoShortCircuit(scii &inst)
{
	switch (inst.get_opcode())
	{
		case Opcode::SAG:
		case Opcode::pAG:
		case Opcode::nAG:
			inst.set_opcode(Opcode::LAG);
			break;
		case Opcode::SAP:
		case Opcode::pAP:
		case Opcode::nAP:
			inst.set_opcode(Opcode::LAP);
			break;
		case Opcode::SAT:
		case Opcode::pAT:
		case Opcode::nAT:
			inst.set_opcode(Opcode::LAT);
			break;
		case Opcode::SAL:
		case Opcode::pAL:
		case Opcode::nAL:
			inst.set_opcode(Opcode::LAL);
			break;

		case Opcode::pSG:
		case Opcode::nSG:
			inst.set_opcode(Opcode::LSG);
			break;
		case Opcode::pSP:
		case Opcode::nSP:
			inst.set_opcode(Opcode::LSP);
			break;
		case Opcode::pST:
		case Opcode::nST:
			inst.set_opcode(Opcode::LST);
			break;
		case Opcode::pSL:
		case Opcode::nSL:
			inst.set_opcode(Opcode::LSL);
			break;

		case Opcode::ATOP:
			inst.set_opcode(Opcode::PTOA);
			break;

		default:
			assert(false && "Unexpected short circuit");
	}
}

std::string _GetPossiblyMissingPublicProcedureName(DecompileLookups &lookups, uint16_t script, uint16_t theExport)
{
	std::string name = lookups.DoesExportExist(script, theExport) ? "" : "__";
	name += _GetPublicProcedureName(script, theExport);
	return name;
}

std::unique_ptr<SyntaxNode> _CodeNodeToSyntaxNode(ConsumptionNode &node, DecompileLookups &lookups)
{
	bool preferLValue = lookups.PreferLValue;
	PreferLValue resetPreferLValue(lookups, false); // Reset it, because if it's true it's not normal.

	if (!node._hasPos)
	{
		return _CodeNodeToSyntaxNode2(node, lookups);
	}

	scii inst = *node.pos;
	if (node.GetType() == ChunkType::ShortCircuitInstruction)
	{
		MorphInstructionIntoShortCircuit(inst);
	}

	Opcode bOpcode = inst.get_opcode();
	switch (bOpcode)
	{
		case Opcode::LDI:
		case Opcode::PUSHI:
		case Opcode::PUSH0:
		case Opcode::PUSH1:
		case Opcode::PUSH2:
		{
			unique_ptr<PropertyValue> value = std::make_unique<PropertyValue>();
			switch (bOpcode)
			{
				case Opcode::PUSH0:
					value->SetValue(0);
					break;
				case Opcode::PUSH1:
					value->SetValue(1);
					break;
				case Opcode::PUSH2:
					value->SetValue(2);
					break;
				default: // LDI and PUSHI
					uint16_t theValue = inst.get_first_operand();
					value->SetValue(theValue);
					if (theValue >= 32768)
					{
						// This was probably intended to be negative
						value->Negate();
					}
					break;
			}
			return unique_ptr<SyntaxNode>(move(value));
		}
		break;

		case Opcode::PUSH:
			assert(node.Children().size() == 1);
			{
				// Fwd to the first child (should be what gets in to the accumulator)
				return _CodeNodeToSyntaxNode(*node.Children()[0], lookups);
			}
			break;

		case Opcode::CALLK:   // kernel, # of params
		case Opcode::CALLB:   // mainproc index, # of params
		case Opcode::CALLE:   // script, index, # of params
		case Opcode::CALL:	// offset, # of params
		{
			WORD cParams = (bOpcode == Opcode::CALLE) ? inst.get_third_operand() : inst.get_second_operand();
			cParams /= 2; // bytes -> params
			cParams += 1; // +1 because there is a parameter count first.
			std::stringstream ss;
			switch (bOpcode)
			{
				case Opcode::CALLK:
					ss << lookups.LookupKernelName(inst.get_first_operand());
					break;
				case Opcode::CALLB:
					ss << _GetPossiblyMissingPublicProcedureName(lookups, 0, inst.get_first_operand());
					break;
				case Opcode::CALLE:
					ss << _GetPossiblyMissingPublicProcedureName(lookups, inst.get_first_operand(), inst.get_second_operand());
					break;
				case Opcode::CALL:
					ss << _GetProcNameFromScriptOffset(inst.get_final_postop_offset() + inst.get_first_operand());
					// Track this call so that we can say whether or not a local proc "belongs" to a class.
					lookups.TrackProcedureCall(inst.get_final_postop_offset() + inst.get_first_operand());
					break;
			}
			unique_ptr<ProcedureCall> pProcCall = std::make_unique<ProcedureCall>();
			pProcCall->SetName(ss.str());
			for (WORD i = 0; i < node.GetChildCount(); ++i)
			{
				if (i < cParams)
				{
					if (i == 0)
					{
						// The first one should just be the number of parameters.
						WORD cParamsCheck = _GetImmediateFromCodeNode(*node.Child(i));
						assert((cParamsCheck + 1) == cParams);
					}
					else
					{
						_ApplySyntaxNodeToCodeNode(*node.Child(i), *pProcCall, lookups);
					}
				}
				else
				{
					if (i == cParams)
					{
						// This might be a rest instruction.
						_ApplySyntaxNodeToCodeNode(*node.Child(i), *pProcCall, lookups);
					}
					else
					{
						assert(false);  // How would we get here?
					}
				}
			}

			return unique_ptr<SyntaxNode>(move(pProcCall));
		}
		break;

		case Opcode::SELF:
		case Opcode::SEND:
		case Opcode::SUPER:
		{
			unique_ptr<SendCall> sendCall = std::make_unique<SendCall>();
			if (bOpcode == Opcode::SELF)
			{
				sendCall->SetName(SelfToken);
			}
			else if (bOpcode == Opcode::SUPER)
			{
				// Actually you can send to any super class... the first operand says which. Does anyone use this?
				// Let's assert that they don't
				const sci::ClassDefinition *classDefinition = lookups.GetClassContext();
				assert(classDefinition);
				if (classDefinition)
				{
					uint16_t species = inst.get_first_operand();
					std::string superClassContext = classDefinition->GetSuperClass();
					std::string superClassStated = lookups.LookupClassName(species);
					assert(superClassContext == superClassStated);
				} // If we're in a proc without ownership, we can't assert anything. TODO insert comment warning about this.
				
				sendCall->SetName("super");
			}
			Consumption cons = _GetInstructionConsumption(inst, &lookups);

			if (cons.cStackConsume == 0)
			{
				// This is a zero send. They happen sometimes, presumably where Sierra programmers enclosed stuff
				// in extra parentheses.
				assert(cons.cAccConsume == 1);
				assert(node.GetChildCount() == 1);
				return _CodeNodeToSyntaxNode(*node.Child(0), lookups);
				// In this case, we can just ignore the send, and forward to the acc generator.
			}
			else
			{


				WORD cStackPushesLeft = cons.cStackConsume;
				WORD cAccLeft = (bOpcode == Opcode::SEND) ? 1 : 0;
				size_t i = 0;
				WORD cParamsLeft = 0;
				bool fLookingForSelector = true;
				unique_ptr<SendParam> sendParam;

				while ((cAccLeft || cStackPushesLeft) && (i < node.GetChildCount()))
				{
					ConsumptionNode *pPreviousChild = i ? node.Child(i - 1) : nullptr;
					if (cAccLeft)
					{
						Consumption consAcc = _GetInstructionConsumption(*node.Child(i), lookups);
						if (consAcc.cAccGenerate)
						{
							// We want an LValue instead of a property value. Tell _CodeNodeToSyntaxNode this.
							PreferLValue preferLValue(lookups, true);
							unique_ptr<SyntaxNode> pSendObject = _CodeNodeToSyntaxNode(*node.Child(i), lookups);
							const LValue *pValue = SafeSyntaxNode<LValue>(pSendObject.get());
							if (pValue)
							{
								unique_ptr<LValue> lValue(static_cast<LValue*>(pSendObject.release()));
								sendCall->SetLValue(move(lValue));
							}
							else
							{
								sendCall->SetStatement1(move(pSendObject));
							}
							--cAccLeft;
						}
					}

					if (cStackPushesLeft)
					{
						Consumption consAcc = _GetInstructionConsumption(*node.Child(i), lookups);
						if (consAcc.cStackGenerate)
						{
							if (fLookingForSelector)
							{
								sendParam.reset(new SendParam);
								fLookingForSelector = false;
								bool found;
								WORD wValue = _GetImmediateFromCodeNode(*node.Child(i), pPreviousChild, false, &found);
								if (!found)
								{
									// Occasionally selectors can be variables.
									std::unique_ptr<SyntaxNode> syntaxNode = _CodeNodeToSyntaxNode(*node.Child(i), lookups);
									const LValue *pLValue = SafeSyntaxNode<LValue>(syntaxNode.get());
									if (pLValue)
									{
										sendParam->SetName(pLValue->GetName());
									}
									else
									{
										throw ConsumptionNodeException(node.Child(i), "Expected selector.");
									}
								}
								else
								{
									sendParam->SetName(lookups.LookupSelectorName(wValue));
									sendParam->SetIsMethod(!lookups.IsPropertySelectorOnly(wValue));
								}
							}
							else if (cParamsLeft)
							{
								--cParamsLeft;
								_ApplySyntaxNodeToCodeNode(*node.Child(i), *sendParam, lookups); // Hmm, we were passing the wrong previous node here before... will that matter?
								if (cParamsLeft == 0)
								{
									if (_MaybeConsumeRestInstruction(sendParam.get(), i + 1, node, lookups))
									{
										i++;
									}
									sendCall->AddSendParam(std::move(sendParam));
									fLookingForSelector = true;
								}
							}
							else
							{
								// Must be a param count
								cParamsLeft = _GetImmediateFromCodeNode(*node.Child(i), pPreviousChild);
								if (cParamsLeft == 0)
								{
									if (_MaybeConsumeRestInstruction(sendParam.get(), i + 1, node, lookups))
									{
										i++;
									}
									sendCall->AddSendParam(std::move(sendParam));
									fLookingForSelector = true;
								}
							}
							--cStackPushesLeft;
						}
					}

					++i; // Always increment i
					// TODO: warn if we didn't do anything in this loop?  Unused instruction???
				}

				assert((!sendCall->GetObjectA().empty() || sendCall->GetStatement1()) && "Send call with no object");
				sendCall->SimplifySendObject();
				return unique_ptr<SyntaxNode>(move(sendCall));
			}
		}
		break;

		case Opcode::BNT:
		case Opcode::BT:
			assert(node.GetChildCount() == 1);
			if (node.GetChildCount() >= 1)
			{
				// This should have one child, which is the condition...
				return _CodeNodeToSyntaxNode(*node.Child(0), lookups);
			}
			break;

		case Opcode::SUB:
		case Opcode::MUL:
		case Opcode::DIV:
		case Opcode::MOD:
		case Opcode::SHR:
		case Opcode::SHL:
		case Opcode::XOR:
		case Opcode::AND:
		case Opcode::OR:
		case Opcode::ADD:
		case Opcode::EQ:
		case Opcode::GT:
		case Opcode::LT:
		case Opcode::LE:
		case Opcode::NE:
		case Opcode::GE:
		case Opcode::UGT:
		case Opcode::UGE:
		case Opcode::ULT:
		case Opcode::ULE:
			if (node.GetChildCount() >= 2)
			{
				unique_ptr<BinaryOp> binaryOp = std::make_unique<BinaryOp>();
				// The child that generates the stack push should be the first statement.
				ConsumptionNode *pCodeNodeStack = node.Child(0);
				ConsumptionNode *pCodeNodeAcc = node.Child(1);
				Consumption cons = _GetInstructionConsumption(*pCodeNodeStack, lookups);
				if (!cons.cStackGenerate)
				{
					// Ooops... the other one must have been the stack-generating one.
					swap(pCodeNodeStack, pCodeNodeAcc);
				}
				_ApplySyntaxNodeToCodeNode1(*pCodeNodeStack, *binaryOp, lookups);
				_ApplySyntaxNodeToCodeNode2(*pCodeNodeAcc, *binaryOp, lookups);
				binaryOp->Operator = GetBinaryOperatorForInstruction(bOpcode);
				return unique_ptr<SyntaxNode>(move(binaryOp));
			}
			break;

		case Opcode::BNOT:
		case Opcode::NOT:
		case Opcode::NEG:
			if (node.GetChildCount() >= 1)
			{
				unique_ptr<UnaryOp> unaryOp = std::make_unique<UnaryOp>();
				ConsumptionNode *pCodeNodeAcc = node.Child(0);
				Consumption cons = _GetInstructionConsumption(*pCodeNodeAcc, lookups);
				assert(cons.cAccGenerate);
				_ApplySyntaxNodeToCodeNode1(*pCodeNodeAcc, *unaryOp, lookups);
				unaryOp->Operator = GetUnaryOperatorForInstruction(bOpcode);
				return unique_ptr<SyntaxNode>(move(unaryOp));
			}
			break;

		case Opcode::REST:
		{
			std::unique_ptr<RestStatement> rest = std::make_unique<RestStatement>();
			// We need to tell the function that
			// someone used a rest instruction with this index:
			uint16_t parameterIndex = inst.get_first_operand();
			assert(parameterIndex > 0); // otherwise, I don't know what I'm doing.
			// The rest statement will take on its name if so. Otherwise we'll use "params"
			lookups.TrackRestStatement(rest.get(), parameterIndex);
			return unique_ptr<SyntaxNode>(move(rest));
		}
		break;

		case Opcode::LEA:
		{
			WORD wType = inst.get_first_operand();
			bool hasIndexer = ((wType >> 1) & LEA_ACC_AS_INDEX_MOD) == LEA_ACC_AS_INDEX_MOD;
			if (hasIndexer)
			{
				unique_ptr<ComplexPropertyValue> pValue = std::make_unique<ComplexPropertyValue>();

				assert(node.GetChildCount() == 1);
				pValue->SetIndexer(_CodeNodeToSyntaxNode(*node.Child(0), lookups));

				_ProcessLEA(*pValue, inst, lookups);
				return unique_ptr<SyntaxNode>(move(pValue));
			}
			else
			{
				unique_ptr<PropertyValue> pValue = std::make_unique<PropertyValue>();
				_ProcessLEA(*pValue, inst, lookups);
				return unique_ptr<SyntaxNode>(move(pValue));
			}
		}
		break;

		case Opcode::CLASS:
		case Opcode::PUSHSELF:
		{
			std::string className = (bOpcode == Opcode::CLASS) ? lookups.LookupClassName(inst.get_first_operand()) : SelfToken;
			if (className.empty() && (bOpcode == Opcode::CLASS))
			{
				// The species has no name. Its script is not in the game (QfG4
				// keeps class-table entries for stripped scripts). Emit a
				// synthesized name; a classdef gives it a species number, so
				// the text round-trips without falling back to asm.
				className = GetUnknownClassName(inst.get_first_operand());
			}
			if (!className.empty())
			{
				unique_ptr<PropertyValue> value = std::make_unique<PropertyValue>();
				value->SetValue(className, ValueType::Token);
				return unique_ptr<SyntaxNode>(move(value));
			}
		}
		break;

		case Opcode::LOFSA:
		case Opcode::LOFSS:
		{
			// The first operand specifies an offset from the start of the next instruction.
			// In SCI0 is this a relative offset from the post operation program counter.
			// In SCI1 it appears to be an absolute offset.
			ICompiledScriptSpecificLookups::ObjectType type;
			SCIVersion version = lookups.Helper.Version;
			uint16_t wName = version.lofsaOpcodeIsAbsolute ?
				inst.get_first_operand() :
				(inst.get_first_operand() + inst.get_final_postop_offset());
			std::string name = InvalidLookupError;
			// SQ4, main, there is a lofsa that points into the middle of a string.
			lookups.LookupScriptThing(wName, type, name);
			unique_ptr<PropertyValue> value = std::make_unique<PropertyValue>();
			value->SetValue(name, _ScriptObjectTypeToPropertyValueType(type));
			return unique_ptr<SyntaxNode>(move(value));
		}
		break;

		case Opcode::ATOP:	// acc to property index
		case Opcode::STOP:	// Stack to property index
		{
			unique_ptr<Assignment> pAssignment = std::make_unique<Assignment>();
			if (node.GetChildCount())
			{
				_ApplySyntaxNodeToCodeNode1(*node.Child(0), *pAssignment, lookups);
			}
			// Now this is a property... find out which.
			WORD wPropertyIndex = node.GetCode()->get_first_operand();
			unique_ptr<LValue> lValue = make_unique<LValue>();
			lValue->SetName(lookups.LookupPropertyName(wPropertyIndex));
			pAssignment->SetVariable(move(lValue));
			pAssignment->Operator = AssignmentOperator::Assign;
			return unique_ptr<SyntaxNode>(move(pAssignment));
		}
		break;

		case Opcode::PTOS:	// property index to stack
		case Opcode::PTOA:	// property index to acc
		case Opcode::IPTOA:   // Inc prop to acc
		case Opcode::DPTOA:   // Dec prop to acc
		case Opcode::IPTOS:   // Inc prop to stack
		case Opcode::DPTOS:   // Dec prop to stack
		{
			unique_ptr<PropertyValue> pValue = std::make_unique<PropertyValue>();
			assert(node.GetChildCount() == 0);
			WORD wPropertyIndex = node.GetCode()->get_first_operand();
			pValue->SetValue(lookups.LookupPropertyName(wPropertyIndex), ValueType::Token);
			bool fIncrement = (bOpcode == Opcode::IPTOA) || (bOpcode == Opcode::IPTOS);
			bool fDecrement = (bOpcode == Opcode::DPTOA) || (bOpcode == Opcode::DPTOS);
			if (fIncrement || fDecrement)
			{
				unique_ptr<UnaryOp> pUnary = std::make_unique<UnaryOp>();
				pUnary->Operator = fIncrement ? UnaryOperator::Increment : UnaryOperator::Decrement;
				pUnary->SetStatement1(std::move(pValue));
				return unique_ptr<SyntaxNode>(move(pUnary));
			}
			else
			{
				if (preferLValue && (pValue->GetType() == ValueType::Token))
				{
					// Come callers want an LValue instead
					unique_ptr<LValue> lValue = make_unique<LValue>();
					lValue->SetName(pValue->GetStringValue());
					return unique_ptr<SyntaxNode>(move(lValue));
				}
				else
				{
					return unique_ptr<SyntaxNode>(move(pValue));
				}
			}
		}
		break;

		case Opcode::DUP:
			assert(false); // In what conditions do we hit this? Should already be handled in switch statements.
			/*
			if (node.GetPrevious())
			{
				return _CodeNodeToSyntaxNode(*node.GetPrevious(), lookups);
			}
			else
			{
				// TODO: Walk backwards until we have an instruction that puts something on the stack.
				// This may not work in all cases (if we pass a branch, etc...)
				CoreLog(LogLevel::Warning, "Possible incorrect logic.");
				// How do we handle this one?
				// assert(false);
			}*/
			break;

		case Opcode::JMP:
			// Return empty comment.
			return unique_ptr<SyntaxNode>(new Comment(CommentType::None));

		case Opcode::PPREV:
			// This is a special one. In our pre-processing, we should have found the "previous" accumulator
			// value that PPREV needs and inserted it as a child.
			assert(node.GetChildCount() == 1);
			if (node.GetChildCount() > 0)
			{
				return _CodeNodeToSyntaxNode(*node.Child(0), lookups);
			}
			break;

		case Opcode::LINK:
			return unique_ptr<SyntaxNode>(new Comment(CommentType::None));
			break;

		case Opcode::RET:
		{
			unique_ptr<ReturnStatement> pReturn = std::make_unique<ReturnStatement>();
			if (node.GetChildCount())
			{
				_ApplySyntaxNodeToCodeNode1(*node.Child(0), *pReturn, lookups);
			}
			return unique_ptr<SyntaxNode>(move(pReturn));
			// TODO: Difficult to determine if the code actually meant to return anything...
			// we'll need to check if the previous thing was "intentional" or not...
			// We could have trouble with branching...
		}
		break;

		case Opcode::SELFID:
		{
			unique_ptr<PropertyValue> pv = make_unique<PropertyValue>();
			// REVIEW: Given that different syntaxes might use different keywords
			// for "self", it is questionable to have it as a string token here.
			pv->SetValue(SelfToken, ValueType::Token);
			return unique_ptr<SyntaxNode>(move(pv));
		}
		break;

		case Opcode::TOSS:
		{
			// Switch statement should be identified by now.
			// If we encounter a lone TOSS at this point, it suggests we have mis-identified a switch statement.
			// This can happen for "degenerate switches", for instance SQ5, 200, rightDome::doVerb has this:
			//
			// lsp	param[$1]
			// pushi	$11d; 285, doVerb
			// push1
			// lsp	param[$1]
			// &rest	$2
			// super	Feature, $6
			// toss
			// ret
			//
			// This looks like a switch statement with only a default case
			// Let's construct that now. If our first child generates stack, that child will be the condition.
			// The rest of the children will form the default case.
			if (node.GetChildCount())
			{
				Consumption consumption = _GetInstructionConsumption(*node.Child(0), lookups);
				if (consumption.cStackGenerate)
				{
					// Ok, with this, we can at least make a passable switch statement
					unique_ptr<SwitchStatement> switchStatement = make_unique<SwitchStatement>();
					_ApplySyntaxNodeToCodeNode1(*node.Child(0), *switchStatement, lookups);
					
					unique_ptr<CaseStatement> defaultCase = make_unique<CaseStatement>();
					defaultCase->SetDefault(true);
					for (size_t i = 1; i < node.GetChildCount(); i++)
					{
						_ApplySyntaxNodeToCodeNode(*node.Child(i), *defaultCase, lookups);
					}
					switchStatement->AddCase(move(defaultCase));

					return unique_ptr<SyntaxNode>(move(switchStatement));
				}
			}
		}
		break;

		case Opcode::Filename:
		case Opcode::LineNumber:
			// Empty comment...
			return  unique_ptr<SyntaxNode>(std::make_unique<Comment>(CommentType::None));

		default:
			if ((bOpcode >= Opcode::FirstLoadStore) && (bOpcode <= Opcode::LastLoadStore))
			{
				// This could be a load or store operation.
				if (_IsVOStoreOperation(bOpcode))
				{
					// Store operation: Assignment (LValue/PropertyValue)
					unique_ptr<Assignment> pAssignment = std::make_unique<Assignment>();
					pAssignment->Operator = AssignmentOperator::Assign;

					// The variable name
					unique_ptr<LValue> lValue = make_unique<LValue>();
					WORD wVarIndex;
					VarScope varScope;
					lValue->SetName(_GetVariableNameFromCodePos(inst, lookups, &varScope, &wVarIndex));
					bool isIndexed = _IsVOIndexed(bOpcode);
					lookups.TrackVariableUsage(varScope, wVarIndex, isIndexed);
					if (isIndexed) // The accumulator is used as an indexer.
					{
						for (size_t i = 0; i < node.GetChildCount(); i++)
						{
							Consumption cons = _GetInstructionConsumption(*node.Child(i), lookups);
							if (cons.cAccGenerate)
							{
								lValue->SetIndexer(_CodeNodeToSyntaxNode(*node.Child(i), lookups));
								break;
							}
						}
					}
					pAssignment->SetVariable(move(lValue));

					// Is assigned our child:
					if (node.GetChildCount() < 1)
					{
						throw ConsumptionNodeException(&node, "Assignment RValue did not exist.");
					}
					for (size_t i = 0; i < node.GetChildCount(); i++)
					{
						Consumption cons = _GetInstructionConsumption(*node.Child(i), lookups);
						if ((_IsVOStack(bOpcode) && cons.cStackGenerate) || (!_IsVOStack(bOpcode) && cons.cAccGenerate))
						{
							_ApplySyntaxNodeToCodeNode1(*node.Child(i), *pAssignment, lookups);
						}
					}
					return unique_ptr<SyntaxNode>(move(pAssignment));
				}
				else
				{
					// Load operation - make PropertyValue
					std::unique_ptr<PropertyValueBase> pValue;

					// Does it have an indexer?
					bool isIndexed = _IsVOIndexed(bOpcode);
					if (isIndexed)
					{
						pValue.reset(new ComplexPropertyValue);
						// Then it should have a child
						if (node.GetChildCount() >= 1)
						{
							static_cast<ComplexPropertyValue*>(pValue.get())->SetIndexer(_CodeNodeToSyntaxNode(*node.Child(0), lookups));
						}
						else
						{
							assert(false); // REVIEW, TODO 
						}
					}
					else
					{
						pValue.reset(new PropertyValue);
					}

					VarScope varScope;
					WORD wIndex;
					pValue->SetValue(_GetVariableNameFromCodePos(inst, lookups, &varScope, &wIndex), ValueType::Token);
					lookups.TrackVariableUsage(varScope, wIndex, isIndexed);

					// If it has an incrementer or decrementer, wrap it in a unary operator first.
					bool fIncrement = _IsVOIncremented(bOpcode);
					bool fDecrement = _IsVODecremented(bOpcode);
					if (fIncrement || fDecrement)
					{
						unique_ptr<UnaryOp> pUnary = std::make_unique<UnaryOp>();
						pUnary->Operator = (fIncrement ? UnaryOperator::Increment : UnaryOperator::Decrement);
						pUnary->SetStatement1(std::move(pValue));
						return unique_ptr<SyntaxNode>(move(pUnary));
					}
					else
					{
						if (preferLValue && (pValue->GetType() == ValueType::Token))
						{
							// Come callers want an LValue instead
							unique_ptr<LValue> lValue = make_unique<LValue>();
							lValue->SetName(pValue->GetStringValue());
							if (isIndexed)
							{
								lValue->SetIndexer(move(static_cast<ComplexPropertyValue*>(pValue.get())->StealIndexer()));
							}
							return unique_ptr<SyntaxNode>(move(lValue));
						}
						else
						{
							return unique_ptr<SyntaxNode>(move(pValue));
						}
					}
				}
			}
			break;
	}

	// This happens when a toss is used in weird circumstances, for instance.
	// e.g. SQ5, rightDome::doVerb.
	throw ConsumptionNodeException(&node, "Unexpected opcode.");
}

void OutputNewStructure(sci::FunctionBase &func, const scope::CodeModel &model, const scope::Region &root, std::list<scii> &code, const std::set<int> &passedDeadBranches, DecompileLookups &lookups)
{
	unique_ptr<ConsumptionNode> mainChunk = scope::BuildValues(model, root, code, lookups.FunctionDecompileHints.ReturnsValue, passedDeadBranches);

	string debugTrackName = GetMethodTrackingName(func.GetOwnerClass(), func, true);
	if (lookups.DebugInstructionConsumption && (!lookups.pszDebugFilter || PathMatchSpec(debugTrackName.c_str(), lookups.pszDebugFilter)))
	{
		std::stringstream ss;
		mainChunk->Print(ss, 0);
		lookups.DecompileResults().AddResult(DecompilerResultType::Debug, debugTrackName + " chunks (scope):\n" + ss.str());
	}

	try
	{
		for (auto &child : mainChunk->Children())
		{
			_ApplySyntaxNodeToCodeNode(*child, func, lookups);
		}
	}
	catch (ConsumptionNodeException &e)
	{
		int offset = e.node->_hasPos ? e.node->GetCode()->get_final_offset_dontcare() : -1;
		throw scope::ScopeError("values", "syntax", offset, e.message);
	}
}
