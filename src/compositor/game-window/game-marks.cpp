#include "game-marks.hpp"

#include <fnmatch.h>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

namespace game_window
{
bool matches_any(const std::string& patterns, const std::string& app_id)
{
    std::istringstream words(patterns);
    std::string pattern;
    while (words >> pattern)
    {
        if (fnmatch(pattern.c_str(), app_id.c_str(), FNM_CASEFOLD) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool is_steam_id(const std::string& text)
{
    static const std::regex number{"[1-9][0-9]*"};
    return std::regex_match(text, number);
}

bool game_environment(const std::string& entries)
{
    std::istringstream list(entries);
    std::string entry;
    while (std::getline(list, entry, '\0'))
    {
        const auto equals = entry.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }
        const std::string name = entry.substr(0, equals);
        const std::string value = entry.substr(equals + 1);
        const bool steam = name == "SteamAppId" || name == "SteamGameId" || name == "STEAM_COMPAT_APP_ID";
        const bool launcher = name == "GAME_NAME" || name == "LUTRIS_GAME_UUID" || name == "UMU_ID";
        if ((steam && is_steam_id(value)) || (launcher && !value.empty()))
        {
            return true;
        }
    }
    return false;
}

bool started_as_game(pid_t pid)
{
    if (pid <= 0)
    {
        return false;
    }
    std::ifstream file{"/proc/" + std::to_string(pid) + "/environ", std::ios::binary};
    return game_environment({std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()});
}
} // namespace game_window
