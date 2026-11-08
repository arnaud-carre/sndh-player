/*
 * CUI: modern native UI components for C.
 *
 * One header for the whole component library. The windowing layer lives in
 * cui_app.h and the renderers in cui_render.h, so you can use CUI with the
 * built-in window backends or plug it into your own engine.
 *
 *     #include <cui/cui.h>
 *     #include <cui/cui_app.h>
 *
 *     int main(void) {
 *         cui_app *app = cui_app_create(&(cui_app_desc){ .title = "Hello" });
 *         bool on = false;
 *         while (cui_app_frame(app)) {
 *             cui_ctx *ui = cui_app_ui(app);
 *             cui_column(ui, .pad = 24, .gap = 12) {
 *                 cui_heading(ui, "Hello, CUI", 2);
 *                 cui_switch(ui, "Notifications", &on);
 *                 if (cui_button(ui, "Save", .variant = CUI_BUTTON_PRIMARY))
 *                     cui_toast(ui, "Saved", .tone = CUI_TONE_SUCCESS);
 *             }
 *         }
 *         cui_app_destroy(app);
 *     }
 *
 * Conventions
 *   - Immediate mode: call the component functions every frame. They return
 *     what happened (clicked, changed) and read and write your own variables.
 *   - Every component that takes options has a macro form with named
 *     arguments: cui_button(ui, "Save", .variant = CUI_BUTTON_PRIMARY).
 *     The *_ex function takes a pointer to the options struct (handy from C++).
 *   - Containers use a scope: cui_row(ui, .gap = 8) { ... }. Do not `return`
 *     or `break` out of a scope; use the explicit begin/end functions if you
 *     need early exits.
 *   - Units are logical points. CUI scales everything for high DPI screens.
 *   - Strings are UTF-8. "Label##key" shows "Label" but uses the whole
 *     string as the identity, so two buttons can share a visible label.
 *     "Label###key" uses only "###key", so the label can change (a count,
 *     a translation) while the widget keeps its state and focus.
 *
 * MIT License. See LICENSE.
 */
#ifndef CUI_H
#define CUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include "cui_icons.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CUI_VERSION_MAJOR 1
#define CUI_VERSION_MINOR 1
#define CUI_VERSION_PATCH 0
#define CUI_VERSION_STRING "1.1.0"

#ifndef CUI_API
#  if defined(_WIN32) && defined(CUI_SHARED) && defined(CUI_BUILDING)
#    define CUI_API __declspec(dllexport)
#  elif defined(_WIN32) && defined(CUI_SHARED)
#    define CUI_API __declspec(dllimport)
#  elif defined(__GNUC__) && defined(CUI_SHARED)
#    define CUI_API __attribute__((visibility("default")))
#  else
#    define CUI_API
#  endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define CUI_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#  define CUI_PRINTF(fmt, args)
#endif

/* ------------------------------------------------------------------------ */
/* Basic types                                                              */
/* ------------------------------------------------------------------------ */

typedef struct cui_ctx cui_ctx;
typedef uint32_t cui_id;

/* Compound literals in C, brace initialization in C++. */
#ifdef __cplusplus
#  define CUI__LIT(T) T
#else
#  define CUI__LIT(T) (T)
#endif

typedef struct cui_vec2 { float x, y; } cui_vec2;
typedef struct cui_rect { float x, y, w, h; } cui_rect;
typedef struct cui_color { uint8_t r, g, b, a; } cui_color;

#define CUI_RGB(hex)  (CUI__LIT(cui_color){ (uint8_t)(((hex) >> 16) & 0xff), (uint8_t)(((hex) >> 8) & 0xff), (uint8_t)((hex) & 0xff), 255 })
#define CUI_RGBA(hex) (CUI__LIT(cui_color){ (uint8_t)(((hex) >> 24) & 0xff), (uint8_t)(((hex) >> 16) & 0xff), (uint8_t)(((hex) >> 8) & 0xff), (uint8_t)((hex) & 0xff) })
#define CUI_TRANSPARENT (CUI__LIT(cui_color){ 0, 0, 0, 0 })

/* Semantic colors. Use these instead of raw colors so your UI follows the
 * theme (light/dark) and the accent. They can go anywhere a cui_color goes. */
typedef enum cui_token {
    CUI_TOKEN_NONE = 0,
    CUI_TOKEN_BACKGROUND, CUI_TOKEN_SURFACE, CUI_TOKEN_SURFACE_RAISED, CUI_TOKEN_SURFACE_MUTED,
    CUI_TOKEN_FOREGROUND, CUI_TOKEN_TEXT_SECONDARY, CUI_TOKEN_TEXT_MUTED,
    CUI_TOKEN_BORDER, CUI_TOKEN_BORDER_SUBTLE, CUI_TOKEN_BORDER_STRONG,
    CUI_TOKEN_ACCENT, CUI_TOKEN_ACCENT_STRONG, CUI_TOKEN_ACCENT_SUBTLE, CUI_TOKEN_ACCENT_FOREGROUND,
    CUI_TOKEN_SUCCESS, CUI_TOKEN_WARNING, CUI_TOKEN_DANGER, CUI_TOKEN_INFO,
    CUI_TOKEN_CONTROL_ON, CUI_TOKEN_CONTROL_GLYPH, CUI_TOKEN_CONTROL_TRACK,
    CUI_TOKEN_CONTROL_TRACK_HOVER, CUI_TOKEN_CONTROL_THUMB, CUI_TOKEN_CONTROL_THUMB_ON,
    CUI_TOKEN_HOVER, CUI_TOKEN_ACTIVE, CUI_TOKEN_SCRIM, CUI_TOKEN_FOCUS_RING,
    CUI_TOKEN_SERIES_1, CUI_TOKEN_SERIES_2, CUI_TOKEN_SERIES_3, CUI_TOKEN_SERIES_4,
    CUI_TOKEN_SERIES_5, CUI_TOKEN_SERIES_6,
    CUI_TOKEN_COUNT
} cui_token;

#define CUI_TOKEN_COLOR(t) (CUI__LIT(cui_color){ (uint8_t)(t), 0xC5, 0x1D, 0 })
#define CUI_COLOR_BACKGROUND      CUI_TOKEN_COLOR(CUI_TOKEN_BACKGROUND)
#define CUI_COLOR_SURFACE         CUI_TOKEN_COLOR(CUI_TOKEN_SURFACE)
#define CUI_COLOR_SURFACE_RAISED  CUI_TOKEN_COLOR(CUI_TOKEN_SURFACE_RAISED)
#define CUI_COLOR_SURFACE_MUTED   CUI_TOKEN_COLOR(CUI_TOKEN_SURFACE_MUTED)
#define CUI_COLOR_FOREGROUND      CUI_TOKEN_COLOR(CUI_TOKEN_FOREGROUND)
#define CUI_COLOR_TEXT_SECONDARY  CUI_TOKEN_COLOR(CUI_TOKEN_TEXT_SECONDARY)
#define CUI_COLOR_TEXT_MUTED      CUI_TOKEN_COLOR(CUI_TOKEN_TEXT_MUTED)
#define CUI_COLOR_BORDER          CUI_TOKEN_COLOR(CUI_TOKEN_BORDER)
#define CUI_COLOR_BORDER_SUBTLE   CUI_TOKEN_COLOR(CUI_TOKEN_BORDER_SUBTLE)
#define CUI_COLOR_BORDER_STRONG   CUI_TOKEN_COLOR(CUI_TOKEN_BORDER_STRONG)
#define CUI_COLOR_ACCENT          CUI_TOKEN_COLOR(CUI_TOKEN_ACCENT)
#define CUI_COLOR_ACCENT_STRONG   CUI_TOKEN_COLOR(CUI_TOKEN_ACCENT_STRONG)
#define CUI_COLOR_ACCENT_SUBTLE   CUI_TOKEN_COLOR(CUI_TOKEN_ACCENT_SUBTLE)
#define CUI_COLOR_ACCENT_FOREGROUND CUI_TOKEN_COLOR(CUI_TOKEN_ACCENT_FOREGROUND)
#define CUI_COLOR_SUCCESS         CUI_TOKEN_COLOR(CUI_TOKEN_SUCCESS)
#define CUI_COLOR_WARNING         CUI_TOKEN_COLOR(CUI_TOKEN_WARNING)
#define CUI_COLOR_DANGER          CUI_TOKEN_COLOR(CUI_TOKEN_DANGER)
#define CUI_COLOR_INFO            CUI_TOKEN_COLOR(CUI_TOKEN_INFO)
#define CUI_COLOR_HOVER           CUI_TOKEN_COLOR(CUI_TOKEN_HOVER)
#define CUI_COLOR_SCRIM           CUI_TOKEN_COLOR(CUI_TOKEN_SCRIM)
#define CUI_COLOR_SERIES(n)       CUI_TOKEN_COLOR(CUI_TOKEN_SERIES_1 + ((n) % 6))

/* ------------------------------------------------------------------------ */
/* Context and frame                                                        */
/* ------------------------------------------------------------------------ */

typedef enum cui_theme_mode { CUI_THEME_LIGHT = 0, CUI_THEME_DARK = 1 } cui_theme_mode;

typedef enum cui_accent {
    CUI_ACCENT_NEUTRAL = 0, CUI_ACCENT_VIOLET, CUI_ACCENT_BLUE, CUI_ACCENT_GREEN,
    CUI_ACCENT_AMBER, CUI_ACCENT_ORANGE, CUI_ACCENT_CORAL, CUI_ACCENT_ROSE,
    CUI_ACCENT_COUNT
} cui_accent;

typedef enum cui_texture_format { CUI_TEXTURE_R8 = 1, CUI_TEXTURE_RGBA8 = 4 } cui_texture_format;
typedef uintptr_t cui_texture;

/* How CUI talks to a renderer. The built-in renderers fill this in for you
 * (cui_render_gl_iface / cui_render_sw_iface). Implement it to use CUI with
 * any graphics API. */
typedef struct cui_renderer_iface {
    void *user;
    cui_texture (*texture_create)(void *user, int w, int h, cui_texture_format fmt, const void *pixels);
    void (*texture_update)(void *user, cui_texture tex, int x, int y, int w, int h, const void *pixels, int stride);
    void (*texture_destroy)(void *user, cui_texture tex);
} cui_renderer_iface;

typedef struct cui_allocator {
    void *(*alloc)(size_t size, void *user);
    void *(*realloc)(void *ptr, size_t size, void *user);
    void (*free)(void *ptr, void *user);
    void *user;
} cui_allocator;

typedef struct cui_ctx_desc {
    cui_renderer_iface renderer;      /* required for drawing; may be set later */
    cui_allocator allocator;          /* optional, defaults to malloc/free */
    cui_theme_mode theme;
    cui_accent accent;
    bool no_default_fonts;            /* skip the embedded Inter/Geist/Lucide fonts */
} cui_ctx_desc;

CUI_API cui_ctx *cui_create(const cui_ctx_desc *desc);
CUI_API void cui_destroy(cui_ctx *ui);
CUI_API void cui_set_renderer(cui_ctx *ui, const cui_renderer_iface *renderer);

/* Size of the drawable area in logical points, and the scale to pixels
 * (2.0 on a typical retina display). */
CUI_API void cui_set_viewport(cui_ctx *ui, float width, float height, float scale);
CUI_API cui_vec2 cui_viewport_size(cui_ctx *ui);
CUI_API float cui_dpi_scale(cui_ctx *ui);

/* dt is the time since the previous frame in seconds. */
CUI_API void cui_begin_frame(cui_ctx *ui, float dt);
CUI_API void cui_end_frame(cui_ctx *ui);

/* True while something animates or input is still settling. Event driven
 * apps can sleep until the next input event when this is false. */
CUI_API bool cui_needs_redraw(cui_ctx *ui);
/* Seconds until CUI wants a frame (for caret blink, tooltips, toasts).
 * Negative means "only on input". */
CUI_API float cui_next_wakeup(cui_ctx *ui);
CUI_API void cui_request_redraw(cui_ctx *ui);
/* Asks for a frame within this many seconds even with no input, for things
 * that change on a timer (a clock, polling, a countdown). Call it each frame
 * while it is needed; an idle app then sleeps until the soonest one instead
 * of redrawing continuously. */
CUI_API void cui_request_wakeup(cui_ctx *ui, float seconds);
CUI_API double cui_time(cui_ctx *ui);
CUI_API float cui_delta_time(cui_ctx *ui);   /* seconds since the last frame */
CUI_API uint64_t cui_frame_count(cui_ctx *ui);

/* ------------------------------------------------------------------------ */
/* Input (called by the platform layer)                                     */
/* ------------------------------------------------------------------------ */

typedef enum cui_mouse_button { CUI_MOUSE_LEFT = 0, CUI_MOUSE_RIGHT = 1, CUI_MOUSE_MIDDLE = 2 } cui_mouse_button;

typedef enum cui_key {
    CUI_KEY_NONE = 0,
    CUI_KEY_TAB, CUI_KEY_ENTER, CUI_KEY_ESCAPE, CUI_KEY_BACKSPACE, CUI_KEY_DELETE, CUI_KEY_SPACE,
    CUI_KEY_LEFT, CUI_KEY_RIGHT, CUI_KEY_UP, CUI_KEY_DOWN,
    CUI_KEY_HOME, CUI_KEY_END, CUI_KEY_PAGE_UP, CUI_KEY_PAGE_DOWN,
    CUI_KEY_A, CUI_KEY_B, CUI_KEY_C, CUI_KEY_D, CUI_KEY_E, CUI_KEY_F, CUI_KEY_G, CUI_KEY_H, CUI_KEY_I,
    CUI_KEY_J, CUI_KEY_K, CUI_KEY_L, CUI_KEY_M, CUI_KEY_N, CUI_KEY_O, CUI_KEY_P, CUI_KEY_Q, CUI_KEY_R,
    CUI_KEY_S, CUI_KEY_T, CUI_KEY_U, CUI_KEY_V, CUI_KEY_W, CUI_KEY_X, CUI_KEY_Y, CUI_KEY_Z,
    CUI_KEY_0, CUI_KEY_1, CUI_KEY_2, CUI_KEY_3, CUI_KEY_4, CUI_KEY_5, CUI_KEY_6, CUI_KEY_7, CUI_KEY_8, CUI_KEY_9,
    CUI_KEY_MINUS, CUI_KEY_EQUAL, CUI_KEY_COMMA, CUI_KEY_PERIOD, CUI_KEY_SLASH,
    CUI_KEY_F1, CUI_KEY_F2, CUI_KEY_F3, CUI_KEY_F4, CUI_KEY_F5, CUI_KEY_F6,
    CUI_KEY_F7, CUI_KEY_F8, CUI_KEY_F9, CUI_KEY_F10, CUI_KEY_F11, CUI_KEY_F12,
    CUI_KEY_COUNT
} cui_key;

typedef enum cui_mod {
    CUI_MOD_SHIFT = 1 << 0,
    CUI_MOD_CTRL  = 1 << 1,
    CUI_MOD_ALT   = 1 << 2,
    CUI_MOD_SUPER = 1 << 3   /* Command on macOS, Windows key elsewhere */
} cui_mod;

/* The "primary" shortcut modifier: Command on macOS, Ctrl elsewhere. */
#if defined(__APPLE__)
#  define CUI_MOD_PRIMARY CUI_MOD_SUPER
#else
#  define CUI_MOD_PRIMARY CUI_MOD_CTRL
#endif

typedef enum cui_cursor {
    CUI_CURSOR_ARROW = 0, CUI_CURSOR_HAND, CUI_CURSOR_TEXT, CUI_CURSOR_RESIZE_EW,
    CUI_CURSOR_RESIZE_NS, CUI_CURSOR_MOVE, CUI_CURSOR_NOT_ALLOWED, CUI_CURSOR_COUNT
} cui_cursor;

CUI_API void cui_input_mouse_move(cui_ctx *ui, float x, float y);
CUI_API void cui_input_mouse_button(cui_ctx *ui, cui_mouse_button button, bool down);
CUI_API void cui_input_mouse_leave(cui_ctx *ui);
CUI_API void cui_input_scroll(cui_ctx *ui, float dx, float dy);  /* in lines; +dy scrolls up */
CUI_API void cui_input_key(cui_ctx *ui, cui_key key, bool down, int mods);
CUI_API void cui_input_text(cui_ctx *ui, const char *utf8);
CUI_API void cui_input_focus(cui_ctx *ui, bool window_focused);
/* Files dragged from the OS: hover while a drag is over the window (where the
 * platform reports it), then the drop itself. Paths are UTF-8 and copied. */
CUI_API void cui_input_file_hover(cui_ctx *ui, bool hovering, float x, float y);
CUI_API void cui_input_file_drop(cui_ctx *ui, const char *const *paths, int count, float x, float y);
/* Files dropped this frame (dropzones claim theirs first). */
CUI_API int cui_dropped_file_count(cui_ctx *ui);
CUI_API const char *cui_dropped_file(cui_ctx *ui, int index);
CUI_API bool cui_file_drag_active(cui_ctx *ui);   /* files are being dragged over the window */

/* Platform services. The built-in backends set these. */
typedef struct cui_platform_iface {
    void *user;
    void (*set_clipboard)(void *user, const char *utf8);
    const char *(*get_clipboard)(void *user);   /* returned string stays valid until next call */
    void (*set_cursor)(void *user, cui_cursor cursor);
    void (*set_text_input_rect)(void *user, cui_rect caret);  /* IME candidate window position */
    /* Optional: draws one emoji (UTF-8) into a px by px RGBA buffer (straight alpha), for
     * when no emoji font is loaded; the browser build uses the browser's emoji. */
    bool (*draw_emoji)(void *user, const char *utf8, int len, int px, unsigned char *rgba);
} cui_platform_iface;
CUI_API void cui_set_platform(cui_ctx *ui, const cui_platform_iface *platform);
CUI_API cui_cursor cui_get_cursor(cui_ctx *ui);

/* Queries, useful for app level shortcuts. */
CUI_API bool cui_key_pressed(cui_ctx *ui, cui_key key);          /* pressed this frame (with repeat) */
CUI_API bool cui_key_down(cui_ctx *ui, cui_key key);
CUI_API bool cui_shortcut(cui_ctx *ui, int mods, cui_key key);   /* e.g. cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_K) */
CUI_API int cui_key_mods(cui_ctx *ui);
CUI_API cui_vec2 cui_mouse_pos(cui_ctx *ui);
CUI_API bool cui_mouse_down(cui_ctx *ui, cui_mouse_button b);
CUI_API bool cui_mouse_clicked(cui_ctx *ui, cui_mouse_button b);
/* True when a text field has keyboard focus (so the app should not treat
 * typing as shortcuts). */
CUI_API bool cui_wants_keyboard(cui_ctx *ui);
CUI_API bool cui_wants_mouse(cui_ctx *ui);

/* ------------------------------------------------------------------------ */
/* Theme                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct cui_shadow { float y, blur, spread; float alpha; } cui_shadow;

typedef struct cui_theme {
    cui_color colors[CUI_TOKEN_COUNT];
    cui_color tone_sky, tone_teal, tone_green, tone_amber, tone_rose, tone_orchid, tone_violet;

    float radius_control;   /* buttons, inputs                (default 10) */
    float radius_panel;     /* menus, popovers, nested panels (default 14) */
    float radius_surface;   /* cards, dialogs                 (default 18) */

    float control_sm, control_md, control_lg;   /* control heights (30/36/44) */
    float text_xs, text_sm, text_body, text_lg; /* 11/12/13/15 */
    float heading[4];                           /* h1..h4: 36/28/22/17 */

    cui_shadow shadow_resting[2], shadow_raised[2], shadow_floating[2];

    float motion;           /* 1 = normal, 0 = reduced motion (no travel), >1 slow motion */
    float font_scale;       /* user text scale multiplier (default 1) */
} cui_theme;

CUI_API void cui_set_theme(cui_ctx *ui, cui_theme_mode mode);
CUI_API cui_theme_mode cui_get_theme_mode(cui_ctx *ui);
CUI_API void cui_set_accent(cui_ctx *ui, cui_accent accent);
CUI_API cui_accent cui_get_accent(cui_ctx *ui);
/* The active theme. Edit it to customize; changes apply next frame. Theme
 * changes through cui_set_theme/cui_set_accent animate smoothly. */
CUI_API cui_theme *cui_theme_get(cui_ctx *ui);
CUI_API void cui_set_reduced_motion(cui_ctx *ui, bool reduced);
CUI_API cui_color cui_color_of(cui_ctx *ui, cui_color c);   /* resolves tokens */
CUI_API cui_color cui_token_color(cui_ctx *ui, cui_token token);
CUI_API const char *cui_accent_name(cui_accent accent);

/* Color helpers. Mixing is done in OKLab, like CSS color-mix(in oklab). */
CUI_API cui_color cui_oklch(float l, float c, float h_degrees, float alpha);
CUI_API cui_color cui_mix(cui_color a, cui_color b, float t);
CUI_API cui_color cui_alpha(cui_color c, float alpha_multiplier);

/* ------------------------------------------------------------------------ */
/* Fonts                                                                    */
/* ------------------------------------------------------------------------ */

typedef int cui_font;
enum {
    CUI_FONT_BODY = 0,     /* Inter Regular  */
    CUI_FONT_MEDIUM = 1,   /* Inter Medium   */
    CUI_FONT_DISPLAY = 2,  /* Geist Medium, for headings 28pt and up */
    CUI_FONT_MONO = 3,     /* Geist Mono     */
    CUI_FONT_ICONS = 4     /* Lucide icons   */
};

/* Load a TrueType/OpenType font. The data must stay alive if copy is false.
 * Returns a font id, or -1 on failure. */
CUI_API cui_font cui_font_load_memory(cui_ctx *ui, const void *data, size_t size, bool copy);
CUI_API cui_font cui_font_load_file(cui_ctx *ui, const char *path);
/* Replace one of the built-in roles (e.g. use your brand font as CUI_FONT_BODY). */
CUI_API void cui_font_set_role(cui_ctx *ui, cui_font role, cui_font font);
/* Glyphs missing from `font` are looked up in `fallback` (e.g. CJK, emoji). */
CUI_API void cui_font_add_fallback(cui_ctx *ui, cui_font font, cui_font fallback);

typedef struct cui_text_metrics { float width, height, ascent, descent, line_height; } cui_text_metrics;
/* CUI_TEXT_SHAPING optionally shapes static Arabic-script text and resolves
 * bidi paragraphs from their first strong character. Editing, explicit bidi
 * formatting and general OpenType equivalence are unsupported; see docs/integration.md. */
CUI_API cui_text_metrics cui_measure_text(cui_ctx *ui, cui_font font, float size, const char *text, int len);

/* ------------------------------------------------------------------------ */
/* Layout                                                                   */
/* ------------------------------------------------------------------------ */

typedef enum cui_size_kind { CUI_SIZE_FIT = 0, CUI_SIZE_GROW, CUI_SIZE_FIXED, CUI_SIZE_PERCENT } cui_size_kind;
typedef struct cui_size { cui_size_kind kind; float value; } cui_size;

#define CUI_FIT       (CUI__LIT(cui_size){ CUI_SIZE_FIT, 0 })
#define CUI_FILL      (CUI__LIT(cui_size){ CUI_SIZE_GROW, 1 })
#define CUI_GROW(w)   (CUI__LIT(cui_size){ CUI_SIZE_GROW, (float)(w) })
#define CUI_PX(v)     (CUI__LIT(cui_size){ CUI_SIZE_FIXED, (float)(v) })
#define CUI_PCT(p)    (CUI__LIT(cui_size){ CUI_SIZE_PERCENT, (float)(p) })   /* 0..100 of parent */

typedef enum cui_dir { CUI_COLUMN = 0, CUI_ROW, CUI_STACK, CUI_GRID } cui_dir;
/* CUI_ALIGN_DEFAULT: columns stretch their children, rows center them. */
typedef enum cui_align { CUI_ALIGN_DEFAULT = 0, CUI_ALIGN_START, CUI_ALIGN_CENTER, CUI_ALIGN_END, CUI_ALIGN_STRETCH } cui_align;
typedef enum cui_justify {
    CUI_JUSTIFY_START = 0, CUI_JUSTIFY_CENTER, CUI_JUSTIFY_END, CUI_JUSTIFY_BETWEEN, CUI_JUSTIFY_AROUND
} cui_justify;

typedef enum cui_elevation { CUI_ELEVATION_NONE = 0, CUI_ELEVATION_RESTING, CUI_ELEVATION_RAISED, CUI_ELEVATION_FLOATING } cui_elevation;

/* Options for every container. All fields are optional. */
typedef struct cui_box {
    int _;
    const char *id;            /* identity for scroll state etc. (defaults to an automatic id) */
    cui_dir dir;               /* CUI_COLUMN (default), CUI_ROW, CUI_STACK, CUI_GRID */
    cui_size width, height;    /* default: fit content (columns stretch children horizontally) */
    float min_width, max_width, min_height, max_height;
    float pad, pad_x, pad_y, pad_top, pad_right, pad_bottom, pad_left;
    float gap;                 /* space between children (and between wrapped lines) */
    int columns;               /* CUI_GRID: column count (with min_column_width: the most columns) */
    float min_column_width;    /* CUI_GRID: as many columns as fit at this width, at least one */
    bool wrap;                 /* CUI_ROW: children that do not fit move to a new line */
    cui_align align;           /* cross axis alignment of children */
    cui_justify justify;       /* main axis distribution */
    cui_color bg;              /* background */
    cui_color border_color;    /* defaults to CUI_COLOR_BORDER when border > 0 */
    float border;              /* border width */
    float radius;              /* corner radius; use radius_tl.. for single corners */
    float radius_tl, radius_tr, radius_br, radius_bl;
    cui_elevation elevation;
    bool scroll_x, scroll_y;   /* make the box a scroll area (clips content) */
    bool clip;                 /* clip children to the box */
    float opacity;             /* 0 means 1 (fully opaque) */
} cui_box;

CUI_API cui_id cui_box_begin(cui_ctx *ui, const cui_box *opts);
CUI_API void cui_box_end(cui_ctx *ui);
CUI_API cui_id cui_row_begin(cui_ctx *ui, const cui_box *opts);
CUI_API cui_id cui_column_begin(cui_ctx *ui, const cui_box *opts);
CUI_API void cui_spacer(cui_ctx *ui);                         /* flexible space */
CUI_API void cui_gap(cui_ctx *ui, float size);                /* fixed space */
CUI_API void cui_separator(cui_ctx *ui);                      /* hairline, follows the parent direction */

/* Card: a bordered surface with generous padding. */
typedef struct cui_card_opts {
    int _;
    const char *id;
    cui_size width, height;
    float min_width, max_width;  /* e.g. CUI_FILL up to max_width: fills narrow screens, stops on wide ones */
    float pad, pad_x, pad_y, gap;
    cui_dir dir;
    cui_align align;
    bool wrap;                 /* CUI_ROW cards: children that do not fit move to a new line */
    bool hoverable;            /* lifts slightly on hover and returns clicks */
    bool muted;                /* muted surface, no border */
    bool selected;             /* accent border */
} cui_card_opts;
CUI_API bool cui_card_begin(cui_ctx *ui, const cui_card_opts *opts);   /* returns true when clicked (hoverable) */
CUI_API void cui_card_end(cui_ctx *ui);

/* Scroll area with smooth scrolling and overlay scrollbars. */
typedef struct cui_scroll_opts {
    int _;
    const char *id;
    cui_size width, height;
    bool horizontal;            /* also scroll horizontally */
    float pad, gap;
    bool fade_edges;            /* content fades out at an edge with more to scroll */
    cui_color fade_color;       /* what it fades into (default: the nearest opaque box around it) */
} cui_scroll_opts;
CUI_API void cui_scroll_begin(cui_ctx *ui, const cui_scroll_opts *opts);
CUI_API void cui_scroll_end(cui_ctx *ui);
CUI_API void cui_scroll_to(cui_ctx *ui, const char *scroll_id, float y);

/* Resizable split view: two panes with a draggable divider. `ratio` is the
 * size of the first pane in 0..1. Call cui_split_next between the panes. */
typedef struct cui_split_opts { int _; bool vertical; float min_first, min_second; cui_size width, height; } cui_split_opts;
CUI_API void cui_split_begin(cui_ctx *ui, const char *id, float *ratio, const cui_split_opts *opts);
CUI_API void cui_split_next(cui_ctx *ui);
CUI_API void cui_split_end(cui_ctx *ui);

/* ------------------------------------------------------------------------ */
/* Identity                                                                 */
/* ------------------------------------------------------------------------ */

CUI_API cui_id cui_make_id(cui_ctx *ui, const char *str);
CUI_API void cui_push_id(cui_ctx *ui, const char *str);
CUI_API void cui_push_id_int(cui_ctx *ui, int value);
CUI_API void cui_pop_id(cui_ctx *ui);
/* The id and rectangle of the most recent component (for tooltips, popovers). */
CUI_API cui_id cui_last_id(cui_ctx *ui);
CUI_API cui_rect cui_last_rect(cui_ctx *ui);
/* Rectangle of any component or box id from the latest layout. */
CUI_API cui_rect cui_get_rect(cui_ctx *ui, cui_id id);
CUI_API bool cui_last_hovered(cui_ctx *ui);
CUI_API bool cui_is_disabled(cui_ctx *ui);
CUI_API void cui_push_disabled(cui_ctx *ui, bool disabled);
CUI_API void cui_pop_disabled(cui_ctx *ui);

/* ------------------------------------------------------------------------ */
/* Text                                                                     */
/* ------------------------------------------------------------------------ */

typedef enum cui_text_align { CUI_TEXT_LEFT = 0, CUI_TEXT_CENTER, CUI_TEXT_RIGHT } cui_text_align;

typedef struct cui_text_opts {
    int _;
    cui_font font;             /* default CUI_FONT_BODY (0) */
    float size;                /* default theme text_body */
    cui_color color;           /* default foreground */
    cui_text_align align;
    bool nowrap;               /* single line, ellipsis when it does not fit */
    bool tabular;              /* tabular numbers */
    bool selectable;           /* reserved */
    const char *tooltip;       /* optional passive tooltip on the text */
    cui_size width;
    float line_height;         /* multiplier, default 1.45 */
} cui_text_opts;

CUI_API void cui_text_ex(cui_ctx *ui, const char *text, const cui_text_opts *opts);
CUI_API void cui_textf(cui_ctx *ui, const char *fmt, ...) CUI_PRINTF(2, 3);
CUI_API void cui_heading(cui_ctx *ui, const char *text, int level);          /* 1..4 */
CUI_API void cui_label(cui_ctx *ui, const char *text);                       /* medium weight */
CUI_API void cui_secondary(cui_ctx *ui, const char *text);                   /* secondary color */
CUI_API void cui_muted(cui_ctx *ui, const char *text);                       /* small, muted */
CUI_API void cui_code(cui_ctx *ui, const char *text);                        /* inline code */
CUI_API void cui_kbd(cui_ctx *ui, const char *keys);                         /* keyboard keycap, e.g. "Ctrl K" */
CUI_API bool cui_link(cui_ctx *ui, const char *text);
CUI_API void cui_icon(cui_ctx *ui, const char *icon, float size, cui_color color);

/* ------------------------------------------------------------------------ */
/* Buttons                                                                  */
/* ------------------------------------------------------------------------ */

typedef enum cui_button_variant {
    CUI_BUTTON_DEFAULT = 0,    /* secondary for buttons, ghost for icon buttons */
    CUI_BUTTON_SECONDARY,      /* neutral filled */
    CUI_BUTTON_PRIMARY,        /* the one main action */
    CUI_BUTTON_OUTLINE,
    CUI_BUTTON_GHOST,
    CUI_BUTTON_DANGER,
    CUI_BUTTON_LINK
} cui_button_variant;

typedef enum cui_control_size { CUI_SIZE_MD = 0, CUI_SIZE_SM, CUI_SIZE_LG } cui_control_size;

typedef struct cui_button_opts {
    int _;
    cui_button_variant variant;
    cui_control_size size;
    const char *icon;          /* leading icon, e.g. CUI_ICON_PLUS */
    const char *icon_right;    /* trailing icon */
    bool disabled;
    bool loading;              /* spinner, ignores clicks */
    bool full_width;
    bool selected;             /* toggled look */
    const char *tooltip;
    const char *shortcut;      /* shown as a hint in the tooltip, e.g. "Ctrl S" */
    cui_size width;
    cui_color color;           /* text and icon color, e.g. CUI_COLOR_ACCENT for a toggled icon */
} cui_button_opts;

CUI_API bool cui_button_ex(cui_ctx *ui, const char *label, const cui_button_opts *opts);
/* Square/round button with only an icon. Give it a tooltip so it has a name. */
CUI_API bool cui_icon_button_ex(cui_ctx *ui, const char *icon, const cui_button_opts *opts);

/* Segmented control: a row of options with a gliding highlight. */
typedef struct cui_segmented_opts { int _; cui_control_size size; bool full_width; const char *const *icons; } cui_segmented_opts;
CUI_API bool cui_segmented_ex(cui_ctx *ui, const char *id, const char *const *items, int count, int *index, const cui_segmented_opts *opts);

/* ------------------------------------------------------------------------ */
/* Selection controls                                                       */
/* ------------------------------------------------------------------------ */

typedef struct cui_check_opts { int _; const char *description; bool disabled; bool indeterminate; } cui_check_opts;
CUI_API bool cui_checkbox_ex(cui_ctx *ui, const char *label, bool *value, const cui_check_opts *opts);
CUI_API bool cui_switch_ex(cui_ctx *ui, const char *label, bool *value, const cui_check_opts *opts);
CUI_API bool cui_radio_ex(cui_ctx *ui, const char *label, int *value, int option, const cui_check_opts *opts);
CUI_API bool cui_radio_group(cui_ctx *ui, const char *id, const char *const *items, int count, int *index);

typedef struct cui_slider_opts {
    int _;
    float step;                /* 0 = continuous */
    bool show_value;
    const char *format;        /* printf format for the value, default "%.0f" */
    bool disabled;
    cui_size width;
} cui_slider_opts;
/* Elastic slider: the track stretches like rubber when dragged past its ends. */
CUI_API bool cui_slider_ex(cui_ctx *ui, const char *label, float *value, float min, float max, const cui_slider_opts *opts);
CUI_API bool cui_range_slider(cui_ctx *ui, const char *label, float *lo, float *hi, float min, float max);
CUI_API bool cui_rating(cui_ctx *ui, const char *id, int *value, int max);

typedef struct cui_chip_opts { int _; const char *icon; bool disabled; } cui_chip_opts;
CUI_API bool cui_chip_ex(cui_ctx *ui, const char *label, bool *selected, const cui_chip_opts *opts);

/* ------------------------------------------------------------------------ */
/* Text input                                                               */
/* ------------------------------------------------------------------------ */

enum { CUI_CHANGED = 1, CUI_SUBMITTED = 2, CUI_FOCUSED = 4, CUI_BLURRED = 8 };

typedef struct cui_input_opts {
    int _;
    const char *label;         /* label above the field */
    const char *placeholder;
    const char *icon;          /* leading icon */
    const char *hint;          /* helper text below */
    const char *error;         /* error text below (red) */
    bool password;
    bool disabled;
    bool clearable;            /* x button when not empty */
    bool autofocus;
    bool read_only;
    cui_control_size size;
    cui_size width;
    float height;              /* textarea: height in points; negative fills the space */
    const char *prefix;        /* muted text before the value, e.g. "$" or "https://" */
    const char *suffix;        /* muted text after the value, e.g. "USD" or "kg" */
} cui_input_opts;

/* Single line text field. Returns CUI_CHANGED / CUI_SUBMITTED (Enter) flags. */
CUI_API int cui_input_ex(cui_ctx *ui, const char *id, char *buf, size_t cap, const cui_input_opts *opts);
/* Multi-line text area with word wrap. */
CUI_API int cui_textarea_ex(cui_ctx *ui, const char *id, char *buf, size_t cap, const cui_input_opts *opts);
/* Search field with a search icon and a clear button. */
CUI_API int cui_search_ex(cui_ctx *ui, const char *id, char *buf, size_t cap, const cui_input_opts *opts);

typedef struct cui_number_opts { int _; const char *label; double step; int decimals; bool disabled; cui_size width; const char *suffix; } cui_number_opts;
CUI_API bool cui_number_ex(cui_ctx *ui, const char *id, double *value, double min, double max, const cui_number_opts *opts);

/* ------------------------------------------------------------------------ */
/* Select, combobox, date picker                                            */
/* ------------------------------------------------------------------------ */

typedef struct cui_select_opts {
    int _;
    const char *label;
    const char *placeholder;
    const char *const *icons;
    bool searchable;           /* combobox: filter options by typing */
    bool disabled;
    cui_size width;
} cui_select_opts;
CUI_API bool cui_select_ex(cui_ctx *ui, const char *id, const char *const *items, int count, int *index, const cui_select_opts *opts);

typedef struct cui_date { int year, month, day; } cui_date;   /* month 1..12 */
CUI_API cui_date cui_date_today(void);
CUI_API bool cui_calendar(cui_ctx *ui, const char *id, cui_date *value);
typedef struct cui_date_opts { int _; const char *label; const char *placeholder; cui_size width; } cui_date_opts;
CUI_API bool cui_date_picker_ex(cui_ctx *ui, const char *id, cui_date *value, const cui_date_opts *opts);

CUI_API bool cui_color_picker(cui_ctx *ui, const char *id, cui_color *value);

/* ------------------------------------------------------------------------ */
/* Navigation                                                               */
/* ------------------------------------------------------------------------ */

typedef enum cui_tabs_variant { CUI_TABS_UNDERLINE = 0, CUI_TABS_PILL } cui_tabs_variant;
typedef struct cui_tabs_opts { int _; cui_tabs_variant variant; const char *const *icons; bool full_width; } cui_tabs_opts;
CUI_API bool cui_tabs_ex(cui_ctx *ui, const char *id, const char *const *items, int count, int *index, const cui_tabs_opts *opts);

/* Sidebar navigation item with a gliding selection highlight shared by all
 * items in the same group. */
typedef struct cui_nav_opts { int _; const char *icon; const char *badge; bool selected; const char *group; } cui_nav_opts;
CUI_API bool cui_nav_item_ex(cui_ctx *ui, const char *label, const cui_nav_opts *opts);
CUI_API void cui_nav_section(cui_ctx *ui, const char *title);

CUI_API int cui_breadcrumb(cui_ctx *ui, const char *const *items, int count);   /* returns clicked index or -1 */
CUI_API bool cui_pagination(cui_ctx *ui, const char *id, int *page, int page_count);  /* page is 0 based */
CUI_API void cui_stepper(cui_ctx *ui, const char *const *steps, int count, int current);

/* ------------------------------------------------------------------------ */
/* Menus and popovers                                                       */
/* ------------------------------------------------------------------------ */

typedef enum cui_placement {
    CUI_PLACE_BOTTOM_START = 0, CUI_PLACE_BOTTOM, CUI_PLACE_BOTTOM_END,
    CUI_PLACE_TOP_START, CUI_PLACE_TOP, CUI_PLACE_TOP_END,
    CUI_PLACE_RIGHT_START, CUI_PLACE_LEFT_START, CUI_PLACE_RIGHT, CUI_PLACE_LEFT
} cui_placement;

/* Popover anchored to the previous component. Toggle it with
 * cui_popover_toggle / cui_popover_open. */
typedef struct cui_popover_opts { int _; cui_placement placement; float width; float pad; float gap; } cui_popover_opts;
CUI_API bool cui_popover_begin(cui_ctx *ui, const char *id, const cui_popover_opts *opts);
CUI_API void cui_popover_end(cui_ctx *ui);

/* Guided tour: a spotlight on one control at a time, with a card beside it
 * that says what the control does, for onboarding and "what's new". The
 * rest of the window dims; the spotlight glides from one control to the
 * next. Call it after the controls it points at, every frame while *step is
 * 0 or more; it sets *step to -1 when the tour ends. Right or Enter go to the
 * next step, Left goes back and Esc ends the tour.
 *
 *     static int step = 0;
 *     cui_button(ui, "Export");  steps[1].target = cui_last_id(ui);
 *     ...
 *     if (step >= 0) cui_tour(ui, "intro", steps, 4, &step);
 */
typedef struct cui_tour_step {
    cui_id target;             /* the control to point at (cui_last_id after it); 0: a card in the middle */
    const char *title;
    const char *body;
    cui_placement placement;   /* the card's side of the target, default below it */
} cui_tour_step;
typedef struct cui_tour_opts {
    int _;
    float width;               /* the card, default 300 */
    float padding;             /* room around the target inside the spotlight, default 6 */
    bool block_target;         /* the spotlighted control ignores the pointer too */
    const char *next_label, *back_label, *done_label;   /* default "Next", "Back", "Done" */
    const char *count_format;  /* the step count, default "%d of %d" (step, then total) */
} cui_tour_opts;
enum { CUI_TOUR_NONE = 0, CUI_TOUR_NEXT, CUI_TOUR_BACK, CUI_TOUR_DONE, CUI_TOUR_SKIPPED };
/* What happened this frame: one of CUI_TOUR_*. */
CUI_API int cui_tour_ex(cui_ctx *ui, const char *id, const cui_tour_step *steps, int count, int *step, const cui_tour_opts *opts);
CUI_API void cui_popover_open(cui_ctx *ui, const char *id);
CUI_API void cui_popover_close(cui_ctx *ui, const char *id);
CUI_API void cui_popover_toggle(cui_ctx *ui, const char *id);
CUI_API bool cui_popover_is_open(cui_ctx *ui, const char *id);

/* Dropdown menu: a button that opens a menu. Items return true when chosen. */
typedef struct cui_menu_opts { int _; const char *icon; cui_button_variant variant; cui_placement placement; float width; bool icon_only; } cui_menu_opts;
CUI_API bool cui_menu_begin(cui_ctx *ui, const char *label, const cui_menu_opts *opts);
CUI_API void cui_menu_end(cui_ctx *ui);
/* Context menu: opens on right click over the previous component. */
CUI_API bool cui_context_menu_begin(cui_ctx *ui, const char *id);
CUI_API void cui_context_menu_end(cui_ctx *ui);

typedef struct cui_menu_item_opts { int _; const char *icon; const char *shortcut; bool danger; bool disabled; bool checked; const char *tooltip; } cui_menu_item_opts;
CUI_API bool cui_menu_item_ex(cui_ctx *ui, const char *label, const cui_menu_item_opts *opts);
CUI_API void cui_menu_separator(cui_ctx *ui);
CUI_API void cui_menu_label(cui_ctx *ui, const char *text);

/* Tooltip for the previous component, shown after a short hover delay. */
CUI_API void cui_tooltip(cui_ctx *ui, const char *text);

/* ------------------------------------------------------------------------ */
/* Dialogs, sheets, toasts, alerts                                          */
/* ------------------------------------------------------------------------ */

typedef struct cui_dialog_opts {
    int _;
    const char *description;
    float width;               /* default 440 */
    bool no_close_button;
    bool persistent;           /* clicking outside or Esc does not close */
    bool center_title;         /* center the title, balancing the close button */
} cui_dialog_opts;
/* Modal dialog. Shown while *open is true; closing sets it to false. */
CUI_API bool cui_dialog_begin(cui_ctx *ui, const char *title, bool *open, const cui_dialog_opts *opts);
CUI_API void cui_dialog_end(cui_ctx *ui);
/* A right-aligned row for dialog actions. */
CUI_API void cui_dialog_footer_begin(cui_ctx *ui);
CUI_API void cui_dialog_footer_end(cui_ctx *ui);

typedef enum cui_side { CUI_SIDE_RIGHT = 0, CUI_SIDE_LEFT, CUI_SIDE_BOTTOM, CUI_SIDE_TOP } cui_side;
typedef struct cui_sheet_opts { int _; cui_side side; float size; const char *description; } cui_sheet_opts;
CUI_API bool cui_sheet_begin(cui_ctx *ui, const char *title, bool *open, const cui_sheet_opts *opts);
CUI_API void cui_sheet_end(cui_ctx *ui);

typedef enum cui_tone {
    CUI_TONE_NEUTRAL = 0, CUI_TONE_INFO, CUI_TONE_SUCCESS, CUI_TONE_WARNING, CUI_TONE_DANGER,
    CUI_TONE_VIOLET, CUI_TONE_TEAL, CUI_TONE_ROSE, CUI_TONE_ACCENT
} cui_tone;

typedef struct cui_toast_opts {
    int _;
    const char *description;
    cui_tone tone;
    float duration;            /* seconds, default 4; negative = until dismissed */
    const char *action;        /* optional action button label */
    bool show_timer;           /* a draining line shows how long the action stays (undo) */
} cui_toast_opts;
/* Queue a toast. Returns a handle; cui_toast_action_clicked(handle) tells
 * whether its action button was pressed. */
CUI_API int cui_toast_ex(cui_ctx *ui, const char *title, const cui_toast_opts *opts);
CUI_API bool cui_toast_action_clicked(cui_ctx *ui, int handle);
CUI_API void cui_toast_dismiss(cui_ctx *ui, int handle);

typedef struct cui_alert_opts { int _; const char *description; cui_tone tone; const char *icon; bool dismissible; } cui_alert_opts;
/* Inline alert. Returns false once dismissed (when dismissible). */
CUI_API bool cui_alert_ex(cui_ctx *ui, const char *title, const cui_alert_opts *opts);

/* ------------------------------------------------------------------------ */
/* Feedback and display                                                     */
/* ------------------------------------------------------------------------ */

typedef struct cui_progress_opts { int _; bool indeterminate; cui_tone tone; float height; const char *label; bool show_value; cui_size width; } cui_progress_opts;
CUI_API void cui_progress_ex(cui_ctx *ui, float value, const cui_progress_opts *opts);
CUI_API void cui_spinner(cui_ctx *ui, float size);
CUI_API void cui_skeleton(cui_ctx *ui, cui_size width, float height);

typedef struct cui_badge_opts { int _; cui_tone tone; bool dot; bool solid; const char *icon; cui_control_size size; } cui_badge_opts;
CUI_API void cui_badge_ex(cui_ctx *ui, const char *text, const cui_badge_opts *opts);

typedef struct cui_avatar_opts { int _; float size; cui_texture image; const char *status; bool square; } cui_avatar_opts;
CUI_API void cui_avatar_ex(cui_ctx *ui, const char *name, const cui_avatar_opts *opts);
CUI_API void cui_avatar_group(cui_ctx *ui, const char *const *names, int count, int max_visible, float size);

typedef struct cui_empty_opts { int _; const char *icon; const char *description; const char *action; } cui_empty_opts;
/* Empty state. Returns true when its action button is clicked. */
CUI_API bool cui_empty_state_ex(cui_ctx *ui, const char *title, const cui_empty_opts *opts);

typedef struct cui_metric_opts {
    int _; float delta; const char *delta_label; const float *spark; int spark_count; const char *icon; cui_size width;
    bool lower_is_better;       /* build times, errors, latency: a fall is good news, shown in green */
} cui_metric_opts;
CUI_API void cui_metric_ex(cui_ctx *ui, const char *label, const char *value, const cui_metric_opts *opts);

CUI_API void cui_meter(cui_ctx *ui, const char *label, float used, float total, const char *unit);

/* ------------------------------------------------------------------------ */
/* Data display                                                             */
/* ------------------------------------------------------------------------ */

/* Accordion / disclosure. Content animates its height. */
CUI_API bool cui_accordion_begin(cui_ctx *ui, const char *title, bool *open);
CUI_API void cui_accordion_end(cui_ctx *ui);

/* Tree view node. */
typedef struct cui_tree_opts { int _; const char *icon; bool leaf; bool selected; } cui_tree_opts;
CUI_API bool cui_tree_begin(cui_ctx *ui, const char *label, bool *open, const cui_tree_opts *opts);
CUI_API void cui_tree_end(cui_ctx *ui);
CUI_API bool cui_tree_clicked(cui_ctx *ui);   /* the node created by the last cui_tree_begin was clicked */

/* List item: a selectable row with optional icon, subtitle and trailing text. */
typedef struct cui_list_item_opts { int _; const char *icon; const char *subtitle; const char *trailing; bool selected; const char *avatar; } cui_list_item_opts;
CUI_API bool cui_list_item_ex(cui_ctx *ui, const char *title, const cui_list_item_opts *opts);

/* Virtual list: only the visible rows are built, so lists with a million
 * rows stay fast. Fills first/last with the visible range. */
CUI_API void cui_virtual_list_begin(cui_ctx *ui, const char *id, int count, float row_height, cui_size height, int *first, int *last);
CUI_API void cui_virtual_list_end(cui_ctx *ui);

/* Table */
typedef struct cui_table_column {
    const char *title;
    cui_size width;            /* default fill */
    cui_text_align align;
    bool sortable;
} cui_table_column;

typedef struct cui_table_sort { int column; bool descending; } cui_table_sort;

typedef struct cui_table_opts {
    int _;
    cui_table_sort *sort;      /* optional, updated when a sortable header is clicked */
    cui_size height;           /* scrolls when set */
    bool striped;
    bool compact;
    bool selectable;
} cui_table_opts;
CUI_API bool cui_table_begin_ex(cui_ctx *ui, const char *id, const cui_table_column *columns, int column_count, const cui_table_opts *opts);
/* Begin a row. Returns true when the row is clicked. */
CUI_API bool cui_table_row(cui_ctx *ui, bool selected);
/* Next cell; put any components inside, or use cui_table_cell_text. */
CUI_API void cui_table_cell(cui_ctx *ui);
CUI_API void cui_table_cell_text(cui_ctx *ui, const char *text);
CUI_API void cui_table_cell_textf(cui_ctx *ui, const char *fmt, ...) CUI_PRINTF(2, 3);
CUI_API void cui_table_end(cui_ctx *ui);

/* Charts */
typedef struct cui_series {
    const char *name;
    const float *values;
    int count;
    cui_color color;           /* default: series palette */
} cui_series;

typedef struct cui_chart_opts {
    int _;
    cui_size width;
    float height;              /* default 220 */
    const char *const *labels; /* x axis labels */
    int label_count;
    bool area;                 /* fill under lines */
    bool no_grid;
    bool no_legend;
    bool stacked;              /* bar chart */
    bool smooth;               /* curved lines */
    const char *format;        /* value format for axis and tooltip, default "%.0f" */
    float min, max;            /* fixed y range when min != max */
} cui_chart_opts;
CUI_API void cui_line_chart_ex(cui_ctx *ui, const char *id, const cui_series *series, int count, const cui_chart_opts *opts);
CUI_API void cui_bar_chart_ex(cui_ctx *ui, const char *id, const cui_series *series, int count, const cui_chart_opts *opts);
CUI_API void cui_donut_chart_ex(cui_ctx *ui, const char *id, const float *values, const char *const *labels, int count, const cui_chart_opts *opts);
CUI_API void cui_sparkline(cui_ctx *ui, const float *values, int count, cui_size width, float height, cui_color color);
CUI_API void cui_gauge(cui_ctx *ui, const char *label, float value, float size);

/* Images */
CUI_API cui_texture cui_image_create(cui_ctx *ui, int w, int h, const void *rgba);
CUI_API cui_texture cui_image_load(cui_ctx *ui, const char *path, int *w, int *h);   /* PNG/JPEG via stb_image */
CUI_API void cui_image_destroy(cui_ctx *ui, cui_texture tex);
typedef struct cui_image_opts { int _; cui_size width, height; float radius; cui_color tint; } cui_image_opts;
CUI_API void cui_image_ex(cui_ctx *ui, cui_texture tex, const cui_image_opts *opts);

/* Image viewer: zoom and pan a picture. The wheel (or a pinch, which comes
 * as Ctrl+wheel) zooms around the pointer, dragging pans, double-click
 * switches between fitting the view and 100%. With the viewer focused, + and
 * - zoom, 0 fits, 1 shows actual pixels and the arrows pan. Zoom and pan ease.
 * Keep a cui_image_view to keep the zoom and position (zeroed: fit). */
typedef struct cui_image_view {
    float zoom;                /* screen points per image pixel; 0 fits the image in the view */
    float cx, cy;              /* the image point at the middle of the view, in pixels */
} cui_image_view;
typedef struct cui_image_viewer_opts {
    int _;
    cui_size width, height;    /* default the width it gets and 360 */
    float max_zoom;            /* default 16 (1600%) */
    bool no_toolbar;           /* the zoom buttons and percentage in the corner */
    bool checkerboard;         /* a grid behind transparent images */
} cui_image_viewer_opts;
/* Returns true when the view changed. */
CUI_API bool cui_image_viewer_ex(cui_ctx *ui, const char *id, cui_texture tex, int image_w, int image_h, cui_image_view *view, const cui_image_viewer_opts *opts);

/* Cropper: a crop rectangle over a picture, in image pixels. Drag inside to
 * move it, drag a corner or an edge to resize it; the outside dims and thirds
 * lines show while you drag. With it focused, the arrows move the crop (Shift
 * for 10 pixels). A zeroed crop starts as the largest centered rectangle of
 * the aspect. */
typedef struct cui_cropper_opts {
    int _;
    cui_size width, height;    /* default the width it gets and 360 */
    float aspect;              /* width / height to keep, 0 for free */
    bool round;                /* a round mask, for avatars (keeps a 1:1 aspect) */
    float min_size;            /* smallest side, image pixels; default 16 */
} cui_cropper_opts;
/* Returns true when the crop changed. */
CUI_API bool cui_cropper_ex(cui_ctx *ui, const char *id, cui_texture tex, int image_w, int image_h, cui_rect *crop, const cui_cropper_opts *opts);

/* ------------------------------------------------------------------------ */
/* Command palette (block)                                                  */
/* ------------------------------------------------------------------------ */

typedef struct cui_command {
    const char *title;
    const char *icon;
    const char *group;
    const char *shortcut;
} cui_command;
/* Spotlight style command palette. Returns the index of the chosen command,
 * or -1. Open it with a shortcut: if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_K)) open = true; */
CUI_API int cui_command_palette(cui_ctx *ui, bool *open, const cui_command *commands, int count);

/* ------------------------------------------------------------------------ */
/* More components                                                          */
/* ------------------------------------------------------------------------ */

/* Radio cards: larger selectable cards for choices that need a description
 * (plans, templates). */
typedef struct cui_choice { const char *title; const char *description; const char *icon; const char *badge; } cui_choice;
typedef struct cui_radio_cards_opts { int _; int columns; } cui_radio_cards_opts;
CUI_API bool cui_radio_cards_ex(cui_ctx *ui, const char *id, const cui_choice *choices, int count, int *index, const cui_radio_cards_opts *opts);

/* Tag input: tags are stored comma separated in buf. Enter or comma adds the
 * typed tag, Backspace on an empty field removes the last one. */
typedef struct cui_tag_opts { int _; const char *label; const char *placeholder; int max_tags; cui_size width; } cui_tag_opts;
CUI_API bool cui_tag_input_ex(cui_ctx *ui, const char *id, char *buf, size_t cap, const cui_tag_opts *opts);

/* One time code input: `length` boxes; code must hold length + 1 bytes.
 * Returns true when all boxes are filled (once per change). */
CUI_API bool cui_otp_input(cui_ctx *ui, const char *id, char *code, int length);

/* Connected buttons. Put buttons between begin and end. */
CUI_API void cui_button_group_begin(cui_ctx *ui);
CUI_API void cui_button_group_end(cui_ctx *ui);

/* Icon button that copies text to the clipboard and confirms with a check. */
CUI_API bool cui_copy_button(cui_ctx *ui, const char *id, const char *text);

/* Hold to confirm: a destructive action that fills while held and fires when
 * full. Returns true once. */
typedef struct cui_hold_opts { int _; float duration; const char *icon; cui_button_variant variant; } cui_hold_opts;
CUI_API bool cui_hold_button_ex(cui_ctx *ui, const char *label, const cui_hold_opts *opts);

/* Password strength meter (0 weak .. 4 strong). Returns the score. */
CUI_API int cui_password_strength(cui_ctx *ui, const char *password);

/* Activity timeline item. */
typedef struct cui_timeline_opts { int _; const char *time; const char *description; const char *icon; cui_tone tone; bool last; } cui_timeline_opts;
CUI_API void cui_timeline_item_ex(cui_ctx *ui, const char *title, const cui_timeline_opts *opts);

/* Reorderable list: drag rows to reorder. `order` holds your item indices in
 * display order and is rewritten when a row is dropped. Build the rows in
 * display order: for (i...) cui_reorder_item(ui, i) { draw item order[i] }.
 * Returns true on the frame the order changes. */
CUI_API void cui_reorder_begin(cui_ctx *ui, const char *id, int *order, int count);
CUI_API bool cui_reorder_end(cui_ctx *ui);
CUI_API void cui_reorder_item_begin(cui_ctx *ui, int position);
CUI_API void cui_reorder_item_end(cui_ctx *ui);

/* Circular progress with the value in the middle. */
CUI_API void cui_progress_ring(cui_ctx *ui, const char *label, float value, float size);

/* ------------------------------------------------------------------------ */
/* More actions                                                             */
/* ------------------------------------------------------------------------ */

/* Split button: a main action with a menu of related actions on the chevron.
 * Returns 0 when the main part is clicked, 1 + i when menu item i is chosen
 * and -1 otherwise. */
typedef struct cui_split_button_opts {
    int _;
    cui_button_variant variant;   /* default primary */
    cui_control_size size;
    const char *icon;
    const char *const *item_icons;
    bool disabled;
} cui_split_button_opts;
CUI_API int cui_split_button_ex(cui_ctx *ui, const char *label, const char *const *items, int count, const cui_split_button_opts *opts);

/* Toggle group: a row of toggles (bold, italic...). values[i] is toggled
 * when item i is clicked; with exclusive set exactly one stays on. Labels
 * become tooltips when icon_only is set. Returns true when a value changed. */
typedef struct cui_toggle_group_opts { int _; const char *const *icons; bool exclusive; bool icon_only; cui_control_size size; } cui_toggle_group_opts;
CUI_API bool cui_toggle_group_ex(cui_ctx *ui, const char *id, const char *const *labels, int count, bool *values, const cui_toggle_group_opts *opts);

/* Two step destructive button: the first click morphs it into a confirm
 * state with a countdown, the second click confirms. Returns true once. */
typedef struct cui_confirm_opts {
    int _;
    const char *confirm_label;    /* default "Click to confirm" */
    const char *icon;
    cui_button_variant variant;   /* resting look, default secondary */
    cui_control_size size;
    float timeout;                /* seconds before it disarms, default 3 */
} cui_confirm_opts;
CUI_API bool cui_confirm_button_ex(cui_ctx *ui, const char *label, const cui_confirm_opts *opts);

/* Button that shows the result of the work it started: idle, a spinner while
 * busy, then a check (or an error) that settles back. Width morphs smoothly
 * between the states. Returns true when clicked while idle. */
typedef enum cui_status { CUI_STATUS_IDLE = 0, CUI_STATUS_BUSY, CUI_STATUS_SUCCESS, CUI_STATUS_ERROR } cui_status;
typedef struct cui_status_button_opts {
    int _;
    const char *busy_label;       /* default label + "..." */
    const char *success_label;    /* default "Done" */
    const char *error_label;      /* default "Failed" */
    const char *icon;
    cui_button_variant variant;   /* default primary */
    cui_control_size size;
} cui_status_button_opts;
CUI_API bool cui_status_button_ex(cui_ctx *ui, const char *label, cui_status status, const cui_status_button_opts *opts);

/* Reactions: icon pills with counts. Clicking toggles `active` and adjusts
 * the count. Returns the index that changed, or -1. */
typedef struct cui_reaction { const char *icon; int count; bool active; } cui_reaction;
CUI_API int cui_reactions(cui_ctx *ui, const char *id, cui_reaction *items, int count);

/* Speed dial: a round floating action button that fans out related actions.
 * Returns the chosen action index, or -1. */
typedef struct cui_speed_action { const char *icon; const char *label; } cui_speed_action;
typedef struct cui_speed_dial_opts { int _; const char *icon; cui_placement placement; const char *tooltip; } cui_speed_dial_opts;
CUI_API int cui_speed_dial_ex(cui_ctx *ui, const char *id, const cui_speed_action *actions, int count, const cui_speed_dial_opts *opts);

/* Theme switcher: light, dark and (optionally) system, as a compact icon
 * segmented control. mode is 0 light, 1 dark, 2 system. Light and dark are
 * applied for you; for system call cui_set_theme with the OS setting.
 * Returns true when mode changed. */
CUI_API bool cui_theme_switcher(cui_ctx *ui, int *mode, bool with_system);
/* Sun and moon icon button that toggles the theme with a morph. */
CUI_API bool cui_theme_toggle(cui_ctx *ui);

/* User menu: an avatar, a name and a line of detail that opens a menu of
 * account actions. Put menu items between begin and end. */
typedef struct cui_user_menu_opts { int _; const char *detail; const char *status; cui_texture avatar; bool compact; cui_placement placement; float width; } cui_user_menu_opts;
CUI_API bool cui_user_menu_begin(cui_ctx *ui, const char *name, const cui_user_menu_opts *opts);
CUI_API void cui_user_menu_end(cui_ctx *ui);

/* Swipe actions: drag a row sideways (or swipe on a trackpad) to reveal
 * actions behind it. A long swipe triggers the first action. The row content
 * goes between begin and end; end returns the chosen action: 0..left_count-1
 * for left actions, 100 + i for right actions, or -1. */
typedef struct cui_swipe_action { const char *icon; const char *label; cui_tone tone; } cui_swipe_action;
CUI_API void cui_swipe_item_begin(cui_ctx *ui, const char *id, const cui_swipe_action *left, int left_count,
                                  const cui_swipe_action *right, int right_count);
CUI_API int cui_swipe_item_end(cui_ctx *ui);

/* ------------------------------------------------------------------------ */
/* Data grid                                                                */
/* ------------------------------------------------------------------------ */

/* A virtualized table for large data: sticky header, resizable and sortable
 * columns, checkbox selection (click, Ctrl+click, Shift+click, Ctrl+A),
 * keyboard navigation and editable cells. Build only the rows in
 * [first, last):
 *
 *     int first, last;
 *     if (cui_data_grid_begin(ui, "people", cols, 4, count, &first, &last, .selected = sel)) {
 *         for (int r = first; r < last; r++) {
 *             cui_data_grid_row(ui, r);
 *             cui_data_grid_cell_text(ui, people[r].name);
 *             ...
 *         }
 *         cui_data_grid_end(ui);
 *     }
 */
typedef struct cui_grid_column {
    const char *title;
    float width;               /* initial width in points, default 150 */
    cui_text_align align;
    bool sortable;
    bool fixed;                /* not resizable */
    int priority;              /* 0 always shown; when the grid is too narrow, columns with a
                                * priority hide, the highest number first, until the rest fit */
} cui_grid_column;
typedef struct cui_grid_opts {
    int _;
    cui_size width, height;    /* default fill */
    float row_height;          /* default 40 */
    bool *selected;            /* one bool per row; adds a checkbox column */
    cui_table_sort *sort;
    bool proportional_columns; /* grid column widths are relative weights, recomputed on resize */
    bool striped;
    int *cursor;               /* the keyboard row, optional; setting it scrolls that row into view */
    bool hide_checkboxes;      /* with selected: rows still select (click, Shift, Ctrl) but no checkbox column */
    int *anchor;               /* the row Shift ranges start from, optional (in and out like cursor) */
} cui_grid_opts;
CUI_API bool cui_data_grid_begin_ex(cui_ctx *ui, const char *id, const cui_grid_column *columns, int column_count, int row_count,
                                    int *first, int *last, const cui_grid_opts *opts);
CUI_API bool cui_data_grid_row(cui_ctx *ui, int row);   /* true when the row was clicked */
CUI_API void cui_data_grid_cell(cui_ctx *ui);           /* next cell; put components inside */
CUI_API void cui_data_grid_cell_text(cui_ctx *ui, const char *text);
/* Editable text cell: double click (or Enter on the cursor row) to edit,
 * Enter or clicking away commits, Esc reverts. True when committed. */
CUI_API bool cui_data_grid_cell_edit(cui_ctx *ui, char *buf, size_t cap);
/* The same editable text, inside the current cell (after an avatar, say). */
CUI_API bool cui_data_grid_edit(cui_ctx *ui, char *buf, size_t cap);
CUI_API void cui_data_grid_end(cui_ctx *ui);

/* Tree cell: indentation, a disclosure chevron and a label, for tree tables
 * (use it in a table or grid cell). Returns true when *open toggled. */
CUI_API bool cui_tree_cell(cui_ctx *ui, const char *label, int depth, bool has_children, bool *open, const char *icon);

/* Filter bar: a search field, a chip per active filter (click it to pick
 * values) and "+ Filter" to add one. selected[i] is a bit mask of the
 * chosen options of fields[i]. Returns true when anything changed. */
typedef struct cui_filter_field { const char *name; const char *icon; const char *const *options; int option_count; } cui_filter_field;
CUI_API bool cui_filter_bar(cui_ctx *ui, const char *id, const cui_filter_field *fields, int count, uint32_t *selected,
                            char *search, size_t search_cap);

/* ---- Tree table and path bar ---- */

/* Tree table: an expandable tree with columns (a file explorer, a scene
 * graph, debugger variables), built on the data grid. You own the nodes: a
 * flat array linked by index, where children may arrive later (lazy). The
 * table flattens the open part of the tree every frame and builds only the
 * rows in view, so 100k nodes cost what a screenful does.
 *
 *     cui_tree_table_begin(ui, "files", cols, 4, nodes, count, .sort = &sort, .cursor = &cursor);
 *     int n;
 *     while ((n = cui_tree_table_row(ui)) >= 0) {
 *         cui_tree_table_name(ui, files[n].name, files[n].icon);
 *         cui_data_grid_cell_text(ui, files[n].size);
 *     }
 *     cui_tree_table_events ev = cui_tree_table_end(ui);
 *     for (int k = 0; k < ev.load_count; k++) start_loading(ev.load[k]);
 *
 * Keyboard: Up and Down move, Right opens a node or moves to its first
 * child, Left closes it or moves to its parent, Home and End, Enter
 * activates, * opens every sibling, typing jumps to a name (with .label).
 * Selection is the data grid's: click, Shift for a range, Ctrl to toggle,
 * Ctrl+A, Esc. Columns hide by cui_grid_column.priority when narrow. */
enum {
    CUI_TREE_HAS_CHILDREN = 1 << 0,   /* shows a chevron before its children are loaded */
    CUI_TREE_OPEN         = 1 << 1,   /* expanded (the table toggles it) */
    CUI_TREE_SELECTED     = 1 << 2,   /* the table sets and clears it */
    CUI_TREE_LOADING      = 1 << 3    /* children pending: an open node shows a spinner row; clear it when loaded */
};
typedef struct cui_tree_node {
    int parent;                /* -1 for a top level node */
    int first_child;           /* -1 when it has none, or none loaded yet */
    int next_sibling;          /* -1 for the last one */
    uint32_t flags;            /* CUI_TREE_* */
} cui_tree_node;
typedef struct cui_tree_table_opts {
    int _;
    cui_size width, height;    /* default fill */
    float row_height;          /* default 32 */
    int root;                  /* the first top level node, default 0 */
    cui_table_sort *sort;      /* sortable headers set it; sort each level yourself (cui_tree_sort) */
    int *cursor;               /* the keyboard node, optional (in and out) */
    bool guides;               /* quiet vertical lines for each indentation level */
    const char *(*label)(void *user, int node);   /* names for type-ahead, optional */
    void *user;
} cui_tree_table_opts;
typedef struct cui_tree_table_events {
    int activated;             /* node chosen with Enter or a double click, else -1 */
    int expanded, collapsed;   /* a node opened or closed this frame, else -1 */
    const int *load;           /* nodes to load children for (each now has CUI_TREE_LOADING), valid this frame */
    int load_count;
    bool selection_changed;
    bool sort_changed;         /* re-sort every sibling list by *sort */
} cui_tree_table_events;
CUI_API void cui_tree_table_begin_ex(cui_ctx *ui, const char *id, const cui_grid_column *columns, int column_count,
                                     cui_tree_node *nodes, int node_count, const cui_tree_table_opts *opts);
/* The next row in view: returns its node, or -1 when done. Then build its
 * cells: cui_tree_table_name first, then cui_data_grid_cell(_text). */
CUI_API int cui_tree_table_row(cui_ctx *ui);
/* The first cell: indentation, guides, the chevron, an icon and the name
 * (cut short with a tooltip of the whole name when it does not fit). */
CUI_API void cui_tree_table_name(cui_ctx *ui, const char *label, const char *icon);
CUI_API int cui_tree_table_depth(cui_ctx *ui);   /* depth of the current row, 0 at the top */
CUI_API cui_tree_table_events cui_tree_table_end(cui_ctx *ui);
/* Sorts every sibling list (stable) by cmp, relinking the nodes. Returns
 * the new first top level node. */
CUI_API int cui_tree_sort(cui_tree_node *nodes, int node_count, int root, int (*cmp)(void *user, int a, int b), void *user);

/* Path bar: a breadcrumb for file and scene paths. Segments are clickable,
 * the last one is the current place. Middle segments that do not fit
 * collapse into a "..." menu. With .menus, the chevron after segment k opens
 * a menu you fill with the children of segment k (cui_path_bar_menu_begin).
 * Click the empty part (or the current segment), or press Ctrl+L, to type a
 * path: Enter submits it, Esc or clicking away cancels. */
typedef struct cui_path_bar_opts {
    int _;
    const char *icon;          /* before the first segment */
    const char *text;          /* what editing starts with, default the segments joined by "/" */
    const char *placeholder;
    cui_control_size size;
    cui_size width;            /* default fill */
    bool menus;                /* separators open a menu of the next segment's siblings */
    bool no_edit;              /* no text mode */
} cui_path_bar_opts;
typedef struct cui_path_bar_result {
    int clicked;               /* segment clicked (here or in the "..." menu), else -1 */
    const char *submitted;     /* the typed path after Enter, else NULL; valid this frame */
    bool editing;
} cui_path_bar_result;
CUI_API cui_path_bar_result cui_path_bar_ex(cui_ctx *ui, const char *id, const char *const *segments, int count, const cui_path_bar_opts *opts);
/* True while the menu of separator *segment is showing: build menu items
 * for the children of that segment, then call cui_path_bar_menu_end. */
CUI_API bool cui_path_bar_menu_begin(cui_ctx *ui, const char *id, int *segment);
CUI_API void cui_path_bar_menu_end(cui_ctx *ui);
/* File browser and file picker (an open or save dialog drawn by CUI, like
 * the rest of the window; cui_app_file_dialog in cui_app.h opens the
 * system's own). The browser lists a folder (name,
 * date modified and size; folders first, any column sorts), with back,
 * forward and up, a path bar you can type into, a filter field and a
 * sidebar of places. Double-click or Enter opens a folder or picks a file;
 * Backspace goes up, Alt+Left and Alt+Right go back and forward, F5
 * reloads, and typing a letter jumps to the next name that starts with it.
 * A folder is read when you enter it (or reload), not every frame, and only
 * the rows in view are built.
 *
 * Listing goes through `list`, by default cui_fs_list (the local disk; none
 * in a browser build), so a dialog can show any tree: a zip, a remote store.
 *
 *     static bool open; static char path[512];
 *     if (cui_button(ui, "Open...")) open = true;
 *     if (cui_file_picker(ui, "open", &open, path, sizeof path, .filter = "*.png;*.jpg")) load(path);
 */
typedef struct cui_file_entry {
    const char *name;
    bool dir;
    uint64_t size;             /* bytes */
    int64_t modified;          /* seconds since 1970 (UTC); 0 unknown */
} cui_file_entry;
/* Lists a folder into out (names copied into names). Returns the number of
 * entries, or -1 when the folder cannot be read. Without hidden, names
 * starting with a dot (and hidden files on Windows) are left out. */
typedef int (*cui_file_list_fn)(void *user, const char *path, cui_file_entry *out, int cap, char *names, size_t names_cap, bool hidden);
CUI_API int cui_fs_list(void *user, const char *path, cui_file_entry *out, int cap, char *names, size_t names_cap, bool hidden);

typedef struct cui_file_browser_opts {
    int _;
    cui_size width, height;    /* default fill and 380 */
    const char *filter;        /* patterns that files must match, e.g. "*.png;*.jpg"; folders always show */
    bool folders_only;
    bool hidden;               /* show hidden files */
    bool no_places;            /* the sidebar of places */
    const char *const *places; int place_count;   /* sidebar paths, default home, desktop, documents, downloads and the root */
    cui_file_list_fn list; void *list_user;       /* default cui_fs_list */
    int max_entries;           /* default 20,000 */
} cui_file_browser_opts;
typedef struct cui_file_browser_result {
    const char *picked;        /* a file double-clicked or Entered, as a full path; valid this frame */
    const char *selected;      /* the selected entry as a full path, or NULL; valid this frame */
    bool navigated;            /* the folder in *path changed */
    bool error;                /* the folder could not be read */
} cui_file_browser_result;
/* path: the folder shown, yours; the browser writes it as you move around. */
CUI_API cui_file_browser_result cui_file_browser_ex(cui_ctx *ui, const char *id, char *path, size_t path_cap, const cui_file_browser_opts *opts);

typedef struct cui_file_picker_opts {
    int _;
    const char *title;         /* default "Open", "Save" or "Choose a folder" */
    bool save;                 /* a name field and Save */
    bool folders;              /* choose a folder */
    const char *start;         /* the folder to start in, default the current directory */
    const char *name;          /* save: the name to start with */
    const char *filter;        /* e.g. "*.png;*.jpg" */
    const char *filter_label;  /* e.g. "Images" */
    cui_file_list_fn list; void *list_user;
} cui_file_picker_opts;
/* True once, with the chosen path in out, when the dialog is confirmed;
 * *open turns false then, or on Cancel and Esc. */
CUI_API bool cui_file_picker_ex(cui_ctx *ui, const char *id, bool *open, char *out, size_t out_cap, const cui_file_picker_opts *opts);

#define cui_tree_table_begin(ui, id, cols, ncols, nodes, n, ...) cui_tree_table_begin_ex((ui), (id), (cols), (ncols), (nodes), (n), CUI__OPTS(cui_tree_table_opts, __VA_ARGS__))
#define cui_path_bar(ui, id, segs, n, ...) cui_path_bar_ex((ui), (id), (segs), (n), CUI__OPTS(cui_path_bar_opts, __VA_ARGS__))
#define cui_file_browser(ui, id, path, cap, ...) cui_file_browser_ex((ui), (id), (path), (cap), CUI__OPTS(cui_file_browser_opts, __VA_ARGS__))
#define cui_terminal(ui, id, t, ...) cui_terminal_ex((ui), (id), (t), CUI__OPTS(cui_terminal_opts, __VA_ARGS__))
#define cui_file_picker(ui, id, open, out, cap, ...) cui_file_picker_ex((ui), (id), (open), (out), (cap), CUI__OPTS(cui_file_picker_opts, __VA_ARGS__))

/* ------------------------------------------------------------------------ */
/* Code                                                                     */
/* ------------------------------------------------------------------------ */

/* Code block with syntax highlighting (lang: "c", "cpp", "js", "ts",
 * "python", "rust", "go", "json", "shell"), line numbers, a title bar and a
 * copy button. Long lines scroll sideways. */
typedef struct cui_code_opts {
    int _;
    const char *lang;
    const char *title;         /* e.g. a file name */
    bool line_numbers;
    int highlight_from, highlight_to;   /* 1 based line range to emphasize */
    float max_height;          /* scrolls beyond this, default none */
    float size;                /* font size, default 13 */
    bool no_header;            /* no title bar, even with a lang */
} cui_code_opts;
CUI_API void cui_code_block_ex(cui_ctx *ui, const char *id, const char *code, const cui_code_opts *opts);

/* JSON viewer: a collapsible tree with typed colors and item counts.
 * Returns false when the text is not valid JSON (and shows the error). */
typedef struct cui_json_opts { int _; int expand_depth; float max_height; } cui_json_opts;
CUI_API bool cui_json_view_ex(cui_ctx *ui, const char *id, const char *json, const cui_json_opts *opts);

/* ---- Markdown view ---- */

/* Markdown view: renders your markdown text (UTF-8) as native content, every
 * frame, without keeping a copy:
 *
 *     static char notes[4096] = "## Release 2.4\n- [ ] Tag the release\n";
 *     cui_markdown_result r = cui_markdown(ui, "notes", notes);
 *     if (r.link) show_status(r.link);              (never opened for you)
 *     cui_markdown_apply_task(notes, &r);           (a checkbox was clicked)
 *
 * Supported: ATX headings (# to ######) and setext headings (=== and ---
 * under a line), paragraphs (single newlines are soft breaks; two trailing
 * spaces, a trailing backslash or <br> break the line), **bold**, *italic*
 * (and _ forms), ~~strikethrough~~, `inline code`, [links](url) and
 * ![images](url) (shown as links to the image), <https://autolinks>, bare
 * https:// and www. links, backslash escapes, &amp; &lt; &gt; &quot; &#39;
 * &nbsp;, bullet (- * +) and ordered (1. or 1)) lists nested by indentation,
 * task items (- [ ] and - [x]), block quotes, fenced code blocks (``` or ~~~
 * with a language, drawn by cui_code_block with its highlighter), indented
 * code, horizontal rules (--- *** ___) and pipe tables with an alignment row
 * (drawn with cui_table, scrolling sideways when narrower than their columns).
 * Everything else (HTML, reference links, footnotes, math) shows as plain
 * text. Containers nest at most 12 deep; deeper content shows as text.
 *
 * With .streaming set (text still arriving, as in an AI reply), an unclosed
 * **, *, ~~, `, [link] or (url) at the very end renders as if it were
 * closed, markers that may still grow are held back, and an unfinished
 * table row or heading underline waits for its line, so appending text never
 * flickers between styles. An unclosed code fence always runs to the end.
 *
 * Links use the accent color with an underline that firms up on hover, show
 * their URL as a tooltip, take Tab focus and activate with Enter. Text is not
 * selectable (CUI text has no selection yet); code blocks have a copy button.
 * Parsing allocates only from the frame arena (about 16 bytes a line and a
 * few dozen per inline marker), so a long reply re-parses every frame. */
typedef struct cui_markdown_opts {
    int _;
    cui_size width;            /* default fill */
    bool fit_content;          /* as wide as the content needs, up to the space there is (chat bubbles) */
    int length;                /* bytes of text to show, default strlen (text then need not end in a NUL) */
    float size;                /* body text size, default theme text_body */
    cui_color color;           /* body text color, default foreground; links then take it too, underlined */
    bool streaming;            /* text is still arriving */
    bool compact;              /* smaller headings and tighter spacing, for chat bubbles */
} cui_markdown_opts;
typedef struct cui_markdown_result {
    const char *link;          /* URL of the link clicked (or activated with Enter) this frame, or NULL; frame memory */
    const char *hovered_link;  /* URL of the link under the pointer, or NULL; frame memory */
    int task_offset;           /* a task box was toggled: byte offset in text of the character between its brackets, else -1 */
    int task_line;             /* that item's 1 based line in text, 0 when none */
    bool task_checked;         /* the state the box was toggled to */
    float height;              /* height of the view in the latest layout */
} cui_markdown_result;
CUI_API cui_markdown_result cui_markdown_ex(cui_ctx *ui, const char *id, const char *text, const cui_markdown_opts *opts);
/* Writes a reported task toggle into text ('x' or ' ' between the brackets).
 * Returns false when nothing was toggled or text no longer has a box there. */
CUI_API bool cui_markdown_apply_task(char *text, const cui_markdown_result *result);
#ifndef __cplusplus
#define cui_markdown(ui, id, text, ...) cui_markdown_ex((ui), (id), (text), CUI__OPTS(cui_markdown_opts, __VA_ARGS__))
#endif

/* ---- Code editor ---- */

/* Code editor: editable code with the code block's highlighting, a line
 * number gutter and a monospace grid, for shader editors, script consoles
 * and config files. You own the text: a NUL terminated buffer and its
 * capacity, edited in place.
 *
 *     static char src[64 * 1024] = "void main() {\n}\n";
 *     cui_code_editor_result r = cui_code_editor(ui, "shader", src, sizeof src,
 *                                                .lang = "c", .height = 360);
 *     if (r.changed) recompile(src);
 *     cui_textf(ui, "Ln %d, Col %d", r.line, r.column);
 *
 * Lines never wrap; long ones scroll sideways while the gutter stays. Only
 * the lines in view are tokenized and drawn, so files of tens of thousands
 * of lines stay fast. An edit that would not fit in the capacity is refused
 * whole (r.refused). Keys when focused: arrows, Ctrl+arrows by word, Home
 * (first character, then column 0), End, Page Up/Down, Ctrl+Home/End;
 * Shift extends; Ctrl+A; Ctrl+C/X/V (the whole line without a selection);
 * Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y; Tab and Shift+Tab indent and outdent the
 * selected lines; Enter keeps the indentation (a level deeper after an
 * opening bracket, or a colon in Python); Ctrl+F finds, Ctrl+G goes to a
 * line; Esc closes the bar, else clears the selection, else lets the next
 * Tab move focus. Double-click selects a word, triple-click or the gutter a
 * line. Read only keeps selection, copy and find. */
typedef struct cui_code_editor_opts {
    int _;
    const char *lang;          /* as cui_code_block: "c", "cpp", "js", "ts", "python", "rust", "go", "json", "shell"; NULL plain */
    cui_size width;            /* default fill */
    float height;              /* default 320; negative fills the space */
    float size;                /* font size, default 13 */
    int tab_size;              /* columns per indentation level, default 4 */
    bool use_tabs;             /* indent with tab characters instead of spaces */
    bool read_only;
    bool no_line_numbers;
    bool autofocus;
    const char *placeholder;   /* shown while the text is empty */
} cui_code_editor_opts;
typedef struct cui_code_editor_result {
    bool changed;              /* the text changed this frame */
    bool refused;              /* an edit did not fit in the capacity and was dropped */
    bool focused;              /* the editor or its find bar has the keyboard */
    int cursor, anchor;        /* caret and the other end of the selection, byte offsets */
    int line, column;          /* the caret, 1 based; columns count tabs to their stop */
    int line_count, length;    /* lines, and bytes of text */
    int bracket, bracket_match;   /* the bracket beside the caret and its partner, byte offsets, or -1 */
    int find_matches, find_index; /* with the find bar open: matches, and which one is selected (1 based, 0 none) */
} cui_code_editor_result;
CUI_API cui_code_editor_result cui_code_editor_ex(cui_ctx *ui, const char *id, char *buf, size_t cap, const cui_code_editor_opts *opts);
/* On the editor's next frame: put the caret at a line and column (1 based),
 * focus the editor and scroll there. Call from the same id scope. */
CUI_API void cui_code_editor_goto(cui_ctx *ui, const char *id, int line, int column);
/* On the next frame: select bytes anchor..cursor, focus and scroll there. */
CUI_API void cui_code_editor_select(cui_ctx *ui, const char *id, int anchor, int cursor);
/* Opens the find bar with query and selects the next match (NULL closes). */
CUI_API void cui_code_editor_find(cui_ctx *ui, const char *id, const char *query);
#ifndef __cplusplus
#define cui_code_editor(ui, id, buf, cap, ...) cui_code_editor_ex((ui), (id), (buf), (cap), CUI__OPTS(cui_code_editor_opts, __VA_ARGS__))
#endif
/* ---- Diff viewer ---- */

/* Diff viewer: compares two versions of a text line by line, side by side or
 * inline, with changed words marked inside changed lines. The texts are yours
 * (UTF-8, read during the call); the view keeps a copy and diffs again only
 * when one of them changes, so it is cheap to call every frame.
 *
 *     static cui_diff_mode mode = CUI_DIFF_SPLIT;
 *     static int change = 0;                       (the selected change, -1 none)
 *     cui_diff_result r = cui_diff_view(ui, "diff", before, after, .mode = mode, .current = &change,
 *                                       .language = "c", .old_title = "parser.c (main)", .new_title = "parser.c");
 *     if (cui_icon_button(ui, CUI_ICON_ARROW_DOWN, .tooltip = "Next change", .shortcut = "N"))
 *         change = CUI_MIN(change + 1, r.changes - 1);   (or any index: the view scrolls to it)
 *     cui_textf(ui, "+%d -%d", r.added, r.removed);
 *
 * The line diff is Myers' algorithm (the shortest edit script, in linear
 * memory). Very different large texts are capped: past about 16 million search
 * steps the remaining parts are shown as replaced, still a correct diff but
 * not the shortest (r.approximate). Insertions and deletions slide to end on
 * a blank line where they can, the way an added function reads best. Paired
 * changed lines get word marks when they have enough in common.
 *
 * Split shows the old text on the left and the new on the right, rows aligned
 * with a blank filler opposite added or removed lines; narrower than
 * split_min_width (560 points) it shows inline instead (r.mode says which).
 * Each side has its line numbers and a + or - sign. Unchanged stretches keep
 * .context lines (3) beside each change; longer ones fold into a "42 unchanged
 * lines" row that opens on a click. Lines longer than the view scroll
 * sideways, both sides together; the first 1024 bytes of a line are drawn.
 *
 * With the view focused: N or Alt+Down goes to the next change, P or Alt+Up to
 * the previous one, arrows and Page Up/Down scroll, E opens every fold and
 * Shift+E folds them again, Esc clears the selected change. A click on a
 * change selects it. Only the rows in sight are built. */
typedef enum cui_diff_mode { CUI_DIFF_SPLIT = 0, CUI_DIFF_INLINE = 1 } cui_diff_mode;
typedef struct cui_diff_opts {
    int _;
    cui_diff_mode mode;
    const char *language;       /* syntax colors, as cui_code_block: "c", "js", "python", ... */
    const char *old_title, *new_title;   /* a header above the sides, e.g. file names, with the +/- summary */
    int *current;               /* the selected change, read and written (may be NULL); set it to scroll to a change */
    int context;                /* unchanged lines kept beside each change, default 3 */
    bool no_fold;               /* show every unchanged line */
    bool ignore_eol;            /* "\r\n" equals "\n", and a missing final newline does not count */
    float split_min_width;      /* below this width split shows inline, default 560 */
    cui_size width;             /* default fill */
    float height;               /* default: the rows, up to 420; negative fills the space */
    float size;                 /* font size, default 12 */
} cui_diff_opts;
typedef struct cui_diff_result {
    int added, removed;         /* lines */
    int changes;                /* change blocks (hunks without context) */
    int current;                /* the selected change, -1 none */
    bool current_changed;       /* this frame, by the keys, a click or .current */
    cui_diff_mode mode;         /* what is shown: inline when split did not fit */
    int folds, hidden;          /* folded rows shown, and the unchanged lines they hide */
    bool recomputed;            /* a text changed and was diffed this frame */
    bool approximate;           /* the search hit its limit: correct, maybe not the shortest */
} cui_diff_result;
CUI_API cui_diff_result cui_diff_view_ex(cui_ctx *ui, const char *id, const char *old_text, const char *new_text, const cui_diff_opts *opts);
#define cui_diff_view(ui, id, old_text, new_text, ...) cui_diff_view_ex((ui), (id), (old_text), (new_text), CUI__OPTS(cui_diff_opts, __VA_ARGS__))

/* The rows the view shows after folding, e.g. to put a comment button beside
 * a line. Copies rows [first, first + cap) and returns how many there are (0
 * before the view's first frame). Call it in the same id scope as the view. */
typedef enum cui_diff_row_kind {
    CUI_DIFF_ROW_SAME = 0, CUI_DIFF_ROW_REMOVED, CUI_DIFF_ROW_ADDED,
    CUI_DIFF_ROW_CHANGED,       /* split: a removed line beside the added line that replaced it */
    CUI_DIFF_ROW_FOLD           /* hidden unchanged lines, starting at old_line / new_line */
} cui_diff_row_kind;
typedef struct cui_diff_row {
    cui_diff_row_kind kind;
    int old_line, new_line;     /* 0 based, -1 for none */
    int change;                 /* the change it belongs to, or -1 */
    int hidden;                 /* fold rows: lines hidden */
} cui_diff_row;
CUI_API int cui_diff_view_rows(cui_ctx *ui, const char *id, cui_diff_row *rows, int first, int cap);

/* The same diff without a view. Old lines [old_start, old_start + old_count)
 * became new lines [new_start, new_start + new_count), 0 based; a pure
 * insertion has old_count 0 and goes before old line old_start. Writes up to
 * cap hunks and returns how many there are (-1 when out of memory). Lines end
 * at "\n"; a "\r" before it is part of the line ending. Allocates with malloc. */
typedef struct cui_diff_hunk { int old_start, old_count, new_start, new_count; } cui_diff_hunk;
typedef struct cui_diff_stats {
    int added, removed, hunks;
    int old_lines, new_lines;
    bool approximate;
} cui_diff_stats;
enum { CUI_DIFF_IGNORE_EOL = 1 };   /* flags */
CUI_API int cui_diff_compute(const char *old_text, size_t old_len, const char *new_text, size_t new_len, unsigned flags,
                             cui_diff_hunk *hunks, int cap, cui_diff_stats *stats);
/* Word marks between two versions of one line, as byte ranges (a length of -1
 * reads up to the terminator). Returns false when the lines have too little in
 * common for marks to help, or a line is very long (over 2000 bytes). */
typedef struct cui_diff_range { int start, length; } cui_diff_range;
CUI_API bool cui_diff_words(const char *old_line, int old_len, const char *new_line, int new_len,
                            cui_diff_range *old_ranges, int *old_count, cui_diff_range *new_ranges, int *new_count, int cap);

/* ------------------------------------------------------------------------ */
/* Conversation                                                             */
/* ------------------------------------------------------------------------ */

/* Chat message bubble. Consecutive messages from one person set grouped so
 * they stack tightly and show the avatar once. */
typedef struct cui_chat_opts {
    int _;
    bool mine;                 /* right aligned, accent bubble */
    const char *author;        /* name above the first bubble of a group */
    const char *time;
    const char *status;        /* under your own last message, e.g. "Read" */
    bool grouped;              /* continues the previous message */
    bool last_in_group;        /* shows the avatar (others) and status (mine) */
    cui_texture avatar;
    bool markdown;             /* render the text with cui_markdown (compact, as wide as it needs) */
    cui_markdown_result *markdown_result;   /* optional: links and task toggles the markdown reported */
} cui_chat_opts;
CUI_API void cui_chat_message_ex(cui_ctx *ui, const char *text, const cui_chat_opts *opts);
CUI_API void cui_chat_divider(cui_ctx *ui, const char *label);       /* "Today" */
CUI_API void cui_typing_indicator(cui_ctx *ui, const char *name);    /* bouncing dots */

/* Comment with replies. depth indents replies under a connector line.
 * Returns CUI_COMMENT_LIKE or CUI_COMMENT_REPLY flags for what was clicked;
 * *liked and *likes are updated for you. */
enum { CUI_COMMENT_LIKE = 1, CUI_COMMENT_REPLY = 2 };
typedef struct cui_comment_opts { int _; const char *time; int depth; int *likes; bool *liked; bool edited; const char *badge; } cui_comment_opts;
CUI_API int cui_comment_ex(cui_ctx *ui, const char *id, const char *author, const char *text, const cui_comment_opts *opts);

/* Presence: who is here now. Overlapping avatars with a colored ring each,
 * that spread out on hover and name themselves. */
typedef struct cui_presence_user { const char *name; cui_color color; cui_texture avatar; bool idle; } cui_presence_user;
CUI_API void cui_presence(cui_ctx *ui, const char *id, const cui_presence_user *users, int count, int max_visible, float size);

/* ------------------------------------------------------------------------ */
/* Media                                                                    */
/* ------------------------------------------------------------------------ */

/* Carousel: slides that you swipe, drag, scroll or page with the arrows
 * and dots. Build every slide between begin and end:
 *     cui_carousel(ui, "tour", 4, &index, .height = 220) {
 *         for (int k = 0; k < 4; k++) cui_carousel_slide(ui) { ... }
 *     }
 */
typedef struct cui_carousel_opts { int _; float height; float auto_advance; float gap; bool no_arrows, no_dots, loop; } cui_carousel_opts;
CUI_API bool cui_carousel_begin(cui_ctx *ui, const char *id, int count, int *index, const cui_carousel_opts *opts);
CUI_API void cui_carousel_slide_begin(cui_ctx *ui);
CUI_API void cui_carousel_slide_end(cui_ctx *ui);
CUI_API void cui_carousel_end(cui_ctx *ui);

/* Before and after: drag the divider to compare two images. *pos is 0..1. */
typedef struct cui_compare_opts {
    int _; cui_size width; float height; const char *before_label, *after_label;
    float aspect;               /* the images' width / height: they are cropped to fill, never stretched */
} cui_compare_opts;
CUI_API bool cui_image_compare_ex(cui_ctx *ui, const char *id, cui_texture before, cui_texture after, float *pos, const cui_compare_opts *opts);

/* Card stack: a deck where the top card can be flicked away left or right.
 * Build `count` cards; card 0 is the top. end returns -1 or 1 on the frame
 * the top card leaves (to the left or right), else 0. */
typedef struct cui_card_stack_opts { int _; cui_size width; float height; int visible; } cui_card_stack_opts;
CUI_API void cui_card_stack_begin(cui_ctx *ui, const char *id, int count, const cui_card_stack_opts *opts);
CUI_API void cui_card_stack_item_begin(cui_ctx *ui, int depth);
CUI_API void cui_card_stack_item_end(cui_ctx *ui);
CUI_API int cui_card_stack_end(cui_ctx *ui);

/* Odometer: a number whose digits roll to their new values. */
typedef struct cui_counter_opts { int _; const char *format; float size; cui_font font; cui_color color; bool group_thousands; } cui_counter_opts;
CUI_API void cui_counter_ex(cui_ctx *ui, const char *id, double value, const cui_counter_opts *opts);

/* Ticker: items that scroll by continuously, pausing on hover. */
typedef struct cui_ticker_item { const char *label; const char *value; float delta; bool lower_is_better; } cui_ticker_item;   /* a fall is good: shown green */
typedef struct cui_ticker_opts { int _; float speed; float height; } cui_ticker_opts;
CUI_API void cui_ticker_ex(cui_ctx *ui, const char *id, const cui_ticker_item *items, int count, const cui_ticker_opts *opts);

/* ------------------------------------------------------------------------ */
/* More charts                                                              */
/* ------------------------------------------------------------------------ */
/* Every chart below has a hover tooltip, animates in once, follows the theme
 * and returns the index of the mark that was clicked, or -1. Colors default
 * to the series palette; numbers use `format` (printf, default compact). */

/* Heatmap: a rows x cols matrix (values[row * cols + col]) on one hue,
 * light to dark, with a scale below. */
typedef struct cui_heatmap_opts {
    int _;
    const char *const *row_labels, *const *col_labels;
    float cell;                /* cell size, default fits the width */
    float min, max;            /* value range, default the data's */
    cui_color color;           /* hue of the scale, default the accent */
    const char *format;
    cui_size width;
} cui_heatmap_opts;
CUI_API int cui_heatmap_ex(cui_ctx *ui, const char *id, const float *values, int rows, int cols, const cui_heatmap_opts *opts);

/* Calendar heatmap: one cell per day from start, weeks as columns, in five
 * levels, with month and weekday labels and a Less / More legend. */
typedef struct cui_calendar_heatmap_opts { int _; float cell; cui_color color; const char *format; const char *unit; } cui_calendar_heatmap_opts;
CUI_API int cui_calendar_heatmap_ex(cui_ctx *ui, const char *id, cui_date start, const float *values, int days, const cui_calendar_heatmap_opts *opts);

/* Treemap: rectangles with areas proportional to their values. */
typedef struct cui_viz_item { const char *label; float value; cui_color color; } cui_viz_item;
typedef struct cui_treemap_opts { int _; cui_size width; float height; const char *format; } cui_treemap_opts;
CUI_API int cui_treemap_ex(cui_ctx *ui, const char *id, const cui_viz_item *items, int count, const cui_treemap_opts *opts);

/* Radar chart: series over shared axes (series[i].values[axis]). */
typedef struct cui_radar_opts { int _; float size; float max; const char *format; bool no_legend; } cui_radar_opts;
CUI_API int cui_radar_chart_ex(cui_ctx *ui, const char *id, const cui_series *series, int count, const char *const *axes, int axis_count,
                               const cui_radar_opts *opts);

/* Funnel: stages narrowing by value, with the conversion between them. */
typedef struct cui_funnel_opts { int _; cui_size width; float height; const char *format; cui_color color; } cui_funnel_opts;
CUI_API int cui_funnel_ex(cui_ctx *ui, const char *id, const cui_viz_item *stages, int count, const cui_funnel_opts *opts);

/* Sankey: flows between nodes; columns follow the links from the sources. */
typedef struct cui_sankey_link { int from, to; float value; } cui_sankey_link;
typedef struct cui_sankey_opts { int _; cui_size width; float height; const char *format; float node_width; } cui_sankey_opts;
CUI_API int cui_sankey_ex(cui_ctx *ui, const char *id, const char *const *nodes, int node_count, const cui_sankey_link *links, int link_count,
                          const cui_sankey_opts *opts);

/* Waffle: a 10 x 10 grid of squares split by share, with a legend. */
typedef struct cui_waffle_opts { int _; float cell; int columns, rows; const char *format; } cui_waffle_opts;
CUI_API int cui_waffle_ex(cui_ctx *ui, const char *id, const cui_viz_item *items, int count, const cui_waffle_opts *opts);

/* Split bar: one bar split into parts, with a legend of values and shares. */
typedef struct cui_split_bar_opts { int _; cui_size width; float height; const char *format; bool no_legend; } cui_split_bar_opts;
CUI_API int cui_split_bar_ex(cui_ctx *ui, const char *id, const cui_viz_item *items, int count, const cui_split_bar_opts *opts);

/* Bar list: ranked horizontal bars. Rows are keyed by label, so when the
 * values change the bars grow and the rows slide to their new rank. */
typedef struct cui_bar_list_opts { int _; cui_size width; const char *format; int max_rows; bool keep_order; cui_color color; } cui_bar_list_opts;
CUI_API int cui_bar_list_ex(cui_ctx *ui, const char *id, const cui_viz_item *items, int count, const cui_bar_list_opts *opts);

/* Activity rings: concentric progress rings; values past 1 keep winding. */
typedef struct cui_rings_opts { int _; float size; float thickness; const char *center; const char *caption; bool legend; /* names and shares beside the rings */ } cui_rings_opts;
CUI_API int cui_activity_rings_ex(cui_ctx *ui, const char *id, const cui_viz_item *rings, int count, const cui_rings_opts *opts);

/* Scatter (or bubble) chart: points per series, optional sizes. */
typedef struct cui_scatter_series { const char *name; const cui_vec2 *points; const float *sizes; int count; cui_color color; } cui_scatter_series;
typedef struct cui_scatter_opts {
    int _;
    cui_size width;
    float height;
    const char *x_label, *y_label;
    const char *x_format, *y_format;
    bool no_legend;
} cui_scatter_opts;
CUI_API int cui_scatter_chart_ex(cui_ctx *ui, const char *id, const cui_scatter_series *series, int count, const cui_scatter_opts *opts);

/* Radial gauge: one value on a dial with colored bands, labelled ticks, a
 * needle and a target marker, for dashboards (speed, load, temperature, a
 * score against a goal). The needle springs to new values.
 *
 *     static const cui_gauge_band bands[] = { { 60, CUI_TONE_SUCCESS }, { 85, CUI_TONE_WARNING }, { 100, CUI_TONE_DANGER } };
 *     cui_radial_gauge(ui, "cpu", load, 0, 100, .unit = "%", .label = "CPU load", .bands = bands, .band_count = 3);
 */
typedef struct cui_gauge_band { float to; cui_tone tone; } cui_gauge_band;   /* from the band before (or min) up to `to` */
typedef struct cui_radial_gauge_opts {
    int _;
    const char *label;         /* under the value */
    const char *format;        /* the value and the tick labels, default "%.0f" */
    const char *unit;          /* after the value, smaller */
    float size;                /* width, default 200; the height follows the sweep */
    float sweep;               /* degrees of arc, default 240 (90 to 300) */
    const cui_gauge_band *bands; int band_count;
    int ticks;                 /* labelled divisions, default 5; -1 hides the ticks */
    float target;              /* a marker on the arc, when show_target is set */
    bool show_target;
    bool no_needle;            /* a thick arc filled to the value instead of a needle */
} cui_radial_gauge_opts;
CUI_API void cui_radial_gauge_ex(cui_ctx *ui, const char *id, float value, float min, float max, const cui_radial_gauge_opts *opts);

/* Candlestick chart: open, high, low and close per period with volume under
 * it, for prices. The wheel zooms around the pointer, dragging (or Shift+wheel)
 * pans, a crosshair shows each candle's values, and the price axis fits the
 * candles in view. Zoomed out past a few points per candle, candles merge
 * per pixel column, so a long history stays fast. When the view shows the
 * newest candle it follows new ones as they are appended. With the chart
 * focused, Left and Right move the crosshair (Shift: ten at a time), Home and
 * End jump to the ends, + and - zoom and 0 fits everything.
 *
 *     cui_candlestick(ui, "btc", candles, count, .labels = dates, .overlays = &sma, .overlay_count = 1);
 */
typedef struct cui_candle { float open, high, low, close, volume; } cui_candle;
typedef struct cui_candlestick_opts {
    int _;
    cui_size width;
    float height;              /* default 320 */
    const char *const *labels; /* one per candle (dates or times): the time axis and the tooltip */
    const char *format;        /* prices, default "%.2f" */
    const cui_series *overlays; int overlay_count;   /* lines with one value per candle, e.g. moving averages */
    bool no_volume;            /* volume bars show when any candle has volume */
    bool ohlc;                 /* OHLC bars instead of candle bodies */
    cui_color up, down;        /* default the success and danger colors */
} cui_candlestick_opts;
/* Returns the candle under the crosshair (pointer or keys), or -1. */
CUI_API int cui_candlestick_ex(cui_ctx *ui, const char *id, const cui_candle *candles, int count, const cui_candlestick_opts *opts);

/* ------------------------------------------------------------------------ */
/* Motion and text effects                                                  */
/* ------------------------------------------------------------------------ */
/* All of these respect reduced motion: they show their final state. */

typedef struct cui_text_fx_opts {
    int _;
    cui_font font;
    float size;
    cui_color color;
    cui_size width;
    bool nowrap;
    float speed;               /* shimmer: sweeps per second; typewriter: characters per second */
    float delay;               /* seconds before it starts */
    bool caret;                /* typewriter: a blinking caret while typing */
} cui_text_fx_opts;
/* Text with a highlight sweeping across it, for "Thinking..." states. */
CUI_API void cui_shimmer_text_ex(cui_ctx *ui, const char *text, const cui_text_fx_opts *opts);
/* Types the text out. When the text grows (streamed tokens) it carries on
 * from where it was; other changes start over. Returns true once complete. */
CUI_API bool cui_typewriter_ex(cui_ctx *ui, const char *id, const char *text, const cui_text_fx_opts *opts);
/* Words fade and rise into place one after another, when first shown and
 * again whenever the text changes. */
CUI_API void cui_text_reveal_ex(cui_ctx *ui, const char *id, const char *text, const cui_text_fx_opts *opts);
/* One word at a time; the next one rolls up into place and the width follows. */
CUI_API void cui_word_rotate_ex(cui_ctx *ui, const char *id, const char *const *words, int count, float interval, const cui_text_fx_opts *opts);
/* A figure whose changed digits roll into place: up when the value rises, down
 * when it falls, a beat apart from the left; unchanged characters stay still.
 * Pass the formatted text ("$64,210", "1.2k"); digits are tabular. */
CUI_API void cui_number_roll_ex(cui_ctx *ui, const char *id, const char *text, const cui_text_fx_opts *opts);

/* Text painted with a gradient from one color to another; animate makes it drift. */
typedef struct cui_gradient_text_opts { int _; cui_font font; float size; cui_color from, to; bool animate; cui_size width; } cui_gradient_text_opts;
CUI_API void cui_gradient_text_ex(cui_ctx *ui, const char *text, const cui_gradient_text_opts *opts);

/* Spotlight card: a soft light follows the pointer across the card and lights
 * the nearest stretch of its border. beam sends a light around the edge. */
typedef struct cui_spotlight_opts { int _; cui_size width, height; float pad, gap; cui_color color; bool beam; } cui_spotlight_opts;
CUI_API void cui_spotlight_begin(cui_ctx *ui, const char *id, const cui_spotlight_opts *opts);
CUI_API void cui_spotlight_end(cui_ctx *ui);

typedef enum cui_loader_style { CUI_LOADER_DOTS = 0, CUI_LOADER_BARS, CUI_LOADER_PULSE, CUI_LOADER_ORBIT, CUI_LOADER_GRID } cui_loader_style;
typedef struct cui_loader_opts { int _; cui_loader_style style; float size; cui_color color; } cui_loader_opts;
CUI_API void cui_loader_ex(cui_ctx *ui, const cui_loader_opts *opts);

/* Dock: a row of icons that grow as the pointer passes, with labels and a
 * dot under the running ones. Each item returns true when clicked. */
typedef struct cui_dock_opts { int _; float size; float magnify; } cui_dock_opts;
typedef struct cui_dock_item_opts { int _; cui_color color; bool running; int badge; } cui_dock_item_opts;
CUI_API void cui_dock_begin(cui_ctx *ui, const char *id, const cui_dock_opts *opts);
CUI_API bool cui_dock_item_ex(cui_ctx *ui, const char *icon, const char *label, const cui_dock_item_opts *opts);
CUI_API void cui_dock_separator(cui_ctx *ui);
CUI_API void cui_dock_end(cui_ctx *ui);

/* Bottom tab bar for compact layouts, with a pill that glides to the selection. */
typedef struct cui_tab_bar_item { const char *icon; const char *label; int badge; } cui_tab_bar_item;
typedef struct cui_tab_bar_opts { int _; cui_size width; bool labels_on_selected_only; } cui_tab_bar_opts;
CUI_API bool cui_tab_bar_ex(cui_ctx *ui, const char *id, const cui_tab_bar_item *items, int count, int *selected, const cui_tab_bar_opts *opts);

/* Control tile: a large toggle with an icon, a label and a state line. */
typedef struct cui_tile_opts { int _; const char *icon; const char *detail; cui_color color; cui_size width; bool wide; } cui_tile_opts;
CUI_API bool cui_control_tile_ex(cui_ctx *ui, const char *label, bool *on, const cui_tile_opts *opts);

/* Waveform: bars from peaks (0..1). With progress, the played part is
 * colored and clicking or dragging seeks; returns true when it did. */
typedef struct cui_waveform_opts { int _; cui_size width; float height; float bar; float gap; cui_color color; } cui_waveform_opts;
CUI_API bool cui_waveform_ex(cui_ctx *ui, const char *id, const float *peaks, int count, float *progress, const cui_waveform_opts *opts);

/* Now playing card: artwork, title, artist, a scrubber and transport buttons. */
typedef struct cui_now_playing { const char *title, *artist; cui_texture art; cui_color art_color; float position, duration; bool playing; } cui_now_playing;
typedef struct cui_now_playing_opts { int _; cui_size width; bool compact; } cui_now_playing_opts;
enum { CUI_PLAYER_TOGGLE = 1, CUI_PLAYER_PREV = 2, CUI_PLAYER_NEXT = 4, CUI_PLAYER_SEEK = 8 };
CUI_API int cui_now_playing_ex(cui_ctx *ui, const char *id, cui_now_playing *np, const cui_now_playing_opts *opts);

/* ------------------------------------------------------------------------ */
/* AI interfaces                                                            */
/* ------------------------------------------------------------------------ */

/* Reasoning: a model's thinking. While thinking, the header shimmers and
 * counts the seconds; afterwards it reads "Thought for 12s" and the body
 * folds away. Click the header to open or close it. Put text inside. */
typedef struct cui_reasoning_opts { int _; bool thinking; float seconds; const char *title; } cui_reasoning_opts;
CUI_API bool cui_reasoning_begin(cui_ctx *ui, const char *id, const cui_reasoning_opts *opts);
CUI_API void cui_reasoning_end(cui_ctx *ui);

/* Tool call: a compact row with the tool, a detail and its status; opens to
 * show the input and the output (shown as JSON when they parse). */
typedef enum cui_task_status { CUI_TASK_PENDING = 0, CUI_TASK_RUNNING, CUI_TASK_DONE, CUI_TASK_FAILED, CUI_TASK_SKIPPED } cui_task_status;
typedef struct cui_tool_call_opts { int _; cui_task_status status; const char *icon; const char *detail; const char *input; const char *output; float seconds; } cui_tool_call_opts;
CUI_API void cui_tool_call_ex(cui_ctx *ui, const char *id, const char *name, const cui_tool_call_opts *opts);

/* Agent plan: steps with their status on a connected rail, and progress. */
typedef struct cui_plan_step { const char *title; const char *detail; cui_task_status status; } cui_plan_step;
typedef struct cui_agent_plan_opts { int _; const char *title; } cui_agent_plan_opts;
CUI_API int cui_agent_plan_ex(cui_ctx *ui, const char *id, const cui_plan_step *steps, int count, const cui_agent_plan_opts *opts);

/* Agent status pill: what the agent is doing right now and for how long. */
typedef struct cui_agent_status_opts { int _; cui_task_status status; float seconds; const char *icon; } cui_agent_status_opts;
CUI_API void cui_agent_status_ex(cui_ctx *ui, const char *text, const cui_agent_status_opts *opts);

/* Sources: cards for the documents an answer used, and inline citations
 * that show a card on hover. Both return true / the index when clicked. */
typedef struct cui_source { const char *title; const char *url; const char *snippet; } cui_source;
CUI_API int cui_source_list(cui_ctx *ui, const char *id, const cui_source *sources, int count);
CUI_API bool cui_citation(cui_ctx *ui, int number, const cui_source *source);

/* Context meter: a small ring of how full the context is; hover for a
 * breakdown. It turns amber past 80% and red past 95%. */
typedef struct cui_context_opts { int _; const cui_viz_item *parts; int part_count; const char *unit; } cui_context_opts;
CUI_API void cui_context_meter_ex(cui_ctx *ui, const char *id, float used, float limit, const cui_context_opts *opts);

/* Response actions: copy, regenerate, vote and share under an answer. */
enum { CUI_RESPONSE_COPY = 1, CUI_RESPONSE_REGENERATE = 2, CUI_RESPONSE_UP = 4, CUI_RESPONSE_DOWN = 8, CUI_RESPONSE_SHARE = 16 };
typedef struct cui_response_actions_opts { int _; const char *copy_text; int *vote; bool no_regenerate, no_share; } cui_response_actions_opts;
CUI_API int cui_response_actions_ex(cui_ctx *ui, const char *id, const cui_response_actions_opts *opts);

/* Prompt suggestions: cards to start a conversation with. */
typedef struct cui_prompt_suggestion { const char *icon; const char *title; const char *detail; } cui_prompt_suggestion;
typedef struct cui_prompt_suggestions_opts { int _; int columns; } cui_prompt_suggestions_opts;
CUI_API int cui_prompt_suggestions_ex(cui_ctx *ui, const char *id, const cui_prompt_suggestion *items, int count, const cui_prompt_suggestions_opts *opts);

/* Model picker: the current model on a button; a list with details opens. */
typedef struct cui_model { const char *name; const char *detail; const char *badge; const char *icon; } cui_model;
typedef struct cui_model_picker_opts { int _; bool ghost; cui_placement placement; } cui_model_picker_opts;
CUI_API bool cui_model_picker_ex(cui_ctx *ui, const char *id, const cui_model *models, int count, int *selected, const cui_model_picker_opts *opts);

/* Chat composer: a growing text area with attachments, a model picker and a
 * send button that turns into stop while busy. Enter sends, Shift+Enter adds
 * a line. Returns CUI_COMPOSER_* flags; *removed is the attachment whose x
 * was clicked. */
enum { CUI_COMPOSER_SEND = 1, CUI_COMPOSER_STOP = 2, CUI_COMPOSER_ATTACH = 4, CUI_COMPOSER_REMOVE = 8, CUI_COMPOSER_MODEL = 16 };
typedef struct cui_composer_opts {
    int _;
    const char *placeholder;
    bool busy;
    const char *const *attachments;
    int attachment_count;
    int *removed;
    const cui_model *models;
    int model_count;
    int *model;
    int max_lines;             /* default 8 */
    cui_size width;
} cui_composer_opts;
CUI_API int cui_chat_composer_ex(cui_ctx *ui, const char *id, char *buf, size_t cap, const cui_composer_opts *opts);

/* Voice orb: a living shape for voice modes. level is the input or output
 * loudness (0..1). Returns true when clicked. */
typedef enum cui_voice_state { CUI_VOICE_IDLE = 0, CUI_VOICE_LISTENING, CUI_VOICE_THINKING, CUI_VOICE_SPEAKING } cui_voice_state;
typedef struct cui_voice_orb_opts { int _; float size; cui_color color; } cui_voice_orb_opts;
CUI_API bool cui_voice_orb_ex(cui_ctx *ui, const char *id, cui_voice_state state, float level, const cui_voice_orb_opts *opts);

/* ------------------------------------------------------------------------ */
/* Planning                                                                 */
/* ------------------------------------------------------------------------ */

/* Kanban board: columns of cards you drag between columns and within them.
 * The board reports drops; you move the card in your data:
 *
 *     cui_kanban_begin(ui, "board");
 *     for (int c = 0; c < ncols; c++) {
 *         cui_kanban_column_begin(ui, cols[c].title, cols[c].count, .dot = CUI_COLOR_SERIES(c));
 *         for (each card k in column c) {
 *             cui_kanban_card_begin(ui, card_id);   ... content ...   cui_kanban_card_end(ui);
 *         }
 *         cui_kanban_column_end(ui);
 *     }
 *     cui_kanban_move m;
 *     if (cui_kanban_end(ui, &m)) move_card(m.card, m.to_column, m.to_index);
 */
typedef struct cui_kanban_move { int card; int from_column, to_column, to_index; } cui_kanban_move;
typedef struct cui_kanban_column_opts { int _; cui_color dot; int limit; float width; } cui_kanban_column_opts;
CUI_API void cui_kanban_begin(cui_ctx *ui, const char *id);
CUI_API bool cui_kanban_column_begin_ex(cui_ctx *ui, const char *title, int card_count, const cui_kanban_column_opts *opts);   /* true: "+" clicked */
CUI_API bool cui_kanban_card_begin(cui_ctx *ui, int card_id);   /* true when clicked */
CUI_API void cui_kanban_card_end(cui_ctx *ui);
CUI_API void cui_kanban_column_end(cui_ctx *ui);
CUI_API bool cui_kanban_end(cui_ctx *ui, cui_kanban_move *move);

/* Week calendar: seven day columns on an hour grid. Drag an event to move
 * it (15 minute steps), drag its bottom edge to resize it. Returns the
 * index of a clicked event, or -1. */
typedef struct cui_cal_event { const char *title; const char *detail; int day; float start, end; cui_tone tone; } cui_cal_event;
typedef struct cui_week_opts { int _; float start_hour, end_hour, hour_height; cui_size height; bool read_only; } cui_week_opts;
CUI_API int cui_week_calendar_ex(cui_ctx *ui, const char *id, cui_date week_start, cui_cal_event *events, int count, const cui_week_opts *opts);

/* Month calendar: a month grid with events. All-day and multi-day events
 * are bars across the days they cover; timed events are a dot, a time and a
 * title. A day with more than fits says "+N more" and opens a list of them.
 * On a narrow screen the cells show colored dots and the selected day's
 * events are listed under the grid.
 *
 *     static cui_date month = { 2026, 11, 1 }, day;
 *     cui_month_result r = cui_month_calendar(ui, "cal", &month, events, count, .selected = &day);
 *     if (r.event >= 0) open_event(r.event);
 *     if (r.day_activated) new_event_on(day);
 *
 * With the grid focused the arrows move the selected day (into the next or
 * previous month), Page Up and Page Down change month, Home goes to today
 * and Enter activates the day. */
typedef struct cui_month_event {
    const char *title;
    cui_date start, end;       /* inclusive; an unset or earlier end means one day */
    float time;                /* hour of a timed event (9.5 is 9:30); ignored when all_day */
    bool all_day;              /* a bar, like every event that spans days */
    cui_tone tone;
} cui_month_event;
typedef struct cui_month_opts {
    int _;
    cui_date *selected;        /* the selected day: yours to read and set */
    int first_weekday;         /* 0 Monday (default) .. 6 Sunday */
    float cell_height;         /* default 104, 44 on a phone */
    const char *const *month_names;   /* 12 names for another language, January first */
    const char *const *day_names;     /* 7 short names, Monday first */
    bool hour12;               /* 9:30a rather than 9:30 */
    cui_date today;            /* overrides the system date when set (another time zone, tests) */
} cui_month_opts;
typedef struct cui_month_result {
    int event;                 /* clicked event, or -1 */
    bool day_clicked;          /* a day was clicked or moved to with the keys (see selected) */
    bool day_activated;        /* double-clicked or Enter: add an event there */
    bool month_changed;        /* the arrows, Today or the keys changed *month */
} cui_month_result;
CUI_API cui_month_result cui_month_calendar_ex(cui_ctx *ui, const char *id, cui_date *month, const cui_month_event *events, int count, const cui_month_opts *opts);

/* Gantt chart: a task list beside a timeline of bars. Drag a bar to move
 * it, its ends to resize it. Dependencies are drawn as elbow arrows.
 * Returns the index of a clicked task, or -1. */
typedef struct cui_gantt_task { const char *name; const char *owner; int start, end; float progress; cui_tone tone; int depends_on; bool milestone; } cui_gantt_task;
typedef struct cui_gantt_opts { int _; float day_width, row_height; cui_size height; int today; bool read_only; } cui_gantt_opts;
CUI_API int cui_gantt_ex(cui_ctx *ui, const char *id, cui_gantt_task *tasks, int count, cui_date origin, int days, const cui_gantt_opts *opts);

/* ------------------------------------------------------------------------ */
/* Disclosure                                                               */
/* ------------------------------------------------------------------------ */

/* Hover card: rich content that appears when the pointer rests on the
 * previous component, and stays while the pointer is over either. */
typedef struct cui_hover_card_opts { int _; cui_placement placement; float width; float delay; } cui_hover_card_opts;
CUI_API bool cui_hover_card_begin(cui_ctx *ui, const char *id, const cui_hover_card_opts *opts);
CUI_API void cui_hover_card_end(cui_ctx *ui);

/* Confirm popover: a small question anchored to the previous component.
 * Open it with cui_popover_open(ui, id). Returns true when confirmed. */
typedef struct cui_confirm_popover_opts {
    int _;
    const char *title;
    const char *description;
    const char *confirm_label;    /* default "Confirm" */
    const char *cancel_label;     /* default "Cancel" */
    bool danger;
    cui_placement placement;
} cui_confirm_popover_opts;
CUI_API bool cui_confirm_popover_ex(cui_ctx *ui, const char *id, const cui_confirm_popover_opts *opts);

/* Text clamped to a few lines with a fade and Show more / Show less. */
typedef struct cui_expandable_text_opts { int _; int lines; float size; cui_color color; cui_color fade_to; } cui_expandable_text_opts;
CUI_API bool cui_expandable_text_ex(cui_ctx *ui, const char *id, const char *text, const cui_expandable_text_opts *opts);

/* Expandable card: a card whose header opens a body with a height spring. */
typedef struct cui_expandable_card_opts { int _; const char *description; const char *icon; const char *badge; cui_tone badge_tone; } cui_expandable_card_opts;
CUI_API bool cui_expandable_card_begin(cui_ctx *ui, const char *title, bool *open, const cui_expandable_card_opts *opts);
CUI_API void cui_expandable_card_end(cui_ctx *ui);

/* Table of contents for a scroll area: highlights the section in view on a
 * progress rail, and scrolls to a section when clicked. Put
 * cui_toc_anchor(ui, "anchor") at each heading inside the scroll area, and
 * call cui_toc in the same scope as the scroll area. Returns the active index. */
typedef struct cui_toc_item { const char *title; const char *anchor; int level; } cui_toc_item;
CUI_API int cui_toc(cui_ctx *ui, const char *scroll_id, const cui_toc_item *items, int count);
CUI_API void cui_toc_anchor(cui_ctx *ui, const char *anchor);

/* ------------------------------------------------------------------------ */
/* More feedback                                                            */
/* ------------------------------------------------------------------------ */

/* Announcement banner across the top of a view. Collapses smoothly when
 * dismissed (*open becomes false). Returns true when the action is clicked. */
typedef struct cui_banner_opts { int _; cui_tone tone; const char *icon; const char *action; bool dismissible; } cui_banner_opts;
CUI_API bool cui_banner_ex(cui_ctx *ui, const char *text, bool *open, const cui_banner_opts *opts);

/* Countdown: days, hours, minutes and seconds as tiles whose digits roll. */
typedef struct cui_countdown_opts { int _; bool hide_days; bool compact; } cui_countdown_opts;
CUI_API void cui_countdown_ex(cui_ctx *ui, const char *id, double seconds_left, const cui_countdown_opts *opts);

/* Onboarding checklist: a card with progress and steps. The first open step
 * shows its description and action. Clicking a step's circle toggles done.
 * Returns the index whose action was clicked, or -1. */
typedef struct cui_check_step { const char *title; const char *description; const char *action; bool done; } cui_check_step;
typedef struct cui_checklist_opts { int _; const char *title; const char *description; } cui_checklist_opts;
CUI_API int cui_checklist_ex(cui_ctx *ui, const char *id, cui_check_step *steps, int count, const cui_checklist_opts *opts);

/* ------------------------------------------------------------------------ */
/* Pickers                                                                  */
/* ------------------------------------------------------------------------ */

/* Multi-select: a field with chips for the chosen items and a list with
 * checkboxes (and search). selected[i] is true for chosen items. */
typedef struct cui_multi_select_opts {
    int _;
    const char *label;
    const char *placeholder;
    const char *const *icons;
    bool searchable;
    int max_chips;             /* chips before "+N", default 3 */
    cui_size width;
} cui_multi_select_opts;
CUI_API bool cui_multi_select_ex(cui_ctx *ui, const char *id, const char *const *items, int count, bool *selected, const cui_multi_select_opts *opts);

/* Date ranges. A zero start means nothing is chosen. */
typedef struct cui_date_range { cui_date start, end; } cui_date_range;
/* One or two months side by side; click a start and an end. */
CUI_API bool cui_range_calendar(cui_ctx *ui, const char *id, cui_date_range *range, int months);
typedef struct cui_date_range_opts { int _; const char *label; const char *placeholder; cui_size width; bool no_presets; } cui_date_range_opts;
CUI_API bool cui_date_range_picker_ex(cui_ctx *ui, const char *id, cui_date_range *range, const cui_date_range_opts *opts);
CUI_API int cui_date_to_days(cui_date d);      /* days since 1970-01-01 */
CUI_API cui_date cui_date_from_days(int days);

/* Time picker: a field that opens hour and minute columns. hour is 0..23. */
typedef struct cui_time_opts { int _; const char *label; bool twelve_hour; int minute_step; cui_size width; } cui_time_opts;
CUI_API bool cui_time_picker_ex(cui_ctx *ui, const char *id, int *hour, int *minute, const cui_time_opts *opts);

/* Wheel picker: a drum of options that you drag, scroll or arrow through.
 * Several side by side make a date or time reel. */
/* join_left / join_right square off the selection band so wheels placed
 * side by side share one continuous band. */
typedef struct cui_wheel_opts { int _; int rows; cui_size width; bool loop; bool join_left, join_right; } cui_wheel_opts;
CUI_API bool cui_wheel_picker_ex(cui_ctx *ui, const char *id, const char *const *items, int count, int *index, const cui_wheel_opts *opts);

/* Time dial: a clock face. Drag the hand to set hours, then minutes. */
CUI_API bool cui_time_dial(cui_ctx *ui, const char *id, int *hour, int *minute, float size);

/* Ruler slider: a tape measure that slides under a fixed marker, with
 * momentum and snapping to step. */
typedef struct cui_ruler_opts { int _; float step; int major_every; float spacing; const char *format; const char *unit; cui_size width; } cui_ruler_opts;
CUI_API bool cui_ruler_slider_ex(cui_ctx *ui, const char *label, float *value, float min, float max, const cui_ruler_opts *opts);

/* Range slider over a histogram of the data it filters. */
typedef struct cui_histogram_opts { int _; float height; const char *format; } cui_histogram_opts;
CUI_API bool cui_histogram_slider_ex(cui_ctx *ui, const char *label, const float *bins, int bin_count, float *lo, float *hi,
                                     float min, float max, const cui_histogram_opts *opts);

/* Knob, fader and level meter: the controls of audio, synth and mixer panels.
 *
 *     cui_knob(ui, "Cutoff", &cutoff, 20, 20000, .format = "%.0f", .unit = "Hz", .reset = 1000);
 *     cui_knob(ui, "Pan", &pan, -1, 1, .bipolar = true, .format = "%+.2f");
 *     cui_fader(ui, "Vocals", &gain_db, -60, 6, .unit = "dB", .ticks = marks, .tick_count = 6);
 *     cui_level_meter(ui, "vu", peaks, 2, .scale = true);   // peaks: linear sample peaks, 1.0 = 0 dBFS
 *
 * Knobs turn by dragging up and down (Shift for fine steps); the wheel turns a
 * focused knob or fader. Double-click, Delete or Backspace go back to `reset`
 * (0 unless set, kept within the range: 0 dB, the middle of -1..1, or the
 * minimum of 20..20000). Arrows step, Shift+arrows and Page Up/Down step ten
 * times as far, Home and End go to the ends. */
typedef struct cui_knob_opts {
    int _;
    const char *label;         /* under the knob; the id when NULL */
    const char *format;        /* printf format for the value, default "%.0f" */
    const char *unit;          /* after the value, e.g. "Hz" or "dB" */
    float size;                /* diameter, default 48 */
    float step;                /* 0 = continuous (keys move 1% of the range) */
    float reset;               /* double-click value, default 0 (clamped to the range) */
    bool bipolar;              /* the arc grows from the middle: pan, EQ gain */
    bool log_scale;            /* equal turns multiply the value: frequency, time (min > 0) */
    bool hide_value;
    bool disabled;
    cui_color color;           /* the value arc, default the control color */
} cui_knob_opts;
CUI_API bool cui_knob_ex(cui_ctx *ui, const char *id, float *value, float min, float max, const cui_knob_opts *opts);

typedef struct cui_fader_opts {
    int _;
    const char *label;         /* under a vertical fader, before a horizontal one; the id when NULL */
    const char *format;        /* default "%.1f" */
    const char *unit;
    float length;              /* track length, default 160 */
    bool horizontal;
    float step;
    float reset;               /* double-click value, default 0 (clamped to the range) */
    const float *ticks; int tick_count;   /* scale marks in value units, labelled beside the track */
    const float *meter; int meter_channels;   /* a level meter beside the track (linear peaks, up to 8); a dB fader's range sets its scale */
    bool hide_value;
    bool disabled;
} cui_fader_opts;
CUI_API bool cui_fader_ex(cui_ctx *ui, const char *id, float *value, float min, float max, const cui_fader_opts *opts);

/* Level meter: bars that jump up to each new peak and fall back at `decay`
 * dB per second, with a peak mark that holds for `hold` seconds and a clip
 * light that stays on until clicked. Levels are linear sample peaks (1.0 is
 * 0 dBFS); green runs to -18 dB, amber to -6 dB, red above. */
typedef struct cui_level_meter_opts {
    int _;
    float min_db, max_db;      /* default -60 and +6 */
    float length;              /* default 160 */
    float thickness;           /* each channel's bar, default 6 */
    bool horizontal;
    bool scale;                /* dB labels beside the bars */
    bool segmented;            /* LED segments instead of a solid bar */
    float hold;                /* peak hold, seconds; default 1.5, < 0 none */
    float decay;               /* fall rate, dB per second; default 20 */
} cui_level_meter_opts;
/* Returns true when the clip light was clicked to clear it. */
CUI_API bool cui_level_meter_ex(cui_ctx *ui, const char *id, const float *levels, int channels, const cui_level_meter_opts *opts);

/* ------------------------------------------------------------------------ */
/* Capture                                                                  */
/* ------------------------------------------------------------------------ */

/* Signature pad: draw with the mouse or a pen. The strokes are kept for you;
 * read them with cui_signature_points (stroke_ends[i] is one past the last
 * point of stroke i). */
typedef struct cui_signature_opts { int _; cui_size width; float height; const char *placeholder; cui_color ink; } cui_signature_opts;
CUI_API bool cui_signature_pad_ex(cui_ctx *ui, const char *id, const cui_signature_opts *opts);
CUI_API int cui_signature_points(cui_ctx *ui, const char *id, const cui_vec2 **points, const int **stroke_ends, int *stroke_count);
CUI_API void cui_signature_clear(cui_ctx *ui, const char *id);

/* Dropzone: drop files from the OS, or click to browse. Returns the number
 * of files dropped on it this frame (read them with cui_dropped_file), -1
 * when clicked (open a file dialog), 0 otherwise. accept filters by
 * extension, e.g. "png;jpg;pdf". */
typedef struct cui_dropzone_opts {
    int _;
    const char *title;         /* default "Drop files here" */
    const char *description;   /* default "or click to browse" */
    const char *icon;
    const char *accept;
    float height;              /* default 160 */
    cui_size width;
    bool compact;              /* one row instead of a tall area */
} cui_dropzone_opts;
CUI_API int cui_dropzone_ex(cui_ctx *ui, const char *id, const cui_dropzone_opts *opts);
CUI_API bool cui_path_has_extension(const char *path, const char *accept);

/* Avatar upload: a round avatar that takes a dropped image, with a camera
 * badge on hover. Returns 1 when *image changed, -1 when clicked (browse),
 * 0 otherwise. A dropped image is loaded with cui_image_load. */
CUI_API int cui_avatar_upload(cui_ctx *ui, const char *id, const char *name, cui_texture *image, float size);

/* ------------------------------------------------------------------------ */
/* More inputs                                                              */
/* ------------------------------------------------------------------------ */

/* Inline edit: text that turns into a field in place. Click (or Enter when
 * focused) to edit, Enter or clicking away saves, Esc cancels. Returns
 * CUI_CHANGED when an edit is committed. */
typedef struct cui_inline_edit_opts { int _; cui_font font; float size; const char *placeholder; cui_size width; } cui_inline_edit_opts;
CUI_API int cui_inline_edit_ex(cui_ctx *ui, const char *id, char *buf, size_t cap, const cui_inline_edit_opts *opts);

/* Scrub field: drag sideways anywhere on it to change the value (Shift for
 * fine steps), or click to type a number. Arrow keys step when focused. */
typedef struct cui_scrub_opts {
    int _;
    float step;                /* default (max - min) / 100 */
    const char *format;        /* default "%.0f" */
    const char *suffix;        /* e.g. "px" or "%" */
    const char *icon;          /* shown instead of the label */
    cui_size width;
} cui_scrub_opts;
CUI_API bool cui_scrub_ex(cui_ctx *ui, const char *label, float *value, float min, float max, const cui_scrub_opts *opts);

/* Money field: shows the amount with thousands separators, edits as plain
 * digits, keeps a fixed number of decimals. */
typedef struct cui_money_opts { int _; const char *label; const char *symbol; const char *code; int decimals; cui_size width; const char *hint; } cui_money_opts;
CUI_API bool cui_money_input_ex(cui_ctx *ui, const char *id, double *value, const cui_money_opts *opts);

/* Shortcut recorder: click and press a key combination to record it.
 * Backspace clears, Esc cancels. Returns true when the combination changed.
 * cui_shortcut_label formats a combination ("Ctrl Shift K"). */
typedef struct cui_key_combo { int mods; cui_key key; } cui_key_combo;
typedef struct cui_shortcut_recorder_opts { int _; const char *placeholder; cui_size width; } cui_shortcut_recorder_opts;
CUI_API bool cui_shortcut_recorder_ex(cui_ctx *ui, const char *id, cui_key_combo *combo, const cui_shortcut_recorder_opts *opts);
CUI_API const char *cui_shortcut_label(cui_ctx *ui, cui_key_combo combo);
CUI_API const char *cui_key_name(cui_key key);

/* ---- Node editor ---- */

/* Node editor: nodes with input and output pins on a canvas that pans and
 * zooms, joined by curved links. You own the nodes, pins and links; the
 * editor moves the node positions you pass and reports what the user did:
 *
 *     if (cui_node_editor_begin(ui, "graph", &view, .height = CUI_PX(480))) {
 *         for (each node n) {
 *             cui_node_begin(ui, n->id, &n->x, &n->y, n->title);
 *             cui_node_input(ui, n->in_pin, "Color", .type = 1);
 *             cui_node_content(ui) { cui_slider(ui, "##fac", &n->factor, 0, 1); }
 *             cui_node_output(ui, n->out_pin, "Result", .type = 1);
 *             cui_node_end(ui);
 *         }
 *         for (each link l) cui_node_link(ui, l->id, l->from_pin, l->to_pin);
 *         int a, b, id;
 *         while (cui_node_link_created(ui, &a, &b)) add_link(a, b);   (output pin, input pin)
 *         while (cui_node_link_removed(ui, &id)) remove_link(id);
 *         while (cui_node_deleted(ui, &id)) remove_node(id);
 *         cui_node_editor_end(ui);
 *     }
 *
 * Node, pin and link ids are your own integers (pin ids unique in the
 * editor). Scroll to zoom around the pointer; pan with the middle button or
 * Space and drag. Drag a card to move it (with the rest of the selection),
 * click to select, Shift or Ctrl to add, drag on the empty canvas for a
 * marquee. Drag from a pin to make a link; drag a link's input end off its
 * pin to detach it. Delete removes the selection, F frames every node. */
typedef struct cui_node_view { float x, y, zoom; } cui_node_view;   /* pan in points; zoom 0 means 1 */
typedef struct cui_node_editor_opts {
    int _;
    cui_size width, height;    /* default: fill the width, 440 tall */
    float min_zoom, max_zoom;  /* default 0.25 .. 2 */
    bool no_grid;
    bool minimap;              /* an overview of the graph in the bottom right corner; click it to move there */
} cui_node_editor_opts;
CUI_API bool cui_node_editor_begin_ex(cui_ctx *ui, const char *id, cui_node_view *view, const cui_node_editor_opts *opts);
CUI_API void cui_node_editor_end(cui_ctx *ui);
/* A node at canvas position (*x, *y), updated while it is dragged. Returns
 * true while the node is selected. */
typedef struct cui_node_opts { int _; float width; const char *icon; } cui_node_opts;   /* width default 184 */
CUI_API bool cui_node_begin_ex(cui_ctx *ui, int node_id, float *x, float *y, const char *title, const cui_node_opts *opts);
CUI_API void cui_node_end(cui_ctx *ui);
/* A row with a pin on the left (input) or right (output). type picks the pin
 * color from the series palette and limits links to pins of the same type
 * (0 connects to anything). Returns true when the pin has a link. */
typedef struct cui_pin_opts { int _; int type; } cui_pin_opts;
CUI_API bool cui_node_input_ex(cui_ctx *ui, int pin_id, const char *label, const cui_pin_opts *opts);
CUI_API bool cui_node_output_ex(cui_ctx *ui, int pin_id, const char *label, const cui_pin_opts *opts);
/* A row for ordinary components. They are laid out at 100% and scaled with
 * the view, so their text is sharpest at 100% zoom and below. */
CUI_API void cui_node_content_begin(cui_ctx *ui);
CUI_API void cui_node_content_end(cui_ctx *ui);
/* A link from an output pin to an input pin. Returns true while it is selected. */
CUI_API bool cui_node_link(cui_ctx *ui, int link_id, int from_pin, int to_pin);
/* What the user did this frame, one event per call; call them after
 * cui_node_editor_begin (and up to the end of the frame). */
CUI_API bool cui_node_link_created(cui_ctx *ui, int *from_pin, int *to_pin);
CUI_API bool cui_node_link_removed(cui_ctx *ui, int *link_id);
CUI_API bool cui_node_deleted(cui_ctx *ui, int *node_id);
#ifndef __cplusplus
#define cui_node_editor_begin(ui, id, view, ...) cui_node_editor_begin_ex((ui), (id), (view), CUI__OPTS(cui_node_editor_opts, __VA_ARGS__))
#define cui_node_begin(ui, node_id, x, y, title, ...) cui_node_begin_ex((ui), (node_id), (x), (y), (title), CUI__OPTS(cui_node_opts, __VA_ARGS__))
#define cui_node_input(ui, pin, label, ...)  cui_node_input_ex((ui), (pin), (label), CUI__OPTS(cui_pin_opts, __VA_ARGS__))
#define cui_node_output(ui, pin, label, ...) cui_node_output_ex((ui), (pin), (label), CUI__OPTS(cui_pin_opts, __VA_ARGS__))
#define cui_node_content(ui)                 CUI__SCOPE(cui_node_content_begin(ui), cui_node_content_end(ui))
#endif

/* ------------------------------------------------------------------------ */
/* App chrome: menu bar, toolbar, status bar, document tabs                 */
/* ------------------------------------------------------------------------ */

/* Menu bar: File / Edit / View along the top of a window. Each menu holds
 * the usual menu items (cui_menu_item, cui_menu_separator, cui_menu_label).
 * Once a menu is open, hovering another one switches to it. Alt (on its own)
 * or F10 focuses the bar; arrows move between menus and items, Enter
 * chooses, Esc steps back. Alt + a menu's mnemonic letter opens it (the
 * first letter of its label unless `mnemonic` says otherwise). */
typedef struct cui_menu_bar_opts { int _; bool no_border; } cui_menu_bar_opts;
CUI_API void cui_menu_bar_begin(cui_ctx *ui, const char *id, const cui_menu_bar_opts *opts);
CUI_API void cui_menu_bar_end(cui_ctx *ui);
/* One menu of the bar. Returns true while its items should be built; call
 * cui_menu_bar_menu_end only then. */
typedef struct cui_menu_bar_menu_opts { int _; char mnemonic; float width; bool disabled; } cui_menu_bar_menu_opts;
CUI_API bool cui_menu_bar_menu_begin(cui_ctx *ui, const char *label, const cui_menu_bar_menu_opts *opts);
CUI_API void cui_menu_bar_menu_end(cui_ctx *ui);

/* Toolbar: a row of compact icon buttons, toggles, separators and small text
 * buttons. Icon only buttons show their tooltip as their name. Items that do
 * not fit collapse, from the end, into a "more" menu; an item chosen there
 * returns true on the next frame. */
typedef struct cui_toolbar_opts { int _; bool no_border; } cui_toolbar_opts;
CUI_API void cui_toolbar_begin(cui_ctx *ui, const char *id, const cui_toolbar_opts *opts);
CUI_API void cui_toolbar_end(cui_ctx *ui);
typedef struct cui_tool_opts {
    int _;
    const char *label;         /* text on the button (icon may be NULL); also its name in the overflow menu */
    const char *tooltip;       /* the name of an icon only button */
    const char *shortcut;      /* shown in the tooltip, e.g. "Ctrl B" */
    bool *toggle;              /* toggle button: flipped on click, shown pressed while true */
    bool disabled;
} cui_tool_opts;
CUI_API bool cui_toolbar_button_ex(cui_ctx *ui, const char *icon, const cui_tool_opts *opts);
CUI_API void cui_toolbar_separator(cui_ctx *ui);
CUI_API void cui_toolbar_spacer(cui_ctx *ui);   /* pushes the following items to the right */

/* Status bar: a thin bar along the bottom of a window, with a hairline on
 * top and no fill of its own (put it in a box with the background you want).
 * Items go left to right; cui_status_bar_spacer sends the rest to the right
 * edge. */
typedef struct cui_status_bar_opts { int _; bool no_border; } cui_status_bar_opts;
CUI_API void cui_status_bar_begin(cui_ctx *ui, const char *id, const cui_status_bar_opts *opts);
CUI_API void cui_status_bar_end(cui_ctx *ui);
CUI_API void cui_status_bar_spacer(cui_ctx *ui);
typedef struct cui_status_item_opts { int _; const char *icon; cui_tone tone; const char *tooltip; bool disabled; } cui_status_item_opts;
/* A message; `tone` colors its icon. */
CUI_API void cui_status_bar_text_ex(cui_ctx *ui, const char *text, const cui_status_item_opts *opts);
/* A small clickable item ("Ln 12, Col 4", "UTF-8"). Returns true when clicked. */
CUI_API bool cui_status_bar_item_ex(cui_ctx *ui, const char *label, const cui_status_item_opts *opts);
/* A small progress bar with an optional label. value is 0..1. */
typedef struct cui_status_progress_opts { int _; const char *label; bool indeterminate; float width; } cui_status_progress_opts;
CUI_API void cui_status_bar_progress_ex(cui_ctx *ui, float value, const cui_status_progress_opts *opts);

/* Document tabs: editor style tabs. Clicking selects (*selected is
 * updated), the x or a middle click asks to close, dragging reorders, and
 * the strip scrolls sideways when the tabs do not fit. A dirty tab shows a
 * dot that turns into the close button under the pointer. The selected tab
 * is filled with the surface color so it joins the content below; the strip
 * itself has no fill, like the other bars. Tabs keep their
 * state by title, so give two tabs with the same name a key: "main.c##src".
 * The component never removes or moves your tabs itself: it reports a close
 * or a move in the result and you apply it (selection already follows a
 * move). With keyboard focus on the strip, Left and Right select, Home and
 * End jump, Shift+Left/Right move the tab and Delete closes it. With
 * `shortcuts` set it also answers Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+PageUp/Down
 * and Ctrl+W (Cmd+W on macOS) from anywhere in the window. */
typedef struct cui_doc_tab { const char *title; const char *icon; const char *tooltip; bool dirty; } cui_doc_tab;
typedef struct cui_doc_tabs_opts { int _; bool shortcuts; bool no_close; bool no_reorder; } cui_doc_tabs_opts;
typedef struct cui_doc_tabs_result {
    bool changed;              /* *selected changed (click, keys or a move) */
    int close;                 /* tab to close, or -1 */
    int move_from, move_to;    /* move the tab at move_from to move_to, or -1 */
} cui_doc_tabs_result;
CUI_API cui_doc_tabs_result cui_doc_tabs_ex(cui_ctx *ui, const char *id, const cui_doc_tab *tabs, int count, int *selected, const cui_doc_tabs_opts *opts);

/* ---- Docking ---- */

/* Dockspace: an IDE style workspace that lays out your panels in a tree of
 * resizable splits whose leaves hold one or more panels as document tabs.
 * Submit every panel each frame between cui_dockspace_begin and
 * cui_dockspace_end; the dockspace decides where it goes, and a panel's
 * content is built only while its tab is the one showing:
 *
 *     cui_dockspace_begin(ui, "main", .layout = "dock1 h0.25([Scene tree],[Viewport])");
 *     if (cui_dock_panel_begin(ui, "Inspector", &show_inspector)) {
 *         ...content...
 *         cui_dock_panel_end(ui);
 *     }
 *     cui_dockspace_end(ui);
 *
 * Drag a divider to resize. Drag a tab out of its strip to re-dock it: drop
 * targets appear over the leaf under the pointer (the middle stacks it as a
 * tab, the four sides split the leaf) and along the dockspace's outer edges,
 * with a tinted preview of where it will land; Esc cancels. A leaf that loses
 * its last panel collapses into its neighbour. The x on a tab writes
 * *open = false; set it back to true (from a View menu) and the panel
 * returns to its last place, or as a tab in the largest leaf. Pass NULL for a
 * panel that cannot be closed. Panels are identified by title ("Log (3)###log"
 * keeps the identity while the label changes), unique within a dockspace and
 * at most 63 bytes; a dockspace holds up to 48 panels. The id is global (it
 * ignores cui_push_id), so cui_dock_save and cui_dock_load can be called from
 * anywhere. Dockspaces lay out from the previous frame's size, so a panel
 * that appears or reopens shows from the next frame (one is requested).
 * After cui_dock_panel_begin, cui_last_rect gives the panel's tab. */
typedef struct cui_dockspace_opts {
    int _;
    const char *layout;        /* first run layout, in the cui_dock_save format */
    cui_size width, height;    /* default fill */
    float min_panel_width;     /* default 120; splits stop here (and share what there is when too narrow) */
    float min_panel_height;    /* default 96 */
    bool shortcuts;            /* Ctrl+Tab, Ctrl+Shift+Tab and Ctrl+W act on the focused leaf */
} cui_dockspace_opts;
CUI_API void cui_dockspace_begin_ex(cui_ctx *ui, const char *id, const cui_dockspace_opts *opts);
CUI_API void cui_dockspace_end(cui_ctx *ui);
/* Returns true while the panel's content should be built; then call
 * cui_dock_panel_end. */
typedef struct cui_dock_panel_opts { int _; const char *icon; bool dirty; } cui_dock_panel_opts;
CUI_API bool cui_dock_panel_begin_ex(cui_ctx *ui, const char *title, bool *open, const cui_dock_panel_opts *opts);
CUI_API void cui_dock_panel_end(cui_ctx *ui);
/* Saved layout: one line, e.g.
 *     dock1 h0.22([*Scene tree,Assets],v0.7([*Viewport],[*Log,-Console]))
 * h splits side by side and v stacks, the number is the first child's share,
 * [..] is a leaf's tabs in order, * the selected tab, - a closed panel; a
 * backslash escapes , [ ] ( ) and itself in names. cui_dock_save returns the full length (as
 * snprintf does) and writes what fits, or 0 when there is no layout.
 * cui_dock_load replaces the layout (and sets each panel's *open from it on
 * its next frame); names it does not know yet are kept for when they appear.
 * Malformed text returns false and changes nothing. */
CUI_API size_t cui_dock_save(cui_ctx *ui, const char *id, char *buf, size_t cap);
CUI_API bool cui_dock_load(cui_ctx *ui, const char *id, const char *text);
#ifndef __cplusplus
#define cui_dockspace_begin(ui, id, ...)    cui_dockspace_begin_ex((ui), (id), CUI__OPTS(cui_dockspace_opts, __VA_ARGS__))
#define cui_dockspace(ui, id, ...)          CUI__SCOPE(cui_dockspace_begin_ex((ui), (id), CUI__OPTS(cui_dockspace_opts, __VA_ARGS__)), cui_dockspace_end(ui))
#define cui_dock_panel_begin(ui, title, open, ...) cui_dock_panel_begin_ex((ui), (title), (open), CUI__OPTS(cui_dock_panel_opts, __VA_ARGS__))
#define cui_dock_panel(ui, title, open, ...) CUI__SCOPE_IF(cui_dock_panel_begin_ex((ui), (title), (open), CUI__OPTS(cui_dock_panel_opts, __VA_ARGS__)), cui_dock_panel_end(ui))
#endif
/* Tool panels: property inspector, log console                             */
/* ------------------------------------------------------------------------ */

/* Property inspector: labelled rows in folding groups, a label column on the
 * left (drag the line between the columns to resize it) and an editor on the
 * right. Every row returns true when its value changed. Groups end at the
 * next group or at cui_inspector_end.
 *
 *     static bool transform_open = true;
 *     if (cui_inspector_begin(ui, "props")) {
 *         if (cui_inspector_group(ui, "Transform", &transform_open)) {
 *             cui_prop_vec3(ui, "Position", pos);
 *             cui_prop_float(ui, "Scale", &scale, .min = 0.01f, .max = 100, .step = 0.01f);
 *         }
 *         cui_inspector_end(ui);
 *     }
 *
 * Number rows are scrub fields: drag the field or its label sideways (Shift
 * for fine steps), click to type, arrows step. Rows can also be used outside
 * an inspector. */
typedef struct cui_inspector_opts {
    int _;
    cui_size width;            /* default fill */
    float label_width;         /* initial label column in points, default 38% of the width */
} cui_inspector_opts;
CUI_API bool cui_inspector_begin_ex(cui_ctx *ui, const char *id, const cui_inspector_opts *opts);
CUI_API void cui_inspector_end(cui_ctx *ui);
/* A folding group header. Build its rows only when this returns true (it
 * stays true while the group animates closed). open may be NULL for a plain
 * section that does not fold. */
CUI_API bool cui_inspector_group(cui_ctx *ui, const char *title, bool *open);

typedef struct cui_prop_opts {
    int _;
    float min, max;            /* number and vector rows; both 0 = unbounded */
    float step;                /* drag and arrow step, default 1/100 of the range (0.1 unbounded) */
    const char *format;        /* printf format, default from the step ("%.2f" for 0.01) */
    const char *suffix;        /* unit after the value, e.g. "px" or "deg" */
    const char *placeholder;   /* text rows */
    const char *tooltip;       /* shown on the label */
    bool disabled;
    bool checkbox;             /* bool rows: a checkbox instead of a switch */
} cui_prop_opts;
CUI_API bool cui_prop_float_ex(cui_ctx *ui, const char *label, float *value, const cui_prop_opts *opts);
CUI_API bool cui_prop_int_ex(cui_ctx *ui, const char *label, int *value, const cui_prop_opts *opts);
CUI_API bool cui_prop_text_ex(cui_ctx *ui, const char *label, char *buf, size_t cap, const cui_prop_opts *opts);
CUI_API bool cui_prop_bool_ex(cui_ctx *ui, const char *label, bool *value, const cui_prop_opts *opts);
CUI_API bool cui_prop_enum_ex(cui_ctx *ui, const char *label, const char *const *items, int count, int *index, const cui_prop_opts *opts);
/* A swatch and hex value that opens the color picker. */
CUI_API bool cui_prop_color_ex(cui_ctx *ui, const char *label, cui_color *value, const cui_prop_opts *opts);
/* Compact X, Y (Z, W) scrub fields side by side; value holds 2, 3 or 4 floats. */
CUI_API bool cui_prop_vec_ex(cui_ctx *ui, const char *label, float *value, int components, const cui_prop_opts *opts);

/* Log console. The app owns the storage: give cui_log_init an array of line
 * records and a byte buffer for the text. Appending never allocates; when
 * either is full the oldest lines are dropped (a ring buffer). Lines longer
 * than the text buffer are cut to fit.
 *
 *     static cui_log_line lines[4096];
 *     static char text[256 * 1024];
 *     static cui_log_buffer log;
 *     cui_log_init(&log, lines, 4096, text, sizeof text);
 *     log.now = seconds_since_start();             // optional timestamp source
 *     cui_log_append(&log, CUI_LOG_WARN, "Shader %s took %d ms", name, ms);
 *     ...
 *     cui_log_view(ui, "log", &log, .height = 320, .timestamps = true);
 *
 * Each append is one line: a trailing newline is dropped and other control
 * characters show as spaces. The view keeps its own filter, search, follow
 * and selection state (a small index of the matching lines, at most one entry
 * per line slot, freed with the context). */
typedef enum cui_log_level { CUI_LOG_TRACE = 0, CUI_LOG_DEBUG, CUI_LOG_INFO, CUI_LOG_WARN, CUI_LOG_ERROR, CUI_LOG_LEVEL_COUNT } cui_log_level;
typedef struct cui_log_line { uint32_t offset, len; double time; uint8_t level; } cui_log_line;
typedef struct cui_log_buffer {
    double now;                /* timestamp given to the next appended line (seconds) */
    /* everything below is managed by cui_log_*; read it, do not write it */
    cui_log_line *lines; int line_cap;
    char *text; uint32_t text_cap;
    int first, count;          /* ring of lines: oldest record and how many */
    uint32_t text_end;         /* where the next line's text goes */
    uint64_t total;            /* lines ever appended; line i of the buffer is number total - count + i */
    int level_count[CUI_LOG_LEVEL_COUNT];
} cui_log_buffer;
CUI_API void cui_log_init(cui_log_buffer *buf, cui_log_line *lines, int line_cap, char *text, size_t text_cap);
CUI_API void cui_log_append(cui_log_buffer *buf, cui_log_level level, const char *fmt, ...) CUI_PRINTF(3, 4);
CUI_API void cui_log_appendv(cui_log_buffer *buf, cui_log_level level, const char *fmt, va_list ap);
/* Appends len bytes (-1: up to the terminator) stamped with time. */
CUI_API void cui_log_append_text(cui_log_buffer *buf, cui_log_level level, double time, const char *text, int len);
CUI_API void cui_log_clear(cui_log_buffer *buf);
/* Line i (0 = oldest), or NULL when out of range. *text is terminated (and len bytes long). */
CUI_API const cui_log_line *cui_log_get(const cui_log_buffer *buf, int i, const char **text);

typedef struct cui_log_view_opts {
    int _;
    cui_size width;            /* default fill */
    float height;              /* default 320; negative fills the space */
    bool timestamps;           /* show each line's time as HH:MM:SS.mmm */
    bool no_toolbar;           /* just the lines */
    float size;                /* font size, default 12 */
} cui_log_view_opts;
/* Toolbar: level filters, a search field that filters and highlights,
 * Follow (keeps the newest line in view; scrolling up turns it off, reaching
 * the bottom turns it back on), Copy (selected lines, or all shown) and Clear.
 * Click a line to select it, Shift+click for a range; with the list focused,
 * arrows move, Shift+arrows extend, Ctrl+A selects all, Ctrl+C copies, Esc
 * clears the selection, End follows again. */
CUI_API void cui_log_view_ex(cui_ctx *ui, const char *id, cui_log_buffer *buf, const cui_log_view_opts *opts);

/* ---- Terminal ---- */
/* Terminal: program output with ANSI colors over an input line with a
 * prompt and history, for consoles, REPLs and build tools. Output goes into
 * a buffer you own (appending never allocates; the oldest lines go when it
 * is full) and is parsed as it arrives: \n starts a line, \r returns to its
 * start (progress bars redraw in place), \t, \b, and ESC [ ... m sets
 * colors and styles (16 colors, 256 and 24-bit; bold, dim, italic,
 * underline, inverse). Other escape sequences are skipped.
 *
 *     static char text[512 * 1024]; static cui_terminal_line lines[8192];
 *     static cui_terminal_buffer term;
 *     cui_terminal_init(&term, text, sizeof text, lines, 8192);    // once
 *     cui_terminal_write(&term, output, n);                          // as output arrives
 *     cui_terminal_result r = cui_terminal(ui, "term", &term, .height = 360);
 *     if (r.command) run_command(r.command);                         // echoed for you
 *
 * Long lines wrap. The view follows the newest line until you scroll up.
 * Drag to select, Ctrl+C copies the selection (or, with none, sets
 * r.interrupt), Ctrl+L clears, Up and Down walk the history and clicking the
 * output puts the keyboard in the input line. Only the rows in view are
 * drawn. Text is left to right, one cell per character. */
typedef struct cui_terminal_line { uint32_t offset, len, cols; } cui_terminal_line;
typedef struct cui_terminal_buffer {
    /* managed by cui_terminal_*; read it, do not write it */
    char *text; uint32_t text_cap;
    cui_terminal_line *lines; int line_cap;
    int first, count;          /* ring of lines; the last one is still being written */
    uint32_t end;              /* where the next byte goes */
    uint64_t total;            /* lines ever started */
    uint8_t mode;              /* parser: text, escape, CSI or OSC */
    uint8_t seq_n; char seq[30];   /* the escape sequence being read, so a write can split one */
    bool cr;                   /* a \r was seen: the next character starts the line over */
} cui_terminal_buffer;
CUI_API void cui_terminal_init(cui_terminal_buffer *t, char *text, size_t text_cap, cui_terminal_line *lines, int line_cap);
CUI_API void cui_terminal_write(cui_terminal_buffer *t, const char *data, int len);   /* len -1: up to the terminator */
CUI_API void cui_terminal_printf(cui_terminal_buffer *t, const char *fmt, ...) CUI_PRINTF(2, 3);
CUI_API void cui_terminal_clear(cui_terminal_buffer *t);
typedef struct cui_terminal_opts {
    int _;
    cui_size width;            /* default fill */
    float height;              /* default 320; negative fills the space */
    float size;                /* font size, default 13 */
    const char *prompt;        /* default "$ " */
    bool no_input;             /* output only */
} cui_terminal_opts;
typedef struct cui_terminal_result {
    const char *command;       /* the line entered (Enter), already echoed; valid this frame */
    bool interrupt;            /* Ctrl+C with nothing selected */
} cui_terminal_result;
CUI_API cui_terminal_result cui_terminal_ex(cui_ctx *ui, const char *id, cui_terminal_buffer *t, const cui_terminal_opts *opts);

/* ---- Emoji ----
 * Color emoji come from an emoji font: the system's (cui_emoji_font_load_system, which
 * the picker calls by itself when nothing is loaded) or one the app ships. Bitmap color
 * fonts (sbix as on macOS, CBDT as on Linux and Android) and layered ones (COLR version 0,
 * as on Windows) are read, as is a plain outline font for one-color emoji. Sequences with
 * skin tones, joined people and flags are looked up through the font's ligatures. Big
 * fonts are read from the file as needed rather than loaded whole. Drawn emoji are kept in
 * one texture and reused. */
CUI_API bool cui_emoji_font_load_file(cui_ctx *ui, const char *path);
CUI_API bool cui_emoji_font_load_memory(cui_ctx *ui, const void *data, size_t size, bool copy);
CUI_API bool cui_emoji_font_load_system(cui_ctx *ui);
CUI_API bool cui_emoji_supported(cui_ctx *ui, const char *utf8, int len);   /* the font (or platform) can draw it */
CUI_API void cui_draw_emoji(cui_ctx *ui, const char *utf8, int len, cui_rect r);
CUI_API void cui_emoji(cui_ctx *ui, const char *utf8, float size);           /* one emoji as a component */

/* Emoji picker: search, groups with tabs, recent emoji, skin tones and a preview with the
 * name. Returns the emoji picked this frame (UTF-8, valid until the next frame) or NULL.
 * Put it in a popover to open it from a button. */
typedef struct cui_emoji_picker_opts {
    int _;
    cui_size width;                   /* default up to 360 */
    float height;                     /* the grid; default 280 */
    float size;                       /* emoji size; default 26 */
    char *recent; size_t recent_cap;  /* the app's copy of recent emoji, newest first, space separated */
    int *tone;                        /* skin tone, 0 (none) to 5 (dark), kept by the app */
    const char *placeholder;          /* "Search emoji" */
    const char *empty_text;           /* "No emoji found" */
    const char *hint;                 /* the preview with nothing to show: "Pick an emoji" */
    const char *const *group_names;   /* 10 names: recent, then Unicode's nine groups */
    bool no_search, no_preview, no_tone;
} cui_emoji_picker_opts;
CUI_API const char *cui_emoji_picker_ex(cui_ctx *ui, const char *id, const cui_emoji_picker_opts *opts);
#define cui_emoji_picker(ui, id, ...) cui_emoji_picker_ex((ui), (id), CUI__OPTS(cui_emoji_picker_opts, __VA_ARGS__))

/* ---- Hex viewer ---- */

/* Hex viewer: offsets, bytes in hex and the same bytes as ASCII, in the code
 * font, for binary tools, emulators and debuggers. You own the bytes (they
 * are read during the call, never kept) and, optionally, the selection:
 *
 *     static cui_hex_selection sel;
 *     static const cui_hex_range marks[] = {
 *         { 0, 8, CUI_TONE_INFO, "PNG signature" },
 *         { 8, 25, CUI_TONE_VIOLET, "IHDR chunk" },
 *     };
 *     cui_hex_result r = cui_hex_view(ui, "hex", file, file_size, &sel,
 *                                     .base = 0x80000000u, .ranges = marks, .range_count = 2);
 *     if (r.selection_changed) show_inspector(file + sel.start, sel.length);
 *
 * Only the rows in view are built, so a file of many megabytes scrolls as
 * fast as a short one (the scroll position is kept in rows, so very large
 * files stay exact). Rows hold 16 bytes (8 or 32 with bytes_per_row) with a
 * small gap every 8. When the view is too narrow it drops to fewer bytes per
 * row (down to 8), then hides the ASCII column, then tightens its margins,
 * then shows 4 bytes per row; it never scrolls sideways. Hovering a byte marks it in both columns, and a
 * byte inside a highlighted range shows the range's label as a tooltip.
 *
 * Click a byte to select it, drag or Shift+click for a range. With the view
 * focused: arrows move the cursor byte, Shift+arrows extend, Page Up/Down
 * move by a screen, Home/End go to the start/end of the row and Ctrl+Home/End
 * to the start/end of the data, Ctrl+A selects everything, Esc clears the
 * selection. Ctrl+C copies the selection as hex ("89 50 4E 47") and
 * Ctrl+Shift+C as ASCII (other bytes as '.'), up to 4 MiB of bytes.
 *
 * With .edit set the view is editable: typing hex digits overwrites the byte
 * at the cursor, high digit first, then moves on. Edits never change the size. */
typedef struct cui_hex_range {
    size_t offset, length;
    cui_tone tone;             /* tints the range's background */
    const char *label;         /* tooltip for the range; may be NULL */
} cui_hex_range;
/* The selected bytes. length 0 means nothing is selected; cursor is the byte
 * the keys move from and is one end of a selection. Set it yourself to select
 * from code. */
typedef struct cui_hex_selection { size_t start, length, cursor; } cui_hex_selection;
typedef struct cui_hex_view_opts {
    int _;
    cui_size width;            /* default fill */
    float height;              /* default: the rows, up to 320; negative fills the space */
    uint64_t base;             /* address shown for byte 0 in the offset column */
    int bytes_per_row;         /* 8, 16 (default) or 32 */
    const cui_hex_range *ranges; int range_count;   /* drawn in order, later ones on top */
    uint8_t *edit;             /* writable bytes to show and overwrite instead of data (usually the same memory), or NULL for read only */
    bool no_ascii;             /* hide the ASCII column */
    bool no_header;            /* hide the column header (00 01 .. 0F) */
    float size;                /* font size, default 12 */
} cui_hex_view_opts;
typedef struct cui_hex_result {
    cui_hex_selection selection;   /* after this frame (the same as *sel when you pass one) */
    bool selection_changed;        /* by the pointer, the keys or cui_hex_view_goto */
    bool edited;                   /* bytes in .edit were overwritten this frame */
    size_t edit_offset, edit_length;
    int64_t hovered;               /* byte under the pointer, or -1 */
    bool copied;                   /* Ctrl+C or Ctrl+Shift+C put the selection on the clipboard */
} cui_hex_result;
/* sel may be NULL: the view then keeps the selection itself. */
CUI_API cui_hex_result cui_hex_view_ex(cui_ctx *ui, const char *id, const uint8_t *data, size_t size, cui_hex_selection *sel, const cui_hex_view_opts *opts);
/* Go to offset: on the view's next frame its cursor moves to offset, that byte
 * becomes the selection and it scrolls into view (to the middle when it was
 * out of sight). Wire a "Go to" field to it; it is safe to call every frame. */
CUI_API void cui_hex_view_goto(cui_ctx *ui, const char *id, size_t offset);
/* ---- Flame graph ---- */

/* Flame graph / profiler timeline: time runs left to right, call depth top to
 * bottom (row d holds the spans at depth d). You own the spans, the view and
 * the selection; the component pans and zooms *view and writes *selected.
 *
 *     static cui_flame_view view;              (zeroed: fits every span)
 *     static int selected = -1;
 *     cui_flame_sort(spans, count);            (once, after recording)
 *     cui_flame_result r = cui_flame_graph(ui, "profile", spans, count, &view, &selected);
 *     if (r.selection_changed && selected >= 0) show_details(&spans[selected]);
 *
 * Times are seconds (any origin; the ruler counts from the earliest span).
 * Spans must be sorted by depth, then start (cui_flame_sort does it), and
 * spans of one depth must not overlap: a child lies within its parent. Only
 * the spans in view are visited (binary searches per row), so captures of
 * 100k spans stay fast; spans thinner than a pixel merge into a neutral run.
 * The spans need to be valid only during the call. color_key picks the color
 * (0: hashed from the name), so the same function keeps its color.
 *
 * Mouse: the wheel zooms around the pointer, Shift+wheel or a sideways scroll
 * pans, drag pans (sideways and, for deep stacks, up and down). Click selects
 * a span (empty space clears), double-click zooms to fit it (on empty space:
 * fits everything). Hover shows a tooltip: name, duration and its share of
 * the parent (or of the view, at depth 0).
 * Keys when focused: Left/Right move to the previous/next span in the row,
 * Up to the parent, Down to the first child (the view follows); Shift+Left/
 * Right pan; + and - zoom; Enter or Space zooms to the selection; Home or F
 * fits everything; Esc fits everything, or when already fitted clears the
 * selection. Zooming to a span or fitting animates (snaps with reduced
 * motion). Not included: an aggregated (merged call tree) flame graph mode. */
typedef struct cui_flame_span {
    const char *name;
    double start, end;         /* seconds, end >= start */
    int depth;                 /* row, 0 = top */
    uint32_t color_key;        /* 0: hashed from name */
} cui_flame_span;
typedef struct cui_flame_view {
    double start, end;         /* visible time range; end <= start fits every span */
    float scroll;              /* rows scrolled off the top, in points */
} cui_flame_view;
typedef struct cui_flame_opts {
    int _;
    cui_size width;            /* default fill */
    float height;              /* default: fits the rows (up to 360) */
    float row_height;          /* default 20 */
    double min_range;          /* deepest zoom in seconds, default 1e-8 */
} cui_flame_opts;
typedef struct cui_flame_result {
    bool selection_changed;    /* *selected changed (click or keys) */
    bool view_changed;         /* *view changed (zoom, pan, fit, animation) */
    int hovered;               /* span under the pointer, or -1 */
    int zoomed;                /* span zoomed to this frame (double-click, Enter), or -1 */
} cui_flame_result;
/* selected: index into spans, -1 for none (may be NULL). */
CUI_API cui_flame_result cui_flame_graph_ex(cui_ctx *ui, const char *id, const cui_flame_span *spans, int count,
                                            cui_flame_view *view, int *selected, const cui_flame_opts *opts);
/* Sorts spans into the order the graph needs: by depth, then start (longer
 * first on ties). */
CUI_API void cui_flame_sort(cui_flame_span *spans, int count);
/* Where a span was drawn in the latest frame, in points (zero size when out
 * of view). flame is the graph's id: cui_last_id(ui) right after the call. */
CUI_API cui_rect cui_flame_span_rect(cui_ctx *ui, cui_id flame, const cui_flame_span *span);
/* ---- Curve and gradient editors ---- */

/* Curve editor: keyframes on a plot, for easing, animation timing and
 * response curves. You own the array of keys, sorted by x; the editor edits
 * it in place (moves, handles, modes, adds and removes) and reports what it
 * did in the result, so data you keep beside each key can follow:
 *
 *     static cui_curve_key ease[8] = { { 0, 0 }, { 0.42f, 0.1f }, { 1, 1 } };
 *     static int n = 3, sel = -1;
 *     cui_curve_result r = cui_curve_editor(ui, "ease", ease, &n, 8, .selected = &sel);
 *     if (r.added >= 0) insert_my_data(r.added);
 *     if (r.removed >= 0) remove_my_data(r.removed);
 *     ...
 *     float y = cui_curve_eval(ease, n, t);   (the same curve the editor draws)
 *
 * The mode of a key shapes the segment that leaves it: SMOOTH and FREE are
 * cubic beziers, LINEAR a straight line, STEP holds the key's value until the
 * next key. A SMOOTH key computes its tangents (no overshoot, flat at the
 * ends and at peaks); a FREE key uses its handles, which are relative to the
 * key in curve units (in_dx <= 0, out_dx >= 0) and are shortened to the
 * neighbouring key so the curve never folds back. The incoming segment of a
 * LINEAR or STEP key arrives with an automatic tangent. Before the first key
 * the curve holds the first value, after the last key the last value.
 *
 * Drag a key to move it (x stays between its neighbours, both axes inside the
 * range); drag the handles of the selected key (a SMOOTH key becomes FREE);
 * Shift or `snap` snaps a drag to the grid. Double click empty space to add
 * a key (on the curve when the click is near it), Delete or Backspace removes
 * the selected key (never fewer than two). Keys are focusable: Tab moves
 * through them, the arrows nudge (Shift for fine steps). Hovering shows the
 * value under the pointer. The row under the plot sets the selected key's
 * mode and exact position (its fields stack on a narrow editor). */
typedef enum cui_curve_mode {
    CUI_CURVE_SMOOTH = 0,      /* automatic tangents */
    CUI_CURVE_FREE,            /* the key's own handles */
    CUI_CURVE_LINEAR,          /* a straight line to the next key */
    CUI_CURVE_STEP             /* hold this value until the next key */
} cui_curve_mode;
typedef struct cui_curve_key {
    float x, y;
    float in_dx, in_dy;        /* handle toward the previous key, relative to the key (FREE) */
    float out_dx, out_dy;      /* handle toward the next key, relative to the key (FREE) */
    uint8_t mode;              /* cui_curve_mode */
} cui_curve_key;
typedef struct cui_curve_opts {
    int _;
    float x_min, x_max;        /* plotted range; both 0 = 0..1. Keys stay inside it */
    float y_min, y_max;        /* both 0 = 0..1; leave room above and below for overshoot */
    cui_size width;            /* default fill */
    float height;              /* plot height, default 220 */
    const char *x_format, *y_format;   /* printf formats for labels and readouts, default from the grid step */
    const char *x_label, *y_label;     /* readout names, default "X" and "Y" (e.g. "Time", "Value") */
    int *selected;             /* the selected key or -1, kept by you; NULL keeps it in the editor */
    bool snap;                 /* drags always snap to the grid (otherwise hold Shift) */
    bool show_playhead;        /* a marker at x = playhead */
    float playhead;
} cui_curve_opts;
typedef struct cui_curve_result {
    bool changed;              /* a key moved, a handle or mode changed, or a key was added or removed */
    int added;                 /* index of the key added this frame, or -1 */
    int removed;               /* index the removed key had, or -1 */
    int selected;              /* the selected key, or -1 */
    bool hovered;              /* the pointer is over the plot: */
    float hover_x, hover_y;    /* the x under it and the curve's value there */
} cui_curve_result;
CUI_API cui_curve_result cui_curve_editor_ex(cui_ctx *ui, const char *id, cui_curve_key *keys, int *count, int capacity, const cui_curve_opts *opts);
/* The curve's value at x: exactly what the editor draws. keys sorted by x. */
CUI_API float cui_curve_eval(const cui_curve_key *keys, int count, float x);

/* Gradient editor: a color ramp over a checkerboard (so alpha shows) with
 * the stops underneath. You own the stops; the editor edits them in place
 * and keeps them sorted by position. Drag a stop to move it (it may pass its
 * neighbours), drag it off the ramp or press Delete to remove it (never
 * fewer than two), click the ramp or the empty track to add a stop with the
 * color found there. Double click a stop (or Enter on a focused one) to open
 * the color picker; the row under the ramp shows the selected stop's color,
 * location and opacity. Stops are focusable: Tab moves through them, Left and
 * Right nudge (Shift for fine steps). */
typedef struct cui_gradient_stop { float pos; cui_color color; } cui_gradient_stop;   /* pos 0..1 */
typedef struct cui_gradient_opts {
    int _;
    cui_size width;            /* default fill */
    float height;              /* ramp height, default 28 */
    int *selected;             /* the selected stop, kept by you; NULL keeps it in the editor */
} cui_gradient_opts;
typedef struct cui_gradient_result {
    bool changed;              /* a stop moved, changed color, or was added or removed */
    int added;                 /* index of the stop added this frame, or -1 */
    int removed;               /* index the removed stop had, or -1 */
    int selected;              /* the selected stop */
} cui_gradient_result;
CUI_API cui_gradient_result cui_gradient_editor_ex(cui_ctx *ui, const char *id, cui_gradient_stop *stops, int *count, int capacity, const cui_gradient_opts *opts);
/* The color at t: exactly what the ramp shows (each channel, alpha too,
 * interpolated in sRGB between the stops on either side; the end colors
 * continue past the first and last stop). */
CUI_API cui_color cui_gradient_eval(const cui_gradient_stop *stops, int count, float t);

/* ---- Timeline ---- */

/* Timeline editor (dope sheet): a track list on the left, a time ruler on
 * top and a row per track with its keys (and clips, for video and audio
 * tracks). You own the tracks, their keys and clips, the playhead time and
 * the selection; the editor edits them in place and reports what it did:
 *
 *     static cui_curve_key rot[32] = { { 0, 0 }, { 1, 30 }, { 2, 0 } };
 *     static bool rot_sel[32];                   (parallel to the keys)
 *     static cui_timeline_track tracks[] = {
 *         { "Hero", CUI_ICON_USER, 0 },
 *         { "Rotation", NULL, 1, .keys = rot, .key_count = 3, .key_capacity = 32, .key_selected = rot_sel },
 *     };
 *     static float t;
 *     cui_timeline_editor_result r = cui_timeline_editor(ui, "anim", tracks, 2, &t, .end = 4, .fps = 24);
 *     if (r.play_toggled) playing = !playing;    (you advance t while playing)
 *     float angle = cui_curve_eval(rot, tracks[1].key_count, t);
 *
 * Keys are curve keys (x is the time in seconds, y the value), so a track's
 * array goes straight into cui_curve_editor and cui_curve_eval; the mode sets
 * the mark: a diamond for SMOOTH and FREE, a circle for LINEAR, a square for
 * STEP. Keys stay sorted by time and inside start..end. A track whose next
 * tracks are deeper is a group: a chevron collapses it and its row shows
 * its tracks' keys as small marks. Only the rows in view are visited and
 * only the keys in view are searched (binary searches), so thousands of
 * tracks stay fast.
 *
 * Mouse: click the ruler or drag in it to scrub (snapped to frames); click a
 * key to select it, Shift+click adds, Ctrl+click toggles; drag in empty
 * space for a box selection (Shift or Ctrl adds to the selection); drag
 * selected keys to move them, by whole frames (hold Shift to move freely);
 * double-click a row to add a key there (on the curve, so the motion is
 * kept); drag a clip to move it, its ends to trim it; click a track name to
 * select its keys. The wheel zooms time around the pointer, Shift+wheel or a
 * sideways scroll pans, the wheel over the track list scrolls the rows, and
 * a middle-button drag pans both ways. Hovering a key shows its time and
 * value.
 * Keys when focused: Left/Right step the playhead a frame (Shift: 10),
 * Alt+Left/Right move the selected keys a frame (Shift: 10), Up/Down jump to
 * the previous/next key, Home/End to the start/end, Space reports a play
 * request, Delete or Backspace removes the selected keys and clips, Ctrl+A
 * selects every key on the visible tracks, Esc clears the selection, F fits
 * the whole sequence (animated, snaps with reduced motion), + and - zoom
 * around the playhead. The view follows the playhead when it leaves the
 * view. */
typedef enum cui_timeline_ruler {
    CUI_TIMELINE_FRAMES = 0,   /* frame numbers */
    CUI_TIMELINE_SECONDS,      /* 1.5 s */
    CUI_TIMELINE_TIMECODE      /* minutes:seconds:frames (hours too past an hour) */
} cui_timeline_ruler;
typedef struct cui_timeline_clip {
    float start, end;          /* seconds, end > start */
    float offset;              /* where the clip starts in its media; trimming the start moves it along */
    const char *name;
    bool selected;
} cui_timeline_clip;
typedef struct cui_timeline_track {
    const char *name;
    const char *icon;          /* optional, before the name */
    int depth;                 /* 0 top level; the deeper tracks after a track belong to it (a group) */
    bool collapsed;            /* a group hides its tracks (its chevron toggles this) */
    bool muted, locked;        /* toggled by the row's buttons (.toggles); a locked track can't be edited */
    uint8_t color;             /* series color 1..6; 0 takes the group's, or the next one at the top level */
    cui_curve_key *keys;       /* sorted by x: x is the time in seconds, y the value (may be NULL) */
    int key_count, key_capacity;
    bool *key_selected;        /* parallel to keys, kept by you; NULL: the keys are shown but not selectable */
    cui_timeline_clip *clips;  /* sorted by start, not overlapping (may be NULL) */
    int clip_count;
} cui_timeline_track;
typedef struct cui_timeline_view {
    float start, end;          /* visible time range; end <= start fits the sequence */
    float scroll;              /* rows scrolled off the top, in points */
} cui_timeline_view;
typedef struct cui_timeline_editor_opts {
    int _;
    float start, end;          /* the sequence in seconds; both 0 = 0..10 */
    float fps;                 /* frames per second, default 30: snapping, stepping and frame labels */
    int ruler;                 /* cui_timeline_ruler */
    cui_timeline_view *view;   /* NULL keeps it in the editor */
    float *in, *out;           /* a play range: with both set, markers to drag in the ruler and shading outside */
    int *current_track, *current_key;   /* the key last clicked or added, or -1; kept by you (NULL: in the editor) */
    cui_size width;            /* default fill */
    float height;              /* default: fits every track (up to 360) */
    float row_height;          /* default 26 */
    float track_width;         /* track list, default 184; narrower (then icons only) on a narrow editor */
    bool toggles;              /* hide and lock buttons on every track */
    bool free_drag;            /* drags and scrubbing do not snap to frames */
} cui_timeline_editor_opts;
typedef struct cui_timeline_editor_result {
    bool changed;              /* keys or clips moved, were added or removed, or a track flag changed */
    bool time_changed;         /* *time moved (scrub, ruler click, keys) */
    bool scrubbing;            /* the playhead is being dragged */
    bool play_toggled;         /* Space: start or stop playback */
    bool selection_changed;
    bool view_changed;
    bool keys_moved;           /* selected keys moved this frame (drag or Alt+arrows) */
    int added_track, added_key;  /* the key added this frame (double click), or -1 */
    int removed_keys, removed_clips;   /* how many were removed this frame (Delete) */
    int clip_track, clip;      /* the clip moved or trimmed this frame, or -1 */
    int toggled;               /* the track whose collapsed, muted or locked flag changed, or -1 */
    int current_track, current_key;
    int hovered_track, hovered_key;    /* the key under the pointer, or -1 */
} cui_timeline_editor_result;
/* time: the playhead in seconds (may be NULL). */
CUI_API cui_timeline_editor_result cui_timeline_editor_ex(cui_ctx *ui, const char *id, cui_timeline_track *tracks, int count,
                                                          float *time, const cui_timeline_editor_opts *opts);
#ifndef __cplusplus
#define cui_timeline_editor(ui, id, tracks, count, time, ...) \
    cui_timeline_editor_ex((ui), (id), (tracks), (count), (time), CUI__OPTS(cui_timeline_editor_opts, __VA_ARGS__))
#endif

/* ------------------------------------------------------------------------ */
/* Custom drawing                                                           */
/* ------------------------------------------------------------------------ */

typedef struct cui_corners { float tl, tr, br, bl; } cui_corners;
#define CUI_CORNERS(r) (CUI__LIT(cui_corners){ (r), (r), (r), (r) })

CUI_API void cui_draw_rect(cui_ctx *ui, cui_rect r, cui_corners radius, cui_color fill);
CUI_API void cui_draw_rect_border(cui_ctx *ui, cui_rect r, cui_corners radius, cui_color fill, cui_color border, float border_width);
CUI_API void cui_draw_rect_gradient(cui_ctx *ui, cui_rect r, cui_corners radius, cui_color top, cui_color bottom);
CUI_API void cui_draw_shadow(cui_ctx *ui, cui_rect r, cui_corners radius, float blur, float spread, cui_vec2 offset, cui_color color);
CUI_API void cui_draw_line(cui_ctx *ui, cui_vec2 a, cui_vec2 b, float thickness, cui_color color);
CUI_API void cui_draw_polyline(cui_ctx *ui, const cui_vec2 *points, int count, float thickness, cui_color color, bool closed);
CUI_API void cui_draw_polygon(cui_ctx *ui, const cui_vec2 *points, int count, cui_color color);   /* convex */
CUI_API void cui_draw_circle(cui_ctx *ui, cui_vec2 center, float radius, cui_color color);
CUI_API void cui_draw_arc(cui_ctx *ui, cui_vec2 center, float radius, float a0, float a1, float thickness, cui_color color);
CUI_API void cui_draw_text(cui_ctx *ui, cui_font font, float size, cui_vec2 pos, cui_color color, const char *text, int len);
CUI_API void cui_draw_icon(cui_ctx *ui, const char *icon, cui_rect r, float size, cui_color color);
CUI_API void cui_draw_image(cui_ctx *ui, cui_texture tex, cui_rect r, cui_rect uv, cui_corners radius, cui_color tint);
CUI_API void cui_push_clip(cui_ctx *ui, cui_rect r);
CUI_API void cui_pop_clip(cui_ctx *ui);

/* Canvas: a box of the given size that you paint yourself. The callback
 * runs after layout, with the final rectangle. Returns interaction info. */
typedef void (*cui_paint_fn)(cui_ctx *ui, cui_rect rect, void *user);
typedef struct cui_canvas_result { cui_rect rect; bool hovered, pressed, clicked; cui_vec2 mouse; } cui_canvas_result;
CUI_API cui_canvas_result cui_canvas(cui_ctx *ui, const char *id, cui_size width, cui_size height, cui_paint_fn paint, const void *user, size_t user_size);

/* Persistent per-id storage for your own components (zeroed on first use).
 * State not used for about 300 frames is freed (and zeroed again if it comes
 * back), so it suits what can be rebuilt. For resources held in it, such as
 * textures, set a function that frees them: it runs when the state goes or
 * the context is destroyed. */
CUI_API void *cui_state(cui_ctx *ui, cui_id id, size_t size);
CUI_API void cui_state_on_free(cui_ctx *ui, cui_id id, void (*fn)(cui_ctx *ui, void *data));

/* Animation helpers. Values move toward target with a spring and request
 * frames while moving. Reduced motion makes them snap. */
CUI_API float cui_animate(cui_ctx *ui, cui_id id, float target);                 /* smooth, no overshoot */
CUI_API float cui_animate_spring(cui_ctx *ui, cui_id id, float target, float duration, float bounce);

/* ------------------------------------------------------------------------ */
/* Draw data (consumed by renderers)                                        */
/* ------------------------------------------------------------------------ */

/* Vertex shading modes. See docs/rendering.md for the reference shader. */
enum { CUI_SHADE_TEXT = 0, CUI_SHADE_RRECT = 1, CUI_SHADE_SHADOW = 2, CUI_SHADE_IMAGE = 3, CUI_SHADE_SOLID = 4 };

typedef struct cui_vertex {
    float x, y;            /* position in pixels */
    float u, v;            /* texture coordinate */
    float lx, ly;          /* position relative to the shape center, pixels */
    float hw, hh;          /* half size of the shape, pixels */
    float r_tl, r_tr, r_br, r_bl;  /* corner radii, pixels */
    float border;          /* border width (RRECT) or blur sigma (SHADOW) */
    float mode;            /* CUI_SHADE_* */
    cui_color color;       /* fill */
    cui_color border_color;
} cui_vertex;

typedef struct cui_draw_cmd {
    cui_rect clip;         /* pixels */
    cui_texture texture;   /* 0 = font atlas */
    uint32_t index_offset;
    uint32_t index_count;
} cui_draw_cmd;

typedef struct cui_draw_data {
    const cui_vertex *vertices;
    int vertex_count;
    const uint32_t *indices;
    int index_count;
    const cui_draw_cmd *commands;
    int command_count;
    int width_px, height_px;   /* framebuffer size */
    cui_color clear_color;
    cui_texture font_atlas;
} cui_draw_data;

/* Valid after cui_end_frame until the next cui_begin_frame. */
CUI_API const cui_draw_data *cui_get_draw_data(cui_ctx *ui);

/* ------------------------------------------------------------------------ */
/* Named-argument macros                                                    */
/* ------------------------------------------------------------------------ */

#ifndef __cplusplus
#define CUI__OPTS(type, ...) (&(type){ ._ = 0, __VA_ARGS__ })
#define CUI__CAT2(a, b) a##b
#define CUI__CAT(a, b) CUI__CAT2(a, b)
#define CUI__V CUI__CAT(cui__scope_, __LINE__)
/* Runs the body once between begin and end. */
#define CUI__SCOPE(begin, end) for (int CUI__V = ((void)(begin), 0); !CUI__V; CUI__V = 1, (end))
/* Runs the body (and end) only when begin returns true. */
#define CUI__SCOPE_IF(begin, end) for (int CUI__V = !(begin); !CUI__V; CUI__V = 1, (end))

#define cui_box(ui, ...)        CUI__SCOPE(cui_box_begin((ui), CUI__OPTS(cui_box, __VA_ARGS__)), cui_box_end(ui))
#define cui_row(ui, ...)        CUI__SCOPE(cui_row_begin((ui), CUI__OPTS(cui_box, __VA_ARGS__)), cui_box_end(ui))
#define cui_column(ui, ...)     CUI__SCOPE(cui_column_begin((ui), CUI__OPTS(cui_box, __VA_ARGS__)), cui_box_end(ui))
#define cui_card(ui, ...)       CUI__SCOPE(cui_card_begin((ui), CUI__OPTS(cui_card_opts, __VA_ARGS__)), cui_card_end(ui))
#define cui_scroll(ui, ...)     CUI__SCOPE(cui_scroll_begin((ui), CUI__OPTS(cui_scroll_opts, __VA_ARGS__)), cui_scroll_end(ui))
#define cui_split(ui, id, ratio, ...) CUI__SCOPE(cui_split_begin((ui), (id), (ratio), CUI__OPTS(cui_split_opts, __VA_ARGS__)), cui_split_end(ui))
#define cui_id_scope(ui, str)   CUI__SCOPE(cui_push_id((ui), (str)), cui_pop_id(ui))
#define cui_disabled_scope(ui, d) CUI__SCOPE(cui_push_disabled((ui), (d)), cui_pop_disabled(ui))

#define cui_text(ui, text, ...)             cui_text_ex((ui), (text), CUI__OPTS(cui_text_opts, __VA_ARGS__))
#define cui_button(ui, label, ...)          cui_button_ex((ui), (label), CUI__OPTS(cui_button_opts, __VA_ARGS__))
#define cui_icon_button(ui, icon, ...)      cui_icon_button_ex((ui), (icon), CUI__OPTS(cui_button_opts, __VA_ARGS__))
#define cui_segmented(ui, id, items, n, idx, ...) cui_segmented_ex((ui), (id), (items), (n), (idx), CUI__OPTS(cui_segmented_opts, __VA_ARGS__))
#define cui_checkbox(ui, label, v, ...)     cui_checkbox_ex((ui), (label), (v), CUI__OPTS(cui_check_opts, __VA_ARGS__))
#define cui_switch(ui, label, v, ...)       cui_switch_ex((ui), (label), (v), CUI__OPTS(cui_check_opts, __VA_ARGS__))
#define cui_radio(ui, label, v, opt, ...)   cui_radio_ex((ui), (label), (v), (opt), CUI__OPTS(cui_check_opts, __VA_ARGS__))
#define cui_slider(ui, label, v, lo, hi, ...) cui_slider_ex((ui), (label), (v), (lo), (hi), CUI__OPTS(cui_slider_opts, __VA_ARGS__))
#define cui_chip(ui, label, sel, ...)       cui_chip_ex((ui), (label), (sel), CUI__OPTS(cui_chip_opts, __VA_ARGS__))
#define cui_input(ui, id, buf, cap, ...)    cui_input_ex((ui), (id), (buf), (cap), CUI__OPTS(cui_input_opts, __VA_ARGS__))
#define cui_textarea(ui, id, buf, cap, ...) cui_textarea_ex((ui), (id), (buf), (cap), CUI__OPTS(cui_input_opts, __VA_ARGS__))
#define cui_search(ui, id, buf, cap, ...)   cui_search_ex((ui), (id), (buf), (cap), CUI__OPTS(cui_input_opts, __VA_ARGS__))
#define cui_number(ui, id, v, lo, hi, ...)  cui_number_ex((ui), (id), (v), (lo), (hi), CUI__OPTS(cui_number_opts, __VA_ARGS__))
#define cui_select(ui, id, items, n, idx, ...) cui_select_ex((ui), (id), (items), (n), (idx), CUI__OPTS(cui_select_opts, __VA_ARGS__))
#define cui_date_picker(ui, id, v, ...)     cui_date_picker_ex((ui), (id), (v), CUI__OPTS(cui_date_opts, __VA_ARGS__))
#define cui_tabs(ui, id, items, n, idx, ...) cui_tabs_ex((ui), (id), (items), (n), (idx), CUI__OPTS(cui_tabs_opts, __VA_ARGS__))
#define cui_nav_item(ui, label, ...)        cui_nav_item_ex((ui), (label), CUI__OPTS(cui_nav_opts, __VA_ARGS__))
#define cui_popover(ui, id, ...)            CUI__SCOPE_IF(cui_popover_begin((ui), (id), CUI__OPTS(cui_popover_opts, __VA_ARGS__)), cui_popover_end(ui))
#define cui_image_viewer(ui, id, tex, w, h, view, ...) cui_image_viewer_ex((ui), (id), (tex), (w), (h), (view), CUI__OPTS(cui_image_viewer_opts, __VA_ARGS__))
#define cui_cropper(ui, id, tex, w, h, crop, ...) cui_cropper_ex((ui), (id), (tex), (w), (h), (crop), CUI__OPTS(cui_cropper_opts, __VA_ARGS__))
#define cui_tour(ui, id, steps, count, step, ...) cui_tour_ex((ui), (id), (steps), (count), (step), CUI__OPTS(cui_tour_opts, __VA_ARGS__))
#define cui_menu(ui, label, ...)            CUI__SCOPE_IF(cui_menu_begin((ui), (label), CUI__OPTS(cui_menu_opts, __VA_ARGS__)), cui_menu_end(ui))
#define cui_context_menu(ui, id)            CUI__SCOPE_IF(cui_context_menu_begin((ui), (id)), cui_context_menu_end(ui))
#define cui_menu_item(ui, label, ...)       cui_menu_item_ex((ui), (label), CUI__OPTS(cui_menu_item_opts, __VA_ARGS__))
#define cui_dialog(ui, title, open, ...)    CUI__SCOPE_IF(cui_dialog_begin((ui), (title), (open), CUI__OPTS(cui_dialog_opts, __VA_ARGS__)), cui_dialog_end(ui))
#define cui_dialog_footer(ui)               CUI__SCOPE(cui_dialog_footer_begin(ui), cui_dialog_footer_end(ui))
#define cui_sheet(ui, title, open, ...)     CUI__SCOPE_IF(cui_sheet_begin((ui), (title), (open), CUI__OPTS(cui_sheet_opts, __VA_ARGS__)), cui_sheet_end(ui))
#define cui_toast(ui, title, ...)           cui_toast_ex((ui), (title), CUI__OPTS(cui_toast_opts, __VA_ARGS__))
#define cui_alert(ui, title, ...)           cui_alert_ex((ui), (title), CUI__OPTS(cui_alert_opts, __VA_ARGS__))
#define cui_progress(ui, value, ...)        cui_progress_ex((ui), (value), CUI__OPTS(cui_progress_opts, __VA_ARGS__))
#define cui_badge(ui, text, ...)            cui_badge_ex((ui), (text), CUI__OPTS(cui_badge_opts, __VA_ARGS__))
#define cui_avatar(ui, name, ...)           cui_avatar_ex((ui), (name), CUI__OPTS(cui_avatar_opts, __VA_ARGS__))
#define cui_empty_state(ui, title, ...)     cui_empty_state_ex((ui), (title), CUI__OPTS(cui_empty_opts, __VA_ARGS__))
#define cui_metric(ui, label, value, ...)   cui_metric_ex((ui), (label), (value), CUI__OPTS(cui_metric_opts, __VA_ARGS__))
#define cui_accordion(ui, title, open)      CUI__SCOPE_IF(cui_accordion_begin((ui), (title), (open)), cui_accordion_end(ui))
#define cui_tree(ui, label, open, ...)      CUI__SCOPE_IF(cui_tree_begin((ui), (label), (open), CUI__OPTS(cui_tree_opts, __VA_ARGS__)), cui_tree_end(ui))
#define cui_list_item(ui, title, ...)       cui_list_item_ex((ui), (title), CUI__OPTS(cui_list_item_opts, __VA_ARGS__))
#define cui_virtual_list(ui, id, n, rh, h, first, last) CUI__SCOPE(cui_virtual_list_begin((ui), (id), (n), (rh), (h), (first), (last)), cui_virtual_list_end(ui))
#define cui_table(ui, id, cols, ncols, ...) CUI__SCOPE_IF(cui_table_begin_ex((ui), (id), (cols), (ncols), CUI__OPTS(cui_table_opts, __VA_ARGS__)), cui_table_end(ui))
#define cui_line_chart(ui, id, s, n, ...)   cui_line_chart_ex((ui), (id), (s), (n), CUI__OPTS(cui_chart_opts, __VA_ARGS__))
#define cui_bar_chart(ui, id, s, n, ...)    cui_bar_chart_ex((ui), (id), (s), (n), CUI__OPTS(cui_chart_opts, __VA_ARGS__))
#define cui_donut_chart(ui, id, v, l, n, ...) cui_donut_chart_ex((ui), (id), (v), (l), (n), CUI__OPTS(cui_chart_opts, __VA_ARGS__))
#define cui_image(ui, tex, ...)             cui_image_ex((ui), (tex), CUI__OPTS(cui_image_opts, __VA_ARGS__))
#define cui_radio_cards(ui, id, ch, n, idx, ...) cui_radio_cards_ex((ui), (id), (ch), (n), (idx), CUI__OPTS(cui_radio_cards_opts, __VA_ARGS__))
#define cui_tag_input(ui, id, buf, cap, ...) cui_tag_input_ex((ui), (id), (buf), (cap), CUI__OPTS(cui_tag_opts, __VA_ARGS__))
#define cui_button_group(ui)                CUI__SCOPE(cui_button_group_begin(ui), cui_button_group_end(ui))
#define cui_hold_button(ui, label, ...)     cui_hold_button_ex((ui), (label), CUI__OPTS(cui_hold_opts, __VA_ARGS__))
#define cui_reorder_item(ui, pos)           CUI__SCOPE(cui_reorder_item_begin((ui), (pos)), cui_reorder_item_end(ui))
#define cui_timeline_item(ui, title, ...)   cui_timeline_item_ex((ui), (title), CUI__OPTS(cui_timeline_opts, __VA_ARGS__))
#define cui_kanban_column_begin(ui, title, count, ...) cui_kanban_column_begin_ex((ui), (title), (count), CUI__OPTS(cui_kanban_column_opts, __VA_ARGS__))
#define cui_week_calendar(ui, id, week, events, count, ...) cui_week_calendar_ex((ui), (id), (week), (events), (count), CUI__OPTS(cui_week_opts, __VA_ARGS__))
#define cui_month_calendar(ui, id, month, events, count, ...) cui_month_calendar_ex((ui), (id), (month), (events), (count), CUI__OPTS(cui_month_opts, __VA_ARGS__))
#define cui_gantt(ui, id, tasks, count, origin, days, ...) cui_gantt_ex((ui), (id), (tasks), (count), (origin), (days), CUI__OPTS(cui_gantt_opts, __VA_ARGS__))
#define cui_reasoning(ui, id, ...)          CUI__SCOPE_IF(cui_reasoning_begin((ui), (id), CUI__OPTS(cui_reasoning_opts, __VA_ARGS__)), cui_reasoning_end(ui))
#define cui_tool_call(ui, id, name, ...)    cui_tool_call_ex((ui), (id), (name), CUI__OPTS(cui_tool_call_opts, __VA_ARGS__))
#define cui_agent_plan(ui, id, steps, n, ...) cui_agent_plan_ex((ui), (id), (steps), (n), CUI__OPTS(cui_agent_plan_opts, __VA_ARGS__))
#define cui_agent_status(ui, text, ...)     cui_agent_status_ex((ui), (text), CUI__OPTS(cui_agent_status_opts, __VA_ARGS__))
#define cui_context_meter(ui, id, used, limit, ...) cui_context_meter_ex((ui), (id), (used), (limit), CUI__OPTS(cui_context_opts, __VA_ARGS__))
#define cui_response_actions(ui, id, ...)   cui_response_actions_ex((ui), (id), CUI__OPTS(cui_response_actions_opts, __VA_ARGS__))
#define cui_prompt_suggestions(ui, id, items, n, ...) cui_prompt_suggestions_ex((ui), (id), (items), (n), CUI__OPTS(cui_prompt_suggestions_opts, __VA_ARGS__))
#define cui_model_picker(ui, id, models, n, sel, ...) cui_model_picker_ex((ui), (id), (models), (n), (sel), CUI__OPTS(cui_model_picker_opts, __VA_ARGS__))
#define cui_chat_composer(ui, id, buf, cap, ...) cui_chat_composer_ex((ui), (id), (buf), (cap), CUI__OPTS(cui_composer_opts, __VA_ARGS__))
#define cui_voice_orb(ui, id, state, level, ...) cui_voice_orb_ex((ui), (id), (state), (level), CUI__OPTS(cui_voice_orb_opts, __VA_ARGS__))
#define cui_shimmer_text(ui, text, ...)    cui_shimmer_text_ex((ui), (text), CUI__OPTS(cui_text_fx_opts, __VA_ARGS__))
#define cui_typewriter(ui, id, text, ...)   cui_typewriter_ex((ui), (id), (text), CUI__OPTS(cui_text_fx_opts, __VA_ARGS__))
#define cui_text_reveal(ui, id, text, ...)  cui_text_reveal_ex((ui), (id), (text), CUI__OPTS(cui_text_fx_opts, __VA_ARGS__))
#define cui_word_rotate(ui, id, words, n, interval, ...) cui_word_rotate_ex((ui), (id), (words), (n), (interval), CUI__OPTS(cui_text_fx_opts, __VA_ARGS__))
#define cui_number_roll(ui, id, text, ...)  cui_number_roll_ex((ui), (id), (text), CUI__OPTS(cui_text_fx_opts, __VA_ARGS__))
#define cui_gradient_text(ui, text, ...)  cui_gradient_text_ex((ui), (text), CUI__OPTS(cui_gradient_text_opts, __VA_ARGS__))
#define cui_spotlight(ui, id, ...)          CUI__SCOPE(cui_spotlight_begin((ui), (id), CUI__OPTS(cui_spotlight_opts, __VA_ARGS__)), cui_spotlight_end(ui))
#define cui_loader(ui, ...)                 cui_loader_ex((ui), CUI__OPTS(cui_loader_opts, __VA_ARGS__))
#define cui_dock(ui, id, ...)               CUI__SCOPE(cui_dock_begin((ui), (id), CUI__OPTS(cui_dock_opts, __VA_ARGS__)), cui_dock_end(ui))
#define cui_dock_item(ui, icon, label, ...) cui_dock_item_ex((ui), (icon), (label), CUI__OPTS(cui_dock_item_opts, __VA_ARGS__))
#define cui_tab_bar(ui, id, items, n, sel, ...) cui_tab_bar_ex((ui), (id), (items), (n), (sel), CUI__OPTS(cui_tab_bar_opts, __VA_ARGS__))
#define cui_control_tile(ui, label, on, ...) cui_control_tile_ex((ui), (label), (on), CUI__OPTS(cui_tile_opts, __VA_ARGS__))
#define cui_waveform(ui, id, peaks, n, progress, ...) cui_waveform_ex((ui), (id), (peaks), (n), (progress), CUI__OPTS(cui_waveform_opts, __VA_ARGS__))
#define cui_now_playing(ui, id, np, ...)    cui_now_playing_ex((ui), (id), (np), CUI__OPTS(cui_now_playing_opts, __VA_ARGS__))
#define cui_heatmap(ui, id, v, rows, cols, ...) cui_heatmap_ex((ui), (id), (v), (rows), (cols), CUI__OPTS(cui_heatmap_opts, __VA_ARGS__))
#define cui_calendar_heatmap(ui, id, start, v, days, ...) cui_calendar_heatmap_ex((ui), (id), (start), (v), (days), CUI__OPTS(cui_calendar_heatmap_opts, __VA_ARGS__))
#define cui_treemap(ui, id, items, n, ...)  cui_treemap_ex((ui), (id), (items), (n), CUI__OPTS(cui_treemap_opts, __VA_ARGS__))
#define cui_radar_chart(ui, id, s, n, axes, na, ...) cui_radar_chart_ex((ui), (id), (s), (n), (axes), (na), CUI__OPTS(cui_radar_opts, __VA_ARGS__))
#define cui_funnel(ui, id, stages, n, ...)  cui_funnel_ex((ui), (id), (stages), (n), CUI__OPTS(cui_funnel_opts, __VA_ARGS__))
#define cui_sankey(ui, id, nodes, nn, links, nl, ...) cui_sankey_ex((ui), (id), (nodes), (nn), (links), (nl), CUI__OPTS(cui_sankey_opts, __VA_ARGS__))
#define cui_waffle(ui, id, items, n, ...)   cui_waffle_ex((ui), (id), (items), (n), CUI__OPTS(cui_waffle_opts, __VA_ARGS__))
#define cui_split_bar(ui, id, items, n, ...) cui_split_bar_ex((ui), (id), (items), (n), CUI__OPTS(cui_split_bar_opts, __VA_ARGS__))
#define cui_bar_list(ui, id, items, n, ...) cui_bar_list_ex((ui), (id), (items), (n), CUI__OPTS(cui_bar_list_opts, __VA_ARGS__))
#define cui_activity_rings(ui, id, rings, n, ...) cui_activity_rings_ex((ui), (id), (rings), (n), CUI__OPTS(cui_rings_opts, __VA_ARGS__))
#define cui_scatter_chart(ui, id, s, n, ...) cui_scatter_chart_ex((ui), (id), (s), (n), CUI__OPTS(cui_scatter_opts, __VA_ARGS__))
#define cui_radial_gauge(ui, id, value, min, max, ...) cui_radial_gauge_ex((ui), (id), (value), (min), (max), CUI__OPTS(cui_radial_gauge_opts, __VA_ARGS__))
#define cui_candlestick(ui, id, candles, count, ...) cui_candlestick_ex((ui), (id), (candles), (count), CUI__OPTS(cui_candlestick_opts, __VA_ARGS__))
#define cui_chat_message(ui, text, ...)     cui_chat_message_ex((ui), (text), CUI__OPTS(cui_chat_opts, __VA_ARGS__))
#define cui_comment(ui, id, author, text, ...) cui_comment_ex((ui), (id), (author), (text), CUI__OPTS(cui_comment_opts, __VA_ARGS__))
#define cui_carousel(ui, id, count, index, ...) CUI__SCOPE(cui_carousel_begin((ui), (id), (count), (index), CUI__OPTS(cui_carousel_opts, __VA_ARGS__)), cui_carousel_end(ui))
#define cui_carousel_slide(ui)              CUI__SCOPE(cui_carousel_slide_begin(ui), cui_carousel_slide_end(ui))
#define cui_image_compare(ui, id, before, after, pos, ...) cui_image_compare_ex((ui), (id), (before), (after), (pos), CUI__OPTS(cui_compare_opts, __VA_ARGS__))
#define cui_card_stack_item(ui, depth)      CUI__SCOPE(cui_card_stack_item_begin((ui), (depth)), cui_card_stack_item_end(ui))
#define cui_counter(ui, id, value, ...)     cui_counter_ex((ui), (id), (value), CUI__OPTS(cui_counter_opts, __VA_ARGS__))
#define cui_ticker(ui, id, items, count, ...) cui_ticker_ex((ui), (id), (items), (count), CUI__OPTS(cui_ticker_opts, __VA_ARGS__))
#define cui_data_grid_begin(ui, id, cols, ncols, rows, first, last, ...) cui_data_grid_begin_ex((ui), (id), (cols), (ncols), (rows), (first), (last), CUI__OPTS(cui_grid_opts, __VA_ARGS__))
#define cui_code_block(ui, id, code, ...)   cui_code_block_ex((ui), (id), (code), CUI__OPTS(cui_code_opts, __VA_ARGS__))
#define cui_json_view(ui, id, json, ...)    cui_json_view_ex((ui), (id), (json), CUI__OPTS(cui_json_opts, __VA_ARGS__))
#define cui_hover_card(ui, id, ...)         CUI__SCOPE_IF(cui_hover_card_begin((ui), (id), CUI__OPTS(cui_hover_card_opts, __VA_ARGS__)), cui_hover_card_end(ui))
#define cui_confirm_popover(ui, id, ...)    cui_confirm_popover_ex((ui), (id), CUI__OPTS(cui_confirm_popover_opts, __VA_ARGS__))
#define cui_expandable_text(ui, id, text, ...) cui_expandable_text_ex((ui), (id), (text), CUI__OPTS(cui_expandable_text_opts, __VA_ARGS__))
#define cui_expandable_card(ui, title, open, ...) CUI__SCOPE_IF(cui_expandable_card_begin((ui), (title), (open), CUI__OPTS(cui_expandable_card_opts, __VA_ARGS__)), cui_expandable_card_end(ui))
#define cui_banner(ui, text, open, ...)     cui_banner_ex((ui), (text), (open), CUI__OPTS(cui_banner_opts, __VA_ARGS__))
#define cui_countdown(ui, id, seconds, ...) cui_countdown_ex((ui), (id), (seconds), CUI__OPTS(cui_countdown_opts, __VA_ARGS__))
#define cui_checklist(ui, id, steps, count, ...) cui_checklist_ex((ui), (id), (steps), (count), CUI__OPTS(cui_checklist_opts, __VA_ARGS__))
#define cui_multi_select(ui, id, items, count, selected, ...) cui_multi_select_ex((ui), (id), (items), (count), (selected), CUI__OPTS(cui_multi_select_opts, __VA_ARGS__))
#define cui_date_range_picker(ui, id, range, ...) cui_date_range_picker_ex((ui), (id), (range), CUI__OPTS(cui_date_range_opts, __VA_ARGS__))
#define cui_time_picker(ui, id, hour, minute, ...) cui_time_picker_ex((ui), (id), (hour), (minute), CUI__OPTS(cui_time_opts, __VA_ARGS__))
#define cui_wheel_picker(ui, id, items, count, index, ...) cui_wheel_picker_ex((ui), (id), (items), (count), (index), CUI__OPTS(cui_wheel_opts, __VA_ARGS__))
#define cui_ruler_slider(ui, label, value, min, max, ...) cui_ruler_slider_ex((ui), (label), (value), (min), (max), CUI__OPTS(cui_ruler_opts, __VA_ARGS__))
#define cui_histogram_slider(ui, label, bins, n, lo, hi, min, max, ...) cui_histogram_slider_ex((ui), (label), (bins), (n), (lo), (hi), (min), (max), CUI__OPTS(cui_histogram_opts, __VA_ARGS__))
#define cui_knob(ui, id, value, min, max, ...) cui_knob_ex((ui), (id), (value), (min), (max), CUI__OPTS(cui_knob_opts, __VA_ARGS__))
#define cui_fader(ui, id, value, min, max, ...) cui_fader_ex((ui), (id), (value), (min), (max), CUI__OPTS(cui_fader_opts, __VA_ARGS__))
#define cui_level_meter(ui, id, levels, channels, ...) cui_level_meter_ex((ui), (id), (levels), (channels), CUI__OPTS(cui_level_meter_opts, __VA_ARGS__))
#define cui_signature_pad(ui, id, ...)      cui_signature_pad_ex((ui), (id), CUI__OPTS(cui_signature_opts, __VA_ARGS__))
#define cui_dropzone(ui, id, ...)           cui_dropzone_ex((ui), (id), CUI__OPTS(cui_dropzone_opts, __VA_ARGS__))
#define cui_split_button(ui, label, items, count, ...) cui_split_button_ex((ui), (label), (items), (count), CUI__OPTS(cui_split_button_opts, __VA_ARGS__))
#define cui_toggle_group(ui, id, labels, count, values, ...) cui_toggle_group_ex((ui), (id), (labels), (count), (values), CUI__OPTS(cui_toggle_group_opts, __VA_ARGS__))
#define cui_confirm_button(ui, label, ...)  cui_confirm_button_ex((ui), (label), CUI__OPTS(cui_confirm_opts, __VA_ARGS__))
#define cui_status_button(ui, label, status, ...) cui_status_button_ex((ui), (label), (status), CUI__OPTS(cui_status_button_opts, __VA_ARGS__))
#define cui_speed_dial(ui, id, actions, count, ...) cui_speed_dial_ex((ui), (id), (actions), (count), CUI__OPTS(cui_speed_dial_opts, __VA_ARGS__))
#define cui_user_menu(ui, name, ...)        CUI__SCOPE_IF(cui_user_menu_begin((ui), (name), CUI__OPTS(cui_user_menu_opts, __VA_ARGS__)), cui_user_menu_end(ui))
#define cui_inline_edit(ui, id, buf, cap, ...) cui_inline_edit_ex((ui), (id), (buf), (cap), CUI__OPTS(cui_inline_edit_opts, __VA_ARGS__))
#define cui_scrub(ui, label, value, min, max, ...) cui_scrub_ex((ui), (label), (value), (min), (max), CUI__OPTS(cui_scrub_opts, __VA_ARGS__))
#define cui_money_input(ui, id, value, ...) cui_money_input_ex((ui), (id), (value), CUI__OPTS(cui_money_opts, __VA_ARGS__))
#define cui_shortcut_recorder(ui, id, combo, ...) cui_shortcut_recorder_ex((ui), (id), (combo), CUI__OPTS(cui_shortcut_recorder_opts, __VA_ARGS__))
/* app chrome */
#define cui_menu_bar(ui, id, ...)           CUI__SCOPE(cui_menu_bar_begin((ui), (id), CUI__OPTS(cui_menu_bar_opts, __VA_ARGS__)), cui_menu_bar_end(ui))
#define cui_menu_bar_menu(ui, label, ...)   CUI__SCOPE_IF(cui_menu_bar_menu_begin((ui), (label), CUI__OPTS(cui_menu_bar_menu_opts, __VA_ARGS__)), cui_menu_bar_menu_end(ui))
#define cui_toolbar(ui, id, ...)            CUI__SCOPE(cui_toolbar_begin((ui), (id), CUI__OPTS(cui_toolbar_opts, __VA_ARGS__)), cui_toolbar_end(ui))
#define cui_toolbar_button(ui, icon, ...)   cui_toolbar_button_ex((ui), (icon), CUI__OPTS(cui_tool_opts, __VA_ARGS__))
#define cui_status_bar(ui, id, ...)         CUI__SCOPE(cui_status_bar_begin((ui), (id), CUI__OPTS(cui_status_bar_opts, __VA_ARGS__)), cui_status_bar_end(ui))
#define cui_status_bar_text(ui, text, ...)  cui_status_bar_text_ex((ui), (text), CUI__OPTS(cui_status_item_opts, __VA_ARGS__))
#define cui_status_bar_item(ui, label, ...) cui_status_bar_item_ex((ui), (label), CUI__OPTS(cui_status_item_opts, __VA_ARGS__))
#define cui_status_bar_progress(ui, value, ...) cui_status_bar_progress_ex((ui), (value), CUI__OPTS(cui_status_progress_opts, __VA_ARGS__))
#define cui_doc_tabs(ui, id, tabs, count, selected, ...) cui_doc_tabs_ex((ui), (id), (tabs), (count), (selected), CUI__OPTS(cui_doc_tabs_opts, __VA_ARGS__))
/* tool panels */
#define cui_inspector_begin(ui, id, ...)    cui_inspector_begin_ex((ui), (id), CUI__OPTS(cui_inspector_opts, __VA_ARGS__))
#define cui_inspector(ui, id, ...)          CUI__SCOPE_IF(cui_inspector_begin_ex((ui), (id), CUI__OPTS(cui_inspector_opts, __VA_ARGS__)), cui_inspector_end(ui))
#define cui_prop_float(ui, label, v, ...)   cui_prop_float_ex((ui), (label), (v), CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_int(ui, label, v, ...)     cui_prop_int_ex((ui), (label), (v), CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_text(ui, label, buf, cap, ...) cui_prop_text_ex((ui), (label), (buf), (cap), CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_bool(ui, label, v, ...)    cui_prop_bool_ex((ui), (label), (v), CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_enum(ui, label, items, n, idx, ...) cui_prop_enum_ex((ui), (label), (items), (n), (idx), CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_color(ui, label, v, ...)   cui_prop_color_ex((ui), (label), (v), CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_vec2(ui, label, v, ...)    cui_prop_vec_ex((ui), (label), (v), 2, CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_vec3(ui, label, v, ...)    cui_prop_vec_ex((ui), (label), (v), 3, CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_prop_vec4(ui, label, v, ...)    cui_prop_vec_ex((ui), (label), (v), 4, CUI__OPTS(cui_prop_opts, __VA_ARGS__))
#define cui_log_view(ui, id, buf, ...)      cui_log_view_ex((ui), (id), (buf), CUI__OPTS(cui_log_view_opts, __VA_ARGS__))
#define cui_hex_view(ui, id, data, size, sel, ...) cui_hex_view_ex((ui), (id), (data), (size), (sel), CUI__OPTS(cui_hex_view_opts, __VA_ARGS__))
#define cui_flame_graph(ui, id, spans, count, view, selected, ...) \
    cui_flame_graph_ex((ui), (id), (spans), (count), (view), (selected), CUI__OPTS(cui_flame_opts, __VA_ARGS__))
/* curve and gradient editors */
#define cui_curve_editor(ui, id, keys, count, cap, ...) cui_curve_editor_ex((ui), (id), (keys), (count), (cap), CUI__OPTS(cui_curve_opts, __VA_ARGS__))
#define cui_gradient_editor(ui, id, stops, count, cap, ...) cui_gradient_editor_ex((ui), (id), (stops), (count), (cap), CUI__OPTS(cui_gradient_opts, __VA_ARGS__))
#endif /* !__cplusplus */

#ifdef __cplusplus
}
#endif

#endif /* CUI_H */
