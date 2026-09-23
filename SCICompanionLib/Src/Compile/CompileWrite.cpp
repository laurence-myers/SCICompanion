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

sci::Status WriteCompiledResource(CResourceMap &resourceMap, const CompileWriteOptions &options, ResourceType type, uint16_t number, const std::vector<uint8_t> &data)
{
    if (!options.writeResources)
    {
        return sci::Ok();
    }
    const GameFolderHelper &helper = resourceMap.Helper();
    SCI_TRY(CheckResourceSize(helper.Version, (DWORD)data.size(), type));
    if (options.outDir.empty())
    {
        ResourceBlob blob(helper, nullptr, type, data, helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, helper.GetSaveSourceFlags(options.saveTo));
        return resourceMap.WriteResource(blob);
    }

    if (options.saveTo == ResourceSaveLocation::Package)
    {
        return sci::Fail(sci::ErrorCode::Usage, "an output folder takes patch files, not the package");
    }
    std::string path = options.outDir + "\\" + CompiledResourceFileName(type, number, helper.Version, options.raw);
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
