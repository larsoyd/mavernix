#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

// cells 96 square from the right edge at 988, pictures 48 wide in their middle, first row top 30
class TestDesktopPlaces : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    // an icon not put by hand
    static QVariant none() { return QVariant::fromValue(nullptr); }

    static QVariant at(int x, int y) { return QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}}; }

    // put off the grid on purpose, so the grid leaves it there
    static QVariant loose(int x, int y)
    {
        return QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("loose"), true}};
    }

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
                          "  readonly property var grid: ({ right: 988, top: 30, rows: 5, cellWidth: 96,\n"
                          "                                 cellHeight: 96, pictureX: 24, pictureY: 0, iconSize: 48 })\n"
                          "  function places(a) { return DesktopPlaces.places(grid, a[0], a[1]) }\n"
                          "  function onGrid(a) { return DesktopPlaces.onGrid(grid, a[0], a[1]) }\n"
                          "  function first(g) { return DesktopPlaces.slotPlace(g, 0) }\n"
                          "  function rescaled(a) { return DesktopPlaces.rescaled(a[0], a[1], a[2], a[3]) }\n"
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

    void theGridLeavesALooseIconWhereItWasPut()
    {
        QCOMPARE(text(places({loose(734, 298), at(720, 330)}, true)), QStringLiteral("734,298 724,414"));
    }

    void theGridLetsOthersFlowAroundALooseIcon()
    {
        QCOMPARE(text(places({loose(868, 30), none(), none()}, true)), QStringLiteral("868,30 916,126 916,222"));
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

    // the reference desktop at 1920 by 1080 under a 22 high menu bar, icon 64 then 32,
    // grid spacing 54 then 100, read as icon middles 1500,600 then 1500,410 then 1289,410
    static QVariantMap screenGrid(int iconSize, int cellWidth, int cellHeight)
    {
        return {{QStringLiteral("right"), 1916}, {QStringLiteral("top"), 26}, {QStringLiteral("rows"), 8},
                {QStringLiteral("cellWidth"), cellWidth}, {QStringLiteral("cellHeight"), cellHeight},
                {QStringLiteral("pictureX"), (cellWidth - iconSize) / 2}, {QStringLiteral("pictureY"), iconSize / 16},
                {QStringLiteral("iconSize"), iconSize}};
    }
    QVariantList rescaled(const QVariantMap& from, const QVariantMap& to, const QVariantList& put)
    {
        const QVariantMap screen{{QStringLiteral("width"), 1920}, {QStringLiteral("height"), 1080}};
        return call("rescaled", {QVariant(from), QVariant(to), QVariant(put), QVariant(screen)});
    }

    void aSmallerIconMovesAPutIconTowardTheFirstSlot()
    {
        QCOMPARE(text(rescaled(screenGrid(64, 114, 112), screenGrid(32, 114, 76), {at(1468, 568)})),
            QStringLiteral("1484,394"));
    }

    void widerSpacingMovesAPutIconAwayFromTheFirstSlot()
    {
        QCOMPARE(text(rescaled(screenGrid(32, 114, 76), screenGrid(32, 172, 76), {at(1484, 394)})),
            QStringLiteral("1273,394"));
    }

    void anIconRescaledOffTheScreenIsPlacedAgain()
    {
        const QVariantList result = rescaled(screenGrid(32, 84, 76), screenGrid(32, 172, 76), {at(10, 394)});
        QCOMPARE(result.size(), 1);
        QVERIFY2(result.at(0).isNull(), qPrintable(result.at(0).toString()));
    }

    void anIconNotPutByHandStaysUnplaced()
    {
        const QVariantList result = rescaled(screenGrid(64, 114, 112), screenGrid(32, 114, 76), {none()});
        QVERIFY(result.at(0).isNull());
    }

    void aLooseIconStaysLooseWhenRescaled()
    {
        const QVariantList result = rescaled(screenGrid(64, 114, 112), screenGrid(32, 114, 76), {loose(1468, 568)});
        QCOMPARE(result.at(0).toMap().value(QStringLiteral("loose")).toBool(), true);
    }

    void theFirstSlotOfTheReferenceDesktopIsWhereFinderPutsIt()
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "first", Q_RETURN_ARG(QVariant, result),
                                  Q_ARG(QVariant, QVariant(screenGrid(64, 114, 112))));
        QCOMPARE(text({result}), QStringLiteral("1827,30"));
    }
};

QTEST_MAIN(TestDesktopPlaces)
#include "test-desktop-places.moc"
