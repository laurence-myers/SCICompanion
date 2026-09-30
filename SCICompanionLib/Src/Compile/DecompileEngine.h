#pragma once

#include <string>

// The engine that turns the instructions of a function into source.
enum class DecompileEngine
{
	Classic,			// the control-flow graph and the backward walk of the instructions
	Scope,				// the scope parser and the forward value stage
	ScopeThenClassic,	// Scope; Classic for a function that Scope cannot decompile
};

// The name of the engine on the command line: classic, scope or auto.
const char *DecompileEngineName(DecompileEngine engine);

// The engine of a name that DecompileEngineName gives. False for another
// text.
bool ParseDecompileEngine(const std::string &text, DecompileEngine &engine);

// The environment variable that gives the engine when no option gives it.
extern const char *const DecompileEngineVariable;

// The value of SCIC_DECOMPILE_ENGINE; empty when it is not set.
std::string DecompileEngineVariableValue();

// The engine when no option gives it: the engine of SCIC_DECOMPILE_ENGINE,
// else Classic. Throws a sci::DataError (Usage) when the variable has an
// unknown value, so that a wrong name does not silently give Classic.
DecompileEngine DefaultDecompileEngine();
