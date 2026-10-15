#include "stdafx.h"
#include "CppUnitTest.h"
#include "Cli.h"
#include "CliCommands.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "PaletteOperations.h"
#include "PaletteUse.h"
#include "PicImage.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "ResourceEntity.h"
#include "ResourceMap.h"
#include "ResourceUtil.h"
#include "View.h"
#include "format.h"
#include "TestSupport.h"
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // The entries of the array that are true, as runs: "10-12,90".
    std::string RunsOf(const std::array<bool, 256> &entries)
    {
        return IndexRunsText(IndexRunsOf(entries));
    }

    // A cel with these pixels, row after row. The bytes of a row after its
    // width have the padding value.
    Cel MakeCel(uint16_t width, uint16_t height, uint8_t transparentColor, const std::vector<uint8_t> &pixels, uint8_t padding)
    {
        Cel cel;
        cel.size = size16(width, height);
        cel.TransparentColor = transparentColor;
        cel.Data.allocate(cel.GetDataSize());
        for (size_t i = 0; i < cel.GetDataSize(); i++)
        {
            cel.Data[i] = padding;
        }
        for (size_t y = 0; y < height; y++)
        {
            for (size_t x = 0; x < width; x++)
            {
                cel.Data[(y * cel.GetStride()) + x] = pixels[(y * width) + x];
            }
        }
        return cel;
    }

    // The bytes of a palette with a header (the form of SCI1.1 and SCI2)
    // that stores the entries first to first + count - 1. With 3-byte
    // entries, the header has the flag of the entries. With 4-byte entries,
    // entry i has the flag flags[i] (1 when flags is empty). Entry e has
    // the colour (e, e + 1, e + 2).
    std::vector<uint8_t> MakePaletteBytes(int first, int count, bool threeByte, uint8_t headerFlag, const std::vector<uint8_t> &flags = {})
    {
        std::vector<uint8_t> bytes(37, 0);
        bytes[0] = 0x0e;
        bytes[10] = 1;
        bytes[25] = (uint8_t)first;
        bytes[29] = (uint8_t)(count & 0xff);
        bytes[30] = (uint8_t)(count >> 8);
        bytes[31] = headerFlag;
        bytes[32] = threeByte ? 1 : 0;
        for (int i = 0; i < count; i++)
        {
            const int entry = first + i;
            if (!threeByte)
            {
                bytes.push_back(flags.empty() ? (uint8_t)1 : flags[i]);
            }
            bytes.push_back((uint8_t)entry);
            bytes.push_back((uint8_t)(entry + 1));
            bytes.push_back((uint8_t)(entry + 2));
        }
        return bytes;
    }

    // The first line that starts with the text; empty when no line does.
    std::string LineWith(const std::vector<std::string> &lines, const std::string &start)
    {
        for (const std::string &line : lines)
        {
            if (line.compare(0, start.size(), start) == 0)
            {
                return line;
            }
        }
        return std::string();
    }

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

    // The parts of a text between a separator, an empty last part too.
    std::vector<std::string> PartsOf(const std::string &text, char separator)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        for (;;)
        {
            size_t end = text.find(separator, start);
            parts.push_back(text.substr(start, (end == std::string::npos) ? std::string::npos : end - start));
            if (end == std::string::npos)
            {
                return parts;
            }
            start = end + 1;
        }
    }

    bool Has(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }
}

namespace UnitTests
{
    // The palette entries of a view (PaletteUse.h).
    TEST_CLASS(TestPaletteUse)
    {
    public:
        TEST_METHOD(PixelEntriesAreThePixelsWithoutTheTransparentColorAndThePadding)
        {
            std::unique_ptr<ResourceEntity> view(CreateViewResource(sciVersion1_1));
            RasterComponent &raster = view->GetComponent<RasterComponent>();
            raster.Loops.clear();
            // A row of 5 pixels has 8 bytes.
            Loop first;
            first.Cels.push_back(MakeCel(5, 2, 7, { 10, 11, 12, 10, 7, 7, 7, 12, 12, 11 }, 200));
            Assert::AreEqual((size_t)16, first.Cels[0].GetDataSize(), L"setup: the rows have padding");
            // Entry 11 is the transparent colour of this cel only.
            Loop second;
            second.Cels.push_back(MakeCel(2, 1, 11, { 11, 90 }, 201));
            second.Cels.push_back(MakeCel(1, 1, 0, { 255 }, 202));
            raster.Loops.push_back(first);
            raster.Loops.push_back(second);

            ViewPaletteUse use = DescribeViewPaletteUse(raster, nullptr);

            Assert::AreEqual(2, use.loops);
            Assert::AreEqual(3, use.cels);
            Assert::AreEqual(std::string("10-12,90,255"), RunsOf(use.pixelEntries));
            Assert::IsFalse(use.hasPalette);
            Assert::AreEqual(std::string(""), RunsOf(use.paletteEntries));
        }

        TEST_METHOD(ACelWithFewerBytesThanItsSizeGivesThePixelsThatItHas)
        {
            std::unique_ptr<ResourceEntity> view(CreateViewResource(sciVersion1_1));
            RasterComponent &raster = view->GetComponent<RasterComponent>();
            raster.Loops.clear();
            Cel cel;
            cel.size = size16(4, 3);
            cel.TransparentColor = 99;
            cel.Data.allocate(4);
            for (size_t i = 0; i < 4; i++)
            {
                cel.Data[i] = (uint8_t)(20 + i);
            }
            Loop loop;
            loop.Cels.push_back(cel);
            raster.Loops.push_back(loop);

            ViewPaletteUse use = DescribeViewPaletteUse(raster, nullptr);

            Assert::AreEqual(std::string("20-23"), RunsOf(use.pixelEntries));
        }

        TEST_METHOD(PaletteEntriesAreTheEntriesThatThePaletteMarksAsUsed)
        {
            std::unique_ptr<ResourceEntity> view(CreateViewResource(sciVersion1_1));
            RasterComponent &raster = view->GetComponent<RasterComponent>();
            raster.Loops.clear();
            Loop loop;
            loop.Cels.push_back(MakeCel(1, 1, 0, { 5 }, 0));
            raster.Loops.push_back(loop);
            PaletteComponent palette;
            for (int entry = 0; entry < 256; entry++)
            {
                palette.Colors[entry].rgbReserved = 0;
            }
            palette.Colors[64].rgbReserved = 1;
            palette.Colors[65].rgbReserved = 1;
            palette.Colors[66].rgbReserved = 3;
            palette.Colors[255].rgbReserved = 1;

            ViewPaletteUse use = DescribeViewPaletteUse(raster, &palette);

            Assert::IsTrue(use.hasPalette);
            Assert::AreEqual(std::string("64-66,255"), RunsOf(use.paletteEntries));
            Assert::AreEqual(std::string("64-66,255"), RunsOf(UsedPaletteEntries(palette)));
            Assert::AreEqual(std::string("5"), RunsOf(use.pixelEntries));
        }

        TEST_METHOD(ACelOf65533PixelsInARowGivesThePixelsOfEachRow)
        {
            std::unique_ptr<ResourceEntity> view(CreateViewResource(sciVersion1_1));
            RasterComponent &raster = view->GetComponent<RasterComponent>();
            raster.Loops.clear();
            // A row of 65533 pixels has 65536 bytes, which 16 bits do not hold.
            const size_t stride = 65536;
            Cel cel;
            cel.size = size16(65533, 2);
            cel.TransparentColor = 0;
            cel.Data.allocate(stride * 2);
            for (size_t i = 0; i < stride * 2; i++)
            {
                cel.Data[i] = 0;
            }
            cel.Data[stride + 5] = 77;
            Loop loop;
            loop.Cels.push_back(cel);
            raster.Loops.push_back(loop);

            ViewPaletteUse use = DescribeViewPaletteUse(raster, nullptr);

            Assert::AreEqual(std::string("77"), RunsOf(use.pixelEntries));
        }

        TEST_METHOD(UsedEntriesAreReadAtTheOffsetOfThePalette)
        {
            // The palette is not at the start of the data.
            std::vector<uint8_t> data(4, 0xee);
            const std::vector<uint8_t> palette = MakePaletteBytes(80, 4, true, 1);
            data.insert(data.end(), palette.begin(), palette.end());

            sci::Result<std::array<bool, 256>> used = ReadUsedPaletteEntries(data.data(), data.size(), 4);

            AssertOk(used, "the used entries");
            Assert::AreEqual(std::string("80-83"), RunsOf(*used));
        }

        TEST_METHOD(UsedEntriesOfThreeByteEntriesHaveTheFlagOfTheHeader)
        {
            for (uint8_t flag : { (uint8_t)3, (uint8_t)0 })
            {
                const std::vector<uint8_t> data = MakePaletteBytes(80, 4, true, flag);

                sci::Result<std::array<bool, 256>> used = ReadUsedPaletteEntries(data.data(), data.size(), 0);

                AssertOk(used, "the used entries");
                // The flag 0 marks no entry as used.
                Assert::AreEqual(std::string((flag == 0) ? "" : "80-83"), RunsOf(*used), Wide(fmt::format("flag {0}", (int)flag)).c_str());
            }
        }

        TEST_METHOD(UsedEntriesOfFourByteEntriesHaveTheirOwnFlags)
        {
            // The flag of the header is not the flag of a 4-byte entry.
            const std::vector<uint8_t> data = MakePaletteBytes(200, 4, false, 0, { 0, 1, 0, 3 });

            sci::Result<std::array<bool, 256>> used = ReadUsedPaletteEntries(data.data(), data.size(), 0);

            AssertOk(used, "the used entries");
            Assert::AreEqual(std::string("201,203"), RunsOf(*used));
        }

        TEST_METHOD(UsedEntriesOfAPaletteThatStoresEntriesPast255AreAnError)
        {
            const std::vector<uint8_t> data = MakePaletteBytes(200, 100, true, 1);

            sci::Result<std::array<bool, 256>> used = ReadUsedPaletteEntries(data.data(), data.size(), 0);

            Assert::IsFalse(used.has_value(), L"the palette stores the entries 200 to 299");
            Assert::IsTrue(used.error().code == sci::ErrorCode::Format, Wide(used.error().ToString()).c_str());
        }
    };

    // scic dev palette-use, run in this process through RunCli, on a copy
    // of the SCI1.1 template game.
    TEST_CLASS(TestCliPaletteUse)
    {
        NoAppState _noAppState;
        GameCopy _game;
        std::string _gameFolder;

        // Run, and assert the exit code; the message is the output.
        cli::StringConsole Expect(int exitCode, const std::vector<std::string> &args)
        {
            cli::StringConsole console;
            int code = cli::RunCli(args, console);
            Assert::AreEqual(exitCode, code, Wide(console.out + console.err).c_str());
            return console;
        }

        // The numbers of the resources of a type, from the lowest.
        std::vector<int> NumbersOf(GameSession &session, ResourceType type)
        {
            std::set<int> numbers;
            auto container = session.ResourceMap().Resources(ResourceTypeToFlag(type), session.Helper().GetDefaultEnumFlags() | ResourceEnumFlags::MostRecentOnly);
            for (auto it = container->begin(); it != container->end(); ++it)
            {
                numbers.insert(it.GetResourceNumber());
            }
            return std::vector<int>(numbers.begin(), numbers.end());
        }

        std::unique_ptr<ResourceEntity> EntityOf(GameSession &session, ResourceType type, int number)
        {
            std::unique_ptr<ResourceBlob> blob = session.ResourceMap().MostRecentResource(type, number, false);
            Assert::IsNotNull(blob.get(), L"setup: the resource of the game");
            sci::Result<std::unique_ptr<ResourceEntity>> entity = TryCreateResourceFromResourceData(*blob);
            AssertOk(entity, "setup: the resource of the game");
            return std::move(*entity);
        }

        // Writes a patch file for a palette into the game copy: the header
        // of a patch file, then the data.
        void WritePalettePatch(GameSession &session, int number, const std::vector<uint8_t> &data)
        {
            std::vector<uint8_t> bytes = { 0x8b, 0x00 };
            bytes.insert(bytes.end(), data.begin(), data.end());
            WriteFileBytes(_gameFolder + "\\" + GetFileNameFor(ResourceType::Palette, number, NoBase36, session.Version()), bytes);
        }

        // A view with 1 cel of 3 x 2 pixels, and a palette that marks the
        // entries 64 to 66 as used. Entry e has the colour
        // (0x10 + e, 0x20 + e, 0x30 + e).
        std::unique_ptr<ResourceEntity> MakeViewWithPalette(GameSession &session)
        {
            std::unique_ptr<ResourceEntity> view(CreateViewResource(session.Version()));
            RasterComponent &raster = view->GetComponent<RasterComponent>();
            raster.Loops.clear();
            Loop loop;
            loop.Cels.push_back(MakeCel(3, 2, 0, { 64, 65, 0, 0, 70, 64 }, 0));
            raster.Loops.push_back(loop);
            view->AddComponent(std::make_unique<PaletteComponent>());
            PaletteComponent &palette = view->GetComponent<PaletteComponent>();
            for (int entry = 0; entry < 256; entry++)
            {
                palette.Colors[entry] = RGBQUAD();
            }
            for (int entry = 64; entry <= 66; entry++)
            {
                palette.Colors[entry].rgbRed = (BYTE)(0x10 + entry);
                palette.Colors[entry].rgbGreen = (BYTE)(0x20 + entry);
                palette.Colors[entry].rgbBlue = (BYTE)(0x30 + entry);
                palette.Colors[entry].rgbReserved = 1;
            }
            return view;
        }

        // The bytes of the view as a resource of the game.
        std::vector<uint8_t> BytesOf(const ResourceEntity &view, int number)
        {
            sci::ostream stream;
            std::map<BlobKey, uint32_t> propertyBag;
            view.WriteTo(stream, true, number, propertyBag);
            return std::vector<uint8_t>(stream.GetInternalPointer(), stream.GetInternalPointer() + stream.GetDataSize());
        }

        // Writes a patch file for the view into the game copy: the header
        // of a patch file with 2 more bytes, then the data.
        void WriteViewPatch(GameSession &session, int number, const std::vector<uint8_t> &data)
        {
            std::vector<uint8_t> bytes = { 0x80, 0x81, 0x00, 0x00 };
            bytes.insert(bytes.end(), data.begin(), data.end());
            WriteFileBytes(_gameFolder + "\\" + GetFileNameFor(ResourceType::View, number, NoBase36, session.Version()), bytes);
        }

    public:
        TEST_METHOD(PaletteUseHasARowForEachViewAndEachPalette)
        {
            _gameFolder = _game.Make(TemplateSci11);
            GameSession &session = _game.Open();
            const std::vector<int> views = NumbersOf(session, ResourceType::View);
            const std::vector<int> palettes = NumbersOf(session, ResourceType::Palette);
            Assert::IsTrue((views.size() >= 2) && !palettes.empty(), L"setup: the template has views and a palette");

            cli::StringConsole console = Expect(0, { "dev", "palette-use", _gameFolder });

            const std::vector<std::string> lines = LinesOf(console.out);
            Assert::AreEqual(1 + views.size() + palettes.size(), lines.size(), Wide(console.out).c_str());
            Assert::AreEqual(std::string(cli::PaletteUseHeader), lines[0]);
            Assert::AreEqual((size_t)9, PartsOf(lines[0], '\t').size());
            bool anyEmptyView = false;
            for (size_t i = 0; i < views.size(); i++)
            {
                std::unique_ptr<ResourceEntity> entity = EntityOf(session, ResourceType::View, views[i]);
                const PaletteComponent *palette = entity->TryGetComponent<PaletteComponent>();
                const ViewPaletteUse use = DescribeViewPaletteUse(entity->GetComponent<RasterComponent>(), palette);
                Assert::IsTrue((use.loops > 0) && (use.cels > 0), L"setup: the view has cels");
                // The views of the template have no palette, or a palette
                // with no entry: "-". A view with only transparent pixels
                // has "-" too.
                Assert::AreEqual(std::string(""), RunsOf(use.paletteEntries), L"setup: the view has no palette, or a palette with no entry");
                const std::string pixelEntries = RunsOf(use.pixelEntries);
                anyEmptyView = anyEmptyView || pixelEntries.empty();
                const std::string expected = fmt::format("view\t{0}\tpackage\t{1}\t{2}\t-\t{3}\t-\t", views[i], use.loops, use.cels, pixelEntries.empty() ? std::string("-") : pixelEntries);
                Assert::AreEqual(expected, lines[1 + i]);
            }
            Assert::IsTrue(anyEmptyView, L"setup: a view of the template has only transparent pixels");
            // View 900 of the template has 3 loops and 3 cels.
            Assert::AreEqual(std::string("view\t900\tpackage\t3\t3\t-\t0-1,3,5-7,16-23,31,255\t-\t"), lines[2]);
            for (size_t i = 0; i < palettes.size(); i++)
            {
                std::unique_ptr<ResourceEntity> entity = EntityOf(session, ResourceType::Palette, palettes[i]);
                const std::string expected = fmt::format("palette\t{0}\tpackage\t-\t-\t{1}\t-\t-\t", palettes[i], RunsOf(UsedPaletteEntries(entity->GetComponent<PaletteComponent>())));
                Assert::AreEqual(expected, lines[1 + views.size() + i]);
            }
            // Palette 999 of the template uses the entries 0 to 63 and 253 to 255.
            Assert::AreEqual(std::string("palette\t999\tpackage\t-\t-\t0-63,253-255\t-\t-\t"), lines.back());
            Assert::IsTrue(Has(console.err, fmt::format("Views: {0}. Palettes: {1}.", views.size(), palettes.size())), Wide(console.err).c_str());
        }

        TEST_METHOD(PaletteUseWithColorsHasTheColorOfEachUsedEntry)
        {
            _gameFolder = _game.Make(TemplateSci11);
            GameSession &session = _game.Open();
            std::unique_ptr<ResourceEntity> entity = EntityOf(session, ResourceType::Palette, 999);
            const PaletteComponent &palette = entity->GetComponent<PaletteComponent>();

            cli::StringConsole console = Expect(0, { "dev", "palette-use", _gameFolder, "--colors" });

            const std::vector<std::string> fields = PartsOf(LinesOf(console.out).back(), '\t');
            Assert::AreEqual((size_t)9, fields.size());
            Assert::AreEqual(std::string("999"), fields[1]);
            const std::vector<std::string> colors = PartsOf(fields[7], ' ');
            // The entries 0 to 63 and 253 to 255.
            Assert::AreEqual((size_t)67, colors.size(), Wide(fields[7]).c_str());
            for (size_t i = 0; i < colors.size(); i++)
            {
                const int entry = (i < 64) ? (int)i : (int)(253 + (i - 64));
                const RGBQUAD &color = palette.Colors[entry];
                Assert::AreEqual(fmt::format("{0}={1:02x}{2:02x}{3:02x}", entry, color.rgbRed, color.rgbGreen, color.rgbBlue), colors[i]);
            }
            // The first entry of the template is black, and the last is white.
            Assert::AreEqual(std::string("0=000000"), colors.front());
            Assert::AreEqual(std::string("255=ffffff"), colors.back());
        }

        TEST_METHOD(PaletteUseWritesTheTableIntoTheOutFile)
        {
            _gameFolder = _game.Make(TemplateSci11);
            const std::string file = _gameFolder + "-palette-use.tsv";
            const std::string table = Expect(0, { "dev", "palette-use", _gameFolder }).out;

            cli::StringConsole console = Expect(0, { "dev", "palette-use", _gameFolder, "--out", file });

            const std::string written = ReadFileText(file);
            std::error_code ec;
            fs::remove(file, ec);
            Assert::AreEqual(std::string(""), console.out);
            // The lines: the file has the line ends of a text file.
            Assert::IsTrue(LinesOf(table).size() > 2, L"setup: the table has rows");
            Assert::IsTrue(LinesOf(table) == LinesOf(written), Wide(written).c_str());
            Assert::IsTrue(Has(console.err, "Views: "), Wide(console.err).c_str());
        }

        TEST_METHOD(PaletteUseShowsThePaletteOfAViewAndItsColors)
        {
            _gameFolder = _game.Make(TemplateSci11);
            {
                // A new view 5 as a patch file.
                GameSession &session = _game.Open();
                std::unique_ptr<ResourceEntity> view = MakeViewWithPalette(session);
                WriteViewPatch(session, 5, BytesOf(*view, 5));
                _game.CloseSessions();
            }

            cli::StringConsole console = Expect(0, { "dev", "palette-use", _gameFolder, "--colors" });

            const std::vector<std::string> lines = LinesOf(console.out);
            // Views 0 and 900 are the first views of the template.
            Assert::AreEqual(std::string("view\t5\tpatch\t1\t1\t64-66\t64-65,70\t64=506070 65=516171 66=526272\t"), lines[2], Wide(console.out).c_str());
            Assert::IsTrue(Has(lines[1], "view\t0\tpackage\t"), Wide(lines[1]).c_str());
            Assert::IsTrue(Has(lines[3], "view\t900\tpackage\t3\t3\t-\t"), Wide(lines[3]).c_str());
            // Without the option, the cell has "-".
            const std::vector<std::string> plain = LinesOf(Expect(0, { "dev", "palette-use", _gameFolder }).out);
            Assert::AreEqual(std::string("view\t5\tpatch\t1\t1\t64-66\t64-65,70\t-\t"), plain[2]);
        }

        TEST_METHOD(PaletteUseReadsTheFlagOfThreeByteEntriesFromTheHeader)
        {
            _gameFolder = _game.Make(TemplateSci11);
            {
                // Two new palettes as patch files, with 3-byte entries that
                // do not start at entry 0: the header of 500 gives them the
                // flag 1, and the header of 501 gives them the flag 0.
                GameSession &session = _game.Open();
                WritePalettePatch(session, 500, MakePaletteBytes(80, 4, true, 1));
                WritePalettePatch(session, 501, MakePaletteBytes(80, 4, true, 0));
                _game.CloseSessions();
            }

            cli::StringConsole console = Expect(0, { "dev", "palette-use", _gameFolder, "--colors" });

            const std::vector<std::string> lines = LinesOf(console.out);
            Assert::AreEqual(std::string("palette\t500\tpatch\t-\t-\t80-83\t-\t80=505152 81=515253 82=525354 83=535455\t"), LineWith(lines, "palette\t500\t"), Wide(console.out).c_str());
            Assert::AreEqual(std::string("palette\t501\tpatch\t-\t-\t-\t-\t-\t"), LineWith(lines, "palette\t501\t"), Wide(console.out).c_str());
        }

        TEST_METHOD(PaletteUseReadsTheFlagOfThreeByteEntriesOfAView)
        {
            _gameFolder = _game.Make(TemplateSci11);
            {
                // The palette of the view gets the form with 3-byte entries
                // and the flag 0 in its header (the bytes 32 and 31 of the
                // palette). Its 256 entries then mark no entry as used; the
                // reader of the views gives each of them the flag 1.
                GameSession &session = _game.Open();
                std::vector<uint8_t> bytes = BytesOf(*MakeViewWithPalette(session), 5);
                const uint32_t paletteOffset = ReadViewPaletteOffsetVGA11(bytes.data(), bytes.size());
                Assert::IsTrue((paletteOffset != 0) && ((size_t)paletteOffset + 37 < bytes.size()), L"setup: the view has a palette");
                bytes[paletteOffset + 31] = 0;
                bytes[paletteOffset + 32] = 1;
                WriteViewPatch(session, 5, bytes);
                _game.CloseSessions();
                std::unique_ptr<ResourceEntity> read = EntityOf(_game.Open(), ResourceType::View, 5);
                Assert::AreEqual(std::string("0-255"), RunsOf(UsedPaletteEntries(read->GetComponent<PaletteComponent>())), L"setup: the flags that the reader gives");
                _game.CloseSessions();
            }

            cli::StringConsole console = Expect(0, { "dev", "palette-use", _gameFolder, "--colors" });

            Assert::AreEqual(std::string("view\t5\tpatch\t1\t1\t-\t64-65,70\t-\t"), LineWith(LinesOf(console.out), "view\t5\t"), Wide(console.out).c_str());
        }

        TEST_METHOD(PaletteUseGivesADamagedPaletteAnErrorInItsRow)
        {
            _gameFolder = _game.Make(TemplateSci11);
            {
                // The palette stores the entries 200 to 299.
                GameSession &session = _game.Open();
                WritePalettePatch(session, 500, MakePaletteBytes(200, 100, true, 1));
                _game.CloseSessions();
            }

            cli::StringConsole console = Expect(6, { "dev", "palette-use", _gameFolder });

            const std::string line = LineWith(LinesOf(console.out), "palette\t500\t");
            const std::string noValues = "palette\t500\t-\t-\t-\t-\t-\t-\t";
            Assert::AreEqual(noValues, line.substr(0, noValues.size()), Wide(console.out).c_str());
            Assert::IsTrue(line.size() > noValues.size(), L"the row has the error");
            Assert::IsTrue(Has(console.err, "palette 500: "), Wide(console.err).c_str());
            // The palette of the template has its row with no error.
            Assert::AreEqual(std::string("palette\t999\tpackage\t-\t-\t0-63,253-255\t-\t-\t"), LineWith(LinesOf(console.out), "palette\t999\t"));
        }

        TEST_METHOD(PaletteUseGivesAViewThatAReaderRepairedAnErrorInItsRow)
        {
            _gameFolder = _game.Make(TemplateSci11);
            {
                // A view whose second loop mirrors itself: the reader of the
                // views logs a warning and gives the loop a cel.
                GameSession &session = _game.Open();
                std::unique_ptr<ResourceEntity> view = MakeViewWithPalette(session);
                Loop mirror;
                mirror.IsMirror = true;
                mirror.MirrorOf = 1;
                view->GetComponent<RasterComponent>().Loops.push_back(mirror);
                WriteViewPatch(session, 5, BytesOf(*view, 5));
                _game.CloseSessions();
            }

            cli::StringConsole console = Expect(6, { "dev", "palette-use", _gameFolder });

            const std::string line = LineWith(LinesOf(console.out), "view\t5\t");
            const std::string noValues = "view\t5\t-\t-\t-\t-\t-\t-\t";
            Assert::AreEqual(noValues, line.substr(0, noValues.size()), Wide(console.out).c_str());
            Assert::IsTrue(Has(line, "Empty loop found"), Wide(line).c_str());
            Assert::IsTrue(Has(console.err, "view 5: "), Wide(console.err).c_str());
        }

        TEST_METHOD(PaletteUseRefusesAnOutFileInTheGameFolder)
        {
            _gameFolder = _game.Make(TemplateSci11);
            const std::string map = _gameFolder + "\\resource.map";
            const std::vector<uint8_t> before = ReadFileBytes(map);
            Assert::IsFalse(before.empty(), L"setup: the map of the game");

            cli::StringConsole console = Expect(2, { "dev", "palette-use", _gameFolder, "--out", map });

            Assert::IsTrue(before == ReadFileBytes(map), L"the map of the game did not change");
            Assert::IsTrue(Has(console.err, "--out"), Wide(console.err).c_str());
            // A folder of the game that is not there yet is in the game too.
            Expect(2, { "dev", "palette-use", _gameFolder, "--out", _gameFolder + "\\new\\table.tsv" });
            Assert::IsFalse(fs::exists(_gameFolder + "\\new"));
        }

        TEST_METHOD(PaletteUseRefusesBadOptionsAndAGameThatIsNotThere)
        {
            _gameFolder = _game.Make(TemplateSci11);
            Expect(2, { "dev", "palette-use", _gameFolder, "--out", "" });
            Expect(2, { "dev", "palette-use" });
            Expect(3, { "dev", "palette-use", _gameFolder + "-none" });
        }

        TEST_METHOD(PaletteUseWithAnOutFileThatCannotBeWrittenExitsWith9)
        {
            _gameFolder = _game.Make(TemplateSci11);

            cli::StringConsole console = Expect(9, { "dev", "palette-use", _gameFolder, "--out", _gameFolder + "-none\\table.tsv" });

            Assert::AreEqual(std::string(""), console.out);
            Assert::IsTrue(Has(console.err, "table.tsv"), Wide(console.err).c_str());
        }

        TEST_METHOD(PaletteUse_CtrlC_ExitsWith7)
        {
            _gameFolder = _game.Make(TemplateSci11);
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "dev", "palette-use", _gameFolder }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
        }

        TEST_METHOD(PaletteUseGivesAViewThatCannotBeReadAnErrorInItsRow)
        {
            _gameFolder = _game.Make(TemplateSci11);
            int damaged = 0;
            size_t viewCount = 0;
            {
                GameSession &session = _game.Open();
                const std::vector<int> views = NumbersOf(session, ResourceType::View);
                Assert::IsTrue(views.size() >= 2, L"setup: the template has views");
                damaged = views[0];
                viewCount = views.size();
                WriteViewPatch(session, damaged, std::vector<uint8_t>(100, 0xff));
                _game.CloseSessions();
            }

            cli::StringConsole console = Expect(6, { "dev", "palette-use", _gameFolder });

            const std::vector<std::string> lines = LinesOf(console.out);
            const std::vector<std::string> first = PartsOf(lines[1], '\t');
            Assert::AreEqual((size_t)9, first.size(), Wide(lines[1]).c_str());
            // The cells of the row before the error have "-".
            const std::string noValues = fmt::format("view\t{0}\t-\t-\t-\t-\t-\t-\t", damaged);
            Assert::AreEqual(noValues, lines[1].substr(0, noValues.size()));
            Assert::IsFalse(first[8].empty(), L"the row of the damaged view has the error");
            Assert::IsTrue(Has(console.err, fmt::format("view {0}: ", damaged)), Wide(console.err).c_str());
            // The next view has its row with no error.
            const std::vector<std::string> second = PartsOf(lines[2], '\t');
            Assert::AreEqual(std::string("view"), second[0]);
            Assert::AreEqual(std::string(""), second[8]);
            Assert::IsTrue(Has(console.err, fmt::format("Views: {0}.", viewCount)), Wide(console.err).c_str());
        }
    };
}
