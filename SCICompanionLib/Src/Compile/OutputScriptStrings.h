#pragma once

#include <string>
#include <vector>

struct SCIVersion;
class CompileLog;
class ScriptId;

void ExtractScriptStrings(const SCIVersion &version, CompileLog &log, ScriptId scriptId, std::vector<std::string> &allStrings);
