/* Month calendar: a month grid with events. The grid is one painted node:
 * the events of the weeks in view are laid out once per frame into items
 * (bars, timed lines and "+N more" links) addressed by row, column and line,
 * and the same items serve the paint and, from the previous frame's rect,
 * the pointer. A month shows at most 42 days, so the work per frame follows
 * the events in those weeks. */
#include "cui_internal.h"

#define cui__fs cui__fsz

#define CUI__MC_HEAD   26.0f   /* the day number's band at the top of a cell */
#define CUI__MC_LINE   19.0f   /* one event line */
#define CUI__MC_MAXIT  512
#define CUI__MC_NARROW 520.0f  /* below this width: dots in the cells, an agenda under them */

static const char *cui__mc_months[12] = { "January", "February", "March", "April", "May", "June", "July",
                                          "August", "September", "October", "November", "December" };
static const char *cui__mc_days[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static const char *cui__mc_long_days[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

enum { CUI__MC_BAR = 0, CUI__MC_TIMED, CUI__MC_MORE };
typedef struct { int16_t row, c0, c1, line; uint8_t kind; bool cont_l, cont_r; int ev, more, day; } cui__mc_item;

typedef struct {
    cui_date sel;
    int hover_ev, hover_day, more_day;   /* day indices are days since 1970 */
    int focus_day;
    bool has_sel, init;
} cui__mc_state;

typedef struct {
    int start_day, rows, first_wd, today, sel, hover_ev, hover_day, month, focus_day;
    bool compact, has_sel, focused;
    float cell_h;
    cui__mc_item *items; int nitems;
    const cui_month_event *ev;   /* a copy of the events the items name */
    const char **titles;
    const char **times;
    cui_color line, fg, muted, accent, accent_fg, sel_bg, hover_bg;
} cui__mc_paint;

static int cui__mc_weekday(int days) { return ((days + 3) % 7 + 7) % 7; }   /* 0 = Monday; 1970-01-01 was a Thursday */
static int cui__mc_days_in(int y, int m) {
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[(m - 1 + 12) % 12];
}

static void cui__mc_span(const cui_month_event *e, int *a, int *b) {
    *a = cui_date_to_days(e->start);
    *b = e->end.year ? cui_date_to_days(e->end) : *a;
    if (*b < *a) *b = *a;
}

static const char *cui__mc_time(cui_ctx *ui, float h, bool h12) {
    int mins = (int)lroundf(cui__clampf(h, 0, 24) * 60), hh = mins / 60 % 24, mm = mins % 60;
    if (!h12) return cui__frame_printf(ui, "%d:%02d", hh, mm);
    int h1 = hh % 12 == 0 ? 12 : hh % 12;
    return mm ? cui__frame_printf(ui, "%d:%02d%c", h1, mm, hh < 12 ? 'a' : 'p') : cui__frame_printf(ui, "%d%c", h1, hh < 12 ? 'a' : 'p');
}

/* Lays out the weeks: bars in lanes shared by the week, timed events under
 * them in each day (in time order), and "+N more" where a day runs out of
 * lines. timed: the single-day timed events in view, sorted by day and time. */
typedef struct { int day; float time; int i; } cui__mc_timed;
static int cui__mc_timed_cmp(const void *a, const void *b) {
    const cui__mc_timed *x = a, *y = b;
    if (x->day != y->day) return x->day < y->day ? -1 : 1;
    if (x->time != y->time) return x->time < y->time ? -1 : 1;
    return x->i < y->i ? -1 : x->i > y->i;
}

static int cui__mc_layout(const cui_month_event *ev, int count, int start_day, int rows, int lines,
                          const cui__mc_timed *timed, int ntimed, cui__mc_item *out, int cap) {
    int n = 0, ti = 0;
    for (int row = 0; row < rows; row++) {
        int w0 = start_day + row * 7, w1 = w0 + 6;
        int used[7] = { 0 }, hidden[7] = { 0 };
        uint8_t lane_end[64];   /* per lane: the last column taken, +1 */
        memset(lane_end, 0, sizeof lane_end);
        /* bars: multi-day ones first, each in the earliest lane free from its first column */
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < count; i++) {
                int a, b;
                cui__mc_span(&ev[i], &a, &b);
                bool bar = ev[i].all_day || b > a;
                if (!bar || b < w0 || a > w1) continue;
                if ((pass == 0) != (b - a >= 1)) continue;
                int c0 = CUI_MAX(a, w0) - w0, c1 = CUI_MIN(b, w1) - w0;
                int lane = 0;
                while (lane < 64 && lane_end[lane] > c0) lane++;
                if (lane >= 64) continue;
                lane_end[lane] = (uint8_t)(c1 + 1);
                for (int c = c0; c <= c1; c++) used[c] = CUI_MAX(used[c], lane + 1);
                if (lane >= lines || n >= cap) { for (int c = c0; c <= c1; c++) hidden[c]++; continue; }   /* past the last line */
                out[n++] = (cui__mc_item){ (int16_t)row, (int16_t)c0, (int16_t)c1, (int16_t)lane, CUI__MC_BAR, a < w0, b > w1, i, 0, w0 + c0 };
            }
        /* timed events, already in day and time order, under the bars of their day */
        while (ti < ntimed && timed[ti].day < w0) ti++;
        for (; ti < ntimed && timed[ti].day <= w1; ti++) {
            int c = timed[ti].day - w0;
            if (used[c] < lines && n < cap) out[n++] = (cui__mc_item){ (int16_t)row, (int16_t)c, (int16_t)c, (int16_t)used[c], CUI__MC_TIMED, false, false, timed[ti].i, 0, timed[ti].day };
            else hidden[c]++;
            used[c]++;
        }
        /* a day with too many: its last line becomes "+N more" */
        for (int c = 0; c < 7; c++) {
            if (hidden[c] == 0) continue;
            int extra = hidden[c];
            for (int k = n - 1; k >= 0; k--) {
                cui__mc_item *it = &out[k];
                if (it->row != row) break;
                if (it->kind > CUI__MC_TIMED || it->line != lines - 1 || c < it->c0 || c > it->c1) continue;
                extra++;
                if (it->kind == CUI__MC_TIMED || it->c0 == it->c1) it->kind = 255;   /* dropped below */
                else if (it->c0 == c) { it->c0++; it->cont_l = true; it->day++; }   /* the bar steps around this day */
                else if (it->c1 == c) { it->c1--; it->cont_r = true; }
                else if (n < cap) {
                    out[n] = *it;
                    out[n].c0 = (int16_t)(c + 1); out[n].cont_l = true; out[n].day = w0 + c + 1;
                    it->c1 = (int16_t)(c - 1); it->cont_r = true;
                    n++;
                }
            }
            if (n < cap) out[n++] = (cui__mc_item){ (int16_t)row, (int16_t)c, (int16_t)c, (int16_t)(lines - 1), CUI__MC_MORE, false, false, -1, extra, w0 + c };
        }
        int m = 0;
        for (int k = 0; k < n; k++) if (out[k].kind != 255) out[m++] = out[k];
        n = m;
    }
    return n;
}

/* the single-day timed events in [d0, d1], sorted */
static int cui__mc_collect(cui_ctx *ui, const cui_month_event *ev, int count, int d0, int d1, cui__mc_timed **out) {
    cui__mc_timed *t = cui__frame_alloc(ui, sizeof *t * (size_t)CUI_MAX(1, count));
    int n = 0;
    for (int i = 0; i < count; i++) {
        int a, b;
        cui__mc_span(&ev[i], &a, &b);
        if (ev[i].all_day || b > a || a < d0 || a > d1) continue;
        t[n++] = (cui__mc_timed){ a, ev[i].time, i };
    }
    qsort(t, (size_t)n, sizeof *t, cui__mc_timed_cmp);
    *out = t;
    return n;
}

typedef struct { cui_rect grid; float cw, ch; } cui__mc_geo;
static cui__mc_geo cui__mc_geom(cui_rect r, int rows) {
    cui__mc_geo g = { r, r.w / 7.0f, rows > 0 ? r.h / (float)rows : r.h };
    return g;
}
static cui_rect cui__mc_item_rect(const cui__mc_geo *g, const cui__mc_item *it) {
    float x = g->grid.x + it->c0 * g->cw + 3, w = (it->c1 - it->c0 + 1) * g->cw - 6;
    float y = g->grid.y + it->row * g->ch + CUI__MC_HEAD + it->line * CUI__MC_LINE;
    return (cui_rect){ x, y, w, CUI__MC_LINE - 2 };
}

static void cui__paint_month(cui_ctx *ui, cui_node *n, void *data) {
    cui__mc_paint *p = data;
    cui_rect r = n->rect;
    cui__mc_geo g = cui__mc_geom(r, p->rows);
    float fs = cui__fs(ui, ui->theme.text_sm), fx = cui__fs(ui, ui->theme.text_xs);
    float fh = cui__font_line_height(ui, CUI_FONT_BODY, fs);
    /* cells and the grid lines */
    for (int row = 0; row < p->rows; row++)
        for (int c = 0; c < 7; c++) {
            int day = p->start_day + row * 7 + c;
            cui_rect cell = { r.x + c * g.cw, r.y + row * g.ch, g.cw, g.ch };
            cui_date dt = cui_date_from_days(day);
            bool in_month = dt.month == p->month, sel = p->has_sel && day == p->sel;
            if (!p->compact && sel) cui_draw_rect(ui, cell, CUI_CORNERS(0), p->sel_bg);
            else if (!p->compact && day == p->hover_day && p->hover_ev < 0) cui_draw_rect(ui, cell, CUI_CORNERS(0), p->hover_bg);
            char num[4];
            snprintf(num, sizeof num, "%d", dt.day);
            float nw = cui__text_width(ui, day == p->today || sel ? CUI_FONT_MEDIUM : CUI_FONT_BODY, fs, num, -1, true);
            cui_color col = in_month ? p->fg : p->muted;
            if (p->compact) {
                cui_vec2 ctr = { cell.x + cell.w * 0.5f, cell.y + 16 };
                if (sel) { cui_draw_circle(ui, ctr, 13, p->fg); col = CUI_C(ui, BACKGROUND); }
                else if (day == p->today) { cui_draw_arc(ui, ctr, 12.5f, 0, 2 * CUI_PI, 1.5f, p->accent); col = p->accent; }
                cui__draw_text_line(ui, day == p->today || sel ? CUI_FONT_MEDIUM : CUI_FONT_BODY, fs, ctr.x - nw * 0.5f, ctr.y - fh * 0.5f, col, num, -1, true);
                if (p->focused && day == p->focus_day) cui__draw_focus_ring(ui, (cui_rect){ ctr.x - 15, ctr.y - 15, 30, 30 }, CUI_CORNERS(15), 1);
            } else {
                float nx = cell.x + 8, ny = cell.y + 5;
                if (day == p->today) {
                    cui_draw_rect(ui, (cui_rect){ nx - 5, ny - 1, CUI_MAX(22.0f, nw + 10), 20 }, CUI_CORNERS(10), p->accent);
                    col = p->accent_fg;
                    nx += (CUI_MAX(22.0f, nw + 10) - nw - 10) * 0.5f;
                }
                cui__draw_text_line(ui, day == p->today || sel ? CUI_FONT_MEDIUM : CUI_FONT_BODY, fs, nx, ny + (18 - fh) * 0.5f, col, num, -1, true);
                if (p->focused && day == p->focus_day) cui__draw_focus_ring(ui, cui__rect_inset(cell, 2), CUI_CORNERS(4), 1);
            }
        }
    if (!p->compact) {
        for (int row = 1; row < p->rows; row++) cui_draw_rect(ui, (cui_rect){ r.x, r.y + row * g.ch - 0.5f, r.w, 1 }, CUI_CORNERS(0), p->line);
        for (int c = 1; c < 7; c++) cui_draw_rect(ui, (cui_rect){ r.x + c * g.cw - 0.5f, r.y, 1, r.h }, CUI_CORNERS(0), p->line);
    }
    /* events */
    for (int k = 0; k < p->nitems; k++) {
        const cui__mc_item *it = &p->items[k];
        if (p->compact) continue;   /* a narrow grid shows dots, drawn below */
        cui_rect q = cui__mc_item_rect(&g, it);
        if (q.w < 4) continue;
        bool hov = it->ev >= 0 && it->ev == p->hover_ev;
        if (it->kind == CUI__MC_MORE) {
            const char *s = cui__frame_printf(ui, "+%d more", it->more);
            cui_color mc = it->day == p->hover_day && p->hover_ev == -2 ? p->fg : CUI_C(ui, TEXT_SECONDARY);
            cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fx, q.x + 6, q.y + (q.h - cui__font_line_height(ui, CUI_FONT_MEDIUM, fx)) * 0.5f, q.w - 8, mc, s, (int)strlen(s), false);
            continue;
        }
        const cui_month_event *e = &p->ev[it->ev];
        cui_color tone = cui__tone_color(ui, e->tone ? e->tone : CUI_TONE_INFO);
        const char *title = p->titles[it->ev];
        if (it->kind == CUI__MC_BAR) {
            cui_color bg = cui_mix(CUI_C(ui, SURFACE), tone, ui->mode == CUI_THEME_DARK ? (hov ? 0.34f : 0.26f) : (hov ? 0.24f : 0.16f));
            cui_corners cr = { it->cont_l ? 0 : 4, it->cont_r ? 0 : 4, it->cont_r ? 0 : 4, it->cont_l ? 0 : 4 };
            cui_rect bq = q;
            if (it->cont_l) { bq.x -= 3; bq.w += 3; }
            if (it->cont_r) bq.w += 3;
            cui_draw_rect(ui, bq, cr, bg);
            if (!it->cont_l) cui_draw_rect(ui, (cui_rect){ bq.x, bq.y, 3, bq.h }, (cui_corners){ 4, 0, 0, 4 }, tone);
            float tx = bq.x + (it->cont_l ? 6 : 8);
            cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fx, tx, q.y + (q.h - cui__font_line_height(ui, CUI_FONT_MEDIUM, fx)) * 0.5f, bq.x + bq.w - tx - 4, p->fg, title, (int)strlen(title), false);
        } else {
            if (hov) cui_draw_rect(ui, q, CUI_CORNERS(4), p->hover_bg);
            cui_draw_circle(ui, (cui_vec2){ q.x + 7, q.y + q.h * 0.5f }, 3, tone);
            const char *tm = p->times[it->ev];
            float tw = cui__text_width(ui, CUI_FONT_BODY, fx, tm, -1, true);
            float ty = q.y + (q.h - cui__font_line_height(ui, CUI_FONT_BODY, fx)) * 0.5f;
            bool show_time = q.w > tw + 50;
            if (show_time) cui__draw_text_line(ui, CUI_FONT_BODY, fx, q.x + 15, ty, p->muted, tm, -1, true);
            float tx = q.x + 15 + (show_time ? tw + 5 : 0);
            cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fx, tx, ty, q.x + q.w - tx - 2, p->fg, title, (int)strlen(title), false);
        }
    }
    if (p->compact) {
        /* up to three dots per day, under the number */
        for (int row = 0; row < p->rows; row++)
            for (int c = 0; c < 7; c++) {
                int day = p->start_day + row * 7 + c, nd = 0;
                cui_color dots[3];
                for (int k = 0; k < p->nitems && nd < 3; k++) {
                    const cui__mc_item *it = &p->items[k];
                    if (it->row != row || c < it->c0 || c > it->c1 || it->ev < 0) continue;
                    dots[nd++] = cui__tone_color(ui, p->ev[it->ev].tone ? p->ev[it->ev].tone : CUI_TONE_INFO);
                }
                (void)day;
                float cx = r.x + c * g.cw + g.cw * 0.5f - (nd - 1) * 3.5f, cy = r.y + row * g.ch + 35;
                for (int k = 0; k < nd; k++) cui_draw_circle(ui, (cui_vec2){ cx + k * 7, cy }, 2.2f, dots[k]);
            }
    }
}

/* the agenda under a narrow grid: the selected day's events */
static int cui__mc_agenda(cui_ctx *ui, cui_id id, const cui_month_event *ev, int count, int day, const cui_month_opts *o) {
    int clicked = -1;
    cui_date d = cui_date_from_days(day);
    const char *const *mn = o->month_names ? o->month_names : cui__mc_months;
    int wd = cui__mc_weekday(day);
    const char *head = o->day_names ? cui__frame_printf(ui, "%s %d %s", o->day_names[wd], d.day, mn[d.month - 1])
                                    : cui__frame_printf(ui, "%s, %d %s", cui__mc_long_days[wd], d.day, mn[d.month - 1]);
    int col = cui__node_new(ui, id ^ 0xA6E0u);
    ui->nodes[col].gap = 2; ui->nodes[col].w = CUI_FILL; ui->nodes[col].pad[0] = 6;
    cui__open(ui, col);
    int h = cui__text_node(ui, head, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
    ui->nodes[h].line_height = 1.6f;
    /* the day's events: all-day and multi-day first, then by time */
    int *list = cui__frame_alloc(ui, sizeof(int) * (size_t)CUI_MAX(1, count));
    int nl = 0;
    for (int i = 0; i < count; i++) {
        int a, b;
        cui__mc_span(&ev[i], &a, &b);
        if (day < a || day > b) continue;
        int k = nl++;
        bool ib = ev[i].all_day || b > a;
        while (k > 0) {
            int j = list[k - 1], ja, jb;
            cui__mc_span(&ev[j], &ja, &jb);
            bool jbar = ev[j].all_day || jb > ja;
            if (jbar && !ib) break;
            if (jbar == ib && (ib || ev[j].time <= ev[i].time)) break;
            list[k] = j; k--;
        }
        list[k] = i;
    }
    int shown = 0;
    for (int li = 0; li < nl; li++) {
            int i = list[li], a, b;
            cui__mc_span(&ev[i], &a, &b);
            bool bar = ev[i].all_day || b > a;
            cui_id rid = cui__hash(&i, sizeof i, id ^ 0xA6E1u);
            cui_interact r = cui__interact(ui, rid, CUI_IF_FOCUSABLE);
            if (r.clicked || r.keyboard_activate) clicked = i;
            int row = cui__node_new(ui, rid);
            cui_node *rn = &ui->nodes[row];
            rn->dir = CUI_ROW; rn->gap = 10; rn->align = CUI_ALIGN_CENTER; rn->w = CUI_FILL;
            rn->pad[0] = rn->pad[2] = 7; rn->pad[1] = rn->pad[3] = 8;
            rn->radius = CUI_CORNERS(ui->theme.radius_control);
            if (r.hovered) { rn->bg = CUI_C(ui, HOVER); cui__set_cursor(ui, CUI_CURSOR_HAND); }
            cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
            cui__focus_ring(ui, row, r.focus_visible ? 1.0f : 0.0f);
            cui__open(ui, row);
            int bar_n = cui__leaf(ui, 0, CUI_PX(3), CUI_PX(28));
            ui->nodes[bar_n].bg = cui__tone_color(ui, ev[i].tone ? ev[i].tone : CUI_TONE_INFO);
            ui->nodes[bar_n].radius = CUI_CORNERS(1.5f);
            int tcol = cui__node_new(ui, 0);
            ui->nodes[tcol].w = CUI_FILL; ui->nodes[tcol].gap = 0;
            cui__open(ui, tcol);
            int tt = cui__text_node(ui, ev[i].title ? ev[i].title : "", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
            ui->nodes[tt].w = CUI_FILL; ui->nodes[tt].line_height = 1.35f;
            const char *sub = bar ? (b > a ? cui__frame_printf(ui, "%d days", b - a + 1) : "All day") : cui__mc_time(ui, ev[i].time, o->hour12);
            int st = cui__text_node(ui, sub, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[st].line_height = 1.3f; ui->nodes[st].flags |= CUI_NF_TABULAR;
            cui__close(ui);
            cui__close(ui);
            shown++;
    }
    if (!shown) {
        int e = cui__text_node(ui, "No events", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[e].line_height = 1.8f;
    }
    cui__close(ui);
    return clicked;
}

cui_month_result cui_month_calendar_ex(cui_ctx *ui, const char *id_str, cui_date *month, const cui_month_event *events, int count, const cui_month_opts *o) {
    cui_month_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_month_result res = { -1, false, false, false };
    if (!events) count = 0;
    count = CUI_MAX(0, count);
    cui_id id = cui_make_id(ui, id_str ? id_str : "month");
    cui_id gid = id ^ 0x6A1Du, pid = id ^ 0x6A1Eu;
    cui__mc_state *st = cui_state(ui, id, sizeof *st);
    cui_date today = o->today.year ? o->today : cui_date_today();
    int today_n = cui_date_to_days(today);
    static cui_date own;
    cui_date *mon = month ? month : &own;
    if (!mon->year) { mon->year = today.year; mon->month = today.month; }
    mon->month = CUI_CLAMP(mon->month, 1, 12);
    if (!st->init) {
        st->hover_ev = -1; st->hover_day = st->more_day = -1;
        st->focus_day = o->selected && o->selected->year ? cui_date_to_days(*o->selected) : today_n;
        st->init = true;
    }
    int sel_n = o->selected && o->selected->year ? cui_date_to_days(*o->selected) : -1;
    cui_push_id(ui, id_str ? id_str : "month");
    /* moving the shown month by n */
#define CUI__MC_SHIFT(n) do { int m_ = mon->year * 12 + (mon->month - 1) + (n); mon->year = m_ / 12; mon->month = m_ % 12 + 1; res.month_changed = true; } while (0)
    cui_rect pr = cui__prev_rect(ui, id);
    bool compact = pr.w > 0 && pr.w < CUI__MC_NARROW;
    const char *const *mn = o->month_names ? o->month_names : cui__mc_months;
    const char *const *dn = o->day_names ? o->day_names : cui__mc_days;
    int col = cui__node_new(ui, id);
    ui->nodes[col].w = CUI_FILL; ui->nodes[col].gap = 8; ui->nodes[col].self_align = CUI_ALIGN_STRETCH;
    cui__open(ui, col);
    /* header: the month, Today, previous and next */
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].align = CUI_ALIGN_CENTER; ui->nodes[head].gap = 4; ui->nodes[head].w = CUI_FILL;
    cui__open(ui, head);
    int ht = cui__text_node(ui, cui__frame_printf(ui, "%s %d", mn[mon->month - 1], mon->year), -1, CUI_FONT_MEDIUM,
                            cui__fs(ui, compact ? ui->theme.text_body + 1 : ui->theme.text_body + 3), CUI_C(ui, FOREGROUND), false);
    ui->nodes[ht].w = CUI_FILL; ui->nodes[ht].line_height = 1.3f;
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_OUTLINE;
    if (cui_button_ex(ui, "Today", &bo)) {
        res.month_changed = mon->year != today.year || mon->month != today.month;
        mon->year = today.year; mon->month = today.month;
        st->focus_day = today_n;
    }
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST;
    bo.tooltip = "Previous month";
    if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_LEFT, &bo)) CUI__MC_SHIFT(-1);
    bo.tooltip = "Next month";
    if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_RIGHT, &bo)) CUI__MC_SHIFT(1);
    cui__close(ui);
    /* weekday names */
    int fwd = CUI_CLAMP(o->first_weekday, 0, 6);
    int wh = cui__node_new(ui, 0);
    ui->nodes[wh].dir = CUI_ROW; ui->nodes[wh].w = CUI_FILL;
    cui__open(ui, wh);
    for (int c = 0; c < 7; c++) {
        const char *nm = dn[(fwd + c) % 7];
        char one[8];
        if (compact && !o->day_names) { snprintf(one, sizeof one, "%.1s", nm); nm = cui__frame_strdup(ui, one, -1); }
        int t = cui__text_node(ui, nm, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[t].w = CUI_GROW(1); ui->nodes[t].min_w = 0;
        ui->nodes[t].text_align = compact ? CUI_TEXT_CENTER : CUI_TEXT_LEFT;
        ui->nodes[t].pad[3] = compact ? 0 : 8;
        ui->nodes[t].line_height = 1.6f;
    }
    cui__close(ui);
    /* the weeks in view */
    int first = cui_date_to_days((cui_date){ mon->year, mon->month, 1 });
    int lead = (cui__mc_weekday(first) - fwd + 7) % 7;
    int start_day = first - lead;
    int rows = (lead + cui__mc_days_in(mon->year, mon->month) + 6) / 7;
    float cell_h = compact ? 46 : o->cell_height > 0 ? o->cell_height : 104;   /* a phone's cells hold a number and dots */
    int lines = compact ? 99 : CUI_MAX(1, (int)((cell_h - CUI__MC_HEAD - 4) / CUI__MC_LINE));
    cui__mc_item *items = cui__frame_alloc(ui, sizeof(cui__mc_item) * CUI__MC_MAXIT);
    cui__mc_timed *timed;
    int ntimed = cui__mc_collect(ui, events, count, start_day, start_day + rows * 7 - 1, &timed);
    int nitems = cui__mc_layout(events, count, start_day, rows, lines, timed, ntimed, items, CUI__MC_MAXIT);
    /* the pointer, from last frame's grid */
    cui_interact r = cui__interact(ui, gid, CUI_IF_FOCUSABLE);
    cui_rect gr = cui__prev_rect(ui, gid);
    int hover_ev = -1, hover_day = -1;
    bool on_more = false;
    if (r.hovered && gr.w > 0) {
        cui__mc_geo g = cui__mc_geom(gr, rows);
        int c = (int)floorf((ui->mx - gr.x) / g.cw), row = (int)floorf((ui->my - gr.y) / g.ch);
        if (c >= 0 && c < 7 && row >= 0 && row < rows) hover_day = start_day + row * 7 + c;
        if (!compact)
            for (int k = 0; k < nitems; k++) {
                cui_rect q = cui__mc_item_rect(&g, &items[k]);
                if (cui__rect_contains(q, ui->mx, ui->my)) { hover_ev = items[k].ev; on_more = items[k].kind == CUI__MC_MORE; break; }
            }
        if (hover_ev >= 0 || on_more) cui__set_cursor(ui, CUI_CURSOR_HAND);
    }
    if (r.clicked && hover_day >= 0) {
        if (hover_ev >= 0) res.event = hover_ev;
        else if (on_more) { st->more_day = hover_day; cui__popup_set(ui, pid, true); }
        else {
            sel_n = hover_day; res.day_clicked = true;
            if (r.double_clicked || ui->click_count >= 2) res.day_activated = true;
        }
        st->focus_day = hover_day;
    }
    /* keys move the selected day; leaving the month turns the page */
    if (r.focused) {
        int fd = st->focus_day, moved = 0;
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) moved = -1;
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) moved = 1;
        if (cui_key_pressed(ui, CUI_KEY_UP)) moved = -7;
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) moved = 7;
        if (moved) { fd += moved; sel_n = fd; res.day_clicked = true; }
        if (cui_key_pressed(ui, CUI_KEY_PAGE_UP) || cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) {
            int k = cui_key_pressed(ui, CUI_KEY_PAGE_UP) ? -1 : 1;
            cui_date fdd = cui_date_from_days(fd);
            int mm = fdd.year * 12 + fdd.month - 1 + k;
            int y = mm / 12, m = mm % 12 + 1;
            fd = cui_date_to_days((cui_date){ y, m, CUI_MIN(fdd.day, cui__mc_days_in(y, m)) });
            sel_n = fd; res.day_clicked = true;
        }
        if (cui_key_pressed(ui, CUI_KEY_HOME)) { fd = today_n; sel_n = fd; res.day_clicked = true; }
        if (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE)) { sel_n = fd; res.day_clicked = true; res.day_activated = cui_key_pressed(ui, CUI_KEY_ENTER); }
        st->focus_day = fd;
        cui_date fdd = cui_date_from_days(fd);
        if (fdd.year != mon->year || fdd.month != mon->month) { mon->year = fdd.year; mon->month = fdd.month; res.month_changed = true; }
    }
    if (res.day_clicked && o->selected) *o->selected = cui_date_from_days(sel_n);
    if (res.month_changed) {
        /* the grid moved: lay the new month out this frame */
        first = cui_date_to_days((cui_date){ mon->year, mon->month, 1 });
        lead = (cui__mc_weekday(first) - fwd + 7) % 7;
        start_day = first - lead;
        rows = (lead + cui__mc_days_in(mon->year, mon->month) + 6) / 7;
        ntimed = cui__mc_collect(ui, events, count, start_day, start_day + rows * 7 - 1, &timed);
        nitems = cui__mc_layout(events, count, start_day, rows, lines, timed, ntimed, items, CUI__MC_MAXIT);
        hover_ev = -1; hover_day = -1;
    }
    int grid = cui__leaf(ui, gid, CUI_FILL, CUI_PX(rows * cell_h));
    ui->nodes[grid].self_align = CUI_ALIGN_STRETCH;
    if (!compact) {
        ui->nodes[grid].border = 1; ui->nodes[grid].border_color = CUI_C(ui, BORDER);
        ui->nodes[grid].radius = CUI_CORNERS(ui->theme.radius_control);
        ui->nodes[grid].flags |= CUI_NF_CLIP;
    }
    cui__mark_interactive(ui, grid, CUI_IF_FOCUSABLE);
    cui__mc_paint *p = cui__frame_alloc(ui, sizeof *p);
    memset(p, 0, sizeof *p);
    p->start_day = start_day; p->rows = rows; p->today = today_n; p->sel = sel_n; p->has_sel = sel_n >= 0;
    p->hover_ev = on_more ? -2 : hover_ev; p->hover_day = hover_day; p->month = mon->month;
    p->compact = compact; p->cell_h = cell_h; p->focused = r.focus_visible; p->focus_day = st->focus_day;
    p->items = items; p->nitems = nitems;
    /* only the events the items name are read in the paint: copy their titles and times */
    p->ev = cui__frame_alloc(ui, sizeof(cui_month_event) * (size_t)CUI_MAX(1, count));
    p->titles = cui__frame_alloc(ui, sizeof(char *) * (size_t)CUI_MAX(1, count));
    p->times = cui__frame_alloc(ui, sizeof(char *) * (size_t)CUI_MAX(1, count));
    for (int k = 0; k < nitems; k++) {
        int e = items[k].ev;
        if (e < 0 || p->titles[e]) continue;
        ((cui_month_event *)p->ev)[e] = events[e];
        p->titles[e] = cui__frame_strdup(ui, events[e].title ? events[e].title : "", -1);
        p->times[e] = cui__mc_time(ui, events[e].time, o->hour12);
    }
    p->line = CUI_C(ui, BORDER_SUBTLE);
    p->fg = CUI_C(ui, FOREGROUND); p->muted = CUI_C(ui, TEXT_MUTED);
    p->accent = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
    p->accent_fg = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, BACKGROUND) : CUI_C(ui, ACCENT_FOREGROUND);
    p->sel_bg = cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.045f);
    p->hover_bg = cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.025f);
    ui->nodes[grid].paint = cui__paint_month;
    ui->nodes[grid].paint_data = p;
    ui->nodes[grid].bg = CUI_C(ui, SURFACE);
    /* "+N more": a list of that day's events, anchored on its link */
    cui_popup *pp = cui__popup_get(ui, pid, true);
    if (pp && pp->open && st->more_day >= 0 && !compact) {
        int mrow = (st->more_day - start_day) / 7, mc = (st->more_day - start_day) % 7;
        if (st->more_day < start_day || mrow >= rows) cui__popup_set(ui, pid, false);
        else {
            cui__open(ui, grid);
            cui_rect gr2 = gr.w > 0 ? gr : (cui_rect){ 0, 0, 700, rows * cell_h };
            float cw = gr2.w / 7;
            cui_id aid = id ^ 0x6A1Fu;
            cui__absolute(ui, aid, mc * cw, mrow * cell_h + CUI__MC_HEAD + (lines - 1) * CUI__MC_LINE, cw, CUI__MC_LINE);
            cui__close(ui);
            if (cui__popup_begin(ui, pid, aid, CUI_PLACE_BOTTOM_START, 240, 10, 4, false)) {
                cui_date md = cui_date_from_days(st->more_day);
                int t = cui__text_node(ui, cui__frame_printf(ui, "%s %d", mn[md.month - 1], md.day), -1, CUI_FONT_MEDIUM,
                                       cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
                ui->nodes[t].line_height = 1.6f;
                for (int i = 0; i < count; i++) {
                    int a, b;
                    cui__mc_span(&events[i], &a, &b);
                    if (st->more_day < a || st->more_day > b) continue;
                    cui_id rid = cui__hash(&i, sizeof i, id ^ 0x6A20u);
                    cui_interact ir = cui__interact(ui, rid, CUI_IF_FOCUSABLE);
                    if (ir.clicked || ir.keyboard_activate) { res.event = i; cui__popup_set(ui, pid, false); }
                    int row = cui__node_new(ui, rid);
                    cui_node *rn = &ui->nodes[row];
                    rn->dir = CUI_ROW; rn->gap = 8; rn->align = CUI_ALIGN_CENTER; rn->w = CUI_FILL;
                    rn->pad[0] = rn->pad[2] = 5; rn->pad[1] = rn->pad[3] = 6;
                    rn->radius = CUI_CORNERS(6);
                    if (ir.hovered) { rn->bg = CUI_C(ui, HOVER); cui__set_cursor(ui, CUI_CURSOR_HAND); }
                    cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
                    cui__focus_ring(ui, row, ir.focus_visible ? 1.0f : 0.0f);
                    cui__open(ui, row);
                    int dot = cui__leaf(ui, 0, CUI_PX(8), CUI_PX(8));
                    ui->nodes[dot].radius = CUI_CORNERS(4);
                    ui->nodes[dot].bg = cui__tone_color(ui, events[i].tone ? events[i].tone : CUI_TONE_INFO);
                    int tt = cui__text_node(ui, events[i].title ? events[i].title : "", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
                    ui->nodes[tt].w = CUI_FILL; ui->nodes[tt].line_height = 1.3f;
                    bool bar = events[i].all_day || b > a;
                    int tm = cui__text_node(ui, bar ? "All day" : cui__mc_time(ui, events[i].time, o->hour12), -1, CUI_FONT_BODY,
                                            cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
                    ui->nodes[tm].flags |= CUI_NF_TABULAR;
                    cui__close(ui);
                }
                cui__popup_end(ui);
            }
        }
    }
    if (compact) {
        int ag = cui__mc_agenda(ui, id, events, count, sel_n >= 0 ? sel_n : st->focus_day, o);
        if (ag >= 0) res.event = ag;
    }
    cui__close(ui);
    cui_pop_id(ui);
#undef CUI__MC_SHIFT
    ui->last_id = gid;
    return res;
}
