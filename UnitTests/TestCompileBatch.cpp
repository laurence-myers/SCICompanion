#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "GameFolderHelper.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "ScriptCatalog.h"
#include "Helper.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // Runs a test with no AppState, as the command line does.
    struct NoAppStateForBatch
    {
        AppState *saved;
        NoAppStateForBatch() : saved(appState) { appState = nullptr; }
        ~NoAppStateForBatch() { appState = saved; }
    };

    std::wstring WideForBatch(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::vector<uint8_t> BytesOf(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // Every diagnostic and status of the report, for an assert text.
    std::string Describe(const CompileReport &report)
    {
        std::string text;
        for (const ScriptOutcome &outcome : report.scripts)
        {
            text += outcome.name + ": " + (outcome.status ? std::string("ok") : outcome.status.error().ToString()) + "\n";
            for (const CompileResult &result : outcome.diagnostics)
            {
                if (result.IsError() || result.IsWarning())
                {
                    text += "  " + result.GetMessage() + "\n";
                }
            }
        }
        if (!report.tables)
        {
            text += "tables: " + report.tables.error().ToString() + "\n";
        }
        if (!report.commit)
        {
            text += "commit: " + report.commit.error().ToString() + "\n";
        }
        return text;
    }

    // Test events: a fault at one script, and an abort after one.
    class TestCompileEvents : public ICompileEvents
    {
    public:
        int throwAt = -1;
        std::atomic<bool> *abortAfterFirst = nullptr;
        std::vector<size_t> started;
        size_t done = 0;

        void OnScriptStart(size_t index, size_t count, const ScriptId &script) override
        {
            started.push_back(index);
            if ((int)index == throwAt)
            {
                throw std::runtime_error("an injected fault");
            }
        }
        void OnScriptDone(const ScriptOutcome &outcome) override
        {
            done++;
            if (abortAfterFirst)
            {
                abortAfterFirst->store(true);
            }
        }
    };

    const char *GoodText(uint16_t number)
    {
        switch (number)
        {
        case 904:
            return "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2Good904 0)\n(procedure (s2Good904)\n    (return 4)\n)\n";
        default:
            return "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2Good906 0)\n(procedure (s2Good906)\n    (return 6)\n)\n";
        }
    }

    // Two undeclared names: two errors.
    const char *BadText =
        "(script# 905)\n"
        "(include sci.sh)\n"
        "(include game.sh)\n"
        "(use main)\n"
        "(public s2Bad 0)\n"
        "(procedure (s2Bad)\n"
        "    (return (+ s2UndeclaredOne s2UndeclaredTwo))\n"
        ")\n";

    // A new class, so the compile adds a species and selectors to the tables,
    // and an error, so the compile fails.
    const char *BadClassText =
        "(script# 907)\n"
        "(include sci.sh)\n"
        "(include game.sh)\n"
        "(use main)\n"
        "(use obj)\n"
        "(class S2BadWidget of Obj\n"
        "    (properties\n"
        "        s2BadWidgetSize 0\n"
        "    )\n"
        "    (method (s2BadWidgetGo)\n"
        "        (return s2UndeclaredThree)\n"
        "    )\n"
        ")\n";
}

namespace UnitTests
{
    // Plan step S2. Before it, the compile of many scripts was in the GUI's
    // compile dialog: one log for all scripts (whose counts grew with each
    // count, P11), no status for each script, and no exception boundary.
    TEST_CLASS(TestCompileBatch)
    {
        std::string _copyFolder;

        void RemoveCopy()
        {
            if (!_copyFolder.empty())
            {
                std::error_code ec;
                fs::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

        void OpenCopy(const char *templateFolder, GameSession &session)
        {
            RemoveCopy();
            _copyFolder = CopyGameFromModuleFolder(templateFolder);
            Assert::IsTrue(session.Open(_copyFolder).has_value(), L"setup: the copy must open");
        }

        static ScriptId WriteScript(GameSession &session, const std::string &name, uint16_t number, const std::string &text)
        {
            std::string path = session.Helper().GetScriptFileName(name);
            {
                std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
                file << text;
            }
            ScriptId scriptId(path.c_str());
            scriptId.SetResourceNumber(number);
            return scriptId;
        }

        // Scripts 904 (good), 905 (two errors) and 906 (good) in the SCI0 copy.
        std::vector<ScriptId> ThreeScripts(GameSession &session)
        {
            return {
                WriteScript(session, "S2Good904", 904, GoodText(904)),
                WriteScript(session, "S2Bad905", 905, BadText),
                WriteScript(session, "S2Good906", 906, GoodText(906)),
            };
        }

        bool GameHasFile(const std::string &name)
        {
            return fs::exists(fs::path(_copyFolder) / name);
        }

        static SessionOptions TestSessionOptions()
        {
            SessionOptions options;
            options.dataFolder = GetTestModuleDirectory();
            return options;
        }

        static CompileOptions ToPatchFiles()
        {
            CompileOptions options;
            options.write.saveTo = ResourceSaveLocation::Patch;
            return options;
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        // Every script of both templates compiles in one batch, with no
        // error, and the tables and the one commit are Ok.
        TEST_METHOD(CompileAll_Templates_NoErrors)
        {
            NoAppStateForBatch noAppState;
            for (const char *templateFolder : { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" })
            {
                GameSession session(TestSessionOptions());
                OpenCopy(templateFolder, session);
                auto selection = SelectAllScripts(session, SelectorMode::Compile);
                Assert::IsTrue(selection.has_value(), L"setup: the scripts must be found");
                Assert::IsTrue(selection->scripts.size() > 20, L"setup: the template has its scripts");
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, selection->scripts, CompileOptions(), abort, events);
                Assert::IsTrue(report.has_value(), WideForBatch(report ? std::string() : report.error().ToString()).c_str());
                Assert::IsTrue(report->Succeeded(), WideForBatch(Describe(*report)).c_str());
                Assert::AreEqual(selection->scripts.size(), report->CompiledCount());
                Assert::AreEqual((size_t)0, report->ErrorCount(), WideForBatch(Describe(*report)).c_str());
                Assert::AreEqual(selection->scripts.size(), events.done, L"one OnScriptDone for each script");
            }
        }

        // One broken script does not stop the others: they compile and are
        // written, and the report has one Compile status, with its two
        // errors.
        TEST_METHOD(OneBrokenScript_TheOthersAreWritten)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, ThreeScripts(session), ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value());
            Assert::AreEqual((size_t)3, report->scripts.size(), WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(report->scripts[0].status.has_value() && report->scripts[2].status.has_value(), WideForBatch(Describe(*report)).c_str());
            Assert::IsFalse(report->scripts[1].status.has_value());
            Assert::IsTrue(report->scripts[1].status.error().code == sci::ErrorCode::Compile, WideForBatch(Describe(*report)).c_str());
            Assert::AreEqual((size_t)2, report->ErrorCount(), WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(report->commit.has_value() && report->tables.has_value(), WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(GameHasFile("script.904") && GameHasFile("script.906"), L"the good scripts are written");
            Assert::IsFalse(GameHasFile("script.905"), L"the broken script is not written");
            Assert::IsFalse(report->Succeeded());
        }

        // An exception in the compile of one script is an Internal status of
        // that script, with the text in its diagnostics; the batch goes on.
        TEST_METHOD(ScriptThatThrows_IsInternal_TheBatchGoesOn)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2Good904", 904, GoodText(904)),
                WriteScript(session, "S2Good906", 906, GoodText(906)),
            };
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            events.throwAt = 0;
            auto report = CompileScripts(session, scripts, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value());
            Assert::AreEqual((size_t)2, report->scripts.size());
            Assert::IsFalse(report->scripts[0].status.has_value());
            Assert::IsTrue(report->scripts[0].status.error().code == sci::ErrorCode::Internal, WideForBatch(Describe(*report)).c_str());
            // The output pane and the command line show the diagnostics.
            bool inDiagnostics = false;
            for (const CompileResult &result : report->scripts[0].diagnostics)
            {
                inDiagnostics = inDiagnostics || (result.IsError() && (result.GetMessage().find("an injected fault") != std::string::npos));
            }
            Assert::IsTrue(inDiagnostics, WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(report->scripts[1].status.has_value(), WideForBatch(Describe(*report)).c_str());
            Assert::IsFalse(GameHasFile("script.904"));
            Assert::IsTrue(GameHasFile("script.906"), L"the batch goes on after the fault");
        }

        // The counts of a log are the counts of its results: a second count
        // does not add them again (P11).
        TEST_METHOD(CompileLog_CountsItsResultsOnce)
        {
            CompileLog log;
            log.ReportResult(CompileResult("first", CompileResult::CRT_Error));
            log.ReportResult(CompileResult("second", CompileResult::CRT_Error));
            log.ReportResult(CompileResult("third", CompileResult::CRT_Warning));
            log.CalculateErrors();
            log.CalculateErrors();
            log.SummarizeAndReportErrors();
            Assert::AreEqual(std::string("2 errors, 1 warnings."), log.Results().back().GetMessage());
        }

        // The abort flag stops the batch after the current script. The
        // finished scripts are written, as with the GUI's Cancel button.
        TEST_METHOD(Abort_StopsAfterTheCurrentScript_AndCommits)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            events.abortAfterFirst = &abort;
            auto report = CompileScripts(session, ThreeScripts(session), ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(report->cancelled);
            Assert::AreEqual((size_t)1, report->scripts.size(), WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(report->commit.has_value());
            Assert::IsTrue(GameHasFile("script.904"), L"the finished script is written");
            Assert::IsFalse(GameHasFile("script.906"), L"the batch stopped before script 906");
        }

        // failFast stops after the first script that fails.
        TEST_METHOD(FailFast_StopsAfterTheFirstFailure)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = ThreeScripts(session);
            std::swap(scripts[0], scripts[1]);   // 905 (bad) first
            CompileOptions options = ToPatchFiles();
            options.failFast = true;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(report->stopped);
            Assert::AreEqual((size_t)1, report->scripts.size(), WideForBatch(Describe(*report)).c_str());
            Assert::IsFalse(GameHasFile("script.904") || GameHasFile("script.906"), L"nothing after the failure compiles");
        }

        // The table rule of plan section 4.5: when no script compiled, the
        // tables are not saved, although the failed compile changed them.
        TEST_METHOD(NoScriptCompiled_TablesAreNotSaved)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2BadWidget", 907, BadClassText) };
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value());
            Assert::AreEqual((size_t)0, report->CompiledCount(), WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(report->tables.has_value());
            Assert::IsFalse(GameHasFile("vocab.996") || GameHasFile("vocab.997"), L"the tables must not be saved when no script compiled");
        }

        // A batch that is not finished withdraws its queued writes.
        TEST_METHOD(UnfinishedBatch_WithdrawsItsWrites)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            std::vector<uint8_t> map = BytesOf(_copyFolder + "\\resource.map");
            {
                auto batch = CompileBatch::Start(session, scripts, CompileOptions());
                Assert::IsTrue(batch.has_value(), WideForBatch(batch ? std::string() : batch.error().ToString()).c_str());
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                Assert::IsTrue((*batch)->Step(abort, events));
            }
            Assert::IsTrue(map == BytesOf(_copyFolder + "\\resource.map"), L"the package must not change");
            Assert::IsFalse(session.ResourceMap().IsDeferring(), L"the batch must close its deferred writes");
        }

        // The batch cannot start with the package of a patch-mode game, or
        // with an output folder and the package.
        TEST_METHOD(Start_RefusesABadDestination)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };

            CompileOptions withOutDir;
            withOutDir.write.saveTo = ResourceSaveLocation::Package;
            withOutDir.write.outDir = _copyFolder;
            auto usage = CompileBatch::Start(session, scripts, withOutDir);
            Assert::IsFalse(usage.has_value());
            Assert::IsTrue(usage.error().code == sci::ErrorCode::Usage, WideForBatch(usage.error().ToString()).c_str());

            session.Helper().SetResourceSaveLocation(ResourceSaveLocation::Patch);
            CompileOptions toPackage;
            toPackage.write.saveTo = ResourceSaveLocation::Package;
            auto refused = CompileBatch::Start(session, scripts, toPackage);
            Assert::IsFalse(refused.has_value());
            Assert::IsTrue(refused.error().code == sci::ErrorCode::WriteRefused, WideForBatch(refused.error().ToString()).c_str());
            Assert::IsFalse(session.ResourceMap().IsDeferring(), L"a batch that did not start opens no deferred writes");
        }

        // A script whose source file is missing gives NotFound, and an error
        // in its diagnostics. Before plan step S2, the compile failed with no
        // message.
        TEST_METHOD(MissingSourceFile_IsNotFound)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            ScriptId missing(session.Helper().GetScriptFileName("S2Missing").c_str());
            missing.SetResourceNumber(908);
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { missing }, CompileOptions(), abort, events);
            Assert::IsTrue(report.has_value());
            Assert::AreEqual((size_t)1, report->scripts.size());
            Assert::IsFalse(report->scripts[0].status.has_value());
            Assert::IsTrue(report->scripts[0].status.error().code == sci::ErrorCode::NotFound, WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(Describe(*report).find("Could not read S2Missing.sc") != std::string::npos, WideForBatch(Describe(*report)).c_str());
        }
    };
}
