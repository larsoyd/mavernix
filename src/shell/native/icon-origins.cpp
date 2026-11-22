#include "icon-origins.hpp"

#include <QDebug>

IconOrigins::IconOrigins(QObject* parent) : QObject(parent)
{
    // a compositor that comes back has lost the boxes
    connect(&compositor, &CompositorSocket::connected, this, &IconOrigins::send);
}

void IconOrigins::place(const QString& screenName, const QVariantMap& boxes)
{
    latest = QJsonObject{{"output", screenName}, {"origins", QJsonObject::fromVariantMap(boxes)}};
    if (compositor.isConnected())
    {
        send();
    }
    else if (!compositor.isConnecting())
    {
        compositor.open();
    }
}

void IconOrigins::send()
{
    if (latest.isEmpty())
    {
        return;
    }
    compositor.request("themed-open-close/origins", latest, [this](const QJsonObject& answer) {
        if (answer.contains("error") && !warned)
        {
            qWarning().noquote() << "[icon-origins] refused reason=" << answer.value("error").toString();
            warned = true;
        }
    });
}
