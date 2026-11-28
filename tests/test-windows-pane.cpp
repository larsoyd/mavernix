#include <QtTest>

#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>

#include "choices.hpp"
#include "program-picker.hpp"
#include "windows-pane.hpp"

class TestWindowsPane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    // a missing control fails the test and hands back a stand-in, so later steps cannot crash
    QCheckBox missing_check;
    QSlider missing_slider;
    QPushButton missing_button;

    void start(const QJsonObject& chosen = {})
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject window;
        window[QStringLiteral("snap_to_edges")] = true;
        window[QStringLiteral("gaps")] = false;
        window[QStringLiteral("gap_size")] = 12;
        window[QStringLiteral("games_fill_screen")] = true;
        window[QStringLiteral("full_size_apps")] = QJsonArray();
        QJsonObject config;
        config[QStringLiteral("window")] = window;
        QJsonObject settings;
        settings[QStringLiteral("config")] = config;
        settings[QStringLiteral("default_config")] = config;
        settings[QStringLiteral("user_choices_file")] = folder.filePath(QStringLiteral("choices.json"));
        if (!chosen.isEmpty())
        {
            QFile choices(folder.filePath(QStringLiteral("choices.json")));
            QVERIFY(choices.open(QIODevice::WriteOnly));
            choices.write(QJsonDocument(chosen).toJson());
        }
        const QString path = folder.filePath(QStringLiteral("settings.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", path.toUtf8());
    }

    QJsonValue chosen(const QString& key)
    {
        QFile file(folder.filePath(QStringLiteral("choices.json")));
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("window")).toObject().value(key);
    }

    QCheckBox* check(QWidget& pane, const QString& words)
    {
        for (QCheckBox* found : pane.findChildren<QCheckBox*>())
        {
            if (found->text() == words)
            {
                return found;
            }
        }
        QTest::qFail(qPrintable(QStringLiteral("no tick box reads \"%1\"").arg(words)), __FILE__, __LINE__);
        return &missing_check;
    }

    template <typename Control> Control* named(QWidget& pane, const QString& name, Control* missing)
    {
        if (auto* found = pane.findChild<Control*>(name))
        {
            return found;
        }
        QTest::qFail(qPrintable(QStringLiteral("no control named %1").arg(name)), __FILE__, __LINE__);
        return missing;
    }

  private slots:
    void the_margins_switch_writes_gaps()
    {
        start();
        Choices choices;
        WindowsPane pane(choices);
        QCheckBox* gaps = check(pane, QStringLiteral("Tiled windows have margins"));
        QCOMPARE(gaps->isChecked(), false);
        gaps->click();
        QCOMPARE(chosen(QStringLiteral("gaps")), QJsonValue(true));
        gaps->click();
        QCOMPARE(chosen(QStringLiteral("gaps")), QJsonValue(false));
    }

    void the_margin_parts_wake_with_the_switch()
    {
        start();
        Choices choices;
        WindowsPane pane(choices);
        QCheckBox* gaps = check(pane, QStringLiteral("Tiled windows have margins"));
        QSlider* size = named<QSlider>(pane, QStringLiteral("windows_gap_size"), &missing_slider);
        QCheckBox* games = check(pane, QStringLiteral("Games have no margins"));
        QPushButton* apps = named<QPushButton>(pane, QStringLiteral("windows_full_size_apps"), &missing_button);
        QCOMPARE((QList<bool>{size->isEnabled(), games->isEnabled(), apps->isEnabled()}), (QList<bool>{false, false, false}));
        gaps->click();
        QCOMPARE((QList<bool>{size->isEnabled(), games->isEnabled(), apps->isEnabled()}), (QList<bool>{true, true, true}));
    }

    void the_slider_writes_the_next_size_up()
    {
        start(QJsonObject{{QStringLiteral("window"), QJsonObject{{QStringLiteral("gaps"), true}}}});
        Choices choices;
        WindowsPane pane(choices);
        QSlider* size = named<QSlider>(pane, QStringLiteral("windows_gap_size"), &missing_slider);
        size->setFocus();
        QTest::keyClick(size, Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(chosen(QStringLiteral("gap_size")), QJsonValue(16), 3000);
    }

    void games_can_keep_margins()
    {
        start(QJsonObject{{QStringLiteral("window"), QJsonObject{{QStringLiteral("gaps"), true}}}});
        Choices choices;
        WindowsPane pane(choices);
        QCheckBox* games = check(pane, QStringLiteral("Games have no margins"));
        QCOMPARE(games->isChecked(), true);
        games->click();
        QCOMPARE(chosen(QStringLiteral("games_fill_screen")), QJsonValue(false));
    }

    void programs_are_added_and_removed()
    {
        start();
        Choices choices;
        QStringList offered = {QStringLiteral("foot.desktop"), QStringLiteral("org.gimp.GIMP.desktop"),
            QStringLiteral("foot.desktop"), QString()};
        ProgramList list(choices, QStringLiteral("window"), QStringLiteral("full_size_apps"),
            [&offered](QWidget*) { return offered.takeFirst(); });
        auto* add = list.findChild<QPushButton*>(QStringLiteral("add"));
        auto* remove = list.findChild<QPushButton*>(QStringLiteral("remove"));
        auto* shown = list.findChild<QListWidget*>();
        QVERIFY(add && remove && shown);
        add->click();
        add->click();
        // the same program twice, then a picker closed without a choice, change nothing
        add->click();
        add->click();
        QCOMPARE(chosen(QStringLiteral("full_size_apps")),
            QJsonValue(QJsonArray{QStringLiteral("foot.desktop"), QStringLiteral("org.gimp.GIMP.desktop")}));
        QCOMPARE(shown->count(), 2);
        QCOMPARE(remove->isEnabled(), true);
        shown->setCurrentRow(0);
        remove->click();
        QCOMPARE(chosen(QStringLiteral("full_size_apps")), QJsonValue(QJsonArray{QStringLiteral("org.gimp.GIMP.desktop")}));
        shown->setCurrentRow(0);
        remove->click();
        QCOMPARE(chosen(QStringLiteral("full_size_apps")), QJsonValue(QJsonArray()));
        QCOMPARE(remove->isEnabled(), false);
    }

    void a_program_no_longer_installed_still_shows_and_goes()
    {
        start(QJsonObject{{QStringLiteral("window"),
            QJsonObject{{QStringLiteral("full_size_apps"), QJsonArray{QStringLiteral("gone.desktop")}}}}});
        Choices choices;
        ProgramList list(choices, QStringLiteral("window"), QStringLiteral("full_size_apps"),
            [](QWidget*) { return QString(); });
        auto* shown = list.findChild<QListWidget*>();
        QVERIFY(shown);
        QCOMPARE(shown->count(), 1);
        QCOMPARE(shown->item(0)->text(), QStringLiteral("gone"));
    }
};

QTEST_MAIN(TestWindowsPane)
#include "test-windows-pane.moc"
