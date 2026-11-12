// where the plate of layouts sits while a window is dragged and which zone the pointer picks
// free of the compositor so it can be tested alone

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zone_picker
{
// equal to wlroots' WLR_EDGE_*, checked at compile time in the plugin
const uint32_t EDGE_TOP = 1;
const uint32_t EDGE_BOTTOM = 2;
const uint32_t EDGE_LEFT = 4;
const uint32_t EDGE_RIGHT = 8;
const uint32_t EDGES_ALL = EDGE_TOP | EDGE_BOTTOM | EDGE_LEFT | EDGE_RIGHT;

struct box_t
{
    int x;
    int y;
    int width;
    int height;

    bool operator==(const box_t& other) const
    {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }

    bool holds(int px, int py) const { return px >= x && px < x + width && py >= y && py < y + height; }
};

// a zone as shares of the room windows may use, 0 to 1
struct zone_t
{
    std::string name;
    double x;
    double y;
    double width;
    double height;
};

using layout_t = std::vector<zone_t>;

struct metrics_t
{
    // how far below the plate's hiding place the pointer wakes it
    int distance = 75;
    // how much of the plate shows once woken
    int peek = 30;
    int padding = 16;
    int gap = 14;
    // room between the cells of one small screen
    int gutter = 2;
    // how far outside the open plate the pointer may stray
    int margin = 10;
    double mini_share = 0.05;
    int mini_min = 64;
    int mini_max = 160;
};

enum class state_t
{
    HIDDEN,
    PEEK,
    OPEN,
};

struct mini_t
{
    box_t box;
    std::vector<box_t> cells;
};

// the open plate in the screen's own coordinates
struct plate_t
{
    box_t box;
    std::vector<mini_t> minis;
};

struct pick_t
{
    int layout = -1;
    int zone = -1;

    bool any() const { return layout >= 0; }
    bool operator==(const pick_t& other) const { return layout == other.layout && zone == other.zone; }
};

// layouts split by bars, each a run of zones written name,x,y,width,height in percent
// a zone that is no box inside the screen is left out and so is an empty layout
std::vector<layout_t> parse_layouts(const std::string& text);

// layouts that do not fit the screen's width are left off the end
plate_t lay_out(int screen_width, int screen_height, const box_t& workarea, const std::vector<layout_t>& layouts,
    const metrics_t& metrics);

state_t next_state(state_t now, const plate_t& plate, const metrics_t& metrics, int px, int py);

pick_t zone_at(const plate_t& plate, const std::vector<layout_t>& layouts, int px, int py);

// neighbouring zones meet on the same pixel
box_t target_box(const box_t& workarea, const zone_t& zone);

uint32_t tiled_edges(const zone_t& zone);
} // namespace zone_picker
