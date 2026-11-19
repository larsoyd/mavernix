#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include <memory>

class TestProgramStreams : public QObject
{
    Q_OBJECT

  private:
    QQmlEngine engine;
    std::unique_ptr<QObject> streams;

    QVariant call(const char* name, const QVariant& first, const QVariant& second = QVariant())
    {
        QVariant result;
        const bool called = QMetaObject::invokeMethod(streams.get(), name, Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, first), Q_ARG(QVariant, second));
        return called ? result : QVariant(QStringLiteral("[not called]"));
    }

    static QVariantMap stream(const QVariantMap& properties, double volume = 1.0, bool muted = false)
    {
        return {{QStringLiteral("properties"), properties}, {QStringLiteral("volume"), volume},
            {QStringLiteral("muted"), muted}};
    }

    // as a browser, a wine game and the shell's own sounds were seen to send them
    static QVariantMap browser()
    {
        return {{QStringLiteral("application.name"), QStringLiteral("Google Chrome")},
            {QStringLiteral("application.icon-name"), QStringLiteral("google-chrome")},
            {QStringLiteral("application.process.binary"), QStringLiteral("chrome")},
            {QStringLiteral("application.process.id"), 485111}, {QStringLiteral("media.name"), QStringLiteral("Playback")},
            {QStringLiteral("node.name"), QStringLiteral("Google Chrome")}};
    }

    static QVariantMap wineGame()
    {
        return {{QStringLiteral("application.name"), QStringLiteral("Firewatch.exe")},
            {QStringLiteral("media.name"), QStringLiteral("Firewatch.exe")},
            {QStringLiteral("node.name"), QStringLiteral("Firewatch.exe")}};
    }

    static QVariantMap shellSound(bool bare)
    {
        QVariantMap sound = {{QStringLiteral("media.role"), QStringLiteral("Notification")},
            {QStringLiteral("media.name"), QStringLiteral("quickshell")},
            {QStringLiteral("node.name"), QStringLiteral("quickshell")}};
        if (!bare) {
            sound.insert(QStringLiteral("application.name"), QStringLiteral("quickshell"));
            sound.insert(QStringLiteral("application.process.binary"), QStringLiteral("quickshell"));
        }
        return sound;
    }

    QVariantList groups(const QVariantList& list)
    {
        return call("groups", list).toList();
    }

    static QStringList labels(const QVariantList& rows)
    {
        QStringList found;
        for (const QVariant& row : rows) {
            found.append(row.toMap().value(QStringLiteral("label")).toString());
        }
        return found;
    }

  private slots:
    void initTestCase()
    {
        QQmlComponent component(&engine);
        component.setData("import QtQml\n"
                          "import \"file://" SHELL_DIR "/ProgramStreams.js\" as ProgramStreams\n"
                          "QtObject {\n"
                          "  function listed(properties, unused) { return ProgramStreams.listed(properties) }\n"
                          "  function hints(properties, unused) { return ProgramStreams.hints(properties) }\n"
                          "  function label(properties, unused) { return ProgramStreams.label(properties) }\n"
                          "  function groups(list, unused) { return ProgramStreams.groups(list) }\n"
                          "  function windowFor(hints, appIds) { return ProgramStreams.windowFor(hints, appIds) }\n"
                          "}\n",
            QUrl());
        streams.reset(component.create());
        QVERIFY2(streams != nullptr, qPrintable(component.errorString()));
    }

    void alertSoundsAreNoProgram()
    {
        QCOMPARE(call("listed", shellSound(false)).toBool(), false);
        QCOMPARE(call("listed", shellSound(true)).toBool(), false);
        QVariantMap event = browser();
        event.insert(QStringLiteral("media.role"), QStringLiteral("event"));
        QCOMPARE(call("listed", event).toBool(), false);
    }

    void aProgramPlayingIsListed()
    {
        QCOMPARE(call("listed", browser()).toBool(), true);
        QCOMPARE(call("listed", wineGame()).toBool(), true);
        QVariantMap music = browser();
        music.insert(QStringLiteral("media.role"), QStringLiteral("Music"));
        QCOMPARE(call("listed", music).toBool(), true);
    }

    void hintsRunFromMostExactToLeast()
    {
        QVariantMap sandboxed = browser();
        sandboxed.insert(QStringLiteral("pipewire.access.portal.app_id"), QStringLiteral("com.google.Chrome"));
        QCOMPARE(call("hints", sandboxed).toStringList(),
            QStringList({QStringLiteral("com.google.Chrome"), QStringLiteral("google-chrome"),
                QStringLiteral("chrome"), QStringLiteral("Google Chrome")}));
        QCOMPARE(call("hints", wineGame()).toStringList(), QStringList({QStringLiteral("Firewatch.exe")}));
        QCOMPARE(call("hints", QVariantMap()).toStringList(), QStringList());
    }

    void aHintFindsItsWindowWhateverTheCase()
    {
        const QStringList open = {QStringLiteral("foot"), QStringLiteral("firewatch.exe"),
            QStringLiteral("google-chrome")};
        QCOMPARE(call("windowFor", QStringList({QStringLiteral("Firewatch.exe")}), open).toString(),
            QStringLiteral("firewatch.exe"));
        QCOMPARE(call("windowFor", QStringList({QStringLiteral("chrome"), QStringLiteral("google-chrome")}), open)
                     .toString(),
            QStringLiteral("google-chrome"));
        QCOMPARE(call("windowFor", QStringList({QStringLiteral("Google-Chrome"), QStringLiteral("foot")}), open)
                     .toString(),
            QStringLiteral("google-chrome"));
        QCOMPARE(call("windowFor", QStringList({QStringLiteral("mpv")}), open).toString(), QString());
        QCOMPARE(call("windowFor", QStringList(), open).toString(), QString());
    }

    void theOwnNameLosesItsWindowsEnding()
    {
        QCOMPARE(call("label", wineGame()).toString(), QStringLiteral("Firewatch"));
        QCOMPARE(call("label", browser()).toString(), QStringLiteral("Google Chrome"));
        QVariantMap bare = {{QStringLiteral("node.name"), QStringLiteral("alsa_playback.tool")}};
        QCOMPARE(call("label", bare).toString(), QStringLiteral("alsa_playback.tool"));
        QVariantMap binaryOnly = {{QStringLiteral("application.process.binary"), QStringLiteral("mpv")},
            {QStringLiteral("node.name"), QStringLiteral("output")}};
        QCOMPARE(call("label", binaryOnly).toString(), QStringLiteral("mpv"));
        QCOMPARE(call("label", QVariantMap()).toString(), QString());
    }

    void streamsOfOneProgramShareARow()
    {
        const QVariantList rows = groups({stream(browser(), 0.4), stream(wineGame(), 0.7), stream(browser(), 0.9)});
        QCOMPARE(labels(rows), QStringList({QStringLiteral("Firewatch"), QStringLiteral("Google Chrome")}));
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("members")).toList(), QVariantList({0, 2}));
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("members")).toList(), QVariantList({1}));
    }

    void aRowIsAsLoudAsItsLoudestStream()
    {
        const QVariantList rows = groups({stream(browser(), 0.4), stream(browser(), 0.9), stream(browser(), 1.0, true)});
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("loudness")).toDouble(), 0.9);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("muted")).toBool(), false);
    }

    void aRowIsMutedOnlyWhenAllOfItIs()
    {
        const QVariantList rows = groups({stream(browser(), 0.4, true), stream(browser(), 0.9, true)});
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("muted")).toBool(), true);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("loudness")).toDouble(), 0.0);
    }

    void loudnessStaysWithinTheSlider()
    {
        const QVariantList rows = groups({stream(browser(), 1.6)});
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("loudness")).toDouble(), 1.0);
    }

    void alertsAndNamelessStreamsMakeNoRow()
    {
        const QVariantList rows = groups({stream(shellSound(false)), stream(shellSound(true)), stream(QVariantMap()),
            stream(wineGame())});
        QCOMPARE(labels(rows), QStringList({QStringLiteral("Firewatch")}));
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("members")).toList(), QVariantList({3}));
    }

    void rowsSortByNameWhateverTheCase()
    {
        const QVariantMap lower = {{QStringLiteral("application.name"), QStringLiteral("mpv")}};
        const QVariantMap upper = {{QStringLiteral("application.name"), QStringLiteral("Zoom")}};
        const QVariantMap first = {{QStringLiteral("application.name"), QStringLiteral("audacity")}};
        QCOMPARE(labels(groups({stream(upper), stream(lower), stream(first)})),
            QStringList({QStringLiteral("audacity"), QStringLiteral("mpv"), QStringLiteral("Zoom")}));
    }

    void noStreamsGiveNoRows()
    {
        QCOMPARE(groups({}).size(), 0);
    }
};

QTEST_MAIN(TestProgramStreams)
#include "test-program-streams.moc"
