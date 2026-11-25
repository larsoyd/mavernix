#include "dig-edges.hpp"

namespace dock_dig
{
namespace
{
bool overlap(const box_t& a, const box_t& b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
} // namespace

std::vector<bool> edges_to_dig(const std::vector<box_t>& screens, size_t dock)
{
    std::vector<bool> edges(screens.size(), false);
    for (size_t i = 0; i < screens.size(); i++)
    {
        if (i == dock)
        {
            continue;
        }
        // the row one below the edge, another screen there takes the edge away
        const box_t below = {screens[i].x, screens[i].y + screens[i].height + 1, screens[i].width, 1};
        bool free_edge = true;
        for (size_t other = 0; other < screens.size(); other++)
        {
            free_edge = free_edge && (other == dock || !overlap(below, screens[other]));
        }
        edges[i] = free_edge;
    }
    return edges;
}

bool on_bottom_row(const box_t& screen, double x, double y)
{
    const double bottom = screen.y + screen.height;
    return x >= screen.x && x < screen.x + screen.width && y >= bottom - 1 && y < bottom;
}

side_t side_named(const std::string& name)
{
    return name == "left" ? side_t::left : name == "right" ? side_t::right : side_t::bottom;
}

bool on_edge_row(const box_t& screen, side_t side, double x, double y)
{
    if (side == side_t::bottom)
    {
        return on_bottom_row(screen, x, y);
    }
    const double right = screen.x + screen.width;
    const bool in_column = side == side_t::left ? x >= screen.x && x < screen.x + 1 : x >= right - 1 && x < right;
    return in_column && y >= screen.y && y < screen.y + screen.height;
}

std::pair<double, double> inward(side_t side, double dx, double dy)
{
    if (side == side_t::left)
    {
        return {dy, -dx};
    }
    if (side == side_t::right)
    {
        return {dy, dx};
    }
    return {dx, dy};
}
} // namespace dock_dig
