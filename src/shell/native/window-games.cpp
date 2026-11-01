#include "window-games.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QStandardPaths>

using namespace game_facts;

namespace
{
QByteArray contents(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

WindowGames::WindowGames(QObject* parent) : QObject(parent)
{
    connect(&compositor, &CompositorSocket::connected, this, &WindowGames::startWatching);
    connect(&compositor, &CompositorSocket::eventReceived, this, [this](const QJsonObject& event) {
        if (event.value("event").toString() == "view-mapped")
        {
            takeWindow(event.value("view").toObject());
        }
    });
    compositor.open();
}

int WindowGames::revision() const
{
    return changes;
}

void WindowGames::startWatching()
{
    const QJsonObject wanted{{"events", QJsonArray{"view-mapped"}}};
    compositor.request("window-rules/events/watch", wanted, [](const QJsonObject&) {});
    compositor.request("window-rules/list-views", {}, [this](const QJsonObject& answer) {
        for (const QJsonValue& window : answer.value("list").toArray())
        {
            takeWindow(window.toObject());
        }
    });
}

void WindowGames::takeWindow(const QJsonObject& description)
{
    const QString appId = description.value("app-id").toString();
    if (games.contains(appId))
    {
        return;
    }
    const int pid = description.value("pid").toInt(-1);
    const ProcessGame game = gameOfEnvironment(contents(QString("/proc/%1/environ").arg(pid)));
    if (game.steamId.isEmpty() && game.name.isEmpty())
    {
        return;
    }
    qDebug().noquote() << "[games] app=" + appId << "steam_id=" + game.steamId << "name=" + game.name;
    games.insert(appId, game);
    changes += 1;
    emit changed();
}

QString WindowGames::steamId(const QString& appId, int) const
{
    const QString own = steamIdOfAppId(appId);
    return own.isEmpty() ? games.value(appId).steamId : own;
}

QString WindowGames::gameName(const QString& appId, int) const
{
    return games.value(appId).name;
}

QStringList WindowGames::steamFolders() const
{
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList folders{data + "/Steam", QDir::homePath() + "/.steam/steam",
                        QDir::homePath() + "/.var/app/com.valvesoftware.Steam/data/Steam"};
    for (const QString& root : QStringList(folders))
    {
        const QByteArray libraries = contents(root + "/steamapps/libraryfolders.vdf");
        folders << steamLibraries(QString::fromUtf8(libraries));
    }
    return folders;
}

QString WindowGames::steamName(const QString& id) const
{
    for (const QString& folder : steamFolders())
    {
        const QString name = manifestName(QString::fromUtf8(contents(folder + "/steamapps/appmanifest_" + id + ".acf")));
        if (!name.isEmpty())
        {
            return name;
        }
    }
    return {};
}

QString WindowGames::steamIcon(const QString& id) const
{
    for (const QString& folder : steamFolders())
    {
        const QString cache = folder + "/appcache/librarycache/" + id;
        const QString icon = clientIconOf(QDir(cache).entryList(QDir::Files));
        if (!icon.isEmpty())
        {
            return cache + "/" + icon;
        }
    }
    return {};
}

QString WindowGames::lutrisIcon(const QString& gameName) const
{
    return game_facts::lutrisIcon(gameName);
}
