// where the month and day sit on a calendar page, in which face, ink and lean

#include <QtTest>

#include <QFontDatabase>

#include "date-page.hpp"

class TestDatePage : public QObject
{
    Q_OBJECT

  private:
    static QImage page_with(const QString& month, const QString& day, int side = 512, const QColor& paper = QColor(128, 128, 128))
    {
        QImage page(side, side, QImage::Format_ARGB32_Premultiplied);
        page.fill(paper);
        draw_date_page(page, month, day);
        return page;
    }

    // the box around the ink that is at least half there, in shares of the side
    static QRectF ink(const QImage& page, bool dark)
    {
        QRect box;
        for (int y = 0; y < page.height(); y++)
        {
            for (int x = 0; x < page.width(); x++)
            {
                const int level = qGray(page.pixel(x, y));
                if (dark ? level < 78 : level > 178)
                {
                    box |= QRect(x, y, 1, 1);
                }
            }
        }
        const double side = page.width();
        return QRectF(box.x() / side, box.y() / side, box.width() / side, box.height() / side);
    }

    static bool near(double value, double wanted, double slack)
    {
        return qAbs(value - wanted) <= slack;
    }

    static bool neue()
    {
        return QFontDatabase::hasFamily(QStringLiteral("Helvetica Neue"));
    }

    static QByteArray told(const QRectF& box)
    {
        return QStringLiteral("x %1..%2 y %3..%4").arg(box.left()).arg(box.right()).arg(box.top()).arg(box.bottom()).toUtf8();
    }

    static bool matches(const QRectF& box, const QRectF& wanted, double slack)
    {
        return near(box.left(), wanted.left(), slack) && near(box.right(), wanted.right(), slack) &&
               near(box.top(), wanted.top(), slack) && near(box.bottom(), wanted.bottom(), slack);
    }

    static QRectF edges(double left, double right, double top, double bottom)
    {
        return QRectF(QPointF(left, top), QPointF(right, bottom));
    }

  private slots:
    void initTestCase()
    {
        QVERIFY2(neue() || QFontDatabase::hasFamily(QStringLiteral("Helvetica")),
            "refused: neither font=Helvetica Neue nor font=Helvetica is installed");
    }

    // boxes read off the original tile drawn at 1024 for these dates, in each of its two faces
    void dayFillsTheOriginalsBox_data()
    {
        QTest::addColumn<QString>("day");
        QTest::addColumn<QRectF>("helvetica");
        QTest::addColumn<QRectF>("helvetica_neue");
        QTest::newRow("one digit") << "1" << edges(0.3945, 0.5654, 0.4092, 0.7129) << edges(0.3994, 0.5723, 0.4189, 0.7197);
        QTest::newRow("a kerned pair") << "11" << edges(0.2979, 0.6611, 0.3916, 0.7295) << edges(0.2920, 0.6797, 0.3994, 0.7383);
        QTest::newRow("two digits") << "17" << edges(0.2861, 0.6914, 0.3818, 0.7314) << edges(0.2920, 0.6885, 0.3906, 0.7383);
        QTest::newRow("widest") << "28" << edges(0.2803, 0.7178, 0.3936, 0.7461) << edges(0.2822, 0.7285, 0.3994, 0.7549);
        QTest::newRow("last") << "31" << edges(0.2793, 0.6729, 0.3896, 0.7393) << edges(0.2803, 0.6797, 0.3994, 0.7441);
    }

    void dayFillsTheOriginalsBox()
    {
        QFETCH(QString, day);
        QFETCH(QRectF, helvetica);
        QFETCH(QRectF, helvetica_neue);
        const QRectF box = ink(page_with("", day), true);
        QVERIFY2(matches(box, neue() ? helvetica_neue : helvetica, 0.006), told(box).constData());
    }

    void monthFillsTheOriginalsBox_data()
    {
        QTest::addColumn<QString>("month");
        QTest::addColumn<QRectF>("helvetica");
        QTest::addColumn<QRectF>("helvetica_neue");
        QTest::newRow("FEB") << "FEB" << edges(0.1289, 0.3408, 0.2637, 0.3721) << edges(0.1328, 0.3340, 0.2920, 0.3955);
        QTest::newRow("MAR") << "MAR" << edges(0.1289, 0.3760, 0.2568, 0.3730) << edges(0.1328, 0.3750, 0.2842, 0.3955);
        QTest::newRow("MAY") << "MAY" << edges(0.1289, 0.3594, 0.2539, 0.3730) << edges(0.1328, 0.3535, 0.2822, 0.3955);
        QTest::newRow("JUL") << "JUL" << edges(0.1318, 0.3350, 0.2676, 0.3721) << edges(0.1357, 0.3330, 0.2949, 0.3945);
        QTest::newRow("SEP") << "SEP" << edges(0.1299, 0.3379, 0.2627, 0.3701) << edges(0.1338, 0.3311, 0.2910, 0.3926);
    }

    void monthFillsTheOriginalsBox()
    {
        QFETCH(QString, month);
        QFETCH(QRectF, helvetica);
        QFETCH(QRectF, helvetica_neue);
        const QRectF box = ink(page_with(month, ""), false);
        QVERIFY2(matches(box, neue() ? helvetica_neue : helvetica, 0.008), told(box).constData());
    }

    // black at four fifths, so white paper shows through as 51
    void dayIsFourFifthsBlack()
    {
        const QImage page = page_with("", "28", 512, Qt::white);
        int darkest = 255;
        int solid = 0;
        for (int y = 0; y < page.height(); y++)
        {
            for (int x = 0; x < page.width(); x++)
            {
                const int level = qGray(page.pixel(x, y));
                darkest = qMin(darkest, level);
                solid += level == 51 ? 1 : 0;
            }
        }
        QCOMPARE(darkest, 51);
        QVERIFY2(solid > 10000, qPrintable(QString::number(solid)));
    }

    void monthIsWhite()
    {
        const QImage page = page_with("SEP", "", 512, Qt::black);
        int lightest = 0;
        for (int y = 0; y < page.height(); y++)
        {
            for (int x = 0; x < page.width(); x++)
            {
                lightest = qMax(lightest, qGray(page.pixel(x, y)));
            }
        }
        QCOMPARE(lightest, 255);
    }

    // the page leans ten degrees, so a stem moves right by 0.18 of what it drops
    void textLeansWithThePage()
    {
        const QImage page = page_with("", "1");
        const QRectF box = ink(page, true);
        const auto left_edge = [&](double share) {
            const int y = qRound((box.top() + box.height() * share) * page.height());
            for (int x = 0; x < page.width(); x++)
            {
                if (qGray(page.pixel(x, y)) < 78)
                {
                    return x;
                }
            }
            return -1;
        };
        const double high = left_edge(0.45);
        const double low = left_edge(0.9);
        QVERIFY(high >= 0 && low >= 0);
        const double lean = (low - high) / (box.height() * 0.45 * page.height());
        QVERIFY2(near(lean, 0.176, 0.03), qPrintable(QString::number(lean)));
    }

    void scalesWithThePicture_data()
    {
        QTest::addColumn<int>("side");
        QTest::newRow("small") << 64;
        QTest::newRow("dock") << 128;
        QTest::newRow("large") << 1024;
    }

    void scalesWithThePicture()
    {
        QFETCH(int, side);
        const QRectF day = ink(page_with("JUL", "17", 512), true);
        const QRectF month = ink(page_with("JUL", "17", 512), false);
        // a pixel either way is a lot of a small picture
        const double slack = 0.004 + 1.5 / side;
        const QRectF small_day = ink(page_with("JUL", "17", side), true);
        const QRectF small_month = ink(page_with("JUL", "17", side), false);
        QVERIFY2(matches(small_day, day, slack), told(small_day).constData());
        QVERIFY2(matches(small_month, month, slack), told(small_month).constData());
    }

    void noWordsLeaveThePageAlone()
    {
        QImage plain(128, 128, QImage::Format_ARGB32_Premultiplied);
        plain.fill(QColor(128, 128, 128));
        QCOMPARE(page_with("", "", 128), plain);
        QVERIFY(page_with("JUL", "", 128) != plain);
        QVERIFY(page_with("", "17", 128) != plain);
        QVERIFY(page_with("JUL", "17", 128) != page_with("JUL", "18", 128));
    }

    void emptyPictureIsLeftEmpty()
    {
        QImage none;
        draw_date_page(none, "JUL", "17");
        QVERIFY(none.isNull());
    }

    // the month as the desktop's language shortens it, in capitals, and the day without a leading nought
    void wordsForADate_data()
    {
        QTest::addColumn<QDate>("date");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("month");
        QTest::addColumn<QString>("day");
        QTest::newRow("english") << QDate(2016, 7, 17) << "en_US" << "JUL" << "17";
        QTest::newRow("single digit") << QDate(2016, 2, 1) << "en_US" << "FEB" << "1";
        QTest::newRow("norwegian") << QDate(2026, 5, 31) << "nb_NO" << "MAI" << "31";
        QTest::newRow("norwegian december") << QDate(2026, 12, 11) << "nb_NO" << "DES" << "11";
        // the form a month has standing alone, which in norwegian is not the one inside a date
        QTest::newRow("standing alone") << QDate(2026, 7, 4) << "nb_NO" << "JUL" << "4";
        QTest::newRow("as the language shortens it") << QDate(2026, 1, 9) << "fr_FR" << "JANV." << "9";
    }

    void wordsForADate()
    {
        QFETCH(QDate, date);
        QFETCH(QString, language);
        QFETCH(QString, month);
        QFETCH(QString, day);
        const DatePageWords words = date_page_words(date, QLocale(language));
        QCOMPARE(words.month, month);
        QCOMPARE(words.day, day);
    }

    void whichPicturesAreAPage_data()
    {
        QTest::addColumn<QString>("icon");
        QTest::addColumn<bool>("page");
        QTest::newRow("by name") << "almanac" << true;
        QTest::newRow("a picture file") << "/opt/almanac/share/icons/hicolor/512x512/apps/almanac.png" << true;
        QTest::newRow("a drawing") << "/usr/share/icons/hicolor/scalable/apps/almanac.svg" << true;
        QTest::newRow("another program") << "calendar" << false;
        QTest::newRow("a longer name") << "almanac-symbolic" << false;
        QTest::newRow("a folder of that name") << "/home/someone/almanac/other.png" << false;
        QTest::newRow("a chosen picture") << "/home/someone/Pictures/my calendar.png" << false;
        QTest::newRow("nothing") << "" << false;
    }

    void whichPicturesAreAPage()
    {
        QFETCH(QString, icon);
        QFETCH(bool, page);
        QCOMPARE(is_date_page(icon), page);
    }

    void noDateNoWords()
    {
        const DatePageWords words = date_page_words(QDate(), QLocale(QStringLiteral("en_US")));
        QVERIFY(words.month.isEmpty() && words.day.isEmpty());
    }
};

QTEST_MAIN(TestDatePage)
#include "test-date-page.moc"
