#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceEntity.h"
#include "ResourceUtil.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "Text.h"
#include "CompiledScript.h"
#include "CompileInterfaces.h"
#include "CompileContext.h"
#include "Vocab99x.h"
#include "Helper.h"
#include <filesystem>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    // Runs a test with no AppState, as the command line does.
    struct NoAppStateInScope
    {
        AppState *saved;
        NoAppStateInScope() : saved(appState) { appState = nullptr; }
        ~NoAppStateInScope() { appState = saved; }
    };

    std::wstring WideText(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::string CodeName(const sci::Error &error)
    {
        return sci::ErrorCodeName(error.code);
    }
}

namespace UnitTests
{
    // Plan step F2: engine errors are values at the boundary. Before it, bad
    // data threw the Microsoft-only std::exception("...") with no code, a
    // partial text read was silent, a failed decompression reached the caller
    // only as a log line, and the script and table loads gave only a bool.
    TEST_CLASS(TestEngineErrors)
    {
        std::string _copyFolder;

        void RemoveCopy()
        {
            if (!_copyFolder.empty())
            {
                std::error_code ec;
                std::filesystem::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        TEST_METHOD(DataError_ReadPastTheEnd_IsFormat)
        {
            uint8_t bytes[1] = { 7 };
            sci::istream stream(bytes, 1);
            stream.setThrowExceptions(true);
            uint16_t word = 0;
            bool threw = false;
            try
            {
                stream >> word;
            }
            catch (const sci::DataError &e)
            {
                threw = true;
                Assert::IsTrue(e.code() == sci::ErrorCode::Format);
            }
            Assert::IsTrue(threw, L"a read past the end must throw a DataError");
        }

        // Old catch sites catch std::exception; a DataError must still reach them.
        TEST_METHOD(DataError_TooLarge_IsUnsupportedAndAStdException)
        {
            bool threw = false;
            try
            {
                ThrowExceptionIfOverflow(10, 5, "Size");
            }
            catch (const std::exception &e)
            {
                threw = true;
                const sci::DataError *dataError = dynamic_cast<const sci::DataError *>(&e);
                Assert::IsNotNull(dataError, L"the exception must be a DataError");
                Assert::IsTrue(dataError->code() == sci::ErrorCode::Unsupported);
                Assert::IsTrue(std::string(e.what()).find("too large") != std::string::npos, WideText(e.what()).c_str());
            }
            Assert::IsTrue(threw);
        }

        TEST_METHOD(TryCreateResource_Text_HasItsStrings)
        {
            GameFolderHelper helper;
            std::vector<uint8_t> data = { 'H', 'i', 0, 'B', 'y', 'e', 0 };
            ResourceBlob blob(helper, nullptr, ResourceType::Text, data, 1, 5, NoBase36, sciVersion0, ResourceSourceFlags::PatchFile);

            auto created = TryCreateResourceFromResourceData(blob);

            Assert::IsTrue(created.has_value(), WideText(created ? std::string() : created.error().ToString()).c_str());
            Assert::AreEqual(size_t(2), (*created)->GetComponent<TextComponent>().Texts.size());
        }

        // Before F2, TextReadFrom kept the texts read so far and said nothing.
        TEST_METHOD(TryCreateResource_TextWithNoFinalNul_IsAFormatError)
        {
            GameFolderHelper helper;
            std::vector<uint8_t> data = { 'H', 'i', 0, 'B', 'y', 'e' };
            ResourceBlob blob(helper, nullptr, ResourceType::Text, data, 1, 5, NoBase36, sciVersion0, ResourceSourceFlags::PatchFile);

            auto created = TryCreateResourceFromResourceData(blob);

            Assert::IsFalse(created.has_value(), L"a partial read must be an error");
            Assert::AreEqual(std::string("format"), CodeName(created.error()));
            Assert::AreEqual(std::string("text 5"), created.error().where.resource);
        }

        // Before F2, a failed decompression reached the caller only as a log line.
        TEST_METHOD(TryCreateResource_DecompressionFailed_IsAFormatError)
        {
            GameFolderHelper helper;
            std::vector<uint8_t> data = { 'H', 'i', 0 };
            ResourceBlob blob(helper, nullptr, ResourceType::Text, data, 1, 5, NoBase36, sciVersion0, ResourceSourceFlags::PatchFile);
            blob.AddStatusFlags(ResourceLoadStatusFlags::DecompressionFailed);

            auto created = TryCreateResourceFromResourceData(blob);

            Assert::IsFalse(created.has_value());
            Assert::AreEqual(std::string("format"), CodeName(created.error()));
            Assert::AreEqual(std::string("text 5"), created.error().where.resource);
            Assert::IsTrue(created.error().message.find("decompressed") != std::string::npos, WideText(created.error().ToString()).c_str());
        }

        // A truncated script: Format, with the script in the location. Before
        // F2, Load gave only false, or read zeros past the end and succeeded.
        TEST_METHOD(CompiledScriptTryLoad_TruncatedScript_IsAFormatError)
        {
            NoAppStateInScope noAppState;
            const char *templates[] = { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" };
            for (const char *name : templates)
            {
                _copyFolder = CopyGameFromModuleFolder(name);
                {
                    GameSession session;
                    Assert::IsTrue(session.Open(_copyFolder).has_value());
                    const GameFolderHelper &helper = session.Helper();

                    CompiledScript intact(0);
                    sci::Status loaded = intact.TryLoad(helper, helper.Version, 0);
                    Assert::IsTrue(loaded.has_value(), WideText(loaded ? std::string() : loaded.error().ToString()).c_str());

                    // Write a copy of script 0 with only its first 10 bytes.
                    std::unique_ptr<ResourceBlob> original = helper.MostRecentResource(ResourceType::Script, 0, ResourceEnumFlags::None);
                    Assert::IsTrue(original && (original->GetLength() > 10));
                    std::vector<uint8_t> data(original->GetData(), original->GetData() + 10);
                    ResourceBlob truncated(helper, nullptr, ResourceType::Script, data, helper.Version.DefaultVolumeFile, 0, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
                    sci::Status written = session.ResourceMap().WriteResource(truncated);
                    Assert::IsTrue(written.has_value(), WideText(written ? std::string() : written.error().ToString()).c_str());

                    CompiledScript broken(0);
                    loaded = broken.TryLoad(helper, helper.Version, 0);
                    Assert::IsFalse(loaded.has_value(), WideText(name).c_str());
                    Assert::AreEqual(std::string("format"), CodeName(loaded.error()), WideText(loaded.error().ToString()).c_str());
                    Assert::AreEqual(std::string("script 0"), loaded.error().where.resource);
                    // The stream's own text: TryLoad reads in throw mode, so a read
                    // past the end fails at once. (Without it, the loader reads zeros.)
                    Assert::IsTrue(loaded.error().message.find("past end of stream") != std::string::npos, WideText(loaded.error().ToString()).c_str());
                }
                RemoveCopy();
            }
        }

        TEST_METHOD(CompiledScriptTryLoad_MissingScript_IsNotFound)
        {
            NoAppStateInScope noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());

            CompiledScript missing(950);
            sci::Status loaded = missing.TryLoad(session.Helper(), session.Version(), 950);

            Assert::IsFalse(loaded.has_value());
            Assert::AreEqual(std::string("not-found"), CodeName(loaded.error()));
            Assert::AreEqual(std::string("script 950"), loaded.error().where.resource);
        }

        TEST_METHOD(TablesTryLoad_Templates_Load)
        {
            NoAppStateInScope noAppState;
            const char *templates[] = { "\\TemplateGame\\SCI0", "\\TemplateGame\\SCI1.1" };
            for (const char *name : templates)
            {
                _copyFolder = CopyGameFromModuleFolder(name);
                {
                    GameSession session;
                    Assert::IsTrue(session.Open(_copyFolder).has_value());
                    GlobalCompiledScriptLookups lookups;
                    sci::Status lookupsLoaded = lookups.TryLoad(session.Helper());
                    Assert::IsTrue(lookupsLoaded.has_value(), WideText(lookupsLoaded ? std::string() : lookupsLoaded.error().ToString()).c_str());
                    CompileTables tables;
                    sci::Status tablesLoaded = tables.TryLoad(session.ResourceMap());
                    Assert::IsTrue(tablesLoaded.has_value(), WideText(tablesLoaded ? std::string() : tablesLoaded.error().ToString()).c_str());
                }
                RemoveCopy();
            }
        }

        // A game with no class table (vocab 996): NotFound, with the resource.
        TEST_METHOD(CheckVocabTables_NoClassTable_IsNotFound)
        {
            NoAppStateInScope noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            std::unique_ptr<ResourceBlob> classTable = session.Helper().MostRecentResource(ResourceType::Vocab, 996, ResourceEnumFlags::None);
            Assert::IsTrue(classTable != nullptr, L"the template has a class table");
            session.ResourceMap().DeleteResource(classTable.get());

            sci::Status checked = CheckVocabTables(session.Helper());

            Assert::IsFalse(checked.has_value());
            Assert::AreEqual(std::string("not-found"), CodeName(checked.error()));
            Assert::AreEqual(std::string("vocab 996"), checked.error().where.resource);
            Assert::AreEqual(std::string("the game has no class table"), checked.error().message);
        }
    };
}
