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
            // No entry of the palette changed.
            bool unchanged = false;
            // The failure is a patch file that could not be written.
            bool writeFailed = false;
        };

        // The messages of a view that got new colours.
        void ReportColors(const std::string &viewText, const PaletteAction &action, const ViewPaletteEdit &edit, CliOutput &output)
        {
            if (!edit.AnyEntry())
            {
                output.Warning(fmt::format("{0}: its palette stores {1}, and no entry of the range {2}-{3}: the view does not change", viewText, StoredText(edit.before), action.first, action.last));
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

        // The messages of a view whose entries lost their mark.
        void ReportClear(const std::string &viewText, const PaletteAction &action, const ViewPaletteEdit &edit, CliOutput &output)
        {
            if (!edit.AnyEntry())
            {
                output.Warning(fmt::format("{0}: its palette has no mark for an entry of the range {1}-{2}: the view does not change", viewText, action.first, action.last));
            }
            else if (edit.after.entryCount != edit.before.entryCount)
            {
                output.Detail(fmt::format("{0}: its palette stores {1} (before: {2}), so the entries {3} have no mark", viewText, StoredText(edit.after), StoredText(edit.before), RunsText(edit.entries)));
            }
            else
            {
                output.Detail(fmt::format("{0}: no mark for the entries {1}", viewText, RunsText(edit.entries)));
            }
        }

        sci::Status ChangeViewPalette(GameSession &session, int number, const PaletteAction &action, const std::string &outDir, bool dryRun, CliOutput &output, ViewPaletteResult &result)
        {
            const std::string viewText = ViewText(number);
            // The copy that the game uses: a patch file, else the package.
            std::unique_ptr<ResourceBlob> source = session.ResourceMap().MostRecentResource(ResourceType::View, number, false);
            if (!source)
            {
                return sci::Fail(sci::ErrorCode::NotFound, fmt::format("the game has no view {0}", number));
            }
            SCI_TRY(CheckResourceData(*source));
            const std::vector<uint8_t> data(source->GetData(), source->GetData() + source->GetLength());
            const SCIVersion &version = session.Version();

            // A view that its reader repaired is not the view of the game: the
            // check of its pixels would be a check of other pixels.
            RepairLogSink repairs;
            sci::Result<ViewPaletteEdit> edited = [&]() -> sci::Result<ViewPaletteEdit>
            {
                ScopedCoreLogSink scopedSink(repairs);
                return action.clearUsed ? ClearViewPaletteMarks(version, data, action.first, action.last) :
                    ReplaceViewPaletteColors(version, data, action.first, action.last, action.colors);
            }();
            SCI_TRY_ASSIGN(ViewPaletteEdit edit, std::move(edited));
            if (!repairs.Text().empty())
            {
                return sci::Fail(sci::ErrorCode::Format, "the reader repaired damaged data: " + repairs.Text());
            }
            if (action.clearUsed)
            {
                ReportClear(viewText, action, edit, output);
            }
            else
            {
                ReportColors(viewText, action, edit, output);
            }
            result.unchanged = !edit.AnyEntry();
            SCI_TRY(CheckResourceSize(version, (DWORD)edit.data.size(), ResourceType::View));

            const std::string path = outDir + "\\" + GetFileNameFor(ResourceType::View, number, NoBase36, version);
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
                return sci::Fail(sci::FromHResult(hr, "Writing " + path));
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
            // The exception boundary of the view.
            sci::Status status = sci::Guard("changing the palette of " + ViewText(number), [&]() -> sci::Status
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
                written++;
                unchanged += result.unchanged ? 1 : 0;
            }
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
