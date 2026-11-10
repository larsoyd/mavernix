// tells the compositor's dock-dig plugin which screen the dock is on
// and hears from it when the pointer digs at a screen's bottom edge

#pragma once

#include <QObject>
#include <QString>

#include "compositor-socket.hpp"

class DockDig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)

  public:
    explicit DockDig(QObject* parent = nullptr);

    QString output() const { return outputName; }
    void setOutput(const QString& name);

  signals:
    void outputChanged();
    void dug(const QString& output, const QString& kind);

  private:
    void send();

    CompositorSocket compositor;
    QString outputName;
};
