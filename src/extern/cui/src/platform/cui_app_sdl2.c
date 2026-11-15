/* SDL2 backend: Wayland, X11, Windows, macOS, and everything else SDL runs
 * on. Select it with -DCUI_PLATFORM=sdl2. */
#include "cui_platform.h"
#include <SDL.h>

typedef struct {
    SDL_Window *win;
    SDL_GLContext gl;
    SDL_Cursor *cursors[CUI_CURSOR_COUNT];
    cui_cursor cursor;
    SDL_Surface *surface;
} cui__sdl;

static cui__sdl *cui__s(cui_app *app) { return app->plat; }

double cui__plat_now(void) { return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }
const char *cui__plat_name(void) { return "sdl2"; }
int cui__plat_system_dark(void) { return -1; }

static void cui__sdl_set_cursor(void *user, cui_cursor c) {
    cui__sdl *s = cui__s(user);
    if (c == s->cursor || (int)c < 0 || c >= CUI_CURSOR_COUNT) return;
    s->cursor = c;
    if (s->cursors[c]) SDL_SetCursor(s->cursors[c]);
}
static void cui__sdl_set_clip(void *user, const char *utf8) { (void)user; SDL_SetClipboardText(utf8); }
static const char *cui__sdl_get_clip(void *user) {
    cui_app *app = user;
    char *t = SDL_GetClipboardText();
    if (!t) return NULL;
    cui__app_set_clipboard_cache(app, t);
    SDL_free(t);
    return app->clipboard;
}

bool cui__plat_init(cui_app *app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) return false;
    cui__sdl *s = calloc(1, sizeof *s);
    if (!s) return false;
    app->plat = s;
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
    if (!app->desc.fixed_size) flags |= SDL_WINDOW_RESIZABLE;
    const char *env = getenv("CUI_RENDERER");
    if (app->desc.renderer != CUI_RENDERER_SOFTWARE && !(env && strcmp(env, "software") == 0)) {
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    }
    s->win = SDL_CreateWindow(app->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, app->desc.width, app->desc.height, flags);
    if (!s->win && (flags & SDL_WINDOW_OPENGL)) {
        flags &= ~(Uint32)SDL_WINDOW_OPENGL;
        s->win = SDL_CreateWindow(app->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, app->desc.width, app->desc.height, flags);
    }
    if (!s->win) { free(s); app->plat = NULL; return false; }
    SDL_SetWindowMinimumSize(s->win, app->desc.min_width, app->desc.min_height);
    SDL_SystemCursor ids[CUI_CURSOR_COUNT] = { SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_HAND, SDL_SYSTEM_CURSOR_IBEAM,
                                              SDL_SYSTEM_CURSOR_SIZEWE, SDL_SYSTEM_CURSOR_SIZENS, SDL_SYSTEM_CURSOR_SIZEALL, SDL_SYSTEM_CURSOR_NO };
    for (int i = 0; i < CUI_CURSOR_COUNT; i++) s->cursors[i] = SDL_CreateSystemCursor(ids[i]);
    SDL_StartTextInput();
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    cui_platform_iface pi;
    memset(&pi, 0, sizeof pi);
    pi.user = app;
    pi.set_cursor = cui__sdl_set_cursor;
    pi.set_clipboard = cui__sdl_set_clip;
    pi.get_clipboard = cui__sdl_get_clip;
    cui_set_platform(app->ui, &pi);
    return true;
}

void cui__plat_show(cui_app *app) { SDL_ShowWindow(cui__s(app)->win); }

void cui__plat_shutdown(cui_app *app) {
    cui__sdl *s = cui__s(app);
    if (!s) return;
    for (int i = 0; i < CUI_CURSOR_COUNT; i++) if (s->cursors[i]) SDL_FreeCursor(s->cursors[i]);
    if (s->gl) SDL_GL_DeleteContext(s->gl);
    SDL_DestroyWindow(s->win);
    free(s);
    app->plat = NULL;
    SDL_Quit();
}

bool cui__plat_gl_create(cui_app *app, bool vsync) {
    cui__sdl *s = cui__s(app);
    if (!(SDL_GetWindowFlags(s->win) & SDL_WINDOW_OPENGL)) return false;
    s->gl = SDL_GL_CreateContext(s->win);
    if (!s->gl) return false;
    SDL_GL_MakeCurrent(s->win, s->gl);
    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    return true;
}

void *cui__plat_gl_proc(const char *name) { return SDL_GL_GetProcAddress(name); }
void cui__plat_gl_swap(cui_app *app) { SDL_GL_SwapWindow(cui__s(app)->win); }

void cui__plat_present(cui_app *app, const void *bgra, int w, int h, int stride) {
    cui__sdl *s = cui__s(app);
    SDL_Surface *dst = SDL_GetWindowSurface(s->win);
    if (!dst) return;
    SDL_Surface *src = SDL_CreateRGBSurfaceWithFormatFrom((void *)bgra, w, h, 32, stride, SDL_PIXELFORMAT_ARGB8888);
    if (src) {
        SDL_BlitSurface(src, NULL, dst, NULL);
        SDL_FreeSurface(src);
    }
    SDL_UpdateWindowSurface(s->win);
}

void cui__plat_metrics(cui_app *app, float *w, float *h, float *scale, int *fb_w, int *fb_h) {
    cui__sdl *s = cui__s(app);
    int ww, wh, dw, dh;
    SDL_GetWindowSize(s->win, &ww, &wh);
    if (s->gl) SDL_GL_GetDrawableSize(s->win, &dw, &dh);
    else { dw = ww; dh = wh; }
    float sc = ww > 0 ? (float)dw / (float)ww : 1.0f;
    const char *env = getenv("CUI_SCALE");
    if (env && sc == 1.0f) { float e = (float)atof(env); if (e > 0.25f && e < 8) { sc = e; } }
    *scale = sc;
    *fb_w = (int)(ww * sc + 0.5f);
    *fb_h = (int)(wh * sc + 0.5f);
    *w = (float)ww;
    *h = (float)wh;
    if (!s->gl) { *w = (float)ww / sc; *h = (float)wh / sc; *fb_w = ww; *fb_h = wh; }
}

void cui__plat_set_title(cui_app *app, const char *title) { SDL_SetWindowTitle(cui__s(app)->win, title); }
void cui__plat_set_size(cui_app *app, int w, int h) { SDL_SetWindowSize(cui__s(app)->win, w, h); }
void *cui__plat_native(cui_app *app) { return cui__s(app)->win; }

static cui_key cui__sdl_key(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return (cui_key)(CUI_KEY_A + (k - SDLK_a));
    if (k >= SDLK_0 && k <= SDLK_9) return (cui_key)(CUI_KEY_0 + (k - SDLK_0));
    if (k >= SDLK_F1 && k <= SDLK_F12) return (cui_key)(CUI_KEY_F1 + (k - SDLK_F1));
    switch (k) {
    case SDLK_TAB: return CUI_KEY_TAB;
    case SDLK_RETURN: case SDLK_KP_ENTER: return CUI_KEY_ENTER;
    case SDLK_ESCAPE: return CUI_KEY_ESCAPE;
    case SDLK_BACKSPACE: return CUI_KEY_BACKSPACE;
    case SDLK_DELETE: return CUI_KEY_DELETE;
    case SDLK_SPACE: return CUI_KEY_SPACE;
    case SDLK_LEFT: return CUI_KEY_LEFT;
    case SDLK_RIGHT: return CUI_KEY_RIGHT;
    case SDLK_UP: return CUI_KEY_UP;
    case SDLK_DOWN: return CUI_KEY_DOWN;
    case SDLK_HOME: return CUI_KEY_HOME;
    case SDLK_END: return CUI_KEY_END;
    case SDLK_PAGEUP: return CUI_KEY_PAGE_UP;
    case SDLK_PAGEDOWN: return CUI_KEY_PAGE_DOWN;
    case SDLK_MINUS: return CUI_KEY_MINUS;
    case SDLK_EQUALS: return CUI_KEY_EQUAL;
    case SDLK_COMMA: return CUI_KEY_COMMA;
    case SDLK_PERIOD: return CUI_KEY_PERIOD;
    case SDLK_SLASH: return CUI_KEY_SLASH;
    default: return CUI_KEY_NONE;
    }
}

static int cui__sdl_mods(Uint16 m) {
    int r = 0;
    if (m & KMOD_SHIFT) r |= CUI_MOD_SHIFT;
    if (m & KMOD_CTRL) r |= CUI_MOD_CTRL;
    if (m & KMOD_ALT) r |= CUI_MOD_ALT;
    if (m & KMOD_GUI) r |= CUI_MOD_SUPER;
    return r;
}

static void cui__sdl_event(cui_app *app, SDL_Event *e) {
    cui_ctx *ui = app->ui;
    switch (e->type) {
    case SDL_QUIT: app->running = false; break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_CLOSE) app->running = false;
        else if (e->window.event == SDL_WINDOWEVENT_LEAVE) cui_input_mouse_leave(ui);
        else if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) cui_input_focus(ui, true);
        else if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) cui_input_focus(ui, false);
        cui_request_redraw(ui);
        break;
    case SDL_MOUSEMOTION: cui_input_mouse_move(ui, (float)e->motion.x, (float)e->motion.y); break;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
        cui_mouse_button b = e->button.button == SDL_BUTTON_LEFT ? CUI_MOUSE_LEFT : e->button.button == SDL_BUTTON_RIGHT ? CUI_MOUSE_RIGHT : CUI_MOUSE_MIDDLE;
        cui_input_mouse_move(ui, (float)e->button.x, (float)e->button.y);
        cui_input_mouse_button(ui, b, e->type == SDL_MOUSEBUTTONDOWN);
        break;
    }
    case SDL_MOUSEWHEEL: {
        float dx = (float)e->wheel.x, dy = (float)e->wheel.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        dx = e->wheel.preciseX; dy = e->wheel.preciseY;
#endif
        cui_input_scroll(ui, -dx, dy);
        break;
    }
    case SDL_KEYDOWN: case SDL_KEYUP: {
        cui_key k = cui__sdl_key(e->key.keysym.sym);
        cui_input_key(ui, k, e->type == SDL_KEYDOWN, cui__sdl_mods(e->key.keysym.mod));
        break;
    }
    case SDL_TEXTINPUT: cui_input_text(ui, e->text.text); break;
    case SDL_DROPFILE: {
        /* SDL reports no position for drops: use the pointer */
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        const char *paths[1] = { e->drop.file };
        if (e->drop.file) cui_input_file_drop(ui, paths, 1, (float)mx, (float)my);
        SDL_free(e->drop.file);
        break;
    }
    default: break;
    }
}

void cui__plat_wait(cui_app *app, double timeout) {
    SDL_Event e;
    if (timeout != 0) {
        int ms = timeout < 0 ? -1 : (int)(timeout * 1000.0 + 0.5);
        if (ms < 0 ? SDL_WaitEvent(&e) : SDL_WaitEventTimeout(&e, ms)) cui__sdl_event(app, &e);
    }
    while (SDL_PollEvent(&e)) cui__sdl_event(app, &e);
}

bool cui__plat_present_part(cui_app *app, const void *bgra, int w, int h, int stride, int x, int y, int pw, int ph) {
    (void)app; (void)bgra; (void)w; (void)h; (void)stride; (void)x; (void)y; (void)pw; (void)ph;
    return false;   /* the whole buffer is shown each time */
}

void cui__plat_gl_release(cui_app *app) { (void)app; }   /* not used: these keep OpenGL */
