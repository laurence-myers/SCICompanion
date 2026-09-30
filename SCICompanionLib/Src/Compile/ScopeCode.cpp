#include "stdafx.h"
#include "ScopeCode.h"
#include "PMachine.h"
#include "DecompilerCore.h"
#include "format.h"
#include <unordered_map>

namespace scope
{
	ScopeError::ScopeError(const std::string &stage, const std::string &id, int offset, const std::string &detail) :
		sci::DataError(fmt::format("[scope:{0}:{1}]", stage, id), sci::ErrorCode::Unsupported),
		_stage(stage), _id(id), _offset(offset), _detail(detail)
	{
	}

	CodeModel::CodeModel(const std::list<scii> &code)
	{
		std::unordered_map<const scii *, int> indexOf;
		for (const scii &inst : code)
		{
			if (inst.get_opcode() == Opcode::INDETERMINATE)
			{
				continue;
			}
			Inst entry;
			entry.inst = &inst;
			entry.op = inst.get_opcode();
			indexOf[&inst] = (int)_insts.size();
			_insts.push_back(entry);
		}
		for (Inst &entry : _insts)
		{
			if ((entry.op == Opcode::BT) || (entry.op == Opcode::BNT) || (entry.op == Opcode::JMP))
			{
				code_pos target = const_cast<scii *>(entry.inst)->get_branch_target();
				auto found = indexOf.find(&*target);
				if (found == indexOf.end())
				{
					throw ScopeError("code", "bad-branch-target", entry.inst->get_final_offset_dontcare());
				}
				entry.target = found->second;
				entry.parseTarget = found->second;
			}
			Consumption consumption = _GetInstructionConsumption(const_cast<scii &>(*entry.inst), nullptr);
			entry.pops = consumption.cStackConsume;
			entry.pushes = consumption.cStackGenerate;
		}

		_FindLiveCode();
		_FindNoOps();
		_FindDepths();
		_FindNaryCompares();
		_FindLoops();
		_FindSwitches();
		_ApplyDialect();
	}

	uint16_t CodeModel::Offset(int i) const
	{
		return (i < Size()) ? At(i).get_final_offset_dontcare() : 0xffff;
	}

	bool CodeModel::IsBranch(int i) const
	{
		Opcode op = Op(i);
		return (op == Opcode::BT) || (op == Opcode::BNT) || (op == Opcode::JMP);
	}

	bool CodeModel::IsConditional(int i) const
	{
		Opcode op = Op(i);
		return (op == Opcode::BT) || (op == Opcode::BNT);
	}

	bool CodeModel::IsCompare(Opcode op)
	{
		switch (op)
		{
		case Opcode::EQ:
		case Opcode::NE:
		case Opcode::GT:
		case Opcode::GE:
		case Opcode::LT:
		case Opcode::LE:
		case Opcode::UGT:
		case Opcode::UGE:
		case Opcode::ULT:
		case Opcode::ULE:
			return true;
		default:
			return false;
		}
	}

	bool CodeModel::FallsThrough(int i) const
	{
		Opcode op = Op(i);
		return (op != Opcode::JMP) && (op != Opcode::RET);
	}

	int CodeModel::NextLive(int i) const
	{
		while ((i < Size()) && !IsLive(i))
		{
			++i;
		}
		return i;
	}

	bool CodeModel::IsFlowBranch(int i) const
	{
		return IsBranch(i) && IsLive(i) && !IsNoOp(i) && !IsInert(i);
	}

	Arrival CodeModel::ArrivalOf(Opcode op)
	{
		switch (op)
		{
		case Opcode::BT:
			return Arrival::True;
		case Opcode::BNT:
			return Arrival::False;
		default:
			return Arrival::Jump;
		}
	}

	int CodeModel::Resolve(int i, Arrival arrival) const
	{
		int cur = i;
		for (int steps = 0; steps <= Size(); ++steps)
		{
			if ((cur < 0) || (cur >= Size()))
			{
				return cur;
			}
			Opcode op = Op(cur);
			if (op == Opcode::JMP)
			{
				cur = Target(cur);
			}
			else if ((op == Opcode::BNT) && (arrival != Arrival::Jump))
			{
				cur = (arrival == Arrival::False) ? Target(cur) : (cur + 1);
			}
			else if ((op == Opcode::BT) && (arrival != Arrival::Jump))
			{
				cur = (arrival == Arrival::True) ? Target(cur) : (cur + 1);
			}
			else
			{
				return cur;
			}
		}
		return NoIndex;
	}

	bool CodeModel::SameTarget(int a, int b, Arrival arrival) const
	{
		int resolvedA = Resolve(a, arrival);
		return (resolvedA != NoIndex) && (resolvedA == Resolve(b, arrival));
	}

	int CodeModel::DepthAfter(int i) const
	{
		const Inst &entry = _insts[i];
		if (entry.depthBefore < 0)
		{
			return -1;
		}
		return (std::max)(0, entry.depthBefore - entry.pops + entry.pushes);
	}

	bool CodeModel::IsBackBranch(int i) const
	{
		return IsBranch(i) && IsLive(i) && (Target(i) <= i);
	}

	const std::vector<int> &CodeModel::BackBranches(int head) const
	{
		static const std::vector<int> none;
		auto found = _backBranches.find(head);
		return (found != _backBranches.end()) ? found->second : none;
	}

	int CodeModel::Latch(int head) const
	{
		const std::vector<int> &branches = BackBranches(head);
		return branches.empty() ? NoIndex : branches.back();
	}

	int CodeModel::SwitchHead(int toss) const
	{
		return _insts[toss].switchHead;
	}

	void CodeModel::_FindLiveCode()
	{
		if (_insts.empty())
		{
			return;
		}
		std::vector<int> work = { 0 };
		_insts[0].live = true;
		while (!work.empty())
		{
			int i = work.back();
			work.pop_back();
			auto visit = [&](int next)
			{
				if ((next < Size()) && !_insts[next].live)
				{
					_insts[next].live = true;
					work.push_back(next);
				}
			};
			if (IsBranch(i))
			{
				visit(Target(i));
			}
			if (FallsThrough(i))
			{
				visit(i + 1);
			}
		}
		for (int i = 0; i < Size(); ++i)
		{
			if (IsLive(i) && IsBranch(i))
			{
				_insts[Target(i)].sources.push_back(i);
			}
		}
	}

	void CodeModel::_FindNoOps()
	{
		for (int i = 0; i < Size(); ++i)
		{
			if (IsLive(i) && IsBranch(i) && (Target(i) > i) && (Target(i) == NextLive(i + 1)))
			{
				_insts[i].noOp = true;
			}
		}
	}

	void CodeModel::_FindDepths()
	{
		if (_insts.empty())
		{
			return;
		}
		std::vector<int> work = { 0 };
		_insts[0].depthBefore = 0;
		while (!work.empty())
		{
			int i = work.back();
			work.pop_back();
			int after = _insts[i].depthBefore - _insts[i].pops + _insts[i].pushes;
			if (after < 0)
			{
				_underflow = true;
				after = 0;
			}
			auto visit = [&](int next)
			{
				if (next >= Size())
				{
					return;
				}
				Inst &entry = _insts[next];
				if (entry.depthBefore < 0)
				{
					entry.depthBefore = after;
					work.push_back(next);
				}
				else if (after != entry.depthBefore)
				{
					entry.depthConflict = true;
					if (after < entry.depthBefore)
					{
						entry.depthBefore = after;
						work.push_back(next);
					}
				}
			};
			if (IsBranch(i))
			{
				visit(Target(i));
			}
			if (FallsThrough(i))
			{
				visit(i + 1);
			}
		}
	}

	// "cmp; bnt O; pprev; ...; cmp" is one value when each O resolves, for a
	// false value, where the end of the chain resolves (plan section 3.2).
	void CodeModel::_FindNaryCompares()
	{
		std::vector<bool> inChain(_insts.size(), false);
		auto isLink = [&](int cmp)
		{
			return (cmp + 2 < Size()) &&
				(Op(cmp + 1) == Opcode::BNT) && IsLive(cmp + 1) &&
				(Op(cmp + 2) == Opcode::PPREV) && IsLive(cmp + 2) && !IsLabel(cmp + 2);
		};
		for (int i = 0; i < Size(); ++i)
		{
			if (!IsLive(i) || !IsCompare(Op(i)) || inChain[i] || !isLink(i))
			{
				continue;
			}
			std::vector<int> branches;
			int cmp = i;
			bool valid = true;
			while (valid && isLink(cmp))
			{
				branches.push_back(cmp + 1);
				int pprev = cmp + 2;
				int depth = DepthBefore(pprev);
				// The compare that takes the value of the pprev: the first
				// instruction after it that brings the stack back to depth.
				int next = NoIndex;
				for (int j = pprev + 1; j < Size(); ++j)
				{
					if (IsLive(j) && (DepthAfter(j) <= depth))
					{
						next = j;
						break;
					}
				}
				valid = (next != NoIndex) && IsCompare(Op(next)) && (DepthBefore(next) == depth + 1);
				if (valid)
				{
					cmp = next;
					inChain[cmp] = true;
				}
			}
			if (!valid)
			{
				continue;
			}
			int end = cmp + 1;
			for (int branch : branches)
			{
				valid = valid && SameTarget(Target(branch), end, Arrival::False);
			}
			if (valid)
			{
				for (int branch : branches)
				{
					_insts[branch].inert = true;
				}
			}
		}
	}

	// A head is the target of a live back branch. The back branches of a
	// head include a dead one: the latch of a loop whose body ends with a
	// return is dead, and the loop still ends there.
	void CodeModel::_FindLoops()
	{
		for (int i = 0; i < Size(); ++i)
		{
			if (IsBackBranch(i))
			{
				_backBranches[Target(i)];
			}
		}
		for (int i = 0; i < Size(); ++i)
		{
			if (IsBranch(i) && (Target(i) <= i) && (_backBranches.find(Target(i)) != _backBranches.end()))
			{
				_backBranches[Target(i)].push_back(i);
			}
		}
		for (const auto &loop : _backBranches)
		{
			_loopHeads.push_back(loop.first);
		}
	}

	// Each toss ends one switch. Its head is the push of the value that it
	// takes: the last instruction before it that takes the stack from the
	// depth after the toss to one more.
	void CodeModel::_FindSwitches()
	{
		for (int toss = 0; toss < Size(); ++toss)
		{
			if (!IsLive(toss) || (Op(toss) != Opcode::TOSS))
			{
				continue;
			}
			_tosses.push_back(toss);
			int depth = DepthBefore(toss) - 1;
			if (depth < 0)
			{
				continue;
			}
			for (int k = toss - 1; k >= 0; --k)
			{
				if (!IsLive(k) || (DepthBefore(k) > depth))
				{
					continue;
				}
				if ((DepthBefore(k) == depth) && (DepthAfter(k) == depth + 1) && (Pushes(k) == 1))
				{
					_insts[toss].switchHead = k;
				}
				break;
			}
		}
		_FindDeadSwitches();
		std::sort(_tosses.begin(), _tosses.end());
	}

	// A toss is dead when each case returns. Its switch starts before the
	// first branch to the toss (the dead jmp at the end of the first case
	// body): at the nearest push that a dup at the next depth follows, when
	// the stack stays above the depth of that push up to the branch.
	void CodeModel::_FindDeadSwitches()
	{
		for (int toss = 0; toss < Size(); ++toss)
		{
			if (IsLive(toss) || (Op(toss) != Opcode::TOSS))
			{
				continue;
			}
			int first = NoIndex;
			for (int b = 0; b < toss; ++b)
			{
				if (IsBranch(b) && (Target(b) == toss))
				{
					first = b;
					break;
				}
			}
			if (first == NoIndex)
			{
				continue;
			}
			int lowest = Size() + 1;
			for (int k = first - 1; k >= 0; --k)
			{
				if (!IsLive(k))
				{
					continue;
				}
				int after = DepthAfter(k);
				if ((Pushes(k) == 1) && (after == DepthBefore(k) + 1) && (k + 1 < Size()) && (Op(k + 1) == Opcode::DUP) &&
					IsLive(k + 1) && (DepthBefore(k + 1) == after) && (lowest >= after))
				{
					_insts[toss].switchHead = k;
					_tosses.push_back(toss);
					break;
				}
				lowest = (std::min)(lowest, DepthBefore(k));
			}
		}
	}

	bool CodeModel::_IsLoopContinuation(int branch, int target) const
	{
		for (const auto &loop : _backBranches)
		{
			int head = loop.first;
			int latch = loop.second.back();
			if ((head <= branch) && (branch <= latch) && ((target == head) || (target == latch + 1)))
			{
				return true;
			}
		}
		return false;
	}

	// The dialects (plan section 3.2). First the threading of Sierra's
	// optimiser: a bt or bnt to a branch of the same sense goes to the target
	// of that branch. Then the "or" forms of this repository's compiler: a bt
	// that goes just past a bnt goes to that bnt, and a bnt that goes just
	// past a forward bt goes to that bt (not a bt to a loop head or to the
	// instruction after a latch). Each is an equal target: the bnt lets a
	// true value through, the bt a false one.
	void CodeModel::_ApplyDialect()
	{
		for (int b = 0; b < Size(); ++b)
		{
			if (!IsConditional(b) || !IsFlowBranch(b))
			{
				continue;
			}
			int target = Target(b);
			for (int steps = 0; (steps < Size()) && (target != b) && IsFlowBranch(target) && (Op(target) == Op(b)); ++steps)
			{
				target = Target(target);
			}
			_insts[b].parseTarget = target;
		}
		for (int b = 0; b < Size(); ++b)
		{
			if (!IsConditional(b) || !IsFlowBranch(b))
			{
				continue;
			}
			int target = ParseTarget(b);
			int before = target - 1;
			if ((target <= b) || (before <= b) || !IsFlowBranch(before) || (Target(before) <= before))
			{
				continue;
			}
			if ((Op(b) == Opcode::BT) && (Op(before) == Opcode::BNT))
			{
				_insts[b].parseTarget = before;
			}
			else if ((Op(b) == Opcode::BNT) && (Op(before) == Opcode::BT) && !_IsLoopContinuation(before, Target(before)))
			{
				_insts[b].parseTarget = before;
			}
		}
	}

	std::string CodeModel::Dump() const
	{
		std::string text;
		for (int i = 0; i < Size(); ++i)
		{
			const scii &inst = At(i);
			std::string operands;
			if (IsBranch(i))
			{
				operands = fmt::format("{0:04x}", Offset(Target(i)));
				if (ParseTarget(i) != Target(i))
				{
					operands += fmt::format(" (parse {0:04x})", Offset(ParseTarget(i)));
				}
			}
			else
			{
				// The count of operands does not depend on the version.
				const OperandType *types = GetOperandTypes(sciVersion1_1, Op(i));
				int count = 0;
				while ((count < 3) && (types[count] != otEMPTY))
				{
					++count;
				}
				for (int k = 0; k < count; ++k)
				{
					operands += fmt::format("{0}{1}", (k == 0) ? "" : " ", inst.get_operand(k));
				}
			}
			std::string flags;
			if (!IsLive(i))
			{
				flags += " dead";
			}
			if (IsLabel(i))
			{
				flags += " label";
			}
			if (IsNoOp(i))
			{
				flags += " no-op";
			}
			if (IsInert(i))
			{
				flags += " inert";
			}
			if (IsBackBranch(i))
			{
				flags += " back";
			}
			if (HasDepthConflict(i))
			{
				flags += " depth-conflict";
			}
			if (SwitchHead(i) != NoIndex)
			{
				flags += fmt::format(" switch {0:04x}", Offset(SwitchHead(i)));
			}
			std::string depth = IsLive(i) ? fmt::format("{0}", DepthBefore(i)) : std::string("-");
			std::string line = fmt::format("{0:04x} {1:<6} {2:<18} d{3}{4}", Offset(i), OpcodeToName(Op(i), inst.get_first_operand()), operands, depth, flags);
			while (!line.empty() && (line.back() == ' '))
			{
				line.pop_back();
			}
			text += line + "\n";
		}
		return text;
	}
}
