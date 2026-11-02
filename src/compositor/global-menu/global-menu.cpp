#include <algorithm>
#include <map>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>

#include <wayfire/nonstd/wlroots-full.hpp>

namespace
{
bool is_window(wayfire_view view)
{
    return view && (view->role == wf::VIEW_ROLE_TOPLEVEL);
}

bool is_desktop(wayfire_view view)
{
    return view && (wf::get_view_layer(view) == wf::scene::layer::BACKGROUND);
}

wayfire_view top_window(wf::output_t* output)
{
    auto views = output->wset()->get_views(wf::WSET_MAPPED_ONLY | wf::WSET_CURRENT_WORKSPACE |
        wf::WSET_EXCLUDE_MINIMIZED | wf::WSET_SORT_STACKING);
    auto found = std::find_if(views.begin(), views.end(), [](wayfire_toplevel_view view) { return is_window(view); });
    return found != views.end() ? wayfire_view(*found) : nullptr;
}

wf::output_t* output_under_pointer()
{
    const wf::pointf_t pointer = wf::get_core().get_cursor_position();
    for (auto output : wf::get_core().output_layout->get_outputs())
    {
        if (output->get_layout_geometry() & wf::point_t{int(pointer.x), int(pointer.y)})
        {
            return output;
        }
    }
    return nullptr;
}

class GlobalMenu : public wf::plugin_interface_t, public wf::per_output_tracker_mixin_t<>
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> methods;
    std::vector<wf::ipc::client_interface_t*> watchers;
    // the window each output's menu bar shows, none is the desktop
    std::map<wf::output_t*, wayfire_view> windows;
    wf::output_t* active = nullptr;
    // the last window chosen on any output, which a bar shared by all outputs shows
    wayfire_view chosen = nullptr;
    wayfire_view focused = nullptr;
    std::vector<wf::output_t*> lost;
    wayfire_view moved = nullptr;
    bool passing = false;
    bool settling = false;
    bool shell_has_keyboard = false;
    bool menu_wants_keyboard = false;
    wayfire_view keyboard_for = nullptr;
    wf::wl_idle_call after_shell;
    wf::wl_idle_call after_others;
    wf::wl_idle_call settle_now;

    static wf::json_t describe(wayfire_view view)
    {
        wf::json_t description;
        description["id"] = view->get_id();
        description["role"] = "toplevel";
        description["app-id"] = view->get_app_id();
        return description;
    }

    wf::json_t message() const
    {
        wf::json_t event;
        event["event"] = "menu-window";
        event["held"] = true;
        wf::json_t each;
        for (const auto& [output, view] : windows)
        {
            if (view)
            {
                each[output->to_string()] = describe(view);
            }
        }
        event["windows"] = each;
        if (active)
        {
            event["active"] = active->to_string();
        }
        if (chosen)
        {
            event["view"] = describe(chosen);
        }
        return event;
    }

    void tell_watchers()
    {
        for (auto client : watchers)
        {
            client->send_json(message());
        }
    }

    void choose(wf::output_t* output, wayfire_view view)
    {
        if (output && ((windows[output] != view) || (active != output)))
        {
            windows[output] = view;
            active = output;
            tell_watchers();
        }
    }

    void choose_window(wayfire_view view)
    {
        const bool changed = (view != chosen);
        chosen = view;
        wf::output_t* output = view ? view->get_output() : wf::get_core().seat->get_active_output();
        const bool told = output && ((windows[output] != view) || (active != output));
        choose(output, view);
        if (changed && !told)
        {
            tell_watchers();
        }
    }

    void give_keyboard(wayfire_view view)
    {
        if (view)
        {
            passing = true;
            wf::get_core().seat->focus_output(view->get_output());
            wf::get_core().seat->focus_view(view);
            passing = false;
        }
    }

    // focus coming back from a shell surface or passing by hover is nobody's choice
    void focus_moved(wf::scene::node_ptr node)
    {
        wayfire_view view = wf::node_to_view(node);
        if (node && !view)
        {
            return;
        }
        if (view && !is_window(view) && !is_desktop(view))
        {
            shell_has_keyboard = true;
            return;
        }
        const bool from_shell = shell_has_keyboard;
        shell_has_keyboard = false;
        wayfire_view now = is_window(view) ? view : nullptr;
        if (from_shell && menu_wants_keyboard)
        {
            menu_wants_keyboard = false;
            after_shell.run_once([this] { give_keyboard(keyboard_for); });
        }
        if (now == focused)
        {
            return;
        }
        focused = now;
        if (!passing && !settling && !from_shell && !wf::get_core().has_data("menu-bar-passing-focus"))
        {
            choose_window(now);
        }
    }

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_focus_changed =
        [this](wf::keyboard_focus_changed_signal* event) { focus_moved(event->new_focus); };

    // a press on a shell surface makes the compositor refocus for that output
    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_button_coming =
        [this](wf::input_event_signal<wlr_pointer_button_event>* event) {
        wayfire_view view = wf::get_core().get_cursor_focus_view();
        passing = (event->event->state == WL_POINTER_BUTTON_STATE_PRESSED) && !is_window(view) && !is_desktop(view);
    };

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_button_event>> on_button =
        [this](wf::post_input_event_signal<wlr_pointer_button_event>* event) {
        passing = false;
        if (event->event->state != WL_POINTER_BUTTON_STATE_PRESSED)
        {
            return;
        }
        auto& core = wf::get_core();
        wayfire_view view = core.get_cursor_focus_view();
        wf::output_t* output = output_under_pointer();
        if (is_window(view))
        {
            choose_window(view);
        }
        else if (output)
        {
            choose(output, is_desktop(view) ? nullptr : windows[output]);
        }
    };

    // a dock or a program asking covers a window that already has the keyboard
    wf::signal::connection_t<wf::view_focus_request_signal> on_focus_request =
        [this](wf::view_focus_request_signal* event) {
        if (is_window(event->view))
        {
            choose_window(event->view);
        }
    };

    wf::signal::connection_t<wf::view_disappeared_signal> on_view_disappeared =
        [this](wf::view_disappeared_signal* event) {
        auto toplevel = wf::toplevel_cast(event->view);
        // a move to another output disappears too, and such a window is still there
        const bool gone = !event->view->is_mapped() || (toplevel && toplevel->minimized);
        const bool was_focused = (event->view == focused);
        if (gone && was_focused)
        {
            focused = nullptr;
        }
        if (gone && (event->view == keyboard_for))
        {
            keyboard_for = nullptr;
        }
        if (gone && (event->view == chosen))
        {
            chosen = nullptr;
        }
        for (auto& [output, view] : windows)
        {
            if ((view == event->view) && (gone || (view->get_output() != output)))
            {
                view = nullptr;
                lost.push_back(output);
                moved = gone ? nullptr : event->view;
            }
        }
        if (was_focused || !lost.empty())
        {
            settling = true;
            // twice, so that focus follows mouse has picked its window
            after_others.run_once([this] { settle_now.run_once([this] { settle(); }); });
        }
    };

    void settle()
    {
        settling = false;
        for (auto output : lost)
        {
            if (!windows.count(output))
            {
                continue;
            }
            if (output == active)
            {
                choose_window(moved ? moved : focused);
            }
            else
            {
                windows[output] = top_window(output);
            }
        }
        lost.clear();
        moved = nullptr;
        tell_watchers();
    }

    // the active output's focus changes with its workspace, the others have no focus to follow
    wf::signal::connection_t<wf::workspace_changed_signal> on_workspace_changed =
        [this](wf::workspace_changed_signal* event) {
        if (event->output != active)
        {
            windows[event->output] = top_window(event->output);
            tell_watchers();
        }
    };

    wf::ipc::method_callback_full watch = [this](const wf::json_t&, wf::ipc::client_interface_t* client) {
        if (std::find(watchers.begin(), watchers.end(), client) == watchers.end())
        {
            watchers.push_back(client);
            client->send_json(message());
        }
        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::ipc::client_disconnected_signal> on_client_disconnected =
        [this](wf::ipc::client_disconnected_signal* event) {
        watchers.erase(std::remove(watchers.begin(), watchers.end(), event->client), watchers.end());
    };

    // a menu command acts on what has the keyboard, so it has to be the menu's window
    wf::ipc::method_callback focus_menu_window = [this](const wf::json_t& request) {
        const std::string name = request.has_member("output") ? std::string(request["output"]) : "";
        wf::output_t* output = wf::get_core().output_layout->find_output(name);
        if (output)
        {
            choose(output, windows[output]);
        }
        keyboard_for = output ? windows[output] : chosen;
        // the menu's own surface takes the keyboard a moment later, and gives it back on closing
        menu_wants_keyboard = true;
        give_keyboard(keyboard_for);
        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback address = [](const wf::json_t& request) {
        auto view = wf::ipc::json_find_view_or_throw(request);
        auto result = wf::ipc::json_ok();
        auto property = [&view](const char* name) { return view->get_property<std::string>(name).value_or(""); };
        const auto service = property("kde-appmenu-service-name");
        const auto path = property("kde-appmenu-object-path");
        if (!service.empty() && !path.empty())
        {
            result["protocol"] = "dbusmenu";
            result["service"] = service;
            result["path"] = path;
        }
        else
        {
            const auto gtkService = property("gtk-shell-unique-bus-name");
            auto menuPath = property("gtk-shell-menubar-path");
            if (menuPath.empty())
                menuPath = property("gtk-shell-app-menu-path");
            if (!gtkService.empty() && !menuPath.empty())
            {
                result["protocol"] = "gmenu";
                result["service"] = gtkService;
                result["path"] = menuPath;
                result["appMenu"] = property("gtk-shell-app-menu-path");
                result["appActions"] = property("gtk-shell-application-object-path");
                result["windowActions"] = property("gtk-shell-window-object-path");
            }
        }
#if WF_HAS_XWAYLAND
        if (auto* surface = view->get_wlr_surface())
        {
            if (auto* xwindow = wlr_xwayland_surface_try_from_wlr_surface(surface))
                result["xid"] = uint64_t(xwindow->window_id);
        }
#endif
        return result;
    };

  public:
    void init() override
    {
        auto& core = wf::get_core();
        methods->register_method("global-menu/address", address);
        methods->register_method("global-menu/watch-menu-window", watch);
        methods->register_method("global-menu/focus-menu-window", focus_menu_window);
        methods->connect(&on_client_disconnected);
        core.connect(&on_focus_changed);
        core.connect(&on_button_coming);
        core.connect(&on_button);
        core.connect(&on_focus_request);
        init_output_tracking();
        active = core.seat->get_active_output();
        focused = core.seat->get_active_view();
        if (focused)
        {
            windows[focused->get_output()] = focused;
        }
    }

    void fini() override
    {
        fini_output_tracking();
        on_client_disconnected.disconnect();
        methods->unregister_method("global-menu/address");
        methods->unregister_method("global-menu/watch-menu-window");
        methods->unregister_method("global-menu/focus-menu-window");
        wf::json_t event;
        event["event"] = "menu-window";
        event["held"] = false;
        for (auto client : watchers)
        {
            client->send_json(event);
        }
    }

    void handle_new_output(wf::output_t* output) override
    {
        output->connect(&on_view_disappeared);
        output->connect(&on_workspace_changed);
        windows[output] = top_window(output);
        tell_watchers();
    }

    void handle_output_removed(wf::output_t* output) override
    {
        windows.erase(output);
        if (active == output)
        {
            active = wf::get_core().seat->get_active_output();
        }
        tell_watchers();
    }
};
} // namespace

DECLARE_WAYFIRE_PLUGIN(GlobalMenu);
