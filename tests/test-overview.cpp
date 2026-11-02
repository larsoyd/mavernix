#include <QtTest>

#include <QAbstractButton>

#include "overview.hpp"

static QVariantMap looks()
{
    return {{QStringLiteral("columns"), 8}, {QStringLiteral("cell_pitch"), 82},
        {QStringLiteral("first_cell_middle"), 47}, {QStringLiteral("line_pitch"), 92},
        {QStringLiteral("group_bottom"), 19}, {QStringLiteral("stripe"), QStringLiteral("#ededed")},
        {QStringLiteral("stripe_alternate"), QStringLiteral("#e5e5e5")}};
}

static QList<QList<Overview::Entry>> groups()
{
    QList<QList<Overview::Entry>> all(2);
    for (const QString& name : {QStringLiteral("Sound"), QStringLiteral("Mouse"), QStringLiteral("Keyboard")})
    {
        all[0].append({name.toLower(), name, QString(), QString()});
    }
    for (int number = 0; number < 8; number++)
    {
        const QString name = QStringLiteral("Other %1").arg(number);
        all[1].append({name, name, QString(), QString()});
    }
    return all;
}

static QAbstractButton* cell(Overview& overview, const QString& name)
{
    for (QAbstractButton* button : overview.findChildren<QAbstractButton*>())
    {
        if (button->accessibleName() == name)
        {
            return button;
        }
    }
    return nullptr;
}

static int brightness_of(Overview& overview, const QString& name)
{
    const QImage picture = overview.grab().toImage();
    const QRect place = cell(overview, name)->geometry();
    // above the icon, inside the spotlight, where only the stripe is
    return picture.pixelColor(place.center().x(), place.top() + 10).lightness();
}

class TestOverview : public QObject
{
    Q_OBJECT

  private slots:
    void categoriesKeepTheirGroupsInStripes()
    {
        Overview overview(groups(), looks());
        QCOMPARE(cell(overview, QStringLiteral("Sound"))->x(), 6);
        QCOMPARE(cell(overview, QStringLiteral("Mouse"))->x(), 88);
        QVERIFY(cell(overview, QStringLiteral("Other 0"))->y() > cell(overview, QStringLiteral("Keyboard"))->y());
    }

    void alphabeticalOrderIsOneRunOfNames()
    {
        Overview overview(groups(), looks());
        overview.setAlphabetical(true);
        QCOMPARE(cell(overview, QStringLiteral("Keyboard"))->pos(), QPoint(6, 0));
        QCOMPARE(cell(overview, QStringLiteral("Mouse"))->pos(), QPoint(88, 0));
        // Sound is the eleventh name, third on the second row, in rows 12 tighter than in categories
        QCOMPARE(cell(overview, QStringLiteral("Sound"))->pos(), QPoint(6 + 2 * 82, 92 - 12));
    }

    void organisingAgainGivesTheSameLayout()
    {
        Overview overview(groups(), looks());
        const QSize categories = overview.sizeHint();
        const QPoint sound = cell(overview, QStringLiteral("Sound"))->pos();
        overview.setAlphabetical(true);
        QVERIFY(overview.sizeHint().height() != categories.height());
        overview.setAlphabetical(false);
        QCOMPARE(overview.sizeHint(), categories);
        QCOMPARE(cell(overview, QStringLiteral("Sound"))->pos(), sound);
    }

    void searchDimsAllButWhatItFinds()
    {
        Overview overview(groups(), looks());
        overview.resize(overview.sizeHint());
        overview.show();
        const int before = brightness_of(overview, QStringLiteral("Mouse"));
        overview.mark(QStringLiteral("mo"));
        QTRY_VERIFY(brightness_of(overview, QStringLiteral("Keyboard")) < before - 40);
        // brightest on the first find, which Return opens
        QVERIFY(brightness_of(overview, QStringLiteral("Mouse")) >= before - 2);
    }

    void aSecondFindIsDimButNotDark()
    {
        Overview overview(groups(), looks());
        overview.resize(overview.sizeHint());
        overview.show();
        overview.mark(QStringLiteral("ou"));
        QTRY_VERIFY(brightness_of(overview, QStringLiteral("Keyboard")) < 185);
        const int first = brightness_of(overview, QStringLiteral("Sound"));
        const int second = brightness_of(overview, QStringLiteral("Mouse"));
        const int left_out = brightness_of(overview, QStringLiteral("Keyboard"));
        QVERIFY2(first > second, qPrintable(QStringLiteral("%1 %2").arg(first).arg(second)));
        QVERIFY2(second > left_out, qPrintable(QStringLiteral("%1 %2").arg(second).arg(left_out)));
    }

    void clearingTheSearchTakesTheDimmingAway()
    {
        Overview overview(groups(), looks());
        overview.resize(overview.sizeHint());
        overview.show();
        const int before = brightness_of(overview, QStringLiteral("Keyboard"));
        overview.mark(QStringLiteral("mo"));
        QTRY_VERIFY(brightness_of(overview, QStringLiteral("Keyboard")) < before - 40);
        overview.mark(QString());
        QTRY_COMPARE(brightness_of(overview, QStringLiteral("Keyboard")), before);
    }

    void cellsStillTakeClicksWhileDimmed()
    {
        Overview overview(groups(), looks());
        overview.resize(overview.sizeHint());
        overview.show();
        overview.mark(QStringLiteral("mo"));
        QTRY_VERIFY(brightness_of(overview, QStringLiteral("Keyboard")) < 200);
        QAbstractButton* keyboard = cell(overview, QStringLiteral("Keyboard"));
        QCOMPARE(overview.childAt(keyboard->geometry().center()), keyboard);
    }
};

QTEST_MAIN(TestOverview)
#include "test-overview.moc"
