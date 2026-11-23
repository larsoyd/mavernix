// where the month and day sit on a calendar page and how they lean

#include <QtTest>

#include "date-page.hpp"

class TestDatePage : public QObject
{
    Q_OBJECT

  private:
    // a grey page, so the white month and the dark day both show
    static QImage page_with(const QString& month, const QString& day, int side = 512)
    {
        QImage page(side, side, QImage::Format_ARGB32_Premultiplied);
        page.fill(QColor(128, 128, 128));
        draw_date_page(page, month, day);
        return page;
    }

    // the box around every pixel on the dark or the light side of the grey, in shares of the side
    static QRectF ink(const QImage& page, bool dark)
    {
        QRect box;
        for (int y = 0; y < page.height(); y++)
        {
            for (int x = 0; x < page.width(); x++)
            {
                const int level = qGray(page.pixel(x, y));
                if (dark ? level < 90 : level > 200)
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

  private slots:
    // on the page it is drawn for, a 17 fills 0.30 to 0.69 across and 0.38 to 0.72 down
    void dayIsCentredOnThePage_data()
    {
        QTest::addColumn<QString>("day");
        QTest::newRow("two digits") << "17";
        QTest::newRow("one digit") << "1";
        QTest::newRow("widest") << "28";
    }

    void dayIsCentredOnThePage()
    {
        QFETCH(QString, day);
        const QRectF box = ink(page_with("JUL", day), true);
        QVERIFY2(near(box.center().x(), 0.498, 0.025), qPrintable(QString::number(box.center().x())));
        QVERIFY2(near(box.center().y(), 0.547, 0.025), qPrintable(QString::number(box.center().y())));
    }

    void dayIsAsTallAsThePageWantsIt()
    {
        const QRectF box = ink(page_with("JUL", "17"), true);
        QVERIFY2(near(box.height(), 0.341, 0.04), qPrintable(QString::number(box.height())));
        QVERIFY2(near(box.width(), 0.393, 0.07), qPrintable(QString::number(box.width())));
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
                if (qGray(page.pixel(x, y)) < 90)
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
        QVERIFY2(near(lean, 0.18, 0.05), qPrintable(QString::number(lean)));
    }

    // JUL starts 0.155 across and stands between 0.28 and 0.39 down
    void monthStartsAtTheLeftOfTheBand()
    {
        const QRectF box = ink(page_with("JUL", "17"), false);
        QVERIFY2(near(box.left(), 0.155, 0.02), qPrintable(QString::number(box.left())));
        QVERIFY2(near(box.top(), 0.276, 0.03), qPrintable(QString::number(box.top())));
        QVERIFY2(near(box.bottom(), 0.39, 0.03), qPrintable(QString::number(box.bottom())));
        QVERIFY2(near(box.right(), 0.366, 0.04), qPrintable(QString::number(box.right())));
    }

    // the band's small calendars begin 0.42 across
    void longMonthStaysOffTheSmallCalendars()
    {
        const QRectF box = ink(page_with("WWWWWW", "17"), false);
        QVERIFY2(box.right() <= 0.41, qPrintable(QString::number(box.right())));
        QVERIFY2(near(box.left(), 0.155, 0.02), qPrintable(QString::number(box.left())));
        QVERIFY(box.height() > 0.03);
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
        const QRectF wanted = ink(page_with("JUL", "17", 512), true);
        const QRectF box = ink(page_with("JUL", "17", side), true);
        // a pixel either way is a lot of a small picture
        const double slack = 0.02 + 2.0 / side;
        QVERIFY2(near(box.left(), wanted.left(), slack) && near(box.right(), wanted.right(), slack) &&
                     near(box.top(), wanted.top(), slack) && near(box.bottom(), wanted.bottom(), slack),
            qPrintable(QStringLiteral("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.right()).arg(box.bottom())));
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
};

QTEST_MAIN(TestDatePage)
#include "test-date-page.moc"
