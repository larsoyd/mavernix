// the plate of layouts shown while a window is dragged to the top
// its place, its states and the zone picked

#include <QtTest>

#include "zone-layout.hpp"

using namespace zone_picker;

namespace
{
const char* DEFAULTS =
    "Left Half,0,0,50,100;Right Half,50,0,50,100|"
    "Whole Screen,0,0,100,100|"
    "Left Third,0,0,33.3333,100;Middle Third,33.3333,0,33.3334,100;Right Third,66.6667,0,33.3333,100|"
    "Upper Left,0,0,50,50;Upper Right,50,0,50,50;Lower Left,0,50,50,50;Lower Right,50,50,50,50";

const box_t WORKAREA = {0, 22, 1920, 1000};

plate_t plate_on(int width, int height, const std::vector<layout_t>& layouts, metrics_t metrics = {})
{
    return lay_out(width, height, {0, 22, width, height - 80}, layouts, metrics);
}
} // namespace

class TestZonePicker : public QObject
{
    Q_OBJECT

  private slots:
    void layouts_are_read_from_text()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        QCOMPARE(layouts.size(), size_t(4));
        QCOMPARE(layouts[0].size(), size_t(2));
        QCOMPARE(layouts[2].size(), size_t(3));
        QCOMPARE(QString::fromStdString(layouts[2][1].name), QStringLiteral("Middle Third"));
        QCOMPARE(layouts[0][1].x, 0.5);
        QCOMPARE(layouts[0][1].width, 0.5);
        QCOMPARE(layouts[3][3].y, 0.5);
    }

    void broken_zones_are_left_out()
    {
        QCOMPARE(parse_layouts("").size(), size_t(0));
        QCOMPARE(parse_layouts("|;|").size(), size_t(0));
        QCOMPARE(parse_layouts("A,0,0,50").size(), size_t(0));
        QCOMPARE(parse_layouts("A,0,0,0,100").size(), size_t(0));
        QCOMPARE(parse_layouts("A,60,0,50,100").size(), size_t(0));
        QCOMPARE(parse_layouts("A,0,60,50,50").size(), size_t(0));
        QCOMPARE(parse_layouts("A,-1,0,50,100").size(), size_t(0));
        QCOMPARE(parse_layouts("A,x,0,50,100").size(), size_t(0));
        QCOMPARE(parse_layouts("A,0,0,50,100,7").size(), size_t(0));
        QCOMPARE(parse_layouts("A,0,0,50,1e999").size(), size_t(0));
        const auto mixed = parse_layouts("A,0,0,50,100;B,0,0,0,0|C,0,0,100,100");
        QCOMPARE(mixed.size(), size_t(2));
        QCOMPARE(mixed[0].size(), size_t(1));
    }

    void the_plate_hangs_centred_below_the_bar()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const metrics_t metrics;
        const auto plate = plate_on(1920, 1080, layouts);
        // a twentieth of 1920 wide, in the screen's own shape
        QCOMPARE(plate.minis.size(), size_t(4));
        QCOMPARE(plate.minis[0].box.width, 96);
        QCOMPARE(plate.minis[0].box.height, 54);
        const int width = 2 * metrics.padding + 4 * 96 + 3 * metrics.gap;
        QCOMPARE(plate.box, (box_t{(1920 - width) / 2, 22, width, 2 * metrics.padding + 54}));
        QCOMPARE(plate.minis[0].box.x, plate.box.x + metrics.padding);
        QCOMPARE(plate.minis[0].box.y, 22 + metrics.padding);
        QCOMPARE(plate.minis[1].box.x, plate.minis[0].box.x + 96 + metrics.gap);
        // the same room either side of the row
        QCOMPARE(plate.box.x + plate.box.width - (plate.minis[3].box.x + 96), metrics.padding);
    }

    void a_wide_screen_gets_wide_minis_up_to_a_limit()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const auto wide = plate_on(3440, 1440, layouts);
        QCOMPARE(wide.minis[0].box.width, 160);
        QCOMPARE(wide.minis[0].box.height, 67);
        const auto small = plate_on(1200, 720, layouts);
        QCOMPARE(small.minis[0].box.width, 64);
        QCOMPARE(small.minis[0].box.height, 38);
    }

    void cells_fill_their_mini_with_a_gutter()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const auto plate = plate_on(1920, 1080, layouts);
        const box_t mini = plate.minis[0].box;
        const auto& cells = plate.minis[0].cells;
        QCOMPARE(cells.size(), size_t(2));
        QCOMPARE(cells[0], (box_t{mini.x, mini.y, 47, 54}));
        QCOMPARE(cells[1], (box_t{mini.x + 49, mini.y, 47, 54}));
        const auto& quarters = plate.minis[3].cells;
        QCOMPARE(quarters[3], (box_t{plate.minis[3].box.x + 49, mini.y + 28, 47, 26}));
        const auto& whole = plate.minis[1].cells;
        QCOMPARE(whole[0], plate.minis[1].box);
    }

    void a_narrow_screen_shrinks_then_drops_layouts()
    {
        std::string many;
        for (int i = 0; i < 20; i++)
        {
            many += "Whole,0,0,100,100|";
        }
        const auto layouts = parse_layouts(many);
        const metrics_t metrics;
        // 20 at 96 need 2218 of 1920: less the room at both sides, both paddings
        // and nineteen gaps, 1590 is shared by twenty
        const auto shrunk = plate_on(1920, 1080, layouts);
        QCOMPARE(shrunk.minis.size(), size_t(20));
        QCOMPARE(shrunk.minis[0].box.width, 79);
        QCOMPARE(shrunk.minis[0].box.height, 44);
        QVERIFY(shrunk.box.x >= metrics.padding);
        QVERIFY(shrunk.box.x + shrunk.box.width <= 1920 - metrics.padding);

        const auto dropped = plate_on(670, 480, layouts);
        // eight need 642, and 670 less the room at both sides is 638
        QCOMPARE(dropped.minis.size(), size_t(7));
        QCOMPARE(dropped.minis[0].box.width, metrics.mini_min);
        QVERIFY(dropped.box.x >= metrics.padding);
        QVERIFY(dropped.box.x + dropped.box.width <= 670 - metrics.padding);

        const auto none = plate_on(1920, 1080, {});
        QCOMPARE(none.minis.size(), size_t(0));
        QCOMPARE(none.box.width, 0);
    }

    void the_plate_wakes_near_the_top_and_opens_at_the_strip()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const metrics_t metrics;
        const auto plate = plate_on(1920, 1080, layouts);
        const int middle = plate.box.x + plate.box.width / 2;

        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, middle, 400), state_t::HIDDEN);
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, middle, 22 + 75), state_t::HIDDEN);
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, middle, 22 + 74), state_t::PEEK);
        // far to the side nothing wakes
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, plate.box.x - 76, 40), state_t::HIDDEN);
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, plate.box.x - 74, 40), state_t::PEEK);
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, plate.box.x + plate.box.width + 76, 40), state_t::HIDDEN);

        QCOMPARE(next_state(state_t::PEEK, plate, metrics, middle, 22 + 30), state_t::PEEK);
        QCOMPARE(next_state(state_t::PEEK, plate, metrics, middle, 22 + 29), state_t::OPEN);
        QCOMPARE(next_state(state_t::PEEK, plate, metrics, plate.box.x - 1, 22 + 10), state_t::PEEK);
        // a quick pointer reaches the strip in one step
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, middle, 22 + 5), state_t::OPEN);
        // on the bar above the plate it only peeks
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, middle, 15), state_t::PEEK);
    }

    void an_open_plate_stays_until_the_pointer_strays()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const metrics_t metrics;
        const auto plate = plate_on(1920, 1080, layouts);
        const int middle = plate.box.x + plate.box.width / 2;
        const int bottom = plate.box.y + plate.box.height;

        QCOMPARE(next_state(state_t::OPEN, plate, metrics, middle, bottom - 1), state_t::OPEN);
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, middle, bottom + 9), state_t::OPEN);
        // the plate is taller than its waking distance, so below it nothing is left to peek for
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, middle, bottom + 10), state_t::HIDDEN);
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, plate.box.x - 10, 40), state_t::OPEN);
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, plate.box.x - 11, 40), state_t::PEEK);
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, middle, 12), state_t::OPEN);
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, middle, 11), state_t::PEEK);
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, middle, 500), state_t::HIDDEN);
    }

    void a_woken_plate_sleeps_a_little_later_than_it_woke()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const metrics_t metrics;
        const auto plate = plate_on(1920, 1080, layouts);
        const int middle = plate.box.x + plate.box.width / 2;
        QCOMPARE(next_state(state_t::PEEK, plate, metrics, middle, 22 + 75 + 9), state_t::PEEK);
        QCOMPARE(next_state(state_t::PEEK, plate, metrics, middle, 22 + 75 + 10), state_t::HIDDEN);
        QCOMPARE(next_state(state_t::PEEK, plate, metrics, plate.box.x - 84, 40), state_t::PEEK);
        QCOMPARE(next_state(state_t::PEEK, plate, metrics, plate.box.x - 86, 40), state_t::HIDDEN);
    }

    void nothing_wakes_without_layouts()
    {
        const metrics_t metrics;
        const auto plate = plate_on(1920, 1080, {});
        QCOMPARE(next_state(state_t::HIDDEN, plate, metrics, 960, 30), state_t::HIDDEN);
        QCOMPARE(next_state(state_t::OPEN, plate, metrics, 960, 30), state_t::HIDDEN);
    }

    void the_pointer_picks_the_zone_under_it()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const auto plate = plate_on(1920, 1080, layouts);
        const box_t halves = plate.minis[0].box;
        QCOMPARE(zone_at(plate, layouts, halves.x, halves.y), (pick_t{0, 0}));
        QCOMPARE(zone_at(plate, layouts, halves.x + 47, halves.y + 20), (pick_t{0, 0}));
        // the gutter belongs to a zone too
        QCOMPARE(zone_at(plate, layouts, halves.x + 48, halves.y + 20), (pick_t{0, 1}));
        QCOMPARE(zone_at(plate, layouts, halves.x + 95, halves.y + 53), (pick_t{0, 1}));
        QVERIFY(!zone_at(plate, layouts, halves.x + 96, halves.y + 20).any());
        QVERIFY(!zone_at(plate, layouts, halves.x - 1, halves.y + 20).any());
        QVERIFY(!zone_at(plate, layouts, halves.x + 10, halves.y - 1).any());
        QVERIFY(!zone_at(plate, layouts, halves.x + 10, halves.y + 54).any());
        const box_t quarters = plate.minis[3].box;
        QCOMPARE(zone_at(plate, layouts, quarters.x + 60, quarters.y + 40), (pick_t{3, 3}));
        QCOMPARE(zone_at(plate, layouts, quarters.x + 60, quarters.y + 10), (pick_t{3, 1}));
        const box_t thirds = plate.minis[2].box;
        QCOMPARE(zone_at(plate, layouts, thirds.x + 48, thirds.y + 10), (pick_t{2, 1}));
    }

    void overlapping_zones_give_the_later_one()
    {
        const auto layouts = parse_layouts("Wide,0,0,100,100;Middle,25,0,50,100");
        const auto plate = plate_on(1920, 1080, layouts);
        const box_t mini = plate.minis[0].box;
        QCOMPARE(zone_at(plate, layouts, mini.x + 48, mini.y + 10), (pick_t{0, 1}));
        QCOMPARE(zone_at(plate, layouts, mini.x + 5, mini.y + 10), (pick_t{0, 0}));
    }

    void targets_meet_on_the_same_pixel()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        const box_t odd = {5, 22, 1921, 999};
        const auto left = target_box(odd, layouts[2][0]);
        const auto middle = target_box(odd, layouts[2][1]);
        const auto right = target_box(odd, layouts[2][2]);
        QCOMPARE(left.x, 5);
        QCOMPARE(left.x + left.width, middle.x);
        QCOMPARE(middle.x + middle.width, right.x);
        QCOMPARE(right.x + right.width, 5 + 1921);
        QCOMPARE(left.y, 22);
        QCOMPARE(left.height, 999);
        QCOMPARE(left.width, 640);
        QCOMPARE(middle.width, 641);

        QCOMPARE(target_box(WORKAREA, layouts[1][0]), WORKAREA);
        QCOMPARE(target_box(WORKAREA, layouts[3][3]), (box_t{960, 522, 960, 500}));
    }

    void edges_follow_the_sides_a_zone_touches()
    {
        const auto layouts = parse_layouts(DEFAULTS);
        QCOMPARE(tiled_edges(layouts[0][0]), EDGE_TOP | EDGE_BOTTOM | EDGE_LEFT);
        QCOMPARE(tiled_edges(layouts[0][1]), EDGE_TOP | EDGE_BOTTOM | EDGE_RIGHT);
        QCOMPARE(tiled_edges(layouts[1][0]), EDGES_ALL);
        QCOMPARE(tiled_edges(layouts[2][1]), EDGE_TOP | EDGE_BOTTOM);
        QCOMPARE(tiled_edges(layouts[2][2]), EDGE_TOP | EDGE_BOTTOM | EDGE_RIGHT);
        QCOMPARE(tiled_edges(layouts[3][2]), EDGE_BOTTOM | EDGE_LEFT);
        QCOMPARE(tiled_edges(parse_layouts("Island,25,25,50,50")[0][0]), uint32_t(0));
    }
};

QTEST_GUILESS_MAIN(TestZonePicker)
#include "test-zone-picker.moc"
