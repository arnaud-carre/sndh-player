/* Internal declarations shared by the CUI implementation files. */
#ifndef CUI_INTERNAL_H
#define CUI_INTERNAL_H

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef CUI_BUILDING
#define CUI_BUILDING
#endif

#include "cui/cui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUI_PI 3.14159265358979323846f

#define CUI_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CUI_MAX(a, b) ((a) > (b) ? (a) : (b))
#define CUI_CLAMP(x, lo, hi) CUI_MIN(CUI_MAX((x), (lo)), (hi))
#define CUI_COUNTOF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline float cui__clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float cui__lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline bool cui__rect_contains(cui_rect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
static inline cui_rect cui__rect_intersect(cui_rect a, cui_rect b) {
    float x0 = CUI_MAX(a.x, b.x), y0 = CUI_MAX(a.y, b.y);
    float x1 = CUI_MIN(a.x + a.w, b.x + b.w), y1 = CUI_MIN(a.y + a.h, b.y + b.h);
    cui_rect r = { x0, y0, CUI_MAX(0, x1 - x0), CUI_MAX(0, y1 - y0) };
    return r;
}
static inline cui_rect cui__rect_inset(cui_rect r, float d) {
    cui_rect o = { r.x + d, r.y + d, CUI_MAX(0, r.w - 2 * d), CUI_MAX(0, r.h - 2 * d) };
    return o;
}
static inline cui_color cui__rgba(int r, int g, int b, int a) {
    cui_color c = { (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
    return c;
}
static inline bool cui__is_token(cui_color c) { return c.a == 0 && c.g == 0xC5 && c.b == 0x1D && c.r != 0; }
static inline bool cui__color_set(cui_color c) { return c.a != 0 || cui__is_token(c); }

/* ---- memory ---- */
void *cui__alloc(cui_ctx *ui, size_t size);
size_t cui__inflate(const void *src, size_t src_len, void *dst, size_t dst_cap);  /* raw DEFLATE */
/* color emoji (cui_emoji.c), PNG for its bitmaps (cui_image.c), outlines for its layers (cui_text.c) */
void cui__emoji_shutdown(cui_ctx *ui);
void cui__emoji_drop_texture(cui_ctx *ui);
void cui__emoji_frame(cui_ctx *ui);
unsigned char *cui__png_decode(const unsigned char *data, int len, int *w, int *h);
void cui__png_free(unsigned char *px);
typedef struct cui__tt cui__tt;
cui__tt *cui__tt_open(cui_ctx *ui, const unsigned char *data, int offset);
float cui__tt_scale(cui__tt *t, float px_per_em);
void cui__tt_vmetrics(cui__tt *t, int *ascent, int *descent);
bool cui__tt_box(cui__tt *t, int gid, float scale, int *x0, int *y0, int *x1, int *y1);
void cui__tt_render(cui__tt *t, int gid, float scale, unsigned char *out, int w, int h, int stride);
void *cui__realloc(cui_ctx *ui, void *p, size_t size);
void cui__free(cui_ctx *ui, void *p);
void *cui__frame_alloc(cui_ctx *ui, size_t size);       /* freed at frame start */
char *cui__frame_strdup(cui_ctx *ui, const char *s, int len);
char *cui__frame_printf(cui_ctx *ui, const char *fmt, ...) CUI_PRINTF(2, 3);
char *cui__frame_vprintf(cui_ctx *ui, const char *fmt, va_list ap);

#define CUI__GROW(ui, arr, count, cap, need) do {                                 \
    if ((count) + (need) > (cap)) {                                               \
        int ncap__ = (cap) ? (cap) * 2 : 64;                                      \
        while (ncap__ < (count) + (need)) ncap__ *= 2;                            \
        (arr) = cui__realloc((ui), (arr), sizeof(*(arr)) * (size_t)ncap__);        \
        (cap) = ncap__;                                                           \
    }                                                                             \
} while (0)

/* ---- utf8 ---- */
int cui__utf8_decode(const char *s, int len, uint32_t *cp);   /* returns bytes consumed (>=1) */
int cui__utf8_encode(uint32_t cp, char *out);
int cui__utf8_prev(const char *s, int pos);
int cui__utf8_next(const char *s, int len, int pos);
int cui__display_len(const char *label);                      /* length before "##" */

/* ---- hashing ---- */
uint32_t cui__hash(const void *data, size_t len, uint32_t seed);
uint32_t cui__hash_str(const char *s, uint32_t seed);

/* ---- animation ---- */
typedef struct cui_spring { float x, v; } cui_spring;
float cui__spring(cui_ctx *ui, cui_spring *s, float target, float duration, float bounce);
float cui__tween(cui_ctx *ui, float *x, float target, float duration);   /* exponential approach */
#define CUI_SPRING_SMOOTH 0.40f, 0.0f
#define CUI_SPRING_SNAPPY 0.26f, 0.12f
#define CUI_SPRING_MORPH  0.42f, 0.16f

/* Motion and state tokens. One value per job, so every component answers in
 * the same time and the same way. Tween durations are the time to settle. */
#define CUI__T_HIGHLIGHT 0.10f   /* list and menu rows that follow the pointer or the keys */
#define CUI__T_HOVER     0.14f   /* hover color, edges, icon color */
#define CUI__T_FOCUS     0.14f   /* focus rings and focused edges */
#define CUI__T_STATE     0.18f   /* checked, selected, on: fills that report a state */
#define CUI__SPRING_EXPAND 0.32f, 0.0f   /* disclosures, trees, accordions open and close */
#define CUI__SPRING_ENTER  0.28f, 0.0f   /* popovers, menus, tooltips, hover cards */
#define CUI__SPRING_DIALOG 0.34f, 0.0f   /* dialogs */
#define CUI__SPRING_EXIT   0.16f, 0.0f   /* every floating layer leaves faster than it came */
#define CUI__ENTER_SCALE 0.96f   /* floating layers grow from here */
#define CUI__PRESS       0.97f   /* buttons, chips, tiles */
#define CUI__PRESS_WIDE  0.98f   /* rows and cards: wide targets travel further at the same scale */
#define CUI__DISABLED    0.45f   /* opacity of anything disabled */
/* Radius steps derived from the theme, so a custom scale carries everywhere */
#define CUI__R_ITEM(ui)    CUI_MAX(6.0f, (ui)->theme.radius_panel - 6.0f)   /* rows inside a panel */
#define CUI__R_CONTROL(ui, h) CUI_MIN((ui)->theme.radius_control, (h) * 0.5f)
#define CUI__FIELD_PAD 12.0f      /* fields: text side */
#define CUI__FIELD_PAD_ICON 10.0f /* fields: the side with an icon */
#define CUI__R_SMALL 6.0f   /* marks inside rows: attachment chips, bars, events */

/* ---- fonts ---- */
typedef struct cui_glyph {
    uint32_t key_cp;
    uint16_t font;
    uint16_t size_q;     /* pixel size * 4 */
    uint8_t subpx;
    uint8_t pad_;
    uint32_t stamp;      /* the frame it was last drawn in, so a full atlas can drop old sizes */
    int16_t x0, y0;      /* offset of bitmap from pen position (pixels) */
    uint16_t w, h;
    uint16_t ax, ay;     /* atlas position */
    int next;            /* hash chain */
} cui_glyph;

typedef struct cui_font_data cui_font_data;

typedef struct cui_text_layout_line { int start, end; float width; } cui_text_layout_line;

#ifdef CUI_TEXT_SHAPING
bool cui__text_needs_shaping(cui_ctx *ui, const char *text, int len);
#endif
/* Glyph metrics for layout, in points. */
float cui__font_line_height(cui_ctx *ui, cui_font font, float size);
float cui__font_ascent(cui_ctx *ui, cui_font font, float size);
float cui__text_width(cui_ctx *ui, cui_font font, float size, const char *text, int len, bool tabular);
/* Index of the byte boundary closest to x within a single line. */
int cui__text_hit(cui_ctx *ui, cui_font font, float size, const char *text, int len, float x);
/* Word wrap into lines. Returns line count; writes up to max lines. */
int cui__text_wrap(cui_ctx *ui, cui_font font, float size, const char *text, int len, float width,
                   cui_text_layout_line *lines, int max_lines);
float cui__text_min_width(cui_ctx *ui, cui_font font, float size, const char *text, int len);
/* Draw single line text with top-left at pos (points). */
void cui__draw_text_line(cui_ctx *ui, cui_font font, float size, float x, float y, cui_color color,
                         const char *text, int len, bool tabular);
/* Draw text truncated with an ellipsis to max_w. */
void cui__draw_text_ellipsis(cui_ctx *ui, cui_font font, float size, float x, float y, float max_w,
                             cui_color color, const char *text, int len, bool tabular);
void cui__fonts_init(cui_ctx *ui, bool load_defaults);
void cui__fonts_shutdown(cui_ctx *ui);
void cui__fonts_begin_frame(cui_ctx *ui);
void cui__fonts_upload(cui_ctx *ui);
cui_font cui__resolve_font(cui_ctx *ui, cui_font f);

/* ---- draw list ---- */
typedef struct cui_xform { float scale, ox, oy, alpha, tx, ty; } cui_xform;

typedef struct cui_draw_list {
    cui_vertex *vtx; int vtx_count, vtx_cap;
    uint32_t *idx; int idx_count, idx_cap;
    cui_draw_cmd *cmd; int cmd_count, cmd_cap;
    cui_rect clip_stack[64]; int clip_depth;
    cui_xform xform_stack[32]; int xform_depth;
    cui_texture cur_tex;
} cui_draw_list;

void cui__draw_reset(cui_ctx *ui);
void cui__push_xform(cui_ctx *ui, float scale, float ox, float oy, float alpha, float tx, float ty);
void cui__pop_xform(cui_ctx *ui);
void cui__draw_glyph_quad(cui_ctx *ui, float x0, float y0, float x1, float y1,
                          float u0, float v0, float u1, float v1, cui_color c);
void cui__draw_check(cui_ctx *ui, cui_rect r, float t, float thickness, cui_color c);
void cui__draw_top_light(cui_ctx *ui, cui_rect r, cui_corners rad, float alpha);
void cui__draw_sheen(cui_ctx *ui, cui_rect r, cui_corners rad, float k);
void cui__draw_spinner(cui_ctx *ui, cui_vec2 center, float radius, float thickness, cui_color c);
void cui__draw_focus_ring(cui_ctx *ui, cui_rect r, cui_corners radius, float alpha);
void cui__draw_elevation(cui_ctx *ui, cui_rect r, cui_corners radius, cui_elevation e, float alpha);
/* Raw triangles with per vertex colors (points). */
typedef struct cui_cvert { float x, y; cui_color c; } cui_cvert;
void cui__draw_triangles(cui_ctx *ui, const cui_cvert *v, int nv, const uint32_t *idx, int ni);

/* ---- layout tree ---- */
enum {
    CUI_NF_TEXT        = 1 << 0,
    CUI_NF_INTERACTIVE = 1 << 1,   /* records a hit rect */
    CUI_NF_SCROLL_X    = 1 << 2,
    CUI_NF_SCROLL_Y    = 1 << 3,
    CUI_NF_CLIP        = 1 << 4,
    CUI_NF_FLOATING    = 1 << 5,
    CUI_NF_BLOCKER     = 1 << 6,   /* blocks input to everything below (modal scrim) */
    CUI_NF_WRAP        = 1 << 7,
    CUI_NF_TABULAR     = 1 << 8,
    CUI_NF_FOCUSABLE   = 1 << 9,
    CUI_NF_SCROLLBAR   = 1 << 10,
    CUI_NF_HIT_OPAQUE  = 1 << 11,  /* catches clicks (e.g. popover background) */
    CUI_NF_PAINT_AFTER = 1 << 12,  /* paint callback runs after children */
    CUI_NF_ABSOLUTE    = 1 << 13,  /* positioned at (fx, fy) inside the parent, out of flow */
    CUI_NF_ON_TOP      = 1 << 14,  /* painted after its siblings */
    CUI_NF_ROW_WRAP    = 1 << 15,  /* row that breaks children onto new lines */
    CUI_NF_LINE_START  = 1 << 16,  /* computed: first child of a wrapped line */
    CUI_NF_KEEP_W      = 1 << 17,  /* never shrinks below its preferred width (its siblings give way) */
    CUI_NF_ADAPTIVE    = 1 << 18   /* rearranges itself when narrow: min_w is its minimum, not its content */
};

struct cui_node;
typedef void (*cui__paint_fn)(cui_ctx *ui, struct cui_node *n, void *data);
/* Per glyph text effect: gets the byte offset in the node's text, the glyph's
 * pen x and the line's top (points); returns the glyph color and may move it. */
typedef cui_color (*cui__text_fx_fn)(cui_ctx *ui, void *data, int byte, float x, float line_y, float *dy, cui_color c);

typedef struct cui_node {
    cui_id id;
    int parent, first, last, next;
    int abs_first, abs_last;   /* absolutely positioned children */
    int abs_anchor;            /* absolute: position relative to this node (-1 = parent) */
    int child_count;
    uint32_t flags;

    /* layout input */
    uint8_t dir, align, justify, self_align;   /* self_align: 0 = inherit */
    cui_size w, h;
    float min_w, max_w, min_h, max_h;
    float pad[4];             /* top, right, bottom, left */
    float gap;
    int columns;
    float col_min;            /* grid: minimum column width, 0 = fixed column count */
    float pref_hint;          /* preferred width of a fit node that may shrink (0 = from content) */

    /* text */
    const char *text;
    int text_len;
    cui_font font;
    float font_size;
    float line_height;        /* multiplier */
    uint8_t text_align;
    cui_color text_color;

    /* style */
    cui_color bg, border_color;
    float border;
    cui_corners radius;
    cui_elevation elevation;
    float elevation_alpha;
    float opacity;
    float scale;              /* transform around center, 1 = none */
    float tx, ty;             /* translation */

    /* custom paint */
    cui__paint_fn paint;
    cui__text_fx_fn text_fx;
    void *text_fx_data;
    void *paint_data;

    /* floating */
    int z;
    cui_id anchor;            /* 0 = viewport */
    uint8_t placement;
    float offset;             /* distance from anchor */
    float fx, fy;             /* explicit position when anchor == 0 and fpos set */
    bool fpos;
    int root_index;           /* order among floating roots */

    /* scroll */
    float scroll_x, scroll_y;

    /* computed */
    float pref_w, pref_h, minc_w, minc_h;   /* content based sizes */
    float cmin_w;                           /* minimum width of the content alone, before min_w */
    cui_rect rect;
    float content_w, content_h;
    int text_lines;
    cui_text_layout_line *lines;
    int grid_cols;            /* grid: columns used by the latest layout */
} cui_node;

/* self_align value for intrinsic size components: never stretched, otherwise
 * aligned like their siblings */
#define CUI__ALIGN_NOSTRETCH 7

/* placement values beyond cui_placement for floating roots */
enum { CUI__PLACE_FULL = 100, CUI__PLACE_CENTER = 101, CUI__PLACE_POINT = 102 };

typedef struct cui_scroll_state {
    float x, y;                 /* current offset */
    float tx, ty;               /* target offset */
    float content_w, content_h; /* from last layout */
    float view_w, view_h;
    float bar_t;                /* scrollbar visibility 0..1 */
    double last_activity;
    bool drag_y, drag_x;
    float drag_grab;
    bool hover_bar;
    cui_color fade;             /* edge fade color, alpha 0 for none */
} cui_scroll_state;

int cui__node_new(cui_ctx *ui, cui_id id);
cui_node *cui__node(cui_ctx *ui, int index);
int cui__open(cui_ctx *ui, int node);            /* push node as current parent */
void cui__close(cui_ctx *ui);
int cui__current(cui_ctx *ui);
void cui__apply_box(cui_ctx *ui, cui_node *n, const cui_box *b);
int cui__leaf(cui_ctx *ui, cui_id id, cui_size w, cui_size h);
/* Width for a component with a default size: `requested` when the caller set
 * one, otherwise `def` points while there is room, shrinking toward the
 * content minimum in a narrower container (never stretched). */
void cui__soft_width(cui_node *n, cui_size requested, float def);
int cui__text_node(cui_ctx *ui, const char *text, int len, cui_font font, float size, cui_color color, bool wrap);
void cui__layout(cui_ctx *ui);
void cui__paint(cui_ctx *ui);
int cui__float_begin(cui_ctx *ui, cui_id id, int z, cui_id anchor, int placement, float offset);
void cui__float_end(cui_ctx *ui);
int cui__absolute(cui_ctx *ui, cui_id id, float x, float y, float w, float h);
void cui__paint_add_hit(cui_ctx *ui, cui_id id, cui_rect r, uint32_t flags);
/* From a paint callback: the wheel over r (inside the current clip) goes to id, ahead of any scroll area under it. */
void cui__paint_wheel_hit(cui_ctx *ui, cui_id id, cui_rect r);

/* ---- per id state ---- */
typedef struct cui_state_entry {
    cui_id id;
    uint64_t last_frame;
    cui_rect rect;             /* rect from the previous frame */
    bool has_rect;
    size_t size;
    void *data;
    void (*dtor)(cui_ctx *ui, void *data);   /* frees memory owned by data */
    int next;
} cui_state_entry;

cui_state_entry *cui__state_entry(cui_ctx *ui, cui_id id);
float cui__hover_fade(cui_ctx *ui, cui_id id, bool on, float duration);
void *cui__state_lazy(cui_ctx *ui, cui_id id, size_t size, bool want);   /* NULL while untouched */   /* state only while above zero */
void cui__state_set_dtor(cui_ctx *ui, cui_id id, void (*dtor)(cui_ctx *ui, void *data));
cui_rect cui__prev_rect(cui_ctx *ui, cui_id id);
bool cui__has_prev_rect(cui_ctx *ui, cui_id id);

/* ---- interaction ---- */
typedef struct cui_hit {
    cui_id id;
    cui_rect rect;
    uint32_t flags;
    cui_id layer;
    int order;
} cui_hit;

typedef struct cui_interact {
    bool hovered, pressed, clicked, right_clicked, double_clicked, focused, active, released, keyboard_activate;
    bool focus_visible;
} cui_interact;

enum { CUI_IF_FOCUSABLE = 1, CUI_IF_DRAG = 2, CUI_IF_KEEP_FOCUS = 4, CUI_IF_TEXT = 8 };
cui_interact cui__interact(cui_ctx *ui, cui_id id, int flags);
void cui__mark_interactive(cui_ctx *ui, int node, int flags);
void cui__set_cursor(cui_ctx *ui, cui_cursor c);
void cui__wakeup_after(cui_ctx *ui, float seconds);
bool cui__is_layer_open(cui_ctx *ui, cui_id layer);

/* ---- popups ---- */
typedef struct cui_popup {
    cui_id id;
    cui_id anchor;
    cui_rect anchor_rect;
    bool open;
    bool at_mouse;
    cui_vec2 mouse;
    uint64_t opened_frame;
    int z;                     /* stacking order: later opens are on top */
    uint64_t last_frame;
    float t;                   /* open animation 0..1 */
    cui_spring s;
} cui_popup;

typedef struct cui_toast_item {
    int handle;
    char title[128];
    char desc[256];
    char action[48];
    cui_tone tone;
    float duration, age;
    bool dismissed, action_clicked, hovered_pause, show_timer;
    cui_spring enter;          /* 0..1 appear */
    cui_spring y;              /* stacked position */
    cui_spring sx;             /* swipe offset; right dismisses */
    float grab_x, vel;         /* drag anchor and smoothed speed (points per second) */
    bool swiping;
    float height;
} cui_toast_item;

/* Scope stacks used while building one frame (kept per context). */
typedef struct cui__popup_build { cui_id id; bool disabled_pushed; bool menu; int item_index; } cui__popup_build;
typedef struct cui__modal_build { cui_id id; bool disabled_pushed; } cui__modal_build;
typedef struct cui__vlist_build { int count; float row_h; int first; } cui__vlist_build;

#define CUI_MAX_TOASTS 16
#define CUI_ID_STACK 64
#define CUI_PARENT_STACK 256

struct cui__emoji;
struct cui_ctx {
    struct cui__emoji *emoji;   /* color emoji: font, cache and data (cui_emoji.c) */
    cui_allocator alloc;
    cui_renderer_iface renderer;
    cui_platform_iface platform;

    /* viewport */
    float vw, vh, scale;

    /* time */
    double time;
    float dt;
    uint64_t frame;
    bool in_frame;
    bool animating;
    int settle_frames;
    float wakeup;              /* seconds, <0 none */

    /* frame arena */
    char *arena; size_t arena_used, arena_cap;
    /* the most each per-frame buffer needed lately: much bigger ones shrink (cui__trim_buffers) */
    int peak_vtx, peak_idx, peak_cmd, peak_nodes; size_t peak_arena; uint64_t peak_since;
    struct cui_arena_block { struct cui_arena_block *next; } *arena_overflow;

    /* nodes */
    cui_node *nodes; int node_count, node_cap;
    int parents[CUI_PARENT_STACK]; int parent_depth;
    int *float_roots; int float_count, float_cap;
    int last_node;
    cui_id last_id;

    /* ids */
    cui_id id_stack[CUI_ID_STACK]; int id_depth;
    int auto_counter[CUI_ID_STACK];
    int disabled_depth;
    bool disabled_stack[32]; int disabled_n;

    /* state store (open addressing over chained entries) */
    cui_state_entry *states; int state_count, state_cap;
    int *state_buckets; int bucket_count;
    int state_free;

    /* hit testing */
    cui_hit *hits; int hit_count, hit_cap;           /* current frame */
    cui_hit *prev_hits; int prev_hit_count, prev_hit_cap;
    cui_id hot, hot_layer;          /* topmost interactive item under the mouse */
    cui_id scroll_target;           /* topmost scroll area under the mouse */
    cui_id active;                  /* item being pressed/dragged */
    cui_id focus;                   /* keyboard focus */
    bool focus_visible;             /* focus came from the keyboard */
    cui_id focus_next_request;
    int focus_move;                 /* +1/-1 tab navigation requested */
    cui_id *focus_order; int focus_count, focus_cap;
    cui_id *prev_focus_order; int prev_focus_count, prev_focus_cap;
    cui_id *focus_layers; int focus_layers_cap;          /* layer of each focus entry */
    cui_id *prev_focus_layers; int prev_focus_layers_cap;
    cui_id modal_layer, prev_modal_layer;               /* topmost open modal */
    int modal_z;
    bool edit_prev;                                     /* a text field had focus last frame */
    cui_id hover_id_for_tooltip;
    double hover_start;
    cui_id layer_stack[32]; int layer_depth;   /* current floating layer while building */
    cui_id current_layer;
    cui_cursor cursor;

    /* input */
    float mx, my, pmx, pmy;
    bool mouse_inside;
    bool mdown[3], mpressed[3], mreleased[3];
    int click_count;
    double last_click_time;
    float last_click_x, last_click_y;
    float scroll_dx, scroll_dy;
    bool kdown[CUI_KEY_COUNT];
    uint8_t kpressed[CUI_KEY_COUNT];
    int mods;
    char text_in[256]; int text_in_len;
    bool window_focused;
    bool input_this_frame;

    /* theme */
    cui_theme theme;
    cui_theme target_theme;
    cui_theme_mode mode;
    cui_accent accent;
    bool reduced_motion;
    float theme_t;              /* theme transition progress */
    cui_theme from_theme;

    /* fonts */
#ifdef CUI_TEXT_SHAPING
    struct cui_shaped_paragraph *shape_cache;
    int shape_cache_count;
    const char *text_paragraph;
    int text_paragraph_len;
#endif
    bool text_unshaped;             /* editing keeps logical glyph positions */
    cui_font_data *fonts; int font_count, font_cap;
    cui_font roles[5];
    cui_glyph *glyphs; int glyph_count, glyph_cap;
    int glyph_buckets[4096];
    uint64_t atlas_check_frame;   /* when the atlas last looked at shrinking */
    uint8_t *atlas; int atlas_w, atlas_h;
    int pack_x, pack_y, pack_row_h;
    bool atlas_dirty; int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
    bool atlas_reset_pending;
    cui_texture atlas_tex;
    int atlas_tex_w, atlas_tex_h;

    /* drawing */
    cui_draw_list dl;
    cui_draw_data draw_data;

    /* widgets */
    cui_popup popups[32]; int popup_count;
    cui_id open_menu_stack[8]; int open_menu_depth;
    cui_toast_item toasts[CUI_MAX_TOASTS]; int toast_count;
    int toast_next_handle;
    cui_id dialog_stack[8]; int dialog_depth;
    cui_id nav_group_last[8];
    int tooltip_node;
    const char *tooltip_text;
    cui_id tooltip_id;
    cui_rect tooltip_rect;
    cui_id tree_last_clicked;
    bool tree_clicked;
    /* text editing */
    struct cui_edit_state *edit;   /* edit state of the focused text field */
    cui_id tab_owner;              /* the focused control types Tab instead of moving focus (code editor); set every frame */
    double caret_time;
    /* table being built */
    struct cui_table_build *table;
    int table_depth;
    /* split being built */
    struct cui_split_build *split;
    /* scope stacks */
    cui__popup_build pb_stack[16]; int pb_depth;
    cui__modal_build mb_stack[8]; int mb_depth;
    cui__vlist_build vl_stack[8]; int vl_depth;
    bool acc_dis[32]; int acc_depth;
    int tree_depth;
    int button_group_depth;
    int layer_seq;             /* increments whenever a popup or modal opens */
    struct cui_reorder_build *reorder;
    /* one shot font override for the next text field (inline edit) */
    bool tf_override;
    cui_font tf_font;
    float tf_size;
    cui_id tf_select_all;
    /* files dropped from the OS, valid for one frame */
    char *drop_buf; size_t drop_used, drop_cap;
    int drop_off[64]; int drop_count;
    float drop_x, drop_y;
    bool drop_claimed;
    bool drag_hover; float drag_x, drag_y;
    bool slider_no_header;     /* next range slider skips its label row */      /* this field selects all of its text when it next has focus */
    struct cui_swipe_build *swipe;
    struct cui_grid_build *grid;   /* data grid being built (grids do not nest) */
    void *carousel;                /* carousel being built */
    void *status_bar;              /* status bar being built: its width budget */
    void *card_stack;              /* card stack being built */
    void *kanban;                  /* kanban board being built */
    void *dock;                    /* dock being built */
    void *spot;                    /* spotlight card being built */
    void *reasoning;               /* reasoning block being built */
    cui__text_fx_fn text_fx;       /* set while a node with a text effect paints */
    void *text_fx_data;
    int text_fx_base;              /* byte offset of the line being drawn */
    float text_fx_line_y;
};

/* ---- theme ---- */
void cui__theme_init(cui_ctx *ui, cui_theme_mode mode, cui_accent accent);
void cui__theme_update(cui_ctx *ui);
void cui__theme_build(cui_theme *t, cui_theme_mode mode, cui_accent accent);
cui_color cui__tone_color(cui_ctx *ui, cui_tone tone);
#define CUI_C(ui, tok) ((ui)->theme.colors[CUI_TOKEN_##tok])

/* ---- widgets internal ---- */
void cui__widgets_begin_frame(cui_ctx *ui);
void cui__widgets_end_frame(cui_ctx *ui);   /* toasts, tooltips */
void cui__tooltip_passive(cui_ctx *ui, cui_id anchor, const char *text);   /* hover by rect, for things that are not interactive */
void cui__overlay_end_frame(cui_ctx *ui);
float cui__control_height(cui_ctx *ui, cui_control_size s);
cui_popup *cui__popup_get(cui_ctx *ui, cui_id id, bool create);
bool cui__popup_begin(cui_ctx *ui, cui_id id, cui_id anchor, int placement, float width, float pad, float gap, bool menu);
void cui__popup_end(cui_ctx *ui);
void cui__popup_set(cui_ctx *ui, cui_id id, bool open);
void cui__close_all_popups(cui_ctx *ui);
void cui__color_picker_open(cui_ctx *ui, cui_id id, cui_color value);   /* opens cui_color_picker's popover from code */
typedef struct { int highlight; int item_count; int prev_item_count; bool keyboard; } cui__menu_nav;
typedef struct { cui_color bg, bg_hover, bg_press, fg, border, border_hover; } cui__btn_colors;
cui__btn_colors cui__button_colors(cui_ctx *ui, cui_button_variant v, bool selected);
/* Edge for controls their label already identifies (outline buttons, chips,
 * groups). Fields, checkboxes and radios keep BORDER_STRONG (3:1): their edge
 * is the only thing that marks them. */
cui_color cui__quiet_edge(cui_ctx *ui, float hover);
cui_color cui__control_edge(cui_ctx *ui, float hover);   /* the 3:1 edge of fields, checks, radios, dropzones */
cui_color cui__edge_on(cui_ctx *ui);                     /* the edge of a focused or open field */
void cui__focus_ring(cui_ctx *ui, int node, float alpha);
void cui__icon_swap(cui_ctx *ui, cui_id state_id, const char *icon, float size, cui_color c);
cui_id cui__position_id(cui_ctx *ui, uint32_t salt);
void cui__chevron_node(cui_ctx *ui, float angle, float size, cui_color color);   /* 0 points down */   /* stable id from the parent and child index */   /* keyboard focus ring painted after the node */
int cui__text_field(cui_ctx *ui, cui_id id, char *buf, size_t cap, const cui_input_opts *o, bool multiline, int style, const char *trailing_icon_default);
int cui__edit_lines(cui_ctx *ui, cui_font f, float size, const char *buf, int len, float width, cui_text_layout_line **out);   /* wrapped lines of an edit buffer */
static inline float cui__fsz(cui_ctx *ui, float size) { return size * ui->theme.font_scale; }
void cui__label_text(cui_ctx *ui, const char *label, cui_font font, float size, cui_color color);
void cui__icon_node(cui_ctx *ui, const char *icon, float size, cui_color color);
void cui__field_label(cui_ctx *ui, const char *label);
void cui__field_message(cui_ctx *ui, const char *hint, const char *error);
int cui__scroll_begin_ex(cui_ctx *ui, cui_id id, const cui_box *box, bool horizontal);
void cui__nice_range(float lo, float hi, int target_ticks, float *out_lo, float *out_hi, float *out_step);
const char *cui__fmt_value(cui_ctx *ui, const char *fmt, float v);
void cui__scroll_end_ex(cui_ctx *ui);
cui_color cui__backdrop(cui_ctx *ui);

void cui__paint_scrollbars(cui_ctx *ui, cui_node *n, cui_rect clip);
void cui__paint_nav_highlight(cui_ctx *ui, cui_node *n, void *data);
void cui__paint_focus_ring_node(cui_ctx *ui, cui_node *n, void *data);
void cui__paint_sheen(cui_ctx *ui, cui_node *n, void *data);   /* data: float strength or NULL */

/* paint data helpers */
#define CUI_PAINT_DATA(ui, type) ((type *)cui__frame_alloc((ui), sizeof(type)))

/* ---- syntax highlighting (cui_code.c), shared with the diff viewer ---- */
#define CUI__CODE_KINDS 10
typedef struct cui__code_tok { int start, len; uint8_t kind; } cui__code_tok;
/* Tokens of one line in lang (the code block's languages); *in_block carries
 * an open block comment to the next line. At most cap (up to 256) tokens. */
int cui__code_tokens(const char *lang, const char *s, int n, bool *in_block, cui__code_tok *out, int cap);
void cui__code_colors(cui_ctx *ui, cui_color *colors);   /* CUI__CODE_KINDS colors, by token kind */

cui_color cui__legible(cui_color fg, cui_color bg, float ratio);
cui_color cui__shade(cui_color c, float L, float chroma_mul);

/* ---- text editing primitives (cui_textedit.c), shared with the code editor ---- */
#define CUI__UNDO_MAX 48
typedef struct cui_edit_state {
    int cursor, anchor;        /* byte offsets */
    float scroll_x, scroll_y;  /* single line horizontal scroll / textarea vertical */
    float preferred_x;         /* for up/down in text areas */
    bool selecting;
    float hover, focus;
    char *undo[CUI__UNDO_MAX];
    int undo_cursor[CUI__UNDO_MAX];
    int undo_count, undo_pos;
    double last_edit;
    int last_kind;
    int last_cursor;
    bool had_focus;
    bool init;
    bool show_password;
} cui_edit_state;
void cui__edit_attach(cui_ctx *ui, cui_id id, cui_edit_state *e);   /* once per state: the history is freed with it */
/* Snapshot before an edit. kind: 1 insert, 2 delete, 3 other; edits of the
 * same kind at the caret within a second merge into one step. */
void cui__edit_undo_push(cui_ctx *ui, cui_edit_state *e, const char *buf, int kind);
bool cui__edit_undo_step(cui_ctx *ui, cui_edit_state *e, char *buf, size_t cap, bool redo);
int cui__edit_insert(char *buf, int *len, size_t cap, int pos, const char *s, int n);   /* drops '\r'; what fits, never half a code point */
void cui__edit_delete(char *buf, int *len, int a, int b);
bool cui__edit_is_word(unsigned char c);
int cui__edit_word_left(const char *s, int pos);
int cui__edit_word_right(const char *s, int len, int pos);

/* ---- syntax highlighting (cui_code.c), shared by the code block and the code editor ---- */
typedef struct { const char *const *kw; const char *const *types; const char *line_comment, *line_comment2; bool block_comments, hash_preproc, backtick, no_caps_types; } cui__lang;
typedef struct { int start, len; uint8_t kind; } cui__tok;
#define CUI__TK_COUNT 10   /* token kinds; colors from cui__syntax_colors */
cui__lang cui__lang_for(const char *lang);
/* Tokenize one line. *in_block carries an open block comment between lines. */
int cui__tokenize(const cui__lang *L, bool json, const char *s, int n, bool *in_block, cui__tok *out, int cap);
void cui__syntax_colors(cui_ctx *ui, cui_color *c);

#endif /* CUI_INTERNAL_H */
