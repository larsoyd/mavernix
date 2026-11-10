// tells a push kept up against a screen's edge from a pointer passing it
// timing as the Dock of OS X 10.9 asks before it moves

#pragma once

#include <cstdint>

namespace dock_dig
{
struct needs_t
{
    // how long the push lasts
    int64_t dig_ms = 250;
    // the longest wait between two moves of one push
    int64_t gap_ms = 100;
    // the most a push may lean, sideways over inward times 57.2958
    double lean = 25;
};

class dig_detect_t
{
  public:
    explicit dig_detect_t(needs_t needs = {});

    void enter(int64_t now_ms);
    // the mouse moved by dx, dy while the pointer stayed on the edge
    void move(double dx, double dy, int64_t now_ms);
    void leave();

    bool digging() const;
    // when to call fire, -1 for not until the next move
    int64_t due_ms() const;
    // true once the dig is through, which ends it
    bool fire(int64_t now_ms);

  private:
    needs_t needs;
    bool on_edge = false;
    int64_t due = -1;
    int64_t last_move_ms = 0;
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
