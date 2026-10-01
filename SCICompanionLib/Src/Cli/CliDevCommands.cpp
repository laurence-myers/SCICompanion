#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "StructuralCompare.h"
#include "CompiledScript.h"
#include "GameSession.h"
#include "MeaningCheck.h"
#include "ResourceMap.h"
#include "ScriptCatalog.h"
#include "FileWrite.h"
#include "format.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

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
            // An empty baseline is no baseline; an empty expected or actual
            // folder is a folder that does not exist.
            bool optional = (folder == &options.baselineFolder);
            if ((!folder->empty() || !optional) && !std::filesystem::is_directory(*folder, ec))
            {
                return sci::Fail(sci::ErrorCode::NotFound, "no such folder: \"" + *folder + "\"");
            }
        }
        SetCurrentItem("comparing the scripts");
        std::set<uint16_t> onlyScripts(options.scripts.begin(), options.scripts.end());
        // The version gives only the defines SCI_0 or SCI_1_1, which decompiled text does not use.
        FolderCompareResult result = CompareScriptFolders(options.expectedFolder, options.actualFolder, options.baselineFolder, sciVersion1_1,
            onlyScripts.empty() ? nullptr : &onlyScripts);

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

    const char *const CompareMeaningHeader = "script\tkey\tfunction\toffset\tverdict\tdetail";

    namespace
    {
        // The bytes of a file; empty when it cannot be read.
        std::vector<uint8_t> ReadBytes(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }
    }

    sci::Result<ExitCode> RunCompareMeaning(const CompareMeaningOptions &options, const std::string &dataFolder, CliOutput &output)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(options.recompiledFolder, ec))
        {
            return sci::Fail(sci::ErrorCode::NotFound, "no such folder: \"" + options.recompiledFolder + "\"");
        }
        SessionOptions sessionOptions;
        sessionOptions.dataFolder = dataFolder;
        GameSession game(sessionOptions);
        SetCurrentItem("opening the game");
        SCI_TRY(game.Open(AbsolutePath(options.gameFolder)));
        GlobalCompiledScriptLookups lookups;
        SCI_TRY(lookups.TryLoad(game.Helper()));
        bool separateHeap = game.Helper().Version.SeparateHeapResources;
        // The compiler makes scripts up to SCI1.1 (a script of SCI2 calls
        // other kernel functions).
        bool compilable = (game.Helper().Version.PackageFormat <= ResourcePackageFormat::SCI11);

        // Each script resource of the game (game.ini can be missing).
        SCI_TRY_ASSIGN(std::vector<ScriptRow> scripts, ListScripts(game, false));
        std::set<int> onlyScripts(options.scripts.begin(), options.scripts.end());
        std::string table = std::string(CompareMeaningHeader) + "\n";
        std::vector<std::string> verdicts;
        std::vector<std::string> errors;
        for (const ScriptRow &script : scripts)
        {
            uint16_t number = script.number;
            if (script.location.empty() || (!onlyScripts.empty() && (onlyScripts.count(number) == 0)))
            {
                continue;
            }
            if (CancelFlag().load())
            {
                return sci::Fail(sci::ErrorCode::Cancelled, "cancelled");
            }
            SetCurrentItem(fmt::format("comparing the meaning of script {0}", number));
            sci::Result<std::vector<meaning::Function>> original = meaning::ReadScript(game.Helper(), lookups, game.ResourceMap().GetVocab000(), number);
            if (!original)
            {
                errors.push_back(fmt::format("script {0}: {1}", number, original.error().ToString()));
                continue;
            }
            std::filesystem::path folder(options.recompiledFolder);
            std::vector<uint8_t> scriptData = ReadBytes(folder / fmt::format("script.{0}.bin", number));
            std::vector<uint8_t> heapData = separateHeap ? ReadBytes(folder / fmt::format("heap.{0}.bin", number)) : std::vector<uint8_t>();
            std::vector<meaning::FunctionOutcome> rows;
            if (scriptData.empty() || !compilable)
            {
                // The compile of the decompiled text failed, or the compiler
                // does not make scripts of the version: no function is
                // compared.
                for (const meaning::Function &function : *original)
                {
                    meaning::FunctionOutcome row;
                    row.key = function.key;
                    row.display = function.display;
                    row.offset = function.offset;
                    row.outcome.detail = compilable ? "not-recompiled" : "version-not-compiled";
                    rows.push_back(row);
                }
            }
            else
            {
                sci::Result<std::vector<meaning::Function>> recompiled = meaning::ReadScriptData(game.Helper(), lookups, game.ResourceMap().GetVocab000(), number, scriptData,
                    separateHeap ? &heapData : nullptr);
                if (!recompiled)
                {
                    // Each function is UNCOMPARED, and the warning makes the
                    // exit code 6.
                    errors.push_back(fmt::format("script {0}: the recompiled script: {1}", number, recompiled.error().ToString()));
                    for (const meaning::Function &function : *original)
                    {
                        meaning::FunctionOutcome row;
                        row.key = function.key;
                        row.display = function.display;
                        row.offset = function.offset;
                        row.outcome.detail = "recompiled-unreadable";
                        rows.push_back(row);
                    }
                }
                else
                {
                    rows = meaning::CompareFunctions(*original, *recompiled);
                }
            }
            for (const meaning::FunctionOutcome &row : rows)
            {
                std::string verdict = meaning::VerdictName(row.outcome.verdict);
                table += fmt::format("{0}\t{1}\t{2}\t{3:04x}\t{4}\t{5}\n", number, TableField(row.key), TableField(row.display), row.offset, verdict, TableField(row.outcome.detail));
                verdicts.push_back(verdict);
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
        for (const std::string &error : errors)
        {
            output.Warning(error);
        }
        output.Message(fmt::format("Functions: {0}. Verdicts: {1}.", verdicts.size(), CountsText(verdicts)));
        if (!errors.empty() && (code == ExitCode::Success))
        {
            code = ExitCode::PartialFailure;
        }
        return code;
    }
}
