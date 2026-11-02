#include "menu-registrar.hpp"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDebug>

namespace
{
const QString REGISTRAR_NAME = QStringLiteral("com.canonical.AppMenu.Registrar");
const QString REGISTRAR_PATH = QStringLiteral("/com/canonical/AppMenu/Registrar");
} // namespace

QDBusArgument& operator<<(QDBusArgument& argument, const RegisteredMenu& menu)
{
    argument.beginStructure();
    argument << menu.window << menu.service << menu.path;
    argument.endStructure();
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, RegisteredMenu& menu)
{
    argument.beginStructure();
    argument >> menu.window >> menu.service >> menu.path;
    argument.endStructure();
    return argument;
}

MenuRegistrar::MenuRegistrar(QObject* parent) : QObject(parent)
{
    qDBusRegisterMetaType<RegisteredMenu>();
    qDBusRegisterMetaType<RegisteredMenus>();
    auto bus = QDBusConnection::sessionBus();
    owners.setConnection(bus);
    owners.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&owners, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString& service) {
        for (auto it = menus.begin(); it != menus.end();)
        {
            if (it->service == service)
            {
                const uint window = it.key();
                it = menus.erase(it);
                emit WindowUnregistered(window);
            }
            else
                ++it;
        }
        owners.removeWatchedService(service);
    });
    if (!bus.registerObject(REGISTRAR_PATH, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals))
        return;
    registered = bus.registerService(REGISTRAR_NAME);
    if (!registered)
    {
        bus.unregisterObject(REGISTRAR_PATH);
        qWarning().noquote() << "Shell.Native: could not take" << REGISTRAR_NAME << bus.lastError().message();
    }
}

MenuRegistrar::~MenuRegistrar()
{
    if (registered)
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterService(REGISTRAR_NAME);
        bus.unregisterObject(REGISTRAR_PATH);
    }
}

bool MenuRegistrar::isRegistered() const
{
    return registered;
}

void MenuRegistrar::RegisterWindow(uint window, const QDBusObjectPath& path)
{
    if (!calledFromDBus() || !window || path.path() == "/")
        return;
    const QString service = message().service();
    if (menus.contains(window) && menus[window].service != service)
    {
        sendErrorReply(QDBusError::AccessDenied, "Window already registered by another connection");
        return;
    }
    menus.insert(window, {window, service, path});
    owners.addWatchedService(service);
    emit WindowRegistered(window, service, path);
}

void MenuRegistrar::UnregisterWindow(uint window)
{
    auto found = menus.find(window);
    if (found == menus.end() || !calledFromDBus())
        return;
    if (found->service != message().service())
    {
        sendErrorReply(QDBusError::AccessDenied, "Only the registering connection may remove this window");
        return;
    }
    menus.erase(found);
    emit WindowUnregistered(window);
}

QString MenuRegistrar::GetMenuForWindow(uint window, QDBusObjectPath& path)
{
    const auto found = menus.constFind(window);
    path = found == menus.cend() ? QDBusObjectPath("/") : found->path;
    return found == menus.cend() ? QString{} : found->service;
}

RegisteredMenus MenuRegistrar::GetMenus()
{
    return menus.values();
}
