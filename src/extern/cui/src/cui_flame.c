/* Flame graph / profiler timeline. Time runs left to right and call depth top
 * to bottom; the caller owns the spans, the view and the selection.
 *
 * Spans are sorted by depth, then start, and spans of one depth do not
 * overlap, so within a row both starts and ends ascend. Every lookup is a
 * binary search on that order: the rows in view, the first span in view per
 * row, the span under the pointer, a span's parent and children. Spans
 * thinner than a pixel are merged into neutral runs, and a run skips ahead a
 * pixel at a time with a binary search, so the work per frame follows the
 * pixels on screen, not the number of spans. */
#include "cui_internal.h"

#define CUI__FL_RULER 24.0f    /* time ruler along the top */
#define CUI__FL_ROW   20.0f    /* default row height (bars are a point shorter) */
#define CUI__FL_PADX  8.0f     /* time axis inset from the panel edge */
#define CUI__FL_PADY  4.0f     /* rows below the ruler, and below the last row */
#define CUI__FL_DRAG  3.0f     /* travel before a press becomes a pan */
#define CUI__FL_LABEL 40.0f    /* narrowest bar that gets a label (three letters and an ellipsis) */
#define CUI__FL_RAD   3.0f

enum { FL_IDLE = 0, FL_PRESS, FL_PAN };
enum { FL_SPAN = 0, FL_HOVER, FL_RUN };

typedef struct {
    int mode;
    float press_x, press_y, last_x, last_y;
    int press_hit; bool press_double;
    cui_spring anim; bool anim_on;
    double a0, a1, b0, b1;      /* animated zoom: from [a0, a1] to [b0, b1] */
    float focus;
    cui_flame_view own_view; int own_sel; bool init;
    /* geometry of the latest paint, for cui_flame_span_rect */
    bool valid;
    float plot_x, plot_w, body_y, row_h, scroll;
    cui_rect body;
    double t0, t1;
} cui__fl_state;

typedef struct { double s, e; int row; uint8_t kind; cui_color fill, fg; const char *label; int label_len; } cui__fl_item;

typedef struct {
    cui__fl_state *st;
    cui_id sid;
    cui__fl_item *items; int n, cap;
    bool has_sel; cui__fl_item sel;
    double t0, t1, origin;
    float row_h, scroll, content_h, focus;
    bool empty;
} cui__fl_paint;

/* ---- searches over the sorted spans ---- */

static int cui__fl_depth_lb(const cui_flame_span *s, int n, int d) {   /* first index with depth >= d */
    int lo = 0, hi = n;
    while (lo < hi) { int m = lo + (hi - lo) / 2; if (s[m].depth < d) lo = m + 1; else hi = m; }
    return lo;
}
static int cui__fl_end_gt(const cui_flame_span *s, int lo, int hi, double t) {   /* first in [lo, hi) with end > t */
    while (lo < hi) { int m = lo + (hi - lo) / 2; if (s[m].end <= t) lo = m + 1; else hi = m; }
    return lo;
}
static int cui__fl_start_le(const cui_flame_span *s, int lo, int hi, double t) {   /* last in [lo, hi) with start <= t, or lo - 1 */
    int a = lo, b = hi;
    while (a < b) { int m = a + (b - a) / 2; if (s[m].start <= t) a = m + 1; else b = m; }
    return a - 1;
}
static void cui__fl_row(const cui_flame_span *s, int n, int d, int *lo, int *hi) {
    *lo = cui__fl_depth_lb(s, n, d);
    *hi = cui__fl_depth_lb(s, n, d + 1);
}

static int cui__fl_parent(const cui_flame_span *s, int n, int i) {
    if (i < 0 || i >= n || s[i].depth <= 0) return -1;
    int lo, hi;
    cui__fl_row(s, n, s[i].depth - 1, &lo, &hi);
    int k = cui__fl_start_le(s, lo, hi, s[i].start);
    return k >= lo && s[k].end >= s[i].end ? k : -1;
}
static int cui__fl_first_child(const cui_flame_span *s, int n, int i) {
    if (i < 0 || i >= n) return -1;
    int lo, hi;
    cui__fl_row(s, n, s[i].depth + 1, &lo, &hi);
    int k = cui__fl_end_gt(s, lo, hi, s[i].start);
    return k < hi && s[k].start >= s[i].start && s[k].start < s[i].end ? k : -1;
}

/* ---- colors: the series palette, hashed so a name keeps its color ---- */

static uint32_t cui__fl_key(const cui_flame_span *sp) {
    if (sp->color_key) return sp->color_key;
    return cui__hash_str(sp->name ? sp->name : "", 0xF1A3u);
}
static cui_color cui__fl_fill(cui_ctx *ui, uint32_t key, bool hover) {
    static const float light[3] = { 0.30f, 0.38f, 0.46f }, dark[3] = { 0.34f, 0.42f, 0.50f };
    cui_color tone = ui->theme.colors[CUI_TOKEN_SERIES_1 + key % 6];
    float k = (ui->mode == CUI_THEME_DARK ? dark : light)[(key / 6) % 3] + (hover ? 0.12f : 0.0f);
    return cui_mix(CUI_C(ui, SURFACE), tone, k);
}
static cui_color cui__fl_fg(cui_ctx *ui, uint32_t key, cui_color fill) {
    cui_color tone = ui->theme.colors[CUI_TOKEN_SERIES_1 + key % 6];
    return cui__legible(cui_mix(CUI_C(ui, FOREGROUND), tone, 0.3f), fill, 4.5f);
}

/* ---- number formatting ---- */

static const char *cui__fl_dur(cui_ctx *ui, double s) {
    const char *u = "s";
    double v = s;
    if (s < 1e-6) { v = s * 1e9; u = "ns"; }
    else if (s < 1e-3) { v = s * 1e6; u = "\xc2\xb5s"; }
    else if (s < 1) { v = s * 1e3; u = "ms"; }
    int dec = v >= 100 ? 0 : v >= 10 ? 1 : 2;
    return cui__frame_printf(ui, "%.*f %s", dec, v, u);
}

/* ruler step: 1, 2 or 5 times a power of ten, at least min_px apart */
static double cui__fl_nice(double raw) {
    if (!(raw > 0)) return 1;
    double p = pow(10.0, floor(log10(raw)));
    double m = raw / p;
    return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
}
static void cui__fl_unit(double step, double *unit, const char **name, int *dec) {
    if (step >= 0.1) { *unit = 1; *name = "s"; }
    else if (step >= 1e-5) { *unit = 1e-3; *name = "ms"; }
    else if (step >= 1e-8) { *unit = 1e-6; *name = "\xc2\xb5s"; }
    else { *unit = 1e-9; *name = "ns"; }
    double r = step / *unit;
    int d = (int)ceil(-log10(r) - 1e-9);
    *dec = CUI_CLAMP(d, 0, 3);
}

/* ---- painting ---- */

static void cui__fl_push(cui_ctx *ui, cui__fl_paint *p, const cui__fl_item *it) {
    if (p->n >= p->cap) {
        int nc = p->cap ? p->cap * 2 : 256;
        cui__fl_item *nv = cui__frame_alloc(ui, sizeof(cui__fl_item) * (size_t)nc);
        if (p->n) memcpy(nv, p->items, sizeof(cui__fl_item) * (size_t)p->n);
        p->items = nv; p->cap = nc;
    }
    p->items[p->n++] = *it;
}

static void cui__fl_bar(cui_ctx *ui, const cui__fl_paint *p, const cui__fl_item *it, float px0, float k, float body_y,
                        cui_rect body, bool selected) {
    float x0 = px0 + (float)((it->s - p->t0) * k), x1 = px0 + (float)((it->e - p->t0) * k);
    float y = body_y + it->row * p->row_h - p->scroll, h = p->row_h - 1;
    if (selected && x1 - x0 < 3) { float c = (x0 + x1) * 0.5f; x0 = c - 1.5f; x1 = c + 1.5f; }
    /* keep the bar within a few points of the view, so huge zooms stay in float range */
    float lim0 = body.x - 4, lim1 = body.x + body.w + 4;
    if (x0 < lim0) x0 = lim0;
    if (x1 > lim1) x1 = lim1;
    float w = x1 - x0;
    if (w >= 2) w -= 1;   /* a point between neighbours */
    if (w <= 0) return;
    cui_rect br = { x0, y, w, h };
    float rad = CUI_MIN(CUI__FL_RAD, CUI_MIN(w, h) * 0.5f);
    if (selected) {
        cui_draw_rect_border(ui, br, CUI_CORNERS(rad), it->fill, CUI_C(ui, ACCENT), 1.5f);
    } else {
        cui_draw_rect(ui, br, CUI_CORNERS(rad), it->fill);
    }
    if (it->label_len > 0 && it->kind != FL_RUN) {
        /* the label stays in the visible part of the bar */
        float vx0 = CUI_MAX(x0, body.x + 1), vx1 = CUI_MIN(x0 + w, body.x + body.w - 1);
        float avail = vx1 - vx0 - 12;
        if (avail >= CUI__FL_LABEL - 12) {
            float fs = cui__fsz(ui, ui->theme.text_xs);
            float fh = cui__font_line_height(ui, CUI_FONT_BODY, fs);
            cui__draw_text_ellipsis(ui, CUI_FONT_BODY, fs, vx0 + 6, y + (h - fh) * 0.5f, avail, it->fg, it->label, it->label_len, false);
        }
    }
}

static void cui__paint_flame(cui_ctx *ui, cui_node *n, void *data) {
    cui__fl_paint *p = data;
    cui__fl_state *st = p->st;
    cui_rect r = n->rect;
    /* the wheel goes to the graph while the pointer is over its visible part */
    {
        cui_rect clip = ui->dl.clip_stack[ui->dl.clip_depth - 1];
        float s = ui->scale;
        cui_rect cp = { clip.x / s, clip.y / s, clip.w / s, clip.h / s };
        cui_scroll_state *ss = cui_state(ui, p->sid, sizeof *ss);
        ss->view_w = r.w; ss->view_h = r.h;
        ss->content_w = r.w + 1e6f; ss->content_h = r.h + 1e6f;
        cui__paint_add_hit(ui, p->sid, cui__rect_intersect(r, cp), CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y);
    }
    float rad = ui->theme.radius_panel;
    float px0 = r.x + CUI__FL_PADX, pw = CUI_MAX(1.0f, r.w - 2 * CUI__FL_PADX);
    double range = p->t1 - p->t0;
    float k = range > 0 ? (float)(pw / range) : 0;
    float body_y = r.y + CUI__FL_RULER + 1 + CUI__FL_PADY;
    cui_rect body = { r.x + 1, r.y + CUI__FL_RULER + 1, CUI_MAX(0.0f, r.w - 2), CUI_MAX(0.0f, r.h - CUI__FL_RULER - 2) };
    float view_h = r.h - CUI__FL_RULER - 1 - 2 * CUI__FL_PADY;   /* rows area, as the build sees it */
    st->valid = !p->empty && k > 0;
    st->plot_x = px0; st->plot_w = pw; st->body_y = body_y; st->row_h = p->row_h; st->scroll = p->scroll;
    st->body = body; st->t0 = p->t0; st->t1 = p->t1;

    cui_push_clip(ui, cui__rect_inset(r, 1));
    /* ruler */
    cui_corners rc = { rad - 1, rad - 1, 0, 0 };
    cui_draw_rect(ui, (cui_rect){ r.x, r.y, r.w, CUI__FL_RULER }, rc, CUI_C(ui, SURFACE_MUTED));
    cui_draw_rect(ui, (cui_rect){ r.x, r.y + CUI__FL_RULER, r.w, 1 }, CUI_CORNERS(0), CUI_C(ui, BORDER));
    float fs = cui__fsz(ui, ui->theme.text_xs);
    if (k > 0) {
        double unit; const char *uname; int dec;
        float min_px = 72;
        double step = cui__fl_nice(range * min_px / pw);
        cui__fl_unit(step, &unit, &uname, &dec);
        /* wide labels ("12.42 ms") need more room: thin the ticks until they fit */
        for (int guard = 0; guard < 4; guard++) {
            const char *wl = cui__frame_printf(ui, "%.*f %s", dec, (p->t1 - p->origin) / unit, uname);
            float lw = cui__text_width(ui, CUI_FONT_BODY, fs, wl, -1, true);
            if ((float)(step * k) >= lw + 16) break;
            step = cui__fl_nice(step * 1.01 * 2);
            cui__fl_unit(step, &unit, &uname, &dec);
        }
        double first = ceil((p->t0 - p->origin) / step - 1e-9);
        double last = floor((p->t1 - p->origin) / step + 1e-9);
        float fh = cui__font_line_height(ui, CUI_FONT_BODY, fs);
        cui_color grid = cui_alpha(CUI_C(ui, BORDER_SUBTLE), ui->mode == CUI_THEME_DARK ? 0.8f : 1.0f);
        for (double q = first; q <= last && q - first < 400; q += 1) {
            double t = p->origin + q * step;
            float x = px0 + (float)((t - p->t0) * k);
            x = floorf(x);
            cui_draw_rect(ui, (cui_rect){ x, r.y + CUI__FL_RULER + 1, 1, r.h - CUI__FL_RULER - 2 }, CUI_CORNERS(0), grid);
            cui_draw_rect(ui, (cui_rect){ x, r.y + CUI__FL_RULER - 5, 1, 5 }, CUI_CORNERS(0), CUI_C(ui, BORDER_STRONG));
            double v = q * step / unit;
            if (fabs(v) < 1e-9) v = 0;
            const char *lbl = cui__frame_printf(ui, "%.*f %s", dec, v, uname);
            float lw = cui__text_width(ui, CUI_FONT_BODY, fs, lbl, -1, true);
            if (x + 4 + lw <= r.x + r.w - 6)
                cui__draw_text_line(ui, CUI_FONT_BODY, fs, x + 4, r.y + (CUI__FL_RULER - 4 - fh) * 0.5f, CUI_C(ui, TEXT_MUTED), lbl, -1, true);
        }
    }
    /* rows */
    cui_push_clip(ui, body);
    for (int i = 0; i < p->n; i++) cui__fl_bar(ui, p, &p->items[i], px0, k, body_y, body, false);
    if (p->has_sel) cui__fl_bar(ui, p, &p->sel, px0, k, body_y, body, true);
    if (p->empty) {
        const char *msg = "No spans";
        float lw = cui__text_width(ui, CUI_FONT_BODY, cui__fsz(ui, ui->theme.text_sm), msg, -1, false);
        float lh = cui__font_line_height(ui, CUI_FONT_BODY, cui__fsz(ui, ui->theme.text_sm));
        cui__draw_text_line(ui, CUI_FONT_BODY, cui__fsz(ui, ui->theme.text_sm), body.x + (body.w - lw) * 0.5f, body.y + (body.h - lh) * 0.5f,
                            CUI_C(ui, TEXT_MUTED), msg, -1, false);
    }
    /* a quiet scroll indicator when rows are hidden above or below */
    if (p->content_h > view_h + 0.5f && view_h > 8) {
        float th = CUI_MAX(16.0f, view_h * view_h / p->content_h);
        float ty = body.y + CUI__FL_PADY + (view_h - th) * (p->scroll / (p->content_h - view_h));
        cui_draw_rect(ui, (cui_rect){ body.x + body.w - 5, ty, 3, th }, CUI_CORNERS(1.5f), cui_alpha(CUI_C(ui, TEXT_MUTED), 0.4f));
    }
    cui_pop_clip(ui);
    if (p->focus > 0.01f)
        cui_draw_rect_border(ui, cui__rect_inset(r, 1), CUI_CORNERS(rad - 1), cui__rgba(0, 0, 0, 0), cui_alpha(CUI_C(ui, FOCUS_RING), p->focus), 2);
    cui_pop_clip(ui);
}

/* ---- the component ---- */

typedef struct {
    const cui_flame_span *s; int n;
    double dmin, dmax;
    int rows;                   /* deepest depth + 1 */
    float plot_x, plot_w, body_y, row_h, body_h;
} cui__fl_ctx;

static int cui__fl_hit(const cui__fl_ctx *c, const cui_flame_view *v, float mx, float my) {
    if (c->n == 0 || c->plot_w <= 0) return -1;
    float yy = my - c->body_y + v->scroll;
    if (yy < 0 || mx < c->plot_x - 1 || mx > c->plot_x + c->plot_w + 1) return -1;
    int d = (int)floorf(yy / c->row_h);
    if (d >= c->rows || yy - d * c->row_h > c->row_h - 1) return -1;   /* the gap between rows */
    int lo, hi;
    cui__fl_row(c->s, c->n, d, &lo, &hi);
    if (lo >= hi) return -1;
    double range = v->end - v->start;
    double t = v->start + (double)(mx - c->plot_x) / c->plot_w * range;
    double tol = range / c->plot_w;   /* a point either side, so a thin span can be found */
    int k = cui__fl_start_le(c->s, lo, hi, t);
    if (k >= lo && c->s[k].end >= t) return k;
    int best = -1;
    double bd = tol;
    if (k >= lo && t - c->s[k].end <= bd) { bd = t - c->s[k].end; best = k; }
    if (k + 1 < hi && c->s[k + 1].start - t <= bd) best = k + 1;
    return best;
}

static void cui__fl_clamp(const cui__fl_ctx *c, cui_flame_view *v, double min_range) {
    double ext = c->dmax - c->dmin;
    double range = v->end - v->start;
    if (!(range > 0)) range = ext;
    range = CUI_MAX(range, min_range);
    if (range > ext) range = ext;
    double s = v->start;
    if (s < c->dmin) s = c->dmin;
    if (s + range > c->dmax) s = c->dmax - range;
    v->start = s; v->end = s + range;
}

static void cui__fl_clamp_scroll(const cui__fl_ctx *c, cui_flame_view *v) {
    float max_s = CUI_MAX(0.0f, c->rows * c->row_h - c->body_h);
    if (!(v->scroll >= 0)) v->scroll = 0;   /* also NaN */
    if (v->scroll > max_s) v->scroll = max_s;
}

static void cui__fl_animate_to(cui__fl_state *st, const cui_flame_view *v, double s, double e) {
    st->a0 = v->start; st->a1 = v->end; st->b0 = s; st->b1 = e;
    st->anim.x = 0; st->anim.v = 0;
    st->anim_on = true;
}

/* Interpolates between two views around the point that stays put on screen,
 * with the range changing geometrically, so a zoom reads as one motion. */
static void cui__fl_lerp(const cui__fl_state *st, double p, double *s, double *e) {
    double r0 = st->a1 - st->a0, r1 = st->b1 - st->b0;
    if (!(r0 > 0) || !(r1 > 0) || fabs(r1 - r0) < 1e-6 * r0) {
        *s = st->a0 + (st->b0 - st->a0) * p;
        *e = st->a1 + (st->b1 - st->a1) * p;
        return;
    }
    double fx = (st->a0 * r1 - st->b0 * r0) / (r1 - r0);   /* same screen fraction in both views */
    double u = (fx - st->a0) / r0;
    double r = exp(log(r0) + (log(r1) - log(r0)) * p);
    *s = fx - u * r;
    *e = *s + r;
}

static void cui__fl_zoom_span(const cui__fl_ctx *c, cui__fl_state *st, cui_flame_view *v, int i, double min_range) {
    const cui_flame_span *sp = &c->s[i];
    double d = CUI_MAX(sp->end - sp->start, min_range);
    cui_flame_view t = { sp->start - d * 0.05, sp->start + d * 1.05, v->scroll };
    cui__fl_clamp(c, &t, min_range);
    cui__fl_animate_to(st, v, t.start, t.end);
}

static void cui__fl_reveal(const cui__fl_ctx *c, cui_flame_view *v, int i, double min_range) {
    const cui_flame_span *sp = &c->s[i];
    double range = v->end - v->start;
    if (sp->end < v->start || sp->start > v->end) {
        double mid = (sp->start + sp->end) * 0.5;
        v->start = mid - range * 0.5; v->end = mid + range * 0.5;
        cui__fl_clamp(c, v, min_range);
    }
    float top = sp->depth * c->row_h;
    if (top < v->scroll) v->scroll = top;
    if (top + c->row_h > v->scroll + c->body_h) v->scroll = top + c->row_h - c->body_h;
    cui__fl_clamp_scroll(c, v);
}

cui_flame_result cui_flame_graph_ex(cui_ctx *ui, const char *id_str, const cui_flame_span *spans, int count,
                                    cui_flame_view *view, int *selected, const cui_flame_opts *o) {
    cui_flame_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_flame_result res;
    res.selection_changed = res.view_changed = false;
    res.hovered = res.zoomed = -1;
    cui_id id = cui_make_id(ui, id_str ? id_str : "flame");
    cui_id sid = id ^ 0xF1A5C011u;
    cui__fl_state *st = cui_state(ui, id ^ 0xF1A3u, sizeof *st);
    if (!st->init) { st->own_sel = -1; st->init = true; }
    cui_flame_view *v = view ? view : &st->own_view;
    int *sel = selected ? selected : &st->own_sel;
    if (!spans || count < 0) count = 0;
    int sel_before = *sel;
    if (*sel < -1 || *sel >= count) *sel = -1;
    double min_range = o->min_range > 0 ? o->min_range : 1e-8;

    /* extent of the data, a row at a time */
    cui__fl_ctx c;
    memset(&c, 0, sizeof c);
    c.s = spans; c.n = count;
    c.row_h = o->row_height > 0 ? o->row_height : CUI__FL_ROW;
    c.dmin = 0; c.dmax = 0;
    bool any = false;
    for (int lo = cui__fl_depth_lb(spans, count, 0); lo < count;) {
        int dd = spans[lo].depth;
        int hi = cui__fl_depth_lb(spans, count, dd + 1);
        if (hi <= lo) break;   /* unsorted input: stop rather than loop */
        double a = spans[lo].start, b = spans[hi - 1].end;
        if (!any || a < c.dmin) c.dmin = a;
        if (!any || b > c.dmax) c.dmax = b;
        any = true;
        c.rows = dd + 1;
        lo = hi;
    }
    if (c.rows > 100000) c.rows = 100000;
    bool has_data = any && c.dmax >= c.dmin && isfinite(c.dmin) && isfinite(c.dmax);
    if (has_data && c.dmax - c.dmin < min_range) { double m = (c.dmin + c.dmax) * 0.5; c.dmin = m - min_range * 0.5; c.dmax = m + min_range * 0.5; }

    bool has_rect = cui__has_prev_rect(ui, id);
    cui_rect R = cui__prev_rect(ui, id);
    c.plot_x = R.x + CUI__FL_PADX;
    c.plot_w = has_rect ? CUI_MAX(1.0f, R.w - 2 * CUI__FL_PADX) : 600;
    c.body_y = R.y + CUI__FL_RULER + 1 + CUI__FL_PADY;
    c.body_h = has_rect ? CUI_MAX(1.0f, R.h - CUI__FL_RULER - 1 - 2 * CUI__FL_PADY) : 200;

    double b0 = v->start, b1 = v->end;
    float bs = v->scroll;
    if (has_data && (!(v->end > v->start) || !isfinite(v->start) || !isfinite(v->end))) {
        v->start = c.dmin; v->end = c.dmax;
        st->anim_on = false;
    }

    cui_interact bi = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    float mx = ui->mx, my = ui->my;
    bool user_moved = false;   /* input that should stop an animation */

    if (has_data && has_rect) {
        double range = v->end - v->start;
        /* wheel: zoom around the pointer; Shift or a sideways scroll pans */
        if (ui->scroll_target == sid && ui->hot_layer == ui->current_layer && (ui->scroll_dx != 0 || ui->scroll_dy != 0)) {
            bool pan = ui->scroll_dx != 0 || (ui->mods & CUI_MOD_SHIFT);
            if (pan) {
                float dl = ui->scroll_dx != 0 ? ui->scroll_dx : ui->scroll_dy;
                double dt = (double)dl * 40.0 / c.plot_w * range;
                v->start -= dt; v->end -= dt;
            } else {
                double f = pow(1.12, (double)ui->scroll_dy);
                double nr = CUI_MAX(range / f, min_range);
                nr = CUI_MIN(nr, c.dmax - c.dmin);
                double u = (double)(mx - c.plot_x) / c.plot_w;
                double tm = v->start + u * range;
                v->start = tm - u * nr; v->end = v->start + nr;
            }
            ui->scroll_dx = ui->scroll_dy = 0;
            user_moved = true;
        }
        /* pointer */
        if (bi.hovered && ui->mpressed[0] && st->mode == FL_IDLE) {
            st->mode = FL_PRESS;
            st->press_x = st->last_x = mx; st->press_y = st->last_y = my;
            st->press_hit = cui__fl_hit(&c, v, mx, my);
            st->press_double = bi.double_clicked;
        }
        if (st->mode == FL_PRESS && ui->mdown[0]) {
            float dx = mx - st->press_x, dy = my - st->press_y;
            if (dx * dx + dy * dy > CUI__FL_DRAG * CUI__FL_DRAG) st->mode = FL_PAN;
        }
        if (st->mode == FL_PAN) {
            double dt = (double)(mx - st->last_x) / c.plot_w * (v->end - v->start);
            v->start -= dt; v->end -= dt;
            v->scroll -= my - st->last_y;
            st->last_x = mx; st->last_y = my;
            cui__set_cursor(ui, CUI_CURSOR_MOVE);
            user_moved = true;
            if (!ui->mdown[0]) st->mode = FL_IDLE;
        } else if (st->mode == FL_PRESS && !ui->mdown[0]) {
            st->mode = FL_IDLE;
            int h = st->press_hit;
            bool in_rows = st->press_y >= c.body_y - CUI__FL_PADY;   /* a click on the ruler keeps the selection */
            if (in_rows && h != *sel) *sel = h;
            if (st->press_double) {
                if (h >= 0) { cui__fl_zoom_span(&c, st, v, h, min_range); res.zoomed = h; }
                else cui__fl_animate_to(st, v, c.dmin, c.dmax);
            }
        }
        if (st->mode != FL_IDLE && !ui->mdown[0]) st->mode = FL_IDLE;
        /* keyboard */
        if (bi.focused && !ui->edit_prev) {
            bool shift = (ui->mods & CUI_MOD_SHIFT) != 0;
            int s = *sel, ns = s;
            if (shift && (cui_key_pressed(ui, CUI_KEY_LEFT) || cui_key_pressed(ui, CUI_KEY_RIGHT))) {
                double dt = (cui_key_pressed(ui, CUI_KEY_LEFT) ? -0.1 : 0.1) * range;
                v->start += dt; v->end += dt;
                user_moved = true;
            } else if (cui_key_pressed(ui, CUI_KEY_LEFT) || cui_key_pressed(ui, CUI_KEY_RIGHT) ||
                       cui_key_pressed(ui, CUI_KEY_UP) || cui_key_pressed(ui, CUI_KEY_DOWN)) {
                if (s < 0) {
                    /* nothing selected: start from the first span in view on the top row */
                    int lo, hi;
                    cui__fl_row(spans, count, spans[cui__fl_depth_lb(spans, count, 0)].depth, &lo, &hi);
                    int k = cui__fl_end_gt(spans, lo, hi, v->start);
                    ns = k < hi ? k : (lo < hi ? lo : -1);
                } else if (cui_key_pressed(ui, CUI_KEY_LEFT)) {
                    if (s > 0 && spans[s - 1].depth == spans[s].depth) ns = s - 1;
                } else if (cui_key_pressed(ui, CUI_KEY_RIGHT)) {
                    if (s + 1 < count && spans[s + 1].depth == spans[s].depth) ns = s + 1;
                } else if (cui_key_pressed(ui, CUI_KEY_UP)) {
                    int pk = cui__fl_parent(spans, count, s);
                    if (pk >= 0) ns = pk;
                } else {
                    int ck = cui__fl_first_child(spans, count, s);
                    if (ck >= 0) ns = ck;
                }
                if (ns >= 0 && ns != s) { *sel = ns; cui__fl_reveal(&c, v, ns, min_range); user_moved = true; }
            }
            ui->kpressed[CUI_KEY_LEFT] = ui->kpressed[CUI_KEY_RIGHT] = ui->kpressed[CUI_KEY_UP] = ui->kpressed[CUI_KEY_DOWN] = 0;
            double cx = (v->start + v->end) * 0.5;
            if (cui_key_pressed(ui, CUI_KEY_EQUAL) || cui_key_pressed(ui, CUI_KEY_MINUS)) {
                double f = cui_key_pressed(ui, CUI_KEY_EQUAL) ? 1 / 1.25 : 1.25;
                double nr = CUI_MIN(CUI_MAX((v->end - v->start) * f, min_range), c.dmax - c.dmin);
                v->start = cx - nr * 0.5; v->end = cx + nr * 0.5;
                user_moved = true;
                ui->kpressed[CUI_KEY_EQUAL] = ui->kpressed[CUI_KEY_MINUS] = 0;
            }
            if ((cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE)) && *sel >= 0) {
                cui__fl_zoom_span(&c, st, v, *sel, min_range);
                res.zoomed = *sel;
                ui->kpressed[CUI_KEY_ENTER] = ui->kpressed[CUI_KEY_SPACE] = 0;
            }
            bool fitted = fabs(v->start - c.dmin) <= 1e-9 * (c.dmax - c.dmin) + 1e-15 &&
                          fabs(v->end - c.dmax) <= 1e-9 * (c.dmax - c.dmin) + 1e-15;
            if (cui_key_pressed(ui, CUI_KEY_HOME) || (cui_key_pressed(ui, CUI_KEY_F) && !(ui->mods & CUI_MOD_PRIMARY))) {
                cui__fl_animate_to(st, v, c.dmin, c.dmax);
                ui->kpressed[CUI_KEY_HOME] = ui->kpressed[CUI_KEY_F] = 0;
            }
            if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
                if (fitted && !st->anim_on) *sel = -1;
                else cui__fl_animate_to(st, v, c.dmin, c.dmax);
                ui->kpressed[CUI_KEY_ESCAPE] = 0;
            }
        }
        if (user_moved) st->anim_on = false;
        if (st->anim_on) {
            float pr = cui__spring(ui, &st->anim, 1.0f, CUI__SPRING_EXPAND);
            double s0, s1;
            cui__fl_lerp(st, pr >= 1.0f ? 1.0 : (double)pr, &s0, &s1);
            v->start = s0; v->end = s1;
            if (pr >= 1.0f && st->anim.v == 0) { v->start = st->b0; v->end = st->b1; st->anim_on = false; }
        } else {
            cui__fl_clamp(&c, v, min_range);
        }
        cui__fl_clamp_scroll(&c, v);
    } else if (!has_data) {
        st->mode = FL_IDLE;
        st->anim_on = false;
        if (*sel != -1) *sel = -1;
    }

    /* hover */
    int hov = -1;
    if (has_data && has_rect && bi.hovered && st->mode != FL_PAN && cui__rect_contains(R, mx, my)) hov = cui__fl_hit(&c, v, mx, my);
    res.hovered = hov;
    res.selection_changed = *sel != sel_before;
    res.view_changed = v->start != b0 || v->end != b1 || v->scroll != bs;

    /* the panel */
    int en = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[en];
    n->w = o->width.kind == CUI_SIZE_FIT && o->width.value == 0 ? CUI_FILL : o->width;
    float def_h = CUI__FL_RULER + 1 + 2 * CUI__FL_PADY + CUI_MAX(3, c.rows) * c.row_h;
    n->h = CUI_PX(o->height > 0 ? o->height : CUI_MIN(360.0f, def_h));
    n->bg = CUI_C(ui, SURFACE);
    n->border = 1;
    n->border_color = CUI_C(ui, BORDER);
    n->radius = CUI_CORNERS(ui->theme.radius_panel);
    n->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, en, CUI_IF_FOCUSABLE);
    cui__fl_paint *p = cui__frame_alloc(ui, sizeof *p);
    memset(p, 0, sizeof *p);
    p->st = st; p->sid = sid;
    p->t0 = v->start; p->t1 = v->end; p->origin = c.dmin;
    p->row_h = c.row_h; p->scroll = v->scroll; p->content_h = c.rows * c.row_h;
    p->empty = !has_data;
    cui__tween(ui, &st->focus, bi.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    p->focus = st->focus;
    n->paint = cui__paint_flame;
    n->paint_data = p;

    if (has_data && v->end > v->start) {
        /* the spans in view, each row from its first visible span */
        double t0 = v->start, t1 = v->end;
        double k = c.plot_w / (t1 - t0);
        int d0 = (int)floorf(v->scroll / c.row_h), d1 = (int)floorf((v->scroll + c.body_h) / c.row_h);
        d0 = CUI_MAX(0, d0); d1 = CUI_MIN(c.rows - 1, d1);
        cui_color run_fill = cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, TEXT_MUTED), ui->mode == CUI_THEME_DARK ? 0.32f : 0.26f);
        for (int row = d0; row <= d1; row++) {
            int lo, hi;
            cui__fl_row(spans, count, row, &lo, &hi);
            int i = cui__fl_end_gt(spans, lo, hi, t0);
            while (i < hi && spans[i].start < t1) {
                const cui_flame_span *sp = &spans[i];
                double xs = (sp->start - t0) * k, xe = (sp->end - t0) * k;
                cui__fl_item it;
                memset(&it, 0, sizeof it);
                it.row = row;
                if (xe - xs >= 1.0) {
                    uint32_t key = cui__fl_key(sp);
                    it.s = sp->start; it.e = sp->end;
                    it.kind = i == hov ? FL_HOVER : FL_SPAN;
                    it.fill = cui__fl_fill(ui, key, i == hov);
                    if (xe - xs >= CUI__FL_LABEL && sp->name && sp->name[0]) {
                        it.fg = cui__fl_fg(ui, key, it.fill);
                        it.label_len = (int)strlen(sp->name);
                        it.label = cui__frame_strdup(ui, sp->name, it.label_len);
                    }
                    cui__fl_push(ui, p, &it);
                    i++;
                    continue;
                }
                /* thinner than a point: merge with the neighbours within a point */
                double rx0 = xs, rx1 = xe;
                i++;
                for (;;) {
                    int j = cui__fl_end_gt(spans, i, hi, t0 + (rx1 + 1.0) / k);
                    if (j > i) { double e2 = (spans[j - 1].end - t0) * k; if (e2 > rx1) rx1 = e2; i = j; }
                    if (i < hi) {
                        double ns = (spans[i].start - t0) * k, ne = (spans[i].end - t0) * k;
                        if (ne - ns < 1.0 && ns <= rx1 + 1.0) { rx1 = ne; i++; continue; }
                    }
                    break;
                }
                if (rx1 - rx0 >= 1.0) {
                    it.s = t0 + rx0 / k; it.e = t0 + rx1 / k;
                    it.kind = FL_RUN;
                    it.fill = run_fill;
                    cui__fl_push(ui, p, &it);
                }
            }
        }
        /* the selection, on top and at least a few points wide */
        int s = *sel;
        if (s >= 0 && spans[s].depth >= d0 && spans[s].depth <= d1 && spans[s].end >= t0 && spans[s].start <= t1) {
            const cui_flame_span *sp = &spans[s];
            uint32_t key = cui__fl_key(sp);
            cui__fl_item it;
            memset(&it, 0, sizeof it);
            it.s = sp->start; it.e = sp->end; it.row = sp->depth;
            it.kind = s == hov ? FL_HOVER : FL_SPAN;
            it.fill = cui__fl_fill(ui, key, s == hov);
            if ((sp->end - sp->start) * k >= CUI__FL_LABEL && sp->name && sp->name[0]) {
                it.fg = cui__fl_fg(ui, key, it.fill);
                it.label_len = (int)strlen(sp->name);
                it.label = cui__frame_strdup(ui, sp->name, it.label_len);
            }
            p->sel = it; p->has_sel = true;
        }
    }

    /* the tooltip anchors on a small box at the pointer, on the hovered row */
    cui__open(ui, en);
    if (hov >= 0 && ui->active == 0 && !ui->mdown[0]) {
        const cui_flame_span *sp = &spans[hov];
        cui_id tid = cui__hash(&hov, sizeof hov, id ^ 0x7111u);
        float ry = c.body_y + sp->depth * c.row_h - v->scroll;
        cui__absolute(ui, tid, mx - R.x - 1, ry - R.y, 2, c.row_h - 1);
        double dur = sp->end - sp->start;
        int pk = cui__fl_parent(spans, count, hov);
        double base = pk >= 0 ? spans[pk].end - spans[pk].start : v->end - v->start;
        double pct = base > 0 ? dur / base * 100.0 : 0;
        const char *of = pk >= 0 ? (spans[pk].name && spans[pk].name[0] ? spans[pk].name : "parent") : "view";
        const char *tip = cui__frame_printf(ui, "%s\n%s \xc2\xb7 %.*f%% of %s", sp->name && sp->name[0] ? sp->name : "Unnamed span",
                                            cui__fl_dur(ui, dur), pct >= 10 ? 0 : 1, pct, of);
        cui__tooltip_passive(ui, tid, tip);
    }
    cui__close(ui);
    ui->last_id = id;
    return res;
}

static int cui__fl_cmp(const void *a, const void *b) {
    const cui_flame_span *x = a, *y = b;
    if (x->depth != y->depth) return x->depth < y->depth ? -1 : 1;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    if (x->end != y->end) return x->end > y->end ? -1 : 1;   /* longer first */
    return 0;
}

void cui_flame_sort(cui_flame_span *spans, int count) {
    if (spans && count > 1) qsort(spans, (size_t)count, sizeof *spans, cui__fl_cmp);
}

cui_rect cui_flame_span_rect(cui_ctx *ui, cui_id flame, const cui_flame_span *span) {
    cui_rect none = { 0, 0, 0, 0 };
    if (!span || !cui__has_prev_rect(ui, flame)) return none;
    cui__fl_state *st = cui_state(ui, flame ^ 0xF1A3u, sizeof *st);
    if (!st->valid || !(st->t1 > st->t0)) return none;
    double k = st->plot_w / (st->t1 - st->t0);
    double x0 = st->plot_x + (span->start - st->t0) * k, x1 = st->plot_x + (span->end - st->t0) * k;
    float y = st->body_y + span->depth * st->row_h - st->scroll;
    cui_rect b = st->body;
    if (x1 < b.x || x0 > b.x + b.w || y + st->row_h - 1 < b.y || y > b.y + b.h || span->depth < 0) return none;
    cui_rect r = { (float)x0, y, (float)(x1 - x0), st->row_h - 1 };
    return r;
}
