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
}
