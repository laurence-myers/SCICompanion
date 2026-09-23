#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "ScriptCatalog.h"
#include "CompileBatchGui.h"
#include "FileWrite.h"
#include "Vocab99x.h"
#include "ResourceBlob.h"
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
        int passesStarted = 0;

        void OnPassStart(int pass) override
        {
            passesStarted++;
        }

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

    // S2b, the passes: S2PassB uses the class of S2PassA.
    const char *PassAText =
        "(script# 904)\n"
        "(include sci.sh)\n"
        "(include game.sh)\n"
        "(use main)\n"
        "(use obj)\n"
        "(class S2PassBase of Obj\n"
        "    (properties\n"
        "        s2PassSize 0\n"
        "    )\n"
        ")\n";
    const char *PassBText =
        "(script# 906)\n"
        "(include sci.sh)\n"
        "(include game.sh)\n"
        "(use main)\n"
        "(use S2PassA)\n"
        "(class S2PassDerived of S2PassBase\n"
        "    (properties\n"
        "        s2PassColor 1\n"
        "    )\n"
        ")\n";

    // A string of the script's auto text (text 904).
    const char *TextScript =
        "(script# 904)\n"
        "(text# 904)\n"
        "(include sci.sh)\n"
        "(include game.sh)\n"
        "(use main)\n"
        "(public s2Text 0)\n"
        "(procedure (s2Text)\n"
        "    (StrLen \"s2 text\")\n"
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

        void WriteBytesToGame(const std::string &name, const std::vector<uint8_t> &bytes)
        {
            std::ofstream file((fs::path(_copyFolder) / name).string(), std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }

        // Patch files that would hide a package write of script 904 and of
        // vocab 997 (a copy of the package's table, so the tables load).
        void WritePatches(GameSession &session)
        {
            WriteBytesToGame("script.904", { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 });
            const GameFolderHelper &helper = session.Helper();
            std::unique_ptr<ResourceBlob> selectors = helper.MostRecentResource(ResourceType::Vocab, 997, ResourceEnumFlags::None);
            Assert::IsTrue(selectors != nullptr);
            std::vector<uint8_t> data(selectors->GetData(), selectors->GetData() + selectors->GetLength());
            ResourceBlob patch(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            Assert::IsTrue(session.ResourceMap().WriteResource(patch).has_value());
            Assert::IsTrue(GameHasFile("vocab.997"), L"setup: vocab.997");
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

        // S2b, the passes of plan section 4.5: script 906 uses script 904,
        // which has no .sco file yet. Pass 1 fails 906 and writes S2PassA.sco;
        // pass 2 compiles 906; pass 3 changes no .sco file and is the last.
        // The commit holds the last pass. With one pass, 906 fails.
        TEST_METHOD(Passes_UntilNoObjectFileChanges)
        {
            NoAppStateForBatch noAppState;
            for (int passes : { 1, 5 })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::vector<ScriptId> scripts = {
                    WriteScript(session, "S2PassB", 906, PassBText),
                    WriteScript(session, "S2PassA", 904, PassAText),
                };
                CompileOptions options = ToPatchFiles();
                options.passes = passes;
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                Assert::IsTrue(report.has_value());
                if (passes == 1)
                {
                    Assert::AreEqual(1, report->passes);
                    Assert::IsFalse(report->scripts[0].status.has_value(), L"one pass: 906 has no S2PassA.sco yet");
                    Assert::IsTrue(report->scripts[1].status.has_value());
                    Assert::IsFalse(GameHasFile("script.906"));
                }
                else
                {
                    Assert::AreEqual(3, report->passes, WideForBatch(Describe(*report)).c_str());
                    Assert::AreEqual(2, events.passesStarted, L"OnPassStart for passes 2 and 3");
                    Assert::IsTrue(report->Succeeded(), WideForBatch(Describe(*report)).c_str());
                    Assert::AreEqual((size_t)2, report->scripts.size(), L"the report has the last pass");
                    Assert::IsTrue(GameHasFile("script.904") && GameHasFile("script.906"), L"the commit holds the last pass");
                }
            }
        }

        // S2b: a .sco file is written only when its bytes change, so a pass
        // can see that nothing changed.
        TEST_METHOD(ObjectFile_IsWrittenOnlyWhenItChanges)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            Assert::IsTrue(CompileScripts(session, scripts, ToPatchFiles(), abort, events)->Succeeded());
            fs::path sco = session.Helper().GetScriptObjectFileName("S2PassA");
            fs::file_time_type old = fs::last_write_time(sco) - std::chrono::hours(24 * 365);
            fs::last_write_time(sco, old);

            Assert::IsTrue(CompileScripts(session, scripts, ToPatchFiles(), abort, events)->Succeeded());
            Assert::IsTrue(fs::last_write_time(sco) == old, L"the .sco file has the same bytes, so it must not be written again");
        }

        // S2b, the shadow check of plan section 5: a patch file that would
        // hide a package write refuses the batch.
        TEST_METHOD(ShadowingPatches_RefuseTheBatch)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            WritePatches(session);
            std::vector<uint8_t> map = BytesOf(_copyFolder + "\\resource.map");

            auto started = CompileBatch::Start(session, scripts, CompileOptions());
            Assert::IsFalse(started.has_value());
            Assert::IsTrue(started.error().code == sci::ErrorCode::WriteRefused, WideForBatch(started.error().ToString()).c_str());
            Assert::IsTrue(started.error().message.find("script.904") != std::string::npos, WideForBatch(started.error().message).c_str());
            Assert::IsTrue(started.error().message.find("vocab.997") != std::string::npos, WideForBatch(started.error().message).c_str());
            Assert::IsTrue(map == BytesOf(_copyFolder + "\\resource.map"));
        }

        // S2b: with Replace, the package is written, and the patch files move
        // to replaced-patches\<time>.
        TEST_METHOD(ShadowingPatches_Replace_MovesThemAfterTheCommit)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            WritePatches(session);
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::IsTrue(report.has_value(), WideForBatch(report ? std::string() : report.error().ToString()).c_str());
            Assert::IsTrue(report->Succeeded(), WideForBatch(Describe(*report)).c_str());
            Assert::AreEqual((size_t)2, report->movedPatches.size());
            Assert::IsFalse(GameHasFile("script.904") || GameHasFile("vocab.997"), L"the patch files are moved");
            Assert::IsTrue(fs::exists(fs::path(_copyFolder) / "replaced-patches"));

            GameSession reopened(TestSessionOptions());
            Assert::IsTrue(reopened.Open(_copyFolder).has_value());
            std::unique_ptr<ResourceBlob> script = reopened.Helper().MostRecentResource(ResourceType::Script, 904, ResourceEnumFlags::None);
            Assert::IsTrue(script && (script->GetSourceFlags() == ResourceSourceFlags::ResourceMap), L"the package has script 904");
        }

        // S2b: the batch checks the queued package writes again before the
        // commit: a script's auto text is known only after its compile.
        TEST_METHOD(ShadowingPatches_TheQueuedTextIsCheckedBeforeTheCommit)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
            WriteBytesToGame("text.904", { 0x80 | (uint8_t)ResourceType::Text, 0, 'x', 0 });
            std::vector<uint8_t> map = BytesOf(_copyFolder + "\\resource.map");
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, CompileOptions(), abort, events);
            Assert::IsTrue(report.has_value(), L"the start does not know the text yet");
            Assert::IsTrue(report->scripts[0].status.has_value(), WideForBatch(Describe(*report)).c_str());
            Assert::IsFalse(report->commit.has_value());
            Assert::IsTrue(report->commit.error().code == sci::ErrorCode::WriteRefused, WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(report->commit.error().message.find("text.904") != std::string::npos, WideForBatch(Describe(*report)).c_str());
            Assert::IsTrue(map == BytesOf(_copyFolder + "\\resource.map"), L"nothing is written");
        }

        // S2c: the shadow check of an SCI1.1 game finds the patch file
        // 997.voc (the acceptance test of plan row S2).
        TEST_METHOD(ShadowingPatches_Sci11_Finds997Voc)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI1.1", session);
            const GameFolderHelper &helper = session.Helper();
            std::unique_ptr<ResourceBlob> selectors = helper.MostRecentResource(ResourceType::Vocab, 997, ResourceEnumFlags::None);
            Assert::IsTrue(selectors != nullptr);
            std::vector<uint8_t> data(selectors->GetData(), selectors->GetData() + selectors->GetLength());
            ResourceBlob patch(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            Assert::IsTrue(session.ResourceMap().WriteResource(patch).has_value());
            Assert::IsTrue(GameHasFile("997.voc"), L"setup: 997.voc");

            auto started = CompileBatch::Start(session, { WriteScript(session, "S2Good904", 904, GoodText(904)) }, CompileOptions());
            Assert::IsFalse(started.has_value());
            Assert::IsTrue(started.error().code == sci::ErrorCode::WriteRefused, WideForBatch(started.error().ToString()).c_str());
            Assert::IsTrue(started.error().message.find("997.voc") != std::string::npos, WideForBatch(started.error().message).c_str());
        }

        // S2c: askShadows answers for the patch files at the start (the GUI
        // asks the user), and its answer is the policy from then on: the text
        // patch file that the batch finds before the commit gets no second
        // question. Refuse stops the batch with Cancelled.
        TEST_METHOD(AskShadows_TheAnswerAtTheStartIsThePolicy)
        {
            NoAppStateForBatch noAppState;
            for (ShadowPolicy answer : { ShadowPolicy::Refuse, ShadowPolicy::Replace, ShadowPolicy::Ignore })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
                WritePatches(session);
                WriteBytesToGame("text.904", { 0x80 | (uint8_t)ResourceType::Text, 0, 'x', 0 });
                std::vector<uint8_t> map = BytesOf(_copyFolder + "\\resource.map");
                std::vector<std::vector<std::string>> questions;
                CompileOptions options;
                options.askShadows = [&](const std::vector<std::string> &files) { questions.push_back(files); return answer; };
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                Assert::AreEqual((size_t)1, questions.size(), L"one question, at the start");
                Assert::AreEqual((size_t)2, questions[0].size(), L"script.904 and vocab.997");
                if (answer == ShadowPolicy::Refuse)
                {
                    Assert::IsFalse(report.has_value());
                    Assert::IsTrue(report.error().code == sci::ErrorCode::Cancelled, WideForBatch(report.error().ToString()).c_str());
                    Assert::IsTrue(events.started.empty(), L"nothing is compiled");
                    Assert::IsTrue(map == BytesOf(_copyFolder + "\\resource.map"), L"nothing is written");
                }
                else
                {
                    Assert::IsTrue(report.has_value(), WideForBatch(report ? std::string() : report.error().ToString()).c_str());
                    Assert::IsTrue(report->Succeeded(), WideForBatch(Describe(*report)).c_str());
                    Assert::IsFalse(map == BytesOf(_copyFolder + "\\resource.map"), L"the package is written");
                    bool replace = (answer == ShadowPolicy::Replace);
                    // Replace moves only the files that hide a written
                    // resource (review of S2b): text.904 too, but not
                    // vocab.997, because the tables did not change and the
                    // commit did not write them.
                    Assert::AreEqual(replace ? (size_t)2 : (size_t)0, report->movedPatches.size(), L"Replace also moves text.904");
                    Assert::AreEqual(!replace, GameHasFile("text.904"));
                    Assert::AreEqual(!replace, GameHasFile("script.904"));
                    Assert::IsTrue(GameHasFile("vocab.997"), L"the tables were not written, so their patch file stays");
                }
            }
        }

        // S2c: a patch file that the batch finds only before the commit (the
        // script's auto text) gets its question then. Refuse writes nothing;
        // No (Ignore) keeps the file (review of 5f545221: no test had it).
        TEST_METHOD(AskShadows_BeforeTheCommit)
        {
            NoAppStateForBatch noAppState;
            for (ShadowPolicy answer : { ShadowPolicy::Refuse, ShadowPolicy::Replace, ShadowPolicy::Ignore })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
                WriteBytesToGame("text.904", { 0x80 | (uint8_t)ResourceType::Text, 0, 'x', 0 });
                std::vector<uint8_t> map = BytesOf(_copyFolder + "\\resource.map");
                std::vector<std::vector<std::string>> questions;
                CompileOptions options;
                options.askShadows = [&](const std::vector<std::string> &files) { questions.push_back(files); return answer; };
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                Assert::IsTrue(report.has_value(), L"no question at the start: the start does not know the text");
                Assert::AreEqual((size_t)1, questions.size());
                Assert::IsTrue((questions[0].size() == 1) && (questions[0][0].find("text.904") != std::string::npos));
                if (answer == ShadowPolicy::Refuse)
                {
                    Assert::IsTrue(!report->commit && (report->commit.error().code == sci::ErrorCode::Cancelled), WideForBatch(Describe(*report)).c_str());
                    Assert::IsTrue(map == BytesOf(_copyFolder + "\\resource.map"), L"nothing is written");
                }
                else
                {
                    Assert::IsTrue(report->Succeeded(), WideForBatch(Describe(*report)).c_str());
                    bool replace = (answer == ShadowPolicy::Replace);
                    Assert::AreEqual(!replace, GameHasFile("text.904"), replace ? L"the text patch file is moved" : L"No keeps the text patch file");
                    Assert::AreEqual(replace ? (size_t)1 : (size_t)0, report->movedPatches.size());
                }
            }
        }

        // S2b: the warnings of a patch-file write (plan section 5).
        TEST_METHOD(PatchWrite_Warnings)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            WriteBytesToGame("script.0904", { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 });
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"");
            std::string warnings;
            for (const std::string &warning : report->warnings)
            {
                warnings += warning + "\n";
            }
            Assert::IsTrue(warnings.find("script.0904") != std::string::npos, WideForBatch(warnings).c_str());
            Assert::IsTrue(warnings.find("996 and 997") != std::string::npos, WideForBatch(warnings).c_str());
        }

        // Review of S1 and S2a: a script that fails after its resources were
        // queued (here, its .sco file cannot be written) writes no resource.
        // Before, the commit wrote the script, but no script had compiled, so
        // the tables were not saved: the game had a class that its class
        // table did not have.
        TEST_METHOD(Savepoint_AFailedScript_WritesNoResource)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            const char *widget =
                "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
                "(class S2RoWidget of Obj\n    (properties\n        s2RoWidgetSize 0\n    )\n)\n";
            ScriptId script = WriteScript(session, "S2RoWidget", 907, widget);
            std::string sco = session.Helper().GetScriptObjectFileName("S2RoWidget");
            {
                std::ofstream file(sco.c_str(), std::ios::binary | std::ios::trunc);
                file << "not the new object file";
            }
            Assert::IsTrue(SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { script }, ToPatchFiles(), abort, events);
            // Writable again, so that the clean-up can remove the copy.
            SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            Assert::IsFalse(report->scripts[0].status.has_value(), L"the .sco file cannot be written");
            Assert::IsTrue(report->commit.has_value());
            Assert::IsFalse(GameHasFile("script.907"), L"the failed script must write no resource");
            Assert::IsFalse(GameHasFile("vocab.996") || GameHasFile("vocab.997"), L"no script compiled, so the tables are not saved");
        }

        // Review of S2b: Replace moves only the patch files that hide a
        // resource that the commit wrote. Before, it moved every file of the
        // start: the file of a script that failed (the game then read the
        // old package copy), and the vocab.997 of tables that did not change
        // (the game then lost selectors).
        TEST_METHOD(Replace_MovesOnlyTheFilesOfWrittenResources)
        {
            NoAppStateForBatch noAppState;
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Bad905", 905, BadText) };
                WriteBytesToGame("script.905", { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 });
                CompileOptions options;
                options.shadows = ShadowPolicy::Replace;
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                Assert::IsTrue(report.has_value() && !report->scripts[0].status.has_value());
                Assert::IsTrue(report->movedPatches.empty(), L"the script did not compile");
                Assert::IsTrue(GameHasFile("script.905"), L"the patch file of a script that did not compile stays");
            }
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            const GameFolderHelper &helper = session.Helper();
            SelectorTable selectors;
            Assert::IsTrue(selectors.Load(helper), L"setup: vocab 997 loads");
            selectors.Add("s2ReviewSelector");
            std::vector<uint8_t> data = selectors.MakeResourceData();
            ResourceBlob patch(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            Assert::IsTrue(session.ResourceMap().WriteResource(patch).has_value());
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"no report");
            Assert::IsTrue(GameHasFile("vocab.997"), L"the tables did not change and were not written, so their patch file stays");
            GameSession reopened(TestSessionOptions());
            Assert::IsTrue(reopened.Open(_copyFolder).has_value());
            SelectorTable after;
            Assert::IsTrue(after.Load(reopened.Helper()));
            Assert::IsTrue(after.IsSelectorName("s2ReviewSelector"), L"the game keeps the selector of the patch file");
        }

        // Review of S2b: a script that compiles in pass 1 and fails in pass
        // 2 writes nothing: the commit holds the last pass. (Before this
        // test, only inspection checked the withdrawal of a pass.)
        TEST_METHOD(Passes_AScriptThatFailsInALaterPass_IsWithdrawn)
        {
            NoAppStateForBatch noAppState;
            // In the game, and in an output folder, whose files wait for the
            // commit too (review of 5f545221).
            for (bool toFolder : { false, true })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                const char *oldY =
                    "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2YProc 0)\n(procedure (s2YProc)\n    (return 1)\n)\n";
                const char *newY =
                    "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2YOther 0)\n(procedure (s2YOther)\n    (return 2)\n)\n";
                const char *textX =
                    "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2PassY)\n(public s2X 0)\n(procedure (s2X)\n    (return (s2YProc))\n)\n";
                std::atomic<bool> abort(false);
                TestCompileEvents setupEvents;
                ScriptId y = WriteScript(session, "S2PassY", 904, oldY);
                auto setup = CompileScripts(session, { y }, ToPatchFiles(), abort, setupEvents);
                Assert::IsTrue(setup.has_value() && setup->Succeeded(), L"setup: the old S2PassY compiles");
                fs::remove(fs::path(_copyFolder) / "script.904");
                WriteScript(session, "S2PassY", 904, newY);
                ScriptId x = WriteScript(session, "S2PassX", 906, textX);
                CompileOptions options = ToPatchFiles();
                options.passes = 5;
                fs::path where = fs::path(_copyFolder);
                if (toFolder)
                {
                    where /= "S2Out";
                    fs::create_directories(where);
                    options.write.outDir = where.string();
                }
                TestCompileEvents events;
                auto report = CompileScripts(session, { x, y }, options, abort, events);
                Assert::IsTrue(report.has_value());
                Assert::AreEqual(2, report->passes, WideForBatch(Describe(*report)).c_str());
                Assert::IsFalse(report->scripts[0].status.has_value(), L"S2PassX fails in pass 2: s2YProc is gone");
                Assert::IsTrue(report->scripts[1].status.has_value(), WideForBatch(Describe(*report)).c_str());
                Assert::IsTrue(fs::exists(where / "script.904"), toFolder ? L"S2PassY is written to the folder" : L"S2PassY is written");
                Assert::IsFalse(fs::exists(where / "script.906"), toFolder ? L"the pass-1 file of S2PassX is dropped" : L"the pass-1 output of S2PassX is withdrawn");
                // Review of 944de1df: the pass-1 .sco of S2PassX, which the commit
                // does not write, goes (there was none before the batch).
                Assert::IsFalse(fs::exists(session.Helper().GetScriptObjectFileName("S2PassX")), WideForBatch("S2PassX.sco goes: " + Describe(*report)).c_str());
                Assert::IsTrue(report->objectFiles.has_value() && (report->restoredObjectFiles.size() == 1), WideForBatch(Describe(*report)).c_str());
            }
        }

        // Review of S2b: an abort between two passes keeps the pass that
        // finished. Before, the next pass withdrew it, and nothing was
        // written.
        TEST_METHOD(Abort_BetweenPasses_KeepsTheFinishedPass)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2PassB", 906, PassBText),
                WriteScript(session, "S2PassA", 904, PassAText),
            };
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            std::atomic<bool> abort(false);
            struct AbortAfterTwo : public ICompileEvents
            {
                std::atomic<bool> *flag = nullptr;
                size_t done = 0;
                int passesStarted = 0;
                void OnPassStart(int pass) override { passesStarted++; }
                void OnScriptDone(const ScriptOutcome &outcome) override
                {
                    if (++done == 2)
                    {
                        flag->store(true);
                    }
                }
            } events;
            events.flag = &abort;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::IsTrue(report.has_value());
            Assert::IsTrue(report->cancelled);
            Assert::AreEqual(0, events.passesStarted, L"no new pass starts after the abort");
            Assert::AreEqual((size_t)2, report->scripts.size(), L"the report has the pass that finished");
            Assert::IsTrue(GameHasFile("script.904"), L"the finished pass is written");
            // Review of 944de1df: that pass changed a .sco, and no pass came
            // after it: a script of the commit can use an old one.
            Assert::IsTrue(report->passLimit, L"the abort reports the pass limit");
        }

        // Review of S2a: options that wrote into the game, or failed each
        // script after its .sco file was written, refuse the start.
        TEST_METHOD(Start_RefusesBadOutputFolderOptions)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            CompileOptions raw = ToPatchFiles();
            raw.write.raw = true;
            auto rawStart = CompileBatch::Start(session, scripts, raw);
            Assert::IsTrue(!rawStart && (rawStart.error().code == sci::ErrorCode::Usage), L"raw files need an output folder");
            CompileOptions missing = ToPatchFiles();
            missing.write.outDir = _copyFolder + "\\S2NoSuchFolder";
            auto missingStart = CompileBatch::Start(session, scripts, missing);
            Assert::IsTrue(!missingStart && (missingStart.error().code == sci::ErrorCode::NotFound), L"an output folder that does not exist");
            CompileOptions game = ToPatchFiles();
            game.write.outDir = _copyFolder;
            auto gameStart = CompileBatch::Start(session, scripts, game);
            Assert::IsTrue(!gameStart && (gameStart.error().code == sci::ErrorCode::Usage), L"the game folder is not an output folder");
        }

        // Review of S2b: a script with no number (a document opened from a
        // file, or the GUI's scan of src) gets the number that its source
        // declares, so the start sees its patch file, and the report has the
        // number. Before, the number was 0xFFFF.
        TEST_METHOD(Start_AScriptWithNoNumber_GetsTheDeclaredNumber)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::string path = session.Helper().GetScriptFileName("S2Good904");
            {
                std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
                file << GoodText(904);
            }
            ScriptId noNumber(path.c_str());
            Assert::IsTrue(noNumber.GetResourceNumber() == InvalidResourceNumber, L"setup: no number");
            WriteBytesToGame("script.904", { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 });
            auto started = CompileBatch::Start(session, { noNumber }, CompileOptions());
            Assert::IsTrue(!started && (started.error().code == sci::ErrorCode::WriteRefused), L"the start sees script.904");

            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { noNumber }, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"no report");
            Assert::AreEqual(904, (int)report->scripts[0].number);
        }

        // Review of S2b: Replace does not move a file over the backup of an
        // earlier batch in the same second: each batch gets a new folder.
        TEST_METHOD(Replace_KeepsTheBackupOfAnEarlierBatch)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            WriteBytesToGame("script.904", { 0x80 | (uint8_t)ResourceType::Script, 0, 9, 9 });
            // A backup of script.904 in the folder of each of the next seconds.
            SYSTEMTIME now;
            GetLocalTime(&now);
            FILETIME start;
            SystemTimeToFileTime(&now, &start);
            std::vector<fs::path> backups;
            for (int second = 0; second < 10; second++)
            {
                ULARGE_INTEGER t;
                t.LowPart = start.dwLowDateTime;
                t.HighPart = start.dwHighDateTime;
                t.QuadPart += (ULONGLONG)second * 10000000ULL;
                FILETIME shifted;
                shifted.dwLowDateTime = t.LowPart;
                shifted.dwHighDateTime = t.HighPart;
                SYSTEMTIME time;
                FileTimeToSystemTime(&shifted, &time);
                char name[32];
                sprintf_s(name, "%04d%02d%02d-%02d%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
                fs::path folder = fs::path(_copyFolder) / "replaced-patches" / name;
                fs::create_directories(folder);
                std::ofstream file((folder / "script.904").string(), std::ios::binary);
                file << "an earlier backup";
                backups.push_back(folder / "script.904");
            }
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"no report");
            Assert::AreEqual((size_t)1, report->movedPatches.size());
            for (const fs::path &backup : backups)
            {
                std::ifstream file(backup.string(), std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                Assert::AreEqual(std::string("an earlier backup"), text, WideForBatch(backup.string()).c_str());
            }
        }

        // Review of S2a: the counts that a caller reads. A failed commit is
        // not a success, and a warning is counted.
        TEST_METHOD(Report_SucceededAndTheWarningCount)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            // The script's number is 905, but its source declares 904: a
            // warning.
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 905, GoodText(904)) };
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"no report");
            Assert::IsTrue(report->WarningCount() >= 1, L"the warning is counted");

            CompileReport failed = *report;
            failed.commit = sci::Fail(sci::ErrorCode::Io, "a failed write");
            Assert::IsFalse(failed.Succeeded(), L"a failed commit is not a success");
            CompileReport notMoved = *report;
            notMoved.moves = sci::Fail(sci::ErrorCode::Io, "a patch file that did not move");
            Assert::IsFalse(notMoved.Succeeded(), L"a patch file that still hides the write is not a success");
        }

        // Review of S2c: the lines that the GUI shows after a batch, and the
        // scripts of a compile-all.
        TEST_METHOD(Gui_ReportLinesAndTheScriptsOfACompileAll)
        {
            NoAppStateForBatch noAppState;
            CompileReport report;
            report.tables = sci::Fail(sci::ErrorCode::Io, "the tables");
            report.commit = sci::Fail(sci::ErrorCode::Cancelled, "the answer was Cancel");
            report.movedPatches = { "a -> b" };
            report.warnings = { "a warning" };
            CompileLog log;
            ReportCompileBatch(report, log, "Write problem: ");
            std::vector<CompileResult> &lines = log.Results();
            Assert::AreEqual((size_t)4, lines.size());
            Assert::IsTrue(lines[0].IsError() && (lines[0].GetMessage().find("the tables") != std::string::npos));
            Assert::IsTrue(!lines[1].IsError() && !lines[1].IsWarning() && (lines[1].GetMessage().find("stopped") != std::string::npos), L"Cancel is not an error");
            Assert::IsTrue(lines[2].GetMessage().find("a -> b") != std::string::npos);
            Assert::IsTrue(lines[3].IsWarning() && (lines[3].GetMessage().find("a warning") != std::string::npos));

            CompileReport failed;
            failed.commit = sci::Fail(sci::ErrorCode::Io, "the disk is full");
            CompileLog failedLog;
            ReportCompileBatch(failed, failedLog, "Write problem: ");
            Assert::IsTrue((failedLog.Results().size() == 1) && failedLog.Results()[0].IsError() &&
                (failedLog.Results()[0].GetMessage().find("Write problem: ") == 0));

            // Review of 5f545221: a table failure that refused the commit is
            // one error line (before, two).
            CompileReport refused;
            refused.tables = sci::Fail(sci::ErrorCode::Io, "vocab.996 is read-only");
            sci::Error refusal = refused.tables.error();
            refusal.context.push_back("no compiled resource was written, because the class and selector tables could not be saved");
            refused.commit = sci::Fail(refusal);
            CompileLog refusedLog;
            ReportCompileBatch(refused, refusedLog, "Write problem: ");
            Assert::AreEqual((size_t)1, refusedLog.Results().size(), L"one line for a table failure");
            Assert::IsTrue(refusedLog.Results()[0].IsError() && (refusedLog.Results()[0].GetMessage().find("so no compiled resource was written") != std::string::npos),
                WideForBatch(refusedLog.Results()[0].GetMessage()).c_str());

            Assert::IsFalse(StartFailureLine(sci::Fail(sci::ErrorCode::Cancelled, "x").value()).IsError(), L"Cancel at the start is not an error");
            Assert::IsTrue(StartFailureLine(sci::Fail(sci::ErrorCode::NotFound, "x").value()).IsError());

            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> all = ScriptsToCompile(session.ResourceMap(), std::unordered_set<std::string>());
            Assert::IsTrue(all.size() > 20, L"every script of game.ini");
            std::vector<ScriptId> door = ScriptsToCompile(session.ResourceMap(), { "door" });
            Assert::AreEqual((size_t)1, door.size());
            Assert::AreEqual(974, (int)door[0].GetResourceNumber());
        }

        // Review of S1: the write shares the file as the ofstream that it
        // replaced did, so a program that has the file open with read and
        // write sharing does not stop it.
        TEST_METHOD(FileWrite_SharesTheFile)
        {
            fs::path folder = fs::temp_directory_path() / "S2FileWriteShare";
            fs::create_directories(folder);
            std::string path = (folder / "shared.sco").string();
            Assert::IsTrue(WriteBytesToFile(path, std::vector<uint8_t>{ 1, 2, 3 }).has_value());
            HANDLE other = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(other != INVALID_HANDLE_VALUE, L"setup: another handle");
            sci::Status written = WriteBytesToFile(path, std::vector<uint8_t>{ 4, 5, 6, 7 });
            CloseHandle(other);
            std::error_code ec;
            fs::remove_all(folder, ec);
            Assert::IsTrue(written.has_value(), WideForBatch(written ? std::string() : written.error().ToString()).c_str());
        }

        // Review of S2c: the raw text of a syntax error with a hint is a
        // sentence of its own. Before, it read 'Expected variable.: "5"'.
        TEST_METHOD(SyntaxError_TheRawTextOfAHint)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            const char *text =
                "(script# 905)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2cHint 0)\n(procedure (s2cHint)\n    (= 5 3)\n)\n";
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { WriteScript(session, "S2cHint", 905, text) }, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value());
            const CompileResult *syntaxError = nullptr;
            for (const CompileResult &result : report->scripts[0].diagnostics)
            {
                if (result.IsError())
                {
                    syntaxError = &result;
                    break;
                }
            }
            Assert::IsNotNull(syntaxError, WideForBatch(Describe(*report)).c_str());
            const std::string &raw = syntaxError->GetRawMessage();
            Assert::IsTrue(raw.find(".:") == std::string::npos, WideForBatch(raw).c_str());
            Assert::IsTrue(raw.find("\"5\"") != std::string::npos, WideForBatch(raw + " / " + syntaxError->GetMessage()).c_str());
        }

        // S2c, plan P13: every diagnostic line is 1-based (some parser
        // messages had 0-based lines), and a diagnostic has its raw message,
        // with no "Error: (file) ... Line: N, col: M" around it, for the
        // command line.
        TEST_METHOD(Diagnostics_OneBasedLinesAndTheRawMessage)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            const char *text =
                "(script# 905)\n"                   // 1
                "(include sci.sh)\n"                // 2
                "(include game.sh)\n"               // 3
                "(use main)\n"                      // 4
                "(public s2cProc 0)\n"              // 5
                "(procedure (s2cProc a)\n"          // 6
                "    (cond\n"                       // 7
                "        (else 1)\n"                // 8
                "        ((== a 1) 2)\n"            // 9
                "    )\n"                           // 10
                "    (return s2cUndeclared)\n"      // 11
                ")\n";
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { WriteScript(session, "S2cLines", 905, text) }, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value());
            const CompileResult *elseMessage = nullptr;
            const CompileResult *undeclared = nullptr;
            for (const CompileResult &result : report->scripts[0].diagnostics)
            {
                if (result.GetMessage().find("else clause must be the last") != std::string::npos)
                {
                    elseMessage = &result;
                }
                if (result.IsError() && (result.GetMessage().find("s2cUndeclared") != std::string::npos))
                {
                    undeclared = &result;
                }
            }
            Assert::IsNotNull(elseMessage, WideForBatch(Describe(*report)).c_str());
            Assert::AreEqual(8, elseMessage->GetLineNumber(), L"the else clause is on line 8 (before: 7, 0-based)");
            Assert::IsNotNull(undeclared, WideForBatch(Describe(*report)).c_str());
            Assert::AreEqual(11, undeclared->GetLineNumber());
            const std::string &raw = undeclared->GetRawMessage();
            Assert::IsTrue((raw.find("s2cUndeclared") != std::string::npos) && (raw.find("Line:") == std::string::npos) && (raw.find("Error:") == std::string::npos),
                WideForBatch(raw).c_str());
            Assert::IsTrue(undeclared->GetMessage().find("Line: 11") != std::string::npos, L"the GUI text keeps its form");
        }

        // S2c, P13: the text of a syntax error has the 1-based line too (it
        // had the 0-based line), and the error has a raw message.
        TEST_METHOD(SyntaxError_OneBasedLineInTheTextAndTheRawMessage)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            const char *text =
                "(script# 905)\n"                   // 1
                "(include sci.sh)\n"                // 2
                "(include game.sh)\n"               // 3
                "(use main)\n"                      // 4
                "(public s2cSyntax 0)\n"            // 5
                "(procedure (s2cSyntax)\n"          // 6
                "    (= )\n"                        // 7
                ")\n";
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { WriteScript(session, "S2cSyntax", 905, text) }, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value());
            const CompileResult *syntaxError = nullptr;
            for (const CompileResult &result : report->scripts[0].diagnostics)
            {
                if (result.IsError())
                {
                    syntaxError = &result;
                    break;
                }
            }
            Assert::IsNotNull(syntaxError, WideForBatch(Describe(*report)).c_str());
            Assert::AreEqual(7, syntaxError->GetLineNumber(), WideForBatch(syntaxError->GetMessage()).c_str());
            Assert::IsTrue(syntaxError->GetMessage().find("(7, ") != std::string::npos, WideForBatch(syntaxError->GetMessage()).c_str());
            const std::string &raw = syntaxError->GetRawMessage();
            Assert::IsTrue(!raw.empty() && (raw.find("Error:") == std::string::npos) && (raw.find("(7, ") == std::string::npos), WideForBatch(raw).c_str());
        }

        // S2c: each outcome has the sizes of its compiled script (the GUI
        // shows them after a compile).
        TEST_METHOD(Outcome_HasTheStats)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { WriteScript(session, "S2Good904", 904, GoodText(904)) }, ToPatchFiles(), abort, events);
            Assert::IsTrue(report.has_value() && report->Succeeded());
            Assert::IsTrue(report->scripts[0].stats.Code > 0, L"the compiled script has code");
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

        // Review of 5f545221: a debug file that cannot be written is a
        // warning, and the batch stays whole. Before, script 907 failed after
        // its .sco was written: the batch withdrew its resources, but script
        // 906 compiled against its new class, and the game got script 906
        // without script 907.
        TEST_METHOD(DebugFileWriteError_IsAWarning_TheBatchStaysWhole)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
            const char *widget =
                "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
                "(class S2DepWidget of Obj\n    (properties\n        s2DepWidgetSize 0\n    )\n)\n";
            const char *user =
                "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2DepWidget)\n"
                "(public s2DepUser 0)\n(procedure (s2DepUser)\n    (S2DepWidget new:)\n)\n";
            std::vector<ScriptId> scripts = { WriteScript(session, "S2DepWidget", 907, widget), WriteScript(session, "S2DepUser", 906, user) };
            std::string debugFile = session.Helper().GetScriptDebugFileName(907);
            fs::create_directories(fs::path(debugFile).parent_path());
            {
                std::ofstream file(debugFile.c_str(), std::ios::binary | std::ios::trunc);
                file << "not the new debug file";
            }
            Assert::IsTrue(SetFileAttributesA(debugFile.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, ToPatchFiles(), abort, events);
            // Writable again, so that the clean-up can remove the copy.
            SetFileAttributesA(debugFile.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"");
            Assert::IsTrue(GameHasFile("script.907") && GameHasFile("script.906"), L"both scripts are written");
            bool warned = false;
            for (const ScriptOutcome &outcome : report->scripts)
            {
                for (const CompileResult &result : outcome.diagnostics)
                {
                    warned = warned || (result.IsWarning() && (result.GetMessage().find("907.scd") != std::string::npos));
                }
            }
            Assert::IsTrue(warned, L"the debug file is a warning");
        }

        // Review of 5f545221: with an output folder, the files wait for the
        // commit. A script that fails writes no file, and a commit that
        // fails writes none. Before, the files of a script that failed stayed
        // in the folder, and so did the files of a batch whose tables could
        // not be written.
        TEST_METHOD(OutputFolder_TheFilesWaitForTheCommit)
        {
            NoAppStateForBatch noAppState;
            const char *widget =
                "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
                "(class S2OutWidget of Obj\n    (properties\n        s2OutWidgetSize 0\n    )\n)\n";
            {
                // A script whose .sco cannot be written.
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                fs::path out = fs::path(_copyFolder) / "S2Out";
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2OutWidget", 907, widget), WriteScript(session, "S2Good904", 904, GoodText(904)) };
                std::string sco = session.Helper().GetScriptObjectFileName("S2OutWidget");
                {
                    std::ofstream file(sco.c_str(), std::ios::binary | std::ios::trunc);
                    file << "not the new object file";
                }
                Assert::IsTrue(SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
                CompileOptions options = ToPatchFiles();
                options.write.outDir = out.string();
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_NORMAL);
                Assert::IsTrue(report.has_value() && report->commit.has_value(), report ? WideForBatch(Describe(*report)).c_str() : L"");
                Assert::IsFalse(report->scripts[0].status.has_value(), L"setup: 907 fails");
                Assert::IsFalse(fs::exists(out / "script.907"), L"a script that fails writes no file");
                Assert::IsTrue(fs::exists(out / "script.904"), L"the script that compiled is written at the commit");
                Assert::IsFalse(GameHasFile("script.904") || GameHasFile("script.907"), L"the game does not change");
            }
            // A file of the folder that cannot be written: a table file, or
            // the file of the script. Every file is checked before the first
            // write, so no file of the batch is written.
            for (const char *readOnly : { "vocab.996", "script.907" })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                fs::path out = fs::path(_copyFolder) / "S2Out";
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2OutWidget", 907, widget) };
                std::string held = (out / readOnly).string();
                {
                    std::ofstream file(held.c_str(), std::ios::binary | std::ios::trunc);
                    file << "an old file";
                }
                Assert::IsTrue(SetFileAttributesA(held.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
                CompileOptions options = ToPatchFiles();
                options.write.outDir = out.string();
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                SetFileAttributesA(held.c_str(), FILE_ATTRIBUTE_NORMAL);
                Assert::IsTrue(report.has_value());
                Assert::IsTrue(report->scripts[0].status.has_value(), WideForBatch(Describe(*report)).c_str());
                Assert::IsFalse(report->commit.has_value(), WideForBatch(std::string(readOnly) + " cannot be written").c_str());
                for (const char *name : { "script.907", "vocab.996", "vocab.997" })
                {
                    if (_stricmp(name, readOnly) != 0)
                    {
                        Assert::IsFalse(fs::exists(out / name), WideForBatch(std::string(name) + " is written, with " + readOnly + " read-only").c_str());
                    }
                }
            }
        }

        // Review of 5f545221: passLimit says that the last pass that the
        // options allow still changed a .sco file (S2PassB needs a second
        // pass).
        TEST_METHOD(Passes_TheLimitIsReported)
        {
            NoAppStateForBatch noAppState;
            for (int passes : { 1, 3 })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2PassB", 906, PassBText), WriteScript(session, "S2PassA", 904, PassAText) };
                CompileOptions options = ToPatchFiles();
                options.passes = passes;
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                Assert::IsTrue(report.has_value());
                Assert::AreEqual(passes == 1, report->passLimit, WideForBatch(Describe(*report)).c_str());
            }
        }

        // Review of 5f545221: a dry run checks the patch files that would hide
        // a package write, as a real run does, and moves none. Before, it
        // skipped the check, so a dry run passed where the real run was
        // refused.
        TEST_METHOD(DryRun_ChecksTheShadowsAndMovesNothing)
        {
            NoAppStateForBatch noAppState;
            for (ShadowPolicy policy : { ShadowPolicy::Refuse, ShadowPolicy::Replace })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
                WriteBytesToGame("script.904", { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 });
                std::vector<uint8_t> map = BytesOf(_copyFolder + "\\resource.map");
                CompileOptions options;
                options.write.writeResources = false;
                options.write.writeObjectFile = false;
                options.write.writeDebugInfo = false;
                options.shadows = policy;
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                if (policy == ShadowPolicy::Refuse)
                {
                    Assert::IsFalse(report.has_value(), L"refused, as the real run is");
                    Assert::IsTrue(report.error().code == sci::ErrorCode::WriteRefused, WideForBatch(report.error().ToString()).c_str());
                }
                else
                {
                    Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"");
                    Assert::IsTrue(report->movedPatches.empty(), L"a dry run moves nothing");
                    bool named = false;
                    for (const std::string &warning : report->warnings)
                    {
                        named = named || (warning.find("script.904") != std::string::npos);
                    }
                    Assert::IsTrue(named, L"the report names the file that a real run would move (review of 4247f34c)");
                }
                Assert::IsTrue(GameHasFile("script.904"), L"the patch file stays");
                Assert::IsTrue(map == BytesOf(_copyFolder + "\\resource.map"), L"nothing is written");
            }
        }

        // Review of 5f545221: every patch file that cannot move is in the
        // error of the moves. Before, only the first one was; the others
        // were warnings.
        TEST_METHOD(Replace_EveryFileThatCannotMove_IsInTheError)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
            WriteBytesToGame("script.904", { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 });
            WriteBytesToGame("text.904", { 0x80 | (uint8_t)ResourceType::Text, 0, 'x', 0 });
            // Held open with read sharing only: the scan can read them, but neither can move.
            HANDLE script = CreateFileA((fs::path(_copyFolder) / "script.904").string().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE text = CreateFileA((fs::path(_copyFolder) / "text.904").string().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue((script != INVALID_HANDLE_VALUE) && (text != INVALID_HANDLE_VALUE), L"setup: the patch files are held");
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            CloseHandle(script);
            CloseHandle(text);
            Assert::IsTrue(report.has_value() && report->commit.has_value(), report ? WideForBatch(Describe(*report)).c_str() : L"");
            Assert::IsFalse(report->moves.has_value(), L"the moves failed");
            std::string error = report->moves.error().ToString();
            Assert::IsTrue((error.find("script.904") != std::string::npos) && (error.find("text.904") != std::string::npos), WideForBatch(error).c_str());
        }

        // Review of 4247f34c: a new pass withdraws the writes of the pass
        // before, but not its .sco files. In pass 2, S2UseY fails against the
        // new .sco of S2UseX, and S2UseX compiles against the pass-1 .sco of
        // S2UseY. Before, the commit wrote the new S2UseX without S2UseY, whose
        // export 1 it calls. Now the commit writes nothing.
        TEST_METHOD(Passes_AScriptThatUsesTheNewObjectFileOfAFailedScript_WritesNothing)
        {
            NoAppStateForBatch noAppState;
            const char *oldY = "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2UseYOld 0)\n(procedure (s2UseYOld)\n    (return 1)\n)\n";
            const char *oldX = "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2UseXOld 0)\n(procedure (s2UseXOld)\n    (return 2)\n)\n";
            const char *newY =
                "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2UseX)\n(public s2UseYNew 0 s2UseYMore 1)\n"
                "(procedure (s2UseYNew)\n    (return (s2UseXOld))\n)\n(procedure (s2UseYMore)\n    (return 7)\n)\n";
            const char *newX =
                "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2UseY)\n(public s2UseXNew 0)\n"
                "(procedure (s2UseXNew)\n    (return (s2UseYMore))\n)\n";
            // In the game, in an output folder, and (variant 2) with a ScriptId
            // of S2UseX whose number (907) is not the number that its source
            // declares (review of 944de1df: the refusal said "script 906").
            for (int variant : { 0, 1, 2 })
            {
                bool toFolder = (variant == 1);
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::atomic<bool> abort(false);
                TestCompileEvents setupEvents;
                ScriptId y = WriteScript(session, "S2UseY", 904, oldY);
                ScriptId x = WriteScript(session, "S2UseX", 906, oldX);
                auto setup = CompileScripts(session, { y, x }, ToPatchFiles(), abort, setupEvents);
                Assert::IsTrue(setup.has_value() && setup->Succeeded(), L"setup: the old scripts compile");
                std::vector<uint8_t> oldYScript = BytesOf(_copyFolder + "\\script.904");
                std::vector<uint8_t> oldXScript = BytesOf(_copyFolder + "\\script.906");
                std::string ySco = session.Helper().GetScriptObjectFileName("S2UseY");
                std::string xSco = session.Helper().GetScriptObjectFileName("S2UseX");
                std::vector<uint8_t> oldYSco = BytesOf(ySco);
                std::vector<uint8_t> oldXSco = BytesOf(xSco);
                WriteScript(session, "S2UseY", 904, newY);
                WriteScript(session, "S2UseX", 906, newX);
                ScriptId batchX(x.GetFullPath().c_str());
                batchX.SetResourceNumber((variant == 2) ? 907 : 906);
                CompileOptions options = ToPatchFiles();
                options.passes = 5;
                fs::path out = fs::path(_copyFolder) / "S2Out";
                if (toFolder)
                {
                    fs::create_directories(out);
                    options.write.outDir = out.string();
                }
                TestCompileEvents events;
                auto report = CompileScripts(session, { y, batchX }, options, abort, events);
                Assert::IsTrue(report.has_value());
                std::string facts = Describe(*report);
                Assert::AreEqual(2, report->passes, WideForBatch(facts).c_str());
                Assert::IsFalse(report->scripts[0].status.has_value(), WideForBatch("setup: S2UseY fails in pass 2: " + facts).c_str());
                Assert::IsTrue(report->scripts[1].status.has_value(), WideForBatch("setup: S2UseX compiles in pass 2: " + facts).c_str());
                Assert::IsFalse(report->commit.has_value(), WideForBatch("the commit writes nothing: " + facts).c_str());
                Assert::IsTrue(report->commit.error().code == sci::ErrorCode::WriteRefused, WideForBatch(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("S2UseX (906) uses S2UseY (904)") != std::string::npos, WideForBatch(facts).c_str());
                Assert::IsTrue(oldYScript == BytesOf(_copyFolder + "\\script.904"), L"S2UseY in the game does not change");
                Assert::IsTrue(oldXScript == BytesOf(_copyFolder + "\\script.906"), L"S2UseX in the game does not change");
                if (toFolder)
                {
                    Assert::IsTrue(fs::is_empty(out), L"no file is written to the folder");
                }
                // Review of 944de1df: the .sco files are back as they were, so
                // S2UseX alone does not compile against the old S2UseY. Before,
                // the new .sco files stayed, and that compile wrote the new
                // S2UseX with the old S2UseY.
                Assert::IsTrue(report->objectFiles.has_value(), WideForBatch(facts).c_str());
                Assert::IsTrue(oldYSco == BytesOf(ySco), L"S2UseY.sco is back");
                Assert::IsTrue(oldXSco == BytesOf(xSco), L"S2UseX.sco is back");
                if (variant == 0)
                {
                    TestCompileEvents aloneEvents;
                    auto alone = CompileScripts(session, { x }, ToPatchFiles(), abort, aloneEvents);
                    Assert::IsTrue(alone.has_value() && !alone->scripts[0].status.has_value(), alone ? WideForBatch("S2UseX alone fails: " + Describe(*alone)).c_str() : L"");
                    Assert::IsTrue(oldXScript == BytesOf(_copyFolder + "\\script.906"), L"S2UseX alone is not written");
                }
            }
        }

        // Review of 4247f34c: an abort in pass 2 commits the pass-2 scripts
        // that ran, and the pass-1 writes of the others are withdrawn.
        // S2AbortX compiled in pass 2 against the new .sco of S2AbortY, which
        // did not run in pass 2. Before, the commit wrote S2AbortX, which calls
        // export 1 of S2AbortY, without S2AbortY. Now it writes nothing.
        TEST_METHOD(Passes_AnAbortInALaterPass_WritesNoScriptThatUsesANewObjectFile)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            const char *oldY = "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2AbortYOld 0)\n(procedure (s2AbortYOld)\n    (return 1)\n)\n";
            const char *newY =
                "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2AbortYNew 0 s2AbortYMore 1)\n"
                "(procedure (s2AbortYNew)\n    (return 3)\n)\n(procedure (s2AbortYMore)\n    (return 7)\n)\n";
            const char *newX =
                "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2AbortY)\n(public s2AbortX 0)\n"
                "(procedure (s2AbortX)\n    (return (s2AbortYMore))\n)\n";
            std::atomic<bool> abort(false);
            TestCompileEvents setupEvents;
            ScriptId y = WriteScript(session, "S2AbortY", 904, oldY);
            auto setup = CompileScripts(session, { y }, ToPatchFiles(), abort, setupEvents);
            Assert::IsTrue(setup.has_value() && setup->Succeeded(), L"setup: the old S2AbortY compiles");
            std::vector<uint8_t> oldYScript = BytesOf(_copyFolder + "\\script.904");
            WriteScript(session, "S2AbortY", 904, newY);
            ScriptId x = WriteScript(session, "S2AbortX", 906, newX);
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            struct AbortInPassTwo : public ICompileEvents
            {
                std::atomic<bool> *flag = nullptr;
                int pass = 1;
                void OnPassStart(int newPass) override { pass = newPass; }
                void OnScriptDone(const ScriptOutcome &outcome) override
                {
                    if (pass >= 2)
                    {
                        flag->store(true);
                    }
                }
            } events;
            events.flag = &abort;
            auto report = CompileScripts(session, { x, y }, options, abort, events);
            Assert::IsTrue(report.has_value());
            std::string facts = Describe(*report);
            Assert::IsTrue(report->cancelled && (report->passes == 2), WideForBatch("setup: the abort comes in pass 2: " + facts).c_str());
            Assert::AreEqual((size_t)1, report->scripts.size(), WideForBatch(facts).c_str());
            Assert::IsTrue(report->scripts[0].status.has_value(), WideForBatch("setup: S2AbortX compiles in pass 2: " + facts).c_str());
            Assert::IsFalse(report->commit.has_value(), WideForBatch("the commit writes nothing: " + facts).c_str());
            Assert::IsTrue(report->commit.error().ToString().find("S2AbortX (906) uses S2AbortY (904)") != std::string::npos, WideForBatch(facts).c_str());
            Assert::IsFalse(GameHasFile("script.906"), L"S2AbortX is not written");
            Assert::IsTrue(oldYScript == BytesOf(_copyFolder + "\\script.904"), L"S2AbortY in the game does not change");
        }

        // Review of 4247f34c: a dry run checks the auto text before the
        // commit, as a real run does. Before, a dry run queued nothing, so it
        // passed where the real run was refused.
        TEST_METHOD(DryRun_ChecksTheAutoTextBeforeTheCommit)
        {
            NoAppStateForBatch noAppState;
            for (bool dryRun : { false, true })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
                WriteBytesToGame("text.904", { 0x80 | (uint8_t)ResourceType::Text, 0, 'x', 0 });
                std::vector<uint8_t> map = BytesOf(_copyFolder + "\\resource.map");
                CompileOptions options;
                options.write.writeResources = !dryRun;
                options.write.writeObjectFile = !dryRun;
                options.write.writeDebugInfo = !dryRun;
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                Assert::IsTrue(report.has_value(), report ? L"" : WideForBatch(report.error().ToString()).c_str());
                std::string facts = std::string(dryRun ? "dry run: " : "real run: ") + Describe(*report);
                Assert::IsTrue(report->scripts[0].status.has_value(), WideForBatch(facts).c_str());
                Assert::IsFalse(report->commit.has_value(), WideForBatch(facts).c_str());
                Assert::IsTrue(report->commit.error().code == sci::ErrorCode::WriteRefused, WideForBatch(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("text.904") != std::string::npos, WideForBatch(facts).c_str());
                Assert::IsTrue(map == BytesOf(_copyFolder + "\\resource.map"), L"nothing is written");
            }
        }

        // Review of 4247f34c: a dry run does not ask what to do with the
        // patch files that would hide a package write (it cannot move them).
        // Refuse stops it, as a real run with no askShadows. Before, it asked
        // "move?", and then moved nothing.
        TEST_METHOD(DryRun_AsksNothing)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            WriteBytesToGame("script.904", { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 });
            int asked = 0;
            CompileOptions options;
            options.write.writeResources = false;
            options.write.writeObjectFile = false;
            options.write.writeDebugInfo = false;
            options.askShadows = [&asked](const std::vector<std::string> &files)
            {
                asked++;
                return ShadowPolicy::Replace;
            };
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::AreEqual(0, asked, L"a dry run asks nothing");
            Assert::IsFalse(report.has_value(), L"Refuse stops the dry run");
            Assert::IsTrue(report.error().code == sci::ErrorCode::WriteRefused, WideForBatch(report.error().ToString()).c_str());
            Assert::IsTrue(GameHasFile("script.904"), L"the patch file stays");
        }

        // Review of 4247f34c: a hidden or system file, a folder with the
        // file's name, or a file that another program holds, in the output
        // folder fails the commit before the first write. Before, a hidden or
        // system file passed the check, and its write failed after the table
        // files were written.
        TEST_METHOD(OutputFolder_AFileThatCannotBeReplaced_FailsBeforeTheFirstWrite)
        {
            NoAppStateForBatch noAppState;
            const char *widget =
                "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
                "(class S2OutWidget of Obj\n    (properties\n        s2OutWidgetSize 0\n    )\n)\n";
            for (std::string kind : { "hidden", "system", "folder", "held" })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                fs::path out = fs::path(_copyFolder) / "S2Out";
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2OutWidget", 907, widget) };
                // The file of the script: the table files come before it.
                std::string target = (out / "script.907").string();
                HANDLE held = INVALID_HANDLE_VALUE;
                if (kind == "folder")
                {
                    fs::create_directories(target);
                }
                else
                {
                    {
                        std::ofstream file(target.c_str(), std::ios::binary | std::ios::trunc);
                        file << "an old file";
                    }
                    if (kind == "held")
                    {
                        held = CreateFileA(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                        Assert::IsTrue(held != INVALID_HANDLE_VALUE, L"setup: the file is held");
                    }
                    else
                    {
                        Assert::IsTrue(SetFileAttributesA(target.c_str(), (kind == "hidden") ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_SYSTEM) != 0);
                    }
                }
                CompileOptions options = ToPatchFiles();
                options.write.outDir = out.string();
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                if (held != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(held);
                }
                if (kind != "folder")
                {
                    // So that the clean-up can remove the copy.
                    SetFileAttributesA(target.c_str(), FILE_ATTRIBUTE_NORMAL);
                }
                Assert::IsTrue(report.has_value());
                std::string facts = kind + ": " + Describe(*report);
                Assert::IsTrue(report->scripts[0].status.has_value(), WideForBatch(facts).c_str());
                Assert::IsFalse(report->commit.has_value(), WideForBatch(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("script.907") != std::string::npos, WideForBatch(facts).c_str());
                Assert::IsFalse(fs::exists(out / "vocab.996") || fs::exists(out / "vocab.997"), WideForBatch("a table file is written: " + facts).c_str());
            }
        }

        // Review of 4247f34c: with raw files, the check before the first write
        // shares the file as the raw write does (read and write), so a program
        // that has the file open with that sharing does not stop the commit.
        // Before, the check shared nothing, and refused it.
        TEST_METHOD(OutputFolderRaw_AFileThatAnotherProgramShares_IsWritten)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            fs::path out = fs::path(_copyFolder) / "S2Out";
            fs::create_directories(out);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            std::string target = (out / "script.904.bin").string();
            {
                std::ofstream file(target.c_str(), std::ios::binary | std::ios::trunc);
                file << "an old file";
            }
            HANDLE reader = CreateFileA(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(reader != INVALID_HANDLE_VALUE, L"setup: the file is open");
            CompileOptions options = ToPatchFiles();
            options.write.outDir = out.string();
            options.write.raw = true;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            CloseHandle(reader);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"");
            std::vector<uint8_t> written = BytesOf(target);
            Assert::IsTrue(std::string(written.begin(), written.end()) != "an old file", L"the file has the new data");
        }

        // Review of 4247f34c: a script whose .sco cannot be written leaves no
        // new debug file (the .sco comes first again), and the failed .sco is
        // not a change, so the batch runs one pass. Before, the debug file was
        // new, and the batch ran every pass that the options allow.
        TEST_METHOD(ObjectFileWriteError_NoDebugFile_OnePass)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
            const char *widget =
                "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
                "(class S2ScoWidget of Obj\n    (properties\n        s2ScoWidgetSize 0\n    )\n)\n";
            ScriptId script = WriteScript(session, "S2ScoWidget", 907, widget);
            std::string sco = session.Helper().GetScriptObjectFileName("S2ScoWidget");
            {
                std::ofstream file(sco.c_str(), std::ios::binary | std::ios::trunc);
                file << "not the new object file";
            }
            Assert::IsTrue(SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
            std::string debugFile = session.Helper().GetScriptDebugFileName(907);
            // The folder is there, so a debug file after a failed .sco would be written.
            fs::create_directories(fs::path(debugFile).parent_path());
            std::error_code ec;
            fs::remove(debugFile, ec);
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, { script }, options, abort, events);
            // Writable again, so that the clean-up can remove the copy.
            SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_NORMAL);
            Assert::IsTrue(report.has_value());
            std::string facts = Describe(*report);
            Assert::IsFalse(report->scripts[0].status.has_value(), WideForBatch("setup: the script fails: " + facts).c_str());
            Assert::IsFalse(fs::exists(debugFile), WideForBatch("no new debug file: " + facts).c_str());
            Assert::AreEqual(1, report->passes, WideForBatch(facts).c_str());
            Assert::IsFalse(report->passLimit, WideForBatch(facts).c_str());
            Assert::IsFalse(GameHasFile("script.907"), L"the script is not written");
        }

        // Review of 944de1df: with an output folder, a dry run checks the files
        // as the write would. Before, a read-only file failed the real run and
        // passed the dry run.
        TEST_METHOD(DryRun_OutputFolder_ChecksTheFiles)
        {
            NoAppStateForBatch noAppState;
            for (bool dryRun : { false, true })
            {
                GameSession session(TestSessionOptions());
                OpenCopy("\\TemplateGame\\SCI0", session);
                fs::path out = fs::path(_copyFolder) / "S2Out";
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
                std::string held = (out / "script.904").string();
                {
                    std::ofstream file(held.c_str(), std::ios::binary | std::ios::trunc);
                    file << "an old file";
                }
                Assert::IsTrue(SetFileAttributesA(held.c_str(), FILE_ATTRIBUTE_READONLY) != 0);
                CompileOptions options = ToPatchFiles();
                options.write.outDir = out.string();
                options.write.writeResources = !dryRun;
                options.write.writeObjectFile = !dryRun;
                options.write.writeDebugInfo = !dryRun;
                std::atomic<bool> abort(false);
                TestCompileEvents events;
                auto report = CompileScripts(session, scripts, options, abort, events);
                // Writable again, so that the clean-up can remove the copy.
                SetFileAttributesA(held.c_str(), FILE_ATTRIBUTE_NORMAL);
                Assert::IsTrue(report.has_value());
                std::string facts = std::string(dryRun ? "dry run: " : "real run: ") + Describe(*report);
                Assert::IsTrue(report->scripts[0].status.has_value(), WideForBatch(facts).c_str());
                Assert::IsFalse(report->commit.has_value(), WideForBatch(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("script.904") != std::string::npos, WideForBatch(facts).c_str());
            }
        }

        // Review of 944de1df: a path of 260 characters or more fails the check
        // before the first write. Before, it passed the check, the table files
        // were written, and the write of the script file failed.
        TEST_METHOD(OutputFolder_APathTooLong_FailsBeforeTheFirstWrite)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            const char *widget =
                "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
                "(class S2LongWidget of Obj\n    (properties\n        s2LongWidgetSize 0\n    )\n)\n";
            std::vector<ScriptId> scripts = { WriteScript(session, "S2LongWidget", 907, widget) };
            // A folder of 245 characters: "\vocab.996.bin" ends at 259, and
            // "\script.907.bin" at 260.
            Assert::IsTrue(_copyFolder.size() < 200, L"setup: a short copy folder");
            fs::path out = _copyFolder + "\\" + std::string(245 - _copyFolder.size() - 1, 'o');
            Assert::AreEqual((size_t)245, out.string().size());
            fs::create_directories(out);
            CompileOptions options = ToPatchFiles();
            options.write.outDir = out.string();
            options.write.raw = true;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::IsTrue(report.has_value());
            std::string facts = Describe(*report);
            Assert::IsTrue(report->scripts[0].status.has_value(), WideForBatch(facts).c_str());
            Assert::IsFalse(report->commit.has_value(), WideForBatch(facts).c_str());
            Assert::IsTrue(report->commit.error().ToString().find("script.907.bin") != std::string::npos, WideForBatch(facts).c_str());
            Assert::IsFalse(fs::exists(out / "vocab.996.bin") || fs::exists(out / "vocab.997.bin"), WideForBatch("a table file is written: " + facts).c_str());
        }

        // Review of 944de1df: a dry run that writes .sco files puts them back:
        // it commits nothing, so the game's scripts stay as they were. Before,
        // the new .sco stayed.
        TEST_METHOD(DryRun_PutsBackTheObjectFiles)
        {
            NoAppStateForBatch noAppState;
            GameSession session(TestSessionOptions());
            OpenCopy("\\TemplateGame\\SCI0", session);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            std::string sco = session.Helper().GetScriptObjectFileName("S2Good904");
            Assert::IsFalse(fs::exists(sco), L"setup: no .sco before the batch");
            CompileOptions options = ToPatchFiles();
            options.write.writeResources = false;
            std::atomic<bool> abort(false);
            TestCompileEvents events;
            auto report = CompileScripts(session, scripts, options, abort, events);
            Assert::IsTrue(report.has_value() && report->Succeeded(), report ? WideForBatch(Describe(*report)).c_str() : L"");
            Assert::IsFalse(fs::exists(sco), L"the .sco of the dry run goes");
            Assert::AreEqual((size_t)1, report->restoredObjectFiles.size());
        }
    };
}
