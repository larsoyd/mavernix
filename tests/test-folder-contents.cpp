#include "folder-contents.hpp"

#include <QDir>
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
};

QTEST_GUILESS_MAIN(TestFolderContents)
#include "test-folder-contents.moc"
