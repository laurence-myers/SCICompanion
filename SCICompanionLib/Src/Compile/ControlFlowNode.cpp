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
#include "ControlFlowNode.h"
#include "format.h"
#include "StlUtil.h"
#include "PMachine.h"

using namespace std;

scii ControlFlowNode::getLastInstruction() { assert(false); return scii(sciVersion0, Opcode::INDETERMINATE, -1); }

// Safe to call on anything. If it doesn't apply, it returns null.
ControlFlowNode *GetOtherBranch(ControlFlowNode *branchNode, ControlFlowNode *branch1)
{
	ControlFlowNode *other = nullptr;
	if (branchNode->Successors().size() == 2)
	{
		ControlFlowNode *thenNode, *elseNode;
		GetThenAndElseBranches(branchNode, &thenNode, &elseNode);
		if (thenNode == branch1)
		{
			other = elseNode;
		}
		else if (elseNode == branch1)
		{
			other = thenNode;
		}
		else
		{
			assert(false && "branch1 is not a successor of branchNode");
		}
	}
	return other;
}

bool IsThenBranch(ControlFlowNode *branchNode, ControlFlowNode *target)
{
	ControlFlowNode *thenNode, *elseNode;
	GetThenAndElseBranches(branchNode, &thenNode, &elseNode);
	return (target->GetStartingAddress() == thenNode->GetStartingAddress());
}

uint16_t ControlFlowNode::GetStartingAddress() const
{
	uint16_t address = 0xffff;
	if (Type == CFGNodeType::RawCode)
	{
		address = (static_cast<const RawCodeNode*>(this))->start->get_final_offset_dontcare();
	}
	else if (Type == CFGNodeType::Exit)
	{
		address = (static_cast<const ExitNode*>(this))->startingAddressForExit;
	}
	else if (Type == CFGNodeType::CommonLatch)
	{
		address = (static_cast<const CommonLatchNode*>(this))->tokenStartingAddress;
	}
	else if (Type == CFGNodeType::FakeBreakOrContinue)
	{
		address = 0;
	}
	else
	{
		assert((*this)[SemId::Head]);
		address = (*this)[SemId::Head]->GetStartingAddress();
	}
	return address;
}

ControlFlowNode *AdvanceToExit(ControlFlowNode *node)
{
	while (node && node->Type != CFGNodeType::Exit)
	{
		node = GetFirstSuccessorOrNull(node);
	}
	return node;
}

bool MaybeGetThenAndElseBranches(ControlFlowNode *node, ControlFlowNode **thenNode, ControlFlowNode **elseNode)
{
	*thenNode = nullptr;
	*elseNode = nullptr;
	if (node->Successors().size() == 2)
	{
		GetThenAndElseBranches(node, thenNode, elseNode);
		return (thenNode && elseNode);
	}
	return false;
}

// The address a branch target must match to select this successor. A common
// latch node stands for the loop head it feeds, so use that head address, not
// its sort-only token address.
static uint16_t BranchTargetAddress(ControlFlowNode *node)
{
	if (node->Type == CFGNodeType::CommonLatch)
	{
		return static_cast<CommonLatchNode*>(node)->headAddress;
	}
	return node->GetStartingAddress();
}

// Given a node that represents a raw code branch or a compound condition, returns
// which of its two successor nodes is the "then"  branch, and which is the "else"
void GetThenAndElseBranches(ControlFlowNode *node, ControlFlowNode **thenNode, ControlFlowNode **elseNode)
{
	*thenNode = nullptr;
	*elseNode = nullptr;
	if (node->Successors().size() != 2)
	{
		throw ControlFlowException(node, "Expected node with two successors (then/else)");
	}
	
	auto it = node->Successors().begin();
	ControlFlowNode *one = *it;
	ControlFlowNode *two = *(++it);

	if (node->Type == CFGNodeType::CompoundCondition)
	{
		CompoundConditionNode *ccNode = static_cast<CompoundConditionNode *>(node);
		uint16_t trueAddress = ccNode->thenBranch;
		if (trueAddress == one->GetStartingAddress())
		{
			*elseNode = two;
			*thenNode = one;
		}
		else
		{
			assert(trueAddress == two->GetStartingAddress());
			*elseNode = one;
			*thenNode = two;
		}
	}
	else if (node->Type == CFGNodeType::RawCode)
	{
		scii lastInstruction = node->getLastInstruction();
		if (lastInstruction.get_opcode() == Opcode::BNT)
		{
			uint16_t target = lastInstruction.get_branch_target()->get_final_offset();
			if (target == BranchTargetAddress(one))
			{
				*elseNode = one;
				*thenNode = two;
			}
			else if (target == BranchTargetAddress(two))
			{
				*elseNode = two;
				*thenNode = one;
			}
			// A synthesized break/continue sits on the branch edge in place of the
			// loop exit the instruction still names. It has no address of its own.
			else if (one->Type == CFGNodeType::FakeBreakOrContinue)
			{
				*elseNode = one;
				*thenNode = two;
			}
			else if (two->Type == CFGNodeType::FakeBreakOrContinue)
			{
				*elseNode = two;
				*thenNode = one;
			}
			else
			{
				throw ControlFlowException(node, "Inconsistent then/else branches. Possible \"continue\" statement?");
			}
		}
		else if (lastInstruction.get_opcode() == Opcode::BT)
		{
			uint16_t target = lastInstruction.get_branch_target()->get_final_offset();
			if (target == BranchTargetAddress(one))
			{
				*elseNode = two;
				*thenNode = one;
			}
			else if (target == BranchTargetAddress(two))
			{
				*elseNode = one;
				*thenNode = two;
			}
			else if (one->Type == CFGNodeType::FakeBreakOrContinue)
			{
				*thenNode = one;
				*elseNode = two;
			}
			else if (two->Type == CFGNodeType::FakeBreakOrContinue)
			{
				*thenNode = two;
				*elseNode = one;
			}
			else
			{
				throw ControlFlowException(node, "Inconsistent then/else branches (bt).");
			}
		}
	}
	else if (node->Type == CFGNodeType::Invert)
	{
		ControlFlowNode *invThen, *invElse;
		GetThenAndElseBranches((*node)[SemId::Head], &invThen, &invElse);
		// Find the exit nodes
		invThen = AdvanceToExit(invThen);
		invElse = AdvanceToExit(invElse);

		if (invThen->GetStartingAddress() == one->GetStartingAddress())
		{
			assert(invElse->GetStartingAddress() == two->GetStartingAddress());
			*thenNode = two;
			*elseNode = one;
		}
		else
		{
			assert(invElse->GetStartingAddress() == one->GetStartingAddress());
			assert(invThen->GetStartingAddress() == two->GetStartingAddress());
			*thenNode = one;
			*elseNode = two;
		}
	}
}

bool IsExitTo(ControlFlowNode *node, uint16_t address)
{
	return node && (node->Type == CFGNodeType::Exit) && (node->GetStartingAddress() == address);
}

ControlFlowNode *GetLoopTestNode(const ControlFlowNode *loop, bool *headSide)
{
	*headSide = true;
	ControlFlowNode *follow = loop->MaybeGet(SemId::Follow);
	if (!follow)
	{
		return nullptr;
	}
	uint16_t exitAddress = follow->GetStartingAddress();
	ControlFlowNode *latch = loop->MaybeGet(SemId::Latch);
	ControlFlowNode *node = (*loop)[SemId::Head];
	for (int guard = 0; node && (guard < 32); guard++)
	{
		if (node->Successors().size() == 2)
		{
			for (ControlFlowNode *succ : node->Successors())
			{
				if (IsExitTo(succ, exitAddress))
				{
					return node;
				}
			}
			break;
		}
		if ((node->Successors().size() != 1) || (node == latch))
		{
			break;
		}
		// Only a value node (an or's join, an if used as a value) can precede
		// the test. Anything else is the start of the body.
		if ((node->Type != CFGNodeType::CompoundCondition) && (node->Type != CFGNodeType::If))
		{
			break;
		}
		node = *node->Successors().begin();
	}
	if (latch && (latch->Successors().size() == 2))
	{
		for (ControlFlowNode *succ : latch->Successors())
		{
			if (IsExitTo(succ, exitAddress))
			{
				*headSide = false;
				return latch;
			}
		}
	}
	return nullptr;
}

RawCodeNode::RawCodeNode(code_pos start) : ControlFlowNode(nullptr, CFGNodeType::RawCode, {}), start(start)
{
	DebugId = fmt::format("{:04x}:{}", start->get_final_offset_dontcare(), OpcodeToName(start->get_opcode(), start->get_first_operand()));
}


bool std::less<ControlFlowNode*>::operator() (const ControlFlowNode* lhs, const ControlFlowNode* rhs) const
{
	uint16_t lAddress = lhs->GetStartingAddress();
	uint16_t rAddress = rhs->GetStartingAddress();
	if (lAddress == rAddress)
	{
		if (lhs->Type == rhs->Type)
		{
			return lhs < rhs;
		}
		else
		{
			return lhs->Type < rhs->Type;
		}
	}
	else
	{
		return lAddress < rAddress;
	}
}
