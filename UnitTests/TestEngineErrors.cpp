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

        TEST_METHOD(CheckVocabTables_NoSelectorTable_IsNotFound)
        {
            NoAppStateInScope noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            std::unique_ptr<ResourceBlob> selectorTable = session.Helper().MostRecentResource(ResourceType::Vocab, 997, ResourceEnumFlags::None);
            Assert::IsTrue(selectorTable != nullptr, L"the template has a selector table");
            session.ResourceMap().DeleteResource(selectorTable.get());

            sci::Status checked = CheckVocabTables(session.Helper());

            Assert::IsFalse(checked.has_value());
            Assert::AreEqual(std::string("not-found"), CodeName(checked.error()));
            Assert::AreEqual(std::string("vocab 997"), checked.error().where.resource);
            Assert::AreEqual(std::string("the game has no selector table"), checked.error().message);
        }

        // F2 review: a table that is there but not valid gives its own name
        // and resource. Before, the message named all three tables.
        TEST_METHOD(TablesTryLoad_SelectorTableNotValid_NamesTheTable)
        {
            NoAppStateInScope noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            const GameFolderHelper &helper = session.Helper();
            // A selector table that says it has 256 selectors, and has none.
            std::vector<uint8_t> data = { 0xff, 0x00 };
            ResourceBlob bad(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
            sci::Status written = session.ResourceMap().WriteResource(bad);
            Assert::IsTrue(written.has_value(), WideText(written ? std::string() : written.error().ToString()).c_str());

            GlobalCompiledScriptLookups lookups;
            sci::Status lookupsLoaded = lookups.TryLoad(helper);
            Assert::IsFalse(lookupsLoaded.has_value());
            Assert::AreEqual(std::string("format"), CodeName(lookupsLoaded.error()));
            Assert::AreEqual(std::string("vocab 997"), lookupsLoaded.error().where.resource);
            Assert::AreEqual(std::string("the selector table is not valid"), lookupsLoaded.error().message);

            CompileTables tables;
            sci::Status tablesLoaded = tables.TryLoad(session.ResourceMap());
            Assert::IsFalse(tablesLoaded.has_value());
            Assert::AreEqual(std::string("vocab 997"), tablesLoaded.error().where.resource);
            Assert::AreEqual(std::string("the selector table is not valid"), tablesLoaded.error().message);
        }

        // F2 review: TryLoad reads in throw mode, but it must not reject a
        // script that Load reads. Script 990 of the SCI1.1 template has an
        // object name value outside its heap.
        TEST_METHOD(CompiledScriptTryLoad_AgreesWithLoad_OnEveryTemplateScript)
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
                    std::string disagreements;
                    int scripts = 0;
                    auto container = helper.Resources(ResourceTypeFlags::Script, ResourceEnumFlags::MostRecentOnly);
                    for (auto &blob : *container)
                    {
                        scripts++;
                        CompiledScript old((uint16_t)blob->GetNumber());
                        bool oldLoaded = old.Load(helper, helper.Version, blob->GetNumber());
                        CompiledScript fresh((uint16_t)blob->GetNumber());
                        sci::Status loaded = fresh.TryLoad(helper, helper.Version, blob->GetNumber());
                        if (oldLoaded != loaded.has_value())
                        {
                            disagreements += (loaded ? "Load fails, TryLoad reads: script " + std::to_string(blob->GetNumber()) : loaded.error().ToString()) + "\n";
                        }
                    }
                    Assert::IsTrue(scripts > 20, WideText(name).c_str());
                    Assert::IsTrue(disagreements.empty(), WideText(std::string(name) + ":\n" + disagreements).c_str());
                }
                RemoveCopy();
            }
        }

        // F2 review: a heap that cannot be read names the heap, not the script.
        // The loader reads a copy of the heap stream, so the name must go with
        // the copy.
        TEST_METHOD(CompiledScriptTryLoad_DamagedHeap_NamesTheHeap)
        {
            NoAppStateInScope noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            const GameFolderHelper &helper = session.Helper();
            std::unique_ptr<ResourceBlob> script = helper.MostRecentResource(ResourceType::Script, 0, ResourceEnumFlags::None);
            std::unique_ptr<ResourceBlob> heap = helper.MostRecentResource(ResourceType::Heap, 0, ResourceEnumFlags::None);
            Assert::IsTrue(script && heap && (heap->GetLength() > 10));

            // A heap cut to its first 10 bytes.
            std::vector<uint8_t> cut(heap->GetData(), heap->GetData() + 10);
            ResourceBlob shortHeap(helper, nullptr, ResourceType::Heap, cut, 1, 0, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            CompiledScript first(0);
            sci::Status loaded = first.TryLoad(helper, helper.Version, 0, *script, &shortHeap);
            Assert::IsFalse(loaded.has_value());
            Assert::AreEqual(std::string("heap 0"), loaded.error().where.resource, WideText(loaded.error().ToString()).c_str());
            Assert::IsTrue(loaded.error().where.offset >= 10, WideText(loaded.error().ToString()).c_str());

            // A heap that did not decompress.
            std::vector<uint8_t> whole(heap->GetData(), heap->GetData() + heap->GetLength());
            ResourceBlob badHeap(helper, nullptr, ResourceType::Heap, whole, 1, 0, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            badHeap.AddStatusFlags(ResourceLoadStatusFlags::DecompressionFailed);
            CompiledScript second(0);
            loaded = second.TryLoad(helper, helper.Version, 0, *script, &badHeap);
            Assert::IsFalse(loaded.has_value());
            Assert::AreEqual(std::string("format"), CodeName(loaded.error()));
            Assert::AreEqual(std::string("heap 0"), loaded.error().where.resource);

            // A script that did not decompress.
            std::vector<uint8_t> scriptData(script->GetData(), script->GetData() + script->GetLength());
            ResourceBlob badScript(helper, nullptr, ResourceType::Script, scriptData, 1, 0, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            badScript.AddStatusFlags(ResourceLoadStatusFlags::DecompressionFailed);
            CompiledScript third(0);
            loaded = third.TryLoad(helper, helper.Version, 0, badScript, heap.get());
            Assert::IsFalse(loaded.has_value());
            Assert::AreEqual(std::string("script 0"), loaded.error().where.resource);
        }

        // K3 review: the TryLoad form that takes the blobs (K3 uses it), for
        // an SCI1.1 script with no heap blob. Without the check, Load would
        // find the heap by itself, and the caller's "no heap" would be lost.
        TEST_METHOD(CompiledScriptTryLoad_BlobsWithNoHeap_IsNotFound)
        {
            NoAppStateInScope noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            const GameFolderHelper &helper = session.Helper();
            std::unique_ptr<ResourceBlob> script = helper.MostRecentResource(ResourceType::Script, 0, ResourceEnumFlags::None);
            Assert::IsTrue(script != nullptr);

            CompiledScript compiled(0);
            sci::Status loaded = compiled.TryLoad(helper, helper.Version, 0, *script, nullptr);

            Assert::IsFalse(loaded.has_value());
            Assert::AreEqual(std::string("not-found"), CodeName(loaded.error()));
            Assert::AreEqual(std::string("heap 0"), loaded.error().where.resource);
        }

        // A blob that delays its decompression decompresses when its data is
        // first read. CheckResourceData must read it before it tests the flags.
        TEST_METHOD(TryCreateResource_DelayedBlobThatDoesNotDecompress_IsAFormatError)
        {
            ResourceHeaderAgnostic header;
            header.Type = ResourceType::Text;
            header.Number = 5;
            header.PackageHint = 1;
            // No SCI version has compression method 7. (The SCI0 LZW decoder
            // does not find errors, so bad LZW data is not a sure failure.)
            header.CompressionMethod = 7;
            header.cbCompressed = 16;
            header.cbDecompressed = 200;
            header.Version = sciVersion0;
            header.SourceFlags = ResourceSourceFlags::ResourceMap;
            std::vector<uint8_t> garbage(64, 0xff);
            sci::istream stream(garbage.data(), (uint32_t)garbage.size());
            ResourceBlob blob;
            blob.CreateFromPackageBits("", header, stream, true);
            Assert::IsFalse(IsFlagSet(blob.GetStatusFlags(), ResourceLoadStatusFlags::DecompressionFailed), L"not decompressed yet");

            auto created = TryCreateResourceFromResourceData(blob);

            Assert::IsFalse(created.has_value(), L"the data cannot be decompressed");
            Assert::AreEqual(std::string("format"), CodeName(created.error()), WideText(created.error().ToString()).c_str());
            Assert::AreEqual(std::string("text 5"), created.error().where.resource);
            Assert::IsTrue(created.error().message.find("decompressed") != std::string::npos, WideText(created.error().ToString()).c_str());
        }

        // F2 review: a volume that ends inside the resource data. Before, the
        // blob held bytes that were not read from the volume, and had no flag.
        TEST_METHOD(ShortReadOfTheData_MarksTheBlobDamaged)
        {
            ResourceHeaderAgnostic header;
            header.Type = ResourceType::Text;
            header.Number = 5;
            header.PackageHint = 1;
            header.CompressionMethod = 0;
            header.cbCompressed = 100;
            header.cbDecompressed = 100;
            header.Version = sciVersion0;
            header.SourceFlags = ResourceSourceFlags::ResourceMap;
            std::vector<uint8_t> tenBytes = { 'H', 'i', 0, 'H', 'i', 0, 'H', 'i', 0, 0 };
            sci::istream stream(tenBytes.data(), (uint32_t)tenBytes.size());
            ResourceBlob blob;
            blob.CreateFromPackageBits("", header, stream, false);

            Assert::IsTrue(IsFlagSet(blob.GetStatusFlags(), ResourceLoadStatusFlags::Corrupted), L"a short read marks the blob");
            sci::Status checked = CheckResourceData(blob);
            Assert::IsFalse(checked.has_value());
            Assert::AreEqual(std::string("text 5"), checked.error().where.resource);
        }

        // F2 review: a map entry whose header is past the end of its volume.
        // Before, the iterator gave an empty blob with no flag, which was the
        // same as an empty resource.
        TEST_METHOD(TruncatedVolume_EveryDamagedResourceIsMarked)
        {
            NoAppStateInScope noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI0");
            GameSession session;
            Assert::IsTrue(session.Open(_copyFolder).has_value());
            // After the open, so that the version detection reads the whole game.
            std::string volume = _copyFolder + "\\resource.001";
            std::filesystem::resize_file(volume, std::filesystem::file_size(volume) / 2);

            int damaged = 0;
            int intact = 0;
            std::string silent;
            auto container = session.Helper().Resources(ResourceTypeFlags::All, ResourceEnumFlags::MostRecentOnly);
            for (auto &blob : *container)
            {
                if (IsFlagSet(blob->GetStatusFlags(), ResourceLoadStatusFlags::Corrupted))
                {
                    damaged++;
                }
                else if (blob->GetLength() == 0)
                {
                    silent += DescribeResource(blob->GetType(), blob->GetNumber()) + " is empty and has no flag\n";
                }
                else
                {
                    intact++;
                }
            }
            Assert::IsTrue(silent.empty(), WideText(silent).c_str());
            Assert::IsTrue(damaged > 1, L"half of the volume is gone");
            Assert::IsTrue(intact > 1, L"the first half of the volume is there");
        }

        // The GUI change of F2: with the default-resource fallback, a text
        // resource whose last string has no NUL becomes a default resource
        // marked "creation failed". Before, the GUI showed the strings read
        // so far.
        TEST_METHOD(CreateResource_TextWithNoFinalNul_FallsBackAndIsMarked)
        {
            GameFolderHelper helper;
            std::vector<uint8_t> data = { 'H', 'i', 0, 'B', 'y', 'e' };
            ResourceBlob blob(helper, nullptr, ResourceType::Text, data, 1, 5, NoBase36, sciVersion0, ResourceSourceFlags::PatchFile);

            std::unique_ptr<ResourceEntity> created = CreateResourceFromResourceData(blob);

            Assert::IsTrue(created != nullptr);
            Assert::AreEqual(size_t(0), created->GetComponent<TextComponent>().Texts.size(), L"not the strings read so far");
            Assert::IsTrue(IsFlagSet(blob.GetStatusFlags(), ResourceLoadStatusFlags::ResourceCreationFailed));
        }

        // F2 review: outside throw mode, a failed string read puts the stream
        // back, so the text reader must stop. Before the fix, it read the same
        // place again with no end.
        TEST_METHOD(TextReadFrom_NoFinalNulOutsideThrowMode_Throws)
        {
            std::unique_ptr<ResourceEntity> text(CreateTextResource(sciVersion0));
            std::vector<uint8_t> data = { 'H', 'i', 0, 'B', 'y', 'e' };
            bool threw = false;
            try
            {
                text->ReadFrom(sci::istream(data.data(), (uint32_t)data.size()), std::map<BlobKey, uint32_t>());
            }
            catch (const sci::DataError &e)
            {
                threw = true;
                Assert::IsTrue(e.code() == sci::ErrorCode::Format);
            }
            Assert::IsTrue(threw, L"the reader must throw, not loop");
        }
    };
}
