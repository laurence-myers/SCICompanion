#include "stdafx.h"
#include "CompileBatchGui.h"
#include "ResourceMap.h"
#include "GameFolderHelper.h"
#include "format.h"
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace
{
    // The file name in the ANSI code page. False when the name has a
    // character that the code page does not have (path::string() throws for
    // such a name).
    bool NarrowFileName(const fs::path &path, std::string &name)
    {
        std::wstring wide = path.filename().wstring();
        if (wide.empty())
        {
            return false;
        }
        BOOL usedDefault = FALSE;
        int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), (int)wide.size(), nullptr, 0, nullptr, &usedDefault);
        if ((length <= 0) || usedDefault)
        {
            return false;
        }
        name.assign(length, '\0');
        WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.data(), (int)wide.size(), &name[0], length, nullptr, nullptr);
        return true;
    }

    // A WM_QUIT is in the queue: a message box would end at once.
    bool QuitPending()
    {
        MSG message;
        return PeekMessage(&message, nullptr, WM_QUIT, WM_QUIT, PM_NOREMOVE) != FALSE;
    }
}

std::vector<ScriptId> ScriptsToCompile(CResourceMap &resourceMap, const std::unordered_set<std::string> &titles)
{
    std::vector<ScriptId> scripts;
    std::vector<ScriptId> all;
    resourceMap.GetAllScripts(all);
    std::copy_if(all.begin(), all.end(), std::back_inserter(scripts),
        [&](const ScriptId &scriptId)
    {
        return titles.empty() || (titles.find(scriptId.GetTitleLower()) != titles.end());
    }
    );

    if (scripts.empty())
    {
        if (IDYES == AfxMessageBox("Error finding scripts to compile.\nDo you want to try scanning the src folder for scripts?", MB_YESNO | MB_APPLMODAL | MB_ICONEXCLAMATION))
        {
            std::string srcFolder = resourceMap.Helper().GetSrcFolder();
            auto matchRSTRegex = std::regex("(\\w+)\\.sc$");
            std::error_code ec;
            for (auto it = fs::directory_iterator(srcFolder, ec); !ec && (it != fs::directory_iterator()); it.increment(ec))
            {
                std::string name;
                std::smatch sm;
                std::error_code notADirectory;
                if (!it->is_directory(notADirectory) && NarrowFileName(it->path(), name) && std::regex_search(name, sm, matchRSTRegex) && (sm.size() > 1))
                {
                    scripts.push_back(ScriptId(srcFolder + "\\" + name));
                }
            }
            if (scripts.empty())
            {
                AfxMessageBox("Could not find any .sc files.", MB_OK | MB_ICONERROR);
            }
        }
    }
    return scripts;
}

ShadowPolicy AskAboutShadowingPatches(const std::vector<std::string> &files)
{
    if (QuitPending())
    {
        // The box cannot get an answer: write the package, and keep the
        // patch files.
        return ShadowPolicy::Ignore;
    }
    const size_t shownFiles = 10;
    std::string list;
    for (size_t i = 0; (i < files.size()) && (i < shownFiles); i++)
    {
        list += files[i] + "\n";
    }
    if (files.size() > shownFiles)
    {
        list += fmt::format("(and {0} more)\n", files.size() - shownFiles);
    }
    std::string text = fmt::format(
        "These patch files would hide the compiled resources in the package. The game and SCI Companion read a patch file before the package.\n\n"
        "{0}\n"
        "Yes: move the patch files to the folder replaced-patches in the game folder.\n"
        "No: keep the patch files. The compiled resources in the package stay hidden.\n"
        "Cancel: stop, and write no resource.",
        list);
    switch (AfxMessageBox(text.c_str(), MB_YESNOCANCEL | MB_ICONWARNING | MB_APPLMODAL))
    {
    case IDYES:
        return ShadowPolicy::Replace;
    case IDNO:
        return ShadowPolicy::Ignore;
    default:
        return ShadowPolicy::Refuse;
    }
}

CompileResult StartFailureLine(const sci::Error &error)
{
    if (error.code == sci::ErrorCode::Cancelled)
    {
        // The user's answer is not an error.
        return CompileResult("The compile was stopped, and nothing was written: " + error.ToString());
    }
    return CompileResult("The compile did not start: " + error.ToString(), CompileResult::CRT_Error);
}

void ReportCompileBatch(const CompileReport &report, ICompileLog &log, const std::string &writeProblem)
{
    // A table failure refuses the commit; that refusal is no second error
    // line.
    bool refusedByTables = !report.tables && !report.commit && (report.commit.error().message == report.tables.error().message);
    if (!report.tables)
    {
        log.ReportResult(CompileResult(std::string("There was a problem saving the class and selector tables") + (refusedByTables ? ", so no compiled resource was written: " : ": ") +
            report.tables.error().ToString(), CompileResult::CRT_Error));
    }
    if (!report.commit && !refusedByTables)
    {
        if (report.commit.error().code == sci::ErrorCode::Cancelled)
        {
            log.ReportResult(CompileResult("The compile was stopped, and no resource was written: " + report.commit.error().ToString()));
        }
        else
        {
            log.ReportResult(CompileResult(writeProblem + report.commit.error().ToString(), CompileResult::CRT_Error));
        }
    }
    for (const std::string &moved : report.movedPatches)
    {
        log.ReportResult(CompileResult("Moved the patch file " + moved));
    }
    // A patch file that could not move is an error: it still hides the
    // package write. The error names every such file.
    if (!report.moves)
    {
        log.ReportResult(CompileResult("Error: " + report.moves.error().ToString(), CompileResult::CRT_Error));
    }
    // The .sco files of the scripts that were not written are back in their
    // state before the compile.
    for (const std::string &restored : report.restoredObjectFiles)
    {
        log.ReportResult(CompileResult("Put back " + restored + ": its script was not written"));
    }
    // A new .sco that was not there before the compile goes.
    for (const std::string &removed : report.removedObjectFiles)
    {
        log.ReportResult(CompileResult("Removed " + removed + ": its script was not written"));
    }
    if (!report.objectFiles)
    {
        log.ReportResult(CompileResult("Error: " + report.objectFiles.error().ToString(), CompileResult::CRT_Error));
    }
    for (const std::string &warning : report.warnings)
    {
        log.ReportResult(CompileResult("Warning: " + warning, CompileResult::CRT_Warning));
    }
}
