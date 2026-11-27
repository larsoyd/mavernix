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

namespace game_facts
{
namespace
{
// words of an exec line, a quoted one kept whole
QStringList execWords(const QString& line)
{
    QStringList words;
    QString word;
    bool quoted = false;
    bool started = false;
    for (qsizetype at = 0; at < line.size(); ++at)
    {
        const QChar c = line.at(at);
        if (quoted && c == '\\' && at + 1 < line.size())
        {
            word += line.at(++at);
        }
        else if (c == '"')
        {
            quoted = !quoted;
            started = true;
        }
        else if (!quoted && c.isSpace())
        {
            if (started)
            {
                words << word;
            }
            word.clear();
            started = false;
        }
        else
        {
            word += c;
            started = true;
        }
    }
    if (started)
    {
        words << word;
    }
    return words;
}

QString fileName(const QString& word)
{
    return word.mid(qMax(word.lastIndexOf('/'), word.lastIndexOf('\\')) + 1);
}
}

// the launcher gio started this very process from, unless an installed one
QString launchedShortcut(const QByteArray& environment, qint64 pid, const QStringList& installedFolders)
{
    QString path;
    bool own = false;
    for (const QByteArray& line : environment.split('\0'))
    {
        if (line.startsWith("GIO_LAUNCHED_DESKTOP_FILE="))
        {
            path = QString::fromUtf8(line.mid(26));
        }
        else if (line.startsWith("GIO_LAUNCHED_DESKTOP_FILE_PID="))
        {
            own = line.mid(30) == QByteArray::number(pid);
        }
    }
    const bool installed = std::any_of(installedFolders.begin(), installedFolders.end(),
                                       [&path](const QString& folder) { return path.startsWith(folder + '/'); });
    return own && !installed ? path : QString();
}

Shortcut shortcutOf(const QString& path, const QString& contents)
{
    Shortcut shortcut{path, {}, {}, {}, {}, {}};
    bool inEntry = false;
    for (const QString& line : contents.split('\n'))
    {
        if (line.startsWith('['))
        {
            inEntry = line.trimmed() == "[Desktop Entry]";
            continue;
        }
        const qsizetype equals = line.indexOf('=');
        if (!inEntry || equals < 0)
        {
            continue;
        }
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key == "Name")
        {
            shortcut.name = value;
        }
        else if (key == "Icon")
        {
            shortcut.icon = value;
        }
        else if (key == "StartupWMClass")
        {
            shortcut.windowClass = value;
        }
        else if (key == "MimeType")
        {
            shortcut.types = value.split(';', Qt::SkipEmptyParts);
        }
        else if (key == "Exec")
        {
            shortcut.programs.clear();
            for (const QString& word : execWords(value))
            {
                shortcut.programs << fileName(word);
            }
        }
    }
    return shortcut;
}

// wine names a window after its exe, so a shortcut that runs or opens that file claims it
QList<Shortcut> shortcutsFor(const QString& appId, const QList<Shortcut>& shortcuts)
{
    QList<Shortcut> found;
    for (const Shortcut& shortcut : shortcuts)
    {
        const bool named = shortcut.windowClass.compare(appId, Qt::CaseInsensitive) == 0 ||
                           shortcut.programs.contains(appId, Qt::CaseInsensitive);
        if (named)
        {
            found << shortcut;
        }
    }
    return found;
}

// a loader like wine is never the program it was asked to run
bool opensWindowsPrograms(const Shortcut& entry)
{
    static const QStringList windows{"application/x-ms-dos-executable", "application/x-msdownload",
                                     "application/vnd.microsoft.portable-executable", "application/x-msi"};
    return std::any_of(windows.begin(), windows.end(),
                       [&entry](const QString& type) { return entry.types.contains(type); });
}

// wine names a window after its exe in lower case and maps that file
QString programOfMaps(const QString& appId, const QByteArray& maps)
{
    if (!appId.endsWith(".exe", Qt::CaseInsensitive))
    {
        return {};
    }
    static const QRegularExpression mapped("^(?:\\S+\\s+){5}(/.*)$", QRegularExpression::MultilineOption);
    for (auto found = mapped.globalMatch(QString::fromUtf8(maps)); found.hasNext();)
    {
        const QString path = found.next().captured(1);
        if (fileName(path).compare(appId, Qt::CaseInsensitive) == 0)
        {
            return path;
        }
    }
    return {};
}
}
