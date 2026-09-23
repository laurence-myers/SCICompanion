#include "stdafx.h"
#include "CliCommands.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ScriptNameMap.h"
#include "ScriptCatalog.h"
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

        output.Result((options.format == "tsv") ? TsvTable(rows, options.derived) : TextTable(rows, options.derived));
        // Plan section 4.3: a compiled script that list read and cannot read
        // is exit code 6.
        bool unreadable = std::any_of(rows.begin(), rows.end(), [](const ScriptRow &row) { return !row.error.empty(); });
        return unreadable ? ExitCode::PartialFailure : ExitCode::Success;
    }
}
