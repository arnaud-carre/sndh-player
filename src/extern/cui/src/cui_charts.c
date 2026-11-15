/* Charts: line and area, bar (grouped or stacked), donut, sparkline and
 * gauge. Charts animate in once, show a crosshair and a tooltip on hover,
 * and use the theme's series palette. */
#include "cui_internal.h"

static float cui__fs(cui_ctx *ui, float size) { return size * ui->theme.font_scale; }

typedef struct { cui_spring intro; cui_spring hx; float hover_t; int hover; bool init; float plot_x, plot_w; } cui__chart_state;

void cui__nice_range(float lo, float hi, int target_ticks, float *out_lo, float *out_hi, float *out_step) {
    if (hi <= lo) { hi = lo + 1; }
    float range = hi - lo;
    float rough = range / (float)target_ticks;
    float mag = powf(10.0f, floorf(log10f(rough)));
    float norm = rough / mag;
    float step = norm < 1.5f ? 1 : norm < 2.25f ? 2 : norm < 3.5f ? 2.5f : norm < 7.5f ? 5 : 10;
    step *= mag;
    *out_lo = floorf(lo / step) * step;
    *out_hi = ceilf(hi / step) * step;
    if (*out_hi - *out_lo < step * 0.5f) *out_hi = *out_lo + step;
    *out_step = step;
}

const char *cui__fmt_value(cui_ctx *ui, const char *fmt, float v) {
    if (fmt) {
        char *s = cui__frame_printf(ui, fmt, (double)v);
        /* money gets its thousands separators: "$3,220", not "$3220" */
        if (s && (strchr(fmt, '$') || strstr(fmt, "\xe2\x82\xac") || strstr(fmt, "\xc2\xa3"))) {
            char *d = s;
            while (*d && (*d < '0' || *d > '9')) d++;
            int run = 0;
            while (d[run] >= '0' && d[run] <= '9') run++;
            if (run >= 4) {
                size_t n = strlen(s), commas = (size_t)(run - 1) / 3;
                char *o = cui__frame_alloc(ui, n + commas + 1);
                size_t pre = (size_t)(d - s), w = 0;
                memcpy(o, s, pre); w = pre;
                for (int i = 0; i < run; i++) {
                    o[w++] = d[i];
                    if ((run - 1 - i) % 3 == 0 && i < run - 1) o[w++] = ',';
                }
                memcpy(o + w, d + run, n - pre - (size_t)run + 1);
                s = o;
            }
        }
        /* zero needs no magnitude: "$0k" reads as "$0" */
        if (v == 0 && s) {
            size_t n = strlen(s);
            if (n >= 2 && s[n - 2] == '0' && (s[n - 1] == 'k' || s[n - 1] == 'K' || s[n - 1] == 'M' || s[n - 1] == 'B')) s[n - 1] = 0;
        }
        return s;
    }
    float a = fabsf(v);
    if (a >= 1e9f) return cui__frame_printf(ui, "%.1fB", (double)(v / 1e9f));
    if (a >= 1e6f) return cui__frame_printf(ui, "%.1fM", (double)(v / 1e6f));
    if (a >= 1e4f) return cui__frame_printf(ui, "%.0fk", (double)(v / 1e3f));
    if (a >= 1e3f) return cui__frame_printf(ui, "%.1fk", (double)(v / 1e3f));
    if (a != floorf(a) && a < 10) return cui__frame_printf(ui, "%.2f", (double)v);
    if (a != floorf(a)) return cui__frame_printf(ui, "%.1f", (double)v);
    return cui__frame_printf(ui, "%.0f", (double)v);
}

static cui_color cui__series_color(cui_ctx *ui, const cui_series *s, int k) {
    if (cui__color_set(s->color)) return cui_color_of(ui, s->color);
    return ui->theme.colors[CUI_TOKEN_SERIES_1 + (k % 6)];
}

typedef struct {
    cui_series *series;
    int count;
    const char *const *labels;
    int label_count;
    cui_chart_opts o;
    float intro;
    int hover;
    float hover_t;
    float hx;
    bool bars;
    bool horizontal;
    cui__chart_state *st;
} cui__chart_paint;

static void cui__chart_tooltip(cui_ctx *ui, cui__chart_paint *p, cui_rect plot, float anchor_x, float anchor_y, int idx) {
    if (p->hover_t <= 0.01f) return;
    float pad = 10, lh = 18;
    const char *title = (p->labels && idx < p->label_count) ? p->labels[idx] : cui__frame_printf(ui, "#%d", idx + 1);
    float w = cui__text_width(ui, CUI_FONT_MEDIUM, ui->theme.text_sm, title, -1, false);
    for (int k = 0; k < p->count; k++) {
        cui_series *s = &p->series[k];
        if (idx >= s->count) continue;
        const char *vs = cui__fmt_value(ui, p->o.format, s->values[idx]);
        float lw = cui__text_width(ui, CUI_FONT_BODY, ui->theme.text_sm, s->name ? s->name : "", -1, false);
        float vw = cui__text_width(ui, CUI_FONT_MEDIUM, ui->theme.text_sm, vs, -1, true);
        w = CUI_MAX(w, 14 + lw + 18 + vw);
    }
    float W = w + pad * 2, H = pad * 2 + lh * (float)(1 + p->count) + 2;
    float x = anchor_x + 14, y = anchor_y - H * 0.5f;
    if (x + W > plot.x + plot.w) x = anchor_x - 14 - W;
    if (x < plot.x - 40) x = plot.x - 40;
    if (y < plot.y - 8) y = plot.y - 8;
    if (y + H > plot.y + plot.h + 8) y = plot.y + plot.h + 8 - H;
    cui_rect r = { x, y, W, H };
    float a = p->hover_t;
    cui__push_xform(ui, 1, 0, 0, a, 0, 0);
    cui__draw_elevation(ui, r, CUI_CORNERS(12), CUI_ELEVATION_RAISED, 1);
    cui_draw_rect_border(ui, r, CUI_CORNERS(12), CUI_C(ui, SURFACE_RAISED),
                         ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui__rgba(0, 0, 0, 18), 1);
    float ty = y + pad;
    float fh = cui__font_line_height(ui, CUI_FONT_MEDIUM, ui->theme.text_sm);
    cui__draw_text_line(ui, CUI_FONT_MEDIUM, ui->theme.text_sm, x + pad, ty + (lh - fh) * 0.5f, CUI_C(ui, FOREGROUND), title, -1, false);
    ty += lh + 2;
    for (int k = 0; k < p->count; k++) {
        cui_series *s = &p->series[k];
        if (idx >= s->count) continue;
        cui_color c = cui__series_color(ui, s, k);
        cui_rect sw = { x + pad, ty + lh * 0.5f - 4, 8, 8 };
        cui_draw_rect(ui, sw, CUI_CORNERS(2.5f), c);
        cui__draw_text_line(ui, CUI_FONT_BODY, ui->theme.text_sm, x + pad + 14, ty + (lh - fh) * 0.5f, CUI_C(ui, TEXT_SECONDARY), s->name ? s->name : "", -1, false);
        const char *vs = cui__fmt_value(ui, p->o.format, s->values[idx]);
        float vw = cui__text_width(ui, CUI_FONT_MEDIUM, ui->theme.text_sm, vs, -1, true);
        cui__draw_text_line(ui, CUI_FONT_MEDIUM, ui->theme.text_sm, x + W - pad - vw, ty + (lh - fh) * 0.5f, CUI_C(ui, FOREGROUND), vs, -1, true);
        ty += lh;
    }
    cui__pop_xform(ui);
}

static void cui__smooth_points(cui_ctx *ui, const cui_vec2 *in, int n, cui_vec2 **out, int *out_n) {
    /* Catmull-Rom with monotone-ish tension, 8 segments per span */
    int seg = 8;
    int m = (n - 1) * seg + 1;
    cui_vec2 *o = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)m);
    int k = 0;
    for (int i = 0; i < n - 1; i++) {
        cui_vec2 p0 = in[i > 0 ? i - 1 : i], p1 = in[i], p2 = in[i + 1], p3 = in[i + 2 < n ? i + 2 : i + 1];
        for (int s = 0; s < seg; s++) {
            float t = (float)s / (float)seg, t2 = t * t, t3 = t2 * t;
            float tension = 0.5f;
            float x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
            float y = p1.y + (-(p0.y) * tension + p2.y * tension) * t * 0 + 0;
            /* y via cubic Hermite with clamped tangents (avoids overshoot) */
            float m1 = (p2.y - p0.y) * 0.5f, m2 = (p3.y - p1.y) * 0.5f;
            float d = p2.y - p1.y;
            if ((d > 0 && m1 < 0) || (d < 0 && m1 > 0)) m1 = 0;
            if ((d > 0 && m2 < 0) || (d < 0 && m2 > 0)) m2 = 0;
            float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
            y = h00 * p1.y + h10 * m1 + h01 * p2.y + h11 * m2;
            (void)x;
            o[k].x = p1.x + (p2.x - p1.x) * t;
            o[k].y = y;
            k++;
        }
    }
    o[k++] = in[n - 1];
    *out = o;
    *out_n = k;
}

static void cui__paint_xy_chart(cui_ctx *ui, cui_node *n, void *data) {
    cui__chart_paint *p = data;
    cui_rect r = n->rect;
    int npts = 0;
    float lo = 1e30f, hi = -1e30f;
    for (int k = 0; k < p->count; k++) {
        cui_series *s = &p->series[k];
        npts = CUI_MAX(npts, s->count);
        for (int i = 0; i < s->count; i++) {
            float v = s->values[i];
            if (p->bars && p->o.stacked) {
                float sum = 0;
                for (int j = 0; j < p->count; j++) if (i < p->series[j].count) sum += p->series[j].values[i];
                v = sum;
            }
            lo = CUI_MIN(lo, v); hi = CUI_MAX(hi, v);
        }
    }
    if (npts == 0) return;
    if (p->o.min != p->o.max) { lo = p->o.min; hi = p->o.max; }
    else {
        if (p->bars || (lo >= 0 && lo <= hi * 0.45f)) lo = CUI_MIN(lo, 0);
    }
    float step;
    if (p->o.min != p->o.max) {
        /* a fixed range keeps its ends: pick the nice step that divides it evenly, nearest four intervals */
        float range = hi - lo, best = 0, best_d = 1e9f;
        float mag = powf(10.0f, floorf(log10f(range)) - 1);
        static const float m[] = { 1, 2, 2.5f, 5 };
        for (int e = 0; e < 3; e++, mag *= 10)
            for (int j = 0; j < 4; j++) {
                float st = m[j] * mag, intervals = range / st;
                if (intervals < 2.5f || intervals > 6.5f || fabsf(intervals - roundf(intervals)) > 1e-3f) continue;
                if (fabsf(intervals - 4) < best_d) { best_d = fabsf(intervals - 4); best = st; }
            }
        if (best > 0) step = best;
        else cui__nice_range(lo, hi, 4, &lo, &hi, &step);
    } else {
        cui__nice_range(lo, hi, 4, &lo, &hi, &step);
    }
    float fs = ui->theme.text_xs + 0.5f;
    float axis_w = 0;
    for (float v = lo; v <= hi + step * 0.5f; v += step)
        axis_w = CUI_MAX(axis_w, cui__text_width(ui, CUI_FONT_BODY, fs, cui__fmt_value(ui, p->o.format, v), -1, true));
    float label_h = p->labels ? 22 : 4;
    cui_rect plot = { r.x + axis_w + 10, r.y + 6, r.w - axis_w - 12, r.h - label_h - 8 };
    if (plot.w <= 10 || plot.h <= 10) return;
    if (p->st) { p->st->plot_x = plot.x - r.x; p->st->plot_w = plot.w; }
    float fh = cui__font_line_height(ui, CUI_FONT_BODY, fs);
    /* grid */
    for (float v = lo; v <= hi + step * 0.5f; v += step) {
        float y = plot.y + plot.h - (v - lo) / (hi - lo) * plot.h;
        y = roundf(y * ui->scale) / ui->scale + 0.5f / ui->scale;
        if (!p->o.no_grid) {
            cui_rect gl = { plot.x, y - 0.5f, plot.w, 1 };
            cui_draw_rect(ui, gl, CUI_CORNERS(0), fabsf(v) < step * 0.01f ? CUI_C(ui, BORDER) : CUI_C(ui, BORDER_SUBTLE));
        }
        const char *lbl = cui__fmt_value(ui, p->o.format, v);
        float lw = cui__text_width(ui, CUI_FONT_BODY, fs, lbl, -1, true);
        cui__draw_text_line(ui, CUI_FONT_BODY, fs, plot.x - 10 - lw, y - fh * 0.5f, CUI_C(ui, TEXT_MUTED), lbl, -1, true);
    }
    /* x labels */
    float slot = p->bars ? plot.w / (float)npts : (npts > 1 ? plot.w / (float)(npts - 1) : plot.w);
    if (p->labels) {
        float last_end = -1e9f;
        for (int i = 0; i < npts && i < p->label_count; i++) {
            float cx = p->bars ? plot.x + slot * (i + 0.5f) : plot.x + slot * (float)i;
            float lw = cui__text_width(ui, CUI_FONT_BODY, fs, p->labels[i], -1, false);
            float x = cx - lw * 0.5f;
            if (!p->bars && i == 0) x = cx - CUI_MIN(lw * 0.5f, 4);
            if (!p->bars && i == npts - 1) x = cx - lw + CUI_MIN(lw * 0.5f, 4);
            if (x < last_end + 8) continue;
            cui__draw_text_line(ui, CUI_FONT_BODY, fs, x, plot.y + plot.h + 8, CUI_C(ui, TEXT_MUTED), p->labels[i], -1, false);
            last_end = x + lw;
        }
    }
    float intro = cui__clampf(p->intro, 0, 1);
    float base_y = plot.y + plot.h - (CUI_CLAMP(0, lo, hi) - lo) / (hi - lo) * plot.h;
    if (p->bars) {
        int ns = p->count;
        float group_w = slot * 0.72f;
        float bw = p->o.stacked ? CUI_MIN(group_w, 44) : CUI_MIN(group_w / (float)ns - 3, 28);
        if (bw < 2) bw = 2;
        for (int i = 0; i < npts; i++) {
            float cx = plot.x + slot * (i + 0.5f);
            float acc = 0;
            float total_w = p->o.stacked ? bw : bw * ns + 3 * (ns - 1);
            bool dim = p->hover >= 0 && p->hover != i;
            for (int k = 0; k < ns; k++) {
                cui_series *s = &p->series[k];
                if (i >= s->count) continue;
                float v = s->values[i] * intro;
                cui_color c = cui__series_color(ui, s, k);
                if (dim) c = cui_alpha(c, 1.0f - 0.55f * p->hover_t);
                float x = p->o.stacked ? cx - bw * 0.5f : cx - total_w * 0.5f + (bw + 3) * (float)k;
                float y0 = plot.y + plot.h - (acc - lo) / (hi - lo) * plot.h;
                float y1 = plot.y + plot.h - (acc + v - lo) / (hi - lo) * plot.h;
                if (!p->o.stacked) { y0 = base_y; y1 = plot.y + plot.h - (v - lo) / (hi - lo) * plot.h; }
                float top = CUI_MIN(y0, y1), h = fabsf(y1 - y0);
                bool is_top = !p->o.stacked || k == ns - 1;
                float rad = CUI_MIN(4.0f, bw * 0.3f);
                cui_corners cr = { is_top ? rad : 0, is_top ? rad : 0, 0, 0 };
                if (v < 0) { cr.tl = cr.tr = 0; cr.bl = cr.br = rad; }
                cui_rect br = { x, top, bw, h };
                cui_draw_rect(ui, br, cr, c);
                if (p->o.stacked) acc += s->values[i] * intro;
            }
        }
        if (p->hover >= 0 && p->hover < npts) {
            float cx = plot.x + slot * (p->hover + 0.5f);
            cui__chart_tooltip(ui, p, plot, cx + slot * 0.25f, plot.y + plot.h * 0.4f, p->hover);
        }
        return;
    }
    /* lines */
    cui_push_clip(ui, (cui_rect){ plot.x - 4, r.y, (plot.w + 8) * intro, r.h });
    for (int k = 0; k < p->count; k++) {
        cui_series *s = &p->series[k];
        if (s->count < 1) continue;
        cui_vec2 *pts = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)s->count);
        for (int i = 0; i < s->count; i++) {
            pts[i].x = plot.x + slot * (float)i;
            pts[i].y = plot.y + plot.h - (s->values[i] - lo) / (hi - lo) * plot.h;
        }
        cui_vec2 *line = pts;
        int ln = s->count;
        if (p->o.smooth && s->count > 2) cui__smooth_points(ui, pts, s->count, &line, &ln);
        cui_color c = cui__series_color(ui, s, k);
        /* with several series only the first is filled: overlapping fills tint each other into a third color */
        if (p->o.area && ln > 1 && k == 0) {
            cui_cvert *v = cui__frame_alloc(ui, sizeof(cui_cvert) * (size_t)ln * 2);
            uint32_t *idx = cui__frame_alloc(ui, sizeof(uint32_t) * (size_t)(ln - 1) * 6);
            float alpha_top = (p->count > 1 ? 0.16f : 0.2f) * (ui->mode == CUI_THEME_DARK ? 1.2f : 1.0f);
            for (int i = 0; i < ln; i++) {
                /* a flat tint: the area is a quantity, not a glow fading into the page */
                v[i * 2].x = line[i].x; v[i * 2].y = line[i].y; v[i * 2].c = cui_alpha(c, alpha_top * 0.6f);
                v[i * 2 + 1].x = line[i].x; v[i * 2 + 1].y = base_y; v[i * 2 + 1].c = cui_alpha(c, alpha_top * 0.6f);
            }
            for (int i = 0; i < ln - 1; i++) {
                uint32_t a = (uint32_t)i * 2;
                uint32_t q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
                memcpy(idx + i * 6, q, sizeof q);
            }
            cui__draw_triangles(ui, v, ln * 2, idx, (ln - 1) * 6);
        }
        cui_draw_polyline(ui, line, ln, 2.0f, c, false);
    }
    cui_pop_clip(ui);
    if (p->hover >= 0 && p->hover_t > 0.01f) {
        float hx = p->hx;
        cui_rect cl = { hx - 0.5f, plot.y, 1, plot.h };
        cui_draw_rect(ui, cl, CUI_CORNERS(0), cui_alpha(CUI_C(ui, BORDER_STRONG), p->hover_t));
        float ay = plot.y + plot.h;
        for (int k = 0; k < p->count; k++) {
            cui_series *s = &p->series[k];
            if (p->hover >= s->count) continue;
            float y = plot.y + plot.h - (s->values[p->hover] - lo) / (hi - lo) * plot.h;
            ay = CUI_MIN(ay, y);
            cui_vec2 c = { plot.x + slot * (float)p->hover, y };
            cui_draw_circle(ui, c, 5.5f, cui_alpha(CUI_C(ui, SURFACE), p->hover_t));
            cui_draw_circle(ui, c, 3.5f, cui_alpha(cui__series_color(ui, s, k), p->hover_t));
        }
        cui__chart_tooltip(ui, p, plot, hx, ay + 30, p->hover);
    }
}

static int cui__legend(cui_ctx *ui, const cui_series *series, int count) {
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 16;
    ui->nodes[row].flags |= CUI_NF_ROW_WRAP;   /* series names wrap rather than truncate */
    cui__open(ui, row);
    for (int k = 0; k < count; k++) {
        int item = cui__node_new(ui, 0);
        ui->nodes[item].dir = CUI_ROW; ui->nodes[item].gap = 6;
        cui__open(ui, item);
        int sw = cui__leaf(ui, 0, CUI_PX(8), CUI_PX(8));
        ui->nodes[sw].bg = cui__series_color(ui, &series[k], k);
        ui->nodes[sw].radius = CUI_CORNERS(2.5f);
        int t = cui__text_node(ui, series[k].name ? series[k].name : "", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t].line_height = 1.3f;
        cui__close(ui);
    }
    cui__close(ui);
    return row;
}

static void cui__xy_chart(cui_ctx *ui, const char *id_str, const cui_series *series, int count, const cui_chart_opts *o, bool bars) {
    cui_chart_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id plot_id = id ^ 0x9107u;
    cui__chart_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, plot_id, 0);
    cui_rect pr = cui__prev_rect(ui, plot_id);
    int npts = 0;
    for (int k = 0; k < count; k++) npts = CUI_MAX(npts, series[k].count);
    /* hover index from the mouse position, using the same geometry as paint */
    int hover = -1;
    if (r.hovered && npts > 0 && pr.w > 0) {
        float px = pr.x + (st->plot_w > 0 ? st->plot_x : 40), pw = st->plot_w > 0 ? st->plot_w : pr.w - 42;
        float rel = (ui->mx - px) / pw;
        if (bars) hover = (int)floorf(rel * (float)npts);
        else hover = (int)floorf(rel * (float)(npts - 1) + 0.5f);
        hover = CUI_CLAMP(hover, 0, npts - 1);
    }
    cui__spring(ui, &st->intro, 1.0f, 0.9f, 0.0f);
    cui__tween(ui, &st->hover_t, hover >= 0 ? 1.0f : 0.0f, hover >= 0 ? 0.12f : 0.2f);
    if (hover >= 0) st->hover = hover;
    int col = cui__node_new(ui, id);
    ui->nodes[col].gap = 12;
    ui->nodes[col].w = o->width.kind ? o->width : CUI_FILL;
    cui__open(ui, col);
    if (!o->no_legend && count > 1) cui__legend(ui, series, count);
    int plot = cui__leaf(ui, plot_id, CUI_FILL, CUI_PX(o->height > 0 ? o->height : 220));
    ui->nodes[plot].self_align = CUI_ALIGN_STRETCH;
    cui__mark_interactive(ui, plot, 0);
    cui__chart_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->series = cui__frame_alloc(ui, sizeof(cui_series) * (size_t)(count > 0 ? count : 1));
    for (int k = 0; k < count; k++) {
        p->series[k] = series[k];
        float *vals = cui__frame_alloc(ui, sizeof(float) * (size_t)(series[k].count > 0 ? series[k].count : 1));
        if (series[k].count > 0) memcpy(vals, series[k].values, sizeof(float) * (size_t)series[k].count);
        p->series[k].values = vals;
        p->series[k].name = series[k].name ? cui__frame_strdup(ui, series[k].name, -1) : NULL;
    }
    p->count = count;
    p->label_count = o->label_count > 0 ? o->label_count : npts;
    p->labels = NULL;
    if (o->labels) {
        const char **copy = cui__frame_alloc(ui, sizeof(char *) * (size_t)(p->label_count > 0 ? p->label_count : 1));
        for (int k = 0; k < p->label_count; k++) copy[k] = o->labels[k] ? cui__frame_strdup(ui, o->labels[k], -1) : "";
        p->labels = copy;
    }
    p->o = *o;
    p->o.format = o->format ? cui__frame_strdup(ui, o->format, -1) : NULL;
    p->o.labels = NULL;
    p->intro = st->intro.x;
    p->hover = st->hover_t > 0.01f ? st->hover : -1;
    p->hover_t = st->hover_t;
    p->bars = bars;
    p->st = st;
    if (!bars && pr.w > 0 && npts > 1) {
        /* glide the crosshair between points */
        float px = pr.x + (st->plot_w > 0 ? st->plot_x : 40), pw = st->plot_w > 0 ? st->plot_w : pr.w - 42;
        float target = px + pw * (float)st->hover / (float)(npts - 1);
        if (!st->init || st->hover_t < 0.05f) { st->hx.x = target; st->init = true; }
        cui__spring(ui, &st->hx, target, 0.22f, 0.0f);
        p->hx = st->hx.x;
    }
    ui->nodes[plot].paint = cui__paint_xy_chart;
    ui->nodes[plot].paint_data = p;
    cui__close(ui);
    ui->last_id = plot_id;
}

void cui_line_chart_ex(cui_ctx *ui, const char *id, const cui_series *series, int count, const cui_chart_opts *o) {
    cui__xy_chart(ui, id, series, count, o, false);
}

void cui_bar_chart_ex(cui_ctx *ui, const char *id, const cui_series *series, int count, const cui_chart_opts *o) {
    cui__xy_chart(ui, id, series, count, o, true);
}

/* ---- donut ---- */

typedef struct { float *values; int count; float intro; int hover; float hover_t; float total; const char *format; const char *const *labels; } cui__donut_paint;

static void cui__paint_donut(cui_ctx *ui, cui_node *n, void *data) {
    cui__donut_paint *p = data;
    cui_rect r = n->rect;
    float size = CUI_MIN(r.w, r.h);
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    float thick = size * 0.13f;
    float radius = size * 0.5f - thick * 0.5f - 4;
    float total = p->total > 0 ? p->total : 1;
    float a = -CUI_PI * 0.5f;
    float sweep_total = 2 * CUI_PI * cui__clampf(p->intro, 0, 1);
    float gap = 2.2f / radius;
    /* the track only shows where the intro has not reached yet, so the gaps between segments are the card itself */
    if (sweep_total < 2 * CUI_PI - 0.001f) cui_draw_arc(ui, c, radius, a + sweep_total, a + 2 * CUI_PI, thick, cui_alpha(CUI_C(ui, CONTROL_TRACK), 0.5f));
    for (int k = 0; k < p->count; k++) {
        float sw = p->values[k] / total * sweep_total;
        if (sw <= 0) continue;
        float a0 = a + gap * 0.5f, a1 = a + sw - gap * 0.5f;
        if (a1 > a0) {
            float th = thick + (p->hover == k ? 5.0f * p->hover_t : 0);
            cui_color col = ui->theme.colors[CUI_TOKEN_SERIES_1 + (k % 6)];
            if (p->hover >= 0 && p->hover != k) col = cui_alpha(col, 1.0f - 0.45f * p->hover_t);
            cui_draw_arc(ui, c, radius, a0, a1, th, col);
        }
        a += sw;
    }
    /* percentage data shows the leading (or hovered) segment instead of a total */
    bool pct = p->format && strstr(p->format, "%%");
    int show = p->hover >= 0 && p->hover_t > 0.5f ? p->hover : -1;
    if (show < 0 && pct) {
        show = 0;
        for (int k = 1; k < p->count; k++) if (p->values[k] > p->values[show]) show = k;
    }
    const char *val = cui__fmt_value(ui, p->format, show >= 0 ? p->values[show] : p->total);
    float vs = CUI_MAX(14.0f, size * 0.16f);
    float vw = cui__text_width(ui, CUI_FONT_DISPLAY, vs, val, -1, true);
    float vh = cui__font_line_height(ui, CUI_FONT_DISPLAY, vs);
    cui__draw_text_line(ui, CUI_FONT_DISPLAY, vs, c.x - vw * 0.5f, c.y - vh * 0.62f, CUI_C(ui, FOREGROUND), val, -1, true);
    const char *sub = "Total";
    if (show >= 0) {
        if (p->labels && p->labels[show]) sub = p->labels[show];
        else sub = cui__frame_printf(ui, "%.0f%%", (double)(p->values[show] / total * 100));
    }
    float sw2 = cui__text_width(ui, CUI_FONT_BODY, ui->theme.text_sm, sub, -1, true);
    cui__draw_text_line(ui, CUI_FONT_BODY, ui->theme.text_sm, c.x - sw2 * 0.5f, c.y + vh * 0.42f, CUI_C(ui, TEXT_SECONDARY), sub, -1, true);
}

void cui_donut_chart_ex(cui_ctx *ui, const char *id_str, const float *values, const char *const *labels, int count, const cui_chart_opts *o) {
    cui_chart_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id did = id ^ 0xD0u;
    cui__chart_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, did, 0);
    cui_rect pr = cui__prev_rect(ui, did);
    float total = 0;
    for (int k = 0; k < count; k++) total += values[k] > 0 ? values[k] : 0;
    int hover = -1;
    if (r.hovered && pr.w > 0 && total > 0) {
        float cx = pr.x + pr.w * 0.5f, cy = pr.y + pr.h * 0.5f;
        float dx = ui->mx - cx, dy = ui->my - cy;
        float dist = sqrtf(dx * dx + dy * dy), size = CUI_MIN(pr.w, pr.h);
        if (dist > size * 0.25f && dist < size * 0.5f) {
            float ang = atan2f(dy, dx) + CUI_PI * 0.5f;
            if (ang < 0) ang += 2 * CUI_PI;
            float acc = 0;
            for (int k = 0; k < count; k++) {
                acc += values[k] / total * 2 * CUI_PI;
                if (ang <= acc) { hover = k; break; }
            }
        }
    }
    cui__spring(ui, &st->intro, 1.0f, 1.0f, 0.0f);
    cui__tween(ui, &st->hover_t, hover >= 0 ? 1.0f : 0.0f, CUI__T_HOVER);
    if (hover >= 0) st->hover = hover;
    float size = o->height > 0 ? o->height : 180;
    /* The row takes the width it is given; the legend sits beside the donut,
     * and moves below it only when last frame's width left no room for it. */
    bool legend = !o->no_legend && labels;
    cui_rect prev = cui__prev_rect(ui, id);
    bool stack = false;
    float fs = cui__fs(ui, ui->theme.text_body);
    float legend_w = 0, full = size;
    {
        if (legend) {
            float ml = 0, mv = 0;
            for (int k = 0; k < count; k++) {
                ml = CUI_MAX(ml, cui__text_width(ui, CUI_FONT_BODY, fs, labels[k] ? labels[k] : "", -1, false));
                const char *v = cui__frame_printf(ui, "%.0f%%", total > 0 ? (double)(values[k] / total * 100) : 0.0);
                mv = CUI_MAX(mv, cui__text_width(ui, CUI_FONT_MEDIUM, fs, v, -1, true));
            }
            legend_w = 8 + 8 + ml + 8 + 16 + 8 + mv + 2;
        }
    }
    if (prev.w > 0) {
        /* narrower: the donut shrinks first, then the legend moves below it */
        float room = legend ? prev.w - 24 - legend_w : prev.w;
        if (room >= 120) size = CUI_MIN(size, room);
        else if (legend) { stack = true; size = CUI_MIN(size, CUI_MAX(120.0f, prev.w)); }
        else size = CUI_MAX(80.0f, room);
    }
    int row = cui__node_new(ui, id);
    ui->nodes[row].dir = stack ? CUI_COLUMN : CUI_ROW; ui->nodes[row].gap = stack ? 16 : 24;
    ui->nodes[row].align = stack ? CUI_ALIGN_START : CUI_ALIGN_CENTER;
    /* it always asks for the side by side width, so a container that fits its
     * content offers that room again as soon as it has it */
    cui__soft_width(&ui->nodes[row], o->width, legend ? full + 24 + legend_w : full);
    if (!o->width.kind) { ui->nodes[row].flags |= CUI_NF_ADAPTIVE; ui->nodes[row].min_w = legend ? CUI_MAX(120.0f, legend_w) : 80; }
    cui__open(ui, row);
    int dn = cui__leaf(ui, did, CUI_PX(size), CUI_PX(size));
    cui__mark_interactive(ui, dn, 0);
    cui__donut_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->values = cui__frame_alloc(ui, sizeof(float) * (size_t)(count > 0 ? count : 1));
    for (int k = 0; k < count; k++) p->values[k] = values[k] > 0 ? values[k] : 0;
    p->count = count; p->intro = st->intro.x; p->total = total;
    p->format = o->format ? cui__frame_strdup(ui, o->format, -1) : NULL;
    p->hover = st->hover_t > 0.01f ? st->hover : -1;
    p->hover_t = st->hover_t;
    p->labels = NULL;
    if (labels) {
        const char **copy = cui__frame_alloc(ui, sizeof(char *) * (size_t)(count > 0 ? count : 1));
        for (int k = 0; k < count; k++) copy[k] = labels[k] ? cui__frame_strdup(ui, labels[k], -1) : "";
        p->labels = copy;
    }
    ui->nodes[dn].paint = cui__paint_donut;
    ui->nodes[dn].paint_data = p;
    if (legend) {
        int col = cui__node_new(ui, 0);
        ui->nodes[col].gap = 10;
        cui__open(ui, col);
        for (int k = 0; k < count; k++) {
            int it = cui__node_new(ui, 0);
            /* rows as wide as the widest one, values pushed to its right edge: the
             * percentages line up without padding the legend to a fixed width */
            ui->nodes[it].dir = CUI_ROW; ui->nodes[it].gap = 8; ui->nodes[it].w = CUI_FILL;
            ui->nodes[it].opacity = (p->hover >= 0 && p->hover != k) ? 1.0f - 0.4f * p->hover_t : 1.0f;
            cui__open(ui, it);
            int sw = cui__leaf(ui, 0, CUI_PX(8), CUI_PX(8));
            ui->nodes[sw].bg = ui->theme.colors[CUI_TOKEN_SERIES_1 + (k % 6)];
            ui->nodes[sw].radius = CUI_CORNERS(2.5f);
            int t = cui__text_node(ui, labels[k], -1, CUI_FONT_BODY, fs, CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[t].line_height = 1.3f;
            ui->nodes[t].flags |= CUI_NF_KEEP_W;   /* a legend never truncates its names */
            int gapn = cui__leaf(ui, 0, CUI_GROW(1), CUI_PX(1));
            ui->nodes[gapn].min_w = 16;
            int v = cui__text_node(ui, cui__frame_printf(ui, "%.0f%%", total > 0 ? (double)(values[k] / total * 100) : 0.0), -1,
                                   CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
            ui->nodes[v].line_height = 1.3f;
            ui->nodes[v].flags |= CUI_NF_TABULAR;
            cui__close(ui);
        }
        cui__close(ui);
    }
    cui__close(ui);
    ui->last_id = did;
}

/* ---- sparkline ---- */

typedef struct { float *values; int count; cui_color color; } cui__spark_paint;

static void cui__paint_spark(cui_ctx *ui, cui_node *n, void *data) {
    cui__spark_paint *p = data;
    cui_rect r = n->rect;
    if (p->count < 2) return;
    float lo = 1e30f, hi = -1e30f;
    for (int i = 0; i < p->count; i++) { lo = CUI_MIN(lo, p->values[i]); hi = CUI_MAX(hi, p->values[i]); }
    if (hi <= lo) hi = lo + 1;
    cui_vec2 *pts = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)p->count);
    for (int i = 0; i < p->count; i++) {
        pts[i].x = r.x + 2 + (r.w - 4) * (float)i / (float)(p->count - 1);
        pts[i].y = r.y + 3 + (r.h - 6) * (1 - (p->values[i] - lo) / (hi - lo));
    }
    /* a line and where it ends: no fill under it, the shape is the information */
    cui_draw_polyline(ui, pts, p->count, 1.75f, p->color, false);
    cui_draw_circle(ui, pts[p->count - 1], 3, p->color);
}

void cui_sparkline(cui_ctx *ui, const float *values, int count, cui_size width, float height, cui_color color) {
    int i = cui__leaf(ui, 0, width.kind ? width : CUI_PX(120), CUI_PX(height > 0 ? height : 32));
    cui__spark_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->values = cui__frame_alloc(ui, sizeof(float) * (size_t)(count > 0 ? count : 1));
    if (count > 0) memcpy(p->values, values, sizeof(float) * (size_t)count);
    p->count = count;
    p->color = cui__color_set(color) ? cui_color_of(ui, color) : ui->theme.colors[CUI_TOKEN_SERIES_1];
    ui->nodes[i].paint = cui__paint_spark;
    ui->nodes[i].paint_data = p;
    if (width.kind == CUI_SIZE_GROW) ui->nodes[i].self_align = CUI_ALIGN_STRETCH;
}

/* ---- gauge ---- */

typedef struct { float value; const char *label; } cui__gauge_paint;

static void cui__paint_gauge(cui_ctx *ui, cui_node *n, void *data) {
    cui__gauge_paint *p = data;
    cui_rect r = n->rect;
    float size = CUI_MIN(r.w, r.h);
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    float thick = size * 0.09f, rad = size * 0.5f - thick;
    float a0 = CUI_PI * 0.75f, a1 = CUI_PI * 2.25f;
    cui_draw_arc(ui, c, rad, a0, a1, thick, CUI_C(ui, CONTROL_TRACK));
    float v = cui__clampf(p->value, 0, 1);
    cui_color col = v > 0.9f ? CUI_C(ui, DANGER) : v > 0.75f ? CUI_C(ui, WARNING) : CUI_C(ui, CONTROL_ON);
    if (v > 0.001f) cui_draw_arc(ui, c, rad, a0, a0 + (a1 - a0) * v, thick, col);
    /* round caps */
    cui_vec2 e0 = { c.x + cosf(a0) * rad, c.y + sinf(a0) * rad };
    cui_draw_circle(ui, e0, thick * 0.5f, v > 0.001f ? col : CUI_C(ui, CONTROL_TRACK));
    cui_vec2 e1 = { c.x + cosf(a0 + (a1 - a0) * v) * rad, c.y + sinf(a0 + (a1 - a0) * v) * rad };
    if (v > 0.001f) cui_draw_circle(ui, e1, thick * 0.5f, col);
    const char *vs = cui__frame_printf(ui, "%.0f%%", (double)(v * 100));
    float fs = size * 0.2f;
    float w = cui__text_width(ui, CUI_FONT_DISPLAY, fs, vs, -1, true), h = cui__font_line_height(ui, CUI_FONT_DISPLAY, fs);
    cui__draw_text_line(ui, CUI_FONT_DISPLAY, fs, c.x - w * 0.5f, c.y - h * 0.55f, CUI_C(ui, FOREGROUND), vs, -1, true);
    if (p->label) {
        float lw = cui__text_width(ui, CUI_FONT_BODY, ui->theme.text_sm, p->label, -1, false);
        cui__draw_text_line(ui, CUI_FONT_BODY, ui->theme.text_sm, c.x - lw * 0.5f, c.y + h * 0.45f, CUI_C(ui, TEXT_SECONDARY), p->label, -1, false);
    }
}

void cui_gauge(cui_ctx *ui, const char *label, float value, float size) {
    if (size <= 0) size = 140;
    cui_id id = cui_make_id(ui, label ? label : "gauge");
    float shown = cui_animate(ui, id, cui__clampf(value, 0, 1));
    int i = cui__leaf(ui, 0, CUI_PX(size), CUI_PX(size));
    ui->nodes[i].self_align = CUI__ALIGN_NOSTRETCH;
    cui__gauge_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->value = shown;
    p->label = label ? cui__frame_strdup(ui, label, -1) : NULL;
    ui->nodes[i].paint = cui__paint_gauge;
    ui->nodes[i].paint_data = p;
}
