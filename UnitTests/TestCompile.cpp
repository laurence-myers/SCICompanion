/***************************************************************************
    Copyright (c) 2015 Philip Fortier

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
#include "ResourceMap.h"
#include "AppState.h"
#include "ScriptOM.h"
#include "CompileContext.h"
#include "Helper.h"
#include "AstPassHelper.h"
#include <fstream>
#include <filesystem>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
	TEST_CLASS(TestCompile)
	{
	public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
        }

        TEST_METHOD(TestCompileAllSCI0)
        {
            _gameFolder = SetUpGameSCI0();
            _DoIt();
        }

        TEST_METHOD(TestCompileAllSCI11)
        {
            _gameFolder = SetUpGameSCI11();
            _DoIt();
        }

        TEST_METHOD_CLEANUP(TestCompileAll_Clean)
        {
            CleanUpGame(_gameFolder);
        }

        // A script that (include)s a NON-header .sc must gain that include's
        // procedures, local variables and defines, and each moved node must be
        // reparented to the including script. Before the fix, MergeScripts moved
        // the procedures but left their owner-script pointer dangling (a
        // use-after-free in FunctionBase::PreScan) and dropped the defines
        // entirely, so a reference to a define in the include failed to compile.
        TEST_METHOD(NonHeaderInclude_ReparentsProcsAndMergesDefines)
        {
            _gameFolder = SetUpGameSCI0();

            std::string includeFolder = appState->GetResourceMap().GetIncludeFolder();
            std::error_code mkec;
            std::filesystem::create_directories(includeFolder, mkec);
            std::string includePath = includeFolder + "\\merge_uaf_include.sc";
            {
                std::ofstream f(includePath.c_str(), std::ios::binary | std::ios::trunc);
                f <<
                    ";;; Sierra Script 1.0 - (do not remove this comment)\n"
                    "(define B_MAGIC 42)\n"
                    "(local\n"
                    "\tbLocalVar\n"
                    ")\n"
                    "(procedure (BMergedProc bParam &tmp bTemp)\n"
                    "\t(= bTemp bParam)\n"
                    "\t(= bLocalVar bTemp)\n"
                    "\t(return bLocalVar)\n"
                    ")\n";
            }

            // The main script includes the non-header .sc and uses its (define)
            // and its procedure. A dropped define is an "Undeclared identifier"
            // error; a dangling owner-script pointer is a use-after-free in
            // PreScan (caught under a debug CRT / ASan build).
            std::string mainText =
                ";;; Sierra Script 1.0 - (do not remove this comment)\n"
                "(script# 970)\n"
                "(include merge_uaf_include.sc)\n"
                "(procedure (AMainProc &tmp aTemp)\n"
                "\t(= aTemp B_MAGIC)\n"
                "\t(= aTemp (BMergedProc aTemp))\n"
                "\t(return aTemp)\n"
                ")\n";
            std::unique_ptr<sci::Script> mainScript = ParseSierraScript(mainText);

            CompileLog log;
            CompileTables tables;
            tables.Load(appState->GetVersion());
            PrecompiledHeaders headers(appState->GetResourceMap());
            CompileResults results(log);

            // Runs MergeScripts, then script.PreScan() (the use-after-free site).
            GenerateScriptResource(appState->GetVersion(), *mainScript, headers, tables, results, false);
            log.CalculateErrors();

            bool hasErrors = log.HasErrors();
            std::string errorText;
            for (const auto &r : log.Results())
            {
                if (r.IsError())
                {
                    errorText += r.GetMessage() + "\n";
                }
            }
            bool defineMerged = false;
            for (auto &d : mainScript->GetDefines())
            {
                if (d->GetName() == "B_MAGIC")
                {
                    defineMerged = true;
                    break;
                }
            }

            std::error_code ec;
            std::filesystem::remove(includePath, ec);

            Assert::IsTrue(defineMerged,
                L"The (define) from the non-header .sc include was dropped by MergeScripts.");
            Assert::IsFalse(hasErrors,
                std::wstring(L"A script including a non-header .sc failed to compile:\n").append(errorText.begin(), errorText.end()).c_str());
        }

        void _DoItHelper()
        {
            std::vector<ScriptId> scripts;
            appState->GetResourceMap().GetAllScripts(scripts);
            CompileLog log;
            // TODO: Clear errors?
            CompileTables tables;
            tables.Load(appState->GetVersion());
            PrecompiledHeaders headers(appState->GetResourceMap());
            for (auto &script : scripts)
            {
                CompileResults results(log);
                NewCompileScript(results, log, tables, headers, script);
            }
            Assert::IsFalse(log.HasErrors());
        }

        void _DoIt()
        {
            _DoItHelper();
        }

    private:
        static std::string _gameFolder;
	};

    std::string TestCompile::_gameFolder;
}