// the day it is, renewed at midnight and when the clock is found on another day

#include <QSignalSpy>
#include <QtTest>

#include "today.hpp"

class TestToday : public QObject
{
    Q_OBJECT

  private:
    QDateTime now;

    Today::Clock clock()
    {
        return [this] { return now; };
    }

    static QDateTime at(int year, int month, int day, int hour, int minute, int second, int ms = 0)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, minute, second, ms));
    }

  private slots:
    void namesTheClocksDay()
    {
        now = at(2026, 11, 23, 14, 5, 0);
        Today today(nullptr, clock());
        QCOMPARE(today.date(), QDate(2026, 11, 23));
        QCOMPARE(today.stamp(), QStringLiteral("2026-11-23"));
    }

    void looksAgainWithinAMinute()
    {
        now = at(2026, 11, 23, 14, 5, 0);
        Today today(nullptr, clock());
        QCOMPARE(today.look(), 60000);
    }

    void looksAgainJustAfterMidnight()
    {
        now = at(2026, 11, 23, 23, 59, 30);
        Today today(nullptr, clock());
        QCOMPARE(today.look(), 30001);
        now = at(2026, 11, 23, 23, 59, 59, 999);
        QCOMPARE(today.look(), 2);
    }

    void saysSoOnceWhenTheDayTurns()
    {
        now = at(2026, 11, 23, 23, 59, 30);
        Today today(nullptr, clock());
        QSignalSpy turned(&today, &Today::changed);
        today.look();
        QCOMPARE(turned.count(), 0);
        now = at(2026, 11, 24, 0, 0, 0, 1);
        today.look();
        today.look();
        QCOMPARE(turned.count(), 1);
        QCOMPARE(today.stamp(), QStringLiteral("2026-11-24"));
    }

    // a clock set back, or a machine that slept through several days
    void followsAClockThatJumps_data()
    {
        QTest::addColumn<QDateTime>("later");
        QTest::addColumn<QString>("stamp");
        QTest::newRow("back") << at(2026, 11, 20, 9, 0, 0) << "2026-11-20";
        QTest::newRow("ahead") << at(2027, 1, 2, 9, 0, 0) << "2027-01-02";
    }

    void followsAClockThatJumps()
    {
        QFETCH(QDateTime, later);
        QFETCH(QString, stamp);
        now = at(2026, 11, 23, 12, 0, 0);
        Today today(nullptr, clock());
        QSignalSpy turned(&today, &Today::changed);
        now = later;
        today.look();
        QCOMPARE(turned.count(), 1);
        QCOMPARE(today.stamp(), stamp);
    }

    void turnsByItself()
    {
        now = at(2026, 11, 23, 23, 59, 59, 980);
        Today today(nullptr, clock());
        QSignalSpy turned(&today, &Today::changed);
        now = at(2026, 11, 24, 0, 0, 0, 5);
        QVERIFY2(turned.wait(2000), "no change within 2 s of a midnight 20 ms away");
        QCOMPARE(today.stamp(), QStringLiteral("2026-11-24"));
    }

    void showingIsSwitchedOnceAChange()
    {
        now = at(2026, 11, 23, 12, 0, 0);
        Today today(nullptr, clock());
        QSignalSpy switched(&today, &Today::shownChanged);
        QVERIFY(today.shown());
        today.setShown(true);
        QCOMPARE(switched.count(), 0);
        today.setShown(false);
        today.setShown(false);
        QCOMPARE(switched.count(), 1);
        QVERIFY(!today.shown());
    }

    void sharedOneIsTheSame()
    {
        QCOMPARE(Today::shared(), Today::shared());
        QCOMPARE(Today::shared()->date(), QDate::currentDate());
    }
};

QTEST_GUILESS_MAIN(TestToday)
#include "test-today.moc"
