#include <gdk/gdkwayland.h>
#include <gtk/gtk.h>
#include <libdbusmenu-glib/server.h>
#include <libdbusmenu-gtk/parser.h>

#include "appmenu-client-protocol.h"

#define REGISTRAR "com.canonical.AppMenu.Registrar"
#define WINDOW_MENU "mavernix-global-menu"
#define EXPORTED_BAR "mavernix-exported-bar"

typedef struct
{
    GtkWidget* window;
    GtkWidget* bar;
    DbusmenuServer* server;
    struct org_kde_kwin_appmenu* appmenu;
    struct wl_callback* acknowledgement;
    gchar* path;
    guint generation;
    guint retries;
    guint retry_source;
} WindowMenu;

typedef struct
{
    GWeakRef window;
    guint generation;
} PendingExport;

static struct org_kde_kwin_appmenu_manager* manager;
static GDBusConnection* session_bus;
static gboolean registrar_present;
static guint refresh_source;
static guint next_menu;
static GtkWidgetClass original_bar;

static void queue_refresh(void);
static void verify_export(WindowMenu* menu);

static gboolean exported(GtkWidget* bar)
{
    return g_object_get_data(G_OBJECT(bar), EXPORTED_BAR) != NULL;
}

static void set_exported(GtkWidget* bar, gboolean active)
{
    if (exported(bar) == active)
        return;
    g_object_set_data(G_OBJECT(bar), EXPORTED_BAR, GINT_TO_POINTER(active));
    gtk_widget_queue_resize(bar);
    gtk_widget_queue_draw(bar);
}

static void preferred_width(GtkWidget* bar, gint* minimum, gint* natural)
{
    if (exported(bar))
        *minimum = *natural = 0;
    else
        original_bar.get_preferred_width(bar, minimum, natural);
}

static void preferred_height(GtkWidget* bar, gint* minimum, gint* natural)
{
    if (exported(bar))
        *minimum = *natural = 0;
    else
        original_bar.get_preferred_height(bar, minimum, natural);
}

static void width_for_height(GtkWidget* bar, gint height, gint* minimum, gint* natural)
{
    if (exported(bar))
        *minimum = *natural = 0;
    else
        original_bar.get_preferred_width_for_height(bar, height, minimum, natural);
}

static void height_for_width(GtkWidget* bar, gint width, gint* minimum, gint* natural)
{
    if (exported(bar))
        *minimum = *natural = 0;
    else
        original_bar.get_preferred_height_for_width(bar, width, minimum, natural);
}

static void allocate_bar(GtkWidget* bar, GtkAllocation* allocation)
{
    if (exported(bar))
    {
        // Keep the bar mapped for GTK's accelerators without allocating its children
        GtkAllocation empty = {allocation->x, allocation->y, 1, 1};
        gtk_widget_set_allocation(bar, &empty);
        gtk_widget_set_clip(bar, &empty);
    }
    else
        original_bar.size_allocate(bar, allocation);
}

static gboolean draw_bar(GtkWidget* bar, cairo_t* cr)
{
    return exported(bar) || original_bar.draw(bar, cr);
}

static void withdraw(WindowMenu* menu)
{
    menu->generation++;
    if (menu->retry_source)
        g_source_remove(menu->retry_source);
    menu->retry_source = 0;
    if (menu->bar)
        set_exported(menu->bar, FALSE);
    g_clear_pointer(&menu->acknowledgement, wl_callback_destroy);
    if (menu->appmenu)
        org_kde_kwin_appmenu_set_address(menu->appmenu, "", "");
    g_clear_pointer(&menu->appmenu, org_kde_kwin_appmenu_release);
    g_clear_object(&menu->server);
    g_clear_object(&menu->bar);
}

static void forget_window(gpointer data)
{
    WindowMenu* menu = data;
    withdraw(menu);
    g_free(menu->path);
    g_free(menu);
}

static void acknowledged(void* data, struct wl_callback* callback, uint32_t serial)
{
    (void)serial;
    WindowMenu* menu = data;
    wl_callback_destroy(callback);
    menu->acknowledgement = NULL;
    if (registrar_present && menu->bar && gtk_widget_get_mapped(menu->window))
        set_exported(menu->bar, TRUE);
}

static const struct wl_callback_listener acknowledgement_listener = {acknowledged};

static gboolean retry_export(gpointer data)
{
    GtkWidget* window = data;
    WindowMenu* menu = g_object_get_data(G_OBJECT(window), WINDOW_MENU);
    menu->retry_source = 0;
    if (registrar_present && gtk_widget_get_mapped(window))
        verify_export(menu);
    return G_SOURCE_REMOVE;
}

static void export_ready(GObject* object, GAsyncResult* result, gpointer data)
{
    PendingExport* pending = data;
    GError* error = NULL;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    GtkWidget* window = g_weak_ref_get(&pending->window);
    WindowMenu* menu = window ? g_object_get_data(G_OBJECT(window), WINDOW_MENU) : NULL;
    if (menu && menu->generation == pending->generation && registrar_present && gtk_widget_get_mapped(window))
    {
        if (!reply)
        {
            if (menu->retries++ < 5)
                menu->retry_source =
                    g_timeout_add_full(G_PRIORITY_DEFAULT, 100, retry_export, g_object_ref(window), g_object_unref);
            else
            {
                g_warning("[global-menu] export unavailable path=%s error=%s", menu->path, error->message);
                withdraw(menu);
            }
        }
        else
        {
            GdkWindow* gdk_window = gtk_widget_get_window(window);
            struct wl_surface* surface = gdk_wayland_window_get_wl_surface(gdk_window);
            if (surface)
            {
                menu->appmenu = org_kde_kwin_appmenu_manager_create(manager, surface);
                org_kde_kwin_appmenu_set_address(
                    menu->appmenu, g_dbus_connection_get_unique_name(session_bus), menu->path);
                struct wl_display* display = gdk_wayland_display_get_wl_display(gtk_widget_get_display(window));
                menu->acknowledgement = wl_display_sync(display);
                wl_callback_add_listener(menu->acknowledgement, &acknowledgement_listener, menu);
                wl_display_flush(display);
            }
        }
    }
    g_clear_error(&error);
    g_clear_pointer(&reply, g_variant_unref);
    g_clear_object(&window);
    g_weak_ref_clear(&pending->window);
    g_free(pending);
}

static void verify_export(WindowMenu* menu)
{
    PendingExport* pending = g_new0(PendingExport, 1);
    g_weak_ref_init(&pending->window, menu->window);
    pending->generation = menu->generation;
    g_dbus_connection_call(session_bus, g_dbus_connection_get_unique_name(session_bus), menu->path,
        "com.canonical.dbusmenu", "GetLayout", g_variant_new("(ii@as)", 0, 0, g_variant_new_strv(NULL, 0)), NULL,
        G_DBUS_CALL_FLAGS_NONE, 1000, NULL, export_ready, pending);
}

static void find_bars(GtkWidget* widget, GPtrArray* bars)
{
    if (!gtk_widget_get_visible(widget) || gtk_widget_in_destruction(widget))
        return;
    if (GTK_IS_MENU_BAR(widget))
    {
        g_ptr_array_add(bars, widget);
        return;
    }
    if (GTK_IS_CONTAINER(widget))
    {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* child = children; child; child = child->next)
            find_bars(child->data, bars);
        g_list_free(children);
    }
}

static void refresh_window(GtkWidget* window)
{
    WindowMenu* menu = g_object_get_data(G_OBJECT(window), WINDOW_MENU);
    GdkWindow* gdk_window = gtk_widget_get_window(window);
    if (!registrar_present || !gtk_widget_get_mapped(window) || !GDK_IS_WAYLAND_WINDOW(gdk_window))
    {
        if (menu)
            withdraw(menu);
        return;
    }
    GPtrArray* bars = g_ptr_array_new();
    find_bars(window, bars);
    GtkWidget* bar = bars->len == 1 ? g_ptr_array_index(bars, 0) : NULL;
    g_ptr_array_unref(bars);
    if (menu && menu->bar == bar)
        return;
    if (menu)
        withdraw(menu);
    if (!bar)
        return;
    if (!menu)
    {
        menu = g_new0(WindowMenu, 1);
        menu->window = window;
        menu->path = g_strdup_printf("/com/canonical/menu/%u", next_menu++);
        g_object_set_data_full(G_OBJECT(window), WINDOW_MENU, menu, forget_window);
    }
    menu->bar = g_object_ref(bar);
    menu->server = dbusmenu_server_new(menu->path);
    DbusmenuMenuitem* root = dbusmenu_gtk_parse_menu_structure(bar);
    if (!root)
    {
        withdraw(menu);
        return;
    }
    dbusmenu_server_set_root(menu->server, root);
    g_object_unref(root);
    menu->retries = 0;
    verify_export(menu);
}

static gboolean refresh(gpointer data)
{
    (void)data;
    refresh_source = 0;
    GList* windows = gtk_window_list_toplevels();
    for (GList* item = windows; item; item = item->next)
        refresh_window(item->data);
    g_list_free(windows);
    return G_SOURCE_REMOVE;
}

static void queue_refresh(void)
{
    if (!refresh_source)
        refresh_source = g_idle_add(refresh, NULL);
}

static gboolean on_widget_change(GSignalInvocationHint* hint, guint count, const GValue* values, gpointer data)
{
    (void)count;
    (void)data;
    GtkWidget* widget = g_value_get_object(values);
    if (GTK_IS_CONTAINER(widget))
    {
        if (GTK_IS_WINDOW(widget) && (hint->signal_id == g_signal_lookup("unmap", GTK_TYPE_WIDGET)))
        {
            WindowMenu* menu = g_object_get_data(G_OBJECT(widget), WINDOW_MENU);
            if (menu)
                withdraw(menu);
        }
        queue_refresh();
    }
    return TRUE;
}

static void on_registrar(GDBusConnection* bus, const gchar* name, const gchar* owner, gpointer data)
{
    (void)name;
    (void)owner;
    (void)data;
    g_set_object(&session_bus, bus);
    registrar_present = TRUE;
    queue_refresh();
}

static void on_registrar_gone(GDBusConnection* bus, const gchar* name, gpointer data)
{
    (void)bus;
    (void)name;
    (void)data;
    registrar_present = FALSE;
    if (refresh_source)
        g_source_remove(refresh_source);
    refresh(NULL);
    g_clear_object(&session_bus);
}

static void on_global(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
    (void)data;
    if (!manager && g_strcmp0(interface, org_kde_kwin_appmenu_manager_interface.name) == 0)
        manager = wl_registry_bind(registry, name, &org_kde_kwin_appmenu_manager_interface, MIN(version, 1));
}

static void on_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {on_global, on_global_remove};

G_MODULE_EXPORT const gchar* g_module_check_init(GModule* module)
{
    g_module_make_resident(module);
    return NULL;
}

G_MODULE_EXPORT void gtk_module_init(gint* argc, gchar*** argv)
{
    (void)argc;
    (void)argv;
    static gboolean initialized;
    GdkDisplay* display = gdk_display_get_default();
    if (initialized || !GDK_IS_WAYLAND_DISPLAY(display) || g_strcmp0(g_getenv("MAVERNIX_GLOBAL_MENU"), "0") == 0)
        return;
    initialized = TRUE;
    struct wl_display* wayland = gdk_wayland_display_get_wl_display(display);
    struct wl_registry* registry = wl_display_get_registry(wayland);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(wayland);
    wl_registry_destroy(registry);
    if (!manager)
        return;
    GtkWidgetClass* bar_class = g_type_class_ref(GTK_TYPE_MENU_BAR);
    original_bar = *bar_class;
    bar_class->get_preferred_width = preferred_width;
    bar_class->get_preferred_height = preferred_height;
    bar_class->get_preferred_width_for_height = width_for_height;
    bar_class->get_preferred_height_for_width = height_for_width;
    bar_class->size_allocate = allocate_bar;
    bar_class->draw = draw_bar;
    const gchar* signals[] = {"map", "unmap", "show", "hide", "hierarchy-changed", "parent-set"};
    for (guint i = 0; i < G_N_ELEMENTS(signals); i++)
        g_signal_add_emission_hook(g_signal_lookup(signals[i], GTK_TYPE_WIDGET), 0, on_widget_change, NULL, NULL);
    g_bus_watch_name(
        G_BUS_TYPE_SESSION, REGISTRAR, G_BUS_NAME_WATCHER_FLAGS_NONE, on_registrar, on_registrar_gone, NULL, NULL);
}
