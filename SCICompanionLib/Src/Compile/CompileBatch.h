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
#include <memory>
#include <string>
#include <vector>

class GameSession;
class DeferResourceAppend;

struct CompileOptions
{
    // Where the output goes (plan step S1). Default: the game's setting.
    CompileWriteOptions write;
    // Stop after the first script that does not compile.
    bool failFast = false;
};

// The result of one script of a batch.
struct ScriptOutcome
{
    uint16_t number = 0;
    std::string name;
    // Ok; Compile (the errors are in the diagnostics); the error of a source
    // file that could not be read or an output that could not be written; or
    // Internal (an exception in the engine).
    sci::Status status;
    std::vector<CompileResult> diagnostics;
};

struct CompileReport
{
    // The scripts that ran, in the order of the batch.
    std::vector<ScriptOutcome> scripts;
    // The save of vocab 996 and 997. Ok when no script compiled (the tables
    // are not saved then).
    sci::Status tables;
    // The one write of the queued resources.
    sci::Status commit;
    // The abort flag stopped the batch.
    bool cancelled = false;
    // failFast stopped the batch after a script that failed.
    bool stopped = false;

    size_t CompiledCount() const;
    size_t FailedCount() const;
    // The error and warning diagnostics of all scripts.
    size_t ErrorCount() const;
    size_t WarningCount() const;
    // Every script compiled, and the tables and the commit are Ok.
    bool Succeeded() const;
};

// Callbacks while a batch runs: the GUI shows its progress, the command line
// prints. They run on the thread that calls Step.
class ICompileEvents
{
public:
    virtual ~ICompileEvents() = default;
    // Before a script compiles, inside the exception boundary of the script:
    // an exception from here is an Internal status of the script.
    virtual void OnScriptStart(size_t index, size_t count, const ScriptId &script) {}
    // After a script compiled or failed.
    virtual void OnScriptDone(const ScriptOutcome &outcome) {}
};

class CompileBatch
{
public:
    // Loads the tables and the headers, checks the destination, and opens
    // one batch of resource writes. Fails when the batch cannot start: the
    // tables do not load; Usage for an output folder with the package;
    // WriteRefused for the package of a game that keeps its resources in
    // patch files (SCI Companion does not read that package).
    static sci::Result<std::unique_ptr<CompileBatch>> Start(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options);
    // A batch that was not finished withdraws its queued writes.
    ~CompileBatch();
    CompileBatch(const CompileBatch &) = delete;
    CompileBatch &operator=(const CompileBatch &) = delete;

    // Compiles the next script. False when no script is left, when the
    // abort flag is set (the report is then cancelled), or when failFast
    // stopped the batch.
    bool Step(const std::atomic<bool> &abort, ICompileEvents &events);
    // Saves the tables when one or more scripts compiled, and commits the
    // queued writes, also after an abort, as the GUI's Cancel button does.
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
    std::unique_ptr<DeferResourceAppend> _defer;
    CompileReport _report;
    size_t _next = 0;
    bool _anyCompiled = false;
    bool _finished = false;
};

// Start, Step until done, Finish. Fails only when the batch cannot start;
// the report has the status of each script.
sci::Result<CompileReport> CompileScripts(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options,
    const std::atomic<bool> &abort, ICompileEvents &events);
