#include "stdafx.h"
#include "CppUnitTest.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "GameFolderHelper.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileWrite.h"
#include "FileWrite.h"
#include "SCO.h"
#include "Helper.h"
#include "TestSupport.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // The bytes of each file under the folder, by path.
    std::map<std::string, std::vector<uint8_t>> Snapshot(const std::string &folder)
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

    // The files that the two snapshots do not have the same.
    std::string Differences(const std::map<std::string, std::vector<uint8_t>> &before, const std::map<std::string, std::vector<uint8_t>> &after)
    {
        std::string text;
        for (const auto &file : after)
        {
            auto old = before.find(file.first);
            if (old == before.end())
            {
                text += "new: " + file.first + "\n";
            }
            else if (old->second != file.second)
            {
                text += "changed: " + file.first + "\n";
            }
        }
        for (const auto &file : before)
        {
            if (after.find(file.first) == after.end())
            {
                text += "removed: " + file.first + "\n";
            }
        }
        return text;
    }

    std::string ErrorsOfLog(CompileLog &log)
    {
        std::string errors;
        for (const CompileResult &result : log.Results())
        {
            if (result.IsError())
            {
                errors += result.GetMessage() + "\n";
            }
        }
        return errors;
    }

    // A template, and the names that its game gives the files of script 903.
    struct TemplateCase
    {
        const char *folder;
        const char *useLine;       // the script that has the base class
        const char *baseClass;
        const char *script;        // patch file names
        const char *heap;          // empty: no heap resource
        const char *text;
        const char *classTable;
        const char *selectorTable;
    };

    const TemplateCase Templates[] =
    {
        { TemplateSci0, "(use obj)\n", "Obj", "script.903", "", "text.903", "vocab.996", "vocab.997" },
        { TemplateSci11, "(use System)\n", "Object", "903.scr", "903.hep", "903.tex", "996.voc", "997.voc" },
    };

    // A class with a new property and a new method, so the compile changes
    // both vocab tables; and a string that goes to text 903.
    std::string WidgetSource(const TemplateCase &templateCase)
    {
        return std::string(
            "(script# 903)\n"
            "(text# 903)\n"
            "(include sci.sh)\n"
            "(include game.sh)\n"
            "(use Main)\n") + templateCase.useLine +
            "(class S1Widget of " + templateCase.baseClass + "\n"
            "    (properties\n"
            "        s1WidgetSize 0\n"
            "    )\n"
            "    (method (s1WidgetGo)\n"
            "        (StrLen \"s1 text\")\n"
            "        (return s1WidgetSize)\n"
            "    )\n"
            ")\n";
    }

    const char *const PackageFiles[] = { "resource.map", "resource.000", "resource.001" };
}

namespace UnitTests
{
    // Where a compile writes (plan section 5): a caller chooses the package,
    // patch files or an output folder for the script, heap, text and vocab
    // tables, and the .sco, .scd and .sc writes report their errors.
    TEST_CLASS(TestCompileDestination)
    {
        GameCopy _game;
        // The temp folder of FileWrite_GivesTheFailureAndKeepsTextMode.
        std::string _folder;

        // Opens a copy of the template, and writes the widget source into
        // src\S1Widget.sc.
        GameSession &OpenCopy(const TemplateCase &templateCase)
        {
            GameSession &session = _game.OpenCopy(templateCase.folder);
            WriteFileText(session.Helper().GetScriptFileName("S1Widget"), WidgetSource(templateCase));
            return session;
        }

        // Compiles src\S1Widget.sc as script 903 with the options, and saves
        // the tables with the same options when the compile succeeds. The
        // first compile in a copy changes both tables.
        static bool CompileWidget(GameSession &session, const CompileWriteOptions &options, CompileLog &log,
            std::vector<uint8_t> *scriptBytes = nullptr, std::vector<uint8_t> *heapBytes = nullptr, std::vector<uint8_t> *debugBytes = nullptr)
        {
            ScriptId scriptId(session.Helper().GetScriptFileName("S1Widget").c_str());
            scriptId.SetResourceNumber(903);
            CompileTables tables;
            Assert::IsTrue(tables.Load(session.ResourceMap()), L"setup: the vocab tables must load");
            PrecompiledHeaders headers(session.ResourceMap());
            CompileResults results(log, session.Version());
            bool compiled = NewCompileScript(session, results, log, tables, headers, scriptId, options);
            if (compiled)
            {
                AssertOk(tables.Save(session.ResourceMap(), options));
                if (scriptBytes)
                {
                    *scriptBytes = results.GetScriptResource();
                }
                if (heapBytes)
                {
                    *heapBytes = results.GetHeapResource();
                }
                if (debugBytes)
                {
                    *debugBytes = results.GetDebugInfo();
                }
            }
            return compiled;
        }

        // CompileWidget, then an assert that the compile succeeds; the message
        // is the errors of the compile.
        static void AssertCompiles(GameSession &session, const CompileWriteOptions &options,
            std::vector<uint8_t> *scriptBytes = nullptr, std::vector<uint8_t> *heapBytes = nullptr, std::vector<uint8_t> *debugBytes = nullptr)
        {
            CompileLog log;
            bool compiled = CompileWidget(session, options, log, scriptBytes, heapBytes, debugBytes);
            Assert::IsTrue(compiled, Wide(ErrorsOfLog(log)).c_str());
        }

        std::map<std::string, std::vector<uint8_t>> PackageSnapshot()
        {
            std::map<std::string, std::vector<uint8_t>> files;
            for (const char *name : PackageFiles)
            {
                if (_game.Has(name))
                {
                    files[name] = ReadFileBytes(_game.Path(name));
                }
            }
            return files;
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveFolder(_folder);
        }

        // Patch in a package-mode game: every resource of the compile is a
        // patch file, and the package does not change.
        TEST_METHOD(Patch_WritesPatchFiles_PackageUnchanged)
        {
            NoAppState noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                GameSession &session = OpenCopy(templateCase);
                Assert::IsTrue(session.Helper().GetResourceSaveLocation(ResourceSaveLocation::Default) == ResourceSaveLocation::Package, L"setup: the template is in package mode");
                auto package = PackageSnapshot();

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Patch;
                AssertCompiles(session, options);

                for (const char *name : { templateCase.script, templateCase.heap, templateCase.text, templateCase.classTable, templateCase.selectorTable })
                {
                    if (*name)
                    {
                        Assert::IsTrue(_game.Has(name), Wide(std::string("no patch file ") + name + " in " + templateCase.folder).c_str());
                    }
                }
                Assert::IsTrue(package == PackageSnapshot(), Wide(std::string("the package changed: ") + templateCase.folder).c_str());
            }
        }

        // Package in a patch-mode game writes the package. Default there
        // gives patch files, as the game says.
        TEST_METHOD(Package_InAPatchModeGame_WritesThePackage)
        {
            NoAppState noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                GameSession &session = OpenCopy(templateCase);
                session.Helper().SetResourceSaveLocation(ResourceSaveLocation::Patch);
                auto package = PackageSnapshot();

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Package;
                AssertCompiles(session, options);
                Assert::IsFalse(_game.Has(templateCase.script), Wide(std::string("a patch file in ") + templateCase.folder).c_str());
                Assert::IsFalse(package == PackageSnapshot(), Wide(std::string("the package did not change: ") + templateCase.folder).c_str());

                AssertCompiles(session, CompileWriteOptions());
                Assert::IsTrue(_game.Has(templateCase.script), Wide(std::string("Default must follow the game's patch mode: ") + templateCase.folder).c_str());
            }
        }

        // An output folder gets the patch files; the game's resources do not
        // change, and src\ gets the .sco.
        TEST_METHOD(OutDir_WritesPatchFilesThere_GameUnchanged)
        {
            NoAppState noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                GameSession &session = OpenCopy(templateCase);
                std::string outDir = _game.Path("out");
                fs::create_directory(outDir);
                fs::remove(session.Helper().GetScriptObjectFileName("S1Widget"));
                auto package = PackageSnapshot();

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Patch;
                options.outDir = outDir;
                AssertCompiles(session, options);

                for (const char *name : { templateCase.script, templateCase.heap, templateCase.text, templateCase.classTable, templateCase.selectorTable })
                {
                    if (*name)
                    {
                        Assert::IsTrue(fs::exists(fs::path(outDir) / name), Wide(std::string("no ") + name + " in the output folder").c_str());
                        Assert::IsFalse(_game.Has(name), Wide(std::string("a patch file in the game: ") + name).c_str());
                    }
                }
                // The patch header: the type with the high bit, then 0.
                std::vector<uint8_t> script = ReadFileBytes((fs::path(outDir) / templateCase.script).string());
                Assert::IsTrue((script.size() > 2) && (script[0] == (0x80 | (int)ResourceType::Script)) && (script[1] == 0), L"the script patch file has the patch header");
                Assert::IsTrue(package == PackageSnapshot(), L"the package changed");
                Assert::IsTrue(fs::exists(session.Helper().GetScriptObjectFileName("S1Widget")), L"the .sco goes to src\\");
            }
        }

        // A resource that cannot be written fails the compile, and the script
        // gets no .sco: the .sco describes the resources.
        TEST_METHOD(OutDir_AFailedWrite_NoObjectFile)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(Templates[0]);
            std::string outDir = _game.Path("out");
            fs::create_directory(outDir);
            std::string sco = session.Helper().GetScriptObjectFileName("S1Widget");
            fs::remove(sco);
            std::string target = (fs::path(outDir) / Templates[0].script).string();
            WriteFileText(target, "an old script");
            Assert::IsTrue(SetFileAttributesA(target.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            CompileWriteOptions options;
            options.saveTo = ResourceSaveLocation::Patch;
            options.outDir = outDir;
            CompileLog log;
            Assert::IsFalse(CompileWidget(session, options, log), L"the script file cannot be written");
            Assert::IsFalse(fs::exists(sco), L"no .sco for a script whose resource was not written");
        }

        // With raw, the output folder gets the plain data of each resource.
        TEST_METHOD(OutDirRaw_WritesThePlainData)
        {
            NoAppState noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                GameSession &session = OpenCopy(templateCase);
                std::string outDir = _game.Path("raw");
                fs::create_directory(outDir);

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Patch;
                options.outDir = outDir;
                options.raw = true;
                std::vector<uint8_t> scriptBytes;
                std::vector<uint8_t> heapBytes;
                AssertCompiles(session, options, &scriptBytes, &heapBytes);

                Assert::IsTrue(ReadFileBytes(outDir + "\\script.903.bin") == scriptBytes, L"script.903.bin must hold the script's bytes");
                if (*templateCase.heap)
                {
                    Assert::IsTrue(!heapBytes.empty() && (ReadFileBytes(outDir + "\\heap.903.bin") == heapBytes), L"heap.903.bin must hold the heap's bytes");
                }
                for (const char *name : { "text.903.bin", "vocab.996.bin", "vocab.997.bin" })
                {
                    Assert::IsTrue(fs::exists(fs::path(outDir) / name), Wide(std::string("no ") + name).c_str());
                }
                Assert::IsFalse(fs::exists(fs::path(outDir) / templateCase.script), L"raw writes no patch file");
            }
        }

        // An output folder takes patch files, not the package.
        TEST_METHOD(OutDirWithPackage_IsAnError)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(Templates[0]);
            auto package = PackageSnapshot();

            CompileWriteOptions options;
            options.saveTo = ResourceSaveLocation::Package;
            options.outDir = _game.Folder();
            CompileLog log;
            Assert::IsFalse(CompileWidget(session, options, log), L"an output folder with the package must fail");
            std::string errors = ErrorsOfLog(log);
            Assert::IsTrue(errors.find("output folder") != std::string::npos, Wide(errors).c_str());
            Assert::IsTrue(package == PackageSnapshot(), L"the package changed");
        }

        // A dry run writes nothing: no resource, table, .sco or .scd.
        TEST_METHOD(DryRun_WritesNothing)
        {
            NoAppState noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                GameSession &session = OpenCopy(templateCase);
                session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
                auto before = Snapshot(_game.Folder());

                CompileWriteOptions options;
                options.writeResources = false;
                options.writeObjectFile = false;
                options.writeDebugInfo = false;
                AssertCompiles(session, options);
                std::string differences = Differences(before, Snapshot(_game.Folder()));
                Assert::IsTrue(differences.empty(), Wide(differences).c_str());
            }
        }

        // The debug information goes to debug\903.scd when the game makes
        // it, and not with writeDebugInfo false.
        TEST_METHOD(DebugInfo_IsWrittenOnlyWhenAsked)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(Templates[1]);
            session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
            std::string scd = _game.Path("debug\\903.scd");

            CompileWriteOptions noDebug;
            noDebug.writeDebugInfo = false;
            AssertCompiles(session, noDebug);
            Assert::IsFalse(fs::exists(scd), L"no .scd with writeDebugInfo false");

            std::vector<uint8_t> debugBytes;
            AssertCompiles(session, CompileWriteOptions(), nullptr, nullptr, &debugBytes);
            Assert::IsFalse(debugBytes.empty(), L"setup: the game makes debug information");
            Assert::IsTrue(ReadFileBytes(scd) == debugBytes, L"debug\\903.scd must hold the debug information");
        }

        // A .sco that cannot be written is an error of the compile.
        TEST_METHOD(ObjectFile_ReadOnly_IsACompileError)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(Templates[0]);
            std::string sco = session.Helper().GetScriptObjectFileName("S1Widget");
            WriteFileText(sco, "old");
            Assert::IsTrue(!!SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_READONLY), L"setup: the .sco must be read-only");

            CompileLog log;
            Assert::IsFalse(CompileWidget(session, CompileWriteOptions(), log), L"the compile must fail when its .sco cannot be written");
            std::string errors = ErrorsOfLog(log);
            Assert::IsTrue(errors.find("S1Widget.sco") != std::string::npos, Wide(errors).c_str());
            Assert::IsTrue(ReadFileBytes(sco) == std::vector<uint8_t>({ 'o', 'l', 'd' }), L"the read-only .sco must not change");
        }

        // The file writer gives NotFound for a missing folder, Io for a
        // read-only file, and CR LF for each line feed of a text.
        TEST_METHOD(FileWrite_GivesTheFailureAndKeepsTextMode)
        {
            _folder = (fs::temp_directory_path() / ("S1FileWrite_" + std::to_string(GetCurrentProcessId()))).string();
            fs::create_directories(_folder);

            sci::Status missing = WriteBytesToFile(_folder + "\\no such folder\\a.bin", std::vector<uint8_t>({ 1, 2 }));
            Assert::IsFalse(missing.has_value());
            Assert::IsTrue(missing.error().code == sci::ErrorCode::NotFound, Wide(missing.error().ToString()).c_str());

            std::string readOnly = _folder + "\\readonly.bin";
            AssertOk(WriteBytesToFile(readOnly, std::vector<uint8_t>({ 1 })));
            SetFileAttributesA(readOnly.c_str(), FILE_ATTRIBUTE_READONLY);
            sci::Status denied = WriteBytesToFile(readOnly, std::vector<uint8_t>({ 2 }));
            Assert::IsFalse(denied.has_value());
            Assert::IsTrue(denied.error().code == sci::ErrorCode::Io, Wide(denied.error().ToString()).c_str());
            Assert::IsTrue(denied.error().ToString().find("readonly.bin") != std::string::npos, L"the error names the file");

            std::string text = _folder + "\\text.sc";
            AssertOk(WriteTextToFile(text, "a\nb\n"));
            Assert::IsTrue(ReadFileBytes(text) == std::vector<uint8_t>({ 'a', '\r', '\n', 'b', '\r', '\n' }), L"text mode: CR LF for each line feed");
        }
    };
}
