#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace game_facts
{
struct ProcessGame
{
    QString steamId;
    QString name;
};

QString steamIdOfAppId(const QString& appId);
ProcessGame gameOfEnvironment(const QByteArray& environment);
QStringList steamLibraries(const QString& libraryFolders);
QString manifestName(const QString& manifest);
QString clientIconOf(const QStringList& cachedFiles);
QString lutrisIcon(const QString& gameName);
QStringList entryIdsOfCgroup(const QByteArray& cgroup);

struct Shortcut
{
    QString path;
    QString name;
    QString icon;
    QString windowClass;
    // the file name of each word of its command
    QStringList programs;
};

QString launchedShortcut(const QByteArray& environment, qint64 pid, const QStringList& installedFolders);
Shortcut shortcutOf(const QString& path, const QString& contents);
QList<Shortcut> shortcutsFor(const QString& appId, const QList<Shortcut>& shortcuts);
}
