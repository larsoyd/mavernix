// loads a reaper plugin as reaper does over a scripted swell, one command per stdin line
#include <dlfcn.h>
#include <glib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
typedef void* HWND;
struct Menu;

struct Item
{
    std::string text;
    unsigned id = 0;
    Menu* submenu = nullptr;
};

struct Menu
{
    std::vector<Item> items;
};

struct Window
{
    unsigned xid = 0;
    Menu* menu = nullptr;
    bool visible = true;
    bool enabled = true;
};

struct MenuItemInfo
{
    unsigned size, mask, type, state, id;
    Menu* submenu;
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

typedef long (*DialogProc)(HWND, unsigned, unsigned long, long);

struct Control
{
    std::string kind;
    std::string text;
    long id = 0;
    unsigned long style = 0;
    long parent = 0;
    bool visible = true;
    bool enabled = true;
    int check = 0;
    std::vector<std::string> items;
    int selection = -1;
    DialogProc proc = nullptr;
    long resource = 0;
    int x = 0, y = 0, width = 0, height = 0;
};

struct Rect
{
    int left, top, right, bottom;
};

enum
{
    WM_DESTROY = 0x2,
    WM_INITDIALOG = 0x110,
    WM_COMMAND = 0x111,
    BM_GETCHECK = 0xf0,
    BM_SETCHECK = 0xf1,
    CB_GETCOUNT = 0x146,
    CB_GETCURSEL = 0x147,
    CB_GETLBTEXT = 0x148,
    CB_SETCURSEL = 0x14e,
    EN_CHANGE = 0x300,
    GW_HWNDNEXT = 2,
    GW_CHILD = 5,
    GWL_ID = -12,
    GWL_STYLE = -16,
    OWN_VISIBLE = 0x02000000,
};

std::vector<std::unique_ptr<Menu>> menus;
std::map<long, Window> windows;
std::map<long, Control> controls;
long nextControl = 1000;
HWND foreground;
int messageAnswer = 1;
std::vector<std::string> nested;
std::string swellFolder;
std::string resourceFolder;
void (*timer)();
int (*entry)(void*, PluginInfo*);
GMainLoop* loop;

HWND handle(long number)
{
    return reinterpret_cast<HWND>(number);
}

Window* windowOf(HWND window)
{
    const auto found = windows.find(reinterpret_cast<long>(window));
    return found == windows.end() ? nullptr : &found->second;
}

Menu* newMenu(std::vector<Item> items)
{
    menus.push_back(std::make_unique<Menu>(Menu{std::move(items)}));
    return menus.back().get();
}

Item* statusItem()
{
    return &windows[1].menu->items.back();
}

Control* controlOf(HWND window)
{
    const auto found = controls.find(reinterpret_cast<long>(window));
    return found == controls.end() ? nullptr : &found->second;
}

long addControl(long parent, Control control)
{
    control.parent = parent;
    controls[nextControl] = std::move(control);
    return nextControl++;
}

// the address reaper would get from swell, which a plugin may have redirected
void* swellFunction(const char* name)
{
    return dlsym(RTLD_DEFAULT, ("fake_" + std::string(name)).c_str());
}

std::string templateState(long window)
{
    std::string state;
    for (const auto& [number, control] : controls)
        if (control.parent == window && control.kind == "Button")
            state += std::to_string(control.id) + ":" + std::to_string(control.check) + " ";
        else if (control.parent == window && control.kind == "combobox")
            state += std::to_string(control.id) + ":#" + std::to_string(control.selection) + " ";
        else if (control.parent == window && control.kind == "Edit")
            state += std::to_string(control.id) + ":=" + control.text + " ";
    return state;
}

Control* childOf(long window, long id)
{
    for (auto& [number, control] : controls)
        if (control.parent == window && control.id == id)
            return &control;
    return nullptr;
}

// reaper's save project procedure, ticks wake controls and format runs a modal loop
void saveProjectCommand(long window, unsigned long id)
{
    auto wake = [window](std::initializer_list<long> ids, bool on) {
        for (long each : ids)
            childOf(window, each)->enabled = on;
    };
    if (id == 1043)
        wake({1057}, childOf(window, 1043)->check);
    else if (id == 1057)
        wake({1056, 1058, 1577, 1578}, childOf(window, 1057)->check && childOf(window, 1057)->enabled);
    else if (id == 1044)
        wake({1045}, childOf(window, 1044)->check);
    else if (id == 1056)
    {
        printf("[host] template modal start\n");
        fflush(stdout);
        const gint64 end = g_get_monotonic_time() + 300000;
        while (g_get_monotonic_time() < end)
            if (!g_main_context_iteration(nullptr, FALSE))
                g_usleep(5000);
        printf("[host] template modal end\n");
    }
}

// stands in for reaper's own dialog procedure behind a browse template
long templateProc(HWND window, unsigned message, unsigned long command, long)
{
    if (message == WM_INITDIALOG)
        printf("[host] template init window=%ld\n", reinterpret_cast<long>(window));
    else if (message == WM_COMMAND)
    {
        printf("[host] template command id=%lu code=%lu\n", command & 0xffff, command >> 16);
        if (controlOf(window)->resource == 5)
            saveProjectCommand(reinterpret_cast<long>(window), command & 0xffff);
    }
    else if (message == WM_DESTROY)
        printf("[host] template destroy state=%s\n", templateState(reinterpret_cast<long>(window)).c_str());
    return 0;
}

Control widget(const char* kind, const char* text, long id, unsigned long style)
{
    Control control;
    control.kind = kind;
    control.text = text;
    control.id = id;
    control.style = style;
    return control;
}

Control placed(Control control, int x, int y, int width, int height, bool enabled = true)
{
    control.x = x;
    control.y = y;
    control.width = width;
    control.height = height;
    control.enabled = enabled;
    return control;
}

// template 0xb9 as reaper 7.83 builds it, plus a hidden control and a label that needs escaping
void fillSaveProject(long window)
{
    addControl(window, placed(widget("Button", "Create subdirectory for project", 1042, 0x40010003), 8, 2, 209, 19));
    addControl(window, placed(widget("Button", "Copy all media into project directory, using:", 1043, 0x40010003), 8, 23, 277, 19));
    addControl(window, placed(widget("Button", "Convert media", 1057, 0x40010003), 291, 23, 112, 19, false));
    addControl(window, placed(widget("Button", "Format...", 1056, 0x40000000), 403, 21, 68, 23, false));
    addControl(window, placed(widget("Button", "Trim media, tail size:", 1058, 0x40010003), 479, 23, 142, 19, false));
    addControl(window, placed(widget("Edit", "500", 1577, 0x40002080), 625, 21, 66, 23, false));
    addControl(window, placed(widget("Static", "ms", 1578, 0x40000000), 695, 25, 19, 15, false));
    addControl(window, placed(widget("Button", "Move all media into project directory", 1044, 0x40010003), 8, 46, 245, 19));
    addControl(window, placed(widget("Button", "Copy \\ rather\tthan move", 1045, 0x40010003), 260, 46, 431, 19, false));
    controls[addControl(window, placed(widget("Button", "Hidden option", 1046, 0x40010003), 8, 70, 100, 19))].visible = false;
}

void fillTemplate(long window, long kind)
{
    if (kind == 5)
        return fillSaveProject(window);
    addControl(window, widget("Button", "Open in new &tab", 1042, 0x40010003));
    controls[addControl(window, widget("Button", "Open read-only", 1043, 0x40010003))].check = 1;
    controls[addControl(window, widget("Button", "Hidden option", 1044, 0x40010003))].visible = false;
    controls[addControl(window, widget("Button", "Disabled option", 1045, 0x48010003))].enabled = false;
    addControl(window, widget("Static", "Format:", 1049, 0x40000000));
    Control& list = controls[addControl(window, widget("combobox", "", 1050, 0x40010003))];
    list.items = {"WAV", "FLAC"};
    list.selection = 1;
    if (kind == 2)
        controls[addControl(window, widget("Button", "Format...", 1056, 0x40000000))].enabled = false;
    else if (kind == 3)
        addControl(window, widget("combobox", "", 1051, 0x40010002));
    else if (kind == 4)
        addControl(window, widget("Edit", "500", 1577, 0x40002080));
}

std::string shown(const char* text, bool list)
{
    std::string out;
    for (const char* part = text; part && (part == text || *part); part += strlen(part) + 1)
    {
        out += part;
        if (!list)
            break;
        out += "|";
    }
    return out;
}

std::string extensions(const std::string& text)
{
    std::string list = text;
    for (char& c : list)
        if (c == '|')
            c = '\0';
    return list + '\0';
}

std::vector<std::string> fields(const std::string& line)
{
    std::vector<std::string> parts;
    std::istringstream words(line);
    for (std::string part; std::getline(words, part, '\t');)
        parts.push_back(part);
    parts.resize(5);
    return parts;
}

void browse(const std::string& command, const std::vector<std::string>& part)
{
    if (command == "open")
    {
        auto function = reinterpret_cast<char* (*)(const char*, const char*, const char*, bool, const char*)>(
            swellFunction("BrowseForFiles"));
        const std::string list = extensions(part[3]);
        char* value = function(part[1].c_str(), part[2].c_str(), "", part[0] == "1", list.c_str());
        printf("[host] result kind=open value=%s\n", value ? shown(value, true).c_str() : "null");
        free(value);
    }
    else if (command == "save")
    {
        auto function = reinterpret_cast<bool (*)(const char*, const char*, const char*, const char*, char*, int)>(
            swellFunction("BrowseForSaveFile"));
        const std::string list = extensions(part[3]);
        char out[4096] = "";
        const bool ok = function(part[0].c_str(), part[1].c_str(), part[2].c_str(), list.c_str(), out, sizeof(out));
        printf("[host] result kind=save ok=%d value=%s\n", ok, out);
    }
    else
    {
        auto function = reinterpret_cast<bool (*)(const char*, const char*, char*, int)>(swellFunction("BrowseForDirectory"));
        char out[4096] = "";
        const bool ok = function(part[0].c_str(), part[1].c_str(), out, sizeof(out));
        printf("[host] result kind=folder ok=%d value=%s\n", ok, out);
    }
}

const char* swellPath()
{
    return swellFolder.c_str();
}

const char* resourcePath()
{
    return resourceFolder.c_str();
}

int addHook(const char* name, void* function)
{
    printf("[host] register name=%s\n", name);
    if (!strcmp(name, "timer"))
        timer = reinterpret_cast<void (*)()>(function);
    else if (!strcmp(name, "-timer") && timer == function)
        timer = nullptr;
    return 1;
}

void* apiFunction(const char* name)
{
    if (!strcmp(name, "GetExePath"))
        return reinterpret_cast<void*>(swellPath);
    if (!strcmp(name, "GetResourcePath"))
        return reinterpret_cast<void*>(resourcePath);
    return nullptr;
}

void run(const std::string& line)
{
    std::istringstream words(line);
    std::string command;
    long number = 0;
    words >> command;
    if (command == "main")
    {
        Menu* file = newMenu({{"&New project\tCtrl+N", 40023}});
        Menu* help = newMenu({{"&About REAPER", 40007}});
        windows[1] = {101, newMenu({{"&File", 0, file}, {"&Help", 0, help}, {"", 40902}, {"", 50121}})};
    }
    else if (command == "status" || command == "undo")
    {
        Item* item = command == "status" ? statusItem() : statusItem() - 1;
        item->text = line.size() > command.size() + 1 ? line.substr(command.size() + 1) : "";
    }
    else if (command == "window")
    {
        std::string kind;
        unsigned xid = 0;
        words >> number >> xid >> kind;
        Menu* own = nullptr;
        if (kind == "menu")
            own = newMenu({{"&Edit", 0, newMenu({{"&Undo\tCtrl+Z", 40029}})}});
        else if (kind == "statusmenu")
            own = newMenu({{"&Edit", 0, newMenu({{"&Undo\tCtrl+Z", 40029}})},
                {"[recording settings]", 0, newMenu({{"Audio device settings...", 40099}})}});
        windows[number] = {xid, own};
    }
    else if (command == "hide" || command == "show" || command == "disable" || command == "enable")
    {
        words >> number;
        Window& window = windows[number];
        if (command == "hide" || command == "show")
            window.visible = command == "show";
        else
            window.enabled = command == "enable";
    }
    else if (command == "tick")
    {
        words >> number;
        for (long tick = 0; tick < number && timer; tick++)
            timer();
    }
    else if (command == "open" || command == "save" || command == "folder")
    {
        std::string rest;
        std::getline(words >> std::ws, rest);
        browse(command, fields(rest));
    }
    else if (command == "template")
    {
        words >> number;
        auto function = reinterpret_cast<void (*)(const char*, DialogProc, void*)>(swellFunction("BrowseFile_SetTemplate"));
        function(reinterpret_cast<const char*>(number), number ? templateProc : nullptr, number ? &controls : nullptr);
    }
    else if (command == "foreground")
    {
        words >> number;
        foreground = handle(number);
    }
    else if (command == "answer")
        words >> messageAnswer;
    else if (command == "nest")
    {
        std::string line;
        std::getline(words >> std::ws, line);
        nested.push_back(line);
    }
    else if (command == "unload")
        entry(nullptr, nullptr);
    else if (command == "quit")
        g_main_loop_quit(loop);
    else
    {
        printf("[host] refused: unknown command=%s\n", command.c_str());
        return;
    }
    printf("[host] ok %s\n", line.c_str());
}

gboolean readCommand(GIOChannel* channel, GIOCondition, gpointer)
{
    gchar* line = nullptr;
    gsize length = 0;
    if (g_io_channel_read_line(channel, &line, &length, nullptr, nullptr) != G_IO_STATUS_NORMAL)
    {
        g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
    }
    run(std::string(line, length && line[length - 1] == '\n' ? length - 1 : length));
    g_free(line);
    fflush(stdout);
    return G_SOURCE_CONTINUE;
}
} // namespace

extern "C"
{
__attribute__((visibility("default"))) guint32 gdk_x11_window_get_xid(void* window)
{
    return static_cast<guint32>(reinterpret_cast<uintptr_t>(window));
}

__attribute__((visibility("default"))) Menu* fake_GetMenu(HWND window)
{
    Window* found = windowOf(window);
    return found ? found->menu : nullptr;
}

__attribute__((visibility("default"))) Menu* fake_GetSubMenu(Menu* menu, int position)
{
    return position >= 0 && position < int(menu->items.size()) ? menu->items[position].submenu : nullptr;
}

__attribute__((visibility("default"))) int fake_GetMenuItemCount(Menu* menu)
{
    return menu ? int(menu->items.size()) : 0;
}

__attribute__((visibility("default"))) int fake_GetMenuItemInfo(Menu* menu, int position, int, MenuItemInfo* info)
{
    if (position < 0 || position >= int(menu->items.size()))
        return 0;
    const Item& item = menu->items[position];
    info->id = item.id;
    info->state = 0;
    info->type = 0;
    info->submenu = item.submenu;
    g_strlcpy(info->text, item.text.c_str(), info->length);
    return 1;
}

__attribute__((visibility("default"))) long fake_SendMessage(HWND window, unsigned message, unsigned long value, long data)
{
    Control* control = controlOf(window);
    if (!control)
    {
        printf("[host] sent window=%ld message=0x%x\n", reinterpret_cast<long>(window), message);
        return 0;
    }
    switch (message)
    {
    case BM_GETCHECK:
        return control->check;
    case BM_SETCHECK:
        control->check = int(value);
        return 0;
    case CB_GETCOUNT:
        return long(control->items.size());
    case CB_GETCURSEL:
        return control->selection;
    case CB_GETLBTEXT:
        if (value >= control->items.size())
            return -1;
        strcpy(reinterpret_cast<char*>(data), control->items[value].c_str());
        return long(control->items[value].size());
    case CB_SETCURSEL:
        control->selection = int(value);
        return control->selection;
    case WM_COMMAND:
        return control->proc ? control->proc(window, message, value, data) : 0;
    }
    return 0;
}

__attribute__((visibility("default"))) char* fake_BrowseForFiles(const char* text, const char*, const char*, bool, const char*)
{
    printf("[host] swell browse kind=open caption=%s\n", text);
    char* value = static_cast<char*>(calloc(16, 1));
    strcpy(value, "/swell/open");
    return value;
}

__attribute__((visibility("default"))) bool fake_BrowseForSaveFile(
    const char* text, const char*, const char*, const char*, char* out, int size)
{
    printf("[host] swell browse kind=save caption=%s\n", text);
    g_strlcpy(out, "/swell/save", size);
    return true;
}

__attribute__((visibility("default"))) bool fake_BrowseForDirectory(const char* text, const char*, char* out, int size)
{
    printf("[host] swell browse kind=folder caption=%s\n", text);
    g_strlcpy(out, "/swell/folder", size);
    return true;
}

__attribute__((visibility("default"))) void fake_BrowseFile_SetTemplate(const char* id, DialogProc, void*)
{
    printf("[host] swell template id=%ld\n", reinterpret_cast<long>(id));
}

__attribute__((visibility("default"))) HWND fake_GetForegroundWindow()
{
    return foreground;
}

__attribute__((visibility("default"))) int fake_EnableWindow(HWND window, int enable)
{
    printf("[host] enable window=%ld on=%d\n", reinterpret_cast<long>(window), enable);
    Window* found = windowOf(window);
    const bool was = found && found->enabled;
    if (found)
        found->enabled = enable;
    return !was;
}

__attribute__((visibility("default"))) void fake_SWELL_RunMessageLoop()
{
    const std::vector<std::string> lines = std::move(nested);
    nested.clear();
    for (const std::string& line : lines)
        run(line);
}

__attribute__((visibility("default"))) int fake_MessageBox(HWND, const char* text, const char* caption, int)
{
    std::string flat = text;
    for (char& c : flat)
        if (c == '\r' || c == '\n')
            c = ' ';
    printf("[host] messagebox caption=%s text=%s answer=%d\n", caption, flat.c_str(), messageAnswer);
    return messageAnswer;
}

__attribute__((visibility("default"))) HWND fake_SWELL_CreateDialog(void*, const char* resource, HWND parent, DialogProc proc, long data)
{
    const long kind = reinterpret_cast<long>(resource);
    if (kind == 9)
        return nullptr;
    const long window = addControl(reinterpret_cast<long>(parent), widget(kind ? "dialog" : "container", "", 0, 0x40000000));
    controls[window].proc = proc;
    controls[window].resource = kind;
    if (kind)
        fillTemplate(window, kind);
    printf("[host] dialog created window=%ld template=%ld parent=%ld\n", window, kind, reinterpret_cast<long>(parent));
    if (proc)
        proc(handle(window), WM_INITDIALOG, 0, data);
    return handle(window);
}

__attribute__((visibility("default"))) int fake_DestroyWindow(HWND window)
{
    const long number = reinterpret_cast<long>(window);
    Control* control = controlOf(window);
    if (control && control->proc)
        control->proc(window, WM_DESTROY, 0, 0);
    std::vector<long> children;
    for (const auto& [child, entry] : controls)
        if (entry.parent == number)
            children.push_back(child);
    for (long child : children)
        fake_DestroyWindow(handle(child));
    printf("[host] destroyed window=%ld\n", number);
    controls.erase(number);
    return 1;
}

__attribute__((visibility("default"))) HWND fake_GetWindow(HWND window, int relation)
{
    const long number = reinterpret_cast<long>(window);
    if (!window)
        return nullptr;
    if (relation == GW_CHILD)
    {
        for (const auto& [child, control] : controls)
            if (control.parent == number)
                return handle(child);
        return nullptr;
    }
    Control* control = controlOf(window);
    if (relation != GW_HWNDNEXT || !control)
        return nullptr;
    for (auto next = controls.upper_bound(number); next != controls.end(); ++next)
        if (next->second.parent == control->parent)
            return handle(next->first);
    return nullptr;
}

__attribute__((visibility("default"))) int fake_GetClassName(HWND window, char* out, int size)
{
    Control* control = controlOf(window);
    g_strlcpy(out, control ? control->kind.c_str() : "", size);
    return control ? int(control->kind.size()) : 0;
}

__attribute__((visibility("default"))) bool fake_GetDlgItemText(HWND window, int item, char* out, int size)
{
    Control* control = item ? nullptr : controlOf(window);
    g_strlcpy(out, control ? control->text.c_str() : "", size);
    return control != nullptr;
}

__attribute__((visibility("default"))) bool fake_SetDlgItemText(HWND window, int item, const char* text)
{
    Control* control = item ? nullptr : controlOf(window);
    if (control)
        control->text = text;
    return control != nullptr;
}

// dialogs sit at 300,200 on the screen so a plugin has to make its rects relative
__attribute__((visibility("default"))) bool fake_GetWindowRect(HWND window, Rect* out)
{
    Control* control = controlOf(window);
    if (!control)
        return false;
    Control* parent = controls.count(control->parent) ? &controls[control->parent] : nullptr;
    const int left = parent && parent->resource ? 300 + control->x : 300, top = parent && parent->resource ? 200 + control->y : 200;
    const bool dialog = !parent || !parent->resource;
    *out = {left, top, left + (dialog ? 723 : control->width), top + (dialog ? 68 : control->height)};
    return true;
}

__attribute__((visibility("default"))) long fake_GetWindowLong(HWND window, int index)
{
    Control* control = controlOf(window);
    if (!control)
        return 0;
    if (index == GWL_STYLE)
        return long(control->style | (control->visible ? OWN_VISIBLE : 0));
    return index == GWL_ID ? control->id : 0;
}

__attribute__((visibility("default"))) int fake_PostMessage(HWND window, unsigned message, unsigned long command, long)
{
    printf("[host] posted window=%ld message=0x%x command=%lu\n", reinterpret_cast<long>(window), message, command);
    fflush(stdout);
    return 1;
}

__attribute__((visibility("default"))) void* fake_SWELL_GetOSWindow(HWND window, const char* kind)
{
    Window* found = windowOf(window);
    return found && !strcmp(kind, "GdkWindow") ? reinterpret_cast<void*>(uintptr_t(found->xid)) : nullptr;
}

__attribute__((visibility("default"))) int fake_EnumWindows(int (*visit)(HWND, long), long data)
{
    for (const auto& [number, window] : windows)
        if (!visit(handle(number), data))
            break;
    return 1;
}

// as swell: visible only up a chain of visible parents, and a dialog nobody showed has none
__attribute__((visibility("default"))) int fake_IsWindowVisible(HWND window)
{
    Control* control = controlOf(window);
    if (!control)
    {
        Window* found = windowOf(window);
        return found && found->visible;
    }
    return control->visible && control->parent && fake_IsWindowVisible(handle(control->parent));
}

__attribute__((visibility("default"))) int fake_IsWindowEnabled(HWND window)
{
    Window* found = windowOf(window);
    Control* control = controlOf(window);
    return (found && found->enabled) || (control && control->enabled);
}
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: reaper-plugin-host SWELL_DIR PLUGIN RESOURCE_DIR\n");
        return 2;
    }
    swellFolder = argv[1];
    resourceFolder = argv[3];
    const std::string swell = swellFolder + "/libSwell.so";
    if (!dlopen(swell.c_str(), RTLD_NOW | RTLD_GLOBAL))
    {
        fprintf(stderr, "[host] refused: %s\n", dlerror());
        return 1;
    }
    void* plugin = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    entry = plugin ? reinterpret_cast<int (*)(void*, PluginInfo*)>(dlsym(plugin, "ReaperPluginEntry")) : nullptr;
    if (!entry)
    {
        fprintf(stderr, "[host] refused: %s\n", dlerror());
        return 1;
    }
    run("main");
    foreground = handle(1);
    PluginInfo info{0x20e, handle(1), addHook, apiFunction};
    printf("[host] loaded result=%d\n", entry(nullptr, &info));
    fflush(stdout);
    loop = g_main_loop_new(nullptr, FALSE);
    GIOChannel* input = g_io_channel_unix_new(0);
    g_io_add_watch(input, GIOCondition(G_IO_IN | G_IO_HUP), readCommand, nullptr);
    g_main_loop_run(loop);
    g_io_channel_unref(input);
    return 0;
}
