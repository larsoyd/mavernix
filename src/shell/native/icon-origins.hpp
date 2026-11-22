// tells the compositor where each desktop icon is, so a window opened from one
// grows out of it and closes back into it, through themed-open-close

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

#include "compositor-socket.hpp"

class IconOrigins : public QObject
{
    Q_OBJECT

  public:
    explicit IconOrigins(QObject* parent = nullptr);

    // every icon's box at once by its name, [x, y, width, height] in the screen's pixels
    Q_INVOKABLE void place(const QString& screenName, const QVariantMap& boxes);

  private:
    void send();

    CompositorSocket compositor;
    QJsonObject latest;
    bool warned = false;
};
