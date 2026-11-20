#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtTest>

#include <memory>

// the band fades over 400 ms here, long enough to sample twice
class TestDesktopBand : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    QQuickItem* band() { return probe->findChild<QQuickItem*>(QStringLiteral("band")); }

    void drag(double x, double y, double width, double height)
    {
        probe->setProperty("span", QRectF(x, y, width, height));
        probe->setProperty("dragging", true);
    }

    void release() { probe->setProperty("dragging", false); }

    bool make(const char* look)
    {
        QQmlComponent component(&engine);
        component.setData(QByteArray("import QtQuick\n"
                                     "import \"file://" SHELL_DIR "\"\n"
                                     "Item {\n"
                                     "  property bool dragging: false\n"
                                     "  property rect span: Qt.rect(0, 0, 0, 0)\n"
                                     "  DesktopBand {\n"
                                     "    objectName: \"band\"\n"
                                     "    dragging: parent.dragging\n"
                                     "    span: parent.span\n"
                                     "    look: (")
                              + look + ")\n  }\n}\n",
            QUrl());
        probe.reset(component.create());
        if (probe == nullptr)
        {
            QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__);
            return false;
        }
        return band() != nullptr;
    }

  private slots:
    void cleanup() { probe.reset(); }

    void aDraggedBandShowsAtOnceAndWhole()
    {
        QVERIFY(make("{ fade_ms: 400 }"));
        drag(10, 20, 300, 200);
        QVERIFY(band()->isVisible());
        QCOMPARE(band()->opacity(), 1.0);
        QCOMPARE(QRectF(band()->x(), band()->y(), band()->width(), band()->height()), QRectF(10, 20, 300, 200));
    }

    void aBandWithNoSizeShowsNothing()
    {
        QVERIFY(make("{ fade_ms: 400 }"));
        drag(10, 20, 0, 0);
        QVERIFY(!band()->isVisible());
    }

    void aReleasedBandStaysInPlaceAndFadesEvenly()
    {
        QVERIFY(make("{ fade_ms: 400 }"));
        drag(10, 20, 300, 200);
        QElapsedTimer since_release;
        since_release.start();
        release();
        for (int wait_ms : {100, 200})
        {
            QTest::qWait(wait_ms);
            const double even = 1.0 - since_release.elapsed() / 400.0;
            QVERIFY(band()->isVisible());
            QCOMPARE(QRectF(band()->x(), band()->y(), band()->width(), band()->height()), QRectF(10, 20, 300, 200));
            QVERIFY2(qAbs(band()->opacity() - even) < 0.08,
                qPrintable(QStringLiteral("opacity=%1 even=%2").arg(band()->opacity()).arg(even)));
        }
        QTRY_VERIFY_WITH_TIMEOUT(!band()->isVisible(), 1000);
    }

    void aPressDuringTheFadeStartsAFreshBand()
    {
        QVERIFY(make("{ fade_ms: 400 }"));
        drag(10, 20, 300, 200);
        release();
        QTest::qWait(150);
        QVERIFY(band()->opacity() < 0.9);
        drag(50, 60, 0, 0);
        QVERIFY(!band()->isVisible());
        drag(50, 60, 40, 30);
        QVERIFY(band()->isVisible());
        QCOMPARE(band()->opacity(), 1.0);
        QTest::qWait(450);
        QVERIFY(band()->isVisible());
        QCOMPARE(band()->opacity(), 1.0);
    }

    void aThemeWithoutAFadeDropsTheBandOnRelease()
    {
        QVERIFY(make("{}"));
        drag(10, 20, 300, 200);
        QVERIFY(band()->isVisible());
        release();
        QVERIFY(!band()->isVisible());
    }

    void aThemeWithoutABandTableStillDrawsOne()
    {
        QVERIFY(make("undefined"));
        drag(10, 20, 300, 200);
        QVERIFY(band()->isVisible());
        release();
        QVERIFY(!band()->isVisible());
    }
};

QTEST_MAIN(TestDesktopBand)
#include "test-desktop-band.moc"
