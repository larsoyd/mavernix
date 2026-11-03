#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "choices.hpp"
#include "datetime-pane.hpp"

class TestDateTimePane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    // a missing control fails the test and hands back a stand-in, so later steps cannot crash
    QCheckBox missing_box;
    QComboBox missing_popup;

    void start(const QString& format, QJsonObject panel = {})
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        panel[QStringLiteral("clock_format")] = format;
        QJsonObject config;
        config[QStringLiteral("panel")] = panel;
        QJsonObject settings;
        settings[QStringLiteral("config")] = config;
        settings[QStringLiteral("user_choices_file")] = folder.filePath(QStringLiteral("choices.json"));
        const QString path = folder.filePath(QStringLiteral("settings.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", path.toUtf8());
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

    QComboBox* popup(QWidget& pane, const QString& value)
    {
        for (QComboBox* found : pane.findChildren<QComboBox*>())
        {
            if (found->findData(value) >= 0)
            {
                return found;
            }
        }
        QTest::qFail(qPrintable(QStringLiteral("no popup offers %1").arg(value)), __FILE__, __LINE__);
        return &missing_popup;
    }

    QVariant panelValue(const Choices& choices, const char* key)
    {
        return choices.value(QStringLiteral("panel"), QLatin1String(key));
    }

    static void choose(QComboBox* list, const QString& value)
    {
        list->setCurrentIndex(list->findData(value));
        emit list->activated(list->currentIndex());
    }

  private slots:
    void secondsGoIntoTheFormatAndOutAgain()
    {
        start(QStringLiteral("ddd HH:mm"));
        Choices choices;
        DateTimePane pane(choices);
        QCheckBox* seconds = box(pane, QStringLiteral("Display the time with seconds"));
        QVERIFY(!seconds->isChecked());
        seconds->setChecked(true);
        QCOMPARE(choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString(),
            QStringLiteral("ddd HH:mm:ss"));
        seconds->setChecked(false);
        QCOMPARE(choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString(),
            QStringLiteral("ddd HH:mm"));
    }

    void twelveHoursAddTheMarkAnd24TakeItAway()
    {
        start(QStringLiteral("HH:mm"));
        Choices choices;
        DateTimePane pane(choices);
        QCheckBox* hours = box(pane, QStringLiteral("Use a 24-hour clock"));
        QVERIFY(hours->isChecked());
        hours->setChecked(false);
        QCOMPARE(choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString(),
            QStringLiteral("h:mm AP"));
        hours->setChecked(true);
        QCOMPARE(choices.value(QStringLiteral("panel"), QStringLiteral("clock_format")).toString(),
            QStringLiteral("HH:mm"));
    }

    void theClockStandsWhereItIsPut()
    {
        start(QStringLiteral("HH:mm"), {{"clock_position", "right"}});
        Choices choices;
        DateTimePane pane(choices);
        QComboBox* position = popup(pane, QStringLiteral("middle"));
        QCOMPARE(position->currentData().toString(), QStringLiteral("right"));
        QCOMPARE(position->count(), 3);
        QCOMPARE(position->itemText(position->findData(QStringLiteral("middle"))), QStringLiteral("Center"));
        choose(position, QStringLiteral("left"));
        QCOMPARE(panelValue(choices, "clock_position").toString(), QStringLiteral("left"));
        choose(position, QStringLiteral("middle"));
        QCOMPARE(panelValue(choices, "clock_position").toString(), QStringLiteral("middle"));
    }

    void aClockWithNoPlaceStandsAtTheRight()
    {
        start(QStringLiteral("HH:mm"));
        Choices choices;
        DateTimePane pane(choices);
        QCOMPARE(popup(pane, QStringLiteral("middle"))->currentData().toString(), QStringLiteral("right"));
    }

    void timeAndDateCanEachBeShownButNotBothLeftOut()
    {
        start(QStringLiteral("HH:mm"), {{"clock_shows_time", true}, {"clock_shows_date", false}});
        Choices choices;
        DateTimePane pane(choices);
        QCheckBox* time = box(pane, QStringLiteral("Show the time"));
        QCheckBox* date = box(pane, QStringLiteral("Show date"));
        QVERIFY(time->isChecked() && !date->isChecked());
        QVERIFY(!time->isEnabled());
        date->setChecked(true);
        QCOMPARE(panelValue(choices, "clock_shows_date").toBool(), true);
        QVERIFY(time->isEnabled() && date->isEnabled());
        time->setChecked(false);
        QCOMPARE(panelValue(choices, "clock_shows_time").toBool(), false);
        QVERIFY(!date->isEnabled());
        QVERIFY(!box(pane, QStringLiteral("Display the time with seconds"))->isEnabled());
    }

    void theDateStyleIsChosenOnlyWhileTheDateShows()
    {
        start(QStringLiteral("HH:mm"), {{"clock_shows_date", false}, {"clock_date_style", "medium"}});
        Choices choices;
        DateTimePane pane(choices);
        QComboBox* style = popup(pane, QStringLiteral("short"));
        QCOMPARE(style->count(), 3);
        QCOMPARE(style->itemText(0), QStringLiteral("Long"));
        QCOMPARE(style->itemText(1), QStringLiteral("Medium"));
        QCOMPARE(style->itemText(2), QStringLiteral("Short"));
        QCOMPARE(style->currentData().toString(), QStringLiteral("medium"));
        QVERIFY(!style->isEnabled());
        box(pane, QStringLiteral("Show date"))->setChecked(true);
        QVERIFY(style->isEnabled());
        choose(style, QStringLiteral("long"));
        QCOMPARE(panelValue(choices, "clock_date_style").toString(), QStringLiteral("long"));
    }

    void aStyleNoLongerOfferedShowsAsLong()
    {
        start(QStringLiteral("HH:mm"), {{"clock_shows_date", true}, {"clock_date_style", "region"}});
        Choices choices;
        DateTimePane pane(choices);
        QCOMPARE(popup(pane, QStringLiteral("short"))->currentData().toString(), QStringLiteral("long"));
    }

    void theRegionsDateFormatIsADateOption()
    {
        start(QStringLiteral("HH:mm"), {{"clock_shows_date", false}});
        Choices choices;
        DateTimePane pane(choices);
        QCheckBox* region = box(pane, QStringLiteral("Use the region's date format"));
        QVERIFY(!region->isChecked());
        QVERIFY(!region->isEnabled());
        box(pane, QStringLiteral("Show the day of the week"))->setChecked(true);
        QVERIFY(region->isEnabled());
        box(pane, QStringLiteral("Show the day of the week"))->setChecked(false);
        box(pane, QStringLiteral("Show date"))->setChecked(true);
        QVERIFY(region->isEnabled());
        region->setChecked(true);
        QCOMPARE(panelValue(choices, "clock_region_date").toBool(), true);
        QVERIFY(popup(pane, QStringLiteral("short"))->isEnabled());
    }

    void theDayOfTheWeekGoesInFrontOfTheFormat()
    {
        start(QStringLiteral("HH:mm"));
        Choices choices;
        DateTimePane pane(choices);
        QCheckBox* weekday = box(pane, QStringLiteral("Show the day of the week"));
        QVERIFY(!weekday->isChecked());
        weekday->setChecked(true);
        QCOMPARE(panelValue(choices, "clock_format").toString(), QStringLiteral("ddd HH:mm"));
        weekday->setChecked(false);
        QCOMPARE(panelValue(choices, "clock_format").toString(), QStringLiteral("HH:mm"));

        start(QStringLiteral("dddd h:mm AP"));
        Choices again;
        DateTimePane long_day(again);
        QVERIFY(box(long_day, QStringLiteral("Show the day of the week"))->isChecked());
        box(long_day, QStringLiteral("Show the day of the week"))->setChecked(false);
        QCOMPARE(panelValue(again, "clock_format").toString(), QStringLiteral("h:mm AP"));
    }

    void theRegionsTimeFormatTakesOverTheHours()
    {
        start(QStringLiteral("HH:mm"), {{"clock_region_time", false}});
        Choices choices;
        DateTimePane pane(choices);
        QCheckBox* region = box(pane, QStringLiteral("Use the region's time format"));
        QCheckBox* hours = box(pane, QStringLiteral("Use a 24-hour clock"));
        QVERIFY(hours->isEnabled());
        region->setChecked(true);
        QCOMPARE(panelValue(choices, "clock_region_time").toBool(), true);
        QVERIFY(!hours->isEnabled());
        region->setChecked(false);
        QVERIFY(hours->isEnabled());
    }
};

QTEST_MAIN(TestDateTimePane)
#include "test-datetime-pane.moc"
