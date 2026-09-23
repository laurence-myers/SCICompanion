/***************************************************************************
	Copyright (c) 2020 Philip Fortier

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
***************************************************************************/
#include "stdafx.h"
#include "Text.h"
#include "ResourceEntity.h"
#include <atomic>

using namespace std;

std::string AutoGenTextSentinel = "_AUTOGEN_";

//From DOS-437 to Win-1252
const unsigned char dos2winTable[] = {
	/* 80 */ 0xC7, 0xFC, 0xE9, 0xE2, 0xE4, 0xE0, 0xE5, 0xE7, 0xEA, 0xEB, 0xE8, 0xEF, 0xEE, 0xEC, 0xC4, 0xC5,
	/* 90 */ 0xC9, 0xE6, 0xC6, 0xF4, 0xF6, 0xF2, 0xFB, 0xF9, 0xFF, 0xD6, 0xDC, 0xA2, 0xA3, 0xA5, 0x3F, 0x3F,
	/* A0 */ 0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xAA, 0xBA, 0xBF, 0x3F, 0xAC, 0xBD, 0xBC, 0xA1, 0xAB, 0xBB,
};
//From Win-1252 to DOS-437
const unsigned char win2dosTable[] = {
	/* C0 */ 0x3F, 0x3F, 0x3F, 0x3F, 0x8E, 0x8F, 0x3F, 0x80, 0x3F, 0x90, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	/* D0 */ 0x3F, 0xA5, 0x3F, 0x3F, 0x3F, 0x3F, 0x99, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x9A, 0x3F, 0x3F, 0x3F,
	/* E0 */ 0x85, 0xA0, 0x83, 0x3F, 0x84, 0x86, 0x91, 0x87, 0x8A, 0x82, 0x88, 0x89, 0x8D, 0xA1, 0x8C, 0x8B,
	/* F0 */ 0x3F, 0xA4, 0x95, 0xA2, 0x93, 0x3F, 0x94, 0x3F, 0x3F, 0x97, 0xA3, 0x96, 0x81, 0x3F, 0x3F, 0x98,
};
//Wait, where's 0xE1/U+00DF, the Sharp S? It's so separate from the rest that I can't be arsed, and just hardcode it.

namespace
{
	std::atomic<int> g_textCodepage(437);
}

void SetTextCodepage(int codepage)
{
	g_textCodepage = (codepage == 1252) ? 1252 : 437;
}

int GetTextCodepage()
{
	return g_textCodepage;
}

std::string Dos2Win(std::string &str)
{
	if (GetTextCodepage() == 1252)
		return str;
	std::string ret;
	for (size_t i = 0; i < str.length(); i++)
	{
		if ((unsigned char)str[i] == 0xE1) ret.push_back(0xDFu); //sharp s
		else if ((unsigned char)str[i] >= 0x80 && (unsigned char)str[i] <= 0xAF)
			ret.push_back(dos2winTable[(unsigned char)str[i] - 0x80]);
		else
			ret.push_back(str[i]);
	}
	return ret;
}

std::string Win2Dos(const std::string &str)
{
	if (GetTextCodepage() == 1252)
		return str;
	std::string ret;
	for (size_t i = 0; i < str.length(); i++)
	{
		if ((unsigned char)str[i] == 0xDF) ret.push_back(0xE1u); //sharp s
		else if ((unsigned char)str[i] >= 0xC0 && (unsigned char)str[i] <= 0xFF)
			ret.push_back(win2dosTable[(unsigned char)str[i] - 0xC0]);
		else
			ret.push_back(str[i]);
	}
	return ret;
}

bool TextEntry::operator == (const TextEntry &other) const
{
	return Noun == other.Noun &&
		Verb == other.Verb &&
		Condition == other.Condition &&
		Sequence == other.Sequence &&
		Talker == other.Talker &&
		Reference == other.Reference &&
		Text == other.Text &&
		Talker == other.Talker;
}
bool TextEntry::operator!=(const TextEntry &other) const
{
	return !(*this == other);
}

int TextComponent::AddString(const std::string &theString)
{
	TextEntry entry = { 0 };
	entry.Text = theString;
	Texts.push_back(entry);
	return (int)(Texts.size() - 1); // Index of added string
}

int TextComponent::AddStringDedupe(const std::string &theString)
{
	for (size_t i = 0; i < Texts.size(); i++)
	{
		if (Texts[i].Text == theString)
		{
			// This string already exists.  Just re-use it.
			return (int)i;
		}
	}
	return AddString(theString);
}

TextChangeHint TextComponent::SetStringAt(int iIndex, const std::string &theString)
{
	TextChangeHint hint = (theString != Texts[iIndex].Text) ? TextChangeHint::Changed : TextChangeHint::None;
	Texts[iIndex].Text = theString;
	return hint;
}

TextChangeHint TextComponent::MoveStringUp(int iIndex)
{
	TextChangeHint hint = TextChangeHint::None;
	if ((iIndex > 0) && (iIndex < (int)Texts.size()))
	{
		hint = TextChangeHint::Changed;
		std::swap(Texts[iIndex - 1], Texts[iIndex]);
	}
	return hint;
}

TextChangeHint TextComponent::MoveStringDown(int iIndex)
{
	TextChangeHint hint = TextChangeHint::None;
	if (Texts.size() > 1)
	{
		if (iIndex < (int)(Texts.size() - 1))
		{
			hint = TextChangeHint::Changed;
			std::swap(Texts[iIndex + 1], Texts[iIndex]);
		}
	}
	return hint;
}

TextChangeHint TextComponent::DeleteString(int iIndex)
{
	Texts.erase(Texts.begin() + iIndex);
	return TextChangeHint::Changed;
}

// ILookupNames
std::string TextComponent::Lookup(uint16_t wName) const
{
	std::string ret;
	if (wName < Texts.size())
	{
		ret = Texts[wName].Text;
	}
	return ret;
}

bool TextComponent::AreTextsEqual(const TextComponent &other) const
{
	bool maybeEqual = other.Texts.size() == Texts.size();
	for (size_t i = 0; maybeEqual && (i < Texts.size()); i++)
	{
		maybeEqual = (Texts[i] == other.Texts[i]);
	}
	return maybeEqual;
}

bool TextComponent::WasAutoGenerated() const
{
	for (const TextEntry &text : Texts)
	{
		if (text.Text == AutoGenTextSentinel)
		{
			return true;
		}
	}
	return false;
}

void TextWriteTo(const ResourceEntity &resource, sci::ostream &byteStream, std::map<BlobKey, uint32_t> &propertyBag)
{
	const TextComponent &text = resource.GetComponent<TextComponent>();
	// Note: this function is not unicode aware
	for (size_t i = 0; i < text.Texts.size(); i++)
	{
		const string &str = Win2Dos(text.Texts[i].Text);
		byteStream.WriteBytes((uint8_t*)str.c_str(), (int)str.length() + 1);
	}
}

void TextReadFrom(ResourceEntity &resource, sci::istream &byteStream, const std::map<BlobKey, uint32_t> &propertyBag)
{
	TextComponent &text = resource.GetComponent<TextComponent>();
	assert(text.Texts.empty());
	text.Flags = MessagePropertyFlags::None;
	// Catch our own exceptions.
	try
	{
		while (byteStream.has_more_data())
		{
			string str;
			byteStream >> str;
			if (byteStream.good())
			{
				TextEntry entry = { 0 };				
				entry.Text = Dos2Win(str);
				text.Texts.push_back(entry);
			}
		}
	}
	catch (...) {}
}

ResourceTraits textTraits =
{
	ResourceType::Text,
	&TextReadFrom,
	&TextWriteTo,
	&NoValidationFunc,
	nullptr
};

ResourceEntity *CreateTextResource(SCIVersion version)
{
	std::unique_ptr<ResourceEntity> pResource = std::make_unique<ResourceEntity>(textTraits);
	pResource->AddComponent(move(make_unique<TextComponent>()));
	return pResource.release();
}

ResourceEntity *CreateDefaultTextResource(SCIVersion version)
{
	// Nothing different.
	return CreateTextResource(version);
}
