// windows put on a half, a quarter or the whole screen keep room around them
// games and listed apps fill their share as before

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include <wayfire/config-backend.hpp>
#include <wayfire/core.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/preview-indication.hpp>
#include <wayfire/plugins/crossfade.hpp>
#include <wayfire/plugins/grid.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>

#include "gap-math.hpp"
#include "game-window.hpp"
#include "window-gaps.hpp"

namespace window_gaps
{
const std::string PLUGIN_NAME = "window-gaps";
// the stock move code files the transformer of a window it drags under its type's name
const std::string DRAGGED_MARK = "N2wf9move_drag19scale_around_grab_tE";
const char* const SLOT_NAMES[] = {"", "bl", "b", "br", "l", "c", "r", "tl", "t", "tr"};

box_t to_box(wf::geometry_t geometry)
{
    return {int(geometry.x), int(geometry.y), int(geometry.width), int(geometry.height)};
}

wf::geometry_t from_box(const box_t& box)
{
    return wf::geometry_t{double(box.x), double(box.y), double(box.width), double(box.height)};
}

// the grid slot a window was put in, kept so it holds its share when the room or the gap changes
struct placed_slot_t : public wf::custom_data_t
{
    int slot = 0;
    // the screen's size when the window was last put there, its place is in those terms
    wf::dimensions_t screen;
};

class plugin_t : public wf::plugin_interface_t, public wf::per_output_tracker_mixin_t<>
{
    wf::option_wrapper_t<int> gap;
    wf::option_wrapper_t<std::string> full_size_apps;
    wf::option_wrapper_t<bool> games_fill;
    wf::option_wrapper_t<std::string> programs;
    wf::option_wrapper_t<std::string> not_games;
    wf::option_wrapper_t<int> snap_threshold{"move/snap_threshold"};
    wf::option_wrapper_t<int> quarter_snap_threshold{"move/quarter_snap_threshold"};
    wf::ipc_activator_t slot_keys[10];

    wf::plugin_activation_data_t grab_interface{
        .name = PLUGIN_NAME,
        .capabilities = wf::CAPABILITY_MANAGE_DESKTOP,
    };

    std::shared_ptr<wf::preview_indication_t> preview;
    std::optional<wf::geometry_t> preview_box;

    bool is_game(wayfire_toplevel_view view)
    {
        const std::string app_id = view->get_app_id();
        if (game_window::matches_any(not_games, app_id))
        {
            return false;
        }

        return game_window::matches_any(programs, app_id) ||
               game_window::started_as_game(game_window::client_pid(view));
    }

    int gap_for(wayfire_toplevel_view view)
    {
        const int room = std::max(0, int(gap));
        if (!room || game_window::matches_any(full_size_apps, view->get_app_id()) || (games_fill && is_game(view)))
        {
            return 0;
        }

        return room;
    }

    // true when the window's room changed
    bool mark(wayfire_toplevel_view view)
    {
        const int was = wf::gap_of(view);
        const int now = gap_for(view);
        if (now == was)
        {
            return false;
        }

        LOGI("[window-gaps] mark view=", view->get_id(), " app_id=", view->get_app_id(), " gap=", now, " was=", was);
        if (now)
        {
            view->get_data_safe<wf::gapped_view_t>()->px = now;
        }
        else
        {
            view->erase_data<wf::gapped_view_t>();
        }

        return true;
    }

    static void tell_changed()
    {
        wf::window_gaps_changed_signal changed;
        wf::get_core().emit(&changed);
    }

    static bool may_place(wayfire_toplevel_view view)
    {
        const uint32_t needed = wf::VIEW_ALLOW_MOVE | wf::VIEW_ALLOW_RESIZE;
        return view->get_output() && ((view->get_allowed_actions() & needed) == needed) &&
               view->toplevel()->pending().mapped && wf::find_topmost_parent(view)->get_wset();
    }

    // grid reuses the animation it finds on a window and makes its own the same way
    static nonstd::observer_ptr<wf::grid::grid_animation_t> animation(wayfire_toplevel_view view)
    {
        if (!view->has_data<wf::grid::grid_animation_t>())
        {
            wf::option_wrapper_t<std::string> kind{"grid/type"};
            wf::option_wrapper_t<wf::animation_description_t> duration{"grid/duration"};
            auto type = wf::grid::grid_animation_t::NONE;
            if (kind.value() == "crossfade")
            {
                type = wf::grid::grid_animation_t::CROSSFADE;
            }
            else if (kind.value() == "wobbly")
            {
                type = wf::grid::grid_animation_t::WOBBLY;
            }

            view->store_data(std::make_unique<wf::grid::grid_animation_t>(view, type, duration));
        }

        return view->get_data<wf::grid::grid_animation_t>();
    }

    static box_t slot_box(wf::output_t* output, int slot, int room)
    {
        const box_t area = to_box(output->workarea->get_workarea());
        const box_t plain_box = to_box(wf::grid::get_slot_dimensions(output, slot));
        const box_t box = gapped(plain_box, area, room);
        // a gap wider than the share would leave a box the framework cannot draw
        return (box.width > 0 && box.height > 0) ? box : plain_box;
    }

    // how far the workspace a tiled window is on lies from the one shown
    static wf::point_t workspace_shift(wayfire_toplevel_view view, wf::dimensions_t screen)
    {
        const auto now = view->get_pending_geometry();
        return {int(std::floor((now.x + now.width / 2.0) / screen.width)),
            int(std::floor((now.y + now.height / 2.0) / screen.height))};
    }

    static void put(wayfire_toplevel_view view, int slot, wf::point_t workspaces_away)
    {
        wf::output_t* output = view->get_output();
        const auto screen = output->get_relative_geometry();
        auto placed = view->get_data_safe<placed_slot_t>();
        placed->slot = slot;
        placed->screen = wf::dimensions(screen);

        wf::geometry_t box = from_box(slot_box(output, slot, wf::gap_of(view)));
        box.x += workspaces_away.x * screen.width;
        box.y += workspaces_away.y * screen.height;
        LOGI("[window-gaps] put view=", view->get_id(), " slot=", slot, " gap=", wf::gap_of(view), " box=", box);
        animation(view)->adjust_target_geometry(box, wf::grid::get_tiled_edges_for_slot(slot));
    }

    // a tile placed by anyone takes its new room, from the plain box under the old one
    static void regap(wayfire_toplevel_view view, int old_room)
    {
        if (!view->is_mapped() || view->pending_fullscreen() || !view->pending_tiled_edges() || !view->get_output())
        {
            return;
        }

        wf::output_t* output = view->get_output();
        const auto screen = output->get_relative_geometry();
        if (auto placed = view->get_data<placed_slot_t>())
        {
            put(view, placed->slot, workspace_shift(view, placed->screen));
            return;
        }

        const wf::point_t away = workspace_shift(view, wf::dimensions(screen));
        const box_t area = to_box(output->workarea->get_workarea());
        wf::geometry_t now = view->get_pending_geometry();
        now.x -= away.x * screen.width;
        now.y -= away.y * screen.height;
        box_t box = gapped(plain(to_box(now), area, old_room), area, wf::gap_of(view));
        if (box.width <= 0 || box.height <= 0)
        {
            return;
        }

        box.x += away.x * screen.width;
        box.y += away.y * screen.height;
        LOGI("[window-gaps] regap view=", view->get_id(), " gap=", wf::gap_of(view), " was=", old_room);
        animation(view)->adjust_target_geometry(from_box(box), view->pending_tiled_edges());
    }

    void mark_all()
    {
        bool changed = store_room();
        for (auto& view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (!toplevel || !toplevel->is_mapped())
            {
                continue;
            }

            const int old_room = wf::gap_of(toplevel);
            if (mark(toplevel))
            {
                changed = true;
                regap(toplevel, old_room);
            }
        }

        if (changed)
        {
            tell_changed();
        }
    }

    // true when the room changed
    bool store_room()
    {
        auto gaps = wf::get_core().get_data_safe<wf::window_gaps_t>();
        const int was = gaps->px;
        gaps->px = std::max(0, int(gap));
        return gaps->px != was;
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_mapped = [this](wf::view_mapped_signal* event) {
        auto view = wf::toplevel_cast(event->view);
        const int old_room = view ? wf::gap_of(view) : 0;
        if (view && mark(view))
        {
            regap(view, old_room);
            tell_changed();
        }
    };

    wf::signal::connection_t<wf::view_app_id_changed_signal> on_app_id = [this](wf::view_app_id_changed_signal* event) {
        auto view = wf::toplevel_cast(event->view);
        const int old_room = view ? wf::gap_of(view) : 0;
        if (view && view->is_mapped() && mark(view))
        {
            regap(view, old_room);
            tell_changed();
        }
    };

    // runs after the plugins that keep zones and fit tiles beside others, before grid
    wf::signal::connection_t<wf::view_tile_request_signal> on_tile_request = [this](wf::view_tile_request_signal* event) {
        auto view = event->view;
        const int slot = int(wf::grid::get_slot_from_tiled_edges(event->edges));
        if (event->carried_out || !slot || (event->desired_size.width <= 0) || !may_place(view))
        {
            return;
        }

        event->carried_out = true;
        if (mark(view))
        {
            tell_changed();
        }

        put(view, slot, event->workspace - view->get_output()->wset()->get_current_workspace());
    };

    wf::signal::connection_t<wf::view_tiled_signal> on_tiled = [](wf::view_tiled_signal* event) {
        auto placed = event->view->get_data<placed_slot_t>();
        if (placed && (event->new_edges != wf::grid::get_tiled_edges_for_slot(placed->slot)))
        {
            event->view->erase_data<placed_slot_t>();
        }
    };

    wf::signal::connection_t<wf::view_fullscreen_request_signal> on_fullscreen_request =
        [](wf::view_fullscreen_request_signal* event) {
        auto placed = event->view->get_data<placed_slot_t>();
        if (placed && !event->state && !event->carried_out && may_place(event->view))
        {
            event->carried_out = true;
            put(event->view, placed->slot, event->workspace - event->view->get_output()->wset()->get_current_workspace());
        }
    };

    wf::signal::connection_t<wf::workarea_changed_signal> on_workarea_changed = [](wf::workarea_changed_signal* event) {
        for (auto& view : event->output->wset()->get_views(wf::WSET_MAPPED_ONLY))
        {
            auto placed = view->get_data<placed_slot_t>();
            if (placed && !view->pending_fullscreen())
            {
                put(view, placed->slot, workspace_shift(view, placed->screen));
            }
        }
    };

    static wayfire_toplevel_view find_dragged()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            auto toplevel = wf::toplevel_cast(view);
            if (toplevel && toplevel->is_mapped() && toplevel->get_transformed_node()->get_transformer(DRAGGED_MARK))
            {
                return toplevel;
            }
        }

        return nullptr;
    }

    void hide_preview(wf::point_t at)
    {
        if (preview)
        {
            preview->set_target_geometry(wf::pointf_t{double(at.x), double(at.y)}, 0, true);
            preview = nullptr;
        }

        preview_box.reset();
    }

    // a drop becomes a tile request and grid only clears its preview
    wf::signal::connection_t<wf::grid::grid_handle_move_signal> on_grid_move =
        [this](wf::grid::grid_handle_move_signal* event) {
        const auto slot =
            (event->operation == wf::grid::MOVE_OP_CLEAR_PREVIEW)
                ? wf::grid::SLOT_NONE
                : wf::grid::calc_slot(event->output, event->input, snap_threshold, quarter_snap_threshold);
        if (slot == wf::grid::SLOT_NONE)
        {
            hide_preview(event->input);
            return;
        }

        const bool drop = event->operation == wf::grid::MOVE_OP_DROP;
        event->operation = wf::grid::MOVE_OP_CLEAR_PREVIEW;
        if (drop)
        {
            hide_preview(event->input);
            wf::get_core().default_wm->tile_request(event->view, wf::grid::get_tiled_edges_for_slot(slot));
            return;
        }

        auto dragged = find_dragged();
        const int room = dragged ? gap_for(dragged) : std::max(0, int(gap));
        const wf::geometry_t box = from_box(slot_box(event->output, slot, room));
        if (!preview || (preview_box != box))
        {
            hide_preview(event->input);
            preview = std::make_shared<wf::preview_indication_t>(
                wf::pointf_t{double(event->input.x), double(event->input.y)}, event->output, "move");
            preview->set_target_geometry(box, 1);
            preview_box = box;
        }
    };

  public:
    void init() override
    {
        auto& core = wf::get_core();
        // the file reload made untyped options if our xml came after the compositor started
        if (!std::dynamic_pointer_cast<wf::config::option_t<int>>(core.config->get_option(PLUGIN_NAME + "/gap")))
        {
            core.config_backend->reload_config_metadata(*core.config);
        }

        gap.load_option(PLUGIN_NAME + "/gap");
        games_fill.load_option(PLUGIN_NAME + "/games_fill");
        full_size_apps.load_option(PLUGIN_NAME + "/full_size_apps");
        programs.load_option(PLUGIN_NAME + "/programs");
        not_games.load_option(PLUGIN_NAME + "/not_games");
        for (auto option : {&full_size_apps, &programs, &not_games})
        {
            option->set_callback([this] { mark_all(); });
        }

        gap.set_callback([this] { mark_all(); });
        games_fill.set_callback([this] { mark_all(); });
        for (int slot = 1; slot < 10; slot++)
        {
            slot_keys[slot].load_from_xml_option(PLUGIN_NAME + "/slot_" + SLOT_NAMES[slot]);
            slot_keys[slot].set_handler([this, slot](wf::output_t* output, wayfire_view view) {
                auto toplevel = wf::toplevel_cast(view);
                if (!output->can_activate_plugin(&grab_interface) || !toplevel)
                {
                    return false;
                }

                wf::get_core().default_wm->tile_request(toplevel, wf::grid::get_tiled_edges_for_slot(slot));
                return true;
            });
        }

        wf::get_core().connect(&on_mapped);
        wf::get_core().connect(&on_app_id);
        wf::get_core().connect(&on_grid_move);
        init_output_tracking();
        mark_all();
    }

    void fini() override
    {
        hide_preview({0, 0});
        fini_output_tracking();
        on_mapped.disconnect();
        on_app_id.disconnect();
        on_grid_move.disconnect();
        for (auto& view : wf::get_core().get_all_views())
        {
            view->erase_data<wf::gapped_view_t>();
            view->erase_data<placed_slot_t>();
        }

        wf::get_core().erase_data<wf::window_gaps_t>();
        tell_changed();
    }

    int get_order_hint() const override { return -1; }

    void handle_new_output(wf::output_t* output) override
    {
        output->connect(&on_tile_request);
        output->connect(&on_tiled);
        output->connect(&on_fullscreen_request);
        output->connect(&on_workarea_changed);
    }

    void handle_output_removed(wf::output_t*) override {}
};
} // namespace window_gaps

DECLARE_WAYFIRE_PLUGIN(window_gaps::plugin_t);
