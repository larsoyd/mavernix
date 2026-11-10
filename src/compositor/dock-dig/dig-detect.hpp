// tells a push against a screen's edge from a pointer that passes, rests or slides along it
// the Dock of OS X 10.9 asks for one unbroken push, here short pushes add up

#pragma once

#include <cstdint>

namespace dock_dig
{
struct needs_t
{
    // how long the pushing must add up to
    int64_t push_ms = 150;
    // the longest wait between two moves of one stroke
    int64_t gap_ms = 100;
    // the most a stroke may lean, sideways over inward
    double lean = 2;
    // the least a stroke moves inward, in pixels a ms
    double speed = 0.04;
    // the share of time not pushing that is taken back from the pushing
    double fade = 0.2;
};

class dig_detect_t
{
  public:
    explicit dig_detect_t(needs_t needs = {});

    void enter(int64_t now_ms);
    // the mouse moved by dx, dy with the pointer on the edge, true when that digs through
    bool move(double dx, double dy, int64_t now_ms);
    void leave();

    // on the edge with a dig still to come
    bool digging() const;
    double pushed_ms() const;

  private:
    void begin_stroke();

    needs_t needs;
    bool on_edge = false;
    // through already, nothing more until the pointer comes to the edge again
    bool spent = false;
    int64_t last_ms = 0;
    double pushed = 0;
    int64_t stroke_ms = 0;
    double run_x = 0;
    double run_y = 0;
};
// a touch of the edge that holds still, which stands in for a dig in some states
class rest_detect_t
{
  public:
    void enter(double x, double y, int64_t now_ms);
    // the pointer is at x, y now
    void move(double x, double y, int64_t now_ms);
    void leave();

    bool resting() const;
    int64_t due_ms() const;
    bool fire();

    static constexpr int64_t REST_MS = 150;
    // how far from where it entered still counts as still
    static constexpr double SLACK = 2;

  private:
    bool on_edge = false;
    int64_t due = -1;
    double entered_x = 0;
    double entered_y = 0;
};
} // namespace dock_dig
