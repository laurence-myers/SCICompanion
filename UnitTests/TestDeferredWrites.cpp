#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "SoundUtil.h"
#include "Helper.h"
#include "Stream.h"
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace UnitTests
{
    // DeferResourceAppend batches resource writes into one rewrite of each
    // destination. Before plan step A1:
    //  - Commit always returned S_OK, and a failed write went only to a
    //    message box (P3);
    //  - a nested batch lost the outer queue, because the inner destructor
    //    called AbandonAppend after its Commit (P4);
    //  - a failed write still wrote the resource's name into game.ini (P5);
    //  - the same resource queued twice gave two map entries, and on SCI0
    //    the older copy won (P6).
    // Before the fixes from the A1 review:
    //  - an abandoned inner batch left its resources in the outer queue, and
    //    a queued copy that it replaced did not come back;
    //  - a failed save of the audio maps still replaced the audio volumes.
    // Most tests run on a copy of each template game (SCI0, then SCI1.1),
    // because the two map formats have different writers.
    TEST_CLASS(TestDeferredWrites)
    {
        std::string _gameFolder;
        std::vector<std::string> _readOnlyFiles;

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

        static std::vector<char> ReadBytes(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        static bool FileExists(const std::string &path)
        {
            return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        }

        static std::wstring Wide(const std::string &text)
        {
            return std::wstring(text.begin(), text.end());
        }

        std::string MapPath() const { return _gameFolder + "\\resource.map"; }

        std::string VolumePath(const GameFolderHelper &helper) const
        {
            char name[32];
            sprintf_s(name, "\\resource.%03d", helper.Version.DefaultVolumeFile);
            return _gameFolder + name;
        }

        void MakeReadOnly(const std::string &path)
        {
            SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY);
            _readOnlyFiles.push_back(path);
        }

        void CleanUpGameCopy()
        {
            for (const std::string &path : _readOnlyFiles)
            {
                SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            }
            _readOnlyFiles.clear();
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
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
                body(appState->GetResourceMap());
                CleanUpGameCopy();
            }
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
                    Assert::IsTrue(rm.WriteResource(MakeText(helper, 901, "outer")).has_value());
                    {
                        DeferResourceAppend inner(rm);
                        Assert::IsTrue(rm.WriteResource(MakeText(helper, 902, "inner")).has_value());
                        Assert::IsTrue(inner.Commit().has_value(), L"an inner commit only closes the inner batch");
                    }
                    Assert::AreEqual(size_t(2), outer.Pending().size(), L"the inner batch must not discard the outer queue");
                    Assert::IsTrue(outer.Commit().has_value());
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
                    Assert::IsTrue(rm.WriteResource(MakeText(helper, 901, "outer")).has_value());
                    {
                        DeferResourceAppend inner(rm);
                        Assert::IsTrue(rm.WriteResource(MakeText(helper, 907, "inner")).has_value());
                        Assert::IsTrue(rm.WriteResource(MakeText(helper, 901, "inner-replaced")).has_value());
                        // No Commit: the inner batch is abandoned.
                    }
                    Assert::AreEqual(size_t(1), outer.Pending().size(), L"the abandoned inner batch must withdraw what it queued");
                    Assert::IsTrue(outer.Commit().has_value());
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
                    Assert::IsTrue(rm.WriteResource(MakeText(helper, 901, "outer")).has_value());
                    {
                        DeferResourceAppend middle(rm);
                        {
                            DeferResourceAppend inner(rm);
                            Assert::IsTrue(rm.WriteResource(MakeText(helper, 901, "inner")).has_value());
                            Assert::IsTrue(rm.WriteResource(MakeText(helper, 908, "inner")).has_value());
                            Assert::IsTrue(inner.Commit().has_value());
                        }
                        // No Commit: the middle batch is abandoned, with what
                        // the inner batch gave it.
                    }
                    Assert::AreEqual(size_t(1), outer.Pending().size());
                    Assert::IsTrue(outer.Commit().has_value());
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
                    Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 906, "never")).has_value());
                    // No Commit.
                }
                Assert::AreEqual(std::string("(missing)"), ReadText(rm, 906));
            });
        }

        TEST_METHOD(Commit_FailedWrite_ReturnsIoAndNamesNothing)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                MakeReadOnly(MapPath());

                DeferResourceAppend batch(rm);
                Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 903, "blocked", "BlockedText")).has_value());
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
                std::vector<char> mapBefore = ReadBytes(MapPath());
                std::vector<char> volumeBefore = ReadBytes(VolumePath(helper));
                Assert::IsFalse(volumeBefore.empty(), Wide(VolumePath(helper)).c_str());
                MakeReadOnly(VolumePath(helper));

                DeferResourceAppend batch(rm);
                Assert::IsTrue(rm.WriteResource(MakeText(helper, 912, "blocked", "VolumeText")).has_value());
                sci::Status committed = batch.Commit();

                Assert::IsFalse(committed.has_value(), L"a commit that cannot write the volume must fail");
                Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(committed.error().code)));
                Assert::IsTrue(mapBefore == ReadBytes(MapPath()), L"the map must not change");
                Assert::IsTrue(volumeBefore == ReadBytes(VolumePath(helper)), L"the volume must not change");
                Assert::AreEqual(std::string(""), helper.GetIniString("Text", "n912"));
            });
        }

        TEST_METHOD(Commit_OneDestinationFails_NamesOnlyTheOtherOne)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                const GameFolderHelper &helper = rm.Helper();
                MakeReadOnly(MapPath());

                DeferResourceAppend batch(rm);
                Assert::IsTrue(rm.WriteResource(MakeText(helper, 910, "patch", "PatchText", ResourceSourceFlags::PatchFile)).has_value());
                Assert::IsTrue(rm.WriteResource(MakeText(helper, 911, "package", "PackageText")).has_value());
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
                    Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 904, "first")).has_value());
                    Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 904, "second")).has_value());
                    Assert::AreEqual(size_t(1), batch.Pending().size());
                    Assert::IsTrue(batch.Commit().has_value());
                }
                Assert::AreEqual(std::string("second"), ReadText(rm, 904));
                Assert::AreEqual(1, CountMapEntries(rm, 904));
            });
        }

        TEST_METHOD(WriteResource_WithoutBatch_ReturnsTheError)
        {
            OnEachTemplate([&](CResourceMap &rm)
            {
                MakeReadOnly(MapPath());
                sci::Status failed = rm.WriteResource(MakeText(rm.Helper(), 905, "blocked", "DirectText"));
                Assert::IsFalse(failed.has_value());
                Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(failed.error().code)));
                Assert::AreEqual(std::string(""), rm.Helper().GetIniString("Text", "n905"));

                SetFileAttributesA(MapPath().c_str(), FILE_ATTRIBUTE_NORMAL);
                Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 905, "written", "DirectText")).has_value());
                Assert::AreEqual(std::string("written"), ReadText(rm, 905));
                Assert::AreEqual(std::string("DirectText"), rm.Helper().GetIniString("Text", "n905"));
            });
        }

        TEST_METHOD(RepackageAudio_FailedMapSave_KeepsTheAudioVolumes)
        {
            // The SCI1.1 template has resource.aud and resource.sfx.
            _gameFolder = SetUpGameSCI11();
            CResourceMap &rm = appState->GetResourceMap();
            std::string aud = GetAudioVolumePath(_gameFolder, false, AudioVolumeName::Aud);
            std::string sfx = GetAudioVolumePath(_gameFolder, false, AudioVolumeName::Sfx);
            // A marker after the last resource: a rebuilt volume does not have it.
            for (const std::string &volume : { aud, sfx })
            {
                std::ofstream file(volume, std::ios::binary | std::ios::app);
                file << "KEEP-THIS-VOLUME";
            }
            std::vector<char> audBefore = ReadBytes(aud);
            std::vector<char> sfxBefore = ReadBytes(sfx);
            MakeReadOnly(MapPath());

            rm.RepackageAudio(true);

            Assert::IsTrue(audBefore == ReadBytes(aud), L"the audio maps were not saved, so resource.aud must not change");
            Assert::IsTrue(sfxBefore == ReadBytes(sfx), L"the audio maps were not saved, so resource.sfx must not change");
            Assert::IsFalse(FileExists(GetAudioVolumePath(_gameFolder, true, AudioVolumeName::Aud)), L"the new resource.aud must be deleted");
            Assert::IsFalse(FileExists(GetAudioVolumePath(_gameFolder, true, AudioVolumeName::Sfx)), L"the new resource.sfx must be deleted");
        }
    };
}
