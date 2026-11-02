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
    Q_PROPERTY(int windowId READ currentWindowId NOTIFY changed)
    // the compositor picks the window, instead of it being the focused one
    Q_PROPERTY(bool held READ isHeld NOTIFY changed)
    Q_PROPERTY(QString menuService READ menuService NOTIFY changed)
    Q_PROPERTY(QString menuPath READ menuPath NOTIFY changed)
    Q_PROPERTY(QVariantMap menuAddress READ menuAddress NOTIFY changed)

  public:
    explicit ActiveWindow(QObject* parent = nullptr);
    QString appId() const;
    int currentWindowId() const;
    bool isHeld() const;
    QString menuService() const;
    QString menuPath() const;
    QVariantMap menuAddress() const;
    Q_INVOKABLE void focusWindow();
    Q_INVOKABLE void closeWindow();

  signals:
    void changed();

  private:
    void startWatching();
    void takeWindow(const QJsonValue& window);
    void takeFocused();
    void followMenuWindow(const QJsonObject& event);
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
    bool held = false;
};
