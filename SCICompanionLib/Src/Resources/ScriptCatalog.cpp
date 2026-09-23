#include "stdafx.h"
#include "ScriptCatalog.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ResourceMap.h"
#include "ResourceBlob.h"
#include "ResourceContainer.h"
#include "ResourceUtil.h"
#include "CompiledScript.h"
#include "format.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace
{
    namespace fs = std::filesystem;

    std::string UpperText(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](char ch) { return (char)std::toupper((unsigned char)ch); });
        return text;
    }

    std::string JoinText(const std::vector<std::string> &items, const std::string &separator)
    {
        std::string text;
        for (const std::string &item : items)
        {
            if (!text.empty())
            {
                text += separator;
            }
            text += item;
        }
        return text;
    }

    // A decimal script number, up to 65535.
    bool ParseScriptNumber(const std::string &text, uint16_t &number)
    {
        if (text.empty() || (text.size() > 5) || !std::all_of(text.begin(), text.end(), [](char ch) { return (ch >= '0') && (ch <= '9'); }))
        {
            return false;
        }
        unsigned long value = std::stoul(text);
        if (value > 0xffff)
        {
            return false;
        }
        number = (uint16_t)value;
        return true;
    }

    // "100-199".
    bool ParseRange(const std::string &text, uint16_t &first, uint16_t &last)
    {
        size_t dash = text.find('-');
        return (dash != std::string::npos) && ParseScriptNumber(text.substr(0, dash), first) &&
            ParseScriptNumber(text.substr(dash + 1), last) && (first <= last);
    }

    bool LooksLikePath(const std::string &text)
    {
        return (text.find('\\') != std::string::npos) || (text.find('/') != std::string::npos) ||
            (text.find('.') != std::string::npos) || (text.find(':') != std::string::npos);
    }

    // A name from rules 1 to 3: a file in src\ has it, or game.ini names it.
    bool HasFileName(const ScriptNameMap *names, uint16_t number)
    {
        if (!names)
        {
            return false;
        }
        NameSource source = names->SourceOf(number);
        return (source == NameSource::GameIni) || (source == NameSource::Source) || (source == NameSource::Sco);
    }

    bool FileExists(const std::string &path)
    {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    // The patch files of one resource type in the game folder, by number, as
    // the patch file source finds them: the file name matches the type's
    // patterns and gives a number, and the file's first byte is the type.
    std::map<uint16_t, std::vector<std::string>> PatchFilesOf(const GameFolderHelper &helper, ResourceType type)
    {
        std::map<uint16_t, std::vector<std::string>> files;
        if (helper.GameFolder.empty() || ((int)type < 0) || ((int)type >= (int)ResourceType::Max))
        {
            return files;
        }
        const char *spec = g_szResourceSpecByType[(int)type];
        WIN32_FIND_DATAA findData;
        HANDLE find = FindFirstFileA((helper.GameFolder + "\\*.*").c_str(), &findData);
        if (find == INVALID_HANDLE_VALUE)
        {
            return files;
        }
        do
        {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !PathMatchSpecA(findData.cFileName, spec))
            {
                continue;
            }
            int number = ResourceNumberFromFileName(findData.cFileName);
            if ((number < 0) || (number > 0xffff))
            {
                continue;
            }
            std::string path = helper.GameFolder + "\\" + findData.cFileName;
            std::ifstream file(path, std::ios::binary);
            char first = 0;
            if (file.read(&first, 1) && ((((unsigned char)first) & 0x7f) == (unsigned char)type))
            {
                files[(uint16_t)number].push_back(path);
            }
        } while (FindNextFileA(find, &findData));
        FindClose(find);
        for (auto &entry : files)
        {
            std::sort(entry.second.begin(), entry.second.end());
        }
        return files;
    }

    struct CompiledInfo
    {
        std::string location;
        bool loaded = false;
        std::string error;
        ScriptObjectsForNaming objects;
    };

    // The compiled scripts of the game (a patch file wins, as in the game),
    // with where each one is. With readObjects, each script is loaded for the
    // objects that the naming rule reads: the "some decompilation" of plan
    // section 4.3. One pass over the resources: a lookup of each script
    // costs about 15 ms.
    std::map<uint16_t, CompiledInfo> ReadCompiledScripts(GameSession &session, bool readObjects)
    {
        const GameFolderHelper &helper = session.Helper();
        std::map<uint16_t, std::unique_ptr<ResourceBlob>> scriptBlobs;
        std::map<uint16_t, std::unique_ptr<ResourceBlob>> heapBlobs;
        ResourceTypeFlags types = ResourceTypeFlags::Script;
        if (readObjects && helper.Version.SeparateHeapResources)
        {
            types = ResourceTypeFlags::Script | ResourceTypeFlags::Heap;
        }
        auto container = helper.Resources(types, ResourceEnumFlags::MostRecentOnly);
        for (auto &&blob : *container)
        {
            uint16_t number = (uint16_t)blob->GetNumber();
            if (blob->GetType() == ResourceType::Script)
            {
                scriptBlobs[number] = std::move(blob);
            }
            else
            {
                heapBlobs[number] = std::move(blob);
            }
        }

        std::map<uint16_t, std::vector<std::string>> patchFiles = PatchFilesOf(helper, ResourceType::Script);
        std::map<uint16_t, CompiledInfo> scripts;
        for (auto &script : scriptBlobs)
        {
            CompiledInfo &info = scripts[script.first];
            const ResourceBlob &blob = *script.second;
            if (blob.GetSourceFlags() == ResourceSourceFlags::PatchFile)
            {
                auto patch = patchFiles.find(script.first);
                std::string file = (patch != patchFiles.end()) ? fs::path(patch->second[0]).filename().string() :
                    GetFileNameFor(ResourceType::Script, script.first, NoBase36, helper.Version);
                info.location = file + " (patch)";
            }
            else
            {
                info.location = fmt::format("resource.{0:03}", blob.GetPackageHint());
            }
            if (readObjects)
            {
                auto heap = heapBlobs.find(script.first);
                CompiledScript compiled(script.first);
                sci::Status loaded = compiled.TryLoad(helper, helper.Version, script.first, blob, (heap != heapBlobs.end()) ? heap->second.get() : nullptr);
                if (loaded)
                {
                    info.loaded = true;
                    info.objects.number = script.first;
                    for (const auto &object : compiled.GetObjects())
                    {
                        info.objects.objects.push_back({ object->GetName(), !object->IsInstance(), object->IsPublic });
                    }
                }
                else
                {
                    info.error = loaded.error().ToString();
                }
            }
        }
        return scripts;
    }

    sci::Status CheckNoConflict(const ScriptNameMap &names, SelectorMode mode)
    {
        if ((mode == SelectorMode::List) || names.Conflicts().empty())
        {
            return sci::Ok();
        }
        return sci::Fail(sci::ErrorCode::Usage, "The script names have a conflict. Remove or rename a file, then run again:\n  " +
            JoinText(names.Conflicts(), "\n  "));
    }

    // Compile keeps the order of game.ini [Script], then number order.
    std::vector<uint16_t> InModeOrder(const std::set<uint16_t> &numbers, const ScriptNameMap &names, SelectorMode mode)
    {
        std::vector<uint16_t> ordered;
        std::set<uint16_t> placed;
        if (mode == SelectorMode::Compile)
        {
            for (uint16_t number : names.GameIniOrder())
            {
                if ((numbers.find(number) != numbers.end()) && placed.insert(number).second)
                {
                    ordered.push_back(number);
                }
            }
        }
        for (uint16_t number : numbers)
        {
            if (placed.insert(number).second)
            {
                ordered.push_back(number);
            }
        }
        return ordered;
    }

    // The state that the selectors share: the names, the compiled scripts,
    // and the derived names (read only when a selector needs them).
    class Selection
    {
    public:
        Selection(GameSession &session, SelectorMode mode) :
            _session(session),
            _helper(session.Helper()),
            _names(session.Helper().ScriptNames),
            _mode(mode),
            _compiled(ReadCompiledScripts(session, false))
        {
        }

        const ScriptNameMap &Names() const { return *_names; }

        std::string NameOf(uint16_t number)
        {
            if (!HasFileName(_names.get(), number) && _TakesDerivedNames())
            {
                const std::map<uint16_t, std::string> &derived = _Derived();
                auto name = derived.find(number);
                if (name != derived.end())
                {
                    return name->second;
                }
            }
            return _names->NameOf(number);
        }

        // Every script that the list shows: compiled, or with a name.
        std::set<uint16_t> Known() const
        {
            std::set<uint16_t> numbers;
            for (const auto &script : _compiled)
            {
                numbers.insert(script.first);
            }
            for (const auto &entry : _names->Entries())
            {
                numbers.insert(entry.first);
            }
            return numbers;
        }

        // True when the mode takes the script; otherwise why is the reason.
        bool Takes(uint16_t number, std::string &why)
        {
            bool compiled = (_compiled.find(number) != _compiled.end());
            std::string source = _helper.GetScriptFileName(NameOf(number));
            switch (_mode)
            {
            case SelectorMode::List:
            {
                std::set<uint16_t> known = Known();
                if (known.find(number) == known.end())
                {
                    why = fmt::format("the game has no script {0}", number);
                    return false;
                }
                return true;
            }
            case SelectorMode::Decompile:
                if (!compiled)
                {
                    why = fmt::format("the game has no compiled script {0}", number);
                    return false;
                }
                return true;
            case SelectorMode::Compile:
                if (!FileExists(source))
                {
                    why = fmt::format("script {0} has no source file {1}", number, source);
                    return false;
                }
                return true;
            default:
                if (!compiled || !FileExists(source))
                {
                    why = !compiled ? fmt::format("the game has no compiled script {0}", number) : fmt::format("script {0} has no source file {1}", number, source);
                    return false;
                }
                return true;
            }
        }

        // The number of a name, ignoring case: rules 1 to 3, then (List and
        // Decompile) a derived name, then nNNN.
        bool NumberOfName(const std::string &name, uint16_t &number)
        {
            uint16_t found;
            if (_names->NumberOf(name, found) && HasFileName(_names.get(), found))
            {
                number = found;
                return true;
            }
            if (_TakesDerivedNames())
            {
                std::string upper = UpperText(name);
                for (const auto &derived : _Derived())
                {
                    if (UpperText(derived.second) == upper)
                    {
                        number = derived.first;
                        return true;
                    }
                }
            }
            if (_names->NumberOf(name, found))
            {
                number = found;
                return true;
            }
            return false;
        }

    private:
        bool _TakesDerivedNames() const
        {
            return (_mode == SelectorMode::List) || (_mode == SelectorMode::Decompile);
        }

        const std::map<uint16_t, std::string> &_Derived()
        {
            if (!_derivedRead)
            {
                _derived = DeriveScriptNames(_session, false);
                _derivedRead = true;
            }
            return _derived;
        }

        GameSession &_session;
        const GameFolderHelper &_helper;
        std::shared_ptr<const ScriptNameMap> _names;
        SelectorMode _mode;
        std::map<uint16_t, CompiledInfo> _compiled;
        std::map<uint16_t, std::string> _derived;
        bool _derivedRead = false;
    };

    // A path selector (compile only): a .sc file in the game's src folder.
    bool ResolvePath(Selection &selection, const GameFolderHelper &helper, const std::string &selector, uint16_t &number, std::string &path, std::string &why)
    {
        fs::path given(selector);
        if (given.is_relative())
        {
            fs::path inGame = fs::path(helper.GameFolder) / given;
            std::error_code ec;
            given = fs::exists(inGame, ec) ? inGame : fs::absolute(given, ec);
        }
        std::string extension = UpperText(given.extension().string());
        if ((extension == ".SH") || (extension == ".SHM") || (extension == ".SHP"))
        {
            why = "a header file cannot be compiled";
            return false;
        }
        if (extension != ".SC")
        {
            why = "a path must name a .sc file";
            return false;
        }
        std::error_code ec;
        if (!fs::is_regular_file(given, ec))
        {
            why = "the file does not exist";
            return false;
        }
        if (!fs::equivalent(given.parent_path(), fs::path(helper.GetSrcFolder()), ec))
        {
            why = fmt::format("the file is not in {0}", helper.GetSrcFolder());
            return false;
        }
        std::string title = given.stem().string();
        if (!selection.NumberOfName(title, number) && !ReadDeclaredScriptNumber(helper, given.string(), number))
        {
            why = "the file declares no script number that can be read";
            return false;
        }
        path = given.string();
        return true;
    }

    ScriptSelection MakeSelection(Selection &selection, const GameFolderHelper &helper, const std::set<uint16_t> &numbers, const std::map<uint16_t, std::string> &givenPaths, SelectorMode mode)
    {
        ScriptSelection result;
        for (uint16_t number : InModeOrder(numbers, selection.Names(), mode))
        {
            auto given = givenPaths.find(number);
            ScriptId scriptId((given != givenPaths.end()) ? given->second : helper.GetScriptFileName(selection.NameOf(number)));
            scriptId.SetResourceNumber(number);
            result.scripts.push_back(scriptId);
        }
        return result;
    }
}

std::map<uint16_t, std::string> DeriveScriptNames(GameSession &session, bool all, std::map<uint16_t, std::string> *errors)
{
    const ScriptNameMap *names = session.Helper().ScriptNames.get();
    std::vector<ScriptObjectsForNaming> toName;
    for (auto &compiled : ReadCompiledScripts(session, true))
    {
        if (!compiled.second.loaded)
        {
            if (errors)
            {
                (*errors)[compiled.first] = compiled.second.error;
            }
            continue;
        }
        if (all || !HasFileName(names, compiled.first))
        {
            toName.push_back(std::move(compiled.second.objects));
        }
    }
    std::vector<std::string> used;
    if (!all && names)
    {
        for (const auto &entry : names->Entries())
        {
            if (HasFileName(names, entry.first))
            {
                used.push_back(entry.second.name);
            }
        }
    }
    return SuggestScriptNames(std::move(toName), used);
}

sci::Status AddDerivedScriptNames(GameSession &session)
{
    return sci::Guard("deriving the script names", [&]() -> sci::Status
    {
        std::shared_ptr<const ScriptNameMap> current = session.Helper().ScriptNames;
        if (!current)
        {
            return sci::Fail(sci::ErrorCode::Internal, "the session has no script names");
        }
        ScriptNameMap names = *current;
        names.AddDerivedNames(DeriveScriptNames(session, false));
        session.ResourceMap().SetScriptNames(std::make_shared<const ScriptNameMap>(std::move(names)));
        return sci::Ok();
    });
}

sci::Result<std::vector<ScriptRow>> ListScripts(GameSession &session, bool alwaysDerive)
{
    return sci::Guard("listing the scripts", [&]() -> sci::Result<std::vector<ScriptRow>>
    {
        const GameFolderHelper &helper = session.Helper();
        std::shared_ptr<const ScriptNameMap> names = helper.ScriptNames;
        if (!names)
        {
            return sci::Fail(sci::ErrorCode::Internal, "the session has no script names");
        }
        std::map<uint16_t, CompiledInfo> compiled = ReadCompiledScripts(session, false);
        bool someUnnamed = false;
        for (const auto &script : compiled)
        {
            someUnnamed = someUnnamed || !HasFileName(names.get(), script.first);
        }
        std::map<uint16_t, std::string> derived;
        std::map<uint16_t, std::string> derivedAll;
        std::map<uint16_t, std::string> errors;
        if (someUnnamed || alwaysDerive)
        {
            derived = DeriveScriptNames(session, false, &errors);
        }
        if (alwaysDerive)
        {
            derivedAll = DeriveScriptNames(session, true);
        }

        std::set<uint16_t> numbers;
        for (const auto &script : compiled)
        {
            numbers.insert(script.first);
        }
        for (const auto &entry : names->Entries())
        {
            numbers.insert(entry.first);
        }
        std::vector<ScriptRow> rows;
        for (uint16_t number : numbers)
        {
            ScriptRow row;
            row.number = number;
            auto name = derived.find(number);
            if (!HasFileName(names.get(), number) && (name != derived.end()))
            {
                row.name = name->second;
                row.source = NameSource::Derived;
            }
            else
            {
                row.name = names->NameOf(number);
                row.source = names->SourceOf(number);
            }
            auto all = derivedAll.find(number);
            if (all != derivedAll.end())
            {
                row.derivedName = all->second;
            }
            auto script = compiled.find(number);
            if (script != compiled.end())
            {
                row.location = script->second.location;
            }
            row.hasSource = FileExists(helper.GetScriptFileName(row.name));
            row.hasObjectFile = FileExists(helper.GetScriptObjectFileName(row.name));
            auto error = errors.find(number);
            if (error != errors.end())
            {
                row.error = error->second;
            }
            rows.push_back(std::move(row));
        }
        return rows;
    });
}

sci::Result<ScriptSelection> ResolveScriptSelectors(GameSession &session, const std::vector<std::string> &selectors, SelectorMode mode)
{
    return sci::Guard("resolving the script selectors", [&]() -> sci::Result<ScriptSelection>
    {
        const GameFolderHelper &helper = session.Helper();
        if (!helper.ScriptNames)
        {
            return sci::Fail(sci::ErrorCode::Internal, "the session has no script names");
        }
        SCI_TRY(CheckNoConflict(*helper.ScriptNames, mode));
        Selection selection(session, mode);
        std::set<uint16_t> chosen;
        std::map<uint16_t, std::string> givenPaths;
        std::vector<std::string> bad;
        for (const std::string &selector : selectors)
        {
            std::string why;
            uint16_t first;
            uint16_t last;
            if (ParseScriptNumber(selector, first))
            {
                if (selection.Takes(first, why))
                {
                    chosen.insert(first);
                }
                else
                {
                    bad.push_back(selector + ": " + why);
                }
            }
            else if (ParseRange(selector, first, last))
            {
                int count = 0;
                for (uint16_t number : selection.Known())
                {
                    std::string skipped;
                    if ((number >= first) && (number <= last) && selection.Takes(number, skipped))
                    {
                        chosen.insert(number);
                        count++;
                    }
                }
                if (count == 0)
                {
                    bad.push_back(selector + ": no script in this range");
                }
            }
            else if (LooksLikePath(selector))
            {
                uint16_t number;
                std::string path;
                if (mode != SelectorMode::Compile)
                {
                    bad.push_back(selector + ": only compile takes a path");
                }
                else if (!ResolvePath(selection, helper, selector, number, path, why))
                {
                    bad.push_back(selector + ": " + why);
                }
                else
                {
                    chosen.insert(number);
                    givenPaths[number] = path;
                }
            }
            else
            {
                uint16_t number;
                if (!selection.NumberOfName(selector, number))
                {
                    bad.push_back(selector + ": no script has this name");
                }
                else if (selection.Takes(number, why))
                {
                    chosen.insert(number);
                }
                else
                {
                    bad.push_back(selector + ": " + why);
                }
            }
        }
        if (!bad.empty())
        {
            return sci::Fail(sci::ErrorCode::Usage, "These scripts cannot be used (run \"scic script list\" to see the scripts):\n  " + JoinText(bad, "\n  "));
        }
        return MakeSelection(selection, helper, chosen, givenPaths, mode);
    });
}

sci::Result<ScriptSelection> SelectAllScripts(GameSession &session, SelectorMode mode)
{
    return sci::Guard("selecting all scripts", [&]() -> sci::Result<ScriptSelection>
    {
        const GameFolderHelper &helper = session.Helper();
        if (!helper.ScriptNames)
        {
            return sci::Fail(sci::ErrorCode::Internal, "the session has no script names");
        }
        SCI_TRY(CheckNoConflict(*helper.ScriptNames, mode));
        Selection selection(session, mode);
        std::set<uint16_t> chosen;
        std::vector<std::string> warnings;
        for (uint16_t number : selection.Known())
        {
            std::string why;
            if (selection.Takes(number, why))
            {
                chosen.insert(number);
            }
            else if ((mode == SelectorMode::Compile) && HasFileName(helper.ScriptNames.get(), number))
            {
                warnings.push_back(why);
            }
        }
        ScriptSelection result = MakeSelection(selection, helper, chosen, std::map<uint16_t, std::string>(), mode);
        result.warnings = warnings;
        return result;
    });
}

sci::Result<std::vector<std::string>> FindShadowingPatches(const GameFolderHelper &helper, const std::vector<ResourceKey> &resources)
{
    return sci::Guard("finding the patch files", [&]() -> sci::Result<std::vector<std::string>>
    {
        std::map<ResourceType, std::set<uint16_t>> wanted;
        for (const ResourceKey &key : resources)
        {
            wanted[key.type].insert(key.number);
        }
        std::vector<std::string> files;
        for (const auto &type : wanted)
        {
            for (const auto &patch : PatchFilesOf(helper, type.first))
            {
                if (type.second.find(patch.first) != type.second.end())
                {
                    files.insert(files.end(), patch.second.begin(), patch.second.end());
                }
            }
        }
        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        return files;
    });
}
