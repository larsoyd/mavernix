#include <QtTest>

#include <cairo.h>

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
        surface(themed::render_title(theme, text, width * scale, height * scale, scale, active, width * scale / 2)),
        width(cairo_image_surface_get_width(surface)), height(cairo_image_surface_get_height(surface))
    {
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

    void a_title_too_small_to_draw_is_an_empty_picture()
    {
        cairo_surface_t *surface = themed::render_title(mavericks_look(), "HHHH", 0, 0, 1, true, 0);
        QCOMPARE(cairo_surface_status(surface), CAIRO_STATUS_SUCCESS);
        QCOMPARE(cairo_image_surface_get_width(surface), 0);
        cairo_surface_destroy(surface);
    }

    void a_centred_title_sits_about_the_wanted_middle()
    {
        themed::window_theme_t theme = mavericks_look();
        cairo_surface_t *surface = themed::render_title(theme, "HHHH", 400, 80, 1, true, 150);
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
