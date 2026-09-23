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
    // again.
    class LastSources : public IDecompileOutput
    {
    public:
        void OnSource(uint16_t scriptNumber, const std::string &source) override
        {
            sources[scriptNumber] = source;
        }

        std::map<uint16_t, std::string> sources;
    };

    // The includes of a script, as the compiler reads them
    // (PrecompiledHeaders::Update): an include that is not a header (.sh and
    // .shm are headers; a .shp polygon file has locals) is merged into the
    // script, and the includes of a header are read too. The .sco then has
    // the locals of the compiler's .sco (S4 review: before, rm110 of the
    // SCI1.1 template had none of the 11 of 110.shp).
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
                ScriptId includeId(resourceMap.GetIncludePath(name));
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
    return !cancelled && (FailedCount() == 0) && mainObjectFile.has_value() && gameIni.has_value();
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
        if (fs::exists(src / "Decompiler.ini", ec) || !fs::is_directory(decompilerFolder, ec))
        {
            return sci::Ok();
        }
        // A plain copy of the files (before plan step S4, the GUI used the
        // shell, with a window).
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
            fs::copy_file(it->path(), target, fs::copy_options::none, fileError);
            if (fileError)
            {
                return sci::Fail(sci::ErrorCode::Io, fmt::format("could not copy {0} to {1}: {2}", it->path().string(), target.string(), fileError.message()));
            }
        }
        if (ec)
        {
            return sci::Fail(sci::ErrorCode::Io, fmt::format("could not read the folder {0}: {1}", decompilerFolder, ec.message()));
        }
        return sci::Ok();
    });
}

sci::Status WriteScriptNamesToGameIni(const GameFolderHelper &helper, const std::map<uint16_t, std::string> &names, GameIniNames mode)
{
    return sci::Guard("writing the script names into game.ini", [&]() -> sci::Status
    {
        if (mode == GameIniNames::None)
        {
            return sci::Ok();
        }
        std::string iniFile = helper.GetGameIniFileName();
        std::error_code ec;
        if ((mode == GameIniNames::Update) && !fs::exists(iniFile, ec))
        {
            // Plan section 3.4: nothing creates game.ini, except Create.
            return sci::Ok();
        }
        for (const auto &name : names)
        {
            std::string key = default_reskey(name.first, NoBase36);
            if (name.second.empty() || (_stricmp(key.c_str(), name.second.c_str()) == 0))
            {
                // The default name needs no entry.
                continue;
            }
            if (helper.GetIniString("Script", key) == name.second)
            {
                continue;
            }
            if (!WritePrivateProfileString("Script", key.c_str(), name.second.c_str(), iniFile.c_str()))
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

        // With an output, nothing is written: not even the src folder.
        if (!output)
        {
            SCI_TRY(PrepareDecompileFolder(helper, resourceMap.GetDecompilerFolder()));
        }

        // Plan section 3.4: every script needs its name first, because the
        // decompiler writes a (use Name) line for each script that it uses.
        // Missing and All change the session's names (the command line); the
        // GUI passes None, and names with game.ini. All resets only the
        // names of the chosen scripts (S4 review).
        if ((options.names == NameAssignment::Missing) || (options.names == NameAssignment::All))
        {
            SCI_TRY(AddDerivedScriptNames(session));
        }
        if (options.names == NameAssignment::All)
        {
            SCI_TRY_ASSIGN(std::vector<std::string> warnings, ResetScriptNames(session, scripts));
            report.warnings.insert(report.warnings.end(), warnings.begin(), warnings.end());
        }

        GlobalCompiledScriptLookups lookups;
        SCI_TRY(lookups.TryLoad(helper));
        // Prime the selector table: the batch reads it as const, and it caches
        // its names.
        uint16_t unused;
        lookups.GetSelectorTable().ReverseLookup("", unused);
        // The game's src\Decompiler.ini, else the one of the data folder: a run
        // that writes no src folder (--stdout) then gives the source that a
        // file run gives (S4 review: before, it used the default settings,
        // with other global and parameter names).
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
        LastSources sources;
        // The outcome of each script in report.scripts: a script that a later
        // group decompiles again gets the outcome of that group.
        std::map<uint16_t, size_t> outcomeIndex;
        std::set<std::pair<std::string, std::string>> knownRenames;
        std::set<uint16_t> toDo = scripts;
        for (int group = 1; !toDo.empty(); group++)
        {
            DecompileBatch batch(config.get(), lookups, resourceMap, counting, options.engine, output ? &sources : nullptr);
            // Each script has its own exception boundary in the batch; this one
            // keeps the report of the scripts that were written when the batch
            // itself throws (S4 review).
            sci::Status ran = sci::Guard("", [&]() -> sci::Status
            {
                batch.Run(toDo);
                return sci::Ok();
            });
            for (uint16_t number : toDo)
            {
                DecompileOutcome outcome;
                outcome.number = number;
                outcome.name = helper.GetScriptTitle(number);
                auto failed = batch.GetFailedScripts().find(number);
                if (failed != batch.GetFailedScripts().end())
                {
                    outcome.status = sci::Fail(failed->second);
                }
                else if (batch.GetWrittenScripts().find(number) == batch.GetWrittenScripts().end())
                {
                    outcome.status = ran ? sci::Status(sci::Fail(sci::ErrorCode::Cancelled, "the run stopped before this script")) : ran;
                }
                auto index = outcomeIndex.find(number);
                if (index == outcomeIndex.end())
                {
                    outcomeIndex[number] = report.scripts.size();
                    report.scripts.push_back(std::move(outcome));
                }
                else
                {
                    report.scripts[index->second] = std::move(outcome);
                }
            }
            if (report.mainObjectFile && !batch.GetMainObjectFileStatus())
            {
                report.mainObjectFile = batch.GetMainObjectFileStatus();
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
                break;
            }
            if (counting.IsAborted())
            {
                report.cancelled = true;
                // Written, but with the old names of globals that the run named.
                report.stale.insert(batch.GetSkippedRewrites().begin(), batch.GetSkippedRewrites().end());
                break;
            }
            if (output || groupRenames.empty())
            {
                // Nothing was written, or no global has a new name: no script
                // is stale.
                break;
            }
            // Plan section 4.4: the scripts that still use a global of this
            // group by its old name. That is every other script, also a script
            // of an earlier group (S4 review: a later group can name a global
            // that an earlier script uses).
            std::set<uint16_t> candidates;
            for (CompiledScript *compiled : lookups.GetGlobalClassTable().GetAllScripts())
            {
                if (toDo.find(compiled->GetScriptNumber()) == toDo.end())
                {
                    candidates.insert(compiled->GetScriptNumber());
                }
            }
            std::set<uint16_t> stale;
            sci::Status checked = sci::Guard("finding the stale scripts", [&]() -> sci::Status
            {
                stale = FindScriptsReferencingGlobals(helper, candidates, groupRenames);
                return sci::Ok();
            });
            if (!checked)
            {
                report.warnings.push_back(checked.error().ToString());
                break;
            }
            if (!options.updateStale || (group >= MaxStaleGroups))
            {
                if (options.updateStale && !stale.empty())
                {
                    report.warnings.push_back(fmt::format("the run stopped after {0} groups of stale scripts", group));
                }
                report.stale = std::move(stale);
                break;
            }
            toDo = std::move(stale);
        }

        if (output)
        {
            for (const DecompileOutcome &outcome : report.scripts)
            {
                auto source = sources.sources.find(outcome.number);
                if (outcome.status && (source != sources.sources.end()))
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
            // written scripts would stop that (S4 review). So the run gives
            // every name then.
            if (!helper.DoesSectionExistWithEntries("Script") && helper.ScriptNames)
            {
                for (const auto &entry : helper.ScriptNames->Entries())
                {
                    names[entry.first] = entry.second.name;
                }
            }
            for (const DecompileOutcome &outcome : report.scripts)
            {
                if (outcome.status)
                {
                    names[outcome.number] = outcome.name;
                }
            }
            report.gameIni = WriteScriptNamesToGameIni(helper, names, options.gameIni);
        }
        return report;
    });
}

sci::Result<std::vector<ObjectFileOutcome>> GenerateObjectFiles(GameSession &session, const std::vector<ScriptId> &scripts)
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
            outcome.status = sci::Guard("", [&]() -> sci::Status
            {
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

                std::unique_ptr<CSCOFile> objectFile = SCOFromScriptAndCompiledScript(parsed, compiled);
                // The pair must agree: the .sco describes the compiled script.
                objectFile->SetScriptNumber(outcome.number);
                // The class names of the source, as the compiler writes them:
                // the name string in the compiled script can be another name
                // (S4 review: the SCI1.1 template's (class Block ... (properties
                // name {Blk}))). The compiler writes the classes in source
                // order, so the positions agree.
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
                return SaveSCOFile(helper, *objectFile, script);
            });
            outcomes.push_back(std::move(outcome));
        }
        return outcomes;
    });
}
