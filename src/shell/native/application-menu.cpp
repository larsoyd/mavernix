#include "application-menu.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDateTime>

namespace
{
const QString MENU_INTERFACE = QStringLiteral("com.canonical.dbusmenu");

const int ROOT_ITEM = 0;
const int WHOLE_TREE = -1;

// Strips mnemonic marks ("_File"); "__" stands for a real underscore.
QString withoutMnemonic(const QString& label)
{
    QString result;
    for (qsizetype position = 0; position < label.size(); position++)
    {
        const bool isUnderscore = label[position] == '_';
        const bool nextIsUnderscore = (position + 1 < label.size()) && (label[position + 1] == '_');
        if (isUnderscore && nextIsUnderscore)
        {
            result += '_';
            position++;
        }
        else if (!isUnderscore)
        {
            result += label[position];
        }
    }
    return result;
}

// The "shortcut" property is a list of combinations: [["Control", "S"]].
QStringList firstShortcut(const QVariant& property)
{
    if (!property.canConvert<QDBusArgument>())
    {
        return {};
    }

    QList<QStringList> combinations;
    property.value<QDBusArgument>() >> combinations;
    return combinations.isEmpty() ? QStringList() : combinations.first();
}

// Reads an item and its subtree. D-Bus signature (ia{sv}av): id,
// properties, and children wrapped in variants.
QVariantMap readItem(const QDBusArgument& argument)
{
    int id = 0;
    QVariantMap properties;
    QVariantList children;

    argument.beginStructure();
    argument >> id >> properties;
    argument.beginArray();
    while (!argument.atEnd())
    {
        QDBusVariant wrappedChild;
        argument >> wrappedChild;
        const QVariantMap child = readItem(wrappedChild.variant().value<QDBusArgument>());
        if (child.value("visible", true).toBool())
        {
            children.append(child);
        }
    }
    argument.endArray();
    argument.endStructure();

    const QString toggleType = properties.value("toggle-type").toString();
    return {
        {"id", id},
        {"label", withoutMnemonic(properties.value("label").toString())},
        {"enabled", properties.value("enabled", true).toBool()},
        {"visible", properties.value("visible", true).toBool()},
        {"separator", properties.value("type").toString() == "separator"},
        {"checkable", !toggleType.isEmpty()},
        {"checked", properties.value("toggle-state", 0).toInt() == 1},
        {"shortcut", firstShortcut(properties.value("shortcut"))},
        {"children", children},
    };
}
} // namespace

ApplicationMenu::ApplicationMenu(QObject* parent) : QObject(parent)
{
    fetchTimer.setSingleShot(true);
    fetchTimer.setInterval(0);
    connect(&fetchTimer, &QTimer::timeout, this, &ApplicationMenu::fetch);
    // a choice comes right after the closing and has to arrive first
    closeTimer.setSingleShot(true);
    closeTimer.setInterval(0);
    connect(&closeTimer, &QTimer::timeout, this, [this] { closeMenusBelow(ROOT_ITEM); });
    connect(&gmenu, &GMenuReader::itemsChanged, this, [this] {
        if (currentAddress.value("protocol") == "gmenu")
            setItems(gmenu.items());
    });
    serviceWatcher.setConnection(QDBusConnection::sessionBus());
    serviceWatcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    connect(&serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
        [this](const QString&, const QString&, const QString& owner) {
        if (!owner.isEmpty())
            followAddress();
        else
        {
            ++addressGeneration;
            fetchTimer.stop();
            closeTimer.stop();
            openMenus.clear();
            gmenu.follow({});
            setItems({});
        }
    });
}

QVariantMap ApplicationMenu::address() const
{
    return currentAddress;
}

void ApplicationMenu::setAddress(const QVariantMap& address)
{
    if (currentAddress == address)
        return;
    closeTimer.stop();
    closeMenusBelow(ROOT_ITEM);
    currentAddress = address;
    currentService = address.value("service").toString();
    currentPath = address.value("path").toString();
    followAddress();
}

QString ApplicationMenu::service() const
{
    return currentService;
}

void ApplicationMenu::setService(const QString& service)
{
    if (service == currentService)
    {
        return;
    }
    currentService = service;
    followAddress();
}

QString ApplicationMenu::path() const
{
    return currentPath;
}

void ApplicationMenu::setPath(const QString& path)
{
    if (path == currentPath)
    {
        return;
    }
    currentPath = path;
    followAddress();
}

QVariantList ApplicationMenu::items() const
{
    return currentItems;
}

void ApplicationMenu::followAddress()
{
    addressGeneration++;
    serviceWatcher.setWatchedServices(currentService.isEmpty() ? QStringList{} : QStringList{currentService});
    emit addressChanged();

    // Empty service and path remove every match this object made.
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.disconnect(QString(), QString(), MENU_INTERFACE, "LayoutUpdated", this, SLOT(fetchSoon()));
    bus.disconnect(QString(), QString(), MENU_INTERFACE, "ItemsPropertiesUpdated", this, SLOT(fetchSoon()));

    setItems({});
    fetchTimer.stop();
    steadyIds.clear();
    gmenu.follow(currentAddress);
    if (currentAddress.value("protocol") == "gmenu")
        return;
    if (currentService.isEmpty() || currentPath.isEmpty())
    {
        return;
    }

    bus.connect(currentService, currentPath, MENU_INTERFACE, "LayoutUpdated", this, SLOT(fetchSoon()));
    bus.connect(currentService, currentPath, MENU_INTERFACE, "ItemsPropertiesUpdated", this, SLOT(fetchSoon()));
    fetchSoon();
}

void ApplicationMenu::fetchSoon()
{
    fetchTimer.start();
}

void ApplicationMenu::fetch()
{
    if (currentService.isEmpty() || currentPath.isEmpty())
    {
        return;
    }

    // GetLayout(parent, depth, property names); no names means all.
    QDBusMessage call = QDBusMessage::createMethodCall(currentService, currentPath, MENU_INTERFACE, "GetLayout");
    call << ROOT_ITEM << WHOLE_TREE << QStringList();

    const int askedInGeneration = addressGeneration;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, askedInGeneration](QDBusPendingCallWatcher* finished) {
        finished->deleteLater();
        if (askedInGeneration != addressGeneration)
        {
            return;
        }

        const QDBusMessage answer = finished->reply();
        if (answer.type() != QDBusMessage::ReplyMessage || answer.arguments().size() < 2)
        {
            // An application that has just quit is not worth a warning.
            const bool applicationGone =
                answer.errorName() == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown");
            if (!applicationGone)
            {
                qWarning().noquote() << "Shell.Native: could not fetch the menu at" << currentService << currentPath
                                     << "-" << answer.errorMessage();
            }
            setItems({});
            return;
        }

        // The answer is a revision number and the root item.
        const QVariantMap root = readItem(answer.arguments().at(1).value<QDBusArgument>());
        exporterIds.clear();
        parents.clear();
        setItems(withSteadyIds(root.value("children").toList(), ROOT_ITEM));
    });
}

QVariantList ApplicationMenu::withSteadyIds(const QVariantList& items, int parent)
{
    QVariantList result;
    QHash<QString, int> uses;
    for (const QVariant& entry : items)
    {
        QVariantMap item = entry.toMap();
        const QString label = item.value("label").toString();
        const QString key = QStringLiteral("%1/%2/%3").arg(parent).arg(uses[label]++).arg(label);
        const int id = steadyIds.contains(key) ? steadyIds.value(key) : steadyIds.size() + 1;
        steadyIds.insert(key, id);
        exporterIds.insert(id, item.value("id").toInt());
        parents.insert(id, parent);
        item.insert("id", id);
        item.insert("children", withSteadyIds(item.value("children").toList(), id));
        result.append(item);
    }
    return result;
}

void ApplicationMenu::setItems(const QVariantList& newItems)
{
    if (newItems == currentItems)
    {
        return;
    }
    currentItems = newItems;
    emit itemsChanged();
}

void ApplicationMenu::aboutToShow(int id)
{
    if (currentAddress.value("protocol") == "gmenu")
    {
        gmenu.aboutToShow(id);
        return;
    }
    const auto exporterId = exporterIds.constFind(id);
    if (currentService.isEmpty() || exporterId == exporterIds.cend())
    {
        return;
    }

    closeTimer.stop();
    closeMenusBelow(parents.value(id, ROOT_ITEM));
    openMenus.append(id);
    // mozilla only fills its menus on the event, changes come back as LayoutUpdated
    QDBusMessage call = QDBusMessage::createMethodCall(currentService, currentPath, MENU_INTERFACE, "AboutToShow");
    call << *exporterId;
    QDBusConnection::sessionBus().asyncCall(call);
    sendEvent(id, QStringLiteral("opened"));
}

void ApplicationMenu::sendEvent(int id, const QString& name)
{
    // every event needs a data value even when it means nothing
    const quint32 timestamp = static_cast<quint32>(QDateTime::currentSecsSinceEpoch());
    QDBusMessage call = QDBusMessage::createMethodCall(currentService, currentPath, MENU_INTERFACE, "Event");
    call << exporterIds.value(id) << name << QVariant::fromValue(QDBusVariant(QString())) << timestamp;
    QDBusConnection::sessionBus().asyncCall(call);
}

void ApplicationMenu::closeMenusBelow(int parent)
{
    while (!openMenus.isEmpty() && openMenus.last() != parent)
    {
        sendEvent(openMenus.takeLast(), QStringLiteral("closed"));
    }
}

void ApplicationMenu::menusClosed()
{
    if (!openMenus.isEmpty())
    {
        closeTimer.start();
    }
}

void ApplicationMenu::activate(int id)
{
    if (currentAddress.value("protocol") == "gmenu")
    {
        gmenu.activate(id);
        return;
    }
    if (currentService.isEmpty() || !exporterIds.contains(id))
    {
        return;
    }
    sendEvent(id, QStringLiteral("clicked"));
}
