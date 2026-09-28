#pragma once

// The name of each script of a game, also when the game has no game.ini
// (docs/scic-cli/plan.md, section 3.4). For each script number, the first
// rule that has a name wins:
//   1. game.ini [Script] (nNNN=Name), when the file exists;
//   2. a src\*.sc file that declares (script# N): its file title;
//   3. a src\*.sco file for script N: its file title;
//   4. a name derived from the compiled script (SuggestScriptNames), which a
//      command adds only when it needs it;
//   5. nNNN.
// A GameSession installs a map in the game's GameFolderHelper when it opens
// the game, and the helper then takes the script names from the map
// (GetScriptTitle). The GUI installs none, and reads game.ini.

#include "Result.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameFolderHelper;

enum class NameSource
{
    GameIni,    // rule 1
    Source,     // rule 2
    Sco,        // rule 3
    Derived,    // rule 4
    Default,    // rule 5: nNNN
};

// "game.ini", "source", "sco", "derived" or "default".
const char *NameSourceText(NameSource source);

// Two files that give one script its name, or one name for two scripts. A
// conflict names its scripts, so that a command that writes can refuse a
// selection with one of them, and leave them out of --all with a warning.
struct NameConflict
{
    std::vector<uint16_t> numbers;  // the scripts in the conflict
    std::vector<std::string> names; // the file titles, or the name, in the conflict
    std::string text;               // what is wrong, and how to fix it
};

class ScriptNameMap
{
public:
    struct Entry
    {
        std::string name;
        NameSource source;
    };

    // Rules 1 to 3. Two src\*.sc files (or two src\*.sco files) that give
    // one script its name, or one name for two scripts, are not an error
    // here: Conflicts() describes each problem. The map gives no name from
    // two files; two scripts with one name keep it. A command that writes
    // (compile, decompile, sco) refuses a selection with a script in a
    // conflict, and --all leaves the script out. game.ini gives a name
    // only with the key that the GUI reads (n007, not n7 or n0007), and
    // without the quotes around it.
    static sci::Result<ScriptNameMap> Build(const GameFolderHelper &helper);

    // Rule 4: gives each script in names that has no name yet its derived
    // name.
    void AddDerivedNames(const std::map<uint16_t, std::string> &names);
    // A reset of the names (--reset-names): each script in names gets its
    // derived name, also a script that has a name from rules 1 to 3.
    void ReplaceNames(const std::map<uint16_t, std::string> &names);

    // Rule 5 when no rule gives a name: nNNN.
    std::string NameOf(uint16_t number) const;
    NameSource SourceOf(uint16_t number) const;
    // The script that has this name, ignoring case. False when no script
    // has it.
    bool NumberOf(const std::string &name, uint16_t &number) const;
    // The scripts that have a name from rules 1 to 4, in number order.
    const std::map<uint16_t, Entry> &Entries() const { return _entries; }
    // The script numbers of game.ini [Script], in the order of the file.
    const std::vector<uint16_t> &GameIniOrder() const { return _gameIniOrder; }
    const std::vector<NameConflict> &Conflicts() const { return _conflicts; }
    // The conflicts that name the script.
    std::vector<const NameConflict *> ConflictsOf(uint16_t number) const;
    // The script of a conflict with this file title or name, ignoring case.
    // A conflict keeps its script out of the names, and a selector must
    // still find it.
    bool ConflictNumberOf(const std::string &name, uint16_t &number) const;
    // The title of each .sc and .sco file in src. A derived name takes none of
    // them, so a decompile does not write over the file of another script.
    const std::vector<std::string> &FileTitles() const { return _fileTitles; }
    // The files in src whose names have a character that the ANSI code page
    // does not have (shown with '?'). The map skips them, so such a file
    // does not stop the open of the game.
    const std::vector<std::string> &SkippedFiles() const { return _skippedFiles; }

private:
    std::map<uint16_t, Entry> _entries;
    std::vector<uint16_t> _gameIniOrder;
    std::vector<NameConflict> _conflicts;
    std::vector<std::string> _fileTitles;
    std::vector<std::string> _skippedFiles;
};

// The number that a source file declares with (script# X), where X is a
// number, or a define of the file or of src\*.sh. False when the scan cannot
// read one (for example, X is a define of an include outside src\). It does
// not throw.
bool ReadDeclaredScriptNumber(const GameFolderHelper &helper, const std::string &sourcePath, uint16_t &number);

// The objects of a compiled script that the naming rule reads, in the order
// of the compiled script.
struct ScriptObjectsForNaming
{
    struct Object
    {
        std::string name;
        bool isClass;
        bool isPublic;
    };
    uint16_t number = 0;
    std::vector<Object> objects;
};

// The decompiler's naming rule (rule 4). Script 0 is "Main". Another script
// gets the name of its first class (a class named "Game" wins), or else of
// its first public instance; a script with neither gets no name. A character
// that a name cannot have (a name has only letters, digits and '_': the
// parser takes no '-' in (use ...)) becomes '_', a name that starts with a
// digit gets a '_' before it, and a Windows device name (CON, NUL, COM1...)
// gets a '_' after it. The scripts go in number order. When an earlier
// script, or reservedNames, has the name already (ignoring case), the script
// gets the name with "_N" after it, where N is its number (and "_2", "_3"...
// after that in the rare case that the suffixed name is taken too). A name
// in ownedNames is taken for every script but its owner (for example, the
// title of a file in src that a script has, so that a reset does not give
// another script that file). Two names of ownedNames that differ only in
// case are one name: the owner of the one that comes last in the map wins.
std::map<uint16_t, std::string> SuggestScriptNames(std::vector<ScriptObjectsForNaming> scripts,
    const std::vector<std::string> &reservedNames = std::vector<std::string>(),
    const std::map<std::string, uint16_t> &ownedNames = std::map<std::string, uint16_t>());

// True when the two names are one name, as Windows compares file names:
// without case, also for the letters outside ASCII.
bool SameScriptName(const std::string &a, const std::string &b);
