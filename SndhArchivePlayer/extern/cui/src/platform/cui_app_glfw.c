/* GLFW backend (Wayland, X11, Windows, macOS). Select it with
 * -DCUI_PLATFORM=glfw. GLFW always provides an OpenGL context here. */
#include "cui_platform.h"
#include <GLFW/glfw3.h>

typedef struct {
    GLFWwindow *win;
    GLFWcursor *cursors[CUI_CURSOR_COUNT];
    cui_cursor cursor;
} cui__glfw;

static cui__glfw *cui__g(cui_app *app) { return app->plat; }

double cui__plat_now(void) { return glfwGetTime(); }
const char *cui__plat_name(void) { return "glfw"; }
int cui__plat_system_dark(void) { return -1; }

static cui_app *cui__glfw_app(GLFWwindow *w) { return (cui_app *)glfwGetWindowUserPointer(w); }

static void cui__glfw_set_cursor(void *user, cui_cursor c) {
    cui__glfw *g = cui__g(user);
    if (c == g->cursor || (int)c < 0 || c >= CUI_CURSOR_COUNT) return;
    g->cursor = c;
    glfwSetCursor(g->win, g->cursors[c]);
}
static void cui__glfw_set_clip(void *user, const char *utf8) { glfwSetClipboardString(cui__g(user)->win, utf8); }
static const char *cui__glfw_get_clip(void *user) { return glfwGetClipboardString(cui__g(user)->win); }

static void cui__glfw_scale(GLFWwindow *w, float *sx) {
    float x = 1, y = 1;
    glfwGetWindowContentScale(w, &x, &y);
    *sx = x;
}

static cui_key cui__glfw_key(int k) {
    if (k >= GLFW_KEY_A && k <= GLFW_KEY_Z) return (cui_key)(CUI_KEY_A + (k - GLFW_KEY_A));
    if (k >= GLFW_KEY_0 && k <= GLFW_KEY_9) return (cui_key)(CUI_KEY_0 + (k - GLFW_KEY_0));
    if (k >= GLFW_KEY_F1 && k <= GLFW_KEY_F12) return (cui_key)(CUI_KEY_F1 + (k - GLFW_KEY_F1));
    switch (k) {
    case GLFW_KEY_TAB: return CUI_KEY_TAB;
    case GLFW_KEY_ENTER: case GLFW_KEY_KP_ENTER: return CUI_KEY_ENTER;
    case GLFW_KEY_ESCAPE: return CUI_KEY_ESCAPE;
    case GLFW_KEY_BACKSPACE: return CUI_KEY_BACKSPACE;
    case GLFW_KEY_DELETE: return CUI_KEY_DELETE;
    case GLFW_KEY_SPACE: return CUI_KEY_SPACE;
    case GLFW_KEY_LEFT: return CUI_KEY_LEFT;
    case GLFW_KEY_RIGHT: return CUI_KEY_RIGHT;
    case GLFW_KEY_UP: return CUI_KEY_UP;
    case GLFW_KEY_DOWN: return CUI_KEY_DOWN;
    case GLFW_KEY_HOME: return CUI_KEY_HOME;
    case GLFW_KEY_END: return CUI_KEY_END;
    case GLFW_KEY_PAGE_UP: return CUI_KEY_PAGE_UP;
    case GLFW_KEY_PAGE_DOWN: return CUI_KEY_PAGE_DOWN;
    case GLFW_KEY_MINUS: return CUI_KEY_MINUS;
    case GLFW_KEY_EQUAL: return CUI_KEY_EQUAL;
    case GLFW_KEY_COMMA: return CUI_KEY_COMMA;
    case GLFW_KEY_PERIOD: return CUI_KEY_PERIOD;
    case GLFW_KEY_SLASH: return CUI_KEY_SLASH;
    default: return CUI_KEY_NONE;
    }
}

static int cui__glfw_mods(int m) {
    int r = 0;
    if (m & GLFW_MOD_SHIFT) r |= CUI_MOD_SHIFT;
    if (m & GLFW_MOD_CONTROL) r |= CUI_MOD_CTRL;
    if (m & GLFW_MOD_ALT) r |= CUI_MOD_ALT;
    if (m & GLFW_MOD_SUPER) r |= CUI_MOD_SUPER;
    return r;
}

static void cb_cursor(GLFWwindow *w, double x, double y) {
    cui_app *app = cui__glfw_app(w);
#if defined(__APPLE__)
    cui_input_mouse_move(app->ui, (float)x, (float)y);
#else
    float s; cui__glfw_scale(w, &s);
    cui_input_mouse_move(app->ui, (float)x / s, (float)y / s);
#endif
}
static void cb_enter(GLFWwindow *w, int entered) { if (!entered) cui_input_mouse_leave(cui__glfw_app(w)->ui); }
static void cb_button(GLFWwindow *w, int b, int action, int mods) {
    (void)mods;
    cui_mouse_button mb = b == GLFW_MOUSE_BUTTON_LEFT ? CUI_MOUSE_LEFT : b == GLFW_MOUSE_BUTTON_RIGHT ? CUI_MOUSE_RIGHT : CUI_MOUSE_MIDDLE;
    cui_input_mouse_button(cui__glfw_app(w)->ui, mb, action == GLFW_PRESS);
}
static void cb_scroll(GLFWwindow *w, double dx, double dy) { cui_input_scroll(cui__glfw_app(w)->ui, (float)-dx, (float)dy); }
static void cb_key(GLFWwindow *w, int key, int sc, int action, int mods) {
    (void)sc;
    cui_key k = cui__glfw_key(key);
    if (action == GLFW_PRESS || action == GLFW_REPEAT) cui_input_key(cui__glfw_app(w)->ui, k, true, cui__glfw_mods(mods));
    else cui_input_key(cui__glfw_app(w)->ui, k, false, cui__glfw_mods(mods));
}
static void cb_char(GLFWwindow *w, unsigned int cp) {
    char buf[8];
    int n = cui__utf8_encode(cp, buf);
    buf[n] = 0;
    cui_input_text(cui__glfw_app(w)->ui, buf);
}
static void cb_focus(GLFWwindow *w, int f) { cui_input_focus(cui__glfw_app(w)->ui, f != 0); }
static void cb_close(GLFWwindow *w) { cui__glfw_app(w)->running = false; }
static void cb_refresh(GLFWwindow *w) { cui_request_redraw(cui__glfw_app(w)->ui); }
static void cb_drop(GLFWwindow *w, int count, const char **paths) {
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
#if !defined(__APPLE__)
    float s; cui__glfw_scale(w, &s);
    x /= s; y /= s;
#endif
    cui_input_file_drop(cui__glfw_app(w)->ui, paths, count, (float)x, (float)y);
}

bool cui__plat_init(cui_app *app) {
    if (!glfwInit()) return false;
    cui__glfw *g = calloc(1, sizeof *g);
    if (!g) return false;
    app->plat = g;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, app->desc.fixed_size ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    g->win = glfwCreateWindow(app->desc.width, app->desc.height, app->title, NULL, NULL);
    if (!g->win) { free(g); app->plat = NULL; glfwTerminate(); return false; }
    glfwSetWindowUserPointer(g->win, app);
    glfwSetWindowSizeLimits(g->win, app->desc.min_width, app->desc.min_height, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetCursorPosCallback(g->win, cb_cursor);
    glfwSetCursorEnterCallback(g->win, cb_enter);
    glfwSetMouseButtonCallback(g->win, cb_button);
    glfwSetScrollCallback(g->win, cb_scroll);
    glfwSetKeyCallback(g->win, cb_key);
    glfwSetCharCallback(g->win, cb_char);
    glfwSetWindowFocusCallback(g->win, cb_focus);
    glfwSetWindowCloseCallback(g->win, cb_close);
    glfwSetWindowRefreshCallback(g->win, cb_refresh);
    glfwSetDropCallback(g->win, cb_drop);
    int shapes[CUI_CURSOR_COUNT] = { GLFW_ARROW_CURSOR, GLFW_HAND_CURSOR, GLFW_IBEAM_CURSOR, GLFW_HRESIZE_CURSOR,
                                     GLFW_VRESIZE_CURSOR, GLFW_ARROW_CURSOR, GLFW_ARROW_CURSOR };
    for (int i = 0; i < CUI_CURSOR_COUNT; i++) g->cursors[i] = glfwCreateStandardCursor(shapes[i]);
    cui_platform_iface pi;
    memset(&pi, 0, sizeof pi);
    pi.user = app;
    pi.set_cursor = cui__glfw_set_cursor;
    pi.set_clipboard = cui__glfw_set_clip;
    pi.get_clipboard = cui__glfw_get_clip;
    cui_set_platform(app->ui, &pi);
    return true;
}

void cui__plat_show(cui_app *app) { glfwShowWindow(cui__g(app)->win); }

void cui__plat_shutdown(cui_app *app) {
    cui__glfw *g = cui__g(app);
    if (!g) return;
    for (int i = 0; i < CUI_CURSOR_COUNT; i++) if (g->cursors[i]) glfwDestroyCursor(g->cursors[i]);
    glfwDestroyWindow(g->win);
    free(g);
    app->plat = NULL;
    glfwTerminate();
}

bool cui__plat_gl_create(cui_app *app, bool vsync) {
    glfwMakeContextCurrent(cui__g(app)->win);
    glfwSwapInterval(vsync ? 1 : 0);
    return true;
}

void *cui__plat_gl_proc(const char *name) { return (void *)glfwGetProcAddress(name); }
void cui__plat_gl_swap(cui_app *app) { glfwSwapBuffers(cui__g(app)->win); }

void cui__plat_present(cui_app *app, const void *bgra, int w, int h, int stride) {
    /* GLFW windows always have a GL context; the software path is not used */
    (void)app; (void)bgra; (void)w; (void)h; (void)stride;
}

void cui__plat_metrics(cui_app *app, float *w, float *h, float *scale, int *fb_w, int *fb_h) {
    GLFWwindow *win = cui__g(app)->win;
    int fw, fh;
    glfwGetFramebufferSize(win, &fw, &fh);
    float s;
    cui__glfw_scale(win, &s);
    if (s <= 0) s = 1;
    *scale = s;
    *fb_w = fw; *fb_h = fh;
    *w = (float)fw / s;
    *h = (float)fh / s;
}

void cui__plat_set_title(cui_app *app, const char *title) { glfwSetWindowTitle(cui__g(app)->win, title); }
void cui__plat_set_size(cui_app *app, int w, int h) { glfwSetWindowSize(cui__g(app)->win, w, h); }
void *cui__plat_native(cui_app *app) { return cui__g(app)->win; }

void cui__plat_wait(cui_app *app, double timeout) {
    (void)app;
    if (timeout < 0) glfwWaitEvents();
    else if (timeout > 0) glfwWaitEventsTimeout(timeout);
    else glfwPollEvents();
}

bool cui__plat_present_part(cui_app *app, const void *bgra, int w, int h, int stride, int x, int y, int pw, int ph) {
    (void)app; (void)bgra; (void)w; (void)h; (void)stride; (void)x; (void)y; (void)pw; (void)ph;
    return false;   /* the whole buffer is shown each time */
}

void cui__plat_gl_release(cui_app *app) { (void)app; }   /* not used: these keep OpenGL */
