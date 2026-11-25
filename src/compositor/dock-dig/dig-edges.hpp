// which screens have a bottom edge to dig at, from where the screens lie

#pragma once

#include <cstddef>
#include <string>
#include <utility>
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

// the screen edge the dock stands on
enum class side_t
{
    bottom,
    left,
    right,
};
side_t side_named(const std::string& name);

// the screen's last row, or its first or last column for a side dock
bool on_edge_row(const box_t& screen, side_t side, double x, double y);

// a move turned so that pushing towards the dock's edge is pushing down
std::pair<double, double> inward(side_t side, double dx, double dy);
} // namespace dock_dig
