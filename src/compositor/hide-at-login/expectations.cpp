#include "expectations.hpp"

#include <algorithm>
#include <strings.h>

namespace hide_at_login
{
void expectations_t::expect(const std::vector<std::string>& ids, clock::time_point deadline)
{
    std::vector<std::string> named;
    std::copy_if(ids.begin(), ids.end(), std::back_inserter(named), [](const std::string& id) { return !id.empty(); });
    if (named.empty())
    {
        return;
    }

    auto same = std::find_if(programs.begin(), programs.end(),
        [&](const program_t& program) { return program.ids.front() == named.front(); });
    if (same != programs.end())
    {
        *same = {named, deadline};
        return;
    }
    programs.push_back({named, deadline});
}

std::optional<std::string> expectations_t::take(const std::string& app_id, clock::time_point now)
{
    auto found = std::find_if(programs.begin(), programs.end(), [&](const program_t& program) {
        return now < program.deadline && std::any_of(program.ids.begin(), program.ids.end(),
            [&](const std::string& id) { return strcasecmp(id.c_str(), app_id.c_str()) == 0; });
    });
    if (found == programs.end())
    {
        return std::nullopt;
    }

    std::string name = found->ids.front();
    programs.erase(found);
    return name;
}

std::vector<std::string> expectations_t::take_expired(clock::time_point now)
{
    std::vector<std::string> names;
    auto kept = std::remove_if(programs.begin(), programs.end(), [&](const program_t& program) {
        if (now < program.deadline)
        {
            return false;
        }
        names.push_back(program.ids.front());
        return true;
    });
    programs.erase(kept, programs.end());
    return names;
}
} // namespace hide_at_login
