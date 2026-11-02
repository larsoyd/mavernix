#include "active-window.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QJsonArray>

ActiveWindow::ActiveWindow(QObject* parent) : QObject(parent)
{
    askAgain.setInterval(750);
    connect(&askAgain, &QTimer::timeout, this, &ActiveWindow::askForMenuAddress);
    connect(&compositor, &CompositorSocket::connected, this, &ActiveWindow::startWatching);
    connect(&compositor, &CompositorSocket::disconnected, this, [this] {
        held = false;
        takeWindow(QJsonValue(QJsonValue::Null));
        asking = false;
        askAgain.start();
    });
    connect(&compositor, &CompositorSocket::eventReceived, this, [this](const QJsonObject& event) {
        const QString name = event.value("event").toString();
        if (name == "view-focused" && !held)
            takeWindow(event.value("view"));
        else if (name == "menu-window")
            followMenuWindow(event);
    });
    connect(&xproperties, &XMenuProperties::resolved, this, [this](uint window, const QVariantMap& address) {
        if (window == xwindow && windowId >= 0)
        {
            asking = false;
            setMenuAddress(address);
        }
    });
    compositor.open();
    askAgain.start();
}

QString ActiveWindow::appId() const
{
    return currentAppId;
}
int ActiveWindow::currentWindowId() const
{
    return windowId;
}
bool ActiveWindow::isHeld() const
{
    return held;
}
QString ActiveWindow::outputName() const
{
    return output;
}
void ActiveWindow::setOutputName(const QString& name)
{
    if (name == output)
        return;
    output = name;
    if (held)
        applyMenuEvent();
    emit changed();
}
bool ActiveWindow::isActiveOutput() const
{
    return menuEvent.value("active").toString() == output;
}
QString ActiveWindow::menuService() const
{
    return currentAddress.value("service").toString();
}
QString ActiveWindow::menuPath() const
{
    return currentAddress.value("path").toString();
}
QVariantMap ActiveWindow::menuAddress() const
{
    return currentAddress;
}

void ActiveWindow::startWatching()
{
    ++generation;
    asking = false;
    held = false;
    windowId = -1;
    xwindow = 0;
    currentAppId.clear();
    setMenuAddress({});
    compositor.request(
        "window-rules/events/watch", {{"events", QJsonArray{"view-focused"}}}, [](const QJsonObject&) {});
    compositor.request("global-menu/watch-menu-window", {}, [](const QJsonObject&) {});
    takeFocused();
}

void ActiveWindow::takeFocused()
{
    compositor.request("window-rules/get-focused-view", {}, [this](const QJsonObject& answer) {
        if (!held)
            takeWindow(answer.value("info"));
    });
}

void ActiveWindow::followMenuWindow(const QJsonObject& event)
{
    const bool wasHeld = held;
    held = event.value("held").toBool();
    menuEvent = event;
    if (held)
        applyMenuEvent();
    else if (wasHeld)
        takeFocused();
    emit changed();
}

void ActiveWindow::applyMenuEvent()
{
    const QJsonValue view = output.isEmpty() ? menuEvent.value("view") : menuEvent.value("windows")[output];
    takeWindow(view.isObject() ? view : QJsonValue(QJsonValue::Null));
}

void ActiveWindow::focusWindow()
{
    if (held)
        compositor.request("global-menu/focus-menu-window", {{"output", output}}, [](const QJsonObject&) {});
}

void ActiveWindow::closeWindow()
{
    if (windowId >= 0)
        compositor.request("window-rules/close-view", {{"id", windowId}}, [](const QJsonObject&) {});
}

void ActiveWindow::takeWindow(const QJsonValue& window)
{
    const QJsonObject description = window.toObject();
    const bool application = description.value("role").toString() == "toplevel";
    const bool desktop = description.value("layer").toString() == "background";
    if (!window.isNull() && !application && !desktop)
        return;
    const int id = window.isNull() || desktop ? -1 : description.value("id").toInt(-1);
    if (id == windowId)
        return;
    ++generation;
    asking = false;
    windowId = id;
    xwindow = 0;
    xproperties.request(0);
    currentAppId = description.value("app-id").toString();
    currentAddress.clear();
    emit changed();
    askAgain.stop();
    if (windowId >= 0)
    {
        askAgain.start();
        askForMenuAddress();
    }
}

void ActiveWindow::askForMenuAddress()
{
    if (!compositor.isConnected())
    {
        if (!compositor.isConnecting())
            compositor.open();
        return;
    }
    if (windowId < 0 || asking)
        return;
    asking = true;
    const uint requested = generation;
    compositor.request("global-menu/address", {{"id", windowId}}, [this, requested](const QJsonObject& answer) {
        if (requested != generation)
            return;
        xwindow = answer.value("xid").toInteger();
        if (!answer.value("service").toString().isEmpty())
        {
            asking = false;
            auto address = answer.toVariantMap();
            address.remove("result");
            address.remove("xid");
            setMenuAddress(address);
            return;
        }
        if (!xwindow)
        {
            asking = false;
            setMenuAddress({});
            return;
        }
        auto call = QDBusMessage::createMethodCall("com.canonical.AppMenu.Registrar",
            "/com/canonical/AppMenu/Registrar", "com.canonical.AppMenu.Registrar", "GetMenuForWindow");
        call << xwindow;
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, requested](QDBusPendingCallWatcher* done) {
            done->deleteLater();
            if (requested != generation)
                return;
            const auto args = done->reply().arguments();
            if (done->reply().type() == QDBusMessage::ReplyMessage && args.size() == 2 && !args[0].toString().isEmpty())
            {
                asking = false;
                setMenuAddress({{"protocol", "dbusmenu"}, {"service", args[0].toString()},
                    {"path", args[1].value<QDBusObjectPath>().path()}});
            }
            else
                xproperties.request(xwindow);
        });
    });
}

void ActiveWindow::setMenuAddress(const QVariantMap& address)
{
    if (address == currentAddress)
        return;
    currentAddress = address;
    emit changed();
}
