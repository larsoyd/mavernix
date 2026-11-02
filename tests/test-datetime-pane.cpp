#include <QtTest>

#include <QCheckBox>
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

    void start(const QString& format)
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject panel;
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
};

QTEST_MAIN(TestDateTimePane)
#include "test-datetime-pane.moc"
