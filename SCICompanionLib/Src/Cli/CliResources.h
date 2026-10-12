#pragma once

// What the commands on the pics, the views and the palettes of a game share
// (plan sections 4.7 to 4.9): the resources that a selector names, the check
// of an output folder, and the sink for the repairs of a reader.

#include "CoreLog.h"
#include "Result.h"
#include <mutex>
#include <string>
#include <vector>

class GameSession;
enum class ResourceType;

namespace cli
{
    // "1 pic", "2 pics".
    std::string CountText(size_t count, const char *word);

    // The numbers of the resources of a type that the game has, from the
    // lowest.
    std::vector<int> ResourceNumbersOf(GameSession &session, ResourceType type);

    // The numbers of "all" that the selectors name: a number ("230") or a
    // range ("100-199"), from the lowest, each number once. The noun is the
    // name of the resource type in a message ("pic", "view"). Usage for a
    // text that is not a number or a range, for a number that is not in
    // "all", and for a range with no number of "all".
    sci::Result<std::vector<int>> SelectResourceNumbers(const std::vector<int> &all, const std::vector<std::string> &selectors, const char *noun);

    // The output folder of a command that writes files for a game: NotFound
    // when it is not a folder, Usage when it is the game folder or a folder
    // inside it (a game can read patch files from a folder of its own).
    sci::Status CheckOutputFolder(GameSession &session, const std::string &outDir);

    // Keeps the warnings and the errors that the readers log while a
    // resource is read: a reader repairs some damage (a palette that stores
    // entries past 255, a cel with a size that is not possible) and only
    // logs it. A codec can log from another thread.
    class RepairLogSink : public ILogSink
    {
    public:
        ~RepairLogSink() { RemoveCoreLogSink(this); }

        void Write(LogLevel level, const std::string &text) override
        {
            if (level != LogLevel::Info)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _text += (_text.empty() ? "" : " ") + text;
            }
        }

        std::string Text()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            return _text;
        }

    private:
        std::mutex _mutex;
        std::string _text;
    };
}
