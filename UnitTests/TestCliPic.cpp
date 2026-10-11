#include "stdafx.h"
#include "CppUnitTest.h"
#include "Cli.h"
#include "CliCommands.h"
#include "CliConsole.h"
#include "CliHost.h"
#include "CliPic.h"
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
#include <algorithm>
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

        // Writes a patch file of pic 100 whose palette does not use the
        // entries first to last (their flag byte is 0). Palette 999 of the
        // template uses the entries 0 to 63 and 253 to 255.
        void WritePic100WithUnusedEntries(int first, int last)
        {
            std::vector<uint8_t> data = PicDataOf(_game.Open(), 100);
            _game.CloseSessions();
            sci::Result<uint32_t> paletteOffset = ReadPicPaletteOffsetVGA11(data.data(), data.size());
            AssertOk(paletteOffset, "setup: the palette of pic 100");
            sci::Result<PaletteLayout> layout = ReadPaletteLayout(data.data(), data.size(), *paletteOffset);
            AssertOk(layout, "setup: the palette of pic 100");
            Assert::AreEqual(4, layout->entrySize);
            for (int entry = first; entry <= last; entry++)
            {
                data[layout->entriesOffset + ((size_t)entry * 4)] = 0;
            }
            WritePicPatch(100, data);
        }

        // Copies the patch files of pics of a folder into the game copy.
        void CopyPicPatchesToGame(const std::string &folder)
        {
            for (const auto &entry : fs::directory_iterator(folder))
            {
                if (entry.path().extension() == ".p56")
                {
                    fs::copy_file(entry.path(), fs::path(_gameFolder) / entry.path().filename(), fs::copy_options::overwrite_existing);
                }
            }
        }

        // A new folder in the output folder of the test.
        std::string NewFolder(const std::string &name)
        {
            std::string folder = _outDir + "\\" + name;
            fs::create_directories(folder);
            return folder;
        }

        // The image of a BMP file of the test.
        IndexedBmp ReadBmp(const std::string &path)
        {
            sci::Result<IndexedBmp> image = ReadIndexedBmpFile(path);
            AssertOk(image, path);
            return *image;
        }

        // Exports pic 100 into a new folder, and gives the path of its BMP.
        std::string ExportPic100(const std::string &folderName = "exported")
        {
            std::string folder = NewFolder(folderName);
            Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", folder });
            return folder + "\\100.bmp";
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
            // The names and the order of the columns (plan section 4.7).
            Assert::AreEqual(std::string("number\tformat\twidth\theight\tcels\tcompressed_cels\tbackground_cel\tbackground_width\tbackground_height\tbackground_x\tbackground_y\tbackground_priority\t"
                "source\tbytes\tcrc32\tskip_pixels\tpalette_entries\tpixel_entries\terror"), lines[0]);
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
            // Zeros before a number do not change it.
            cli::StringConsole zeros = Expect(0, { "pic", "list", _gameFolder, "--format", "tsv", "--pic", "000100", "0110-000200" });
            lines = LinesOf(zeros.out);
            Assert::AreEqual((size_t)4, lines.size(), Wide(zeros.out).c_str());
            Assert::AreEqual(std::string("100"), TabFieldsOf(lines[1])[0]);
            Assert::AreEqual(std::string("200"), TabFieldsOf(lines[3])[0]);

            for (const char *bad : { "abc", "110-100", "100-", "-100", "1e2", "65536", "100000", "99999999999999999999", "+100", " 100" })
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
            // Other names of the game folder: through a folder of it, with a
            // separator at the end, and in upper case.
            cli::StringConsole otherName = Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _gameFolder + "\\src\\.." });
            Assert::IsTrue(Has(otherName.err, "the output folder is the game folder"), Wide(otherName.err).c_str());
            Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _gameFolder + "\\" });
            Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", Upper(_gameFolder) });
            // A folder inside the game folder, at any depth.
            fs::create_directories(_gameFolder + "\\src\\pics");
            before = FilesOf(_gameFolder);
            for (const char *inside : { "\\src", "\\src\\pics", "\\SRC\\Pics" })
            {
                cli::StringConsole console = Expect(2, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", _gameFolder + inside });
                Assert::IsTrue(Has(console.err, "the output folder is inside the game folder"), Wide(std::string(inside) + ": " + console.err).c_str());
            }
            // The folder that holds the game folder is not in the game.
            std::string parent = fs::path(_gameFolder).parent_path().string();
            cli::StringConsole outside = Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", parent, "--dry-run" });
            Assert::IsTrue(Has(outside.err, "A run would export 1 of 1 pic"), Wide(outside.err).c_str());

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

        // Each pic that export wrote goes back with no change. The patch
        // files, put into the game, give the same BMP files again.
        TEST_METHOD(Import_InDir_WithNoChange_GivesTheSameImagesAgain)
        {
            CopyTemplate();
            std::string exported = NewFolder("exported");
            std::string patches = NewFolder("patches");
            std::string again = NewFolder("again");
            Expect(0, { "pic", "export", _gameFolder, "--all", "--out-dir", exported });
            auto before = FilesOf(_gameFolder);

            cli::StringConsole console = Expect(0, { "pic", "import", _gameFolder, "--in-dir", exported, "--out-dir", patches, "-v" });

            Assert::IsTrue(console.out.empty(), Wide(console.out).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.p56", "110.p56", "200.p56" }) == FileNamesOf(patches), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Imported 3 of 3 pics (3 cels) into " + patches + "."), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Wrote " + patches + "\\110.p56"), Wide(console.err).c_str());
            Assert::IsFalse(Has(console.err, "warning"), Wide(console.err).c_str());
            Assert::IsTrue(before == FilesOf(_gameFolder), L"import writes nothing into the game");

            CopyPicPatchesToGame(patches);
            cli::StringConsole list = Expect(0, { "pic", "list", _gameFolder, "--format", "tsv" });
            std::vector<std::string> lines = LinesOf(list.out);
            Assert::AreEqual((size_t)5, lines.size(), Wide(list.out).c_str());
            Assert::AreEqual(std::string("package"), TabFieldsOf(lines[1])[12], Wide(lines[1]).c_str());
            for (size_t i = 2; i < lines.size(); i++)
            {
                Assert::AreEqual(std::string("patch"), TabFieldsOf(lines[i])[12], Wide(lines[i]).c_str());
            }
            Expect(0, { "pic", "export", _gameFolder, "--all", "--out-dir", again });
            for (const char *name : { "100.bmp", "110.bmp", "200.bmp" })
            {
                Assert::IsTrue(ReadFileBytes(exported + "\\" + name) == ReadFileBytes(again + "\\" + name), Wide(std::string(name) + " is the same after the import").c_str());
            }
        }

        // New pixels (two entries of the image, swapped) come back exactly
        // from the game with the patch file, and the palette stays. The
        // patch file has the header of a pic patch: 81 80, and 24 bytes of 0.
        TEST_METHOD(Import_OneImage_ChangedPixelsSurviveExactly)
        {
            CopyTemplate();
            IndexedBmp original = ReadBmp(ExportPic100());
            IndexedBmp changed = original;
            const uint8_t one = changed.pixels[0];
            const uint8_t other = *std::find_if(changed.pixels.begin(), changed.pixels.end(), [one](uint8_t pixel) { return pixel != one; });
            for (uint8_t &pixel : changed.pixels)
            {
                pixel = (pixel == one) ? other : ((pixel == other) ? one : pixel);
            }
            Assert::IsFalse(original.pixels == changed.pixels, L"setup: the image changes");
            std::string image = _outDir + "\\changed.bmp";
            AssertOk(WriteIndexedBmpFile(image, changed), "setup: the changed image");
            std::string patches = NewFolder("patches");
            auto before = FilesOf(_gameFolder);

            cli::StringConsole console = Expect(0, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", patches });

            Assert::IsTrue(Has(console.err, "Imported 1 of 1 pic (1 cel) into " + patches + "."), Wide(console.err).c_str());
            // One image has no CRC-32 to compare with the pic.
            Assert::IsFalse(Has(console.err, "warning"), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.p56" }) == FileNamesOf(patches), L"the patch file");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"import writes nothing into the game");
            std::vector<uint8_t> patch = ReadFileBytes(patches + "\\100.p56");
            Assert::IsTrue(patch.size() > 26, L"the patch file has a header and data");
            Assert::AreEqual(0x81, (int)patch[0]);
            Assert::AreEqual(0x80, (int)patch[1]);
            Assert::IsTrue(std::all_of(patch.begin() + 2, patch.begin() + 26, [](uint8_t byte) { return byte == 0; }), L"24 bytes of 0 after the first two");

            CopyPicPatchesToGame(patches);
            IndexedBmp again = ReadBmp(ExportPic100("again"));
            Assert::IsTrue(changed.pixels == again.pixels, L"the new pixels");
            Assert::IsTrue(original.colors == again.colors, L"the palette stays");
        }

        // --palette replace takes the colours of the range from the colour
        // table of the image, and no other colour.
        TEST_METHOD(Import_PaletteReplace_TakesTheColoursOfTheRangeFromTheImage)
        {
            CopyTemplate();
            IndexedBmp original = ReadBmp(ExportPic100());
            IndexedBmp changed = original;
            for (size_t entry : { (size_t)10, (size_t)64, (size_t)65, (size_t)70, (size_t)71 })
            {
                changed.colors[entry] = IndexedBmpColor{ (uint8_t)entry, (uint8_t)(255 - entry), 77 };
                Assert::IsFalse(original.colors[entry] == changed.colors[entry], L"setup: a new colour");
            }
            std::string image = _outDir + "\\changed.bmp";
            AssertOk(WriteIndexedBmpFile(image, changed), "setup: the changed image");
            std::string patches = NewFolder("patches");

            Expect(0, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--palette", "replace", "--palette-range", "64-70", "--out-dir", patches });

            CopyPicPatchesToGame(patches);
            IndexedBmp again = ReadBmp(ExportPic100("again"));
            Assert::IsTrue(original.pixels == again.pixels, L"the pixels stay");
            for (size_t entry = 0; entry < 256; entry++)
            {
                const bool inRange = (entry >= 64) && (entry <= 70);
                Assert::IsTrue((inRange ? changed.colors[entry] : original.colors[entry]) == again.colors[entry], Wide("colour " + std::to_string(entry)).c_str());
            }

            // --palette keep (the default) takes no colour of the image.
            std::string kept = NewFolder("kept");
            Expect(0, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", kept });
            CopyPicPatchesToGame(kept);
            IndexedBmp keptImage = ReadBmp(ExportPic100("kept-again"));
            Assert::IsTrue(again.colors == keptImage.colors, L"keep does not change the palette of the pic in the game");
        }

        // One case for each check of an image: the pic fails (exit code 6),
        // and no patch file is written.
        TEST_METHOD(Import_ImageThatFailsACheck_ExitsWith6)
        {
            CopyTemplate();
            IndexedBmp original = ReadBmp(ExportPic100());
            std::string patches = NewFolder("patches");
            std::string image = _outDir + "\\image.bmp";
            auto import = [&](const std::vector<std::string> &more) -> cli::StringConsole
            {
                std::vector<std::string> args = { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", patches };
                args.insert(args.end(), more.begin(), more.end());
                cli::StringConsole console = Expect(6, args);
                Assert::IsTrue(FileNamesOf(patches).empty(), Wide("no patch file is written:\n" + console.err).c_str());
                Assert::IsTrue(Has(console.err, "Imported 0 of 1 pic (0 cels) into " + patches + "; 1 pic failed."), Wide(console.err).c_str());
                return console;
            };

            // Another size than the cel.
            IndexedBmp narrow = original;
            narrow.width = 319;
            narrow.pixels.resize(319 * 200);
            AssertOk(WriteIndexedBmpFile(image, narrow), "setup: a narrow image");
            cli::StringConsole size = import({});
            Assert::IsTrue(Has(size.err, "scic: error: pic 100: ") && Has(size.err, "has 319 x 200 pixels, and cel 0 has 320 x 200"), Wide(size.err).c_str());

            // A 24-bit BMP of 2 x 2 pixels.
            std::vector<uint8_t> trueColour = { 'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0 };
            trueColour.resize(70, 0);
            WriteFileBytes(image, trueColour);
            cli::StringConsole bits = import({});
            Assert::IsTrue(Has(bits.err, "a 24-bit BMP file") && Has(bits.err, "[unsupported]"), Wide(bits.err).c_str());

            // A file that is not a BMP file, and no file.
            WriteFileText(image, "not an image");
            cli::StringConsole notBmp = import({});
            Assert::IsTrue(Has(notBmp.err, "not a BMP file") && Has(notBmp.err, "[format]"), Wide(notBmp.err).c_str());
            fs::remove(image);
            cli::StringConsole noFile = import({});
            Assert::IsTrue(Has(noFile.err, "[not-found]"), Wide(noFile.err).c_str());

            // A pixel gets the skip colour of the cel (255; the cel has no
            // pixel of it).
            IndexedBmp hole = original;
            hole.pixels[(3 * 320) + 5] = 255;
            AssertOk(WriteIndexedBmpFile(image, hole), "setup: an image with a new skip pixel");
            cli::StringConsole skip = import({});
            Assert::IsTrue(Has(skip.err, "1 pixel changes to or from the skip colour 255 of cel 0 (the first at 5,3)"), Wide(skip.err).c_str());

            // A cel that the pic does not have.
            AssertOk(WriteIndexedBmpFile(image, original), "setup: the image");
            cli::StringConsole cel = import({ "--cel", "1" });
            Assert::IsTrue(Has(cel.err, "the pic has no cel 1; it has 1 cel"), Wide(cel.err).c_str());

            // A colour table of 64 entries, and a range that goes to 70.
            std::vector<uint8_t> bytes = ReadFileBytes(image);
            Assert::AreEqual(1, (int)bytes[47], L"setup: biClrUsed is 256 (00 01 00 00)");
            bytes[46] = 64;
            bytes[47] = 0;
            WriteFileBytes(image, bytes);
            cli::StringConsole table = import({ "--palette", "replace", "--palette-range", "60-70" });
            Assert::IsTrue(Has(table.err, "the colour table has 64 entries, and the palette range is 60-70"), Wide(table.err).c_str());
        }

        // Pic 100 with the entries 200 to 210 not in use: a pixel of them is
        // an error, or a warning with --allow-unused-indices.
        TEST_METHOD(Import_UnusedEntry_IsAnErrorOrAWarning)
        {
            CopyTemplate();
            WritePic100WithUnusedEntries(200, 210);
            // Entry 5 is then in use only in palette 999.
            WritePic100WithUnusedEntries(5, 5);
            IndexedBmp image = ReadBmp(ExportPic100());
            image.pixels[0] = 205;
            image.pixels[1] = 205;
            image.pixels[2] = 201;
            // An entry that only the pic marks as used, and one that only
            // palette 999 marks as used.
            image.pixels[3] = 199;
            image.pixels[4] = 5;
            std::string path = _outDir + "\\changed.bmp";
            AssertOk(WriteIndexedBmpFile(path, image), "setup: the changed image");
            std::string patches = NewFolder("patches");

            cli::StringConsole refused = Expect(6, { "pic", "import", _gameFolder, "--pic", "100", "--image", path, "--out-dir", patches });
            Assert::IsTrue(Has(refused.err, "3 pixels of cel 0 have palette entries that the pic and palette 999 do not mark as used: 201,205 (--allow-unused-indices makes this a warning)"), Wide(refused.err).c_str());
            Assert::IsTrue(FileNamesOf(patches).empty(), L"no patch file is written");

            cli::StringConsole allowed = Expect(0, { "pic", "import", _gameFolder, "--pic", "100", "--image", path, "--allow-unused-indices", "--out-dir", patches });
            Assert::IsTrue(Has(allowed.err, "scic: warning: pic 100: ") && Has(allowed.err, "do not mark as used: 201,205"), Wide(allowed.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.p56" }) == FileNamesOf(patches), L"the patch file is written");
        }

        TEST_METHOD(Import_UsageErrors)
        {
            CopyTemplate();
            std::string image = ExportPic100();
            std::string exported = fs::path(image).parent_path().string();
            std::string patches = NewFolder("patches");
            std::string empty = NewFolder("empty");
            auto before = FilesOf(_gameFolder);
            auto usage = [&](const std::vector<std::string> &options, const std::string &text)
            {
                std::vector<std::string> args = { "pic", "import", _gameFolder };
                args.insert(args.end(), options.begin(), options.end());
                cli::StringConsole console = Expect(2, args);
                Assert::IsTrue(Has(console.err, text), Wide(text + ":\n" + console.err).c_str());
            };

            usage({ "--pic", "100", "--image", image, "--palette-range", "64-70", "--out-dir", patches }, "--palette-range needs --palette replace");
            usage({ "--pic", "100", "--image", image, "--palette", "replace", "--out-dir", patches }, "--palette replace needs --palette-range");
            for (const char *range : { "64", "70-64", "64-256", "a-b", "64-", "-70", "64-70-80" })
            {
                cli::StringConsole console;
                int code = cli::RunCli({ "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--palette", "replace", "--palette-range", range, "--out-dir", patches }, console);
                // "-70" is an option that scic does not have: also a usage error.
                Assert::AreEqual(2, code, Wide(std::string(range) + ": " + console.err).c_str());
            }
            usage({ "--pic", "100", "--image", image, "--palette", "replace", "--palette-range", "70-64", "--out-dir", patches }, "--palette-range is first-last");
            usage({ "--pic", "100", "--image", image, "--palette", "all", "--out-dir", patches }, "--palette");
            usage({ "--pic", "100", "--out-dir", patches }, "an image needs --pic and --image");
            usage({ "--image", image, "--out-dir", patches }, "an image needs --pic and --image");
            usage({ "--cel", "0", "--out-dir", patches }, "an image needs --pic and --image");
            usage({ "--pic", "100", "--image", image, "--in-dir", exported, "--out-dir", patches }, "give --in-dir, or --pic with --image, not both");
            usage({ "--out-dir", patches }, "give --in-dir, or --pic with --image");
            usage({ "--pic", "100", "--image", image }, "--out-dir");
            usage({ "--pic", "100", "--image", image, "--out-dir", "" }, "--out-dir needs a folder");
            usage({ "--in-dir", "", "--out-dir", patches }, "--in-dir needs a folder");
            usage({ "--pic", "100", "--image", image, "--cel", "256", "--out-dir", patches }, "--cel");
            usage({ "--pic", "100", "--image", image, "--out-dir", _gameFolder }, "the output folder is the game folder");
            // A patch file in a folder of the game could be a file of the
            // game (a game can read patch files from a folder of its own).
            usage({ "--pic", "100", "--image", image, "--out-dir", _gameFolder + "\\src" }, "the output folder is inside the game folder");
            usage({ "--in-dir", exported, "--out-dir", _gameFolder + "\\src" }, "the output folder is inside the game folder");
            usage({ "--pic", "5", "--image", image, "--out-dir", patches }, "the game has no pic 5");
            usage({ "--pic", "1a", "--image", image, "--out-dir", patches }, "\"1a\" is not a pic number");
            usage({ "--in-dir", empty, "--out-dir", patches }, "the input folder has no .toml file of scic pic export");

            // Plan section 8: a folder that is missing is exit code 3.
            cli::StringConsole noOutDir = Expect(3, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", _outDir + "\\no-such-folder" });
            Assert::IsTrue(Has(noOutDir.err, "the output folder does not exist"), Wide(noOutDir.err).c_str());
            cli::StringConsole noInDir = Expect(3, { "pic", "import", _gameFolder, "--in-dir", _outDir + "\\no-such-folder", "--out-dir", patches });
            Assert::IsTrue(Has(noInDir.err, "the input folder does not exist"), Wide(noInDir.err).c_str());
            Expect(3, { "pic", "import", _gameFolder + "\\NoSuchGame", "--pic", "100", "--image", image, "--out-dir", patches });

            Assert::IsTrue(FileNamesOf(patches).empty(), L"no patch file is written");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
        }

        TEST_METHOD(Import_DryRun_WritesNothing)
        {
            CopyTemplate();
            std::string image = ExportPic100();
            std::string patches = NewFolder("patches");
            auto before = FilesOf(_gameFolder);

            cli::StringConsole console = Expect(0, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", patches, "--dry-run" });

            Assert::IsTrue(FileNamesOf(patches).empty(), L"the output folder stays empty");
            Assert::IsTrue(before == FilesOf(_gameFolder), L"no file of the game changes");
            Assert::IsTrue(Has(console.err, "Would write " + patches + "\\100.p56"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "A run would import 1 of 1 pic (1 cel) into " + patches + "."), Wide(console.err).c_str());

            // A dry run does the checks of a run: an image that fails one is
            // exit code 6.
            IndexedBmp hole = ReadBmp(image);
            hole.pixels[0] = 255;
            AssertOk(WriteIndexedBmpFile(image, hole), "setup: an image with a new skip pixel");
            Expect(6, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", patches, "--dry-run" });
        }

        // Each pic has its own exception boundary: a pic whose image fails a
        // check does not stop the others.
        TEST_METHOD(Import_InDir_OnePicFails_TheOthersAreImported)
        {
            CopyTemplate();
            std::string exported = NewFolder("exported");
            std::string patches = NewFolder("patches");
            Expect(0, { "pic", "export", _gameFolder, "--all", "--out-dir", exported });
            WriteFileText(exported + "\\110.bmp", "not an image");
            // The extension of a .toml file can be in upper case, and a
            // folder with that extension is not a .toml file.
            fs::rename(exported + "\\200.toml", exported + "\\200.TOML");
            fs::create_directories(exported + "\\folder.toml");

            cli::StringConsole console = Expect(6, { "pic", "import", _gameFolder, "--in-dir", exported, "--out-dir", patches });

            Assert::IsTrue(Has(console.err, "scic: error: pic 110: ") && Has(console.err, "not a BMP file"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Imported 2 of 3 pics (2 cels) into " + patches + "; 1 pic failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.p56", "200.p56" }) == FileNamesOf(patches), L"the other pics are imported");
        }

        // A file of the input folder whose name has a character that the
        // ANSI code page does not have: scic cannot open a .toml file with
        // such a name, so that file is a failure of its own. Another file
        // with such a name is not read. The good pic is imported.
        TEST_METHOD(Import_InDir_NamesOutsideTheAnsiCodePage)
        {
            CopyTemplate();
            std::string exported = NewFolder("exported");
            std::string patches = NewFolder("patches");
            Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", exported });
            // A Japanese letter, an Arabic letter and a Latin letter with an
            // accent: no ANSI code page has the three.
            const std::wstring name = { (wchar_t)0x65e5, (wchar_t)0x0639, (wchar_t)0x00e9 };
            fs::copy_file(fs::path(exported) / "100.toml", fs::path(exported) / (name + L".toml"));
            fs::copy_file(fs::path(exported) / "100.bmp", fs::path(exported) / (L"notes." + name));

            cli::StringConsole console = Expect(6, { "pic", "import", _gameFolder, "--in-dir", exported, "--out-dir", patches });

            Assert::IsFalse(Has(console.err, "[internal]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, ".toml: the name of the file has a character that the ANSI code page of Windows does not have, so scic cannot open the file [unsupported]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Imported 1 of 2 pics (1 cel) into " + patches + "; 1 pic failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.p56" }) == FileNamesOf(patches), L"the good pic is imported");
        }

        // The .toml files of the input folder that do not give a pic to
        // import: each is a failure with its reason, and the good one is
        // imported.
        TEST_METHOD(Import_InDir_BadSidecars)
        {
            CopyTemplate();
            std::string exported = NewFolder("exported");
            std::string patches = NewFolder("patches");
            Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "110", "--out-dir", exported });
            // Not TOML; a pic that the game does not have; an image file that
            // is not there; a second file for pic 100; no file for a cel.
            WriteFileText(exported + "\\a-not-toml.toml", "[source\n");
            WriteFileText(exported + "\\b-no-such-pic.toml", "[source]\npic = 5\n[[cel]]\nindex = 0\nfile = '100.bmp'\n");
            fs::remove(exported + "\\110.bmp");
            fs::copy_file(exported + "\\100.toml", exported + "\\z-second.toml");
            WriteFileText(exported + "\\c-no-file.toml", "[source]\npic = 200\n[[cel]]\nindex = 0\n");

            cli::StringConsole console = Expect(6, { "pic", "import", _gameFolder, "--in-dir", exported, "--out-dir", patches });

            // The line of a .toml file that cannot be read has the file one
            // time, as the location of the error.
            std::string notToml;
            for (const std::string &line : LinesOf(console.err))
            {
                if (Has(line, "a-not-toml.toml"))
                {
                    notToml = line;
                }
            }
            Assert::IsTrue((notToml.rfind("scic: error: ", 0) == 0) && Has(notToml, "(" + exported + "\\a-not-toml.toml(1,") && Has(notToml, "[format]"), Wide(console.err).c_str());
            Assert::IsTrue(notToml.find("a-not-toml.toml") == notToml.rfind("a-not-toml.toml"), Wide(notToml).c_str());
            Assert::IsTrue(Has(console.err, "scic: error: pic 5: the game has no pic 5"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "scic: error: pic 110: ") && Has(console.err, "110.bmp") && Has(console.err, "[not-found]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "scic: error: pic 100: the pic is in two .toml files of the input folder; the second is " + exported + "\\z-second.toml"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "scic: error: the file names no image file of a cel (" + exported + "\\c-no-file.toml) [usage]"), Wide(console.err).c_str());
            Assert::IsTrue(Has(console.err, "Imported 1 of 6 pics (1 cel) into " + patches + "; 5 pics failed."), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.p56" }) == FileNamesOf(patches), L"the good pic is imported");
        }

        // The pic in the game is not the copy that the images came from: a
        // warning with the two CRC-32 values, and the import goes on.
        TEST_METHOD(Import_InDir_CrcOfAnotherCopy_IsAWarning)
        {
            CopyTemplate();
            std::string exported = NewFolder("exported");
            std::string patches = NewFolder("patches");
            Expect(0, { "pic", "export", _gameFolder, "--pic", "100", "--out-dir", exported });
            // Another copy of pic 100 in the game: a patch file with other
            // flag bytes in its palette.
            WritePic100WithUnusedEntries(200, 210);

            cli::StringConsole console = Expect(0, { "pic", "import", _gameFolder, "--in-dir", exported, "--out-dir", patches });

            Assert::IsTrue(Has(console.err, "scic: warning: pic 100: " + exported + "\\100.toml has the CRC-32 ") && Has(console.err, "the images are of another copy of the pic"), Wide(console.err).c_str());
            Assert::IsTrue(std::vector<std::string>({ "100.p56" }) == FileNamesOf(patches), L"the patch file is written");
        }

        // Plan section 8: a patch file that cannot be written is exit code 9,
        // also in a dry run.
        TEST_METHOD(Import_PatchFileThatCannotBeWritten_ExitsWith9)
        {
            CopyTemplate();
            std::string image = ExportPic100();
            std::string patches = NewFolder("patches");
            WriteReadOnlyFile(patches + "\\100.p56", "read-only");

            cli::StringConsole dryRun = Expect(9, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", patches, "--dry-run" });
            Assert::IsTrue(Has(dryRun.err, "scic: error: pic 100: ") && Has(dryRun.err, "100.p56"), Wide(dryRun.err).c_str());
            cli::StringConsole console = Expect(9, { "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", patches });
            Assert::IsTrue(Has(console.err, "scic: error: pic 100: ") && Has(console.err, "100.p56"), Wide(console.err).c_str());
            Assert::AreEqual(std::string("read-only"), ReadFileText(patches + "\\100.p56"));
        }

        TEST_METHOD(Import_CtrlC_ExitsWith7)
        {
            CopyTemplate();
            std::string image = ExportPic100();
            std::string patches = NewFolder("patches");
            cli::CancelFlag().store(true);

            cli::StringConsole console;
            int code = cli::RunCli({ "pic", "import", _gameFolder, "--pic", "100", "--image", image, "--out-dir", patches }, console);
            cli::CancelFlag().store(false);

            Assert::AreEqual(7, code, Wide(console.out + console.err).c_str());
            Assert::IsTrue(Has(console.err, "stopped by Ctrl+C"), Wide(console.err).c_str());
            Assert::IsTrue(FileNamesOf(patches).empty(), L"no patch file is written");
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
            Assert::IsTrue(Has(OneLine(root.out), "pic list, pic export, pic import"), Wide(root.out).c_str());
            // The dry run of a pic command writes nothing into its output
            // folder.
            Assert::IsTrue(Has(OneLine(root.out), "write nothing into the game or into an output folder"), Wide(root.out).c_str());
            Assert::IsTrue(Has(group.out, "import"), Wide(group.out).c_str());
            cli::StringConsole helpImport = Expect(0, { "help", "pic", "import" });
            Assert::IsTrue(Has(helpImport.out, "--in-dir") && Has(helpImport.out, "--palette-range") && Has(helpImport.out, "--allow-unused-indices") && Has(helpImport.out, "scic pic import"),
                Wide(helpImport.out).c_str());
        }
    };

    // The parts of the pic commands that need no game (CliPic.h), on the SCI2
    // pics of the pic tests: the template games have no pic with more than
    // one cel. Pic 220 has 4 cels that are not compressed; pic 901 has 8
    // compressed cels.
    TEST_CLASS(TestCliPicSci2)
    {
        // Where pic 220 has the pixels of its cels: 640 x 334, 46 x 74,
        // 30 x 74 and 264 x 122.
        static const size_t Cel0Offset = 996;
        static const size_t Cel1Offset = 214756;
        static const size_t Cel2Offset = 218160;
        static const size_t Cel3Offset = 220380;

        static std::vector<uint8_t> PicData(const char *name)
        {
            ResourceBlob blob;
            std::string path = GetTestFileDirectory("Pics") + "\\SCI2\\" + name;
            Assert::IsTrue(SUCCEEDED(blob.CreateFromFile("pic", path, sciVersion2, ResourceSaveLocation::Patch, sciVersion2.DefaultVolumeFile)), Wide("setup: " + path).c_str());
            return std::vector<uint8_t>(blob.GetData(), blob.GetData() + blob.GetLength());
        }

        // Pic 220 with a width of 1 for cel 0 (the first field of its header,
        // after the pic header of 14 bytes): the cel with the largest area is
        // then cel 3, 264 x 122 at 43,0 with the priority 51.
        static std::vector<uint8_t> Pic220WithBackgroundCel3()
        {
            std::vector<uint8_t> data = PicData("220.p56");
            data[14] = 1;
            data[15] = 0;
            return data;
        }

        // Counts the bytes of a cel in the data, for each value.
        static void Count(const std::vector<uint8_t> &data, size_t offset, size_t size, std::array<uint32_t, 256> &counts)
        {
            for (size_t i = 0; i < size; i++)
            {
                counts[data[offset + i]]++;
            }
        }

        // Entry i is (i, 255 - i, 77), and the entries below "used" are
        // marked as used.
        static PaletteComponent TestPalette(int used)
        {
            PaletteComponent palette;
            for (int i = 0; i < 256; i++)
            {
                palette.Colors[i].rgbRed = (uint8_t)i;
                palette.Colors[i].rgbGreen = (uint8_t)(255 - i);
                palette.Colors[i].rgbBlue = 77;
                palette.Colors[i].rgbReserved = (i < used) ? 1 : 0;
            }
            return palette;
        }

        static toml::table ParseToml(const std::string &text)
        {
            try
            {
                return toml::parse(text);
            }
            catch (const toml::parse_error &error)
            {
                Assert::Fail(Wide(std::string(error.description()) + "\n" + text).c_str());
            }
            return toml::table();
        }

        // An image for pic import with the pixels of a cel of the data
        // (width x height bytes at the offset), and a colour table of 256
        // entries: entry i is (i, 255 - i, 77).
        static cli::PicImportImage ImageOf(int cel, const char *name, const std::vector<uint8_t> &data, size_t offset, int width, int height)
        {
            cli::PicImportImage image;
            image.cel = cel;
            image.name = name;
            image.image.width = width;
            image.image.height = height;
            image.image.pixels.assign(data.begin() + offset, data.begin() + offset + ((size_t)width * (size_t)height));
            image.image.colorCount = 256;
            for (int i = 0; i < 256; i++)
            {
                image.image.colors[i] = IndexedBmpColor{ (uint8_t)i, (uint8_t)(255 - i), 77 };
            }
            return image;
        }

        // Swaps two entries of the pixels: the first that is not the skip
        // colour 255, and the next that is another entry and not 255.
        static void SwapTwoEntries(std::vector<uint8_t> &pixels)
        {
            int one = -1;
            int other = -1;
            for (uint8_t pixel : pixels)
            {
                if (pixel == 255)
                {
                    continue;
                }
                if (one < 0)
                {
                    one = pixel;
                }
                else if (pixel != one)
                {
                    other = pixel;
                    break;
                }
            }
            Assert::IsTrue(other >= 0, L"setup: the pixels have two entries");
            for (uint8_t &pixel : pixels)
            {
                pixel = (pixel == one) ? (uint8_t)other : ((pixel == other) ? (uint8_t)one : pixel);
            }
        }

        static std::array<bool, 256> AllEntries()
        {
            std::array<bool, 256> all;
            all.fill(true);
            return all;
        }

    public:
        TEST_METHOD(ListRow_HasTheBackgroundCelItsPriorityAndTheSkipPixels)
        {
            std::vector<uint8_t> data = Pic220WithBackgroundCel3();
            std::array<uint32_t, 256> counts = {};
            Count(data, Cel0Offset, 1 * 334, counts);
            Count(data, Cel1Offset, 46 * 74, counts);
            Count(data, Cel2Offset, 30 * 74, counts);
            Count(data, Cel3Offset, 264 * 122, counts);
            Assert::IsTrue(counts[255] > 0, L"setup: the cels have pixels of the skip colour");
            std::array<bool, 256> entries = {};
            for (size_t i = 0; i < 255; i++)
            {
                entries[i] = (counts[i] != 0);
            }

            cli::PicListRow row = cli::MakePicListRow(sciVersion2, 220, data, true);

            Assert::IsTrue(row.described && row.error.empty(), Wide(row.error).c_str());
            std::vector<std::string> lines = LinesOf(cli::PicListTsvTable({ row }));
            Assert::AreEqual((size_t)2, lines.size());
            std::vector<std::string> expected = { "220", "vga2", "640", "480", "4", "0", "3", "264", "122", "43", "0", "51", "patch", std::to_string(data.size()),
                fmt::format("{0:08x}", (uint32_t)crcFast(data.data(), (int)data.size())), std::to_string(counts[255]), "0-254", IndexRunsText(IndexRunsOf(entries)), "" };
            Assert::IsTrue(expected == TabFieldsOf(lines[1]), Wide(lines[1]).c_str());

            std::string text = cli::PicListTextTable({ row });
            Assert::IsTrue(Has(text, "  3: 264x122 at 43,0 pri 51  patch  "), Wide(text).c_str());
        }

        TEST_METHOD(ListRow_CountsTheCompressedCels)
        {
            cli::PicListRow row = cli::MakePicListRow(sciVersion2, 901, PicData("901.p56"), false);

            Assert::IsTrue(row.described && row.error.empty(), Wide(row.error).c_str());
            std::vector<std::string> fields = TabFieldsOf(LinesOf(cli::PicListTsvTable({ row }))[1]);
            Assert::AreEqual(std::string("320"), fields[2]);
            Assert::AreEqual(std::string("200"), fields[3]);
            Assert::AreEqual(std::string("8"), fields[4]);
            Assert::AreEqual(std::string("8"), fields[5]);
            Assert::AreEqual(std::string("0"), fields[6]);
            Assert::AreEqual(std::string("-1000"), fields[11]);
            Assert::AreEqual(std::string("package"), fields[12]);
            Assert::AreEqual(std::string("0"), fields[15]);
            Assert::AreEqual(std::string("0-254"), fields[16]);
            Assert::AreEqual(std::string(), fields[17]);
            std::string text = cli::PicListTextTable({ row });
            Assert::IsTrue(Has(text, "901  vga2    320x200     8     8  0: 320x124 at 0,22 pri -1000  package"), Wide(text).c_str());
        }

        // The error of a pic that cannot be read comes two spaces after the
        // last cell of its row, also when another row has a long last cell.
        TEST_METHOD(ListRow_PicThatCannotBeRead_HasItsErrorAfterTheLastCell)
        {
            cli::PicListRow good = cli::MakePicListRow(sciVersion2, 220, PicData("220.p56"), false);
            cli::PicListRow bad = cli::MakePicListRow(sciVersion2, 221, std::vector<uint8_t>(100, 0xff), true);

            Assert::IsFalse(bad.described);
            Assert::IsTrue(Has(bad.error, "an SCI2 pic has 14 and 42") && Has(bad.error, "[format]"), Wide(bad.error).c_str());
            std::vector<std::string> fields = TabFieldsOf(LinesOf(cli::PicListTsvTable({ good, bad }))[2]);
            Assert::AreEqual(std::string("221"), fields[0]);
            Assert::AreEqual(std::string(), fields[2]);
            Assert::AreEqual(std::string("patch"), fields[12]);
            Assert::AreEqual(std::string("100"), fields[13]);
            Assert::AreEqual(bad.error, fields[18]);
            std::vector<std::string> lines = LinesOf(cli::PicListTextTable({ good, bad }));
            Assert::AreEqual((size_t)3, lines.size());
            Assert::IsTrue(TabFieldsOf(LinesOf(cli::PicListTsvTable({ good }))[1])[17].size() > 3, L"setup: the last cell of the row of pic 220 is longer than that of the other row");
            const std::string end = " -  (unreadable: " + bad.error + ")";
            Assert::IsTrue((lines[2].size() > end.size()) && (lines[2].compare(lines[2].size() - end.size(), end.size(), end) == 0), Wide(lines[2]).c_str());
            Assert::IsFalse(Has(lines[2], "   (unreadable"), Wide(lines[2]).c_str());
        }

        // A pic of a format with no image that scic reads: its source, size
        // and CRC-32, and no error.
        TEST_METHOD(ListRow_EgaPic_HasNoImageValues)
        {
            std::vector<uint8_t> data(50, 7);

            cli::PicListRow row = cli::MakePicListRow(sciVersion0, 5, data, false);

            Assert::IsFalse(row.described);
            Assert::IsTrue(row.error.empty(), Wide(row.error).c_str());
            std::vector<std::string> expected = { "5", "ega", "", "", "", "", "", "", "", "", "", "", "package", "50", fmt::format("{0:08x}", (uint32_t)crcFast(data.data(), (int)data.size())), "", "", "", "" };
            Assert::IsTrue(expected == TabFieldsOf(LinesOf(cli::PicListTsvTable({ row }))[1]));
        }

        // With every cel: <pic>.bmp is the background cel (cel 3 here, not cel
        // 0), and each other cel is <pic>.cel<index>.bmp.
        TEST_METHOD(ExportFiles_EveryCel_NamesTheBackgroundAndTheOtherCels)
        {
            std::vector<uint8_t> data = Pic220WithBackgroundCel3();
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe");
            PaletteComponent colors = TestPalette(200);
            PaletteComponent palette999 = TestPalette(80);

            sci::Result<std::vector<cli::PicExportFile>> files = cli::MakePicExportFiles(sciVersion2, 220, data, *description, false, colors, &palette999, true);

            AssertOk(files, "files");
            std::vector<std::string> names;
            for (const cli::PicExportFile &file : *files)
            {
                names.push_back(file.name);
                Assert::AreEqual(file.name == "220.toml", file.isText, Wide(file.name).c_str());
            }
            Assert::IsTrue(std::vector<std::string>({ "220.cel0.bmp", "220.cel1.bmp", "220.cel2.bmp", "220.bmp", "220.toml" }) == names, L"the names of the files");

            sci::Result<IndexedBmp> background = DecodeIndexedBmp((*files)[3].bytes);
            AssertOk(background, "220.bmp");
            Assert::AreEqual(264, background->width);
            Assert::AreEqual(122, background->height);
            Assert::IsTrue(std::vector<uint8_t>(data.begin() + Cel3Offset, data.begin() + Cel3Offset + (264 * 122)) == background->pixels, L"the pixels of cel 3");
            for (size_t i = 0; i < 256; i++)
            {
                Assert::IsTrue(IndexedBmpColor{ (uint8_t)i, (uint8_t)(255 - i), 77 } == background->colors[i], Wide("colour " + std::to_string(i)).c_str());
            }
            sci::Result<IndexedBmp> overlay = DecodeIndexedBmp((*files)[1].bytes);
            AssertOk(overlay, "220.cel1.bmp");
            Assert::AreEqual(46, overlay->width);
            Assert::AreEqual(74, overlay->height);
            Assert::IsTrue(std::vector<uint8_t>(data.begin() + Cel1Offset, data.begin() + Cel1Offset + (46 * 74)) == overlay->pixels, L"the pixels of cel 1");

            const toml::table table = ParseToml(files->back().text);
            Assert::AreEqual((int64_t)220, table["source"]["pic"].value_or((int64_t)-1));
            Assert::AreEqual(std::string("vga2"), table["source"]["format"].value_or(std::string()));
            Assert::AreEqual(std::string("package"), table["source"]["from"].value_or(std::string()));
            Assert::AreEqual((int64_t)data.size(), table["source"]["size"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)(uint32_t)crcFast(data.data(), (int)data.size()), table["source"]["crc32"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)3, table["pic"]["background_cel"].value_or((int64_t)-1));
            Assert::AreEqual(std::string("0-254-1"), RunsOf(table["palette"]["flags"]));
            Assert::AreEqual(std::string("0-199"), RunsOf(table["palette"]["used"]));
            Assert::AreEqual(std::string("0-79"), RunsOf(table["palette"]["palette_999"]));
            Assert::AreEqual(std::string("220.cel0.bmp"), table["cel"][0]["file"].value_or(std::string()));
            Assert::AreEqual(std::string("220.cel1.bmp"), table["cel"][1]["file"].value_or(std::string()));
            Assert::AreEqual((int64_t)40, table["cel"][1]["priority"].value_or((int64_t)-1));
            Assert::AreEqual((int64_t)113, table["cel"][1]["x"].value_or((int64_t)-1));
            Assert::AreEqual(std::string("220.bmp"), table["cel"][3]["file"].value_or(std::string()));
            Assert::AreEqual((int64_t)51, table["cel"][3]["priority"].value_or((int64_t)-1));
        }

        // With the background only: one BMP file, and the .toml file names a
        // file only for that cel. A pic from a patch file, and a game with no
        // palette 999.
        TEST_METHOD(ExportFiles_BackgroundOnly_HasTheBackgroundCel)
        {
            std::vector<uint8_t> data = Pic220WithBackgroundCel3();
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe");
            PaletteComponent colors = TestPalette(256);

            sci::Result<std::vector<cli::PicExportFile>> files = cli::MakePicExportFiles(sciVersion2, 220, data, *description, true, colors, nullptr, false);

            AssertOk(files, "files");
            Assert::AreEqual((size_t)2, files->size());
            Assert::AreEqual(std::string("220.bmp"), (*files)[0].name);
            Assert::AreEqual(std::string("220.toml"), (*files)[1].name);
            sci::Result<IndexedBmp> background = DecodeIndexedBmp((*files)[0].bytes);
            AssertOk(background, "220.bmp");
            Assert::AreEqual(264, background->width);
            Assert::IsTrue(std::vector<uint8_t>(data.begin() + Cel3Offset, data.begin() + Cel3Offset + (264 * 122)) == background->pixels, L"the pixels of cel 3");
            const toml::table table = ParseToml(files->back().text);
            Assert::AreEqual(std::string("patch"), table["source"]["from"].value_or(std::string()));
            Assert::AreEqual(std::string(), RunsOf(table["palette"]["palette_999"]));
            Assert::AreEqual(std::string("0-255"), RunsOf(table["palette"]["used"]));
            Assert::IsFalse((bool)table["cel"][0]["file"], L"a cel with no BMP file has no file key");
            Assert::AreEqual(std::string("220.bmp"), table["cel"][3]["file"].value_or(std::string()));
        }

        // A cel with run-length data that would get a file fails the pic, and
        // so does a pic with no cel.
        TEST_METHOD(ExportFiles_CompressedCelOrNoCel_IsUnsupported)
        {
            std::vector<uint8_t> data = PicData("901.p56");
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe");
            PaletteComponent colors = TestPalette(256);

            for (bool everyCel : { false, true })
            {
                sci::Result<std::vector<cli::PicExportFile>> files = cli::MakePicExportFiles(sciVersion2, 901, data, *description, false, colors, nullptr, everyCel);
                Assert::IsFalse(files.has_value());
                Assert::IsTrue(files.error().code == sci::ErrorCode::Unsupported, Wide(files.error().ToString()).c_str());
                Assert::IsTrue(Has(files.error().message, "cel 0 is compressed"), Wide(files.error().ToString()).c_str());
            }

            PicDescription noCel;
            noCel.format = PicFormat::VGA2;
            sci::Result<std::vector<cli::PicExportFile>> files = cli::MakePicExportFiles(sciVersion2, 5, data, noCel, false, colors, nullptr, true);
            Assert::IsFalse(files.has_value());
            Assert::IsTrue(files.error().code == sci::ErrorCode::Unsupported, Wide(files.error().ToString()).c_str());
            Assert::IsTrue(Has(files.error().message, "the pic has no cel to export"), Wide(files.error().ToString()).c_str());
        }

        // The background (cel 3 here, not cel 0) and cel 1 get new pixels: no
        // other byte of the pic changes.
        TEST_METHOD(ImportData_TwoImages_ChangeOnlyThePixelsOfTheirCels)
        {
            std::vector<uint8_t> data = Pic220WithBackgroundCel3();
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe");
            cli::PicImportImage background = ImageOf(-1, "back.bmp", data, Cel3Offset, 264, 122);
            cli::PicImportImage overlay = ImageOf(1, "one.bmp", data, Cel1Offset, 46, 74);
            SwapTwoEntries(background.image.pixels);
            SwapTwoEntries(overlay.image.pixels);

            std::vector<std::string> warnings;
            sci::Result<std::vector<uint8_t>> made = cli::MakePicImportData(sciVersion2, data, *description, AllEntries(), { overlay, background }, cli::PicImportPalette(), false, warnings);

            AssertOk(made, "import");
            Assert::IsTrue(warnings.empty(), L"no warning");
            Assert::AreEqual(data.size(), made->size());
            std::vector<uint8_t> expected = data;
            std::copy(background.image.pixels.begin(), background.image.pixels.end(), expected.begin() + Cel3Offset);
            std::copy(overlay.image.pixels.begin(), overlay.image.pixels.end(), expected.begin() + Cel1Offset);
            Assert::IsFalse(expected == data, L"setup: the images have new pixels");
            Assert::IsTrue(expected == *made, L"the pic with the new pixels of cel 3 and of cel 1, and no other change");
        }

        // One case for each check of an image: the error names the image or
        // the cel, and no pic comes back.
        TEST_METHOD(ImportData_ImageThatFailsACheck)
        {
            std::vector<uint8_t> data = PicData("220.p56");
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe");
            auto fails = [&](const std::vector<cli::PicImportImage> &images, const std::array<bool, 256> &used, const std::string &text)
            {
                std::vector<std::string> warnings;
                sci::Result<std::vector<uint8_t>> made = cli::MakePicImportData(sciVersion2, data, *description, used, images, cli::PicImportPalette(), false, warnings);
                Assert::IsFalse(made.has_value(), Wide(text).c_str());
                Assert::IsTrue(made.error().code == sci::ErrorCode::Usage, Wide(made.error().ToString()).c_str());
                Assert::IsTrue(Has(made.error().message, text), Wide(text + "\n" + made.error().ToString()).c_str());
            };
            const cli::PicImportImage one = ImageOf(1, "one.bmp", data, Cel1Offset, 46, 74);

            fails({ ImageOf(9, "nine.bmp", data, Cel1Offset, 46, 74) }, AllEntries(), "the pic has no cel 9; it has 4 cels");
            // The background of pic 220 is cel 0.
            fails({ ImageOf(-1, "back.bmp", data, Cel0Offset, 640, 334), ImageOf(0, "zero.bmp", data, Cel0Offset, 640, 334) }, AllEntries(), "two images are for cel 0");
            fails({ ImageOf(1, "short.bmp", data, Cel1Offset, 46, 73) }, AllEntries(), "short.bmp has 46 x 73 pixels, and cel 1 has 46 x 74");
            fails({ ImageOf(1, "wide.bmp", data, Cel1Offset, 47, 74) }, AllEntries(), "wide.bmp has 47 x 74 pixels, and cel 1 has 46 x 74");

            // Two pixels of cel 1 get the skip colour: the pixel at 4,1, and
            // one after it.
            cli::PicImportImage holes = one;
            const size_t first = (1 * 46) + 4;
            Assert::IsTrue((holes.image.pixels[first] != 255) && (holes.image.pixels[first + 1] != 255), L"setup: two pixels that are not the skip colour");
            holes.image.pixels[first] = 255;
            holes.image.pixels[first + 1] = 255;
            fails({ holes }, AllEntries(), "one.bmp: 2 pixels change to or from the skip colour 255 of cel 1 (the first at 4,1); the pixels of the skip colour must stay where they are");
            // A pixel of cel 2 that has the skip colour gets another entry.
            cli::PicImportImage filled = ImageOf(2, "two.bmp", data, Cel2Offset, 30, 74);
            Assert::AreEqual(255, (int)filled.image.pixels[0], L"setup: the first pixel of cel 2 has the skip colour");
            filled.image.pixels[0] = 7;
            fails({ filled }, AllEntries(), "two.bmp: 1 pixel changes to or from the skip colour 255 of cel 2 (the first at 0,0)");

            // An entry of the pixels that is not in use: an error, or a
            // warning and the pic.
            const uint8_t entry = one.image.pixels[0];
            Assert::IsTrue(entry != 255, L"setup: the first pixel of cel 1 is not the skip colour");
            std::array<bool, 256> used = AllEntries();
            used[entry] = false;
            const size_t count = (size_t)std::count(one.image.pixels.begin(), one.image.pixels.end(), entry);
            const std::string unused = fmt::format("one.bmp: {0} pixels of cel 1 have palette entries that the pic and palette 999 do not mark as used: {1}", count, (int)entry);
            Assert::IsTrue(count > 1, L"setup: more than one pixel has the entry");
            fails({ one }, used, unused + " (--allow-unused-indices makes this a warning)");
            std::vector<std::string> warnings;
            sci::Result<std::vector<uint8_t>> allowed = cli::MakePicImportData(sciVersion2, data, *description, used, { one }, cli::PicImportPalette(), true, warnings);
            AssertOk(allowed, "with the unused entries allowed");
            Assert::IsTrue(std::vector<std::string>({ unused }) == warnings, Wide(warnings.empty() ? std::string("no warning") : warnings[0]).c_str());
            Assert::IsTrue(data == *allowed, L"the pic with the same pixels");
            // The skip colour is not an entry of the image: it needs no use.
            used = {};
            used[entry] = true;
            cli::PicImportImage oneEntry = one;
            for (uint8_t &pixel : oneEntry.image.pixels)
            {
                pixel = (pixel == 255) ? 255 : entry;
            }
            warnings.clear();
            AssertOk(cli::MakePicImportData(sciVersion2, data, *description, used, { oneEntry }, cli::PicImportPalette(), false, warnings), "an image of the skip colour and one used entry");
        }

        // A cel with run-length data cannot get new pixels, and a pic with no
        // cel has no background.
        TEST_METHOD(ImportData_CompressedCelOrNoCel)
        {
            std::vector<uint8_t> data = PicData("901.p56");
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe");
            std::vector<std::string> warnings;
            cli::PicImportImage image;
            image.cel = 0;
            image.name = "zero.bmp";
            image.image.width = 320;
            image.image.height = 124;
            image.image.pixels.assign(320 * 124, 0);

            sci::Result<std::vector<uint8_t>> compressed = cli::MakePicImportData(sciVersion2, data, *description, AllEntries(), { image }, cli::PicImportPalette(), false, warnings);
            Assert::IsFalse(compressed.has_value());
            Assert::IsTrue(compressed.error().code == sci::ErrorCode::Unsupported, Wide(compressed.error().ToString()).c_str());
            Assert::IsTrue(Has(compressed.error().message, "cel 0 is compressed; only a cel that is not compressed can get new pixels"), Wide(compressed.error().ToString()).c_str());

            PicDescription noCel;
            noCel.format = PicFormat::VGA2;
            image.cel = -1;
            sci::Result<std::vector<uint8_t>> none = cli::MakePicImportData(sciVersion2, data, noCel, AllEntries(), { image }, cli::PicImportPalette(), false, warnings);
            Assert::IsFalse(none.has_value());
            Assert::IsTrue(none.error().code == sci::ErrorCode::Usage, Wide(none.error().ToString()).c_str());
            Assert::AreEqual(std::string("the pic has no cel"), none.error().message);
        }

        // With "replace", the colours of the range come from the colour
        // table of the images, which must agree in the range; no other byte
        // of the pic changes.
        TEST_METHOD(ImportData_PaletteReplace)
        {
            std::vector<uint8_t> data = PicData("220.p56");
            sci::Result<PicDescription> description = DescribePic(sciVersion2, data);
            AssertOk(description, "setup: describe");
            cli::PicImportPalette palette;
            palette.replace = true;
            palette.first = 10;
            palette.last = 20;
            const cli::PicImportImage one = ImageOf(1, "one.bmp", data, Cel1Offset, 46, 74);
            cli::PicImportImage two = ImageOf(2, "two.bmp", data, Cel2Offset, 30, 74);
            // The images can have other colours outside the range.
            two.image.colors[9] = IndexedBmpColor{ 1, 2, 3 };
            two.image.colors[21] = IndexedBmpColor{ 1, 2, 3 };

            std::vector<std::string> warnings;
            sci::Result<std::vector<uint8_t>> made = cli::MakePicImportData(sciVersion2, data, *description, AllEntries(), { one, two }, palette, false, warnings);

            AssertOk(made, "import");
            Assert::IsTrue(warnings.empty(), L"no warning");
            sci::Result<PicDescription> after = DescribePic(sciVersion2, *made);
            AssertOk(after, "describe the new pic");
            bool anyNew = false;
            for (int entry = 0; entry < 256; entry++)
            {
                const bool inRange = (entry >= 10) && (entry <= 20);
                const IndexedBmpColor expected = inRange ? one.image.colors[entry] : description->palette.colors[entry];
                Assert::IsTrue(expected == after->palette.colors[entry], Wide("colour " + std::to_string(entry)).c_str());
                anyNew = anyNew || (inRange && (expected != description->palette.colors[entry]));
            }
            Assert::IsTrue(anyNew, L"setup: the range gets a new colour");
            // The palette of pic 220 has its entries at offset 225, 3 bytes
            // for each.
            Assert::AreEqual(data.size(), made->size());
            for (size_t i = 0; i < data.size(); i++)
            {
                if ((i < 225 + (10 * 3)) || (i >= 225 + (21 * 3)))
                {
                    Assert::AreEqual((int)data[i], (int)(*made)[i], Wide("byte " + std::to_string(i)).c_str());
                }
            }

            // An entry of the range that the pic does not mark as used: a
            // warning.
            PicDescription unmarked = *description;
            unmarked.palette.flags[12] = 0;
            unmarked.palette.flags[13] = 0;
            AssertOk(cli::MakePicImportData(sciVersion2, data, unmarked, AllEntries(), { one }, palette, false, warnings), "import with entries that are not marked");
            Assert::IsTrue(std::vector<std::string>({ "the pic does not mark the entries 12-13 as used, so the game takes their colours from another palette" }) == warnings,
                Wide(warnings.empty() ? std::string("no warning") : warnings[0]).c_str());

            auto fails = [&](const std::vector<cli::PicImportImage> &images, const cli::PicImportPalette &range, const std::string &text)
            {
                std::vector<std::string> ignored;
                sci::Result<std::vector<uint8_t>> failed = cli::MakePicImportData(sciVersion2, data, *description, AllEntries(), images, range, false, ignored);
                Assert::IsFalse(failed.has_value(), Wide(text).c_str());
                Assert::IsTrue(failed.error().code == sci::ErrorCode::Usage, Wide(failed.error().ToString()).c_str());
                Assert::IsTrue(Has(failed.error().message, text), Wide(text + "\n" + failed.error().ToString()).c_str());
            };
            // The second image has another colour for an entry of the range.
            cli::PicImportImage other = two;
            other.image.colors[15] = IndexedBmpColor{ 9, 9, 9 };
            fails({ one, other }, palette, "one.bmp and two.bmp have another colour for entry 15; the images of a pic must have one palette");
            // The first and the last entry of the range are compared too.
            other = two;
            other.image.colors[10] = IndexedBmpColor{ 9, 9, 9 };
            fails({ one, other }, palette, "have another colour for entry 10");
            other = two;
            other.image.colors[20] = IndexedBmpColor{ 9, 9, 9 };
            fails({ one, other }, palette, "have another colour for entry 20");
            // A colour table that ends before the end of the range.
            cli::PicImportImage few = one;
            few.image.colorCount = 20;
            fails({ few }, palette, "one.bmp: the colour table has 20 entries, and the palette range is 10-20");
            few.image.colorCount = 21;
            AssertOk(cli::MakePicImportData(sciVersion2, data, *description, AllEntries(), { few }, palette, false, warnings), "a colour table with the last entry of the range");
            // Pic 220 stores the entries 0 to 254.
            cli::PicImportPalette outside = palette;
            outside.first = 250;
            outside.last = 255;
            fails({ one }, outside, "the palette stores the entries 0-254; the range 250-255 is not inside them");
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

        // The reader gives back each value that the writer wrote.
        TEST_METHOD(Parse_GivesBackWhatTextWrote)
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

            sci::Result<PicSidecar> read = ParsePicSidecar(PicSidecarText(sidecar));

            AssertOk(read, "parse");
            Assert::AreEqual(230, read->pic);
            Assert::AreEqual(std::string("vga2"), read->format);
            Assert::AreEqual(std::string("patch"), read->from);
            Assert::AreEqual((uint32_t)252588, read->size);
            Assert::IsTrue(read->hasCrc32);
            Assert::AreEqual((uint32_t)0xdeadbeef, read->crc32);
            Assert::AreEqual(640, read->width);
            Assert::AreEqual(480, read->height);
            Assert::AreEqual(1, read->backgroundCel);
            Assert::IsTrue(read->hasPalette);
            Assert::AreEqual(104, read->storedFirst);
            Assert::AreEqual(132, read->storedCount);
            Assert::AreEqual(3, read->entrySize);
            Assert::AreEqual((size_t)2, read->flags.size());
            Assert::AreEqual(201, read->flags[1].first);
            Assert::AreEqual(235, read->flags[1].last);
            Assert::AreEqual(0, read->flags[1].flag);
            Assert::AreEqual(1, read->flags[0].flag);
            Assert::IsTrue(sidecar.palette999 == read->palette999);
            Assert::IsTrue(sidecar.used == read->used);
            Assert::AreEqual((size_t)2, read->cels.size());
            const PicSidecarCel &first = read->cels[0];
            Assert::AreEqual(0, first.index);
            Assert::IsTrue(first.file.empty());
            Assert::AreEqual(46, first.width);
            Assert::AreEqual(74, first.height);
            Assert::AreEqual(113, first.x);
            Assert::AreEqual(-5, first.y);
            Assert::AreEqual(-1000, first.priority);
            Assert::AreEqual(255, first.skipColor);
            Assert::IsTrue(first.compressed);
            Assert::IsTrue(first.entries.empty());
            Assert::AreEqual((uint32_t)77, first.skipPixels);
            const PicSidecarCel &second = read->cels[1];
            Assert::AreEqual(1, second.index);
            Assert::AreEqual(std::string("230.bmp"), second.file);
            Assert::AreEqual(640, second.width);
            Assert::AreEqual(334, second.height);
            Assert::AreEqual(40, second.priority);
            Assert::AreEqual(250, second.skipColor);
            Assert::IsFalse(second.compressed);
            Assert::IsTrue(background.entries == second.entries);
        }

        // The reader needs only the pic number and the index of each cel.
        TEST_METHOD(Parse_KeysThatAreNotThereKeepTheirValues)
        {
            sci::Result<PicSidecar> read = ParsePicSidecar("[source]\npic = 7\n\n[[cel]]\nindex = 2\nfile = \"7.cel2.bmp\"\n");

            AssertOk(read, "parse");
            Assert::AreEqual(7, read->pic);
            Assert::IsFalse(read->hasCrc32);
            Assert::IsTrue(read->format.empty());
            Assert::AreEqual(-1, read->backgroundCel);
            Assert::IsFalse(read->hasPalette);
            Assert::AreEqual((size_t)1, read->cels.size());
            Assert::AreEqual(2, read->cels[0].index);
            Assert::AreEqual(std::string("7.cel2.bmp"), read->cels[0].file);

            sci::Result<PicSidecar> noCels = ParsePicSidecar("[source]\npic = 65535\n");
            AssertOk(noCels, "a file with no cel");
            Assert::AreEqual(65535, noCels->pic);
            Assert::IsTrue(noCels->cels.empty());
        }

        TEST_METHOD(Parse_BadText_IsFormat)
        {
            auto fails = [](const std::string &text, const std::string &textInMessage, int line)
            {
                sci::Result<PicSidecar> read = ParsePicSidecar(text);
                Assert::IsFalse(read.has_value(), Wide("the parse must fail: " + text).c_str());
                Assert::IsTrue(read.error().code == sci::ErrorCode::Format, Wide(read.error().ToString()).c_str());
                Assert::IsTrue(Has(read.error().message, textInMessage), Wide(textInMessage + ": " + read.error().ToString()).c_str());
                if (line > 0)
                {
                    Assert::AreEqual(line, read.error().where.line, Wide(read.error().ToString()).c_str());
                }
            };
            // Not TOML: the error has the line.
            fails("[source]\npic = = 7\n", "", 2);
            // No pic number.
            fails("", "the file has no pic number", 0);
            fails("[pic]\nwidth = 3\n", "the file has no pic number", 0);
            fails("[source]\nformat = 'vga2'\n", "the file has no pic number", 0);
            // A value of another type, or outside its range.
            fails("[source]\npic = '7'\n", "[source]: \"pic\" is not a whole number", 2);
            fails("[source]\npic = 7.0\n", "[source]: \"pic\" is not a whole number", 2);
            fails("[source]\npic = 65536\n", "[source]: \"pic\" is 65536; it must be 0 to 65535", 2);
            fails("[source]\npic = -1\n", "[source]: \"pic\" is -1; it must be 0 to 65535", 2);
            fails("[source]\npic = 7\ncrc32 = 0x100000000\n", "[source]: \"crc32\" is 4294967296", 3);
            fails("[source]\npic = 7\nformat = 2\n", "[source]: \"format\" is not a text", 3);
            fails("[source]\npic = 7\n[palette]\npresent = 1\n", "[palette]: \"present\" is not true or false", 4);
            fails("[source]\npic = 7\n[palette]\nused = 5\n", "[palette]: \"used\" is not an array", 4);
            fails("[source]\npic = 7\n[palette]\nused = [[5]]\n", "an element of \"used\" is not 2 numbers", 4);
            fails("[source]\npic = 7\n[palette]\nused = [[9, 3]]\n", "an element of \"used\" is not 2 numbers", 4);
            fails("[source]\npic = 7\n[palette]\nused = [[0, 256]]\n", "an element of \"used\" is not 2 numbers", 4);
            fails("[source]\npic = 7\n[palette]\nflags = [[0, 9]]\n", "an element of \"flags\" is not 3 numbers", 4);
            // A table key that is not a table.
            fails("source = 5\n", "\"source\" is not a table ([source])", 1);
            fails("pic = 5\n[source]\npic = 7\n", "\"pic\" is not a table ([pic])", 1);
            fails("palette = 'none'\n[source]\npic = 7\n", "\"palette\" is not a table ([palette])", 1);
            // The cels.
            fails("[source]\npic = 7\n[cel]\nindex = 0\n", "\"cel\" is not an array of tables", 0);
            fails("[source]\npic = 7\n[[cel]]\nfile = 'a.bmp'\n", "a [[cel]] has no index", 0);
            fails("[source]\npic = 7\n[[cel]]\nindex = 256\n", "[[cel]]: \"index\" is 256; it must be 0 to 255", 4);
            fails("[source]\npic = 7\n[[cel]]\nindex = 3\nwidth = 'wide'\n", "[[cel]] 3: \"width\" is not a whole number", 5);
            // The file of a cel is a file name with no folder.
            for (const char *file : { "..\\\\7.bmp", "sub/7.bmp", "C:7.bmp", "..", ".", "" })
            {
                fails(std::string("[source]\npic = 7\n[[cel]]\nindex = 3\nfile = \"") + file + "\"\n", "[[cel]] 3: \"file\" must be a file name with no folder", 5);
            }
            // A file name with a Japanese, an Arabic and an accented Latin
            // letter (in UTF-8): no ANSI code page has the three.
            fails(std::string("[source]\npic = 7\n[[cel]]\nindex = 3\nfile = \"") + "\xE6\x97\xA5\xD8\xB9\xC3\xA9" + ".bmp\"\n",
                "[[cel]] 3: \"file\" has a character that the ANSI code page of Windows does not have", 5);
        }

        // The text of a TOML file is UTF-8, and a path of scic is in the
        // ANSI code page: the reader gives the file of a cel in that code
        // page.
        TEST_METHOD(Parse_FileNameOutsideAscii_IsInTheAnsiCodePage)
        {
            // "caf", an e with an acute accent, ".bmp".
            const std::wstring wide = { L'c', L'a', L'f', (wchar_t)0x00e9, L'.', L'b', L'm', L'p' };
            const std::string utf8 = std::string("caf") + "\xC3\xA9" + ".bmp";
            char ansi[16] = {};
            BOOL usedDefault = FALSE;
            int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), (int)wide.size(), ansi, (int)sizeof(ansi), nullptr, &usedDefault);

            sci::Result<PicSidecar> read = ParsePicSidecar("[source]\npic = 7\n[[cel]]\nindex = 0\nfile = \"" + utf8 + "\"\n");

            if ((length > 0) && !usedDefault)
            {
                AssertOk(read, "parse");
                Assert::AreEqual(std::string(ansi, (size_t)length), read->cels[0].file);
            }
            else
            {
                // The code page of this machine does not have the letter.
                Assert::IsFalse(read.has_value());
            }
        }

        TEST_METHOD(ReadFile_MissingFileAndBadFile)
        {
            std::string folder = (fs::temp_directory_path() / ("scicompanion-sidecar-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount()))).string();
            fs::create_directories(folder);

            sci::Result<PicSidecar> missing = ReadPicSidecarFile(folder + "\\missing.toml");
            Assert::IsFalse(missing.has_value());
            Assert::IsTrue(missing.error().code == sci::ErrorCode::NotFound, Wide(missing.error().ToString()).c_str());

            std::string bad = folder + "\\bad.toml";
            WriteFileText(bad, "[source]\npic = 70000\n");
            sci::Result<PicSidecar> outOfRange = ReadPicSidecarFile(bad);
            Assert::IsFalse(outOfRange.has_value());
            Assert::IsTrue(outOfRange.error().code == sci::ErrorCode::Format, Wide(outOfRange.error().ToString()).c_str());
            Assert::AreEqual(bad, outOfRange.error().where.file);
            Assert::AreEqual(2, outOfRange.error().where.line);

            std::string good = folder + "\\good.toml";
            PicSidecar sidecar;
            sidecar.pic = 12;
            sidecar.format = "vga11";
            WriteFileText(good, PicSidecarText(sidecar));
            sci::Result<PicSidecar> read = ReadPicSidecarFile(good);
            RemoveFolder(folder);
            AssertOk(read, "the good file");
            Assert::AreEqual(12, read->pic);
            Assert::AreEqual(std::string("vga11"), read->format);
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
