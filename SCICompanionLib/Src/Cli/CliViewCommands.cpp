#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "CliResources.h"
#include "CliView.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceSourceFlags.h"
#include "ResourceUtil.h"
#include "PicImage.h"
#include "ViewPalette.h"
#include "IndexedBmp.h"
#include "FileWrite.h"
#include "ViewImage.h"
#include "ViewSidecar.h"
#include "PaletteUse.h"
#include "ResourceEntity.h"
#include "format.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace cli
{
    namespace
    {
        std::string ViewText(int number)
        {
            return fmt::format("view {0}", number);
        }

        std::string RunsText(const std::array<bool, 256> &entries)
        {
            return IndexRunsText(IndexRunsOf(entries));
        }

        // A view of the game: the most recent copy of its resource (a patch
        // file, else the package).
        struct ViewSource
        {
            std::unique_ptr<ResourceBlob> blob;
            std::vector<uint8_t> data;
            bool fromPatch = false;
        };

        // NotFound when the game has no such view; Format when its resource
        // is damaged.
        sci::Result<ViewSource> ReadViewSource(GameSession &session, int number)
        {
            ViewSource source;
            source.blob = session.ResourceMap().MostRecentResource(ResourceType::View, number, false);
            if (!source.blob)
            {
                return sci::Fail(sci::ErrorCode::NotFound, fmt::format("the game has no view {0}", number));
            }
            SCI_TRY(CheckResourceData(*source.blob));
            const uint8_t *bytes = source.blob->GetData();
            source.data.assign(bytes, bytes + source.blob->GetLength());
            source.fromPatch = IsFlagSet(source.blob->GetSourceFlags(), ResourceSourceFlags::PatchFile);
            return std::move(source);
        }

        // The row of a view of the game. The exception boundary of the view:
        // a view that cannot be read, or whose reader repaired damaged data,
        // has the error in its row.
        ViewListRow RowOf(GameSession &session, int number)
        {
            RepairLogSink repairs;
            sci::Result<ViewListRow> row = [&]() -> sci::Result<ViewListRow>
            {
                ScopedCoreLogSink scopedSink(repairs);
                return sci::Guard("reading " + ViewText(number), [&]() -> sci::Result<ViewListRow>
                {
                    SCI_TRY_ASSIGN(ViewSource source, ReadViewSource(session, number));
                    return MakeViewListRow(session.Version(), number, source.data, source.fromPatch);
                });
            }();
            const std::string repaired = repairs.Text();
            if (row && repaired.empty())
            {
                return *row;
            }
            ViewListRow unread = row ? *row : ViewListRow();
            unread.number = number;
            unread.format = ViewFormatName(session.Version().ViewFormat);
            unread.described = false;
            if (repaired.empty())
            {
                unread.error = row.error().ToString();
            }
            else
            {
                sci::Error error;
                error.code = sci::ErrorCode::Format;
                error.message = "the reader repaired damaged data: " + repaired;
                unread.error = error.ToString();
            }
            return unread;
        }

        // "0-255 (4)": the entries that a palette stores, and the bytes of an
        // entry; "none" for a palette that stores no entry; "-" for no
        // palette.
        std::string StoredCell(const PicPaletteDescription &palette)
        {
            if (!palette.present)
            {
                return "-";
            }
            if (palette.entryCount == 0)
            {
                return "none";
            }
            return fmt::format("{0}-{1} ({2})", palette.firstEntry, palette.firstEntry + palette.entryCount - 1, palette.entrySize);
        }

        uint32_t SkipPixelsOf(const ViewDescription &description)
        {
            uint32_t pixels = 0;
            for (const ViewCelDescription &cel : description.cels)
            {
                pixels += cel.SkipPixels();
            }
            return pixels;
        }

        // The resolution byte of an SCI2 view; empty for another format.
        std::string ResolutionCell(const ViewDescription &description)
        {
            return (description.format == ViewFormat::VGA2) ? fmt::format("{0}", (int)description.resolution) : std::string();
        }

        // A cell with "-" for no value.
        std::string OrDash(const std::string &text)
        {
            return text.empty() ? std::string("-") : text;
        }

        // The cells of the row for the text table, in the order of the
        // header.
        std::vector<std::string> TextCells(const ViewListRow &row)
        {
            std::vector<std::string> cells = { fmt::format("{0}", row.number), row.format };
            const ViewDescription &view = row.description;
            if (row.described)
            {
                cells.push_back(fmt::format("{0}", view.loops.size()));
                cells.push_back(fmt::format("{0}", view.MirrorLoops()));
                cells.push_back(fmt::format("{0}", view.cels.size()));
                cells.push_back(fmt::format("{0}", view.RawCels()));
                cells.push_back(OrDash(ResolutionCell(view)));
            }
            else
            {
                cells.insert(cells.end(), 5, "-");
            }
            cells.push_back(OrDash(row.source));
            cells.push_back(row.source.empty() ? std::string("-") : fmt::format("{0}", row.bytes));
            cells.push_back(row.source.empty() ? std::string("-") : fmt::format("{0:08x}", row.crc32));
            if (row.described)
            {
                cells.push_back(StoredCell(view.palette));
                cells.push_back(fmt::format("{0}", SkipPixelsOf(view)));
                cells.push_back(OrDash(RunsText(view.palette.UsedEntries())));
                cells.push_back(OrDash(RunsText(view.PixelEntries())));
            }
            else
            {
                cells.insert(cells.end(), 4, "-");
            }
            return cells;
        }

        // "0-235" for the entries that a palette stores; "no entry" for none.
        std::string StoredText(const PaletteLayout &layout)
        {
            if (layout.entryCount == 0)
            {
                return "no entry";
            }
            return fmt::format("{0}-{1}", layout.firstEntry, layout.firstEntry + layout.entryCount - 1);
        }

        // The action of a run: new colours for the range, or no mark for it.
        struct PaletteAction
        {
            bool clearUsed = false;
            int first = 0;
            int last = 0;
            std::array<IndexedBmpColor, 256> colors = {};
        };

        // What the command did with one view.
        struct ViewPaletteResult
        {
            // The view has no change, and gets no patch file.
            bool unchanged = false;
            // A patch file could not be written or removed.
            bool writeFailed = false;
        };

        // The messages of a view after the action with new colours.
        void ReportColors(const std::string &viewText, const PaletteAction &action, const ViewPaletteEdit &edit, CliOutput &output)
        {
            if (!edit.AnyEntry())
            {
                output.Warning(fmt::format("{0}: its palette stores {1}, and no entry of the range {2}-{3}: the view does not change, and gets no patch file", viewText, StoredText(edit.before), action.first, action.last));
                return;
            }
            if (!edit.changed)
            {
                output.Warning(fmt::format("{0}: the entries {1} of its palette have the colours of the file: the view does not change, and gets no patch file", viewText, RunsText(edit.entries)));
                return;
            }
            output.Detail(fmt::format("{0}: new colours for the entries {1}", viewText, RunsText(edit.entries)));
            std::array<bool, 256> notUsed = {};
            for (size_t i = 0; i < notUsed.size(); i++)
            {
                notUsed[i] = edit.entries[i] && !edit.usedBefore[i];
            }
            const std::string notUsedText = RunsText(notUsed);
            if (!notUsedText.empty())
            {
                output.Warning(fmt::format("{0}: its palette does not mark the entries {1} as used, so the game takes their colours from another palette", viewText, notUsedText));
            }
        }

        // The messages of a view after the action that takes marks.
        void ReportClear(const std::string &viewText, const PaletteAction &action, const ViewPaletteEdit &edit, CliOutput &output)
        {
            if (!edit.changed)
            {
                output.Warning(fmt::format("{0}: its palette has no mark for an entry of the range {1}-{2}: the view does not change, and gets no patch file", viewText, action.first, action.last));
                return;
            }
            if (edit.after.entryCount != edit.before.entryCount)
            {
                output.Detail(fmt::format("{0}: its palette stores {1} (before: {2}), so the entries {3} have no mark", viewText, StoredText(edit.after), StoredText(edit.before), RunsText(edit.entries)));
            }
            else
            {
                output.Detail(fmt::format("{0}: no mark for the entries {1}", viewText, RunsText(edit.entries)));
            }
            if (edit.sizeWordKept)
            {
                output.Detail(fmt::format("{0}: the size word of its palette is not 22 + the count of its entries x {1}, and keeps its value", viewText, edit.before.entrySize));
            }
        }

        // The view of the game with the change of the action: the copy that
        // the game uses (a patch file, else the package). NotFound when the
        // game has no such view; Format for a view that a reader repaired
        // (the reader logs a warning and puts a cel of its own in the place
        // of damaged data): such a view is not the view of the game, so the
        // repair is the reason, also when the change fails. Else the errors
        // of the action.
        sci::Result<ViewPaletteEdit> EditView(GameSession &session, int number, const PaletteAction &action)
        {
            RepairLogSink repairs;
            sci::Result<ViewPaletteEdit> edited = [&]() -> sci::Result<ViewPaletteEdit>
            {
                ScopedCoreLogSink scopedSink(repairs);
                SCI_TRY_ASSIGN(ViewSource source, ReadViewSource(session, number));
                return action.clearUsed ? ClearViewPaletteMarks(session.Version(), source.data, action.first, action.last) :
                    ReplaceViewPaletteColors(session.Version(), source.data, action.first, action.last, action.colors);
            }();
            if (!repairs.Text().empty())
            {
                return sci::Fail(sci::ErrorCode::Format, "the reader repaired damaged data: " + repairs.Text());
            }
            return edited;
        }

        // The output folder has a patch file of a view only when this run
        // changed the view. So a view that failed ("failed") or that has no
        // change ("has no change") loses the patch file that an earlier run
        // left. A dry run removes nothing, and says what a run would remove.
        // Io when the file cannot be removed.
        sci::Status RemoveOldPatchFile(const std::string &path, const std::string &viewText, const char *why, bool dryRun, CliOutput &output)
        {
            std::error_code ec;
            if (!fs::exists(path, ec))
            {
                return sci::Ok();
            }
            if (dryRun)
            {
                // A file that does not open for writing cannot be removed.
                SCI_TRY(CheckFileCanBeReplaced(path, 0));
                output.Message(fmt::format("Would remove {0} of an earlier run: {1} {2}", path, viewText, why));
                return sci::Ok();
            }
            // As a write, this does not take the place of a read-only file.
            if (!DeleteFileA(path.c_str()))
            {
                return sci::Fail(sci::FromLastError(fmt::format("Removing {0} of an earlier run", path)));
            }
            output.Message(fmt::format("Removed {0} of an earlier run: {1} {2}", path, viewText, why));
            return sci::Ok();
        }

        sci::Status ChangeViewPalette(GameSession &session, int number, const PaletteAction &action, const std::string &outDir, bool dryRun, CliOutput &output, ViewPaletteResult &result)
        {
            const std::string viewText = ViewText(number);
            const SCIVersion &version = session.Version();
            const std::string path = outDir + "\\" + GetFileNameFor(ResourceType::View, number, NoBase36, version);

            // The exception boundary of the view: a view that fails loses
            // its old patch file also after an exception.
            sci::Result<ViewPaletteEdit> edited = sci::Guard("changing the palette of " + viewText, [&]() -> sci::Result<ViewPaletteEdit>
            {
                return EditView(session, number, action);
            });
            if (!edited)
            {
                sci::Status removed = RemoveOldPatchFile(path, viewText, "failed", dryRun, output);
                if (!removed)
                {
                    result.writeFailed = true;
                    output.Error(viewText + ": " + removed.error().ToString());
                }
                return sci::Fail(edited.error());
            }
            const ViewPaletteEdit &edit = *edited;
            if (action.clearUsed)
            {
                ReportClear(viewText, action, edit, output);
            }
            else
            {
                ReportColors(viewText, action, edit, output);
            }
            if (!edit.changed)
            {
                result.unchanged = true;
                sci::Status removed = RemoveOldPatchFile(path, viewText, "has no change", dryRun, output);
                result.writeFailed = !removed;
                return removed;
            }
            return WriteViewPatchFile(session, number, edit.data, path, dryRun, output, result.writeFailed);
        }
    }

    sci::Status WriteViewPatchFile(GameSession &session, int number, const std::vector<uint8_t> &data, const std::string &path, bool dryRun, CliOutput &output, bool &writeFailed)
    {
        const SCIVersion &version = session.Version();
        SCI_TRY(CheckResourceSize(version, (DWORD)data.size(), ResourceType::View));
        // The write of a patch file shares nothing (ResourceBlob::SaveToFile).
        sci::Status writable = CheckFileCanBeReplaced(path, 0);
        if (!writable)
        {
            writeFailed = true;
            return writable;
        }
        if (dryRun)
        {
            output.Message("Would write " + path);
            return sci::Ok();
        }
        ResourceBlob blob(session.Helper(), nullptr, ResourceType::View, data, version.DefaultVolumeFile, number, NoBase36, version, ResourceSourceFlags::PatchFile);
        HRESULT hr = blob.SaveToFile(path);
        if (FAILED(hr))
        {
            writeFailed = true;
            // No file with a part of the view stays.
            sci::Error error = sci::FromHResult(hr, "Writing " + path);
            DeleteFileA(path.c_str());
            return sci::Fail(error);
        }
        output.Detail("Wrote " + path);
        return sci::Ok();
    }

    const char *const ViewListTsvHeader =
        "number\tformat\tloops\tmirror_loops\tcels\traw_cels\tresolution\tsource\tbytes\tcrc32\tpalette_first\tpalette_count\tentry_size\t"
        "skip_pixels\tpalette_entries\tpixel_entries\terror\n";

    ViewListRow MakeViewListRow(const SCIVersion &version, int number, const std::vector<uint8_t> &data, bool fromPatch)
    {
        ViewListRow row;
        row.number = number;
        row.format = ViewFormatName(version.ViewFormat);
        row.source = fromPatch ? "patch" : "package";
        row.bytes = data.size();
        row.crc32 = Crc32Of(data);
        if (CanReadViewImage(version.ViewFormat))
        {
            // The exception boundary of the description.
            sci::Result<ViewDescription> description = sci::Guard(fmt::format("reading view {0}", number), [&]() { return DescribeView(version, data); });
            if (description)
            {
                row.description = std::move(*description);
                row.described = true;
            }
            else
            {
                row.error = description.error().ToString();
            }
        }
        return row;
    }

    // The numbers to the right, the texts to the left. The entries are the
    // last columns: they can be long.
    std::string ViewListTextTable(const std::vector<ViewListRow> &rows)
    {
        const std::vector<std::string> header = { "View", "Format", "Loops", "Mirrors", "Cels", "Raw", "Res", "Source", "Bytes", "CRC32", "Palette", "Skip px", "Palette entries", "Pixel entries" };
        const std::vector<bool> right = { true, false, true, true, true, true, true, false, true, false, false, true, false, false };
        std::vector<std::vector<std::string>> cells;
        std::vector<std::string> errors;
        for (const ViewListRow &row : rows)
        {
            cells.push_back(TextCells(row));
            errors.push_back(row.error);
        }
        return AlignedTextTable(header, right, cells, errors);
    }

    std::string ViewListTsvTable(const std::vector<ViewListRow> &rows)
    {
        std::string text = ViewListTsvHeader;
        for (const ViewListRow &row : rows)
        {
            text += fmt::format("{0}\t{1}\t", row.number, row.format);
            const ViewDescription &view = row.description;
            if (row.described)
            {
                text += fmt::format("{0}\t{1}\t{2}\t{3}\t{4}\t", view.loops.size(), view.MirrorLoops(), view.cels.size(), view.RawCels(), ResolutionCell(view));
            }
            else
            {
                text += "\t\t\t\t\t";
            }
            text += row.source.empty() ? std::string("\t\t\t") : fmt::format("{0}\t{1}\t{2:08x}\t", row.source, row.bytes, row.crc32);
            if (row.described)
            {
                const PicPaletteDescription &palette = view.palette;
                text += palette.present ? fmt::format("{0}\t{1}\t{2}\t", palette.firstEntry, palette.entryCount, palette.entrySize) : std::string("\t\t\t");
                text += fmt::format("{0}\t{1}\t{2}\t", SkipPixelsOf(view), RunsText(palette.UsedEntries()), RunsText(view.PixelEntries()));
            }
            else
            {
                text += "\t\t\t\t\t\t";
            }
            text += row.error + "\n";
        }
        return text;
    }

    sci::Result<ExitCode> RunViewList(GameSession &session, const ViewListOptions &options, CliOutput &output)
    {
        std::vector<int> views = ResourceNumbersOf(session, ResourceType::View);
        if (!options.views.empty())
        {
            SCI_TRY_ASSIGN(views, SelectResourceNumbers(views, options.views, "view"));
        }
        std::vector<ViewListRow> rows;
        for (int number : views)
        {
            if (CancelFlag().load())
            {
                // Ctrl+C while the views were read: no table, and the run
                // fails (exit code 7).
                return sci::Fail(sci::ErrorCode::Cancelled, "stopped by Ctrl+C");
            }
            SetCurrentItem("reading " + ViewText(number));
            rows.push_back(RowOf(session, number));
        }
        SetCurrentItem("");
        output.Result((options.format == "tsv") ? ViewListTsvTable(rows) : ViewListTextTable(rows));
        bool unreadable = std::any_of(rows.begin(), rows.end(), [](const ViewListRow &row) { return !row.error.empty(); });
        return unreadable ? ExitCode::PartialFailure : ExitCode::Success;
    }

    std::string ViewCelFileName(int view, int loop, int cel)
    {
        return fmt::format("{0}.l{1}.c{2}.bmp", view, loop, cel);
    }

    sci::Result<std::vector<PicExportFile>> MakeViewExportFiles(const SCIVersion &version, int number, const std::vector<uint8_t> &data, const ViewDescription &description, bool fromPatch,
        const PaletteComponent &colors, const PaletteComponent *palette999)
    {
        if (description.cels.empty())
        {
            return sci::Fail(sci::ErrorCode::Unsupported, "the view has no cel to export");
        }
        ViewSidecar sidecar = MakeViewSidecar(description);
        sidecar.view = number;
        sidecar.from = fromPatch ? "patch" : "package";
        sidecar.size = (uint32_t)data.size();
        sidecar.crc32 = Crc32Of(data);
        sidecar.palette999 = IndexRunsOf((palette999 != nullptr) ? UsedPaletteEntries(*palette999) : std::array<bool, 256>{});
        sidecar.used = IndexRunsOf(UsedPaletteEntries(colors));

        SCI_TRY_ASSIGN(std::vector<std::vector<uint8_t>> pixels, GetAllViewCelPixels(version, data));
        std::vector<PicExportFile> files;
        for (size_t i = 0; i < description.cels.size(); i++)
        {
            const ViewCelDescription &cel = description.cels[i];
            IndexedBmp image;
            image.width = cel.width;
            image.height = cel.height;
            image.pixels = std::move(pixels[i]);
            image.colors = ExportColorTable(colors);
            PicExportFile file;
            file.name = ViewCelFileName(number, cel.loop, cel.cel);
            sci::Result<std::vector<uint8_t>> bytes = EncodeIndexedBmp(image);
            if (!bytes)
            {
                return sci::Fail(bytes.error().code, fmt::format("loop {0} cel {1}: {2}", cel.loop, cel.cel, bytes.error().message));
            }
            file.bytes = std::move(*bytes);
            sidecar.cels[i].file = file.name;
            files.push_back(std::move(file));
        }
        PicExportFile sidecarFile;
        sidecarFile.name = fmt::format("{0}.toml", number);
        sidecarFile.text = ViewSidecarText(sidecar);
        sidecarFile.isText = true;
        files.push_back(std::move(sidecarFile));
        return files;
    }

    namespace
    {
        // What the export of one view did.
        struct ViewExportResult
        {
            size_t cels = 0;
            // The view has no stored cel, and the export of every view
            // skipped it.
            bool skipped = false;
            // The failure is a file that could not be written.
            bool writeFailed = false;
        };

        // The files of a view: its description (DescribeView gives Format
        // for a view that its reader repaired), the colours as SCI Companion
        // shows the view (its palette over palette 999), then the write.
        // Format also for a repair that a reader logs while the resource of
        // the view is found and checked.
        sci::Status ExportView(GameSession &session, int number, bool everyView, const std::string &outDir, bool dryRun, CliOutput &output, ViewExportResult &result)
        {
            RepairLogSink repairs;
            sci::Result<ViewSource> read = [&]()
            {
                ScopedCoreLogSink scopedSink(repairs);
                return ReadViewSource(session, number);
            }();
            const std::string repaired = repairs.Text();
            if (!repaired.empty())
            {
                return sci::Fail(sci::ErrorCode::Format, "the reader repaired damaged data: " + repaired);
            }
            SCI_TRY_ASSIGN(ViewSource source, std::move(read));
            const SCIVersion &version = session.Version();
            SCI_TRY_ASSIGN(ViewDescription description, DescribeView(version, source.data));
            if (description.cels.empty() && everyView)
            {
                result.skipped = true;
                return sci::Ok();
            }
            SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> entity, TryCreateResourceFromResourceData(*source.blob));
            std::unique_ptr<PaletteComponent> merged = session.ResourceMap().GetMergedPalette(*entity, 999);
            if (!merged)
            {
                return sci::Fail(sci::ErrorCode::Internal, "the palette of the view could not be made");
            }
            SCI_TRY_ASSIGN(std::vector<PicExportFile> files, MakeViewExportFiles(version, number, source.data, description, source.fromPatch, *merged, session.ResourceMap().GetPalette999()));
            result.cels = (size_t)std::count_if(files.begin(), files.end(), [](const PicExportFile &file) { return !file.isText; });
            return WriteExportFiles(outDir, files, dryRun, output, result.writeFailed);
        }
    }

    sci::Result<ExitCode> RunViewExport(GameSession &session, const ViewExportOptions &options, const CommonOptions &common, CliOutput &output)
    {
        const std::string outDir = AbsolutePath(options.outDir);
        SCI_TRY(CheckOutputFolder(session, outDir));
        std::vector<int> views = ResourceNumbersOf(session, ResourceType::View);
        if (!options.all)
        {
            SCI_TRY_ASSIGN(views, SelectResourceNumbers(views, options.views, "view"));
        }
        // The resource map reads palette 999 one time, here: a warning of its
        // reader goes to the output once, and is not a repair of a view.
        sci::Status palette999 = sci::Guard("reading palette 999", [&]() -> sci::Status
        {
            session.ResourceMap().GetPalette999();
            return sci::Ok();
        });
        if (!palette999)
        {
            output.Warning(palette999.error().ToString());
        }

        ReportFacts facts;
        size_t exported = 0;
        size_t cels = 0;
        size_t skipped = 0;
        size_t failed = 0;
        for (int number : views)
        {
            if (CancelFlag().load())
            {
                facts.cancelled = true;
                break;
            }
            SetCurrentItem("exporting " + ViewText(number));
            ViewExportResult result;
            // The exception boundary of the view.
            sci::Status status = sci::Guard("exporting " + ViewText(number), [&]() -> sci::Status
            {
                return ExportView(session, number, options.all, outDir, common.dryRun, output, result);
            });
            if (!status)
            {
                failed++;
                output.Error(ViewText(number) + ": " + status.error().ToString());
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
                output.Detail(ViewText(number) + " has no cel: skipped");
            }
            else
            {
                exported++;
                cels += result.cels;
            }
        }
        SetCurrentItem("");

        std::string summary = fmt::format("{0} {1} of {2} ({3}) into {4}", common.dryRun ? "A run would export" : "Exported", exported, CountText(views.size(), "view"), CountText(cels, "cel"), outDir);
        if (skipped > 0)
        {
            summary += fmt::format("; {0} no cel", (skipped == 1) ? "1 view has" : fmt::format("{0} views have", skipped));
        }
        if (failed > 0)
        {
            summary += fmt::format("; {0} failed", CountText(failed, "view"));
        }
        if (facts.cancelled)
        {
            summary += "; stopped by Ctrl+C";
        }
        output.Message(summary + ".");
        return ExitCodeForFacts(facts);
    }

    sci::Result<std::vector<uint8_t>> MakeViewImportData(const SCIVersion &version, const std::vector<uint8_t> &data, const ViewDescription &description, const std::array<bool, 256> &usedEntries,
        const std::vector<ViewImportImage> &images, const PicImportPalette &palette, bool allowUnusedEntries, std::vector<std::string> &warnings)
    {
        SCI_TRY_ASSIGN(std::vector<std::vector<uint8_t>> oldPixels, GetAllViewCelPixels(version, data));
        std::vector<ViewCelPixels> cels;
        // The images whose colour tables give the colours of the range.
        std::vector<std::pair<std::string, const IndexedBmp *>> rangeImages;
        for (const ViewImportImage &image : images)
        {
            const std::string celText = fmt::format("cel {0} of loop {1}", image.cel, image.loop);
            const ViewCelDescription *cel = description.Find(image.loop, image.cel);
            if (cel == nullptr)
            {
                if ((image.loop >= 0) &&((size_t)image.loop < description.loops.size()) && (description.loops[image.loop].mirrorOf >= 0))
                {
                    const int mirrorOf = description.loops[image.loop].mirrorOf;
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}: loop {1} mirrors loop {2}, so it stores no cel; its cels are those of loop {2}", image.name, image.loop, mirrorOf));
                }
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}: the view stores no {1}", image.name, celText));
            }
            if (std::any_of(cels.begin(), cels.end(), [&](const ViewCelPixels &other) { return (other.loop == image.loop) && (other.cel == image.cel); }))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("two images are for {0}", celText));
            }
            if ((image.image.width != cel->width) || (image.image.height != cel->height))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0} has {1} x {2} pixels, and {3} has {4} x {5}", image.name, image.image.width, image.image.height, celText, cel->width, cel->height));
            }
            const size_t index = (size_t)(cel - description.cels.data());
            SCI_TRY(CheckImportPixels(image.name, celText, "view", cel->skipColor, cel->width, oldPixels[index], image.image.pixels, usedEntries, allowUnusedEntries, warnings));
            if (palette.replace)
            {
                if (image.image.colorCount <= palette.last)
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}: the colour table has {1} {2}, and the palette range is {3}-{4}", image.name, image.image.colorCount,
                        (image.image.colorCount == 1) ? "entry" : "entries", palette.first, palette.last));
                }
                rangeImages.emplace_back(image.name, &image.image);
            }
            ViewCelPixels pixels;
            pixels.loop = image.loop;
            pixels.cel = image.cel;
            pixels.pixels = image.image.pixels;
            cels.push_back(std::move(pixels));
        }

        std::vector<uint8_t> newData = data;
        if (!rangeImages.empty())
        {
            if (!description.palette.present)
            {
                return sci::Fail(sci::ErrorCode::Unsupported, "the view has no palette, so no entry can get a new colour");
            }
            // An entry that the view does not mark as used keeps its colour.
            std::array<IndexedBmpColor, 256> colors = {};
            SCI_TRY_ASSIGN(colors, ReplacedPaletteColors(rangeImages, palette, description.palette, "view", warnings));
            SCI_TRY_ASSIGN(ViewPaletteEdit edit, ReplaceViewPaletteColors(version, newData, palette.first, palette.last, colors));
            std::array<bool, 256> notStored = {};
            for (int entry = palette.first; entry <= palette.last; entry++)
            {
                notStored[entry] = !edit.entries[entry];
            }
            if (!RunsText(notStored).empty())
            {
                warnings.push_back(fmt::format("its palette stores {0}, so the entries {1} of the range get no colour", StoredText(edit.before), RunsText(notStored)));
            }
            newData = std::move(edit.data);
        }
        return ReplaceViewCelPixels(version, newData, cels);
    }

    namespace
    {
        // The image of one cel of a view import, before it is read.
        struct ViewImportCel
        {
            int loop = 0;
            int cel = 0;
            std::string imagePath;
        };

        // The images of an import for one view.
        struct ViewImportJob
        {
            int number = 0;
            std::vector<ViewImportCel> cels;
            // The CRC-32 that the sidecar of the images has for the view.
            bool hasCrc32 = false;
            uint32_t crc32 = 0;
            std::string sidecarPath;
        };

        // What the import of one view did.
        struct ViewImportResult
        {
            size_t cels = 0;
            // The view has no change, and gets no patch file.
            bool unchanged = false;
            // A patch file could not be written or removed.
            bool writeFailed = false;
        };

        // The job of a sidecar file: the view, and the cels that have a file.
        // The files are in the folder of the sidecar.
        sci::Result<ViewImportJob> JobOfViewSidecar(const std::string &sidecarPath)
        {
            SCI_TRY_ASSIGN(ViewSidecar sidecar, ReadViewSidecarFile(sidecarPath));
            sci::ErrorLocation where;
            where.file = sidecarPath;
            if (!sidecar.hasView)
            {
                return sci::Fail(sci::ErrorCode::Format, "the file has no view number ([source] view): it is not a file of scic view export", where);
            }
            ViewImportJob job;
            job.number = sidecar.view;
            job.hasCrc32 = sidecar.hasCrc32;
            job.crc32 = sidecar.crc32;
            job.sidecarPath = sidecarPath;
            const std::string folder = fs::path(sidecarPath).parent_path().string();
            for (const ViewSidecarCel &cel : sidecar.cels)
            {
                if (!cel.file.empty())
                {
                    ViewImportCel importCel;
                    importCel.loop = cel.loop;
                    importCel.cel = cel.cel;
                    importCel.imagePath = folder + "\\" + cel.file;
                    job.cels.push_back(importCel);
                }
            }
            return job;
        }

        // The new bytes of the view, or the same bytes for a view with no
        // change. Usage for a sidecar file that names no image file; Format
        // for a view that a reader repaired while its resource was read
        // (DescribeView gives Format for a repair of the reader of the view).
        sci::Result<std::vector<uint8_t>> MakeImportedView(GameSession &session, const ViewImportJob &job, const PicImportPalette &palette, bool allowUnused, CliOutput &output, bool &unchanged)
        {
            const std::string viewText = ViewText(job.number);
            if (job.cels.empty())
            {
                sci::ErrorLocation where;
                where.file = job.sidecarPath;
                return sci::Fail(sci::ErrorCode::Usage, "the file names no image file of a cel", where);
            }
            RepairLogSink repairs;
            sci::Result<ViewSource> read = [&]()
            {
                ScopedCoreLogSink scopedSink(repairs);
                return ReadViewSource(session, job.number);
            }();
            const std::string repaired = repairs.Text();
            if (!repaired.empty())
            {
                return sci::Fail(sci::ErrorCode::Format, "the reader repaired damaged data: " + repaired);
            }
            SCI_TRY_ASSIGN(ViewSource source, std::move(read));
            const SCIVersion &version = session.Version();
            SCI_TRY_ASSIGN(ViewDescription description, DescribeView(version, source.data));
            const uint32_t crc32 = Crc32Of(source.data);
            if (job.hasCrc32 && (job.crc32 != crc32))
            {
                output.Warning(fmt::format("{0}: {1} has the CRC-32 {2:08x}, and the view in the game has {3:08x}: the images are of another copy of the view", viewText, job.sidecarPath, job.crc32, crc32));
            }

            // The entries that the game has a colour for: those that the
            // view or palette 999 marks as used (the palette of export).
            SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> entity, TryCreateResourceFromResourceData(*source.blob));
            std::unique_ptr<PaletteComponent> merged = session.ResourceMap().GetMergedPalette(*entity, 999);
            if (!merged)
            {
                return sci::Fail(sci::ErrorCode::Internal, "the palette of the view could not be made");
            }

            // Each image is read, and then all are checked, before the view
            // changes.
            std::vector<ViewImportImage> images;
            for (const ViewImportCel &importCel : job.cels)
            {
                ViewImportImage image;
                image.loop = importCel.loop;
                image.cel = importCel.cel;
                image.name = importCel.imagePath;
                SCI_TRY_ASSIGN(image.image, ReadIndexedBmpFile(importCel.imagePath));
                images.push_back(std::move(image));
            }
            std::vector<std::string> warnings;
            PicImportPalette viewPalette = palette;
            viewPalette.exportColors = ExportColorTable(*merged);
            sci::Result<std::vector<uint8_t>> made = MakeViewImportData(version, source.data, description, UsedPaletteEntries(*merged), images, viewPalette, allowUnused, warnings);
            for (const std::string &warning : warnings)
            {
                output.Warning(viewText + ": " + warning);
            }
            SCI_TRY_ASSIGN(std::vector<uint8_t> data, std::move(made));
            // A view too large for a resource fails here, so that it loses
            // the patch file of an earlier run.
            SCI_TRY(CheckResourceSize(version, (DWORD)data.size(), ResourceType::View));
            unchanged = (data == source.data);
            return data;
        }

        sci::Status ImportView(GameSession &session, const ViewImportJob &job, const PicImportPalette &palette, bool allowUnused, const std::string &outDir, bool dryRun, CliOutput &output, ViewImportResult &result)
        {
            const std::string viewText = ViewText(job.number);
            const std::string path = outDir + "\\" + GetFileNameFor(ResourceType::View, job.number, NoBase36, session.Version());
            bool unchanged = false;
            // The exception boundary of the view: a view that fails loses its
            // old patch file also after an exception.
            sci::Result<std::vector<uint8_t>> made = sci::Guard("importing " + viewText, [&]() -> sci::Result<std::vector<uint8_t>>
            {
                return MakeImportedView(session, job, palette, allowUnused, output, unchanged);
            });
            if (!made)
            {
                sci::Status removed = RemoveOldPatchFile(path, viewText, "failed", dryRun, output);
                if (!removed)
                {
                    result.writeFailed = true;
                    output.Error(viewText + ": " + removed.error().ToString());
                }
                return sci::Fail(made.error());
            }
            result.cels = job.cels.size();
            if (unchanged)
            {
                result.unchanged = true;
                output.Warning(fmt::format("{0}: the images have the pixels of the view: the view does not change, and gets no patch file", viewText));
                sci::Status removed = RemoveOldPatchFile(path, viewText, "has no change", dryRun, output);
                result.writeFailed = !removed;
                return removed;
            }
            return WriteViewPatchFile(session, job.number, *made, path, dryRun, output, result.writeFailed);
        }
    }

    sci::Result<ExitCode> RunViewImport(GameSession &session, const ViewImportOptions &options, const CommonOptions &common, CliOutput &output)
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
        // boundary of its view).
        struct Work
        {
            ViewImportJob job;
            std::string sidecarPath;
            bool badName = false;
        };
        std::vector<Work> works;
        if (!options.inDir.empty())
        {
            SCI_TRY_ASSIGN(std::vector<SidecarFile> files, ListSidecarFiles(AbsolutePath(options.inDir), "scic view export"));
            for (const SidecarFile &file : files)
            {
                Work work;
                work.sidecarPath = file.path;
                work.badName = file.badName;
                works.push_back(work);
            }
        }
        else
        {
            Work work;
            if (!ParseNumber(options.view, work.job.number))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("\"{0}\" is not a view number", options.view));
            }
            const std::vector<int> views = ResourceNumbersOf(session, ResourceType::View);
            if (std::find(views.begin(), views.end(), work.job.number) == views.end())
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("the game has no view {0}", work.job.number));
            }
            ViewImportCel cel;
            cel.loop = options.loop;
            cel.cel = options.cel;
            cel.imagePath = AbsolutePath(options.image);
            work.job.cels.push_back(cel);
            works.push_back(work);
        }
        // The resource map reads palette 999 one time, here: a warning of its
        // reader goes to the output once, and is not a repair of a view.
        sci::Status palette999 = sci::Guard("reading palette 999", [&]() -> sci::Status
        {
            session.ResourceMap().GetPalette999();
            return sci::Ok();
        });
        if (!palette999)
        {
            output.Warning(palette999.error().ToString());
        }

        ReportFacts facts;
        size_t imported = 0;
        size_t cels = 0;
        size_t unchanged = 0;
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
            // view is known.
            std::string name = work.sidecarPath.empty() ? ViewText(work.job.number) : work.sidecarPath;
            SetCurrentItem("importing " + name);
            ViewImportResult result;
            // The exception boundary of the view.
            sci::Status status = sci::Guard("importing " + name, [&]() -> sci::Status
            {
                if (work.badName)
                {
                    return sci::Fail(sci::ErrorCode::Unsupported, "the name of the file has a character that the ANSI code page of Windows does not have, so scic cannot open the file");
                }
                if (!work.sidecarPath.empty())
                {
                    SCI_TRY_ASSIGN(work.job, JobOfViewSidecar(work.sidecarPath));
                    name = ViewText(work.job.number);
                    SetCurrentItem("importing " + name);
                }
                if (!done.insert(work.job.number).second)
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("the view is in two .toml files of the input folder; the second is {0}", work.sidecarPath));
                }
                return ImportView(session, work.job, palette, options.allowUnusedIndices, outDir, common.dryRun, output, result);
            });
            if (!status)
            {
                failed++;
                // An error of a sidecar file whose view is not known has the
                // file as its location, so the line needs no name before it.
                const bool hasItsFile = (name == work.sidecarPath) && !status.error().where.file.empty();
                output.Error(hasItsFile ? status.error().ToString() : (name + ": " + status.error().ToString()));
                if (status.error().code == sci::ErrorCode::Internal)
                {
                    facts.internal = true;
                }
                else if (!result.writeFailed)
                {
                    facts.otherFailures = true;
                }
            }
            else if (result.unchanged)
            {
                unchanged++;
            }
            else
            {
                imported++;
                cels += result.cels;
            }
            facts.writeFailed = facts.writeFailed || result.writeFailed;
        }
        SetCurrentItem("");

        std::string summary = fmt::format("{0} {1} of {2} ({3}) into {4}", common.dryRun ? "A run would import" : "Imported", imported, CountText(works.size(), "view"), CountText(cels, "cel"), outDir);
        if (unchanged > 0)
        {
            summary += fmt::format("; {0} with no change", CountText(unchanged, "view"));
        }
        if (failed > 0)
        {
            summary += fmt::format("; {0} failed", CountText(failed, "view"));
        }
        if (facts.cancelled)
        {
            summary += "; stopped by Ctrl+C";
        }
        output.Message(summary + ".");
        return ExitCodeForFacts(facts);
    }

    sci::Result<ExitCode> RunViewPalette(GameSession &session, const ViewPaletteOptions &options, const CommonOptions &common, CliOutput &output)
    {
        const std::string outDir = AbsolutePath(options.outDir);
        SCI_TRY(CheckOutputFolder(session, outDir));
        PaletteAction action;
        action.clearUsed = options.clearUsed;
        if (!ParsePaletteRange(options.paletteRange, action.first, action.last))
        {
            return sci::Fail(sci::ErrorCode::Usage, "--palette-range is first-last, two palette entries of 0 to 255 (for example 80-235)");
        }
        SCI_TRY_ASSIGN(std::vector<int> views, SelectResourceNumbers(ResourceNumbersOf(session, ResourceType::View), options.views, "view"));
        if (!action.clearUsed)
        {
            // One colour table for each view of the run.
            const std::string imagePath = AbsolutePath(options.colorsFrom);
            SCI_TRY_ASSIGN(IndexedBmp image, ReadIndexedBmpFile(imagePath));
            if (image.colorCount <= action.last)
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}: the colour table has {1} {2}, and the palette range is {3}-{4}", imagePath, image.colorCount,
                    (image.colorCount == 1) ? "entry" : "entries", action.first, action.last));
            }
            action.colors = image.colors;
        }

        ReportFacts facts;
        size_t written = 0;
        size_t unchanged = 0;
        size_t failed = 0;
        for (int number : views)
        {
            if (CancelFlag().load())
            {
                facts.cancelled = true;
                break;
            }
            SetCurrentItem("changing the palette of " + ViewText(number));
            ViewPaletteResult result;
            // The exception boundary of the files of the view.
            sci::Status status = sci::Guard("writing the patch file of " + ViewText(number), [&]() -> sci::Status
            {
                return ChangeViewPalette(session, number, action, outDir, common.dryRun, output, result);
            });
            if (!status)
            {
                failed++;
                output.Error(ViewText(number) + ": " + status.error().ToString());
                if (status.error().code == sci::ErrorCode::Internal)
                {
                    facts.internal = true;
                }
                else if (!result.writeFailed)
                {
                    facts.otherFailures = true;
                }
            }
            else if (result.unchanged)
            {
                unchanged++;
            }
            else
            {
                written++;
            }
            facts.writeFailed = facts.writeFailed || result.writeFailed;
        }
        SetCurrentItem("");

        std::string summary = fmt::format("{0} {1} of {2} into {3}", common.dryRun ? "A run would write" : "Wrote", written, CountText(views.size(), "view"), outDir);
        if (unchanged > 0)
        {
            summary += fmt::format("; {0} with no change", CountText(unchanged, "view"));
        }
        if (failed > 0)
        {
            summary += fmt::format("; {0} failed", CountText(failed, "view"));
        }
        if (facts.cancelled)
        {
            summary += "; stopped by Ctrl+C";
        }
        output.Message(summary + ".");
        return ExitCodeForFacts(facts);
    }

    sci::Result<NewView> MakeAddedView(const ViewSidecar &sidecar, const std::vector<ViewImportImage> &images, const std::array<bool, 256> &palette999Used, bool allowUnusedEntries,
        std::vector<std::string> &warnings)
    {
        NewView view;
        view.scaleFlags = (uint8_t)sidecar.scaleFlags;
        view.byte4 = (uint8_t)sidecar.byte4;
        view.resolution = (uint8_t)sidecar.resolution;
        view.nativeWidth = (uint16_t)sidecar.nativeWidth;
        view.nativeHeight = (uint16_t)sidecar.nativeHeight;

        // The loops, by their index.
        const size_t loopCount = sidecar.loops.size();
        if ((loopCount == 0) || (loopCount > 255))
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("the file has {0} [[loop]] tables; a view has 1 to 255 loops", loopCount));
        }
        if ((sidecar.loopCount != 0) && ((size_t)sidecar.loopCount != loopCount))
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("[view] loops is {0}, and the file has {1} [[loop]] tables", sidecar.loopCount, loopCount));
        }
        std::vector<const ViewSidecarLoop *> loops(loopCount, nullptr);
        for (const ViewSidecarLoop &loop : sidecar.loops)
        {
            if ((loop.index < 0) || ((size_t)loop.index >= loopCount) || (loops[loop.index] != nullptr))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("a [[loop]] has the index {0}; the indices of the {1} loops are 0 to {2}, each one time", loop.index, loopCount, loopCount - 1));
            }
            loops[loop.index] = &loop;
        }
        view.loops.resize(loopCount);
        for (size_t l = 0; l < loopCount; l++)
        {
            view.loops[l].mirrorOf = loops[l]->mirrorOf;
        }

        // The cels, by their loop and cel.
        std::vector<std::vector<const ViewSidecarCel *>> cels(loopCount);
        for (const ViewSidecarCel &cel : sidecar.cels)
        {
            const std::string celText = fmt::format("cel {0} of loop {1}", cel.cel, cel.loop);
            if ((cel.loop < 0) || ((size_t)cel.loop >= loopCount))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("the [[cel]] of {0}: the view has no loop {1}", celText, cel.loop));
            }
            if (loops[cel.loop]->mirrorOf >= 0)
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("the [[cel]] of {0}: loop {1} mirrors loop {2}, so it has no cels of its own", celText, cel.loop, loops[cel.loop]->mirrorOf));
            }
            std::vector<const ViewSidecarCel *> &loopCels = cels[cel.loop];
            if ((cel.cel < 0) || (cel.cel > 254))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("the [[cel]] of {0}: a loop has the cels 0 to 254", celText));
            }
            if ((size_t)cel.cel >= loopCels.size())
            {
                loopCels.resize((size_t)cel.cel + 1, nullptr);
            }
            if (loopCels[cel.cel] != nullptr)
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("the file has two [[cel]] tables for {0}", celText));
            }
            loopCels[cel.cel] = &cel;
        }
        // A loop that is not a mirror can have no cel, as some of Sierra's
        // views have.
        size_t storedCels = 0;
        for (size_t l = 0; l < loopCount; l++)
        {
            const auto gap = std::find(cels[l].begin(), cels[l].end(), nullptr);
            if (gap != cels[l].end())
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("loop {0} has no [[cel]] for cel {1}; the cels of a loop are 0 to n - 1", l, gap - cels[l].begin()));
            }
            storedCels += cels[l].size();
        }
        if ((sidecar.celCount != 0) && ((size_t)sidecar.celCount != storedCels))
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("[view] cels is {0}, and the file has {1} [[cel]] tables", sidecar.celCount, storedCels));
        }
        std::vector<int> mirrorOf;
        for (size_t l = 0; l < loopCount; l++)
        {
            mirrorOf.push_back(loops[l]->mirrorOf);
        }
        for (size_t l = 0; l < loopCount; l++)
        {
            const int shown = MirroredLoop(mirrorOf, l);
            if (shown < 0)
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("loop {0} mirrors loop {1}; a mirror shows the cels of another loop of the view, and a chain of mirrors ends at a loop that is not a mirror", l, mirrorOf[l]));
            }
            // A mirror of a mirror gets the count of the header of the
            // mirror that it names (Sierra's views have 0 there): its cels
            // key gives that count.
            const int named = mirrorOf[l];
            if ((named >= 0) && (mirrorOf[named] >= 0))
            {
                NewViewLoop &namedLoop = view.loops[named];
                if ((namedLoop.headerCels >= 0) && (namedLoop.headerCels != loops[l]->cels))
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("two loops mirror loop {0}, and their [[loop]] tables have other cels ({1} and {2})", named, namedLoop.headerCels, loops[l]->cels));
                }
                // When the loop at the end of the chain has cels, 0 makes a
                // mirror with no cel.
                if ((loops[l]->cels == 0) && !cels[shown].empty())
                {
                    warnings.push_back(fmt::format("loop {0} mirrors loop {1}, which is a mirror, and the [[loop]] of loop {0} has cels = 0 (or no cels key): the interpreters give loop {0} no cel, as in some of Sierra's views",
                        l, named));
                }
                namedLoop.headerCels = loops[l]->cels;
                continue;
            }
            const size_t count = cels[shown].size();
            if ((loops[l]->cels != 0) && ((size_t)loops[l]->cels != count))
            {
                return sci::Fail(sci::ErrorCode::Usage, ((size_t)shown == l) ? fmt::format("the [[loop]] of loop {0} has cels = {1}, and the loop has {2}", l, loops[l]->cels, CountText(count, "cel")) :
                    fmt::format("the [[loop]] of loop {0} has cels = {1}, and loop {2}, whose cels it shows, has {3}", l, loops[l]->cels, shown, CountText(count, "cel")));
            }
        }

        // The image of each cel.
        std::vector<std::vector<const ViewImportImage *>> celImages(loopCount);
        for (size_t l = 0; l < loopCount; l++)
        {
            celImages[l].assign(cels[l].size(), nullptr);
        }
        for (const ViewImportImage &image : images)
        {
            if ((image.loop >= 0) && ((size_t)image.loop < loopCount) && (image.cel >= 0) && ((size_t)image.cel < celImages[image.loop].size()))
            {
                celImages[image.loop][image.cel] = &image;
            }
        }

        // The palette.
        if (sidecar.hasPalette)
        {
            PicPaletteDescription &palette = view.palette;
            palette.present = true;
            palette.firstEntry = sidecar.storedFirst;
            palette.entryCount = sidecar.storedCount;
            palette.entrySize = (sidecar.entrySize == 0) ? 4 : sidecar.entrySize;
            const int last = sidecar.storedFirst + sidecar.storedCount - 1;
            if (last > 255)
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("[palette] stores {0} entries from entry {1}; a palette has the entries 0 to 255", sidecar.storedCount, sidecar.storedFirst));
            }
            for (int entry = palette.firstEntry; entry <= last; entry++)
            {
                palette.flags[entry] = 1;
            }
            for (const PicSidecarFlagRun &run : sidecar.flags)
            {
                if ((run.first < palette.firstEntry) || (run.last > last))
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("[palette] flags has the run {0}-{1}, which is not in the stored entries {2}-{3}", run.first, run.last, palette.firstEntry, last));
                }
                std::fill(palette.flags.begin() + run.first, palette.flags.begin() + run.last + 1, (uint8_t)run.flag);
            }
            // The colours come from the colour tables of the images.
            const ViewImportImage *first = nullptr;
            for (const ViewImportImage &image : images)
            {
                if ((sidecar.storedCount > 0) && (image.image.colorCount <= last))
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}: the colour table has {1} {2}, and the palette stores the entries {3}-{4}", image.name, image.image.colorCount,
                        (image.image.colorCount == 1) ? "entry" : "entries", palette.firstEntry, last));
                }
                if (first == nullptr)
                {
                    first = &image;
                    continue;
                }
                for (int entry = palette.firstEntry; entry <= last; entry++)
                {
                    if ((palette.flags[entry] != 0) && (image.image.colors[entry] != first->image.colors[entry]))
                    {
                        return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0} and {1} have another colour for entry {2}; the images of a view must have one palette", first->name, image.name, entry));
                    }
                }
            }
            for (int entry = palette.firstEntry; (first != nullptr) && (entry <= last); entry++)
            {
                palette.colors[entry] = first->image.colors[entry];
            }
        }
        std::array<bool, 256> used = palette999Used;
        for (size_t entry = 0; entry < used.size(); entry++)
        {
            used[entry] = used[entry] || (view.palette.flags[entry] != 0);
        }

        // The cels, with the checks of their images.
        for (size_t l = 0; l < loopCount; l++)
        {
            for (size_t c = 0; c < cels[l].size(); c++)
            {
                const ViewSidecarCel &cel = *cels[l][c];
                const std::string celText = fmt::format("cel {0} of loop {1}", c, l);
                const ViewImportImage *image = celImages[l][c];
                if (cel.file.empty() || (image == nullptr))
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("the [[cel]] of {0} names no image file", celText));
                }
                if (((cel.width != 0) && (cel.width != image->image.width)) || ((cel.height != 0) && (cel.height != image->image.height)))
                {
                    return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0} has {1} x {2} pixels, and the [[cel]] of {3} has {4} x {5}", image->name, image->image.width, image->image.height, celText,
                        cel.width, cel.height));
                }
                // A new cel has no old pixels: the check of the skip colour
                // compares the pixels with themselves, and only the check of
                // the entries applies.
                SCI_TRY(CheckImportPixels(image->name, celText, "view", (uint8_t)cel.skipColor, image->image.width, image->image.pixels, image->image.pixels, used, allowUnusedEntries, warnings));
                NewViewCel newCel;
                newCel.width = image->image.width;
                newCel.height = image->image.height;
                newCel.x = cel.x;
                newCel.y = cel.y;
                newCel.skipColor = (uint8_t)cel.skipColor;
                newCel.pixels = image->image.pixels;
                newCel.raw = cel.raw;
                view.loops[l].cels.push_back(std::move(newCel));
            }
        }
        return view;
    }

    namespace
    {
        // The new bytes of the view of the .toml file. The cel files are in
        // the folder of the .toml file.
        sci::Result<std::vector<uint8_t>> MakeAddedViewData(GameSession &session, const std::string &sidecarPath, bool allowUnused, CliOutput &output, const std::string &viewText, size_t &loops, size_t &cels)
        {
            SCI_TRY_ASSIGN(ViewSidecar sidecar, ReadViewSidecarFile(sidecarPath));
            const std::string folder = fs::path(sidecarPath).parent_path().string();
            std::vector<ViewImportImage> images;
            for (const ViewSidecarCel &cel : sidecar.cels)
            {
                if (cel.file.empty())
                {
                    continue;
                }
                ViewImportImage image;
                image.loop = cel.loop;
                image.cel = cel.cel;
                image.name = folder + "\\" + cel.file;
                SCI_TRY_ASSIGN(image.image, ReadIndexedBmpFile(image.name));
                images.push_back(std::move(image));
            }
            const PaletteComponent *palette999 = session.ResourceMap().GetPalette999();
            const std::array<bool, 256> palette999Used = (palette999 != nullptr) ? UsedPaletteEntries(*palette999) : std::array<bool, 256>{};
            std::vector<std::string> warnings;
            sci::Result<NewView> view = MakeAddedView(sidecar, images, palette999Used, allowUnused, warnings);
            for (const std::string &warning : warnings)
            {
                output.Warning(viewText + ": " + warning);
            }
            SCI_TRY(view);
            loops = view->loops.size();
            cels = 0;
            for (const NewViewLoop &loop : view->loops)
            {
                cels += loop.cels.size();
            }
            SCI_TRY_ASSIGN(std::vector<uint8_t> data, MakeNewViewBytes(session.Version(), *view));
            // A view too large for a resource fails here, so that it loses
            // the patch file of an earlier run.
            SCI_TRY(CheckResourceSize(session.Version(), (DWORD)data.size(), ResourceType::View));
            return data;
        }
    }

    sci::Result<ExitCode> RunViewAdd(GameSession &session, const ViewAddOptions &options, const CommonOptions &common, CliOutput &output)
    {
        const std::string outDir = AbsolutePath(options.outDir);
        SCI_TRY(CheckOutputFolder(session, outDir));
        int number = 0;
        if (!ParseNumber(options.view, number))
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("\"{0}\" is not a view number (0 to 65535)", options.view));
        }
        const std::string sidecarPath = AbsolutePath(options.from);
        std::error_code ec;
        if (!fs::is_regular_file(sidecarPath, ec))
        {
            return sci::Fail(sci::ErrorCode::NotFound, "the .toml file does not exist: " + sidecarPath);
        }
        const std::vector<int> views = ResourceNumbersOf(session, ResourceType::View);
        if (!options.replace && (std::find(views.begin(), views.end(), number) != views.end()))
        {
            return sci::Fail(sci::ErrorCode::Usage, fmt::format("the game has view {0}; give --replace to make a new view in its place, or use scic view import to change the cels of the view", number));
        }
        // The resource map reads palette 999 one time, here: a warning of its
        // reader goes to the output once.
        sci::Status palette999 = sci::Guard("reading palette 999", [&]() -> sci::Status
        {
            session.ResourceMap().GetPalette999();
            return sci::Ok();
        });
        if (!palette999)
        {
            output.Warning(palette999.error().ToString());
        }

        ReportFacts facts;
        if (CancelFlag().load())
        {
            facts.cancelled = true;
            output.Message("Added no view; stopped by Ctrl+C.");
            return ExitCodeForFacts(facts);
        }
        const std::string viewText = ViewText(number);
        const std::string path = outDir + "\\" + GetFileNameFor(ResourceType::View, number, NoBase36, session.Version());
        SetCurrentItem("adding " + viewText);
        bool writeFailed = false;
        size_t loops = 0;
        size_t cels = 0;
        // The exception boundary of the view.
        sci::Status status = sci::Guard("adding " + viewText, [&]() -> sci::Status
        {
            sci::Result<std::vector<uint8_t>> made = sci::Guard("making " + viewText, [&]() -> sci::Result<std::vector<uint8_t>>
            {
                return MakeAddedViewData(session, sidecarPath, options.allowUnusedIndices, output, viewText, loops, cels);
            });
            if (!made)
            {
                // The output folder keeps no patch file of the view of an
                // earlier run.
                sci::Status removed = RemoveOldPatchFile(path, viewText, "failed", common.dryRun, output);
                if (!removed)
                {
                    writeFailed = true;
                    output.Error(viewText + ": " + removed.error().ToString());
                }
                return sci::Fail(made.error());
            }
            return WriteViewPatchFile(session, number, *made, path, common.dryRun, output, writeFailed);
        });
        SetCurrentItem("");
        if (!status)
        {
            output.Error(viewText + ": " + status.error().ToString());
            if (status.error().code == sci::ErrorCode::Internal)
            {
                facts.internal = true;
            }
            else if (!writeFailed)
            {
                facts.otherFailures = true;
            }
            facts.writeFailed = writeFailed;
            return ExitCodeForFacts(facts);
        }
        output.Message(fmt::format("{0} {1} ({2}, {3}) as {4}.", common.dryRun ? "A run would add" : "Added", viewText, CountText(loops, "loop"), CountText(cels, "cel"), path));
        return ExitCodeForFacts(facts);
    }
}
