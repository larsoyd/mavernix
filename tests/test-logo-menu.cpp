#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestLogoMenu : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    // Apple menu, Dock, as on Mavericks
    static QVariantList entries()
    {
        const QVariantMap hiding = {{QStringLiteral("label"), QStringLiteral("Turn Hiding On")},
            {QStringLiteral("label_on"), QStringLiteral("Turn Hiding Off")}, {QStringLiteral("state"), QStringLiteral("dock_hiding")},
            {QStringLiteral("action"), QStringLiteral("dock_hiding")}};
        const auto side = [](const char* label, const char* action) {
            return QVariantMap{{QStringLiteral("label"), QString::fromLatin1(label)},
                {QStringLiteral("ticked"), QString::fromLatin1(action)}, {QStringLiteral("action"), QString::fromLatin1(action)}};
        };
        const QVariantMap dock = {{QStringLiteral("label"), QStringLiteral("Dock")},
            {QStringLiteral("entries"), QVariantList{hiding, QVariantMap{{QStringLiteral("separator"), true}},
                side("Position on Left", "dock_position_left"), side("Position on Bottom", "dock_position_bottom"),
                side("Position on Right", "dock_position_right")}}};
        return {QVariantMap{{QStringLiteral("label"), QStringLiteral("About This Mac")}}, dock};
    }

    QVariantList items(const QStringList& held)
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "items", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant(entries())),
            Q_ARG(QVariant, QVariant(held)));
        return result.toList();
    }

    QVariantMap entryFor(int id)
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "entryFor", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant(entries())), Q_ARG(QVariant, id));
        return result.toMap();
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/LogoMenu.js\" as LogoMenu\n"
                          "QtObject {\n"
                          "  function items(entries, held) { return LogoMenu.items(entries, state => held.includes(state)) }\n"
                          "  function entryFor(entries, id) { return LogoMenu.entryFor(entries, id) ?? ({}) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void anEntryWithEntriesIsASubmenu()
    {
        const QVariantList shown = items({});
        QCOMPARE(shown.size(), 2);
        const QVariantList children = shown[1].toMap()[QStringLiteral("children")].toList();
        QCOMPARE(children.size(), 5);
        QCOMPARE(children[2].toMap()[QStringLiteral("label")].toString(), QStringLiteral("Position on Left"));
        QVERIFY(children[1].toMap()[QStringLiteral("separator")].toBool());
    }

    void theSideTheDockIsOnIsTicked()
    {
        const QVariantList children = items({QStringLiteral("dock_position_right")})[1].toMap()[QStringLiteral("children")].toList();
        QCOMPARE(children[2].toMap()[QStringLiteral("checked")].toBool(), false);
        QCOMPARE(children[3].toMap()[QStringLiteral("checked")].toBool(), false);
        QCOMPARE(children[4].toMap()[QStringLiteral("checked")].toBool(), true);
        QVERIFY(children[4].toMap()[QStringLiteral("checkable")].toBool());
    }

    void aToggleSaysWhatItWillDo()
    {
        QCOMPARE(items({})[1].toMap()[QStringLiteral("children")].toList()[0].toMap()[QStringLiteral("label")].toString(),
            QStringLiteral("Turn Hiding On"));
        QCOMPARE(items({QStringLiteral("dock_hiding")})[1].toMap()[QStringLiteral("children")].toList()[0].toMap()[QStringLiteral("label")].toString(),
            QStringLiteral("Turn Hiding Off"));
    }

    void everyIdLeadsBackToItsEntry()
    {
        const QVariantList shown = items({});
        QCOMPARE(entryFor(shown[0].toMap()[QStringLiteral("id")].toInt())[QStringLiteral("label")].toString(), QStringLiteral("About This Mac"));
        const QVariantList children = shown[1].toMap()[QStringLiteral("children")].toList();
        for (int number = 2; number < 5; number++)
        {
            const QVariantMap child = children[number].toMap();
            QCOMPARE(entryFor(child[QStringLiteral("id")].toInt())[QStringLiteral("action")].toString(),
                entries()[1].toMap()[QStringLiteral("entries")].toList()[number].toMap()[QStringLiteral("action")].toString());
        }
    }
};

QTEST_GUILESS_MAIN(TestLogoMenu)
#include "test-logo-menu.moc"
