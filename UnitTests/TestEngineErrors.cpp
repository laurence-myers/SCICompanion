#include "stdafx.h"
#include "CppUnitTest.h"
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
#include "ResourceSources.h"
#include "ResourceMapOperations.h"
#include "TestSupport.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    std::string CodeName(const sci::Error &error)
    {
        return sci::ErrorCodeName(error.code);
    }

    // The code of the sci::DataError that fn throws; "none" when fn throws
    // nothing.
    template<typename TFunc>
    std::string DataErrorCode(TFunc fn)
    {
        try
        {
            fn();
        }
        catch (const sci::DataError &e)
        {
            return sci::ErrorCodeName(e.code());
        }
        return "none";
    }

    // The package header of text 5 of an SCI0 game.
    ResourceHeaderAgnostic TextHeader(uint16_t compressionMethod, uint32_t compressedSize, uint32_t size)
    {
        ResourceHeaderAgnostic header;
        header.Type = ResourceType::Text;
        header.Number = 5;
        header.PackageHint = 1;
        header.CompressionMethod = compressionMethod;
        header.cbCompressed = compressedSize;
        header.cbDecompressed = size;
        header.Version = sciVersion0;
        header.SourceFlags = ResourceSourceFlags::ResourceMap;
        return header;
    }
}

namespace UnitTests
{
    // Engine errors are values at the boundary: bad data throws a
    // sci::DataError with a code, a partial text read and a failed
    // decompression are Format errors of the resource, and the script and
    // table loads give a Status.
    TEST_CLASS(TestEngineErrors)
    {
        GameCopy _game;

        // A new copy of the template, and a session on it with the default
        // options.
        GameSession &OpenCopy(const char *templateFolder)
        {
            _game.Make(templateFolder);
            return _game.Open(SessionOptions());
        }

        // In a copy of the SCI1.1 template, sets count bytes of the package
        // header of the resource to 0, from byte first of the header (9
        // bytes: the type with 0x80, the number, the compressed and the full
        // size, the method). True when the blob of the resource is then
        // Corrupted and TryCreate refuses it.
        bool DamagedHeader(ResourceType type, uint16_t number, size_t count, size_t first = 0)
        {
            NoAppState noAppState;
            uint32_t size = 0;
            {
                GameSession &session = OpenCopy(TemplateSci11);
                std::unique_ptr<ResourceBlob> blob = session.Helper().MostRecentResource(type, number, ResourceEnumFlags::None);
                Assert::IsTrue(blob != nullptr, L"setup: the SCI1.1 template has the resource");
                Assert::IsTrue(blob->GetSourceFlags() == ResourceSourceFlags::ResourceMap, L"setup: the resource is in the package");
                size = blob->GetHeader().cbDecompressed;
            }
            _game.CloseSessions();
            std::string volumePath = _game.Path("resource.000");
            std::vector<uint8_t> volume = ReadFileBytes(volumePath);
            size_t header = SIZE_MAX;
            int matches = 0;
            for (size_t i = 0; (i + 9) <= volume.size(); i++)
            {
                if ((volume[i] == (0x80 | (int)type)) && (volume[i + 1] == (number & 0xff)) && (volume[i + 2] == (number >> 8)) &&
                    (volume[i + 5] == (size & 0xff)) && (volume[i + 6] == ((size >> 8) & 0xff)))
                {
                    header = i;
                    matches++;
                }
            }
            Assert::AreEqual(1, matches, L"setup: the header of the resource must be found once");
            std::fill(volume.begin() + header + first, volume.begin() + header + first + count, (uint8_t)0);
            WriteFileBytes(volumePath, volume);

            GameSession &session = _game.Open(SessionOptions());
            std::unique_ptr<ResourceBlob> blob = session.Helper().MostRecentResource(type, number, ResourceEnumFlags::None);
            Assert::IsTrue(blob != nullptr);
            return IsFlagSet(blob->GetStatusFlags(), ResourceLoadStatusFlags::Corrupted) && !TryCreateResourceFromResourceData(*blob).has_value();
        }

    public:
        TEST_METHOD(DataError_ReadPastTheEnd_IsFormat)
        {
            uint8_t bytes[1] = { 7 };
            sci::istream stream(bytes, 1);
            stream.setThrowExceptions(true);
            uint16_t word = 0;
            Assert::AreEqual(std::string("format"), DataErrorCode([&]() { stream >> word; }), L"a read past the end must throw a DataError");
        }

        // Some catch sites catch std::exception; a DataError must still reach them.
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
                Assert::IsTrue(std::string(e.what()).find("too large") != std::string::npos, Wide(e.what()).c_str());
            }
            Assert::IsTrue(threw);
        }

        TEST_METHOD(TryCreateResource_Text_HasItsStrings)
        {
            GameFolderHelper helper;
            std::vector<uint8_t> data = { 'H', 'i', 0, 'B', 'y', 'e', 0 };
            ResourceBlob blob(helper, nullptr, ResourceType::Text, data, 1, 5, NoBase36, sciVersion0, ResourceSourceFlags::PatchFile);

            auto created = TryCreateResourceFromResourceData(blob);

            Assert::AreEqual(size_t(2), ValueOf(created)->GetComponent<TextComponent>().Texts.size());
        }

        // A text whose last string has no NUL is a Format error, not the texts
        // read so far.
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

        // A failed decompression is a Format error of the resource.
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
            Assert::IsTrue(created.error().message.find("decompressed") != std::string::npos, Wide(created.error().ToString()).c_str());
        }

        // A truncated script: Format, with the script in the location.
        TEST_METHOD(CompiledScriptTryLoad_TruncatedScript_IsAFormatError)
        {
            NoAppState noAppState;
            for (const char *name : { TemplateSci0, TemplateSci11 })
            {
                GameSession &session = OpenCopy(name);
                const GameFolderHelper &helper = session.Helper();

                CompiledScript intact(0);
                sci::Status loaded = intact.TryLoad(helper, helper.Version, 0);
                AssertOk(loaded);

                // Write a copy of script 0 with only its first 10 bytes.
                std::unique_ptr<ResourceBlob> original = helper.MostRecentResource(ResourceType::Script, 0, ResourceEnumFlags::None);
                Assert::IsTrue(original && (original->GetLength() > 10));
                std::vector<uint8_t> data(original->GetData(), original->GetData() + 10);
                ResourceBlob truncated(helper, nullptr, ResourceType::Script, data, helper.Version.DefaultVolumeFile, 0, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
                AssertOk(session.ResourceMap().WriteResource(truncated));

                CompiledScript broken(0);
                loaded = broken.TryLoad(helper, helper.Version, 0);
                Assert::IsFalse(loaded.has_value(), Wide(name).c_str());
                Assert::AreEqual(std::string("format"), CodeName(loaded.error()), Wide(loaded.error().ToString()).c_str());
                Assert::AreEqual(std::string("script 0"), loaded.error().where.resource);
                // The stream's own text: TryLoad reads in throw mode, so a read
                // past the end fails at once. (Without it, the loader reads zeros.)
                Assert::IsTrue(loaded.error().message.find("past end of stream") != std::string::npos, Wide(loaded.error().ToString()).c_str());
            }
        }

        TEST_METHOD(CompiledScriptTryLoad_MissingScript_IsNotFound)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(TemplateSci0);

            CompiledScript missing(950);
            sci::Status loaded = missing.TryLoad(session.Helper(), session.Version(), 950);

            Assert::IsFalse(loaded.has_value());
            Assert::AreEqual(std::string("not-found"), CodeName(loaded.error()));
            Assert::AreEqual(std::string("script 950"), loaded.error().where.resource);
        }

        TEST_METHOD(TablesTryLoad_Templates_Load)
        {
            NoAppState noAppState;
            for (const char *name : { TemplateSci0, TemplateSci11 })
            {
                GameSession &session = OpenCopy(name);
                GlobalCompiledScriptLookups lookups;
                AssertOk(lookups.TryLoad(session.Helper()));
                CompileTables tables;
                AssertOk(tables.TryLoad(session.ResourceMap()));
            }
        }

        // A game with no class table (vocab 996): NotFound, with the resource.
        TEST_METHOD(CheckVocabTables_NoClassTable_IsNotFound)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(TemplateSci0);
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
            NoAppState noAppState;
            GameSession &session = OpenCopy(TemplateSci0);
            std::unique_ptr<ResourceBlob> selectorTable = session.Helper().MostRecentResource(ResourceType::Vocab, 997, ResourceEnumFlags::None);
            Assert::IsTrue(selectorTable != nullptr, L"the template has a selector table");
            session.ResourceMap().DeleteResource(selectorTable.get());

            sci::Status checked = CheckVocabTables(session.Helper());

            Assert::IsFalse(checked.has_value());
            Assert::AreEqual(std::string("not-found"), CodeName(checked.error()));
            Assert::AreEqual(std::string("vocab 997"), checked.error().where.resource);
            Assert::AreEqual(std::string("the game has no selector table"), checked.error().message);
        }

        // A table that is there but not valid gives its own name and
        // resource.
        TEST_METHOD(TablesTryLoad_SelectorTableNotValid_NamesTheTable)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(TemplateSci0);
            const GameFolderHelper &helper = session.Helper();
            // A selector table that says it has 256 selectors, and has none.
            std::vector<uint8_t> data = { 0xff, 0x00 };
            ResourceBlob bad(helper, nullptr, ResourceType::Vocab, data, helper.Version.DefaultVolumeFile, 997, NoBase36, helper.Version, helper.GetDefaultSaveSourceFlags());
            AssertOk(session.ResourceMap().WriteResource(bad));

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

        // TryLoad reads in throw mode, but it must not reject a script that
        // Load reads. Script 990 of the SCI1.1 template has an object name
        // value outside its heap.
        TEST_METHOD(CompiledScriptTryLoad_AgreesWithLoad_OnEveryTemplateScript)
        {
            NoAppState noAppState;
            for (const char *name : { TemplateSci0, TemplateSci11 })
            {
                const GameFolderHelper &helper = OpenCopy(name).Helper();
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
                Assert::IsTrue(scripts > 20, Wide(name).c_str());
                Assert::IsTrue(disagreements.empty(), Wide(std::string(name) + ":\n" + disagreements).c_str());
            }
        }

        // A heap that cannot be read names the heap, not the script. The
        // loader reads a copy of the heap stream, so the name must go with the
        // copy.
        TEST_METHOD(CompiledScriptTryLoad_DamagedHeap_NamesTheHeap)
        {
            NoAppState noAppState;
            const GameFolderHelper &helper = OpenCopy(TemplateSci11).Helper();
            std::unique_ptr<ResourceBlob> script = helper.MostRecentResource(ResourceType::Script, 0, ResourceEnumFlags::None);
            std::unique_ptr<ResourceBlob> heap = helper.MostRecentResource(ResourceType::Heap, 0, ResourceEnumFlags::None);
            Assert::IsTrue(script && heap && (heap->GetLength() > 10));

            // A heap cut to its first 10 bytes.
            std::vector<uint8_t> cut(heap->GetData(), heap->GetData() + 10);
            ResourceBlob shortHeap(helper, nullptr, ResourceType::Heap, cut, 1, 0, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            CompiledScript first(0);
            sci::Status loaded = first.TryLoad(helper, helper.Version, 0, *script, &shortHeap);
            Assert::IsFalse(loaded.has_value());
            Assert::AreEqual(std::string("heap 0"), loaded.error().where.resource, Wide(loaded.error().ToString()).c_str());
            Assert::IsTrue(loaded.error().where.offset >= 10, Wide(loaded.error().ToString()).c_str());

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

        // The TryLoad form that takes the blobs (the species table and the
        // script catalog use it), for an SCI1.1 script with no heap blob.
        // Without the check, Load would find the heap by itself, and the
        // caller's "no heap" would be lost.
        TEST_METHOD(CompiledScriptTryLoad_BlobsWithNoHeap_IsNotFound)
        {
            NoAppState noAppState;
            const GameFolderHelper &helper = OpenCopy(TemplateSci11).Helper();
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
            std::vector<uint8_t> garbage(64, 0xff);
            sci::istream stream(garbage.data(), (uint32_t)garbage.size());
            ResourceBlob blob;
            // No SCI version has compression method 7. (The SCI0 LZW decoder
            // does not find errors, so bad LZW data is not a sure failure.)
            blob.CreateFromPackageBits("", TextHeader(7, 16, 200), stream, true);
            Assert::IsFalse(IsFlagSet(blob.GetStatusFlags(), ResourceLoadStatusFlags::DecompressionFailed), L"not decompressed yet");

            auto created = TryCreateResourceFromResourceData(blob);

            Assert::IsFalse(created.has_value(), L"the data cannot be decompressed");
            Assert::AreEqual(std::string("format"), CodeName(created.error()), Wide(created.error().ToString()).c_str());
            Assert::AreEqual(std::string("text 5"), created.error().where.resource);
            Assert::IsTrue(created.error().message.find("decompressed") != std::string::npos, Wide(created.error().ToString()).c_str());
        }

        // A volume that ends inside the resource data: the short read marks
        // the blob Corrupted, and CheckResourceData fails.
        TEST_METHOD(ShortReadOfTheData_MarksTheBlobDamaged)
        {
            std::vector<uint8_t> tenBytes = { 'H', 'i', 0, 'H', 'i', 0, 'H', 'i', 0, 0 };
            sci::istream stream(tenBytes.data(), (uint32_t)tenBytes.size());
            ResourceBlob blob;
            blob.CreateFromPackageBits("", TextHeader(0, 100, 100), stream, false);

            Assert::IsTrue(IsFlagSet(blob.GetStatusFlags(), ResourceLoadStatusFlags::Corrupted), L"a short read marks the blob");
            sci::Status checked = CheckResourceData(blob);
            Assert::IsFalse(checked.has_value());
            Assert::AreEqual(std::string("text 5"), checked.error().where.resource);
        }

        // The same for compressed data, which the blob reads on another path.
        TEST_METHOD(ShortReadOfCompressedData_MarksTheBlobDamaged)
        {
            std::vector<uint8_t> tenBytes(10, 0);
            sci::istream stream(tenBytes.data(), (uint32_t)tenBytes.size());
            ResourceBlob blob;
            // Compression method 1 is LZW in SCI0. Delay the decompression, so
            // that only the read can set the flag.
            blob.CreateFromPackageBits("", TextHeader(1, 100, 200), stream, true);

            Assert::IsTrue(IsFlagSet(blob.GetStatusFlags(), ResourceLoadStatusFlags::Corrupted), L"a short read of compressed data marks the blob");
        }

        // An empty resource in the package (a text with no strings) is
        // valid: its header has sizes of 0, but its blob has no Corrupted
        // flag, and TryCreate accepts it.
        TEST_METHOD(EmptyPackageResource_LoadsWithNoFlag)
        {
            NoAppState noAppState;
            for (const char *templateFolder : { TemplateSci0, TemplateSci11 })
            {
                GameSession &session = OpenCopy(templateFolder);
                const GameFolderHelper &helper = session.Helper();
                std::vector<uint8_t> noData;
                ResourceBlob empty(helper, nullptr, ResourceType::Text, noData, helper.Version.DefaultVolumeFile, 555, NoBase36, helper.Version, ResourceSourceFlags::ResourceMap);
                AssertOk(session.ResourceMap().WriteResource(empty));

                std::unique_ptr<ResourceBlob> blob = helper.MostRecentResource(ResourceType::Text, 555, ResourceEnumFlags::None);
                Assert::IsTrue(blob != nullptr, Wide(std::string("no text 555 in ") + templateFolder).c_str());
                Assert::IsTrue(blob->GetSourceFlags() == ResourceSourceFlags::ResourceMap, L"setup: text 555 is in the package");
                Assert::AreEqual(0, (int)blob->GetLength(), L"setup: text 555 is empty");
                Assert::IsFalse(IsFlagSet(blob->GetStatusFlags(), ResourceLoadStatusFlags::Corrupted),
                    Wide(std::string("an empty resource is not damaged: ") + templateFolder).c_str());
                AssertOk(TryCreateResourceFromResourceData(*blob));
            }
        }

        // Damage that zeroes an SCI1.1 package header gives sizes of 0, as an
        // empty resource has; the header's type and number (also 0) do not
        // match the map entry, so the blob is damaged.
        TEST_METHOD(ZeroedPackageHeader_IsDamaged)
        {
            Assert::IsTrue(DamagedHeader(ResourceType::Text, 10, 9), L"a zeroed header must mark the blob Corrupted");
        }

        // A zeroed header reads as view 0 with sizes of 0, so for view 0
        // itself (the first header of the SCI1.1 template's resource.000) the
        // type and the number match the map entry. Its type byte has no 0x80
        // mark, so it is damage, not a valid empty view (a rebuild would drop
        // the data of view 0).
        TEST_METHOD(ZeroedHeaderOfView0_IsDamaged)
        {
            Assert::IsTrue(DamagedHeader(ResourceType::View, 0, 9), L"a zeroed header of view 0 must mark the blob Corrupted");
        }

        // A header with only one size of 0 is damaged too.
        TEST_METHOD(PackageHeaderWithOneSizeOfZero_IsDamaged)
        {
            Assert::IsTrue(DamagedHeader(ResourceType::Text, 10, 2, 3), L"a header with a compressed size of 0 must mark the blob Corrupted");
        }

        // The number of a package header is signed, but a valid empty
        // resource numbered 32768 or more must still match its map entry: it
        // is not marked Corrupted, and a rebuild keeps it.
        TEST_METHOD(EmptyPackageResource_HighNumber_IsKept)
        {
            NoAppState noAppState;
            {
                GameSession &session = OpenCopy(TemplateSci11);
                const GameFolderHelper &helper = session.Helper();
                std::vector<uint8_t> noData;
                ResourceBlob empty(helper, nullptr, ResourceType::Text, noData, helper.Version.DefaultVolumeFile, 40000, NoBase36, helper.Version, ResourceSourceFlags::ResourceMap);
                AssertOk(session.ResourceMap().WriteResource(empty));
                std::unique_ptr<ResourceBlob> blob = helper.MostRecentResource(ResourceType::Text, 40000, ResourceEnumFlags::None);
                Assert::IsTrue(blob != nullptr, L"setup: text 40000");
                Assert::IsFalse(IsFlagSet(blob->GetStatusFlags(), ResourceLoadStatusFlags::Corrupted), L"an empty resource numbered 40000 is not damaged");
                std::unique_ptr<ResourceSource> package = CreateResourceSource(ResourceTypeFlags::All, helper, ResourceSourceFlags::ResourceMap, ResourceSourceAccessFlags::ReadWrite);
                std::map<ResourceType, RebuildStats> stats;
                package->RebuildResources(true, *package, stats);
            }
            _game.CloseSessions();
            GameSession &session = _game.Open(SessionOptions());
            std::unique_ptr<ResourceBlob> blob = session.Helper().MostRecentResource(ResourceType::Text, 40000, ResourceEnumFlags::None);
            Assert::IsTrue(blob != nullptr, L"the rebuild keeps text 40000");
            Assert::IsFalse(IsFlagSet(blob->GetStatusFlags(), ResourceLoadStatusFlags::Corrupted));
        }

        // A rebuild keeps an empty package resource, and a delete removes it,
        // although the header reader throws "the resource is empty" for its
        // sizes of 0.
        TEST_METHOD(EmptyPackageResource_RebuildKeepsIt_DeleteRemovesIt)
        {
            NoAppState noAppState;
            for (const char *templateFolder : { TemplateSci0, TemplateSci11 })
            {
                {
                    GameSession &session = OpenCopy(templateFolder);
                    const GameFolderHelper &helper = session.Helper();
                    std::vector<uint8_t> noData;
                    ResourceBlob empty(helper, nullptr, ResourceType::Text, noData, helper.Version.DefaultVolumeFile, 555, NoBase36, helper.Version, ResourceSourceFlags::ResourceMap);
                    AssertOk(session.ResourceMap().WriteResource(empty));
                    // The package step of the GUI's "rebuild resources".
                    std::unique_ptr<ResourceSource> package = CreateResourceSource(ResourceTypeFlags::All, helper, ResourceSourceFlags::ResourceMap, ResourceSourceAccessFlags::ReadWrite);
                    std::map<ResourceType, RebuildStats> stats;
                    package->RebuildResources(true, *package, stats);
                }
                _game.CloseSessions();
                {
                    GameSession &session = _game.Open(SessionOptions());
                    std::unique_ptr<ResourceBlob> blob = session.Helper().MostRecentResource(ResourceType::Text, 555, ResourceEnumFlags::None);
                    Assert::IsTrue(blob != nullptr, Wide(std::string("the rebuild dropped the empty text: ") + templateFolder).c_str());
                    Assert::IsFalse(IsFlagSet(blob->GetStatusFlags(), ResourceLoadStatusFlags::Corrupted));
                    session.ResourceMap().DeleteResource(blob.get());
                }
                _game.CloseSessions();
                GameSession &session = _game.Open(SessionOptions());
                Assert::IsTrue(nullptr == session.Helper().MostRecentResource(ResourceType::Text, 555, ResourceEnumFlags::None),
                    Wide(std::string("the delete left the empty text: ") + templateFolder).c_str());
            }
        }

        // A map entry whose header is past the end of its volume gives a blob
        // marked Corrupted, not an empty blob with no flag (which would look
        // like an empty resource).
        TEST_METHOD(TruncatedVolume_EveryDamagedResourceIsMarked)
        {
            NoAppState noAppState;
            GameSession &session = OpenCopy(TemplateSci0);
            // After the open, so that the version detection reads the whole game.
            std::string volume = _game.Path("resource.001");
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
            Assert::IsTrue(silent.empty(), Wide(silent).c_str());
            Assert::IsTrue(damaged > 1, L"half of the volume is gone");
            Assert::IsTrue(intact > 1, L"the first half of the volume is there");
        }

        // With the GUI's default-resource fallback, a text resource whose
        // last string has no NUL becomes a default resource marked "creation
        // failed", not the strings read so far.
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

        // Outside throw mode, a failed string read puts the stream back, so
        // the text reader must stop (it throws); else it would read the same
        // place again with no end.
        TEST_METHOD(TextReadFrom_NoFinalNulOutsideThrowMode_Throws)
        {
            std::unique_ptr<ResourceEntity> text(CreateTextResource(sciVersion0));
            std::vector<uint8_t> data = { 'H', 'i', 0, 'B', 'y', 'e' };
            std::string code = DataErrorCode([&]() { text->ReadFrom(sci::istream(data.data(), (uint32_t)data.size()), std::map<BlobKey, uint32_t>()); });
            Assert::AreEqual(std::string("format"), code, L"the reader must throw, not loop");
        }
    };
}
