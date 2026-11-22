#include "folder-contents.hpp"

#include <QDir>
#include <QScopeGuard>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestFolderContents : public QObject
{
    Q_OBJECT

  private:
    static QStringList names(const FolderContents& contents)
    {
        QStringList found;
        for (const QVariant& entry : contents.entries()) {
            found.append(entry.toMap().value(QStringLiteral("name")).toString());
        }
        return found;
    }

    static void write(const QString& path, const QByteArray& text)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

  private slots:
    void aFileAddedLaterShowsUp()
    {
        QTemporaryDir home;
        FolderContents contents;
        contents.setPath(home.path());
        QCOMPARE(names(contents), QStringList());

        QSignalSpy changed(&contents, &FolderContents::entriesChanged);
        write(home.filePath(QStringLiteral("letter.txt")), "hello");
        QTRY_COMPARE_WITH_TIMEOUT(names(contents), QStringList{QStringLiteral("letter.txt")}, 3000);
        QVERIFY(changed.count() >= 1);
    }

    void aFolderMadeLaterIsNoticedWithItsFiles()
    {
        QTemporaryDir home;
        const QString desktop = home.filePath(QStringLiteral("deep/Desktop"));
        FolderContents contents;
        contents.setPath(desktop);
        QCOMPARE(names(contents), QStringList());

        QVERIFY(QDir().mkpath(desktop));
        write(desktop + QStringLiteral("/Paint Box.desktop"),
            "[Desktop Entry]\nType=Application\nName=Paint Box\nIcon=paint-box\nExec=true\n");
        QVERIFY(QFile::setPermissions(desktop + QStringLiteral("/Paint Box.desktop"),
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QTRY_COMPARE_WITH_TIMEOUT(names(contents), QStringList{QStringLiteral("Paint Box")}, 3000);
        QVERIFY(contents.entries().first().toMap().value(QStringLiteral("isLauncher")).toBool());

        write(desktop + QStringLiteral("/later.txt"), "more");
        QTRY_COMPARE_WITH_TIMEOUT(names(contents).size(), 2, 3000);
    }

    void aFolderRemovedAndMadeAgainIsFollowed()
    {
        QTemporaryDir home;
        const QString desktop = home.filePath(QStringLiteral("Desktop"));
        QVERIFY(QDir().mkpath(desktop));
        write(desktop + QStringLiteral("/first.txt"), "one");
        FolderContents contents;
        contents.setPath(desktop);
        QCOMPARE(names(contents), QStringList{QStringLiteral("first.txt")});

        QVERIFY(QDir(desktop).removeRecursively());
        QTRY_COMPARE_WITH_TIMEOUT(names(contents), QStringList(), 3000);
        QVERIFY(QDir().mkpath(desktop));
        write(desktop + QStringLiteral("/second.txt"), "two");
        QTRY_COMPARE_WITH_TIMEOUT(names(contents), QStringList{QStringLiteral("second.txt")}, 3000);
    }

    void aLinkToAUsualFolderShowsThatFoldersIcon()
    {
        QTemporaryDir home;
        const QByteArray oldHome = qgetenv("HOME");
        const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
        qputenv("HOME", home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral(".config")).toUtf8());
        auto restore = qScopeGuard([&] {
            qputenv("HOME", oldHome);
            qputenv("XDG_CONFIG_HOME", oldConfig);
        });

        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral(".config"))));
        write(home.filePath(QStringLiteral(".config/user-dirs.dirs")),
            "XDG_DOWNLOAD_DIR=\"$HOME/Fetched\"\nXDG_MUSIC_DIR=\"$HOME/Tunes\"\nXDG_PICTURES_DIR=\"$HOME/\"\n");
        for (const char* name : {"Fetched", "Tunes", "Desktop/Plain", "Desktop/Tunes copy"}) {
            QVERIFY(QDir().mkpath(home.filePath(QString::fromLatin1(name))));
        }
        const QString desktop = home.filePath(QStringLiteral("Desktop"));
        QVERIFY(QFile::link(home.filePath(QStringLiteral("Fetched")), desktop + QStringLiteral("/Downloads")));
        QVERIFY(QFile::link(home.filePath(QStringLiteral("Tunes")), desktop + QStringLiteral("/Music")));
        QVERIFY(QFile::link(home.path(), desktop + QStringLiteral("/Home")));

        FolderContents contents;
        contents.setPath(desktop);
        QMap<QString, QString> icons;
        for (const QVariant& entry : contents.entries()) {
            const QVariantMap map = entry.toMap();
            icons.insert(map.value(QStringLiteral("name")).toString(), map.value(QStringLiteral("icon")).toString());
            QCOMPARE(map.value(QStringLiteral("genericIcon")).toString(), QStringLiteral("folder"));
            QVERIFY(map.value(QStringLiteral("isFolder")).toBool());
        }
        QCOMPARE(icons.value(QStringLiteral("Downloads")), QStringLiteral("folder-download"));
        QCOMPARE(icons.value(QStringLiteral("Music")), QStringLiteral("folder-music"));
        QCOMPARE(icons.value(QStringLiteral("Home")), QStringLiteral("user-home"));
        QCOMPARE(icons.value(QStringLiteral("Plain")), QStringLiteral("folder"));
        QCOMPARE(icons.value(QStringLiteral("Tunes copy")), QStringLiteral("folder"));
    }

    void aFolderShowsTheIconItsDirectoryFileNames()
    {
        QTemporaryDir home;
        for (const char* name : {"code", "Desktop/Sketches", "Desktop/Other group"}) {
            QVERIFY(QDir().mkpath(home.filePath(QString::fromLatin1(name))));
        }
        write(home.filePath(QStringLiteral("code/.directory")), "[Desktop Entry]\nIcon=folder-development\n");
        write(home.filePath(QStringLiteral("Desktop/Sketches/.directory")),
            "[Settings]\nIcon=wrong\n\n[Desktop Entry]\nIcon=folder-pictures\n");
        write(home.filePath(QStringLiteral("Desktop/Other group/.directory")), "[Dolphin]\nIcon=wrong\n");
        const QString desktop = home.filePath(QStringLiteral("Desktop"));
        QVERIFY(QFile::link(home.filePath(QStringLiteral("code")), desktop + QStringLiteral("/git")));

        FolderContents contents;
        contents.setPath(desktop);
        QMap<QString, QString> icons;
        for (const QVariant& entry : contents.entries()) {
            const QVariantMap map = entry.toMap();
            icons.insert(map.value(QStringLiteral("name")).toString(), map.value(QStringLiteral("icon")).toString());
        }
        QCOMPARE(icons.value(QStringLiteral("git")), QStringLiteral("folder-development"));
        QCOMPARE(icons.value(QStringLiteral("Sketches")), QStringLiteral("folder-pictures"));
        QCOMPARE(icons.value(QStringLiteral("Other group")), QStringLiteral("folder"));
    }
};

QTEST_GUILESS_MAIN(TestFolderContents)
#include "test-folder-contents.moc"
