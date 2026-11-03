#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <cmath>
#include <memory>

class TestClipboardStackSize : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    // the live theme's grid looks
    static QVariantMap looks()
    {
        return {{QStringLiteral("cell_width"), 128}, {QStringLiteral("cell_height"), 128},
            {QStringLiteral("side_padding"), 16}, {QStringLiteral("first_row_top"), 40},
            {QStringLiteral("bottom_padding"), 10}, {QStringLiteral("stage_share"), 0.8},
            {QStringLiteral("fit_spacing"), 28}};
    }

    QVariantMap call(const char* name, const QVariantList& arguments)
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(probe.get(), name, Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant(arguments)));
        return called ? result.toMap() : QVariantMap{{QStringLiteral("error"), QStringLiteral("not called")}};
    }

    QVariantMap layout(int count, double width, double height, const QVariant& size = QVariant())
    {
        return call("layout", {looks(), count, width, height, size});
    }

    double listHeight(const QString& size, double rowHeight, double padding, double room)
    {
        return call("listHeight", {size, rowHeight, padding, room}).value(QStringLiteral("height")).toDouble();
    }

    QVariantList menuItems(const QVariantMap& place)
    {
        return call("forClipboard", {place}).value(QStringLiteral("items")).toList();
    }

    static QVariantMap itemLabelled(const QVariantList& items, const QString& label)
    {
        for (const QVariant& item : items) {
            if (item.toMap().value(QStringLiteral("label")) == label) {
                return item.toMap();
            }
        }
        return {};
    }

    static double boxHeight(const QVariantMap& shape)
    {
        return 40 + shape[QStringLiteral("shownRows")].toInt() * 128 * shape[QStringLiteral("cellScale")].toDouble()
            + 10;
    }

    static double boxWidth(const QVariantMap& shape)
    {
        return 2 * 16 + shape[QStringLiteral("columns")].toInt() * 128 * shape[QStringLiteral("cellScale")].toDouble();
    }

    // free areas in logical pixels: wide, 1080p, 720p, 720p at scale 2, 1080p at 0.75, tiny
    static QList<QSizeF> screens()
    {
        return {{3440, 1290}, {1920, 960}, {1280, 628}, {640, 268}, {2560, 1320}, {320, 150}};
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "\"\n"
                          "import \"file://" SHELL_DIR "/FolderGridLayout.js\" as FolderGridLayout\n"
                          "import \"file://" SHELL_DIR "/StackSize.js\" as StackSize\n"
                          "QtObject {\n"
                          "  property DockMenus menus: DockMenus {\n"
                          "    config: ({})\n"
                          "    words: ({})\n"
                          "    dockFolders: QtObject {\n"
                          "      readonly property var views: [\"fan\", \"grid\", \"list\"]\n"
                          "      property var clipboard: null\n"
                          "      function isClipboard(folder) { return folder?.clipboard === true }\n"
                          "      function viewOf(folder) { return folder.view === \"list\" ? \"list\" : \"grid\" }\n"
                          "    }\n"
                          "  }\n"
                          "  function layout(a) {\n"
                          "    const bound = a[4] === undefined ? undefined : StackSize.bounds(a[4])\n"
                          "    return FolderGridLayout.layout(a[0], a[1], a[2], a[3], bound)\n"
                          "  }\n"
                          "  function listHeight(a) { return { height: StackSize.listHeight(a[0], a[1], a[2], a[3]) } }\n"
                          "  function forClipboard(a) { return { items: menus.forClipboard(a[0]) } }\n"
                          "  function clipboardList(a) {\n"
                          "    const icons = { forFolderEntry: entry => \"\" }\n"
                          "    return menus.clipboardList(a[0], icons, a[1])\n"
                          "  }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void aFolderWithoutABoundStillFillsTheStage()
    {
        const QVariantMap shape = layout(100, 3440, 1290);
        QCOMPARE(shape[QStringLiteral("columns")].toInt(), 10);
        QCOMPARE(shape[QStringLiteral("shownRows")].toInt(), 10);
    }

    void mediumIsTheDefaultAndShowsFourByThree()
    {
        for (const QVariant& size : {QVariant(QStringLiteral("medium")), QVariant(QString()),
                 QVariant(QStringLiteral("huge"))}) {
            const QVariantMap shape = layout(100, 3440, 1290, size);
            QCOMPARE(shape[QStringLiteral("columns")].toInt(), 4);
            QCOMPARE(shape[QStringLiteral("shownRows")].toInt(), 3);
            QCOMPARE(shape[QStringLiteral("rows")].toInt(), 25);
            QCOMPARE(shape[QStringLiteral("cellScale")].toDouble(), 1.0);
        }
    }

    void smallShowsFewerSmallerCells()
    {
        const QVariantMap shape = layout(100, 3440, 1290, QStringLiteral("small"));
        QCOMPARE(shape[QStringLiteral("columns")].toInt(), 3);
        QCOMPARE(shape[QStringLiteral("shownRows")].toInt(), 2);
        QVERIFY2(shape[QStringLiteral("cellScale")].toDouble() < 1.0, "small cells are smaller");
    }

    void largeShowsMoreButNeverTheWholeScreen()
    {
        const QVariantMap shape = layout(100, 3440, 1290, QStringLiteral("large"));
        QCOMPARE(shape[QStringLiteral("columns")].toInt(), 6);
        QCOMPARE(shape[QStringLiteral("shownRows")].toInt(), 4);
    }

    void fewCopiesMakeASmallBox()
    {
        const QVariantMap shape = layout(2, 3440, 1290, QStringLiteral("medium"));
        QCOMPARE(shape[QStringLiteral("columns")].toInt(), 2);
        QCOMPARE(shape[QStringLiteral("shownRows")].toInt(), 1);
        const QVariantMap none = layout(1, 3440, 1290, QStringLiteral("small"));
        QCOMPARE(none[QStringLiteral("columns")].toInt(), 1);
    }

    void noCountShowsMoreColumnsOrRowsThanTheSizeAllows()
    {
        const QHash<QString, QPair<int, int>> most{{QStringLiteral("small"), {3, 2}},
            {QStringLiteral("medium"), {4, 3}}, {QStringLiteral("large"), {6, 4}}};
        for (auto size = most.cbegin(); size != most.cend(); ++size) {
            for (int count = 1; count <= 40; count++) {
                const QVariantMap shape = layout(count, 3440, 1290, size.key());
                const int columns = shape[QStringLiteral("columns")].toInt();
                const int rows = shape[QStringLiteral("shownRows")].toInt();
                const QString where = QStringLiteral("%1 with %2: %3 x %4").arg(size.key()).arg(count).arg(columns).arg(rows);
                QVERIFY2(columns <= size.value().first && rows <= size.value().second, qPrintable(where));
                QVERIFY2(columns * rows >= std::min(count, size.value().first * size.value().second),
                    qPrintable(where + " hides copies that fit"));
            }
        }
    }

    void aSmallScreenShrinksCellsBeforeShowingFewer()
    {
        for (const QString& size : {QStringLiteral("small"), QStringLiteral("medium"), QStringLiteral("large")}) {
            const QVariantMap shape = layout(100, 640, 268, size);
            const int shown = shape[QStringLiteral("columns")].toInt() * shape[QStringLiteral("shownRows")].toInt();
            QVERIFY2(shown >= 3, qPrintable(QStringLiteral("%1 shows %2 cells").arg(size).arg(shown)));
            QVERIFY2(shape[QStringLiteral("cellScale")].toDouble() >= 0.5,
                qPrintable(QStringLiteral("%1 cells at %2").arg(size).arg(shape[QStringLiteral("cellScale")].toDouble())));
        }
    }

    void everySizeStaysInsideItsShareOfEveryScreen()
    {
        const QHash<QString, double> shares{{QStringLiteral("small"), 0.4}, {QStringLiteral("medium"), 0.5},
            {QStringLiteral("large"), 0.75}};
        for (const QSizeF& screen : screens()) {
            double lastArea = 0;
            for (const QString& size : {QStringLiteral("small"), QStringLiteral("medium"), QStringLiteral("large")}) {
                for (int count : {1, 7, 12, 40, 100}) {
                    const QVariantMap shape = layout(count, screen.width(), screen.height(), size);
                    const QString where = QStringLiteral("%1 on %2x%3 with %4: %5 x %6 at %7")
                                              .arg(size)
                                              .arg(screen.width())
                                              .arg(screen.height())
                                              .arg(count)
                                              .arg(shape[QStringLiteral("columns")].toInt())
                                              .arg(shape[QStringLiteral("shownRows")].toInt())
                                              .arg(shape[QStringLiteral("cellScale")].toDouble());
                    const double cell = 128 * shape[QStringLiteral("cellScale")].toDouble();
                    QVERIFY2(cell > 0 && cell == std::floor(cell), qPrintable(where + " cell is not whole pixels"));
                    QVERIFY2(boxHeight(shape) <= std::max(screen.height() * shares[size] + 50, 50 + cell),
                        qPrintable(where + QStringLiteral(" too tall: %1").arg(boxHeight(shape))));
                    QVERIFY2(boxHeight(shape) <= screen.height(),
                        qPrintable(where + QStringLiteral(" taller than the screen: %1").arg(boxHeight(shape))));
                    QVERIFY2(boxWidth(shape) <= screen.width(),
                        qPrintable(where + QStringLiteral(" wider than the screen: %1").arg(boxWidth(shape))));
                    if (count == 100) {
                        const double area = boxWidth(shape) * boxHeight(shape);
                        QVERIFY2(area >= lastArea, qPrintable(where + " is smaller than the size below it"));
                        lastArea = area;
                    }
                }
            }
        }
    }

    void theListIsBoundedByRowsAndByTheScreen()
    {
        QCOMPARE(listHeight(QStringLiteral("medium"), 19, 4, 1290), 12 * 19 + 8.0);
        QCOMPARE(listHeight(QStringLiteral("small"), 19, 4, 1290), 8 * 19 + 8.0);
        QCOMPARE(listHeight(QStringLiteral("large"), 19, 4, 1290), 20 * 19 + 8.0);
        QCOMPARE(listHeight(QStringLiteral("nonsense"), 19, 4, 1290), 12 * 19 + 8.0);
        for (double room : {600.0, 268.0, 150.0, 60.0, 20.0}) {
            for (const QString& size : {QStringLiteral("small"), QStringLiteral("medium"), QStringLiteral("large")}) {
                const double height = listHeight(size, 19, 4, room);
                const QString where = QStringLiteral("%1 in %2: %3").arg(size).arg(room).arg(height);
                QVERIFY2(height <= room, qPrintable(where + " runs past the room"));
                QVERIFY2(height == std::floor(height), qPrintable(where + " is not whole pixels"));
                QVERIFY2(height >= std::min(room, 3 * 19 + 8.0), qPrintable(where + " shows under three rows"));
            }
        }
        QVERIFY(listHeight(QStringLiteral("medium"), 19, 4, 268) <= 268 * 0.5);
    }

    void theTileMenuOffersTheSizesWithTheChosenOneTicked()
    {
        const QVariantMap plain{{QStringLiteral("clipboard"), true}};
        QVariantList items = menuItems(plain);
        QVERIFY2(itemLabelled(items, QStringLiteral("Size")).value(QStringLiteral("enabled")) == false,
            "a Size heading");
        QCOMPARE(itemLabelled(items, QStringLiteral("Medium")).value(QStringLiteral("checked")).toBool(), true);
        QCOMPARE(itemLabelled(items, QStringLiteral("Small")).value(QStringLiteral("checked")).toBool(), false);
        QCOMPARE(itemLabelled(items, QStringLiteral("Small")).value(QStringLiteral("id")).toInt(), 600);
        QCOMPARE(itemLabelled(items, QStringLiteral("Large")).value(QStringLiteral("id")).toInt(), 602);

        QVariantMap small = plain;
        small[QStringLiteral("size")] = QStringLiteral("small");
        items = menuItems(small);
        QCOMPARE(itemLabelled(items, QStringLiteral("Small")).value(QStringLiteral("checked")).toBool(), true);
        QCOMPARE(itemLabelled(items, QStringLiteral("Medium")).value(QStringLiteral("checked")).toBool(), false);
    }

    void theListCutsLongCopiesShorterWhenSmall()
    {
        const QVariantMap copy{{QStringLiteral("name"), QString(400, QLatin1Char('x'))},
            {QStringLiteral("picture"), QString()}, {QStringLiteral("kind"), QStringLiteral("text")}};
        const QHash<QString, int> longest{
            {QStringLiteral("small"), 32}, {QStringLiteral("medium"), 44}, {QStringLiteral("large"), 60}};
        for (auto size = longest.cbegin(); size != longest.cend(); ++size) {
            const QVariantList items = call("clipboardList", {QVariantList{copy}, size.key()})
                                           .value(QStringLiteral("items"))
                                           .toList();
            QVERIFY2(!items.isEmpty(), qPrintable(size.key() + " gave no items"));
            const QString label = items.first().toMap().value(QStringLiteral("label")).toString();
            QCOMPARE(label.length(), size.value());
            QVERIFY(label.endsWith(QChar(0x2026)));
        }
        const QVariantList shortOne = call("clipboardList",
            {QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("a")},
                 {QStringLiteral("picture"), QString()}, {QStringLiteral("kind"), QStringLiteral("text")}}},
                QStringLiteral("small")})
                                          .value(QStringLiteral("items"))
                                          .toList();
        QCOMPARE(shortOne.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("a"));
    }
};

QTEST_MAIN(TestClipboardStackSize)
#include "test-clipboard-stack-size.moc"
