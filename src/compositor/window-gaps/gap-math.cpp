#include "gap-math.hpp"

namespace window_gaps
{
namespace
{
struct sides_t
{
    int left;
    int top;
    int right;
    int bottom;
};

sides_t sides_of(const box_t& box)
{
    return {box.x, box.y, box.x + box.width, box.y + box.height};
}

box_t box_of(const sides_t& sides)
{
    return {sides.left, sides.top, sides.right - sides.left, sides.bottom - sides.top};
}

// left and top take the larger half so an odd gap stays whole between two tiles
int leading_half(int gap)
{
    return gap - gap / 2;
}

int trailing_half(int gap)
{
    return gap / 2;
}
} // namespace

box_t gapped(const box_t& plain, const box_t& area, int gap)
{
    const sides_t from = sides_of(plain);
    const sides_t room = sides_of(area);
    sides_t to = from;
    to.left += (from.left == room.left) ? gap : leading_half(gap);
    to.top += (from.top == room.top) ? gap : leading_half(gap);
    to.right -= (from.right == room.right) ? gap : trailing_half(gap);
    to.bottom -= (from.bottom == room.bottom) ? gap : trailing_half(gap);
    return box_of(to);
}

box_t plain(const box_t& gapped, const box_t& area, int gap)
{
    const sides_t from = sides_of(gapped);
    const sides_t room = sides_of(area);
    // no tile is that narrow, so a side this close to the border was put on it
    const int reach = gap + gap / 2;
    sides_t to = from;
    to.left = (from.left - room.left <= reach) ? room.left : from.left - leading_half(gap);
    to.top = (from.top - room.top <= reach) ? room.top : from.top - leading_half(gap);
    to.right = (room.right - from.right <= reach) ? room.right : from.right + trailing_half(gap);
    to.bottom = (room.bottom - from.bottom <= reach) ? room.bottom : from.bottom + trailing_half(gap);
    return box_of(to);
}
} // namespace window_gaps
