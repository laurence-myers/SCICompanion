#include "stdafx.h"
#include "CppUnitTest.h"
#include "ScopeAsm.h"
#include "ScopeRegion.h"
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

namespace
{
	using namespace scope;

	struct RegionLine
	{
		int indent;
		std::vector<std::string> words;
		std::string text;
	};

	class RegionReader
	{
	public:
		explicit RegionReader(const std::string &text)
		{
			std::istringstream lines(text);
			std::string line;
			while (std::getline(lines, line))
			{
				if (!line.empty() && (line.back() == '\r'))
				{
					line.pop_back();
				}
				size_t spaces = line.find_first_not_of(' ');
				if (spaces == std::string::npos)
				{
					continue;
				}
				RegionLine entry;
				entry.indent = (int)spaces;
				entry.text = line;
				std::istringstream tokens(line);
				std::string word;
				while (tokens >> word)
				{
					entry.words.push_back(word);
				}
				_lines.push_back(entry);
			}
			if (!_lines.empty())
			{
				_base = _lines[0].indent;
			}
		}

		std::unique_ptr<Region> Root()
		{
			std::unique_ptr<Region> root = Items(_base);
			Assert::IsTrue(_next == _lines.size(), Wide("a line with a bad indent: " + (_next < _lines.size() ? _lines[_next].text : std::string())).c_str());
			return root;
		}

	private:
		static int Number(const std::string &word)
		{
			return (int)std::stoul(word, nullptr, 16);
		}

		bool At(int indent, const char *first) const
		{
			return (_next < _lines.size()) && (_lines[_next].indent == indent) && (_lines[_next].words[0] == first);
		}

		// The items at an indent, until a line with a smaller indent.
		std::unique_ptr<Region> Items(int indent)
		{
			std::unique_ptr<Region> sequence = MakeSequence();
			while ((_next < _lines.size()) && (_lines[_next].indent >= indent))
			{
				Assert::AreEqual(indent, _lines[_next].indent, Wide("a line with a bad indent: " + _lines[_next].text).c_str());
				sequence->items.push_back(Node(indent));
			}
			return sequence;
		}

		// A label line ("then", "body") and the items under it.
		std::unique_ptr<Region> Labelled(int indent, const char *label)
		{
			Assert::IsTrue(At(indent, label), Wide(std::string("no ") + label + " line").c_str());
			++_next;
			return Items(indent + 2);
		}

		std::unique_ptr<Region> Node(int indent)
		{
			const RegionLine &line = _lines[_next++];
			const std::vector<std::string> &w = line.words;
			const std::string &kind = w[0];
			std::unique_ptr<Region> region;
			if (kind == "code")
			{
				size_t dash = w[1].find('-');
				int first = Number(w[1].substr(0, dash));
				int last = (dash == std::string::npos) ? first : Number(w[1].substr(dash + 1));
				return MakeCode(first, last);
			}
			if (kind == "if")
			{
				region = std::make_unique<Region>(RegionKind::If);
				for (size_t k = 1; k < w.size(); ++k)
				{
					region->tests.push_back(Number(w[k]));
				}
				while (At(indent + 2, "term"))
				{
					region->terms.push_back(Labelled(indent + 2, "term"));
				}
				region->thenPart = Labelled(indent + 2, "then");
				if (At(indent + 2, "else"))
				{
					const std::vector<std::string> &e = _lines[_next].words;
					if (e[1] == "break" || e[1] == "continue")
					{
						region->elseKind = (e[1] == "break") ? ElseKind::Break : ElseKind::Continue;
						region->level = std::stoi(e[2]);
						++_next;
					}
					else
					{
						region->elseKind = ElseKind::Else;
						region->branch = Number(e[1]);
						region->elsePart = Labelled(indent + 2, "else");
					}
				}
				return region;
			}
			if (kind == "or")
			{
				region = std::make_unique<Region>(RegionKind::Or);
				region->branch = Number(w[1]);
				region->body = Items(indent + 2);
				return region;
			}
			if (kind == "loop")
			{
				region = std::make_unique<Region>(RegionKind::Loop);
				region->head = Number(w[1]);
				region->branch = Number(w[3]);
				region->body = Labelled(indent + 2, "body");
				if (At(indent + 2, "step"))
				{
					region->step = Labelled(indent + 2, "step");
				}
				return region;
			}
			if (kind == "switch")
			{
				region = std::make_unique<Region>(RegionKind::Switch);
				region->head = Number(w[1]);
				region->toss = Number(w[3]);
				while (At(indent + 2, "case"))
				{
					const std::vector<std::string> &c = _lines[_next++].words;
					std::unique_ptr<Region> item = std::make_unique<Region>(RegionKind::Case);
					for (size_t k = 1; k + 1 < c.size(); k += 2)
					{
						(c[k] == "bnt" ? item->branch : item->caseJmp) = Number(c[k + 1]);
					}
					if (At(indent + 4, "value"))
					{
						item->value = Labelled(indent + 4, "value");
					}
					item->body = Labelled(indent + 4, "body");
					region->cases.push_back(std::move(item));
				}
				return region;
			}
			if (kind == "exit")
			{
				region = std::make_unique<Region>(RegionKind::Exit);
				region->branch = Number(w[1]);
				return region;
			}
			RegionKind jumpKind;
			if (kind == "break")
			{
				jumpKind = RegionKind::Break;
			}
			else if (kind == "continue")
			{
				jumpKind = RegionKind::Continue;
			}
			else if (kind == "breakif")
			{
				jumpKind = RegionKind::BreakIf;
			}
			else
			{
				Assert::AreEqual(std::string("contif"), kind, Wide("an unknown region: " + line.text).c_str());
				jumpKind = RegionKind::ContIf;
			}
			region = std::make_unique<Region>(jumpKind);
			region->level = std::stoi(w[1]);
			region->branch = Number(w[2]);
			return region;
		}

		std::vector<RegionLine> _lines;
		size_t _next = 0;
		int _base = 0;
	};
}

std::unique_ptr<scope::Region> ParseRegions(const std::string &text)
{
	RegionReader reader(text);
	return reader.Root();
}
