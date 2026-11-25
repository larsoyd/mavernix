// the genie towards a dock icon below the window and towards one beside it

#include <QtTest>

#include <algorithm>

#include "warp-geometry.hpp"

using namespace warp;

namespace
{
const box_t WINDOW = {300, 200, 800, 600};
// a tile of a dock on the right of a 1920 wide screen, and one on the left
const box_t RIGHT_ICON = {1860, 500, 48, 48};
const box_t LEFT_ICON = {9, 500, 48, 48};
const box_t BELOW_ICON = {900, 1020, 48, 48};

double cut_length(const row_t& cut)
{
    return cut.right_x - cut.left_x;
}
} // namespace

class TestWarpGeometry : public QObject
{
    Q_OBJECT

  private slots:
    void towardsAnIconBelowItIsTheGenieAsBefore()
    {
        const shape_t shape = genie_shape(WINDOW, BELOW_ICON, 0.4, 4, toward_t::below);
        const std::vector<row_t> rows = genie_rows(WINDOW, BELOW_ICON, 0.4, 4);
        QVERIFY(!shape.columns);
        QCOMPARE(shape.rows.size(), rows.size());
        QCOMPARE(shape.rows.front().y, rows.front().y);
        QCOMPARE(shape.rows.back().left_x, rows.back().left_x);
    }

    // at the start the cuts are the window's columns, from its left edge to its right
    void towardsTheRightItStartsAsTheWindowInColumns()
    {
        const shape_t shape = genie_shape(WINDOW, RIGHT_ICON, 0, 4, toward_t::right);
        QVERIFY(shape.columns);
        QVERIFY(shape.rows.size() >= 2);
        const auto [low, high] = std::minmax_element(shape.rows.begin(), shape.rows.end(),
            [](const row_t& a, const row_t& b) { return a.y < b.y; });
        QCOMPARE(low->y, WINDOW.x);
        QCOMPARE(high->y, WINDOW.x + WINDOW.width);
        QCOMPARE(low->picture_height, 0.0);
        QCOMPARE(high->picture_height, 1.0);
        QCOMPARE(low->left_x, WINDOW.y);
        QCOMPARE(low->right_x, WINDOW.y + WINDOW.height);
    }

    // half way the funnel narrows towards the icon, and at the end all of it is in the icon
    void towardsTheRightTheFunnelNarrowsToTheIcon()
    {
        const shape_t middle = genie_shape(WINDOW, RIGHT_ICON, 0.5, 4, toward_t::right);
        const auto nearest = std::max_element(middle.rows.begin(), middle.rows.end(),
            [](const row_t& a, const row_t& b) { return a.y < b.y; });
        const auto farthest = std::min_element(middle.rows.begin(), middle.rows.end(),
            [](const row_t& a, const row_t& b) { return a.y < b.y; });
        QVERIFY2(cut_length(*nearest) < cut_length(*farthest),
            qPrintable(QStringLiteral("near %1 far %2").arg(cut_length(*nearest)).arg(cut_length(*farthest))));
        const shape_t end = genie_shape(WINDOW, RIGHT_ICON, 1, 4, toward_t::right);
        for (const row_t& cut : end.rows)
        {
            QVERIFY(cut.y >= RIGHT_ICON.x - 0.01 && cut.y <= RIGHT_ICON.x + RIGHT_ICON.width + 0.01);
            QVERIFY(cut.left_x >= RIGHT_ICON.y - 0.01 && cut.right_x <= RIGHT_ICON.y + RIGHT_ICON.height + 0.01);
        }
    }

    // to the left the window's right edge goes last, and the picture still runs left to right
    void towardsTheLeftItMirrors()
    {
        const shape_t start = genie_shape(WINDOW, LEFT_ICON, 0, 4, toward_t::left);
        QVERIFY(start.columns);
        for (const row_t& cut : start.rows)
        {
            QCOMPARE(cut.picture_height, (cut.y - WINDOW.x) / WINDOW.width);
        }
        const shape_t middle = genie_shape(WINDOW, LEFT_ICON, 0.5, 4, toward_t::left);
        const auto nearest = std::min_element(middle.rows.begin(), middle.rows.end(),
            [](const row_t& a, const row_t& b) { return a.y < b.y; });
        const auto farthest = std::max_element(middle.rows.begin(), middle.rows.end(),
            [](const row_t& a, const row_t& b) { return a.y < b.y; });
        QVERIFY(cut_length(*nearest) < cut_length(*farthest));
        const shape_t end = genie_shape(WINDOW, LEFT_ICON, 1, 4, toward_t::left);
        for (const row_t& cut : end.rows)
        {
            QVERIFY(cut.y >= LEFT_ICON.x - 0.01 && cut.y <= LEFT_ICON.x + LEFT_ICON.width + 0.01);
        }
    }

    // the scale shape goes straight to the icon whichever way it lies
    void theScaleShapeKeepsItsRows()
    {
        const shape_t shape = scale_shape(WINDOW, RIGHT_ICON, 1);
        QVERIFY(!shape.columns);
        QCOMPARE(shape.rows.size(), size_t(2));
        QCOMPARE(shape.rows[0].left_x, RIGHT_ICON.x);
    }
};

QTEST_GUILESS_MAIN(TestWarpGeometry)
#include "test-warp-geometry.moc"
