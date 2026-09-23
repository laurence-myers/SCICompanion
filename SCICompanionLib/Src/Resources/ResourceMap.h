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
#include "GameFolderHelper.h"
#include "ResourceEntity.h"
#include "TalkerToViewMap.h"

class RunLogic;
class DebuggerThread;
class PostBuildThread;
struct Vocab000;
struct AudioMapComponent;
struct PaletteComponent;
class MessageSource;
class MessageHeaderFile;
class AppState;

// FWD declaration
class ResourceContainer;
class ResourceBlob;
class ResourceRecency;
class ResourceEntity;
class GlobalCompiledScriptLookups;
class IResourceMapEvents;
enum class ResourceSaveLocation : uint16_t;

//
// REVIEW: CResourceMap needs to be protected with a critical section
//

class ISCIAppServices
{
public:
	virtual void OnGameFolderUpdate() = 0;
	virtual void SetRecentlyInteractedView(int number) = 0;
};


//
// This lets you operate on anything involving the resource.map, and is basically
// where we go to learn information about the currently loaded game.
//
class CResourceMap
{
public:
	CResourceMap(ISCIAppServices *appServices, ResourceRecency *resourceRecency);
	~CResourceMap();

	RunLogic &GetRunLogic();

	// ResourceBlob: the raw resource bits already in a ready-to-save format.
	// ResourceEntity: a runtime version of a resource that we can edit.
	//
	// WriteResource writes a resource into the game: into the package or as a
	// patch file, as the blob's source flags say. While a DeferResourceAppend
	// batch is open, it only queues the resource (a second copy of the same
	// resource replaces the first), and the batch's Commit writes it. No UI.
	sci::Status WriteResource(const ResourceBlob &resource);
	// The GUI form of WriteResource: it also shows the error text.
	HRESULT AppendResource(const ResourceBlob &resource);
	HRESULT AppendResourceAskForNumber(ResourceBlob &resource, bool warnOnOverwrite);
	void AppendResourceAskForNumber(ResourceEntity &resource);
	void AppendResourceAskForNumber(ResourceEntity &resource, const std::string &name, bool warnOnOverwrite = false);
	// WriteResource for an entity: it serializes the entity, checks its size,
	// and writes it as WriteResource(blob) does. No UI. An entity whose own
	// checks fail (PerformChecks, which can ask the user) gives Cancelled.
	sci::Status WriteResource(const ResourceEntity &resource, int packageNumber, int resourceNumber, const std::string &name, uint32_t base36Header = NoBase36, int *pChecksum = nullptr);
	// The same, with the entity's own package, number and base-36 number.
	sci::Status WriteResource(const ResourceEntity &resource, int *pChecksum = nullptr);
	// The GUI forms: they also show the error text.
	bool AppendResource(const ResourceEntity &resource, int *pChecksum = nullptr);
	bool AppendResource(const ResourceEntity &resource, int packageNumber, int resourceNumber, const std::string &name, uint32_t base36Header = NoBase36, int *pChecksum = nullptr);

	int SuggestResourceNumber(ResourceType type);
	void AssignName(const ResourceBlob &resource);
	void AssignName(ResourceType iType, int iResourceNumber, uint32_t base36Number, PCTSTR pszName);

	ResourceSaveLocation GetDefaultResourceSaveLocation();

	// The main functions for enumerating resources.
	std::unique_ptr<ResourceContainer> Resources(ResourceTypeFlags types, ResourceEnumFlags flags, int mapContext = -1);
	std::unique_ptr<ResourceBlob> MostRecentResource(ResourceType type, int number, bool getName, uint32_t base36Number = NoBase36, int mapContext = -1);
	bool DoesResourceExist(ResourceType type, int number, std::string *retrieveName = nullptr, ResourceSaveLocation location = ResourceSaveLocation::Default) const;

	void AddSync(IResourceMapEvents *pSync);
	void RemoveSync(IResourceMapEvents *pSync);
	void NotifyToReloadResourceType(ResourceType iType);
	void NotifyToRegenerateImages();

	void DeleteResource(const ResourceBlob *pResource);

	// Opens the game in the folder (an empty folder closes it). The GUI form:
	// a failure shows a message box and throws a CUserException.
	void SetGameFolder(const std::string &gameFolder);
	// Opens the game in the folder. No dialog and no exception: a failure
	// comes back as an error, and then no game is open. An empty folder is a
	// Usage error.
	sci::Status TryOpen(const std::string &gameFolder);
	// True when the game's resource map is corrupt or truncated (an SCI1+ lookup
	// table with no terminator). Safe to call on the UI thread after a game is
	// opened; the enumeration itself degrades to zero entries either way (#117).
	bool IsResourceMapCorrupt();

	TalkerToViewMap &GetTalkerToViewMap();

	std::string GetGameFolder() const;
	std::string GetIncludeFolder() const;
	std::string GetIncludePath(const std::string &includeFileName);
	std::string GetTemplateFolder();
	std::string GetSamplesFolder();
	std::string GetTopLevelSamplesFolder();
	std::string GetObjectsFolder();
	std::string GetDecompilerFolder();
	bool IsGameLoaded() { return !_gameFolderHelper.GameFolder.empty(); }
	HRESULT GetGameIni(PTSTR pszBuf, size_t cchBuf);
	HRESULT GetScriptNumber(ScriptId script, WORD &wScript);
	const SCIVersion &GetSCIVersion() const;
	void SetVersion(const SCIVersion &version);
	const GameFolderHelper &Helper() const { return _gameFolderHelper; }
	// The script names of a GameSession (docs/scic-cli/plan.md section 3.4).
	// An open clears them. The GUI sets none.
	void SetScriptNames(std::shared_ptr<const ScriptNameMap> names);
	const Vocab000 *GetVocab000();
	const PaletteComponent *GetPalette999();
	void SaveAudioMap65535(const AudioMapComponent &newAudioMap, int mapContext);
	GlobalCompiledScriptLookups *GetCompiledScriptLookups();
	std::vector<int> GetPaletteList();
	std::unique_ptr<PaletteComponent> GetPalette(int fallbackPalette);
	std::unique_ptr<PaletteComponent> GetMergedPalette(const ResourceEntity &resource, int fallbackPalette);
	// Thread-safe overload for the render workers (#97): the caller supplies the
	// global palette (precomputed on the UI thread, e.g. a copy of GetPalette999()),
	// so this touches no shared mutable map state -- only the passed resource and the
	// immutable _emptyPalette. The fallbackPalette overload above walks the map to
	// resolve the global palette and so must run on the UI thread.
	std::unique_ptr<PaletteComponent> GetMergedPalette(const ResourceEntity &resource, const PaletteComponent *globalPalette) const;
	ResourceEntity *GetVocabResourceToEdit();
	void ClearVocab000();
	std::unique_ptr<ResourceEntity> CreateResourceFromNumber(ResourceType type, int wNumber, uint32_t base36Number = NoBase36, int mapContext = -1);
	void GetAllScripts(std::vector<ScriptId> &scripts);
	void GetNumberToNameMap(std::unordered_map<WORD, std::string> &scos);
	// The folder that holds include\ and Decompiler\. By default, the folder
	// of the program.
	void SetDataFolder(const std::string &folder);
	bool CanSaveResourcesToMap();
	void SkipNextVersionSniff() { _skipVersionSniffOnce = true; }

	MessageSource *GetVerbsMessageSource(bool reload = false);
	MessageSource *GetTalkersMessageSource(bool reload = false);

	bool IsResourceCompatible(const ResourceBlob &resource);

	void StartDebuggerThread(int optionalResourceNumber);
	void AbortDebuggerThread();

	void StartPostBuildThread();
	void AbortPostBuildThread();
	void PokeResourceMapReloaded();

	void RepackageAudio(bool force = false);

	// True while a DeferResourceAppend batch is open: writes only queue.
	bool IsDeferring() const { return !_deferLevels.empty(); }

private:
	void _SniffSCIVersion();
	sci::Status _OpenGameFolder(const std::string &gameFolder);

	void BeginDeferAppend();
	sci::Status EndDeferAppend();
	void AbandonAppend();
	friend class DeferResourceAppend;

	// Member variables

	TalkerToViewMap _talkerToView;

	std::vector<IResourceMapEvents*> _syncs;

	ResourceRecency *_resourceRecency;
	ISCIAppServices *_appServices;

	// Useful resources to cache
	std::unique_ptr<ResourceEntity> _pVocab000;
	std::unique_ptr<ResourceEntity> _pPalette999;
	std::unique_ptr<PaletteComponent> _emptyPalette;
	std::vector<int> _paletteList;
	bool _paletteListNeedsUpdate;

	std::unique_ptr<MessageHeaderFile> _verbsHeaderFile;
	std::unique_ptr<MessageHeaderFile> _talkersHeaderFile;


	std::unique_ptr<GlobalCompiledScriptLookups> _globalCompiledScriptLookups;

	// Defer appending resources when you are appending a lot (E.g. during compiling).
	// One level for each open DeferResourceAppend batch, the outermost first.
	struct DeferLevel
	{
		size_t queuedAtStart = 0;	// The queue length when the level opened.
		// The queued copies from before this level that this level replaced,
		// with their queue index. Abandoning the level puts them back.
		std::vector<std::pair<size_t, std::unique_ptr<ResourceBlob>>> replaced;
		bool HasReplaced(size_t index) const;
	};
	std::vector<DeferLevel> _deferLevels;
	// Pointers, so that to put back a replaced copy cannot throw (an abandon
	// runs in a destructor).
	std::vector<std::unique_ptr<ResourceBlob>> _deferredResources;

	GameFolderHelper _gameFolderHelper;

	bool _skipVersionSniffOnce;					 // Skip version sniffing when loading a game the next time.

	std::string _dataFolder;					 // With a final backslash; empty for the folder of the program

	std::shared_ptr<DebuggerThread> _debuggerThread;
	std::shared_ptr<PostBuildThread> _postBuildThread;

	std::unique_ptr<RunLogic> _runLogic;
};

//
// Defer the actual writing of resources so it happens in one big batch at the end.
//
// Batches nest. Only the outermost Commit writes; an inner Commit only closes
// the inner batch and keeps its resources for the outer one. A batch that is
// destroyed without a Commit abandons its level: the resources that it queued
// are withdrawn, and the queued copies that it replaced come back. For the
// outermost batch, that discards the whole queue.
//
class DeferResourceAppend
{
public:
	DeferResourceAppend(CResourceMap &map, bool fDoIt = true) : _map(map), _fDoIt(fDoIt), _closed(false)
	{
		if (fDoIt)
		{
			_map.BeginDeferAppend();
		}
	}
	DeferResourceAppend(const DeferResourceAppend &) = delete;
	DeferResourceAppend &operator=(const DeferResourceAppend &) = delete;

	// Writes the queued resources, one rewrite for each destination, and
	// returns the first failure. The resources of a destination that failed
	// are not named in game.ini.
	sci::Status Commit()
	{
		if (!_fDoIt || _closed)
		{
			return sci::Ok();
		}
		_closed = true;
		return _map.EndDeferAppend();
	}

	// The resources queued so far, by this batch and by any batch around it.
	std::vector<const ResourceBlob*> Pending() const
	{
		std::vector<const ResourceBlob*> pending;
		for (const auto &queued : _map._deferredResources)
		{
			pending.push_back(queued.get());
		}
		return pending;
	}

	~DeferResourceAppend()
	{
		if (_fDoIt && !_closed)
		{
			_map.AbandonAppend();
		}
	}
private:
	CResourceMap &_map;
	bool _fDoIt;
	bool _closed;
};

// Shows a failed write to the user (a message box with a GUI, the log
// without one). For GUI code that has nowhere else to report it. A Cancelled
// error is not shown: the user chose it.
void ShowWriteError(const sci::Status &status);
