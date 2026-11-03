// Dragging the edge that tiled windows share (src/compositor/shared-edges):
// which windows are on a seam, how far it may move, and where they end up.

#include <QtTest>

#include "seam-math.hpp"

using namespace shared_edges;

namespace
{
// How far apart two edges may be and still count as one seam.
const int TOLERANCE_PX = 4;
// The least width or height a window is given when it sets none itself.
const int FLOOR_PX = 80;
// the work area every test lays its windows in
const box_t AREA = {0, 0, 1000, 600};

window_t window(int x, int y, int width, int height, int min_width = 100, int min_height = 100)
{
    window_t made;
    made.box = {x, y, width, height};
    made.min_width = min_width;
    made.min_height = min_height;
    return made;
}

bool same(const box_t& a, const box_t& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
} // namespace

class TestSharedEdges : public QObject
{
    Q_OBJECT

  private slots:
    void two_halves()
    {
        const std::vector<window_t> halves = {window(0, 0, 500, 600), window(500, 0, 500, 600)};
        const auto seam = find_seam(halves, 0, side_t::RIGHT, TOLERANCE_PX);
        QVERIFY(seam.has_value());
        QVERIFY(seam->vertical);
        QCOMPARE(seam->before.size(), std::size_t(1));
        QCOMPARE(seam->after.size(), std::size_t(1));

        // The same seam from the other side; none at the screen's edge or across.
        QVERIFY(find_seam(halves, 1, side_t::LEFT, TOLERANCE_PX).has_value());
        QVERIFY(!find_seam(halves, 0, side_t::LEFT, TOLERANCE_PX).has_value());
        QVERIFY(!find_seam(halves, 0, side_t::BOTTOM, TOLERANCE_PX).has_value());

        QVERIFY(same(moved_box(halves[0], *seam, 0, 100), {0, 0, 600, 600}));
        QVERIFY(same(moved_box(halves[1], *seam, 1, 100), {600, 0, 400, 600}));

        // Held where either window would get under its least width of 100.
        QCOMPARE(limit_move(halves, *seam, 800, FLOOR_PX, AREA), 400);
        QCOMPARE(limit_move(halves, *seam, -800, FLOOR_PX, AREA), -400);
        QCOMPARE(limit_move(halves, *seam, 37, FLOOR_PX, AREA), 37);
    }

    void one_tall_and_two_stacked()
    {
        const std::vector<window_t> three = {
            window(0, 0, 500, 600), window(500, 0, 500, 300), window(500, 300, 500, 300)};

        // However it is picked up, the seam beside the tall one moves all three.
        for (const auto& seam :
            {find_seam(three, 1, side_t::LEFT, TOLERANCE_PX), find_seam(three, 0, side_t::RIGHT, TOLERANCE_PX)})
        {
            QVERIFY(seam.has_value());
            QCOMPARE(seam->before.size(), std::size_t(1));
            QCOMPARE(seam->after.size(), std::size_t(2));
        }

        // The line between the stacked two is a seam of its own, without the tall one.
        const auto across = find_seam(three, 1, side_t::BOTTOM, TOLERANCE_PX);
        QVERIFY(across.has_value());
        QVERIFY(!across->vertical);
        QCOMPARE(across->before, std::vector<std::size_t>{1});
        QCOMPARE(across->after, std::vector<std::size_t>{2});
    }

    void windows_apart_do_not_move_together()
    {
        const std::vector<window_t> apart = {
            window(0, 0, 500, 200), window(500, 0, 500, 200), window(0, 400, 500, 200), window(500, 400, 500, 200)};
        const auto seam = find_seam(apart, 0, side_t::RIGHT, TOLERANCE_PX);
        QVERIFY(seam.has_value());
        QCOMPARE(seam->before.size(), std::size_t(1));
        QCOMPARE(seam->after.size(), std::size_t(1));
    }

    void a_small_gap_is_still_a_seam()
    {
        const std::vector<window_t> near = {window(0, 0, 500, 600), window(502, 0, 498, 600)};
        QVERIFY(find_seam(near, 0, side_t::RIGHT, TOLERANCE_PX).has_value());
        const std::vector<window_t> far = {window(0, 0, 500, 600), window(520, 0, 480, 600)};
        QVERIFY(!find_seam(far, 0, side_t::RIGHT, TOLERANCE_PX).has_value());
    }

    void windows_keep_their_own_size_limits()
    {
        std::vector<window_t> limited = {window(0, 0, 500, 600, 300), window(500, 0, 500, 600, 100)};
        const auto seam = find_seam(limited, 0, side_t::RIGHT, TOLERANCE_PX);
        QVERIFY(seam.has_value());
        QCOMPARE(limit_move(limited, *seam, -400, FLOOR_PX, AREA), -200);

        limited[1].max_width = 600;
        QCOMPARE(limit_move(limited, *seam, -400, FLOOR_PX, AREA), -100);

        limited[0].min_width = 100;
        QCOMPARE(limit_move(limited, *seam, -400, FLOOR_PX, AREA), -100);

        limited[1].max_width = 0;
        QCOMPARE(limit_move(limited, *seam, 800, FLOOR_PX, AREA), 400);
    }

    void stacked_windows_move_up_and_down()
    {
        const std::vector<window_t> stacked = {window(0, 0, 1000, 300), window(0, 300, 1000, 300)};
        const auto seam = find_seam(stacked, 1, side_t::TOP, TOLERANCE_PX);
        QVERIFY(seam.has_value());
        QVERIFY(!seam->vertical);
        QVERIFY(same(moved_box(stacked[0], *seam, 0, 50), {0, 0, 1000, 350}));
        QVERIFY(same(moved_box(stacked[1], *seam, 1, 50), {0, 350, 1000, 250}));
    }

    void a_lone_tile_moves_its_free_edge_inside_the_work_area()
    {
        const std::vector<window_t> lone = {window(0, 0, 500, 600)};
        QVERIFY(!find_seam(lone, 0, side_t::RIGHT, TOLERANCE_PX).has_value());

        const auto seam = find_seam(lone, 0, side_t::RIGHT, TOLERANCE_PX, true);
        QVERIFY(seam.has_value());
        QCOMPARE(seam->before, std::vector<std::size_t>{0});
        QVERIFY(seam->after.empty());
        QCOMPARE(limit_move(lone, *seam, 900, FLOOR_PX, AREA), 500);
        QCOMPARE(limit_move(lone, *seam, -900, FLOOR_PX, AREA), -400);
        QVERIFY(same(moved_box(lone[0], *seam, 0, 200), {0, 0, 700, 600}));

        const std::vector<window_t> right = {window(500, 0, 500, 600)};
        const auto left_seam = find_seam(right, 0, side_t::LEFT, TOLERANCE_PX, true);
        QVERIFY(left_seam.has_value());
        QCOMPARE(limit_move(right, *left_seam, -900, FLOOR_PX, AREA), -500);
        QVERIFY(same(moved_box(right[0], *left_seam, 0, -100), {400, 0, 600, 600}));
    }

    void a_seam_stays_in_the_work_area()
    {
        // a neighbour that sticks out past the screen must not pull the seam after it
        const std::vector<window_t> wide = {window(0, 0, 500, 600), window(500, 0, 900, 600)};
        const auto seam = find_seam(wide, 0, side_t::RIGHT, TOLERANCE_PX);
        QVERIFY(seam.has_value());
        QCOMPARE(limit_move(wide, *seam, 700, FLOOR_PX, AREA), 500);
        QCOMPARE(limit_move(wide, *seam, -700, FLOOR_PX, AREA), -400);
    }

    void a_held_resize_drags_the_free_edges_on_the_pressed_side()
    {
        const box_t left_half = {0, 0, 500, 600};
        const uint32_t left_tiled = EDGE_LEFT | EDGE_TOP | EDGE_BOTTOM;
        // wherever it is pressed, a left half has only its right edge to give
        QCOMPARE(held_edges(left_half, left_tiled, 50, 50), uint32_t(EDGE_RIGHT));
        QCOMPARE(held_edges(left_half, left_tiled, 450, 550), uint32_t(EDGE_RIGHT));

        const box_t top_left = {0, 0, 500, 300};
        QCOMPARE(held_edges(top_left, EDGE_LEFT | EDGE_TOP, 10, 10), uint32_t(EDGE_RIGHT | EDGE_BOTTOM));

        // a tile in the middle column takes the side under the pointer
        const box_t middle = {300, 0, 400, 600};
        QCOMPARE(held_edges(middle, EDGE_TOP | EDGE_BOTTOM, 320, 300), uint32_t(EDGE_LEFT));
        QCOMPARE(held_edges(middle, EDGE_TOP | EDGE_BOTTOM, 680, 300), uint32_t(EDGE_RIGHT));

        QCOMPARE(held_edges(left_half, EDGE_LEFT | EDGE_RIGHT | EDGE_TOP | EDGE_BOTTOM, 200, 200), uint32_t(0));
    }

    void dragged_sides_split_a_corner_into_its_free_edges()
    {
        const auto sides = dragged_sides(EDGE_RIGHT | EDGE_BOTTOM, EDGE_LEFT | EDGE_TOP);
        QCOMPARE(sides.size(), std::size_t(2));
        QVERIFY(sides[0].side == side_t::RIGHT && sides[0].free);
        QVERIFY(sides[1].side == side_t::BOTTOM && sides[1].free);

        // an edge on the screen's border cannot move, so only the other one is dragged
        const auto half = dragged_sides(EDGE_RIGHT | EDGE_BOTTOM, EDGE_LEFT | EDGE_TOP | EDGE_BOTTOM);
        QCOMPARE(half.size(), std::size_t(1));
        QVERIFY(half[0].side == side_t::RIGHT && half[0].free);

        const auto stuck = dragged_sides(EDGE_LEFT, EDGE_LEFT | EDGE_TOP | EDGE_BOTTOM);
        QCOMPARE(stuck.size(), std::size_t(1));
        QVERIFY(stuck[0].side == side_t::LEFT && !stuck[0].free);
    }

    void a_new_tile_takes_the_room_its_neighbour_leaves()
    {
        const box_t right_half = {500, 0, 500, 600};
        const uint32_t right_edges = EDGE_RIGHT | EDGE_TOP | EDGE_BOTTOM;
        const tile_t wide_left = {{0, 0, 700, 600}, EDGE_LEFT | EDGE_TOP | EDGE_BOTTOM};
        QVERIFY(same(fit_beside(right_half, right_edges, {wide_left}, FLOOR_PX), {700, 0, 300, 600}));

        const tile_t narrow_left = {{0, 0, 300, 600}, EDGE_LEFT | EDGE_TOP | EDGE_BOTTOM};
        QVERIFY(same(fit_beside(right_half, right_edges, {narrow_left}, FLOOR_PX), {300, 0, 700, 600}));

        const box_t left_half = {0, 0, 500, 600};
        const tile_t narrow_right = {{800, 0, 200, 600}, EDGE_RIGHT | EDGE_TOP | EDGE_BOTTOM};
        QVERIFY(same(
            fit_beside(left_half, EDGE_LEFT | EDGE_TOP | EDGE_BOTTOM, {narrow_right}, FLOOR_PX), {0, 0, 800, 600}));
    }

    void a_new_tile_ignores_windows_that_are_not_across_from_it()
    {
        const box_t right_half = {500, 0, 500, 600};
        const uint32_t right_edges = EDGE_RIGHT | EDGE_TOP | EDGE_BOTTOM;
        // full width, on the same side, or too wide to leave room
        const tile_t top_half = {{0, 0, 1000, 300}, EDGE_LEFT | EDGE_RIGHT | EDGE_TOP};
        const tile_t also_right = {{600, 0, 400, 600}, right_edges};
        const tile_t too_wide = {{0, 0, 950, 600}, EDGE_LEFT | EDGE_TOP | EDGE_BOTTOM};
        for (const tile_t& other : {top_half, also_right, too_wide})
        {
            QVERIFY(same(fit_beside(right_half, right_edges, {other}, FLOOR_PX), right_half));
        }

        // a left tile in the other row does not touch a top right quarter
        const tile_t bottom_left = {{0, 300, 700, 300}, EDGE_LEFT | EDGE_BOTTOM};
        const box_t top_right = {500, 0, 500, 300};
        QVERIFY(same(fit_beside(top_right, EDGE_RIGHT | EDGE_TOP, {bottom_left}, FLOOR_PX), top_right));
    }

    void a_new_quarter_fits_both_ways()
    {
        const box_t bottom_right = {500, 300, 500, 300};
        const tile_t top_right = {{500, 0, 500, 200}, EDGE_RIGHT | EDGE_TOP};
        const tile_t bottom_left = {{0, 300, 600, 300}, EDGE_LEFT | EDGE_BOTTOM};
        QVERIFY(same(fit_beside(bottom_right, EDGE_RIGHT | EDGE_BOTTOM, {top_right, bottom_left}, FLOOR_PX),
            {600, 200, 400, 400}));
    }
};

QTEST_APPLESS_MAIN(TestSharedEdges)
#include "test-shared-edges.moc"
