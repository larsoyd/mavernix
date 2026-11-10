#include <QtTest>

#include "dig-detect.hpp"
#include "dig-edges.hpp"

using namespace dock_dig;

namespace
{
// moves by dx, dy every step_ms from the move after start_ms up to end_ms
// gives the time of the move that dug through, or -1
int64_t push(dig_detect_t& detect, int64_t start_ms, int64_t end_ms, double dx = 0, double dy = 4, int64_t step_ms = 10)
{
    for (int64_t now = start_ms + step_ms; now <= end_ms; now += step_ms)
    {
        if (detect.move(dx, dy, now))
        {
            return now;
        }
    }
    return -1;
}

const box_t LEFT = {0, 0, 1280, 720};
const box_t RIGHT = {1280, 0, 1280, 720};
} // namespace

class TestDockDig : public QObject
{
    Q_OBJECT

  private slots:
    void a_steady_push_digs_through()
    {
        dig_detect_t detect;
        detect.enter(1000);
        QVERIFY(detect.digging());
        QCOMPARE(push(detect, 1000, 1400), 1150);
        QVERIFY(!detect.digging());
    }

    void a_push_that_stops_short_does_not_dig()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 140), -1);
        QCOMPARE(detect.pushed_ms(), 140.0);
        QVERIFY(detect.digging());
    }

    void two_short_pushes_add_up()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 100), -1);
        // the 210 ms apart take 42 back, 58 are left, 92 more are needed
        QCOMPARE(push(detect, 300, 600), 410);
    }

    void pushes_far_apart_do_not_add_up()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 100), -1);
        QCOMPARE(push(detect, 1000, 1400), 1160);
    }

    void the_move_after_a_pause_only_starts_a_stroke()
    {
        dig_detect_t detect;
        detect.enter(0);
        // 101 ms apart is over the gap each time
        QCOMPARE(push(detect, 0, 5000, 0, 40, 101), -1);
        QCOMPARE(detect.pushed_ms(), 0.0);
    }

    void a_wait_of_just_the_gap_keeps_the_stroke()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 5000, 0, 40, 100), 200);
    }

    void a_push_at_a_slant_digs()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 400, 7, 4), 150);
        dig_detect_t other_way;
        other_way.enter(0);
        QCOMPARE(push(other_way, 0, 400, -7, 4), 150);
    }

    void a_slide_along_the_edge_does_not_dig()
    {
        dig_detect_t detect;
        detect.enter(0);
        // 9 over 4 leans more than 2
        QCOMPARE(push(detect, 0, 3000, 9, 4), -1);
        QCOMPARE(detect.pushed_ms(), 0.0);
    }

    void the_lean_is_of_the_whole_stroke()
    {
        dig_detect_t detect;
        detect.enter(0);
        QVERIFY(!detect.move(0, 100, 10));
        // 150 over 100 is within, though this move alone goes level
        QVERIFY(!detect.move(150, 0, 20));
        QCOMPARE(detect.pushed_ms(), 20.0);
        // 210 over 100 is not, and the stroke is forgotten
        QVERIFY(!detect.move(60, 0, 30));
        QCOMPARE(detect.pushed_ms(), 18.0);
        // so the next move is judged alone
        QVERIFY(!detect.move(0, 4, 40));
        QCOMPARE(detect.pushed_ms(), 28.0);
    }

    void a_level_or_outward_move_does_not_dig()
    {
        dig_detect_t level;
        level.enter(0);
        QCOMPARE(push(level, 0, 3000, 6, 0), -1);
        dig_detect_t outward;
        outward.enter(0);
        QCOMPARE(push(outward, 0, 3000, 0, -4), -1);
        QCOMPARE(outward.pushed_ms(), 0.0);
    }

    void a_tremble_does_not_dig()
    {
        dig_detect_t detect;
        detect.enter(0);
        // 0.3 px in 30 ms is 0.01 px a ms
        QCOMPARE(push(detect, 0, 6000, 0, 0.3, 30), -1);
        QCOMPARE(detect.pushed_ms(), 0.0);
    }

    void a_slow_lean_digs()
    {
        dig_detect_t detect;
        detect.enter(0);
        // 0.5 px in 10 ms is 0.05 px a ms
        QCOMPARE(push(detect, 0, 400, 0, 0.5), 150);
    }

    void the_speed_is_of_the_whole_stroke()
    {
        dig_detect_t detect;
        detect.enter(0);
        QVERIFY(!detect.move(0, 8, 10));
        // 8 px in 100 ms is still fast enough, though these moves alone are not
        QCOMPARE(push(detect, 10, 100, 0, 0), -1);
        QCOMPARE(detect.pushed_ms(), 100.0);
    }

    void a_bounce_off_the_edge_keeps_the_push()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 100), -1);
        detect.leave();
        QVERIFY(!detect.digging());
        QVERIFY(!detect.move(0, 4, 110));
        detect.enter(120);
        // 100 less 4 for the 20 ms away, 54 more are needed
        QCOMPARE(push(detect, 120, 400), 180);
    }

    void a_stroke_ends_when_the_pointer_leaves()
    {
        dig_detect_t detect;
        detect.enter(0);
        QVERIFY(!detect.move(0, 500, 10));
        detect.leave();
        detect.enter(20);
        // the slide that follows is no part of the push before it
        QCOMPARE(push(detect, 20, 2000, 9, 4), -1);
        QCOMPARE(detect.pushed_ms(), 0.0);
    }

    void time_away_from_the_edge_fades_the_push()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 100), -1);
        detect.leave();
        detect.enter(400);
        QCOMPARE(detect.pushed_ms(), 40.0);
    }

    void after_a_dig_the_edge_is_spent_until_the_pointer_returns()
    {
        dig_detect_t detect;
        detect.enter(0);
        QCOMPARE(push(detect, 0, 150), 150);
        QCOMPARE(push(detect, 150, 2000), -1);
        QCOMPARE(detect.pushed_ms(), 0.0);
        detect.leave();
        detect.enter(2100);
        QVERIFY(detect.digging());
        QCOMPARE(push(detect, 2100, 2400), 2250);
    }

    void a_move_away_from_the_edge_is_nothing()
    {
        dig_detect_t detect;
        QCOMPARE(push(detect, 0, 1000), -1);
        QCOMPARE(detect.pushed_ms(), 0.0);
    }

    void other_needs_are_kept()
    {
        dig_detect_t longer(needs_t{300, 100, 2, 0.04, 0.2});
        longer.enter(0);
        QCOMPARE(push(longer, 0, 600), 300);

        dig_detect_t short_gap(needs_t{150, 50, 2, 0.04, 0.2});
        short_gap.enter(0);
        QCOMPARE(push(short_gap, 0, 3000, 0, 40, 60), -1);

        dig_detect_t upright(needs_t{150, 100, 1, 0.04, 0.2});
        upright.enter(0);
        QCOMPARE(push(upright, 0, 3000, 5, 4), -1);
        upright.leave();
        upright.enter(4000);
        QCOMPARE(push(upright, 4000, 4400, 4, 5), 4150);

        dig_detect_t brisk(needs_t{150, 100, 2, 0.5, 0.2});
        brisk.enter(0);
        QCOMPARE(push(brisk, 0, 3000, 0, 4), -1);
        brisk.leave();
        brisk.enter(4000);
        QCOMPARE(push(brisk, 4000, 4400, 0, 6), 4150);

        dig_detect_t forgetful(needs_t{150, 100, 2, 0.04, 1});
        forgetful.enter(0);
        QCOMPARE(push(forgetful, 0, 100), -1);
        // the 110 ms apart take all 100 back
        QCOMPARE(push(forgetful, 200, 600), 360);
    }

    void a_touch_that_holds_still_rests()
    {
        rest_detect_t rest;
        rest.enter(100, 719, 1000);
        QVERIFY(rest.resting());
        QCOMPARE(rest.due_ms(), 1150);
        rest.move(102, 719, 1100);
        rest.move(98, 717, 1120);
        QCOMPARE(rest.due_ms(), 1150);
        QVERIFY(rest.fire());
        QVERIFY(!rest.resting());
        QCOMPARE(rest.due_ms(), -1);
    }

    void moving_off_the_spot_starts_the_rest_over()
    {
        rest_detect_t rest;
        rest.enter(100, 719, 0);
        rest.move(103, 719, 60);
        QCOMPARE(rest.due_ms(), 210);
        rest.move(100, 716, 90);
        QCOMPARE(rest.due_ms(), 240);
        // the spot is where it entered, not where it last was
        rest.move(104, 719, 100);
        rest.move(105, 719, 110);
        QCOMPARE(rest.due_ms(), 260);
        rest.move(101, 719, 130);
        QCOMPARE(rest.due_ms(), 260);
    }

    void a_rest_ends_on_leaving()
    {
        rest_detect_t rest;
        rest.enter(100, 719, 0);
        rest.leave();
        QVERIFY(!rest.resting());
        QCOMPARE(rest.due_ms(), -1);
        QVERIFY(!rest.fire());
        rest.move(200, 719, 50);
        QCOMPARE(rest.due_ms(), -1);
    }

    void the_screen_beside_the_docks_has_an_edge()
    {
        QCOMPARE(edges_to_dig({LEFT, RIGHT}, 0), (std::vector<bool>{false, true}));
        QCOMPARE(edges_to_dig({LEFT, RIGHT}, 1), (std::vector<bool>{true, false}));
    }

    void one_screen_has_none()
    {
        QCOMPARE(edges_to_dig({LEFT}, 0), (std::vector<bool>{false}));
    }

    void a_screen_with_another_under_it_has_none()
    {
        const box_t under = {1280, 720, 1280, 720};
        QCOMPARE(edges_to_dig({LEFT, RIGHT, under}, 0), (std::vector<bool>{false, false, true}));
    }

    void the_docks_screen_under_one_does_not_count()
    {
        const box_t under = {0, 720, 1280, 720};
        QCOMPARE(edges_to_dig({LEFT, under}, 1), (std::vector<bool>{true, false}));
    }

    void a_screen_under_part_of_one_takes_the_whole_edge()
    {
        const box_t dock = {-1920, 0, 1920, 1080};
        const box_t wide = {0, 0, 2560, 1440};
        const box_t small = {2000, 1440, 800, 600};
        QCOMPARE(edges_to_dig({dock, wide, small}, 0), (std::vector<bool>{false, false, true}));
    }

    void a_screen_only_touching_a_corner_is_not_under()
    {
        const box_t dock = {-1920, 0, 1920, 1080};
        const box_t corner = {2560, 720, 800, 600};
        QCOMPARE(edges_to_dig({dock, RIGHT, corner}, 0), (std::vector<bool>{false, true, true}));
    }

    void a_gap_of_two_rows_frees_the_edge()
    {
        const box_t dock = {-1920, 0, 1920, 1080};
        const box_t near = {1280, 721, 1280, 720};
        const box_t far = {1280, 722, 1280, 720};
        QCOMPARE(edges_to_dig({dock, RIGHT, near}, 0), (std::vector<bool>{false, false, true}));
        QCOMPARE(edges_to_dig({dock, RIGHT, far}, 0), (std::vector<bool>{false, true, true}));
    }

    void a_dock_index_outside_gives_every_free_edge()
    {
        QCOMPARE(edges_to_dig({LEFT, RIGHT}, 7), (std::vector<bool>{true, true}));
    }

    void the_bottom_row_is_one_row()
    {
        QVERIFY(on_bottom_row(RIGHT, 1900, 719.996));
        QVERIFY(on_bottom_row(RIGHT, 1280, 719));
        QVERIFY(!on_bottom_row(RIGHT, 1900, 718.9));
        QVERIFY(!on_bottom_row(RIGHT, 1279.5, 719.5));
        QVERIFY(!on_bottom_row(RIGHT, 2560, 719.5));
        QVERIFY(!on_bottom_row(RIGHT, 1900, 720));
    }
};

QTEST_GUILESS_MAIN(TestDockDig)
#include "test-dock-dig.moc"
