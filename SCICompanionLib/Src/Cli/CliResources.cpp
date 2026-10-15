#include "stdafx.h"
#include "CliResources.h"
#include "CliCommands.h"
#include "GameSession.h"
#include "GameFolderHelper.h"
#include "ResourceMap.h"
#include "ResourceContainer.h"
#include "crc.h"
#include "format.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace cli
{
    std::string CountText(size_t count, const char *word)
    {
        return fmt::format("{0} {1}{2}", count, word, (count == 1) ? "" : "s");
    }

    uint32_t Crc32Of(const std::vector<uint8_t> &data)
    {
        return data.empty() ? 0 : (uint32_t)crcFast(data.data(), (int)data.size());
    }

    std::string AlignedTextTable(const std::vector<std::string> &header, const std::vector<bool> &right, const std::vector<std::vector<std::string>> &rows, const std::vector<std::string> &errors)
    {
        std::vector<std::vector<std::string>> table = { header };
        table.insert(table.end(), rows.begin(), rows.end());
        std::vector<size_t> widths(header.size(), 0);
        for (const std::vector<std::string> &cells : table)
        {
            for (size_t i = 0; i < cells.size(); i++)
            {
                widths[i] = (std::max)(widths[i], cells[i].size());
            }
        }
        std::string text;
        for (size_t r = 0; r < table.size(); r++)
        {
            std::string line;
            for (size_t i = 0; i < table[r].size(); i++)
            {
                const std::string &cell = table[r][i];
                const std::string padding(widths[i] - cell.size(), ' ');
                line += (i == 0) ? "" : "  ";
                line += right[i] ? (padding + cell) : (cell + padding);
            }
            // No spaces at the end of a line, and none before the error but
            // the two that divide it from the last cell.
            line.erase(line.find_last_not_of(' ') + 1);
            if ((r > 0) && !errors[r - 1].empty())
            {
                line += "  (unreadable: " + errors[r - 1] + ")";
            }
            text += line + "\n";
        }
        return text;
    }

    bool ParseNumber(const std::string &text, int &number)
    {
        if (text.empty())
        {
            return false;
        }
        int value = 0;
        for (char ch : text)
        {
            if ((ch < '0') || (ch > '9'))
            {
                return false;
            }
            value = (value * 10) + (ch - '0');
            if (value > 65535)
            {
                return false;
            }
        }
        number = value;
        return true;
    }

    std::vector<int> ResourceNumbersOf(GameSession &session, ResourceType type)
    {
        std::set<int> numbers;
        auto container = session.ResourceMap().Resources(ResourceTypeToFlag(type), session.Helper().GetDefaultEnumFlags() | ResourceEnumFlags::MostRecentOnly);
        for (auto it = container->begin(); it != container->end(); ++it)
        {
            numbers.insert(it.GetResourceNumber());
        }
        return std::vector<int>(numbers.begin(), numbers.end());
    }

    sci::Result<std::vector<int>> SelectResourceNumbers(const std::vector<int> &all, const std::vector<std::string> &selectors, const char *noun)
    {
        std::set<int> selected;
        for (const std::string &selector : selectors)
        {
            int first = 0;
            int last = 0;
            size_t dash = selector.find('-');
            bool parsed = (dash == std::string::npos) ? ParseNumber(selector, first) :
                (ParseNumber(selector.substr(0, dash), first) && ParseNumber(selector.substr(dash + 1), last));
            if (dash == std::string::npos)
            {
                last = first;
            }
            if (!parsed || (first > last))
            {
                return sci::Fail(sci::ErrorCode::Usage, fmt::format("\"{0}\" is not a {1} number or a range of {1} numbers (for example 230 or 100-199)", selector, noun));
            }
            bool any = false;
            for (int number : all)
            {
                if ((number >= first) && (number <= last))
                {
                    selected.insert(number);
                    any = true;
                }
            }
            if (!any)
            {
                return sci::Fail(sci::ErrorCode::Usage, (first == last) ? fmt::format("the game has no {0} {1}", noun, first) : fmt::format("the game has no {0} in the range {1}-{2}", noun, first, last));
            }
        }
        return std::vector<int>(selected.begin(), selected.end());
    }

    sci::Status CheckOutputFolder(GameSession &session, const std::string &outDir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(outDir, ec))
        {
            return sci::Fail(sci::ErrorCode::NotFound, "the output folder does not exist: " + outDir);
        }
        switch (PlaceInGameFolder(session.Helper().GameFolder, outDir))
        {
        case GameFolderPlace::GameFolder:
            return sci::Fail(sci::ErrorCode::Usage, "the output folder is the game folder; give a folder outside the game");
        case GameFolderPlace::Inside:
            return sci::Fail(sci::ErrorCode::Usage, "the output folder is inside the game folder; give a folder outside the game");
        default:
            return sci::Ok();
        }
    }
}
