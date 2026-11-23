#include "folder-contents.hpp"

#include "file-moment.hpp"

#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QImageReader>
#include <QProcess>
#include <QtConcurrent>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QMimeType>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <optional>

#include <sys/xattr.h>

FolderContents::FolderContents(QObject* parent) : QObject(parent)
{
    connect(&watcher, &QFileSystemWatcher::directoryChanged, this, &FolderContents::look);
}

QString FolderContents::path() const
{
    return folderPath;
}

void FolderContents::setPath(const QString& path)
{
    if (path == folderPath)
    {
        return;
    }

    folderPath = path;
    emit pathChanged();
    look();
}

QString FolderContents::order() const
{
    return wantedOrder;
}

void FolderContents::setOrder(const QString& order)
{
    if (order == wantedOrder)
    {
        return;
    }

    wantedOrder = order;
    emit orderChanged();
    look();
}

QVariantList FolderContents::entries() const
{
    return found;
}

bool FolderContents::itemInfo() const
{
    return wantsInfo;
}

void FolderContents::setItemInfo(bool itemInfo)
{
    if (itemInfo == wantsInfo)
    {
        return;
    }
    wantsInfo = itemInfo;
    emit itemInfoChanged();
    measure();
}

QVariantMap FolderContents::infos() const
{
    return measured;
}

// By name alone: quick, and enough for an icon.
static QMimeType kind_of(const QFileInfo& file)
{
    static const QMimeDatabase kinds;
    if (file.isDir())
    {
        return kinds.mimeTypeForName(QStringLiteral("inode/directory"));
    }
    return kinds.mimeTypeForFile(file.fileName(), QMimeDatabase::MatchExtension);
}

// the tags kde and others keep in an extended attribute, empty without any
static QString tags_of(const QFileInfo& file)
{
    char tags[1024];
    const ssize_t length = getxattr(QFile::encodeName(file.absoluteFilePath()).constData(), "user.xdg.tags", tags,
        sizeof tags);
    return length > 0 ? QString::fromUtf8(tags, length) : QString();
}

static void sort_files(QFileInfoList& files, const QString& order)
{
    const bool by_time = order == QLatin1String("added") || order == QLatin1String("modified")
        || order == QLatin1String("created");
    const auto by_name = [](const QFileInfo& first, const QFileInfo& second) {
        return first.fileName().localeAwareCompare(second.fileName()) < 0;
    };
    const auto size = [](const QFileInfo& file) { return file.isDir() ? -1 : file.size(); };
    std::stable_sort(files.begin(), files.end(), [&](const QFileInfo& first, const QFileInfo& second) {
        if (by_time)
        {
            return moment_for(first, order) > moment_for(second, order);
        }
        if (order == QLatin1String("last_opened") && first.lastRead() != second.lastRead())
        {
            return first.lastRead() > second.lastRead();
        }
        if (order == QLatin1String("size") && size(first) != size(second))
        {
            return size(first) > size(second);
        }
        if (order == QLatin1String("tags"))
        {
            const QString one = tags_of(first);
            const QString other = tags_of(second);
            if (one != other)
            {
                return other.isEmpty() || (!one.isEmpty() && one.localeAwareCompare(other) < 0);
            }
        }
        if (order == QLatin1String("kind"))
        {
            const int kinds = kind_of(first).comment().localeAwareCompare(kind_of(second).comment());
            if (kinds != 0)
            {
                return kinds < 0;
            }
        }
        return by_name(first, second);
    });
}

// what finder's item info says about a file: a folder's count, a picture's size, a sound's length
static QVariantMap info_of(const QString& path)
{
    const QFileInfo file(path);
    if (file.isDir())
    {
        return {{QStringLiteral("items"), QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size()}};
    }
    const QString kind = kind_of(file).name();
    if (kind.startsWith(QLatin1String("image/")))
    {
        const QSize size = QImageReader(path).size();
        if (size.isValid())
        {
            return {{QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()}};
        }
    }
    if (kind.startsWith(QLatin1String("audio/")) || kind.startsWith(QLatin1String("video/")))
    {
        QProcess probe;
        probe.setStandardInputFile(QProcess::nullDevice());
        probe.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                                                QStringLiteral("format=duration"), QStringLiteral("-of"),
                                                QStringLiteral("csv=p=0"), path});
        if (!probe.waitForFinished(3000))
        {
            probe.kill();
            probe.waitForFinished(1000);
            qWarning("[folder-contents] info=duration path=%s refused=ffprobe_timeout", qPrintable(path));
            return {};
        }
        bool read = false;
        const double seconds = probe.readAllStandardOutput().trimmed().toDouble(&read);
        if (read)
        {
            return {{QStringLiteral("seconds"), int(std::floor(seconds))}};
        }
    }
    return {};
}

QStringList FolderContents::pathsInOrder(const QString& order) const
{
    QFileInfoList files;
    for (const QVariant& entry : found)
    {
        files.append(QFileInfo(entry.toMap().value(QStringLiteral("path")).toString()));
    }
    sort_files(files, order);
    QStringList paths;
    for (const QFileInfo& file : files)
    {
        paths.append(file.absoluteFilePath());
    }
    return paths;
}

void FolderContents::measure()
{
    const int round = ++measuring;
    if (!wantsInfo)
    {
        if (!measured.isEmpty())
        {
            measured.clear();
            emit infosChanged();
        }
        return;
    }
    QStringList paths;
    for (const QVariant& entry : found)
    {
        paths.append(entry.toMap().value(QStringLiteral("path")).toString());
    }
    auto* watch = new QFutureWatcher<QVariantMap>(this);
    connect(watch, &QFutureWatcher<QVariantMap>::finished, this, [this, watch, round] {
        watch->deleteLater();
        if (round == measuring && wantsInfo)
        {
            measured = watch->result();
            emit infosChanged();
        }
    });
    watch->setFuture(QtConcurrent::run([paths] {
        QVariantMap infos;
        for (const QString& path : paths)
        {
            infos.insert(path, info_of(path));
        }
        return infos;
    }));
}


// A launcher's name and icon from its [Desktop Entry] group. Only executable
// files count, so a downloaded launcher does nothing until the user allows it.
struct launcher_t
{
    QString name;
    QString icon;
};

// the keys of a file's [Desktop Entry] group
static QHash<QString, QString> desktop_entry(const QString& path)
{
    QHash<QString, QString> keys;
    QFile entry(path);
    if (!entry.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return keys;
    }
    bool in_entry_group = false;
    while (!entry.atEnd())
    {
        const QString line = QString::fromUtf8(entry.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('[')))
        {
            in_entry_group = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (in_entry_group && equals > 0)
        {
            keys.insert(line.left(equals), line.mid(equals + 1));
        }
    }
    return keys;
}

static std::optional<launcher_t> launcher_in(const QFileInfo& file)
{
    if (file.suffix() != QLatin1String("desktop") || !file.isFile() || !file.isExecutable())
    {
        return std::nullopt;
    }
    const QHash<QString, QString> keys = desktop_entry(file.absoluteFilePath());
    const launcher_t launcher{keys.value(QStringLiteral("Name")), keys.value(QStringLiteral("Icon"))};
    if (keys.value(QStringLiteral("Type")) != QLatin1String("Application") || launcher.name.isEmpty())
    {
        return std::nullopt;
    }
    return launcher;
}

// the icon the folder names for itself, else home's or a usual folder's own picture
static QString folder_icon(const QFileInfo& file)
{
    const QString target = file.canonicalFilePath();
    const QString own = desktop_entry(target + QStringLiteral("/.directory")).value(QStringLiteral("Icon"));
    if (!own.isEmpty())
    {
        return own;
    }
    const QString home = QFileInfo(QDir::homePath()).canonicalFilePath();
    if (target == home)
    {
        return QStringLiteral("user-home");
    }
    const std::pair<QStandardPaths::StandardLocation, const char*> usual[] = {
        {QStandardPaths::DesktopLocation, "user-desktop"},
        {QStandardPaths::DocumentsLocation, "folder-documents"},
        {QStandardPaths::DownloadLocation, "folder-download"},
        {QStandardPaths::MusicLocation, "folder-music"},
        {QStandardPaths::PicturesLocation, "folder-pictures"},
        {QStandardPaths::MoviesLocation, "folder-videos"},
        {QStandardPaths::PublicShareLocation, "folder-publicshare"},
    };
    for (const auto& [location, icon] : usual)
    {
        const QString place = QFileInfo(QStandardPaths::writableLocation(location)).canonicalFilePath();
        if (place == target)
        {
            return QString::fromLatin1(icon);
        }
    }
    return QStringLiteral("folder");
}

// the folder, or the nearest one above it while it is missing
static QString nearest_folder(QString path)
{
    while (!QFileInfo(path).isDir())
    {
        const QString above = QFileInfo(path).path();
        if (above == path)
        {
            break;
        }
        path = above;
    }
    return path;
}

void FolderContents::look()
{
    if (!watcher.directories().isEmpty())
    {
        watcher.removePaths(watcher.directories());
    }

    QFileInfoList files;
    const QDir folder(folderPath);
    if (!folderPath.isEmpty())
    {
        // a folder made later is noticed through the one above it
        watcher.addPath(nearest_folder(folderPath));
        if (folder.exists())
        {
            files = folder.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        }
    }

    sort_files(files, wantedOrder);

    QVariantList entries;
    for (const QFileInfo& file : files)
    {
        const bool is_folder = file.isDir();
        if (const auto launcher = launcher_in(file))
        {
            entries.append(QVariantMap{
                {QStringLiteral("name"), launcher->name},
                {QStringLiteral("path"), file.absoluteFilePath()},
                {QStringLiteral("icon"), launcher->icon},
                {QStringLiteral("genericIcon"), QStringLiteral("application-x-executable")},
                {QStringLiteral("isFolder"), false},
                {QStringLiteral("isLauncher"), true},
                {QStringLiteral("isLink"), file.isSymLink()},
            });
            continue;
        }
        const QMimeType kind = kind_of(file);
        entries.append(QVariantMap{
            {QStringLiteral("name"), file.fileName()},
            {QStringLiteral("path"), file.absoluteFilePath()},
            {QStringLiteral("icon"), is_folder ? folder_icon(file) : kind.iconName()},
            {QStringLiteral("genericIcon"), is_folder ? QStringLiteral("folder") : kind.genericIconName()},
            {QStringLiteral("isFolder"), is_folder},
            {QStringLiteral("isLauncher"), false},
            {QStringLiteral("isLink"), file.isSymLink()},
        });
    }

    found = entries;
    emit entriesChanged();
    measure();
}
