#include "stdafx.h"
#include "CppUnitTest.h"
#include "ViewImage.h"
#include "ViewSidecar.h"
#include "View.h"
#include "ResourceEntity.h"
#include "SierraViewBytes.h"
#include "TestSupport.h"
#include "format.h"
#include <filesystem>
#include <map>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace sierraview;

namespace
{
    // The bytes of a view of the version that the view writer of SCI
    // Companion makes: 1 loop with 1 cel of 3 x 2 pixels, and no palette.
    std::vector<uint8_t> WrittenViewBytes(const SCIVersion &version)
    {
        std::unique_ptr<ResourceEntity> view(CreateViewResource(version));
        RasterComponent &raster = view->GetComponent<RasterComponent>();
        Cel cel;
        cel.size = size16(3, 2);
        cel.TransparentColor = 0;
        cel.Data.allocate(cel.GetDataSize());
        cel.Data.fill(7);
        Loop loop;
        loop.Cels.push_back(cel);
        raster.Loops.push_back(loop);
        sci::ostream stream;
        std::map<BlobKey, uint32_t> propertyBag;
        view->WriteTo(stream, true, 5, propertyBag);
        return std::vector<uint8_t>(stream.GetInternalPointer(), stream.GetInternalPointer() + stream.GetDataSize());
    }

    void AssertError(const sci::Error &error, sci::ErrorCode code, const std::string &part)
    {
        Assert::IsTrue((error.code == code) && (error.ToString().find(part) != std::string::npos), Wide(error.ToString()).c_str());
    }
}

namespace UnitTests
{
    // The layout, the description and the pixels of views in the bytes of
    // their resource (ViewImage.h), on views in Sierra's form
    // (SierraViewBytes.h) and from the view writer.
    TEST_CLASS(TestViewImage)
    {
    public:
        TEST_METHOD(Layout_SierraForm_HasTheOffsetsAndTheFieldsOfEachHeader)
        {
            std::vector<CelPlace> places;
            const MadeView made = ThreeLoopView();
            const std::vector<uint8_t> data = MakeSierraView(made, &places);

            sci::Result<ViewLayout> layout = ReadViewLayout(data.data(), data.size());

            AssertOk(layout);
            Assert::AreEqual((uint32_t)18, layout->headerSize);
            Assert::AreEqual((int)made.resolution, (int)layout->resolution);
            Assert::AreEqual(52, (int)layout->celHeaderSize);
            Assert::AreEqual(16, (int)layout->loopHeaderSize);
            Assert::AreEqual((uint32_t)(18 + (16 * 3) + (52 * 3)), layout->paletteOffset);
            Assert::AreEqual((size_t)3, layout->loops.size());
            Assert::AreEqual(-1, layout->loops[0].mirrorOf);
            Assert::AreEqual(0, layout->loops[1].mirrorOf);
            Assert::AreEqual(2, layout->loops[1].celCount);
            Assert::IsTrue(layout->loops[1].cels.empty(), L"a mirror has no cel headers");
            Assert::AreEqual((uint32_t)(18 + 16), layout->loops[1].headerOffset);
            const std::vector<const ViewCelLayout *> cels = { &layout->loops[0].cels[0], &layout->loops[0].cels[1], &layout->loops[2].cels[0] };
            for (size_t i = 0; i < cels.size(); i++)
            {
                Assert::AreEqual((uint32_t)places[i].header, cels[i]->headerOffset);
                Assert::AreEqual((uint32_t)places[i].rle, cels[i]->rleOffset);
                Assert::AreEqual((uint32_t)places[i].literal, cels[i]->literalOffset);
                Assert::AreEqual((uint32_t)places[i].rowTable, cels[i]->rowTableOffset);
            }
            Assert::AreEqual(5, cels[0]->width);
            Assert::AreEqual(3, cels[0]->height);
            Assert::AreEqual(-2, cels[0]->x);
            Assert::AreEqual(3, cels[0]->y);
            Assert::AreEqual(255, (int)cels[1]->skipColor);
            Assert::AreEqual(0x8a, (int)cels[0]->compression);
            Assert::IsFalse(cels[0]->IsRaw());
            Assert::IsTrue(cels[2]->IsRaw());
        }

        TEST_METHOD(Layout_DamagedHeaders)
        {
            const std::vector<uint8_t> good = MakeSierraView(ThreeLoopView());
            AssertError(ReadViewLayout(good.data(), 17).error(), sci::ErrorCode::Format, "the view has 17 bytes; its header needs 18");

            std::vector<uint8_t> sci1 = good;
            sci1[12] = 33;
            AssertError(ReadViewLayout(sci1.data(), sci1.size()).error(), sci::ErrorCode::Unsupported, "the form of an SCI1 view");
            std::vector<uint8_t> smallLoop = good;
            smallLoop[12] = 15;
            AssertError(ReadViewLayout(smallLoop.data(), smallLoop.size()).error(), sci::ErrorCode::Format, "the loop headers of the view have 15 bytes");
            std::vector<uint8_t> smallCel = good;
            smallCel[13] = 35;
            AssertError(ReadViewLayout(smallCel.data(), smallCel.size()).error(), sci::ErrorCode::Format, "the cel headers of the view have 35 bytes");
            AssertError(ReadViewLayout(good.data(), 18 + 15).error(), sci::ErrorCode::Format, "the header of loop 0 is not in the 33 bytes of the view");
            AssertError(ReadViewLayout(good.data(), 18 + 48 + 35).error(), sci::ErrorCode::Format, "the header of cel 0 of loop 0 is not in the 101 bytes of the view");
            std::vector<uint8_t> badMirror = good;
            badMirror[18 + 16] = 3;
            AssertError(ReadViewLayout(badMirror.data(), badMirror.size()).error(), sci::ErrorCode::Format, "loop 1 mirrors loop 3, and the view has 3 loops");
            std::vector<uint8_t> farCel = good;
            PutDword(farCel, 18 + 12, 0x7ffffff0);
            AssertError(ReadViewLayout(farCel.data(), farCel.size()).error(), sci::ErrorCode::Format, "the header of cel 0 of loop 0 is not in the");
        }

        TEST_METHOD(Describe_SierraForm_HasTheLoopsTheCelsAndThePalette)
        {
            for (int celHeaderSize : { 52, 36 })
            {
                MadeView made = ThreeLoopView();
                made.celHeaderSize = celHeaderSize;
                const std::vector<uint8_t> data = MakeSierraView(made);

                sci::Result<ViewDescription> view = DescribeView(sciVersion2, data);

                AssertOk(view, fmt::format("cel headers of {0} bytes", celHeaderSize));
                Assert::IsTrue(view->format == ViewFormat::VGA2);
                Assert::AreEqual(1, (int)view->resolution);
                Assert::AreEqual((size_t)3, view->loops.size());
                Assert::AreEqual(1, view->MirrorLoops());
                Assert::AreEqual(2, view->loops[1].cels, L"a mirror has the cels of its loop");
                Assert::AreEqual((size_t)3, view->cels.size());
                Assert::AreEqual(1, view->RawCels());
                const ViewCelDescription *second = view->Find(0, 1);
                Assert::IsNotNull(second);
                Assert::AreEqual(3u, second->SkipPixels());
                Assert::AreEqual(4, second->width);
                Assert::IsNull(view->Find(1, 0), L"a mirror stores no cel");
                const ViewCelDescription *raw = view->Find(2, 0);
                Assert::IsTrue((raw != nullptr) && raw->raw);
                Assert::AreEqual(1u, raw->pixelCounts[40]);
                Assert::AreEqual(std::string("10-16,20-24,40-45"), IndexRunsText(IndexRunsOf(view->PixelEntries())));
                Assert::IsTrue(view->palette.present);
                Assert::AreEqual(4, view->palette.entrySize);
                Assert::AreEqual(std::string("10-99"), IndexRunsText(IndexRunsOf(view->palette.UsedEntries())));
            }
        }

        TEST_METHOD(Describe_Sci11_ViewOfTheWriter)
        {
            const std::vector<uint8_t> data = WrittenViewBytes(sciVersion1_1);

            sci::Result<ViewDescription> view = DescribeView(sciVersion1_1, data);

            AssertOk(view);
            Assert::IsTrue(view->format == ViewFormat::VGA1_1);
            Assert::AreEqual((size_t)1, view->cels.size());
            Assert::AreEqual(6u, view->cels[0].pixelCounts[7]);
            Assert::IsFalse(view->palette.present);
        }

        // A palette of 3-byte entries has one flag for all of them, in its
        // header: 0 marks none of them.
        TEST_METHOD(Describe_ThreeByteEntries_HaveTheFlagOfTheHeader)
        {
            for (uint8_t flag : { (uint8_t)0, (uint8_t)1 })
            {
                MadeView made = ThreeLoopView();
                made.entrySize = 3;
                made.paletteFirst = 0;
                made.paletteCount = 80;
                made.sharedFlag = flag;

                sci::Result<ViewDescription> view = DescribeView(sciVersion2, MakeSierraView(made));

                AssertOk(view);
                Assert::AreEqual(3, view->palette.entrySize);
                Assert::AreEqual(80, view->palette.entryCount);
                Assert::AreEqual(std::string(flag ? "0-79" : ""), IndexRunsText(IndexRunsOf(view->palette.UsedEntries())));
                Assert::AreEqual((int)made.colors[79][1], (int)view->palette.colors[79].green);
            }
        }

        TEST_METHOD(CelPixels_AreTheRowsFromTheTop)
        {
            const MadeView made = ThreeLoopView();
            const std::vector<uint8_t> data = MakeSierraView(made);
            const std::vector<std::pair<int, int>> cels = { { 0, 0 }, { 0, 1 }, { 2, 0 } };
            for (const auto &cel : cels)
            {
                sci::Result<std::vector<uint8_t>> pixels = GetViewCelPixels(sciVersion2, data, cel.first, cel.second);
                AssertOk(pixels);
                Assert::IsTrue(made.loops[cel.first].cels[cel.second].pixels == *pixels, Wide(fmt::format("loop {0} cel {1}", cel.first, cel.second)).c_str());
            }
            AssertError(GetViewCelPixels(sciVersion2, data, 1, 0).error(), sci::ErrorCode::Usage, "loop 1 mirrors loop 0: it stores no cel");
            AssertError(GetViewCelPixels(sciVersion2, data, 3, 0).error(), sci::ErrorCode::Usage, "the view has no loop 3; it has 3 loops");
            AssertError(GetViewCelPixels(sciVersion2, data, 0, 2).error(), sci::ErrorCode::Usage, "loop 0 has no cel 2; it has 2 cels");
            AssertError(GetViewCelPixels(sciVersion0, data, 0, 0).error(), sci::ErrorCode::Unsupported, "a view of the format ega cannot be read");
            AssertError(DescribeView(sciVersion1_Late, data).error(), sci::ErrorCode::Unsupported, "a view of the format vga1 cannot be read");
        }

        // A loop that is not a mirror and whose header has no cel is a form
        // of Sierra's views (view 905 of QfG4): it has no cel, and a mirror of
        // it has none. The reader logs no repair (DescribeView would give it
        // as an error).
        TEST_METHOD(Describe_LoopWhoseHeaderHasNoCel_HasNoCel)
        {
            MadeView made = ThreeLoopView();
            made.loops[2].cels.clear();
            MadeLoop mirror;
            mirror.mirrorOf = 2;
            made.loops.push_back(mirror);
            const std::vector<uint8_t> data = MakeSierraView(made);

            sci::Result<ViewDescription> view = DescribeView(sciVersion2, data);

            AssertOk(view);
            Assert::AreEqual((size_t)4, view->loops.size());
            Assert::AreEqual(0, view->loops[2].cels);
            Assert::AreEqual(0, view->loops[3].cels);
            Assert::AreEqual(2, view->loops[1].cels);
            Assert::AreEqual((size_t)2, view->cels.size());
            AssertError(GetViewCelPixels(sciVersion2, data, 2, 0).error(), sci::ErrorCode::Usage, "loop 2 has no cel 0; it has 0 cels");
        }

        // A loop that mirrors itself gets no cel from the reader, which then
        // repairs the view with a cel of its own and a warning.
        TEST_METHOD(Describe_MirrorOfItself_IsARepair)
        {
            MadeView made = ThreeLoopView();
            made.loops[1].mirrorOf = 1;

            sci::Result<ViewDescription> view = DescribeView(sciVersion2, MakeSierraView(made));

            Assert::IsFalse(view.has_value());
            AssertError(view.error(), sci::ErrorCode::Format, "the reader repaired damaged data: Empty loop found: loop 1.");
        }
    };

    // The .toml file of scic view export (ViewSidecar.h).
    TEST_CLASS(TestViewSidecar)
    {
        // The sidecar of the view of ThreeLoopView, with a source and files.
        static ViewSidecar MadeSidecar()
        {
            sci::Result<ViewDescription> view = DescribeView(sciVersion2, MakeSierraView(ThreeLoopView()));
            AssertOk(view, "setup: the description of the view");
            ViewSidecar sidecar = MakeViewSidecar(*view);
            sidecar.view = 7;
            sidecar.from = "patch";
            sidecar.size = 1234;
            sidecar.crc32 = 0x89abcdef;
            sidecar.palette999 = { IndexRun{ 0, 63 } };
            sidecar.used = { IndexRun{ 0, 99 } };
            sidecar.cels[0].file = "7.l0.c0.bmp";
            sidecar.cels[2].file = "7.l2.c0.bmp";
            return sidecar;
        }

    public:
        TEST_METHOD(Text_IsTomlWithTheValues)
        {
            const std::string text = ViewSidecarText(MadeSidecar());

            Assert::IsTrue(text.rfind("# scic view export: view 7.", 0) == 0, Wide(text).c_str());
            for (const char *part : { "view = 7", "format = 'vga2'", "from = 'patch'", "size = 1234", "crc32 = 0x89ABCDEF", "loops = 3", "cels = 3", "resolution = 1", "scale_flags = 0",
                "present = true", "stored_first = 0", "stored_count = 256", "entry_size = 4", "flags = [ [ 0, 9, 0 ], [ 10, 99, 1 ], [ 100, 255, 0 ] ]",
                "palette_999 = [ [ 0, 63 ] ]", "used = [ [ 0, 99 ] ]", "mirror_of = 0", "file = '7.l0.c0.bmp'", "file = '7.l2.c0.bmp'", "raw = true", "skip_color = 255",
                "entries = [ [ 20, 24 ] ]", "skip_pixels = 3", "x = -2" })
            {
                Assert::IsTrue(text.find(part) != std::string::npos, Wide(std::string(part) + " in:\n" + text).c_str());
            }
            Assert::AreEqual((size_t)1, CountOf(text, "mirror_of"), L"only the mirror has mirror_of");
            Assert::AreEqual((size_t)2, CountOf(text, "file = "), L"a cel with no file has no file key");
        }

        TEST_METHOD(Parse_GivesBackWhatTextWrote)
        {
            const ViewSidecar written = MadeSidecar();

            sci::Result<ViewSidecar> read = ParseViewSidecar(ViewSidecarText(written));

            AssertOk(read);
            Assert::IsTrue(read->hasView && read->hasCrc32);
            Assert::AreEqual(written.view, read->view);
            Assert::AreEqual(written.format, read->format);
            Assert::AreEqual(written.from, read->from);
            Assert::AreEqual(written.size, read->size);
            Assert::AreEqual(written.crc32, read->crc32);
            Assert::AreEqual(written.loopCount, read->loopCount);
            Assert::AreEqual(written.celCount, read->celCount);
            Assert::AreEqual(written.scaleFlags, read->scaleFlags);
            Assert::AreEqual(written.resolution, read->resolution);
            Assert::AreEqual(written.hasPalette, read->hasPalette);
            Assert::AreEqual(written.storedFirst, read->storedFirst);
            Assert::AreEqual(written.storedCount, read->storedCount);
            Assert::AreEqual(written.entrySize, read->entrySize);
            Assert::AreEqual(written.flags.size(), read->flags.size());
            for (size_t i = 0; i < written.flags.size(); i++)
            {
                Assert::IsTrue((written.flags[i].first == read->flags[i].first) && (written.flags[i].last == read->flags[i].last) && (written.flags[i].flag == read->flags[i].flag));
            }
            Assert::IsTrue(written.palette999 == read->palette999);
            Assert::IsTrue(written.used == read->used);
            Assert::AreEqual(written.loops.size(), read->loops.size());
            for (size_t i = 0; i < written.loops.size(); i++)
            {
                Assert::AreEqual(written.loops[i].index, read->loops[i].index);
                Assert::AreEqual(written.loops[i].cels, read->loops[i].cels);
                Assert::AreEqual(written.loops[i].mirrorOf, read->loops[i].mirrorOf);
            }
            Assert::AreEqual(written.cels.size(), read->cels.size());
            for (size_t i = 0; i < written.cels.size(); i++)
            {
                const ViewSidecarCel &a = written.cels[i];
                const ViewSidecarCel &b = read->cels[i];
                Assert::IsTrue((a.loop == b.loop) && (a.cel == b.cel) && (a.file == b.file) && (a.width == b.width) && (a.height == b.height) && (a.x == b.x) && (a.y == b.y) &&
                    (a.skipColor == b.skipColor) && (a.raw == b.raw) && (a.entries == b.entries) && (a.skipPixels == b.skipPixels), Wide(fmt::format("cel {0}", i)).c_str());
            }
        }

        TEST_METHOD(Parse_KeysThatAreNotThereKeepTheirValues)
        {
            sci::Result<ViewSidecar> read = ParseViewSidecar("[[cel]]\nloop = 1\ncel = 2\nfile = 'a.bmp'\n");

            AssertOk(read);
            Assert::IsFalse(read->hasView);
            Assert::IsFalse(read->hasCrc32);
            Assert::IsFalse(read->hasPalette);
            Assert::AreEqual((size_t)1, read->cels.size());
            Assert::AreEqual(1, read->cels[0].loop);
            Assert::AreEqual(2, read->cels[0].cel);
            Assert::AreEqual(std::string("a.bmp"), read->cels[0].file);
            Assert::AreEqual(0, read->cels[0].skipColor);
        }

        TEST_METHOD(Parse_BadText_IsFormat)
        {
            const std::vector<std::pair<std::string, std::string>> cases =
            {
                { "[source\n", "" },
                { "source = 3\n", "\"source\" is not a table ([source])" },
                { "[source]\nview = 70000\n", "[source]: \"view\" is 70000; it must be 0 to 65535" },
                { "[view]\nresolution = 'one'\n", "[view]: \"resolution\" is not a whole number" },
                { "[palette]\nflags = [ [ 3, 1, 0 ] ]\n", "an element of \"flags\" is not 3 numbers" },
                { "loop = 3\n", "\"loop\" is not an array of tables ([[loop]])" },
                { "loop = [ 3 ]\n", "an element of \"loop\" is not a table ([[loop]])" },
                { "[[loop]]\ncels = 2\n", "a [[loop]] has no index" },
                { "[[loop]]\nindex = 1\nmirror_of = 300\n", "[[loop]] 1: \"mirror_of\" is 300; it must be 0 to 255" },
                { "[[cel]]\nloop = 0\n", "a [[cel]] has no loop or no cel" },
                { "[[cel]]\nloop = 0\ncel = 1\nfile = 'sub\\\\a.bmp'\n", "[[cel]] 0 1: \"file\" must be a file name with no folder" },
                { "[[cel]]\nloop = 0\ncel = 1\nraw = 1\n", "[[cel]] 0 1: \"raw\" is not true or false" },
            };
            for (const auto &test : cases)
            {
                sci::Result<ViewSidecar> read = ParseViewSidecar(test.first);
                Assert::IsFalse(read.has_value(), Wide(test.first).c_str());
                AssertError(read.error(), sci::ErrorCode::Format, test.second);
                Assert::IsTrue(read.error().where.line > 0, Wide(test.first + ": " + read.error().ToString()).c_str());
            }
        }

        TEST_METHOD(ReadFile_MissingFileAndBadFile)
        {
            const std::string folder = (std::filesystem::temp_directory_path() / ("scicompanion-viewsidecar-" + std::to_string(GetCurrentProcessId()))).string();
            std::filesystem::create_directories(folder);
            const std::string missing = folder + "\\missing.toml";
            sci::Result<ViewSidecar> notThere = ReadViewSidecarFile(missing);
            Assert::IsFalse(notThere.has_value());
            Assert::IsTrue(notThere.error().code == sci::ErrorCode::NotFound, Wide(notThere.error().ToString()).c_str());
            Assert::AreEqual(missing, notThere.error().where.file);

            const std::string bad = folder + "\\bad.toml";
            WriteFileText(bad, "[[cel]]\nloop = 'x'\ncel = 0\n");
            sci::Result<ViewSidecar> badRead = ReadViewSidecarFile(bad);
            Assert::IsFalse(badRead.has_value());
            AssertError(badRead.error(), sci::ErrorCode::Format, "[[cel]]: \"loop\" is not a whole number");
            Assert::AreEqual(bad, badRead.error().where.file);
            RemoveFolder(folder);
        }

        // The view header of some views of SQ6 has 20 bytes (the word at
        // offset 0 is 18): the loop headers come after it.
        TEST_METHOD(Layout_LongerViewHeader)
        {
            MadeView made = ThreeLoopView();
            made.headerWord = 18;
            made.scaleFlags = 1;
            const std::vector<uint8_t> data = MakeSierraView(made);

            sci::Result<ViewLayout> layout = ReadViewLayout(data.data(), data.size());
            sci::Result<ViewDescription> view = DescribeView(sciVersion2, data);

            AssertOk(layout);
            Assert::AreEqual((uint32_t)20, layout->headerSize);
            Assert::AreEqual((uint32_t)(20 + 16), layout->loops[1].headerOffset);
            AssertOk(view);
            Assert::AreEqual(1, (int)view->scaleFlags);
            Assert::AreEqual((size_t)3, view->cels.size());
            Assert::AreEqual(1u, view->Find(2, 0)->pixelCounts[45]);
        }

        // Two cel headers can point at the same data.
        TEST_METHOD(Layout_TwoCelHeadersWithOneData)
        {
            MadeView made = ThreeLoopView();
            MadeCel copy = made.loops[0].cels[1];
            copy.sharesDataWithPrevious = true;
            copy.x = 7;
            made.loops[0].cels.push_back(copy);
            const std::vector<uint8_t> data = MakeSierraView(made);

            sci::Result<ViewLayout> layout = ReadViewLayout(data.data(), data.size());

            AssertOk(layout);
            const std::vector<ViewCelLayout> &cels = layout->loops[0].cels;
            Assert::AreEqual((size_t)3, cels.size());
            Assert::AreEqual(cels[1].rleOffset, cels[2].rleOffset);
            Assert::AreEqual(cels[1].literalOffset, cels[2].literalOffset);
            Assert::AreEqual(7, cels[2].x);
            sci::Result<std::vector<uint8_t>> pixels = GetViewCelPixels(sciVersion2, data, 0, 2);
            AssertOk(pixels);
            Assert::IsTrue(copy.pixels == *pixels);
        }

        TEST_METHOD(SetCelTopDownPixels_NeedsThePixelsOfTheCel)
        {
            Cel cel;
            cel.size = size16(3, 2);
            cel.Data.allocate(cel.GetDataSize());
            cel.Data.fill(0);

            AssertError(SetCelTopDownPixels(cel, std::vector<uint8_t>(5, 1)).error(), sci::ErrorCode::Usage, "the cel has 3 x 2 pixels (6 bytes), and the new pixels are 5 bytes");
            AssertOk(SetCelTopDownPixels(cel, { 1, 2, 3, 4, 5, 6 }));
            sci::Result<std::vector<uint8_t>> pixels = CelTopDownPixels(cel);
            AssertOk(pixels);
            Assert::IsTrue((std::vector<uint8_t>{ 1, 2, 3, 4, 5, 6 }) == *pixels);
        }
    };
}
