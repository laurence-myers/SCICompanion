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
#include "sci.h"
#include <set>
#include <string>
#include <unordered_map>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #72: service-layer robustness in process control and error reporting.
    TEST_CLASS(TestProcessAndError)
    {
    public:
        // GetMessageFromLastError read, printed and freed lpMsgBuf even when
        // FormatMessage failed and never set it. An error code with no system
        // message text drives that path.
        TEST_METHOD(GetMessageFromLastError_UnknownCode_ReturnsTextWithoutCrashing)
        {
            SetLastError(0x3FFFFFFF); // no system message for this code
            std::string message = GetMessageFromLastError("UnitTestOperation");
            Assert::IsTrue(message.find("UnitTestOperation") != std::string::npos, L"the caller's details must be in the message");
            Assert::IsTrue(message.find("failed with error") != std::string::npos);
            Assert::IsTrue(message.find(std::to_string(0x3FFFFFFF)) != std::string::npos, L"the error code must be in the message");
        }

        // The process-tree walk must terminate even when the snapshot's parent
        // chain is cyclic. PID reuse on a busy machine (a CI runner) can make a
        // process's recorded parent lead back around; the old two-node-only
        // cycle check missed longer cycles, so the walk looped and the kill
        // vector grew until operator new threw an out-of-memory exception
        // (observed on CI). CollectProcessTreeToKill now bounds the walk with a
        // visited set. A crafted cyclic map exercises that deterministically.
        TEST_METHOD(CollectProcessTreeToKill_CyclicParentChain_TerminatesAndFlagsCycle)
        {
            // 1 -> 2 -> 3 -> 1 is a three-node cycle (longer than the old check
            // handled), and it does NOT contain the target PID. This mirrors the
            // real crash: the process being killed is a leaf, while an unrelated
            // group of PIDs forms a parent-chain cycle elsewhere in the snapshot,
            // so the "reached the target's parent" check never breaks the walk --
            // only the cycle guard can. Every PID is non-zero.
            std::unordered_map<DWORD, DWORD> childToParent;
            childToParent[1] = 2;
            childToParent[2] = 3;
            childToParent[3] = 1;

            bool cycle = false;
            // Without the fix this call never returns (it loops until OOM); the
            // test therefore also proves the walk terminates.
            std::set<DWORD> killIds = CollectProcessTreeToKill(childToParent, 99, &cycle);
            Assert::IsTrue(cycle, L"a cyclic parent chain not containing the target must be detected and the walk must stop");
            // Nothing is a descendant of the (absent) target 99, so only 99 is in
            // the kill set.
            Assert::IsTrue(killIds.find(99) != killIds.end(), L"the target PID is always included");
            Assert::AreEqual((size_t)1, killIds.size(), L"no unrelated cyclic PID should be marked for killing");
        }

        // The normal (acyclic) case still collects the whole descendant tree.
        TEST_METHOD(CollectProcessTreeToKill_AcyclicTree_CollectsDescendants)
        {
            // Kill root 100. 200's parent is 100, 300's parent is 200 (a
            // grandchild), 400 belongs to an unrelated tree (parent 999).
            std::unordered_map<DWORD, DWORD> childToParent;
            childToParent[100] = 1;     // root's own parent is some other process
            childToParent[200] = 100;
            childToParent[300] = 200;
            childToParent[400] = 999;
            childToParent[999] = 1;

            bool cycle = true;
            std::set<DWORD> killIds = CollectProcessTreeToKill(childToParent, 100, &cycle);
            Assert::IsFalse(cycle, L"an acyclic map must not be flagged cyclic");
            Assert::IsTrue(killIds.find(100) != killIds.end(), L"the root must be killed");
            Assert::IsTrue(killIds.find(200) != killIds.end(), L"a direct child must be killed");
            Assert::IsTrue(killIds.find(300) != killIds.end(), L"a grandchild must be killed");
            Assert::IsTrue(killIds.find(400) == killIds.end(), L"an unrelated process must NOT be killed");
            Assert::IsTrue(killIds.find(999) == killIds.end(), L"an unrelated parent must NOT be killed");
        }

        // TerminateProcessTree leaked the CreateToolhelp32Snapshot handle on
        // every call. Kill a few short-lived children and check the process
        // handle count does not climb with each call.
        TEST_METHOD(TerminateProcessTree_DoesNotLeakTheSnapshotHandle)
        {
            // One un-measured warm-up call: the first CreateProcess / toolhelp use
            // can lazily open a few persistent handles that are not leaks.
            {
                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi = {};
                char cmd[] = "cmd.exe /c pause";
                if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                {
                    CloseHandle(pi.hThread);
                    TerminateProcessTree(pi.hProcess, 0);
                    WaitForSingleObject(pi.hProcess, 5000);
                    CloseHandle(pi.hProcess);
                }
            }

            const int iterations = 8;
            DWORD before = 0;
            Assert::IsTrue(!!GetProcessHandleCount(GetCurrentProcess(), &before));

            for (int i = 0; i < iterations; i++)
            {
                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi = {};
                char cmd[] = "cmd.exe /c pause";
                BOOL launched = CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
                Assert::IsTrue(!!launched, L"setup: cmd.exe must launch");
                CloseHandle(pi.hThread);

                TerminateProcessTree(pi.hProcess, 0);
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
            }

            DWORD after = 0;
            Assert::IsTrue(!!GetProcessHandleCount(GetCurrentProcess(), &after));
            // One leaked snapshot per call would add `iterations` handles. Allow
            // a small amount of unrelated churn.
            Assert::IsTrue(after < before + (DWORD)iterations,
                (std::wstring(L"handle count grew from ") + std::to_wstring(before) + L" to " + std::to_wstring(after)).c_str());
        }
    };
}
