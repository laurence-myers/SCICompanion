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
#include "Task.h"
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // BackgroundScheduler runs each task on a worker thread. This class spawns
    // that thread, so it is an integration test (the name carries "Integration",
    // which is how RunTests.ps1 keeps it out of the fast unit leg).
    TEST_CLASS(TestBackgroundSchedulerIntegration)
    {
    public:
        // A task that throws must not take down the worker thread. Before the fix,
        // the exception escaped the worker's thread function -- an unconditional
        // std::terminate that killed the whole test run. After the fix the throw is
        // caught and the next task still runs.
        BEGIN_TEST_METHOD_ATTRIBUTE(Scheduler_ThrowingTask_WorkerSurvivesAndRunsNext)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(Scheduler_ThrowingTask_WorkerSurvivesAndRunsNext)
        {
            BackgroundScheduler<int> scheduler; // default: no response window

            // First task throws.
            scheduler.SubmitTask(std::make_unique<int>(1),
                [](ITaskStatus &, int &) -> std::unique_ptr<int>
                {
                    throw std::runtime_error("boom");
                });

            // Second task signals a promise. It runs only if the worker survived.
            // The promise is heap-owned and captured by value, so it outlives the
            // test frame even on the (pre-fix) path where the worker never gets here.
            auto ran = std::make_shared<std::promise<int>>();
            std::future<int> future = ran->get_future();
            scheduler.SubmitTask(std::make_unique<int>(42),
                [ran](ITaskStatus &, int &payload) -> std::unique_ptr<int>
                {
                    ran->set_value(payload);
                    return nullptr;
                });

            std::future_status status = future.wait_for(std::chrono::seconds(5));
            Assert::IsTrue(status == std::future_status::ready,
                L"the scheduler must survive a throwing task and run the next one");
            Assert::AreEqual(42, future.get(), L"the following task ran with its payload");
        }
    };
}
