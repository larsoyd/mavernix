#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QRadioButton>
#include <QTemporaryDir>

#include "choices.hpp"
#include "dock-pane.hpp"
#include "pane-resets.hpp"

class TestDockPane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    // a missing control fails the test and hands back a stand-in, so later steps cannot crash
    QRadioButton missing_button;

    void start()
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject dock;
        dock[QStringLiteral("position")] = QStringLiteral("bottom");
        QJsonObject config;
        config[QStringLiteral("dock")] = dock;
        QJsonObject settings;
        settings[QStringLiteral("config")] = config;
        settings[QStringLiteral("default_config")] = config;
        settings[QStringLiteral("user_choices_file")] = folder.filePath(QStringLiteral("choices.json"));
        const QString path = folder.filePath(QStringLiteral("settings.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", path.toUtf8());
    }

    QRadioButton* button(QWidget& pane, const QString& words)
    {
        for (QRadioButton* found : pane.findChildren<QRadioButton*>())
        {
            if (found->text() == words)
            {
                return found;
            }
        }
        QTest::qFail(qPrintable(QStringLiteral("no button reads \"%1\"").arg(words)), __FILE__, __LINE__);
        return &missing_button;
    }

  private slots:
    void theDockStartsAtTheBottom()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        QCOMPARE(button(pane, QStringLiteral("Bottom"))->isChecked(), true);
        QCOMPARE(button(pane, QStringLiteral("Left"))->isChecked(), false);
        QCOMPARE(button(pane, QStringLiteral("Right"))->isChecked(), false);
    }

    void choosingASideWritesIt()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        button(pane, QStringLiteral("Left"))->click();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("position")).toString(), QStringLiteral("left"));
        button(pane, QStringLiteral("Right"))->click();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("position")).toString(), QStringLiteral("right"));
    }

    // the dock's own menu changes it too while the pane is open
    void theRowFollowsTheFile()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        Choices other;
        other.set(QStringLiteral("dock"), QStringLiteral("position"), QStringLiteral("right"));
        QTRY_VERIFY_WITH_TIMEOUT(button(pane, QStringLiteral("Right"))->isChecked(), 3000);
    }

    void resettingThePaneTakesItBack()
    {
        QStringList reset;
        for (const Choices::Key& key : pane_choices(QStringLiteral("dock")))
        {
            reset << key.table + QLatin1Char('.') + key.key;
        }
        QVERIFY2(reset.contains(QStringLiteral("dock.position")), qPrintable(reset.join(' ')));
    }
};

QTEST_MAIN(TestDockPane)
#include "test-dock-pane.moc"
