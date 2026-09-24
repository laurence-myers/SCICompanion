#include "stdafx.h"
#include "TestSupport.h"
#include "AppState.h"
#include "Helper.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
namespace fs = std::filesystem;

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

GameSession &GameCopy::OpenCopy(const char *templateFolder, bool bare)
{
    Make(templateFolder, bare);
    return Open();
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
