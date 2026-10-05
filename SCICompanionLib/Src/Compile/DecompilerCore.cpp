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
#include "PMachine.h"
#include "ScriptOMAll.h"
#include "scii.h"
#include "DisassembleHelper.h"
#include "ScopeParser.h"
#include "ScopeVerify.h"
#include "DecompilerNew.h"
#include "DecompilerAstPasses.h"
#include "DecompilerFallback.h"
#include "SCISourceCodeFormatter.h"
#include "format.h"
#include "DecompilerConfig.h"
#include "DecompilerResults.h"
#include <iterator>
#include "GameFolderHelper.h"
#include "Operators.h"

#define DEBUG_DECOMPILER 1

using namespace sci;
using namespace std;

BinaryOperator GetBinaryOpFromAssignment(AssignmentOperator assignment);

const char InvalidLookupError[] = "LOOKUP_ERROR";
const char RestParamName[] = "params";
const char SelfToken[] = "self";

ValueType _ScriptObjectTypeToPropertyValueType(ICompiledScriptSpecificLookups::ObjectType type)
{
	switch (type)
	{
	case ICompiledScriptSpecificLookups::ObjectTypeSaid:
		return ValueType::Said;
		break;
	case ICompiledScriptSpecificLookups::ObjectTypeString:
		return ValueType::String;
		break;
	case ICompiledScriptSpecificLookups::ObjectTypeClass:
		return ValueType::Token;
		break;
	}
	return ValueType::Token;
}


bool _IsVOIndexed(Opcode bOpcode)
{
	return !!(static_cast<BYTE>(bOpcode) & 0x08);
}
bool _IsVOPureStack(Opcode bOpcode)
{
	return !!(static_cast<BYTE>(bOpcode) & 0x04);
}
bool _IsVOStack(Opcode bOpcode)
{
	// It's a stack operation if it says it's a stack operation,
	// or if the accumulator is being used as an index.
	// WARNING PHIL: true for store, but maybe no load???
	return _IsVOPureStack(bOpcode) || _IsVOIndexed(bOpcode);
}
bool _IsVOStoreOperation(Opcode bOpcode)
{
	return ((static_cast<BYTE>(bOpcode) & 0x30) == 0x10);
}
bool _IsVOIncremented(Opcode bOpcode)
{
	return ((static_cast<BYTE>(bOpcode) & 0x30) == VO_INC_AND_LOAD);
}
bool _IsVODecremented(Opcode bOpcode)
{
	return ((static_cast<BYTE>(bOpcode) & 0x30) == VO_DEC_AND_LOAD);
}

std::string _GetPublicProcedureName(WORD wScript, WORD wIndex)
{
	std::stringstream ss;
	ss << "proc" << wScript << "_" << wIndex;
	return ss.str();
}

typedef std::list<scii>::reverse_iterator rcode_pos;

struct Fixup
{
	code_pos branchInstruction;
	WORD wTarget;
	bool fForward;
};

code_pos get_cur_pos(std::list<scii> &code)
{
	code_pos pos = code.end();
	--pos;
	return pos;
}

// A bnt of a fault of Sierra's compiler: the test of an empty last clause
// of a switch or a cond. Its target is bad, but the clause is empty, so the
// code meant the next instruction (at wNext; pNext points to it):
// - "eq?; bnt; toss" (the last case of a switch), with any target other
//   than the toss;
// - a target past the end of the code, before a toss, jmp or ret (the
//   last clause of a cond).
// The end of the code is the bound of the decode: the end of the script on
// the first decode of _DecodeFunction (and in FunctionCodeLength), the
// estimated end of the function on the second. So a bad target of a cond
// clause that is in the script, after the function, at the start of an
// instruction, gives no fix: the first decode goes on into the code after
// the function. The first decode cannot use the estimated end, which can come
// from a bogus export: with it, a correct bnt that the compiler threads past
// a jmp would look like a bad one.
static bool _IsSierraBadBranch(const SCIVersion &sciVersion, Opcode previous, Opcode bOpcode, uint16_t wTarget, uint16_t wNext, uint16_t codeLength, const BYTE *pNext, const BYTE *pEnd)
{
	if ((bOpcode != Opcode::BNT) || (pNext >= pEnd) || (wTarget == wNext))
	{
		return false;
	}
	Opcode next = RawToOpcode(sciVersion, *pNext);
	if ((next == Opcode::TOSS) && (previous == Opcode::EQ))
	{
		return true;
	}
	return (wTarget > codeLength) && ((next == Opcode::TOSS) || (next == Opcode::JMP) || (next == Opcode::RET));
}

//
// pBegin/pEnd - bounding pointers for the raw byte code
// wBaseOffset - byte offset in script file where pBegin is (used to calculate absolute code offsets)
// code		- (out) list of sci instructions.
// outside	- the code of the script, or null. A branch out of the function to
//			  that code (a patch of a game: the KQ4 copy in "patch\NEW" puts a
//			  new init before a jmp to the old one) makes the decode read the
//			  code there too, as the tail of the function: each part of it
//			  until a ret (or a jmp out of the part) that no branch of the
//			  part goes past, right after a jmp that goes there, else after
//			  the code of the function. Its instructions are "outside the
//			  function".
//
// Returns the end of the code of the function. With abortOnError, a problem
// gives nullptr and no message (results can be null); else results gets the
// message.
const BYTE *_ConvertToInstructions(const SCIVersion &sciVersion, IDecompilerResults *results, std::list<scii> &code, const BYTE *pBegin, const BYTE *pEnd, WORD wBaseOffset, bool abortOnError, const OutsideCode *outside)
{
	std::unordered_map<WORD, code_pos> referenceToCodePos;
	std::vector<Fixup> branchTargetsToFixup;

	code_pos undetermined = code.end();

	// The code of the function, then each part outside it: its bytes, and its
	// start as a position from pBegin (uint16 positions wrap, so each address
	// of the script has one position).
	struct Part
	{
		const BYTE *pBegin;
		const BYTE *pEnd;
		uint16_t wStart;
		// The jmp that its instructions come after in the list; code.end(): they go at the end.
		code_pos afterJmp;
	};
	std::vector<Part> parts = { { pBegin, pEnd, 0, code.end() } };
	const size_t MaxParts = 9;
	// The address of a position, and the code section of the script that has
	// it as a part outside the function (else nullptr).
	auto outsideSection = [&](uint16_t wTarget) -> const CodeSection *
	{
		if (!outside || !outside->sections)
		{
			return nullptr;
		}
		uint16_t address = (uint16_t)(wTarget + wBaseOffset);
		for (const CodeSection &section : *outside->sections)
		{
			if ((address >= section.begin) && (address < section.end))
			{
				return &section;
			}
		}
		return nullptr;
	};

	uint16_t codeLength = (uint16_t)(pEnd - pBegin);
	const BYTE *pFunctionEnd = pBegin;
	// A bogus instruction after the ret at the end of the code of the function.
	code_pos functionPadding = code.end();
	for (size_t part = 0; part < parts.size(); ++part)
	{
		const BYTE *pPartEnd = parts[part].pEnd;
		uint16_t wPartStart = parts[part].wStart;
		uint16_t wPartLength = (uint16_t)(pPartEnd - parts[part].pBegin);
		code_pos insertAt = (parts[part].afterJmp == code.end()) ? code.end() : std::next(parts[part].afterJmp);
		code_pos cur = code.end();
		Opcode previous = Opcode::INDETERMINATE;
		if (referenceToCodePos.find(wPartStart) != referenceToCodePos.end())
		{
			// An earlier part has this code.
			continue;
		}
		// The targets in this part, as distances from its start.
		std::set<uint16_t> branchTargets;
		uint16_t wReferencePosition = wPartStart;
		const BYTE *pCur = parts[part].pBegin;
		while (pCur < pPartEnd)
		{
			if ((part > 0) && (referenceToCodePos.find(wReferencePosition) != referenceToCodePos.end()))
			{
				// The part goes on into code that an earlier part has: a jmp
				// there, of no bytes, keeps the order of the list.
				cur = code.insert(insertAt, scii(sciVersion, Opcode::JMP, undetermined, true, -1));
				Fixup fixup = { cur, wReferencePosition, false };
				branchTargetsToFixup.push_back(fixup);
				cur->set_offset_and_size(wReferencePosition + wBaseOffset, 0);
				cur->set_outside_function();
				break;
			}
			const BYTE *pThisInstruction = pCur;
			BYTE bRawOpcode = *pCur;
			bool bByte = (*pCur) & 1;
			Opcode bOpcode = RawToOpcode(sciVersion, bRawOpcode);
			assert(bOpcode <= Opcode::LastOne);
			++pCur; // Advance past opcode.
			uint16_t wOperands[3];
			ZeroMemory(wOperands, sizeof(wOperands));
			int cIncr = 0;
			bool fTruncatedOperand = false;

			for (int i = 0; i < 3; i++)
			{
				OperandType opType = GetOperandTypes(sciVersion, bOpcode)[i];
				cIncr = GetOperandSize(bRawOpcode, opType, pCur, pPartEnd);
				if ((cIncr != 0) && ((pPartEnd - pCur) < (ptrdiff_t)cIncr))
				{
					// The operand runs past the end of the code. Stop before we read
					// it, so we never index past the script resource buffer.
					fTruncatedOperand = true;
					break;
				}
				switch (cIncr)
				{
				case 1:
					// We may need to sign-extend this.
					if ((opType == otINT) || (opType == otINT8) || (opType == otLABEL))
					{
						wOperands[i] = (uint16_t)(int16_t)(int8_t)*pCur;
					}
					else
					{
						wOperands[i] = (uint16_t)*pCur;
					}
					break;
				case 2:
					wOperands[i] = *((uint16_t*)pCur); // REVIEW
					break;
				default:
					break;
				}
				pCur += cIncr;
				if (cIncr == 0) // No more operands
				{
					break;
				}
			}
			if (fTruncatedOperand)
			{
				// A truncated final instruction. On the abort pass, fail so the caller
				// retries with a tighter bound; otherwise report it and stop decoding,
				// leaving the already-decoded instructions (and their fixups) intact.
				if (abortOnError)
				{
					return nullptr;
				}
				results->AddResult(DecompilerResultType::Warning,
					fmt::format("Truncated instruction at 0x{0:04x}; stopping decode.", (uint16_t)(wReferencePosition + wBaseOffset)));
				break;
			}
			uint16_t wSize = (uint16_t)(pCur - pThisInstruction);
			// A jmp out of the part: the part can end there, as at a ret.
			bool fLeavesPart = false;
			// Add the instruction - use the constructor that takes all arguments, even if
			// not all are valid.
			if ((bOpcode == Opcode::BNT) || (bOpcode == Opcode::BT) || (bOpcode == Opcode::JMP))
			{
				// +1 because its the operand start pos.
				uint16_t wTarget = CalcOffset(sciVersion, wReferencePosition + 1, wOperands[0], bByte, bRawOpcode);
				uint16_t wNext = wReferencePosition + wSize;
				// The distances from the start of this part.
				uint16_t wHere = (uint16_t)(wReferencePosition - wPartStart);
				uint16_t wThere = (uint16_t)(wTarget - wPartStart);
				// In the code of the function (the first part goes to codeLength:
				// a target there is a later instruction of the function).
				bool inFunction = (wTarget <= codeLength);
				bool inPart = (part == 0) ? inFunction : (wThere <= wPartLength);
				const CodeSection *section = nullptr;
				fLeavesPart = (bOpcode == Opcode::JMP) && !inPart;

				if (_IsSierraBadBranch(sciVersion, previous, bOpcode, wTarget, wNext, (part == 0) ? codeLength : (uint16_t)0xffff, pCur, pPartEnd))
				{
					cur = code.insert(insertAt, scii(sciVersion, bOpcode, undetermined, true, -1));
					cur->set_bad_branch_target(wTarget + wBaseOffset);
					Fixup fixup = { cur, wNext, true };
					branchTargetsToFixup.push_back(fixup);
					branchTargets.insert((uint16_t)(wNext - wPartStart));
				}
				else if (inPart || inFunction)
				{
					cur = code.insert(insertAt, scii(sciVersion, bOpcode, undetermined, true, -1));
					// A branch from a part outside the function to the function goes back.
					bool fForward = inPart && (wThere > wHere) && ((part == 0) || !inFunction);
					Fixup fixup = { cur, wTarget, fForward };
					branchTargetsToFixup.push_back(fixup);
					if (fForward)
					{
						branchTargets.insert(wThere);
					}
				}
				else if ((parts.size() < MaxParts) && ((section = outsideSection(wTarget)) != nullptr))
				{
					// A part outside the function: it comes after the parts so far.
					cur = code.insert(insertAt, scii(sciVersion, bOpcode, undetermined, true, -1));
					Fixup fixup = { cur, wTarget, true };
					branchTargetsToFixup.push_back(fixup);
					uint16_t address = (uint16_t)(wTarget + wBaseOffset);
					// A part that a jmp goes to comes right after the jmp, which then does nothing (the
					// KQ4 copy in "patch\NEW", script 0, gSound::play: "bt; jmp; ret").
					parts.push_back({ outside->pScript + address, outside->pScript + section->end, wTarget, (bOpcode == Opcode::JMP) ? cur : code.end() });
				}
				else if (abortOnError)
				{
					return nullptr;
				}
				else
				{
					// This goes out of bounds, and it is no bnt of a fault of Sierra's compiler, and no code of
					// the script. We can't intelligently reason about where the branch is supposed to point, so just
					// replace it with a load operation.
					// We need to make sure it's the same size though.
					cur = code.insert(insertAt, scii(sciVersion, Opcode::LDI, 0xbaad, -1));
					results->AddResult(DecompilerResultType::Warning, fmt::format("Bad branch at 0x{0:4x}, replaced with -17747.", (uint16_t)(wReferencePosition + wBaseOffset)));
				}
			}
			else
			{
				cur = code.insert(insertAt, scii(sciVersion, bOpcode, wOperands[0], wOperands[1], wOperands[2], -1));
			}

			// Store the position of the instruction we just added:
			referenceToCodePos[wReferencePosition] = cur;
			// Store the actual offset in the instruction itself:
			cur->set_offset_and_size(wReferencePosition + wBaseOffset, wSize);
			if (part > 0)
			{
				cur->set_outside_function();
			}
			previous = bOpcode;

			// Attempt to detect the end of the part. If we encounter a return statement (or a jmp out of the part) and
			// it's equal to or beyond each target in the part, then we have reached the end.
			if ((bOpcode == Opcode::RET) || fLeavesPart)
			{
				if (branchTargets.empty() || ((*branchTargets.rbegin()) <= (uint16_t)(wReferencePosition - wPartStart)))
				{
					// We've reached the end
					break;
				}
			}

			wReferencePosition += wSize;
		}
		if (part == 0)
		{
			pFunctionEnd = pCur;
			if ((cur != code.end()) && (cur != code.begin()) && (std::prev(cur)->get_opcode() == Opcode::RET) && (cur->get_opcode() != Opcode::RET))
			{
				functionPadding = cur;
			}
		}
	}

	// Now fixup any branches.
	for (size_t i = 0; i < branchTargetsToFixup.size(); i++)
	{
		Fixup &fixup = branchTargetsToFixup[i];
		std::unordered_map<WORD, code_pos>::iterator it = referenceToCodePos.find(fixup.wTarget);
		if (it != referenceToCodePos.end())
		{
			fixup.branchInstruction->set_branch_target(it->second, fixup.fForward);
		}
		else
		{
			// The first try (abortOnError) fails with no message: the caller
			// tries again with a tighter bound, and that try can work.
			if (!abortOnError)
			{
				results->AddResult(DecompilerResultType::Error, "Invalid branch target.");
			}
			return nullptr;
		}
	}

	// Special hack... function code is placed at even intervals.  That means there might be an extra bogus
	// instruction at the end, just after a ret statement (often it is Opcode::BNOT, which is 0).  If the instruction
	// before the end of the code of the function is a ret instruction, remove the last instruction (unless it's also
	// a ret - since sometimes functions will end with two RETs, both of which are jump targets).
	if (functionPadding != code.end())
	{
		code.erase(functionPadding);
	}

	return pFunctionEnd;
}

bool _IsVariableUse(SCIVersion version, code_pos pos, VarScope varScope, WORD &wIndex)
{
	bool fRet = false;
	Opcode bOpcode = pos->get_opcode();
	if (GetOperandTypes(version, bOpcode)[0] == otVAR)
	{
		// It's a variable opcode.
		wIndex = pos->get_first_operand();
		fRet = (static_cast<VarScope>(static_cast<BYTE>(bOpcode) & 0x03) == varScope);
	}
	else if (bOpcode == Opcode::LEA)
	{
		// The "load effective address" instruction
		uint16_t wType = pos->get_first_operand();
		fRet = (static_cast<VarScope>(static_cast<BYTE>((wType >> 1))& 0x03) == varScope);
		if (fRet)
		{
			wIndex = pos->get_second_operand();
		}
	}
	return fRet;
}

void _FigureOutParameters(SCIVersion sciVersion, FunctionBase &function, FunctionSignature &signature, std::list<scii> &code)
{
	WORD wBiggest = 0;
	for (code_pos pos = code.begin(); pos != code.end(); ++pos)
	{
		WORD wIndex;
		if (_IsVariableUse(sciVersion, pos, VarScope::Param, wIndex))
		{
			wBiggest = max(wIndex, wBiggest);
		}
		else if (pos->get_opcode() == Opcode::REST)
		{
			// REST usage also indicates params.
			uint16_t restIndex = pos->get_first_operand();
			wBiggest = max(restIndex, wBiggest);
		}
	}
	if (wBiggest) // Parameters start at index 1, so 0 means no parameters
	{
		for (WORD i = 1; i <= wBiggest; i++)
		{
			std::unique_ptr<FunctionParameter> pParam = std::make_unique<FunctionParameter>();
			pParam->SetName(_GetParamVariableName(i));
			pParam->SetDataType("var"); // Generic for now... we could try to detect things in the future.
			signature.AddParam(std::move(pParam), false);   // false -> hard to detect if optional or not... serious code analysis req'd
		}
	}
}

//
// Scans the code for local variable usage, and adds "temp0, temp1, etc..." variables to function.
//
template<typename _TVarHolder>
void _FigureOutTempVariables(DecompileLookups &lookups, _TVarHolder &function, VarScope varScope, std::list<scii> &code)
{
	// Look for the link instruction.
	WORD cTotalVariableRoom = 0;
	for (code_pos pos = code.begin(); pos != code.end(); ++pos)
	{
		if (pos->get_opcode() == Opcode::LINK)
		{
			cTotalVariableRoom = pos->get_first_operand();
			break;
		}
	}

	if (cTotalVariableRoom)
	{
		// So, I don't know if I can put the things in initializers. That would involve extra work.
		// So I won't bother (I'll just leave the assignment ops)
		std::string methodTrackingName = GetMethodTrackingName(function.GetOwnerClass(), function);
		auto &localUsage = lookups.GetTempUsage(methodTrackingName);
		vector<VariableRange> varRanges;
		CalculateVariableRanges(localUsage, cTotalVariableRoom, varRanges);

		// Ok, now I have the ranges. Without doing extra work to find assignment opcodes that come at
		// the begining of the function, I'll just avoid putting any initializers in the variable decls.
		for (VariableRange &varRange : varRanges)
		{
			unique_ptr<VariableDecl> tempVar = std::make_unique<VariableDecl>();
			tempVar->SetDataType("var"); // For now...
			tempVar->SetName(_GetTempVariableName(varRange.index));
			tempVar->SetSize(varRange.arraySize);
			function.AddVariable(move(tempVar));
		}
	}
}

std::string _indent(int iIndent)
{
	std::string theFill;
	theFill.insert(theFill.begin(), iIndent, ' ');
	return theFill;
}

Consumption _GetInstructionConsumption(scii &inst, DecompileLookups *lookups)
{
	Opcode bOpcode = inst.get_opcode();
	assert(bOpcode <= Opcode::LastOne);
	int cEatStack = 0;
	bool fChangesAcc = false;
	bool fEatsAcc = false;
	bool fPutsOnStack = false;

	switch (bOpcode)
	{
	case Opcode::SELF:
		cEatStack = inst.get_first_operand() / 2;
		fChangesAcc = true;
		break;

	case Opcode::SEND:
		cEatStack = inst.get_first_operand() / 2;
		fChangesAcc = true;
		fEatsAcc = true;
		break;

	case Opcode::SUPER:
		cEatStack = inst.get_second_operand() / 2;
		fChangesAcc = true;
		break;

	case Opcode::BNOT:
	case Opcode::NOT:
	case Opcode::NEG:
		fChangesAcc = true;
		fEatsAcc = true;
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
		fChangesAcc = true;
		fEatsAcc = true;
		cEatStack = 1;
		break;

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
		fChangesAcc = true;
		fEatsAcc = true;
		cEatStack = 1;
		break;

	case Opcode::BT:
	case Opcode::BNT:
		// These leave the accumulator intact. So we might want to put fChangesAcc here.
		// Some stuff decompiles incorrect (see MergePoly usage in SQ5)
		fEatsAcc = true;
		break;

	case Opcode::RET:
		if (lookups && lookups->FunctionDecompileHints.ReturnsValue)
		{
			fEatsAcc = true;
		}
		// Wreaks havoc
		// fEatsAcc = true; // But not always intentional...
		// Because we don't know the intent of the code, we can't really do anything here.
		// The acc could be valid, or garbage. So we can just do the "doesn't affect anything"
		// statement before the return here and call it good. SCI Studio and SCI Companion both compile ok with that
		// (though SCICompanion gives warnings).
		// e.g.
		//	return (5)
		// will become
		//	5				// SCICompanion gives warning here
		//	return
		break;

	case Opcode::JMP:
	case Opcode::LINK:
		break;

	case Opcode::LDI:
		fChangesAcc = true;
		break;

	case Opcode::PUSH:
		fEatsAcc = true;
		fPutsOnStack = true;
		break;

	case Opcode::PUSHI:
	case Opcode::PUSH0:
	case Opcode::PUSH1:
	case Opcode::PUSH2:
		fPutsOnStack = true;
		break;

	case Opcode::PUSHSELF:
		fPutsOnStack = true;
		break;


	case Opcode::TOSS:
		//fChangesAcc = true; // doesn't touch the acc, by definition (toss)
		cEatStack = 1;
		break;

	case Opcode::DUP:
		fPutsOnStack = true;
		break;

	case Opcode::CALL:
	case Opcode::CALLK:
	case Opcode::CALLB:
		cEatStack = inst.get_second_operand() / 2;
		cEatStack++; // the number didn't include the # of instructions push.
		fChangesAcc = true;
		break;

	case Opcode::CALLE:
		//fEatsAcc = true;
		cEatStack = inst.get_third_operand() / 2;
		cEatStack++; // Also a parameter count.
		fChangesAcc = true;
		break;

	case Opcode::CLASS:
	case Opcode::SELFID:
		fChangesAcc = true;
		break;

	case Opcode::PPREV:
		fPutsOnStack = true;
		break;

	case Opcode::REST:
		// Doesn't really affect anything
		break;

	case Opcode::LEA:
		fEatsAcc = !!(static_cast<BYTE>(inst.get_first_operand() >> 1) & LEA_ACC_AS_INDEX_MOD);
		fChangesAcc = true;
		break;

	case Opcode::PTOA:
	case Opcode::IPTOA:
	case Opcode::DPTOA:
		fChangesAcc = true;
		break;

	case Opcode::ATOP:
		fEatsAcc = true;
		fChangesAcc = true; 
		// Not technically, but it leaves the value in the accumulator, so
		// it's what people should look at.
		break;

	case Opcode::PTOS:
	case Opcode::IPTOS:
	case Opcode::DPTOS:
		fPutsOnStack = true;
		break;

	case Opcode::STOP:
		cEatStack = 1;
		break;

	case Opcode::LOFSA:
		fChangesAcc = true;
		break;

	case Opcode::LOFSS:
		fPutsOnStack = true;
		break;

	case Opcode::LineNumber:
	case Opcode::Filename:
		// SCI2+ only.
		// File and line number info. Doesn't do anything.
		break;

	default:
		assert((bOpcode >= Opcode::FirstLoadStore) && (bOpcode <= Opcode::LastLoadStore));
		// TODO: use our defines/consts
		if (_IsVOStoreOperation(bOpcode))
		{
			// Store operation
			if (_IsVOPureStack(bOpcode))
			{
				cEatStack = 1;
			}
			else
			{
				if (_IsVOStack(bOpcode))
				{
					cEatStack = 1;
				}
				fEatsAcc = true;
				fChangesAcc = true; // Not really, but leaves a valid thing in the acc.
			}
		}
		else
		{
			// Load operation
			if (_IsVOPureStack(bOpcode))
			{
				fPutsOnStack = true;
			}
			else
			{
				fChangesAcc = true;
			}
		}
		if (_IsVOIndexed(bOpcode))
		{
			fEatsAcc = true; // index is in acc
		}
		break;

	}

	Consumption cons;
	if (fEatsAcc)
	{
		cons.cAccConsume++;
	}
	if (fChangesAcc)
	{
		cons.cAccGenerate++;
	}
	cons.cStackConsume = cEatStack;
	if (fPutsOnStack)
	{
		cons.cStackGenerate++;
	}

	return cons;
}

class DetermineHexValues : public IExploreNode
{
public:
	DetermineHexValues(FunctionBase &func)
	{
		useHex.push(true);
		func.Traverse(*this);
	}

	void ExploreNode(SyntaxNode &node, ExploreNodeState state) override
	{
		// Set property values to hex if we should:
		if (state == ExploreNodeState::Pre)
		{
			PropertyValue *propValue = SafeSyntaxNode<PropertyValue>(&node);
			if (propValue && useHex.top())
			{
				propValue->_fHex = true;
				propValue->_fNegate = false;
			}
		}

		// Push a "hex context" if we are in a bitwise operation.
		string operation;
		BinaryOperator binaryoperation = BinaryOperator::None;
		UnaryOperator unaryOperator = UnaryOperator::None;
		BinaryOp *binaryOp = SafeSyntaxNode<BinaryOp>(&node);
		if (binaryOp)
		{
			binaryoperation = binaryOp->Operator;
		}
		UnaryOp *unaryOp = SafeSyntaxNode<UnaryOp>(&node);
		if (unaryOp)
		{
			unaryOperator = unaryOp->Operator;
		}
		Assignment *assignment = SafeSyntaxNode<Assignment>(&node);
		if (assignment)
		{
			binaryoperation = GetBinaryOpFromAssignment(assignment->Operator);
		}

		if (binaryoperation == BinaryOperator::BinaryOr ||
			binaryoperation == BinaryOperator::BinaryAnd ||
			binaryoperation == BinaryOperator::ExclusiveOr ||
			unaryOperator == UnaryOperator::BinaryNot ||
			binaryoperation == BinaryOperator::ShiftRight ||
			binaryoperation == BinaryOperator::ShiftLeft)
		{
			if (state == ExploreNodeState::Pre)
			{
				useHex.push(true);
			}
			else if (state == ExploreNodeState::Post)
			{
				useHex.pop();
			}
		}
		else
		{
			// Ignore certain "insignificant" syntax nodes
			if (state == ExploreNodeState::Pre)
			{
				useHex.push(false);
			}
			else if (state == ExploreNodeState::Post)
			{
				useHex.pop();
			}
		}
	}

private:
	stack<bool> useHex;
};

class ResolveCallSiteParameters : public IExploreNode
{
public:
	ResolveCallSiteParameters(DecompileLookups &decompileLookups, FunctionBase &func) : _lookups(decompileLookups)
	{
		func.Traverse(*this);
	}

	void ExploreNode(SyntaxNode &node, ExploreNodeState state) override
	{
		if (state == ExploreNodeState::Pre)
		{
			SendParam *send = SafeSyntaxNode<SendParam>(&node);
			if (send)
			{
				_lookups.GetDecompilerConfig()->ResolveMethodCallParameterTypes(*send);
			}
			ProcedureCall *procCall = SafeSyntaxNode<ProcedureCall>(&node);
			if (procCall)
			{
				_lookups.GetDecompilerConfig()->ResolveProcedureCallParameterTypes(*procCall);
			}
		}
	}

private:
	DecompileLookups &_lookups;
};

class DetermineNegativeValues : public IExploreNode
{
public:
	DetermineNegativeValues(FunctionBase &func)
	{
		useNeg.push(true);
		func.Traverse(*this);
	}

	void ExploreNode(SyntaxNode &node, ExploreNodeState state) override
	{
		// Set property values to hex if we should:
		if (state == ExploreNodeState::Pre)
		{
			PropertyValue *propValue = SafeSyntaxNode<PropertyValue>(&node);
			if (propValue && useNeg.top())
			{
				if ((propValue->GetType() == ValueType::Number) && (propValue->GetNumberValue() > 32768))
				{
					propValue->_fNegate = true;
				}
			}
		}

		// Push a "neg context" if we are in a signed comparison

		BinaryOperator binaryOperation = BinaryOperator::None;
		BinaryOp *binaryOp = SafeSyntaxNode<BinaryOp>(&node);
		if (binaryOp)
		{
			binaryOperation = binaryOp->Operator;
		}
		Assignment *assignment = SafeSyntaxNode<Assignment>(&node);
		if (assignment)
		{
			binaryOperation = GetBinaryOpFromAssignment(assignment->Operator);
		}

		if (binaryOperation == BinaryOperator::Add ||
			binaryOperation == BinaryOperator::Subtract ||
			binaryOperation == BinaryOperator::LessThan ||
			binaryOperation == BinaryOperator::GreaterThan ||
			binaryOperation == BinaryOperator::LessEqual ||
			binaryOperation == BinaryOperator::GreaterEqual)
		{
			if (state == ExploreNodeState::Pre)
			{
				useNeg.push(true);
			}
			else if (state == ExploreNodeState::Post)
			{
				useNeg.pop();
			}
		}
		else
		{
			if (state == ExploreNodeState::Pre)
			{
				useNeg.push(false);
			}
			else if (state == ExploreNodeState::Post)
			{
				useNeg.pop();
			}
		}
	}

private:
	stack<bool> useNeg;
};

void _DetermineIfFunctionReturnsValue(std::list<scii> code, DecompileLookups &lookups)
{
	// Look for return statements and see if they have any statements without side effects before them.
	code_pos cur = code.end();
	--cur;
	while (!lookups.FunctionDecompileHints.ReturnsValue && (cur != code.begin()))
	{
		if (cur->get_opcode() == Opcode::RET)
		{
			--cur;
			Opcode opcode = cur->get_opcode();
			switch (opcode)
			{
				// These instructions don't really have any effect other than
				// to put something in the accumulator. If they came right before a ret,
				// then it's safe to assume this function returns a value.
				// To be more complete, we could follow branches, as this is a common pattern that
				// won't be caught by us:
				// (if (blah)
				//	 5
				// (else 4)
				// return

				case Opcode::LDI:
				case Opcode::SELFID:
				case Opcode::BNOT:
				case Opcode::NOT:
				case Opcode::NEG:
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
				case Opcode::PTOA:
				case Opcode::LOFSA:
				case Opcode::CLASS:
				case Opcode::LEA:
					lookups.FunctionDecompileHints.ReturnsValue = true;
					break;

				default:
					// A plain load. A store or a ++/-- is a statement of its own
					// (the golden decompilations never return one).
					if ((opcode >= Opcode::LAG) && (opcode <= Opcode::LastLoadStore))
					{
						if (!_IsVOStoreOperation(opcode) && !_IsVOIncremented(opcode) &&
							!_IsVODecremented(opcode) && !_IsVOPureStack(opcode))
						{
							lookups.FunctionDecompileHints.ReturnsValue = true;
						}
					}
					// TODO: We could also check for zero parameter sends for known property selectors.
			}
		}
		else
		{
			--cur;
		}
	}
}

std::string GetUnknownClassName(uint16_t species)
{
	return fmt::format("Unknown_Class_{0}", species);
}

void _TrackExternalScriptUsage(std::list<scii> code, DecompileLookups &lookups)
{
	code_pos cur = code.end();
	--cur;
	while (cur != code.begin())
	{
		Opcode opcode = cur->get_opcode();
		switch (opcode)
		{
			case Opcode::CALLB:
				lookups.TrackUsingScript(0);
				break;

			case Opcode::CALLE:
				lookups.TrackUsingScript(cur->get_first_operand());
				break;

			case Opcode::CLASS:
			{
				uint16_t classIndex = cur->get_first_operand();
				uint16_t scriptNumber;
				if (lookups.GetSpeciesScriptNumber(classIndex, scriptNumber))
				{
					lookups.TrackUsingScript(scriptNumber);
				}
				if (lookups.LookupClassName(classIndex).empty())
				{
					// The species has no name: its defining script is not in
					// the game. Record it so a classdef is emitted for it.
					lookups.TrackUnknownSpecies(classIndex);
				}
				break;
			}

			case Opcode::LEA:
			{
				VarScope scope;
				_GetVariableNameFromCodePos(*cur, lookups, &scope);
				if (scope == VarScope::Global)
				{
					lookups.TrackUsingScript(0);
				}
				break;
			}

			default:
			{
				if ((opcode >= Opcode::FirstLoadStore) && (opcode <= Opcode::LastLoadStore))
				{
					VarScope scope;
					_GetVariableNameFromCodePos(*cur, lookups, &scope);
					if (scope == VarScope::Global)
					{
						lookups.TrackUsingScript(0);
					}
				}
			}
				break;
		}

		--cur;
	}
}

namespace
{
	// Stages of the scope engine. An error has the message of the stage that
	// failed, "[scope:<stage>:<id>]"; an exception that is not a ScopeError
	// gives "[scope:internal] <text>". where (when it is not null) gets the
	// place and the detail of a ScopeError, " at <offset>: <detail>" (with
	// no place: ": <detail>").
	sci::Status _ScopeStages(const std::function<void()> &stages, std::string *where = nullptr)
	{
		sci::Status status = sci::Guard("scope", [&]() -> sci::Status
		{
			try
			{
				stages();
			}
			catch (const scope::ScopeError &e)
			{
				if (where)
				{
					*where = (e.Offset() >= 0) ? fmt::format(" at {0:04x}: {1}", e.Offset(), e.Detail()) : (": " + e.Detail());
				}
				throw;
			}
			return sci::Ok();
		});
		if (!status && (status.error().code != sci::ErrorCode::Unsupported))
		{
			return sci::Fail(sci::ErrorCode::Internal, "[scope:internal] " + status.error().message);
		}
		return status;
	}

	// The scope engine: the control-flow stages, then the value stage, which
	// gives the statements of the function. code is the list of the
	// instructions as they were decoded.
	sci::Status _DecompileWithScope(FunctionBase &func, DecompileLookups &lookups, std::list<scii> &code, std::string *where)
	{
		return _ScopeStages([&]()
		{
			scope::CodeModel model(code);
			std::unique_ptr<scope::Region> root = scope::Parse(model);
			std::set<int> passedDeadBranches;
			scope::Verify(model, *root, &passedDeadBranches);
			OutputNewStructure(func, model, *root, code, passedDeadBranches, lookups);
			for (int i = 0; i < model.Size(); ++i)
			{
				if (model.IsStray(i))
				{
					string name = func.GetOwnerClass() ? (func.GetOwnerClass()->GetName() + "::" + func.GetName()) : func.GetName();
					std::string message = fmt::format(
						"{0}: the source leaves out the {1} at {2:04x}, a fault of Sierra's compiler. It goes to {3:04x} with fewer values on the stack than the code there takes.",
						name, OpcodeToName(model.Op(i), 0), model.Offset(i), model.Offset(model.BytecodeTarget(i)));
					lookups.DecompileResults().AddResult(DecompilerResultType::Warning, message);
					func.GetOwnerScript()->AddHeaderComment("WARNING: " + message);
				}
			}
		}, where);
	}
}

// The decode of a function: to the end of the script, else to the estimated
// end. The end of the code, or nullptr.
static const BYTE *_DecodeFunction(DecompileLookups &lookups, std::list<scii> &code, const BYTE *pBegin, const BYTE *pEstimatedMaxEnd, const BYTE *pScriptResourceEnd, WORD wBaseOffset)
{
	const BYTE *discoveredEnd = _ConvertToInstructions(lookups.GetVersion(), &lookups.DecompileResults(), code, pBegin, pScriptResourceEnd, wBaseOffset, true, lookups.GetOutsideCode());
	if (discoveredEnd == nullptr)
	{
		// If there were problems with that (say bogus branches that go somewhere incorrect), try a tighter bound.
		// We don't want to try the tight bound right away, because it might have been determined using bogus
		// exports in the export table (e.g. SQ5 does this in script 243).
		code.clear();
		discoveredEnd = _ConvertToInstructions(lookups.GetVersion(), &lookups.DecompileResults(), code, pBegin, pEstimatedMaxEnd, wBaseOffset, false, lookups.GetOutsideCode());
	}
	return discoveredEnd;
}

int FunctionCodeLength(const SCIVersion &version, const BYTE *pBegin, const BYTE *pScriptResourceEnd, uint16_t wBaseOffset)
{
	std::list<scii> code;
	const BYTE *end = _ConvertToInstructions(version, nullptr, code, pBegin, pScriptResourceEnd, wBaseOffset, true, nullptr);
	return end ? (int)(end - pBegin) : -1;
}

bool ReadFunctionCode(DecompileLookups &lookups, const BYTE *pBegin, const BYTE *pEstimatedMaxEnd, const BYTE *pScriptResourceEnd, WORD wBaseOffset, std::list<scii> &code, bool &returnsValue)
{
	code.clear();
	returnsValue = false;
	if (!_DecodeFunction(lookups, code, pBegin, pEstimatedMaxEnd, pScriptResourceEnd, wBaseOffset))
	{
		return false;
	}
	code.insert(code.begin(), scii(lookups.GetVersion(), Opcode::INDETERMINATE, -1));
	lookups.FunctionDecompileHints.Reset();
	_DetermineIfFunctionReturnsValue(code, lookups);
	returnsValue = lookups.FunctionDecompileHints.ReturnsValue;
	lookups.FunctionDecompileHints.Reset();
	return true;
}

// The decompile of one function; the caller sends its report line.
static void _DecompileRawBody(FunctionBase &func, DecompileLookups &lookups, const BYTE *pBegin, const BYTE *pEstimatedMaxEnd, const BYTE *pScriptResourceEnd, WORD wBaseOffset, DecompiledFunction &report)
{
	lookups.EndowWithFunction(&func);

	// Take the raw data, and turn it into a list of scii instructions, and make sure the branch targets point to code_pos's
	std::list<scii> code;
	const BYTE *discoveredEnd = _DecodeFunction(lookups, code, pBegin, pEstimatedMaxEnd, pScriptResourceEnd, wBaseOffset);

	bool success = false;
	if (discoveredEnd)
	{
		// A placeholder at the start of the code: the scope CodeModel skips it,
		// and the list scans below use it.
		code.insert(code.begin(), scii(lookups.GetVersion(), Opcode::INDETERMINATE, -1));
		if (lookups.DebugControlFlow)
		{
			report.scopeTree = scope::ParseForDump(code);
			string trackingName = GetMethodTrackingName(func.GetOwnerClass(), func, true);
			if (!lookups.pszDebugFilter || PathMatchSpec(trackingName.c_str(), lookups.pszDebugFilter))
			{
				lookups.DecompileResults().AddResult(DecompilerResultType::Debug,
					fmt::format("Scope: {0}\n{1}Code:\n{2}", trackingName, report.scopeTree, scope::CodeForDump(code)));
			}
		}
		for (const scii &inst : code)
		{
			if (inst.is_bad_branch())
			{
				string name = func.GetOwnerClass() ? (func.GetOwnerClass()->GetName() + "::" + func.GetName()) : func.GetName();
				std::string message = fmt::format(
					"{0}: the bnt at {1:04x} goes to {2:04x}, a fault of Sierra's compiler (the test of an empty last clause). When the test fails, the game goes there and can crash. The decompile goes on to the next instruction.",
					name, inst.get_final_offset_dontcare(), inst.get_bad_branch_target());
				lookups.DecompileResults().AddResult(DecompilerResultType::Warning, message);
				func.GetOwnerScript()->AddHeaderComment("WARNING: " + message);
			}
		}
		for (scii &inst : code)
		{
			Opcode opcode = inst.get_opcode();
			if (((opcode == Opcode::BNT) || (opcode == Opcode::BT) || (opcode == Opcode::JMP)) && !inst.is_outside_function() &&
				inst.get_branch_target()->is_outside_function())
			{
				string name = func.GetOwnerClass() ? (func.GetOwnerClass()->GetName() + "::" + func.GetName()) : func.GetName();
				std::string message = fmt::format(
					"{0}: the {1} at {2:04x} goes to {3:04x}, outside the code of the function (a patch of the game). The decompile reads the code there as the tail of the function.",
					name, OpcodeToName(opcode, 0), inst.get_final_offset_dontcare(), inst.get_branch_target()->get_final_offset_dontcare());
				lookups.DecompileResults().AddResult(DecompilerResultType::Warning, message);
				func.GetOwnerScript()->AddHeaderComment("WARNING: " + message);
			}
		}
		_DetermineIfFunctionReturnsValue(code, lookups);

		// Construct the function -> for now use procedure, but really should be method or proc
		unique_ptr<FunctionSignature> pSignature = std::make_unique<FunctionSignature>();
		_FigureOutParameters(lookups.GetVersion(), func, *pSignature, code);
		func.AddSignature(std::move(pSignature));

		_TrackExternalScriptUsage(code, lookups);

		if (!lookups.DecompileAsm)
		{
			std::string where;
			sci::Status scoped = _DecompileWithScope(func, lookups, code, &where);
			if (scoped)
			{
				success = true;
				report.scope = "ok";
				report.output = "scope";
			}
			else
			{
				report.scope = scoped.error().message;
				func.GetStatements().clear();
				lookups.ResetOnFailure();
				string className = func.GetOwnerClass() ? func.GetOwnerClass()->GetName() : "";
				lookups.DecompileResults().AddResult(DecompilerResultType::Warning,
					fmt::format("{0} {1}::{2}: {3}{4}", func.GetOwnerScript()->GetName(), className, func.GetName(), report.scope, where));
			}
		}
	}
	else
	{
		func.AddSignature(std::make_unique<FunctionSignature>());
		func.AddStatement(std::make_unique<sci::PropertyValue>("CorruptFunction_CantDetermineCodeBounds", ValueType::Token));
	}

	if (!success && !lookups.DecompileResults().IsAborted() && discoveredEnd)
	{
		// Disassemble the function instead.
		// First though, we need to remove all code:
		func.GetStatements().clear();
		lookups.ResetOnFailure();

		lookups.DecompileResults().AddResult(DecompilerResultType::Important, fmt::format("Falling back to disassembly for {0}", func.GetName()));
		DisassembleFallback(func, code.begin(), code.end(), lookups);
		report.output = "asm";
	}

	// Give some statistics.
	if (discoveredEnd)
	{
		lookups.DecompileResults().InformStats(success, discoveredEnd - pBegin);
	}
	if (!lookups.DecompileResults().IsAborted())
	{
		if (!discoveredEnd)
		{
			report.output = "corrupt";
		}
		report.byteCount = discoveredEnd ? (int)(discoveredEnd - pBegin) : 0;
	}

	if (!lookups.DecompileResults().IsAborted())
	{
		if (success)
		{
			if (lookups.DebugInstructionConsumption)
			{
				// The text before the AST passes, for diagnosing a pass.
				std::stringstream ss;
				sci::SourceCodeWriter writer(ss);
				if (auto *method = dynamic_cast<sci::MethodDefinition*>(&func))
				{
					OutputSourceCode_SCI(*method, writer);
				}
				else if (auto *proc = dynamic_cast<sci::ProcedureDefinition*>(&func))
				{
					OutputSourceCode_SCI(*proc, writer);
				}
				lookups.DecompileResults().AddResult(DecompilerResultType::Debug, "Before AST passes:\n" + ss.str());
			}
			AstPassOptions astOptions;
			RunDecompilerAstPasses(func, astOptions, &lookups.DecompileResults());
		}
		ResolveCallSiteParameters resolveCallSiteParameters(lookups, func);
		DetermineHexValues determineHexValues(func);
		DetermineNegativeValues determinedNegValues(func);

		_FigureOutTempVariables(lookups, func, VarScope::Temp, code);

		lookups.ResolveRestStatements();

		lookups.EndowWithFunction(nullptr);

		func.PruneExtraneousReturn();
	}
}

// pEnd can be the end of script data. I have added autodetection support.
void DecompileRaw(FunctionBase &func, DecompileLookups &lookups, const BYTE *pBegin, const BYTE *pEstimatedMaxEnd, const BYTE *pScriptResourceEnd, WORD wBaseOffset)
{
	DecompiledFunction report;
	report.script = lookups.GetScriptNumber();
	report.className = func.GetOwnerClass() ? func.GetOwnerClass()->GetName() : "";
	report.name = func.GetName();
	report.offset = wBaseOffset;
	report.index = lookups.FunctionCount++;
	try
	{
		_DecompileRawBody(func, lookups, pBegin, pEstimatedMaxEnd, pScriptResourceEnd, wBaseOffset, report);
	}
	catch (...)
	{
		// The script fails; the report still has a line for the function.
		if (!lookups.DecompileResults().IsAborted())
		{
			report.output = "error";
			lookups.DecompileResults().InformFunction(report);
		}
		throw;
	}
	// The line comes after the AST passes and the naming of the function.
	if (!lookups.DecompileResults().IsAborted())
	{
		lookups.DecompileResults().InformFunction(report);
	}
}

// Both for variable opcodes, and Opcode::LEA
std::string _GetVariableNameFromCodePos(const scii &inst, DecompileLookups &lookups, VarScope *pVarType, WORD *pwIndexOut)
{
	std::string name;
	BYTE bThing;
	WORD wIndex;
	if (inst.get_opcode() == Opcode::LEA)
	{
		bThing = (BYTE)inst.get_first_operand();
		bThing >>= 1;
		wIndex = inst.get_second_operand();
	}
	else
	{
		bThing = (BYTE)inst.get_opcode();
		wIndex = inst.get_first_operand();
		assert(bThing >= 64);
	}
	std::stringstream ss;
	switch (bThing & 0x3)
	{
	case VO_GLOBAL:
		{
			// global
			name = lookups.ReverseLookupGlobalVariableName(wIndex);
			if (name.empty())
			{
				ss << _GetGlobalVariableName(wIndex);
				name = ss.str();
			}
		}
		break;
	case VO_LOCAL:
		// local
		ss << _GetLocalVariableName(wIndex, lookups.GetScriptNumber());
		name = ss.str();
		break;
	case VO_TEMP:
		// temp
		ss << _GetTempVariableName(wIndex);
		name = ss.str();
		break;
	case VO_PARAM:
		// param
		name = lookups.LookupParameterName(wIndex);
		break;
	}

	if (pwIndexOut)
	{
		*pwIndexOut = wIndex;
	}
	if (pVarType)
	{
		*pVarType = static_cast<VarScope>(bThing & 0x3);
	}

	return name;
}

DecompileLookups::DecompileLookups(const IDecompilerConfig *config, const GameFolderHelper &helper, uint16_t wScript, GlobalCompiledScriptLookups *pLookups, IObjectFileScriptLookups *pOFLookups, ICompiledScriptSpecificLookups *pScriptThings, ILookupNames *pTextResource, IPrivateSpeciesLookups *pPrivateSpecies, IDecompilerResults &results) :
_wScript(wScript), _pLookups(pLookups), _pOFLookups(pOFLookups), _pScriptThings(pScriptThings), _pTextResource(pTextResource), _pPrivateSpecies(pPrivateSpecies), PreferLValue(false), _results(results), Helper(helper), DebugControlFlow(false), DebugInstructionConsumption(false), _config(config)
{

	// Track all the valid script/export combos, so we know when someone is calling an invalid one.
	for (CompiledScript *script : _pLookups->GetGlobalClassTable().GetAllScripts())
	{
		_scriptExistance.insert(script->GetScriptNumber());
		uint32_t scriptNumber = script->GetScriptNumber();
		// In the high word we'll put the script number.
		uint32_t index = 0;
		for (uint16_t theExport : script->GetExports())
		{
			if (theExport != 0)
			{
				uint32_t scriptAndExport = (scriptNumber << 16) | index;
				_scriptExportExistance.insert(scriptAndExport);
			}
			index++;
		}
	}
}

std::string DecompileLookups::LookupSelectorName(WORD wIndex)
{
	std::string name = _pLookups->LookupSelectorName(wIndex);
	// A selector with the name of a keyword of the syntax, which the text would give another
	// name (the SCI1.1 template has cond, 509, which the text would write as case, the name
	// of 732): sel_<number>, which the compiler reads back as the number.
	if ((name == "cond") || (name == "continue") || (name == "repeat"))
	{
		name = fmt::format("sel_{0}", wIndex);
	}
	return name;
}
std::string DecompileLookups::LookupKernelName(WORD wIndex)
{
	return _pLookups->LookupKernelName(wIndex);
}
std::string DecompileLookups::LookupClassName(WORD wIndex)
{
	std::string ret = _pLookups->LookupClassName(wIndex);
	if (ret.empty())
	{
		ret = _pPrivateSpecies->LookupClassName(wIndex);
	}
	return ret;
}
bool DecompileLookups::LookupSpeciesPropertyList(WORD wIndex, std::vector<WORD> &props)
{
	bool fRet = _pLookups->LookupSpeciesPropertyList(wIndex, props);
	if (!fRet)
	{
		fRet = _pPrivateSpecies->LookupSpeciesPropertyList(wIndex, props);
	}
	return fRet;
}
bool DecompileLookups::LookupSpeciesPropertyListAndValues(WORD wIndex, std::vector<WORD> &props, std::vector<CompiledVarValue> &values)
{
	bool fRet = _pLookups->LookupSpeciesPropertyListAndValues(wIndex, props, values);
	if (!fRet)
	{
		fRet = _pPrivateSpecies->LookupSpeciesPropertyListAndValues(wIndex, props, values);
	}
	return fRet;
}
std::string DecompileLookups::ReverseLookupGlobalVariableName(WORD wIndex)
{
	static std::string s_defaults[] = {
		"gEgo",
		"gGame",
		"gRoom",
		"gSpeed",
		"gQuitGame",
		"gCast",
		"gRegions",
		"gLocales",
		"gTimers",
		"gSounds",  // gInv, SQ
		"gInv",	 // gAddToPics...
		"gAddToPics",   // something passed to OwnedBy
		"gFeatures",
		"gSFeatures",
		"gRoomNumberExit",
		"gPreviousRoomNumber",
		"gRoomNumber",
		"gDebugOnExit",
		"gScore",
		"gMaxScore",
	};


	std::string result = _pOFLookups->ReverseLookupGlobalVariableName(wIndex);
	if (result.empty())
	{
		// Disable this and come up with a better solution that does this automatically
		/*
		// Supply some defaults.  These may be different for different games.
		if (wIndex < ARRAYSIZE(s_defaults))
		{
			result = s_defaults[wIndex];
		}*/
	}
	return result;
}
std::string DecompileLookups::ReverseLookupPublicExportName(WORD wScript, WORD wIndex)
{
	std::string ret = _pOFLookups->ReverseLookupPublicExportName(wScript, wIndex);
	if (ret.empty())
	{
		// This will be true if there are no .sco files.
		ret = _GetPublicProcedureName(wScript, wIndex);
	}
	return ret;
}
std::string DecompileLookups::LookupPropertyName(WORD wPropertyIndex)
{
	if (_pPropertyNames)
	{
		_requestedProperty = true;
		return _pPropertyNames->LookupPropertyName(this, wPropertyIndex);
	}
	else
	{
		return PropertyInNonMethodName;
	}
}
bool DecompileLookups::LookupPropertyName(uint16_t wPropertyIndex, std::string &name)
{
	if (_pPropertyNames)
	{
		_requestedProperty = true;
		name = _pPropertyNames->LookupPropertyName(this, wPropertyIndex);
		return true;
	}
	else
	{
		return false;
	}
}
bool DecompileLookups::LookupScriptThing(WORD wName, ICompiledScriptSpecificLookups::ObjectType &type, std::string &name) const
{
	return _pScriptThings->LookupObjectName(wName, type, name);
}

bool DecompileLookups::GetSpeciesScriptNumber(uint16_t species, uint16_t &scriptNumber)
{
	return _pLookups->GetGlobalClassTable().GetSpeciesScriptNumber(species, scriptNumber);
}

std::string DecompileLookups::LookupParameterName(WORD wIndex)
{
	if (wIndex)
	{
		const FunctionSignature &signature = *_pFunc->GetSignatures()[0];
		size_t iRealIndex = (wIndex - 1);
		assert(iRealIndex < signature.GetParams().size()); // Since it was us who analyzed the code and added the right # of params
		return signature.GetParams()[iRealIndex]->GetName();
	}
	else
	{
		return "paramTotal"; // parameter 0 is the count of params
	}
}

std::string DecompileLookups::LookupTextResource(WORD wIndex) const
{
	std::string ret;
	if (_pTextResource)
	{
		ret = _pTextResource->Lookup(wIndex);
	}
	return ret;
}

bool DecompileLookups::IsPropertySelectorOnly(uint16_t selector) const
{
	// Heuristic: a selector no object in the game uses as a method.
	const auto &propertySelectors = _pLookups->GetPropertySelectors();
	const auto &methodSelectors = _pLookups->GetMethodSelectors();
	return (propertySelectors.find(selector) != propertySelectors.end()) &&
		(methodSelectors.find(selector) == methodSelectors.end());
}

const SelectorTable& DecompileLookups::GetSelectorTable() const
{
	return _pLookups->GetSelectorTable();
}

bool DecompileLookups::DoesExportExist(uint16_t script, uint16_t theExport) const
{
	uint32_t scriptAndExport = (((uint32_t)script) << 16) | theExport;
	if (_scriptExportExistance.find(scriptAndExport) == _scriptExportExistance.end())
	{
		return false;
	}
	// An export with no procedure has no procedure in the decompiled text of
	// its script.
	return !_pLookups->IsExportSlotWithNoProcedure(script, theExport);
}

uint16_t DecompileLookups::GetNameSelector() const
{
	uint16_t nameSelector = 0;
	_pLookups->GetSelectorTable().ReverseLookup("name", nameSelector);
	return nameSelector;
}

void DecompileLookups::SetPosition(sci::SyntaxNode *pNode)
{
	pNode->SetPosition(_fakePosition);
	_fakePosition = LineCol(_fakePosition.Line() + 1, 0);
}

std::set<uint16_t> DecompileLookups::GetValidUsings()
{
	// Only return usings for scripts that actually exist.
	std::set<uint16_t> results;
	for (uint16_t script : _usings)
	{
		if (_scriptExistance.find(script) != _scriptExistance.end())
		{
			results.insert(script);
		}
	}
	return results;
}

const ILookupPropertyName *DecompileLookups::GetPossiblePropertiesForProc(uint16_t localProcOffset)
{
	auto it = _localProcToPropLookups.find(localProcOffset);
	if (it != _localProcToPropLookups.end())
	{
		return it->second;
	}
	return nullptr;
}

const SCIVersion &DecompileLookups::GetVersion()
{
	return Helper.Version;
}

void DecompileLookups::EndowWithFunction(sci::FunctionBase *pFunc)
{
	_pFunc = pFunc;
	FunctionDecompileHints.Reset();
	if (_pFunc)
	{
		_functionTrackingName = GetMethodTrackingName(_pFunc->GetOwnerClass(), *_pFunc);
	}
	else
	{
		_functionTrackingName.clear();
		_restStatementTrack.clear();
	}
}

const ClassDefinition *DecompileLookups::GetClassContext() const
{
	const MethodDefinition *method = SafeSyntaxNode<MethodDefinition>(_pFunc);
	if (method)
	{
		return method->GetOwnerClass();
	}
	return nullptr;
}

void DecompileLookups::TrackVariableUsage(VarScope varScope, WORD wIndex, bool isIndexed)
{
	map<WORD, bool> *pFunctionVarUsage = nullptr;
	if ((varScope == VarScope::Local) || ((GetScriptNumber() == 0) && (varScope == VarScope::Global)))
	{
		pFunctionVarUsage = &_localVarUsage;
	}
	else if (varScope == VarScope::Temp)
	{
		auto &it = _tempVarUsage.find(_functionTrackingName);
		if (it != _tempVarUsage.end())
		{
			pFunctionVarUsage = &(*it).second;
		}
		else
		{
			_tempVarUsage[_functionTrackingName] = map<WORD, bool>();
			pFunctionVarUsage = &_tempVarUsage[_functionTrackingName];
		}
	}

	if (pFunctionVarUsage)
	{
		// We can't just blindly set isIndexed. If a variable is used in both manners, we'll treat
		// it as indexed. So check if something is already there.
		auto &findIt = pFunctionVarUsage->find(wIndex);
		if (findIt != pFunctionVarUsage->end())
		{
			(*pFunctionVarUsage)[wIndex] = isIndexed || findIt->second;
		}
		else
		{
			(*pFunctionVarUsage)[wIndex] = isIndexed;
		}
	}
}

// REVIEW: Stuff that is tracked while decompiling should really be put elsewhere
// (i.e. not in DEcompileLookups, which should be read-only)
void DecompileLookups::ResetOnFailure()
{
	// This holds weak pointers to objects in the script, so clear those out.
	_restStatementTrack.clear();
}

void DecompileLookups::ReleaseDecompileState()
{
	_localVarUsage.clear();
	_tempVarUsage.clear();
	_restStatementTrack.clear();
	_localProcToPropLookups.clear();
	_pPropertyNames = nullptr;
	_outside = { nullptr, nullptr };
	_pFunc = nullptr;
	FunctionDecompileHints.Reset();
}

void DecompileLookups::TrackRestStatement(sci::RestStatement *rest, uint16_t index)
{
	_restStatementTrack.emplace_back(rest, index);
}

void DecompileLookups::ResolveRestStatements()
{
	for (auto &pair : _restStatementTrack)
	{
		uint16_t varIndex = pair.second;
		FunctionSignature &signature = *_pFunc->GetSignaturesNC()[0];
		size_t iRealIndex = (varIndex - 1);
		if (iRealIndex < signature.GetParams().size())
		{
			// There is already a named parameter for this guy. Use this name for the rest statement.
			pair.first->SetName(signature.GetParams()[iRealIndex]->GetName());
		}
		else
		{
			// The more common case. Use "params"
			pair.first->SetName(RestParamName);
			// Now, we need this in the function signature too. 
			// We shouldn't ever need to add more than one parameter.
			// REVIEW: This probably shouldn't be an assert, since it could theoretically happen.
			// But I'd like to leave it in for now until I know the code is correct
			assert(iRealIndex == signature.GetParams().size());
			signature.AddParam(RestParamName);
		}
	}
}

void DecompileLookups::TrackProcedureCall(uint16_t offset)
{
	auto it = _localProcToPropLookups.find(offset);
	if (it == _localProcToPropLookups.end())
	{
		_localProcToPropLookups[offset] = _pPropertyNames;
	}
	else
	{
		// This procedure is called from multiple contexts (e.g. multiple
		// objects or other procedures) so we can't say it belongs to one object.
		if (it->second != _pPropertyNames)
		{
			_localProcToPropLookups[offset] = nullptr;
		}
	}
}

void CalculateVariableRanges(const std::map<WORD, bool> &usage, WORD variableCount, vector<VariableRange> &varRanges)
{
	// (1) This first part of the code attempts to figure out which variables are arrays and which are not.
	// For the global script, we assume none of the variables are arrays. We can't determine their usage from just
	// main itself, because all scripts use them.
	VariableRange currentVarRange = { 0, 0 };
	bool hasVariableInProcess = false;
	bool isCurrentIndexed = false;
	auto &usageIterator = usage.cbegin();
	auto &endUsage = usage.cend();
	for (WORD i = 0; i < variableCount; i++)
	{
		if ((usageIterator != endUsage) && (usageIterator->first == i))
		{
			// We start a new variable here
			if (hasVariableInProcess)
			{
				// If we have something in process, add it now
				currentVarRange.arraySize = (i - currentVarRange.index);
				assert(isCurrentIndexed || (currentVarRange.arraySize == 1));
				varRanges.push_back(currentVarRange);
			}

			// Now start the new one
			currentVarRange.index = i;
			isCurrentIndexed = usageIterator->second;
			hasVariableInProcess = true;

			++usageIterator;
		}
		else
		{
			// This var index when never directly used.
			if (hasVariableInProcess)
			{
				// This is a new one, unless we're indexed
				if (!isCurrentIndexed)
				{
					currentVarRange.arraySize = 1;
					varRanges.push_back(currentVarRange);

					// But start a new one. isCurrentIndexed stays false.
					// Actually, let it be indexed. Otherwise unused arrays take
					// up tons of noise.
					isCurrentIndexed = true;
					currentVarRange.index = i;
					hasVariableInProcess = true;
				}
				// else if indexed, just continue
			}
			else
			{
				// Just make a single one
				// Actually, let's make an indexed one.
				isCurrentIndexed = true;
				currentVarRange.index = i;
				hasVariableInProcess = true;
				//varRanges.push_back(VariableRange() = { i, 1 });
			}
		}
	}

	// Need one at the end too
	if (hasVariableInProcess)
	{
		currentVarRange.arraySize = variableCount - currentVarRange.index;
		varRanges.push_back(currentVarRange);
	}
}

void AddLocalVariablesToScript(sci::Script &script, const CompiledScript &compiledScript, DecompileLookups &lookups, const std::vector<CompiledVarValue> &localVarValues)
{
	// Based on what we find in lookups, we should be able to deduce what is an array and what is not.
	// And we should be able to initialize things too. Default values are zero.
	vector<VariableRange> varRanges;
	if (script.GetScriptNumber() == 0)
	{
		for (size_t i = 0; i < localVarValues.size(); i++)
		{
			varRanges.push_back({ (uint16_t)i, 1 });
		}

	}
	else
	{
		CalculateVariableRanges(lookups.GetLocalUsage(), static_cast<WORD>(localVarValues.size()), varRanges);
	}

	// The next step is to supply values and initializers
	// Script local variables are zero-initialized by default. So we don't need to assign anything to them if the value is zero.
	for (VariableRange &varRange : varRanges)
	{
		unique_ptr<VariableDecl> localVar = std::make_unique<VariableDecl>();
		localVar->SetDataType("var"); // For now...
		localVar->SetName(_GetLocalVariableName(varRange.index, script.GetScriptNumber()));
		localVar->SetSize(varRange.arraySize);

		int wStart = varRange.index;
		int wEnd = varRange.index + varRange.arraySize;
		// Now we need to determine if we are setting any initializers.
		// We need to provide initializers all the way up to the last non-zero value.
		WORD firstAllZeroesFromHereIndex = wStart;
		for (int i = wEnd - 1; i >= wStart; i--)
		{
			if (localVarValues[i].value != 0)
			{
				firstAllZeroesFromHereIndex = static_cast<WORD>(i + 1);
				break;
			}
		}

		// We need to supply values from varRange.index to firstAllZeroesFromHereIndex
		for (WORD w = varRange.index; w < firstAllZeroesFromHereIndex; w++)
		{
			PropertyValue value;
			uint16_t propValue = localVarValues[w].value;
			if (localVarValues[w].isObjectOrString)
			{
				ValueType typeStringOrSaid;
				std::string theString = compiledScript.GetStringOrSaidFromOffset(propValue, typeStringOrSaid);
				value.SetValue(theString, typeStringOrSaid);
			}
			else
			{
				value.SetValue(propValue);
				if (propValue >= 32768)
				{
					// Probably intended to be negative.
					value.Negate();
				}
			}
			localVar->AddSimpleInitializer(value);
		}

		script.AddVariable(move(localVar));
	}
}
