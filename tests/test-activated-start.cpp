#include "activated-start.hpp"

#include <QFile>
#include <QPromise>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

class TestActivatedStart : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;

    QStringList tokenWriter(const QString& name) const
    {
        return {QStringLiteral("sh"), QStringLiteral("-c"),
                QStringLiteral("printf %s \"${XDG_ACTIVATION_TOKEN-none}\" > %1.part && mv %1.part %1")
                    .arg(folder.filePath(name))};
    }

    static QString seen(const QString& path)
    {
        QFile file(path);
        if (!QTest::qWaitFor([&] { return file.exists(); }, 5000)) {
            return QStringLiteral("never written");
        }
        if (!file.open(QIODevice::ReadOnly)) {
            return QStringLiteral("unreadable");
        }
        return QString::fromUtf8(file.readAll());
    }

    QStringList tokenAppender(const QString& name) const
    {
        return {QStringLiteral("sh"), QStringLiteral("-c"),
                QStringLiteral("printf '%s\\n' \"${XDG_ACTIVATION_TOKEN-none}\" >> %1").arg(folder.filePath(name))};
    }

    static QStringList lines(const QString& path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

  private slots:
    void init()
    {
        qputenv("XDG_ACTIVATION_TOKEN", "spent-at-login");
    }

    void cleanup()
    {
        ActivatedStart::tokenSource = [](QWindow*) { return QtFuture::makeReadyValueFuture(QString()); };
    }

    void aTokenFromTheCompositorReachesTheProgram()
    {
        ActivatedStart::tokenSource = [](QWindow*) { return QtFuture::makeReadyValueFuture(QStringLiteral("tok-1")); };
        QQuickWindow window;
        ActivatedStart starter;
        starter.run(tokenWriter(QStringLiteral("asked")), window.contentItem());
        QCOMPARE(seen(folder.filePath(QStringLiteral("asked"))), QStringLiteral("tok-1"));
    }

    void aSilentCompositorDoesNotHoldUpTheOpen()
    {
        QPromise<QString> never;
        ActivatedStart::tokenSource = [&never](QWindow*) { return never.future(); };
        QQuickWindow window;
        ActivatedStart starter;
        QElapsedTimer clock;
        clock.start();
        starter.run(tokenWriter(QStringLiteral("silent")), window.contentItem());
        QCOMPARE(seen(folder.filePath(QStringLiteral("silent"))), QStringLiteral("none"));
        QVERIFY2(clock.elapsed() < 2000, qPrintable(QStringLiteral("took %1 ms").arg(clock.elapsed())));
    }

    void aTokenAfterTheWaitStartsNothingTwice()
    {
        auto late = std::make_shared<QPromise<QString>>();
        ActivatedStart::tokenSource = [late](QWindow*) { return late->future(); };
        QQuickWindow window;
        ActivatedStart starter;
        const QString path = folder.filePath(QStringLiteral("late"));
        starter.run(tokenAppender(QStringLiteral("late")), window.contentItem());
        QTRY_COMPARE_WITH_TIMEOUT(lines(path), QStringList{QStringLiteral("none")}, 3000);
        late->start();
        late->addResult(QStringLiteral("tok-late"));
        late->finish();
        QTest::qWait(600);
        QCOMPARE(lines(path), QStringList{QStringLiteral("none")});
    }

    void theTokenGivenReachesTheProgram()
    {
        QVERIFY(ActivatedStart::start(tokenWriter(QStringLiteral("given")), QStringLiteral("from-the-click")));
        QCOMPARE(seen(folder.filePath(QStringLiteral("given"))), QStringLiteral("from-the-click"));
    }

    void withoutATokenTheShellsOwnIsNotHandedOn()
    {
        QVERIFY(ActivatedStart::start(tokenWriter(QStringLiteral("none")), QString()));
        QCOMPARE(seen(folder.filePath(QStringLiteral("none"))), QStringLiteral("none"));
    }

    void withoutAWindowItStartsAtOnce()
    {
        ActivatedStart starter;
        QElapsedTimer clock;
        clock.start();
        starter.run(tokenWriter(QStringLiteral("nowindow")), nullptr);
        QCOMPARE(seen(folder.filePath(QStringLiteral("nowindow"))), QStringLiteral("none"));
        QVERIFY2(clock.elapsed() < 2000, qPrintable(QStringLiteral("took %1 ms").arg(clock.elapsed())));
    }

    void aWindowWithNoWaylandBehindItStillStarts()
    {
        QQuickWindow window;
        ActivatedStart starter;
        starter.run(tokenWriter(QStringLiteral("offscreen")), window.contentItem());
        QCOMPARE(seen(folder.filePath(QStringLiteral("offscreen"))), QStringLiteral("none"));
    }

    void anEmptyCommandStartsNothing()
    {
        QVERIFY(!ActivatedStart::start({}, QString()));
    }
};

QTEST_MAIN(TestActivatedStart)
#include "test-activated-start.moc"
