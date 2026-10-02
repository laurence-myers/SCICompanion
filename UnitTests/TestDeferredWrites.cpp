#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppSession.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "SoundUtil.h"
#include "Helper.h"
#include "Stream.h"
#include "TestSupport.h"
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // DeferResourceAppend batches resource writes into one rewrite of each
    // destination:
    //  - Commit returns the error of a failed write;
    //  - a nested batch keeps the outer queue: an inner Commit only closes
    //    the inner batch;
    //  - a failed write does not write the resource's name into game.ini;
    //  - the same resource queued twice gives one map entry, and the last
    //    copy wins;
    //  - an abandoned inner batch withdraws its resources from the outer
    //    queue, and a queued copy that it replaced comes back;
    //  - a failed save of the audio maps keeps the audio volumes.
    // Most tests run on a copy of each template game (SCI0, then SCI1.1),
    // because the two map formats have different writers.
    TEST_CLASS(TestDeferredWrites)
    {
        std::string _gameFolder;

        static std::vector<uint8_t> TextBytes(const std::string &text)
        {
            std::vector<uint8_t> bytes(text.begin(), text.end());
            bytes.push_back(0);
            return bytes;
        }

        static ResourceBlob MakeText(const GameFolderHelper &helper, int number, const std::string &text, const char *name = nullptr, ResourceSourceFlags sourceFlags = ResourceSourceFlags::ResourceMap)
        {
            return ResourceBlob(helper, name, ResourceType::Text, TextBytes(text), helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, sourceFlags);
        }

        static std::string ReadText(CResourceMap &rm, int number)
        {
            std::unique_ptr<ResourceBlob> blob = rm.MostRecentResource(ResourceType::Text, number, false);
            if (!blob)
            {
                return "(missing)";
            }
            sci::istream stream = blob->GetReadStream();
            std::string text(reinterpret_cast<const char *>(stream.GetInternalPointer()), stream.getBytesRemaining());
            while (!text.empty() && text.back() == '\0')
            {
                text.pop_back();
            }
            return text;
        }

        static int CountMapEntries(CResourceMap &rm, int number)
        {
            int count = 0;
            auto container = rm.Resources(ResourceTypeFlags::Text, ResourceEnumFlags::ExcludePatchFiles);
            for (auto it = container->begin(); it != container->end(); ++it)
            {
                if (it.GetResourceNumber() == number)
                {
                    count++;
                }
            }
            return count;
        }

        static bool FileExists(const std::string &path)
        {
            return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        }

        std::string MapPath() const { return _gameFolder + "\\resource.map"; }

        std::string VolumePath(const GameFolderHelper &helper) const
        {
            char name[32];
            sprintf_s(name, "\\resource.%03d", helper.Version.DefaultVolumeFile);
            return _gameFolder + name;
        }

        void CleanUpGameCopy()
        {
            if (!_gameFolder.empty())
            {
                // RemoveFolder also deletes the read-only files of a test.
                CleanUpExistingGame();
                RemoveFolder(_gameFolder);
                _gameFolder.clear();
            }
        }

        // Runs the body on a copy of the SCI0 template, then on a copy of the
        // SCI1.1 template. The test log names the template of a failure.
        template<typename TBody>
        void OnEachTemplate(TBody body)
        {
            for (bool sci11 : { false, true })
            {
                Logger::WriteMessage(sci11 ? "template: SCI1.1\n" : "template: SCI0\n");
                _gameFolder = sci11 ? SetUpGameSCI11() : SetUpGameSCI0();
                body(AppResourceMap());
                CleanUpGameCopy();
            }
        }

        // Opens a copy of the SCI1.1 template, which has resource.aud and
        // resource.sfx, and gives the paths of the two. Each gets a marker
        // after its last resource: a rebuilt volume does not have it.
        std::pair<std::string, std::string> SetUpAudioVolumes()
        {
            _gameFolder = SetUpGameSCI11();
            std::string aud = GetAudioVolumePath(_gameFolder, false, AudioVolumeName::Aud);
            std::string sfx = GetAudioVolumePath(_gameFolder, false, AudioVolumeName::Sfx);
            for (const std::string &volume : { aud, sfx })
            {
                std::ofstream file(volume, std::ios::binary | std::ios::app);
                file << "KEEP-THIS-VOLUME";
            }
            return { aud, sfx };
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            // Also runs after a failed assertion.
            CleanUpGameCopy();
        }

        TEST_METHOD(NestedBatch_WritesTheOuterQueue)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                const GameFolderHelper &helper = rm.Helper();
                {
                    DeferResourceAppend outer(rm);
                    AssertOk(rm.WriteResource(MakeText(helper, 901, "outer")));
                    {
                        DeferResourceAppend inner(rm);
                        AssertOk(rm.WriteResource(MakeText(helper, 902, "inner")));
                        AssertOk(inner.Commit(), "an inner commit only closes the inner batch");
                    }
                    Assert::AreEqual(size_t(2), outer.Pending().size(), L"the inner batch must not discard the outer queue");
                    AssertOk(outer.Commit());
                }
                Assert::AreEqual(std::string("outer"), ReadText(rm, 901));
                Assert::AreEqual(std::string("inner"), ReadText(rm, 902));
            });
        }

        TEST_METHOD(AbandonedInnerBatch_WithdrawsItsWrites)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                const GameFolderHelper &helper = rm.Helper();
                {
                    DeferResourceAppend outer(rm);
                    AssertOk(rm.WriteResource(MakeText(helper, 901, "outer")));
                    {
                        DeferResourceAppend inner(rm);
                        AssertOk(rm.WriteResource(MakeText(helper, 907, "inner")));
                        AssertOk(rm.WriteResource(MakeText(helper, 901, "inner-replaced")));
                        // No Commit: the inner batch is abandoned.
                    }
                    Assert::AreEqual(size_t(1), outer.Pending().size(), L"the abandoned inner batch must withdraw what it queued");
                    AssertOk(outer.Commit());
                }
                Assert::AreEqual(std::string("outer"), ReadText(rm, 901), L"the copy that the abandoned batch replaced must come back");
                Assert::AreEqual(std::string("(missing)"), ReadText(rm, 907));
            });
        }

        TEST_METHOD(AbandonedMiddleBatch_WithdrawsACommittedInnerBatch)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                const GameFolderHelper &helper = rm.Helper();
                {
                    DeferResourceAppend outer(rm);
                    AssertOk(rm.WriteResource(MakeText(helper, 901, "outer")));
                    {
                        DeferResourceAppend middle(rm);
                        {
                            DeferResourceAppend inner(rm);
                            AssertOk(rm.WriteResource(MakeText(helper, 901, "inner")));
                            AssertOk(rm.WriteResource(MakeText(helper, 908, "inner")));
                            AssertOk(inner.Commit());
                        }
                        // No Commit: the middle batch is abandoned, with what
                        // the inner batch gave it.
                    }
                    Assert::AreEqual(size_t(1), outer.Pending().size());
                    AssertOk(outer.Commit());
                }
                Assert::AreEqual(std::string("outer"), ReadText(rm, 901), L"the outer copy must come back");
                Assert::AreEqual(std::string("(missing)"), ReadText(rm, 908));
            });
        }

        TEST_METHOD(AbandonedBatch_WritesNothing)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                {
                    DeferResourceAppend batch(rm);
                    AssertOk(rm.WriteResource(MakeText(rm.Helper(), 906, "never")));
                    // No Commit.
                }
                Assert::AreEqual(std::string("(missing)"), ReadText(rm, 906));
            });
        }

        TEST_METHOD(Commit_FailedWrite_ReturnsIoAndNamesNothing)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                SetFileAttributesA(MapPath().c_str(), FILE_ATTRIBUTE_READONLY);

                DeferResourceAppend batch(rm);
                AssertOk(rm.WriteResource(MakeText(rm.Helper(), 903, "blocked", "BlockedText")));
                sci::Status committed = batch.Commit();

                Assert::IsFalse(committed.has_value(), L"a commit that cannot write the map must fail");
                Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(committed.error().code)));
                std::string text = committed.error().ToString();
                Assert::IsTrue(text.find("resource.map") != std::string::npos, Wide(text).c_str());
                Assert::AreEqual(std::string(""), rm.Helper().GetIniString("Text", "n903"), L"a failed write must not name the resource in game.ini");
            });
        }

        TEST_METHOD(Commit_ReadOnlyVolume_ReturnsIoAndKeepsTheGame)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                const GameFolderHelper &helper = rm.Helper();
                std::vector<uint8_t> mapBefore = ReadFileBytes(MapPath());
                std::vector<uint8_t> volumeBefore = ReadFileBytes(VolumePath(helper));
                Assert::IsFalse(volumeBefore.empty(), Wide(VolumePath(helper)).c_str());
                SetFileAttributesA(VolumePath(helper).c_str(), FILE_ATTRIBUTE_READONLY);

                DeferResourceAppend batch(rm);
                AssertOk(rm.WriteResource(MakeText(helper, 912, "blocked", "VolumeText")));
                sci::Status committed = batch.Commit();

                Assert::IsFalse(committed.has_value(), L"a commit that cannot write the volume must fail");
                Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(committed.error().code)));
                Assert::IsTrue(mapBefore == ReadFileBytes(MapPath()), L"the map must not change");
                Assert::IsTrue(volumeBefore == ReadFileBytes(VolumePath(helper)), L"the volume must not change");
                Assert::AreEqual(std::string(""), helper.GetIniString("Text", "n912"));
            });
        }

        TEST_METHOD(Commit_OneDestinationFails_NamesOnlyTheOtherOne)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                const GameFolderHelper &helper = rm.Helper();
                SetFileAttributesA(MapPath().c_str(), FILE_ATTRIBUTE_READONLY);

                DeferResourceAppend batch(rm);
                AssertOk(rm.WriteResource(MakeText(helper, 910, "patch", "PatchText", ResourceSourceFlags::PatchFile)));
                AssertOk(rm.WriteResource(MakeText(helper, 911, "package", "PackageText")));
                sci::Status committed = batch.Commit();

                Assert::IsFalse(committed.has_value(), L"the package write fails");
                Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(committed.error().code)));
                Assert::AreEqual(std::string("patch"), ReadText(rm, 910), L"the patch file destination is still written");
                Assert::AreEqual(std::string("PatchText"), helper.GetIniString("Text", "n910"));
                Assert::AreEqual(std::string(""), helper.GetIniString("Text", "n911"));
            });
        }

        TEST_METHOD(SameResourceQueuedTwice_LastCopyWins)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                {
                    DeferResourceAppend batch(rm);
                    AssertOk(rm.WriteResource(MakeText(rm.Helper(), 904, "first")));
                    AssertOk(rm.WriteResource(MakeText(rm.Helper(), 904, "second")));
                    Assert::AreEqual(size_t(1), batch.Pending().size());
                    AssertOk(batch.Commit());
                }
                Assert::AreEqual(std::string("second"), ReadText(rm, 904));
                Assert::AreEqual(1, CountMapEntries(rm, 904));
            });
        }

        TEST_METHOD(WriteResource_WithoutBatch_ReturnsTheError)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                SetFileAttributesA(MapPath().c_str(), FILE_ATTRIBUTE_READONLY);
                sci::Status failed = rm.WriteResource(MakeText(rm.Helper(), 905, "blocked", "DirectText"));
                Assert::IsFalse(failed.has_value());
                Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(failed.error().code)));
                Assert::AreEqual(std::string(""), rm.Helper().GetIniString("Text", "n905"));

                SetFileAttributesA(MapPath().c_str(), FILE_ATTRIBUTE_NORMAL);
                AssertOk(rm.WriteResource(MakeText(rm.Helper(), 905, "written", "DirectText")));
                Assert::AreEqual(std::string("written"), ReadText(rm, 905));
                Assert::AreEqual(std::string("DirectText"), rm.Helper().GetIniString("Text", "n905"));
            });
        }

        TEST_METHOD(RepackageAudio_FailedMapSave_KeepsTheAudioVolumes)
        {
            auto [aud, sfx] = SetUpAudioVolumes();
            CResourceMap &rm = AppResourceMap();
            std::vector<uint8_t> audBefore = ReadFileBytes(aud);
            std::vector<uint8_t> sfxBefore = ReadFileBytes(sfx);
            std::vector<uint8_t> mapBefore = ReadFileBytes(MapPath());
            std::string upToDate = _gameFolder + "\\audiocache\\uptodate.bin";
            bool upToDateExisted = FileExists(upToDate);
            std::vector<uint8_t> upToDateBefore = ReadFileBytes(upToDate);
            SetFileAttributesA(MapPath().c_str(), FILE_ATTRIBUTE_READONLY);

            rm.RepackageAudio(true);

            Assert::IsTrue(audBefore == ReadFileBytes(aud), L"the audio maps were not saved, so resource.aud must not change");
            Assert::IsTrue(sfxBefore == ReadFileBytes(sfx), L"the audio maps were not saved, so resource.sfx must not change");
            Assert::IsFalse(FileExists(GetAudioVolumePath(_gameFolder, true, AudioVolumeName::Aud)), L"the new resource.aud must be deleted");
            Assert::IsFalse(FileExists(GetAudioVolumePath(_gameFolder, true, AudioVolumeName::Sfx)), L"the new resource.sfx must be deleted");
            Assert::IsTrue(mapBefore == ReadFileBytes(MapPath()), L"resource.map must not change");
            Assert::AreEqual(upToDateExisted, FileExists(upToDate));
            Assert::IsTrue(upToDateBefore == ReadFileBytes(upToDate), L"the cache must stay out of date, so the next repackage tries again");
        }

        TEST_METHOD(RepackageAudio_InsideABatch_IsRefused)
        {
            // In a batch, the audio maps would only be queued while the
            // volumes are replaced at once.
            auto [aud, sfx] = SetUpAudioVolumes();
            CResourceMap &rm = AppResourceMap();
            std::vector<uint8_t> audBefore = ReadFileBytes(aud);
            std::vector<uint8_t> sfxBefore = ReadFileBytes(sfx);
            std::vector<uint8_t> mapBefore = ReadFileBytes(MapPath());

            {
                DeferResourceAppend batch(rm);
                rm.RepackageAudio(true);
                Assert::AreEqual(size_t(0), batch.Pending().size(), L"no audio map may be queued");
                // No Commit: the batch is abandoned.
            }

            Assert::IsTrue(audBefore == ReadFileBytes(aud), L"resource.aud must not change");
            Assert::IsTrue(sfxBefore == ReadFileBytes(sfx), L"resource.sfx must not change");
            Assert::IsTrue(mapBefore == ReadFileBytes(MapPath()), L"resource.map must not change");
        }
    };
}
