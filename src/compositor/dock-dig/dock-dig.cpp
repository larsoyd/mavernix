// tells watchers when the pointer digs at the bottom edge of a screen
// the dock is not on, so the dock can come there as on OS X 10.9

#include <algorithm>
#include <string>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>

#include "dig-detect.hpp"
#include "dig-edges.hpp"

namespace dock_dig
{
const std::string WATCH_METHOD = "dock-dig/watch";
const std::string CONFIGURE_METHOD = "dock-dig/configure";
const std::string EVENT = "dock-dig";

class plugin_t : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
    std::vector<wf::ipc::client_interface_t*> watchers;
    std::string dock_output;

    dig_detect_t detect;
    // the screen whose edge the pointer is on, kept until it leaves the row
    std::string at_edge;
    wf::wl_timer<false> timer;
    int buttons_down = 0;

    // the screen with an edge to dig at under the pointer, if any
    std::string edge_under_pointer()
    {
        const auto outputs = wf::get_core().output_layout->get_outputs();
        const wf::pointf_t at = wf::get_core().get_cursor_position();
        std::vector<box_t> screens;
        size_t dock = outputs.size();
        for (size_t i = 0; i < outputs.size(); i++)
        {
            const wf::geometry_t box = outputs[i]->get_layout_geometry();
            screens.push_back({box.x, box.y, box.width, box.height});
            if (outputs[i]->to_string() == dock_output)
            {
                dock = i;
            }
        }
        if (dock == outputs.size())
        {
            return "";
        }
        const std::vector<bool> edges = edges_to_dig(screens, dock);
        for (size_t i = 0; i < outputs.size(); i++)
        {
            if (edges[i] && on_bottom_row(screens[i], at.x, at.y))
            {
                return outputs[i]->to_string();
            }
        }
        return "";
    }

    void leave()
    {
        at_edge.clear();
        detect.leave();
    }

    void keep_time()
    {
        if (detect.due_ms() >= 0)
        {
            const int64_t wait = std::max<int64_t>(detect.due_ms() - wf::get_current_time(), 0);
            timer.set_timeout(uint32_t(wait), [this] { fire(); });
        }
    }

    void fire()
    {
        const bool through = detect.fire(wf::get_current_time());
        keep_time();
        // a held button is a drag or a press, not a dig
        if (!through || buttons_down > 0)
        {
            return;
        }
        wf::json_t event;
        event["event"] = EVENT;
        event["output"] = at_edge;
        event["kind"] = "move";
        for (auto client : watchers)
        {
            client->send_json(event);
        }
    }

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>> on_motion =
        [this](wf::post_input_event_signal<wlr_pointer_motion_event>* signal) {
        const std::string edge = edge_under_pointer();
        const int64_t now = wf::get_current_time();
        if (edge.empty())
        {
            if (!at_edge.empty())
            {
                leave();
            }
            return;
        }
        if (edge != at_edge)
        {
            at_edge = edge;
            detect.enter(now);
        }
        else
        {
            detect.move(signal->event->delta_x, signal->event->delta_y, now);
        }
        keep_time();
    };

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_button_event>> on_button =
        [this](wf::post_input_event_signal<wlr_pointer_button_event>* signal) {
        const bool pressed = signal->event->state == WL_POINTER_BUTTON_STATE_PRESSED;
        buttons_down = std::max(buttons_down + (pressed ? 1 : -1), 0);
    };

    wf::ipc::method_callback_full on_watch = [this](const wf::json_t&, wf::ipc::client_interface_t* client) {
        if (std::find(watchers.begin(), watchers.end(), client) == watchers.end())
        {
            watchers.push_back(client);
        }
        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback on_configure = [this](const wf::json_t& data) {
        if (!data.has_member("output") || !data["output"].is_string())
        {
            return wf::ipc::json_error("output must be a screen's name");
        }
        dock_output = data["output"].as_string();
        leave();
        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [this](wf::ipc::client_disconnected_signal* event) {
        watchers.erase(std::remove(watchers.begin(), watchers.end(), event->client), watchers.end());
    };

  public:
    void init() override
    {
        repo->register_method(WATCH_METHOD, on_watch);
        repo->register_method(CONFIGURE_METHOD, on_configure);
        repo->connect(&on_client_disconnected);
        wf::get_core().connect(&on_motion);
        wf::get_core().connect(&on_button);
    }

    void fini() override
    {
        on_button.disconnect();
        on_motion.disconnect();
        on_client_disconnected.disconnect();
        repo->unregister_method(CONFIGURE_METHOD);
        repo->unregister_method(WATCH_METHOD);
        watchers.clear();
    }
};
} // namespace dock_dig

DECLARE_WAYFIRE_PLUGIN(dock_dig::plugin_t);
