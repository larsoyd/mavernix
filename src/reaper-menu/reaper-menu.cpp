#include <dlfcn.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <libdbusmenu-glib/menuitem.h>
#include <libdbusmenu-glib/server.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "menu-text.hpp"

namespace
{
typedef void* HMENU;
typedef void* HWND;

// these three mirror swell's and reaper's own headers
struct MenuItemInfo
{
    unsigned size, mask, type, state, id;
    HMENU submenu;
    void* checked;
    void* unchecked;
    unsigned long data;
    char* text;
    int length;
    void* picture;
};

struct PluginInfo
{
    int version;
    HWND mainWindow;
    int (*add)(const char*, void*);
    void* (*function)(const char*);
};

enum
{
    MASK_ID = 1,
    MASK_STATE = 2,
    MASK_TYPE = 4,
    MASK_SUBMENU = 8,
    STATE_GRAYED = 1,
    STATE_DISABLED = 2,
    STATE_CHECKED = 8,
    TYPE_SEPARATOR = 0x800,
    MESSAGE_COMMAND = 0x111,
    MESSAGE_FILL_MENU = 0x117,
};

const char* const REGISTRAR = "com.canonical.AppMenu.Registrar";
const char* const REGISTRAR_PATH = "/com/canonical/AppMenu/Registrar";
const char* const THEME_FILE = "/libSwell-user.colortheme";
const char* const EMPTY_BAR = "menubar_height 0";
const int TICKS_BETWEEN_CHECKS = 10;
const int SECONDS_TO_WAIT_FOR_REGISTRAR = 15;

struct Swell
{
    HMENU (*GetMenu)(HWND);
    HMENU (*GetSubMenu)(HMENU, int);
    int (*GetMenuItemCount)(HMENU);
    int (*GetMenuItemInfo)(HMENU, int, int, MenuItemInfo*);
    long (*SendMessage)(HWND, unsigned, unsigned long, long);
    int (*PostMessage)(HWND, unsigned, unsigned long, long);
    void* (*SWELL_GetOSWindow)(HWND, const char*);
    int (*EnumWindows)(int (*)(HWND, long), long);
    int (*IsWindowVisible)(HWND);
    int (*IsWindowEnabled)(HWND);
} swell;

struct Export
{
    HMENU menu = nullptr;
    int topItems = 0;
    std::string path;
    DbusmenuServer* server = nullptr;
    std::string status;
    DbusmenuMenuitem* statusItem = nullptr;
};

struct Shown
{
    guint32 xid = 0;
    std::string path;
};

// a menu can be replaced under us, so items hold their way down from the root
struct ItemPlace
{
    HWND holder;
    std::vector<int> route;
    unsigned command;
};

HWND mainWindow;
int (*registerHook)(const char*, void*);
guint registrarWatch;
guint overdueTimeout;
std::string resourceFolder;
GDBusConnection* bus;
std::map<HWND, Export> exports;
std::map<HWND, Shown> shownWindows;
int nextExport;
int ticks;

void fill(DbusmenuMenuitem* parent, HWND holder, HMENU menu, const std::vector<int>& route);

void forgetPlace(gpointer place, GClosure*)
{
    delete static_cast<ItemPlace*>(place);
}

HMENU menuAt(const ItemPlace* place)
{
    HMENU menu = swell.GetMenu(place->holder);
    for (size_t step = 0; menu && step < place->route.size(); step++)
        menu = swell.GetSubMenu(menu, place->route[step]);
    return menu;
}

void activated(DbusmenuMenuitem*, guint, gpointer data)
{
    const auto* place = static_cast<ItemPlace*>(data);
    // a modal dialog has the window disabled and would not expect a command
    if (swell.IsWindowEnabled(place->holder))
        swell.PostMessage(place->holder, MESSAGE_COMMAND, place->command, 0);
    else
        fprintf(stderr, "[reaper-menu] command refused id=%u reason=window_disabled\n", place->command);
}

gboolean aboutToShow(DbusmenuMenuitem* item, gpointer data)
{
    const auto* place = static_cast<ItemPlace*>(data);
    HMENU menu = menuAt(place);
    if (!menu)
        return TRUE;
    swell.SendMessage(place->holder, MESSAGE_FILL_MENU, reinterpret_cast<unsigned long>(menu), 0);
    g_list_free_full(dbusmenu_menuitem_take_children(item), g_object_unref);
    fill(item, place->holder, menu, place->route);
    return TRUE;
}

void setShortcut(DbusmenuMenuitem* item, const std::vector<std::string>& keys)
{
    GVariantBuilder combination;
    g_variant_builder_init(&combination, G_VARIANT_TYPE("as"));
    for (const std::string& key : keys)
        g_variant_builder_add(&combination, "s", key.c_str());
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aas"));
    g_variant_builder_add_value(&list, g_variant_builder_end(&combination));
    dbusmenu_menuitem_property_set_variant(item, DBUSMENU_MENUITEM_PROP_SHORTCUT, g_variant_builder_end(&list));
}

DbusmenuMenuitem* makeItem(const MenuItemInfo& info, const char* text)
{
    DbusmenuMenuitem* item = dbusmenu_menuitem_new();
    if (info.type & TYPE_SEPARATOR)
    {
        dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_TYPE, "separator");
        return item;
    }
    dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_LABEL, menuLabel(text).c_str());
    dbusmenu_menuitem_property_set_bool(
        item, DBUSMENU_MENUITEM_PROP_ENABLED, !(info.state & (STATE_GRAYED | STATE_DISABLED)));
    if (info.state & STATE_CHECKED)
    {
        dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE, DBUSMENU_MENUITEM_TOGGLE_CHECK);
        dbusmenu_menuitem_property_set_int(
            item, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE, DBUSMENU_MENUITEM_TOGGLE_STATE_CHECKED);
    }
    const std::vector<std::string> keys = menuShortcut(text);
    if (!keys.empty())
        setShortcut(item, keys);
    return item;
}

void fill(DbusmenuMenuitem* parent, HWND holder, HMENU menu, const std::vector<int>& route)
{
    const int count = swell.GetMenuItemCount(menu);
    for (int position = 0; position < count; position++)
    {
        char text[512] = "";
        MenuItemInfo info{};
        info.size = sizeof(info);
        info.mask = MASK_ID | MASK_STATE | MASK_TYPE | MASK_SUBMENU;
        info.text = text;
        info.length = sizeof(text);
        if (!swell.GetMenuItemInfo(menu, position, 1, &info))
            continue;
        // the bar's own status text and spacers have no menu
        if (route.empty() && !info.submenu)
            continue;
        DbusmenuMenuitem* item = makeItem(info, text);
        auto* place = new ItemPlace{holder, route, info.id};
        if (info.submenu)
        {
            place->route.push_back(position);
            dbusmenu_menuitem_property_set(
                item, DBUSMENU_MENUITEM_PROP_CHILD_DISPLAY, DBUSMENU_MENUITEM_CHILD_DISPLAY_SUBMENU);
            fill(item, holder, info.submenu, place->route);
            g_signal_connect_data(item, DBUSMENU_MENUITEM_SIGNAL_ABOUT_TO_SHOW, G_CALLBACK(aboutToShow), place,
                forgetPlace, GConnectFlags(0));
        }
        else
            g_signal_connect_data(item, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED, G_CALLBACK(activated), place,
                forgetPlace, GConnectFlags(0));
        dbusmenu_menuitem_child_append(parent, item);
        g_object_unref(item);
    }
}

// reaper keeps its audio status in the bar's last item and changes only its text
bool readStatus(HMENU menu, MenuItemInfo& info, std::string& text)
{
    char buffer[512] = "";
    info = MenuItemInfo{};
    info.size = sizeof(info);
    info.mask = MASK_ID | MASK_STATE | MASK_TYPE | MASK_SUBMENU;
    info.text = buffer;
    info.length = sizeof(buffer);
    if (!swell.GetMenuItemInfo(menu, swell.GetMenuItemCount(menu) - 1, 1, &info) || info.submenu ||
        !isStatusText(buffer))
        return false;
    text = buffer;
    return true;
}

DbusmenuMenuitem* addStatus(DbusmenuMenuitem* root, HWND holder, const MenuItemInfo& info, const std::string& text)
{
    DbusmenuMenuitem* item = makeItem(info, text.c_str());
    g_signal_connect_data(item, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED, G_CALLBACK(activated),
        new ItemPlace{holder, {}, info.id}, forgetPlace, GConnectFlags(0));
    dbusmenu_menuitem_child_append(root, item);
    g_object_unref(item);
    return item;
}

void setRoot(Export& entry, HWND holder, HMENU menu)
{
    DbusmenuMenuitem* root = dbusmenu_menuitem_new();
    fill(root, holder, menu, {});
    MenuItemInfo info;
    entry.status.clear();
    entry.statusItem = readStatus(menu, info, entry.status) ? addStatus(root, holder, info, entry.status) : nullptr;
    dbusmenu_server_set_root(entry.server, root);
    g_object_unref(root);
    entry.menu = menu;
    entry.topItems = swell.GetMenuItemCount(menu);
    fprintf(stderr, "[reaper-menu] exported path=%s top_items=%d status=%d\n", entry.path.c_str(), entry.topItems,
        entry.statusItem != nullptr);
}

void followStatus(Export& entry, HWND holder)
{
    MenuItemInfo info;
    std::string text;
    if (readStatus(entry.menu, info, text) != (entry.statusItem != nullptr))
        setRoot(entry, holder, entry.menu);
    else if (entry.statusItem && text != entry.status)
    {
        dbusmenu_menuitem_property_set(entry.statusItem, DBUSMENU_MENUITEM_PROP_LABEL, menuLabel(text).c_str());
        entry.status = text;
    }
}

const Export& exportOf(HWND holder)
{
    Export& entry = exports[holder];
    if (!entry.server)
    {
        entry.path = "/com/canonical/menu/reaper/" + std::to_string(nextExport++);
        entry.server = dbusmenu_server_new(entry.path.c_str());
    }
    HMENU menu = swell.GetMenu(holder);
    if (menu != entry.menu || swell.GetMenuItemCount(menu) != entry.topItems)
        setRoot(entry, holder, menu);
    else
        followStatus(entry, holder);
    return entry;
}

void registerWindow(guint32 xid, const std::string& path)
{
    g_dbus_connection_call(bus, REGISTRAR, REGISTRAR_PATH, REGISTRAR, "RegisterWindow",
        g_variant_new("(uo)", xid, path.c_str()), nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, nullptr,
        nullptr);
}

void unregisterWindow(guint32 xid)
{
    g_dbus_connection_call(bus, REGISTRAR, REGISTRAR_PATH, REGISTRAR, "UnregisterWindow", g_variant_new("(u)", xid),
        nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, nullptr, nullptr, nullptr);
}

int collectWindow(HWND window, long windows)
{
    if (swell.IsWindowVisible(window) && swell.SWELL_GetOSWindow(window, "GdkWindow"))
        reinterpret_cast<std::vector<HWND>*>(windows)->push_back(window);
    return 1;
}

void checkWindows()
{
    std::vector<HWND> windows;
    swell.EnumWindows(collectWindow, reinterpret_cast<long>(&windows));
    std::set<HWND> holders;
    std::set<HWND> present;
    for (HWND window : windows)
    {
        const HWND holder = swell.GetMenu(window) ? window : mainWindow;
        if (!swell.GetMenu(holder))
            continue;
        holders.insert(holder);
        present.insert(window);
        const std::string path = exportOf(holder).path;
        const guint32 xid = gdk_x11_window_get_xid(static_cast<GdkWindow*>(swell.SWELL_GetOSWindow(window, "GdkWindow")));
        Shown& shown = shownWindows[window];
        if (shown.xid && shown.xid != xid)
            unregisterWindow(shown.xid);
        if (shown.xid != xid || shown.path != path)
            registerWindow(xid, path);
        shown = {xid, path};
    }
    for (auto entry = shownWindows.begin(); entry != shownWindows.end();)
    {
        if (present.count(entry->first))
            ++entry;
        else
        {
            unregisterWindow(entry->second.xid);
            entry = shownWindows.erase(entry);
        }
    }
    for (auto entry = exports.begin(); entry != exports.end();)
    {
        if (holders.count(entry->first))
            ++entry;
        else
        {
            g_object_unref(entry->second.server);
            entry = exports.erase(entry);
        }
    }
}

// the bar is read at start and only the session puts it right again
void restoreBarForNextStart()
{
    const std::string name = resourceFolder + THEME_FILE;
    std::ifstream file(name);
    std::ostringstream kept;
    bool ours = false;
    for (std::string line; std::getline(file, line);)
    {
        if (line == EMPTY_BAR)
            ours = true;
        else
            kept << line << '\n';
    }
    file.close();
    if (!ours)
        return;
    if (kept.str().empty())
        remove(name.c_str());
    else
        std::ofstream(name) << kept.str();
    fprintf(stderr, "[reaper-menu] bar restored for the next start file=%s\n", name.c_str());
}

void timer()
{
    if (bus && ++ticks % TICKS_BETWEEN_CHECKS == 0)
        checkWindows();
}

// reaper's timer stands still behind a modal dialog at startup
gboolean registrarOverdue(gpointer)
{
    overdueTimeout = 0;
    if (!bus)
    {
        fprintf(stderr, "[reaper-menu] no registrar waited_s=%d\n", SECONDS_TO_WAIT_FOR_REGISTRAR);
        restoreBarForNextStart();
    }
    return G_SOURCE_REMOVE;
}

void registrarAppeared(GDBusConnection* connection, const gchar*, const gchar* owner, gpointer)
{
    fprintf(stderr, "[reaper-menu] registrar owner=%s\n", owner);
    bus = connection;
}

void registrarVanished(GDBusConnection*, const gchar*, gpointer)
{
    if (bus)
        fprintf(stderr, "[reaper-menu] registrar vanished windows=%zu\n", shownWindows.size());
    bus = nullptr;
    shownWindows.clear();
}

void unload()
{
    registerHook("-timer", reinterpret_cast<void*>(timer));
    if (overdueTimeout)
        g_source_remove(overdueTimeout);
    for (const auto& [window, shown] : shownWindows)
        unregisterWindow(shown.xid);
    // the watcher may hold the last reference to the bus, the flush keeps it until the calls are out
    if (bus)
        g_dbus_connection_flush(bus, nullptr, nullptr, nullptr);
    g_bus_unwatch_name(registrarWatch);
    fprintf(stderr, "[reaper-menu] unloaded windows=%zu menus=%zu\n", shownWindows.size(), exports.size());
    shownWindows.clear();
    for (auto& [holder, entry] : exports)
        g_object_unref(entry.server);
    exports.clear();
    bus = nullptr;
    registerHook = nullptr;
}

template <typename Function> bool load(void* (*lookup)(const char*), Function& function, const char* name)
{
    function = reinterpret_cast<Function>(lookup(name));
    if (!function)
        fprintf(stderr, "[reaper-menu] refused: swell has no %s\n", name);
    return function != nullptr;
}
} // namespace

extern "C" __attribute__((visibility("default"))) int ReaperPluginEntry(void*, PluginInfo* info)
{
    if (!info)
    {
        if (registerHook)
            unload();
        return 0;
    }
    auto folderOf = [info](const char* name) {
        auto function = reinterpret_cast<const char* (*)()>(info->function(name));
        return std::string(function ? function() : "");
    };
    resourceFolder = folderOf("GetResourcePath");
    void* library = dlopen((folderOf("GetExePath") + "/libSwell.so").c_str(), RTLD_NOW | RTLD_NOLOAD);
    auto lookup = library ? reinterpret_cast<void* (*)(const char*)>(dlsym(library, "SWELLAPI_GetFunc")) : nullptr;
#define LOAD(name) load(lookup, swell.name, #name)
    if (!lookup || !(LOAD(GetMenu) && LOAD(GetSubMenu) && LOAD(GetMenuItemCount) && LOAD(GetMenuItemInfo) &&
                       LOAD(SendMessage) && LOAD(PostMessage) && LOAD(SWELL_GetOSWindow) && LOAD(EnumWindows) &&
                       LOAD(IsWindowVisible) && LOAD(IsWindowEnabled)))
    {
        fprintf(stderr, "[reaper-menu] refused: no swell api library=%p\n", library);
        restoreBarForNextStart();
        return 0;
    }
#undef LOAD
    mainWindow = info->mainWindow;
    registrarWatch = g_bus_watch_name(G_BUS_TYPE_SESSION, REGISTRAR, G_BUS_NAME_WATCHER_FLAGS_NONE, registrarAppeared,
        registrarVanished, nullptr, nullptr);
    overdueTimeout = g_timeout_add_seconds(SECONDS_TO_WAIT_FOR_REGISTRAR, registrarOverdue, nullptr);
    registerHook = info->add;
    registerHook("timer", reinterpret_cast<void*>(timer));
    fprintf(stderr, "[reaper-menu] loaded main=%p\n", mainWindow);
    return 1;
}
