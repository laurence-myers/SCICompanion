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

    namespace
    {
        // The image of one cel of an import.
        struct ImportCel
        {
            // Below 0: the background cel of the pic.
            int cel = -1;
            std::string imagePath;
        };

        // The images of an import for one pic.
        struct PicImportJob
        {
            int number = 0;
            std::vector<ImportCel> cels;
            // The CRC-32 that the sidecar of the images has for the pic.
            bool hasCrc32 = false;
            uint32_t crc32 = 0;
            std::string sidecarPath;
        };

        // What the import of one pic did.
        struct PicImportResult
        {
            size_t cels = 0;
            // The failure is a patch file that could not be written.
            bool writeFailed = false;
        };

        // The job of a sidecar file: the pic, and the cels that have a file.
        // The files are in the folder of the sidecar.
        sci::Result<PicImportJob> JobOfSidecar(const std::string &sidecarPath)
        {
            SCI_TRY_ASSIGN(PicSidecar sidecar, ReadPicSidecarFile(sidecarPath));
            PicImportJob job;
            job.number = sidecar.pic;
            job.hasCrc32 = sidecar.hasCrc32;
            job.crc32 = sidecar.crc32;
            job.sidecarPath = sidecarPath;
            const std::string folder = fs::path(sidecarPath).parent_path().string();
            for (const PicSidecarCel &cel : sidecar.cels)
            {
                if (!cel.file.empty())
                {
                    ImportCel importCel;
                    importCel.cel = cel.index;
                    importCel.imagePath = folder + "\\" + cel.file;
                    job.cels.push_back(importCel);
                }
            }
            if (job.cels.empty())
            {
                sci::ErrorLocation where;
                where.file = sidecarPath;
                return sci::Fail(sci::ErrorCode::Usage, "the file names no image file of a cel", where);
            }
            return job;
        }

        // Checks the new pixels of a cel against its pixels in the game: a
        // pixel has the skip colour where the cel has it and nowhere else,
        // and each other pixel is a used entry. Usage for a pixel that
        // fails a check; an unused entry is a warning when the options allow
        // it.
        sci::Status CheckNewPixels(const std::string &imagePath, const PicCelDescription &cel, const std::vector<uint8_t> &oldPixels, const std::vector<uint8_t> &newPixels,
            const std::array<bool, 256> &used, bool allowUnused, std::vector<std::string> &warnings)
        {
            size_t skipChanges = 0;
            size_t firstSkipChange = 0;
            size_t unusedPixels = 0;
            std::array<bool, 256> unused = {};
            for (size_t i = 0; i < newPixels.size(); i++)
            {
                const bool wasSkip = (oldPixels[i] == cel.skipColor);
                const bool isSkip = (newPixels[i] == cel.skipColor);
                if (wasSkip != isSkip)
                {
                    if (skipChanges == 0)
                    {
                        firstSkipChange = i;
                    }
                    skipChanges++;
                }
                else if (!isSkip && !used[newPixels[i]])
                {
                    unused[newPixels[i]] = true;
                    unusedPixels++;
                }
            }
            if (skipChanges > 0)
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}: {1} to or from the skip colour {2} of cel {3} (the first at {4},{5}); the pixels of the skip colour must stay where they are",
                    imagePath, (skipChanges == 1) ? std::string("1 pixel changes") : fmt::format("{0} pixels change", skipChanges), (int)cel.skipColor, cel.index,
                    firstSkipChange % (size_t)cel.width, firstSkipChange / (size_t)cel.width));
            }
            if (unusedPixels > 0)
            {
                const std::string text = fmt::format("{0}: {1} of cel {2} {3} palette entries that the pic and palette 999 do not mark as used: {4}",
                    imagePath, CountText(unusedPixels, "pixel"), cel.index, (unusedPixels == 1) ? "has" : "have", IndexRunsText(IndexRunsOf(unused)));
                if (!allowUnused)
                {
                    return sci::Fail(sci::ErrorCode::Usage, text + " (--allow-unused-indices makes this a warning)");
                }
                warnings.push_back(text);
            }
            return sci::Ok();
        }

        sci::Status ImportPic(GameSession &session, const PicImportJob &job, const PicImportPalette &palette, bool allowUnused, const std::string &outDir, bool dryRun, CliOutput &output, PicImportResult &result)
        {
            const std::string picText = PicText(job.number);
            SCI_TRY_ASSIGN(PicSource source, ReadPic(session, job.number));
            const SCIVersion &version = session.Version();
            SCI_TRY_ASSIGN(PicDescription description, DescribePic(version, source.data));
            if (job.hasCrc32 && (job.crc32 != source.crc32))
            {
                output.Warning(fmt::format("{0}: {1} has the CRC-32 {2:08x}, and the pic in the game has {3:08x}: the images are of another copy of the pic", picText, job.sidecarPath, job.crc32, source.crc32));
            }

            // The entries that the game has a colour for: those that the
            // pic or palette 999 marks as used (the palette of export).
            SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> entity, TryCreateResourceFromResourceData(*source.blob));
            std::unique_ptr<PaletteComponent> merged = session.ResourceMap().GetMergedPalette(*entity, 999);
            if (!merged)
            {
                return sci::Fail(sci::ErrorCode::Internal, "the palette of the pic could not be made");
            }
            const std::array<bool, 256> used = UsedEntriesOf(merged.get());

            // Each image is read, and then all are checked, before the pic
            // changes.
            std::vector<PicImportImage> images;
            for (const ImportCel &importCel : job.cels)
            {
                PicImportImage image;
                image.cel = importCel.cel;
                image.name = importCel.imagePath;
                SCI_TRY_ASSIGN(image.image, ReadIndexedBmpFile(importCel.imagePath));
                images.push_back(std::move(image));
            }
            std::vector<std::string> warnings;
            sci::Result<std::vector<uint8_t>> made = MakePicImportData(version, source.data, description, used, images, palette, allowUnused, warnings);
            for (const std::string &warning : warnings)
            {
                output.Warning(picText + ": " + warning);
            }
            SCI_TRY_ASSIGN(std::vector<uint8_t> data, std::move(made));
            SCI_TRY(CheckResourceSize(version, (DWORD)data.size(), ResourceType::Pic));

            const std::string path = outDir + "\\" + GetFileNameFor(ResourceType::Pic, job.number, NoBase36, version);
            // The write of a patch file shares nothing (ResourceBlob::SaveToFile).
            sci::Status writable = CheckFileCanBeReplaced(path, 0);
            if (!writable)
            {
                result.writeFailed = true;
                return writable;
            }
            result.cels = images.size();
            if (dryRun)
            {
                output.Message("Would write " + path);
                return sci::Ok();
            }
            ResourceBlob blob(session.Helper(), nullptr, ResourceType::Pic, data, version.DefaultVolumeFile, job.number, NoBase36, version, ResourceSourceFlags::PatchFile);
            HRESULT hr = blob.SaveToFile(path);
            if (FAILED(hr))
            {
                result.writeFailed = true;
                return sci::Fail(sci::FromHResult(hr, "Writing " + path));
            }
            output.Detail("Wrote " + path);
            return sci::Ok();
        }
    }

    sci::Result<std::vector<uint8_t>> MakePicImportData(const SCIVersion &version, const std::vector<uint8_t> &data, const PicDescription &description, const std::array<bool, 256> &usedEntries,
        const std::vector<PicImportImage> &images, const PicImportPalette &palette, bool allowUnusedEntries, std::vector<std::string> &warnings)
    {
        // The cel of each image, in the order of the images.
        std::vector<int> cels;
        // The first image: its colour table has the colours of the range.
        const PicImportImage *rangeColors = nullptr;
        for (const PicImportImage &image : images)
        {
            const int index = (image.cel < 0) ? description.backgroundCel : image.cel;
            if ((index < 0) || ((size_t)index >= description.cels.size()))
            {
                return sci::Fail(sci::ErrorCode::Usage, (image.cel < 0) ? std::string("the pic has no cel") :
                    fmt::format("the pic has no cel {0}; it has {1}", image.cel, CountText(description.cels.size(), "cel")));
            }
            if (std::find(cels.begin(), cels.end(), index) != cels.end())
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("two images are for cel {0}", index));
            }
            const PicCelDescription &cel = description.cels[index];
            if (cel.compressed)
            {
                return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("cel {0} is compressed; only a cel that is not compressed can get new pixels", index));
            }
            if ((image.image.width != cel.width) || (image.image.height != cel.height))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0} has {1} x {2} pixels, and cel {3} has {4} x {5}", image.name, image.image.width, image.image.height, index, cel.width, cel.height));
            }
            SCI_TRY_ASSIGN(std::vector<uint8_t> oldPixels, GetCelPixels(version, data, index));
            SCI_TRY(CheckNewPixels(image.name, cel, oldPixels, image.image.pixels, usedEntries, allowUnusedEntries, warnings));
            if (palette.replace)
            {
                if (image.image.colorCount <= palette.last)
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}: the colour table has {1} {2}, and the palette range is {3}-{4}", image.name, image.image.colorCount,
                        (image.image.colorCount == 1) ? "entry" : "entries", palette.first, palette.last));
                }
                if (rangeColors == nullptr)
                {
                    rangeColors = &image;
                }
                else
                {
                    for (int entry = palette.first; entry <= palette.last; entry++)
                    {
                        if (image.image.colors[entry] != rangeColors->image.colors[entry])
                        {
                            return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0} and {1} have another colour for entry {2}; the images of a pic must have one palette", rangeColors->name, image.name, entry));
                        }
                    }
                }
            }
            cels.push_back(index);
        }

        std::vector<uint8_t> newData = data;
        if (rangeColors != nullptr)
        {
            SCI_TRY_ASSIGN(newData, ReplacePaletteRange(version, newData, palette.first, palette.last, rangeColors->image.colors));
            // The range is inside the stored entries here. An entry with the
            // flag 0 gets its new colour, but the game does not use it.
            std::array<bool, 256> notUsed = {};
            bool anyNotUsed = false;
            for (int entry = palette.first; entry <= palette.last; entry++)
            {
                if (description.palette.flags[entry] == 0)
                {
                    notUsed[entry] = true;
                    anyNotUsed = true;
                }
            }
            if (anyNotUsed)
            {
                warnings.push_back(fmt::format("the pic does not mark the entries {0} as used, so the game takes their colours from another palette", IndexRunsText(IndexRunsOf(notUsed))));
            }
        }
        for (size_t i = 0; i < images.size(); i++)
        {
            SCI_TRY_ASSIGN(newData, ReplaceCelPixels(version, newData, cels[i], images[i].image.pixels));
        }
        return newData;
    }

    bool ParsePaletteRange(const std::string &text, int &first, int &last)
    {
        size_t dash = text.find('-');
        if (dash == std::string::npos)
        {
            return false;
        }
        int low = 0;
        int high = 0;
        if (!ParseNumber(text.substr(0, dash), low) || !ParseNumber(text.substr(dash + 1), high) || (high > 255) || (low > high))
        {
            return false;
        }
        first = low;
        last = high;
        return true;
    }

    sci::Result<ExitCode> RunPicImport(GameSession &session, const PicImportOptions &options, const CommonOptions &common, CliOutput &output)
    {
        const std::string outDir = AbsolutePath(options.outDir);
        SCI_TRY(CheckOutputFolder(session, outDir));
        PicImportPalette palette;
        palette.replace = (options.palette == "replace");
        if (palette.replace && !ParsePaletteRange(options.paletteRange, palette.first, palette.last))
        {
            return sci::Fail(sci::ErrorCode::Usage, "--palette replace needs --palette-range first-last, two entries of 0 to 255 (for example 80-235)");
        }

        // A job, or the sidecar file that gives it (read in the exception
        // boundary of its pic).
        struct Work
        {
            PicImportJob job;
            std::string sidecarPath;
        };
        std::vector<Work> works;
        if (!options.inDir.empty())
        {
            const std::string inDir = AbsolutePath(options.inDir);
            std::error_code ec;
            if (!fs::is_directory(inDir, ec))
            {
                return sci::Fail(sci::ErrorCode::NotFound, "the input folder does not exist: " + inDir);
            }
            std::vector<std::string> sidecars;
            for (const auto &entry : fs::directory_iterator(inDir, ec))
            {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](char ch) { return (char)tolower((unsigned char)ch); });
                if (entry.is_regular_file(ec) && (extension == ".toml"))
                {
                    sidecars.push_back(entry.path().string());
                }
            }
            if (sidecars.empty())
            {
                return sci::Fail(sci::ErrorCode::Usage, "the input folder has no .toml file of scic pic export: " + inDir);
            }
            std::sort(sidecars.begin(), sidecars.end());
            for (const std::string &sidecar : sidecars)
            {
                Work work;
                work.sidecarPath = sidecar;
                works.push_back(work);
            }
        }
        else
        {
            Work work;
            if (!ParseNumber(options.pic, work.job.number))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("\"{0}\" is not a pic number", options.pic));
            }
            std::vector<int> pics = PicNumbersOf(session);
            if (std::find(pics.begin(), pics.end(), work.job.number) == pics.end())
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("the game has no pic {0}", work.job.number));
            }
            ImportCel cel;
            cel.cel = options.celGiven ? options.cel : -1;
            cel.imagePath = AbsolutePath(options.image);
            work.job.cels.push_back(cel);
            works.push_back(work);
        }

        ReportFacts facts;
        size_t imported = 0;
        size_t cels = 0;
        size_t failed = 0;
        std::set<int> done;
        for (Work &work : works)
        {
            if (CancelFlag().load())
            {
                facts.cancelled = true;
                break;
            }
            // The name of the work in a message: the sidecar file until its
            // pic is known.
            std::string name = work.sidecarPath.empty() ? PicText(work.job.number) : work.sidecarPath;
            SetCurrentItem("importing " + name);
            PicImportResult result;
            // The exception boundary of the pic.
            sci::Status status = sci::Guard("importing " + name, [&]() -> sci::Status
            {
                if (!work.sidecarPath.empty())
                {
                    SCI_TRY_ASSIGN(work.job, JobOfSidecar(work.sidecarPath));
                    name = PicText(work.job.number);
                    SetCurrentItem("importing " + name);
                }
                if (!done.insert(work.job.number).second)
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0} is in two .toml files of the input folder; the second is {1}", name, work.sidecarPath));
                }
                return ImportPic(session, work.job, palette, options.allowUnusedIndices, outDir, common.dryRun, output, result);
            });
            if (!status)
            {
                failed++;
                output.Error(name + ": " + status.error().ToString());
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
            else
            {
                imported++;
                cels += result.cels;
            }
        }
        SetCurrentItem("");

        std::string summary = fmt::format("{0} {1} of {2} ({3}) into {4}", common.dryRun ? "A run would import" : "Imported", imported, CountText(works.size(), "pic"), CountText(cels, "cel"), outDir);
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
