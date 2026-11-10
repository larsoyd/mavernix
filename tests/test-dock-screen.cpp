#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestDockScreen : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    static QVariantMap screen(const QString& name, int x, int y, int width, int height)
    {
        return {{QStringLiteral("name"), name}, {QStringLiteral("x"), x}, {QStringLiteral("y"), y},
            {QStringLiteral("width"), width}, {QStringLiteral("height"), height}};
    }

    QString home(const QVariantList& screens, const QString& main)
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(probe.get(), "home", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant(screens)), Q_ARG(QVariant, main));
        return called ? result.toString() : QStringLiteral("[not called]");
    }

    int slideMs(double travel)
    {
        QVariant result;
        QMetaObject::invokeMethod(probe.get(), "slideMs", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, travel));
        return result.toInt();
    }

    const QVariantMap main_screen = screen(QStringLiteral("main"), 0, 0, 1920, 1080);

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/DockScreen.js\" as DockScreen\n"
                          "QtObject {\n"
                          "  function home(screens, main) { return DockScreen.home(screens, main) }\n"
                          "  function slideMs(travel) { return DockScreen.slideMs(travel) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void aLoneScreenIsHome()
    {
        QCOMPARE(home({main_screen}, QStringLiteral("main")), QStringLiteral("main"));
    }

    void screensBesideTheMainOneLeaveItHome()
    {
        const QVariantList screens = {screen(QStringLiteral("left"), -1280, 0, 1280, 720), main_screen,
            screen(QStringLiteral("right"), 1920, 200, 2560, 1440)};
        QCOMPARE(home(screens, QStringLiteral("main")), QStringLiteral("main"));
    }

    void aScreenAboveLeavesItHome()
    {
        QCOMPARE(home({screen(QStringLiteral("above"), 0, -1080, 1920, 1080), main_screen}, QStringLiteral("main")),
            QStringLiteral("main"));
    }

    void theScreenUnderTheMainOneIsHome()
    {
        QCOMPARE(home({main_screen, screen(QStringLiteral("under"), 0, 1080, 1920, 1080)}, QStringLiteral("main")),
            QStringLiteral("under"));
    }

    void theLowestOfAColumnIsHome()
    {
        const QVariantList screens = {screen(QStringLiteral("lowest"), 300, 1800, 800, 600), main_screen,
            screen(QStringLiteral("under"), 0, 1080, 1280, 720)};
        QCOMPARE(home(screens, QStringLiteral("main")), QStringLiteral("lowest"));
    }

    void aScreenUnderPartOfTheEdgeCounts()
    {
        QCOMPARE(home({main_screen, screen(QStringLiteral("under"), 1900, 1080, 1280, 720)}, QStringLiteral("main")),
            QStringLiteral("under"));
    }

    void aScreenTouchingOnlyTheCornerDoesNot()
    {
        QCOMPARE(home({main_screen, screen(QStringLiteral("corner"), 1920, 1080, 1280, 720)}, QStringLiteral("main")),
            QStringLiteral("main"));
    }

    void aGapOfTwoRowsStillCountsAndThreeDoNot()
    {
        QCOMPARE(home({main_screen, screen(QStringLiteral("near"), 0, 1082, 1280, 720)}, QStringLiteral("main")),
            QStringLiteral("near"));
        QCOMPARE(home({main_screen, screen(QStringLiteral("far"), 0, 1083, 1280, 720)}, QStringLiteral("main")),
            QStringLiteral("main"));
    }

    void ofTwoUnderItTheLeftOneIsHome()
    {
        const QVariantList screens = {main_screen, screen(QStringLiteral("b"), 960, 1080, 960, 600),
            screen(QStringLiteral("a"), 0, 1080, 960, 600)};
        QCOMPARE(home(screens, QStringLiteral("main")), QStringLiteral("a"));
        QCOMPARE(home({screens[2], screens[0], screens[1]}, QStringLiteral("main")), QStringLiteral("a"));
    }

    void aCopyOfTheMainScreenLeavesItHome()
    {
        QCOMPARE(home({main_screen, screen(QStringLiteral("copy"), 0, 0, 1920, 1080)}, QStringLiteral("main")),
            QStringLiteral("main"));
    }

    void anUnknownMainScreenHasNoHome()
    {
        QCOMPARE(home({main_screen}, QStringLiteral("gone")), QString());
        QCOMPARE(home({}, QStringLiteral("main")), QString());
    }

    void theSlideGrowsWithTheLogOfTheWay()
    {
        QCOMPARE(slideMs(0), 60);
        QCOMPARE(slideMs(36), 180);
        QCOMPARE(slideMs(54), 240);
        QCOMPARE(slideMs(107), 240);
        QCOMPARE(slideMs(108), 300);
        QCOMPARE(slideMs(-54), 240);
    }
};

QTEST_GUILESS_MAIN(TestDockScreen)
#include "test-dock-screen.moc"
