#pragma once

// Helpers that the test files share: a test with no AppState, a copy of a
// template game with its sessions, files, the text of an assert message,
// and the asserts of a result.

#include "CppUnitTest.h"
#include "GameSession.h"
#include "Result.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AppState;

// The template games, under the test module folder.
extern const char *const TemplateSci0;
extern const char *const TemplateSci11;

// Sets appState to null for its life, as scic runs: the code under test
// must work with no GUI.
class NoAppState
{
public:
    NoAppState();
    ~NoAppState();
    NoAppState(const NoAppState &) = delete;
    NoAppState &operator=(const NoAppState &) = delete;

private:
    AppState *_saved;
};

// The session options of the tests: the data folder is the test module
// folder.
SessionOptions TestSessionOptions();

// A copy of a template game in a temp folder, and the sessions that are
// open on it. Remove, and the destructor, close the sessions and delete the
// copy, also its read-only files.
class GameCopy
{
public:
    GameCopy() = default;
    ~GameCopy();
    GameCopy(const GameCopy &) = delete;
    GameCopy &operator=(const GameCopy &) = delete;

    // A new copy of the template; the copy before it is removed. Bare: with
    // no game.ini and no src folder.
    const std::string &Make(const char *templateFolder, bool bare = false);
    // A new session on the copy; an assert fails when it does not open. The
    // other sessions stay open.
    GameSession &Open(const SessionOptions &options = TestSessionOptions());
    // Make, then Open.
    GameSession &OpenCopy(const char *templateFolder, bool bare = false);
    // Closes the sessions, for a test that then changes the files.
    void CloseSessions();
    void Remove();

    const std::string &Folder() const { return _folder; }
    // <copy>\name, and <copy>\src\name.
    std::string Path(const std::string &name) const;
    std::string Src(const std::string &name) const;
    bool Has(const std::string &name) const;

private:
    std::string _folder;
    std::vector<std::unique_ptr<GameSession>> _sessions;
};

// Deletes a folder and all in it, also read-only files. Nothing happens
// when the folder is not there.
void RemoveFolder(const std::string &folder);

// The text for an assert message: each byte is one wide character.
std::wstring Wide(const std::string &text);

// The file as text or as bytes: empty when it cannot be read.
std::string ReadFileText(const std::string &path);
std::vector<uint8_t> ReadFileBytes(const std::string &path);
// Writes the file, in place of what it had.
void WriteFileText(const std::string &path, const std::string &text);
void WriteFileBytes(const std::string &path, const std::vector<uint8_t> &bytes);
// Replaces the first "from" in the file. An assert fails when the file does
// not have it.
void ReplaceFirst(const std::string &path, const std::string &from, const std::string &to);
// How many times "what" is in the text.
size_t CountOf(const std::string &text, const std::string &what);
// The text in upper case (ASCII).
std::string Upper(std::string text);

// Asserts that the result has a value; the message is the error of the
// result, after the text of "what".
template<typename R>
void AssertOk(const R &result, const std::string &what = std::string())
{
    std::string message = what;
    if (!result.has_value())
    {
        message += (what.empty() ? "" : ": ") + result.error().ToString();
    }
    Microsoft::VisualStudio::CppUnitTestFramework::Assert::IsTrue(result.has_value(), Wide(message).c_str());
}

// The value of the result, after AssertOk.
template<typename T>
T &ValueOf(sci::Result<T> &result, const std::string &what = std::string())
{
    AssertOk(result, what);
    return *result;
}
