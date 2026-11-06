#include <dlfcn.h>
#include <fcntl.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
typedef void* HWND;
typedef long (*DialogProc)(HWND, unsigned, unsigned long, long);

struct Rect
{
    int left, top, right, bottom;
};

// mirrors reaper's own header
struct PluginInfo
{
    int version;
    HWND mainWindow;
    int (*add)(const char*, void*);
    void* (*function)(const char*);
};

enum
{
    MESSAGE_COMMAND = 0x111,
    BUTTON_GET_CHECK = 0xf0,
    BUTTON_SET_CHECK = 0xf1,
    LIST_GET_COUNT = 0x146,
    LIST_GET_SELECTION = 0x147,
    LIST_GET_TEXT = 0x148,
    LIST_SET_SELECTION = 0x14e,
    LIST_CHANGED = 1,
    TEXT_CHANGED = 0x300,
    AUTO_CHECKBOX = 3,
    DROPDOWN_LIST = 3,
    OWN_VISIBLE = 0x02000000,
    NEXT_WINDOW = 2,
    FIRST_CHILD = 5,
    WINDOW_ID = -12,
    WINDOW_STYLE = -16,
    OK_CANCEL = 1,
    CANCEL = 2,
    RESPONSE_SUCCESS = 0,
    RESPONSE_CANCELLED = 1,
};

const char* const PORTAL = "org.freedesktop.portal.Desktop";
const char* const PORTAL_PATH = "/org/freedesktop/portal/desktop";
const char* const CHOOSER = "org.freedesktop.portal.FileChooser";
// covers a portal that is started on demand, whose helper can take the bus timeout to come up
const int CALL_TIMEOUT_MS = 30000;
const int JUMP_SIZE = 14;
const char* const PANEL = "mavernix-reaper-save-panel";
// a cold first start of the panel loads kio and the theme
const int PANEL_START_SECONDS = 10;
// time a panel gets to leave after the dialog is over or its output closed
const int PANEL_EXIT_SECONDS = 5;

struct Swell
{
    HWND (*GetForegroundWindow)();
    int (*IsWindowEnabled)(HWND);
    int (*EnableWindow)(HWND, int);
    void (*SWELL_RunMessageLoop)();
    void* (*SWELL_GetOSWindow)(HWND, const char*);
    int (*MessageBox)(HWND, const char*, const char*, int);
    HWND (*SWELL_CreateDialog)(void*, const char*, HWND, DialogProc, long);
    int (*DestroyWindow)(HWND);
    HWND (*GetWindow)(HWND, int);
    int (*GetClassName)(HWND, char*, int);
    bool (*GetDlgItemText)(HWND, int, char*, int);
    bool (*SetDlgItemText)(HWND, int, const char*);
    bool (*GetWindowRect)(HWND, Rect*);
    long (*GetWindowLong)(HWND, int);
    long (*SendMessage)(HWND, unsigned, unsigned long, long);
} swell;

struct Jump
{
    const char* name;
    void* replacement;
    unsigned char* target = nullptr;
    unsigned char saved[JUMP_SIZE] = {};
    bool placed = false;
};

struct Template
{
    const char* id = nullptr;
    DialogProc proc = nullptr;
    void* resources = nullptr;
};

struct Control
{
    HWND window;
    long id;
    std::string kind;
    Rect rect;
    bool enabled;
    int value;
    std::string text;
    std::string label;
    std::vector<std::string> options;
};

struct Answer
{
    guint response = RESPONSE_CANCELLED;
    std::vector<std::string> paths;
    std::string filter;
    std::vector<std::pair<std::string, std::string>> choices;
};

char* openFiles(const char* text, const char* folder, const char* file, bool multiple, const char* extensions);
bool saveFile(const char* text, const char* folder, const char* file, const char* extensions, char* out, int size);
bool chooseFolder(const char* text, const char* folder, char* out, int size);
void setTemplate(const char* id, DialogProc proc, void* resources);

enum
{
    OPEN,
    SAVE,
    FOLDER,
    TEMPLATE
};
Jump jumps[] = {{"BrowseForFiles", reinterpret_cast<void*>(openFiles)},
    {"BrowseForSaveFile", reinterpret_cast<void*>(saveFile)},
    {"BrowseForDirectory", reinterpret_cast<void*>(chooseFolder)},
    {"BrowseFile_SetTemplate", reinterpret_cast<void*>(setTemplate)}};

HWND mainWindow;
std::string panelPath;
Template pending;
bool asking;
int requests;

bool write(unsigned char* at, const unsigned char* bytes)
{
    const long page = sysconf(_SC_PAGESIZE);
    auto start = reinterpret_cast<unsigned char*>(reinterpret_cast<uintptr_t>(at) & ~uintptr_t(page - 1));
    const size_t length = at + JUMP_SIZE > start + page ? 2 * page : page;
    if (mprotect(start, length, PROT_READ | PROT_WRITE | PROT_EXEC))
        return false;
    memcpy(at, bytes, JUMP_SIZE);
    mprotect(start, length, PROT_READ | PROT_EXEC);
    return true;
}

// an absolute jump at the function's entry, so every caller of swell's function lands in ours
bool place(Jump& jump)
{
    unsigned char code[JUMP_SIZE] = {0xff, 0x25, 0, 0, 0, 0};
    memcpy(code + 6, &jump.replacement, sizeof(void*));
    jump.placed = write(jump.target, code);
    return jump.placed;
}

void undo(Jump& jump)
{
    if (jump.placed && write(jump.target, jump.saved))
        jump.placed = false;
}

template <typename Result, typename... Arguments> Result original(Jump& jump, Arguments... arguments)
{
    undo(jump);
    Result result = reinterpret_cast<Result (*)(Arguments...)>(jump.target)(arguments...);
    place(jump);
    return result;
}

void forwardTemplate(const Template& held)
{
    undo(jumps[TEMPLATE]);
    reinterpret_cast<void (*)(const char*, DialogProc, void*)>(jumps[TEMPLATE].target)(held.id, held.proc, held.resources);
    place(jumps[TEMPLATE]);
}

std::string text(HWND window)
{
    char buffer[512] = "";
    swell.GetDlgItemText(window, 0, buffer, sizeof(buffer));
    return buffer;
}

std::string label(const std::string& raw)
{
    std::string out;
    for (size_t at = 0; at < raw.size(); at++)
    {
        if (raw[at] == '&' && at + 1 < raw.size())
            at++;
        out += raw[at];
    }
    return out;
}

// reaper's extra controls below the file list, built hidden so its own procedure fills and reads them
class Options
{
public:
    explicit Options(const Template& held)
    {
        if (!held.id)
            return;
        holder = swell.SWELL_CreateDialog(nullptr, nullptr, nullptr, holderProc, 0);
        dialog = holder ? swell.SWELL_CreateDialog(held.resources, held.id, holder, held.proc, 0) : nullptr;
        if (!dialog)
        {
            portalRefusal = panelRefusal = "template_not_built";
            return;
        }
        std::string lastLabel;
        for (HWND window = swell.GetWindow(dialog, FIRST_CHILD); window; window = swell.GetWindow(window, NEXT_WINDOW))
            read(window, lastLabel);
    }

    ~Options()
    {
        if (holder)
            swell.DestroyWindow(holder);
    }

    // the portal can show ticks and lists that are enabled now
    std::vector<const Control*> choices() const
    {
        std::vector<const Control*> out;
        for (const Control& control : controls)
            if (control.enabled && (control.kind == "check" || control.kind == "list"))
                out.push_back(&control);
        return out;
    }

    void apply(const Answer& answer)
    {
        for (const auto& [id, value] : answer.choices)
            for (const Control* choice : choices())
                if (std::to_string(choice->id) == id)
                    set(*find(choice->id), choice->kind == "list" ? atoi(value.c_str()) : value == "true");
    }

    Control* find(long id)
    {
        for (Control& control : controls)
            if (control.id == id)
                return &control;
        return nullptr;
    }

    void set(Control& control, int wanted)
    {
        const bool list = control.kind == "list";
        if (wanted == control.value || (list && (wanted < 0 || wanted >= int(control.options.size()))))
            return;
        control.value = wanted;
        swell.SendMessage(control.window, list ? LIST_SET_SELECTION : BUTTON_SET_CHECK, wanted, 0);
        command(control, list ? LIST_CHANGED : 0);
    }

    void setText(Control& control, const std::string& value)
    {
        control.text = value;
        swell.SetDlgItemText(control.window, 0, value.c_str());
        command(control, TEXT_CHANGED);
    }

    void command(const Control& control, unsigned long code)
    {
        swell.SendMessage(dialog, MESSAGE_COMMAND, control.id | code << 16, reinterpret_cast<long>(control.window));
    }

    // reads every control again and returns those reaper changed
    std::vector<const Control*> refresh()
    {
        std::vector<const Control*> changed;
        for (Control& control : controls)
        {
            const Control was = control;
            measure(control);
            if (control.enabled != was.enabled || control.value != was.value || control.text != was.text)
                changed.push_back(&control);
        }
        return changed;
    }

    std::vector<Control> controls;
    std::string portalRefusal;
    std::string panelRefusal;

private:
    static long holderProc(HWND, unsigned, unsigned long, long) { return 0; }

    void read(HWND window, std::string& lastLabel)
    {
        char kind[64] = "";
        swell.GetClassName(window, kind, sizeof(kind));
        const long style = swell.GetWindowLong(window, WINDOW_STYLE);
        const long id = swell.GetWindowLong(window, WINDOW_ID);
        // the holder is never shown, so only the control's own flag in its style says whether reaper hid it
        if (!(style & OWN_VISIBLE))
            return;
        Control control{window, id, "", {}, false, 0, "", "", {}};
        if (!strcmp(kind, "Static"))
            control.kind = "label";
        else if (!strcmp(kind, "Button"))
            control.kind = (style & 0xf) == AUTO_CHECKBOX ? "check" : (style & 0xf) == 0 ? "button" : "";
        else if (!strcmp(kind, "combobox"))
            control.kind = (style & 3) == DROPDOWN_LIST ? "list" : "";
        else if (!strcmp(kind, "Edit"))
            control.kind = "edit";
        const std::string refusal = std::string("control=") + kind + ":" + std::to_string(id);
        // a disabled control wakes when a box is ticked, so every kind counts whether it is enabled or not
        if (control.kind.empty() && panelRefusal.empty())
            panelRefusal = refusal;
        if ((control.kind.empty() || control.kind == "button" || control.kind == "edit") && portalRefusal.empty())
            portalRefusal = refusal;
        if (control.kind.empty())
            return;
        measure(control);
        if (control.kind == "label")
            lastLabel = label(control.text);
        control.label = control.kind == "list" ? lastLabel : label(control.text);
        for (long item = 0, count = control.kind == "list" ? swell.SendMessage(window, LIST_GET_COUNT, 0, 0) : 0; item < count;
             item++)
        {
            char entry[512] = "";
            swell.SendMessage(window, LIST_GET_TEXT, item, reinterpret_cast<long>(entry));
            control.options.push_back(entry);
        }
        controls.push_back(control);
    }

    void measure(Control& control) const
    {
        Rect outer{}, inner{};
        swell.GetWindowRect(dialog, &outer);
        swell.GetWindowRect(control.window, &inner);
        control.rect = {inner.left - outer.left, inner.top - outer.top, inner.right - inner.left, inner.bottom - inner.top};
        control.enabled = swell.IsWindowEnabled(control.window);
        control.text = control.kind == "list" ? "" : text(control.window);
        control.value = control.kind == "check" ? int(swell.SendMessage(control.window, BUTTON_GET_CHECK, 0, 0))
                        : control.kind == "list" ? int(swell.SendMessage(control.window, LIST_GET_SELECTION, 0, 0))
                                                 : 0;
    }

    HWND holder = nullptr;
    HWND dialog = nullptr;
};

// swell's extension list is pairs of a name and globs split by semicolons, ended by an empty name
std::vector<std::pair<std::string, std::vector<std::string>>> filterList(const char* extensions)
{
    std::vector<std::pair<std::string, std::vector<std::string>>> list;
    for (const char* name = extensions; name && *name;)
    {
        const char* globs = name + strlen(name) + 1;
        if (!*globs)
            break;
        std::vector<std::string> split;
        gchar** parts = g_strsplit(globs, ";", -1);
        for (gchar** part = parts; *part; part++)
            if (**part)
                split.push_back(strcmp(*part, "*.*") ? *part : "*");
        g_strfreev(parts);
        list.push_back({name, split});
        name = globs + strlen(globs) + 1;
    }
    return list;
}

GVariant* filterVariant(const std::pair<std::string, std::vector<std::string>>& filter)
{
    GVariantBuilder globs;
    g_variant_builder_init(&globs, G_VARIANT_TYPE("a(us)"));
    for (const std::string& glob : filter.second)
        g_variant_builder_add(&globs, "(us)", 0u, glob.c_str());
    return g_variant_new("(s@a(us))", filter.first.c_str(), g_variant_builder_end(&globs));
}

const char* extensionOf(const char* path)
{
    const char* dot = strrchr(path, '.');
    return dot ? dot : "";
}

int filterFor(const char* extension, const char* extensions)
{
    if (*extension != '.')
        return -1;
    const auto list = filterList(extensions);
    for (size_t at = 0; at < list.size(); at++)
        for (const std::string& glob : list[at].second)
            if (glob.size() > 1 && glob[0] == '*' && !g_ascii_strcasecmp(glob.c_str() + 1, extension))
                return int(at);
    return -1;
}

void addFilters(GVariantBuilder& options, const char* extensions, int current)
{
    const auto list = filterList(extensions);
    if (list.empty())
        return;
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a(sa(us))"));
    for (const auto& filter : list)
        g_variant_builder_add_value(&all, filterVariant(filter));
    g_variant_builder_add(&options, "{sv}", "filters", g_variant_builder_end(&all));
    g_variant_builder_add(&options, "{sv}", "current_filter", filterVariant(list[current < 0 ? 0 : current]));
}

void addChoices(GVariantBuilder& options, const Options& extra)
{
    if (extra.choices().empty())
        return;
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a(ssa(ss)s)"));
    for (const Control* choice : extra.choices())
    {
        GVariantBuilder items;
        g_variant_builder_init(&items, G_VARIANT_TYPE("a(ss)"));
        for (size_t at = 0; at < choice->options.size(); at++)
            g_variant_builder_add(&items, "(ss)", std::to_string(at).c_str(), choice->options[at].c_str());
        const std::string value = choice->kind == "check" ? (choice->value ? "true" : "false") : std::to_string(choice->value);
        g_variant_builder_add(&all, "(ss@a(ss)s)", std::to_string(choice->id).c_str(), choice->label.c_str(),
            g_variant_builder_end(&items), value.c_str());
    }
    g_variant_builder_add(&options, "{sv}", "choices", g_variant_builder_end(&all));
}

void addFolder(GVariantBuilder& options, const char* folder)
{
    if (folder && *folder)
        g_variant_builder_add(&options, "{sv}", "current_folder", g_variant_new_bytestring(folder));
}

// swell's loop keeps reaper drawing behind the dialog until it is answered
template <typename Done> void pump(HWND owner, Done done)
{
    const bool enabled = owner && swell.IsWindowEnabled(owner);
    if (enabled)
        swell.EnableWindow(owner, 0);
    while (!done())
    {
        swell.SWELL_RunMessageLoop();
        while (g_main_context_iteration(nullptr, FALSE))
            ;
        if (!done())
            g_usleep(10000);
    }
    if (enabled)
        swell.EnableWindow(owner, 1);
}

class Request
{
public:
    Request(GDBusConnection* bus) : bus(bus)
    {
        std::string sender = g_dbus_connection_get_unique_name(bus) + 1;
        for (char& c : sender)
            if (c == '.')
                c = '_';
        token = "mavernix" + std::to_string(++requests);
        expected = std::string(PORTAL_PATH) + "/request/" + sender + "/" + token;
        responses = g_dbus_connection_signal_subscribe(bus, PORTAL, "org.freedesktop.portal.Request", "Response", nullptr,
            nullptr, G_DBUS_SIGNAL_FLAGS_NONE, responded, this, nullptr);
        owners = g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
            "/org/freedesktop/DBus", PORTAL, G_DBUS_SIGNAL_FLAGS_NONE, ownerChanged, this, nullptr);
    }

    ~Request()
    {
        g_dbus_connection_signal_unsubscribe(bus, responses);
        g_dbus_connection_signal_unsubscribe(bus, owners);
    }

    std::string ask(const char* method, const char* title, HWND owner, GVariantBuilder& options, Answer& answer)
    {
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token.c_str()));
        void* gdk = owner ? swell.SWELL_GetOSWindow(owner, "GdkWindow") : nullptr;
        char parent[32] = "";
        if (gdk)
            snprintf(parent, sizeof(parent), "x11:%lx", gdk_x11_window_get_xid(static_cast<GdkWindow*>(gdk)));
        g_dbus_connection_call(bus, PORTAL, PORTAL_PATH, CHOOSER, method,
            g_variant_new("(ss@a{sv})", parent, title ? title : "", g_variant_builder_end(&options)), G_VARIANT_TYPE("(o)"),
            G_DBUS_CALL_FLAGS_NONE, CALL_TIMEOUT_MS, nullptr, called, this);
        pump(owner, [this] { return answered || !failure.empty(); });
        if (answered)
            answer = result;
        return failure;
    }

private:
    static void called(GObject* source, GAsyncResult* outcome, gpointer data)
    {
        auto request = static_cast<Request*>(data);
        GError* error = nullptr;
        GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), outcome, &error);
        if (!reply)
        {
            request->failure = std::string("call_error=") + error->message;
            g_error_free(error);
            return;
        }
        const gchar* handle = nullptr;
        g_variant_get(reply, "(&o)", &handle);
        request->expected = handle;
        g_variant_unref(reply);
    }

    static void responded(GDBusConnection*, const gchar*, const gchar* path, const gchar*, const gchar*, GVariant* parameters,
        gpointer data)
    {
        auto request = static_cast<Request*>(data);
        if (request->expected != path)
            return;
        GVariant* results = nullptr;
        g_variant_get(parameters, "(u@a{sv})", &request->result.response, &results);
        const gchar** uris = nullptr;
        if (g_variant_lookup(results, "uris", "^a&s", &uris))
            for (const gchar** uri = uris; *uri; uri++)
                if (gchar* path = g_filename_from_uri(*uri, nullptr, nullptr))
                {
                    request->result.paths.push_back(path);
                    g_free(path);
                }
        g_free(uris);
        GVariant* filter = g_variant_lookup_value(results, "current_filter", G_VARIANT_TYPE("(sa(us))"));
        if (filter)
        {
            GVariantIter* globs = nullptr;
            const gchar* glob = nullptr;
            guint kind = 0;
            g_variant_get(filter, "(&sa(us))", nullptr, &globs);
            if (g_variant_iter_next(globs, "(u&s)", &kind, &glob) && kind == 0)
                request->result.filter = glob;
            g_variant_iter_free(globs);
            g_variant_unref(filter);
        }
        GVariantIter* choices = nullptr;
        const gchar* id = nullptr;
        const gchar* value = nullptr;
        if (g_variant_lookup(results, "choices", "a(ss)", &choices))
        {
            while (g_variant_iter_next(choices, "(&s&s)", &id, &value))
                request->result.choices.push_back({id, value});
            g_variant_iter_free(choices);
        }
        g_variant_unref(results);
        request->answered = true;
    }

    static void ownerChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters,
        gpointer data)
    {
        const gchar* owner = nullptr;
        g_variant_get(parameters, "(&s&s&s)", nullptr, nullptr, &owner);
        auto request = static_cast<Request*>(data);
        if (!*owner && !request->answered)
            request->failure = "portal_gone";
    }

    GDBusConnection* bus;
    std::string token;
    std::string expected;
    guint responses = 0;
    guint owners = 0;
    bool answered = false;
    std::string failure;
    Answer result;
};

HWND ownerWindow()
{
    HWND owner = swell.GetForegroundWindow();
    return owner ? owner : mainWindow;
}

std::string escaped(const std::string& raw)
{
    std::string out;
    for (char c : raw)
        out += c == '\\' ? "\\\\" : c == '\t' ? "\\t" : c == '\n' ? "\\n" : c == '\r' ? "\\r" : std::string(1, c);
    return out;
}

std::vector<std::string> fieldsOf(const std::string& line)
{
    std::vector<std::string> fields(1);
    for (size_t at = 0; at < line.size(); at++)
        if (line[at] == '\t')
            fields.emplace_back();
        else if (line[at] == '\\' && at + 1 < line.size())
        {
            const char next = line[++at];
            fields.back() += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
        }
        else
            fields.back() += line[at];
    return fields;
}

bool number(const std::string& text, long& out)
{
    char* end = nullptr;
    out = strtol(text.c_str(), &end, 10);
    return !text.empty() && !*end;
}

class Panel;

// outlives its panel, so a process that is slow to leave is still reaped, and stopped if it stays
struct Child
{
    Panel* panel;
    GPid pid;
    guint watchdog;
};

// the save panel process, kept in step with the hidden template
class Panel
{
public:
    explicit Panel(Options& extra) : extra(extra) {}

    ~Panel()
    {
        if (output)
            g_source_remove(output);
        if (deadline)
            g_source_remove(deadline);
        if (socket >= 0)
            close(socket);
        if (!child)
            return;
        child->panel = nullptr;
        if (answered && failure.empty())
            child->watchdog = g_timeout_add_seconds(PANEL_EXIT_SECONDS, stop, child);
        else
            kill(child->pid, SIGKILL);
    }

    std::string ask(const std::vector<std::string>& lines, Answer& answer)
    {
        if (!start())
            return failure;
        for (const std::string& line : lines)
            send(line);
        wait(PANEL_START_SECONDS, "panel_silent");
        pump(ownerWindow(), [this] { return answered || !failure.empty(); });
        if (answered)
            answer = result;
        return failure;
    }

private:
    bool start()
    {
        int ends[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends))
        {
            failure = std::string("panel_spawn=") + strerror(errno);
            return false;
        }
        const char* arguments[] = {panelPath.c_str(), nullptr};
        GError* error = nullptr;
        GPid pid = 0;
        const bool started = g_spawn_async_with_fds(nullptr, const_cast<char**>(arguments), nullptr,
            G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr, &pid, ends[1], ends[1], -1, &error);
        close(ends[1]);
        socket = ends[0];
        if (!started)
        {
            failure = std::string("panel_spawn=") + error->message;
            g_error_free(error);
            return false;
        }
        fcntl(socket, F_SETFL, O_NONBLOCK);
        child = new Child{this, pid, 0};
        g_child_watch_add(pid, exited, child);
        output = g_unix_fd_add(socket, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), readable, this);
        return true;
    }

    void send(const std::string& line)
    {
        const std::string out = line + '\n';
        if (failure.empty() && ::send(socket, out.data(), out.size(), MSG_NOSIGNAL) != ssize_t(out.size()))
            failure = "panel_write";
    }

    void wait(int seconds, const char* reason)
    {
        if (deadline)
            g_source_remove(deadline);
        lapse = reason;
        deadline = g_timeout_add_seconds(seconds, expired, this);
    }

    void handle(const std::string& line)
    {
        const auto field = fieldsOf(line);
        const std::string& verb = field[0];
        long id = 0, value = 0;
        Control* control = field.size() > 1 && number(field[1], id) ? extra.find(id) : nullptr;
        const bool valued = field.size() == 3 && number(field[2], value);
        const std::string kind = control ? control->kind : "";
        if (!shown && verb == "shown" && field.size() == 1)
        {
            shown = true;
            g_source_remove(deadline);
            deadline = 0;
        }
        else if (shown && verb == "accept" && field.size() == 3)
        {
            answered = true;
            result.response = RESPONSE_SUCCESS;
            if (!field[1].empty())
                result.paths = {field[1]};
            result.filter = field[2];
        }
        else if (shown && verb == "cancel" && field.size() == 1)
        {
            answered = true;
            result.response = RESPONSE_CANCELLED;
        }
        else if (shown && ((verb == "check" && kind == "check" && valued && (value == 0 || value == 1)) ||
                              (verb == "select" && kind == "list" && valued) ||
                              (verb == "text" && kind == "edit" && field.size() == 3) ||
                              (verb == "click" && kind == "button" && field.size() == 2)))
            change(*control, verb, verb == "text" ? field[2] : "", int(value));
        else
            failure = "panel_line=" + escaped(line);
    }

    // a disabled control takes no change, as in swell, and its state goes back so the panel undoes it
    void change(Control& control, const std::string& verb, const std::string& text, int value)
    {
        if (control.enabled && verb == "click")
        {
            send("busy");
            extra.command(control, 0);
        }
        else if (control.enabled && verb == "text")
            extra.setText(control, text);
        else if (control.enabled)
            extra.set(control, value);
        std::vector<const Control*> changed = extra.refresh();
        if (!control.enabled && std::find(changed.begin(), changed.end(), &control) == changed.end())
            changed.push_back(&control);
        for (const Control* each : changed)
            send("state\t" + std::to_string(each->id) + "\t" + std::to_string(each->enabled) + "\t" +
                 std::to_string(each->value) + "\t" + escaped(each->text));
        send("ready");
    }

    static gboolean readable(gint fd, GIOCondition, gpointer data)
    {
        auto panel = static_cast<Panel*>(data);
        char chunk[4096];
        const ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0)
        {
            panel->output = 0;
            panel->wait(PANEL_EXIT_SECONDS, "panel_closed");
            return G_SOURCE_REMOVE;
        }
        panel->buffer.append(chunk, size_t(got));
        for (size_t end; !panel->answered && panel->failure.empty() && (end = panel->buffer.find('\n')) != std::string::npos;)
        {
            const std::string line = panel->buffer.substr(0, end);
            panel->buffer.erase(0, end + 1);
            panel->handle(line);
        }
        return G_SOURCE_CONTINUE;
    }

    static gboolean expired(gpointer data)
    {
        auto panel = static_cast<Panel*>(data);
        panel->deadline = 0;
        if (panel->failure.empty() && !panel->answered)
            panel->failure = panel->lapse;
        return G_SOURCE_REMOVE;
    }

    static void exited(GPid pid, gint status, gpointer data)
    {
        auto child = static_cast<Child*>(data);
        Panel* panel = child->panel;
        if (panel && !panel->answered && panel->failure.empty())
            panel->failure = WIFEXITED(status) ? "panel_exit=" + std::to_string(WEXITSTATUS(status))
                                               : "panel_signal=" + std::to_string(WTERMSIG(status));
        if (panel)
            panel->child = nullptr;
        if (child->watchdog)
            g_source_remove(child->watchdog);
        g_spawn_close_pid(pid);
        delete child;
    }

    static gboolean stop(gpointer data)
    {
        auto child = static_cast<Child*>(data);
        child->watchdog = 0;
        kill(child->pid, SIGKILL);
        return G_SOURCE_REMOVE;
    }

    Options& extra;
    Child* child = nullptr;
    int socket = -1;
    guint output = 0;
    guint deadline = 0;
    const char* lapse = "";
    std::string buffer;
    bool shown = false;
    bool answered = false;
    std::string failure;
    Answer result;
};

std::string finish(const char* method, size_t count, const Answer& answer, std::string failure)
{
    if (failure.empty() && answer.response != RESPONSE_SUCCESS && answer.response != RESPONSE_CANCELLED)
        failure = "response=" + std::to_string(answer.response);
    if (failure.empty() && answer.response == RESPONSE_SUCCESS && answer.paths.empty())
        failure = "no_files";
    fprintf(stderr, "[reaper-files] asked method=%s choices=%zu response=%u files=%zu failure=%s\n", method, count,
        answer.response, answer.paths.size(), failure.empty() ? "none" : failure.c_str());
    return failure;
}

std::string ask(const char* method, const char* title, GVariantBuilder& options, Options& extra, Answer& answer)
{
    GError* error = nullptr;
    GDBusConnection* bus =
        asking || !extra.portalRefusal.empty() ? nullptr : g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!bus)
    {
        g_variant_builder_clear(&options);
        const std::string reason = asking                         ? "nested"
                                   : !extra.portalRefusal.empty() ? "template " + extra.portalRefusal
                                                                  : std::string("no_bus=") + error->message;
        g_clear_error(&error);
        return reason;
    }
    addChoices(options, extra);
    asking = true;
    std::string failure;
    {
        Request request(bus);
        failure = request.ask(method, title, ownerWindow(), options, answer);
    }
    asking = false;
    g_object_unref(bus);
    failure = finish(method, extra.choices().size(), answer, failure);
    if (failure.empty())
        extra.apply(answer);
    return failure;
}

std::string askPanel(const std::vector<std::string>& lines, Options& extra, Answer& answer)
{
    if (asking)
        return "nested";
    asking = true;
    std::string failure;
    {
        Panel panel(extra);
        failure = panel.ask(lines, answer);
    }
    asking = false;
    return finish("panel", extra.controls.size(), answer, failure);
}

std::vector<std::string> panelLines(const char* title, const char* folder, const char* name, const char* extensions,
    int current, const Options& extra)
{
    std::vector<std::string> lines{"title\t" + escaped(title ? title : "")};
    if (folder && *folder)
        lines.push_back("folder\t" + escaped(folder));
    if (name && *name && *name != '.')
        lines.push_back("name\t" + escaped(name));
    for (const auto& [filter, globs] : filterList(extensions))
    {
        std::string joined;
        for (const std::string& glob : globs)
            joined += (joined.empty() ? "" : ";") + glob;
        lines.push_back("filter\t" + escaped(filter) + "\t" + escaped(joined));
    }
    if (current >= 0)
        lines.push_back("current\t" + std::to_string(current));
    for (const Control& control : extra.controls)
    {
        const Rect& at = control.rect;
        lines.push_back("control\t" + std::to_string(control.id) + "\t" + control.kind + "\t" + std::to_string(at.left) +
                        "\t" + std::to_string(at.top) + "\t" + std::to_string(at.right) + "\t" + std::to_string(at.bottom) +
                        "\t" + std::to_string(control.enabled) + "\t" + std::to_string(control.value) + "\t" +
                        escaped(control.text));
        for (const std::string& option : control.options)
            lines.push_back("item\t" + std::to_string(control.id) + "\t" + escaped(option));
    }
    lines.push_back("show");
    return lines;
}

void fallingBack(const char* function, const std::string& reason)
{
    fprintf(stderr, "[reaper-files] fallback function=%s reason=%s\n", function, reason.c_str());
}

Template take()
{
    Template held = pending;
    pending = {};
    return held;
}

char* openFiles(const char* text, const char* folder, const char* file, bool multiple, const char* extensions)
{
    const Template held = take();
    std::string failure;
    Answer answer;
    {
        Options extra(held);
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(multiple));
        addFolder(options, folder);
        addFilters(options, extensions, 0);
        failure = ask("OpenFile", text, options, extra, answer);
    }
    if (!failure.empty())
    {
        fallingBack("BrowseForFiles", failure);
        forwardTemplate(held);
        return original<char*>(jumps[OPEN], text, folder, file, multiple, extensions);
    }
    if (answer.response != RESPONSE_SUCCESS)
        return nullptr;
    std::string out;
    if (answer.paths.size() == 1)
        out = answer.paths[0] + '\0';
    else
    {
        gchar* parent = g_path_get_dirname(answer.paths[0].c_str());
        out = std::string(parent) + (strcmp(parent, "/") ? "/" : "") + '\0';
        g_free(parent);
        for (const std::string& path : answer.paths)
        {
            gchar* name = g_path_get_basename(path.c_str());
            out += std::string(name) + '\0';
            g_free(name);
        }
    }
    char* value = static_cast<char*>(calloc(out.size() + 1, 1));
    memcpy(value, out.data(), out.size());
    return value;
}

// swell adds the chosen or first filter's extension to a name that has none of the list's
std::string withExtension(const std::string& path, const std::string& chosen, const char* extensions)
{
    if (!extensions || !*extensions || filterFor(extensionOf(path.c_str()), extensions) >= 0)
        return path;
    std::string glob = chosen.size() > 2 && chosen[0] == '*' && chosen[1] == '.' && chosen[2] != '*' ? chosen : "";
    if (glob.empty())
        glob = extensions + strlen(extensions) + 1;
    if (glob.size() < 3 || glob[0] != '*' || glob[1] != '.' || glob[2] == '*')
        return path;
    return path + glob.substr(1, glob.find(';') - 1);
}

bool saveFile(const char* text, const char* folder, const char* file, const char* extensions, char* out, int size)
{
    const Template held = take();
    std::string failure;
    Answer answer;
    {
        Options extra(held);
        const char* name = file ? strrchr(file, '/') : nullptr;
        name = name ? name + 1 : file;
        const int current = file ? filterFor(extensionOf(file), extensions) : -1;
        if (!extra.portalRefusal.empty() && extra.panelRefusal.empty())
            failure = askPanel(panelLines(text, folder, name, extensions, current, extra), extra, answer);
        else
        {
            GVariantBuilder options;
            g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
            if (name && *name && *name != '.')
                g_variant_builder_add(&options, "{sv}", "current_name", g_variant_new_string(name));
            addFolder(options, folder);
            addFilters(options, extensions, current);
            failure = ask("SaveFile", text, options, extra, answer);
        }
    }
    if (!failure.empty())
    {
        fallingBack("BrowseForSaveFile", failure);
        forwardTemplate(held);
        return original<bool>(jumps[SAVE], text, folder, file, extensions, out, size);
    }
    if (answer.response != RESPONSE_SUCCESS)
        return false;
    const std::string path = withExtension(answer.paths[0], answer.filter, extensions);
    struct stat existing;
    if (path != answer.paths[0] && !stat(path.c_str(), &existing))
    {
        const std::string question = "File exists:\r\n\r\n" + path + "\r\n\r\nOverwrite?";
        if (swell.MessageBox(ownerWindow(), question.c_str(), "Overwrite file?", OK_CANCEL) == CANCEL)
            return false;
    }
    g_strlcpy(out, path.c_str(), size);
    return true;
}

bool chooseFolder(const char* text, const char* folder, char* out, int size)
{
    const Template held = take();
    std::string failure;
    Answer answer;
    {
        Options extra(held);
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "directory", g_variant_new_boolean(true));
        addFolder(options, folder);
        failure = ask("OpenFile", text, options, extra, answer);
    }
    if (!failure.empty())
    {
        fallingBack("BrowseForDirectory", failure);
        forwardTemplate(held);
        return original<bool>(jumps[FOLDER], text, folder, out, size);
    }
    if (answer.response != RESPONSE_SUCCESS)
        return false;
    g_strlcpy(out, answer.paths[0].c_str(), size);
    return true;
}

void setTemplate(const char* id, DialogProc proc, void* resources)
{
    pending = {id, proc, resources};
}

void unload()
{
    for (Jump& jump : jumps)
        undo(jump);
    fprintf(stderr, "[reaper-files] unloaded\n");
}

template <typename Function> bool load(void* (*lookup)(const char*), Function& function, const char* name)
{
    function = reinterpret_cast<Function>(lookup(name));
    if (!function)
        fprintf(stderr, "[reaper-files] refused: swell has no %s\n", name);
    return function != nullptr;
}
} // namespace

extern "C" __attribute__((visibility("default"))) int ReaperPluginEntry(void*, PluginInfo* info)
{
    if (!info)
    {
        unload();
        return 0;
    }
    auto exePath = reinterpret_cast<const char* (*)()>(info->function("GetExePath"));
    void* library = exePath ? dlopen((std::string(exePath()) + "/libSwell.so").c_str(), RTLD_NOW | RTLD_NOLOAD) : nullptr;
    auto lookup = library ? reinterpret_cast<void* (*)(const char*)>(dlsym(library, "SWELLAPI_GetFunc")) : nullptr;
    if (!lookup)
    {
        fprintf(stderr, "[reaper-files] refused: no swell api library=%p\n", library);
        return 0;
    }
#define LOAD(name) load(lookup, swell.name, #name)
    bool found = LOAD(GetForegroundWindow) && LOAD(IsWindowEnabled) && LOAD(EnableWindow) &&
                 LOAD(SWELL_RunMessageLoop) && LOAD(SWELL_GetOSWindow) && LOAD(MessageBox) && LOAD(SWELL_CreateDialog) &&
                 LOAD(DestroyWindow) &&  LOAD(GetWindow) && LOAD(GetClassName) && LOAD(GetDlgItemText) &&
                 LOAD(SetDlgItemText) && LOAD(GetWindowRect) && LOAD(GetWindowLong) && LOAD(SendMessage);
#undef LOAD
    for (Jump& jump : jumps)
        found = found && load(lookup, jump.target, jump.name);
    if (!found)
        return 0;
    for (Jump& jump : jumps)
    {
        memcpy(jump.saved, jump.target, JUMP_SIZE);
        if (!place(jump))
        {
            fprintf(stderr, "[reaper-files] refused: could not redirect %s error=%s\n", jump.name, strerror(errno));
            unload();
            return 0;
        }
    }
    mainWindow = info->mainWindow;
    Dl_info self;
    char* real = dladdr(reinterpret_cast<void*>(ReaperPluginEntry), &self) ? realpath(self.dli_fname, nullptr) : nullptr;
    if (real)
    {
        gchar* folder = g_path_get_dirname(real);
        panelPath = std::string(folder) + "/" + PANEL;
        g_free(folder);
        free(real);
    }
    fprintf(stderr, "[reaper-files] loaded functions=%zu\n", sizeof(jumps) / sizeof(jumps[0]));
    return 1;
}
