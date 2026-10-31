// The plugin "focus-follows-mouse": a window takes the keyboard a moment after the pointer enters it.

#include <wayfire/core.hpp>
#include <wayfire/config-backend.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/view.hpp>

namespace focus_follows_mouse
{
static bool takes_focus(wayfire_view view)
{
    return view && (view->role == wf::VIEW_ROLE_TOPLEVEL);
}

// A shell surface above the windows holds the keyboard while a menu is open.
static bool shell_holds_keyboard()
{
    wayfire_view holder = wf::node_to_view(wf::get_core().seat->get_active_node());
    return holder && (wf::get_view_layer(holder) > wf::scene::layer::WORKSPACE);
}

class plugin_t : public wf::plugin_interface_t, public wf::per_output_tracker_mixin_t<>
{
    wf::option_wrapper_t<int> delay_ms;
    wf::option_wrapper_t<bool> raise;
    wf::wl_timer<false> delay;
    wf::wl_idle_call after_disappearing;
    wayfire_view hovered = nullptr;

    void focus(wayfire_view view)
    {
        if (shell_holds_keyboard() || (view == wf::get_core().seat->get_active_view()))
        {
            return;
        }
        // only here, an active output with no window would drop the keyboard
        wf::get_core().seat->focus_output(view->get_output());
        wf::get_core().seat->focus_view(view);
        if (raise)
        {
            wf::view_bring_to_front(view);
        }
    }

    // no change inside one window, so a window picked by keyboard keeps focus
    void pointer_moved()
    {
        wayfire_view view = wf::get_core().get_cursor_focus_view();
        if (view == hovered)
        {
            return;
        }
        hovered = view;
        delay.disconnect();
        if (!takes_focus(view))
        {
            return;
        }
        if (delay_ms <= 0)
        {
            focus(view);
            return;
        }
        delay.set_timeout(delay_ms, [this] { focus(hovered); });
    }

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>> on_motion = [this](auto*) {
        pointer_moved();
    };
    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_absolute_event>> on_motion_absolute =
        [this](auto*) { pointer_moved(); };

    // left alone the compositor picks the last used window, not the one under the pointer
    wf::signal::connection_t<wf::view_disappeared_signal> on_view_disappeared =
        [this](wf::view_disappeared_signal* event) {
        const bool was_active = (event->view == wf::get_core().seat->get_active_view());
        if (event->view == hovered)
        {
            hovered = nullptr;
            delay.disconnect();
        }
        if (was_active)
        {
            after_disappearing.run_once([this] {
                const wf::pointf_t pointer = wf::get_core().get_cursor_position();
                wayfire_view view = wf::get_core().get_view_at(pointer);
                hovered = view;
                if (takes_focus(view))
                {
                    focus(view);
                }
            });
        }
    };

  public:
    void init() override
    {
        auto& core = wf::get_core();
        // loaded into a running session, the compositor has not read our xml
        if (!core.config->get_option("focus-follows-mouse/delay_ms"))
        {
            core.config_backend->reload_config_metadata(*core.config);
        }
        delay_ms.load_option("focus-follows-mouse/delay_ms");
        raise.load_option("focus-follows-mouse/raise");
        core.connect(&on_motion);
        core.connect(&on_motion_absolute);
        init_output_tracking();
    }

    void fini() override
    {
        fini_output_tracking();
        delay.disconnect();
    }

    void handle_new_output(wf::output_t* output) override { output->connect(&on_view_disappeared); }

    void handle_output_removed(wf::output_t*) override {}
};
} // namespace focus_follows_mouse

DECLARE_WAYFIRE_PLUGIN(focus_follows_mouse::plugin_t);
