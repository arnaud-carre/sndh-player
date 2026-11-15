/* Hex viewer: offsets, bytes in hex and as ASCII, for binary tools. The view
 * scrolls by itself, in rows kept as a double, so only the rows in sight are
 * built and very large buffers stay exact. */
#include "cui_internal.h"

#define cui__fs cui__fsz

#define CUI__HEX_SALT  0x4E3A11u
#define CUI__HEX_BAR   12.0f            /* scrollbar zone along the right edge */
#define CUI__HEX_INSET 4.0f             /* above the first row and below the last */
#define CUI__HEX_COPY_MAX ((size_t)1 << 22)   /* bytes copied at most (4 MiB) */

typedef struct {
    double pos, target;                 /* top of the view, in rows */
    cui_hex_selection own;              /* the selection when the caller passes none */
    size_t goto_off;
    size_t nibble_at;                   /* the byte whose high digit was typed */
    double last_activity;               /* wheel or drag, shows the scrollbar */
    float bar_t, bar_grab;
    cui_id tip;                         /* tooltip anchor last asked for */
    bool goto_pending, dragging, bar_drag, nibble;
} cui__hex_state;

typedef struct {
    int bpr, digits;
    bool ascii;
    float cw, pad, x_off, x_hex, gg, x_ascii, need;
} cui__hex_geom;

static float cui__hex_cell_x(const cui__hex_geom *g, int i) { return g->x_hex + (float)i * 3.0f * g->cw + (float)(i / 8) * g->gg; }
static float cui__hex_hex_end(const cui__hex_geom *g) { return cui__hex_cell_x(g, g->bpr - 1) + 2.0f * g->cw; }

static cui__hex_geom cui__hex_make(float cw, int digits, int bpr, bool ascii, bool tight) {
    cui__hex_geom g;
    memset(&g, 0, sizeof g);
    g.bpr = bpr; g.digits = digits; g.ascii = ascii; g.cw = cw;
    g.pad = tight ? 8 : 12;
    g.gg = cw;                          /* the small gap after every 8 bytes */
    g.x_off = g.pad;
    g.x_hex = g.pad + (float)digits * cw + (tight ? 1.5f : 2.0f) * cw;
    float end = cui__hex_hex_end(&g);
    g.x_ascii = end + 2.0f * cw;
    g.need = (ascii ? g.x_ascii + (float)bpr * cw : end) + g.pad;
    return g;
}

/* The widest layout that fits w: fewer bytes per row first (down to 8), then
 * no ASCII column, then tighter margins, then 4 bytes per row. */
static cui__hex_geom cui__hex_fit(float cw, int digits, int bpr, bool ascii, float w) {
    cui__hex_geom g = cui__hex_make(cw, digits, bpr, ascii, false);
    if (w <= 0) return g;
    while (g.need > w && bpr > 8) { bpr /= 2; g = cui__hex_make(cw, digits, bpr, ascii, false); }
    if (g.need <= w) return g;
    if (ascii) { g = cui__hex_make(cw, digits, 8, false, false); if (g.need <= w) return g; }
    g = cui__hex_make(cw, digits, 8, false, true);
    if (g.need <= w) return g;
    return cui__hex_make(cw, digits, 4, false, true);
}

static void cui__hex_digits(char *out, uint64_t v, int n) {
    static const char hx[] = "0123456789ABCDEF";
    for (int i = n - 1; i >= 0; i--) { out[i] = hx[v & 15u]; v >>= 4; }
    out[n] = 0;
}

static bool cui__hex_printable(uint8_t c) { return c >= 0x20 && c < 0x7F; }

/* anchor: the fixed end of the selection (the cursor is the moving end) */
static size_t cui__hex_anchor(const cui_hex_selection *s) {
    if (s->length == 0) return s->cursor;
    size_t last = s->start + s->length - 1;
    return s->cursor == s->start && s->length > 1 ? last : s->start;
}
static void cui__hex_select(cui_hex_selection *s, size_t anchor, size_t cursor) {
    s->start = CUI_MIN(anchor, cursor);
    s->length = CUI_MAX(anchor, cursor) - s->start + 1;
    s->cursor = cursor;
}

/* Byte at x (relative to the view) in row `row`. With clamp, a point beside
 * the columns or past the end picks the nearest byte; otherwise -1. */
static int64_t cui__hex_hit(const cui__hex_geom *g, float x, size_t row, size_t size, bool clamp) {
    int col = -1;
    float cw = g->cw, hex_end = cui__hex_hex_end(g);
    if (x >= g->x_hex - cw * 0.5f && x < hex_end + cw * 0.5f) {
        for (col = 0; col < g->bpr - 1; col++) {
            float next = cui__hex_cell_x(g, col + 1);
            if (x < (cui__hex_cell_x(g, col) + 2.0f * cw + next) * 0.5f) break;
        }
    } else if (g->ascii && x >= g->x_ascii - cw * 0.5f && x < g->x_ascii + (float)g->bpr * cw + cw * 0.5f) {
        col = (int)floorf((x - g->x_ascii) / cw);
        col = CUI_CLAMP(col, 0, g->bpr - 1);
    } else if (clamp) {
        if (x < g->x_hex) col = 0;
        else if (g->ascii && x >= (hex_end + g->x_ascii) * 0.5f && x < g->x_ascii) col = 0;
        else col = g->bpr - 1;
        if (g->ascii && x >= g->x_ascii + (float)g->bpr * cw) col = g->bpr - 1;
    }
    if (col < 0) return -1;
    size_t bpr = (size_t)g->bpr;
    if (row > (SIZE_MAX - (size_t)col) / bpr) return clamp && size ? (int64_t)(size - 1) : -1;
    size_t b = row * bpr + (size_t)col;
    if (b >= size) return clamp && size ? (int64_t)(size - 1) : -1;
    return (int64_t)b;
}

static void cui__hex_copy(cui_ctx *ui, const uint8_t *bytes, size_t start, size_t len, bool ascii) {
    if (!ui->platform.set_clipboard || len == 0) return;
    if (len > CUI__HEX_COPY_MAX) len = CUI__HEX_COPY_MAX;
    size_t cap = ascii ? len + 1 : len * 3;
    char *out = cui__alloc(ui, cap);
    if (!out) return;
    static const char hx[] = "0123456789ABCDEF";
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = bytes[start + i];
        if (ascii) out[n++] = cui__hex_printable(c) ? (char)c : '.';
        else {
            if (i) out[n++] = ' ';
            out[n++] = hx[c >> 4];
            out[n++] = hx[c & 15];
        }
    }
    out[n] = 0;
    ui->platform.set_clipboard(ui->platform.user, out);
    cui__free(ui, out);
}

/* ---- painting ---- */

typedef struct { size_t a, b; cui_color tint, text; } cui__hex_mark;   /* bytes [a, b) */

typedef struct {
    cui__hex_geom g;
    int bpr_req;
    bool ascii_req;
    cui_id sid;
    float size, row_h, header_h, bar_t;
    double pos, max_top, view_rows;
    size_t size_bytes, first_row, row_count;   /* rows [first_row, first_row + row_count) */
    const uint8_t *bytes;                      /* a copy of those rows */
    uint64_t base;
    bool header, empty, has_sel, bar_big, show_cursor, nibble;
    size_t sel_lo, sel_hi, cursor;
    int64_t hover;
    const cui__hex_mark *marks; int nmarks;
    cui_color fg, muted, secondary, sel, hover_c, bar, line;
} cui__hex_paint;

/* one tinted run per row: bytes a..b (columns) in both columns */
static void cui__hex_run(cui_ctx *ui, const cui__hex_paint *p, cui_rect r, float y, int a, int b, cui_color c) {
    const cui__hex_geom *g = &p->g;
    float h = p->row_h - 2;
    float x0 = cui__hex_cell_x(g, a) - g->cw * 0.4f, x1 = cui__hex_cell_x(g, b) + 2.0f * g->cw + g->cw * 0.4f;
    cui_draw_rect(ui, (cui_rect){ r.x + x0, y + 1, x1 - x0, h }, CUI_CORNERS(4), c);
    if (g->ascii) {
        float a0 = g->x_ascii + (float)a * g->cw - 1, a1 = g->x_ascii + (float)(b + 1) * g->cw + 1;
        cui_draw_rect(ui, (cui_rect){ r.x + a0, y + 1, a1 - a0, h }, CUI_CORNERS(4), c);
    }
}

static void cui__hex_cell_rects(const cui__hex_paint *p, cui_rect r, float y, int col, cui_rect *hex, cui_rect *asc) {
    const cui__hex_geom *g = &p->g;
    *hex = (cui_rect){ r.x + cui__hex_cell_x(g, col) - 3, y + 1, 2.0f * g->cw + 6, p->row_h - 2 };
    *asc = (cui_rect){ r.x + g->x_ascii + (float)col * g->cw - 1, y + 1, g->cw + 2, p->row_h - 2 };
}

static void cui__paint_hex(cui_ctx *ui, cui_node *n, void *data) {
    cui__hex_paint *p = data;
    cui_rect r = n->rect;
    const cui__hex_geom *g = &p->g;
    /* the wheel comes here while the pointer is over the view and it can scroll */
    {
        cui_rect clip = ui->dl.clip_stack[ui->dl.clip_depth - 1];
        float s = ui->scale;
        cui_rect cp = { clip.x / s, clip.y / s, clip.w / s, clip.h / s };
        cui_scroll_state *ss = cui_state(ui, p->sid, sizeof *ss);
        ss->view_w = r.w; ss->view_h = r.h;
        ss->content_w = r.w;
        ss->content_h = p->max_top > 0 ? r.h + 1e6f : r.h;
        cui__paint_add_hit(ui, p->sid, cui__rect_intersect(r, cp), CUI_NF_SCROLL_Y);
    }
    if (p->empty) {
        const char *msg = "No data";
        float fs = cui__fs(ui, ui->theme.text_body);
        float w = cui__text_width(ui, CUI_FONT_BODY, fs, msg, -1, false);
        float fh = cui__font_line_height(ui, CUI_FONT_BODY, fs);
        cui__draw_text_line(ui, CUI_FONT_BODY, fs, r.x + (r.w - w) * 0.5f, r.y + (r.h - fh) * 0.5f, p->muted, msg, -1, false);
        return;
    }
    /* laid out for last frame's width; ask again when this one needs another layout */
    cui__hex_geom now = cui__hex_fit(g->cw, g->digits, p->bpr_req, p->ascii_req, r.w);
    if (now.bpr != g->bpr || now.ascii != g->ascii) cui_request_redraw(ui);
    float size = p->size, cw = g->cw;
    float fh = cui__font_line_height(ui, CUI_FONT_MONO, size);
    float ty = (p->row_h - fh) * 0.5f;
    int hover_col = p->hover >= 0 ? (int)((size_t)p->hover % (size_t)g->bpr) : -1;
    size_t hover_row = p->hover >= 0 ? (size_t)p->hover / (size_t)g->bpr : SIZE_MAX;
    char buf[24];
    if (p->header) {
        for (int i = 0; i < g->bpr; i++) {
            cui__hex_digits(buf, (uint64_t)i, 2);
            cui_color c = i == hover_col ? p->secondary : p->muted;
            cui__draw_text_line(ui, CUI_FONT_MONO, size, r.x + cui__hex_cell_x(g, i), r.y + ty, c, buf, 2, true);
            if (g->ascii) {
                cui__hex_digits(buf, (uint64_t)i, 1);
                cui__draw_text_line(ui, CUI_FONT_MONO, size, r.x + g->x_ascii + (float)i * cw, r.y + ty, c, buf, 1, true);
            }
        }
        cui_draw_rect(ui, (cui_rect){ r.x, r.y + p->header_h - 1, r.w, 1 }, CUI_CORNERS(0), p->line);
    }
    cui_rect area = { r.x, r.y + p->header_h, r.w, r.h - p->header_h };
    cui_push_clip(ui, area);
    for (size_t k = 0; k < p->row_count; k++) {
        size_t row = p->first_row + k;
        size_t b0 = row * (size_t)g->bpr;
        int ncols = (int)CUI_MIN((size_t)g->bpr, p->size_bytes - b0);
        float y = area.y + CUI__HEX_INSET + (float)((double)row - p->pos) * p->row_h;
        size_t b1 = b0 + (size_t)ncols - 1;
        /* highlighted ranges, then the selection over them */
        for (int m = 0; m < p->nmarks; m++) {
            const cui__hex_mark *mk = &p->marks[m];
            if (mk->b <= b0 || mk->a > b1) continue;
            int a = (int)(CUI_MAX(mk->a, b0) - b0), b = (int)(CUI_MIN(mk->b - 1, b1) - b0);
            cui__hex_run(ui, p, r, y, a, b, mk->tint);
        }
        if (p->has_sel && p->sel_hi >= b0 && p->sel_lo <= b1) {
            int a = (int)(CUI_MAX(p->sel_lo, b0) - b0), b = (int)(CUI_MIN(p->sel_hi, b1) - b0);
            cui__hex_run(ui, p, r, y, a, b, p->sel);
        }
        cui_rect hr, ar;
        if (row == hover_row) {
            cui__hex_cell_rects(p, r, y, hover_col, &hr, &ar);
            cui_draw_rect(ui, hr, CUI_CORNERS(4), p->hover_c);
            if (g->ascii) cui_draw_rect(ui, ar, CUI_CORNERS(3), p->hover_c);
        }
        if (p->show_cursor && p->cursor >= b0 && p->cursor <= b1) {
            int col = (int)(p->cursor - b0);
            cui__hex_cell_rects(p, r, y, col, &hr, &ar);
            cui_draw_rect_border(ui, hr, CUI_CORNERS(4), CUI_TRANSPARENT, p->bar, 1.5f);
            if (g->ascii) cui_draw_rect_border(ui, ar, CUI_CORNERS(3), CUI_TRANSPARENT, p->bar, 1.5f);
            if (p->nibble)   /* the high digit is typed: a block over the low one, which is next */
                cui_draw_rect(ui, (cui_rect){ r.x + cui__hex_cell_x(g, col) + cw, y + 4, cw, p->row_h - 8 }, CUI_CORNERS(2), cui_alpha(p->bar, 0.28f));
        }
        /* text */
        cui__hex_digits(buf, p->base + (uint64_t)b0, g->digits);
        bool here = row == hover_row || (p->show_cursor && p->cursor >= b0 && p->cursor <= b1);
        cui__draw_text_line(ui, CUI_FONT_MONO, size, r.x + g->x_off, y + ty, here ? p->secondary : p->muted, buf, g->digits, true);
        const uint8_t *src = p->bytes + k * (size_t)g->bpr;
        for (int i = 0; i < ncols; i++) {
            uint8_t c = src[i];
            size_t at = b0 + (size_t)i;
            cui_color tc = p->fg;
            for (int m = p->nmarks - 1; m >= 0; m--)
                if (at >= p->marks[m].a && at < p->marks[m].b) { tc = p->marks[m].text; break; }
            /* zero bytes and dots step back, less so on a selection or under the pointer */
            cui_color quiet = p->muted;
            if ((p->has_sel && at >= p->sel_lo && at <= p->sel_hi) || (int64_t)at == p->hover) { tc = p->fg; quiet = p->secondary; }
            cui__hex_digits(buf, c, 2);
            cui__draw_text_line(ui, CUI_FONT_MONO, size, r.x + cui__hex_cell_x(g, i), y + ty, c ? tc : quiet, buf, 2, true);
            if (g->ascii) {
                bool pr = cui__hex_printable(c);
                buf[0] = pr ? (char)c : '.';
                cui__draw_text_line(ui, CUI_FONT_MONO, size, r.x + g->x_ascii + (float)i * cw, y + ty, pr ? tc : quiet, buf, 1, true);
            }
        }
    }
    cui_pop_clip(ui);
    if (p->max_top > 0 && p->bar_t > 0.01f) {
        float track_h = area.h - 6;
        double total = p->view_rows + p->max_top;
        float thumb_h = CUI_MAX(28.0f, track_h * (float)(p->view_rows / total));
        float y = area.y + 3 + (float)(p->pos / p->max_top) * (track_h - thumb_h);
        float w = p->bar_big ? 8 : 6;
        cui_color c = cui_alpha(p->bar, (p->bar_big ? 0.38f : 0.26f) * p->bar_t);
        cui_draw_rect(ui, (cui_rect){ r.x + r.w - w - 3, y, w, thumb_h }, CUI_CORNERS(w * 0.5f), c);
    }
}

/* ---- the view ---- */

void cui_hex_view_goto(cui_ctx *ui, const char *id_str, size_t offset) {
    cui__hex_state *s = cui_state(ui, cui_make_id(ui, id_str) ^ CUI__HEX_SALT, sizeof *s);
    s->goto_pending = true;
    s->goto_off = offset;
    ui->animating = true;
}

static int cui__hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

cui_hex_result cui_hex_view_ex(cui_ctx *ui, const char *id_str, const uint8_t *data, size_t size, cui_hex_selection *sel, const cui_hex_view_opts *o) {
    cui_hex_view_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_hex_result res;
    memset(&res, 0, sizeof res);
    res.hovered = -1;
    const uint8_t *bytes = o->edit ? o->edit : data;
    if (!bytes) size = 0;
    cui_id id = cui_make_id(ui, id_str);
    cui_id lid = id ^ 0x4E5B0D1u, sid = id ^ 0x4E5C011u;
    cui__hex_state *s = cui_state(ui, id ^ CUI__HEX_SALT, sizeof *s);
    if (!sel) sel = &s->own;
    /* keep the caller's selection inside the data */
    if (size == 0) memset(sel, 0, sizeof *sel);
    else {
        if (sel->cursor >= size) sel->cursor = size - 1;
        if (sel->start >= size) sel->length = 0;
        else if (sel->length > size - sel->start) sel->length = size - sel->start;
    }
    cui_hex_selection before = *sel;

    float fsize = cui__fs(ui, o->size > 0 ? o->size : 12);
    float row_h = roundf(fsize * 1.7f);
    float cw = cui__text_width(ui, CUI_FONT_MONO, fsize, "0000000000000000", 16, true) / 16.0f;
    int bpr_req = o->bytes_per_row == 8 || o->bytes_per_row == 32 ? o->bytes_per_row : 16;
    /* offset digits: enough for the last address, at least 4, an even count */
    uint64_t last_addr = o->base + (size ? (uint64_t)size - 1 : 0);
    if (last_addr < o->base) last_addr = UINT64_MAX;   /* wrapped */
    int digits = 1;
    for (uint64_t v = last_addr >> 4; v; v >>= 4) digits++;
    digits = CUI_MAX(4, (digits + 1) & ~1);
    bool header = !o->no_header && size > 0;
    float header_h = header ? row_h + 1 : 0;

    cui_rect br = cui__prev_rect(ui, lid);
    bool known = cui__has_prev_rect(ui, lid), has_rect = known && br.w > 0;
    if (!known) ui->animating = true;   /* lay out again once the width is known */
    cui__hex_geom g = cui__hex_fit(cw, digits, bpr_req, !o->no_ascii, has_rect ? br.w : 0);
    size_t bpr = (size_t)g.bpr;
    size_t rows = size ? (size - 1) / bpr + 1 : 0;
    float content_h = (float)rows * row_h + 2 * CUI__HEX_INSET;
    float height = o->height > 0 ? o->height : CUI_MIN(320.0f, header_h + (size ? content_h : 96.0f) + 2);
    float rows_h = (has_rect ? br.h : height - 2) - header_h;
    double view_rows = CUI_MAX(1.0, (double)(rows_h - 2 * CUI__HEX_INSET) / row_h);
    double max_top = CUI_MAX(0.0, (double)rows - view_rows);
    float rows_top = br.y + header_h;

    /* ---- pointer ---- */
    cui_interact lr = cui__interact(ui, lid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    bool over_bar = has_rect && max_top > 0 && ui->mx >= br.x + br.w - CUI__HEX_BAR;
    if (has_rect && size > 0) {
        float lx = ui->mx - br.x;
        double row_f = s->pos + (double)(ui->my - rows_top - CUI__HEX_INSET) / row_h;
        if (lr.hovered && ui->mpressed[0]) {
            s->nibble = false;
            if (over_bar) {
                float track_h = rows_h - 6;
                float thumb_h = CUI_MAX(28.0f, track_h * (float)(view_rows / (view_rows + max_top)));
                float thumb_y = rows_top + 3 + (float)(s->pos / max_top) * (track_h - thumb_h);
                s->bar_grab = ui->my >= thumb_y && ui->my <= thumb_y + thumb_h ? ui->my - thumb_y : thumb_h * 0.5f;
                s->bar_drag = true;
            } else if (ui->my >= rows_top) {
                int64_t b = row_f >= 0 && row_f < (double)rows ? cui__hex_hit(&g, lx, (size_t)row_f, size, true) : -1;
                if (b >= 0) {
                    size_t anchor = (ui->mods & CUI_MOD_SHIFT) ? cui__hex_anchor(sel) : (size_t)b;
                    cui__hex_select(sel, anchor, (size_t)b);
                    s->dragging = true;
                } else sel->length = 0;
            }
        }
        if (s->bar_drag && lr.active && ui->mdown[0]) {
            float track_h = rows_h - 6;
            float thumb_h = CUI_MAX(28.0f, track_h * (float)(view_rows / (view_rows + max_top)));
            float t = (ui->my - rows_top - 3 - s->bar_grab) / CUI_MAX(1.0f, track_h - thumb_h);
            s->pos = s->target = cui__clampf(t, 0, 1) * max_top;
            s->last_activity = ui->time;
        }
        if (s->dragging && lr.active && ui->mdown[0]) {
            /* past the top or bottom edge the view scrolls, faster further out */
            float top = rows_top, bottom = br.y + br.h;
            if (ui->my < top || ui->my > bottom) {
                float dist = ui->my < top ? top - ui->my : ui->my - bottom;
                double step = (double)ui->dt * (6.0 + 8.0 * dist / row_h);
                s->target = s->pos = CUI_MAX(0.0, CUI_MIN(max_top, s->pos + (ui->my < top ? -step : step)));
                ui->animating = true;
            }
            double lo = floor(s->pos), hi = CUI_MAX(lo, ceil(s->pos + view_rows) - 1);
            double rf = CUI_MAX(lo, CUI_MIN(hi, row_f));
            rf = CUI_MAX(0.0, CUI_MIN((double)rows - 1, rf));
            int64_t b = cui__hex_hit(&g, lx, (size_t)rf, size, true);
            if (b >= 0 && (size_t)b != sel->cursor) cui__hex_select(sel, cui__hex_anchor(sel), (size_t)b);
        }
        if (lr.hovered && !lr.active && !over_bar && ui->my >= rows_top && row_f >= 0 && row_f < (double)rows)
            res.hovered = cui__hex_hit(&g, lx, (size_t)row_f, size, false);
    }
    if (!ui->mdown[0]) s->dragging = s->bar_drag = false;
    if (ui->scroll_target == sid && ui->scroll_dy != 0 && max_top > 0) {
        s->target -= (double)ui->scroll_dy * 3.0;
        s->last_activity = ui->time;
        ui->scroll_dy = 0;   /* consumed */
    }

    /* ---- go to offset ---- */
    bool center = false;
    if (s->goto_pending && size > 0) {
        size_t off = CUI_MIN(s->goto_off, size - 1);
        cui__hex_select(sel, off, off);
        s->nibble = false;
        center = true;
    }
    s->goto_pending = false;

    /* ---- keys ---- */
    bool reveal = center;
    if (ui->focus == lid && size > 0) {
        int mods = ui->mods;
        bool shift = (mods & CUI_MOD_SHIFT) != 0, prim = (mods & CUI_MOD_PRIMARY) != 0;
        bool far = (mods & (CUI_MOD_CTRL | CUI_MOD_PRIMARY)) != 0;
        size_t cur = sel->cursor, to = cur, last = size - 1;
        size_t page = (size_t)CUI_MAX(1.0, floor(view_rows) - 1) * bpr;
        bool moved = false;
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) { to = to > 0 ? to - 1 : 0; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { to = to < last ? to + 1 : last; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_UP)) { if (to >= bpr) to -= bpr; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) { to = last - to >= bpr ? to + bpr : (to / bpr < last / bpr ? last : to); moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) { to = to >= page ? to - page : to % bpr; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) { to = last - to >= page ? to + page : last; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_HOME)) { to = far ? 0 : to - to % bpr; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_END)) { to = far ? last : CUI_MIN(last, to - to % bpr + bpr - 1); moved = true; }
        if (moved) {
            cui__hex_select(sel, shift ? cui__hex_anchor(sel) : to, to);
            s->nibble = false;
            reveal = true;
        }
        if (prim && cui_key_pressed(ui, CUI_KEY_A)) cui__hex_select(sel, 0, last);
        if (prim && cui_key_pressed(ui, CUI_KEY_C) && sel->length > 0) {
            cui__hex_copy(ui, bytes, sel->start, sel->length, shift);
            res.copied = true;
        }
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE) && sel->length > 0) {
            sel->length = 0;
            s->nibble = false;
            ui->kpressed[CUI_KEY_ESCAPE] = 0;
        }
        /* typing hex digits overwrites the byte at the cursor */
        if (o->edit && ui->text_in_len > 0 && !(mods & (CUI_MOD_CTRL | CUI_MOD_SUPER | CUI_MOD_ALT))) {
            bool used = false;
            size_t lo = SIZE_MAX, hi = 0;
            if (s->nibble && s->nibble_at != sel->cursor) s->nibble = false;
            for (int i = 0; i < ui->text_in_len; i++) {
                int v = cui__hex_value(ui->text_in[i]);
                if (v < 0) continue;
                used = true;
                size_t at = sel->cursor;
                uint8_t *p = &o->edit[at];
                if (!s->nibble) {
                    *p = (uint8_t)((v << 4) | (*p & 0x0F));
                    s->nibble = true;
                    s->nibble_at = at;
                } else {
                    *p = (uint8_t)((*p & 0xF0) | v);
                    s->nibble = false;
                    if (at < last) sel->cursor = at + 1;
                }
                lo = CUI_MIN(lo, at); hi = CUI_MAX(hi, at);
                cui__hex_select(sel, sel->cursor, sel->cursor);
            }
            if (used) {
                res.edited = true;
                res.edit_offset = lo;
                res.edit_length = hi - lo + 1;
                ui->text_in_len = 0;
                ui->text_in[0] = 0;
                ui->focus_visible = true;   /* typing is keyboard use: show the cursor */
                reveal = true;
            }
        }
    }
    if (reveal && size > 0) {
        double row = (double)(sel->cursor / bpr);
        if (center && (row < s->target || row + 1 > s->target + view_rows)) s->target = row - view_rows * 0.5 + 0.5;
        else if (row < s->target) s->target = row;
        else if (row + 1 > s->target + view_rows) s->target = row + 1 - view_rows;
        ui->animating = true;
    }

    /* ---- scrolling ---- */
    s->target = CUI_MAX(0.0, CUI_MIN(max_top, s->target));
    if (s->pos > max_top) s->pos = max_top;
    float motion = ui->theme.motion;
    if (motion <= 0 || ui->frame <= 1) s->pos = s->target;
    else {
        double a = 1.0 - exp(-(double)ui->dt * 4.6 / (0.14 * (double)motion));
        s->pos += (s->target - s->pos) * a;
        if (fabs(s->pos - s->target) < 0.01) s->pos = s->target;
        else ui->animating = true;
    }
    bool recent = s->last_activity > 0 && ui->time - s->last_activity < 0.9;
    bool bar_hover = over_bar && (lr.hovered || s->bar_drag);
    bool show_bar = recent || bar_hover || s->bar_drag;
    if (recent) cui__wakeup_after(ui, (float)(0.9 - (ui->time - s->last_activity)) + 0.01f);
    cui__tween(ui, &s->bar_t, show_bar ? 1.0f : 0.0f, show_bar ? 0.12f : 0.3f);

    /* ---- build ---- */
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->h = o->height < 0 ? CUI_FILL : CUI_PX(height);
    on->radius = CUI_CORNERS(ui->theme.radius_control);
    on->border = 1; on->border_color = CUI_C(ui, BORDER);
    bool dark = ui->mode == CUI_THEME_DARK;
    cui_color bg = dark ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, BACKGROUND), 0.4f) : CUI_C(ui, SURFACE_MUTED);
    on->bg = bg;   /* the code block's surface */
    on->flags |= CUI_NF_CLIP;
    cui__open(ui, outer);
    int body = cui__node_new(ui, lid);
    cui_node *bn = &ui->nodes[body];
    bn->w = CUI_FILL; bn->h = CUI_FILL;
    cui__mark_interactive(ui, body, CUI_IF_FOCUSABLE | CUI_IF_DRAG);

    cui__hex_paint *p = cui__frame_alloc(ui, sizeof *p);
    memset(p, 0, sizeof *p);
    p->g = g; p->sid = sid;
    p->bpr_req = bpr_req; p->ascii_req = !o->no_ascii;
    p->size = fsize; p->row_h = row_h; p->header_h = header_h; p->header = header;
    p->pos = s->pos; p->max_top = max_top; p->view_rows = view_rows;
    p->bar_t = s->bar_t; p->bar_big = bar_hover || s->bar_drag;
    p->size_bytes = size; p->base = o->base; p->empty = size == 0;
    p->hover = res.hovered;
    p->has_sel = sel->length > 0;
    p->sel_lo = sel->start; p->sel_hi = sel->start + (sel->length ? sel->length - 1 : 0);
    p->cursor = sel->cursor;
    p->show_cursor = ui->focus == lid && ui->focus_visible && size > 0;
    p->nibble = s->nibble && s->nibble_at == sel->cursor;
    cui_color fg = CUI_C(ui, FOREGROUND);
    p->fg = fg;
    p->muted = CUI_C(ui, TEXT_MUTED);
    p->secondary = CUI_C(ui, TEXT_SECONDARY);
    p->sel = ui->accent == CUI_ACCENT_NEUTRAL ? cui_mix(bg, fg, dark ? 0.20f : 0.13f) : cui_mix(bg, CUI_C(ui, ACCENT), dark ? 0.32f : 0.22f);
    p->hover_c = cui_alpha(fg, dark ? 0.14f : 0.09f);
    p->bar = ui->accent == CUI_ACCENT_NEUTRAL ? fg : CUI_C(ui, ACCENT);
    p->line = CUI_C(ui, BORDER);
    if (size > 0) {
        /* the rows in sight, copied: the caller may change its bytes before painting */
        size_t first = (size_t)floor(s->pos);
        size_t last_row = (size_t)CUI_MIN((double)rows, ceil(s->pos + view_rows + (double)(2 * CUI__HEX_INSET) / row_h) + 1);
        if (first > last_row) first = last_row;
        size_t nb = CUI_MIN(size - first * bpr, (last_row - first) * bpr);
        uint8_t *copy = cui__frame_alloc(ui, nb ? nb : 1);
        if (nb) memcpy(copy, bytes + first * bpr, nb);
        p->bytes = copy;
        p->first_row = first;
        p->row_count = last_row - first;
        /* ranges that reach the rows in sight */
        size_t va = first * bpr, vb = first * bpr + nb;
        int nm = 0;
        cui__hex_mark *marks = o->range_count > 0 && o->ranges ? cui__frame_alloc(ui, sizeof *marks * (size_t)o->range_count) : NULL;
        for (int i = 0; marks && i < o->range_count; i++) {
            const cui_hex_range *rg = &o->ranges[i];
            if (rg->length == 0 || rg->offset >= size) continue;
            size_t a = rg->offset, b = rg->length > size - a ? size : a + rg->length;
            if (b <= va || a >= vb) continue;
            cui_color tone = cui__tone_color(ui, rg->tone);
            marks[nm].a = a; marks[nm].b = b;
            marks[nm].tint = cui_alpha(tone, dark ? 0.22f : 0.15f);
            marks[nm].text = cui_mix(fg, tone, dark ? 0.22f : 0.30f);
            nm++;
        }
        p->marks = marks; p->nmarks = nm;
    }
    bn->paint = cui__paint_hex;
    bn->paint_data = p;

    /* the label of the range under the pointer, as a tooltip over that byte */
    cui_id tip = 0;
    if (res.hovered >= 0 && o->ranges && has_rect) {
        size_t at = (size_t)res.hovered;
        for (int i = o->range_count - 1; i >= 0; i--) {
            const cui_hex_range *rg = &o->ranges[i];
            if (!rg->label || !rg->label[0] || at < rg->offset || at - rg->offset >= rg->length) continue;
            tip = lid ^ (0x9E3779B1u * (cui_id)(i + 1));
            size_t row = at / bpr;
            int col = (int)(at % bpr);
            float y = header_h + CUI__HEX_INSET + (float)((double)row - s->pos) * row_h;
            cui__open(ui, body);
            cui__absolute(ui, tip, cui__hex_cell_x(&g, col) - 3, y, 2.0f * g.cw + 6, row_h);
            cui__close(ui);
            unsigned long long n = (unsigned long long)CUI_MIN(rg->length, size - rg->offset);
            cui__tooltip_passive(ui, tip, cui__frame_printf(ui, "%s, %llu byte%s", rg->label, n, n == 1 ? "" : "s"));
            break;
        }
    }
    if (!tip && s->tip && ui->tooltip_id == s->tip) ui->tooltip_id = 0;   /* the next range waits its delay again */
    s->tip = tip;

    if (ui->focus == lid) cui__focus_ring(ui, outer, ui->focus_visible ? 1.0f : 0.0f);
    cui__close(ui);   /* outer */
    ui->last_id = id;

    res.selection = *sel;
    res.selection_changed = memcmp(&before, sel, sizeof before) != 0;
    return res;
}
