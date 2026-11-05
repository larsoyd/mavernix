// dragging the edge two tiled windows share resizes both, a lone tile keeps its tiling

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include <wayfire/bindings-repository.hpp>
#include <wayfire/core.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/plugins/common/preview-indication.hpp>
#include <wayfire/plugins/crossfade.hpp>
#include <wayfire/plugins/grid.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/toplevel.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/txn/transaction.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>
#include <wlr/util/edges.h>

#include "seam-math.hpp"

namespace
{
const std::string PLUGIN_NAME = "shared-edges";

static_assert(
    uint32_t(shared_edges::EDGE_LEFT) == WLR_EDGE_LEFT && uint32_t(shared_edges::EDGE_RIGHT) == WLR_EDGE_RIGHT &&
    uint32_t(shared_edges::EDGE_TOP) == WLR_EDGE_TOP && uint32_t(shared_edges::EDGE_BOTTOM) == WLR_EDGE_BOTTOM);

// grid reuses the animation it finds, a crossfade would leave a gap beside the neighbour
void skip_crossfade(wayfire_toplevel_view view)
{
    if (view->get_output() && !view->has_data<wf::grid::grid_animation_t>())
    {
        wf::option_wrapper_t<wf::animation_description_t> duration{"grid/duration"};
        view->store_data(std::make_unique<wf::grid::grid_animation_t>(
            view, wf::grid::grid_animation_t::NONE, duration));
    }
}

// The stock window manager, but asking this plugin first about resizes.
class shared_edge_window_manager_t : public wf::window_manager_t
{
  public:
    std::function<bool(wayfire_toplevel_view, uint32_t)> take_over_resize;

    void tile_request(wayfire_toplevel_view view, uint32_t edges, std::optional<wf::point_t> ws = {}) override
    {
        if (edges != wf::TILED_EDGES_ALL && edges != 0)
        {
            skip_crossfade(view);
        }

        wf::window_manager_t::tile_request(view, edges, ws);
    }

    void resize_request(wayfire_toplevel_view view, uint32_t edges = 0) override
    {
        if (take_over_resize && take_over_resize(view, edges))
        {
            return;
        }

        wf::window_manager_t::resize_request(view, edges);
    }
};

bool is_tiled_and_resizable(wayfire_toplevel_view view)
{
    return view->is_mapped() && !view->minimized && !view->pending_fullscreen() &&
           (view->role == wf::VIEW_ROLE_TOPLEVEL) && view->pending_tiled_edges() &&
           (view->get_allowed_actions() & wf::VIEW_ALLOW_RESIZE);
}

// the seam pointer, the four way one where two seams cross
std::string seam_cursor(bool upright, bool across)
{
    if (upright && across)
    {
        return "move";
    }

    return upright ? "col-resize" : "row-resize";
}

shared_edges::box_t to_box(wf::geometry_t geometry)
{
    return {int(geometry.x), int(geometry.y), int(geometry.width), int(geometry.height)};
}

wf::geometry_t from_box(const shared_edges::box_t& box)
{
    return wf::geometry_t{double(box.x), double(box.y), double(box.width), double(box.height)};
}

shared_edges::window_t describe(wayfire_toplevel_view view)
{
    // Size limits are of the contents; add the frame as the stock plugin does.
    const auto& margins = view->toplevel()->pending().margins;
    const auto frame_width = static_cast<int>(margins.left + margins.right);
    const auto frame_height = static_cast<int>(margins.top + margins.bottom);
    const wf::dimensions_t least = view->toplevel()->get_min_size();
    const wf::dimensions_t largest = view->toplevel()->get_max_size();

    const auto geometry = view->toplevel()->pending().geometry;
    shared_edges::window_t window;
    window.box = to_box(geometry);
    window.min_width = std::max(1, least.width) + frame_width;
    window.min_height = std::max(1, least.height) + frame_height;
    window.max_width = largest.width > 0 ? largest.width + frame_width : 0;
    window.max_height = largest.height > 0 ? largest.height + frame_height : 0;
    return window;
}

// clients drawing their own frame may have no resize edge once tiled, so this takes seams
class seam_strip_node_t : public wf::scene::node_t, public wf::pointer_interaction_t
{
  public:
    struct hit_t
    {
        wayfire_toplevel_view view;
        uint32_t edges;
    };

    std::function<std::optional<hit_t>(wf::output_t*, wf::pointf_t)> seam_at;
    std::function<void(const hit_t&)> pressed;

    explicit seam_strip_node_t(wf::output_t* output) : node_t(false), output(output) {}

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        if (seam_at(output, at))
        {
            return wf::scene::input_node_t{.node = this, .local_coords = at};
        }

        return {};
    }

    wf::pointer_interaction_t& pointer_interaction() override { return *this; }
    std::string stringify() const override { return "shared-edges strip"; }

    void handle_pointer_enter(wf::pointf_t at) override { show_cursor(at); }
    void handle_pointer_motion(wf::pointf_t at, uint32_t) override { show_cursor(at); }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        const auto hit = seam_at(output, wf::get_core().get_cursor_position());
        if (hit && (event.state == WL_POINTER_BUTTON_STATE_PRESSED) && (event.button == BTN_LEFT))
        {
            pressed(*hit);
        }
    }

  private:
    wf::output_t* output;

    void show_cursor(wf::pointf_t at)
    {
        if (const auto hit = seam_at(output, at))
        {
            wf::get_core().set_cursor(
                seam_cursor(hit->edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT), hit->edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)));
        }
    }
};

class shared_edges_t : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<>,
    public wf::pointer_interaction_t
{
    wf::option_wrapper_t<bool> enabled{PLUGIN_NAME + "/enabled"};
    wf::option_wrapper_t<int> tolerance_px{PLUGIN_NAME + "/tolerance"};
    wf::option_wrapper_t<int> least_size_px{PLUGIN_NAME + "/min_size"};
    wf::option_wrapper_t<int> grip_px{PLUGIN_NAME + "/grip"};
    wf::option_wrapper_t<int> snap_threshold{"move/snap_threshold"};
    wf::option_wrapper_t<int> quarter_snap_threshold{"move/quarter_snap_threshold"};
    wf::option_wrapper_t<wf::buttonbinding_t> held_resize{"resize/activate"};

    std::map<wf::output_t*, std::shared_ptr<seam_strip_node_t>> strips;

    wf::plugin_activation_data_t grab_interface = {
        .name = PLUGIN_NAME,
        .capabilities = wf::CAPABILITY_GRAB_INPUT | wf::CAPABILITY_MANAGE_DESKTOP,
    };

    // moves count from where the windows started so nothing drifts, a corner holds two seams
    wf::output_t* output = nullptr;
    std::unique_ptr<wf::input_grab_t> input_grab;
    std::vector<wayfire_toplevel_view> members;
    std::vector<shared_edges::window_t> starting;
    std::vector<shared_edges::seam_t> seams;
    std::vector<int> last_moves_px;
    wf::pointf_t pointer_at_start;
    uint32_t release_button = BTN_LEFT;

    bool dragging() const { return output != nullptr; }

    wf::signal::connection_t<wf::view_disappeared_signal> on_view_disappeared =
        [=](wf::view_disappeared_signal* event) {
        for (auto& member : members)
        {
            if (member == event->view)
            {
                end_drag();
                return;
            }
        }
    };

    std::shared_ptr<wf::preview_indication_t> preview;
    std::optional<wf::geometry_t> preview_box;

    // a slot beside other tiles, sized to meet them, or nothing if grid's own slot fits
    std::optional<wf::geometry_t> fitted_slot(wf::output_t* on, uint32_t slot)
    {
        if (!enabled || (slot == wf::grid::SLOT_NONE) || (slot == wf::grid::SLOT_CENTER))
        {
            return {};
        }

        std::vector<shared_edges::tile_t> tiles;
        const uint32_t which = wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_CURRENT_WORKSPACE;
        for (auto& other : on->wset()->get_views(which))
        {
            tiles.push_back({to_box(other->toplevel()->pending().geometry), other->pending_tiled_edges()});
        }

        const auto plain = to_box(wf::grid::get_slot_dimensions(on, slot));
        const auto fitted =
            shared_edges::fit_beside(plain, wf::grid::get_tiled_edges_for_slot(slot), tiles, least_size_px);
        if ((fitted.x == plain.x) && (fitted.y == plain.y) && (fitted.width == plain.width) &&
            (fitted.height == plain.height))
        {
            return {};
        }

        return from_box(fitted);
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

    // runs before grid, which keeps the plain slot for any request it gets first
    wf::signal::connection_t<wf::view_tile_request_signal> on_tile_request = [=](wf::view_tile_request_signal* event) {
        auto view = event->view;
        wf::output_t* on = view->get_output();
        const uint32_t needed = wf::VIEW_ALLOW_MOVE | wf::VIEW_ALLOW_RESIZE;
        if (event->carried_out || !on || ((view->get_allowed_actions() & needed) != needed) ||
            (event->workspace != on->wset()->get_current_workspace()))
        {
            return;
        }

        if (const auto box = fitted_slot(on, wf::grid::get_slot_from_tiled_edges(event->edges)))
        {
            event->carried_out = true;
            skip_crossfade(view);
            view->get_data<wf::grid::grid_animation_t>()->adjust_target_geometry(*box, event->edges);
        }
    };

    // a fitted drop becomes a tile request and grid only clears its own preview
    wf::signal::connection_t<wf::grid::grid_handle_move_signal> on_grid_move =
        [=](wf::grid::grid_handle_move_signal* event) {
        const auto slot =
            (event->operation == wf::grid::MOVE_OP_CLEAR_PREVIEW)
                ? wf::grid::SLOT_NONE
                : wf::grid::calc_slot(event->output, event->input, snap_threshold, quarter_snap_threshold);
        const bool drop = event->operation == wf::grid::MOVE_OP_DROP;
        const auto box = fitted_slot(event->output, slot);
        if (!box)
        {
            hide_preview(event->input);
            if (drop && (slot != wf::grid::SLOT_NONE) && (slot != wf::grid::SLOT_CENTER))
            {
                skip_crossfade(event->view);
            }

            return;
        }

        event->operation = wf::grid::MOVE_OP_CLEAR_PREVIEW;
        if (drop)
        {
            hide_preview(event->input);
            wf::get_core().default_wm->tile_request(event->view, wf::grid::get_tiled_edges_for_slot(slot));
            return;
        }

        if (!preview || (preview_box != box))
        {
            hide_preview(event->input);
            preview = std::make_shared<wf::preview_indication_t>(
                wf::pointf_t{double(event->input.x), double(event->input.y)}, event->output, "move");
            preview->set_target_geometry(*box, 1);
            preview_box = box;
        }
    };

    void tiled_windows(wayfire_toplevel_view view, std::vector<wayfire_toplevel_view>& candidates,
        std::vector<shared_edges::window_t>& windows, std::size_t& dragged)
    {
        const uint32_t which = wf::WSET_MAPPED_ONLY | wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_CURRENT_WORKSPACE;
        for (auto& other : view->get_output()->wset()->get_views(which))
        {
            if (other == view)
            {
                dragged = candidates.size();
            }
            else if (!is_tiled_and_resizable(other))
            {
                continue;
            }

            candidates.push_back(other);
            windows.push_back(describe(other));
        }
    }

    bool take_over(wayfire_toplevel_view view, uint32_t edges, uint32_t button = BTN_LEFT)
    {
        if (!enabled || dragging() || !view->get_output() || !is_tiled_and_resizable(view))
        {
            return false;
        }

        wf::output_t* view_output = view->get_output();
        std::vector<wayfire_toplevel_view> candidates;
        std::vector<shared_edges::window_t> windows;
        std::size_t dragged = 0;
        tiled_windows(view, candidates, windows, dragged);

        std::vector<shared_edges::seam_t> found;
        for (const auto& dragged_side : shared_edges::dragged_sides(edges, view->pending_tiled_edges()))
        {
            if (auto seam =
                    shared_edges::find_seam(windows, dragged, dragged_side.side, tolerance_px, dragged_side.free))
            {
                found.push_back(*seam);
            }
        }

        if (found.empty() || !view_output->activate_plugin(&grab_interface))
        {
            return false;
        }

        // keep only the seams' windows and renumber to match
        output = view_output;
        std::map<std::size_t, std::size_t> renumbered;
        const auto member = [&](std::size_t old_index) {
            auto [at, added] = renumbered.try_emplace(old_index, members.size());
            if (added)
            {
                members.push_back(candidates[old_index]);
                starting.push_back(windows[old_index]);
            }

            return at->second;
        };

        for (const auto& seam : found)
        {
            shared_edges::seam_t kept{seam.vertical, {}, {}};
            for (std::size_t old_index : seam.before)
            {
                kept.before.push_back(member(old_index));
            }

            for (std::size_t old_index : seam.after)
            {
                kept.after.push_back(member(old_index));
            }

            seams.push_back(kept);
        }

        pointer_at_start = wf::get_core().get_cursor_position();
        last_moves_px.assign(seams.size(), 0);
        release_button = button;
        output->connect(&on_view_disappeared);

        input_grab = std::make_unique<wf::input_grab_t>(PLUGIN_NAME, output, nullptr, this, nullptr);
        input_grab->set_wants_raw_input(true);
        input_grab->grab_input(wf::scene::layer::OVERLAY);
        grab_interface.cancel = [=]() { end_drag(); };

        const auto upright = [](const shared_edges::seam_t& seam) { return seam.vertical; };
        wf::get_core().set_cursor(seam_cursor(std::any_of(seams.begin(), seams.end(), upright),
            !std::all_of(seams.begin(), seams.end(), upright)));
        return true;
    }

    // the resize binding held over a tiled window drags its free edges with their neighbours
    wf::button_callback on_held_resize = [=](const wf::buttonbinding_t& pressed) {
        auto view = wf::toplevel_cast(wf::get_core().get_cursor_focus_view());
        if (!view || !view->get_output() || !is_tiled_and_resizable(view))
        {
            return false;
        }

        const auto origin = wf::origin(view->get_output()->get_layout_geometry());
        const auto at = wf::get_core().get_cursor_position();
        const uint32_t edges = shared_edges::held_edges(to_box(view->toplevel()->pending().geometry),
            view->pending_tiled_edges(), int(at.x - origin.x), int(at.y - origin.y));
        return take_over(view, edges, pressed.get_button());
    };

    // asks the workspace so a popup or window above the seam keeps its clicks
    std::optional<seam_strip_node_t::hit_t> seam_at(wf::output_t* on, wf::pointf_t at)
    {
        if (!enabled)
        {
            return {};
        }

        const auto under = on->wset()->get_node()->find_node_at(at);
        wayfire_toplevel_view view = under ? wf::toplevel_cast(wf::node_to_view(under->node.get())) : nullptr;
        if (!view || !is_tiled_and_resizable(view))
        {
            return {};
        }

        const auto origin = wf::origin(on->get_layout_geometry());
        const wf::pointf_t local = {at.x - origin.x, at.y - origin.y};
        const auto box = view->toplevel()->pending().geometry;
        const std::pair<uint32_t, double> distances[] = {
            {WLR_EDGE_RIGHT, box.x + box.width - local.x},
            {WLR_EDGE_LEFT, local.x - box.x},
            {WLR_EDGE_BOTTOM, box.y + box.height - local.y},
            {WLR_EDGE_TOP, local.y - box.y},
        };

        std::vector<wayfire_toplevel_view> candidates;
        std::vector<shared_edges::window_t> windows;
        std::size_t dragged = 0;
        tiled_windows(view, candidates, windows, dragged);
        uint32_t edges = 0;
        for (const auto& [edge, distance] : distances)
        {
            const auto side = shared_edges::dragged_sides(edge, 0).front().side;
            // past the box is the window's own resize margin, and a press there drags the seam too
            if ((distance < grip_px) && shared_edges::find_seam(windows, dragged, side, tolerance_px))
            {
                edges |= edge;
            }
        }

        return edges ? std::optional{seam_strip_node_t::hit_t{view, edges}} : std::nullopt;
    }

    void end_drag()
    {
        if (!dragging())
        {
            return;
        }

        on_view_disappeared.disconnect();
        input_grab->ungrab_input();
        input_grab.reset();
        output->deactivate_plugin(&grab_interface);
        output = nullptr;
        members.clear();
        starting.clear();
        seams.clear();
    }

    void follow_pointer()
    {
        const wf::pointf_t pointer = wf::get_core().get_cursor_position();
        const auto area = to_box(output->workarea->get_workarea());
        std::vector<int> moves_px;
        for (const auto& seam : seams)
        {
            const double travelled = seam.vertical ? pointer.x - pointer_at_start.x : pointer.y - pointer_at_start.y;
            moves_px.push_back(shared_edges::limit_move(starting, seam, int(travelled), least_size_px, area));
        }

        if (moves_px == last_moves_px)
        {
            return;
        }

        last_moves_px = moves_px;
        auto transaction = wf::txn::transaction_t::create();
        for (std::size_t index = 0; index < members.size(); index++)
        {
            // gravity is the edges that stay put if the client picks another size
            shared_edges::window_t moved = starting[index];
            uint32_t gravity = WLR_EDGE_LEFT | WLR_EDGE_TOP;
            for (std::size_t which = 0; which < seams.size(); which++)
            {
                const auto& seam = seams[which];
                moved.box = shared_edges::moved_box(moved, seam, index, moves_px[which]);
                const bool is_after = std::find(seam.after.begin(), seam.after.end(), index) != seam.after.end();
                if (is_after)
                {
                    gravity = seam.vertical ? (gravity & ~WLR_EDGE_LEFT) | WLR_EDGE_RIGHT
                                            : (gravity & ~WLR_EDGE_TOP) | WLR_EDGE_BOTTOM;
                }
            }

            auto& pending = members[index]->toplevel()->pending();
            pending.geometry = from_box(moved.box);
            pending.gravity = gravity;
            transaction->add_object(members[index]->toplevel());
        }

        wf::get_core().tx_manager->schedule_transaction(std::move(transaction));
    }

  public:
    void init() override
    {
        auto manager = std::make_unique<shared_edge_window_manager_t>();
        manager->take_over_resize = [=](wayfire_toplevel_view view, uint32_t edges) { return take_over(view, edges); };
        wf::get_core().default_wm = std::move(manager);
        wf::get_core().connect(&on_grid_move);
        wf::get_core().bindings->add_button(held_resize, &on_held_resize);
        init_output_tracking();
    }

    void fini() override
    {
        end_drag();
        hide_preview({0, 0});
        wf::get_core().bindings->rem_binding(&on_held_resize);
        fini_output_tracking();
        wf::get_core().default_wm = std::make_unique<wf::window_manager_t>();
    }

    // runs before grid, which has to find the animation on a drop
    int get_order_hint() const override { return -1; }

    void handle_new_output(wf::output_t* new_output) override
    {
        new_output->connect(&on_tile_request);
        auto strip = std::make_shared<seam_strip_node_t>(new_output);
        strip->seam_at = [=](wf::output_t* on, wf::pointf_t at) { return seam_at(on, at); };
        strip->pressed = [=](const seam_strip_node_t::hit_t& hit) { take_over(hit.view, hit.edges); };
        wf::scene::add_front(new_output->node_for_layer(wf::scene::layer::WORKSPACE), strip);
        strips[new_output] = strip;
    }

    void handle_output_removed(wf::output_t* old_output) override
    {
        wf::scene::remove_child(strips[old_output]);
        strips.erase(old_output);
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if ((event.state == WL_POINTER_BUTTON_STATE_RELEASED) && (event.button == release_button))
        {
            end_drag();
        }
    }

    void handle_pointer_motion(wf::pointf_t, uint32_t) override
    {
        if (dragging())
        {
            follow_pointer();
        }
    }
};
} // namespace

DECLARE_WAYFIRE_PLUGIN(shared_edges_t);
