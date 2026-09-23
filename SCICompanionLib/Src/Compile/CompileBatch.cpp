#include "stdafx.h"
#include "CompileBatch.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceUtil.h"
#include "ScriptCatalog.h"
#include "CoreLog.h"
#include "format.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    std::string JoinPaths(const std::vector<std::string> &paths)
    {
        std::string text;
        for (const std::string &path : paths)
        {
            text += (text.empty() ? "" : ", ") + path;
        }
        return text;
    }

    // Into the game's package: not an output folder, not a dry run.
    bool WritesThePackage(const GameFolderHelper &helper, const CompileWriteOptions &write)
    {
        return (helper.GetResourceSaveLocation(write.saveTo) == ResourceSaveLocation::Package) && write.outDir.empty() && write.writeResources;
    }

    // The patch files that would hide the package writes of these scripts:
    // script N, heap N (SCI1.1), and vocab 996 and 997. Plan section 5: a
    // patch file hides the package copy, in the game and in SCI Companion. A
    // script's text is checked in Finish, when the batch knows which scripts
    // write one.
    sci::Result<std::vector<std::string>> ShadowingPatchesOf(const GameFolderHelper &helper, const std::vector<ScriptId> &scripts)
    {
        std::vector<ResourceKey> keys;
        for (const ScriptId &script : scripts)
        {
            keys.push_back({ ResourceType::Script, script.GetResourceNumber() });
            if (helper.Version.SeparateHeapResources)
            {
                keys.push_back({ ResourceType::Heap, script.GetResourceNumber() });
            }
        }
        keys.push_back({ ResourceType::Vocab, (uint16_t)VocabClassTable });
        keys.push_back({ ResourceType::Vocab, (uint16_t)VocabSelectorNames });
        return FindShadowingPatches(helper, keys);
    }

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
        const GameFolderHelper &helper = session.Helper();
        std::unique_ptr<CompileBatch> batch(new CompileBatch(session, std::move(scripts), options));
        batch->_toPackage = WritesThePackage(helper, options.write);
        if (batch->_toPackage && (options.shadows != ShadowPolicy::Ignore))
        {
            SCI_TRY_ASSIGN(std::vector<std::string> shadowing, ShadowingPatchesOf(helper, batch->_scripts));
            if (!shadowing.empty())
            {
                SCI_TRY(batch->_DecideAbout(shadowing, true));
            }
        }
        SCI_TRY(batch->_tables.TryLoad(session.ResourceMap()));
        batch->_headers = std::make_unique<PrecompiledHeaders>(session.ResourceMap());
        batch->_defer = std::make_unique<DeferResourceAppend>(session.ResourceMap());
        batch->_passDefer = std::make_unique<DeferResourceAppend>(session.ResourceMap());
        return std::move(batch);
    });
}

bool CompileBatch::Step(const std::atomic<bool> &abort, ICompileEvents &events)
{
    if (_finished || _report.cancelled || _report.stopped)
    {
        return false;
    }
    if (_next >= _scripts.size())
    {
        // The end of a pass. Another pass when a .sco file changed (plan
        // section 4.5): the scripts that use it are right only then.
        if (_scripts.empty() || !_passChangedObjectFile || (_pass >= _options.passes))
        {
            return false;
        }
        // The next pass writes every script again; the writes of this pass
        // go, so the commit holds the last pass.
        _passDefer.reset();
        _passDefer = std::make_unique<DeferResourceAppend>(_session.ResourceMap());
        _pass++;
        _next = 0;
        _passChangedObjectFile = false;
        _anyCompiled = false;
        _report.scripts.clear();
        int pass = _pass;
        sci::Status notified = sci::Guard("reporting a new pass", [&]() -> sci::Status
        {
            events.OnPassStart(pass);
            return sci::Ok();
        });
        if (!notified)
        {
            CoreLog(LogLevel::Warning, notified.error().ToString());
        }
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
    bool objectFileChanged = false;
    outcome.status = sci::Guard(fmt::format("compiling {0}", script.GetFileNameOrig()), [&]() -> sci::Status
    {
        events.OnScriptStart(index, _scripts.size(), script);
        CompileResults results(log, _session.Version());
        sci::Status compiled = CompileScriptFile(_session, results, log, _tables, *_headers, script, _options.write);
        objectFileChanged = results.ObjectFileChanged();
        outcome.stats = results.Stats;
        returned = true;
        return compiled;
    });
    outcome.diagnostics = log.Results();
    _passChangedObjectFile = _passChangedObjectFile || objectFileChanged;
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

// What to do with patch files that would hide a package write: the policy,
// or the answer of options.askShadows (the answer is the policy from then
// on). Replace keeps the files, to move them after the commit.
sci::Status CompileBatch::_DecideAbout(const std::vector<std::string> &files, bool beforeTheCompile)
{
    bool asked = false;
    if ((_options.shadows == ShadowPolicy::Refuse) && _options.askShadows)
    {
        _options.shadows = _options.askShadows(files);
        asked = true;
    }
    if (_options.shadows == ShadowPolicy::Replace)
    {
        _shadowingPatches.insert(_shadowingPatches.end(), files.begin(), files.end());
        return sci::Ok();
    }
    if (_options.shadows == ShadowPolicy::Ignore)
    {
        return sci::Ok();
    }
    std::string text = "these patch files would hide the package copies of the compiled resources: " + JoinPaths(files) + ".";
    if (!beforeTheCompile)
    {
        text += " Nothing was written.";
    }
    if (asked)
    {
        return sci::Fail(sci::ErrorCode::Cancelled, text);
    }
    return sci::Fail(sci::ErrorCode::WriteRefused, text + " Move them aside, or replace them (--replace-patches).");
}

// Plan section 5: the batch checks the queued package writes again before
// the commit. A script's auto text is known only after its compile.
sci::Status CompileBatch::_CheckQueuedWrites()
{
    if (!_toPackage || (_options.shadows == ShadowPolicy::Ignore))
    {
        return sci::Ok();
    }
    std::vector<ResourceKey> keys;
    for (const ResourceBlob *queued : _defer->Pending())
    {
        if (queued->GetSourceFlags() == ResourceSourceFlags::ResourceMap)
        {
            keys.push_back({ queued->GetType(), (uint16_t)queued->GetNumber() });
        }
    }
    SCI_TRY_ASSIGN(std::vector<std::string> shadowing, FindShadowingPatches(_session.Helper(), keys));
    std::vector<std::string> added;
    for (const std::string &file : shadowing)
    {
        if (std::find(_shadowingPatches.begin(), _shadowingPatches.end(), file) == _shadowingPatches.end())
        {
            added.push_back(file);
        }
    }
    if (added.empty())
    {
        return sci::Ok();
    }
    return _DecideAbout(added, false);
}

// ShadowPolicy::Replace, after the package write: the patch files go to
// <game>\replaced-patches\<time>. A file that does not move is a warning; the
// package has the resources already.
void CompileBatch::_MoveShadowingPatches()
{
    if (_shadowingPatches.empty())
    {
        return;
    }
    const GameFolderHelper &helper = _session.Helper();
    SYSTEMTIME now;
    GetLocalTime(&now);
    fs::path folder = fs::path(helper.GameFolder) / "replaced-patches" /
        fmt::format("{0:04}{1:02}{2:02}-{3:02}{4:02}{5:02}", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::error_code ec;
    fs::create_directories(folder, ec);
    for (const std::string &file : _shadowingPatches)
    {
        fs::path target = folder / fs::path(file).filename();
        std::error_code moved;
        fs::rename(file, target, moved);
        if (moved)
        {
            _report.warnings.push_back(fmt::format("could not move the patch file {0}: {1}", file, moved.message()));
        }
        else
        {
            _report.movedPatches.push_back(file + " -> " + target.string());
        }
    }
}

// Plan section 5: the warnings of a patch-file write.
void CompileBatch::_AddWarnings()
{
    const GameFolderHelper &helper = _session.Helper();
    bool toPatchFiles = (helper.GetResourceSaveLocation(_options.write.saveTo) == ResourceSaveLocation::Patch) &&
        _options.write.outDir.empty() && _options.write.writeResources;
    if (!toPatchFiles)
    {
        return;
    }
    // A patch file with another name for a resource that the batch wrote:
    // SCI Companion can load either file.
    std::vector<ResourceKey> keys;
    for (const ScriptOutcome &outcome : _report.scripts)
    {
        if (outcome.status)
        {
            keys.push_back({ ResourceType::Script, outcome.number });
            if (helper.Version.SeparateHeapResources)
            {
                keys.push_back({ ResourceType::Heap, outcome.number });
            }
        }
    }
    sci::Result<std::vector<std::string>> files = FindShadowingPatches(helper, keys);
    if (files)
    {
        for (const std::string &file : *files)
        {
            int number = ResourceNumberFromFileName(fs::path(file).filename().string().c_str());
            std::string standard = (number < 0) ? std::string() : GetFileNameFor(ResourceType::Script, number, NoBase36, helper.Version);
            std::string standardHeap = (number < 0) ? std::string() : GetFileNameFor(ResourceType::Heap, number, NoBase36, helper.Version);
            std::string name = fs::path(file).filename().string();
            if ((_stricmp(name.c_str(), standard.c_str()) != 0) && (_stricmp(name.c_str(), standardHeap.c_str()) != 0))
            {
                _report.warnings.push_back(fmt::format("{0} is a patch file with another name for a compiled resource; SCI Companion can load either file", file));
            }
        }
    }
    // Patch tables in a package-mode game hide the GUI's later package saves
    // of the tables.
    if (_anyCompiled && (helper.GetResourceSaveLocation(ResourceSaveLocation::Default) == ResourceSaveLocation::Package) &&
        (_tables.Species().IsDirty() || _tables.Selectors().IsDirty()))
    {
        _report.warnings.push_back("the class and selector tables were written as patch files (996 and 997); in this game, which keeps its resources "
            "in the package, they hide the package copies that SCI Companion saves later");
    }
}

CompileReport CompileBatch::Finish()
{
    if (!_finished)
    {
        _finished = true;
        _report.passes = _pass;
        // The last pass joins the batch.
        sci::Status joined = sci::Guard("closing the last pass", [&]() -> sci::Status
        {
            return _passDefer->Commit();
        });
        // The table rule: save only when a script compiled (plan section 4.5).
        if (_anyCompiled)
        {
            _report.tables = sci::Guard("saving the class and selector tables", [&]() -> sci::Status
            {
                return _tables.Save(_session.ResourceMap(), _options.write);
            });
        }
        if (!joined)
        {
            _report.commit = joined;
            return _report;
        }
        _report.commit = sci::Guard("checking the queued writes for patch files", [&]() -> sci::Status
        {
            return _CheckQueuedWrites();
        });
        if (!_report.commit)
        {
            // Nothing is written: the destructor withdraws the queued writes.
            return _report;
        }
        _report.commit = sci::Guard("writing the compiled resources", [&]() -> sci::Status
        {
            return _defer->Commit();
        });
        if (_report.commit)
        {
            _MoveShadowingPatches();
            _AddWarnings();
        }
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
