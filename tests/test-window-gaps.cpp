// room left around tiled windows, the box a tile gets and the plain box it came from

#include <QtTest>

#include "gap-math.hpp"

using namespace window_gaps;

namespace
{
// a work area below a 22 px menu bar and above a 60 px dock, on a second screen
const box_t AREA = {1920, 22, 1000, 518};

box_t share(double x, double y, double width, double height)
{
    const int left = AREA.x + int(AREA.width * x);
    const int top = AREA.y + int(AREA.height * y);
    return {left, top, AREA.x + int(AREA.width * (x + width)) - left, AREA.y + int(AREA.height * (y + height)) - top};
}

char* text(const box_t& box)
{
    return QTest::toString(QStringLiteral("%1,%2 %3x%4").arg(box.x).arg(box.y).arg(box.width).arg(box.height));
}
} // namespace

namespace QTest
{
template <> char* toString(const box_t& box) { return text(box); }
} // namespace QTest

class TestWindowGaps : public QObject
{
    Q_OBJECT

  private slots:
    void maximized_keeps_the_gap_on_every_side()
    {
        QCOMPARE(gapped(AREA, AREA, 12), (box_t{1932, 34, 976, 494}));
    }

    void halves_sit_one_gap_apart()
    {
        const box_t left = gapped(share(0, 0, 0.5, 1), AREA, 12);
        const box_t right = gapped(share(0.5, 0, 0.5, 1), AREA, 12);
        QCOMPARE(left, (box_t{1932, 34, 482, 494}));
        QCOMPARE(right, (box_t{2426, 34, 482, 494}));
        QCOMPARE(right.x - (left.x + left.width), 12);
    }

    void an_odd_gap_stays_whole_between_tiles()
    {
        const box_t left = gapped(share(0, 0, 0.5, 1), AREA, 7);
        const box_t right = gapped(share(0.5, 0, 0.5, 1), AREA, 7);
        QCOMPARE(right.x - (left.x + left.width), 7);
        QCOMPARE(left.x - AREA.x, 7);
        QCOMPARE(AREA.x + AREA.width - (right.x + right.width), 7);
    }

    void quarters_meet_one_gap_apart_both_ways()
    {
        const box_t upper_left = gapped(share(0, 0, 0.5, 0.5), AREA, 10);
        const box_t lower_right = gapped(share(0.5, 0.5, 0.5, 0.5), AREA, 10);
        QCOMPARE(upper_left, (box_t{1930, 32, 485, 244}));
        QCOMPARE(lower_right, (box_t{2425, 286, 485, 244}));
    }

    void a_middle_third_has_inner_gaps_at_both_sides()
    {
        const box_t middle = gapped(share(1.0 / 3, 0, 1.0 / 3, 1), AREA, 12);
        QCOMPARE(middle, (box_t{2259, 34, 321, 494}));
    }

    void no_gap_leaves_the_box_alone()
    {
        const box_t half = share(0, 0, 0.5, 1);
        QCOMPARE(gapped(half, AREA, 0), half);
        QCOMPARE(plain(half, AREA, 0), half);
    }

    void plain_undoes_gapped()
    {
        for (int gap : {2, 7, 12, 48})
        {
            for (const box_t& box : {AREA, share(0, 0, 0.5, 1), share(0.5, 0.5, 0.5, 0.5),
                     share(1.0 / 3, 0, 1.0 / 3, 1), share(0, 0, 2.0 / 3, 1)})
            {
                QCOMPARE(plain(gapped(box, AREA, gap), AREA, gap), box);
            }
        }
    }

    void plain_finds_the_border_past_a_client_short_of_its_box()
    {
        // a terminal sized in whole cells ends a few pixels before its tile does
        box_t right = gapped(share(0.5, 0, 0.5, 1), AREA, 12);
        right.width -= 5;
        right.height -= 3;
        const box_t found = plain(right, AREA, 12);
        QCOMPARE(found.x + found.width, AREA.x + AREA.width);
        QCOMPARE(found.y + found.height, AREA.y + AREA.height);
    }

    void plain_keeps_neighbours_sharing_one_edge()
    {
        const box_t left = plain(gapped(share(0, 0, 0.5, 1), AREA, 12), AREA, 12);
        const box_t right = plain(gapped(share(0.5, 0, 0.5, 1), AREA, 12), AREA, 12);
        QCOMPARE(left.x + left.width, right.x);
    }
};

QTEST_GUILESS_MAIN(TestWindowGaps)
#include "test-window-gaps.moc"
