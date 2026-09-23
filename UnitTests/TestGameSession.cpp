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
        std::string _gameFolder;

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
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
                _gameFolder = CopyGameFromModuleFolder(entry.first);
                {
                    GameSession session;
                    sci::Status opened = session.Open(_gameFolder);
                    std::string text = opened ? std::string() : opened.error().ToString();
                    Assert::IsTrue(opened.has_value(), Wide(text).c_str());
                    Assert::IsTrue(session.ResourceMap().IsGameLoaded());
                    Assert::AreEqual(_gameFolder, session.Helper().GameFolder);
                    Assert::IsTrue(session.Version().MapFormat == entry.second, Wide(entry.first).c_str());
                }
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        TEST_METHOD(Open_NoResourceMap_ReturnsNotFoundThatNamesTheFile)
        {
            NoAppState noAppState;
            _gameFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            std::filesystem::remove(_gameFolder + "\\resource.map");

            GameSession session;
            sci::Status opened = session.Open(_gameFolder);

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
            SetCoreLogSink(previous);
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
}
