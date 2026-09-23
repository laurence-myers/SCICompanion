#pragma once

// Where a compile writes (plan step S1; plan section 5): the destination of
// the script, heap, text and vocab resources, and whether the object and
// debug files are written.

#include "GameFolderHelper.h"
#include "Result.h"
#include <cstdint>
#include <string>
#include <vector>

class CResourceMap;
enum class ResourceType;

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
};

// The file name of a compiled resource in an output folder: the patch file
// name for the game's version ("script.110", "110.scr"), or with raw the
// type and number ("script.110.bin").
std::string CompiledResourceFileName(ResourceType type, uint16_t number, const SCIVersion &version, bool raw);

// Writes one compiled resource (script, heap, text, vocab 996 or vocab 997)
// to the destination of the options. Usage for an output folder with
// Package. Inside a DeferResourceAppend batch, a write to the game only
// queues.
sci::Status WriteCompiledResource(CResourceMap &resourceMap, const CompileWriteOptions &options, ResourceType type, uint16_t number, const std::vector<uint8_t> &data);
