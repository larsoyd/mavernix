#include "drag-hot-spot.hpp"

#include <QGuiApplication>
#include <QWindow>

namespace
{
// the drag starts a few events after the press moves far enough
constexpr int lookEveryMs = 16;
constexpr int mostLooks = 30;
}

DragHotSpot::DragHotSpot(QObject* parent) : QObject(parent)
{
    retry.setInterval(lookEveryMs);
    connect(&retry, &QTimer::timeout, this, &DragHotSpot::look);
}

void DragHotSpot::send()
{
    looks = 0;
    retry.start();
}

void DragHotSpot::look()
{
    ++looks;
    for (QWindow* window : QGuiApplication::allWindows())
    {
        if (window->inherits("QShapedPixmapWindow") && window->isVisible())
        {
            retry.stop();
            QMetaObject::invokeMethod(window, "update");
            qInfo("[drag] hot_spot sent looks=%d", looks);
            return;
        }
    }
    if (looks >= mostLooks)
    {
        retry.stop();
        qWarning("[drag] hot_spot no_icon_window looks=%d", looks);
    }
}
