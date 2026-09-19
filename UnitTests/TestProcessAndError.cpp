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
#include <string>

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
