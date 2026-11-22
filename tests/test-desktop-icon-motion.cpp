#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtTest>

#include <memory>

// moves take 800 ms here so a sample early in one is well clear of a straight line
class TestDesktopIconMotion : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    QQuickItem* icon() { return probe->findChild<QQuickItem*>(QStringLiteral("icon")); }

    bool make(int duration)
    {
        QQmlComponent component(&engine);
        component.setData(QByteArray("import QtQuick\n"
                                     "import \"file://" SHELL_DIR "\"\n"
                                     "Item {\n"
                                     "  property point place: Qt.point(100, 50)\n"
                                     "  DesktopIconMotion {\n"
                                     "    objectName: \"icon\"\n"
                                     "    place: parent.place\n"
                                     "    duration: ")
                              + QByteArray::number(duration) + "\n  }\n}\n",
            QUrl());
        probe.reset(component.create());
        if (probe == nullptr)
        {
            QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__);
            return false;
        }
        return icon() != nullptr;
    }

    void moveTo(double x, double y) { probe->setProperty("place", QPointF(x, y)); }

    void jumpTo(double x, double y)
    {
        QVERIFY(QMetaObject::invokeMethod(icon(), "jump", Q_ARG(QVariant, QVariant(QPointF(x, y)))));
    }

    QPointF at() { return QPointF(icon()->x(), icon()->y()); }

  private slots:
    void cleanup() { probe.reset(); }

    void anIconStartsAtItsPlace()
    {
        QVERIFY(make(800));
        QCOMPARE(at(), QPointF(100, 50));
    }

    // eased at both ends the middle half of the way takes about 0.31 of the
    // time, on a straight line it takes half
    void aNewPlaceIsReachedByAGlideEasedAtBothEnds()
    {
        QVERIFY(make(1600));
        QElapsedTimer since;
        since.start();
        moveTo(500, 450);
        qint64 started = -1, quarter = -1, threeQuarters = -1, arrived = -1;
        while (arrived < 0 && since.elapsed() < 3000)
        {
            QTest::qWait(5);
            const double share = (icon()->x() - 100) / 400;
            QCOMPARE((icon()->y() - 50) / 400, share);
            const qint64 now = since.elapsed();
            if (started < 0 && share > 0)
                started = now;
            if (quarter < 0 && share >= 0.25)
                quarter = now;
            if (threeQuarters < 0 && share >= 0.75)
                threeQuarters = now;
            if (share >= 1)
                arrived = now;
        }
        QVERIFY2(arrived > 0, "never arrived");
        const double middle = double(threeQuarters - quarter) / double(arrived - started);
        qInfo("[motion] middle=%f started=%lld quarter=%lld three=%lld arrived=%lld", middle, started, quarter, threeQuarters, arrived);
        QVERIFY2(middle > 0.28 && middle < 0.44,
            qPrintable(QStringLiteral("middle=%1 started=%2 quarter=%3 three_quarters=%4 arrived=%5")
                           .arg(middle)
                           .arg(started)
                           .arg(quarter)
                           .arg(threeQuarters)
                           .arg(arrived)));
        QCOMPARE(at(), QPointF(500, 450));
    }

    void aNewPlaceDuringAGlideTurnsItFromWhereItIs()
    {
        QVERIFY(make(800));
        moveTo(500, 450);
        QTest::qWait(400);
        const double turnedAt = icon()->x();
        QVERIFY2(turnedAt > 150, qPrintable(QStringLiteral("x=%1").arg(turnedAt)));
        moveTo(100, 50);
        QTest::qWait(30);
        QVERIFY2(icon()->x() > turnedAt - 40, qPrintable(QStringLiteral("turned_at=%1 x=%2").arg(turnedAt).arg(icon()->x())));
        QTRY_COMPARE_WITH_TIMEOUT(at(), QPointF(100, 50), 1500);
    }

    void aJumpLandsAtOnceAndTheNextPlaceGlidesFromThere()
    {
        QVERIFY(make(800));
        jumpTo(300, 300);
        QCOMPARE(at(), QPointF(300, 300));
        moveTo(340, 300);
        QTest::qWait(200);
        QVERIFY2(icon()->x() > 300 && icon()->x() < 340, qPrintable(QStringLiteral("x=%1").arg(icon()->x())));
        QTRY_COMPARE_WITH_TIMEOUT(at(), QPointF(340, 300), 1500);
    }

    void aJumpStopsAGlideUnderWay()
    {
        QVERIFY(make(800));
        moveTo(500, 450);
        QTest::qWait(100);
        jumpTo(20, 30);
        QTest::qWait(200);
        QCOMPARE(at(), QPointF(20, 30));
    }

    void withNoDurationAnIconJumpsToEachPlace()
    {
        QVERIFY(make(0));
        moveTo(500, 450);
        QCOMPARE(at(), QPointF(500, 450));
    }
};

QTEST_MAIN(TestDesktopIconMotion)
#include "test-desktop-icon-motion.moc"
