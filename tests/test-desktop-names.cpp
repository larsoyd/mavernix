#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

// monospaced letters 10 px apart in a room of 105 px, so a line holds 10 letters
class TestDesktopNames : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> probe;

    QStringList lines(const QString& name, int most = 2)
    {
        probe->setProperty("most", most);
        probe->setProperty("name", name);
        return probe->property("lines").toStringList();
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQuick\n"
                          "import \"file://" SHELL_DIR "\"\n"
                          "Item {\n"
                          "  property string name\n"
                          "  property int most: 2\n"
                          "  readonly property alias lines: split.lines\n"
                          "  FontMetrics { id: metrics; font.family: \"DejaVu Sans Mono\"; font.pixelSize: 16 }\n"
                          "  DesktopNameLines {\n"
                          "    id: split\n"
                          "    text: parent.name\n"
                          "    most: parent.most\n"
                          "    room: 10.5 * metrics.advanceWidth(\"0\")\n"
                          "    font: metrics.font\n"
                          "  }\n"
                          "}\n",
            QUrl());
        probe.reset(component.create());
        QVERIFY2(probe != nullptr, qPrintable(component.errorString()));
    }

    void aNameThatFitsStaysOnOneLine()
    {
        QCOMPARE(lines(QStringLiteral("Note.txt")), QStringList{QStringLiteral("Note.txt")});
    }

    void aNameFillingTheRoomExactlyStaysOnOneLine()
    {
        QCOMPARE(lines(QStringLiteral("abcdefghij")), QStringList{QStringLiteral("abcdefghij")});
    }

    void aLongNameBreaksAfterTheLastWordThatFits()
    {
        QCOMPARE(lines(QStringLiteral("A very long document")),
            (QStringList{QStringLiteral("A very"), QStringLiteral("long document")}));
    }

    void oneLongWordBreaksBetweenLetters()
    {
        QCOMPARE(lines(QStringLiteral("abcdefghijklmnopqrstuvwxyz")),
            (QStringList{QStringLiteral("abcdefghij"), QStringLiteral("klmnopqrstuvwxyz")}));
    }

    void aFirstWordTooLongBreaksBetweenLettersAndKeepsTheRest()
    {
        QCOMPARE(lines(QStringLiteral("abcdefghijklmno pq")),
            (QStringList{QStringLiteral("abcdefghij"), QStringLiteral("klmno pq")}));
    }

    void theSecondLineKeepsEverythingLeftForItsMiddleCut()
    {
        const QString name = QString(400, QLatin1Char('w'));
        const QStringList got = lines(name);
        QCOMPARE(got.size(), 2);
        QCOMPARE(got[0], QString(10, QLatin1Char('w')));
        QCOMPARE(got[1], QString(390, QLatin1Char('w')));
    }

    void oneLineAllowedKeepsTheWholeName()
    {
        QCOMPARE(lines(QStringLiteral("A very long document"), 1), QStringList{QStringLiteral("A very long document")});
    }

    void anEmptyNameIsOneEmptyLine()
    {
        QCOMPARE(lines(QString()), QStringList{QString()});
    }

    void oneLetterIsOneLine()
    {
        QCOMPARE(lines(QStringLiteral("x")), QStringList{QStringLiteral("x")});
    }

    void spacesAloneStayOneLine()
    {
        QCOMPARE(lines(QStringLiteral("   ")).size(), 1);
    }

    void lineBreaksInANameNeverMakeAThirdLine()
    {
        QCOMPARE(lines(QStringLiteral("a\nb\nc")), (QStringList{QStringLiteral("a"), QStringLiteral("b c")}));
    }

    void aLineBreakInAShortNameDoesNotStackItOnOneLine()
    {
        QCOMPARE(lines(QStringLiteral("a\nb"), 1), QStringList{QStringLiteral("a b")});
    }
};

QTEST_MAIN(TestDesktopNames)
#include "test-desktop-names.moc"
