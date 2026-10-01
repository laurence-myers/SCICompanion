#include "stdafx.h"
#include "MeaningCheck.h"
#include "CompiledScript.h"
#include "DecompilerCore.h"
#include "DecompileScript.h"
#include "DecompilerResults.h"
#include "GameFolderHelper.h"
#include "PMachine.h"
#include "ScopeCode.h"
#include "format.h"
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

// The value of an operation on two numbers (Evaluate.cpp).
bool EvalBinaryOp(Opcode opcode, uint16_t aUnsigned, uint16_t bUnsigned, uint16_t &result);

namespace meaning
{
	const char *VerdictName(Verdict verdict)
	{
		switch (verdict)
		{
		case Verdict::Same:
			return "SAME";
		case Verdict::Diff:
			return "DIFF";
		default:
			return "UNCOMPARED";
		}
	}

	namespace
	{
		// The limits of a walk: the steps of one run to an effect (a loop
		// with no effect spins), and the pairs of effects of a function.
		const int MaxRunSteps = 20000;
		const int MaxEffectPairs = 100000;
		// The longest text of a value (a value that grows on each turn of a
		// loop, or doubles with each instruction).
		const size_t MaxExpressionText = 4000;
		// The tests that a run looks past, to find a test with no effect; more
		// when the two sides differ at a test (a chain of tests that all get to
		// the same effect, where the other side has fewer tests).
		const int MaxTestLookahead = 2;
		const int MaxDeepTestLookahead = 12;

		// A form that the check does not read: the function is UNCOMPARED.
		struct Unreadable
		{
			std::string reason;
		};

		// The expression of a value. Two values are equal when their texts
		// are equal.
		struct Expr;
		using ExprPtr = std::shared_ptr<const Expr>;
		struct Expr
		{
			// A variable: its kind ('g', 'l', 't', 'p', or 'P' for a property)
			// and its index; 0 for another expression. An indexed variable has
			// the index expression in args.
			char varKind = 0;
			uint16_t varIndex = 0;
			bool indexed = false;
			// A value that an effect made: a call result, or the value of a
			// variable before a store or a call. tagPair is the pair of the
			// effects, tagAge the occurrences of the pair since then (a value
			// of an earlier turn of a loop is another value); tagBase is the
			// head with no tag. -1: no tag.
			int tagPair = -1;
			int tagAge = 0;
			std::string tagBase;
			std::string head;
			std::vector<ExprPtr> args;
			std::string text;
		};

		std::shared_ptr<Expr> _New(const std::string &head, std::vector<ExprPtr> args)
		{
			std::shared_ptr<Expr> expr = std::make_shared<Expr>();
			expr->head = head;
			expr->args = std::move(args);
			expr->text = head;
			if (!expr->args.empty())
			{
				expr->text += "(";
				for (size_t i = 0; i < expr->args.size(); i++)
				{
					expr->text += ((i > 0) ? " " : "") + expr->args[i]->text;
				}
				expr->text += ")";
			}
			if (expr->text.size() > MaxExpressionText)
			{
				throw Unreadable{ "large-expression" };
			}
			return expr;
		}

		// A number: its value.
		bool _IsNumber(const ExprPtr &expr, uint16_t &value)
		{
			if (expr->args.empty() && !expr->head.empty() && isdigit((unsigned char)expr->head[0]))
			{
				value = (uint16_t)std::stoul(expr->head);
				return true;
			}
			return false;
		}

		// The operations whose operands can be grouped and ordered in any
		// way: the compilers group (+ a b c) in two ways, and fold numbers.
		bool _IsAssociative(const std::string &op)
		{
			return (op == "add") || (op == "mul") || (op == "and") || (op == "or") || (op == "xor");
		}

		// The value of an operation on numbers, as a compiler folds it; false
		// for an operation that the check does not fold.
		bool _Fold(const std::string &op, const std::vector<uint16_t> &values, uint16_t &result)
		{
			if (values.size() == 1)
			{
				uint16_t value = values[0];
				if (op == "bnot")
				{
					result = (uint16_t)~value;
				}
				else if (op == "neg")
				{
					result = (uint16_t)(0 - value);
				}
				else if (op == "not")
				{
					result = (value == 0) ? 1 : 0;
				}
				else
				{
					return false;
				}
				return true;
			}
			if (_IsAssociative(op))
			{
				result = values[0];
				for (size_t i = 1; i < values.size(); i++)
				{
					uint16_t value = values[i];
					result = (op == "add") ? (uint16_t)(result + value) : ((op == "mul") ? (uint16_t)(result * value) :
						((op == "and") ? (uint16_t)(result & value) : ((op == "or") ? (uint16_t)(result | value) : (uint16_t)(result ^ value))));
				}
				return true;
			}
			if (values.size() != 2)
			{
				return false;
			}
			// The other operations of two numbers, as the PMachine does them
			// (the compiler folds them the same way).
			static const Opcode BinaryOps[] = { Opcode::SUB, Opcode::DIV, Opcode::MOD, Opcode::SHR, Opcode::SHL, Opcode::EQ, Opcode::NE, Opcode::GT,
				Opcode::GE, Opcode::LT, Opcode::LE, Opcode::UGT, Opcode::UGE, Opcode::ULT, Opcode::ULE };
			for (Opcode binary : BinaryOps)
			{
				if (op == OpcodeToName(binary, 0))
				{
					// A division by 0, and -32768 / -1, stop the PMachine: no
					// value.
					bool divide = (binary == Opcode::DIV) || (binary == Opcode::MOD);
					if (divide && ((values[1] == 0) || ((values[0] == 0x8000) && (values[1] == 0xffff))))
					{
						return false;
					}
					return EvalBinaryOp(binary, values[0], values[1], result);
				}
			}
			return false;
		}

		// The number that does not change the value of an associative
		// operation (0 for add).
		uint16_t _Identity(const std::string &op)
		{
			return (op == "mul") ? 1 : ((op == "and") ? 0xffff : 0);
		}

		// An expression in its normal form: the operands of an associative
		// operation in one list, with its numbers folded into one number at
		// the end (none when it is the identity), and an operation on
		// numbers folded. The other operands keep their order: they are
		// evaluated in it.
		ExprPtr Make(const std::string &head, std::vector<ExprPtr> args = {})
		{
			if (_IsAssociative(head))
			{
				std::vector<ExprPtr> flat;
				std::vector<uint16_t> numbers;
				for (const ExprPtr &arg : args)
				{
					uint16_t value;
					if (_IsNumber(arg, value))
					{
						numbers.push_back(value);
					}
					else if ((arg->head == head) && !arg->varKind)
					{
						for (const ExprPtr &inner : arg->args)
						{
							if (_IsNumber(inner, value))
							{
								numbers.push_back(value);
							}
							else
							{
								flat.push_back(inner);
							}
						}
					}
					else
					{
						flat.push_back(arg);
					}
				}
				uint16_t folded = _Identity(head);
				if (numbers.size() == 1)
				{
					folded = numbers[0];
				}
				else if (!numbers.empty())
				{
					_Fold(head, numbers, folded);
				}
				if (flat.empty())
				{
					return _New(std::to_string(folded), {});
				}
				if (folded != _Identity(head))
				{
					flat.push_back(_New(std::to_string(folded), {}));
				}
				if (flat.size() == 1)
				{
					return flat[0];
				}
				return _New(head, std::move(flat));
			}
			if (!args.empty())
			{
				std::vector<uint16_t> values;
				for (const ExprPtr &arg : args)
				{
					uint16_t value;
					if (!_IsNumber(arg, value))
					{
						break;
					}
					values.push_back(value);
				}
				uint16_t result;
				if ((values.size() == args.size()) && _Fold(head, values, result))
				{
					return _New(std::to_string(result), {});
				}
			}
			return _New(head, std::move(args));
		}

		ExprPtr Const(uint16_t value)
		{
			return Make(std::to_string(value));
		}

		bool IsConst(const ExprPtr &expr, uint16_t &value)
		{
			return _IsNumber(expr, value);
		}

		ExprPtr Var(char kind, uint16_t index, ExprPtr indexExpr = nullptr)
		{
			std::vector<ExprPtr> args;
			if (indexExpr)
			{
				args.push_back(indexExpr);
			}
			std::shared_ptr<Expr> expr = _New(fmt::format("{0}{1}{2}", kind, index, indexExpr ? "[]" : ""), std::move(args));
			expr->varKind = kind;
			expr->varIndex = index;
			expr->indexed = (indexExpr != nullptr);
			return expr;
		}

		// A value that the effects of the pair made (Expr::tagPair), with the
		// operands args.
		ExprPtr Tagged(const std::string &base, int pair, int age = 0, std::vector<ExprPtr> args = {})
		{
			std::string head = fmt::format("{0}{1}", base, pair);
			if (age > 0)
			{
				head += fmt::format("^{0}", age);
			}
			std::shared_ptr<Expr> expr = _New(head, std::move(args));
			expr->tagPair = pair;
			expr->tagAge = age;
			expr->tagBase = base;
			return expr;
		}

		// The values of the pair that a state keeps when the pair occurs
		// again: each is one turn older.
		const int MaxTagAge = 2;
		ExprPtr Age(const ExprPtr &expr, int pair)
		{
			if (!expr)
			{
				return expr;
			}
			std::vector<ExprPtr> args;
			bool argsChanged = false;
			for (const ExprPtr &arg : expr->args)
			{
				args.push_back(Age(arg, pair));
				argsChanged = argsChanged || (args.back() != arg);
			}
			if (expr->tagPair == pair)
			{
				if (expr->tagAge >= MaxTagAge)
				{
					throw Unreadable{ "value-of-an-old-turn" };
				}
				return Tagged(expr->tagBase, pair, expr->tagAge + 1, std::move(args));
			}
			if (!argsChanged)
			{
				return expr;
			}
			std::shared_ptr<Expr> copy = _New(expr->head, std::move(args));
			copy->varKind = expr->varKind;
			copy->varIndex = expr->varIndex;
			copy->indexed = expr->indexed;
			copy->tagPair = expr->tagPair;
			copy->tagAge = expr->tagAge;
			copy->tagBase = expr->tagBase;
			return copy;
		}

		// The variables that a store or a call can change.
		using VarPredicate = std::function<bool(char kind, uint16_t index, bool indexed)>;

		// The expression with each variable that the predicate names replaced
		// by its value before the effect of the pair: "<var>@<pair>".
		ExprPtr Freeze(const ExprPtr &expr, const VarPredicate &changed, int pair)
		{
			if (!expr)
			{
				return expr;
			}
			std::vector<ExprPtr> args;
			bool argsChanged = false;
			for (const ExprPtr &arg : expr->args)
			{
				args.push_back(Freeze(arg, changed, pair));
				argsChanged = argsChanged || (args.back() != arg);
			}
			if (expr->varKind && changed(expr->varKind, expr->varIndex, expr->indexed))
			{
				return Tagged(expr->head + "@", pair, 0, std::move(args));
			}
			if (!argsChanged)
			{
				return expr;
			}
			std::shared_ptr<Expr> copy = _New(expr->head, std::move(args));
			copy->varKind = expr->varKind;
			copy->varIndex = expr->varIndex;
			copy->indexed = expr->indexed;
			copy->tagPair = expr->tagPair;
			copy->tagAge = expr->tagAge;
			copy->tagBase = expr->tagBase;
			return copy;
		}

		// The value of the last store to a variable on a path, and its truth
		// when a test on the path read it (as State::accTruth).
		struct Binding
		{
			ExprPtr value;
			int truth = 0;
		};

		// The state of the machine on one path.
		struct State
		{
			int pc = 0;
			ExprPtr acc;
			ExprPtr prev;
			// The extra arguments of a rest instruction, for the next call.
			ExprPtr rest;
			std::vector<ExprPtr> stack;
			// The truth of the value in the accumulator, when a test on this
			// path read it: 1 true, -1 false, 0 not known.
			int accTruth = 0;
			// The value of the last store to a plain variable or a property
			// on this path (key: the text of the variable): a load gives it.
			// So a load and a value that the optimiser reused (it deleted the
			// load) are one value. A loop head drops them: a value of the last
			// turn of the loop is not one value.
			std::map<std::string, Binding> bindings;

			// The text of the state. With accDead, the accumulator is left
			// out: no instruction reads its value.
			std::string Key(bool accDead = false) const
			{
				std::string key = fmt::format("{0}|{1}|{2}|{3}|{4}|", pc, (acc && !accDead) ? acc->text : "", prev ? prev->text : "", rest ? rest->text : "", accDead ? 0 : accTruth);
				for (const ExprPtr &value : stack)
				{
					key += value->text + ",";
				}
				key += "|";
				for (const auto &binding : bindings)
				{
					key += fmt::format("{0}={1}:{2},", binding.first, binding.second.value->text, binding.second.truth);
				}
				return key;
			}

			// Each value of the pair is one turn older (meaning::Age).
			void Age(int pair)
			{
				acc = meaning::Age(acc, pair);
				prev = meaning::Age(prev, pair);
				rest = meaning::Age(rest, pair);
				for (ExprPtr &value : stack)
				{
					value = meaning::Age(value, pair);
				}
				for (auto &binding : bindings)
				{
					binding.second.value = meaning::Age(binding.second.value, pair);
				}
			}

			void Freeze(const VarPredicate &changed, int pair)
			{
				acc = meaning::Freeze(acc, changed, pair);
				prev = meaning::Freeze(prev, changed, pair);
				// The extra arguments of a rest are the parameters when it ran.
				if (rest && (rest->tagPair < 0) && changed('p', 0, true))
				{
					rest = Tagged(rest->head + "@", pair);
				}
				for (ExprPtr &value : stack)
				{
					value = meaning::Freeze(value, changed, pair);
				}
				for (auto it = bindings.begin(); it != bindings.end(); )
				{
					ExprPtr var = Var(it->first[0], (uint16_t)std::stoul(it->first.substr(1)));
					if (changed(var->varKind, var->varIndex, false))
					{
						it = bindings.erase(it);
					}
					else
					{
						it->second.value = meaning::Freeze(it->second.value, changed, pair);
						++it;
					}
				}
			}
		};

		enum class EffectKind
		{
			Call,
			Store,
			Test,
			Return,
			Spin,
		};

		const char *EffectKindName(EffectKind kind)
		{
			switch (kind)
			{
			case EffectKind::Call:
				return "call";
			case EffectKind::Store:
				return "store";
			case EffectKind::Test:
				return "test";
			case EffectKind::Return:
				return "return";
			default:
				return "spin";
			}
		}

		// The next effect of a path: its kind, and a text that has its target
		// and its values.
		struct Effect
		{
			EffectKind kind = EffectKind::Spin;
			std::string text;
			// Test: the value whose truth chooses the outcome (with the nots
			// removed), and the instruction of each outcome.
			ExprPtr test;
			int onTrue = -1;
			int onFalse = -1;
		};

		// A variable of the load and store opcodes (64 to 127): bits 0-1 the
		// kind, bit 2 the stack, bit 3 the index in the accumulator, bits
		// 4-5 the operation (load, store, increment, decrement).
		const char VarKinds[] = { 'g', 'l', 't', 'p' };

		class Walker
		{
		public:
			Walker(const Function &function, bool returnsValue) : _function(function), _returnsValue(returnsValue)
			{
				_loopHead.assign(function.code.size() + 1, false);
				// The instructions that control gets to from the entry: a
				// branch back in dead code makes no loop head.
				std::vector<bool> live(function.code.size(), false);
				std::vector<int> pending = { 0 };
				while (!pending.empty())
				{
					int i = pending.back();
					pending.pop_back();
					if ((i < 0) || (i >= (int)function.code.size()) || live[i])
					{
						continue;
					}
					live[i] = true;
					const Instruction &inst = function.code[i];
					if (inst.target >= 0)
					{
						pending.push_back(inst.target);
					}
					if ((inst.op != Opcode::JMP) && (inst.op != Opcode::RET))
					{
						pending.push_back(i + 1);
					}
				}
				for (size_t i = 0; i < function.code.size(); i++)
				{
					const Instruction &inst = function.code[i];
					if (live[i] && (inst.target >= 0) && (inst.target <= (int)i))
					{
						_loopHead[inst.target] = true;
					}
					_hasLea = _hasLea || (inst.op == Opcode::LEA);
					_hasPprev = _hasPprev || (inst.op == Opcode::PPREV);
				}
				_FindAccLiveness();
			}

			// The text of the state, without the accumulator where its value
			// is dead.
			std::string KeyOf(const State &state) const
			{
				bool dead = (state.pc >= 0) && (state.pc < (int)_accLive.size()) && !_accLive[state.pc];
				return state.Key(dead);
			}

			State Start() const
			{
				State state;
				state.acc = Make("acc0");
				state.prev = _hasPprev ? Make("prev0") : nullptr;
				_ArriveAt(state, 0);
				return state;
			}

			int Offset(const State &state) const
			{
				return (state.pc < (int)_function.code.size()) ? _function.code[state.pc].offset : -1;
			}

			// Runs the instructions that have no effect, and stops at the
			// next effect (state.pc is its instruction). depth counts the
			// tests that the run looks past.
			Effect RunToEffect(State &state, int depth = 0, int maxDepth = MaxTestLookahead) const
			{
				for (int steps = 0; ; steps++)
				{
					if (state.pc >= (int)_function.code.size())
					{
						// The code runs into the code after the function (the
						// decompiler found the wrong end of the function).
						throw Unreadable{ "runs-past-end" };
					}
					if (steps > MaxRunSteps)
					{
						Effect effect;
						effect.kind = EffectKind::Spin;
						effect.text = "spin";
						return effect;
					}
					Effect effect;
					if (_Describe(state, effect))
					{
						if ((effect.kind == EffectKind::Test) && (depth < maxDepth))
						{
							// A test whose two outcomes get to the same effect in
							// the same state (an empty then-part, a last case with
							// no body) is no effect.
							State onTrue = Branch(state, effect, true, false);
							State onFalse = Branch(state, effect, false, false);
							Effect nextTrue = RunToEffect(onTrue, depth + 1, maxDepth);
							Effect nextFalse = RunToEffect(onFalse, depth + 1, maxDepth);
							int truth = (onTrue.accTruth == onFalse.accTruth) ? onTrue.accTruth : 0;
							onTrue.accTruth = truth;
							onFalse.accTruth = truth;
							// The truths that the tests gave the variables (no store
							// is between: a store is an effect): a truth that the
							// two outcomes do not share is not known, and a binding
							// that a test made on one outcome only is dropped.
							_MergeTestBindings(onTrue, onFalse);
							_MergeTestBindings(onFalse, onTrue);
							if ((nextTrue.text == nextFalse.text) && (KeyOf(onTrue) == KeyOf(onFalse)))
							{
								state = onTrue;
								return nextTrue;
							}
						}
						return effect;
					}
					_Step(state);
				}
			}

			// The state after the effect that RunToEffect gave (not a test).
			// pair names the effect in the values that it makes.
			State Apply(const State &before, const Effect &effect, int pair) const
			{
				State state = before;
				state.Age(pair);
				const Instruction &inst = _function.code[state.pc];
				if (effect.kind == EffectKind::Call)
				{
					int words = 0;
					_CallFrame(state, inst, words);
					state.stack.resize(state.stack.size() - words);
					state.rest = nullptr;
					state.Freeze(_CallChanges(), pair);
					state.acc = Tagged("#", pair);
					state.accTruth = 0;
					// The compares of the called code set the previous value.
					if (state.prev)
					{
						state.prev = Tagged("prev#", pair);
					}
				}
				else
				{
					_ApplyStore(state, inst, pair);
				}
				_ArriveAt(state, state.pc + 1);
				return state;
			}

			// The state of an outcome of a test.
			State Branch(const State &before, const Effect &effect, bool truth, bool zeroOrOne = true) const
			{
				State state = before;
				state.accTruth = _AccTruth(state.acc, effect.test, truth);
				// A compare and a not give 1 or 0: on an outcome of a test, the
				// value is that number (not in the lookahead of RunToEffect, which
				// merges the two outcomes) (QfG3 script 471: the text passes TRUE
				// where the code passes the value of the eq? that the test
				// read).
				if (zeroOrOne && _IsZeroOrOne(state.acc))
				{
					state.acc = Const((state.accTruth > 0) ? 1 : 0);
				}
				// A variable whose value is the tested value has its truth: a
				// load of it again (the compiler does not reuse the value) is
				// no test.
				for (auto &binding : state.bindings)
				{
					if (binding.second.value->text == effect.test->text)
					{
						binding.second.truth = truth ? 1 : -1;
					}
				}
				if (effect.test->varKind && !effect.test->indexed && (effect.test->tagPair < 0) && (state.bindings.count(effect.test->text) == 0))
				{
					state.bindings[effect.test->text] = Binding{ effect.test, truth ? 1 : -1 };
				}
				_ArriveAt(state, truth ? effect.onTrue : effect.onFalse);
				return state;
			}

		private:
			// The bindings of state that other shares (see RunToEffect).
			static void _MergeTestBindings(State &state, State &other)
			{
				for (auto it = state.bindings.begin(); it != state.bindings.end(); )
				{
					auto found = other.bindings.find(it->first);
					if (found == other.bindings.end())
					{
						it = state.bindings.erase(it);
						continue;
					}
					if (found->second.truth != it->second.truth)
					{
						it->second.truth = 0;
						found->second.truth = 0;
					}
					++it;
				}
			}

			void _ArriveAt(State &state, int pc) const
			{
				state.pc = pc;
				if ((pc >= 0) && (pc < (int)_loopHead.size()) && _loopHead[pc])
				{
					state.bindings.clear();
					// A continue out of a switch leaves the switch value on the
					// stack, and no instruction reads it: the stack keeps the
					// depth of the loop head.
					if ((pc < (int)_function.depth.size()) && (_function.depth[pc] >= 0) && ((int)state.stack.size() > _function.depth[pc]))
					{
						state.stack.resize(_function.depth[pc]);
					}
				}
			}

			// A value that is 1 or 0: a compare or a not.
			static bool _IsZeroOrOne(const ExprPtr &value)
			{
				static const char *const ZeroOrOne[] = { "not", "eq?", "ne?", "gt?", "ge?", "lt?", "le?", "ugt?", "uge?", "ult?", "ule?" };
				for (const char *head : ZeroOrOne)
				{
					if (value->args.size() && (value->head == head))
					{
						return true;
					}
				}
				return false;
			}

			// The truth of the accumulator when the test value (the
			// accumulator less its nots) has the truth.
			static int _AccTruth(const ExprPtr &acc, const ExprPtr &test, bool truth)
			{
				bool value = truth;
				ExprPtr expr = acc;
				while (expr != test)
				{
					value = !value;
					expr = expr->args[0];
				}
				return value ? 1 : -1;
			}

			VarPredicate _CallChanges() const
			{
				bool all = _hasLea;
				return [all](char kind, uint16_t, bool)
				{
					return all || (kind == 'g') || (kind == 'l') || (kind == 'P');
				};
			}

			static ExprPtr _Pop(State &state)
			{
				if (state.stack.empty())
				{
					throw Unreadable{ "stack-underflow" };
				}
				ExprPtr value = state.stack.back();
				state.stack.pop_back();
				return value;
			}

			static const ExprPtr &_Top(const State &state, int fromTop)
			{
				if ((int)state.stack.size() <= fromTop)
				{
					throw Unreadable{ "stack-underflow" };
				}
				return state.stack[state.stack.size() - 1 - fromTop];
			}

			static void _SetAcc(State &state, ExprPtr value)
			{
				state.acc = std::move(value);
				state.accTruth = 0;
			}

			// A variable or a property as a load reads it. truth (when it is
			// not null) gets the truth of the value of its binding.
			static ExprPtr _Load(const State &state, char kind, uint16_t index, ExprPtr indexExpr = nullptr, int *truth = nullptr)
			{
				ExprPtr var = Var(kind, index, indexExpr);
				if (truth)
				{
					*truth = 0;
				}
				if (!indexExpr)
				{
					auto it = state.bindings.find(var->text);
					if (it != state.bindings.end())
					{
						if (truth)
						{
							*truth = it->second.truth;
						}
						return it->second.value;
					}
				}
				return var;
			}

			// A load into the accumulator: the value and its truth.
			static void _LoadToAcc(State &state, char kind, uint16_t index, ExprPtr indexExpr = nullptr)
			{
				int truth;
				ExprPtr value = _Load(state, kind, index, indexExpr, &truth);
				_SetAcc(state, value);
				state.accTruth = truth;
			}

			// The values of a call: the words that it takes from the stack
			// and the text of the call.
			std::string _CallFrame(const State &state, const Instruction &inst, int &words) const
			{
				std::string target;
				bool frames = false;
				switch (inst.op)
				{
				case Opcode::SEND:
					words = inst.operands[0] / 2;
					target = (state.acc->text == "self") ? std::string("self") : ("send " + state.acc->text);
					frames = true;
					break;
				case Opcode::SELF:
					words = inst.operands[0] / 2;
					target = "self";
					frames = true;
					break;
				case Opcode::SUPER:
					words = inst.operands[1] / 2;
					target = fmt::format("super {0}", inst.operands[0]);
					frames = true;
					break;
				case Opcode::CALL:
					words = inst.operands[1] / 2 + 1;
					target = "call " + inst.text;
					break;
				case Opcode::CALLK:
					words = inst.operands[1] / 2 + 1;
					target = fmt::format("callk {0}", inst.operands[0]);
					break;
				case Opcode::CALLB:
					words = inst.operands[1] / 2 + 1;
					target = fmt::format("callb {0}", inst.operands[0]);
					break;
				default:
					// calle: a calle of the script's own export is a call of
					// the procedure (Instruction::text).
					words = inst.operands[2] / 2 + 1;
					target = inst.text.empty() ? fmt::format("calle {0} {1}", inst.operands[0], inst.operands[1]) : ("call " + inst.text);
					break;
				}
				if ((int)state.stack.size() < words)
				{
					throw Unreadable{ "stack-underflow" };
				}
				std::vector<ExprPtr> values(state.stack.end() - words, state.stack.end());
				std::string text = target + ":";
				if (frames)
				{
					size_t i = 0;
					while (i < values.size())
					{
						uint16_t argc;
						if ((i + 1 >= values.size()) || !IsConst(values[i + 1], argc) || (i + 2 + argc > values.size()))
						{
							throw Unreadable{ "send-frame" };
						}
						text += " " + values[i]->text + "(";
						for (size_t a = 0; a < argc; a++)
						{
							text += ((a > 0) ? " " : "") + values[i + 2 + a]->text;
						}
						i += 2 + argc;
						if ((i == values.size()) && state.rest)
						{
							text += ((argc > 0) ? " " : "") + state.rest->text;
						}
						text += ")";
					}
				}
				else
				{
					text += " (";
					for (size_t a = 0; a < values.size(); a++)
					{
						text += ((a > 0) ? " " : "") + values[a]->text;
					}
					if (state.rest)
					{
						text += " " + state.rest->text;
					}
					text += ")";
				}
				return text;
			}

			// The variable and the value of a store (the instruction is a
			// store); the state is the state before it. Pops what the store
			// takes from the stack when pop is set.
			void _Store(State &state, const Instruction &inst, ExprPtr &target, ExprPtr &value, bool &toStack, bool &toAcc) const
			{
				Opcode op = inst.op;
				toStack = false;
				toAcc = false;
				switch (op)
				{
				case Opcode::ATOP:
					target = Var('P', inst.operands[0]);
					value = state.acc;
					toAcc = true;
					return;
				case Opcode::STOP:
					target = Var('P', inst.operands[0]);
					value = _Pop(state);
					return;
				case Opcode::IPTOA:
				case Opcode::DPTOA:
				case Opcode::IPTOS:
				case Opcode::DPTOS:
					target = Var('P', inst.operands[0]);
					value = Make(((op == Opcode::IPTOA) || (op == Opcode::IPTOS)) ? "add" : "sub", { _Load(state, 'P', inst.operands[0]), Const(1) });
					toAcc = (op == Opcode::IPTOA) || (op == Opcode::DPTOA);
					toStack = !toAcc;
					return;
				default:
					break;
				}
				uint8_t bits = (uint8_t)op;
				char kind = _VarKind(bits);
				bool stack = (bits & 4) != 0;
				bool indexed = (bits & 8) != 0;
				int operation = (bits >> 4) & 3;
				ExprPtr indexExpr = indexed ? state.acc : nullptr;
				target = Var(kind, inst.operands[0], indexExpr);
				if (operation == 1)
				{
					// sa*: the accumulator (indexed: the stack, and the value
					// goes to the accumulator); ss*: the stack.
					value = (stack || indexed) ? _Pop(state) : state.acc;
					toAcc = !stack;
				}
				else
				{
					value = Make((operation == 2) ? "add" : "sub", { _Load(state, kind, inst.operands[0], indexExpr), Const(1) });
					toAcc = !stack;
					toStack = stack;
				}
			}

			void _ApplyStore(State &state, const Instruction &inst, int pair) const
			{
				ExprPtr target;
				ExprPtr value;
				bool toStack;
				bool toAcc;
				_Store(state, inst, target, value, toStack, toAcc);
				char kind = target->varKind;
				uint16_t index = target->varIndex;
				bool indexed = target->indexed;
				// A plain variable can be an element of an indexed one, and an
				// indexed store can change each variable of its kind.
				VarPredicate changed = [kind, index, indexed](char k, uint16_t i, bool idx)
				{
					return (k == kind) && (indexed || idx || (i == index));
				};
				// A store of the accumulator leaves its value (and its truth).
				bool sameValue = (value == state.acc);
				int truthBefore = sameValue ? state.accTruth : 0;
				state.Freeze(changed, pair);
				value = Freeze(value, changed, pair);
				if (!indexed)
				{
					state.bindings[Var(kind, index)->text] = Binding{ value, truthBefore };
				}
				if (toAcc)
				{
					int truth = state.accTruth;
					_SetAcc(state, value);
					state.accTruth = sameValue ? truth : 0;
				}
				if (toStack)
				{
					state.stack.push_back(value);
				}
			}

			// The effect at the instruction of the state; false for an
			// instruction with no effect.
			bool _Describe(const State &state, Effect &effect) const
			{
				const Instruction &inst = _function.code[state.pc];
				switch (inst.op)
				{
				case Opcode::SEND:
				case Opcode::SELF:
				case Opcode::SUPER:
				case Opcode::CALL:
				case Opcode::CALLK:
				case Opcode::CALLB:
				case Opcode::CALLE:
				{
					if (_IsEmptySend(state, inst))
					{
						return false;
					}
					int words;
					effect.kind = EffectKind::Call;
					effect.text = _CallFrame(state, inst, words);
					return true;
				}
				case Opcode::RET:
					effect.kind = EffectKind::Return;
					effect.text = _returnsValue ? ("ret " + state.acc->text) : std::string("ret");
					return true;
				case Opcode::BT:
				case Opcode::BNT:
				{
					uint16_t value;
					if ((state.accTruth != 0) || IsConst(state.acc, value) || (inst.target == state.pc + 1))
					{
						// A known outcome, or a branch to the next instruction.
						return false;
					}
					// The test of a not is the test of its operand with the
					// outcomes swapped.
					ExprPtr test = state.acc;
					bool swapped = false;
					while ((test->head == "not") && (test->args.size() == 1))
					{
						test = test->args[0];
						swapped = !swapped;
					}
					int taken = inst.target;
					int next = state.pc + 1;
					bool takenOnTrue = (inst.op == Opcode::BT);
					effect.kind = EffectKind::Test;
					effect.test = test;
					effect.onTrue = (takenOnTrue != swapped) ? taken : next;
					effect.onFalse = (takenOnTrue != swapped) ? next : taken;
					effect.text = "test " + test->text;
					return true;
				}
				default:
					break;
				}
				if (_IsStore(inst.op))
				{
					State copy = state;
					ExprPtr target;
					ExprPtr value;
					bool toStack;
					bool toAcc;
					_Store(copy, inst, target, value, toStack, toAcc);
					effect.kind = EffectKind::Store;
					effect.text = "store " + target->text + " = " + value->text;
					return true;
				}
				return false;
			}

			static bool _IsStore(Opcode op)
			{
				switch (op)
				{
				case Opcode::ATOP:
				case Opcode::STOP:
				case Opcode::IPTOA:
				case Opcode::DPTOA:
				case Opcode::IPTOS:
				case Opcode::DPTOS:
					return true;
				default:
					break;
				}
				if ((op >= Opcode::FirstLoadStore) && (op <= Opcode::LastLoadStore))
				{
					return (((uint8_t)op >> 4) & 3) != 0;
				}
				return false;
			}

			// One instruction with no effect.
			void _Step(State &state) const
			{
				const Instruction &inst = _function.code[state.pc];
				Opcode op = inst.op;
				int next = state.pc + 1;
				switch (op)
				{
				case Opcode::BNOT:
					_SetAcc(state, Make("bnot", { state.acc }));
					break;
				case Opcode::NOT:
				{
					int truth = state.accTruth;
					_SetAcc(state, Make("not", { state.acc }));
					state.accTruth = -truth;
					break;
				}
				case Opcode::NEG:
					_SetAcc(state, Make("neg", { state.acc }));
					break;
				case Opcode::ADD:
				case Opcode::SUB:
				case Opcode::MUL:
				case Opcode::DIV:
				case Opcode::MOD:
				case Opcode::SHR:
				case Opcode::SHL:
				case Opcode::XOR:
				case Opcode::AND:
				case Opcode::OR:
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
				{
					ExprPtr left = _Pop(state);
					ExprPtr right = state.acc;
					if (CodeIsCompare(op) && _hasPprev)
					{
						state.prev = right;
					}
					_SetAcc(state, Make(OpcodeToName(op, 0), { left, right }));
					break;
				}
				case Opcode::BT:
				case Opcode::BNT:
				{
					// A test whose outcome is known: the truth of a test before
					// it on this path, or a constant.
					uint16_t value;
					bool truth = (state.accTruth != 0) ? (state.accTruth > 0) : (IsConst(state.acc, value) && (value != 0));
					if ((op == Opcode::BT) == truth)
					{
						next = inst.target;
					}
					break;
				}
				case Opcode::JMP:
					next = inst.target;
					break;
				case Opcode::LDI:
					_SetAcc(state, Const(inst.operands[0]));
					break;
				case Opcode::PUSH:
					state.stack.push_back(state.acc);
					break;
				case Opcode::PUSHI:
					state.stack.push_back(Const(inst.operands[0]));
					break;
				case Opcode::PUSH0:
				case Opcode::PUSH1:
				case Opcode::PUSH2:
					state.stack.push_back(Const((uint16_t)((int)op - (int)Opcode::PUSH0)));
					break;
				case Opcode::TOSS:
					_Pop(state);
					break;
				case Opcode::DUP:
					state.stack.push_back(_Top(state, 0));
					break;
				case Opcode::LINK:
				case Opcode::LineNumber:
				case Opcode::Filename:
				// A send with no message (_IsEmptySend).
				case Opcode::SEND:
				case Opcode::SELF:
				case Opcode::SUPER:
					break;
				case Opcode::CLASS:
					_SetAcc(state, Make(fmt::format("class {0}", inst.operands[0])));
					break;
				case Opcode::REST:
					state.rest = Make(fmt::format("&rest {0}", inst.operands[0]));
					break;
				case Opcode::LEA:
				{
					// Bits 1-2: the kind of the variable (a local of script 0
					// is a global).
					uint16_t type = inst.operands[0];
					if (_function.localsAreGlobals && (((type >> 1) & 3) == 1))
					{
						type &= ~0x2;
					}
					bool accIndex = ((type >> 1) & LEA_ACC_AS_INDEX_MOD) != 0;
					std::vector<ExprPtr> args;
					if (accIndex)
					{
						args.push_back(state.acc);
					}
					_SetAcc(state, Make(fmt::format("lea {0} {1}", type, inst.operands[1]), args));
					break;
				}
				case Opcode::SELFID:
					_SetAcc(state, Make("self"));
					break;
				case Opcode::PUSHSELF:
					state.stack.push_back(Make("self"));
					break;
				case Opcode::PPREV:
					state.stack.push_back(state.prev ? state.prev : Make("prev0"));
					break;
				case Opcode::PTOA:
					_LoadToAcc(state, 'P', inst.operands[0]);
					break;
				case Opcode::PTOS:
					state.stack.push_back(_Load(state, 'P', inst.operands[0]));
					break;
				case Opcode::LOFSA:
				case Opcode::LOFSS:
				{
					if (inst.text.empty())
					{
						throw Unreadable{ "address" };
					}
					ExprPtr value = Make(inst.text);
					if (op == Opcode::LOFSA)
					{
						_SetAcc(state, value);
					}
					else
					{
						state.stack.push_back(value);
					}
					break;
				}
				default:
					if ((op >= Opcode::FirstLoadStore) && (op <= Opcode::LastLoadStore) && ((((uint8_t)op >> 4) & 3) == 0))
					{
						uint8_t bits = (uint8_t)op;
						ExprPtr index = (bits & 8) ? state.acc : nullptr;
						if (bits & 4)
						{
							state.stack.push_back(_Load(state, _VarKind(bits), inst.operands[0], index));
						}
						else
						{
							_LoadToAcc(state, _VarKind(bits), inst.operands[0], index);
						}
						break;
					}
					throw Unreadable{ fmt::format("opcode {0}", (int)op) };
				}
				_ArriveAt(state, next);
			}

			// The kind of the variable of a load or store opcode. The locals
			// of script 0 are the globals.
			char _VarKind(uint8_t bits) const
			{
				char kind = VarKinds[bits & 3];
				return ((kind == 'l') && _function.localsAreGlobals) ? 'g' : kind;
			}

			// A send with no message sends nothing, and leaves the
			// accumulator.
			static bool _IsEmptySend(const State &state, const Instruction &inst)
			{
				int bytes = (inst.op == Opcode::SUPER) ? inst.operands[1] : inst.operands[0];
				return ((inst.op == Opcode::SEND) || (inst.op == Opcode::SELF) || (inst.op == Opcode::SUPER)) && (bytes == 0) && !state.rest;
			}

			// An instruction reads the accumulator (reads), and puts a value
			// in it (writes).
			void _AccUse(const Instruction &inst, bool &reads, bool &writes) const
			{
				Opcode op = inst.op;
				reads = false;
				writes = false;
				if (op <= Opcode::ULE)
				{
					// The operations and the compares.
					reads = true;
					writes = true;
					return;
				}
				switch (op)
				{
				case Opcode::BT:
				case Opcode::BNT:
				case Opcode::PUSH:
				case Opcode::ATOP:
					reads = true;
					return;
				case Opcode::RET:
					reads = _returnsValue;
					return;
				case Opcode::SEND:
					// A send with no message (_IsEmptySend) leaves the
					// accumulator.
					reads = true;
					writes = (inst.operands[0] != 0);
					return;
				case Opcode::SELF:
					writes = (inst.operands[0] != 0);
					return;
				case Opcode::SUPER:
					writes = (inst.operands[1] != 0);
					return;
				case Opcode::LEA:
					reads = ((inst.operands[0] >> 1) & LEA_ACC_AS_INDEX_MOD) != 0;
					writes = true;
					return;
				case Opcode::LDI:
				case Opcode::CLASS:
				case Opcode::SELFID:
				case Opcode::LOFSA:
				case Opcode::PTOA:
				case Opcode::IPTOA:
				case Opcode::DPTOA:
				case Opcode::CALL:
				case Opcode::CALLK:
				case Opcode::CALLB:
				case Opcode::CALLE:
					writes = true;
					return;
				default:
					break;
				}
				if ((op >= Opcode::FirstLoadStore) && (op <= Opcode::LastLoadStore))
				{
					uint8_t bits = (uint8_t)op;
					bool stack = (bits & 4) != 0;
					bool indexed = (bits & 8) != 0;
					int operation = (bits >> 4) & 3;
					// An indexed variable: the index is in the accumulator. A
					// store: sa* reads the value; sa*i puts the value from the
					// stack in the accumulator. A load or an increment to the
					// accumulator puts its value there.
					reads = indexed || ((operation == 1) && !stack);
					writes = !stack && ((operation != 1) || indexed);
				}
			}

			// _accLive: the value in the accumulator before the instruction
			// can be read before another value replaces it.
			void _FindAccLiveness()
			{
				int size = (int)_function.code.size();
				std::vector<bool> reads(size);
				std::vector<bool> writes(size);
				for (int i = 0; i < size; i++)
				{
					bool r;
					bool w;
					_AccUse(_function.code[i], r, w);
					reads[i] = r;
					writes[i] = w;
				}
				_accLive.assign(size + 1, false);
				for (bool changed = true; changed; )
				{
					changed = false;
					for (int i = size - 1; i >= 0; i--)
					{
						const Instruction &inst = _function.code[i];
						bool after = false;
						if ((inst.op != Opcode::JMP) && (inst.op != Opcode::RET))
						{
							after = _accLive[i + 1];
						}
						if (inst.target >= 0)
						{
							after = after || _accLive[inst.target];
						}
						bool live = reads[i] || (!writes[i] && after);
						if (live != _accLive[i])
						{
							_accLive[i] = live;
							changed = true;
						}
					}
				}
			}

			static bool CodeIsCompare(Opcode op)
			{
				return (op >= Opcode::EQ) && (op <= Opcode::ULE);
			}

			const Function &_function;
			bool _returnsValue;

			bool _hasLea = false;
			bool _hasPprev = false;
			std::vector<bool> _loopHead;
			std::vector<bool> _accLive;
		};

		std::string Place(const Walker &walker, const State &state)
		{
			int offset = walker.Offset(state);
			return (offset >= 0) ? fmt::format("{0:04x}", offset) : std::string("end");
		}
	}

	Outcome Compare(const Function &original, const Function &recompiled)
	{
		Outcome outcome;
		if (!original.unreadable.empty() || !recompiled.unreadable.empty())
		{
			outcome.detail = !original.unreadable.empty() ? ("original: " + original.unreadable) : ("recompiled: " + recompiled.unreadable);
			return outcome;
		}
		Walker walkerA(original, original.returnsValue);
		Walker walkerB(recompiled, original.returnsValue);
		std::map<std::pair<int, int>, int> pairs;
		// The pairs of states that the walk has had, as hashes (64-bit
		// FNV-1a) of their texts: the texts can be long.
		std::unordered_set<uint64_t> seen;
		auto hash = [](const std::string &text)
		{
			uint64_t value = 14695981039346656037ull;
			for (unsigned char ch : text)
			{
				value = (value ^ ch) * 1099511628211ull;
			}
			return value;
		};
		std::vector<std::pair<State, State>> work;
		const char *side = "original";
		// The first form that the check does not read, on a path that then
		// stops. The other paths go on: a DIFF on one of them is a DIFF.
		std::string unreadable;
		work.emplace_back(walkerA.Start(), walkerB.Start());
		int count = 0;
		while (!work.empty())
		{
			std::pair<State, State> item = std::move(work.back());
			work.pop_back();
			if (!seen.insert(hash(walkerA.KeyOf(item.first) + "#" + walkerB.KeyOf(item.second))).second)
			{
				continue;
			}
			if (++count > MaxEffectPairs)
			{
				outcome.detail = "too-many-paths";
				return outcome;
			}
			State &a = item.first;
			State &b = item.second;
			try
			{
				State startA = a;
				State startB = b;
				side = "original";
				Effect effectA = walkerA.RunToEffect(a);
				side = "recompiled";
				Effect effectB = walkerB.RunToEffect(b);
				if (((effectA.kind != effectB.kind) || (effectA.text != effectB.text)) && ((effectA.kind == EffectKind::Test) || (effectB.kind == EffectKind::Test)))
				{
					// Look past more tests on both sides.
					a = startA;
					b = startB;
					side = "original";
					effectA = walkerA.RunToEffect(a, 0, MaxDeepTestLookahead);
					side = "recompiled";
					effectB = walkerB.RunToEffect(b, 0, MaxDeepTestLookahead);
				}
				if ((effectA.kind != effectB.kind) || (effectA.text != effectB.text))
				{
					outcome.verdict = Verdict::Diff;
					outcome.detail = fmt::format("at {0}/{1}: {2} | {3}", Place(walkerA, a), Place(walkerB, b), effectA.text, effectB.text);
					return outcome;
				}
				switch (effectA.kind)
				{
				case EffectKind::Test:
					work.emplace_back(walkerA.Branch(a, effectA, false), walkerB.Branch(b, effectB, false));
					work.emplace_back(walkerA.Branch(a, effectA, true), walkerB.Branch(b, effectB, true));
					break;
				case EffectKind::Call:
				case EffectKind::Store:
				{
					auto key = std::make_pair(a.pc, b.pc);
					auto found = pairs.find(key);
					int pair = (found != pairs.end()) ? found->second : (pairs[key] = (int)pairs.size());
					side = "original";
					State nextA = walkerA.Apply(a, effectA, pair);
					side = "recompiled";
					State nextB = walkerB.Apply(b, effectB, pair);
					work.emplace_back(std::move(nextA), std::move(nextB));
					break;
				}
				default:
					break;
				}
			}
			catch (const Unreadable &e)
			{
				if (unreadable.empty())
				{
					unreadable = std::string(side) + ": " + e.reason;
				}
			}
		}
		outcome.verdict = unreadable.empty() ? Verdict::Same : Verdict::Uncompared;
		outcome.detail = unreadable;
		return outcome;
	}

	Function MakeFunction(const std::string &key, const std::string &display, std::list<scii> &code, bool returnsValue, const SCIVersion &version,
		const std::function<std::string(uint16_t)> &addressText, const std::function<std::string(uint16_t)> &procedureKey,
		const std::function<std::string(uint16_t, uint16_t)> &calleKey)
	{
		Function function;
		function.key = key;
		function.display = display;
		function.returnsValue = returnsValue;
		std::unordered_map<const scii *, int> indexOf;
		for (scii &inst : code)
		{
			if (inst.get_opcode() != Opcode::INDETERMINATE)
			{
				indexOf[&inst] = (int)indexOf.size();
			}
		}
		for (scii &inst : code)
		{
			Opcode op = inst.get_opcode();
			if (op == Opcode::INDETERMINATE)
			{
				continue;
			}
			Instruction out;
			out.op = op;
			for (int i = 0; i < 3; i++)
			{
				out.operands[i] = inst.get_operand(i);
			}
			out.offset = inst.get_final_offset_dontcare();
			if ((op == Opcode::BT) || (op == Opcode::BNT) || (op == Opcode::JMP))
			{
				auto target = indexOf.find(&*inst.get_branch_target());
				if (target == indexOf.end())
				{
					function.unreadable = fmt::format("branch at {0:04x}", out.offset);
				}
				else
				{
					out.target = target->second;
				}
			}
			else if ((op == Opcode::LOFSA) || (op == Opcode::LOFSS))
			{
				uint16_t address = version.lofsaOpcodeIsAbsolute ? inst.get_first_operand() : (uint16_t)(inst.get_first_operand() + inst.get_final_postop_offset());
				out.text = addressText(address);
			}
			else if (op == Opcode::CALL)
			{
				out.text = procedureKey((uint16_t)(inst.get_final_postop_offset() + inst.get_first_operand()));
			}
			else if ((op == Opcode::CALLE) && calleKey)
			{
				out.text = calleKey(out.operands[0], out.operands[1]);
			}
			function.code.push_back(out);
		}
		try
		{
			scope::CodeModel model(code);
			if (model.Size() == (int)function.code.size())
			{
				for (int i = 0; i < model.Size(); i++)
				{
					function.depth.push_back(model.DepthBefore(i));
				}
			}
		}
		catch (const scope::ScopeError &)
		{
			// No depths: the stack keeps each value at a loop head.
		}
		return function;
	}

	namespace
	{
		const char LocalPrefix[] = "local ";

		// The code of an empty procedure: a ret, with no other instruction
		// than a link or a line number.
		bool _IsEmptyProcedure(const Function &function)
		{
			bool ret = false;
			for (const Instruction &inst : function.code)
			{
				if (inst.op == Opcode::RET)
				{
					ret = true;
				}
				else if ((inst.op != Opcode::LINK) && (inst.op != Opcode::LineNumber) && (inst.op != Opcode::Filename))
				{
					return false;
				}
			}
			return ret;
		}

		bool _IsLocalKey(const std::string &key)
		{
			return key.compare(0, sizeof(LocalPrefix) - 1, LocalPrefix) == 0;
		}

		// The function with the targets of its calls of local procedures
		// hidden.
		Function _WithoutLocalTargets(const Function &function)
		{
			Function copy = function;
			for (Instruction &inst : copy.code)
			{
				if (((inst.op == Opcode::CALL) || (inst.op == Opcode::CALLE)) && _IsLocalKey(inst.text))
				{
					inst.text = "local ?";
				}
			}
			return copy;
		}

		// The recompiled functions, with the keys of the original local
		// procedures. The text can have its local procedures in another
		// order than the original (the decompiler prints a procedure that
		// reads properties inside its class), and the keys "local <n>"
		// count them in address order. Each original local procedure pairs
		// with a recompiled one that means the same when the targets of the
		// calls of local procedures are hidden: the one at its own place
		// first, else the first other one; then the others pair with one that
		// the compare cannot read (UNCOMPARED), and those that are left pair in
		// their order.
		std::vector<Function> _PairLocalProcedures(const std::vector<Function> &original, const std::vector<Function> &recompiled)
		{
			std::vector<const Function *> originalLocals;
			std::vector<size_t> recompiledLocals;
			for (const Function &function : original)
			{
				if (_IsLocalKey(function.key))
				{
					originalLocals.push_back(&function);
				}
			}
			for (size_t i = 0; i < recompiled.size(); i++)
			{
				if (_IsLocalKey(recompiled[i].key))
				{
					recompiledLocals.push_back(i);
				}
			}
			std::vector<Function> result = recompiled;
			if (originalLocals.empty() || recompiledLocals.empty())
			{
				return result;
			}
			std::vector<Function> hiddenRecompiled;
			for (size_t index : recompiledLocals)
			{
				hiddenRecompiled.push_back(_WithoutLocalTargets(recompiled[index]));
			}
			std::vector<bool> taken(recompiledLocals.size(), false);
			std::vector<bool> paired(originalLocals.size(), false);
			std::map<std::string, std::string> newKeys;
			// First the partners that mean the same, then those that the compare
			// cannot read (UNCOMPARED).
			for (int pass = 0; pass < 2; pass++)
			{
				for (size_t o = 0; o < originalLocals.size(); o++)
				{
					if (paired[o] || !originalLocals[o]->unreadable.empty())
					{
						continue;
					}
					Function hidden = _WithoutLocalTargets(*originalLocals[o]);
					auto same = [&](size_t r)
					{
						if (taken[r])
						{
							return false;
						}
						Verdict verdict = Compare(hidden, hiddenRecompiled[r]).verdict;
						return (verdict == Verdict::Same) || ((pass == 1) && (verdict == Verdict::Uncompared));
					};
					size_t partner = recompiledLocals.size();
					if ((o < recompiledLocals.size()) && same(o))
					{
						partner = o;
					}
					else
					{
						for (size_t r = 0; r < recompiledLocals.size(); r++)
						{
							if (same(r))
							{
								partner = r;
								break;
							}
						}
					}
					if (partner < recompiledLocals.size())
					{
						taken[partner] = true;
						paired[o] = true;
						newKeys[recompiled[recompiledLocals[partner]].key] = originalLocals[o]->key;
					}
				}
			}
			// The procedures with no such partner pair in their order (the
			// compare then shows where they differ); a recompiled procedure
			// that is left gets a key that no original procedure has.
			size_t next = 0;
			for (size_t r = 0; r < recompiledLocals.size(); r++)
			{
				if (taken[r])
				{
					continue;
				}
				while ((next < originalLocals.size()) && paired[next])
				{
					next++;
				}
				const std::string &key = recompiled[recompiledLocals[r]].key;
				if (next < originalLocals.size())
				{
					paired[next] = true;
					newKeys[key] = originalLocals[next]->key;
				}
				else
				{
					newKeys[key] = key + " (recompiled)";
				}
			}
			for (Function &function : result)
			{
				auto renamed = newKeys.find(function.key);
				if (renamed != newKeys.end())
				{
					function.key = renamed->second;
				}
				for (Instruction &inst : function.code)
				{
					if ((inst.op == Opcode::CALL) || (inst.op == Opcode::CALLE))
					{
						auto target = newKeys.find(inst.text);
						if (target != newKeys.end())
						{
							inst.text = target->second;
						}
					}
				}
			}
			return result;
		}
	}

	std::vector<FunctionOutcome> CompareFunctions(const std::vector<Function> &original, const std::vector<Function> &recompiledAsRead)
	{
		std::vector<Function> recompiled = _PairLocalProcedures(original, recompiledAsRead);
		std::map<std::string, const Function *> byKey;
		for (const Function &function : recompiled)
		{
			byKey[function.key] = &function;
		}
		std::set<std::string> originalKeys;
		std::vector<FunctionOutcome> outcomes;
		for (const Function &function : original)
		{
			originalKeys.insert(function.key);
			FunctionOutcome row;
			row.key = function.key;
			row.display = function.display;
			row.offset = function.offset;
			auto partner = byKey.find(function.key);
			if (function.badExport && (partner != byKey.end()) && _IsEmptyProcedure(*partner->second))
			{
				// No code: the text has an empty procedure for the export.
				row.outcome.verdict = Verdict::Uncompared;
				row.outcome.detail = "bad-export";
			}
			else if (function.badExport && (partner != byKey.end()))
			{
				row.outcome.verdict = Verdict::Diff;
				row.outcome.detail = "bad-export-body";
			}
			else if (partner == byKey.end())
			{
				// The text lost the function (or gave it to another object).
				row.outcome.verdict = Verdict::Diff;
				row.outcome.detail = "no-recompiled-function";
			}
			else
			{
				row.outcome = Compare(function, *partner->second);
			}
			outcomes.push_back(row);
		}
		for (const Function &function : recompiled)
		{
			if (originalKeys.count(function.key) == 0)
			{
				// A function that the text adds (a method overrides one of a
				// class).
				FunctionOutcome row;
				row.key = function.key;
				row.display = function.display;
				row.offset = function.offset;
				row.outcome.verdict = Verdict::Diff;
				row.outcome.detail = "no-original-function";
				outcomes.push_back(row);
			}
		}
		return outcomes;
	}

	std::vector<Function> ReadFunctions(const CompiledScript &script, DecompileLookups &lookups, const Vocab000 *pWords)
	{
		std::vector<FunctionCode> codes = ReadScriptFunctions(script, lookups, pWords);
		// The key of the procedure at each address (for a call): exports
		// that share an address have the key of the lowest export.
		std::map<uint16_t, std::string> procedureKeys;
		std::map<uint16_t, std::string> procedureNames;
		int locals = 0;
		for (const FunctionCode &code : codes)
		{
			if (!code.method && (code.exportIndex >= 0))
			{
				auto known = procedureKeys.find(code.offset);
				if (known == procedureKeys.end())
				{
					procedureKeys[code.offset] = fmt::format("export {0}", code.exportIndex);
					procedureNames[code.offset] = fmt::format("proc{0}_{1}", script.GetScriptNumber(), code.exportIndex);
				}
			}
		}
		for (const FunctionCode &code : codes)
		{
			if (!code.method && (code.exportIndex < 0) && (procedureKeys.count(code.offset) == 0))
			{
				procedureKeys[code.offset] = fmt::format("local {0}", locals++);
				procedureNames[code.offset] = fmt::format("localproc_{0:04x}", code.offset);
			}
		}
		auto procedureKey = [&](uint16_t address) -> std::string
		{
			auto it = procedureKeys.find(address);
			return (it != procedureKeys.end()) ? it->second : fmt::format("code {0:04x}", address);
		};
		std::vector<uint16_t> exports = script.GetExports();
		auto calleKey = [&](uint16_t scriptNumber, uint16_t exportIndex) -> std::string
		{
			if ((scriptNumber != script.GetScriptNumber()) || (exportIndex >= exports.size()))
			{
				return std::string();
			}
			// An export with no procedure (an object) keeps the calle form.
			auto it = procedureKeys.find(exports[exportIndex]);
			return (it != procedureKeys.end()) ? it->second : std::string();
		};
		auto addressText = [&](uint16_t address) -> std::string
		{
			ICompiledScriptSpecificLookups::ObjectType type;
			std::string name;
			if (!script.LookupObjectName(address, type, name))
			{
				return std::string();
			}
			if (type == ICompiledScriptSpecificLookups::ObjectTypeClass)
			{
				// A class of the script is the value of a class instruction.
				for (const auto &object : script.GetObjects())
				{
					if ((object->GetName() == name) && !object->IsInstance())
					{
						return fmt::format("class {0}", object->GetSpecies());
					}
				}
				return "object " + name;
			}
			return std::string((type == ICompiledScriptSpecificLookups::ObjectTypeString) ? "string " : "said ") + name;
		};
		std::vector<Function> functions;
		std::map<std::string, int> keyCounts;
		for (FunctionCode &code : codes)
		{
			std::string key;
			std::string display;
			if (code.method)
			{
				display = code.objectName + "::" + lookups.LookupSelectorName(code.selector);
				key = display;
			}
			else if (code.exportIndex >= 0)
			{
				// Each export by its own index (exports can share an
				// address).
				key = fmt::format("export {0}", code.exportIndex);
				display = fmt::format("proc{0}_{1}", script.GetScriptNumber(), code.exportIndex);
			}
			else
			{
				key = procedureKeys[code.offset];
				display = procedureNames[code.offset];
			}
			// Two functions with one key (an object with a method twice).
			if (keyCounts[key]++ > 0)
			{
				key += fmt::format(" #{0}", keyCounts[key]);
			}
			Function function = MakeFunction(key, display, code.code, code.returnsValue, lookups.GetVersion(), addressText, procedureKey, calleKey);
			function.offset = code.offset;
			function.badExport = (code.exportIndex >= 0) && code.badAddress;
			function.localsAreGlobals = (script.GetScriptNumber() == 0);
			if (!code.read)
			{
				function.unreadable = "code-bounds";
			}
			functions.push_back(std::move(function));
		}
		return functions;
	}

	namespace
	{
		// The decompiler messages of a read: the check does not show them.
		class QuietResults : public IDecompilerResults
		{
		public:
			void AddResult(DecompilerResultType, const std::string &) override {}
			bool IsAborted() override { return false; }
			void InformStats(bool, int) override {}
			void InformFunction(const DecompiledFunction &) override {}
			void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &) override {}
		};
	}

	namespace
	{
		std::vector<Function> _ReadLoaded(CompiledScript &compiled, const GameFolderHelper &helper, GlobalCompiledScriptLookups &lookups, const Vocab000 *pWords, uint16_t scriptNumber)
		{
			FixDuplicateObjectNames(compiled, lookups.GetSelectorTable());
			QuietResults results;
			ObjectFileScriptLookups objectFileLookups(helper, lookups.GetSelectorTable());
			DecompileLookups decompileLookups(nullptr, helper, scriptNumber, &lookups, &objectFileLookups, &compiled, nullptr, &compiled, results);
			return ReadFunctions(compiled, decompileLookups, pWords);
		}
	}

	sci::Result<std::vector<Function>> ReadScript(const GameFolderHelper &helper, GlobalCompiledScriptLookups &lookups, const Vocab000 *pWords, uint16_t scriptNumber)
	{
		CompiledScript compiled(scriptNumber, CompiledScriptFlags::RemoveBadExports);
		SCI_TRY(compiled.TryLoad(helper, helper.Version, scriptNumber));
		return _ReadLoaded(compiled, helper, lookups, pWords, scriptNumber);
	}

	sci::Result<std::vector<Function>> ReadScriptData(const GameFolderHelper &helper, GlobalCompiledScriptLookups &lookups, const Vocab000 *pWords, uint16_t scriptNumber,
		const std::vector<uint8_t> &script, const std::vector<uint8_t> *heap)
	{
		CompiledScript compiled(scriptNumber, CompiledScriptFlags::RemoveBadExports);
		if (script.empty() || (helper.Version.SeparateHeapResources && (!heap || heap->empty())))
		{
			return sci::Fail(sci::ErrorCode::NotFound, "the script or its heap is missing");
		}
		sci::istream scriptStream(script.data(), (uint32_t)script.size());
		std::unique_ptr<sci::istream> heapStream;
		if (helper.Version.SeparateHeapResources)
		{
			heapStream = std::make_unique<sci::istream>(heap->data(), (uint32_t)heap->size());
		}
		bool loaded = false;
		SCI_TRY(sci::Guard("the script could not be read", [&]() -> sci::Status
		{
			loaded = compiled.Load(helper, helper.Version, scriptNumber, scriptStream, heapStream.get());
			return sci::Ok();
		}));
		if (!loaded)
		{
			return sci::Fail(sci::ErrorCode::Format, "the script could not be read");
		}
		return _ReadLoaded(compiled, helper, lookups, pWords, scriptNumber);
	}
}
