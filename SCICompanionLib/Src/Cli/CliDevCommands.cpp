#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "StructuralCompare.h"
#include "CompiledScript.h"
#include "CoreLog.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "MeaningCheck.h"
#include "PaletteOperations.h"
#include "PaletteUse.h"
#include "PicImage.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "ResourceEntity.h"
#include "ResourceMap.h"
#include "ResourceSourceFlags.h"
#include "ResourceUtil.h"
#include "ScriptCatalog.h"
#include "View.h"
#include "FileWrite.h"
#include "format.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
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

    const char *const PaletteUseHeader = "kind\tnumber\tsource\tloops\tcels\tpalette_entries\tpixel_entries\tcolors\terror";

    namespace
    {
        // The numbers of the resources of a type, from the lowest.
        std::vector<int> ResourceNumbersOf(GameSession &game, ResourceType type)
        {
            std::set<int> numbers;
            auto container = game.ResourceMap().Resources(ResourceTypeToFlag(type), game.Helper().GetDefaultEnumFlags() | ResourceEnumFlags::MostRecentOnly);
            for (auto it = container->begin(); it != container->end(); ++it)
            {
                numbers.insert(it.GetResourceNumber());
            }
            return std::vector<int>(numbers.begin(), numbers.end());
        }

        // The runs of the entries, or "-" for none.
        std::string EntriesText(const std::array<bool, 256> &entries)
        {
            const std::string text = IndexRunsText(IndexRunsOf(entries));
            return text.empty() ? std::string("-") : text;
        }

        // "80=aabbcc 81=001122": the colour of each used entry of the
        // palette; "-" for none.
        std::string ColorsText(const PaletteComponent &palette, const std::array<bool, 256> &used)
        {
            std::string text;
            for (int entry = 0; entry < 256; entry++)
            {
                if (used[entry])
                {
                    const RGBQUAD &color = palette.Colors[entry];
                    text += fmt::format("{0}{1}={2:02x}{3:02x}{4:02x}", text.empty() ? "" : " ", entry, color.rgbRed, color.rgbGreen, color.rgbBlue);
                }
            }
            return text.empty() ? std::string("-") : text;
        }

        // Keeps the warnings and the errors that the readers log while a
        // resource is read: a reader repairs some damage (a palette that
        // stores entries past 255, a cel with a size that is not possible)
        // and only logs it. A codec can log from another thread.
        class RepairLogSink : public ILogSink
        {
        public:
            ~RepairLogSink() { RemoveCoreLogSink(this); }

            void Write(LogLevel level, const std::string &text) override
            {
                if (level != LogLevel::Info)
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    _text += (_text.empty() ? "" : " ") + text;
                }
            }

            std::string Text()
            {
                std::lock_guard<std::mutex> lock(_mutex);
                return _text;
            }

        private:
            std::mutex _mutex;
            std::string _text;
        };

        // The cells of a row after the number.
        struct PaletteUseRow
        {
            std::string source = "-";
            std::string loops = "-";
            std::string cels = "-";
            std::string paletteEntries = "-";
            std::string pixelEntries = "-";
            std::string colors = "-";
        };

        // NotFound when the game has no such resource; Format when it is
        // damaged.
        sci::Result<PaletteUseRow> PaletteUseOf(GameSession &game, ResourceType type, int number, bool colors)
        {
            std::unique_ptr<ResourceBlob> blob = game.ResourceMap().MostRecentResource(type, number, false);
            if (!blob)
            {
                return sci::Fail(sci::ErrorCode::NotFound, "the game has no such resource");
            }
            SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> entity, TryCreateResourceFromResourceData(*blob));
            PaletteUseRow row;
            row.source = IsFlagSet(blob->GetSourceFlags(), ResourceSourceFlags::PatchFile) ? "patch" : "package";
            const PaletteComponent *palette = entity->TryGetComponent<PaletteComponent>();
            std::array<bool, 256> used = {};
            if (palette != nullptr)
            {
                used = UsedPaletteEntries(*palette);
            }
            // The used entries come from the bytes of the palette where the
            // command knows its place: a palette resource, and a view of
            // SCI1.1 or SCI2. A damaged palette is then an error.
            const uint8_t *data = blob->GetData();
            const size_t size = blob->GetLength();
            if (type == ResourceType::View)
            {
                const RasterComponent *raster = entity->TryGetComponent<RasterComponent>();
                if (raster == nullptr)
                {
                    return sci::Fail(sci::ErrorCode::Format, "the view has no image");
                }
                const ViewPaletteUse use = DescribeViewPaletteUse(*raster, palette);
                row.loops = fmt::format("{0}", use.loops);
                row.cels = fmt::format("{0}", use.cels);
                row.pixelEntries = EntriesText(use.pixelEntries);
                const ViewFormat format = game.Version().ViewFormat;
                const uint32_t paletteOffset = ((format == ViewFormat::VGA1_1) || (format == ViewFormat::VGA2)) ? ReadViewPaletteOffsetVGA11(data, size) : 0;
                if ((palette != nullptr) && (paletteOffset != 0))
                {
                    SCI_TRY_ASSIGN(used, ReadUsedPaletteEntries(data, size, paletteOffset));
                }
            }
            else
            {
                if (palette == nullptr)
                {
                    return sci::Fail(sci::ErrorCode::Format, "the resource has no palette");
                }
                SCI_TRY_ASSIGN(used, ReadUsedPaletteEntries(data, size, 0));
            }
            row.paletteEntries = EntriesText(used);
            if (colors && (palette != nullptr))
            {
                row.colors = ColorsText(*palette, used);
            }
            return row;
        }
    }

    sci::Result<ExitCode> RunPaletteUse(const PaletteUseOptions &options, const std::string &dataFolder, CliOutput &output)
    {
        SessionOptions sessionOptions;
        sessionOptions.dataFolder = dataFolder;
        GameSession game(sessionOptions);
        SetCurrentItem("opening the game");
        SCI_TRY(game.Open(AbsolutePath(options.gameFolder)));
        if (!options.outFile.empty())
        {
            // The table must not take the place of a file of the game.
            const std::string outFolder = std::filesystem::path(AbsolutePath(options.outFile)).parent_path().string();
            if (PlaceInGameFolder(game.Helper().GameFolder, outFolder) != GameFolderPlace::Outside)
            {
                return sci::Fail(sci::ErrorCode::Usage, "the file of --out is in the game folder; give a file outside the game");
            }
        }

        struct Kind
        {
            ResourceType type;
            const char *name;
        };
        const Kind kinds[] = { { ResourceType::View, "view" }, { ResourceType::Palette, "palette" } };
        std::string table = std::string(PaletteUseHeader) + "\n";
        size_t counts[2] = {};
        std::vector<std::string> errors;
        for (size_t kind = 0; kind < 2; kind++)
        {
            for (int number : ResourceNumbersOf(game, kinds[kind].type))
            {
                if (CancelFlag().load())
                {
                    return sci::Fail(sci::ErrorCode::Cancelled, "cancelled");
                }
                const std::string name = fmt::format("{0} {1}", kinds[kind].name, number);
                SetCurrentItem("reading " + name);
                RepairLogSink repairs;
                // The exception boundary of the resource.
                sci::Result<PaletteUseRow> read = sci::Guard("reading " + name, [&]() -> sci::Result<PaletteUseRow>
                {
                    ScopedCoreLogSink scopedSink(repairs);
                    return PaletteUseOf(game, kinds[kind].type, number, options.colors);
                });
                if (read && !repairs.Text().empty())
                {
                    // The row would show the repaired resource as a good one.
                    read = sci::Fail(sci::ErrorCode::Format, "the reader repaired damaged data: " + repairs.Text());
                }
                const PaletteUseRow row = read ? *read : PaletteUseRow();
                std::string error;
                if (!read)
                {
                    error = read.error().ToString();
                    errors.push_back(name + ": " + error);
                }
                table += fmt::format("{0}\t{1}\t{2}\t{3}\t{4}\t{5}\t{6}\t{7}\t{8}\n", kinds[kind].name, number, row.source, row.loops, row.cels, row.paletteEntries, row.pixelEntries,
                    row.colors, TableField(error));
                counts[kind]++;
            }
        }
        SetCurrentItem("");

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
        output.Message(fmt::format("Views: {0}. Palettes: {1}.", counts[0], counts[1]));
        if (!errors.empty() && (code == ExitCode::Success))
        {
            code = ExitCode::PartialFailure;
        }
        return code;
    }
}
