#pragma once

#include <iomanip>
#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include "DecompilerCore.h"
#include "PMachine.h"

// The chunk tree of a function: each instruction has the instructions that
// give its operands as children, in address order; the control structures
// are nodes with no instruction. The syntax stage (_CodeNodeToSyntaxNode in
// DecompilerNew.cpp) reads it.
enum class ChunkType
{
	None,
	If,
	Then,
	Else,
	Condition,
	Do,
	While,
	LoopBody,
	And,
	Or,
	First,
	Second,
	Invert,
	Switch,
	Case,
	DefaultCase,
	CaseCondition,
	CaseBody,
	SwitchValue,
	Break,
	Continue,
	NeedsAccumulator,
	NeedsAccumulatorSpecial,
	FailedToGetAccumulator,
	FailedToGetStack,
	NeedsStack,
	ZeroNode,
	TrueNode,
	ShortCircuitInstruction,
	FunctionBody,
	CaseDeleted,
	Nary,
	For,		// Condition, LoopBody, Step
	Step,		// the statements of the step of a for loop
};

// The names of the chunk types, for the debug dumps.
extern const char *chunkTypeNames[];

std::string _indent2(int iIndent);

struct ConsumptionNode;
Consumption _GetInstructionConsumption(ConsumptionNode &node, DecompileLookups &lookups);

struct ConsumptionNode
{
	ConsumptionNode() : _hasPos(false), _chunkType(ChunkType::None), _parentWeak(nullptr) {}

	bool _hasPos;
	code_pos pos;
	ChunkType _chunkType;
	ConsumptionNode *_parentWeak;
	// A copy of a value that the code has in another place (the scope
	// engine reads a value again that the optimiser did not load again).
	bool _copy = false;
	// Break, Continue: the loop, 1 for the innermost one.
	int _level = 1;

	std::unique_ptr<ConsumptionNode> Clone()
	{
		std::unique_ptr<ConsumptionNode> clone = std::make_unique<ConsumptionNode>();
		clone->_hasPos = _hasPos;
		clone->pos = pos;
		clone->_chunkType = _chunkType;
		clone->_copy = _copy;
		clone->_level = _level;
		for (auto &child : children)
		{
			clone->children.push_back(std::move(child->Clone()));
			assert(clone->children.back()->_parentWeak == nullptr);
			clone->children.back()->_parentWeak = clone.get();
		}
		assert(_parentWeak); // Don't want to be cloning top-levl eguy
		return clone;
	}

	code_pos GetCode() const
	{
		assert(_hasPos);
		return pos;
	}
	ChunkType GetType() const
	{
		return _chunkType;
	}

	int GetMyIndex() const
	{
		return _parentWeak->GetIndexOf(this);
	}

	int GetIndexOf(const ConsumptionNode *child) const
	{
		for (size_t i = 0; i < children.size(); i++)
		{
			if (children[i].get() == child)
			{
				return (int)i;
			}
		}
		assert(false && "Corrupt hierarchy");
		return 0;
	}

	ConsumptionNode *GetChild(ChunkType type) const
	{
		for (auto &child : children)
		{
			if (child->GetType() == type)
			{
				return child.get();
			}
		}
		return nullptr;
	}

	void SetPos(code_pos pos) { this->pos = pos; _hasPos = true; }
	void SetType(ChunkType type) { this->_chunkType = type; _hasPos = false; }
	void SetTypeDontClearPos(ChunkType type) { this->_chunkType = type; }

	ConsumptionNode *PrependChild()
	{
		std::unique_ptr<ConsumptionNode> newNode = std::make_unique<ConsumptionNode>();
		ConsumptionNode *returnValue = newNode.get();
		PrependChild(std::move(newNode));
		return returnValue;
	}
	void PrependChild(std::unique_ptr<ConsumptionNode> chunk)
	{
		chunk->_parentWeak = this;
		children.insert(children.begin(), std::move(chunk));
	}
	void AppendChild(std::unique_ptr<ConsumptionNode> chunk)
	{
		chunk->_parentWeak = this;
		children.push_back(std::move(chunk));
	}

	void Print(std::ostream &os, int iIndent) const
	{
		os << std::hex;
		os << _indent2(iIndent);
		if (_hasPos)
		{
			os << OpcodeToName(pos->get_opcode(), pos->get_first_operand()) << " " << pos->get_first_operand() << "  [" << std::setw(4) << std::setfill('0') << pos->get_final_offset_dontcare() << "]";
		}
		else
		{
			os << "[" << chunkTypeNames[(int)_chunkType] << "]";
		}
		if (_copy)
		{
			os << " (copy)";
		}
		os << "\n";

		for (auto &child : children)
		{
			child->Print(os, iIndent + 2);
		}
	}

	const std::vector<std::unique_ptr<ConsumptionNode>> &Children() { return children; }

	std::unique_ptr<ConsumptionNode> ReplaceChild(size_t index, std::unique_ptr<ConsumptionNode> replacement)
	{
		std::unique_ptr<ConsumptionNode> returnValue = std::move(children[index]);
		children[index] = std::move(replacement);
		children[index]->_parentWeak = this;
		returnValue->_parentWeak = nullptr;
		return returnValue;
	}

	void InsertChild(size_t index, std::unique_ptr<ConsumptionNode> replacement)
	{
		children.insert(children.begin() + index, std::move(replacement));
		children[index]->_parentWeak = this;
	}

	// If lookups is provided, then we will replace stolen nodes with NeedsStack, if necessary.
	// This is a recent change, so I've scoped it only to where I encountered this bug (switch statements, SQ4, script 376)
	std::unique_ptr<ConsumptionNode> StealChild(size_t index, DecompileLookups *lookups = nullptr)
	{
		std::unique_ptr<ConsumptionNode> stolen = std::move(children[index]);
		children.erase(children.begin() + index);
		stolen->_parentWeak = nullptr;

		// What if we needed this?
		bool replace = false;
		if (lookups)
		{
			if (_GetInstructionConsumption(*stolen, *lookups).cStackGenerate)
			{
				if (_GetInstructionConsumption(*this, *lookups).cStackConsume)
				{
					// We just lost a stack that we were relying on, so we'll need a replacement.
					// We'll insert it at the beginning. So like, stuff "shifts down".
					ConsumptionNode *replacementChild = PrependChild();
					replacementChild->SetType(ChunkType::NeedsStack);
				}
			}
		}

		return stolen;
	}
	size_t GetChildCount() { return children.size(); }
	ConsumptionNode *Child(int i) { return children[i].get(); }

private:
	// Children might be stored backward for now, we'll see
	std::vector<std::unique_ptr<ConsumptionNode>> children;
};

class ConsumptionNodeException : public std::exception
{
public:
	ConsumptionNodeException(const ConsumptionNode *node, const std::string &message) : message(message), node(node) {}
	const char *what() const noexcept override { return message.c_str(); }

	const ConsumptionNode *node;
	std::string message;
};
