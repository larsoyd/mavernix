#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestDockAxis : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    QVariant call(const char* name, const QVariantList& arguments)
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(probe.get(), "invoke", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QString::fromLatin1(name)), Q_ARG(QVariant, QVariant(arguments)));
        return called ? result : QVariant(QStringLiteral("[not called]"));
    }

    QPointF point(const QVariant& value)
    {
        const QVariantMap map = value.toMap();
        return {map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble()};
    }

    // a point of the strip on a window of the given size
    QPointF toWindow(const QString& side, double thickness, QPointF along)
    {
        const QVariantList matrix = call("stripMatrix", {side, thickness}).toList();
        if (matrix.size() != 6) {
            return {-9999, -9999};
        }
        const auto at = [&](int index) { return matrix[index].toDouble(); };
        return {at(0) * along.x() + at(1) * along.y() + at(2), at(3) * along.x() + at(4) * along.y() + at(5)};
    }

    QPointF toStrip(const QString& side, double thickness, QPointF window)
    {
        return point(call("toStrip", {side, thickness, window.x(), window.y()}));
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/DockAxis.js\" as DockAxis\n"
                          "import \"file://" SHELL_DIR "/DockLayout.js\" as DockLayout\n"
                          "QtObject {\n"
                          "  function invoke(name, args) { return DockAxis[name](...args) }\n"
                          "  function shelfEdges(looks, tile, start, end) {\n"
                          "    return DockLayout.shelfEdges(looks, tile, start, end, DockLayout.pushNow(looks, tile, start, end, 0, 0))\n"
                          "  }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void theBottomStripIsTheWindow()
    {
        QCOMPARE(toWindow(QStringLiteral("bottom"), 90, {300, 80}), QPointF(300, 80));
        QCOMPARE(toStrip(QStringLiteral("bottom"), 90, {300, 80}), QPointF(300, 80));
    }

    // the strip's lower edge is the screen's edge and its x runs down the screen, Finder at the top
    void aLeftStripStandsOnTheLeftEdge()
    {
        QCOMPARE(toWindow(QStringLiteral("left"), 90, {0, 90}), QPointF(0, 0));
        QCOMPARE(toWindow(QStringLiteral("left"), 90, {500, 90}), QPointF(0, 500));
        QCOMPARE(toWindow(QStringLiteral("left"), 90, {500, 0}), QPointF(90, 500));
    }

    void aRightStripStandsOnTheRightEdge()
    {
        QCOMPARE(toWindow(QStringLiteral("right"), 90, {0, 90}), QPointF(90, 0));
        QCOMPARE(toWindow(QStringLiteral("right"), 90, {500, 90}), QPointF(90, 500));
        QCOMPARE(toWindow(QStringLiteral("right"), 90, {500, 0}), QPointF(0, 500));
    }

    void windowPointsGoBackToTheStrip()
    {
        for (const QString side : {QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right")}) {
            for (const QPointF along : {QPointF(0, 0), QPointF(37, 12), QPointF(812.5, 63)}) {
                QCOMPARE(toStrip(side, 90, toWindow(side, 90, along)), along);
            }
        }
    }

    // upright content in a box of the strip lands on the box's own square of the screen, the right way up
    void uprightContentFillsItsBoxTheRightWayUp()
    {
        const struct {
            QString side;
            QPointF topLeft, topRight, bottomLeft;
        } cases[] = {
            {QStringLiteral("bottom"), {0, 0}, {40, 0}, {0, 16}},
            // a box 16 along and 40 across shows 40 wide and 16 high, its left side on the screen's edge
            {QStringLiteral("left"), {0, 40}, {0, 0}, {16, 40}},
            {QStringLiteral("right"), {0, 0}, {0, 40}, {16, 0}},
        };
        for (const auto& wanted : cases) {
            const QVariantList matrix = call("uprightMatrix", {wanted.side, 16, 40}).toList();
            QCOMPARE(matrix.size(), 6);
            const auto map = [&](QPointF content) {
                return QPointF(matrix[0].toDouble() * content.x() + matrix[1].toDouble() * content.y()
                                   + matrix[2].toDouble(),
                    matrix[3].toDouble() * content.x() + matrix[4].toDouble() * content.y() + matrix[5].toDouble());
            };
            const bool sideways = wanted.side != QStringLiteral("bottom");
            const double contentWidth = sideways ? 40 : 16;
            const double contentHeight = sideways ? 16 : 40;
            QCOMPARE(map({0, 0}), wanted.topLeft);
            QCOMPARE(map({contentWidth, 0}), wanted.side == QStringLiteral("bottom") ? QPointF(16, 0) : wanted.topRight);
            QCOMPARE(map({0, contentHeight}), wanted.side == QStringLiteral("bottom") ? QPointF(0, 40) : wanted.bottomLeft);
            // and through the strip on the screen, right is right and down is down
            const QPointF origin = toWindow(wanted.side, 90, map({0, 0}));
            const QPointF right = toWindow(wanted.side, 90, map({1, 0})) - origin;
            const QPointF down = toWindow(wanted.side, 90, map({0, 1})) - origin;
            QCOMPARE(right, QPointF(1, 0));
            QCOMPARE(down, QPointF(0, 1));
        }
    }

    void theWindowHangsOnItsEdge()
    {
        const QVariantMap bottom = call("anchors", {QStringLiteral("bottom")}).toMap();
        QVERIFY(bottom[QStringLiteral("left")].toBool() && bottom[QStringLiteral("right")].toBool()
                && bottom[QStringLiteral("bottom")].toBool() && !bottom[QStringLiteral("top")].toBool());
        const QVariantMap left = call("anchors", {QStringLiteral("left")}).toMap();
        QVERIFY(left[QStringLiteral("left")].toBool() && left[QStringLiteral("top")].toBool()
                && left[QStringLiteral("bottom")].toBool() && !left[QStringLiteral("right")].toBool());
        const QVariantMap right = call("anchors", {QStringLiteral("right")}).toMap();
        QVERIFY(right[QStringLiteral("right")].toBool() && right[QStringLiteral("top")].toBool()
                && right[QStringLiteral("bottom")].toBool() && !right[QStringLiteral("left")].toBool());
    }

    void theWindowsCornerOnTheScreen()
    {
        QCOMPARE(point(call("windowOrigin", {QStringLiteral("bottom"), 1920, 1080, 1920, 0})), QPointF(0, 1080));
        QCOMPARE(point(call("windowOrigin", {QStringLiteral("left"), 1920, 1080, 300, 0})), QPointF(0, 1080));
        QCOMPARE(point(call("windowOrigin", {QStringLiteral("right"), 1920, 1080, 300, 0})), QPointF(1620, 1080));
    }

    // Mavericks pads a side dock's row by a tile / 2.5, at most 8
    void theShelfsEndsArePaddedUpToTheirMost()
    {
        const QVariantMap looks = {{QStringLiteral("end_padding"), QVariantList{0.4, 0}},
            {QStringLiteral("end_padding_most"), 8}, {QStringLiteral("end_trim"), 0},
            {QStringLiteral("magnification_reach"), 3}};
        const auto edges = [&](double tile) {
            QVariant result;
            QMetaObject::invokeMethod(probe.get(), "shelfEdges", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, looks),
                Q_ARG(QVariant, tile), Q_ARG(QVariant, 100.0), Q_ARG(QVariant, 500.0));
            const QVariantMap map = result.toMap();
            return QPair<int, int>(map[QStringLiteral("left")].toInt(), map[QStringLiteral("right")].toInt());
        };
        QCOMPARE(edges(16), qMakePair(94, 506));
        QCOMPARE(edges(47), qMakePair(92, 508));
        QVariantMap open = looks;
        open.remove(QStringLiteral("end_padding_most"));
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "shelfEdges", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, open),
            Q_ARG(QVariant, 47.0), Q_ARG(QVariant, 100.0), Q_ARG(QVariant, 500.0));
        QCOMPARE(result.toMap()[QStringLiteral("left")].toInt(), 82);
    }

    void anUnknownSideIsTheBottom()
    {
        QCOMPARE(call("sideOf", {QStringLiteral("left")}).toString(), QStringLiteral("left"));
        QCOMPARE(call("sideOf", {QStringLiteral("right")}).toString(), QStringLiteral("right"));
        QCOMPARE(call("sideOf", {QStringLiteral("top")}).toString(), QStringLiteral("bottom"));
        QCOMPARE(call("sideOf", {QVariant()}).toString(), QStringLiteral("bottom"));
    }
};

QTEST_GUILESS_MAIN(TestDockAxis)
#include "test-dock-axis.moc"
