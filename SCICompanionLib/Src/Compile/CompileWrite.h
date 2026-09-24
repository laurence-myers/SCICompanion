#pragma once

// Where a compile writes (plan section 5): the destination of the script,
// heap, text and vocab resources, and whether the object and debug files
// are written.

#include "GameFolderHelper.h"
#include "Result.h"
#include <cstdint>
#include <string>
#include <vector>

class CResourceMap;
enum class ResourceType;

// A compiled resource for an output folder that waits for the commit of a
// batch.
struct StagedOutputFile
{
    ResourceType type;
    uint16_t number;
    std::vector<uint8_t> data;
};

// A compiled resource that a compile wrote: in a batch, one that the commit
// writes (a dry run: would write). Plan section 6.5.
struct WrittenResource
{
    ResourceType type;
    uint16_t number;
};

struct CompileWriteOptions
{
    // The package or patch files. Default takes the game's setting
    // (SaveToPatchFiles in game.ini), as the GUI does.
    ResourceSaveLocation saveTo = ResourceSaveLocation::Default;
    // When not empty: the resources go into this folder as patch files, and
    // the game's resources do not change. Not with Package.
    std::string outDir;
    // With outDir: the plain resource data with no patch header, as
    // "script.110.bin".
    bool raw = false;
    // False: no resource is written (a dry run).
    bool writeResources = true;
    // False: no src\<name>.sco.
    bool writeObjectFile = true;
    // False: no debug\<n>.scd. The game's GenerateDebugInfo setting decides
    // whether the compiler makes debug information.
    bool writeDebugInfo = true;
    // With outDir: when set, WriteCompiledResource adds each resource here
    // and writes no file, and WriteStagedOutputFiles writes them later.
    // CompileBatch sets it, so that a script that fails, or a batch whose
    // commit is refused, leaves no file in the folder. In a dry run (no
    // writeResources), the list has what a real run would write, and
    // nothing writes it.
    std::vector<StagedOutputFile> *staged = nullptr;
};

// The file name of a compiled resource in an output folder: the patch file
// name for the game's version ("script.110", "110.scr"), or with raw the
// type and number ("script.110.bin").
std::string CompiledResourceFileName(ResourceType type, uint16_t number, const SCIVersion &version, bool raw);

// Writes one compiled resource (script, heap, text, vocab 996 or vocab 997)
// to the destination of the options. Usage for an output folder with
// Package. Inside a DeferResourceAppend batch, a write to the game only
// queues; with options.staged, a write to an output folder only stages.
sci::Status WriteCompiledResource(CResourceMap &resourceMap, const CompileWriteOptions &options, ResourceType type, uint16_t number, const std::vector<uint8_t> &data);

// Writes the staged files into the output folder of the options, in order.
// Every file that is there already must open for writing first, so that a
// read-only, hidden or system file, a folder with the file's name, or a
// file that another program holds, fails the write before the first file;
// so does a path that is too long. A write that fails after that check (a
// full disk) leaves the files before it.
sci::Status WriteStagedOutputFiles(const GameFolderHelper &helper, const CompileWriteOptions &options, const std::vector<StagedOutputFile> &files);

// The check of WriteStagedOutputFiles before its first write, with no write
// (a dry run).
sci::Status CheckStagedOutputFiles(const GameFolderHelper &helper, const CompileWriteOptions &options, const std::vector<StagedOutputFile> &files);
