// tells watchers when the pointer pushes against a screen's bottom edge: move for a screen
// the dock is not on, reveal for its own under a full screen window, as on OS X 10.9

#include <algorithm>
#include <ctime>
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
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>

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

    struct edge_t
    {
        std::string output;
        std::string kind;
        // a touch that holds still does it, no dig needed
        bool by_rest = false;

        bool operator==(const edge_t& other) const
        {
            return output == other.output && kind == other.kind && by_rest == other.by_rest;
        }
    };

    dig_detect_t detect;
    rest_detect_t rest;
    // the edge the pointer is on, kept until it leaves the row
    edge_t at_edge;
    // the edge the pushes so far were at, another edge starts from nothing
    edge_t pushed_edge;
    wf::wl_timer<false> timer;
    int buttons_down = 0;

    // whether a full screen window lies on top of the screen
    static bool covered(wf::output_t* output)
    {
        const auto views = output->wset()->get_views(wf::WSET_CURRENT_WORKSPACE | wf::WSET_MAPPED_ONLY |
            wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_SORT_STACKING);
        return !views.empty() && views.front()->pending_fullscreen();
    }

    edge_t edge_under_pointer()
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
            return {};
        }
        const bool dock_covered = covered(outputs[dock]);
        if (on_bottom_row(screens[dock], at.x, at.y))
        {
            return dock_covered ? edge_t{dock_output, "reveal", false} : edge_t{};
        }
        const std::vector<bool> edges = edges_to_dig(screens, dock);
        for (size_t i = 0; i < outputs.size(); i++)
        {
            if (edges[i] && on_bottom_row(screens[i], at.x, at.y))
            {
                // from under a full screen window an open screen is reached by a rest
                return {outputs[i]->to_string(), "move", dock_covered && !covered(outputs[i])};
            }
        }
        return {};
    }

    // a surface under a full screen window may wait for a frame event that never comes
    static void wake_dock(const std::string& output)
    {
        for (auto view : wf::get_core().get_all_views())
        {
            wlr_surface* surface = view->get_wlr_surface();
            if (surface && view->get_app_id() == "mavernix-dock" && view->get_output() &&
                view->get_output()->to_string() == output)
            {
                timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                wlr_surface_send_frame_done(surface, &now);
            }
        }
    }

    void leave()
    {
        at_edge = {};
        detect.leave();
        rest.leave();
    }

    // only a rest comes due without a move
    void keep_time()
    {
        if (at_edge.by_rest && rest.due_ms() >= 0)
        {
            const int64_t wait = std::max<int64_t>(rest.due_ms() - wf::get_current_time(), 0);
            timer.set_timeout(uint32_t(wait), [this] {
                if (rest.fire())
                {
                    tell();
                }
            });
        }
    }

    void tell()
    {
        const char *by = at_edge.by_rest ? "rest" : "push";
        // a held button is a drag or a press, not a dig
        if (buttons_down > 0)
        {
            LOGI("[dock-dig] refused: button held output=", at_edge.output, " kind=", at_edge.kind, " by=", by);
            return;
        }
        LOGI("[dock-dig] dug output=", at_edge.output, " kind=", at_edge.kind, " by=", by,
            " watchers=", watchers.size());
        wf::json_t event;
        event["event"] = EVENT;
        event["output"] = at_edge.output;
        event["kind"] = at_edge.kind;
        for (auto client : watchers)
        {
            client->send_json(event);
        }
        if (at_edge.kind == "reveal")
        {
            wake_dock(at_edge.output);
        }
    }

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>> on_motion =
        [this](wf::post_input_event_signal<wlr_pointer_motion_event>* signal) {
        const edge_t edge = edge_under_pointer();
        const wf::pointf_t at = wf::get_core().get_cursor_position();
        const int64_t now = wf::get_current_time();
        if (edge.output.empty())
        {
            if (!at_edge.output.empty())
            {
                leave();
            }
            return;
        }
        if (!(edge == at_edge))
        {
            if (!(edge == pushed_edge))
            {
                detect = dig_detect_t();
                pushed_edge = edge;
            }
            at_edge = edge;
            edge.by_rest ? rest.enter(at.x, at.y, now) : detect.enter(now);
        }
        else if (edge.by_rest)
        {
            rest.move(at.x, at.y, now);
        }
        else if (detect.move(signal->event->delta_x, signal->event->delta_y, now))
        {
            tell();
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
        detect = dig_detect_t();
        pushed_edge = {};
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
