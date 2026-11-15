/* Timeline editor (dope sheet). A track list on the left, a time ruler on
 * top and a row per track with its keys and clips. The caller owns the
 * tracks, their keys (curve keys: x is the time, y the value), the clips, the
 * playhead and the selection (a bool beside each key, a flag in each clip);
 * the editor edits them in place and reports what it did.
 *
 * Like the flame graph, the whole editor is one interactive node: hits are
 * found from the last frame's rectangle with binary searches (keys sorted by
 * time, clips by start), and only the rows in view are visited, so the work
 * per frame follows what is on screen. What is drawn is copied into frame
 * memory, so the paint never reads the caller's arrays. */
#include "cui_internal.h"

#define CUI__TL_RULER   28.0f    /* time ruler along the top */
#define CUI__TL_ROW     26.0f    /* default row height */
#define CUI__TL_LIST    184.0f   /* default track list width */
#define CUI__TL_ICONS   40.0f    /* the track list collapsed to icons */
#define CUI__TL_PADX    10.0f    /* time axis inset from the time area, so end keys show whole */
#define CUI__TL_INDENT  14.0f    /* per nesting level in the track list */
#define CUI__TL_CHEV    18.0f    /* room for a group's chevron */
#define CUI__TL_TOGGLE  22.0f    /* a hide or lock button */
#define CUI__TL_DRAG    3.0f     /* travel before a press becomes a drag */
#define CUI__TL_KEY     5.0f     /* key mark radius */
#define CUI__TL_HIT     7.0f     /* keys are grabbable this far from their center */
#define CUI__TL_EDGE    6.0f     /* clip ends grab this wide */
#define CUI__TL_DEPTH   16       /* deepest nesting followed for groups */

enum { TL_IDLE = 0, TL_SCRUB, TL_KEY, TL_EMPTY, TL_CLIP, TL_CLIP_IN, TL_CLIP_OUT, TL_PAN, TL_MARK_IN, TL_MARK_OUT, TL_NONE };
enum { TL_DIAMOND = 0, TL_CIRCLE, TL_SQUARE };
enum { TL_F_SEL = 1, TL_F_HOVER = 2, TL_F_DIM = 4, TL_F_SUMMARY = 8, TL_F_EDGE_IN = 16, TL_F_EDGE_OUT = 32 };
enum { TL_R_GROUP = 1, TL_R_COLLAPSED = 2, TL_R_MUTED = 4, TL_R_LOCKED = 8, TL_R_CURRENT = 16, TL_R_HOVER = 32,
       TL_R_HOT_CHEV = 64, TL_R_HOT_MUTE = 128, TL_R_HOT_LOCK = 256 };

typedef struct {
    int mode;
    float press_x, press_y, last_x, last_y;
    bool moved, was_selected;
    int mods;
    int grab_track, grab_key, grab_row;
    float grab_t, grab_s, grab_e, grab_off;
    cui_timeline_view own_view;
    float own_time;
    int own_track, own_key;
    bool init;
    cui_spring anim; bool anim_on;
    double a0, a1, b0, b1;
    float last_time;
    float focus;
} cui__tl_state;

typedef struct {
    cui_rect r;
    int list_mode;            /* 0 names and toggles, 1 names, 2 icons only */
    float list_w;
    float area_x, area_w;     /* the time area, right of the list */
    float plot_x, plot_w;     /* the time axis */
    float body_y, body_h;     /* the rows */
    float row_h;
} cui__tl_geo;

typedef struct { float t; int row; uint8_t shape, flags, tone; } cui__tl_mark;
typedef struct { float s, e; int row; uint8_t tone, flags; const char *name; int name_len; } cui__tl_bar;
typedef struct { int row; const char *name, *icon; int name_len; int depth; uint8_t tone; uint16_t flags; } cui__tl_row;

typedef struct {
    cui__tl_state *st;
    cui_id sid;
    float v0, v1, scroll, row_h, track_w;
    bool toggles;
    float seq0, seq1, fps;
    int ruler;
    float time;
    bool range; float in, out;
    int hot_mark;             /* 1 in, 2 out under the pointer */
    int nrows;
    cui__tl_row *rows; int nr;
    cui__tl_mark *marks; int nm, mcap;
    cui__tl_bar *bars; int nb, bcap;
    bool marquee; cui_rect mq;
    float focus;
    bool empty;
} cui__tl_paint;

/* ---- small helpers ---- */

static float cui__tl_snap(float t, float fps) { return roundf(t * fps) / fps; }
static bool cui__tl_on_frame(float t, float fps) { float f = t * fps; return fabsf(f - roundf(f)) < 1e-3f; }

static cui__tl_geo cui__tl_geo_of(cui_rect r, float row_h, float track_w, bool toggles) {
    cui__tl_geo g;
    memset(&g, 0, sizeof g);
    g.r = r;
    g.row_h = row_h;
    float full = track_w > 0 ? track_w : CUI__TL_LIST;
    float lw;
    if (r.w >= full * 3.0f) lw = full;
    else if (r.w >= 380) lw = CUI_MAX(120.0f, CUI_MIN(full, r.w * 0.32f));
    else lw = CUI__TL_ICONS;
    g.list_w = lw;
    g.list_mode = lw <= CUI__TL_ICONS ? 2 : (toggles && lw >= 160 ? 0 : 1);
    g.area_x = r.x + lw;
    g.area_w = CUI_MAX(1.0f, r.w - lw);
    g.plot_x = g.area_x + CUI__TL_PADX;
    g.plot_w = CUI_MAX(1.0f, g.area_w - 2 * CUI__TL_PADX);
    g.body_y = r.y + CUI__TL_RULER + 1;
    g.body_h = CUI_MAX(0.0f, r.h - CUI__TL_RULER - 2);
    return g;
}

/* first key with x >= t */
static int cui__tl_key_lb(const cui_curve_key *k, int n, float t) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = lo + (hi - lo) / 2; if (k[m].x < t) lo = m + 1; else hi = m; }
    return lo;
}
/* first clip with end > t */
static int cui__tl_clip_lb(const cui_timeline_clip *c, int n, float t) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = lo + (hi - lo) / 2; if (c[m].end <= t) lo = m + 1; else hi = m; }
    return lo;
}

static int cui__tl_keys_n(const cui_timeline_track *t) { return t->keys && t->key_count > 0 ? t->key_count : 0; }
static int cui__tl_clips_n(const cui_timeline_track *t) { return t->clips && t->clip_count > 0 ? t->clip_count : 0; }

/* The visible rows (tracks not inside a collapsed group), each track's
 * series tone and the end of its subtree (sub_end > i + 1: a group). */
static int cui__tl_flatten(const cui_timeline_track *t, int n, int *rows, uint8_t *tone, int *sub_end) {
    int stack[CUI__TL_DEPTH];
    int sd = 0, top = 0, nr = 0;
    const int open = 1 << 30;
    int hide = open;
    for (int i = 0; i < n; i++) {
        int d = CUI_CLAMP(t[i].depth, 0, 1000);
        while (sd > 0 && CUI_CLAMP(t[stack[sd - 1]].depth, 0, 1000) >= d) sub_end[stack[--sd]] = i;
        int c = t[i].color;
        if (c >= 1 && c <= 6) tone[i] = (uint8_t)(c - 1);
        else if (sd > 0) tone[i] = tone[stack[sd - 1]];
        else tone[i] = (uint8_t)(top % 6);
        if (sd == 0) top++;
        if (d <= hide) hide = open;
        if (hide == open) rows[nr++] = i;
        sub_end[i] = i + 1;
        bool group = i + 1 < n && CUI_CLAMP(t[i + 1].depth, 0, 1000) > d;
        if (group) {
            if (sd < CUI__TL_DEPTH) stack[sd++] = i;
            if (t[i].collapsed && hide == open) hide = d;
        }
    }
    while (sd > 0) sub_end[stack[--sd]] = n;
    return nr;
}

/* ---- labels ---- */

static const char *cui__tl_timecode(cui_ctx *ui, double t, float fps, bool hours) {
    int F = (int)floorf(fps + 0.5f);
    if (F < 1) F = 1;
    long long f = llround(t * fps);
    const char *sign = f < 0 ? "-" : "";
    if (f < 0) f = -f;
    long long ff = f % F, s = f / F, ss = s % 60, m = s / 60;
    if (hours) return cui__frame_printf(ui, "%s%lld:%02lld:%02lld:%02lld", sign, m / 60, m % 60, ss, ff);
    return cui__frame_printf(ui, "%s%02lld:%02lld:%02lld", sign, m, ss, ff);
}

static int cui__tl_sec_dec(double step) {
    int d = (int)ceil(-log10(step) - 1e-9);
    return CUI_CLAMP(d, 0, 3);
}

static const char *cui__tl_label(cui_ctx *ui, const cui__tl_paint *p, double t, double step) {
    if (p->ruler == CUI_TIMELINE_TIMECODE) return cui__tl_timecode(ui, t, p->fps, p->seq1 >= 3600);
    if (p->ruler == CUI_TIMELINE_SECONDS) {
        double v = fabs(t) < 1e-9 ? 0 : t;
        return cui__frame_printf(ui, "%.*f s", cui__tl_sec_dec(step), v);
    }
    return cui__frame_printf(ui, "%lld", llround(t * p->fps));
}

/* the playhead (or a key) as text: "Frame 24", "1.00 s", "00:01:00" */
static const char *cui__tl_time_text(cui_ctx *ui, int ruler, float fps, float seq1, float t) {
    if (ruler == CUI_TIMELINE_TIMECODE) return cui__tl_timecode(ui, t, fps, seq1 >= 3600);
    if (ruler == CUI_TIMELINE_SECONDS) return cui__frame_printf(ui, "%.2f s", (double)t);
    return cui__frame_printf(ui, "Frame %lld", llround((double)t * fps));
}

static const char *cui__tl_value(cui_ctx *ui, float v) {
    float a = fabsf(v);
    int dec = a >= 100 ? 0 : a >= 10 ? 1 : 2;
    if (a < 0.5f * powf(10.0f, (float)-dec)) v = 0;
    return cui__frame_printf(ui, "%.*f", dec, (double)v);
}

/* The labelled ruler step in seconds: whole frames (frame and timecode
 * rulers) or a 1, 2, 5 step (seconds), wide enough for its labels. */
static double cui__tl_step(cui_ctx *ui, const cui__tl_paint *p, double k, float fs) {
    double fps = p->fps;
    double cand[40];
    int nc = 0;
    if (p->ruler == CUI_TIMELINE_SECONDS) {
        for (double m = 1e-3; m < 1e6 && nc < 36; m *= 10) { cand[nc++] = m; cand[nc++] = 2 * m; cand[nc++] = 5 * m; }
    } else if (p->ruler == CUI_TIMELINE_TIMECODE) {
        static const double fr[] = { 1, 2, 5, 10 };
        static const double sec[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 18000, 36000 };
        double F = floor(fps + 0.5);
        for (int i = 0; i < 4; i++) if (fr[i] < F) cand[nc++] = fr[i] / fps;
        for (int i = 0; i < 16; i++) cand[nc++] = sec[i] * F / fps;
    } else {
        for (double m = 1; m < 1e8 && nc < 36; m *= 10) { cand[nc++] = m / fps; cand[nc++] = 2 * m / fps; cand[nc++] = 5 * m / fps; }
    }
    const char *wide = cui__tl_label(ui, p, p->seq1, cand[0]);
    float lw = cui__text_width(ui, CUI_FONT_BODY, fs, wide, -1, true);
    for (int i = 0; i < nc; i++) {
        if (p->ruler == CUI_TIMELINE_SECONDS) {
            wide = cui__tl_label(ui, p, p->seq1, cand[i]);
            lw = cui__text_width(ui, CUI_FONT_BODY, fs, wide, -1, true);
        }
        if (cand[i] * k >= CUI_MAX(44.0f, lw + 14)) return cand[i];
    }
    return cand[nc - 1];
}

/* ---- painting ---- */

static void cui__tl_shape(cui_ctx *ui, float x, float y, float r, int shape, cui_color c) {
    if (shape == TL_SQUARE) {
        float h = r * 0.84f;
        cui_draw_rect(ui, (cui_rect){ x - h, y - h, 2 * h, 2 * h }, CUI_CORNERS(CUI_MIN(1.5f, h * 0.4f)), c);
    } else if (shape == TL_CIRCLE) {
        cui_draw_circle(ui, (cui_vec2){ x, y }, r * 0.9f, c);
    } else {
        cui_vec2 q[4] = { { x, y - r }, { x + r, y }, { x, y + r }, { x - r, y } };
        cui_draw_polygon(ui, q, 4, c);
    }
}

static cui_color cui__tl_tone(cui_ctx *ui, int tone) { return ui->theme.colors[CUI_TOKEN_SERIES_1 + tone % 6]; }
static cui_color cui__tl_accent(cui_ctx *ui) { return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT); }

/* A fill inside the panel that meets the bottom border: its bottom corners
 * follow the panel's (concentric), so nothing shows past the rounded edge. */
static void cui__tl_fill(cui_ctx *ui, cui_rect pr, float rad, cui_rect q, cui_color c) {
    float bot = pr.y + pr.h - 1, R = rad - 1;
    if (q.y + q.h > bot) q.h = bot - q.y;
    if (q.w <= 0 || q.h <= 0) return;
    cui_corners cr = { 0, 0, 0, 0 };
    if (q.y + q.h > bot - R && q.h >= R) {
        if (q.x < pr.x + 1 + R) cr.bl = R;
        if (q.x + q.w > pr.x + pr.w - 1 - R) cr.br = R;
    }
    cui_draw_rect(ui, q, cr, c);
}
/* a vertical line down to the bottom border, ending on its curve near a corner */
static void cui__tl_vline(cui_ctx *ui, cui_rect pr, float rad, float x, float y0, float w, cui_color c) {
    float R = rad - 1, bot = pr.y + pr.h - 1, cy = bot - R;
    float cx0 = pr.x + 1 + R, cx1 = pr.x + pr.w - 1 - R;
    float dx = x < cx0 ? cx0 - x : x + w > cx1 ? x + w - cx1 : 0;
    if (dx > 0) bot = dx >= R ? cy : cy + sqrtf(R * R - dx * dx);
    if (bot > y0) cui_draw_rect(ui, (cui_rect){ x, y0, w, bot - y0 }, CUI_CORNERS(0), c);
}

static void cui__paint_timeline(cui_ctx *ui, cui_node *n, void *data) {
    cui__tl_paint *p = data;
    cui_rect r = n->rect;
    /* the wheel goes to the editor while the pointer is over its visible part */
    {
        cui_rect clip = ui->dl.clip_stack[ui->dl.clip_depth - 1];
        float s = ui->scale;
        cui_rect cp = { clip.x / s, clip.y / s, clip.w / s, clip.h / s };
        cui_scroll_state *ss = cui_state(ui, p->sid, sizeof *ss);
        ss->view_w = r.w; ss->view_h = r.h;
        ss->content_w = r.w + 1e6f; ss->content_h = r.h + 1e6f;
        cui__paint_add_hit(ui, p->sid, cui__rect_intersect(r, cp), CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y);
    }
    cui__tl_geo g = cui__tl_geo_of(r, p->row_h, p->track_w, p->toggles);
    bool dark = ui->mode == CUI_THEME_DARK;
    float rad = ui->theme.radius_panel, sc = ui->scale;
    double k = (double)g.plot_w / (double)(p->v1 - p->v0);
#define TX(t) ((float)(g.plot_x + ((double)(t) - p->v0) * k))
    cui_color surface = CUI_C(ui, SURFACE), muted = CUI_C(ui, TEXT_MUTED), sub = CUI_C(ui, BORDER_SUBTLE);
    cui_color accent = cui__tl_accent(ui);
    cui_color grid = cui_alpha(sub, dark ? 0.8f : 1.0f);
    cui_color group_bg = cui_alpha(CUI_C(ui, SURFACE_MUTED), dark ? 0.55f : 0.7f);
    float fs = cui__fsz(ui, ui->theme.text_xs), fsm = cui__fsz(ui, ui->theme.text_sm);
    float fh = cui__font_line_height(ui, CUI_FONT_BODY, fs), fhm = cui__font_line_height(ui, CUI_FONT_BODY, fsm);

    double step = cui__tl_step(ui, p, k, fs);
    double first = ceil(p->v0 / step - 1e-6), last = floor(p->v1 / step + 1e-6);

    cui_push_clip(ui, cui__rect_inset(r, 1));
    /* ruler band and the hairlines between the parts */
    cui_draw_rect(ui, (cui_rect){ r.x, r.y, r.w, CUI__TL_RULER }, (cui_corners){ rad - 1, rad - 1, 0, 0 }, CUI_C(ui, SURFACE_MUTED));
    cui_draw_rect(ui, (cui_rect){ r.x, r.y + CUI__TL_RULER, r.w, 1 }, CUI_CORNERS(0), CUI_C(ui, BORDER));

    /* rows */
    cui_rect body = { r.x + 1, g.body_y, CUI_MAX(0.0f, r.w - 2), g.body_h };
    cui_push_clip(ui, body);
    for (int i = 0; i < p->nr; i++) {
        const cui__tl_row *row = &p->rows[i];
        float y = g.body_y + row->row * g.row_h - p->scroll;
        if (row->flags & TL_R_GROUP) cui__tl_fill(ui, r, rad, (cui_rect){ r.x + 1, y, r.w - 2, g.row_h }, group_bg);
        if (row->flags & TL_R_CURRENT) cui__tl_fill(ui, r, rad, (cui_rect){ r.x + 1, y, g.list_w - 1, g.row_h }, CUI_C(ui, ACCENT_SUBTLE));
        else if (row->flags & TL_R_HOVER) cui__tl_fill(ui, r, rad, (cui_rect){ r.x + 1, y, g.list_w - 1, g.row_h }, CUI_C(ui, HOVER));
        if (y + g.row_h < r.y + r.h - 1.5f) cui_draw_rect(ui, (cui_rect){ r.x, y + g.row_h - 1, r.w, 1 }, CUI_CORNERS(0), sub);
    }
    /* the time area */
    cui_push_clip(ui, (cui_rect){ g.area_x, g.body_y, g.area_w - 1, g.body_h });
    for (double q = first; q <= last && q - first < 400; q += 1) {
        cui__tl_vline(ui, r, rad, floorf(TX(q * step) * sc) / sc, g.body_y, 1, grid);
    }
    if (p->range) {
        cui_color shade = cui_alpha(CUI_C(ui, FOREGROUND), dark ? 0.07f : 0.045f);
        float xi = TX(p->in), xo = TX(p->out);
        if (xi > g.area_x) cui_draw_rect(ui, (cui_rect){ g.area_x, g.body_y, xi - g.area_x, g.body_h }, CUI_CORNERS(0), shade);
        if (xo < g.area_x + g.area_w) cui__tl_fill(ui, r, rad, (cui_rect){ xo, g.body_y, g.area_x + g.area_w - 1 - xo, g.body_h }, shade);
    }
    /* clips */
    for (int i = 0; i < p->nb; i++) {
        const cui__tl_bar *b = &p->bars[i];
        float y = g.body_y + b->row * g.row_h - p->scroll + 3, h = g.row_h - 7;
        float x0 = CUI_MAX(TX(b->s), g.area_x - 8), x1 = CUI_MIN(TX(b->e), g.area_x + g.area_w - 6);
        float w = x1 - x0;
        if (w < 1) continue;
        cui_color tone = cui__tl_tone(ui, b->tone);
        float a = b->flags & TL_F_DIM ? CUI__DISABLED : 1.0f;
        cui_color fill = cui_alpha(cui_mix(surface, tone, dark ? 0.32f : 0.2f), a);
        cui_color edge = (b->flags & TL_F_SEL) ? accent : cui_alpha(cui_mix(surface, tone, dark ? 0.6f : 0.5f), a);
        float br = CUI_MIN(CUI__R_SMALL, CUI_MIN(w, h) * 0.5f);
        cui_draw_rect_border(ui, (cui_rect){ x0, y, w, h }, CUI_CORNERS(br), fill, edge, (b->flags & TL_F_SEL) ? 1.5f : 1.0f);
        cui_color grip = cui_mix(surface, tone, 0.85f);
        if ((b->flags & TL_F_EDGE_IN) && w > 8) cui_draw_rect(ui, (cui_rect){ x0 + 2.5f, y + 4, 2, h - 8 }, CUI_CORNERS(1), grip);
        if ((b->flags & TL_F_EDGE_OUT) && w > 8) cui_draw_rect(ui, (cui_rect){ x0 + w - 4.5f, y + 4, 2, h - 8 }, CUI_CORNERS(1), grip);
        if (b->name_len > 0) {
            float vx0 = CUI_MAX(x0, g.area_x + 1), vx1 = CUI_MIN(x0 + w, g.area_x + g.area_w - 1);
            float avail = vx1 - vx0 - 16;
            if (avail >= 20) {
                cui_color fg = cui__legible(cui_alpha(cui_mix(CUI_C(ui, FOREGROUND), tone, 0.3f), a), fill, 4.5f);
                cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fs, vx0 + 8, y + (h - fh) * 0.5f, avail, fg, b->name, b->name_len, false);
            }
        }
    }
    /* the playhead line, under the keys so they stay readable */
    float px = roundf(TX(p->time) * sc) / sc;
    cui__tl_vline(ui, r, rad, px - 0.5f, g.body_y, 1.5f, accent);
    /* keys: summary marks, then keys, the selected ones on top */
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < p->nm; i++) {
            const cui__tl_mark *m = &p->marks[i];
            int want = (m->flags & TL_F_SUMMARY) ? 0 : (m->flags & TL_F_SEL) ? 2 : 1;
            if (want != pass) continue;
            float x = TX(m->t), y = g.body_y + m->row * g.row_h - p->scroll + g.row_h * 0.5f;
            if (pass == 0) {
                cui_draw_circle(ui, (cui_vec2){ x, y }, 2.0f, cui_alpha(muted, 0.75f));
                continue;
            }
            float a = m->flags & TL_F_DIM ? CUI__DISABLED : 1.0f;
            cui_color tone = cui__tl_tone(ui, m->tone);
            if (pass == 2) {
                cui__tl_shape(ui, x, y, CUI__TL_KEY + 2.5f, m->shape, surface);
                cui__tl_shape(ui, x, y, CUI__TL_KEY + 1.0f, m->shape, cui_alpha(accent, a));
                cui__tl_shape(ui, x, y, 1.8f, m->shape, surface);
            } else {
                cui_color c = m->flags & TL_F_HOVER ? cui_mix(tone, CUI_C(ui, FOREGROUND), 0.25f) : tone;
                cui__tl_shape(ui, x, y, CUI__TL_KEY + 1.5f, m->shape, surface);
                cui__tl_shape(ui, x, y, CUI__TL_KEY, m->shape, cui_alpha(c, a));
            }
        }
    if (p->marquee) {
        cui_rect mq = cui__rect_intersect(p->mq, (cui_rect){ g.area_x, g.body_y, g.area_w, g.body_h });
        if (mq.w > 0 && mq.h > 0)
            cui_draw_rect_border(ui, mq, CUI_CORNERS(2), cui_alpha(accent, 0.08f), cui_alpha(accent, 0.7f), 1);
    }
    cui_pop_clip(ui);   /* time area */

    /* the track list */
    cui_push_clip(ui, (cui_rect){ r.x + 1, g.body_y, g.list_w - 1, g.body_h });
    for (int i = 0; i < p->nr; i++) {
        const cui__tl_row *row = &p->rows[i];
        float y = g.body_y + row->row * g.row_h - p->scroll;
        cui_rect cell = { r.x, y, g.list_w, g.row_h - 1 };
        bool group = (row->flags & TL_R_GROUP) != 0, hov = (row->flags & TL_R_HOVER) != 0;
        cui_color tone = cui__tl_tone(ui, row->tone);
        cui_color name_c = row->flags & TL_R_MUTED ? muted : group || (row->flags & TL_R_CURRENT) ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY);
        const char *chev = row->flags & TL_R_COLLAPSED ? CUI_ICON_CHEVRON_RIGHT : CUI_ICON_CHEVRON_DOWN;
        if (g.list_mode == 2) {
            /* icons only: a group's chevron, a track's icon or first letter */
            if (!group) cui_draw_rect(ui, (cui_rect){ r.x + 4, y + (g.row_h - 12) * 0.5f, 2.5f, 12 }, CUI_CORNERS(1.25f), tone);
            cui_rect box = { r.x + 6, y, g.list_w - 6, g.row_h - 1 };
            if (group) cui_draw_icon(ui, chev, box, 14, row->flags & TL_R_HOT_CHEV ? CUI_C(ui, FOREGROUND) : muted);
            else if (row->icon) cui_draw_icon(ui, row->icon, box, 14, name_c);
            else if (row->name_len > 0) {
                int len = cui__utf8_next(row->name, row->name_len, 0);
                float w = cui__text_width(ui, CUI_FONT_MEDIUM, fs, row->name, len, false);
                cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs, box.x + (box.w - w) * 0.5f, y + (g.row_h - 1 - fh) * 0.5f, name_c, row->name, len, false);
            }
            continue;
        }
        float x = r.x + 8 + row->depth * CUI__TL_INDENT;
        if (group) cui_draw_icon(ui, chev, (cui_rect){ x - 1, y, 16, g.row_h - 1 }, 14, row->flags & TL_R_HOT_CHEV ? CUI_C(ui, FOREGROUND) : muted);
        x += CUI__TL_CHEV;
        if (!group) {
            cui_draw_rect(ui, (cui_rect){ x, y + (g.row_h - 12) * 0.5f, 3, 12 }, CUI_CORNERS(1.5f), cui_alpha(tone, row->flags & TL_R_MUTED ? CUI__DISABLED : 1.0f));
            x += 10;
        }
        if (row->icon) {
            cui_draw_icon(ui, row->icon, (cui_rect){ x - 1, y, 16, g.row_h - 1 }, 14, name_c);
            x += 21;
        }
        float right = cell.x + cell.w - 8;
        if (g.list_mode == 0) {
            float lx = cell.x + cell.w - 4 - CUI__TL_TOGGLE, hx = lx - CUI__TL_TOGGLE;
            bool hid = (row->flags & TL_R_MUTED) != 0, lk = (row->flags & TL_R_LOCKED) != 0;
            if (hid || hov) cui_draw_icon(ui, hid ? CUI_ICON_EYE_OFF : CUI_ICON_EYE, (cui_rect){ hx, y, CUI__TL_TOGGLE, g.row_h - 1 }, 14,
                                         row->flags & TL_R_HOT_MUTE ? CUI_C(ui, FOREGROUND) : hid ? CUI_C(ui, TEXT_SECONDARY) : muted);
            if (lk || hov) cui_draw_icon(ui, lk ? CUI_ICON_LOCK : CUI_ICON_UNLOCK, (cui_rect){ lx, y, CUI__TL_TOGGLE, g.row_h - 1 }, 14,
                                        row->flags & TL_R_HOT_LOCK ? CUI_C(ui, FOREGROUND) : lk ? CUI_C(ui, TEXT_SECONDARY) : muted);
            right = hx - 2;
        }
        if (row->name_len > 0 && right - x > 12)
            cui__draw_text_ellipsis(ui, group ? CUI_FONT_MEDIUM : CUI_FONT_BODY, fsm, x, y + (g.row_h - 1 - fhm) * 0.5f, right - x, name_c,
                                    row->name, row->name_len, false);
    }
    cui_pop_clip(ui);
    cui_draw_rect(ui, (cui_rect){ g.area_x - 1, r.y, 1, r.h - 1 }, CUI_CORNERS(0), CUI_C(ui, BORDER));
    /* a quiet scroll indicator when rows are hidden above or below */
    float content_h = p->nrows * g.row_h;
    if (content_h > g.body_h + 0.5f && g.body_h > 8) {
        float th = CUI_MAX(16.0f, g.body_h * g.body_h / content_h);
        float ty = g.body_y + (g.body_h - th) * (p->scroll / (content_h - g.body_h));
        cui_draw_rect(ui, (cui_rect){ r.x + r.w - 6, ty + 2, 3, th - 4 }, CUI_CORNERS(1.5f), cui_alpha(muted, 0.4f));
    }
    if (p->empty) {
        const char *msg = "No tracks";
        float lw = cui__text_width(ui, CUI_FONT_BODY, fsm, msg, -1, false);
        cui__draw_text_line(ui, CUI_FONT_BODY, fsm, g.area_x + (g.area_w - lw) * 0.5f, g.body_y + (g.body_h - fhm) * 0.5f, muted, msg, -1, false);
    }
    cui_pop_clip(ui);   /* body */

    /* the ruler: ticks, labels, the play range and the playhead's handle */
    cui_push_clip(ui, (cui_rect){ g.area_x, r.y, g.area_w - 1, CUI__TL_RULER });
    double fpx = k / p->fps;   /* points per frame */
    double minor = 0;
    if (p->ruler != CUI_TIMELINE_SECONDS && fpx >= 6) minor = 1.0 / p->fps;
    else if (step / 5 * k >= 8) minor = step / 5;
    else if (step / 2 * k >= 8) minor = step / 2;
    if (minor > 0) {
        double m0 = ceil(p->v0 / minor - 1e-6), m1 = floor(p->v1 / minor + 1e-6);
        for (double q = m0; q <= m1 && q - m0 < 2000; q += 1) {
            float x = floorf(TX(q * minor) * sc) / sc;
            cui_draw_rect(ui, (cui_rect){ x, r.y + CUI__TL_RULER - 3, 1, 3 }, CUI_CORNERS(0), CUI_C(ui, BORDER));
        }
    }
    for (double q = first; q <= last && q - first < 400; q += 1) {
        double t = q * step;
        float x = floorf(TX(t) * sc) / sc;
        cui_draw_rect(ui, (cui_rect){ x, r.y + CUI__TL_RULER - 6, 1, 6 }, CUI_CORNERS(0), CUI_C(ui, BORDER_STRONG));
        const char *lbl = cui__tl_label(ui, p, t, step);
        float lw = cui__text_width(ui, CUI_FONT_BODY, fs, lbl, -1, true);
        if (x + 4 + lw <= g.area_x + g.area_w - 6)
            cui__draw_text_line(ui, CUI_FONT_BODY, fs, x + 4, r.y + (CUI__TL_RULER - 5 - fh) * 0.5f, muted, lbl, -1, true);
    }
    if (p->range) {
        for (int side = 0; side < 2; side++) {
            float x = roundf(TX(side ? p->out : p->in) * sc) / sc;
            cui_color c = p->hot_mark == side + 1 ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY);
            float my0 = r.y + CUI__TL_RULER - 9;
            cui_draw_rect(ui, (cui_rect){ x - 0.75f, my0, 1.5f, CUI__TL_RULER - (my0 - r.y) }, CUI_CORNERS(0), c);
            float fx = side ? x - 6 : x;
            cui_draw_rect(ui, (cui_rect){ fx, my0, 6, 4 }, side ? (cui_corners){ 2, 0, 0, 2 } : (cui_corners){ 0, 2, 2, 0 }, c);
        }
    }
    {
        float hw = 11, hh = 14, hy = r.y + CUI__TL_RULER - hh - 1;
        cui_draw_rect(ui, (cui_rect){ px + 0.25f - hw * 0.5f, hy, hw, hh - 4 }, (cui_corners){ 3, 3, 0, 0 }, accent);
        cui_vec2 tri[3] = { { px + 0.25f - hw * 0.5f, hy + hh - 4 }, { px + 0.25f + hw * 0.5f, hy + hh - 4 }, { px + 0.25f, hy + hh + 0.5f } };
        cui_draw_polygon(ui, tri, 3, accent);
        cui_draw_rect(ui, (cui_rect){ px - 0.5f, hy + hh - 2, 1.5f, CUI__TL_RULER - (hy + hh - 2 - r.y) }, CUI_CORNERS(0), accent);
    }
    cui_pop_clip(ui);
    /* the playhead's time in the corner above the track list */
    {
        const char *t = g.list_mode == 2 ? cui__frame_printf(ui, "%lld", llround((double)p->time * p->fps))
                                         : cui__tl_time_text(ui, p->ruler, p->fps, p->seq1, p->time);
        float tw = cui__text_width(ui, CUI_FONT_MEDIUM, fs, t, -1, true);
        float tx = g.list_mode == 2 ? r.x + (g.list_w - tw) * 0.5f : r.x + 12;
        if (tw <= g.list_w - 6)
            cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs, tx, r.y + (CUI__TL_RULER - fh) * 0.5f, CUI_C(ui, TEXT_SECONDARY), t, -1, true);
    }
    if (p->focus > 0.01f)
        cui_draw_rect_border(ui, cui__rect_inset(r, 1), CUI_CORNERS(rad - 1), cui__rgba(0, 0, 0, 0), cui_alpha(CUI_C(ui, FOCUS_RING), p->focus), 2);
    cui_pop_clip(ui);
#undef TX
}

static void cui__tl_push_mark(cui_ctx *ui, cui__tl_paint *p, const cui__tl_mark *m) {
    if (p->nm >= p->mcap) {
        int nc = p->mcap ? p->mcap * 2 : 256;
        cui__tl_mark *nv = cui__frame_alloc(ui, sizeof *nv * (size_t)nc);
        if (p->nm) memcpy(nv, p->marks, sizeof *nv * (size_t)p->nm);
        p->marks = nv; p->mcap = nc;
    }
    p->marks[p->nm++] = *m;
}
static void cui__tl_push_bar(cui_ctx *ui, cui__tl_paint *p, const cui__tl_bar *b) {
    if (p->nb >= p->bcap) {
        int nc = p->bcap ? p->bcap * 2 : 32;
        cui__tl_bar *nv = cui__frame_alloc(ui, sizeof *nv * (size_t)nc);
        if (p->nb) memcpy(nv, p->bars, sizeof *nv * (size_t)p->nb);
        p->bars = nv; p->bcap = nc;
    }
    p->bars[p->nb++] = *b;
}

/* ---- editing ---- */

static bool cui__tl_clear(cui_timeline_track *t, int n) {
    bool any = false;
    for (int i = 0; i < n; i++) {
        int kn = cui__tl_keys_n(&t[i]);
        if (t[i].key_selected)
            for (int j = 0; j < kn; j++) if (t[i].key_selected[j]) { t[i].key_selected[j] = false; any = true; }
        int cn = cui__tl_clips_n(&t[i]);
        for (int j = 0; j < cn; j++) if (t[i].clips[j].selected) { t[i].clips[j].selected = false; any = true; }
    }
    return any;
}

/* After keys moved, slides each back to its sorted place (the selection
 * moves with it); f0 and f1 are key indices to follow, or NULL. */
static void cui__tl_resort(cui_curve_key *k, bool *sel, int n, int *f0, int *f1) {
    for (int i = 1; i < n; i++) {
        int j = i;
        while (j > 0 && k[j - 1].x > k[j].x) {
            cui_curve_key tk = k[j - 1]; k[j - 1] = k[j]; k[j] = tk;
            if (sel) { bool ts = sel[j - 1]; sel[j - 1] = sel[j]; sel[j] = ts; }
            if (f0) { if (*f0 == j) *f0 = j - 1; else if (*f0 == j - 1) *f0 = j; }
            if (f1) { if (*f1 == j) *f1 = j - 1; else if (*f1 == j - 1) *f1 = j; }
            j--;
        }
    }
}

/* Moves the selected keys of the unlocked tracks by delta, as a block kept
 * inside the sequence. Keys on a frame stay on frames when snapping; the
 * grabbed key lands exactly on `exact` when the block was not held back.
 * Grab and current (track, key) follow their keys through the re-sort. */
static bool cui__tl_move(cui_timeline_track *t, int n, float delta, bool snap, float fps, float s0, float s1,
                         int gt, int *gk, float exact, int ct, int *ck) {
    float mn = 1e30f, mx = -1e30f;
    for (int i = 0; i < n; i++) {
        if (t[i].locked || !t[i].key_selected) continue;
        int kn = cui__tl_keys_n(&t[i]);
        for (int j = 0; j < kn; j++)
            if (t[i].key_selected[j]) { mn = CUI_MIN(mn, t[i].keys[j].x); mx = CUI_MAX(mx, t[i].keys[j].x); }
    }
    if (mn > mx || !isfinite(delta)) return false;
    float d = cui__clampf(delta, s0 - mn, s1 - mx);
    bool use_exact = d == delta && gk && isfinite(exact);
    if (d == 0) return false;
    bool moved = false;
    for (int i = 0; i < n; i++) {
        if (t[i].locked || !t[i].key_selected) continue;
        int kn = cui__tl_keys_n(&t[i]);
        bool any = false;
        for (int j = 0; j < kn; j++) {
            if (!t[i].key_selected[j]) continue;
            float x = t[i].keys[j].x, nx = x + d;
            if (use_exact && i == gt && j == *gk) nx = exact;
            else if (snap && cui__tl_on_frame(x, fps)) nx = cui__tl_snap(nx, fps);
            nx = cui__clampf(nx, s0, s1);
            if (nx != x) { t[i].keys[j].x = nx; any = true; }
        }
        if (any) {
            cui__tl_resort(t[i].keys, t[i].key_selected, kn, i == gt ? gk : NULL, i == ct ? ck : NULL);
            moved = true;
        }
    }
    return moved;
}

/* Removes the selected keys and clips of the unlocked tracks; the current
 * key follows (or becomes -1 when it went). */
static void cui__tl_delete(cui_timeline_track *t, int n, int ct, int *ck, int *keys_out, int *clips_out) {
    int rk = 0, rc = 0;
    for (int i = 0; i < n; i++) {
        if (t[i].locked) continue;
        int kn = cui__tl_keys_n(&t[i]);
        if (t[i].key_selected && kn > 0) {
            int w = 0, nck = -1;
            for (int j = 0; j < kn; j++) {
                if (t[i].key_selected[j]) { rk++; continue; }
                if (i == ct && j == *ck) nck = w;
                t[i].keys[w] = t[i].keys[j];
                t[i].key_selected[w] = false;
                w++;
            }
            if (w != kn) {
                for (int j = w; j < kn; j++) t[i].key_selected[j] = false;
                t[i].key_count = w;
                if (i == ct) *ck = nck;
            }
        }
        int cn = cui__tl_clips_n(&t[i]);
        if (cn > 0) {
            int w = 0;
            for (int j = 0; j < cn; j++) {
                if (t[i].clips[j].selected) { rc++; continue; }
                t[i].clips[w++] = t[i].clips[j];
            }
            t[i].clip_count = w;
        }
    }
    *keys_out = rk;
    *clips_out = rc;
}

/* Adds a key on track tr at time x, on the curve. Returns its index (or the
 * index of a key already on that time), -1 when there is no room. */
static int cui__tl_add(cui_timeline_track *tr, float x, bool *existed) {
    *existed = false;
    if (!tr->keys || tr->locked || tr->key_capacity <= 0) return -1;
    int n = tr->key_count > 0 ? tr->key_count : 0;
    int j = cui__tl_key_lb(tr->keys, n, x);
    if (j < n && fabsf(tr->keys[j].x - x) < 1e-5f) { *existed = true; return j; }
    if (j > 0 && fabsf(tr->keys[j - 1].x - x) < 1e-5f) { *existed = true; return j - 1; }
    if (n >= tr->key_capacity) return -1;
    float y = n > 0 ? cui_curve_eval(tr->keys, n, x) : 0;
    memmove(&tr->keys[j + 1], &tr->keys[j], sizeof *tr->keys * (size_t)(n - j));
    if (tr->key_selected) memmove(&tr->key_selected[j + 1], &tr->key_selected[j], sizeof *tr->key_selected * (size_t)(n - j));
    cui_curve_key nk;
    memset(&nk, 0, sizeof nk);
    nk.x = x; nk.y = y;
    /* inside a stepped or straight run the new key keeps that shape */
    nk.mode = (uint8_t)(j > 0 && (tr->keys[j - 1].mode == CUI_CURVE_LINEAR || tr->keys[j - 1].mode == CUI_CURVE_STEP) ? tr->keys[j - 1].mode : CUI_CURVE_SMOOTH);
    tr->keys[j] = nk;
    if (tr->key_selected) tr->key_selected[j] = false;
    tr->key_count = n + 1;
    return j;
}

/* the key nearest to time t on a track within tol seconds, or -1 */
static int cui__tl_key_at(const cui_timeline_track *tr, float t, float tol) {
    int n = cui__tl_keys_n(tr);
    if (!n) return -1;
    int best = -1;
    float bd = tol;
    for (int i = cui__tl_key_lb(tr->keys, n, t - tol), guard = 0; i < n && tr->keys[i].x <= t + tol && guard < 64; i++, guard++) {
        float d = fabsf(tr->keys[i].x - t);
        bool sel = tr->key_selected && tr->key_selected[i];
        if (d < bd || (d == bd && sel) || best < 0) { bd = d; best = i; }
    }
    return best;
}

/* the clip under x on a track: part 0 body, 1 start edge, 2 end edge; -1 none */
static int cui__tl_clip_at(const cui_timeline_track *tr, const cui__tl_geo *g, double v0, double k, float mx, int *part) {
    int n = cui__tl_clips_n(tr);
    if (!n || k <= 0) return -1;
    float t = (float)(v0 + (mx - g->plot_x) / k);
    for (int i = CUI_MAX(0, cui__tl_clip_lb(tr->clips, n, t) - 1), guard = 0; i < n && guard < 3; i++, guard++) {
        const cui_timeline_clip *c = &tr->clips[i];
        float x0 = (float)(g->plot_x + (c->start - v0) * k), x1 = (float)(g->plot_x + (c->end - v0) * k);
        if (mx < x0 - 1 || mx > x1 + 1) continue;
        float edge = CUI_MIN(CUI__TL_EDGE, (x1 - x0) / 3);
        *part = mx - x0 <= edge ? 1 : x1 - mx <= edge ? 2 : 0;
        return i;
    }
    return -1;
}

static void cui__tl_clamp(cui_timeline_view *v, float s0, float s1, float min_range) {
    float ext = s1 - s0;
    float range = v->end - v->start;
    if (!(range > 0) || !isfinite(range)) range = ext;
    range = cui__clampf(range, CUI_MIN(min_range, ext), ext);
    float s = isfinite(v->start) ? v->start : s0;
    if (s + range > s1) s = s1 - range;
    if (s < s0) s = s0;
    v->start = s;
    v->end = s + range;
    if (range == ext) v->end = s1;
}

/* geometric zoom between two views around the point that stays put (as the flame graph) */
static void cui__tl_lerp(const cui__tl_state *st, double p, double *s, double *e) {
    double r0 = st->a1 - st->a0, r1 = st->b1 - st->b0;
    if (!(r0 > 0) || !(r1 > 0) || fabs(r1 - r0) < 1e-6 * r0) {
        *s = st->a0 + (st->b0 - st->a0) * p;
        *e = st->a1 + (st->b1 - st->a1) * p;
        return;
    }
    double fx = (st->a0 * r1 - st->b0 * r0) / (r1 - r0);
    double u = (fx - st->a0) / r0;
    double rr = exp(log(r0) + (log(r1) - log(r0)) * p);
    *s = fx - u * rr;
    *e = *s + rr;
}

/* ---- the component ---- */

cui_timeline_editor_result cui_timeline_editor_ex(cui_ctx *ui, const char *id_str, cui_timeline_track *tracks, int count,
                                                  float *time, const cui_timeline_editor_opts *o) {
    cui_timeline_editor_opts dflt;
    memset(&dflt, 0, sizeof dflt);
    if (!o) o = &dflt;
    cui_timeline_editor_result res;
    memset(&res, 0, sizeof res);
    res.added_track = res.added_key = res.clip_track = res.clip = res.toggled = -1;
    res.hovered_track = res.hovered_key = -1;
    if (!tracks || count < 0) count = 0;

    cui_id id = cui_make_id(ui, id_str ? id_str : "timeline");
    cui_id sid = id ^ 0x71AE5C01u;
    cui__tl_state *st = cui_state(ui, id ^ 0x71AEu, sizeof *st);
    if (!st->init) { st->own_track = st->own_key = -1; st->init = true; }
    cui_timeline_view *v = o->view ? o->view : &st->own_view;
    float *tp = time ? time : &st->own_time;
    int *ctp = o->current_track ? o->current_track : &st->own_track;
    int *ckp = o->current_key ? o->current_key : &st->own_key;

    float s0 = o->start, s1 = o->end;
    if (!isfinite(s0) || !isfinite(s1)) s0 = s1 = 0;
    if (!(s1 > s0)) s1 = s0 + 10;
    float fps = o->fps > 0 && isfinite(o->fps) ? CUI_MIN(o->fps, 1000.0f) : 30;
    float frame = 1.0f / fps;
    float min_range = CUI_MIN(CUI_MAX(8.0f * frame, 1e-3f), s1 - s0);
    float row_h = o->row_height > 0 ? CUI_MAX(12.0f, o->row_height) : CUI__TL_ROW;
    int ruler = o->ruler >= CUI_TIMELINE_FRAMES && o->ruler <= CUI_TIMELINE_TIMECODE ? o->ruler : CUI_TIMELINE_FRAMES;
    bool range_on = o->in && o->out;
    float t_before = *tp;
    if (!isfinite(*tp)) *tp = s0;
    int ct = *ctp, ck = *ckp;
    if (ct < -1 || ct >= count) ct = -1;
    if (ct < 0 || ck < -1 || ck >= cui__tl_keys_n(&tracks[ct])) ck = -1;

    /* rows */
    int *rows = cui__frame_alloc(ui, sizeof(int) * (size_t)CUI_MAX(1, count));
    int *sub_end = cui__frame_alloc(ui, sizeof(int) * (size_t)CUI_MAX(1, count));
    uint8_t *tone = cui__frame_alloc(ui, (size_t)CUI_MAX(1, count));
    int nr = cui__tl_flatten(tracks, count, rows, tone, sub_end);

    bool has_rect = cui__has_prev_rect(ui, id);
    cui_rect R = cui__prev_rect(ui, id);
    cui__tl_geo g = cui__tl_geo_of(has_rect ? R : (cui_rect){ 0, 0, 800, 300 }, row_h, o->track_width, o->toggles);

    float b0 = v->start, b1 = v->end, bs = v->scroll;
    if (!(v->end > v->start) || !isfinite(v->start) || !isfinite(v->end)) { v->start = s0; v->end = s1; st->anim_on = false; }

    cui_interact bi = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    float mx = ui->mx, my = ui->my;
    bool shift = (ui->mods & CUI_MOD_SHIFT) != 0, prim = (ui->mods & CUI_MOD_PRIMARY) != 0, alt = (ui->mods & CUI_MOD_ALT) != 0;
    bool snap = !o->free_drag && !shift;
    bool user_moved = false, relist = false;
    int sel_any = 0;   /* selection changed */
    double k = (double)g.plot_w / (double)(v->end - v->start);
#define T_AT(x) ((float)(v->start + ((double)(x) - g.plot_x) / k))
#define ROW_AT(y) ((int)floorf(((y) - g.body_y + v->scroll) / row_h))

    if (has_rect) {
        /* wheel: zoom time around the pointer; Shift or sideways pans; over the list, scroll the rows */
        if (ui->scroll_target == sid && ui->hot_layer == ui->current_layer && (ui->scroll_dx != 0 || ui->scroll_dy != 0)) {
            double range = v->end - v->start;
            if (mx < g.area_x && ui->scroll_dx == 0) {
                v->scroll -= ui->scroll_dy * 40.0f;
            } else if (ui->scroll_dx != 0 || shift) {
                float dl = ui->scroll_dx != 0 ? ui->scroll_dx : ui->scroll_dy;
                double dt = (double)dl * 40.0 / g.plot_w * range;
                v->start = (float)(v->start - dt); v->end = (float)(v->end - dt);
            } else {
                double f = pow(1.12, (double)ui->scroll_dy);
                double nrng = CUI_MIN(CUI_MAX(range / f, (double)min_range), (double)(s1 - s0));
                double u = (double)(mx - g.plot_x) / g.plot_w;
                double tm = v->start + u * range;
                double ns = tm - u * nrng;
                v->start = (float)ns; v->end = (float)(ns + nrng);
            }
            ui->scroll_dx = ui->scroll_dy = 0;
            user_moved = true;
        }
        /* middle button: pan both ways */
        if (st->mode == TL_IDLE && ui->mpressed[CUI_MOUSE_MIDDLE] && ui->hot == id) {
            st->mode = TL_PAN; st->last_x = mx; st->last_y = my;
        }
        if (st->mode == TL_PAN) {
            double dt = (double)(mx - st->last_x) / k;
            v->start = (float)(v->start - dt); v->end = (float)(v->end - dt);
            v->scroll -= my - st->last_y;
            st->last_x = mx; st->last_y = my;
            cui__set_cursor(ui, CUI_CURSOR_MOVE);
            user_moved = true;
            if (!ui->mdown[CUI_MOUSE_MIDDLE]) st->mode = TL_IDLE;
        }
        k = (double)g.plot_w / (double)(v->end - v->start);

        /* left press */
        if (bi.hovered && ui->mpressed[CUI_MOUSE_LEFT] && st->mode == TL_IDLE) {
            st->press_x = st->last_x = mx; st->press_y = st->last_y = my;
            st->moved = false; st->was_selected = false;
            st->mods = ui->mods;
            st->mode = TL_NONE;
            int row = ROW_AT(my);
            int tr_i = my >= g.body_y && row >= 0 && row < nr ? rows[row] : -1;
            st->grab_row = row;
            if (my < g.body_y - 1) {
                if (mx >= g.area_x) {
                    float xi = range_on ? (float)(g.plot_x + (*o->in - v->start) * k) : 0, xo = range_on ? (float)(g.plot_x + (*o->out - v->start) * k) : 0;
                    if (range_on && fabsf(mx - xi) <= 5 && fabsf(mx - xi) <= fabsf(mx - xo)) st->mode = TL_MARK_IN;
                    else if (range_on && fabsf(mx - xo) <= 5) st->mode = TL_MARK_OUT;
                    else st->mode = TL_SCRUB;
                }
            } else if (mx < g.area_x) {
                if (tr_i >= 0) {
                    cui_timeline_track *tr = &tracks[tr_i];
                    bool group = sub_end[tr_i] > tr_i + 1;
                    float cx = g.list_mode == 2 ? R.x + 6 : R.x + 8 + CUI_CLAMP(tr->depth, 0, 1000) * CUI__TL_INDENT;
                    float lx = R.x + g.list_w - 4 - CUI__TL_TOGGLE, hx = lx - CUI__TL_TOGGLE;
                    if (group && ((g.list_mode == 2) || (mx >= cx - 4 && mx < cx + CUI__TL_CHEV)) ) {
                        tr->collapsed = !tr->collapsed; res.toggled = tr_i; res.changed = relist = true;
                    } else if (g.list_mode == 0 && mx >= lx) {
                        tr->locked = !tr->locked; res.toggled = tr_i; res.changed = true;
                    } else if (g.list_mode == 0 && mx >= hx) {
                        tr->muted = !tr->muted; res.toggled = tr_i; res.changed = true;
                    } else if (group && bi.double_clicked) {
                        tr->collapsed = !tr->collapsed; res.toggled = tr_i; res.changed = relist = true;
                    } else {
                        /* the name: select the track's keys */
                        if (!shift && !prim && cui__tl_clear(tracks, count)) sel_any = 1;
                        int kn = cui__tl_keys_n(tr);
                        if (tr->key_selected && !tr->locked)
                            for (int j = 0; j < kn; j++) if (!tr->key_selected[j]) { tr->key_selected[j] = true; sel_any = 1; }
                        ct = tr_i; ck = -1;
                    }
                }
            } else if (tr_i >= 0) {
                cui_timeline_track *tr = &tracks[tr_i];
                int kh = cui__tl_key_at(tr, T_AT(mx), (float)(CUI__TL_HIT / k));
                int part = 0, ch = kh < 0 ? cui__tl_clip_at(tr, &g, v->start, k, mx, &part) : -1;
                if (kh >= 0) {
                    ct = tr_i; ck = kh;
                    st->grab_track = tr_i; st->grab_key = kh; st->grab_t = tr->keys[kh].x;
                    bool *sel = tr->key_selected;
                    if (!sel || tr->locked) {
                        /* shown, not editable */
                    } else if (prim) {
                        sel[kh] = !sel[kh]; sel_any = 1;
                        if (sel[kh]) st->mode = TL_KEY; else ck = -1;
                    } else if (shift) {
                        if (!sel[kh]) { sel[kh] = true; sel_any = 1; }
                        st->mode = TL_KEY;
                    } else if (!sel[kh]) {
                        cui__tl_clear(tracks, count);
                        sel[kh] = true; sel_any = 1;
                        st->mode = TL_KEY;
                    } else {
                        st->was_selected = true;
                        st->mode = TL_KEY;
                    }
                } else if (ch >= 0 && !tr->locked) {
                    cui_timeline_clip *c = &tr->clips[ch];
                    st->grab_track = tr_i; st->grab_key = ch;
                    st->grab_s = c->start; st->grab_e = c->end; st->grab_off = c->offset;
                    if (prim) { c->selected = !c->selected; sel_any = 1; }
                    else if (shift) { if (!c->selected) { c->selected = true; sel_any = 1; } }
                    else if (!c->selected) { if (cui__tl_clear(tracks, count)) sel_any = 1; c->selected = true; sel_any = 1; }
                    else st->was_selected = true;
                    ct = tr_i; ck = -1;
                    if (c->selected) st->mode = part == 1 ? TL_CLIP_IN : part == 2 ? TL_CLIP_OUT : TL_CLIP;
                } else if (bi.double_clicked && !tr->locked) {
                    float t = T_AT(mx);
                    if (!o->free_drag) t = cui__tl_snap(t, fps);
                    t = cui__clampf(t, s0, s1);
                    bool existed;
                    int j = cui__tl_add(tr, t, &existed);
                    if (j >= 0) {
                        cui__tl_clear(tracks, count);
                        if (tr->key_selected) tr->key_selected[j] = true;
                        sel_any = 1;
                        ct = tr_i; ck = j;
                        if (!existed) { res.added_track = tr_i; res.added_key = j; res.changed = true; }
                    }
                } else {
                    st->mode = TL_EMPTY;
                }
            } else if (my >= g.body_y) {
                st->mode = TL_EMPTY;
            }
            if (st->mode == TL_SCRUB) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
        }
        /* drags */
        if (st->mode != TL_IDLE && st->mode != TL_PAN && ui->mdown[CUI_MOUSE_LEFT]) {
            float dx = mx - st->press_x, dy = my - st->press_y;
            if (!st->moved && dx * dx + dy * dy > CUI__TL_DRAG * CUI__TL_DRAG) st->moved = true;
            float t = T_AT(mx);
            switch (st->mode) {
            case TL_SCRUB: {
                t = cui__clampf(t, CUI_MAX(s0, v->start), CUI_MIN(s1, v->end));
                if (snap) t = cui__clampf(cui__tl_snap(t, fps), s0, s1);
                *tp = t;
                res.scrubbing = true;
                cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
            } break;
            case TL_MARK_IN: case TL_MARK_OUT: {
                if (snap) t = cui__tl_snap(t, fps);
                if (st->mode == TL_MARK_IN) *o->in = cui__clampf(t, s0, CUI_MAX(s0, *o->out - frame));
                else *o->out = cui__clampf(t, CUI_MIN(s1, *o->in + frame), s1);
                res.changed = true;
                cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
            } break;
            case TL_KEY: {
                if (!st->moved) break;
                cui_timeline_track *gtr = st->grab_track >= 0 && st->grab_track < count ? &tracks[st->grab_track] : NULL;
                if (!gtr || st->grab_key < 0 || st->grab_key >= cui__tl_keys_n(gtr)) break;
                float tg = st->grab_t + (float)(dx / k);
                if (snap) tg = cui__tl_snap(tg, fps);
                float cur = gtr->keys[st->grab_key].x;
                int fk = ck, gk = st->grab_key;
                if (cui__tl_move(tracks, count, tg - cur, snap, fps, s0, s1, st->grab_track, &gk, tg, ct, &fk)) {
                    res.keys_moved = res.changed = true;
                }
                st->grab_key = gk; ck = fk;
                cui__set_cursor(ui, CUI_CURSOR_MOVE);
            } break;
            case TL_CLIP: case TL_CLIP_IN: case TL_CLIP_OUT: {
                cui_timeline_track *tr = st->grab_track >= 0 && st->grab_track < count ? &tracks[st->grab_track] : NULL;
                int cn = tr ? cui__tl_clips_n(tr) : 0;
                if (!tr || st->grab_key < 0 || st->grab_key >= cn || !st->moved) break;
                cui_timeline_clip *c = &tr->clips[st->grab_key];
                float lo = st->grab_key > 0 ? CUI_MAX(s0, tr->clips[st->grab_key - 1].end) : s0;
                float hi = st->grab_key + 1 < cn ? CUI_MIN(s1, tr->clips[st->grab_key + 1].start) : s1;
                float d = (float)(dx / k);
                float ps = c->start, pe = c->end, po = c->offset;
                if (st->mode == TL_CLIP) {
                    float len = st->grab_e - st->grab_s;
                    float ns = st->grab_s + d;
                    if (snap) ns = cui__tl_snap(ns, fps);
                    ns = cui__clampf(ns, lo, CUI_MAX(lo, hi - len));
                    c->start = ns; c->end = ns + len;
                } else if (st->mode == TL_CLIP_IN) {
                    float ns = st->grab_s + d;
                    if (snap) ns = cui__tl_snap(ns, fps);
                    ns = cui__clampf(ns, lo, CUI_MAX(lo, st->grab_e - frame));
                    c->start = ns; c->offset = st->grab_off + (ns - st->grab_s);
                } else {
                    float ne = st->grab_e + d;
                    if (snap) ne = cui__tl_snap(ne, fps);
                    c->end = cui__clampf(ne, CUI_MIN(hi, st->grab_s + frame), hi);
                }
                if (c->start != ps || c->end != pe || c->offset != po) {
                    res.clip_track = st->grab_track; res.clip = st->grab_key; res.changed = true;
                }
                cui__set_cursor(ui, st->mode == TL_CLIP ? CUI_CURSOR_MOVE : CUI_CURSOR_RESIZE_EW);
            } break;
            default: break;
            }
        }
        /* release */
        if (st->mode != TL_IDLE && st->mode != TL_PAN && !ui->mdown[CUI_MOUSE_LEFT]) {
            bool additive = (st->mods & (CUI_MOD_SHIFT | CUI_MOD_PRIMARY)) != 0;
            if (st->mode == TL_KEY && !st->moved && st->was_selected && !additive) {
                /* a click on a key of a selection selects just that key */
                cui_timeline_track *tr = st->grab_track < count ? &tracks[st->grab_track] : NULL;
                if (tr && tr->key_selected && st->grab_key < cui__tl_keys_n(tr)) {
                    cui__tl_clear(tracks, count);
                    tr->key_selected[st->grab_key] = true;
                    sel_any = 1;
                }
            } else if ((st->mode == TL_CLIP || st->mode == TL_CLIP_IN || st->mode == TL_CLIP_OUT) && !st->moved && st->was_selected && !additive) {
                cui_timeline_track *tr = st->grab_track < count ? &tracks[st->grab_track] : NULL;
                if (tr && st->grab_key < cui__tl_clips_n(tr)) {
                    cui__tl_clear(tracks, count);
                    tr->clips[st->grab_key].selected = true;
                    sel_any = 1;
                }
            } else if (st->mode == TL_EMPTY) {
                if (!st->moved) {
                    if (!additive && cui__tl_clear(tracks, count)) sel_any = 1;
                    if (!additive) {
                        ck = -1;
                        if (st->grab_row >= 0 && st->grab_row < nr) ct = rows[st->grab_row];
                    }
                } else {
                    /* box selection */
                    if (!additive && cui__tl_clear(tracks, count)) sel_any = 1;
                    float ta = T_AT(CUI_MIN(st->press_x, mx)), tb = T_AT(CUI_MAX(st->press_x, mx));
                    int ra = ROW_AT(CUI_MIN(st->press_y, my)), rb = ROW_AT(CUI_MAX(st->press_y, my));
                    ra = CUI_MAX(ra, 0); rb = CUI_MIN(rb, nr - 1);
                    for (int ri = ra; ri <= rb; ri++) {
                        cui_timeline_track *tr = &tracks[rows[ri]];
                        if (tr->locked || !tr->key_selected) continue;
                        int kn = cui__tl_keys_n(tr);
                        for (int j = cui__tl_key_lb(tr->keys, kn, ta); j < kn && tr->keys[j].x <= tb; j++)
                            if (!tr->key_selected[j]) { tr->key_selected[j] = true; sel_any = 1; }
                    }
                }
            }
            st->mode = TL_IDLE;
        }

        /* keyboard */
        if (bi.focused && !ui->edit_prev) {
            int steps = shift ? 10 : 1;
            bool left = cui_key_pressed(ui, CUI_KEY_LEFT) != 0, right = cui_key_pressed(ui, CUI_KEY_RIGHT) != 0;
            if ((left || right) && alt) {
                int fk = ck;
                if (cui__tl_move(tracks, count, (right ? 1.0f : -1.0f) * (float)steps * frame, true, fps, s0, s1, -1, NULL, NAN, ct, &fk))
                    res.keys_moved = res.changed = true;
                ck = fk;
            } else if (left || right) {
                float t = cui__tl_on_frame(*tp, fps) ? cui__tl_snap(*tp, fps) : (right ? floorf(*tp * fps) : ceilf(*tp * fps)) / fps;
                t = cui__tl_snap(t + (right ? 1.0f : -1.0f) * (float)steps * frame, fps);
                *tp = cui__clampf(t, s0, s1);
            }
            if (cui_key_pressed(ui, CUI_KEY_UP) || cui_key_pressed(ui, CUI_KEY_DOWN)) {
                bool next = cui_key_pressed(ui, CUI_KEY_DOWN) != 0;
                float best = next ? 1e30f : -1e30f, t = *tp, eps = frame * 0.01f;
                for (int ri = 0; ri < nr; ri++) {
                    const cui_timeline_track *tr = &tracks[rows[ri]];
                    int kn = cui__tl_keys_n(tr);
                    if (!kn) continue;
                    int j = cui__tl_key_lb(tr->keys, kn, next ? t + eps : t - eps);
                    if (next && j < kn) { while (j < kn && tr->keys[j].x <= t + eps) j++; if (j < kn) best = CUI_MIN(best, tr->keys[j].x); }
                    if (!next && j > 0) best = CUI_MAX(best, tr->keys[j - 1].x);
                }
                if (fabsf(best) < 1e29f) *tp = best;
            }
            ui->kpressed[CUI_KEY_LEFT] = ui->kpressed[CUI_KEY_RIGHT] = ui->kpressed[CUI_KEY_UP] = ui->kpressed[CUI_KEY_DOWN] = 0;
            if (cui_key_pressed(ui, CUI_KEY_HOME)) { *tp = s0; ui->kpressed[CUI_KEY_HOME] = 0; }
            if (cui_key_pressed(ui, CUI_KEY_END)) { *tp = s1; ui->kpressed[CUI_KEY_END] = 0; }
            if (cui_key_pressed(ui, CUI_KEY_SPACE)) { res.play_toggled = true; ui->kpressed[CUI_KEY_SPACE] = 0; }
            if (cui_key_pressed(ui, CUI_KEY_DELETE) || cui_key_pressed(ui, CUI_KEY_BACKSPACE)) {
                cui__tl_delete(tracks, count, ct, &ck, &res.removed_keys, &res.removed_clips);
                if (res.removed_keys || res.removed_clips) { res.changed = true; sel_any = 1; }
                ui->kpressed[CUI_KEY_DELETE] = ui->kpressed[CUI_KEY_BACKSPACE] = 0;
            }
            if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
                if (cui__tl_clear(tracks, count)) sel_any = 1;
                ck = -1;
                ui->kpressed[CUI_KEY_ESCAPE] = 0;
            }
            if (prim && cui_key_pressed(ui, CUI_KEY_A)) {
                for (int ri = 0; ri < nr; ri++) {
                    cui_timeline_track *tr = &tracks[rows[ri]];
                    if (tr->locked || !tr->key_selected) continue;
                    int kn = cui__tl_keys_n(tr);
                    for (int j = 0; j < kn; j++) if (!tr->key_selected[j]) { tr->key_selected[j] = true; sel_any = 1; }
                }
                ui->kpressed[CUI_KEY_A] = 0;
            }
            if (cui_key_pressed(ui, CUI_KEY_EQUAL) || cui_key_pressed(ui, CUI_KEY_MINUS)) {
                double f = cui_key_pressed(ui, CUI_KEY_EQUAL) ? 1 / 1.25 : 1.25;
                double range = v->end - v->start;
                double nrng = CUI_MIN(CUI_MAX(range * f, (double)min_range), (double)(s1 - s0));
                double c = CUI_MIN(CUI_MAX((double)*tp, (double)v->start), (double)v->end);
                double u = (c - v->start) / range;
                v->start = (float)(c - u * nrng); v->end = (float)(c - u * nrng + nrng);
                user_moved = true;
                ui->kpressed[CUI_KEY_EQUAL] = ui->kpressed[CUI_KEY_MINUS] = 0;
            }
            if (cui_key_pressed(ui, CUI_KEY_F) && !prim) {
                st->a0 = v->start; st->a1 = v->end; st->b0 = s0; st->b1 = s1;
                st->anim.x = 0; st->anim.v = 0;
                st->anim_on = true;
                ui->kpressed[CUI_KEY_F] = 0;
            }
        }
        if (user_moved) st->anim_on = false;
        /* the view follows the playhead when it leaves the view (keys, playback) */
        if (*tp != st->last_time && st->mode != TL_SCRUB && st->mode != TL_PAN && !st->anim_on) {
            float range = v->end - v->start;
            if (*tp > v->end) { v->start = *tp - range * 0.1f; v->end = v->start + range; }
            else if (*tp < v->start) { v->start = *tp - range * 0.9f; v->end = v->start + range; }
        }
        if (st->anim_on) {
            float pr = cui__spring(ui, &st->anim, 1.0f, CUI__SPRING_EXPAND);
            double a, b;
            cui__tl_lerp(st, pr >= 1.0f ? 1.0 : (double)pr, &a, &b);
            v->start = (float)a; v->end = (float)b;
            if (pr >= 1.0f && st->anim.v == 0) { v->start = (float)st->b0; v->end = (float)st->b1; st->anim_on = false; }
        } else {
            cui__tl_clamp(v, s0, s1, min_range);
        }
    } else {
        cui__tl_clamp(v, s0, s1, min_range);
    }
    if (relist) nr = cui__tl_flatten(tracks, count, rows, tone, sub_end);
    {
        float max_s = CUI_MAX(0.0f, nr * row_h - g.body_h);
        if (!(v->scroll >= 0)) v->scroll = 0;
        if (v->scroll > max_s) v->scroll = max_s;
    }
    if (ct < 0 || ct >= count) ct = -1;
    if (ct < 0 || ck >= cui__tl_keys_n(&tracks[ct])) ck = -1;
    k = (double)g.plot_w / (double)(v->end - v->start);

    /* hover */
    int hov_track = -1, hov_key = -1, hov_clip = -1, hov_part = 0, hov_row = -1, hov_list_part = 0, hot_mark = 0;
    bool idle = st->mode == TL_IDLE && !ui->mdown[CUI_MOUSE_LEFT];
    if (has_rect && bi.hovered && cui__rect_contains(R, mx, my) && (idle || st->mode == TL_NONE)) {
        int row = ROW_AT(my);
        if (my >= g.body_y && row >= 0 && row < nr) {
            hov_row = row;
            int ti = rows[row];
            if (mx >= g.area_x) {
                const cui_timeline_track *tr = &tracks[ti];
                int kh = cui__tl_key_at(tr, T_AT(mx), (float)(CUI__TL_HIT / k));
                if (kh >= 0) { hov_track = ti; hov_key = kh; if (!tr->locked && tr->key_selected) cui__set_cursor(ui, CUI_CURSOR_HAND); }
                else if (!tr->locked) {
                    hov_clip = cui__tl_clip_at(tr, &g, v->start, k, mx, &hov_part);
                    if (hov_clip >= 0) { hov_track = ti; cui__set_cursor(ui, hov_part ? CUI_CURSOR_RESIZE_EW : CUI_CURSOR_HAND); }
                }
            } else {
                const cui_timeline_track *tr = &tracks[ti];
                bool group = sub_end[ti] > ti + 1;
                float cx = R.x + 8 + CUI_CLAMP(tr->depth, 0, 1000) * CUI__TL_INDENT;
                float lx = R.x + g.list_w - 4 - CUI__TL_TOGGLE, hx = lx - CUI__TL_TOGGLE;
                if (group && (g.list_mode == 2 || (mx >= cx - 4 && mx < cx + CUI__TL_CHEV))) hov_list_part = 1;
                else if (g.list_mode == 0 && mx >= lx) hov_list_part = 3;
                else if (g.list_mode == 0 && mx >= hx) hov_list_part = 2;
                if (hov_list_part) cui__set_cursor(ui, CUI_CURSOR_HAND);
            }
        } else if (my < g.body_y - 1 && mx >= g.area_x && range_on) {
            float xi = (float)(g.plot_x + (*o->in - v->start) * k), xo = (float)(g.plot_x + (*o->out - v->start) * k);
            if (fabsf(mx - xi) <= 5) hot_mark = 1;
            else if (fabsf(mx - xo) <= 5) hot_mark = 2;
            if (hot_mark) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
        }
    }
    res.hovered_track = hov_key >= 0 ? hov_track : -1;
    res.hovered_key = hov_key;

    /* the panel */
    int en = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[en];
    nd->w = o->width.kind == CUI_SIZE_FIT && o->width.value == 0 ? CUI_FILL : o->width;
    float def_h = CUI__TL_RULER + 2 + CUI_MAX(3, count) * row_h;
    nd->h = CUI_PX(o->height > 0 ? o->height : CUI_MIN(360.0f, def_h));
    nd->bg = CUI_C(ui, SURFACE);
    nd->border = 1;
    nd->border_color = CUI_C(ui, BORDER);
    nd->radius = CUI_CORNERS(ui->theme.radius_panel);
    nd->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, en, CUI_IF_FOCUSABLE);
    cui__tl_paint *p = cui__frame_alloc(ui, sizeof *p);
    memset(p, 0, sizeof *p);
    p->st = st; p->sid = sid;
    p->v0 = v->start; p->v1 = v->end; p->scroll = v->scroll;
    p->row_h = row_h; p->track_w = o->track_width; p->toggles = o->toggles;
    p->seq0 = s0; p->seq1 = s1; p->fps = fps; p->ruler = ruler;
    p->time = cui__clampf(*tp, s0, s1);
    p->range = range_on;
    if (range_on) { p->in = *o->in; p->out = *o->out; }
    p->hot_mark = hot_mark;
    p->nrows = nr;
    p->empty = count == 0;
    cui__tween(ui, &st->focus, bi.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    p->focus = st->focus;
    nd->paint = cui__paint_timeline;
    nd->paint_data = p;

    /* what is in view */
    bool mq_on = st->mode == TL_EMPTY && st->moved;
    bool mq_add = (st->mods & (CUI_MOD_SHIFT | CUI_MOD_PRIMARY)) != 0;
    float mqx0 = 0, mqx1 = 0;
    int mqr0 = 0, mqr1 = -1;
    if (mq_on) {
        p->marquee = true;
        p->mq = (cui_rect){ CUI_MIN(st->press_x, mx), CUI_MIN(st->press_y, my), fabsf(mx - st->press_x), fabsf(my - st->press_y) };
        mqx0 = p->mq.x; mqx1 = p->mq.x + p->mq.w;
        mqr0 = ROW_AT(p->mq.y); mqr1 = ROW_AT(p->mq.y + p->mq.h);
    }
    if (nr > 0 && g.body_h > 0) {
        int r0 = CUI_MAX(0, (int)floorf(v->scroll / row_h)), r1 = CUI_MIN(nr - 1, (int)floorf((v->scroll + g.body_h) / row_h));
        p->rows = cui__frame_alloc(ui, sizeof *p->rows * (size_t)CUI_MAX(1, r1 - r0 + 1));
        float margin = (float)((CUI__TL_KEY + 3) / k);
        float tv0 = v->start - margin, tv1 = v->end + margin;
        int pw = (int)g.plot_w + 4 * (int)CUI__TL_PADX + 4;
        uint8_t *seen = NULL;
        for (int ri = r0; ri <= r1; ri++) {
            int ti = rows[ri];
            cui_timeline_track *tr = &tracks[ti];
            bool group = sub_end[ti] > ti + 1;
            cui__tl_row *row = &p->rows[p->nr++];
            memset(row, 0, sizeof *row);
            row->row = ri;
            row->name_len = tr->name ? cui__display_len(tr->name) : 0;
            row->name = row->name_len ? cui__frame_strdup(ui, tr->name, row->name_len) : NULL;
            row->icon = tr->icon ? cui__frame_strdup(ui, tr->icon, (int)strlen(tr->icon)) : NULL;
            row->depth = CUI_CLAMP(tr->depth, 0, 64);
            row->tone = tone[ti];
            row->flags = (uint16_t)((group ? TL_R_GROUP : 0) | (tr->collapsed && group ? TL_R_COLLAPSED : 0) | (tr->muted ? TL_R_MUTED : 0) |
                                    (tr->locked ? TL_R_LOCKED : 0) | (ti == ct ? TL_R_CURRENT : 0) | (ri == hov_row && mx < g.area_x ? TL_R_HOVER : 0));
            if (ri == hov_row && hov_list_part) row->flags |= (uint16_t)(hov_list_part == 1 ? TL_R_HOT_CHEV : hov_list_part == 2 ? TL_R_HOT_MUTE : TL_R_HOT_LOCK);
            uint8_t dim = tr->muted ? TL_F_DIM : 0;
            /* clips */
            int cn = cui__tl_clips_n(tr);
            for (int j = CUI_MAX(0, cui__tl_clip_lb(tr->clips, cn, tv0)); j < cn && tr->clips[j].start <= tv1; j++) {
                cui__tl_bar b;
                memset(&b, 0, sizeof b);
                const cui_timeline_clip *c = &tr->clips[j];
                b.s = c->start; b.e = c->end; b.row = ri; b.tone = tone[ti];
                b.flags = (uint8_t)(dim | (c->selected ? TL_F_SEL : 0));
                if (ti == hov_track && j == hov_clip) b.flags |= (uint8_t)(hov_part == 1 ? TL_F_EDGE_IN : hov_part == 2 ? TL_F_EDGE_OUT : TL_F_EDGE_IN | TL_F_EDGE_OUT);
                if (c->name) { b.name_len = cui__display_len(c->name); b.name = cui__frame_strdup(ui, c->name, b.name_len); }
                cui__tl_push_bar(ui, p, &b);
            }
            /* keys, one per point at most (selected ones always) */
            int kn = cui__tl_keys_n(tr);
            bool in_mq = mq_on && ri >= mqr0 && ri <= mqr1 && !tr->locked && tr->key_selected;
            float last_x = -1e9f;
            for (int j = cui__tl_key_lb(tr->keys, kn, tv0); j < kn && tr->keys[j].x <= tv1;) {
                float x = (float)(g.plot_x + (tr->keys[j].x - v->start) * k);
                bool sel = tr->key_selected && tr->key_selected[j];
                if (mq_on) {
                    bool inside = in_mq && x >= mqx0 && x <= mqx1;
                    sel = mq_add ? sel || inside : inside;
                }
                if (!sel && x - last_x < 1.0f) {
                    int nj = cui__tl_key_lb(tr->keys, kn, (float)(v->start + (last_x + 1.0f - g.plot_x) / k));
                    j = CUI_MAX(nj, j + 1);
                    continue;
                }
                cui__tl_mark m;
                int mode = tr->keys[j].mode;
                m.t = tr->keys[j].x; m.row = ri;
                m.shape = (uint8_t)(mode == CUI_CURVE_LINEAR ? TL_CIRCLE : mode == CUI_CURVE_STEP ? TL_SQUARE : TL_DIAMOND);
                m.flags = (uint8_t)(dim | (sel ? TL_F_SEL : 0) | (ti == hov_track && j == hov_key ? TL_F_HOVER : 0));
                m.tone = tone[ti];
                cui__tl_push_mark(ui, p, &m);
                if (!sel) last_x = x;
                j++;
            }
            /* a group's row: its tracks' keys as small marks, one per point */
            if (group) {
                if (!seen) seen = cui__frame_alloc(ui, (size_t)pw);
                memset(seen, 0, (size_t)pw);
                for (int d = ti + 1; d < sub_end[ti]; d++) {
                    const cui_timeline_track *dt = &tracks[d];
                    int dn = cui__tl_keys_n(dt);
                    for (int j = cui__tl_key_lb(dt->keys, dn, tv0); j < dn && dt->keys[j].x <= tv1; j++) {
                        int px = (int)((dt->keys[j].x - v->start) * k) + 2 * (int)CUI__TL_PADX;
                        if (px < 0 || px >= pw) continue;
                        if (seen[px]) {
                            /* skip the keys at this point */
                            int nj = cui__tl_key_lb(dt->keys, dn, (float)(v->start + (px + 1 - 2 * CUI__TL_PADX) / k));
                            j = CUI_MAX(nj, j + 1) - 1;
                            continue;
                        }
                        seen[px] = 1;
                        cui__tl_mark m;
                        m.t = dt->keys[j].x; m.row = ri; m.shape = TL_DIAMOND; m.flags = TL_F_SUMMARY; m.tone = 0;
                        cui__tl_push_mark(ui, p, &m);
                    }
                }
            }
        }
    }

    /* tooltips anchor on a small box at the thing they describe */
    cui__open(ui, en);
    if (has_rect && ui->active == 0 && !ui->mdown[CUI_MOUSE_LEFT]) {
        const char *tip = NULL;
        cui_rect box = { 0, 0, 0, 0 };
        uint32_t key[3] = { 0, 0, 0 };
        if (hov_key >= 0) {
            const cui_timeline_track *tr = &tracks[hov_track];
            const cui_curve_key *kk = &tr->keys[hov_key];
            float x = (float)(g.plot_x + (kk->x - v->start) * k), y = g.body_y + hov_row * row_h - v->scroll;
            box = (cui_rect){ x - 6, y + 2, 12, row_h - 4 };
            tip = cui__frame_printf(ui, "%.*s\n%s \xc2\xb7 %s", tr->name ? cui__display_len(tr->name) : 0, tr->name ? tr->name : "",
                                    cui__tl_time_text(ui, ruler, fps, s1, kk->x), cui__tl_value(ui, kk->y));
            key[0] = 1; key[1] = (uint32_t)hov_track; key[2] = (uint32_t)hov_key;
        } else if (hov_clip >= 0 && hov_part == 0) {
            const cui_timeline_clip *c = &tracks[hov_track].clips[hov_clip];
            float y = g.body_y + hov_row * row_h - v->scroll;
            box = (cui_rect){ mx - 1, y + 3, 2, row_h - 7 };
            tip = cui__frame_printf(ui, "%.*s\n%s \xc2\xb7 %.2f s", c->name ? cui__display_len(c->name) : 0, c->name ? c->name : "",
                                    cui__tl_time_text(ui, ruler, fps, s1, c->start), (double)(c->end - c->start));
            key[0] = 2; key[1] = (uint32_t)hov_track; key[2] = (uint32_t)hov_clip;
        } else if (hov_row >= 0 && mx < g.area_x) {
            const cui_timeline_track *tr = &tracks[rows[hov_row]];
            float y = g.body_y + hov_row * row_h - v->scroll;
            box = (cui_rect){ R.x + 2, y, g.list_w - 4, row_h - 1 };
            if (hov_list_part == 2) tip = tr->muted ? "Show track" : "Hide track";
            else if (hov_list_part == 3) tip = tr->locked ? "Unlock track" : "Lock track";
            else if (g.list_mode == 2 && tr->name) tip = cui__frame_printf(ui, "%.*s", cui__display_len(tr->name), tr->name);
            if (hov_list_part >= 2) box = (cui_rect){ hov_list_part == 3 ? R.x + g.list_w - 4 - CUI__TL_TOGGLE : R.x + g.list_w - 4 - 2 * CUI__TL_TOGGLE, y, CUI__TL_TOGGLE, row_h - 1 };
            key[0] = 3 + (uint32_t)hov_list_part; key[1] = (uint32_t)rows[hov_row];
        }
        if (tip) {
            cui_id tid = cui__hash(key, sizeof key, id ^ 0x7117u);
            cui__absolute(ui, tid, box.x - R.x, box.y - R.y, box.w, box.h);
            cui__tooltip_passive(ui, tid, tip);
        }
    }
    cui__close(ui);
#undef T_AT
#undef ROW_AT

    if (*tp != t_before) res.time_changed = true;
    if (sel_any) res.selection_changed = true;
    res.view_changed = v->start != b0 || v->end != b1 || v->scroll != bs;
    st->last_time = *tp;
    *ctp = ct; *ckp = ck;
    res.current_track = ct; res.current_key = ck;
    ui->last_id = id;
    return res;
}
