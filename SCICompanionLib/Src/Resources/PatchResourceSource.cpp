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
#include "PatchResourceSource.h"
#include "ResourceBlob.h"

PatchFilesResourceSource::PatchFilesResourceSource(ResourceTypeFlags types, SCIVersion version, const std::string &gameFolder, ResourceSourceFlags sourceFlags) :
	_gameFolder(gameFolder),
	_gameFolderSpec(gameFolder + "\\*.*"),
	_hFind(INVALID_HANDLE_VALUE),
	_version(version),
	_stillMore(true),
	_sourceFlags(sourceFlags)
{
	// Prepare a filter against which to 
	uint32_t flags = (uint32_t)types;
	uint32_t resourceType = 0;
	while (flags)
	{
		if (flags & 0x1)
		{
			if (resourceType < ARRAYSIZE(g_szResourceSpecByType))
			{
				if (!_fileSpec.empty())
				{
					_fileSpec += ";";
				}
				_fileSpec += g_szResourceSpecByType[resourceType];
			}
		}
		flags >>= 1;
		resourceType++;
	}
}


bool PatchFilesResourceSource::ReadNextEntry(ResourceTypeFlags typeFlags, IteratorState &state, ResourceMapEntryAgnostic &entry, std::vector<uint8_t> *optionalRawData)
{
	if (_stillMore && (_hFind == INVALID_HANDLE_VALUE))
	{
		_hFind = FindFirstFile(_gameFolderSpec.c_str(), &_findData);
	}

	_stillMore = _stillMore && (_hFind != INVALID_HANDLE_VALUE);
	bool foundOne = false;
	while (_stillMore && !foundOne)
	{
		if (PathMatchSpec(_findData.cFileName, _fileSpec.c_str()))
		{
			int number = ResourceNumberFromFileName(_findData.cFileName);
			if (number != -1)
			{
				// We need a valid number.
				// We do need to peek open the file right now.
				ScopedHandle patchFile;
				std::string fullPath = _gameFolder + "\\" + _findData.cFileName;
				patchFile.hFile = CreateFile(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (patchFile.hFile != INVALID_HANDLE_VALUE)
				{
					// Read the first two bytes. The first is the type, the next is the offset.
					uint8_t word[2];
					DWORD cbRead;
					if (ReadFile(patchFile.hFile, &word, sizeof(word), &cbRead, nullptr) && (cbRead == sizeof(word)))
					{
						ResourceType type = (ResourceType)(word[0] & 0x7f);
						if (IsFlagSet(typeFlags, ResourceTypeToFlag(type)))
						{
							entry.Number = number;
							entry.Offset = GetResourceOffsetInFile(word[1]) + 2;	// For the word we just read.
							entry.Type = type;
							entry.ExtraData = _nextIndex;
							entry.PackageNumber = 0;

							// This is hokey, but we need a way to know the filename for an item
							_indexToFilename[_nextIndex] = _findData.cFileName;
							_nextIndex++;
							foundOne = true;
						}
					}
				}
			}
		}

		_stillMore = !!FindNextFile(_hFind, &_findData);
	}

	if (!_stillMore)
	{
		FindClose(_hFind);
		_hFind = INVALID_HANDLE_VALUE;
	}

	return _stillMore || foundOne;
}

sci::istream PatchFilesResourceSource::GetHeaderAndPositionedStream(const ResourceMapEntryAgnostic &mapEntry, ResourceHeaderAgnostic &headerEntry)
{
	std::string fileName = _indexToFilename[mapEntry.ExtraData];	// We used package number as a transport vessel for our arbitrary data
	assert(!fileName.empty());
	ScopedHandle patchFile;
	std::string fullPath = _gameFolder + "\\" + fileName;
	patchFile.hFile = CreateFile(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (patchFile.hFile != INVALID_HANDLE_VALUE)
	{
		auto streamHolder = std::make_unique<sci::streamOwner>(patchFile.hFile);
		sci::istream readStream = streamHolder->getReader();
		// We need to be owners of this stream data.
		_streamHolder[mapEntry.ExtraData] = move(streamHolder);

		// Now fill in the headerEntry
		headerEntry.Number = mapEntry.Number;
		headerEntry.Base36Number = mapEntry.Base36Number;
		headerEntry.Type = mapEntry.Type;
		headerEntry.CompressionMethod = 0;
		headerEntry.Version = _version;

		readStream.seekg(mapEntry.Offset);
		headerEntry.cbDecompressed = readStream.getBytesRemaining();
		headerEntry.cbCompressed = readStream.getBytesRemaining();
		headerEntry.SourceFlags = _sourceFlags;
		headerEntry.PackageHint = 0;	// No package.

		return readStream;
	}
	return sci::istream(nullptr, 0); // Empty stream....
}

sci::istream PatchFilesResourceSource::GetPositionedStreamAndResourceSizeIncludingHeader(const ResourceMapEntryAgnostic &mapEntry, uint32_t &size, bool &includesHeader)
{
	includesHeader = false;
	ResourceHeaderAgnostic header;
	sci::istream stream = GetHeaderAndPositionedStream(mapEntry, header);
	size = header.cbCompressed;
	return stream;
}

void PatchFilesResourceSource::RemoveEntry(const ResourceMapEntryAgnostic &mapEntry)
{
	std::string filename = GetFileNameFor(mapEntry.Type, mapEntry.Number, mapEntry.Base36Number, _version);
	std::string fullPath = _gameFolder + "\\" + filename;
	deletefile(fullPath);
}

sci::Status CheckPatchFileCanBeReplaced(const std::string &path)
{
	DWORD attributes = GetFileAttributesA(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		return sci::Ok(); // A new file: nothing to replace.
	}
	if (attributes & FILE_ATTRIBUTE_READONLY)
	{
		sci::Error error;
		error.code = sci::ErrorCode::Io;
		error.message = "The file is read-only, so it cannot be replaced";
		error.where.file = path;
		return sci::Fail(std::move(error));
	}
	ScopedHandle handle;
	handle.hFile = CreateFileA(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle.hFile == INVALID_HANDLE_VALUE)
	{
		DWORD lastError = GetLastError();
		sci::Error error = sci::FromWin32(lastError, "Replacing " + path);
		error.where.file = path;
		return sci::Fail(std::move(error));
	}
	return sci::Ok();
}

namespace
{
	// Throws if the rename in AppendResources cannot replace this existing
	// file.
	void _CheckCanReplace(const std::string &path)
	{
		sci::Status replaceable = CheckPatchFileCanBeReplaced(path);
		if (!replaceable)
		{
			throw sci::DataError(replaceable.error());
		}
	}
}

AppendBehavior PatchFilesResourceSource::AppendResources(const std::vector<const ResourceBlob*> &blobs)
{
	// Write every resource to a .bak file first, and check that every existing
	// target can be replaced; only then replace the targets. A failure before
	// the renames leaves every old patch file as it was, and removes the .bak
	// files, so a batch that fails part way does not leave, for example, a
	// new .scr next to an old .hep. Each replace is atomic, so no patch file is
	// ever left half written. A rename can still fail after the checks (if
	// another program locks the file at that moment); then the renames before
	// it stay done, and the .bak files that are left are removed.
	std::vector<std::pair<std::string, std::string>> written; // .bak, target
	std::string currentBak;
	try
	{
		for (const ResourceBlob *blob : blobs)
		{
			std::string filename = GetFileNameFor(*blob);
			std::string fullPath = _gameFolder + "\\" + filename;
			currentBak = fullPath + ".bak";

			// SaveToHandle refuses a resource that is too big for the format,
			// with an unhelpful "out of memory" code. Say what is wrong instead.
			DWORD size = max(blob->GetHeader().cbCompressed, blob->GetHeader().cbDecompressed);
			sci::Status sizeOk = CheckResourceSize(blob->GetVersion(), size, blob->GetType());
			if (!sizeOk)
			{
				sci::Error error = sizeOk.error();
				error.where.file = filename;
				throw sci::DataError(std::move(error));
			}

			{
				ScopedFile file(currentBak, GENERIC_WRITE, 0, CREATE_ALWAYS);
				HRESULT hr = blob->SaveToHandle(file.hFile, true);
				if (FAILED(hr))
				{
					throw sci::DataError(sci::FromHResult(hr, "Writing " + currentBak));
				}
			}
			written.emplace_back(currentBak, fullPath);
			currentBak.clear();
		}

		for (const auto &bakAndTarget : written)
		{
			_CheckCanReplace(bakAndTarget.second);
		}
	}
	catch (...)
	{
		if (!currentBak.empty())
		{
			DeleteFileA(currentBak.c_str());
		}
		for (const auto &bakAndTarget : written)
		{
			DeleteFileA(bakAndTarget.first.c_str());
		}
		throw;
	}

	for (size_t i = 0; i < written.size(); i++)
	{
		try
		{
			replacefile(written[i].first, written[i].second);
		}
		catch (...)
		{
			// The renames before this one stay done; remove the .bak files
			// that are left.
			for (size_t j = i; j < written.size(); j++)
			{
				DeleteFileA(written[j].first.c_str());
			}
			throw;
		}
	}
	return AppendBehavior::Replace;
}

PatchFilesResourceSource::~PatchFilesResourceSource()
{
	if (_hFind != INVALID_HANDLE_VALUE)
	{
		FindClose(_hFind);
	}
}
