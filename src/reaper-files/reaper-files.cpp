#include <dlfcn.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
typedef void* HWND;
typedef long (*DialogProc)(HWND, unsigned, unsigned long, long);

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

struct Choice
{
    HWND control;
    std::string id;
    std::string label;
    std::vector<std::string> options;
    int value;
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
            refusal = "template_not_built";
            return;
        }
        std::string lastLabel;
        for (HWND control = swell.GetWindow(dialog, FIRST_CHILD); control && refusal.empty();
             control = swell.GetWindow(control, NEXT_WINDOW))
            read(control, lastLabel);
    }

    ~Options()
    {
        if (holder)
            swell.DestroyWindow(holder);
    }

    void apply(const Answer& answer) const
    {
        for (const auto& [id, value] : answer.choices)
            for (const Choice& choice : choices)
                if (choice.id == id)
                    set(choice, value);
    }

    std::vector<Choice> choices;
    std::string refusal;

private:
    static long holderProc(HWND, unsigned, unsigned long, long) { return 0; }

    void read(HWND control, std::string& lastLabel)
    {
        char kind[64] = "";
        swell.GetClassName(control, kind, sizeof(kind));
        const long style = swell.GetWindowLong(control, WINDOW_STYLE);
        const long id = swell.GetWindowLong(control, WINDOW_ID);
        // the holder is never shown, so only the control's own flag in its style says whether reaper hid it
        if (!(style & OWN_VISIBLE))
            return;
        const bool box = !strcmp(kind, "Button") && (style & 0xf) == AUTO_CHECKBOX;
        const bool list = !strcmp(kind, "combobox") && (style & 3) == DROPDOWN_LIST;
        if (!strcmp(kind, "Static"))
            lastLabel = label(text(control));
        // a disabled control wakes when a box is ticked, so it must be one the portal can show as well
        else if (!box && !list)
            refusal = std::string("control=") + kind + ":" + std::to_string(id);
        else if (!swell.IsWindowEnabled(control))
            return;
        else if (box)
            choices.push_back({control, std::to_string(id), label(text(control)), {},
                int(swell.SendMessage(control, BUTTON_GET_CHECK, 0, 0))});
        else
        {
            Choice choice{control, std::to_string(id), lastLabel, {}, int(swell.SendMessage(control, LIST_GET_SELECTION, 0, 0))};
            const long count = swell.SendMessage(control, LIST_GET_COUNT, 0, 0);
            for (long item = 0; item < count; item++)
            {
                char entry[512] = "";
                swell.SendMessage(control, LIST_GET_TEXT, item, reinterpret_cast<long>(entry));
                choice.options.push_back(entry);
            }
            choices.push_back(choice);
        }
    }

    void set(const Choice& choice, const std::string& value) const
    {
        const bool list = !choice.options.empty();
        const int wanted = list ? atoi(value.c_str()) : value == "true";
        if (wanted == choice.value || (list && (wanted < 0 || wanted >= int(choice.options.size()))))
            return;
        const unsigned long id = strtoul(choice.id.c_str(), nullptr, 10);
        swell.SendMessage(choice.control, list ? LIST_SET_SELECTION : BUTTON_SET_CHECK, wanted, 0);
        swell.SendMessage(dialog, MESSAGE_COMMAND, id | (list ? LIST_CHANGED << 16 : 0), reinterpret_cast<long>(choice.control));
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
    if (extra.choices.empty())
        return;
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a(ssa(ss)s)"));
    for (const Choice& choice : extra.choices)
    {
        GVariantBuilder items;
        g_variant_builder_init(&items, G_VARIANT_TYPE("a(ss)"));
        for (size_t at = 0; at < choice.options.size(); at++)
            g_variant_builder_add(&items, "(ss)", std::to_string(at).c_str(), choice.options[at].c_str());
        const std::string value = choice.options.empty() ? (choice.value ? "true" : "false") : std::to_string(choice.value);
        g_variant_builder_add(&all, "(ss@a(ss)s)", choice.id.c_str(), choice.label.c_str(), g_variant_builder_end(&items),
            value.c_str());
    }
    g_variant_builder_add(&options, "{sv}", "choices", g_variant_builder_end(&all));
}

void addFolder(GVariantBuilder& options, const char* folder)
{
    if (folder && *folder)
        g_variant_builder_add(&options, "{sv}", "current_folder", g_variant_new_bytestring(folder));
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

    // swell's loop keeps reaper drawing behind the dialog until the portal answers
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
        const bool enabled = owner && swell.IsWindowEnabled(owner);
        if (enabled)
            swell.EnableWindow(owner, 0);
        do
        {
            swell.SWELL_RunMessageLoop();
            while (g_main_context_iteration(nullptr, FALSE))
                ;
            if (!answered && failure.empty())
                g_usleep(10000);
        } while (!answered && failure.empty());
        if (enabled)
            swell.EnableWindow(owner, 1);
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

std::string ask(const char* method, const char* title, GVariantBuilder& options, const Options& extra, Answer& answer)
{
    GError* error = nullptr;
    GDBusConnection* bus = asking || !extra.refusal.empty() ? nullptr : g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!bus)
    {
        g_variant_builder_clear(&options);
        const std::string reason = asking ? "nested" : !extra.refusal.empty() ? "template " + extra.refusal
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
    if (failure.empty() && answer.response != RESPONSE_SUCCESS && answer.response != RESPONSE_CANCELLED)
        failure = "response=" + std::to_string(answer.response);
    if (failure.empty() && answer.response == RESPONSE_SUCCESS && answer.paths.empty())
        failure = "no_files";
    fprintf(stderr, "[reaper-files] asked method=%s choices=%zu response=%u files=%zu failure=%s\n", method, extra.choices.size(),
        answer.response, answer.paths.size(), failure.empty() ? "none" : failure.c_str());
    if (failure.empty())
        extra.apply(answer);
    return failure;
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
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        const char* name = file ? strrchr(file, '/') : nullptr;
        name = name ? name + 1 : file;
        if (name && *name && *name != '.')
            g_variant_builder_add(&options, "{sv}", "current_name", g_variant_new_string(name));
        addFolder(options, folder);
        addFilters(options, extensions, file ? filterFor(extensionOf(file), extensions) : -1);
        failure = ask("SaveFile", text, options, extra, answer);
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
                 LOAD(GetWindowLong) && LOAD(SendMessage);
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
    fprintf(stderr, "[reaper-files] loaded functions=%zu\n", sizeof(jumps) / sizeof(jumps[0]));
    return 1;
}
