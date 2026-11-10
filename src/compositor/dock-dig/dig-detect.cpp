#include "dig-detect.hpp"

#include <cmath>

namespace dock_dig
{
namespace
{
const double LEAN_SCALE = 57.2957795;
}

dig_detect_t::dig_detect_t(needs_t needs) : needs(needs) {}

void dig_detect_t::enter(int64_t now_ms)
{
    on_edge = true;
    due = now_ms + needs.dig_ms;
    last_move_ms = now_ms;
    run_x = run_y = 0;
}

void dig_detect_t::move(double dx, double dy, int64_t now_ms)
{
    if (!on_edge)
    {
        return;
    }
    // the lean of the run before this move, a level run has none
    const double lean = run_y == 0 ? 0 : std::abs(run_x) / std::abs(run_y) * LEAN_SCALE;
    run_x += dx;
    run_y += dy;
    if (now_ms - last_move_ms > needs.gap_ms || lean > needs.lean)
    {
        due = now_ms + needs.dig_ms;
        run_x = run_y = 0;
    }
    last_move_ms = now_ms;
}

void dig_detect_t::leave()
{
    on_edge = false;
    due = -1;
}

bool dig_detect_t::digging() const
{
    return on_edge;
}

int64_t dig_detect_t::due_ms() const
{
    return due;
}

bool dig_detect_t::fire(int64_t now_ms)
{
    if (!on_edge)
    {
        return false;
    }
    due = -1;
    if (now_ms - last_move_ms > needs.gap_ms)
    {
        return false;
    }
    on_edge = false;
    return true;
}
void rest_detect_t::enter(double x, double y, int64_t now_ms)
{
    on_edge = true;
    due = now_ms + REST_MS;
    entered_x = x;
    entered_y = y;
}

void rest_detect_t::move(double x, double y, int64_t now_ms)
{
    if (on_edge && (std::abs(x - entered_x) > SLACK || std::abs(y - entered_y) > SLACK))
    {
        due = now_ms + REST_MS;
    }
}

void rest_detect_t::leave()
{
    on_edge = false;
    due = -1;
}

bool rest_detect_t::resting() const
{
    return on_edge;
}

int64_t rest_detect_t::due_ms() const
{
    return due;
}

bool rest_detect_t::fire()
{
    const bool rested = on_edge;
    leave();
    return rested;
}
} // namespace dock_dig
