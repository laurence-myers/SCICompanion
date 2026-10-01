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

		// A value on the symbolic stack. A &rest is an entry too: it is an
		// argument of the call that takes the values below it, and does not
		// count in the depth.
		struct StackEntry
		{
			std::unique_ptr<ConsumptionNode> node;
			int time;
			bool rest;
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
			Evaluator(const CodeModel &model, std::list<scii> &code, bool returnsValue) :
				_model(model), _returnsValue(returnsValue), _structural(model.Size(), 0), _made(model.Size(), 0)
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
					depth += entry.rest ? 0 : 1;
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
				}
			}

			// The node that gives the accumulator now, when the reader can
			// take it: it is the last statement of the current list, and no
			// value was pushed after it (the operand of an instruction comes
			// after its stack operands). The copy of a dup counts as a push:
			// in the source, its value comes after the node.
			bool _AccIsAvailable() const
			{
				if (!_pending || (_list->node->GetChildCount() == 0) || (_list->node->Child((int)_list->node->GetChildCount() - 1) != _pending))
				{
					return false;
				}
				return _stack.empty() || (_stack.back().time < _pendingTime);
			}

			std::unique_ptr<ConsumptionNode> _TakePending()
			{
				size_t last = _list->node->GetChildCount() - 1;
				std::unique_ptr<ConsumptionNode> node = _list->node->StealChild(last);
				_list->items.pop_back();
				_pending = nullptr;
				return node;
			}

			// A copy of the value of the accumulator, from its fact.
			std::unique_ptr<ConsumptionNode> _CopyOfAcc(int reader)
			{
				if (_acc.kind == FactKind::Unknown)
				{
					std::string why = !_pending ? "no node" :
						((_list->node->GetChildCount() == 0) || (_list->node->Child((int)_list->node->GetChildCount() - 1) != _pending)) ? "a statement after the node" :
						"a push after the node";
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
					if (top.rest && !call)
					{
						_Fail("rest-outside-call", reader);
					}
					if (!top.rest)
					{
						--count;
					}
					operands.push_back(std::move(top.node));
					_stack.pop_back();
				}
				std::reverse(operands.begin(), operands.end());
				return operands;
			}

			void _Push(std::unique_ptr<ConsumptionNode> node, bool rest = false)
			{
				_stack.push_back({ std::move(node), ++_time, rest });
			}

			void _Region(const Region &region)
			{
				if ((region.kind != RegionKind::Sequence) && (region.kind != RegionKind::Code))
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
					_Fail("not-implemented", _FirstOf(region), "loop");
				case RegionKind::Switch:
				case RegionKind::Case:
					_Fail("not-implemented", _FirstOf(region), "switch");
				default:
					_Fail("not-implemented", _FirstOf(region), "break, continue, breakif or contif");
				}
			}

			void _Instruction(int i)
			{
				if (!_model.IsLive(i) && ((i == 0) || _model.IsLive(i - 1)))
				{
					// The start of dead code: no path gives the accumulator a
					// value.
					_ResetFacts();
				}
				Opcode op = _model.Op(i);
				if (_model.IsBranch(i))
				{
					// A dead branch, a branch that does nothing, or the inert
					// bnt of an n-ary compare: no node.
					if (_model.IsInert(i))
					{
						_Fail("not-implemented", i, "n-ary compare");
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
					_Fail("not-implemented", i, "pprev");
				case Opcode::TOSS:
					_Fail("toss-outside-switch", i);
				case Opcode::REST:
					_Push(_Node(i), true);
					return;
				default:
					break;
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
				if (_stack.empty() || _stack.back().rest || !_IsPureValue(*_stack.back().node))
				{
					_Fail("dup-no-value", i);
				}
				std::unique_ptr<ConsumptionNode> copy = _DeepCopy(*_stack.back().node);
				_Structural(i);
				_Push(std::move(copy));
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
			// go on.
			void _SequenceInto(const Region *sequence, ConsumptionNode *container, int endIndex)
			{
				List list = { container, _Depth(), {} };
				List *outer = _list;
				_list = &list;
				_pending = nullptr;
				if (sequence)
				{
					_Region(*sequence);
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
				if ((region.elseKind == ElseKind::Break) || (region.elseKind == ElseKind::Continue))
				{
					_Fail("not-implemented", region.tests.front(), "if with a loop else");
				}
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

				// The join is a label. The value of the if is the accumulator
				// at its end.
				_ResetFacts();
				_Append(std::move(ifNode), true, region.tests.front());
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
				std::unique_ptr<ConsumptionNode> second = _OperandValue(region.body.get(), region.branch, "or-statement");
				_ResetFacts();
				_Append(_Logical(ChunkType::Or, std::move(first), std::move(second)), true, region.branch);
			}

			// The invariants of the tree: each instruction is in the tree one
			// time (as a node that is not a copy, or as a branch of the
			// structure), and the children of a node are in address order.
			void _CheckTree(ConsumptionNode &root)
			{
				std::vector<int> inTree(_model.Size(), 0);
				std::map<const scii *, int> indexOf;
				for (int i = 0; i < _model.Size(); ++i)
				{
					indexOf[&*_pos[i]] = i;
				}
				_CheckOrder(root, indexOf, inTree);
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
		};
	}

	std::unique_ptr<ConsumptionNode> BuildValues(const CodeModel &model, const Region &root, std::list<scii> &code, bool returnsValue)
	{
		Evaluator evaluator(model, code, returnsValue);
		return evaluator.Run(root);
	}
}
