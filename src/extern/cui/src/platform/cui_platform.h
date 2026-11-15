/* Interface between the shared app loop and a windowing backend. */
#ifndef CUI_PLATFORM_H
#define CUI_PLATFORM_H

#include "../cui_internal.h"
#include "cui/cui_app.h"
#include "cui/cui_render.h"

struct cui_app {
    cui_ctx *ui;
    cui_app_desc desc;
    char title[256];
    cui_gl_renderer *gl;
    cui_sw_renderer *sw;
    uint8_t *sw_pixels;
    int sw_w, sw_h;
    bool software;
    bool running;
    bool in_frame;
    bool presented_once;
    bool force_present;       /* software: the window needs the whole picture again (exposed, resized) */
    double last_time;
    double last_present;
    void (*frame_cb)(cui_ctx *ui, void *user);
    void *frame_user;
    bool in_live_resize;
    void *plat;                /* backend state */
    char *clipboard;           /* last clipboard string handed to CUI */
    const char *screenshot_path;  /* CUI_SCREENSHOT: save a PNG and quit */
    int screenshot_frames;
    int frames;
};

/* Implemented by each backend. */
bool cui__plat_init(cui_app *app);
void cui__plat_shutdown(cui_app *app);
bool cui__plat_gl_create(cui_app *app, bool vsync);
void *cui__plat_gl_proc(const char *name);
void cui__plat_gl_swap(cui_app *app);
void cui__plat_gl_release(cui_app *app);   /* drop the GL context: the window goes on with software pictures */
void cui__plat_present(cui_app *app, const void *bgra, int w, int h, int stride);
/* software: shows only the part (x, y, pw, ph) of the buffer that changed; false when the
 * backend cannot, and the whole buffer is shown instead */
bool cui__plat_present_part(cui_app *app, const void *bgra, int w, int h, int stride, int x, int y, int pw, int ph);
void cui__plat_wait(cui_app *app, double timeout);   /* < 0 waits for the next event */
void cui__plat_metrics(cui_app *app, float *w, float *h, float *scale, int *fb_w, int *fb_h);
void cui__plat_set_title(cui_app *app, const char *title);
void cui__plat_set_size(cui_app *app, int w, int h);
void cui__plat_show(cui_app *app);
void *cui__plat_native(cui_app *app);
const char *cui__plat_name(void);
double cui__plat_now(void);
int cui__plat_system_dark(void);                      /* -1 unknown, 0 light, 1 dark */
bool cui__plat_file_dialog(cui_app *app, const cui_file_dialog_opts *o, char *out, size_t cap);
bool cui__plat_open_url(const char *url);

/* Shared helpers for backends. */
void cui__app_render(cui_app *app);                   /* end frame (if open), render, present */
void cui__app_live_frame(cui_app *app);               /* run one frame from inside a resize loop */
void cui__app_set_clipboard_cache(cui_app *app, const char *utf8);

#endif
