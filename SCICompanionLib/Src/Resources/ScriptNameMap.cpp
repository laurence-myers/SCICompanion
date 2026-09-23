#include "stdafx.h"
#include "ScriptNameMap.h"
#include "GameFolderHelper.h"
#include "format.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace
{
    namespace fs = std::filesystem;

    std::string Upper(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](char ch) { return (char)std::toupper((unsigned char)ch); });
        return text;
    }

    std::string Trim(const std::string &text)
    {
        size_t start = text.find_first_not_of(" \t");
        if (start == std::string::npos)
        {
            return std::string();
        }
        size_t end = text.find_last_not_of(" \t");
        return text.substr(start, end - start + 1);
    }

    std::string DefaultName(uint16_t number)
    {
        return fmt::format("n{0:0>3}", number);
    }

    // A decimal number, or a hex number after a $ ("$1F"), up to 65535.
    bool ParseNumber(const std::string &text, uint16_t &value)
    {
        bool hex = !text.empty() && (text[0] == '$');
        std::string digits = hex ? text.substr(1) : text;
        if (digits.empty() || (digits.size() > (hex ? 4u : 5u)))
        {
            return false;
        }
        unsigned long number = 0;
        for (char ch : digits)
        {
            int digit;
            if ((ch >= '0') && (ch <= '9'))
            {
                digit = ch - '0';
            }
            else if (hex && (ch >= 'a') && (ch <= 'f'))
            {
                digit = ch - 'a' + 10;
            }
            else if (hex && (ch >= 'A') && (ch <= 'F'))
            {
                digit = ch - 'A' + 10;
            }
            else
            {
                return false;
            }
            number = number * (hex ? 16 : 10) + digit;
        }
        if (number > 0xffff)
        {
            return false;
        }
        value = (uint16_t)number;
        return true;
    }

    bool ReadFileText(const fs::path &path, std::string &text)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        text = buffer.str();
        return true;
    }

    // The script text with each ; comment and each string ("..." or {...})
    // made into spaces, so that a scan for a form does not find one there.
    std::string CodeOnly(const std::string &text)
    {
        std::string code = text;
        size_t i = 0;
        while (i < code.size())
        {
            char ch = code[i];
            if (ch == ';')
            {
                while ((i < code.size()) && (code[i] != '\n'))
                {
                    code[i++] = ' ';
                }
            }
            else if ((ch == '"') || (ch == '{'))
            {
                char end = (ch == '"') ? '"' : '}';
                code[i++] = ' ';
                while ((i < code.size()) && (code[i] != end))
                {
                    if ((end == '"') && (code[i] == '\\') && ((i + 1) < code.size()))
                    {
                        code[i++] = ' ';
                    }
                    if (code[i] != '\n')
                    {
                        code[i] = ' ';
                    }
                    i++;
                }
                if (i < code.size())
                {
                    code[i++] = ' ';
                }
            }
            else
            {
                i++;
            }
        }
        return code;
    }

    bool IsSpace(char ch)
    {
        return std::isspace((unsigned char)ch) != 0;
    }

    // The tokens of a form "(keyword token token ...)" that starts at pos
    // (code[pos] is '('). False when the form has another keyword, or holds
    // a nested form.
    bool ReadForm(const std::string &code, size_t pos, const char *keyword, std::vector<std::string> &tokens)
    {
        size_t i = pos + 1;
        while ((i < code.size()) && IsSpace(code[i]))
        {
            i++;
        }
        size_t length = std::strlen(keyword);
        if (code.compare(i, length, keyword) != 0)
        {
            return false;
        }
        i += length;
        if ((i >= code.size()) || !IsSpace(code[i]))
        {
            return false;
        }
        tokens.clear();
        while (i < code.size())
        {
            while ((i < code.size()) && IsSpace(code[i]))
            {
                i++;
            }
            if ((i >= code.size()) || (code[i] == '('))
            {
                return false;
            }
            if (code[i] == ')')
            {
                return true;
            }
            size_t start = i;
            while ((i < code.size()) && !IsSpace(code[i]) && (code[i] != '(') && (code[i] != ')'))
            {
                i++;
            }
            tokens.push_back(code.substr(start, i - start));
        }
        return false;
    }

    // Each (define NAME VALUE) of the code whose value is a number.
    void ReadNumberDefines(const std::string &code, std::map<std::string, uint16_t> &defines)
    {
        std::vector<std::string> tokens;
        for (size_t pos = code.find('('); pos != std::string::npos; pos = code.find('(', pos + 1))
        {
            uint16_t value;
            if (ReadForm(code, pos, "define", tokens) && (tokens.size() == 2) && ParseNumber(tokens[1], value))
            {
                defines[tokens[0]] = value;
            }
        }
    }

    // The number of the (script# X) form of a script. X is a number, or a
    // define of the script itself or of src\*.sh.
    bool ReadScriptNumber(const std::string &text, const std::map<std::string, uint16_t> &defines, uint16_t &number)
    {
        std::string code = CodeOnly(text);
        std::vector<std::string> tokens;
        for (size_t pos = code.find('('); pos != std::string::npos; pos = code.find('(', pos + 1))
        {
            if (ReadForm(code, pos, "script#", tokens) && (tokens.size() == 1))
            {
                if (ParseNumber(tokens[0], number))
                {
                    return true;
                }
                std::map<std::string, uint16_t> ownDefines;
                ReadNumberDefines(code, ownDefines);
                auto own = ownDefines.find(tokens[0]);
                if (own != ownDefines.end())
                {
                    number = own->second;
                    return true;
                }
                auto shared = defines.find(tokens[0]);
                if (shared != defines.end())
                {
                    number = shared->second;
                    return true;
                }
                return false;
            }
        }
        return false;
    }

    // The script number in a .sco header: "SCO", 5 bytes of version and
    // alignment, then the number (CSCOFile::Save).
    bool ReadScoScriptNumber(const fs::path &path, uint16_t &number)
    {
        std::ifstream file(path, std::ios::binary);
        unsigned char header[10];
        if (!file.read(reinterpret_cast<char *>(header), sizeof(header)) || (std::memcmp(header, "SCO", 3) != 0))
        {
            return false;
        }
        number = (uint16_t)(header[8] | (header[9] << 8));
        return true;
    }

    // The nNNN=Name lines of the [Script] section of game.ini, in the order
    // of the file. The buffer grows until the whole section fits. The first
    // line for a number wins, as GetPrivateProfileString gives it.
    std::vector<std::pair<uint16_t, std::string>> ReadGameIniScripts(const std::string &iniFile)
    {
        std::vector<std::pair<uint16_t, std::string>> scripts;
        std::vector<char> buffer;
        DWORD size = 32768;
        for (;;)
        {
            buffer.assign(size, 0);
            DWORD length = GetPrivateProfileSectionA("Script", buffer.data(), size, iniFile.c_str());
            // A section that does not fit gives size - 2.
            if ((length < (size - 2)) || (size >= (1u << 26)))
            {
                break;
            }
            size *= 2;
        }
        std::set<uint16_t> seen;
        for (const char *line = buffer.data(); *line; line += std::strlen(line) + 1)
        {
            std::string text = line;
            size_t equals = text.find('=');
            if (equals == std::string::npos)
            {
                continue;
            }
            std::string key = Trim(text.substr(0, equals));
            std::string value = Trim(text.substr(equals + 1));
            if ((value.size() >= 2) && (value.front() == '"') && (value.back() == '"'))
            {
                value = value.substr(1, value.size() - 2);
            }
            uint16_t number;
            if ((key.size() < 2) || ((key[0] != 'n') && (key[0] != 'N')) || (key[1] == '$') || !ParseNumber(key.substr(1), number) || value.empty())
            {
                continue;
            }
            if (seen.insert(number).second)
            {
                scripts.emplace_back(number, value);
            }
        }
        return scripts;
    }

    std::string Join(const std::vector<std::string> &items, const std::string &separator)
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

    // A name for a file and for (use Name): letters, digits, '_' and '-'.
    std::string CleanName(const std::string &name)
    {
        std::string clean;
        if (!name.empty() && std::isdigit((unsigned char)name[0]))
        {
            clean = "_";
        }
        for (char ch : name)
        {
            clean.push_back((std::isalnum((unsigned char)ch) || (ch == '_') || (ch == '-')) ? ch : '_');
        }
        return clean;
    }
}

const char *NameSourceText(NameSource source)
{
    switch (source)
    {
    case NameSource::GameIni:
        return "game.ini";
    case NameSource::Source:
        return "source";
    case NameSource::Sco:
        return "sco";
    case NameSource::Derived:
        return "derived";
    default:
        return "default";
    }
}

sci::Result<ScriptNameMap> ScriptNameMap::Build(const GameFolderHelper &helper)
{
    ScriptNameMap map;
    if (helper.GameFolder.empty())
    {
        return map;
    }

    // Rule 1: game.ini [Script].
    for (const auto &script : ReadGameIniScripts(helper.GetGameIniFileName()))
    {
        map._entries[script.first] = { script.second, NameSource::GameIni };
        map._gameIniOrder.push_back(script.first);
    }

    // The files of src\. The order of the file names makes the messages stable.
    std::map<uint16_t, std::vector<std::string>> sourceTitles;
    std::map<uint16_t, std::vector<std::string>> scoTitles;
    std::error_code ec;
    fs::path srcFolder = helper.GetSrcFolder();
    if (!srcFolder.empty() && fs::is_directory(srcFolder, ec))
    {
        std::vector<fs::path> headers;
        std::vector<fs::path> sources;
        std::vector<fs::path> objectFiles;
        for (fs::directory_iterator it(srcFolder, ec), end; !ec && (it != end); it.increment(ec))
        {
            if (!it->is_regular_file(ec))
            {
                continue;
            }
            std::string extension = Upper(it->path().extension().string());
            if (extension == ".SH")
            {
                headers.push_back(it->path());
            }
            else if (extension == ".SC")
            {
                sources.push_back(it->path());
            }
            else if (extension == ".SCO")
            {
                objectFiles.push_back(it->path());
            }
        }
        std::sort(headers.begin(), headers.end());
        std::sort(sources.begin(), sources.end());
        std::sort(objectFiles.begin(), objectFiles.end());

        // A script can declare its number with a define of a header, as the
        // template games do: (script# DOOR_SCRIPT) with DOOR_SCRIPT in game.sh.
        std::map<std::string, uint16_t> defines;
        for (const fs::path &header : headers)
        {
            std::string text;
            if (ReadFileText(header, text))
            {
                ReadNumberDefines(CodeOnly(text), defines);
            }
        }
        for (const fs::path &source : sources)
        {
            std::string text;
            uint16_t number;
            if (ReadFileText(source, text) && ReadScriptNumber(text, defines, number))
            {
                sourceTitles[number].push_back(source.stem().string());
            }
        }
        for (const fs::path &objectFile : objectFiles)
        {
            uint16_t number;
            if (ReadScoScriptNumber(objectFile, number))
            {
                scoTitles[number].push_back(objectFile.stem().string());
            }
        }
    }

    // Rule 2, then rule 3. Two files that would give one script its name
    // make a conflict, and neither file gives the name.
    std::set<uint16_t> conflicted;
    for (const auto &source : sourceTitles)
    {
        if (map._entries.find(source.first) != map._entries.end())
        {
            continue;
        }
        if (source.second.size() > 1)
        {
            map._conflicts.push_back(fmt::format("script {0} is declared by more than one file in src: {1}.sc",
                source.first, Join(source.second, ".sc, ")));
            conflicted.insert(source.first);
            continue;
        }
        map._entries[source.first] = { source.second[0], NameSource::Source };
    }
    for (const auto &objectFile : scoTitles)
    {
        if ((map._entries.find(objectFile.first) != map._entries.end()) || (conflicted.find(objectFile.first) != conflicted.end()))
        {
            continue;
        }
        if (objectFile.second.size() > 1)
        {
            map._conflicts.push_back(fmt::format("script {0} has more than one object file in src: {1}.sco",
                objectFile.first, Join(objectFile.second, ".sco, ")));
            continue;
        }
        map._entries[objectFile.first] = { objectFile.second[0], NameSource::Sco };
    }

    // One name for two scripts: their files would be the same file.
    std::map<std::string, std::vector<uint16_t>> numbersOfName;
    for (const auto &entry : map._entries)
    {
        numbersOfName[Upper(entry.second.name)].push_back(entry.first);
    }
    for (const auto &name : numbersOfName)
    {
        if (name.second.size() > 1)
        {
            std::vector<std::string> numbers;
            for (uint16_t number : name.second)
            {
                numbers.push_back(fmt::format("{0} ({1})", number, NameSourceText(map._entries[number].source)));
            }
            map._conflicts.push_back(fmt::format("the name {0} is the name of scripts {1}",
                map._entries[name.second[0]].name, Join(numbers, ", ")));
        }
    }
    return map;
}

void ScriptNameMap::AddDerivedNames(const std::map<uint16_t, std::string> &names)
{
    for (const auto &name : names)
    {
        if (!name.second.empty() && (_entries.find(name.first) == _entries.end()))
        {
            _entries[name.first] = { name.second, NameSource::Derived };
        }
    }
}

std::string ScriptNameMap::NameOf(uint16_t number) const
{
    auto entry = _entries.find(number);
    return (entry != _entries.end()) ? entry->second.name : DefaultName(number);
}

NameSource ScriptNameMap::SourceOf(uint16_t number) const
{
    auto entry = _entries.find(number);
    return (entry != _entries.end()) ? entry->second.source : NameSource::Default;
}

bool ScriptNameMap::NumberOf(const std::string &name, uint16_t &number) const
{
    std::string upper = Upper(name);
    for (const auto &entry : _entries)
    {
        if (Upper(entry.second.name) == upper)
        {
            number = entry.first;
            return true;
        }
    }
    // Rule 5: nNNN names a script that no other rule names.
    uint16_t defaultNumber;
    if ((upper.size() >= 2) && (upper[0] == 'N') && (upper[1] != '$') && ParseNumber(upper.substr(1), defaultNumber) &&
        (_entries.find(defaultNumber) == _entries.end()) && (Upper(DefaultName(defaultNumber)) == upper))
    {
        number = defaultNumber;
        return true;
    }
    return false;
}

std::map<uint16_t, std::string> SuggestScriptNames(std::vector<ScriptObjectsForNaming> scripts, const std::vector<std::string> &reservedNames)
{
    std::sort(scripts.begin(), scripts.end(), [](const ScriptObjectsForNaming &a, const ScriptObjectsForNaming &b) { return a.number < b.number; });
    std::set<std::string> used;
    for (const std::string &name : reservedNames)
    {
        used.insert(Upper(name));
    }
    std::map<uint16_t, std::string> names;
    for (const ScriptObjectsForNaming &script : scripts)
    {
        std::string name;
        if (script.number == 0)
        {
            name = "Main";
        }
        else
        {
            // The first class, but a class named "Game" wins (KQ6 script
            // 994); else the first public instance.
            std::string firstClass;
            std::string firstPublicInstance;
            for (const ScriptObjectsForNaming::Object &object : script.objects)
            {
                if (object.isClass)
                {
                    if (firstClass.empty() || (object.name == "Game"))
                    {
                        firstClass = object.name;
                    }
                }
                else if (object.isPublic && firstPublicInstance.empty())
                {
                    firstPublicInstance = object.name;
                }
            }
            name = firstClass.empty() ? firstPublicInstance : firstClass;
        }
        if (name.empty())
        {
            continue;
        }
        name = CleanName(name);
        // Ignore case: Windows file names do. A suffixed name can be taken
        // too; then a second suffix makes it free.
        if (used.find(Upper(name)) != used.end())
        {
            std::string suffixed = fmt::format("{0}_{1}", name, script.number);
            name = suffixed;
            for (int extra = 2; used.find(Upper(name)) != used.end(); extra++)
            {
                name = fmt::format("{0}_{1}", suffixed, extra);
            }
        }
        used.insert(Upper(name));
        names[script.number] = name;
    }
    return names;
}
