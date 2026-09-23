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
#include <fstream>
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

    // Runs a test with no AppState, as the command line does.
    struct NoAppState
    {
        AppState *saved;
        NoAppState() : saved(appState) { appState = nullptr; }
        ~NoAppState() { appState = saved; }
    };

    static std::wstring Wide(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    // Plan step B1. Before it:
    //  - a game could not be opened without AppState and its dialogs:
    //    SetGameFolder showed a message box and threw a CUserException with
    //    no text (P12);
    //  - with no GUI, SafeMessageBox wrote to AppState's log file, which is
    //    closed unless the GUI got a log file argument, and cut the text at
    //    260 characters. With no AppState, the text was lost (P8);
    //  - the codecs logged through appState: a null dereference with no
    //    AppState;
    //  - DependencyTracker kept a reference to its constructor's parameter,
    //    so every later read of the setting was undefined (P17).
    TEST_CLASS(TestGameSession)
    {
        std::string _gameFolder;    // From SetUpGame: CleanUpGame also deletes its AppState.
        std::string _copyFolder;    // From CopyGameFromModuleFolder: no AppState.

        void RemoveCopy()
        {
            if (!_copyFolder.empty())
            {
                std::error_code ec;
                std::filesystem::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        TEST_METHOD(Open_Templates_WorkWithNoAppState)
        {
            NoAppState noAppState;
            const std::pair<const char *, ResourceMapFormat> templates[] =
            {
                { "\\TemplateGame\\SCI0", ResourceMapFormat::SCI0 },
                { "\\TemplateGame\\SCI1.1", ResourceMapFormat::SCI11 },
            };
            for (const auto &entry : templates)
            {
                _copyFolder = CopyGameFromModuleFolder(entry.first);
                {
                    GameSession session;
                    sci::Status opened = session.Open(_copyFolder);
                    std::string text = opened ? std::string() : opened.error().ToString();
                    Assert::IsTrue(opened.has_value(), Wide(text).c_str());
                    Assert::IsTrue(session.ResourceMap().IsGameLoaded());
                    Assert::AreEqual(_copyFolder, session.Helper().GameFolder);
                    Assert::IsTrue(session.Version().MapFormat == entry.second, Wide(entry.first).c_str());
                }
                RemoveCopy();
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
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            std::filesystem::remove(_copyFolder + "\\resource.map");

            GameSession session;
            sci::Status opened = session.Open(_copyFolder);

            Assert::IsFalse(opened.has_value(), L"a folder with no resource.map must not open");
            Assert::AreEqual(std::string("not-found"), std::string(sci::ErrorCodeName(opened.error().code)));
            std::string text = opened.error().ToString();
            Assert::IsTrue(text.find("resource.map") != std::string::npos, Wide(text).c_str());
            Assert::IsFalse(session.ResourceMap().IsGameLoaded(), L"after a failed open, no game is open");
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

        TEST_METHOD(LogInfo_GoesToTheCoreLogInFull)
        {
            _gameFolder = SetUpGameSCI0();
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            std::string longText(600, 'z');

            appState->LogInfo("%s!", longText.c_str());

            Assert::AreEqual(size_t(1), sink.lines.size());
            Assert::AreEqual(longText + "!", sink.lines[0].second);
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

    // Plan step B3a. Before it, the compile read the game, its version, its
    // options, the text codepage and the class browser through appState. With
    // no AppState, the first compile dereferenced null.
    TEST_CLASS(TestHeadlessCompile)
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
        static std::string ErrorsOf(CompileLog &log)
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

        // Writes the script text to src\<name>.sc of the session's game, and
        // compiles it as the resource number. A successful compile saves the
        // tables.
        static bool CompileNewScript(GameSession &session, const std::string &name, uint16_t number, const char *text, CompileLog &log)
        {
            std::string path = session.Helper().GetScriptFileName(name);
            {
                std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
                file << text;
            }
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
            RemoveCopy();
            SetTextCodepage(437);
        }

        TEST_METHOD(CompileAll_Templates_WorkWithNoAppState)
        {
            NoAppState noAppState;
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            const char *templates[] = { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" };
            for (const char *name : templates)
            {
                _copyFolder = CopyGameFromModuleFolder(name);
                {
                    // The data folder holds include\ (sci.sh, keys.sh).
                    SessionOptions options;
                    options.dataFolder = GetTestModuleDirectory();
                    GameSession session(options);
                    sci::Status opened = session.Open(_copyFolder);
                    Assert::IsTrue(opened.has_value(), Wide(opened ? std::string() : opened.error().ToString()).c_str());

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
                    std::string errors;
                    for (const CompileResult &result : log.Results())
                    {
                        if (result.IsError() || (result.GetMessage().find("&getpoly") != std::string::npos))
                        {
                            errors += result.GetMessage() + "\n";
                        }
                    }
                    Assert::IsTrue(errors.empty(), Wide(std::string(name) + ": " + errors).c_str());
                    Assert::AreEqual(scripts.size(), compiled, Wide(name).c_str());
                }
                RemoveCopy();
            }
        }

        TEST_METHOD(TextCodepage_ComesFromGameIni)
        {
            NoAppState noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            std::string iniFile = _copyFolder + "\\game.ini";
            // Byte 0x81 is u-umlaut in codepage 437. In codepage 1252 it is 0xFC.
            std::string dosText = "\x81";
            std::string winText = "\xFC";

            {
                SetTextCodepage(1252);
                GameSession session;
                Assert::IsTrue(session.Open(_copyFolder).has_value());
                Assert::AreEqual(437, GetTextCodepage(), L"a game.ini with no codepage gives 437");
                Assert::AreEqual(winText, Dos2Win(dosText));
                Assert::AreEqual(dosText, Win2Dos(winText));
            }

            Assert::IsTrue(WritePrivateProfileString("Game", "Codepage", "1252", iniFile.c_str()) != FALSE);
            {
                GameSession session;
                Assert::IsTrue(session.Open(_copyFolder).has_value());
                Assert::AreEqual(1252, GetTextCodepage(), L"the codepage must come from game.ini");
                Assert::AreEqual(dosText, Dos2Win(dosText), L"a 1252 game keeps its text");
            }
        }
        TEST_METHOD(TextCodepage_FollowsTheGamePropertiesChange)
        {
            NoAppState noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());
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
            NoAppState noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            SessionOptions options;
            options.dataFolder = GetTestModuleDirectory();
            GameSession session(options);
            Assert::IsTrue(session.Open(_copyFolder).has_value());

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
            NoAppState noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            SessionOptions options;
            options.dataFolder = GetTestModuleDirectory();
            GameSession session(options);
            Assert::IsTrue(session.Open(_copyFolder).has_value());

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

    // Plan step B3b. Before it, the decompile read the text resources,
    // vocab.000, the version and the class lookups through appState, and
    // logged through it. With no AppState, the first decompile dereferenced
    // null.
    TEST_CLASS(TestHeadlessDecompile)
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

        // Runs the test body for a session on a copy of each template.
        template<typename TBody>
        void ForEachTemplate(TBody body)
        {
            const char *templates[] = { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" };
            for (const char *name : templates)
            {
                _copyFolder = CopyGameFromModuleFolder(name);
                {
                    // The data folder holds include\ (sci.sh, keys.sh).
                    SessionOptions options;
                    options.dataFolder = GetTestModuleDirectory();
                    GameSession session(options);
                    sci::Status opened = session.Open(_copyFolder);
                    Assert::IsTrue(opened.has_value(), Wide(opened ? std::string() : opened.error().ToString()).c_str());

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
                RemoveCopy();
            }
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        TEST_METHOD(DecompileAll_Templates_WorkWithNoAppState)
        {
            NoAppState noAppState;
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            ForEachTemplate([](const char *name, GameSession &session, GlobalCompiledScriptLookups &lookups, IDecompilerConfig &config, const std::set<uint16_t> &scriptNumbers)
            {
                TestDecompilerResults results;
                DecompileBatch batch(&config, lookups, session.ResourceMap(), results);
                batch.Run(scriptNumbers);
                Assert::AreEqual(scriptNumbers.size(), batch.GetWrittenScripts().size(), Wide(name).c_str());
            });
        }

        // The asm output uses the fallback path for every function, and the
        // formatter loads its own lookups from the helper.
        TEST_METHOD(DecompileAsm_Templates_WorkWithNoAppState)
        {
            NoAppState noAppState;
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            ForEachTemplate([](const char *name, GameSession &session, GlobalCompiledScriptLookups &lookups, IDecompilerConfig &config, const std::set<uint16_t> &scriptNumbers)
            {
                for (uint16_t scriptNumber : scriptNumbers)
                {
                    CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
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
        // session. Before B3b, it read them from the folder of the running
        // program (in the tests, the test host, which has no include folder).
        // With the shipped Decompiler.ini, script 12 of the SCI1.1 template
        // gets palFIND_COLOR from sci.sh; with no sci.sh, it gets 5.
        TEST_METHOD(DecompilerConfig_ReadsTheHeadersFromTheDataFolder)
        {
            NoAppState noAppState;
            CaptureLogSink sink;
            ScopedCoreLogSink scoped(sink);
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::error_code ec;
            std::filesystem::copy_file(GetTestModuleDirectory() + "\\Decompiler\\Decompiler.ini", _copyFolder + "\\src\\Decompiler.ini", ec);
            Assert::IsFalse(static_cast<bool>(ec), L"copying Decompiler.ini failed");

            SessionOptions options;
            options.dataFolder = GetTestModuleDirectory();
            GameSession session(options);
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(session.Helper()), L"the lookups must load");
            uint16_t dummy;
            lookups.GetSelectorTable().ReverseLookup("", dummy);
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(session.ResourceMap(), lookups.GetSelectorTable());
            Assert::IsTrue(config->error.empty(), Wide(config->error).c_str());

            CompiledScript compiled(0, CompiledScriptFlags::RemoveBadExports);
            Assert::IsTrue(compiled.Load(session.Helper(), session.Version(), 12), L"script 12 must load");
            TestDecompilerResults results;
            std::unique_ptr<sci::Script> script = DecompileScript(config.get(), lookups, session.ResourceMap(), 12, compiled, results);
            std::stringstream text;
            sci::SourceCodeWriter out(text, script.get());
            script->OutputSourceCode(out);
            Assert::IsTrue(text.str().find("palFIND_COLOR") != std::string::npos, L"the enum from sci.sh must name the value");
        }    };
}
