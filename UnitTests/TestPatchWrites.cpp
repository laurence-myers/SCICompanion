#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceUtil.h"
#include "GameFolderHelper.h"
#include "Text.h"
#include "Helper.h"
#include "TestSupport.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // The patch writer and the audio cache writer give their errors:
    //  - a resource that cannot be written (for example one too big for the
    //    format) fails, the old patch file stays, and no .bak file is left;
    //  - a batch writes every patch file to a .bak file, and checks every
    //    target, before the first rename, so a failed write or a target that
    //    cannot be replaced leaves the old patch files (not a new .scr with
    //    an old .hep);
    //  - CheckResourceSize gives an error, not a message box, and quotes the
    //    limit of the game's format;
    //  - a failed save of the audio cache's audio map comes back as an
    //    error, and marks the cache out of date.
    TEST_CLASS(TestPatchWrites)
    {
        std::string _gameFolder;

        static ResourceBlob MakeText(const GameFolderHelper &helper, int number, const std::vector<uint8_t> &bytes)
        {
            return ResourceBlob(helper, nullptr, ResourceType::Text, bytes, helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
        }

        static std::vector<uint8_t> Bytes(const std::string &text)
        {
            std::vector<uint8_t> bytes(text.begin(), text.end());
            bytes.push_back(0);
            return bytes;
        }

        std::string PatchPath(int number) const
        {
            return _gameFolder + "\\" + GetFileNameFor(ResourceType::Text, number, NoBase36, appState->GetResourceMap().Helper().Version);
        }

        // The patch file's data, without its two-byte header.
        std::string ReadPatchText(int number) const
        {
            std::ifstream in(PatchPath(number), std::ios::binary);
            if (!in)
            {
                return "(missing)";
            }
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (bytes.size() < 2)
            {
                return "(truncated)";
            }
            std::string text = bytes.substr(2);
            while (!text.empty() && text.back() == '\0')
            {
                text.pop_back();
            }
            return text;
        }

        bool AnyBakFile() const
        {
            for (const auto &entry : std::filesystem::directory_iterator(_gameFolder))
            {
                if (entry.path().extension() == ".bak")
                {
                    return true;
                }
            }
            return false;
        }

    public:
        TEST_METHOD_INITIALIZE(Setup)
        {
            _gameFolder = SetUpGameSCI0();
        }

        TEST_METHOD_CLEANUP(CleanUp)
        {
            if (!_gameFolder.empty())
            {
                // RemoveFolder also deletes the read-only files of a test.
                CleanUpExistingGame();
                RemoveFolder(_gameFolder);
                _gameFolder.clear();
            }
        }

        TEST_METHOD(WriteEntity_Oversize_ReturnsUnsupportedAndWritesNothing)
        {
            CResourceMap &rm = appState->GetResourceMap();
            std::unique_ptr<ResourceEntity> text(CreateTextResource(rm.GetSCIVersion()));
            text->GetComponent<TextComponent>().AddString(std::string(MaxResourceSize + 10, 'x'));
            text->ResourceNumber = 913;
            text->SourceFlags = ResourceSourceFlags::PatchFile;

            // The entity path gives an error here, not a message box, which
            // would block a run with no GUI.
            sci::Status failed = rm.WriteResource(*text);

            Assert::IsFalse(failed.has_value());
            Assert::AreEqual(std::string("unsupported"), std::string(sci::ErrorCodeName(failed.error().code)));
            std::string message = failed.error().ToString();
            Assert::IsTrue(message.find(std::to_string(MaxResourceSize)) != std::string::npos, Wide(message).c_str());
            Assert::IsTrue(message.find("Text 913") != std::string::npos, L"the error must name the resource");
            Assert::AreEqual(std::string("(missing)"), ReadPatchText(913));
        }

        TEST_METHOD(ReadOnlyTarget_ReplacesNoPatchFile)
        {
            CResourceMap &rm = appState->GetResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            AssertOk(rm.WriteResource(MakeText(helper, 920, Bytes("old920"))));
            AssertOk(rm.WriteResource(MakeText(helper, 921, Bytes("old921"))));
            SetFileAttributesA(PatchPath(921).c_str(), FILE_ATTRIBUTE_READONLY);

            sci::Status committed = sci::Ok();
            {
                DeferResourceAppend batch(rm);
                AssertOk(rm.WriteResource(MakeText(helper, 920, Bytes("new920"))));
                AssertOk(rm.WriteResource(MakeText(helper, 921, Bytes("new921"))));
                committed = batch.Commit();
            }

            Assert::IsFalse(committed.has_value(), L"a read-only target must stop the batch");
            Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(committed.error().code)));
            std::string message = committed.error().ToString();
            std::string name = GetFileNameFor(ResourceType::Text, 921, NoBase36, helper.Version);
            Assert::IsTrue(message.find(name) != std::string::npos, Wide(message).c_str());
            Assert::AreEqual(std::string("old920"), ReadPatchText(920), L"no patch file of the batch may be replaced");
            Assert::AreEqual(std::string("old921"), ReadPatchText(921));
            Assert::IsFalse(AnyBakFile(), L"no .bak file may be left behind");
        }

        TEST_METHOD(AudioCacheWrite_FailedMapSave_MarksTheCacheOutOfDate)
        {
            // Audio needs an SCI1.1 game.
            CleanUpGame(_gameFolder);
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = appState->GetResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            int mapNumber = helper.Version.AudioMapResourceNumber;
            std::string upToDatePath = _gameFolder + "\\audiocache\\uptodate.bin";
            auto upToDateMaps = [&]()
            {
                std::set<int> maps;
                std::ifstream file(upToDatePath, std::ios::binary);
                int number;
                while (file.read(reinterpret_cast<char*>(&number), sizeof(number)))
                {
                    maps.insert(number);
                }
                return maps;
            };
            ResourceBlob audio(helper, nullptr, ResourceType::Audio, std::vector<uint8_t>(64, 0x80), 0, 5, NoBase36, helper.Version, ResourceSourceFlags::AudioCache);
            AssertOk(rm.WriteResource(audio));
            rm.RepackageAudio(true);
            Assert::AreEqual(size_t(1), upToDateMaps().count(mapNumber), L"the repackage marks the cache map up to date");

            std::string cacheMap = _gameFolder + "\\audiocache\\" + GetFileNameFor(ResourceType::AudioMap, mapNumber, NoBase36, helper.Version);
            SetFileAttributesA(cacheMap.c_str(), FILE_ATTRIBUTE_READONLY);
            Assert::IsFalse(rm.WriteResource(audio).has_value());

            // The new audio file is in the cache, so the next repackage must
            // rebuild from it.
            Assert::AreEqual(size_t(0), upToDateMaps().count(mapNumber), L"after the failed save, the cache map must be out of date");
        }

        TEST_METHOD(AudioCacheWrite_FailedMapSave_ReturnsTheError)
        {
            // Audio needs an SCI1.1 game.
            CleanUpGame(_gameFolder);
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = appState->GetResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            ResourceBlob audio(helper, nullptr, ResourceType::Audio, std::vector<uint8_t>(64, 0x80), 0, 5, NoBase36, helper.Version, ResourceSourceFlags::AudioCache);

            // The first write makes the audio cache and its audio map.
            AssertOk(rm.WriteResource(audio));
            std::string cacheMap = _gameFolder + "\\audiocache\\" + GetFileNameFor(ResourceType::AudioMap, helper.Version.AudioMapResourceNumber, NoBase36, helper.Version);
            Assert::IsTrue(std::filesystem::exists(cacheMap), Wide(cacheMap).c_str());

            SetFileAttributesA(cacheMap.c_str(), FILE_ATTRIBUTE_READONLY);
            sci::Status failed = rm.WriteResource(audio);

            Assert::IsFalse(failed.has_value(), L"a failed save of the cache's audio map must come back");
            Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(failed.error().code)));
        }

        TEST_METHOD(OversizeResource_KeepsTheOldPatchFile)
        {
            CResourceMap &rm = appState->GetResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            AssertOk(rm.WriteResource(MakeText(helper, 907, Bytes("original"))));
            Assert::AreEqual(std::string("original"), ReadPatchText(907));

            std::vector<uint8_t> tooBig(MaxResourceSize + 1, 'x');
            sci::Status failed = rm.WriteResource(MakeText(helper, 907, tooBig));

            Assert::IsFalse(failed.has_value(), L"a resource too big for the format must not be written");
            Assert::AreEqual(std::string("unsupported"), std::string(sci::ErrorCodeName(failed.error().code)));
            Assert::AreEqual(std::string("original"), ReadPatchText(907), L"the old patch file must stay");
            Assert::IsFalse(AnyBakFile(), L"no .bak file may be left behind");
        }

        TEST_METHOD(FailedResourceInBatch_KeepsTheOtherPatchFiles)
        {
            CResourceMap &rm = appState->GetResourceMap();
            const GameFolderHelper &helper = rm.Helper();
            AssertOk(rm.WriteResource(MakeText(helper, 908, Bytes("old"))));

            sci::Status committed = sci::Ok();
            {
                DeferResourceAppend batch(rm);
                AssertOk(rm.WriteResource(MakeText(helper, 908, Bytes("new"))));
                AssertOk(rm.WriteResource(MakeText(helper, 909, std::vector<uint8_t>(MaxResourceSize + 1, 'x'))));
                committed = batch.Commit();
            }

            Assert::IsFalse(committed.has_value());
            Assert::AreEqual(std::string("old"), ReadPatchText(908), L"a batch that fails must not replace any of its patch files");
            Assert::AreEqual(std::string("(missing)"), ReadPatchText(909));
            Assert::IsFalse(AnyBakFile(), L"no .bak file may be left behind");
        }

        TEST_METHOD(CheckResourceSize_QuotesTheLimitOfTheFormat)
        {
            SCIVersion sci0 = sciVersion0;
            sci::Status tooBigForSci0 = CheckResourceSize(sci0, MaxResourceSize + 1, ResourceType::Text);
            Assert::IsFalse(tooBigForSci0.has_value());
            Assert::AreEqual(std::string("unsupported"), std::string(sci::ErrorCodeName(tooBigForSci0.error().code)));
            Assert::IsTrue(tooBigForSci0.error().message.find(std::to_string(MaxResourceSize)) != std::string::npos);
            AssertOk(CheckResourceSize(sci0, MaxResourceSize, ResourceType::Text));

            SCIVersion sci11 = sciVersion1_1;
            AssertOk(CheckResourceSize(sci11, MaxResourceSize + 1, ResourceType::Text), "SCI1.1 maps allow larger resources");
            sci::Status tooBigForSci11 = CheckResourceSize(sci11, MaxResourceSizeLarge + 1, ResourceType::Text);
            Assert::IsFalse(tooBigForSci11.has_value());
            Assert::IsTrue(tooBigForSci11.error().message.find(std::to_string(MaxResourceSizeLarge)) != std::string::npos, L"the message must quote the limit of this format");
        }
    };
}
