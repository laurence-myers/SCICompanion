#pragma once

// The parts of the GUI's compile that have no window: the scripts of a
// compile-all, the question about the patch files that would hide a
// package save, and the lines of a batch. Compile-all
// (CompileABunchOfScripts) and the compile of one script
// (CScriptDocument::OnCompile) use them; a unit test can call them.

#include "CompileBatch.h"
#include <string>
#include <unordered_set>
#include <vector>

class CResourceMap;

// The scripts of game.ini, or the ones with these lower-case titles. When
// there is none, it offers to scan the src folder for .sc files. A file whose
// name has a character that the ANSI code page does not have is skipped,
// and the scan goes on.
std::vector<ScriptId> ScriptsToCompile(CResourceMap &resourceMap, const std::unordered_set<std::string> &titles);

// The GUI asks before a package save that a patch file would hide. Yes:
// move the patch files aside (Replace). No: keep them (Ignore). Cancel:
// stop, and write no resource (Refuse). While the program quits, it asks
// nothing, and the batch writes the package (Ignore).
ShadowPolicy AskAboutShadowingPatches(const std::vector<std::string> &files);

// The line for a batch that did not start: an error, or a plain message when
// the user stopped it (Cancel in the question).
CompileResult StartFailureLine(const sci::Error &error);

// The lines of a finished batch after the lines of its scripts: the table
// save, the commit (a plain message when the user stopped it), the moved
// patch files, the .sco files that went back, and the warnings.
void ReportCompileBatch(const CompileReport &report, ICompileLog &log, const std::string &writeProblem);
