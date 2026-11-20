// Reading a theme's window frame the way the compositor does
// (src/compositor/decoration/theme-file.cpp): the themes that come with the
// desktop are valid, and a mistake in a theme is refused with its place named.

#include <QtTest>

#include <stdexcept>

#include "theme-file.hpp"

class TestThemeFile : public QObject
{
    Q_OBJECT

  private:
    // A copy of the plain theme with `change` applied to its text, in a
    // folder of its own.
    static QString changed_plain_theme(QTemporaryDir& folder, const QString& from, const QString& to)
    {
        QFile original(QStringLiteral(THEMES_DIR "/plain/theme.toml"));
        if (!original.open(QIODevice::ReadOnly))
        {
            qFatal("cannot read the plain theme");
        }
        QString text = QString::fromUtf8(original.readAll());
        if (!text.contains(from))
        {
            qFatal("the plain theme has no \"%s\" to change", qPrintable(from));
        }
        text.replace(from, to);

        QFile changed(folder.filePath(QStringLiteral("theme.toml")));
        if (!changed.open(QIODevice::WriteOnly))
        {
            qFatal("cannot write the changed theme");
        }
        changed.write(text.toUtf8());
        return folder.path();
    }

  private slots:
    void the_shipped_themes_are_valid_data()
    {
        QTest::addColumn<QString>("theme");
        QTest::newRow("plain") << QStringLiteral("plain");
    }

    void the_shipped_themes_are_valid()
    {
        QFETCH(QString, theme);
        const themed::window_theme_t read =
            themed::load_window_theme((QStringLiteral(THEMES_DIR "/") + theme).toStdString());
        QVERIFY(read.titlebar_height > 0);
        QVERIFY(read.button_width > 0);
        QVERIFY(!read.close.looks.empty());
    }

    void a_misspelt_key_is_refused_by_name()
    {
        QTemporaryDir folder;
        const QString theme = changed_plain_theme(folder, QStringLiteral("[window.buttons.close]\n"),
            QStringLiteral("[window.buttons.close]\nnormall = { color = \"#000000\" }\n"));
        try
        {
            themed::load_window_theme(theme.toStdString());
            QFAIL("a theme with an unknown key was accepted");
        }
        catch (const std::exception& error)
        {
            QVERIFY2(QString::fromUtf8(error.what()).contains(QStringLiteral("normall")), error.what());
        }
    }

    void a_shadow_fades_for_200_ms_unless_the_theme_says_otherwise()
    {
        const themed::window_theme_t read =
            themed::load_window_theme(QStringLiteral(THEMES_DIR "/plain").toStdString());
        QCOMPARE(read.shadow_fade_ms, 200);
    }

    void a_shadow_fade_comes_from_the_theme_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<int>("expected");
        QTest::newRow("longer") << QStringLiteral("350") << 350;
        QTest::newRow("off") << QStringLiteral("0") << 0;
    }

    void a_shadow_fade_comes_from_the_theme()
    {
        QFETCH(QString, value);
        QFETCH(int, expected);
        QTemporaryDir folder;
        const QString theme = changed_plain_theme(folder, QStringLiteral("[window.shadow]\n"),
            QStringLiteral("[window.shadow]\nfade_ms = ") + value + QStringLiteral("\n"));
        QCOMPARE(themed::load_window_theme(theme.toStdString()).shadow_fade_ms, expected);
    }

    void a_bad_shadow_fade_is_refused_by_name_data()
    {
        QTest::addColumn<QString>("value");
        QTest::newRow("below zero") << QStringLiteral("-1");
        QTest::newRow("fraction") << QStringLiteral("1.5");
        QTest::newRow("text") << QStringLiteral("\"slow\"");
    }

    void a_bad_shadow_fade_is_refused_by_name()
    {
        QFETCH(QString, value);
        QTemporaryDir folder;
        const QString theme = changed_plain_theme(folder, QStringLiteral("[window.shadow]\n"),
            QStringLiteral("[window.shadow]\nfade_ms = ") + value + QStringLiteral("\n"));
        try
        {
            themed::load_window_theme(theme.toStdString());
            QFAIL("a theme with a bad fade_ms was accepted");
        }
        catch (const std::exception& error)
        {
            QVERIFY2(QString::fromUtf8(error.what()).contains(QStringLiteral("fade_ms")), error.what());
        }
    }

    void a_title_is_one_colour_with_no_copy_unless_the_theme_says_otherwise()
    {
        const themed::window_theme_t read =
            themed::load_window_theme(QStringLiteral(THEMES_DIR "/plain").toStdString());
        QVERIFY(read.title_gradient.empty());
        QVERIFY(read.title_gradient_inactive.empty());
        QCOMPARE(read.title_shadow_color.alpha, 0.0);
        QCOMPARE(read.title_shadow_color_inactive.alpha, 0.0);
        QCOMPARE(read.title_shadow_drop, 1);
        QVERIFY(read.title_smoothing == themed::title_smoothing_t::PLAIN);
    }

    void a_title_is_smoothed_as_the_theme_says_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<int>("smoothing");
        QTest::newRow("plain") << QStringLiteral("plain") << int(themed::title_smoothing_t::PLAIN);
        QTest::newRow("subpixel") << QStringLiteral("subpixel") << int(themed::title_smoothing_t::SUBPIXEL);
        QTest::newRow("grayscale") << QStringLiteral("grayscale") << int(themed::title_smoothing_t::GRAYSCALE);
    }

    void a_title_is_smoothed_as_the_theme_says()
    {
        QFETCH(QString, value);
        QFETCH(int, smoothing);
        QTemporaryDir folder;
        const QString theme = with_title_keys(folder, QStringLiteral("title_smoothing = \"%1\"\n").arg(value));
        QCOMPARE(int(themed::load_window_theme(theme.toStdString()).title_smoothing), smoothing);
    }

    static QString with_title_keys(QTemporaryDir& folder, const QString& keys)
    {
        return changed_plain_theme(folder, QStringLiteral("title_align = \"left\"\n"),
            QStringLiteral("title_align = \"left\"\n") + keys);
    }

    static QString hex(const themed::rgba_t& color)
    {
        return QStringLiteral("%1 %2 %3 %4")
            .arg(qRound(color.red * 255)).arg(qRound(color.green * 255))
            .arg(qRound(color.blue * 255)).arg(qRound(color.alpha * 255));
    }

    void a_titles_gradient_and_copy_come_from_the_theme()
    {
        QTemporaryDir folder;
        const QString theme = with_title_keys(folder, QStringLiteral(
            "title_gradient = [\"#2e3f50\", \"#4d5e6f\"]\n"
            "title_gradient_inactive = [\"#10203095\", \"#40506096\"]\n"
            "title_shadow_color = \"#fffefd8c\"\n"
            "title_shadow_color_inactive = \"#0102034d\"\n"
            "title_shadow_drop = 2\n"));
        const themed::window_theme_t read = themed::load_window_theme(theme.toStdString());
        QCOMPARE(read.title_gradient.size(), size_t(2));
        QCOMPARE(hex(read.title_gradient[0]), QStringLiteral("46 63 80 255"));
        QCOMPARE(hex(read.title_gradient[1]), QStringLiteral("77 94 111 255"));
        QCOMPARE(read.title_gradient_inactive.size(), size_t(2));
        QCOMPARE(hex(read.title_gradient_inactive[0]), QStringLiteral("16 32 48 149"));
        QCOMPARE(hex(read.title_gradient_inactive[1]), QStringLiteral("64 80 96 150"));
        QCOMPARE(hex(read.title_shadow_color), QStringLiteral("255 254 253 140"));
        QCOMPARE(hex(read.title_shadow_color_inactive), QStringLiteral("1 2 3 77"));
        QCOMPARE(read.title_shadow_drop, 2);
    }

    void an_inactive_title_without_its_own_looks_takes_the_active_ones()
    {
        QTemporaryDir folder;
        const QString theme = with_title_keys(folder, QStringLiteral(
            "title_gradient = [\"#2e3f50\", \"#4d5e6f\"]\ntitle_shadow_color = \"#fffefd8c\"\n"));
        const themed::window_theme_t read = themed::load_window_theme(theme.toStdString());
        QCOMPARE(read.title_gradient_inactive.size(), size_t(2));
        QCOMPARE(hex(read.title_gradient_inactive[0]), QStringLiteral("46 63 80 255"));
        QCOMPARE(hex(read.title_gradient_inactive[1]), QStringLiteral("77 94 111 255"));
        QCOMPARE(hex(read.title_shadow_color_inactive), QStringLiteral("255 254 253 140"));
        QCOMPARE(read.title_shadow_drop, 1);
    }

    void a_bad_title_look_is_refused_by_name_data()
    {
        QTest::addColumn<QString>("keys");
        QTest::addColumn<QString>("named");
        QTest::newRow("one colour") << QStringLiteral("title_gradient = [\"#2e2e2e\"]\n")
                                    << QStringLiteral("title_gradient\"");
        QTest::newRow("three colours") << QStringLiteral("title_gradient = [\"#000000\", \"#111111\", \"#222222\"]\n")
                                       << QStringLiteral("title_gradient\"");
        QTest::newRow("not a list") << QStringLiteral("title_gradient = \"#2e2e2e\"\n")
                                    << QStringLiteral("title_gradient\"");
        QTest::newRow("not colours") << QStringLiteral("title_gradient = [1, 2]\n")
                                     << QStringLiteral("title_gradient\"");
        QTest::newRow("inactive one colour") << QStringLiteral("title_gradient_inactive = [\"#2e2e2e\"]\n")
                                             << QStringLiteral("title_gradient_inactive\"");
        QTest::newRow("copy not a colour") << QStringLiteral("title_shadow_color = 3\n")
                                           << QStringLiteral("title_shadow_color\"");
        QTest::newRow("inactive copy not a colour") << QStringLiteral("title_shadow_color_inactive = \"white\"\n")
                                                    << QStringLiteral("title_shadow_color_inactive\"");
        QTest::newRow("drop fraction") << QStringLiteral("title_shadow_drop = 1.5\n")
                                       << QStringLiteral("title_shadow_drop\"");
        QTest::newRow("drop below zero") << QStringLiteral("title_shadow_drop = -1\n")
                                         << QStringLiteral("title_shadow_drop\"");
        QTest::newRow("drop too deep") << QStringLiteral("title_shadow_drop = 17\n")
                                       << QStringLiteral("title_shadow_drop\"");
        QTest::newRow("drop text") << QStringLiteral("title_shadow_drop = \"low\"\n")
                                   << QStringLiteral("title_shadow_drop\"");
        QTest::newRow("unknown smoothing") << QStringLiteral("title_smoothing = \"lcd\"\n")
                                           << QStringLiteral("title_smoothing\"");
        QTest::newRow("smoothing not text") << QStringLiteral("title_smoothing = 1\n")
                                            << QStringLiteral("title_smoothing\"");
    }

    void a_bad_title_look_is_refused_by_name()
    {
        QFETCH(QString, keys);
        QFETCH(QString, named);
        QTemporaryDir folder;
        const QString theme = with_title_keys(folder, keys);
        try
        {
            themed::load_window_theme(theme.toStdString());
            QFAIL("a theme with a bad title look was accepted");
        }
        catch (const std::exception& error)
        {
            QVERIFY2(QString::fromUtf8(error.what()).contains(named), error.what());
        }
    }

    void a_missing_theme_is_refused()
    {
        QVERIFY_THROWS_EXCEPTION(std::exception, themed::load_window_theme("/nonexistent"));
    }
};

QTEST_APPLESS_MAIN(TestThemeFile)
#include "test-theme-file.moc"
