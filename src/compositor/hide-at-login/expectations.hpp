#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace hide_at_login
{
using clock = std::chrono::steady_clock;

// programs started at login whose first window should open hidden
// each is known by several ids, its entry name, window class and command
class expectations_t
{
  public:
    // the first id names the program, expecting it again moves its deadline
    void expect(const std::vector<std::string>& ids, clock::time_point deadline);
    // the program a new window belongs to, taken off the list, ids compare without case
    std::optional<std::string> take(const std::string& app_id, clock::time_point now);
    // programs whose time ran out, taken off the list
    std::vector<std::string> take_expired(clock::time_point now);
    bool empty() const { return programs.empty(); }
    size_t count() const { return programs.size(); }

  private:
    struct program_t
    {
        std::vector<std::string> ids;
        clock::time_point deadline;
    };
    std::vector<program_t> programs;
};
} // namespace hide_at_login
