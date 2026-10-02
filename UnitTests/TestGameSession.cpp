#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "GameSession.h"
#include "CoreLog.h"
#include "ResourceMap.h"
#include "GameFolderHelper.h"
#include "DependencyTracker.h"
#include "ScriptOM.h"
#include "Codec.h"
#include "Helper.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "Text.h"
#include "CompiledScript.h"
#include "DecompilerConfig.h"
#include "DecompileBatch.h"
#include "DecompilerCore.h"
#include "DecompileScript.h"
#include "DecompileHelper.h"
#include "OutputCodeHelper.h"
#include "TestSupport.h"
#include "Pic.h"
#include "ResourceEntity.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "PaletteOperations.h"
#include "View.h"
#include "crc.h"
#include <set>
#include <sstream>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // A sink that keeps what it gets.
    class CaptureLogSink : public ILogSink
    {
    public:
        void Write(LogLevel level, const std::string &text) override
        {
            std::lock_guard<std::mutex> lock(_mutex);
            lines.emplace_back(level, text);
        }
        std::vector<std::pair<LogLevel, std::string>> lines;

    private:
        std::mutex _mutex;
    };

    // The engine works with no AppState and no dialog:
    //  - GameSession::Open gives an error with a code and a text;
    //  - with no GUI, SafeMessageBox writes the whole text to the core log,
    //    and gives the safe answer;
    //  - the codecs log through the core log, also with no AppState;
    //  - DependencyTracker follows the setting after its construction.
    TEST_CLASS(TestGameSession)
    {
        std::string _gameFolder;    // From SetUpGame: CleanUpGame also deletes its AppState.
        GameCopy _game;             // A copy with no AppState.

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // The core fills its tables when the program starts, not the
        // AppState: scic has none.
        TEST_METHOD(CoreTables_FilledAtTheStart)
        {
            Assert::IsTrue(memcmp(g_egaColorsExtended + 240, g_egaColors, sizeof(g_egaColors)) == 0, L"g_egaColorsExtended");
            Assert::IsTrue(g_egaColorsMixed[0xff].rgbBlue > 0xf0, L"g_egaColorsMixed: white with white");
            Assert::IsTrue(memcmp(g_egaDummyPalette.Colors, g_egaColors, sizeof(g_egaColors)) == 0, L"g_egaDummyPalette");
            Assert::AreEqual(255, (int)g_vgaPaletteMapping[255], L"g_vgaPaletteMapping");
            unsigned char data[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
            Assert::AreNotEqual(0u, (unsigned)crcFast(data, (int)sizeof(data)), L"the CRC table");
        }

        TEST_METHOD(Open_Templates_WorkWithNoAppState)
        {
            NoAppState noAppState;
            const std::pair<const char *, ResourceMapFormat> templates[] =
            {
                { TemplateSci0, ResourceMapFormat::SCI0 },
                { TemplateSci11, ResourceMapFormat::SCI11 },
            };
            for (const auto &entry : templates)
            {
                _game.Make(entry.first);
                GameSession session;
                AssertOk(session.Open(_game.Folder()));
                Assert::IsTrue(session.ResourceMap().IsGameLoaded());
                Assert::AreEqual(_game.Folder(), session.Helper().GameFolder);
                Assert::IsTrue(session.Version().MapFormat == entry.second, Wide(entry.first).c_str());
            }
        }

        TEST_METHOD(Open_EmptyFolder_IsAUsageError)
        {
            NoAppState noAppState;
            GameSession session;
            sci::Status opened = session.Open("");
            Assert::IsFalse(opened.has_value(), L"an empty folder is not a game");
            Assert::AreEqual(std::string("usage"), std::string(sci::ErrorCodeName(opened.error().code)));
            Assert::IsFalse(session.ResourceMap().IsGameLoaded());
        }

        TEST_METHOD(HeadlessAppState_DoesNotTakeTheSink)
        {
            ILogSink *before = GetCoreLogSink();
            {
                CaptureLogSink sink;
                ScopedCoreLogSink scoped(sink);
                // An AppState with no app (as in the tests) has no log file.
                _gameFolder = SetUpGameSCI0();
                Assert::IsTrue(GetCoreLogSink() == &sink, L"the AppState must not take the sink");
                CoreLog(LogLevel::Info, "after the AppState");
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
                Assert::IsTrue(GetCoreLogSink() == &sink, L"deleting the AppState must not remove another sink");
                Assert::AreEqual(size_t(1), sink.lines.size());
            }
            Assert::IsTrue(GetCoreLogSink() == before, L"the scope puts back the sink before it");
        }

        TEST_METHOD(Open_NoResourceMap_ReturnsNotFoundThatNamesTheFile)
        {
            NoAppState noAppState;
            _game.Make(TemplateSci0);
            std::filesystem::remove(_game.Path("resource.map"));

            GameSession session;
            sci::Status opened = session.Open(_game.Folder());

            Assert::IsFalse(opened.has_value(), L"a folder with no resource.map must not open");
            Assert::AreEqual(std::string("not-found"), std::string(sci::ErrorCodeName(opened.error().code)));
            std::string text = opened.error().ToString();
            Assert::IsTrue(text.find("resource.map") != std::string::npos, Wide(text).c_str());
            Assert::IsFalse(session.ResourceMap().IsGameLoaded(), L"after a failed open, no game is open");
        }

        // A damaged or empty resource.map does not open: Format, and no game
        // is open. Empty, garbage, and an SCI1.1 map cut in half (its
        // lookup table is whole, and its first entries are good).
        TEST_METHOD(Open_DamagedResourceMap_IsAFormatError)
        {
            NoAppState noAppState;
            const std::vector<std::pair<const char *, std::string>> cases = {
                { TemplateSci0, "empty" },
                { TemplateSci11, "empty" },
                { TemplateSci0, "garbage" },
                { TemplateSci11, "garbage" },
                { TemplateSci11, "half" },
            };
            for (const auto &entry : cases)
            {
                _game.Make(entry.first);
                std::string map = _game.Path("resource.map");
                std::string bytes = ReadFileText(map);
                if (entry.second == "empty")
                {
                    bytes.clear();
                }
                else if (entry.second == "half")
                {
                    bytes.resize(bytes.size() / 2);
                }
                else
                {
                    uint32_t value = 12345;
                    for (char &ch : bytes)
                    {
                        value = value * 1103515245 + 12345;
                        ch = (char)(value >> 16);
                    }
                }
                WriteFileText(map, bytes);
                std::string what = std::string(entry.first) + " " + entry.second;
                {
                    GameSession session;
                    sci::Status opened = session.Open(_game.Folder());
                    Assert::IsFalse(opened.has_value(), Wide(what).c_str());
                    Assert::AreEqual(std::string("format"), std::string(sci::ErrorCodeName(opened.error().code)), Wide(what + ": " + opened.error().ToString()).c_str());
                    Assert::IsTrue(opened.error().ToString().find("resource.map") != std::string::npos, Wide(opened.error().ToString()).c_str());
                    Assert::IsFalse(session.ResourceMap().IsGameLoaded(), Wide(what).c_str());
                }
            }
        }

        TEST_METHOD(DataFolder_GivesTheIncludeAndDecompilerFolders)
        {
            NoAppState noAppState;
            SessionOptions options;
            options.dataFolder = "C:\\SciData";
            GameSession session(options);
            Assert::AreEqual(std::string("C:\\SciData\\include"), session.ResourceMap().GetIncludeFolder());
            Assert::AreEqual(std::string("C:\\SciData\\Decompiler"), session.ResourceMap().GetDecompilerFolder());
        }

        TEST_METHOD(SafeMessageBox_NoAppState_LogsTheWholeText)
        {
            NoAppState noAppState;
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            std::string longText = std::string(1000, 'x') + "END";

            Assert::AreEqual(IDOK, SafeMessageBox(longText, MB_OK | MB_ICONWARNING));
            Assert::AreEqual(IDNO, SafeMessageBox("Go ahead?", MB_YESNO | MB_ICONQUESTION), L"a question gets the safe answer");
            SafeMessageBox("Failed.", MB_OK | MB_ICONERROR);

            Assert::AreEqual(size_t(3), sink.lines.size());
            Assert::IsTrue(sink.lines[0].first == LogLevel::Warning);
            Assert::AreEqual(longText, sink.lines[0].second, L"the text must not be cut");
            Assert::IsTrue(sink.lines[1].first == LogLevel::Info);
            Assert::IsTrue(sink.lines[2].first == LogLevel::Error);
        }

        TEST_METHOD(SafeMessageBox_AppStateWithNoGui_LogsTheWholeText)
        {
            _gameFolder = SetUpGameSCI0();
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            std::string longText(1000, 'y');

            SafeMessageBox(longText, MB_OK);

            Assert::AreEqual(size_t(1), sink.lines.size());
            Assert::AreEqual(longText, sink.lines[0].second);
        }

        TEST_METHOD(Codec_BadData_LogsWithNoAppState)
        {
            NoAppState noAppState;
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            // Mode 5 is not a DCL mode (0 or 1).
            uint8_t source[8] = { 0x05, 0x04, 0, 0, 0, 0, 0, 0 };
            uint8_t destination[16] = {};

            Assert::IsFalse(decompressDCL(destination, source, sizeof(destination), sizeof(source)));

            Assert::AreEqual(size_t(1), sink.lines.size());
            Assert::IsTrue(sink.lines[0].first == LogLevel::Warning);
            Assert::IsTrue(sink.lines[0].second.find("DCL-INFLATE") != std::string::npos, Wide(sink.lines[0].second).c_str());
        }

        TEST_METHOD(CoreLogFormat_LongText_IsNotCut)
        {
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            std::string longText(5000, 'w');
            CoreLogFormat(LogLevel::Error, "[%s] %d", longText.c_str(), 42);
            Assert::AreEqual(size_t(1), sink.lines.size());
            Assert::AreEqual("[" + longText + "] 42", sink.lines[0].second);
        }

        TEST_METHOD(CoreLog_NoSink_DoesNothing)
        {
            ILogSink *previous = SetCoreLogSink(nullptr);
            CoreLog(LogLevel::Info, "nobody listens");
            CoreLogFormat(LogLevel::Info, "%s", "nobody listens");
            Assert::IsNull(GetCoreLogSink());
            SetCoreLogSink(previous);
        }

        TEST_METHOD(RemoveCoreLogSink_RemovesOnlyTheSinkItNames)
        {
            CaptureLogSink first;
            CaptureLogSink second;
            ScopedCoreLogSink scoped(first);
            RemoveCoreLogSink(&second);
            Assert::IsTrue(GetCoreLogSink() == &first, L"another sink must stay");
            RemoveCoreLogSink(&first);
            Assert::IsNull(GetCoreLogSink());
        }

        TEST_METHOD(SafeMessageBox_NoGui_GivesTheSafeAnswer)
        {
            NoAppState noAppState;
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            Assert::AreEqual(IDOK, SafeMessageBox("a", MB_OK));
            Assert::AreEqual(IDCANCEL, SafeMessageBox("b", MB_OKCANCEL));
            Assert::AreEqual(IDNO, SafeMessageBox("c", MB_YESNO));
            Assert::AreEqual(IDCANCEL, SafeMessageBox("d", MB_YESNOCANCEL));
            Assert::AreEqual(IDCANCEL, SafeMessageBox("e", MB_RETRYCANCEL));
            Assert::AreEqual(IDABORT, SafeMessageBox("f", MB_ABORTRETRYIGNORE));
            Assert::AreEqual(size_t(6), sink.lines.size());
        }

        TEST_METHOD(PicCheck_NoGui_WarnsInTheCoreLog)
        {
            // An EGA pic with no Set Palette command: the check before the
            // save warns. With no GUI, the warning goes to the core log, and
            // the pic is saved.
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            std::unique_ptr<ResourceEntity> pic(CreatePicResource(session.Version()));

            AssertOk(session.ResourceMap().WriteResource(*pic, 1, 900, ""));

            bool warned = false;
            for (const auto &line : sink.lines)
            {
                warned = warned || ((line.first == LogLevel::Warning) && (line.second.find("Set Palette") != std::string::npos));
            }
            Assert::IsTrue(warned, L"the missing Set Palette command must be in the core log");
            Assert::IsTrue(session.ResourceMap().MostRecentResource(ResourceType::Pic, 900, false) != nullptr);
        }

        TEST_METHOD(AudioCacheFiles_EnumerateWithNoAppState)
        {
            // The audio cache files are a source of audio resources. With no
            // AppState, the enumeration still works.
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci11);
            std::unique_ptr<ResourceContainer> audio = session.ResourceMap().Resources(ResourceTypeFlags::Audio, ResourceEnumFlags::IncludeCacheFiles);
            Assert::IsTrue(audio != nullptr);
            for (auto &blob : *audio)
            {
                Assert::IsTrue(blob != nullptr);
            }
        }

        TEST_METHOD(DeleteResource_LastScript_NoGui_AsksNothing)
        {
            // The GUI asks whether to remove the last copy of a script from
            // game.ini, and its source file too. With no GUI, nothing asks:
            // the resource goes, and game.ini and the source stay.
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::unique_ptr<ResourceBlob> script = session.ResourceMap().MostRecentResource(ResourceType::Script, 973, false);
            Assert::IsTrue(script != nullptr);

            session.ResourceMap().DeleteResource(script.get());

            Assert::IsTrue(session.ResourceMap().MostRecentResource(ResourceType::Script, 973, false) == nullptr);
            Assert::IsTrue(_game.Has("src\\Avoid.sc"));
            Assert::IsTrue(ReadFileText(_game.Path("game.ini")).find("n973=Avoid") != std::string::npos);
        }

        TEST_METHOD(DependencyTracker_FollowsTheSettingAfterConstruction)
        {
            BOOL track = TRUE;
            DependencyTracker tracker(track);
            sci::Script script(ScriptId("rm001.sc"));
            script.AddInclude("game.sh");
            tracker.ProcessScript(script);

            track = FALSE;
            tracker.NotifyHeaderFileChanged("game.sh");
            std::unordered_set<std::string> dirty;
            tracker.GetScriptsToRecompile(dirty, true);
            Assert::IsTrue(dirty.empty(), L"with the setting off, a header change marks no script");

            track = TRUE;
            tracker.NotifyHeaderFileChanged("game.sh");
            tracker.GetScriptsToRecompile(dirty, true);
            Assert::AreEqual(size_t(1), dirty.size(), L"with the setting on, the script that includes the header is marked");
        }
    };

    // The compile gets the game, its version, its options and the class
    // hints from the session, and the open of the session sets the text
    // codepage, so the compile works with no AppState.
    TEST_CLASS(TestHeadlessCompile)
    {
        NoAppState _noAppState;
        GameCopy _game;

        // The errors of the log, one on a line. With a text, also the other
        // messages that have the text.
        static std::string ErrorsOf(CompileLog &log, const std::string &text = std::string())
        {
            std::string errors;
            for (const CompileResult &result : log.Results())
            {
                if (result.IsError() || (!text.empty() && (result.GetMessage().find(text) != std::string::npos)))
                {
                    errors += result.GetMessage() + "\n";
                }
            }
            return errors;
        }

        // Writes the script text to src\<name>.sc of the session's game, and
        // compiles it as the resource number. A successful compile saves the
        // tables.
        static bool CompileNewScript(GameSession &session, const std::string &name, uint16_t number, const char *text, CompileLog &log)
        {
            std::string path = session.Helper().GetScriptFileName(name);
            WriteFileText(path, text);
            ScriptId scriptId(path.c_str());
            scriptId.SetResourceNumber(number);
            CompileTables tables;
            Assert::IsTrue(tables.Load(session.ResourceMap()), L"the vocab tables must load");
            PrecompiledHeaders headers(session.ResourceMap());
            CompileResults results(log, session.Version());
            bool compiled = NewCompileScript(session, results, log, tables, headers, scriptId);
            if (compiled)
            {
                tables.Save(session.ResourceMap());
            }
            return compiled;
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            SetTextCodepage(437);
        }

        TEST_METHOD(CompileAll_Templates_WorkWithNoAppState)
        {
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            for (const char *name : { TemplateSci0, TemplateSci11 })
            {
                // The data folder of the session holds include\ (sci.sh, keys.sh).
                GameSession &session = _game.OpenCopy(name);

                std::vector<ScriptId> scripts;
                session.ResourceMap().GetAllScripts(scripts);
                Assert::IsFalse(scripts.empty(), Wide(name).c_str());

                CompileLog log;
                CompileTables tables;
                Assert::IsTrue(tables.Load(session.ResourceMap()), L"the vocab tables must load");
                PrecompiledHeaders headers(session.ResourceMap());
                size_t compiled = 0;
                for (ScriptId &script : scripts)
                {
                    CompileResults results(log, session.Version());
                    if (NewCompileScript(session, results, log, tables, headers, script))
                    {
                        compiled++;
                    }
                }
                tables.Save(session.ResourceMap());

                // A polygon that the compile cannot find is only a message, so
                // look for it too: the SCI1.1 template's rooms use &getpoly.
                std::string errors = ErrorsOf(log, "&getpoly");
                Assert::IsTrue(errors.empty(), Wide(std::string(name) + ": " + errors).c_str());
                Assert::AreEqual(scripts.size(), compiled, Wide(name).c_str());
            }
        }

        TEST_METHOD(TextCodepage_ComesFromGameIni)
        {
            _game.Make(TemplateSci0);
            // Byte 0x81 is u-umlaut in codepage 437. In codepage 1252 it is 0xFC.
            std::string dosText = "\x81";
            std::string winText = "\xFC";

            SetTextCodepage(1252);
            _game.Open(SessionOptions());
            Assert::AreEqual(437, GetTextCodepage(), L"a game.ini with no codepage gives 437");
            Assert::AreEqual(winText, Dos2Win(dosText));
            Assert::AreEqual(dosText, Win2Dos(winText));
            _game.CloseSessions();

            Assert::IsTrue(WritePrivateProfileString("Game", "Codepage", "1252", _game.Path("game.ini").c_str()) != FALSE);
            _game.Open(SessionOptions());
            Assert::AreEqual(1252, GetTextCodepage(), L"the codepage must come from game.ini");
            Assert::AreEqual(dosText, Dos2Win(dosText), L"a 1252 game keeps its text");
        }
        TEST_METHOD(TextCodepage_FollowsTheGamePropertiesChange)
        {
            _game.Make(TemplateSci0);
            GameSession &session = _game.Open(SessionOptions());
            Assert::AreEqual(437, GetTextCodepage());

            // The Game Properties dialog saves the language through the helper.
            session.Helper().SetCodepage(1252);

            Assert::AreEqual(1252, GetTextCodepage(), L"the change must take effect at once");
            Assert::AreEqual(1252, session.Helper().GetCodepage(), L"game.ini must keep the change");
        }

        // A script that adds a class, a property and a method: the compile saves
        // the class table (vocab 996) and the selector table (vocab 997).
        TEST_METHOD(Compile_NewClass_SavesTheTablesWithNoAppState)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0);

            const char *text =
                "(script# 950)\n"
                "(include sci.sh)\n"
                "(include game.sh)\n"
                "(use main)\n"
                "(use obj)\n"
                "(class HeadlessClass of Obj\n"
                "    (properties\n"
                "        headlessProp 0\n"
                "    )\n"
                "    (method (headlessMethod)\n"
                "        (return headlessProp)\n"
                "    )\n"
                ")\n";
            CompileLog log;
            bool compiled = CompileNewScript(session, "HeadlessClass", 950, text, log);
            Assert::IsTrue(compiled, Wide(ErrorsOf(log)).c_str());

            SelectorTable selectors;
            Assert::IsTrue(selectors.Load(session.Helper()));
            uint16_t selector;
            Assert::IsTrue(selectors.ReverseLookup("headlessProp", selector), L"the new property must be in the saved selector table");
            Assert::IsTrue(selectors.ReverseLookup("headlessMethod", selector), L"the new method must be in the saved selector table");
            SpeciesTable species;
            Assert::IsTrue(species.Load(session.Helper()));
            SpeciesIndex speciesIndex;
            Assert::IsTrue(species.GetSpeciesIndex(950, 0, speciesIndex), L"the new class must be in the saved class table");
        }

        // A compile error with no class hints: the error names the identifier,
        // and the compile does not crash.
        TEST_METHOD(Compile_UndefinedName_ReportsAnErrorWithNoAppState)
        {
            GameSession &session = _game.OpenCopy(TemplateSci0);

            const char *text =
                "(script# 951)\n"
                "(include sci.sh)\n"
                "(include game.sh)\n"
                "(use main)\n"
                "(procedure (HeadlessProc)\n"
                "    (return headlessUndefinedName)\n"
                ")\n";
            CompileLog log;
            Assert::IsFalse(CompileNewScript(session, "HeadlessBad", 951, text, log), L"a script with an error must not compile");
            Assert::IsTrue(log.HasErrors());
            std::string errors = ErrorsOf(log);
            Assert::IsTrue(errors.find("headlessUndefinedName") != std::string::npos, Wide(errors).c_str());
        }    };

    // The decompile reads the game (the text resources, vocab.000 and the
    // version) through the resource map of the session, takes the class
    // lookups from its caller, and logs through the core log, so it works
    // with no AppState.
    TEST_CLASS(TestHeadlessDecompile)
    {
        NoAppState _noAppState;
        GameCopy _game;

        // Runs the test body for a session on a copy of each template.
        template<typename TBody>
        void ForEachTemplate(TBody body)
        {
            for (const char *name : { TemplateSci0, TemplateSci11 })
            {
                // The data folder of the session holds include\ (sci.sh, keys.sh).
                GameSession &session = _game.OpenCopy(name);

                GlobalCompiledScriptLookups lookups;
                Assert::IsTrue(lookups.Load(session.Helper()), L"the lookups must load");
                uint16_t dummy;
                lookups.GetSelectorTable().ReverseLookup("", dummy);
                std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(session.ResourceMap(), lookups.GetSelectorTable());

                std::set<uint16_t> scriptNumbers;
                for (CompiledScript *script : lookups.GetGlobalClassTable().GetAllScripts())
                {
                    scriptNumbers.insert(script->GetScriptNumber());
                }
                Assert::IsFalse(scriptNumbers.empty(), Wide(name).c_str());

                body(name, session, lookups, *config, scriptNumbers);
            }
        }

    public:
        TEST_METHOD(DecompileAll_Templates_WorkWithNoAppState)
        {
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            ForEachTemplate([](const char *name, GameSession &session, GlobalCompiledScriptLookups &lookups, IDecompilerConfig &config, const std::set<uint16_t> &scriptNumbers)
            {
                TestDecompilerResults results;
                DecompileBatch batch(&config, lookups, session.ResourceMap(), results);
                AssertOk(batch.Run(scriptNumbers), name);
                Assert::AreEqual(scriptNumbers.size(), batch.GetWrittenScripts().size(), Wide(name).c_str());
            });
        }

        // The asm output uses the fallback path for every function, and the
        // formatter loads its own lookups from the helper.
        TEST_METHOD(DecompileAsm_Templates_WorkWithNoAppState)
        {
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            ForEachTemplate([](const char *name, GameSession &session, GlobalCompiledScriptLookups &lookups, IDecompilerConfig &config, const std::set<uint16_t> &scriptNumbers)
            {
                for (uint16_t scriptNumber : scriptNumbers)
                {
                    CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
                    compiled.SetNameSelector(lookups.GetSelectorTable(), &lookups);
                    Assert::IsTrue(compiled.Load(session.Helper(), session.Version(), scriptNumber), Wide(name).c_str());
                    TestDecompilerResults results;
                    std::unique_ptr<sci::Script> script = DecompileScript(&config, lookups, session.ResourceMap(), scriptNumber, compiled, results,
                        false, false, nullptr, true, false);
                    Assert::IsNotNull(script.get());
                    ConvertToSCISyntaxHelper(*script, session.Helper());
                }
            });
        }

        // The decompiler reads sci.sh and keys.sh from the data folder of the
        // session, not from the folder of the running program (in the tests,
        // the test host, which has no include folder). With the shipped
        // Decompiler.ini, script 12 of the SCI1.1 template gets palFIND_COLOR
        // from sci.sh; with no sci.sh, it gets 5.
        TEST_METHOD(DecompilerConfig_ReadsTheHeadersFromTheDataFolder)
        {
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            _game.Make(TemplateSci11);
            std::error_code ec;
            std::filesystem::copy_file(GetTestModuleDirectory() + "\\Decompiler\\Decompiler.ini", _game.Src("Decompiler.ini"), ec);
            Assert::IsFalse(static_cast<bool>(ec), L"copying Decompiler.ini failed");

            GameSession &session = _game.Open();
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(session.Helper()), L"the lookups must load");
            uint16_t dummy;
            lookups.GetSelectorTable().ReverseLookup("", dummy);
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(session.ResourceMap(), lookups.GetSelectorTable());
            Assert::IsTrue(config->error.empty(), Wide(config->error).c_str());

            CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
            compiled.SetNameSelector(lookups.GetSelectorTable(), &lookups);
            Assert::IsTrue(compiled.Load(session.Helper(), session.Version(), 12), L"script 12 must load");
            TestDecompilerResults results;
            std::unique_ptr<sci::Script> script = DecompileScript(config.get(), lookups, session.ResourceMap(), 12, compiled, results);
            std::stringstream text;
            sci::SourceCodeWriter out(text, script.get());
            script->OutputSourceCode(out);
            Assert::IsTrue(text.str().find("palFIND_COLOR") != std::string::npos, L"the enum from sci.sh must name the value");
        }    };
}
