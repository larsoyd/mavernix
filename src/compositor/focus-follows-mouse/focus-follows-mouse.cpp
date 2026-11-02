// The plugin "focus-follows-mouse": a window takes the keyboard a moment after the pointer enters it.

#include <algorithm>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/config-backend.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/view.hpp>

namespace focus_follows_mouse
{
static bool takes_focus(wayfire_view view)
{
    return view && (view->role == wf::VIEW_ROLE_TOPLEVEL);
}

static bool is_desktop(wayfire_view view)
{
    return view && (wf::get_view_layer(view) == wf::scene::layer::BACKGROUND);
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
    wf::option_wrapper_t<bool> menu_on_click;
    wf::wl_timer<false> delay;
    wf::wl_idle_call after_disappearing;
    wayfire_view hovered = nullptr;

    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> repo;
    std::vector<wf::ipc::client_interface_t*> watchers;
    // null is the desktop
    wayfire_view menu_window = nullptr;
    wayfire_view focused = nullptr;
    bool menu_window_lost = false;
    bool focus_is_passing = false;
    bool settling = false;
    bool refocus_after_settling = false;
    bool shell_has_keyboard = false;
    bool menu_wants_keyboard = false;
    wf::wl_idle_call after_shell;

    wf::json_t menu_message() const
    {
        wf::json_t event;
        event["event"] = "menu-window";
        event["held"] = bool(menu_on_click);
        if (menu_window)
        {
            wf::json_t view;
            view["id"] = menu_window->get_id();
            view["role"] = "toplevel";
            view["app-id"] = menu_window->get_app_id();
            event["view"] = view;
        }
        return event;
    }

    void tell_watchers()
    {
        for (auto client : watchers)
        {
            client->send_json(menu_message());
        }
    }

    void set_menu_window(wayfire_view view)
    {
        if ((view != menu_window) || menu_window_lost)
        {
            menu_window = view;
            menu_window_lost = false;
            tell_watchers();
        }
    }

    void give_menu_window_keyboard()
    {
        if (menu_window)
        {
            focus_is_passing = true;
            wf::get_core().seat->focus_output(menu_window->get_output());
            wf::get_core().seat->focus_view(menu_window);
            focus_is_passing = false;
        }
    }

    // a focus that came back from a shell surface or from a hover is nobody's choice
    void focus_moved(wf::scene::node_ptr node)
    {
        wayfire_view view = wf::node_to_view(node);
        if (node && !view)
        {
            return;
        }
        if (view && !takes_focus(view) && !is_desktop(view))
        {
            shell_has_keyboard = true;
            return;
        }
        const bool from_shell = shell_has_keyboard;
        shell_has_keyboard = false;
        wayfire_view now = takes_focus(view) ? view : nullptr;
        if (from_shell && menu_wants_keyboard)
        {
            menu_wants_keyboard = false;
            after_shell.run_once([this] { give_menu_window_keyboard(); });
        }
        if (now == focused)
        {
            return;
        }
        focused = now;
        if (!focus_is_passing && !settling && !from_shell)
        {
            set_menu_window(now);
        }
    }

    void focus(wayfire_view view)
    {
        if (shell_holds_keyboard() || (view == wf::get_core().seat->get_active_view()))
        {
            return;
        }
        focus_is_passing = true;
        // only here, an active output with no window would drop the keyboard
        wf::get_core().seat->focus_output(view->get_output());
        wf::get_core().seat->focus_view(view);
        focus_is_passing = false;
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

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_button_event>> on_button =
        [this](wf::post_input_event_signal<wlr_pointer_button_event>* event) {
        if (event->event->state != WL_POINTER_BUTTON_STATE_PRESSED)
        {
            return;
        }
        wayfire_view view = wf::get_core().get_cursor_focus_view();
        if (takes_focus(view))
        {
            set_menu_window(view);
        }
    };

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_focus_changed =
        [this](wf::keyboard_focus_changed_signal* event) { focus_moved(event->new_focus); };

    // a dock or a program asking covers a window that already has the keyboard
    wf::signal::connection_t<wf::view_focus_request_signal> on_focus_request =
        [this](wf::view_focus_request_signal* event) {
        if (takes_focus(event->view))
        {
            set_menu_window(event->view);
        }
    };

    // left alone the compositor picks the last used window, not the one under the pointer
    wf::signal::connection_t<wf::view_disappeared_signal> on_view_disappeared =
        [this](wf::view_disappeared_signal* event) {
        const bool was_active = (event->view == wf::get_core().seat->get_active_view());
        // a move to another output disappears too, and such a window is still there
        auto toplevel = wf::toplevel_cast(event->view);
        const bool gone = !event->view->is_mapped() || (toplevel && toplevel->minimized);
        if (event->view == hovered)
        {
            hovered = nullptr;
            delay.disconnect();
        }
        if (gone && (event->view == focused))
        {
            focused = nullptr;
        }
        if (gone && (event->view == menu_window))
        {
            menu_window = nullptr;
            menu_window_lost = true;
        }
        if (was_active || menu_window_lost)
        {
            settling = true;
            refocus_after_settling = refocus_after_settling || was_active;
            after_disappearing.run_once([this] { settle(); });
        }
    };

    void settle()
    {
        if (refocus_after_settling)
        {
            const wf::pointf_t pointer = wf::get_core().get_cursor_position();
            wayfire_view view = wf::get_core().get_view_at(pointer);
            hovered = view;
            if (takes_focus(view))
            {
                focus(view);
            }
        }
        refocus_after_settling = false;
        settling = false;
        if (menu_window_lost)
        {
            set_menu_window(focused);
        }
    }

    wf::ipc::method_callback_full on_watch = [this](const wf::json_t&, wf::ipc::client_interface_t* client) {
        if (std::find(watchers.begin(), watchers.end(), client) == watchers.end())
        {
            watchers.push_back(client);
            client->send_json(menu_message());
        }
        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [this](wf::ipc::client_disconnected_signal* event) {
        watchers.erase(std::remove(watchers.begin(), watchers.end(), event->client), watchers.end());
    };

    // a menu command acts on what has the keyboard, so it has to be the menu's window
    wf::ipc::method_callback on_focus_menu_window = [this](const wf::json_t&) {
        // the menu's own surface takes the keyboard a moment later, and gives it back on closing
        menu_wants_keyboard = true;
        give_menu_window_keyboard();
        return wf::ipc::json_ok();
    };

  public:
    void init() override
    {
        auto& core = wf::get_core();
        // the file reload made untyped options if our xml came after the compositor started
        if (!std::dynamic_pointer_cast<wf::config::option_t<int>>(
                core.config->get_option("focus-follows-mouse/delay_ms")))
        {
            core.config_backend->reload_config_metadata(*core.config);
        }
        delay_ms.load_option("focus-follows-mouse/delay_ms");
        raise.load_option("focus-follows-mouse/raise");
        menu_on_click.load_option("focus-follows-mouse/menu_on_click");
        menu_on_click.set_callback([this] { tell_watchers(); });
        menu_window = focused = wf::get_core().seat->get_active_view();
        repo->register_method("focus-follows-mouse/watch-menu-window", on_watch);
        repo->register_method("focus-follows-mouse/focus-menu-window", on_focus_menu_window);
        repo->connect(&on_client_disconnected);
        core.connect(&on_motion);
        core.connect(&on_motion_absolute);
        core.connect(&on_button);
        core.connect(&on_focus_changed);
        core.connect(&on_focus_request);
        init_output_tracking();
    }

    void fini() override
    {
        fini_output_tracking();
        delay.disconnect();
        on_client_disconnected.disconnect();
        repo->unregister_method("focus-follows-mouse/watch-menu-window");
        repo->unregister_method("focus-follows-mouse/focus-menu-window");
        for (auto client : watchers)
        {
            wf::json_t event;
            event["event"] = "menu-window";
            event["held"] = false;
            client->send_json(event);
        }
    }

    void handle_new_output(wf::output_t* output) override { output->connect(&on_view_disappeared); }

    void handle_output_removed(wf::output_t*) override {}
};
} // namespace focus_follows_mouse

DECLARE_WAYFIRE_PLUGIN(focus_follows_mouse::plugin_t);
