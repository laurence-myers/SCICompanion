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
// Removes the resource of the blob from its source: a patch file is deleted,
// and an entry of the package goes out of the map and the volume (both are
// written again). The source must have an entry with the number, the
// package and the bytes of the blob. When it removes the last copy of a
// script, the services of the GUI (if any) can remove the script from
// game.ini. No UI. NotFound when the source has no such entry; Unsupported
// for a source that cannot remove a resource; Io when a file cannot be
// deleted or written; Internal when the map has no entry of the resource
// that its volume has. It runs in an exception boundary.
sci::Status RemoveResource(CResourceMap &resourceMap, const ResourceBlob &data);
// The GUI form of RemoveResource: it shows the error ("Resource not found."
// for NotFound).
void DeleteResource(CResourceMap &resourceMap, const ResourceBlob &data);
// resourceMap: the map that the audio cache source writes its audio maps
// through (ResourceSourceFlags::AudioCache). The other sources do not use it.
std::unique_ptr<ResourceSource> CreateResourceSource(ResourceTypeFlags flagsHint, const GameFolderHelper &helper, ResourceSourceFlags source, ResourceSourceAccessFlags access = ResourceSourceAccessFlags::Read, int mapContext = -1, CResourceMap *resourceMap = nullptr);
