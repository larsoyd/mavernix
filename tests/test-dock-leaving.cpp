#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestDockLeaving : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    static QVariantMap kept(const char* key)
    {
        return {{"key", QString::fromLatin1(key)}, {"isSeparator", false}, {"kept", true}, {"listedAs", QString::fromLatin1(key)}};
    }

    static QVariantMap unkept(const char* key)
    {
        return {{"key", QString::fromLatin1(key)}, {"isSeparator", false}, {"kept", false}, {"listedAs", QString::fromLatin1(key)}};
    }

    // a separator, a window or the trash: no word on keeping at all
    static QVariantMap other(const char* key)
    {
        return {{"key", QString::fromLatin1(key)}, {"isSeparator", false}};
    }

    static QVariantMap leaving(const char* key)
    {
        return {{"key", QString::fromLatin1(key)}, {"isSeparator", false}, {"kept", false}, {"leaving", true}};
    }

    QVariantList merged(const QVariantList& shown, const QVariantList& live, bool leaves = true)
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "withLeaving", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant(QVariantList{QVariant(shown), QVariant(live), leaves})));
        return result.toList();
    }

    // every key in order, with a star on the ones marked as leaving
    static QString row(const QVariantList& places)
    {
        QStringList keys;
        for (const QVariant& place : places) {
            const QVariantMap map = place.toMap();
            keys.append(map.value("key").toString() + (map.value("leaving").toBool() ? "*" : ""));
        }
        return keys.join(' ');
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/DockLayout.js\" as DockLayout\n"
                          "QtObject {\n"
                          "  function withLeaving(a) { return DockLayout.withLeaving(a[0], a[1], a[2]) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void anUnkeptProgramThatGoesStaysWhereItStood()
    {
        const QVariantList shown{kept("a"), kept("b"), unkept("p"), other("separator 0"), other("trash")};
        const QVariantList live{kept("a"), kept("b"), other("separator 0"), other("trash")};
        const QVariantList places = merged(shown, live);
        QCOMPARE(row(places), QStringLiteral("a b p* separator 0 trash"));
        QVERIFY2(!places[2].toMap().contains("listedAs") || !places[2].toMap().value("listedAs").isValid(),
            "a leaving place can no longer be dragged or kept");
    }

    void aKeptPlaceTakenOutGoesAtOnce()
    {
        QCOMPARE(row(merged({kept("a"), kept("b")}, {kept("a")})), QStringLiteral("a"));
    }

    void aWindowOrTheTrashGoesAtOnce()
    {
        const QVariantList shown{kept("a"), other("separator 0"), other("window 3"), other("trash")};
        QCOMPARE(row(merged(shown, {kept("a")})), QStringLiteral("a"));
    }

    void aProgramThatComesBackIsNoLongerLeaving()
    {
        const QVariantList places = merged({kept("a"), leaving("p")}, {kept("a"), unkept("p")});
        QCOMPARE(row(places), QStringLiteral("a p"));
        QCOMPARE(places[1].toMap().value("listedAs").toString(), QStringLiteral("p"));
    }

    void twoThatLeaveTogetherKeepTheirOrder()
    {
        const QVariantList shown{kept("a"), unkept("p"), unkept("q"), other("trash")};
        QCOMPARE(row(merged(shown, {kept("a"), other("trash")})), QStringLiteral("a p* q* trash"));
    }

    void aLeavingPlaceStaysThroughLaterChanges()
    {
        const QVariantList shown{kept("a"), leaving("p"), other("trash")};
        const QVariantList live{kept("a"), unkept("n"), other("trash")};
        QCOMPARE(row(merged(shown, live)), QStringLiteral("a p* n trash"));
    }

    void itFollowsItsLeftNeighbourWhenThatOneMoves()
    {
        const QVariantList shown{kept("a"), kept("b"), unkept("p"), other("trash")};
        const QVariantList live{kept("b"), kept("a"), other("trash")};
        QCOMPARE(row(merged(shown, live)), QStringLiteral("b p* a trash"));
    }

    void withItsLeftNeighbourGoneItStandsAfterTheOneBefore()
    {
        const QVariantList shown{kept("a"), kept("b"), unkept("p"), other("trash")};
        QCOMPARE(row(merged(shown, {kept("a"), other("trash")})), QStringLiteral("a p* trash"));
    }

    void theFirstPlaceLeavingStaysFirst()
    {
        const QVariantList shown{unkept("p"), other("separator 0"), other("trash")};
        QCOMPARE(row(merged(shown, {other("trash")})), QStringLiteral("p* trash"));
    }

    void nothingNewLeavesWhenLeavingIsOff()
    {
        QCOMPARE(row(merged({kept("a"), unkept("p")}, {kept("a")}, false)), QStringLiteral("a"));
    }

    void oneAlreadyLeavingFinishesWhenLeavingIsOff()
    {
        QCOMPARE(row(merged({kept("a"), leaving("p")}, {kept("a")}, false)), QStringLiteral("a p*"));
    }

    void anEmptyRowStaysEmpty()
    {
        QCOMPARE(row(merged({}, {})), QString());
        QCOMPARE(row(merged({}, {kept("a")})), QStringLiteral("a"));
    }
};

QTEST_GUILESS_MAIN(TestDockLeaving)
#include "test-dock-leaving.moc"
