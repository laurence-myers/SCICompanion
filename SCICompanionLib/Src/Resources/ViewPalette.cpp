#include "stdafx.h"
#include "ViewPalette.h"
#include "PaletteUse.h"
#include "PicImage.h"
#include "View.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceUtil.h"
#include "ResourceSourceFlags.h"
#include "GameFolderHelper.h"
#include "format.h"

namespace
{
    // The view as SCI Companion reads it.
    sci::Result<std::unique_ptr<ResourceEntity>> ParseView(const SCIVersion &version, const std::vector<uint8_t> &data)
    {
        GameFolderHelper helper;
        // The blob has a name, so that it does not look for one in a game.
        ResourceBlob blob(helper, "view", ResourceType::View, data, version.DefaultVolumeFile, -1, NoBase36, version, ResourceSourceFlags::PatchFile);
        sci::Result<std::unique_ptr<ResourceEntity>> view = TryCreateResourceFromResourceData(blob);
        if (!view)
        {
            // The number of the view is not known here: the caller names it.
            view.error().where.resource.clear();
        }
        else if ((*view)->TryGetComponent<RasterComponent>() == nullptr)
        {
            return sci::Fail(sci::ErrorCode::Format, "the resource is not a view");
        }
        return view;
    }

    // What these functions read of a view before they change it.
    struct ViewRead
    {
        std::unique_ptr<ResourceEntity> view;
        size_t paletteOffset = 0;
        PaletteLayout layout;
        std::array<bool, 256> used = {};
        // The entries of the pixels of the cels, without the transparent
        // colour of each cel.
        std::array<bool, 256> pixelEntries = {};
    };

    sci::Result<ViewRead> ReadView(const SCIVersion &version, const std::vector<uint8_t> &data)
    {
        if ((version.ViewFormat != ViewFormat::VGA1_1) && (version.ViewFormat != ViewFormat::VGA2))
        {
            return sci::Fail(sci::ErrorCode::Unsupported, "the palette of a view of this game version cannot change; only a view of SCI1.1 or SCI2 can");
        }
        ViewRead read;
        SCI_TRY_ASSIGN(read.view, ParseView(version, data));
        const PaletteComponent *palette = read.view->TryGetComponent<PaletteComponent>();
        if (palette == nullptr)
        {
            return sci::Fail(sci::ErrorCode::Unsupported, "the view has no palette");
        }
        read.paletteOffset = ReadViewPaletteOffsetVGA11(data.data(), data.size());
        if (read.paletteOffset == 0)
        {
            // The reader gave the data to the reader of SCI1 views, which
            // found a palette.
            return sci::Fail(sci::ErrorCode::Unsupported, "the view has the form of an SCI1 view; the palette of such a view cannot change");
        }
        SCI_TRY_ASSIGN(read.layout, ReadPaletteLayout(data.data(), data.size(), read.paletteOffset));
        SCI_TRY_ASSIGN(read.used, ReadUsedPaletteEntries(data.data(), data.size(), read.paletteOffset));
        read.pixelEntries = DescribeViewPaletteUse(read.view->GetComponent<RasterComponent>(), palette).pixelEntries;
        return std::move(read);
    }

    // The two cels have the same size, placement, transparent colour and
    // pixels.
    bool SameCel(const Cel &one, const Cel &two)
    {
        if ((one.size != two.size) || (one.placement != two.placement) || (one.TransparentColor != two.TransparentColor) || (one.Data.size() != two.Data.size()))
        {
            return false;
        }
        const size_t width = one.size.cx;
        const size_t stride = StrideOf(one);
        for (size_t y = 0; (width > 0) && (y < (size_t)one.size.cy); y++)
        {
            const size_t at = y * stride;
            if ((at + width) > one.Data.size())
            {
                // A cel with fewer bytes than its size needs.
                return (at >= one.Data.size()) || (memcmp(&one.Data[at], &two.Data[at], one.Data.size() - at) == 0);
            }
            if (memcmp(&one.Data[at], &two.Data[at], width) != 0)
            {
                return false;
            }
        }
        return true;
    }

    // The edit of the new bytes of a view that was read from the old bytes.
    // The view must read back with the cels that it had: the change is one
    // of palette bytes.
    sci::Result<ViewPaletteEdit> FinishEdit(const SCIVersion &version, const ViewRead &read, const std::vector<uint8_t> &oldData, std::vector<uint8_t> &&data, const std::array<bool, 256> &entries)
    {
        ViewPaletteEdit edit;
        edit.changed = (data != oldData);
        edit.entries = entries;
        edit.usedBefore = read.used;
        edit.before = read.layout;
        SCI_TRY_ASSIGN(edit.after, ReadPaletteLayout(data.data(), data.size(), read.paletteOffset));
        SCI_TRY_ASSIGN(std::unique_ptr<ResourceEntity> readBack, ParseView(version, data));
        if (!SameViewCels(read.view->GetComponent<RasterComponent>(), readBack->GetComponent<RasterComponent>()))
        {
            return sci::Fail(sci::ErrorCode::Unsupported, "the cels of the view are not the same after the change of its palette: a cel and the palette have bytes in common");
        }
        edit.data = std::move(data);
        return std::move(edit);
    }
}

bool SameViewCels(const RasterComponent &one, const RasterComponent &two)
{
    if (one.Loops.size() != two.Loops.size())
    {
        return false;
    }
    for (size_t loop = 0; loop < one.Loops.size(); loop++)
    {
        const std::vector<Cel> &oneCels = one.Loops[loop].Cels;
        const std::vector<Cel> &twoCels = two.Loops[loop].Cels;
        if ((oneCels.size() != twoCels.size()) || !std::equal(oneCels.begin(), oneCels.end(), twoCels.begin(), SameCel))
        {
            return false;
        }
    }
    return true;
}

bool ViewPaletteEdit::AnyEntry() const
{
    return std::find(entries.begin(), entries.end(), true) != entries.end();
}

sci::Result<ViewPaletteEdit> ReplaceViewPaletteColors(const SCIVersion &version, const std::vector<uint8_t> &data, int first, int last, const std::array<IndexedBmpColor, 256> &colors)
{
    SCI_TRY_ASSIGN(ViewRead read, ReadView(version, data));
    if ((first < 0) || (last > 255) || (first > last))
    {
        return sci::Fail(sci::ErrorCode::Usage, fmt::format("{0}-{1} is not a range of palette entries", first, last));
    }
    std::vector<uint8_t> patched = data;
    std::array<bool, 256> entries = {};
    // The stored entries of the range.
    const int from = (std::max)(first, read.layout.firstEntry);
    const int to = (std::min)(last, read.layout.firstEntry + read.layout.entryCount - 1);
    if (from <= to)
    {
        RGBQUAD quads[256] = {};
        for (size_t i = 0; i < colors.size(); i++)
        {
            quads[i].rgbRed = colors[i].red;
            quads[i].rgbGreen = colors[i].green;
            quads[i].rgbBlue = colors[i].blue;
        }
        SCI_TRY(PatchPaletteColors(patched, read.paletteOffset, from, to, quads));
        std::fill(entries.begin() + from, entries.begin() + to + 1, true);
    }
    return FinishEdit(version, read, data, std::move(patched), entries);
}

sci::Result<ViewPaletteEdit> ClearViewPaletteMarks(const SCIVersion &version, const std::vector<uint8_t> &data, int first, int last)
{
    SCI_TRY_ASSIGN(ViewRead read, ReadView(version, data));
    std::vector<uint8_t> patched = data;
    SCI_TRY_ASSIGN(const PaletteMarksChange change, ClearPaletteMarks(patched, read.paletteOffset, first, last));
    std::array<bool, 256> lost = {};
    for (size_t i = 0; i < lost.size(); i++)
    {
        lost[i] = change.entries[i] && read.pixelEntries[i];
    }
    const std::vector<IndexRun> lostRuns = IndexRunsOf(lost);
    if (!lostRuns.empty())
    {
        return sci::Fail(sci::ErrorCode::Unsupported, fmt::format("pixels of the view have the entries {0}, which would lose their mark: the view would lose the colours of these pixels", IndexRunsText(lostRuns)));
    }
    SCI_TRY_ASSIGN(ViewPaletteEdit edit, FinishEdit(version, read, data, std::move(patched), change.entries));
    edit.sizeWordKept = change.sizeWordKept;
    return std::move(edit);
}
