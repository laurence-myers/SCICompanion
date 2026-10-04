#include "stdafx.h"
#include "TestSupport.h"
#include "AppState.h"
#include "Helper.h"
#include "CompileBatch.h"
#include "CompiledScript.h"
#include "SCO.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

namespace
{
    // A compile with no abort and no events, for the setup of a fixture.
    sci::Result<CompileReport> Compile(GameSession &session, const std::vector<ScriptId> &scripts, const CompileOptions &options = CompileOptions())
    {
        std::atomic<bool> abort(false);
        ICompileEvents events;
        return CompileScripts(session, scripts, options, abort, events);
    }
}

const char *const TemplateSci0 = "\\TemplateGame\\SCI0";
const char *const TemplateSci11 = "\\TemplateGame\\SCI1.1";

NoAppState::NoAppState() : _saved(appState)
{
    appState = nullptr;
}

NoAppState::~NoAppState()
{
    appState = _saved;
}

SessionOptions TestSessionOptions()
{
    SessionOptions options;
    options.dataFolder = GetTestModuleDirectory();
    return options;
}

GameCopy::~GameCopy()
{
    Remove();
}

const std::string &GameCopy::Make(const char *templateFolder, bool bare)
{
    Remove();
    _folder = CopyGameFromModuleFolder(templateFolder);
    if (bare)
    {
        std::error_code ec;
        fs::remove(fs::path(_folder) / "game.ini", ec);
        fs::remove_all(fs::path(_folder) / "src", ec);
    }
    return _folder;
}

GameSession &GameCopy::Open(const SessionOptions &options)
{
    _sessions.push_back(std::make_unique<GameSession>(options));
    AssertOk(_sessions.back()->Open(_folder), "setup: the copy must open");
    return *_sessions.back();
}

GameSession &GameCopy::OpenCopy(const char *templateFolder, bool bare, const SessionOptions &options)
{
    Make(templateFolder, bare);
    return Open(options);
}

void GameCopy::CloseSessions()
{
    // The last session first: it can use the state that an earlier one set.
    while (!_sessions.empty())
    {
        _sessions.pop_back();
    }
}

void GameCopy::Remove()
{
    CloseSessions();
    RemoveFolder(_folder);
    _folder.clear();
}

std::string GameCopy::Path(const std::string &name) const
{
    return (fs::path(_folder) / name).string();
}

std::string GameCopy::Src(const std::string &name) const
{
    return (fs::path(_folder) / "src" / name).string();
}

bool GameCopy::Has(const std::string &name) const
{
    std::error_code ec;
    return fs::exists(fs::path(_folder) / name, ec);
}

void RemoveFolder(const std::string &folder)
{
    if (folder.empty())
    {
        return;
    }
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && (it != end); it.increment(ec))
    {
        DWORD attributes = GetFileAttributesW(it->path().c_str());
        if ((attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_READONLY))
        {
            SetFileAttributesW(it->path().c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
        }
    }
    fs::remove_all(folder, ec);
}

ScopedEnvironmentVariable::ScopedEnvironmentVariable(const char *name, const char *value) : _name(name), _wasSet(false)
{
    DWORD length = GetEnvironmentVariableA(name, nullptr, 0);
    if (length > 0)
    {
        std::string saved(length, '\0');
        DWORD copied = GetEnvironmentVariableA(name, &saved[0], length);
        if (copied < length)
        {
            saved.resize(copied);
            _saved = saved;
            _wasSet = true;
        }
    }
    SetEnvironmentVariableA(name, value);
}

ScopedEnvironmentVariable::~ScopedEnvironmentVariable()
{
    SetEnvironmentVariableA(_name.c_str(), _wasSet ? _saved.c_str() : nullptr);
}

std::wstring Wide(const std::string &text)
{
    return std::wstring(text.begin(), text.end());
}

std::string ReadFileText(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::vector<uint8_t> ReadFileBytes(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void WriteFileText(const std::string &path, const std::string &text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

void WriteFileBytes(const std::string &path, const std::vector<uint8_t> &bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void ReplaceFirst(const std::string &path, const std::string &from, const std::string &to)
{
    std::string text = ReadFileText(path);
    size_t at = text.find(from);
    Assert::IsTrue(at != std::string::npos, Wide("setup: \"" + from + "\" in " + path).c_str());
    text.replace(at, from.size(), to);
    WriteFileText(path, text);
}

size_t CountOf(const std::string &text, const std::string &what)
{
    size_t count = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
    {
        count++;
    }
    return count;
}

std::string Upper(std::string text)
{
    for (char &ch : text)
    {
        ch = (char)toupper((unsigned char)ch);
    }
    return text;
}

std::string JoinLines(const std::vector<std::string> &lines)
{
    std::string text;
    for (const std::string &line : lines)
    {
        text += line + "\n";
    }
    return text;
}

void MakeReadOnly(const std::string &path)
{
    Assert::IsTrue(SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_READONLY) != 0, Wide("setup: " + path + " is read-only").c_str());
}

void WriteReadOnlyFile(const std::string &path, const std::string &text)
{
    WriteFileText(path, text);
    MakeReadOnly(path);
}

ScriptId ScriptAt(const std::string &path, uint16_t number)
{
    ScriptId script(path.c_str());
    script.SetResourceNumber(number);
    return script;
}

void WriteUnreadableScript(GameCopy &game, uint16_t number)
{
    WriteFileBytes(game.Path("script." + std::to_string(number)), { 0x80 | (uint8_t)ResourceType::Script, 0, 5, 0, 1 });
}

std::vector<uint8_t> MakeTruncatedScript995(size_t size)
{
    std::vector<uint8_t> data((size < 428) ? 428 : size, 0x48); // ret
    auto header = [&data](size_t offset, uint8_t type, uint16_t length)
    {
        data[offset] = type;
        data[offset + 1] = 0;
        data[offset + 2] = (uint8_t)(length & 0xff);
        data[offset + 3] = (uint8_t)(length >> 8);
    };
    header(0, 7, 8); // exports
    data[4] = 1;
    data[5] = 0;
    data[6] = 12;
    data[7] = 0;
    header(8, 2, 8); // code
    data[12] = 0x83; // lal 0
    data[13] = 0;
    data[14] = 0x48; // ret
    data[15] = 0x48;
    header(16, 8, 408); // relocations
    std::fill(data.begin() + 20, data.begin() + 424, (uint8_t)0);
    header(424, 2, 2950); // code
    data.resize(size);
    return data;
}

void NameMainGlobals(GameCopy &game, const std::vector<size_t> &slots)
{
    GameSession &session = game.Open();
    GlobalCompiledScriptLookups lookups;
    AssertOk(lookups.TryLoad(session.Helper()));
    std::unique_ptr<CSCOFile> mainSCO = GetExistingSCOFromScriptNumber(session.Helper(), 0, lookups.GetSelectorTable());
    Assert::IsNotNull(mainSCO.get(), L"setup: Main.sco");
    for (size_t slot : slots)
    {
        mainSCO->GetVariables()[slot].SetName("global" + std::to_string(slot));
    }
    AssertOk(SaveSCOFile(session.Helper(), *mainSCO));
    game.CloseSessions();
}

void PrepareStaleFixtures(GameCopy &game)
{
    game.Make(TemplateSci11);
    NameMainGlobals(game, { 5 });
    for (const auto &fixture : std::vector<std::pair<std::string, std::string>>{ { "BatchGlobalsA", "950" }, { "BatchGlobalsB", "951" } })
    {
        std::string text = ReadFileText(GetTestFileDirectory("Decompile\\SCI1.1") + "\\" + fixture.first + ".sc");
        std::string declared = "(script# " + fixture.second + ")";
        size_t at = text.find(declared);
        Assert::IsTrue(at != std::string::npos, L"setup: the fixture declares its number");
        text.replace(at, declared.size(), (fixture.second == "950") ? "(script# 959)" : "(script# 960)");
        WriteFileText(game.Src(fixture.first + ".sc"), text);
    }
    auto compiled = Compile(game.Open(), { ScriptAt(game.Src("BatchGlobalsA.sc"), 959), ScriptAt(game.Src("BatchGlobalsB.sc"), 960) });
    Assert::IsTrue(compiled.has_value() && compiled->Succeeded(), L"setup: the fixtures must compile");
    game.CloseSessions();
}
