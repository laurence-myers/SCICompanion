#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "ResourceEntity.h"
#include "ResourceSourceFlags.h"
#include "ResourceUtil.h"
#include "PaletteOperations.h"
#include "PicImage.h"
#include "PicSidecar.h"
#include "IndexedBmp.h"
#include "FileWrite.h"
#include "crc.h"
#include "format.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace cli
{
    const char *const PicListTsvHeader =
        "number\tformat\twidth\theight\tcels\tcompressed_cels\tbackground_cel\tbackground_width\tbackground_height\tbackground_x\tbackground_y\tbackground_priority\t"
        "source\tbytes\tcrc32\tskip_pixels\tpalette_entries\tpixel_entries\terror\n";

    namespace
    {
        std::string PicText(int number)
        {
            return fmt::format("pic {0}", number);
        }

        // "1 pic", "2 pics".
        std::string CountText(size_t count, const char *word)
        {
            return fmt::format("{0} {1}{2}", count, word, (count == 1) ? "" : "s");
        }

        // The numbers of the pics of the game, from the lowest.
        std::vector<int> PicNumbersOf(GameSession &session)
        {
            std::set<int> numbers;
            auto container = session.ResourceMap().Resources(ResourceTypeFlags::Pic, session.Helper().GetDefaultEnumFlags() | ResourceEnumFlags::MostRecentOnly);
            for (auto it = container->begin(); it != container->end(); ++it)
            {
                numbers.insert(it.GetResourceNumber());
            }
            return std::vector<int>(numbers.begin(), numbers.end());
        }

        // A number of 1 to 5 digits, up to 65535.
        bool ParseNumber(const std::string &text, int &number)
        {
            if (text.empty() || (text.size() > 5) || !std::all_of(text.begin(), text.end(), [](char ch) { return (ch >= '0') && (ch <= '9'); }))
            {
                return false;
            }
            number = std::stoi(text);
            return number <= 65535;
        }

        // The pics of the game that the selectors name: a number ("230") or a
        // range ("100-199"), from the lowest, each pic once. Usage for a text
        // that is not a number or a range, for a number that is not a pic of
        // the game, and for a range with no pic of the game.
        sci::Result<std::vector<int>> SelectPics(const std::vector<int> &all, const std::vector<std::string> &selectors)
        {
            std::set<int> selected;
            for (const std::string &selector : selectors)
            {
                int first = 0;
                int last = 0;
                size_t dash = selector.find('-');
                bool parsed = (dash == std::string::npos) ? ParseNumber(selector, first) :
                    (ParseNumber(selector.substr(0, dash), first) && ParseNumber(selector.substr(dash + 1), last));
                if (dash == std::string::npos)
                {
                    last = first;
                }
                if (!parsed || (first > last))
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("\"{0}\" is not a pic number or a range of pic numbers (for example 230 or 100-199)", selector));
                }
                bool any = false;
                for (int number : all)
                {
                    if ((number >= first) && (number <= last))
                    {
                        selected.insert(number);
                        any = true;
                    }
                }
                if (!any)
                {
                    return sci::Fail(sci::ErrorCode::Usage, (first == last) ? fmt::format("the game has no pic {0}", first) : fmt::format("the game has no pic in the range {0}-{1}", first, last));
                }
            }
            return std::vector<int>(selected.begin(), selected.end());
        }

        // A pic of the game: the most recent copy of its resource.
        struct PicSource
        {
            int number = 0;
            std::unique_ptr<ResourceBlob> blob;
            std::vector<uint8_t> data;
            bool fromPatch = false;
            uint32_t crc32 = 0;
        };

        const char *SourceText(const PicSource &source)
        {
            return source.fromPatch ? "patch" : "package";
        }

        // NotFound when the game has no such pic; Format when its resource is
        // damaged.
        sci::Result<PicSource> ReadPic(GameSession &session, int number)
        {
            PicSource source;
            source.number = number;
            source.blob = session.ResourceMap().MostRecentResource(ResourceType::Pic, number, false);
            if (!source.blob)
            {
                return sci::Fail(sci::ErrorCode::NotFound, fmt::format("the game has no pic {0}", number));
            }
            SCI_TRY(CheckResourceData(*source.blob));
            const uint8_t *bytes = source.blob->GetData();
            source.data.assign(bytes, bytes + source.blob->GetLength());
            source.fromPatch = IsFlagSet(source.blob->GetSourceFlags(), ResourceSourceFlags::PatchFile);
            source.crc32 = source.data.empty() ? 0 : (uint32_t)crcFast(source.data.data(), (int)source.data.size());
            return std::move(source);
        }

        // The entries of a palette that are marked as used (their flag is not
        // 0); none for no palette.
        std::array<bool, 256> UsedEntriesOf(const PaletteComponent *palette)
        {
            std::array<bool, 256> used = {};
            if (palette != nullptr)
            {
                for (size_t i = 0; i < used.size(); i++)
                {
                    used[i] = (palette->Colors[i].rgbReserved != 0);
                }
            }
            return used;
        }

        // The palette entries that the pixels of the cels have, without the
        // skip colour of each cel.
        std::array<bool, 256> PixelEntriesOf(const PicDescription &description)
        {
            std::array<bool, 256> entries = {};
            for (const PicCelDescription &cel : description.cels)
            {
                std::array<bool, 256> ofCel = cel.UsedEntries();
                ofCel[cel.skipColor] = false;
                for (size_t i = 0; i < entries.size(); i++)
                {
                    entries[i] = entries[i] || ofCel[i];
                }
            }
            return entries;
        }

        // The output folder of export: NotFound when it is not a folder,
        // Usage when it is the game folder.
        sci::Status CheckOutputFolder(GameSession &session, const std::string &outDir)
        {
            std::error_code ec;
            if (!fs::is_directory(outDir, ec))
            {
                return sci::Fail(sci::ErrorCode::NotFound, "the output folder does not exist: " + outDir);
            }
            if (fs::equivalent(outDir, session.Helper().GameFolder, ec))
            {
                return sci::Fail(sci::ErrorCode::Usage, "the output folder is the game folder; give another folder");
            }
            return sci::Ok();
        }

        // One row of pic list.
        struct PicRow
        {
            int number = 0;
            std::string format;
            // Empty when the pic was not read.
            std::string source;
            size_t bytes = 0;
            uint32_t crc32 = 0;
            // The pic has an image that scic reads, and description has it.
            bool described = false;
            PicDescription description;
            // The pic cannot be read.
            std::string error;
        };

        PicRow RowOf(GameSession &session, int number)
        {
            PicRow row;
            row.number = number;
            row.format = PicFormatName(session.Version().PicFormat);
            // The exception boundary of the pic.
            sci::Status read = sci::Guard("reading " + PicText(number), [&]() -> sci::Status
            {
                SCI_TRY_ASSIGN(PicSource source, ReadPic(session, number));
                row.source = SourceText(source);
                row.bytes = source.data.size();
                row.crc32 = source.crc32;
                if (CanReadPicImage(session.Version().PicFormat))
                {
                    SCI_TRY_ASSIGN(row.description, DescribePic(session.Version(), source.data));
                    row.described = true;
                }
                return sci::Ok();
            });
            if (!read)
            {
                row.error = read.error().ToString();
            }
            return row;
        }

        size_t CompressedCels(const PicDescription &description)
        {
            return (size_t)std::count_if(description.cels.begin(), description.cels.end(), [](const PicCelDescription &cel) { return cel.compressed; });
        }

        uint32_t SkipPixels(const PicDescription &description)
        {
            uint32_t pixels = 0;
            for (const PicCelDescription &cel : description.cels)
            {
                pixels += cel.SkipPixels();
            }
            return pixels;
        }

        const PicCelDescription *BackgroundOf(const PicDescription &description)
        {
            return (description.backgroundCel >= 0) ? &description.cels[description.backgroundCel] : nullptr;
        }

        // The cells of the row for the text table, in the order of the
        // header. A pic that was not described has "-" in its image cells.
        std::vector<std::string> TextCells(const PicRow &row)
        {
            std::vector<std::string> cells = { fmt::format("{0}", row.number), row.format };
            if (row.described)
            {
                const PicDescription &pic = row.description;
                const PicCelDescription *background = BackgroundOf(pic);
                std::string backgroundText = "-";
                if (background != nullptr)
                {
                    backgroundText = fmt::format("{0}: {1}x{2} at {3},{4}", background->index, background->width, background->height, background->x, background->y);
                    if (pic.format == PicFormat::VGA2)
                    {
                        backgroundText += fmt::format(" pri {0}", background->priority);
                    }
                }
                cells.push_back(fmt::format("{0}x{1}", pic.width, pic.height));
                cells.push_back(fmt::format("{0}", pic.cels.size()));
                cells.push_back(fmt::format("{0}", CompressedCels(pic)));
                cells.push_back(backgroundText);
            }
            else
            {
                cells.insert(cells.end(), 4, "-");
            }
            cells.push_back(row.source.empty() ? std::string("-") : row.source);
            cells.push_back(row.source.empty() ? std::string("-") : fmt::format("{0}", row.bytes));
            cells.push_back(row.source.empty() ? std::string("-") : fmt::format("{0:08x}", row.crc32));
            if (row.described)
            {
                std::string pixelEntries = IndexRunsText(IndexRunsOf(PixelEntriesOf(row.description)));
                std::string paletteEntries = IndexRunsText(IndexRunsOf(row.description.palette.UsedEntries()));
                cells.push_back(fmt::format("{0}", SkipPixels(row.description)));
                cells.push_back(paletteEntries.empty() ? std::string("-") : paletteEntries);
                cells.push_back(pixelEntries.empty() ? std::string("-") : pixelEntries);
            }
            else
            {
                cells.insert(cells.end(), 3, "-");
            }
            return cells;
        }

        // Aligned columns: the numbers to the right, the texts to the left.
        // The entries of the pixels are the last column: they can be long.
        // Then the error of a pic that cannot be read.
        std::string TextTable(const std::vector<PicRow> &rows)
        {
            const std::vector<std::string> header = { "Pic", "Format", "Size", "Cels", "Comp", "Background cel", "Source", "Bytes", "CRC32", "Skip px", "Palette entries", "Pixel entries" };
            const std::vector<bool> right = { true, false, false, true, true, false, false, true, false, true, false, false };
            std::vector<std::vector<std::string>> table = { header };
            for (const PicRow &row : rows)
            {
                table.push_back(TextCells(row));
            }
            std::vector<size_t> widths(header.size(), 0);
            for (const std::vector<std::string> &cells : table)
            {
                for (size_t i = 0; i < cells.size(); i++)
                {
                    widths[i] = (std::max)(widths[i], cells[i].size());
                }
            }
            std::string text;
            for (size_t r = 0; r < table.size(); r++)
            {
                std::string line;
                for (size_t i = 0; i < table[r].size(); i++)
                {
                    const std::string &cell = table[r][i];
                    const std::string padding(widths[i] - cell.size(), ' ');
                    line += (i == 0) ? "" : "  ";
                    line += right[i] ? (padding + cell) : (cell + padding);
                }
                if ((r > 0) && !rows[r - 1].error.empty())
                {
                    line += "  (unreadable: " + rows[r - 1].error + ")";
                }
                // No spaces at the end of a line.
                line.erase(line.find_last_not_of(' ') + 1);
                text += line + "\n";
            }
            return text;
        }

        // Tab-separated columns with a header row, for scripts. A cell with
        // no value is empty.
        std::string TsvTable(const std::vector<PicRow> &rows)
        {
            std::string text = PicListTsvHeader;
            for (const PicRow &row : rows)
            {
                text += fmt::format("{0}\t{1}\t", row.number, row.format);
                if (row.described)
                {
                    const PicDescription &pic = row.description;
                    text += fmt::format("{0}\t{1}\t{2}\t{3}\t", pic.width, pic.height, pic.cels.size(), CompressedCels(pic));
                    const PicCelDescription *background = BackgroundOf(pic);
                    if (background != nullptr)
                    {
                        text += fmt::format("{0}\t{1}\t{2}\t{3}\t{4}\t", background->index, background->width, background->height, background->x, background->y);
                        text += (pic.format == PicFormat::VGA2) ? fmt::format("{0}\t", background->priority) : std::string("\t");
                    }
                    else
                    {
                        text += "\t\t\t\t\t\t";
                    }
                }
                else
                {
                    text += "\t\t\t\t\t\t\t\t\t\t";
                }
                text += row.source.empty() ? std::string("\t\t\t") : fmt::format("{0}\t{1}\t{2:08x}\t", row.source, row.bytes, row.crc32);
                if (row.described)
                {
                    text += fmt::format("{0}\t{1}\t{2}\t", SkipPixels(row.description), IndexRunsText(IndexRunsOf(row.description.palette.UsedEntries())),
                        IndexRunsText(IndexRunsOf(PixelEntriesOf(row.description))));
                }
                else
                {
                    text += "\t\t\t";
                }
                text += row.error + "\n";
            }
            return text;
        }

        // What the export of one pic did.
        struct PicExportResult
        {
            size_t cels = 0;
            // The pic has no cel, and the export of every pic skipped it.
            bool skipped = false;
            // The failure is a file that could not be written.
            bool writeFailed = false;
        };

        // A file of the export, made in memory.
        struct ExportFile
        {
            std::string path;
            std::vector<uint8_t> bytes;
            // A text file: each line end becomes CR LF.
            std::string text;
            bool isText = false;
        };

        sci::Status ExportPic(GameSession &session, int number, bool everyPic, bool everyCel, const std::string &outDir, bool dryRun, CliOutput &output, PicExportResult &result)
        {
            SCI_TRY_ASSIGN(PicSource source, ReadPic(session, number));
            const SCIVersion &version = session.Version();
            SCI_TRY_ASSIGN(PicDescription description, DescribePic(version, source.data));
            if (description.cels.empty())
            {
                if (everyPic)
                {
                    result.skipped = true;
                    return sci::Ok();
                }
                return sci::Fail(sci::ErrorCode::Unsupported, "the pic has no cel to export");
            }

            // The colours as SCI Companion shows the pic: the palette of the
            // pic, and palette 999 of the game for the entries that the pic
            // does not use.
            SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> entity, TryCreateResourceFromResourceData(*source.blob));
            std::unique_ptr<PaletteComponent> merged = session.ResourceMap().GetMergedPalette(*entity, 999);
            if (!merged)
            {
                return sci::Fail(sci::ErrorCode::Internal, "the palette of the pic could not be made");
            }

            PicSidecar sidecar = MakePicSidecar(description);
            sidecar.pic = number;
            sidecar.from = SourceText(source);
            sidecar.size = (uint32_t)source.data.size();
            sidecar.crc32 = source.crc32;
            sidecar.palette999 = IndexRunsOf(UsedEntriesOf(session.ResourceMap().GetPalette999()));
            sidecar.used = IndexRunsOf(UsedEntriesOf(merged.get()));

            // Each file is made before the first is written, so a pic with a
            // cel that cannot be exported writes no file.
            std::vector<ExportFile> files;
            for (const PicCelDescription &cel : description.cels)
            {
                const bool background = (cel.index == description.backgroundCel);
                if (!background && !everyCel)
                {
                    continue;
                }
                if (cel.compressed)
                {
                    return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("cel {0} is compressed; only a cel that is not compressed can be exported", cel.index));
                }
                IndexedBmp image;
                image.width = cel.width;
                image.height = cel.height;
                SCI_TRY_ASSIGN(image.pixels, GetCelPixels(version, source.data, cel.index));
                for (size_t i = 0; i < image.colors.size(); i++)
                {
                    image.colors[i].red = merged->Colors[i].rgbRed;
                    image.colors[i].green = merged->Colors[i].rgbGreen;
                    image.colors[i].blue = merged->Colors[i].rgbBlue;
                }
                ExportFile file;
                std::string name = background ? fmt::format("{0}.bmp", number) : fmt::format("{0}.cel{1}.bmp", number, cel.index);
                file.path = outDir + "\\" + name;
                SCI_TRY_ASSIGN(file.bytes, EncodeIndexedBmp(image));
                files.push_back(std::move(file));
                sidecar.cels[cel.index].file = name;
                result.cels++;
            }
            ExportFile sidecarFile;
            sidecarFile.path = outDir + "\\" + fmt::format("{0}.toml", number);
            sidecarFile.text = PicSidecarText(sidecar);
            sidecarFile.isText = true;
            files.push_back(std::move(sidecarFile));

            // Each file that is there already must open for writing, so that a
            // file that cannot be replaced fails the pic before its first
            // write. The write shares read and write (WriteBytesToFile).
            for (const ExportFile &file : files)
            {
                sci::Status writable = CheckFileCanBeReplaced(file.path, FILE_SHARE_READ | FILE_SHARE_WRITE);
                if (!writable)
                {
                    result.writeFailed = true;
                    return writable;
                }
            }
            for (const ExportFile &file : files)
            {
                if (dryRun)
                {
                    output.Message("Would write " + file.path);
                    continue;
                }
                sci::Status written = file.isText ? WriteTextToFile(file.path, file.text) : WriteBytesToFile(file.path, file.bytes);
                if (!written)
                {
                    result.writeFailed = true;
                    return written;
                }
                output.Detail("Wrote " + file.path);
            }
            return sci::Ok();
        }
    }

    sci::Result<ExitCode> RunPicList(GameSession &session, const PicListOptions &options, CliOutput &output)
    {
        std::vector<int> pics = PicNumbersOf(session);
        if (!options.pics.empty())
        {
            SCI_TRY_ASSIGN(pics, SelectPics(pics, options.pics));
        }
        std::vector<PicRow> rows;
        for (int number : pics)
        {
            if (CancelFlag().load())
            {
                // Ctrl+C while the pics were read: no table, and the run
                // fails (exit code 7).
                return sci::Fail(sci::ErrorCode::Cancelled, "stopped by Ctrl+C");
            }
            SetCurrentItem("reading " + PicText(number));
            rows.push_back(RowOf(session, number));
        }
        SetCurrentItem("");
        output.Result((options.format == "tsv") ? TsvTable(rows) : TextTable(rows));
        bool unreadable = std::any_of(rows.begin(), rows.end(), [](const PicRow &row) { return !row.error.empty(); });
        return unreadable ? ExitCode::PartialFailure : ExitCode::Success;
    }

    sci::Result<ExitCode> RunPicExport(GameSession &session, const PicExportOptions &options, const CommonOptions &common, CliOutput &output)
    {
        const std::string outDir = AbsolutePath(options.outDir);
        SCI_TRY(CheckOutputFolder(session, outDir));
        std::vector<int> pics = PicNumbersOf(session);
        if (!options.all)
        {
            SCI_TRY_ASSIGN(pics, SelectPics(pics, options.pics));
        }

        ReportFacts facts;
        size_t exported = 0;
        size_t cels = 0;
        size_t skipped = 0;
        size_t failed = 0;
        for (int number : pics)
        {
            if (CancelFlag().load())
            {
                facts.cancelled = true;
                break;
            }
            SetCurrentItem("exporting " + PicText(number));
            PicExportResult result;
            // The exception boundary of the pic.
            sci::Status status = sci::Guard("exporting " + PicText(number), [&]() -> sci::Status
            {
                return ExportPic(session, number, options.all, options.cel == "all", outDir, common.dryRun, output, result);
            });
            if (!status)
            {
                failed++;
                output.Error(PicText(number) + ": " + status.error().ToString());
                if (status.error().code == sci::ErrorCode::Internal)
                {
                    facts.internal = true;
                }
                else if (result.writeFailed)
                {
                    facts.writeFailed = true;
                }
                else
                {
                    facts.otherFailures = true;
                }
            }
            else if (result.skipped)
            {
                skipped++;
                output.Detail(PicText(number) + " has no cel: skipped");
            }
            else
            {
                exported++;
                cels += result.cels;
            }
        }
        SetCurrentItem("");

        std::string summary = fmt::format("{0} {1} of {2} ({3}) into {4}", common.dryRun ? "A run would export" : "Exported", exported, CountText(pics.size(), "pic"), CountText(cels, "cel"), outDir);
        if (skipped > 0)
        {
            summary += fmt::format("; {0} no cel", (skipped == 1) ? "1 pic has" : fmt::format("{0} pics have", skipped));
        }
        if (failed > 0)
        {
            summary += fmt::format("; {0} failed", CountText(failed, "pic"));
        }
        if (facts.cancelled)
        {
            summary += "; stopped by Ctrl+C";
        }
        output.Message(summary + ".");
        return ExitCodeForFacts(facts);
    }
}
