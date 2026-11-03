#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestFolderGridScroll : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    // rows of 100 px, the last row starts at 900
    double call(const char* name, double from, double by)
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(probe.get(), name, Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, from), Q_ARG(QVariant, by));
        return called ? result.toDouble() : -1;
    }
    double resting(double contentY, double travel) { return call("resting", contentY, travel); }
    double wheel(double goal, double notches) { return call("wheel", goal, notches); }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/FolderGridLayout.js\" as FolderGridLayout\n"
                          "QtObject {\n"
                          "  function resting(y, travel) { return FolderGridLayout.restingY(y, 100, 900, travel) }\n"
                          "  function wheel(goal, notches) { return FolderGridLayout.wheelGoal(goal, notches, 100, 900) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void aScrollDownNeverRestsAboveWhereItStopped()
    {
        for (double y : {101.0, 130.0, 149.0, 151.0, 199.0}) {
            QCOMPARE(resting(y, 1), 200.0);
        }
    }

    void aScrollUpNeverRestsBelowWhereItStopped()
    {
        for (double y : {101.0, 149.0, 151.0, 199.0}) {
            QCOMPARE(resting(y, -1), 100.0);
        }
    }

    void aRowAlreadyReachedStays()
    {
        QCOMPARE(resting(200.2, 1), 200.0);
        QCOMPARE(resting(199.8, -1), 200.0);
        QCOMPARE(resting(140, 0), 100.0);
        QCOMPARE(resting(160, 0), 200.0);
    }

    void restingKeepsInsideTheContent()
    {
        QCOMPARE(resting(-30, -1), 0.0);
        QCOMPARE(resting(950, 1), 900.0);
        QCOMPARE(resting(899, 1), 900.0);
    }

    void eachNotchIsOneRowFromWhereTheViewIsGoing()
    {
        QCOMPARE(wheel(0, 1), 100.0);
        QCOMPARE(wheel(100, 1), 200.0);
        QCOMPARE(wheel(100, 3), 400.0);
        QCOMPARE(wheel(300, -1), 200.0);
        QCOMPARE(wheel(250, 1), 300.0);
        QCOMPARE(wheel(250, -1), 200.0);
    }

    void notchesStopAtTheEnds()
    {
        QCOMPARE(wheel(0, -1), 0.0);
        QCOMPARE(wheel(900, 1), 900.0);
        QCOMPARE(wheel(800, 5), 900.0);
    }
};

QTEST_MAIN(TestFolderGridScroll)
#include "test-folder-grid-scroll.moc"
