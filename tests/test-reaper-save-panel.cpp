#include <KFileCustomDialog>
#include <KFileWidget>
#include <KUrlComboBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

#include "save-panel.h"

// template 0xb9 as reaper 7.83 builds it
const QStringList SAVE_PROJECT = {
    "title\tSave Project",
    "filter\tREAPER Project files (*.RPP)\t*.RPP",
    "filter\tEDL TXT (Vegas) files (*.TXT)\t*.TXT",
    "current\t1",
    "control\t1042\tcheck\t8\t2\t209\t19\t1\t0\tCreate subdirectory for project",
    "control\t1043\tcheck\t8\t23\t277\t19\t1\t0\tCopy all media into project directory, using:",
    "control\t1057\tcheck\t291\t23\t112\t19\t0\t0\tConvert media",
    "control\t1056\tbutton\t403\t21\t68\t23\t0\t0\tFormat...",
    "control\t1058\tcheck\t479\t23\t142\t19\t0\t0\tTrim media, tail size:",
    "control\t1577\tedit\t625\t21\t66\t23\t0\t0\t500",
    "control\t1578\tlabel\t695\t25\t19\t15\t0\t0\tms",
    "control\t1044\tcheck\t8\t46\t245\t19\t1\t0\tMove all media into project directory",
    "control\t1045\tcheck\t260\t46\t431\t19\t0\t0\tCopy rather than move source media if not in old project media path",
};

class TestReaperSavePanel : public QObject
{
    Q_OBJECT

    QTemporaryDir folder;
    QStringList sent;

    SavePanel* opened(QStringList lines)
    {
        auto panel = new SavePanel(this);
        connect(panel, &SavePanel::send, this, [this](const QString& line) { sent << line; });
        sent.clear();
        lines.insert(1, "folder\t" + folder.path());
        lines << "show";
        for (const QString& line : lines)
            if (!panel->feed(line))
                qFatal("refused line %s", qPrintable(line));
        return panel;
    }

    template <typename Widget> static Widget* control(SavePanel* panel, const char* id)
    {
        auto widget = panel->options()->findChild<Widget*>(id);
        if (!widget)
            qWarning("no control %s", id);
        return widget;
    }

    // the controls of each row, read from where the layout put them
    static QList<QStringList> rows(SavePanel* panel)
    {
        QList<QStringList> out;
        QLayout* layout = panel->options()->layout();
        for (int row = 0; row < layout->count(); row++)
        {
            QStringList ids;
            QLayout* line = layout->itemAt(row)->layout();
            for (int at = 0; line && at < line->count(); at++)
                if (QWidget* widget = line->itemAt(at)->widget())
                    ids << widget->objectName();
            out << ids;
        }
        return out;
    }

private Q_SLOTS:
    void cleanup() { qDeleteAll(findChildren<SavePanel*>()); }

    void escaping()
    {
        const QString raw = "a\tb\\c\nd\re";
        QCOMPARE(SavePanel::escaped(raw), QString("a\\tb\\\\c\\nd\\re"));
        QCOMPARE(SavePanel::fields("text\t1577\t" + SavePanel::escaped(raw)), QStringList({"text", "1577", raw}));
        QCOMPARE(SavePanel::fields(""), QStringList({""}));
        QCOMPARE(SavePanel::fields("trailing\\"), QStringList({"trailing\\"}));
    }

    void refusesWhatItCannotShow()
    {
        SavePanel panel;
        QVERIFY(!panel.feed("bogus"));
        QVERIFY(!panel.feed("control\t1\tradio\t0\t0\t1\t1\t1\t0\tX"));
        QVERIFY(!panel.feed("control\t1\tcheck\t0\tzero\t1\t1\t1\t0\tX"));
        QVERIFY(!panel.feed("control\t1\tcheck\t0\t0\t1\t1\t1\toff\tX"));
        QVERIFY(panel.feed("control\t1\tcheck\t0\t0\t1\t1\t1\t0\tX"));
        QVERIFY(!panel.feed("control\t1\tcheck\t0\t0\t1\t1\t1\t0\tX"));
        QVERIFY(!panel.feed("item\t1\tWAV"));
        QVERIFY(!panel.feed("item\t9\tWAV"));
        QVERIFY(!panel.feed("state\t1\t1\t1\tX"));
        QVERIFY(!panel.feed("busy"));
        QVERIFY(!panel.feed("current\tfirst"));
        QVERIFY(!panel.feed("title"));
    }

    void buildsReapersRows()
    {
        SavePanel* panel = opened(SAVE_PROJECT);
        QCOMPARE(sent, QStringList({"shown"}));
        QCOMPARE(panel->dialog()->windowTitle(), QString("Save Project"));
        QCOMPARE(panel->dialog()->fileWidget()->operationMode(), KFileWidget::Saving);
        QCOMPARE(panel->dialog()->fileWidget()->currentFilter().label(), QString("EDL TXT (Vegas) files (*.TXT)"));
        QCOMPARE(rows(panel), QList<QStringList>({{"1042"}, {"1043", "1057", "1056", "1058", "1577", "1578"}, {"1044", "1045"}}));
        QVERIFY(control<QCheckBox>(panel, "1043")->isEnabled());
        QVERIFY(!control<QCheckBox>(panel, "1057")->isEnabled());
        QVERIFY(!control<QPushButton>(panel, "1056")->isEnabled());
        QVERIFY(!control<QLineEdit>(panel, "1577")->isEnabled());
        QCOMPARE(control<QLineEdit>(panel, "1577")->text(), QString("500"));
        QCOMPARE(control<QLabel>(panel, "1578")->text(), QString("ms"));
        QCOMPARE(control<QCheckBox>(panel, "1045")->text(),
            QString("Copy rather than move source media if not in old project media path"));
        QVERIFY(panel->dialog()->isVisible());
        QVERIFY(panel->options()->isVisibleTo(panel->dialog()));
    }

    void rowsFollowTheTemplateNotTheOrder()
    {
        SavePanel* panel = opened({"title\tT", "control\t3\tcheck\t200\t40\t50\t19\t1\t0\tC",
            "control\t2\tlabel\t8\t44\t50\t15\t1\t0\t&Tail", "control\t1\tcheck\t8\t2\t50\t19\t1\t0\tA"});
        QCOMPARE(rows(panel), QList<QStringList>({{"1"}, {"2", "3"}}));
        QCOMPARE(control<QLabel>(panel, "2")->text(), QString("Tail"));
    }

    void tellsTheUsersChangesOnly()
    {
        SavePanel* panel = opened(SAVE_PROJECT);
        control<QCheckBox>(panel, "1043")->click();
        QCOMPARE(sent.last(), QString("check\t1043\t1"));
        QVERIFY(panel->feed("state\t1057\t1\t0\tConvert media"));
        QVERIFY(control<QCheckBox>(panel, "1057")->isEnabled());
        QVERIFY(panel->feed("state\t1043\t1\t0\tCopy all media into project directory, using:"));
        QVERIFY(panel->feed("state\t1042\t1\t1\tCreate subdirectory for project"));
        QVERIFY(control<QCheckBox>(panel, "1042")->isChecked());
        QCOMPARE(sent.size(), 2);

        QVERIFY(panel->feed("state\t1577\t1\t0\t500"));
        QLineEdit* edit = control<QLineEdit>(panel, "1577");
        edit->setFocus();
        edit->end(false);
        QTest::keyClicks(edit, "0");
        QCOMPARE(sent.last(), QString("text\t1577\t5000"));
        QVERIFY(panel->feed("state\t1577\t1\t0\t5000"));
        QCOMPARE(sent.size(), 3);
    }

    void aButtonWaitsForReaper()
    {
        SavePanel* panel = opened(SAVE_PROJECT);
        QVERIFY(panel->feed("state\t1056\t1\t0\tFormat..."));
        control<QPushButton>(panel, "1056")->click();
        QCOMPARE(sent.last(), QString("click\t1056"));
        QVERIFY(!panel->dialog()->isEnabled());
        QVERIFY(panel->feed("ready"));
        QVERIFY(panel->dialog()->isEnabled());
        QVERIFY(panel->feed("busy"));
        QVERIFY(!panel->dialog()->isEnabled());
    }

    void listsAndPlainLabels()
    {
        SavePanel* panel = opened({"title\tT", "control\t1049\tlabel\t0\t0\t40\t15\t1\t0\tFormat:",
            "control\t1050\tlist\t44\t0\t80\t21\t1\t1\t", "item\t1050\tWAV", "item\t1050\tFLAC"});
        QComboBox* list = control<QComboBox>(panel, "1050");
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->currentIndex(), 1);
        QVERIFY(panel->feed("state\t1050\t1\t0\t"));
        QCOMPARE(list->currentIndex(), 0);
        QCOMPARE(sent, QStringList({"shown"}));
        emit list->activated(1);
        QCOMPARE(sent.last(), QString("select\t1050\t1"));
    }

    void cancelAnswersOnce()
    {
        SavePanel* panel = opened(SAVE_PROJECT);
        panel->dialog()->reject();
        panel->dialog()->reject();
        QCOMPARE(sent, QStringList({"shown", "cancel"}));
    }

    void acceptGivesThePathAndFilter()
    {
        SavePanel* panel = opened(SAVE_PROJECT);
        QLineEdit* name = panel->dialog()->fileWidget()->locationEdit()->lineEdit();
        name->setFocus();
        QTest::keyClicks(name, "song.TXT");
        panel->dialog()->fileWidget()->okButton()->click();
        QTRY_COMPARE(sent.size(), 2);
        QCOMPARE(sent.last(), "accept\t" + folder.filePath("song.TXT") + "\t*.TXT");
    }

    void anExistingFileIsAskedAboutAndOnlyLocalFilesAreOffered()
    {
        SavePanel* panel = opened(SAVE_PROJECT);
        QCOMPARE(panel->dialog()->fileWidget()->supportedSchemes(), QStringList({"file"}));
        QFile existing(folder.filePath("old.TXT"));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.close();
        QLineEdit* name = panel->dialog()->fileWidget()->locationEdit()->lineEdit();
        name->setFocus();
        QTest::keyClicks(name, "old.TXT");
        bool asked = false;
        QTimer::singleShot(300, this, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets())
                if (auto question = qobject_cast<QDialog*>(widget); question && question != panel->dialog() && question->isVisible())
                {
                    asked = true;
                    question->reject();
                }
        });
        panel->dialog()->fileWidget()->okButton()->click();
        QTRY_VERIFY(asked);
        QTest::qWait(200);
        QVERIFY2(sent == QStringList({"shown"}), qPrintable(sent.join(" | ")));
    }

    void longAndEmptyTextStayInBounds()
    {
        const QString endless(400, QChar('w'));
        SavePanel* panel = opened({"title\tT", "control\t1\tcheck\t8\t2\t50\t19\t1\t0\t" + endless,
            "control\t2\tlabel\t8\t30\t50\t15\t1\t0\t", "control\t3\tbutton\t8\t50\t50\t23\t1\t0\t" + endless});
        const int screen = QGuiApplication::primaryScreen()->availableGeometry().width();
        QCheckBox* box = control<QCheckBox>(panel, "1");
        QVERIFY2(box->sizeHint().width() < screen, qPrintable(QString::number(box->sizeHint().width())));
        QCOMPARE(box->toolTip(), endless);
        QVERIFY(box->text().endsWith(QChar(0x2026)));
        QPushButton* button = control<QPushButton>(panel, "3");
        QVERIFY2(button->sizeHint().width() < screen, qPrintable(QString::number(button->sizeHint().width())));
        QCOMPARE(button->toolTip(), endless);
        QCOMPARE(control<QLabel>(panel, "2")->text(), QString());
        QVERIFY(control<QLabel>(panel, "2")->toolTip().isEmpty());
        QVERIFY(panel->options()->minimumSizeHint().width() <= screen);
        QVERIFY(panel->dialog()->width() >= panel->dialog()->minimumSizeHint().width());
    }
};

int main(int argc, char** argv)
{
    // kio keeps state in the user's folders and on the bus, so neither is real here
    QTemporaryDir home;
    for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
        qputenv(name, (home.path() + "/" + name).toUtf8());
    qputenv("XDG_RUNTIME_DIR", home.path().toUtf8());
    qunsetenv("DBUS_SESSION_BUS_ADDRESS");
    QApplication app(argc, argv);
    TestReaperSavePanel test;
    return QTest::qExec(&test, argc, argv);
}

#include "test-reaper-save-panel.moc"
