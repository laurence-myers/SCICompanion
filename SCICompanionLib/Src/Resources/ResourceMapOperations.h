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
#pragma once
#include "ResourceSources.h"
#include "Result.h"

class GameFolderHelper;
class CResourceMap;
class ResourceBlob;
class ResourceSource;
enum class ResourceTypeFlags;
// Removes the resource of the blob from its source: the patch file that the
// source listed for it is deleted (with the name that it has on disk, also a
// name that is not the standard one), and an entry of the package goes out
// of the map and the volume (both are written again). The source must have
// an entry with the number, the package and the bytes of the blob. When it
// removes the last copy of a script, the services of the GUI (if any) can
// remove the script from game.ini. removedFile (if not null) gets the path
// of the deleted patch file, or an empty text for an entry of the package.
// No UI. NotFound when the source has no such entry, and when the listed
// patch file is not there any more; Unsupported for a source that cannot
// remove a resource; Io when a file cannot be deleted or written (the
// context of the error names the .bak files of a package write that
// stopped halfway); Internal when the second read of the map (for its
// rewrite) does not give the entry that the first read found. It runs in
// an exception boundary.
sci::Status RemoveResource(CResourceMap &resourceMap, const ResourceBlob &data, std::string *removedFile = nullptr);
// The message box of the GUI forms for an error of RemoveResource:
// "Resource not found." for NotFound, else the text of the error with its
// context and no code, with an error icon for Unsupported and a warning
// icon for the others.
void ShowRemoveError(const sci::Error &error);
// The GUI form of RemoveResource: ShowRemoveError for its error.
void DeleteResource(CResourceMap &resourceMap, const ResourceBlob &data);
// resourceMap: the map that the audio cache source writes its audio maps
// through (ResourceSourceFlags::AudioCache). The other sources do not use it.
std::unique_ptr<ResourceSource> CreateResourceSource(ResourceTypeFlags flagsHint, const GameFolderHelper &helper, ResourceSourceFlags source, ResourceSourceAccessFlags access = ResourceSourceAccessFlags::Read, int mapContext = -1, CResourceMap *resourceMap = nullptr);
