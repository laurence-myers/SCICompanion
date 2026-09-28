#pragma once

// The decompile of scripts as one run (plan sections 3.3, 3.4, 4.4 and
// 6.5): the src folder, the names of every script, the batch with a status
// for each script, the stale scripts, the statistics, and the names in
// game.ini. The command line and the GUI's Decompile dialog use it.

#include "DecompileBatch.h"
#include "CompileInterfaces.h"
#include "Result.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class GameSession;
class GameFolderHelper;
class IDecompilerResults;

// The names of the scripts before the run (plan section 3.4). Missing and
// All need a session with a script-name map (GameSession::Open installs one).
enum class NameAssignment
{
    Missing,    // a compiled script with no name gets its derived name (rule 4)
    All,        // as Missing, then each script of the run gets its derived name (--reset-names; ResetScriptNames)
    None,       // the names stay as they are (the GUI names with game.ini)
};

// The names in game.ini after the run (--game-ini). The entries are the names
// of the scripts that a group of the run wrote; when game.ini has no [Script]
// entry, the names of every script (as the Decompile dialog gives them before
// its first run), but not those of a name conflict, and a script that a
// reset renamed and that no group gave an ok outcome (it failed, or no
// group reached it) keeps its name from before the reset (its files have
// that name). A name that game.ini has with another value is replaced
// (after a reset of the names, the GUI then finds the new files). The
// default name nNNN gets no entry.
enum class GameIniNames
{
    Update,     // write the entries when game.ini exists
    Create,     // the same, and create game.ini when it does not exist and an entry is needed
    None,       // never write game.ini
};

struct DecompileRunOptions
{
    DecompileOptions engine;
    NameAssignment names = NameAssignment::Missing;
    GameIniNames gameIni = GameIniNames::Update;
    // After a run on some of the scripts: decompile the stale scripts too,
    // and again until a group names no new global. A script of an earlier
    // group can be stale again.
    bool updateStale = false;
    // Decompile in memory and write nothing (--dry-run), but do the steps
    // of a run that writes: the stale scripts (read from the sources in
    // memory), the groups of updateStale (each starts from the main .sco
    // of the group before), and the list of the files that the run would
    // write, with main's .sco, game.ini and the src folder. A later script
    // of the run still reads the .sco files that are on disk, so a source
    // can differ a little from the source of a run that writes. Ignored
    // with an output.
    bool dryRun = false;
    // After an abort (or a batch that threw), find the scripts whose files
    // still use a global of the run by its old name (report.stale); the
    // check reads every source file. The Decompile dialog sets false: it
    // offers no stale script after a Cancel.
    bool staleAfterAbort = true;
};

struct DecompileStats
{
    int functions = 0;          // decompiled to source
    int fallbacks = 0;          // fell back to asm
    int functionBytes = 0;
    int fallbackBytes = 0;
};

struct DecompileOutcome
{
    uint16_t number = 0;
    std::string name;
    // Ok: the files of the script were written (or its source went to the
    // output). Else why the script failed; Cancelled when the run stopped
    // before it.
    sci::Status status;
};

struct DecompileReport
{
    // The scripts of the run in number order, then the new scripts of each
    // group of stale scripts that updateStale decompiled. A script that a
    // later group decompiles again has one outcome: the last one of a group
    // that reached it. A later group that stopped before it (an abort, or a
    // batch that threw) keeps the earlier outcome, written or failed.
    std::vector<DecompileOutcome> scripts;
    // The globals that the run named: (standard name, new name).
    std::vector<std::pair<std::string, std::string>> globalRenames;
    // The scripts that use a global of the run by its old name: without
    // updateStale, the scripts that the run did not decompile; after an
    // abort or a batch that threw (with no output), every script whose file
    // still uses one, also a script that the run wrote before it stopped.
    std::set<uint16_t> stale;
    DecompileStats stats;
    bool cancelled = false;
    // For example, the files that keep an old name after a reset of the
    // names, or a Decompiler.ini that could not be read. Each also went to
    // the results as a warning, when it was found.
    std::vector<std::string> warnings;
    // The write of main's .sco with the new global names.
    sci::Status mainObjectFile;
    // The write of the names into game.ini.
    sci::Status gameIni;
    // The batch of a group: Ok, or the error of a batch that threw (the
    // code of a sci::DataError, else Internal). The scripts that it did not
    // reach keep an earlier outcome, or get the error; the stale check of an
    // abort runs, and the report does not succeed.
    sci::Status batch;
    // The files that the run wrote, or with dryRun would write, other than
    // the .sc and .sco of each script in scripts: the decompiler files that
    // the run copies into the src folder, main's .sco with the new global
    // names, and game.ini. Empty with an output.
    std::vector<std::string> files;

    size_t WrittenCount() const;
    size_t FailedCount() const;
    // Every script was written, the run was not cancelled, and main's .sco,
    // game.ini and the batch are Ok.
    bool Succeeded() const;
};

// Decompiles the scripts. Fails only when the run cannot start (for example,
// the src folder cannot be made, or the compiled scripts cannot be read); the
// report has the status of each script. With output, the source of each
// script that decompiled goes to it at the end, in number order, and nothing
// is written: no .sc, no .sco, no src folder, no game.ini; the run finds no
// stale script (--stdout), and a reset of the names gives no warning about
// the old files. After an abort or a batch that threw, no source goes to the
// output (the source of a part of a run is not the source of a run), and a
// script that decompiled gets Cancelled or the error of the batch.
sci::Result<DecompileReport> RunDecompile(GameSession &session, const std::set<uint16_t> &scripts, const DecompileRunOptions &options,
    IDecompilerResults &results, IDecompileOutput *output = nullptr);

// Makes the src folder and, when src\Decompiler.ini does not exist, copies
// the files of decompilerFolder into it. It never overwrites a file, and it
// copies nothing when decompilerFolder does not exist.
sci::Status PrepareDecompileFolder(const GameFolderHelper &helper, const std::string &decompilerFolder);

// Plan section 4.4 (--game-ini): an entry in game.ini [Script] for each of
// the names that game.ini does not have, or has with another value (the
// default name nNNN needs none). Update writes only into a game.ini that
// exists; nothing else creates it (plan section 3.4), and Create creates it
// only when a name needs an entry.
sci::Status WriteScriptNamesToGameIni(const GameFolderHelper &helper, const std::map<uint16_t, std::string> &names, GameIniNames mode);

// The [Script] entries (key, name) that WriteScriptNamesToGameIni would
// write for the mode: none when the mode would not write game.ini (a dry
// run).
std::vector<std::pair<std::string, std::string>> GameIniEntriesToWrite(const GameFolderHelper &helper, const std::map<uint16_t, std::string> &names, GameIniNames mode);

struct ObjectFileOutcome
{
    uint16_t number = 0;
    std::string name;
    // Ok: the .sco file was written (or, with dryRun, made), or the script
    // was skipped. Compile: the source has syntax errors, or its public
    // block has errors (in diagnostics). Cancelled: the abort flag stopped
    // the run before this script. Else why the script failed; with dryRun,
    // Io when a .sco that would change cannot be opened for writing, as the
    // write would fail.
    sci::Status status;
    // The .sco file has new bytes: it was written, or with dryRun would be.
    // False when the file already has these bytes: nothing is written
    // (SaveSCOFile).
    bool changed = false;
    // Why the script was skipped: it has no source file, or the game has no
    // compiled script for it. Empty when it was not skipped.
    std::string skipped;
    // The .sco file that was written (or, with dryRun, would be).
    std::string path;
    std::vector<CompileResult> diagnostics;
};

struct ObjectFileOptions
{
    // Make each .sco, and write none (--dry-run).
    bool dryRun = false;
    // Set between two scripts: the rest are Cancelled (Ctrl+C).
    const std::atomic<bool> *abort = nullptr;
    // Before each script (the command line names it in its crash line).
    std::function<void(const ScriptId &script)> onScript;
};

// Plan section 4.6 (script sco): for each script, the .sco file from its
// source file (the path of the ScriptId) and the game's compiled script
// (the number of the ScriptId), with the code of the decompiler
// (SCOFromScriptAndCompiledScript). As the compiler does, the source gets
// its includes that are not headers (the locals of a .shp file), and each
// class gets its name in the source (a warning when the source and the
// compiled script have different numbers of classes). The public block is
// checked as the compiler checks it: a slot used twice, or a name with no
// class, instance or procedure in the source, fails the script, and so
// does a name of a procedure or instance of an include that is not a
// header, unless the include's own public block lists it (the compiler:
// "needs to be marked public"). A public block whose slots differ from the
// slots that the compiled script exports is a warning, also when the
// source has no public block. The .sco goes to
// src\<title of the ScriptId>.sco. It does not compile, and it changes no
// resource. Source from another tool gets the .sco files that a compile
// needs for each (use ...).
sci::Result<std::vector<ObjectFileOutcome>> GenerateObjectFiles(GameSession &session, const std::vector<ScriptId> &scripts,
    const ObjectFileOptions &options = ObjectFileOptions());
