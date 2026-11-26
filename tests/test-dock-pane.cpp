#include <QtTest>

#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRadioButton>
#include <QSlider>
#include <QTemporaryDir>

#include "choices.hpp"
#include "dock-pane.hpp"
#include "pane-resets.hpp"

class TestDockPane : public QObject
{
    Q_OBJECT

  private:
    QTemporaryDir folder;
    // a missing control fails the test and hands back a stand-in, so later steps cannot crash
    QRadioButton missing_button;

    void start(bool side_shadow_in_theme = true)
    {
        QFile::remove(folder.filePath(QStringLiteral("choices.json")));
        QJsonObject dock;
        dock[QStringLiteral("position")] = QStringLiteral("bottom");
        dock[QStringLiteral("side_screen")] = QStringLiteral("main");
        dock[QStringLiteral("side_icon_shadow")] = 0.22;
        QJsonObject config;
        config[QStringLiteral("dock")] = dock;
        QJsonObject settings;
        settings[QStringLiteral("config")] = config;
        settings[QStringLiteral("default_config")] = config;
        if (side_shadow_in_theme)
        {
            QJsonObject shadow;
            shadow[QStringLiteral("color")] = QStringLiteral("#000000");
            shadow[QStringLiteral("blur")] = 3;
            QJsonObject side;
            side[QStringLiteral("icon_shadow")] = shadow;
            QJsonObject dock_looks;
            dock_looks[QStringLiteral("side")] = side;
            QJsonObject theme;
            theme[QStringLiteral("dock")] = dock_looks;
            settings[QStringLiteral("theme")] = theme;
        }
        settings[QStringLiteral("user_choices_file")] = folder.filePath(QStringLiteral("choices.json"));
        const QString path = folder.filePath(QStringLiteral("settings.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
        qputenv("DESKTOP_SHELL_SETTINGS", path.toUtf8());
    }

    QRadioButton* button(QWidget& pane, const QString& words)
    {
        for (QRadioButton* found : pane.findChildren<QRadioButton*>())
        {
            if (found->text() == words)
            {
                return found;
            }
        }
        QTest::qFail(qPrintable(QStringLiteral("no button reads \"%1\"").arg(words)), __FILE__, __LINE__);
        return &missing_button;
    }

    QCheckBox missing_check;

    QCheckBox* outermost(QWidget& pane)
    {
        const QString words = QStringLiteral("Put a side dock on the outermost display");
        for (QCheckBox* found : pane.findChildren<QCheckBox*>())
        {
            if (found->text() == words)
            {
                return found;
            }
        }
        QTest::qFail("no check reads the outermost display words", __FILE__, __LINE__);
        return &missing_check;
    }

    QSlider missing_slider;

    QSlider* sideShadow(QWidget& pane)
    {
        auto* found = pane.findChild<QSlider*>(QStringLiteral("side_icon_shadow"));
        if (found == nullptr)
        {
            QTest::qFail("no side icon shadow slider", __FILE__, __LINE__);
            return &missing_slider;
        }
        return found;
    }

  private slots:
    void theDockStartsAtTheBottom()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        QCOMPARE(button(pane, QStringLiteral("Bottom"))->isChecked(), true);
        QCOMPARE(button(pane, QStringLiteral("Left"))->isChecked(), false);
        QCOMPARE(button(pane, QStringLiteral("Right"))->isChecked(), false);
    }

    void choosingASideWritesIt()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        button(pane, QStringLiteral("Left"))->click();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("position")).toString(), QStringLiteral("left"));
        button(pane, QStringLiteral("Right"))->click();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("position")).toString(), QStringLiteral("right"));
    }

    // the dock's own menu changes it too while the pane is open
    void theRowFollowsTheFile()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        Choices other;
        other.set(QStringLiteral("dock"), QStringLiteral("position"), QStringLiteral("right"));
        QTRY_VERIFY_WITH_TIMEOUT(button(pane, QStringLiteral("Right"))->isChecked(), 3000);
    }

    void aSideDockStaysOnTheMainDisplayAtFirst()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        QCOMPARE(outermost(pane)->isChecked(), false);
        QCOMPARE(outermost(pane)->isEnabled(), false);
        button(pane, QStringLiteral("Right"))->click();
        QCOMPARE(outermost(pane)->isEnabled(), true);
        outermost(pane)->click();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("side_screen")).toString(),
            QStringLiteral("outermost"));
        outermost(pane)->click();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("side_screen")).toString(), QStringLiteral("main"));
        button(pane, QStringLiteral("Bottom"))->click();
        QCOMPARE(outermost(pane)->isEnabled(), false);
    }

    void theOutermostCheckFollowsTheFile()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        Choices other;
        other.set(QStringLiteral("dock"), QStringLiteral("side_screen"), QStringLiteral("outermost"));
        QTRY_VERIFY_WITH_TIMEOUT(outermost(pane)->isChecked(), 3000);
    }

    void theSideShadowStartsAtItsDefaultAndOnlyForASide()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        QSlider* slider = sideShadow(pane);
        QCOMPARE(slider->value(), 3);
        QCOMPARE(slider->isEnabled(), false);
        button(pane, QStringLiteral("Left"))->click();
        QCOMPARE(slider->isEnabled(), true);
        button(pane, QStringLiteral("Bottom"))->click();
        QCOMPARE(slider->isEnabled(), false);
    }

    void movingTheSideShadowWritesItsStrength()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        button(pane, QStringLiteral("Right"))->click();
        QSlider* slider = sideShadow(pane);
        slider->setValue(0);
        emit slider->sliderReleased();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("side_icon_shadow")).toDouble(), 0.0);
        slider->setValue(slider->maximum());
        emit slider->sliderReleased();
        QCOMPARE(choices.value(QStringLiteral("dock"), QStringLiteral("side_icon_shadow")).toDouble(), 0.4);
    }

    void aSideDockChosenElsewhereEnablesTheShadow()
    {
        start();
        Choices choices;
        DockPane pane(choices);
        Choices other;
        other.set(QStringLiteral("dock"), QStringLiteral("position"), QStringLiteral("left"));
        QTRY_VERIFY_WITH_TIMEOUT(sideShadow(pane)->isEnabled(), 3000);
    }

    void aThemeWithoutASideShadowHasNoSlider()
    {
        start(false);
        Choices choices;
        DockPane pane(choices);
        pane.show();
        QCOMPARE(sideShadow(pane)->isVisibleTo(&pane), false);
        start();
        Choices themed;
        DockPane themed_pane(themed);
        themed_pane.show();
        QCOMPARE(sideShadow(themed_pane)->isVisibleTo(&themed_pane), true);
    }

    void resettingThePaneTakesItBack()
    {
        QStringList reset;
        for (const Choices::Key& key : pane_choices(QStringLiteral("dock")))
        {
            reset << key.table + QLatin1Char('.') + key.key;
        }
        QVERIFY2(reset.contains(QStringLiteral("dock.position")), qPrintable(reset.join(' ')));
        QVERIFY2(reset.contains(QStringLiteral("dock.side_screen")), qPrintable(reset.join(' ')));
        QVERIFY2(reset.contains(QStringLiteral("dock.side_icon_shadow")), qPrintable(reset.join(' ')));
    }
};

QTEST_MAIN(TestDockPane)
#include "test-dock-pane.moc"
