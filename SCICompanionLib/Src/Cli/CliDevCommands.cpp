#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "StructuralCompare.h"
#include "FileWrite.h"
#include "format.h"
#include <algorithm>
#include <filesystem>
#include <map>

namespace cli
{
    const char *const CompareStructureHeader = "script\tkey\tfunction\tverdict\tbaseline\tchange";

    namespace
    {
        // A field of the table: a tab or a line break becomes a space.
        std::string TableField(std::string text)
        {
            std::replace_if(text.begin(), text.end(), [](char ch) { return (ch == '\t') || (ch == '\r') || (ch == '\n'); }, ' ');
            return text;
        }

        // "SAME 12, NAMES 3" in the order of the first row of each name.
        std::string CountsText(const std::vector<std::string> &names)
        {
            std::vector<std::string> order;
            std::map<std::string, size_t> counts;
            for (const std::string &name : names)
            {
                if (counts[name]++ == 0)
                {
                    order.push_back(name);
                }
            }
            std::string text;
            for (const std::string &name : order)
            {
                text += fmt::format("{0}{1} {2}", text.empty() ? "" : ", ", name, counts[name]);
            }
            return text.empty() ? std::string("none") : text;
        }
    }

    sci::Result<ExitCode> RunCompareStructure(const CompareStructureOptions &options, CliOutput &output)
    {
        std::error_code ec;
        for (const std::string *folder : { &options.expectedFolder, &options.actualFolder, &options.baselineFolder })
        {
            if (!folder->empty() && !std::filesystem::is_directory(*folder, ec))
            {
                return sci::Fail(sci::ErrorCode::NotFound, "no such folder: " + *folder);
            }
        }
        SetCurrentItem("comparing the scripts");
        // The version gives only the defines SCI_0 or SCI_1_1, which decompiled text does not use.
        FolderCompareResult result = CompareScriptFolders(options.expectedFolder, options.actualFolder, options.baselineFolder, sciVersion1_1);

        std::string table = std::string(CompareStructureHeader) + "\n";
        std::vector<std::string> verdicts;
        std::vector<std::string> changes;
        for (const FunctionCompareRow &row : result.rows)
        {
            std::string verdict = StructureVerdictName(row.verdict);
            std::string baseline = row.hasBaseline ? StructureVerdictName(row.baselineVerdict) : "";
            std::string change = row.hasBaseline ? StructureChangeName(row.change) : "";
            table += fmt::format("{0}\t{1}\t{2}\t{3}\t{4}\t{5}\n", row.script, TableField(row.key), TableField(row.display), verdict, baseline, change);
            verdicts.push_back(verdict);
            if (row.hasBaseline && !change.empty())
            {
                changes.push_back(change);
            }
        }
        ExitCode code = ExitCode::Success;
        if (options.outFile.empty())
        {
            output.Result(table);
        }
        else
        {
            sci::Status written = WriteTextToFile(options.outFile, table);
            if (!written)
            {
                output.Error(written.error().ToString());
                code = ExitCode::WriteFailed;
            }
        }
        for (const std::string &error : result.errors)
        {
            output.Warning(error);
        }
        output.Message(fmt::format("Functions: {0}. Verdicts: {1}.", result.rows.size(), CountsText(verdicts)));
        if (!options.baselineFolder.empty())
        {
            output.Message(fmt::format("Changes from the baseline: {0}.", CountsText(changes)));
        }
        if (!result.errors.empty() && (code == ExitCode::Success))
        {
            code = ExitCode::PartialFailure;
        }
        return code;
    }
}
