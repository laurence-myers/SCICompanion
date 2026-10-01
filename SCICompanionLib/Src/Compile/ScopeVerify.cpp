#include "stdafx.h"
#include "ScopeVerify.h"
#include "PMachine.h"
#include "format.h"

namespace scope
{
	namespace
	{
		// A resolution with any value that stops at a test.
		const int AtTest = -2;

		// Where control goes after an instruction: the next instruction, or
		// a test of the value in the accumulator with a place for each value.
		// An index is an instruction, Size() the end of the function, NoIndex
		// a circle.
		struct Successor
		{
			bool isTest = false;
			int next = NoIndex;
			int onTrue = NoIndex;
			int onFalse = NoIndex;

			static Successor Next(int next)
			{
				Successor successor;
				successor.next = next;
				return successor;
			}

			// A test that goes to one place for both values is no test.
			static Successor Test(int onTrue, int onFalse)
			{
				if (onTrue == onFalse)
				{
					return Next(onTrue);
				}
				Successor successor;
				successor.isTest = true;
				successor.onTrue = onTrue;
				successor.onFalse = onFalse;
				return successor;
			}

			bool operator==(const Successor &other) const
			{
				return (isTest == other.isTest) && (next == other.next) && (onTrue == other.onTrue) && (onFalse == other.onFalse);
			}
		};

		enum class SkeletonKind
		{
			Inst,	// an instruction that is not a branch of the control flow
			Inert,	// an inert bnt of an n-ary compare
			Skip,	// a branch that does nothing: it goes past the dead code after it
			Jmp,
			Bt,
			Bnt,
			End,	// the end of the function
		};

		struct SkeletonOp
		{
			SkeletonKind kind;
			int inst;
			int label;
		};

		class Skeleton
		{
		public:
			explicit Skeleton(const CodeModel &model) : _model(model) {}

			void Build(const Region &root)
			{
				_Emit(&root);
				_ops.push_back({ SkeletonKind::End, NoIndex, NoIndex });
			}

			// Where control goes after the instruction at a position of the
			// skeleton.
			Successor SuccessorAt(int position) const
			{
				int i = _ops[position].inst;
				if (!_model.FallsThrough(i))
				{
					return Successor::Next(_model.Size());
				}
				return From(position + 1);
			}

			// Where control goes from a position of the skeleton, with any
			// value in the accumulator.
			Successor From(int position) const
			{
				int landing = _Resolve(position, Arrival::Jump);
				if (landing == AtTest)
				{
					return Successor::Test(_Resolve(position, Arrival::True), _Resolve(position, Arrival::False));
				}
				return Successor::Next(landing);
			}

			const std::vector<SkeletonOp> &Ops() const { return _ops; }

			// The resolutions of From and SuccessorAt add the dead branches that
			// they go through to the set (when it is not null).
			void RecordDeadBranches(std::set<int> *passed) { _passed = passed; }

		private:
			struct LoopLabels
			{
				int exit;
				int cont;
			};

			int _NewLabel()
			{
				_labels.push_back(NoIndex);
				return (int)_labels.size() - 1;
			}

			void _Place(int label)
			{
				_labels[label] = (int)_ops.size();
			}

			void _Fail(const std::string &id, int index, const std::string &detail) const
			{
				int offset = ((index >= 0) && (index < _model.Size())) ? _model.Offset(index) : -1;
				throw ScopeError("verify", id, offset, detail);
			}

			// The node owns the instruction as a branch: it must be one of
			// the opcode.
			void _Expect(int index, Opcode op, const char *what) const
			{
				if ((index < 0) || (index >= _model.Size()) || (_model.Op(index) != op))
				{
					_Fail("opcode", index, fmt::format("the {0} is no {1}", what, OpcodeToName(op, 0)));
				}
				if (_model.IsLive(index) && _model.IsInert(index))
				{
					_Fail("opcode", index, fmt::format("the {0} is an inert bnt of an n-ary compare", what));
				}
			}

			void _Branch(SkeletonKind kind, int inst, int label)
			{
				_ops.push_back({ kind, inst, label });
			}

			const LoopLabels &_Loop(int level, int branch) const
			{
				if ((level < 1) || (level > (int)_loops.size()))
				{
					_Fail("level", branch, fmt::format("level {0} with {1} loops", level, _loops.size()));
				}
				return _loops[_loops.size() - level];
			}

			void _Emit(const Region *region)
			{
				if (!region)
				{
					return;
				}
				switch (region->kind)
				{
				case RegionKind::Sequence:
				{
					// The end of the sequence, for an exit in it.
					int endLabel = _NewLabel();
					_sequenceEnds.push_back(endLabel);
					for (const auto &item : region->items)
					{
						_Emit(item.get());
					}
					_sequenceEnds.pop_back();
					_Place(endLabel);
					break;
				}

				case RegionKind::Code:
					for (int i = region->first; i <= region->last; ++i)
					{
						if ((i < 0) || (i >= _model.Size()))
						{
							_Fail("layout", i, "an instruction out of the function");
						}
						if (_model.IsFlowBranch(i))
						{
							_Fail("branch-in-code", i, "a branch of the control flow in a code region");
						}
						SkeletonKind kind = SkeletonKind::Inst;
						// A dead jmp over dead code only goes where the skip
						// goes (a break can resolve through it: the exit of a
						// loop that is a dead jmp to the code after the loop).
						bool deadSkip = !_model.IsLive(i) && (_model.Op(i) == Opcode::JMP) && (_model.Target(i) > i) &&
							(_model.Target(i) == _model.NextLive(i + 1));
						if ((_model.IsLive(i) && _model.IsNoOp(i)) || deadSkip)
						{
							kind = SkeletonKind::Skip;
						}
						else if (_model.IsLive(i) && _model.IsInert(i))
						{
							kind = SkeletonKind::Inert;
						}
						_ops.push_back({ kind, i, NoIndex });
					}
					break;

				case RegionKind::If:
				{
					int elseLabel = _NewLabel();
					int endLabel = _NewLabel();
					int falseLabel = elseLabel;
					if (region->elseKind == ElseKind::Break)
					{
						falseLabel = _Loop(region->level, region->tests.empty() ? NoIndex : region->tests[0]).exit;
					}
					else if (region->elseKind == ElseKind::Continue)
					{
						falseLabel = _Loop(region->level, region->tests.empty() ? NoIndex : region->tests[0]).cont;
					}
					for (size_t k = 0; k < region->tests.size(); ++k)
					{
						if (k > 0)
						{
							_Emit((k - 1 < region->terms.size()) ? region->terms[k - 1].get() : nullptr);
						}
						_Expect(region->tests[k], Opcode::BNT, "test of an if");
						_Branch(SkeletonKind::Bnt, region->tests[k], falseLabel);
					}
					_Emit(region->thenPart.get());
					if (region->elseKind == ElseKind::Else)
					{
						_Expect(region->branch, Opcode::JMP, "jmp before an else");
						_Branch(SkeletonKind::Jmp, region->branch, endLabel);
						_Place(elseLabel);
						_Emit(region->elsePart.get());
					}
					else
					{
						_Place(elseLabel);
					}
					_Place(endLabel);
					break;
				}

				case RegionKind::Or:
				{
					int endLabel = _NewLabel();
					_Expect(region->branch, Opcode::BT, "bt of an or");
					_Branch(SkeletonKind::Bt, region->branch, endLabel);
					_Emit(region->body.get());
					_Place(endLabel);
					break;
				}

				case RegionKind::Loop:
				{
					int headLabel = _NewLabel();
					LoopLabels labels = { _NewLabel(), region->step ? _NewLabel() : headLabel };
					_Place(headLabel);
					_loops.push_back(labels);
					_Emit(region->body.get());
					if (region->step)
					{
						_Place(labels.cont);
						_Emit(region->step.get());
					}
					int latch = region->branch;
					if ((latch < 0) || (latch >= _model.Size()) || !_model.IsBranch(latch))
					{
						_Fail("opcode", latch, "the latch of a loop is no branch");
					}
					if (_model.Target(latch) != region->head)
					{
						_Fail("loop-head", latch, "the latch does not go to the head of the loop");
					}
					Opcode op = _model.Op(latch);
					_Branch((op == Opcode::JMP) ? SkeletonKind::Jmp : ((op == Opcode::BT) ? SkeletonKind::Bt : SkeletonKind::Bnt), latch, headLabel);
					_loops.pop_back();
					_Place(labels.exit);
					break;
				}

				case RegionKind::Switch:
				{
					if ((region->head < 0) || (region->head >= _model.Size()) || (_model.Pushes(region->head) != 1))
					{
						_Fail("opcode", region->head, "the head of a switch is no push");
					}
					_Expect(region->toss, Opcode::TOSS, "end of a switch");
					_ops.push_back({ SkeletonKind::Inst, region->head, NoIndex });
					int doneLabel = _NewLabel();
					std::vector<int> caseLabels;
					for (size_t k = 0; k < region->cases.size(); ++k)
					{
						caseLabels.push_back(_NewLabel());
					}
					for (size_t k = 0; k < region->cases.size(); ++k)
					{
						if (!region->cases[k] || (region->cases[k]->kind != RegionKind::Case))
						{
							_Fail("layout", region->head, "a switch with an item that is no case");
						}
						const Region &item = *region->cases[k];
						_Place(caseLabels[k]);
						_Emit(item.value.get());
						if (item.branch != NoIndex)
						{
							_Expect(item.branch, Opcode::BNT, "test of a case");
							_Branch(SkeletonKind::Bnt, item.branch, (k + 1 < caseLabels.size()) ? caseLabels[k + 1] : doneLabel);
						}
						_Emit(item.body.get());
						if (item.caseJmp != NoIndex)
						{
							_Expect(item.caseJmp, Opcode::JMP, "jmp at the end of a case");
							_Branch(SkeletonKind::Jmp, item.caseJmp, doneLabel);
						}
					}
					_Place(doneLabel);
					_ops.push_back({ SkeletonKind::Inst, region->toss, NoIndex });
					break;
				}

				case RegionKind::Case:
					_Fail("layout", NoIndex, "a case out of a switch");
					break;

				case RegionKind::Break:
					_Expect(region->branch, Opcode::JMP, "break");
					_Branch(SkeletonKind::Jmp, region->branch, _Loop(region->level, region->branch).exit);
					break;

				case RegionKind::Continue:
					_Expect(region->branch, Opcode::JMP, "continue");
					_Branch(SkeletonKind::Jmp, region->branch, _Loop(region->level, region->branch).cont);
					break;

				case RegionKind::BreakIf:
					_Expect(region->branch, Opcode::BT, "breakif");
					_Branch(SkeletonKind::Bt, region->branch, _Loop(region->level, region->branch).exit);
					break;

				case RegionKind::ContIf:
					_Expect(region->branch, Opcode::BT, "contif");
					_Branch(SkeletonKind::Bt, region->branch, _Loop(region->level, region->branch).cont);
					break;

				case RegionKind::Exit:
					_Expect(region->branch, Opcode::JMP, "exit");
					if (_sequenceEnds.empty())
					{
						_Fail("layout", region->branch, "an exit out of a sequence");
					}
					_Branch(SkeletonKind::Jmp, region->branch, _sequenceEnds.back());
					break;
				}
			}

			// Where control gets to an instruction that is not a branch, from
			// a position of the skeleton, for the value in the accumulator.
			int _Resolve(int position, Arrival arrival) const
			{
				for (size_t steps = 0; steps <= _ops.size(); ++steps)
				{
					const SkeletonOp &op = _ops[position];
					switch (op.kind)
					{
					case SkeletonKind::Inst:
						return op.inst;
					case SkeletonKind::End:
						return _model.Size();
					case SkeletonKind::Jmp:
						_Passed(op.inst);
						position = _labels[op.label];
						break;
					case SkeletonKind::Bnt:
					case SkeletonKind::Bt:
					{
						if (arrival == Arrival::Jump)
						{
							return AtTest;
						}
						_Passed(op.inst);
						bool taken = (arrival == Arrival::True) == (op.kind == SkeletonKind::Bt);
						position = taken ? _labels[op.label] : (position + 1);
						break;
					}
					case SkeletonKind::Inert:
						// The n-ary pass made sure that the bytecode target is
						// where the end of the compare goes for a false value.
						if (arrival == Arrival::Jump)
						{
							return AtTest;
						}
						if (arrival == Arrival::False)
						{
							return _model.Resolve(_model.Target(op.inst), Arrival::False);
						}
						++position;
						break;
					case SkeletonKind::Skip:
						// Like the bytecode: past each dead instruction after it.
						++position;
						while ((_ops[position].inst != NoIndex) && !_model.IsLive(_ops[position].inst))
						{
							++position;
						}
						break;
					}
				}
				return NoIndex;
			}

			void _Passed(int inst) const
			{
				if (_passed && (inst != NoIndex) && !_model.IsLive(inst))
				{
					_passed->insert(inst);
				}
			}

			const CodeModel &_model;
			std::set<int> *_passed = nullptr;
			std::vector<SkeletonOp> _ops;
			std::vector<int> _labels;
			std::vector<LoopLabels> _loops;
			std::vector<int> _sequenceEnds;
		};

		Successor _BytecodeFrom(const CodeModel &model, int position);

		// Each code region has instructions of the function, first..last,
		// before the layout of the tree is built.
		void _CheckCodeRanges(const CodeModel &model, const Region *region)
		{
			if (!region)
			{
				return;
			}
			if (region->kind == RegionKind::Code)
			{
				if ((region->first < 0) || (region->last < region->first) || (region->last >= model.Size()))
				{
					int index = ((region->first >= 0) && (region->first < model.Size())) ? region->first : NoIndex;
					throw ScopeError("verify", "layout", (index != NoIndex) ? model.Offset(index) : -1,
						fmt::format("a code region with the instructions {0}..{1}", region->first, region->last));
				}
				return;
			}
			auto each = [&](const std::vector<std::unique_ptr<Region>> &list)
			{
				for (const auto &item : list)
				{
					_CheckCodeRanges(model, item.get());
				}
			};
			each(region->items);
			each(region->terms);
			each(region->cases);
			_CheckCodeRanges(model, region->thenPart.get());
			_CheckCodeRanges(model, region->elsePart.get());
			_CheckCodeRanges(model, region->body.get());
			_CheckCodeRanges(model, region->step.get());
			_CheckCodeRanges(model, region->value.get());
		}

		// Where control goes after a live instruction that is not a branch,
		// in the bytecode.
		Successor _BytecodeSuccessor(const CodeModel &model, int i)
		{
			if (!model.FallsThrough(i))
			{
				return Successor::Next(model.Size());
			}
			return _BytecodeFrom(model, i + 1);
		}

		// Where control goes from an address of the bytecode, with any value
		// in the accumulator.
		Successor _BytecodeFrom(const CodeModel &model, int position)
		{
			int landing = model.Resolve(position, Arrival::Jump);
			if ((landing >= 0) && (landing < model.Size()) && model.IsConditional(landing))
			{
				return Successor::Test(model.Resolve(position, Arrival::True), model.Resolve(position, Arrival::False));
			}
			return Successor::Next(landing);
		}

		std::string _Place(const CodeModel &model, int index)
		{
			if (index == NoIndex)
			{
				return "circle";
			}
			if (index < 0)
			{
				return fmt::format("bad index {0}", index);
			}
			if (index >= model.Size())
			{
				return "end";
			}
			return fmt::format("{0:04x}", model.Offset(index));
		}

		std::string _Text(const CodeModel &model, const Successor &successor)
		{
			if (successor.isTest)
			{
				return fmt::format("test (true {0}, false {1})", _Place(model, successor.onTrue), _Place(model, successor.onFalse));
			}
			return "next " + _Place(model, successor.next);
		}
	}

	void Verify(const CodeModel &model, const Region &root, std::set<int> *passedDeadBranches)
	{
		_CheckCodeRanges(model, &root);
		std::vector<int> layout = Layout(root);
		for (size_t k = 0; k < (std::max)(layout.size(), (size_t)model.Size()); ++k)
		{
			int expected = (k < (size_t)model.Size()) ? (int)k : NoIndex;
			int actual = (k < layout.size()) ? layout[k] : NoIndex;
			if (actual != expected)
			{
				int index = (expected != NoIndex) ? expected : actual;
				int offset = ((index >= 0) && (index < model.Size())) ? model.Offset(index) : -1;
				throw ScopeError("verify", "layout", offset,
					fmt::format("the tree has {0} where the function has {1}", _Place(model, actual), _Place(model, expected)));
			}
		}

		Skeleton skeleton(model);
		skeleton.Build(root);
		skeleton.RecordDeadBranches(passedDeadBranches);
		const std::vector<SkeletonOp> &ops = skeleton.Ops();
		Successor treeEntry = skeleton.From(0);
		Successor bytecodeEntry = _BytecodeFrom(model, 0);
		if (!(treeEntry == bytecodeEntry))
		{
			throw ScopeError("verify", "entry", (model.Size() > 0) ? model.Offset(0) : -1,
				fmt::format("the tree starts with {0}, the bytecode {1}", _Text(model, treeEntry), _Text(model, bytecodeEntry)));
		}
		for (int position = 0; position < (int)ops.size(); ++position)
		{
			const SkeletonOp &op = ops[position];
			if ((op.kind != SkeletonKind::Inst) || !model.IsLive(op.inst) || model.IsBranch(op.inst))
			{
				continue;
			}
			Successor tree = skeleton.SuccessorAt(position);
			Successor bytecode = _BytecodeSuccessor(model, op.inst);
			if (!(tree == bytecode))
			{
				throw ScopeError("verify", "successor", model.Offset(op.inst),
					fmt::format("the tree gives {0}, the bytecode {1}", _Text(model, tree), _Text(model, bytecode)));
			}
		}
	}
}
