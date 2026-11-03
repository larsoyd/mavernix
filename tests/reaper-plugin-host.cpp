// loads the menu plugin as reaper does over a scripted swell, one command per stdin line
#include <dlfcn.h>
#include <glib.h>

#include <cstdio>
#include <cstring>
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

std::vector<std::unique_ptr<Menu>> menus;
std::map<long, Window> windows;
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

__attribute__((visibility("default"))) long fake_SendMessage(HWND window, unsigned message, unsigned long, long)
{
    printf("[host] sent window=%ld message=0x%x\n", reinterpret_cast<long>(window), message);
    return 0;
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

__attribute__((visibility("default"))) int fake_IsWindowVisible(HWND window)
{
    Window* found = windowOf(window);
    return found && found->visible;
}

__attribute__((visibility("default"))) int fake_IsWindowEnabled(HWND window)
{
    Window* found = windowOf(window);
    return found && found->enabled;
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
