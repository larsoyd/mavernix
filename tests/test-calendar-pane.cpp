#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "calendar-pane.hpp"
#include "choices.hpp"

class TestCalendarPane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    // a missing control fails the test and hands back a stand-in, so later steps cannot crash
    QCheckBox missing_box;
    QComboBox missing_popup;

    void start(const QJsonObject& calendar = {})
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject config;
        config[QStringLiteral("calendar")] = calendar;
        QJsonObject settings;
        settings[QStringLiteral("config")] = config;
        settings[QStringLiteral("user_choices_file")] = folder.filePath(QStringLiteral("choices.json"));
        const QString path = folder.filePath(QStringLiteral("settings.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", path.toUtf8());
        // two calendars as almanac keeps them
        QDir().mkpath(folder.filePath(QStringLiteral("data/almanac/calendars")));
        for (const auto& [id, name] : {std::pair{"home", "Home"}, std::pair{"work", "Work"}})
        {
            QFile ics(folder.filePath(QStringLiteral("data/almanac/calendars/%1.ics").arg(QLatin1String(id))));
            QVERIFY(ics.open(QIODevice::WriteOnly));
            ics.write(QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-WR-CALNAME:%1\r\nEND:VCALENDAR\r\n").arg(QLatin1String(name)).toUtf8());
        }
        qputenv("XDG_DATA_HOME", folder.filePath(QStringLiteral("data")).toUtf8());
    }

    QCheckBox* box(QWidget& pane, const QString& words)
    {
        for (QCheckBox* found : pane.findChildren<QCheckBox*>())
        {
            if (found->text() == words)
            {
                return found;
            }
        }
        QTest::qFail(qPrintable(QStringLiteral("no tick box reads \"%1\"").arg(words)), __FILE__, __LINE__);
        return &missing_box;
    }

    QComboBox* popup(QWidget& pane, const QVariant& value)
    {
        for (QComboBox* found : pane.findChildren<QComboBox*>())
        {
            if (found->findData(value) >= 0)
            {
                return found;
            }
        }
        QTest::qFail(qPrintable(QStringLiteral("no popup offers %1").arg(value.toString())), __FILE__, __LINE__);
        return &missing_popup;
    }

    static QVariant stored(const Choices& choices, const char* key)
    {
        return choices.value(QStringLiteral("calendar"), QLatin1String(key));
    }

    static void choose(QComboBox* list, const QVariant& value)
    {
        list->setCurrentIndex(list->findData(value));
        emit list->activated(list->currentIndex());
    }

  private slots:
    void daysPerWeekAreFiveOrSeven()
    {
        start();
        Choices choices;
        CalendarPane pane(choices);
        QComboBox* days = popup(pane, 5);
        QCOMPARE(days->count(), 2);
        QCOMPARE(days->currentData().toInt(), 7);
        choose(days, 5);
        QCOMPARE(stored(choices, "days_per_week").toInt(), 5);
    }

    void theWeekStartsWithTheRegionUntilADayIsChosen()
    {
        start();
        Choices choices;
        CalendarPane pane(choices);
        QComboBox* first = popup(pane, QStringLiteral("region"));
        QCOMPARE(first->count(), 8);
        QCOMPARE(first->currentData().toString(), QStringLiteral("region"));
        choose(first, 1);
        QCOMPARE(stored(choices, "first_weekday").toInt(), 1);
        choose(first, QStringLiteral("region"));
        QVERIFY(!stored(choices, "first_weekday").isValid() || stored(choices, "first_weekday").isNull());
    }

    void workingHoursAreChosenByTheHour()
    {
        start();
        Choices choices;
        CalendarPane pane(choices);
        // as the original: mornings from midnight to noon, evenings from noon to midnight
        QComboBox* starts = popup(pane, 0);
        QComboBox* ends = popup(pane, 1440);
        QCOMPARE(starts->count(), 13);
        QCOMPARE(ends->count(), 13);
        QCOMPARE(starts->currentData().toInt(), 480);
        QCOMPARE(ends->currentData().toInt(), 1080);
        choose(starts, 420);
        choose(ends, 1200);
        QCOMPARE(stored(choices, "day_start_minute").toInt(), 420);
        QCOMPARE(stored(choices, "day_end_minute").toInt(), 1200);
    }

    void hoursShownGoFromSixToTwentyFour()
    {
        start({{"hours_shown", 10}});
        Choices choices;
        CalendarPane pane(choices);
        QComboBox* hours = popup(pane, 24);
        QCOMPARE(hours->count(), 19);
        QCOMPARE(hours->currentData().toInt(), 10);
        choose(hours, 16);
        QCOMPARE(stored(choices, "hours_shown").toInt(), 16);
    }

    void holidaysAndTheYearsEventsAreTicked()
    {
        start();
        Choices choices;
        CalendarPane pane(choices);
        QCheckBox* holidays = box(pane, QStringLiteral("Show Holidays calendar"));
        QCheckBox* heat = box(pane, QStringLiteral("Show events in year view"));
        QVERIFY(holidays->isChecked());
        QVERIFY(!heat->isChecked());
        holidays->setChecked(false);
        heat->setChecked(true);
        QCOMPARE(stored(choices, "holidays").toBool(), false);
        QCOMPARE(stored(choices, "year_heat_map").toBool(), true);
    }

    void defaultCalendarListsAlmanacsCalendars()
    {
        start();
        Choices choices;
        CalendarPane pane(choices);
        QComboBox* calendars = popup(pane, QStringLiteral("work"));
        QCOMPARE(calendars->count(), 3);
        QCOMPARE(calendars->currentData().toString(), QString());
        QCOMPARE(calendars->itemText(calendars->findData(QStringLiteral("home"))), QStringLiteral("Home"));
        choose(calendars, QStringLiteral("work"));
        QCOMPARE(stored(choices, "default_calendar").toString(), QStringLiteral("work"));
    }

    void theClockAndTheIconAreChosenHere()
    {
        start();
        Choices choices;
        CalendarPane pane(choices);
        QComboBox* click = popup(pane, QStringLiteral("app"));
        QCOMPARE(click->currentData().toString(), QStringLiteral("calendar"));
        choose(click, QStringLiteral("date"));
        QCOMPARE(stored(choices, "clock_click").toString(), QStringLiteral("date"));
        QCheckBox* icon = box(pane, QStringLiteral("Show today’s date on the Almanac icon"));
        QVERIFY(icon->isChecked());
        icon->setChecked(false);
        QCOMPARE(stored(choices, "icon_date").toBool(), false);
    }
};

QTEST_MAIN(TestCalendarPane)
#include "test-calendar-pane.moc"
