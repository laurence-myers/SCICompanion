#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "ScriptNameMap.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompiledScript.h"
#include "ScriptOMAll.h"
#include "DecompilerConfig.h"
#include "DecompileBatch.h"
#include "DecompileHelper.h"
#include "Helper.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    // Runs a test with no AppState, as the command line does.
    struct NoAppStateForNames
    {
        AppState *saved;
        NoAppStateForNames() : saved(appState) { appState = nullptr; }
        ~NoAppStateForNames() { appState = saved; }
    };

    std::wstring WideName(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::string UpperName(std::string text)
    {
        for (char &ch : text)
        {
            ch = (char)std::toupper((unsigned char)ch);
        }
        return text;
    }

    ScriptObjectsForNaming Script(uint16_t number, std::vector<ScriptObjectsForNaming::Object> objects)
    {
        ScriptObjectsForNaming script;
        script.number = number;
        script.objects = std::move(objects);
        return script;
    }

    ScriptObjectsForNaming::Object Class(const std::string &name)
    {
        return { name, true, false };
    }

    ScriptObjectsForNaming::Object Instance(const std::string &name, bool isPublic)
    {
        return { name, false, isPublic };
    }

    // The [Script] names of game.ini: number to name.
    std::map<uint16_t, std::string> GameIniNames(const std::string &gameFolder)
    {
        GameFolderHelper helper;
        helper.GameFolder = gameFolder;
        std::map<uint16_t, std::string> names;
        sci::Result<ScriptNameMap> map = ScriptNameMap::Build(helper);
        Assert::IsTrue(map.has_value());
        for (const auto &entry : map->Entries())
        {
            if (entry.second.source == NameSource::GameIni)
            {
                names[entry.first] = entry.second.name;
            }
        }
        return names;
    }

    std::string ReadText(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }
}

namespace UnitTests
{
    // Plan step S3: the naming rule of the decompiler (rule 4 of plan
    // section 3.4). Before S3, the Decompile dialog had the rule inline, and
    // it went through the scripts in the hash order of the class table: the
    // "_N" suffix of a duplicate name could go to either script.
    TEST_CLASS(TestSuggestScriptNames)
    {
    public:
        TEST_METHOD(TheRule_MainFirstClassGamePublicInstance)
        {
            std::map<uint16_t, std::string> names = SuggestScriptNames({
                Script(0, { Class("Foo") }),
                Script(5, { Class("Actor"), Class("Game"), Class("Other") }),
                Script(6, { Instance("theRoom", true), Class("Room") }),
                Script(7, { Instance("hidden", false) }),
                Script(8, { Instance("hidden", false), Instance("rm8", true), Instance("second", true) }),
                Script(9, {}),
            });
            Assert::AreEqual(std::string("Main"), names[0]);
            Assert::AreEqual(std::string("Game"), names[5], L"a class named Game wins");
            Assert::AreEqual(std::string("Room"), names[6], L"a class wins over a public instance");
            Assert::IsTrue(names.find(7) == names.end(), L"no class and no public instance: no name");
            Assert::AreEqual(std::string("rm8"), names[8], L"the first public instance");
            Assert::IsTrue(names.find(9) == names.end());
        }

        TEST_METHOD(Duplicates_TheSuffixFollowsTheNumber)
        {
            // The same scripts in two orders give the same names.
            std::vector<ScriptObjectsForNaming> descending = { Script(30, { Class("Door") }), Script(20, { Class("door") }), Script(10, { Class("Door") }) };
            std::vector<ScriptObjectsForNaming> mixed = { Script(20, { Class("door") }), Script(10, { Class("Door") }), Script(30, { Class("Door") }) };
            for (const auto &input : { descending, mixed })
            {
                std::map<uint16_t, std::string> names = SuggestScriptNames(input);
                Assert::AreEqual(std::string("Door"), names[10], L"the lowest number keeps the name");
                Assert::AreEqual(std::string("door_20"), names[20], L"case is ignored");
                Assert::AreEqual(std::string("Door_30"), names[30]);
            }
        }

        TEST_METHOD(ReservedNamesAndSuffixedNames_AreUsed)
        {
            std::map<uint16_t, std::string> names = SuggestScriptNames({
                Script(3, { Class("Foo") }),
                Script(5, { Class("Foo_12") }),
                Script(12, { Class("Foo") }),
                Script(40, { Class("Room") }),
            }, { "ROOM" });
            Assert::AreEqual(std::string("Foo"), names[3]);
            Assert::AreEqual(std::string("Foo_12"), names[5]);
            Assert::AreEqual(std::string("Foo_12_2"), names[12], L"a suffixed name that is taken gets one more suffix");
            Assert::AreEqual(std::string("Room_40"), names[40], L"a reserved name is taken");
        }

        TEST_METHOD(NamesThatCannotBeAFileOrAToken_AreCleaned)
        {
            std::map<uint16_t, std::string> names = SuggestScriptNames({
                Script(1, { Class("Door door") }),
                Script(2, { Class("3dRoom") }),
                Script(3, { Class("what?#") }),
            });
            Assert::AreEqual(std::string("Door_door"), names[1]);
            Assert::AreEqual(std::string("_3dRoom"), names[2]);
            Assert::AreEqual(std::string("what__"), names[3]);
        }
    };

    // Plan step S3: the script-name map (plan section 3.4). Before S3, every
    // script name came from game.ini [Script]; with no game.ini, every
    // script was nNNN, so the decompiler wrote src\n110.sc and (use n255),
    // and the compiler found no script file.
    TEST_CLASS(TestScriptNameMap)
    {
        std::string _copyFolder;

        void RemoveCopy()
        {
            if (!_copyFolder.empty())
            {
                std::error_code ec;
                std::filesystem::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

        std::string SrcFile(const std::string &name) const
        {
            return _copyFolder + "\\src\\" + name;
        }

        sci::Result<ScriptNameMap> BuildForCopy()
        {
            GameFolderHelper helper;
            helper.GameFolder = _copyFolder;
            return ScriptNameMap::Build(helper);
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        // With no game.ini, the names come from src\: the same file names as
        // with game.ini (ignoring case, as Windows file names do). Most
        // template scripts declare (script# SOME_DEFINE) with the define in
        // src\game.sh.
        TEST_METHOD(NoGameIni_TheSameNamesFromSrc)
        {
            const char *templates[] = { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" };
            for (const char *name : templates)
            {
                _copyFolder = CopyGameFromModuleFolder(name);
                std::map<uint16_t, std::string> reference = GameIniNames(_copyFolder);
                Assert::IsTrue(reference.size() > 20, WideName(name).c_str());
                std::filesystem::remove(_copyFolder + "\\game.ini");

                sci::Result<ScriptNameMap> map = BuildForCopy();
                Assert::IsTrue(map.has_value());
                Assert::IsTrue(map->Conflicts().empty(), WideName(map->Conflicts().empty() ? std::string() : map->Conflicts()[0]).c_str());
                std::string differences;
                int fromSource = 0;
                for (const auto &script : reference)
                {
                    if (UpperName(map->NameOf(script.first)) != UpperName(script.second))
                    {
                        differences += std::to_string(script.first) + ": game.ini " + script.second + ", src " + map->NameOf(script.first) + "\n";
                    }
                    // A script with a .sc file gets its name from it (rule 2).
                    if (std::filesystem::exists(SrcFile(script.second + ".sc")))
                    {
                        fromSource++;
                        if (map->SourceOf(script.first) != NameSource::Source)
                        {
                            differences += std::to_string(script.first) + ": the name does not come from " + script.second + ".sc\n";
                        }
                    }
                }
                Assert::IsTrue(differences.empty(), WideName(std::string(name) + ":\n" + differences).c_str());
                Assert::IsTrue(fromSource > 20, WideName(name).c_str());
                RemoveCopy();
            }
        }

        // Rule 3: a .sco file names a script that no .sc file declares. A .sc
        // name wins over a .sco name.
        TEST_METHOD(ScoFile_NamesAScriptWithNoSource)
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(_copyFolder + "\\game.ini");
            std::filesystem::remove(SrcFile("TitleScreen.sc"));
            std::filesystem::rename(SrcFile("Main.sco"), SrcFile("OtherMain.sco"));

            sci::Result<ScriptNameMap> map = BuildForCopy();
            Assert::IsTrue(map.has_value());
            Assert::AreEqual(std::string("TitleScreen"), map->NameOf(100));
            Assert::IsTrue(map->SourceOf(100) == NameSource::Sco);
            Assert::AreEqual(std::string("Main"), map->NameOf(0), L"Main.sc wins over OtherMain.sco");
            Assert::IsTrue(map->SourceOf(0) == NameSource::Source);
        }

        // Rule 1 wins over the files, also when they would conflict.
        TEST_METHOD(GameIni_WinsOverTheFiles)
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::copy_file(SrcFile("TitleScreen.sc"), SrcFile("Title2.sc"));

            sci::Result<ScriptNameMap> map = BuildForCopy();
            Assert::IsTrue(map.has_value());
            Assert::AreEqual(std::string("TitleScreen"), map->NameOf(100));
            Assert::IsTrue(map->SourceOf(100) == NameSource::GameIni);
            Assert::IsTrue(map->Conflicts().empty(), L"game.ini names script 100, so the second file does not matter");
        }

        TEST_METHOD(TwoSourcesForOneScript_IsAConflict)
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(_copyFolder + "\\game.ini");
            std::filesystem::copy_file(SrcFile("TitleScreen.sc"), SrcFile("Title2.sc"));

            sci::Result<ScriptNameMap> map = BuildForCopy();
            Assert::IsTrue(map.has_value());
            Assert::AreEqual(size_t(1), map->Conflicts().size());
            const std::string &conflict = map->Conflicts()[0];
            Assert::IsTrue((conflict.find("Title2.sc") != std::string::npos) && (conflict.find("TitleScreen.sc") != std::string::npos), WideName(conflict).c_str());
            Assert::AreEqual(std::string("n100"), map->NameOf(100), L"neither file names the script");
        }

        TEST_METHOD(OneNameForTwoScripts_IsAConflict)
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            WritePrivateProfileStringA("Script", "n900", "main", (_copyFolder + "\\game.ini").c_str());

            sci::Result<ScriptNameMap> map = BuildForCopy();
            Assert::IsTrue(map.has_value());
            Assert::AreEqual(size_t(1), map->Conflicts().size());
            Assert::IsTrue(map->Conflicts()[0].find("scripts 0 (game.ini), 900 (game.ini)") != std::string::npos, WideName(map->Conflicts()[0]).c_str());
        }

        // The old [Script] readers used a buffer of 20000 characters, and a
        // longer section gave no names at all. The map's reader grows its
        // buffer.
        TEST_METHOD(GameIni_ALongScriptSection_IsReadInFull)
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::string ini = "[Script]\r\n";
            for (int number = 1000; number < 4000; number++)
            {
                ini += "n" + std::to_string(number) + "=AScriptWithALongName" + std::to_string(number) + "\r\n";
            }
            {
                std::ofstream file(_copyFolder + "\\game.ini", std::ios::binary | std::ios::trunc);
                file << ini;
            }
            Assert::IsTrue(ini.size() > 60000);

            sci::Result<ScriptNameMap> map = BuildForCopy();
            Assert::IsTrue(map.has_value());
            Assert::AreEqual(std::string("AScriptWithALongName1000"), map->NameOf(1000));
            Assert::AreEqual(std::string("AScriptWithALongName3999"), map->NameOf(3999));
            Assert::AreEqual(size_t(3000), map->GameIniOrder().size());
        }

        // A (script# N) in a comment or in a string is not the declaration.
        TEST_METHOD(ScriptDeclaration_InACommentOrAString_IsNotRead)
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(_copyFolder + "\\game.ini");
            {
                std::ofstream file(SrcFile("Declared.sc"), std::ios::binary | std::ios::trunc);
                file << "; (script# 1201)\r\n"
                    "(define TEXT {(script# 1202)})\r\n"
                    "(define QUOTED \"(script# 1203)\")\r\n"
                    "(script# 1204)\r\n";
            }

            sci::Result<ScriptNameMap> map = BuildForCopy();
            Assert::IsTrue(map.has_value());
            Assert::AreEqual(std::string("Declared"), map->NameOf(1204));
            Assert::AreEqual(std::string("n1201"), map->NameOf(1201));
            Assert::AreEqual(std::string("n1202"), map->NameOf(1202));
            Assert::AreEqual(std::string("n1203"), map->NameOf(1203));
        }

        TEST_METHOD(DerivedAndDefaultNames)
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(_copyFolder + "\\game.ini");
            sci::Result<ScriptNameMap> map = BuildForCopy();
            Assert::IsTrue(map.has_value());
            Assert::AreEqual(std::string("n1234"), map->NameOf(1234), L"rule 5");
            Assert::IsTrue(map->SourceOf(1234) == NameSource::Default);

            map->AddDerivedNames({ { 1234, "Derived1234" }, { 0, "NotMain" } });
            Assert::AreEqual(std::string("Derived1234"), map->NameOf(1234));
            Assert::IsTrue(map->SourceOf(1234) == NameSource::Derived);
            Assert::AreEqual(std::string("Main"), map->NameOf(0), L"a derived name does not replace a name from the files");

            uint16_t number = 0;
            Assert::IsTrue(map->NumberOf("titlescreen", number), L"a name is found ignoring case");
            Assert::AreEqual(100, (int)number);
            Assert::IsTrue(map->NumberOf("N1233", number), L"nNNN names a script with no other name");
            Assert::AreEqual(1233, (int)number);
            Assert::IsFalse(map->NumberOf("n000", number), L"script 0 has another name");
            Assert::IsFalse(map->NumberOf("NoSuchScript", number));
        }
    };

    // Plan step S3: a GameSession gives its helper the script names.
    TEST_CLASS(TestSessionScriptNames)
    {
        std::string _copyFolder;

        void RemoveCopy()
        {
            if (!_copyFolder.empty())
            {
                std::error_code ec;
                std::filesystem::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

        std::string GameIni() const
        {
            return _copyFolder + "\\game.ini";
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        // With no game.ini, the file names come from src\. Before S3 they were
        // src\n000.sc and src\n000.sco.
        TEST_METHOD(NoGameIni_TheHelperNamesTheFilesFromSrc)
        {
            NoAppStateForNames noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(GameIni());
            GameSession session;
            sci::Status opened = session.Open(_copyFolder);
            Assert::IsTrue(opened.has_value(), WideName(opened ? std::string() : opened.error().ToString()).c_str());

            Assert::AreEqual(_copyFolder + "\\src\\Main.sc", session.Helper().GetScriptFileName((uint16_t)0));
            Assert::AreEqual(_copyFolder + "\\src\\TitleScreen.sco", session.Helper().GetScriptObjectFileName((uint16_t)100));
            std::unordered_map<WORD, std::string> numberToName;
            session.ResourceMap().GetNumberToNameMap(numberToName);
            Assert::AreEqual(std::string("Controls"), numberToName[255], L"the compiler's number-to-name map");
            Assert::IsFalse(std::filesystem::exists(GameIni()), L"the open writes no game.ini");
        }

        // An open replaces the names of the game that was open before.
        TEST_METHOD(Reopen_ReplacesTheNames)
        {
            NoAppStateForNames noAppState;
            std::string sci0 = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            GameSession session;
            Assert::IsTrue(session.Open(sci0).has_value());
            Assert::AreEqual(std::string("Door"), session.Helper().GetScriptTitle(974));
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            Assert::AreEqual(std::string("n974"), session.Helper().GetScriptTitle(974), L"the SCI1.1 template has no script 974");

            // An open of the resource map alone (as the GUI opens a game)
            // clears the names; the helper then reads game.ini.
            Assert::IsTrue(session.Open(sci0).has_value());
            Assert::IsTrue(session.ResourceMap().TryOpen(_copyFolder).has_value());
            Assert::IsTrue(session.Helper().ScriptNames == nullptr, L"the names of the game before are gone");
            Assert::AreEqual(std::string("n974"), session.Helper().GetScriptTitle(974));
            std::error_code ec;
            std::filesystem::remove_all(sci0, ec);
        }

        // A compile in a session with no game.ini finds the script by its
        // src\ name, and does not create game.ini. The compiled blobs take
        // their names from game.ini only (FigureOutName), because the
        // resource map writes a blob's name into game.ini (and creates the
        // file) when it saves the blob.
        TEST_METHOD(Compile_NoGameIni_CreatesNoGameIni)
        {
            NoAppStateForNames noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(GameIni());
            SessionOptions options;
            options.dataFolder = GetTestModuleDirectory();
            GameSession session(options);
            Assert::IsTrue(session.Open(_copyFolder).has_value());

            std::string path = session.Helper().GetScriptFileName((uint16_t)100);
            Assert::IsTrue(std::filesystem::exists(path), WideName(path).c_str());
            ScriptId scriptId(path.c_str());
            scriptId.SetResourceNumber(100);
            CompileLog log;
            CompileTables tables;
            Assert::IsTrue(tables.Load(session.ResourceMap()));
            PrecompiledHeaders headers(session.ResourceMap());
            CompileResults results(log, session.Version());
            bool compiled = NewCompileScript(session, results, log, tables, headers, scriptId);
            std::string errors;
            for (const CompileResult &result : log.Results())
            {
                if (result.IsError())
                {
                    errors += result.GetMessage() + "\n";
                }
            }
            Assert::IsTrue(compiled && errors.empty(), WideName(errors).c_str());
            Assert::IsFalse(std::filesystem::exists(GameIni()), L"a compile must not create game.ini");
        }

        // A decompile with no game.ini writes src\<name>.sc with the names of
        // src\, and its (use ...) lines use them. Before S3: src\n100.sc and
        // (use n000).
        TEST_METHOD(Decompile_NoGameIni_UsesTheNamesOfSrc)
        {
            NoAppStateForNames noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(GameIni());
            SessionOptions options;
            options.dataFolder = GetTestModuleDirectory();
            GameSession session(options);
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            std::string path = _copyFolder + "\\src\\TitleScreen.sc";
            std::filesystem::remove(path);

            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(session.Helper()));
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(session.ResourceMap(), lookups.GetSelectorTable());
            TestDecompilerResults decompilerResults;
            DecompileBatch batch(config.get(), lookups, session.ResourceMap(), decompilerResults);
            batch.Run({ 100 });

            Assert::IsTrue(std::filesystem::exists(path), L"the decompile writes src\\TitleScreen.sc");
            std::string text = ReadText(path);
            Assert::IsTrue(text.find("(use Main)") != std::string::npos, WideName(text.substr(0, 400)).c_str());
            Assert::IsTrue(text.find("(use n000)") == std::string::npos);
            Assert::IsFalse(std::filesystem::exists(_copyFolder + "\\src\\n100.sc"));
            Assert::IsFalse(std::filesystem::exists(GameIni()), L"a decompile must not create game.ini");
        }
    };
}
