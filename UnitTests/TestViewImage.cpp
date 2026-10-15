#include "stdafx.h"
#include "CppUnitTest.h"
#include "ViewImage.h"
#include "ViewSidecar.h"
#include "SidecarToml.h"
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

    uint32_t DwordOf(const std::vector<uint8_t> &data, size_t at)
    {
        return (uint32_t)data[at] | ((uint32_t)data[at + 1] << 8) | ((uint32_t)data[at + 2] << 16) | ((uint32_t)data[at + 3] << 24);
    }

    // The pixels of a run-length cel as ScummVM draws an SCI2 cel: each row
    // starts at the offsets of the row table, and has width pixels. An
    // assert fails for a run that goes past the end of its row.
    std::vector<uint8_t> DecodeWithRowTable(const std::vector<uint8_t> &data, size_t celHeader)
    {
        const int width = data[celHeader] | (data[celHeader + 1] << 8);
        const int height = data[celHeader + 2] | (data[celHeader + 3] << 8);
        const uint8_t skip = data[celHeader + 8];
        const uint32_t rle = DwordOf(data, celHeader + 24);
        const uint32_t literal = DwordOf(data, celHeader + 28);
        const uint32_t table = DwordOf(data, celHeader + 32);
        std::vector<uint8_t> pixels;
        for (int y = 0; y < height; y++)
        {
            size_t r = rle + DwordOf(data, table + (4 * (size_t)y));
            size_t l = literal + DwordOf(data, table + (4 * ((size_t)height + y)));
            int x = 0;
            while (x < width)
            {
                const uint8_t code = data[r++];
                int count = code & 0x3f;
                if ((code & 0xc0) == 0xc0)
                {
                    pixels.insert(pixels.end(), count, skip);
                }
                else if ((code & 0xc0) == 0x80)
                {
                    pixels.insert(pixels.end(), count, data[l++]);
                }
                else
                {
                    count += (code & 0x40) ? 64 : 0;
                    pixels.insert(pixels.end(), data.begin() + l, data.begin() + l + count);
                    l += count;
                }
                Assert::IsTrue(count > 0, L"a run of no pixel");
                x += count;
            }
            Assert::AreEqual(width, x, L"a run goes past the end of its row");
        }
        return pixels;
    }

    // The offsets of the cel headers of the view, in the order of the
    // stored cels.
    std::vector<size_t> CelHeadersOf(const std::vector<uint8_t> &data)
    {
        sci::Result<ViewLayout> layout = ReadViewLayout(data.data(), data.size());
        AssertOk(layout, "the layout of the view");
        std::vector<size_t> headers;
        for (const ViewLoopLayout &loop : layout->loops)
        {
            for (const ViewCelLayout &cel : loop.cels)
            {
                headers.push_back(cel.headerOffset);
            }
        }
        return headers;
    }

    // The offsets where the two byte vectors differ, in the bytes that both
    // have.
    std::vector<size_t> DifferencesOf(const std::vector<uint8_t> &one, const std::vector<uint8_t> &two)
    {
        std::vector<size_t> at;
        for (size_t i = 0; i < (std::min)(one.size(), two.size()); i++)
        {
            if (one[i] != two[i])
            {
                at.push_back(i);
            }
        }
        return at;
    }

    // Pixels of the size from first, with runs of the skip colour, runs of
    // one colour longer than 63, and single pixels.
    std::vector<uint8_t> MixedPixels(int width, int height, uint8_t skip, uint8_t first)
    {
        std::vector<uint8_t> pixels((size_t)width * height);
        for (size_t i = 0; i < pixels.size(); i++)
        {
            const size_t block = i / 70;
            pixels[i] = (block % 3 == 0) ? skip : ((block % 3 == 1) ? first : (uint8_t)(first + 1 + (i % 5)));
        }
        return pixels;
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
            Assert::AreEqual(1, (int)layout->byte4);
            Assert::AreEqual(640, (int)layout->nativeWidth);
            Assert::AreEqual(480, (int)layout->nativeHeight);
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
                Assert::AreEqual(1, (int)view->byte4);
                Assert::AreEqual(640, (int)view->nativeWidth);
                Assert::AreEqual(480, (int)view->nativeHeight);
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

        // Each row is its own runs, and the row table gives its start: a
        // decoder of each row from the row table gives the pixels.
        TEST_METHOD(Encode_EachRowIsItsOwnRuns)
        {
            for (const std::pair<int, int> &size : std::vector<std::pair<int, int>>{ { 1, 1 }, { 3, 2 }, { 150, 7 }, { 64, 3 }, { 200, 1 } })
            {
                const std::vector<uint8_t> pixels = MixedPixels(size.first, size.second, 9, 40);
                sci::Result<EncodedViewCel> made = EncodeViewCel(size.first, size.second, 9, pixels);
                AssertOk(made);
                const EncodedViewCel &encoded = *made;

                // A view with this one cel, as the bytes of the encoder.
                std::vector<uint8_t> data(36 + 8);
                PutWord(data, 0, (uint32_t)size.first);
                PutWord(data, 2, (uint32_t)size.second);
                data[8] = 9;
                PutDword(data, 24, (uint32_t)data.size());
                data.insert(data.end(), encoded.rle.begin(), encoded.rle.end());
                PutDword(data, 28, (uint32_t)data.size());
                data.insert(data.end(), encoded.literal.begin(), encoded.literal.end());
                const size_t table = data.size();
                PutDword(data, 32, (uint32_t)table);
                data.resize(table + (8 * (size_t)size.second));
                for (int y = 0; y < size.second; y++)
                {
                    PutDword(data, table + (4 * (size_t)y), encoded.rleRows[y]);
                    PutDword(data, table + (4 * ((size_t)size.second + y)), encoded.literalRows[y]);
                }

                Assert::IsTrue(pixels == DecodeWithRowTable(data, 0), Wide(fmt::format("{0} x {1}", size.first, size.second)).c_str());
            }
        }

        // A cel with run-length data gets new data after the end of the
        // view; only the 5 fields of its header change in the old bytes.
        TEST_METHOD(Replace_RunLengthCel_AppendsItsDataAndKeepsEachOtherByte)
        {
            for (bool sci2 : { true, false })
            {
                MadeView made = ThreeLoopView();
                made.sci2 = sci2;
                const SCIVersion &version = sci2 ? sciVersion2 : sciVersion1_1;
                const std::vector<uint8_t> data = MakeSierraView(made);
                const size_t header = CelHeadersOf(data)[0];
                const std::vector<uint8_t> pixels = { 60, 60, 60, 61, 0, 0, 62, 63, 64, 64, 65, 65, 65, 65, 0 };

                sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(version, data, { ViewCelPixels{ 0, 0, pixels } });

                AssertOk(patched);
                Assert::IsTrue(patched->size() > data.size());
                for (size_t at : DifferencesOf(data, *patched))
                {
                    const bool field = (at >= header + 12) && (at < header + 36) && !((at >= header + 20) && (at < header + 24));
                    Assert::IsTrue(field && (sci2 || (at < header + 32)), Wide(fmt::format("byte {0} changed", at)).c_str());
                }
                sci::Result<std::vector<std::vector<uint8_t>>> cels = GetAllViewCelPixels(version, *patched);
                AssertOk(cels);
                Assert::IsTrue(pixels == (*cels)[0]);
                Assert::IsTrue(made.loops[0].cels[1].pixels == (*cels)[1]);
                Assert::IsTrue(made.loops[2].cels[0].pixels == (*cels)[2]);
                Assert::AreEqual((uint32_t)data.size(), DwordOf(*patched, header + 24));
                if (sci2)
                {
                    Assert::IsTrue(pixels == DecodeWithRowTable(*patched, header));
                }
                // The sizes of the header: the run-length data, and that
                // and the literal data.
                const uint32_t rle = DwordOf(*patched, header + 16);
                Assert::AreEqual(DwordOf(*patched, header + 28) - DwordOf(*patched, header + 24), rle);
                const uint32_t end = sci2 ? DwordOf(*patched, header + 32) : (uint32_t)patched->size();
                Assert::AreEqual(end - DwordOf(*patched, header + 24), DwordOf(*patched, header + 12));
            }
        }

        // New pixels that have the runs of the old codes keep the codes, the
        // row table and the sizes: only literal bytes change.
        TEST_METHOD(Replace_RunLengthCel_WithTheOldRuns_ChangesOnlyItsLiteralData)
        {
            for (bool sci2 : { true, false })
            {
                MadeView made = ThreeLoopView();
                made.sci2 = sci2;
                const SCIVersion &version = sci2 ? sciVersion2 : sciVersion1_1;
                std::vector<CelPlace> places;
                const std::vector<uint8_t> data = MakeSierraView(made, &places);
                const std::vector<uint8_t> pixels(15, 20);

                sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(version, data, { ViewCelPixels{ 0, 0, pixels } });

                AssertOk(patched);
                Assert::AreEqual(data.size(), patched->size());
                const std::vector<size_t> differences = DifferencesOf(data, *patched);
                Assert::IsFalse(differences.empty());
                for (size_t at : differences)
                {
                    Assert::IsTrue((at >= places[0].literal) && (at < places[0].literal + 15), Wide(fmt::format("byte {0} changed", at)).c_str());
                }
                sci::Result<std::vector<uint8_t>> read = GetViewCelPixels(version, *patched, 0, 0);
                AssertOk(read);
                Assert::IsTrue(pixels == *read);
                if (sci2)
                {
                    Assert::IsTrue(pixels == DecodeWithRowTable(*patched, places[0].header));
                }
            }
        }

        // New run-length data that is not longer than the old data takes
        // its place: the view keeps its size. A run of one colour of the old
        // codes has two colours now (13 and 14 of row 0), so the codes change.
        TEST_METHOD(Replace_RunLengthCel_ThatFits_ChangesInPlace)
        {
            for (bool sci2 : { true, false })
            {
                // Row 0: a run of 3 pixels of 20, then 5 literal pixels; row
                // 1: 8 literal pixels. The codes have 3 bytes, the literal
                // data 14.
                MadeView made;
                made.sci2 = sci2;
                made.hasPalette = false;
                MadeLoop loop;
                MadeCel cel;
                cel.width = 8;
                cel.height = 2;
                cel.pixels = { 20, 20, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33 };
                loop.cels.push_back(cel);
                made.loops.push_back(loop);
                const SCIVersion &version = sci2 ? sciVersion2 : sciVersion1_1;
                std::vector<CelPlace> places;
                const std::vector<uint8_t> data = MakeSierraView(made, &places);
                // The run of 20 is 20, 20, 21 now: 2 literal pixels and a
                // run of 6 pixels of 21 (3 codes and 11 literal bytes).
                std::vector<uint8_t> pixels = cel.pixels;
                std::fill(pixels.begin() + 2, pixels.begin() + 8, (uint8_t)21);

                sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(version, data, { ViewCelPixels{ 0, 0, pixels } });

                AssertOk(patched);
                Assert::AreEqual(data.size(), patched->size());
                const size_t header = places[0].header;
                for (size_t at : DifferencesOf(data, *patched))
                {
                    const bool sizes = (at >= header + 12) && (at < header + 20);
                    const bool own = at >= places[0].rle;
                    Assert::IsTrue(sizes || own, Wide(fmt::format("byte {0} changed", at)).c_str());
                }
                Assert::AreEqual((uint32_t)places[0].rle, DwordOf(*patched, header + 24));
                Assert::AreEqual(3u, DwordOf(*patched, header + 16));
                Assert::AreEqual(14u, DwordOf(*patched, header + 12));
                sci::Result<std::vector<uint8_t>> read = GetViewCelPixels(version, *patched, 0, 0);
                AssertOk(read);
                Assert::IsTrue(pixels == *read);
                if (sci2)
                {
                    Assert::IsTrue(pixels == DecodeWithRowTable(*patched, header));
                }
            }
        }

        // A cel whose literal runs have single pixels of the skip colour (as
        // Sierra's encoder of SCI1.1 wrote them) keeps its codes when two
        // colours swap.
        TEST_METHOD(Replace_SkipPixelsInLiteralRuns_KeepTheOldCodes)
        {
            MadeView made;
            made.sci2 = false;
            made.skipInLiteral = true;
            made.hasPalette = false;
            MadeLoop loop;
            MadeCel cel;
            cel.width = 6;
            cel.height = 1;
            cel.skip = 9;
            cel.pixels = { 1, 9, 2, 9, 3, 4 };
            loop.cels.push_back(cel);
            made.loops.push_back(loop);
            std::vector<CelPlace> places;
            const std::vector<uint8_t> data = MakeSierraView(made, &places);
            Assert::AreEqual((uint8_t)6, data[places[0].rle], L"setup: one literal run of 6 pixels");

            sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion1_1, data, { ViewCelPixels{ 0, 0, { 2, 9, 1, 9, 3, 4 } } });

            AssertOk(patched);
            Assert::AreEqual(data.size(), patched->size());
            for (size_t at : DifferencesOf(data, *patched))
            {
                Assert::IsTrue((at >= places[0].literal) && (at < places[0].literal + 6), Wide(fmt::format("byte {0} changed", at)).c_str());
            }
            // A new pixel of the skip colour in a literal run is not
            // Sierra's form: the cel gets new codes.
            sci::Result<std::vector<uint8_t>> moved = ReplaceViewCelPixels(sciVersion1_1, data, { ViewCelPixels{ 0, 0, { 1, 9, 2, 9, 9, 4 } } });
            AssertOk(moved);
            Assert::AreNotEqual((int)data[places[0].rle], (int)(*moved)[DwordOf(*moved, places[0].header + 24)]);
        }

        // An SCI2 cel with no row table (the offset in its header is 0): it
        // gets none.
        TEST_METHOD(Replace_Sci2CelWithNoRowTable)
        {
            MadeView made = ThreeLoopView();
            made.loops[0].cels[0].noRowTable = true;
            std::vector<CelPlace> places;
            const std::vector<uint8_t> data = MakeSierraView(made, &places);
            const std::vector<uint8_t> pixels = { 60, 60, 60, 61, 0, 0, 62, 63, 64, 64, 65, 65, 65, 65, 0 };

            sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 0, 0, pixels } });

            AssertOk(patched);
            Assert::AreEqual(0u, DwordOf(*patched, places[0].header + 32));
            Assert::AreEqual(data.size() + 14, patched->size(), L"the run-length and the literal data, and no row table");
            sci::Result<std::vector<uint8_t>> read = GetViewCelPixels(sciVersion2, *patched, 0, 0);
            AssertOk(read);
            Assert::IsTrue(pixels == *read);
        }

        // A raw cel whose data is also bytes of the palette gets new data
        // after the view: the palette keeps its bytes.
        TEST_METHOD(Replace_RawCelInThePalette_AppendsItsData)
        {
            const std::vector<uint8_t> original = MakeSierraView(ThreeLoopView());
            std::vector<uint8_t> data = original;
            const size_t header = CelHeadersOf(data)[2];
            const uint32_t palette = DwordOf(data, 8);
            PutDword(data, header + 24, palette + 40);
            const std::vector<uint8_t> pixels = { 50, 51, 52, 53, 54, 55 };

            sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 2, 0, pixels } });

            AssertOk(patched);
            Assert::AreEqual(data.size() + 6, patched->size());
            Assert::AreEqual((uint32_t)data.size(), DwordOf(*patched, header + 24));
            for (size_t at : DifferencesOf(data, *patched))
            {
                Assert::IsTrue((at >= header + 24) && (at < header + 28), Wide(fmt::format("byte {0} changed", at)).c_str());
            }
        }

        // A raw cel whose pixels are not all in the view gets new data after
        // the view.
        TEST_METHOD(Replace_RawCelPastTheEnd_AppendsItsData)
        {
            std::vector<uint8_t> data = MakeSierraView(ThreeLoopView());
            const size_t header = CelHeadersOf(data)[2];
            Assert::AreEqual((uint32_t)(data.size() - 6), DwordOf(data, header + 24), L"setup: the raw cel is the last data");
            data.resize(data.size() - 2);
            const std::vector<uint8_t> pixels = { 50, 51, 52, 53, 54, 55 };

            sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 2, 0, pixels } });

            AssertOk(patched);
            Assert::AreEqual(data.size() + 6, patched->size());
            Assert::AreEqual((uint32_t)data.size(), DwordOf(*patched, header + 24));
        }

        TEST_METHOD(Replace_CelsThatCannotChange_AreUnsupported)
        {
            const MadeView made = ThreeLoopView();
            const std::vector<uint8_t> data = MakeSierraView(made);
            const std::vector<size_t> headers = CelHeadersOf(data);
            std::vector<uint8_t> pixels = made.loops[0].cels[0].pixels;
            pixels[0] = 99;

            // Byte 10 with the bit 0x80.
            std::vector<uint8_t> flagged = data;
            flagged[headers[0] + 10] = 0x80;
            AssertError(ReplaceViewCelPixels(sciVersion2, flagged, { ViewCelPixels{ 0, 0, pixels } }).error(), sci::ErrorCode::Unsupported, "byte 10 of the header of cel 0 of loop 0");

            // Loop 2 points at the cel headers of loop 0.
            std::vector<uint8_t> twins = data;
            const size_t loop2 = 18 + 32;
            twins[loop2 + 2] = 2;
            PutDword(twins, loop2 + 12, DwordOf(twins, 18 + 12));
            AssertError(ReplaceViewCelPixels(sciVersion2, twins, { ViewCelPixels{ 2, 0, pixels } }).error(), sci::ErrorCode::Unsupported, "cel 0 of loop 2 has the cel header of cel 0 of loop 0");
        }

        // Sierra's interpreter of SCI1.1 reads a view of 65535 bytes or
        // fewer.
        TEST_METHOD(Replace_Sci11ViewOver64KB_IsUnsupported)
        {
            MadeView made;
            made.sci2 = false;
            MadeLoop loop;
            loop.cels.push_back(PatternCel(250, 200, 0, 1, 250));
            loop.cels.push_back(PatternCel(250, 200, 0, 7, 1));
            made.loops.push_back(loop);
            made.hasPalette = false;
            const std::vector<uint8_t> data = MakeSierraView(made);
            Assert::IsTrue(data.size() < 0xffff, L"setup: a view under 64 KB");

            sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion1_1, data, { ViewCelPixels{ 0, 1, PatternCel(250, 200, 0, 1, 250).pixels } });

            Assert::IsFalse(patched.has_value());
            AssertError(patched.error(), sci::ErrorCode::Unsupported, "Sierra's interpreter of SCI1.1 reads a view of 65535 bytes or fewer");
            // The same change in an SCI2 view.
            made.sci2 = true;
            AssertOk(ReplaceViewCelPixels(sciVersion2, MakeSierraView(made), { ViewCelPixels{ 0, 1, PatternCel(250, 200, 0, 1, 250).pixels } }));
        }

        TEST_METHOD(Encode_PixelsOfAnotherSize_IsUsage)
        {
            AssertError(EncodeViewCel(3, 2, 0, std::vector<uint8_t>(5, 1)).error(), sci::ErrorCode::Usage, "a cel of 3 x 2 pixels needs 6 bytes of pixels, not 5");
            AssertError(EncodeViewCel(3, 2, 0, std::vector<uint8_t>(6, 1), 1).error(), sci::ErrorCode::Usage, "the shortest run of a colour is 1; it must be 2 or more");
        }

        // fillRun 2 gives a run code to 2 pixels of one colour.
        TEST_METHOD(Encode_FillRunOfTwo)
        {
            const std::vector<uint8_t> pixels = { 5, 5, 6, 6, 7, 8 };

            sci::Result<EncodedViewCel> two = EncodeViewCel(6, 1, 0, pixels, 2);
            sci::Result<EncodedViewCel> three = EncodeViewCel(6, 1, 0, pixels, 3);

            AssertOk(two);
            AssertOk(three);
            Assert::IsTrue((std::vector<uint8_t>{ 0x82, 0x82, 0x02 }) == two->rle);
            Assert::IsTrue((std::vector<uint8_t>{ 5, 6, 7, 8 }) == two->literal);
            Assert::IsTrue((std::vector<uint8_t>{ 0x06 }) == three->rle);
            Assert::IsTrue(pixels == three->literal);
        }

        // A cel of Sierra's SCI1.1 encoder, which gave a run code to 2
        // pixels: new pixels whose last run of 2 has two colours fit in its
        // place only in that form.
        TEST_METHOD(Replace_CelOfRunsOfTwo_FitsInPlace)
        {
            for (bool sci2 : { false, true })
            {
                MadeView made;
                made.sci2 = sci2;
                made.fillRun = 2;
                made.hasPalette = false;
                MadeLoop loop;
                MadeCel cel;
                cel.width = 8;
                cel.height = 2;
                cel.pixels = { 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17 };
                loop.cels.push_back(cel);
                made.loops.push_back(loop);
                const std::vector<uint8_t> data = MakeSierraView(made);
                const SCIVersion &version = sci2 ? sciVersion2 : sciVersion1_1;
                std::vector<uint8_t> swapped = cel.pixels;
                swapped[6] = 12;

                sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(version, data, { ViewCelPixels{ 0, 0, swapped } });

                AssertOk(patched);
                Assert::AreEqual(data.size(), patched->size(), L"the new data takes the place of the old data");
                sci::Result<std::vector<uint8_t>> read = GetViewCelPixels(version, *patched, 0, 0);
                AssertOk(read);
                Assert::IsTrue(swapped == *read);
            }
        }

        // A raw cel that no other header points at changes in place.
        TEST_METHOD(Replace_RawCel_ChangesInPlace)
        {
            const std::vector<uint8_t> data = MakeSierraView(ThreeLoopView());
            const std::vector<uint8_t> pixels = { 50, 51, 52, 53, 54, 55 };

            sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 2, 0, pixels } });

            AssertOk(patched);
            Assert::AreEqual(data.size(), patched->size());
            const uint32_t at = DwordOf(data, CelHeadersOf(data)[2] + 24);
            const std::vector<size_t> differences = DifferencesOf(data, *patched);
            Assert::IsFalse(differences.empty());
            for (size_t offset : differences)
            {
                Assert::IsTrue((offset >= at) && (offset < at + 6), Wide(fmt::format("byte {0} changed", offset)).c_str());
            }
            sci::Result<std::vector<uint8_t>> read = GetViewCelPixels(sciVersion2, *patched, 2, 0);
            AssertOk(read);
            Assert::IsTrue(pixels == *read);
        }

        // Two cel headers that point at one data: the changed cel gets new
        // data, and the other keeps the old data (a raw cel and a cel with
        // run-length data).
        TEST_METHOD(Replace_SharedData_TheOtherCelKeepsIt)
        {
            for (bool raw : { false, true })
            {
                MadeView made = ThreeLoopView();
                MadeCel first = made.loops[0].cels[1];
                first.raw = raw;
                MadeCel copy = first;
                copy.sharesDataWithPrevious = true;
                made.loops[0].cels = { first, copy };
                const std::vector<uint8_t> data = MakeSierraView(made);
                std::vector<uint8_t> pixels = first.pixels;
                pixels[3] = 77;

                sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 0, 1, pixels } });

                AssertOk(patched);
                sci::Result<std::vector<std::vector<uint8_t>>> cels = GetAllViewCelPixels(sciVersion2, *patched);
                AssertOk(cels);
                Assert::IsTrue(first.pixels == (*cels)[0], L"the other cel keeps its pixels");
                Assert::IsTrue(pixels == (*cels)[1]);
                Assert::IsTrue(patched->size() > data.size(), L"the new data is after the view");
            }
        }

        TEST_METHOD(Replace_NoChange_GivesTheSameBytes)
        {
            const MadeView made = ThreeLoopView();
            const std::vector<uint8_t> data = MakeSierraView(made);

            sci::Result<std::vector<uint8_t>> patched = ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 0, 0, made.loops[0].cels[0].pixels }, ViewCelPixels{ 2, 0, made.loops[2].cels[0].pixels } });

            AssertOk(patched);
            Assert::IsTrue(data == *patched);
        }

        TEST_METHOD(Replace_BadCels_AreUsage)
        {
            const MadeView made = ThreeLoopView();
            const std::vector<uint8_t> data = MakeSierraView(made);
            const std::vector<uint8_t> pixels = made.loops[0].cels[0].pixels;

            AssertError(ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 1, 0, pixels } }).error(), sci::ErrorCode::Usage, "the view stores no cel 0 of loop 1");
            AssertError(ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 0, 5, pixels } }).error(), sci::ErrorCode::Usage, "the view stores no cel 5 of loop 0");
            AssertError(ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 0, 0, pixels }, ViewCelPixels{ 0, 0, pixels } }).error(), sci::ErrorCode::Usage, "two pixel sets are for cel 0 of loop 0");
            AssertError(ReplaceViewCelPixels(sciVersion2, data, { ViewCelPixels{ 0, 0, std::vector<uint8_t>(14, 10) } }).error(), sci::ErrorCode::Usage, "cel 0 of loop 0 has 5 x 3 pixels (15 bytes), and the new pixels are 14 bytes");
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
            for (const char *part : { "view = 7", "format = 'vga2'", "from = 'patch'", "size = 1234", "crc32 = 0x89ABCDEF", "loops = 3", "cels = 3", "resolution = 1", "scale_flags = 0", "byte_4 = 1", "native_width = 640", "native_height = 480",
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
            Assert::AreEqual(written.byte4, read->byte4);
            Assert::AreEqual(written.resolution, read->resolution);
            Assert::AreEqual(written.nativeWidth, read->nativeWidth);
            Assert::AreEqual(written.nativeHeight, read->nativeHeight);
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
            Assert::IsTrue(read->cels[0].entries.empty() && read->used.empty() && read->flags.empty());
        }

        // A reader of a key keeps the value when the table has no such key.
        TEST_METHOD(ReadRuns_KeyThatIsNotThere_KeepsTheValue)
        {
            toml::table table;
            std::vector<IndexRun> runs = { IndexRun{ 1, 2 } };
            std::vector<PicSidecarFlagRun> flags(1);

            AssertOk(sidecartoml::ReadRuns(table, "[t]", "runs", runs));
            AssertOk(sidecartoml::ReadFlagRuns(table, "[t]", "flags", flags));

            Assert::AreEqual((size_t)1, runs.size());
            Assert::AreEqual((size_t)1, flags.size());
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
    };
}
