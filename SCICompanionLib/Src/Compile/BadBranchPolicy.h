#pragma once

// What the decompiler gives for a function with a bad branch: a bnt of a
// fault of Sierra's compiler (scii::is_bad_branch), or a branch out of the
// function (scii::is_outside_function). The decode is the same for both.
enum class BadBranchPolicy
{
	Fix,	// text, when the scope engine can read the function
	Asm,	// asm, with a comment at each such branch
};
