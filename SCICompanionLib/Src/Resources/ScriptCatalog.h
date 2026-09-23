#pragma once

// The script services of the command line (docs/scic-cli/plan.md sections
// 3.3, 4.2, 4.3 and 5): the list of the scripts of a game, the script
// selectors, and the shadow check before a package write. They read the
// script names of the session (ScriptNameMap). Only AddDerivedScriptNames
// changes the session; nothing here writes a file.

#include "Result.h"
#include "ScriptNameMap.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameSession;
class GameFolderHelper;

struct ScriptRow
{
    uint16_t number = 0;
    // The name that list and decompile use: from rules 1 to 3, else the
    // derived name, else nNNN.
    std::string name;
    NameSource source = NameSource::Default;
    // The derived name (rule 4) when ListScripts derives every name
    // (alwaysDerive), also for a script that has a name from rules 1 to 3.
    std::string derivedName;
    // Where the game has the compiled script: "resource.000", or
    // "110.scr (patch)"; "" when it has none.
    std::string location;
    bool hasSource = false;         // src\<name>.sc exists
    bool hasObjectFile = false;     // src\<name>.sco exists
    // Why the compiled script cannot be read. "" when it was read, or when
    // ListScripts did not read it (no name had to be derived).
    std::string error;
};

// The derived names (rule 4) for the compiled scripts that have no name from
// rules 1 to 3, with the names of rules 1 to 3 counted as used. With all: for
// every compiled script, as "reset the names" gives them. A script that
// cannot be read gets no name, and its error goes to errors.
std::map<uint16_t, std::string> DeriveScriptNames(GameSession &session, bool all, std::map<uint16_t, std::string> *errors = nullptr);

// Gives the session's names the derived name of each compiled script that
// has no name (rule 4): the decompiler needs a name for every script before
// it writes a (use ...) line.
sci::Status AddDerivedScriptNames(GameSession &session);

// Plan section 4.3: each script of the game, in number order. That is each
// script resource, and each script that has only a source file, a .sco file
// or a game.ini entry. When a compiled script has no name from rules 1 to 3,
// or with alwaysDerive, the compiled scripts are read to derive names.
sci::Result<std::vector<ScriptRow>> ListScripts(GameSession &session, bool alwaysDerive);

enum class SelectorMode
{
    List,       // any script of the list
    Decompile,  // a script that the game has compiled
    Compile,    // a script with a source file; also a path to src\*.sc
    Sco,        // a script with a source file and a compiled script
};

struct ScriptSelection
{
    // The path is src\<name>.sc (for Compile, the file that the selector
    // gave), and the resource number is set.
    std::vector<ScriptId> scripts;
    // Scripts that --all leaves out, and why.
    std::vector<std::string> warnings;
};

// Plan section 4.2. A selector is a number, a range ("100-199": each script
// in the range that the mode takes), a name (ignoring case; List and
// Decompile also take a derived name), or for Compile a path to a file in
// src\ (not a header). The result has no duplicates. Compile keeps the order
// of game.ini [Script], then number order; the others use number order.
// Every bad selector is in one Usage error. A mode that writes (Decompile,
// Compile, Sco) is a Usage error while the script names have a conflict.
sci::Result<ScriptSelection> ResolveScriptSelectors(GameSession &session, const std::vector<std::string> &selectors, SelectorMode mode);

// --all: every script that the mode takes, in the same order. For Compile,
// a named script with no source file is left out, with a warning.
sci::Result<ScriptSelection> SelectAllScripts(GameSession &session, SelectorMode mode);

struct ResourceKey
{
    ResourceType type;
    uint16_t number;
};

// Plan section 5: the patch files in the game folder that would hide a
// package copy of these resources, with a standard name (110.scr) or
// another one (0110.scr). Full paths, in order.
sci::Result<std::vector<std::string>> FindShadowingPatches(const GameFolderHelper &helper, const std::vector<ResourceKey> &resources);
