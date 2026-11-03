// What happens to tiled windows when the line between them is dragged.
// Free of Wayfire's types so tests/test-shared-edges.cpp can check it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace shared_edges
{
/** A window's frame, in pixels. */
struct box_t
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct window_t
{
    box_t box;
    // Frame included; a maximum of 0 means no limit.
    int min_width = 1;
    int min_height = 1;
    int max_width = 0;
    int max_height = 0;
};

// same values as wlroots' edges
enum edge_t : uint32_t
{
    EDGE_TOP = 1,
    EDGE_BOTTOM = 2,
    EDGE_LEFT = 4,
    EDGE_RIGHT = 8,
};

enum class side_t
{
    LEFT,
    RIGHT,
    TOP,
    BOTTOM,
};

/** The windows that move together when one seam is dragged. */
struct seam_t
{
    // A vertical seam has windows side by side and moves sideways.
    bool vertical = true;
    // Indices of windows whose right/bottom edge (before) or left/top edge
    // (after) is on the seam.
    std::vector<std::size_t> before;
    std::vector<std::size_t> after;
};

// a tiled window and the work area borders it is tiled to
struct tile_t
{
    box_t box;
    uint32_t tiled_edges = 0;
};

struct dragged_side_t
{
    side_t side;
    // false for an edge on the work area's border
    bool free;
};

// the seam at one side of the dragged window and every window joined to it, alone lets it be one sided
std::optional<seam_t> find_seam(
    const std::vector<window_t>& windows, std::size_t dragged, side_t side, int tolerance_px, bool alone = false);

// how far the seam may follow the pointer within size limits, the floor and the work area
int limit_move(
    const std::vector<window_t>& windows, const seam_t& seam, int wanted_px, int floor_px, const box_t& area);

// the free edges a resize drags, or the edges as asked when none is free
std::vector<dragged_side_t> dragged_sides(uint32_t edges, uint32_t tiled_edges);

// a held resize drags the free edge of each axis, the one nearer the pointer if both are free
uint32_t held_edges(const box_t& box, uint32_t tiled_edges, int x, int y);

// a new tile grows or shrinks to meet the tiles across from it instead of covering them
box_t fit_beside(const box_t& slot, uint32_t edges, const std::vector<tile_t>& others, int floor_px);

/** Where a window lies once the seam has been moved by `move_px`. */
box_t moved_box(const window_t& window, const seam_t& seam, std::size_t index, int move_px);
} // namespace shared_edges
