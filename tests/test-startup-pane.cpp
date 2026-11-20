// the Startup pane shows and changes Hide beside each login item

#include <QtTest>

#include <QDir>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "choices.hpp"
#include "startup-pane.hpp"

class TestStartupPane : public QObject
{
    Q_OBJECT

    QTemporaryDir folder;

    QString own(const QString& file_name) const { return folder.filePath(QStringLiteral("config/autostart/") + file_name); }

    void write_entry(const QString& file_name, const QString& name, const QString& more = QString())
    {
        QDir().mkpath(folder.filePath(QStringLiteral("config/autostart")));
        QFile file(own(file_name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=true\n%2").arg(name, more).toUtf8());
    }

    QString read(const QString& file_name) const
    {
        QFile file(own(file_name));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    static QTreeWidget* tree(QWidget& pane) { return pane.findChild<QTreeWidget*>(); }

  private slots:
    void initTestCase()
    {
        QVERIFY(folder.isValid());
        qputenv("XDG_CONFIG_HOME", folder.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", folder.filePath(QStringLiteral("xdg")).toUtf8());
        qunsetenv("XDG_CURRENT_DESKTOP");
        QJsonObject settings;
        settings[QStringLiteral("config")] = QJsonObject();
        settings[QStringLiteral("user_choices_file")] = folder.filePath(QStringLiteral("choices.json"));
        QFile file(folder.filePath(QStringLiteral("settings.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", folder.filePath(QStringLiteral("settings.json")).toUtf8());
    }

    void cleanup() { QDir(folder.filePath(QStringLiteral("config/autostart"))).removeRecursively(); }

    void each_item_shows_its_hide_tick()
    {
        write_entry("lutris.desktop", "Lutris", "X-Mavernix-HideAtLogin=true\n");
        write_entry("mail.desktop", "Mail");
        Choices choices;
        StartupPane pane(choices);
        QTreeWidget* list = tree(pane);
        QVERIFY2(list, "the pane has no list of login items");
        QCOMPARE(list->columnCount(), 2);
        QCOMPARE(list->headerItem()->text(1), QStringLiteral("Hide"));
        QCOMPARE(list->topLevelItemCount(), 2);
        QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("Lutris"));
        QCOMPARE(list->topLevelItem(0)->checkState(1), Qt::Checked);
        QCOMPARE(list->topLevelItem(1)->checkState(1), Qt::Unchecked);
        QCOMPARE(list->topLevelItem(1)->checkState(0), Qt::Checked);
    }

    void ticking_hide_marks_the_entry_and_unticking_clears_it()
    {
        write_entry("mail.desktop", "Mail");
        Choices choices;
        StartupPane pane(choices);
        QVERIFY2(tree(pane) && tree(pane)->topLevelItemCount() == 1, "the pane lists no login item");
        tree(pane)->topLevelItem(0)->setCheckState(1, Qt::Checked);
        QTRY_VERIFY(read("mail.desktop").contains(QStringLiteral("X-Mavernix-HideAtLogin=true")));
        QTRY_COMPARE(tree(pane)->topLevelItemCount(), 1);
        QVERIFY(tree(pane)->topLevelItemCount() == 1);
        tree(pane)->topLevelItem(0)->setCheckState(1, Qt::Unchecked);
        QTRY_VERIFY(!read("mail.desktop").contains(QStringLiteral("X-Mavernix-HideAtLogin")));
        QVERIFY(read("mail.desktop").contains(QStringLiteral("Exec=true")));
    }

    void the_open_tick_still_switches_the_item()
    {
        write_entry("mail.desktop", "Mail");
        Choices choices;
        StartupPane pane(choices);
        QVERIFY2(tree(pane) && tree(pane)->topLevelItemCount() == 1, "the pane lists no login item");
        tree(pane)->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QTRY_VERIFY(read("mail.desktop").contains(QStringLiteral("Hidden=true")));
        QVERIFY(!read("mail.desktop").contains(QStringLiteral("X-Mavernix-HideAtLogin")));
    }

    void the_keys_reach_the_hide_tick()
    {
        write_entry("mail.desktop", "Mail");
        Choices choices;
        StartupPane pane(choices);
        pane.show();
        QTreeWidget* list = tree(pane);
        QVERIFY2(list && list->topLevelItemCount() == 1, "the pane lists no login item");
        list->setFocus();
        list->setCurrentItem(list->topLevelItem(0), 0);
        QTest::keyClick(list, Qt::Key_Right);
        QCOMPARE(list->currentIndex().column(), 1);
        QTest::keyClick(list, Qt::Key_Space);
        QTRY_VERIFY(read("mail.desktop").contains(QStringLiteral("X-Mavernix-HideAtLogin=true")));
    }

    void a_long_name_is_cut_inside_its_column()
    {
        write_entry("long.desktop", QString(400, QLatin1Char('W')));
        Choices choices;
        StartupPane pane(choices);
        pane.show();
        QTreeWidget* list = tree(pane);
        QVERIFY2(list, "the pane has no list of login items");
        QCOMPARE(list->textElideMode(), Qt::ElideRight);
        QVERIFY(list->header()->sectionSize(0) + list->header()->sectionSize(1) <= list->viewport()->width());
        QVERIFY(list->header()->sectionSize(1) > 0);
    }
};

QTEST_MAIN(TestStartupPane)
#include "test-startup-pane.moc"
