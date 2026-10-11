#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "CliPic.h"
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

        // A number of 0 to 65535: digits only, with any count of zeros before
        // them.
        bool ParseNumber(const std::string &text, int &number)
        {
            if (text.empty())
            {
                return false;
            }
            int value = 0;
            for (char ch : text)
            {
                if ((ch < '0') || (ch > '9'))
                {
                    return false;
                }
                value = (value * 10) + (ch - '0');
                if (value > 65535)
                {
                    return false;
                }
            }
            number = value;
            return true;
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

        // The CRC-32 of the bytes of a pic resource.
        uint32_t Crc32Of(const std::vector<uint8_t> &data)
        {
            return data.empty() ? 0 : (uint32_t)crcFast(data.data(), (int)data.size());
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
            source.crc32 = Crc32Of(source.data);
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
        // Usage when it is the game folder or a folder inside it (a game can
        // read patch files from a folder of its own). The folders are
        // compared as files, so another name of a folder (another case, a
        // short name, a junction) is the same folder.
        sci::Status CheckOutputFolder(GameSession &session, const std::string &outDir)
        {
            std::error_code ec;
            if (!fs::is_directory(outDir, ec))
            {
                return sci::Fail(sci::ErrorCode::NotFound, "the output folder does not exist: " + outDir);
            }
            const fs::path game = session.Helper().GameFolder;
            // The path with no link in it, so that each parent of the text is
            // a parent of the folder.
            fs::path folder = fs::canonical(outDir, ec);
            if (ec)
            {
                folder = fs::absolute(outDir, ec);
            }
            for (bool first = true;; first = false)
            {
                if (fs::equivalent(folder, game, ec))
                {
                    return sci::Fail(sci::ErrorCode::Usage, first ? "the output folder is the game folder; give a folder outside the game" :
                        "the output folder is inside the game folder; give a folder outside the game");
                }
                const fs::path parent = folder.parent_path();
                if (parent.empty() || (parent == folder))
                {
                    return sci::Ok();
                }
                folder = parent;
            }
        }

        // The row of a pic of the game. The exception boundary of the pic: a
        // pic that cannot be read has the error in its row.
        PicListRow RowOf(GameSession &session, int number)
        {
            sci::Result<PicListRow> row = sci::Guard("reading " + PicText(number), [&]() -> sci::Result<PicListRow>
            {
                SCI_TRY_ASSIGN(PicSource source, ReadPic(session, number));
                return MakePicListRow(session.Version(), number, source.data, source.fromPatch);
            });
            if (row)
            {
                return *row;
            }
            PicListRow unread;
            unread.number = number;
            unread.format = PicFormatName(session.Version().PicFormat);
            unread.error = row.error().ToString();
            return unread;
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
        std::vector<std::string> TextCells(const PicListRow &row)
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

        // What the export of one pic did.
        struct PicExportResult
        {
            size_t cels = 0;
            // The pic has no cel, and the export of every pic skipped it.
            bool skipped = false;
            // The failure is a file that could not be written.
            bool writeFailed = false;
        };

        sci::Status ExportPic(GameSession &session, int number, bool everyPic, bool everyCel, const std::string &outDir, bool dryRun, CliOutput &output, PicExportResult &result)
        {
            SCI_TRY_ASSIGN(PicSource source, ReadPic(session, number));
            const SCIVersion &version = session.Version();
            SCI_TRY_ASSIGN(PicDescription description, DescribePic(version, source.data));
            if (description.cels.empty() && everyPic)
            {
                result.skipped = true;
                return sci::Ok();
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

            // Each file is made before the first is written, so a pic with a
            // cel that cannot be exported writes no file.
            SCI_TRY_ASSIGN(std::vector<PicExportFile> files, MakePicExportFiles(version, number, source.data, description, source.fromPatch, *merged, session.ResourceMap().GetPalette999(), everyCel));

            // Each file that is there already must open for writing, so that a
            // file that cannot be replaced fails the pic before its first
            // write. The write shares read and write (WriteBytesToFile).
            for (const PicExportFile &file : files)
            {
                sci::Status writable = CheckFileCanBeReplaced(outDir + "\\" + file.name, FILE_SHARE_READ | FILE_SHARE_WRITE);
                if (!writable)
                {
                    result.writeFailed = true;
                    return writable;
                }
            }
            for (const PicExportFile &file : files)
            {
                const std::string path = outDir + "\\" + file.name;
                if (!file.isText)
                {
                    result.cels++;
                }
                if (dryRun)
                {
                    output.Message("Would write " + path);
                    continue;
                }
                sci::Status written = file.isText ? WriteTextToFile(path, file.text) : WriteBytesToFile(path, file.bytes);
                if (!written)
                {
                    result.writeFailed = true;
                    return written;
                }
                output.Detail("Wrote " + path);
            }
            return sci::Ok();
        }
    }

    PicListRow MakePicListRow(const SCIVersion &version, int number, const std::vector<uint8_t> &data, bool fromPatch)
    {
        PicListRow row;
        row.number = number;
        row.format = PicFormatName(version.PicFormat);
        row.source = fromPatch ? "patch" : "package";
        row.bytes = data.size();
        row.crc32 = Crc32Of(data);
        if (CanReadPicImage(version.PicFormat))
        {
            // The exception boundary of the description.
            sci::Result<PicDescription> description = sci::Guard(fmt::format("reading pic {0}", number), [&]() { return DescribePic(version, data); });
            if (description)
            {
                row.description = *description;
                row.described = true;
            }
            else
            {
                row.error = description.error().ToString();
            }
        }
        return row;
    }

    // Aligned columns: the numbers to the right, the texts to the left. The
    // entries of the pixels are the last column: they can be long.
    std::string PicListTextTable(const std::vector<PicListRow> &rows)
    {
        const std::vector<std::string> header = { "Pic", "Format", "Size", "Cels", "Comp", "Background cel", "Source", "Bytes", "CRC32", "Skip px", "Palette entries", "Pixel entries" };
        const std::vector<bool> right = { true, false, false, true, true, false, false, true, false, true, false, false };
        std::vector<std::vector<std::string>> table = { header };
        for (const PicListRow &row : rows)
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
            // No spaces at the end of a line, and none before the error but
            // the two that divide it from the last cell.
            line.erase(line.find_last_not_of(' ') + 1);
            if ((r > 0) && !rows[r - 1].error.empty())
            {
                line += "  (unreadable: " + rows[r - 1].error + ")";
            }
            text += line + "\n";
        }
        return text;
    }

    std::string PicListTsvTable(const std::vector<PicListRow> &rows)
    {
        std::string text = PicListTsvHeader;
        for (const PicListRow &row : rows)
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

    sci::Result<std::vector<PicExportFile>> MakePicExportFiles(const SCIVersion &version, int number, const std::vector<uint8_t> &data, const PicDescription &description, bool fromPatch,
        const PaletteComponent &colors, const PaletteComponent *palette999, bool everyCel)
    {
        if (description.cels.empty())
        {
            return sci::Fail(sci::ErrorCode::Unsupported, "the pic has no cel to export");
        }
        PicSidecar sidecar = MakePicSidecar(description);
        sidecar.pic = number;
        sidecar.from = fromPatch ? "patch" : "package";
        sidecar.size = (uint32_t)data.size();
        sidecar.crc32 = Crc32Of(data);
        sidecar.palette999 = IndexRunsOf(UsedEntriesOf(palette999));
        sidecar.used = IndexRunsOf(UsedEntriesOf(&colors));

        std::vector<PicExportFile> files;
        for (const PicCelDescription &cel : description.cels)
        {
            const bool background = (cel.index == description.backgroundCel);
            if (!background && !everyCel)
            {
                continue;
            }
            IndexedBmp image;
            image.width = cel.width;
            image.height = cel.height;
            // Unsupported for a cel with run-length data in an SCI2 pic.
            SCI_TRY_ASSIGN(image.pixels, GetCelPixels(version, data, cel.index));
            for (size_t i = 0; i < image.colors.size(); i++)
            {
                image.colors[i].red = colors.Colors[i].rgbRed;
                image.colors[i].green = colors.Colors[i].rgbGreen;
                image.colors[i].blue = colors.Colors[i].rgbBlue;
            }
            PicExportFile file;
            file.name = background ? fmt::format("{0}.bmp", number) : fmt::format("{0}.cel{1}.bmp", number, cel.index);
            SCI_TRY_ASSIGN(file.bytes, EncodeIndexedBmp(image));
            sidecar.cels[cel.index].file = file.name;
            files.push_back(std::move(file));
        }
        PicExportFile sidecarFile;
        sidecarFile.name = fmt::format("{0}.toml", number);
        sidecarFile.text = PicSidecarText(sidecar);
        sidecarFile.isText = true;
        files.push_back(std::move(sidecarFile));
        return files;
    }

    sci::Result<ExitCode> RunPicList(GameSession &session, const PicListOptions &options, CliOutput &output)
    {
        std::vector<int> pics = PicNumbersOf(session);
        if (!options.pics.empty())
        {
            SCI_TRY_ASSIGN(pics, SelectPics(pics, options.pics));
        }
        std::vector<PicListRow> rows;
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
        output.Result((options.format == "tsv") ? PicListTsvTable(rows) : PicListTextTable(rows));
        bool unreadable = std::any_of(rows.begin(), rows.end(), [](const PicListRow &row) { return !row.error.empty(); });
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
