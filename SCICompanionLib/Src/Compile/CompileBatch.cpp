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
#include "FileWrite.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

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

    // Into the game's package: not an output folder. Also a dry run, which
    // checks the patch files as a real run does (review of 5f545221: before,
    // a dry run passed where the real run was refused).
    bool WritesThePackage(const GameFolderHelper &helper, const CompileWriteOptions &write)
    {
        return (helper.GetResourceSaveLocation(write.saveTo) == ResourceSaveLocation::Package) && write.outDir.empty();
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

    // "Name (N)" for a script of the batch, else "script N".
    std::string ScriptLabel(const std::vector<ScriptId> &scripts, uint16_t number)
    {
        for (const ScriptId &script : scripts)
        {
            if (script.GetResourceNumber() == number)
            {
                return fmt::format("{0} ({1})", script.GetTitle(), number);
            }
        }
        return fmt::format("script {0}", number);
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
    return !cancelled && !stopped && (FailedCount() == 0) && tables.has_value() && commit.has_value() && moves.has_value() && objectFiles.has_value();
}

CompileBatch::CompileBatch(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options) :
    _session(session), _scripts(std::move(scripts)), _options(options), _objectFilesBefore(_scripts.size())
{
}

// The DeferResourceAppend member withdraws the queued writes of a batch that
// was not finished; the .sco files that it changed go back too.
CompileBatch::~CompileBatch()
{
    if (!_finished)
    {
        sci::Status restored = sci::Guard("putting back the .sco files", [&]() -> sci::Status
        {
            return _RestoreObjectFiles(false);
        });
        if (!restored)
        {
            CoreLog(LogLevel::Warning, restored.error().ToString());
        }
    }
}

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
        // Review of S2a: before, these options wrote into the game, or failed
        // each script after its .sco file was written.
        if (options.write.raw && options.write.outDir.empty())
        {
            return sci::Fail(sci::ErrorCode::Usage, "raw files need an output folder");
        }
        if (!options.write.outDir.empty())
        {
            std::error_code ec;
            if (!fs::is_directory(options.write.outDir, ec))
            {
                return sci::Fail(sci::ErrorCode::NotFound, "the output folder does not exist: " + options.write.outDir);
            }
            if (fs::equivalent(options.write.outDir, session.Helper().GameFolder, ec))
            {
                return sci::Fail(sci::ErrorCode::Usage, "the output folder is the game folder; to write into the game, give no output folder");
            }
        }
        const GameFolderHelper &helper = session.Helper();
        // A script with no number (a document opened from a file, or the
        // GUI's scan of src) gets the number that its source declares, so the
        // shadow check and the report have it (review of S2b).
        for (ScriptId &script : scripts)
        {
            uint16_t declared;
            if ((script.GetResourceNumber() == InvalidResourceNumber) && ReadDeclaredScriptNumber(helper, script.GetFullPath(), declared))
            {
                script.SetResourceNumber(declared);
            }
        }
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
            // A .sco file changed in the last pass that the options allow.
            _report.passLimit = !_scripts.empty() && _passChangedObjectFile;
            return false;
        }
        if (abort.load())
        {
            // An abort between two passes keeps the pass that finished
            // (review of S2b: before, the next pass withdrew it first).
            _report.cancelled = true;
            return false;
        }
        // The next pass writes every script again; the writes of this pass
        // go, so the commit holds the last pass.
        _passDefer.reset();
        _passDefer = std::make_unique<DeferResourceAppend>(_session.ResourceMap());
        _passFiles.clear();
        _passObjectFileUses.clear();
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
    // Outside the exception boundary: a compile that throws after its .sco
    // write still gives the change (review of 944de1df).
    std::unique_ptr<CompileResults> results;
    outcome.status = sci::Guard(fmt::format("compiling {0}", script.GetFileNameOrig()), [&]() -> sci::Status
    {
        events.OnScriptStart(index, _scripts.size(), script);
        // The .sco before its first write, for Finish (review of 944de1df).
        if (_options.write.writeObjectFile)
        {
            _CaptureObjectFile(index, script);
        }
        results = std::make_unique<CompileResults>(log, _session.Version());
        // A savepoint of the script (review of S1 and S2a): a script that
        // fails withdraws the resources that it queued, so the commit never
        // writes a compiled script without its tables. Its files for an
        // output folder wait too, and go with it (review of 5f545221).
        DeferResourceAppend scriptLevel(_session.ResourceMap());
        std::vector<StagedOutputFile> scriptFiles;
        // A dry run stages too: its list is what a real run would write, for
        // the patch-file check before the commit (review of 4247f34c).
        CompileWriteOptions write = _options.write;
        if (!write.outDir.empty() || !write.writeResources)
        {
            write.staged = &scriptFiles;
        }
        sci::Status compiled = CompileScriptFile(_session, *results, log, _tables, *_headers, script, write);
        outcome.stats = results->Stats;
        // The log has a failure of the compile, but not a failure to join the
        // pass.
        returned = true;
        if (compiled)
        {
            compiled = scriptLevel.Commit();
            returned = compiled.has_value();
        }
        if (compiled)
        {
            _passFiles.insert(_passFiles.end(), scriptFiles.begin(), scriptFiles.end());
        }
        return compiled;
    });
    outcome.diagnostics = log.Results();
    bool objectFileChanged = results && results->ObjectFileChanged();
    uint16_t compiledNumber = results ? results->GetScriptNumber() : InvalidResourceNumber;
    std::set<uint16_t> usedObjectFiles = results ? results->LoadedObjectFiles() : std::set<uint16_t>();
    if (compiledNumber != InvalidResourceNumber)
    {
        _compiledTitles[compiledNumber] = script.GetTitle();
    }
    _passChangedObjectFile = _passChangedObjectFile || objectFileChanged;
    if (objectFileChanged)
    {
        _changedObjectFiles.emplace(compiledNumber, index);
    }
    if (outcome.status)
    {
        _anyCompiled = true;
        if (compiledNumber != InvalidResourceNumber)
        {
            _passObjectFileUses[compiledNumber] = std::move(usedObjectFiles);
        }
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
// on). Replace keeps the files, to move them after the commit. A dry run
// does not ask: it cannot move them (review of 4247f34c: it asked "move?",
// and then moved nothing).
sci::Status CompileBatch::_DecideAbout(const std::vector<std::string> &files, bool beforeTheCompile)
{
    bool asked = false;
    if ((_options.shadows == ShadowPolicy::Refuse) && _options.askShadows && _options.write.writeResources)
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
        // The .sco and .scd files are written with each script.
        text += " No resource was written.";
    }
    if (asked)
    {
        return sci::Fail(sci::ErrorCode::Cancelled, text);
    }
    return sci::Fail(sci::ErrorCode::WriteRefused, text + " Move them aside, or replace them (--replace-patches).");
}

// A script of the commit compiled against the .sco files that it read. A new
// pass withdraws the writes of the pass before, but not its .sco files, so a
// script that failed in the last pass, or that did not run in it (an abort,
// or failFast), can have a new .sco and the old resources in the game. A
// script that used that .sco would then call a version of it that the game
// does not have (review of 4247f34c: in pass 2, Y failed against the new
// .sco of X, and X, compiled against the pass-1 .sco of Y, was written
// without Y). The commit then writes nothing.
sci::Status CompileBatch::_CheckObjectFileUses() const
{
    std::string uses;
    for (const auto &user : _passObjectFileUses)
    {
        std::string changed;
        for (uint16_t used : user.second)
        {
            if ((_changedObjectFiles.count(used) != 0) && (_passObjectFileUses.count(used) == 0))
            {
                changed += (changed.empty() ? "" : ", ") + _LabelOf(used);
            }
        }
        if (!changed.empty())
        {
            uses += (uses.empty() ? "" : "; ") + _LabelOf(user.first) + " uses " + changed;
        }
    }
    if (uses.empty())
    {
        return sci::Ok();
    }
    // Finish then puts back the .sco files (review of 944de1df: before,
    // the new ones stayed, and the next compile wrote the mix).
    return sci::Fail(sci::ErrorCode::WriteRefused, "no compiled resource was written, because these scripts compiled against the new .sco file "
        "of a script that failed in the last pass, or did not run in it, so the commit does not write it: " + uses +
        ". The .sco files are back as they were before the compile. Correct the scripts that failed, then compile again.");
}

// "Name (N)" for a compiled number: the script of the batch that compiled
// to it (review of 944de1df: a script that declares another number than its
// ScriptId was "script N"), else the ScriptId with that number.
std::string CompileBatch::_LabelOf(uint16_t number) const
{
    auto title = _compiledTitles.find(number);
    if (title != _compiledTitles.end())
    {
        return fmt::format("{0} ({1})", title->second, number);
    }
    return ScriptLabel(_scripts, number);
}

// Reads the .sco of the script as it is before the batch writes it. Only the
// first compile of the script reads it: a later pass finds the file of
// the pass before.
void CompileBatch::_CaptureObjectFile(size_t index, const ScriptId &script)
{
    ObjectFileBefore &before = _objectFilesBefore[index];
    if (before.captured)
    {
        return;
    }
    before.captured = true;
    before.path = _session.Helper().GetScriptObjectFileName(script.GetTitle());
    std::error_code ec;
    before.existed = fs::exists(before.path, ec) || ec;
    if (before.existed)
    {
        std::ifstream file(before.path, std::ios::binary);
        before.unreadable = !file;
        if (file)
        {
            before.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            before.unreadable = file.bad();
        }
    }
}

// Review of 944de1df: the compile writes a .sco before the commit, so a
// commit that is refused or fails, a dry run, or a script whose writes are
// not in the commit, left a new .sco that describes a script the game does
// not have, and the next compile used it. Each such .sco goes back to its
// bytes from before the batch, or goes when there was no file.
sci::Status CompileBatch::_RestoreObjectFiles(bool committed)
{
    std::vector<std::string> notRestored;
    for (const auto &changed : _changedObjectFiles)
    {
        if (committed && (_passObjectFileUses.find(changed.first) != _passObjectFileUses.end()))
        {
            // The commit wrote the script.
            continue;
        }
        const ObjectFileBefore &before = _objectFilesBefore[changed.second];
        if (!before.captured || before.unreadable)
        {
            notRestored.push_back(before.path + ": the file could not be read before the compile");
            continue;
        }
        sci::Status restored = sci::Ok();
        if (before.existed)
        {
            restored = WriteBytesToFile(before.path, before.bytes);
        }
        else
        {
            std::error_code ec;
            fs::remove(before.path, ec);
            if (ec)
            {
                restored = sci::Fail(sci::ErrorCode::Io, fmt::format("Removing {0}: {1}", before.path, ec.message()));
            }
        }
        if (restored)
        {
            _report.restoredObjectFiles.push_back(before.path);
        }
        else
        {
            notRestored.push_back(restored.error().ToString());
        }
    }
    if (notRestored.empty())
    {
        return sci::Ok();
    }
    std::string text;
    for (const std::string &file : notRestored)
    {
        text += (text.empty() ? "" : "; ") + file;
    }
    return sci::Fail(sci::ErrorCode::Io, "these .sco files could not go back to their state before the compile, so they describe scripts that the game "
        "does not have: " + text);
}

// Plan section 5: the batch checks the queued package writes again before
// the commit. A script's auto text is known only after its compile. A dry
// run queues nothing: its staged list has the writes of a real run (review
// of 4247f34c: before, a dry run passed where the real run was refused).
sci::Status CompileBatch::_CheckQueuedWrites(const std::vector<StagedOutputFile> &tableFiles)
{
    _hidingPatches.clear();
    if (!_toPackage || (_options.shadows == ShadowPolicy::Ignore))
    {
        return sci::Ok();
    }
    std::vector<ResourceKey> keys;
    if (_options.write.writeResources)
    {
        for (const ResourceBlob *queued : _defer->Pending())
        {
            if (queued->GetSourceFlags() == ResourceSourceFlags::ResourceMap)
            {
                keys.push_back({ queued->GetType(), (uint16_t)queued->GetNumber() });
            }
        }
    }
    else
    {
        auto addKeys = [&keys](const std::vector<StagedOutputFile> &files)
        {
            for (const StagedOutputFile &file : files)
            {
                keys.push_back({ file.type, file.number });
            }
        };
        addKeys(tableFiles);
        addKeys(_passFiles);
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
    if (!added.empty())
    {
        SCI_TRY(_DecideAbout(added, false));
    }
    // Replace moves only these: the files that hide a resource that the
    // commit writes (review of S2b: the list of the start also had the files
    // of scripts that failed, and of tables that did not change).
    _hidingPatches = shadowing;
    return sci::Ok();
}

// ShadowPolicy::Replace, after the package write: the patch files go to
// <game>\replaced-patches\<time>. The files that do not move are an error in
// report.moves (Io); the package has the resources already, so the commit
// stays.
void CompileBatch::_MoveShadowingPatches()
{
    if ((_options.shadows != ShadowPolicy::Replace) || _hidingPatches.empty())
    {
        return;
    }
    if (!_options.write.writeResources)
    {
        // A dry run writes nothing, so it moves nothing; it names the files
        // that a real run would move.
        _report.warnings.push_back("a real run would move these patch files, which would hide the package write, to the folder replaced-patches: " +
            JoinPaths(_hidingPatches));
        return;
    }
    const GameFolderHelper &helper = _session.Helper();
    SYSTEMTIME now;
    GetLocalTime(&now);
    fs::path base = fs::path(helper.GameFolder) / "replaced-patches" /
        fmt::format("{0:04}{1:02}{2:02}-{3:02}{4:02}{5:02}", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    // A new folder for each batch: an earlier batch in the same second keeps
    // its files (review of S2b).
    std::error_code ec;
    fs::path folder = base;
    for (int n = 2; fs::exists(folder, ec); n++)
    {
        folder = fs::path(base.string() + fmt::format("-{0}", n));
    }
    fs::create_directories(folder, ec);
    std::vector<std::string> notMoved;
    for (const std::string &file : _hidingPatches)
    {
        fs::path target = folder / fs::path(file).filename();
        std::error_code moved;
        fs::rename(file, target, moved);
        if (moved)
        {
            notMoved.push_back(fmt::format("{0} ({1})", file, moved.message()));
        }
        else
        {
            _report.movedPatches.push_back(file + " -> " + target.string());
        }
    }
    if (!notMoved.empty())
    {
        // Every file, in one error (review of 5f545221: before, only the
        // first file was in the error).
        std::string text;
        for (const std::string &file : notMoved)
        {
            text += (text.empty() ? "" : "; ") + file;
        }
        _report.moves = sci::Fail(sci::ErrorCode::Io, "could not move these patch files, which still hide the package write: " + text);
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
        _Commit();
        // Review of 944de1df: the .sco files of the scripts that the commit
        // does not write go back (all of them without a commit).
        bool committed = _report.commit.has_value() && _options.write.writeResources;
        _report.objectFiles = sci::Guard("putting back the .sco files", [&]() -> sci::Status
        {
            return _RestoreObjectFiles(committed);
        });
        // An abort or failFast after a pass that changed a .sco: a script of
        // the commit can use an old one (review of 944de1df).
        if ((_report.cancelled || _report.stopped) && _passChangedObjectFile)
        {
            _report.passLimit = true;
        }
    }
    return _report;
}

// The commit of Finish: the tables, the checks, and the write.
void CompileBatch::_Commit()
{
    {
        // The last pass joins the batch.
        sci::Status joined = sci::Guard("closing the last pass", [&]() -> sci::Status
        {
            return _passDefer->Commit();
        });
        // The table rule: save only when a script compiled (plan section 4.5).
        // With an output folder, the table files wait for the commit too; a
        // dry run lists them for its patch-file check.
        std::vector<StagedOutputFile> tableFiles;
        if (_anyCompiled)
        {
            CompileWriteOptions write = _options.write;
            if (!write.outDir.empty() || !write.writeResources)
            {
                write.staged = &tableFiles;
            }
            _report.tables = sci::Guard("saving the class and selector tables", [&]() -> sci::Status
            {
                return _tables.Save(_session.ResourceMap(), write);
            });
        }
        if (!joined)
        {
            _report.commit = joined;
            return;
        }
        if (!_report.tables)
        {
            // The compiled scripts need their tables (review of S2a: before,
            // the commit wrote them without the tables).
            sci::Error error = _report.tables.error();
            error.context.push_back("no compiled resource was written, because the class and selector tables could not be saved");
            _report.commit = sci::Fail(error);
            return;
        }
        _report.commit = sci::Guard("checking the .sco files that the scripts used", [&]() -> sci::Status
        {
            return _CheckObjectFileUses();
        });
        if (!_report.commit)
        {
            return;
        }
        _report.commit = sci::Guard("checking the queued writes for patch files", [&]() -> sci::Status
        {
            return _CheckQueuedWrites(tableFiles);
        });
        if (!_report.commit)
        {
            // Nothing is written: the destructor withdraws the queued writes.
            return;
        }
        // The GUI's timers show the time of the write only (review of S2c:
        // before, they held the time of the question too).
        g_compileIOTimer.Start();
        g_compileAppendTimer.Start();
        _report.commit = sci::Guard("writing the compiled resources", [&]() -> sci::Status
        {
            SCI_TRY(_defer->Commit());
            if (_options.write.outDir.empty())
            {
                return sci::Ok();
            }
            // The tables first: a script without its tables is worse than
            // tables without the script.
            std::vector<StagedOutputFile> files = tableFiles;
            files.insert(files.end(), _passFiles.begin(), _passFiles.end());
            if (!_options.write.writeResources)
            {
                // A dry run checks the files as the write would (review of
                // 944de1df: before, it checked nothing here).
                return CheckStagedOutputFiles(_session.Helper(), _options.write, files);
            }
            return WriteStagedOutputFiles(_session.Helper(), _options.write, files);
        });
        g_compileAppendTimer.Stop();
        g_compileIOTimer.Stop();
        if (_report.commit)
        {
            _MoveShadowingPatches();
            _AddWarnings();
        }
    }
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
