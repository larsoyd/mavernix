#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestDockMenus : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    QVariantList dockMenu(const QString& side, bool hides)
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(probe.get(), "dockMenu", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, side), Q_ARG(QVariant, hides));
        return called ? result.toList() : QVariantList{};
    }

    static QStringList labels(const QVariantList& items)
    {
        QStringList result;
        for (const QVariant& item : items) {
            const QVariantMap map = item.toMap();
            result << (map.value(QStringLiteral("separator")).toBool() ? QStringLiteral("-")
                                                                        : map.value(QStringLiteral("label")).toString());
        }
        return result;
    }

    static QVariantMap labelled(const QVariantList& items, const QString& label)
    {
        for (const QVariant& item : items) {
            if (item.toMap().value(QStringLiteral("label")) == label) {
                return item.toMap();
            }
        }
        return {};
    }

    int id(const char* name)
    {
        return probe->property("menus").value<QObject*>()->property(name).toInt();
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "\"\n"
                          "QtObject {\n"
                          "  property DockMenus menus: DockMenus {\n"
                          "    config: ({ settings_command: [\"settings\"] })\n"
                          "    words: ({})\n"
                          "    dockFolders: QtObject {}\n"
                          "  }\n"
                          "  function dockMenu(side, hides) {\n"
                          "    menus.side = side\n"
                          "    menus.hidesItself = hides\n"
                          "    return menus.forDock()\n"
                          "  }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    // as on Mavericks: hiding, magnification, the position, then the settings
    void theDockMenuOffersItsPositionAfterHidingAndMagnification()
    {
        QCOMPARE(labels(dockMenu(QStringLiteral("bottom"), false)),
            QStringList({QStringLiteral("Turn hiding on"), QStringLiteral("Turn magnification on"),
                QStringLiteral("Position on screen"), QStringLiteral("-"), QStringLiteral("Dock settings…")}));
        QCOMPARE(labelled(dockMenu(QStringLiteral("bottom"), true), QStringLiteral("Turn hiding off"))
                     .value(QStringLiteral("id"))
                     .toInt(),
            id("hidingId"));
    }

    void thePositionsAreTickedWhereTheDockIs()
    {
        for (const QString& side : {QStringLiteral("left"), QStringLiteral("bottom"), QStringLiteral("right")}) {
            const QVariantList children =
                labelled(dockMenu(side, false), QStringLiteral("Position on screen")).value(QStringLiteral("children")).toList();
            QCOMPARE(labels(children), QStringList({QStringLiteral("Left"), QStringLiteral("Bottom"), QStringLiteral("Right")}));
            const QStringList sides = {QStringLiteral("left"), QStringLiteral("bottom"), QStringLiteral("right")};
            for (int number = 0; number < 3; number++) {
                const QVariantMap item = children[number].toMap();
                QCOMPARE(item.value(QStringLiteral("checked")).toBool(), sides[number] == side);
                QCOMPARE(item.value(QStringLiteral("id")).toInt(), id("positionBaseId") + number);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestDockMenus)
#include "test-dock-menus.moc"
