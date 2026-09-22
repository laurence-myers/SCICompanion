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
#include "View.h"
#include "ResourceEntity.h"
#include "ResourceUtil.h"
#include "ResourceMap.h"
#include "AppState.h"
#include "ResourceContainer.h"
#include "Helper.h"
#include "format.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// This test enumerates all the resources of games in a folder. The games can't be checked in
// to source code, so it's up to you to place your legitimately-owned Sierra games here.
// Set the SCICOMP_GAMES_FOLDER environment variable to point at your own folder; the
// hard-coded path below is only a fallback (#76 B5).
const char SierraGameFolderFallback[] = "e:\\SierraGames\\";
// Each subfolder of this folder will be analyzed to find a resource.map.
static std::string GetSierraGameFolder()
{
    char buffer[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("SCICOMP_GAMES_FOLDER", buffer, ARRAYSIZE(buffer));
    if ((len > 0) && (len < ARRAYSIZE(buffer)))
    {
        std::string folder = buffer;
        if (!folder.empty() && (folder.back() != '\\') && (folder.back() != '/'))
        {
            folder += "\\";
        }
        return folder;
    }
    return SierraGameFolderFallback;
}

// We can't easily identify the game, so just identify type/number pairs that are known to fail.
// These are the known problems out of about 35 games from SCI0 to SCI1.1
std::pair<ResourceType, int> KnownFailures[]
{
    { ResourceType::View, 111 },        // LSL6
    { ResourceType::View, 140 },        // PQ2
    { ResourceType::View, 17 },         // SQ4
};

namespace UnitTests
{
    TEST_CLASS(TestAllGamesLoad)
    {
    public:
        TEST_CLASS_INITIALIZE(ClassSetup)
        {
            appState = nullptr;
        }

        TEST_CLASS_CLEANUP(ClassCleanup)
        {
            if (appState)
            {
                appState->ResetClassBrowser();
            }
            delete appState;
            appState = nullptr;
        }

        TEST_METHOD(TestAllGames)
        {
            const std::string gamesFolder = GetSierraGameFolder();
            std::string findString = gamesFolder;
            findString += "*";
            // Collect the file names
            std::vector<std::string> folders;
            WIN32_FIND_DATA findData = { 0 };
            HANDLE hFolder = FindFirstFile(findString.c_str(), &findData);
            if (hFolder != INVALID_HANDLE_VALUE)
            {
                BOOL ok = TRUE;
                while (ok)
                {
                    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        if (*findData.cFileName != '.')
                        {
                            folders.push_back(findData.cFileName);
                        }
                    }

                    ok = FindNextFile(hFolder, &findData);
                }
                FindClose(hFolder);
            }

            if (folders.empty())
            {
                Logger::WriteMessage(L"Found no games.");
            }

            for (auto folder : folders)
            {
                std::string finalPath = gamesFolder;
                finalPath += folder;
                _LoadAllResources(finalPath);
            }
        }

        // Loads every resource a container yields, with the shared robustness: skip a
        // header-stage failure and a zero-length placeholder, and report a
        // decompress/parse failure (unless it is a KnownFailure). Iterate manually so
        // the read stays inside a try (a range-for evaluates the iterator outside it).
        // Returns the number that loaded. (#182)
        int _LoadResourceContainer(ResourceContainer *container)
        {
            auto toWide = [](const std::string &s) { return std::wstring(s.begin(), s.end()); };
            int count = 0;
            for (auto it = container->begin(); it != container->end(); )
            {
                ResourceType type = it.GetResourceType();
                int number = it.GetResourceNumber();

                // Read the map entry but delay decompression, so a zero-length
                // placeholder is skipped by its header length, and a header-level
                // failure (a corrupt/stray map entry, e.g. KQ4 "view" 1049) is skipped
                // rather than failing -- as ScummVM does with a mismatched header.
                std::unique_ptr<ResourceBlob> blob;
                try
                {
                    blob = it.CreateButDelayDecompression();
                }
                catch (const std::exception &e)
                {
                    std::wstring skipMessage = fmt::format(L"Skipping unreadable resource {0} of type {1}: {2}",
                        number, (int)type, toWide(e.what()));
                    Logger::WriteMessage(skipMessage.c_str());
                }

                if (blob != nullptr)
                {
                    type = blob->GetType();
                    number = blob->GetNumber();
                    if (blob->GetLength() == 0)
                    {
                        std::wstring skipMessage = fmt::format(L"Skipping empty resource {0} of type {1}.", number, (int)type);
                        Logger::WriteMessage(skipMessage.c_str());
                    }
                    else
                    {
                        try
                        {
                            // Realize (decompress) and parse now, so a failure throws
                            // here inside the try and is reported with the resource id.
                            blob->EnsureRealized();
                            std::unique_ptr<ResourceEntity> resource = CreateResourceFromResourceData(*blob, false);
                            count++;
                        }
                        catch (const std::exception &e)
                        {
                            auto itFind = std::find_if(std::begin(KnownFailures), std::end(KnownFailures),
                                [&](const std::pair<ResourceType, int> &pair) { return pair.first == type && pair.second == number; }
                                );

                            if (itFind == std::end(KnownFailures))
                            {
                                std::wstring message = fmt::format(L"Unexpected: failed to load resource {0} of type {1}: {2}",
                                    number, (int)type, toWide(e.what()));
                                Assert::IsTrue(false, message.c_str());
                            }
                        }
                    }
                }

                try
                {
                    ++it;
                }
                catch (const std::exception &e)
                {
                    std::wstring message = fmt::format(L"Failed to advance the resource iterator after resource {0} of type {1}: {2}",
                        number, (int)type, toWide(e.what()));
                    Assert::IsTrue(false, message.c_str());
                    break;
                }
            }
            return count;
        }

        void _LoadAllResources(const std::string &gameFolder)
        {
            char szPath[MAX_PATH];
            GetCurrentDirectory(MAX_PATH, szPath);

            std::wstring message = fmt::format(L"Loading game in {0}.", gameFolder);
            Logger::WriteMessage(message.c_str());

            auto toWide = [](const std::string &s) { return std::wstring(s.begin(), s.end()); };

            appState = new AppState(nullptr);
            try
            {
                appState->GetResourceMap().SetGameFolder(gameFolder);
            }
            catch (CException *pEx)
            {
                // A failed map open is signalled to the caller with
                // AfxThrowUserException (a CUserException), which carries no text.
                // Without this catch it escaped as a bare "Unhandled C++ Exception"
                // that named no game. Name the folder and the likely reasons
                // instead. (#182)
                pEx->Delete();
                std::wstring message = fmt::format(L"Failed to open the game in {0}. Its resource map could not be read (missing, corrupt, or an unrecognised SCI version).", toWide(gameFolder));
                Assert::IsTrue(false, message.c_str());
            }
            Assert::IsTrue(appState->GetResourceMap().IsGameLoaded());

            // Normally ResourceMap uses the module filename for this. But unit tests are run from another exe.
            std::string exeFolder = szPath;
            exeFolder += "\\";
            appState->GetResourceMap().SetIncludeFolderForTest(exeFolder);

            ResourceTypeFlags flags = ResourceTypeFlags::AllCreatable;
            flags &= ~ResourceTypeFlags::Sound;     // Leave sounds out for now, we still don't load SCI10 sounds properly.
            flags &= ~ResourceTypeFlags::Vocab;     // Vocabs can't just be "created", we need to follow more specific logic. TODO
            auto container = appState->GetResourceMap().Resources(flags, ResourceEnumFlags::None | ResourceEnumFlags::AddInDefaultEnumFlags);
            int count = _LoadResourceContainer(container.get());

            // CD talkie games keep their speech in per-room message-audio maps. A
            // bulk enumeration yields only the main audio map (the AudioResourceSource
            // filters to the map context, rewriting -1 to the version's main map
            // number), so load each room's speech through its own map context. Freddy
            // Pharkas, for example, keeps ~47 room maps plus RESOURCE.AUD in an AUDIO
            // subfolder. (#182)
            const int mainAudioMapNumber = appState->GetResourceMap().Helper().Version.AudioMapResourceNumber;
            std::vector<int> audioMapNumbers;
            {
                auto mapContainer = appState->GetResourceMap().Resources(ResourceTypeFlags::AudioMap, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::AddInDefaultEnumFlags);
                for (auto it = mapContainer->begin(); it != mapContainer->end(); )
                {
                    audioMapNumbers.push_back(it.GetResourceNumber());
                    try { ++it; } catch (...) { break; }
                }
            }
            for (int audioMapNumber : audioMapNumbers)
            {
                // The main audio map was already loaded by the bulk enumeration above
                // (its Audio source used map context -1, which resolves to this map);
                // skip it here so its speech is not loaded and counted twice.
                if (audioMapNumber == mainAudioMapNumber)
                {
                    continue;
                }
                auto speechContainer = appState->GetResourceMap().Resources(ResourceTypeFlags::Audio, ResourceEnumFlags::MostRecentOnly | ResourceEnumFlags::AddInDefaultEnumFlags, audioMapNumber);
                count += _LoadResourceContainer(speechContainer.get());
            }

            message = fmt::format(L"Loaded {0} resources.", count);
            Logger::WriteMessage(message.c_str());

            // A game the tool opened must yield at least one loadable resource. Since
            // header-stage and zero-length failures are skipped (not reported), this
            // floor is what still catches a wholesale load regression -- e.g. a broken
            // version detection that makes every resource skip -- which would otherwise
            // leave the sweep green with count 0. (#182)
            Assert::IsTrue(count > 0, fmt::format(L"No resources loaded from {0}.", toWide(gameFolder)).c_str());

            appState->ResetClassBrowser();
            delete appState;
            appState = nullptr;
        }

    };
}