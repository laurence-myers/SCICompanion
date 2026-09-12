/***************************************************************************
    Copyright (c) 2015 Philip Fortier

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
#include "CppUnitTest.h"
#include "Helper.h"
#include "AppState.h"
#include "ClassBrowser.h"
#include "SyntaxParser.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// The tests run from vstest, so the current directory is not the output folder.
// Resolve data files against the directory of this test module instead.
static std::string GetModuleDirectory()
{
    HMODULE hModule = nullptr;
    GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCTSTR>(&GetModuleDirectory), &hModule);
    char szPath[MAX_PATH] = {};
    GetModuleFileName(hModule, szPath, MAX_PATH);
    std::string path = szPath;
    size_t slash = path.find_last_of('\\');
    if (slash != std::string::npos)
    {
        path = path.substr(0, slash);
    }
    return path;
}

std::string GetTestFileDirectory(const std::string &subDirectory)
{
    return GetModuleDirectory() + "\\TestFiles\\" + subDirectory;
}

std::string GetTestModuleDirectory()
{
    return GetModuleDirectory();
}

std::string SetUpGame(const std::string &name)
{
    std::string moduleDir = GetModuleDirectory();
    std::string srcGameFolder = moduleDir + name;

    std::string gameFolder = GetRandomTempFolder();
    Assert::IsFalse(gameFolder.empty());
    CopyFilesOver(nullptr, srcGameFolder, gameFolder);

    appState = new AppState(nullptr);
    // AppState(nullptr) does not run InitInstance, so the grammars are not
    // loaded. Load them now, or SyntaxParser_Parse fails.
    InitializeSyntaxParsers();
    appState->GetResourceMap().SetGameFolder(gameFolder);

    // Point the include folder at the module folder. The app post-build put the
    // "include" folder (sci.sh, keys.sh) there.
    std::string exeFolder = moduleDir + "\\";
    appState->GetResourceMap().SetIncludeFolderForTest(exeFolder);

    return gameFolder;
}

void CleanUpGame(const std::string &gameFolder)
{
    appState->GetClassBrowser().SetClassBrowserEvents(nullptr);

    appState->ResetClassBrowser();
    delete appState;
    appState = nullptr;

    char szPath[MAX_PATH];
    StringCchCopy(szPath, ARRAYSIZE(szPath), gameFolder.c_str());
    szPath[gameFolder.length() + 1] = 0;   // double null term

    SHFILEOPSTRUCT fileOp = { 0 };
    fileOp.hwnd = nullptr;
    fileOp.wFunc = FO_DELETE;
    fileOp.pFrom = szPath;
    fileOp.pTo = nullptr;
    fileOp.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;
    SHFileOperation(&fileOp);
}

std::string SetUpGameSCI0()
{
    return SetUpGame("\\TemplateGame\\SCI0\\*");
}

std::string SetUpGameSCI11()
{
    return SetUpGame("\\TemplateGame\\SCI1.1\\*");
}
