#include "stdafx.h"
#include "CompileWrite.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceUtil.h"
#include "FileWrite.h"
#include "format.h"

std::string CompiledResourceFileName(ResourceType type, uint16_t number, const SCIVersion &version, bool raw)
{
    if (raw)
    {
        std::string typeName = GetResourceTypeTitle(type);
        std::transform(typeName.begin(), typeName.end(), typeName.begin(), [](char ch) { return (char)tolower((unsigned char)ch); });
        return fmt::format("{0}.{1}.bin", typeName, number);
    }
    return GetFileNameFor(type, number, NoBase36, version);
}

namespace
{
    std::string OutputPathOf(const GameFolderHelper &helper, const CompileWriteOptions &options, ResourceType type, uint16_t number)
    {
        return options.outDir + "\\" + CompiledResourceFileName(type, number, helper.Version, options.raw);
    }

    // One file of the output folder: a patch file, or with raw the plain
    // data.
    sci::Status WriteOutputFile(const GameFolderHelper &helper, const CompileWriteOptions &options, ResourceType type, uint16_t number, const std::vector<uint8_t> &data)
    {
        std::string path = OutputPathOf(helper, options, type, number);
        if (options.raw)
        {
            return WriteBytesToFile(path, data);
        }
        ResourceBlob blob(helper, nullptr, type, data, helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
        HRESULT hr = blob.SaveToFile(path);
        if (FAILED(hr))
        {
            return sci::Fail(sci::FromHResult(hr, "Writing " + path));
        }
        return sci::Ok();
    }
}

sci::Status WriteCompiledResource(CResourceMap &resourceMap, const CompileWriteOptions &options, ResourceType type, uint16_t number, const std::vector<uint8_t> &data)
{
    const GameFolderHelper &helper = resourceMap.Helper();
    // Also for a dry run, so that it fails where a real run fails (review of
    // S1).
    SCI_TRY(CheckResourceSize(helper.Version, (DWORD)data.size(), type));
    if (options.raw && options.outDir.empty())
    {
        // Before, the raw option was ignored with no message (review of S1).
        return sci::Fail(sci::ErrorCode::Usage, "raw files need an output folder");
    }
    if (!options.writeResources)
    {
        // A dry run of a batch lists what a real run would write, for the
        // patch-file check before its commit (review of 4247f34c).
        if (options.staged)
        {
            options.staged->push_back({ type, number, data });
        }
        return sci::Ok();
    }
    if (options.outDir.empty())
    {
        ResourceBlob blob(helper, nullptr, type, data, helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, helper.GetSaveSourceFlags(options.saveTo));
        return resourceMap.WriteResource(blob);
    }

    if (options.saveTo == ResourceSaveLocation::Package)
    {
        return sci::Fail(sci::ErrorCode::Usage, "an output folder takes patch files, not the package");
    }
    if (options.staged)
    {
        // The batch writes it at its commit (review of 5f545221).
        options.staged->push_back({ type, number, data });
        return sci::Ok();
    }
    return WriteOutputFile(helper, options, type, number, data);
}

sci::Status WriteStagedOutputFiles(const GameFolderHelper &helper, const CompileWriteOptions &options, const std::vector<StagedOutputFile> &files)
{
    // Every file that is there already must open for writing, so that a
    // read-only file, or a file that another program holds, fails the
    // commit before the first write. The write replaces a file with
    // CREATE_ALWAYS and FILE_ATTRIBUTE_NORMAL, which Windows refuses for a
    // hidden or system file, so the check refuses them too (review of
    // 4247f34c). The check shares the file as the write does: a raw write
    // shares read and write (WriteBytesToFile), a patch file nothing.
    for (const StagedOutputFile &file : files)
    {
        std::string path = OutputPathOf(helper, options, file.type, file.number);
        DWORD attributes = GetFileAttributesA(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            continue;
        }
        if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            return sci::Fail(sci::ErrorCode::Io, "Writing " + path + ": a folder has this name");
        }
        if (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
        {
            return sci::Fail(sci::ErrorCode::Io, "Writing " + path + ": the file is hidden or a system file, which the write cannot replace");
        }
        DWORD share = options.raw ? (FILE_SHARE_READ | FILE_SHARE_WRITE) : 0;
        HANDLE handle = CreateFileA(path.c_str(), GENERIC_WRITE, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return sci::Fail(sci::FromWin32(GetLastError(), "Writing " + path));
        }
        CloseHandle(handle);
    }
    for (const StagedOutputFile &file : files)
    {
        SCI_TRY(WriteOutputFile(helper, options, file.type, file.number, file.data));
    }
    return sci::Ok();
}
