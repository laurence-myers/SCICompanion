#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "GameFolderHelper.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileWrite.h"
#include "FileWrite.h"
#include "SCO.h"
#include "Helper.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // Runs a test with no AppState, as the command line does.
    struct NoAppStateForDestination
    {
        AppState *saved;
        NoAppStateForDestination() : saved(appState) { appState = nullptr; }
        ~NoAppStateForDestination() { appState = saved; }
    };

    std::wstring WideForDestination(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::vector<uint8_t> ReadAllBytes(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // The bytes of each file under the folder, by path.
    std::map<std::string, std::vector<uint8_t>> Snapshot(const std::string &folder)
    {
        std::map<std::string, std::vector<uint8_t>> files;
        for (const auto &entry : fs::recursive_directory_iterator(folder))
        {
            if (entry.is_regular_file())
            {
                files[entry.path().string()] = ReadAllBytes(entry.path().string());
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
        { "\\TemplateGame\\SCI0", "(use obj)\n", "Obj", "script.903", "", "text.903", "vocab.996", "vocab.997" },
        { "\\TemplateGame\\SCI1.1", "(use System)\n", "Object", "903.scr", "903.hep", "903.tex", "996.voc", "997.voc" },
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
        std::string _copyFolder;

        void RemoveCopy()
        {
            if (!_copyFolder.empty())
            {
                // A test can leave a read-only file.
                std::error_code ec;
                if (fs::exists(_copyFolder, ec))
                {
                    for (const auto &entry : fs::recursive_directory_iterator(_copyFolder, ec))
                    {
                        SetFileAttributesA(entry.path().string().c_str(), FILE_ATTRIBUTE_NORMAL);
                    }
                }
                fs::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

        // Opens a copy of the template, and writes the widget source into
        // src\S1Widget.sc.
        void OpenCopy(const TemplateCase &templateCase, GameSession &session)
        {
            RemoveCopy();
            _copyFolder = CopyGameFromModuleFolder(templateCase.folder);
            Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
            std::ofstream file(session.Helper().GetScriptFileName("S1Widget").c_str(), std::ios::binary | std::ios::trunc);
            file << WidgetSource(templateCase);
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
                sci::Status saved = tables.Save(session.ResourceMap(), options);
                Assert::IsTrue(saved.has_value(), WideForDestination(saved ? std::string() : saved.error().ToString()).c_str());
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

        bool GameHasFile(const std::string &name)
        {
            return fs::exists(fs::path(_copyFolder) / name);
        }

        std::map<std::string, std::vector<uint8_t>> PackageSnapshot()
        {
            std::map<std::string, std::vector<uint8_t>> files;
            for (const char *name : PackageFiles)
            {
                if (GameHasFile(name))
                {
                    files[name] = ReadAllBytes((fs::path(_copyFolder) / name).string());
                }
            }
            return files;
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        // Patch in a package-mode game: every resource of the compile is a
        // patch file, and the package does not change.
        TEST_METHOD(Patch_WritesPatchFiles_PackageUnchanged)
        {
            NoAppStateForDestination noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                SessionOptions sessionOptions;
                sessionOptions.dataFolder = GetTestModuleDirectory();
                GameSession session(sessionOptions);
                OpenCopy(templateCase, session);
                Assert::IsTrue(session.Helper().GetResourceSaveLocation(ResourceSaveLocation::Default) == ResourceSaveLocation::Package, L"setup: the template is in package mode");
                auto package = PackageSnapshot();

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Patch;
                CompileLog log;
                bool compiled = CompileWidget(session, options, log);
                Assert::IsTrue(compiled, WideForDestination(ErrorsOfLog(log)).c_str());

                for (const char *name : { templateCase.script, templateCase.heap, templateCase.text, templateCase.classTable, templateCase.selectorTable })
                {
                    if (*name)
                    {
                        Assert::IsTrue(GameHasFile(name), WideForDestination(std::string("no patch file ") + name + " in " + templateCase.folder).c_str());
                    }
                }
                Assert::IsTrue(package == PackageSnapshot(), WideForDestination(std::string("the package changed: ") + templateCase.folder).c_str());
            }
        }

        // Package in a patch-mode game writes the package. Default there
        // gives patch files, as the game says.
        TEST_METHOD(Package_InAPatchModeGame_WritesThePackage)
        {
            NoAppStateForDestination noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                SessionOptions sessionOptions;
                sessionOptions.dataFolder = GetTestModuleDirectory();
                GameSession session(sessionOptions);
                OpenCopy(templateCase, session);
                session.Helper().SetResourceSaveLocation(ResourceSaveLocation::Patch);
                auto package = PackageSnapshot();

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Package;
                CompileLog log;
                bool compiled = CompileWidget(session, options, log);
                Assert::IsTrue(compiled, WideForDestination(ErrorsOfLog(log)).c_str());
                Assert::IsFalse(GameHasFile(templateCase.script), WideForDestination(std::string("a patch file in ") + templateCase.folder).c_str());
                Assert::IsFalse(package == PackageSnapshot(), WideForDestination(std::string("the package did not change: ") + templateCase.folder).c_str());

                CompileLog defaultLog;
                compiled = CompileWidget(session, CompileWriteOptions(), defaultLog);
                Assert::IsTrue(compiled, WideForDestination(ErrorsOfLog(defaultLog)).c_str());
                Assert::IsTrue(GameHasFile(templateCase.script), WideForDestination(std::string("Default must follow the game's patch mode: ") + templateCase.folder).c_str());
            }
        }

        // An output folder gets the patch files; the game's resources do not
        // change, and src\ gets the .sco.
        TEST_METHOD(OutDir_WritesPatchFilesThere_GameUnchanged)
        {
            NoAppStateForDestination noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                SessionOptions sessionOptions;
                sessionOptions.dataFolder = GetTestModuleDirectory();
                GameSession session(sessionOptions);
                OpenCopy(templateCase, session);
                std::string outDir = _copyFolder + "\\out";
                fs::create_directory(outDir);
                fs::remove(session.Helper().GetScriptObjectFileName("S1Widget"));
                auto package = PackageSnapshot();

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Patch;
                options.outDir = outDir;
                CompileLog log;
                bool compiled = CompileWidget(session, options, log);
                Assert::IsTrue(compiled, WideForDestination(ErrorsOfLog(log)).c_str());

                for (const char *name : { templateCase.script, templateCase.heap, templateCase.text, templateCase.classTable, templateCase.selectorTable })
                {
                    if (*name)
                    {
                        Assert::IsTrue(fs::exists(fs::path(outDir) / name), WideForDestination(std::string("no ") + name + " in the output folder").c_str());
                        Assert::IsFalse(GameHasFile(name), WideForDestination(std::string("a patch file in the game: ") + name).c_str());
                    }
                }
                // The patch header: the type with the high bit, then 0.
                std::vector<uint8_t> script = ReadAllBytes((fs::path(outDir) / templateCase.script).string());
                Assert::IsTrue((script.size() > 2) && (script[0] == (0x80 | (int)ResourceType::Script)) && (script[1] == 0), L"the script patch file has the patch header");
                Assert::IsTrue(package == PackageSnapshot(), L"the package changed");
                Assert::IsTrue(fs::exists(session.Helper().GetScriptObjectFileName("S1Widget")), L"the .sco goes to src\\");
            }
        }

        // A resource that cannot be written fails the compile, and the script
        // gets no .sco: the .sco describes the resources.
        TEST_METHOD(OutDir_AFailedWrite_NoObjectFile)
        {
            NoAppStateForDestination noAppState;
            SessionOptions sessionOptions;
            sessionOptions.dataFolder = GetTestModuleDirectory();
            GameSession session(sessionOptions);
            OpenCopy(Templates[0], session);
            std::string outDir = _copyFolder + "\\out";
            fs::create_directory(outDir);
            std::string sco = session.Helper().GetScriptObjectFileName("S1Widget");
            fs::remove(sco);
            std::string target = (fs::path(outDir) / Templates[0].script).string();
            {
                std::ofstream file(target.c_str(), std::ios::binary | std::ios::trunc);
                file << "an old script";
            }
            Assert::IsTrue(SetFileAttributesA(target.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            CompileWriteOptions options;
            options.saveTo = ResourceSaveLocation::Patch;
            options.outDir = outDir;
            CompileLog log;
            bool compiled = CompileWidget(session, options, log);
            // Writable again, so that the clean-up can remove the copy.
            SetFileAttributesA(target.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsFalse(compiled, L"the script file cannot be written");
            Assert::IsFalse(fs::exists(sco), L"no .sco for a script whose resource was not written");
        }

        // With raw, the output folder gets the plain data of each resource.
        TEST_METHOD(OutDirRaw_WritesThePlainData)
        {
            NoAppStateForDestination noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                SessionOptions sessionOptions;
                sessionOptions.dataFolder = GetTestModuleDirectory();
                GameSession session(sessionOptions);
                OpenCopy(templateCase, session);
                std::string outDir = _copyFolder + "\\raw";
                fs::create_directory(outDir);

                CompileWriteOptions options;
                options.saveTo = ResourceSaveLocation::Patch;
                options.outDir = outDir;
                options.raw = true;
                CompileLog log;
                std::vector<uint8_t> scriptBytes;
                std::vector<uint8_t> heapBytes;
                bool compiled = CompileWidget(session, options, log, &scriptBytes, &heapBytes);
                Assert::IsTrue(compiled, WideForDestination(ErrorsOfLog(log)).c_str());

                Assert::IsTrue(ReadAllBytes(outDir + "\\script.903.bin") == scriptBytes, L"script.903.bin must hold the script's bytes");
                if (*templateCase.heap)
                {
                    Assert::IsTrue(!heapBytes.empty() && (ReadAllBytes(outDir + "\\heap.903.bin") == heapBytes), L"heap.903.bin must hold the heap's bytes");
                }
                for (const char *name : { "text.903.bin", "vocab.996.bin", "vocab.997.bin" })
                {
                    Assert::IsTrue(fs::exists(fs::path(outDir) / name), WideForDestination(std::string("no ") + name).c_str());
                }
                Assert::IsFalse(fs::exists(fs::path(outDir) / templateCase.script), L"raw writes no patch file");
            }
        }

        // An output folder takes patch files, not the package.
        TEST_METHOD(OutDirWithPackage_IsAnError)
        {
            NoAppStateForDestination noAppState;
            SessionOptions sessionOptions;
            sessionOptions.dataFolder = GetTestModuleDirectory();
            GameSession session(sessionOptions);
            OpenCopy(Templates[0], session);
            auto package = PackageSnapshot();

            CompileWriteOptions options;
            options.saveTo = ResourceSaveLocation::Package;
            options.outDir = _copyFolder;
            CompileLog log;
            Assert::IsFalse(CompileWidget(session, options, log), L"an output folder with the package must fail");
            std::string errors = ErrorsOfLog(log);
            Assert::IsTrue(errors.find("output folder") != std::string::npos, WideForDestination(errors).c_str());
            Assert::IsTrue(package == PackageSnapshot(), L"the package changed");
        }

        // A dry run writes nothing: no resource, table, .sco or .scd.
        TEST_METHOD(DryRun_WritesNothing)
        {
            NoAppStateForDestination noAppState;
            for (const TemplateCase &templateCase : Templates)
            {
                SessionOptions sessionOptions;
                sessionOptions.dataFolder = GetTestModuleDirectory();
                GameSession session(sessionOptions);
                OpenCopy(templateCase, session);
                session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
                auto before = Snapshot(_copyFolder);

                CompileWriteOptions options;
                options.writeResources = false;
                options.writeObjectFile = false;
                options.writeDebugInfo = false;
                CompileLog log;
                bool compiled = CompileWidget(session, options, log);
                Assert::IsTrue(compiled, WideForDestination(ErrorsOfLog(log)).c_str());
                std::string differences = Differences(before, Snapshot(_copyFolder));
                Assert::IsTrue(differences.empty(), WideForDestination(differences).c_str());
            }
        }

        // The debug information goes to debug\903.scd when the game makes
        // it, and not with writeDebugInfo false.
        TEST_METHOD(DebugInfo_IsWrittenOnlyWhenAsked)
        {
            NoAppStateForDestination noAppState;
            SessionOptions sessionOptions;
            sessionOptions.dataFolder = GetTestModuleDirectory();
            GameSession session(sessionOptions);
            OpenCopy(Templates[1], session);
            session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
            std::string scd = _copyFolder + "\\debug\\903.scd";

            CompileWriteOptions noDebug;
            noDebug.writeDebugInfo = false;
            CompileLog log;
            Assert::IsTrue(CompileWidget(session, noDebug, log), WideForDestination(ErrorsOfLog(log)).c_str());
            Assert::IsFalse(fs::exists(scd), L"no .scd with writeDebugInfo false");

            CompileLog debugLog;
            std::vector<uint8_t> debugBytes;
            Assert::IsTrue(CompileWidget(session, CompileWriteOptions(), debugLog, nullptr, nullptr, &debugBytes), WideForDestination(ErrorsOfLog(debugLog)).c_str());
            Assert::IsFalse(debugBytes.empty(), L"setup: the game makes debug information");
            Assert::IsTrue(ReadAllBytes(scd) == debugBytes, L"debug\\903.scd must hold the debug information");
        }

        // A .sco that cannot be written is an error of the compile.
        TEST_METHOD(ObjectFile_ReadOnly_IsACompileError)
        {
            NoAppStateForDestination noAppState;
            SessionOptions sessionOptions;
            sessionOptions.dataFolder = GetTestModuleDirectory();
            GameSession session(sessionOptions);
            OpenCopy(Templates[0], session);
            std::string sco = session.Helper().GetScriptObjectFileName("S1Widget");
            {
                std::ofstream file(sco.c_str(), std::ios::binary | std::ios::trunc);
                file << "old";
            }
            Assert::IsTrue(!!SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_READONLY), L"setup: the .sco must be read-only");

            CompileLog log;
            Assert::IsFalse(CompileWidget(session, CompileWriteOptions(), log), L"the compile must fail when its .sco cannot be written");
            std::string errors = ErrorsOfLog(log);
            Assert::IsTrue(errors.find("S1Widget.sco") != std::string::npos, WideForDestination(errors).c_str());
            Assert::IsTrue(ReadAllBytes(sco) == std::vector<uint8_t>({ 'o', 'l', 'd' }), L"the read-only .sco must not change");
        }

        // The file writer gives NotFound for a missing folder, Io for a
        // read-only file, and CR LF for each line feed of a text.
        TEST_METHOD(FileWrite_GivesTheFailureAndKeepsTextMode)
        {
            _copyFolder = (fs::temp_directory_path() / ("S1FileWrite_" + std::to_string(GetCurrentProcessId()))).string();
            fs::create_directories(_copyFolder);

            sci::Status missing = WriteBytesToFile(_copyFolder + "\\no such folder\\a.bin", std::vector<uint8_t>({ 1, 2 }));
            Assert::IsFalse(missing.has_value());
            Assert::IsTrue(missing.error().code == sci::ErrorCode::NotFound, WideForDestination(missing.error().ToString()).c_str());

            std::string readOnly = _copyFolder + "\\readonly.bin";
            Assert::IsTrue(WriteBytesToFile(readOnly, std::vector<uint8_t>({ 1 })).has_value());
            SetFileAttributesA(readOnly.c_str(), FILE_ATTRIBUTE_READONLY);
            sci::Status denied = WriteBytesToFile(readOnly, std::vector<uint8_t>({ 2 }));
            Assert::IsFalse(denied.has_value());
            Assert::IsTrue(denied.error().code == sci::ErrorCode::Io, WideForDestination(denied.error().ToString()).c_str());
            Assert::IsTrue(denied.error().ToString().find("readonly.bin") != std::string::npos, L"the error names the file");

            std::string text = _copyFolder + "\\text.sc";
            Assert::IsTrue(WriteTextToFile(text, "a\nb\n").has_value());
            Assert::IsTrue(ReadAllBytes(text) == std::vector<uint8_t>({ 'a', '\r', '\n', 'b', '\r', '\n' }), L"text mode: CR LF for each line feed");
        }
    };
}
