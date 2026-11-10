#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <sys/stat.h>

#include "emoji-set.hpp"

namespace
{
const char* const LIST = "# a note\n"
                         "@smileys\n"
                         "😀\tgrinning face\tgrin happy smile\t\n"
                         "😂\tface with tears of joy\tlol laugh happy\t\n"
                         "🙂\tslightly smiling face\tsmile\t\n"
                         "😉\twinking face\tflirt\t\n"
                         "😍\tsmiling face with heart-eyes\tlove\t\n"
                         "🤔\tthinking face\thmm\t\n"
                         "@people\n"
                         "👋\twaving hand\thello bye\t👋🏻 👋🏿\n"
                         "👍\tthumbs up\tgood yes like\t👍🏻 👍🏿\n"
                         "@flags\n"
                         "🇳🇴\tNorway\tflag\t\n";

QStringList texts(const EmojiSet& set)
{
    QStringList out;
    for (int index = 0; index < set.count(); ++index)
        out << set.textAt(index);
    return out;
}

// one string per row: a group name in brackets or the row's emoji
QStringList drawn(const EmojiSet& set)
{
    QStringList out;
    for (const QVariant& row : set.rows())
    {
        const QVariantMap map = row.toMap();
        if (map.contains("group"))
        {
            out << "[" + map["group"].toString() + "]";
            continue;
        }
        QString line;
        for (const QVariant& cell : map["cells"].toList())
            line += cell.toMap()["text"].toString();
        out << line;
    }
    return out;
}
} // namespace

class EmojiSetTest : public QObject
{
    Q_OBJECT

    QTemporaryDir home;
    QString listFile;
    int folders = 0;

    QString freshFolder() { return home.filePath(QString("state-%1").arg(++folders)); }

    void prepare(EmojiSet& set, const QString& folder, int columns = 4)
    {
        set.drawable = nullptr;
        set.setDataFile(listFile);
        set.setFolder(folder);
        set.setColumns(columns);
    }

  private slots:
    void initTestCase()
    {
        QVERIFY(home.isValid());
        listFile = home.filePath("emoji.tsv");
        QFile file(listFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(LIST);
    }

    void groupsComeInFileOrderUnderTheirNames()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        QCOMPARE(set.count(), 9);
        QCOMPARE(drawn(set), QStringList({"[smileys]", "😀😂🙂😉", "😍🤔", "[people]", "👋👍", "[flags]", "🇳🇴"}));
    }

    void columnsDecideTheRows()
    {
        EmojiSet set;
        prepare(set, freshFolder(), 3);
        QCOMPARE(drawn(set).mid(0, 3), QStringList({"[smileys]", "😀😂🙂", "😉😍🤔"}));
        set.setColumns(5);
        QCOMPARE(drawn(set).mid(0, 3), QStringList({"[smileys]", "😀😂🙂😉😍", "🤔"}));
    }

    void rowsKnowTheirFirstIndex()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        const QVariantList rows = set.rows();
        QCOMPARE(rows[1].toMap()["first"].toInt(), 0);
        QCOMPARE(rows[2].toMap()["first"].toInt(), 4);
        QCOMPARE(rows[4].toMap()["first"].toInt(), 6);
        QCOMPARE(set.rowOf(5), 2);
        QCOMPARE(set.rowOf(8), 6);
        QCOMPARE(set.rowOf(40), -1);
    }

    void sectionsNameTheRowEachGroupStartsAt()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        const QVariantList sections = set.sections();
        QCOMPARE(sections.size(), 3);
        QCOMPARE(sections[1].toMap()["group"].toString(), QString("people"));
        QCOMPARE(sections[1].toMap()["row"].toInt(), 3);
        QCOMPARE(sections[2].toMap()["row"].toInt(), 5);
    }

    void searchMatchesEveryWordByItsStart()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        set.setFilter("  SMI fa ");
        QCOMPARE(drawn(set), QStringList({"[found]", "🙂😍😀"}));
        set.setFilter("ace");
        QCOMPARE(set.count(), 0);
        QVERIFY(set.rows().isEmpty());
    }

    void searchPutsNamesBeforeSearchWords()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        set.setFilter("smil");
        QCOMPARE(texts(set), QStringList({"🙂", "😍", "😀"}));
        set.setFilter("face");
        QCOMPARE(texts(set), QStringList({"😀", "😂", "🙂", "😉", "😍", "🤔"}));
        set.setFilter("");
        QCOMPARE(set.count(), 9);
    }

    void namesAreReadByIndex()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        QCOMPARE(set.nameAt(6), QString("waving hand"));
        QCOMPARE(set.nameAt(-1), QString());
        QCOMPARE(set.textAt(99), QString());
    }

    void keysMoveAcrossAndStopAtTheEnds()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        QCOMPARE(set.moved(0, 1, 0), 1);
        QCOMPARE(set.moved(3, 1, 0), 4);
        QCOMPARE(set.moved(0, -1, 0), 0);
        QCOMPARE(set.moved(8, 1, 0), 8);
        QCOMPARE(set.moved(-1, 1, 0), 0);
        QCOMPARE(set.moved(-1, 0, 1), 0);
    }

    void keysMoveDownInTheSameColumnPastGroupNames()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        QCOMPARE(set.moved(1, 0, 1), 5);
        // a shorter row takes its last one
        QCOMPARE(set.moved(3, 0, 1), 5);
        QCOMPARE(set.moved(5, 0, 1), 7);
        QCOMPARE(set.moved(7, 0, 1), 8);
        QCOMPARE(set.moved(8, 0, 1), 8);
        QCOMPARE(set.moved(8, 0, -1), 6);
        QCOMPARE(set.moved(7, 0, -2), 1);
        QCOMPARE(set.moved(1, 0, -1), 1);
        QCOMPARE(set.moved(0, 0, 9), 8);
    }

    void usedEmojiLeadTheListNewestFirst()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        set.used("👍");
        set.used("😂");
        set.used("👍");
        QCOMPARE(drawn(set).mid(0, 3), QStringList({"[recent]", "👍😂", "[smileys]"}));
        QCOMPARE(set.count(), 11);
        QCOMPARE(set.nameAt(0), QString("thumbs up"));
    }

    void recentOnesAreCapped()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        set.setKeepRecent(2);
        for (const char* text : {"😀", "😂", "🙂"})
            set.used(text);
        QCOMPARE(drawn(set).at(1), QString("🙂😂"));
    }

    void aLowerCapHidesOlderOnesAndAHigherOneBringsNoneBack()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        for (const char* text : {"😀", "😂", "🙂"})
            set.used(text);
        set.setKeepRecent(1);
        QCOMPARE(drawn(set).at(1), QString("🙂"));
        set.used("😉");
        set.setKeepRecent(5);
        QCOMPARE(drawn(set).at(1), QString("😉"));
    }

    void noColumnsCountsAsOne()
    {
        EmojiSet set;
        prepare(set, freshFolder(), 0);
        QCOMPARE(set.columns(), 1);
        QCOMPARE(drawn(set).mid(0, 3), QStringList({"[smileys]", "😀", "😂"}));
    }

    void recentOnesSurviveARestartInAPrivateFile()
    {
        const QString folder = freshFolder();
        {
            EmojiSet set;
            prepare(set, folder);
            set.used("🇳🇴");
        }
        EmojiSet again;
        prepare(again, folder);
        QCOMPARE(drawn(again).mid(0, 2), QStringList({"[recent]", "🇳🇴"}));
        struct stat info {};
        QCOMPARE(::stat(qPrintable(folder + "/recent.json"), &info), 0);
        QCOMPARE(int(info.st_mode & 0777), 0600);
    }

    void aRecentOneTheListLostIsNotShown()
    {
        const QString folder = freshFolder();
        {
            EmojiSet set;
            prepare(set, folder);
            set.used("😉");
            set.used("🦖");
        }
        EmojiSet again;
        prepare(again, folder);
        again.drawable = [](const QString& text) { return text != "😉"; };
        QVERIFY(!drawn(again).contains("[recent]"));
    }

    void clearingForgetsRecentOnes()
    {
        const QString folder = freshFolder();
        EmojiSet set;
        prepare(set, folder);
        set.used("😀");
        set.clearRecent();
        QCOMPARE(drawn(set).at(0), QString("[smileys]"));
        EmojiSet again;
        prepare(again, folder);
        QCOMPARE(drawn(again).at(0), QString("[smileys]"));
    }

    void tonesAreOfferedWithTheBaseFirst()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        QCOMPARE(set.tonesAt(6), QStringList({"👋", "👋🏻", "👋🏿"}));
        QVERIFY(set.tonesAt(0).isEmpty());
        const QVariantMap wave = set.rows()[4].toMap()["cells"].toList()[0].toMap();
        QCOMPARE(wave["toned"].toBool(), true);
        const QVariantMap grin = set.rows()[1].toMap()["cells"].toList()[0].toMap();
        QCOMPARE(grin["toned"].toBool(), false);
    }

    void aChosenToneReplacesItsBaseUntilTheBaseIsChosen()
    {
        const QString folder = freshFolder();
        {
            EmojiSet set;
            prepare(set, folder);
            set.used("👋🏿");
            QCOMPARE(drawn(set).mid(0, 2), QStringList({"[recent]", "👋🏿"}));
            QCOMPARE(drawn(set).at(6), QString("👋🏿👍"));
            QCOMPARE(set.nameAt(0), QString("waving hand"));
            QCOMPARE(set.tonesAt(0), QStringList({"👋", "👋🏻", "👋🏿"}));
        }
        EmojiSet again;
        prepare(again, folder);
        QCOMPARE(drawn(again).at(6), QString("👋🏿👍"));
        again.used("👋");
        QCOMPARE(drawn(again).at(6), QString("👋👍"));
        QCOMPARE(drawn(again).at(1), QString("👋"));
    }

    void searchShowsTheChosenTone()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        set.used("👍🏻");
        set.setFilter("thumbs");
        QCOMPARE(texts(set), QStringList({"👍🏻"}));
    }

    void whatTheFontCannotDrawIsLeftOut()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        set.drawable = [](const QString& text) { return text != "😂" && text != "🇳🇴"; };
        set.setColumns(6);
        QCOMPARE(drawn(set), QStringList({"[smileys]", "😀🙂😉😍🤔", "[people]", "👋👍"}));
    }

    void aMissingListShowsNothing()
    {
        EmojiSet set;
        set.drawable = nullptr;
        set.setFolder(freshFolder());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[emoji\\] load refused reason=unreadable.*"));
        set.setDataFile(home.filePath("none.tsv"));
        QCOMPARE(set.count(), 0);
        QVERIFY(set.rows().isEmpty());
        QCOMPARE(set.moved(-1, 1, 0), -1);
    }

    void brokenLinesAreSkipped()
    {
        const QString path = home.filePath("broken.tsv");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("😀\tbefore any group\t\t\n@smileys\nno tabs here\n🙂\tslightly smiling face\tsmile\t\n\n");
        file.close();
        EmojiSet set;
        set.drawable = nullptr;
        set.setFolder(freshFolder());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("\\[emoji\\] load skipped=2 .*"));
        set.setDataFile(path);
        QCOMPARE(texts(set), QStringList({"🙂"}));
    }

    void aKeycapLineIsNotTakenForANote()
    {
        const QString path = home.filePath("keycap.tsv");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# a note\n@symbols\n#️⃣\tkeycap: #\t\t\n# another note\n*️⃣\tkeycap: *\t\t\n");
        file.close();
        EmojiSet set;
        set.drawable = nullptr;
        set.setFolder(freshFolder());
        set.setDataFile(path);
        QCOMPARE(texts(set), QStringList({"#️⃣", "*️⃣"}));
        set.setFilter("#");
        QCOMPARE(texts(set), QStringList({"#️⃣"}));
    }

    void shownChangesAreAnnounced()
    {
        EmojiSet set;
        prepare(set, freshFolder());
        QSignalSpy spy(&set, &EmojiSet::shownChanged);
        set.setFilter("face");
        set.setFilter("face");
        set.used("😀");
        set.setColumns(4);
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(EmojiSetTest)
#include "test-emoji-set.moc"
