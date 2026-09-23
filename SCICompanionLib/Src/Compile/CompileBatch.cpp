#include "stdafx.h"
#include "CompileBatch.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "CoreLog.h"
#include "format.h"

namespace
{
    size_t CountDiagnostics(const std::vector<CompileResult> &diagnostics, bool errors)
    {
        return (size_t)std::count_if(diagnostics.begin(), diagnostics.end(),
            [errors](const CompileResult &result) { return errors ? result.IsError() : result.IsWarning(); });
    }
}

size_t CompileReport::CompiledCount() const
{
    return (size_t)std::count_if(scripts.begin(), scripts.end(), [](const ScriptOutcome &outcome) { return outcome.status.has_value(); });
}

size_t CompileReport::FailedCount() const
{
    return scripts.size() - CompiledCount();
}

size_t CompileReport::ErrorCount() const
{
    size_t count = 0;
    for (const ScriptOutcome &outcome : scripts)
    {
        count += CountDiagnostics(outcome.diagnostics, true);
    }
    return count;
}

size_t CompileReport::WarningCount() const
{
    size_t count = 0;
    for (const ScriptOutcome &outcome : scripts)
    {
        count += CountDiagnostics(outcome.diagnostics, false);
    }
    return count;
}

bool CompileReport::Succeeded() const
{
    return !cancelled && !stopped && (FailedCount() == 0) && tables.has_value() && commit.has_value();
}

CompileBatch::CompileBatch(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options) :
    _session(session), _scripts(std::move(scripts)), _options(options)
{
}

// The DeferResourceAppend member withdraws the queued writes of a batch that
// was not finished.
CompileBatch::~CompileBatch() = default;

sci::Result<std::unique_ptr<CompileBatch>> CompileBatch::Start(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options)
{
    return sci::Guard("starting the compile", [&]() -> sci::Result<std::unique_ptr<CompileBatch>>
    {
        if (options.write.saveTo == ResourceSaveLocation::Package)
        {
            if (!options.write.outDir.empty())
            {
                return sci::Fail(sci::ErrorCode::Usage, "an output folder takes patch files, not the package");
            }
            if (session.Helper().GetResourceSaveLocation(ResourceSaveLocation::Default) == ResourceSaveLocation::Patch)
            {
                return sci::Fail(sci::ErrorCode::WriteRefused,
                    "this game keeps its resources in patch files (SaveToPatchFiles in game.ini), so SCI Companion does not read its package; compile to patch files");
            }
        }
        std::unique_ptr<CompileBatch> batch(new CompileBatch(session, std::move(scripts), options));
        SCI_TRY(batch->_tables.TryLoad(session.ResourceMap()));
        batch->_headers = std::make_unique<PrecompiledHeaders>(session.ResourceMap());
        batch->_defer = std::make_unique<DeferResourceAppend>(session.ResourceMap());
        return std::move(batch);
    });
}

bool CompileBatch::Step(const std::atomic<bool> &abort, ICompileEvents &events)
{
    if (_finished || _report.cancelled || _report.stopped || (_next >= _scripts.size()))
    {
        return false;
    }
    if (abort.load())
    {
        _report.cancelled = true;
        return false;
    }

    size_t index = _next++;
    ScriptId script = _scripts[index];
    ScriptOutcome outcome;
    outcome.number = script.GetResourceNumber();
    outcome.name = script.GetTitle();
    CompileLog log;
    bool returned = false;
    outcome.status = sci::Guard(fmt::format("compiling {0}", script.GetFileNameOrig()), [&]() -> sci::Status
    {
        events.OnScriptStart(index, _scripts.size(), script);
        CompileResults results(log, _session.Version());
        sci::Status compiled = CompileScriptFile(_session, results, log, _tables, *_headers, script, _options.write);
        returned = true;
        return compiled;
    });
    outcome.diagnostics = log.Results();
    if (outcome.status)
    {
        _anyCompiled = true;
    }
    else
    {
        if (!returned)
        {
            // An exception escaped the compile: the log does not have it.
            outcome.diagnostics.push_back(CompileResult(outcome.status.error().ToString(), CompileResult::CRT_Error));
        }
        _report.stopped = _options.failFast;
    }

    sci::Status reported = sci::Guard("reporting a compiled script", [&]() -> sci::Status
    {
        events.OnScriptDone(outcome);
        return sci::Ok();
    });
    if (!reported)
    {
        CoreLog(LogLevel::Warning, reported.error().ToString());
    }
    _report.scripts.push_back(std::move(outcome));
    return true;
}

CompileReport CompileBatch::Finish()
{
    if (!_finished)
    {
        _finished = true;
        // The table rule: save only when a script compiled (plan section 4.5).
        if (_anyCompiled)
        {
            _report.tables = sci::Guard("saving the class and selector tables", [&]() -> sci::Status
            {
                return _tables.Save(_session.ResourceMap(), _options.write);
            });
        }
        _report.commit = sci::Guard("writing the compiled resources", [&]() -> sci::Status
        {
            return _defer->Commit();
        });
    }
    return _report;
}

sci::Result<CompileReport> CompileScripts(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options,
    const std::atomic<bool> &abort, ICompileEvents &events)
{
    SCI_TRY_ASSIGN(std::unique_ptr<CompileBatch> batch, CompileBatch::Start(session, std::move(scripts), options));
    while (batch->Step(abort, events))
    {
    }
    return batch->Finish();
}
