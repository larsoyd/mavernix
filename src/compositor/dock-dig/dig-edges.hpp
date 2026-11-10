// which screens have a bottom edge to dig at, from where the screens lie

#pragma once

#include <cstddef>
#include <vector>

namespace dock_dig
{
struct box_t
{
    int x;
    int y;
    int width;
    int height;
};

// one answer per screen, the dock's own is never one
std::vector<bool> edges_to_dig(const std::vector<box_t>& screens, size_t dock);

// whether a point of the layout lies in the screen's last row
bool on_bottom_row(const box_t& screen, double x, double y);
} // namespace dock_dig
