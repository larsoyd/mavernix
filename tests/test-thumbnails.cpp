#include "thumbnails.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

// stand-ins for mavernix-thumbnail on the path, and a private cache
class TestThumbnails : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir root;
    QByteArray oldPath;

    QString cache() const { return root.filePath(QStringLiteral("cache/thumbnails")); }
    QString ran() const { return root.filePath(QStringLiteral("ran")); }

    static QString md5(const QString& file)
    {
        return QString::fromLatin1(QCryptographicHash::hash(QUrl::fromLocalFile(file).toEncoded(),
                                                            QCryptographicHash::Md5).toHex());
    }

    static void writeThumb(const QString& out, const QString& file, qint64 mtime, QColor colour)
    {
        QImage picture(64, 48, QImage::Format_ARGB32);
        picture.fill(colour);
        picture.setText(QStringLiteral("Thumb::URI"), QString::fromUtf8(QUrl::fromLocalFile(file).toEncoded()));
        picture.setText(QStringLiteral("Thumb::MTime"), QString::number(mtime));
        QVERIFY(QDir().mkpath(QFileInfo(out).path()));
        QVERIFY(picture.save(out, "PNG"));
    }

    void helper(const QByteArray& script)
    {
        const QString path = root.filePath(QStringLiteral("bin/mavernix-thumbnail"));
        QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("bin"))));
        QFile helper(path);
        QVERIFY(helper.open(QIODevice::WriteOnly));
        helper.write("#!/bin/sh\necho x >> '" + ran().toUtf8() + "'\n" + script + "\n");
        helper.close();
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

    int runs() const
    {
        QFile file(ran());
        return file.open(QIODevice::ReadOnly) ? file.readAll().count('\n') : 0;
    }

    QString document()
    {
        const QString file = root.filePath(QStringLiteral("files/note.pdf"));
        QDir().mkpath(QFileInfo(file).path());
        QFile note(file);
        note.open(QIODevice::WriteOnly);
        note.write("%PDF");
        return file;
    }

    static qint64 mtimeOf(const QString& file) { return QFileInfo(file).lastModified().toSecsSinceEpoch(); }

  private slots:
    void init()
    {
        QDir(root.path()).removeRecursively();
        QDir().mkpath(root.path());
        qputenv("XDG_CACHE_HOME", QFile::encodeName(root.filePath(QStringLiteral("cache"))));
        oldPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(root.filePath(QStringLiteral("bin"))) + ':' + oldPath);
    }
    void cleanup() { qputenv("PATH", oldPath); }

    void aCurrentThumbnailIsReadWithoutMakingOne()
    {
        const QString file = document();
        helper("exit 1");
        writeThumb(cache() + QStringLiteral("/large/") + md5(file) + QStringLiteral(".png"), file, mtimeOf(file), Qt::blue);
        QString error;
        const QImage found = thumbnail_for(file, &error);
        QVERIFY2(!found.isNull(), qPrintable(error));
        QCOMPARE(found.pixelColor(1, 1), QColor(Qt::blue));
        QCOMPARE(runs(), 0);
    }

    void aThumbnailOfAnOlderVersionIsMadeAgain()
    {
        const QString file = document();
        const QString target = cache() + QStringLiteral("/large/") + md5(file) + QStringLiteral(".png");
        writeThumb(target, file, mtimeOf(file) - 100, Qt::blue);
        const QString fresh = root.filePath(QStringLiteral("fresh.png"));
        writeThumb(fresh, file, mtimeOf(file), Qt::green);
        helper("cp '" + fresh.toUtf8() + "' \"$2\"");
        QString error;
        const QImage found = thumbnail_for(file, &error);
        QVERIFY2(!found.isNull(), qPrintable(error));
        QCOMPARE(found.pixelColor(1, 1), QColor(Qt::green));
        QCOMPARE(runs(), 1);
    }

    void aFileWithoutAPreviewIsNotAskedAboutTwice()
    {
        const QString file = document();
        helper("exit 1");
        QString error;
        QVERIFY(thumbnail_for(file, &error).isNull());
        QVERIFY(thumbnail_for(file, &error).isNull());
        QCOMPARE(runs(), 1);
        QVERIFY(QFile::exists(cache() + QStringLiteral("/fail/mavernix/") + md5(file) + QStringLiteral(".png")));
    }

    void aChangedFileIsAskedAboutAgainAfterAFailure()
    {
        const QString file = document();
        helper("exit 1");
        QString error;
        thumbnail_for(file, &error);
        QFile touched(file);
        QVERIFY(touched.open(QIODevice::ReadWrite));
        QVERIFY(touched.setFileTime(QDateTime::currentDateTime().addDays(1), QFileDevice::FileModificationTime));
        touched.close();
        thumbnail_for(file, &error);
        QCOMPARE(runs(), 2);
    }

    void aMissingHelperIsSaidSo()
    {
        const QString file = document();
        qputenv("PATH", QFile::encodeName(root.filePath(QStringLiteral("nowhere"))));
        QString error;
        QVERIFY(thumbnail_for(file, &error, 500).isNull());
        QVERIFY2(error.contains(QStringLiteral("cannot start")), qPrintable(error));
    }

    void aHelperThatHangsIsStopped()
    {
        const QString file = document();
        helper("exec sleep 30");
        QElapsedTimer clock;
        clock.start();
        QString error;
        QVERIFY(thumbnail_for(file, &error, 500).isNull());
        QVERIFY2(clock.elapsed() < 5000, qPrintable(QString::number(clock.elapsed())));
        QVERIFY2(error.contains(QStringLiteral("timeout")), qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(TestThumbnails)
#include "test-thumbnails.moc"
