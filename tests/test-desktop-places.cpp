#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

// slots 96 apart, first column left edge 916, first row top 30
class TestDesktopPlaces : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    // an icon not put by hand
    static QVariant none() { return QVariant::fromValue(nullptr); }

    static QVariant at(int x, int y) { return QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}}; }

    QVariantList call(const char* function, const QVariantList& arguments)
    {
        QVariant result;
        if (!QMetaObject::invokeMethod(
                probe.get(), function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant(arguments))))
        {
            QTest::qFail("probe not called", __FILE__, __LINE__);
        }
        return result.toList();
    }

    QVariantList places(const QVariantList& put, bool snap) { return call("places", {QVariant(put), snap}); }

    static QString text(const QVariantList& points)
    {
        QStringList words;
        for (const QVariant& point : points)
        {
            const QVariantMap map = point.toMap();
            words << QStringLiteral("%1,%2")
                         .arg(map.value(QStringLiteral("x")).toInt())
                         .arg(map.value(QStringLiteral("y")).toInt());
        }
        return words.join(QLatin1Char(' '));
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/DesktopPlaces.js\" as DesktopPlaces\n"
                          "QtObject {\n"
                          "  readonly property var grid: ({ width: 1000, top: 30, rows: 5, edge: 60,\n"
                          "                                 iconSize: 48, columnPitch: 96, rowPitch: 96 })\n"
                          "  function places(a) { return DesktopPlaces.places(grid, a[0], a[1]) }\n"
                          "  function onGrid(a) { return DesktopPlaces.onGrid(grid, a[0], a[1]) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void withoutTheGridAnIconStaysWhereItWasPut()
    {
        QCOMPARE(text(places({at(500, 333), none()}, false)), QStringLiteral("500,333 916,30"));
    }

    void withoutTheGridOthersFlowAroundTheSlotsAPutIconCovers()
    {
        QCOMPARE(text(places({at(868, 30), none(), none()}, false)), QStringLiteral("868,30 916,126 916,222"));
    }

    void theGridPutsAnIconOnTheNearestSlot()
    {
        QCOMPARE(text(places({at(734, 298)}, true)), QStringLiteral("724,318"));
    }

    void theGridGivesTheLaterOfTwoIconsOnOneSlotTheNextOne()
    {
        QCOMPARE(text(places({at(734, 298), at(720, 330)}, true)), QStringLiteral("724,318 724,414"));
    }

    void theGridLetsOthersFlowIntoTheSlotsBesidePutIcons()
    {
        QCOMPARE(text(places({at(880, 30), none(), none()}, true)), QStringLiteral("916,30 916,126 916,222"));
    }

    void theGridKeepsAnIconBelowTheLastRowInTheLastRow()
    {
        QCOMPARE(text(places({at(916, 2000)}, true)), QStringLiteral("916,414"));
    }

    void droppedIconsTakeFreeSlotsAndLeaveTheOthersWhereTheyAre()
    {
        QCOMPARE(text(call("onGrid", {QVariantList{at(916, 30)}, QVariantList{at(910, 40), at(820, 30)}})),
            QStringLiteral("916,126 820,30"));
    }

    void cleaningUpWithNothingKeptSnapsEveryIcon()
    {
        QCOMPARE(text(call("onGrid", {QVariantList{}, QVariantList{at(500, 333), at(510, 340)}})),
            QStringLiteral("532,318 532,414"));
    }
};

QTEST_MAIN(TestDesktopPlaces)
#include "test-desktop-places.moc"
