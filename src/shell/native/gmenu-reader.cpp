#include <gio/gio.h>

#include "gmenu-reader.hpp"

#include <QMap>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

namespace
{
QString attribute(GMenuModel* model, int index, const char* name)
{
    gchar* value = nullptr;
    g_menu_model_get_item_attribute(model, index, name, "s", &value);
    const QString text = QString::fromUtf8(value);
    g_free(value);
    return text;
}

QString labelText(QString text)
{
    text.replace("__", QChar(1));
    text.remove('_');
    text.replace(QChar(1), '_');
    return text;
}

QStringList shortcutKeys(QString text)
{
    QStringList keys;
    static const QRegularExpression modifier("^<([^>]+)>");
    while (text.startsWith('<'))
    {
        const auto match = modifier.match(text);
        if (!match.hasMatch())
            return {};
        QString key = match.captured(1);
        if (key == "Primary" || key == "Ctrl")
            key = "Control";
        keys.append(key);
        text.remove(0, match.capturedLength());
    }
    if (!text.isEmpty())
        keys.append(text);
    return keys;
}
} // namespace

struct GMenuReader::State
{
    struct Action
    {
        QString name;
        GActionGroup* group;
        GVariant* target;
        QString submenuAction;
    };

    GMenuReader* owner;
    GDBusConnection* bus = nullptr;
    GMenuModel* root = nullptr;
    GMenuModel* applicationMenu = nullptr;
    GActionGroup* app = nullptr;
    GActionGroup* window = nullptr;
    GCancellable* pending = nullptr;
    QTimer rebuild;
    QTimer dispatch;
    QVariantMap address;
    QVariantList items;
    QSet<GMenuModel*> models;
    QMap<int, Action> actions;
    int nextId = 0;
    int generation = 0;
    int remaining = 0;

    explicit State(GMenuReader* reader) : owner(reader)
    {
        rebuild.setSingleShot(true);
        QObject::connect(&rebuild, &QTimer::timeout, owner, [this] { read(); });
        dispatch.setInterval(20);
        QObject::connect(&dispatch, &QTimer::timeout, owner, [] {
            for (int i = 0; i < 64 && g_main_context_pending(nullptr); ++i)
                g_main_context_iteration(nullptr, false);
        });
    }

    void clearItems()
    {
        for (auto* model : models)
        {
            g_signal_handlers_disconnect_by_data(model, this);
            g_object_unref(model);
        }
        models.clear();
        for (auto& action : actions)
            g_clear_pointer(&action.target, g_variant_unref);
        actions.clear();
    }

    void clear()
    {
        ++generation;
        rebuild.stop();
        dispatch.stop();
        if (pending)
            g_cancellable_cancel(pending);
        g_clear_object(&pending);
        clearItems();
        for (auto* group : {app, window})
            if (group)
                g_signal_handlers_disconnect_by_data(group, this);
        g_clear_object(&root);
        g_clear_object(&applicationMenu);
        g_clear_object(&app);
        g_clear_object(&window);
        g_clear_object(&bus);
        items.clear();
    }

    GActionGroup* groupFor(QString& name)
    {
        if (name.startsWith("app."))
        {
            name.remove(0, 4);
            return app;
        }
        if (name.startsWith("win."))
        {
            name.remove(0, 4);
            return window;
        }
        return nullptr;
    }

    QVariantList readModel(GMenuModel* model, const QString& prefix, QSet<GMenuModel*> ancestors)
    {
        if (ancestors.contains(model) || ancestors.size() >= 32)
            return {};
        ancestors.insert(model);
        if (!models.contains(model))
        {
            models.insert(G_MENU_MODEL(g_object_ref(model)));
            g_signal_connect(model, "items-changed", G_CALLBACK(+[](GMenuModel*, gint, gint, gint, gpointer data) {
                static_cast<State*>(data)->rebuild.start();
            }),
                this);
        }
        QVariantList result;
        for (int i = 0; i < g_menu_model_get_n_items(model) && remaining > 0; ++i)
        {
            --remaining;
            QString ns = attribute(model, i, "action-namespace");
            ns = ns.isEmpty() ? prefix : prefix + ns + '.';
            GMenuModel* section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
            if (section)
            {
                auto sectionItems = readModel(section, ns, ancestors);
                if (!result.isEmpty() && !sectionItems.isEmpty())
                    result.append(QVariantMap{{"id", ++nextId}, {"separator", true}, {"enabled", false}});
                result.append(sectionItems);
                g_object_unref(section);
                continue;
            }
            QString name = attribute(model, i, G_MENU_ATTRIBUTE_ACTION);
            if (!name.isEmpty())
                name.prepend(prefix);
            auto* group = groupFor(name);
            bool exists = group && g_action_group_has_action(group, name.toUtf8().constData());
            bool enabled = exists && g_action_group_get_action_enabled(group, name.toUtf8().constData());
            const QString hidden = attribute(model, i, "hidden-when");
            if ((hidden == "action-missing" && !exists) || (hidden == "action-disabled" && !enabled))
                continue;
            GVariant* target = g_menu_model_get_item_attribute_value(model, i, G_MENU_ATTRIBUTE_TARGET, nullptr);
            GVariant* value = exists ? g_action_group_get_action_state(group, name.toUtf8().constData()) : nullptr;
            bool checkable = value && (target || g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN));
            bool checked =
                value && (target ? g_variant_equal(value, target)
                                 : g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(value));
            g_clear_pointer(&value, g_variant_unref);
            GMenuModel* submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
            QVariantList children;
            if (submenu)
            {
                children = readModel(submenu, ns, ancestors);
                g_object_unref(submenu);
                enabled = true;
            }
            int id = ++nextId;
            actions.insert(id, {name, group, target, attribute(model, i, "submenu-action")});
            result.append(QVariantMap{{"id", id}, {"label", labelText(attribute(model, i, G_MENU_ATTRIBUTE_LABEL))},
                {"enabled", enabled}, {"separator", false}, {"checkable", checkable}, {"checked", checked},
                {"shortcut", shortcutKeys(attribute(model, i, "accel"))}, {"children", children}});
        }
        return result;
    }

    void read()
    {
        const auto previous = models;
        for (auto* model : previous)
            g_object_ref(model);
        clearItems();
        remaining = 4096;
        items = root ? readModel(root, {}, {}) : QVariantList{};
        if (address.value("appMenu") == address.value("path") && !items.isEmpty())
            items = {QVariantMap{{"id", ++nextId}, {"application", true}, {"label", "Application"}, {"enabled", true},
                {"children", items}}};
        else if (applicationMenu)
        {
            auto children = readModel(applicationMenu, {}, {});
            if (!children.isEmpty())
                items.prepend(QVariantMap{{"id", ++nextId}, {"application", true}, {"label", "Application"},
                    {"enabled", true}, {"children", children}});
        }
        for (auto* model : previous)
            g_object_unref(model);
        emit owner->itemsChanged();
    }

    void watch(GActionGroup* group)
    {
        if (!group)
            return;
        g_signal_connect(group, "action-added",
            G_CALLBACK(+[](GActionGroup*, const gchar*, gpointer data) { static_cast<State*>(data)->rebuild.start(); }),
            this);
        g_signal_connect(group, "action-removed",
            G_CALLBACK(+[](GActionGroup*, const gchar*, gpointer data) { static_cast<State*>(data)->rebuild.start(); }),
            this);
        g_signal_connect(group, "action-enabled-changed",
            G_CALLBACK(+[](GActionGroup*, const gchar*, gboolean, gpointer data) {
            static_cast<State*>(data)->rebuild.start();
        }),
            this);
        g_signal_connect(group, "action-state-changed",
            G_CALLBACK(+[](GActionGroup*, const gchar*, GVariant*, gpointer data) {
            static_cast<State*>(data)->rebuild.start();
        }),
            this);
        g_strfreev(g_action_group_list_actions(group));
    }

    void connected()
    {
        auto service = address.value("service").toString().toUtf8();
        auto path = address.value("path").toString().toUtf8();
        root = G_MENU_MODEL(g_dbus_menu_model_get(bus, service.constData(), path.constData()));
        auto appMenuPath = address.value("appMenu").toString().toUtf8();
        if (appMenuPath != path && g_variant_is_object_path(appMenuPath.constData()))
            applicationMenu = G_MENU_MODEL(g_dbus_menu_model_get(bus, service.constData(), appMenuPath.constData()));
        auto appPath = address.value("appActions").toString().toUtf8();
        auto windowPath = address.value("windowActions").toString().toUtf8();
        if (g_variant_is_object_path(appPath.constData()))
            app = G_ACTION_GROUP(g_dbus_action_group_get(bus, service.constData(), appPath.constData()));
        if (g_variant_is_object_path(windowPath.constData()))
            window = G_ACTION_GROUP(g_dbus_action_group_get(bus, service.constData(), windowPath.constData()));
        watch(app);
        watch(window);
        read();
    }
};

GMenuReader::GMenuReader(QObject* parent) : QObject(parent), state(std::make_unique<State>(this)) {}
GMenuReader::~GMenuReader()
{
    state->clear();
}
QVariantList GMenuReader::items() const
{
    return state->items;
}

void GMenuReader::follow(const QVariantMap& address)
{
    state->clear();
    state->address = address;
    emit itemsChanged();
    if (address.value("protocol") != "gmenu" ||
        !g_dbus_is_name(address.value("service").toString().toUtf8().constData()) ||
        !g_variant_is_object_path(address.value("path").toString().toUtf8().constData()))
        return;
    state->pending = g_cancellable_new();
    struct Request
    {
        QPointer<GMenuReader> reader;
        int generation;
    };
    auto* request = new Request{this, state->generation};
    state->dispatch.start();
    g_bus_get(G_BUS_TYPE_SESSION, state->pending, +[](GObject*, GAsyncResult* result, gpointer data) {
        std::unique_ptr<Request> request(static_cast<Request*>(data));
        GDBusConnection* bus = g_bus_get_finish(result, nullptr);
        if (bus && request->reader && request->reader->state->generation == request->generation)
        {
            request->reader->state->bus = bus;
            request->reader->state->connected();
        }
        else
            g_clear_object(&bus);
    }, request);
}

void GMenuReader::activate(int id)
{
    const auto found = state->actions.constFind(id);
    if (found == state->actions.cend() || !found->group)
        return;
    auto name = found->name.toUtf8();
    if (!g_action_group_get_action_enabled(found->group, name.constData()))
        return;
    const GVariantType* type = g_action_group_get_action_parameter_type(found->group, name.constData());
    if ((type && (!found->target || !g_variant_is_of_type(found->target, type))) || (!type && found->target))
        return;
    g_action_group_activate_action(found->group, name.constData(), found->target);
}

void GMenuReader::aboutToShow(int id)
{
    const auto found = state->actions.constFind(id);
    if (found == state->actions.cend())
        return;
    auto name = found->submenuAction;
    auto* group = state->groupFor(name);
    if (group && g_action_group_has_action(group, name.toUtf8().constData()))
        g_action_group_change_action_state(group, name.toUtf8().constData(), g_variant_new_boolean(true));
}
