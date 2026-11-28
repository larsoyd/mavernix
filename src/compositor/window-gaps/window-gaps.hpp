#pragma once

#include <wayfire/object.hpp>
#include <wayfire/toplevel-view.hpp>

namespace wf
{
// on a window that keeps room around itself while tiled, games and listed apps get none
struct gapped_view_t : public wf::custom_data_t
{
    int px = 0;
};

// on core while window-gaps runs, the room a tile keeps before any exception
struct window_gaps_t : public wf::custom_data_t
{
    int px = 0;
};

// emitted on core when windows gained, lost or changed their room
struct window_gaps_changed_signal
{};

inline int gap_of(wf::object_base_t& view)
{
    auto gapped = view.get_data<gapped_view_t>();
    return gapped ? gapped->px : 0;
}

inline int gap_of(wayfire_toplevel_view view)
{
    return gap_of(*view);
}
} // namespace wf
