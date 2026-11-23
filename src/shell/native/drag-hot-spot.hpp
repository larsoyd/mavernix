#pragma once

#include <QObject>
#include <QTimer>

// qt sends a drag's hot spot with the icon's next attach, which never comes
// once the icon is drawn, so the icon window is repainted after the drag starts
class DragHotSpot : public QObject
{
    Q_OBJECT

  public:
    explicit DragHotSpot(QObject* parent = nullptr);

    Q_INVOKABLE void send();

  private:
    void look();

    QTimer retry;
    int looks = 0;
};
