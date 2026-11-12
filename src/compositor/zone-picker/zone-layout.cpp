#include "zone-layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace zone_picker
{
namespace
{
// how far a share may miss a side and still touch it
const double TOUCH = 0.0005;

std::vector<std::string> split(const std::string& text, char by)
{
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (true)
    {
        const std::size_t at = text.find(by, from);
        parts.push_back(text.substr(from, at == std::string::npos ? at : at - from));
        if (at == std::string::npos)
        {
            return parts;
        }

        from = at + 1;
    }
}

std::optional<double> share(const std::string& percent)
{
    char* end = nullptr;
    const double value = std::strtod(percent.c_str(), &end);
    if (percent.empty() || (*end != '\0') || !std::isfinite(value) || (value < 0) || (value > 100))
    {
        return {};
    }

    return value / 100;
}

std::optional<zone_t> parse_zone(const std::string& text)
{
    const auto fields = split(text, ',');
    if (fields.size() != 5)
    {
        return {};
    }

    const auto x = share(fields[1]), y = share(fields[2]), width = share(fields[3]), height = share(fields[4]);
    if (!x || !y || !width || !height || (*width <= 0) || (*height <= 0) || (*x + *width > 1 + TOUCH) ||
        (*y + *height > 1 + TOUCH))
    {
        return {};
    }

    return zone_t{fields[0], *x, *y, *width, *height};
}

int plate_width(int count, int mini_width, const metrics_t& metrics)
{
    return 2 * metrics.padding + count * mini_width + (count - 1) * metrics.gap;
}

// one side of a cell, pulled in where it meets another cell
int cell_side(double share, int length, int gutter, bool far_side)
{
    const int at = int(std::lround(share * length));
    if ((at <= 0) || (at >= length))
    {
        return at;
    }

    return far_side ? at - gutter / 2 : at + (gutter + 1) / 2;
}

box_t cell_box(const box_t& mini, const zone_t& zone, int gutter)
{
    const int left = cell_side(zone.x, mini.width, gutter, false);
    const int right = cell_side(zone.x + zone.width, mini.width, gutter, true);
    const int top = cell_side(zone.y, mini.height, gutter, false);
    const int bottom = cell_side(zone.y + zone.height, mini.height, gutter, true);
    return {mini.x + left, mini.y + top, std::max(right - left, 1), std::max(bottom - top, 1)};
}

bool within(const box_t& box, int grown, int px, int py)
{
    return box_t{box.x - grown, box.y - grown, box.width + 2 * grown, box.height + 2 * grown}.holds(px, py);
}

// beside or below the plate's hiding place, the bar above it counts too
bool in_reach(const box_t& box, int distance, int px, int py)
{
    return (px >= box.x - distance) && (px < box.x + box.width + distance) && (py < box.y + distance);
}
} // namespace

std::vector<layout_t> parse_layouts(const std::string& text)
{
    std::vector<layout_t> layouts;
    for (const auto& layout_text : split(text, '|'))
    {
        layout_t layout;
        for (const auto& zone_text : split(layout_text, ';'))
        {
            if (const auto zone = parse_zone(zone_text))
            {
                layout.push_back(*zone);
            }
        }

        if (!layout.empty())
        {
            layouts.push_back(layout);
        }
    }

    return layouts;
}

plate_t lay_out(int screen_width, int screen_height, const box_t& workarea, const std::vector<layout_t>& layouts,
    const metrics_t& metrics)
{
    plate_t plate{{screen_width / 2, workarea.y, 0, 0}, {}};
    int count = int(layouts.size());
    if ((count == 0) || (screen_width <= 0) || (screen_height <= 0))
    {
        return plate;
    }

    const int room = screen_width - 2 * metrics.padding;
    int mini_width = std::clamp(int(std::lround(screen_width * metrics.mini_share)), metrics.mini_min,
        metrics.mini_max);
    if (plate_width(count, mini_width, metrics) > room)
    {
        const int spare = room - 2 * metrics.padding - (count - 1) * metrics.gap;
        mini_width = std::max(spare / count, metrics.mini_min);
    }

    while ((count > 1) && (plate_width(count, mini_width, metrics) > room))
    {
        count--;
    }

    const int mini_height = std::max(int(std::lround(double(mini_width) * screen_height / screen_width)), 1);
    const int width = plate_width(count, mini_width, metrics);
    plate.box = {(screen_width - width) / 2, workarea.y, width, 2 * metrics.padding + mini_height};
    for (int index = 0; index < count; index++)
    {
        mini_t mini;
        mini.box = {plate.box.x + metrics.padding + index * (mini_width + metrics.gap),
            plate.box.y + metrics.padding, mini_width, mini_height};
        for (const auto& zone : layouts[index])
        {
            mini.cells.push_back(cell_box(mini.box, zone, metrics.gutter));
        }

        plate.minis.push_back(mini);
    }

    return plate;
}

state_t next_state(state_t now, const plate_t& plate, const metrics_t& metrics, int px, int py)
{
    if (plate.minis.empty())
    {
        return state_t::HIDDEN;
    }

    const box_t& box = plate.box;
    if ((now == state_t::OPEN) && within(box, metrics.margin, px, py))
    {
        return state_t::OPEN;
    }

    if (box_t{box.x, box.y, box.width, metrics.peek}.holds(px, py))
    {
        return state_t::OPEN;
    }

    // a woken plate lets go a little later than it woke
    const int reach = metrics.distance + (now == state_t::PEEK ? metrics.margin : 0);
    return in_reach(box, reach, px, py) ? state_t::PEEK : state_t::HIDDEN;
}

pick_t zone_at(const plate_t& plate, const std::vector<layout_t>& layouts, int px, int py)
{
    for (std::size_t index = 0; index < plate.minis.size(); index++)
    {
        const box_t& mini = plate.minis[index].box;
        if (!mini.holds(px, py))
        {
            continue;
        }

        const double x = (px - mini.x + 0.5) / mini.width;
        const double y = (py - mini.y + 0.5) / mini.height;
        const layout_t& zones = layouts[index];
        for (int zone = int(zones.size()) - 1; zone >= 0; zone--)
        {
            const zone_t& at = zones[zone];
            if ((x >= at.x) && (x < at.x + at.width) && (y >= at.y) && (y < at.y + at.height))
            {
                return {int(index), zone};
            }
        }
    }

    return {};
}

box_t target_box(const box_t& workarea, const zone_t& zone)
{
    const int left = int(std::lround(zone.x * workarea.width));
    const int right = int(std::lround((zone.x + zone.width) * workarea.width));
    const int top = int(std::lround(zone.y * workarea.height));
    const int bottom = int(std::lround((zone.y + zone.height) * workarea.height));
    return {workarea.x + left, workarea.y + top, right - left, bottom - top};
}

uint32_t tiled_edges(const zone_t& zone)
{
    uint32_t edges = 0;
    edges |= zone.x <= TOUCH ? EDGE_LEFT : 0;
    edges |= zone.x + zone.width >= 1 - TOUCH ? EDGE_RIGHT : 0;
    edges |= zone.y <= TOUCH ? EDGE_TOP : 0;
    edges |= zone.y + zone.height >= 1 - TOUCH ? EDGE_BOTTOM : 0;
    return edges;
}
} // namespace zone_picker
