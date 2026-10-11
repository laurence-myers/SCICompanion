#include "stdafx.h"
#include "CppUnitTest.h"
#include "Cli.h"
#include "CliCommands.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "IndexedBmp.h"
#include "PaletteOperations.h"
#include "Pic.h"
#include "PicImage.h"
#include "PicSidecar.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceMap.h"
#include "ResourceUtil.h"
#include "TomlFile.h"
#include "crc.h"
#include "format.h"
#include "Helper.h"
#include "TestSupport.h"
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
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

    // The fields of a line of a tsv table, an empty last field too.
    std::vector<std::string> TabFieldsOf(const std::string &line)
    {
        std::vector<std::string> fields;
        size_t start = 0;
        for (;;)
        {
            size_t tab = line.find('\t', start);
            fields.push_back(line.substr(start, (tab == std::string::npos) ? std::string::npos : tab - start));
            if (tab == std::string::npos)
            {
                return fields;
            }
            start = tab + 1;
        }
    }

    bool Has(const std::string &text, const std::string &part)
    {
        return text.find(part) != std::string::npos;
    }

    // The runs of an array of [first, last] arrays, as "0-2,7-7".
    std::string RunsOf(const toml::node_view<const toml::node> &node)
    {
        std::string text;
        const toml::array *runs = node.as_array();
        Assert::IsNotNull(runs, L"an array of runs");
        for (const toml::node &run : *runs)
        {
            const toml::array *pair = run.as_array();
            Assert::IsNotNull(pair, L"a run is an array");
            text += text.empty() ? "" : ",";
            for (size_t i = 0; i < pair->size(); i++)
            {
                text += ((i == 0) ? "" : "-") + std::to_string((*pair)[i].value_or((int64_t)-1));
            }
        }
        return text;
    }
}

namespace UnitTests
{
    // scic pic list and scic pic export (plan section 4.7), run in this
    // process through RunCli, on copies of the template games. The SCI1.1
    // template has the pics 0 (no cel), 100, 110 and 200.
    TEST_CLASS(TestCliPic)
    {
        NoAppState _noAppState;
        GameCopy _game;
        std::string _gameFolder;
        std::string _outDir;

        void CopyTemplate(const char *templateFolder = TemplateSci11)
        {
            _gameFolder = _game.Make(templateFolder);
        }

        // Run, and assert the exit code; the message is the output.
        cli::StringConsole Expect(int exitCode, const std::vector<std::string> &args)
        {
            cli::StringConsole console;
            int code = cli::RunCli(args, console);
            Assert::AreEqual(exitCode, code, Wide(console.out + console.err).c_str());
            return console;
        }

        // The data of a pic of the game copy.
        std::vector<uint8_t> PicDataOf(GameSession &session, int number)
        {
            std::unique_ptr<ResourceBlob> blob = session.ResourceMap().MostRecentResource(ResourceType::Pic, number, false);
            Assert::IsNotNull(blob.get(), L"setup: the pic of the game");
            return std::vector<uint8_t>(blob->GetData(), blob->GetData() + blob->GetLength());
        }

        // Writes a patch file for the pic into the game copy: the header of
        // a patch file, then the data.
        void WritePicPatch(int number, const std::vector<uint8_t> &data)
        {
            std::vector<uint8_t> bytes = { 0x81, 0x81, 0x00, 0x00 };
            bytes.insert(bytes.end(), data.begin(), data.end());
            WriteFileBytes(_gameFolder + "\\" + std::to_string(number) + ".p56", bytes);
        }

    public:
        TEST_METHOD_INITIALIZE(SetUp)
        {
            _outDir = (fs::temp_directory_path() / ("scicompanion-pic-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()))).string();
            fs::create_directories(_outDir);
        }

        TEST_METHOD_CLEANUP(TearDown)
        {
            cli::CancelFlag().store(false);
            RemoveFolder(_outDir);
        }

        TEST_METHOD(List_Text_HasARowForEachPic)
        {
            CopyTemplate();

            cli::StringConsole console = Expect(0, { "pic", "list", _gameFolder });

            std::vector<std::string> lines = LinesOf(console.out);
            Assert::AreEqual((size_t)5, lines.size(), Wide(console.out).c_str());
            Assert::IsTrue(lines[0].rfind("Pic  Format  Size     Cels  Comp  Background cel     Source   Bytes  CRC32     Skip px  Palette entries  Pixel entries", 0) == 0, Wide(console.out).c_str());
            Assert::IsTrue(lines[1].rfind("  0  vga11   320x190     0     0  -                  package", 0) == 0, Wide(console.out).c_str());
            Assert::IsTrue(lines[2].rfind("100  vga11   320x200     1     0  0: 320x200 at 0,0  package", 0) == 0, Wide(console.out).c_str());
            Assert::IsTrue(Has(lines[2], "  0-255            64-159"), Wide(console.out).c_str());
            Assert::IsTrue(lines[3].rfind("110  vga11   320x190     1     0  0: 320x190 at 0,0  package", 0) == 0, Wide(console.out).c_str());
            Assert::IsTrue(lines[4].rfind("200  vga11   320x190     1     0  0: 320x190 at 0,0  package", 0) == 0, Wide(console.out).c_str());
            for (const std::string &line : lines)
            {
                Assert::IsFalse(line.empty() || (line.back() == ' '), Wide("no spaces at the end of a line: \"" + line + "\"").c_str());
            }
            Assert::IsTrue(console.err.empty(), Wide(console.err).c_str());
        }

        TEST_METHOD(List_Tsv_HasTheValuesOfThePic)
        {
            CopyTemplate();
            std::vector<uint8_t> data = PicDataOf(_game.Open(), 100);
            _game.CloseSessions();

            cli::StringConsole console = Expect(0, { "pic", "list", _gameFolder, "--format", "tsv" });

            std::vector<std::string> lines = LinesOf(console.out);
            Assert::AreEqual((size_t)5, lines.size(), Wide(console.out).c_str());
            Assert::AreEqual(std::string(cli::PicListTsvHeader), lines[0] + "\n");
            const size_t columns = TabFieldsOf(lines[0]).size();
            Assert::AreEqual((size_t)19, columns);
            for (const std::string &line : lines)
            {
                Assert::AreEqual(columns, TabFieldsOf(line).size(), Wide(line).c_str());
            }
            std::vector<std::string> expected = { "100", "vga11", "320", "200", "1", "0", "0", "320", "200", "0", "0", "", "package", std::to_string(data.size()),
                fmt::format("{0:08x}", (uint32_t)crcFast(data.data(), (int)data.size())), "0", "0-255", "64-159", "" };
            Assert::IsTrue(expected == TabFieldsOf(lines[2]), Wide(lines[2]).c_str());
            // Pic 0 has no cel.
            std::vector<std::string> noCel = TabFieldsOf(lines[1]);
            Assert::AreEqual(std::string("0"), noCel[0]);
            Assert::AreEqual(std::string("0"), noCel[4]);
            Assert::AreEqual(std::string(), noCel[6]);
            Assert::AreEqual(std::string(), noCel[7]);
            Assert::AreEqual(std::string(), noCel[17]);
        }

        TEST_METHOD(List_Selectors)
        {
            CopyTemplate();

            cli::StringConsole two = Expect(0, { "pic", "list", _gameFolder, "--format", "tsv", "--pic", "200", "100" });
            std::vector<std::string> lines = LinesOf(two.out);
            Assert::AreEqual((size_t)3, lines.size(), Wide(two.out).c_str());
            Assert::AreEqual(std::string("100"), TabFieldsOf(lines[1])[0]);
            Assert::AreEqual(std::string("200"), TabFieldsOf(lines[2])[0]);

            // A range takes the pics that the game has in it.
            cli::StringConsole range = Expect(0, { "pic", "list", _gameFolder, "--format", "tsv", "--pic", "100-199" });
            lines = LinesOf(range.out);
            Assert::AreEqual((size_t)3, lines.size(), Wide(range.out).c_str());
            Assert::AreEqual(std::string("100"), TabFieldsOf(lines[1])[0]);
            Assert::AreEqual(std::string("110"), TabFieldsOf(lines[2])[0]);

            // A pic that two selectors name comes once.
            cli::StringConsole twice = Expect(0, { "pic", "list", _gameFolder, "--format", "tsv", "--pic", "110", "100-199", "110" });
            lines = LinesOf(twice.out);
            Assert::AreEqual((size_t)3, lines.size(), Wide(twice.out).c_str());
            Assert::AreEqual(std::string("100"), TabFieldsOf(lines[1])[0]);
            Assert::AreEqual(std::string("110"), TabFieldsOf(lines[2])[0]);

            cli::StringConsole unknown = Expect(2, { "pic", "list", _gameFolder, "--pic", "5" });
            Assert::IsTrue(Has(unknown.err, "the game has no pic 5") && unknown.out.empty(), Wide(unknown.out + unknown.err).c_str());
            cli::StringConsole emptyRange = Expect(2, { "pic", "list", _gameFolder, "--pic", "300-400" });
            Assert::IsTrue(Has(emptyRange.err, "the game has no pic in the range 300-400"), Wide(emptyRange.err).c_str());
            for (const char *bad : { "abc", "110-100", "100-", "-100", "1e2", "100000" })
            {
                cli::StringConsole console;
                int code = cli::RunCli({ "pic", "list", _gameFolder, "--pic", bad }, console);
                // "-100" is an option that scic does not have: also a usage error.
                Assert::AreEqual(2, code, Wide(std::string(bad) + ": " + console.err).c_str());
                Assert::IsTrue(console.out.empty(), Wide(std::string(bad) + ": " + console.out).c_str());
            }
            cli::StringConsole notANumber = Expect(2, { "pic", "list", _gameFolder, "--pic", "abc" });
            Assert::IsTrue(Has(notANumber.err, "\"abc\" is not a pic number or a range of pic numbers"), Wide(notANumber.err).c_str());
        }

        // The pic commands do not read include\ or Decompiler\: a data folder
        // with no include\sci.sh stops only a script command.
        TEST_METHOD(PicCommands_NeedNoDataFolder)
        {
            CopyTemplate();
            std::string empty = _outDir + "\\empty-data-folder";
            std::string files = _outDir + "\\files";
            fs::create_directories(empty);
            fs::create_directories(files);

            Expect(0, { "pic", "list", _gameFolder, "--data-dir", empty });
            Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", files, "--data-dir", empty });
            cli::StringConsole script = Expect(3, { "script", "list", _gameFolder, "--data-dir", empty });
            Assert::IsTrue(Has(script.err, "include\\sci.sh"), Wide(script.err).c_str());
        }

        TEST_METHOD(List_WritesNothing)
        {
            CopyTemplate();
            auto before = FilesOf(_gameFolder);

            Expect(0, { "pic", "list", _gameFolder });
            Expect(0, { "pic", "list", _gameFolder, "--format", "tsv" });

            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
        }

        // A pic of an EGA game has no image that scic reads: a row with its
        // format, and no cels.
        TEST_METHOD(List_EgaGame_HasRowsWithNoImage)
        {
            CopyTemplate(TemplateSci0);

            cli::StringConsole console = Expect(0, { "pic", "list", _gameFolder, "--format", "tsv" });

            std::vector<std::string> lines = LinesOf(console.out);
            Assert::IsTrue(lines.size() > 1, Wide(console.out).c_str());
            for (size_t i = 1; i < lines.size(); i++)
            {
                std::vector<std::string> fields = TabFieldsOf(lines[i]);
                Assert::AreEqual(std::string("ega"), fields[1], Wide(lines[i]).c_str());
                Assert::AreEqual(std::string(), fields[2], Wide(lines[i]).c_str());
                Assert::AreEqual(std::string(), fields[4], Wide(lines[i]).c_str());
                Assert::IsFalse(fields[12].empty(), Wide(lines[i]).c_str());
                Assert::IsFalse(fields[13].empty(), Wide(lines[i]).c_str());
                Assert::AreEqual(std::string(), fields[18], Wide(lines[i]).c_str());
            }
            cli::StringConsole text = Expect(0, { "pic", "list", _gameFolder });
            Assert::IsTrue(Has(text.out, "  ega     -        -     -  -"), Wide(text.out).c_str());
        }

        // A patch file is the copy of the pic that the game uses. One with
        // data that is not a pic gets its error in its row, and exit code 6.
        TEST_METHOD(List_PatchFiles_GoodAndUnreadable)
        {
            CopyTemplate();
            std::vector<uint8_t> data = PicDataOf(_game.Open(), 100);
            _game.CloseSessions();
            WritePicPatch(100, data);
            WritePicPatch(110, std::vector<uint8_t>(100, 0xff));

            cli::StringConsole console;
            int code = cli::RunCli({ "pic", "list", _gameFolder, "--format", "tsv" }, console);

            Assert::AreEqual(6, code, Wide(console.out + console.err).c_str());
            std::vector<std::string> lines = LinesOf(console.out);
            Assert::AreEqual((size_t)5, lines.size(), Wide(console.out).c_str());
            std::vector<std::string> good = TabFieldsOf(lines[2]);
            Assert::AreEqual(std::string("patch"), good[12], Wide(lines[2]).c_str());
            Assert::AreEqual(std::string("64-159"), good[17], Wide(lines[2]).c_str());
            Assert::AreEqual(std::string(), good[18], Wide(lines[2]).c_str());
            std::vector<std::string> bad = TabFieldsOf(lines[3]);
            Assert::AreEqual(std::string("110"), bad[0]);
            Assert::AreEqual(std::string("patch"), bad[12], Wide(lines[3]).c_str());
            Assert::AreEqual(std::string("100"), bad[13], Wide(lines[3]).c_str());
            Assert::AreEqual(std::string(), bad[2], Wide(lines[3]).c_str());
            Assert::IsTrue(Has(bad[18], "an SCI1.1 pic has 38") && Has(bad[18], "[format]"), Wide(lines[3]).c_str());
            Assert::AreEqual(std::string("package"), TabFieldsOf(lines[4])[12], Wide(lines[4]).c_str());

            cli::StringConsole text;
            Assert::AreEqual(6, cli::RunCli({ "pic", "list", _gameFolder }, text));
            Assert::IsTrue(Has(text.out, "(unreadable: "), Wide(text.out).c_str());
        }

        TEST_METHOD(List_CtrlC_ExitsWith7)
        {
            CopyTemplate();
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "pic", "list", _gameFolder }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
        }

        // The BMP has the pixels of the cel and the colours that SCI
        // Companion shows; the sidecar has the values of the pic.
        TEST_METHOD(Export_Background_WritesTheBmpAndTheSidecar)
        {
            CopyTemplate();
            GameSession &session = _game.Open();
            std::unique_ptr<ResourceBlob> blob = session.ResourceMap().MostRecentResource(ResourceType::Pic, 100, false);
            Assert::IsNotNull(blob.get(), L"setup: pic 100");
            std::vector<uint8_t> data(blob->GetData(), blob->GetData() + blob->GetLength());
            sci::Result<std::vector<uint8_t>> pixels = GetCelPixels(session.Version(), data, 0);
            AssertOk(pixels, "setup: the pixels of pic 100");
            sci::Result<std::unique_ptr<ResourceEntity>> entity = TryCreateResourceFromResourceData(*blob);
            AssertOk(entity, "setup: pic 100");
            std::unique_ptr<PaletteComponent> palette = session.ResourceMap().GetMergedPalette(**entity, 999);
            _game.CloseSessions();
            auto before = FilesOf(_gameFolder);

            cli::StringConsole console = Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _outDir });

            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
            Assert::IsTrue(Has(console.err, "Exported 1 of 1 pic (1 cel) into " + _outDir + "."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.bmp", "100.toml" }) == FileNamesOf(_outDir), L"the files of the output folder");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");

            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(_outDir + "\\100.bmp");
            AssertOk(image, "the BMP");
            Assert::AreEqual(320, image->width);
            Assert::AreEqual(200, image->height);
            Assert::AreEqual(256, image->colorCount);
            Assert::IsTrue(*pixels == image->pixels, L"the pixels are the bytes of the cel");
            for (size_t i = 0; i < image->colors.size(); i++)
            {
                IndexedBmpColor expected;
                expected.red = palette->Colors[i].rgbRed;
                expected.green = palette->Colors[i].rgbGreen;
                expected.blue = palette->Colors[i].rgbBlue;
                Assert::IsTrue(expected == image->colors[i], Wide("colour " + std::to_string(i)).c_str());
            }
            Assert::IsFalse(image->colors[64] == IndexedBmpColor(), L"a colour of the pic is not black");

            sci::Result<toml::table> sidecar = ParseTomlFile(_outDir + "\\100.toml");
            AssertOk(sidecar, "the sidecar");
            const toml::table &table = *sidecar;
            Assert::AreEqual((int64_t)100, table["source"]["pic"].value_or((int64_t)-1));
            Assert::AreEqual(std::string("vga11"), table["source"]["format"].value_or(std::string()));
            Assert::AreEqual(std::string("package"), table["source"]["from"].value_or(std::string()));
            Assert::AreEqual((int64_t)data.size(), table["source"]["size"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)(uint32_t)crcFast(data.data(), (int)data.size()), table["source"]["crc32"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)320, table["pic"]["width"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)200, table["pic"]["height"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)0, table["pic"]["background_cel"].value_or((int64_t)-1));
            Assert::IsTrue(table["palette"]["present"].value_or(false));
            Assert::AreEqual((int64_t)0, table["palette"]["stored_first"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)256, table["palette"]["stored_count"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)4, table["palette"]["entry_size"].value_or((int64_t)-1));
            Assert::AreEqual(std::string("0-63-1,64-159-3,160-255-1"), RunsOf(table["palette"]["flags"]));
            Assert::AreEqual(std::string("0-63,253-255"), RunsOf(table["palette"]["palette_999"]));
            Assert::AreEqual(std::string("0-255"), RunsOf(table["palette"]["used"]));
            const toml::array *cels = table["cel"].as_array();
            Assert::IsNotNull(cels, L"the cels");
            Assert::AreEqual((size_t)1, cels->size());
            toml::node_view<const toml::node> cel = table["cel"][0];
            Assert::AreEqual((int64_t)0, cel["index"].value_or((int64_t)-1));
            Assert::AreEqual(std::string("100.bmp"), cel["file"].value_or(std::string()));
            Assert::AreEqual((int64_t)320, cel["width"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)200, cel["height"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)0, cel["x"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)0, cel["y"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)255, cel["skip_color"].value_or((int64_t)-1));
            Assert::IsFalse(cel["compressed"].value_or(true));
            Assert::AreEqual(std::string("64-159"), RunsOf(cel["entries"]));
            Assert::AreEqual((int64_t)0, cel["skip_pixels"].value_or((int64_t)-1));
            Assert::IsFalse((bool)cel["priority"], L"an SCI1.1 cel has no priority");
        }

        // A patch file of pic 100 whose palette does not use the entries 0 to
        // 63 (their flag byte is 0), and has another colour for them. The
        // game takes those entries from palette 999, and so does the table
        // of the BMP.
        TEST_METHOD(Export_TableHasPalette999ForTheEntriesThatThePicDoesNotUse)
        {
            CopyTemplate();
            GameSession &session = _game.Open();
            std::vector<uint8_t> data = PicDataOf(session, 100);
            const PaletteComponent *palette999 = session.ResourceMap().GetPalette999();
            Assert::IsNotNull(palette999, L"setup: palette 999 of the template");
            std::vector<RGBQUAD> global(palette999->Colors, palette999->Colors + 256);
            _game.CloseSessions();
            sci::Result<uint32_t> paletteOffset = ReadPicPaletteOffsetVGA11(data.data(), data.size());
            AssertOk(paletteOffset, "setup: the palette of pic 100");
            sci::Result<PaletteLayout> layout = ReadPaletteLayout(data.data(), data.size(), *paletteOffset);
            AssertOk(layout, "setup: the palette of pic 100");
            Assert::AreEqual(4, layout->entrySize);
            for (size_t entry = 0; entry < 64; entry++)
            {
                uint8_t *bytes = data.data() + layout->entriesOffset + (entry * 4);
                bytes[0] = 0;
                bytes[1] = 1;
                bytes[2] = 2;
                bytes[3] = 3;
            }
            WritePicPatch(100, data);

            Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _outDir });

            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(_outDir + "\\100.bmp");
            AssertOk(image, "the BMP");
            for (size_t entry = 0; entry < 64; entry++)
            {
                IndexedBmpColor expected;
                expected.red = global[entry].rgbRed;
                expected.green = global[entry].rgbGreen;
                expected.blue = global[entry].rgbBlue;
                Assert::IsTrue(expected == image->colors[entry], Wide("colour " + std::to_string(entry) + " is that of palette 999").c_str());
            }
            Assert::IsFalse(image->colors[5] == IndexedBmpColor{ 1, 2, 3 }, L"setup: palette 999 has another colour than the patch");
            sci::Result<toml::table> sidecar = ParseTomlFile(_outDir + "\\100.toml");
            AssertOk(sidecar, "the sidecar");
            const toml::table &table = *sidecar;
            Assert::AreEqual(std::string("patch"), table["source"]["from"].value_or(std::string()));
            Assert::AreEqual(std::string("0-63-0,64-159-3,160-255-1"), RunsOf(table["palette"]["flags"]));
            Assert::AreEqual(std::string("0-63,253-255"), RunsOf(table["palette"]["palette_999"]));
            Assert::AreEqual(std::string("0-255"), RunsOf(table["palette"]["used"]));
        }

        // With --all, a pic with no cel (pic 0) is skipped and counted.
        TEST_METHOD(Export_All_SkipsAPicWithNoCel)
        {
            CopyTemplate();

            cli::StringConsole console = Expect(0, { "pic", "export", _gameFolder, "--all", "--cel", "all", "--out-dir", _outDir, "-v" });

            Assert::IsTrue(std::vector<std::string>({ "100.bmp", "100.toml", "110.bmp", "110.toml", "200.bmp", "200.toml" }) == FileNamesOf(_outDir), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Exported 3 of 4 pics (3 cels) into " + _outDir + "; 1 pic has no cel."), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "pic 0 has no cel: skipped"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote " + _outDir + "\\110.bmp"), Wide(console.err).c_str());

            // A second export gives the same bytes.
            auto first = FilesOf(_outDir);
            Expect(0, { "pic", "export", _gameFolder, "--all", "--out-dir", _outDir });
            Assert::IsTrue(first == FilesOf(_outDir), L"the second export writes the same files");
        }

        TEST_METHOD(Export_NamedPicWithNoCel_ExitsWith6)
        {
            CopyTemplate();

            cli::StringConsole console = Expect(6, { "pic", "export", _gameFolder, "--pic", "0", "100", "--out-dir", _outDir });

            Assert::IsTrue(Has(console.err, "scic: error: pic 0: the pic has no cel to export"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Exported 1 of 2 pics (1 cel) into " + _outDir + "; 1 pic failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.bmp", "100.toml" }) == FileNamesOf(_outDir), L"the pic that did not fail is exported");
        }

        TEST_METHOD(Export_DryRun_WritesNothing)
        {
            CopyTemplate();
            auto before = FilesOf(_gameFolder);

            cli::StringConsole console = Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _outDir, "--dry-run" });

            Assert::IsTrue(FileNamesOf(_outDir).empty(), L"the output folder stays empty");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
            Assert::IsTrue(Has(console.err, "Would write " + _outDir + "\\100.bmp"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Would write " + _outDir + "\\100.toml"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "A run would export 1 of 1 pic (1 cel) into " + _outDir + "."), Wide(console.err).c_str());
        }

        TEST_METHOD(Export_UsageErrors)
        {
            CopyTemplate();
            auto before = FilesOf(_gameFolder);

            cli::StringConsole noOutDir = Expect(2, { "pic", "export", _gameFolder, "--pic", "100" });
            Assert::IsTrue(Has(noOutDir.err, "--out-dir"), Wide(noOutDir.err).c_str());
            cli::StringConsole emptyOutDir = Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", "" });
            Assert::IsTrue(Has(emptyOutDir.err, "--out-dir needs a folder"), Wide(emptyOutDir.err).c_str());
            cli::StringConsole both = Expect(2, { "pic", "export", _gameFolder, "--all", "--pic", "100", "--out-dir", _outDir });
            Assert::IsTrue(Has(both.err, "give --all or --pic, not both"), Wide(both.err).c_str());
            cli::StringConsole neither = Expect(2, { "pic", "export", _gameFolder, "--out-dir", _outDir });
            Assert::IsTrue(Has(neither.err, "give --all, or --pic with one or more pics"), Wide(neither.err).c_str());
            Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--cel", "some", "--out-dir", _outDir });
            cli::StringConsole unknown = Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "5", "--out-dir", _outDir });
            Assert::IsTrue(Has(unknown.err, "the game has no pic 5"), Wide(unknown.err).c_str());
            cli::StringConsole gameFolder = Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _gameFolder });
            Assert::IsTrue(Has(gameFolder.err, "the output folder is the game folder"), Wide(gameFolder.err).c_str());
            // Another name of the game folder.
            Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _gameFolder + "\\src\\.." });

            // Plan section 8: a folder that is missing is exit code 3.
            cli::StringConsole missing = Expect(3, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _outDir + "\\no-such-folder" });
            Assert::IsTrue(Has(missing.err, "the output folder does not exist"), Wide(missing.err).c_str());
            Expect(3, { "pic", "export", _gameFolder + "\\NoSuchGame", "--pic", "100", "--out-dir", _outDir });

            Assert::IsTrue(FileNamesOf(_outDir).empty(), L"no file is written");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
        }

        // The image of an EGA pic is not cels: each pic fails, and the exit
        // code is 6.
        TEST_METHOD(Export_EgaGame_ExitsWith6)
        {
            CopyTemplate(TemplateSci0);

            cli::StringConsole console = Expect(6, { "pic", "export", _gameFolder, "--all", "--out-dir", _outDir });

            Assert::IsTrue(Has(console.err, "the format ega") && Has(console.err, "[unsupported]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Exported 0 of "), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_outDir).empty(), L"no file is written");
        }

        // Each pic has its own exception boundary: a pic that cannot be read
        // does not stop the others.
        TEST_METHOD(Export_OnePicFails_TheOthersAreExported)
        {
            CopyTemplate();
            WritePicPatch(110, std::vector<uint8_t>(100, 0xff));

            cli::StringConsole console = Expect(6, { "pic", "export", _gameFolder, "--all", "--out-dir", _outDir });

            Assert::IsTrue(Has(console.err, "scic: error: pic 110: ") && Has(console.err, "[format]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Exported 2 of 4 pics (2 cels) into " + _outDir + "; 1 pic has no cel; 1 pic failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.bmp", "100.toml", "200.bmp", "200.toml" }) == FileNamesOf(_outDir), L"the other pics are exported");
        }

        // Plan section 8: a file that cannot be written is exit code 9, also
        // in a dry run.
        TEST_METHOD(Export_FileThatCannotBeWritten_ExitsWith9)
        {
            CopyTemplate();
            WriteReadOnlyFile(_outDir + "\\100.toml", "read-only");

            cli::StringConsole dryRun = Expect(9, { "pic", "export", _gameFolder, "--pic", "100", "200", "--out-dir", _outDir, "--dry-run" });
            Assert::IsTrue(Has(dryRun.err, "scic: error: pic 100: ") && Has(dryRun.err, "100.toml"), Wide(dryRun.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.toml" }) == FileNamesOf(_outDir), L"a dry run writes nothing");

            cli::StringConsole console = Expect(9, { "pic", "export", _gameFolder, "--pic", "100", "200", "--out-dir", _outDir });
            Assert::IsTrue(Has(console.err, "scic: error: pic 100: ") && Has(console.err, "100.toml"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Exported 1 of 2 pics (1 cel) into " + _outDir + "; 1 pic failed."), Wide(console.err).c_str());
            Assert::AreEqual(std::string("read-only"), ReadFileText(_outDir + "\\100.toml"));
            // No file of the pic that failed, and each file of the other pic.
            Assert::IsTrue(std::vector<std::string>({ "100.toml", "200.bmp", "200.toml" }) == FileNamesOf(_outDir), L"the files of the output folder");
        }

        TEST_METHOD(Export_CtrlC_ExitsWith7)
        {
            CopyTemplate();
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "pic", "export", _gameFolder, "--all", "--out-dir", _outDir }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(Has(console.err, "stopped by Ctrl+C"), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(_outDir).empty(), L"no file is written");
        }

        TEST_METHOD(Help_ListsThePicCommands)
        {
            cli::StringConsole group = Expect(0, { "pic" });
            Assert::IsTrue(Has(group.out, "list") && Has(group.out, "export"), Wide(group.out).c_str());
            cli::StringConsole helpGroup = Expect(0, { "help", "pic" });
            Assert::IsTrue(Has(helpGroup.out, "list") && Has(helpGroup.out, "export"), Wide(helpGroup.out).c_str());
            cli::StringConsole helpExport = Expect(0, { "help", "pic", "export" });
            Assert::IsTrue(Has(helpExport.out, "--out-dir") && Has(helpExport.out, "--cel") && Has(helpExport.out, "scic pic export"), Wide(helpExport.out).c_str());
            cli::StringConsole helpList = Expect(0, { "pic", "list", "--help" });
            Assert::IsTrue(Has(helpList.out, "--format") && Has(helpList.out, "scic pic list"), Wide(helpList.out).c_str());
            cli::StringConsole root = Expect(0, { "help" });
            Assert::IsTrue(Has(root.out, "pic list, pic export"), Wide(root.out).c_str());
        }
    };

    // The .toml file of scic pic export (PicSidecar.h).
    TEST_CLASS(TestPicSidecar)
    {
    public:
        // The text is TOML that gives the values back.
        TEST_METHOD(Text_IsTomlWithTheValues)
        {
            PicSidecar sidecar;
            sidecar.pic = 230;
            sidecar.format = "vga2";
            sidecar.from = "patch";
            sidecar.size = 252588;
            sidecar.crc32 = 0xdeadbeef;
            sidecar.width = 640;
            sidecar.height = 480;
            sidecar.backgroundCel = 1;
            sidecar.hasPalette = true;
            sidecar.storedFirst = 104;
            sidecar.storedCount = 132;
            sidecar.entrySize = 3;
            sidecar.flags = { { 104, 200, 1 }, { 201, 235, 0 } };
            sidecar.palette999 = { { 0, 79 } };
            sidecar.used = { { 0, 79 }, { 104, 200 } };
            PicSidecarCel overlay;
            overlay.index = 0;
            overlay.width = 46;
            overlay.height = 74;
            overlay.x = 113;
            overlay.y = -5;
            overlay.priority = -1000;
            overlay.skipColor = 255;
            overlay.compressed = true;
            overlay.skipPixels = 77;
            PicSidecarCel background;
            background.index = 1;
            background.file = "230.bmp";
            background.width = 640;
            background.height = 334;
            background.priority = 40;
            background.skipColor = 250;
            background.entries = { { 0, 12 }, { 80, 80 }, { 104, 235 } };
            sidecar.cels = { overlay, background };

            std::string text = PicSidecarText(sidecar);

            toml::table table;
            try
            {
                table = toml::parse(text);
            }
            catch (const toml::parse_error &error)
            {
                Assert::Fail(Wide(std::string(error.description()) + "\n" + text).c_str());
            }
            Assert::AreEqual((int64_t)230, table["source"]["pic"].value_or((int64_t)-1), Wide(text).c_str());
            Assert::AreEqual(std::string("vga2"), table["source"]["format"].value_or(std::string()));
            Assert::AreEqual(std::string("patch"), table["source"]["from"].value_or(std::string()));
            Assert::AreEqual((int64_t)252588, table["source"]["size"].value_or((int64_t)-1));
            // More than the largest 32-bit value with a sign: the value keeps
            // its 32 bits with no sign.
            Assert::AreEqual((int64_t)0xdeadbeef, table["source"]["crc32"].value_or((int64_t)-1));
            Assert::IsTrue(Has(text, "crc32 = 0xDEADBEEF"), Wide(text).c_str());
            Assert::AreEqual((int64_t)640, table["pic"]["width"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)480, table["pic"]["height"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)1, table["pic"]["background_cel"].value_or((int64_t)-1));
            Assert::IsTrue(table["palette"]["present"].value_or(false));
            Assert::AreEqual((int64_t)104, table["palette"]["stored_first"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)132, table["palette"]["stored_count"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)3, table["palette"]["entry_size"].value_or((int64_t)-1));
            const toml::table &constTable = table;
            Assert::AreEqual(std::string("104-200-1,201-235-0"), RunsOf(constTable["palette"]["flags"]));
            Assert::AreEqual(std::string("0-79"), RunsOf(constTable["palette"]["palette_999"]));
            Assert::AreEqual(std::string("0-79,104-200"), RunsOf(constTable["palette"]["used"]));
            const toml::array *cels = table["cel"].as_array();
            Assert::IsNotNull(cels, L"the cels");
            Assert::AreEqual((size_t)2, cels->size());
            toml::node_view<const toml::node> first = constTable["cel"][0];
            Assert::AreEqual((int64_t)0, first["index"].value_or((int64_t)-1));
            Assert::IsFalse((bool)first["file"], L"a cel with no file has no file key");
            Assert::AreEqual((int64_t)46, first["width"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)74, first["height"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)113, first["x"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)-5, first["y"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)-1000, first["priority"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)255, first["skip_color"].value_or((int64_t)-1));
            Assert::IsTrue(first["compressed"].value_or(false));
            Assert::AreEqual(std::string(), RunsOf(first["entries"]));
            Assert::AreEqual((int64_t)77, first["skip_pixels"].value_or((int64_t)-1));
            toml::node_view<const toml::node> second = constTable["cel"][1];
            Assert::AreEqual((int64_t)1, second["index"].value_or((int64_t)-1));
            Assert::AreEqual(std::string("230.bmp"), second["file"].value_or(std::string()));
            Assert::AreEqual((int64_t)40, second["priority"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)250, second["skip_color"].value_or((int64_t)-1));
            Assert::IsFalse(second["compressed"].value_or(true));
            Assert::AreEqual(std::string("0-12,80-80,104-235"), RunsOf(second["entries"]));
        }

        // A pic with no palette and no cel is still TOML.
        TEST_METHOD(Text_PicWithNoPaletteAndNoCel)
        {
            PicSidecar sidecar;
            sidecar.pic = 7;
            sidecar.format = "vga11";

            std::string text = PicSidecarText(sidecar);

            toml::table table;
            try
            {
                table = toml::parse(text);
            }
            catch (const toml::parse_error &error)
            {
                Assert::Fail(Wide(std::string(error.description()) + "\n" + text).c_str());
            }
            Assert::AreEqual((int64_t)7, table["source"]["pic"].value_or((int64_t)-1), Wide(text).c_str());
            Assert::AreEqual((int64_t)-1, table["pic"]["background_cel"].value_or((int64_t)0));
            Assert::IsFalse(table["palette"]["present"].value_or(true));
            const toml::array *cels = table["cel"].as_array();
            Assert::IsNotNull(cels, L"the cels");
            Assert::IsTrue(cels->empty());
        }

        // The description of pic 220 of the pic tests: 4 cels, and a palette
        // of 3-byte entries 0 to 254.
        TEST_METHOD(Make_TakesTheValuesOfTheDescription)
        {
            ResourceBlob blob;
            std::string path = GetTestFileDirectory("Pics") + "\\SCI2\\220.p56";
            Assert::IsTrue(SUCCEEDED(blob.CreateFromFile("pic", path, sciVersion2, ResourceSaveLocation::Patch, sciVersion2.DefaultVolumeFile)), L"setup: pic 220");
            std::vector<uint8_t> data(blob.GetData(), blob.GetData() + blob.GetLength());
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe pic 220");

            PicSidecar sidecar = MakePicSidecar(*description);

            Assert::AreEqual(std::string("vga2"), sidecar.format);
            Assert::AreEqual(640, sidecar.width);
            Assert::AreEqual(480, sidecar.height);
            Assert::AreEqual(0, sidecar.backgroundCel);
            Assert::IsTrue(sidecar.hasPalette);
            Assert::AreEqual(0, sidecar.storedFirst);
            Assert::AreEqual(255, sidecar.storedCount);
            Assert::AreEqual(3, sidecar.entrySize);
            Assert::AreEqual((size_t)1, sidecar.flags.size());
            Assert::AreEqual(0, sidecar.flags[0].first);
            Assert::AreEqual(254, sidecar.flags[0].last);
            Assert::AreEqual(1, sidecar.flags[0].flag);
            Assert::AreEqual((size_t)4, sidecar.cels.size());
            Assert::AreEqual(1, sidecar.cels[1].index);
            Assert::AreEqual(46, sidecar.cels[1].width);
            Assert::AreEqual(74, sidecar.cels[1].height);
            Assert::AreEqual(113, sidecar.cels[1].x);
            Assert::AreEqual(40, sidecar.cels[1].priority);
            Assert::AreEqual(255, sidecar.cels[1].skipColor);
            Assert::IsTrue(sidecar.cels[1].file.empty());
            // The entries of the cel do not have its skip colour, and the
            // count of its skip pixels is that of the description.
            const PicCelDescription &cel = description->cels[1];
            Assert::IsTrue(cel.SkipPixels() > 0, L"setup: cel 1 has pixels of the skip colour");
            Assert::AreEqual(cel.SkipPixels(), sidecar.cels[1].skipPixels);
            std::array<bool, 256> entries = cel.UsedEntries();
            entries[255] = false;
            Assert::IsTrue(IndexRunsOf(entries) == sidecar.cels[1].entries);
            Assert::IsTrue(sidecar.cels[1].entries.back().last < 255);
        }
    };
}
