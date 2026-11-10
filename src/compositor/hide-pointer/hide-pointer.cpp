// hides the pointer while typing or after a rest, by the rules of KWin's Hide Cursor effect
// only the picture goes, programs still get every pointer event

#include <wayfire/core.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>

#include "typing-key.hpp"

namespace hide_pointer
{
template<class event_t>
using on_input_t = wf::signal::connection_t<wf::input_event_signal<event_t>>;

class plugin_t : public wf::plugin_interface_t
{
    wf::option_wrapper_t<bool> when_typing{"hide-pointer/when_typing"};
    wf::option_wrapper_t<int> after_seconds{"hide-pointer/after_seconds"};

    bool hidden = false;
    wf::wl_timer<false> rest;

    void hide()
    {
        if (!hidden)
        {
            wf::get_core().hide_cursor();
            hidden = true;
        }
    }

    void show()
    {
        if (hidden)
        {
            wf::get_core().unhide_cursor();
            hidden = false;
        }
    }

    void wait_for_rest()
    {
        rest.disconnect();
        if (after_seconds > 0)
        {
            rest.set_timeout(after_seconds * 1000, [this] { hide(); });
        }
    }

    void used()
    {
        show();
        wait_for_rest();
    }

    on_input_t<wlr_pointer_motion_event> on_motion = [this](auto) { used(); };
    on_input_t<wlr_pointer_motion_absolute_event> on_absolute_motion = [this](auto) { used(); };
    on_input_t<wlr_pointer_button_event> on_button = [this](auto) { used(); };
    on_input_t<wlr_pointer_axis_event> on_axis = [this](auto) { used(); };
    on_input_t<wlr_tablet_tool_proximity_event> on_pen_near = [this](auto) { used(); };
    on_input_t<wlr_tablet_tool_axis_event> on_pen_moved = [this](auto) { used(); };
    on_input_t<wlr_tablet_tool_tip_event> on_pen_tip = [this](auto) { used(); };

    // a key does not count as use, so it never puts off the rest timer
    on_input_t<wlr_keyboard_key_event> on_key = [this](wf::input_event_signal<wlr_keyboard_key_event>* signal) {
        if (!when_typing || signal->event->state != WL_KEYBOARD_KEY_STATE_PRESSED)
        {
            return;
        }
        wlr_keyboard* keyboard = wlr_keyboard_from_input_device(signal->device);
        if (keyboard->xkb_state && is_typing(keyboard->xkb_state, signal->event->keycode + 8))
        {
            hide();
        }
    };

  public:
    void init() override
    {
        wait_for_rest();
        when_typing.set_callback([this] { used(); });
        after_seconds.set_callback([this] { used(); });
        wf::get_core().connect(&on_motion);
        wf::get_core().connect(&on_absolute_motion);
        wf::get_core().connect(&on_button);
        wf::get_core().connect(&on_axis);
        wf::get_core().connect(&on_pen_near);
        wf::get_core().connect(&on_pen_moved);
        wf::get_core().connect(&on_pen_tip);
        wf::get_core().connect(&on_key);
    }

    void fini() override
    {
        rest.disconnect();
        show();
    }
};
} // namespace hide_pointer

DECLARE_WAYFIRE_PLUGIN(hide_pointer::plugin_t);
