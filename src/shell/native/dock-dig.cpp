#include "dock-dig.hpp"

#include <QDebug>
#include <QJsonObject>

DockDig::DockDig(QObject* parent) : QObject(parent)
{
    connect(&compositor, &CompositorSocket::connected, this, [this] {
        compositor.request("dock-dig/watch", {}, [](const QJsonObject& answer) {
            if (answer.contains("error"))
            {
                qWarning().noquote() << "Shell.Native: the dock stays on its screen:"
                                     << answer.value("error").toString()
                                     << "(is the compositor's \"dock-dig\" plugin loaded?)";
            }
        });
        send();
    });
    connect(&compositor, &CompositorSocket::eventReceived, this, [this](const QJsonObject& event) {
        if (event.value("event").toString() == "dock-dig")
        {
            emit dug(event.value("output").toString(), event.value("kind").toString());
        }
    });
    compositor.open();
}

void DockDig::setOutput(const QString& name)
{
    if (name == outputName)
    {
        return;
    }
    outputName = name;
    emit outputChanged();
    send();
}

void DockDig::send()
{
    if (outputName.isEmpty() || !compositor.isConnected())
    {
        return;
    }
    compositor.request("dock-dig/configure", {{"output", outputName}}, [](const QJsonObject&) {});
}
