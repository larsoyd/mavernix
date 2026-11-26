// asks the compositor entry-cursor plugin to reveal or hide the pointer
// connects over the compositor control socket with no subprocess cost

#pragma once

#include <QObject>

#include "compositor-socket.hpp"

class EntryCursor : public QObject
{
    Q_OBJECT

  public:
    explicit EntryCursor(QObject* parent = nullptr);

    Q_INVOKABLE void show();
    Q_INVOKABLE void hide(int ms = 3000);

  private:
    enum class Pending
    {
        None,
        Show,
        Hide,
    };

    void send();

    CompositorSocket compositor;
    Pending pending = Pending::None;
    int pendingMs = 3000;
};
