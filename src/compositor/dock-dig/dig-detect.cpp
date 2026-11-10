#include "dig-detect.hpp"

#include <algorithm>
#include <cmath>

namespace dock_dig
{
dig_detect_t::dig_detect_t(needs_t needs) : needs(needs) {}

void dig_detect_t::begin_stroke()
{
    stroke_ms = 0;
    run_x = run_y = 0;
}

void dig_detect_t::enter(int64_t now_ms)
{
    pushed = std::max(0.0, pushed - needs.fade * double(now_ms - last_ms));
    last_ms = now_ms;
    on_edge = true;
    spent = false;
    begin_stroke();
}

bool dig_detect_t::move(double dx, double dy, int64_t now_ms)
{
    if (!on_edge || spent)
    {
        return false;
    }
    const int64_t waited = now_ms - last_ms;
    last_ms = now_ms;
    if (waited > needs.gap_ms)
    {
        begin_stroke();
    }
    else
    {
        stroke_ms += waited;
    }
    run_x += dx;
    run_y += dy;
    // a stroke pushes while it goes inward fast enough and more inward than along
    const bool pushing = stroke_ms > 0 && run_y >= needs.speed * double(stroke_ms) &&
        std::abs(run_x) <= needs.lean * run_y;
    if (!pushing)
    {
        pushed = std::max(0.0, pushed - needs.fade * double(waited));
        begin_stroke();
        return false;
    }
    pushed += double(waited);
    if (pushed < double(needs.push_ms))
    {
        return false;
    }
    pushed = 0;
    spent = true;
    return true;
}

void dig_detect_t::leave()
{
    on_edge = false;
}

bool dig_detect_t::digging() const
{
    return on_edge && !spent;
}

double dig_detect_t::pushed_ms() const
{
    return pushed;
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
