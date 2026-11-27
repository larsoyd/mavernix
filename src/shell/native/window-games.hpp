#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "compositor-socket.hpp"
#include "game-facts.hpp"

class WindowGames : public QObject
{
    Q_OBJECT
    // bindings on steamId and gameName read this to update
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // where the desktop keeps its launchers
    Q_PROPERTY(QString shortcutFolder MEMBER shortcutFolder)

  public:
    explicit WindowGames(QObject* parent = nullptr);

    int revision() const;

    Q_INVOKABLE QString steamId(const QString& appId, int revision) const;
    Q_INVOKABLE QString gameName(const QString& appId, int revision) const;
    Q_INVOKABLE QString steamName(const QString& id) const;
    Q_INVOKABLE QString steamIcon(const QString& id) const;
    Q_INVOKABLE QString lutrisIcon(const QString& gameName) const;
    Q_INVOKABLE QStringList launcherEntries(const QString& appId, int revision) const;
    Q_INVOKABLE QString shortcutName(const QString& appId, int revision) const;
    Q_INVOKABLE QString shortcutIcon(const QString& appId, int revision) const;

  signals:
    void changed();

  private:
    void startWatching();
    void takeWindow(const QJsonObject& description);
    void takeLauncher(const QJsonObject& description);
    void takeShortcut(const QJsonObject& description);
    QStringList steamFolders() const;

    CompositorSocket compositor;
    QHash<QString, game_facts::ProcessGame> games;
    QHash<QString, QStringList> launchers;
    QHash<QString, game_facts::Shortcut> shortcuts;
    QString shortcutFolder;
    int changes = 0;
};
