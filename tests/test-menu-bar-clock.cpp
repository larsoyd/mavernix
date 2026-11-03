#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestMenuBarClock : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> clock;

    // 13 january 2026, a tuesday, at 13:05:07
    QString text(const QVariantMap& choices, const QString& time_locale, const QString& words_locale, int day = 13,
        int month = 1)
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(clock.get(), "text", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, day), Q_ARG(QVariant, month), Q_ARG(QVariant, choices), Q_ARG(QVariant, time_locale),
            Q_ARG(QVariant, words_locale));
        return called ? result.toString() : QStringLiteral("[text not called]");
    }

    QVariantMap placed(const QStringList& left, const QStringList& right, const QString& position)
    {
        QVariant result;
        QMetaObject::invokeMethod(clock.get(), "placed", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, left),
            Q_ARG(QVariant, right), Q_ARG(QVariant, position));
        return result.toMap();
    }

    static QVariantMap with(const QString& format, bool time, bool date, const QString& style = QString(),
        bool region_time = false)
    {
        return {{QStringLiteral("format"), format}, {QStringLiteral("showTime"), time},
            {QStringLiteral("showDate"), date}, {QStringLiteral("dateStyle"), style},
            {QStringLiteral("regionTime"), region_time}};
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/MenuBarClock.js\" as MenuBarClock\n"
                          "QtObject {\n"
                          "  function text(day, month, choices, timeName, wordsName) {\n"
                          "    return MenuBarClock.text(new Date(2026, month - 1, day, 13, 5, 7), choices,\n"
                          "                             Qt.locale(timeName), Qt.locale(wordsName))\n"
                          "  }\n"
                          "  function placed(left, right, position) { return MenuBarClock.placed(left, right, position) }\n"
                          "}\n",
            QUrl());
        clock.reset(component.create());
        QVERIFY2(clock != nullptr, qPrintable(component.errorString()));
    }

    void timeAloneIsTheFormatAsBefore()
    {
        QCOMPARE(text(with("HH:mm", true, false), "nb_NO", "en_US"), QStringLiteral("13:05"));
        QCOMPARE(text(with("ddd h:mm AP", true, false), "nb_NO", "nb_NO"), QStringLiteral("Tue 1:05 PM"));
    }

    void fullDateIsWrittenOutWithAnEnglishOrdinal()
    {
        QCOMPARE(text(with("HH:mm", true, true, "full"), "nb_NO", "en_US"), QStringLiteral("January 13th 13:05"));
        const QList<QPair<int, QString>> days = {{1, "1st"}, {2, "2nd"}, {3, "3rd"}, {4, "4th"}, {11, "11th"},
            {12, "12th"}, {21, "21st"}, {22, "22nd"}, {23, "23rd"}, {31, "31st"}};
        for (const auto& [day, ordinal] : days)
        {
            QCOMPARE(text(with("HH:mm", false, true, "full"), "nb_NO", "en_US", day), "January " + ordinal);
        }
    }

    void shortDateCutsTheMonthWithAPointUnlessItIsWhole()
    {
        QCOMPARE(text(with("HH:mm", true, true, "short"), "nb_NO", "en_US"), QStringLiteral("Jan. 13th 13:05"));
        QCOMPARE(text(with("HH:mm", false, true, "short"), "nb_NO", "en_US", 13, 5), QStringLiteral("May 13th"));
    }

    void otherLanguagesTakeTheirOwnDayAndMonthWithoutOrdinals()
    {
        QCOMPARE(text(with("HH:mm", false, true, "full"), "nb_NO", "nb_NO"), QStringLiteral("13. januar"));
        QCOMPARE(text(with("HH:mm", false, true, "full"), "de_DE", "de_DE"), QStringLiteral("13. Januar"));
        QCOMPARE(text(with("HH:mm", false, true, "full"), "es_ES", "es_ES"), QStringLiteral("13 de enero"));
        QCOMPARE(text(with("HH:mm", false, true, "short"), "nb_NO", "nb_NO"), QStringLiteral("13. jan."));
    }

    void numbersKeepTheRegionsOrderWithAFullYear()
    {
        QCOMPARE(text(with("HH:mm", true, true, "numeric"), "nb_NO", "en_US"), QStringLiteral("13/01/2026 13:05"));
        QCOMPARE(text(with("HH:mm", false, true, "numeric"), "en_US", "en_US"), QStringLiteral("01/13/2026"));
        QCOMPARE(text(with("HH:mm", false, true, "numeric"), "ja_JP", "en_US"), QStringLiteral("2026/01/13"));
        QCOMPARE(text(with("HH:mm", false, true, "numeric"), "en_US", "en_US", 3), QStringLiteral("01/03/2026"));
    }

    void theRegionWritesItsOwnDateAndTime()
    {
        QCOMPARE(text(with("HH:mm", true, true, "region"), "nb_NO", "en_US"), QStringLiteral("13.01.2026 13:05"));
        QCOMPARE(text(with("HH:mm", true, false, QString(), true), "en_US", "en_US"), QStringLiteral(u"1:05\u202FPM"));
        QCOMPARE(text(with("HH:mm:ss", true, false, QString(), true), "nb_NO", "en_US"), QStringLiteral("13:05:07"));
        QCOMPARE(text(with("h:mm AP", true, true, "region", true), "nb_NO", "en_US"),
            QStringLiteral("13.01.2026 13:05"));
    }

    void theDayOfTheWeekLeadsInTheDatesLanguage()
    {
        QCOMPARE(text(with("ddd HH:mm", false, true, "full"), "nb_NO", "en_US"), QStringLiteral("Tue January 13th"));
        QCOMPARE(text(with("ddd HH:mm", true, true, "short"), "nb_NO", "en_US"), QStringLiteral("Tue Jan. 13th 13:05"));
        QCOMPARE(text(with("ddd HH:mm", true, true, "region"), "en_US", "en_US"), QStringLiteral("Tue 1/13/26 13:05"));
        QCOMPARE(text(with("ddd HH:mm", true, true, "region"), "nb_NO", "en_US"), QStringLiteral("tir. 13.01.2026 13:05"));
        QCOMPARE(text(with("ddd h:mm AP", true, true, "numeric"), "nb_NO", "nb_NO"),
            QStringLiteral("Tue 13/01/2026 1:05 PM"));
    }

    void theClockMovesToTheChosenPlace()
    {
        const QStringList left = {"logo", "app_name", "menus"};
        const QStringList right = {"tray", "clock", "search"};
        QVariantMap at = placed(left, right, "right");
        QCOMPARE(at["left"].toStringList(), left);
        QCOMPARE(at["middle"].toStringList(), QStringList());
        QCOMPARE(at["right"].toStringList(), right);

        at = placed(left, right, "left");
        QCOMPARE(at["left"].toStringList(), QStringList({"logo", "clock", "app_name", "menus"}));
        QCOMPARE(at["right"].toStringList(), QStringList({"tray", "search"}));

        at = placed({"app_name", "menus"}, right, "left");
        QCOMPARE(at["left"].toStringList(), QStringList({"clock", "app_name", "menus"}));

        at = placed(left, right, "middle");
        QCOMPARE(at["left"].toStringList(), left);
        QCOMPARE(at["middle"].toStringList(), QStringList({"clock"}));
        QCOMPARE(at["right"].toStringList(), QStringList({"tray", "search"}));
    }

    void aHiddenClockStaysHiddenWherever()
    {
        const QVariantMap at = placed({"logo", "menus"}, {"tray"}, "middle");
        QCOMPARE(at["middle"].toStringList(), QStringList());
        QCOMPARE(at["left"].toStringList(), QStringList({"logo", "menus"}));
    }
};

QTEST_MAIN(TestMenuBarClock)
#include "test-menu-bar-clock.moc"
