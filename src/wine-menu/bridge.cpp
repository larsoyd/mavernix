#include <gio/gio.h>
#include <glib-unix.h>
#include <libdbusmenu-glib/menuitem.h>
#include <libdbusmenu-glib/server.h>

#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "menu-text.hpp"

extern char** environ;

namespace
{
const char* const REGISTRAR = "com.canonical.AppMenu.Registrar";
const char* const REGISTRAR_PATH = "/com/canonical/AppMenu/Registrar";
const char* const OWN_NAME = "org.mavernix.WineMenu";
const char* const AGENT_FILES[] = {"mavernix-wine-menu64.dll", "mavernix-wine-menu32.dll",
    "mavernix-wine-menu-agent64.exe", "mavernix-wine-menu-agent32.exe"};
const int SCAN_SECONDS = 2;
const gint64 OPEN_WAIT_US = 500000;
const int MOST_LINES = 4000;
const size_t LONGEST_LINE = 4096;

enum
{
    POPUP = 1,
    GRAYED = 2,
    CHECKED = 4,
    SEPARATOR = 8,
    RADIO = 16,
};

struct Entry
{
    int position = 0;
    unsigned flags = 0;
    unsigned id = 0;
    std::string text;
    std::vector<Entry> children;
};

// xid 0 is a window of wine's wayland driver, its address goes to the compositor through the program
struct Window
{
    guint32 xid = 0;
    std::string path;
    DbusmenuServer* server = nullptr;
    DbusmenuMenuitem* root = nullptr;
    std::vector<Entry> bar;
    bool registered = false;
};

struct Client
{
    unsigned serial = 0;
    GSocketConnection* connection = nullptr;
    GSocket* socket = nullptr;
    GSource* source = nullptr;
    std::string buffer;
    std::string nonce;
    std::string name;
    bool trusted = false;
    bool closing = false;
    std::map<std::string, Window> windows;
    std::string menuWindow;
    guint32 menuXid = 0;
    int menuLines = 0;
    std::vector<std::pair<int, Entry>> menuItems;
    std::set<std::string> opened;
};

// items find their window again by name, a window can go while the shell holds an id
struct ItemPlace
{
    unsigned client;
    std::string window;
    std::string route;
    int position;
};

struct Pending
{
    unsigned client;
    std::string window;
    guint32 xid;
};

GMainLoop* loop;
GDBusConnection* bus;
bool registrarUp;
std::string token;
guint16 port;
std::string agentsDir;
std::string onlyPrefix;
std::map<unsigned, std::unique_ptr<Client>> clients;
std::map<int, std::string> servers;
std::set<std::pair<dev_t, ino_t>> ownDisplays;
unsigned nextClient = 1;
unsigned nextExport = 1;
unsigned nextOpen = 1;

void say(const char* format, ...) G_GNUC_PRINTF(1, 2);
void say(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fputs("[wine-menu] ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(arguments);
}

std::string sign(const std::string& text)
{
    gchar* mac = g_compute_hmac_for_string(G_CHECKSUM_SHA256, reinterpret_cast<const guchar*>(token.data()),
        token.size(), text.c_str(), -1);
    std::string result = mac;
    g_free(mac);
    return result;
}

bool sameText(const std::string& one, const std::string& other)
{
    unsigned char difference = one.size() != other.size();
    for (size_t at = 0; at < one.size() && at < other.size(); at++)
        difference |= one[at] ^ other[at];
    return difference == 0;
}

std::string randomHex(int bytes)
{
    std::string text;
    for (int at = 0; at < bytes; at++)
    {
        char pair[3];
        snprintf(pair, sizeof pair, "%02x", g_random_int_range(0, 256));
        text += pair;
    }
    return text;
}

Client* clientOf(unsigned serial)
{
    auto found = clients.find(serial);
    return found == clients.end() || found->second->closing ? nullptr : found->second.get();
}

void dropLater(Client& client);

void send(Client& client, const std::string& line)
{
    if (client.closing)
        return;
    std::string data = line + "\n";
    const char* at = data.data();
    gssize left = data.size();
    while (left > 0)
    {
        GError* error = nullptr;
        gssize sent = g_socket_send(client.socket, at, left, nullptr, &error);
        if (sent < 0 && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK) &&
            g_socket_condition_timed_wait(client.socket, G_IO_OUT, G_USEC_PER_SEC, nullptr, nullptr))
        {
            g_error_free(error);
            continue;
        }
        if (sent <= 0)
        {
            say("client %s lost while sending error=%s", client.name.c_str(), error ? error->message : "closed");
            g_clear_error(&error);
            dropLater(client);
            return;
        }
        at += sent;
        left -= sent;
    }
}

void callRegistrar(const char* method, GVariant* arguments, GAsyncReadyCallback done, gpointer data)
{
    g_dbus_connection_call(bus, REGISTRAR, REGISTRAR_PATH, REGISTRAR, method, arguments, nullptr,
        G_DBUS_CALL_FLAGS_NO_AUTO_START, 2000, nullptr, done, data);
}

void unregisterXid(guint32 xid)
{
    if (registrarUp)
        callRegistrar("UnregisterWindow", g_variant_new("(u)", xid), nullptr, nullptr);
}

void registered(GObject*, GAsyncResult* result, gpointer data)
{
    std::unique_ptr<Pending> pending(static_cast<Pending*>(data));
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(bus, result, &error);
    if (!reply)
    {
        say("register refused xid=0x%x error=%s, the program keeps its bar", pending->xid, error->message);
        g_error_free(error);
        return;
    }
    g_variant_unref(reply);
    Client* client = clientOf(pending->client);
    if (!client || !client->windows.count(pending->window) || !registrarUp)
        return;
    Window& window = client->windows[pending->window];
    if (window.xid != pending->xid)
        return;
    window.registered = true;
    say("shown window=%s xid=0x%x path=%s", pending->window.c_str(), pending->xid, window.path.c_str());
    send(*client, "hide " + pending->window);
}

void registerWindow(Client& client, const std::string& name, Window& window)
{
    if (!registrarUp || window.registered)
        return;
    if (!window.xid)
    {
        window.registered = true;
        say("shown window=%s wayland path=%s", name.c_str(), window.path.c_str());
        send(client, "attach " + name + " " + g_dbus_connection_get_unique_name(bus) + " " + window.path);
        send(client, "hide " + name);
        return;
    }
    callRegistrar("RegisterWindow", g_variant_new("(uo)", window.xid, window.path.c_str()), registered,
        new Pending{client.serial, name, window.xid});
}

void forgetPlace(gpointer place, GClosure*)
{
    delete static_cast<ItemPlace*>(place);
}

std::string routeTo(const std::string& route, int position)
{
    return route == "-" ? std::to_string(position) : route + "." + std::to_string(position);
}

void activated(DbusmenuMenuitem*, guint, gpointer data)
{
    const auto* place = static_cast<ItemPlace*>(data);
    if (Client* client = clientOf(place->client))
        send(*client, "activate " + place->window + " " + place->route + " " + std::to_string(place->position));
}

void handleLines(Client& client);

// the program fills some menus only when they open, the shell waits for what it put there
gboolean aboutToShow(DbusmenuMenuitem* item, gpointer data)
{
    const auto* place = static_cast<ItemPlace*>(data);
    Client* client = clientOf(place->client);
    if (!client)
        return TRUE;
    const unsigned serial = client->serial;
    const std::string answer = place->window + " " + std::to_string(nextOpen++);
    g_object_ref(item);
    send(*client, "open " + answer + " " + routeTo(place->route, place->position));
    const gint64 end = g_get_monotonic_time() + OPEN_WAIT_US;
    while ((client = clientOf(serial)) && !client->opened.count(answer))
    {
        const gint64 left = end - g_get_monotonic_time();
        if (left <= 0 || !g_socket_condition_timed_wait(client->socket, G_IO_IN, left, nullptr, nullptr))
            break;
        char chunk[4096];
        const gssize got = g_socket_receive(client->socket, chunk, sizeof chunk, nullptr, nullptr);
        if (got <= 0)
        {
            dropLater(*client);
            break;
        }
        client->buffer.append(chunk, got);
        handleLines(*client);
    }
    if (client && !client->opened.erase(answer))
        say("open not answered window=%s in %lld ms", place->window.c_str(), (long long)(OPEN_WAIT_US / 1000));
    g_object_unref(item);
    return TRUE;
}

void menuEvent(DbusmenuMenuitem*, const gchar* name, GVariant*, guint, gpointer data)
{
    const auto* place = static_cast<ItemPlace*>(data);
    Client* client = clientOf(place->client);
    if (client && !strcmp(name, "closed"))
        send(*client, "close " + place->window + " " + routeTo(place->route, place->position));
}

void setShortcut(DbusmenuMenuitem* item, const std::vector<std::string>& keys)
{
    if (keys.empty())
    {
        dbusmenu_menuitem_property_remove(item, DBUSMENU_MENUITEM_PROP_SHORTCUT);
        return;
    }
    GVariantBuilder combination;
    g_variant_builder_init(&combination, G_VARIANT_TYPE("as"));
    for (const std::string& key : keys)
        g_variant_builder_add(&combination, "s", key.c_str());
    GVariantBuilder list;
    g_variant_builder_init(&list, G_VARIANT_TYPE("aas"));
    g_variant_builder_add_value(&list, g_variant_builder_end(&combination));
    dbusmenu_menuitem_property_set_variant(item, DBUSMENU_MENUITEM_PROP_SHORTCUT, g_variant_builder_end(&list));
}

void describe(DbusmenuMenuitem* item, const Entry& entry)
{
    dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_LABEL, menuLabel(entry.text).c_str());
    dbusmenu_menuitem_property_set_bool(item, DBUSMENU_MENUITEM_PROP_ENABLED, !(entry.flags & GRAYED));
    // win32 says only whether a mark is there, not whether an item could carry one
    if (entry.flags & CHECKED)
    {
        dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
            entry.flags & RADIO ? DBUSMENU_MENUITEM_TOGGLE_RADIO : DBUSMENU_MENUITEM_TOGGLE_CHECK);
        dbusmenu_menuitem_property_set_int(
            item, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE, DBUSMENU_MENUITEM_TOGGLE_STATE_CHECKED);
    }
    else
    {
        dbusmenu_menuitem_property_remove(item, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE);
        dbusmenu_menuitem_property_remove(item, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE);
    }
    setShortcut(item, menuShortcut(entry.text));
}

void fill(DbusmenuMenuitem* parent, const std::vector<Entry>& entries, unsigned client, const std::string& window,
    const std::string& route)
{
    for (const Entry& entry : entries)
    {
        DbusmenuMenuitem* item = dbusmenu_menuitem_new();
        auto* place = new ItemPlace{client, window, route, entry.position};
        if (entry.flags & SEPARATOR)
        {
            dbusmenu_menuitem_property_set(item, DBUSMENU_MENUITEM_PROP_TYPE, "separator");
            delete place;
        }
        else if (entry.flags & POPUP)
        {
            describe(item, entry);
            dbusmenu_menuitem_property_set(
                item, DBUSMENU_MENUITEM_PROP_CHILD_DISPLAY, DBUSMENU_MENUITEM_CHILD_DISPLAY_SUBMENU);
            fill(item, entry.children, client, window, routeTo(route, entry.position));
            g_signal_connect(item, DBUSMENU_MENUITEM_SIGNAL_EVENT, G_CALLBACK(menuEvent), place);
            g_signal_connect_data(item, DBUSMENU_MENUITEM_SIGNAL_ABOUT_TO_SHOW, G_CALLBACK(aboutToShow), place,
                forgetPlace, GConnectFlags(0));
        }
        else
        {
            describe(item, entry);
            g_signal_connect_data(item, DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED, G_CALLBACK(activated), place,
                forgetPlace, GConnectFlags(0));
        }
        dbusmenu_menuitem_child_append(parent, item);
        g_object_unref(item);
    }
}

bool sameShape(const std::vector<Entry>& one, const std::vector<Entry>& other)
{
    if (one.size() != other.size())
        return false;
    for (size_t at = 0; at < one.size(); at++)
        if (one[at].position != other[at].position ||
            (one[at].flags & (POPUP | SEPARATOR)) != (other[at].flags & (POPUP | SEPARATOR)))
            return false;
    return true;
}

// an open menu keeps its ids when only words and marks change
void follow(DbusmenuMenuitem* parent, const std::vector<Entry>& old, const std::vector<Entry>& fresh, unsigned client,
    const std::string& window, const std::string& route)
{
    if (!sameShape(old, fresh))
    {
        g_list_free_full(dbusmenu_menuitem_take_children(parent), g_object_unref);
        fill(parent, fresh, client, window, route);
        return;
    }
    GList* children = dbusmenu_menuitem_get_children(parent);
    for (size_t at = 0; at < fresh.size() && children; at++, children = children->next)
    {
        auto* item = static_cast<DbusmenuMenuitem*>(children->data);
        if (fresh[at].flags & SEPARATOR)
            continue;
        describe(item, fresh[at]);
        if (fresh[at].flags & POPUP)
            follow(item, old[at].children, fresh[at].children, client, window, routeTo(route, fresh[at].position));
    }
}

std::vector<Entry> treeOf(const std::vector<std::pair<int, Entry>>& items)
{
    std::vector<Entry> bar;
    std::vector<std::vector<Entry>*> levels{&bar};
    for (const auto& [depth, entry] : items)
    {
        if (depth < 0 || depth >= int(levels.size()))
            continue;
        levels.resize(depth + 1);
        levels[depth]->push_back(entry);
        levels.push_back(&levels[depth]->back().children);
    }
    return bar;
}

void showMenu(Client& client)
{
    const std::string name = client.menuWindow;
    Window& window = client.windows[name];
    std::vector<Entry> bar = treeOf(client.menuItems);
    client.menuItems.clear();
    if (window.registered && window.xid != client.menuXid)
    {
        if (window.xid)
            unregisterXid(window.xid);
        else
            send(client, "detach " + name);
        window.registered = false;
    }
    window.xid = client.menuXid;
    if (!window.server)
    {
        window.path = "/com/canonical/menu/wine/" + std::to_string(nextExport++);
        window.server = dbusmenu_server_new(window.path.c_str());
        window.root = dbusmenu_menuitem_new();
        fill(window.root, bar, client.serial, name, "-");
        dbusmenu_server_set_root(window.server, window.root);
        say("exported %s window=%s xid=0x%x top_items=%zu", client.name.c_str(), name.c_str(), window.xid, bar.size());
    }
    else
        follow(window.root, window.bar, bar, client.serial, name, "-");
    window.bar = std::move(bar);
    registerWindow(client, name, window);
}

void forgetWindow(Window& window)
{
    if (window.registered && window.xid)
        unregisterXid(window.xid);
    if (window.root)
        g_object_unref(window.root);
    if (window.server)
        g_object_unref(window.server);
}

bool readItem(Client& client, const std::string& line)
{
    std::istringstream fields(line);
    int depth;
    Entry entry;
    if (!(fields >> depth >> entry.position >> std::hex >> entry.flags >> std::dec >> entry.id))
        return false;
    fields.get();
    std::getline(fields, entry.text);
    client.menuItems.emplace_back(depth, std::move(entry));
    return true;
}

void meet(Client& client, const std::string& line)
{
    std::istringstream fields(line);
    std::string verb, proof, nonce, pid, exe;
    fields >> verb >> proof >> nonce >> pid;
    std::getline(fields >> std::ws, exe);
    if (verb != "hello" || nonce.empty() || !sameText(proof, sign("client " + client.nonce)))
    {
        say("refused a connection that does not hold the token");
        dropLater(client);
        return;
    }
    client.trusted = true;
    client.name = exe + "(" + pid + ")";
    send(client, "welcome " + sign("bridge " + nonce));
    say("client %s", client.name.c_str());
}

void handleLine(Client& client, const std::string& line)
{
    if (!client.trusted)
        return meet(client, line);
    if (client.menuLines > 0)
    {
        if (!readItem(client, line))
            say("unreadable item from %s: %s", client.name.c_str(), line.c_str());
        if (--client.menuLines == 0)
            showMenu(client);
        return;
    }
    std::istringstream fields(line);
    std::string verb, window, extra;
    fields >> verb >> window;
    if (verb == "menu" && fields >> std::hex >> client.menuXid >> std::dec >> client.menuLines &&
        client.menuLines > 0 && client.menuLines <= MOST_LINES)
        client.menuWindow = window;
    else if (verb == "menu")
    {
        say("refused a menu from %s: %s", client.name.c_str(), line.c_str());
        dropLater(client);
    }
    else if (verb == "gone" && client.windows.count(window))
    {
        say("withdrawn window=%s", window.c_str());
        forgetWindow(client.windows[window]);
        client.windows.erase(window);
    }
    else if (verb == "opened" && fields >> extra)
        client.opened.insert(window + " " + extra);
    else if (verb == "note")
        say("%s %s", client.name.c_str(), line.c_str() + 5);
}

void handleLines(Client& client)
{
    size_t start = 0;
    for (size_t newline; !client.closing && (newline = client.buffer.find('\n', start)) != std::string::npos;
         start = newline + 1)
        handleLine(client, client.buffer.substr(start, newline - start));
    client.buffer.erase(0, start);
    if (client.buffer.size() > LONGEST_LINE)
    {
        say("refused an endless line from %s", client.name.c_str());
        dropLater(client);
    }
}

gboolean dropNow(gpointer data)
{
    auto found = clients.find(GPOINTER_TO_UINT(data));
    if (found == clients.end())
        return G_SOURCE_REMOVE;
    Client& client = *found->second;
    say("client %s gone windows=%zu", client.name.empty() ? "unknown" : client.name.c_str(), client.windows.size());
    for (auto& [name, window] : client.windows)
        forgetWindow(window);
    g_source_destroy(client.source);
    g_source_unref(client.source);
    g_io_stream_close(G_IO_STREAM(client.connection), nullptr, nullptr);
    g_object_unref(client.connection);
    clients.erase(found);
    return G_SOURCE_REMOVE;
}

// a hung up socket stays readable, its source would starve the idle that ends it
void dropLater(Client& client)
{
    if (client.closing)
        return;
    client.closing = true;
    g_source_destroy(client.source);
    g_idle_add(dropNow, GUINT_TO_POINTER(client.serial));
}

gboolean readable(GSocket* socket, GIOCondition, gpointer data)
{
    Client* client = clientOf(GPOINTER_TO_UINT(data));
    if (!client)
        return G_SOURCE_CONTINUE;
    char chunk[4096];
    GError* error = nullptr;
    const gssize got = g_socket_receive(socket, chunk, sizeof chunk, nullptr, &error);
    if (got < 0 && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
    {
        g_error_free(error);
        return G_SOURCE_CONTINUE;
    }
    g_clear_error(&error);
    if (got <= 0)
    {
        dropLater(*client);
        return G_SOURCE_CONTINUE;
    }
    client->buffer.append(chunk, got);
    handleLines(*client);
    return G_SOURCE_CONTINUE;
}

gboolean incoming(GSocketService*, GSocketConnection* connection, GObject*, gpointer)
{
    auto client = std::make_unique<Client>();
    client->serial = nextClient++;
    client->connection = G_SOCKET_CONNECTION(g_object_ref(connection));
    client->socket = g_socket_connection_get_socket(connection);
    g_socket_set_blocking(client->socket, FALSE);
    client->nonce = randomHex(16);
    client->source = g_socket_create_source(client->socket, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), nullptr);
    g_source_set_callback(client->source, G_SOURCE_FUNC(readable), GUINT_TO_POINTER(client->serial), nullptr);
    g_source_attach(client->source, nullptr);
    Client& added = *client;
    clients[client->serial] = std::move(client);
    send(added, "challenge " + added.nonce);
    return TRUE;
}

void registrarAppeared(GDBusConnection*, const gchar*, const gchar* owner, gpointer)
{
    say("registrar owner=%s", owner);
    registrarUp = true;
    for (auto& [serial, client] : clients)
        for (auto& [name, window] : client->windows)
            registerWindow(*client, name, window);
}

void registrarVanished(GDBusConnection*, const gchar*, gpointer)
{
    if (registrarUp)
        say("registrar vanished, programs get their bars back");
    registrarUp = false;
    for (auto& [serial, client] : clients)
        for (auto& [name, window] : client->windows)
        {
            if (!std::exchange(window.registered, false))
                continue;
            if (!window.xid)
                send(*client, "detach " + name);
            send(*client, "show " + name);
        }
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

// a sandboxed wine sees the home folder but not the system's library folder
std::string copyAgents()
{
    const char* cache = g_get_user_cache_dir();
    const std::string folder = std::string(cache) + "/mavernix/wine-menu";
    if (g_mkdir_with_parents(folder.c_str(), 0700))
        return say("refused: cannot make %s", folder.c_str()), "";
    for (const char* name : AGENT_FILES)
    {
        const std::string wanted = readFile(agentsDir + "/" + name), target = folder + "/" + name;
        if (wanted.empty())
            return say("refused: no %s in %s", name, agentsDir.c_str()), "";
        if (readFile(target) == wanted)
            continue;
        // a running program may have the old library mapped, so it gets a new file
        const std::string fresh = target + ".new";
        if (!g_file_set_contents(fresh.c_str(), wanted.data(), wanted.size(), nullptr) || rename(fresh.c_str(), target.c_str()))
            return say("refused: cannot write %s", target.c_str()), "";
    }
    return folder;
}

std::vector<std::string> environmentOf(int pid)
{
    std::vector<std::string> values;
    const std::string data = readFile("/proc/" + std::to_string(pid) + "/environ");
    for (size_t start = 0, end; start < data.size(); start = end + 1)
    {
        end = data.find('\0', start);
        if (end == std::string::npos)
            end = data.size();
        values.push_back(data.substr(start, end - start));
    }
    return values;
}

std::string valueIn(const std::vector<std::string>& environment, const std::string& name)
{
    for (const std::string& entry : environment)
        if (entry.compare(0, name.size() + 1, name + "=") == 0)
            return entry.substr(name.size() + 1);
    return "";
}

std::string realPath(const std::string& path)
{
    char* resolved = realpath(path.c_str(), nullptr);
    std::string result = resolved ? resolved : "";
    free(resolved);
    return result;
}

std::string xSocket(const std::string& display)
{
    const size_t colon = display.rfind(':');
    if (colon == std::string::npos || (colon && display.compare(0, colon, "unix")))
        return "";
    const std::string number = display.substr(colon + 1, display.find('.', colon) - colon - 1);
    return number.empty() || number.find_first_not_of("0123456789") != std::string::npos ? ""
                                                                                          : "/tmp/.X11-unix/X" + number;
}

std::string waylandSocket(const std::string& display, const std::string& runtime)
{
    if (display.empty() || display[0] == '/')
        return display;
    return runtime.empty() ? "" : runtime + "/" + display;
}

// sockets are compared by inode, so a program in a container that shares them still counts
std::set<std::pair<dev_t, ino_t>> displaysOf(const std::string& root, const std::vector<std::string>& environment)
{
    std::set<std::pair<dev_t, ino_t>> found;
    for (const std::string& path : {xSocket(valueIn(environment, "DISPLAY")),
             waylandSocket(valueIn(environment, "WAYLAND_DISPLAY"), valueIn(environment, "XDG_RUNTIME_DIR"))})
    {
        struct stat info;
        if (!path.empty() && !stat((root + path).c_str(), &info) && S_ISSOCK(info.st_mode))
            found.insert({info.st_dev, info.st_ino});
    }
    return found;
}

void agentEnded(GPid pid, gint status, gpointer data)
{
    say("agent pid=%d for %s ended status=%d", pid, static_cast<char*>(data), status);
    g_free(data);
    g_spawn_close_pid(pid);
}

// the agent runs inside the program's own wine, which may live in a container that shares this home
void startAgents(int serverPid)
{
    const std::string serverExe = realPath("/proc/" + std::to_string(serverPid) + "/exe");
    const std::vector<std::string> serverEnvironment = environmentOf(serverPid);
    std::string prefix = valueIn(serverEnvironment, "WINEPREFIX");
    prefix = realPath(prefix.empty() ? std::string(g_get_home_dir()) + "/.wine" : prefix);
    if (!onlyPrefix.empty() && prefix != onlyPrefix)
        return;
    // a program on another display, such as a test's, is not this desktop's to serve
    bool shared = false;
    for (const auto& display : displaysOf("/proc/" + std::to_string(serverPid) + "/root", serverEnvironment))
        shared |= ownDisplays.count(display) > 0;
    if (!shared)
        return say("skipped server=%d prefix=%s, its programs show on another display", serverPid, prefix.c_str());
    const std::string wine = serverExe.substr(0, serverExe.rfind('/') + 1) + "wine";
    if (prefix.empty() || access(wine.c_str(), X_OK))
        return say("refused: no wine beside server=%d exe=%s prefix=%s", serverPid, serverExe.c_str(), prefix.c_str());
    const std::string folder = copyAgents();
    if (folder.empty())
        return;
    std::vector<std::string> environment;
    for (char** entry = environ; *entry; entry++)
        if (strncmp(*entry, "WINE", 4))
            environment.push_back(*entry);
    for (const std::string& entry : serverEnvironment)
        if (!entry.compare(0, 4, "WINE") && entry.compare(0, 17, "WINESERVERSOCKET=") &&
            entry.compare(0, 18, "WINELOADERNOEXEC=") && entry.compare(0, 10, "WINEDEBUG="))
            environment.push_back(entry);
    environment.push_back("WINEPREFIX=" + prefix);
    environment.push_back("WINEDEBUG=-all");
    environment.push_back("MAVERNIX_WINE_MENU_PORT=" + std::to_string(port));
    environment.push_back("MAVERNIX_WINE_MENU_TOKEN=" + token);
    std::vector<char*> envp;
    for (std::string& entry : environment)
        envp.push_back(entry.data());
    envp.push_back(nullptr);
    std::string agent = folder + "/mavernix-wine-menu-agent64.exe";
    char* argv[] = {const_cast<char*>(wine.c_str()), agent.data(), nullptr};
    GPid pid;
    GError* error = nullptr;
    if (!g_spawn_async(nullptr, argv, envp.data(), G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr, &pid, &error))
    {
        say("refused: agent did not start prefix=%s error=%s", prefix.c_str(), error->message);
        g_error_free(error);
        return;
    }
    say("agent pid=%d prefix=%s server=%d", pid, prefix.c_str(), serverPid);
    g_child_watch_add(pid, agentEnded, g_strdup(prefix.c_str()));
}

bool ownedServer(const std::string& pid)
{
    struct stat info;
    const std::string folder = "/proc/" + pid;
    if (stat(folder.c_str(), &info) || info.st_uid != getuid())
        return false;
    std::string name = readFile(folder + "/comm");
    return name.compare(0, 10, "wineserver") == 0;
}

gboolean scan(gpointer)
{
    const char* session = getenv("DESKTOP_SESSION_PID");
    if (session && *session && kill(atoi(session), 0) && errno == ESRCH)
    {
        say("the session ended");
        g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
    }
    std::set<int> alive;
    if (DIR* proc = opendir("/proc"))
    {
        while (dirent* entry = readdir(proc))
            if (entry->d_name[0] >= '1' && entry->d_name[0] <= '9' && ownedServer(entry->d_name))
                alive.insert(atoi(entry->d_name));
        closedir(proc);
    }
    for (int pid : alive)
        if (!servers.count(pid))
        {
            servers[pid] = "";
            startAgents(pid);
        }
    for (auto entry = servers.begin(); entry != servers.end();)
        entry = alive.count(entry->first) ? std::next(entry) : servers.erase(entry);
    return G_SOURCE_CONTINUE;
}

gboolean stopping(gpointer)
{
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

void nameLost(GDBusConnection*, const gchar* name, gpointer)
{
    say("refused: %s is taken, another bridge runs", name);
    g_main_loop_quit(loop);
}

bool listen(GSocketService* service)
{
    GInetAddress* loopback = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    GSocketAddress* wanted = g_inet_socket_address_new(loopback, 0);
    GSocketAddress* bound = nullptr;
    GError* error = nullptr;
    const bool done = g_socket_listener_add_address(G_SOCKET_LISTENER(service), wanted, G_SOCKET_TYPE_STREAM,
        G_SOCKET_PROTOCOL_TCP, nullptr, &bound, &error);
    if (done)
        port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(bound));
    else
        say("refused: cannot listen error=%s", error->message);
    g_clear_error(&error);
    g_clear_object(&bound);
    g_object_unref(wanted);
    g_object_unref(loopback);
    return done;
}
} // namespace

int main(int argc, char** argv)
{
    agentsDir = AGENTS_DIR;
    for (int at = 1; at + 1 < argc; at += 2)
    {
        if (!strcmp(argv[at], "--agents"))
            agentsDir = argv[at + 1];
        else if (!strcmp(argv[at], "--only-prefix"))
            onlyPrefix = realPath(argv[at + 1]);
        else
            return fprintf(stderr, "usage: %s [--agents DIR] [--only-prefix PREFIX]\n", argv[0]), 2;
    }
    if (argc % 2 == 0)
        return fprintf(stderr, "usage: %s [--agents DIR] [--only-prefix PREFIX]\n", argv[0]), 2;
    if (const char* off = getenv("MAVERNIX_GLOBAL_MENU"); off && !strcmp(off, "0"))
        return say("off, MAVERNIX_GLOBAL_MENU=0"), 0;
    std::vector<std::string> ownEnvironment;
    for (char** entry = environ; *entry; entry++)
        ownEnvironment.push_back(*entry);
    ownDisplays = displaysOf("", ownEnvironment);
    if (ownDisplays.empty())
        return say("refused: no display of its own, DISPLAY and WAYLAND_DISPLAY name no socket"), 1;
    GError* error = nullptr;
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!bus)
        return say("refused: no session bus error=%s", error->message), 1;
    loop = g_main_loop_new(nullptr, FALSE);
    token = randomHex(32);
    GSocketService* service = g_socket_service_new();
    if (!listen(service))
        return 1;
    g_signal_connect(service, "incoming", G_CALLBACK(incoming), nullptr);
    g_socket_service_start(service);
    const guint name = g_bus_own_name_on_connection(bus, OWN_NAME, G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE, nullptr,
        nameLost, nullptr, nullptr);
    const guint watch = g_bus_watch_name_on_connection(bus, REGISTRAR, G_BUS_NAME_WATCHER_FLAGS_NONE,
        registrarAppeared, registrarVanished, nullptr, nullptr);
    g_unix_signal_add(SIGTERM, stopping, nullptr);
    g_unix_signal_add(SIGINT, stopping, nullptr);
    scan(nullptr);
    g_timeout_add_seconds(SCAN_SECONDS, scan, nullptr);
    say("listening port=%u agents=%s%s%s", port, agentsDir.c_str(), onlyPrefix.empty() ? "" : " only=",
        onlyPrefix.c_str());
    g_main_loop_run(loop);
    for (auto& [serial, client] : clients)
        for (auto& [window, exported] : client->windows)
            send(*client, "show " + window);
    std::vector<unsigned> serials;
    for (auto& [serial, client] : clients)
        serials.push_back(serial);
    for (unsigned serial : serials)
        dropNow(GUINT_TO_POINTER(serial));
    g_dbus_connection_flush_sync(bus, nullptr, nullptr);
    g_bus_unwatch_name(watch);
    g_bus_unown_name(name);
    g_socket_service_stop(service);
    g_object_unref(service);
    say("stopped");
    return 0;
}
