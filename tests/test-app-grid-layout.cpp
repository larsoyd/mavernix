#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestAppGridLayout : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    QVariantMap fitted(double width, double height, double dockRoom, int rows = 5, int largest = 128)
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(probe.get(), "fitted", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant(QVariantList{width, height, dockRoom, 15, 7, rows, largest})));
        return called ? result.toMap() : QVariantMap{{QStringLiteral("error"), QStringLiteral("not called")}};
    }

    static int number(const QVariantMap& fit, const char* key)
    {
        return fit.value(QString::fromLatin1(key), -1).toInt();
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/AppGridLayout.js\" as AppGridLayout\n"
                          "QtObject {\n"
                          "  function fitted(a) { return AppGridLayout.fitted(a[0], a[1], a[2], a[3], a[4], a[5], a[6]) }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void aLaptopScreenGetsMidSizedIcons()
    {
        const QVariantMap fit = fitted(1280, 800, 70);
        QCOMPARE(number(fit, "side"), 42);
        QCOMPARE(number(fit, "columnPitch"), 170);
        QCOMPARE(number(fit, "rowPitch"), 126);
        QCOMPARE(number(fit, "iconSize"), 72);
        QCOMPARE(number(fit, "firstIconTop"), 77);
        QCOMPARE(number(fit, "dotsAboveBottom"), 109);
        QCOMPARE(number(fit, "rows"), 5);
    }

    void aWideScreenGetsTheLargestIconsDrawnTogether()
    {
        const QVariantMap fit = fitted(3440, 1440, 70);
        QCOMPARE(number(fit, "iconSize"), 128);
        QCOMPARE(number(fit, "side"), 526);
        QCOMPARE(number(fit, "columnPitch"), 341);
        QCOMPARE(number(fit, "rowPitch"), 254);
        QCOMPARE(number(fit, "firstIconTop"), 145);
        QCOMPARE(number(fit, "dotsAboveBottom"), 108);
    }

    void aScreenWithoutTheDockUsesItsRoom()
    {
        const QVariantMap fit = fitted(2560, 1440, 0);
        QCOMPARE(number(fit, "side"), 392);
        QCOMPARE(number(fit, "columnPitch"), 253);
        QCOMPARE(number(fit, "rowPitch"), 250);
        QCOMPARE(number(fit, "iconSize"), 128);
        QCOMPARE(number(fit, "firstIconTop"), 160);
        QCOMPARE(number(fit, "dotsAboveBottom"), 75);
    }

    void middlingScreensDrawTheColumnsInByLess()
    {
        const QVariantMap full_hd = fitted(1920, 1080, 70);
        QCOMPARE(number(full_hd, "side"), 179);
        QCOMPARE(number(full_hd, "columnPitch"), 223);
        QCOMPARE(number(full_hd, "rowPitch"), 179);
        QCOMPARE(number(full_hd, "iconSize"), 128);
        QCOMPARE(number(full_hd, "firstIconTop"), 70);
        QCOMPARE(number(full_hd, "dotsAboveBottom"), 117);

        const QVariantMap wide_laptop = fitted(1680, 1050, 70);
        QCOMPARE(number(wide_laptop, "side"), 123);
        QCOMPARE(number(wide_laptop, "columnPitch"), 204);
        QCOMPARE(number(wide_laptop, "rowPitch"), 170);
        QCOMPARE(number(wide_laptop, "iconSize"), 120);
        QCOMPARE(number(wide_laptop, "firstIconTop"), 69);

        QCOMPARE(number(fitted(1440, 900, 70), "side"), 48);
    }

    void aScreenTooSmallForAnyRowStillDrawsIcons()
    {
        const QVariantMap fit = fitted(320, 150, 0);
        QCOMPARE(number(fit, "rows"), 1);
        QCOMPARE(number(fit, "iconSize"), 20);
    }

    void aSmallScreenShowsFewerRowsNotCrampedOnes()
    {
        const QVariantMap fit = fitted(640, 360, 40);
        QCOMPARE(number(fit, "rows"), 2);
        QCOMPARE(number(fit, "rowPitch"), 104);
        QCOMPARE(number(fit, "columnPitch"), 85);
        QCOMPARE(number(fit, "iconSize"), 32);
        QCOMPARE(number(fit, "firstIconTop"), 98);
    }

    void theThemeCapsTheIcons()
    {
        QCOMPARE(number(fitted(3440, 1440, 70, 5, 64), "iconSize"), 64);
    }

    void everyRowKeepsToTheScreen()
    {
        const QList<QSizeF> screens = {{3440, 1440}, {2560, 1440}, {1920, 1080}, {1680, 1050}, {1440, 900},
            {1280, 800}, {1280, 720}, {1080, 1920}, {960, 540}, {640, 360}};
        for (const QSizeF& screen : screens) {
            for (const double dockRoom : {0.0, 48.0, 70.0, 110.0}) {
                for (const int rows : {4, 5}) {
                    const QVariantMap fit = fitted(screen.width(), screen.height(), dockRoom, rows);
                    const QByteArray where = QByteArray::number(screen.width()) + "x"
                        + QByteArray::number(screen.height()) + " dock " + QByteArray::number(dockRoom) + " rows "
                        + QByteArray::number(rows);
                    const int icon = number(fit, "iconSize");
                    const int shown = number(fit, "rows");
                    const int last_label_bottom = number(fit, "firstIconTop") + (shown - 1) * number(fit, "rowPitch")
                        + icon + 11 + 15;
                    QVERIFY2(shown >= 1 && shown <= rows, where.constData());
                    QVERIFY2(icon >= 32 && icon <= 128 && icon % 8 == 0, where.constData());
                    QVERIFY2(icon + 11 + 15 <= number(fit, "rowPitch"), where.constData());
                    QVERIFY2(icon <= number(fit, "columnPitch"), where.constData());
                    QVERIFY2(number(fit, "firstIconTop") >= 57, where.constData());
                    QVERIFY2(last_label_bottom <= screen.height() - number(fit, "dotsAboveBottom"), where.constData());
                    QVERIFY2(number(fit, "dotsAboveBottom") >= dockRoom + 30, where.constData());
                    QVERIFY2(number(fit, "side") + 7 * number(fit, "columnPitch") <= screen.width(),
                        where.constData());
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestAppGridLayout)
#include "test-app-grid-layout.moc"
