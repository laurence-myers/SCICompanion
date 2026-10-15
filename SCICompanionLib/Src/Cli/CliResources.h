#pragma once

// What the commands on the pics, the views and the palettes of a game share
// (plan sections 4.7 to 4.9): the resources that a selector names, the check
// of an output folder, and the sink for the repairs of a reader.

#include "CoreLog.h"
#include "Result.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class GameSession;
enum class ResourceType;

namespace cli
{
    // "1 pic", "2 pics", "0 errors": the count and the word, with an "s"
    // for each count but 1.
    std::string CountText(size_t count, const char *word);

    // The CRC-32 of the bytes of a resource; 0 for no bytes.
    uint32_t Crc32Of(const std::vector<uint8_t> &data);

    // A text table of list: the header line, then a line for each row, with
    // the columns aligned (right: the numbers to the right, the texts to the
    // left), two spaces between the columns, and no space at the end of a
    // line. A row with an error (errors has a text for each row; empty: no
    // error) ends with "  (unreadable: <error>)".
    std::string AlignedTextTable(const std::vector<std::string> &header, const std::vector<bool> &right, const std::vector<std::vector<std::string>> &rows, const std::vector<std::string> &errors);

    // A number of 0 to 65535: digits only, with any count of zeros before
    // them. False for another text; the number then keeps its value.
    bool ParseNumber(const std::string &text, int &number);

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
        ~RepairLogSink();

        void Write(LogLevel level, const std::string &text) override;
        // The texts of the warnings and the errors so far, with a space
        // between them; empty for none.
        std::string Text();

    private:
        std::mutex _mutex;
        std::string _text;
    };
}
