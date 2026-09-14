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
#include "SyntaxParser.h"
#include "SCISyntaxParser.h"

// Our parser global variable. There is only one script syntax now (Sierra).
SCISyntaxParser g_sci;

void InitializeSyntaxParsers()
{
	g_sci.Load();
}

bool SyntaxParser_ParseAC(sci::Script &script, CCrystalScriptStream::const_iterator &streamIt, std::unordered_set<std::string> preProcessorDefines, SyntaxContext *pContext)
{
	bool fRet = false;
	if (!script.IsHeader())
	{
		fRet = g_sci.Parse(script, streamIt, preProcessorDefines, *pContext);
	}
	return fRet;
}

bool SyntaxParser_Parse(sci::Script &script, CCrystalScriptStream &stream, std::unordered_set<std::string> preProcessorDefines, ICompileLog *pLog, bool fParseComments, SyntaxContext *pContext, bool addCommentsToOM)
{
	bool fRet = false;
	if (script.IsHeader())
	{
		fRet = g_sci.ParseHeader(script, stream.begin(), preProcessorDefines, pLog, fParseComments);
	}
	else
	{
		if (pContext)
		{
			// Someone is doing a partial compile (e.g. tooltips) and supply their own context.
			fRet = g_sci.Parse(script, stream.begin(), preProcessorDefines, *pContext);
		}
		else
		{
			// Or maybe someone either wants error logs:
			fRet = g_sci.Parse(script, stream.begin(), preProcessorDefines, pLog, addCommentsToOM, fParseComments);
		}
	}
	return fRet;
}

std::unordered_set<std::string> PreProcessorDefinesFromSCIVersion(SCIVersion version)
{
	std::unordered_set<std::string> defines;
	// Only two versions supported for now.
	if (version.SeparateHeapResources)
	{
		defines.insert("SCI_1_1");
	}
	else
	{
		defines.insert("SCI_0");
	}
	return defines;
}
