#include "stdafx.h"
#include "ScopeParser.h"
#include "ScopeVerify.h"
#include "PMachine.h"
#include "format.h"

namespace scope
{
	namespace
	{
		// An if whose then-part the parser reads. At the top level of the
		// then-part, a bnt to the else entry is one more and-term.
		struct OpenIf
		{
			Region *region;
			int elseEntry;
		};

		class Parser
		{
		public:
			explicit Parser(const CodeModel &model) : _model(model) {}

			std::unique_ptr<Region> Run()
			{
				return _Sequence(0, _model.Size(), nullptr);
			}

		private:
			[[noreturn]] void _Fail(const char *id, int branch) const
			{
				throw ScopeError("parse", id, _model.Offset(branch),
					fmt::format("{0} to {1:04x}", OpcodeToName(_model.Op(branch), 0), _model.Offset(_model.Target(branch))));
			}

			// The sequence [lo, hi): code regions, and a region for each
			// branch of the control flow.
			std::unique_ptr<Region> _Sequence(int lo, int hi, OpenIf *thenOf)
			{
				std::unique_ptr<Region> sequence = MakeSequence();
				int codeStart = NoIndex;
				int i = lo;
				while (i < hi)
				{
					if (!_model.IsFlowBranch(i))
					{
						if (codeStart == NoIndex)
						{
							codeStart = i;
						}
						++i;
						continue;
					}
					if (codeStart != NoIndex)
					{
						sequence->items.push_back(MakeCode(codeStart, i - 1));
						codeStart = NoIndex;
					}
					i = _Branch(i, hi, sequence, thenOf);
				}
				if (codeStart != NoIndex)
				{
					sequence->items.push_back(MakeCode(codeStart, hi - 1));
				}
				return sequence;
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
						sequence->items.push_back(_IfToEnd(p, hi, hi));
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
				_Fail("no-scope-for-target", p);
			}

			// A bnt to X inside (p, hi]: an if. A jmp J at X - 1 is the else
			// marker when it does something, J is inside (X, hi] or is the end
			// of the sequence, and it is not the latch of a loop.
			int _If(int p, int target, int hi, Region &sequence)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::If);
				region->tests.push_back(p);
				OpenIf open = { region.get(), target };
				int marker = target - 1;
				int elseEnd = NoIndex;
				if ((marker > p) && (_model.Op(marker) == Opcode::JMP) && !(_model.IsLive(marker) && _model.IsNoOp(marker)) && !_IsLatch(marker))
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

			// A bnt whose false value goes to elseEntry: an if whose
			// then-part is the rest of the sequence, [p + 1, hi).
			std::unique_ptr<Region> _IfToEnd(int p, int hi, int elseEntry)
			{
				std::unique_ptr<Region> region = std::make_unique<Region>(RegionKind::If);
				region->tests.push_back(p);
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

			bool _IsLatch(int) const
			{
				return false;
			}

			const CodeModel &_model;
		};
	}

	std::unique_ptr<Region> Parse(const CodeModel &model)
	{
		Parser parser(model);
		return parser.Run();
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
