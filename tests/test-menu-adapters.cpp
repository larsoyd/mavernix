#include <gio/gio.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QLocalServer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "application-menu.hpp"
#include "compositor-socket.hpp"
#include "menu-registrar.hpp"

namespace
{
const char* DBUSMENU_XML = R"xml(<node><interface name="com.canonical.dbusmenu">
<method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/>
<arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
<method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
<method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
<signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal>
</interface></node>)xml";

// new ids on every rebuild like firefox
struct RebuildingMenu
{
    int base = 10;
    QString leaf = "Leaf";
    QStringList calls;
    GDBusConnection* bus = nullptr;

    void rebuild(const QString& newLeaf)
    {
        base += 10;
        leaf = newLeaf;
        g_dbus_connection_emit_signal(bus, nullptr, "/test/rebuilding", "com.canonical.dbusmenu", "LayoutUpdated",
            g_variant_new("(ui)", base, 0), nullptr);
    }

    GVariant* layout() const
    {
        const QString text = QString("(uint32 %1, (0, {'children-display': <'submenu'>}, ["
                                     "<(%2, {'label': <'_File'>, 'children-display': <'submenu'>}, ["
                                     "<(%3, {'label': <'Sub'>, 'children-display': <'submenu'>}, ["
                                     "<(%4, {'label': <'%5'>}, @av [])>])>,"
                                     "<(%6, {'label': <'Plain'>}, @av [])>])>]))")
                                 .arg(base).arg(base + 1).arg(base + 2).arg(base + 3).arg(leaf).arg(base + 4);
        return g_variant_new_parsed(text.toUtf8().constData());
    }

    static void called(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
        GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data)
    {
        auto* menu = static_cast<RebuildingMenu*>(data);
        if (g_str_equal(method, "GetLayout"))
        {
            g_dbus_method_invocation_return_value(invocation, menu->layout());
            return;
        }
        gint32 id = 0;
        if (g_str_equal(method, "AboutToShow"))
        {
            g_variant_get(parameters, "(i)", &id);
            menu->calls << QString("AboutToShow %1").arg(id);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }
        const gchar* name = nullptr;
        g_variant_get(parameters, "(i&svu)", &id, &name, nullptr, nullptr);
        menu->calls << QString("%1 %2").arg(name).arg(id);
        if (id > menu->base + 4 || id <= menu->base)
        {
            g_dbus_method_invocation_return_dbus_error(invocation, "test.Stale", "no such id");
            return;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }
};
} // namespace

class TestMenuAdapters : public QObject
{
    Q_OBJECT

  private slots:
    void connectedSocketDestruction()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QLocalServer server;
        QVERIFY(server.listen(directory.filePath("ipc")));
        qputenv("WAYFIRE_SOCKET", server.fullServerName().toUtf8());
        int disconnected = 0;
        {
            CompositorSocket socket;
            connect(&socket, &CompositorSocket::disconnected, this, [&] { ++disconnected; });
            socket.open();
            QTRY_VERIFY_WITH_TIMEOUT(socket.isConnected(), 1000);
            socket.request("pending", {}, [](const QJsonObject&) {});
        }
        QCOMPARE(disconnected, 0);
    }

    void remoteMenuAndActions()
    {
        GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        QVERIFY(bus);
        GMenu* root = g_menu_new();
        GMenu* submenu = g_menu_new();
        g_menu_append(submenu, "_Run", "app.run");
        GMenuItem* toggleItem = g_menu_item_new("Toggle", "win.toggle");
        g_menu_item_set_attribute(toggleItem, "accel", "s", "<Primary><Shift>t");
        g_menu_append_item(submenu, toggleItem);
        g_object_unref(toggleItem);
        g_menu_append_submenu(root, "_File", G_MENU_MODEL(submenu));
        GSimpleActionGroup* actions = g_simple_action_group_new();
        GSimpleAction* action = g_simple_action_new("run", nullptr);
        int activations = 0;
        g_signal_connect(action, "activate",
            G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) { ++*static_cast<int*>(data); }), &activations);
        g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(action));
        GSimpleActionGroup* windowActions = g_simple_action_group_new();
        GSimpleAction* toggle = g_simple_action_new_stateful("toggle", nullptr, g_variant_new_boolean(false));
        g_signal_connect(toggle, "activate", G_CALLBACK(+[](GSimpleAction* action, GVariant*, gpointer) {
            g_simple_action_set_state(action, g_variant_new_boolean(true));
        }),
            nullptr);
        g_action_map_add_action(G_ACTION_MAP(windowActions), G_ACTION(toggle));
        guint modelId = g_dbus_connection_export_menu_model(bus, "/test/menu", G_MENU_MODEL(root), nullptr);
        guint actionId = g_dbus_connection_export_action_group(bus, "/test/actions", G_ACTION_GROUP(actions), nullptr);
        guint windowId =
            g_dbus_connection_export_action_group(bus, "/test/window", G_ACTION_GROUP(windowActions), nullptr);
        QVERIFY(modelId && actionId);
        ApplicationMenu menu;
        QVERIFY(menu.setProperty("address",
            QVariantMap{{"protocol", "gmenu"}, {"service", g_dbus_connection_get_unique_name(bus)},
                {"path", "/test/menu"}, {"appActions", "/test/actions"}, {"windowActions", "/test/window"}}));
        QTRY_VERIFY_WITH_TIMEOUT(!menu.items().isEmpty(), 3000);
        auto children = [&menu] { return menu.items().first().toMap().value("children").toList(); };
        QTRY_COMPARE_WITH_TIMEOUT(children().size(), 2, 3000);
        QCOMPARE(menu.items().first().toMap().value("label").toString(), "File");
        QTRY_VERIFY_WITH_TIMEOUT(children().first().toMap().value("enabled").toBool(), 3000);
        menu.activate(children().first().toMap().value("id").toInt());
        QTRY_COMPARE_WITH_TIMEOUT(activations, 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(children().at(1).toMap().value("checkable").toBool(), 3000);
        QCOMPARE(children().at(1).toMap().value("shortcut").toStringList(), QStringList({"Control", "Shift", "t"}));
        QVERIFY(!children().at(1).toMap().value("checked").toBool());
        menu.activate(children().at(1).toMap().value("id").toInt());
        QTRY_VERIFY_WITH_TIMEOUT(children().at(1).toMap().value("checked").toBool(), 3000);
        g_simple_action_set_enabled(action, false);
        QTRY_VERIFY_WITH_TIMEOUT(!children().first().toMap().value("enabled").toBool(), 3000);
        menu.activate(children().first().toMap().value("id").toInt());
        QTest::qWait(50);
        QCOMPARE(activations, 1);
        g_menu_append(submenu, "Second", "app.run");
        QTRY_COMPARE_WITH_TIMEOUT(children().size(), 3, 3000);
        const int stale = children().at(1).toMap().value("id").toInt();
        auto appOnly = menu.address();
        appOnly.insert("appMenu", "/test/menu");
        menu.setAddress(appOnly);
        QTRY_VERIFY_WITH_TIMEOUT(
            !menu.items().isEmpty() && menu.items().first().toMap().value("application").toBool(), 3000);
        menu.activate(stale);
        QVERIFY(menu.setProperty("address", QVariantMap{}));
        QVERIFY(menu.items().isEmpty());
        g_dbus_connection_unexport_menu_model(bus, modelId);
        g_dbus_connection_unexport_action_group(bus, actionId);
        g_dbus_connection_unexport_action_group(bus, windowId);
        g_object_unref(toggle);
        g_object_unref(windowActions);
        g_object_unref(action);
        g_object_unref(actions);
        g_object_unref(submenu);
        g_object_unref(root);
        g_object_unref(bus);
    }

    void rebuildsKeepItemsAndOpenMenus()
    {
        GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        QVERIFY(bus);
        RebuildingMenu exporter;
        exporter.bus = bus;
        GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(DBUSMENU_XML, nullptr);
        QVERIFY(node);
        const GDBusInterfaceVTable table = {RebuildingMenu::called, nullptr, nullptr, {}};
        const guint object = g_dbus_connection_register_object(
            bus, "/test/rebuilding", node->interfaces[0], &table, &exporter, nullptr, nullptr);
        QVERIFY(object);

        ApplicationMenu menu;
        QSignalSpy changes(&menu, &ApplicationMenu::itemsChanged);
        menu.setAddress(QVariantMap{{"protocol", "dbusmenu"}, {"service", g_dbus_connection_get_unique_name(bus)},
            {"path", "/test/rebuilding"}});
        QTRY_VERIFY_WITH_TIMEOUT(!menu.items().isEmpty(), 3000);
        const QVariantMap top = menu.items().first().toMap();
        const QVariantMap sub = top.value("children").toList().first().toMap();
        const QVariantMap plain = top.value("children").toList().last().toMap();
        const int topId = top.value("id").toInt();
        const int subId = sub.value("id").toInt();
        const int plainId = plain.value("id").toInt();
        const QVariantList before = menu.items();

        menu.aboutToShow(topId);
        QTRY_COMPARE_WITH_TIMEOUT(exporter.calls, QStringList({"AboutToShow 11", "opened 11"}), 3000);

        const int count = changes.count();
        exporter.rebuild("Leaf");
        QTest::qWait(100);
        QCOMPARE(menu.items(), before);
        QCOMPARE(changes.count(), count);

        menu.aboutToShow(subId);
        QTRY_COMPARE_WITH_TIMEOUT(exporter.calls.mid(2), QStringList({"AboutToShow 22", "opened 22"}), 3000);
        exporter.rebuild("New leaf");
        QTRY_COMPARE_WITH_TIMEOUT(changes.count(), count + 1, 3000);
        const QVariantMap after = menu.items().first().toMap();
        QCOMPARE(after.value("id").toInt(), topId);
        QCOMPARE(after.value("children").toList().first().toMap().value("id").toInt(), subId);

        menu.menusClosed();
        menu.activate(plainId);
        QTRY_COMPARE_WITH_TIMEOUT(
            exporter.calls.mid(4), QStringList({"clicked 34", "closed 32", "closed 31"}), 3000);

        exporter.calls.clear();
        menu.menusClosed();
        QTest::qWait(50);
        QCOMPARE(exporter.calls, QStringList());

        menu.aboutToShow(topId);
        menu.aboutToShow(subId);
        QTRY_COMPARE_WITH_TIMEOUT(exporter.calls.size(), 4, 3000);
        exporter.calls.clear();
        menu.aboutToShow(topId);
        QTRY_COMPARE_WITH_TIMEOUT(
            exporter.calls, QStringList({"closed 32", "closed 31", "AboutToShow 31", "opened 31"}), 3000);

        exporter.calls.clear();
        menu.menusClosed();
        menu.aboutToShow(topId);
        QTRY_COMPARE_WITH_TIMEOUT(
            exporter.calls, QStringList({"closed 31", "AboutToShow 31", "opened 31"}), 3000);

        exporter.calls.clear();
        menu.setAddress(QVariantMap{});
        QTRY_COMPARE_WITH_TIMEOUT(exporter.calls, QStringList({"closed 31"}), 3000);

        g_dbus_connection_unregister_object(bus, object);
        g_dbus_node_info_unref(node);
        g_object_unref(bus);
    }

    void registrarAcceptsWindowRegistration()
    {
        MenuRegistrar registrar;
        QVERIFY(registrar.isRegistered());
        const auto caller = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "test-registrant");
        auto invoke = [&caller](const QString& method, const QVariantList& args) {
            auto message = QDBusMessage::createMethodCall("com.canonical.AppMenu.Registrar",
                "/com/canonical/AppMenu/Registrar", "com.canonical.AppMenu.Registrar", method);
            message.setArguments(args);
            return new QDBusPendingCallWatcher(caller.asyncCall(message, 2000));
        };
        auto* registered = invoke("RegisterWindow", {42u, QVariant::fromValue(QDBusObjectPath("/test/menu"))});
        QTRY_VERIFY_WITH_TIMEOUT(registered->isFinished(), 3000);
        QCOMPARE(registered->reply().type(), QDBusMessage::ReplyMessage);
        delete registered;
        auto* found = invoke("GetMenuForWindow", {42u});
        QTRY_VERIFY_WITH_TIMEOUT(found->isFinished(), 3000);
        QCOMPARE(found->reply().arguments().value(0).toString(), caller.baseService());
        QCOMPARE(found->reply().arguments().value(1).value<QDBusObjectPath>().path(), "/test/menu");
        delete found;
        const auto intruder = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "test-intruder");
        auto message = QDBusMessage::createMethodCall("com.canonical.AppMenu.Registrar",
            "/com/canonical/AppMenu/Registrar", "com.canonical.AppMenu.Registrar", "UnregisterWindow");
        message << 42u;
        QDBusPendingCallWatcher denied(intruder.asyncCall(message, 2000));
        QTRY_VERIFY_WITH_TIMEOUT(denied.isFinished(), 3000);
        QCOMPARE(denied.reply().errorName(), "org.freedesktop.DBus.Error.AccessDenied");
        auto* all = invoke("GetMenus", {});
        QTRY_VERIFY_WITH_TIMEOUT(all->isFinished(), 3000);
        auto menus = qdbus_cast<RegisteredMenus>(all->reply().arguments().value(0));
        QCOMPARE(menus.size(), 1);
        QCOMPARE(menus.first().window, 42u);
        delete all;
        auto* removed = invoke("UnregisterWindow", {42u});
        QTRY_VERIFY_WITH_TIMEOUT(removed->isFinished(), 3000);
        delete removed;
        found = invoke("GetMenuForWindow", {42u});
        QTRY_VERIFY_WITH_TIMEOUT(found->isFinished(), 3000);
        QCOMPARE(found->reply().arguments().value(0).toString(), QString());
        delete found;
        QDBusConnection::disconnectFromBus("test-registrant");
    }
};

QTEST_GUILESS_MAIN(TestMenuAdapters)
#include "test-menu-adapters.moc"
