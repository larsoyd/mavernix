// which process drew a window, for plugins that ask whether it is a game

#pragma once

#include <sys/types.h>

#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/view.hpp>

#include "game-marks.hpp"

namespace game_window
{
inline pid_t client_pid(wayfire_view view)
{
    wlr_surface* surface = view->get_wlr_surface();
    if (!surface)
    {
        return 0;
    }
#if WF_HAS_XWAYLAND
    if (auto* xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface))
    {
        return xsurface->pid;
    }
#endif
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(surface->resource), &pid, nullptr, nullptr);
    return pid;
}
} // namespace game_window
