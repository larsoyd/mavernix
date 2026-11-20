#include <QtTest>

#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <wayland-server-protocol.h>

#include "title-text.hpp"

namespace
{
struct pixel_t
{
    int alpha, red, green, blue;
};

// a drawn title, freed with the test's scope
class picture_t
{
  public:
    picture_t(const themed::window_theme_t& theme, const char *text, double scale, bool active,
        int width = 400, int height = 80) :
        surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width * scale, height * scale)),
        width(cairo_image_surface_get_width(surface)), height(cairo_image_surface_get_height(surface))
    {
        themed::draw_title(surface, theme, text, {0, 0, this->width, this->height}, scale, active,
            this->width / 2.0);
        cairo_surface_flush(surface);
    }

    ~picture_t() { cairo_surface_destroy(surface); }

    bool drawn() const { return cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS; }

    pixel_t at(int x, int y) const
    {
        const unsigned char *row = cairo_image_surface_get_data(surface) + y * cairo_image_surface_get_stride(surface);
        const uint32_t value = *reinterpret_cast<const uint32_t*>(row + 4 * x);
        return {int(value >> 24), int((value >> 16) & 255), int((value >> 8) & 255), int(value & 255)};
    }

    // rows that hold any ink, first and last, -1 when there is none
    int first_row() const { return row_from(0, height, 1); }
    int last_row() const { return row_from(height - 1, -1, -1); }

    // columns of a row whose pixel is fully covered
    QList<int> solid_columns(int y) const
    {
        QList<int> columns;
        for (int x = 0; x < width; x++)
        {
            if (at(x, y).alpha == 255)
            {
                columns << x;
            }
        }
        return columns;
    }

    cairo_surface_t *surface;
    int width, height;

  private:
    int row_from(int start, int stop, int step) const
    {
        for (int y = start; y != stop; y += step)
        {
            for (int x = 0; x < width; x++)
            {
                if (at(x, y).alpha > 0)
                {
                    return y;
                }
            }
        }
        return -1;
    }
};

// what a title leaves on a flat grey bar, summed over the three channels
struct ink_t
{
    QList<int> rows;
    int total = 0, width = 0, fringed = 0;
    // red over blue where brightness climbs to the right, so its sign says which way fringes lean
    long lean = 0;
    // where the ink balances across, in pixels from the left edge
    double centre = 0;
};

// an opaque bar of one grey, the title drawn onto it as the frame does
class bar_t
{
  public:
    bar_t(int width, int height, int level) :
        surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height)), level(level)
    {
        cairo_t *cr = cairo_create(surface);
        cairo_set_source_rgb(cr, level / 255.0, level / 255.0, level / 255.0);
        cairo_paint(cr);
        cairo_destroy(cr);
    }

    ~bar_t() { cairo_surface_destroy(surface); }

    void draw(const themed::window_theme_t& theme, const char *text, double scale, bool active,
        themed::subpixel_t order, cairo_rectangle_int_t box = {0, 0, -1, -1}) const
    {
        if (box.width < 0)
        {
            box = {0, 0, cairo_image_surface_get_width(surface), cairo_image_surface_get_height(surface)};
        }
        themed::draw_title(surface, theme, text, box, scale, active, box.width / 2.0, order);
        cairo_surface_flush(surface);
    }

    pixel_t at(int x, int y) const
    {
        const unsigned char *row = cairo_image_surface_get_data(surface) + y * cairo_image_surface_get_stride(surface);
        const uint32_t value = *reinterpret_cast<const uint32_t*>(row + 4 * x);
        return {int(value >> 24), int((value >> 16) & 255), int((value >> 8) & 255), int(value & 255)};
    }

    ink_t measure() const
    {
        const int width = cairo_image_surface_get_width(surface), height = cairo_image_surface_get_height(surface);
        ink_t ink;
        QList<int> rows;
        int left = width, right = -1;
        for (int y = 0; y < height; y++)
        {
            int row = 0;
            for (int x = 0; x < width; x++)
            {
                const pixel_t pixel = at(x, y);
                const int off = qAbs(pixel.red - level) + qAbs(pixel.green - level) + qAbs(pixel.blue - level);
                if (off > 0)
                {
                    left  = qMin(left, x);
                    right = qMax(right, x);
                }
                ink.fringed += qAbs(pixel.red - pixel.blue) > 40;
                if ((x > 0) && (x + 1 < width))
                {
                    const pixel_t before = at(x - 1, y), after = at(x + 1, y);
                    const int climb = (after.red + after.green + after.blue) - (before.red + before.green + before.blue);
                    ink.lean += (pixel.red - pixel.blue) * ((climb > 0) - (climb < 0));
                }
                row += off;
            }
            rows << row;
            ink.total += row;
        }
        while (!rows.isEmpty() && rows.first() == 0)
        {
            rows.removeFirst();
        }
        while (!rows.isEmpty() && rows.last() == 0)
        {
            rows.removeLast();
        }
        ink.rows  = rows;
        ink.width = right - left + 1;
        double moment = 0;
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                const pixel_t pixel = at(x, y);
                moment += (x + 0.5) * (qAbs(pixel.red - level) + qAbs(pixel.green - level) + qAbs(pixel.blue - level));
            }
        }
        ink.centre = ink.total ? moment / ink.total : 0;
        return ink;
    }

    cairo_surface_t *surface;
    int level;
};

// the face the numbers below were read with, a stand in would give other ones
bool has_lucida_ui()
{
    FcPattern *wanted = FcNameParse(reinterpret_cast<const FcChar8*>(".Lucida Grande UI"));
    FcConfigSubstitute(nullptr, wanted, FcMatchPattern);
    FcDefaultSubstitute(wanted);
    FcResult result;
    FcPattern *found = FcFontMatch(nullptr, wanted, &result);
    bool same = false;
    FcChar8 *family = nullptr;
    for (int index = 0; found && FcPatternGetString(found, FC_FAMILY, index, &family) == FcResultMatch; index++)
    {
        same = same || QByteArray(reinterpret_cast<const char*>(family)) == ".Lucida Grande UI";
    }
    FcPatternDestroy(wanted);
    if (found)
    {
        FcPatternDestroy(found);
    }
    return same;
}

themed::rgba_t grey(int level, int alpha = 255)
{
    return {level / 255.0, level / 255.0, level / 255.0, alpha / 255.0};
}

// big letters with flat tops and feet, so whole rows are fully covered
themed::window_theme_t plain_look()
{
    themed::window_theme_t theme;
    theme.title_font_family = "DejaVu Sans";
    theme.title_font_size   = 40;
    theme.title_color = {0x20 / 255.0, 0x40 / 255.0, 0x60 / 255.0, 1.0};
    theme.title_color_inactive = {0x20 / 255.0, 0x40 / 255.0, 0x60 / 255.0, 0.5};
    theme.title_align = themed::title_align_t::CENTER;
    return theme;
}

// as measured on os x 10.9.5, letters 46 to 77 over a white copy at 140 of 255
themed::window_theme_t mavericks_look()
{
    themed::window_theme_t theme = plain_look();
    theme.title_gradient = {grey(46), grey(77)};
    theme.title_gradient_inactive = {grey(46, 149), grey(77, 149)};
    theme.title_shadow_color = grey(255, 140);
    theme.title_shadow_color_inactive = grey(255, 77);
    return theme;
}

// "Hamburgefonstiv Music" at 13 px drawn by os x 10.9.5 with text colour, or as a window title
themed::window_theme_t lucida(themed::window_theme_t theme)
{
    theme.title_font_family = ".Lucida Grande UI";
    theme.title_font_size   = 13;
    theme.title_smoothing   = themed::title_smoothing_t::SUBPIXEL;
    return theme;
}

themed::window_theme_t flat_text(int level)
{
    themed::window_theme_t theme = lucida(plain_look());
    theme.title_color = grey(level);
    return theme;
}

struct reference_t
{
    const char *name;
    themed::window_theme_t theme;
    bool active;
    int bar;
    int total, width, fringed;
    long lean;
    QList<int> rows;
};

QList<reference_t> references()
{
    return {
        {"black on white", flat_text(0), true, 255, 332226, 148, 601, -63755,
         {934, 7785, 9677, 8939, 49282, 46866, 38474, 40835, 35022, 41081, 46967, 1952, 3131, 1281}},
        {"white on black", flat_text(255), true, 0, 483462, 148, 634, -59497,
         {2601, 13072, 14118, 12672, 67170, 70109, 56106, 57428, 51081, 61714, 63142, 7304, 4193, 2752}},
        {"title on white", lucida(mavericks_look()), true, 255, 274610, 148, 520, -48252,
         {894, 7105, 8347, 7411, 41460, 39327, 31838, 33508, 28689, 33640, 37139, 1648, 2470, 1134}},
        {"title on black", lucida(mavericks_look()), true, 0, 215791, 148, 258, -18796,
         {478, 2660, 4800, 5195, 15443, 26115, 27673, 21626, 22469, 21578, 25677, 34208, 4443, 1913, 1513}},
        {"inactive title on white", lucida(mavericks_look()), false, 255, 160619, 148, 341, -28245,
         {520, 4123, 4874, 4333, 24216, 23054, 18689, 19583, 16773, 19690, 21628, 1023, 1446, 664, 3}},
        {"inactive title on black", lucida(mavericks_look()), false, 0, 154449, 148, 85, -16475,
         {267, 1829, 3776, 3978, 10007, 19923, 20267, 16812, 17119, 16868, 19821, 19063, 2617, 1275, 827}},
    };
}

// rows holding at least a hundredth of the fullest one, so a stray grey level does not count
int inked_rows(const QList<int>& rows)
{
    const int most = *std::max_element(rows.begin(), rows.end());
    return std::count_if(rows.begin(), rows.end(), [most] (int row) { return row * 100 >= most; });
}

const char *REFERENCE_TEXT = "Hamburgefonstiv Music";

QString text(const pixel_t& pixel)
{
    return QStringLiteral("%1 %2 %3 %4").arg(pixel.alpha).arg(pixel.red).arg(pixel.green).arg(pixel.blue);
}
}

class TestTitleText : public QObject
{
    Q_OBJECT

  private slots:
    void a_title_without_looks_of_its_own_is_one_colour()
    {
        const picture_t title(plain_look(), "HHHH", 1, true);
        QVERIFY(title.drawn());
        QVERIFY(title.first_row() >= 0);
        int solid = 0;
        for (int y = title.first_row(); y <= title.last_row(); y++)
        {
            for (int x : title.solid_columns(y))
            {
                QCOMPARE(text(title.at(x, y)), QStringLiteral("255 32 64 96"));
                solid++;
            }
        }
        QVERIFY2(solid > 500, qPrintable(QString::number(solid)));
    }

    void an_inactive_title_without_looks_of_its_own_is_its_own_colour()
    {
        const picture_t title(plain_look(), "HHHH", 1, false);
        const int middle = (title.first_row() + title.last_row()) / 2;
        int most = 0;
        for (int x = 0; x < title.width; x++)
        {
            most = qMax(most, title.at(x, middle).alpha);
        }
        QVERIFY2(qAbs(most - 128) <= 1, qPrintable(QString::number(most)));
    }

    void a_gradient_runs_from_the_top_of_the_letters_to_their_bottom_data()
    {
        QTest::addColumn<double>("scale");
        QTest::newRow("1") << 1.0;
        QTest::newRow("0.75") << 0.75;
        QTest::newRow("2") << 2.0;
    }

    void a_gradient_runs_from_the_top_of_the_letters_to_their_bottom()
    {
        QFETCH(double, scale);
        themed::window_theme_t theme = mavericks_look();
        theme.title_shadow_color = grey(255, 0);
        const picture_t title(theme, "HHHH", scale, true);
        QVERIFY(title.drawn());
        QList<int> levels;
        for (int y = title.first_row(); y <= title.last_row(); y++)
        {
            const QList<int> columns = title.solid_columns(y);
            if (columns.isEmpty())
            {
                continue;
            }
            const pixel_t first = title.at(columns.first(), y);
            for (int x : columns)
            {
                const pixel_t pixel = title.at(x, y);
                QCOMPARE(pixel.red, pixel.green);
                QCOMPARE(pixel.red, pixel.blue);
                QVERIFY2(qAbs(pixel.red - first.red) <= 1, qPrintable(text(pixel) + " beside " + text(first)));
            }
            levels << first.red;
        }
        QVERIFY2(levels.size() >= 20 * scale, qPrintable(QString::number(levels.size())));
        QVERIFY2(levels.first() >= 46 && levels.first() <= 49, qPrintable(QString::number(levels.first())));
        QVERIFY2(levels.last() >= 74 && levels.last() <= 77, qPrintable(QString::number(levels.last())));
        for (int index = 1; index < levels.size(); index++)
        {
            QVERIFY2(levels[index] >= levels[index - 1],
                qPrintable(QStringLiteral("row %1: %2 after %3").arg(index).arg(levels[index]).arg(levels[index - 1])));
        }
        const int middle = levels[levels.size() / 2];
        QVERIFY2(qAbs(middle - 61) <= 2, qPrintable(QString::number(middle)));
    }

    void short_letters_take_the_whole_gradient_too()
    {
        themed::window_theme_t theme = mavericks_look();
        theme.title_shadow_color = grey(255, 0);
        theme.title_font_size = 80;
        const picture_t title(theme, "xxx", 1, true, 400, 160);
        int lightest = 255, darkest = 0;
        for (int y = title.first_row(); y <= title.last_row(); y++)
        {
            for (int x : title.solid_columns(y))
            {
                lightest = qMin(lightest, title.at(x, y).red);
                darkest  = qMax(darkest, title.at(x, y).red);
            }
        }
        QVERIFY2(lightest <= 48 && darkest >= 75, qPrintable(QStringLiteral("%1 to %2").arg(lightest).arg(darkest)));
    }

    void a_copy_of_the_letters_shows_under_them_data()
    {
        QTest::addColumn<double>("scale");
        QTest::addColumn<int>("lower");
        QTest::newRow("1") << 1.0 << 1;
        QTest::newRow("0.75") << 0.75 << 1;
        QTest::newRow("1.25") << 1.25 << 1;
        QTest::newRow("2") << 2.0 << 2;
    }

    void a_copy_of_the_letters_shows_under_them()
    {
        QFETCH(double, scale);
        QFETCH(int, lower);
        themed::window_theme_t bare = mavericks_look();
        bare.title_shadow_color = grey(255, 0);
        const picture_t alone(bare, "HHHH", scale, true);
        const picture_t title(mavericks_look(), "HHHH", scale, true);
        QCOMPARE(title.first_row(), alone.first_row());
        QCOMPARE(title.last_row(), alone.last_row() + lower);

        // the letters cover the copy wherever both are
        int feet = alone.last_row();
        while (alone.solid_columns(feet).isEmpty())
        {
            feet--;
        }
        for (int y = alone.first_row(); y <= feet; y++)
        {
            for (int x : alone.solid_columns(y))
            {
                QCOMPARE(text(title.at(x, y)), text(alone.at(x, y)));
            }
        }

        // under the feet only the copy is left, white at 140 of 255
        int seen = 0;
        for (int x : alone.solid_columns(feet))
        {
            if (alone.at(x, feet + lower).alpha == 0)
            {
                const pixel_t pixel = title.at(x, feet + lower);
                QVERIFY2(qAbs(pixel.alpha - 140) <= 1 && pixel.red == pixel.alpha && pixel.green == pixel.alpha
                         && pixel.blue == pixel.alpha, qPrintable(text(pixel)));
                seen++;
            }
        }
        QVERIFY2(seen > 10 * scale, qPrintable(QString::number(seen)));
    }

    void a_deeper_drop_puts_the_copy_lower()
    {
        themed::window_theme_t theme = mavericks_look();
        theme.title_shadow_drop = 3;
        themed::window_theme_t bare = theme;
        bare.title_shadow_color = grey(255, 0);
        const picture_t alone(bare, "HHHH", 1, true);
        const picture_t title(theme, "HHHH", 1, true);
        QCOMPARE(title.last_row(), alone.last_row() + 3);
    }

    // on 10.9.5 an inactive title read 62 on black where its letters were 53
    void an_inactive_title_is_its_own_letters_over_its_own_copy()
    {
        themed::window_theme_t theme = mavericks_look();
        theme.title_gradient_inactive = {grey(53, 149), grey(53, 149)};
        const picture_t title(theme, "HHHH", 1, false);
        const int middle = (title.first_row() + title.last_row()) / 2;
        int most = 0;
        pixel_t inside{};
        for (int x = 0; x < title.width; x++)
        {
            if (title.at(x, middle).alpha > most)
            {
                inside = title.at(x, middle);
                most = inside.alpha;
            }
        }
        QVERIFY2(qAbs(inside.red - 62) <= 2 && inside.red == inside.green && inside.red == inside.blue,
            qPrintable(text(inside)));
        QVERIFY2(qAbs(inside.alpha - 181) <= 2, qPrintable(text(inside)));
    }

    void initTestCase()
    {
        QVERIFY2(has_lucida_ui(), "the .Lucida Grande UI face is missing, the reference numbers need it");
    }

    void a_mavericks_title_carries_the_ink_of_the_original_data()
    {
        QTest::addColumn<int>("index");
        const QList<reference_t> all = references();
        for (int index = 0; index < all.size(); index++)
        {
            QTest::newRow(all[index].name) << index;
        }
    }

    void a_mavericks_title_carries_the_ink_of_the_original()
    {
        QFETCH(int, index);
        const reference_t wanted = references()[index];
        const bar_t bar(220, 24, wanted.bar);
        bar.draw(wanted.theme, REFERENCE_TEXT, 1, wanted.active, themed::subpixel_t::RGB);
        const ink_t ink = bar.measure();
        QVERIFY2(qAbs(ink.total - wanted.total) * 100 <= wanted.total * 4,
            qPrintable(QStringLiteral("total %1 against %2").arg(ink.total).arg(wanted.total)));
        const int most = *std::max_element(wanted.rows.begin(), wanted.rows.end());
        for (int row = 0; row < wanted.rows.size(); row++)
        {
            if (wanted.rows[row] * 4 < most)
            {
                continue;
            }
            const int got = row < ink.rows.size() ? ink.rows[row] : 0;
            QVERIFY2(qAbs(got - wanted.rows[row]) * 10 <= wanted.rows[row],
                qPrintable(QStringLiteral("row %1: %2 against %3").arg(row).arg(got).arg(wanted.rows[row])));
        }
    }

    void a_mavericks_title_is_as_wide_and_tall_as_the_original_data()
    {
        a_mavericks_title_carries_the_ink_of_the_original_data();
    }

    void a_mavericks_title_is_as_wide_and_tall_as_the_original()
    {
        QFETCH(int, index);
        const reference_t wanted = references()[index];
        const bar_t bar(220, 24, wanted.bar);
        bar.draw(wanted.theme, REFERENCE_TEXT, 1, wanted.active, themed::subpixel_t::RGB);
        const ink_t ink = bar.measure();
        QVERIFY2(qAbs(ink.width - wanted.width) <= 1,
            qPrintable(QStringLiteral("width %1 against %2").arg(ink.width).arg(wanted.width)));
        QVERIFY2(qAbs(inked_rows(ink.rows) - inked_rows(wanted.rows)) <= 1,
            qPrintable(QStringLiteral("rows %1 against %2").arg(inked_rows(ink.rows)).arg(inked_rows(wanted.rows))));
    }

    void a_mavericks_title_has_the_colour_fringes_of_the_original_data()
    {
        a_mavericks_title_carries_the_ink_of_the_original_data();
    }

    void a_mavericks_title_has_the_colour_fringes_of_the_original()
    {
        QFETCH(int, index);
        const reference_t wanted = references()[index];
        const bar_t bar(220, 24, wanted.bar);
        bar.draw(wanted.theme, REFERENCE_TEXT, 1, wanted.active, themed::subpixel_t::RGB);
        const ink_t ink = bar.measure();
        QVERIFY2(qAbs(ink.fringed - wanted.fringed) * 4 <= wanted.fringed,
            qPrintable(QStringLiteral("%1 fringed pixels against %2").arg(ink.fringed).arg(wanted.fringed)));
        QVERIFY2((ink.lean < 0) == (wanted.lean < 0) && qAbs(ink.lean - wanted.lean) * 4 <= qAbs(wanted.lean),
            qPrintable(QStringLiteral("fringes lean %1 against %2").arg(ink.lean).arg(wanted.lean)));
    }

    void a_title_puts_its_letters_where_the_original_puts_them_from_the_pen_data()
    {
        QTest::addColumn<int>("text_level");
        QTest::addColumn<double>("centre");
        QTest::newRow("black on white") << 0 << 72.065;
        QTest::newRow("white on black") << 255 << 72.158;
    }

    void a_title_puts_its_letters_where_the_original_puts_them_from_the_pen()
    {
        QFETCH(int, text_level);
        QFETCH(double, centre);
        themed::window_theme_t theme = flat_text(text_level);
        theme.title_align = themed::title_align_t::LEFT;
        const bar_t bar(220, 24, 255 - text_level);
        bar.draw(theme, REFERENCE_TEXT, 1, true, themed::subpixel_t::RGB);
        const double got = bar.measure().centre;
        QVERIFY2(qAbs(got - centre) <= 0.3, qPrintable(QStringLiteral("ink centre %1 against %2").arg(got).arg(centre)));
    }

    // ink per column of black on white, from the pen onwards
    void a_title_has_each_stem_where_the_original_has_it()
    {
        const QList<int> wanted = {130,5312,3940,760,786,780,1301,6737,3297,129,2290,3217,2532,2626,4575,1704,189,401,4687,1970,907,1303,4664,2614,927,1021,4054,3288,68,616,7114,3222,1783,1889,3065,3601,433,82,3431,2991,919,948,2899,3790,91,91,3684,2985,979,924,252,3102,4360,2747,2624,3248,6197,1351,159,2717,3811,2826,2639,3361,1952,56,1056,6558,4570,1715,1081,2818,3389,1988,1698,2231,3764,1584,13,2221,4137,1079,962,1406,4616,1030,171,2567,3325,2739,3566,1475,208,3061,5959,2240,1287,34,2818,4869,147,1026,2550,2483,1926,2351,2152,536,0,0,0,0,130,5246,4907,3100,3067,1999,2388,2533,3726,6695,620,82,3431,2991,919,948,2899,3790,91,532,3117,2989,2886,3245,601,117,4657,3042,0,537,3711,3036,1981,1730,1744,185,0,0,0,0};
        themed::window_theme_t theme = flat_text(0);
        theme.title_align = themed::title_align_t::LEFT;
        const bar_t bar(220, 24, 255);
        bar.draw(theme, REFERENCE_TEXT, 1, true, themed::subpixel_t::RGB);
        long apart = 0, all = 0;
        for (int x = 0; x < wanted.size(); x++)
        {
            int column = 0;
            for (int y = 0; y < 24; y++)
            {
                const pixel_t pixel = bar.at(x, y);
                column += (255 - pixel.red) + (255 - pixel.green) + (255 - pixel.blue);
            }
            apart += qAbs(column - wanted[x]);
            all   += wanted[x];
        }
        const double share = double(apart) / all;
        QVERIFY2(share <= 0.14, qPrintable(QStringLiteral("columns off by %1 of all ink").arg(share)));
    }

    void a_screen_with_blue_on_the_left_gets_the_fringes_mirrored()
    {
        const themed::window_theme_t theme = lucida(mavericks_look());
        const bar_t rgb(220, 24, 222), bgr(220, 24, 222);
        rgb.draw(theme, REFERENCE_TEXT, 1, true, themed::subpixel_t::RGB);
        bgr.draw(theme, REFERENCE_TEXT, 1, true, themed::subpixel_t::BGR);
        QVERIFY(rgb.measure().fringed > 100);
        for (int y = 0; y < 24; y++)
        {
            for (int x = 0; x < 220; x++)
            {
                const pixel_t a = rgb.at(x, y), b = bgr.at(x, y);
                QVERIFY2(a.red == b.blue && a.green == b.green && a.blue == b.red,
                    qPrintable(QStringLiteral("%1,%2: %3 against %4").arg(x).arg(y).arg(text(a), text(b))));
            }
        }
    }

    void a_screen_gets_the_letters_its_subpixels_allow_data()
    {
        QTest::addColumn<int>("subpixel");
        QTest::addColumn<int>("transform");
        QTest::addColumn<int>("order");
        QTest::newRow("unknown") << int(WL_OUTPUT_SUBPIXEL_UNKNOWN) << int(WL_OUTPUT_TRANSFORM_NORMAL) << int(themed::subpixel_t::RGB);
        QTest::newRow("rgb") << int(WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB) << int(WL_OUTPUT_TRANSFORM_NORMAL) << int(themed::subpixel_t::RGB);
        QTest::newRow("bgr") << int(WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR) << int(WL_OUTPUT_TRANSFORM_NORMAL) << int(themed::subpixel_t::BGR);
        QTest::newRow("upside down") << int(WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB) << int(WL_OUTPUT_TRANSFORM_180) << int(themed::subpixel_t::BGR);
        QTest::newRow("vertical") << int(WL_OUTPUT_SUBPIXEL_VERTICAL_RGB) << int(WL_OUTPUT_TRANSFORM_NORMAL) << int(themed::subpixel_t::NONE);
        QTest::newRow("none") << int(WL_OUTPUT_SUBPIXEL_NONE) << int(WL_OUTPUT_TRANSFORM_NORMAL) << int(themed::subpixel_t::NONE);
        QTest::newRow("quarter turn") << int(WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB) << int(WL_OUTPUT_TRANSFORM_90) << int(themed::subpixel_t::NONE);
        QTest::newRow("flipped quarter turn") << int(WL_OUTPUT_SUBPIXEL_UNKNOWN) << int(WL_OUTPUT_TRANSFORM_FLIPPED_270) << int(themed::subpixel_t::NONE);
        QTest::newRow("flipped") << int(WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB) << int(WL_OUTPUT_TRANSFORM_FLIPPED) << int(themed::subpixel_t::BGR);
    }

    void a_screen_gets_the_letters_its_subpixels_allow()
    {
        QFETCH(int, subpixel);
        QFETCH(int, transform);
        QFETCH(int, order);
        QCOMPARE(int(themed::subpixel_order(wl_output_subpixel(subpixel), wl_output_transform(transform))), order);
    }

    void a_title_without_subpixels_to_use_is_grey_and_as_heavy_data()
    {
        QTest::addColumn<int>("smoothing");
        QTest::addColumn<int>("order");
        QTest::addColumn<double>("scale");
        QTest::newRow("grayscale theme") << int(themed::title_smoothing_t::GRAYSCALE) << int(themed::subpixel_t::RGB) << 1.0;
        QTest::newRow("screen without subpixels") << int(themed::title_smoothing_t::SUBPIXEL) << int(themed::subpixel_t::NONE) << 1.0;
        QTest::newRow("fractional scale") << int(themed::title_smoothing_t::SUBPIXEL) << int(themed::subpixel_t::RGB) << 0.75;
    }

    void a_title_without_subpixels_to_use_is_grey_and_as_heavy()
    {
        QFETCH(int, smoothing);
        QFETCH(int, order);
        QFETCH(double, scale);
        themed::window_theme_t theme = lucida(mavericks_look());
        const bar_t sharp(220, 24, 222);
        sharp.draw(theme, REFERENCE_TEXT, scale, true, themed::subpixel_t::RGB);
        theme.title_smoothing = themed::title_smoothing_t(smoothing);
        const bar_t grey_bar(220, 24, 222);
        grey_bar.draw(theme, REFERENCE_TEXT, scale, true, themed::subpixel_t(order));
        const ink_t ink = grey_bar.measure();
        for (int y = 0; y < 24; y++)
        {
            for (int x = 0; x < 220; x++)
            {
                const pixel_t pixel = grey_bar.at(x, y);
                QVERIFY2(pixel.red == pixel.green && pixel.green == pixel.blue, qPrintable(text(pixel)));
            }
        }
        const int subpixel_total = scale == 1.0 ? sharp.measure().total : ink.total;
        QVERIFY2(qAbs(ink.total - subpixel_total) * 100 <= subpixel_total * 5,
            qPrintable(QStringLiteral("%1 against %2").arg(ink.total).arg(subpixel_total)));
        const bar_t plain_bar(220, 24, 222);
        theme.title_smoothing = themed::title_smoothing_t::PLAIN;
        plain_bar.draw(theme, REFERENCE_TEXT, scale, true, themed::subpixel_t::RGB);
        QVERIFY2(ink.total > plain_bar.measure().total,
            qPrintable(QStringLiteral("%1 against plain %2").arg(ink.total).arg(plain_bar.measure().total)));
    }

    void a_doubled_title_is_twice_as_wide_with_its_fringes()
    {
        const themed::window_theme_t theme = lucida(flat_text(0));
        const bar_t single(220, 24, 255), doubled(440, 48, 255);
        single.draw(theme, REFERENCE_TEXT, 1, true, themed::subpixel_t::RGB);
        doubled.draw(theme, REFERENCE_TEXT, 2, true, themed::subpixel_t::RGB);
        const ink_t one = single.measure(), two = doubled.measure();
        QVERIFY2(qAbs(two.width - 2 * one.width) <= 2, qPrintable(QStringLiteral("%1 and %2").arg(one.width).arg(two.width)));
        QVERIFY2(two.fringed > one.fringed, qPrintable(QStringLiteral("%1 and %2").arg(one.fringed).arg(two.fringed)));
        QVERIFY2(qAbs(two.total - 4 * one.total) * 100 <= 4 * one.total * 15,
            qPrintable(QStringLiteral("%1 and %2").arg(one.total).arg(two.total)));
    }

    // a colour emoji has no outline to build letters from, so such a title is drawn the plain way
    void a_title_with_a_picture_glyph_is_drawn_as_plain_text()
    {
        themed::window_theme_t theme = lucida(mavericks_look());
        const char *title = "\xF0\x9F\x99\x82 smile";
        const bar_t smoothed(220, 24, 222), plain(220, 24, 222);
        smoothed.draw(theme, title, 1, true, themed::subpixel_t::RGB);
        theme.title_smoothing = themed::title_smoothing_t::PLAIN;
        plain.draw(theme, title, 1, true, themed::subpixel_t::RGB);
        QVERIFY(plain.measure().total > 0);
        for (int y = 0; y < 24; y++)
        {
            for (int x = 0; x < 220; x++)
            {
                QVERIFY2(text(smoothed.at(x, y)) == text(plain.at(x, y)),
                    qPrintable(QStringLiteral("%1,%2: %3 against %4").arg(x).arg(y).arg(text(smoothed.at(x, y)), text(plain.at(x, y)))));
            }
        }
    }

    void a_title_stays_inside_its_box()
    {
        const themed::window_theme_t theme = lucida(mavericks_look());
        const bar_t bar(220, 40, 222);
        const cairo_rectangle_int_t box{30, 8, 60, 24};
        bar.draw(theme, REFERENCE_TEXT, 1, true, themed::subpixel_t::RGB, box);
        int inside = 0;
        for (int y = 0; y < 40; y++)
        {
            for (int x = 0; x < 220; x++)
            {
                const pixel_t pixel = bar.at(x, y);
                const bool touched = pixel.red != 222 || pixel.green != 222 || pixel.blue != 222;
                const bool in_box = x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
                QVERIFY2(!touched || in_box, qPrintable(QStringLiteral("%1,%2 %3").arg(x).arg(y).arg(text(pixel))));
                inside += touched;
            }
        }
        QVERIFY2(inside > 100, qPrintable(QString::number(inside)));
    }

    void a_title_without_letters_draws_nothing_data()
    {
        QTest::addColumn<QString>("words");
        QTest::newRow("empty") << QString();
        QTest::newRow("spaces") << QStringLiteral("   ");
    }

    void a_title_without_letters_draws_nothing()
    {
        QFETCH(QString, words);
        for (bool active : {true, false})
        {
            const picture_t title(mavericks_look(), words.toUtf8().constData(), 1, active);
            QVERIFY(title.drawn());
            QCOMPARE(title.first_row(), -1);
        }
    }

    void a_title_with_no_room_draws_nothing_data()
    {
        QTest::addColumn<int>("smoothing");
        QTest::newRow("plain") << int(themed::title_smoothing_t::PLAIN);
        QTest::newRow("subpixel") << int(themed::title_smoothing_t::SUBPIXEL);
    }

    void a_title_with_no_room_draws_nothing()
    {
        QFETCH(int, smoothing);
        themed::window_theme_t theme = mavericks_look();
        theme.title_smoothing = themed::title_smoothing_t(smoothing);
        const bar_t bar(220, 24, 255);
        bar.draw(theme, "HHHH", 1, true, themed::subpixel_t::RGB, {10, 4, 0, 16});
        bar.draw(theme, "HHHH", 1, true, themed::subpixel_t::RGB, {10, 4, 50, 0});
        QCOMPARE(bar.measure().total, 0);
    }

    void a_centred_title_sits_about_the_wanted_middle()
    {
        themed::window_theme_t theme = mavericks_look();
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 80);
        themed::draw_title(surface, theme, "HHHH", {0, 0, 400, 80}, 1, true, 150);
        cairo_surface_flush(surface);
        int left = 400, right = -1;
        for (int y = 0; y < 80; y++)
        {
            const unsigned char *row = cairo_image_surface_get_data(surface) + y * cairo_image_surface_get_stride(surface);
            for (int x = 0; x < 400; x++)
            {
                if (row[4 * x + 3] > 0)
                {
                    left  = qMin(left, x);
                    right = qMax(right, x);
                }
            }
        }
        cairo_surface_destroy(surface);
        QVERIFY2(qAbs((left + right + 1) / 2.0 - 150) <= 3, qPrintable(QStringLiteral("%1 to %2").arg(left).arg(right)));
    }
};

QTEST_APPLESS_MAIN(TestTitleText)
#include "test-title-text.moc"
