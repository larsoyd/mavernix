#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

// icons on a grid 100 apart, first column on the right as on the desktop
class TestDesktopKeys : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    static QVariant at(int x, int y) { return QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}}; }

    // 0 at 900,0  1 at 900,100  2 at 900,200  3 at 800,0  4 at 800,100
    static QVariantList grid() { return {at(900, 0), at(900, 100), at(900, 200), at(800, 0), at(800, 100)}; }

    int next(int from, const char* direction, const QVariantList& places = grid())
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "next", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant(places)),
            Q_ARG(QVariant, from), Q_ARG(QVariant, QString::fromLatin1(direction)));
        return result.toInt();
    }

    int typed(const QStringList& names, const QString& text)
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "typed", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant(names)), Q_ARG(QVariant, text));
        return result.toInt();
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/DesktopKeys.js\" as DesktopKeys\n"
                          "QtObject {\n"
                          "  function next(places, from, direction) { return DesktopKeys.next(places, from, direction) }\n"
                          "  function typed(names, text) { return DesktopKeys.typed(names, text) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void downTakesTheIconBelow() { QCOMPARE(next(0, "down"), 1); }

    void upTakesTheIconAbove() { QCOMPARE(next(2, "up"), 1); }

    void leftTakesTheNextColumnOnTheSameRow() { QCOMPARE(next(1, "left"), 4); }

    void rightComesBack() { QCOMPARE(next(4, "right"), 1); }

    void nothingThatWayKeepsThePick() { QCOMPARE(next(2, "down"), 2); }

    void theNearestInLineWinsOverACloserOneAside()
    {
        // 1 is straight left of 0, 2 is nearer but well below the line
        QCOMPARE(next(0, "left", {at(900, 0), at(700, 0), at(850, 90)}), 1);
    }

    void nothingPickedStartsAtTheFirstIcon() { QCOMPARE(next(-1, "down"), 0); }

    void typingPicksTheFirstNameItBegins()
    {
        QCOMPARE(typed({QStringLiteral("Alpha"), QStringLiteral("beta"), QStringLiteral("Bravo")}, QStringLiteral("br")), 2);
    }

    void typingFollowsNameOrderNotTheIconsOrder()
    {
        // icons sorted by kind, the name first in order still wins
        QCOMPARE(typed({QStringLiteral("Bz"), QStringLiteral("Ba")}, QStringLiteral("b")), 1);
    }

    void typingIgnoresCase() { QCOMPARE(typed({QStringLiteral("alpha"), QStringLiteral("Beta")}, QStringLiteral("BE")), 1); }

    void typingPastEveryNamePicksTheNearestAfterIt()
    {
        // as finder does, the name that would follow the text in order
        QCOMPARE(typed({QStringLiteral("Apple"), QStringLiteral("Cherry"), QStringLiteral("Date")}, QStringLiteral("b")), 1);
    }

    void typingPastTheLastNamePicksTheLast()
    {
        QCOMPARE(typed({QStringLiteral("Apple"), QStringLiteral("Cherry")}, QStringLiteral("zz")), 1);
    }

    void typingWithNoIconsPicksNothing() { QCOMPARE(typed({}, QStringLiteral("a")), -1); }
};

QTEST_MAIN(TestDesktopKeys)
#include "test-desktop-keys.moc"
