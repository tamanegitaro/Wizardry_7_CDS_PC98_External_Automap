#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cctype>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <tlhelp32.h>
#include <commdlg.h>

#include "src/wizardry_am/external_compat.h"

static_assert(sizeof(void*) == 8, "Wizardry7Automap must be built as a 64-bit executable.");

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#endif

// GetProcAddress returns FARPROC, whose generic signature does not match the
// actual API function type. Copying the pointer representation avoids GCC's
// -Wcast-function-type warning while preserving the normal Windows pattern of
// resolving optional APIs at runtime.
template <typename Proc>
static Proc GetProcAddressTyped(HMODULE module, const char* name) noexcept
{
    static_assert(sizeof(Proc) == sizeof(FARPROC),
                  "Unexpected function-pointer size on this Windows target.");

    const FARPROC raw = GetProcAddress(module, name);
    Proc typed = nullptr;
    std::memcpy(&typed, &raw, sizeof(typed));
    return typed;
}

static void EnableDpiAwarenessForStablePixels()
{
    // Keep configured window dimensions in physical pixels, matching the
    // original automap's fixed-pixel rendering.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;

    using SetProcessDpiAwarenessContextProc = BOOL (WINAPI *)(DPI_AWARENESS_CONTEXT);
    const auto setContext = GetProcAddressTyped<SetProcessDpiAwarenessContextProc>(
        user32, "SetProcessDpiAwarenessContext");
    if (setContext) {
        if (setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
        if (setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) return;
    }

    using SetProcessDPIAwareProc = BOOL (WINAPI *)();
    const auto setAware = GetProcAddressTyped<SetProcessDPIAwareProc>(
        user32, "SetProcessDPIAware");
    if (setAware) setAware();
}

static constexpr int W7EA_DEFAULT_WINDOW_WIDTH = 512;
static constexpr int W7EA_DEFAULT_WINDOW_HEIGHT = 512;
static constexpr int W7EA_MIN_WINDOW_SIZE = 256;
static constexpr int W7EA_MAX_WINDOW_SIZE = 2048;

static bool s_automapEnabled = true;
static int s_configWindowWidth = W7EA_DEFAULT_WINDOW_WIDTH;
static int s_configWindowHeight = W7EA_DEFAULT_WINDOW_HEIGHT;
static int s_configWindowX = -1;
static int s_configWindowY = -1;
struct TargetProcessSpec {
    std::string name;
    std::wstring wideName;
};

static std::vector<TargetProcessSpec> s_targetProcesses;
static std::string s_attachedTargetProcessName;

int am_width = W7EA_DEFAULT_WINDOW_WIDTH;
int am_height = W7EA_DEFAULT_WINDOW_HEIGHT;
int map_scroll_x = 0;
int map_scroll_y = 0;
int map_draw_x = 0;
int map_draw_y = 0;
PhysPt am_loadaddress = 0;

extern unsigned long w7tiles32[];
extern int w7tiles32_count;
extern uintptr_t amw7_dataseg_addr;
extern bool amw7_rus;
extern bool amw7_show_tooltips;
extern bool amw7_hide_in_dark_zones;
extern bool amw7_sns_mode;
extern bool amw7_pc98_mode;
extern uintptr_t amw7_pc98_anchor;
extern uint16_t amw7_current_level;
extern uint16_t amw7_current_quadrant;
extern uint16_t amw7_current_qX;
extern uint16_t amw7_current_qY;
extern uint16_t amw7_current_dir;

void W7_NewGame();
void W7_SetSavePath(const char* basePath);
void W7_NativeLoadNotes();
void W7_PollState();
void W7_Update(int xSize, int ySize);
bool W7_NeedUpdate();
void W7_ForceFullRefresh(bool resetExploration);
void W7_OnMouseMotionInAutomapWindow(int newX, int newY, bool alt);
void W7_OnAutomapDrag(int dx, int dy);
void W7_OnMouseButtonInAutomapWindow(AM_MouseButtonEvent* btn, bool alt, bool ctrl);

static const wchar_t* W7EA_MAIN_CLASS = L"Wizardry7AutomapMainWindow";
static const wchar_t* W7EA_MAIN_TITLE = L"Wizardry7Automap Experimental";
static std::vector<uint32_t> s_frameBuffer;
static std::vector<uint32_t> s_w7Atlas;
static BITMAPINFO s_frameBitmapInfo = {};
static bool s_running = true;
static bool s_forceRedraw = true;
static std::string s_statusText = "Waiting for anex86.exe";
static HANDLE s_process = nullptr;
static bool s_gameAttached = false;
static constexpr unsigned W7EA_DS_ANCHOR_FAILURE_LIMIT = 3;
static unsigned s_dsAnchorFailureCount = 0;

static bool s_dragStart = false;
static int s_dragStartX = 0;
static int s_dragStartY = 0;
static int s_mouseOldX = 0;
static int s_mouseOldY = 0;
static bool s_trackingMouseLeave = false;
static uint8_t s_pendingLeftClicks = 1;
static constexpr size_t W7EA_SNAPSHOT_SIZE = 0x10000;
static std::array<uint8_t, W7EA_SNAPSHOT_SIZE> s_pc98Snapshot = {};
static uintptr_t s_pc98SnapshotBase = 0;
static bool s_pc98SnapshotValid = false;

static HWND s_nativeWindow = nullptr;
static HWND s_statusOverlay = nullptr;
static HWND s_noteTooltip = nullptr;
static HFONT s_uiFont = nullptr;
static std::wstring s_noteTooltipText;
static bool s_noteTooltipRequested = false;
static bool s_noteTooltipVisible = false;

static const wchar_t* W7EA_STATUS_CLASS = L"W7EAStatusOverlayWindow";
static const wchar_t* W7EA_TOOLTIP_CLASS = L"W7EANoteTooltipWindow";
static const wchar_t* W7EA_INPUT_CLASS = L"W7EAInputDialogWindow";

static UINT GetWindowDpiCompat(HWND hwnd)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using GetDpiForWindowProc = UINT (WINAPI *)(HWND);
        const auto getDpiForWindow = GetProcAddressTyped<GetDpiForWindowProc>(
            user32, "GetDpiForWindow");
        if (getDpiForWindow) {
            const UINT dpi = getDpiForWindow(hwnd);
            if (dpi != 0) return dpi;
        }
    }

    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96u;
}

static BOOL AdjustWindowRectForDpiCompat(RECT* rect, DWORD style, BOOL hasMenu,
                                         DWORD exStyle, UINT dpi)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using AdjustWindowRectExForDpiProc = BOOL (WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
        const auto adjustForDpi = GetProcAddressTyped<AdjustWindowRectExForDpiProc>(
            user32, "AdjustWindowRectExForDpi");
        if (adjustForDpi) return adjustForDpi(rect, style, hasMenu, exStyle, dpi);
    }
    return AdjustWindowRectEx(rect, style, hasMenu, exStyle);
}

static void ApplyConfiguredWindowPlacement(HWND hwnd, DWORD style, DWORD exStyle)
{
    RECT current = {};
    GetWindowRect(hwnd, &current);
    int x = (s_configWindowX < 0) ? static_cast<int>(current.left) : s_configWindowX;
    int y = (s_configWindowY < 0) ? static_cast<int>(current.top) : s_configWindowY;

    // Move first so the DPI query uses the monitor selected by position_x/y.
    SetWindowPos(hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT wanted = { 0, 0, am_width, am_height };
    const UINT dpi = GetWindowDpiCompat(hwnd);
    if (!AdjustWindowRectForDpiCompat(&wanted, style, FALSE, exStyle, dpi)) return;

    SetWindowPos(hwnd, nullptr, x, y,
                 wanted.right - wanted.left,
                 wanted.bottom - wanted.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty()) return std::wstring();
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (count <= 1) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), count);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}

static void RecreateUIFont(HWND referenceWindow)
{
    if (s_uiFont) {
        DeleteObject(s_uiFont);
        s_uiFont = nullptr;
    }

    const UINT dpi = GetWindowDpiCompat(referenceWindow);
    const int height = -MulDiv(10, static_cast<int>(dpi), 72);
    s_uiFont = CreateFontW(
        height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static LRESULT CALLBACK StatusOverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) return 0;

        RECT client = {};
        GetClientRect(hwnd, &client);
        FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        const int length = GetWindowTextLengthW(hwnd);
        std::vector<wchar_t> text(static_cast<size_t>(std::max(0, length)) + 1u, L'\0');
        if (length > 0) GetWindowTextW(hwnd, text.data(), length + 1);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(230, 230, 230));
        HGDIOBJ oldFont = SelectObject(
            hdc, s_uiFont ? reinterpret_cast<HGDIOBJ>(s_uiFont)
                          : GetStockObject(DEFAULT_GUI_FONT));
        RECT textRect = client;
        InflateRect(&textRect, -24, -24);
        DrawTextW(hdc, text.data(), -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK NoteTooltipWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (!hdc) return 0;

        RECT client = {};
        GetClientRect(hwnd, &client);
        FillRect(hdc, &client, GetSysColorBrush(COLOR_INFOBK));

        HPEN borderPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWFRAME));
        HGDIOBJ oldPen = borderPen ? SelectObject(hdc, borderPen) : nullptr;
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, client.left, client.top, client.right, client.bottom);
        SelectObject(hdc, oldBrush);
        if (borderPen) {
            SelectObject(hdc, oldPen);
            DeleteObject(borderPen);
        }

        const UINT dpi = GetWindowDpiCompat(hwnd);
        const int padX = MulDiv(8, static_cast<int>(dpi), 96);
        const int padY = MulDiv(6, static_cast<int>(dpi), 96);
        RECT textRect = client;
        InflateRect(&textRect, -padX, -padY);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_INFOTEXT));
        HGDIOBJ oldFont = SelectObject(
            hdc, s_uiFont ? reinterpret_cast<HGDIOBJ>(s_uiFont)
                          : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(hdc, s_noteTooltipText.c_str(), -1, &textRect,
                  DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool RegisterNativeUIClasses()
{
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));

    WNDCLASSEXW statusClass = {};
    statusClass.cbSize = sizeof(statusClass);
    statusClass.lpfnWndProc = StatusOverlayWndProc;
    statusClass.hInstance = instance;
    statusClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    statusClass.lpszClassName = W7EA_STATUS_CLASS;
    if (!RegisterClassExW(&statusClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW tooltipClass = {};
    tooltipClass.cbSize = sizeof(tooltipClass);
    tooltipClass.lpfnWndProc = NoteTooltipWndProc;
    tooltipClass.hInstance = instance;
    tooltipClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    tooltipClass.lpszClassName = W7EA_TOOLTIP_CLASS;
    if (!RegisterClassExW(&tooltipClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    return true;
}

static void ResizeStatusOverlay()
{
    if (!s_nativeWindow || !s_statusOverlay) return;
    RECT client = {};
    if (!GetClientRect(s_nativeWindow, &client)) return;
    SetWindowPos(s_statusOverlay, HWND_TOP,
                 0, 0,
                 std::max(1L, client.right - client.left),
                 std::max(1L, client.bottom - client.top),
                 SWP_NOACTIVATE);
}

static void SetStatusOverlayText(const std::string& text)
{
    if (!s_statusOverlay) return;
    const std::wstring wide = Utf8ToWide(text);
    SetWindowTextW(s_statusOverlay, wide.c_str());
    InvalidateRect(s_statusOverlay, nullptr, TRUE);
}

static void ShowStatusOverlay(bool show)
{
    if (!s_statusOverlay) return;
    ShowWindow(s_statusOverlay, show ? SW_SHOWNA : SW_HIDE);
    if (show) {
        ResizeStatusOverlay();
        InvalidateRect(s_statusOverlay, nullptr, TRUE);
        UpdateWindow(s_statusOverlay);
    }
}

static void HideNoteTooltip()
{
    s_noteTooltipRequested = false;
    if (s_noteTooltip && s_noteTooltipVisible) {
        ShowWindow(s_noteTooltip, SW_HIDE);
    }
    s_noteTooltipVisible = false;
}

static void UpdatePendingNoteTooltip(uint32_t /*now*/)
{
    if (!s_noteTooltipRequested || s_noteTooltipText.empty() ||
        !s_noteTooltip || !s_nativeWindow) {
        return;
    }
    if (s_noteTooltipVisible) return;

    POINT cursor = {};
    if (!GetCursorPos(&cursor)) return;

    const UINT dpi = GetWindowDpiCompat(s_nativeWindow);
    const int padX = MulDiv(8, static_cast<int>(dpi), 96);
    const int padY = MulDiv(6, static_cast<int>(dpi), 96);
    const int maxTextWidth = MulDiv(420, static_cast<int>(dpi), 96);

    HDC hdc = GetDC(s_noteTooltip);
    if (!hdc) return;
    HGDIOBJ oldFont = SelectObject(
        hdc, s_uiFont ? reinterpret_cast<HGDIOBJ>(s_uiFont)
                      : GetStockObject(DEFAULT_GUI_FONT));
    RECT measured = { 0, 0, maxTextWidth, 0 };
    DrawTextW(hdc, s_noteTooltipText.c_str(), -1, &measured,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX |
              DT_EDITCONTROL | DT_CALCRECT);
    SelectObject(hdc, oldFont);
    ReleaseDC(s_noteTooltip, hdc);

    const int measuredWidth = std::max(0, static_cast<int>(measured.right - measured.left));
    const int measuredHeight = std::max(0, static_cast<int>(measured.bottom - measured.top));
    const int width = std::max(MulDiv(48, static_cast<int>(dpi), 96),
                               std::min(maxTextWidth, measuredWidth) + padX * 2);
    const int height = std::max(MulDiv(24, static_cast<int>(dpi), 96),
                                measuredHeight + padY * 2);
    const int offsetX = MulDiv(16, static_cast<int>(dpi), 96);
    const int offsetY = MulDiv(22, static_cast<int>(dpi), 96);

    int x = cursor.x + offsetX;
    int y = cursor.y + offsetY;
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        const RECT& work = info.rcWork;
        if (x + width > work.right) x = cursor.x - width - offsetX;
        if (y + height > work.bottom) y = cursor.y - height - offsetY;
        x = std::max(static_cast<int>(work.left),
                     std::min(x, static_cast<int>(work.right) - width));
        y = std::max(static_cast<int>(work.top),
                     std::min(y, static_cast<int>(work.bottom) - height));
    }

    SetWindowPos(s_noteTooltip, HWND_TOPMOST, x, y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(s_noteTooltip, nullptr, TRUE);
    UpdateWindow(s_noteTooltip);
    s_noteTooltipVisible = true;
}

struct InputDialogState {
    HWND edit = nullptr;
    std::wstring hint;
    std::wstring initialText;
    std::wstring resultText;
    bool accepted = false;
    bool done = false;
};

static LRESULT CALLBACK InputDialogWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    InputDialogState* state = reinterpret_cast<InputDialogState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
        state = static_cast<InputDialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state) return -1;

        const UINT dpi = GetWindowDpiCompat(hwnd);
        auto scaled = [dpi](int value) {
            return MulDiv(value, static_cast<int>(dpi), 96);
        };
        HFONT font = s_uiFont ? s_uiFont
                              : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        HWND label = CreateWindowExW(
            0, L"STATIC", state->hint.c_str(),
            WS_CHILD | WS_VISIBLE,
            scaled(12), scaled(12), scaled(456), scaled(36),
            hwnd, nullptr, nullptr, nullptr);
        state->edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", state->initialText.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
            scaled(12), scaled(50), scaled(456), scaled(92),
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1001)),
            nullptr, nullptr);
        HWND ok = CreateWindowExW(
            0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            scaled(308), scaled(152), scaled(76), scaled(28),
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
            nullptr, nullptr);
        HWND cancel = CreateWindowExW(
            0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            scaled(392), scaled(152), scaled(76), scaled(28),
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
            nullptr, nullptr);
        if (!label || !state->edit || !ok || !cancel) return -1;

        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(state->edit, EM_SETLIMITTEXT, 4096, 0);
        SendMessageW(state->edit, EM_SETSEL, 0, -1);
        SetFocus(state->edit);
        return 0;
    }
    case WM_COMMAND:
        if (!state) return 0;
        if (LOWORD(wp) == IDOK) {
            const int length = GetWindowTextLengthW(state->edit);
            std::vector<wchar_t> buffer(static_cast<size_t>(std::max(0, length)) + 1u, L'\0');
            const int copied = GetWindowTextW(state->edit, buffer.data(), length + 1);
            state->resultText.assign(buffer.data(),
                                     static_cast<size_t>(std::max(0, copied)));
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state) state->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool RegisterInputDialogClass()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = InputDialogWndProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(
        static_cast<ULONG_PTR>(COLOR_BTNFACE + 1));
    wc.lpszClassName = W7EA_INPUT_CLASS;

    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static bool ShowInputDialog(HWND parent,
                            const wchar_t* title,
                            const wchar_t* hint,
                            const wchar_t* initialText,
                            std::wstring& resultText)
{
    if (!parent || !RegisterInputDialogClass()) return false;

    InputDialogState state;
    state.hint = hint ? hint : L"Enter a note. Leave it blank to delete the marker.";
    state.initialText = initialText ? initialText : L"";

    const UINT dpi = GetWindowDpiCompat(parent);
    RECT wanted = { 0, 0,
                    MulDiv(480, static_cast<int>(dpi), 96),
                    MulDiv(194, static_cast<int>(dpi), 96) };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    if (!AdjustWindowRectEx(&wanted, style, FALSE, 0)) return false;

    RECT parentRect = {};
    GetWindowRect(parent, &parentRect);
    const int width = wanted.right - wanted.left;
    const int height = wanted.bottom - wanted.top;
    const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
    const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        W7EA_INPUT_CLASS,
        title ? title : L"Map Note",
        style,
        x, y, width, height,
        parent, nullptr,
        reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)),
        &state);
    if (!dialog) return false;

    const BOOL parentWasEnabled = IsWindowEnabled(parent);
    EnableWindow(parent, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg = {};
    while (!state.done) {
        const int result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) s_running = false;
            break;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (parentWasEnabled) EnableWindow(parent, TRUE);
    SetActiveWindow(parent);
    SetFocus(parent);

    if (!state.accepted) return false;
    resultText = std::move(state.resultText);
    return true;
}

static bool InitializeNativeUI()
{
    if (!s_nativeWindow || !RegisterNativeUIClasses()) return false;

    RecreateUIFont(s_nativeWindow);

    RECT client = {};
    GetClientRect(s_nativeWindow, &client);
    s_statusOverlay = CreateWindowExW(
        0, W7EA_STATUS_CLASS, L"",
        WS_CHILD | WS_VISIBLE,
        0, 0,
        std::max(1L, client.right - client.left),
        std::max(1L, client.bottom - client.top),
        s_nativeWindow, nullptr,
        reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)), nullptr);
    if (!s_statusOverlay) return false;

    s_noteTooltip = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        W7EA_TOOLTIP_CLASS, L"", WS_POPUP,
        0, 0, 0, 0,
        s_nativeWindow, nullptr,
        reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)), nullptr);
    if (!s_noteTooltip) return false;

    SetStatusOverlayText(s_statusText);
    return true;
}

static void ShutdownNativeUI()
{
    HideNoteTooltip();
    if (s_noteTooltip) {
        DestroyWindow(s_noteTooltip);
        s_noteTooltip = nullptr;
    }
    if (s_statusOverlay) {
        DestroyWindow(s_statusOverlay);
        s_statusOverlay = nullptr;
    }
    if (s_uiFont) {
        DeleteObject(s_uiFont);
        s_uiFont = nullptr;
    }
    s_nativeWindow = nullptr;
}

static std::string TrimString(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
        ++first;

    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
        --last;

    return value.substr(first, last - first);
}

static std::string ToLowerString(std::string value)
{
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

static std::string UnquoteConfigValue(const std::string& value)
{
    std::string result = TrimString(value);
    if (result.size() >= 2) {
        const char first = result.front();
        const char last = result.back();
        if ((first == '\"' && last == '\"') || (first == '\'' && last == '\''))
            result = TrimString(result.substr(1, result.size() - 2));
    }
    return result;
}

static std::string FileNameOnly(std::string value)
{
    value = UnquoteConfigValue(value);
    const size_t slash = value.find_last_of("/\\");
    if (slash != std::string::npos)
        value = value.substr(slash + 1);
    return TrimString(value);
}

static std::wstring ConfigStringToWide(const std::string& value)
{
    if (value.empty())
        return std::wstring();

    auto convert = [&](UINT codePage, DWORD flags) -> std::wstring {
        const int needed = MultiByteToWideChar(codePage, flags, value.c_str(), -1, nullptr, 0);
        if (needed <= 1)
            return std::wstring();
        std::wstring result(static_cast<size_t>(needed), L'\0');
        if (MultiByteToWideChar(codePage, flags, value.c_str(), -1, result.data(), needed) <= 0)
            return std::wstring();
        result.resize(static_cast<size_t>(needed - 1));
        return result;
    };

    std::wstring result = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
    if (result.empty())
        result = convert(CP_ACP, 0);
    return result;
}

static std::string WaitingStatusText()
{
    if (s_targetProcesses.size() == 1)
        return std::string("Waiting for ") + s_targetProcesses.front().name;
    return "Waiting for target process";
}

static bool ParseBoolValue(const std::string& value, bool fallback)
{
    const std::string v = ToLowerString(TrimString(value));
    if (v == "1" || v == "true" || v == "yes" || v == "on")
        return true;
    if (v == "0" || v == "false" || v == "no" || v == "off")
        return false;
    return fallback;
}

static int ParseIntValue(const std::string& value, int fallback, int minValue, int maxValue)
{
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str())
        return fallback;

    int result = static_cast<int>(parsed);
    if (result < minValue) result = minValue;
    if (result > maxValue) result = maxValue;
    return result;
}

static void WriteDefaultAutomapConfig(const char* path)
{
    CreateDirectoryA("Config", nullptr);

    DWORD attrs = GetFileAttributesA(path);
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return;

    FILE* fp = std::fopen(path, "w");
    if (!fp)
        return;

    std::fprintf(fp,
                 "[automap]\n"
                 "target1=\"anex86.exe\"\n"
                 "target2=\"np21.exe\"\n"
                 "target3=\"np21nt.exe\"\n"
                 "target4=\"np2sx.exe\"\n"
                 "target5=\"np2sxnt.exe\"\n"
                 "target6=\"np2nt.exe\"\n"
                 "target7=\"np2.exe\"\n"
                 "target8=\"np2w.exe\"\n"
                 "target9=\"np2x64w.exe\"\n"
                 "target10=\"np21w.exe\"\n"
                 "target11=\"np21x64w.exe\"\n"
                 "target12=\"Next.EXE\"\n"
                 "target13=\"WIZ7.EXE\"\n"
                 "target14=\n"
                 "target15=\n"
                 "target16=\n"
                 "enable=true\n"
                 "show_tooltips=true\n"
                 "hide_in_dark_zones=true\n"
                 "width=512\n"
                 "height=512\n"
                 "position_x=-1\n"
                 "position_y=-1\n"
                 "wiz7_sns_mode=false\n");
    std::fclose(fp);
}

static void LoadAutomapConfig()
{
    const char* configPath = "Config\\Wizardry7Automap.conf";

    // Keep default values if the file is absent or partially specified.
    WriteDefaultAutomapConfig(configPath);

    FILE* fp = std::fopen(configPath, "r");
    if (!fp) {
        return;
    }

    bool inAutomapSection = false;
    // target= is slot 0 for backward compatibility. target1..target16 follow it.
    std::array<std::string, 17> targetSlots{};
    char line[512];
    while (std::fgets(line, sizeof(line), fp)) {
        std::string text = TrimString(line);
        if (text.empty() || text[0] == '#' || text[0] == ';')
            continue;

        if (text.front() == '[' && text.back() == ']') {
            const std::string section = ToLowerString(TrimString(text.substr(1, text.size() - 2)));
            inAutomapSection = (section == "automap");
            continue;
        }

        if (!inAutomapSection)
            continue;

        const size_t eq = text.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = ToLowerString(TrimString(text.substr(0, eq)));
        std::string value = TrimString(text.substr(eq + 1));

        int targetIndex = -1;
        if (key == "target") {
            targetIndex = 0;
        } else if (key.rfind("target", 0) == 0 && key.size() > 6) {
            const std::string suffix = key.substr(6);
            char* end = nullptr;
            const long parsedIndex = std::strtol(suffix.c_str(), &end, 10);
            if (end != suffix.c_str() && *end == '\0' && parsedIndex >= 1 && parsedIndex <= 16)
                targetIndex = static_cast<int>(parsedIndex);
        }

        if (targetIndex >= 0) {
            targetSlots[static_cast<size_t>(targetIndex)] = FileNameOnly(value);
        } else if (key == "enable") {
            s_automapEnabled = ParseBoolValue(value, s_automapEnabled);
        } else if (key == "show_tooltips") {
            amw7_show_tooltips = ParseBoolValue(value, amw7_show_tooltips);
        } else if (key == "hide_in_dark_zones") {
            amw7_hide_in_dark_zones = ParseBoolValue(value, amw7_hide_in_dark_zones);
        } else if (key == "width") {
            s_configWindowWidth = ParseIntValue(value, s_configWindowWidth, W7EA_MIN_WINDOW_SIZE, W7EA_MAX_WINDOW_SIZE);
        } else if (key == "height") {
            s_configWindowHeight = ParseIntValue(value, s_configWindowHeight, W7EA_MIN_WINDOW_SIZE, W7EA_MAX_WINDOW_SIZE);
        } else if (key == "position_x") {
            s_configWindowX = ParseIntValue(value, s_configWindowX, -1, 32767);
        } else if (key == "position_y") {
            s_configWindowY = ParseIntValue(value, s_configWindowY, -1, 32767);
        } else if (key == "wiz7_sns_mode") {
            amw7_sns_mode = ParseBoolValue(value, amw7_sns_mode);
        }
    }
    std::fclose(fp);

    s_targetProcesses.clear();
    for (const std::string& targetName : targetSlots) {
        if (targetName.empty())
            continue;

        const std::wstring wideName = ConfigStringToWide(targetName);
        if (wideName.empty())
            continue;

        bool duplicate = false;
        for (const TargetProcessSpec& existing : s_targetProcesses) {
            if (_wcsicmp(existing.wideName.c_str(), wideName.c_str()) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            s_targetProcesses.push_back(TargetProcessSpec{targetName, wideName});
    }

    // Keep a useful fallback for an empty or malformed configuration.
    if (s_targetProcesses.empty())
        s_targetProcesses.push_back(TargetProcessSpec{"anex86.exe", L"anex86.exe"});

    am_width = s_configWindowWidth;
    am_height = s_configWindowHeight;

    s_statusText = WaitingStatusText();
}

static bool EnableProcessInspectionPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_PRIVILEGES tp;
    std::memset(&tp, 0, sizeof(tp));
    tp.PrivilegeCount = 1;
    if (!LookupPrivilegeValue(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }

    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD err = GetLastError();
    CloseHandle(token);

    if (err == ERROR_SUCCESS) {
        return true;
    }
    return false;
}

static HANDLE OpenTargetProcess(DWORD pid)
{
    const DWORD rights[] = {
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
        PROCESS_VM_READ | SYNCHRONIZE,
        PROCESS_VM_READ,
    };
    for (DWORD r : rights) {
        HANDLE h = OpenProcess(r, FALSE, pid);
        if (h) {
            return h;
        }
    }
    return nullptr;
}


static uint32_t PaletteIndexToPackedRgb(int idx)
{
    // The automap's historical packed-color format stores red in the low byte,
    // green in the next byte and blue in the third byte (0x00BBGGRR).
    static const uint32_t palette[] = {
        0x00000000u, // black
        0x000000FFu, // red
        0x0000FF00u, // green
        0x0000FFFFu, // yellow
        0x00FF0000u, // blue
        0x00FF00FFu, // magenta
        0x00FFFF00u, // cyan
        0x00404040u,
        0x00BFBFBFu,
        0x001F6699u,
        0x001F9FFAu,
        0x00B30AFAu,
        0x00B36699u,
        0x00FFFFFFu,
    };
    int i = idx + 1;
    if (i < 0) i = 0;
    const int count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    if (i >= count) i = count - 1;
    return palette[i];
}

static uint32_t PackedRgbToDib(uint32_t packed)
{
    const uint32_t r = packed & 0xFFu;
    const uint32_t g = (packed >> 8) & 0xFFu;
    const uint32_t b = (packed >> 16) & 0xFFu;
    // A 32-bit BI_RGB DIB uses B,G,R,unused bytes in memory. On little-endian
    // Windows this integer representation is 0x00RRGGBB.
    return (r << 16) | (g << 8) | b;
}

static void PutPixelClipped(int x, int y, uint32_t dibColor)
{
    if (x < 0 || y < 0 || x >= am_width || y >= am_height) return;
    s_frameBuffer[static_cast<size_t>(y) * static_cast<size_t>(am_width) +
                  static_cast<size_t>(x)] = dibColor;
}

static void ClearFrameBuffer(uint32_t dibColor = 0)
{
    std::fill(s_frameBuffer.begin(), s_frameBuffer.end(), dibColor);
}

static bool ResizeFrameBuffer(int width, int height)
{
    if (width <= 0 || height <= 0) return false;

    const size_t pixelCount = static_cast<size_t>(width) *
                              static_cast<size_t>(height);
    try {
        s_frameBuffer.assign(pixelCount, 0u);
    } catch (...) {
        return false;
    }

    am_width = width;
    am_height = height;

    std::memset(&s_frameBitmapInfo, 0, sizeof(s_frameBitmapInfo));
    s_frameBitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s_frameBitmapInfo.bmiHeader.biWidth = am_width;
    s_frameBitmapInfo.bmiHeader.biHeight = -am_height; // top-down
    s_frameBitmapInfo.bmiHeader.biPlanes = 1;
    s_frameBitmapInfo.bmiHeader.biBitCount = 32;
    s_frameBitmapInfo.bmiHeader.biCompression = BI_RGB;
    return true;
}

static bool InitializeFrameBuffer()
{
    try {
        s_w7Atlas.assign(256u * 32u, 0u);
    } catch (...) {
        return false;
    }

    const int atlasCount = std::min(w7tiles32_count, 256 * 32);
    for (int i = 0; i < atlasCount; ++i) {
        const uint32_t p = static_cast<uint32_t>(w7tiles32[i]);
        if (p == 0) {
            s_w7Atlas[static_cast<size_t>(i)] = 0;
            continue;
        }
        const uint32_t r = p & 0xFFu;
        const uint32_t g = (p >> 8) & 0xFFu;
        const uint32_t b = (p >> 16) & 0xFFu;
        s_w7Atlas[static_cast<size_t>(i)] =
            0xFF000000u | (r << 16) | (g << 8) | b;
    }

    return ResizeFrameBuffer(am_width, am_height);
}

void AM_DrawRect(int x, int y, int w, int h, int color)
{
    if (w <= 0 || h <= 0 || s_frameBuffer.empty()) return;
    uint32_t packed = static_cast<uint32_t>(color);
    // Preserve support for the old small palette indices while also accepting
    // W7's direct 0x00BBGGRR note/object colors.
    if (color >= -1 && color <= 12) packed = PaletteIndexToPackedRgb(color);
    const uint32_t dibColor = PackedRgbToDib(packed);

    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(am_width, x + w);
    const int bottom = std::min(am_height, y + h);
    if (left >= right || top >= bottom) return;

    for (int py = top; py < bottom; ++py) {
        uint32_t* row = s_frameBuffer.data() +
                        static_cast<size_t>(py) * static_cast<size_t>(am_width);
        std::fill(row + left, row + right, dibColor);
    }
}

void AM_DrawOutlineRect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    const uint32_t dibColor = PackedRgbToDib(color);
    for (int px = x; px < x + w; ++px) {
        PutPixelClipped(px, y, dibColor);
        PutPixelClipped(px, y + h - 1, dibColor);
    }
    for (int py = y; py < y + h; ++py) {
        PutPixelClipped(x, py, dibColor);
        PutPixelClipped(x + w - 1, py, dibColor);
    }
}

void AM_DrawW6Sprite(int, int, int, int, float, float, float, float,
                     bool, AM_RendererFlip)
{
    // Wizardry 6 rendering is not used by this W7-only executable.
}

static void AM_DrawSprite(int x, int y, int w, int h,
                          float u0, float v0, float u1, float v1,
                          bool dark, AM_RendererFlip flip)
{
    if (w <= 0 || h <= 0 || s_w7Atlas.empty() || s_frameBuffer.empty()) return;
    if (x >= am_width || y >= am_height || x + w <= 0 || y + h <= 0) return;

    const int atlasW = 256;
    const int atlasH = 32;
    int sx = static_cast<int>(u0 * atlasW);
    int sy = static_cast<int>(v0 * atlasH);
    int sw = static_cast<int>((u1 - u0) * atlasW);
    int sh = static_cast<int>((v1 - v0) * atlasH);
    if (sw <= 0 || sh <= 0) return;

    sx = std::max(0, std::min(sx, atlasW - 1));
    sy = std::max(0, std::min(sy, atlasH - 1));
    sw = std::min(sw, atlasW - sx);
    sh = std::min(sh, atlasH - sy);
    if (sw <= 0 || sh <= 0) return;

    const bool flipH = (static_cast<uint32_t>(flip) & AM_FLIP_HORIZONTAL) != 0;
    const bool flipV = (static_cast<uint32_t>(flip) & AM_FLIP_VERTICAL) != 0;

    for (int dy = 0; dy < h; ++dy) {
        const int outY = y + dy;
        if (outY < 0 || outY >= am_height) continue;
        int srcY = (dy * sh) / h;
        if (flipV) srcY = sh - 1 - srcY;

        for (int dx = 0; dx < w; ++dx) {
            const int outX = x + dx;
            if (outX < 0 || outX >= am_width) continue;
            int srcX = (dx * sw) / w;
            if (flipH) srcX = sw - 1 - srcX;

            const uint32_t src = s_w7Atlas[
                static_cast<size_t>(sy + srcY) * static_cast<size_t>(atlasW) +
                static_cast<size_t>(sx + srcX)];
            if ((src >> 24) == 0) continue;

            uint32_t r = (src >> 16) & 0xFFu;
            uint32_t g = (src >> 8) & 0xFFu;
            uint32_t b = src & 0xFFu;
            if (dark) {
                r >>= 1;
                g >>= 1;
                b >>= 1;
            }
            PutPixelClipped(outX, outY, (r << 16) | (g << 8) | b);
        }
    }
}

void AM_DrawW7Sprite(int x, int y, int w, int h,
                     float u0, float v0, float u1, float v1,
                     bool dark, AM_RendererFlip flip)
{
    AM_DrawSprite(x, y, w, h, u0, v0, u1, v1, dark, flip);
}

void SetAutomapWindowTitle(const char* title)
{
    if (!s_nativeWindow) return;
    const std::wstring wide = Utf8ToWide(title ? title : "Wizardry7Automap");
    SetWindowTextW(s_nativeWindow, wide.c_str());
}

bool AM_SetClipboardText(const char* text)
{
    if (!text || !OpenClipboard(s_nativeWindow)) return false;
    EmptyClipboard();
    const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (count <= 0) {
        CloseClipboard();
        return false;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE,
                                 static_cast<SIZE_T>(count) * sizeof(wchar_t));
    if (!memory) {
        CloseClipboard();
        return false;
    }
    wchar_t* destination = static_cast<wchar_t*>(GlobalLock(memory));
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, destination, count);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

void TooltipForAutomapWindow_Show(bool show)
{
    if (!show || s_noteTooltipText.empty()) {
        HideNoteTooltip();
        return;
    }

    s_noteTooltipRequested = true;
    UpdatePendingNoteTooltip(static_cast<uint32_t>(GetTickCount64()));
}

void TooltipForAutomapWindow_SetText(wchar_t* text)
{
    const std::wstring next = text ? text : L"";
    if (next == s_noteTooltipText) return;

    s_noteTooltipText = next;
    if (s_noteTooltipVisible && s_noteTooltip) {
        ShowWindow(s_noteTooltip, SW_HIDE);
        s_noteTooltipVisible = false;
    }
}

void TooltipForMainWindow_Show(bool) {}
void TooltipForMainWindow_SetText(wchar_t*) {}

bool InputBox(const wchar_t* title, const wchar_t* hint, wchar_t* buf, int bufSize)
{
    if (!buf || bufSize <= 0 || !s_nativeWindow) return false;

    std::wstring result;
    if (!ShowInputDialog(s_nativeWindow, title, hint, buf, result)) {
        return false;
    }

    const size_t maxChars = static_cast<size_t>(bufSize - 1);
    const size_t copyChars = std::min(maxChars, result.size());
    if (copyChars > 0) {
        std::wmemcpy(buf, result.data(), copyChars);
    }
    buf[copyChars] = L'\0';
    return true;
}

bool ChooseColorDialog(uint32_t& color, uint32_t* palette)
{
    static COLORREF customColors[16] = {};
    if (palette) {
        for (int i = 0; i < 16; ++i) {
            customColors[i] = static_cast<COLORREF>(palette[i] & 0x00FFFFFFu);
        }
    }

    CHOOSECOLORW choose = {};
    choose.lStructSize = sizeof(choose);
    choose.hwndOwner = s_nativeWindow;
    choose.rgbResult = static_cast<COLORREF>(color & 0x00FFFFFFu);
    choose.lpCustColors = customColors;
    choose.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&choose)) return false;

    color = static_cast<uint32_t>(choose.rgbResult);
    return true;
}

uint16_t GetBitArrayElement(uint8_t* pArray, uint16_t index, uint16_t bitsPerElement)
{
    uint16_t elemOfs = (uint16_t)(index * bitsPerElement);
    uint16_t elemShift = (uint16_t)(elemOfs % 8);
    uint16_t elemMask = 0xFFFF;
    elemMask >>= (uint16_t)(16 - bitsPerElement);
    elemOfs = (uint16_t)(elemOfs / 8);
    uint16_t ret = *reinterpret_cast<uint16_t*>(pArray + elemOfs);
    ret >>= elemShift;
    ret &= elemMask;
    return ret;
}

void AutoMapUpdate()
{
    s_forceRedraw = true;
}

static void InvalidateDsExeAnchor()
{
    s_gameAttached = false;
    s_pc98SnapshotValid = false;
    s_pc98SnapshotBase = 0;
    s_dsAnchorFailureCount = 0;
    HideNoteTooltip();
    am_loadaddress = 0;
    amw7_dataseg_addr = 0;
    amw7_pc98_anchor = 0;
    amw7_pc98_mode = false;
    W7_NewGame();

    s_statusText = "Waiting for DS.EXE...";
    SetAutomapWindowTitle(s_statusText.c_str());
    SetStatusOverlayText(s_statusText);
    ShowStatusOverlay(true);
    s_forceRedraw = true;
}

static void RegisterDsExeAnchorFailure()
{
    ++s_dsAnchorFailureCount;
    if (s_dsAnchorFailureCount >= W7EA_DS_ANCHOR_FAILURE_LIMIT) {
        InvalidateDsExeAnchor();
    }
}

static bool SnapshotContains(uintptr_t addr, size_t size)
{
    if (!s_pc98SnapshotValid || s_pc98SnapshotBase == 0) return false;
    if (addr < s_pc98SnapshotBase) return false;
    const uintptr_t offset = addr - s_pc98SnapshotBase;
    return offset <= W7EA_SNAPSHOT_SIZE &&
           size <= W7EA_SNAPSHOT_SIZE - static_cast<size_t>(offset);
}

static bool RefreshPC98Snapshot()
{
    s_pc98SnapshotValid = false;
    s_pc98SnapshotBase = 0;

    if (!s_process || !amw7_pc98_mode || amw7_pc98_anchor == 0) {
        return false;
    }

    SIZE_T got = 0;
    bool ok = ReadProcessMemory(
                  s_process,
                  reinterpret_cast<LPCVOID>(amw7_pc98_anchor),
                  s_pc98Snapshot.data(),
                  s_pc98Snapshot.size(),
                  &got) &&
              got == s_pc98Snapshot.size();

    // Normally the complete 64 KiB guest data segment is one readable block.
    // If a host memory-protection boundary splits it, fall back to four 16 KiB
    // reads while retaining the same coherent local snapshot interface.
    if (!ok) {
        ok = true;
        constexpr size_t kFallbackChunk = 0x4000;
        for (size_t offset = 0; offset < s_pc98Snapshot.size(); offset += kFallbackChunk) {
            SIZE_T chunkGot = 0;
            if (!ReadProcessMemory(
                    s_process,
                    reinterpret_cast<LPCVOID>(amw7_pc98_anchor + offset),
                    s_pc98Snapshot.data() + offset,
                    kFallbackChunk,
                    &chunkGot) ||
                chunkGot != kFallbackChunk) {
                ok = false;
                break;
            }
        }
    }

    if (!ok) {
        RegisterDsExeAnchorFailure();
        return false;
    }

    static constexpr char kDsExeAnchor[] = "a:ds.exe";
    if (std::memcmp(s_pc98Snapshot.data(), kDsExeAnchor, sizeof(kDsExeAnchor) - 1) != 0) {
        RegisterDsExeAnchorFailure();
        return false;
    }

    if (s_dsAnchorFailureCount != 0) {
        s_dsAnchorFailureCount = 0;
    }

    s_pc98SnapshotBase = amw7_pc98_anchor;
    s_pc98SnapshotValid = true;
    return true;
}

bool mem_readb_checked(uintptr_t addr, uint8_t* out)
{
    if (out) *out = 0;
    if (!out) return false;

    if (amw7_pc98_mode) {
        if (!SnapshotContains(addr, sizeof(*out))) return false;
        *out = s_pc98Snapshot[static_cast<size_t>(addr - s_pc98SnapshotBase)];
        return true;
    }

    if (!s_process) return false;
    SIZE_T got = 0;
    if (!ReadProcessMemory(s_process, reinterpret_cast<LPCVOID>(addr),
                           out, sizeof(*out), &got) ||
        got != sizeof(*out)) {
        *out = 0;
        return false;
    }
    return true;
}

bool mem_readw_checked(uintptr_t addr, uint16_t* out)
{
    if (out) *out = 0;
    if (!out) return false;

    if (amw7_pc98_mode) {
        if (!SnapshotContains(addr, sizeof(*out))) return false;
        std::memcpy(out,
                    s_pc98Snapshot.data() +
                        static_cast<size_t>(addr - s_pc98SnapshotBase),
                    sizeof(*out));
        return true;
    }

    if (!s_process) return false;
    SIZE_T got = 0;
    if (!ReadProcessMemory(s_process, reinterpret_cast<LPCVOID>(addr),
                           out, sizeof(*out), &got) ||
        got != sizeof(*out)) {
        *out = 0;
        return false;
    }
    return true;
}

void MEM_BlockRead(uintptr_t addr, void* out, size_t len)
{
    if (!out || len == 0) return;
    std::memset(out, 0, len);

    if (amw7_pc98_mode) {
        if (!SnapshotContains(addr, len)) return;
        std::memcpy(out,
                    s_pc98Snapshot.data() +
                        static_cast<size_t>(addr - s_pc98SnapshotBase),
                    len);
        return;
    }

    if (!s_process) return;
    SIZE_T got = 0;
    ReadProcessMemory(s_process, reinterpret_cast<LPCVOID>(addr), out, len, &got);
}

static bool IsReadableProtection(DWORD protect)
{
    if (protect & PAGE_GUARD) return false;
    if (protect & PAGE_NOACCESS) return false;

    switch (protect & 0xFF) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return true;
    default:
        return false;
    }
}

static bool ProcessAlive(HANDLE h)
{
    if (!h) return false;

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(h, &exitCode)) {
        // Fallback: this requires SYNCHRONIZE access.  If the handle lacks it,
        // WAIT_FAILED must not be treated as "the process exited".
        DWORD rc = WaitForSingleObject(h, 0);
        if (rc == WAIT_TIMEOUT) return true;
        if (rc == WAIT_OBJECT_0) return false;
        return true;
    }

    return exitCode == STILL_ACTIVE;
}

static bool FindTargetProcess(DWORD& pid, std::string& matchedName)
{
    pid = 0;
    matchedName.clear();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    std::vector<std::pair<DWORD, std::wstring>> processes;
    PROCESSENTRY32W pe;
    std::memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            processes.emplace_back(pe.th32ProcessID, pe.szExeFile);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // Preserve compatibility priority: target= first, then target1..target16.
    for (const TargetProcessSpec& target : s_targetProcesses) {
        for (const auto& process : processes) {
            if (_wcsicmp(process.second.c_str(), target.wideName.c_str()) == 0) {
                pid = process.first;
                matchedName = target.name;
                return true;
            }
        }
    }

    return false;
}

static bool ReadExact(HANDLE process, uintptr_t addr, void* out, size_t size)
{
    SIZE_T got = 0;
    return ReadProcessMemory(process, (LPCVOID)addr, out, size, &got) && got == size;
}

static bool MatchesAt(HANDLE process, uintptr_t addr, const char* sig, size_t len)
{
    std::vector<char> buf(len);
    return ReadExact(process, addr, buf.data(), len) && std::memcmp(buf.data(), sig, len) == 0;
}

static bool BufferMatchesAt(const char* data, size_t dataSize, size_t offset,
                            const char* sig, size_t sigLen)
{
    return data && sig && offset <= dataSize && sigLen <= dataSize - offset &&
           std::memcmp(data + offset, sig, sigLen) == 0;
}

static bool VerifyDsExeResidentData(HANDLE process, uintptr_t candidate,
                                    const char* vmaze, const char* stackOverflow)
{
    // The original verification checks every possible start position in the
    // 0x2000-byte window with ReadProcessMemory.  Read the same window once
    // and perform the identical signature checks locally.  If the block lies
    // across an unreadable boundary, fall back to the original per-address
    // checks so candidate acceptance does not change.
    static constexpr size_t kWindowSize = 0x2000;
    static constexpr size_t kVmazeLen = 5;
    static constexpr size_t kStackOverflowLen = 14;
    static constexpr size_t kReadSize = kWindowSize + kStackOverflowLen - 1;

    std::array<char, kReadSize> verifyBuffer = {};
    SIZE_T got = 0;
    const BOOL readOk = ReadProcessMemory(
        process, reinterpret_cast<LPCVOID>(candidate),
        verifyBuffer.data(), verifyBuffer.size(), &got);

    if (readOk && got == verifyBuffer.size()) {
        bool hasVmazeNear = false;
        bool hasStackNear = false;
        for (size_t i = 0; i < kWindowSize; ++i) {
            if (!hasVmazeNear &&
                BufferMatchesAt(verifyBuffer.data(), got, i, vmaze, kVmazeLen)) {
                hasVmazeNear = true;
            }
            if (!hasStackNear &&
                BufferMatchesAt(verifyBuffer.data(), got, i, stackOverflow, kStackOverflowLen)) {
                hasStackNear = true;
            }
            if (hasVmazeNear && hasStackNear)
                return true;
        }
        return false;
    }

    bool hasVmazeNear = false;
    bool hasStackNear = false;
    for (uintptr_t p = candidate; p < candidate + kWindowSize; ++p) {
        if (!hasVmazeNear && MatchesAt(process, p, vmaze, kVmazeLen))
            hasVmazeNear = true;
        if (!hasStackNear && MatchesAt(process, p, stackOverflow, kStackOverflowLen))
            hasStackNear = true;
        if (hasVmazeNear && hasStackNear)
            return true;
    }
    return false;
}

static bool FindWizardry7Layout(HANDLE process, uintptr_t& anchor, uintptr_t& dataSeg)
{

    anchor = 0;
    dataSeg = 0;

    const char* dsAnchor = "a:ds.exe";
    const char* vmaze = "VMAZE";
    const char* stackOverflow = "STACK OVERFLOW";
    const size_t dsLen = 8;
    const size_t chunkSize = 1u << 20; // 1 MB
    const size_t overlap = 256;

    SYSTEM_INFO systemInfo = {};
    GetNativeSystemInfo(&systemInfo);
    const uintptr_t maximumAddress = reinterpret_cast<uintptr_t>(systemInfo.lpMaximumApplicationAddress);
    // Scan the complete address range supported by this 64-bit build.
    // There is deliberately no 2 GiB cutoff. VirtualQueryEx naturally stops
    // when the target process has no further queryable address space.
    uintptr_t addr = reinterpret_cast<uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    MEMORY_BASIC_INFORMATION mbi;
    while (addr <= maximumAddress &&
           VirtualQueryEx(process, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        uintptr_t base = (uintptr_t)mbi.BaseAddress;
        size_t regionSize = (size_t)mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && IsReadableProtection(mbi.Protect) && regionSize >= dsLen) {

            size_t offset = 0;
            while (offset < regionSize) {
                size_t toRead = std::min(chunkSize, regionSize - offset);
                std::vector<char> buf(toRead + overlap);
                SIZE_T got = 0;
                if (ReadProcessMemory(process, (LPCVOID)(base + offset), buf.data(), toRead, &got) && got >= dsLen) {
                    size_t limit = (size_t)got >= dsLen ? (size_t)got - dsLen + 1 : 0;
                    for (size_t i = 0; i < limit; ++i) {
                        if (std::memcmp(buf.data() + i, dsAnchor, dsLen) != 0)
                            continue;

                        uintptr_t candidate = base + offset + i;

                        // Verify this is the DS.EXE resident data table, not a random path copy.
                        if (!VerifyDsExeResidentData(process, candidate, vmaze, stackOverflow)) {
                            continue;
                        }

                        anchor = candidate;
                        dataSeg = candidate; // PC-98 mode: the renderer uses amw7_pc98_anchor-relative offsets.
                        return true;
                    }
                }
                if (toRead <= overlap) break;
                offset += (toRead - overlap);
            }
        }

        if (regionSize == 0 || base > maximumAddress)
            break;
        if (regionSize > maximumAddress - base)
            break;

        const uintptr_t next = base + regionSize;
        if (next <= addr)
            break;
        addr = next;
    }
    return false;
}

static void DetachFromProcess()
{
    if (s_process) {
        CloseHandle(s_process);
        s_process = nullptr;
    }
    s_gameAttached = false;
    s_attachedTargetProcessName.clear();
    s_dsAnchorFailureCount = 0;
    s_pc98SnapshotValid = false;
    s_pc98SnapshotBase = 0;
    HideNoteTooltip();
    am_loadaddress = 0;
    amw7_dataseg_addr = 0;
    amw7_pc98_anchor = 0;
    amw7_pc98_mode = false;
    W7_NewGame();
    s_statusText = WaitingStatusText();
    SetAutomapWindowTitle(s_statusText.c_str());
    SetStatusOverlayText(s_statusText);
    ShowStatusOverlay(true);
    s_forceRedraw = true;
}

static void EnsureAttachedOrWaiting()
{
    if (s_process && !ProcessAlive(s_process)) {
        const char* exitedName = s_attachedTargetProcessName.empty() ? "Target process" : s_attachedTargetProcessName.c_str();
        std::printf("%s exited. Waiting again...\n", exitedName);
        DetachFromProcess();
    }

    if (!s_process) {
        DWORD pid = 0;
        std::string matchedName;
        if (FindTargetProcess(pid, matchedName)) {
            HANDLE h = OpenTargetProcess(pid);
            if (h) {
                s_process = h;
                s_attachedTargetProcessName = matchedName;
                s_statusText = matchedName + " found - scanning for Wizardry 7";
                SetAutomapWindowTitle(s_statusText.c_str());
                // Paint the centered status before the synchronous memory scan.
                SetStatusOverlayText(s_statusText);
                ShowStatusOverlay(true);
                s_forceRedraw = true;
                std::printf("Attached to %s (PID=%lu)\n", matchedName.c_str(), (unsigned long)pid);
            }
        }
        if (!s_process)
            return;
        // Fall through immediately to the Wizardry 7 memory scan after a successful attach.
    }

    if (!s_gameAttached) {
        uintptr_t loadBase = 0;
        uintptr_t dataSeg = 0;
        if (FindWizardry7Layout(s_process, loadBase, dataSeg)) {
            am_loadaddress = loadBase;
            amw7_dataseg_addr = dataSeg;
            amw7_pc98_anchor = loadBase;
            amw7_pc98_mode = true;
            amw7_rus = false;
            W7_NewGame();
            W7_SetSavePath("./Config");
            W7_NativeLoadNotes();
            s_dsAnchorFailureCount = 0;
            s_gameAttached = true;
            s_statusText = "Wizardry 7 detected";
            SetAutomapWindowTitle("Wizardry7Automap Experimental");
            std::printf("Wizardry 7 detected. anchor=0x%p dataseg=0x%p\n",
                        (void*)loadBase,
                        (void*)dataSeg);
            s_forceRedraw = true;
        }
    }
}

static void PresentFrameBuffer()
{
    if (!s_nativeWindow) return;
    InvalidateRect(s_nativeWindow, nullptr, FALSE);
    UpdateWindow(s_nativeWindow);
}

static void DrawStatusScreen()
{
    ClearFrameBuffer();
    PresentFrameBuffer();
    SetStatusOverlayText(s_statusText);
    ShowStatusOverlay(true);
}

static void DrawAutomapFrame()
{
    ShowStatusOverlay(false);
    ClearFrameBuffer();
    if (s_gameAttached) {
        W7_Update(am_width, am_height);
    }
    PresentFrameBuffer();
}

static bool IsAltDown()
{
    return (GetKeyState(VK_MENU) & 0x8000) != 0;
}

static bool IsCtrlDown()
{
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

static void DispatchAutomapMouseButton(uint8_t button, uint8_t clicks, int x, int y)
{
    AM_MouseButtonEvent event = {};
    event.button = button;
    event.clicks = clicks;
    event.x = x;
    event.y = y;
    W7_OnMouseButtonInAutomapWindow(&event, IsAltDown(), IsCtrlDown());
    s_forceRedraw = true;
}

static void BeginLeftDrag(int x, int y, uint8_t clicks)
{
    HideNoteTooltip();
    s_pendingLeftClicks = clicks;
    if (!IsAltDown() && !IsCtrlDown()) {
        s_dragStartX = x;
        s_dragStartY = y;
        s_mouseOldX = x;
        s_mouseOldY = y;
        s_dragStart = true;
        SetCapture(s_nativeWindow);
    }
}

static void EndLeftDrag(int x, int y)
{
    const bool moved = s_dragStart &&
                       (s_dragStartX != x || s_dragStartY != y);
    s_dragStart = false;
    if (GetCapture() == s_nativeWindow) ReleaseCapture();
    if (!moved) {
        DispatchAutomapMouseButton(AM_BUTTON_LEFT,
                                   s_pendingLeftClicks,
                                   x, y);
    }
    s_pendingLeftClicks = 1;
}

static void PaintMainWindow(HWND hwnd, HDC hdc)
{
    RECT client = {};
    GetClientRect(hwnd, &client);
    const int clientWidth = std::max(1L, client.right - client.left);
    const int clientHeight = std::max(1L, client.bottom - client.top);

    if (s_frameBuffer.empty()) {
        FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return;
    }

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc,
                  0, 0, clientWidth, clientHeight,
                  0, 0, am_width, am_height,
                  s_frameBuffer.data(),
                  &s_frameBitmapInfo,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

static LRESULT CALLBACK MainWindowWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc) PaintMainWindow(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE: {
        bool resized = false;
        if (wp != SIZE_MINIMIZED) {
            const int width = static_cast<int>(LOWORD(lp));
            const int height = static_cast<int>(HIWORD(lp));
            if (width > 0 && height > 0 &&
                (width != am_width || height != am_height)) {
                resized = ResizeFrameBuffer(width, height);
                if (resized) HideNoteTooltip();
            }
        }
        ResizeStatusOverlay();

        // During an interactive border drag Windows runs its own modal sizing
        // loop, so the normal application loop may not redraw until the mouse
        // button is released. Render here as well to keep the map live while
        // the user resizes the window.
        if (resized && s_nativeWindow == hwnd) {
            if (s_gameAttached)
                DrawAutomapFrame();
            else
                DrawStatusScreen();
            s_forceRedraw = false;
        }
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
        if (limits) {
            RECT minimum = { 0, 0, W7EA_MIN_WINDOW_SIZE, W7EA_MIN_WINDOW_SIZE };
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            if (AdjustWindowRectForDpiCompat(
                    &minimum, style, FALSE, exStyle, GetWindowDpiCompat(hwnd))) {
                limits->ptMinTrackSize.x = minimum.right - minimum.left;
                limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
            }
        }
        return 0;
    }

    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        const UINT dpi = HIWORD(wp);
        RECT wanted = { 0, 0, am_width, am_height };
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        if (AdjustWindowRectForDpiCompat(&wanted, style, FALSE, exStyle, dpi)) {
            const int x = suggested ? suggested->left : 0;
            const int y = suggested ? suggested->top : 0;
            SetWindowPos(hwnd, nullptr, x, y,
                         wanted.right - wanted.left,
                         wanted.bottom - wanted.top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        }
        RecreateUIFont(hwnd);
        ResizeStatusOverlay();
        HideNoteTooltip();
        s_forceRedraw = true;
        return 0;
    }

    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lp);
        const int y = GET_Y_LPARAM(lp);
        if (!s_trackingMouseLeave) {
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            if (TrackMouseEvent(&track)) s_trackingMouseLeave = true;
        }

        const bool alt = IsAltDown();
        if (!alt && s_dragStart && (x != s_mouseOldX || y != s_mouseOldY)) {
            HideNoteTooltip();
            W7_OnAutomapDrag(s_mouseOldX - x, s_mouseOldY - y);
            s_forceRedraw = true;
        }
        s_mouseOldX = x;
        s_mouseOldY = y;
        W7_OnMouseMotionInAutomapWindow(x, y, alt);
        return 0;
    }

    case WM_MOUSELEAVE:
        s_trackingMouseLeave = false;
        HideNoteTooltip();
        return 0;

    case WM_KILLFOCUS:
        HideNoteTooltip();
        return 0;

    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        BeginLeftDrag(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 1);
        return 0;

    case WM_LBUTTONDBLCLK:
        SetFocus(hwnd);
        BeginLeftDrag(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), 2);
        return 0;

    case WM_LBUTTONUP:
        EndLeftDrag(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        SetFocus(hwnd);
        HideNoteTooltip();
        return 0;

    case WM_MBUTTONUP:
        DispatchAutomapMouseButton(AM_BUTTON_MIDDLE, 1,
                                   GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_RBUTTONUP:
        DispatchAutomapMouseButton(AM_BUTTON_RIGHT, 1,
                                   GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != hwnd) s_dragStart = false;
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        s_running = false;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool RegisterMainWindowClass()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_OWNDC;
    wc.lpfnWndProc = MainWindowWndProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = W7EA_MAIN_CLASS;
    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static bool InitializeWin32Window()
{
    EnableDpiAwarenessForStablePixels();
    if (!RegisterMainWindowClass()) return false;
    if (!InitializeFrameBuffer()) return false;

    const DWORD style = WS_OVERLAPPEDWINDOW;
    const DWORD exStyle = 0;
    s_nativeWindow = CreateWindowExW(
        exStyle, W7EA_MAIN_CLASS, W7EA_MAIN_TITLE, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        std::max(320, am_width), std::max(320, am_height),
        nullptr, nullptr,
        reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)), nullptr);
    if (!s_nativeWindow) return false;

    ApplyConfiguredWindowPlacement(s_nativeWindow, style, exStyle);

    if (!InitializeNativeUI()) {
        DestroyWindow(s_nativeWindow);
        s_nativeWindow = nullptr;
        return false;
    }

    ShowWindow(s_nativeWindow, SW_SHOW);
    UpdateWindow(s_nativeWindow);
    return true;
}

static void ShutdownWin32Window()
{
    HWND mainWindow = s_nativeWindow;
    ShutdownNativeUI();
    if (mainWindow && IsWindow(mainWindow)) {
        DestroyWindow(mainWindow);
    }
    s_nativeWindow = nullptr;
    s_frameBuffer.clear();
    s_w7Atlas.clear();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{

    LoadAutomapConfig();
    if (!s_automapEnabled) {
        return 0;
    }

    EnableProcessInspectionPrivilege();

    if (!InitializeWin32Window()) {
        ShutdownWin32Window();
        MessageBoxW(nullptr,
                    L"Wizardry7Automap could not create its Win32/GDI window.",
                    L"Wizardry7Automap", MB_OK | MB_ICONERROR);
        return 1;
    }

    SetAutomapWindowTitle(s_statusText.c_str());

    uint64_t lastAttachCheck = 0;
    uint64_t lastPoll = 0;

    while (s_running) {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                s_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!s_running) break;

        const uint64_t now64 = GetTickCount64();
        const uint32_t now = static_cast<uint32_t>(now64 & 0xFFFFFFFFu);
        if (now64 - lastAttachCheck >= 1000 || lastAttachCheck == 0) {
            lastAttachCheck = now64;
            EnsureAttachedOrWaiting();
        }

        if (s_gameAttached && ProcessAlive(s_process) && amw7_dataseg_addr != 0) {
            if (now64 - lastPoll >= 33 || lastPoll == 0) {
                lastPoll = now64;
                if (RefreshPC98Snapshot()) {
                    W7_PollState();
                    if (W7_NeedUpdate()) s_forceRedraw = true;
                }
            }
        }

        UpdatePendingNoteTooltip(now);

        if (s_forceRedraw) {
            if (s_gameAttached)
                DrawAutomapFrame();
            else
                DrawStatusScreen();
            s_forceRedraw = false;
        }

        Sleep(5);
    }

    DetachFromProcess();
    ShutdownWin32Window();
    return 0;
}
