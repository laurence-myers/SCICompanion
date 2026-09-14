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
//
// Guards the shipped script/data files that used to be authored in the old
// "SCI Studio" syntax. They are now Sierra Script, and the Studio parser is
// gone, so every one of them must parse with the Sierra parser. If someone
// re-introduces a Studio-syntax file (or breaks one of these), these tests
// fail instead of the app silently failing to parse them at runtime.
//
#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "ResourceMap.h"
#include "ScriptOMAll.h"
#include "SyntaxParser.h"
#include "CompileContext.h"
#include "CrystalScriptStream.h"
#include "CCrystalTextBuffer.h"
#include "Helper.h"
#include <filesystem>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace sci;

namespace UnitTests
{
    static std::wstring ToW(const std::string &s)
    {
        return std::wstring(s.begin(), s.end());
    }

    // Parse a file exactly the way the app parses scripts/headers, and fail
    // with the parser's error text if it does not parse.
    static std::unique_ptr<Script> ParseShipped(const std::string &fullPath, bool addCommentsToOM = false)
    {
        Assert::IsTrue(std::filesystem::exists(fullPath),
            (L"Shipped file missing (build the app so the post-build copies Files): " + ToW(fullPath)).c_str());

        ScriptId scriptId(fullPath.c_str());
        auto script = std::make_unique<Script>(scriptId);

        CCrystalTextBuffer buffer;
        Assert::IsTrue(buffer.LoadFromFile(fullPath.c_str()) != FALSE,
            (L"Could not load " + ToW(fullPath)).c_str());
        CScriptStreamLimiter limiter(&buffer);
        CCrystalScriptStream stream(&limiter);

        CompileLog log;
        bool ok = SyntaxParser_Parse(*script, stream,
            PreProcessorDefinesFromSCIVersion(appState->GetVersion()), &log, addCommentsToOM, nullptr, addCommentsToOM);
        buffer.FreeAll();

        if (!ok || log.HasErrors())
        {
            std::string message = "Parse failed for " + fullPath + ":";
            for (const CompileResult &r : log.Results())
            {
                message += "\n  " + r.GetMessage();
            }
            Assert::Fail(ToW(message).c_str());
        }
        return script;
    }

    static bool HasDefine(const Script &script, const std::string &name)
    {
        for (const auto &d : script.GetDefines())
        {
            if (d->GetName() == name)
            {
                return true;
            }
        }
        return false;
    }

    static std::string ModuleSub(const std::string &relative)
    {
        return GetTestModuleDirectory() + "\\" + relative;
    }

    TEST_CLASS(TestShippedFiles)
    {
    public:
        TEST_METHOD_CLEANUP(Clean)
        {
            if (!_gameFolder.empty())
            {
                CleanUpGame(_gameFolder);
                _gameFolder.clear();
            }
        }

        // The global include headers (parsed on every compile) and the SCI0
        // Insert-Object templates.
        TEST_METHOD(ShippedFiles_SCI0)
        {
            _gameFolder = SetUpGameSCI0();

            auto sci = ParseShipped(ModuleSub("include\\sci.sh"));
            // #ifdef SCI_0 blocks must survive: this define is only inside one.
            Assert::IsTrue(HasDefine(*sci, "ENABLED"), L"sci.sh lost its #ifdef SCI_0 define ENABLED");
            Assert::IsTrue(HasDefine(*sci, "TRUE"), L"sci.sh lost TRUE");

            ParseShipped(ModuleSub("include\\keys.sh"));
            ParseShipped(ModuleSub("Decompiler\\game.sh"));

            auto objects = ParseShipped(ModuleSub("Objects\\SCI0\\Objects.sc"), true);
            Assert::AreEqual((size_t)4, objects->GetClassesNC().size(), L"Objects/SCI0 should define 4 template instances");
            auto methods = ParseShipped(ModuleSub("Objects\\SCI0\\Methods.sc"), true);
            Assert::AreEqual((size_t)1, methods->GetClassesNC().size(), L"Methods/SCI0 should define 1 class");
        }

        // The SCI1.1 include header, Insert-Object templates, and the template
        // game's message/polygon header files.
        TEST_METHOD(ShippedFiles_SCI11)
        {
            _gameFolder = SetUpGameSCI11();

            auto sci = ParseShipped(ModuleSub("include\\sci.sh"));
            // #ifdef SCI_1_1 blocks must survive: this define is only inside one.
            Assert::IsTrue(HasDefine(*sci, "rsBITMAP"), L"sci.sh lost its #ifdef SCI_1_1 define rsBITMAP");
            Assert::IsTrue(HasDefine(*sci, "TRUE"), L"sci.sh lost TRUE");

            auto objects = ParseShipped(ModuleSub("Objects\\SCI1.1\\Objects.sc"), true);
            Assert::AreEqual((size_t)7, objects->GetClassesNC().size(), L"Objects/SCI1.1 should define 7 template instances");
            // The optional-property marker (trailing underscore) must be preserved.
            bool foundOptionalProp = false;
            for (const auto &cls : objects->GetClassesNC())
            {
                for (const auto &prop : cls->GetProperties())
                {
                    if (prop->GetName() == "nsTop_")
                    {
                        foundOptionalProp = true;
                    }
                }
            }
            Assert::IsTrue(foundOptionalProp, L"Objects/SCI1.1 lost the optional property nsTop_");

            auto methods = ParseShipped(ModuleSub("Objects\\SCI1.1\\Methods.sc"), true);
            Assert::AreEqual((size_t)1, methods->GetClassesNC().size(), L"Methods/SCI1.1 should define 1 class");

            // A converted Insert-Object template must round-trip: re-emitting it
            // in Sierra syntax (what InsertObject does) and re-parsing must work.
            std::stringstream ss;
            SourceCodeWriter writer(ss, objects.get());
            objects->OutputSourceCode(writer);
            std::string roundTripPath = appState->GetResourceMap().Helper().GetScriptFileName("ShippedRoundTrip");
            {
                std::ofstream file(roundTripPath.c_str(), std::ios::binary | std::ios::trunc);
                file << ss.str();
            }
            ParseShipped(roundTripPath, true);

            // Message header (included by scripts as a Sierra header) and polygon
            // header (parsed as a Sierra script).
            const char *shms[] = { "0", "13", "15", "20", "110", "111" };
            for (const char *n : shms)
            {
                std::string path = ModuleSub(std::string("TemplateGame\\SCI1.1\\msg\\") + n + ".shm");
                auto shm = ParseShipped(path);
                if (std::string(n) == "0")
                {
                    Assert::IsTrue(HasDefine(*shm, "N_GAMEINFO"), L"0.shm lost the NOUNS define N_GAMEINFO");
                }
            }
            ParseShipped(ModuleSub("TemplateGame\\SCI1.1\\poly\\110.shp"));
        }

        // Nothing shipped should regress to Studio syntax: every source/data
        // file the app parses must carry the Sierra Script marker on line 1.
        TEST_METHOD(ShippedFiles_AllCarryMarker)
        {
            _gameFolder = SetUpGameSCI11();

            const char *roots[] = { "TemplateGame", "Objects", "include", "Decompiler" };
            const std::string marker = "Sierra Script";
            int checked = 0;
            for (const char *root : roots)
            {
                std::string base = ModuleSub(root);
                if (!std::filesystem::exists(base))
                {
                    continue;
                }
                for (auto &entry : std::filesystem::recursive_directory_iterator(base))
                {
                    if (!entry.is_regular_file())
                    {
                        continue;
                    }
                    std::string ext = entry.path().extension().string();
                    if (ext != ".sc" && ext != ".sh" && ext != ".shm" && ext != ".shp")
                    {
                        continue;
                    }
                    std::ifstream file(entry.path());
                    std::string firstLine;
                    std::getline(file, firstLine);
                    if (firstLine.find(marker) == std::string::npos)
                    {
                        Assert::Fail((L"Shipped script file is not Sierra Script (no marker on line 1): " +
                            entry.path().wstring()).c_str());
                    }
                    checked++;
                }
            }
            Assert::IsTrue(checked > 0, L"No shipped script files were found to check");
        }

    private:
        std::string _gameFolder;
    };
}
