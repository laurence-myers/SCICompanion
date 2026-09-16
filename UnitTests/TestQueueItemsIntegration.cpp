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
#include "IntegrationHarness.h"
#include "QueueItems.h"
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    namespace
    {
        struct QiTestItem
        {
            int value;
            bool poison;
        };

        struct QiTestResult
        {
            int value = 0;

            // Mirrors the render workers: a corrupt item makes the parse throw.
            static QiTestResult *CreateFromWorkItem(QiTestItem *item)
            {
                if (item->poison)
                {
                    throw std::runtime_error("corrupt resource");
                }
                QiTestResult *result = new QiTestResult();
                result->value = item->value;
                return result;
            }
        };
    }

    // QueueItems runs CreateFromWorkItem on a detached worker thread. A corrupt
    // resource makes that throw; before the fix the exception escaped the thread
    // (an unconditional std::terminate that took down the app). The class name
    // carries "Integration" (it spawns a thread), so it runs only under
    // RunTests.ps1 -Integration.
    TEST_CLASS(TestQueueItemsIntegration)
    {
    public:
        BEGIN_TEST_METHOD_ATTRIBUTE(QueueItems_ThrowingWorkItem_WorkerSurvivesAndServesNext)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(QueueItems_ThrowingWorkItem_WorkerSurvivesAndServesNext)
        {
            IntegrationHarness::MessageOnlyWindow window; // receives the "result ready" post
            const UINT kResultReady = WM_USER + 11;
            auto queue = std::make_shared<QueueItems<QiTestItem, QiTestResult>>(window.Handle(), kResultReady);
            Assert::IsTrue(queue->Init(), L"the worker thread must start");

            // A poison item throws in CreateFromWorkItem; a good item follows it.
            queue->GiveWorkItem(std::unique_ptr<QiTestItem>(new QiTestItem{ 0, true }));
            queue->GiveWorkItem(std::unique_ptr<QiTestItem>(new QiTestItem{ 7, false }));

            // Poll for the good result within a deadline. If the poison item had
            // terminated the worker, the good result would never arrive (and before
            // the fix the whole test process would have aborted).
            QiTestResult *raw = nullptr;
            bool got = false;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (queue->TakeWorkResult(&raw))
                {
                    got = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            std::unique_ptr<QiTestResult> result(raw);

            Assert::IsTrue(got, L"the worker must survive the throwing item and produce the next result");
            Assert::AreEqual(7, result ? result->value : -1, L"the good item's result must come through");

            queue->Abort();
        }
    };
}
