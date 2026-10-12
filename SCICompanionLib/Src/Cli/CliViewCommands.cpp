#include "stdafx.h"
#include "CliCommands.h"
#include "CliHost.h"
#include "CliResources.h"
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
#include "format.h"
#include <filesystem>

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
                std::unique_ptr<ResourceBlob> source = session.ResourceMap().MostRecentResource(ResourceType::View, number, false);
                if (!source)
                {
                    return sci::Fail(sci::ErrorCode::NotFound, fmt::format("the game has no view {0}", number));
                }
                SCI_TRY(CheckResourceData(*source));
                const std::vector<uint8_t> data(source->GetData(), source->GetData() + source->GetLength());
                return action.clearUsed ? ClearViewPaletteMarks(session.Version(), data, action.first, action.last) :
                    ReplaceViewPaletteColors(session.Version(), data, action.first, action.last, action.colors);
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
            SCI_TRY(CheckResourceSize(version, (DWORD)edit.data.size(), ResourceType::View));

            // The write of a patch file shares nothing (ResourceBlob::SaveToFile).
            sci::Status writable = CheckFileCanBeReplaced(path, 0);
            if (!writable)
            {
                result.writeFailed = true;
                return writable;
            }
            if (dryRun)
            {
                output.Message("Would write " + path);
                return sci::Ok();
            }
            ResourceBlob blob(session.Helper(), nullptr, ResourceType::View, edit.data, version.DefaultVolumeFile, number, NoBase36, version, ResourceSourceFlags::PatchFile);
            HRESULT hr = blob.SaveToFile(path);
            if (FAILED(hr))
            {
                result.writeFailed = true;
                // No file with a part of the view stays.
                sci::Error error = sci::FromHResult(hr, "Writing " + path);
                DeleteFileA(path.c_str());
                return sci::Fail(error);
            }
            output.Detail("Wrote " + path);
            return sci::Ok();
        }
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
}
