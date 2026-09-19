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
#include "ResourceBlob.h"
#include "GameFolderHelper.h"
#include "Helper.h"
#include "Stream.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // #68: MapAndPackageSource::AppendResources always writes the resource data
    // uncompressed, and it zeroed CompressionMethod on a local header copy --
    // but then passed blob->GetHeader() (the original) to the header writer.
    // A blob whose header still described a compressed source (method and a
    // shorter compressed length) was therefore written with a header that
    // claimed compression over plain data, and the next read tried to
    // decompress it. The writer now receives the local header, with the method
    // zeroed and the compressed length set to the decompressed length.
    TEST_CLASS(TestAppendHeader)
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

        TEST_METHOD(AppendBlobWithStaleCompressedHeader_ReadsBackUncompressed)
        {
            CResourceMap &rm = appState->GetResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            const int number = 900;

            std::vector<uint8_t> data;
            for (int i = 0; i < 64; i++)
            {
                data.push_back((uint8_t)(0x41 + (i % 26)));
            }
            data.push_back(0);

            ResourceBlob blob(helper, nullptr, ResourceType::Text, data, helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
            Assert::AreEqual(0, blob.GetEncoding(), L"setup: a blob built from bits is uncompressed");

            // Make the header look like one read from a compressed volume: a
            // non-zero method and a compressed length shorter than the data.
            // The data itself stays plain, exactly as GetReadStream returns it.
            blob.GetHeader().CompressionMethod = 1;
            blob.GetHeader().cbCompressed = blob.GetHeader().cbDecompressed / 2;

            HRESULT hr = rm.AppendResource(blob);
            Assert::IsTrue(SUCCEEDED(hr), L"AppendResource must succeed");

            std::unique_ptr<ResourceBlob> readBack = rm.MostRecentResource(ResourceType::Text, number, false);
            Assert::IsTrue(readBack != nullptr, L"the appended resource must be found");
            Assert::AreEqual(0, readBack->GetEncoding(), L"the written header must say uncompressed (method 0)");
            Assert::AreEqual((DWORD)data.size(), readBack->GetDecompressedLength());
            Assert::AreEqual((DWORD)data.size(), readBack->GetCompressedLength(), L"the written compressed length must equal the plain data length");

            sci::istream stream = readBack->GetReadStream();
            Assert::AreEqual((uint32_t)data.size(), stream.getBytesRemaining());
            Assert::IsTrue(0 == memcmp(stream.GetInternalPointer(), data.data(), data.size()), L"the data must round-trip byte for byte");
        }
    };
}
