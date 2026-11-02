#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include "pane-parts.hpp"

static const int pane_width = 628;

// held at the top of a taller page, as the settings window holds a pane
struct Page
{
    QWidget window;
    QVBoxLayout rows{&window};
    QWidget* pane = new QWidget;

    Page()
    {
        rows.setContentsMargins(0, 0, 0, 0);
        rows.addWidget(pane, 0, Qt::AlignTop);
        rows.addStretch();
    }

    void settle()
    {
        window.resize(pane_width, 700);
        window.show();
        QCoreApplication::processEvents();
    }
};

static QLabel* label_with(QWidget& pane, const QString& words)
{
    for (QLabel* label : pane.findChildren<QLabel*>())
    {
        if (label->text() == words)
        {
            return label;
        }
    }
    return nullptr;
}

class TestPaneLayout : public QObject
{
    Q_OBJECT

  private slots:
    void labelsEndJustBeforeTheirControls()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane, 229);
        auto* first = new QComboBox;
        auto* second = new QComboBox;
        form->addRow(QStringLiteral("Size:"), first);
        form->addRow(QStringLiteral("Position on screen:"), second);
        page.settle();

        QCOMPARE(first->x(), 229);
        QCOMPARE(second->x(), 229);
        for (const char* words : {"Size:", "Position on screen:"})
        {
            const QLabel* label = label_with(pane, QLatin1String(words));
            QVERIFY(label != nullptr);
            const int ink_end = label->x() + label->contentsRect().right() + 1;
            QCOMPARE(ink_end, 229 - label_gap_px);
            QVERIFY(label->alignment() & Qt::AlignRight);
        }
    }

    void aLongLabelBreaksRatherThanMovingTheAxis()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane, 120);
        auto* control = new QComboBox;
        form->addRow(QStringLiteral("Move and resize a window by holding this key:"), control);
        page.settle();
        QCOMPARE(control->x(), 120);
    }

    void aLabelSitsLevelWithItsControl()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane);
        auto* tall = new QComboBox;
        tall->setFixedHeight(40);
        form->addRow(QStringLiteral("Tall:"), tall);
        page.settle();
        const QLabel* label = label_with(pane, QStringLiteral("Tall:"));
        const int label_middle = label->y() + label->contentsRect().center().y();
        QVERIFY2(qAbs(label_middle - tall->geometry().center().y()) <= 1,
            qPrintable(QStringLiteral("label=%1 control=%2").arg(label_middle).arg(tall->geometry().center().y())));
    }

    void tickBoxesStandTwentyApartAtTheAxis()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane);
        auto* first = new QCheckBox(QStringLiteral("One"));
        auto* second = new QCheckBox(QStringLiteral("Two"));
        auto* third = new QCheckBox(QStringLiteral("Three"));
        form->addCheck(first);
        form->addCheck(second);
        form->addCheck(third);
        page.settle();
        QCOMPARE(second->y() - first->y(), check_pitch_px);
        QCOMPARE(third->y() - second->y(), check_pitch_px);
        QCOMPARE(first->x(), 229);
    }

    void aTickBoxWithANoteKeepsThePitch()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane);
        auto* first = new QCheckBox(QStringLiteral("One"));
        form->addCheck(with_note(first, QStringLiteral("What it means")));
        page.settle();
        QCOMPARE(first->height(), check_pitch_px);
    }

    void aNoteLeavesRoomBeforeTheNextBox()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane);
        QWidget* noted = with_note(new QCheckBox(QStringLiteral("One")), QStringLiteral("What it means"));
        auto* next = new QCheckBox(QStringLiteral("Two"));
        form->addCheck(noted);
        form->addCheck(next);
        page.settle();
        QCOMPARE(next->y() - noted->geometry().bottom() - 1, row_gap_px);
    }

    void controlsGetRoomBetweenThem()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane);
        auto* first = new QComboBox;
        auto* second = new QComboBox;
        form->addRow(QStringLiteral("A:"), first);
        form->addRow(QStringLiteral("B:"), second);
        page.settle();
        QCOMPARE(second->y() - first->geometry().bottom() - 1, row_gap_px);
    }

    void aDividerCrossesThePaneWithRoomAround()
    {
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane);
        auto* above = new QCheckBox(QStringLiteral("Above"));
        auto* below = new QCheckBox(QStringLiteral("Below"));
        form->addCheck(above);
        form->addDivider();
        form->addCheck(below);
        page.settle();

        auto* line = pane.findChild<QFrame*>(QStringLiteral("divider"));
        QVERIFY(line != nullptr);
        const QPoint at = line->mapTo(&pane, QPoint(0, 0));
        QCOMPARE(at.x(), divider_inset_px);
        QCOMPARE(line->width(), pane_width - 2 * divider_inset_px);
        QCOMPARE(line->height(), 2);
        QVERIFY(at.y() - above->geometry().bottom() >= divider_room_px);
        QVERIFY(below->y() - (at.y() + line->height()) >= divider_room_px);
    }

    void endWordsStayInsideTheirSlider_data()
    {
        QTest::addColumn<bool>("fixed");
        QTest::newRow("fixed width") << true;
        QTest::newRow("minimum width") << false;
    }

    void endWordsStayInsideTheirSlider()
    {
        QFETCH(bool, fixed);
        Page page;
        QWidget& pane = *page.pane;
        auto* form = new PaneForm(&pane);
        auto* slider = new QSlider(Qt::Horizontal);
        if (fixed)
        {
            slider->setFixedWidth(200);
        }
        else
        {
            slider->setMinimumWidth(200);
        }
        form->addRow(QStringLiteral("Speed:"), with_end_words(slider, QStringLiteral("Slow"), QStringLiteral("Fast")));
        page.settle();

        QCOMPARE(slider->width(), 200);
        const QLabel* high = label_with(pane, QStringLiteral("Fast"));
        QCOMPARE(high->mapTo(&pane, QPoint(high->width(), 0)).x(), slider->mapTo(&pane, QPoint(200, 0)).x());
    }
};

QTEST_MAIN(TestPaneLayout)
#include "test-pane-layout.moc"
