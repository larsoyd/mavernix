// hears from the compositor's zone-picker plugin where the plate of layouts
// is while a window is dragged, and which zone the pointer is on

#pragma once

#include <QObject>
#include <QVariantMap>

#include "compositor-socket.hpp"

class ZonePicker : public QObject
{
    Q_OBJECT

  public:
    explicit ZonePicker(QObject* parent = nullptr);

  signals:
    // the plugin's own words: output, state, plate, minis, hover, name
    void told(const QVariantMap& plate);

  private:
    CompositorSocket compositor;
};
