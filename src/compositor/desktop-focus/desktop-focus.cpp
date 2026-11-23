// The plugin "desktop-focus": a click on the desktop leaves no window
// active, so its title bar greys and the menu bar shows the desktop's menus.
//
// Wayfire keeps the last window active while a shell surface has the
// keyboard ("workarounds/keep_last_toplevel_activated"), which the menu bar's
// menus and the dock need. The desktop is the one surface that should not:
// when a surface in the background layer takes the keyboard, the active
// window is let go. The keyboard stays with the desktop, which holds it.

#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/view.hpp>

#include <linux/input-event-codes.h>

#include <memory>

namespace desktop_focus
{
static bool on_desktop(wayfire_view view)
{
    return view && (wf::get_view_layer(view) == wf::scene::layer::BACKGROUND);
}

class plugin_t : public wf::plugin_interface_t
{
    // the desktop while it is the front program, as finder stays in front after its own menus close
    std::weak_ptr<wf::view_interface_t> front_desktop;
    wf::wl_idle_call back_to_desktop;

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_focus_changed =
        [this](wf::keyboard_focus_changed_signal* event) {
        wayfire_view view = wf::node_to_view(event->new_focus);
        if (on_desktop(view))
        {
            front_desktop = view->weak_from_this();
            // clears the active window only, the keyboard stays where it is
            wf::get_core().seat->focus_view(nullptr);
            return;
        }

        if (!wf::toplevel_cast(view))
        {
            return;
        }

        // a shell overlay that closes hands the keyboard to the last window, not to the desktop in front
        auto desktop = front_desktop.lock();
        if ((event->reason == wf::keyboard_focus_reason::REFOCUS) && desktop && desktop->is_mapped())
        {
            back_to_desktop.run_once([this] {
                if (auto still = front_desktop.lock(); still && still->is_mapped())
                {
                    wf::get_core().seat->focus_view(wayfire_view{still.get()});
                }
            });
            return;
        }

        front_desktop.reset();
    };

    // any button on the desktop makes it the front program, not only the left one wayfire focuses on
    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_button_event>> on_button =
        [](wf::post_input_event_signal<wlr_pointer_button_event>* event) {
        if ((event->event->state != WL_POINTER_BUTTON_STATE_PRESSED) || (event->event->button == BTN_LEFT))
        {
            return;
        }

        wayfire_view view = wf::get_core().get_cursor_focus_view();
        if (on_desktop(view))
        {
            wf::get_core().seat->focus_view(view);
        }
    };

  public:
    void init() override
    {
        wf::get_core().connect(&on_focus_changed);
        wf::get_core().connect(&on_button);
    }

    void fini() override
    {
        on_focus_changed.disconnect();
        on_button.disconnect();
    }
};
} // namespace desktop_focus

DECLARE_WAYFIRE_PLUGIN(desktop_focus::plugin_t);
