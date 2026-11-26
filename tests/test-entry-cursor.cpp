// EntryCursor talks to the compositor entry-cursor plugin over WAYFIRE_SOCKET

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "entry-cursor.hpp"

namespace
{
struct FakeCompositor
{
    QLocalServer server;
    QList<QJsonObject> requests;

    bool listen(const QString& path)
    {
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (QLocalSocket* client = server.nextPendingConnection())
            {
                auto* buf = new QByteArray;
                QObject::connect(client, &QLocalSocket::readyRead, client, [this, client, buf] {
                    *buf += client->readAll();
                    while (buf->size() >= 4)
                    {
                        const quint32 len = qFromLittleEndian<quint32>(buf->constData());
                        if (buf->size() < 4 + static_cast<qsizetype>(len))
                        {
                            return;
                        }
                        const QByteArray payload = buf->mid(4, len);
                        buf->remove(0, 4 + static_cast<qsizetype>(len));
                        requests.append(QJsonDocument::fromJson(payload).object());

                        const QByteArray reply = R"({"result":"ok"})";
                        QByteArray header(4, '\0');
                        qToLittleEndian<quint32>(static_cast<quint32>(reply.size()), header.data());
                        client->write(header);
                        client->write(reply);
                        client->flush();
                    }
                });
                QObject::connect(client, &QLocalSocket::disconnected, client, [client, buf] {
                    delete buf;
                    client->deleteLater();
                });
            }
        });
        return server.listen(path);
    }
};
} // namespace

class EntryCursorTest : public QObject
{
    Q_OBJECT

  private slots:
    void sendsShowAndHideRequests()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sockPath = dir.path() + "/wf.sock";

        FakeCompositor fake;
        QVERIFY(fake.listen(sockPath));
        qputenv("WAYFIRE_SOCKET", sockPath.toUtf8());

        EntryCursor cursor;
        cursor.show();
        QTRY_COMPARE(fake.requests.size(), 1);
        QCOMPARE(fake.requests[0].value("method").toString(), QStringLiteral("entry-cursor/show"));

        cursor.hide(2500);
        QTRY_COMPARE(fake.requests.size(), 2);
        QCOMPARE(fake.requests[1].value("method").toString(), QStringLiteral("entry-cursor/hide"));
        QCOMPARE(fake.requests[1].value("data").toObject().value("ms").toInt(), 2500);
    }

    void reconnectsAndSendsAfterDisconnect()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString sockPath = dir.path() + "/wf-late.sock";

        qputenv("WAYFIRE_SOCKET", sockPath.toUtf8());
        EntryCursor cursor;
        QCoreApplication::processEvents();

        FakeCompositor fake;
        QVERIFY(fake.listen(sockPath));

        cursor.hide(1500);
        cursor.show();
        QTRY_COMPARE(fake.requests.size(), 2);
        QCOMPARE(fake.requests[0].value("method").toString(), QStringLiteral("entry-cursor/hide"));
        QCOMPARE(fake.requests[0].value("data").toObject().value("ms").toInt(), 1500);
        QCOMPARE(fake.requests[1].value("method").toString(), QStringLiteral("entry-cursor/show"));
    }
};

QTEST_MAIN(EntryCursorTest)
#include "test-entry-cursor.moc"
