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
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

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

        // #47 is a lock-ordering deadlock in SCIClassBrowser::OnOpenGame: it held
        // _mutexClassBrowser and then joined the reload worker, which needs that
        // same mutex. This reproduces the general hazard on the real scheduler:
        // Exit() (which joins the worker) called while the caller holds a lock the
        // running task needs deadlocks. The OnOpenGame fix is to Exit() before
        // taking the mutex. OnOpenGame itself needs a loaded game to drive, so its
        // reorder is verified by inspection; this test guards the mechanism.
        BEGIN_TEST_METHOD_ATTRIBUTE(Scheduler_ExitWhileHoldingTaskLock_Deadlocks)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(Scheduler_ExitWhileHoldingTaskLock_Deadlocks)
        {
            std::recursive_mutex sharedMutex; // stands in for _mutexClassBrowser
            BackgroundScheduler<int> scheduler;

            auto taskStarted = std::make_shared<std::promise<void>>();
            std::future<void> started = taskStarted->get_future();
            scheduler.SubmitTask(std::make_unique<int>(0),
                [&sharedMutex, taskStarted](ITaskStatus &, int &) -> std::unique_ptr<int>
                {
                    taskStarted->set_value();
                    // The reload takes the browser mutex; here it blocks because the
                    // "OnOpenGame" thread below holds it.
                    std::lock_guard<std::recursive_mutex> lock(sharedMutex);
                    return nullptr;
                });

            // Hold the mutex, as the buggy OnOpenGame did before joining the worker.
            std::unique_lock<std::recursive_mutex> held(sharedMutex);
            Assert::IsTrue(started.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                L"the scheduler must dispatch the task");

            // Exit() joins the worker, which is blocked on the mutex we hold. Run it
            // on our own thread so we can join it after breaking the deadlock (a
            // detached thread would touch the scheduler after it is destroyed).
            std::atomic<bool> exitReturned{ false };
            std::thread exitThread([&scheduler, &exitReturned]()
            {
                scheduler.Exit();
                exitReturned.store(true);
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            Assert::IsFalse(exitReturned.load(),
                L"Exit() while holding the lock the task needs must deadlock (the #47 hazard)");

            // Break the deadlock so the test can clean up: release the mutex; the
            // worker then finishes, Exit() returns, and we join our thread.
            held.unlock();
            exitThread.join();
            Assert::IsTrue(exitReturned.load(), L"releasing the lock lets Exit() return");
        }

        // #92: _hwndResponse/_msgResponse were touched under three different regimes
        // -- written under _mutex in SubmitTask(HWND,...), read under _mutexResponse
        // in _DoWork, and cleared under NO lock in DeactivateHWND -- a data race whose
        // worst case is a PostMessage to a destroyed window. The fix routes every
        // access through _mutexResponse. A data race cannot be reproduced
        // deterministically on MSVC (no ThreadSanitizer), so this test guards the
        // mechanism: it churns DeactivateHWND and the response-window setter from a
        // second thread while the worker keeps completing response-bearing tasks, and
        // asserts the scheduler stays live and shuts down cleanly (no hang, no crash,
        // no deadlock from the lock change). Under the ASan leg (#42) it also runs the
        // concurrent accesses through tooling.
        BEGIN_TEST_METHOD_ATTRIBUTE(Scheduler_ResponseHwndChurn_StaysLiveAndExits)
            TEST_METHOD_ATTRIBUTE(L"TestCategory", L"Integration")
        END_TEST_METHOD_ATTRIBUTE()
        TEST_METHOD(Scheduler_ResponseHwndChurn_StaysLiveAndExits)
        {
            // A non-null but bogus window handle: _DoWork takes the response path when
            // _hwndResponse is set, and PostMessage to an invalid handle just returns
            // FALSE (no window is created or destroyed here).
            HWND fakeHwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1));
            const UINT msg = 0x8000; // WM_APP

            auto completed = std::make_shared<std::atomic<int>>(0);
            {
                BackgroundScheduler<int, int> scheduler(fakeHwnd, msg);

                // Churn the response window from another thread: alternately clear it
                // (DeactivateHWND) and set it again (SubmitTask(HWND,...)), racing the
                // worker's reads in _DoWork.
                std::atomic<bool> stop{ false };
                std::thread churn([&scheduler, fakeHwnd, msg, &stop]()
                {
                    while (!stop.load())
                    {
                        scheduler.DeactivateHWND(fakeHwnd);
                        scheduler.SubmitTask(fakeHwnd, msg, std::make_unique<int>(0),
                            [](ITaskStatus &, int &) -> std::unique_ptr<int> { return nullptr; });
                    }
                });

                // Meanwhile submit many response-bearing tasks; each makes _DoWork read
                // _hwndResponse/_msgResponse under _mutexResponse.
                for (int i = 0; i < 2000; i++)
                {
                    scheduler.SubmitTask(std::make_unique<int>(i),
                        [completed](ITaskStatus &, int &payload) -> std::unique_ptr<int>
                        {
                            completed->fetch_add(1);
                            return std::make_unique<int>(payload);
                        });
                }

                // The worker must still be alive: a sentinel task signals a promise.
                auto ran = std::make_shared<std::promise<int>>();
                std::future<int> future = ran->get_future();
                scheduler.SubmitTask(std::make_unique<int>(7),
                    [ran](ITaskStatus &, int &payload) -> std::unique_ptr<int>
                    {
                        ran->set_value(payload);
                        return nullptr;
                    });

                Assert::IsTrue(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready,
                    L"the scheduler must stay live while the response window is churned");
                Assert::AreEqual(7, future.get(), L"the sentinel task ran");

                stop.store(true);
                churn.join();
                // scheduler destructor calls Exit(), which must return (no deadlock).
            }

            Assert::IsTrue(completed->load() > 0, L"response-bearing tasks ran");
        }
    };
}
