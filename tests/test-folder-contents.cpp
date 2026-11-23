#include "folder-contents.hpp"

#include <QDir>
#include <QScopeGuard>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <QImage>

#include <sys/xattr.h>

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

    void linksSayTheyAreLinksAndNothingElseDoes()
    {
        QTemporaryDir place;
        const QString desktop = place.filePath(QStringLiteral("Desktop"));
        QVERIFY(QDir().mkpath(desktop + QStringLiteral("/Folder")));
        QVERIFY(QDir().mkpath(place.filePath(QStringLiteral("Away"))));
        write(desktop + QStringLiteral("/Note.txt"), "x");
        write(place.filePath(QStringLiteral("far.txt")), "x");
        write(place.filePath(QStringLiteral("tool.desktop")), "[Desktop Entry]\nType=Application\nName=Tool\nExec=true\n");
        QFile::setPermissions(place.filePath(QStringLiteral("tool.desktop")),
            QFile::permissions(place.filePath(QStringLiteral("tool.desktop"))) | QFile::ExeOwner);
        QVERIFY(QFile::link(place.filePath(QStringLiteral("Away")), desktop + QStringLiteral("/Away link")));
        QVERIFY(QFile::link(place.filePath(QStringLiteral("far.txt")), desktop + QStringLiteral("/far link.txt")));
        QVERIFY(QFile::link(place.filePath(QStringLiteral("tool.desktop")), desktop + QStringLiteral("/tool.desktop")));

        FolderContents contents;
        contents.setPath(desktop);
        QMap<QString, bool> links;
        for (const QVariant& entry : contents.entries()) {
            const QVariantMap map = entry.toMap();
            QVERIFY2(map.contains(QStringLiteral("isLink")), qPrintable(map.value(QStringLiteral("name")).toString()));
            links.insert(map.value(QStringLiteral("path")).toString().section(QLatin1Char('/'), -1),
                map.value(QStringLiteral("isLink")).toBool());
        }
        QCOMPARE(links.size(), 5);
        QCOMPARE(links.value(QStringLiteral("Away link")), true);
        QCOMPARE(links.value(QStringLiteral("far link.txt")), true);
        QCOMPARE(links.value(QStringLiteral("tool.desktop")), true);
        QCOMPARE(links.value(QStringLiteral("Folder")), false);
        QCOMPARE(links.value(QStringLiteral("Note.txt")), false);
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

    static void sized(const QString& path, int bytes) { write(path, QByteArray(bytes, 'x')); }

    static void tagged(const QString& path, const char* tags)
    {
        QVERIFY2(setxattr(QFile::encodeName(path).constData(), "user.xdg.tags", tags, strlen(tags), 0) == 0,
            qPrintable(path));
    }

    void theLargestFileComesFirstAndFoldersLast()
    {
        QTemporaryDir home;
        sized(home.filePath(QStringLiteral("small")), 10);
        sized(home.filePath(QStringLiteral("large")), 100);
        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral("box"))));
        FolderContents contents;
        contents.setOrder(QStringLiteral("size"));
        contents.setPath(home.path());
        QCOMPARE(names(contents), (QStringList{QStringLiteral("large"), QStringLiteral("small"), QStringLiteral("box")}));
    }

    void theLastOpenedFileComesFirst()
    {
        QTemporaryDir home;
        for (const char* name : {"older", "newer"}) {
            sized(home.filePath(QString::fromLatin1(name)), 1);
        }
        const auto openedAt = [&](const char* name, int days) {
            QFile file(home.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-days), QFileDevice::FileAccessTime));
        };
        openedAt("older", 9);
        openedAt("newer", 1);
        FolderContents contents;
        contents.setOrder(QStringLiteral("last_opened"));
        contents.setPath(home.path());
        QCOMPARE(names(contents), (QStringList{QStringLiteral("newer"), QStringLiteral("older")}));
    }

    void taggedFilesComeFirstByTagThenTheRestByName()
    {
        QTemporaryDir home;
        for (const char* name : {"apple", "berry", "cherry", "date"}) {
            sized(home.filePath(QString::fromLatin1(name)), 1);
        }
        tagged(home.filePath(QStringLiteral("date")), "Blue");
        tagged(home.filePath(QStringLiteral("cherry")), "Red");
        tagged(home.filePath(QStringLiteral("berry")), "Blue");
        FolderContents contents;
        contents.setOrder(QStringLiteral("tags"));
        contents.setPath(home.path());
        QCOMPARE(names(contents), (QStringList{QStringLiteral("berry"), QStringLiteral("date"), QStringLiteral("cherry"),
                                               QStringLiteral("apple")}));
    }

    // finder sorts kinds by the words it shows for them, folder then plain text then png
    void kindsSortByTheirDescription()
    {
        QTemporaryDir home;
        sized(home.filePath(QStringLiteral("a.txt")), 1);
        sized(home.filePath(QStringLiteral("b.png")), 1);
        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral("z"))));
        FolderContents contents;
        contents.setOrder(QStringLiteral("kind"));
        contents.setPath(home.path());
        QCOMPARE(names(contents), (QStringList{QStringLiteral("z"), QStringLiteral("a.txt"), QStringLiteral("b.png")}));
    }

    void anotherOrderCanBeAskedForOnceWithoutChangingTheEntries()
    {
        QTemporaryDir home;
        sized(home.filePath(QStringLiteral("alpha")), 10);
        sized(home.filePath(QStringLiteral("zeta")), 100);
        FolderContents contents;
        contents.setPath(home.path());
        QCOMPARE(contents.pathsInOrder(QStringLiteral("size")),
            (QStringList{home.filePath(QStringLiteral("zeta")), home.filePath(QStringLiteral("alpha"))}));
        QCOMPARE(names(contents), (QStringList{QStringLiteral("alpha"), QStringLiteral("zeta")}));
    }

    static QVariantMap infoOf(const FolderContents& contents, const QString& path)
    {
        return contents.infos().value(path).toMap();
    }

    // a second of silence at 8 kHz, 8 bit mono
    static QByteArray wave()
    {
        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + 8000);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(8000) << quint16(1) << quint16(8);
        out.writeRawData("data", 4);
        out << quint32(8000);
        data.append(QByteArray(8000, char(128)));
        return data;
    }

    void itemInfoCountsAFoldersShownItemsAndMeasuresPicturesAndSounds()
    {
        QTemporaryDir home;
        const QString box = home.filePath(QStringLiteral("box"));
        QVERIFY(QDir().mkpath(box));
        for (const char* name : {"one", "two", ".hidden"}) {
            sized(box + QLatin1Char('/') + QString::fromLatin1(name), 1);
        }
        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral("empty"))));
        QImage picture(603, 269, QImage::Format_RGB32);
        picture.fill(Qt::red);
        QVERIFY(picture.save(home.filePath(QStringLiteral("example2.png"))));
        write(home.filePath(QStringLiteral("tone.wav")), wave());
        sized(home.filePath(QStringLiteral("note.txt")), 5);

        FolderContents contents;
        contents.setItemInfo(true);
        contents.setPath(home.path());
        QTRY_COMPARE_WITH_TIMEOUT(contents.infos().size(), 5, 5000);
        QCOMPARE(infoOf(contents, box).value(QStringLiteral("items")).toInt(), 2);
        QCOMPARE(infoOf(contents, home.filePath(QStringLiteral("empty"))).value(QStringLiteral("items")).toInt(), 0);
        const QVariantMap picturesInfo = infoOf(contents, home.filePath(QStringLiteral("example2.png")));
        QCOMPARE(picturesInfo.value(QStringLiteral("width")).toInt(), 603);
        QCOMPARE(picturesInfo.value(QStringLiteral("height")).toInt(), 269);
        QCOMPARE(infoOf(contents, home.filePath(QStringLiteral("tone.wav"))).value(QStringLiteral("seconds")).toInt(), 1);
        QVERIFY(infoOf(contents, home.filePath(QStringLiteral("note.txt"))).isEmpty());
    }

    // a stand-in for the sound prober that takes its time, first on the path
    static QString slowProber(const QTemporaryDir& tools, const QByteArray& script)
    {
        const QString prober = tools.filePath(QStringLiteral("ffprobe"));
        write(prober, "#!/bin/sh\n" + script + "\n");
        QFile::setPermissions(prober, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(tools.path()) + ':' + path);
        return QString::fromLocal8Bit(path);
    }

    void aFolderLeftWhileItWasMeasuredDoesNotOverwriteTheNextOne()
    {
        QTemporaryDir tools;
        QTemporaryDir first;
        QTemporaryDir second;
        const QString path = slowProber(tools, "sleep 1\necho 7");
        const auto restore = qScopeGuard([&] { qputenv("PATH", path.toLocal8Bit()); });
        sized(first.filePath(QStringLiteral("slow.wav")), 10);
        QVERIFY(QDir().mkpath(second.filePath(QStringLiteral("box"))));
        FolderContents contents;
        contents.setItemInfo(true);
        contents.setPath(first.path());
        contents.setPath(second.path());
        QTRY_COMPARE_WITH_TIMEOUT(contents.infos().keys(), QStringList{second.filePath(QStringLiteral("box"))}, 2000);
        QTest::qWait(2500);
        QCOMPARE(contents.infos().keys(), QStringList{second.filePath(QStringLiteral("box"))});
    }

    void aSoundTheProberCannotReadInTimeShowsNothing()
    {
        QTemporaryDir tools;
        QTemporaryDir home;
        const QString path = slowProber(tools, "exec sleep 30");
        const auto restore = qScopeGuard([&] { qputenv("PATH", path.toLocal8Bit()); });
        sized(home.filePath(QStringLiteral("slow.wav")), 10);
        FolderContents contents;
        contents.setItemInfo(true);
        contents.setPath(home.path());
        QTRY_COMPARE_WITH_TIMEOUT(contents.infos().size(), 1, 6000);
        QVERIFY(infoOf(contents, home.filePath(QStringLiteral("slow.wav"))).isEmpty());
    }

    void withoutItemInfoNothingIsMeasured()
    {
        QTemporaryDir home;
        QVERIFY(QDir().mkpath(home.filePath(QStringLiteral("box"))));
        FolderContents contents;
        contents.setPath(home.path());
        QTest::qWait(300);
        QCOMPARE(contents.infos().size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestFolderContents)
#include "test-folder-contents.moc"
