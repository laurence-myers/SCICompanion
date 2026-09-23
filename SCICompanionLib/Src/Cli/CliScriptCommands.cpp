#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ScriptNameMap.h"
#include "ScriptCatalog.h"
#include "CompileInterfaces.h"
#include "DecompileRun.h"
#include "DecompilerResults.h"
#include "format.h"
#include <algorithm>
#include <atomic>
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
            // Ctrl+C while the scripts were read (C1 review: before, list
            // printed the table and exited with 0).
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
        // no source file has no position.
        void PrintDiagnostic(const CompileResult &result, CliOutput &output)
        {
            DiagnosticLevel level = result.IsError() ? DiagnosticLevel::Error : (result.IsWarning() ? DiagnosticLevel::Warning : DiagnosticLevel::Message);
            const char *kind = result.IsError() ? "error" : (result.IsWarning() ? "warning" : "message");
            ScriptId script = result.GetScript();
            if (script.IsNone() || (result.GetLineNumber() <= 0))
            {
                output.Diagnostic(level, fmt::format("scic: {0}: {1}", kind, result.GetRawMessage()));
            }
            else
            {
                // The path as it was given: GetFullPath has the folder in lower
                // case (review of 11106215).
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
                    // 6.6): the batch starts each script, the naming and the
                    // write of main's .sco with these messages, and the run its
                    // later steps (review of 11106215: the item stayed the last
                    // script).
                    for (const auto &step : StepItems())
                    {
                        if (message.rfind(step.first, 0) == 0)
                        {
                            SetCurrentItem(step.second.empty() ? ("decompiling script " + message.substr(step.first.size())) : step.second);
                            break;
                        }
                    }
                    // The summary has a line for each file (review of
                    // 11106215: with --verbose, a file had two lines).
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
        void PrintDecompileReport(const DecompileReport &report, const GameFolderHelper &helper, bool dryRun, bool toStdout, CliOutput &output)
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
            // and game.ini (review of 11106215: a dry run did not list them).
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
                summary += fmt::format(" Stopped by Ctrl+C: {0} scripts were not decompiled.", notReached);
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
                output.Warning(fmt::format(dryRun ? "after the run, these scripts would use a global of the run by its old name: {0}; decompile them too, or give --update-stale" :
                    "these scripts use a global of the run by its old name: {0}; decompile them again, or give --update-stale", ListText(stale)));
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
        // memory (review of 11106215).
        run.dryRun = common.dryRun && !options.toStdout;
        CliDecompileResults results(output);
        CliDecompileOutput sources(output);
        SCI_TRY_ASSIGN(DecompileReport report, RunDecompile(session, numbers, run, results, options.toStdout ? &sources : nullptr));
        SetCurrentItem("printing the report");
        PrintDecompileReport(report, session.Helper(), run.dryRun, options.toStdout, output);
        return ExitCodeForReport(report, results.Errors());
    }

    sci::Result<ExitCode> RunScriptSco(GameSession &session, const ScriptScoOptions &options, const CommonOptions &common, CliOutput &output)
    {
        SCI_TRY_ASSIGN(ScriptSelection selection, Select(session, options.all, options.selectors, SelectorMode::Sco, output));
        ObjectFileOptions objectFileOptions;
        objectFileOptions.dryRun = common.dryRun;
        objectFileOptions.abort = &CancelFlag();
        // The crash line names the script (review of 11106215).
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
                // The file has these bytes: a run writes nothing (review of
                // 11106215: a dry run said "would write").
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
            summary += fmt::format(" Stopped by Ctrl+C: {0} scripts were not done.", notReached);
        }
        output.Message(summary);
        return ExitCodeForFacts(facts);
    }
}
