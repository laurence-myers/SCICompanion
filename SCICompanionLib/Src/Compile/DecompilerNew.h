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
#pragma once

namespace scope
{
	class CodeModel;
	struct Region;
}

// The scope engine: the forward value stage makes the chunk tree of the
// region tree, and the chunk tree gives the statements of func. code is the
// list that the model was made from; passedDeadBranches has the dead branches
// that a path of the tree goes through (scope::Verify). Throws a
// scope::ScopeError when a stage fails.
void OutputNewStructure(sci::FunctionBase &func, const scope::CodeModel &model, const scope::Region &root, std::list<scii> &code, const std::set<int> &passedDeadBranches, DecompileLookups &lookups);
