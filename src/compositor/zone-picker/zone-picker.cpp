// a window dragged toward a screen's top is offered a plate of layouts
// dropped on one of its zones it is tiled there

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/preview-indication.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/crossfade.hpp>
#include <wayfire/plugins/grid.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>
#include <wlr/util/edges.h>

#include "zone-layout.hpp"

namespace zone_picker
{
const std::string PLUGIN_NAME = "zone-picker";
const std::string WATCH_METHOD = "zone-picker/watch";
const std::string EVENT = "zone-picker";
// the stock move code files the transformer of a window it drags under its type's name
const std::string DRAGGED_MARK = "N2wf9move_drag19scale_around_grab_tE";
const std::string GHOST_MARK = "zone-picker-ghost";
const int GHOST_STEP_MS = 8;

static_assert(EDGE_LEFT == WLR_EDGE_LEFT && EDGE_RIGHT == WLR_EDGE_RIGHT && EDGE_TOP == WLR_EDGE_TOP &&
    EDGE_BOTTOM == WLR_EDGE_BOTTOM);

box_t to_box(wf::geometry_t geometry)
{
    return {int(geometry.x), int(geometry.y), int(geometry.width), int(geometry.height)};
}

wf::geometry_t from_box(const box_t& box)
{
    return wf::geometry_t{double(box.x), double(box.y), double(box.width), double(box.height)};
}

const char* name_of(state_t state)
{
    return state == state_t::OPEN ? "open" : state == state_t::PEEK ? "peek" : "hidden";
}

wf::json_t to_json(const box_t& box)
{
    wf::json_t json;
    json["x"] = box.x;
    json["y"] = box.y;
    json["width"] = box.width;
    json["height"] = box.height;
    return json;
}

// the zone a window was dropped on, kept so it holds its share when the room changes
struct tiled_zone_t : public wf::custom_data_t
{
    zone_t zone;
    // the screen's size when the window was last put there, its place is in those terms
    wf::dimensions_t screen;
};

class plugin_t : public wf::plugin_interface_t, public wf::per_output_tracker_mixin_t<>
{
    wf::option_wrapper_t<bool> enabled{PLUGIN_NAME + "/enabled"};
    wf::option_wrapper_t<std::string> layouts_text{PLUGIN_NAME + "/layouts"};
    wf::option_wrapper_t<int> distance{PLUGIN_NAME + "/distance"};
    wf::option_wrapper_t<int> peek{PLUGIN_NAME + "/peek"};
    wf::option_wrapper_t<int> padding{PLUGIN_NAME + "/padding"};
    wf::option_wrapper_t<int> gap{PLUGIN_NAME + "/gap"};
    wf::option_wrapper_t<int> gutter{PLUGIN_NAME + "/gutter"};
    wf::option_wrapper_t<int> margin{PLUGIN_NAME + "/margin"};
    wf::option_wrapper_t<double> mini_share{PLUGIN_NAME + "/mini_share"};
    wf::option_wrapper_t<int> mini_min{PLUGIN_NAME + "/mini_min"};
    wf::option_wrapper_t<int> mini_max{PLUGIN_NAME + "/mini_max"};
    wf::option_wrapper_t<double> window_opacity{PLUGIN_NAME + "/window_opacity"};
    wf::option_wrapper_t<int> window_fade_ms{PLUGIN_NAME + "/window_fade_ms"};

    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
    std::vector<wf::ipc::client_interface_t*> watchers;
    std::vector<layout_t> layouts;

    // what is on show, nothing while shown_on is null
    wf::output_t* shown_on = nullptr;
    state_t state = state_t::HIDDEN;
    pick_t pick;
    wayfire_toplevel_view dragged;
    std::shared_ptr<wf::preview_indication_t> preview;

    // the dragged window is drawn above all else, so it turns see-through over an open plate
    struct
    {
        // kept alive until its opacity is back
        std::shared_ptr<wf::view_interface_t> view;
        std::shared_ptr<wf::scene::view_2d_transformer_t> look;
        double from = 1;
        double to = 1;
        int64_t since = 0;
        wf::wl_timer<true> clock;
    } ghost;

    void restore_ghost()
    {
        ghost.clock.disconnect();
        if (ghost.view)
        {
            ghost.view->get_transformed_node()->rem_transformer<wf::scene::floating_inner_node_t>(GHOST_MARK);
            ghost.view->damage();
        }

        ghost.view = nullptr;
        ghost.look = nullptr;
    }

    bool step_ghost()
    {
        const double along = std::clamp(double(wf::get_current_time() - ghost.since) / std::max<int>(window_fade_ms, 1),
            0.0, 1.0);
        ghost.look->alpha = float(ghost.from + (ghost.to - ghost.from) * along * along * (3 - 2 * along));
        ghost.view->damage();
        if (along < 1)
        {
            return true;
        }

        if (ghost.to >= 1)
        {
            restore_ghost();
        }

        return false;
    }

    void fade_window(wayfire_toplevel_view view, double opacity)
    {
        if (ghost.view && (!view || (ghost.view.get() != view.get())))
        {
            restore_ghost();
        }

        if (!view || (!ghost.view && (opacity >= 1)) || (ghost.view && (ghost.to == opacity)))
        {
            return;
        }

        if (!ghost.view)
        {
            ghost.view = view->shared_from_this();
            ghost.look = std::make_shared<wf::scene::view_2d_transformer_t>(view);
            view->get_transformed_node()->add_transformer(ghost.look, wf::TRANSFORMER_2D, GHOST_MARK);
        }

        ghost.from = ghost.look->alpha;
        ghost.to = opacity;
        ghost.since = wf::get_current_time();
        ghost.clock.set_timeout(GHOST_STEP_MS, [this] { return step_ghost(); });
    }

    metrics_t metrics() const
    {
        metrics_t now;
        now.distance = distance;
        now.peek = peek;
        now.padding = padding;
        now.gap = gap;
        now.gutter = gutter;
        now.margin = margin;
        now.mini_share = mini_share;
        now.mini_min = mini_min;
        now.mini_max = std::max<int>(mini_max, mini_min);
        return now;
    }

    static bool is_dragged(wayfire_toplevel_view view)
    {
        return view->get_transformed_node()->get_transformer<wf::scene::floating_inner_node_t>(DRAGGED_MARK) != nullptr;
    }

    wayfire_toplevel_view find_dragged()
    {
        if (dragged && is_dragged(dragged))
        {
            return dragged;
        }

        for (auto& view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (toplevel && toplevel->is_mapped() && is_dragged(toplevel))
            {
                return toplevel;
            }
        }

        return nullptr;
    }

    // a window of one fixed size still counts as resizable to the compositor
    static bool may_tile(wayfire_toplevel_view view)
    {
        const uint32_t needed = wf::VIEW_ALLOW_MOVE | wf::VIEW_ALLOW_RESIZE;
        const wf::dimensions_t least = view->toplevel()->get_min_size();
        const wf::dimensions_t largest = view->toplevel()->get_max_size();
        const bool fixed = (largest.width > 0) && (largest.height > 0) && (least.width >= largest.width) &&
            (least.height >= largest.height);
        return (view->role == wf::VIEW_ROLE_TOPLEVEL) && ((view->get_allowed_actions() & needed) == needed) && !fixed;
    }

    // the window's own limits are of its contents, the frame comes on top
    static bool takes(wayfire_toplevel_view view, const box_t& box)
    {
        const auto& margins = view->toplevel()->pending().margins;
        const int frame_width = int(margins.left + margins.right);
        const int frame_height = int(margins.top + margins.bottom);
        const wf::dimensions_t least = view->toplevel()->get_min_size();
        const wf::dimensions_t largest = view->toplevel()->get_max_size();
        return (box.width >= least.width + frame_width) && (box.height >= least.height + frame_height) &&
               ((largest.width <= 0) || (box.width <= largest.width + frame_width)) &&
               ((largest.height <= 0) || (box.height <= largest.height + frame_height));
    }

    static box_t room_on(wf::output_t* output)
    {
        return to_box(output->workarea->get_workarea());
    }

    void hide_preview()
    {
        if (preview)
        {
            const auto at = preview->view->get_geometry();
            preview->set_target_geometry(wf::pointf_t{at.x + at.width / 2, at.y}, 0, true);
            preview = nullptr;
        }
    }

    void show_preview(wf::output_t* output, wf::point_t from, const box_t& target)
    {
        hide_preview();
        preview = std::make_shared<wf::preview_indication_t>(wf::pointf_t{double(from.x), double(from.y)}, output, "move");
        preview->set_target_geometry(from_box(target), 1);
    }

    void tell(const plate_t& plate, wayfire_toplevel_view view)
    {
        LOGI("[zone-picker] state=", name_of(state), " output=", shown_on->to_string(), " layout=", pick.layout,
            " zone=", pick.zone, " watchers=", watchers.size());
        wf::json_t event;
        event["event"] = EVENT;
        event["output"] = shown_on->to_string();
        event["state"] = name_of(state);
        if (state != state_t::HIDDEN)
        {
            const box_t room = room_on(shown_on);
            event["plate"] = to_json(plate.box);
            event["peek"] = int(peek);
            auto minis = wf::json_t::array();
            for (std::size_t index = 0; index < plate.minis.size(); index++)
            {
                wf::json_t mini = to_json(plate.minis[index].box);
                auto zones = wf::json_t::array();
                for (std::size_t zone = 0; zone < plate.minis[index].cells.size(); zone++)
                {
                    wf::json_t cell = to_json(plate.minis[index].cells[zone]);
                    cell["name"] = layouts[index][zone].name;
                    cell["ok"] = takes(view, target_box(room, layouts[index][zone]));
                    zones.append(cell);
                }

                mini["zones"] = zones;
                minis.append(mini);
            }

            event["minis"] = minis;
            if (pick.any())
            {
                auto hover = wf::json_t::array();
                hover.append(pick.layout);
                hover.append(pick.zone);
                event["hover"] = hover;
                event["name"] = layouts[pick.layout][pick.zone].name;
            }
            else
            {
                event["hover"] = wf::json_t::null();
            }
        }

        for (auto client : watchers)
        {
            client->send_json(event);
        }
    }

    void hide()
    {
        hide_preview();
        fade_window(dragged, 1);
        dragged = nullptr;
        if (shown_on)
        {
            state = state_t::HIDDEN;
            pick = {};
            tell({}, nullptr);
            shown_on = nullptr;
        }
    }

    // where the pointer leaves the plate, and the zone under it if the window fits there
    void follow(wf::output_t* output, wf::point_t at, wayfire_toplevel_view view)
    {
        const auto screen = output->get_relative_geometry();
        const box_t room = room_on(output);
        const plate_t plate = lay_out(int(screen.width), int(screen.height), room, layouts, metrics());
        const state_t before = shown_on ? state : state_t::HIDDEN;
        const state_t now = next_state(before, plate, metrics(), at.x, at.y);
        pick_t picked = now == state_t::OPEN ? zone_at(plate, layouts, at.x, at.y) : pick_t{};
        if (picked.any() && !takes(view, target_box(room, layouts[picked.layout][picked.zone])))
        {
            picked = {};
        }

        if ((now == before) && (picked == pick))
        {
            return;
        }

        if (now == state_t::HIDDEN)
        {
            hide();
            return;
        }

        const bool new_target = !(picked == pick);
        shown_on = output;
        state = now;
        pick = picked;
        dragged = view;
        fade_window(view, state == state_t::OPEN ? std::clamp<double>(window_opacity, 0, 1) : 1);
        tell(plate, view);
        if (!pick.any())
        {
            hide_preview();
        }
        else if (new_target)
        {
            show_preview(output, at, target_box(room, layouts[pick.layout][pick.zone]));
        }
    }

    // grid animates whatever it finds on the window, a crossfade would open a gap beside a neighbour
    static nonstd::observer_ptr<wf::grid::grid_animation_t> placing(wayfire_toplevel_view view, uint32_t edges)
    {
        if ((edges != wf::TILED_EDGES_ALL) && !view->has_data<wf::grid::grid_animation_t>())
        {
            wf::option_wrapper_t<wf::animation_description_t> duration{"grid/duration"};
            view->store_data(std::make_unique<wf::grid::grid_animation_t>(
                view, wf::grid::grid_animation_t::NONE, duration));
        }

        return view->get_data<wf::grid::grid_animation_t>();
    }

    // how far the workspace a tiled window is on lies from the one shown
    static wf::point_t workspace_shift(wayfire_toplevel_view view, wf::dimensions_t screen)
    {
        const auto now = view->get_pending_geometry();
        return {int(std::floor((now.x + now.width / 2) / screen.width)),
            int(std::floor((now.y + now.height / 2) / screen.height))};
    }

    static void put(wayfire_toplevel_view view, const zone_t& zone, wf::point_t workspaces_away = {0, 0})
    {
        wf::output_t* output = view->get_output();
        const auto screen = output->get_relative_geometry();
        const uint32_t edges = tiled_edges(zone);
        wf::geometry_t box = from_box(target_box(room_on(output), zone));
        box.x += workspaces_away.x * screen.width;
        box.y += workspaces_away.y * screen.height;
        if (auto kept = view->get_data<tiled_zone_t>())
        {
            kept->screen = wf::dimensions(screen);
        }

        placing(view, edges)->adjust_target_geometry(box, edges);
    }

    void tile(wayfire_toplevel_view view, const zone_t& zone)
    {
        const uint32_t edges = tiled_edges(zone);
        LOGI("[zone-picker] tile view=", view->get_id(), " zone=", zone.name, " edges=", edges);
        if (edges == wf::TILED_EDGES_ALL)
        {
            wf::get_core().default_wm->tile_request(view, edges);
            return;
        }

        if (edges)
        {
            view->get_data_safe<tiled_zone_t>()->zone = zone;
        }

        view->toplevel()->pending().tiled_edges = edges;
        put(view, zone);
    }

    wf::signal::connection_t<wf::grid::grid_handle_move_signal> on_grid_move =
        [this](wf::grid::grid_handle_move_signal* event) {
        // a drag sends this for a screen before it sends anything for the next one
        if (event->operation == wf::grid::MOVE_OP_CLEAR_PREVIEW)
        {
            hide();
            return;
        }

        const bool drop = event->operation == wf::grid::MOVE_OP_DROP;
        auto view = drop ? event->view : find_dragged();
        if (!enabled || !view || !may_tile(view))
        {
            hide();
            return;
        }

        follow(event->output, event->input, view);
        const pick_t dropped_on = pick;
        if (drop)
        {
            hide();
        }

        if (!dropped_on.any())
        {
            return;
        }

        // the stock snapping only clears what it shows
        event->operation = wf::grid::MOVE_OP_CLEAR_PREVIEW;
        if (drop)
        {
            tile(view, layouts[dropped_on.layout][dropped_on.zone]);
        }
    };

    // the same tiling asked again keeps the zone, on a new screen too
    wf::signal::connection_t<wf::view_tile_request_signal> on_tile_request = [](wf::view_tile_request_signal* event) {
        auto kept = event->view->get_data<tiled_zone_t>();
        if (kept && (event->edges == tiled_edges(kept->zone)))
        {
            event->carried_out = true;
            put(event->view, kept->zone,
                event->workspace - event->view->get_output()->wset()->get_current_workspace());
        }
    };

    // any other tiling ends it
    wf::signal::connection_t<wf::view_tiled_signal> on_tiled = [](wf::view_tiled_signal* event) {
        auto kept = event->view->get_data<tiled_zone_t>();
        if (kept && (event->new_edges != tiled_edges(kept->zone)))
        {
            event->view->erase_data<tiled_zone_t>();
        }
    };

    wf::signal::connection_t<wf::view_fullscreen_request_signal> on_fullscreen_request =
        [](wf::view_fullscreen_request_signal* event) {
        auto kept = event->view->get_data<tiled_zone_t>();
        if (kept && !event->state)
        {
            event->carried_out = true;
            put(event->view, kept->zone,
                event->workspace - event->view->get_output()->wset()->get_current_workspace());
        }
    };

    wf::signal::connection_t<wf::workarea_changed_signal> on_workarea_changed =
        [](wf::workarea_changed_signal* event) {
        for (auto& view : event->output->wset()->get_views(wf::WSET_MAPPED_ONLY))
        {
            auto kept = view->get_data<tiled_zone_t>();
            if (kept && !view->pending_fullscreen())
            {
                put(view, kept->zone, workspace_shift(view, kept->screen));
            }
        }
    };

    wf::ipc::method_callback_full on_watch = [this](const wf::json_t&, wf::ipc::client_interface_t* client) {
        if (std::find(watchers.begin(), watchers.end(), client) == watchers.end())
        {
            watchers.push_back(client);
        }

        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [this](wf::ipc::client_disconnected_signal* event) {
        watchers.erase(std::remove(watchers.begin(), watchers.end(), event->client), watchers.end());
    };

  public:
    void init() override
    {
        layouts = parse_layouts(layouts_text);
        layouts_text.set_callback([this] {
            hide();
            layouts = parse_layouts(layouts_text);
        });
        repo->register_method(WATCH_METHOD, on_watch);
        repo->connect(&on_client_disconnected);
        wf::get_core().connect(&on_grid_move);
        init_output_tracking();
    }

    void fini() override
    {
        hide();
        restore_ghost();
        fini_output_tracking();
        on_grid_move.disconnect();
        on_client_disconnected.disconnect();
        repo->unregister_method(WATCH_METHOD);
        for (auto& view : wf::get_core().get_all_views())
        {
            view->erase_data<tiled_zone_t>();
        }
    }

    // ahead of the plugins that snap and fit tiles, which must see a drop on a zone as none of theirs
    int get_order_hint() const override { return -2; }

    void handle_new_output(wf::output_t* output) override
    {
        output->connect(&on_tile_request);
        output->connect(&on_tiled);
        output->connect(&on_fullscreen_request);
        output->connect(&on_workarea_changed);
    }

    void handle_output_removed(wf::output_t*) override {}
};
} // namespace zone_picker

DECLARE_WAYFIRE_PLUGIN(zone_picker::plugin_t);
