#include "stdafx.h"
#include "CppUnitTest.h"
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
#include "TestSupport.h"
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
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
}

namespace UnitTests
{
    // The naming rule of the decompiler (rule 4 of plan section 3.4). The
    // scripts go in number order, so the "_N" suffix of a duplicate name does
    // not depend on the order of the input.
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
                Script(4, { Class("Voice-Over") }),
                Script(5, { Class("Con") }),
                Script(6, { Class("lpt1") }),
                Script(7, { Class("Console") }),
            });
            Assert::AreEqual(std::string("Door_door"), names[1]);
            Assert::AreEqual(std::string("_3dRoom"), names[2]);
            Assert::AreEqual(std::string("what__"), names[3]);
            // The parser takes no '-' in (use Voice-Over).
            Assert::AreEqual(std::string("Voice_Over"), names[4]);
            // Windows opens a device for CON.sc or LPT1.sc.
            Assert::AreEqual(std::string("Con_"), names[5]);
            Assert::AreEqual(std::string("lpt1_"), names[6]);
            Assert::AreEqual(std::string("Console"), names[7]);
        }
    };

    // The script-name map (plan section 3.4). The names come from game.ini
    // and from the files of src\, so the decompiler and the compiler use the
    // names of the script files also in a game with no game.ini.
    TEST_CLASS(TestScriptNameMap)
    {
        GameCopy _game;

        // The script-name map of the copy. An assert fails when the build
        // fails.
        ScriptNameMap BuildMap()
        {
            GameFolderHelper helper;
            helper.GameFolder = _game.Folder();
            sci::Result<ScriptNameMap> map = ScriptNameMap::Build(helper);
            return std::move(ValueOf(map));
        }

        // The [Script] names of game.ini: number to name.
        std::map<uint16_t, std::string> GameIniNames()
        {
            std::map<uint16_t, std::string> names;
            ScriptNameMap map = BuildMap();
            for (const auto &entry : map.Entries())
            {
                if (entry.second.source == NameSource::GameIni)
                {
                    names[entry.first] = entry.second.name;
                }
            }
            return names;
        }

    public:
        // With no game.ini, the names come from src\: the same file names as
        // with game.ini (ignoring case, as Windows file names do). Most
        // template scripts declare (script# SOME_DEFINE) with the define in
        // src\game.sh.
        TEST_METHOD(NoGameIni_TheSameNamesFromSrc)
        {
            for (const char *name : { TemplateSci0, TemplateSci11 })
            {
                _game.Make(name);
                std::map<uint16_t, std::string> reference = GameIniNames();
                Assert::IsTrue(reference.size() > 20, Wide(name).c_str());
                std::filesystem::remove(_game.Path("game.ini"));

                ScriptNameMap map = BuildMap();
                Assert::IsTrue(map.Conflicts().empty(), Wide(map.Conflicts().empty() ? std::string() : map.Conflicts()[0].text).c_str());
                std::string differences;
                int fromSource = 0;
                for (const auto &script : reference)
                {
                    if (Upper(map.NameOf(script.first)) != Upper(script.second))
                    {
                        differences += std::to_string(script.first) + ": game.ini " + script.second + ", src " + map.NameOf(script.first) + "\n";
                    }
                    // A script with a .sc file gets its name from it (rule 2).
                    if (std::filesystem::exists(_game.Src(script.second + ".sc")))
                    {
                        fromSource++;
                        if (map.SourceOf(script.first) != NameSource::Source)
                        {
                            differences += std::to_string(script.first) + ": the name does not come from " + script.second + ".sc\n";
                        }
                    }
                }
                Assert::IsTrue(differences.empty(), Wide(std::string(name) + ":\n" + differences).c_str());
                Assert::IsTrue(fromSource > 20, Wide(name).c_str());
            }
        }

        // Rule 3: a .sco file names a script that no .sc file declares. A .sc
        // name wins over a .sco name.
        TEST_METHOD(ScoFile_NamesAScriptWithNoSource)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            std::filesystem::remove(_game.Src("TitleScreen.sc"));
            std::filesystem::rename(_game.Src("Main.sco"), _game.Src("OtherMain.sco"));

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(std::string("TitleScreen"), map.NameOf(100));
            Assert::IsTrue(map.SourceOf(100) == NameSource::Sco);
            Assert::AreEqual(std::string("Main"), map.NameOf(0), L"Main.sc wins over OtherMain.sco");
            Assert::IsTrue(map.SourceOf(0) == NameSource::Source);
        }

        // Rule 1 wins over the files, also when they would conflict.
        TEST_METHOD(GameIni_WinsOverTheFiles)
        {
            _game.Make(TemplateSci11);
            std::filesystem::copy_file(_game.Src("TitleScreen.sc"), _game.Src("Title2.sc"));

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(std::string("TitleScreen"), map.NameOf(100));
            Assert::IsTrue(map.SourceOf(100) == NameSource::GameIni);
            Assert::IsTrue(map.Conflicts().empty(), L"game.ini names script 100, so the second file does not matter");
        }

        TEST_METHOD(TwoSourcesForOneScript_IsAConflict)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            std::filesystem::copy_file(_game.Src("TitleScreen.sc"), _game.Src("Title2.sc"));

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(size_t(1), map.Conflicts().size());
            const std::string &conflict = map.Conflicts()[0].text;
            Assert::IsTrue((conflict.find("Title2.sc") != std::string::npos) && (conflict.find("TitleScreen.sc") != std::string::npos), Wide(conflict).c_str());
            // The conflict names its script, and says how to fix it.
            Assert::IsTrue(map.Conflicts()[0].numbers == std::vector<uint16_t>({ 100 }));
            Assert::IsTrue(conflict.find("Keep one of the files") != std::string::npos, Wide(conflict).c_str());
            Assert::AreEqual(std::string("n100"), map.NameOf(100), L"neither file names the script");
        }

        TEST_METHOD(OneNameForTwoScripts_IsAConflict)
        {
            _game.Make(TemplateSci11);
            WritePrivateProfileStringA("Script", "n900", "main", _game.Path("game.ini").c_str());

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(size_t(1), map.Conflicts().size());
            const std::string &conflict = map.Conflicts()[0].text;
            Assert::IsTrue(conflict.find("scripts 0 (game.ini), 900 (game.ini)") != std::string::npos, Wide(conflict).c_str());
            Assert::IsTrue(conflict.find("in game.ini [Script]") != std::string::npos, Wide(conflict).c_str());
            Assert::IsTrue(map.Conflicts()[0].numbers == std::vector<uint16_t>({ 0, 900 }));
            Assert::AreEqual(size_t(1), map.ConflictsOf(900).size());
            Assert::AreEqual(size_t(0), map.ConflictsOf(100).size());
        }

        // game.ini gives a name only with the key that the GUI reads (n007:
        // GetPrivateProfileString finds a key by its text), and without
        // single or double quotes, as the GUI reads it.
        TEST_METHOD(GameIni_TheKeyAndTheValueAsTheGuiReadsThem)
        {
            _game.Make(TemplateSci11);
            std::string ini = _game.Path("game.ini");
            WritePrivateProfileStringA("Script", "n7", "S3Short", ini.c_str());
            WritePrivateProfileStringA("Script", "n0780", "S3Padded", ini.c_str());
            WritePrivateProfileStringA("Script", "n779", "'S3Quoted'", ini.c_str());

            ScriptNameMap map = BuildMap();
            Assert::AreNotEqual(std::string("S3Short"), map.NameOf(7), L"n7 is not the key of script 7");
            Assert::AreNotEqual(std::string("S3Padded"), map.NameOf(780), L"n0780 is not the key of script 780");
            Assert::AreEqual(std::string("S3Quoted"), map.NameOf(779));
        }

        // Rule 5 takes only the standard form of the default name.
        TEST_METHOD(DefaultName_OnlyTheStandardForm)
        {
            _game.Make(TemplateSci11);
            ScriptNameMap map = BuildMap();
            uint16_t number = 0;
            Assert::IsTrue(map.NumberOf("n7777", number) && (number == 7777));
            Assert::IsTrue(map.NumberOf("N7777", number) && (number == 7777), L"ignoring case");
            Assert::IsFalse(map.NumberOf("n07777", number));
            Assert::IsFalse(map.NumberOf("n1", number), L"the default name of script 1 is n001");
        }

        // Two .sco files for one script (rule 3) are a conflict.
        TEST_METHOD(TwoObjectFilesForOneScript_IsAConflict)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            std::filesystem::remove(_game.Src("TitleScreen.sc"));
            std::filesystem::copy_file(_game.Src("TitleScreen.sco"), _game.Src("Title2.sco"));

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(size_t(1), map.Conflicts().size());
            const std::string &conflict = map.Conflicts()[0].text;
            Assert::IsTrue(conflict.find("object file") != std::string::npos, Wide(conflict).c_str());
            Assert::IsTrue(map.Conflicts()[0].numbers == std::vector<uint16_t>({ 100 }));
            Assert::AreEqual(std::string("n100"), map.NameOf(100), L"neither file names the script");
        }

        // A script can declare its number with its own define.
        TEST_METHOD(ScriptDeclaration_WithTheScriptsOwnDefine)
        {
            _game.Make(TemplateSci11);
            WriteFileText(_game.Src("S3OwnDefine.sc"), "(define S3_OWN_NUMBER 7777)\n(script# S3_OWN_NUMBER)\n");
            ScriptNameMap map = BuildMap();
            Assert::AreEqual(std::string("S3OwnDefine"), map.NameOf(7777));
            Assert::IsTrue(map.SourceOf(7777) == NameSource::Source);
        }

        // Windows file names ignore the case of letters outside ASCII too, so
        // "Über" and "über" are one file: a conflict.
        TEST_METHOD(NamesThatDifferOnlyInTheCaseOfALetterOutsideAscii_AreAConflict)
        {
            if (GetACP() != 1252)
            {
                // The names are code page 1252 bytes, so the test skips on
                // another code page.
                Logger::WriteMessage(L"skipped: the ANSI code page is not 1252");
                return;
            }
            _game.Make(TemplateSci11);
            std::string ini = _game.Path("game.ini");
            WritePrivateProfileStringA("Script", "n777", "\xDC" "ber", ini.c_str());
            WritePrivateProfileStringA("Script", "n778", "\xFC" "ber", ini.c_str());

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(size_t(1), map.Conflicts().size());
            Assert::IsTrue(map.Conflicts()[0].numbers == std::vector<uint16_t>({ 777, 778 }));
        }

        // The [Script] readers of CResourceMap have a buffer of 20000
        // characters, and a longer section gives them no names at all. The
        // map's reader grows its buffer.
        TEST_METHOD(GameIni_ALongScriptSection_IsReadInFull)
        {
            _game.Make(TemplateSci11);
            std::string ini = "[Script]\r\n";
            for (int number = 1000; number < 4000; number++)
            {
                ini += "n" + std::to_string(number) + "=AScriptWithALongName" + std::to_string(number) + "\r\n";
            }
            WriteFileText(_game.Path("game.ini"), ini);
            Assert::IsTrue(ini.size() > 60000);

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(std::string("AScriptWithALongName1000"), map.NameOf(1000));
            Assert::AreEqual(std::string("AScriptWithALongName3999"), map.NameOf(3999));
            Assert::AreEqual(size_t(3000), map.GameIniOrder().size());
        }

        // A (script# N) in a comment or in a string is not the declaration.
        TEST_METHOD(ScriptDeclaration_InACommentOrAString_IsNotRead)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            WriteFileText(_game.Src("Declared.sc"), "; (script# 1201)\r\n"
                "(define TEXT {(script# 1202)})\r\n"
                "(define QUOTED \"(script# 1203)\")\r\n"
                "(script# 1204)\r\n");

            ScriptNameMap map = BuildMap();
            Assert::AreEqual(std::string("Declared"), map.NameOf(1204));
            Assert::AreEqual(std::string("n1201"), map.NameOf(1201));
            Assert::AreEqual(std::string("n1202"), map.NameOf(1202));
            Assert::AreEqual(std::string("n1203"), map.NameOf(1203));
        }

        TEST_METHOD(DerivedAndDefaultNames)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            ScriptNameMap map = BuildMap();
            Assert::AreEqual(std::string("n1234"), map.NameOf(1234), L"rule 5");
            Assert::IsTrue(map.SourceOf(1234) == NameSource::Default);

            map.AddDerivedNames({ { 1234, "Derived1234" }, { 0, "NotMain" } });
            Assert::AreEqual(std::string("Derived1234"), map.NameOf(1234));
            Assert::IsTrue(map.SourceOf(1234) == NameSource::Derived);
            Assert::AreEqual(std::string("Main"), map.NameOf(0), L"a derived name does not replace a name from the files");

            uint16_t number = 0;
            Assert::IsTrue(map.NumberOf("titlescreen", number), L"a name is found ignoring case");
            Assert::AreEqual(100, (int)number);
            Assert::IsTrue(map.NumberOf("N1233", number), L"nNNN names a script with no other name");
            Assert::AreEqual(1233, (int)number);
            Assert::IsFalse(map.NumberOf("n000", number), L"script 0 has another name");
            Assert::IsFalse(map.NumberOf("NoSuchScript", number));
        }
    };

    // A GameSession gives its helper the script names.
    TEST_CLASS(TestSessionScriptNames)
    {
        NoAppState _noAppState;
        GameCopy _game;

    public:
        // With no game.ini, the file names come from src\.
        TEST_METHOD(NoGameIni_TheHelperNamesTheFilesFromSrc)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            GameSession &session = _game.Open(SessionOptions());

            Assert::AreEqual(_game.Src("Main.sc"), session.Helper().GetScriptFileName((uint16_t)0));
            Assert::AreEqual(_game.Src("TitleScreen.sco"), session.Helper().GetScriptObjectFileName((uint16_t)100));
            std::unordered_map<WORD, std::string> numberToName;
            session.ResourceMap().GetNumberToNameMap(numberToName);
            Assert::AreEqual(std::string("Controls"), numberToName[255], L"the compiler's number-to-name map");
            Assert::IsFalse(_game.Has("game.ini"), L"the open writes no game.ini");
        }

        // An open replaces the names of the game that was open before.
        TEST_METHOD(Reopen_ReplacesTheNames)
        {
            GameCopy sci0;
            sci0.Make(TemplateSci0);
            _game.Make(TemplateSci11);
            GameSession session;
            AssertOk(session.Open(sci0.Folder()));
            Assert::AreEqual(std::string("Door"), session.Helper().GetScriptTitle(974));
            AssertOk(session.Open(_game.Folder()));
            Assert::AreEqual(std::string("n974"), session.Helper().GetScriptTitle(974), L"the SCI1.1 template has no script 974");

            // An open of the resource map alone (as the GUI opens a game)
            // clears the names; the helper then reads game.ini.
            AssertOk(session.Open(sci0.Folder()));
            AssertOk(session.ResourceMap().TryOpen(_game.Folder()));
            Assert::IsTrue(session.Helper().ScriptNames == nullptr, L"the names of the game before are gone");
            Assert::AreEqual(std::string("n974"), session.Helper().GetScriptTitle(974));
        }

        // A compile in a session with no game.ini finds the script by its
        // src\ name, and does not create game.ini. The compiled blobs take
        // their names from game.ini only (FigureOutName), because the
        // resource map writes a blob's name into game.ini (and creates the
        // file) when it saves the blob.
        TEST_METHOD(Compile_NoGameIni_CreatesNoGameIni)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            GameSession &session = _game.Open();

            std::string path = session.Helper().GetScriptFileName((uint16_t)100);
            Assert::IsTrue(std::filesystem::exists(path), Wide(path).c_str());
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
            Assert::IsTrue(compiled && errors.empty(), Wide(errors).c_str());
            Assert::IsFalse(_game.Has("game.ini"), L"a compile must not create game.ini");
        }

        // A decompile with no game.ini writes src\<name>.sc with the names of
        // src\, and its (use ...) lines use them: src\TitleScreen.sc and
        // (use Main), not src\n100.sc and (use n000).
        TEST_METHOD(Decompile_NoGameIni_UsesTheNamesOfSrc)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            GameSession &session = _game.Open();
            std::string path = _game.Src("TitleScreen.sc");
            std::filesystem::remove(path);

            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(session.Helper()));
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(session.ResourceMap(), lookups.GetSelectorTable());
            TestDecompilerResults decompilerResults;
            DecompileBatch batch(config.get(), lookups, session.ResourceMap(), decompilerResults);
            batch.Run({ 100 });

            Assert::IsTrue(std::filesystem::exists(path), L"the decompile writes src\\TitleScreen.sc");
            std::string text = ReadFileText(path);
            Assert::IsTrue(text.find("(use Main)") != std::string::npos, Wide(text.substr(0, 400)).c_str());
            Assert::IsTrue(text.find("(use n000)") == std::string::npos);
            Assert::IsFalse(_game.Has("src\\n100.sc"));
            Assert::IsFalse(_game.Has("game.ini"), L"a decompile must not create game.ini");
        }
    };
}
