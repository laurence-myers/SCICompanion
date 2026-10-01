#include "stdafx.h"
#include "DecompileEngine.h"
#include "Result.h"
#include "format.h"

const char *const DecompileEngineVariable = "SCIC_DECOMPILE_ENGINE";

const char *DecompileEngineName(DecompileEngine engine)
{
	switch (engine)
	{
	case DecompileEngine::Scope:
		return "scope";
	case DecompileEngine::ScopeThenClassic:
		return "auto";
	default:
		return "classic";
	}
}

bool ParseDecompileEngine(const std::string &text, DecompileEngine &engine)
{
	for (DecompileEngine candidate : { DecompileEngine::Classic, DecompileEngine::Scope, DecompileEngine::ScopeThenClassic })
	{
		if (text == DecompileEngineName(candidate))
		{
			engine = candidate;
			return true;
		}
	}
	return false;
}

std::string DecompileEngineVariableValue()
{
	// A value of any length.
	DWORD length = GetEnvironmentVariableA(DecompileEngineVariable, nullptr, 0);
	if (length <= 1)
	{
		return std::string();
	}
	std::string value(length, '\0');
	DWORD copied = GetEnvironmentVariableA(DecompileEngineVariable, &value[0], length);
	if ((copied == 0) || (copied >= length))
	{
		return std::string();
	}
	value.resize(copied);
	return value;
}

DecompileEngine DefaultDecompileEngine()
{
	std::string value = DecompileEngineVariableValue();
	DecompileEngine engine = DecompileEngine::Scope;
	if (!value.empty() && !ParseDecompileEngine(value, engine))
	{
		throw sci::DataError(fmt::format("{0} is \"{1}\"; give classic, scope or auto", DecompileEngineVariable, value), sci::ErrorCode::Usage);
	}
	return engine;
}
