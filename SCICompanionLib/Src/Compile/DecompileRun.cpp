#include "stdafx.h"
#include "DecompileRun.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ResourceMap.h"
#include "CompiledScript.h"
#include "Vocab99x.h"
#include "ScriptOMAll.h" // DecompilerConfig.h uses the sci:: node types declared here
#include "DecompilerConfig.h"
#include "DecompilerResults.h"
#include "ScriptCatalog.h"
#include "CompileContext.h"
#include "CrystalScriptStream.h"
#include "SyntaxParser.h"
#include "ScriptText.h"
#include "SCO.h"
#include "FileWrite.h"
#include "format.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
    // Counts the statistics of the run, and passes everything on.
    class CountingResults : public IDecompilerResults
    {
    public:
        CountingResults(IDecompilerResults &inner, DecompileStats &stats) : _inner(inner), _stats(stats) {}

        void AddResult(DecompilerResultType type, const std::string &message) override
        {
            _inner.AddResult(type, message);
        }
        bool IsAborted() override
        {
            return _inner.IsAborted();
        }
        void InformStats(bool functionSuccessful, int byteCount) override
        {
            if (functionSuccessful)
            {
                _stats.functions++;
                _stats.functionBytes += byteCount;
            }
            else
            {
                _stats.fallbacks++;
                _stats.fallbackBytes += byteCount;
            }
            _inner.InformStats(functionSuccessful, byteCount);
        }
        void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &renames) override
        {
            _inner.SetGlobalVarsUpdated(renames);
        }

    private:
        IDecompilerResults &_inner;
        DecompileStats &_stats;
    };

    // The last source of each script: pass 2 of the batch can give a script
    // again. For a dry run, the batch checks the writes.
    class LastSources : public IDecompileOutput
    {
    public:
        explicit LastSources(bool dryRun) : _dryRun(dryRun) {}

        void OnSource(uint16_t scriptNumber, const std::string &source) override
        {
            sources[scriptNumber] = source;
        }
        bool ChecksTheWrites() const override
        {
            return _dryRun;
        }

        std::map<uint16_t, std::string> sources;

    private:
        bool _dryRun;
    };

    // The includes of a script, as the compiler reads them
    // (PrecompiledHeaders::Update): an include that is not a header (.sh and
    // .shm are headers; a .shp polygon file has locals) is merged into the
    // script, and the includes of a header are read too. The .sco then has
    // the locals of the compiler's .sco (for example, rm110 of the SCI1.1
    // template has the 11 locals of 110.shp).
    sci::Status MergeIncludedScripts(sci::Script &script, CResourceMap &resourceMap, const SCIVersion &version, CompileLog &log)
    {
        std::set<std::string> seen;
        std::set<std::string> toRead(script.GetIncludes().begin(), script.GetIncludes().end());
        while (!toRead.empty())
        {
            std::set<std::string> next;
            for (const std::string &name : toRead)
            {
                if (!seen.insert(name).second)
                {
                    continue;
                }
                std::string includePath = resourceMap.GetIncludePath(name);
                if (includePath.empty())
                {
                    // The include is not there: the error names it (an open
                    // of the empty path would not).
                    return sci::Fail(sci::ErrorCode::NotFound, "the include " + name + " is in neither the include folder nor src");
                }
                ScriptId includeId(includePath);
                SCI_TRY_ASSIGN(ScriptText text, LoadScriptText(includeId.GetFullPath()));
                CScriptStreamLimiter limiter(text);
                CCrystalScriptStream stream(&limiter);
                sci::Script included(includeId);
                if (!SyntaxParser_Parse(included, stream, PreProcessorDefinesFromSCIVersion(version), &log))
                {
                    sci::ErrorLocation where;
                    where.file = includeId.GetFullPath();
                    return sci::Fail(sci::ErrorCode::Compile, "the include " + name + " has syntax errors", where);
                }
                if (included.IsHeader())
                {
                    next.insert(included.GetIncludes().begin(), included.GetIncludes().end());
                }
                else
                {
                    MergeScripts(script, included);
                }
            }
            toRead.clear();
            for (const std::string &name : next)
            {
                if (seen.find(name) == seen.end())
                {
                    toRead.insert(name);
                }
            }
        }
        return sci::Ok();
    }

    // A diagnostic at a node of the source, in the form of the compiler's
    // (CompileContext::_ReportThing): a 1-based line, and the raw message.
    CompileResult DiagnosticAt(const ScriptId &script, const ISourceCodePosition &position, bool error, const std::string &message)
    {
        int line = position.GetLineNumber() + 1;
        CompileResult result(fmt::format("{0}: ({1}) {2}  Line: {3}, col: {4}", error ? "Error" : "Warning", script.GetFileNameOrig(), message, line, position.GetColumnNumber()),
            script, line, position.GetColumnNumber(), error ? CompileResult::CRT_Error : CompileResult::CRT_Warning);
        result.SetRawMessage(message);
        return result;
    }

    std::string SlotsText(const std::set<int> &slots)
    {
        std::string text;
        for (int slot : slots)
        {
            text += (text.empty() ? "" : " ") + std::to_string(slot);
        }
        return text.empty() ? std::string("none") : text;
    }

    // The public block, as the compiler checks it (Script::PreScan in
    // Compile.cpp): a slot used twice, or a name that no class, instance or
    // procedure of the source has, is an error (the .sco builder would take
    // both, and the compile of the script would then fail). So is a name
    // whose definition is not public: PostProcessScript makes a definition
    // public from the public block of its own file, so a procedure or
    // instance of an include that is not a header is public only when the
    // include lists it. A block whose slots differ from the slots that the
    // compiled script exports is a warning (for example, the SCI1.1
    // template's Main and DebugHandler export slots that their sources do
    // not list), also when the source has no block (the .sco then has no
    // export).
    sci::Status CheckPublicBlock(const sci::Script &parsed, const ScriptId &script, uint16_t number, const CompiledScript &compiled, std::vector<CompileResult> &diagnostics)
    {
        std::set<int> slots;
        bool errors = false;
        for (const auto &entry : parsed.GetExports())
        {
            if (!slots.insert(entry->Slot).second)
            {
                diagnostics.push_back(DiagnosticAt(script, *entry, true, fmt::format("Export slot {0} has already been used.", entry->Slot)));
                errors = true;
            }
            auto object = std::find_if(parsed.GetClasses().begin(), parsed.GetClasses().end(), [&](const auto &candidate) { return candidate->GetName() == entry->Name; });
            auto procedure = std::find_if(parsed.GetProcedures().begin(), parsed.GetProcedures().end(), [&](const auto &candidate) { return candidate->GetName() == entry->Name; });
            if ((object == parsed.GetClasses().end()) && (procedure == parsed.GetProcedures().end()))
            {
                diagnostics.push_back(DiagnosticAt(script, *entry, true, fmt::format("Unknown export {0} in slot {1}.", entry->Name, entry->Slot)));
                errors = true;
            }
            else if ((object != parsed.GetClasses().end()) ? !(*object)->IsPublic() : !(*procedure)->IsPublic())
            {
                // The compiler gives the position of the definition, which can
                // be in the include; this one is in the script.
                diagnostics.push_back(DiagnosticAt(script, *entry, true, fmt::format("{0} needs to be marked public in order to be exported.", entry->Name)));
                errors = true;
            }
        }
        if (errors)
        {
            sci::ErrorLocation where;
            where.file = script.GetFullPath();
            return sci::Fail(sci::ErrorCode::Compile, "the public block has errors", where);
        }
        std::set<int> compiledSlots;
        std::vector<uint16_t> exports = compiled.GetExports();
        for (size_t slot = 0; slot < exports.size(); slot++)
        {
            if (exports[slot] != 0)
            {
                compiledSlots.insert((int)slot);
            }
        }
        if (compiledSlots != slots)
        {
            if (parsed.GetExports().empty())
            {
                // No entry to point at: the start of the file.
                std::string message = fmt::format("The source has no public block, and compiled script {0} exports the slots {1}.", number, SlotsText(compiledSlots));
                CompileResult result(fmt::format("Warning: ({0}) {1}  Line: 1, col: 0", script.GetFileNameOrig(), message), script, 1, 0, CompileResult::CRT_Warning);
                result.SetRawMessage(message);
                diagnostics.push_back(result);
            }
            else
            {
                diagnostics.push_back(DiagnosticAt(script, *parsed.GetExports().front(), false,
                    fmt::format("The public block has the slots {0}, and compiled script {1} exports the slots {2}.", SlotsText(slots), number, SlotsText(compiledSlots))));
            }
        }
        return sci::Ok();
    }

    // The decompiler files that the preparation of src copies: (from, to).
    // None when src\Decompiler.ini exists, or when the folder of the data
    // is not there. It never lists a file that src has.
    sci::Result<std::vector<std::pair<fs::path, fs::path>>> DecompilerFilesToCopy(const fs::path &src, const std::string &decompilerFolder)
    {
        std::vector<std::pair<fs::path, fs::path>> copies;
        std::error_code ec;
        if (fs::exists(src / "Decompiler.ini", ec) || !fs::is_directory(decompilerFolder, ec))
        {
            return copies;
        }
        for (fs::directory_iterator it(decompilerFolder, ec), end; !ec && (it != end); it.increment(ec))
        {
            std::error_code fileError;
            if (!it->is_regular_file(fileError))
            {
                continue;
            }
            fs::path target = src / it->path().filename();
            if (fs::exists(target, fileError))
            {
                // Never overwrite a file of the game.
                continue;
            }
            copies.emplace_back(it->path(), target);
        }
        if (ec)
        {
            return sci::Fail(sci::ErrorCode::Io, fmt::format("could not read the folder {0}: {1}", decompilerFolder, ec.message()));
        }
        return copies;
    }

    // A guard for the stale loop: each group names a global that no group
    // named before, so the loop ends; this stops only a loop that a bug made
    // endless.
    const int MaxStaleGroups = 100;
}

size_t DecompileReport::WrittenCount() const
{
    return (size_t)std::count_if(scripts.begin(), scripts.end(), [](const DecompileOutcome &outcome) { return outcome.status.has_value(); });
}

size_t DecompileReport::FailedCount() const
{
    return scripts.size() - WrittenCount();
}

bool DecompileReport::Succeeded() const
{
    return !cancelled && (FailedCount() == 0) && mainObjectFile.has_value() && gameIni.has_value() && batch.has_value();
}

sci::Status PrepareDecompileFolder(const GameFolderHelper &helper, const std::string &decompilerFolder)
{
    return sci::Guard("preparing the src folder", [&]() -> sci::Status
    {
        fs::path src = helper.GetSrcFolder();
        std::error_code ec;
        fs::create_directories(src, ec);
        if (ec)
        {
            return sci::Fail(sci::ErrorCode::Io, fmt::format("could not make the folder {0}: {1}", src.string(), ec.message()));
        }
        // A plain copy of the files, with no shell and no window.
        SCI_TRY_ASSIGN(auto copies, DecompilerFilesToCopy(src, decompilerFolder));
        for (const auto &copy : copies)
        {
            std::error_code fileError;
            fs::copy_file(copy.first, copy.second, fs::copy_options::none, fileError);
            if (fileError)
            {
                return sci::Fail(sci::ErrorCode::Io, fmt::format("could not copy {0} to {1}: {2}", copy.first.string(), copy.second.string(), fileError.message()));
            }
        }
        return sci::Ok();
    });
}


std::vector<std::pair<std::string, std::string>> GameIniEntriesToWrite(const GameFolderHelper &helper, const std::map<uint16_t, std::string> &names, GameIniNames mode)
{
    std::vector<std::pair<std::string, std::string>> entries;
    std::error_code ec;
    if ((mode == GameIniNames::None) || ((mode == GameIniNames::Update) && !fs::exists(helper.GetGameIniFileName(), ec)))
    {
        // Plan section 3.4: nothing creates game.ini, except Create.
        return entries;
    }
    for (const auto &name : names)
    {
        std::string key = default_reskey(name.first, NoBase36);
        if (name.second.empty() || (_stricmp(key.c_str(), name.second.c_str()) == 0))
        {
            // The default name needs no entry.
            continue;
        }
        if (helper.GetIniString("Script", key) != name.second)
        {
            entries.emplace_back(key, name.second);
        }
    }
    return entries;
}

sci::Status WriteScriptNamesToGameIni(const GameFolderHelper &helper, const std::map<uint16_t, std::string> &names, GameIniNames mode)
{
    return sci::Guard("writing the script names into game.ini", [&]() -> sci::Status
    {
        std::string iniFile = helper.GetGameIniFileName();
        for (const auto &entry : GameIniEntriesToWrite(helper, names, mode))
        {
            if (!WritePrivateProfileString("Script", entry.first.c_str(), entry.second.c_str(), iniFile.c_str()))
            {
                return sci::Fail(sci::FromWin32(GetLastError(), "writing " + iniFile));
            }
        }
        return sci::Ok();
    });
}

sci::Result<DecompileReport> RunDecompile(GameSession &session, const std::set<uint16_t> &scripts, const DecompileRunOptions &options,
    IDecompilerResults &results, IDecompileOutput *output)
{
    return sci::Guard("decompiling", [&]() -> sci::Result<DecompileReport>
    {
        const GameFolderHelper &helper = session.Helper();
        CResourceMap &resourceMap = session.ResourceMap();
        DecompileReport report;
        // With an output (--stdout), the run only decompiles. A dry run
        // decompiles in memory too, but does the other steps.
        bool dryRun = options.dryRun && !output;
        bool inMemory = dryRun || output;

        // With an output, nothing is written: not even the src folder. The
        // report lists the files that the preparation copies (a dry run:
        // would copy).
        if (!output)
        {
            sci::Result<std::vector<std::pair<fs::path, fs::path>>> copies = DecompilerFilesToCopy(helper.GetSrcFolder(), resourceMap.GetDecompilerFolder());
            if (dryRun)
            {
                // The check of the preparation: it cannot make the folder
                // where a file has its name.
                std::error_code ec;
                if (fs::exists(helper.GetSrcFolder(), ec) && !fs::is_directory(helper.GetSrcFolder(), ec))
                {
                    return sci::Fail(sci::ErrorCode::Io, fmt::format("could not make the folder {0}: a file has this name", helper.GetSrcFolder()));
                }
                SCI_TRY(copies);
            }
            else
            {
                SCI_TRY(PrepareDecompileFolder(helper, resourceMap.GetDecompilerFolder()));
            }
            if (copies)
            {
                for (const auto &copy : *copies)
                {
                    report.files.push_back(copy.second.string());
                }
            }
        }

        // Plan section 3.4: every script needs its name first, because the
        // decompiler writes a (use Name) line for each script that it uses.
        // Missing and All change the session's names (the command line); the
        // GUI passes None, and names with game.ini. All resets only the
        // names of the chosen scripts.
        if ((options.names == NameAssignment::Missing) || (options.names == NameAssignment::All))
        {
            SCI_TRY(AddDerivedScriptNames(session));
        }
        // The names of the chosen scripts before a reset: game.ini keeps them
        // for a script that no group writes.
        std::map<uint16_t, std::string> namesBeforeReset;
        if (options.names == NameAssignment::All)
        {
            for (uint16_t number : scripts)
            {
                namesBeforeReset[number] = helper.GetScriptTitle(number);
            }
            SCI_TRY_ASSIGN(std::vector<std::string> warnings, ResetScriptNames(session, scripts, dryRun));
            // With an output, no file is written, so the old files do not
            // matter.
            if (!output)
            {
                for (const std::string &warning : warnings)
                {
                    report.warnings.push_back(warning);
                    results.AddResult(DecompilerResultType::Warning, warning);
                }
            }
        }

        GlobalCompiledScriptLookups lookups;
        SCI_TRY(lookups.TryLoad(helper));
        // Prime the selector table: the batch reads it as const, and it caches
        // its names.
        uint16_t unused;
        lookups.GetSelectorTable().ReverseLookup("", unused);
        // The game's src\Decompiler.ini, else the one of the data folder: a run
        // that writes no src folder (--stdout) then gives the source that a
        // file run gives, not a source with the default settings (other
        // global and parameter names).
        std::string iniPath = helper.GetSrcFolder() + "\\Decompiler.ini";
        std::error_code ec;
        if (!fs::exists(iniPath, ec))
        {
            std::string dataIniPath = (fs::path(resourceMap.GetDecompilerFolder()) / "Decompiler.ini").string();
            if (fs::exists(dataIniPath, ec))
            {
                iniPath = dataIniPath;
            }
        }
        std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(resourceMap, lookups.GetSelectorTable(), iniPath);
        if (!config->error.empty())
        {
            std::string warning = "Decompiler.ini: " + config->error;
            report.warnings.push_back(warning);
            results.AddResult(DecompilerResultType::Warning, warning);
        }

        CountingResults counting(results, report.stats);
        LastSources sources(dryRun);
        // The outcome of each script in report.scripts: a script that a later
        // group decompiles again gets the outcome of that group, when the
        // group reached it.
        std::map<uint16_t, size_t> outcomeIndex;
        // Each script that a group wrote, with its name: game.ini gets these
        // names, also when a later group fails or stops before the script.
        std::map<uint16_t, std::string> writtenNames;
        std::set<std::pair<std::string, std::string>> knownRenames;
        // A dry run: the main .sco of the group before, which a run that
        // writes reads from the file.
        std::unique_ptr<CSCOFile> carriedMain;
        // A group wrote (a dry run: would write) main's .sco with the new names.
        bool mainWritten = false;
        std::set<uint16_t> toDo = scripts;
        for (int group = 1; !toDo.empty(); group++)
        {
            DecompileBatch batch(config.get(), lookups, resourceMap, counting, options.engine, inMemory ? &sources : nullptr);
            if (carriedMain)
            {
                batch.SetMainObjectFile(std::move(carriedMain));
            }
            // Each script has its own exception boundary in the batch, and so
            // has the batch; this one keeps the report of the scripts that were
            // written when the batch itself throws.
            sci::Status ran = sci::Guard("", [&]() -> sci::Status
            {
                return batch.Run(toDo);
            });
            for (uint16_t number : toDo)
            {
                DecompileOutcome outcome;
                outcome.number = number;
                outcome.name = helper.GetScriptTitle(number);
                outcome.objectFileChanged = (batch.GetChangedObjectFiles().find(number) != batch.GetChangedObjectFiles().end());
                // This group wrote the script, or it failed in this group.
                bool reached = true;
                auto failed = batch.GetFailedScripts().find(number);
                if (failed != batch.GetFailedScripts().end())
                {
                    outcome.status = sci::Fail(failed->second);
                }
                else if (batch.GetWrittenScripts().find(number) == batch.GetWrittenScripts().end())
                {
                    reached = false;
                    outcome.status = ran ? sci::Status(sci::Fail(sci::ErrorCode::Cancelled, "the run stopped before this script")) : ran;
                }
                if (outcome.status)
                {
                    writtenNames[number] = outcome.name;
                }
                auto index = outcomeIndex.find(number);
                if (index == outcomeIndex.end())
                {
                    outcomeIndex[number] = report.scripts.size();
                    report.scripts.push_back(std::move(outcome));
                }
                else if (!reached)
                {
                    // This group stopped before the script (an abort, or a batch
                    // that threw): it keeps the outcome of the earlier group,
                    // written or failed, and the stale check after an abort
                    // lists it. A Cancelled or the error of this batch does
                    // not replace that outcome.
                }
                else
                {
                    // A .sco that an earlier group changed stays changed.
                    outcome.objectFileChanged = outcome.objectFileChanged || report.scripts[index->second].objectFileChanged;
                    report.scripts[index->second] = std::move(outcome);
                }
            }
            if (report.mainObjectFile && !batch.GetMainObjectFileStatus())
            {
                report.mainObjectFile = batch.GetMainObjectFileStatus();
            }
            mainWritten = mainWritten || batch.MainObjectFileChanged();
            if (dryRun)
            {
                carriedMain = batch.TakeMainObjectFile();
            }
            // The renames that this group found first.
            std::vector<std::pair<std::string, std::string>> groupRenames;
            for (const auto &rename : batch.GetGlobalRenames())
            {
                if (knownRenames.insert(rename).second)
                {
                    groupRenames.push_back(rename);
                }
            }
            report.globalRenames.insert(report.globalRenames.end(), groupRenames.begin(), groupRenames.end());
            if (!ran)
            {
                results.AddResult(DecompilerResultType::Error, ran.error().ToString());
                report.batch = ran;
                break;
            }
            if (counting.IsAborted())
            {
                report.cancelled = true;
                if (!output)
                {
                    // Written, but with the old names of globals that the run
                    // named.
                    report.stale.insert(batch.GetSkippedRewrites().begin(), batch.GetSkippedRewrites().end());
                }
                break;
            }
            if (output || groupRenames.empty())
            {
                // --stdout, or no global has a new name: no script is stale.
                break;
            }
            // Plan section 4.4: the scripts that still use a global of this
            // group by its old name. That is every other script, also a script
            // of an earlier group (a later group can name a global that an
            // earlier script uses), and a script of this group that failed:
            // it can have named a global before it failed, and its old file
            // still uses the old name.
            std::set<uint16_t> candidates;
            for (CompiledScript *compiled : lookups.GetGlobalClassTable().GetAllScripts())
            {
                uint16_t number = compiled->GetScriptNumber();
                if ((toDo.find(number) == toDo.end()) || (batch.GetFailedScripts().find(number) != batch.GetFailedScripts().end()))
                {
                    candidates.insert(number);
                }
            }
            std::set<uint16_t> stale;
            results.AddResult(DecompilerResultType::Update, "Finding the stale scripts");
            sci::Status checked = sci::Guard("finding the stale scripts", [&]() -> sci::Status
            {
                // A dry run reads the sources of the run from memory.
                stale = FindScriptsReferencingGlobals(helper, candidates, groupRenames, dryRun ? &sources.sources : nullptr);
                return sci::Ok();
            });
            if (!checked)
            {
                report.warnings.push_back(checked.error().ToString());
                results.AddResult(DecompilerResultType::Warning, checked.error().ToString());
                break;
            }
            if (!options.updateStale || (group >= MaxStaleGroups))
            {
                if (options.updateStale && !stale.empty())
                {
                    report.warnings.push_back(fmt::format("the run stopped after {0} groups of stale scripts", group));
                    results.AddResult(DecompilerResultType::Warning, report.warnings.back());
                }
                report.stale = std::move(stale);
                break;
            }
            toDo = std::move(stale);
        }

        // After an abort, every script whose file still uses a global of the
        // run by its old name: a rewrite that the abort stopped, a script of
        // an earlier group that a later group did not reach, and a script
        // that the run did not decompile. Main's .sco has the new names: the
        // scripts that the run wrote with them need it. The same after a batch
        // that threw.
        if ((report.cancelled || !report.batch) && !output && options.staleAfterAbort && !report.globalRenames.empty())
        {
            std::set<uint16_t> candidates;
            for (CompiledScript *compiled : lookups.GetGlobalClassTable().GetAllScripts())
            {
                candidates.insert(compiled->GetScriptNumber());
            }
            sci::Status checked = sci::Guard("finding the stale scripts", [&]() -> sci::Status
            {
                std::set<uint16_t> stale = FindScriptsReferencingGlobals(helper, candidates, report.globalRenames, dryRun ? &sources.sources : nullptr);
                report.stale.insert(stale.begin(), stale.end());
                return sci::Ok();
            });
            if (!checked)
            {
                report.warnings.push_back(checked.error().ToString());
                results.AddResult(DecompilerResultType::Warning, checked.error().ToString());
            }
        }

        if (output)
        {
            // Not after an abort or a batch that threw: the source of pass 1
            // is not the source of the run. The script then did not
            // decompile: Cancelled, or the error of the batch.
            for (DecompileOutcome &outcome : report.scripts)
            {
                if (!outcome.status)
                {
                    continue;
                }
                auto source = sources.sources.find(outcome.number);
                if (report.cancelled)
                {
                    outcome.status = sci::Fail(sci::ErrorCode::Cancelled, "the run stopped before it printed the source");
                }
                else if (!report.batch)
                {
                    outcome.status = report.batch;
                }
                else if (source != sources.sources.end())
                {
                    output->OnSource(outcome.number, source->second);
                }
            }
        }
        else
        {
            std::map<uint16_t, std::string> names;
            // The Decompile dialog names every script before its first run,
            // when game.ini has no [Script] entry; entries for only the
            // written scripts would stop that. So the run gives every name
            // then.
            if (!helper.DoesSectionExistWithEntries("Script") && helper.ScriptNames)
            {
                for (const auto &entry : helper.ScriptNames->Entries())
                {
                    // Not the scripts of a name conflict: the GUI does not
                    // check game.ini for one. A script that a reset renamed
                    // and that no group wrote keeps its name from before the
                    // reset: its files have that name (for example, a
                    // cancelled reset of 979 must not write n979=MenuBar_979,
                    // a file that does not exist).
                    if (helper.ScriptNames->ConflictsOf(entry.first).empty())
                    {
                        auto before = namesBeforeReset.find(entry.first);
                        bool unwritten = (before != namesBeforeReset.end()) && (writtenNames.find(entry.first) == writtenNames.end());
                        names[entry.first] = unwritten ? before->second : entry.second.name;
                    }
                }
            }
            for (const auto &written : writtenNames)
            {
                names[written.first] = written.second;
            }
            // Main's .sco, unless script 0 wrote it (its line has it).
            auto mainOutcome = outcomeIndex.find(0);
            if (mainWritten && ((mainOutcome == outcomeIndex.end()) || !report.scripts[mainOutcome->second].status))
            {
                report.files.push_back(helper.GetScriptObjectFileName(helper.GetScriptTitle(0)));
            }
            if (!GameIniEntriesToWrite(helper, names, options.gameIni).empty())
            {
                if (dryRun)
                {
                    // The check of the write: a game.ini that it could not
                    // replace fails the run.
                    report.gameIni = CheckFileCanBeReplaced(helper.GetGameIniFileName(), FILE_SHARE_READ | FILE_SHARE_WRITE);
                }
                else
                {
                    results.AddResult(DecompilerResultType::Update, "Writing the script names into game.ini");
                    report.gameIni = WriteScriptNamesToGameIni(helper, names, options.gameIni);
                }
                if (report.gameIni)
                {
                    report.files.push_back(helper.GetGameIniFileName());
                }
            }
        }
        return report;
    });
}

sci::Result<std::vector<ObjectFileOutcome>> GenerateObjectFiles(GameSession &session, const std::vector<ScriptId> &scripts, const ObjectFileOptions &options)
{
    return sci::Guard("making the .sco files", [&]() -> sci::Result<std::vector<ObjectFileOutcome>>
    {
        const GameFolderHelper &helper = session.Helper();
        std::vector<ObjectFileOutcome> outcomes;
        for (const ScriptId &script : scripts)
        {
            ObjectFileOutcome outcome;
            outcome.number = script.GetResourceNumber();
            outcome.name = script.GetTitle();
            outcome.path = helper.GetScriptObjectFileName(script.GetTitle());
            if (options.abort && options.abort->load())
            {
                outcome.status = sci::Fail(sci::ErrorCode::Cancelled, "the run stopped before this script");
                outcomes.push_back(std::move(outcome));
                continue;
            }
            outcome.status = sci::Guard("", [&]() -> sci::Status
            {
                if (options.onScript)
                {
                    options.onScript(script);
                }
                std::error_code ec;
                if (!fs::exists(script.GetFullPath(), ec))
                {
                    outcome.skipped = "the script has no source file";
                    return sci::Ok();
                }
                CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
                sci::Status loaded = compiled.TryLoad(helper, helper.Version, outcome.number);
                if (!loaded && (loaded.error().code == sci::ErrorCode::NotFound))
                {
                    outcome.skipped = "the game has no compiled script " + std::to_string(outcome.number);
                    return sci::Ok();
                }
                SCI_TRY(loaded);

                SCI_TRY_ASSIGN(ScriptText text, LoadScriptText(script.GetFullPath()));
                CScriptStreamLimiter limiter(text);
                CCrystalScriptStream stream(&limiter);
                sci::Script parsed(script);
                // A (GetPoly "name") statement reads the game's polygon files.
                parsed.SetPolyFolder(helper.GetPolyFolder());
                CompileLog log;
                bool syntaxOk = SyntaxParser_Parse(parsed, stream, PreProcessorDefinesFromSCIVersion(helper.Version), &log);
                sci::Status merged = syntaxOk ? MergeIncludedScripts(parsed, session.ResourceMap(), helper.Version, log) : sci::Ok();
                outcome.diagnostics = log.Results();
                if (!syntaxOk)
                {
                    sci::ErrorLocation where;
                    where.file = script.GetFullPath();
                    return sci::Fail(sci::ErrorCode::Compile, "the source has syntax errors", where);
                }
                SCI_TRY(merged);
                if (parsed.GetScriptNumberDefine().empty() && (parsed.GetScriptNumber() != outcome.number))
                {
                    outcome.diagnostics.push_back(CompileResult(fmt::format("{0} declares script {1}; the .sco is for script {2}",
                        script.GetFileNameOrig(), parsed.GetScriptNumber(), outcome.number), CompileResult::CRT_Warning));
                }
                SCI_TRY(CheckPublicBlock(parsed, script, outcome.number, compiled, outcome.diagnostics));

                std::unique_ptr<CSCOFile> objectFile = SCOFromScriptAndCompiledScript(parsed, compiled);
                // The pair must agree: the .sco describes the compiled script.
                objectFile->SetScriptNumber(outcome.number);
                // The class names of the source, as the compiler writes them:
                // the name string in the compiled script can be another name
                // (for example, the SCI1.1 template's (class Block ...
                // (properties name {Blk}))). The compiler writes the classes
                // in source order, so the positions agree.
                std::vector<std::string> sourceClasses;
                for (const auto &classDefinition : parsed.GetClasses())
                {
                    if (!classDefinition->IsInstance())
                    {
                        sourceClasses.push_back(classDefinition->GetName());
                    }
                }
                std::vector<CSCOObjectClass> &classes = objectFile->GetObjects();
                if (sourceClasses.size() == classes.size())
                {
                    for (size_t i = 0; i < classes.size(); i++)
                    {
                        classes[i].SetName(sourceClasses[i]);
                    }
                }
                else
                {
                    outcome.diagnostics.push_back(CompileResult(fmt::format("{0} has {1} classes, and compiled script {2} has {3}; the .sco uses the names of the compiled script",
                        script.GetFileNameOrig(), sourceClasses.size(), outcome.number, classes.size()), CompileResult::CRT_Warning));
                }
                if (options.dryRun)
                {
                    // What the write would do: nothing for the same bytes, else
                    // a write that must open the file.
                    SCI_TRY_ASSIGN(outcome.changed, SCOFileWouldChange(helper, *objectFile, script));
                    return sci::Ok();
                }
                return SaveSCOFile(helper, *objectFile, script, &outcome.changed);
            });
            outcomes.push_back(std::move(outcome));
        }
        return outcomes;
    });
}
