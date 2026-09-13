/***************************************************************************
	Copyright (c) 2026 Philip Fortier

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

namespace sci
{
	class FunctionBase;
}
class IDecompilerResults;

struct AstPassOptions
{
	int maxSweeps = 32;
};

// Rewrites one decompiled function's AST into idiomatic form:
//  - nested ifs and value-position ifs become (and ...) / (or ...)
//  - double nots in a boolean context collapse
//  - loop-exit else-breaks are folded into the loop test and factored out
//  - (= a (op a b)) becomes (op= a b)
// Runs after control-flow decompilation, on a function that decompiled
// successfully. See AstRewrite for the framework.
void RunDecompilerAstPasses(sci::FunctionBase &func, const AstPassOptions &options, IDecompilerResults *results);
