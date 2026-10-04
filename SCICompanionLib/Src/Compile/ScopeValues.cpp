#include "stdafx.h"
#include "ScopeValues.h"
#include "ConsumptionNode.h"
#include "DecompilerCore.h"
#include "PMachine.h"
#include "format.h"

namespace scope
{
	namespace
	{
		// What the accumulator holds as Sierra's optimiser knows it (sc,
		// OPTIMIZE.CPP): the optimiser deletes a load of a value that the
		// accumulator holds, and a push of it becomes "push". A copy of the
		// value comes from the instruction that set the fact.
		enum class FactKind
		{
			Unknown,
			Immediate,	// ldi n
			Variable,	// a load of a variable, or a store to it, or an increment or decrement of it
			Property,	// a load of a property
			Pushed,		// a push of a node with no effect: the accumulator still has its value
			One,		// the fall-through of a bnt of a compare: the machine put 1 there
		};

		struct Fact
		{
			FactKind kind = FactKind::Unknown;
			int source = NoIndex;
		};

		enum class EntryKind
		{
			Value,			// a node
			Rest,			// a &rest: an argument of the call that takes the values below it; not in the depth
			SwitchValue,	// the value of a switch, which the switch node has (ref)
			CaseDup,		// the dup at the start of the value of a case: the left operand of its eq?
			Prev,			// a pprev: the operands of the compare before it (ref), for an n-ary compare
		};

		// An entry of the symbolic stack.
		struct StackEntry
		{
			std::unique_ptr<ConsumptionNode> node;
			int time;
			EntryKind kind;
			ConsumptionNode *ref;
		};

		// A statement list: a node whose children are the statements of a
		// sequence, with the stack depth after each one and the instruction
		// of each one. A sequence does not take values that were pushed
		// before it (its floor).
		struct List
		{
			ConsumptionNode *node;
			int floor;
			std::vector<std::pair<int, int>> items;
		};

		bool _IsVariableOp(Opcode op)
		{
			return (op >= Opcode::FirstLoadStore) && (op <= Opcode::LastLoadStore);
		}

		bool _IsCallOp(Opcode op)
		{
			switch (op)
			{
			case Opcode::SEND:
			case Opcode::CALL:
			case Opcode::CALLB:
			case Opcode::CALLE:
			case Opcode::CALLK:
			case Opcode::SELF:
			case Opcode::SUPER:
				return true;
			default:
				return false;
			}
		}

		// A value that can be read a second time: a number, a variable, a
		// property, an object, self, or a push of one of them.
		bool _IsPureValue(ConsumptionNode &node)
		{
			if (!node._hasPos)
			{
				return false;
			}
			Opcode op = node.GetCode()->get_opcode();
			switch (op)
			{
			case Opcode::LDI:
			case Opcode::PUSHI:
			case Opcode::PUSH0:
			case Opcode::PUSH1:
			case Opcode::PUSH2:
			case Opcode::PUSHSELF:
			case Opcode::SELFID:
			case Opcode::PTOA:
			case Opcode::PTOS:
			case Opcode::LOFSA:
			case Opcode::LOFSS:
			case Opcode::CLASS:
				return node.GetChildCount() == 0;
			case Opcode::PUSH:
				return (node.GetChildCount() == 1) && _IsPureValue(*node.Child(0));
			default:
				break;
			}
			if (node.GetType() == ChunkType::ShortCircuitInstruction)
			{
				return true;
			}
			return _IsVariableOp(op) && !_IsVOStoreOperation(op) && !_IsVOIncremented(op) && !_IsVODecremented(op) &&
				!_IsVOIndexed(op) && (node.GetChildCount() == 0);
		}

		// A copy of the node and its children, each marked as a copy.
		std::unique_ptr<ConsumptionNode> _DeepCopy(ConsumptionNode &node)
		{
			std::unique_ptr<ConsumptionNode> copy = std::make_unique<ConsumptionNode>();
			copy->SetType(node.GetType());
			if (node._hasPos)
			{
				copy->SetPos(node.GetCode());
			}
			copy->_copy = true;
			for (size_t i = 0; i < node.GetChildCount(); ++i)
			{
				copy->AppendChild(_DeepCopy(*node.Child((int)i)));
			}
			return copy;
		}

		class Evaluator
		{
		public:
			Evaluator(const CodeModel &model, std::list<scii> &code, bool returnsValue, const std::set<int> &passedDeadBranches) :
				_model(model), _returnsValue(returnsValue), _passedDeadBranches(passedDeadBranches), _structural(model.Size(), 0), _made(model.Size(), 0)
			{
				for (auto it = code.begin(); it != code.end(); ++it)
				{
					if (it->get_opcode() != Opcode::INDETERMINATE)
					{
						_pos.push_back(it);
					}
				}
				if ((int)_pos.size() != model.Size())
				{
					throw ScopeError("values", "code-mismatch", -1);
				}
				for (int i = 0; i < model.Size(); ++i)
				{
					_indexOf[&*_pos[i]] = i;
				}
			}

			std::unique_ptr<ConsumptionNode> Run(const Region &root)
			{
				std::unique_ptr<ConsumptionNode> body = std::make_unique<ConsumptionNode>();
				body->SetType(ChunkType::FunctionBody);
				List list = { body.get(), 0, {} };
				_list = &list;
				_Region(root);
				_EndList(list, _model.Size() - 1);
				_CheckTree(*body);
				_BreakIfForms(*body);
				return body;
			}

		private:
			[[noreturn]] void _Fail(const char *id, int index, const std::string &detail = std::string()) const
			{
				int offset = ((index >= 0) && (index < _model.Size())) ? _model.Offset(index) : -1;
				std::string text = detail;
				if (text.empty() && (index >= 0) && (index < _model.Size()))
				{
					text = OpcodeToName(_model.Op(index), 0);
				}
				throw ScopeError("values", id, offset, text);
			}

			int _Depth() const
			{
				int depth = 0;
				for (const StackEntry &entry : _stack)
				{
					depth += (entry.kind == EntryKind::Rest) ? 0 : 1;
				}
				return depth;
			}

			// The first instruction of a region, for the offset of an error.
			int _FirstOf(const Region &region) const
			{
				std::vector<int> layout = Layout(region);
				return layout.empty() ? NoIndex : layout.front();
			}

			void _ResetFacts()
			{
				_acc = Fact();
				_pending = nullptr;
			}

			// A node for the instruction; it counts as the instruction's place
			// in the tree.
			std::unique_ptr<ConsumptionNode> _Node(int i)
			{
				std::unique_ptr<ConsumptionNode> node = std::make_unique<ConsumptionNode>();
				node->SetPos(_pos[i]);
				_made[i]++;
				return node;
			}

			// An instruction with no node: a branch that the structure of the
			// tree gives, a branch that does nothing, a dup (its value is a
			// copy), or an instruction with no value (link, a line number).
			void _Structural(int i)
			{
				if (i != NoIndex)
				{
					_structural[i]++;
				}
			}

			// A statement at the end of the list; index is its instruction.
			void _Append(std::unique_ptr<ConsumptionNode> node, bool pending, int index)
			{
				ConsumptionNode *raw = node.get();
				_list->node->AppendChild(std::move(node));
				_list->items.push_back({ _Depth(), index });
				if (pending)
				{
					_pending = raw;
					_pendingTime = ++_time;
					_accDiffers = false;
				}
			}

			// The node that gives the accumulator now, when the reader can
			// take it: it is the last statement of the current list, and no
			// value was pushed after it (the operand of an instruction comes
			// after its stack operands). The copy of a dup counts as a push:
			// in the source, its value comes after the node.
			bool _AccIsAvailable() const
			{
				return _PendingIsLast() && (_stack.empty() || (_stack.back().time < _pendingTime));
			}

			// The node of the accumulator is the last statement of the current
			// list.
			bool _PendingIsLast() const
			{
				return _pending && (_list->node->GetChildCount() > 0) && (_list->node->Child((int)_list->node->GetChildCount() - 1) == _pending);
			}

			std::unique_ptr<ConsumptionNode> _TakePending()
			{
				size_t last = _list->node->GetChildCount() - 1;
				std::unique_ptr<ConsumptionNode> node = _list->node->StealChild(last);
				_list->items.pop_back();
				_pending = nullptr;
				return node;
			}

			// Operands in the other order: one operand stays in the accumulator,
			// and a constant is pushed after it, for an arithmetic or bit
			// operation whose operands can change places (not a compare: it sets
			// the previous value). The SQ4 copy in a "patch" folder has this in
			// script 381 (roboClerkWelcome::changeState: "callk Random; pushi 6;
			// mul"); Sierra's compiler gives "push; ldi 6; mul". True when the
			// reader is such an operation, the node of the accumulator is the
			// last statement, the accumulator has no fact (with a fact, the
			// operand is a copy), and the only value pushed after the node is a
			// constant.
			bool _IsSwappedOperands(int reader, const Consumption &consumption) const
			{
				Opcode op = _model.Op(reader);
				bool swappable = (op == Opcode::ADD) || (op == Opcode::MUL) || (op == Opcode::AND) || (op == Opcode::OR) || (op == Opcode::XOR);
				if (!swappable || (consumption.cStackConsume != 1) || (consumption.cAccConsume != 1) || !_PendingIsLast() ||
					(_acc.kind != FactKind::Unknown) || _stack.empty())
				{
					return false;
				}
				const StackEntry &top = _stack.back();
				if ((top.kind != EntryKind::Value) || !top.node || !top.node->_hasPos || (top.node->GetChildCount() != 0) || (top.time < _pendingTime))
				{
					return false;
				}
				if ((_stack.size() >= 2) && (_stack[_stack.size() - 2].time > _pendingTime))
				{
					return false;
				}
				Opcode pushed = top.node->GetCode()->get_opcode();
				return (pushed == Opcode::PUSHI) || (pushed == Opcode::PUSH0) || (pushed == Opcode::PUSH1) || (pushed == Opcode::PUSH2);
			}

			// A copy of the value of the accumulator, from its fact.
			std::unique_ptr<ConsumptionNode> _CopyOfAcc(int reader)
			{
				if (_acc.kind == FactKind::Unknown)
				{
					std::string why = !_pending ? "no node" : (!_PendingIsLast() ? "a statement after the node" : "a push after the node");
					_Fail("acc-no-fact", reader, fmt::format("{0}: {1}, and no fact", OpcodeToName(_model.Op(reader), 0), why));
				}
				if (_acc.kind == FactKind::Pushed)
				{
					return _DeepCopy(*_pushedNode);
				}
				if (_acc.kind == FactKind::One)
				{
					return _Wrap(ChunkType::TrueNode, nullptr);
				}
				std::unique_ptr<ConsumptionNode> copy = std::make_unique<ConsumptionNode>();
				Opcode op = _model.Op(_acc.source);
				if (_IsVariableOp(op) && (_IsVOStoreOperation(op) || _IsVOIncremented(op) || _IsVODecremented(op)))
				{
					// The value is read back from the variable.
					copy->SetType(ChunkType::ShortCircuitInstruction);
				}
				copy->SetPos(_pos[_acc.source]);
				copy->_copy = true;
				return copy;
			}

			// The accumulator operand of the reader: the node that gives it,
			// or a copy from the fact.
			std::unique_ptr<ConsumptionNode> _TakeAcc(int reader)
			{
				if (_AccIsAvailable())
				{
					return _TakePending();
				}
				return _CopyOfAcc(reader);
			}

			// The stack operands of the reader, in push order. A call also
			// takes the &rest entries among them.
			std::vector<std::unique_ptr<ConsumptionNode>> _Pop(int reader, int count)
			{
				std::vector<std::unique_ptr<ConsumptionNode>> operands;
				bool call = _IsCallOp(_model.Op(reader));
				while (count > 0)
				{
					if (_Depth() <= _list->floor)
					{
						_Fail("stack-underflow", reader);
					}
					StackEntry &top = _stack.back();
					switch (top.kind)
					{
					case EntryKind::Rest:
						if (!call)
						{
							_Fail("rest-outside-call", reader);
						}
						break;
					case EntryKind::Value:
						--count;
						break;
					case EntryKind::SwitchValue:
						_Fail("switch-value-taken", reader);
					case EntryKind::CaseDup:
						_Fail("case-dup-taken", reader);
					case EntryKind::Prev:
						_Fail("pprev-outside-compare", reader);
					}
					operands.push_back(std::move(top.node));
					_stack.pop_back();
				}
				std::reverse(operands.begin(), operands.end());
				return operands;
			}

			void _Push(std::unique_ptr<ConsumptionNode> node, EntryKind kind = EntryKind::Value, ConsumptionNode *ref = nullptr)
			{
				_stack.push_back({ std::move(node), ++_time, kind, ref });
			}

			// The last statement of the current list, out of the list.
			std::unique_ptr<ConsumptionNode> _StealLast()
			{
				std::unique_ptr<ConsumptionNode> node = _list->node->StealChild(_list->node->GetChildCount() - 1);
				_list->items.pop_back();
				if (node.get() == _pending)
				{
					_pending = nullptr;
				}
				return node;
			}

			// A node that was made for the instruction, and that the tree has
			// not: the instruction is a part of the structure.
			void _Unmade(int i)
			{
				_made[i]--;
				_structural[i]++;
			}

			void _Region(const Region &region)
			{
				// A structure ends the code right after a ret.
				if (region.kind != RegionKind::Code)
				{
					_afterReturn = false;
				}
				_RegionBody(region);
				if (region.kind != RegionKind::Code)
				{
					_afterReturn = false;
				}
			}

			void _RegionBody(const Region &region)
			{
				// A break, a continue and an exit keep their place when they are
				// dead: another branch can resolve through it (an inner break
				// that goes to a dead jmp after its loop leaves the outer loop
				// too).
				bool hasTest = (region.kind != RegionKind::Sequence) && (region.kind != RegionKind::Code) && (region.kind != RegionKind::Break) &&
					(region.kind != RegionKind::Continue) && (region.kind != RegionKind::Exit);
				if (hasTest)
				{
					std::vector<int> layout = Layout(region);
					if (!layout.empty() && std::none_of(layout.begin(), layout.end(), [&](int i) { return _model.IsLive(i); }))
					{
						// A structure that no path reaches (the layout of the
						// templates after a return): dead code. Its tests have
						// no value.
						for (int i : layout)
						{
							_Instruction(i);
						}
						return;
					}
				}
				switch (region.kind)
				{
				case RegionKind::Sequence:
					for (const auto &item : region.items)
					{
						_Region(*item);
					}
					break;
				case RegionKind::Code:
					for (int i = region.first; i <= region.last; ++i)
					{
						_Instruction(i);
					}
					break;
				case RegionKind::If:
					_If(region);
					break;
				case RegionKind::Or:
					_Or(region);
					break;
				case RegionKind::Exit:
					// The rest of the sequence is dead.
					_Structural(region.branch);
					_ResetFacts();
					break;
				case RegionKind::Loop:
					_Loop(region);
					break;
				case RegionKind::Switch:
					_Switch(region);
					break;
				case RegionKind::Case:
					_Fail("case-outside-switch", _FirstOf(region));
				case RegionKind::Break:
				case RegionKind::Continue:
					// The rest of the sequence is dead.
					_Structural(region.branch);
					// A dead one is a statement when a path of the tree goes
					// through it (verify), as the text gets there.
					if (_model.IsLive(region.branch) || (_passedDeadBranches.count(region.branch) != 0))
					{
						_Append(_LoopJump(region.kind == RegionKind::Break, region.level), false, region.branch);
					}
					_ResetFacts();
					break;
				case RegionKind::BreakIf:
				case RegionKind::ContIf:
					_LoopJumpIf(region);
					break;
				}
			}

			// A break or a continue of the loop at the level.
			std::unique_ptr<ConsumptionNode> _LoopJump(bool isBreak, int level)
			{
				std::unique_ptr<ConsumptionNode> node = std::make_unique<ConsumptionNode>();
				node->SetType(isBreak ? ChunkType::Break : ChunkType::Continue);
				node->_level = level;
				return node;
			}

			// A node of the type with one child.
			std::unique_ptr<ConsumptionNode> _Wrap(ChunkType type, std::unique_ptr<ConsumptionNode> child)
			{
				std::unique_ptr<ConsumptionNode> node = std::make_unique<ConsumptionNode>();
				node->SetType(type);
				if (child)
				{
					node->AppendChild(std::move(child));
				}
				return node;
			}

			// A breakif or a contif: an if whose then-part is the break or the
			// continue. The fall-through keeps the facts.
			void _LoopJumpIf(const Region &region)
			{
				_DeadTest(region.branch);
				std::unique_ptr<ConsumptionNode> ifNode = std::make_unique<ConsumptionNode>();
				ifNode->SetType(ChunkType::If);
				ifNode->AppendChild(_Wrap(ChunkType::Condition, _TakeAcc(region.branch)));
				ifNode->AppendChild(_Wrap(ChunkType::Then, _LoopJump(region.kind == RegionKind::BreakIf, region.level)));
				_Structural(region.branch);
				_Append(std::move(ifNode), false, region.branch);
			}

			void _Instruction(int i)
			{
				if (!_model.IsLive(i))
				{
					// The live code after dead code starts at a label: no fact.
					// Dead code right after a ret stays as statements (no text
					// gets to it either); other dead code gives no statement:
					// no path of the tree gets to it (verify), and as text it
					// would run.
					// A jmp over the dead code that does nothing, and that is
					// the only branch to the live code after it, keeps the
					// node and the facts (the KQ4 copy in "patch\NEW", script
					// 996, User::getInput: the value of an if goes over dead
					// code to a store).
					bool passedOver = (i > 0) && (_model.Op(i - 1) == Opcode::JMP) && _model.IsNoOp(i - 1) && (_model.Target(i - 1) != NoIndex) &&
						(_model.Sources(_model.Target(i - 1)).size() == 1);
					if (((i == 0) || _model.IsLive(i - 1)) && !passedOver)
					{
						_ResetFacts();
					}
					if (!_afterReturn || _model.IsBranch(i))
					{
						_Structural(i);
						return;
					}
				}
				else
				{
					_afterReturn = (_model.Op(i) == Opcode::RET);
				}
				Opcode op = _model.Op(i);
				if (_model.IsBranch(i))
				{
					// A branch that does nothing, or the inert bnt of an n-ary
					// compare: no node.
					if (_model.IsInert(i))
					{
						// The compare before it stays a statement until the
						// compare after the pprev takes it.
						if (!_AccIsAvailable() || !_IsCompareNode(*_pending))
						{
							_Fail("nary-no-compare", i);
						}
						_naryCompare = _pending;
					}
					_Structural(i);
					return;
				}
				switch (op)
				{
				case Opcode::LineNumber:
				case Opcode::Filename:
				case Opcode::LINK:
					_Structural(i);
					return;
				case Opcode::DUP:
					_Dup(i);
					return;
				case Opcode::PPREV:
					// The accumulator operand of the compare before the inert
					// bnt: the compare after it takes the operands of both.
					if (!_naryCompare)
					{
						_Fail("pprev-no-compare", i);
					}
					_Structural(i);
					_Push(nullptr, EntryKind::Prev, _naryCompare);
					_naryCompare = nullptr;
					return;
				case Opcode::TOSS:
					_Fail("toss-outside-switch", i);
				case Opcode::REST:
					_Push(_Node(i), EntryKind::Rest);
					return;
				default:
					break;
				}
				if (((op == Opcode::NOT) || (op == Opcode::BNOT) || (op == Opcode::NEG)) && !_AccIsAvailable() && (_acc.kind == FactKind::Unknown) &&
					_AccIsDeadAfter(i))
				{
					// An operation on a value that no node has (a test took
					// it), whose result no instruction reads: no code (LB2
					// script 250, sDoTakeOffFlight::changeState: "bnt L;
					// bnot; bnot; L: ldi 1").
					_Structural(i);
					return;
				}
				if (CodeModel::IsCompare(op) && !_stack.empty() && (_stack.back().kind == EntryKind::Prev))
				{
					_NaryCompare(i);
					return;
				}
				if ((op == Opcode::EQ) && !_stack.empty() && (_stack.back().kind == EntryKind::CaseDup))
				{
					_CaseCompare(i);
					return;
				}

				Consumption consumption = _GetInstructionConsumption(*_pos[i], nullptr);
				if (op == Opcode::RET)
				{
					consumption.cAccConsume = _returnsValue ? 1 : 0;
				}
				std::unique_ptr<ConsumptionNode> node = _Node(i);
				// The accumulator operand comes after the stack operands: a node
				// that was made before them is not the operand.
				bool accAvailable = _AccIsAvailable();
				if ((op == Opcode::RET) && accAvailable && _IsLoopNode(*_pending))
				{
					// A ret after a loop is a bare return, not a return of the value
					// of the loop.
					accAvailable = false;
				}
				if (!accAvailable && _IsSwappedOperands(i, consumption))
				{
					// A constant on the stack after the other operand, which
					// stays in the accumulator: the node of the
					// accumulator is an operand too, before the constant (address
					// order). The text has the operands in either order.
					node->AppendChild(_TakePending());
					for (auto &operand : _Pop(i, consumption.cStackConsume))
					{
						node->AppendChild(std::move(operand));
					}
					_UpdateFacts(i);
					_Append(std::move(node), consumption.cAccGenerate != 0, i);
					return;
				}
				for (auto &operand : _Pop(i, consumption.cStackConsume))
				{
					node->AppendChild(std::move(operand));
				}
				std::unique_ptr<ConsumptionNode> accOperand;
				if (consumption.cAccConsume)
				{
					if (accAvailable)
					{
						accOperand = _TakePending();
					}
					else if (op != Opcode::RET)
					{
						accOperand = _CopyOfAcc(i);
					}
					else if (_accDiffers)
					{
						// The text leaves another value in the accumulator.
						_Fail("acc-differs", i);
					}
					// A ret with no node of its own: the source can be (return)
					// or a return of the value that the accumulator holds, which
					// compile to the same code. It is a bare return.
				}
				if (_IsCallOp(op))
				{
					_HoistSlotStores(i, *node);
				}
				// A push of a node with no effect, when the accumulator has no fact:
				// a reader of the accumulator after it gets a copy of the node
				// (Longbow script 893, Table::at: "push; add").
				ConsumptionNode *pushed = ((op == Opcode::PUSH) && accAvailable && accOperand && !_HasEffect(*accOperand)) ? accOperand.get() : nullptr;
				if (accOperand)
				{
					node->AppendChild(std::move(accOperand));
				}
				bool noFact = (_acc.kind == FactKind::Unknown);
				_UpdateFacts(i);
				if (pushed && noFact)
				{
					_acc = { FactKind::Pushed, i };
					_pushedNode = pushed;
				}
				if (consumption.cStackGenerate)
				{
					_Push(std::move(node));
				}
				else
				{
					_Append(std::move(node), consumption.cAccGenerate != 0, i);
				}
			}

			// The node changes a variable or a property, or calls (not a copy,
			// which reads the value back).
			static bool _HasEffect(ConsumptionNode &node)
			{
				if (node._hasPos && !node._copy && (node.GetType() != ChunkType::ShortCircuitInstruction))
				{
					Opcode op = node.GetCode()->get_opcode();
					bool variableChange = _IsVariableOp(op) && (_IsVOStoreOperation(op) || _IsVOIncremented(op) || _IsVODecremented(op));
					bool propertyChange = (op == Opcode::ATOP) || (op == Opcode::STOP) || (op == Opcode::IPTOA) || (op == Opcode::DPTOA) ||
						(op == Opcode::IPTOS) || (op == Opcode::DPTOS);
					if (variableChange || propertyChange || _IsCallOp(op))
					{
						return true;
					}
				}
				for (size_t k = 0; k < node.GetChildCount(); ++k)
				{
					if (_HasEffect(*node.Child((int)k)))
					{
						return true;
					}
				}
				return false;
			}

			// A store of a value that can be read a second time, to a property
			// or to a variable that is not indexed.
			static bool _IsPlainStore(ConsumptionNode &node)
			{
				if (!node._hasPos || node._copy || (node.GetType() == ChunkType::ShortCircuitInstruction) || (node.GetChildCount() != 1) ||
					!_IsPureValue(*node.Child(0)))
				{
					return false;
				}
				Opcode op = node.GetCode()->get_opcode();
				return (op == Opcode::ATOP) || (_IsVariableOp(op) && _IsVOStoreOperation(op) && !_IsVOIndexed(op));
			}

			// A store of a value to a variable that is not indexed.
			static bool _IsVariableStore(ConsumptionNode &node)
			{
				if (!node._hasPos || node._copy || (node.GetType() == ChunkType::ShortCircuitInstruction) || (node.GetChildCount() != 1))
				{
					return false;
				}
				Opcode op = node.GetCode()->get_opcode();
				return _IsVariableOp(op) && _IsVOStoreOperation(op) && !_IsVOIndexed(op);
			}

			// The number that a slot pushes: pushi n, push0, push1, push2, or a
			// push of ldi n or of a plain store of ldi n.
			static bool _SlotNumber(ConsumptionNode &slot, int &number)
			{
				if (!slot._hasPos)
				{
					return false;
				}
				ConsumptionNode *node = &slot;
				Opcode op = node->GetCode()->get_opcode();
				if ((op == Opcode::PUSH) && (node->GetChildCount() == 1))
				{
					node = node->Child(0);
					if (_IsPlainStore(*node))
					{
						node = node->Child(0);
					}
					if (!node->_hasPos || (node->GetCode()->get_opcode() != Opcode::LDI))
					{
						return false;
					}
					op = Opcode::LDI;
				}
				switch (op)
				{
				case Opcode::LDI:
				case Opcode::PUSHI:
					number = node->GetCode()->get_first_operand();
					return true;
				case Opcode::PUSH0:
					number = 0;
					return true;
				case Opcode::PUSH1:
					number = 1;
					return true;
				case Opcode::PUSH2:
					number = 2;
					return true;
				default:
					return false;
				}
			}

			// The argument count of a call, and the selector and the argument
			// count of each message of a send, are not in the text. The
			// optimiser turns a push of a number that the accumulator holds
			// into "push", so the push of such a slot can take a store whose
			// value is that number (SQ1 VGA script 34: "ldi 3; aTop cycles;
			// push; push; push1; pushi 61; callb"; KQ6 script 370: "ldi 110;
			// aTop y; push; push0; super"). The store is a statement before the
			// call, and the push takes a copy of the value. A selector can
			// also be the value of a variable that the store sets (Hoyle
			// Classic script 700, BridgeHand::bid: "sat temp6; push; push0;
			// lat temp5; send"): the push reads the variable back. The store
			// must come before each instruction of the operands before the
			// slot (in the text, it comes before the call). Another effect in
			// a slot is a failure. A slot that reads back a variable that a
			// store of ldi n set (the optimiser deleted the load) is the
			// number: the text of a send needs the number of its arguments.
			void _HoistSlotStores(int reader, ConsumptionNode &call)
			{
				Opcode op = _model.Op(reader);
				size_t count = call.GetChildCount();
				if ((op == Opcode::SEND) || (op == Opcode::SELF) || (op == Opcode::SUPER))
				{
					// Each message: the selector, the argument count, the
					// arguments, and maybe a &rest.
					size_t k = 0;
					while (k < count)
					{
						ConsumptionNode *selector = call.Child((int)k);
						if (selector->_hasPos && (selector->GetCode()->get_opcode() == Opcode::REST))
						{
							++k;
							continue;
						}
						if (k + 1 >= count)
						{
							break;
						}
						ConsumptionNode *argc = call.Child((int)k + 1);
						_Slot(reader, call, *selector);
						_Slot(reader, call, *argc);
						int number = 0;
						if (!_SlotNumber(*argc, number))
						{
							break;
						}
						k += 2 + number;
					}
				}
				else if (count > 0)
				{
					_Slot(reader, call, *call.Child(0));
				}
			}

			// One slot of the call (see _HoistSlotStores).
			void _Slot(int reader, ConsumptionNode &call, ConsumptionNode &slot)
			{
				if (!slot._hasPos || (slot.GetCode()->get_opcode() != Opcode::PUSH) || (slot.GetChildCount() != 1))
				{
					return;
				}
				ConsumptionNode *value = slot.Child(0);
				if (value->_copy)
				{
					// A copy that reads back a variable that a store of ldi n
					// set: the slot is the number.
					auto immediate = (value->GetType() == ChunkType::ShortCircuitInstruction) ? _storeImmediate.find(_indexOf.at(&*value->GetCode())) : _storeImmediate.end();
					if (immediate != _storeImmediate.end())
					{
						std::unique_ptr<ConsumptionNode> number = std::make_unique<ConsumptionNode>();
						number->SetPos(_pos[immediate->second]);
						number->_copy = true;
						slot.StealChild(0);
						slot.AppendChild(std::move(number));
					}
					return;
				}
				bool plain = _IsPlainStore(*value);
				if (plain || _IsVariableStore(*value))
				{
					// The store must come before each instruction of the
					// operands before the slot: in the text, it comes before
					// the call.
					int storeFirst = _FirstInstruction(*value);
					for (size_t k = 0; call.Child((int)k) != &slot; ++k)
					{
						if (_FirstInstruction(*call.Child((int)k)) < storeFirst)
						{
							_Fail("slot-order", reader, "an operand before a store in a slot");
						}
					}
					std::unique_ptr<ConsumptionNode> store = slot.StealChild(0);
					if (plain)
					{
						slot.AppendChild(_DeepCopy(*store->Child(0)));
					}
					else
					{
						// The value is read back from the variable.
						std::unique_ptr<ConsumptionNode> copy = std::make_unique<ConsumptionNode>();
						copy->SetType(ChunkType::ShortCircuitInstruction);
						copy->SetPos(store->GetCode());
						copy->_copy = true;
						slot.AppendChild(std::move(copy));
					}
					int index = _indexOf.at(&*store->GetCode());
					_Append(std::move(store), false, index);
				}
				else if (_HasEffect(*value))
				{
					_Fail("slot-effect", reader);
				}
			}

			// The first instruction of the subtree that is not a copy;
			// INT_MAX when it has none.
			int _FirstInstruction(ConsumptionNode &node) const
			{
				int first = (node._hasPos && !node._copy) ? _indexOf.at(&*node.GetCode()) : INT_MAX;
				for (size_t k = 0; k < node.GetChildCount(); ++k)
				{
					first = (std::min)(first, _FirstInstruction(*node.Child((int)k)));
				}
				return first;
			}


			// No instruction reads the accumulator after the instruction:
			// the code after it (following each jmp) puts a value there
			// before an instruction reads it, or before a branch or a ret
			// that can read it.
			bool _AccIsDeadAfter(int i)
			{
				return _AccIsDeadAt(i + 1);
			}

			// No instruction reads the accumulator from the instruction k on
			// (see _AccIsDeadAfter).
			bool _AccIsDeadAt(int k)
			{
				for (int steps = 0; (steps < 64) && (k >= 0) && (k < _model.Size()); ++steps)
				{
					Opcode op = _model.Op(k);
					if (op == Opcode::JMP)
					{
						k = _model.Target(k);
						continue;
					}
					if ((op == Opcode::BT) || (op == Opcode::BNT))
					{
						return false;
					}
					if (op == Opcode::RET)
					{
						return !_returnsValue;
					}
					if ((op == Opcode::NOT) || (op == Opcode::BNOT) || (op == Opcode::NEG))
					{
						// It reads the value only to put another one there.
						++k;
						continue;
					}
					Consumption consumption = _GetInstructionConsumption(*_pos[k], nullptr);
					if (consumption.cAccConsume)
					{
						return false;
					}
					if (consumption.cAccGenerate)
					{
						return true;
					}
					++k;
				}
				return false;
			}

			// The innermost loop around the node is the value of a reader (its
			// parent is not a statement list).
			static bool _InLoopWithValue(ConsumptionNode &node)
			{
				ConsumptionNode *loop = node._parentWeak;
				while (loop && !_IsLoopNode(*loop))
				{
					loop = loop->_parentWeak;
				}
				ConsumptionNode *list = loop ? loop->_parentWeak : nullptr;
				switch (list ? list->GetType() : ChunkType::None)
				{
				case ChunkType::FunctionBody:
				case ChunkType::Then:
				case ChunkType::Else:
				case ChunkType::LoopBody:
				case ChunkType::CaseBody:
				case ChunkType::Step:
				case ChunkType::None:
					return false;
				default:
					return true;
				}
			}

			static bool _IsLoopNode(const ConsumptionNode &node)
			{
				ChunkType type = node.GetType();
				return (type == ChunkType::While) || (type == ChunkType::For) || (type == ChunkType::Do);
			}

			// A dup outside a switch: a copy of the value on the stack top
			// (the optimiser makes a dup of a push of the same value). The
			// dup does not take the value, so the value can be one that was
			// pushed before the sequence.
			void _Dup(int i)
			{
				if (!_stack.empty() && (_stack.back().kind == EntryKind::SwitchValue))
				{
					_Structural(i);
					if (_expectCaseDup)
					{
						// The start of the value of a case.
						_expectCaseDup = false;
						_Push(nullptr, EntryKind::CaseDup);
						return;
					}
					// A push of the value of the switch.
					if (!_IsPureValue(*_stack.back().ref))
					{
						_Fail("dup-no-value", i);
					}
					_Push(_DeepCopy(*_stack.back().ref));
					return;
				}
				if (!_stack.empty() && (_stack.back().kind == EntryKind::CaseDup) && (_stack.size() >= 2) &&
					(_stack[_stack.size() - 2].kind == EntryKind::SwitchValue) && _IsPureValue(*_stack[_stack.size() - 2].ref))
				{
					// A dup in the value of a case: a copy of the value of the
					// switch, which the dup at the start of the case value
					// copied (QfG4 floppy script 670, pMainDoor::doVerb:
					// "dup; dup; ldi 4; eq?": the case value is an and whose
					// first term compares the switch value).
					_Structural(i);
					_Push(_DeepCopy(*_stack[_stack.size() - 2].ref));
					return;
				}
				if (_stack.empty() || (_stack.back().kind != EntryKind::Value) || !_IsPureValue(*_stack.back().node))
				{
					_Fail("dup-no-value", i);
				}
				std::unique_ptr<ConsumptionNode> copy = _DeepCopy(*_stack.back().node);
				_Structural(i);
				_Push(std::move(copy));
			}

			// A compare instruction, or an n-ary compare.
			static bool _IsCompareNode(ConsumptionNode &node)
			{
				return (node.GetType() == ChunkType::Nary) || (node._hasPos && !node._copy && CodeModel::IsCompare(node.GetCode()->get_opcode()));
			}

			// The compare after a pprev: with the compare before the inert bnt,
			// one n-ary compare, as Sierra compiles (< a b c). It takes the
			// operands of the compare before it, and the accumulator.
			void _NaryCompare(int i)
			{
				Opcode op = _model.Op(i);
				bool accAvailable = _AccIsAvailable();
				if (_Depth() <= _list->floor)
				{
					_Fail("stack-underflow", i);
				}
				ConsumptionNode *before = _stack.back().ref;
				_stack.pop_back();
				std::unique_ptr<ConsumptionNode> last = accAvailable ? _TakePending() : _CopyOfAcc(i);
				if ((_list->node->GetChildCount() == 0) || (_list->node->Child((int)_list->node->GetChildCount() - 1) != before))
				{
					_Fail("nary-order", i);
				}
				std::unique_ptr<ConsumptionNode> beforeNode = _StealLast();
				ConsumptionNode *compare = (beforeNode->GetType() == ChunkType::Nary) ? beforeNode->Child(0) : beforeNode.get();
				if (compare->GetCode()->get_opcode() != op)
				{
					_Fail("nary-mixed", i);
				}
				_Unmade(_indexOf.at(&*compare->GetCode()));
				std::unique_ptr<ConsumptionNode> node = _Node(i);
				while (compare->GetChildCount() > 0)
				{
					node->AppendChild(compare->StealChild(0));
				}
				node->AppendChild(std::move(last));
				_UpdateFacts(i);
				_Append(_Wrap(ChunkType::Nary, std::move(node)), true, i);
			}

			// The eq? of the value of a case: the dup of the value of the
			// switch, and the case value in the accumulator.
			void _CaseCompare(int i)
			{
				bool accAvailable = _AccIsAvailable();
				if (_Depth() <= _list->floor)
				{
					_Fail("stack-underflow", i);
				}
				_stack.pop_back();
				_caseValue = accAvailable ? _TakePending() : _CopyOfAcc(i);
				_Structural(i);
				_UpdateFacts(i);
			}

			// A switch: the head pushes the value of the switch, which the
			// switch node has; the stack keeps its place until the toss. The
			// first case keeps the facts of the head; each other case starts
			// at a label, and so does the toss. The value of the switch is the
			// accumulator at its end.
			void _Switch(const Region &region)
			{
				int orStatements = _orStatements;
				std::unique_ptr<ConsumptionNode> switchNode = std::make_unique<ConsumptionNode>();
				switchNode->SetType(ChunkType::Switch);
				_Instruction(region.head);
				if (_stack.empty() || (_stack.back().kind != EntryKind::Value))
				{
					_Fail("switch-head", region.head);
				}
				StackEntry &top = _stack.back();
				switchNode->AppendChild(_Wrap(ChunkType::SwitchValue, std::move(top.node)));
				top.kind = EntryKind::SwitchValue;
				top.ref = switchNode->Child(0)->Child(0);
				bool first = true;
				for (const auto &item : region.cases)
				{
					if (!first)
					{
						_ResetFacts();
					}
					first = false;
					std::unique_ptr<ConsumptionNode> caseNode = std::make_unique<ConsumptionNode>();
					caseNode->SetType(ChunkType::Case);
					if (item->value)
					{
						caseNode->AppendChild(_Wrap(ChunkType::CaseCondition, _CaseValue(*item)));
					}
					_Structural(item->branch);
					if (item->value && (_acc.kind == FactKind::Unknown))
					{
						// The body of a case starts on the fall-through of the bnt of
						// the eq? of its value: the accumulator is 1 (QfG3 script 471,
						// uhuraCompete::changeState: "bnt; pushi 3; push1; push").
						_acc = { FactKind::One, NoIndex };
					}
					std::unique_ptr<ConsumptionNode> body = std::make_unique<ConsumptionNode>();
					body->SetType(ChunkType::CaseBody);
					ConsumptionNode *bodyRaw = body.get();
					caseNode->AppendChild(std::move(body));
					_SequenceInto(item->body.get(), bodyRaw, (item->caseJmp != NoIndex) ? item->caseJmp : region.toss);
					_Structural(item->caseJmp);
					switchNode->AppendChild(std::move(caseNode));
				}
				_ResetFacts();
				if (_stack.empty() || (_stack.back().kind != EntryKind::SwitchValue))
				{
					_Fail("stack-unbalanced", region.toss, "the toss takes no switch value");
				}
				_stack.pop_back();
				_Structural(region.toss);
				_AppendStructure(std::move(switchNode), orStatements, region.head);
			}

			// The value of a case: [dup, eq?], with the case value in the
			// accumulator at the eq?. It has no statement.
			std::unique_ptr<ConsumptionNode> _CaseValue(const Region &item)
			{
				ConsumptionNode holder;
				holder.SetType(ChunkType::None);
				List list = { &holder, _Depth(), {} };
				List *outer = _list;
				_list = &list;
				_pending = nullptr;
				_expectCaseDup = true;
				_caseValue.reset();
				_Region(*item.value);
				int at = _FirstOf(*item.value);
				if (_expectCaseDup || !_caseValue)
				{
					_Fail("case-value", at);
				}
				if (holder.GetChildCount() != 0)
				{
					_Fail("case-value-statement", at, "statements in the value of a case");
				}
				_EndList(list, at);
				_list = outer;
				_pending = nullptr;
				return std::move(_caseValue);
			}

			// A loop. The head, the continue point and the exit are labels. A
			// bt or bnt latch reads the accumulator at the end of the body.
			void _Loop(const Region &region)
			{
				int orStatements = _orStatements;
				int latch = region.branch;
				Opcode latchOp = _model.Op(latch);
				bool conditional = _model.IsLive(latch) && (latchOp != Opcode::JMP);
				if (conditional && region.step)
				{
					_Fail("for-conditional-latch", latch);
				}
				_ResetFacts();
				std::unique_ptr<ConsumptionNode> body = std::make_unique<ConsumptionNode>();
				body->SetType(ChunkType::LoopBody);
				std::unique_ptr<ConsumptionNode> latchValue;
				_SequenceInto(region.body.get(), body.get(), latch, conditional ? &latchValue : nullptr);
				std::unique_ptr<ConsumptionNode> step;
				if (region.step)
				{
					_ResetFacts();
					step = std::make_unique<ConsumptionNode>();
					step->SetType(ChunkType::Step);
					_SequenceInto(region.step.get(), step.get(), latch);
				}
				_Structural(latch);
				_ResetFacts();

				std::unique_ptr<ConsumptionNode> loop = std::make_unique<ConsumptionNode>();
				std::unique_ptr<ConsumptionNode> test;
				if (conditional)
				{
					// A do loop: it goes on while the latch branches.
					loop->SetType(ChunkType::Do);
					test = (latchOp == Opcode::BT) ? std::move(latchValue) : _Wrap(ChunkType::Invert, std::move(latchValue));
				}
				else
				{
					loop->SetType(region.step ? ChunkType::For : ChunkType::While);
					test = _TakeLoopTest(*body);
					if (!test)
					{
						test = _Wrap(ChunkType::TrueNode, nullptr);
					}
				}
				// The children in address order: the test of a do loop is at
				// its end.
				if (conditional)
				{
					loop->AppendChild(std::move(body));
					loop->AppendChild(_Wrap(ChunkType::Condition, std::move(test)));
				}
				else
				{
					loop->AppendChild(_Wrap(ChunkType::Condition, std::move(test)));
					loop->AppendChild(std::move(body));
				}
				if (step)
				{
					loop->AppendChild(std::move(step));
				}
				// The value of the loop is the accumulator at its exit (0 at the
				// exit of its test, the value of a breakif, the value before a
				// break): a test after it can read it (ICEMAN script 385,
				// localproc_02bc: the test of the outer loop is the inner loop).
				// An or as a statement in the loop gives its text another value at
				// an exit: then the loop has no value.
				_AppendStructure(std::move(loop), orStatements, region.head);
			}

			// The test of a while: a body that is one if whose else is the
			// break of the loop (the test is the first thing in the loop).
			// The body becomes the then-part of the if. Null when the body is
			// not such an if.
			std::unique_ptr<ConsumptionNode> _TakeLoopTest(ConsumptionNode &body)
			{
				if ((body.GetChildCount() != 1) || (body.Child(0)->GetType() != ChunkType::If))
				{
					return nullptr;
				}
				ConsumptionNode *ifNode = body.Child(0);
				ConsumptionNode *elseNode = ifNode->GetChild(ChunkType::Else);
				if (!elseNode || (elseNode->GetChildCount() != 1) || (elseNode->Child(0)->GetType() != ChunkType::Break) || (elseNode->Child(0)->_level != 1))
				{
					return nullptr;
				}
				std::unique_ptr<ConsumptionNode> ifOwned = body.StealChild(0);
				std::unique_ptr<ConsumptionNode> test = ifOwned->GetChild(ChunkType::Condition)->StealChild(0);
				ConsumptionNode *thenNode = ifOwned->GetChild(ChunkType::Then);
				while (thenNode->GetChildCount() > 0)
				{
					body.AppendChild(thenNode->StealChild(0));
				}
				return test;
			}

			// The facts after the instruction (sc, OPTIMIZE.CPP). Where the
			// optimiser believes more than the machine does (a store or an
			// increment to the stack leaves the accumulator alone), the
			// facts follow the machine.
			void _UpdateFacts(int i)
			{
				Opcode op = _model.Op(i);
				switch (op)
				{
				case Opcode::LDI:
					_acc = { FactKind::Immediate, i };
					return;
				case Opcode::PTOA:
					_acc = { FactKind::Property, i };
					return;
				case Opcode::PUSH:
				case Opcode::PUSHI:
				case Opcode::PUSH0:
				case Opcode::PUSH1:
				case Opcode::PUSH2:
				case Opcode::PUSHSELF:
				case Opcode::PTOS:
				case Opcode::REST:
				case Opcode::RET:
				case Opcode::BT:
				case Opcode::BNT:
				case Opcode::JMP:
				case Opcode::TOSS:
				case Opcode::LINK:
					return;
				case Opcode::ATOP:
					// The property changes; the accumulator does not. A pushed
					// node can read the property.
					if (_acc.kind == FactKind::Pushed)
					{
						_acc = Fact();
					}
					return;
				case Opcode::STOP:
				case Opcode::IPTOS:
				case Opcode::DPTOS:
					if (_acc.kind == FactKind::Pushed)
					{
						_acc = Fact();
					}
					// The property changes; the accumulator does not.
					if ((_acc.kind == FactKind::Property) && (_model.At(_acc.source).get_first_operand() == _model.At(i).get_first_operand()))
					{
						_acc = Fact();
					}
					return;
				default:
					break;
				}
				if (!_IsVariableOp(op))
				{
					_acc = Fact();
					return;
				}
				bool load = !_IsVOStoreOperation(op) && !_IsVOIncremented(op) && !_IsVODecremented(op);
				if (_IsVOPureStack(op))
				{
					if (!load && (_acc.kind == FactKind::Pushed))
					{
						// A pushed node can read the variable.
						_acc = Fact();
					}
					if (!load && (_acc.kind == FactKind::Variable))
					{
						// The variable changes; the accumulator does not. An
						// indexed store can change any variable of its kind.
						Opcode source = _model.Op(_acc.source);
						bool sameKind = (static_cast<BYTE>(source) & 0x03) == (static_cast<BYTE>(op) & 0x03);
						if (sameKind && (_IsVOIndexed(op) || (_model.At(_acc.source).get_first_operand() == _model.At(i).get_first_operand())))
						{
							_acc = Fact();
						}
					}
					return;
				}
				if (!load && !_IsVOIndexed(op) && (_acc.kind == FactKind::Immediate) && _IsVOStoreOperation(op))
				{
					// A store of ldi n: a copy that reads the variable back is
					// the number too.
					_storeImmediate[i] = _acc.source;
				}
				_acc = _IsVOIndexed(op) ? Fact() : Fact{ FactKind::Variable, i };
			}

			// A sequence in a new list whose node is container; the facts
			// go on. With value, the instruction at endIndex reads the
			// accumulator at the end of the sequence (the latch of a loop).
			void _SequenceInto(const Region *sequence, ConsumptionNode *container, int endIndex, std::unique_ptr<ConsumptionNode> *value = nullptr)
			{
				List list = { container, _Depth(), {} };
				List *outer = _list;
				_list = &list;
				_pending = nullptr;
				if (sequence)
				{
					_Region(*sequence);
				}
				if (value)
				{
					*value = _TakeAcc(endIndex);
				}
				_EndList(list, endIndex);
				_list = outer;
			}

			// The end of a sequence: the stack is back at its floor, and each
			// statement ended there.
			void _EndList(const List &list, int endIndex)
			{
				// Values that the sequence pushed, that have no effect, and that
				// no instruction takes (SQ1 VGA script 40, pinkShip::doVerb:
				// "pushi 40; ldi 16; jmp" to the toss of a switch, which pops
				// the 40, and the ret discards the rest): no code, when no
				// instruction after them reads them (_DroppedValuesAreUnread).
				std::vector<int> dropped;
				size_t count = 0;
				int lastPush = -1;
				while ((_Depth() - (int)count > list.floor) && (count < _stack.size()))
				{
					const StackEntry &entry = _stack[_stack.size() - 1 - count];
					if ((entry.kind != EntryKind::Value) || !entry.node || _HasEffect(*entry.node))
					{
						break;
					}
					if (entry.node->_hasPos)
					{
						lastPush = (std::max)(lastPush, _indexOf.at(&*entry.node->GetCode()));
					}
					++count;
				}
				if ((count > 0) && (_Depth() - (int)count == list.floor) && (lastPush >= 0) && _DroppedValuesAreUnread(lastPush))
				{
					for (size_t d = 0; d < count; ++d)
					{
						dropped.push_back(_FirstInstruction(*_stack.back().node));
						_UnmakeTree(*_stack.back().node);
						_stack.pop_back();
					}
					// A copy of a pushed node would read a node that is gone.
					if (_acc.kind == FactKind::Pushed)
					{
						_acc = Fact();
					}
				}
				if (_Depth() != list.floor)
				{
					_Fail("stack-unbalanced", endIndex, fmt::format("depth {0}, expected {1}", _Depth(), list.floor));
				}
				for (size_t n = 0; n < list.items.size(); ++n)
				{
					const auto &item = list.items[n];
					// A statement after such values was at their depth.
					int below = (int)std::count_if(dropped.begin(), dropped.end(), [&](int first) { return first < item.second; });
					if ((item.first != list.floor) && (item.first - list.floor > below) && !_StatementBeforeItsExpression(list, n))
					{
						_Fail("statement-in-expression", item.second, fmt::format("depth {0}, expected {1}", item.first, list.floor));
					}
				}
			}

			// The values that a sequence leaves on the stack, whose last push
			// is at lastPush, stay there with no instruction that reads them:
			// the code after it (following each jmp) gets to a ret, and each
			// instruction on the way takes only values that it pushed after
			// them, except a toss (which takes a value and reads nothing).
			// A branch, or a dup or pprev at their depth, reads them or can.
			bool _DroppedValuesAreUnread(int lastPush) const
			{
				int above = 0;
				int k = lastPush + 1;
				for (int steps = 0; (steps < 256) && (k >= 0) && (k < _model.Size()); ++steps)
				{
					Opcode op = _model.Op(k);
					if (op == Opcode::JMP)
					{
						k = _model.Target(k);
						continue;
					}
					if (op == Opcode::RET)
					{
						return true;
					}
					if ((op == Opcode::BT) || (op == Opcode::BNT))
					{
						return false;
					}
					if (((op == Opcode::DUP) || (op == Opcode::PPREV)) && (above == 0))
					{
						return false;
					}
					int pops = _model.Pops(k);
					if (pops > above)
					{
						if (op != Opcode::TOSS)
						{
							return false;
						}
						pops = above;
					}
					above += _model.Pushes(k) - pops;
					++k;
				}
				return false;
			}


			void _UnmakeTree(ConsumptionNode &node)
			{
				if (node._hasPos && !node._copy)
				{
					_Unmade(_indexOf.at(&*node.GetCode()));
				}
				for (size_t k = 0; k < node.GetChildCount(); ++k)
				{
					_UnmakeTree(*node.Child((int)k));
				}
			}

			// A statement in the middle of the operands of the statement after
			// it, whose value no instruction reads (QfG4 CD script 81,
			// ant::cue: "pushi 65; push1; pushi 65; push0; lag 0; send 4; ldi
			// 5; push; ..."). When each instruction of that statement before
			// it is a push of a number (a selector, an argument count), or when
			// the statement has no effect (LSL3 script 460, LightScript::changeState:
			// a div whose value no push takes), the statement comes before it in
			// the text with the same effects; the tree check skips its place
			// (ConsumptionNode::_hoisted).
			bool _StatementBeforeItsExpression(const List &list, size_t n)
			{
				// Only when the statement after it is that statement: a statement
				// between them would be in the operands of one of them.
				size_t next = n + 1;
				if ((next >= list.items.size()) || (list.items[next].first != list.floor))
				{
					return false;
				}
				ConsumptionNode *statement = list.node->Child((int)n);
				int first = _FirstInstruction(*statement);
				if (_HasEffect(*statement) && !_OnlyNumbersBefore(*list.node->Child((int)next), first))
				{
					return false;
				}
				statement->_hoisted = true;
				return true;
			}

			// Each instruction of the subtree before the instruction limit
			// (not copies) is a push of a number.
			bool _OnlyNumbersBefore(ConsumptionNode &node, int limit) const
			{
				if (node._hasPos && !node._copy && (_indexOf.at(&*node.GetCode()) < limit))
				{
					Opcode op = node.GetCode()->get_opcode();
					if ((op != Opcode::PUSHI) && (op != Opcode::PUSH0) && (op != Opcode::PUSH1) && (op != Opcode::PUSH2))
					{
						return false;
					}
				}
				for (size_t k = 0; k < node.GetChildCount(); ++k)
				{
					if (!_OnlyNumbersBefore(*node.Child((int)k), limit))
					{
						return false;
					}
				}
				return true;
			}

			// The value at the end of a sequence that is an and-term: its only
			// statement, or a group of its statements whose last one gives the
			// value (Pepper script 230, sTalkPoorRich::changeState: "ldi 1; sal
			// local1; pushi #setScript; ...; send 6; bnt" is the term
			// ((= local1 1) (global2 setScript: sGetKeyNotYet))).
			std::unique_ptr<ConsumptionNode> _OperandValue(const Region *sequence, int reader, const char *id)
			{
				ConsumptionNode holder;
				holder.SetType(ChunkType::None);
				List list = { &holder, _Depth(), {} };
				List *outer = _list;
				_list = &list;
				_pending = nullptr;
				if (sequence)
				{
					_Region(*sequence);
				}
				// With statements before it, the value is the last statement:
				// a copy from a fact would be an expression that the code
				// does not have.
				if ((holder.GetChildCount() > 0) && !_AccIsAvailable())
				{
					_Fail(id, reader, "statements in an operand, and the last one is not the value");
				}
				std::unique_ptr<ConsumptionNode> value = _TakeAcc(reader);
				_EndList(list, reader);
				_list = outer;
				if (holder.GetChildCount() == 0)
				{
					return value;
				}
				std::unique_ptr<ConsumptionNode> group = std::make_unique<ConsumptionNode>();
				group->SetType(ChunkType::Group);
				while (holder.GetChildCount() > 0)
				{
					group->AppendChild(holder.StealChild(0));
				}
				group->AppendChild(std::move(value));
				return group;
			}

			std::unique_ptr<ConsumptionNode> _Logical(ChunkType type, std::unique_ptr<ConsumptionNode> first, std::unique_ptr<ConsumptionNode> second)
			{
				std::unique_ptr<ConsumptionNode> node = std::make_unique<ConsumptionNode>();
				node->SetType(type);
				std::unique_ptr<ConsumptionNode> firstNode = std::make_unique<ConsumptionNode>();
				firstNode->SetType(ChunkType::First);
				firstNode->AppendChild(std::move(first));
				std::unique_ptr<ConsumptionNode> secondNode = std::make_unique<ConsumptionNode>();
				secondNode->SetType(ChunkType::Second);
				secondNode->AppendChild(std::move(second));
				node->AppendChild(std::move(firstNode));
				node->AppendChild(std::move(secondNode));
				return node;
			}

			void _If(const Region &region)
			{
				int orStatements = _orStatements;
				std::unique_ptr<ConsumptionNode> ifNode = std::make_unique<ConsumptionNode>();
				ifNode->SetType(ChunkType::If);
				_DeadTest(region.tests.front());

				// The test: the value before each bnt, as an and.
				std::unique_ptr<ConsumptionNode> test = _TakeAcc(region.tests.front());
				_Structural(region.tests.front());
				for (size_t k = 1; k < region.tests.size(); ++k)
				{
					const Region *term = (k - 1 < region.terms.size()) ? region.terms[k - 1].get() : nullptr;
					_Structural(region.tests[k]);
					if (!term || Layout(*term).empty())
					{
						// A bnt right after a bnt that a branch goes to: the
						// accumulator has values from two places.
						_Fail("empty-term", region.tests[k]);
					}
					std::unique_ptr<ConsumptionNode> value = _OperandValue(term, region.tests[k], "term-statement");
					test = _Logical(ChunkType::And, std::move(test), std::move(value));
				}
				ConsumptionNode *condition = ifNode->PrependChild();
				condition->SetType(ChunkType::Condition);
				condition->AppendChild(std::move(test));

				// The then-part starts with the facts of the branch: after the bnt of a
				// compare, the accumulator is 1.
				ConsumptionNode *lastTest = (region.tests.size() == 1) ? condition->Child(0) : nullptr;
				if (lastTest && _IsCompareNode(*lastTest) && (_acc.kind == FactKind::Unknown))
				{
					_acc = { FactKind::One, NoIndex };
				}
				std::unique_ptr<ConsumptionNode> thenNode = std::make_unique<ConsumptionNode>();
				thenNode->SetType(ChunkType::Then);
				ConsumptionNode *thenRaw = thenNode.get();
				ifNode->AppendChild(std::move(thenNode));
				_SequenceInto(region.thenPart.get(), thenRaw, region.tests.back());

				if (region.elseKind == ElseKind::Else)
				{
					// The else entry is a label.
					_Structural(region.branch);
					_ResetFacts();
					std::unique_ptr<ConsumptionNode> elseNode = std::make_unique<ConsumptionNode>();
					elseNode->SetType(ChunkType::Else);
					ConsumptionNode *elseRaw = elseNode.get();
					ifNode->AppendChild(std::move(elseNode));
					_SequenceInto(region.elsePart.get(), elseRaw, region.branch);
				}
				else if ((region.elseKind == ElseKind::Break) || (region.elseKind == ElseKind::Continue))
				{
					// The then-part is the rest of the sequence; a false test
					// leaves the loop or goes to its continue point.
					ifNode->AppendChild(_Wrap(ChunkType::Else, _LoopJump(region.elseKind == ElseKind::Break, region.level)));
					_ResetFacts();
					_Append(std::move(ifNode), false, region.tests.front());
					return;
				}

				// The join is a label. The value of the if is the accumulator
				// at its end.
				_ResetFacts();
				_AppendStructure(std::move(ifNode), orStatements, region.tests.front());
			}

			// A test that no path reaches has no value.
			void _DeadTest(int test)
			{
				if (!_model.IsLive(test))
				{
					_ResetFacts();
				}
			}

			void _Or(const Region &region)
			{
				_DeadTest(region.branch);
				std::unique_ptr<ConsumptionNode> first = _TakeAcc(region.branch);
				_Structural(region.branch);

				// The second operand.
				std::unique_ptr<ConsumptionNode> thenNode = std::make_unique<ConsumptionNode>();
				thenNode->SetType(ChunkType::Then);
				List list = { thenNode.get(), _Depth(), {} };
				List *outer = _list;
				_list = &list;
				_pending = nullptr;
				if (region.body)
				{
					_Region(*region.body);
				}
				bool isValue = (thenNode->GetChildCount() == 0) || ((thenNode->GetChildCount() == 1) && _AccIsAvailable());
				// Statements before the value of the second operand, and an
				// instruction after the or reads its value: the second operand
				// is a group of the statements, whose last one gives the value
				// (QfG1 VGA script 0, proc0_3: "ldi 17; sat temp0" before a
				// loop, then "not; bnt" after it). The join is the target that
				// the parser uses: a compiler can thread the bt past the bnt.
				int join = _model.ParseTarget(region.branch);
				bool isGroup = !isValue && _AccIsAvailable() && (join != NoIndex) && !_AccIsDeadAt(join);
				std::unique_ptr<ConsumptionNode> second = (isValue || isGroup) ? _TakeAcc(region.branch) : nullptr;
				_EndList(list, region.branch);
				_list = outer;
				_ResetFacts();
				if (isGroup)
				{
					std::unique_ptr<ConsumptionNode> group = std::make_unique<ConsumptionNode>();
					group->SetType(ChunkType::Group);
					while (thenNode->GetChildCount() > 0)
					{
						group->AppendChild(thenNode->StealChild(0));
					}
					group->AppendChild(std::move(second));
					second = std::move(group);
				}
				if (isValue || isGroup)
				{
					_Append(_Logical(ChunkType::Or, std::move(first), std::move(second)), true, region.branch);
					return;
				}
				// Statements in the second operand, and it is no group: as a
				// statement, (or c X) is (if (not c) X). It has no value: a
				// reader of it has no text.
				std::unique_ptr<ConsumptionNode> ifNode = std::make_unique<ConsumptionNode>();
				ifNode->SetType(ChunkType::If);
				ifNode->AppendChild(_Wrap(ChunkType::Condition, _Wrap(ChunkType::Invert, std::move(first))));
				ifNode->AppendChild(std::move(thenNode));
				_Append(std::move(ifNode), false, region.branch);
				++_orStatements;
				_accDiffers = true;
			}

			// A structure whose value is the accumulator at its end. With the
			// statement form of an or in it, the text has another value there:
			// the structure is no value, and a bare ret after it fails.
			void _AppendStructure(std::unique_ptr<ConsumptionNode> node, int orStatementsBefore, int index)
			{
				bool hasOrStatement = (_orStatements != orStatementsBefore);
				_Append(std::move(node), !hasOrStatement, index);
				if (hasOrStatement)
				{
					_accDiffers = true;
				}
			}

			// The forms of Sierra's source and of Snuffer: an if whose then-part
			// is empty and whose else is one break (or continue) of level 1 is
			// (breakif (not c)) (or contif). A loop whose body is such an if
			// took it as its test before.
			void _BreakIfForms(ConsumptionNode &node)
			{
				for (size_t i = 0; i < node.GetChildCount(); ++i)
				{
					_BreakIfForms(*node.Child((int)i));
				}
				if (node.GetType() != ChunkType::If)
				{
					return;
				}
				ConsumptionNode *thenNode = node.GetChild(ChunkType::Then);
				ConsumptionNode *elseNode = node.GetChild(ChunkType::Else);
				if (!thenNode || (thenNode->GetChildCount() != 0) || !elseNode || (elseNode->GetChildCount() != 1))
				{
					return;
				}
				ConsumptionNode *jump = elseNode->Child(0);
				if (((jump->GetType() != ChunkType::Break) && (jump->GetType() != ChunkType::Continue)) || (jump->_level != 1))
				{
					return;
				}
				// Only a statement: an if that is a value keeps its value.
				ConsumptionNode *list = node._parentWeak;
				switch (list ? list->GetType() : ChunkType::None)
				{
				case ChunkType::FunctionBody:
				case ChunkType::Then:
				case ChunkType::Else:
				case ChunkType::LoopBody:
				case ChunkType::CaseBody:
				case ChunkType::Step:
					break;
				default:
					return;
				}
				// The only statement of a while with a test, whose else is a
				// break: the AST pass LoopTestAbsorber makes an and of the two
				// tests.
				ConsumptionNode *loop = list->_parentWeak;
				// A break of a loop whose value a reader takes: the break leaves the
				// value of c, and (breakif (not c)) would leave 1.
				if ((jump->GetType() == ChunkType::Break) && _InLoopWithValue(node))
				{
					return;
				}
				if ((jump->GetType() == ChunkType::Break) && loop && (list->GetType() == ChunkType::LoopBody) && (list->GetChildCount() == 1) &&
					(loop->GetType() == ChunkType::While) && (loop->GetChild(ChunkType::Condition)->Child(0)->GetType() != ChunkType::TrueNode))
				{
					return;
				}
				ConsumptionNode *condition = node.GetChild(ChunkType::Condition);
				std::unique_ptr<ConsumptionNode> test = condition->StealChild(0);
				condition->AppendChild(_Wrap(ChunkType::Invert, std::move(test)));
				thenNode->AppendChild(elseNode->StealChild(0));
				node.StealChild(node.GetIndexOf(elseNode));
			}

			// The invariants of the tree: each instruction is in the tree one
			// time (as a node that is not a copy, or as a branch of the
			// structure), and the children of a node are in address order.
			void _CheckTree(ConsumptionNode &root)
			{
				std::vector<int> inTree(_model.Size(), 0);
				_CheckOrder(root, _indexOf, inTree);
				for (int i = 0; i < _model.Size(); ++i)
				{
					if ((inTree[i] + _structural[i] != 1) || (inTree[i] != _made[i]))
					{
						_Fail("not-in-tree-once", i, fmt::format("{0} times in the tree, {1} as a branch", inTree[i], _structural[i]));
					}
				}
			}

			// The first and the last instruction of the subtree (not copies),
			// or (-1, -1) when it has none.
			std::pair<int, int> _CheckOrder(ConsumptionNode &node, const std::map<const scii *, int> &indexOf, std::vector<int> &inTree)
			{
				std::pair<int, int> range(-1, -1);
				int previous = -1;
				for (size_t c = 0; c < node.GetChildCount(); ++c)
				{
					std::pair<int, int> child = _CheckOrder(*node.Child((int)c), indexOf, inTree);
					if ((child.first == -1) || node.Child((int)c)->_hoisted)
					{
						continue;
					}
					if (child.first <= previous)
					{
						_Fail("children-out-of-order", child.first);
					}
					previous = child.second;
					range.first = (range.first == -1) ? child.first : range.first;
					range.second = child.second;
				}
				if (node._hasPos && !node._copy)
				{
					int self = indexOf.at(&*node.GetCode());
					inTree[self]++;
					if (self <= previous)
					{
						_Fail("children-out-of-order", self);
					}
					range.first = (range.first == -1) ? self : range.first;
					range.second = self;
				}
				return range;
			}

			const CodeModel &_model;
			// The function returns a value: a ret reads the accumulator.
			bool _returnsValue;
			// The dead branches that a path of the tree goes through.
			const std::set<int> &_passedDeadBranches;
			std::vector<code_pos> _pos;
			std::vector<int> _structural;
			std::vector<int> _made;

			std::vector<StackEntry> _stack;
			List *_list = nullptr;
			// The node that gives the accumulator, while no other node took
			// its place; and when it was made.
			ConsumptionNode *_pending = nullptr;
			int _pendingTime = 0;
			Fact _acc;
			int _time = 0;
			// The index of each instruction.
			std::map<const scii *, int> _indexOf;
			// The compare before an inert bnt, until the pprev after it.
			ConsumptionNode *_naryCompare = nullptr;
			// The next dup of the value of a switch starts the value of a case.
			bool _expectCaseDup = false;
			// The case value that the eq? of a case took.
			std::unique_ptr<ConsumptionNode> _caseValue;
			// The count of the statement forms of or so far.
			int _orStatements = 0;
			// The text leaves another value in the accumulator than the code
			// (after the statement form of an or).
			bool _accDiffers = false;
			// FactKind::Pushed: the node that the push took.
			ConsumptionNode *_pushedNode = nullptr;
			// A store of ldi n to a variable (its instruction): the ldi.
			std::map<int, int> _storeImmediate;
			// The last live instruction is a ret (in the same sequence).
			bool _afterReturn = false;
		};
	}

	std::unique_ptr<ConsumptionNode> BuildValues(const CodeModel &model, const Region &root, std::list<scii> &code, bool returnsValue, const std::set<int> &passedDeadBranches)
	{
		Evaluator evaluator(model, code, returnsValue, passedDeadBranches);
		return evaluator.Run(root);
	}
}
