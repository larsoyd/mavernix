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

// app[-launcher]-ID[@random].service or app[-launcher]-ID-random.scope, with a dash in ID written \x2d
QStringList entryIdsOfCgroup(const QByteArray& cgroup)
{
    static const QRegularExpression service("^app-(.+?)(?:@[^/]*)?\\.service$");
    static const QRegularExpression scope("^app-(.+)-[^-]+\\.scope$");
    const QByteArray path = cgroup.startsWith("0::") ? cgroup : cgroup.mid(cgroup.indexOf("\n0::") + 1);
    if (!path.startsWith("0::"))
    {
        return {};
    }
    for (const QString& unit : QString::fromUtf8(path.mid(3, path.indexOf('\n') - 3)).split('/'))
    {
        auto found = service.match(unit);
        found = found.hasMatch() ? found : scope.match(unit);
        if (!found.hasMatch())
        {
            continue;
        }
        const QString name = found.captured(1);
        const auto plain = [](const QString& escaped) {
            QByteArray bytes = escaped.toUtf8();
            bytes.replace("\\x", "%");
            return QString::fromUtf8(QByteArray::fromPercentEncoding(bytes));
        };
        const qsizetype launcher = name.indexOf('-');
        return launcher < 0 ? QStringList{plain(name)} : QStringList{plain(name), plain(name.mid(launcher + 1))};
    }
    return {};
}
}
