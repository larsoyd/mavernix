// the room left around tiled windows, free of the compositor so it can be tested alone

#pragma once

namespace window_gaps
{
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
};

// a side on the area's border moves in by the gap, a side between tiles by half of it
box_t gapped(const box_t& plain, const box_t& area, int gap);

// the box a gapped tile came from
box_t plain(const box_t& gapped, const box_t& area, int gap);
} // namespace window_gaps
