/***************************************************************************
    Copyright (c) 2026 Philip Fortier

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
***************************************************************************/
#include "stdafx.h"
#include "CppUnitTest.h"
#include <fstream>
#include "Helper.h"
#include "DecompileHelper.h"
#include "TestSupport.h"
#include "StructuralCompare.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "SCO.h"
#include "CompiledScript.h"
#include "GameFolderHelper.h"
#include "ScriptOMAll.h"
#include "DecompileScript.h"
#include "DecompilerConfig.h"
#include "ResourceContainer.h"
#include "ResourceEntity.h"
#include "Text.h"
#include "format.h"
#include "DecompileRun.h"
#include "AppSession.h"
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Each family fixture has the shape of a QfG4 function. The fixture uses
// hand-written asm to match Sierra's exact bytecode: SCI Companion's own
// compiler gives other code. Each test asserts a clean decompile (no
// fallback, no asm). See UnitTests\README.md.

namespace UnitTests
{
    static void LogWarnings(const std::string &label, const DecompileOutput &out)
    {
        std::string msg = fmt::format("{0}: fallbacks={1}", label, out.fallbacks);
        for (const std::string &w : out.warnings)
        {
            msg += "\n  " + w;
        }
        Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());
    }

// A test of a fixture whose decompiled text equals its expected file.
#define FIXTURE_TEST(name, fixture, number) \
        TEST_METHOD(name) \
        { \
            _gameFolder = SetUpGameSCI11(); \
            AssertDecompileMatchesExpected(fixture, number); \
        }

    TEST_CLASS(TestDecompile)
    {
    public:
        // A prior test that failed before cleanup would leak the global app
        // state. Catch that here.
        TEST_METHOD_INITIALIZE(Setup)
        {
            Assert::IsNull(appState, L"appState leaked from a prior test");
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // The harness works. A plain script round-trips with no fallback.
        TEST_METHOD(Harness_SmokeRoundTrip)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("D0_Plain", 901);
            Assert::AreEqual(0, out.fallbacks, L"plain fixture should not fall back");
            Assert::IsFalse(out.ContainsAsm(), L"plain fixture should have no asm");
        }

        // The value-and/or compiler fix: a logical and/or used for its value
        // compiles to Sierra-semantic bytecode (the last evaluated operand is
        // left in the accumulator) and round-trips. Fidelity to (and a b) comes
        // with the AST passes; here we only require a stable round trip with no
        // fallback.
        // A public procedure whose real name starts with "proc" but is not a
        // generated procN_i name. Before the fix, ResolvePublicProcedureCalls ->
        // _IsUndeterminedPublicProc -> stoi("Foo") threw std::invalid_argument,
        // which escaped to the batch's catch(...) and killed the run. Here it
        // escapes to the test (DecompileToText does not catch), failing it.
        TEST_METHOD(PublicProcNamedProc_NoThrow)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("PublicProcFoo");
            std::string error;
            Assert::IsTrue(CompileFixture(940, "PublicProcFoo", &error),
                std::wstring(error.begin(), error.end()).c_str());
            DecompileOutput out = DecompileToText(940);
            Assert::IsTrue(out.text.find("procFoo") != std::string::npos,
                L"the public procedure procFoo should decompile with its name intact");
        }

        // An export that points into the code of the function before it
        // (Sierra left such stale exports, for example QfG3 script 7) is
        // no procedure: the decompiler leaves it out, with a warning, and the
        // meaning check reads no function there.
        TEST_METHOD(StaleExport_IsLeftOut)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("X3_StaleExport");
            std::string error;
            Assert::IsTrue(CompileFixture(964, "X3_StaleExport", &error), Wide(error).c_str());
            const GameFolderHelper &helper = AppResourceMap().Helper();
            CompiledScript compiled(964, CompiledScriptFlags::RemoveBadExports);
            Assert::IsTrue(compiled.Load(helper, helper.Version, 964), L"setup: the script loads");
            std::vector<uint8_t> script = compiled.GetRawBytes();
            std::unique_ptr<ResourceBlob> heapBlob = helper.MostRecentResource(ResourceType::Heap, 964, ResourceEnumFlags::None);
            Assert::IsNotNull(heapBlob.get(), L"setup: the heap");
            sci::istream heapRead = heapBlob->GetReadStream();
            std::vector<uint8_t> heap(heapRead.GetDataSize());
            heapRead.read_data(heap.data(), (uint32_t)heap.size());
            // SCI1.1: the count of the exports at 6, the exports from 8.
            Assert::AreEqual((uint16_t)2, (uint16_t)(script[6] | (script[7] << 8)), L"setup: two exports");
            uint16_t first = (uint16_t)(script[8] | (script[9] << 8));
            uint16_t inside = first + 4;    // the second ldi of staleFirst
            script[10] = (uint8_t)(inside & 0xff);
            script[11] = (uint8_t)(inside >> 8);

            // The meaning check reads no function at the stale export.
            GlobalCompiledScriptLookups lookups;
            lookups.Load(helper);
            sci::Result<std::vector<meaning::Function>> functions = meaning::ReadScriptData(helper, lookups, AppResourceMap().GetVocab000(), 964, script, &heap);
            Assert::IsTrue(functions.has_value(), L"the patched script reads");
            for (const meaning::Function &function : *functions)
            {
                Assert::AreNotEqual(std::string("export 1"), function.key, L"no function at the stale export");
            }

            // The decompile leaves the export out.
            CompiledScript patched(964, CompiledScriptFlags::RemoveBadExports);
            sci::istream scriptStream(script.data(), (uint32_t)script.size());
            sci::istream heapStream(heap.data(), (uint32_t)heap.size());
            Assert::IsTrue(patched.Load(helper, helper.Version, 964, scriptStream, &heapStream), L"the patched script loads");
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(AppResourceMap(), lookups.GetSelectorTable());
            TestDecompilerResults results;
            std::unique_ptr<sci::Script> decompiled = DecompileScript(config.get(), lookups, AppResourceMap(), 964, patched, results);
            std::stringstream text;
            sci::SourceCodeWriter writer(text, decompiled.get());
            decompiled->OutputSourceCode(writer);
            std::string source = text.str();
            Assert::IsTrue(source.find("(procedure (staleFirst") != std::string::npos, Wide(source).c_str());
            Assert::IsTrue(source.find("staleSecond") == std::string::npos, Wide(source).c_str());
            bool warned = false;
            for (const std::string &warning : results.warnings)
            {
                warned = warned || (warning.find("Export 1 points into the code of another function") != std::string::npos);
            }
            Assert::IsTrue(warned, L"a warning names the export");
            // The function report has a line for it.
            bool reported = false;
            for (const DecompiledFunction &function : results.functions)
            {
                reported = reported || ((function.output == "stale") && (function.offset == inside));
            }
            Assert::IsTrue(reported, L"the function report has the stale export");
        }

        // An export that points outside the code of the script is no
        // procedure: past the end of the script (ICEMAN script 0: exports 6
        // to 29 are f9ff), or into the script before its code. The decompiler
        // leaves it out, with a warning, and the meaning check reads no
        // function there.
        TEST_METHOD(ExportOutsideTheCode_IsLeftOut)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("X3_StaleExport");
            std::string error;
            Assert::IsTrue(CompileFixture(964, "X3_StaleExport", &error), Wide(error).c_str());
            const GameFolderHelper &helper = AppResourceMap().Helper();
            CompiledScript compiled(964, CompiledScriptFlags::RemoveBadExports);
            Assert::IsTrue(compiled.Load(helper, helper.Version, 964), L"setup: the script loads");
            std::unique_ptr<ResourceBlob> heapBlob = helper.MostRecentResource(ResourceType::Heap, 964, ResourceEnumFlags::None);
            Assert::IsNotNull(heapBlob.get(), L"setup: the heap");
            sci::istream heapRead = heapBlob->GetReadStream();
            std::vector<uint8_t> heap(heapRead.GetDataSize());
            heapRead.read_data(heap.data(), (uint32_t)heap.size());
            GlobalCompiledScriptLookups lookups;
            lookups.Load(helper);
            for (uint16_t outside : { (uint16_t)0xf9ff, (uint16_t)4 })
            {
                std::string label = fmt::format("export 1 at {0:04x}: ", outside);
                std::vector<uint8_t> script = compiled.GetRawBytes();
                // SCI1.1: the count of the exports at 6, the exports from 8.
                Assert::AreEqual((uint16_t)2, (uint16_t)(script[6] | (script[7] << 8)), L"setup: two exports");
                script[10] = (uint8_t)(outside & 0xff);
                script[11] = (uint8_t)(outside >> 8);

                // The meaning check reads no function at the export.
                sci::Result<std::vector<meaning::Function>> functions = meaning::ReadScriptData(helper, lookups, AppResourceMap().GetVocab000(), 964, script, &heap);
                Assert::IsTrue(functions.has_value(), Wide(label + "the patched script reads").c_str());
                for (const meaning::Function &function : *functions)
                {
                    Assert::AreNotEqual(std::string("export 1"), function.key, Wide(label + "no function at the export").c_str());
                }

                // The decompile leaves the export out.
                CompiledScript patched(964, CompiledScriptFlags::RemoveBadExports);
                sci::istream scriptStream(script.data(), (uint32_t)script.size());
                sci::istream heapStream(heap.data(), (uint32_t)heap.size());
                Assert::IsTrue(patched.Load(helper, helper.Version, 964, scriptStream, &heapStream), Wide(label + "the patched script loads").c_str());
                std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(AppResourceMap(), lookups.GetSelectorTable());
                TestDecompilerResults results;
                std::unique_ptr<sci::Script> decompiled = DecompileScript(config.get(), lookups, AppResourceMap(), 964, patched, results);
                std::stringstream text;
                sci::SourceCodeWriter writer(text, decompiled.get());
                decompiled->OutputSourceCode(writer);
                std::string source = text.str();
                Assert::IsTrue(source.find("(procedure (staleFirst") != std::string::npos, Wide(label + source).c_str());
                Assert::IsTrue(source.find("staleSecond") == std::string::npos, Wide(label + source).c_str());
                Assert::IsTrue(source.find("proc964_1") == std::string::npos, Wide(label + source).c_str());
                bool warned = false;
                for (const std::string &warning : results.warnings)
                {
                    warned = warned || (warning.find(fmt::format("Export 1 points outside the code of the script ({0:04x})", outside)) != std::string::npos);
                    Assert::IsTrue(warning.find("Invalid function offset") == std::string::npos, Wide(label + warning).c_str());
                }
                Assert::IsTrue(warned, Wide(label + "a warning names the export").c_str());
                // The function report has a line for it.
                bool reported = false;
                for (const DecompiledFunction &function : results.functions)
                {
                    reported = reported || ((function.output == "outside") && (function.offset == outside));
                }
                Assert::IsTrue(reported, Wide(label + "the function report has the export").c_str());
            }
        }

        // A call to an export that has no procedure in the decompiled text of
        // its script is a call to a missing procedure (__proc964_1). The .sco
        // of that decompiled script gives no warning for the slot.
        TEST_METHOD(ExportOutsideTheCode_CallAndObjectFile)
        {
            AssertCallToALeftOutExport(false);
        }

        TEST_METHOD(StaleExport_CallAndObjectFile)
        {
            AssertCallToALeftOutExport(true);
        }

        // Script 959 calls export 1 of script 964. The test moves that export
        // into the code of the first procedure (stale), or outside the code.
        void AssertCallToALeftOutExport(bool stale)
        {
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = AppResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            AddFixtureScript("X3_StaleExport");
            AddFixtureScript("X5_CallLeftOutExport");
            std::string error;
            Assert::IsTrue(CompileFixture(964, "X3_StaleExport", &error), Wide(error).c_str());
            Assert::IsTrue(CompileFixture(959, "X5_CallLeftOutExport", &error), Wide(error).c_str());
            CompiledScript compiled(964, CompiledScriptFlags::RemoveBadExports);
            Assert::IsTrue(compiled.Load(helper, helper.Version, 964), L"setup: the script loads");
            std::vector<uint8_t> script = compiled.GetRawBytes();
            // SCI1.1: the count of the exports at 6, the exports from 8.
            Assert::AreEqual((uint16_t)2, (uint16_t)(script[6] | (script[7] << 8)), L"setup: two exports");
            uint16_t first = (uint16_t)(script[8] | (script[9] << 8));
            // Stale: the second ldi of staleFirst.
            uint16_t moved = stale ? (uint16_t)(first + 4) : (uint16_t)0xf9ff;
            script[10] = (uint8_t)(moved & 0xff);
            script[11] = (uint8_t)(moved >> 8);
            ResourceBlob blob(helper, nullptr, ResourceType::Script, script, helper.Version.DefaultVolumeFile, 964, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
            Assert::IsTrue(SUCCEEDED(rm.AppendResource(blob)), L"setup: the patched script 964");

            DecompileOutput caller = DecompileToText(959);
            Assert::IsTrue(caller.text.find("(__proc964_1)") != std::string::npos, Wide(caller.text).c_str());
            Assert::IsTrue(caller.text.find("staleSecond") == std::string::npos, Wide(caller.text).c_str());
            // The asm of the call names the missing procedure too.
            {
                GlobalCompiledScriptLookups lookups;
                lookups.Load(helper);
                CompiledScript callerScript(959, CompiledScriptFlags::RemoveBadExports);
                Assert::IsTrue(callerScript.Load(helper, helper.Version, 959), L"setup: script 959 loads");
                std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(rm, lookups.GetSelectorTable());
                TestDecompilerResults results;
                std::unique_ptr<sci::Script> decompiled = DecompileScript(config.get(), lookups, rm, 959, callerScript, results, false, false, nullptr, true);
                std::stringstream text;
                sci::SourceCodeWriter writer(text, decompiled.get());
                decompiled->OutputSourceCode(writer);
                std::string source = text.str();
                Assert::IsTrue(source.find("(asm") != std::string::npos, Wide("setup: asm\n" + source).c_str());
                Assert::IsTrue(source.find("__proc964_1") != std::string::npos, Wide(source).c_str());
            }

            DecompileOutput callee = DecompileToText(964);
            Assert::IsTrue(callee.text.find("staleSecond") == std::string::npos, Wide(callee.text).c_str());
            std::string path = helper.GetScriptFileName("X3_StaleExport");
            {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << callee.text;
            }
            ScriptId scriptId(path.c_str());
            scriptId.SetResourceNumber(964);
            sci::Result<std::vector<ObjectFileOutcome>> outcomes = GenerateObjectFiles(AppSession(), { scriptId });
            Assert::IsTrue(outcomes.has_value() && (outcomes->size() == 1), L"the .sco run");
            const ObjectFileOutcome &outcome = (*outcomes)[0];
            Assert::IsTrue(outcome.status.has_value(), Wide(outcome.status ? std::string() : outcome.status.error().message).c_str());
            for (const CompileResult &diagnostic : outcome.diagnostics)
            {
                Assert::IsTrue(diagnostic.GetMessage().find("exports the slots") == std::string::npos, Wide(diagnostic.GetMessage()).c_str());
            }
        }

        // A local procedure at 03af has its code. (Space Quest V script 16
        // has two exports that point to 03af, outside its code; the local
        // procedure of QfG3 script 471 at 03af is a real one.)
        TEST_METHOD(LocalProcedureAt03af_HasItsCode)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("X4_LocalProcAt03af", 902);
            Assert::IsTrue(out.text.find("(procedure (localproc_03af param1)") != std::string::npos, Wide("setup: the local procedure is at 03af\n" + out.text).c_str());
            Assert::IsTrue(out.text.find("(= local0 param1)") != std::string::npos, Wide(out.text).c_str());
            Assert::IsFalse(out.HasWarningContaining("Invalid function offset"), Wide(out.text).c_str());
        }

        // Sierra's compiler gave a bad target to the bnt of an empty last
        // clause (PQ3 script 202, addPoint::changeState; ICEMAN script 968,
        // SmoothLooper::doit). The clause is empty, so the code meant the next
        // instruction: the decompile goes there, with a warning, and the
        // recompiled text means the same. The target is past the end of the
        // code, or (for "eq?; bnt; toss") an instruction of the procedure.
        TEST_METHOD(SierraBadBranch_GoesToTheNextInstruction)
        {
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = AppResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            AddFixtureScript("X6_SierraBadBranch");
            for (bool inside : { false, true })
            {
                std::string label = inside ? "target in the code: " : "target past the end: ";
                std::string error;
                Assert::IsTrue(CompileFixture(941, "X6_SierraBadBranch", &error), Wide(label + error).c_str());
                CompiledScript compiled(941, CompiledScriptFlags::RemoveBadExports);
                Assert::IsTrue(compiled.Load(helper, helper.Version, 941), Wide(label + "setup: the script loads").c_str());
                std::vector<uint8_t> script = compiled.GetRawBytes();
                Assert::AreEqual(3, _SetBadBranchTargets(helper.Version, script, inside), Wide(label + "setup: a bnt in each procedure").c_str());
                ResourceBlob blob(helper, nullptr, ResourceType::Script, script, helper.Version.DefaultVolumeFile, 941, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
                Assert::IsTrue(SUCCEEDED(rm.AppendResource(blob)), Wide(label + "setup: the patched script").c_str());
                std::vector<meaning::Function> original = ReadMeaningFunctions(941);

                DecompileOutput out = DecompileToText(941);
                Assert::AreEqual(0, out.fallbacks, Wide(label + out.text).c_str());
                Assert::IsFalse(out.ContainsAsm(), Wide(label + out.text).c_str());
                Assert::IsTrue(out.text.find("-17747") == std::string::npos, Wide(label + out.text).c_str());
                int warnings = 0;
                for (const std::string &warning : out.warnings)
                {
                    Assert::IsTrue(warning.find("Bad branch") == std::string::npos, Wide(label + warning).c_str());
                    warnings += (warning.find("a fault of Sierra's compiler (the test of an empty last clause)") != std::string::npos) ? 1 : 0;
                }
                Assert::AreEqual(3, warnings, Wide(label + "a warning for each bnt").c_str());
                Assert::AreEqual(3, _CountOf(out.text, "; WARNING: "), Wide(label + "a header comment for each bnt\n" + out.text).c_str());
                Assert::AreEqual(3, _CountOf(out.text, "; COMPILER BUG: "), Wide(label + "a comment in each empty clause\n" + out.text).c_str());

                // The recompiled text means the same.
                {
                    std::ofstream file(helper.GetScriptFileName("X6_SierraBadBranch"), std::ios::binary | std::ios::trunc);
                    file << out.text;
                }
                Assert::IsTrue(CompileFixture(941, "X6_SierraBadBranch", &error), Wide(label + "the recompile: " + error + "\n" + out.text).c_str());
                AssertMeaningKept("X6_SierraBadBranch", original, 941);
                AddFixtureScript("X6_SierraBadBranch");
            }
        }

        static int _CountOf(const std::string &text, const std::string &needle)
        {
            int count = 0;
            for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
            {
                ++count;
            }
            return count;
        }

        // Gives a bad target to each bnt before a toss, ret or jmp in the
        // exported procedures of an SCI1.1 script: past the end of the code;
        // with inside, the bnt of "eq?; bnt; toss" goes to the start of its
        // procedure. The count of the bnts.
        static int _SetBadBranchTargets(const SCIVersion &version, std::vector<uint8_t> &script, bool inside)
        {
            int count = 0;
            uint16_t exports = (uint16_t)(script[6] | (script[7] << 8));
            for (uint16_t e = 0; e < exports; ++e)
            {
                size_t start = (size_t)(script[8 + 2 * e] | (script[9 + 2 * e] << 8));
                Opcode previous = Opcode::INDETERMINATE;
                size_t lastTarget = start;
                for (size_t at = start; at < script.size(); )
                {
                    Opcode opcode = RawToOpcode(version, script[at]);
                    size_t next = at + scii::GetInstructionSize(version, script[at]);
                    bool word = ((script[at] & 1) == 0);
                    if ((opcode == Opcode::BNT) || (opcode == Opcode::BT) || (opcode == Opcode::JMP))
                    {
                        int offset = word ? (int)(int16_t)(script[at + 1] | (script[at + 2] << 8)) : (int)(int8_t)script[at + 1];
                        lastTarget = max(lastTarget, (size_t)((int)next + offset));
                    }
                    if ((opcode == Opcode::BNT) && (next < script.size()))
                    {
                        Opcode after = RawToOpcode(version, script[next]);
                        if ((after == Opcode::TOSS) || (after == Opcode::RET) || (after == Opcode::JMP))
                        {
                            bool toStart = inside && (after == Opcode::TOSS) && (previous == Opcode::EQ);
                            int offset = toStart ? -(int)(next - start) : (word ? -0x8000 : -0x80);
                            script[at + 1] = (uint8_t)(offset & 0xff);
                            if (word)
                            {
                                script[at + 2] = (uint8_t)((offset >> 8) & 0xff);
                            }
                            ++count;
                        }
                    }
                    if ((opcode == Opcode::RET) && (lastTarget <= at))
                    {
                        break;
                    }
                    previous = opcode;
                    at = next;
                }
            }
            return count;
        }

        // A stale .sco with fewer exports than the compiled script. The proc at
        // the missing export index gets the generated proc952_1 name. Before the
        // fix the inverted condition (if (name.empty()) SetName(name)) blanked
        // it; after the fix the generated name survives.
        TEST_METHOD(StaleSco_KeepsProcName)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("StaleScoProcs");
            std::string error;
            Assert::IsTrue(CompileFixture(952, "StaleScoProcs", &error),
                std::wstring(error.begin(), error.end()).c_str());

            const GameFolderHelper &helper = AppResourceMap().Helper();
            GlobalCompiledScriptLookups lookups;
            lookups.Load(helper);
            std::unique_ptr<CSCOFile> sco = GetExistingSCOFromScriptNumber(helper, 952, lookups.GetSelectorTable());
            Assert::IsNotNull(sco.get(), L"compile should have written an .sco");
            Assert::IsTrue(sco->GetExports().size() >= 2, L"fixture should export two procs");

            // Drop export index 1 explicitly to simulate a stale .sco.
            std::vector<CSCOPublicExport> &exports = sco->GetExports();
            for (auto it = exports.begin(); it != exports.end(); ++it)
            {
                if (it->GetIndex() == 1)
                {
                    exports.erase(it);
                    break;
                }
            }
            Assert::IsTrue(SaveSCOFile(helper, *sco).has_value(), L"setup: could not write the .sco");

            DecompileOutput out = DecompileToText(952);
            Assert::IsTrue(out.text.find("proc952_1") != std::string::npos,
                L"the undetermined public proc name must survive a stale .sco, not be blanked");
        }

        FIXTURE_TEST(Compiler_ValueAndOr, "C1_ValueAndOr", 908)

        // Sierra's own sequence for an indexed compound assignment loads to
        // the accumulator and pushes ("lati; push"), where SCI Companion
        // emits "lsti". The decompiler folds both.
        FIXTURE_TEST(SierraIndexedMathAssign, "C3_SierraIndexedMathAssign", 932)

        // Compiler: a compound assignment to an indexed variable with a simple
        // indexer compiles to Sierra's sequence, so the text round-trips. Used
        // as a value, it gives the new value.
        FIXTURE_TEST(Compiler_IndexedMathAssign, "C2_IndexedMathAssign", 920)

        // Compiler: a classdef that names the species of a real class does
        // not turn the selector check off for that class.
        TEST_METHOD(Compiler_ClassDefRealClassIsChecked)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("C4_ClassDefRealClass");
            std::string error;
            bool ok = CompileFixture(934, "C4_ClassDefRealClass", &error);
            Assert::IsFalse(ok, L"a bogus selector on a real class must not compile");
            Assert::IsTrue(error.find("c4BogusSelector") != std::string::npos,
                std::wstring(error.begin(), error.end()).c_str());
        }

        // Compiler: an indexer with a side effect in an indexed compound
        // assignment runs twice, and the compiler says so; a plain expression
        // gets no warning.
        TEST_METHOD(Compiler_IndexerSideEffectWarns)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("C5_IndexerSideEffect");
            std::string error;
            std::vector<std::string> warnings;
            bool ok = CompileFixture(935, "C5_IndexerSideEffect", &error, &warnings);
            Assert::IsTrue(ok, std::wstring(error.begin(), error.end()).c_str());
            int twice = 0;
            for (const std::string &w : warnings)
            {
                if (w.find("evaluated twice") != std::string::npos)
                {
                    twice++;
                }
            }
            Assert::AreEqual(1, twice, L"exactly one indexer has a side effect");
        }

        // A break out of a loop from inside a switch case.
        FIXTURE_TEST(BreakInSwitchCase, "F9_BreakInSwitchCase", 921)

        // A mid-body continue: a second jump to the loop head.
        FIXTURE_TEST(MidBodyContinue, "F10_MidBodyContinue", 922)

        // A chained comparison compiled with a pprev folds back into one n-ary
        // comparison; a comparison with no shared operand stays an and.
        FIXTURE_TEST(ChainedComparison, "N1_ChainedCompare", 923)

        // Sierra's own shape for a chained comparison, a variable last.
        FIXTURE_TEST(SierraChainedComparison, "N2_SierraChainedCompare", 933)

        // A bare "jmp head" that several branches inside a loop body share.
        FIXTURE_TEST(LatchTrampoline, "F11_LatchTrampoline", 924)

        // A break at the end of an if's else, followed by a statement that
        // another branch also reaches: the if does not hold the shared
        // statement.
        FIXTURE_TEST(BreakJoin, "F12_BreakJoin", 925)

        // A repeat whose break jumps past the latch, to the end of the loop,
        // with a second repeat (which holds a while) between the latch and
        // the end. The first repeat holds the other two loops.
        FIXTURE_TEST(BreakPastLatch, "F14_BreakPastLatch", 936)

        // A while that is the first statement of a repeat: the two loops
        // share their head. With a breakif in the while, the decompiler makes
        // them nested loops. A while with a continue stays one loop. There is
        // no warning.
        TEST_METHOD(SharedLoopHead)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = AssertDecompileMatchesExpected("F15_SharedLoopHead", 937);
            LogWarnings("F15", out);
            Assert::IsTrue(out.warnings.empty(), L"expected no warning");
        }

        // A while that is the first statement of a repeat, with no break in
        // the while: one loop, whose text is a cond in the repeat.
        FIXTURE_TEST(SharedLoopHead_OneLoopStructures, "F16_SharedHeadOneLoop", 938)

        // An or that ends where the and around it ends: Sierra's compiler
        // sends the or's "bt" past that join, to the outer or's end. No round
        // trip: the compiler of this repository gives the or another shape.
        TEST_METHOD(ThreadedOrJoin)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("F17_ThreadedOrJoin", 939, false);
        }

        // A loop whose body starts with a switch, with a "jmp" to the loop
        // head that only branches reach.
        FIXTURE_TEST(SwitchHeadContinue, "F18_SwitchHeadContinue", 942)

        // An if with an else, used as a value in a compare that is an operand
        // of an and, or the first operand of an or: the compare takes the push
        // before the if. The join of the if can also store the value.
        FIXTURE_TEST(ValueIfInAnd, "F19_ValueIfInAnd", 943)

        // An or that is the test of an if at the end of a loop body, with an
        // and as its last operand: the and's "bnt" goes past the if's "bnt"
        // to the loop head.
        FIXTURE_TEST(OrAndLoopHead, "F20_OrAndLoopHead", 944)

        // Return values take the golden shape: an if whose branches return is
        // not itself returned; a value-shaped if at the end of the function
        // is; a ++ before the final ret is not a return value.
        FIXTURE_TEST(ReturnShapes, "R1_ReturnShapes", 927)

        // Sierra reuses the accumulator: a store, then the pushes of a send
        // whose target or pushed argument is that variable, with no load. The
        // store is a statement of its own; the send reads the variable.
        FIXTURE_TEST(ReusedAccumulator, "A1_ReusedAcc", 928)

        // A selector pushed as "push" after an ldi of its number (the
        // optimizer's reuse), and a literal argument repeated with dup.
        FIXTURE_TEST(ReusedSelector, "A2_ReusedSelector", 930)

        // A send whose last argument is a value if, with earlier pushes before
        // the if: the pushes belong to the send across the join.
        FIXTURE_TEST(ValueIfArgument, "F13_ValueIfArgument", 931)

        // A bnt right after a bnt to the same target is dead (the accumulator
        // is unchanged): the compare before it is not cloned into a second
        // operand.
        FIXTURE_TEST(DeadBranch, "B1_DeadBranch", 929)

        // Plain SCI Companion source: nested conds in a loop body. The text is
        // its own oracle, so the compiler's jump dialect round-trips stably.
        FIXTURE_TEST(Plain_CondInLoop, "P2_CondInLoop", 926)

        // Family 1: a conditional branch to the loop head.
        TEST_METHOD(Family1_LoopHeadContinue)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F1_LoopHeadContinue", 900);
            LogWarnings("F1", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsTrue(out.text.find("(if temp1") != std::string::npos,
                L"the if at the end of the loop body should reconstruct");
        }

        // Family 3: a short-circuit and/or value that is joined and then
        // consumed. Each fixture is pinned to its Sierra-shaped expected text.
        FIXTURE_TEST(Family3_ValueIfReturn, "F3_ValueIfReturn", 909)
        FIXTURE_TEST(Family3_OrThreeTerms, "F3_OrThreeTerms", 910)
        FIXTURE_TEST(Family3_OrAndOr, "F3_OrAndOr", 911)
        FIXTURE_TEST(Family3_AndOr, "F3_AndOr", 912)
        FIXTURE_TEST(Family3_IfValueWithElse, "F3_IfValueWithElse", 913)
        FIXTURE_TEST(Family3_AndAsArgument, "F3_AndAsArgument", 914)

        // A shared-then shape ((or (not X) Y) with a synthesized not) is not
        // a Sierra compiler output. The scope parser must refuse it, not
        // merge it as an and with the wrong value. The asm fallback
        // round-trips.
        TEST_METHOD(Unstructured_SharedThenBranch)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("X_SharedThenBranch", 903);
            LogWarnings("X", out);
            Assert::IsTrue(out.fallbacks >= 1, L"expected a clean fallback");
            Assert::IsTrue(out.HasWarningContaining("[scope:parse:no-scope-for-target]"),
                L"expected the scope parser to refuse the shape");
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
            // The function report has the refusal.
            Assert::AreEqual((size_t)1, out.functions.size());
            const DecompiledFunction &function = out.functions[0];
            Assert::AreEqual((uint16_t)903, function.script);
            Assert::AreEqual(std::string(), function.className);
            Assert::AreEqual(std::string("asm"), function.output);
            Assert::IsTrue(function.byteCount > 0, L"the bytes of the function");
            Assert::AreEqual(std::string("[scope:parse:no-scope-for-target]"), function.scope);
        }

        // Code that the text cannot have (a super in a procedure, a property
        // past the end of the object): each function falls back to asm, which
        // compiles to the same code.
        TEST_METHOD(Unstructured_NoTextForTheCode)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("X3_NoTextForTheCode", 971);
            LogWarnings("X3", out);
            Assert::AreEqual(3, out.fallbacks, L"expected three fallbacks");
            Assert::IsTrue(out.HasWarningContaining("A super in a procedure."));
            Assert::IsTrue(out.HasWarningContaining("A property with no name."));
            Assert::IsTrue(out.ContainsAsm(), L"expected an asm fallback");
            for (const DecompiledFunction &function : out.functions)
            {
                Assert::AreEqual(std::string("asm"), function.output);
                Assert::AreEqual(std::string("[scope:values:syntax]"), function.scope);
            }
        }

        // Family 4: a "bnt" to the loop exit inside the body.
        FIXTURE_TEST(Family4_BreakElseEdge, "F4_BreakElseEdge", 904)
        FIXTURE_TEST(Family4_WhileAnd, "F4_WhileAnd", 915)
        FIXTURE_TEST(Family4_WhileOr, "F4_WhileOr", 916)

        // Compound conditions compiled by SCI Companion's own compiler (its
        // "bt" targets the then block). The decompiled text must equal the
        // source, which covers the unchain fixup end to end.
        FIXTURE_TEST(Plain_CompoundConditions, "P1_CompoundConditions", 917)

        // Family 8: a statement shares the block with the test of an if that
        // a "ret" consumes as a value: the statement comes before the return.
        FIXTURE_TEST(Family8_AssignBeforeCondInRet, "F8_AssignBeforeCondInRet", 918)
        FIXTURE_TEST(Family8_DeadValueStatement, "F8_DeadValueStatement", 919)

        // Family 5: an empty leading while, then another loop.
        TEST_METHOD(Family5_EmptyLeadingWhile)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F5_EmptyLeadingWhile", 905);
            LogWarnings("F5", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsTrue(out.text.find("(while") != std::string::npos,
                L"expected the two while loops to reconstruct");
        }

        // Family 6: an empty trailing for, with a dead back-jump.
        TEST_METHOD(Family6_EmptyTrailingFor)
        {
            _gameFolder = SetUpGameSCI11();
            DecompileOutput out = DecompileAndRoundTrip("F6_EmptyTrailingFor", 906);
            LogWarnings("F6", out);
            Assert::AreEqual(0, out.fallbacks, L"should decompile with no fallback");
            Assert::IsFalse(out.ContainsAsm(), L"should have no asm");
            Assert::IsTrue(out.text.find("(while") != std::string::npos,
                L"expected the outer while to reconstruct");
        }

        // Family 7: a class opcode names a species whose defining script is
        // not in the game. The decompiler synthesizes Unknown_Class_<species>
        // and emits a classdef, so it decompiles and round-trips with no asm.
        FIXTURE_TEST(Family7_UnknownClass, "F7_UnknownClass", 907)

        // Values that the optimiser reuses across a branch.
        FIXTURE_TEST(ReuseAcrossBranch, "V1_ReuseAcrossBranch", 953)
        // The operands of a mul that the optimiser swapped.
        FIXTURE_TEST(SwappedOperands, "V2_SwappedOperands", 965)
        // A store that the push of an argument count or a selector takes.
        FIXTURE_TEST(StoreInSlot, "V3_StoreInSlot", 966)
        // A loop whose value is the test of an if.
        FIXTURE_TEST(LoopValue, "V4_LoopValue", 967)
        // A term of an and with statements before its value: a group. Script
        // 979 has the text-tuple procedure FormatPrint, and text 976 has the
        // text of its call.
        TEST_METHOD(GroupTerm)
        {
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = AppResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            std::vector<uint8_t> text = { 'G', 'r', 'o', 'u', 'p', 0 };
            ResourceBlob blob(helper, nullptr, ResourceType::Text, text, helper.Version.DefaultVolumeFile, 976, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
            Assert::IsTrue(SUCCEEDED(rm.AppendResource(blob)), L"setup: text 976");
            std::unique_ptr<ResourceEntity> textResource = rm.CreateResourceFromNumber(ResourceType::Text, 976);
            Assert::IsTrue(textResource && textResource->TryGetComponent<TextComponent>() && (textResource->TryGetComponent<TextComponent>()->Texts.size() == 1), L"setup: text 976 reads back");
            // The text-tuple procedures are in Decompiler.ini, which the decompile reads from src.
            std::string ini = GetTestModuleDirectory() + "\\Decompiler\\Decompiler.ini";
            Assert::IsTrue(CopyFile(ini.c_str(), (helper.GetSrcFolder() + "\\Decompiler.ini").c_str(), FALSE) != 0, L"setup: Decompiler.ini");
            AddFixtureScript("V5_FormatPrint");
            std::string error;
            Assert::IsTrue(CompileFixture(979, "V5_FormatPrint", &error), Wide(error).c_str());
            DecompileOutput out = AssertDecompileMatchesExpected("V5_GroupTerm", 976);
            Assert::IsTrue(out.text.find("; Group") != std::string::npos, L"setup: the text of the call is a comment");
        }

        // The second operand of an or with statements before its value, which
        // an instruction after the or reads: a group.
        FIXTURE_TEST(GroupOrOperand, "V7_GroupOrOperand", 957)

        // A group whose first statement is a value would be a call or a send
        // as text: the function falls back to asm, and the text compiles.
        TEST_METHOD(GroupValueFirst_FallsBackToAsm)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("V6_GroupValueFirst");
            std::string error;
            Assert::IsTrue(CompileFixture(980, "V6_GroupValueFirst", &error), Wide(error).c_str());
            DecompileOutput out = DecompileToText(980);
            Assert::AreEqual(1, out.fallbacks, L"the function falls back to asm");
            Assert::IsTrue(out.text.find("(local0 (Abs") == std::string::npos, Wide(out.text).c_str());
            {
                std::ofstream file(AppResourceMap().Helper().GetScriptFileName("V6_GroupValueFirst").c_str(), std::ios::binary | std::ios::trunc);
                file << out.text;
            }
            error.clear();
            bool compiled = CompileFixture(980, "V6_GroupValueFirst", &error);
            Assert::IsTrue(compiled, Wide("the decompiled text does not compile: " + error).c_str());
        }
        // An instance with the name of a property, and a &rest before the last
        // argument.
        FIXTURE_TEST(ObjectNamedLikeAProperty, "O1_ObjectNamedLikeAProperty", 968)
        // Classes of the class table with one name, and an instance named like a class.
        FIXTURE_TEST(ClassNames, "O2_ClassNames", 973)

        // Of two classes with one species (King's Quest V script 764 has two
        // SaveIcon classes), the first is the class of the class table and
        // keeps its name; the second gets another name, so the text has no two
        // classes with one name.
        TEST_METHOD(ClassNames_TwoClassesWithOneSpecies)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("O2_ClassNames");
            std::string error;
            Assert::IsTrue(CompileFixture(973, "O2_ClassNames", &error), Wide(error).c_str());
            const GameFolderHelper &helper = AppResourceMap().Helper();
            CompiledScript compiled(973);
            Assert::IsTrue(compiled.Load(helper, helper.Version, 973), L"setup: the script loads");
            std::vector<uint8_t> script = compiled.GetRawBytes();
            std::unique_ptr<ResourceBlob> heapBlob = helper.MostRecentResource(ResourceType::Heap, 973, ResourceEnumFlags::None);
            Assert::IsNotNull(heapBlob.get(), L"setup: the heap");
            sci::istream heapRead = heapBlob->GetReadStream();
            std::vector<uint8_t> heap(heapRead.GetDataSize());
            heapRead.read_data(heap.data(), (uint32_t)heap.size());
            // The species of the second class (-script-, after the magic word and
            // four words) becomes that of the first.
            std::vector<uint16_t> classPositions;
            for (const auto &object : compiled.GetObjects())
            {
                if (!object->IsInstance())
                {
                    classPositions.push_back(object->GetPosInResource());
                }
            }
            Assert::AreEqual((size_t)2, classPositions.size(), L"setup: two classes");
            heap[classPositions[1] + 10] = heap[classPositions[0] + 10];
            heap[classPositions[1] + 11] = heap[classPositions[0] + 11];

            GlobalCompiledScriptLookups lookups;
            lookups.Load(helper);
            CompiledScript patched(973);
            sci::istream scriptStream(script.data(), (uint32_t)script.size());
            sci::istream heapStream(heap.data(), (uint32_t)heap.size());
            Assert::IsTrue(patched.Load(helper, helper.Version, 973, scriptStream, &heapStream), L"the patched script loads");
            Assert::AreEqual(patched.GetObjects()[0]->GetSpecies(), patched.GetObjects()[1]->GetSpecies(), L"setup: one species");
            std::unique_ptr<IDecompilerConfig> config = CreateDecompilerConfig(AppResourceMap(), lookups.GetSelectorTable());
            TestDecompilerResults results;
            std::unique_ptr<sci::Script> decompiled = DecompileScript(config.get(), lookups, AppResourceMap(), 973, patched, results);
            std::stringstream text;
            sci::SourceCodeWriter writer(text, decompiled.get());
            decompiled->OutputSourceCode(writer);
            std::string source = text.str();
            size_t first = source.find("(class o2Same of");
            Assert::IsTrue(first != std::string::npos, Wide(source).c_str());
            Assert::IsTrue(source.find("(class o2Same of", first + 1) == std::string::npos, Wide(source).c_str());
        }
        FIXTURE_TEST(RestBeforeTheLastArgument, "R2_RestBeforeTheLastArgument", 969)
        // Sends that the compiler warns about.
        FIXTURE_TEST(CompilerWarnings, "C5_CompilerWarnings", 970)
        // Classes with no superclass: their properties in the order of the text.
        FIXTURE_TEST(RootClasses, "R3_RootClasses", 972)
        // Classes with &layout: slots that are not the slots of the superclass.
        FIXTURE_TEST(ClassLayout, "O3_ClassLayout", 974)

        // The meaning check keys the methods of a class with a made-up name (no
        // name string, or a made-up name as its string) by its species: the
        // position of the class, and so its made-up name, can change when the
        // text compiles again (Castle of Dr. Brain script 943).
        TEST_METHOD(RootClasses_MadeUpNameKeyedBySpecies)
        {
            _gameFolder = SetUpGameSCI11();
            AddFixtureScript("R3_RootClasses");
            std::string error;
            Assert::IsTrue(CompileFixture(972, "R3_RootClasses", &error), Wide(error).c_str());
            bool madeUp = false;
            bool madeUpString = false;
            bool named = false;
            bool nameSecond = false;
            for (const meaning::Function &function : ReadMeaningFunctions(972))
            {
                if (function.display == "Class_972_0::doit")
                {
                    madeUp = true;
                    Assert::IsTrue(function.key.rfind("class ", 0) == 0, Wide(function.key).c_str());
                    Assert::IsTrue(function.key.find("::doit") != std::string::npos, Wide(function.key).c_str());
                }
                if (function.display == "Class_972_9::doit")
                {
                    madeUpString = true;
                    Assert::IsTrue(function.key.rfind("class ", 0) == 0, Wide(function.key).c_str());
                }
                if (function.display == "r3NamedRoot::doit")
                {
                    named = true;
                    Assert::AreEqual(std::string("r3NamedRoot::doit"), function.key);
                }
                if (function.display == "r3NameSecond::doit")
                {
                    // The name slot is after another property.
                    nameSecond = true;
                    Assert::AreEqual(std::string("r3NameSecond::doit"), function.key);
                }
            }
            Assert::IsTrue(madeUp && madeUpString && named && nameSecond, L"the four methods");
        }

        // Switches: a switch as a value, a case value with a branch, an
        // empty last case (also in a loop), and cases that all return.
        FIXTURE_TEST(SwitchValue, "S1_SwitchValue", 945)
        FIXTURE_TEST(CaseValueBranch, "S2_CaseValueBranch", 946)
        FIXTURE_TEST(EmptyLastCase, "S3_EmptyLastCase", 947)
        FIXTURE_TEST(EmptyLastCaseInLoop, "S4_EmptyLastCaseInLoop", 948)
        FIXTURE_TEST(SwitchAllReturn, "S5_SwitchAllReturn", 949)
        // A case whose test does nothing is the else case.
        FIXTURE_TEST(NoOpCaseTest, "S6_NoOpCaseTest", 963)
        // Dead breaks after the jmp of a then-part: no statement.
        FIXTURE_TEST(DeadBreak, "D1_DeadBreak", 956)
        // Dead code after a jmp and after a break: no statement.
        FIXTURE_TEST(DeadCode, "D2_DeadCode", 958)
        // A continue of a for from an inner while (the compiler makes its
        // jmp to the step of the for).
        FIXTURE_TEST(ContinueTwoInFor, "L2_ContinueTwoInFor", 962)
        // A break of level 2, and a continue in a do loop. No round trip:
        // the compiler of this repository gives the repeat another shape.
        TEST_METHOD(LoopLevels)
        {
            _gameFolder = SetUpGameSCI11();
            AssertDecompileMatchesExpected("L1_LoopLevels", 954, false);
        }

        // Regression guard: total assembly fallbacks across the template game
        // must not grow, and no new script may fall back. Lower BASELINE and
        // shrink the allowlist deliberately when a fix helps.
        TEST_METHOD(TemplateGame_FallbackBaseline)
        {
            _gameFolder = SetUpGameSCI11();
            std::vector<std::string> failed;
            std::vector<std::string> warnings;
            int processed = 0;
            int fallbacks = CountFallbacksAllScripts(&failed, &processed, &warnings);
            std::string msg = fmt::format("Template: {0} scripts, {1} fallbacks in {2}", processed, fallbacks, failed.size());
            for (const std::string &name : failed)
            {
                msg += "\n  " + name;
            }
            for (const std::string &w : warnings)
            {
                msg += "\n    " + w;
            }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());

            // Floor: the guard is meaningless if no scripts decompiled. The
            // template has more than 80 scripts.
            Assert::IsTrue(processed >= 80, L"too few scripts decompiled; check the template game data");

            const int BASELINE = 0;   // was 7; every template script now decompiles. Keep at 0.
            Assert::IsTrue(fallbacks <= BASELINE, L"template fallbacks grew beyond baseline");

            // The set of scripts that fall back. A new failure is caught even
            // when a fix removes a different one. Empty: nothing falls back.
            std::set<std::string> allowed = {};
            for (const std::string &name : failed)
            {
                Assert::IsTrue(allowed.count(name) == 1,
                    std::wstring(L"new fallback script: ").append(name.begin(), name.end()).c_str());
            }
        }

        // Regression guard: the decompiled text of every template script
        // recompiles. Catches a newly-emitted construct the compiler rejects.
        // Every script must recompile; a failure entry carries the compiler
        // error for diagnosis.
        TEST_METHOD(TemplateGame_Recompiles)
        {
            _gameFolder = SetUpGameSCI11();
            std::vector<std::string> failed;
            int processed = 0;
            RecompileAllDecompiledScripts(&failed, &processed);
            std::string msg = fmt::format("Recompile: {0} scripts, {1} failed", processed, failed.size());
            for (const std::string &name : failed)
            {
                msg += "\n  " + name;
            }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());

            Assert::IsTrue(processed >= 80, L"too few scripts processed; check the template game data");
            Assert::AreEqual((size_t)0, failed.size(),
                std::wstring(msg.begin(), msg.end()).c_str());
        }

        // Regression guard: the decompiled text of every template script matches
        // its committed snapshot. Any output change fails here. Accept an
        // intended change with RunTests.ps1 -UpdateSnapshots, then commit the
        // snapshot change with the code.
        TEST_METHOD(TemplateGame_Snapshot)
        {
            _gameFolder = SetUpGameSCI11();
            SnapshotResult r = CompareTemplateSnapshots();
            std::string msg = fmt::format("Snapshot: {0} scripts, {1} changed, {2} missing",
                r.processed, r.mismatched.size(), r.missingExpected.size());
            for (const std::string &name : r.mismatched) { msg += "\n  changed: " + name; }
            for (const std::string &name : r.missingExpected) { msg += "\n  missing: " + name; }
            Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());

            Assert::IsTrue(r.processed >= 80, L"too few scripts; check the template game data");
            Assert::IsTrue(r.missingExpected.empty(),
                L"a snapshot is missing; run RunTests.ps1 -UpdateSnapshots to create it");
            Assert::IsTrue(r.mismatched.empty(),
                L"a snapshot changed; review then run RunTests.ps1 -UpdateSnapshots");
        }

    private:
        std::string _gameFolder;
    };

    // Not in the default run. Decompiles named template scripts with the
    // control-flow dump on and logs every warning, to diagnose a failure:
    //   RunTests.ps1 -Filter "FullyQualifiedName~DiagnosticDumps"
    TEST_CLASS(DiagnosticDumps)
    {
    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
            if (_existingGame)
            {
                CleanUpExistingGame();
                _existingGame = false;
            }
        }

        // Decompiles every script of an existing game (read-only) to a folder,
        // for the golden diff with Tools\CompareDecompile.ps1. Driven by
        // environment variables so no local path is in the source:
        //   SCICOMP_DUMP_GAME   game folder (required; the test skips without it)
        //   SCICOMP_DUMP_OUT    output folder (required)
        //   SCICOMP_DUMP_NAMES  golden folder used to name the files (optional)
        // Warnings go to <out>\_warnings.txt.
        TEST_METHOD(Dump_ExistingGame)
        {
            const char *game = getenv("SCICOMP_DUMP_GAME");
            const char *out = getenv("SCICOMP_DUMP_OUT");
            const char *namesDir = getenv("SCICOMP_DUMP_NAMES");
            if (!game || !out)
            {
                Logger::WriteMessage(L"Skipped: set SCICOMP_DUMP_GAME and SCICOMP_DUMP_OUT.");
                return;
            }
            SetUpExistingGame(game);
            _existingGame = true;

            // One script, with the control-flow and instruction dumps on, for
            // diagnosing a failure. SCICOMP_DUMP_SCRIPT is the script number.
            if (const char *one = getenv("SCICOMP_DUMP_SCRIPT"))
            {
                uint16_t number = static_cast<uint16_t>(atoi(one));
                DecompileOutput single = DecompileToText(number, true, true);
                std::string report = single.text + "\n\n===== warnings =====\n";
                for (const std::string &w : single.warnings)
                {
                    report += w + "\n";
                }
                CreateDirectoryA(out, nullptr);
                std::ofstream file(fmt::format("{0}\\_script_{1}.txt", out, number), std::ios::binary);
                file << report;
                std::ofstream selectors(fmt::format("{0}\\_selectors.txt", out), std::ios::binary);
                selectors << DumpSelectorTable();
                Logger::WriteMessage(L"Wrote single-script dump.");
                return;
            }

            std::vector<std::string> warnings;
            int processed = 0;
            int fallbacks = DumpAllScripts(out, namesDir ? namesDir : "", &warnings, &processed);

            std::string report = fmt::format("Dump: {0} scripts, {1} fallbacks\n", processed, fallbacks);
            for (const std::string &w : warnings)
            {
                report += w + "\n";
            }
            std::ofstream file(std::string(out) + "\\_warnings.txt", std::ios::binary);
            file << report;
            Logger::WriteMessage(std::wstring(report.begin(), report.end()).c_str());
        }

        // The structural compare of a dump against golden sources, per
        // function: SCICOMP_COMPARE_EXPECTED (golden folder),
        // SCICOMP_COMPARE_ACTUAL (the dump), SCICOMP_COMPARE_OUT (report and
        // per-difference files). The template game only provides the parser's
        // context.
        TEST_METHOD(Compare_Structural)
        {
            const char *expected = getenv("SCICOMP_COMPARE_EXPECTED");
            const char *actual = getenv("SCICOMP_COMPARE_ACTUAL");
            const char *out = getenv("SCICOMP_COMPARE_OUT");
            if (!expected || !actual)
            {
                Logger::WriteMessage(L"Skipped: set SCICOMP_COMPARE_EXPECTED and SCICOMP_COMPARE_ACTUAL.");
                return;
            }
            _gameFolder = SetUpGameSCI11();
            StructuralCompareResult result = CompareStructural(expected, actual, out ? out : "", AppVersion());
            std::string report = result.Report();
            Logger::WriteMessage(std::wstring(report.begin(), report.end()).c_str());
        }

        TEST_METHOD(Dump_FailingTemplateScripts)
        {
            _gameFolder = SetUpGameSCI11();
            // SCICOMP_DUMP_TITLES overrides the list: comma-separated titles.
            std::vector<std::string> titles = { "ScrollableInventory", "SaveRestoreDialog", "Gauge" };
            if (const char *env = getenv("SCICOMP_DUMP_TITLES"))
            {
                titles.clear();
                std::string list = env;
                size_t start = 0;
                while (start <= list.size())
                {
                    size_t comma = list.find(',', start);
                    if (comma == std::string::npos)
                    {
                        comma = list.size();
                    }
                    if (comma > start)
                    {
                        titles.push_back(list.substr(start, comma - start));
                    }
                    start = comma + 1;
                }
            }
            for (const std::string &title : titles)
            {
                DecompileOutput out;
                if (DecompileTemplateScriptByTitle(title, out))
                {
                    std::string msg = std::string("##### ") + title + "\n";
                    for (const std::string &w : out.warnings)
                    {
                        msg += w + "\n";
                    }
                    Logger::WriteMessage(std::wstring(msg.begin(), msg.end()).c_str());
                }
            }
        }

    private:
        std::string _gameFolder;
        bool _existingGame = false;
    };
}
