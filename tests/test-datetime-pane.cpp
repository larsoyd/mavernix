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
        return nullptr;
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
        return nullptr;
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
        QCheckBox* seconds = box(pane, QStringLiteral("Show seconds"));
        QVERIFY(seconds != nullptr);
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
        QCheckBox* hours = box(pane, QStringLiteral("Show the time with 24 hours"));
        QVERIFY(hours != nullptr);
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
        QVERIFY(position != nullptr);
        QCOMPARE(position->currentData().toString(), QStringLiteral("right"));
        QCOMPARE(position->count(), 3);
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
        QVERIFY(time != nullptr && date != nullptr);
        QVERIFY(time->isChecked() && !date->isChecked());
        QVERIFY(!time->isEnabled());
        date->setChecked(true);
        QCOMPARE(panelValue(choices, "clock_shows_date").toBool(), true);
        QVERIFY(time->isEnabled() && date->isEnabled());
        time->setChecked(false);
        QCOMPARE(panelValue(choices, "clock_shows_time").toBool(), false);
        QVERIFY(!date->isEnabled());
        QVERIFY(!box(pane, QStringLiteral("Show seconds"))->isEnabled());
    }

    void theDateStyleIsChosenOnlyWhileTheDateShows()
    {
        start(QStringLiteral("HH:mm"), {{"clock_shows_date", false}, {"clock_date_style", "full"}});
        Choices choices;
        DateTimePane pane(choices);
        QComboBox* style = popup(pane, QStringLiteral("numeric"));
        QVERIFY(style != nullptr);
        QCOMPARE(style->count(), 4);
        QCOMPARE(style->currentData().toString(), QStringLiteral("full"));
        QVERIFY(!style->isEnabled());
        box(pane, QStringLiteral("Show date"))->setChecked(true);
        QVERIFY(style->isEnabled());
        choose(style, QStringLiteral("region"));
        QCOMPARE(panelValue(choices, "clock_date_style").toString(), QStringLiteral("region"));
    }

    void theDayOfTheWeekGoesInFrontOfTheFormat()
    {
        start(QStringLiteral("HH:mm"));
        Choices choices;
        DateTimePane pane(choices);
        QCheckBox* weekday = box(pane, QStringLiteral("Show the day of the week"));
        QVERIFY(weekday != nullptr);
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
        QVERIFY(region != nullptr);
        QCheckBox* hours = box(pane, QStringLiteral("Show the time with 24 hours"));
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
