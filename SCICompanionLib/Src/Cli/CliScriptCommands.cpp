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
                output.Diagnostic(level, fmt::format("{0}({1},{2}): {3} : {4}", script.GetFullPath(), result.GetLineNumber(), result.GetColumn() + 1, kind, result.GetRawMessage()));
            }
        }

        // The messages of the decompiler: an error or a warning as it comes,
        // and the progress with --verbose. Ctrl+C stops the run.
        class CliDecompileResults : public IDecompilerResults
        {
        public:
            explicit CliDecompileResults(CliOutput &output) : _output(output) {}

            void AddResult(DecompilerResultType type, const std::string &message) override
            {
                switch (type)
                {
                case DecompilerResultType::Error:
                    _output.Error(message);
                    break;
                case DecompilerResultType::Warning:
                    _output.Warning(message);
                    break;
                default:
                    // The batch starts each script with this message: the
                    // crash line names the script (plan section 6.6).
                    if (message.rfind("Decompiling script ", 0) == 0)
                    {
                        SetCurrentItem("decompiling script " + message.substr(19));
                    }
                    _output.Detail(message);
                    break;
                }
            }
            bool IsAborted() override { return CancelFlag().load(); }
            void InformStats(bool functionSuccessful, int byteCount) override {}
            void SetGlobalVarsUpdated(const std::vector<std::pair<std::string, std::string>> &renames) override {}

        private:
            CliOutput &_output;
        };

        // --stdout: the source of the script to stdout. --dry-run: no source.
        class CliDecompileOutput : public IDecompileOutput
        {
        public:
            CliDecompileOutput(CliOutput &output, bool print) : _output(output), _print(print) {}

            void OnSource(uint16_t scriptNumber, const std::string &source) override
            {
                if (_print)
                {
                    _output.Result(source);
                }
            }

        private:
            CliOutput &_output;
            bool _print;
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
                output.Warning(fmt::format("these scripts use a global of the run by its old name: {0}; decompile them again, or give --update-stale", ListText(stale)));
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
        CliDecompileResults results(output);
        // With --stdout or --dry-run, the run writes nothing: no .sc, .sco,
        // src folder or game.ini.
        bool writes = !options.toStdout && !common.dryRun;
        CliDecompileOutput sources(output, options.toStdout);
        SCI_TRY_ASSIGN(DecompileReport report, RunDecompile(session, numbers, run, results, writes ? nullptr : &sources));
        PrintDecompileReport(report, session.Helper(), common.dryRun && !options.toStdout, options.toStdout, output);
        return ExitCodeForReport(report);
    }

    sci::Result<ExitCode> RunScriptSco(GameSession &session, const ScriptScoOptions &options, const CommonOptions &common, CliOutput &output)
    {
        SCI_TRY_ASSIGN(ScriptSelection selection, Select(session, options.all, options.selectors, SelectorMode::Sco, output));
        ObjectFileOptions objectFileOptions;
        objectFileOptions.dryRun = common.dryRun;
        objectFileOptions.abort = &CancelFlag();
        SCI_TRY_ASSIGN(std::vector<ObjectFileOutcome> outcomes, GenerateObjectFiles(session, selection.scripts, objectFileOptions));

        ReportFacts facts;
        size_t made = 0;
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
            else
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
        }
        std::string summary = fmt::format("{0} {1} .sco files; {2} scripts skipped.", common.dryRun ? "Made (and did not write)" : "Wrote", made, skipped);
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
