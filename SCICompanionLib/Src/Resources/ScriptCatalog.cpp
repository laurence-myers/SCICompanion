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

    bool AllDigits(const std::string &text)
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](char ch) { return (ch >= '0') && (ch <= '9'); });
    }

    // Why a selector that has the form of a number or a range is not one;
    // "" when it does not have that form. So "200-100" and "65536" get this
    // reason, not "no script has this name".
    std::string WhyNotANumber(const std::string &text)
    {
        size_t dash = text.find('-');
        bool isRange = (dash != std::string::npos) && AllDigits(text.substr(0, dash)) && AllDigits(text.substr(dash + 1));
        if (!AllDigits(text) && !isRange)
        {
            return std::string();
        }
        uint16_t first;
        uint16_t last;
        if (!isRange || !ParseScriptNumber(text.substr(0, dash), first) || !ParseScriptNumber(text.substr(dash + 1), last))
        {
            return "a script number is 0 to 65535";
        }
        return "the first number of a range must not be larger than the second";
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

    // One file, also when the two names differ in case.
    bool SamePath(const std::string &a, const std::string &b)
    {
        std::error_code ec;
        return (_stricmp(a.c_str(), b.c_str()) == 0) || fs::equivalent(a, b, ec);
    }

    using PatchKey = std::pair<ResourceType, uint16_t>;

    // The patch files of the game folder, by type and number, as the patch
    // file source finds them when it reads these types together
    // (PatchFilesResourceSource::ReadNextEntry): the file name matches a name
    // pattern of one of the types and gives a number, the file has 2 bytes or
    // more, and its first byte gives its type. So 105.hep with a script's
    // type byte is script 105 when scripts and heaps are read together, and a
    // 1-byte 101.scr is no resource.
    std::map<PatchKey, std::vector<std::string>> PatchFilesOf(const GameFolderHelper &helper, const std::set<ResourceType> &types)
    {
        std::map<PatchKey, std::vector<std::string>> files;
        std::string spec;
        for (ResourceType type : types)
        {
            if (((int)type >= 0) && ((int)type < (int)ResourceType::Max))
            {
                spec += (spec.empty() ? "" : ";") + std::string(g_szResourceSpecByType[(int)type]);
            }
        }
        if (helper.GameFolder.empty() || spec.empty())
        {
            return files;
        }
        WIN32_FIND_DATAA findData;
        HANDLE find = FindFirstFileA((helper.GameFolder + "\\*.*").c_str(), &findData);
        if (find == INVALID_HANDLE_VALUE)
        {
            return files;
        }
        do
        {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !PathMatchSpecA(findData.cFileName, spec.c_str()))
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
            char header[2] = {};
            if (file.read(header, sizeof(header)))
            {
                ResourceType type = (ResourceType)(((unsigned char)header[0]) & 0x7f);
                if (types.find(type) != types.end())
                {
                    files[PatchKey(type, (uint16_t)number)].push_back(path);
                }
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

        // The types that the container read, as the patch file source reads
        // them together.
        std::set<ResourceType> patchTypes = { ResourceType::Script };
        if (types != ResourceTypeFlags::Script)
        {
            patchTypes.insert(ResourceType::Heap);
        }
        std::map<PatchKey, std::vector<std::string>> patchFiles = PatchFilesOf(helper, patchTypes);
        std::map<uint16_t, CompiledInfo> scripts;
        for (auto &script : scriptBlobs)
        {
            CompiledInfo &info = scripts[script.first];
            const ResourceBlob &blob = *script.second;
            if (blob.GetSourceFlags() == ResourceSourceFlags::PatchFile)
            {
                auto patch = patchFiles.find(PatchKey(ResourceType::Script, script.first));
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

    // The name conflicts of the chosen scripts, for a mode that writes (the
    // list shows every script), each with its own fix. --all leaves such a
    // script out, with a warning.
    std::vector<std::string> ConflictsOfChosen(const ScriptNameMap &names, SelectorMode mode, const std::set<uint16_t> &numbers)
    {
        std::vector<std::string> texts;
        if (mode == SelectorMode::List)
        {
            return texts;
        }
        std::set<const NameConflict *> seen;
        for (uint16_t number : numbers)
        {
            for (const NameConflict *conflict : names.ConflictsOf(number))
            {
                if (seen.insert(conflict).second)
                {
                    texts.push_back(conflict->text);
                }
            }
        }
        return texts;
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

        // Every script that the list shows: compiled, with a name, or in a
        // conflict. A conflict keeps its script out of the names, so --all and
        // a range see it only through this.
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
            for (const NameConflict &conflict : _names->Conflicts())
            {
                numbers.insert(conflict.numbers.begin(), conflict.numbers.end());
            }
            return numbers;
        }

        // src\<name>.sc exists.
        bool HasSource(uint16_t number)
        {
            return FileExists(_helper.GetScriptFileName(NameOf(number)));
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
            if (_names->NumberOf(name, found) || _names->ConflictNumberOf(name, found))
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
                sci::Result<std::map<uint16_t, std::string>> derived = DeriveScriptNames(_session, false);
                if (!derived)
                {
                    // The callers run inside an exception boundary.
                    throw sci::DataError(derived.error());
                }
                _derived = std::move(*derived);
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
        // src\..\src\X.sc and src/X.sc name the same file as src\X.sc.
        given = given.lexically_normal();
        given.make_preferred();
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

namespace
{
    // The derived names of a reset. The scripts in chosen get their derived
    // names; every other script keeps its name, and no chosen script takes
    // it. The title of a file in src belongs to the script that has that
    // name. A chosen script can keep the title of its own file, but no script
    // takes the file of another script. For example, a reset of script 979 of
    // the SCI0 template must not give it "MenuBar": the run would write over
    // menubar.sc, the source of script 997. A file that no script has keeps
    // its title from every script. The current name of a
    // chosen script belongs to it too, so that no other chosen script takes
    // it: two scripts would then have one name. A chosen script that gets no
    // derived name keeps its name (it cannot be read, or it has no class and
    // no public instance). A script that no group of the run gives an ok
    // outcome keeps its name in game.ini (RunDecompile). So a reset is not
    // always the same twice: when the current name of one chosen script is
    // the derived name of another, the other gets the "_N" suffix, and a
    // second reset can give it the plain name.
    std::map<uint16_t, std::string> ResetNamesOf(const ScriptNameMap *names, std::vector<ScriptObjectsForNaming> toName, const std::set<uint16_t> &chosen)
    {
        std::vector<std::string> reserved;
        std::map<std::string, uint16_t> owned;
        if (names)
        {
            for (const auto &entry : names->Entries())
            {
                if (chosen.find(entry.first) == chosen.end())
                {
                    reserved.push_back(entry.second.name);
                }
            }
            for (uint16_t number : chosen)
            {
                owned[names->NameOf(number)] = number;
            }
            for (const std::string &title : names->FileTitles())
            {
                // The title of the own file of a chosen script is its current
                // name. In a name conflict, the script that NumberOf finds (the
                // lowest number) owns the title: this entry replaces the one
                // of the loop above, which gives it to the highest number.
                uint16_t owner;
                if (names->NumberOf(title, owner) && (chosen.find(owner) != chosen.end()))
                {
                    owned[title] = owner;
                }
                else
                {
                    reserved.push_back(title);
                }
            }
        }
        return SuggestScriptNames(std::move(toName), reserved, owned);
    }

    // The compiled scripts that can be read, for the naming rule. The error
    // of each script that cannot be read goes to errors.
    std::map<uint16_t, ScriptObjectsForNaming> ReadForNaming(GameSession &session, std::map<uint16_t, std::string> *errors)
    {
        std::map<uint16_t, ScriptObjectsForNaming> scripts;
        for (auto &compiled : ReadCompiledScripts(session, true))
        {
            if (compiled.second.loaded)
            {
                scripts[compiled.first] = std::move(compiled.second.objects);
            }
            else if (errors)
            {
                (*errors)[compiled.first] = compiled.second.error;
            }
        }
        return scripts;
    }
}

sci::Result<std::map<uint16_t, std::string>> DeriveScriptNames(GameSession &session, bool all, std::map<uint16_t, std::string> *errors)
{
    // No exception leaves a service (plan section 6.2).
    return sci::Guard("deriving the script names", [&]() -> sci::Result<std::map<uint16_t, std::string>>
    {
        const ScriptNameMap *names = session.Helper().ScriptNames.get();
        std::vector<ScriptObjectsForNaming> toName;
        std::set<uint16_t> chosen;
        for (auto &compiled : ReadForNaming(session, errors))
        {
            if (all || !HasFileName(names, compiled.first))
            {
                chosen.insert(compiled.first);
                toName.push_back(std::move(compiled.second));
            }
        }
        if (all)
        {
            // The names of a reset of every script.
            return ResetNamesOf(names, std::move(toName), chosen);
        }
        std::vector<std::string> used;
        if (names)
        {
            for (const auto &entry : names->Entries())
            {
                if (HasFileName(names, entry.first))
                {
                    used.push_back(entry.second.name);
                }
            }
            // Also the file of a script in a conflict, and any other file in src:
            // a decompile must not write over it.
            used.insert(used.end(), names->FileTitles().begin(), names->FileTitles().end());
        }
        return SuggestScriptNames(std::move(toName), used);
    });
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
        SCI_TRY_ASSIGN(auto derived, DeriveScriptNames(session, false));
        names.AddDerivedNames(derived);
        session.ResourceMap().SetScriptNames(std::make_shared<const ScriptNameMap>(std::move(names)));
        return sci::Ok();
    });
}

sci::Result<std::vector<std::string>> ResetScriptNames(GameSession &session, const std::set<uint16_t> &numbers, bool dryRun)
{
    return sci::Guard("resetting the script names", [&]() -> sci::Result<std::vector<std::string>>
    {
        const GameFolderHelper &helper = session.Helper();
        std::shared_ptr<const ScriptNameMap> current = helper.ScriptNames;
        if (!current)
        {
            return sci::Fail(sci::ErrorCode::Internal, "the session has no script names");
        }
        // Only the chosen scripts: game.ini gets only the names of the
        // scripts that the run writes, so a new name for another script
        // would be in memory only, and a later run would use the old name.
        std::vector<ScriptObjectsForNaming> toName;
        for (auto &compiled : ReadForNaming(session, nullptr))
        {
            if (numbers.find(compiled.first) != numbers.end())
            {
                toName.push_back(std::move(compiled.second));
            }
        }
        std::map<uint16_t, std::string> derived = ResetNamesOf(current.get(), std::move(toName), numbers);
        std::vector<std::string> warnings;
        for (const auto &name : derived)
        {
            std::string old = current->NameOf(name.first);
            if (_stricmp(old.c_str(), name.second.c_str()) != 0)
            {
                for (const std::string &file : { helper.GetScriptFileName(old), helper.GetScriptObjectFileName(old) })
                {
                    std::error_code ec;
                    if (fs::exists(file, ec))
                    {
                        warnings.push_back(fmt::format(dryRun ? "{0} would keep its old name: script {1} would be {2}" : "{0} keeps its old name: script {1} is now {2}",
                            file, name.first, name.second));
                    }
                }
            }
        }
        ScriptNameMap names = *current;
        names.ReplaceNames(derived);
        session.ResourceMap().SetScriptNames(std::make_shared<const ScriptNameMap>(std::move(names)));
        return warnings;
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
            SCI_TRY_ASSIGN(derived, DeriveScriptNames(session, false, &errors));
        }
        if (alwaysDerive)
        {
            SCI_TRY_ASSIGN(derivedAll, DeriveScriptNames(session, true));
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
        for (const NameConflict &conflict : names->Conflicts())
        {
            numbers.insert(conflict.numbers.begin(), conflict.numbers.end());
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
        Selection selection(session, mode);
        std::set<uint16_t> chosen;
        std::map<uint16_t, std::string> givenPaths;
        // The scripts that a number, a range or a name chose.
        std::set<uint16_t> byNumberOrName;
        std::vector<std::string> bad;
        // In a mode that writes, a script in a name conflict gives the
        // conflict and its fix, before any other reason, and the one Usage
        // error then refuses the whole selection. For this check, a range has
        // each script in it, also one that the mode does not take. A conflict
        // of another script refuses nothing.
        std::set<const NameConflict *> reported;
        auto inConflict = [&](uint16_t number) -> bool
        {
            if (mode == SelectorMode::List)
            {
                return false;
            }
            std::vector<const NameConflict *> conflicts = helper.ScriptNames->ConflictsOf(number);
            for (const NameConflict *conflict : conflicts)
            {
                if (reported.insert(conflict).second)
                {
                    bad.push_back(conflict->text);
                }
            }
            return !conflicts.empty();
        };
        for (const std::string &selector : selectors)
        {
            std::string why;
            uint16_t first;
            uint16_t last;
            uint16_t named;
            if (ParseScriptNumber(selector, first))
            {
                if (inConflict(first))
                {
                }
                else if (selection.Takes(first, why))
                {
                    chosen.insert(first);
                    byNumberOrName.insert(first);
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
                    if ((number < first) || (number > last))
                    {
                        continue;
                    }
                    if (inConflict(number))
                    {
                        count++;
                    }
                    else if (selection.Takes(number, skipped))
                    {
                        chosen.insert(number);
                        byNumberOrName.insert(number);
                        count++;
                    }
                }
                if (count == 0)
                {
                    bad.push_back(selector + ": no script in this range");
                }
            }
            else if (!(why = WhyNotANumber(selector)).empty())
            {
                bad.push_back(selector + ": " + why);
            }
            // A script name can have a '.' (game.ini: n993=gamefile.sh), so a
            // name wins over a path.
            else if (LooksLikePath(selector) && !selection.NumberOfName(selector, named))
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
                else if (!inConflict(number))
                {
                    auto given = givenPaths.find(number);
                    if ((given != givenPaths.end()) && !SamePath(given->second, path))
                    {
                        bad.push_back(fmt::format("{0}: script {1} is also {2}", selector, number, given->second));
                    }
                    else
                    {
                        chosen.insert(number);
                        givenPaths[number] = path;
                    }
                }
            }
            else
            {
                uint16_t number;
                if (!selection.NumberOfName(selector, number))
                {
                    bad.push_back(selector + ": no script has this name");
                }
                else if (inConflict(number))
                {
                }
                else if (selection.Takes(number, why))
                {
                    chosen.insert(number);
                    byNumberOrName.insert(number);
                }
                else
                {
                    bad.push_back(selector + ": " + why);
                }
            }
        }
        // A number, a range or a name, and a path, for one script: an error
        // when the path is not the script's own file, so that the path does
        // not take the place of that file with no message.
        for (const auto &given : givenPaths)
        {
            if (byNumberOrName.find(given.first) != byNumberOrName.end())
            {
                std::string own = helper.GetScriptFileName(selection.NameOf(given.first));
                if (!SamePath(own, given.second))
                {
                    bad.push_back(fmt::format("{0}: script {1} is also {2}", given.second, given.first, own));
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
        Selection selection(session, mode);
        std::set<uint16_t> chosen;
        std::vector<std::string> warnings;
        for (uint16_t number : selection.Known())
        {
            std::string why;
            std::vector<std::string> conflicts = ConflictsOfChosen(*helper.ScriptNames, mode, { number });
            if (!conflicts.empty())
            {
                warnings.push_back(fmt::format("script {0} is left out: {1}", number, conflicts[0]));
            }
            else if (selection.Takes(number, why))
            {
                chosen.insert(number);
            }
            else if ((mode == SelectorMode::Sco) && (selection.HasSource(number) || HasFileName(helper.ScriptNames.get(), number)))
            {
                // Plan section 4.6: a source with no compiled script, or a
                // name with no source, is skipped and listed, not left out
                // with no message; the .sco run gives the reason.
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
        std::set<ResourceType> types;
        std::set<PatchKey> wanted;
        for (const ResourceKey &key : resources)
        {
            types.insert(key.type);
            wanted.insert(PatchKey(key.type, key.number));
        }
        std::vector<std::string> files;
        for (const auto &patch : PatchFilesOf(helper, types))
        {
            if (wanted.find(patch.first) != wanted.end())
            {
                files.insert(files.end(), patch.second.begin(), patch.second.end());
            }
        }
        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        return files;
    });
}
