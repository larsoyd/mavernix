#include <QtTest>

#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "choices.hpp"
#include "desktop-pane.hpp"
#include "pane-resets.hpp"

class TestDesktopPane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    // a missing control fails the test and hands back a stand-in, so later steps cannot crash
    QCheckBox missing_box;

    void start(const QJsonObject& desktop_files)
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject config;
        config[QStringLiteral("desktop_files")] = desktop_files;
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

    static QVariant chosen(const Choices& choices, const char* key)
    {
        return choices.value(QStringLiteral("desktop_files"), QLatin1String(key));
    }

  private slots:
    void bothAreOffAsTheConfigSays()
    {
        start({{QStringLiteral("show"), true}, {QStringLiteral("snap_to_grid"), false},
            {QStringLiteral("deselect_after_opening"), false}});
        Choices choices;
        DesktopPane pane(choices);
        QCOMPARE(box(pane, QStringLiteral("Snap icons to grid"))->isChecked(), false);
        QCOMPARE(box(pane, QStringLiteral("Deselect icons after opening"))->isChecked(), false);
    }

    void tickingThemWritesTheChoices()
    {
        start({{QStringLiteral("show"), true}});
        Choices choices;
        DesktopPane pane(choices);
        box(pane, QStringLiteral("Snap icons to grid"))->setChecked(true);
        box(pane, QStringLiteral("Deselect icons after opening"))->setChecked(true);
        QCOMPARE(chosen(choices, "snap_to_grid"), QVariant(true));
        QCOMPARE(chosen(choices, "deselect_after_opening"), QVariant(true));
    }

    void theyShowWhatIsChosen()
    {
        start({{QStringLiteral("show"), true}, {QStringLiteral("snap_to_grid"), true},
            {QStringLiteral("deselect_after_opening"), true}});
        Choices choices;
        DesktopPane pane(choices);
        QCOMPARE(box(pane, QStringLiteral("Snap icons to grid"))->isChecked(), true);
        QCOMPARE(box(pane, QStringLiteral("Deselect icons after opening"))->isChecked(), true);
    }

    void theyWaitForTheFilesToBeShown()
    {
        start({{QStringLiteral("show"), false}});
        Choices choices;
        DesktopPane pane(choices);
        QCheckBox* files = box(pane, QStringLiteral("Show the files of the Desktop folder"));
        QCOMPARE(box(pane, QStringLiteral("Snap icons to grid"))->isEnabled(), false);
        QCOMPARE(box(pane, QStringLiteral("Deselect icons after opening"))->isEnabled(), false);
        files->setChecked(true);
        QCOMPARE(box(pane, QStringLiteral("Snap icons to grid"))->isEnabled(), true);
        QCOMPARE(box(pane, QStringLiteral("Deselect icons after opening"))->isEnabled(), true);
    }

    void resettingThePaneTakesThemBack()
    {
        QStringList reset;
        for (const Choices::Key& key : pane_choices(QStringLiteral("desktop")))
        {
            reset << key.table + QLatin1Char('.') + key.key;
        }
        QVERIFY2(reset.contains(QStringLiteral("desktop_files.snap_to_grid")), qPrintable(reset.join(' ')));
        QVERIFY2(reset.contains(QStringLiteral("desktop_files.deselect_after_opening")), qPrintable(reset.join(' ')));
    }
};

QTEST_MAIN(TestDesktopPane)
#include "test-desktop-pane.moc"
