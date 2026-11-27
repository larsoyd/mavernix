#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

#define MOST_THREADS 512
#define PROCESS_WINE_MAKE_PROCESS_SYSTEM 1000
#define QT_NATIVE_MENUS L"windows:menus=native"

typedef LONG(NTAPI *set_process_info)(HANDLE, ULONG, void *, ULONG);

static struct
{
    DWORD thread;
    HHOOK hook;
} hooked[MOST_THREADS];
static int hooked_count;
static HMODULE client;
static HOOKPROC menu_hook;
static BOOL own_wow64;

static void say(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    fprintf(stderr, "[wine-menu-agent%d] ", (int)sizeof(void *) * 8);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(arguments);
}

// the session waits for system processes only until the last program ends
static HANDLE become_system(void)
{
    HANDLE ends = NULL;
    set_process_info set = (set_process_info)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess");
    if (!set || set(GetCurrentProcess(), PROCESS_WINE_MAKE_PROCESS_SYSTEM, &ends, sizeof(HANDLE *)))
        return NULL;
    return ends;
}

static HANDLE publish_bridge(void)
{
    char port[16], token[80];
    if (!GetEnvironmentVariableA("MAVERNIX_WINE_MENU_PORT", port, sizeof port) ||
        !GetEnvironmentVariableA("MAVERNIX_WINE_MENU_TOKEN", token, sizeof token))
        return NULL;
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 256, L"Local\\MavernixWineMenu");
    char *view = mapping ? MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 256) : NULL;
    if (!view)
        return NULL;
    snprintf(view, 256, "%s %s", port, token);
    UnmapViewOfFile(view);
    return mapping;
}

static void ask_qt_for_native_menus(void)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL))
        return;
    // a value the user chose stays
    if (RegQueryValueExW(key, L"QT_QPA_PLATFORM", NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND &&
        !RegSetValueExW(key, L"QT_QPA_PLATFORM", 0, REG_SZ, (const BYTE *)QT_NATIVE_MENUS, sizeof QT_NATIVE_MENUS))
        say("qt programs started from now on use win32 menus");
    RegCloseKey(key);
}

// a patched wine loads these into every program as it starts, before its first window
static void load_client_at_start(const WCHAR *client_path)
{
    WCHAR list[1024] = L"";
    DWORD size = sizeof list - sizeof(WCHAR), load = 1;
    HKEY key;
    if (wcschr(client_path, L' ') || wcslen(client_path) >= ARRAYSIZE(list) / 2)
        return say("refused: the client path %ls cannot go in a space separated list", client_path);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Windows", 0, NULL, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL))
        return say("refused: no windows key for AppInit_DLLs error=%lu", GetLastError());
    if (RegQueryValueExW(key, L"AppInit_DLLs", NULL, NULL, (BYTE *)list, &size))
        list[0] = 0;
    list[size / sizeof(WCHAR)] = 0;
    BOOL listed = FALSE;
    for (WCHAR *name = list, *end; *name; name = end)
    {
        while (*name == L' ' || *name == L',')
            name++;
        for (end = name; *end && *end != L' ' && *end != L','; end++)
            ;
        listed |= end - name == (ptrdiff_t)wcslen(client_path) && !_wcsnicmp(name, client_path, end - name);
    }
    // a library another program put there stays
    if (!listed)
    {
        if (wcslen(list) + 1 + wcslen(client_path) >= ARRAYSIZE(list))
            return RegCloseKey(key), say("refused: AppInit_DLLs is full");
        if (list[0])
            wcscat(list, L" ");
        wcscat(list, client_path);
        if (RegSetValueExW(key, L"AppInit_DLLs", 0, REG_SZ, (const BYTE *)list, (wcslen(list) + 1) * sizeof(WCHAR)))
            return RegCloseKey(key), say("refused: AppInit_DLLs not written");
    }
    if (RegSetValueExW(key, L"LoadAppInit_DLLs", 0, REG_DWORD, (const BYTE *)&load, sizeof load))
        say("refused: LoadAppInit_DLLs not written");
    else
        say("programs load the client from their start");
    RegCloseKey(key);
}

static BOOL is_hooked(DWORD thread)
{
    for (int at = 0; at < hooked_count; at++)
        if (hooked[at].thread == thread)
            return TRUE;
    return FALSE;
}

// a hook only loads a library of the program's own width
static BOOL same_width(DWORD process)
{
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    BOOL wow64 = FALSE, known = handle && IsWow64Process(handle, &wow64);
    if (handle)
        CloseHandle(handle);
    return known && wow64 == own_wow64;
}

static BOOL CALLBACK visit(HWND window, LPARAM unused)
{
    (void)unused;
    DWORD process, thread;
    // a program that loaded the client at its start watches its own windows
    if (!IsWindowVisible(window) || !GetMenu(window) || GetPropW(window, L"MavernixWineMenuWatched") ||
        hooked_count == MOST_THREADS)
        return TRUE;
    thread = GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() || is_hooked(thread) || !same_width(process))
        return TRUE;
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, menu_hook, client, thread);
    hooked[hooked_count].thread = thread;
    hooked[hooked_count++].hook = hook;
    if (!hook)
    {
        say("hook refused process=%lu thread=%lu error=%lu", process, thread, GetLastError());
        return TRUE;
    }
    say("hooked process=%lu thread=%lu window=%p", process, thread, window);
    SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, NULL);
    return TRUE;
}

static void forget_ended_threads(void)
{
    for (int at = 0; at < hooked_count;)
    {
        DWORD status = 0;
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, hooked[at].thread);
        BOOL alive = thread && GetExitCodeThread(thread, &status) && status == STILL_ACTIVE;
        if (thread)
            CloseHandle(thread);
        if (alive)
        {
            at++;
            continue;
        }
        if (hooked[at].hook)
            UnhookWindowsHookEx(hooked[at].hook);
        hooked[at] = hooked[--hooked_count];
    }
}

// wine makes a new desktop after the last program on the old one closes, the agent outlives it
// so the name is kept from the start, a desktop that is gone no longer gives it
static WCHAR desktop_name[64];
static HDESK followed;
static void follow_desktop(void)
{
    if (hooked_count || !desktop_name[0])
        return;
    HDESK desktop = OpenDesktopW(desktop_name, 0, FALSE, DESKTOP_ENUMERATE | DESKTOP_READOBJECTS | DESKTOP_HOOKCONTROL);
    if (!desktop)
        return;
    if (!SetThreadDesktop(desktop))
    {
        say("refused: cannot follow desktop %ls error=%lu", desktop_name, GetLastError());
        CloseDesktop(desktop);
        return;
    }
    if (followed)
        CloseDesktop(followed);
    followed = desktop;
}

static BOOL path_beside(const WCHAR *name, WCHAR path[MAX_PATH])
{
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    WCHAR *slash = wcsrchr(path, L'\\');
    if (!length || length == MAX_PATH || !slash || wcslen(name) >= (size_t)(path + MAX_PATH - slash - 1))
        return FALSE;
    wcscpy(slash + 1, name);
    return TRUE;
}

static void start_watcher(const WCHAR *name)
{
    WCHAR path[MAX_PATH], line[MAX_PATH + 16];
    STARTUPINFOW startup = {.cb = sizeof startup, .dwFlags = STARTF_USESTDHANDLES};
    PROCESS_INFORMATION started;
    if (!path_beside(name, path))
        return;
    swprintf(line, MAX_PATH + 16, L"\"%ls\" --watch", path);
    // a console host would be one more process the prefix waits for
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(path, line, NULL, NULL, TRUE, DETACHED_PROCESS, NULL, NULL, &startup, &started))
        return say("refused: watcher did not start error=%lu", GetLastError());
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
}

// wine may start us under a launcher that waits for us, so the watchers start from here
static int start_watchers(void)
{
    WCHAR wow64[MAX_PATH];
    start_watcher(AGENT_NAME);
#ifdef SIBLING_NAME
    if (GetSystemWow64DirectoryW(wow64, MAX_PATH))
        start_watcher(SIBLING_NAME);
#endif
    (void)wow64;
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    WCHAR client_path[MAX_PATH];
    if (argc < 2 || wcscmp(argv[1], L"--watch"))
        return start_watchers();
    HANDLE published = publish_bridge();
    if (!published)
        return say("refused: no bridge to publish"), 1;
    CreateMutexW(NULL, FALSE, sizeof(void *) == 8 ? L"Local\\MavernixWineMenuAgent64" : L"Local\\MavernixWineMenuAgent32");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return say("an agent runs already, it now reaches the new bridge"), 0;
    HANDLE ends = become_system();
    if (!ends)
        return say("refused: cannot become a system process, it would keep the prefix running"), 1;
    client = path_beside(CLIENT_NAME, client_path) ? LoadLibraryW(client_path) : NULL;
    menu_hook = client ? (HOOKPROC)(void *)GetProcAddress(client, "menu_hook") : NULL;
    if (!menu_hook)
        return say("refused: no client library error=%lu", GetLastError()), 1;
    IsWow64Process(GetCurrentProcess(), &own_wow64);
    if (!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop_name, sizeof desktop_name, NULL))
        say("refused: no desktop name error=%lu, programs after a desktop restart are missed", GetLastError());
    ask_qt_for_native_menus();
    load_client_at_start(client_path);
    say("watching");
    while (WaitForSingleObject(ends, 1000) == WAIT_TIMEOUT)
    {
        forget_ended_threads();
        follow_desktop();
        EnumWindows(visit, 0);
    }
    say("the prefix ends threads=%d", hooked_count);
    CloseHandle(published);
    return 0;
}
