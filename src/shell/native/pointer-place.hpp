#pragma once

#include <QObject>
#include <QTimer>

#include "compositor-socket.hpp"

// asks the compositor where the pointer is, in layout coordinates
class PointerPlace : public QObject
{
    Q_OBJECT

  public:
    explicit PointerPlace(QObject* parent = nullptr);

    Q_INVOKABLE void ask();

  signals:
    void found(qreal x, qreal y);
    void unknown();

  private:
    void question();
    void giveUp(const char* reason);

    CompositorSocket compositor;
    QTimer deadline;
    int asked = 0;
};
