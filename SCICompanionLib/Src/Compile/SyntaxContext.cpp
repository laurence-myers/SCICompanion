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
//
// Non-template SyntaxContext members and the shared parser error messages.
// These are language-agnostic support for the Sierra parser.
//
#include "stdafx.h"
#include "ScriptOMAll.h"
#include "SyntaxContext.h"

// Parser error messages. Declared extern in ParserActions.h so they can be used
// as function-template parameters by the parser's error actions.
extern char const errBinaryOp[] = "Expected second argument.";
extern char const errCaseArg[] = "Expected case value.";
extern char const errSwitchArg[] = "Expected switch argument.";
extern char const errSendObject[] = "Expected send object.";
extern char const errArgument[] = "Expected argument.";
extern char const errInteger[] = "Expected integer literal.";
extern char const errIntegerTooLarge[] = "Number too large. Largest 16-bit number is 65535.";
extern char const errIntegerTooSmall[] = "Number too small. Smallest 16-bit number is -32768.";
extern char const errThen[] = "Expected then clause.";
extern char const errVarName[] = "Expected variable name.";
extern char const errFileNameString[] = "Expected file name string.";
extern char const errElse[] = "Expected else clause.";
extern char const errNoKeywordOrSelector[] = "No keyword or selector permitted here.";
extern char const errCollectionArg[] = "Expected collection.";

//
// Our syntax context implementations
//
const sci::SyntaxNode *SyntaxContext::GetSyntaxNode(sci::NodeType type) const
{
	sci::SyntaxNode *pNode = nullptr;
	auto it = _statements._Get_container().rbegin();
	while (it != _statements._Get_container().rend())
	{
		if (*it)
		{
			if (type == (*it)->GetNodeType())
			{
				pNode = (*it).get();
				break;
			}
		}
		++it;
	}
	return pNode;
}

sci::NodeType SyntaxContext::GetTopKnownNode() const
{
	sci::NodeType type = sci::NodeTypeUnknown;
	auto it = _statements._Get_container().rbegin();
	while (it != _statements._Get_container().rend())
	{
		if (*it)
		{
			type =(*it)->GetNodeType();
			break;
		}
		++it;
	}
	return type;
}

void SyntaxContext::ReportError(const std::string &error, streamIt pos)
{
#ifdef PARSE_DEBUG
	if (ParseDebug)
	{
		OutputDebugString(error.c_str());
		OutputDebugString("\n");
	}
#endif
	// Prefer already-reported errors at the same spot
	// Otherwise prefer errors at a later position (presumably it means more
	// stuff got successfully parsed)
	if ((_beginning < pos) || _error.empty())
	{
		_error = error;
		_beginning = pos;
	}
}
