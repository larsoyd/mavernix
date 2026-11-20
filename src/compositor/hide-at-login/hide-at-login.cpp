// opens the first window of each program the session names minimized, unseen and unfocused
// the session asks right before starting login programs, and the wait ends by itself

#include <chrono>
#include <variant>

#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/animate/animate.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>

#include "expectations.hpp"

namespace hide_at_login
{
const std::string EXPECT_METHOD = "hide-at-login/expect";
const int LONGEST_WAIT_SECONDS = 600;
const int EXPIRY_CHECK_MS = 500;

// the ids of each program, or why the request is refused
std::variant<std::vector<std::vector<std::string>>, std::string> read_programs(const wf::json_t& data)
{
    if (!data.has_member("programs") || !data["programs"].is_array())
    {
        return std::string("programs must be a list of lists of ids");
    }

    std::vector<std::vector<std::string>> programs;
    for (size_t index = 0; index < data["programs"].size(); ++index)
    {
        const wf::json_t ids = data["programs"][index];
        if (!ids.is_array())
        {
            return std::string("programs must be a list of lists of ids");
        }
        std::vector<std::string> names;
        for (size_t at = 0; at < ids.size(); ++at)
        {
            if (!ids[at].is_string())
            {
                return std::string("an id must be text");
            }
            names.push_back(ids[at].as_string());
        }
        programs.push_back(names);
    }
    return programs;
}

class plugin_t : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> methods;
    wf::shared_data::ref_ptr_t<wf::animate::animate_effects_registry_t> effects;
    expectations_t expected;
    wf::wl_timer<true> expiry;
    wf::wl_idle_call end_of_map;

    // core restores a minimized window on the focus request that follows its map
    wf::signal::connection_t<wf::view_focus_request_signal> keep_map_focus =
        [](wf::view_focus_request_signal* request) { request->carried_out = true; };

    void hide(wayfire_toplevel_view view)
    {
        view->set_minimized(true);
        // the open effect shows the window until it ends
        for (const auto& [name, effect] : effects->effects)
        {
            view->erase_data("animation-hook-" + name);
        }
        view->connect(&keep_map_focus);
        end_of_map.run_once([this] { keep_map_focus.disconnect(); });
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_mapped = [this](wf::view_mapped_signal* event) {
        auto view = wf::toplevel_cast(event->view);
        if (!view || view->parent)
        {
            return;
        }
        const auto program = expected.take(view->get_app_id(), clock::now());
        if (!program)
        {
            return;
        }
        hide(view);
        LOGI("[hide-at-login] program=", *program, " app_id=", view->get_app_id(), " view=", view->get_id(),
            " result=hidden");
    };

    bool check_expiry()
    {
        for (const std::string& program : expected.take_expired(clock::now()))
        {
            LOGI("[hide-at-login] program=", program, " result=expired");
        }
        return !expected.empty();
    }

    wf::ipc::method_callback on_expect = [this](const wf::json_t& data) {
        if (!data.has_member("seconds") || !data["seconds"].is_int() || int(data["seconds"]) < 1 ||
            int(data["seconds"]) > LONGEST_WAIT_SECONDS)
        {
            return wf::ipc::json_error("refused: seconds must be a whole number from 1 to " +
                std::to_string(LONGEST_WAIT_SECONDS));
        }
        auto programs = read_programs(data);
        if (auto* why = std::get_if<std::string>(&programs))
        {
            return wf::ipc::json_error("refused: " + *why);
        }

        const auto deadline = clock::now() + std::chrono::seconds(int(data["seconds"]));
        for (const auto& ids : std::get<0>(programs))
        {
            expected.expect(ids, deadline);
        }
        const size_t waiting = expected.count();
        LOGI("[hide-at-login] expect programs=", std::get<0>(programs).size(), " seconds=", int(data["seconds"]),
            " waiting=", waiting);
        expiry.set_timeout(EXPIRY_CHECK_MS, [this] { return check_expiry(); });

        wf::json_t answer = wf::ipc::json_ok();
        answer["waiting"] = int(waiting);
        return answer;
    };

  public:
    void init() override
    {
        methods->register_method(EXPECT_METHOD, on_expect);
        wf::get_core().connect(&on_mapped);
    }

    void fini() override
    {
        methods->unregister_method(EXPECT_METHOD);
        expiry.disconnect();
        end_of_map.disconnect();
        keep_map_focus.disconnect();
    }
};
} // namespace hide_at_login

DECLARE_WAYFIRE_PLUGIN(hide_at_login::plugin_t);
