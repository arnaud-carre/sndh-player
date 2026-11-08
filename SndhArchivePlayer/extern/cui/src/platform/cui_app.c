/* The shared app loop: renderer selection, frame pacing, event driven
 * sleeping and screenshots. Backends supply windows and events. */
#include "cui_platform.h"

static cui_app *cui__current_app;
static void cui__app_set_clip(void *user, const char *utf8);
static const char *cui__app_get_clip(void *user);

static bool cui__env_is(const char *name, const char *value) {
    const char *v = getenv(name);
    return v && strcmp(v, value) == 0;
}

static void cui__app_make_software(cui_app *app) {
    if (!app->sw) app->sw = cui_sw_create();
    cui_sw_set_incremental(app->sw, true);   /* only what changed is drawn again */
    app->software = true;
    cui_renderer_iface iface = cui_sw_iface(app->sw);
    cui_set_renderer(app->ui, &iface);
}

cui_app *cui_app_create(const cui_app_desc *desc) {
    cui_app *app = calloc(1, sizeof *app);
    if (!app) return NULL;
    if (desc) app->desc = *desc;
    cui_app_desc *d = &app->desc;
    if (!d->title) d->title = "CUI";
    snprintf(app->title, sizeof app->title, "%s", d->title);
    d->title = app->title;
    if (d->width <= 0) d->width = 1080;
    if (d->height <= 0) d->height = 720;
    if (d->min_width <= 0) d->min_width = 360;
    if (d->min_height <= 0) d->min_height = 280;
    cui_theme_mode mode = d->theme;
    if (d->follow_system_theme) {
        int dark = cui__plat_system_dark();
        if (dark >= 0) mode = dark ? CUI_THEME_DARK : CUI_THEME_LIGHT;
    }
    /* CUI_THEME=dark|light and CUI_ACCENT=<name> override the app's choice
     * (handy for screenshots and for trying an app in the other theme) */
    if (cui__env_is("CUI_THEME", "dark")) mode = CUI_THEME_DARK;
    if (cui__env_is("CUI_THEME", "light")) mode = CUI_THEME_LIGHT;
    static const char *accents[CUI_ACCENT_COUNT] = { "neutral", "violet", "blue", "green", "amber", "orange", "coral", "rose" };
    for (int i = 0; i < CUI_ACCENT_COUNT; i++)
        if (cui__env_is("CUI_ACCENT", accents[i])) d->accent = (cui_accent)i;
    d->theme = mode;
    cui_ctx_desc cd;
    memset(&cd, 0, sizeof cd);
    cd.theme = mode;
    cd.accent = d->accent;
    app->ui = cui_create(&cd);
    if (!app->ui) { free(app); return NULL; }
    if (!cui__plat_init(app)) {
        fprintf(stderr, "cui: could not create a window (%s backend)\n", cui__plat_name());
        cui_destroy(app->ui);
        free(app);
        return NULL;
    }
    bool want_gl = d->renderer != CUI_RENDERER_SOFTWARE && !cui__env_is("CUI_RENDERER", "software");
    if (want_gl && cui__plat_gl_create(app, !d->no_vsync)) {
        app->gl = cui_gl_create(cui__plat_gl_proc);
        /* OpenGL drawn on the CPU (a virtual machine, a remote session) costs several times
         * what the software renderer does, as that one redraws only what changed. Where the
         * window can take a software picture as well, Auto goes software. */
        bool can_switch = !strcmp(cui__plat_name(), "x11") || !strcmp(cui__plat_name(), "win32");
        if (app->gl && cui_gl_is_software(app->gl) && d->renderer == CUI_RENDERER_AUTO && can_switch && !cui__env_is("CUI_RENDERER", "opengl")) {
            cui_gl_destroy(app->gl);
            app->gl = NULL;
            cui__plat_gl_release(app);
        }
    }
    if (app->gl) {
        cui_renderer_iface iface = cui_gl_iface(app->gl);
        cui_set_renderer(app->ui, &iface);
    } else {
        if (d->renderer == CUI_RENDERER_OPENGL) fprintf(stderr, "cui: OpenGL unavailable, using the software renderer\n");
        cui__app_make_software(app);
    }
    cui_platform_iface pi;
    memset(&pi, 0, sizeof pi);
    pi.user = app;
    /* backends may already have set clipboard/cursor callbacks */
    cui_ctx *ui = app->ui;
    if (!ui->platform.set_clipboard) { pi.set_clipboard = cui__app_set_clip; pi.get_clipboard = cui__app_get_clip; }
    else pi = ui->platform;
    cui_set_platform(ui, &pi);
    app->running = true;
    app->last_time = cui__plat_now();
    app->screenshot_path = getenv("CUI_SCREENSHOT");
    if (app->screenshot_path) {
        const char *f = getenv("CUI_SCREENSHOT_FRAMES");
        app->screenshot_frames = f ? atoi(f) : 90;
        d->continuous = true;
    }
    if (!d->hidden) cui__plat_show(app);
    cui__current_app = app;
    return app;
}

void cui_app_destroy(cui_app *app) {
    if (!app) return;
    if (cui__current_app == app) cui__current_app = NULL;
    if (app->in_frame) { cui_end_frame(app->ui); app->in_frame = false; }
    /* release textures while the GL context still exists */
    cui_set_renderer(app->ui, NULL);
    if (app->gl) cui_gl_destroy(app->gl);
    app->gl = NULL;
    /* close the window before the UI context goes away: the OS may still
     * deliver focus and size messages while the window is destroyed */
    cui__plat_shutdown(app);
    if (app->sw) cui_sw_destroy(app->sw);
    free(app->sw_pixels);
    cui_destroy(app->ui);
    free(app->clipboard);
    free(app);
}

cui_ctx *cui_app_ui(cui_app *app) { return app->ui; }
void cui_app_quit(cui_app *app) { app->running = false; }
bool cui_app_is_software(cui_app *app) { return app->software; }
void *cui_app_native_window(cui_app *app) { return cui__plat_native(app); }
const char *cui_app_backend(cui_app *app) { (void)app; return cui__plat_name(); }

void cui_app_set_title(cui_app *app, const char *title) {
    snprintf(app->title, sizeof app->title, "%s", title ? title : "");
    cui__plat_set_title(app, app->title);
}

void cui_app_set_size(cui_app *app, int w, int h) { cui__plat_set_size(app, w, h); }

cui_vec2 cui_app_size(cui_app *app) {
    float w, h, s;
    int fw, fh;
    cui__plat_metrics(app, &w, &h, &s, &fw, &fh);
    cui_vec2 v = { w, h };
    return v;
}

static void cui__app_set_clip(void *user, const char *utf8) { cui__app_set_clipboard_cache(user, utf8); }
static const char *cui__app_get_clip(void *user) { cui_app *app = user; return app->clipboard; }

void cui__app_set_clipboard_cache(cui_app *app, const char *utf8) {
    free(app->clipboard);
    app->clipboard = NULL;
    if (!utf8) return;
    size_t n = strlen(utf8);
    app->clipboard = malloc(n + 1);
    if (app->clipboard) memcpy(app->clipboard, utf8, n + 1);
}

void cui__app_render(cui_app *app) {
    if (app->in_frame) {
        cui_end_frame(app->ui);
        app->in_frame = false;
    }
    const cui_draw_data *dd = cui_get_draw_data(app->ui);
    int w = dd->width_px, h = dd->height_px;
    if (w <= 0 || h <= 0) return;
    if (app->gl) {
        cui_gl_render(app->gl, dd, true);
        cui__plat_gl_swap(app);
    } else {
        if (w != app->sw_w || h != app->sw_h) {
            free(app->sw_pixels);
            app->sw_pixels = malloc((size_t)w * (size_t)h * 4);
            app->sw_w = w; app->sw_h = h;
        }
        if (!app->sw_pixels) return;
        cui_sw_render(app->sw, dd, app->sw_pixels, w, h, w * 4, CUI_SW_BGRA);
        int dx, dy, dw, dh;
        bool changed = cui_sw_last_damage(app->sw, &dx, &dy, &dw, &dh);
        if (app->force_present || !app->presented_once || (changed && !cui__plat_present_part(app, app->sw_pixels, w, h, w * 4, dx, dy, dw, dh)))
            cui__plat_present(app, app->sw_pixels, w, h, w * 4);
        app->force_present = false;
    }
    app->presented_once = true;
    app->last_present = cui__plat_now();
}

static void cui__app_begin(cui_app *app) {
    float w, h, s;
    int fw, fh;
    cui__plat_metrics(app, &w, &h, &s, &fw, &fh);
    cui_set_viewport(app->ui, w, h, s);
    double now = cui__plat_now();
    float dt = (float)(now - app->last_time);
    app->last_time = now;
    if (dt <= 0) dt = 1.0f / 120.0f;
    cui_begin_frame(app->ui, dt);
    app->in_frame = true;
}

bool cui_app_frame(cui_app *app) {
    if (app->in_frame) {
        cui__app_render(app);
        if (app->screenshot_path && ++app->frames >= app->screenshot_frames) {
            const cui_draw_data *dd = cui_get_draw_data(app->ui);
            int w = dd->width_px, h = dd->height_px;
            unsigned char *px = malloc((size_t)w * (size_t)h * 4);
            if (px && cui_app_screenshot(app, px, w, h)) {
                bool ok = cui_write_png(app->screenshot_path, px, w, h, w * 4);
                fprintf(stderr, "cui: %s %s\n", ok ? "saved" : "could not save", app->screenshot_path);
            }
            free(px);
            app->running = false;
        }
    }
    if (!app->running) return false;
    cui_ctx *ui = app->ui;
    double timeout;
    if (app->desc.continuous || cui_needs_redraw(ui) || !app->presented_once) {
        timeout = 0;
        /* without vsync, pace animation frames to about 120 Hz */
        if (!app->gl || app->desc.no_vsync) {
            double since = cui__plat_now() - app->last_present;
            double target = 1.0 / 120.0;
            if (since < target) timeout = target - since;
        }
    } else {
        float wake = cui_next_wakeup(ui);
        timeout = wake >= 0 ? wake : -1;
    }
    cui__plat_wait(app, timeout);
    if (!app->running) return false;
    cui__app_begin(app);
    return true;
}

void cui__app_live_frame(cui_app *app) {
    /* called by backends while the OS runs a modal resize loop */
    if (!app->frame_cb || app->in_live_resize) {
        if (!app->in_frame) {
            const cui_draw_data *dd = cui_get_draw_data(app->ui);
            if (dd->vertex_count) cui__app_render(app);
        }
        return;
    }
    app->in_live_resize = true;
    if (app->in_frame) { cui_end_frame(app->ui); app->in_frame = false; }
    cui__app_begin(app);
    app->frame_cb(app->ui, app->frame_user);
    cui__app_render(app);
    app->in_live_resize = false;
}

int cui_app_run(const cui_app_desc *desc, void (*frame)(cui_ctx *ui, void *user), void *user) {
    cui_app *app = cui_app_create(desc);
    if (!app) return 1;
    app->frame_cb = frame;
    app->frame_user = user;
    while (cui_app_frame(app)) {
        if (frame) frame(app->ui, user);
    }
    cui_app_destroy(app);
    return 0;
}

bool cui_app_screenshot(cui_app *app, void *rgba, int width, int height) {
    const cui_draw_data *dd = cui_get_draw_data(app->ui);
    if (width < dd->width_px || height < dd->height_px || !rgba) return false;
    if (app->in_frame) cui__app_render(app);
    dd = cui_get_draw_data(app->ui);
    int w = dd->width_px, h = dd->height_px;
    if (app->gl) {
        /* re-render without swapping so the back buffer holds the frame */
        cui_gl_render(app->gl, dd, true);
        cui_gl_read_pixels(app->gl, w, h, rgba);
    } else {
        cui_sw_render(app->sw, dd, rgba, w, h, width * 4, CUI_SW_RGBA);
    }
    return true;
}

bool cui_app_file_dialog(cui_app *app, const cui_file_dialog_opts *opts, char *out, size_t cap) {
    cui_file_dialog_opts d;
    memset(&d, 0, sizeof d);
    if (!opts) opts = &d;
    if (!app) app = cui__current_app;
    if (!out || cap == 0 || !app) return false;
    out[0] = 0;
    bool ok = cui__plat_file_dialog(app, opts, out, cap);
    /* the dialog ran its own loop: input state is stale, and time jumped */
    cui_input_focus(app->ui, false);   /* releases keys, buttons and modifiers */
    cui_input_focus(app->ui, true);
    cui_input_mouse_leave(app->ui);
    app->last_time = cui__plat_now();
    cui_request_redraw(app->ui);
    return ok && out[0];
}

bool cui_app_open_url(const char *url) { return url && *url && cui__plat_open_url(url); }

