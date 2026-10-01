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
					if ((i == 0) || _model.IsLive(i - 1))
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
				if (consumption.cAccConsume)
				{
					if (accAvailable)
					{
						node->AppendChild(_TakePending());
					}
					else if (op != Opcode::RET)
					{
						node->AppendChild(_CopyOfAcc(i));
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
				_UpdateFacts(i);
				if (consumption.cStackGenerate)
				{
					_Push(std::move(node));
				}
				else
				{
					_Append(std::move(node), consumption.cAccGenerate != 0, i);
				}
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
				_Append(std::move(loop), false, region.head);
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
				case Opcode::ATOP:
				case Opcode::REST:
				case Opcode::RET:
				case Opcode::BT:
				case Opcode::BNT:
				case Opcode::JMP:
				case Opcode::TOSS:
				case Opcode::LINK:
					return;
				case Opcode::STOP:
				case Opcode::IPTOS:
				case Opcode::DPTOS:
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
				if (_Depth() != list.floor)
				{
					_Fail("stack-unbalanced", endIndex, fmt::format("depth {0}, expected {1}", _Depth(), list.floor));
				}
				for (const auto &item : list.items)
				{
					if (item.first != list.floor)
					{
						_Fail("statement-in-expression", item.second, fmt::format("depth {0}, expected {1}", item.first, list.floor));
					}
				}
			}

			// The value at the end of a sequence that is an operand (an
			// and-term, the second operand of an or): its only statement.
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
				if ((holder.GetChildCount() > 1) || ((holder.GetChildCount() == 1) && !_AccIsAvailable()))
				{
					_Fail(id, reader, "statements in an operand");
				}
				std::unique_ptr<ConsumptionNode> value = _TakeAcc(reader);
				_EndList(list, reader);
				_list = outer;
				return value;
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

				// The then-part starts with the facts of the branch.
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
				std::unique_ptr<ConsumptionNode> second = isValue ? _TakeAcc(region.branch) : nullptr;
				_EndList(list, region.branch);
				_list = outer;
				_ResetFacts();
				if (isValue)
				{
					_Append(_Logical(ChunkType::Or, std::move(first), std::move(second)), true, region.branch);
					return;
				}
				// Statements in the second operand: as a statement, (or c X) is
				// (if (not c) X). It has no value: a reader of it has no text.
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
					if (child.first == -1)
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
