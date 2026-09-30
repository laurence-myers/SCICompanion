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

			// A parse error in a switch. No change of the loops can help, so
			// Parse does not try again.
			[[noreturn]] void _FailAt(const char *id, int index) const
			{
				throw ScopeError("parse", id, _model.Offset(index), OpcodeToName(_model.Op(index), 0));
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
					int toss = _SwitchAt(i, hi);
					if (toss != NoIndex)
					{
						flush(i);
						sequence->items.push_back(_Switch(i, toss));
						i = toss + 1;
						continue;
					}
					// A dead branch (after a return, or at the exit of a loop
					// whose exit is threaded past it) is a branch when it has a
					// place: another branch can resolve through it. Else it is
					// dead code.
					if (!_model.IsLive(i) && _model.IsBranch(i))
					{
						int savedStart = codeStart;
						flush(i);
						try
						{
							i = _Branch(i, hi, sequence, thenOf);
							continue;
						}
						catch (const BranchError &)
						{
							// It has no place: it stays in the code region.
							if (savedStart != NoIndex)
							{
								sequence->items.pop_back();
							}
							codeStart = (savedStart != NoIndex) ? savedStart : i;
							++i;
							continue;
						}
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

			// The toss of the switch whose head is i, when the switch ends
			// inside the sequence; NoIndex when there is none.
			int _SwitchAt(int i, int hi) const
			{
				if (!_model.IsLive(i))
				{
					return NoIndex;
				}
				for (int toss : _model.Tosses())
				{
					if ((_model.SwitchHead(toss) == i) && (toss < hi))
					{
						return toss;
					}
				}
				return NoIndex;
			}

			// A switch (plan section 3.2). A case starts at a dup at the depth
			// of the switch value; its value is [dup, eq?]; its bnt goes to the
			// next case, or to the toss; its body ends at the jmp to the toss
			// just before the next case. The last case can have no bnt and no
			// body. A case with no dup is the else case: the rest of the
			// switch.
			std::unique_ptr<Region> _Switch(int head, int toss)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::Switch);
				region->head = head;
				region->toss = toss;
				int depth = _model.DepthAfter(head);
				int entry = head + 1;
				while (entry < toss)
				{
					std::unique_ptr<Region> item = std::make_unique<Region>(RegionKind::Case);
					if ((_model.Op(entry) != Opcode::DUP) || !_model.IsLive(entry) || (_model.DepthBefore(entry) != depth))
					{
						item->body = _Sequence(entry, toss, nullptr);
						region->cases.push_back(std::move(item));
						break;
					}
					// The eq? that takes the dup: the first instruction that
					// brings the stack back to the depth of the switch.
					int compare = NoIndex;
					for (int j = entry + 1; j < toss; ++j)
					{
						if (_model.IsLive(j) && (_model.DepthAfter(j) <= depth))
						{
							compare = j;
							break;
						}
					}
					if ((compare == NoIndex) || (_model.Op(compare) != Opcode::EQ) || (_model.DepthAfter(compare) != depth))
					{
						if (entry == head + 1)
						{
							// A switch with only an else: Sierra's optimiser
							// puts a dup in place of a push of the switch value
							// (the same constant) at the start of its body.
							item->body = _Sequence(entry, toss, nullptr);
							region->cases.push_back(std::move(item));
							break;
						}
						_FailAt("case-value", entry);
					}
					item->value = _Sequence(entry, compare + 1, nullptr);
					int test = compare + 1;
					if (test == toss)
					{
						item->body = MakeSequence();
						region->cases.push_back(std::move(item));
						break;
					}
					int next = _model.Target(test);
					// The bnt of an empty last case (this repository's
					// compiler) goes to the toss just after it: it does nothing.
					if ((_model.Op(test) != Opcode::BNT) || !_model.IsLive(test) || _model.IsInert(test) || (next <= test) || (next > toss))
					{
						_FailAt("case-test", test);
					}
					item->branch = test;
					int bodyEnd = next;
					int jump = next - 1;
					if ((jump > test) && (_model.Op(jump) == Opcode::JMP) && _model.SameTarget(_model.Target(jump), toss, Arrival::Jump))
					{
						item->caseJmp = jump;
						bodyEnd = jump;
					}
					item->body = _Sequence(test + 1, bodyEnd, nullptr);
					region->cases.push_back(std::move(item));
					entry = next;
				}
				return region;
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
				// The loop is open while its body is read; an error that the
				// if of an enclosing scope catches must not leave it open.
				struct LoopScope
				{
					std::vector<OpenLoop> &loops;
					~LoopScope() { loops.pop_back(); }
				};
				_loops.push_back({ latch + 1, (stepStart != NoIndex) ? stepStart : head });
				LoopScope scope = { _loops };
				region->body = _Sequence(head, (stepStart != NoIndex) ? stepStart : latch, nullptr);
				if (stepStart != NoIndex)
				{
					region->step = _Sequence(stepStart, latch, nullptr);
				}
				return region;
			}

			// The region of the branch at p, in the sequence that ends at hi.
			// Returns the index after the region.
			int _Branch(int p, int hi, std::unique_ptr<Region> &sequence, OpenIf *thenOf)
			{
				Opcode op = _model.Op(p);
				int target = _model.ParseTarget(p);
				if ((op == Opcode::BNT) && (target != _model.ThreadedTarget(p)) && (_model.Op(target) == Opcode::BT))
				{
					// The "or" form of this repository's compiler moved the bnt
					// onto the bt just before its target. When that bt is a
					// breakif or contif, it is no or: the bnt keeps its target.
					bool toExit = false;
					if (_LoopLevel(_model.Target(target), Arrival::True, toExit) != 0)
					{
						target = _model.ThreadedTarget(p);
					}
				}
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
					// Only for an if with an else-part: when the false value
					// goes to a loop exit or continue point, Sierra's threading
					// sends the bnts of statements there too, and the term
					// would take in those statements. The loop rule below
					// gives the same control flow as nested ifs.
					// A term is a value: it has no way out.
					if (thenOf && (thenOf->region->elseKind == ElseKind::Else) && _model.SameTarget(target, thenOf->elseEntry, Arrival::False) &&
						!_JumpsOut(sequence.get(), 0))
					{
						thenOf->region->terms.push_back(std::move(sequence));
						thenOf->region->tests.push_back(p);
						sequence = MakeSequence();
						return p + 1;
					}
				}
				bool orJumpsOut = false;
				if (op == Opcode::BT)
				{
					int orEnd = ((target > p) && (target <= hi)) ? target : (_model.SameTarget(target, hi, Arrival::True) ? hi : NoIndex);
					if (orEnd != NoIndex)
					{
						std::unique_ptr<Region> region = _Or(p, orEnd);
						if (region)
						{
							sequence->items.push_back(std::move(region));
							return orEnd;
						}
						// No or: a breakif or contif when the target is a place
						// of a loop.
						orJumpsOut = true;
					}
				}
				bool toExit = false;
				int level = _LoopLevel(target, CodeModel::ArrivalOf(op), toExit);
				if ((level == 0) && orJumpsOut)
				{
					_Fail("or-jumps-out", p);
				}
				if ((level == 0) && (op == Opcode::JMP) && _model.IsLive(p) && _model.SameTarget(target, hi, Arrival::Jump) && _IsDead(p + 1, hi))
				{
					// A jmp to the end of the sequence, and only dead code
					// after it (an else-part that no test reaches).
					std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::Exit);
					region->branch = p;
					sequence->items.push_back(std::move(region));
					return p + 1;
				}
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

			// No instruction of [lo, hi) is live.
			bool _IsDead(int lo, int hi) const
			{
				return _model.NextLive(lo) >= hi;
			}

			// A bnt to X inside (p, hi]: an if. A jmp J at X - 1 is the else
			// marker when it does something, it is not the latch of a loop,
			// and J is inside (X, hi] or is the end of the sequence. Such a jmp
			// can also be the last statement of the then-part (a continue to
			// the step of a for loop, before the statements after the if):
			// when the reading with an else fails, the reading with no else is
			// tried.
			int _If(int p, int target, int hi, Region &sequence)
			{
				if (++_work > MaxWork)
				{
					throw ScopeError("parse", "too-complex", _model.Offset(p), "too many readings of the ifs");
				}
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
				std::unique_ptr<BranchError> elseError;
				if (elseEnd != NoIndex)
				{
					try
					{
						std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::If);
						region->tests.push_back(p);
						region->elseKind = ElseKind::Else;
						region->branch = marker;
						OpenIf open = { region.get(), target };
						region->thenPart = _Sequence(p + 1, marker, &open);
						region->elsePart = _Sequence(target, elseEnd, nullptr);
						sequence.items.push_back(std::move(region));
						return elseEnd;
					}
					catch (const BranchError &e)
					{
						// Try the reading with no else.
						elseError = std::make_unique<BranchError>(e);
					}
				}
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::If);
				region->tests.push_back(p);
				OpenIf open = { region.get(), target };
				try
				{
					region->thenPart = _Sequence(p + 1, target, &open);
				}
				catch (const BranchError &e)
				{
					// When the jmp of the else fails as a statement, the error
					// of the reading with an else tells more (the retry of
					// Parse reads it).
					if (elseError && (e.branch == marker))
					{
						throw *elseError;
					}
					throw;
				}
				sequence.items.push_back(std::move(region));
				return target;
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
			// An operand with a way out (a break or continue of an outer loop,
			// or an exit) is no value: null, it is no or.
			std::unique_ptr<Region> _Or(int p, int end)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::Or);
				region->branch = p;
				region->body = _Sequence(p + 1, end, nullptr);
				if (_JumpsOut(region->body.get(), 0))
				{
					return nullptr;
				}
				return region;
			}

			// The region has a way out to a place of an enclosing scope other
			// than its end: a break or continue of a loop outside it, or an
			// exit. An and-term and the second operand of an or are values,
			// which have none. depth is the count of the loops of the region
			// around the node.
			bool _JumpsOut(const Region *region, int depth) const
			{
				if (!region)
				{
					return false;
				}
				switch (region->kind)
				{
				case RegionKind::Sequence:
					return std::any_of(region->items.begin(), region->items.end(), [&](const std::unique_ptr<Region> &item) { return _JumpsOut(item.get(), depth); });
				case RegionKind::Code:
					// A return can be inside a value (a for loop as an operand).
					return false;
				case RegionKind::If:
					return (((region->elseKind == ElseKind::Break) || (region->elseKind == ElseKind::Continue)) && (region->level > depth)) ||
						std::any_of(region->terms.begin(), region->terms.end(), [&](const std::unique_ptr<Region> &term) { return _JumpsOut(term.get(), depth); }) ||
						_JumpsOut(region->thenPart.get(), depth) || _JumpsOut(region->elsePart.get(), depth);
				case RegionKind::Or:
					return _JumpsOut(region->body.get(), depth);
				case RegionKind::Loop:
					return _JumpsOut(region->body.get(), depth + 1) || _JumpsOut(region->step.get(), depth + 1);
				case RegionKind::Switch:
					return std::any_of(region->cases.begin(), region->cases.end(), [&](const std::unique_ptr<Region> &item) { return _JumpsOut(item.get(), depth); });
				case RegionKind::Case:
					return _JumpsOut(region->value.get(), depth) || _JumpsOut(region->body.get(), depth);
				case RegionKind::Break:
				case RegionKind::Continue:
				case RegionKind::BreakIf:
				case RegionKind::ContIf:
					return region->level > depth;
				case RegionKind::Exit:
					return true;
				}
				return false;
			}

			// The limit of the readings of the ifs in one parse.
			static const int MaxWork = 100000;

			const CodeModel &_model;
			const LoopSet &_loopSet;
			std::vector<OpenLoop> _loops;
			int _work = 0;
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
		// first statement of a repeat); else a forward jmp or bt inside a
		// loop goes to the step of a for loop (a continue is a jmp, a contif
		// a bt). Returns a text for the change; empty when neither applies.
		std::string _Retry(const CodeModel &model, LoopSet &loops, int branch)
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
						return fmt::format("split the loop at {0:04x}", model.Offset(back));
					}
				}
			}
			if (model.Op(branch) == Opcode::BNT)
			{
				return std::string();
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
				return fmt::format("step {0:04x} for the loop at {1:04x}", model.Offset(target), model.Offset(bestHead));
			}
			return std::string();
		}
	}

	std::unique_ptr<Region> Parse(const CodeModel &model)
	{
		LoopSet loops = _InitialLoops(model);
		std::string tried;
		for (;;)
		{
			try
			{
				Parser parser(model, loops);
				return parser.Run();
			}
			catch (const BranchError &e)
			{
				std::string change = _Retry(model, loops, e.branch);
				if (change.empty())
				{
					throw ScopeError(e.Stage(), e.Id(), e.Offset(), tried.empty() ? e.Detail() : (e.Detail() + " (after: " + tried + ")"));
				}
				tried += (tried.empty() ? "" : "; ") + fmt::format("{0:04x} {1}", e.Offset(), change);
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

	std::string CodeForDump(const std::list<scii> &code)
	{
		try
		{
			CodeModel model(code);
			return model.Dump();
		}
		catch (const ScopeError &e)
		{
			return fmt::format("{0} at {1:04x}: {2}\n", e.what(), e.Offset(), e.Detail());
		}
	}
}
