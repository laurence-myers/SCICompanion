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

		// The target of a branch in the model: the target that the bytecode
		// has, or the next instruction for a stray branch; NoIndex for an
		// instruction that is not a branch.
		int Target(int i) const { return _insts[i].target; }
		// The target of a branch as the bytecode has it, also for a stray
		// branch.
		int BytecodeTarget(int i) const { return _insts[i].bytecodeTarget; }
		// A bt or bnt that Sierra's compiler made by mistake: it gets to its
		// target with fewer values on the stack than the fall-through into
		// the target, so the code there takes values that its path did not
		// push: the stack underflows with it, and not without it (Camelot
		// script 40, Rm40::handleEvent: "toss; bnt" into the arguments of a
		// call). The model reads it as a branch to the next instruction, a
		// no-op. It is never the last instruction.
		bool IsStray(int i) const { return _insts[i].stray; }
		// A toss outside the code of the function (scii::is_outside_function),
		// right before a ret, that takes a value that the function did not
		// push: a patch of a game goes to the "toss; ret" of a switch of
		// another function (the SQ4 copy in "patch", script 271). The ret
		// clears the stack, so the toss does nothing: the model reads it as
		// no instruction of the stack, and it ends no switch.
		bool IsSpareToss(int i) const { return _insts[i].spareToss; }
		// The target that the parser uses: the target, or an equal target
		// that the dialect pass chose (plan section 3.2).
		int ParseTarget(int i) const { return _insts[i].parseTarget; }
		// The target after the threading of the dialect pass only (the
		// target before an "or" form moved it).
		int ThreadedTarget(int i) const { return _insts[i].threadedTarget; }
		// The live branches that go to the instruction, in address order.
		const std::vector<int> &Sources(int i) const { return _insts[i].sources; }
		// A live branch goes to the instruction.
		bool IsLabel(int i) const { return !_insts[i].sources.empty(); }

		// Control can get to the instruction from the entry.
		bool IsLive(int i) const { return _insts[i].live; }
		// The first live instruction at or after i; Size() when there is none.
		int NextLive(int i) const;
		// A live branch that does nothing: a jmp over dead code only (or
		// over nothing, and not over the dead latch of a loop, which the jmp
		// leaves), a bt or bnt whose target is its fall-through, or a bt or
		// bnt right after a bt or bnt of the same kind to the same place
		// (only jmps that do nothing between them), when no other branch
		// goes to it or to those jmps (a bt to a bnt, or a bnt to a bt, can).
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

		// The switch of a toss: the push of the value that the toss takes;
		// NoIndex when there is none. A dead toss (each case returns) has the
		// head of the switch whose cases jump to it.
		int SwitchHead(int toss) const;
		// The live tosses, and the dead tosses that have a head, in address
		// order.
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
			int threadedTarget = NoIndex;
			int bytecodeTarget = NoIndex;
			bool stray = false;
			bool spareToss = false;
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
		void _FindSpareTosses();
		void _FindStrayBranches();
		void _FindNaryCompares();
		void _FindLoops();
		void _FindSwitches();
		void _FindDeadSwitches();
		bool _IsLoopEnd(int head, int after) const;
		void _ApplyDialect();

		std::vector<Inst> _insts;
		std::vector<int> _loopHeads;
		std::map<int, std::vector<int>> _backBranches;
		std::vector<int> _tosses;
		bool _underflow = false;
	};
}
