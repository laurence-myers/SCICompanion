#include "stdafx.h"
#include "CppUnitTest.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "CompileBatch.h"
#include "ScriptCatalog.h"
#include "CompileBatchGui.h"
#include "ExitCodes.h"
#include "FileWrite.h"
#include "Vocab99x.h"
#include "CompiledScript.h"
#include "ResourceBlob.h"
#include "Helper.h"
#include "TestSupport.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
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

    // Asserts that the batch started and succeeded. The message is the
    // error or the report, after the text of "what".
    void AssertSucceeded(const sci::Result<CompileReport> &report, const std::string &what = std::string())
    {
        AssertOk(report, what);
        Assert::IsTrue(report->Succeeded(), Wide(what.empty() ? Describe(*report) : what + ": " + Describe(*report)).c_str());
    }

    enum class DiagnosticKind { Any, Error, Warning };

    // The first diagnostic of the script that is of the kind and has the
    // text; null when there is none.
    const CompileResult *FindDiagnostic(const ScriptOutcome &outcome, DiagnosticKind kind, const std::string &text = std::string())
    {
        for (const CompileResult &result : outcome.diagnostics)
        {
            bool ofKind = (kind == DiagnosticKind::Any) || ((kind == DiagnosticKind::Error) ? result.IsError() : result.IsWarning());
            if (ofKind && (result.GetMessage().find(text) != std::string::npos))
            {
                return &result;
            }
        }
        return nullptr;
    }

    // Test events: a fault at one script, and an abort after a count of
    // scripts or in a later pass. The batch reads the abort flag of the
    // events.
    class TestCompileEvents : public ICompileEvents
    {
    public:
        std::atomic<bool> abort{ false };
        int throwAt = -1;
        // Sets abort when this count of scripts is done (0: no abort).
        size_t abortAfterDone = 0;
        // Sets abort when a script of this pass or of a later pass is done
        // (0: no abort).
        int abortFromPass = 0;
        // Sets abort in OnPassStart (as Ctrl+C of scic can, from another
        // thread, while the batch starts a pass).
        bool abortAtPassStart = false;
        std::vector<size_t> started;
        size_t done = 0;
        int passesStarted = 0;
        int pass = 1;

        void OnPassStart(int newPass) override
        {
            passesStarted++;
            pass = newPass;
            if (abortAtPassStart)
            {
                abort.store(true);
            }
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
            if (((abortAfterDone > 0) && (done >= abortAfterDone)) || ((abortFromPass > 0) && (pass >= abortFromPass)))
            {
                abort.store(true);
            }
        }
    };

    // CompileScripts with the abort flag of the events.
    sci::Result<CompileReport> Compile(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options, TestCompileEvents &events)
    {
        return CompileScripts(session, std::move(scripts), options, events.abort, events);
    }

    // CompileScripts with events that the test does not read.
    sci::Result<CompileReport> Compile(GameSession &session, std::vector<ScriptId> scripts, const CompileOptions &options)
    {
        TestCompileEvents events;
        return Compile(session, std::move(scripts), options, events);
    }

    // The bytes of a small patch file of a script, and of a text.
    const std::vector<uint8_t> ScriptPatch = { 0x80 | (uint8_t)ResourceType::Script, 0, 1, 2 };
    const std::vector<uint8_t> TextPatch = { 0x80 | (uint8_t)ResourceType::Text, 0, 'x', 0 };

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

    // For the passes: S2PassB uses the class of S2PassA.
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

    // Script 907 with the new class S2<name>Widget, so the compile adds a
    // species and selectors to the tables.
    std::string WidgetText(const std::string &name)
    {
        return "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
            "(class S2" + name + "Widget of Obj\n    (properties\n        s2" + name + "WidgetSize 0\n    )\n)\n";
    }
}

namespace UnitTests
{
    // CompileScripts compiles many scripts as one batch: each script gets its
    // own log and its own status, inside an exception boundary.
    TEST_CLASS(TestCompileBatch)
    {
        GameCopy _game;

        static ScriptId WriteScript(GameSession &session, const std::string &name, uint16_t number, const std::string &text)
        {
            std::string path = session.Helper().GetScriptFileName(name);
            WriteFileText(path, text);
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

        // Patch files that would hide a package write of script 904 and of
        // vocab 997 (a copy of the package's table, so the tables load).
        void WritePatches(GameSession &session)
        {
            WriteFileBytes(_game.Path("script.904"), ScriptPatch);
            const GameFolderHelper &helper = session.Helper();
            std::unique_ptr<ResourceBlob> selectors = helper.MostRecentResource(ResourceType::Vocab, 997, ResourceEnumFlags::None);
            Assert::IsTrue(selectors != nullptr);
            std::vector<uint8_t> data(selectors->GetData(), selectors->GetData() + selectors->GetLength());
            ResourceBlob patch(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            AssertOk(session.ResourceMap().WriteResource(patch));
            Assert::IsTrue(_game.Has("vocab.997"), L"setup: vocab.997");
        }

        static CompileOptions ToPatchFiles()
        {
            CompileOptions options;
            options.write.saveTo = ResourceSaveLocation::Patch;
            return options;
        }

        // A dry run writes no resource, no .sco file and no debug file.
        static void MakeDry(CompileOptions &options, bool dryRun = true)
        {
            options.write.writeResources = !dryRun;
            options.write.writeObjectFile = !dryRun;
            options.write.writeDebugInfo = !dryRun;
        }

        // The species of the classes of the script that the compile wrote, in
        // their order.
        static std::vector<uint16_t> CompiledSpecies(GameSession &session, uint16_t number, std::vector<const CompiledObject *> *classesOut = nullptr, std::unique_ptr<CompiledScript> *keep = nullptr)
        {
            auto compiled = std::make_unique<CompiledScript>(number);
            AssertOk(compiled->TryLoad(session.Helper(), session.Helper().Version, number), "the compiled script");
            std::vector<uint16_t> species;
            for (const auto &object : compiled->GetObjects())
            {
                if (!object->IsInstance())
                {
                    species.push_back(object->GetSpecies());
                    if (classesOut)
                    {
                        classesOut->push_back(object.get());
                    }
                }
            }
            if (keep)
            {
                *keep = std::move(compiled);
            }
            return species;
        }

        static std::string TwoClassText(const std::vector<std::string> &names)
        {
            std::string text = "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n";
            for (const std::string &name : names)
            {
                text += "(class S2" + name + " of Obj\n    (properties\n        s2" + name + "Size 0\n    )\n)\n";
            }
            return text;
        }

        // A class keeps its species by its name: a class that moves keeps it,
        // and the species of a leftover class (one that the table gives
        // another script: KQ5 script 992 has Rev, species 24 of script 978)
        // goes only to the class with its name. Removing the leftover class
        // leaves the next class its own species, not the leftover's.
        TEST_METHOD(Species_FollowTheClassNames)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            AssertSucceeded(Compile(session, { WriteScript(session, "S2Classes", 907, TwoClassText({ "Alpha", "Beta" })) }, ToPatchFiles()));
            std::vector<const CompiledObject *> classes;
            std::unique_ptr<CompiledScript> keep;
            std::vector<uint16_t> first = CompiledSpecies(session, 907, &classes, &keep);
            Assert::AreEqual((size_t)2, first.size());
            uint16_t alpha = first[0];
            uint16_t beta = first[1];

            // The classes in the other order keep their species.
            AssertSucceeded(Compile(session, { WriteScript(session, "S2Classes", 907, TwoClassText({ "Beta", "Alpha" })) }, ToPatchFiles()));
            std::vector<uint16_t> swapped = CompiledSpecies(session, 907);
            Assert::AreEqual((int)beta, (int)swapped[0]);
            Assert::AreEqual((int)alpha, (int)swapped[1]);

            // Alpha with the species of another script (a leftover class).
            AssertSucceeded(Compile(session, { WriteScript(session, "S2Classes", 907, TwoClassText({ "Alpha", "Beta" })) }, ToPatchFiles()));
            classes.clear();
            CompiledSpecies(session, 907, &classes, &keep);
            SpeciesTable table;
            Assert::IsTrue(table.Load(session.Helper(), false));
            uint16_t other = 0;
            uint16_t otherScript = 0;
            uint16_t place = 0;
            while (table.GetSpeciesLocation(SpeciesIndex(other), otherScript, place) && (otherScript == 907))
            {
                other++;
            }
            Assert::AreNotEqual((uint16_t)907, otherScript, L"setup: a species of another script");
            const GameFolderHelper &helper = session.Helper();
            std::unique_ptr<ResourceBlob> original = helper.MostRecentResource(ResourceType::Script, 907, ResourceEnumFlags::None);
            std::vector<uint8_t> data(original->GetData(), original->GetData() + original->GetLength());
            size_t alphaAt = classes[0]->GetPosInResource() + 6;
            Assert::IsTrue((data[alphaAt] | (data[alphaAt + 1] << 8)) == alpha, L"setup: the species is where the SCI0 format puts it");
            data[alphaAt] = (uint8_t)(other & 0xff);
            data[alphaAt + 1] = (uint8_t)(other >> 8);
            ResourceBlob patched(helper, nullptr, ResourceType::Script, data, helper.Version.DefaultVolumeFile, 907, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            AssertOk(session.ResourceMap().WriteResource(patched));

            AssertSucceeded(Compile(session, { WriteScript(session, "S2Classes", 907, TwoClassText({ "Alpha", "Beta" })) }, ToPatchFiles()));
            std::vector<uint16_t> leftover = CompiledSpecies(session, 907);
            Assert::AreEqual((int)other, (int)leftover[0], L"the leftover class keeps its species");
            Assert::AreEqual((int)beta, (int)leftover[1], L"the class after it keeps its species");

            // Without the leftover class, Beta keeps its species.
            AssertSucceeded(Compile(session, { WriteScript(session, "S2Classes", 907, TwoClassText({ "Beta" })) }, ToPatchFiles()));
            std::vector<uint16_t> removed = CompiledSpecies(session, 907);
            Assert::AreEqual((size_t)1, removed.size());
            Assert::AreEqual((int)beta, (int)removed[0], L"the class after a removed leftover class keeps its species");
        }


        // A method named sel_<number> of a selector that has a name (the
        // decompiler writes a selector whose name the compiler gives another
        // selector, or a keyword, that way): the class gets the method of
        // that selector.
        TEST_METHOD(Methods_ANumberedNameOfANamedSelector)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            SelectorTable selectors;
            Assert::IsTrue(selectors.Load(session.Helper()));
            uint16_t doit = 0;
            Assert::IsTrue(selectors.ReverseLookup("doit", doit));
            std::string text = "(script# 907)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use obj)\n"
                "(instance s2Numbered of Obj\n    (properties)\n    (method (sel_" + std::to_string(doit) + ")\n        (return 1)\n    )\n)\n";
            AssertSucceeded(Compile(session, { WriteScript(session, "S2Numbered", 907, text) }, ToPatchFiles()));
            CompiledScript compiled(907);
            AssertOk(compiled.TryLoad(session.Helper(), session.Helper().Version, 907), "the compiled script");
            bool found = false;
            for (const auto &object : compiled.GetObjects())
            {
                for (uint16_t method : object->GetMethods())
                {
                    found = found || (method == doit);
                }
            }
            Assert::IsTrue(found, L"the instance has the method of the selector");
        }


    public:
        // Every script of both templates compiles in one batch, with no
        // error, and the tables and the one commit are Ok.
        TEST_METHOD(CompileAll_Templates_NoErrors)
        {
            NoAppState noAppState;
            for (const char *templateFolder : { TemplateSci0, TemplateSci11 })
            {
                GameSession &session = _game.OpenCopy(templateFolder);
                auto selection = SelectAllScripts(session, SelectorMode::Compile);
                AssertOk(selection, "setup: the scripts must be found");
                Assert::IsTrue(selection->scripts.size() > 20, L"setup: the template has its scripts");
                TestCompileEvents events;
                auto report = Compile(session, selection->scripts, CompileOptions(), events);
                AssertSucceeded(report);
                Assert::AreEqual(selection->scripts.size(), report->CompiledCount());
                Assert::AreEqual((size_t)0, report->ErrorCount(), Wide(Describe(*report)).c_str());
                Assert::AreEqual(selection->scripts.size(), events.done, L"one OnScriptDone for each script");
            }
        }

        // One broken script does not stop the others: they compile and are
        // written, and the report has one Compile status, with its two
        // errors.
        TEST_METHOD(OneBrokenScript_TheOthersAreWritten)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            auto report = Compile(session, ThreeScripts(session), ToPatchFiles());
            AssertOk(report);
            Assert::AreEqual((size_t)3, report->scripts.size(), Wide(Describe(*report)).c_str());
            Assert::IsTrue(report->scripts[0].status.has_value() && report->scripts[2].status.has_value(), Wide(Describe(*report)).c_str());
            Assert::IsFalse(report->scripts[1].status.has_value());
            Assert::IsTrue(report->scripts[1].status.error().code == sci::ErrorCode::Compile, Wide(Describe(*report)).c_str());
            Assert::AreEqual((size_t)2, report->ErrorCount(), Wide(Describe(*report)).c_str());
            Assert::IsTrue(report->commit.has_value() && report->tables.has_value(), Wide(Describe(*report)).c_str());
            Assert::IsTrue(_game.Has("script.904") && _game.Has("script.906"), L"the good scripts are written");
            Assert::IsFalse(_game.Has("script.905"), L"the broken script is not written");
            Assert::IsFalse(report->Succeeded());
        }

        // An exception in the compile of one script is an Internal status of
        // that script, with the text in its diagnostics; the batch goes on.
        TEST_METHOD(ScriptThatThrows_IsInternal_TheBatchGoesOn)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2Good904", 904, GoodText(904)),
                WriteScript(session, "S2Good906", 906, GoodText(906)),
            };
            TestCompileEvents events;
            events.throwAt = 0;
            auto report = Compile(session, scripts, ToPatchFiles(), events);
            AssertOk(report);
            Assert::AreEqual((size_t)2, report->scripts.size());
            Assert::IsFalse(report->scripts[0].status.has_value());
            Assert::IsTrue(report->scripts[0].status.error().code == sci::ErrorCode::Internal, Wide(Describe(*report)).c_str());
            // The output pane and the command line show the diagnostics.
            Assert::IsNotNull(FindDiagnostic(report->scripts[0], DiagnosticKind::Error, "an injected fault"), Wide(Describe(*report)).c_str());
            Assert::IsTrue(report->scripts[1].status.has_value(), Wide(Describe(*report)).c_str());
            Assert::IsFalse(_game.Has("script.904"));
            Assert::IsTrue(_game.Has("script.906"), L"the batch goes on after the fault");
        }

        // The counts of a log are the counts of its results: a second count
        // does not add them again.
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
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            TestCompileEvents events;
            events.abortAfterDone = 1;
            auto report = Compile(session, ThreeScripts(session), ToPatchFiles(), events);
            AssertOk(report);
            Assert::IsTrue(report->cancelled);
            Assert::AreEqual((size_t)1, report->scripts.size(), Wide(Describe(*report)).c_str());
            Assert::IsTrue(report->commit.has_value());
            Assert::IsTrue(_game.Has("script.904"), L"the finished script is written");
            Assert::IsFalse(_game.Has("script.906"), L"the batch stopped before script 906");
        }

        // failFast stops after the first script that fails.
        TEST_METHOD(FailFast_StopsAfterTheFirstFailure)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = ThreeScripts(session);
            std::swap(scripts[0], scripts[1]);   // 905 (bad) first
            CompileOptions options = ToPatchFiles();
            options.failFast = true;
            auto report = Compile(session, scripts, options);
            AssertOk(report);
            Assert::IsTrue(report->stopped);
            Assert::AreEqual((size_t)1, report->scripts.size(), Wide(Describe(*report)).c_str());
            Assert::IsFalse(_game.Has("script.904") || _game.Has("script.906"), L"nothing after the failure compiles");
        }

        // The table rule of plan section 4.5: when no script compiled, the
        // tables are not saved, although the failed compile changed them.
        TEST_METHOD(NoScriptCompiled_TablesAreNotSaved)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2BadWidget", 907, BadClassText) };
            auto report = Compile(session, scripts, ToPatchFiles());
            AssertOk(report);
            Assert::AreEqual((size_t)0, report->CompiledCount(), Wide(Describe(*report)).c_str());
            Assert::IsTrue(report->tables.has_value());
            Assert::IsFalse(_game.Has("vocab.996") || _game.Has("vocab.997"), L"the tables must not be saved when no script compiled");
        }

        // A batch that is not finished withdraws its queued writes, and puts
        // back the .sco files that it changed: a new one goes, and one that
        // was there gets its old bytes.
        TEST_METHOD(UnfinishedBatch_WithdrawsItsWrites)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            ScriptId old = WriteScript(session, "S2Good906", 906, GoodText(906));
            AssertSucceeded(Compile(session, { old }, ToPatchFiles()), "setup: S2Good906 has a .sco");
            std::string oldSco = session.Helper().GetScriptObjectFileName("S2Good906");
            std::vector<uint8_t> oldScoBytes = ReadFileBytes(oldSco);
            // A new export name: the .sco of S2Good906 changes.
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2Good904", 904, GoodText(904)),
                WriteScript(session, "S2Good906", 906,
                    "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2Other906 0)\n(procedure (s2Other906)\n    (return 7)\n)\n"),
            };
            std::string newSco = session.Helper().GetScriptObjectFileName("S2Good904");
            std::vector<uint8_t> map = ReadFileBytes(_game.Path("resource.map"));
            std::vector<uint8_t> oldPatch = ReadFileBytes(_game.Path("script.906"));
            {
                auto batch = CompileBatch::Start(session, scripts, ToPatchFiles());
                AssertOk(batch);
                TestCompileEvents events;
                Assert::IsTrue((*batch)->Step(events.abort, events));
                Assert::IsTrue((*batch)->Step(events.abort, events));
                Assert::IsTrue(fs::exists(newSco), L"setup: the batch writes the new .sco");
                Assert::IsTrue(oldScoBytes != ReadFileBytes(oldSco), L"setup: the batch changes the old .sco");
            }
            Assert::IsTrue(map == ReadFileBytes(_game.Path("resource.map")), L"the package must not change");
            Assert::IsFalse(_game.Has("script.904"), L"no new patch file");
            Assert::IsTrue(oldPatch == ReadFileBytes(_game.Path("script.906")), L"the patch file must not change");
            Assert::IsFalse(session.ResourceMap().IsDeferring(), L"the batch must close its deferred writes");
            Assert::IsFalse(fs::exists(newSco), L"the new .sco goes");
            Assert::IsTrue(oldScoBytes == ReadFileBytes(oldSco), L"the old .sco gets its old bytes");
        }

        // Two scripts that compile to one number: the .sco of each goes back,
        // not only that of the first.
        TEST_METHOD(ObjectFiles_TwoScriptsWithOneNumber_EachGoesBack)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            // S2TwinB declares 904 too (a warning).
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2TwinA", 904, GoodText(904)),
                WriteScript(session, "S2TwinB", 905, GoodText(904)),
            };
            std::string scoA = session.Helper().GetScriptObjectFileName("S2TwinA");
            std::string scoB = session.Helper().GetScriptObjectFileName("S2TwinB");
            CompileOptions options = ToPatchFiles();
            options.write.writeResources = false;
            auto report = Compile(session, scripts, options);
            AssertOk(report);
            std::string facts = Describe(*report);
            Assert::AreEqual((size_t)2, report->CompiledCount(), Wide("setup: both compile: " + facts).c_str());
            Assert::IsFalse(fs::exists(scoA) || fs::exists(scoB), Wide("both new .sco files go: " + facts).c_str());
            Assert::AreEqual((size_t)2, report->removedObjectFiles.size(), Wide(facts).c_str());
        }

        // A .sco that cannot go back is an Io error in report.objectFiles:
        // the batch did not succeed, and scic exits with 9. The text has one
        // error code.
        TEST_METHOD(ObjectFiles_ARestoreThatFails_IsAnIoError)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, GoodText(904)) };
            AssertSucceeded(Compile(session, scripts, ToPatchFiles()), "setup: S2PassA has a .sco");
            WriteScript(session, "S2PassA", 904, PassAText);
            std::string sco = session.Helper().GetScriptObjectFileName("S2PassA");
            CompileOptions options = ToPatchFiles();
            options.write.writeResources = false;
            auto batch = CompileBatch::Start(session, scripts, options);
            AssertOk(batch);
            TestCompileEvents events;
            Assert::IsTrue((*batch)->Step(events.abort, events));
            Assert::IsTrue(SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: a read-only .sco");
            CompileReport report = (*batch)->Finish();
            SetFileAttributesA(sco.c_str(), FILE_ATTRIBUTE_NORMAL);
            std::string facts = Describe(report);
            Assert::IsTrue(report.scripts[0].status.has_value() && report.commit.has_value(), Wide("setup: " + facts).c_str());
            Assert::IsFalse(report.objectFiles.has_value(), Wide(facts).c_str());
            Assert::IsTrue(report.objectFiles.error().code == sci::ErrorCode::Io, Wide(facts).c_str());
            std::string error = report.objectFiles.error().ToString();
            Assert::IsTrue(error.find(sco) != std::string::npos, Wide(error).c_str());
            Assert::AreEqual((size_t)1, CountOf(error, "[io]"), Wide(error).c_str());
            Assert::IsFalse(report.Succeeded(), L"a .sco that did not go back is not a success");
            Assert::AreEqual((int)cli::ExitCode::WriteFailed, (int)cli::ExitCodeForReport(report), Wide(facts).c_str());
        }

        // The batch cannot start with the package of a patch-mode game, or
        // with an output folder and the package.
        TEST_METHOD(Start_RefusesABadDestination)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };

            CompileOptions withOutDir;
            withOutDir.write.saveTo = ResourceSaveLocation::Package;
            withOutDir.write.outDir = _game.Folder();
            auto usage = CompileBatch::Start(session, scripts, withOutDir);
            Assert::IsFalse(usage.has_value());
            Assert::IsTrue(usage.error().code == sci::ErrorCode::Usage, Wide(usage.error().ToString()).c_str());

            session.Helper().SetResourceSaveLocation(ResourceSaveLocation::Patch);
            CompileOptions toPackage;
            toPackage.write.saveTo = ResourceSaveLocation::Package;
            auto refused = CompileBatch::Start(session, scripts, toPackage);
            Assert::IsFalse(refused.has_value());
            Assert::IsTrue(refused.error().code == sci::ErrorCode::WriteRefused, Wide(refused.error().ToString()).c_str());
            Assert::IsFalse(session.ResourceMap().IsDeferring(), L"a batch that did not start opens no deferred writes");
        }

        // The passes of plan section 4.5: script 906 uses script 904,
        // which has no .sco file yet. Pass 1 fails 906 and writes S2PassA.sco;
        // pass 2 compiles 906; pass 3 changes no .sco file and is the last.
        // The commit holds the last pass. With one pass, 906 fails.
        TEST_METHOD(Passes_UntilNoObjectFileChanges)
        {
            NoAppState noAppState;
            for (int passes : { 1, 5 })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                std::vector<ScriptId> scripts = {
                    WriteScript(session, "S2PassB", 906, PassBText),
                    WriteScript(session, "S2PassA", 904, PassAText),
                };
                CompileOptions options = ToPatchFiles();
                options.passes = passes;
                TestCompileEvents events;
                auto report = Compile(session, scripts, options, events);
                AssertOk(report);
                if (passes == 1)
                {
                    Assert::AreEqual(1, report->passes);
                    Assert::IsFalse(report->scripts[0].status.has_value(), L"one pass: 906 has no S2PassA.sco yet");
                    Assert::IsTrue(report->scripts[1].status.has_value());
                    Assert::IsFalse(_game.Has("script.906"));
                }
                else
                {
                    Assert::AreEqual(3, report->passes, Wide(Describe(*report)).c_str());
                    Assert::AreEqual(2, events.passesStarted, L"OnPassStart for passes 2 and 3");
                    Assert::IsTrue(report->Succeeded(), Wide(Describe(*report)).c_str());
                    Assert::AreEqual((size_t)2, report->scripts.size(), L"the report has the last pass");
                    Assert::IsTrue(_game.Has("script.904") && _game.Has("script.906"), L"the commit holds the last pass");
                }
            }
        }

        // A .sco file is written only when its bytes change, so a pass can
        // see that nothing changed.
        TEST_METHOD(ObjectFile_IsWrittenOnlyWhenItChanges)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            AssertSucceeded(Compile(session, scripts, ToPatchFiles()));
            fs::path sco = session.Helper().GetScriptObjectFileName("S2PassA");
            fs::file_time_type old = fs::last_write_time(sco) - std::chrono::hours(24 * 365);
            fs::last_write_time(sco, old);

            AssertSucceeded(Compile(session, scripts, ToPatchFiles()));
            Assert::IsTrue(fs::last_write_time(sco) == old, L"the .sco file has the same bytes, so it must not be written again");
        }

        // The shadow check of plan section 5: a patch file that would hide a
        // package write refuses the batch.
        TEST_METHOD(ShadowingPatches_RefuseTheBatch)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            WritePatches(session);
            std::vector<uint8_t> map = ReadFileBytes(_game.Path("resource.map"));

            auto started = CompileBatch::Start(session, scripts, CompileOptions());
            Assert::IsFalse(started.has_value());
            Assert::IsTrue(started.error().code == sci::ErrorCode::WriteRefused, Wide(started.error().ToString()).c_str());
            Assert::IsTrue(started.error().message.find("script.904") != std::string::npos, Wide(started.error().message).c_str());
            Assert::IsTrue(started.error().message.find("vocab.997") != std::string::npos, Wide(started.error().message).c_str());
            Assert::IsTrue(map == ReadFileBytes(_game.Path("resource.map")));
        }

        // With Replace, the package is written, and the patch files move to
        // replaced-patches\<time>.
        TEST_METHOD(ShadowingPatches_Replace_MovesThemAfterTheCommit)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            WritePatches(session);
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            auto report = Compile(session, scripts, options);
            AssertSucceeded(report);
            Assert::AreEqual((size_t)2, report->movedPatches.size());
            Assert::IsFalse(_game.Has("script.904") || _game.Has("vocab.997"), L"the patch files are moved");
            Assert::IsTrue(_game.Has("replaced-patches"));

            GameSession &reopened = _game.Open();
            std::unique_ptr<ResourceBlob> script = reopened.Helper().MostRecentResource(ResourceType::Script, 904, ResourceEnumFlags::None);
            Assert::IsTrue(script && (script->GetSourceFlags() == ResourceSourceFlags::ResourceMap), L"the package has script 904");
        }

        // The batch checks the queued package writes again before the
        // commit: a script's auto text is known only after its compile.
        TEST_METHOD(ShadowingPatches_TheQueuedTextIsCheckedBeforeTheCommit)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
            WriteFileBytes(_game.Path("text.904"), TextPatch);
            std::vector<uint8_t> map = ReadFileBytes(_game.Path("resource.map"));
            auto report = Compile(session, scripts, CompileOptions());
            AssertOk(report, "the start does not know the text yet");
            Assert::IsTrue(report->scripts[0].status.has_value(), Wide(Describe(*report)).c_str());
            Assert::IsFalse(report->commit.has_value());
            Assert::IsTrue(report->commit.error().code == sci::ErrorCode::WriteRefused, Wide(Describe(*report)).c_str());
            Assert::IsTrue(report->commit.error().message.find("text.904") != std::string::npos, Wide(Describe(*report)).c_str());
            Assert::IsTrue(map == ReadFileBytes(_game.Path("resource.map")), L"nothing is written");
        }

        // The shadow check of an SCI1.1 game finds the patch file 997.voc.
        TEST_METHOD(ShadowingPatches_Sci11_Finds997Voc)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci11);
            const GameFolderHelper &helper = session.Helper();
            std::unique_ptr<ResourceBlob> selectors = helper.MostRecentResource(ResourceType::Vocab, 997, ResourceEnumFlags::None);
            Assert::IsTrue(selectors != nullptr);
            std::vector<uint8_t> data(selectors->GetData(), selectors->GetData() + selectors->GetLength());
            ResourceBlob patch(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            AssertOk(session.ResourceMap().WriteResource(patch));
            Assert::IsTrue(_game.Has("997.voc"), L"setup: 997.voc");

            auto started = CompileBatch::Start(session, { WriteScript(session, "S2Good904", 904, GoodText(904)) }, CompileOptions());
            Assert::IsFalse(started.has_value());
            Assert::IsTrue(started.error().code == sci::ErrorCode::WriteRefused, Wide(started.error().ToString()).c_str());
            Assert::IsTrue(started.error().message.find("997.voc") != std::string::npos, Wide(started.error().message).c_str());
        }

        // askShadows answers for the patch files at the start (the GUI asks
        // the user), and its answer is the policy from then on: the text
        // patch file that the batch finds before the commit gets no second
        // question. Refuse stops the batch with Cancelled.
        TEST_METHOD(AskShadows_TheAnswerAtTheStartIsThePolicy)
        {
            NoAppState noAppState;
            for (ShadowPolicy answer : { ShadowPolicy::Refuse, ShadowPolicy::Replace, ShadowPolicy::Ignore })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
                WritePatches(session);
                WriteFileBytes(_game.Path("text.904"), TextPatch);
                std::vector<uint8_t> map = ReadFileBytes(_game.Path("resource.map"));
                std::vector<std::vector<std::string>> questions;
                CompileOptions options;
                options.askShadows = [&](const std::vector<std::string> &files) { questions.push_back(files); return answer; };
                TestCompileEvents events;
                auto report = Compile(session, scripts, options, events);
                Assert::AreEqual((size_t)1, questions.size(), L"one question, at the start");
                Assert::AreEqual((size_t)2, questions[0].size(), L"script.904 and vocab.997");
                if (answer == ShadowPolicy::Refuse)
                {
                    Assert::IsFalse(report.has_value());
                    Assert::IsTrue(report.error().code == sci::ErrorCode::Cancelled, Wide(report.error().ToString()).c_str());
                    Assert::IsTrue(events.started.empty(), L"nothing is compiled");
                    Assert::IsTrue(map == ReadFileBytes(_game.Path("resource.map")), L"nothing is written");
                }
                else
                {
                    AssertSucceeded(report);
                    Assert::IsFalse(map == ReadFileBytes(_game.Path("resource.map")), L"the package is written");
                    bool replace = (answer == ShadowPolicy::Replace);
                    // Replace moves only the files that hide a written
                    // resource: text.904 too, but not vocab.997, because the
                    // tables did not change and the commit did not write
                    // them.
                    Assert::AreEqual(replace ? (size_t)2 : (size_t)0, report->movedPatches.size(), L"Replace also moves text.904");
                    Assert::AreEqual(!replace, _game.Has("text.904"));
                    Assert::AreEqual(!replace, _game.Has("script.904"));
                    Assert::IsTrue(_game.Has("vocab.997"), L"the tables were not written, so their patch file stays");
                }
            }
        }

        // A patch file that the batch finds only before the commit (the
        // script's auto text) gets its question then. Refuse writes nothing;
        // No (Ignore) keeps the file.
        TEST_METHOD(AskShadows_BeforeTheCommit)
        {
            NoAppState noAppState;
            for (ShadowPolicy answer : { ShadowPolicy::Refuse, ShadowPolicy::Replace, ShadowPolicy::Ignore })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
                WriteFileBytes(_game.Path("text.904"), TextPatch);
                std::vector<uint8_t> map = ReadFileBytes(_game.Path("resource.map"));
                std::vector<std::vector<std::string>> questions;
                CompileOptions options;
                options.askShadows = [&](const std::vector<std::string> &files) { questions.push_back(files); return answer; };
                auto report = Compile(session, scripts, options);
                AssertOk(report, "no question at the start: the start does not know the text");
                Assert::AreEqual((size_t)1, questions.size());
                Assert::IsTrue((questions[0].size() == 1) && (questions[0][0].find("text.904") != std::string::npos));
                if (answer == ShadowPolicy::Refuse)
                {
                    Assert::IsTrue(!report->commit && (report->commit.error().code == sci::ErrorCode::Cancelled), Wide(Describe(*report)).c_str());
                    Assert::IsTrue(map == ReadFileBytes(_game.Path("resource.map")), L"nothing is written");
                }
                else
                {
                    Assert::IsTrue(report->Succeeded(), Wide(Describe(*report)).c_str());
                    bool replace = (answer == ShadowPolicy::Replace);
                    Assert::AreEqual(!replace, _game.Has("text.904"), replace ? L"the text patch file is moved" : L"No keeps the text patch file");
                    Assert::AreEqual(replace ? (size_t)1 : (size_t)0, report->movedPatches.size());
                }
            }
        }

        // The warnings of a patch-file write (plan section 5).
        TEST_METHOD(PatchWrite_Warnings)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2PassA", 904, PassAText) };
            WriteFileBytes(_game.Path("script.0904"), ScriptPatch);
            auto report = Compile(session, scripts, ToPatchFiles());
            AssertSucceeded(report);
            std::string warnings;
            for (const std::string &warning : report->warnings)
            {
                warnings += warning + "\n";
            }
            Assert::IsTrue(warnings.find("script.0904") != std::string::npos, Wide(warnings).c_str());
            Assert::IsTrue(warnings.find("996 and 997") != std::string::npos, Wide(warnings).c_str());
        }

        // A script that fails after its resources are queued (here, its .sco
        // file cannot be written) writes no resource. No script compiled, so
        // the tables are not saved: a written script would give the game a
        // class that its class table does not have.
        TEST_METHOD(Savepoint_AFailedScript_WritesNoResource)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            ScriptId script = WriteScript(session, "S2RoWidget", 907, WidgetText("Ro"));
            WriteReadOnlyFile(session.Helper().GetScriptObjectFileName("S2RoWidget"), "not the new object file");
            auto report = Compile(session, { script }, ToPatchFiles());
            AssertOk(report);
            Assert::IsFalse(report->scripts[0].status.has_value(), L"the .sco file cannot be written");
            Assert::IsTrue(report->commit.has_value());
            Assert::IsFalse(_game.Has("script.907"), L"the failed script must write no resource");
            Assert::IsFalse(_game.Has("vocab.996") || _game.Has("vocab.997"), L"no script compiled, so the tables are not saved");
        }

        // Replace moves only the patch files that hide a resource that the
        // commit wrote. It does not move the file of a script that failed
        // (the game would then read the old package copy), or the vocab.997
        // of tables that did not change (the game would then lose
        // selectors).
        TEST_METHOD(Replace_MovesOnlyTheFilesOfWrittenResources)
        {
            NoAppState noAppState;
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Bad905", 905, BadText) };
                WriteFileBytes(_game.Path("script.905"), ScriptPatch);
                CompileOptions options;
                options.shadows = ShadowPolicy::Replace;
                auto report = Compile(session, scripts, options);
                AssertOk(report);
                Assert::IsFalse(report->scripts[0].status.has_value());
                Assert::IsTrue(report->movedPatches.empty(), L"the script did not compile");
                Assert::IsTrue(_game.Has("script.905"), L"the patch file of a script that did not compile stays");
            }
            GameSession &session = _game.OpenCopy(TemplateSci0);
            const GameFolderHelper &helper = session.Helper();
            SelectorTable selectors;
            Assert::IsTrue(selectors.Load(helper), L"setup: vocab 997 loads");
            selectors.Add("s2ReviewSelector");
            std::vector<uint8_t> data = selectors.MakeResourceData();
            ResourceBlob patch(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            AssertOk(session.ResourceMap().WriteResource(patch));
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            auto report = Compile(session, scripts, options);
            AssertSucceeded(report);
            Assert::IsTrue(_game.Has("vocab.997"), L"the tables did not change and were not written, so their patch file stays");
            GameSession &reopened = _game.Open();
            SelectorTable after;
            Assert::IsTrue(after.Load(reopened.Helper()));
            Assert::IsTrue(after.IsSelectorName("s2ReviewSelector"), L"the game keeps the selector of the patch file");
        }

        // A script that compiles in pass 1 and fails in pass 2 writes
        // nothing: the commit holds the last pass.
        TEST_METHOD(Passes_AScriptThatFailsInALaterPass_IsWithdrawn)
        {
            NoAppState noAppState;
            // In the game, and in an output folder, whose files wait for the
            // commit too.
            for (bool toFolder : { false, true })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                const char *oldY =
                    "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2YProc 0)\n(procedure (s2YProc)\n    (return 1)\n)\n";
                const char *newY =
                    "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2YOther 0)\n(procedure (s2YOther)\n    (return 2)\n)\n";
                const char *textX =
                    "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2PassY)\n(public s2X 0)\n(procedure (s2X)\n    (return (s2YProc))\n)\n";
                ScriptId y = WriteScript(session, "S2PassY", 904, oldY);
                AssertSucceeded(Compile(session, { y }, ToPatchFiles()), "setup: the old S2PassY compiles");
                fs::remove(_game.Path("script.904"));
                WriteScript(session, "S2PassY", 904, newY);
                ScriptId x = WriteScript(session, "S2PassX", 906, textX);
                CompileOptions options = ToPatchFiles();
                options.passes = 5;
                fs::path where = _game.Folder();
                if (toFolder)
                {
                    where /= "S2Out";
                    fs::create_directories(where);
                    options.write.outDir = where.string();
                }
                auto report = Compile(session, { x, y }, options);
                AssertOk(report);
                Assert::AreEqual(2, report->passes, Wide(Describe(*report)).c_str());
                Assert::IsFalse(report->scripts[0].status.has_value(), L"S2PassX fails in pass 2: s2YProc is gone");
                Assert::IsTrue(report->scripts[1].status.has_value(), Wide(Describe(*report)).c_str());
                Assert::IsTrue(fs::exists(where / "script.904"), toFolder ? L"S2PassY is written to the folder" : L"S2PassY is written");
                Assert::IsFalse(fs::exists(where / "script.906"), toFolder ? L"the pass-1 file of S2PassX is dropped" : L"the pass-1 output of S2PassX is withdrawn");
                // The pass-1 .sco of S2PassX, which the commit does not write,
                // goes (there was none before the batch).
                Assert::IsFalse(fs::exists(session.Helper().GetScriptObjectFileName("S2PassX")), Wide("S2PassX.sco goes: " + Describe(*report)).c_str());
                Assert::IsTrue(report->objectFiles.has_value() && (report->removedObjectFiles.size() == 1) && report->restoredObjectFiles.empty(),
                    Wide(Describe(*report)).c_str());
            }
        }

        // An abort between two passes keeps the pass that finished: no new
        // pass starts, and the commit writes the finished pass.
        TEST_METHOD(Abort_BetweenPasses_KeepsTheFinishedPass)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2PassB", 906, PassBText),
                WriteScript(session, "S2PassA", 904, PassAText),
            };
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            TestCompileEvents events;
            events.abortAfterDone = 2;
            auto report = Compile(session, scripts, options, events);
            AssertOk(report);
            Assert::IsTrue(report->cancelled);
            Assert::AreEqual(0, events.passesStarted, L"no new pass starts after the abort");
            Assert::AreEqual((size_t)2, report->scripts.size(), L"the report has the pass that finished");
            Assert::IsTrue(_game.Has("script.904"), L"the finished pass is written");
            // That pass changed a .sco, and no pass comes after it, so a
            // script of the commit can use an old one.
            Assert::IsTrue(report->passLimit, L"the abort reports the pass limit");
        }

        // An abort that comes while OnPassStart runs (Ctrl+C of scic, from
        // another thread) keeps the pass that finished, as an abort between
        // the passes does.
        TEST_METHOD(Abort_InOnPassStart_KeepsTheFinishedPass)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2PassB", 906, PassBText),
                WriteScript(session, "S2PassA", 904, PassAText),
            };
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            TestCompileEvents events;
            events.abortAtPassStart = true;
            auto report = Compile(session, scripts, options, events);
            AssertOk(report);
            std::string facts = Describe(*report);
            Assert::AreEqual(1, events.passesStarted, Wide("setup: pass 2 starts: " + facts).c_str());
            Assert::IsTrue(report->cancelled && (report->passes == 1), Wide(facts).c_str());
            Assert::AreEqual((size_t)2, report->scripts.size(), Wide("the report has the pass that finished: " + facts).c_str());
            Assert::IsTrue(report->commit.has_value(), Wide(facts).c_str());
            Assert::IsTrue(_game.Has("script.904"), Wide("the finished pass is written: " + facts).c_str());
            Assert::IsTrue(report->passLimit, Wide(facts).c_str());
        }

        // Options that would write into the game, or fail each script after
        // its .sco file is written, refuse the start.
        TEST_METHOD(Start_RefusesBadOutputFolderOptions)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            CompileOptions raw = ToPatchFiles();
            raw.write.raw = true;
            auto rawStart = CompileBatch::Start(session, scripts, raw);
            Assert::IsTrue(!rawStart && (rawStart.error().code == sci::ErrorCode::Usage), L"raw files need an output folder");
            CompileOptions missing = ToPatchFiles();
            missing.write.outDir = _game.Path("S2NoSuchFolder");
            auto missingStart = CompileBatch::Start(session, scripts, missing);
            Assert::IsTrue(!missingStart && (missingStart.error().code == sci::ErrorCode::NotFound), L"an output folder that does not exist");
            CompileOptions game = ToPatchFiles();
            game.write.outDir = _game.Folder();
            auto gameStart = CompileBatch::Start(session, scripts, game);
            Assert::IsTrue(!gameStart && (gameStart.error().code == sci::ErrorCode::Usage), L"the game folder is not an output folder");
        }

        // A script with no number (a document opened from a file, or the
        // GUI's scan of src) gets the number that its source declares, so the
        // start sees its patch file, and the report has the number.
        TEST_METHOD(Start_AScriptWithNoNumber_GetsTheDeclaredNumber)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::string path = session.Helper().GetScriptFileName("S2Good904");
            WriteFileText(path, GoodText(904));
            ScriptId noNumber(path.c_str());
            Assert::IsTrue(noNumber.GetResourceNumber() == InvalidResourceNumber, L"setup: no number");
            WriteFileBytes(_game.Path("script.904"), ScriptPatch);
            auto started = CompileBatch::Start(session, { noNumber }, CompileOptions());
            Assert::IsTrue(!started && (started.error().code == sci::ErrorCode::WriteRefused), L"the start sees script.904");

            auto report = Compile(session, { noNumber }, ToPatchFiles());
            AssertSucceeded(report);
            Assert::AreEqual(904, (int)report->scripts[0].number);
        }

        // Replace does not move a file over the backup of an earlier batch in
        // the same second: each batch gets a new folder.
        TEST_METHOD(Replace_KeepsTheBackupOfAnEarlierBatch)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            WriteFileBytes(_game.Path("script.904"), { 0x80 | (uint8_t)ResourceType::Script, 0, 9, 9 });
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
                fs::path folder = fs::path(_game.Folder()) / "replaced-patches" / name;
                fs::create_directories(folder);
                WriteFileText((folder / "script.904").string(), "an earlier backup");
                backups.push_back(folder / "script.904");
            }
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            auto report = Compile(session, scripts, options);
            AssertSucceeded(report);
            Assert::AreEqual((size_t)1, report->movedPatches.size());
            for (const fs::path &backup : backups)
            {
                Assert::AreEqual(std::string("an earlier backup"), ReadFileText(backup.string()), Wide(backup.string()).c_str());
            }
        }

        // The counts that a caller reads. A failed commit is not a success,
        // and a warning is counted.
        TEST_METHOD(Report_SucceededAndTheWarningCount)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            // The script's number is 905, but its source declares 904: a
            // warning.
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 905, GoodText(904)) };
            auto report = Compile(session, scripts, ToPatchFiles());
            AssertSucceeded(report);
            Assert::IsTrue(report->WarningCount() >= 1, L"the warning is counted");

            CompileReport failed = *report;
            failed.commit = sci::Fail(sci::ErrorCode::Io, "a failed write");
            Assert::IsFalse(failed.Succeeded(), L"a failed commit is not a success");
            CompileReport notMoved = *report;
            notMoved.moves = sci::Fail(sci::ErrorCode::Io, "a patch file that did not move");
            Assert::IsFalse(notMoved.Succeeded(), L"a patch file that still hides the write is not a success");
        }

        // The lines that the GUI shows after a batch, and the scripts of a
        // compile-all.
        TEST_METHOD(Gui_ReportLinesAndTheScriptsOfACompileAll)
        {
            NoAppState noAppState;
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

            // A table failure that refuses the commit gives one error line.
            CompileReport refused;
            refused.tables = sci::Fail(sci::ErrorCode::Io, "vocab.996 is read-only");
            sci::Error refusal = refused.tables.error();
            refusal.context.push_back("no compiled resource was written, because the class and selector tables could not be saved");
            refused.commit = sci::Fail(refusal);
            CompileLog refusedLog;
            ReportCompileBatch(refused, refusedLog, "Write problem: ");
            Assert::AreEqual((size_t)1, refusedLog.Results().size(), L"one line for a table failure");
            Assert::IsTrue(refusedLog.Results()[0].IsError() && (refusedLog.Results()[0].GetMessage().find("so no compiled resource was written") != std::string::npos),
                Wide(refusedLog.Results()[0].GetMessage()).c_str());

            // The .sco files: "Put back" for a file with its old bytes,
            // "Removed" for a new file that went, and an error line for a
            // file that could not go back.
            CompileReport objectFiles;
            objectFiles.restoredObjectFiles = { "src\\old.sco" };
            objectFiles.removedObjectFiles = { "src\\new.sco" };
            objectFiles.objectFiles = sci::Fail(sci::ErrorCode::Io, "src\\held.sco");
            CompileLog objectFilesLog;
            ReportCompileBatch(objectFiles, objectFilesLog, "Write problem: ");
            std::vector<CompileResult> &objectFileLines = objectFilesLog.Results();
            Assert::AreEqual((size_t)3, objectFileLines.size());
            Assert::IsTrue(!objectFileLines[0].IsError() && (objectFileLines[0].GetMessage().find("Put back src\\old.sco") == 0), Wide(objectFileLines[0].GetMessage()).c_str());
            Assert::IsTrue(!objectFileLines[1].IsError() && (objectFileLines[1].GetMessage().find("Removed src\\new.sco") == 0), Wide(objectFileLines[1].GetMessage()).c_str());
            Assert::IsTrue(objectFileLines[2].IsError() && (objectFileLines[2].GetMessage().find("src\\held.sco") != std::string::npos), Wide(objectFileLines[2].GetMessage()).c_str());

            Assert::IsFalse(StartFailureLine(sci::Fail(sci::ErrorCode::Cancelled, "x").value()).IsError(), L"Cancel at the start is not an error");
            Assert::IsTrue(StartFailureLine(sci::Fail(sci::ErrorCode::NotFound, "x").value()).IsError());

            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> all = ScriptsToCompile(session.ResourceMap(), std::unordered_set<std::string>());
            Assert::IsTrue(all.size() > 20, L"every script of game.ini");
            std::vector<ScriptId> door = ScriptsToCompile(session.ResourceMap(), { "door" });
            Assert::AreEqual((size_t)1, door.size());
            Assert::AreEqual(974, (int)door[0].GetResourceNumber());
        }

        // The write shares the file for read and write, as an ofstream does,
        // so a program that has the file open with read and write sharing
        // does not stop it.
        TEST_METHOD(FileWrite_SharesTheFile)
        {
            fs::path folder = fs::temp_directory_path() / "S2FileWriteShare";
            fs::create_directories(folder);
            std::string path = (folder / "shared.sco").string();
            AssertOk(WriteBytesToFile(path, std::vector<uint8_t>{ 1, 2, 3 }));
            HANDLE other = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(other != INVALID_HANDLE_VALUE, L"setup: another handle");
            sci::Status written = WriteBytesToFile(path, std::vector<uint8_t>{ 4, 5, 6, 7 });
            CloseHandle(other);
            RemoveFolder(folder.string());
            AssertOk(written);
        }

        // In the raw text of a syntax error, the hint is a sentence of its
        // own ('The text there is "5".'), not ': "5"' after the error text.
        TEST_METHOD(SyntaxError_TheRawTextOfAHint)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            const char *text =
                "(script# 905)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2cHint 0)\n(procedure (s2cHint)\n    (= 5 3)\n)\n";
            auto report = Compile(session, { WriteScript(session, "S2cHint", 905, text) }, ToPatchFiles());
            AssertOk(report);
            const CompileResult *syntaxError = FindDiagnostic(report->scripts[0], DiagnosticKind::Error);
            Assert::IsNotNull(syntaxError, Wide(Describe(*report)).c_str());
            const std::string &raw = syntaxError->GetRawMessage();
            Assert::IsTrue(raw.find(".:") == std::string::npos, Wide(raw).c_str());
            Assert::IsTrue(raw.find("\"5\"") != std::string::npos, Wide(raw + " / " + syntaxError->GetMessage()).c_str());
        }

        // Every diagnostic line is 1-based, and a diagnostic has its raw
        // message, with no "Error: (file) ... Line: N, col: M" around it, for
        // the command line.
        TEST_METHOD(Diagnostics_OneBasedLinesAndTheRawMessage)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
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
            auto report = Compile(session, { WriteScript(session, "S2cLines", 905, text) }, ToPatchFiles());
            AssertOk(report);
            const CompileResult *elseMessage = FindDiagnostic(report->scripts[0], DiagnosticKind::Any, "else clause must be the last");
            const CompileResult *undeclared = FindDiagnostic(report->scripts[0], DiagnosticKind::Error, "s2cUndeclared");
            // One of each: a second one would hide behind the first.
            for (const char *text : { "else clause must be the last", "s2cUndeclared" })
            {
                Assert::AreEqual(1, (int)std::count_if(report->scripts[0].diagnostics.begin(), report->scripts[0].diagnostics.end(),
                    [&](const CompileResult &result) { return result.GetMessage().find(text) != std::string::npos; }), Wide(Describe(*report)).c_str());
            }
            Assert::IsNotNull(elseMessage, Wide(Describe(*report)).c_str());
            Assert::AreEqual(8, elseMessage->GetLineNumber(), L"the else clause is on line 8 (1-based)");
            // The GUI text of the parser's warning has the form of the
            // compiler's.
            Assert::IsTrue((elseMessage->GetMessage().find("Warning: (S2cLines.sc) The else clause") == 0) && (elseMessage->GetMessage().find("  Line: 8, col: ") != std::string::npos),
                Wide(elseMessage->GetMessage()).c_str());
            Assert::IsTrue(elseMessage->GetRawMessage().find("The else clause") == 0, Wide(elseMessage->GetRawMessage()).c_str());
            Assert::IsNotNull(undeclared, Wide(Describe(*report)).c_str());
            Assert::AreEqual(11, undeclared->GetLineNumber());
            const std::string &raw = undeclared->GetRawMessage();
            Assert::IsTrue((raw.find("s2cUndeclared") != std::string::npos) && (raw.find("Line:") == std::string::npos) && (raw.find("Error:") == std::string::npos),
                Wide(raw).c_str());
            Assert::IsTrue(undeclared->GetMessage().find("Line: 11") != std::string::npos, L"the GUI text keeps its form");
        }

        // The text of a syntax error has the 1-based line too, and the
        // error has a raw message.
        TEST_METHOD(SyntaxError_OneBasedLineInTheTextAndTheRawMessage)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            const char *text =
                "(script# 905)\n"                   // 1
                "(include sci.sh)\n"                // 2
                "(include game.sh)\n"               // 3
                "(use main)\n"                      // 4
                "(public s2cSyntax 0)\n"            // 5
                "(procedure (s2cSyntax)\n"          // 6
                "    (= )\n"                        // 7
                ")\n";
            auto report = Compile(session, { WriteScript(session, "S2cSyntax", 905, text) }, ToPatchFiles());
            AssertOk(report);
            const CompileResult *syntaxError = FindDiagnostic(report->scripts[0], DiagnosticKind::Error);
            Assert::IsNotNull(syntaxError, Wide(Describe(*report)).c_str());
            Assert::AreEqual(7, syntaxError->GetLineNumber(), Wide(syntaxError->GetMessage()).c_str());
            Assert::IsTrue(syntaxError->GetMessage().find("(7, ") != std::string::npos, Wide(syntaxError->GetMessage()).c_str());
            const std::string &raw = syntaxError->GetRawMessage();
            Assert::IsTrue(!raw.empty() && (raw.find("Error:") == std::string::npos) && (raw.find("(7, ") == std::string::npos), Wide(raw).c_str());
        }

        // Each outcome has the sizes of its compiled script (the GUI shows
        // them after a compile).
        TEST_METHOD(Outcome_HasTheStats)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            auto report = Compile(session, { WriteScript(session, "S2Good904", 904, GoodText(904)) }, ToPatchFiles());
            AssertSucceeded(report);
            Assert::IsTrue(report->scripts[0].stats.Code > 0, L"the compiled script has code");
        }

        // A script whose source file is missing gives NotFound, and an error
        // in its diagnostics.
        TEST_METHOD(MissingSourceFile_IsNotFound)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            ScriptId missing(session.Helper().GetScriptFileName("S2Missing").c_str());
            missing.SetResourceNumber(908);
            auto report = Compile(session, { missing }, CompileOptions());
            AssertOk(report);
            Assert::AreEqual((size_t)1, report->scripts.size());
            Assert::IsFalse(report->scripts[0].status.has_value());
            Assert::IsTrue(report->scripts[0].status.error().code == sci::ErrorCode::NotFound, Wide(Describe(*report)).c_str());
            Assert::IsTrue(Describe(*report).find("Could not read S2Missing.sc") != std::string::npos, Wide(Describe(*report)).c_str());
        }

        // A debug file that cannot be written is a warning (the game does not
        // need it), and the batch stays whole: script 906 compiles against
        // the new class of script 907, and both scripts are written.
        TEST_METHOD(DebugFileWriteError_IsAWarning_TheBatchStaysWhole)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
            const char *user =
                "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2DepWidget)\n"
                "(public s2DepUser 0)\n(procedure (s2DepUser)\n    (S2DepWidget new:)\n)\n";
            std::vector<ScriptId> scripts = { WriteScript(session, "S2DepWidget", 907, WidgetText("Dep")), WriteScript(session, "S2DepUser", 906, user) };
            std::string debugFile = session.Helper().GetScriptDebugFileName(907);
            fs::create_directories(fs::path(debugFile).parent_path());
            WriteReadOnlyFile(debugFile, "not the new debug file");
            auto report = Compile(session, scripts, ToPatchFiles());
            AssertSucceeded(report);
            Assert::IsTrue(_game.Has("script.907") && _game.Has("script.906"), L"both scripts are written");
            bool warned = std::any_of(report->scripts.begin(), report->scripts.end(),
                [](const ScriptOutcome &outcome) { return FindDiagnostic(outcome, DiagnosticKind::Warning, "907.scd") != nullptr; });
            Assert::IsTrue(warned, L"the debug file is a warning");
        }

        // With an output folder, the files wait for the commit. A script that
        // fails writes no file, and a commit that fails writes none.
        TEST_METHOD(OutputFolder_TheFilesWaitForTheCommit)
        {
            NoAppState noAppState;
            {
                // A script whose .sco cannot be written.
                GameSession &session = _game.OpenCopy(TemplateSci0);
                fs::path out = _game.Path("S2Out");
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2OutWidget", 907, WidgetText("Out")), WriteScript(session, "S2Good904", 904, GoodText(904)) };
                WriteReadOnlyFile(session.Helper().GetScriptObjectFileName("S2OutWidget"), "not the new object file");
                CompileOptions options = ToPatchFiles();
                options.write.outDir = out.string();
                auto report = Compile(session, scripts, options);
                AssertOk(report);
                Assert::IsTrue(report->commit.has_value(), Wide(Describe(*report)).c_str());
                Assert::IsFalse(report->scripts[0].status.has_value(), L"setup: 907 fails");
                Assert::IsFalse(fs::exists(out / "script.907"), L"a script that fails writes no file");
                Assert::IsTrue(fs::exists(out / "script.904"), L"the script that compiled is written at the commit");
                Assert::IsFalse(_game.Has("script.904") || _game.Has("script.907"), L"the game does not change");
            }
            // A file of the folder that cannot be written: a table file, or
            // the file of the script. Every file is checked before the first
            // write, so no file of the batch is written.
            for (const char *readOnly : { "vocab.996", "script.907" })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                fs::path out = _game.Path("S2Out");
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2OutWidget", 907, WidgetText("Out")) };
                WriteReadOnlyFile((out / readOnly).string(), "an old file");
                CompileOptions options = ToPatchFiles();
                options.write.outDir = out.string();
                auto report = Compile(session, scripts, options);
                AssertOk(report);
                Assert::IsTrue(report->scripts[0].status.has_value(), Wide(Describe(*report)).c_str());
                Assert::IsFalse(report->commit.has_value(), Wide(std::string(readOnly) + " cannot be written").c_str());
                for (const char *name : { "script.907", "vocab.996", "vocab.997" })
                {
                    if (_stricmp(name, readOnly) != 0)
                    {
                        Assert::IsFalse(fs::exists(out / name), Wide(std::string(name) + " is written, with " + readOnly + " read-only").c_str());
                    }
                }
            }
        }

        // passLimit says that the last pass that the options allow still
        // changed a .sco file (S2PassB needs a second pass).
        TEST_METHOD(Passes_TheLimitIsReported)
        {
            NoAppState noAppState;
            for (int passes : { 1, 3 })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2PassB", 906, PassBText), WriteScript(session, "S2PassA", 904, PassAText) };
                CompileOptions options = ToPatchFiles();
                options.passes = passes;
                auto report = Compile(session, scripts, options);
                AssertOk(report);
                Assert::AreEqual(passes == 1, report->passLimit, Wide(Describe(*report)).c_str());
            }
        }

        // A dry run checks the patch files that would hide a package write,
        // as a real run does, and moves none. So a dry run is refused where
        // the real run is refused.
        TEST_METHOD(DryRun_ChecksTheShadowsAndMovesNothing)
        {
            NoAppState noAppState;
            for (ShadowPolicy policy : { ShadowPolicy::Refuse, ShadowPolicy::Replace })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
                WriteFileBytes(_game.Path("script.904"), ScriptPatch);
                std::vector<uint8_t> map = ReadFileBytes(_game.Path("resource.map"));
                CompileOptions options;
                MakeDry(options);
                options.shadows = policy;
                auto report = Compile(session, scripts, options);
                if (policy == ShadowPolicy::Refuse)
                {
                    Assert::IsFalse(report.has_value(), L"refused, as the real run is");
                    Assert::IsTrue(report.error().code == sci::ErrorCode::WriteRefused, Wide(report.error().ToString()).c_str());
                }
                else
                {
                    AssertSucceeded(report);
                    Assert::IsTrue(report->movedPatches.empty(), L"a dry run moves nothing");
                    bool named = std::any_of(report->warnings.begin(), report->warnings.end(),
                        [](const std::string &warning) { return warning.find("script.904") != std::string::npos; });
                    Assert::IsTrue(named, L"the report names the file that a real run would move");
                }
                Assert::IsTrue(_game.Has("script.904"), L"the patch file stays");
                Assert::IsTrue(map == ReadFileBytes(_game.Path("resource.map")), L"nothing is written");
            }
        }

        // Every patch file that cannot move is in the error of the moves.
        TEST_METHOD(Replace_EveryFileThatCannotMove_IsInTheError)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
            WriteFileBytes(_game.Path("script.904"), ScriptPatch);
            WriteFileBytes(_game.Path("text.904"), TextPatch);
            // Held open with read sharing only: the scan can read them, but neither can move.
            HANDLE script = CreateFileA(_game.Path("script.904").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE text = CreateFileA(_game.Path("text.904").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue((script != INVALID_HANDLE_VALUE) && (text != INVALID_HANDLE_VALUE), L"setup: the patch files are held");
            CompileOptions options;
            options.shadows = ShadowPolicy::Replace;
            auto report = Compile(session, scripts, options);
            CloseHandle(script);
            CloseHandle(text);
            AssertOk(report);
            Assert::IsTrue(report->commit.has_value(), Wide(Describe(*report)).c_str());
            Assert::IsFalse(report->moves.has_value(), L"the moves failed");
            std::string error = report->moves.error().ToString();
            Assert::IsTrue((error.find("script.904") != std::string::npos) && (error.find("text.904") != std::string::npos), Wide(error).c_str());
        }

        // A new pass withdraws the writes of the pass before, but not its
        // .sco files. In pass 2, S2UseY fails against the new .sco of S2UseX,
        // and S2UseX compiles against the pass-1 .sco of S2UseY. The new
        // S2UseX calls export 1 of S2UseY, so the commit must not write it
        // without S2UseY: the commit writes nothing.
        TEST_METHOD(Passes_AScriptThatUsesTheNewObjectFileOfAFailedScript_WritesNothing)
        {
            NoAppState noAppState;
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
            // declares: the refusal names S2UseX with the declared number.
            for (int variant : { 0, 1, 2 })
            {
                bool toFolder = (variant == 1);
                GameSession &session = _game.OpenCopy(TemplateSci0);
                ScriptId y = WriteScript(session, "S2UseY", 904, oldY);
                ScriptId x = WriteScript(session, "S2UseX", 906, oldX);
                AssertSucceeded(Compile(session, { y, x }, ToPatchFiles()), "setup: the old scripts compile");
                std::vector<uint8_t> oldYScript = ReadFileBytes(_game.Path("script.904"));
                std::vector<uint8_t> oldXScript = ReadFileBytes(_game.Path("script.906"));
                std::string ySco = session.Helper().GetScriptObjectFileName("S2UseY");
                std::string xSco = session.Helper().GetScriptObjectFileName("S2UseX");
                std::vector<uint8_t> oldYSco = ReadFileBytes(ySco);
                std::vector<uint8_t> oldXSco = ReadFileBytes(xSco);
                WriteScript(session, "S2UseY", 904, newY);
                WriteScript(session, "S2UseX", 906, newX);
                ScriptId batchX(x.GetFullPath().c_str());
                batchX.SetResourceNumber((variant == 2) ? 907 : 906);
                CompileOptions options = ToPatchFiles();
                options.passes = 5;
                fs::path out = _game.Path("S2Out");
                if (toFolder)
                {
                    fs::create_directories(out);
                    options.write.outDir = out.string();
                }
                auto report = Compile(session, { y, batchX }, options);
                AssertOk(report);
                std::string facts = Describe(*report);
                Assert::AreEqual(2, report->passes, Wide(facts).c_str());
                Assert::IsFalse(report->scripts[0].status.has_value(), Wide("setup: S2UseY fails in pass 2: " + facts).c_str());
                Assert::IsTrue(report->scripts[1].status.has_value(), Wide("setup: S2UseX compiles in pass 2: " + facts).c_str());
                Assert::IsFalse(report->commit.has_value(), Wide("the commit writes nothing: " + facts).c_str());
                Assert::IsTrue(report->commit.error().code == sci::ErrorCode::WriteRefused, Wide(facts).c_str());
                std::string refusal = report->commit.error().ToString();
                Assert::IsTrue(refusal.find("S2UseX (906) uses S2UseY (904), which failed") != std::string::npos, Wide(facts).c_str());
                // The refusal comes before the .sco files go back: the report
                // names them.
                Assert::IsTrue(refusal.find(" are back") == std::string::npos, Wide(refusal).c_str());
                Assert::IsTrue(refusal.find("Correct the scripts that failed") != std::string::npos, Wide(refusal).c_str());
                Assert::AreEqual((size_t)2, report->restoredObjectFiles.size(), Wide(facts).c_str());
                Assert::IsTrue(oldYScript == ReadFileBytes(_game.Path("script.904")), L"S2UseY in the game does not change");
                Assert::IsTrue(oldXScript == ReadFileBytes(_game.Path("script.906")), L"S2UseX in the game does not change");
                if (toFolder)
                {
                    Assert::IsTrue(fs::is_empty(out), L"no file is written to the folder");
                }
                // The .sco files are back as they were, so S2UseX alone does
                // not compile against the old S2UseY. If the new .sco files
                // stayed, that compile would write the new S2UseX with the old
                // S2UseY.
                Assert::IsTrue(report->objectFiles.has_value(), Wide(facts).c_str());
                Assert::IsTrue(oldYSco == ReadFileBytes(ySco), L"S2UseY.sco is back");
                Assert::IsTrue(oldXSco == ReadFileBytes(xSco), L"S2UseX.sco is back");
                if (variant == 0)
                {
                    auto alone = Compile(session, { x }, ToPatchFiles());
                    AssertOk(alone);
                    Assert::IsFalse(alone->scripts[0].status.has_value(), Wide("S2UseX alone fails: " + Describe(*alone)).c_str());
                    Assert::IsTrue(oldXScript == ReadFileBytes(_game.Path("script.906")), L"S2UseX alone is not written");
                }
            }
        }

        // An abort in pass 2 commits the pass-2 scripts that ran, and the
        // pass-1 writes of the others are withdrawn. S2AbortX compiles in
        // pass 2 against the new .sco of S2AbortY, which does not run in pass
        // 2. S2AbortX calls export 1 of S2AbortY, so the commit must not write
        // it without S2AbortY: the commit writes nothing.
        TEST_METHOD(Passes_AnAbortInALaterPass_WritesNoScriptThatUsesANewObjectFile)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            const char *oldY = "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2AbortYOld 0)\n(procedure (s2AbortYOld)\n    (return 1)\n)\n";
            const char *newY =
                "(script# 904)\n(include sci.sh)\n(include game.sh)\n(use main)\n(public s2AbortYNew 0 s2AbortYMore 1)\n"
                "(procedure (s2AbortYNew)\n    (return 3)\n)\n(procedure (s2AbortYMore)\n    (return 7)\n)\n";
            const char *newX =
                "(script# 906)\n(include sci.sh)\n(include game.sh)\n(use main)\n(use S2AbortY)\n(public s2AbortX 0)\n"
                "(procedure (s2AbortX)\n    (return (s2AbortYMore))\n)\n";
            ScriptId y = WriteScript(session, "S2AbortY", 904, oldY);
            AssertSucceeded(Compile(session, { y }, ToPatchFiles()), "setup: the old S2AbortY compiles");
            std::vector<uint8_t> oldYScript = ReadFileBytes(_game.Path("script.904"));
            WriteScript(session, "S2AbortY", 904, newY);
            ScriptId x = WriteScript(session, "S2AbortX", 906, newX);
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            TestCompileEvents events;
            events.abortFromPass = 2;
            auto report = Compile(session, { x, y }, options, events);
            AssertOk(report);
            std::string facts = Describe(*report);
            Assert::IsTrue(report->cancelled && (report->passes == 2), Wide("setup: the abort comes in pass 2: " + facts).c_str());
            Assert::AreEqual((size_t)1, report->scripts.size(), Wide(facts).c_str());
            Assert::IsTrue(report->scripts[0].status.has_value(), Wide("setup: S2AbortX compiles in pass 2: " + facts).c_str());
            Assert::IsFalse(report->commit.has_value(), Wide("the commit writes nothing: " + facts).c_str());
            std::string refusal = report->commit.error().ToString();
            Assert::IsTrue(refusal.find("S2AbortX (906) uses S2AbortY (904), which did not run") != std::string::npos, Wide(facts).c_str());
            // No script failed: the advice names none.
            Assert::IsTrue(refusal.find("Correct the scripts that failed") == std::string::npos, Wide(refusal).c_str());
            Assert::IsTrue(refusal.find("Compile again.") != std::string::npos, Wide(refusal).c_str());
            Assert::IsFalse(_game.Has("script.906"), L"S2AbortX is not written");
            Assert::IsTrue(oldYScript == ReadFileBytes(_game.Path("script.904")), L"S2AbortY in the game does not change");
        }

        // A dry run checks the auto text before the commit, as a real run
        // does. So a dry run is refused where the real run is refused.
        TEST_METHOD(DryRun_ChecksTheAutoTextBeforeTheCommit)
        {
            NoAppState noAppState;
            for (bool dryRun : { false, true })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Text", 904, TextScript) };
                WriteFileBytes(_game.Path("text.904"), TextPatch);
                std::vector<uint8_t> map = ReadFileBytes(_game.Path("resource.map"));
                CompileOptions options;
                MakeDry(options, dryRun);
                auto report = Compile(session, scripts, options);
                AssertOk(report);
                std::string facts = std::string(dryRun ? "dry run: " : "real run: ") + Describe(*report);
                Assert::IsTrue(report->scripts[0].status.has_value(), Wide(facts).c_str());
                Assert::IsFalse(report->commit.has_value(), Wide(facts).c_str());
                Assert::IsTrue(report->commit.error().code == sci::ErrorCode::WriteRefused, Wide(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("text.904") != std::string::npos, Wide(facts).c_str());
                Assert::IsTrue(map == ReadFileBytes(_game.Path("resource.map")), L"nothing is written");
            }
        }

        // A dry run does not ask what to do with the patch files that would
        // hide a package write (it cannot move them). Refuse stops it, as a
        // real run with no askShadows.
        TEST_METHOD(DryRun_AsksNothing)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            WriteFileBytes(_game.Path("script.904"), ScriptPatch);
            int asked = 0;
            CompileOptions options;
            MakeDry(options);
            options.askShadows = [&asked](const std::vector<std::string> &files)
            {
                asked++;
                return ShadowPolicy::Replace;
            };
            auto report = Compile(session, scripts, options);
            Assert::AreEqual(0, asked, L"a dry run asks nothing");
            Assert::IsFalse(report.has_value(), L"Refuse stops the dry run");
            Assert::IsTrue(report.error().code == sci::ErrorCode::WriteRefused, Wide(report.error().ToString()).c_str());
            Assert::IsTrue(_game.Has("script.904"), L"the patch file stays");
        }

        // A hidden or system file, a folder with the file's name, or a file
        // that another program holds, in the output folder fails the commit
        // before the first write, so no table file is written.
        TEST_METHOD(OutputFolder_AFileThatCannotBeReplaced_FailsBeforeTheFirstWrite)
        {
            NoAppState noAppState;
            for (std::string kind : { "hidden", "system", "folder", "held" })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                fs::path out = _game.Path("S2Out");
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2OutWidget", 907, WidgetText("Out")) };
                // The file of the script: the table files come before it.
                std::string target = (out / "script.907").string();
                HANDLE held = INVALID_HANDLE_VALUE;
                if (kind == "folder")
                {
                    fs::create_directories(target);
                }
                else
                {
                    WriteFileText(target, "an old file");
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
                auto report = Compile(session, scripts, options);
                if (held != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(held);
                }
                AssertOk(report);
                std::string facts = kind + ": " + Describe(*report);
                Assert::IsTrue(report->scripts[0].status.has_value(), Wide(facts).c_str());
                Assert::IsFalse(report->commit.has_value(), Wide(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("script.907") != std::string::npos, Wide(facts).c_str());
                Assert::IsFalse(fs::exists(out / "vocab.996") || fs::exists(out / "vocab.997"), Wide("a table file is written: " + facts).c_str());
            }
        }

        // With raw files, the check before the first write shares the file
        // as the raw write does (read and write), so a program that has the
        // file open with that sharing does not stop the commit.
        TEST_METHOD(OutputFolderRaw_AFileThatAnotherProgramShares_IsWritten)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            fs::path out = _game.Path("S2Out");
            fs::create_directories(out);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            std::string target = (out / "script.904.bin").string();
            WriteFileText(target, "an old file");
            HANDLE reader = CreateFileA(target.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Assert::IsTrue(reader != INVALID_HANDLE_VALUE, L"setup: the file is open");
            CompileOptions options = ToPatchFiles();
            options.write.outDir = out.string();
            options.write.raw = true;
            auto report = Compile(session, scripts, options);
            CloseHandle(reader);
            AssertSucceeded(report);
            Assert::IsTrue(ReadFileText(target) != "an old file", L"the file has the new data");
        }

        // A script whose .sco cannot be written leaves no new debug file (the
        // .sco is written before the debug file), and the failed .sco is not
        // a change, so the batch runs one pass.
        TEST_METHOD(ObjectFileWriteError_NoDebugFile_OnePass)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            session.Helper().SetIniString(GameSection, "GenerateDebugInfo", "true");
            ScriptId script = WriteScript(session, "S2ScoWidget", 907, WidgetText("Sco"));
            WriteReadOnlyFile(session.Helper().GetScriptObjectFileName("S2ScoWidget"), "not the new object file");
            std::string debugFile = session.Helper().GetScriptDebugFileName(907);
            // The folder is there, so a debug file after a failed .sco would be written.
            fs::create_directories(fs::path(debugFile).parent_path());
            std::error_code ec;
            fs::remove(debugFile, ec);
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            auto report = Compile(session, { script }, options);
            AssertOk(report);
            std::string facts = Describe(*report);
            Assert::IsFalse(report->scripts[0].status.has_value(), Wide("setup: the script fails: " + facts).c_str());
            Assert::IsFalse(fs::exists(debugFile), Wide("no new debug file: " + facts).c_str());
            Assert::AreEqual(1, report->passes, Wide(facts).c_str());
            Assert::IsFalse(report->passLimit, Wide(facts).c_str());
            Assert::IsFalse(_game.Has("script.907"), L"the script is not written");
        }

        // With an output folder, a dry run checks the files as the write
        // would, so a read-only file fails the dry run as it fails the real
        // run.
        TEST_METHOD(DryRun_OutputFolder_ChecksTheFiles)
        {
            NoAppState noAppState;
            for (bool dryRun : { false, true })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                fs::path out = _game.Path("S2Out");
                fs::create_directories(out);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
                WriteReadOnlyFile((out / "script.904").string(), "an old file");
                CompileOptions options = ToPatchFiles();
                options.write.outDir = out.string();
                MakeDry(options, dryRun);
                auto report = Compile(session, scripts, options);
                AssertOk(report);
                std::string facts = std::string(dryRun ? "dry run: " : "real run: ") + Describe(*report);
                Assert::IsTrue(report->scripts[0].status.has_value(), Wide(facts).c_str());
                Assert::IsFalse(report->commit.has_value(), Wide(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("script.904") != std::string::npos, Wide(facts).c_str());
            }
        }

        // A path of 260 characters or more fails the check before the first
        // write, so no table file is written.
        TEST_METHOD(OutputFolder_APathTooLong_FailsBeforeTheFirstWrite)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2LongWidget", 907, WidgetText("Long")) };
            // A folder of 245 characters: "\vocab.996.bin" ends at 259, and
            // "\script.907.bin" at 260.
            Assert::IsTrue(_game.Folder().size() < 200, L"setup: a short copy folder");
            fs::path out = _game.Folder() + "\\" + std::string(245 - _game.Folder().size() - 1, 'o');
            Assert::AreEqual((size_t)245, out.string().size());
            fs::create_directories(out);
            CompileOptions options = ToPatchFiles();
            options.write.outDir = out.string();
            options.write.raw = true;
            auto report = Compile(session, scripts, options);
            AssertOk(report);
            std::string facts = Describe(*report);
            Assert::IsTrue(report->scripts[0].status.has_value(), Wide(facts).c_str());
            Assert::IsFalse(report->commit.has_value(), Wide(facts).c_str());
            Assert::IsTrue(report->commit.error().ToString().find("script.907.bin") != std::string::npos, Wide(facts).c_str());
            Assert::IsFalse(fs::exists(out / "vocab.996.bin") || fs::exists(out / "vocab.997.bin"), Wide("a table file is written: " + facts).c_str());
        }

        // A dry run that writes .sco files puts them back: it commits
        // nothing, so the game's scripts stay as they were.
        TEST_METHOD(DryRun_PutsBackTheObjectFiles)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
            std::string sco = session.Helper().GetScriptObjectFileName("S2Good904");
            Assert::IsFalse(fs::exists(sco), L"setup: no .sco before the batch");
            CompileOptions options = ToPatchFiles();
            options.write.writeResources = false;
            auto report = Compile(session, scripts, options);
            AssertSucceeded(report);
            Assert::IsFalse(fs::exists(sco), L"the .sco of the dry run goes");
            Assert::AreEqual((size_t)1, report->removedObjectFiles.size());
            Assert::IsTrue(report->restoredObjectFiles.empty(), L"a file that was not there is not put back");
        }

        // A dry run into the game's patch files checks each patch file as
        // the write would, so a read-only patch file fails the dry run as it
        // fails the real run. The real run removes the debug file that it
        // wrote.
        TEST_METHOD(DryRun_PatchFiles_ChecksTheFiles)
        {
            NoAppState noAppState;
            for (bool dryRun : { false, true })
            {
                GameSession &session = _game.OpenCopy(TemplateSci0);
                // The compile writes debug\904.scd.
                Assert::IsTrue(WritePrivateProfileStringA("Game", "GenerateDebugInfo", "true", _game.Path("game.ini").c_str()) != 0);
                std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
                WriteFileBytes(_game.Path("script.904"), ScriptPatch);
                Assert::IsTrue(SetFileAttributesA(_game.Path("script.904").c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: a read-only patch file");
                CompileOptions options = ToPatchFiles();
                MakeDry(options, dryRun);
                auto report = Compile(session, scripts, options);
                SetFileAttributesA(_game.Path("script.904").c_str(), FILE_ATTRIBUTE_NORMAL);
                AssertOk(report);
                std::string facts = std::string(dryRun ? "dry run: " : "real run: ") + Describe(*report);
                Assert::IsTrue(report->scripts[0].status.has_value(), Wide(facts).c_str());
                Assert::IsFalse(report->commit.has_value(), Wide(facts).c_str());
                Assert::IsTrue(report->commit.error().ToString().find("script.904") != std::string::npos, Wide(facts).c_str());
                // The commit wrote nothing, so the debug file of the script
                // goes too.
                Assert::IsFalse(fs::exists(session.Helper().GetScriptDebugFilePath(904)), Wide("no debug file: " + facts).c_str());
            }
        }

        // A line break of another style than the first one of the file does
        // not end a line (the editor's rule): a warning at its line.
        TEST_METHOD(OtherLineBreak_IsAWarning)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            // The first break is CR LF; the others are LF alone.
            std::string text = std::string("(script# 904)\r\n") + (GoodText(904) + std::string(GoodText(904)).find('\n') + 1);
            auto report = Compile(session, { WriteScript(session, "S2Good904", 904, text) }, ToPatchFiles());
            AssertOk(report);
            std::string facts = Describe(*report);
            const CompileResult *warning = FindDiagnostic(report->scripts[0], DiagnosticKind::Warning, "line break of another style");
            Assert::IsNotNull(warning, Wide(facts).c_str());
            Assert::AreEqual(2, warning->GetLineNumber(), Wide(facts).c_str());
        }

        // A game.sh that does not parse gives its syntax errors to the first
        // script of the batch that includes it, and one line to the others.
        TEST_METHOD(BrokenHeader_ItsErrorsOnce)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::string header = _game.Src("game.sh");
            WriteFileText(header, ReadFileText(header) + "\r\n(define\r\n");
            std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)), WriteScript(session, "S2Good906", 906, GoodText(906)) };
            auto report = Compile(session, scripts, ToPatchFiles());
            AssertOk(report);
            std::string facts = Describe(*report);
            Assert::AreEqual((size_t)2, report->FailedCount(), Wide("both scripts fail: " + facts).c_str());
            std::vector<size_t> headerErrors;
            for (const ScriptOutcome &outcome : report->scripts)
            {
                headerErrors.push_back((size_t)std::count_if(outcome.diagnostics.begin(), outcome.diagnostics.end(),
                    [](const CompileResult &result) { return result.IsError() && (_stricmp(result.GetScript().GetFileNameOrig().c_str(), "game.sh") == 0); }));
            }
            Assert::IsTrue((headerErrors[0] > 0) && (headerErrors[1] == 0), Wide("the syntax errors of game.sh come once: " + facts).c_str());
            Assert::IsTrue(FindDiagnostic(report->scripts[1], DiagnosticKind::Error, "listed for the first script that includes it") != nullptr, Wide(facts).c_str());
        }

        // A dry run into the package checks the package files as the write
        // would, so a read-only volume or map fails the dry run as it fails
        // the real run. The real run removes its .bak files, except the map
        // that matches a volume that it replaced.
        TEST_METHOD(DryRun_Package_ChecksTheFiles)
        {
            NoAppState noAppState;
            for (const char *file : { "resource.001", "resource.map" })
            {
                for (bool dryRun : { false, true })
                {
                    GameSession &session = _game.OpenCopy(TemplateSci0);
                    std::vector<ScriptId> scripts = { WriteScript(session, "S2Good904", 904, GoodText(904)) };
                    std::string path = _game.Path(file);
                    Assert::IsTrue(SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY) != 0, L"setup: a read-only package file");
                    CompileOptions options;
                    options.write.saveTo = ResourceSaveLocation::Package;
                    MakeDry(options, dryRun);
                    auto report = Compile(session, scripts, options);
                    SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                    AssertOk(report);
                    std::string facts = std::string(file) + (dryRun ? ", dry run: " : ", real run: ") + Describe(*report);
                    Assert::IsTrue(report->scripts[0].status.has_value(), Wide(facts).c_str());
                    Assert::IsFalse(report->commit.has_value(), Wide(facts).c_str());
                    Assert::IsTrue(report->commit.error().ToString().find(file) != std::string::npos, Wide(facts).c_str());
                    // A write that fails before a volume moves leaves no .bak
                    // file. After the volume moved, the map that matches it
                    // stays as resource.map.bak, and the error names it.
                    bool mapFailed = (std::string(file) == "resource.map") && !dryRun;
                    Assert::IsFalse(_game.Has("resource.001.bak"), Wide(facts).c_str());
                    Assert::AreEqual(mapFailed, _game.Has("resource.map.bak"), Wide(facts).c_str());
                    Assert::IsTrue(!mapFailed || (report->commit.error().ToString().find("resource.map.bak") != std::string::npos), Wide(facts).c_str());
                    _game.CloseSessions();
                }
            }
        }

        // A dry run writes no .sco, so it runs one pass; a .sco that a run
        // would change sets passLimit. A .sco that a run could not write
        // fails the script (Io), as in a run.
        TEST_METHOD(DryRun_AnObjectFileThatWouldChange)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            std::vector<ScriptId> scripts = {
                WriteScript(session, "S2PassB", 906, PassBText),
                WriteScript(session, "S2PassA", 904, PassAText),
            };
            CompileOptions options = ToPatchFiles();
            options.passes = 5;
            MakeDry(options);
            auto report = Compile(session, scripts, options);
            AssertOk(report);
            std::string facts = Describe(*report);
            Assert::AreEqual(1, report->passes, Wide(facts).c_str());
            Assert::IsTrue(report->passLimit, Wide("a run would change S2PassA.sco: " + facts).c_str());
            Assert::IsFalse(fs::exists(session.Helper().GetScriptObjectFileName("S2PassA")), L"the dry run writes no .sco");

            WriteReadOnlyFile(session.Helper().GetScriptObjectFileName("S2PassA"), "not the new object file");
            for (bool dryRun : { false, true })
            {
                CompileOptions one = ToPatchFiles();
                MakeDry(one, dryRun);
                auto readOnly = Compile(session, { scripts[1] }, one);
                AssertOk(readOnly);
                std::string readOnlyFacts = std::string(dryRun ? "dry run: " : "real run: ") + Describe(*readOnly);
                Assert::IsFalse(readOnly->scripts[0].status.has_value(), Wide(readOnlyFacts).c_str());
                Assert::IsTrue(readOnly->scripts[0].status.error().code == sci::ErrorCode::Io, Wide(readOnlyFacts).c_str());
            }
        }

        // The length check measures the full path: a relative path joins the
        // current folder.
        TEST_METHOD(FileWrite_TheLengthCheckMeasuresTheFullPath)
        {
            NoAppState noAppState;
            GameSession &session = _game.OpenCopy(TemplateSci0);
            // A folder of 245 characters: "\script.907.bin" ends at 260, and
            // "\script.97.bin" at 259.
            Assert::IsTrue(_game.Folder().size() < 200, L"setup: a short copy folder");
            fs::path folder = _game.Folder() + "\\" + std::string(245 - _game.Folder().size() - 1, 'o');
            fs::create_directories(folder);
            fs::path saved = fs::current_path();
            fs::current_path(folder);
            sci::Status tooLong = CheckFileCanBeReplaced("script.907.bin", FILE_SHARE_READ | FILE_SHARE_WRITE);
            sci::Status fits = CheckFileCanBeReplaced("script.97.bin", FILE_SHARE_READ | FILE_SHARE_WRITE);
            size_t length = FullPathLength("script.907.bin");
            fs::current_path(saved);
            Assert::AreEqual((size_t)260, length);
            Assert::IsFalse(tooLong.has_value(), L"260 characters are too many");
            Assert::IsTrue(tooLong.error().ToString().find("260") != std::string::npos, Wide(tooLong.error().ToString()).c_str());
            AssertOk(fits, "259 characters fit");
        }
    };
}
