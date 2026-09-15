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
#include "ScriptOM.h"
#include "CompileContext.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace sci;

namespace UnitTests
{
    // #33: MergeScripts merges a non-header .sc include into the main script. It
    // moved procedures, variables and defines, but dropped classes and instances,
    // so a class declared in a non-header include never reached the including
    // script. A class and an instance are both ClassDefinitions in the same vector.
    TEST_CLASS(TestMergeScripts)
    {
    public:
        TEST_METHOD(MergeScripts_MovesClassesAndInstances)
        {
            Script mainScript;
            Script included;

            auto classDef = std::make_unique<ClassDefinition>();
            classDef->CreateNew(&included, TEXT("MergedClass"), TEXT("Obj"));
            classDef->SetInstance(false);
            included.AddClass(std::move(classDef));

            auto instDef = std::make_unique<ClassDefinition>();
            instDef->CreateNew(&included, TEXT("MergedInstance"), TEXT("MergedClass"));
            instDef->SetInstance(true);
            included.AddClass(std::move(instDef));

            Assert::AreEqual((size_t)2, included.GetClasses().size(), L"setup: two definitions in the include");
            Assert::AreEqual((size_t)0, mainScript.GetClasses().size(), L"setup: main starts with none");

            MergeScripts(mainScript, included);

            // Before the fix these were dropped, so main stayed empty.
            Assert::AreEqual((size_t)2, mainScript.GetClasses().size(),
                L"the class and the instance must move into the main script");

            int classCount = 0;
            int instanceCount = 0;
            for (const auto &c : mainScript.GetClasses())
            {
                // Each moved node must be reparented, or a later pass reads a
                // destroyed script (the include is freed after the merge).
                Assert::IsTrue(c->GetOwnerScript() == &mainScript,
                    L"a moved class/instance must be reparented to the main script");
                if (c->IsInstance()) { instanceCount++; } else { classCount++; }
            }
            Assert::AreEqual(1, classCount, L"one class moved");
            Assert::AreEqual(1, instanceCount, L"one instance moved");
        }
    };
}
