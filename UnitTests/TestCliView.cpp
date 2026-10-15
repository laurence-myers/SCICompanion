#include "stdafx.h"
#include "CppUnitTest.h"
#include "Cli.h"
#include "CliCommands.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "CliView.h"
#include "GameSession.h"
#include "IndexedBmp.h"
#include "PaletteOperations.h"
#include "PaletteUse.h"
#include "PicImage.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceMap.h"
#include "ResourceSourceFlags.h"
#include "ResourceUtil.h"
#include "View.h"
#include "ViewPalette.h"
#include "ViewSidecar.h"
#include "crc.h"
#include "format.h"
#include "SierraViewBytes.h"
#include "TestSupport.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // The size of the header of a palette, and of the palette that the view
    // writer makes: the header and 256 entries of 4 bytes.
    const size_t PaletteHeaderSize = 37;
    const size_t WrittenPaletteSize = PaletteHeaderSize + (256 * 4);

    // The header of the patch file of a view of SCI1.1 or SCI2: the type
    // byte, 0x80, and 24 bytes of 0.
    std::vector<uint8_t> ViewPatchHeader()
    {
        std::vector<uint8_t> header = { 0x80, 0x80 };
        header.resize(26, 0);
        return header;
    }

    // The entries of the array that are true, as runs: "10-12,90".
    std::string RunsOf(const std::array<bool, 256> &entries)
    {
        return IndexRunsText(IndexRunsOf(entries));
    }

    // The colour that entry e of a test view has.
    IndexedBmpColor OldColor(int entry)
    {
        return IndexedBmpColor{ (uint8_t)(0x10 + entry), (uint8_t)(0x20 + entry), (uint8_t)(0x30 + entry) };
    }

    // The colour that a test gives to entry e: each byte is another value
    // than the byte of OldColor.
    IndexedBmpColor NewColor(int entry)
    {
        return IndexedBmpColor{ (uint8_t)(0x90 + entry), (uint8_t)(0xa0 + entry), (uint8_t)(0xb0 + entry) };
    }

    std::array<IndexedBmpColor, 256> NewColors()
    {
        std::array<IndexedBmpColor, 256> colors;
        for (int entry = 0; entry < 256; entry++)
        {
            colors[entry] = NewColor(entry);
        }
        return colors;
    }

    // The bytes of a view of the version with 1 cel of 3 x 2 pixels and a
    // palette of 256 entries of 4 bytes. The cel has the transparent colour
    // 0. The palette marks the entries 60 to 99 (flag 1) and 200 (flag 3) as
    // used, and entry e has OldColor(e). With no palette, the view has none.
    enum class SecondLoop
    {
        None,
        // A second loop with no cel, as some of Sierra's views have: the
        // reader of the views gives it a 1 x 1 cel and logs nothing.
        Empty,
        // A second loop that mirrors itself: the reader of the views logs a
        // warning and gives that loop a cel (a repair).
        MirrorsItself,
    };

    std::vector<uint8_t> MakeViewBytes(const SCIVersion &version, const std::vector<uint8_t> &pixels = { 64, 65, 0, 0, 70, 64 }, bool withPalette = true, SecondLoop secondLoop = SecondLoop::None)
    {
        std::unique_ptr<ResourceEntity> view(CreateViewResource(version));
        RasterComponent &raster = view->GetComponent<RasterComponent>();
        raster.Loops.clear();
        Cel cel;
        cel.size = size16(3, 2);
        cel.TransparentColor = 0;
        cel.Data.allocate(cel.GetDataSize());
        cel.Data.fill(0);
        for (size_t y = 0; y < 2; y++)
        {
            for (size_t x = 0; x < 3; x++)
            {
                cel.Data[(y * cel.GetStride()) + x] = pixels[(y * 3) + x];
            }
        }
        Loop loop;
        loop.Cels.push_back(cel);
        raster.Loops.push_back(loop);
        if (secondLoop != SecondLoop::None)
        {
            Loop second;
            if (secondLoop == SecondLoop::MirrorsItself)
            {
                second.IsMirror = true;
                second.MirrorOf = 1;
            }
            raster.Loops.push_back(second);
        }
        if (withPalette)
        {
            view->AddComponent(std::make_unique<PaletteComponent>());
            PaletteComponent &palette = view->GetComponent<PaletteComponent>();
            for (int entry = 0; entry < 256; entry++)
            {
                const IndexedBmpColor color = OldColor(entry);
                palette.Colors[entry].rgbRed = color.red;
                palette.Colors[entry].rgbGreen = color.green;
                palette.Colors[entry].rgbBlue = color.blue;
                palette.Colors[entry].rgbReserved = ((entry >= 60) && (entry <= 99)) ? 1 : ((entry == 200) ? 3 : 0);
            }
        }
        sci::ostream stream;
        std::map<BlobKey, uint32_t> propertyBag;
        view->WriteTo(stream, true, 5, propertyBag);
        return std::vector<uint8_t>(stream.GetInternalPointer(), stream.GetInternalPointer() + stream.GetDataSize());
    }

    // The offset of the palette of the view of MakeViewBytes.
    size_t PaletteOffsetOf(const std::vector<uint8_t> &view)
    {
        const uint32_t offset = ReadViewPaletteOffsetVGA11(view.data(), view.size());
        Assert::IsTrue((offset != 0) && (((size_t)offset + WrittenPaletteSize) <= view.size()), L"setup: the view has a palette of 256 entries of 4 bytes");
        return offset;
    }

    // The size word of a palette (at offset 13 of its header): the 22 bytes
    // of the header after the word, and the entries.
    uint16_t SizeWordFor(int count, int entrySize)
    {
        return (uint16_t)(22 + (count * entrySize));
    }

    uint16_t SizeWordOf(const std::vector<uint8_t> &view, size_t palette)
    {
        return (uint16_t)(view[palette + 13] | (view[palette + 14] << 8));
    }

    void SetSizeWord(std::vector<uint8_t> &view, size_t palette, uint16_t value)
    {
        view[palette + 13] = (uint8_t)(value & 0xff);
        view[palette + 14] = (uint8_t)(value >> 8);
    }

    // Gives the palette of the view of MakeViewBytes other stored entries, in
    // place: the entries first to first + count - 1, of 3 bytes (the header
    // has the flag of all of them) or of 4 bytes (each entry keeps its flag:
    // 1 for 60 to 99, 3 for 200, else 0). Entry e keeps OldColor(e), and the
    // size word of the header gets the value of the entries. The bytes of
    // the old palette after the new entries get 0xee: they are not a part of
    // the palette.
    void StoreEntries(std::vector<uint8_t> &view, int first, int count, bool threeByte, uint8_t headerFlag)
    {
        const size_t palette = PaletteOffsetOf(view);
        const int entrySize = threeByte ? 3 : 4;
        SetSizeWord(view, palette, SizeWordFor(count, entrySize));
        view[palette + 25] = (uint8_t)first;
        view[palette + 29] = (uint8_t)(count & 0xff);
        view[palette + 30] = (uint8_t)(count >> 8);
        view[palette + 31] = headerFlag;
        view[palette + 32] = threeByte ? 1 : 0;
        std::fill(view.begin() + palette + PaletteHeaderSize, view.begin() + palette + WrittenPaletteSize, (uint8_t)0xee);
        for (int i = 0; i < count; i++)
        {
            const int entry = first + i;
            const IndexedBmpColor color = OldColor(entry);
            uint8_t *bytes = &view[palette + PaletteHeaderSize + ((size_t)i * entrySize)];
            if (!threeByte)
            {
                *bytes++ = ((entry >= 60) && (entry <= 99)) ? 1 : ((entry == 200) ? 3 : 0);
            }
            bytes[0] = color.red;
            bytes[1] = color.green;
            bytes[2] = color.blue;
        }
    }

    // The view of MakeViewBytes with 3-byte entries.
    std::vector<uint8_t> MakeThreeByteViewBytes(const SCIVersion &version, int first, int count, uint8_t flag, const std::vector<uint8_t> &pixels = { 64, 65, 0, 0, 70, 64 }, SecondLoop secondLoop = SecondLoop::None)
    {
        std::vector<uint8_t> view = MakeViewBytes(version, pixels, true, secondLoop);
        StoreEntries(view, first, count, true, flag);
        return view;
    }

    // The view of MakeViewBytes with 4-byte entries that do not start at
    // entry 0.
    std::vector<uint8_t> MakeFourByteViewBytes(const SCIVersion &version, int first, int count)
    {
        std::vector<uint8_t> view = MakeViewBytes(version);
        StoreEntries(view, first, count, false, 0);
        return view;
    }

    // How many bytes of the two are not the same; the sizes must be equal.
    size_t DifferentBytes(const std::vector<uint8_t> &one, const std::vector<uint8_t> &two)
    {
        Assert::AreEqual(one.size(), two.size(), L"the size of the view stays");
        size_t count = 0;
        for (size_t i = 0; i < one.size(); i++)
        {
            count += (one[i] != two[i]) ? 1 : 0;
        }
        return count;
    }

    // The entries that the palette of the view marks as used.
    std::string UsedRunsOf(const std::vector<uint8_t> &view)
    {
        sci::Result<std::array<bool, 256>> used = ReadUsedPaletteEntries(view.data(), view.size(), ReadViewPaletteOffsetVGA11(view.data(), view.size()));
        AssertOk(used, "the used entries of the view");
        return RunsOf(*used);
    }

    template<typename R>
    void AssertFails(const R &result, sci::ErrorCode code, const std::string &textInMessage)
    {
        Assert::IsFalse(result.has_value(), Wide("the call must fail: " + textInMessage).c_str());
        Assert::IsTrue(result.error().code == code, Wide(result.error().ToString()).c_str());
        Assert::IsTrue(result.error().message.find(textInMessage) != std::string::npos, Wide(result.error().ToString()).c_str());
    }

    // The two versions whose views the functions take.
    struct VersionCase
    {
        const char *name;
        const SCIVersion &version;
    };
    std::vector<VersionCase> Versions()
    {
        return { { "SCI1.1", sciVersion1_1 }, { "SCI2", sciVersion2 } };
    }

    // The names of the files of a folder (not of its subfolders), in order.
    std::vector<std::string> FileNamesOf(const std::string &folder)
    {
        std::vector<std::string> names;
        for (const auto &entry : fs::directory_iterator(folder))
        {
            names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    // Each file of a folder tree, with its bytes.
    std::map<std::string, std::vector<uint8_t>> FilesOf(const std::string &folder)
    {
        std::map<std::string, std::vector<uint8_t>> files;
        for (const auto &entry : fs::recursive_directory_iterator(folder))
        {
            if (entry.is_regular_file())
            {
                files[entry.path().string()] = ReadFileBytes(entry.path().string());
            }
        }
        return files;
    }

    bool Has(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }

    // The first line that starts with the text; empty when no line does.
    std::string LineWith(const std::string &text, const std::string &start)
    {
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && (line.back() == '\r'))
            {
                line.pop_back();
            }
            if (line.compare(0, start.size(), start) == 0)
            {
                return line;
            }
        }
        return std::string();
    }

    // The lines of the text, with no line end.
    std::vector<std::string> LinesOf(const std::string &text)
    {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && (line.back() == '\r'))
            {
                line.pop_back();
            }
            lines.push_back(line);
        }
        return lines;
    }

    // The cells of a line of a tsv table.
    std::vector<std::string> TabFieldsOf(const std::string &line)
    {
        std::vector<std::string> fields;
        size_t start = 0;
        for (;;)
        {
            const size_t tab = line.find('\t', start);
            fields.push_back(line.substr(start, (tab == std::string::npos) ? std::string::npos : (tab - start)));
            if (tab == std::string::npos)
            {
                return fields;
            }
            start = tab + 1;
        }
    }

    // The text with one space for each run of spaces and line ends: the
    // help of scic wraps its lines.
    std::string OneLine(const std::string &text)
    {
        std::string line;
        for (char ch : text)
        {
            const bool space = (ch == ' ') || (ch == '\n') || (ch == '\r') || (ch == '\t');
            if (!space)
            {
                line += ch;
            }
            else if (!line.empty() && (line.back() != ' '))
            {
                line += ' ';
            }
        }
        return line;
    }
}

namespace UnitTests
{
    // The change of the palette of a view in the bytes of its resource
    // (ViewPalette.h), on views that the tests make: the template games have
    // no view with a palette that marks an entry.
    TEST_CLASS(TestViewPalette)
    {
    public:
        TEST_METHOD(ReplaceColors_FourByteEntries_ChangesOnlyTheColoursOfTheRange)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeViewBytes(test.version);
                const size_t entries = PaletteOffsetOf(view) + PaletteHeaderSize;

                sci::Result<ViewPaletteEdit> edit = ReplaceViewPaletteColors(test.version, view, 80, 90, NewColors());

                AssertOk(edit, test.name);
                // The same bytes, but the red, the green and the blue of the
                // entries 80 to 90: a 4-byte entry has its flag byte first.
                std::vector<uint8_t> expected = view;
                for (int entry = 80; entry <= 90; entry++)
                {
                    const IndexedBmpColor color = NewColor(entry);
                    expected[entries + ((size_t)entry * 4) + 1] = color.red;
                    expected[entries + ((size_t)entry * 4) + 2] = color.green;
                    expected[entries + ((size_t)entry * 4) + 3] = color.blue;
                }
                Assert::IsTrue(expected == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::AreEqual((size_t)33, DifferentBytes(view, edit->data), Wide(test.name).c_str());
                Assert::AreEqual(std::string("80-90"), RunsOf(edit->entries));
                Assert::AreEqual(std::string("60-99,200"), RunsOf(edit->usedBefore));
                Assert::AreEqual(std::string("60-99,200"), UsedRunsOf(edit->data), L"the marks stay");
                Assert::AreEqual(256, edit->after.entryCount);
            }
        }

        TEST_METHOD(ReplaceColors_ThreeByteEntries_ChangesOnlyTheStoredEntriesOfTheRange)
        {
            for (const VersionCase &test : Versions())
            {
                // The palette stores the entries 60 to 99.
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 60, 40, 1);
                const size_t entries = PaletteOffsetOf(view) + PaletteHeaderSize;

                sci::Result<ViewPaletteEdit> edit = ReplaceViewPaletteColors(test.version, view, 80, 235, NewColors());

                AssertOk(edit, test.name);
                std::vector<uint8_t> expected = view;
                for (int entry = 80; entry <= 99; entry++)
                {
                    const IndexedBmpColor color = NewColor(entry);
                    expected[entries + ((size_t)(entry - 60) * 3)] = color.red;
                    expected[entries + ((size_t)(entry - 60) * 3) + 1] = color.green;
                    expected[entries + ((size_t)(entry - 60) * 3) + 2] = color.blue;
                }
                Assert::IsTrue(expected == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::AreEqual((size_t)60, DifferentBytes(view, edit->data), Wide(test.name).c_str());
                Assert::AreEqual(std::string("80-99"), RunsOf(edit->entries));
                Assert::AreEqual(std::string("60-99"), UsedRunsOf(edit->data), L"the marks stay");
                Assert::AreEqual(40, edit->after.entryCount);
            }
        }

        TEST_METHOD(ReplaceColors_NoStoredEntryInTheRange_ChangesNothing)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 60, 40, 1);

                sci::Result<ViewPaletteEdit> edit = ReplaceViewPaletteColors(test.version, view, 100, 120, NewColors());

                AssertOk(edit, test.name);
                Assert::IsTrue(view == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::IsFalse(edit->AnyEntry());
                Assert::IsFalse(edit->changed);
            }
        }

        // The palette stores the entries 60 to 99, and the range starts
        // before entry 60: the stored entries of the range change.
        TEST_METHOD(ReplaceColors_RangeThatStartsBeforeTheFirstStoredEntry_ChangesTheStoredEntries)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 60, 40, 1);
                const size_t entries = PaletteOffsetOf(view) + PaletteHeaderSize;

                sci::Result<ViewPaletteEdit> edit = ReplaceViewPaletteColors(test.version, view, 40, 70, NewColors());

                AssertOk(edit, test.name);
                std::vector<uint8_t> expected = view;
                for (int entry = 60; entry <= 70; entry++)
                {
                    const IndexedBmpColor color = NewColor(entry);
                    expected[entries + ((size_t)(entry - 60) * 3)] = color.red;
                    expected[entries + ((size_t)(entry - 60) * 3) + 1] = color.green;
                    expected[entries + ((size_t)(entry - 60) * 3) + 2] = color.blue;
                }
                Assert::IsTrue(expected == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::AreEqual((size_t)33, DifferentBytes(view, edit->data), Wide(test.name).c_str());
                Assert::AreEqual(std::string("60-70"), RunsOf(edit->entries));
                Assert::IsTrue(edit->changed);
            }
        }

        // A view whose cel has its pixel bytes in the palette: the bytes of
        // the colours of the entries 80 and 81 are also pixels of the cel.
        // New colours for these entries would change the cel.
        TEST_METHOD(ReplaceColors_CelWithBytesOfThePalette_IsUnsupported)
        {
            for (const VersionCase &test : Versions())
            {
                std::vector<uint8_t> view = MakeViewBytes(test.version);
                const size_t palette = PaletteOffsetOf(view);
                // The view has 1 loop: its header is after the header of the
                // view (18 bytes), and has the offset of the header of its
                // cel at 12. The header of a cel has the offset of its pixel
                // bytes at 28.
                const size_t celHeader = view[18 + 12] | (view[18 + 13] << 8);
                Assert::AreEqual((size_t)34, celHeader, L"setup: the header of the cel is after the header of the loop");
                const size_t colorsOf80 = palette + PaletteHeaderSize + ((size_t)80 * 4) + 1;
                view[celHeader + 28] = (uint8_t)(colorsOf80 & 0xff);
                view[celHeader + 29] = (uint8_t)((colorsOf80 >> 8) & 0xff);
                view[celHeader + 30] = 0;
                view[celHeader + 31] = 0;

                // The colours of other entries are not bytes of the cel.
                AssertOk(ReplaceViewPaletteColors(test.version, view, 100, 120, NewColors()), test.name);
                AssertFails(ReplaceViewPaletteColors(test.version, view, 80, 81, NewColors()), sci::ErrorCode::Unsupported, "a cel and the palette have bytes in common");
            }
        }

        TEST_METHOD(ClearMarks_FourByteEntries_ClearsOnlyTheFlagBytesOfTheRange)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeViewBytes(test.version);
                const size_t entries = PaletteOffsetOf(view) + PaletteHeaderSize;

                sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, view, 80, 235);

                AssertOk(edit, test.name);
                // The same bytes, but the flag bytes of the entries of the
                // range that had a mark: 80 to 99, and 200.
                std::vector<uint8_t> expected = view;
                for (int entry = 80; entry <= 99; entry++)
                {
                    expected[entries + ((size_t)entry * 4)] = 0;
                }
                expected[entries + ((size_t)200 * 4)] = 0;
                Assert::IsTrue(expected == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::AreEqual((size_t)21, DifferentBytes(view, edit->data), Wide(test.name).c_str());
                Assert::AreEqual(std::string("80-99,200"), RunsOf(edit->entries));
                Assert::AreEqual(std::string("60-79"), UsedRunsOf(edit->data));
                Assert::AreEqual(256, edit->after.entryCount);
            }
        }

        TEST_METHOD(ClearMarks_ThreeByteEntries_StoresFewerEntries)
        {
            for (const VersionCase &test : Versions())
            {
                // The palette stores the entries 0 to 235, as a view of the
                // hi-res Leisure Suit Larry 6 does.
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 0, 236, 1);
                const size_t palette = PaletteOffsetOf(view);
                Assert::AreEqual(std::string("0-235"), UsedRunsOf(view), L"setup: the marks of the view");

                // A range that ends at the last stored entry, and one that
                // ends after it.
                for (int last : { 235, 255 })
                {
                    sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, view, 80, last);

                    AssertOk(edit, test.name);
                    // The same bytes, but the count of the stored entries
                    // (236, then 80: the first of its 2 bytes) and the size
                    // word (22 + 236 x 3 = 730, then 22 + 80 x 3 = 262: 2
                    // bytes).
                    Assert::AreEqual(730, (int)SizeWordOf(view, palette), L"setup: the size word of the palette");
                    std::vector<uint8_t> expected = view;
                    expected[palette + 29] = 80;
                    SetSizeWord(expected, palette, 262);
                    Assert::IsTrue(expected == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                    Assert::AreEqual((size_t)3, DifferentBytes(view, edit->data), Wide(test.name).c_str());
                    Assert::AreEqual(std::string("80-235"), RunsOf(edit->entries));
                    Assert::AreEqual(std::string("0-79"), UsedRunsOf(edit->data));
                    Assert::AreEqual(236, edit->before.entryCount);
                    Assert::AreEqual(80, edit->after.entryCount);
                    Assert::IsFalse(edit->sizeWordKept);
                    Assert::IsTrue(edit->changed);
                }

                // A size word that does not have the value of the entries
                // keeps its value: only the count changes.
                for (uint16_t word : { (uint16_t)0, (uint16_t)731, (uint16_t)1046 })
                {
                    std::vector<uint8_t> otherWord = view;
                    SetSizeWord(otherWord, palette, word);

                    sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, otherWord, 80, 235);

                    AssertOk(edit, test.name);
                    std::vector<uint8_t> expected = otherWord;
                    expected[palette + 29] = 80;
                    Assert::IsTrue(expected == edit->data, Wide(fmt::format("the bytes of the view with the size word {0}: {1}", word, test.name)).c_str());
                    Assert::AreEqual((size_t)1, DifferentBytes(otherWord, edit->data), Wide(test.name).c_str());
                    Assert::IsTrue(edit->sizeWordKept);
                }
            }
        }

        // The palette stores the entries 60 to 99: the count that stays is
        // the count of the entries before the range, not the first entry of
        // the range.
        TEST_METHOD(ClearMarks_ThreeByteEntries_FirstStoredEntryAbove0_StoresFewerEntries)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 60, 40, 1);
                const size_t palette = PaletteOffsetOf(view);

                sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, view, 80, 99);

                AssertOk(edit, test.name);
                // The count (40, then 20) and the size word (22 + 40 x 3 =
                // 142, then 22 + 20 x 3 = 82: 1 byte).
                std::vector<uint8_t> expected = view;
                expected[palette + 29] = 20;
                SetSizeWord(expected, palette, 82);
                Assert::IsTrue(expected == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::AreEqual((size_t)2, DifferentBytes(view, edit->data), Wide(test.name).c_str());
                Assert::AreEqual(std::string("80-99"), RunsOf(edit->entries));
                Assert::AreEqual(std::string("60-79"), UsedRunsOf(edit->data));
                Assert::AreEqual(20, edit->after.entryCount);
            }
        }

        // The palette stores the entries 40 to 139 with 4 bytes each: the
        // flag byte of an entry is at its place after the first stored entry.
        TEST_METHOD(ClearMarks_FourByteEntries_FirstStoredEntryAbove0_ClearsTheFlagBytesOfTheRange)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeFourByteViewBytes(test.version, 40, 100);
                const size_t entries = PaletteOffsetOf(view) + PaletteHeaderSize;
                Assert::AreEqual(std::string("60-99"), UsedRunsOf(view), L"setup: the marks of the view");

                sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, view, 80, 120);

                AssertOk(edit, test.name);
                std::vector<uint8_t> expected = view;
                for (int entry = 80; entry <= 99; entry++)
                {
                    expected[entries + ((size_t)(entry - 40) * 4)] = 0;
                }
                Assert::IsTrue(expected == edit->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::AreEqual((size_t)20, DifferentBytes(view, edit->data), Wide(test.name).c_str());
                Assert::AreEqual(std::string("80-99"), RunsOf(edit->entries));
                Assert::AreEqual(std::string("60-79"), UsedRunsOf(edit->data));
            }
        }

        // The flag 0 in the header marks no 3-byte entry: no entry can lose
        // a mark, so the view does not change, a range that ends before the
        // last stored entry is no error, and a pixel in the range is no
        // error.
        TEST_METHOD(ClearMarks_ThreeByteEntries_FlagOfTheHeaderIs0_ChangesNothing)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 0, 236, 0);
                Assert::AreEqual(std::string(""), UsedRunsOf(view), L"setup: the palette marks no entry");

                // The pixels of the cel have the entries 64, 65 and 70.
                for (const auto &range : { std::make_pair(80, 235), std::make_pair(80, 200), std::make_pair(60, 235), std::make_pair(0, 255) })
                {
                    sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, view, range.first, range.second);

                    AssertOk(edit, fmt::format("{0}: {1}-{2}", test.name, range.first, range.second));
                    Assert::IsTrue(view == edit->data, Wide(fmt::format("the bytes of the view: {0}: {1}-{2}", test.name, range.first, range.second)).c_str());
                    Assert::IsFalse(edit->AnyEntry());
                    Assert::IsFalse(edit->changed);
                    Assert::AreEqual(236, edit->after.entryCount);
                }
            }
        }

        TEST_METHOD(ClearMarks_ThreeByteEntries_RangeNotAtTheEnd_IsUnsupported)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 0, 236, 1);

                AssertFails(ClearViewPaletteMarks(test.version, view, 80, 200), sci::ErrorCode::Unsupported, "the range 80-200 does not reach the last stored entry (235)");
                // The last stored entry is 235: a range that ends at 234
                // leaves it.
                AssertFails(ClearViewPaletteMarks(test.version, view, 80, 234), sci::ErrorCode::Unsupported, "the range 80-234 does not reach the last stored entry (235)");

                // A range with no stored entry changes nothing.
                const std::vector<uint8_t> few = MakeThreeByteViewBytes(test.version, 0, 80, 1);
                sci::Result<ViewPaletteEdit> none = ClearViewPaletteMarks(test.version, few, 80, 200);
                AssertOk(none, test.name);
                Assert::IsTrue(few == none->data, Wide(std::string("the bytes of the view: ") + test.name).c_str());
                Assert::IsFalse(none->AnyEntry());
            }
        }

        TEST_METHOD(ClearMarks_PixelWithAnEntryThatLosesItsMark_IsUnsupported)
        {
            for (const VersionCase &test : Versions())
            {
                // The pixels of the cel have the entries 64, 65 and 70 (0 is
                // the transparent colour).
                const std::vector<uint8_t> fourByte = MakeViewBytes(test.version);
                AssertFails(ClearViewPaletteMarks(test.version, fourByte, 60, 70), sci::ErrorCode::Unsupported, "pixels of the view have the entries 64-65,70");
                AssertFails(ClearViewPaletteMarks(test.version, fourByte, 70, 235), sci::ErrorCode::Unsupported, "pixels of the view have the entries 70,");

                const std::vector<uint8_t> threeByte = MakeThreeByteViewBytes(test.version, 0, 236, 1);
                AssertFails(ClearViewPaletteMarks(test.version, threeByte, 65, 235), sci::ErrorCode::Unsupported, "pixels of the view have the entries 65,70");

                // A pixel with an entry of the range that has no mark loses
                // nothing: the palette of the view does not give its colour.
                std::vector<uint8_t> noMark = fourByte;
                noMark[PaletteOffsetOf(noMark) + PaletteHeaderSize + ((size_t)70 * 4)] = 0;
                sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, noMark, 66, 235);
                AssertOk(edit, test.name);
                Assert::AreEqual(std::string("66-69,71-99,200"), RunsOf(edit->entries));

                // The transparent colour of a cel is not a pixel: each pixel
                // of this view has entry 0, which is the transparent colour
                // of its cel and which the palette marks as used.
                const std::vector<uint8_t> onlyTransparent = MakeThreeByteViewBytes(test.version, 0, 236, 1, { 0, 0, 0, 0, 0, 0 });
                sci::Result<ViewPaletteEdit> noPixel = ClearViewPaletteMarks(test.version, onlyTransparent, 0, 235);
                AssertOk(noPixel, test.name);
                Assert::AreEqual(std::string("0-235"), RunsOf(noPixel->entries));
            }
        }

        TEST_METHOD(ViewWithNoPalette_AndViewOfAnotherFormat_CannotChange)
        {
            for (const VersionCase &test : Versions())
            {
                const std::vector<uint8_t> noPalette = MakeViewBytes(test.version, { 64, 65, 0, 0, 70, 64 }, false);
                AssertFails(ReplaceViewPaletteColors(test.version, noPalette, 80, 90, NewColors()), sci::ErrorCode::Unsupported, "the view has no palette");
                AssertFails(ClearViewPaletteMarks(test.version, noPalette, 80, 90), sci::ErrorCode::Unsupported, "the view has no palette");

                // Data that is not a view.
                const std::vector<uint8_t> damaged(100, 0xff);
                sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, damaged, 80, 90);
                Assert::IsFalse(edit.has_value(), L"data that is not a view");
                Assert::IsTrue(edit.error().code == sci::ErrorCode::Format, Wide(edit.error().ToString()).c_str());

                const std::vector<uint8_t> view = MakeViewBytes(test.version);
                AssertFails(ReplaceViewPaletteColors(test.version, view, 90, 80, NewColors()), sci::ErrorCode::Usage, "90-80 is not a range of palette entries");
                AssertFails(ClearViewPaletteMarks(test.version, view, 80, 256), sci::ErrorCode::Usage, "80-256 is not a range of palette entries");
            }
            // A view of SCI0.
            std::unique_ptr<ResourceEntity> ega(CreateDefaultViewResource(sciVersion0));
            sci::ostream stream;
            std::map<BlobKey, uint32_t> propertyBag;
            ega->WriteTo(stream, true, 5, propertyBag);
            const std::vector<uint8_t> egaBytes(stream.GetInternalPointer(), stream.GetInternalPointer() + stream.GetDataSize());
            AssertFails(ReplaceViewPaletteColors(sciVersion0, egaBytes, 80, 90, NewColors()), sci::ErrorCode::Unsupported, "only a view of SCI1.1 or SCI2 can");
            AssertFails(ClearViewPaletteMarks(sciVersion0, egaBytes, 80, 90), sci::ErrorCode::Unsupported, "only a view of SCI1.1 or SCI2 can");
        }

        // The reader of the views gives a loop with no cel a new cel: two
        // reads of one view must give the same cel. The memory of the cel
        // has other bytes before its constructor runs.
        TEST_METHOD(Cel_DefaultConstructor_GivesTheTransparentColourTheValue0)
        {
            alignas(Cel) unsigned char memory[sizeof(Cel)];
            memset(memory, 0xcc, sizeof(memory));

            Cel *cel = new (memory) Cel();

            const int transparentColor = cel->TransparentColor;
            const int width = cel->size.cx;
            const int height = cel->size.cy;
            const bool stride32 = cel->Stride32;
            cel->~Cel();
            Assert::AreEqual(0, transparentColor);
            Assert::AreEqual(0, width);
            Assert::AreEqual(0, height);
            Assert::IsTrue(stride32);
        }

        // The check that a view reads back with the cels that it had.
        TEST_METHOD(SameViewCels_ComparesThePixelsAndNotThePadding)
        {
            // A view with 1 cel of 3 x 2 pixels: a row of 3 pixels has 4
            // bytes, and the last of them has the padding value.
            auto makeView = [](uint8_t padding, uint8_t lastPixel, uint8_t transparentColor) -> std::unique_ptr<ResourceEntity>
            {
                std::unique_ptr<ResourceEntity> view(CreateViewResource(sciVersion1_1));
                Cel cel;
                cel.size = size16(3, 2);
                cel.TransparentColor = transparentColor;
                cel.Data.allocate(8);
                const uint8_t bytes[8] = { 64, 65, 0, padding, 0, 70, lastPixel, padding };
                cel.Data.assign(bytes, bytes + 8);
                Loop loop;
                loop.Cels.push_back(cel);
                RasterComponent &raster = view->GetComponent<RasterComponent>();
                raster.Loops.clear();
                raster.Loops.push_back(loop);
                return view;
            };
            const std::unique_ptr<ResourceEntity> view = makeView(0xaa, 64, 0);
            const RasterComponent &raster = view->GetComponent<RasterComponent>();
            Assert::AreEqual((size_t)4, StrideOf(raster.Loops[0].Cels[0]), L"setup: a row has a byte of padding");

            Assert::IsTrue(SameViewCels(raster, makeView(0xaa, 64, 0)->GetComponent<RasterComponent>()), L"the same view");
            Assert::IsTrue(SameViewCels(raster, makeView(0xbb, 64, 0)->GetComponent<RasterComponent>()), L"another value in the padding of the rows");
            Assert::IsFalse(SameViewCels(raster, makeView(0xaa, 66, 0)->GetComponent<RasterComponent>()), L"another pixel");
            Assert::IsFalse(SameViewCels(raster, makeView(0xaa, 64, 9)->GetComponent<RasterComponent>()), L"another transparent colour");

            // Another count of cels.
            const std::unique_ptr<ResourceEntity> twoCels = makeView(0xaa, 64, 0);
            RasterComponent &twoCelsRaster = twoCels->GetComponent<RasterComponent>();
            twoCelsRaster.Loops[0].Cels.push_back(twoCelsRaster.Loops[0].Cels[0]);
            Assert::IsFalse(SameViewCels(raster, twoCelsRaster), L"another count of cels");
        }

        TEST_METHOD(ClearMarks_PaletteThatWouldStoreNoEntry)
        {
            for (const VersionCase &test : Versions())
            {
                // No pixel has an entry: each pixel has the transparent colour.
                const std::vector<uint8_t> view = MakeThreeByteViewBytes(test.version, 0, 236, 1, { 0, 0, 0, 0, 0, 0 });
                const size_t palette = PaletteOffsetOf(view);
                Assert::AreEqual(0x0e, (int)view[palette], L"setup: the first byte of the palette");

                sci::Result<ViewPaletteEdit> edit = ClearViewPaletteMarks(test.version, view, 0, 235);

                AssertOk(edit, test.name);
                Assert::AreEqual(0, edit->after.entryCount);
                Assert::AreEqual(std::string(""), UsedRunsOf(edit->data));
                // The count (236, then 0: 1 byte) and the size word (730,
                // then 22: 2 bytes).
                Assert::AreEqual(22, (int)SizeWordOf(edit->data, palette));
                Assert::AreEqual((size_t)3, DifferentBytes(view, edit->data), Wide(test.name).c_str());

                // With a first byte of 0, a palette with a count of 0 reads
                // as a palette with no header.
                std::vector<uint8_t> firstByte0 = view;
                firstByte0[palette] = 0;
                Assert::AreEqual(std::string("0-235"), UsedRunsOf(firstByte0), L"setup: the palette still reads as one with a header");
                AssertFails(ClearViewPaletteMarks(test.version, firstByte0, 0, 235), sci::ErrorCode::Unsupported, "a reader takes such bytes as a palette with no header");
            }
        }
    };

    // scic view palette (plan section 4.9), run in this process through
    // RunCli, on a copy of the SCI1.1 template game with views that the
    // tests put there as patch files.
    TEST_CLASS(TestCliView)
    {
        NoAppState _noAppState;
        GameCopy _game;
        std::string _gameFolder;
        std::string _outDir;
        std::string _patches;

        // Run, and assert the exit code; the message is the output.
        cli::StringConsole Expect(int exitCode, const std::vector<std::string> &args)
        {
            cli::StringConsole console;
            int code = cli::RunCli(args, console);
            Assert::AreEqual(exitCode, code, Wide(console.out + console.err).c_str());
            return console;
        }

        // Writes a patch file for the view into the game copy: the header
        // of the patch file of a view, then the data.
        void PutViewIntoGame(int number, const std::vector<uint8_t> &data)
        {
            std::vector<uint8_t> bytes = ViewPatchHeader();
            bytes.insert(bytes.end(), data.begin(), data.end());
            WriteFileBytes(_gameFolder + "\\" + std::to_string(number) + ".v56", bytes);
        }

        // The data of a patch file of the output folder: the bytes after
        // its header.
        std::vector<uint8_t> PatchDataOf(int number)
        {
            const std::vector<uint8_t> bytes = ReadFileBytes(_patches + "\\" + std::to_string(number) + ".v56");
            Assert::IsTrue(bytes.size() > 26, L"the patch file has a header and data");
            return std::vector<uint8_t>(bytes.begin() + 26, bytes.end());
        }

        // Copies the patch files of the output folder into the game copy.
        void CopyPatchesToGame()
        {
            for (const auto &entry : fs::directory_iterator(_patches))
            {
                fs::copy_file(entry.path(), fs::path(_gameFolder) / entry.path().filename(), fs::copy_options::overwrite_existing);
            }
        }

        // The row of the view in the table of scic dev palette-use --colors.
        std::string PaletteUseRowOf(int number)
        {
            cli::StringConsole console = Expect(0, { "dev", "palette-use", _gameFolder, "--colors" });
            return LineWith(console.out, fmt::format("view\t{0}\t", number));
        }

        // Writes a BMP file of 1 pixel whose colour table has NewColor(e) for
        // each entry e (or OldColor(e), the colours of the test views), and
        // gives its path.
        std::string WriteColorsBmp(bool oldColors = false)
        {
            IndexedBmp image;
            image.width = 1;
            image.height = 1;
            image.pixels = { 0 };
            image.colors = NewColors();
            for (int entry = 0; oldColors && (entry < 256); entry++)
            {
                image.colors[entry] = OldColor(entry);
            }
            const std::string path = _outDir + (oldColors ? "\\old-colors.bmp" : "\\colors.bmp");
            AssertOk(WriteIndexedBmpFile(path, image), "setup: the BMP file");
            return path;
        }

        // Puts the Sierra-form view of ThreeLoopView (SCI1.1) into the game
        // as view 500, exports it into a new folder, and gives the folder.
        std::string ExportView500(const std::string &name = "exported")
        {
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.sci2 = false;
            PutViewIntoGame(500, sierraview::MakeSierraView(made));
            const std::string folder = _outDir + "\\" + name;
            fs::create_directories(folder);
            Expect(0, { "view", "export", _gameFolder, "--view", "500", "--out-dir", folder });
            return folder;
        }

        // Reads a BMP file, lets the change work on it, and writes it again.
        template<typename TChange>
        void ChangeBmp(const std::string &path, TChange change)
        {
            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);
            AssertOk(image, "setup: " + path);
            change(*image);
            AssertOk(WriteIndexedBmpFile(path, *image), "setup: " + path);
        }

        // The pixels of a cel of view 500 in the game copy with the patch
        // files of the output folder.
        std::vector<uint8_t> PixelsWithPatches(int loop, int cel)
        {
            CopyPatchesToGame();
            GameSession &session = _game.Open();
            std::unique_ptr<ResourceBlob> blob = session.ResourceMap().MostRecentResource(ResourceType::View, 500, false);
            Assert::IsNotNull(blob.get());
            const std::vector<uint8_t> data(blob->GetData(), blob->GetData() + blob->GetLength());
            _game.CloseSessions();
            sci::Result<std::vector<uint8_t>> pixels = GetViewCelPixels(sciVersion1_1, data, loop, cel);
            AssertOk(pixels);
            return *pixels;
        }

        // "64=506070": the cell of an entry with a colour.
        static std::string ColorCell(int entry, const IndexedBmpColor &color)
        {
            return fmt::format("{0}={1:02x}{2:02x}{3:02x}", entry, color.red, color.green, color.blue);
        }

    public:
        TEST_METHOD_INITIALIZE(SetUp)
        {
            _outDir = (fs::temp_directory_path() / ("scicompanion-view-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()))).string();
            _patches = _outDir + "\\patches";
            fs::create_directories(_patches);
            _gameFolder = _game.Make(TemplateSci11);
        }

        TEST_METHOD_CLEANUP(TearDown)
        {
            cli::CancelFlag().store(false);
            RemoveFolder(_outDir);
        }

        // The source is the copy of the view that the game uses: view 0 of
        // the template is in the package, and the test puts a patch file
        // with another view 0 into the game.
        TEST_METHOD(Palette_ColorsFrom_WritesThePatchFileOfTheViewWithTheNewColours)
        {
            const std::vector<uint8_t> view = MakeViewBytes(sciVersion1_1);
            PutViewIntoGame(0, view);
            const size_t entries = PaletteOffsetOf(view) + PaletteHeaderSize;
            const std::string bmp = WriteColorsBmp();
            auto before = FilesOf(_gameFolder);

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "0", "--colors-from", bmp, "--palette-range", "64-65", "--out-dir", _patches, "-v" });

            Assert::IsTrue(Has(console.err, "Wrote 1 of 1 view into " + _patches + "."), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "view 0: new colours for the entries 64-65"), Wide(console.err).c_str());
            Assert::IsFalse(Has(console.err, "warning"), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "0.v56" }) == FileNamesOf(_patches), L"the patch file");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"the command writes nothing into the game");
            // The bytes of the view, with other values only in the red, the
            // green and the blue of the entries 64 and 65.
            std::vector<uint8_t> expected = view;
            for (int entry = 64; entry <= 65; entry++)
            {
                const IndexedBmpColor color = NewColor(entry);
                expected[entries + ((size_t)entry * 4) + 1] = color.red;
                expected[entries + ((size_t)entry * 4) + 2] = color.green;
                expected[entries + ((size_t)entry * 4) + 3] = color.blue;
            }
            const std::vector<uint8_t> patch = PatchDataOf(0);
            Assert::IsTrue(expected == patch, L"the bytes of the view in the patch file");
            Assert::AreEqual((size_t)6, DifferentBytes(view, patch));

            // The game copy with the patch file: the marks and the pixels are
            // the same, and the two entries have the new colours.
            const std::string rowBefore = PaletteUseRowOf(0);
            CopyPatchesToGame();
            const std::string rowAfter = PaletteUseRowOf(0);
            const std::string start = "view\t0\tpatch\t1\t1\t60-99,200\t64-65,70\t";
            Assert::AreEqual(start, rowBefore.substr(0, start.size()), Wide(rowBefore).c_str());
            Assert::AreEqual(start, rowAfter.substr(0, start.size()), Wide(rowAfter).c_str());
            Assert::IsTrue(Has(rowBefore, " " + ColorCell(64, OldColor(64)) + " " + ColorCell(65, OldColor(65)) + " " + ColorCell(66, OldColor(66)) + " "), Wide(rowBefore).c_str());
            Assert::IsTrue(Has(rowAfter, " " + ColorCell(63, OldColor(63)) + " " + ColorCell(64, NewColor(64)) + " " + ColorCell(65, NewColor(65)) + " " + ColorCell(66, OldColor(66)) + " "), Wide(rowAfter).c_str());
        }

        // View 996 of the template is in the package, and its palette marks
        // no entry: the command reads the view of the package, and the view
        // has no change.
        TEST_METHOD(Palette_ViewOfThePackage_IsTheSource)
        {
            std::vector<uint8_t> packageView;
            {
                std::unique_ptr<ResourceBlob> blob = _game.Open().ResourceMap().MostRecentResource(ResourceType::View, 996, false);
                Assert::IsNotNull(blob.get(), L"setup: view 996 of the template");
                Assert::IsFalse(IsFlagSet(blob->GetSourceFlags(), ResourceSourceFlags::PatchFile), L"setup: the view is in the package");
                packageView.assign(blob->GetData(), blob->GetData() + blob->GetLength());
                _game.CloseSessions();
            }
            Assert::IsTrue(ReadViewPaletteOffsetVGA11(packageView.data(), packageView.size()) != 0, L"setup: the view has a palette");

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "996", "--clear-used", "--palette-range", "0-255", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: warning: view 996: its palette has no mark for an entry of the range 0-255: the view does not change, and gets no patch file"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 0 of 1 view into " + _patches + "; 1 view with no change."), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"a view with no change gets no patch file");
        }

        TEST_METHOD(Palette_PatchFile_HasTheHeaderOfAViewPatch)
        {
            const std::vector<uint8_t> view = MakeViewBytes(sciVersion1_1);
            PutViewIntoGame(5, view);

            Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches });

            // 80 80, then 24 bytes of 0, then the view: ScummVM takes the
            // data of a view patch of SCI1.1 or later at offset 26 plus the
            // value of byte 3.
            const std::vector<uint8_t> patch = ReadFileBytes(_patches + "\\5.v56");
            Assert::AreEqual(view.size() + 26, patch.size());
            Assert::IsTrue(ViewPatchHeader() == std::vector<uint8_t>(patch.begin(), patch.begin() + 26), L"the header of the patch file");
            // The view starts with the size of its header.
            Assert::IsTrue(std::equal(view.begin(), view.begin() + 8, patch.begin() + 26), L"the view comes after the header");
        }

        TEST_METHOD(Palette_ClearUsed_FourByteEntries_ClearsTheFlagBytes)
        {
            const std::vector<uint8_t> view = MakeViewBytes(sciVersion1_1);
            PutViewIntoGame(5, view);
            const size_t entries = PaletteOffsetOf(view) + PaletteHeaderSize;

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches, "-v" });

            Assert::IsTrue(Has(console.err, "view 5: no mark for the entries 80-99,200"), Wide(console.err).c_str());
            std::vector<uint8_t> expected = view;
            for (int entry = 80; entry <= 99; entry++)
            {
                expected[entries + ((size_t)entry * 4)] = 0;
            }
            expected[entries + ((size_t)200 * 4)] = 0;
            const std::vector<uint8_t> patch = PatchDataOf(5);
            Assert::IsTrue(expected == patch, L"the bytes of the view in the patch file");
            Assert::AreEqual((size_t)21, DifferentBytes(view, patch));

            CopyPatchesToGame();
            const std::string row = PaletteUseRowOf(5);
            const std::string start = "view\t5\tpatch\t1\t1\t60-79\t64-65,70\t" + ColorCell(60, OldColor(60)) + " ";
            Assert::AreEqual(start, row.substr(0, start.size()), Wide(row).c_str());
        }

        TEST_METHOD(Palette_ClearUsed_ThreeByteEntries_StoresFewerEntries)
        {
            const std::vector<uint8_t> view = MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 1);
            PutViewIntoGame(6, view);
            const size_t palette = PaletteOffsetOf(view);

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "6", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches, "-v" });

            Assert::IsTrue(Has(console.err, "view 6: its palette stores 0-79 (before: 0-235), so the entries 80-235 have no mark"), Wide(console.err).c_str());
            Assert::IsFalse(Has(console.err, "the size word"), Wide(console.err).c_str());
            // The count (236, then 80) and the size word (730, then 262).
            std::vector<uint8_t> expected = view;
            expected[palette + 29] = 80;
            SetSizeWord(expected, palette, 262);
            const std::vector<uint8_t> patch = PatchDataOf(6);
            Assert::IsTrue(expected == patch, L"the bytes of the view in the patch file");
            Assert::AreEqual((size_t)3, DifferentBytes(view, patch));

            const std::string rowBefore = PaletteUseRowOf(6);
            CopyPatchesToGame();
            const std::string rowAfter = PaletteUseRowOf(6);
            Assert::IsTrue(Has(rowBefore, "view\t6\tpatch\t1\t1\t0-235\t64-65,70\t"), Wide(rowBefore).c_str());
            const std::string start = "view\t6\tpatch\t1\t1\t0-79\t64-65,70\t" + ColorCell(0, OldColor(0)) + " ";
            Assert::AreEqual(start, rowAfter.substr(0, start.size()), Wide(rowAfter).c_str());
            Assert::IsTrue(Has(rowAfter, " " + ColorCell(79, OldColor(79)) + "\t"), Wide(rowAfter).c_str());
        }

        // A size word that does not have the value of the entries keeps its
        // value, and -v says so.
        TEST_METHOD(Palette_ClearUsed_ThreeByteEntries_SizeWordWithAnotherValue_KeepsItsValue)
        {
            std::vector<uint8_t> view = MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 1);
            const size_t palette = PaletteOffsetOf(view);
            SetSizeWord(view, palette, 1046);
            PutViewIntoGame(6, view);

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "6", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches, "-v" });

            Assert::IsTrue(Has(console.err, "view 6: the size word of its palette is not 22 + the count of its entries x 3, and keeps its value"), Wide(console.err).c_str());
            std::vector<uint8_t> expected = view;
            expected[palette + 29] = 80;
            const std::vector<uint8_t> patch = PatchDataOf(6);
            Assert::IsTrue(expected == patch, L"the bytes of the view in the patch file");
            Assert::AreEqual((size_t)1, DifferentBytes(view, patch));
        }

        // The flag 0 in the header of a palette with 3-byte entries marks no
        // entry: the view has no change, also with a range that ends before
        // the last stored entry and has pixels of the view.
        TEST_METHOD(Palette_ClearUsed_ThreeByteEntriesWithTheFlag0_HasNoChange)
        {
            PutViewIntoGame(6, MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 0));

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "6", "--clear-used", "--palette-range", "60-200", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: warning: view 6: its palette has no mark for an entry of the range 60-200: the view does not change, and gets no patch file"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 0 of 1 view into " + _patches + "; 1 view with no change."), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");
        }

        // Each view has its own exception boundary: a view that fails does
        // not stop the next one.
        TEST_METHOD(Palette_ClearUsed_RangeNotAtTheEndOfThreeByteEntries_FailsTheView)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            PutViewIntoGame(6, MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 1));
            PutViewIntoGame(7, MakeViewBytes(sciVersion1_1));

            cli::StringConsole console = Expect(6, { "view", "palette", _gameFolder, "--view", "5-7", "--clear-used", "--palette-range", "80-200", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view 6: the palette has one flag for all its entries (0-235)") && Has(console.err, "the range 80-200 does not reach the last stored entry (235) [unsupported]"),
                Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 2 of 3 views into " + _patches + "; 1 view failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "5.v56", "7.v56" }) == FileNamesOf(_patches), L"the other views are written");
        }

        TEST_METHOD(Palette_ClearUsed_PixelWithAnEntryOfTheRange_FailsTheView)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            PutViewIntoGame(6, MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 1));
            // No pixel of view 7 has an entry.
            PutViewIntoGame(7, MakeViewBytes(sciVersion1_1, { 0, 0, 0, 0, 0, 0 }));

            cli::StringConsole console = Expect(6, { "view", "palette", _gameFolder, "--view", "5", "6", "7", "--clear-used", "--palette-range", "65-235", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view 5: pixels of the view have the entries 65,70, which would lose their mark: the view would lose the colours of these pixels [unsupported]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "scic: error: view 6: pixels of the view have the entries 65,70, which would lose their mark: the view would lose the colours of these pixels [unsupported]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 1 of 3 views into " + _patches + "; 2 views failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "7.v56" }) == FileNamesOf(_patches), L"the view with no such pixel is written");
        }

        TEST_METHOD(Palette_ViewWithNoPalette_FailsTheView)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1, { 64, 65, 0, 0, 70, 64 }, false));
            PutViewIntoGame(6, MakeViewBytes(sciVersion1_1));
            const std::string bmp = WriteColorsBmp();

            cli::StringConsole console = Expect(6, { "view", "palette", _gameFolder, "--view", "5", "6", "--colors-from", bmp, "--palette-range", "80-235", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view 5: the view has no palette [unsupported]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 1 of 2 views into " + _patches + "; 1 view failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "6.v56" }) == FileNamesOf(_patches), L"the view with a palette is written");
        }

        // A view of a game version that is not SCI1.1 or SCI2.
        TEST_METHOD(Palette_ViewOfAnotherGameVersion_FailsTheView)
        {
            _gameFolder = _game.Make(TemplateSci0);

            cli::StringConsole console = Expect(6, { "view", "palette", _gameFolder, "--view", "0-65535", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view ") && Has(console.err, ": the palette of a view of this game version cannot change; only a view of SCI1.1 or SCI2 can [unsupported]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 0 of ") && Has(console.err, " failed."), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");
        }

        // A view whose second loop mirrors itself: the reader of the views
        // logs a warning and gives the loop a cel. The repair is the reason
        // that the view fails, also when the change of its palette fails.
        TEST_METHOD(Palette_ViewThatItsReaderRepaired_FailsTheViewWithTheRepairAsTheReason)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1, { 64, 65, 0, 0, 70, 64 }, true, SecondLoop::MirrorsItself));
            PutViewIntoGame(6, MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 1, { 64, 65, 0, 0, 70, 64 }, SecondLoop::MirrorsItself));
            const std::string bmp = WriteColorsBmp();
            const auto expectTheRepair = [&](const cli::StringConsole &console, int number)
            {
                const std::string line = LineWith(console.err, fmt::format("scic: error: view {0}: ", number));
                Assert::IsTrue(Has(line, fmt::format("view {0}: the reader repaired damaged data: Empty loop found", number)) && Has(line, "[format]"), Wide(console.err).c_str());
                Assert::IsFalse(Has(console.err, "bytes in common") || Has(console.err, "does not reach") || Has(console.err, "pixels of the view"), Wide(console.err).c_str());
                Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");
            };

            // New colours: the change itself does not fail.
            cli::StringConsole colors = Expect(6, { "view", "palette", _gameFolder, "--view", "5", "6", "--colors-from", bmp, "--palette-range", "80-235", "--out-dir", _patches });
            expectTheRepair(colors, 5);
            expectTheRepair(colors, 6);

            // The range does not reach the last stored entry of view 6, and
            // pixels of view 5 have entries of the range.
            cli::StringConsole range = Expect(6, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "60-200", "--out-dir", _patches });
            expectTheRepair(range, 5);
            expectTheRepair(range, 6);
        }

        // A loop with no cel is a form of Sierra's views, not damage: the
        // palette of such a view changes.
        TEST_METHOD(Palette_ViewWithALoopThatHasNoCel_Changes)
        {
            const std::vector<uint8_t> view = MakeViewBytes(sciVersion1_1, { 64, 65, 0, 0, 70, 64 }, true, SecondLoop::Empty);
            PutViewIntoGame(5, view);
            const std::string bmp = WriteColorsBmp();

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--colors-from", bmp, "--palette-range", "80-235", "--out-dir", _patches });

            Assert::AreEqual(view.size(), PatchDataOf(5).size(), Wide(console.err).c_str());
        }

        TEST_METHOD(Palette_ColorsFrom_BmpThatCannotGiveTheColours_StopsTheRun)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            const std::string bmp = _outDir + "\\image.bmp";
            auto run = [&](int exitCode, const std::string &range) -> cli::StringConsole
            {
                cli::StringConsole console = Expect(exitCode, { "view", "palette", _gameFolder, "--view", "5", "--colors-from", bmp, "--palette-range", range, "--out-dir", _patches });
                Assert::IsTrue(FileNamesOf(_patches).empty(), Wide("no patch file is written:\n" + console.err).c_str());
                Assert::IsFalse(Has(console.err, "Wrote "), Wide(console.err).c_str());
                return console;
            };

            // No file.
            cli::StringConsole noFile = run(3, "80-235");
            Assert::IsTrue(Has(noFile.err, "image.bmp") && Has(noFile.err, "[not-found]"), Wide(noFile.err).c_str());

            // A file that is not a BMP file.
            WriteFileText(bmp, "not an image");
            cli::StringConsole notBmp = run(3, "80-235");
            Assert::IsTrue(Has(notBmp.err, "not a BMP file") && Has(notBmp.err, "[format]"), Wide(notBmp.err).c_str());

            // A 24-bit BMP of 2 x 2 pixels.
            std::vector<uint8_t> trueColour = { 'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0 };
            trueColour.resize(70, 0);
            WriteFileBytes(bmp, trueColour);
            cli::StringConsole bits = run(3, "80-235");
            Assert::IsTrue(Has(bits.err, "a 24-bit BMP file") && Has(bits.err, "[unsupported]"), Wide(bits.err).c_str());

            // A colour table of 64 entries, and a range that goes to 70.
            std::vector<uint8_t> bytes = ReadFileBytes(WriteColorsBmp());
            Assert::AreEqual(1, (int)bytes[47], L"setup: biClrUsed is 256 (00 01 00 00)");
            bytes[46] = 64;
            bytes[47] = 0;
            WriteFileBytes(bmp, bytes);
            cli::StringConsole table = run(2, "60-70");
            Assert::IsTrue(Has(table.err, "the colour table has 64 entries, and the palette range is 60-70"), Wide(table.err).c_str());
            // The last entry of the table is 63: a range that ends at 64 has
            // an entry with no colour, and a range that ends at 63 has none.
            cli::StringConsole oneMore = run(2, "60-64");
            Assert::IsTrue(Has(oneMore.err, "the colour table has 64 entries, and the palette range is 60-64"), Wide(oneMore.err).c_str());
            cli::StringConsole inTable = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--colors-from", bmp, "--palette-range", "60-63", "--out-dir", _patches });
            Assert::IsTrue(std::vector<std::string>({ "5.v56" }) == FileNamesOf(_patches), Wide(inTable.err).c_str());
        }

        TEST_METHOD(Palette_ColorsFrom_EntryWithNoMark_IsAWarning)
        {
            const std::vector<uint8_t> view = MakeViewBytes(sciVersion1_1);
            PutViewIntoGame(5, view);
            const std::string bmp = WriteColorsBmp();

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--colors-from", bmp, "--palette-range", "95-105", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: warning: view 5: its palette does not mark the entries 100-105 as used, so the game takes their colours from another palette"), Wide(console.err).c_str());
            // The entries with no mark get their colours too.
            Assert::AreEqual((size_t)33, DifferentBytes(view, PatchDataOf(5)));
        }

        // A view with no change gets no patch file: the output folder has
        // the patch file of a view only when the run changed the view.
        TEST_METHOD(Palette_ViewWithNoChange_IsAWarningAndGetsNoPatchFile)
        {
            // The palette of view 5 stores the entries 60 to 99, and the
            // palette of view 6 has no mark after entry 99 but for 200.
            PutViewIntoGame(5, MakeThreeByteViewBytes(sciVersion1_1, 60, 40, 1));
            PutViewIntoGame(6, MakeViewBytes(sciVersion1_1));
            const std::string bmp = WriteColorsBmp();

            cli::StringConsole colors = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--colors-from", bmp, "--palette-range", "100-120", "--out-dir", _patches });

            Assert::IsTrue(Has(colors.err, "scic: warning: view 5: its palette stores 60-99, and no entry of the range 100-120: the view does not change, and gets no patch file"), Wide(colors.err).c_str());
            Assert::IsTrue(Has(colors.err, "Wrote 0 of 1 view into " + _patches + "; 1 view with no change."), Wide(colors.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");

            cli::StringConsole clear = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "100-199", "--out-dir", _patches });

            Assert::IsTrue(Has(clear.err, "scic: warning: view 5: its palette has no mark for an entry of the range 100-199: the view does not change, and gets no patch file"), Wide(clear.err).c_str());
            Assert::IsTrue(Has(clear.err, "scic: warning: view 6: its palette has no mark for an entry of the range 100-199: the view does not change, and gets no patch file"), Wide(clear.err).c_str());
            Assert::IsTrue(Has(clear.err, "Wrote 0 of 2 views into " + _patches + "; 2 views with no change."), Wide(clear.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");
        }

        // The entries of the range have the colours of the file already: no
        // byte of the view would change.
        TEST_METHOD(Palette_ColorsFrom_ColoursThatTheViewHas_IsAWarningAndGetsNoPatchFile)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            const std::string bmp = WriteColorsBmp(true);

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--colors-from", bmp, "--palette-range", "80-90", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: warning: view 5: the entries 80-90 of its palette have the colours of the file: the view does not change, and gets no patch file"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 0 of 1 view into " + _patches + "; 1 view with no change."), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");
        }

        // The patch file that an earlier run left for a view goes when the
        // view has no change in this run.
        TEST_METHOD(Palette_OldPatchFileOfAViewWithNoChange_IsRemoved)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            PutViewIntoGame(7, MakeViewBytes(sciVersion1_1));
            Expect(0, { "view", "palette", _gameFolder, "--view", "5", "7", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches });
            Assert::IsTrue(std::vector<std::string>({ "5.v56", "7.v56" }) == FileNamesOf(_patches), L"setup: the patch files of the first run");
            // A file of the folder that is not the patch file of a named view
            // stays.
            WriteFileText(_patches + "\\notes.txt", "notes");

            // The palette of view 5 has no mark in the range of this run.
            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "100-199", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "Removed " + _patches + "\\5.v56 of an earlier run: view 5 has no change"), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "7.v56", "notes.txt" }) == FileNamesOf(_patches), L"the patch file of view 5 is not there; the file of view 7, which the run did not name, stays");
        }

        // The patch file that an earlier run left for a view goes when the
        // view fails in this run.
        TEST_METHOD(Palette_OldPatchFileOfAViewThatFails_IsRemoved)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            PutViewIntoGame(6, MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 1));
            Expect(0, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches });
            Assert::IsTrue(std::vector<std::string>({ "5.v56", "6.v56" }) == FileNamesOf(_patches), L"setup: the patch files of the first run");
            const std::vector<uint8_t> firstPatchOf5 = ReadFileBytes(_patches + "\\5.v56");

            // The range of this run does not reach the last stored entry of
            // view 6. View 5 loses the mark of other entries than in the
            // first run.
            cli::StringConsole console = Expect(6, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "90-200", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view 6: ") && Has(console.err, "does not reach the last stored entry"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Removed " + _patches + "\\6.v56 of an earlier run: view 6 failed"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote 1 of 2 views into " + _patches + "; 1 view failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "5.v56" }) == FileNamesOf(_patches), L"the patch file of view 6 is not there");
            Assert::IsFalse(firstPatchOf5 == ReadFileBytes(_patches + "\\5.v56"), L"the patch file of view 5 is the file of this run");
        }

        TEST_METHOD(Palette_DryRun_WritesAndRemovesNothing)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            PutViewIntoGame(6, MakeThreeByteViewBytes(sciVersion1_1, 0, 236, 1));
            auto before = FilesOf(_gameFolder);

            cli::StringConsole console = Expect(0, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches, "--dry-run" });

            Assert::IsTrue(FileNamesOf(_patches).empty(), L"the output folder stays empty");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
            Assert::IsTrue(Has(console.err, "Would write " + _patches + "\\5.v56") && Has(console.err, "Would write " + _patches + "\\6.v56"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "A run would write 2 of 2 views into " + _patches + "."), Wide(console.err).c_str());

            // A dry run does the checks of a run: a view that fails one is
            // exit code 6.
            cli::StringConsole failed = Expect(6, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "80-200", "--out-dir", _patches, "--dry-run" });
            Assert::IsTrue(Has(failed.err, "A run would write 1 of 2 views into " + _patches + "; 1 view failed."), Wide(failed.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"the output folder stays empty");

            // The patch files of an earlier run stay in a dry run: the run
            // says what it would remove. View 5 has no change with the range
            // 100-199, and view 6 fails.
            Expect(0, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches });
            auto patches = FilesOf(_patches);
            Assert::AreEqual((size_t)2, patches.size(), L"setup: the patch files of the first run");
            cli::StringConsole stale = Expect(6, { "view", "palette", _gameFolder, "--view", "5", "6", "--clear-used", "--palette-range", "100-199", "--out-dir", _patches, "--dry-run" });
            Assert::IsTrue(Has(stale.err, "Would remove " + _patches + "\\5.v56 of an earlier run: view 5 has no change"), Wide(stale.err).c_str());
            Assert::IsTrue(Has(stale.err, "Would remove " + _patches + "\\6.v56 of an earlier run: view 6 failed"), Wide(stale.err).c_str());
            Assert::IsFalse(Has(stale.err, "Removed "), Wide(stale.err).c_str());
            Assert::IsTrue(patches == FilesOf(_patches), L"the patch files of the first run stay");
        }

        TEST_METHOD(Palette_UsageErrors)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            const std::string bmp = WriteColorsBmp();
            auto before = FilesOf(_gameFolder);
            auto usage = [&](const std::vector<std::string> &options, const std::string &text)
            {
                std::vector<std::string> args = { "view", "palette", _gameFolder };
                args.insert(args.end(), options.begin(), options.end());
                cli::StringConsole console = Expect(2, args);
                Assert::IsTrue(Has(console.err, text), Wide(text + ":\n" + console.err).c_str());
            };

            usage({ "--view", "5", "--palette-range", "80-235", "--out-dir", _patches }, "give --colors-from with a BMP file, or --clear-used");
            usage({ "--view", "5", "--colors-from", bmp, "--clear-used", "--palette-range", "80-235", "--out-dir", _patches }, "give --colors-from or --clear-used, not both");
            usage({ "--view", "5", "--colors-from", "", "--palette-range", "80-235", "--out-dir", _patches }, "--colors-from needs a file");
            usage({ "--view", "5", "--clear-used", "--out-dir", _patches }, "--palette-range");
            usage({ "--clear-used", "--palette-range", "80-235", "--out-dir", _patches }, "--view");
            usage({ "--view", "5", "--clear-used", "--palette-range", "80-235" }, "--out-dir");
            usage({ "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", "" }, "--out-dir needs a folder");
            for (const char *range : { "64", "70-64", "64-256", "a-b", "64-", "64-70-80" })
            {
                usage({ "--view", "5", "--clear-used", "--palette-range", range, "--out-dir", _patches }, "--palette-range is first-last");
            }
            // The game has no file in its folder but its own.
            usage({ "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _gameFolder }, "the output folder is the game folder");
            usage({ "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _gameFolder + "\\src" }, "the output folder is inside the game folder");
            usage({ "--view", "4", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches }, "the game has no view 4");
            usage({ "--view", "1a", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches }, "\"1a\" is not a view number or a range of view numbers");
            usage({ "--view", "10-12", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches }, "the game has no view in the range 10-12");

            // Plan section 8: a folder that is missing is exit code 3.
            cli::StringConsole noOutDir = Expect(3, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _outDir + "\\no-such-folder" });
            Assert::IsTrue(Has(noOutDir.err, "the output folder does not exist"), Wide(noOutDir.err).c_str());
            Expect(3, { "view", "palette", _gameFolder + "\\NoSuchGame", "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches });

            Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
        }

        // Plan section 8: a patch file that cannot be written is exit code 9,
        // also in a dry run.
        TEST_METHOD(Palette_PatchFileThatCannotBeWritten_ExitsWith9)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            WriteReadOnlyFile(_patches + "\\5.v56", "read-only");

            cli::StringConsole dryRun = Expect(9, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches, "--dry-run" });
            Assert::IsTrue(Has(dryRun.err, "scic: error: view 5: ") && Has(dryRun.err, "5.v56"), Wide(dryRun.err).c_str());
            cli::StringConsole console = Expect(9, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches });
            Assert::IsTrue(Has(console.err, "scic: error: view 5: ") && Has(console.err, "5.v56"), Wide(console.err).c_str());
            Assert::AreEqual(std::string("read-only"), ReadFileText(_patches + "\\5.v56"));
        }

        // A patch file of an earlier run that cannot be removed is exit code
        // 9 too, for a view with no change and for a view that fails, also
        // in a dry run.
        TEST_METHOD(Palette_OldPatchFileThatCannotBeRemoved_ExitsWith9)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            WriteReadOnlyFile(_patches + "\\5.v56", "read-only");
            for (const char *dryRun : { "--dry-run", "-q" })
            {
                // No mark in the range 100-199: the view has no change.
                cli::StringConsole noChange = Expect(9, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "100-199", "--out-dir", _patches, dryRun });
                Assert::IsTrue(Has(noChange.err, "scic: error: view 5: ") && Has(noChange.err, "5.v56"), Wide(noChange.err).c_str());
                // Pixels of the view have entries of the range 60-70: the
                // view fails, and the error of the view is there too.
                cli::StringConsole failed = Expect(9, { "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "60-70", "--out-dir", _patches, dryRun });
                Assert::IsTrue(Has(failed.err, "scic: error: view 5: pixels of the view have the entries 64-65,70"), Wide(failed.err).c_str());
                Assert::IsTrue(Has(failed.err, "5.v56"), Wide(failed.err).c_str());
                Assert::AreEqual(std::string("read-only"), ReadFileText(_patches + "\\5.v56"));
            }
        }

        TEST_METHOD(Palette_CtrlC_ExitsWith7)
        {
            PutViewIntoGame(5, MakeViewBytes(sciVersion1_1));
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "view", "palette", _gameFolder, "--view", "5", "--clear-used", "--palette-range", "80-235", "--out-dir", _patches }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(Has(console.err, "stopped by Ctrl+C"), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty(), L"no patch file is written");
        }

        TEST_METHOD(List_Text_HasARowForEachView)
        {
            cli::StringConsole console = Expect(0, { "view", "list", _gameFolder });

            std::vector<std::string> lines = LinesOf(console.out);
            Assert::AreEqual((size_t)17, lines.size(), Wide(console.out).c_str());
            Assert::AreEqual(std::string("View  Format  Loops  Mirrors  Cels  Raw  Res  Source   Bytes  CRC32     Palette  Skip px  Palette entries  Pixel entries"), lines[0]);
            Assert::IsTrue(lines[1].rfind("   0  vga11       9        3    40    0    -  package  22707  ", 0) == 0, Wide(console.out).c_str());
            Assert::IsTrue(Has(lines[1], "  -          15457  -                0-4,8,11-15,48-51"), Wide(console.out).c_str());
            // View 996 has a palette that stores no entry.
            const std::string row996 = LineWith(console.out, " 996");
            Assert::IsTrue(Has(row996, "  none           9  -                -"), Wide(console.out).c_str());
            for (const std::string &line : lines)
            {
                Assert::IsFalse(line.empty() || (line.back() == ' '), Wide("no spaces at the end of a line: \"" + line + "\"").c_str());
            }
            Assert::IsTrue(console.err.empty(), Wide(console.err).c_str());
        }

        // A view of the form of Sierra's views, as a patch file: 3 loops
        // (one a mirror, one with a raw cel) and a palette of 4-byte entries.
        TEST_METHOD(List_Tsv_HasTheValuesOfTheView)
        {
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.sci2 = false;
            const std::vector<uint8_t> data = sierraview::MakeSierraView(made);
            PutViewIntoGame(500, data);

            cli::StringConsole console = Expect(0, { "view", "list", _gameFolder, "--format", "tsv" });

            std::vector<std::string> lines = LinesOf(console.out);
            Assert::AreEqual((size_t)18, lines.size(), Wide(console.out).c_str());
            Assert::AreEqual(std::string("number\tformat\tloops\tmirror_loops\tcels\traw_cels\tresolution\tsource\tbytes\tcrc32\tpalette_first\tpalette_count\tentry_size\t"
                "skip_pixels\tpalette_entries\tpixel_entries\terror"), lines[0]);
            const size_t columns = TabFieldsOf(lines[0]).size();
            Assert::AreEqual((size_t)17, columns);
            for (const std::string &line : lines)
            {
                Assert::AreEqual(columns, TabFieldsOf(line).size(), Wide(line).c_str());
            }
            const std::vector<std::string> expected = { "500", "vga11", "3", "1", "3", "1", "", "patch", std::to_string(data.size()),
                fmt::format("{0:08x}", (uint32_t)crcFast(data.data(), (int)data.size())), "0", "256", "4", "3", "10-99", "10-16,20-24,40-45", "" };
            Assert::IsTrue(expected == TabFieldsOf(LineWith(console.out, "500\t")), Wide(console.out).c_str());
            // View 996 of the package has a palette that stores no entry.
            const std::vector<std::string> row996 = TabFieldsOf(LineWith(console.out, "996\t"));
            Assert::AreEqual(std::string("package"), row996[7]);
            Assert::AreEqual(std::string("0"), row996[11]);
            Assert::AreEqual(std::string(), row996[14]);
        }

        // The resolution byte is a cell of an SCI2 view only.
        TEST_METHOD(List_Sci2Game_HasTheResolutionByte)
        {
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.resolution = 2;
            const std::vector<uint8_t> data = sierraview::MakeSierraView(made);

            cli::ViewListRow row = cli::MakeViewListRow(sciVersion2, 7, data, false);

            Assert::IsTrue(row.described && row.error.empty(), Wide(row.error).c_str());
            const std::vector<std::string> fields = TabFieldsOf(LinesOf(cli::ViewListTsvTable({ row }))[1]);
            Assert::AreEqual(std::string("vga2"), fields[1]);
            Assert::AreEqual(std::string("2"), fields[6]);
            Assert::AreEqual(std::string("package"), fields[7]);
            const std::vector<std::string> text = LinesOf(cli::ViewListTextTable({ row }));
            Assert::IsTrue(text[1].rfind("   7  vga2        3        1     3    1    2  package", 0) == 0, Wide(text[1]).c_str());
        }

        TEST_METHOD(List_Selectors)
        {
            cli::StringConsole two = Expect(0, { "view", "list", _gameFolder, "--format", "tsv", "--view", "996", "0" });
            std::vector<std::string> lines = LinesOf(two.out);
            Assert::AreEqual((size_t)3, lines.size(), Wide(two.out).c_str());
            Assert::AreEqual(std::string("0"), TabFieldsOf(lines[1])[0]);
            Assert::AreEqual(std::string("996"), TabFieldsOf(lines[2])[0]);

            cli::StringConsole range = Expect(0, { "view", "list", _gameFolder, "--format", "tsv", "--view", "980-989" });
            Assert::AreEqual((size_t)6, LinesOf(range.out).size(), Wide(range.out).c_str());

            cli::StringConsole unknown = Expect(2, { "view", "list", _gameFolder, "--view", "5" });
            Assert::IsTrue(Has(unknown.err, "the game has no view 5") && unknown.out.empty(), Wide(unknown.out + unknown.err).c_str());
            cli::StringConsole bad = Expect(2, { "view", "list", _gameFolder, "--view", "abc" });
            Assert::IsTrue(Has(bad.err, "\"abc\" is not a view number or a range of view numbers"), Wide(bad.err).c_str());
        }

        // A view that cannot be read, and a view whose reader repaired damaged
        // data (a loop that mirrors itself gets a cel), have the error in
        // their row: exit 6.
        TEST_METHOD(List_ViewsThatCannotBeRead_HaveTheErrorInTheirRow)
        {
            PutViewIntoGame(501, std::vector<uint8_t>(100, 0xff));
            PutViewIntoGame(502, MakeViewBytes(sciVersion1_1, { 64, 65, 0, 0, 70, 64 }, true, SecondLoop::MirrorsItself));

            cli::StringConsole console = Expect(6, { "view", "list", _gameFolder, "--format", "tsv" });

            const std::vector<std::string> unreadable = TabFieldsOf(LineWith(console.out, "501\t"));
            Assert::AreEqual(std::string("patch"), unreadable[7], Wide(console.out).c_str());
            Assert::AreEqual(std::string("100"), unreadable[8], Wide(console.out).c_str());
            Assert::AreEqual(std::string(), unreadable[2], Wide(console.out).c_str());
            // Its loop headers would have 255 bytes.
            Assert::IsTrue(Has(unreadable[16], "the view has the form of an SCI1 view (its loop headers have 255 bytes) [unsupported]"), Wide(console.out).c_str());
            const std::vector<std::string> repaired = TabFieldsOf(LineWith(console.out, "502\t"));
            Assert::AreEqual(std::string("patch"), repaired[7], Wide(console.out).c_str());
            Assert::AreEqual(std::string(), repaired[2], Wide(console.out).c_str());
            Assert::IsTrue(Has(repaired[16], "the reader repaired damaged data: Empty loop found") && Has(repaired[16], "[format]"), Wide(console.out).c_str());
            // The other views have no error.
            Assert::AreEqual(std::string(), TabFieldsOf(LineWith(console.out, "0\t"))[16]);

            cli::StringConsole text = Expect(6, { "view", "list", _gameFolder, "--view", "501" });
            Assert::IsTrue(Has(text.out, "(unreadable: "), Wide(text.out).c_str());
        }

        // A loop with no cel is a form of Sierra's views: no error, and the
        // loop counts no cel.
        TEST_METHOD(List_ViewWithALoopThatHasNoCel_HasNoError)
        {
            PutViewIntoGame(503, MakeViewBytes(sciVersion1_1, { 64, 65, 0, 0, 70, 64 }, true, SecondLoop::Empty));

            cli::StringConsole console = Expect(0, { "view", "list", _gameFolder, "--format", "tsv", "--view", "503" });

            const std::vector<std::string> row = TabFieldsOf(LineWith(console.out, "503\t"));
            Assert::AreEqual(std::string("2"), row[2], Wide(console.out).c_str());
            Assert::AreEqual(std::string("1"), row[4], Wide(console.out).c_str());
            Assert::AreEqual(std::string(), row[16], Wide(console.out).c_str());
        }

        // A view of the package whose data cannot be decompressed: the read
        // of its resource fails, and its row has the error.
        TEST_METHOD(List_ViewOfThePackageThatCannotBeRead_HasTheErrorInItsRow)
        {
            // The header of view 996 in the volume: the type byte, the number,
            // the two sizes and the compression method (an unknown one here).
            const std::string volume = _gameFolder + "\\resource.000";
            std::vector<uint8_t> bytes = ReadFileBytes(volume);
            const std::vector<uint8_t> header = { 0x80, 0xe4, 0x03 };
            auto at = std::search(bytes.begin(), bytes.end(), header.begin(), header.end());
            Assert::IsTrue(at != bytes.end(), L"setup: the header of view 996 in the volume");
            *(at + 7) = 0x63;
            *(at + 8) = 0x00;
            WriteFileBytes(volume, bytes);

            cli::StringConsole console = Expect(6, { "view", "list", _gameFolder, "--format", "tsv", "--view", "996", "0" });

            const std::vector<std::string> row = TabFieldsOf(LineWith(console.out, "996\t"));
            Assert::AreEqual(std::string(), row[7], Wide(console.out).c_str());
            Assert::IsTrue(Has(row[16], "view 996") && Has(row[16], "[format]"), Wide(console.out).c_str());
            Assert::AreEqual(std::string(), TabFieldsOf(LineWith(console.out, "0\t"))[16], Wide(console.out).c_str());
        }

        // A view of an EGA game has no image that scic reads: a row with its
        // format, and no cels.
        TEST_METHOD(List_EgaGame_HasRowsWithNoImage)
        {
            _gameFolder = _game.Make(TemplateSci0);

            cli::StringConsole console = Expect(0, { "view", "list", _gameFolder, "--format", "tsv" });

            std::vector<std::string> lines = LinesOf(console.out);
            Assert::IsTrue(lines.size() > 1, Wide(console.out).c_str());
            for (size_t i = 1; i < lines.size(); i++)
            {
                std::vector<std::string> fields = TabFieldsOf(lines[i]);
                Assert::AreEqual(std::string("ega"), fields[1], Wide(lines[i]).c_str());
                Assert::AreEqual(std::string(), fields[2], Wide(lines[i]).c_str());
                Assert::IsFalse(fields[7].empty() || fields[8].empty(), Wide(lines[i]).c_str());
                Assert::AreEqual(std::string(), fields[16], Wide(lines[i]).c_str());
            }
            cli::StringConsole text = Expect(0, { "view", "list", _gameFolder });
            Assert::IsTrue(Has(text.out, "  ega         -        -     -    -    -  package"), Wide(text.out).c_str());
        }

        TEST_METHOD(List_WritesNothing)
        {
            const auto before = FilesOf(_gameFolder);

            Expect(0, { "view", "list", _gameFolder });
            Expect(0, { "view", "list", _gameFolder, "--format", "tsv" });

            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
        }

        TEST_METHOD(List_CtrlC_ExitsWith7)
        {
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "view", "list", _gameFolder }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
        }

        // The files of a Sierra-form view: a BMP file for each stored cel
        // (none for the mirror), with the pixels of the cel and the colours of
        // the view over palette 999, and the .toml file.
        TEST_METHOD(Export_WritesTheBmpsAndTheSidecar)
        {
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.sci2 = false;
            const std::vector<uint8_t> data = sierraview::MakeSierraView(made);
            PutViewIntoGame(500, data);
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);

            cli::StringConsole console = Expect(0, { "view", "export", _gameFolder, "--view", "500", "--out-dir", folder });

            Assert::IsTrue(std::vector<std::string>{ "500.l0.c0.bmp", "500.l0.c1.bmp", "500.l2.c0.bmp", "500.toml" } == FileNamesOf(folder), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Exported 1 of 1 view (3 cels) into "), Wide(console.err).c_str());
            const std::vector<std::pair<std::string, const sierraview::MadeCel *>> cels = { { "500.l0.c0.bmp", &made.loops[0].cels[0] }, { "500.l0.c1.bmp", &made.loops[0].cels[1] }, { "500.l2.c0.bmp", &made.loops[2].cels[0] } };
            for (const auto &cel : cels)
            {
                sci::Result<IndexedBmp> image = ReadIndexedBmpFile(folder + "\\" + cel.first);
                AssertOk(image, cel.first);
                Assert::AreEqual(cel.second->width, image->width);
                Assert::IsTrue(cel.second->pixels == image->pixels, Wide(cel.first).c_str());
                // Entry 50: the view marks it, so it has the colour of the view.
                Assert::AreEqual((int)made.colors[50][0], (int)image->colors[50].red);
                Assert::AreEqual((int)made.colors[50][1], (int)image->colors[50].green);
            }
            // Entry 5: the view does not mark it, so it has the colour of
            // palette 999 of the game.
            GameSession &session = _game.Open();
            const PaletteComponent *palette999 = session.ResourceMap().GetPalette999();
            Assert::IsNotNull(palette999);
            sci::Result<IndexedBmp> first = ReadIndexedBmpFile(folder + "\\500.l0.c0.bmp");
            AssertOk(first);
            Assert::AreEqual((int)palette999->Colors[5].rgbGreen, (int)first->colors[5].green);

            sci::Result<ViewSidecar> sidecar = ReadViewSidecarFile(folder + "\\500.toml");
            AssertOk(sidecar);
            Assert::AreEqual(500, sidecar->view);
            Assert::AreEqual(std::string("vga11"), sidecar->format);
            Assert::AreEqual(std::string("patch"), sidecar->from);
            Assert::AreEqual((uint32_t)data.size(), sidecar->size);
            Assert::AreEqual((uint32_t)crcFast(data.data(), (int)data.size()), sidecar->crc32);
            Assert::AreEqual((size_t)3, sidecar->loops.size());
            Assert::AreEqual(0, sidecar->loops[1].mirrorOf);
            Assert::AreEqual((size_t)3, sidecar->cels.size());
            Assert::AreEqual(std::string("500.l2.c0.bmp"), sidecar->cels[2].file);
            Assert::IsTrue(sidecar->cels[2].raw);
            Assert::AreEqual(-2, sidecar->cels[0].x);
            // The used entries: those of the view (10 to 99) and of palette 999.
            std::array<bool, 256> used = UsedPaletteEntries(*palette999);
            std::fill(used.begin() + 10, used.begin() + 100, true);
            Assert::AreEqual(IndexRunsText(IndexRunsOf(used)), IndexRunsText(sidecar->used));
            Assert::AreEqual(IndexRunsText(IndexRunsOf(UsedPaletteEntries(*palette999))), IndexRunsText(sidecar->palette999));
        }

        // A view whose only loop has no cel: --all skips it and counts it; a
        // named view with no cel fails.
        TEST_METHOD(Export_ViewWithNoCel)
        {
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.sci2 = false;
            made.loops = { sierraview::MadeLoop() };
            PutViewIntoGame(501, sierraview::MakeSierraView(made));
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);

            cli::StringConsole all = Expect(0, { "view", "export", _gameFolder, "--all", "--out-dir", folder });
            Assert::IsTrue(Has(all.err, "Exported 16 of 17 views (") && Has(all.err, "; 1 view has no cel."), Wide(all.err).c_str());
            Assert::IsFalse(fs::exists(folder + "\\501.toml"));

            cli::StringConsole named = Expect(6, { "view", "export", _gameFolder, "--view", "501", "--out-dir", folder });
            Assert::IsTrue(Has(named.err, "scic: error: view 501: the view has no cel to export [unsupported]"), Wide(named.err).c_str());
        }

        TEST_METHOD(Export_DryRun_WritesNothing)
        {
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);

            cli::StringConsole console = Expect(0, { "view", "export", _gameFolder, "--view", "900", "--out-dir", folder, "--dry-run" });

            Assert::IsTrue(FileNamesOf(folder).empty(), L"a dry run writes nothing");
            Assert::IsTrue(Has(console.err, "Would write " + folder + "\\900.l0.c0.bmp") && Has(console.err, "Would write " + folder + "\\900.toml"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "A run would export 1 of 1 view (3 cels) into "), Wide(console.err).c_str());
        }

        TEST_METHOD(Export_UsageErrors)
        {
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);
            cli::StringConsole neither = Expect(2, { "view", "export", _gameFolder, "--out-dir", folder });
            Assert::IsTrue(Has(neither.err, "give --all, or --view with one or more views"), Wide(neither.err).c_str());
            cli::StringConsole both = Expect(2, { "view", "export", _gameFolder, "--all", "--view", "0", "--out-dir", folder });
            Assert::IsTrue(Has(both.err, "give --all or --view, not both"), Wide(both.err).c_str());
            cli::StringConsole emptyOut = Expect(2, { "view", "export", _gameFolder, "--all", "--out-dir", "" });
            Assert::IsTrue(Has(emptyOut.err, "--out-dir needs a folder"), Wide(emptyOut.err).c_str());
            cli::StringConsole noOut = Expect(2, { "view", "export", _gameFolder, "--all" });
            Assert::IsTrue(Has(noOut.err, "--out-dir"), Wide(noOut.err).c_str());
            cli::StringConsole game = Expect(2, { "view", "export", _gameFolder, "--all", "--out-dir", _gameFolder });
            Assert::IsTrue(Has(game.err, "the output folder is the game folder"), Wide(game.err).c_str());
            cli::StringConsole missing = Expect(3, { "view", "export", _gameFolder, "--all", "--out-dir", _outDir + "\\missing" });
            Assert::IsTrue(Has(missing.err, "the output folder does not exist"), Wide(missing.err).c_str());
            cli::StringConsole unknown = Expect(2, { "view", "export", _gameFolder, "--view", "5", "--out-dir", folder });
            Assert::IsTrue(Has(unknown.err, "the game has no view 5"), Wide(unknown.err).c_str());
            Assert::IsTrue(FileNamesOf(folder).empty(), L"no file is written");
        }

        // A view that fails does not stop the next one: a view that its reader
        // repaired, and the views of an EGA game.
        TEST_METHOD(Export_ViewsThatFail_ExitWith6)
        {
            PutViewIntoGame(502, MakeViewBytes(sciVersion1_1, { 64, 65, 0, 0, 70, 64 }, true, SecondLoop::MirrorsItself));
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);

            cli::StringConsole repaired = Expect(6, { "view", "export", _gameFolder, "--view", "502", "900", "--out-dir", folder });
            Assert::IsTrue(Has(repaired.err, "scic: error: view 502: ") && Has(repaired.err, "the reader repaired damaged data: Empty loop found") && Has(repaired.err, "[format]"), Wide(repaired.err).c_str());
            Assert::IsTrue(Has(repaired.err, "Exported 1 of 2 views (3 cels) into ") && Has(repaired.err, "; 1 view failed."), Wide(repaired.err).c_str());
            Assert::IsTrue(fs::exists(folder + "\\900.toml") && !fs::exists(folder + "\\502.toml"));

            _gameFolder = _game.Make(TemplateSci0);
            cli::StringConsole ega = Expect(6, { "view", "export", _gameFolder, "--all", "--out-dir", folder });
            Assert::IsTrue(Has(ega.err, "the image of a view of the format ega cannot be read") && Has(ega.err, "[unsupported]"), Wide(ega.err).c_str());
        }

        // A palette 999 that its reader repairs (it stores entries past 255)
        // gives a warning; no view fails for it.
        TEST_METHOD(Export_DamagedPalette999_IsAWarning)
        {
            std::vector<uint8_t> palette;
            {
                GameSession &session = _game.Open();
                std::unique_ptr<ResourceBlob> blob = session.ResourceMap().MostRecentResource(ResourceType::Palette, 999, false);
                Assert::IsNotNull(blob.get(), L"setup: palette 999 of the template");
                palette.assign(blob->GetData(), blob->GetData() + blob->GetLength());
                _game.CloseSessions();
            }
            palette[25] = 200;
            palette[29] = 100;
            palette[30] = 0;
            std::vector<uint8_t> patch = { 0x8b, 0x00 };
            patch.insert(patch.end(), palette.begin(), palette.end());
            WriteFileBytes(_gameFolder + "\\999.pal", patch);
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);

            cli::StringConsole console = Expect(0, { "view", "export", _gameFolder, "--view", "0", "900", "--out-dir", folder, "--dry-run" });

            Assert::AreEqual((size_t)1, CountOf(console.err, "scic: warning: "), Wide(console.err).c_str());
            Assert::IsFalse(Has(console.err, "scic: error: "), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "A run would export 2 of 2 views"), Wide(console.err).c_str());
        }

        // A file that cannot be replaced fails the view before its first
        // write: exit 9.
        TEST_METHOD(Export_FileThatCannotBeWritten_ExitsWith9)
        {
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);
            WriteReadOnlyFile(folder + "\\900.toml", "read-only");

            cli::StringConsole console = Expect(9, { "view", "export", _gameFolder, "--view", "900", "--out-dir", folder });

            Assert::IsTrue(Has(console.err, "scic: error: view 900: ") && Has(console.err, "900.toml"), Wide(console.err).c_str());
            Assert::IsFalse(fs::exists(folder + "\\900.l0.c0.bmp"), L"no file of the view is written");
        }

        TEST_METHOD(Export_CtrlC_ExitsWith7)
        {
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "view", "export", _gameFolder, "--all", "--out-dir", folder }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(Has(console.err, "stopped by Ctrl+C"), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(folder).empty(), L"no file is written");
        }

        // The images of export with no change: no byte would change, so the
        // view gets no patch file.
        TEST_METHOD(Import_InDir_NoChange_IsAWarningAndGetsNoPatchFile)
        {
            const std::string folder = ExportView500();

            cli::StringConsole console = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: warning: view 500: the images have the pixels of the view: the view does not change, and gets no patch file"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Imported 0 of 1 view (0 cels) into ") && Has(console.err, "; 1 view with no change."), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        // Changed pixels in two cels: the patch file has them, and the other
        // cel keeps its pixels.
        TEST_METHOD(Import_InDir_ChangedPixels_AreInThePatchFile)
        {
            const std::string folder = ExportView500();
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &image) { image.pixels[0] = 50; image.pixels[14] = 51; });
            ChangeBmp(folder + "\\500.l2.c0.bmp", [](IndexedBmp &image) { image.pixels[5] = 60; });
            const std::vector<uint8_t> first = ReadIndexedBmpFile(folder + "\\500.l0.c0.bmp")->pixels;
            const std::vector<uint8_t> raw = ReadIndexedBmpFile(folder + "\\500.l2.c0.bmp")->pixels;

            cli::StringConsole console = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "Imported 1 of 1 view (3 cels) into "), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>{ "500.v56" } == FileNamesOf(_patches));
            const std::vector<uint8_t> patch = ReadFileBytes(_patches + "\\500.v56");
            const std::vector<uint8_t> header = ViewPatchHeader();
            Assert::IsTrue((patch.size() > header.size()) && std::equal(header.begin(), header.end(), patch.begin()), L"the header of a view patch");
            Assert::IsTrue(first == PixelsWithPatches(0, 0));
            Assert::IsTrue(raw == PixelsWithPatches(2, 0));
            Assert::IsTrue(sierraview::ThreeLoopView().loops[0].cels[1].pixels == PixelsWithPatches(0, 1));
        }

        TEST_METHOD(Import_OneImage_GoesIntoItsCel)
        {
            const std::string folder = ExportView500();
            const std::string image = folder + "\\500.l0.c1.bmp";
            ChangeBmp(image, [](IndexedBmp &bmp) { bmp.pixels[2] = 30; });

            Expect(0, { "view", "import", _gameFolder, "--view", "500", "--loop", "0", "--cel", "1", "--image", image, "--out-dir", _patches });

            Assert::IsTrue(ReadIndexedBmpFile(image)->pixels == PixelsWithPatches(0, 1));
        }

        TEST_METHOD(Import_ImageThatFailsACheck_ExitsWith6)
        {
            const std::string folder = ExportView500();
            const std::string image = folder + "\\500.l0.c0.bmp";
            auto run = [&](const std::vector<std::string> &extra) -> cli::StringConsole
            {
                std::vector<std::string> args = { "view", "import", _gameFolder, "--view", "500", "--image", image, "--out-dir", _patches };
                args.insert(args.end(), extra.begin(), extra.end());
                cli::StringConsole console = Expect(6, args);
                Assert::IsTrue(FileNamesOf(_patches).empty(), Wide(console.err).c_str());
                return console;
            };

            // A mirror loop, and a cel that the view does not store.
            Assert::IsTrue(Has(run({ "--loop", "1" }).err, "loop 1 mirrors loop 0, so it stores no cel; its cels are those of loop 0"));
            Assert::IsTrue(Has(run({ "--cel", "5" }).err, "the view stores no cel 5 of loop 0"));
            // Another size.
            Assert::IsTrue(Has(run({ "--loop", "2" }).err, "has 5 x 3 pixels, and cel 0 of loop 2 has 3 x 2 [usage]"));
            // A pixel of the skip colour that moves.
            ChangeBmp(image, [](IndexedBmp &bmp) { bmp.pixels[3] = 0; });
            Assert::IsTrue(Has(run({}).err, "1 pixel changes to or from the skip colour 0 of cel 0 of loop 0 (the first at 3,0)"));
            // An entry that the view and palette 999 do not mark as used.
            ChangeBmp(image, [](IndexedBmp &bmp) { bmp.pixels[3] = 13; bmp.pixels[4] = 150; });
            Assert::IsTrue(Has(run({}).err, "1 pixel of cel 0 of loop 0 has palette entries that the view and palette 999 do not mark as used: 150 (--allow-unused-indices makes this a warning)"));
            cli::StringConsole allowed = Expect(0, { "view", "import", _gameFolder, "--view", "500", "--image", image, "--out-dir", _patches, "--allow-unused-indices" });
            Assert::IsTrue(Has(allowed.err, "scic: warning: view 500: ") && Has(allowed.err, "do not mark as used: 150"), Wide(allowed.err).c_str());
        }

        // --palette replace: the stored entries of the range get the colours
        // of the images; the entries that the palette does not store give a
        // warning; images that do not agree fail the view.
        TEST_METHOD(Import_PaletteReplace)
        {
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.sci2 = false;
            made.paletteCount = 90;
            PutViewIntoGame(500, sierraview::MakeSierraView(made));
            const std::string folder = _outDir + "\\exported";
            fs::create_directories(folder);
            Expect(0, { "view", "export", _gameFolder, "--view", "500", "--out-dir", folder });
            for (const char *file : { "500.l0.c0.bmp", "500.l0.c1.bmp", "500.l2.c0.bmp" })
            {
                ChangeBmp(folder + "\\" + file, [](IndexedBmp &bmp) { bmp.colors[85] = IndexedBmpColor{ 1, 2, 3 }; bmp.colors[95] = IndexedBmpColor{ 4, 5, 6 }; });
            }

            cli::StringConsole console = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--palette-range", "85-95", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: warning: view 500: its palette stores 0-89, so the entries 90-95 of the range get no colour"), Wide(console.err).c_str());
            CopyPatchesToGame();
            const std::string row = PaletteUseRowOf(500);
            Assert::IsTrue(Has(row, "85=010203"), Wide(row).c_str());

            ChangeBmp(folder + "\\500.l0.c1.bmp", [](IndexedBmp &bmp) { bmp.colors[86] = IndexedBmpColor{ 9, 9, 9 }; });
            cli::StringConsole disagree = Expect(6, { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--palette-range", "85-95", "--out-dir", _patches });
            Assert::IsTrue(Has(disagree.err, "have another colour for entry 86; the images of a view must have one palette"), Wide(disagree.err).c_str());
        }

        // The entries that the view does not mark as used (the images have
        // the colours of palette 999 there) keep their colours: the exported
        // images give no change. Another colour there gives a warning, and
        // images that differ only there agree.
        TEST_METHOD(Import_PaletteReplace_EntriesWithNoMarkKeepTheirColours)
        {
            const std::string folder = ExportView500();

            cli::StringConsole console = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--palette-range", "0-9", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "view 500: the images have the pixels of the view: the view does not change"), Wide(console.err).c_str());
            Assert::IsFalse(Has(console.err, "keep their colours"), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());

            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &bmp) { bmp.colors[5] = IndexedBmpColor{ 1, 2, 3 }; });
            cli::StringConsole other = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--palette-range", "0-9", "--out-dir", _patches });
            Assert::IsTrue(Has(other.err, "scic: warning: view 500: the view does not mark the entries 5 as used, so they keep their colours (the game takes their colours from another palette); the images have other colours for them, which the view does not get"),
                Wide(other.err).c_str());
            Assert::IsTrue(Has(other.err, "view 500: the images have the pixels of the view: the view does not change"), Wide(other.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        // New colours and new pixels in one run.
        TEST_METHOD(Import_PaletteReplace_WithNewPixels)
        {
            const std::string folder = ExportView500();
            for (const char *file : { "500.l0.c0.bmp", "500.l0.c1.bmp", "500.l2.c0.bmp" })
            {
                ChangeBmp(folder + "\\" + file, [](IndexedBmp &bmp) { bmp.colors[12] = IndexedBmpColor{ 7, 8, 9 }; });
            }
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &bmp) { bmp.pixels[0] = 12; });
            const std::vector<uint8_t> pixels = ReadIndexedBmpFile(folder + "\\500.l0.c0.bmp")->pixels;

            Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--palette-range", "12-12", "--out-dir", _patches });

            Assert::IsTrue(pixels == PixelsWithPatches(0, 0));
            const std::string row = PaletteUseRowOf(500);
            Assert::IsTrue(Has(row, "12=070809"), Wide(row).c_str());
        }

        TEST_METHOD(Import_PaletteReplace_ThatCannotChangeThePalette_FailsTheView)
        {
            const std::string folder = ExportView500();
            // A colour table of 16 entries.
            const std::string image = folder + "\\500.l0.c0.bmp";
            std::vector<uint8_t> bytes = ReadFileBytes(image);
            bytes[46] = 16;
            bytes[47] = 0;
            WriteFileBytes(image, bytes);
            cli::StringConsole shortTable = Expect(6, { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--palette-range", "10-20", "--out-dir", _patches });
            Assert::IsTrue(Has(shortTable.err, "the colour table has 16 entries, and the palette range is 10-20"), Wide(shortTable.err).c_str());

            // A view with no palette.
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.sci2 = false;
            made.hasPalette = false;
            PutViewIntoGame(501, sierraview::MakeSierraView(made));
            const std::string other = _outDir + "\\other";
            fs::create_directories(other);
            Expect(0, { "view", "export", _gameFolder, "--view", "501", "--out-dir", other });
            cli::StringConsole noPalette = Expect(6, { "view", "import", _gameFolder, "--in-dir", other, "--palette", "replace", "--palette-range", "10-20", "--out-dir", _patches });
            Assert::IsTrue(Has(noPalette.err, "view 501: the view has no palette, so no entry can get a new colour [unsupported]"), Wide(noPalette.err).c_str());
        }

        // An old patch file of a view with no change that cannot be removed:
        // exit 9.
        TEST_METHOD(Import_OldPatchFileThatCannotBeRemoved_ExitsWith9)
        {
            const std::string folder = ExportView500();
            WriteReadOnlyFile(_patches + "\\500.v56", "read-only");

            cli::StringConsole console = Expect(9, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "500.v56"), Wide(console.err).c_str());
            Assert::AreEqual(std::string("read-only"), ReadFileText(_patches + "\\500.v56"));
        }

        TEST_METHOD(Import_UsageErrors)
        {
            const std::string folder = ExportView500();
            const std::string image = folder + "\\500.l0.c0.bmp";
            const std::vector<std::pair<std::vector<std::string>, std::string>> cases =
            {
                { { "view", "import", _gameFolder, "--out-dir", _patches }, "give --in-dir, or --view with --image" },
                { { "view", "import", _gameFolder, "--in-dir", folder, "--view", "500", "--out-dir", _patches }, "give --in-dir, or --view with --image, not both" },
                { { "view", "import", _gameFolder, "--view", "500", "--out-dir", _patches }, "an image needs --view and --image" },
                { { "view", "import", _gameFolder, "--in-dir", folder, "--palette-range", "1-2", "--out-dir", _patches }, "--palette-range needs --palette replace" },
                { { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--out-dir", _patches }, "--palette replace needs --palette-range" },
                { { "view", "import", _gameFolder, "--in-dir", folder, "--palette", "replace", "--palette-range", "9-3", "--out-dir", _patches }, "--palette-range is first-last" },
                { { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", "" }, "--out-dir needs a folder" },
                { { "view", "import", _gameFolder, "--in-dir", "", "--out-dir", _patches }, "--in-dir needs a folder" },
                { { "view", "import", _gameFolder, "--view", "abc", "--image", image, "--out-dir", _patches }, "\"abc\" is not a view number" },
                { { "view", "import", _gameFolder, "--view", "5", "--image", image, "--out-dir", _patches }, "the game has no view 5" },
                { { "view", "import", _gameFolder, "--in-dir", _patches, "--out-dir", folder }, "the input folder has no .toml file of scic view export" },
                { { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _gameFolder }, "the output folder is the game folder" },
            };
            for (const auto &test : cases)
            {
                cli::StringConsole console = Expect(2, test.first);
                Assert::IsTrue(Has(console.err, test.second), Wide(test.second + ": " + console.err).c_str());
            }
            cli::StringConsole missing = Expect(3, { "view", "import", _gameFolder, "--in-dir", _outDir + "\\missing", "--out-dir", _patches });
            Assert::IsTrue(Has(missing.err, "the input folder does not exist"), Wide(missing.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        TEST_METHOD(Import_DryRun_WritesNothing)
        {
            const std::string folder = ExportView500();
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &image) { image.pixels[0] = 50; });

            cli::StringConsole console = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches, "--dry-run" });

            Assert::IsTrue(Has(console.err, "Would write " + _patches + "\\500.v56") && Has(console.err, "A run would import 1 of 1 view (3 cels) into "), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        // The output folder keeps a patch file of a named view only when this
        // run changed the view.
        TEST_METHOD(Import_OldPatchFile_OfAViewWithNoChangeOrThatFails_IsRemoved)
        {
            const std::string folder = ExportView500();
            WriteFileText(_patches + "\\500.v56", "old");

            cli::StringConsole noChange = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });
            Assert::IsTrue(Has(noChange.err, "Removed " + _patches + "\\500.v56 of an earlier run: view 500 has no change"), Wide(noChange.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());

            WriteFileText(_patches + "\\500.v56", "old");
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &image) { image.pixels[3] = 0; });
            cli::StringConsole failed = Expect(6, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });
            Assert::IsTrue(Has(failed.err, "Removed " + _patches + "\\500.v56 of an earlier run: view 500 failed"), Wide(failed.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        TEST_METHOD(Import_InDir_BadSidecars)
        {
            const std::string folder = ExportView500();
            const std::string text = ReadFileText(folder + "\\500.toml");
            WriteFileText(folder + "\\a-copy.toml", text);
            WriteFileText(folder + "\\b-no-view.toml", "[[cel]]\nloop = 0\ncel = 0\nfile = '500.l0.c0.bmp'\n");
            WriteFileText(folder + "\\c-no-file.toml", "[source]\nview = 900\n");
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &image) { image.pixels[0] = 50; });
            // The view of a .toml file that names no image file fails, and
            // loses the patch file of an earlier run.
            WriteFileText(_patches + "\\900.v56", "old");

            cli::StringConsole console = Expect(6, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });

            Assert::IsFalse(fs::exists(_patches + "\\900.v56"), Wide(console.err).c_str());

            Assert::IsTrue(Has(console.err, "b-no-view.toml") && Has(console.err, "the file has no view number ([source] view)"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "c-no-file.toml") && Has(console.err, "the file names no image file of a cel"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "view 500: the view is in two .toml files of the input folder; the second is ") && Has(console.err, "a-copy.toml"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Imported 1 of 4 views (3 cels) into ") && Has(console.err, "; 3 views failed."), Wide(console.err).c_str());
        }

        // A .toml file whose view failed still names the view: a later file
        // of the same view fails too.
        TEST_METHOD(Import_InDir_ViewOfAnEarlierFileThatFailed_Fails)
        {
            const std::string folder = ExportView500();
            // "500-old.toml" comes before "500.toml".
            WriteFileText(folder + "\\500-old.toml", "[source]\nview = 500\n");
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &image) { image.pixels[0] = 50; });

            cli::StringConsole console = Expect(6, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "500-old.toml") && Has(console.err, "the file names no image file of a cel"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "view 500: the view is in two .toml files of the input folder; the second is ") && Has(console.err, "500.toml"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Imported 0 of 2 views (0 cels) into ") && Has(console.err, "; 2 views failed."), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        TEST_METHOD(Import_InDir_CrcOfAnotherCopy_IsAWarning)
        {
            const std::string folder = ExportView500();
            sci::Result<ViewSidecar> sidecar = ReadViewSidecarFile(folder + "\\500.toml");
            AssertOk(sidecar);
            sidecar->crc32 = 0x12345678;
            WriteFileText(folder + "\\500.toml", ViewSidecarText(*sidecar));
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &image) { image.pixels[0] = 50; });

            cli::StringConsole console = Expect(0, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: warning: view 500: ") && Has(console.err, "the images are of another copy of the view"), Wide(console.err).c_str());
        }

        TEST_METHOD(Import_PatchFileThatCannotBeWritten_ExitsWith9)
        {
            const std::string folder = ExportView500();
            ChangeBmp(folder + "\\500.l0.c0.bmp", [](IndexedBmp &image) { image.pixels[0] = 50; });
            WriteReadOnlyFile(_patches + "\\500.v56", "read-only");

            cli::StringConsole console = Expect(9, { "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view 500: ") && Has(console.err, "500.v56"), Wide(console.err).c_str());
            Assert::AreEqual(std::string("read-only"), ReadFileText(_patches + "\\500.v56"));
        }

        TEST_METHOD(Import_CtrlC_ExitsWith7)
        {
            const std::string folder = ExportView500();
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "view", "import", _gameFolder, "--in-dir", folder, "--out-dir", _patches }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        // The new bytes of an SCI2 view, with no game: the pixels go into
        // their cels in place, and the resolution byte stays.
        TEST_METHOD(ImportData_Sci2View)
        {
            sierraview::MadeView made = sierraview::ThreeLoopView();
            made.resolution = 2;
            const std::vector<uint8_t> data = sierraview::MakeSierraView(made);
            sci::Result<ViewDescription> description = DescribeView(sciVersion2, data);
            AssertOk(description);
            std::array<bool, 256> used = {};
            used.fill(true);
            cli::ViewImportImage image;
            image.loop = 0;
            image.cel = 1;
            image.name = "image";
            image.image.width = 4;
            image.image.height = 2;
            image.image.pixels = made.loops[0].cels[1].pixels;
            image.image.pixels[3] = 77;
            std::vector<std::string> warnings;

            sci::Result<std::vector<uint8_t>> imported = cli::MakeViewImportData(sciVersion2, data, *description, used, { image }, cli::PicImportPalette(), false, warnings);

            AssertOk(imported);
            Assert::AreEqual(2, (int)(*imported)[5]);
            sci::Result<std::vector<uint8_t>> pixels = GetViewCelPixels(sciVersion2, *imported, 0, 1);
            AssertOk(pixels);
            Assert::IsTrue(image.image.pixels == *pixels);
            Assert::IsTrue(warnings.empty());
        }

        // The files of an SCI2 view, with no game.
        TEST_METHOD(ExportFiles_Sci2View)
        {
            const std::vector<uint8_t> data = sierraview::MakeSierraView(sierraview::ThreeLoopView());
            sci::Result<ViewDescription> description = DescribeView(sciVersion2, data);
            AssertOk(description);
            PaletteComponent colors;

            sci::Result<std::vector<cli::PicExportFile>> files = cli::MakeViewExportFiles(sciVersion2, 9, data, *description, false, colors, nullptr);

            AssertOk(files);
            std::vector<std::string> names;
            for (const cli::PicExportFile &file : *files)
            {
                names.push_back(file.name);
            }
            Assert::IsTrue(std::vector<std::string>{ "9.l0.c0.bmp", "9.l0.c1.bmp", "9.l2.c0.bmp", "9.toml" } == names);
            Assert::IsTrue(Has(files->back().text, "format = 'vga2'") && Has(files->back().text, "resolution = 1") && Has(files->back().text, "palette_999 = []"), Wide(files->back().text).c_str());

            ViewDescription noCel = *description;
            noCel.cels.clear();
            sci::Result<std::vector<cli::PicExportFile>> none = cli::MakeViewExportFiles(sciVersion2, 9, data, noCel, false, colors, nullptr);
            Assert::IsFalse(none.has_value());
            Assert::IsTrue(none.error().code == sci::ErrorCode::Unsupported);
        }

        // The text of a .toml file of export with no source and no file
        // names: the facts of the view.
        static std::string FactsOf(const std::string &path)
        {
            sci::Result<ViewSidecar> sidecar = ReadViewSidecarFile(path);
            AssertOk(sidecar, path);
            sidecar->view = 0;
            sidecar->from.clear();
            sidecar->size = 0;
            sidecar->crc32 = 0;
            for (ViewSidecarCel &cel : sidecar->cels)
            {
                cel.file.clear();
            }
            return ViewSidecarText(*sidecar);
        }

        // A view made from the files of export (4-byte entries for 0 to 255,
        // and 3-byte entries for 10 to 99) exports the same images and the
        // same facts.
        TEST_METHOD(Add_FromTheFilesOfExport_ExportsTheSameFiles)
        {
            sierraview::MadeView threeBytes = sierraview::ThreeLoopView();
            threeBytes.sci2 = false;
            threeBytes.entrySize = 3;
            threeBytes.paletteFirst = 10;
            threeBytes.paletteCount = 90;
            PutViewIntoGame(501, sierraview::MakeSierraView(threeBytes));
            const std::string folder = ExportView500();
            Expect(0, { "view", "export", _gameFolder, "--view", "501", "--out-dir", folder });

            cli::StringConsole added = Expect(0, { "view", "add", _gameFolder, "--view", "9000", "--from", folder + "\\500.toml", "--out-dir", _patches });
            Expect(0, { "view", "add", _gameFolder, "--view", "9001", "--from", folder + "\\501.toml", "--out-dir", _patches });

            Assert::IsTrue(Has(added.err, "Added view 9000 (3 loops, 3 cels) as " + _patches + "\\9000.v56."), Wide(added.err).c_str());
            CopyPatchesToGame();
            const std::string again = _outDir + "\\again";
            fs::create_directories(again);
            Expect(0, { "view", "export", _gameFolder, "--view", "9000", "9001", "--out-dir", again });
            for (const std::pair<int, int> &views : std::vector<std::pair<int, int>>{ { 500, 9000 }, { 501, 9001 } })
            {
                const std::string from = std::to_string(views.first);
                const std::string to = std::to_string(views.second);
                Assert::AreEqual(FactsOf(folder + "\\" + from + ".toml"), FactsOf(again + "\\" + to + ".toml"));
                for (const char *cel : { ".l0.c0.bmp", ".l0.c1.bmp", ".l2.c0.bmp" })
                {
                    Assert::IsTrue(ReadFileBytes(folder + "\\" + from + cel) == ReadFileBytes(again + "\\" + to + cel), Wide(to + cel).c_str());
                }
            }
        }

        TEST_METHOD(Add_ViewOfTheGame_NeedsReplace)
        {
            const std::string folder = ExportView500();
            ChangeBmp(folder + "\\500.l0.c1.bmp", [](IndexedBmp &image) { image.pixels[2] = 30; });

            cli::StringConsole has = Expect(2, { "view", "add", _gameFolder, "--view", "500", "--from", folder + "\\500.toml", "--out-dir", _patches });
            Assert::IsTrue(Has(has.err, "the game has view 500; give --replace to make a new view in its place, or use scic view import to change the cels of the view"), Wide(has.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());

            Expect(0, { "view", "add", _gameFolder, "--view", "500", "--from", folder + "\\500.toml", "--replace", "--out-dir", _patches });
            Assert::IsTrue(ReadIndexedBmpFile(folder + "\\500.l0.c1.bmp")->pixels == PixelsWithPatches(0, 1));
        }

        // The checks of the file and of the images (MakeAddedView).
        TEST_METHOD(AddedView_Checks)
        {
            const std::string folder = ExportView500();
            sci::Result<ViewSidecar> base = ReadViewSidecarFile(folder + "\\500.toml");
            AssertOk(base);
            std::vector<cli::ViewImportImage> baseImages;
            for (const ViewSidecarCel &cel : base->cels)
            {
                cli::ViewImportImage image;
                image.loop = cel.loop;
                image.cel = cel.cel;
                image.name = cel.file;
                image.image = *ReadIndexedBmpFile(folder + "\\" + cel.file);
                baseImages.push_back(image);
            }
            const std::array<bool, 256> noEntries = {};
            std::vector<std::string> warnings;

            sci::Result<NewView> view = cli::MakeAddedView(*base, baseImages, noEntries, false, warnings);

            AssertOk(view);
            Assert::IsTrue(warnings.empty());
            Assert::AreEqual((size_t)3, view->loops.size());
            Assert::AreEqual(0, view->loops[1].mirrorOf);
            Assert::AreEqual(1, (int)view->resolution);
            Assert::AreEqual(640, (int)view->nativeWidth);
            Assert::IsTrue(view->palette.present && (view->palette.entryCount == 256) && (view->palette.flags[10] == 1) && (view->palette.flags[100] == 0));
            Assert::IsTrue((view->loops[0].cels[0].x == -2) && (view->loops[0].cels[0].y == 3) && (view->loops[0].cels[1].skipColor == 255));

            auto fails = [&](const std::function<void(ViewSidecar &, std::vector<cli::ViewImportImage> &)> &change, const std::string &text)
            {
                ViewSidecar sidecar = *base;
                std::vector<cli::ViewImportImage> images = baseImages;
                change(sidecar, images);
                std::vector<std::string> ignored;
                sci::Result<NewView> failed = cli::MakeAddedView(sidecar, images, noEntries, false, ignored);
                Assert::IsFalse(failed.has_value(), Wide(text).c_str());
                Assert::IsTrue((failed.error().code == sci::ErrorCode::Usage) && Has(failed.error().message, text), Wide(text + "\n" + failed.error().ToString()).c_str());
            };
            using Images = std::vector<cli::ViewImportImage>;
            fails([](ViewSidecar &s, Images &) { s.loops.clear(); }, "the file has 0 [[loop]] tables; a view has 1 to 255 loops");
            fails([](ViewSidecar &s, Images &) { s.loopCount = 4; }, "[view] loops is 4, and the file has 3 [[loop]] tables");
            fails([](ViewSidecar &s, Images &) { s.loops[1].index = 0; }, "a [[loop]] has the index 0; the indices of the 3 loops are 0 to 2, each one time");
            fails([](ViewSidecar &s, Images &) { ViewSidecarCel cel = s.cels[0]; cel.loop = 1; s.cels.push_back(cel); }, "the [[cel]] of cel 0 of loop 1: loop 1 mirrors loop 0, so it has no cels of its own");
            fails([](ViewSidecar &s, Images &) { s.cels[1].cel = 2; }, "loop 0 has no [[cel]] for cel 1; the cels of a loop are 0 to n - 1");
            fails([](ViewSidecar &s, Images &) { s.cels[1].cel = 0; }, "the file has two [[cel]] tables for cel 0 of loop 0");
            fails([](ViewSidecar &s, Images &) { s.cels.pop_back(); s.celCount = 0; }, "the [[loop]] of loop 2 has cels = 1, and the loop has 0 cels");
            fails([](ViewSidecar &s, Images &) { s.celCount = 9; }, "[view] cels is 9, and the file has 3 [[cel]] tables");
            fails([](ViewSidecar &s, Images &) { s.loops[0].cels = 3; }, "the [[loop]] of loop 0 has cels = 3, and the loop has 2 cels");
            fails([](ViewSidecar &s, Images &) { s.loops[1].cels = 1; }, "the [[loop]] of loop 1 has cels = 1, and loop 0, whose cels it shows, has 2 cels");
            fails([](ViewSidecar &s, Images &) { s.loops[1].mirrorOf = 1; }, "loop 1 mirrors loop 1; a mirror shows the cels of another loop of the view, and a chain of mirrors ends at a loop that is not a mirror");
            fails([](ViewSidecar &s, Images &) { s.cels[0].width = 6; }, "500.l0.c0.bmp has 5 x 3 pixels, and the [[cel]] of cel 0 of loop 0 has 6 x 3");
            fails([](ViewSidecar &s, Images &i) { s.cels[0].file.clear(); i.erase(i.begin()); }, "the [[cel]] of cel 0 of loop 0 names no image file");
            fails([](ViewSidecar &, Images &i) { i[0].image.pixels[0] = 150; }, "500.l0.c0.bmp: 1 pixel of cel 0 of loop 0 has palette entries that the view and palette 999 do not mark as used: 150");
            fails([](ViewSidecar &, Images &i) { i[1].image.colors[50] = IndexedBmpColor{ 9, 9, 9 }; }, "500.l0.c0.bmp and 500.l0.c1.bmp have another colour for entry 50; the images of a view must have one palette");
            fails([](ViewSidecar &, Images &i) { i[0].image.colorCount = 100; }, "500.l0.c0.bmp: the colour table has 100 entries, and the palette stores the entries 0-255");
            fails([](ViewSidecar &s, Images &) { s.flags.push_back(PicSidecarFlagRun{ 0, 256, 1 }); }, "[palette] flags has the run 0-256, which is not in the stored entries 0-255");
            fails([](ViewSidecar &s, Images &) { s.storedFirst = 200; s.storedCount = 100; }, "[palette] stores 100 entries from entry 200");

            // An unused entry is a warning with allowUnusedEntries, and
            // images that differ in an entry with no mark agree.
            Images images = baseImages;
            images[0].image.pixels[0] = 150;
            images[1].image.colors[5] = IndexedBmpColor{ 9, 9, 9 };
            warnings.clear();
            sci::Result<NewView> allowed = cli::MakeAddedView(*base, images, noEntries, true, warnings);
            AssertOk(allowed);
            Assert::IsTrue((warnings.size() == 1) && Has(warnings[0], "do not mark as used: 150"), Wide(warnings.empty() ? std::string() : warnings[0]).c_str());
            Assert::IsTrue(images[0].image.colors[5] == allowed->palette.colors[5], L"the first image gives the colour of an entry with no mark");
            // A mirror of a mirror: its cels key is the count of the header
            // of the mirror that it names.
            ViewSidecar chain = *base;
            ViewSidecarLoop chainLoop;
            chainLoop.index = 3;
            chainLoop.mirrorOf = 1;
            chainLoop.cels = 0;
            chain.loops.push_back(chainLoop);
            chain.loopCount = 4;
            warnings.clear();
            sci::Result<NewView> chained = cli::MakeAddedView(chain, baseImages, noEntries, false, warnings);
            AssertOk(chained);
            Assert::AreEqual(0, chained->loops[1].headerCels);
            Assert::AreEqual(-1, chained->loops[3].headerCels);
            Assert::IsTrue((warnings.size() == 1) &&
                Has(warnings[0], "loop 3 mirrors loop 1, which is a mirror, and the [[loop]] of loop 3 has cels = 0 (or no cels key): the interpreters give loop 3 no cel"),
                Wide(warnings.empty() ? std::string("no warning") : warnings[0]).c_str());
            warnings.clear();
            chain.loops.back().cels = 2;
            AssertOk(cli::MakeAddedView(chain, baseImages, noEntries, false, warnings));
            Assert::IsTrue(warnings.empty(), L"a mirror of a mirror with cels: no warning");
            // Each mirror of the mirror with cels = 0 gets the warning.
            chain.loops.back().cels = 0;
            chainLoop.index = 4;
            chain.loops.push_back(chainLoop);
            chain.loopCount = 5;
            warnings.clear();
            AssertOk(cli::MakeAddedView(chain, baseImages, noEntries, false, warnings));
            Assert::AreEqual((size_t)2, warnings.size());
            Assert::IsTrue(Has(warnings[1], "the interpreters give loop 4 no cel"), Wide(warnings[1]).c_str());
            chain.loops.back().cels = 2;
            sci::Result<NewView> twoCounts = cli::MakeAddedView(chain, baseImages, noEntries, false, warnings);
            Assert::IsTrue(!twoCounts.has_value() && Has(twoCounts.error().message, "two loops mirror loop 1, and their [[loop]] tables have other cels (0 and 2)"),
                twoCounts.has_value() ? L"no error" : Wide(twoCounts.error().ToString()).c_str());
            // A mirror of a mirror of a loop with no cel: 0 is the only count,
            // and there is no warning.
            ViewSidecar empty = *base;
            ViewSidecarLoop emptyLoop;
            emptyLoop.index = 3;
            empty.loops.push_back(emptyLoop);
            emptyLoop.index = 4;
            emptyLoop.mirrorOf = 3;
            empty.loops.push_back(emptyLoop);
            emptyLoop.index = 5;
            emptyLoop.mirrorOf = 4;
            empty.loops.push_back(emptyLoop);
            empty.loopCount = 6;
            warnings.clear();
            AssertOk(cli::MakeAddedView(empty, baseImages, noEntries, false, warnings));
            Assert::IsTrue(warnings.empty(), warnings.empty() ? L"" : Wide(warnings[0]).c_str());
            // A stored entry with no flag run gets the flag 1.
            ViewSidecar noRuns = *base;
            noRuns.flags.clear();
            sci::Result<NewView> marked = cli::MakeAddedView(noRuns, baseImages, noEntries, false, warnings);
            AssertOk(marked);
            Assert::IsTrue((marked->palette.flags[0] == 1) && (marked->palette.flags[255] == 1));
            // A file with no palette gives a view with no palette.
            ViewSidecar noPalette = *base;
            noPalette.hasPalette = false;
            warnings.clear();
            sci::Result<NewView> none = cli::MakeAddedView(noPalette, baseImages, noEntries, true, warnings);
            AssertOk(none);
            Assert::IsFalse(none->palette.present);
            Assert::AreEqual((size_t)3, warnings.size(), L"a warning for each cel: no entry has a mark");
        }

        TEST_METHOD(Add_ViewThatFails_ExitsWith6AndLosesTheOldPatchFile)
        {
            WriteFileText(_outDir + "\\bad.toml", "[[loop]]\nindex = 0\nmirror_of = 0\n");
            WriteFileText(_patches + "\\9000.v56", "old");

            cli::StringConsole console = Expect(6, { "view", "add", _gameFolder, "--view", "9000", "--from", _outDir + "\\bad.toml", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view 9000: loop 0 mirrors loop 0; a mirror shows the cels of another loop of the view") && Has(console.err, "[usage]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Removed " + _patches + "\\9000.v56 of an earlier run: view 9000 failed"), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());

            // A game whose views scic cannot make.
            const std::string folder = ExportView500();
            _gameFolder = _game.Make(TemplateSci0);
            cli::StringConsole ega = Expect(6, { "view", "add", _gameFolder, "--view", "9000", "--from", folder + "\\500.toml", "--out-dir", _patches });
            Assert::IsTrue(Has(ega.err, "view 9000: a view of the format ega cannot be made; only the formats vga11 and vga2 can [unsupported]"), Wide(ega.err).c_str());
        }

        TEST_METHOD(Add_UsageErrors)
        {
            const std::string folder = ExportView500();
            const std::string from = folder + "\\500.toml";
            const std::vector<std::pair<std::vector<std::string>, std::string>> cases =
            {
                { { "view", "add", _gameFolder, "--view", "abc", "--from", from, "--out-dir", _patches }, "\"abc\" is not a view number (0 to 65535)" },
                { { "view", "add", _gameFolder, "--view", "70000", "--from", from, "--out-dir", _patches }, "\"70000\" is not a view number (0 to 65535)" },
                { { "view", "add", _gameFolder, "--view", "9000", "--from", "", "--out-dir", _patches }, "--from needs a .toml file" },
                { { "view", "add", _gameFolder, "--view", "9000", "--from", from, "--out-dir", "" }, "--out-dir needs a folder" },
                { { "view", "add", _gameFolder, "--view", "9000", "--from", from, "--out-dir", _gameFolder }, "the output folder is the game folder" },
            };
            for (const auto &test : cases)
            {
                cli::StringConsole console = Expect(2, test.first);
                Assert::IsTrue(Has(console.err, test.second), Wide(test.second + ": " + console.err).c_str());
            }
            cli::StringConsole missing = Expect(3, { "view", "add", _gameFolder, "--view", "9000", "--from", folder + "\\missing.toml", "--out-dir", _patches });
            Assert::IsTrue(Has(missing.err, "the .toml file does not exist: "), Wide(missing.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        TEST_METHOD(Add_DryRun_WritesNothing)
        {
            const std::string folder = ExportView500();

            cli::StringConsole console = Expect(0, { "view", "add", _gameFolder, "--view", "9000", "--from", folder + "\\500.toml", "--out-dir", _patches, "--dry-run" });

            Assert::IsTrue(Has(console.err, "Would write " + _patches + "\\9000.v56") && Has(console.err, "A run would add view 9000 (3 loops, 3 cels) as "), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        TEST_METHOD(Add_PatchFileThatCannotBeWritten_ExitsWith9)
        {
            const std::string folder = ExportView500();
            WriteReadOnlyFile(_patches + "\\9000.v56", "read-only");

            cli::StringConsole console = Expect(9, { "view", "add", _gameFolder, "--view", "9000", "--from", folder + "\\500.toml", "--out-dir", _patches });

            Assert::IsTrue(Has(console.err, "scic: error: view 9000: ") && Has(console.err, "9000.v56"), Wide(console.err).c_str());
            Assert::AreEqual(std::string("read-only"), ReadFileText(_patches + "\\9000.v56"));
        }

        TEST_METHOD(Add_CtrlC_ExitsWith7)
        {
            const std::string folder = ExportView500();
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "view", "add", _gameFolder, "--view", "9000", "--from", folder + "\\500.toml", "--out-dir", _patches }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(FileNamesOf(_patches).empty());
        }

        // The bytes of the copy of a view that the game uses; empty when the
        // game has no such view.
        std::vector<uint8_t> ViewBytesOfGame(int number)
        {
            GameSession &session = _game.Open();
            std::unique_ptr<ResourceBlob> blob = session.ResourceMap().MostRecentResource(ResourceType::View, number, false);
            std::vector<uint8_t> data;
            if (blob)
            {
                data.assign(blob->GetData(), blob->GetData() + blob->GetLength());
            }
            _game.CloseSessions();
            return data;
        }

        // Each copy goes: the patch file and the entry in the package. The
        // other views keep their bytes.
        TEST_METHOD(Remove_EachCopy)
        {
            const std::vector<uint8_t> other = ViewBytesOfGame(900);
            Assert::IsFalse(ViewBytesOfGame(0).empty(), L"setup: view 0 of the template is in the package");
            PutViewIntoGame(0, MakeViewBytes(sciVersion1_1));

            cli::StringConsole console = Expect(0, { "view", "remove", _gameFolder, "--view", "0" });

            Assert::IsTrue(Has(console.err, "Removed 1 of 1 view (1 patch file, 1 package entry)."), Wide(console.err).c_str());
            Assert::IsFalse(fs::exists(_gameFolder + "\\0.v56"));
            Assert::IsTrue(ViewBytesOfGame(0).empty(), L"the game has no view 0");
            Assert::IsTrue(other == ViewBytesOfGame(900), L"view 900 keeps its bytes");
            cli::StringConsole gone = Expect(2, { "view", "remove", _gameFolder, "--view", "0" });
            Assert::IsTrue(Has(gone.err, "the game has no view 0"), Wide(gone.err).c_str());
            Expect(0, { "view", "list", _gameFolder, "--view", "900" });
        }

        TEST_METHOD(Remove_FromPatch_KeepsThePackageCopy)
        {
            const std::vector<uint8_t> package = ViewBytesOfGame(0);
            PutViewIntoGame(0, MakeViewBytes(sciVersion1_1));
            Assert::IsFalse(package == ViewBytesOfGame(0), L"setup: the patch file hides the package copy");

            cli::StringConsole console = Expect(0, { "view", "remove", _gameFolder, "--view", "0", "--from", "patch" });

            Assert::IsTrue(Has(console.err, "view 0: the game now uses the copy of the view in the package"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Removed 1 of 1 view (1 patch file, 0 package entries)."), Wide(console.err).c_str());
            Assert::IsFalse(fs::exists(_gameFolder + "\\0.v56"));
            Assert::IsTrue(package == ViewBytesOfGame(0));

            // No patch file now: a usage error, and nothing changes.
            cli::StringConsole none = Expect(2, { "view", "remove", _gameFolder, "--view", "0", "900", "--from", "patch" });
            Assert::IsTrue(Has(none.err, "view 0 has no patch file, and --from patch removes only a patch file"), Wide(none.err).c_str());
            Assert::IsTrue(package == ViewBytesOfGame(0));
        }

        TEST_METHOD(Remove_DryRun_RemovesNothing)
        {
            PutViewIntoGame(0, MakeViewBytes(sciVersion1_1));
            const std::vector<uint8_t> map = ReadFileBytes(_gameFolder + "\\resource.map");
            const std::vector<uint8_t> volume = ReadFileBytes(_gameFolder + "\\resource.000");

            cli::StringConsole console = Expect(0, { "view", "remove", _gameFolder, "--view", "0", "--dry-run" });

            Assert::IsTrue(Has(console.err, "Would remove the patch file " + _gameFolder + "\\0.v56") && Has(console.err, "Would remove the entry of view 0 in package 0"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "A run would remove 1 of 1 view (1 patch file, 1 package entry)."), Wide(console.err).c_str());
            Assert::IsTrue(fs::exists(_gameFolder + "\\0.v56"));
            Assert::IsTrue(map == ReadFileBytes(_gameFolder + "\\resource.map"));
            Assert::IsTrue(volume == ReadFileBytes(_gameFolder + "\\resource.000"));
            cli::StringConsole patch = Expect(0, { "view", "remove", _gameFolder, "--view", "0", "--from", "patch", "--dry-run" });
            Assert::IsTrue(Has(patch.err, "view 0: the game would then use the copy of the view in the package"), Wide(patch.err).c_str());
        }

        TEST_METHOD(Remove_UsageErrors)
        {
            const std::vector<std::pair<std::vector<std::string>, std::string>> cases =
            {
                { { "view", "remove", _gameFolder, "--view", "5000" }, "the game has no view 5000" },
                { { "view", "remove", _gameFolder, "--view", "abc" }, "\"abc\" is not a view number or a range of view numbers" },
                { { "view", "remove", _gameFolder, "--view", "0", "--from", "package" }, "--from" },
                { { "view", "remove", _gameFolder }, "--view" },
            };
            for (const auto &test : cases)
            {
                cli::StringConsole console = Expect(2, test.first);
                Assert::IsTrue(Has(console.err, test.second), Wide(test.second + ": " + console.err).c_str());
            }
            Assert::IsFalse(ViewBytesOfGame(0).empty());
        }

        // A patch file that cannot be removed: exit 9, and the next view goes
        // on.
        TEST_METHOD(Remove_PatchFileThatCannotBeRemoved_ExitsWith9)
        {
            PutViewIntoGame(0, MakeViewBytes(sciVersion1_1));
            const std::string patch = _gameFolder + "\\0.v56";
            Assert::IsTrue(SetFileAttributesA(patch.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, L"setup: a read-only patch file");

            cli::StringConsole console = Expect(9, { "view", "remove", _gameFolder, "--view", "0", "900" });

            SetFileAttributesA(patch.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(Has(console.err, "scic: error: view 0: the patch file " + patch + " cannot be removed: "), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Removed 1 of 2 views") && Has(console.err, "; 1 view failed."), Wide(console.err).c_str());
            Assert::IsTrue(fs::exists(patch));
            Assert::IsTrue(ViewBytesOfGame(900).empty(), L"view 900 goes");
        }

        // The Result form of the removal: a copy that the source does not
        // have, and palette 999.
        TEST_METHOD(RemoveResource_Errors)
        {
            {
                GameSession &session = _game.Open();
                const SCIVersion &version = session.Version();
                ResourceBlob other(session.Helper(), nullptr, ResourceType::View, MakeViewBytes(version), version.DefaultVolumeFile, 900, NoBase36, version, ResourceSourceFlags::ResourceMap);
                sci::Status notFound = session.ResourceMap().RemoveResource(other);
                Assert::IsTrue(!notFound && (notFound.error().code == sci::ErrorCode::NotFound), notFound ? L"no error" : Wide(notFound.error().ToString()).c_str());
                std::unique_ptr<ResourceBlob> palette = session.ResourceMap().MostRecentResource(ResourceType::Palette, 999, false);
                Assert::IsNotNull(palette.get(), L"setup: palette 999");
                sci::Status global = session.ResourceMap().RemoveResource(*palette);
                Assert::IsTrue(!global && (global.error().code == sci::ErrorCode::Unsupported) && Has(global.error().message, "Palette 999 is the global palette"),
                    global ? L"no error" : Wide(global.error().ToString()).c_str());
                _game.CloseSessions();
            }
            Assert::IsFalse(ViewBytesOfGame(900).empty(), L"view 900 stays");
        }

        TEST_METHOD(Remove_CtrlC_ExitsWith7)
        {
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "view", "remove", _gameFolder, "--view", "0" }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsFalse(ViewBytesOfGame(0).empty(), L"nothing is removed");
        }

        TEST_METHOD(Help_ListsTheViewCommand)
        {
            cli::StringConsole group = Expect(0, { "view" });
            Assert::IsTrue(Has(group.out, "palette") && Has(group.out, "list") && Has(group.out, "scic view"), Wide(group.out).c_str());
            cli::StringConsole helpList = Expect(0, { "help", "view", "list" });
            Assert::IsTrue(Has(helpList.out, "--view") && Has(helpList.out, "--format") && Has(helpList.out, "scic view list"), Wide(helpList.out).c_str());
            cli::StringConsole helpGroup = Expect(0, { "help", "view" });
            Assert::IsTrue(Has(helpGroup.out, "palette"), Wide(helpGroup.out).c_str());
            cli::StringConsole helpPalette = Expect(0, { "help", "view", "palette" });
            Assert::IsTrue(Has(helpPalette.out, "--view") && Has(helpPalette.out, "--colors-from") && Has(helpPalette.out, "--clear-used") && Has(helpPalette.out, "--palette-range") &&
                Has(helpPalette.out, "--out-dir") && Has(helpPalette.out, "scic view palette"), Wide(helpPalette.out).c_str());
            cli::StringConsole root = Expect(0, { "help" });
            Assert::IsTrue(Has(OneLine(root.out), "pic import, view list, view export, view import, view add, view remove, view palette."), Wide(root.out).c_str());
            // The line of the group in the list of the commands.
            Assert::IsTrue(Has(OneLine(root.out), " view The view commands: list, export, import, add, remove, palette."), Wide(root.out).c_str());
            cli::StringConsole helpRemove = Expect(0, { "help", "view", "remove" });
            Assert::IsTrue(Has(helpRemove.out, "--view") && Has(helpRemove.out, "--from") && Has(OneLine(helpRemove.out), "A script that uses a removed view fails in the game") &&
                Has(helpRemove.out, "scic view remove"), Wide(helpRemove.out).c_str());
            cli::StringConsole helpAdd = Expect(0, { "help", "view", "add" });
            Assert::IsTrue(Has(helpAdd.out, "--view") && Has(helpAdd.out, "--from") && Has(helpAdd.out, "--replace") && Has(helpAdd.out, "--allow-unused-indices") && Has(helpAdd.out, "--out-dir") &&
                Has(helpAdd.out, "scic view add"), Wide(helpAdd.out).c_str());
            cli::StringConsole helpImport = Expect(0, { "help", "view", "import" });
            Assert::IsTrue(Has(helpImport.out, "--in-dir") && Has(helpImport.out, "--loop") && Has(helpImport.out, "--palette-range") && Has(helpImport.out, "scic view import"), Wide(helpImport.out).c_str());
            cli::StringConsole helpExport = Expect(0, { "help", "view", "export" });
            Assert::IsTrue(Has(helpExport.out, "--view") && Has(helpExport.out, "--all") && Has(helpExport.out, "--out-dir") && Has(helpExport.out, "scic view export"), Wide(helpExport.out).c_str());
        }
    };
}
