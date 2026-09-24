#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ScriptNameMap.h"
#include "ScriptCatalog.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "CompileWrite.h"
#include "ResourceUtil.h"
#include "DecompileRun.h"
#include "DecompilerResults.h"
#include "format.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <set>

namespace cli
{
    namespace
    {
        const char *YesNo(bool value)
        {
            return value ? "yes" : "-";
        }

        std::string Location(const ScriptRow &row)
        {
            return row.location.empty() ? std::string("(not compiled)") : row.location;
        }

        std::string Padded(const std::string &text, size_t width)
        {
            return (text.size() >= width) ? text : (text + std::string(width - text.size(), ' '));
        }

        // Plan section 4.3: aligned columns, then the error of a script that
        // cannot be read.
        std::string TextTable(const std::vector<ScriptRow> &rows, bool derived)
        {
            size_t nameWidth = 4;
            size_t sourceWidth = 9;
            size_t locationWidth = 7;
            size_t derivedWidth = 7;
            for (const ScriptRow &row : rows)
            {
                nameWidth = (std::max)(nameWidth, row.name.size());
                sourceWidth = (std::max)(sourceWidth, std::string(NameSourceText(row.source)).size());
                locationWidth = (std::max)(locationWidth, Location(row).size());
                derivedWidth = (std::max)(derivedWidth, row.derivedName.size());
            }
            std::string text = fmt::format("{0:>5}  {1}  {2}  {3}  src  sco", "No.", Padded("Name", nameWidth), Padded("Name from", sourceWidth), Padded("In game", locationWidth));
            if (derived)
            {
                text += "  Derived";
            }
            text += "\n";
            for (const ScriptRow &row : rows)
            {
                std::string line = fmt::format("{0:>5}  {1}  {2}  {3}  {4}  {5}", row.number, Padded(row.name, nameWidth), Padded(NameSourceText(row.source), sourceWidth),
                    Padded(Location(row), locationWidth), Padded(YesNo(row.hasSource), 3), Padded(YesNo(row.hasObjectFile), 3));
                if (derived)
                {
                    line += "  " + Padded(row.derivedName, derivedWidth);
                }
                if (!row.error.empty())
                {
                    line += "  (unreadable: " + row.error + ")";
                }
                // No spaces at the end of a line.
                line.erase(line.find_last_not_of(' ') + 1);
                text += line + "\n";
            }
            return text;
        }

        // Tab-separated columns with a header row, for scripts.
        std::string TsvTable(const std::vector<ScriptRow> &rows, bool derived)
        {
            std::string text = "number\tname\tname_from\tin_game\tsrc\tsco";
            text += derived ? "\tderived\terror\n" : "\terror\n";
            for (const ScriptRow &row : rows)
            {
                text += fmt::format("{0}\t{1}\t{2}\t{3}\t{4}\t{5}", row.number, row.name, NameSourceText(row.source), row.location,
                    row.hasSource ? "yes" : "no", row.hasObjectFile ? "yes" : "no");
                if (derived)
                {
                    text += "\t" + row.derivedName;
                }
                text += "\t" + row.error + "\n";
            }
            return text;
        }
    }

    std::string AbsolutePath(const std::string &path)
    {
        if (path.empty())
        {
            return path;
        }
        char buffer[MAX_PATH * 4];
        DWORD length = GetFullPathNameA(path.c_str(), ARRAYSIZE(buffer), buffer, nullptr);
        std::string full = ((length > 0) && (length < ARRAYSIZE(buffer))) ? std::string(buffer, length) : path;
        // No separator at the end, except the one of a root ("C:\"): the
        // paths that the run makes add their own (else "game\\src").
        while ((full.size() > 1) && ((full.back() == '\\') || (full.back() == '/')) && !std::filesystem::path(full).relative_path().empty())
        {
            full.pop_back();
        }
        return full;
    }

    sci::Result<ExitCode> RunScriptList(GameSession &session, const ScriptListOptions &options, CliOutput &output)
    {
        const GameFolderHelper &helper = session.Helper();
        if (!helper.ScriptNames)
        {
            return sci::Fail(sci::ErrorCode::Internal, "the session has no script names");
        }
        for (const std::string &skipped : helper.ScriptNames->SkippedFiles())
        {
            output.Warning(skipped + " has a character in its name that the ANSI code page does not have; scic does not read it");
        }
        for (const NameConflict &conflict : helper.ScriptNames->Conflicts())
        {
            output.Warning(conflict.text);
        }

        SCI_TRY_ASSIGN(std::vector<ScriptRow> rows, ListScripts(session, options.derived));
        if (!options.selectors.empty())
        {
            SCI_TRY_ASSIGN(ScriptSelection selection, ResolveScriptSelectors(session, options.selectors, SelectorMode::List));
            std::set<uint16_t> wanted;
            for (const ScriptId &script : selection.scripts)
            {
                wanted.insert(script.GetResourceNumber());
            }
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const ScriptRow &row) { return wanted.find(row.number) == wanted.end(); }), rows.end());
        }

        if (CancelFlag().load())
        {
            // Ctrl+C while the scripts were read: no table, and the run
            // fails (exit code 7).
            return sci::Fail(sci::ErrorCode::Cancelled, "stopped by Ctrl+C");
        }
        output.Result((options.format == "tsv") ? TsvTable(rows, options.derived) : TextTable(rows, options.derived));
        // Plan section 4.3: a compiled script that list read and cannot read
        // is exit code 6.
        bool unreadable = std::any_of(rows.begin(), rows.end(), [](const ScriptRow &row) { return !row.error.empty(); });
        return unreadable ? ExitCode::PartialFailure : ExitCode::Success;
    }

    namespace
    {
        // "110 (rm110)".
        std::string ScriptText(uint16_t number, const std::string &name)
        {
            return fmt::format("{0} ({1})", number, name);
        }

        std::string ListText(const std::vector<std::string> &items, size_t shown = 20)
        {
            std::string text;
            for (size_t i = 0; (i < items.size()) && (i < shown); i++)
            {
                text += (text.empty() ? "" : ", ") + items[i];
            }
            if (items.size() > shown)
            {
                text += fmt::format(" and {0} more", items.size() - shown);
            }
            return text;
        }

        // "1 script was", "2 scripts were".
        std::string WasWereText(size_t count, const char *word)
        {
            return fmt::format("{0} {1}{2}", count, word, (count == 1) ? " was" : "s were");
        }

        std::set<uint16_t> NumbersOf(const ScriptSelection &selection)
        {
            std::set<uint16_t> numbers;
            for (const ScriptId &script : selection.scripts)
            {
                numbers.insert(script.GetResourceNumber());
            }
            return numbers;
        }

        sci::Result<ScriptSelection> Select(GameSession &session, bool all, const std::vector<std::string> &selectors, SelectorMode mode, CliOutput &output)
        {
            sci::Result<ScriptSelection> selection = all ? SelectAllScripts(session, mode) : ResolveScriptSelectors(session, selectors, mode);
            if (selection)
            {
                for (const std::string &warning : selection->warnings)
                {
                    output.Warning(warning);
                }
            }
            return selection;
        }

        // A compiler diagnostic in the MSBuild format (plan section 4.5):
        // "path(line,col): error : message", with a 1-based column. One with
        // no source file has no position. A message with a position is an
        // "info" line, shown unless --quiet: such a message can tell that
        // the parser dropped code (for example "The else clause must be the
        // last clause in a cond."). A message with no position (for example
        // the summary of a compile) shows with --verbose only.
        void PrintDiagnostic(const CompileResult &result, CliOutput &output)
        {
            ScriptId script = result.GetScript();
            bool positioned = !script.IsNone() && (result.GetLineNumber() > 0);
            DiagnosticLevel level = result.IsError() ? DiagnosticLevel::Error : (result.IsWarning() ? DiagnosticLevel::Warning :
                (positioned ? DiagnosticLevel::Info : DiagnosticLevel::Message));
            const char *kind = result.IsError() ? "error" : (result.IsWarning() ? "warning" : (positioned ? "info" : "message"));
            if (!positioned)
            {
                output.Diagnostic(level, fmt::format("scic: {0}: {1}", kind, result.GetRawMessage()));
            }
            else
            {
                // The path as it was given: GetFullPath has the folder in lower
                // case.
                output.Diagnostic(level, fmt::format("{0}({1},{2}): {3} : {4}", script.GetFullPathOrig(), result.GetLineNumber(), result.GetColumn() + 1, kind, result.GetRawMessage()));
            }
        }

        // The messages of the decompiler: an error or a warning as it comes,
        // a debug dump plainly, and the progress with --verbose. Ctrl+C stops
        // the run.
        class CliDecompileResults : public IDecompilerResults
        {
        public:
            explicit CliDecompileResults(CliOutput &output) : _output(output) {}

            void AddResult(DecompilerResultType type, const std::string &message) override
            {
                switch (type)
                {
                case DecompilerResultType::Error:
                    _errors++;
                    _output.Error(message);
                    break;
                case DecompilerResultType::Warning:
                    _output.Warning(message);
                    break;
                case DecompilerResultType::Debug:
                    _output.Dump(message);
                    break;
                default:
                    // The crash line names what the run does (plan section
                    // 6.6). The batch prints one of these messages when it
                    // starts a script, the naming or the write of main's
                    // .sco; the run prints one when it starts the stale check
                    // of a group or the write of game.ini. The item is then
                    // that script or that step, until the next of these
                    // messages or the report.
                    for (const auto &step : StepItems())
                    {
                        if (message.rfind(step.first, 0) == 0)
                        {
                            SetCurrentItem(step.second.empty() ? ("decompiling script " + message.substr(step.first.size())) : step.second);
                            break;
                        }
                    }
                    // The summary has a line for each file, so a "Generated"
                    // message does not show: with --verbose, a file would
                    // have two lines.
                    if (message.rfind("Generated ", 0) != 0)
                    {
                        _output.Detail(message);
                    }
                    break;
                }
            }
            bool IsAborted() override { return CancelFlag().load(); }
            void InformStats(bool functionSuccessful, int byteCount) override {}
            void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &renames) override {}

            // The errors that the decompiler reported.
            size_t Errors() const { return _errors.load(); }

        private:
            // The start of a message, and the item of the crash line ("":
            // "decompiling script " and the rest of the message).
            static const std::vector<std::pair<std::string, std::string>> &StepItems()
            {
                static const std::vector<std::pair<std::string, std::string>> items = {
                    { "Decompiling script ", "" },
                    { "Naming variables", "naming the variables of the decompiled scripts" },
                    { "Updating global variables in script 0", "writing main's .sco" },
                    { "Finding the stale scripts", "finding the stale scripts" },
                    { "Writing the script names into game.ini", "writing game.ini" },
                };
                return items;
            }

            CliOutput &_output;
            std::atomic<size_t> _errors{ 0 };
        };

        // --stdout: the source of the script to stdout.
        class CliDecompileOutput : public IDecompileOutput
        {
        public:
            explicit CliDecompileOutput(CliOutput &output) : _output(output) {}

            void OnSource(uint16_t scriptNumber, const std::string &source) override
            {
                _output.Result(source);
            }

        private:
            CliOutput &_output;
        };

        GameIniNames GameIniNamesOf(const std::string &text)
        {
            if (text == "create")
            {
                return GameIniNames::Create;
            }
            return (text == "none") ? GameIniNames::None : GameIniNames::Update;
        }

        // Plan section 4.4, step 7: the summary after the messages of the run.
        void PrintDecompileReport(const DecompileReport &report, const GameFolderHelper &helper, bool dryRun, bool toStdout, bool updateStale, CliOutput &output)
        {
            std::vector<std::string> failed;
            size_t written = 0;
            size_t notReached = 0;
            for (const DecompileOutcome &outcome : report.scripts)
            {
                if (outcome.status)
                {
                    written++;
                    std::string files = helper.GetScriptFileName(outcome.name) + " and " + helper.GetScriptObjectFileName(outcome.name);
                    if (dryRun)
                    {
                        output.Message("would write " + files);
                    }
                    else if (!toStdout)
                    {
                        output.Detail("wrote " + files);
                    }
                }
                else if (outcome.status.error().code == sci::ErrorCode::Cancelled)
                {
                    notReached++;
                }
                else
                {
                    // Its error was printed when it happened.
                    failed.push_back(ScriptText(outcome.number, outcome.name));
                }
            }
            // The other files: the src folder's decompiler files, main's .sco
            // and game.ini (a dry run lists them too).
            for (const std::string &file : report.files)
            {
                if (dryRun)
                {
                    output.Message("would write " + file);
                }
                else
                {
                    output.Detail("wrote " + file);
                }
            }
            std::string summary = fmt::format("{0} {1} of {2} scripts.", (dryRun || toStdout) ? "Decompiled" : "Decompiled and wrote", written, report.scripts.size());
            if (!failed.empty())
            {
                summary += fmt::format(" Failed: {0}.", ListText(failed));
            }
            if (report.cancelled)
            {
                summary += fmt::format(" Stopped by Ctrl+C: {0} not decompiled.", WasWereText(notReached, "script"));
            }
            output.Message(summary);

            const DecompileStats &stats = report.stats;
            int functions = stats.functions + stats.fallbacks;
            int bytes = stats.functionBytes + stats.fallbackBytes;
            if (functions > 0)
            {
                output.Message(fmt::format("Functions: {0} of {1} decompiled ({2}%); bytes: {3}%.{4}", stats.functions, functions, stats.functions * 100 / functions,
                    (bytes > 0) ? (stats.functionBytes * 100 / bytes) : 100,
                    (stats.fallbacks > 0) ? fmt::format(" {0} fell back to asm.", stats.fallbacks) : std::string()));
            }
            if (!report.globalRenames.empty())
            {
                std::vector<std::string> renames;
                for (const auto &rename : report.globalRenames)
                {
                    renames.push_back(rename.first + " -> " + rename.second);
                }
                output.Message("Globals named: " + ListText(renames) + ".");
            }
            if (!report.stale.empty())
            {
                std::vector<std::string> stale;
                for (uint16_t number : report.stale)
                {
                    stale.push_back(ScriptText(number, helper.GetScriptTitle(number)));
                }
                // With --update-stale, only an abort (or the guard of 100
                // groups) leaves stale scripts: the advice is to decompile
                // them again, not to give --update-stale.
                output.Warning(fmt::format(dryRun ? "after the run, these scripts would use a global of the run by its old name: {0}; decompile them too{1}" :
                    "these scripts use a global of the run by its old name: {0}; decompile them again{1}", ListText(stale), updateStale ? "" : ", or give --update-stale"));
            }
            if (!report.gameIni)
            {
                output.Error("game.ini: " + report.gameIni.error().ToString());
            }
        }
    }

    sci::Result<ExitCode> RunScriptDecompile(GameSession &session, const ScriptDecompileOptions &options, const CommonOptions &common, CliOutput &output)
    {
        SCI_TRY_ASSIGN(ScriptSelection selection, Select(session, options.all, options.selectors, SelectorMode::Decompile, output));
        std::set<uint16_t> numbers = NumbersOf(selection);
        if (options.toStdout && (numbers.size() != 1))
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("--stdout takes one script, not {0}", numbers.size()));
        }
        if (numbers.empty())
        {
            output.Message("No script to decompile.");
            return ExitCode::Success;
        }

        DecompileRunOptions run;
        run.engine.SubstituteTextTuples = options.textTuples;
        run.engine.DecompileAsm = options.asmOnly;
        run.engine.DebugControlFlow = options.debugControlFlow;
        run.engine.DebugInstructionConsumption = options.debugInstructions;
        run.engine.DebugFunctionMatch = options.debugFilter;
        run.names = options.resetNames ? NameAssignment::All : NameAssignment::Missing;
        run.gameIni = GameIniNamesOf(options.gameIni);
        run.updateStale = options.updateStale;
        // With --stdout or --dry-run, the run writes nothing: no .sc, .sco,
        // src folder or game.ini. A dry run does the other steps of a run in
        // memory.
        run.dryRun = common.dryRun && !options.toStdout;
        CliDecompileResults results(output);
        CliDecompileOutput sources(output);
        SCI_TRY_ASSIGN(DecompileReport report, RunDecompile(session, numbers, run, results, options.toStdout ? &sources : nullptr));
        SetCurrentItem("printing the report");
        PrintDecompileReport(report, session.Helper(), run.dryRun, options.toStdout, options.updateStale, output);
        return ExitCodeForReport(report, results.Errors());
    }

    sci::Result<ExitCode> RunScriptSco(GameSession &session, const ScriptScoOptions &options, const CommonOptions &common, CliOutput &output)
    {
        SCI_TRY_ASSIGN(ScriptSelection selection, Select(session, options.all, options.selectors, SelectorMode::Sco, output));
        ObjectFileOptions objectFileOptions;
        objectFileOptions.dryRun = common.dryRun;
        objectFileOptions.abort = &CancelFlag();
        // The crash line names the script.
        objectFileOptions.onScript = [](const ScriptId &script)
        {
            SetCurrentItem("making the .sco of script " + ScriptText(script.GetResourceNumber(), script.GetTitle()));
        };
        SCI_TRY_ASSIGN(std::vector<ObjectFileOutcome> outcomes, GenerateObjectFiles(session, selection.scripts, objectFileOptions));
        SetCurrentItem("printing the report");

        ReportFacts facts;
        size_t made = 0;
        size_t unchanged = 0;
        size_t skipped = 0;
        size_t notReached = 0;
        std::vector<std::string> failed;
        for (const ObjectFileOutcome &outcome : outcomes)
        {
            for (const CompileResult &diagnostic : outcome.diagnostics)
            {
                PrintDiagnostic(diagnostic, output);
            }
            if (!outcome.status)
            {
                switch (outcome.status.error().code)
                {
                case sci::ErrorCode::Cancelled:
                    facts.cancelled = true;
                    notReached++;
                    continue;
                case sci::ErrorCode::Internal:
                    facts.internal = true;
                    break;
                case sci::ErrorCode::Io:
                    facts.writeFailed = true;
                    break;
                default:
                    // Plan section 4.6: a syntax error too is exit code 6.
                    facts.otherFailures = true;
                    break;
                }
                failed.push_back(ScriptText(outcome.number, outcome.name));
                output.Error(ScriptText(outcome.number, outcome.name) + ": " + outcome.status.error().ToString());
            }
            else if (!outcome.skipped.empty())
            {
                skipped++;
                output.Message(fmt::format("skipped {0}: {1}", ScriptText(outcome.number, outcome.name), outcome.skipped));
            }
            else if (outcome.changed)
            {
                made++;
                if (common.dryRun)
                {
                    output.Message("would write " + outcome.path);
                }
                else
                {
                    output.Detail("wrote " + outcome.path);
                }
            }
            else
            {
                // The file has these bytes: a run writes nothing, and a dry
                // run says "would not change".
                unchanged++;
                output.Detail(outcome.path + (common.dryRun ? " would not change" : " did not change"));
            }
        }
        std::string summary = fmt::format("{0} {1} .sco files ({2} {3}); {4} scripts skipped.", common.dryRun ? "Would write" : "Wrote", made,
            unchanged, common.dryRun ? "would not change" : "did not change", skipped);
        if (!failed.empty())
        {
            summary += fmt::format(" Failed: {0}.", ListText(failed));
        }
        if (notReached > 0)
        {
            summary += fmt::format(" Stopped by Ctrl+C: {0} not done.", WasWereText(notReached, "script"));
        }
        output.Message(summary);
        return ExitCodeForFacts(facts);
    }

    namespace
    {
        // The progress of a compile, with --verbose. The crash line names the
        // script (plan section 6.6).
        class CliCompileEvents : public ICompileEvents
        {
        public:
            explicit CliCompileEvents(CliOutput &output) : _output(output) {}

            void OnScriptStart(size_t index, size_t count, const ScriptId &script) override
            {
                std::string name = ScriptText(script.GetResourceNumber(), script.GetTitle());
                SetCurrentItem("compiling script " + name);
                _output.Detail(fmt::format("[{0}/{1}] Compiling {2}", index + 1, count, name));
            }
            void OnPassStart(int pass) override
            {
                _output.Detail(fmt::format("Pass {0}: a .sco file changed, so every script compiles again.", pass));
            }
            void OnFinish() override
            {
                SetCurrentItem("writing the compiled resources");
            }

        private:
            CliOutput &_output;
        };

        // The resources go into the package (not an output folder).
        bool ToThePackage(const GameFolderHelper &helper, const CompileWriteOptions &write)
        {
            return write.outDir.empty() && (helper.GetResourceSaveLocation(write.saveTo) == ResourceSaveLocation::Package);
        }

        // A written resource: its file (a patch file, or a file of the output
        // folder), or "script 110" in the package.
        std::string WrittenText(const WrittenResource &resource, const GameFolderHelper &helper, const CompileWriteOptions &write)
        {
            if (!write.outDir.empty())
            {
                return (std::filesystem::path(write.outDir) / CompiledResourceFileName(resource.type, resource.number, helper.Version, write.raw)).string();
            }
            if (ToThePackage(helper, write))
            {
                std::string type = GetResourceTypeTitle(resource.type);
                std::transform(type.begin(), type.end(), type.begin(), [](char ch) { return (char)tolower((unsigned char)ch); });
                return fmt::format("{0} {1}", type, resource.number);
            }
            return (std::filesystem::path(helper.GameFolder) / CompiledResourceFileName(resource.type, resource.number, helper.Version, false)).string();
        }

        std::string DestinationText(const GameFolderHelper &helper, const CompileWriteOptions &write)
        {
            if (!write.outDir.empty())
            {
                return "into " + write.outDir;
            }
            return ToThePackage(helper, write) ? std::string("into the package") : std::string("as patch files");
        }

        // "1 error", "2 errors".
        std::string CountText(size_t count, const char *word)
        {
            return fmt::format("{0} {1}{2}", count, word, (count == 1) ? "" : "s");
        }

        // Plan section 4.5, step 6: the diagnostics of the last pass (a script
        // that failed in an earlier pass, and compiled in the last, has no
        // error), what went where, and the totals.
        void PrintCompileReport(const CompileReport &report, size_t scriptCount, const GameFolderHelper &helper, const CompileWriteOptions &write, bool dryRun, bool all,
            CliOutput &output)
        {
            for (const ScriptOutcome &outcome : report.scripts)
            {
                for (const CompileResult &diagnostic : outcome.diagnostics)
                {
                    PrintDiagnostic(diagnostic, output);
                }
            }
            // What the commit wrote (a dry run: would write). A commit that
            // failed wrote no resource.
            auto listWrites = [&](const std::vector<WrittenResource> &resources)
            {
                std::vector<std::string> files;
                for (const WrittenResource &resource : resources)
                {
                    files.push_back(WrittenText(resource, helper, write));
                }
                if (files.empty() || !report.commit)
                {
                    return;
                }
                std::string text = ListText(files, files.size());
                if (dryRun)
                {
                    output.Message("would write " + text + (ToThePackage(helper, write) ? " into the package" : ""));
                }
                else
                {
                    output.Detail("wrote " + text + (ToThePackage(helper, write) ? " into the package" : ""));
                }
            };
            std::vector<std::string> failed;
            for (const ScriptOutcome &outcome : report.scripts)
            {
                if (outcome.status)
                {
                    listWrites(outcome.written);
                }
                else
                {
                    failed.push_back(ScriptText(outcome.number, outcome.name));
                    // A compile error is in the diagnostics, and so is the
                    // error of a read, a write or an exception: the compile or
                    // the batch logs it, so it does not print here again.
                    // Another failure prints here.
                    std::string error = outcome.status.error().ToString();
                    bool inTheDiagnostics = (outcome.status.error().code == sci::ErrorCode::Compile) ||
                        std::any_of(outcome.diagnostics.begin(), outcome.diagnostics.end(), [&error](const CompileResult &diagnostic)
                        {
                            return diagnostic.IsError() && (diagnostic.GetRawMessage().find(error) != std::string::npos);
                        });
                    if (!inTheDiagnostics)
                    {
                        output.Error(ScriptText(outcome.number, outcome.name) + ": " + error);
                    }
                }
            }
            listWrites(report.tablesWritten);
            // A table failure refuses the commit: one error line (as the GUI).
            bool refusedByTables = !report.tables && !report.commit && (report.commit.error().message == report.tables.error().message);
            if (!report.tables)
            {
                output.Error(std::string("the class and selector tables could not be saved") + (refusedByTables ? ", so no compiled resource was written: " : ": ") +
                    report.tables.error().ToString());
            }
            if (!report.commit && !refusedByTables)
            {
                output.Error(report.commit.error().ToString());
            }
            for (const std::string &moved : report.movedPatches)
            {
                output.Message("moved the patch file " + moved);
            }
            if (!report.moves)
            {
                output.Error(report.moves.error().ToString());
            }
            for (const std::string &restored : report.restoredObjectFiles)
            {
                output.Message("put back " + restored + ": its script was not written");
            }
            if (!report.objectFiles)
            {
                output.Error(report.objectFiles.error().ToString());
            }
            for (const std::string &warning : report.warnings)
            {
                output.Warning(warning);
            }
            if (report.passLimit)
            {
                // A compile of named scripts is one pass: a .sco that changes is
                // expected, and the scripts that use it are not in the run.
                output.Warning((report.cancelled || report.stopped) ?
                    std::string("the run stopped after a pass that changed a .sco file, so a script can use an old one: compile again") :
                    (all ? fmt::format("pass {0}, the last, still changed a .sco file, so a script can use an old one: compile again, or give more --passes", report.passes) :
                    std::string("a .sco file changed, so the scripts that use it can be out of date: compile them too, or give --all")));
            }

            size_t compiled = report.CompiledCount();
            size_t notReached = scriptCount - report.scripts.size();
            std::string destination = DestinationText(helper, write);
            // With no compiled script, nothing is written: a dry run says "a
            // run would write none".
            std::string summary = (compiled == 0) ? fmt::format("Compiled 0 of {0} scripts, and {1} none", scriptCount, dryRun ? "a run would write" : "wrote") :
                (dryRun ? fmt::format("Compiled {0} of {1} scripts; a run would write them {2}", compiled, scriptCount, destination) :
                (report.commit ? fmt::format("Compiled and wrote {0} of {1} scripts {2}", compiled, scriptCount, destination) :
                fmt::format("Compiled {0} of {1} scripts, and wrote none", compiled, scriptCount)));
            // The warnings of the scripts, and the warnings of the batch that
            // printed above (also the warning of the pass limit).
            size_t warnings = report.WarningCount() + report.warnings.size() + (report.passLimit ? 1 : 0);
            summary += fmt::format(" ({0}, {1}{2}).", CountText(report.ErrorCount(), "error"), CountText(warnings, "warning"),
                (report.passes > 1) ? fmt::format(", {0} passes", report.passes) : std::string());
            if (!failed.empty())
            {
                summary += fmt::format(" Failed: {0}.", ListText(failed));
            }
            if (report.cancelled)
            {
                summary += fmt::format(" Stopped by Ctrl+C: {0} not compiled.", WasWereText(notReached, "script"));
            }
            if (report.stopped)
            {
                summary += fmt::format(" --fail-fast stopped the run: {0} not compiled.", WasWereText(notReached, "script"));
            }
            output.Message(summary);
        }
    }

    sci::Result<ExitCode> RunScriptCompile(GameSession &session, const ScriptCompileOptions &options, const CommonOptions &common, CliOutput &output)
    {
        if (options.intoVolume && options.toGiven && (options.to != "package"))
        {
            // --to patch with --into-volume is a usage error: it must not
            // write the package.
            return sci::Fail(sci::ErrorCode::Usage, "--into-volume is --to package, so it does not go with --to patch");
        }
        bool toPackage = options.intoVolume || (options.to == "package");
        if (options.replacePatches && !toPackage)
        {
            return sci::Fail(sci::ErrorCode::Usage, "--replace-patches goes with --to package");
        }
        if (options.passesGiven && !options.all)
        {
            return sci::Fail(sci::ErrorCode::Usage, "--passes goes with --all: a compile of named scripts is one pass");
        }
        // The step of the crash line.
        SetCurrentItem("selecting the scripts");
        SCI_TRY_ASSIGN(ScriptSelection selection, Select(session, options.all, options.selectors, SelectorMode::Compile, output));
        if (selection.scripts.empty())
        {
            output.Message("No script to compile.");
            return ExitCode::Success;
        }

        session.SetWarnOnUnusedInstances(!options.noWarnUnused);
        CompileOptions compile;
        // Plan section 5: patch files unless --to package, whatever the
        // game's setting.
        compile.write.saveTo = toPackage ? ResourceSaveLocation::Package : ResourceSaveLocation::Patch;
        // An absolute folder: the report gives absolute paths, and the length
        // check measures the path that the write uses.
        compile.write.outDir = AbsolutePath(options.outDir);
        compile.write.raw = options.raw;
        // --dry-run: no resource, table, .sco or .scd.
        compile.write.writeResources = !common.dryRun;
        compile.write.writeObjectFile = !common.dryRun;
        compile.write.writeDebugInfo = !common.dryRun;
        compile.failFast = options.failFast;
        compile.passes = options.all ? options.passes : 1;
        compile.shadows = options.replacePatches ? ShadowPolicy::Replace : ShadowPolicy::Refuse;
        CliCompileEvents events(output);
        SetCurrentItem("starting the compile");
        SCI_TRY_ASSIGN(CompileReport report, CompileScripts(session, selection.scripts, compile, CancelFlag(), events));
        SetCurrentItem("printing the report");
        PrintCompileReport(report, selection.scripts.size(), session.Helper(), compile.write, common.dryRun, options.all, output);
        return ExitCodeForReport(report);
    }
}
