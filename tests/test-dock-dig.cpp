#include <QtTest>

#include "dig-detect.hpp"
#include "dig-edges.hpp"

using namespace dock_dig;

namespace
{
// pushes by dx, dy every step_ms from the move after start_ms up to end_ms
void push(dig_detect_t& detect, int64_t start_ms, int64_t end_ms, double dx = 0, double dy = 4, int64_t step_ms = 10)
{
    for (int64_t now = start_ms + step_ms; now <= end_ms; now += step_ms)
    {
        detect.move(dx, dy, now);
    }
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
        QCOMPARE(detect.due_ms(), 1250);
        push(detect, 1000, 1250);
        QCOMPARE(detect.due_ms(), 1250);
        QVERIFY(detect.fire(1250));
        QVERIFY(!detect.digging());
        QCOMPARE(detect.due_ms(), -1);
    }

    void a_push_that_stopped_does_not_dig()
    {
        dig_detect_t detect;
        detect.enter(0);
        push(detect, 0, 100);
        QVERIFY(!detect.fire(250));
        QVERIFY(detect.digging());
        QCOMPARE(detect.due_ms(), -1);
    }

    void a_move_after_a_pause_starts_over()
    {
        dig_detect_t detect;
        detect.enter(0);
        push(detect, 0, 100);
        detect.move(0, 4, 201);
        QCOMPARE(detect.due_ms(), 451);
        push(detect, 201, 451);
        QVERIFY(detect.fire(451));
    }

    void a_wait_of_just_the_gap_keeps_the_push()
    {
        dig_detect_t detect;
        detect.enter(0);
        detect.move(0, 4, 100);
        detect.move(0, 4, 200);
        QCOMPARE(detect.due_ms(), 250);
        QVERIFY(detect.fire(250));
    }

    void the_last_move_may_be_the_gap_old()
    {
        dig_detect_t early;
        early.enter(0);
        push(early, 0, 150);
        QVERIFY(early.fire(250));

        dig_detect_t stale;
        stale.enter(0);
        push(stale, 0, 140);
        QVERIFY(!stale.fire(250));
    }

    void a_push_that_leans_too_far_starts_over()
    {
        dig_detect_t detect;
        detect.enter(0);
        // 5 over 10 times 57.3 is 28.6
        detect.move(5, 10, 10);
        QCOMPARE(detect.due_ms(), 250);
        detect.move(5, 10, 20);
        QCOMPARE(detect.due_ms(), 270);
    }

    void a_lean_within_the_limit_passes()
    {
        dig_detect_t detect;
        detect.enter(0);
        // 4 over 10 times 57.3 is 22.9
        push(detect, 0, 250, 4, 10);
        QCOMPARE(detect.due_ms(), 250);
        QVERIFY(detect.fire(250));
    }

    void the_lean_is_of_the_whole_run_and_forgotten_on_a_restart()
    {
        dig_detect_t detect;
        detect.enter(0);
        detect.move(0, 100, 10);
        // 30 over 100 is within, though this move alone leans far
        detect.move(30, 0, 20);
        detect.move(0, 0, 30);
        QCOMPARE(detect.due_ms(), 250);
        // 60 over 100 is not
        detect.move(30, 0, 40);
        detect.move(0, 0, 50);
        QCOMPARE(detect.due_ms(), 300);
        detect.move(0, 10, 60);
        detect.move(0, 10, 70);
        QCOMPARE(detect.due_ms(), 300);
    }

    void a_push_the_other_way_counts_by_its_size()
    {
        dig_detect_t detect;
        detect.enter(0);
        detect.move(-5, -10, 10);
        detect.move(0, 0, 20);
        QCOMPARE(detect.due_ms(), 270);
    }

    void a_level_slide_digs_as_in_the_original()
    {
        dig_detect_t detect;
        detect.enter(0);
        push(detect, 0, 250, 6, 0);
        QVERIFY(detect.fire(250));
    }

    void leaving_ends_the_dig()
    {
        dig_detect_t detect;
        detect.enter(0);
        push(detect, 0, 200);
        detect.leave();
        QVERIFY(!detect.digging());
        QCOMPARE(detect.due_ms(), -1);
        QVERIFY(!detect.fire(250));
        detect.move(0, 4, 400);
        QCOMPARE(detect.due_ms(), -1);
    }

    void entering_again_starts_fresh()
    {
        dig_detect_t detect;
        detect.enter(0);
        detect.move(50, 10, 10);
        detect.leave();
        detect.enter(500);
        detect.move(0, 4, 510);
        QCOMPARE(detect.due_ms(), 750);
    }

    void other_needs_are_kept()
    {
        dig_detect_t detect(needs_t{400, 50, 10});
        detect.enter(0);
        QCOMPARE(detect.due_ms(), 400);
        detect.move(0, 4, 60);
        QCOMPARE(detect.due_ms(), 460);
        detect.move(1, 8, 70);
        // 1 over 8 times 57.3 is 7.2, 3 over 12 is 14.3
        detect.move(2, 4, 80);
        QCOMPARE(detect.due_ms(), 460);
        detect.move(0, 4, 90);
        QCOMPARE(detect.due_ms(), 490);
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
