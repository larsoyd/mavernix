#include "zone-picker.hpp"

#include <QDebug>
#include <QJsonObject>

ZonePicker::ZonePicker(QObject* parent) : QObject(parent)
{
    connect(&compositor, &CompositorSocket::connected, this, [this] {
        compositor.request("zone-picker/watch", {}, [](const QJsonObject& answer) {
            if (answer.contains("error"))
            {
                qInfo().noquote() << "Shell.Native: no plate of layouts while dragging windows:"
                                  << answer.value("error").toString();
            }
        });
    });
    connect(&compositor, &CompositorSocket::eventReceived, this, [this](const QJsonObject& event) {
        if (event.value("event").toString() == "zone-picker")
        {
            emit told(event.toVariantMap());
        }
    });
    compositor.open();
}
