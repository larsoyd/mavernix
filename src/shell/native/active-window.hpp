#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include "compositor-socket.hpp"
#include "xmenu-properties.hpp"

class ActiveWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString appId READ appId NOTIFY changed)
    Q_PROPERTY(QString menuService READ menuService NOTIFY changed)
    Q_PROPERTY(QString menuPath READ menuPath NOTIFY changed)
    Q_PROPERTY(QVariantMap menuAddress READ menuAddress NOTIFY changed)

  public:
    explicit ActiveWindow(QObject* parent = nullptr);
    QString appId() const;
    QString menuService() const;
    QString menuPath() const;
    QVariantMap menuAddress() const;

  signals:
    void changed();

  private:
    void startWatching();
    void takeWindow(const QJsonValue& window);
    void askForMenuAddress();
    void setMenuAddress(const QVariantMap& address);

    CompositorSocket compositor;
    XMenuProperties xproperties;
    int windowId = -1;
    uint xwindow = 0;
    uint generation = 0;
    QString currentAppId;
    QVariantMap currentAddress;
    QTimer askAgain;
    bool asking = false;
};
