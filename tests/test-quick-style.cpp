// Qt Quick takes the widget style's name as its own style, so the session's
// style has to exist as a QML module too

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QtTest>

class TestQuickStyle : public QObject
{
    Q_OBJECT

  private slots:
    void controlsLoadUnderTheSessionStyle()
    {
        QTemporaryDir imports;
        QVERIFY(QDir(imports.path()).mkpath("org/kde/desktop"));
        QFile forwarded(imports.filePath("org/kde/desktop/qmldir"));
        QVERIFY(forwarded.open(QIODevice::WriteOnly));
        forwarded.write("module org.kde.desktop\n");
        forwarded.close();

        QQuickStyle::setStyle(QStringLiteral("kvantum"));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QUICK_STYLE_DIR));
        engine.addImportPath(imports.path());
        QQmlComponent button(&engine);
        button.setData("import QtQuick.Controls\nButton {}\n", QUrl());
        QVERIFY2(button.isReady(), qPrintable(button.errorString()));
    }
};

QTEST_MAIN(TestQuickStyle)
#include "test-quick-style.moc"
