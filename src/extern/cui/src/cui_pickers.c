/* Pickers and capture: multi-select, date range, time, wheel, time dial,
 * ruler and histogram sliders, signature pad, dropzone and avatar upload. */
#include "cui_internal.h"

#define cui__fs cui__fsz

static cui_color cui__accent_or_fg(cui_ctx *ui) {
    return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
}

static int cui__label_wrap_begin(cui_ctx *ui, const char *label, cui_size width) {
    if (!label) return -1;
    int col = cui__node_new(ui, 0);
    ui->nodes[col].gap = 6;
    ui->nodes[col].w = width.kind ? width : CUI_FILL;
    cui__open(ui, col);
    cui__field_label(ui, label);
    return col;
}

/* A field shell shared by the pickers: icon, text, trailing chevron. */
static int cui__picker_field(cui_ctx *ui, cui_id id, cui_interact r, float hover, bool open, cui_size width, float def, bool in_label) {
    float h = cui__control_height(ui, CUI_SIZE_MD);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 8;
    n->min_h = h;
    if (in_label) n->w = CUI_FILL; else cui__soft_width(n, width, def);
    n->pad[1] = CUI__FIELD_PAD_ICON; n->pad[3] = CUI__FIELD_PAD; n->pad[0] = n->pad[2] = 3;
    n->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, h * 0.5f));
    n->bg = CUI_C(ui, SURFACE);
    n->border = 1;
    cui_color border = cui__control_edge(ui, hover);
    n->border_color = cui_mix(border, cui__edge_on(ui), open ? 1.0f : 0.0f);
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    return i;
}

/* ------------------------------------------------------------------------ */
/* Multi-select                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { float hover; char query[96]; } cui__ms_state;
typedef struct { float t; } cui__check_paint;

static void cui__paint_check_box(cui_ctx *ui, cui_node *n, void *data) {
    cui__check_paint *p = data;
    cui_rect r = n->rect;
    cui_color on = CUI_C(ui, CONTROL_ON);
    cui_draw_rect_border(ui, r, CUI_CORNERS(5), cui_mix(CUI_C(ui, SURFACE), on, p->t), cui_mix(CUI_C(ui, BORDER_STRONG), on, p->t), 1);
    if (p->t > 0.02f) cui__draw_check(ui, cui__rect_inset(r, 3.5f), p->t, 1.6f, CUI_C(ui, CONTROL_GLYPH));
}

/* A menu row with a checkbox that toggles without closing the menu. */
static bool cui__check_item(cui_ctx *ui, const char *label, const char *icon, bool checked, int index, cui__menu_nav *nav) {
    if (nav) nav->item_count++;
    cui_id id = cui_make_id(ui, label);
    cui_interact r = cui__interact(ui, id, 0);
    if (r.hovered && nav && (ui->mx != ui->pmx || ui->my != ui->pmy || !nav->keyboard)) { nav->highlight = index; nav->keyboard = false; }
    bool hl = nav && nav->highlight == index;
    float *st = cui_state(ui, id, sizeof(float) * 2);
    cui__tween(ui, &st[0], hl ? 1.0f : 0.0f, CUI__T_HIGHLIGHT);
    cui__tween(ui, &st[1], checked ? 1.0f : 0.0f, CUI__T_STATE);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 10;
    n->h = CUI_PX(32); n->pad[1] = 10; n->pad[3] = 10;
    n->w = CUI_FILL; n->self_align = CUI_ALIGN_STRETCH;
    n->radius = CUI_CORNERS(CUI_MAX(6.0f, ui->theme.radius_panel - 6.0f));
    n->bg = cui_alpha(CUI_C(ui, ACTIVE), st[0]);
    cui__mark_interactive(ui, i, 0);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    int cb = cui__leaf(ui, 0, CUI_PX(16), CUI_PX(16));
    cui__check_paint *cp = cui__frame_alloc(ui, sizeof *cp);
    cp->t = st[1];
    ui->nodes[cb].paint = cui__paint_check_box;
    ui->nodes[cb].paint_data = cp;
    ui->nodes[cb].self_align = CUI_ALIGN_CENTER;
    if (icon) cui__icon_node(ui, icon, 16, CUI_C(ui, TEXT_SECONDARY));
    int t = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].w = CUI_FILL;
    cui__close(ui);
    bool toggle = r.clicked || (hl && (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE)));
    return toggle;
}

static bool cui__contains_nocase(const char *hay, const char *needle) {
    if (!needle || !*needle) return true;
    size_t nl = strlen(needle);
    for (const char *h = hay; *h; h++) {
        size_t k = 0;
        while (k < nl && h[k]) {
            char a = h[k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            k++;
        }
        if (k == nl) return true;
    }
    return false;
}

bool cui_multi_select_ex(cui_ctx *ui, const char *id_str, const char *const *items, int count, bool *selected, const cui_multi_select_opts *o) {
    cui_multi_select_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id pid = id ^ 0x3517u;
    cui__ms_state *st = cui_state(ui, id, sizeof *st);
    cui_popup *p = cui__popup_get(ui, pid, true);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    bool open = p && p->open;
    if (r.clicked && p && !(open && r.keyboard_activate)) { cui__popup_set(ui, pid, !open); p->at_mouse = false; st->query[0] = 0; }
    if (p && !p->open && r.focus_visible && cui_key_pressed(ui, CUI_KEY_DOWN)) cui__popup_set(ui, pid, true);
    open = p && p->open;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    bool changed = false;
    int col = cui__label_wrap_begin(ui, o->label, o->width);
    int field = cui__picker_field(ui, id, r, st->hover, open, o->width, 300, col >= 0);
    cui__open(ui, field);
    int max_chips = o->max_chips > 0 ? o->max_chips : 3;
    int n_sel = 0, shown = 0;
    for (int k = 0; k < count; k++) n_sel += selected[k] ? 1 : 0;
    int chips = cui__node_new(ui, 0);
    ui->nodes[chips].dir = CUI_ROW; ui->nodes[chips].gap = 4; ui->nodes[chips].w = CUI_FILL;
    ui->nodes[chips].align = CUI_ALIGN_CENTER; ui->nodes[chips].flags |= CUI_NF_CLIP;
    ui->nodes[chips].min_h = cui__control_height(ui, CUI_SIZE_MD) - 8;
    cui__open(ui, chips);
    cui_push_id(ui, id_str);
    for (int k = 0; k < count && shown < max_chips; k++) {
        if (!selected[k]) continue;
        shown++;
        int chip = cui__node_new(ui, 0);
        cui_node *cn = &ui->nodes[chip];
        cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 1;
        cn->h = CUI_PX(24); cn->pad[3] = 9; cn->pad[1] = 3;
        cn->radius = CUI_CORNERS(12);
        cn->bg = CUI_C(ui, ACTIVE);
        cn->self_align = CUI__ALIGN_NOSTRETCH;
        cui__open(ui, chip);
        int t = cui__text_node(ui, items[k], cui__display_len(items[k]), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = 1.2f;
        cui_button_opts xo;
        memset(&xo, 0, sizeof xo);
        xo.size = CUI_SIZE_SM;
        cui_push_id_int(ui, k);
        if (cui_icon_button_ex(ui, CUI_ICON_X, &xo)) { selected[k] = false; changed = true; }
        cui_pop_id(ui);
        ui->nodes[ui->last_node].w = CUI_PX(18); ui->nodes[ui->last_node].h = CUI_PX(18);
        cui__close(ui);
    }
    cui_pop_id(ui);
    if (n_sel > shown) {
        cui_badge_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM;
        cui_badge_ex(ui, cui__frame_printf(ui, "+%d", n_sel - shown), &bo);
    }
    if (n_sel == 0) {
        int t = cui__text_node(ui, o->placeholder ? o->placeholder : "Select", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[t].line_height = 1.3f;
        ui->nodes[chips].pad[3] = 2;
    }
    cui__close(ui);
    cui__icon_node(ui, CUI_ICON_CHEVRONS_UP_DOWN, 16, CUI_C(ui, TEXT_MUTED));
    cui__close(ui);
    cui__focus_ring(ui, field, r.focus_visible ? 1.0f : 0.0f);
    if (col >= 0) cui__close(ui);
    ui->last_id = id;
    if (cui__popup_begin(ui, pid, id, CUI_PLACE_BOTTOM_START, 0, 6, 0, true)) {
        if (o->searchable) {
            cui_input_opts io;
            memset(&io, 0, sizeof io);
            io.placeholder = "Search";
            io.size = CUI_SIZE_SM;
            cui_id sid = cui_make_id(ui, "#q");
            if (p && p->opened_frame == ui->frame - 1) ui->focus = sid;
            int box = cui__node_new(ui, 0);
            ui->nodes[box].pad[0] = 2; ui->nodes[box].pad[2] = 4;
            cui__open(ui, box);
            cui__text_field(ui, sid, st->query, sizeof st->query, &io, false, 0, CUI_ICON_SEARCH);
            cui__close(ui);
        }
        /* header: how many are chosen, and a way to clear them */
        int head = cui__node_new(ui, 0);
        ui->nodes[head].dir = CUI_ROW; ui->nodes[head].align = CUI_ALIGN_CENTER;
        ui->nodes[head].pad[0] = 2; ui->nodes[head].pad[2] = 2; ui->nodes[head].pad[3] = 10; ui->nodes[head].pad[1] = 2;
        cui__open(ui, head);
        int ht = cui__text_node(ui, n_sel ? cui__frame_printf(ui, "%d selected", n_sel) : "None selected", -1, CUI_FONT_MEDIUM,
                                cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[ht].line_height = 1.3f;
        ui->nodes[ht].w = CUI_FILL;
        cui_button_opts co;
        memset(&co, 0, sizeof co);
        co.size = CUI_SIZE_SM; co.variant = CUI_BUTTON_GHOST;
        bool all = n_sel == count;
        if (cui_button_ex(ui, all ? "Clear##ms" : "Select all##ms", &co)) {
            for (int k = 0; k < count; k++) selected[k] = !all;
            changed = true;
        }
        ui->nodes[ui->last_node].h = CUI_PX(24);
        cui__close(ui);
        cui__menu_nav *nav = cui_state(ui, pid ^ 0x4E4156u, sizeof *nav);
        cui_box sb;
        memset(&sb, 0, sizeof sb);
        sb.max_height = 280;
        cui__scroll_begin_ex(ui, cui_make_id(ui, "#list"), &sb, false);
        ui->nodes[cui__current(ui)].h = CUI_FIT;
        ui->nodes[cui__current(ui)].min_h = 0;
        int index = 0, visible = 0;
        for (int k = 0; k < count; k++) {
            if (o->searchable && !cui__contains_nocase(items[k], st->query)) continue;
            cui_push_id_int(ui, k);
            if (cui__check_item(ui, items[k], o->icons ? o->icons[k] : NULL, selected[k], index, nav)) { selected[k] = !selected[k]; changed = true; }
            cui_pop_id(ui);
            index++;
            visible++;
        }
        if (visible == 0) {
            int e = cui__node_new(ui, 0);
            ui->nodes[e].pad[0] = ui->nodes[e].pad[2] = 10; ui->nodes[e].pad[3] = 10;
            cui__open(ui, e);
            cui__text_node(ui, "No results", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
            cui__close(ui);
        }
        cui__scroll_end_ex(ui);
        cui__popup_end(ui);
    }
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Dates                                                                    */
/* ------------------------------------------------------------------------ */

int cui_date_to_days(cui_date d) {
    int y = d.year, m = d.month, dd = d.day;
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

cui_date cui_date_from_days(int z) {
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    cui_date r = { y + (m <= 2), (int)m, (int)dd };
    return r;
}

static int cui__dow(int days) { int w = (days + 3) % 7; return w < 0 ? w + 7 : w; }   /* 0 = Monday */
static int cui__month_days(int y, int m) {
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[(m - 1 + 12) % 12];
}
static const char *cui__mon[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

typedef struct { int view_y, view_m; bool init, picking; int hover; } cui__rcal_state;

typedef struct {
    bool band, band_l, band_r;   /* band through the cell, rounded on the left / right */
    bool knob;                   /* start or end */
    bool today;
    float hover;
    cui_color band_c, knob_c, hover_c, ring_c;
} cui__rday_paint;

static void cui__paint_range_day(cui_ctx *ui, cui_node *n, void *data) {
    cui__rday_paint *p = data;
    cui_rect r = n->rect;
    float s = CUI_MIN(r.w, r.h);
    cui_rect circle = { r.x + (r.w - s) * 0.5f, r.y + (r.h - s) * 0.5f, s, s };
    if (p->band) {
        cui_rect b = { r.x, r.y + 1, r.w, r.h - 2 };
        cui_corners c = { p->band_l ? b.h * 0.5f : 0, p->band_r ? b.h * 0.5f : 0, p->band_r ? b.h * 0.5f : 0, p->band_l ? b.h * 0.5f : 0 };
        cui_draw_rect(ui, b, c, p->band_c);
    }
    if (p->knob) cui_draw_rect(ui, cui__rect_inset(circle, 1), CUI_CORNERS(s * 0.5f - 1), p->knob_c);
    else if (p->hover > 0.01f) cui_draw_rect(ui, cui__rect_inset(circle, 1), CUI_CORNERS(s * 0.5f - 1), cui_alpha(p->hover_c, p->hover));
    if (p->today && !p->knob) cui_draw_rect_border(ui, cui__rect_inset(circle, 1), CUI_CORNERS(s * 0.5f - 1), cui__rgba(0, 0, 0, 0), p->ring_c, 1);
}

static void cui__range_month(cui_ctx *ui, cui_id cal_id, cui__rcal_state *st, int y, int m, cui_date_range *range, bool *changed,
                             bool prev_btn, bool next_btn, int today_days, int *new_hover) {
    int col = cui__node_new(ui, 0);
    ui->nodes[col].gap = 8;
    ui->nodes[col].w = CUI_PX(7 * 36);
    cui__open(ui, col);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].align = CUI_ALIGN_CENTER;
    ui->nodes[head].h = CUI_PX(30);
    cui__open(ui, head);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM;
    if (prev_btn) {
        bo.tooltip = "Previous month";
        if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_LEFT, &bo)) { if (--st->view_m < 1) { st->view_m = 12; st->view_y--; } }
    } else cui_gap(ui, 30);
    int ht = cui__text_node(ui, cui__frame_printf(ui, "%s %d", cui__mon[m - 1], y), -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body),
                            CUI_C(ui, FOREGROUND), false);
    ui->nodes[ht].w = CUI_FILL;
    ui->nodes[ht].text_align = CUI_TEXT_CENTER;
    ui->nodes[ht].line_height = 1.3f;
    if (next_btn) {
        bo.tooltip = "Next month";
        if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_RIGHT, &bo)) { if (++st->view_m > 12) { st->view_m = 1; st->view_y++; } }
    } else cui_gap(ui, 30);
    cui__close(ui);
    cui_box gb;
    memset(&gb, 0, sizeof gb);
    gb.dir = CUI_GRID; gb.columns = 7; gb.gap = 0;
    int grid = cui__node_new(ui, 0);
    cui__apply_box(ui, &ui->nodes[grid], &gb);
    ui->nodes[grid].w = CUI_FILL;
    cui__open(ui, grid);
    static const char *wd[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    for (int k = 0; k < 7; k++) {
        int t = cui__text_node(ui, wd[k], -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[t].text_align = CUI_TEXT_CENTER;
        ui->nodes[t].line_height = 2.2f;
    }
    int first = cui_date_to_days((cui_date){ y, m, 1 });
    int lead = cui__dow(first);
    int dim = cui__month_days(y, m);
    int s = range->start.year ? cui_date_to_days(range->start) : 0;
    int e = range->end.year ? cui_date_to_days(range->end) : 0;
    bool have_s = range->start.year != 0, have_e = range->end.year != 0;
    /* while picking the end, the hovered day previews it */
    if (st->picking && have_s && st->hover) { e = st->hover; have_e = true; }
    int lo = have_e ? CUI_MIN(s, e) : s, hi = have_e ? CUI_MAX(s, e) : s;
    cui_color band = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, ACTIVE) : cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, ACCENT), ui->mode == CUI_THEME_DARK ? 0.22f : 0.14f);
    for (int cell = 0; cell < 42; cell++) {
        int day = cell - lead + 1;
        if (day < 1 || day > dim) {
            if (cell >= 35 && day > dim) break;
            int blank = cui__leaf(ui, 0, CUI_FILL, CUI_PX(36));
            (void)blank;
            continue;
        }
        int days = first + day - 1;
        cui_id did = cui__hash(&days, sizeof days, cal_id);
        cui_interact r = cui__interact(ui, did, CUI_IF_FOCUSABLE);
        if (r.hovered) *new_hover = days;
        float hv_v = cui__hover_fade(ui, did, r.hovered, CUI__T_HOVER), *hv = &hv_v;
        bool in = have_s && days >= lo && days <= hi && (have_e && hi > lo);
        bool is_end = have_s && (days == s || (have_e && days == e));
        int wdk = cui__dow(days);
        cui__rday_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->band = in;
        p->band_l = days == lo || wdk == 0 || day == 1;
        p->band_r = days == hi || wdk == 6 || day == dim;
        p->knob = is_end;
        p->today = days == today_days;
        p->hover = *hv * 1.4f;
        p->band_c = band;
        p->knob_c = CUI_C(ui, CONTROL_ON);
        p->hover_c = CUI_C(ui, HOVER);
        p->ring_c = CUI_C(ui, BORDER_STRONG);
        int c = cui__node_new(ui, did);
        cui_node *cn = &ui->nodes[c];
        cn->h = CUI_PX(36); cn->w = CUI_FILL;
        cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->justify = CUI_JUSTIFY_CENTER;
        cn->radius = CUI_CORNERS(18);
        cn->paint = cui__paint_range_day;
        cn->paint_data = p;
        cui__mark_interactive(ui, c, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, c);
        const char *buf = cui__frame_printf(ui, "%d", day);
        int t = cui__text_node(ui, buf, -1, is_end ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body),
                               is_end ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = 1.0f;
        ui->nodes[t].flags |= CUI_NF_TABULAR;
        cui__close(ui);
        cui__focus_ring(ui, c, r.focus_visible ? 1.0f : 0.0f);
        if (r.clicked) {
            cui_date dd = cui_date_from_days(days);
            if (!st->picking || !range->start.year) {
                range->start = dd;
                range->end = (cui_date){ 0, 0, 0 };
                st->picking = true;
            } else {
                int sd = cui_date_to_days(range->start);
                if (days < sd) { range->end = range->start; range->start = dd; }
                else range->end = dd;
                st->picking = false;
            }
            *changed = true;
        }
    }
    cui__close(ui);
    cui__close(ui);
}

bool cui_range_calendar(cui_ctx *ui, const char *id_str, cui_date_range *range, int months) {
    if (months < 1) months = 1;
    if (months > 2) months = 2;
    cui_id id = cui_make_id(ui, id_str);
    cui__rcal_state *st = cui_state(ui, id, sizeof *st);
    cui_date today = cui_date_today();
    if (!st->init) {
        cui_date base = range->start.year ? range->start : today;
        st->view_y = base.year; st->view_m = base.month;
        st->init = true;
    }
    bool changed = false;
    int new_hover = 0;
    cui_push_id(ui, id_str);
    int row = cui__node_new(ui, id);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 24; ui->nodes[row].align = CUI_ALIGN_START;
    ui->nodes[row].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    int td = cui_date_to_days(today);
    for (int k = 0; k < months; k++) {
        int m = st->view_m + k, y = st->view_y;
        if (m > 12) { m -= 12; y++; }
        /* the preview uses the day hovered last frame (st->hover) */
        cui__range_month(ui, id, st, y, m, range, &changed, k == 0, k == months - 1, td, &new_hover);
    }
    cui__close(ui);
    cui_pop_id(ui);
    st->hover = new_hover;
    ui->last_id = id;
    return changed;
}

typedef struct { float hover; int preset; } cui__drp_state;

static void cui__fmt_range(char *out, size_t cap, cui_date_range r) {
    if (!r.start.year) { out[0] = 0; return; }
    if (!r.end.year) { snprintf(out, cap, "%.3s %d, %d", cui__mon[r.start.month - 1], r.start.day, r.start.year); return; }
    if (r.start.year == r.end.year)
        snprintf(out, cap, "%.3s %d \xe2\x80\x93 %.3s %d, %d", cui__mon[r.start.month - 1], r.start.day, cui__mon[r.end.month - 1], r.end.day, r.end.year);
    else
        snprintf(out, cap, "%.3s %d, %d \xe2\x80\x93 %.3s %d, %d", cui__mon[r.start.month - 1], r.start.day, r.start.year,
                 cui__mon[r.end.month - 1], r.end.day, r.end.year);
}

/* The preset ranges, as menu items in the current container. */
static bool cui__range_presets(cui_ctx *ui, cui_date_range *range) {
    static const char *names[] = { "Today", "Yesterday", "Last 7 days", "Last 30 days", "This month", "Last month", "This year" };
    bool changed = false;
    cui_date today = cui_date_today();
    int td = cui_date_to_days(today);
    for (int k = 0; k < 7; k++) {
        cui_menu_item_opts mo;
        memset(&mo, 0, sizeof mo);
        cui_date_range pr = { today, today };
        switch (k) {
        case 1: pr.start = pr.end = cui_date_from_days(td - 1); break;
        case 2: pr.start = cui_date_from_days(td - 6); break;
        case 3: pr.start = cui_date_from_days(td - 29); break;
        case 4: pr.start = (cui_date){ today.year, today.month, 1 }; break;
        case 5: {
            int pm = today.month == 1 ? 12 : today.month - 1, py = today.month == 1 ? today.year - 1 : today.year;
            pr.start = (cui_date){ py, pm, 1 };
            pr.end = (cui_date){ py, pm, cui__month_days(py, pm) };
            break;
        }
        case 6: pr.start = (cui_date){ today.year, 1, 1 }; break;
        default: break;
        }
        mo.checked = range->start.year && cui_date_to_days(range->start) == cui_date_to_days(pr.start) &&
                     range->end.year && cui_date_to_days(range->end) == cui_date_to_days(pr.end);
        cui_push_id_int(ui, k);
        /* menu items close every popup when chosen, which is what a preset should do */
        if (cui_menu_item_ex(ui, names[k], &mo)) { *range = pr; changed = true; }
        cui_pop_id(ui);
    }
    return changed;
}

bool cui_date_range_picker_ex(cui_ctx *ui, const char *id_str, cui_date_range *range, const cui_date_range_opts *o) {
    cui_date_range_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id pid = id ^ 0xDA7Fu;
    cui__drp_state *st = cui_state(ui, id, sizeof *st);
    cui_popup *p = cui__popup_get(ui, pid, true);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    if (r.clicked && p) { cui__popup_set(ui, pid, !p->open); p->at_mouse = false; }
    bool open = p && p->open;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int col = cui__label_wrap_begin(ui, o->label, o->width);
    int field = cui__picker_field(ui, id, r, st->hover, open, o->width, 280, col >= 0);
    ui->nodes[field].h = CUI_PX(cui__control_height(ui, CUI_SIZE_MD));
    cui__open(ui, field);
    cui__icon_node(ui, CUI_ICON_CALENDAR, 16, CUI_C(ui, TEXT_MUTED));
    char txt[96];
    cui__fmt_range(txt, sizeof txt, *range);
    bool has = txt[0] != 0;
    int t = cui__text_node(ui, has ? cui__frame_strdup(ui, txt, -1) : (o->placeholder ? o->placeholder : "Pick a date range"), -1, CUI_FONT_BODY,
                           cui__fs(ui, ui->theme.text_body), has ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].w = CUI_FILL;
    cui__close(ui);
    cui__focus_ring(ui, field, r.focus_visible ? 1.0f : 0.0f);
    if (col >= 0) cui__close(ui);
    ui->last_id = id;
    bool changed = false;
    if (cui__popup_begin(ui, pid, id, CUI_PLACE_BOTTOM_START, 0, 0, 0, false)) {
        /* The popover adapts to the screen: presets beside two months, beside
         * one month, or above one month on a phone. */
        float avail = ui->vw - 16, month = 7 * 36, cal_w = month + 28, side = 157;
        bool presets = !o->no_presets;
        bool beside = presets && avail >= side + cal_w;
        ui->nodes[cui__current(ui)].max_h = 1e9f;   /* presets above a month are taller than a menu; the viewport still limits it */
        int months = avail >= (beside ? side : 0) + cal_w + month + 24 ? 2 : 1;
        int row = cui__node_new(ui, 0);
        ui->nodes[row].dir = beside || !presets ? CUI_ROW : CUI_COLUMN; ui->nodes[row].align = CUI_ALIGN_STRETCH;
        cui__open(ui, row);
        if (presets) {
            int pc = cui__node_new(ui, 0);
            ui->nodes[pc].gap = 2;
            if (beside) ui->nodes[pc].w = CUI_PX(156);
            else { ui->nodes[pc].dir = CUI_GRID; ui->nodes[pc].columns = 2; }
            ui->nodes[pc].pad[0] = ui->nodes[pc].pad[1] = ui->nodes[pc].pad[2] = ui->nodes[pc].pad[3] = 8;
            cui__open(ui, pc);
            if (cui__range_presets(ui, range)) changed = true;
            cui__close(ui);
            int sep = beside ? cui__leaf(ui, 0, CUI_PX(1), CUI_FILL) : cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
            ui->nodes[sep].bg = CUI_C(ui, BORDER);
        }
        int cal = cui__node_new(ui, 0);
        ui->nodes[cal].pad[0] = ui->nodes[cal].pad[1] = ui->nodes[cal].pad[2] = ui->nodes[cal].pad[3] = 14;
        cui__open(ui, cal);
        cui__rcal_state *cs = cui_state(ui, cui_make_id(ui, "#rcal"), sizeof *cs);
        bool was_picking = cs->picking;
        if (cui_range_calendar(ui, "#rcal", range, months)) {
            changed = true;
            if (was_picking && range->end.year) cui__popup_set(ui, pid, false);   /* a full range closes the picker */
        }
        cui__close(ui);
        cui__close(ui);
        cui__popup_end(ui);
    }
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Time picker                                                              */
/* ------------------------------------------------------------------------ */

typedef struct { float hover; } cui__tp_state;

static void cui__paint_column_fade(cui_ctx *ui, cui_node *n, void *data) {
    (void)data;
    cui_rect r = n->rect;
    r.x += n->pad[3]; r.w -= n->pad[1] + n->pad[3]; r.y += n->pad[0]; r.h -= n->pad[0] + n->pad[2];   /* the content box */
    cui_color bg = CUI_C(ui, SURFACE_RAISED), clear = bg;
    clear.a = 0;
    float f = 34;
    cui_draw_rect_gradient(ui, (cui_rect){ r.x, r.y, r.w, f }, CUI_CORNERS(0), bg, clear);
    cui_draw_rect_gradient(ui, (cui_rect){ r.x, r.y + r.h - f, r.w, f }, CUI_CORNERS(0), clear, bg);
}

static bool cui__time_column(cui_ctx *ui, const char *sid, int count, int value, int step, int offset, bool twelve, int *out, bool scroll_now) {
    bool changed = false;
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_PX(58);
    sb.height = CUI_PX(216);
    sb.gap = 2;
    sb.pad = 4;
    sb.pad_top = sb.pad_bottom = (216 - 32) * 0.5f;   /* any item can sit in the middle, level with the other columns */
    cui_id scid = cui_make_id(ui, sid);
    cui__scroll_begin_ex(ui, scid, &sb, false);
    int sel_index = -1;
    for (int k = 0, idx = 0; k < count; k += step, idx++) {
        int v = k + offset;
        bool sel = v == value;
        if (sel) sel_index = idx;
        cui_id iid = cui__hash(&k, sizeof k, scid);
        cui_interact r = cui__interact(ui, iid, 0);
        float hv_v = cui__hover_fade(ui, iid, r.hovered, CUI__T_HIGHLIGHT), *hv = &hv_v;
        int it = cui__node_new(ui, iid);
        cui_node *n = &ui->nodes[it];
        n->h = CUI_PX(32); n->w = CUI_FILL;
        n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->justify = CUI_JUSTIFY_CENTER;
        n->radius = CUI_CORNERS(CUI__R_ITEM(ui));
        n->bg = sel ? CUI_C(ui, CONTROL_ON) : cui_alpha(CUI_C(ui, HOVER), *hv * 1.3f);
        cui__mark_interactive(ui, it, 0);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, it);
        const char *txt = twelve ? cui__frame_printf(ui, "%d", v == 0 ? 12 : v) : cui__frame_printf(ui, "%02d", v);
        int t = cui__text_node(ui, txt, -1, sel ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body),
                               sel ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].flags |= CUI_NF_TABULAR;
        ui->nodes[t].line_height = 1.0f;
        cui__close(ui);
        if (r.clicked) { *out = v; changed = true; }
    }
    cui__scroll_end_ex(ui);
    if (scroll_now && sel_index >= 0) {
        cui_scroll_state *ss = cui_state(ui, scid, sizeof *ss);
        ss->ty = ss->y = sel_index * 34.0f;
    }
    return changed;
}

bool cui_time_picker_ex(cui_ctx *ui, const char *id_str, int *hour, int *minute, const cui_time_opts *o) {
    cui_time_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int step = o->minute_step > 0 ? o->minute_step : 5;
    cui_id id = cui_make_id(ui, id_str);
    cui_id pid = id ^ 0x7177u;
    cui__tp_state *st = cui_state(ui, id, sizeof *st);
    cui_popup *p = cui__popup_get(ui, pid, true);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    if (r.clicked && p) { cui__popup_set(ui, pid, !p->open); p->at_mouse = false; }
    bool open = p && p->open;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int h = CUI_CLAMP(*hour, 0, 23), m = CUI_CLAMP(*minute, 0, 59);
    int col = cui__label_wrap_begin(ui, o->label, o->width);
    int field = cui__picker_field(ui, id, r, st->hover, open, o->width, 150, col >= 0);
    ui->nodes[field].h = CUI_PX(cui__control_height(ui, CUI_SIZE_MD));
    cui__open(ui, field);
    cui__icon_node(ui, CUI_ICON_CLOCK, 16, CUI_C(ui, TEXT_MUTED));
    const char *txt = o->twelve_hour ? cui__frame_printf(ui, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM")
                                     : cui__frame_printf(ui, "%02d:%02d", h, m);
    int t = cui__text_node(ui, txt, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].flags |= CUI_NF_TABULAR;
    ui->nodes[t].w = CUI_FILL;
    cui__close(ui);
    cui__focus_ring(ui, field, r.focus_visible ? 1.0f : 0.0f);
    if (col >= 0) cui__close(ui);
    ui->last_id = id;
    bool changed = false;
    if (cui__popup_begin(ui, pid, id, CUI_PLACE_BOTTOM_START, 0, 6, 0, false)) {
        bool scroll_now = p && ui->frame - p->opened_frame <= 2;
        int row = cui__node_new(ui, 0);
        ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 4;
        /* inset from the popover's rounded corners, so nothing pokes into them */
        ui->nodes[row].pad[0] = ui->nodes[row].pad[2] = 6; ui->nodes[row].pad[1] = ui->nodes[row].pad[3] = 4;
        ui->nodes[row].paint = cui__paint_column_fade;   /* rows fade out at the top and bottom edges */
        ui->nodes[row].paint_data = NULL;
        ui->nodes[row].flags |= CUI_NF_PAINT_AFTER;
        cui__open(ui, row);
        int nh = h, nm = m;
        if (o->twelve_hour) {
            int h12 = h % 12;
            if (cui__time_column(ui, "#h", 12, h12, 1, 0, true, &h12, scroll_now)) { nh = h12 + (h >= 12 ? 12 : 0); changed = true; }
        } else if (cui__time_column(ui, "#h", 24, h, 1, 0, false, &nh, scroll_now)) changed = true;
        int sep = cui__leaf(ui, 0, CUI_PX(1), CUI_FILL);
        ui->nodes[sep].bg = CUI_C(ui, BORDER);
        if (cui__time_column(ui, "#m", 60, m, step, 0, false, &nm, scroll_now)) changed = true;
        if (o->twelve_hour) {
            int sep2 = cui__leaf(ui, 0, CUI_PX(1), CUI_FILL);
            ui->nodes[sep2].bg = CUI_C(ui, BORDER);
            int ap = cui__node_new(ui, 0);
            ui->nodes[ap].gap = 2; ui->nodes[ap].pad[1] = ui->nodes[ap].pad[3] = 4;
            ui->nodes[ap].pad[0] = (216 - 32) * 0.5f - (h >= 12 ? 34 : 0);   /* the chosen half sits in the middle row */
            ui->nodes[ap].h = CUI_PX(216);
            cui__open(ui, ap);
            for (int k = 0; k < 2; k++) {
                bool sel = (h >= 12) == (k == 1);
                cui_id aid = cui__hash(&k, sizeof k, id ^ 0xA9u);
                cui_interact ar = cui__interact(ui, aid, 0);
                int it = cui__node_new(ui, aid);
                cui_node *n = &ui->nodes[it];
                n->h = CUI_PX(32); n->w = CUI_PX(50);
                n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->justify = CUI_JUSTIFY_CENTER;
                n->radius = CUI_CORNERS(CUI__R_ITEM(ui));
                n->bg = sel ? CUI_C(ui, CONTROL_ON) : (ar.hovered ? CUI_C(ui, HOVER) : cui__rgba(0, 0, 0, 0));
                cui__mark_interactive(ui, it, 0);
                cui__open(ui, it);
                int tt = cui__text_node(ui, k ? "PM" : "AM", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm),
                                        sel ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, FOREGROUND), false);
                ui->nodes[tt].line_height = 1.0f;
                cui__close(ui);
                if (ar.clicked && !sel) { nh = (h + 12) % 24; changed = true; }
            }
            cui__close(ui);
        }
        cui__close(ui);
        *hour = nh; *minute = nm;
        cui__popup_end(ui);
    }
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Wheel picker                                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_spring pos;            /* position in items (float index) */
    bool dragging, init;
    float press_y, press_pos, vel, last_y;
    double last_t;
    float wheel_acc;
} cui__wheel_state;

typedef struct {
    const char *const *items;
    int count;
    float pos;
    int rows;
    float row_h;
    bool loop, join_l, join_r;
    cui_color band, fg, dim;
    float font_size;
} cui__wheel_paint;

static void cui__paint_wheel(cui_ctx *ui, cui_node *n, void *data) {
    cui__wheel_paint *p = data;
    cui_rect r = n->rect;
    float cy = r.y + r.h * 0.5f;
    float bl = p->join_l ? 0 : 4, br = p->join_r ? 0 : 4;
    cui_corners bc = { p->join_l ? 0 : 9, p->join_r ? 0 : 9, p->join_r ? 0 : 9, p->join_l ? 0 : 9 };
    cui_draw_rect(ui, (cui_rect){ r.x + bl, cy - p->row_h * 0.5f, r.w - bl - br, p->row_h }, bc, p->band);
    cui_push_clip(ui, r);
    const float step = 0.36f;             /* radians per row on the drum */
    float radius = p->row_h / step;
    int center = (int)floorf(p->pos + 0.5f);
    int reach = p->rows / 2 + 2;
    float lh = cui__font_line_height(ui, CUI_FONT_BODY, p->font_size);
    for (int k = center - reach; k <= center + reach; k++) {
        int idx = k;
        if (p->loop) { idx %= p->count; if (idx < 0) idx += p->count; }
        else if (k < 0 || k >= p->count) continue;
        float off = (float)k - p->pos;
        float ang = off * step;
        if (fabsf(ang) > 1.45f) continue;
        float y = cy + sinf(ang) * radius;
        float a = cosf(ang);
        float near = 1.0f - cui__clampf(fabsf(off), 0, 1);
        cui_color c = cui_mix(p->dim, p->fg, near);
        /* fade out before the rows reach the edges, so nothing looks cut */
        float edge = cui__clampf((p->rows * 0.5f - fabsf(off)) / 0.9f, 0, 1);
        c = cui_alpha(c, cui__clampf(a * a * 1.1f, 0, 1) * edge);
        const char *s = p->items[idx];
        int len = cui__display_len(s);
        float w = cui__text_width(ui, CUI_FONT_BODY, p->font_size, s, len, true);
        cui__draw_text_line(ui, CUI_FONT_BODY, p->font_size, r.x + (r.w - w) * 0.5f, y - lh * 0.5f, c, s, len, true);
    }
    cui_pop_clip(ui);
}

bool cui_wheel_picker_ex(cui_ctx *ui, const char *id_str, const char *const *items, int count, int *index, const cui_wheel_opts *o) {
    cui_wheel_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (count <= 0) return false;
    int rows = o->rows > 0 ? (o->rows | 1) : 5;
    float row_h = 34;
    cui_id id = cui_make_id(ui, id_str);
    cui__wheel_state *st = cui_state(ui, id, sizeof *st);
    int old = *index;
    if (*index < 0) *index = 0;
    if (*index >= count) *index = count - 1;
    if (!st->init) { st->pos.x = (float)*index; st->init = true; }
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    float lo = o->loop ? -1e9f : 0, hi = o->loop ? 1e9f : (float)(count - 1);
    if (r.hovered && ui->mpressed[0]) {
        st->dragging = true; st->press_y = ui->my; st->press_pos = st->pos.x;
        st->vel = 0; st->last_y = ui->my; st->last_t = ui->time;
    }
    float target = (float)*index;
    /* keep the spring near the index for looping wheels */
    if (o->loop) {
        float base = st->pos.x;
        float cand = (float)*index + roundf((base - (float)*index) / count) * count;
        target = cand;
    }
    if (st->dragging) {
        if (ui->mdown[0]) {
            float p = st->press_pos - (ui->my - st->press_y) / row_h;
            if (!o->loop) {
                if (p < 0) p = p * 0.3f;
                if (p > count - 1) p = (count - 1) + (p - (count - 1)) * 0.3f;
            }
            double dt = ui->time - st->last_t;
            if (dt > 0.001) { st->vel = cui__lerp(st->vel, -(ui->my - st->last_y) / row_h / (float)dt, 0.4f); st->last_t = ui->time; st->last_y = ui->my; }
            st->pos.x = p; st->pos.v = 0;
            ui->active = id;
            ui->animating = true;
            int near = (int)floorf(p + 0.5f);
            if (o->loop) { near %= count; if (near < 0) near += count; }
            *index = CUI_CLAMP(near, 0, count - 1);
            target = st->pos.x;
        } else {
            st->dragging = false;
            float land = st->pos.x + cui__clampf(st->vel, -40, 40) * 0.12f;
            land = roundf(cui__clampf(land, lo, hi));
            target = land;
            int k = (int)land;
            if (o->loop) { k %= count; if (k < 0) k += count; }
            *index = CUI_CLAMP(k, 0, count - 1);
            st->vel = 0;
        }
    }
    if (r.hovered && ui->scroll_dy != 0 && !st->dragging) {
        st->wheel_acc -= ui->scroll_dy;
        int steps = (int)st->wheel_acc;
        if (steps == 0) steps = st->wheel_acc > 0 ? 1 : -1;
        st->wheel_acc = 0;
        int k = *index + steps;
        if (o->loop) { k %= count; if (k < 0) k += count; target += (float)steps; }
        else { k = CUI_CLAMP(k, 0, count - 1); target = (float)k; }
        *index = k;
        ui->scroll_dy = 0;
    }
    if (ui->focus == id) {
        int dk = 0;
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) dk = 1;
        if (cui_key_pressed(ui, CUI_KEY_UP)) dk = -1;
        if (dk) {
            int k = *index + dk;
            if (o->loop) { k %= count; if (k < 0) k += count; target += (float)dk; }
            else { k = CUI_CLAMP(k, 0, count - 1); target = (float)k; }
            *index = k;
        }
    }
    if (!st->dragging) cui__spring(ui, &st->pos, target, 0.34f, 0.05f);
    int node = cui__leaf(ui, id, o->width.kind ? o->width : CUI_PX(84), CUI_PX(rows * row_h));
    cui_node *n = &ui->nodes[node];
    n->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    n->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, node, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    if (r.hovered) cui__set_cursor(ui, st->dragging ? CUI_CURSOR_RESIZE_NS : CUI_CURSOR_HAND);
    cui__wheel_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->items = items; p->count = count; p->pos = st->pos.x; p->rows = rows; p->row_h = row_h; p->loop = o->loop;
    p->join_l = o->join_left; p->join_r = o->join_right;
    p->band = CUI_C(ui, ACTIVE);
    p->fg = CUI_C(ui, FOREGROUND);
    p->dim = CUI_C(ui, TEXT_MUTED);
    p->font_size = cui__fs(ui, ui->theme.text_body + 2);
    n->paint = cui__paint_wheel;
    n->paint_data = p;
    cui__focus_ring(ui, node, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = id;
    return *index != old;
}

/* ------------------------------------------------------------------------ */
/* Time dial                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { int mode; cui_spring ang, len, swap; bool init, dragging; } cui__dial2_state;

typedef struct {
    int mode, hour, minute;
    float ang, len, swap;
    cui_color face, fg, dim, hand, glyph;
} cui__tdial_paint;

static void cui__paint_time_dial(cui_ctx *ui, cui_node *n, void *data) {
    cui__tdial_paint *p = data;
    cui_rect r = n->rect;
    float R = CUI_MIN(r.w, r.h) * 0.5f;
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    cui_draw_circle(ui, c, R, p->face);
    float outer = R - 22, inner = R - 56;
    float fs_o = R * 0.13f, fs_i = R * 0.10f;
    /* hand under the labels */
    float hl = cui__lerp(inner, outer, p->len);
    cui_vec2 tip = { c.x + sinf(p->ang) * hl, c.y - cosf(p->ang) * hl };
    cui_draw_line(ui, c, tip, 2, p->hand);
    cui_draw_circle(ui, c, 3.5f, p->hand);
    cui_draw_circle(ui, tip, 17, p->hand);
    for (int k = 0; k < 12; k++) {
        float a = k * CUI_PI / 6;
        if (p->mode == 0) {
            int ho = k == 0 ? 12 : k, hi = k == 0 ? 0 : k + 12;
            const char *so = cui__frame_printf(ui, "%d", ho);
            const char *si = cui__frame_printf(ui, "%02d", hi);
            cui_vec2 po = { c.x + sinf(a) * outer, c.y - cosf(a) * outer };
            cui_vec2 pi = { c.x + sinf(a) * inner, c.y - cosf(a) * inner };
            bool so_sel = p->hour == ho % 24 && ho != 12 ? true : (ho == 12 && p->hour == 12);
            bool si_sel = p->hour == (hi % 24);
            float wo = cui__text_width(ui, CUI_FONT_MEDIUM, fs_o, so, -1, true), lo = cui__font_line_height(ui, CUI_FONT_MEDIUM, fs_o);
            float wi = cui__text_width(ui, CUI_FONT_BODY, fs_i, si, -1, true), li = cui__font_line_height(ui, CUI_FONT_BODY, fs_i);
            cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs_o, po.x - wo * 0.5f, po.y - lo * 0.5f, so_sel ? p->glyph : p->fg, so, -1, true);
            cui__draw_text_line(ui, CUI_FONT_BODY, fs_i, pi.x - wi * 0.5f, pi.y - li * 0.5f, si_sel ? p->glyph : p->dim, si, -1, true);
        } else {
            int mv = k * 5;
            const char *sm = cui__frame_printf(ui, "%02d", mv);
            cui_vec2 pm = { c.x + sinf(a) * outer, c.y - cosf(a) * outer };
            float wm = cui__text_width(ui, CUI_FONT_MEDIUM, fs_o, sm, -1, true), lm = cui__font_line_height(ui, CUI_FONT_MEDIUM, fs_o);
            bool sel = p->minute == mv;
            cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs_o, pm.x - wm * 0.5f, pm.y - lm * 0.5f, sel ? p->glyph : p->fg, sm, -1, true);
        }
    }
    if (p->mode == 1 && p->minute % 5 != 0) cui_draw_circle(ui, tip, 3, p->glyph);   /* a minute between labels */
}

bool cui_time_dial(cui_ctx *ui, const char *id_str, int *hour, int *minute, float size) {
    if (size <= 0) size = 248;
    cui_id id = cui_make_id(ui, id_str);
    cui_id face_id = id ^ 0xFACEu;
    cui__dial2_state *st = cui_state(ui, id, sizeof *st);
    int oh = *hour, om = *minute;
    *hour = CUI_CLAMP(*hour, 0, 23);
    *minute = CUI_CLAMP(*minute, 0, 59);
    cui_push_id(ui, id_str);
    int col = cui__node_new(ui, id);
    ui->nodes[col].gap = 14; ui->nodes[col].align = CUI_ALIGN_CENTER; ui->nodes[col].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, col);
    /* the time, with the part being set highlighted */
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].align = CUI_ALIGN_CENTER; ui->nodes[head].gap = 4;
    cui__open(ui, head);
    for (int k = 0; k < 2; k++) {
        cui_id pid = cui__hash(&k, sizeof k, id);
        cui_interact pr = cui__interact(ui, pid, CUI_IF_FOCUSABLE);
        if (pr.clicked) st->mode = k;
        bool sel = st->mode == k;
        int b = cui__node_new(ui, pid);
        cui_node *bn = &ui->nodes[b];
        bn->pad[1] = bn->pad[3] = 10; bn->pad[0] = bn->pad[2] = 2;
        bn->radius = CUI_CORNERS(10);
        bn->bg = sel ? cui_mix(CUI_C(ui, SURFACE), cui__accent_or_fg(ui), ui->mode == CUI_THEME_DARK ? 0.2f : 0.12f) : (pr.hovered ? CUI_C(ui, HOVER) : cui__rgba(0, 0, 0, 0));
        cui__mark_interactive(ui, b, CUI_IF_FOCUSABLE);
        if (pr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, b);
        int t = cui__text_node(ui, cui__frame_printf(ui, "%02d", k ? *minute : *hour), -1, CUI_FONT_DISPLAY, cui__fs(ui, 34),
                               sel ? (ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT_STRONG)) : CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t].flags |= CUI_NF_TABULAR;
        ui->nodes[t].line_height = 1.15f;
        cui__close(ui);
        cui__focus_ring(ui, b, pr.focus_visible ? 1.0f : 0.0f);
        if (k == 0) {
            int c = cui__text_node(ui, ":", 1, CUI_FONT_DISPLAY, cui__fs(ui, 34), CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[c].line_height = 1.15f;
        }
    }
    cui__close(ui);
    cui_interact r = cui__interact(ui, face_id, CUI_IF_DRAG);
    cui_rect fr = cui__prev_rect(ui, face_id);
    if (r.active && ui->mdown[0] && fr.w > 0) {
        float dx = ui->mx - (fr.x + fr.w * 0.5f), dy = ui->my - (fr.y + fr.h * 0.5f);
        float a = atan2f(dx, -dy);
        if (a < 0) a += 2 * CUI_PI;
        float dist = sqrtf(dx * dx + dy * dy), R = fr.w * 0.5f;
        if (st->mode == 0) {
            int k = (int)floorf(a / (CUI_PI / 6) + 0.5f) % 12;
            bool inner = dist < R - 39;
            *hour = inner ? (k == 0 ? 0 : k + 12) : (k == 0 ? 12 : k);
        } else {
            *minute = (int)floorf(a / (CUI_PI / 30) + 0.5f) % 60;
        }
        st->dragging = true;
    } else if (st->dragging && !ui->mdown[0]) {
        st->dragging = false;
        if (st->mode == 0) st->mode = 1;   /* hours set: on to the minutes */
    }
    float target_ang, target_len;
    if (st->mode == 0) {
        int k = *hour % 12;
        target_ang = k * CUI_PI / 6;
        target_len = (*hour == 0 || *hour > 12) ? 0.0f : 1.0f;
    } else {
        target_ang = *minute * CUI_PI / 30;
        target_len = 1.0f;
    }
    if (!st->init) { st->ang.x = target_ang; st->len.x = target_len; st->init = true; }
    /* take the short way round */
    while (target_ang - st->ang.x > CUI_PI) target_ang -= 2 * CUI_PI;
    while (target_ang - st->ang.x < -CUI_PI) target_ang += 2 * CUI_PI;
    cui__spring(ui, &st->ang, target_ang, 0.3f, 0.12f);
    cui__spring(ui, &st->len, target_len, 0.3f, 0.0f);
    int face = cui__leaf(ui, face_id, CUI_PX(size), CUI_PX(size));
    cui__mark_interactive(ui, face, CUI_IF_DRAG);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__tdial_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->mode = st->mode; p->hour = *hour; p->minute = *minute;
    p->ang = st->ang.x; p->len = st->len.x;
    p->face = CUI_C(ui, SURFACE_MUTED);
    p->fg = CUI_C(ui, FOREGROUND);
    p->dim = CUI_C(ui, TEXT_SECONDARY);
    p->hand = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_ON) : CUI_C(ui, ACCENT);
    p->glyph = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, ACCENT_FOREGROUND);
    ui->nodes[face].paint = cui__paint_time_dial;
    ui->nodes[face].paint_data = p;
    cui__close(ui);
    cui_pop_id(ui);
    ui->last_id = id;
    return *hour != oh || *minute != om;
}

/* ------------------------------------------------------------------------ */
/* Ruler slider                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring shown; bool dragging, init; float press_x, start, vel, last_x; double last_t; float hover; } cui__ruler_state;

typedef struct {
    float value, min, max, step, spacing;
    int major;
    const char *format;
    cui_color tick, major_c, label, marker;
} cui__ruler_paint;

static void cui__paint_ruler(cui_ctx *ui, cui_node *n, void *data) {
    cui__ruler_paint *p = data;
    cui_rect r = n->rect;
    float cx = r.x + r.w * 0.5f;
    cui_push_clip(ui, r);
    float units = (r.w * 0.5f) / p->spacing + 2;
    float first = floorf((p->value - p->min) / p->step - units);
    float fs = cui__fs(ui, ui->theme.text_xs);
    for (float k = first; k <= first + units * 2 + 2; k += 1) {
        float v = p->min + k * p->step;
        if (v < p->min - p->step * 0.01f || v > p->max + p->step * 0.01f) continue;
        float x = cx + ((v - p->value) / p->step) * p->spacing;
        float fade = 1.0f - cui__clampf(fabsf(x - cx) / (r.w * 0.5f), 0, 1);
        fade = fade * fade * (3 - 2 * fade);
        int ki = (int)lroundf(k);
        bool major = p->major > 0 && ki % p->major == 0;
        float h = major ? 18 : (p->major > 1 && ki % (p->major / 2 ? p->major / 2 : 1) == 0 ? 12 : 8);
        cui_color c = cui_alpha(major ? p->major_c : p->tick, fade);
        cui_draw_rect(ui, (cui_rect){ x - 0.5f, r.y + 8, 1, h }, CUI_CORNERS(0.5f), c);
        if (major) {
            const char *s = cui__frame_printf(ui, p->format, (double)v);
            float w = cui__text_width(ui, CUI_FONT_BODY, fs, s, -1, true);
            cui__draw_text_line(ui, CUI_FONT_BODY, fs, x - w * 0.5f, r.y + 32, cui_alpha(p->label, fade), s, -1, true);
        }
    }
    cui_pop_clip(ui);
    /* the marker stays put */
    cui_draw_rect(ui, (cui_rect){ cx - 1, r.y + 2, 2, 28 }, CUI_CORNERS(1), p->marker);
    cui_vec2 tri[3] = { { cx - 5, r.y }, { cx + 5, r.y }, { cx, r.y + 6 } };
    cui_draw_polygon(ui, tri, 3, p->marker);
}

bool cui_ruler_slider_ex(cui_ctx *ui, const char *label, float *value, float min, float max, const cui_ruler_opts *o) {
    cui_ruler_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (max <= min) max = min + 1;
    float step = o->step > 0 ? o->step : 1;
    float spacing = o->spacing > 0 ? o->spacing : 9;
    int major = o->major_every > 0 ? o->major_every : 10;
    const char *fmt = o->format ? o->format : "%.0f";
    cui_id id = cui_make_id(ui, label);
    cui_id rid = id ^ 0x2017u;
    cui__ruler_state *st = cui_state(ui, id, sizeof *st);
    float old = *value;
    if (!st->init) { st->shown.x = *value; st->init = true; }
    cui_interact r = cui__interact(ui, rid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    float target = *value;
    if (r.hovered && ui->mpressed[0]) { st->dragging = true; st->press_x = ui->mx; st->start = st->shown.x; st->vel = 0; st->last_x = ui->mx; st->last_t = ui->time; }
    if (st->dragging) {
        if (ui->mdown[0]) {
            float v = st->start - (ui->mx - st->press_x) / spacing * step;
            v = cui__clampf(v, min - step * 3, max + step * 3);
            double dt = ui->time - st->last_t;
            if (dt > 0.001) { st->vel = cui__lerp(st->vel, -(ui->mx - st->last_x) / spacing * step / (float)dt, 0.4f); st->last_t = ui->time; st->last_x = ui->mx; }
            st->shown.x = v; st->shown.v = 0;
            *value = cui__clampf(roundf((v - min) / step) * step + min, min, max);
            ui->active = rid;
            ui->animating = true;
            target = v;
        } else {
            st->dragging = false;
            float land = st->shown.x + cui__clampf(st->vel, -60 * step, 60 * step) * 0.18f;
            *value = cui__clampf(roundf((land - min) / step) * step + min, min, max);
            target = *value;
        }
    }
    if (r.hovered && (ui->scroll_dx != 0 || ui->scroll_dy != 0) && !st->dragging) {
        float dv = (ui->scroll_dx != 0 ? -ui->scroll_dx : -ui->scroll_dy) * step;
        *value = cui__clampf(*value + (dv > 0 ? CUI_MAX(dv, step) : CUI_MIN(dv, -step)), min, max);
        *value = roundf((*value - min) / step) * step + min;
        target = *value;
        ui->scroll_dx = ui->scroll_dy = 0;
    }
    if (ui->focus == rid) {
        float k = (ui->mods & CUI_MOD_SHIFT) ? major : 1;
        if (cui_key_pressed(ui, CUI_KEY_RIGHT) || cui_key_pressed(ui, CUI_KEY_UP)) *value = cui__clampf(*value + step * k, min, max);
        if (cui_key_pressed(ui, CUI_KEY_LEFT) || cui_key_pressed(ui, CUI_KEY_DOWN)) *value = cui__clampf(*value - step * k, min, max);
        if (!st->dragging) target = *value;
    }
    if (!st->dragging) cui__spring(ui, &st->shown, target, 0.4f, 0.0f);
    cui__tween(ui, &st->hover, (r.hovered || st->dragging) ? 1.0f : 0.0f, CUI__T_HOVER);
    int col = cui__node_new(ui, id);
    ui->nodes[col].gap = 6;
    ui->nodes[col].w = o->width.kind ? o->width : CUI_FILL;
    ui->nodes[col].align = CUI_ALIGN_CENTER;
    cui__open(ui, col);
    if (label && cui__display_len(label)) {
        int lt = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[lt].line_height = 1.3f;
    }
    int vr = cui__node_new(ui, 0);
    ui->nodes[vr].dir = CUI_ROW; ui->nodes[vr].gap = 4; ui->nodes[vr].align = CUI_ALIGN_END;
    cui__open(ui, vr);
    int vt = cui__text_node(ui, cui__frame_printf(ui, fmt, (double)*value), -1, CUI_FONT_DISPLAY, cui__fs(ui, 30), CUI_C(ui, FOREGROUND), false);
    ui->nodes[vt].flags |= CUI_NF_TABULAR;
    ui->nodes[vt].line_height = 1.1f;
    if (o->unit) {
        int ut = cui__text_node(ui, o->unit, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[ut].line_height = 1.5f;
    }
    cui__close(ui);
    int tape = cui__leaf(ui, rid, CUI_FILL, CUI_PX(52));
    ui->nodes[tape].self_align = CUI_ALIGN_STRETCH;
    cui__mark_interactive(ui, tape, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    if (r.hovered || st->dragging) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
    cui__ruler_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->value = st->shown.x; p->min = min; p->max = max; p->step = step; p->spacing = spacing; p->major = major; p->format = fmt;
    p->tick = cui__control_edge(ui, st->hover);
    p->major_c = CUI_C(ui, TEXT_SECONDARY);
    p->label = CUI_C(ui, TEXT_MUTED);
    p->marker = cui__accent_or_fg(ui);
    ui->nodes[tape].paint = cui__paint_ruler;
    ui->nodes[tape].paint_data = p;
    cui__focus_ring(ui, tape, r.focus_visible ? 1.0f : 0.0f);
    cui__close(ui);
    ui->last_id = rid;
    return *value != old;
}

/* ------------------------------------------------------------------------ */
/* Histogram range slider                                                   */
/* ------------------------------------------------------------------------ */

typedef struct { const float *bins; int n; float lo_t, hi_t; float tmax; cui_color on, off; } cui__hist_paint;

static void cui__paint_hist(cui_ctx *ui, cui_node *n, void *data) {
    cui__hist_paint *p = data;
    if (p->n <= 0 || p->tmax <= 0) return;
    cui_rect r = n->rect;
    float x0 = r.x + 9, x1 = r.x + r.w - 9;   /* line up with the slider's thumbs */
    float bw = (x1 - x0) / p->n;
    for (int k = 0; k < p->n; k++) {
        float h = (p->bins[k] / p->tmax) * (r.h - 2);
        if (h < 1.5f) h = 1.5f;
        float bx = x0 + k * bw + 1;
        float mid = (k + 0.5f) / p->n;
        bool in = mid >= p->lo_t && mid <= p->hi_t;
        cui_draw_rect(ui, (cui_rect){ bx, r.y + r.h - h, CUI_MAX(1.0f, bw - 2), h }, (cui_corners){ 2, 2, 0, 0 }, in ? p->on : p->off);
    }
}

bool cui_histogram_slider_ex(cui_ctx *ui, const char *label, const float *bins, int bin_count, float *lo, float *hi,
                             float min, float max, const cui_histogram_opts *o) {
    cui_histogram_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (max <= min) max = min + 1;
    const char *fmt = o->format ? o->format : "%.0f";
    cui_id id = cui_make_id(ui, label) ^ 0x4157u;
    int col = cui__node_new(ui, id);
    ui->nodes[col].gap = 8;
    ui->nodes[col].w = CUI_FILL;
    cui__open(ui, col);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW;
    cui__open(ui, head);
    if (label && cui__display_len(label)) {
        int lt = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
        ui->nodes[lt].line_height = 1.35f;
    }
    cui_spacer(ui);
    const char *vs = cui__frame_printf(ui, "%s \xe2\x80\x93 %s", cui__frame_printf(ui, fmt, (double)*lo), cui__frame_printf(ui, fmt, (double)*hi));
    int vt = cui__text_node(ui, vs, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), false);
    ui->nodes[vt].flags |= CUI_NF_TABULAR;
    ui->nodes[vt].line_height = 1.35f;
    cui__close(ui);
    float tmax = 0;
    for (int k = 0; k < bin_count; k++) tmax = CUI_MAX(tmax, bins[k]);
    float *anim = cui_state(ui, id ^ 0x1u, sizeof(float) * 2);
    float tl = (*lo - min) / (max - min), th = (*hi - min) / (max - min);
    if (anim[0] == 0 && anim[1] == 0) { anim[0] = tl; anim[1] = th; }
    cui__tween(ui, &anim[0], tl, 0.1f);
    cui__tween(ui, &anim[1], th, 0.1f);
    int bars = cui__leaf(ui, 0, CUI_FILL, CUI_PX(o->height > 0 ? o->height : 56));
    ui->nodes[bars].self_align = CUI_ALIGN_STRETCH;
    cui__hist_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->bins = cui__frame_alloc(ui, sizeof(float) * (size_t)CUI_MAX(1, bin_count));
    memcpy((float *)p->bins, bins, sizeof(float) * (size_t)bin_count);
    p->n = bin_count; p->lo_t = anim[0]; p->hi_t = anim[1]; p->tmax = tmax;
    p->on = cui__accent_or_fg(ui);
    p->off = CUI_C(ui, CONTROL_TRACK);
    ui->nodes[bars].paint = cui__paint_hist;
    ui->nodes[bars].paint_data = p;
    ui->slider_no_header = true;
    bool changed = cui_range_slider(ui, label, lo, hi, min, max);
    ui->nodes[ui->node_count - 1].ty = -6;   /* tuck the track under the bars */
    cui__close(ui);
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Signature pad                                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_vec2 *pts; int npts, cap;
    float *widths;
    int *ends; int nends, ecap;
    bool drawing;
    double last_t;
    float ref_w;   /* pad width the strokes were recorded at: a narrower pad scales them down */
} cui__sig_state;

static void cui__sig_dtor(cui_ctx *ui, void *data) {
    cui__sig_state *s = data;
    cui__free(ui, s->pts); cui__free(ui, s->widths); cui__free(ui, s->ends);
    s->pts = NULL; s->widths = NULL; s->ends = NULL;
}

typedef struct { cui_vec2 *pts; float *widths; int *ends; int nends, npts; cui_color ink, line, hint; const char *placeholder; bool empty; float ref_w; } cui__sig_paint;

static void cui__paint_signature(cui_ctx *ui, cui_node *n, void *data) {
    cui__sig_paint *p = data;
    cui_rect r = n->rect;
    float base_y = r.y + r.h * 0.72f;
    cui_draw_rect(ui, (cui_rect){ r.x + 24, base_y, r.w - 48, 1 }, CUI_CORNERS(0), p->line);
    cui_draw_icon(ui, CUI_ICON_X, (cui_rect){ r.x + 24, base_y - 22, 16, 16 }, 13, p->hint);
    if (p->empty && p->placeholder) {
        float fs = cui__fs(ui, ui->theme.text_body);
        float w = cui__text_width(ui, CUI_FONT_BODY, fs, p->placeholder, -1, false);
        cui__draw_text_line(ui, CUI_FONT_BODY, fs, r.x + (r.w - w) * 0.5f, base_y - 36, p->hint, p->placeholder, -1, false);
    }
    cui_push_clip(ui, r);
    /* recorded on a wider pad (a rotated tablet): scale toward the baseline so it all shows */
    float sc = p->ref_w > 0 && r.w < p->ref_w ? r.w / p->ref_w : 1, by = r.h * 0.72f, wk = CUI_MAX(sc, 0.5f);
#define CUI__SIG_PT(q) ((cui_vec2){ r.x + (q).x * sc, r.y + by + ((q).y - by) * sc })
    int start = 0;
    for (int s = 0; s <= p->nends; s++) {
        int end = s < p->nends ? p->ends[s] : p->npts;
        if (end <= start) { start = end; continue; }
        if (end - start == 1) cui_draw_circle(ui, CUI__SIG_PT(p->pts[start]), p->widths[start] * 0.5f * wk, p->ink);
        for (int k = start + 1; k < end; k++) {
            cui_vec2 a = CUI__SIG_PT(p->pts[k - 1]), b = CUI__SIG_PT(p->pts[k]);
            float w = (p->widths[k - 1] + p->widths[k]) * 0.5f * wk;
            cui_draw_line(ui, a, b, w, p->ink);
            cui_draw_circle(ui, b, w * 0.5f, p->ink);
        }
        start = end;
    }
#undef CUI__SIG_PT
    cui_pop_clip(ui);
}

static void cui__sig_push(cui_ctx *ui, cui__sig_state *s, cui_vec2 p, float w) {
    if (s->npts >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 512;
        if (s->cap > 65536) { s->cap = 65536; if (s->npts >= s->cap) return; }
        s->pts = cui__realloc(ui, s->pts, sizeof(cui_vec2) * (size_t)s->cap);
        s->widths = cui__realloc(ui, s->widths, sizeof(float) * (size_t)s->cap);
    }
    s->pts[s->npts] = p;
    s->widths[s->npts] = w;
    s->npts++;
}

bool cui_signature_pad_ex(cui_ctx *ui, const char *id_str, const cui_signature_opts *o) {
    cui_signature_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id cid = id ^ 0x5161u;
    cui__sig_state *st = cui_state(ui, id, sizeof *st);
    cui__state_set_dtor(ui, id, cui__sig_dtor);
    bool changed = false;
    cui_interact r = cui__interact(ui, cid, CUI_IF_DRAG);
    cui_rect pr = cui__prev_rect(ui, cid);
    if (st->npts == 0) st->ref_w = pr.w;
    /* the pointer, in the space the strokes were recorded in */
    float sc = st->ref_w > 0 && pr.w > 0 && pr.w < st->ref_w ? pr.w / st->ref_w : 1, by = pr.h * 0.72f;
    cui_vec2 mp = { (ui->mx - pr.x) / sc, by + (ui->my - pr.y - by) / sc };
    if (r.hovered && ui->mpressed[0] && pr.w > 0) {
        st->drawing = true;
        if (st->npts > 0) {
            if (st->nends >= st->ecap) { st->ecap = st->ecap ? st->ecap * 2 : 32; st->ends = cui__realloc(ui, st->ends, sizeof(int) * (size_t)st->ecap); }
            st->ends[st->nends++] = st->npts;
        }
        cui__sig_push(ui, st, mp, 2.6f);
        st->last_t = ui->time;
        changed = true;
    } else if (st->drawing && ui->mdown[0]) {
        cui_vec2 p = mp;
        cui_vec2 q = st->pts[st->npts - 1];
        float dx = p.x - q.x, dy = p.y - q.y, dist = sqrtf(dx * dx + dy * dy);
        if (dist > 1.5f) {
            double dt = ui->time - st->last_t;
            float speed = dt > 0.0005 ? dist / (float)dt : 300;
            float target = cui__clampf(3.4f - speed * 0.0022f, 1.1f, 3.4f);   /* fast strokes are thinner, like ink */
            float w = cui__lerp(st->widths[st->npts - 1], target, 0.35f);
            cui__sig_push(ui, st, p, w);
            st->last_t = ui->time;
            changed = true;
        }
        ui->active = cid;
    } else if (!ui->mdown[0]) {
        st->drawing = false;
    }
    bool empty = st->npts == 0;
    int box = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[box];
    bn->dir = CUI_STACK;
    bn->w = o->width.kind ? o->width : CUI_FILL;
    bn->h = CUI_PX(o->height > 0 ? o->height : 180);
    bn->radius = CUI_CORNERS(ui->theme.radius_control - 2);
    bn->bg = CUI_C(ui, SURFACE);
    bn->border = 1;
    bn->border_color = r.hovered || st->drawing ? CUI_C(ui, BORDER_STRONG) : CUI_C(ui, BORDER);
    bn->flags |= CUI_NF_CLIP;
    cui__open(ui, box);
    int canvas = cui__leaf(ui, cid, CUI_FILL, CUI_FILL);
    cui__mark_interactive(ui, canvas, CUI_IF_DRAG);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_ARROW);
    cui__sig_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->pts = st->pts; p->widths = st->widths; p->ends = st->ends; p->nends = st->nends; p->npts = st->npts;
    p->ink = cui__color_set(o->ink) ? cui_color_of(ui, o->ink) : CUI_C(ui, FOREGROUND);
    p->line = CUI_C(ui, BORDER_STRONG);
    p->hint = CUI_C(ui, TEXT_MUTED);
    p->placeholder = o->placeholder ? o->placeholder : "Sign here";
    p->empty = empty; p->ref_w = st->ref_w;
    ui->nodes[canvas].paint = cui__paint_signature;
    ui->nodes[canvas].paint_data = p;
    if (!empty) {
        int bar = cui__node_new(ui, 0);
        ui->nodes[bar].dir = CUI_ROW; ui->nodes[bar].justify = CUI_JUSTIFY_END; ui->nodes[bar].w = CUI_FILL;
        ui->nodes[bar].pad[0] = ui->nodes[bar].pad[1] = 8;
        cui__open(ui, bar);
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST; bo.icon = CUI_ICON_ROTATE_CCW;
        cui_push_id(ui, id_str);
        if (cui_button_ex(ui, "Clear", &bo)) { st->npts = 0; st->nends = 0; changed = true; }
        cui_pop_id(ui);
        cui__close(ui);
    }
    cui__close(ui);
    ui->last_id = id;
    return changed;
}

int cui_signature_points(cui_ctx *ui, const char *id, const cui_vec2 **points, const int **stroke_ends, int *stroke_count) {
    cui__sig_state *st = cui_state(ui, cui_make_id(ui, id), sizeof *st);
    /* stroke_ends lists the start of every stroke after the first; expose ends instead */
    static int ends_buf[4096];
    int n = 0;
    for (int k = 0; k < st->nends && n < 4095; k++) ends_buf[n++] = st->ends[k];
    if (st->npts > 0 && n < 4096) ends_buf[n++] = st->npts;
    if (points) *points = st->pts;
    if (stroke_ends) *stroke_ends = ends_buf;
    if (stroke_count) *stroke_count = n;
    return st->npts;
}

void cui_signature_clear(cui_ctx *ui, const char *id) {
    cui__sig_state *st = cui_state(ui, cui_make_id(ui, id), sizeof *st);
    st->npts = 0; st->nends = 0;
}

/* ------------------------------------------------------------------------ */
/* Dropzone and avatar upload                                               */
/* ------------------------------------------------------------------------ */

bool cui_path_has_extension(const char *path, const char *accept) {
    if (!accept || !*accept) return true;
    if (!path) return false;
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/'), *bslash = strrchr(path, '\\');
    if (!dot || (slash && dot < slash) || (bslash && dot < bslash)) return false;
    dot++;
    size_t el = strlen(dot);
    const char *p = accept;
    while (*p) {
        while (*p == ';' || *p == ',' || *p == ' ' || *p == '.' || *p == '*') p++;
        const char *e = p;
        while (*e && *e != ';' && *e != ',' && *e != ' ') e++;
        if ((size_t)(e - p) == el) {
            size_t k = 0;
            for (; k < el; k++) {
                char a = dot[k], b = p[k];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) break;
            }
            if (k == el) return true;
        }
        p = e;
    }
    return false;
}

/* Claims this frame's drop if it landed inside r. Returns accepted count. */
static int cui__claim_drop(cui_ctx *ui, cui_rect r, const char *accept, int *rejected) {
    *rejected = 0;
    if (ui->drop_count == 0 || ui->drop_claimed || r.w <= 0) return 0;
    if (!cui__rect_contains(r, ui->drop_x, ui->drop_y)) return 0;
    ui->drop_claimed = true;
    int ok = 0;
    for (int k = 0; k < ui->drop_count; k++) {
        if (cui_path_has_extension(ui->drop_buf + ui->drop_off[k], accept)) ok++;
        else (*rejected)++;
    }
    /* keep only the accepted files visible to cui_dropped_file */
    int w = 0;
    for (int k = 0; k < ui->drop_count; k++)
        if (cui_path_has_extension(ui->drop_buf + ui->drop_off[k], accept)) ui->drop_off[w++] = ui->drop_off[k];
    ui->drop_count = w;
    return ok;
}

typedef struct { cui_color c; float t; } cui__dash_paint;

static void cui__dash_line(cui_ctx *ui, cui_vec2 a, cui_vec2 b, float th, cui_color c, float dash, float gap, float *phase) {
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    if (len <= 0) return;
    float ux = dx / len, uy = dy / len;
    float pos = 0;
    while (pos < len) {
        float period = dash + gap;
        float in = fmodf(*phase, period);
        if (in < dash) {
            float seg = CUI_MIN(dash - in, len - pos);
            cui_vec2 p0 = { a.x + ux * pos, a.y + uy * pos }, p1 = { a.x + ux * (pos + seg), a.y + uy * (pos + seg) };
            cui_draw_line(ui, p0, p1, th, c);
            pos += seg; *phase += seg;
        } else {
            float seg = CUI_MIN(period - in, len - pos);
            pos += seg; *phase += seg;
        }
    }
}

/* Dashed rounded rectangle: straight runs as dashes, corners as dashed arcs. */
static void cui__draw_dashed_rrect(cui_ctx *ui, cui_rect r, float rad, cui_color c, float th, float dash, float gap) {
    rad = CUI_MIN(rad, CUI_MIN(r.w, r.h) * 0.5f);
    float x0 = r.x + th * 0.5f, y0 = r.y + th * 0.5f, x1 = r.x + r.w - th * 0.5f, y1 = r.y + r.h - th * 0.5f;
    float phase = 0;
    cui__dash_line(ui, (cui_vec2){ x0 + rad, y0 }, (cui_vec2){ x1 - rad, y0 }, th, c, dash, gap, &phase);
    struct { float cx, cy, a0; } corners[4] = { { x1 - rad, y0 + rad, -CUI_PI * 0.5f }, { x1 - rad, y1 - rad, 0 }, { x0 + rad, y1 - rad, CUI_PI * 0.5f }, { x0 + rad, y0 + rad, CUI_PI } };
    cui_vec2 runs[4][2] = { { { x1, y0 + rad }, { x1, y1 - rad } }, { { x1 - rad, y1 }, { x0 + rad, y1 } }, { { x0, y1 - rad }, { x0, y0 + rad } }, { { x0 + rad, y0 }, { x0 + rad, y0 } } };
    for (int k = 0; k < 4; k++) {
        /* corner arc, walked in small steps so the dash pattern continues */
        float arc = rad * CUI_PI * 0.5f;
        int steps = CUI_MAX(1, (int)(arc / 1.5f));
        for (int s = 0; s < steps; s++) {
            float a = corners[k].a0 + (CUI_PI * 0.5f) * (s + 0.5f) / steps;
            float period = dash + gap;
            if (fmodf(phase, period) < dash) {
                float a0 = corners[k].a0 + (CUI_PI * 0.5f) * s / steps, a1 = corners[k].a0 + (CUI_PI * 0.5f) * (s + 1) / steps;
                cui_draw_arc(ui, (cui_vec2){ corners[k].cx, corners[k].cy }, rad, a0, a1, th, c);
            }
            (void)a;
            phase += arc / steps;
        }
        if (k < 3) cui__dash_line(ui, runs[k][0], runs[k][1], th, c, dash, gap, &phase);
    }
}

static void cui__paint_dropzone(cui_ctx *ui, cui_node *n, void *data) {
    cui__dash_paint *p = data;
    cui__draw_dashed_rrect(ui, n->rect, n->radius.tl, p->c, 1.25f + 0.5f * p->t, 6, 5);
}

typedef struct { float hover, drag; cui_spring lift; double done_at; int done_count, rejected; } cui__drop_state;

int cui_dropzone_ex(cui_ctx *ui, const char *id_str, const cui_dropzone_opts *o) {
    cui_dropzone_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__drop_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_rect pr = cui__prev_rect(ui, id);
    int rejected = 0;
    int got = cui__claim_drop(ui, pr, o->accept, &rejected);
    if (got || rejected) { st->done_at = ui->time; st->done_count = got; st->rejected = rejected; }
    bool over = ui->drag_hover && cui__rect_contains(pr, ui->drag_x, ui->drag_y);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->drag, over ? 1.0f : 0.0f, CUI__T_STATE);
    cui__spring(ui, &st->lift, over ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);
    bool recent = st->done_at > 0 && ui->time - st->done_at < 2.2;
    if (recent) cui__wakeup_after(ui, (float)(2.2 - (ui->time - st->done_at)) + 0.01f);
    cui_color accent = cui__accent_or_fg(ui);
    int box = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[box];
    bn->dir = o->compact ? CUI_ROW : CUI_COLUMN;
    bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
    bn->gap = o->compact ? 12 : 10;
    bn->w = o->width.kind ? o->width : CUI_FILL;
    bn->h = CUI_PX(o->compact ? 64 : (o->height > 0 ? o->height : 168));
    bn->pad[0] = bn->pad[1] = bn->pad[2] = bn->pad[3] = 16;
    bn->radius = CUI_CORNERS(ui->theme.radius_control);
    bn->bg = cui_mix(cui_alpha(CUI_C(ui, HOVER), st->hover * 0.7f), cui_mix(CUI_C(ui, SURFACE), accent, ui->mode == CUI_THEME_DARK ? 0.12f : 0.06f), st->drag);
    cui__dash_paint *dp = cui__frame_alloc(ui, sizeof *dp);
    cui_color border = cui__control_edge(ui, st->hover);
    dp->c = cui_mix(border, accent, st->drag);
    dp->t = st->drag;
    bn->paint = cui__paint_dropzone;
    bn->paint_data = dp;
    cui__mark_interactive(ui, box, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, box);
    float isz = o->compact ? 36 : 44;
    int ib = cui__node_new(ui, 0);
    cui_node *in = &ui->nodes[ib];
    in->w = CUI_PX(isz); in->h = CUI_PX(isz); in->radius = CUI_CORNERS(isz * 0.5f);
    in->bg = cui_mix(CUI_C(ui, SURFACE_MUTED), cui_alpha(accent, 0.16f), st->drag);
    in->border = 1; in->border_color = CUI_C(ui, BORDER);
    in->dir = CUI_STACK; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER;
    in->ty = -4 * st->lift.x;
    in->self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, ib);
    const char *icon = recent ? (st->rejected && !st->done_count ? CUI_ICON_CIRCLE_ALERT : CUI_ICON_CHECK) : (o->icon ? o->icon : CUI_ICON_UPLOAD);
    cui_color icol = recent ? (st->rejected && !st->done_count ? CUI_C(ui, DANGER) : CUI_C(ui, SUCCESS)) : cui_mix(CUI_C(ui, TEXT_SECONDARY), accent, st->drag);
    cui__icon_swap(ui, id ^ 0x1C0u, icon, o->compact ? 16 : 19, icol);
    cui__close(ui);
    int tc = cui__node_new(ui, 0);
    ui->nodes[tc].gap = 2;
    ui->nodes[tc].align = o->compact ? CUI_ALIGN_START : CUI_ALIGN_CENTER;
    if (o->compact) ui->nodes[tc].w = CUI_FILL;
    cui__open(ui, tc);
    const char *title = o->title ? o->title : "Drop files here";
    if (over) title = "Release to add";
    if (recent) title = st->done_count ? cui__frame_printf(ui, "%d file%s added", st->done_count, st->done_count == 1 ? "" : "s")
                                       : "That file type is not supported";
    int tt = cui__text_node(ui, title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[tt].line_height = 1.35f;
    const char *desc = o->description ? o->description : "or click to browse";
    int dt = cui__text_node(ui, desc, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[dt].line_height = 1.35f;
    cui__close(ui);
    cui__close(ui);
    cui__focus_ring(ui, box, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = id;
    if (got) return got;
    if (r.clicked) return -1;
    return 0;
}

typedef struct { float hover, drag; cui_spring pop; } cui__avup_state;

typedef struct { float hover; cui_color scrim, ring; float ring_t; } cui__avup_paint;
static void cui__paint_avatar_overlay(cui_ctx *ui, cui_node *n, void *data) {
    cui__avup_paint *p = data;
    cui_rect r = n->rect;
    float rad = r.w * 0.5f;
    if (p->hover > 0.01f) {
        cui_draw_rect(ui, r, CUI_CORNERS(rad), cui_alpha(p->scrim, 0.45f * p->hover));
        cui_draw_icon(ui, CUI_ICON_CAMERA, r, r.w * 0.26f, cui_alpha(cui__rgba(255, 255, 255, 255), p->hover));
    }
    if (p->ring_t > 0.01f)
        cui_draw_rect_border(ui, cui__rect_inset(r, -4), CUI_CORNERS(rad + 4), cui__rgba(0, 0, 0, 0), cui_alpha(p->ring, p->ring_t), 2);
}

int cui_avatar_upload(cui_ctx *ui, const char *id_str, const char *name, cui_texture *image, float size) {
    if (size <= 0) size = 88;
    cui_id id = cui_make_id(ui, id_str);
    cui__avup_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_rect pr = cui__prev_rect(ui, id);
    int result = 0;
    int rejected = 0;
    int got = cui__claim_drop(ui, pr, "png;jpg;jpeg;bmp;gif;tga", &rejected);
    if (got > 0) {
        int w = 0, h = 0;
        cui_texture t = cui_image_load(ui, cui_dropped_file(ui, 0), &w, &h);
        if (t) {
            if (*image) cui_image_destroy(ui, *image);
            *image = t;
            result = 1;
            st->pop.x = 0.9f;
        }
    }
    if (r.clicked) result = -1;
    bool over = ui->drag_hover && cui__rect_contains(pr, ui->drag_x, ui->drag_y);
    cui__tween(ui, &st->hover, (r.hovered || over) ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->drag, over ? 1.0f : 0.0f, CUI__T_STATE);
    if (st->pop.x == 0) st->pop.x = 1;
    cui__spring(ui, &st->pop, 1.0f, 0.4f, 0.3f);
    int wrap = cui__node_new(ui, id);
    cui_node *wn = &ui->nodes[wrap];
    wn->dir = CUI_STACK; wn->w = CUI_PX(size); wn->h = CUI_PX(size);
    wn->self_align = CUI__ALIGN_NOSTRETCH;
    wn->radius = CUI_CORNERS(size * 0.5f);
    wn->scale = st->pop.x;
    cui__mark_interactive(ui, wrap, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__avup_paint *ap = cui__frame_alloc(ui, sizeof *ap);
    ap->hover = st->hover; ap->scrim = cui__rgba(0, 0, 0, 255); ap->ring = cui__accent_or_fg(ui); ap->ring_t = st->drag;
    wn->paint = cui__paint_avatar_overlay;
    wn->paint_data = ap;
    wn->flags |= CUI_NF_PAINT_AFTER;
    cui__open(ui, wrap);
    cui_avatar_opts ao;
    memset(&ao, 0, sizeof ao);
    ao.size = size; ao.image = *image;
    /* absolute children paint in order, so the avatar sits in one too and the badge lands on top */
    int face = cui__absolute(ui, 0, 0, 0, size, size);
    cui__open(ui, face);
    cui_avatar_ex(ui, name, &ao);
    cui__close(ui);
    /* edit badge */
    float bs = CUI_MAX(24.0f, size * 0.3f);
    int badge = cui__absolute(ui, 0, size - bs + 2, size - bs + 2, bs, bs);
    cui_node *bd = &ui->nodes[badge];
    bd->radius = CUI_CORNERS(bs * 0.5f);
    bd->bg = CUI_C(ui, SURFACE_RAISED);
    bd->border = 1; bd->border_color = CUI_C(ui, BORDER);
    bd->dir = CUI_STACK; bd->align = CUI_ALIGN_CENTER; bd->justify = CUI_JUSTIFY_CENTER;
    bd->elevation = CUI_ELEVATION_RAISED; bd->elevation_alpha = 1;
    cui__open(ui, badge);
    cui__icon_node(ui, CUI_ICON_PENCIL, bs * 0.46f, CUI_C(ui, FOREGROUND));
    cui__close(ui);
    cui__close(ui);
    cui__focus_ring(ui, wrap, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = id;
    return result;
}
