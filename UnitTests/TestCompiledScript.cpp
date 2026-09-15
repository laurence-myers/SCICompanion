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
#include "CompiledScript.h"
#include "GameFolderHelper.h"
#include "Version.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    TEST_CLASS(TestCompiledScript)
    {
        // Feed crafted script/heap buffers through the istream Load overload, but
        // bound the wall-clock time. A parsing bug that spins forever then fails
        // the test cleanly instead of hanging (or exhausting memory) for the
        // whole run. Returns false if Load did not finish within the timeout.
        static bool LoadSCI11WithTimeout(const std::vector<uint8_t> &script,
            const std::vector<uint8_t> &heap,
            bool &loadResult,
            std::vector<CodeSection> &codeSectionsOut,
            uint32_t &rawByteCountOut)
        {
            auto done = std::make_shared<std::atomic<bool>>(false);
            auto result = std::make_shared<std::atomic<bool>>(false);
            auto rawCount = std::make_shared<std::atomic<uint32_t>>(0u);
            auto sections = std::make_shared<std::vector<CodeSection>>();
            // The worker owns copies of its buffers: if we abandon it on a
            // timeout it may still be reading them.
            auto scriptCopy = std::make_shared<std::vector<uint8_t>>(script);
            auto heapCopy = std::make_shared<std::vector<uint8_t>>(heap);

            std::thread worker([=]()
            {
                GameFolderHelper helper; // Unused: a non-null heapStream is supplied below.
                CompiledScript compiledScript(0);
                sci::istream scriptStream(scriptCopy->data(), (uint32_t)scriptCopy->size());
                sci::istream heapStream(heapCopy->data(), (uint32_t)heapCopy->size());
                bool r = compiledScript.Load(helper, sciVersion1_1, 0, scriptStream, &heapStream);
                *sections = compiledScript._codeSections;
                rawCount->store((uint32_t)compiledScript.GetRawBytes().size());
                result->store(r);
                done->store(true);
            });

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!done->load() && (std::chrono::steady_clock::now() < deadline))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            if (!done->load())
            {
                // Stuck in a parsing loop; abandon the worker (process teardown
                // reclaims it) and report the timeout.
                worker.detach();
                return false;
            }
            worker.join();
            loadResult = result->load();
            codeSectionsOut = *sections;
            rawByteCountOut = rawCount->load();
            return true;
        }

    public:
        // A heap whose string-pointer-offsets offset (its first word) is 0xffff
        // drives the SCI1.1 string reader into an unbounded loop on master: it
        // reads while tellg() < that offset, but istream::operator>>(string)
        // rewinds without advancing when it runs off the end, so the position
        // never reaches the offset. The load must terminate; and because the
        // script's declared code section lies outside the (tiny) resource, it
        // must report failure and expose no code section pointing past
        // GetRawBytes().
        TEST_METHOD(SCI11_BadHeapOffset_TerminatesAndFails)
        {
            // Script: word[0] = heapPointerListOffset = 0xffff (well past the
            // 8-byte resource), words[1..2] padding, word[3] = 0 exports.
            std::vector<uint8_t> script = { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            // Heap: word[0] = stringPointerOffsetsOffset = 0xffff, word[1] =
            // localsCount = 0, word[2] = string-section terminator = 0.
            std::vector<uint8_t> heap = { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 };

            bool loadResult = true;
            std::vector<CodeSection> codeSections;
            uint32_t rawByteCount = 0;
            bool finished = LoadSCI11WithTimeout(script, heap, loadResult, codeSections, rawByteCount);

            Assert::IsTrue(finished, L"CompiledScript::Load did not terminate (infinite loop on a bad heap offset).");
            Assert::IsFalse(loadResult, L"Load must reject a resource whose code section lies outside it.");

            for (const CodeSection &section : codeSections)
            {
                Assert::IsTrue(section.begin <= section.end, L"code section: begin past end");
                Assert::IsTrue((uint32_t)section.end <= rawByteCount, L"code section: end past GetRawBytes().size()");
            }
        }
    };
}
