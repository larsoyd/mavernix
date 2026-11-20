// login items ticked Hide keep the mark in the user's copy, and switching never loses it

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "startup-entries.hpp"

namespace
{
const char* const MARK = "X-Mavernix-HideAtLogin";

QString read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void write(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8());
}

QString entry(const QString& name, const QString& more = QString())
{
    return QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=true\n%2").arg(name, more);
}

const StartupEntry* find(const QList<StartupEntry>& entries, const QString& file_name)
{
    for (const StartupEntry& entry : entries)
    {
        if (entry.file_name == file_name)
        {
            return &entry;
        }
    }
    return nullptr;
}
} // namespace

class TestStartupEntries : public QObject
{
    Q_OBJECT

    QTemporaryDir folder;
    QString users;
    QString system;

    QString own(const QString& file_name) const { return users + QLatin1Char('/') + file_name; }
    QString theirs(const QString& file_name) const { return system + QLatin1Char('/') + file_name; }

    bool hidden_in_list(const QString& file_name)
    {
        const QList<StartupEntry> entries = startup_entries();
        const StartupEntry* found = find(entries, file_name);
        return found != nullptr && found->hide;
    }

  private slots:
    void initTestCase()
    {
        QVERIFY(folder.isValid());
        qputenv("XDG_CONFIG_HOME", (folder.path() + QStringLiteral("/config")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", (folder.path() + QStringLiteral("/xdg")).toUtf8());
        qunsetenv("XDG_CURRENT_DESKTOP");
        users = folder.path() + QStringLiteral("/config/autostart");
        system = folder.path() + QStringLiteral("/xdg/autostart");
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/autostart"), users);
    }

    void cleanup()
    {
        QDir(users).removeRecursively();
        QDir(system).removeRecursively();
    }

    void the_users_own_entry_takes_the_mark()
    {
        write(own("lutris.desktop"), entry("Lutris"));
        QVERIFY(!hidden_in_list("lutris.desktop"));
        QCOMPARE(set_startup_entry_hide("lutris.desktop", true), QString());
        QVERIFY(read(own("lutris.desktop")).contains(QStringLiteral("%1=true").arg(MARK)));
        QVERIFY(hidden_in_list("lutris.desktop"));

        QCOMPARE(set_startup_entry_hide("lutris.desktop", false), QString());
        QVERIFY(!read(own("lutris.desktop")).contains(MARK));
        QVERIFY(!hidden_in_list("lutris.desktop"));
        QVERIFY(read(own("lutris.desktop")).contains(QStringLiteral("Exec=true")));
    }

    void a_systems_entry_gets_a_marked_copy()
    {
        write(theirs("applet.desktop"), entry("Applet"));
        QCOMPARE(set_startup_entry_hide("applet.desktop", true), QString());
        QVERIFY(read(own("applet.desktop")).contains(QStringLiteral("%1=true").arg(MARK)));
        QVERIFY(read(own("applet.desktop")).contains(QStringLiteral("Exec=true")));
        QVERIFY(hidden_in_list("applet.desktop"));
        QVERIFY(!read(theirs("applet.desktop")).contains(MARK));
    }

    void unmarking_a_systems_entry_gives_it_back()
    {
        write(theirs("applet.desktop"), entry("Applet"));
        QCOMPARE(set_startup_entry_hide("applet.desktop", true), QString());
        QCOMPARE(set_startup_entry_hide("applet.desktop", false), QString());
        QVERIFY(!QFile::exists(own("applet.desktop")));
        QVERIFY(!hidden_in_list("applet.desktop"));
    }

    void unmarking_what_is_not_marked_writes_nothing()
    {
        write(theirs("applet.desktop"), entry("Applet"));
        QCOMPARE(set_startup_entry_hide("applet.desktop", false), QString());
        QVERIFY(!QFile::exists(own("applet.desktop")));
    }

    void the_mark_lasts_through_switching_off_and_on()
    {
        write(theirs("applet.desktop"), entry("Applet"));
        QCOMPARE(set_startup_entry_hide("applet.desktop", true), QString());
        QCOMPARE(switch_startup_entry("applet.desktop", false), QString());
        QVERIFY(hidden_in_list("applet.desktop"));
        QCOMPARE(switch_startup_entry("applet.desktop", true), QString());
        QVERIFY(hidden_in_list("applet.desktop"));
        QVERIFY(read(own("applet.desktop")).contains(QStringLiteral("Exec=true")));

        write(own("lutris.desktop"), entry("Lutris", QStringLiteral("%1=true\n").arg(MARK)));
        QCOMPARE(switch_startup_entry("lutris.desktop", false), QString());
        QCOMPARE(switch_startup_entry("lutris.desktop", true), QString());
        QVERIFY(hidden_in_list("lutris.desktop"));
    }

    void switching_on_an_unmarked_copy_still_gives_the_systems_back()
    {
        write(theirs("applet.desktop"), entry("Applet"));
        QCOMPARE(switch_startup_entry("applet.desktop", false), QString());
        QVERIFY(QFile::exists(own("applet.desktop")));
        QCOMPARE(switch_startup_entry("applet.desktop", true), QString());
        QVERIFY(!QFile::exists(own("applet.desktop")));
    }

    void a_mark_on_a_systems_entry_that_the_system_switched_off_keeps_it_on()
    {
        write(theirs("quiet.desktop"), entry("Quiet", QStringLiteral("Hidden=true\n")));
        QCOMPARE(switch_startup_entry("quiet.desktop", true), QString());
        QCOMPARE(set_startup_entry_hide("quiet.desktop", true), QString());
        const QList<StartupEntry> entries = startup_entries();
        const StartupEntry* found = find(entries, "quiet.desktop");
        QVERIFY(found != nullptr);
        QVERIFY(found->on);
        QVERIFY(found->hide);
        QCOMPARE(set_startup_entry_hide("quiet.desktop", false), QString());
        const QList<StartupEntry> after = startup_entries();
        QVERIFY(find(after, "quiet.desktop") != nullptr);
        QVERIFY(find(after, "quiet.desktop")->on);
        QVERIFY(!find(after, "quiet.desktop")->hide);
    }

    void unmarking_a_switched_off_entry_leaves_it_off()
    {
        write(theirs("applet.desktop"), entry("Applet"));
        QCOMPARE(set_startup_entry_hide("applet.desktop", true), QString());
        QCOMPARE(switch_startup_entry("applet.desktop", false), QString());
        QCOMPARE(set_startup_entry_hide("applet.desktop", false), QString());
        const QList<StartupEntry> entries = startup_entries();
        QVERIFY(find(entries, "applet.desktop") != nullptr);
        QVERIFY(!find(entries, "applet.desktop")->on);
        QVERIFY(!find(entries, "applet.desktop")->hide);
    }

    void a_hand_written_one_counts_as_a_mark()
    {
        write(own("lutris.desktop"), entry("Lutris", QStringLiteral("%1=1\n").arg(MARK)));
        QVERIFY(hidden_in_list("lutris.desktop"));
    }

    void a_gone_entry_is_said()
    {
        QVERIFY(!set_startup_entry_hide("gone.desktop", true).isEmpty());
    }
};

QTEST_MAIN(TestStartupEntries)
#include "test-startup-entries.moc"
