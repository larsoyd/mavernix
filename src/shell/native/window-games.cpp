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
            takeLauncher(event.value("view").toObject());
            takeShortcut(event.value("view").toObject());
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
            takeLauncher(window.toObject());
            takeShortcut(window.toObject());
        }
    });
}

void WindowGames::takeWindow(const QJsonObject& description)
{
    const QString appId = description.value("app-id").toString();
    const int pid = description.value("pid").toInt(-1);
    const ProcessGame game = gameOfEnvironment(contents(QString("/proc/%1/environ").arg(pid)));
    const ProcessGame previous = games.value(appId);
    if (game.steamId == previous.steamId && game.name == previous.name)
    {
        return;
    }
    qDebug().noquote() << "[games] app=" + appId << "steam_id=" + game.steamId << "name=" + game.name;
    games.insert(appId, game);
    changes += 1;
    emit changed();
}

// the desktop entry whose launch made the window's systemd unit
void WindowGames::takeLauncher(const QJsonObject& description)
{
    const QString appId = description.value("app-id").toString();
    const int pid = description.value("pid").toInt(-1);
    // a withdrawn x11 window has no pid and says nothing about the launch
    if (pid <= 0)
    {
        return;
    }
    const QStringList ids = entryIdsOfCgroup(contents(QString("/proc/%1/cgroup").arg(pid)));
    if (ids == launchers.value(appId))
    {
        return;
    }
    qDebug().noquote() << "[games] app=" + appId << "launcher=" + ids.join(',');
    launchers.insert(appId, ids);
    changes += 1;
    emit changed();
}

// the desktop launcher a window came from when no installed entry names it
void WindowGames::takeShortcut(const QJsonObject& description)
{
    const QString appId = description.value("app-id").toString();
    const int pid = description.value("pid").toInt(-1);
    if (pid <= 0)
    {
        return;
    }
    const QStringList installed = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    const QString launched = launchedShortcut(contents(QString("/proc/%1/environ").arg(pid)), pid, installed);
    Shortcut found = launched.isEmpty() ? Shortcut{} : shortcutOf(launched, QString::fromUtf8(contents(launched)));
    QString via = "environ";
    if (found.name.isEmpty())
    {
        QList<Shortcut> desktop;
        const QDir folder(shortcutFolder);
        for (const QString& name : folder.entryList({"*.desktop"}, QDir::Files))
        {
            desktop << shortcutOf(folder.filePath(name), QString::fromUtf8(contents(folder.filePath(name))));
        }
        const QList<Shortcut> claims = shortcutsFor(appId, desktop);
        if (claims.size() > 1)
        {
            qWarning().noquote() << "[games] app=" + appId << "shortcut=none reason=clash count=" +
                                                                     QString::number(claims.size());
        }
        found = claims.size() == 1 ? claims.first() : Shortcut{};
        via = "desktop";
    }
    if (found.path == shortcuts.value(appId).path)
    {
        return;
    }
    qDebug().noquote() << "[games] app=" + appId << "shortcut=" + found.path << "via=" + via;
    shortcuts.insert(appId, found);
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

QString WindowGames::shortcutName(const QString& appId, int) const
{
    return shortcuts.value(appId).name;
}

QString WindowGames::shortcutIcon(const QString& appId, int) const
{
    return shortcuts.value(appId).icon;
}

QStringList WindowGames::launcherEntries(const QString& appId, int) const
{
    return launchers.value(appId);
}
