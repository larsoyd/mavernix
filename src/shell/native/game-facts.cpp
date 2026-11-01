#include "game-facts.hpp"

#include <QRegularExpression>

namespace game_facts
{
namespace
{
bool isGameId(const QByteArray& value)
{
    static const QRegularExpression number("^[1-9][0-9]*$");
    return number.match(QString::fromUtf8(value)).hasMatch();
}

// vdf holds one "key" "value" pair per line with backslash escapes
QStringList valuesOf(const QString& vdf, const QString& key)
{
    const QRegularExpression pair("^\\s*\"" + key + "\"\\s+\"((?:[^\"\\\\]|\\\\.)*)\"\\s*$",
                                  QRegularExpression::MultilineOption);
    QStringList values;
    for (auto found = pair.globalMatch(vdf); found.hasNext();)
    {
        QString value = found.next().captured(1);
        values << value.replace(QRegularExpression("\\\\(.)"), "\\1");
    }
    return values;
}
}

QString steamIdOfAppId(const QString& appId)
{
    const QString prefix = "steam_app_";
    if (!appId.startsWith(prefix) || !isGameId(appId.mid(prefix.size()).toUtf8()))
    {
        return {};
    }
    return appId.mid(prefix.size());
}

ProcessGame gameOfEnvironment(const QByteArray& environment)
{
    ProcessGame game;
    for (const QByteArray& line : environment.split('\0'))
    {
        const qsizetype equals = line.indexOf('=');
        const QByteArray name = line.left(equals);
        const QByteArray value = line.mid(equals + 1);
        if ((name == "SteamAppId" || name == "SteamGameId") && isGameId(value) && game.steamId.isEmpty())
        {
            game.steamId = QString::fromUtf8(value);
        }
        else if (name == "GAME_NAME")
        {
            game.name = QString::fromUtf8(value);
        }
    }
    return game;
}

QStringList steamLibraries(const QString& libraryFolders)
{
    return valuesOf(libraryFolders, "path");
}

QString manifestName(const QString& manifest)
{
    const QStringList names = valuesOf(manifest, "name");
    return names.isEmpty() ? QString() : names.first();
}

QString clientIconOf(const QStringList& cachedFiles)
{
    // steam names the small square icon by its hash and wide art by its use
    static const QRegularExpression hashed("^[0-9a-f]{40}\\.jpg$");
    for (const QString& file : cachedFiles)
    {
        if (hashed.match(file).hasMatch())
        {
            return file;
        }
    }
    return {};
}

QString lutrisIcon(const QString& gameName)
{
    QString slug = gameName.toLower();
    slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
    slug.remove(QRegularExpression("^-|-$"));
    return slug.isEmpty() ? QString() : "lutris_" + slug;
}
}
