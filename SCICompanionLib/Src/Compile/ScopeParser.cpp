#include "stdafx.h"
#include "ScopeParser.h"
#include "ScopeVerify.h"
#include "PMachine.h"
#include "format.h"

namespace scope
{
	namespace
	{
		// The loops of the parse: for each head, the latches of its loops,
		// the outermost one (the last back branch) first; and the step of a
		// for loop, by its latch.
		struct LoopSet
		{
			std::map<int, std::vector<int>> latches;
			std::map<int, int> steps;

			bool IsLatch(int branch) const
			{
				for (const auto &head : latches)
				{
					if (std::find(head.second.begin(), head.second.end(), branch) != head.second.end())
					{
						return true;
					}
				}
				return false;
			}
		};

		// A parse error at a branch: the retry of Parse reads the branch.
		class BranchError : public ScopeError
		{
		public:
			BranchError(const std::string &id, int offset, const std::string &detail, int branch) :
				ScopeError("parse", id, offset, detail), branch(branch) {}
			int branch;
		};

		// An if whose then-part the parser reads. At the top level of the
		// then-part, a bnt to the else entry is one more and-term.
		struct OpenIf
		{
			Region *region;
			int elseEntry;
		};

		// A loop that the parser reads.
		struct OpenLoop
		{
			int exit;
			int cont;
		};

		class Parser
		{
		public:
			Parser(const CodeModel &model, const LoopSet &loops) : _model(model), _loopSet(loops) {}

			std::unique_ptr<Region> Run()
			{
				return _Sequence(0, _model.Size(), nullptr);
			}

		private:
			[[noreturn]] void _Fail(const char *id, int branch) const
			{
				throw BranchError(id, _model.Offset(branch),
					fmt::format("{0} to {1:04x}", OpcodeToName(_model.Op(branch), 0), _model.Offset(_model.Target(branch))), branch);
			}

			// The sequence [lo, hi): code regions, loops, and a region for
			// each branch of the control flow.
			std::unique_ptr<Region> _Sequence(int lo, int hi, OpenIf *thenOf)
			{
				std::unique_ptr<Region> sequence = MakeSequence();
				int codeStart = NoIndex;
				auto flush = [&](int end)
				{
					if (codeStart != NoIndex)
					{
						sequence->items.push_back(MakeCode(codeStart, end - 1));
						codeStart = NoIndex;
					}
				};
				int i = lo;
				while (i < hi)
				{
					int latch = _LoopAt(i, hi);
					if (latch != NoIndex)
					{
						flush(i);
						sequence->items.push_back(_Loop(i, latch));
						i = latch + 1;
						continue;
					}
					// A dead jmp to a loop exit or continue point (after a
					// return, or at the exit of an inner loop) is a break or
					// continue: a branch can resolve through it.
					bool toExit = false;
					int level = (!_model.IsLive(i) && (_model.Op(i) == Opcode::JMP)) ? _LoopLevel(_model.Target(i), Arrival::Jump, toExit) : 0;
					if (level != 0)
					{
						flush(i);
						sequence->items.push_back(_LoopJump(i, level, toExit));
						++i;
						continue;
					}
					if (!_model.IsFlowBranch(i))
					{
						if (codeStart == NoIndex)
						{
							codeStart = i;
						}
						++i;
						continue;
					}
					flush(i);
					i = _Branch(i, hi, sequence, thenOf);
				}
				flush(hi);
				return sequence;
			}

			// The latch of the outermost loop at the head i that ends inside
			// the sequence; NoIndex when there is none.
			int _LoopAt(int i, int hi) const
			{
				auto found = _loopSet.latches.find(i);
				if ((found == _loopSet.latches.end()) || !_model.IsLive(i))
				{
					return NoIndex;
				}
				for (int latch : found->second)
				{
					if (latch < hi)
					{
						return latch;
					}
				}
				return NoIndex;
			}

			std::unique_ptr<Region> _Loop(int head, int latch)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::Loop);
				region->head = head;
				region->branch = latch;
				auto step = _loopSet.steps.find(latch);
				int stepStart = (step != _loopSet.steps.end()) ? step->second : NoIndex;
				_loops.push_back({ latch + 1, (stepStart != NoIndex) ? stepStart : head });
				region->body = _Sequence(head, (stepStart != NoIndex) ? stepStart : latch, nullptr);
				if (stepStart != NoIndex)
				{
					region->step = _Sequence(stepStart, latch, nullptr);
				}
				_loops.pop_back();
				return region;
			}

			// The region of the branch at p, in the sequence that ends at hi.
			// Returns the index after the region.
			int _Branch(int p, int hi, std::unique_ptr<Region> &sequence, OpenIf *thenOf)
			{
				Opcode op = _model.Op(p);
				int target = _model.ParseTarget(p);
				if (op == Opcode::BNT)
				{
					if ((target > p) && (target <= hi))
					{
						return _If(p, target, hi, *sequence);
					}
					if (_model.SameTarget(target, hi, Arrival::False))
					{
						sequence->items.push_back(_IfToEnd(p, hi, hi, ElseKind::None, 0));
						return hi;
					}
					if (thenOf && _model.SameTarget(target, thenOf->elseEntry, Arrival::False))
					{
						thenOf->region->terms.push_back(std::move(sequence));
						thenOf->region->tests.push_back(p);
						sequence = MakeSequence();
						return p + 1;
					}
				}
				else if (op == Opcode::BT)
				{
					if ((target > p) && (target <= hi))
					{
						sequence->items.push_back(_Or(p, target));
						return target;
					}
					if (_model.SameTarget(target, hi, Arrival::True))
					{
						sequence->items.push_back(_Or(p, hi));
						return hi;
					}
				}
				bool toExit = false;
				int level = _LoopLevel(target, CodeModel::ArrivalOf(op), toExit);
				if (level == 0)
				{
					_Fail("no-scope-for-target", p);
				}
				const OpenLoop &loop = _loops[_loops.size() - level];
				if (op == Opcode::BNT)
				{
					sequence->items.push_back(_IfToEnd(p, hi, toExit ? loop.exit : loop.cont, toExit ? ElseKind::Break : ElseKind::Continue, level));
					return hi;
				}
				sequence->items.push_back(_LoopJump(p, level, toExit));
				return p + 1;
			}

			// The loop (1 for the innermost one) whose exit or continue point
			// is the target, for the value in the accumulator; 0 when there is
			// none.
			int _LoopLevel(int target, Arrival arrival, bool &toExit) const
			{
				for (int level = 1; level <= (int)_loops.size(); ++level)
				{
					const OpenLoop &loop = _loops[_loops.size() - level];
					toExit = _model.SameTarget(target, loop.exit, arrival);
					if (toExit || _model.SameTarget(target, loop.cont, arrival))
					{
						return level;
					}
				}
				return 0;
			}

			// A break, continue, breakif or contif at the branch p.
			std::unique_ptr<Region> _LoopJump(int p, int level, bool toExit) const
			{
				bool conditional = (_model.Op(p) == Opcode::BT);
				RegionKind kind = conditional ? (toExit ? RegionKind::BreakIf : RegionKind::ContIf) : (toExit ? RegionKind::Break : RegionKind::Continue);
				std::unique_ptr<Region> region = std::make_unique<Region>(kind);
				region->branch = p;
				region->level = level;
				return region;
			}

			// A bnt to X inside (p, hi]: an if. A jmp J at X - 1 is the else
			// marker when it does something, it is not the latch of a loop,
			// and J is inside (X, hi] or is the end of the sequence.
			int _If(int p, int target, int hi, Region &sequence)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::If);
				region->tests.push_back(p);
				OpenIf open = { region.get(), target };
				int marker = target - 1;
				int elseEnd = NoIndex;
				if ((marker > p) && (_model.Op(marker) == Opcode::JMP) && !(_model.IsLive(marker) && _model.IsNoOp(marker)) && !_loopSet.IsLatch(marker))
				{
					int jumpTarget = _model.Target(marker);
					if ((jumpTarget > target) && (jumpTarget <= hi))
					{
						elseEnd = jumpTarget;
					}
					else if (_model.SameTarget(jumpTarget, hi, Arrival::Jump))
					{
						elseEnd = hi;
					}
				}
				if (elseEnd == NoIndex)
				{
					region->thenPart = _Sequence(p + 1, target, &open);
					sequence.items.push_back(std::move(region));
					return target;
				}
				region->thenPart = _Sequence(p + 1, marker, &open);
				region->elseKind = ElseKind::Else;
				region->branch = marker;
				region->elsePart = _Sequence(target, elseEnd, nullptr);
				sequence.items.push_back(std::move(region));
				return elseEnd;
			}

			// A bnt whose false value goes to elseEntry (the end of the
			// sequence, or the exit or the continue point of a loop): an if
			// whose then-part is the rest of the sequence, [p + 1, hi).
			std::unique_ptr<Region> _IfToEnd(int p, int hi, int elseEntry, ElseKind elseKind, int level)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::If);
				region->tests.push_back(p);
				region->elseKind = elseKind;
				region->level = level;
				OpenIf open = { region.get(), elseEntry };
				region->thenPart = _Sequence(p + 1, hi, &open);
				return region;
			}

			// A bt to the end of the or: its second operand is [p + 1, end).
			std::unique_ptr<Region> _Or(int p, int end)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::Or);
				region->branch = p;
				region->body = _Sequence(p + 1, end, nullptr);
				return region;
			}

			const CodeModel &_model;
			const LoopSet &_loopSet;
			std::vector<OpenLoop> _loops;
		};

		LoopSet _InitialLoops(const CodeModel &model)
		{
			LoopSet loops;
			for (int head : model.LoopHeads())
			{
				loops.latches[head].push_back(model.Latch(head));
			}
			return loops;
		}

		// After a parse error at a branch, a change of the loops that can
		// give a parse (plan section 3.2): split a loop at a back jmp J when
		// the branch goes to the instruction after J (a while that is the
		// first statement of a repeat); else a forward branch inside a loop
		// goes to the step of a for loop. False when neither applies.
		bool _Retry(const CodeModel &model, LoopSet &loops, int branch)
		{
			int target = model.Target(branch);
			for (auto &head : loops.latches)
			{
				int start = head.first;
				std::vector<int> &latches = head.second;
				for (int back : model.BackBranches(start))
				{
					if ((model.Op(back) == Opcode::JMP) && (start <= branch) && (branch < back) &&
						(model.NextLive(back + 1) == target) &&
						(std::find(latches.begin(), latches.end(), back) == latches.end()) &&
						(back < latches.front()))
					{
						latches.push_back(back);
						std::sort(latches.begin(), latches.end(), std::greater<int>());
						return true;
					}
				}
			}
			// The innermost loop that has the branch and the target.
			int bestHead = NoIndex;
			int bestLatch = NoIndex;
			for (const auto &head : loops.latches)
			{
				for (int latch : head.second)
				{
					if ((head.first <= branch) && (branch < target) && (target <= latch) &&
						((bestHead == NoIndex) || (head.first > bestHead) || ((head.first == bestHead) && (latch < bestLatch))))
					{
						bestHead = head.first;
						bestLatch = latch;
					}
				}
			}
			if ((bestLatch != NoIndex) && (loops.steps.find(bestLatch) == loops.steps.end()))
			{
				loops.steps[bestLatch] = target;
				return true;
			}
			return false;
		}
	}

	std::unique_ptr<Region> Parse(const CodeModel &model)
	{
		LoopSet loops = _InitialLoops(model);
		for (;;)
		{
			try
			{
				Parser parser(model, loops);
				return parser.Run();
			}
			catch (const BranchError &e)
			{
				if (!_Retry(model, loops, e.branch))
				{
					throw ScopeError(e.Stage(), e.Id(), e.Offset(), e.Detail());
				}
			}
		}
	}

	std::string ParseForDump(const std::list<scii> &code)
	{
		std::string tree;
		try
		{
			CodeModel model(code);
			std::unique_ptr<Region> root = Parse(model);
			tree = Dump(model, *root);
			Verify(model, *root);
			return tree;
		}
		catch (const ScopeError &e)
		{
			std::string text = fmt::format("{0} at {1:04x}: {2}\n", e.what(), e.Offset(), e.Detail());
			return text + tree;
		}
	}
}
