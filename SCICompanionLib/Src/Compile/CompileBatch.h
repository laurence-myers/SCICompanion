#pragma once

// The compile of several scripts as one batch (plan step S2; plan sections
// 3.3, 4.5, 5 and 6.5): the tables and headers load once, each script gets
// its own log and its own status inside an exception boundary, the tables
// are saved once when a script compiled, and the resource writes of the
// whole batch are one commit.

#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileWrite.h"
#include "Result.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class GameSession;
class DeferResourceAppend;

// What a package write does about patch files that would hide it (plan
// section 5: SCI Companion and the game read a patch file first).
enum class ShadowPolicy
{
    Refuse,     // the batch does not start, or does not commit (WriteRefused)
    Replace,    // after the commit, move them to the folder replaced-patches\<time> of the game
    Ignore,     // write the package anyway
};

struct CompileOptions
{
    // Where the output goes (plan step S1). Default: the game's setting.
    CompileWriteOptions write;
    // Stop after the first script that does not compile, in any pass (a
    // later pass could fix a script that uses a .sco of the same batch).
    bool failFast = false;
    // The largest number of passes (plan section 4.5): a pass that changes
    // no .sco file is the last. The commit holds the last pass.
    int passes = 1;
    ShadowPolicy shadows = ShadowPolicy::Refuse;
    // With ShadowPolicy::Refuse: when set, the batch asks it what to do with
    // the patch files that would hide a package write, at the start and
    // before the commit (for the files that it finds only then). Its answer
    // is the policy from then on. Refuse stops the batch with Cancelled, and
    // nothing is written. The GUI asks the user (plan step S2).
    std::function<ShadowPolicy(const std::vector<std::string> &files)> askShadows;
};

// The result of one script of a batch.
struct ScriptOutcome
{
    uint16_t number = 0;
    std::string name;
    // Ok; Compile (the errors are in the diagnostics); the error of a source
    // file that could not be read or an output that could not be written; or
    // the error of an exception (Internal, or the code of a DataError). A
    // script that fails writes no resource: its writes to the game are
    // withdrawn, and its files for an output folder are dropped (they wait
    // for the commit; review of 5f545221). Its .sco is its last write, so it
    // leaves no new .sco; a debug file that it wrote stays, and a debug file
    // that cannot be written is a warning.
    sci::Status status;
    std::vector<CompileResult> diagnostics;
    // The sizes of the compiled script (the GUI shows them).
    CompileStats stats;
};

struct CompileReport
{
    // The scripts that ran, in the order of the batch.
    std::vector<ScriptOutcome> scripts;
    // The save of vocab 996 and 997 into the batch (the commit writes
    // them). Ok when no script compiled (the tables are not saved then).
    sci::Status tables;
    // The one write of the queued resources. Refused when the tables could
    // not be saved: the compiled scripts need them.
    sci::Status commit;
    // With ShadowPolicy::Replace: Ok, or the first patch file that could not
    // move (it still hides the package write).
    sci::Status moves;
    // The abort flag stopped the batch.
    bool cancelled = false;
    // failFast stopped the batch after a script that failed.
    bool stopped = false;
    // The passes that ran.
    int passes = 0;
    // The last pass that options.passes allows changed a .sco file, so a
    // script that uses it can still be out of date.
    bool passLimit = false;
    // Patch files with another name for a written resource, and patch
    // tables that hide later package saves of the GUI.
    std::vector<std::string> warnings;
    // With ShadowPolicy::Replace: the patch files that the batch moved, as
    // "old -> new".
    std::vector<std::string> movedPatches;

    size_t CompiledCount() const;
    size_t FailedCount() const;
    // The error and warning diagnostics of all scripts.
    size_t ErrorCount() const;
    size_t WarningCount() const;
    // Every script compiled, and the tables, the commit and the moves are
    // Ok.
    bool Succeeded() const;
};

// Callbacks while a batch runs: the GUI shows its progress, the command line
// prints. They run on the thread that calls Step.
class ICompileEvents
{
public:
    virtual ~ICompileEvents() = default;
    // Before a script compiles, inside the exception boundary of the script:
    // an exception from here is the status of the script (Internal).
    virtual void OnScriptStart(size_t index, size_t count, const ScriptId &script) {}
    // After a script compiled or failed.
    virtual void OnScriptDone(const ScriptOutcome &outcome) {}
    // Before the second pass and each later one.
    virtual void OnPassStart(int pass) {}
};

class CompileBatch
{
public:
    // Loads the tables and the headers, checks the destination, and opens
    // one batch of resource writes. A script with no number gets the number
    // that its source declares. Fails when the batch cannot start: the
    // tables do not load; Usage for an output folder with the package, for
    // raw files with no output folder, or for the game folder as the output
    // folder; NotFound for an output folder that does not exist;
    // WriteRefused for the package of a game that keeps its resources in
    // patch files (SCI Companion does not read that package), or (with
    // ShadowPolicy::Refuse) when a patch file would hide the script, heap or
    // vocab 996 or 997 in the package; Cancelled when askShadows answered
    // Refuse.
    static sci::Result<std::unique_ptr<CompileBatch>> Start(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options);
    // A batch that was not finished withdraws its queued writes.
    ~CompileBatch();
    CompileBatch(const CompileBatch &) = delete;
    CompileBatch &operator=(const CompileBatch &) = delete;

    // Compiles the next script. False when no script is left, when the
    // abort flag is set (the report is then cancelled), or when failFast
    // stopped the batch. After the last script of a pass that changed a .sco
    // file, it starts the next pass (if options.passes allows): the writes of
    // the pass before are withdrawn, and the report has the new pass only.
    // An abort between two passes keeps the pass that finished. Each script
    // is a savepoint: a script that fails withdraws the writes that it
    // queued, so the commit never writes a script without its tables.
    bool Step(const std::atomic<bool> &abort, ICompileEvents &events);
    // Saves the tables when one or more scripts compiled, and commits the
    // queued writes, also after an abort, as the GUI's Cancel button does.
    // Before the commit, it checks the queued package writes again for patch
    // files that would hide them (a text of a script's auto text), and asks
    // askShadows about new files. With Replace, it moves only the patch
    // files that hide a resource that the commit wrote (a dry run moves
    // none). With an output folder, the commit writes the files of the
    // tables, then those of the scripts that compiled (WriteStagedOutputFiles).
    // Call it once.
    CompileReport Finish();

    size_t Count() const { return _scripts.size(); }
    size_t Done() const { return _next; }

private:
    CompileBatch(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options);

    GameSession &_session;
    std::vector<ScriptId> _scripts;
    CompileOptions _options;
    CompileTables _tables;
    std::unique_ptr<PrecompiledHeaders> _headers;
    // The batch, then the level of the current pass inside it (declared in
    // this order, so a pass level closes before the batch).
    std::unique_ptr<DeferResourceAppend> _defer;
    std::unique_ptr<DeferResourceAppend> _passDefer;
    CompileReport _report;
    size_t _next = 0;
    int _pass = 1;
    bool _passChangedObjectFile = false;
    bool _anyCompiled = false;
    bool _finished = false;
    // Writes into the game's package (not an output folder), also for a dry
    // run: a dry run checks the patch files as a real run does (review of
    // 5f545221).
    bool _toPackage = false;
    // With an output folder: the files of the scripts of this pass that
    // compiled. The commit writes the last pass.
    std::vector<StagedOutputFile> _passFiles;
    // The patch files that the policy or askShadows answered for, so the
    // check before the commit asks only about new ones.
    std::vector<std::string> _shadowingPatches;
    // The patch files that hide a queued package write, found before the
    // commit: the ones that Replace moves.
    std::vector<std::string> _hidingPatches;

    sci::Status _DecideAbout(const std::vector<std::string> &files, bool beforeTheCompile);
    sci::Status _CheckQueuedWrites();
    void _MoveShadowingPatches();
    void _AddWarnings();
};

// Start, Step until done, Finish. Fails only when the batch cannot start;
// the report has the status of each script.
sci::Result<CompileReport> CompileScripts(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options,
    const std::atomic<bool> &abort, ICompileEvents &events);
