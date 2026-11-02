#include <map>
#include <memory>

#include <wayfire/config.h>
#include <wayfire/core.hpp>
#include <wayfire/debug.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>

// the event header needs this and wlroots-full only gives it with the generated protocol header
extern "C"
{
#include <wlr/types/wlr_xdg_shell.h>
}

#include <wayfire/unstable/wlr-view-events.hpp>
#include <wayfire/util.hpp>

namespace xwayland_first_buffer
{
// wlroots maps only from a commit made after pairing, so a first buffer that gets here early
// strands the window: xwayland then waits on a frame an unmapped surface never receives
class watch_t
{
    wlr_xwayland_surface* xw;
    wf::wl_listener_wrapper on_associate;
    wf::wl_listener_wrapper on_destroy;
    wf::wl_idle_call later;

    void map_if_stranded()
    {
        if (xw->surface && !xw->surface->mapped && wlr_surface_has_buffer(xw->surface))
        {
            LOGI("[xwayland-first-buffer] mapped window=", xw->window_id, " title=", xw->title ? xw->title : "");
            wlr_surface_map(xw->surface);
        }
    }

  public:
    explicit watch_t(wlr_xwayland_surface* surface, std::map<wlr_xwayland_surface*, std::unique_ptr<watch_t>>& all)
        : xw(surface)
    {
        // on idle, so that core has connected to the surface's map event
        on_associate.set_callback([this](void*) { later.run_once([this] { map_if_stranded(); }); });
        on_destroy.set_callback([this, &all](void*) { all.erase(xw); });
        on_associate.connect(&xw->events.associate);
        on_destroy.connect(&xw->events.destroy);
    }
};

class plugin_t : public wf::plugin_interface_t
{
    std::map<wlr_xwayland_surface*, std::unique_ptr<watch_t>> watches;

    wf::signal::connection_t<wf::new_xwayland_surface_signal> on_new_surface =
        [this](wf::new_xwayland_surface_signal* event) {
            watches[event->surface] = std::make_unique<watch_t>(event->surface, watches);
        };

  public:
    void init() override { wf::get_core().connect(&on_new_surface); }

    void fini() override
    {
        on_new_surface.disconnect();
        watches.clear();
    }
};
} // namespace xwayland_first_buffer

DECLARE_WAYFIRE_PLUGIN(xwayland_first_buffer::plugin_t);
