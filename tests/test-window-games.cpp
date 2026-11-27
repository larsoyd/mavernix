#include <QtTest>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include "window-games.hpp"

class TestWindowGames : public QObject
{
    Q_OBJECT

  private slots:
    void reusedWindowClass()
    {
        QTemporaryDir directory;
        QLocalServer server;
        QVERIFY(server.listen(directory.filePath("ipc")));
        qputenv("WAYFIRE_SOCKET", server.fullServerName().toUtf8());
        WindowGames games;
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        auto* socket = server.nextPendingConnection();
        QVERIFY(socket);
        auto send = [socket](const QJsonObject& message) {
            const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
            QByteArray frame(4, '\0');
            qToLittleEndian<quint32>(body.size(), frame.data());
            socket->write(frame + body);
            socket->flush();
        };
        auto mapped = [&](qint64 pid) {
            send({{"event", "view-mapped"}, {"view", QJsonObject{
                {"app-id", "steam_app_default"}, {"pid", pid}}}});
        };
        auto run = [&](const QString& name, const QString& expected) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert("GAME_NAME", name);
            environment.remove("SteamAppId");
            environment.remove("SteamGameId");
            process.setProcessEnvironment(environment);
            process.start("sleep", {"2"});
            QVERIFY(process.waitForStarted(1000));
            // started only means forked, the name is there once sleep runs
            auto named = [&process] {
                QFile environ(QStringLiteral("/proc/%1/environ").arg(process.processId()));
                return environ.open(QIODevice::ReadOnly) &&
                       ('\0' + environ.readAll()).contains(QByteArray("\0GAME_NAME=", 11));
            };
            QTRY_VERIFY_WITH_TIMEOUT(named(), 2000);
            mapped(process.processId());
            QTest::qWait(100);
            const QString actual = games.gameName("steam_app_default", games.revision());
            QVERIFY(process.waitForFinished(3000));
            QCOMPARE(actual, expected);
        };
        run("Firewatch", "Firewatch");
        run("Facade", "Facade");
        run("", "");
    }

    void desktopShortcut()
    {
        QTemporaryDir directory;
        const QString desktop = directory.filePath("Desktop");
        QVERIFY(QDir().mkpath(desktop));
        auto write = [&desktop](const QString& name, const QByteArray& text) {
            QFile file(desktop + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text);
        };
        write("Notepad++.desktop", "[Desktop Entry]\nName=Notepad++\nIcon=7ABC_notepad++.0\n"
                                   "Exec=notepad++-open %f\nStartupWMClass=notepad++.exe\n");
        write("Other.desktop", "[Desktop Entry]\nName=Other\nIcon=other\nExec=other\n");
        write("MO2.desktop", "[Desktop Entry]\nName=Mod Organizer\nIcon=/i/mo2.ico\n"
                             "Exec=mo2-fnv %u\nStartupWMClass=modorganizer.exe\n");
        write("Game A.desktop", "[Desktop Entry]\nName=A\nIcon=a\nExec=wine /a/game.exe\n");
        write("Game B.desktop", "[Desktop Entry]\nName=B\nIcon=b\nExec=wine /b/game.exe\n");

        QLocalServer server;
        QVERIFY(server.listen(directory.filePath("ipc")));
        qputenv("WAYFIRE_SOCKET", server.fullServerName().toUtf8());
        WindowGames games;
        games.setProperty("shortcutFolder", desktop);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        auto* socket = server.nextPendingConnection();
        QVERIFY(socket);
        auto mapped = [socket](const QString& appId, qint64 pid) {
            const QJsonObject view{{"app-id", appId}, {"pid", pid}};
            const QByteArray body = QJsonDocument(QJsonObject{{"event", "view-mapped"}, {"view", view}})
                                        .toJson(QJsonDocument::Compact);
            QByteArray frame(4, '\0');
            qToLittleEndian<quint32>(body.size(), frame.data());
            socket->write(frame + body);
            socket->flush();
        };
        // the child sets the pid itself, the shell's $$ stays its pid through exec
        auto run = [&](const QString& appId, const QString& launcher, const QString& pid,
                       const QString& name, const QString& icon) {
            QProcess process;
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.remove("GIO_LAUNCHED_DESKTOP_FILE");
            environment.remove("GIO_LAUNCHED_DESKTOP_FILE_PID");
            if (!launcher.isEmpty())
            {
                environment.insert("GIO_LAUNCHED_DESKTOP_FILE", launcher);
            }
            process.setProcessEnvironment(environment);
            process.start("sh", {"-c", "GIO_LAUNCHED_DESKTOP_FILE_PID=" + pid + " exec sleep 2"});
            QVERIFY(process.waitForStarted(1000));
            auto running = [&process] {
                QFile environ(QStringLiteral("/proc/%1/environ").arg(process.processId()));
                return environ.open(QIODevice::ReadOnly) &&
                       environ.readAll().contains("GIO_LAUNCHED_DESKTOP_FILE_PID=");
            };
            QTRY_VERIFY_WITH_TIMEOUT(running(), 2000);
            mapped(appId, process.processId());
            QTest::qWait(100);
            const QString actualName = games.shortcutName(appId, games.revision());
            const QString actualIcon = games.shortcutIcon(appId, games.revision());
            process.kill();
            QVERIFY(process.waitForFinished(3000));
            QCOMPARE(actualName, name);
            QCOMPARE(actualIcon, icon);
        };
        const QString notepad = desktop + "/Notepad++.desktop";
        const QString other = desktop + "/Other.desktop";
        const QList<QStringList> cases{
            {"whatever.exe", other, "$$", "Other", "other"},
            {"whatever.exe", other, "1", "", ""},
            {"notepad++.exe", "", "$$", "Notepad++", "7ABC_notepad++.0"},
            {"ModOrganizer.exe", notepad, "1", "Mod Organizer", "/i/mo2.ico"},
            {"game.exe", "", "$$", "", ""},
            {"notepad++.exe", other, "$$", "Other", "other"},
            {"notepad++.exe", "", "$$", "Notepad++", "7ABC_notepad++.0"},
            {"gone", "", "", "", ""},
            {"modorganizer.exe", "", "$$", "", ""},
            {"whatever.exe", other, "$$", "Other", "other"},
        };
        for (const QStringList& one : cases)
        {
            if (one[0] == "gone")
            {
                games.setProperty("shortcutFolder", directory.filePath("gone"));
                continue;
            }
            run(one[0], one[1], one[2], one[3], one[4]);
            if (QTest::currentTestFailed())
            {
                qWarning().noquote() << "[test] failed case app=" + one[0] << "launcher=" + one[1] << "pid=" + one[2];
                return;
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestWindowGames)
#include "test-window-games.moc"
