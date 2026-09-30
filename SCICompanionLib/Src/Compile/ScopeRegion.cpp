#include "stdafx.h"
#include "ScopeRegion.h"
#include "format.h"

namespace scope
{
	std::unique_ptr<Region> MakeSequence()
	{
		return std::make_unique<Region>(RegionKind::Sequence);
	}

	std::unique_ptr<Region> MakeCode(int first, int last)
	{
		std::unique_ptr<Region> code = std::make_unique<Region>(RegionKind::Code);
		code->first = first;
		code->last = last;
		return code;
	}

	namespace
	{
		void _Layout(const Region *region, std::vector<int> &out)
		{
			if (!region)
			{
				return;
			}
			switch (region->kind)
			{
			case RegionKind::Sequence:
				for (const auto &item : region->items)
				{
					_Layout(item.get(), out);
				}
				break;
			case RegionKind::Code:
				for (int i = region->first; i <= region->last; ++i)
				{
					out.push_back(i);
				}
				break;
			case RegionKind::If:
				for (size_t k = 0; k < region->tests.size(); ++k)
				{
					if (k > 0)
					{
						_Layout((k - 1 < region->terms.size()) ? region->terms[k - 1].get() : nullptr, out);
					}
					out.push_back(region->tests[k]);
				}
				_Layout(region->thenPart.get(), out);
				if (region->elseKind == ElseKind::Else)
				{
					out.push_back(region->branch);
					_Layout(region->elsePart.get(), out);
				}
				break;
			case RegionKind::Or:
				out.push_back(region->branch);
				_Layout(region->body.get(), out);
				break;
			case RegionKind::Loop:
				_Layout(region->body.get(), out);
				_Layout(region->step.get(), out);
				out.push_back(region->branch);
				break;
			case RegionKind::Switch:
				out.push_back(region->head);
				for (const auto &item : region->cases)
				{
					_Layout(item.get(), out);
				}
				out.push_back(region->toss);
				break;
			case RegionKind::Case:
				_Layout(region->value.get(), out);
				if (region->branch != NoIndex)
				{
					out.push_back(region->branch);
				}
				_Layout(region->body.get(), out);
				if (region->caseJmp != NoIndex)
				{
					out.push_back(region->caseJmp);
				}
				break;
			case RegionKind::Break:
			case RegionKind::Continue:
			case RegionKind::BreakIf:
			case RegionKind::ContIf:
				out.push_back(region->branch);
				break;
			}
		}

		class Dumper
		{
		public:
			explicit Dumper(const CodeModel &model) : _model(model) {}

			std::string Text() const { return _text; }

			void Items(const Region *sequence, int indent)
			{
				if (sequence)
				{
					for (const auto &item : sequence->items)
					{
						Node(*item, indent);
					}
				}
			}

			void Node(const Region &region, int indent)
			{
				switch (region.kind)
				{
				case RegionKind::Sequence:
					Line(indent, "seq");
					Items(&region, indent + 1);
					break;
				case RegionKind::Code:
					Line(indent, (region.first == region.last) ?
						fmt::format("code {0}", Address(region.first)) :
						fmt::format("code {0}-{1}", Address(region.first), Address(region.last)));
					break;
				case RegionKind::If:
				{
					std::string line = "if";
					for (int test : region.tests)
					{
						line += " " + Address(test);
					}
					Line(indent, line);
					for (const auto &term : region.terms)
					{
						Line(indent + 1, "term");
						Items(term.get(), indent + 2);
					}
					Line(indent + 1, "then");
					Items(region.thenPart.get(), indent + 2);
					switch (region.elseKind)
					{
					case ElseKind::Else:
						Line(indent + 1, "else " + Address(region.branch));
						Items(region.elsePart.get(), indent + 2);
						break;
					case ElseKind::Break:
						Line(indent + 1, fmt::format("else break {0}", region.level));
						break;
					case ElseKind::Continue:
						Line(indent + 1, fmt::format("else continue {0}", region.level));
						break;
					case ElseKind::None:
						break;
					}
					break;
				}
				case RegionKind::Or:
					Line(indent, "or " + Address(region.branch));
					Items(region.body.get(), indent + 1);
					break;
				case RegionKind::Loop:
					Line(indent, fmt::format("loop {0} latch {1}", Address(region.head), Address(region.branch)));
					Line(indent + 1, "body");
					Items(region.body.get(), indent + 2);
					if (region.step)
					{
						Line(indent + 1, "step");
						Items(region.step.get(), indent + 2);
					}
					break;
				case RegionKind::Switch:
					Line(indent, fmt::format("switch {0} toss {1}", Address(region.head), Address(region.toss)));
					for (const auto &item : region.cases)
					{
						Node(*item, indent + 1);
					}
					break;
				case RegionKind::Case:
				{
					std::string line = "case";
					if (region.branch != NoIndex)
					{
						line += " bnt " + Address(region.branch);
					}
					if (region.caseJmp != NoIndex)
					{
						line += " jmp " + Address(region.caseJmp);
					}
					Line(indent, line);
					if (region.value)
					{
						Line(indent + 1, "value");
						Items(region.value.get(), indent + 2);
					}
					Line(indent + 1, "body");
					Items(region.body.get(), indent + 2);
					break;
				}
				case RegionKind::Break:
					Line(indent, fmt::format("break {0} {1}", region.level, Address(region.branch)));
					break;
				case RegionKind::Continue:
					Line(indent, fmt::format("continue {0} {1}", region.level, Address(region.branch)));
					break;
				case RegionKind::BreakIf:
					Line(indent, fmt::format("breakif {0} {1}", region.level, Address(region.branch)));
					break;
				case RegionKind::ContIf:
					Line(indent, fmt::format("contif {0} {1}", region.level, Address(region.branch)));
					break;
				}
			}

		private:
			std::string Address(int index) const
			{
				if ((index < 0) || (index >= _model.Size()))
				{
					return "????";
				}
				return fmt::format("{0:04x}", _model.Offset(index));
			}

			void Line(int indent, const std::string &text)
			{
				_text += std::string(indent * 2, ' ') + text + "\n";
			}

			const CodeModel &_model;
			std::string _text;
		};
	}

	std::vector<int> Layout(const Region &root)
	{
		std::vector<int> out;
		_Layout(&root, out);
		return out;
	}

	std::string Dump(const CodeModel &model, const Region &root)
	{
		Dumper dumper(model);
		if (root.kind == RegionKind::Sequence)
		{
			dumper.Items(&root, 0);
		}
		else
		{
			dumper.Node(root, 0);
		}
		return dumper.Text();
	}
}
