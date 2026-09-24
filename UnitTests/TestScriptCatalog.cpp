#include "stdafx.h"
#include "CppUnitTest.h"
#include "AppState.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "ScriptNameMap.h"
#include "ScriptCatalog.h"
#include "Helper.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    // Runs a test with no AppState, as the command line does.
    struct NoAppStateForCatalog
    {
        AppState *saved;
        NoAppStateForCatalog() : saved(appState) { appState = nullptr; }
        ~NoAppStateForCatalog() { appState = saved; }
    };

    std::wstring WideCatalog(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::vector<uint16_t> NumbersOf(const ScriptSelection &selection)
    {
        std::vector<uint16_t> numbers;
        for (const ScriptId &script : selection.scripts)
        {
            numbers.push_back(script.GetResourceNumber());
        }
        return numbers;
    }

    const ScriptRow *RowOf(const std::vector<ScriptRow> &rows, uint16_t number)
    {
        for (const ScriptRow &row : rows)
        {
            if (row.number == number)
            {
                return &row;
            }
        }
        return nullptr;
    }

    void WriteBytes(const std::string &path, std::vector<uint8_t> bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
}

namespace UnitTests
{
    // The script list, the script selectors and the shadow check of the
    // command line (plan sections 4.2, 4.3 and 5).
    TEST_CLASS(TestScriptCatalog)
    {
        std::string _copyFolder;
        std::unique_ptr<GameSession> _session;

        void RemoveCopy()
        {
            _session.reset();
            if (!_copyFolder.empty())
            {
                std::error_code ec;
                std::filesystem::remove_all(_copyFolder, ec);
                _copyFolder.clear();
            }
        }

        std::string SrcFile(const std::string &name) const
        {
            return _copyFolder + "\\src\\" + name;
        }

        // A copy of the SCI1.1 template, with the changes made before the
        // open, in a session.
        GameSession &Open(bool keepGameIni = true, const std::vector<std::string> &removeFromSrc = std::vector<std::string>())
        {
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            if (!keepGameIni)
            {
                std::filesystem::remove(_copyFolder + "\\game.ini");
            }
            for (const std::string &file : removeFromSrc)
            {
                std::filesystem::remove(SrcFile(file));
            }
            return Reopen();
        }

        GameSession &Reopen()
        {
            _session = std::make_unique<GameSession>();
            sci::Status opened = _session->Open(_copyFolder);
            Assert::IsTrue(opened.has_value(), WideCatalog(opened ? std::string() : opened.error().ToString()).c_str());
            return *_session;
        }

        // Writes the script data as a patch file of the game.
        void WriteScriptPatch(GameSession &session, uint16_t number, const std::vector<uint8_t> &data)
        {
            const GameFolderHelper &helper = session.Helper();
            ResourceBlob patch(helper, nullptr, ResourceType::Script, data, helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            sci::Status written = session.ResourceMap().WriteResource(patch);
            Assert::IsTrue(written.has_value(), WideCatalog(written ? std::string() : written.error().ToString()).c_str());
        }

    public:
        TEST_METHOD_CLEANUP(CleanUp)
        {
            RemoveCopy();
        }

        TEST_METHOD(List_Template_NamesAndLocations)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, false);
            Assert::IsTrue(rows.has_value(), WideCatalog(rows ? std::string() : rows.error().ToString()).c_str());
            Assert::IsTrue(rows->size() > 80);
            const ScriptRow *main = RowOf(*rows, 0);
            Assert::IsNotNull(main);
            Assert::AreEqual(std::string("Main"), main->name);
            Assert::IsTrue(main->source == NameSource::GameIni);
            Assert::AreEqual(std::string("resource.000"), main->location);
            Assert::IsTrue(main->hasSource && main->hasObjectFile);
            Assert::IsTrue(main->derivedName.empty() && main->error.empty());
            for (size_t i = 1; i < rows->size(); i++)
            {
                Assert::IsTrue((*rows)[i - 1].number < (*rows)[i].number, L"rows are in number order");
            }
        }

        TEST_METHOD(List_AlwaysDerive_FillsTheDerivedColumn)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, true);
            Assert::IsTrue(rows.has_value());
            Assert::AreEqual(std::string("Main"), RowOf(*rows, 0)->derivedName);
            int derived = 0;
            for (const ScriptRow &row : *rows)
            {
                derived += row.derivedName.empty() ? 0 : 1;
                Assert::IsTrue(row.error.empty(), WideCatalog(row.error).c_str());
            }
            Assert::IsTrue(derived > 60, L"most scripts have a class or a public instance");
        }

        // A compiled script with no game.ini entry, no .sc and no .sco gets
        // its derived name (rule 4).
        TEST_METHOD(List_NoNameFromTheFiles_DerivesTheName)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open(false, { "TitleScreen.sc", "TitleScreen.sco" });
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, false);
            Assert::IsTrue(rows.has_value());
            const ScriptRow *title = RowOf(*rows, 100);
            Assert::IsNotNull(title);
            Assert::IsTrue(title->source == NameSource::Derived);
            Assert::IsTrue(!title->name.empty() && (title->name != "n100"), WideCatalog(title->name).c_str());
            Assert::IsFalse(title->hasSource);
            Assert::IsTrue(RowOf(*rows, 0)->source == NameSource::Source, L"Main still comes from Main.sc");
        }

        TEST_METHOD(List_PatchFileAndUnreadableScript)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            std::unique_ptr<ResourceBlob> script = session.Helper().MostRecentResource(ResourceType::Script, 100, ResourceEnumFlags::None);
            Assert::IsTrue(script != nullptr);
            WriteScriptPatch(session, 100, std::vector<uint8_t>(script->GetData(), script->GetData() + script->GetLength()));
            // A cut copy of script 255 as a patch file.
            std::unique_ptr<ResourceBlob> controls = session.Helper().MostRecentResource(ResourceType::Script, 255, ResourceEnumFlags::None);
            Assert::IsTrue(controls && (controls->GetLength() > 10));
            WriteScriptPatch(session, 255, std::vector<uint8_t>(controls->GetData(), controls->GetData() + 10));

            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, true);
            Assert::IsTrue(rows.has_value());
            Assert::AreEqual(std::string("100.scr (patch)"), RowOf(*rows, 100)->location);
            Assert::IsTrue(RowOf(*rows, 100)->error.empty());
            const ScriptRow *cut = RowOf(*rows, 255);
            Assert::IsFalse(cut->error.empty(), L"a script that cannot be read shows its error");
            Assert::AreEqual(std::string("Controls"), cut->name, L"and keeps its name");
        }

        TEST_METHOD(Selectors_NumbersRangesNamesAndDuplicates)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "255", "Main", "main", "0", "100-100", "titlescreen" }, SelectorMode::Decompile);
            Assert::IsTrue(selected.has_value(), WideCatalog(selected ? std::string() : selected.error().ToString()).c_str());
            std::vector<uint16_t> numbers = NumbersOf(*selected);
            Assert::AreEqual(size_t(3), numbers.size(), L"0 and Main and main are one script");
            Assert::AreEqual(0, (int)numbers[0]);
            Assert::AreEqual(100, (int)numbers[1]);
            Assert::AreEqual(255, (int)numbers[2]);
            // ScriptId keeps its path in lower case.
            Assert::AreEqual(0, _stricmp((_copyFolder + "\\src\\TitleScreen.sc").c_str(), selected->scripts[1].GetFullPath().c_str()), WideCatalog(selected->scripts[1].GetFullPath()).c_str());
        }

        TEST_METHOD(Selectors_EveryBadSelectorIsInOneUsageError)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "NoSuchScript", "0", "4000", "3000-3100" }, SelectorMode::Compile);
            Assert::IsFalse(selected.has_value());
            const sci::Error &error = selected.error();
            Assert::IsTrue(error.code == sci::ErrorCode::Usage);
            Assert::IsTrue(error.message.find("NoSuchScript") != std::string::npos, WideCatalog(error.message).c_str());
            Assert::IsTrue(error.message.find("4000") != std::string::npos, WideCatalog(error.message).c_str());
            Assert::IsTrue(error.message.find("3000-3100") != std::string::npos, WideCatalog(error.message).c_str());
            Assert::IsTrue(error.message.find("scic script list") != std::string::npos);
        }

        TEST_METHOD(Selectors_PathIsForCompileOnly)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            sci::Result<ScriptSelection> compile = ResolveScriptSelectors(session, { "src\\TitleScreen.sc" }, SelectorMode::Compile);
            Assert::IsTrue(compile.has_value(), WideCatalog(compile ? std::string() : compile.error().ToString()).c_str());
            Assert::AreEqual(size_t(1), compile->scripts.size());
            Assert::AreEqual(100, (int)compile->scripts[0].GetResourceNumber());

            sci::Result<ScriptSelection> decompile = ResolveScriptSelectors(session, { "src\\TitleScreen.sc" }, SelectorMode::Decompile);
            Assert::IsFalse(decompile.has_value());
            Assert::IsTrue(decompile.error().message.find("only compile takes a path") != std::string::npos, WideCatalog(decompile.error().message).c_str());

            sci::Result<ScriptSelection> header = ResolveScriptSelectors(session, { "src\\game.sh" }, SelectorMode::Compile);
            Assert::IsFalse(header.has_value());
            Assert::IsTrue(header.error().message.find("a header file cannot be compiled") != std::string::npos, WideCatalog(header.error().message).c_str());
        }

        // Compile keeps the order of game.ini [Script]; decompile uses number
        // order. The template's [Script] section is not in number order.
        TEST_METHOD(Selectors_CompileOrderFollowsGameIni)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            const std::vector<uint16_t> &order = session.Helper().ScriptNames->GameIniOrder();
            size_t i = 1;
            while ((i < order.size()) && (order[i - 1] < order[i]))
            {
                i++;
            }
            Assert::IsTrue(i < order.size(), L"setup: game.ini is not in number order");
            uint16_t first = order[i - 1];
            uint16_t second = order[i];
            std::vector<std::string> selectors = { std::to_string(second), std::to_string(first) };

            sci::Result<ScriptSelection> compile = ResolveScriptSelectors(session, selectors, SelectorMode::Compile);
            Assert::IsTrue(compile.has_value());
            Assert::AreEqual((int)first, (int)NumbersOf(*compile)[0], L"compile: the game.ini order");
            sci::Result<ScriptSelection> decompile = ResolveScriptSelectors(session, selectors, SelectorMode::Decompile);
            Assert::IsTrue(decompile.has_value());
            Assert::AreEqual((int)second, (int)NumbersOf(*decompile)[0], L"decompile: number order");
        }

        // A mode that writes refuses a selection with a script in a name
        // conflict, with the conflict and its fix. A selection without that
        // script works, --all leaves the script out with a warning, and the
        // list works. A conflict refuses no selection without its scripts:
        // real projects have two game.ini names that differ only in case.
        TEST_METHOD(Selectors_Conflict_RefusesOnlyItsScripts)
        {
            NoAppStateForCatalog noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::remove(_copyFolder + "\\game.ini");
            std::filesystem::copy_file(SrcFile("TitleScreen.sc"), SrcFile("Title2.sc"));
            GameSession &session = Reopen();

            sci::Result<ScriptSelection> conflicted = ResolveScriptSelectors(session, { "100" }, SelectorMode::Compile);
            Assert::IsFalse(conflicted.has_value());
            Assert::IsTrue(conflicted.error().code == sci::ErrorCode::Usage);
            Assert::IsTrue(conflicted.error().message.find("Title2.sc") != std::string::npos, WideCatalog(conflicted.error().message).c_str());
            Assert::IsTrue(conflicted.error().message.find("Keep one of the files") != std::string::npos, WideCatalog(conflicted.error().message).c_str());

            sci::Result<ScriptSelection> other = ResolveScriptSelectors(session, { "0" }, SelectorMode::Compile);
            Assert::IsTrue(other.has_value(), WideCatalog(other ? std::string() : other.error().ToString()).c_str());

            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Decompile);
            Assert::IsTrue(all.has_value(), WideCatalog(all ? std::string() : all.error().ToString()).c_str());
            std::vector<uint16_t> numbers = NumbersOf(*all);
            Assert::IsTrue(std::find(numbers.begin(), numbers.end(), (uint16_t)100) == numbers.end(), L"--all leaves out script 100");
            Assert::IsTrue(std::find(numbers.begin(), numbers.end(), (uint16_t)0) != numbers.end());
            Assert::AreEqual(size_t(1), all->warnings.size());
            Assert::IsTrue(all->warnings[0].find("script 100") != std::string::npos, WideCatalog(all->warnings[0]).c_str());

            Assert::IsTrue(ResolveScriptSelectors(session, { "100" }, SelectorMode::List).has_value());
        }

        // A script name can have a '.' (real games have n993=gamefile.sh), so
        // a name wins over a path.
        TEST_METHOD(Selectors_ANameWithADot_IsAName)
        {
            NoAppStateForCatalog noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            WritePrivateProfileStringA("Script", "n7777", "my.script", (_copyFolder + "\\game.ini").c_str());
            GameSession &session = Reopen();
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "my.script" }, SelectorMode::List);
            Assert::IsTrue(selected.has_value(), WideCatalog(selected ? std::string() : selected.error().ToString()).c_str());
            Assert::AreEqual(7777, (int)NumbersOf(*selected)[0]);
        }

        // Two paths for one script number are an error; the selection makes
        // a path normal (no "..", only '\').
        TEST_METHOD(Selectors_TwoPathsForOneScript_AndANormalPath)
        {
            NoAppStateForCatalog noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            std::filesystem::copy_file(SrcFile("TitleScreen.sc"), SrcFile("Title2.sc"));
            GameSession &session = Reopen();

            sci::Result<ScriptSelection> two = ResolveScriptSelectors(session, { "src\\TitleScreen.sc", "src\\Title2.sc" }, SelectorMode::Compile);
            Assert::IsFalse(two.has_value());
            Assert::IsTrue(two.error().message.find("script 100 is also") != std::string::npos, WideCatalog(two.error().message).c_str());

            sci::Result<ScriptSelection> normal = ResolveScriptSelectors(session, { "src\\..\\src/TitleScreen.sc" }, SelectorMode::Compile);
            Assert::IsTrue(normal.has_value(), WideCatalog(normal ? std::string() : normal.error().ToString()).c_str());
            Assert::AreEqual(0, _stricmp((_copyFolder + "\\src\\TitleScreen.sc").c_str(), normal->scripts[0].GetFullPath().c_str()), WideCatalog(normal->scripts[0].GetFullPath()).c_str());
        }

        // Two files that declare a script that the game has not compiled are
        // a conflict too. --all, a range and the name of one of the files
        // show it, and list has the script.
        TEST_METHOD(Conflict_OfAnUncompiledScript_IsShown)
        {
            NoAppStateForCatalog noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            for (const char *name : { "S3NewRoomA.sc", "S3NewRoomB.sc" })
            {
                std::ofstream file(SrcFile(name).c_str(), std::ios::binary);
                file << "(script# 7777)\n";
            }
            GameSession &session = Reopen();

            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Compile);
            Assert::IsTrue(all.has_value(), WideCatalog(all ? std::string() : all.error().ToString()).c_str());
            std::string warnings;
            for (const std::string &warning : all->warnings)
            {
                warnings += warning + "\n";
            }
            Assert::IsTrue(warnings.find("script 7777 is left out") != std::string::npos, WideCatalog(warnings).c_str());
            for (const char *selector : { "7000-8000", "S3NewRoomA" })
            {
                sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { selector }, SelectorMode::Compile);
                Assert::IsFalse(selected.has_value(), WideCatalog(selector).c_str());
                Assert::IsTrue(selected.error().message.find("S3NewRoomB.sc") != std::string::npos, WideCatalog(selected.error().message).c_str());
            }
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, false);
            Assert::IsTrue(rows.has_value() && (RowOf(*rows, 7777) != nullptr), L"list has the script");
        }

        // A derived name takes no file title of src, also not the file of a
        // script in a conflict: a decompile writes the file of the derived
        // name, and it must not write over that file.
        TEST_METHOD(DerivedNames_TakeNoFileOfAConflict)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open(false, { "TitleScreen.sc", "TitleScreen.sco" });
            sci::Result<std::map<uint16_t, std::string>> derived = DeriveScriptNames(session, false);
            Assert::IsTrue(derived.has_value());
            std::string name = derived->at(100);
            for (const std::string &file : { name + ".sc", std::string("S3Other7777.sc") })
            {
                std::ofstream out(SrcFile(file).c_str(), std::ios::binary);
                out << "(script# 7777)\n";
            }
            GameSession &reopened = Reopen();
            Assert::AreEqual(size_t(1), reopened.Helper().ScriptNames->Conflicts().size(), L"setup: the two files are a conflict");
            sci::Result<std::map<uint16_t, std::string>> again = DeriveScriptNames(reopened, false);
            Assert::IsTrue(again.has_value());
            Assert::AreNotEqual(name, again->at(100), L"the derived name must not be the file of the conflict");
        }

        // A file name with a character that the ANSI code page does not have
        // does not stop the open of the game: the map skips the file and
        // reports it.
        TEST_METHOD(FileNameOutsideTheCodePage_IsSkipped)
        {
            NoAppStateForCatalog noAppState;
            BOOL usedDefault = FALSE;
            char narrow[8] = {};
            WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, L"\u03A9", 1, narrow, (int)sizeof(narrow), nullptr, &usedDefault);
            if (!usedDefault)
            {
                Logger::WriteMessage(L"skipped: the ANSI code page has the Greek capital omega");
                return;
            }
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            {
                std::ofstream file(std::filesystem::path(_copyFolder) / L"src" / L"S3\u03A9mega.sc", std::ios::binary);
                file << "(script# 7777)\n";
            }
            GameSession &session = Reopen();
            const ScriptNameMap &names = *session.Helper().ScriptNames;
            Assert::AreEqual(size_t(1), names.SkippedFiles().size());
            Assert::IsTrue(names.SkippedFiles()[0].find("mega.sc") != std::string::npos, WideCatalog(names.SkippedFiles()[0]).c_str());
            Assert::AreEqual(std::string("TitleScreen"), names.NameOf(100), L"the other names are there");
        }

        // A number and a path for one script are an error: the path does not
        // take the place of the script's own file. One file in two spellings
        // is one script.
        TEST_METHOD(Selectors_NumberAndPathForOneScript_AndTwoSpellings)
        {
            NoAppStateForCatalog noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            {
                std::ofstream file(SrcFile("S3Old100.sc").c_str(), std::ios::binary);
                file << "(script# 100)\n";
            }
            GameSession &session = Reopen();
            sci::Result<ScriptSelection> both = ResolveScriptSelectors(session, { "100", "src\\S3Old100.sc" }, SelectorMode::Compile);
            Assert::IsFalse(both.has_value());
            Assert::IsTrue(both.error().message.find("script 100 is also") != std::string::npos, WideCatalog(both.error().message).c_str());
            // A name or a range and a path for one script.
            for (const char *selector : { "TitleScreen", "100-100" })
            {
                sci::Result<ScriptSelection> mixed = ResolveScriptSelectors(session, { selector, "src\\S3Old100.sc" }, SelectorMode::Compile);
                Assert::IsFalse(mixed.has_value(), WideCatalog(selector).c_str());
                Assert::IsTrue(mixed.error().message.find("script 100 is also") != std::string::npos, WideCatalog(mixed.error().message).c_str());
            }

            sci::Result<ScriptSelection> spellings = ResolveScriptSelectors(session, { "src\\TitleScreen.sc", "src\\titlescreen.sc" }, SelectorMode::Compile);
            Assert::IsTrue(spellings.has_value(), WideCatalog(spellings ? std::string() : spellings.error().ToString()).c_str());
            Assert::AreEqual(size_t(1), spellings->scripts.size());
        }

        // A number or a range that is not valid says why, not "no script has
        // this name".
        TEST_METHOD(Selectors_ABadNumberOrRange_SaysWhy)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "200-100", "65536", "0-65536" }, SelectorMode::List);
            Assert::IsFalse(selected.has_value());
            const std::string &message = selected.error().message;
            Assert::IsTrue(message.find("200-100: the first number of a range must not be larger than the second") != std::string::npos, WideCatalog(message).c_str());
            Assert::IsTrue(message.find("65536: a script number is 0 to 65535") != std::string::npos, WideCatalog(message).c_str());
            Assert::IsTrue(message.find("0-65536: a script number is 0 to 65535") != std::string::npos, WideCatalog(message).c_str());
            Assert::IsTrue(message.find("no script has this name") == std::string::npos, WideCatalog(message).c_str());
        }

        // A compile path must be a file in src\.
        TEST_METHOD(Selectors_APathOutsideSrc_IsRefused)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open();
            std::filesystem::copy_file(SrcFile("TitleScreen.sc"), _copyFolder + "\\TitleScreen.sc");
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "TitleScreen.sc" }, SelectorMode::Compile);
            Assert::IsFalse(selected.has_value());
            Assert::IsTrue(selected.error().message.find("the file is not in") != std::string::npos, WideCatalog(selected.error().message).c_str());
        }

        // The derived names (rule 4) count the names of rules 1 to 3 as used;
        // list and decompile take a derived name, compile does not; and
        // AddDerivedScriptNames gives them to the session.
        TEST_METHOD(DerivedNames_UsedNamesSelectorsAndTheSession)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open(false, { "TitleScreen.sc", "TitleScreen.sco" });
            sci::Result<std::map<uint16_t, std::string>> derived = DeriveScriptNames(session, false);
            Assert::IsTrue(derived.has_value());
            std::string name = derived->at(100);
            Assert::IsFalse(name.empty());

            // A file that takes the derived name for another script.
            {
                std::ofstream file(SrcFile(name + ".sc").c_str(), std::ios::binary);
                file << "(script# 7777)\n";
            }
            GameSession &reopened = Reopen();
            sci::Result<std::map<uint16_t, std::string>> again = DeriveScriptNames(reopened, false);
            Assert::IsTrue(again.has_value());
            Assert::AreEqual(name + "_100", again->at(100), L"a name of rules 1 to 3 is used");

            sci::Result<ScriptSelection> decompile = ResolveScriptSelectors(reopened, { name + "_100" }, SelectorMode::Decompile);
            Assert::IsTrue(decompile.has_value(), WideCatalog(decompile ? std::string() : decompile.error().ToString()).c_str());
            Assert::AreEqual(100, (int)NumbersOf(*decompile)[0]);
            Assert::IsFalse(ResolveScriptSelectors(reopened, { name + "_100" }, SelectorMode::Compile).has_value(), L"compile takes no derived name");

            Assert::IsTrue(AddDerivedScriptNames(reopened).has_value());
            Assert::IsTrue(reopened.Helper().ScriptNames->SourceOf(100) == NameSource::Derived);
            Assert::AreEqual(name + "_100", reopened.Helper().ScriptNames->NameOf(100));
        }

        // --all for compile with no game.ini: every src\*.sc that declares a
        // script number.
        TEST_METHOD(SelectAll_Compile_NoGameIni_EverySource)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open(false);
            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Compile);
            Assert::IsTrue(all.has_value(), WideCatalog(all ? std::string() : all.error().ToString()).c_str());
            int sources = 0;
            for (const auto &entry : std::filesystem::directory_iterator(_copyFolder + "\\src"))
            {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](char ch) { return (char)std::tolower((unsigned char)ch); });
                sources += (extension == ".sc") ? 1 : 0;
            }
            Assert::AreEqual((size_t)sources, all->scripts.size());
            for (const ScriptId &script : all->scripts)
            {
                Assert::IsTrue(std::filesystem::exists(script.GetFullPath()), WideCatalog(script.GetFullPath()).c_str());
            }
            Assert::IsTrue(all->warnings.empty());
        }

        TEST_METHOD(SelectAll_Compile_NamedScriptWithNoSource_IsAWarning)
        {
            NoAppStateForCatalog noAppState;
            GameSession &session = Open(true, { "TitleScreen.sc" });
            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Compile);
            Assert::IsTrue(all.has_value());
            std::vector<uint16_t> numbers = NumbersOf(*all);
            Assert::IsTrue(std::find(numbers.begin(), numbers.end(), (uint16_t)100) == numbers.end());
            bool warned = std::any_of(all->warnings.begin(), all->warnings.end(), [](const std::string &warning) { return warning.find("TitleScreen.sc") != std::string::npos; });
            Assert::IsTrue(warned, L"the script with no source file is a warning");

            sci::Result<ScriptSelection> one = ResolveScriptSelectors(session, { "TitleScreen" }, SelectorMode::Compile);
            Assert::IsFalse(one.has_value(), L"one script with no source file is a usage error");
        }

        // The patch files that would hide a package write: a standard name and
        // another name for one resource, other types, and a file whose first
        // byte is another type (not a patch of the resource).
        TEST_METHOD(ShadowingPatches_StandardAndOtherNames)
        {
            NoAppStateForCatalog noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            uint8_t script = 0x80 | (uint8_t)ResourceType::Script;
            WriteBytes(_copyFolder + "\\100.scr", { script, 0, 1, 2 });
            WriteBytes(_copyFolder + "\\0100.scr", { script, 0, 1, 2 });
            WriteBytes(_copyFolder + "\\100.hep", { 0x80 | (uint8_t)ResourceType::Heap, 0, 1, 2 });
            WriteBytes(_copyFolder + "\\996.voc", { 0x80 | (uint8_t)ResourceType::Vocab, 0, 1, 2 });
            WriteBytes(_copyFolder + "\\101.scr", { script, 0, 1, 2 });
            WriteBytes(_copyFolder + "\\102.scr", { 0x80 | (uint8_t)ResourceType::View, 0, 1, 2 });
            GameFolderHelper helper;
            helper.GameFolder = _copyFolder;

            sci::Result<std::vector<std::string>> files = FindShadowingPatches(helper, {
                { ResourceType::Script, 100 }, { ResourceType::Heap, 100 }, { ResourceType::Vocab, 996 }, { ResourceType::Script, 102 }, { ResourceType::Vocab, 997 } });
            Assert::IsTrue(files.has_value());
            std::vector<std::string> expected = { _copyFolder + "\\0100.scr", _copyFolder + "\\100.hep", _copyFolder + "\\100.scr", _copyFolder + "\\996.voc" };
            std::sort(expected.begin(), expected.end());
            std::string actual;
            for (const std::string &file : *files)
            {
                actual += file + "\n";
            }
            Assert::IsTrue(expected == *files, WideCatalog(actual).c_str());
        }

        // The shadow check sees the patch files as the patch file reader
        // does: a 1-byte file is no resource, and 105.hep with a script's
        // type byte is script 105 when scripts and heaps are read together.
        TEST_METHOD(ShadowingPatches_AsThePatchReaderSeesThem)
        {
            NoAppStateForCatalog noAppState;
            _copyFolder = CopyGameFromModuleFolder("\\TemplateGame\\SCI1.1");
            uint8_t script = 0x80 | (uint8_t)ResourceType::Script;
            WriteBytes(_copyFolder + "\\103.scr", { script });
            WriteBytes(_copyFolder + "\\105.hep", { script, 0, 1, 2 });
            GameFolderHelper helper;
            helper.GameFolder = _copyFolder;

            sci::Result<std::vector<std::string>> files = FindShadowingPatches(helper, {
                { ResourceType::Script, 103 }, { ResourceType::Script, 105 }, { ResourceType::Heap, 105 } });
            Assert::IsTrue(files.has_value());
            std::vector<std::string> expected = { _copyFolder + "\\105.hep" };
            std::string actual;
            for (const std::string &file : *files)
            {
                actual += file + "\n";
            }
            Assert::IsTrue(expected == *files, WideCatalog(actual).c_str());
        }
    };
}
