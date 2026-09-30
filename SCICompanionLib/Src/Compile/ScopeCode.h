#pragma once

#include <list>
#include <map>
#include <string>
#include <vector>
#include "Version.h"
#include "scii.h"
#include "Result.h"

// The code model of the scope engine (docs\decompiler-scope-parser\plan.md,
// sections 3.1 and 3.2): the instructions of one function in address order,
// with the facts that the scope parser reads. No stage edits an instruction.
namespace scope
{
	// A failure of a stage of the scope engine. The message is
	// "[scope:<stage>:<id>]"; the id is stable, so that a report can count
	// the failures of one kind. The offset is the address of the
	// instruction that stopped the stage, or -1. The detail is a text for a
	// person (it is not in the message).
	class ScopeError : public sci::DataError
	{
	public:
		ScopeError(const std::string &stage, const std::string &id, int offset, const std::string &detail = std::string());
		const std::string &Stage() const { return _stage; }
		const std::string &Id() const { return _id; }
		int Offset() const { return _offset; }
		const std::string &Detail() const { return _detail; }

	private:
		std::string _stage;
		std::string _id;
		int _offset;
		std::string _detail;
	};

	// No instruction: a branch target of an instruction that is not a branch,
	// or a resolution that goes around in a circle.
	const int NoIndex = -1;

	// The value in the accumulator when control arrives at an address, for
	// the resolution of a branch target (plan section 3.1).
	enum class Arrival
	{
		Jump,	// any value: follow each jmp
		True,	// a true value: follow jmp, take bt, fall through bnt
		False,	// a false value: follow jmp, take bnt, fall through bt
	};

	class CodeModel
	{
	public:
		// The instructions of one function, in address order. The
		// decompiler's placeholder (Opcode::INDETERMINATE) is left out.
		// Throws a ScopeError when a branch target is not an instruction of
		// the function.
		explicit CodeModel(const std::list<scii> &code);

		// The count of instructions. The index Size() is the end of the
		// function.
		int Size() const { return (int)_insts.size(); }
		const scii &At(int i) const { return *_insts[i].inst; }
		Opcode Op(int i) const { return _insts[i].op; }
		// The address of the instruction; the address after the last one
		// for Size().
		uint16_t Offset(int i) const;

		// bt, bnt or jmp.
		bool IsBranch(int i) const;
		// bt or bnt.
		bool IsConditional(int i) const;
		// eq?, ne?, gt?, ge?, lt?, le? and their unsigned forms.
		static bool IsCompare(Opcode op);
		// False for jmp and ret: control does not go to the next instruction.
		bool FallsThrough(int i) const;

		// The target of a branch as the bytecode has it; NoIndex for an
		// instruction that is not a branch.
		int Target(int i) const { return _insts[i].target; }
		// The target that the parser uses: the target, or an equal target
		// that the dialect pass chose (plan section 3.2).
		int ParseTarget(int i) const { return _insts[i].parseTarget; }
		// The live branches that go to the instruction, in address order.
		const std::vector<int> &Sources(int i) const { return _insts[i].sources; }
		// A live branch goes to the instruction.
		bool IsLabel(int i) const { return !_insts[i].sources.empty(); }

		// Control can get to the instruction from the entry.
		bool IsLive(int i) const { return _insts[i].live; }
		// The first live instruction at or after i; Size() when there is none.
		int NextLive(int i) const;
		// A live branch that does nothing: a jmp over dead code only (or
		// over nothing), or a bt or bnt whose target is its fall-through.
		bool IsNoOp(int i) const { return _insts[i].noOp; }
		// A bnt inside an n-ary compare ("cmp; bnt O; pprev"): part of the
		// value of the compare, not a branch of the control flow.
		bool IsInert(int i) const { return _insts[i].inert; }
		// A live branch that the control flow has: not a no-op, not inert.
		bool IsFlowBranch(int i) const;

		// Where control goes from the address, for the value in the
		// accumulator (plan section 3.1). Size() for the end of the
		// function; NoIndex when the branches go around in a circle.
		int Resolve(int i, Arrival arrival) const;
		// Two targets are one target for the arrival: they resolve to the
		// same place. False when one of them goes around in a circle.
		bool SameTarget(int a, int b, Arrival arrival) const;
		// The arrival that a branch gives its target: False for bnt, True
		// for bt, Jump for jmp.
		static Arrival ArrivalOf(Opcode op);

		// The stack depth before and after a live instruction (the count of
		// values that the function pushed); -1 for a dead instruction. Where
		// two paths give two depths, the depth is the smaller one: a break
		// out of a switch leaves the switch value on the stack.
		int DepthBefore(int i) const { return _insts[i].depthBefore; }
		int DepthAfter(int i) const;
		// The values that the instruction takes from the stack and puts on it.
		int Pops(int i) const { return _insts[i].pops; }
		int Pushes(int i) const { return _insts[i].pushes; }
		// Two paths give the instruction two depths.
		bool HasDepthConflict(int i) const { return _insts[i].depthConflict; }
		// An instruction takes more values than the stack has.
		bool HasStackUnderflow() const { return _underflow; }

		// A live branch to an address at or before it.
		bool IsBackBranch(int i) const;
		// The heads of the loops (the targets of back branches), in address
		// order.
		const std::vector<int> &LoopHeads() const { return _loopHeads; }
		// The back branches to a head, in address order, dead ones too (the
		// latch after a return at the end of the body is dead). The last one
		// is the latch of the loop of the head.
		const std::vector<int> &BackBranches(int head) const;
		// The last back branch to the head; NoIndex when it is no head.
		int Latch(int head) const;

		// The switch of a live toss: the push of the value that the toss
		// takes; NoIndex when there is none.
		int SwitchHead(int toss) const;
		// The live tosses, in address order.
		const std::vector<int> &Tosses() const { return _tosses; }

		// A text dump, one line for each instruction: the address, the
		// opcode and its operands, the depth, and the flags.
		std::string Dump() const;

	private:
		struct Inst
		{
			const scii *inst = nullptr;
			Opcode op;
			int target = NoIndex;
			int parseTarget = NoIndex;
			std::vector<int> sources;
			bool live = false;
			bool noOp = false;
			bool inert = false;
			int pops = 0;
			int pushes = 0;
			int depthBefore = -1;
			bool depthConflict = false;
			int switchHead = NoIndex;
		};

		void _FindLiveCode();
		void _FindNoOps();
		void _FindDepths();
		void _FindNaryCompares();
		void _FindLoops();
		void _FindSwitches();
		void _ApplyDialect();
		bool _IsLoopContinuation(int branch, int target) const;

		std::vector<Inst> _insts;
		std::vector<int> _loopHeads;
		std::map<int, std::vector<int>> _backBranches;
		std::vector<int> _tosses;
		bool _underflow = false;
	};
}
