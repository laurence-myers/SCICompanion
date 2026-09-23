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
#include "AppState.h"
#include "ResourceMap.h"
#include "ResourceContainer.h"
#include "ResourceBlob.h"
#include "CompiledScript.h"
#include "ScriptOM.h"
#include "SCO.h"
#include "Helper.h"
#include "Stream.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "DecompileHelper.h"
#include <fstream>
#include <map>
#include <memory>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #60: SCOFromScriptAndCompiledScript builds the .sco from a script AST plus
    // the compiled script. It trusted the AST to match the compiled script:
    //  - a class in the AST with no compiled counterpart was looked up with
    //    map::operator[], which inserted a null pointer that was then read;
    //  - a procedure export with no matching public procedure in the AST read
    //    publicProcNames past its end.
    // A decompile that fell back, or a stale AST, gives exactly that mismatch.
    // Both reads are now bounded: the class is skipped, and the export is
    // skipped once the public-procedure names run out.
    TEST_CLASS(TestScoExports)
    {
        std::string _gameFolder;

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            _gameFolder = SetUpGameSCI0();
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        TEST_METHOD(MismatchedAst_UnknownClassAndExtraProcExports_DoNotOverread)
        {
            // Find a compiled template script that exports at least one procedure.
            CResourceMap &rm = appState->GetResourceMap();
            auto container = rm.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::AddInDefaultEnumFlags);
            std::unique_ptr<CompiledScript> compiled;
            for (auto &blob : *container)
            {
                auto candidate = std::make_unique<CompiledScript>(blob->GetNumber());
                sci::istream byteStream = blob->GetReadStream();
                if (!candidate->Load(rm.Helper(), appState->GetVersion(), blob->GetNumber(), byteStream))
                {
                    continue;
                }
                int procExports = 0;
                for (uint16_t exportOffset : candidate->GetExports())
                {
                    if (candidate->IsExportAProcedure(exportOffset))
                    {
                        procExports++;
                    }
                }
                if (procExports > 0)
                {
                    compiled = std::move(candidate);
                    break;
                }
            }
            Assert::IsTrue(compiled != nullptr, L"setup: the SCI0 template must have a script with a procedure export");

            // An AST that declares NO public procedures and one class that the
            // compiled script does not contain.
            sci::Script script;
            auto classDef = std::make_unique<sci::ClassDefinition>();
            classDef->CreateNew(&script, TEXT("ClassNotInCompiledScript"), TEXT("Obj"));
            classDef->SetInstance(false);
            script.AddClass(std::move(classDef));

            // Before the fix: a null dereference on the unknown class and a read
            // past the end of the (empty) public-procedure name list.
            std::unique_ptr<CSCOFile> sco = SCOFromScriptAndCompiledScript(script, *compiled);

            Assert::IsTrue(sco != nullptr);

            // The class list is consumed by position, so it must be exactly the
            // compiled script's classes, in compiled order, and the AST class that
            // the compiled script does not have must not appear.
            std::vector<std::string> expectedNames;
            for (const auto &object : compiled->GetObjects())
            {
                if (!object->IsInstance())
                {
                    expectedNames.push_back(object->GetName());
                }
            }
            Assert::AreEqual(expectedNames.size(), sco->GetObjects().size(), L"one .sco class per compiled class, none from the unknown AST class");
            for (size_t i = 0; i < expectedNames.size(); i++)
            {
                Assert::AreEqual(expectedNames[i], sco->GetObjects()[i].GetName(), L"the .sco class order must follow the compiled order");
                Assert::AreNotEqual(std::string("ClassNotInCompiledScript"), sco->GetObjects()[i].GetName());
            }
            Assert::AreEqual((size_t)0, sco->GetExports().size(), L"procedure exports with no public procedure name in the AST must be skipped");
        }

        // Plan step K2. The (public name N ...) block gives each export its
        // slot. procA is defined first but is in slot 1. Before K2, the .sco
        // paired the names with the export table in definition order, so procA
        // got slot 0 and procB slot 1, and a call by name went to the wrong
        // procedure (KQ5 Interface.sc).
        TEST_METHOD(PublicBlock_ProceduresDefinedOutOfSlotOrder_KeepTheirSlots)
        {
            CResourceMap &resourceMap = appState->GetResourceMap();
            const std::string name = "SlotOrder";
            const char *source =
                "(script# 950)\n"
                "(include sci.sh)\n"
                "(include game.sh)\n"
                "(public\n"
                "    procB 0\n"
                "    procA 1\n"
                ")\n"
                "(procedure (procA)\n"
                "    (return 1)\n"
                ")\n"
                "(procedure (procB)\n"
                "    (return 2)\n"
                ")\n";
            std::string path = resourceMap.Helper().GetScriptFileName(name);
            {
                std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
                file << source;
            }
            // Compile first: the arguments of Assert::IsTrue are evaluated in
            // no fixed order, so the text must not read error in the same call.
            std::string error;
            bool fixtureCompiled = CompileFixture(950, name, &error);
            Assert::IsTrue(fixtureCompiled, std::wstring(error.begin(), error.end()).c_str());

            ScriptId scriptId(path.c_str());
            scriptId.SetResourceNumber(950);
            CompileLog log;
            std::unique_ptr<sci::Script> script = SimpleCompile(resourceMap.GetSCIVersion(), log, scriptId);
            CompiledScript compiled(950);
            Assert::IsTrue(compiled.Load(resourceMap.Helper(), resourceMap.Helper().Version, 950), L"the compiled script must load");

            std::unique_ptr<CSCOFile> sco = SCOFromScriptAndCompiledScript(*script, compiled);

            std::map<std::string, int> slots;
            for (const CSCOPublicExport &publicExport : sco->GetExports())
            {
                slots[publicExport.GetName()] = publicExport.GetIndex();
            }
            Assert::AreEqual(size_t(2), slots.size());
            Assert::AreEqual(0, slots["procB"], L"procB is in slot 0");
            Assert::AreEqual(1, slots["procA"], L"procA is in slot 1");
        }

        // K2 review: a name in several slots. The compiler writes its .sco in
        // slot order, and a lookup by name gives the first entry. Before, the
        // block order (procA 2 first) gave procA the slot 2, and the
        // compiler's .sco gives it 0.
        TEST_METHOD(PublicBlock_NameInSeveralSlots_IsInSlotOrder)
        {
            const char *source =
                "(script# 950)\n"
                "(include sci.sh)\n"
                "(include game.sh)\n"
                "(public\n"
                "    procA 2\n"
                "    procB 1\n"
                "    procA 0\n"
                ")\n"
                "(procedure (procA)\n"
                "    (return 1)\n"
                ")\n"
                "(procedure (procB)\n"
                "    (return 2)\n"
                ")\n";
            std::string differences = CompareScoExports(950, "SeveralSlots", source);
            Assert::IsTrue(differences.empty(), std::wstring(differences.begin(), differences.end()).c_str());
        }

        // K2 review: for each script of both templates, the .sco built from
        // the source and the compiled script has the exports of the .sco that
        // the compiler writes. Before K2, Main of the SCI1.1 template gave
        // AimToward@7, Die@8 and AddToScore@9: its public block is not in
        // definition order.
        TEST_METHOD(TemplateScripts_ScoExportsEqualTheCompilersSco)
        {
            CompareScoExportsOfEveryScript("SCI0");
            CleanUpGame(_gameFolder);
            _gameFolder = SetUpGameSCI11();
            CompareScoExportsOfEveryScript("SCI1.1");
        }

    private:
        static std::string ExportsText(const CSCOFile &sco)
        {
            std::string text;
            for (const CSCOPublicExport &publicExport : sco.GetExports())
            {
                text += publicExport.GetName() + "@" + std::to_string(publicExport.GetIndex()) + " ";
            }
            return text;
        }

        // The exports of the .sco that the compiler wrote, and of the .sco built
        // from the source and the compiled script. Empty when they are equal.
        static std::string CompareWithTheCompilersSco(const ScriptId &scriptId, const GlobalCompiledScriptLookups &lookups)
        {
            CResourceMap &resourceMap = appState->GetResourceMap();
            const GameFolderHelper &helper = resourceMap.Helper();
            uint16_t number = scriptId.GetResourceNumber();
            std::unique_ptr<CSCOFile> written = GetExistingSCOFromScriptNumber(helper, number, lookups.GetSelectorTable());
            CompileLog log;
            ScriptId source = scriptId;
            std::unique_ptr<sci::Script> script = SimpleCompile(resourceMap.GetSCIVersion(), log, source);
            CompiledScript compiledScript(number);
            if (!written || !script || !compiledScript.Load(helper, helper.Version, number))
            {
                return scriptId.GetTitle() + ": the .sco, the source or the compiled script did not load\n";
            }
            std::unique_ptr<CSCOFile> built = SCOFromScriptAndCompiledScript(*script, compiledScript);
            std::string writtenText = ExportsText(*written);
            std::string builtText = ExportsText(*built);
            if (writtenText == builtText)
            {
                return std::string();
            }
            return scriptId.GetTitle() + ": the compiler wrote \"" + writtenText + "\", built \"" + builtText + "\"\n";
        }

        static std::string CompareScoExports(uint16_t number, const std::string &name, const std::string &source)
        {
            CResourceMap &resourceMap = appState->GetResourceMap();
            std::string path = resourceMap.Helper().GetScriptFileName(name);
            {
                std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
                file << source;
            }
            std::string error;
            bool compiled = CompileFixture(number, name, &error);
            if (!compiled)
            {
                return name + " did not compile: " + error + "\n";
            }
            GlobalCompiledScriptLookups lookups;
            lookups.Load(resourceMap.Helper());
            ScriptId scriptId(path.c_str());
            scriptId.SetResourceNumber(number);
            return CompareWithTheCompilersSco(scriptId, lookups);
        }

        void CompareScoExportsOfEveryScript(const std::string &templateName)
        {
            CResourceMap &resourceMap = appState->GetResourceMap();
            std::vector<ScriptId> scripts;
            resourceMap.GetAllScripts(scripts);

            // Compile every script first, so that each .sco is the compiler's.
            std::string differences;
            std::vector<ScriptId> compiledScripts;
            for (ScriptId &scriptId : scripts)
            {
                if (!PathFileExists(scriptId.GetFullPath().c_str()))
                {
                    continue;
                }
                std::string error;
                bool compiled = CompileFixture(scriptId.GetResourceNumber(), scriptId.GetTitle(), &error);
                if (compiled)
                {
                    compiledScripts.push_back(scriptId);
                }
                else
                {
                    differences += scriptId.GetTitle() + " did not compile: " + error + "\n";
                }
            }

            GlobalCompiledScriptLookups lookups;
            Assert::IsTrue(lookups.Load(resourceMap.Helper()));
            for (const ScriptId &scriptId : compiledScripts)
            {
                differences += CompareWithTheCompilersSco(scriptId, lookups);
            }
            std::wstring wideName(templateName.begin(), templateName.end());
            Assert::IsTrue(compiledScripts.size() > 20, (L"too few scripts in the " + wideName + L" template").c_str());
            Assert::IsTrue(differences.empty(), (wideName + L":\n" + std::wstring(differences.begin(), differences.end())).c_str());
        }
    };
}
