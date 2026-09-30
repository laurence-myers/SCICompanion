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

#include "Version.h"
#include "Result.h"
#include <memory>
#include <string>

class ResourceEntity;

class IResourceIdentifier
{
public:
	virtual int GetPackageHint() const = 0;
	virtual int GetNumber() const = 0;
	virtual ResourceType GetType() const = 0;
	virtual int GetChecksum() const = 0;
	virtual uint32_t GetBase36() const = 0;
};

// fwd decl
class ResourceBlob;
std::unique_ptr<ResourceEntity> CreateResourceFromResourceData(const ResourceBlob &data, bool fallbackOnException = true);
// Creates the resource from its data, with no default resource in place of a
// failure. A read failure is an Error (Format for bad data), with the resource
// in its location.
sci::Result<std::unique_ptr<ResourceEntity>> TryCreateResourceFromResourceData(const ResourceBlob &data);
// The load status of the blob: a corrupt header or a failed decompression is a
// Format error, with the resource in its location. It decompresses a blob that
// delayed its decompression.
sci::Status CheckResourceData(const ResourceBlob &data);

void ExportResourceAsBitmap(const ResourceEntity &resourceEntity);

struct SCI_RESOURCE_INFO
{
	const char *pszSampleFolderName;
	const char *pszTitleDefault;		 // Name of the resource in the editor, and the header in game.ini
	const char *pszFileFilter_SCI0;
	const char *pszFileFilter_SCI1;
	const char *pszNameMatch_SCI0;
	const char *pszNameMatch_SCI1;
};

extern SCI_RESOURCE_INFO g_resourceInfo[18];
SCI_RESOURCE_INFO &GetResourceInfo(ResourceType type);
ResourceType ValidateResourceType(ResourceType type);
// "Script", "Text", ...; "Resource" for a type that is not valid.
const char *GetResourceTypeTitle(ResourceType type);
// "script 110", "text 0": a resource, for the location of an error.
std::string DescribeResource(ResourceType type, int number);
std::string GetFileDialogFilterFor(ResourceType type, SCIVersion version);
std::string GetFileNameFor(ResourceType type, int number, uint32_t base36Number, SCIVersion version);
std::string GetFileNameFor(const ResourceBlob &blob);
bool MatchesResourceFilenameFormat(const std::string &filename, ResourceType type, SCIVersion version, int *numberOut, std::string &nameOut);
bool MatchesResourceFilenameFormat(const std::string &filename, SCIVersion version, int *numberOut, std::string &nameOut);

extern const char Base36AudioPrefix;
extern const char Base36SyncPrefix;

// The file-name patterns of the patch files, by resource type.
#define PATCH_FILE_VIEW "view.*;*.v56;*.v16"
#define PATCH_FILE_PIC "pic.*;*.p56;*.p16"
#define PATCH_FILE_SCRIPT "script.*;*.scr"
#define PATCH_FILE_TEXT "text.*;*.tex"
#define PATCH_FILE_SOUND "sound.*;*.snd"
#define PATCH_FILE_MEMORY ""
#define PATCH_FILE_VOCAB "vocab.*;*.voc"
#define PATCH_FILE_FONT "font.*;*.fon"
#define PATCH_FILE_CURSOR "cursor.*;*.cur"
#define PATCH_FILE_PATCH "patch.*;*.pat"
#define PATCH_FILE_BITMAP "*.bit"
#define PATCH_FILE_PALETTE "*.pal"
#define PATCH_FILE_CDAUDIO "*.cda"
#define PATCH_FILE_AUDIO "*.aud"
#define PATCH_FILE_SYNC "*.syn"
#define PATCH_FILE_MESSAGE "*.msg"
#define PATCH_FILE_AUDIOMAP "*.map"
#define PATCH_FILE_HEAP "*.hep"

//#define PATCH_FILE_TYPES "pic.*;view.*;vocab.*;font.*;cursor.*;text.*;sound.*;patch.*;script.*;*.v56;*.p56;*.scr;*.tex;*.snd;*.voc;*.fon;*.cur;*.pat;*.bit;*.pal;*.cda;*.aud;*.syn;*.msg;*.hep;*.map"

#define PATCH_FILE_TYPES PATCH_FILE_VIEW ";"\
 PATCH_FILE_PIC ";" \
PATCH_FILE_SCRIPT ";" \
PATCH_FILE_TEXT ";" \
PATCH_FILE_SOUND ";" \
PATCH_FILE_MEMORY ";" \
PATCH_FILE_VOCAB ";" \
PATCH_FILE_FONT ";" \
PATCH_FILE_CURSOR ";" \
PATCH_FILE_PATCH ";" \
PATCH_FILE_BITMAP ";" \
PATCH_FILE_PALETTE ";" \
PATCH_FILE_CDAUDIO ";" \
PATCH_FILE_AUDIO ";" \
PATCH_FILE_SYNC ";"  \
PATCH_FILE_MESSAGE ";" \
PATCH_FILE_AUDIOMAP ";" \
PATCH_FILE_HEAP
