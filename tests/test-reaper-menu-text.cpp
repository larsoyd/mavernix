#include <QtTest>

#include "menu-text.hpp"

class TestReaperMenuText : public QObject
{
    Q_OBJECT

    static QStringList keys(const char* text)
    {
        QStringList result;
        for (const std::string& key : menuShortcut(text))
            result << QString::fromStdString(key);
        return result;
    }

  private slots:
    void labels()
    {
        QCOMPARE(menuLabel("&File"), "_File");
        QCOMPARE(menuLabel("Save &as...\tCtrl+Alt+S"), "Save _as...");
        QCOMPARE(menuLabel("Rock && roll"), "Rock & roll");
        QCOMPARE(menuLabel("snake_case"), "snake__case");
        QCOMPARE(menuLabel(""), "");
        QCOMPARE(menuLabel("\tCtrl+N"), "");
    }

    void shortcuts()
    {
        QCOMPARE(keys("New project\tCtrl+N"), QStringList({"Control", "N"}));
        QCOMPARE(keys("Save as\tCtrl+Alt+S"), QStringList({"Control", "Alt", "S"}));
        QCOMPARE(keys("x\tCtrl+Shift+Alt+Space"), QStringList({"Control", "Shift", "Alt", "Space"}));
        QCOMPARE(keys("x\tWin+Z"), QStringList({"Super", "Z"}));
        QCOMPARE(keys("Zoom in\tCtrl++"), QStringList({"Control", "+"}));
        QCOMPARE(keys("Zoom in\t+"), QStringList({"+"}));
        QCOMPARE(keys("x\tNum 5"), QStringList({"Num 5"}));
        QCOMPARE(keys("No shortcut"), QStringList());
        QCOMPARE(keys("Empty\t"), QStringList());
    }

    void statusText()
    {
        QVERIFY(isStatusText("[audio device closed]"));
        QVERIFY(isStatusText("[44.1kHz 24bit WAV : 0/2ch 512spls ~0.0/0.0ms Dummy Audio]"));
        QVERIFY(isStatusText("[MSG] [audio device closed]"));
        QVERIFY(isStatusText("(12.1GB free) [audio device closed]"));
        QVERIFY(!isStatusText(""));
        QVERIFY(!isStatusText("Help"));
        QVERIFY(!isStatusText("&Help"));
        QVERIFY(!isStatusText("7 Day Trial"));
        QVERIFY(!isStatusText("\xc3\x89" "dition"));
    }
};

QTEST_GUILESS_MAIN(TestReaperMenuText)
#include "test-reaper-menu-text.moc"
