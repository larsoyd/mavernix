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
};

QTEST_GUILESS_MAIN(TestWindowGames)
#include "test-window-games.moc"
