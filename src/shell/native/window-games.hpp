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

  public:
    explicit WindowGames(QObject* parent = nullptr);

    int revision() const;

    Q_INVOKABLE QString steamId(const QString& appId, int revision) const;
    Q_INVOKABLE QString gameName(const QString& appId, int revision) const;
    Q_INVOKABLE QString steamName(const QString& id) const;
    Q_INVOKABLE QString steamIcon(const QString& id) const;
    Q_INVOKABLE QString lutrisIcon(const QString& gameName) const;

  signals:
    void changed();

  private:
    void startWatching();
    void takeWindow(const QJsonObject& description);
    QStringList steamFolders() const;

    CompositorSocket compositor;
    QHash<QString, game_facts::ProcessGame> games;
    int changes = 0;
};
