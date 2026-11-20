#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { NEW = 101, OPEN = 102, STATUS_BAR = 201, RUN = 301, RENAME = 1001, DROP_MENU = 1002, QUIT = 1003, DISABLE = 1004,
       ENABLE = 1005, BY_POSITION = 1006, KEY_MENU = 1007, CHILD_PROC = 1008, REUSE = 1009 };

static HWND main_window, twin_window, child_window;
static LONG_PTR child_proc;
static HMENU recent;
static int recent_opened;
static long reported_top = -1, reused_top = -1;
static HWND reused_window;
static DWORD reused_thread;

static void say(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    fflush(stdout);
}

static long client_top(HWND window)
{
    RECT frame;
    POINT origin = {0, 0};
    GetWindowRect(window, &frame);
    ClientToScreen(window, &origin);
    return origin.y - frame.top;
}

static void pump(DWORD milliseconds)
{
    MSG message;
    for (DWORD end = GetTickCount() + milliseconds; GetTickCount() < end; Sleep(10))
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&message);
}

static HMENU make_side_bar(void)
{
    HMENU bar = CreateMenu(), side = CreatePopupMenu();
    AppendMenuA(side, MF_STRING, 701, "&Item");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)side, "&Side");
    return bar;
}

// a window on a thread whose id another window's thread had before
static DWORD WINAPI menu_thread(void *title)
{
    if (reused_thread && GetCurrentThreadId() != reused_thread)
        return 0;
    HWND window = CreateWindowW(L"MenuTestApp", title, WS_OVERLAPPEDWINDOW, 80, 80, 400, 300, 0, make_side_bar(), 0, 0);
    if (reused_thread)
        reused_window = window;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    if (!reused_thread)
    {
        pump(3000);
        DestroyWindow(window);
        return 0;
    }
    for (;;)
        pump(1000);
}

// wine hands out a freed thread id again once enough are free, the oldest first
static DWORD WINAPI reuse_thread_id(void *unused)
{
    (void)unused;
    DWORD gone, id;
    HANDLE first = CreateThread(NULL, 0, menu_thread, (void *)L"Menu Test Gone", 0, &gone);
    WaitForSingleObject(first, 10000);
    CloseHandle(first);
    Sleep(2500);
    reused_thread = gone;
    for (int tries = 1; tries <= 20000; tries++)
    {
        HANDLE thread = CreateThread(NULL, 0, menu_thread, (void *)L"Menu Test Reused", 0, &id);
        if (id == gone)
        {
            CloseHandle(thread);
            say("[app] reuse found=1 tries=%d\n", tries);
            return 0;
        }
        WaitForSingleObject(thread, 10000);
        CloseHandle(thread);
    }
    say("[app] reuse found=0\n");
    return 0;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_COMMAND:
        if (lparam == 0 && window == main_window)
            say("[app] command id=%u\n", LOWORD(wparam));
        break;
    case WM_ENTERMENULOOP:
        say("[app] entermenuloop\n");
        EndMenu();
        break;
    case WM_MENUCOMMAND:
        say("[app] menucommand position=%u file=%d\n", (unsigned)wparam, (HMENU)lparam == GetSubMenu(GetMenu(window), 0));
        break;
    case WM_INITMENUPOPUP:
        if ((HMENU)wparam == recent)
        {
            char text[32];
            snprintf(text, sizeof text, "Recent %d", ++recent_opened);
            AppendMenuA(recent, MF_STRING, 500 + recent_opened, text);
            say("[app] initpopup recent=%d\n", recent_opened);
        }
        break;
    case WM_UNINITMENUPOPUP:
        if ((HMENU)wparam == recent)
            say("[app] uninitpopup recent\n");
        break;
    case WM_WINDOWPOSCHANGED:
        if (window == main_window && client_top(window) != reported_top)
        {
            reported_top = client_top(window);
            say("[app] layout client_top=%ld menu=%d\n", reported_top, GetMenu(window) != NULL);
        }
        if (window == reused_window && client_top(window) != reused_top)
        {
            reused_top = client_top(window);
            say("[app] reused client_top=%ld\n", reused_top);
        }
        break;
    case WM_APP:
        if (wparam == RENAME)
            ModifyMenuA(GetSubMenu(GetMenu(main_window), 0), NEW, MF_BYCOMMAND | MF_STRING, NEW, "&New Window\tCtrl+Shift+N");
        else if (wparam == DROP_MENU)
            SetMenu(main_window, NULL);
        else if (wparam == QUIT)
            PostQuitMessage(0);
        else if (wparam == DISABLE || wparam == ENABLE)
            EnableWindow(main_window, wparam == ENABLE);
        else if (wparam == KEY_MENU)
            SendMessageW(main_window, WM_SYSCOMMAND, SC_KEYMENU, 0);
        else if (wparam == REUSE)
            CloseHandle(CreateThread(NULL, 0, reuse_thread_id, NULL, 0, NULL));
        else if (wparam == CHILD_PROC && SendMessageW(child_window, WM_GETTEXTLENGTH, 0, 0) >= 0)
            say("[app] child proc kept=%d\n", GetWindowLongPtrW(child_window, GWLP_WNDPROC) == child_proc);
        else if (wparam == BY_POSITION)
        {
            MENUINFO style = {.cbSize = sizeof style, .fMask = MIM_STYLE, .dwStyle = MNS_NOTIFYBYPOS};
            SetMenuInfo(GetMenu(main_window), &style);
        }
        say("[app] done %u\n", (unsigned)wparam);
        return 0;
    case WM_DESTROY:
        if (window == main_window)
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static HMENU make_bar(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), view = CreatePopupMenu();
    recent = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, NEW, "&New\tCtrl+N");
    AppendMenuA(file, MF_STRING | MF_GRAYED, OPEN, "&Open...");
    AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_POPUP, (UINT_PTR)recent, "&Recent");
    AppendMenuA(view, MF_STRING | MF_CHECKED, STATUS_BAR, "&Status Bar");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View");
    AppendMenuA(bar, MF_STRING, RUN, "&Run");
    return bar;
}

static DWORD WINAPI read_orders(void *unused)
{
    (void)unused;
    char line[64];
    while (fgets(line, sizeof line, stdin))
    {
        unsigned order = !strncmp(line, "rename", 6) ? RENAME : !strncmp(line, "dropmenu", 8) ? DROP_MENU
                       : !strncmp(line, "quit", 4) ? QUIT : !strncmp(line, "disable", 7) ? DISABLE
                       : !strncmp(line, "enable", 6) ? ENABLE : !strncmp(line, "byposition", 10) ? BY_POSITION
                       : !strncmp(line, "keymenu", 7) ? KEY_MENU : !strncmp(line, "childproc", 9) ? CHILD_PROC
                       : !strncmp(line, "reuse", 5) ? REUSE : 0;
        if (order)
            PostMessageW(main_window, WM_APP, order, 0);
    }
    return 0;
}

int main(void)
{
    WNDCLASSW type = {0};
    type.lpfnWndProc = window_proc;
    type.lpszClassName = L"MenuTestApp";
    type.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&type);
    const WCHAR *title = sizeof(void *) == 8 ? L"Menu Test 64" : L"Menu Test 32";
    twin_window = CreateWindowW(L"MenuTestApp", L"Menu Test Twin", WS_OVERLAPPEDWINDOW, 600, 40, 400, 300, 0, 0, 0, 0);
    main_window = CreateWindowW(L"MenuTestApp", title, WS_OVERLAPPEDWINDOW, 40, 40, 500, 360, 0, make_bar(), 0, 0);
    child_window = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 10, 200, 24, main_window,
                                 (HMENU)7, 0, 0);
    child_proc = GetWindowLongPtrW(child_window, GWLP_WNDPROC);
    ShowWindow(twin_window, SW_SHOWNOACTIVATE);
    ShowWindow(main_window, SW_SHOW);
    say("[app] twin client_top=%ld\n", client_top(twin_window));
    say("[app] ready xid=0x%lx pid=%lu\n", (unsigned long)(ULONG_PTR)GetPropW(main_window, L"__wine_x11_whole_window"),
        GetCurrentProcessId());
    CloseHandle(CreateThread(NULL, 0, read_orders, NULL, 0, NULL));
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    say("[app] exit\n");
    return 0;
}
