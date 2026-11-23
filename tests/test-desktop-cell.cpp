#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

// cells read off the reference desktop with lucida grande bold, whose zero
// is 7, 8, 9 and 11 wide and whose line is 13, 16, 18 and 19 high at 10, 12, 14 and 16
class TestDesktopCell : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    QString cell(int iconSize, int spacing, int textSize, bool right, bool info)
    {
        const QHash<int, QPair<int, int>> font{{10, {7, 13}}, {12, {8, 16}}, {14, {9, 18}}, {16, {11, 19}}};
        const QVariantMap view{{QStringLiteral("iconSize"), iconSize},
                               {QStringLiteral("spacing"), spacing},
                               {QStringLiteral("right"), right},
                               {QStringLiteral("info"), info}};
        const QVariantMap metrics{{QStringLiteral("zero"), font[textSize].first},
                                  {QStringLiteral("line"), font[textSize].second},
                                  {QStringLiteral("infoLine"), font[std::max(10, textSize - 2)].second}};
        QVariant result;
        if (!QMetaObject::invokeMethod(probe.get(), "cell", Q_RETURN_ARG(QVariant, result),
                                       Q_ARG(QVariant, QVariant(view)), Q_ARG(QVariant, QVariant(metrics))))
        {
            QTest::qFail("probe not called", __FILE__, __LINE__);
        }
        const QVariantMap size = result.toMap();
        return QStringLiteral("%1x%2").arg(size.value(QStringLiteral("width")).toDouble())
            .arg(size.value(QStringLiteral("height")).toDouble());
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/DesktopCell.js\" as DesktopCell\n"
                          "import \"file://" SHELL_DIR "/DesktopView.js\" as DesktopView\n"
                          "QtObject {\n"
                          "  function cell(view, metrics) { return DesktopCell.cell(view, metrics) }\n"
                          "  function layout(view, size) { return DesktopCell.layout(view, size) }\n"
                          "  function iconSize(raw) { return DesktopView.iconSize(raw) }\n"
                          "  function arrangement(a) { return DesktopView.arrangement(a[0], a[1]) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void theDefaultCellHoldsTwoLinesUnderTheIcon() { QCOMPARE(cell(64, 54, 12, false, false), QStringLiteral("114x112")); }

    void theIconSizeChangesOnlyTheHeightWhileTheTitleIsWider()
    {
        QCOMPARE(cell(16, 54, 12, false, false), QStringLiteral("114x58"));
        QCOMPARE(cell(32, 54, 12, false, false), QStringLiteral("114x76"));
    }

    void aLargeIconIsWiderThanTheTitleRoom() { QCOMPARE(cell(128, 54, 12, false, false), QStringLiteral("156x184")); }

    void theTextSizeScalesRoomAndLines()
    {
        QCOMPARE(cell(64, 54, 10, false, false), QStringLiteral("100x106"));
        QCOMPARE(cell(64, 54, 16, false, false), QStringLiteral("152x118"));
    }

    void tightSpacingGivesOneLineUpTo25()
    {
        QCOMPARE(cell(64, 1, 12, false, false), QStringLiteral("84x96"));
        QCOMPARE(cell(64, 25, 12, false, false), QStringLiteral("84x96"));
        QCOMPARE(cell(64, 26, 12, false, false), QStringLiteral("84x112"));
    }

    void wideSpacingStopsAtTwentyZeros() { QCOMPARE(cell(64, 100, 12, false, false), QStringLiteral("172x112")); }

    void itemInfoAddsALineRoundedToEven() { QCOMPARE(cell(64, 54, 12, false, true), QStringLiteral("114x126")); }

    void aTitleOnTheRightSitsBesideTheIcon()
    {
        QCOMPARE(cell(64, 54, 12, true, false), QStringLiteral("188x78"));
        QCOMPARE(cell(128, 54, 12, true, false), QStringLiteral("260x150"));
        QCOMPARE(cell(32, 10, 14, true, false), QStringLiteral("100x42"));
    }

    void aSmallIconWithItsTitleOnTheRightKeepsOneLine() { QCOMPARE(cell(16, 54, 12, true, false), QStringLiteral("134x22")); }

    void aSmallIconTallerThanItsTitleGetsOneSpareRow() { QCOMPARE(cell(16, 54, 10, true, false), QStringLiteral("120x20")); }

    void theIconMarginIsAWholeSixteenth()
    {
        QCOMPARE(cell(40, 54, 12, true, false), QStringLiteral("160x50"));
        QCOMPARE(cell(40, 54, 12, false, false), QStringLiteral("114x84"));
    }

    QString layout(int iconSize, int width, int height, bool right)
    {
        const QVariantMap view{{QStringLiteral("iconSize"), iconSize}, {QStringLiteral("right"), right}};
        const QVariantMap size{{QStringLiteral("width"), width}, {QStringLiteral("height"), height}};
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "layout", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant(view)),
                                  Q_ARG(QVariant, QVariant(size)));
        const QVariantMap map = result.toMap();
        return QStringLiteral("%1,%2 %3,%4").arg(map.value(QStringLiteral("pictureX")).toDouble())
            .arg(map.value(QStringLiteral("pictureY")).toDouble()).arg(map.value(QStringLiteral("titleX")).toDouble())
            .arg(map.value(QStringLiteral("titleY")).toDouble());
    }

    // icon middles read 57,36 and 36,39 and 9,11 from their cells' corners
    void aPictureSitsAtTheTopMiddleOverItsTitle() { QCOMPARE(layout(64, 114, 112, false), QStringLiteral("25,4 0,74")); }

    void aPictureSitsAtTheMiddleLeftBesideItsTitle() { QCOMPARE(layout(64, 188, 78, true), QStringLiteral("4,7 74,0")); }

    void aSmallPictureBesideItsTitleIsCentredToo() { QCOMPARE(layout(16, 134, 22, true), QStringLiteral("1,3 20,0")); }

    QVariant ask(const char* function, const QVariant& argument)
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
        return result;
    }

    // the reference slider left at 69.0, 72.9 and 77.7 showed 68, 72 and 76
    void theIconSizeStepsByFour()
    {
        QCOMPARE(ask("iconSize", 69.0).toInt(), 68);
        QCOMPARE(ask("iconSize", 72.9).toInt(), 72);
        QCOMPARE(ask("iconSize", 77.7).toInt(), 76);
    }

    void theIconSizeStaysBetween16And128()
    {
        QCOMPARE(ask("iconSize", 4).toInt(), 16);
        QCOMPARE(ask("iconSize", 512).toInt(), 128);
    }

    QString arrangement(const QVariant& stored, const QVariant& snap)
    {
        return ask("arrangement", QVariantList{stored, snap}).toString();
    }

    void aChosenArrangementIsKept() { QCOMPARE(arrangement(QStringLiteral("size"), true), QStringLiteral("size")); }

    void theOldGridSwitchBecomesSnapToGrid() { QCOMPARE(arrangement(QVariant(), true), QStringLiteral("grid")); }

    void nothingChosenIsFree() { QCOMPARE(arrangement(QVariant(), QVariant()), QStringLiteral("none")); }

    // a stored spacing past the slider's end, as finder's settings allow up to 512
    void spacingPastTheSliderStaysAtTwentyZeros() { QCOMPARE(cell(64, 300, 12, false, false), QStringLiteral("172x112")); }

    void itemInfoOnTheRightFitsBesideALargerIcon() { QCOMPARE(cell(64, 54, 12, true, true), QStringLiteral("188x78")); }
};

QTEST_MAIN(TestDesktopCell)
#include "test-desktop-cell.moc"
