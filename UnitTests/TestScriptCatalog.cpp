#include "stdafx.h"
#include "CppUnitTest.h"
#include "GameSession.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "GameFolderHelper.h"
#include "ScriptNameMap.h"
#include "ScriptCatalog.h"
#include "Helper.h"
#include "TestSupport.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
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
}

namespace UnitTests
{
    // The script list, the script selectors and the shadow check of the
    // command line (plan sections 4.2, 4.3 and 5).
    TEST_CLASS(TestScriptCatalog)
    {
        NoAppState _noAppState;
        GameCopy _game;

        // A copy of the SCI1.1 template, with the changes made before the
        // open, in a session with the default options.
        GameSession &Open(bool keepGameIni = true, const std::vector<std::string> &removeFromSrc = std::vector<std::string>())
        {
            _game.Make(TemplateSci11);
            if (!keepGameIni)
            {
                std::filesystem::remove(_game.Path("game.ini"));
            }
            for (const std::string &file : removeFromSrc)
            {
                std::filesystem::remove(_game.Src(file));
            }
            return _game.Open(SessionOptions());
        }

        // Writes the script data as a patch file of the game.
        void WriteScriptPatch(GameSession &session, uint16_t number, const std::vector<uint8_t> &data)
        {
            const GameFolderHelper &helper = session.Helper();
            ResourceBlob patch(helper, nullptr, ResourceType::Script, data, helper.Version.DefaultVolumeFile, number, NoBase36, helper.Version, ResourceSourceFlags::PatchFile);
            AssertOk(session.ResourceMap().WriteResource(patch));
        }

    public:
        TEST_METHOD(List_Template_NamesAndLocations)
        {
            GameSession &session = Open();
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, false);
            Assert::IsTrue(ValueOf(rows).size() > 80);
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
            GameSession &session = Open();
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, true);
            Assert::AreEqual(std::string("Main"), RowOf(ValueOf(rows), 0)->derivedName);
            int derived = 0;
            for (const ScriptRow &row : *rows)
            {
                derived += row.derivedName.empty() ? 0 : 1;
                Assert::IsTrue(row.error.empty(), Wide(row.error).c_str());
            }
            Assert::IsTrue(derived > 60, L"most scripts have a class or a public instance");
        }

        // A compiled script with no game.ini entry, no .sc and no .sco gets
        // its derived name (rule 4).
        TEST_METHOD(List_NoNameFromTheFiles_DerivesTheName)
        {
            GameSession &session = Open(false, { "TitleScreen.sc", "TitleScreen.sco" });
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, false);
            const ScriptRow *title = RowOf(ValueOf(rows), 100);
            Assert::IsNotNull(title);
            Assert::IsTrue(title->source == NameSource::Derived);
            Assert::IsTrue(!title->name.empty() && (title->name != "n100"), Wide(title->name).c_str());
            Assert::IsFalse(title->hasSource);
            Assert::IsTrue(RowOf(*rows, 0)->source == NameSource::Source, L"Main still comes from Main.sc");
        }

        TEST_METHOD(List_PatchFileAndUnreadableScript)
        {
            GameSession &session = Open();
            std::unique_ptr<ResourceBlob> script = session.Helper().MostRecentResource(ResourceType::Script, 100, ResourceEnumFlags::None);
            Assert::IsTrue(script != nullptr);
            WriteScriptPatch(session, 100, std::vector<uint8_t>(script->GetData(), script->GetData() + script->GetLength()));
            // A cut copy of script 255 as a patch file.
            std::unique_ptr<ResourceBlob> controls = session.Helper().MostRecentResource(ResourceType::Script, 255, ResourceEnumFlags::None);
            Assert::IsTrue(controls && (controls->GetLength() > 10));
            WriteScriptPatch(session, 255, std::vector<uint8_t>(controls->GetData(), controls->GetData() + 10));

            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, true);
            Assert::AreEqual(std::string("100.scr (patch)"), RowOf(ValueOf(rows), 100)->location);
            Assert::IsTrue(RowOf(*rows, 100)->error.empty());
            const ScriptRow *cut = RowOf(*rows, 255);
            Assert::IsFalse(cut->error.empty(), L"a script that cannot be read shows its error");
            Assert::AreEqual(std::string("Controls"), cut->name, L"and keeps its name");
        }

        TEST_METHOD(Selectors_NumbersRangesNamesAndDuplicates)
        {
            GameSession &session = Open();
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "255", "Main", "main", "0", "100-100", "titlescreen" }, SelectorMode::Decompile);
            std::vector<uint16_t> numbers = NumbersOf(ValueOf(selected));
            Assert::AreEqual(size_t(3), numbers.size(), L"0 and Main and main are one script");
            Assert::AreEqual(0, (int)numbers[0]);
            Assert::AreEqual(100, (int)numbers[1]);
            Assert::AreEqual(255, (int)numbers[2]);
            // ScriptId keeps its path in lower case.
            Assert::AreEqual(0, _stricmp(_game.Src("TitleScreen.sc").c_str(), selected->scripts[1].GetFullPath().c_str()), Wide(selected->scripts[1].GetFullPath()).c_str());
        }

        TEST_METHOD(Selectors_EveryBadSelectorIsInOneUsageError)
        {
            GameSession &session = Open();
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "NoSuchScript", "0", "4000", "3000-3100" }, SelectorMode::Compile);
            Assert::IsFalse(selected.has_value());
            const sci::Error &error = selected.error();
            Assert::IsTrue(error.code == sci::ErrorCode::Usage);
            Assert::IsTrue(error.message.find("NoSuchScript") != std::string::npos, Wide(error.message).c_str());
            Assert::IsTrue(error.message.find("4000") != std::string::npos, Wide(error.message).c_str());
            Assert::IsTrue(error.message.find("3000-3100") != std::string::npos, Wide(error.message).c_str());
            Assert::IsTrue(error.message.find("scic script list") != std::string::npos);
        }

        TEST_METHOD(Selectors_PathIsForCompileOnly)
        {
            GameSession &session = Open();
            sci::Result<ScriptSelection> compile = ResolveScriptSelectors(session, { "src\\TitleScreen.sc" }, SelectorMode::Compile);
            Assert::AreEqual(size_t(1), ValueOf(compile).scripts.size());
            Assert::AreEqual(100, (int)compile->scripts[0].GetResourceNumber());

            sci::Result<ScriptSelection> decompile = ResolveScriptSelectors(session, { "src\\TitleScreen.sc" }, SelectorMode::Decompile);
            Assert::IsFalse(decompile.has_value());
            Assert::IsTrue(decompile.error().message.find("only compile takes a path") != std::string::npos, Wide(decompile.error().message).c_str());

            sci::Result<ScriptSelection> header = ResolveScriptSelectors(session, { "src\\game.sh" }, SelectorMode::Compile);
            Assert::IsFalse(header.has_value());
            Assert::IsTrue(header.error().message.find("a header file cannot be compiled") != std::string::npos, Wide(header.error().message).c_str());
        }

        // Compile keeps the order of game.ini [Script]; decompile uses number
        // order. The template's [Script] section is not in number order.
        TEST_METHOD(Selectors_CompileOrderFollowsGameIni)
        {
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
            Assert::AreEqual((int)first, (int)NumbersOf(ValueOf(compile))[0], L"compile: the game.ini order");
            sci::Result<ScriptSelection> decompile = ResolveScriptSelectors(session, selectors, SelectorMode::Decompile);
            Assert::AreEqual((int)second, (int)NumbersOf(ValueOf(decompile))[0], L"decompile: number order");
        }

        // A mode that writes refuses a selection with a script in a name
        // conflict, with the conflict and its fix. A selection without that
        // script works, --all leaves the script out with a warning, and the
        // list works. A conflict refuses no selection without its scripts:
        // real projects have two game.ini names that differ only in case.
        TEST_METHOD(Selectors_Conflict_RefusesOnlyItsScripts)
        {
            _game.Make(TemplateSci11);
            std::filesystem::remove(_game.Path("game.ini"));
            std::filesystem::copy_file(_game.Src("TitleScreen.sc"), _game.Src("Title2.sc"));
            GameSession &session = _game.Open(SessionOptions());

            sci::Result<ScriptSelection> conflicted = ResolveScriptSelectors(session, { "100" }, SelectorMode::Compile);
            Assert::IsFalse(conflicted.has_value());
            Assert::IsTrue(conflicted.error().code == sci::ErrorCode::Usage);
            Assert::IsTrue(conflicted.error().message.find("Title2.sc") != std::string::npos, Wide(conflicted.error().message).c_str());
            Assert::IsTrue(conflicted.error().message.find("Keep one of the files") != std::string::npos, Wide(conflicted.error().message).c_str());

            AssertOk(ResolveScriptSelectors(session, { "0" }, SelectorMode::Compile));

            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Decompile);
            std::vector<uint16_t> numbers = NumbersOf(ValueOf(all));
            Assert::IsTrue(std::find(numbers.begin(), numbers.end(), (uint16_t)100) == numbers.end(), L"--all leaves out script 100");
            Assert::IsTrue(std::find(numbers.begin(), numbers.end(), (uint16_t)0) != numbers.end());
            Assert::AreEqual(size_t(1), all->warnings.size());
            Assert::IsTrue(all->warnings[0].find("script 100") != std::string::npos, Wide(all->warnings[0]).c_str());

            AssertOk(ResolveScriptSelectors(session, { "100" }, SelectorMode::List));
        }

        // A script name can have a '.' (real games have n993=gamefile.sh), so
        // a name wins over a path.
        TEST_METHOD(Selectors_ANameWithADot_IsAName)
        {
            _game.Make(TemplateSci11);
            WritePrivateProfileStringA("Script", "n7777", "my.script", _game.Path("game.ini").c_str());
            GameSession &session = _game.Open(SessionOptions());
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "my.script" }, SelectorMode::List);
            Assert::AreEqual(7777, (int)NumbersOf(ValueOf(selected))[0]);
        }

        // Two paths for one script number are an error; the selection makes
        // a path normal (no "..", only '\').
        TEST_METHOD(Selectors_TwoPathsForOneScript_AndANormalPath)
        {
            _game.Make(TemplateSci11);
            std::filesystem::copy_file(_game.Src("TitleScreen.sc"), _game.Src("Title2.sc"));
            GameSession &session = _game.Open(SessionOptions());

            sci::Result<ScriptSelection> two = ResolveScriptSelectors(session, { "src\\TitleScreen.sc", "src\\Title2.sc" }, SelectorMode::Compile);
            Assert::IsFalse(two.has_value());
            Assert::IsTrue(two.error().message.find("script 100 is also") != std::string::npos, Wide(two.error().message).c_str());

            sci::Result<ScriptSelection> normal = ResolveScriptSelectors(session, { "src\\..\\src/TitleScreen.sc" }, SelectorMode::Compile);
            AssertOk(normal);
            Assert::AreEqual(0, _stricmp(_game.Src("TitleScreen.sc").c_str(), normal->scripts[0].GetFullPath().c_str()), Wide(normal->scripts[0].GetFullPath()).c_str());
        }

        // Two files that declare a script that the game has not compiled are
        // a conflict too. --all, a range and the name of one of the files
        // show it, and list has the script.
        TEST_METHOD(Conflict_OfAnUncompiledScript_IsShown)
        {
            _game.Make(TemplateSci11);
            WriteFileText(_game.Src("S3NewRoomA.sc"), "(script# 7777)\n");
            WriteFileText(_game.Src("S3NewRoomB.sc"), "(script# 7777)\n");
            GameSession &session = _game.Open(SessionOptions());

            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Compile);
            std::string warnings = JoinLines(ValueOf(all).warnings);
            Assert::IsTrue(warnings.find("script 7777 is left out") != std::string::npos, Wide(warnings).c_str());
            for (const char *selector : { "7000-8000", "S3NewRoomA" })
            {
                sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { selector }, SelectorMode::Compile);
                Assert::IsFalse(selected.has_value(), Wide(selector).c_str());
                Assert::IsTrue(selected.error().message.find("S3NewRoomB.sc") != std::string::npos, Wide(selected.error().message).c_str());
            }
            sci::Result<std::vector<ScriptRow>> rows = ListScripts(session, false);
            Assert::IsTrue(rows.has_value() && (RowOf(*rows, 7777) != nullptr), L"list has the script");
        }

        // A derived name takes no file title of src, also not the file of a
        // script in a conflict: a decompile writes the file of the derived
        // name, and it must not write over that file.
        TEST_METHOD(DerivedNames_TakeNoFileOfAConflict)
        {
            GameSession &session = Open(false, { "TitleScreen.sc", "TitleScreen.sco" });
            sci::Result<std::map<uint16_t, std::string>> derived = DeriveScriptNames(session, false);
            std::string name = ValueOf(derived).at(100);
            WriteFileText(_game.Src(name + ".sc"), "(script# 7777)\n");
            WriteFileText(_game.Src("S3Other7777.sc"), "(script# 7777)\n");
            _game.CloseSessions();
            GameSession &reopened = _game.Open(SessionOptions());
            Assert::AreEqual(size_t(1), reopened.Helper().ScriptNames->Conflicts().size(), L"setup: the two files are a conflict");
            sci::Result<std::map<uint16_t, std::string>> again = DeriveScriptNames(reopened, false);
            Assert::AreNotEqual(name, ValueOf(again).at(100), L"the derived name must not be the file of the conflict");
        }

        // A file name with a character that the ANSI code page does not have
        // does not stop the open of the game: the map skips the file and
        // reports it.
        TEST_METHOD(FileNameOutsideTheCodePage_IsSkipped)
        {
            BOOL usedDefault = FALSE;
            char narrow[8] = {};
            WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, L"\u03A9", 1, narrow, (int)sizeof(narrow), nullptr, &usedDefault);
            if (!usedDefault)
            {
                Logger::WriteMessage(L"skipped: the ANSI code page has the Greek capital omega");
                return;
            }
            _game.Make(TemplateSci11);
            {
                std::ofstream file(std::filesystem::path(_game.Folder()) / L"src" / L"S3\u03A9mega.sc", std::ios::binary);
                file << "(script# 7777)\n";
            }
            GameSession &session = _game.Open(SessionOptions());
            const ScriptNameMap &names = *session.Helper().ScriptNames;
            Assert::AreEqual(size_t(1), names.SkippedFiles().size());
            Assert::IsTrue(names.SkippedFiles()[0].find("mega.sc") != std::string::npos, Wide(names.SkippedFiles()[0]).c_str());
            Assert::AreEqual(std::string("TitleScreen"), names.NameOf(100), L"the other names are there");
        }

        // A name of game.ini with a folder in it is not a file name: the map
        // does not use it (the name comes from src), and reports it.
        TEST_METHOD(GameIniNameWithAFolder_IsIgnored)
        {
            _game.Make(TemplateSci11);
            Assert::IsTrue(WritePrivateProfileStringA("Script", "n100", "src\\TitleScreen", _game.Path("game.ini").c_str()) != 0);
            GameSession &session = _game.Open(SessionOptions());
            const ScriptNameMap &names = *session.Helper().ScriptNames;
            Assert::AreEqual(std::string("TitleScreen"), names.NameOf(100), L"the name of the source");
            Assert::AreEqual(size_t(1), names.IgnoredGameIniNames().size());
            Assert::IsTrue(names.IgnoredGameIniNames()[0].find("src\\TitleScreen") != std::string::npos, Wide(names.IgnoredGameIniNames()[0]).c_str());
        }

        // A number and a path for one script are an error: the path does not
        // take the place of the script's own file. One file in two spellings
        // is one script.
        TEST_METHOD(Selectors_NumberAndPathForOneScript_AndTwoSpellings)
        {
            _game.Make(TemplateSci11);
            WriteFileText(_game.Src("S3Old100.sc"), "(script# 100)\n");
            GameSession &session = _game.Open(SessionOptions());
            sci::Result<ScriptSelection> both = ResolveScriptSelectors(session, { "100", "src\\S3Old100.sc" }, SelectorMode::Compile);
            Assert::IsFalse(both.has_value());
            Assert::IsTrue(both.error().message.find("script 100 is also") != std::string::npos, Wide(both.error().message).c_str());
            // A name or a range and a path for one script.
            for (const char *selector : { "TitleScreen", "100-100" })
            {
                sci::Result<ScriptSelection> mixed = ResolveScriptSelectors(session, { selector, "src\\S3Old100.sc" }, SelectorMode::Compile);
                Assert::IsFalse(mixed.has_value(), Wide(selector).c_str());
                Assert::IsTrue(mixed.error().message.find("script 100 is also") != std::string::npos, Wide(mixed.error().message).c_str());
            }

            sci::Result<ScriptSelection> spellings = ResolveScriptSelectors(session, { "src\\TitleScreen.sc", "src\\titlescreen.sc" }, SelectorMode::Compile);
            Assert::AreEqual(size_t(1), ValueOf(spellings).scripts.size());
        }

        // A number or a range that is not valid says why, not "no script has
        // this name".
        TEST_METHOD(Selectors_ABadNumberOrRange_SaysWhy)
        {
            GameSession &session = Open();
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "200-100", "65536", "0-65536" }, SelectorMode::List);
            Assert::IsFalse(selected.has_value());
            const std::string &message = selected.error().message;
            Assert::IsTrue(message.find("200-100: the first number of a range must not be larger than the second") != std::string::npos, Wide(message).c_str());
            Assert::IsTrue(message.find("65536: a script number is 0 to 65535") != std::string::npos, Wide(message).c_str());
            Assert::IsTrue(message.find("0-65536: a script number is 0 to 65535") != std::string::npos, Wide(message).c_str());
            Assert::IsTrue(message.find("no script has this name") == std::string::npos, Wide(message).c_str());
        }

        // A compile path must be a file in src\.
        TEST_METHOD(Selectors_APathOutsideSrc_IsRefused)
        {
            GameSession &session = Open();
            std::filesystem::copy_file(_game.Src("TitleScreen.sc"), _game.Path("TitleScreen.sc"));
            sci::Result<ScriptSelection> selected = ResolveScriptSelectors(session, { "TitleScreen.sc" }, SelectorMode::Compile);
            Assert::IsFalse(selected.has_value());
            Assert::IsTrue(selected.error().message.find("the file is not in") != std::string::npos, Wide(selected.error().message).c_str());
        }

        // The derived names (rule 4) count the names of rules 1 to 3 as used;
        // list and decompile take a derived name, compile does not; and
        // AddDerivedScriptNames gives them to the session.
        TEST_METHOD(DerivedNames_UsedNamesSelectorsAndTheSession)
        {
            GameSession &session = Open(false, { "TitleScreen.sc", "TitleScreen.sco" });
            sci::Result<std::map<uint16_t, std::string>> derived = DeriveScriptNames(session, false);
            std::string name = ValueOf(derived).at(100);
            Assert::IsFalse(name.empty());

            // A file that takes the derived name for another script.
            WriteFileText(_game.Src(name + ".sc"), "(script# 7777)\n");
            _game.CloseSessions();
            GameSession &reopened = _game.Open(SessionOptions());
            sci::Result<std::map<uint16_t, std::string>> again = DeriveScriptNames(reopened, false);
            Assert::AreEqual(name + "_100", ValueOf(again).at(100), L"a name of rules 1 to 3 is used");

            sci::Result<ScriptSelection> decompile = ResolveScriptSelectors(reopened, { name + "_100" }, SelectorMode::Decompile);
            Assert::AreEqual(100, (int)NumbersOf(ValueOf(decompile))[0]);
            Assert::IsFalse(ResolveScriptSelectors(reopened, { name + "_100" }, SelectorMode::Compile).has_value(), L"compile takes no derived name");

            AssertOk(AddDerivedScriptNames(reopened));
            Assert::IsTrue(reopened.Helper().ScriptNames->SourceOf(100) == NameSource::Derived);
            Assert::AreEqual(name + "_100", reopened.Helper().ScriptNames->NameOf(100));
        }

        // --all for compile with no game.ini: every src\*.sc that declares a
        // script number.
        TEST_METHOD(SelectAll_Compile_NoGameIni_EverySource)
        {
            GameSession &session = Open(false);
            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Compile);
            AssertOk(all);
            int sources = 0;
            for (const auto &entry : std::filesystem::directory_iterator(_game.Path("src")))
            {
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](char ch) { return (char)std::tolower((unsigned char)ch); });
                sources += (extension == ".sc") ? 1 : 0;
            }
            Assert::AreEqual((size_t)sources, all->scripts.size());
            for (const ScriptId &script : all->scripts)
            {
                Assert::IsTrue(std::filesystem::exists(script.GetFullPath()), Wide(script.GetFullPath()).c_str());
            }
            Assert::IsTrue(all->warnings.empty());
        }

        TEST_METHOD(SelectAll_Compile_NamedScriptWithNoSource_IsAWarning)
        {
            GameSession &session = Open(true, { "TitleScreen.sc" });
            sci::Result<ScriptSelection> all = SelectAllScripts(session, SelectorMode::Compile);
            std::vector<uint16_t> numbers = NumbersOf(ValueOf(all));
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
            _game.Make(TemplateSci11);
            uint8_t script = 0x80 | (uint8_t)ResourceType::Script;
            WriteFileBytes(_game.Path("100.scr"), { script, 0, 1, 2 });
            WriteFileBytes(_game.Path("0100.scr"), { script, 0, 1, 2 });
            WriteFileBytes(_game.Path("100.hep"), { 0x80 | (uint8_t)ResourceType::Heap, 0, 1, 2 });
            WriteFileBytes(_game.Path("996.voc"), { 0x80 | (uint8_t)ResourceType::Vocab, 0, 1, 2 });
            WriteFileBytes(_game.Path("101.scr"), { script, 0, 1, 2 });
            WriteFileBytes(_game.Path("102.scr"), { 0x80 | (uint8_t)ResourceType::View, 0, 1, 2 });
            GameFolderHelper helper;
            helper.GameFolder = _game.Folder();

            sci::Result<std::vector<std::string>> files = FindShadowingPatches(helper, {
                { ResourceType::Script, 100 }, { ResourceType::Heap, 100 }, { ResourceType::Vocab, 996 }, { ResourceType::Script, 102 }, { ResourceType::Vocab, 997 } });
            AssertOk(files);
            std::vector<std::string> expected = { _game.Path("0100.scr"), _game.Path("100.hep"), _game.Path("100.scr"), _game.Path("996.voc") };
            std::sort(expected.begin(), expected.end());
            Assert::IsTrue(expected == *files, Wide(JoinLines(*files)).c_str());
        }

        // The shadow check sees the patch files as the patch file reader
        // does: a 1-byte file is no resource, and 105.hep with a script's
        // type byte is script 105 when scripts and heaps are read together.
        TEST_METHOD(ShadowingPatches_AsThePatchReaderSeesThem)
        {
            _game.Make(TemplateSci11);
            uint8_t script = 0x80 | (uint8_t)ResourceType::Script;
            WriteFileBytes(_game.Path("103.scr"), { script });
            WriteFileBytes(_game.Path("105.hep"), { script, 0, 1, 2 });
            GameFolderHelper helper;
            helper.GameFolder = _game.Folder();

            sci::Result<std::vector<std::string>> files = FindShadowingPatches(helper, {
                { ResourceType::Script, 103 }, { ResourceType::Script, 105 }, { ResourceType::Heap, 105 } });
            AssertOk(files);
            std::vector<std::string> expected = { _game.Path("105.hep") };
            Assert::IsTrue(expected == *files, Wide(JoinLines(*files)).c_str());
        }
    };
}
