#include "stdafx.h"
#include "CppUnitTest.h"
#include "ScopeAsm.h"
#include "PMachine.h"
#include "Version.h"
#include <sstream>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
	std::wstring Wide(const std::string &text)
	{
		return std::wstring(text.begin(), text.end());
	}

	uint16_t ParseNumber(const std::string &token)
	{
		if (!token.empty() && (token[0] == '$'))
		{
			return (uint16_t)std::stoul(token.substr(1), nullptr, 16);
		}
		return (uint16_t)std::stol(token);
	}
}

ScopeAsm::ScopeAsm(const std::string &text)
{
	struct Branch
	{
		code_pos pos;
		std::string label;
		int index;
	};
	std::vector<Branch> branches;
	std::istringstream lines(text);
	std::string line;
	int index = 0;
	while (std::getline(lines, line))
	{
		size_t comment = line.find(';');
		if (comment != std::string::npos)
		{
			line = line.substr(0, comment);
		}
		std::istringstream tokens(line);
		std::vector<std::string> words;
		std::string word;
		while (tokens >> word)
		{
			words.push_back(word);
		}
		if (words.empty())
		{
			continue;
		}
		if (words[0].back() == ':')
		{
			std::string label = words[0].substr(0, words[0].size() - 1);
			Assert::IsTrue(_labels.find(label) == _labels.end(), Wide("a label two times: " + label).c_str());
			_labels[label] = index;
			continue;
		}
		bool usesAccIndex;
		Opcode op = NameToOpcode(words[0], usesAccIndex);
		Assert::IsTrue(op != Opcode::INDETERMINATE, Wide("not an opcode: " + words[0]).c_str());
		if ((op == Opcode::BT) || (op == Opcode::BNT) || (op == Opcode::JMP))
		{
			Assert::AreEqual((size_t)2, words.size(), Wide("a branch takes one label: " + line).c_str());
			code.push_back(scii(sciVersion1_1, op, code.end(), true, -1));
			code_pos pos = code.end();
			--pos;
			branches.push_back({ pos, words[1], index });
		}
		else
		{
			uint16_t operands[3] = { 0, 0, 0 };
			for (size_t k = 1; (k < words.size()) && (k <= 3); ++k)
			{
				operands[k - 1] = ParseNumber(words[k]);
			}
			code.push_back(scii(sciVersion1_1, op, operands[0], operands[1], operands[2], -1));
		}
		code.back().set_offset_and_size((uint16_t)index, 1);
		++index;
	}
	std::vector<code_pos> positions;
	for (code_pos pos = code.begin(); pos != code.end(); ++pos)
	{
		positions.push_back(pos);
	}
	for (Branch &branch : branches)
	{
		int target = At(branch.label);
		Assert::IsTrue(target < (int)positions.size(), Wide("a label after the last instruction: " + branch.label).c_str());
		branch.pos->set_branch_target(positions[target], target > branch.index);
	}
}

int ScopeAsm::At(const std::string &label) const
{
	auto found = _labels.find(label);
	Assert::IsTrue(found != _labels.end(), Wide("no label " + label).c_str());
	return found->second;
}
