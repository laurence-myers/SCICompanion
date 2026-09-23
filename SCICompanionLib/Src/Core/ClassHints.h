#pragma once

// Optional help for compile error messages: the script that exports a name
// ("did you forget a use?"). The GUI's class browser gives it; the command
// line has none, and the compile is the same without it.

#include <string>

class IClassHints
{
public:
    virtual ~IClassHints() = default;
    // The name of the script (with no ".sc") that exports the procedure, the
    // class or the main global with this name, or an empty string.
    virtual std::string ScriptThatExports(const std::string &identifier) = 0;
};
