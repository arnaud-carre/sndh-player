/* Curve and gradient editors. The curve editor shows keyframes on a plot
 * (easing, timing and response curves); the gradient editor a color ramp
 * with its stops underneath. Both edit the caller's array in place and
 * report what they did. cui_curve_eval and cui_gradient_eval are the math
 * the editors draw with, so an app evaluates exactly what it shows.
 *
 * The keys, stops and handles are small absolutely placed nodes for hit
 * testing and focus (placed from the last frame's rectangles, like every
 * hit in CUI); everything visible is painted by the plot and the ramp
 * themselves from the current rectangle. */
#include "cui_internal.h"

static cui_color cui__cv_accent(cui_ctx *ui) { return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT); }

/* ======================================================================== */
/* Curve math                                                               */
/* ======================================================================== */

static bool cui__cv_bezier_mode(int mode) { return mode != CUI_CURVE_LINEAR && mode != CUI_CURVE_STEP; }

/* Automatic tangent of key i: weighted Catmull-Rom, limited so the curve
 * never overshoots its keys (monotone between them), flat at the ends and at
 * peaks and valleys. */
static float cui__cv_auto_slope(const cui_curve_key *k, int n, int i) {
    if (i <= 0 || i >= n - 1) return 0;
    float dx0 = k[i].x - k[i - 1].x, dx1 = k[i + 1].x - k[i].x;
    if (!(dx0 > 0) || !(dx1 > 0)) return 0;
    float d0 = (k[i].y - k[i - 1].y) / dx0, d1 = (k[i + 1].y - k[i].y) / dx1;
    if (!(d0 * d1 > 0)) return 0;
    float s = (k[i + 1].y - k[i - 1].y) / (dx0 + dx1);
    float lim = 3.0f * fminf(fabsf(d0), fabsf(d1));
    if (fabsf(s) > lim) s = copysignf(lim, s);
    return s;
}

/* A stored handle shortened to the segment width (keeping its direction),
 * so x stays monotone and the curve never folds back. dx is a forward length. */
static void cui__cv_fit(float dx, float dy, float len, float *ox, float *oy) {
    if (!(dx > 0)) dx = 0;
    if (dx > len) { dy *= len / dx; dx = len; }
    *ox = dx;
    *oy = dy;
}

/* The two inner control points of the bezier segment from key i to key i+1,
 * relative to their keys. */
static void cui__cv_seg_handles(const cui_curve_key *k, int n, int i, float *ox, float *oy, float *ix, float *iy) {
    const cui_curve_key *a = &k[i], *b = &k[i + 1];
    float len = b->x - a->x;
    if (a->mode == CUI_CURVE_FREE) cui__cv_fit(a->out_dx, a->out_dy, len, ox, oy);
    else { float s = cui__cv_auto_slope(k, n, i); *ox = len / 3.0f; *oy = s * len / 3.0f; }
    if (b->mode == CUI_CURVE_FREE) {
        float tx, ty;
        cui__cv_fit(-b->in_dx, b->in_dy, len, &tx, &ty);
        *ix = -tx; *iy = ty;
    } else { float s = cui__cv_auto_slope(k, n, i + 1); *ix = -len / 3.0f; *iy = -s * len / 3.0f; }
}

static float cui__cv_bez(float p0, float p1, float p2, float p3, float t) {
    float u = 1 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}
static float cui__cv_bez_d(float p0, float p1, float p2, float p3, float t) {
    float u = 1 - t;
    return 3 * (u * u * (p1 - p0) + 2 * u * t * (p2 - p1) + t * t * (p3 - p2));
}

/* Value of segment i (keys i and i+1) at x, with keys[i].x <= x < keys[i+1].x. */
static float cui__cv_seg_eval(const cui_curve_key *k, int n, int i, float x) {
    const cui_curve_key *a = &k[i], *b = &k[i + 1];
    float len = b->x - a->x;
    if (!(len > 0)) return b->y;
    float tx = x - a->x;
    if (a->mode == CUI_CURVE_STEP) return a->y;
    if (a->mode == CUI_CURVE_LINEAR) return a->y + (b->y - a->y) * (tx / len);
    float ox, oy, ix, iy;
    cui__cv_seg_handles(k, n, i, &ox, &oy, &ix, &iy);
    /* x(t) is monotone (the handles are fitted into the segment): Newton
     * steps inside a shrinking bracket, bisecting when a step leaves it */
    float x1 = ox, x2 = len + ix;
    float lo = 0, hi = 1, t = tx / len;
    for (int it = 0; it < 40; it++) {
        float e = cui__cv_bez(0, x1, x2, len, t) - tx;
        if (fabsf(e) <= len * 1e-6f) break;
        if (e > 0) hi = t; else lo = t;
        float d = cui__cv_bez_d(0, x1, x2, len, t);
        float nt = d > 1e-12f ? t - e / d : -1.0f;
        t = (nt > lo && nt < hi) ? nt : 0.5f * (lo + hi);
    }
    return cui__cv_bez(a->y, a->y + oy, b->y + iy, b->y, t);
}

float cui_curve_eval(const cui_curve_key *k, int n, float x) {
    if (!k || n <= 0) return 0;
    if (!(x > k[0].x)) return k[0].y;   /* also NaN */
    if (x >= k[n - 1].x) return k[n - 1].y;
    int i = 0;
    while (i < n - 2 && x >= k[i + 1].x) i++;
    return cui__cv_seg_eval(k, n, i, x);
}

/* ======================================================================== */
/* Curve editor                                                             */
/* ======================================================================== */

#define CUI__CV_PLOT_H     220.0f
#define CUI__CV_KEY_HIT    22.0f    /* keys are grabbable well beyond their dot */
#define CUI__CV_HANDLE_HIT 18.0f
#define CUI__CV_HANDLE_MIN 18.0f    /* a handle is never shown shorter than this */
#define CUI__CV_NEAR       10.0f    /* a double click this close to the curve adds the key on it */
#define CUI__CV_NARROW     440.0f   /* narrower than this, the detail row takes two lines */
#define CUI__CV_TINY       320.0f   /* and narrower than this, three */

typedef struct { float x0, x1, y0, y1; } cui__cv_range;

typedef struct {
    cui_rect plot;            /* the range, in points */
    float xstep, ystep;       /* labelled grid steps */
    int xdec, ydec;           /* decimals of the labels */
    float fs;                 /* label size */
} cui__cv_layout;

static int cui__cv_decimals(float step) {
    int d = 0;
    float m = step;
    while (d < 6 && fabsf(m - roundf(m)) > 1e-3f * fmaxf(1.0f, fabsf(m))) { m *= 10; d++; }
    return d;
}

/* decimals for a readout of a value in a range: about a hundredth of it */
static int cui__cv_readout_decimals(float range) {
    float d = ceilf(-log10f(range / 100.0f) - 1e-4f);
    return (int)CUI_CLAMP(d, 0.0f, 6.0f);
}

static const char *cui__cv_fmt(cui_ctx *ui, const char *fmt, int dec, float v) {
    if (fabsf(v) < 0.5f * powf(10.0f, (float)-dec)) v = 0;   /* no "-0.00" */
    if (fmt) return cui__frame_printf(ui, fmt, (double)v);
    return cui__frame_printf(ui, "%.*f", dec, (double)v);
}

/* the first multiple of step at or above lo, and how many fit up to hi */
static int cui__cv_ticks(float lo, float hi, float step, float *first) {
    *first = ceilf(lo / step - 1e-4f) * step;
    float n = floorf((hi - *first) / step + 1e-4f) + 1;
    if (!(n > 0)) return 0;   /* also NaN */
    return n > 64 ? 64 : (int)n;
}

static cui__cv_layout cui__cv_layout_of(cui_ctx *ui, cui_rect r, const cui__cv_range *g, const char *yfmt) {
    cui__cv_layout L;
    memset(&L, 0, sizeof L);
    L.fs = ui->theme.text_xs + 0.5f;
    float fh = cui__font_line_height(ui, CUI_FONT_BODY, L.fs);
    float top = 12, bottom = fh + 14, right = 14;
    float ph = r.h - top - bottom;
    float lo, hi;
    cui__nice_range(g->y0, g->y1, (int)CUI_CLAMP(ph / 44.0f, 2.0f, 8.0f), &lo, &hi, &L.ystep);
    L.ydec = cui__cv_decimals(L.ystep);
    float first, lw = 0;
    int ny = cui__cv_ticks(g->y0, g->y1, L.ystep, &first);
    for (int k = 0; k < ny; k++)
        lw = CUI_MAX(lw, cui__text_width(ui, CUI_FONT_BODY, L.fs, cui__cv_fmt(ui, yfmt, L.ydec, first + L.ystep * (float)k), -1, true));
    float left = 12 + lw + 8;
    float pw = r.w - left - right;
    cui__nice_range(g->x0, g->x1, (int)CUI_CLAMP(pw / 84.0f, 2.0f, 10.0f), &lo, &hi, &L.xstep);
    L.xdec = cui__cv_decimals(L.xstep);
    L.plot.x = r.x + left; L.plot.y = r.y + top;
    L.plot.w = CUI_MAX(1.0f, pw); L.plot.h = CUI_MAX(1.0f, ph);
    return L;
}

static float cui__cv_sx(const cui__cv_layout *L, const cui__cv_range *g, float x) { return L->plot.x + (x - g->x0) / (g->x1 - g->x0) * L->plot.w; }
static float cui__cv_sy(const cui__cv_layout *L, const cui__cv_range *g, float y) { return L->plot.y + L->plot.h - (y - g->y0) / (g->y1 - g->y0) * L->plot.h; }
static float cui__cv_ux(const cui__cv_layout *L, const cui__cv_range *g, float sx) { return g->x0 + (sx - L->plot.x) / L->plot.w * (g->x1 - g->x0); }
static float cui__cv_uy(const cui__cv_layout *L, const cui__cv_range *g, float sy) { return g->y0 + (L->plot.y + L->plot.h - sy) / L->plot.h * (g->y1 - g->y0); }

/* Where a handle of key i shows (side 0 in, 1 out), in points. False when
 * that side has no editable handle: the segment there is not a bezier, or
 * the key computes its incoming tangent itself (LINEAR, STEP). */
static bool cui__cv_handle_pos(const cui_curve_key *k, int n, int i, int side, const cui__cv_layout *L, const cui__cv_range *g, cui_vec2 *out) {
    if (i < 0 || i >= n || !cui__cv_bezier_mode(k[i].mode)) return false;
    float dx, dy, ox, oy, ix, iy;
    if (side == 0) {
        if (i == 0 || !cui__cv_bezier_mode(k[i - 1].mode) || !(k[i].x > k[i - 1].x)) return false;
        cui__cv_seg_handles(k, n, i - 1, &ox, &oy, &ix, &iy);
        dx = ix; dy = iy;
    } else {
        if (i >= n - 1 || !(k[i + 1].x > k[i].x)) return false;
        cui__cv_seg_handles(k, n, i, &ox, &oy, &ix, &iy);
        dx = ox; dy = oy;
    }
    float kx = cui__cv_sx(L, g, k[i].x), ky = cui__cv_sy(L, g, k[i].y);
    float hx = cui__cv_sx(L, g, k[i].x + dx), hy = cui__cv_sy(L, g, k[i].y + dy);
    float vx = hx - kx, vy = hy - ky, len = sqrtf(vx * vx + vy * vy);
    if (len < CUI__CV_HANDLE_MIN) {
        if (len > 0.01f) { vx *= CUI__CV_HANDLE_MIN / len; vy *= CUI__CV_HANDLE_MIN / len; }
        else { vx = side ? CUI__CV_HANDLE_MIN : -CUI__CV_HANDLE_MIN; vy = 0; }
        hx = kx + vx; hy = ky + vy;
    }
    out->x = hx; out->y = hy;
    return true;
}

/* A SMOOTH (or LINEAR, STEP) key becomes FREE with the handles it showed. */
static void cui__cv_bake(cui_curve_key *k, int n, int i) {
    float s = cui__cv_auto_slope(k, n, i);
    float lin = i > 0 ? CUI_MAX(0.0f, k[i].x - k[i - 1].x) : 0;
    float lout = i < n - 1 ? CUI_MAX(0.0f, k[i + 1].x - k[i].x) : 0;
    k[i].in_dx = -lin / 3.0f; k[i].in_dy = -s * lin / 3.0f;
    k[i].out_dx = lout / 3.0f; k[i].out_dy = s * lout / 3.0f;
    k[i].mode = CUI_CURVE_FREE;
}

/* Moves key i toward (x, y), x kept between its neighbours and both inside
 * the range. Returns true when it moved. */
static bool cui__cv_place(cui_curve_key *k, int n, int i, const cui__cv_range *g, float x, float y) {
    float lo = i > 0 ? CUI_MAX(k[i - 1].x, g->x0) : g->x0;
    float hi = i < n - 1 ? CUI_MIN(k[i + 1].x, g->x1) : g->x1;
    float nx = lo <= hi ? cui__clampf(x, lo, hi) : k[i].x;
    float ny = cui__clampf(y, g->y0, g->y1);
    if (!(nx == nx)) nx = k[i].x;
    if (!(ny == ny)) ny = k[i].y;
    if (nx == k[i].x && ny == k[i].y) return false;
    k[i].x = nx; k[i].y = ny;
    return true;
}

static float cui__cv_snap(float v, float step) { return step > 0 ? roundf(v / step) * step : v; }

typedef struct { int sel; float grab_x, grab_y; bool init; } cui__cv_state;

typedef struct {
    cui_curve_key *keys; int n;
    cui__cv_range g;
    const char *yfmt, *xfmt;
    int sel, focus_key, hot_key, active_key, hot_handle, active_handle;
    bool hover; float hx, hy;
    bool playhead; float px;
    float *key_hover;         /* per key, 0..1 */
    cui_color curve;
} cui__cv_paint;

static void cui__cv_key_dot(cui_ctx *ui, cui_vec2 c, bool selected, float hover, cui_color curve, cui_color surface) {
    if (selected) {
        cui_draw_circle(ui, c, 6.5f, surface);
        cui_draw_circle(ui, c, 5.0f, curve);
    } else {
        cui_draw_circle(ui, c, 4.75f, curve);
        cui_draw_circle(ui, c, 3.0f, cui_mix(surface, curve, 0.35f * hover));
    }
}

static void cui__paint_curve(cui_ctx *ui, cui_node *nd, void *data) {
    cui__cv_paint *p = data;
    cui_rect r = nd->rect;
    const cui__cv_range *g = &p->g;
    cui__cv_layout L = cui__cv_layout_of(ui, r, g, p->yfmt);
    cui_rect P = L.plot;
    if (P.w < 8 || P.h < 8) return;
    float s = ui->scale;
    float fh = cui__font_line_height(ui, CUI_FONT_BODY, L.fs);
    cui_color sub = CUI_C(ui, BORDER_SUBTLE), edge = CUI_C(ui, BORDER), muted = CUI_C(ui, TEXT_MUTED);
    cui_color surface = CUI_C(ui, SURFACE);
    float first;
    /* grid: half steps when there is room, labelled steps, the range's edges */
    for (int axis = 0; axis < 2; axis++) {
        float step = axis ? L.ystep : L.xstep;
        float lo = axis ? g->y0 : g->x0, hi = axis ? g->y1 : g->x1;
        float span = axis ? P.h : P.w;
        bool minor = (step * 0.5f) / (hi - lo) * span >= 18.0f;
        float st = minor ? step * 0.5f : step;
        int cnt = cui__cv_ticks(lo, hi, st, &first);
        for (int k = 0; k < cnt; k++) {
            float v = first + st * (float)k;
            bool major = !minor || fabsf(v / step - roundf(v / step)) < 1e-3f;
            cui_color c = major ? sub : cui_alpha(sub, 0.55f);
            if (axis == 0) {
                float x = roundf(cui__cv_sx(&L, g, v) * s) / s + 0.5f / s;
                cui_draw_rect(ui, (cui_rect){ x - 0.5f, P.y, 1, P.h }, CUI_CORNERS(0), c);
            } else {
                float y = roundf(cui__cv_sy(&L, g, v) * s) / s + 0.5f / s;
                cui_draw_rect(ui, (cui_rect){ P.x, y - 0.5f, P.w, 1 }, CUI_CORNERS(0), c);
            }
        }
    }
    cui_draw_rect_border(ui, (cui_rect){ P.x - 0.5f, P.y - 0.5f, P.w + 1, P.h + 1 }, CUI_CORNERS(0), cui__rgba(0, 0, 0, 0), edge, 1);
    /* labels: y on the left, x below; labels that would collide are left out */
    int cnt = cui__cv_ticks(g->y0, g->y1, L.ystep, &first);
    float last_y = 1e9f;
    for (int k = 0; k < cnt; k++) {
        float v = first + L.ystep * (float)k;
        float y = cui__cv_sy(&L, g, v);
        if (last_y - y < fh + 2 && k > 0) continue;
        const char *t = cui__cv_fmt(ui, p->yfmt, L.ydec, v);
        float tw = cui__text_width(ui, CUI_FONT_BODY, L.fs, t, -1, true);
        cui__draw_text_line(ui, CUI_FONT_BODY, L.fs, P.x - 8 - tw, y - fh * 0.5f, muted, t, -1, true);
        last_y = y;
    }
    cnt = cui__cv_ticks(g->x0, g->x1, L.xstep, &first);
    float last_end = -1e9f;
    for (int k = 0; k < cnt; k++) {
        float v = first + L.xstep * (float)k;
        const char *t = cui__cv_fmt(ui, p->xfmt, L.xdec, v);
        float tw = cui__text_width(ui, CUI_FONT_BODY, L.fs, t, -1, true);
        float x = cui__cv_sx(&L, g, v) - tw * 0.5f;
        x = cui__clampf(x, r.x + 4, r.x + r.w - 4 - tw);
        if (x < last_end + 10) continue;
        cui__draw_text_line(ui, CUI_FONT_BODY, L.fs, x, P.y + P.h + 6, muted, t, -1, true);
        last_end = x + tw;
    }
    /* the curve may overshoot the range a little; it never runs over the labels */
    cui_push_clip(ui, (cui_rect){ r.x + 1, r.y + 1, r.w - 2, P.y + P.h + 6 - r.y });
    const cui_curve_key *k = p->keys;
    int n = p->n;
    cui_color curve = p->curve;
    if (n > 0) {
        /* before the first and after the last key the curve holds the end values */
        float ky0 = cui__cv_sy(&L, g, k[0].y), kx0 = cui__cv_sx(&L, g, k[0].x);
        float kyn = cui__cv_sy(&L, g, k[n - 1].y), kxn = cui__cv_sx(&L, g, k[n - 1].x);
        if (kx0 > P.x + 0.5f) cui_draw_line(ui, (cui_vec2){ P.x, ky0 }, (cui_vec2){ kx0, ky0 }, 1.5f, cui_alpha(curve, 0.4f));
        if (kxn < P.x + P.w - 0.5f) cui_draw_line(ui, (cui_vec2){ kxn, kyn }, (cui_vec2){ P.x + P.w, kyn }, 1.5f, cui_alpha(curve, 0.4f));
    }
    if (n > 1) {
        /* the segments, from the same math as cui_curve_eval: beziers are
         * sampled along t (points exactly on the curve), steps jump at the key */
        int cap = n * 66 + 2, m = 0;
        cui_vec2 *pts = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)cap);
        pts[m++] = (cui_vec2){ cui__cv_sx(&L, g, k[0].x), cui__cv_sy(&L, g, k[0].y) };
        for (int i = 0; i < n - 1; i++) {
            const cui_curve_key *a = &k[i], *b = &k[i + 1];
            float ax = cui__cv_sx(&L, g, a->x), ay = cui__cv_sy(&L, g, a->y);
            float bx = cui__cv_sx(&L, g, b->x), by = cui__cv_sy(&L, g, b->y);
            if (!(b->x > a->x)) { pts[m++] = (cui_vec2){ bx, by }; continue; }
            if (a->mode == CUI_CURVE_STEP) { pts[m++] = (cui_vec2){ bx, ay }; pts[m++] = (cui_vec2){ bx, by }; continue; }
            if (a->mode == CUI_CURVE_LINEAR) { pts[m++] = (cui_vec2){ bx, by }; continue; }
            float ox, oy, ix, iy;
            cui__cv_seg_handles(k, n, i, &ox, &oy, &ix, &iy);
            float c1x = cui__cv_sx(&L, g, a->x + ox), c1y = cui__cv_sy(&L, g, a->y + oy);
            float c2x = cui__cv_sx(&L, g, b->x + ix), c2y = cui__cv_sy(&L, g, b->y + iy);
            float est = sqrtf((c1x - ax) * (c1x - ax) + (c1y - ay) * (c1y - ay)) + sqrtf((c2x - c1x) * (c2x - c1x) + (c2y - c1y) * (c2y - c1y)) +
                        sqrtf((bx - c2x) * (bx - c2x) + (by - c2y) * (by - c2y));
            int seg = (int)CUI_CLAMP(est / 3.0f, 8.0f, 64.0f);
            for (int j = 1; j <= seg; j++) {
                float t = (float)j / (float)seg;
                pts[m++] = (cui_vec2){ cui__cv_bez(ax, c1x, c2x, bx, t), cui__cv_bez(ay, c1y, c2y, by, t) };
            }
        }
        cui_draw_polyline(ui, pts, m, 2.0f, curve, false);
    }
    /* the playhead and the hovered value */
    if (p->playhead && p->px >= g->x0 && p->px <= g->x1) {
        float x = roundf(cui__cv_sx(&L, g, p->px) * s) / s;
        cui_draw_rect(ui, (cui_rect){ x - 0.75f, P.y, 1.5f, P.h }, CUI_CORNERS(0.75f), cui_alpha(CUI_C(ui, FOREGROUND), 0.55f));
        if (n > 0) {
            cui_vec2 c = { x, cui__cv_sy(&L, g, cui_curve_eval(k, n, p->px)) };
            cui_draw_circle(ui, c, 5.0f, surface);
            cui_draw_circle(ui, c, 3.5f, CUI_C(ui, FOREGROUND));
        }
    }
    if (p->hover && n > 0) {
        float x = roundf(cui__cv_sx(&L, g, p->hx) * s) / s + 0.5f / s;
        cui_draw_rect(ui, (cui_rect){ x - 0.5f, P.y, 1, P.h }, CUI_CORNERS(0), CUI_C(ui, BORDER));
        cui_vec2 c = { cui__cv_sx(&L, g, p->hx), cui__cv_sy(&L, g, p->hy) };
        cui_draw_circle(ui, c, 4.5f, surface);
        cui_draw_circle(ui, c, 3.0f, curve);
    }
    /* the selected key's handles */
    if (p->sel >= 0 && p->sel < n) {
        cui_vec2 kc = { cui__cv_sx(&L, g, k[p->sel].x), cui__cv_sy(&L, g, k[p->sel].y) };
        for (int side = 0; side < 2; side++) {
            cui_vec2 h;
            if (!cui__cv_handle_pos(k, n, p->sel, side, &L, g, &h)) continue;
            bool on = p->active_handle == side, hov = p->hot_handle == side;
            cui_draw_line(ui, kc, h, 1.0f, cui_alpha(CUI_C(ui, TEXT_MUTED), 0.8f));
            cui_draw_circle(ui, h, 4.75f, surface);
            cui_draw_circle(ui, h, 3.5f, on ? curve : hov ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
        }
    }
    /* keys, the selected one on top */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            bool sel = i == p->sel;
            if (sel != (pass == 1)) continue;
            cui_vec2 c = { cui__cv_sx(&L, g, k[i].x), cui__cv_sy(&L, g, k[i].y) };
            float hv = p->key_hover ? p->key_hover[i] : 0;
            cui__cv_key_dot(ui, c, sel, hv, curve, surface);
            if (i == p->focus_key)
                cui_draw_rect_border(ui, (cui_rect){ c.x - 9, c.y - 9, 18, 18 }, CUI_CORNERS(9), cui__rgba(0, 0, 0, 0), CUI_C(ui, FOCUS_RING), 2);
        }
    cui_pop_clip(ui);
}

static cui_id cui__cv_key_id(cui_id eid, int i) { return cui__hash(&i, sizeof i, eid ^ 0xC0E7u); }

static void cui__cv_remove(cui_curve_key *keys, int *n, int i) {
    if (i < 0 || i >= *n) return;
    memmove(&keys[i], &keys[i + 1], sizeof *keys * (size_t)(*n - i - 1));
    (*n)--;
}

cui_curve_result cui_curve_editor_ex(cui_ctx *ui, const char *id_str, cui_curve_key *keys, int *count, int capacity, const cui_curve_opts *o) {
    static const char *const modes[4] = { "Smooth", "Free", "Linear", "Step" };
    cui_curve_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_curve_result res;
    memset(&res, 0, sizeof res);
    res.added = res.removed = res.selected = -1;
    int n = (keys && count && *count > 0) ? *count : 0;
    if (capacity < n) capacity = n;
    cui__cv_range g = { o->x_min, o->x_max, o->y_min, o->y_max };
    if (!isfinite(g.x0) || !isfinite(g.x1)) g.x0 = g.x1 = 0;
    if (!isfinite(g.y0) || !isfinite(g.y1)) g.y0 = g.y1 = 0;
    if (!(g.x1 > g.x0)) { if (g.x0 == 0 && g.x1 == 0) g.x1 = 1; else g.x1 = g.x0 + 1; }
    if (!(g.y1 > g.y0)) { if (g.y0 == 0 && g.y1 == 0) g.y1 = 1; else g.y1 = g.y0 + 1; }
    const char *xlabel = o->x_label ? o->x_label : "X", *ylabel = o->y_label ? o->y_label : "Y";
    const char *xname = cui__frame_printf(ui, "%.*s", cui__display_len(xlabel), xlabel);   /* for the tooltips */
    const char *yname = cui__frame_printf(ui, "%.*s", cui__display_len(ylabel), ylabel);
    int xrd = cui__cv_readout_decimals(g.x1 - g.x0), yrd = cui__cv_readout_decimals(g.y1 - g.y0);

    cui_id id = cui_make_id(ui, id_str);
    cui_id plot_id = id ^ 0x9107C0u, tip_id = id ^ 0x7199u;
    cui_id hid[2] = { id ^ 0x4A11u, id ^ 0x4A12u };
    cui__cv_state *st = cui_state(ui, id ^ 0x5C0Eu, sizeof *st);
    if (!st->init) { st->sel = -1; st->init = true; }
    int *selp = o->selected ? o->selected : &st->sel;
    int sel = *selp;
    if (sel < -1 || sel >= n) sel = -1;
    bool shift = (ui->mods & CUI_MOD_SHIFT) != 0;
    bool snap = o->snap || shift;
    cui_rect R = cui__prev_rect(ui, plot_id);
    bool have = R.w > 0 && R.h > 0;
    cui__cv_layout L = cui__cv_layout_of(ui, R, &g, o->y_format);
    float mx = ui->mx, my = ui->my;

    /* a key or a handle being dragged */
    if (have && ui->mdown[0] && !ui->mpressed[0]) {
        for (int i = 0; i < n; i++) {
            if (ui->active != cui__cv_key_id(id, i)) continue;
            float nx = cui__cv_ux(&L, &g, mx + st->grab_x), ny = cui__cv_uy(&L, &g, my + st->grab_y);
            if (snap) { nx = cui__cv_snap(nx, L.xstep * 0.5f); ny = cui__cv_snap(ny, L.ystep * 0.5f); }
            if (cui__cv_place(keys, n, i, &g, nx, ny)) res.changed = true;
            sel = i;
            break;
        }
        for (int side = 0; side < 2 && sel >= 0; side++) {
            if (ui->active != hid[side]) continue;
            cui_curve_key *k = &keys[sel];
            float dx = cui__cv_ux(&L, &g, mx + st->grab_x) - k->x, dy = cui__cv_uy(&L, &g, my + st->grab_y) - k->y;
            if (side == 0) { k->in_dx = CUI_MIN(dx, 0.0f); k->in_dy = dy; }
            else { k->out_dx = CUI_MAX(dx, 0.0f); k->out_dy = dy; }
            res.changed = true;
        }
    }

    /* keys: press selects and grabs, keys nudge and delete */
    int remove = -1, hot_key = -1, active_key = -1, focus_key = -1;
    for (int i = 0; i < n; i++) {
        cui_id kid = cui__cv_key_id(id, i);
        cui_interact r = cui__interact(ui, kid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
        if (r.pressed && ui->mpressed[0] && have) {
            sel = i;
            st->grab_x = cui__cv_sx(&L, &g, keys[i].x) - mx;
            st->grab_y = cui__cv_sy(&L, &g, keys[i].y) - my;
        }
        if (r.hovered || r.active) { hot_key = i; cui__set_cursor(ui, r.active ? CUI_CURSOR_MOVE : CUI_CURSOR_HAND); }
        if (r.active) active_key = i;
        if (r.focus_visible) {
            focus_key = i;
            sel = i;
            float fx = (g.x1 - g.x0) * (shift ? 0.001f : 0.01f), fy = (g.y1 - g.y0) * (shift ? 0.001f : 0.01f);
            float nx = keys[i].x, ny = keys[i].y;
            if (cui_key_pressed(ui, CUI_KEY_LEFT)) nx -= fx;
            if (cui_key_pressed(ui, CUI_KEY_RIGHT)) nx += fx;
            if (cui_key_pressed(ui, CUI_KEY_DOWN)) ny -= fy;
            if (cui_key_pressed(ui, CUI_KEY_UP)) ny += fy;
            if ((nx != keys[i].x || ny != keys[i].y) && cui__cv_place(keys, n, i, &g, nx, ny)) res.changed = true;
        }
        if (r.focused && (cui_key_pressed(ui, CUI_KEY_DELETE) || cui_key_pressed(ui, CUI_KEY_BACKSPACE))) remove = i;
        if (r.hovered && !r.active && ui->active == 0)
            cui__tooltip_passive(ui, kid, cui__frame_printf(ui, "%s %s, %s %s", xname, cui__cv_fmt(ui, o->x_format, xrd, keys[i].x),
                                                            yname, cui__cv_fmt(ui, o->y_format, yrd, keys[i].y)));
    }

    /* the selected key's handles; grabbing one makes a computed tangent the key's own */
    int hot_handle = -1, active_handle = -1;
    cui_vec2 hpos[2];
    bool hshow[2] = { false, false };
    for (int side = 0; side < 2; side++) {
        hshow[side] = have && sel >= 0 && cui__cv_handle_pos(keys, n, sel, side, &L, &g, &hpos[side]);
        if (!hshow[side]) continue;
        cui_interact r = cui__interact(ui, hid[side], CUI_IF_DRAG);
        if (r.pressed && ui->mpressed[0]) {
            if (keys[sel].mode != CUI_CURVE_FREE) { cui__cv_bake(keys, n, sel); res.changed = true; }
            st->grab_x = hpos[side].x - mx;
            st->grab_y = hpos[side].y - my;
        }
        if (r.hovered || r.active) { hot_handle = side; cui__set_cursor(ui, r.active ? CUI_CURSOR_MOVE : CUI_CURSOR_HAND); }
        if (r.active) active_handle = side;
    }

    /* empty space: a click clears the selection, a double click adds a key */
    cui_interact br = cui__interact(ui, plot_id, 0);
    cui_rect P = L.plot;
    bool inside = have && mx >= P.x - 6 && mx <= P.x + P.w + 6 && my >= P.y - 6 && my <= P.y + P.h + 6;
    if (br.pressed && ui->mpressed[0]) {
        sel = -1;
        if (br.double_clicked && inside && keys && count && n < capacity) {
            float x = cui__clampf(cui__cv_ux(&L, &g, mx), g.x0, g.x1);
            float y = cui__cv_uy(&L, &g, my);
            float on = n > 0 ? cui_curve_eval(keys, n, x) : y;
            if (n > 0 && fabsf(cui__cv_sy(&L, &g, on) - my) <= CUI__CV_NEAR) y = on;
            else if (snap) { x = cui__cv_snap(x, L.xstep * 0.5f); y = cui__cv_snap(y, L.ystep * 0.5f); }
            x = cui__clampf(x, g.x0, g.x1);
            y = cui__clampf(y, g.y0, g.y1);
            int j = 0;
            while (j < n && keys[j].x <= x) j++;
            memmove(&keys[j + 1], &keys[j], sizeof *keys * (size_t)(n - j));
            cui_curve_key nk;
            memset(&nk, 0, sizeof nk);
            nk.x = x; nk.y = y;
            /* inside a stepped or straight run the new key keeps that shape */
            nk.mode = (uint8_t)(j > 0 && !cui__cv_bezier_mode(keys[j - 1].mode) ? keys[j - 1].mode : CUI_CURVE_SMOOTH);
            keys[j] = nk;
            n++;
            *count = n;
            sel = j;
            res.added = j;
            res.changed = true;
            ui->focus = cui__cv_key_id(id, j);
            ui->focus_visible = false;
        }
    }
    /* the value under the pointer */
    if (have && ui->hot == plot_id && ui->active == 0 && n > 0 && mx >= P.x && mx <= P.x + P.w && my >= R.y && my <= R.y + R.h) {
        res.hovered = true;
        res.hover_x = cui__cv_ux(&L, &g, mx);
        res.hover_y = cui_curve_eval(keys, n, res.hover_x);
    }

    /* ---- build ---- */
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->bg = CUI_C(ui, SURFACE);
    on->border = 1;
    on->border_color = CUI_C(ui, BORDER);
    on->radius = CUI_CORNERS(ui->theme.radius_panel);
    on->flags |= CUI_NF_CLIP;
    on->gap = 0;
    cui__open(ui, outer);
    cui_push_id(ui, id_str);
    int pn = cui__node_new(ui, plot_id);
    ui->nodes[pn].w = CUI_FILL;
    ui->nodes[pn].h = CUI_PX(o->height > 0 ? o->height : CUI__CV_PLOT_H);
    cui__mark_interactive(ui, pn, 0);
    cui__cv_paint *pp = cui__frame_alloc(ui, sizeof *pp);
    memset(pp, 0, sizeof *pp);
    pp->n = n;
    pp->keys = cui__frame_alloc(ui, sizeof(cui_curve_key) * (size_t)CUI_MAX(1, n));
    if (n) memcpy(pp->keys, keys, sizeof(cui_curve_key) * (size_t)n);
    pp->key_hover = cui__frame_alloc(ui, sizeof(float) * (size_t)CUI_MAX(1, n));
    for (int i = 0; i < n; i++) {
        float *hv = cui_state(ui, cui__cv_key_id(id, i) ^ 0x40u, sizeof(float));
        pp->key_hover[i] = cui__tween(ui, hv, i == hot_key ? 1.0f : 0.0f, CUI__T_HOVER);
    }
    pp->g = g;
    pp->xfmt = o->x_format; pp->yfmt = o->y_format;
    pp->sel = sel;
    pp->focus_key = focus_key; pp->hot_key = hot_key; pp->active_key = active_key;
    pp->hot_handle = hot_handle; pp->active_handle = active_handle;
    pp->hover = res.hovered; pp->hx = res.hover_x; pp->hy = res.hover_y;
    pp->playhead = o->show_playhead; pp->px = o->playhead;
    pp->curve = cui__cv_accent(ui);
    ui->nodes[pn].paint = cui__paint_curve;
    ui->nodes[pn].paint_data = pp;
    cui__open(ui, pn);
    if (have) {
        /* hit areas, placed with the last frame's layout like every hit */
        for (int i = 0; i < n; i++) {
            const cui_curve_key *k = &keys[i];
            float kx = cui__cv_sx(&L, &g, k->x), ky = cui__cv_sy(&L, &g, k->y);
            int a =cui__absolute(ui, cui__cv_key_id(id, i), kx - R.x - CUI__CV_KEY_HIT * 0.5f, ky - R.y - CUI__CV_KEY_HIT * 0.5f, CUI__CV_KEY_HIT, CUI__CV_KEY_HIT);
            cui__mark_interactive(ui, a, CUI_IF_FOCUSABLE);
            ui->nodes[a].radius = CUI_CORNERS(CUI__CV_KEY_HIT * 0.5f);
        }
        for (int side = 0; side < 2; side++) {
            if (!hshow[side] || res.added >= 0) continue;
            int a = cui__absolute(ui, hid[side], hpos[side].x - R.x - CUI__CV_HANDLE_HIT * 0.5f, hpos[side].y - R.y - CUI__CV_HANDLE_HIT * 0.5f,
                                  CUI__CV_HANDLE_HIT, CUI__CV_HANDLE_HIT);
            cui__mark_interactive(ui, a, 0);
        }
        if (res.hovered) {
            float tx = cui__cv_sx(&L, &g, res.hover_x), ty = cui__cv_sy(&L, &g, res.hover_y);
            cui__absolute(ui, tip_id, tx - R.x - 1, ty - R.y - 4, 2, 2);
            cui__tooltip_passive(ui, tip_id, cui__frame_printf(ui, "%s %s, %s %s", xname, cui__cv_fmt(ui, o->x_format, xrd, res.hover_x),
                                                               yname, cui__cv_fmt(ui, o->y_format, yrd, res.hover_y)));
        }
    }
    cui__close(ui);   /* plot */

    /* the selected key's mode and exact position */
    int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[line].bg = CUI_C(ui, BORDER_SUBTLE);
    cui_rect orr = cui__prev_rect(ui, id);
    bool narrow = orr.w > 0 && orr.w < CUI__CV_NARROW;
    bool tiny = orr.w > 0 && orr.w < CUI__CV_TINY;   /* the two fields stack rather than cut their labels */
    int foot = cui__node_new(ui, 0);
    cui_node *fn = &ui->nodes[foot];
    fn->w = CUI_FILL;
    fn->pad[0] = fn->pad[2] = 10; fn->pad[1] = fn->pad[3] = 12;
    fn->gap = 8;
    fn->dir = narrow ? CUI_COLUMN : CUI_ROW;
    fn->align = narrow ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER;
    fn->min_h = (tiny ? 36 * 3 + 16 : narrow ? 36 * 2 + 8 : 36) + 20;
    fn->justify = narrow ? CUI_JUSTIFY_CENTER : CUI_JUSTIFY_START;   /* the hint sits left, centered vertically */
    cui__open(ui, foot);
    if (sel >= 0 && sel < n) {
        cui_curve_key *k = &keys[sel];
        int mode = k->mode <= CUI_CURVE_STEP ? k->mode : CUI_CURVE_SMOOTH;
        cui_select_opts so; memset(&so, 0, sizeof so);
        cui_scrub_opts xo; memset(&xo, 0, sizeof xo);
        cui_scrub_opts yo; memset(&yo, 0, sizeof yo);
        cui_button_opts bo; memset(&bo, 0, sizeof bo);
        so.width = narrow ? CUI_GROW(1) : CUI_PX(128);
        xo.width = yo.width = narrow ? CUI_GROW(1) : CUI_PX(132);
        xo.step = (g.x1 - g.x0) / 100.0f; yo.step = (g.y1 - g.y0) / 100.0f;
        xo.format = o->x_format ? o->x_format : cui__frame_printf(ui, "%%.%df", xrd);
        yo.format = o->y_format ? o->y_format : cui__frame_printf(ui, "%%.%df", yrd);
        bo.variant = CUI_BUTTON_GHOST;
        bo.tooltip = "Remove key";
        bo.disabled = n <= 2;
        float vx = k->x, vy = k->y;
        bool del = false;
        int row1 = -1;
        if (narrow) { row1 = cui__node_new(ui, 0); ui->nodes[row1].dir = CUI_ROW; ui->nodes[row1].gap = 8; ui->nodes[row1].align = CUI_ALIGN_CENTER; cui__open(ui, row1); }
        if (cui_select_ex(ui, "##mode", modes, 4, &mode, &so)) {
            if (mode == CUI_CURVE_FREE && k->mode != CUI_CURVE_FREE) cui__cv_bake(keys, n, sel);
            k->mode = (uint8_t)mode;
            res.changed = true;
        }
        if (narrow) {
            del = cui_icon_button_ex(ui, CUI_ICON_TRASH_2, &bo);
            cui__close(ui);
            int row2 = cui__node_new(ui, 0);
            ui->nodes[row2].dir = tiny ? CUI_COLUMN : CUI_ROW; ui->nodes[row2].gap = 8;
            cui__open(ui, row2);
        }
        bool cx = cui_scrub_ex(ui, cui__frame_printf(ui, "%s##cv-x", xlabel), &vx, g.x0, g.x1, &xo);
        bool cy = cui_scrub_ex(ui, cui__frame_printf(ui, "%s##cv-y", ylabel), &vy, g.y0, g.y1, &yo);
        if ((cx || cy) && cui__cv_place(keys, n, sel, &g, vx, vy)) res.changed = true;
        if (narrow) cui__close(ui);
        else {
            cui_spacer(ui);
            del = cui_icon_button_ex(ui, CUI_ICON_TRASH_2, &bo);
        }
        if (del) remove = sel;
    } else {
        int t = cui__text_node(ui, n < capacity ? "Double-click the plot to add a key, or select one to edit it." : "Select a key to edit it.", -1,
                               CUI_FONT_BODY, cui__fsz(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), true);
        ui->nodes[t].line_height = 1.4f;
    }
    cui__close(ui);   /* footer */
    cui_pop_id(ui);
    cui__close(ui);   /* outer */

    if (remove >= 0 && remove < n && n > 2) {
        cui__cv_remove(keys, &n, remove);
        *count = n;
        res.removed = remove;
        res.changed = true;
        sel = CUI_MIN(remove, n - 1);
        if (ui->focus == cui__cv_key_id(id, remove) || ui->focus == 0) ui->focus = cui__cv_key_id(id, sel);
    }
    *selp = sel;
    res.selected = sel;
    ui->last_id = id;
    return res;
}

/* ======================================================================== */
/* Gradient                                                                 */
/* ======================================================================== */

static uint8_t cui__gr_lerp8(uint8_t a, uint8_t b, float t) {
    float v = (float)a + ((float)b - (float)a) * t;
    return (uint8_t)CUI_CLAMP(v + 0.5f, 0.0f, 255.0f);
}

cui_color cui_gradient_eval(const cui_gradient_stop *s, int n, float t) {
    cui_color none = { 0, 0, 0, 0 };
    if (!s || n <= 0) return none;
    /* the stops on either side of t (works on unsorted stops too) */
    int lo = -1, hi = -1;
    for (int i = 0; i < n; i++) {
        if (s[i].pos <= t && (lo < 0 || s[i].pos > s[lo].pos)) lo = i;
        if (s[i].pos >= t && (hi < 0 || s[i].pos < s[hi].pos)) hi = i;
    }
    if (lo < 0 && hi < 0) return s[0].color;   /* NaN */
    if (lo < 0) return s[hi].color;
    if (hi < 0) return s[lo].color;
    float span = s[hi].pos - s[lo].pos;
    if (!(span > 0)) return s[lo].color;
    float f = (t - s[lo].pos) / span;
    cui_color a = s[lo].color, b = s[hi].color, c;
    c.r = cui__gr_lerp8(a.r, b.r, f); c.g = cui__gr_lerp8(a.g, b.g, f);
    c.b = cui__gr_lerp8(a.b, b.b, f); c.a = cui__gr_lerp8(a.a, b.a, f);
    return c;
}

#define CUI__GR_RAMP_H  28.0f
#define CUI__GR_TRACK_H 26.0f
#define CUI__GR_HIT_W   22.0f
#define CUI__GR_PAD     9.0f    /* room for a stop marker past each end of the ramp */
#define CUI__GR_AWAY    32.0f   /* dragged this far off the ramp, a stop is removed on release */
#define CUI__GR_NARROW  440.0f

typedef struct { int sel; float grab; bool pending, init; } cui__gr_state;

static cui_id cui__gr_stop_id(cui_id eid, int i) { return cui__hash(&i, sizeof i, eid ^ 0x6A57u); }

/* After stop i moved, slides it to its sorted place; returns its new index. */
static int cui__gr_resort(cui_gradient_stop *s, int n, int i) {
    while (i > 0 && s[i - 1].pos > s[i].pos) { cui_gradient_stop t = s[i - 1]; s[i - 1] = s[i]; s[i] = t; i--; }
    while (i < n - 1 && s[i + 1].pos < s[i].pos) { cui_gradient_stop t = s[i + 1]; s[i + 1] = s[i]; s[i] = t; i++; }
    return i;
}

typedef struct { cui_gradient_stop *stops; int n; cui_color light, dark, edge; bool alpha; } cui__gr_ramp_paint;

/* A checkerboard inside a rounded rectangle: cells are cut to the shape's
 * two bands, so nothing shows past the rounded corners. */
static void cui__gr_checker(cui_ctx *ui, cui_rect r, float rad, float cell, cui_color light, cui_color dark) {
    cui_draw_rect(ui, r, CUI_CORNERS(rad), light);
    cui_rect band[2] = { { r.x + rad, r.y, r.w - 2 * rad, r.h }, { r.x, r.y + rad, r.w, r.h - 2 * rad } };
    int cols = (int)ceilf(r.w / cell), rows = (int)ceilf(r.h / cell);
    for (int y = 0; y < rows; y++)
        for (int x = (y & 1); x < cols; x += 2) {
            cui_rect c = { r.x + (float)x * cell, r.y + (float)y * cell, cell, cell };
            c = cui__rect_intersect(c, r);
            for (int b = 0; b < 2; b++) {
                cui_rect q = cui__rect_intersect(c, band[b]);
                if (q.w > 0 && q.h > 0) cui_draw_rect(ui, q, CUI_CORNERS(0), dark);
            }
        }
}

/* The ramp: vertical strips between the stops and along the rounded ends,
 * each strip's edges colored by cui_gradient_eval (the renderer interpolates
 * between them exactly as the eval does). */
static void cui__paint_gradient_ramp(cui_ctx *ui, cui_node *nd, void *data) {
    cui__gr_ramp_paint *p = data;
    cui_rect r = nd->rect;
    if (r.w < 4 || p->n <= 0) return;
    float rad = CUI_MIN(8.0f, r.h * 0.5f);
    if (p->alpha) cui__gr_checker(ui, r, rad, 7.0f, p->light, p->dark);
    enum { ARC = 8 };
    int cap = 2 * (ARC + 1) + p->n + 2, m = 0;
    float *xs = cui__frame_alloc(ui, sizeof(float) * (size_t)cap);
    for (int k = 0; k <= ARC; k++) {
        float e = rad * (1.0f - cosf((float)k / ARC * CUI_PI * 0.5f));
        xs[m++] = r.x + e;
        xs[m++] = r.x + r.w - e;
    }
    for (int i = 0; i < p->n; i++) xs[m++] = r.x + cui__clampf(p->stops[i].pos, 0, 1) * r.w;
    for (int a = 1; a < m; a++) {   /* insertion sort: a few dozen values */
        float v = xs[a];
        int b = a - 1;
        while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; }
        xs[b + 1] = v;
    }
    cui_cvert *v = cui__frame_alloc(ui, sizeof(cui_cvert) * (size_t)m * 2);
    uint32_t *idx = cui__frame_alloc(ui, sizeof(uint32_t) * (size_t)m * 6);
    int nv = 0, ni = 0;
    for (int k = 0; k < m; k++) {
        float x = xs[k];
        if (k > 0 && x - xs[k - 1] < 1e-3f) continue;
        float dl = r.x + rad - x, dr = x - (r.x + r.w - rad), dd = CUI_MAX(dl, dr), h = 0;
        if (dd > 0) { dd = CUI_MIN(dd, rad); h = rad - sqrtf(CUI_MAX(0.0f, rad * rad - dd * dd)); }
        cui_color c = cui_gradient_eval(p->stops, p->n, (x - r.x) / r.w);
        if (nv >= 2) {
            uint32_t a = (uint32_t)nv - 2;
            uint32_t q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
            memcpy(idx + ni, q, sizeof q);
            ni += 6;
        }
        v[nv].x = x; v[nv].y = r.y + h; v[nv].c = c; nv++;
        v[nv].x = x; v[nv].y = r.y + r.h - h; v[nv].c = c; nv++;
    }
    if (ni) cui__draw_triangles(ui, v, nv, idx, ni);
    cui_draw_rect_border(ui, r, CUI_CORNERS(rad), cui__rgba(0, 0, 0, 0), p->edge, 1);
}

typedef struct {
    cui_gradient_stop *stops; int n;
    int sel, focus, hot, pending;
    float pending_dy;
    cui_color light, dark, ring, ring_on, surface, edge;
} cui__gr_track_paint;

static void cui__paint_gradient_track(cui_ctx *ui, cui_node *nd, void *data) {
    cui__gr_track_paint *p = data;
    cui_rect r = nd->rect;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < p->n; i++) {
            bool sel = i == p->sel;
            if (sel != (pass == 1)) continue;
            float cx = roundf(r.x + cui__clampf(p->stops[i].pos, 0, 1) * r.w);
            bool lift = i == p->pending;
            if (lift) cui__push_xform(ui, 1, 0, 0, 0.4f, 0, p->pending_dy);
            cui_color ring = sel ? p->ring_on : (i == p->hot ? cui__control_edge(ui, 1) : p->ring);
            cui_vec2 tri[3] = { { cx, r.y + 1 }, { cx + 5, r.y + 7 }, { cx - 5, r.y + 7 } };
            cui_draw_polygon(ui, tri, 3, ring);
            cui_rect body = { cx - 8, r.y + 6, 16, 16 };
            cui_draw_rect_border(ui, body, CUI_CORNERS(5), p->surface, ring, sel ? 2.0f : 1.0f);
            cui_rect sw = cui__rect_inset(body, 3);
            cui_color c = p->stops[i].color;
            if (c.a < 255) {
                cui_draw_rect(ui, sw, CUI_CORNERS(2.5f), p->light);
                cui_draw_rect(ui, (cui_rect){ sw.x + sw.w * 0.5f, sw.y, sw.w * 0.5f, sw.h * 0.5f }, (cui_corners){ 0, 2.5f, 0, 0 }, p->dark);
                cui_draw_rect(ui, (cui_rect){ sw.x, sw.y + sw.h * 0.5f, sw.w * 0.5f, sw.h * 0.5f }, (cui_corners){ 0, 0, 0, 2.5f }, p->dark);
            }
            cui_draw_rect_border(ui, sw, CUI_CORNERS(2.5f), c, p->edge, 1);
            if (i == p->focus) cui_draw_rect_border(ui, cui__rect_inset(body, -3), CUI_CORNERS(8), cui__rgba(0, 0, 0, 0), CUI_C(ui, FOCUS_RING), 2);
            if (lift) cui__pop_xform(ui);
        }
}

cui_gradient_result cui_gradient_editor_ex(cui_ctx *ui, const char *id_str, cui_gradient_stop *stops, int *count, int capacity, const cui_gradient_opts *o) {
    cui_gradient_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_gradient_result res;
    memset(&res, 0, sizeof res);
    res.added = res.removed = -1;
    int n = (stops && count && *count > 0) ? *count : 0;
    if (capacity < n) capacity = n;
    cui_id id = cui_make_id(ui, id_str);
    cui_id ramp_id = id ^ 0x7A3Bu, track_id = id ^ 0x7AC4u;
    cui__gr_state *st = cui_state(ui, id ^ 0x6E5Du, sizeof *st);
    if (!st->init) { st->sel = 0; st->init = true; }
    int *selp = o->selected ? o->selected : &st->sel;
    int sel = *selp;
    if (sel < 0 || sel >= n) sel = n > 0 ? 0 : -1;
    bool shift = (ui->mods & CUI_MOD_SHIFT) != 0;
    cui_rect RR = cui__prev_rect(ui, ramp_id), TR = cui__prev_rect(ui, track_id);
    bool have = RR.w > 0 && TR.w > 0;
    float mx = ui->mx, my = ui->my;
    bool open_picker = false;
    int remove = -1, pending = -1;
    float pending_dy = 0;

    /* the stop being dragged; it may pass its neighbours (the array stays
     * sorted and the drag, focus and selection follow it) */
    for (int i = 0; i < n && have; i++) {
        cui_id sid = cui__gr_stop_id(id, i);
        if (ui->active != sid) continue;
        if (ui->mdown[0] && !ui->mpressed[0]) {
            bool away = n > 2 && (my > TR.y + TR.h + CUI__GR_AWAY || my < RR.y - CUI__GR_AWAY);
            st->pending = away;
            if (away) { pending = i; pending_dy = my - (TR.y + TR.h * 0.5f); }
            else {
                float pos = cui__clampf((mx - st->grab - RR.x) / RR.w, 0, 1);
                if (pos != stops[i].pos) {
                    stops[i].pos = pos;
                    res.changed = true;
                    int j = cui__gr_resort(stops, n, i);
                    if (j != i) {
                        ui->active = cui__gr_stop_id(id, j);
                        if (ui->focus == sid) ui->focus = ui->active;
                    }
                    sel = j;
                }
            }
        } else if (ui->mreleased[0]) {
            if (st->pending && n > 2) remove = i;
            st->pending = false;
        }
        break;
    }

    /* stops: press selects and grabs, a double click opens the picker */
    int hot = -1, focus = -1, nudge = -1;
    float nudge_d = 0;
    for (int i = 0; i < n; i++) {
        cui_interact r = cui__interact(ui, cui__gr_stop_id(id, i), CUI_IF_FOCUSABLE | CUI_IF_DRAG);
        if (r.pressed && ui->mpressed[0] && have) {
            sel = i;
            st->grab = mx - (RR.x + stops[i].pos * RR.w);
            st->pending = false;
            if (r.double_clicked) open_picker = true;
        }
        if (r.hovered || r.active) { hot = i; cui__set_cursor(ui, r.active ? CUI_CURSOR_MOVE : CUI_CURSOR_HAND); }
        if (r.focus_visible) {
            focus = i;
            sel = i;
            float step = shift ? 0.001f : 0.01f;
            if (cui_key_pressed(ui, CUI_KEY_LEFT)) { nudge = i; nudge_d -= step; }
            if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { nudge = i; nudge_d += step; }
            if (r.keyboard_activate) open_picker = true;
        }
        if (r.focused && (cui_key_pressed(ui, CUI_KEY_DELETE) || cui_key_pressed(ui, CUI_KEY_BACKSPACE))) remove = i;
    }
    if (nudge >= 0) {
        float pos = cui__clampf(stops[nudge].pos + nudge_d, 0, 1);
        if (pos != stops[nudge].pos) {
            cui_id was = cui__gr_stop_id(id, nudge);
            stops[nudge].pos = pos;
            res.changed = true;
            int j = cui__gr_resort(stops, n, nudge);
            if (ui->focus == was) ui->focus = cui__gr_stop_id(id, j);
            sel = j;
            focus = j;
        }
    }
    /* the ramp and the empty track: a press adds a stop there and drags it */
    cui_interact rr = cui__interact(ui, ramp_id, CUI_IF_DRAG);
    cui_interact tr = cui__interact(ui, track_id, CUI_IF_DRAG);
    if ((rr.hovered || tr.hovered) && n < capacity) cui__set_cursor(ui, CUI_CURSOR_HAND);
    if (((rr.pressed || tr.pressed) && ui->mpressed[0]) && have && stops && count && n < capacity && remove < 0) {
        float pos = cui__clampf((mx - RR.x) / RR.w, 0, 1);
        cui_gradient_stop ns;
        ns.pos = pos;
        ns.color = cui_gradient_eval(stops, n, pos);
        int j = 0;
        while (j < n && stops[j].pos <= pos) j++;
        memmove(&stops[j + 1], &stops[j], sizeof *stops * (size_t)(n - j));
        stops[j] = ns;
        n++;
        *count = n;
        sel = j;
        res.added = j;
        res.changed = true;
        ui->active = ui->focus = cui__gr_stop_id(id, j);
        ui->focus_visible = false;
        st->grab = 0;
        st->pending = false;
    }

    /* ---- build ---- */
    bool any_alpha = false;
    for (int i = 0; i < n; i++) if (stops[i].color.a < 255) any_alpha = true;
    bool dark = ui->mode == CUI_THEME_DARK;
    cui_color light = CUI_C(ui, SURFACE), darkc = cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), dark ? 0.16f : 0.1f);
    cui_color edge = cui_alpha(CUI_C(ui, FOREGROUND), dark ? 0.16f : 0.12f);
    cui_gradient_stop *copy = cui__frame_alloc(ui, sizeof(cui_gradient_stop) * (size_t)CUI_MAX(1, n));
    if (n) memcpy(copy, stops, sizeof(cui_gradient_stop) * (size_t)n);

    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->gap = 0;
    cui__open(ui, outer);
    cui_push_id(ui, id_str);
    int bars = cui__node_new(ui, 0);   /* the ramp and the track, inset so the end markers fit */
    ui->nodes[bars].w = CUI_FILL;
    ui->nodes[bars].pad[1] = ui->nodes[bars].pad[3] = CUI__GR_PAD;
    ui->nodes[bars].gap = 0;
    cui__open(ui, bars);
    int ramp = cui__leaf(ui, ramp_id, CUI_FILL, CUI_PX(o->height > 0 ? o->height : CUI__GR_RAMP_H));
    cui__mark_interactive(ui, ramp, 0);
    cui__gr_ramp_paint *rp = cui__frame_alloc(ui, sizeof *rp);
    rp->stops = copy; rp->n = n; rp->light = light; rp->dark = darkc; rp->edge = edge; rp->alpha = any_alpha;
    ui->nodes[ramp].paint = cui__paint_gradient_ramp;
    ui->nodes[ramp].paint_data = rp;
    int track = cui__node_new(ui, track_id);
    ui->nodes[track].w = CUI_FILL;
    ui->nodes[track].h = CUI_PX(CUI__GR_TRACK_H);
    cui__mark_interactive(ui, track, 0);
    cui__gr_track_paint *tp = cui__frame_alloc(ui, sizeof *tp);
    tp->stops = copy; tp->n = n; tp->sel = sel; tp->focus = focus; tp->hot = hot;
    tp->pending = pending; tp->pending_dy = pending_dy;
    tp->light = light; tp->dark = darkc; tp->edge = edge;
    tp->ring = CUI_C(ui, BORDER_STRONG); tp->ring_on = cui__cv_accent(ui); tp->surface = CUI_C(ui, SURFACE);
    ui->nodes[track].paint = cui__paint_gradient_track;
    ui->nodes[track].paint_data = tp;
    cui__open(ui, track);
    if (have)
        for (int i = 0; i < n; i++) {
            float cx = RR.x + stops[i].pos * RR.w;
            int a = cui__absolute(ui, cui__gr_stop_id(id, i), cx - TR.x - CUI__GR_HIT_W * 0.5f, 0, CUI__GR_HIT_W, CUI__GR_TRACK_H);
            cui__mark_interactive(ui, a, CUI_IF_FOCUSABLE);
        }
    cui__close(ui);   /* track */
    cui__close(ui);   /* bars */

    /* the selected stop: color (the picker), location, opacity */
    cui_rect orr = cui__prev_rect(ui, id);
    bool narrow = orr.w > 0 && orr.w < CUI__GR_NARROW, tiny = orr.w > 0 && orr.w < CUI__CV_TINY;
    int row = cui__node_new(ui, 0);
    cui_node *rn = &ui->nodes[row];
    rn->w = CUI_FILL;
    rn->dir = narrow ? CUI_COLUMN : CUI_ROW;
    rn->align = narrow ? CUI_ALIGN_STRETCH : CUI_ALIGN_CENTER;
    rn->gap = 8;
    rn->pad[0] = 6;
    cui__open(ui, row);
    if (sel >= 0 && sel < n) {
        cui_gradient_stop *s = &stops[sel];
        cui_id cid = cui_make_id(ui, "##color");
        if (open_picker) cui__color_picker_open(ui, cid, s->color);
        int line1 = -1;
        if (narrow) { line1 = cui__node_new(ui, 0); ui->nodes[line1].dir = CUI_ROW; ui->nodes[line1].gap = 8; ui->nodes[line1].align = CUI_ALIGN_CENTER; cui__open(ui, line1); }
        cui_color c = s->color;
        uint8_t alpha = c.a;
        if (cui_color_picker(ui, "##color", &c)) {
            c.a = alpha;   /* the picker edits the color; opacity has its own field */
            if (memcmp(&c, &s->color, sizeof c) != 0) { s->color = c; res.changed = true; }
        }
        int pk = ui->nodes[cui__current(ui)].last;
        if (narrow && pk >= 0 && ui->nodes[pk].id == cid) ui->nodes[pk].w = CUI_GROW(1);
        cui_button_opts bo; memset(&bo, 0, sizeof bo);
        bo.variant = CUI_BUTTON_GHOST;
        bo.tooltip = "Remove stop";
        bo.disabled = n <= 2;
        bool del = false;
        if (narrow) {
            del = cui_icon_button_ex(ui, CUI_ICON_TRASH_2, &bo);
            cui__close(ui);
            int line2 = cui__node_new(ui, 0);
            ui->nodes[line2].dir = tiny ? CUI_COLUMN : CUI_ROW; ui->nodes[line2].gap = 8;
            cui__open(ui, line2);
        }
        cui_scrub_opts lo; memset(&lo, 0, sizeof lo);
        lo.step = 1; lo.format = "%.0f"; lo.suffix = "%";
        lo.width = narrow ? CUI_GROW(1) : CUI_PX(140);
        cui_scrub_opts ao = lo;
        float loc = s->pos * 100.0f, op = (float)s->color.a * (100.0f / 255.0f);
        if (cui_scrub_ex(ui, "Location", &loc, 0, 100, &lo)) {
            s->pos = cui__clampf(loc / 100.0f, 0, 1);
            res.changed = true;
            int was = sel;
            sel = cui__gr_resort(stops, n, sel);
            if (sel != was && ui->focus == cui__gr_stop_id(id, was)) ui->focus = cui__gr_stop_id(id, sel);
            s = &stops[sel];
        }
        if (cui_scrub_ex(ui, "Opacity", &op, 0, 100, &ao)) {
            s->color.a = (uint8_t)CUI_CLAMP(op * 2.55f + 0.5f, 0.0f, 255.0f);
            res.changed = true;
        }
        if (narrow) cui__close(ui);
        else {
            cui_spacer(ui);
            del = cui_icon_button_ex(ui, CUI_ICON_TRASH_2, &bo);
        }
        if (del) remove = sel;
    }
    cui__close(ui);   /* row */
    cui_pop_id(ui);
    cui__close(ui);   /* outer */

    if (remove >= 0 && remove < n && n > 2) {
        memmove(&stops[remove], &stops[remove + 1], sizeof *stops * (size_t)(n - remove - 1));
        n--;
        *count = n;
        res.removed = remove;
        res.changed = true;
        sel = CUI_MIN(remove, n - 1);
        if (ui->focus == cui__gr_stop_id(id, remove)) ui->focus = cui__gr_stop_id(id, sel);
    }
    *selp = sel;
    res.selected = sel;
    ui->last_id = id;
    return res;
}
