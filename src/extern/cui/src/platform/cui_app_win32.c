/* Win32 backend (Windows 7 and later). Per-monitor DPI awareness, WGL
 * OpenGL 3.3 with a GDI software fallback, UTF-16 text input, clipboard,
 * dark title bars and smooth live resizing. */
#include "cui_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

typedef HGLRC (WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL (WINAPI *PFN_wglSwapIntervalEXT)(int);
typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
typedef BOOL (WINAPI *PFN_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

#define WGL_CONTEXT_MAJOR_VERSION_ARB_ 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB_ 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB_ 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB_ 0x0001

typedef struct {
    HWND hwnd;
    HDC hdc;
    HGLRC glrc;
    int width, height;     /* client pixels */
    float scale;
    bool tracking;
    WCHAR high_surrogate;
    HCURSOR cursors[CUI_CURSOR_COUNT];
    cui_cursor cursor;
    bool in_size_move;
    BITMAPINFO bmi;
} cui__win32;

static HMODULE cui__opengl32;
static PFN_GetDpiForWindow cui__GetDpiForWindow;
static PFN_AdjustWindowRectExForDpi cui__AdjustWindowRectExForDpi;

static cui__win32 *cui__w(cui_app *app) { return app->plat; }

double cui__plat_now(void) {
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

const char *cui__plat_name(void) { return "win32"; }

int cui__plat_system_dark(void) {
    HKEY key;
    DWORD value = 1, size = sizeof value;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return -1;
    LONG r = RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS) return -1;
    return value == 0 ? 1 : 0;
}

static void cui__win32_dpi_awareness(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    PFN_SetProcessDpiAwarenessContext ctx = user32 ? (PFN_SetProcessDpiAwarenessContext)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : NULL;
    if (user32) {
        cui__GetDpiForWindow = (PFN_GetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");
        cui__AdjustWindowRectExForDpi = (PFN_AdjustWindowRectExForDpi)(void *)GetProcAddress(user32, "AdjustWindowRectExForDpi");
    }
    if (ctx && ctx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */)) return;
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        PFN_SetProcessDpiAwareness f = (PFN_SetProcessDpiAwareness)(void *)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (f && SUCCEEDED(f(2))) return;
    }
    SetProcessDPIAware();
}

static float cui__win32_window_scale(HWND hwnd) {
    const char *env = getenv("CUI_SCALE");
    if (env) { float s = (float)atof(env); if (s > 0.25f && s < 8) return s; }
    UINT dpi = 96;
    if (cui__GetDpiForWindow && hwnd) dpi = cui__GetDpiForWindow(hwnd);
    else {
        HDC dc = GetDC(NULL);
        dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
    }
    return dpi > 0 ? (float)dpi / 96.0f : 1.0f;
}

static void cui__win32_dark_titlebar(HWND hwnd, bool dark) {
    static PFN_DwmSetWindowAttribute set;
    static bool tried;
    if (!tried) {
        tried = true;
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) set = (PFN_DwmSetWindowAttribute)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    }
    if (!set) return;
    BOOL v = dark ? TRUE : FALSE;
    if (FAILED(set(hwnd, 20, &v, sizeof v))) set(hwnd, 19, &v, sizeof v);
}

static int cui__win32_mods(void) {
    int m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= CUI_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= CUI_MOD_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000) m |= CUI_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= CUI_MOD_SUPER;
    return m;
}

static cui_key cui__win32_key(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return (cui_key)(CUI_KEY_A + (int)(vk - 'A'));
    if (vk >= '0' && vk <= '9') return (cui_key)(CUI_KEY_0 + (int)(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (cui_key)(CUI_KEY_F1 + (int)(vk - VK_F1));
    switch (vk) {
    case VK_TAB: return CUI_KEY_TAB;
    case VK_RETURN: return CUI_KEY_ENTER;
    case VK_ESCAPE: return CUI_KEY_ESCAPE;
    case VK_BACK: return CUI_KEY_BACKSPACE;
    case VK_DELETE: return CUI_KEY_DELETE;
    case VK_SPACE: return CUI_KEY_SPACE;
    case VK_LEFT: return CUI_KEY_LEFT;
    case VK_RIGHT: return CUI_KEY_RIGHT;
    case VK_UP: return CUI_KEY_UP;
    case VK_DOWN: return CUI_KEY_DOWN;
    case VK_HOME: return CUI_KEY_HOME;
    case VK_END: return CUI_KEY_END;
    case VK_PRIOR: return CUI_KEY_PAGE_UP;
    case VK_NEXT: return CUI_KEY_PAGE_DOWN;
    case VK_OEM_MINUS: return CUI_KEY_MINUS;
    case VK_OEM_PLUS: return CUI_KEY_EQUAL;
    case VK_OEM_COMMA: return CUI_KEY_COMMA;
    case VK_OEM_PERIOD: return CUI_KEY_PERIOD;
    case VK_OEM_2: return CUI_KEY_SLASH;
    default: return CUI_KEY_NONE;
    }
}

static void cui__win32_set_cursor(void *user, cui_cursor c) {
    cui_app *app = user;
    cui__win32 *w = cui__w(app);
    if ((int)c < 0 || c >= CUI_CURSOR_COUNT) return;
    w->cursor = c;
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(w->hwnd, &p);
    if (p.x >= 0 && p.y >= 0 && p.x < w->width && p.y < w->height) SetCursor(w->cursors[c]);
}

static void cui__win32_set_clip(void *user, const char *utf8) {
    cui_app *app = user;
    cui__win32 *w = cui__w(app);
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0 || !OpenClipboard(w->hwnd)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n * sizeof(WCHAR));
    if (mem) {
        WCHAR *dst = GlobalLock(mem);
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, dst, n);
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    }
    CloseClipboard();
}

static const char *cui__win32_get_clip(void *user) {
    cui_app *app = user;
    cui__win32 *w = cui__w(app);
    if (!OpenClipboard(w->hwnd)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const char *result = NULL;
    if (h) {
        const WCHAR *src = GlobalLock(h);
        if (src) {
            int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
            if (n > 0) {
                char *buf = malloc((size_t)n);
                if (buf) {
                    WideCharToMultiByte(CP_UTF8, 0, src, -1, buf, n, NULL, NULL);
                    /* drop carriage returns */
                    char *o = buf;
                    for (char *p = buf; *p; p++) if (*p != '\r') *o++ = *p;
                    *o = 0;
                    cui__app_set_clipboard_cache(app, buf);
                    free(buf);
                    result = app->clipboard;
                }
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
}

static LRESULT CALLBACK cui__win32_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    cui_app *app = (cui_app *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!app) {
        if (msg == WM_NCCREATE) {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    cui__win32 *w = cui__w(app);
    cui_ctx *ui = app->ui;
    float s = w ? w->scale : 1.0f;
    switch (msg) {
    case WM_CLOSE:
        app->running = false;
        return 0;
    case WM_SIZE:
        if (w) {
            w->width = LOWORD(lp);
            w->height = HIWORD(lp);
            cui_request_redraw(ui);
            if (w->in_size_move && w->width > 0 && w->height > 0) cui__app_live_frame(app);
        }
        return 0;
    case WM_ENTERSIZEMOVE: if (w) w->in_size_move = true; return 0;
    case WM_EXITSIZEMOVE: if (w) w->in_size_move = false; return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        if (w && w->in_size_move) cui__app_live_frame(app);
        cui_request_redraw(ui);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DPICHANGED: {
        if (w) w->scale = (float)HIWORD(wp) / 96.0f;
        RECT *r = (RECT *)lp;
        SetWindowPos(hwnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        cui_request_redraw(ui);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        float sc = w ? w->scale : cui__win32_window_scale(hwnd);
        RECT r = { 0, 0, (LONG)(app->desc.min_width * sc), (LONG)(app->desc.min_height * sc) };
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(hwnd, GWL_STYLE), FALSE, 0);
        mm->ptMinTrackSize.x = r.right - r.left;
        mm->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_MOUSEMOVE:
        if (w && !w->tracking) {
            TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            w->tracking = true;
        }
        cui_input_mouse_move(ui, (float)GET_X_LPARAM(lp) / s, (float)GET_Y_LPARAM(lp) / s);
        return 0;
    case WM_MOUSELEAVE:
        if (w) w->tracking = false;
        cui_input_mouse_leave(ui);
        return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
        bool down = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
        cui_mouse_button b = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) ? CUI_MOUSE_LEFT
                           : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) ? CUI_MOUSE_RIGHT : CUI_MOUSE_MIDDLE;
        cui_input_mouse_move(ui, (float)GET_X_LPARAM(lp) / s, (float)GET_Y_LPARAM(lp) / s);
        cui_input_mouse_button(ui, b, down);
        if (down) SetCapture(hwnd);
        else if (!(wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) ReleaseCapture();
        return 0;
    }
    case WM_MOUSEWHEEL:
        cui_input_scroll(ui, 0, (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA);
        return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
        if (n > 64) n = 64;
        POINT pt = { 0, 0 };
        DragQueryPoint(drop, &pt);
        char *paths[64];
        int got = 0;
        for (UINT i = 0; i < n; i++) {
            UINT len = DragQueryFileW(drop, i, NULL, 0);
            WCHAR *wbuf = (WCHAR *)malloc(sizeof(WCHAR) * (len + 1));
            if (!wbuf) continue;
            DragQueryFileW(drop, i, wbuf, len + 1);
            int u = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
            char *ubuf = u > 0 ? (char *)malloc((size_t)u) : NULL;
            if (ubuf) { WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, ubuf, u, NULL, NULL); paths[got++] = ubuf; }
            free(wbuf);
        }
        DragFinish(drop);
        cui_input_file_drop(ui, (const char *const *)paths, got, (float)pt.x / s, (float)pt.y / s);
        for (int i = 0; i < got; i++) free(paths[i]);
        SetForegroundWindow(hwnd);
        return 0;
    }
    case WM_MOUSEHWHEEL:
        cui_input_scroll(ui, -(float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA, 0);
        return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        cui_key k = cui__win32_key(wp);
        if (k) cui_input_key(ui, k, down, cui__win32_mods());
        else if (wp == VK_SHIFT || wp == VK_CONTROL || wp == VK_MENU || wp == VK_LWIN || wp == VK_RWIN)
            cui_input_key(ui, CUI_KEY_NONE, down, cui__win32_mods());   /* a modifier on its own: releasing Ctrl after a shortcut must register */
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
        if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) return 0;
        return 0;
    }
    case WM_CHAR: {
        WCHAR c = (WCHAR)wp;
        if (c >= 0xD800 && c <= 0xDBFF) { if (w) w->high_surrogate = c; return 0; }
        uint32_t cp = c;
        if (c >= 0xDC00 && c <= 0xDFFF) {
            if (!w || !w->high_surrogate) return 0;
            cp = 0x10000 + (((uint32_t)w->high_surrogate - 0xD800) << 10) + ((uint32_t)c - 0xDC00);
            w->high_surrogate = 0;
        }
        if (cp < 32 || cp == 127) return 0;
        char buf[8];
        int n = cui__utf8_encode(cp, buf);
        buf[n] = 0;
        cui_input_text(ui, buf);
        return 0;
    }
    case WM_SETFOCUS: cui_input_focus(ui, true); return 0;
    case WM_KILLFOCUS: cui_input_focus(ui, false); return 0;
    case WM_SETCURSOR:
        if (w && LOWORD(lp) == HTCLIENT) { SetCursor(w->cursors[w->cursor]); return TRUE; }
        break;
    case WM_SETTINGCHANGE:
        if (app->desc.follow_system_theme && lp && lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) {
            int dark = cui__plat_system_dark();
            if (dark >= 0) {
                cui_set_theme(ui, dark ? CUI_THEME_DARK : CUI_THEME_LIGHT);
                cui__win32_dark_titlebar(hwnd, dark == 1);
            }
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool cui__plat_init(cui_app *app) {
    cui__win32_dpi_awareness();
    cui__win32 *w = calloc(1, sizeof *w);
    if (!w) return false;
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = cui__win32_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.lpszClassName = L"CUIWindow";
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (app->desc.fixed_size) style &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    float scale = cui__win32_window_scale(NULL);
    RECT r = { 0, 0, (LONG)(app->desc.width * scale), (LONG)(app->desc.height * scale) };
    AdjustWindowRectEx(&r, style, FALSE, 0);
    WCHAR title[256];
    MultiByteToWideChar(CP_UTF8, 0, app->title, -1, title, 256);
    app->plat = w;
    w->hwnd = CreateWindowExW(0, wc.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              NULL, NULL, inst, app);
    if (!w->hwnd) { free(w); app->plat = NULL; return false; }
    DragAcceptFiles(w->hwnd, TRUE);   /* files dropped from Explorer arrive as WM_DROPFILES */
    w->scale = cui__win32_window_scale(w->hwnd);
    if (w->scale != scale) {
        RECT r2 = { 0, 0, (LONG)(app->desc.width * w->scale), (LONG)(app->desc.height * w->scale) };
        AdjustWindowRectEx(&r2, style, FALSE, 0);
        SetWindowPos(w->hwnd, NULL, 0, 0, r2.right - r2.left, r2.bottom - r2.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT cr;
    GetClientRect(w->hwnd, &cr);
    w->width = cr.right; w->height = cr.bottom;
    w->hdc = GetDC(w->hwnd);
    LPCWSTR ids[CUI_CURSOR_COUNT] = { (LPCWSTR)IDC_ARROW, (LPCWSTR)IDC_HAND, (LPCWSTR)IDC_IBEAM, (LPCWSTR)IDC_SIZEWE,
                                      (LPCWSTR)IDC_SIZENS, (LPCWSTR)IDC_SIZEALL, (LPCWSTR)IDC_NO };
    for (int i = 0; i < CUI_CURSOR_COUNT; i++) w->cursors[i] = LoadCursorW(NULL, ids[i]);
    cui__win32_dark_titlebar(w->hwnd, app->desc.theme == CUI_THEME_DARK);
    cui_platform_iface pi;
    memset(&pi, 0, sizeof pi);
    pi.user = app;
    pi.set_clipboard = cui__win32_set_clip;
    pi.get_clipboard = cui__win32_get_clip;
    pi.set_cursor = cui__win32_set_cursor;
    cui_set_platform(app->ui, &pi);
    return true;
}

void cui__plat_show(cui_app *app) {
    cui__win32 *w = cui__w(app);
    ShowWindow(w->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(w->hwnd);
}

void cui__plat_shutdown(cui_app *app) {
    cui__win32 *w = cui__w(app);
    if (!w) return;
    if (w->glrc) { wglMakeCurrent(NULL, NULL); wglDeleteContext(w->glrc); }
    if (w->hdc) ReleaseDC(w->hwnd, w->hdc);
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, 0);   /* no more messages to the app */
    DestroyWindow(w->hwnd);
    free(w);
    app->plat = NULL;
}

void *cui__plat_gl_proc(const char *name) {
    void *p = (void *)wglGetProcAddress(name);
    if (p == NULL || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1) {
        if (!cui__opengl32) cui__opengl32 = LoadLibraryW(L"opengl32.dll");
        p = cui__opengl32 ? (void *)GetProcAddress(cui__opengl32, name) : NULL;
    }
    return p;
}

bool cui__plat_gl_create(cui_app *app, bool vsync) {
    cui__win32 *w = cui__w(app);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(w->hdc, &pfd);
    if (!pf || !SetPixelFormat(w->hdc, pf, &pfd)) return false;
    HGLRC legacy = wglCreateContext(w->hdc);
    if (!legacy) return false;
    wglMakeCurrent(w->hdc, legacy);
    PFN_wglCreateContextAttribsARB create = (PFN_wglCreateContextAttribsARB)(void *)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!create) { wglMakeCurrent(NULL, NULL); wglDeleteContext(legacy); return false; }
    int attrs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB_, 3, WGL_CONTEXT_MINOR_VERSION_ARB_, 3,
                    WGL_CONTEXT_PROFILE_MASK_ARB_, WGL_CONTEXT_CORE_PROFILE_BIT_ARB_, 0 };
    HGLRC ctx = create(w->hdc, NULL, attrs);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);
    if (!ctx) return false;
    if (!wglMakeCurrent(w->hdc, ctx)) { wglDeleteContext(ctx); return false; }
    w->glrc = ctx;
    PFN_wglSwapIntervalEXT swap = (PFN_wglSwapIntervalEXT)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap) swap(vsync ? 1 : 0);
    return true;
}

void cui__plat_gl_swap(cui_app *app) { SwapBuffers(cui__w(app)->hdc); }

void cui__plat_present(cui_app *app, const void *bgra, int width, int height, int stride) {
    cui__win32 *w = cui__w(app);
    BITMAPINFO *bmi = &w->bmi;
    memset(bmi, 0, sizeof *bmi);
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = stride / 4;
    bmi->bmiHeader.biHeight = -height;   /* top down */
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(w->hdc, 0, 0, (DWORD)width, (DWORD)height, 0, 0, 0, (UINT)height, bgra, bmi, DIB_RGB_COLORS);
}

void cui__plat_metrics(cui_app *app, float *wpt, float *hpt, float *scale, int *fb_w, int *fb_h) {
    cui__win32 *w = cui__w(app);
    *scale = w->scale;
    *fb_w = w->width; *fb_h = w->height;
    *wpt = (float)w->width / w->scale;
    *hpt = (float)w->height / w->scale;
}

void cui__plat_set_title(cui_app *app, const char *title) {
    WCHAR buf[512];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, buf, 512);
    SetWindowTextW(cui__w(app)->hwnd, buf);
}

void cui__plat_set_size(cui_app *app, int width, int height) {
    cui__win32 *w = cui__w(app);
    RECT r = { 0, 0, (LONG)(width * w->scale), (LONG)(height * w->scale) };
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(w->hwnd, GWL_STYLE), FALSE, 0);
    SetWindowPos(w->hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

void *cui__plat_native(cui_app *app) { return cui__w(app)->hwnd; }

void cui__plat_wait(cui_app *app, double timeout) {
    if (timeout != 0) {
        DWORD ms = timeout < 0 ? INFINITE : (DWORD)(timeout * 1000.0 + 0.5);
        MsgWaitForMultipleObjectsEx(0, NULL, ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { app->running = false; break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool cui__plat_present_part(cui_app *app, const void *bgra, int w, int h, int stride, int x, int y, int pw, int ph) {
    (void)app; (void)bgra; (void)w; (void)h; (void)stride; (void)x; (void)y; (void)pw; (void)ph;
    return false;   /* the whole buffer is shown each time */
}

void cui__plat_gl_release(cui_app *app) {
    cui__win32 *w = cui__w(app);
    if (w->glrc) { wglMakeCurrent(NULL, NULL); wglDeleteContext(w->glrc); w->glrc = NULL; }
}
