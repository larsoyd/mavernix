#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOST_WINDOWS 64
#define MOST_LINES 4000
#define DEEPEST 8
#define SCAN_MS 300
#define ANSWER_MS 2000
#define RETRY_MS 2000
#define SUBCLASS_ID 0x6d656e75
#define WATCHED L"MavernixWineMenuWatched"
#define ADDRESS L"__wine_appmenu_address"

enum { HIDE = 1, SHOW = 2, ATTACH = 3, DETACH = 4 };
enum { POPUP = 1, GRAYED = 2, CHECKED = 4, SEPARATOR = 8, RADIO = 16 };

struct text
{
    char *data;
    size_t used, size;
};

static struct
{
    HWND window;
    char *sent;
    BOOL hidden;
    BOOL settled;
    ATOM address;
    BOOL attached;
} shown[MOST_WINDOWS];
static int shown_count;
static UINT order_message;
static SOCKET link = INVALID_SOCKET;
static char token[80];
static struct text incoming;
static LONG started;

static void add(struct text *text, const char *format, ...)
{
    for (;;)
    {
        va_list arguments;
        va_start(arguments, format);
        size_t room = text->size - text->used;
        int needed = vsnprintf(text->data ? text->data + text->used : NULL, room, format, arguments);
        va_end(arguments);
        if (needed < 0)
            return;
        if ((size_t)needed < room)
        {
            text->used += needed;
            return;
        }
        text->size = (text->size + needed + 1) * 2;
        text->data = realloc(text->data, text->size);
    }
}

static BOOL send_text(const char *data, size_t size)
{
    while (size && link != INVALID_SOCKET)
    {
        int sent = send(link, data, (int)size, 0);
        // a half sent line would garble the stream, the loop sees the shutdown and lets go
        if (sent <= 0)
        {
            shutdown(link, SD_BOTH);
            return FALSE;
        }
        data += sent;
        size -= sent;
    }
    return link != INVALID_SOCKET;
}

static void tell(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof line - 1, format, arguments);
    va_end(arguments);
    if (length < 0 || length >= (int)sizeof line - 1)
        length = sizeof line - 2;
    line[length++] = '\n';
    send_text(line, length);
}

// runs on the window's own thread, the hidden bar keeps its menu so the program sees no change
static LRESULT CALLBACK bar_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR hidden)
{
    (void)id;
    if (message == order_message && (wparam == HIDE || wparam == SHOW))
    {
        if (hidden != (wparam == HIDE))
        {
            SetWindowSubclass(window, bar_proc, SUBCLASS_ID, wparam == HIDE);
            SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        return 1;
    }
    // on wayland the driver hands the address to the compositor when the window moves
    if (message == order_message && (wparam == ATTACH || wparam == DETACH))
    {
        if (wparam == ATTACH)
            SetPropW(window, ADDRESS, (HANDLE)lparam);
        else
            RemovePropW(window, ADDRESS);
        SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        return 1;
    }
    if (hidden && message == WM_NCCALCSIZE)
    {
        LONG_PTR menu = GetWindowLongPtrW(window, GWLP_ID);
        SetWindowLongPtrW(window, GWLP_ID, 0);
        LRESULT result = DefSubclassProc(window, message, wparam, lparam);
        SetWindowLongPtrW(window, GWLP_ID, menu);
        return result;
    }
    // alt would walk a bar nobody can see
    if (hidden && message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_KEYMENU)
        return 0;
    if (message == WM_NCDESTROY)
        RemovePropW(window, WATCHED);
    return DefSubclassProc(window, message, wparam, lparam);
}

static void hex(const UCHAR *bytes, int count, char *out)
{
    for (int at = 0; at < count; at++)
        sprintf(out + at * 2, "%02x", bytes[at]);
}

static BOOL sign(const char *text, char out[65])
{
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
    UCHAR digest[32];
    BOOL done = FALSE;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))
        return FALSE;
    if (!BCryptCreateHash(algorithm, &hash, NULL, 0, (UCHAR *)token, (ULONG)strlen(token), 0))
    {
        done = !BCryptHashData(hash, (UCHAR *)text, (ULONG)strlen(text), 0) &&
               !BCryptFinishHash(hash, digest, sizeof digest, 0);
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (done)
        hex(digest, sizeof digest, out);
    return done;
}

static BOOL read_line(char *line, size_t size, DWORD wait_ms)
{
    DWORD end = GetTickCount() + wait_ms;
    for (;;)
    {
        char *newline = incoming.used ? memchr(incoming.data, '\n', incoming.used) : NULL;
        if (newline)
        {
            size_t length = newline - incoming.data;
            if (length >= size)
                return FALSE;
            memcpy(line, incoming.data, length);
            line[length] = 0;
            memmove(incoming.data, newline + 1, incoming.used - length - 1);
            incoming.used -= length + 1;
            return TRUE;
        }
        LONG left = (LONG)(end - GetTickCount());
        if (left <= 0)
            return FALSE;
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(link, &readable);
        struct timeval timeout = {left / 1000, (left % 1000) * 1000};
        if (select(0, &readable, NULL, NULL, &timeout) <= 0)
            return FALSE;
        char chunk[4096];
        int got = recv(link, chunk, sizeof chunk, 0);
        if (got <= 0)
            return FALSE;
        add(&incoming, "%.*s", got, chunk);
    }
}

// both sides prove they hold the token before any menu crosses
static BOOL meet_bridge(void)
{
    char line[256], proof[65], nonce[33], expected[65], signed_text[128];
    UCHAR random[16];
    if (!read_line(line, sizeof line, ANSWER_MS) || strncmp(line, "challenge ", 10) || strlen(line + 10) > 64 ||
        BCryptGenRandom(NULL, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG))
        return FALSE;
    hex(random, sizeof random, nonce);
    snprintf(signed_text, sizeof signed_text, "client %s", line + 10);
    if (!sign(signed_text, proof))
        return FALSE;
    char exe[MAX_PATH] = "", *name;
    GetModuleFileNameA(NULL, exe, sizeof exe);
    name = strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe;
    tell("hello %s %s %lu %s", proof, nonce, GetCurrentProcessId(), name);
    snprintf(signed_text, sizeof signed_text, "bridge %s", nonce);
    return sign(signed_text, expected) && read_line(line, sizeof line, ANSWER_MS) && !strncmp(line, "welcome ", 8) &&
           !strcmp(line + 8, expected);
}

static void hang_up(void)
{
    closesocket(link);
    link = INVALID_SOCKET;
}

static BOOL connect_bridge(void)
{
    unsigned port = 0;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\MavernixWineMenu");
    const char *view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 256) : NULL;
    BOOL known = view && sscanf(view, "%u %79s", &port, token) == 2 && port && port < 65536;
    if (view)
        UnmapViewOfFile(view);
    if (mapping)
        CloseHandle(mapping);
    if (!known)
        return FALSE;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons((u_short)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    link = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    DWORD send_ms = ANSWER_MS;
    int on = 1;
    setsockopt(link, SOL_SOCKET, SO_SNDTIMEO, (const char *)&send_ms, sizeof send_ms);
    setsockopt(link, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof on);
    incoming.used = 0;
    if (link != INVALID_SOCKET && !connect(link, (struct sockaddr *)&address, sizeof address) && meet_bridge())
        return TRUE;
    if (link != INVALID_SOCKET)
        hang_up();
    return FALSE;
}

// only the subclass answers, a window whose thread is not hooked yet says no and is asked again
static BOOL order(HWND window, int what, ATOM address)
{
    DWORD_PTR answer = 0;
    return SendMessageTimeoutW(window, order_message, what, address, SMTO_ABORTIFHUNG, ANSWER_MS, &answer) && answer;
}

static void forget_address(int at)
{
    if (shown[at].address)
        GlobalDeleteAtom(shown[at].address);
    shown[at].address = 0;
    shown[at].attached = FALSE;
}

static void drop_bridge(void)
{
    hang_up();
    for (int at = 0; at < shown_count; at++)
    {
        if (shown[at].address && IsWindow(shown[at].window))
            order(shown[at].window, DETACH, 0);
        forget_address(at);
        if (shown[at].hidden && IsWindow(shown[at].window))
            order(shown[at].window, SHOW, 0);
        free(shown[at].sent);
    }
    shown_count = 0;
}

static int describe(struct text *text, HMENU menu, int depth, int lines)
{
    int count = GetMenuItemCount(menu);
    for (int position = 0; position < count && lines < MOST_LINES; position++)
    {
        WCHAR wide[512] = L"";
        char label[1024] = "";
        MENUITEMINFOW info = {.cbSize = sizeof info};
        info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        info.dwTypeData = wide;
        info.cch = ARRAYSIZE(wide) - 1;
        if (!GetMenuItemInfoW(menu, position, TRUE, &info))
            continue;
        unsigned flags = (info.hSubMenu ? POPUP : 0) | (info.fState & MFS_DISABLED ? GRAYED : 0) |
                         (info.fState & MFS_CHECKED ? CHECKED : 0) | (info.fType & MFT_SEPARATOR ? SEPARATOR : 0) |
                         (info.fType & MFT_RADIOCHECK ? RADIO : 0);
        // bitmaps such as a maximized child's window buttons carry no words
        if (!(flags & SEPARATOR) && !wide[0])
            continue;
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, label, sizeof label, NULL, NULL);
        for (char *at = label; *at; at++)
            if (*at == '\n' || *at == '\r')
                *at = ' ';
        add(text, "%d %d %x %u %s\n", depth, position, flags, info.wID, label);
        lines++;
        if (info.hSubMenu && depth < DEEPEST)
            lines = describe(text, info.hSubMenu, depth + 1, lines);
    }
    return lines;
}

static int shown_index(HWND window)
{
    for (int at = 0; at < shown_count; at++)
        if (shown[at].window == window)
            return at;
    return -1;
}

static void report(HWND window, BOOL always)
{
    // a window of wine's wayland driver has no x11 window, its menu goes out with xid 0
    ULONG_PTR xid = (ULONG_PTR)GetPropW(window, L"__wine_x11_whole_window");
    struct text items = {0}, message = {0};
    int at = shown_index(window);
    if ((!xid && !GetPropW(window, L"__wine_appmenu_supported")) || (at < 0 && shown_count == MOST_WINDOWS))
        return;
    int lines = describe(&items, GetMenu(window), 0, 0);
    if (!items.used)
        return;
    if (at < 0)
    {
        at = shown_count++;
        shown[at].window = window;
        shown[at].sent = NULL;
        shown[at].hidden = FALSE;
        shown[at].settled = TRUE;
        shown[at].address = 0;
        shown[at].attached = FALSE;
    }
    if (always || !shown[at].sent || strcmp(shown[at].sent, items.data))
    {
        add(&message, "menu %lx %lx %d\n%s", (unsigned long)(ULONG_PTR)window, (unsigned long)xid, lines, items.data);
        send_text(message.data, message.used);
        free(shown[at].sent);
        shown[at].sent = items.data;
        items.data = NULL;
    }
    free(items.data);
    free(message.data);
}

static BOOL CALLBACK collect(HWND window, LPARAM list)
{
    DWORD process;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && IsWindowVisible(window) && GetMenu(window))
    {
        HWND *found = (HWND *)list;
        for (int at = 0; at < MOST_WINDOWS; at++)
            if (!found[at])
            {
                found[at] = window;
                break;
            }
    }
    return TRUE;
}

static void scan(void)
{
    HWND found[MOST_WINDOWS] = {0};
    EnumWindows(collect, (LPARAM)found);
    for (int at = 0; at < MOST_WINDOWS && found[at]; at++)
        report(found[at], FALSE);
    for (int at = 0; at < shown_count; at++)
    {
        if (shown[at].address && !shown[at].attached)
            shown[at].attached = order(shown[at].window, ATTACH, shown[at].address);
        if (!shown[at].settled)
            shown[at].settled = order(shown[at].window, shown[at].hidden ? HIDE : SHOW, 0);
    }
    for (int at = 0; at < shown_count;)
    {
        BOOL present = FALSE;
        for (int seen = 0; seen < MOST_WINDOWS && found[seen]; seen++)
            present |= found[seen] == shown[at].window;
        if (present)
        {
            at++;
            continue;
        }
        tell("gone %lx", (unsigned long)(ULONG_PTR)shown[at].window);
        // a hidden window keeps its properties, a freed atom there could name someone else's menu later
        if (shown[at].address && IsWindow(shown[at].window))
            order(shown[at].window, DETACH, 0);
        forget_address(at);
        free(shown[at].sent);
        shown[at] = shown[--shown_count];
    }
}

// a route is the positions from the bar down, "-" is the bar itself
static HMENU menu_at(HWND window, const char *route, int *last)
{
    HMENU menu = GetMenu(window);
    *last = -1;
    if (!strcmp(route, "-"))
        return menu;
    for (const char *step = route; menu && *step;)
    {
        char *end;
        *last = strtol(step, &end, 10);
        menu = GetSubMenu(menu, *last);
        step = *end == '.' ? end + 1 : end;
    }
    return menu;
}

static void activate(HWND window, const char *route, int position)
{
    int last;
    HMENU menu = menu_at(window, route, &last), bar = GetMenu(window);
    MENUITEMINFOW info = {.cbSize = sizeof info};
    info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU;
    MENUINFO style = {.cbSize = sizeof style};
    style.fMask = MIM_STYLE;
    if (!menu || !GetMenuItemInfoW(menu, position, TRUE, &info) || info.hSubMenu || info.fType & MFT_SEPARATOR)
        return tell("note command refused route=%s position=%d reason=no_item", route, position);
    if (info.fState & MFS_DISABLED)
        return tell("note command refused id=%u reason=grayed", info.wID);
    // a modal dialog has the window disabled and would not expect a command
    if (!IsWindowEnabled(window))
        return tell("note command refused id=%u reason=window_disabled", info.wID);
    if (GetMenuInfo(bar, &style) && style.dwStyle & MNS_NOTIFYBYPOS)
        PostMessageW(window, WM_MENUCOMMAND, position, (LPARAM)menu);
    else
        PostMessageW(window, WM_COMMAND, MAKEWPARAM(info.wID, 0), 0);
    tell("note command id=%u", info.wID);
}

static void open_menu(HWND window, const char *serial, const char *route)
{
    int last;
    HMENU menu = menu_at(window, route, &last);
    if (menu && last >= 0)
    {
        if (!strchr(route, '.'))
            SendMessageTimeoutW(window, WM_INITMENU, (WPARAM)GetMenu(window), 0, SMTO_ABORTIFHUNG, ANSWER_MS, NULL);
        SendMessageTimeoutW(window, WM_INITMENUPOPUP, (WPARAM)menu, MAKELPARAM(last, FALSE), SMTO_ABORTIFHUNG,
                            ANSWER_MS, NULL);
        report(window, TRUE);
    }
    tell("opened %lx %s", (unsigned long)(ULONG_PTR)window, serial);
}

static void close_menu(HWND window, const char *route)
{
    int last;
    HMENU menu = menu_at(window, route, &last);
    if (menu && last >= 0)
        SendMessageTimeoutW(window, WM_UNINITMENUPOPUP, (WPARAM)menu, 0, SMTO_ABORTIFHUNG, ANSWER_MS, NULL);
}

static void obey(char *line)
{
    char verb[16], first[80], second[80];
    unsigned long handle;
    int fields = sscanf(line, "%15s %lx %79s %79s", verb, &handle, first, second);
    HWND window = (HWND)(ULONG_PTR)handle;
    DWORD process = 0;
    if (fields < 2 || !IsWindow(window) || (GetWindowThreadProcessId(window, &process), process != GetCurrentProcessId()))
        return;
    int at = shown_index(window);
    if ((!strcmp(verb, "hide") || !strcmp(verb, "show")) && at >= 0)
    {
        shown[at].hidden = verb[0] == 'h';
        shown[at].settled = order(window, shown[at].hidden ? HIDE : SHOW, 0) || !shown[at].hidden;
    }
    else if (!strcmp(verb, "attach") && fields == 4 && at >= 0)
    {
        WCHAR wide[160];
        if (shown[at].address)
            order(window, DETACH, 0);
        forget_address(at);
        if (_snwprintf(wide, ARRAYSIZE(wide), L"%hs %hs", first, second) < 0 || !(shown[at].address = GlobalAddAtomW(wide)))
            return tell("note attach refused window=%lx reason=no_atom", handle);
        // a thread not hooked yet says no, the scan asks again
        shown[at].attached = order(window, ATTACH, shown[at].address);
    }
    else if (!strcmp(verb, "detach") && at >= 0 && shown[at].address)
    {
        order(window, DETACH, 0);
        forget_address(at);
    }
    else if (!strcmp(verb, "activate") && fields == 4)
        activate(window, first, atoi(second));
    else if (!strcmp(verb, "open") && fields == 4)
        open_menu(window, first, second);
    else if (!strcmp(verb, "close") && fields == 3)
        close_menu(window, first);
}

static DWORD WINAPI keep_in_touch(void *unused)
{
    (void)unused;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
    for (;;)
    {
        if (link == INVALID_SOCKET && !connect_bridge())
        {
            Sleep(RETRY_MS);
            continue;
        }
        char line[256];
        while (link != INVALID_SOCKET && read_line(line, sizeof line, 0))
            obey(line);
        scan();
        if (!read_line(line, sizeof line, SCAN_MS))
        {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(link, &readable);
            struct timeval now = {0, 0};
            char peek;
            if (select(0, &readable, NULL, NULL, &now) > 0 && recv(link, &peek, 1, MSG_PEEK) <= 0)
                drop_bridge();
            continue;
        }
        obey(line);
    }
}

// wine exports the subclass lookup by number only, so a property marks the windows taken
static void watch_window(HWND window)
{
    if (!(GetWindowLongW(window, GWL_STYLE) & WS_CHILD) && GetMenu(window) && !GetPropW(window, WATCHED) &&
        SetWindowSubclass(window, bar_proc, SUBCLASS_ID, FALSE))
        SetPropW(window, WATCHED, (HANDLE)1);
}

__declspec(dllexport) LRESULT CALLBACK menu_hook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION)
    {
        if (!InterlockedExchange(&started, 1))
        {
            char off[4];
            HMODULE self;
            order_message = RegisterWindowMessageW(L"MavernixWineMenuOrder");
            if (!(GetEnvironmentVariableA("MAVERNIX_GLOBAL_MENU", off, sizeof off) && !strcmp(off, "0")) &&
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   (LPCWSTR)(void *)menu_hook, &self))
                CloseHandle(CreateThread(NULL, 0, keep_in_touch, NULL, 0, NULL));
            else
                order_message = 0;
        }
        if (order_message)
            watch_window(((const CWPSTRUCT *)lparam)->hwnd);
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}
