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
} // namespace dock_dig
