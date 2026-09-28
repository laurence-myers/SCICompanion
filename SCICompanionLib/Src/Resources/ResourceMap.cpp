/***************************************************************************
	Copyright (c) 2020 Philip Fortier

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
#include "AppState.h"
#include "ResourceContainer.h"
#include "ResourceMap.h"
#include "ScriptNameMap.h"
#include "ResourceRecency.h"
#include "SaveResourceDialog.h"
#include "RemoveScriptDialog.h"
#include "View.h"
#include "Cursor.h"
#include "Font.h"
#include "Pic.h"
#include "PicOperations.h"
#include "Text.h"
#include "Sound.h"
#include "Vocab000.h"
#include "PaletteOperations.h"
#include "Message.h"
#include "Audio.h"
#include "AudioMap.h"
#include "Sync.h"
#include "ResourceEntity.h"
#include "ResourceSources.h"
#include "CompiledScript.h"
#include "Disassembler.h"
#include "ResourceMapOperations.h"
#include "MessageHeaderFile.h"
#include "MessageSource.h"
#include "format.h"
#include "ResourceMapEvents.h"
#include "DebuggerThread.h"
#include "PostBuildThread.h"
#include "RunLogic.h"
#include "ResourceBlob.h"
#include "DependencyTracker.h"
#include "VersionDetectionHelper.h"
#include <filesystem>

using namespace std;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

std::string ResourceDisplayNameFromType(ResourceType type);

HRESULT copyfile(const string &destination, const string &source)
{
	return CopyFile(destination.c_str(), source.c_str(), FALSE) ? S_OK : ResultFromLastError();
}

void deletefile(const string &filename)
{
	if (PathFileExists(filename.c_str()))
	{
		if (!DeleteFile(filename.c_str()))
		{
			DWORD error = GetLastError();
			sci::ThrowWin32(error, "Deleting " + filename);
		}
	}
}

HRESULT SetFilePositionHelper(HANDLE hFile, DWORD dwPos)
{
	HRESULT hr;
	if (INVALID_SET_FILE_POINTER != SetFilePointer(hFile, dwPos, nullptr, FILE_BEGIN))
	{
		hr = S_OK;
	}
	else
	{
		hr = ResultFromLastError();
	}
	return hr;
}



HRESULT WriteResourceMapTerminatingBits(HANDLE hFileMap)
{
	HRESULT hr;
	// Write the terminating bits.
	DWORD cbWritten;
	RESOURCEMAPENTRY_SCI0 entryTerm;
	memset(&entryTerm, 0xff, sizeof(entryTerm));
	if (WriteFile(hFileMap, &entryTerm, sizeof(entryTerm), &cbWritten, nullptr) && (cbWritten == sizeof(entryTerm)))
	{
		hr = S_OK;
	}
	else
	{
		hr = ResultFromLastError();
	}
	return hr;
}

HRESULT TestForReadOnly(const string &filename)
{
	HRESULT hr = S_OK;
	DWORD dwAttribs = GetFileAttributes(filename.c_str());
	if (INVALID_FILE_ATTRIBUTES == dwAttribs)
	{
		hr = ResultFromLastError();
	}
	else if (dwAttribs & FILE_ATTRIBUTE_READONLY)
	{
		hr = HRESULT_FROM_WIN32(ERROR_FILE_READ_ONLY);
	}
	return hr;
}

HRESULT TestDelete(const string &filename)
{
	HRESULT hr = S_OK;
	HANDLE hFile = CreateFile(filename.c_str(), DELETE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFile);
	}
	else
	{
		hr = ResultFromLastError();
	}
	return hr;
}

bool IsValidPackageNumber(int iPackageNumber)
{
	return (iPackageNumber >= 0) && (iPackageNumber < 63);
}


ResourceTypeFlags ResourceTypeToFlag(ResourceType dwType)
{
	if (((int)dwType < 0) || ((int)dwType >= NumResourceTypes))
	{
		// A corrupt map can carry a type byte outside the known range. Shifting by
		// that amount is undefined behaviour and can alias a real type's flag, so
		// map it to None. Callers that filter on the result then skip the entry.
		return ResourceTypeFlags::None;
	}
	return (ResourceTypeFlags)(1 << (int)dwType);
}

ResourceType ResourceFlagToType(ResourceTypeFlags dwFlags)
{
	uint32_t dwType = (uint32_t)dwFlags;
	int iShifts = 0;
	while (dwType > 1)
	{
		dwType = dwType >> 1;
		iShifts++;
	}
	return (ResourceType)iShifts;
}

HRESULT RebuildResources(const GameFolderHelper &helper, SCIVersion version, BOOL fShowUI, ResourceSaveLocation saveLocation, std::map<ResourceType, RebuildStats> &stats)
{
	try
	{
		// Do the audio stuff first, because it will end up adding new audio maps to the game's resources
		// (and RebuildResource should clean out the old ones)
		if (version.AudioVolumeName != AudioVolumeName::None)
		{
			std::unique_ptr<ResourceSource> resourceSource = CreateResourceSource(ResourceTypeFlags::All, helper, ResourceSourceFlags::AudioCache);
			resourceSource->RebuildResources(true, *resourceSource, stats);
		}

		// Enumerate resources and write the ones we have not already encountered.
		std::unique_ptr<ResourceSource> resourceSource = CreateResourceSource(ResourceTypeFlags::All, helper, ResourceSourceFlags::ResourceMap);
		ResourceSource *theActualSource = resourceSource.get();
		std::unique_ptr<ResourceSource> patchFileSource;
		if (saveLocation == ResourceSaveLocation::Patch)
		{
			// If this project saves to patch files by default, then we should use patch files as the source for rebuilding the resource package.
			patchFileSource = CreateResourceSource(ResourceTypeFlags::All, helper, ResourceSourceFlags::PatchFile);
			theActualSource = patchFileSource.get();
		}
		resourceSource->RebuildResources(true, *theActualSource, stats);

		if (version.MessageMapSource != MessageMapSource::Included)
		{
			ResourceSourceFlags sourceFlags = (version.MessageMapSource == MessageMapSource::MessageMap) ? ResourceSourceFlags::MessageMap : ResourceSourceFlags::AltMap;
			std::unique_ptr<ResourceSource> messageSource = CreateResourceSource(ResourceTypeFlags::All, helper, ResourceSourceFlags::MessageMap);
			messageSource->RebuildResources(true, *messageSource, stats);
		}
	}
	catch (std::exception &e)
	{
		SafeMessageBox(e.what(), MB_OK | MB_ICONWARNING);
	}
	return S_OK;
}

//
// CResourceMap
// Helper class for managing resources.
//
CResourceMap::CResourceMap(ISCIAppServices *appServices, ResourceRecency *resourceRecency) : _appServices(appServices), _resourceRecency(resourceRecency)
{
	_runLogic = std::make_unique<RunLogic>();
	_paletteListNeedsUpdate = true;
	_skipVersionSniffOnce = false;
	_pVocab000 = nullptr;
	_gameFolderHelper.Version = sciVersion0;	// By default
	_deferredResources.reserve(300);			// So we don't need to resize much it when adding
	_emptyPalette = std::make_unique<PaletteComponent>();
	memset(_emptyPalette->Colors, 0, sizeof(_emptyPalette->Colors));
}

CResourceMap::~CResourceMap()
{
	assert(_syncs.empty()); // They should remove themselves.
	assert(_deferLevels.empty());
}

//
// Adds the name of the resource to the game.ini file.
//
void CResourceMap::AssignName(const ResourceBlob &resource)
{
	// Assign the name of the item.
	std::string keyName = default_reskey(resource.GetNumber(), resource.GetHeader().Base36Number);
	std::string name = resource.GetName();
	if (!name.empty() && (0 != lstrcmpi(keyName.c_str(), name.c_str())))
	{
		Helper().SetIniString(g_resourceInfo[(int)resource.GetType()].pszTitleDefault, keyName, name);
	}
}

void CResourceMap::AssignName(ResourceType type, int iResourceNumber, uint32_t base36Number, PCTSTR pszName)
{
	// Assign the name of the item.
	std::string keyName = default_reskey(iResourceNumber, base36Number);
	std::string newValue;
	if (pszName)
	{
		newValue = pszName;
	}
	if (0 != lstrcmpi(keyName.c_str(), newValue.c_str()))
	{
		Helper().SetIniString(g_resourceInfo[(int)type].pszTitleDefault, keyName, newValue);
	}
}

bool CResourceMap::DeferLevel::HasReplaced(size_t index) const
{
	return std::any_of(replaced.begin(), replaced.end(),
		[index](const std::pair<size_t, std::unique_ptr<ResourceBlob>> &entry) { return entry.first == index; });
}

void CResourceMap::BeginDeferAppend()
{
	ASSERT(!_deferLevels.empty() || _deferredResources.empty());
	DeferLevel level;
	level.queuedAtStart = _deferredResources.size();
	_deferLevels.push_back(std::move(level));
}

void CResourceMap::AbandonAppend()
{
	// Withdraw what this level queued, and put back the copies from before
	// this level that it replaced. For the outermost level, the queue is then
	// empty. Nothing here allocates: this runs in a destructor.
	if (_deferLevels.empty())
	{
		return;
	}
	DeferLevel &level = _deferLevels.back();
	// The queue is never shorter than a level's start; check it anyway.
	size_t start = (level.queuedAtStart < _deferredResources.size()) ? level.queuedAtStart : _deferredResources.size();
	_deferredResources.erase(_deferredResources.begin() + start, _deferredResources.end());
	for (auto &replaced : level.replaced)
	{
		if (replaced.first < _deferredResources.size())
		{
			_deferredResources[replaced.first] = std::move(replaced.second);
		}
	}
	_deferLevels.pop_back();
}

namespace
{
	// "the resource package", "the audio cache", ...
	std::string _DescribeDestination(ResourceSourceFlags sourceFlags)
	{
		if (IsFlagSet(sourceFlags, ResourceSourceFlags::AudioCache) || IsFlagSet(sourceFlags, ResourceSourceFlags::AudioMapCache))
		{
			return "the audio cache";
		}
		if (IsFlagSet(sourceFlags, ResourceSourceFlags::PatchFile))
		{
			return "patch files";
		}
		if (IsFlagSet(sourceFlags, ResourceSourceFlags::MessageMap))
		{
			return "the message package";
		}
		if (IsFlagSet(sourceFlags, ResourceSourceFlags::Aud) || IsFlagSet(sourceFlags, ResourceSourceFlags::Sfx))
		{
			return "the audio volume";
		}
		return "the resource package";
	}

	// "writing Text 901 to the patch file 901.tex", "writing Script 110 to the resource package"
	std::string _DescribeWrite(const ResourceBlob &blob)
	{
		std::string destination = (blob.GetSourceFlags() == ResourceSourceFlags::PatchFile) ?
			("the patch file " + GetFileNameFor(blob)) : _DescribeDestination(blob.GetSourceFlags());
		return fmt::format("writing {0} {1} to {2}", GetResourceTypeTitle(blob.GetType()), blob.GetNumber(), destination);
	}

	// One resource as _DescribeWrite does; more as "writing 3 resources to
	// patch files". The error message names the file that failed.
	std::string _DescribeBucket(const std::vector<const ResourceBlob*> &blobs)
	{
		if (blobs.size() == 1)
		{
			return _DescribeWrite(*blobs[0]);
		}
		return fmt::format("writing {0} resources to {1}", blobs.size(), _DescribeDestination(blobs[0]->GetSourceFlags()));
	}

	// The same resource for the same destination: a second copy in one batch
	// replaces the first. Two copies would give the map two entries (SCI1) or
	// keep the older copy (SCI0).
	bool _IsSameQueuedResource(const ResourceBlob &a, const ResourceBlob &b)
	{
		return (a.GetType() == b.GetType()) && (a.GetNumber() == b.GetNumber()) &&
			(a.GetBase36() == b.GetBase36()) && (a.GetSourceFlags() == b.GetSourceFlags());
	}
}

sci::Status CResourceMap::EndDeferAppend()
{
	if (_deferLevels.empty())
	{
		return sci::Ok();
	}
	DeferLevel level = std::move(_deferLevels.back());
	_deferLevels.pop_back();
	if (!_deferLevels.empty())
	{
		// An inner batch: the outermost Commit writes the queue. If the outer
		// level is abandoned, it must still put back the copies that this
		// level replaced and that were queued before the outer level opened.
		return sci::Guard("closing a nested resource batch", [&]() -> sci::Status
		{
			DeferLevel &outer = _deferLevels.back();
			for (auto &replaced : level.replaced)
			{
				if ((replaced.first < outer.queuedAtStart) && !outer.HasReplaced(replaced.first))
				{
					outer.replaced.push_back(std::move(replaced));
				}
			}
			return sci::Ok();
		});
	}

	// Take the queue first, so a failure below cannot leave it behind.
	std::vector<std::unique_ptr<ResourceBlob>> queued;
	queued.swap(_deferredResources);
	if (queued.empty())
	{
		return sci::Ok();
	}

	sci::Status firstFailure = sci::Ok();
	std::vector<const ResourceBlob*> written;
	sci::Status writeStatus = sci::Guard("writing the queued resources", [&]() -> sci::Status
	{
		// Bucketize the resources by resource source and map context. Each
		// bucket is one rewrite of its destination.
		std::map<uint64_t, std::vector<const ResourceBlob*>> keyToIndices;
		for (const auto &blob : queued)
		{
			uint32_t mapContext = (uint32_t)((blob->GetBase36() == NoBase36) ? -1 : blob->GetNumber());
			uint64_t bucketKey = (uint64_t)blob->GetSourceFlags() | (((uint64_t)mapContext) << 32);
			keyToIndices[bucketKey].push_back(blob.get());
		}

		for (auto &pair : keyToIndices)
		{
			std::vector<const ResourceBlob*> &blobsForThisSource = pair.second;
			ResourceSourceFlags sourceFlags = blobsForThisSource[0]->GetSourceFlags();
			int mapContext = (blobsForThisSource[0]->GetBase36() == NoBase36) ? -1 : blobsForThisSource[0]->GetNumber();
			sci::Status status = sci::Guard(_DescribeBucket(blobsForThisSource), [&]() -> sci::Status
			{
				std::unique_ptr<ResourceSource> resourceSource = CreateResourceSource(ResourceTypeFlags::All, _gameFolderHelper, sourceFlags, ResourceSourceAccessFlags::ReadWrite, mapContext);
				if (!resourceSource)
				{
					return sci::Fail(sci::ErrorCode::Unsupported, "no writer for this destination");
				}
				resourceSource->AppendResources(blobsForThisSource);
				return sci::Ok();
			});
			if (status)
			{
				written.insert(written.end(), blobsForThisSource.begin(), blobsForThisSource.end());
			}
			else if (firstFailure)
			{
				firstFailure = status;
			}
		}
		return sci::Ok();
	});
	if (!writeStatus && firstFailure)
	{
		firstFailure = writeStatus;
	}

	// Name only what was written; a failed write must not change game.ini.
	sci::Status nameStatus = sci::Guard("naming the written resources in game.ini", [&]() -> sci::Status
	{
		for (const ResourceBlob *blob : written)
		{
			AssignName(*blob);
		}
		return sci::Ok();
	});
	if (!nameStatus && firstFailure)
	{
		firstFailure = nameStatus;
	}

	// Tell the views to reload every queued type, also after a failure: a
	// destination that failed can be partly written.
	sci::Status reloadStatus = sci::Guard("reloading the resource views", [&]() -> sci::Status
	{
		bool reload[NumResourceTypes] = {};
		for (const auto &blob : queued)
		{
			int type = (int)blob->GetType();
			if ((type >= 0) && (type < NumResourceTypes))
			{
				reload[type] = true;
			}
		}
		for (int iType = 0; iType < ARRAYSIZE(reload); iType++)
		{
			if (reload[iType])
			{
				NotifyToReloadResourceType((ResourceType)iType);
			}
		}
		return sci::Ok();
	});
	if (!reloadStatus && firstFailure)
	{
		firstFailure = reloadStatus;
	}
	return firstFailure;
}

void ShowWriteError(const sci::Status &status)
{
	if (!status && (status.error().code != sci::ErrorCode::Cancelled))
	{
		SafeMessageBox(status.error().ToString(), MB_OK | MB_ICONWARNING);
	}
}

//
// Suggest an unused resource number for this type.
//
int CResourceMap::SuggestResourceNumber(ResourceType type)
{
	int iNumber = 0;
	// Figure out a number to suggest...
	vector<bool> rgNumbers(1000, false);

	auto resourceContainer = Resources(ResourceTypeToFlag(type), Helper().GetDefaultEnumFlags() | ResourceEnumFlags::MostRecentOnly);
	for (auto &blobIt = resourceContainer->begin(); blobIt != resourceContainer->end(); ++blobIt)
	{
		int iThisNumber = blobIt.GetResourceNumber();
		if (iThisNumber >= 0 && iThisNumber < (int)rgNumbers.size())
		{
			rgNumbers[iThisNumber] = true;
		}
	}

	// Find the first one that is still false
	// (iterators on c style arrays are pointers to those arrays)
	auto result = find(rgNumbers.begin(), rgNumbers.end(), false);
	iNumber = (int)(result - rgNumbers.begin());
	return iNumber;
}

bool CResourceMap::IsResourceCompatible(const ResourceBlob &resource)
{
	return Helper().IsResourceCompatible(resource);
}

void CResourceMap::StartPostBuildThread()
{
	AbortPostBuildThread();
	_postBuildThread = CreatePostBuildThread(Helper().GameFolder);
}

void CResourceMap::AbortPostBuildThread()
{
	if (_postBuildThread)
	{
		_postBuildThread->Abort();
		_postBuildThread.reset();
	}
}

void CResourceMap::PokeResourceMapReloaded()
{
	// Refresh everything.
	for_each(_syncs.begin(), _syncs.end(), bind2nd(mem_fun(&IResourceMapEvents::OnResourceMapReloaded), false));
}

void CResourceMap::StartDebuggerThread(int optionalResourceNumber)
{
	AbortDebuggerThread();
	_debuggerThread = CreateDebuggerThread(Helper().GameFolder, optionalResourceNumber);
}
 
void CResourceMap::RepackageAudio(bool force)
{
	// Rebuild any out-of-date audio resources. This should nearly be a no-op if none are out of data.
	// TODO: This could be slow, so provide some kind of UI feedback?
	if (GetSCIVersion().AudioVolumeName != AudioVolumeName::None)
	{
		std::map<ResourceType, RebuildStats> stats;
		std::unique_ptr<ResourceSource> resourceSource = CreateResourceSource(ResourceTypeFlags::All, Helper(), ResourceSourceFlags::AudioCache);
		resourceSource->RebuildResources(force, *resourceSource, stats);
	}
}

void CResourceMap::AbortDebuggerThread()
{
	if (_debuggerThread)
	{
		_debuggerThread->Abort();
		_debuggerThread.reset();
	}
}

void CResourceMap::AppendResourceAskForNumber(ResourceEntity &resource)
{
	AppendResourceAskForNumber(resource, "", false);
}

void CResourceMap::AppendResourceAskForNumber(ResourceEntity &resource, const std::string &name, bool warnOnOverwrite)
{
	// Invoke dialog to suggest/ask for a resource number
	SaveResourceDialog srd(warnOnOverwrite, resource.GetType());
	srd.Init(-1, SuggestResourceNumber(resource.GetType()), name);
	if (IDOK == srd.DoModal())
	{
		// Assign it.
		resource.ResourceNumber = srd.GetResourceNumber();
		resource.PackageNumber = srd.GetPackageNumber();
		AssignName(resource.GetType(), resource.ResourceNumber, NoBase36, srd.GetName().c_str());
		AppendResource(resource);
	}
}

//
// Ask the user where to save the resource... and then save it.
//
HRESULT CResourceMap::AppendResourceAskForNumber(ResourceBlob &resource, bool warnOnOverwrite)
{
	if (!IsVersionCompatible(resource.GetType(), resource.GetVersion(), GetSCIVersion()))
	{
		if (IDNO == AfxMessageBox("The version of the resource being added does not match the version of the game.\nAdding it might cause the game to be corrupted.\nDo you want to go ahead anyway?", MB_YESNO))
		{
			return E_FAIL;
		}
	}
	// Invoke dialog to suggest/ask for a resource number
	SaveResourceDialog srd(warnOnOverwrite, resource.GetType());
	srd.Init(-1, SuggestResourceNumber(resource.GetType()), resource.GetName());
	if (IDOK == srd.DoModal())
	{
		// Assign it.
		resource.SetNumber(srd.GetResourceNumber());
		resource.SetPackage(srd.GetPackageNumber());
		resource.SetName(nullptr);
		if (!srd.GetName().empty())
		{
			resource.SetName(srd.GetName().c_str());
		}

		// Save it.
		return AppendResource(resource);
	}
	else
	{
		return E_FAIL; // User cancelled.
	}
}

ResourceSaveLocation CResourceMap::GetDefaultResourceSaveLocation()
{
	return Helper().GetResourceSaveLocation(ResourceSaveLocation::Default);
}

// #144: the passed blob's header is NOT updated to describe what was written.
// AppendResources writes a corrected header to the volume (compression method 0,
// compressed length equal to the decompressed length, the source version -- see
// MapAndPackageSource::AppendResources), but it works on a local copy of the header.
// The blob object still carries its source header (its original compression method
// and compressed length). So the blob is stale after this call: do not reuse it to,
// for example, locate and delete the resource by header comparison. Re-read the
// resource from the map instead. The blob is only valid for the length of this call.
sci::Status CResourceMap::WriteResource(const ResourceBlob &resource)
{
	if (!_deferLevels.empty())
	{
		// If resource appends are deferred, then just add it to the queue.
		return sci::Guard("queuing a resource write", [&]() -> sci::Status
		{
			DeferLevel &level = _deferLevels.back();
			std::unique_ptr<ResourceBlob> copy = std::make_unique<ResourceBlob>(resource);
			for (size_t i = 0; i < _deferredResources.size(); i++)
			{
				if (_IsSameQueuedResource(*_deferredResources[i], resource))
				{
					// Keep a copy that was queued before this level, so that
					// abandoning this level can put it back.
					if ((i < level.queuedAtStart) && !level.HasReplaced(i))
					{
						level.replaced.emplace_back(i, std::move(_deferredResources[i]));
					}
					_deferredResources[i] = std::move(copy);
					return sci::Ok();
				}
			}
			_deferredResources.push_back(std::move(copy));
			return sci::Ok();
		});
	}

	AppendBehavior appendBehavior = AppendBehavior::Append;
	sci::Status status = sci::Guard("writing a resource", [&]() -> sci::Status
	{
		return sci::Guard(_DescribeWrite(resource), [&]() -> sci::Status
		{
			int mapContext = (resource.GetBase36() == NoBase36) ? -1 : resource.GetNumber();
			std::unique_ptr<ResourceSource> resourceSource = CreateResourceSource(ResourceTypeFlags::All, _gameFolderHelper, resource.GetSourceFlags(), ResourceSourceAccessFlags::ReadWrite, mapContext);
			if (!resourceSource)
			{
				return sci::Fail(sci::ErrorCode::Unsupported, "no writer for this destination");
			}
			std::vector<const ResourceBlob*> blobs;
			blobs.push_back(&resource);
			appendBehavior = resourceSource->AppendResources(blobs);
			return sci::Ok();
		});
	});
	if (!status)
	{
		// A failed write must not change game.ini.
		return status;
	}

	return sci::Guard("naming the written resource and updating the views", [&]() -> sci::Status
	{
		if (_appServices && (resource.GetType() == ResourceType::View))
		{
			_appServices->SetRecentlyInteractedView(resource.GetNumber());
		}

		AssignName(resource);

		if (resource.GetType() == ResourceType::Script)
		{
			// We'll need to re-gen this:
			_globalCompiledScriptLookups.reset(nullptr);
		}

		if (resource.GetType() == ResourceType::Palette)
		{
			_paletteListNeedsUpdate = true;
			if (resource.GetNumber() == 999)
			{
				_pPalette999.reset(nullptr);
			}
		}

		// pResource is only valid for the length of this call.  Nonetheless, call our syncs
		for (auto &sync : _syncs)
		{
			sync->OnResourceAdded(&resource, appendBehavior);
		}
		return sci::Ok();
	});
}

HRESULT CResourceMap::AppendResource(const ResourceBlob &resource)
{
	sci::Status status = WriteResource(resource);
	if (!status)
	{
		ShowWriteError(status);
		return E_FAIL;
	}
	return S_OK;
}

bool CResourceMap::AppendResource(const ResourceEntity &resource, int *pChecksum)
{
	return AppendResource(resource, resource.PackageNumber, resource.ResourceNumber, "", resource.Base36Number, pChecksum);
}

sci::Status CResourceMap::WriteResource(const ResourceEntity &resource, int packageNumber, int resourceNumber, const std::string &name, uint32_t base36Number, int *pChecksum)
{
	ResourceBlob data;
	std::string context = fmt::format("preparing {0} {1}", GetResourceTypeTitle(resource.GetType()), resourceNumber);
	SCI_TRY(sci::Guard(context, [&]() -> sci::Status
	{
		if (!resource.PerformChecks())
		{
			// The check has already told the user why: a message, or a
			// question that the user answered "no" (with no GUI, the answer
			// is always "no"). Some checks are not a choice, for example
			// duplicate message tuples; they give Cancelled too.
			return sci::Fail(sci::ErrorCode::Cancelled, "the resource did not pass its checks");
		}

		sci::ostream serial;
		resource.WriteTo(serial, true, resourceNumber, data.GetPropertyBag());
		// The maximum resource size grows with the SCI version, as the map and
		// package size and offset fields widened. Audio has its own map format.
		SCI_TRY(sci::WithContext(CheckResourceSize(Helper().Version, serial.tellp(), resource.GetType()), context));

		sci::istream readStream = istream_from_ostream(serial);
		ResourceSourceFlags sourceFlags = resource.SourceFlags;
		if (sourceFlags == ResourceSourceFlags::Invalid)
		{
			sourceFlags = (GetDefaultResourceSaveLocation() == ResourceSaveLocation::Patch) ? ResourceSourceFlags::PatchFile : ResourceSourceFlags::ResourceMap;
		}
		HRESULT hr = data.CreateFromBits(Helper(), nullptr, resource.GetType(), &readStream, packageNumber, resourceNumber, base36Number, _gameFolderHelper.Version, sourceFlags);
		if (FAILED(hr))
		{
			// Only a null stream fails, so this is a bug.
			return sci::Fail(sci::ErrorCode::Internal, "could not make the resource data");
		}
		if (!name.empty())
		{
			data.SetName(name.c_str());
		}
		return sci::Ok();
	}));

	sci::Status status = WriteResource(data);
	if (pChecksum)
	{
		*pChecksum = data.GetChecksum();
	}
	return status;
}

sci::Status CResourceMap::WriteResource(const ResourceEntity &resource, int *pChecksum)
{
	return WriteResource(resource, resource.PackageNumber, resource.ResourceNumber, "", resource.Base36Number, pChecksum);
}

bool CResourceMap::AppendResource(const ResourceEntity &resource, int packageNumber, int resourceNumber, const std::string &name, uint32_t base36Number, int *pChecksum)
{
	sci::Status status = WriteResource(resource, packageNumber, resourceNumber, name, base36Number, pChecksum);
	ShowWriteError(status);
	return status.has_value();
}

std::unique_ptr<ResourceContainer> CResourceMap::Resources(ResourceTypeFlags types, ResourceEnumFlags enumFlags, int mapContext)
{
	ResourceRecency *pRecency = nullptr;
	if (_resourceRecency && ((enumFlags & ResourceEnumFlags::CalculateRecency) != ResourceEnumFlags::None))
	{
		pRecency = _resourceRecency;
		for (int i = 0; i < NumResourceTypes; i++)
		{
			if ((int)types & (1 << i))
			{
				// This resource type is being re-enumerated.
				pRecency->ClearResourceType(i);
			}
		}
	}

	return Helper().Resources(types, enumFlags, pRecency, mapContext);
}

bool CResourceMap::DoesResourceExist(ResourceType type, int number, std::string *retrieveName, ResourceSaveLocation location) const
{
	return Helper().DoesResourceExist(type, number, retrieveName, location);
}

std::unique_ptr<ResourceBlob> CResourceMap::MostRecentResource(ResourceType type, int number, bool getName, uint32_t base36Number, int mapContext)
{
	ResourceEnumFlags flags = ResourceEnumFlags::AddInDefaultEnumFlags;
	if (getName)
	{
		flags |= ResourceEnumFlags::NameLookups;
	}
	return Helper().MostRecentResource(type, number, flags, base36Number, mapContext);
}

void CResourceMap::_SniffSCIVersion()
{
	if (_skipVersionSniffOnce)
	{
		_skipVersionSniffOnce = false;
		return;
	}

	SniffSCIVersion(_gameFolderHelper);
}

void CResourceMap::NotifyToRegenerateImages()
{
	for_each(_syncs.begin(), _syncs.end(), mem_fun(&IResourceMapEvents::OnImagesInvalidated));
}

void CResourceMap::NotifyToReloadResourceType(ResourceType iType)
{
	for_each(_syncs.begin(), _syncs.end(), bind2nd(mem_fun(&IResourceMapEvents::OnResourceTypeReloaded), iType));
	if (iType == ResourceType::Palette)
	{
		_paletteListNeedsUpdate = true;
	}

	if (iType == ResourceType::Script)
	{
		// We'll need to re-gen this:
		_globalCompiledScriptLookups.reset(nullptr);
	}
}

//
// Add a listener for events.
//
void CResourceMap::AddSync(IResourceMapEvents *pSync)
{
	_syncs.push_back(pSync);
}

//
// Before your object gets destroyed, it MUST remove itself
//
void CResourceMap::RemoveSync(IResourceMapEvents *pSync)
{
	// We wouldn't expect to remove a sync that isn't there, so we can just erase
	// whatever we find (which should be a valid iterator)
	_syncs.erase(find(_syncs.begin(), _syncs.end(), pSync));
}

//
// Deletes the resource specified by pData, using the resource number, type and bits to compare.
//
void CResourceMap::DeleteResource(const ResourceBlob *pData)
{
	// Early bail out. Without palette 999, we'll mis-identify the SCI type, and things will be bad
	// (and also... we won't have a global palette)
	if ((pData->GetType() == ResourceType::Palette) && (pData->GetNumber() == 999))
	{
		AfxMessageBox("Palette 999 is the global palette and cannot be deleted.", MB_OK | MB_ICONERROR);
		return;
	}

	try
	{
		::DeleteResource(*this, *pData);
	}
	catch (std::exception &e)
	{
		SafeMessageBox(e.what(), MB_OK | MB_ICONWARNING);
	}

	// Call our syncs, so they update.
	if (pData->GetType() == ResourceType::Script)
	{
		// We'll need to re-gen this:
		_globalCompiledScriptLookups.reset(nullptr);
	}

	for_each(_syncs.begin(), _syncs.end(), bind2nd(mem_fun(&IResourceMapEvents::OnResourceDeleted), pData));
	if (pData->GetType() == ResourceType::Palette)
	{
		_paletteListNeedsUpdate = true;
	}
}

//
// Returns an empty string (or pszDefault) if there is no key
//
std::string GetIniString(const std::string &iniFileName, PCTSTR pszSectionName, const std::string &keyName, PCSTR pszDefault)
{
	std::string strRet;
	char sz[200];
	if (GetPrivateProfileString(pszSectionName, keyName.c_str(), nullptr, sz, (DWORD)ARRAYSIZE(sz), iniFileName.c_str()))
	{
		strRet = sz;
	}
	else
	{
		strRet = pszDefault;
	}
	return strRet;
}

std::string CResourceMap::GetGameFolder() const
{
	return _gameFolderHelper.GameFolder;
}

//
// Gets the include folder that has read-only headers
//
std::string CResourceMap::GetIncludeFolder() const
{
	if (_dataFolder.empty())
	{
		return _gameFolderHelper.GetIncludeFolder();
	}
	return _dataFolder + "include";
}

void CResourceMap::SetDataFolder(const std::string &folder)
{
	_dataFolder = folder;
	if (!_dataFolder.empty() && (_dataFolder.back() != '\\') && (_dataFolder.back() != '/'))
	{
		_dataFolder += "\\";
	}
}

//
// Gets the include folder that has read-only headers
//
std::string CResourceMap::GetTemplateFolder()
{
	return GetExeSubFolder("TemplateGame");
}

std::string CResourceMap::GetTopLevelSamplesFolder()
{
	return GetExeSubFolder("Samples");
}

//
// Gets the samples folder 
//
std::string CResourceMap::GetSamplesFolder()
{
	std::string folder = GetExeSubFolder("Samples");
	if (GetSCIVersion().MapFormat == ResourceMapFormat::SCI0)
	{
		folder += "\\SCI0";
	}
	else
	{
		folder += "\\SCI1.1";
	}
	return folder;
}


//
// Gets the objects folder 
//
std::string CResourceMap::GetObjectsFolder()
{
	std::string folder = GetExeSubFolder("Objects");
	if (GetSCIVersion().MapFormat == ResourceMapFormat::SCI0)
	{
		folder += "\\SCI0";
	}
	else
	{
		folder += "\\SCI1.1";
	}
	return folder;
}

std::string CResourceMap::GetDecompilerFolder()
{
	return _dataFolder.empty() ? GetExeSubFolder("Decompiler") : (_dataFolder + "Decompiler");
}

bool hasEnding(std::string const &fullString, std::string const &ending) {
	if (fullString.length() >= ending.length()) {
		return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
	}
	else {
		return false;
	}
}

std::string CResourceMap::GetIncludePath(const std::string &includeFileName)
{
	if (hasEnding(includeFileName, ".shm"))
	{
		return Helper().GetMsgFolder() + "\\" + includeFileName;
	}
	else if (hasEnding(includeFileName, ".shp"))
	{
		return Helper().GetPolyFolder() + "\\" + includeFileName;
	}
	else
	{
		std::string includeFolder = GetIncludeFolder();
		if (!includeFolder.empty())
		{
			includeFolder += "\\";
			includeFolder += includeFileName;
			if (PathFileExists(includeFolder.c_str()))
			{
				return includeFolder;
			}
		}
		includeFolder = Helper().GetSrcFolder();
		includeFolder += "\\";
		includeFolder += includeFileName;
		if (PathFileExists(includeFolder.c_str()))
		{
			return includeFolder;
		}
		return "";
	}
}


HRESULT CResourceMap::GetGameIni(PTSTR pszBuf, size_t cchBuf)
{
	HRESULT hr = E_FAIL;
	if (!_gameFolderHelper.GameFolder.empty())
	{
		hr = StringCchPrintf(pszBuf, cchBuf, TEXT("%s\\%s"), _gameFolderHelper.GameFolder.c_str(), TEXT("game.ini"));
	}
	return hr;
}

const SCIVersion &CResourceMap::GetSCIVersion() const
{
	return _gameFolderHelper.Version;
}

void CResourceMap::SetVersion(const SCIVersion &version)
{
	_gameFolderHelper.Version = version;
}

//
// Perf: we're opening and closing the file each time.  We could do this once.
//
std::string FigureOutResourceName(const std::string &iniFileName, ResourceType type, int iNumber, uint32_t base36Number)
{
	std::string name;
	if ((size_t)type < ARRAYSIZE(g_resourceInfo))
	{
		std::string keyName = default_reskey(iNumber, base36Number);
		name = GetIniString(iniFileName, GetResourceInfo(type).pszTitleDefault, keyName, keyName.c_str());
	}
	return name;
}

// Someone on the forums started hitting problems once they had over 350 scripts. Our size was 5000.
// This allows 4x that. Should be high enough.
const DWORD c_IniSectionMaxSize = 20000;

HRESULT CResourceMap::GetScriptNumber(ScriptId script, WORD &wScript)
{
	wScript = 0xffff;
	TCHAR szGameIni[MAX_PATH];
	HRESULT hr = GetGameIni(szGameIni, ARRAYSIZE(szGameIni));
	if (SUCCEEDED(hr))
	{
		hr = E_INVALIDARG;
		DWORD cchBuf = c_IniSectionMaxSize;
		std::unique_ptr<char[]> szNameValues = std::make_unique<char[]>(cchBuf);

		DWORD nLength = GetPrivateProfileSection(TEXT("Script"), szNameValues.get(), cchBuf, szGameIni);
		if (nLength > 0 && ((cchBuf - 2) != nLength)) // returns (cchNameValues - 2) in case of insufficient buffer 
		{
			TCHAR *psz = szNameValues.get();
			while(*psz && (FAILED(hr)))
			{
				size_t cch = strlen(psz);
				char *pszEq = StrChr(psz, TEXT('='));
				CString strTitle = script.GetTitle().c_str();
				if (pszEq && (0 == strTitle.CompareNoCase(pszEq + 1)))
				{
					// We have a match in script name... find the number
					TCHAR *pszNumber = StrChr(psz, TEXT('n'));
					if (pszNumber)
					{
						wScript = (WORD)StrToInt(pszNumber + 1);
						ASSERT(script.GetResourceNumber() == InvalidResourceNumber ||
							   script.GetResourceNumber() == wScript);
						hr = S_OK;
					}
				}
				// Advance to next string.
				psz += (cch + 1);
			}
		}
	}

	if (FAILED(hr) && script.GetResourceNumber() != InvalidResourceNumber)
	{
		wScript = script.GetResourceNumber();
		hr = S_OK;
	}

	return hr;
}

const Vocab000 *CResourceMap::GetVocab000()
{
	ResourceEntity *pResource = GetVocabResourceToEdit();
	if (pResource)
	{
		return pResource->TryGetComponent<Vocab000>();
	}
	return nullptr;
}

std::unique_ptr<PaletteComponent> CResourceMap::GetPalette(int fallbackPaletteNumber)
{
	std::unique_ptr<PaletteComponent> paletteReturn;
	if (fallbackPaletteNumber == 999)
	{
		const PaletteComponent *pc = GetPalette999();
		if (pc)
		{
			paletteReturn = make_unique<PaletteComponent>(*pc);
		}
	}
	else
	{
		std::unique_ptr<ResourceEntity> paletteFallback = CreateResourceFromNumber(ResourceType::Palette, fallbackPaletteNumber);
		if (paletteFallback && paletteFallback->TryGetComponent<PaletteComponent>())
		{
			paletteReturn = make_unique<PaletteComponent>(paletteFallback->GetComponent<PaletteComponent>());
		}
	}
	return paletteReturn;
}

std::unique_ptr<PaletteComponent> CResourceMap::GetMergedPalette(const ResourceEntity &resource, int fallbackPaletteNumber)
{
	// Resolve the global palette from the map (GetPalette999 lazily creates a cached
	// member; CreateResourceFromNumber walks the map), then merge. This map access is
	// why this overload must run on the UI thread; the render workers use the
	// overload below with a palette the UI thread precomputed for them (#97).
	if (fallbackPaletteNumber == 999)
	{
		return GetMergedPalette(resource, GetPalette999());
	}

	std::unique_ptr<ResourceEntity> paletteFallback = CreateResourceFromNumber(ResourceType::Palette, fallbackPaletteNumber);
	const PaletteComponent *globalPalette = paletteFallback ? paletteFallback->TryGetComponent<PaletteComponent>() : nullptr;
	return GetMergedPalette(resource, globalPalette);
}

std::unique_ptr<PaletteComponent> CResourceMap::GetMergedPalette(const ResourceEntity &resource, const PaletteComponent *globalPalette) const
{
	assert((_gameFolderHelper.Version.ViewFormat != ViewFormat::EGA) || (_gameFolderHelper.Version.PicFormat != PicFormat::EGA));
	// Thread-safe: reads only the passed resource, the caller-supplied globalPalette,
	// and the immutable _emptyPalette (set once in the constructor) -- no mutable
	// shared map state, so a render worker can call this concurrently with the UI
	// thread (#97). (The Debug-only assert above reads _gameFolderHelper.Version,
	// which is stable during rendering.) MergeFromOther is a no-op when globalPalette
	// is null.
	const PaletteComponent *paletteEmbedded = resource.TryGetComponent<PaletteComponent>();
	if (!paletteEmbedded)
	{
		paletteEmbedded = _emptyPalette.get();
	}

	// Clone the embedded palette first - REVIEW: make_unique arg forwarding doesn't work with copy constructor. No object copy happens.
	std::unique_ptr<PaletteComponent> paletteReturn = make_unique<PaletteComponent>(*paletteEmbedded);
	paletteReturn->MergeFromOther(globalPalette);
	return paletteReturn;
}

void CResourceMap::SaveAudioMap65535(const AudioMapComponent &newAudioMap, int mapContext)
{
	// Best idea is to get the existing one, modify, then save. That way we don't need to figure out where it came from.
	int number = mapContext;
	if (mapContext == -1)
	{
		number = newAudioMap.Traits.MainAudioMapResourceNumber;
	}
	std::unique_ptr<ResourceEntity> entity = CreateResourceFromNumber(ResourceType::AudioMap, number, NoBase36, mapContext);
	
	// Assign the new component to it.
	entity->RemoveComponent<AudioMapComponent>();
	entity->AddComponent<AudioMapComponent>(std::make_unique<AudioMapComponent>(newAudioMap));
	AppendResource(*entity);
}

const PaletteComponent *CResourceMap::GetPalette999()
{
	PaletteComponent *globalPalette = nullptr;
	if (_gameFolderHelper.Version.HasPalette)
	{
		if (!_pPalette999)
		{
			_pPalette999 = CreateResourceFromNumber(ResourceType::Palette, 999);
		}
		if (_pPalette999)
		{
			globalPalette = _pPalette999->TryGetComponent<PaletteComponent>();
		}
	}
	return globalPalette;
}

std::vector<int> CResourceMap::GetPaletteList()
{
	if (_paletteListNeedsUpdate)
	{
		_paletteListNeedsUpdate = false;
		_paletteList.clear();
		auto paletteContainer = Resources(ResourceTypeFlags::Palette, Helper().GetDefaultEnumFlags() | ResourceEnumFlags::MostRecentOnly);
		bool has999 = false;
		for (auto it = paletteContainer->begin(); it != paletteContainer->end(); ++it)
		{
			if (it.GetResourceNumber() == 999)
			{
				has999 = true;
			}
			else
			{
				_paletteList.push_back(it.GetResourceNumber());
			}
		}

		// Sort and put 999 first.
		std::sort(_paletteList.begin(), _paletteList.end());
		if (has999)
		{
			_paletteList.insert(_paletteList.begin(), 999);
		}
	}
	return _paletteList;
}

GlobalCompiledScriptLookups *CResourceMap::GetCompiledScriptLookups()
{
	if (!_globalCompiledScriptLookups)
	{
		_globalCompiledScriptLookups = make_unique<GlobalCompiledScriptLookups>();
		if (!_globalCompiledScriptLookups->Load(Helper()))
		{
			// Warning... (happens in LB Dagger)
		}
	}
	return _globalCompiledScriptLookups.get();
}

ResourceEntity *CResourceMap::GetVocabResourceToEdit()
{
	if (!_pVocab000)
	{
		_pVocab000 = CreateResourceFromNumber(ResourceType::Vocab, _gameFolderHelper.Version.MainVocabResource);
	}
	return _pVocab000.get();
}

void CResourceMap::ClearVocab000()
{
	_pVocab000.reset(nullptr);
}

HRESULT GetFilePositionHelper(HANDLE hFile, DWORD *pdwPos)
{
	HRESULT hr = S_OK;
	*pdwPos = SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);
	if (*pdwPos == INVALID_SET_FILE_POINTER)
	{
		// Might have failed...
		hr = ResultFromLastError();
	}
	return hr;
}

void CResourceMap::GetAllScripts(std::vector<ScriptId> &scripts)
{
	TCHAR szIniFile[MAX_PATH];
	if (SUCCEEDED(GetGameIni(szIniFile, ARRAYSIZE(szIniFile))))
	{
		DWORD cchBuf = c_IniSectionMaxSize;
		std::unique_ptr<char[]> szNameValues = std::make_unique<char[]>(cchBuf);
		DWORD nLength =  GetPrivateProfileSection(TEXT("Script"), szNameValues.get(), cchBuf, szIniFile);
		if (nLength > 0 && ((cchBuf - 2) != nLength)) // returns (cchBuf - 2) in case of insufficient buffer 
		{
			TCHAR *psz = szNameValues.get();
			while(*psz)
			{
				// The format is
				// n000=ScriptName
				size_t cch = strlen(psz);
				char *pszNumber = StrChr(psz, TEXT('n'));
				if (pszNumber)
				{
					char *pszEq = StrChr(pszNumber, TEXT('='));
					if (pszEq)
					{
						// Add this script...
						ScriptId scriptId(_gameFolderHelper.GetScriptFileName(pszEq + 1));
						// Isolate the number.
						*pszEq = 0;	 // n123
						pszNumber++;	// 123
						scriptId.SetResourceNumber(StrToInt(pszNumber));
						scripts.push_back(scriptId);
					}
				}
				// Advance to next string.
				psz += (cch + 1);
			}
		}
	}
}

void CResourceMap::SetScriptNames(std::shared_ptr<const ScriptNameMap> names)
{
	_gameFolderHelper.ScriptNames = std::move(names);
}

void CResourceMap::GetNumberToNameMap(std::unordered_map<WORD, std::string> &scos)
{
	if (_gameFolderHelper.ScriptNames)
	{
		// A session's names: game.ini when it exists, and the files of src\.
		for (const auto &entry : _gameFolderHelper.ScriptNames->Entries())
		{
			scos[entry.first] = entry.second.name;
		}
		return;
	}
	TCHAR szIniFile[MAX_PATH];
	if (SUCCEEDED(GetGameIni(szIniFile, ARRAYSIZE(szIniFile))))
	{
		DWORD cchBuf = c_IniSectionMaxSize;
		std::unique_ptr<char[]> szNameValues = std::make_unique<char[]>(cchBuf);
		DWORD nLength =  GetPrivateProfileSection(TEXT("Script"), szNameValues.get(), cchBuf, szIniFile);
		if (nLength > 0 && ((cchBuf - 2) != nLength)) // returns (cchBuf - 2) in case of insufficient buffer 
		{
			TCHAR *psz = szNameValues.get();
			while(*psz)
			{
				// The format is
				// n000=ScriptName
				size_t cch = strlen(psz);
				char *pszNumber = StrChr(psz, TEXT('n'));
				if (pszNumber)
				{
					++pszNumber; // Advance past the 'n'
					char *pszEq = StrChr(pszNumber, TEXT('='));
					if (pszEq)
					{
						scos[(WORD)StrToInt(pszNumber)] = pszEq + 1;
					}
				}
				// Advance to next string.
				psz += (cch + 1);
			}
		}
	}
}

bool CResourceMap::CanSaveResourcesToMap()
{
	return true;	// Now supported for all.
}

MessageSource *CResourceMap::GetVerbsMessageSource(bool reload)
{
	if (!_verbsHeaderFile || reload)
	{
		string messageFilename = "Verbs.sh";
		string messageFilePath = fmt::format("{0}\\{1}", Helper().GetSrcFolder(), messageFilename);
		_verbsHeaderFile = make_unique<MessageHeaderFile>(messageFilePath, messageFilename, initializer_list<string>({ "VERBS" }));
	}
	return _verbsHeaderFile->GetMessageSource();
}

MessageSource *CResourceMap::GetTalkersMessageSource(bool reload)
{
	if (!_talkersHeaderFile || reload)
	{
		string messageFilename = "Talkers.sh";
		string messageFilePath = fmt::format("{0}\\{1}", Helper().GetSrcFolder(), messageFilename);
		_talkersHeaderFile = make_unique<MessageHeaderFile>(messageFilePath, messageFilename, initializer_list<string>({}));
	}
	return _talkersHeaderFile->GetMessageSource();
}

RunLogic &CResourceMap::GetRunLogic()
{
	return *_runLogic;
}

//
// Called when we open a new game.
//
bool CResourceMap::IsResourceMapCorrupt()
{
	if (!IsGameLoaded())
	{
		return false;
	}
	try
	{
		std::unique_ptr<ResourceSource> resourceSource = CreateResourceSource(ResourceTypeFlags::All, _gameFolderHelper, ResourceSourceFlags::ResourceMap);
		return resourceSource && resourceSource->IsResourceMapCorrupt();
	}
	catch (std::exception &)
	{
		// Could not even open/read the map to check. Do not raise a false
		// "corrupt lookup table" alarm here; other open-path handling reports a
		// map that cannot be opened at all.
		return false;
	}
}

void CResourceMap::SetGameFolder(const string &gameFolder)
{
	sci::Status opened = _OpenGameFolder(gameFolder);
	if (!opened)
	{
		SafeMessageBox(fmt::format("Unable to open resource map: {0}", opened.error().message), MB_OK | MB_ICONWARNING);
		AfxThrowUserException();
	}
}

sci::Status CResourceMap::TryOpen(const std::string &gameFolder)
{
	if (gameFolder.empty())
	{
		// For SetGameFolder, an empty folder closes the game; here it is a mistake.
		return sci::Fail(sci::ErrorCode::Usage, "No game folder was given");
	}
	// The whole open is inside the exception boundary: the parts before and after the
	// version sniff can also throw (for example, out of memory).
	sci::Status opened = sci::Guard("opening the game in " + gameFolder, [&]() -> sci::Status
	{
		SCI_TRY(_OpenGameFolder(gameFolder));
		return _CheckResourceMap();
	});
	if (!opened)
	{
		// No game is open now.
		_gameFolderHelper.GameFolder = "";
	}
	return opened;
}

sci::Status CResourceMap::_CheckResourceMap()
{
	sci::ErrorLocation where;
	where.file = Helper().GameFolder + "\\resource.map";
	std::error_code ec;
	if (std::filesystem::file_size(where.file, ec) == 0)
	{
		return sci::Fail(sci::ErrorCode::Format, "resource.map is empty", where);
	}
	std::unique_ptr<ResourceSource> source = CreateResourceSource(ResourceTypeFlags::All, _gameFolderHelper, ResourceSourceFlags::ResourceMap);
	if (!source)
	{
		return sci::Fail(sci::ErrorCode::Unsupported, "the format of resource.map is not supported", where);
	}
	if (source->IsResourceMapCorrupt())
	{
		return sci::Fail(sci::ErrorCode::Format, "resource.map is damaged: its lookup table has no end", where);
	}
	if (source->IsResourceMapTruncated())
	{
		return sci::Fail(sci::ErrorCode::Format, "resource.map is damaged: the file ends before the end that its lookup table gives", where);
	}
	// A map is good when a volume file holds one of its first entries: the
	// header at the offset of the entry has the type and the number of the
	// entry. An empty map, or a map of other volumes, has no such entry.
	const int entriesToTry = 256;
	int tried = 0;
	IteratorState state;
	ResourceMapEntryAgnostic entry;
	while ((tried < entriesToTry) && source->ReadNextEntry(ResourceTypeFlags::All, state, entry, nullptr))
	{
		tried++;
		sci::Result<bool> held = sci::Guard("reading the header of an entry of resource.map", [&]() -> sci::Result<bool>
		{
			ResourceHeaderAgnostic header;
			sci::istream stream = source->GetHeaderAndPositionedStream(entry, header);
			return stream.good() && (header.Type == entry.Type) && ((uint16_t)header.Number == entry.Number);
		});
		if (held && *held)
		{
			return sci::Ok();
		}
	}
	return sci::Fail(sci::ErrorCode::Format, (tried == 0) ? std::string("resource.map is damaged or empty: it has no entry") :
		fmt::format("resource.map is damaged: no volume file holds any of its first {0} entries", tried), where);
}

sci::Status CResourceMap::_OpenGameFolder(const string &gameFolder)
{
	_runLogic->SetGameFolder(gameFolder);
	_gameFolderHelper.GameFolder = gameFolder;
	// The names of the game that was open before do not apply.
	_gameFolderHelper.ScriptNames.reset();
	// The text codepage comes from game.ini (437 when there is none).
	SetTextCodepage(gameFolder.empty() ? 437 : Helper().GetCodepage());
	_talkerToView = TalkerToViewMap(Helper().GetLipSyncFolder());
	ClearVocab000();
	_pPalette999.reset(nullptr);					// REVIEW: also do this if global palette is edited.
	_globalCompiledScriptLookups.reset(nullptr);
	_talkersHeaderFile.reset(nullptr);
	_verbsHeaderFile.reset(nullptr);
	if (!gameFolder.empty())
	{
		sci::Status status = sci::Guard("opening the game in " + gameFolder, [&]() -> sci::Status
		{
			// We get here when we close documents.
			_SniffSCIVersion();

			// Send initial load notification
			for_each(_syncs.begin(), _syncs.end(), bind2nd(mem_fun(&IResourceMapEvents::OnResourceMapReloaded), true));

			_paletteListNeedsUpdate = true;
			return sci::Ok();
		});
		if (!status)
		{
			// No game is open now.
			_gameFolderHelper.GameFolder = "";
			return status;
		}
	}

	AbortDebuggerThread();

	if (_appServices)
	{
		_appServices->OnGameFolderUpdate();
	}
	return sci::Ok();
}

TalkerToViewMap &CResourceMap::GetTalkerToViewMap()
{
	return _talkerToView;
}

// Returns null if it doesn't exist.
std::unique_ptr<ResourceEntity> CResourceMap::CreateResourceFromNumber(ResourceType type, int number, uint32_t base36Number, int mapContext)
{
	std::unique_ptr<ResourceEntity> pResource;
	unique_ptr<ResourceBlob> data = MostRecentResource(type, number, true, base36Number, mapContext);
	// This can legitimately fail. For instance, a script that hasn't yet been compiled.
	if (data)
	{
		pResource = CreateResourceFromResourceData(*data);
	}
	return pResource;
}

template<typename TCreateFunc, typename TFallbackFunc>
std::unique_ptr<ResourceEntity> CreateResourceHelper(const ResourceBlob &data, TCreateFunc createFunction, TFallbackFunc fallbackFunc, bool fallbackOnException)
{
	std::unique_ptr<ResourceEntity> pResourceReturn(createFunction(data.GetVersion()));
	try 
	{ 
		pResourceReturn->InitFromResource(&data); 
	} 
	catch (std::exception)
	{
		if (!fallbackOnException)
		{
			throw;
		}

		data.AddStatusFlags(ResourceLoadStatusFlags::ResourceCreationFailed);
		pResourceReturn.reset(fallbackFunc(data.GetVersion()));
		pResourceReturn->ResourceNumber = data.GetNumber();
		pResourceReturn->PackageNumber = data.GetPackageHint();
	}
	return pResourceReturn;
}

void DoNothing(ResourceEntity &resource) {}

//
// Given a ResourceBlob, this creates the SCI resource represented by the data, and hands back
// a ResourceEntity.
// If there is an exception creating the resource, a default one is handed back.
//
std::unique_ptr<ResourceEntity> CreateResourceFromResourceData(const ResourceBlob &data, bool fallbackOnException)
{
	switch (data.GetType())
	{
		case ResourceType::View:
			return CreateResourceHelper(data, CreateViewResource, CreateDefaultViewResource, fallbackOnException);
		case ResourceType::Font:
			return CreateResourceHelper(data, CreateFontResource, CreateDefaultFontResource, fallbackOnException);
		case ResourceType::Cursor:
			return CreateResourceHelper(data, CreateCursorResource, CreateDefaultCursorResource, fallbackOnException);
		case ResourceType::Text:
			return CreateResourceHelper(data, CreateTextResource, CreateDefaultTextResource, fallbackOnException);
		case ResourceType::Sound:
			return CreateResourceHelper(data, CreateSoundResource, CreateDefaultSoundResource, fallbackOnException);
		case ResourceType::Vocab:
			return CreateResourceHelper(data, CreateVocabResource, CreateVocabResource, fallbackOnException);
		case ResourceType::Pic:
			return CreateResourceHelper(data, CreatePicResource, CreateDefaultPicResource, fallbackOnException);
		case ResourceType::Palette:
			return CreateResourceHelper(data, CreatePaletteResource, CreatePaletteResource, fallbackOnException);
		case ResourceType::Message:
			return CreateResourceHelper(data, CreateMessageResource, CreateDefaultMessageResource, fallbackOnException);
		case ResourceType::Audio:
			if (data.GetVersion().AudioIsWav && (data.GetBase36() == NoBase36))
			{
				return CreateResourceHelper(data, CreateWaveAudioResource, CreateDefaultAudioResource, fallbackOnException);
			}
			else
			{
				return CreateResourceHelper(data, CreateAudioResource, CreateDefaultAudioResource, fallbackOnException);
			}
		case ResourceType::AudioMap:
			return CreateResourceHelper(data, CreateMapResource, CreateMapResource, fallbackOnException);
		default:
		assert(false);
		break;
	}
	return nullptr;
}

sci::Status CheckResourceData(const ResourceBlob &data)
{
	// A blob that delayed its decompression decompresses when its data is read.
	data.GetReadStream();
	sci::ErrorLocation where;
	where.resource = DescribeResource(data.GetType(), data.GetNumber());
	if (IsFlagSet(data.GetStatusFlags(), ResourceLoadStatusFlags::Corrupted))
	{
		return sci::Fail(sci::ErrorCode::Format, "the resource is damaged: its header or its data could not be read", where);
	}
	if (IsFlagSet(data.GetStatusFlags(), ResourceLoadStatusFlags::DecompressionFailed))
	{
		return sci::Fail(sci::ErrorCode::Format, "the resource data could not be decompressed", where);
	}
	return sci::Ok();
}

sci::Result<std::unique_ptr<ResourceEntity>> TryCreateResourceFromResourceData(const ResourceBlob &data)
{
	std::string resource = DescribeResource(data.GetType(), data.GetNumber());
	// The context does not name the resource: the location does.
	sci::Result<std::unique_ptr<ResourceEntity>> created = sci::Guard("the resource could not be read", [&]() -> sci::Result<std::unique_ptr<ResourceEntity>>
	{
		SCI_TRY(CheckResourceData(data));
		std::unique_ptr<ResourceEntity> entity = CreateResourceFromResourceData(data, false);
		if (!entity)
		{
			return sci::Fail(sci::ErrorCode::Unsupported, "this type of resource cannot be read");
		}
		return std::move(entity);
	});
	if (!created && created.error().where.resource.empty())
	{
		created.error().where.resource = resource;
	}
	return created;
}