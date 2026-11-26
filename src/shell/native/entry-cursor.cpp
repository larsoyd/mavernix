#include "entry-cursor.hpp"

#include <QDebug>
#include <QJsonObject>
#include <QTimer>

EntryCursor::EntryCursor(QObject* parent) : QObject(parent)
{
    connect(&compositor, &CompositorSocket::connected, this, &EntryCursor::send);
    if (!qEnvironmentVariable("WAYFIRE_SOCKET").isEmpty())
    {
        QTimer::singleShot(0, this, [this] {
            if (!compositor.isConnected() && !compositor.isConnecting())
            {
                compositor.open();
            }
        });
    }
}

void EntryCursor::show()
{
    pending = Pending::Show;
    send();
}

void EntryCursor::hide(int ms)
{
    pending = Pending::Hide;
    pendingMs = ms;
    send();
}

void EntryCursor::send()
{
    if (pending == Pending::None)
    {
        return;
    }
    if (!compositor.isConnected())
    {
        if (!compositor.isConnecting())
        {
            compositor.open();
        }
        return;
    }
    const Pending action = pending;
    const int ms = pendingMs;
    pending = Pending::None;
    if (action == Pending::Show)
    {
        compositor.request("entry-cursor/show", {}, [](const QJsonObject& answer) {
            if (answer.contains("error"))
            {
                qWarning().noquote() << "Shell.Native: entry-cursor/show failed:"
                                     << answer.value("error").toString();
            }
        });
    }
    else
    {
        compositor.request("entry-cursor/hide", {{"ms", ms}}, [](const QJsonObject& answer) {
            if (answer.contains("error"))
            {
                qWarning().noquote() << "Shell.Native: entry-cursor/hide failed:"
                                     << answer.value("error").toString();
            }
        });
    }
}
