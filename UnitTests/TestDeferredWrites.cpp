#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "Helper.h"
#include "Stream.h"
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
    TEST_CLASS(TestDeferredWrites)
    {
        std::string _gameFolder;

        static std::vector<uint8_t> TextBytes(const std::string &text)
        {
            std::vector<uint8_t> bytes(text.begin(), text.end());
            bytes.push_back(0);
            return bytes;
        }

        static ResourceBlob MakeText(const GameFolderHelper &helper, int number, const std::string &text, const char *name = nullptr)
        {
            return ResourceBlob(helper, name, ResourceType::Text, TextBytes(text), helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, ResourceSourceFlags::ResourceMap);
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

        std::string MapPath() const { return _gameFolder + "\\resource.map"; }

        void MakeMapReadOnly(bool readOnly)
        {
            SetFileAttributesA(MapPath().c_str(), readOnly ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL);
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
                MakeMapReadOnly(false);
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        TEST_METHOD(NestedBatch_WritesTheOuterQueue)
        {
            CResourceMap &rm = appState->GetResourceMap();
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
        }

        TEST_METHOD(AbandonedBatch_WritesNothing)
        {
            CResourceMap &rm = appState->GetResourceMap();
            {
                DeferResourceAppend batch(rm);
                Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 906, "never")).has_value());
                // No Commit.
            }
            Assert::AreEqual(std::string("(missing)"), ReadText(rm, 906));
        }

        TEST_METHOD(Commit_FailedWrite_ReturnsIoAndNamesNothing)
        {
            CResourceMap &rm = appState->GetResourceMap();
            MakeMapReadOnly(true);

            DeferResourceAppend batch(rm);
            Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 903, "blocked", "BlockedText")).has_value());
            sci::Status committed = batch.Commit();

            Assert::IsFalse(committed.has_value(), L"a commit that cannot write the map must fail");
            Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(committed.error().code)));
            std::string text = committed.error().ToString();
            Assert::IsTrue(text.find("resource.map") != std::string::npos, std::wstring(text.begin(), text.end()).c_str());
            Assert::AreEqual(std::string(""), rm.Helper().GetIniString("Text", "n903"), L"a failed write must not name the resource in game.ini");
        }

        TEST_METHOD(SameResourceQueuedTwice_LastCopyWins)
        {
            CResourceMap &rm = appState->GetResourceMap();
            {
                DeferResourceAppend batch(rm);
                Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 904, "first")).has_value());
                Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 904, "second")).has_value());
                Assert::AreEqual(size_t(1), batch.Pending().size());
                Assert::IsTrue(batch.Commit().has_value());
            }
            Assert::AreEqual(std::string("second"), ReadText(rm, 904));
            Assert::AreEqual(1, CountMapEntries(rm, 904));
        }

        TEST_METHOD(WriteResource_WithoutBatch_ReturnsTheError)
        {
            CResourceMap &rm = appState->GetResourceMap();
            MakeMapReadOnly(true);
            sci::Status failed = rm.WriteResource(MakeText(rm.Helper(), 905, "blocked", "DirectText"));
            Assert::IsFalse(failed.has_value());
            Assert::AreEqual(std::string("io"), std::string(sci::ErrorCodeName(failed.error().code)));
            Assert::AreEqual(std::string(""), rm.Helper().GetIniString("Text", "n905"));

            MakeMapReadOnly(false);
            Assert::IsTrue(rm.WriteResource(MakeText(rm.Helper(), 905, "written", "DirectText")).has_value());
            Assert::AreEqual(std::string("written"), ReadText(rm, 905));
            Assert::AreEqual(std::string("DirectText"), rm.Helper().GetIniString("Text", "n905"));
        }
    };
}
