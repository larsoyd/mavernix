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
// a new window waits this long at most for the bridge before it shows its own bar
#define READY_MS 250
#define MOST_ASKED 16
#define SUBCLASS_ID 0x6d656e75
#define WATCHED L"MavernixWineMenuWatched"
#define ADDRESS L"__wine_appmenu_address"

enum { HIDE = 1, SHOW = 2, ATTACH = 3, DETACH = 4 };
enum { CONNECTING, READY, UNREACHABLE };
enum { FREE, ASKED, SENT, EXPORTED, FAILED };
// subclass data, the bar is hidden and the window was offered to the bridge
enum { BAR_HIDDEN = 1, OFFERED = 2 };
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
    // offered before it was shown, the scan must not take it for gone
    BOOL fresh;
} shown[MOST_WINDOWS];
static int shown_count;
static UINT order_message;
static SOCKET link = INVALID_SOCKET;
static char token[80];
static struct text incoming;

static int bridge_state = CONNECTING;
static BOOL panel;
static char bus_name[128];

// windows offered before they show, shared with their threads under the lock
static CRITICAL_SECTION lock;
static struct
{
    HWND window;
    int state;
    ATOM address;
    HANDLE answered;
} asked[MOST_ASKED];
static SOCKET wake = INVALID_SOCKET;
static struct sockaddr_in wake_address;

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

static BOOL offer(HWND window);

// runs on the window's own thread, the hidden bar keeps its menu so the program sees no change
static LRESULT CALLBACK bar_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    (void)id;
    // a menu given after the window was made is offered before the frame that would show it
    if (message == WM_NCCALCSIZE && !(data & OFFERED) && GetMenu(window))
    {
        data |= OFFERED | (offer(window) ? BAR_HIDDEN : 0);
        SetWindowSubclass(window, bar_proc, SUBCLASS_ID, data);
    }
    BOOL hidden = data & BAR_HIDDEN;
    if (message == order_message && (wparam == HIDE || wparam == SHOW))
    {
        if (hidden != (wparam == HIDE))
        {
            SetWindowSubclass(window, bar_proc, SUBCLASS_ID, (data & ~BAR_HIDDEN) | (wparam == HIDE ? BAR_HIDDEN : 0));
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

// the agent publishes the bridge in the prefix, the bridge itself in the session's runtime folder
static BOOL find_bridge(unsigned *port)
{
    char runtime[MAX_PATH], path[MAX_PATH + 32], text[128] = "";
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\MavernixWineMenu");
    const char *view = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 256) : NULL;
    if (view)
        snprintf(text, sizeof text, "%s", view);
    if (view)
        UnmapViewOfFile(view);
    if (mapping)
        CloseHandle(mapping);
    DWORD length = GetEnvironmentVariableA("WINE_HOST_XDG_RUNTIME_DIR", runtime, sizeof runtime);
    if (!text[0] && length && length < sizeof runtime)
    {
        snprintf(path, sizeof path, "Z:%s/mavernix-wine-menu", runtime);
        for (char *slash = path; (slash = strchr(slash, '/'));)
            *slash = '\\';
        FILE *file = fopen(path, "r");
        if (file && !fgets(text, sizeof text, file))
            text[0] = 0;
        if (file)
            fclose(file);
    }
    return sscanf(text, "%u %79s", port, token) == 2 && *port && *port < 65536;
}

static BOOL connect_bridge(void)
{
    unsigned port = 0;
    if (!find_bridge(&port))
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
    bridge_state = UNREACHABLE;
    EnterCriticalSection(&lock);
    for (int at = 0; at < MOST_ASKED; at++)
        if (asked[at].state == ASKED || asked[at].state == SENT)
        {
            asked[at].state = FAILED;
            SetEvent(asked[at].answered);
        }
    LeaveCriticalSection(&lock);
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
    // the driver marks a window it can publish only once it shows, an offered one goes out before
    BOOL early = at >= 0 && shown[at].fresh;
    if ((!xid && !early && !GetPropW(window, L"__wine_appmenu_supported")) || (at < 0 && shown_count == MOST_WINDOWS))
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
        shown[at].fresh = FALSE;
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
            shown[at].fresh = FALSE;
        if (present || (shown[at].fresh && IsWindow(shown[at].window)))
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

static void answer(HWND window, int state)
{
    EnterCriticalSection(&lock);
    for (int at = 0; at < MOST_ASKED; at++)
        if (asked[at].window == window && (asked[at].state == ASKED || asked[at].state == SENT))
        {
            asked[at].state = state;
            SetEvent(asked[at].answered);
        }
    LeaveCriticalSection(&lock);
}

static void obey(char *line)
{
    char verb[16], first[80], second[80], name[128];
    unsigned long handle;
    int on;
    if (sscanf(line, "bridge %127s %d", name, &on) == 2)
    {
        strcpy(bus_name, name);
        panel = on;
        bridge_state = READY;
        return;
    }
    if (sscanf(line, "panel %d", &on) == 1)
    {
        panel = on;
        return;
    }
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
        WCHAR wide[160], held[160];
        // a window offered before it showed already names this menu
        if (_snwprintf(wide, ARRAYSIZE(wide), L"%hs %hs", first, second) >= 0 && shown[at].address &&
            GlobalGetAtomNameW(shown[at].address, held, ARRAYSIZE(held)) && !wcscmp(held, wide))
        {
            shown[at].attached = TRUE;
            return;
        }
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
    else if (!strcmp(verb, "exported"))
        answer(window, EXPORTED);
    else if (!strcmp(verb, "activate") && fields == 4)
        activate(window, first, atoi(second));
    else if (!strcmp(verb, "open") && fields == 4)
        open_menu(window, first, second);
    else if (!strcmp(verb, "close") && fields == 3)
        close_menu(window, first);
}

// a datagram to ourselves ends the bridge thread's wait when a window is offered
static void open_wake(void)
{
    int size = sizeof wake_address;
    SOCKET own = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    wake_address.sin_family = AF_INET;
    wake_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (own != INVALID_SOCKET && !bind(own, (struct sockaddr *)&wake_address, sizeof wake_address) &&
        !getsockname(own, (struct sockaddr *)&wake_address, &size))
        wake = own;
    else if (own != INVALID_SOCKET)
        closesocket(own);
}

static void wait_input(DWORD wait_ms)
{
    fd_set readable;
    FD_ZERO(&readable);
    if (link != INVALID_SOCKET)
        FD_SET(link, &readable);
    if (wake != INVALID_SOCKET)
        FD_SET(wake, &readable);
    if (!readable.fd_count)
        return Sleep(wait_ms);
    struct timeval timeout = {wait_ms / 1000, (wait_ms % 1000) * 1000};
    if (select(0, &readable, NULL, NULL, &timeout) <= 0)
        return;
    char chunk[4096];
    if (wake != INVALID_SOCKET && FD_ISSET(wake, &readable))
        recv(wake, chunk, sizeof chunk, 0);
    if (link != INVALID_SOCKET && FD_ISSET(link, &readable))
    {
        int got = recv(link, chunk, sizeof chunk, 0);
        if (got <= 0)
            drop_bridge();
        else
            add(&incoming, "%.*s", got, chunk);
    }
}

// on the bridge thread, an offered window goes out hidden and named before it shows
static void take_offers(void)
{
    HWND waiting[MOST_ASKED];
    int count = 0;
    EnterCriticalSection(&lock);
    for (int at = 0; at < MOST_ASKED; at++)
        if (asked[at].state == ASKED)
            waiting[count++] = asked[at].window;
    LeaveCriticalSection(&lock);
    for (int next = 0; next < count && bridge_state != CONNECTING; next++)
    {
        HWND window = waiting[next];
        WCHAR wide[192];
        int at = shown_index(window);
        if (bridge_state != READY || !panel || (at < 0 && shown_count == MOST_WINDOWS) ||
            _snwprintf(wide, ARRAYSIZE(wide), L"%hs /com/canonical/menu/wine/p%lu_%lx", bus_name, GetCurrentProcessId(),
                       (unsigned long)(ULONG_PTR)window) < 0)
        {
            answer(window, FAILED);
            continue;
        }
        if (at < 0)
        {
            at = shown_count++;
            shown[at].window = window;
            shown[at].sent = NULL;
        }
        else
            forget_address(at);
        shown[at].hidden = TRUE;
        shown[at].settled = TRUE;
        shown[at].fresh = TRUE;
        shown[at].attached = TRUE;
        shown[at].address = GlobalAddAtomW(wide);
        SetPropW(window, ADDRESS, (HANDLE)(ULONG_PTR)shown[at].address);
        EnterCriticalSection(&lock);
        for (int slot = 0; slot < MOST_ASKED; slot++)
            if (asked[slot].window == window && asked[slot].state == ASKED)
                asked[slot].state = SENT;
        LeaveCriticalSection(&lock);
        report(window, TRUE);
        // an empty bar sends nothing, so nothing would ever be exported
        if (!shown[at].sent)
        {
            RemovePropW(window, ADDRESS);
            forget_address(at);
            shown[at] = shown[--shown_count];
            answer(window, FAILED);
        }
    }
}

static DWORD WINAPI keep_in_touch(void *unused)
{
    (void)unused;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data))
        return 1;
    open_wake();
    for (;;)
    {
        if (link == INVALID_SOCKET)
        {
            bridge_state = CONNECTING;
            if (!connect_bridge())
            {
                bridge_state = UNREACHABLE;
                take_offers();
                wait_input(RETRY_MS);
                continue;
            }
        }
        char line[256];
        while (link != INVALID_SOCKET && read_line(line, sizeof line, 0))
            obey(line);
        take_offers();
        scan();
        wait_input(SCAN_MS);
    }
}

static BOOL CALLBACK begin_contact(INIT_ONCE *once, void *unused, void **context)
{
    (void)once, (void)unused, (void)context;
    char off[4];
    HMODULE pinned;
    if ((GetEnvironmentVariableA("MAVERNIX_GLOBAL_MENU", off, sizeof off) && !strcmp(off, "0")) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(void *)begin_contact, &pinned))
        return TRUE;
    order_message = RegisterWindowMessageW(L"MavernixWineMenuOrder");
    CloseHandle(CreateThread(NULL, 0, keep_in_touch, NULL, 0, NULL));
    return TRUE;
}

static BOOL in_contact(void)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, begin_contact, NULL, NULL);
    return order_message != 0;
}

// the window's thread keeps answering sent messages while it waits, the bridge thread may be asking it
static BOOL offer(HWND window)
{
    HANDLE answered = in_contact() ? CreateEventW(NULL, TRUE, FALSE, NULL) : NULL;
    int at = -1;
    if (!answered)
        return FALSE;
    EnterCriticalSection(&lock);
    for (int slot = 0; slot < MOST_ASKED && at < 0; slot++)
        if (asked[slot].state == FREE)
        {
            at = slot;
            asked[at].window = window;
            asked[at].state = ASKED;
            asked[at].answered = answered;
        }
    LeaveCriticalSection(&lock);
    if (at >= 0 && wake != INVALID_SOCKET)
        sendto(wake, "", 1, 0, (struct sockaddr *)&wake_address, sizeof wake_address);
    for (DWORD end = GetTickCount() + READY_MS; at >= 0;)
    {
        LONG left = (LONG)(end - GetTickCount());
        DWORD woke = left > 0 ? MsgWaitForMultipleObjects(1, &answered, FALSE, left, QS_SENDMESSAGE) : WAIT_TIMEOUT;
        MSG message;
        if (woke != WAIT_OBJECT_0 + 1)
            break;
        PeekMessageW(&message, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    EnterCriticalSection(&lock);
    BOOL exported = at >= 0 && asked[at].state == EXPORTED;
    if (at >= 0)
    {
        asked[at].state = FREE;
        asked[at].window = NULL;
    }
    LeaveCriticalSection(&lock);
    CloseHandle(answered);
    return exported;
}

// wine exports the subclass lookup by number only, so a property marks the windows taken
static void watch_window(HWND window)
{
    if (GetPropW(window, WATCHED) || GetWindowLongW(window, GWL_STYLE) & WS_CHILD || !in_contact())
        return;
    DWORD_PTR data = GetMenu(window) ? OFFERED | (offer(window) ? BAR_HIDDEN : 0) : 0;
    if (!SetWindowSubclass(window, bar_proc, SUBCLASS_ID, data))
        return;
    SetPropW(window, WATCHED, (HANDLE)1);
    if (data & BAR_HIDDEN)
        SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

__declspec(dllexport) LRESULT CALLBACK menu_hook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION)
        watch_window(((const CWPSTRUCT *)lparam)->hwnd);
    return CallNextHookEx(NULL, code, wparam, lparam);
}

// runs on the thread making the window, after WM_CREATE and before it can be shown
static void CALLBACK created(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD thread,
                             DWORD time)
{
    (void)hook, (void)event, (void)thread, (void)time;
    if (object == OBJID_WINDOW && child == CHILDID_SELF)
        watch_window(window);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    InitializeCriticalSection(&lock);
    DisableThreadLibraryCalls(instance);
    if (!SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, instance, created, GetCurrentProcessId(), 0,
                         WINEVENT_INCONTEXT))
        fprintf(stderr, "[wine-menu-client] refused: no window event hook error=%lu\n", GetLastError());
    return TRUE;
}
