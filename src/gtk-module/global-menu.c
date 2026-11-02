#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <libdbusmenu-glib/server.h>
#include <libdbusmenu-gtk/parser.h>

#include "appmenu-client-protocol.h"

#define REGISTRAR "com.canonical.AppMenu.Registrar"

typedef struct
{
    GtkWidget* bar;
    DbusmenuServer* server;
    struct org_kde_kwin_appmenu* appmenu;
    struct wl_surface* surface;
    gchar* path;
    gboolean published;
} WindowMenu;

static struct org_kde_kwin_appmenu_manager* manager;
static gboolean registrar_present;
static GSList* windows;
static guint next_menu;

static void on_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    (void)data;
    if (g_strcmp0(interface, org_kde_kwin_appmenu_manager_interface.name) == 0)
    {
        manager = wl_registry_bind(registry, name, &org_kde_kwin_appmenu_manager_interface, MIN(version, 1));
    }
}

static void on_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {on_global, on_global_remove};

static GtkWidget* find_bar(GtkWidget* widget)
{
    if (GTK_IS_MENU_BAR(widget))
    {
        return widget;
    }
    if (!GTK_IS_CONTAINER(widget))
    {
        return NULL;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget* found = NULL;
    for (GList* child = children; child && !found; child = child->next)
    {
        found = find_bar(child->data);
    }
    g_list_free(children);
    return found;
}

// the app shows its bar again on its own, after fullscreen for one
static void keep_hidden(GtkWidget* bar, GParamSpec* spec, WindowMenu* menu)
{
    (void)spec;
    if (menu->published && gtk_widget_get_visible(bar))
    {
        gtk_widget_hide(bar);
    }
}

static void announce(WindowMenu* menu, GtkWidget* window)
{
    GdkWindow* gdk_window = gtk_widget_get_window(window);
    struct wl_surface* surface = gdk_window ? gdk_wayland_window_get_wl_surface(gdk_window) : NULL;
    if (!manager || !surface || surface == menu->surface)
    {
        return;
    }
    g_clear_pointer(&menu->appmenu, org_kde_kwin_appmenu_release);
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!bus)
    {
        return;
    }
    menu->surface = surface;
    menu->appmenu = org_kde_kwin_appmenu_manager_create(manager, surface);
    org_kde_kwin_appmenu_set_address(menu->appmenu, g_dbus_connection_get_unique_name(bus), menu->path);
    g_object_unref(bus);
    gdk_display_flush(gdk_display_get_default());
}

static void publish(WindowMenu* menu)
{
    if (!registrar_present || !menu->bar)
    {
        return;
    }
    if (!menu->server)
    {
        menu->path = g_strdup_printf("/com/canonical/menu/%u", next_menu++);
        menu->server = dbusmenu_server_new(menu->path);
        DbusmenuMenuitem* root = dbusmenu_gtk_parse_menu_structure(menu->bar);
        dbusmenu_server_set_root(menu->server, root);
        g_object_unref(root);
    }
    menu->published = TRUE;
    gtk_widget_hide(menu->bar);
    announce(menu, gtk_widget_get_toplevel(menu->bar));
}

static void withdraw(WindowMenu* menu)
{
    menu->published = FALSE;
    if (menu->bar)
    {
        gtk_widget_show(menu->bar);
    }
}

static void forget_window(gpointer data, GObject* window)
{
    (void)window;
    WindowMenu* menu = data;
    windows = g_slist_remove(windows, menu);
    g_clear_pointer(&menu->appmenu, org_kde_kwin_appmenu_release);
    g_clear_object(&menu->server);
    g_free(menu->path);
    g_free(menu);
}

static gboolean on_map(GSignalInvocationHint* hint, guint count, const GValue* values, gpointer data)
{
    (void)hint;
    (void)count;
    (void)data;
    GtkWidget* widget = g_value_get_object(&values[0]);
    if (!GTK_IS_WINDOW(widget) || !GDK_IS_WAYLAND_WINDOW(gtk_widget_get_window(widget)))
    {
        return TRUE;
    }
    WindowMenu* menu = g_object_get_data(G_OBJECT(widget), "mavernix-global-menu");
    if (!menu)
    {
        GtkWidget* bar = find_bar(widget);
        if (!bar)
        {
            return TRUE;
        }
        menu = g_new0(WindowMenu, 1);
        menu->bar = bar;
        g_object_add_weak_pointer(G_OBJECT(bar), (gpointer*)&menu->bar);
        g_signal_connect(bar, "notify::visible", G_CALLBACK(keep_hidden), menu);
        g_object_set_data(G_OBJECT(widget), "mavernix-global-menu", menu);
        g_object_weak_ref(G_OBJECT(widget), forget_window, menu);
        windows = g_slist_prepend(windows, menu);
    }
    menu->surface = NULL;
    publish(menu);
    return TRUE;
}

static void on_registrar(GDBusConnection* bus, const gchar* name, const gchar* owner, gpointer data)
{
    (void)bus;
    (void)name;
    (void)owner;
    (void)data;
    registrar_present = TRUE;
    for (GSList* item = windows; item; item = item->next)
    {
        publish(item->data);
    }
}

static void on_registrar_gone(GDBusConnection* bus, const gchar* name, gpointer data)
{
    (void)bus;
    (void)name;
    (void)data;
    registrar_present = FALSE;
    for (GSList* item = windows; item; item = item->next)
    {
        withdraw(item->data);
    }
}

G_MODULE_EXPORT void gtk_module_init(gint* argc, gchar*** argv)
{
    (void)argc;
    (void)argv;
    GdkDisplay* display = gdk_display_get_default();
    if (!GDK_IS_WAYLAND_DISPLAY(display))
    {
        return;
    }
    struct wl_display* wayland = gdk_wayland_display_get_wl_display(display);
    struct wl_registry* registry = wl_display_get_registry(wayland);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wayland);
    wl_registry_destroy(registry);
    if (!manager)
    {
        return;
    }
    // the registrar is what says a menu bar is there to show the menus
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (bus)
    {
        GVariant* owner = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetNameOwner", g_variant_new("(s)", REGISTRAR), NULL, G_DBUS_CALL_FLAGS_NONE,
            1000, NULL, NULL);
        registrar_present = owner != NULL;
        g_clear_pointer(&owner, g_variant_unref);
        g_object_unref(bus);
    }
    g_bus_watch_name(G_BUS_TYPE_SESSION, REGISTRAR, G_BUS_NAME_WATCHER_FLAGS_NONE, on_registrar, on_registrar_gone, NULL,
        NULL);
    g_type_class_ref(GTK_TYPE_WIDGET);
    g_signal_add_emission_hook(g_signal_lookup("map", GTK_TYPE_WIDGET), 0, on_map, NULL, NULL);
}
