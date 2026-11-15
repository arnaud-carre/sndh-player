/*
 * CUI app layer: a window, an event loop and a renderer in a few lines.
 *
 * Native backends: Win32 (Windows), Cocoa (macOS), X11 (Linux/BSD).
 * Optional backends: SDL2 and GLFW (Wayland, consoles, anything SDL runs on).
 * Rendering uses OpenGL 3.3 when available and falls back to the built-in
 * software renderer, so apps also run in VMs and on remote desktops.
 *
 * Loop style:
 *
 *     cui_app *app = cui_app_create(&(cui_app_desc){ .title = "Notes" });
 *     while (cui_app_frame(app)) {
 *         cui_ctx *ui = cui_app_ui(app);
 *         ... components ...
 *     }
 *     cui_app_destroy(app);
 *
 * Callback style (also gives smooth live window resizing on Windows/macOS):
 *
 *     static void frame(cui_ctx *ui, void *user) { ... }
 *     int main(void) { return cui_app_run(&(cui_app_desc){ .title = "Notes" }, frame, NULL); }
 *
 * The loop is event driven: when nothing animates and no input arrives the
 * app sleeps and uses no CPU.
 */
#ifndef CUI_APP_H
#define CUI_APP_H

#include "cui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cui_app cui_app;

typedef enum cui_renderer_kind {
    CUI_RENDERER_AUTO = 0,     /* OpenGL, or software when there is no GPU (CPU-drawn OpenGL, X11 and Win32) */
    CUI_RENDERER_OPENGL,
    CUI_RENDERER_SOFTWARE
} cui_renderer_kind;

typedef struct cui_app_desc {
    const char *title;          /* default "CUI" */
    int width, height;          /* logical size, default 1080 x 720 */
    int min_width, min_height;  /* default 360 x 280 */
    bool fixed_size;            /* not resizable */
    bool continuous;            /* render every frame even when idle (games, video) */
    bool no_vsync;
    bool hidden;                /* create the window hidden (tests) */
    cui_renderer_kind renderer;
    cui_theme_mode theme;
    cui_accent accent;
    bool follow_system_theme;   /* use the OS light/dark setting where available */
} cui_app_desc;

CUI_API cui_app *cui_app_create(const cui_app_desc *desc);
CUI_API void cui_app_destroy(cui_app *app);

/* Presents the previous frame, waits for input or animation, then begins a
 * new frame. Returns false once the window was closed or cui_app_quit was
 * called. */
CUI_API bool cui_app_frame(cui_app *app);
CUI_API cui_ctx *cui_app_ui(cui_app *app);

/* Creates the app, calls frame() until the window closes, destroys it. */
CUI_API int cui_app_run(const cui_app_desc *desc, void (*frame)(cui_ctx *ui, void *user), void *user);

CUI_API void cui_app_quit(cui_app *app);
CUI_API void cui_app_set_title(cui_app *app, const char *title);
CUI_API void cui_app_set_size(cui_app *app, int width, int height);
CUI_API cui_vec2 cui_app_size(cui_app *app);
CUI_API bool cui_app_is_software(cui_app *app);
/* HWND, NSWindow*, X11 Window (cast to uintptr_t), SDL_Window* or GLFWwindow*. */
CUI_API void *cui_app_native_window(cui_app *app);
/* Name of the active backend: "win32", "cocoa", "x11", "sdl2", "glfw". */
CUI_API const char *cui_app_backend(cui_app *app);

/* Native file dialogs. Blocking; returns true and writes the chosen path
 * (UTF-8) to out when the user confirms. On Linux this uses zenity or
 * kdialog when installed. Pass NULL as app to use the current app (handy in
 * cui_app_run callbacks). */
typedef enum cui_file_dialog_kind { CUI_FILE_OPEN = 0, CUI_FILE_SAVE, CUI_FILE_FOLDER } cui_file_dialog_kind;
typedef struct cui_file_dialog_opts {
    cui_file_dialog_kind kind;
    const char *title;
    const char *filter_name;    /* e.g. "Images" */
    const char *filter;         /* extensions separated by ';', e.g. "png;jpg" */
    const char *default_name;   /* save dialogs */
} cui_file_dialog_opts;
CUI_API bool cui_app_file_dialog(cui_app *app, const cui_file_dialog_opts *opts, char *out, size_t cap);

/* Opens a URL or file with the system's default handler. */
CUI_API bool cui_app_open_url(const char *url);

/* Renders the current UI into an RGBA buffer (any backend). Handy for
 * screenshots and tests. Returns false when the buffer is too small. */
CUI_API bool cui_app_screenshot(cui_app *app, void *rgba, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* CUI_APP_H */
