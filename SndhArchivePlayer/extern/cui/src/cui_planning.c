/* Planning views: kanban board, week calendar and gantt chart. */
#include "cui_internal.h"

#include <time.h>

#define cui__fs cui__fsz

static cui_color cui__acc(cui_ctx *ui) { return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT); }

/* ------------------------------------------------------------------------ */
/* Kanban                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    int drag_card;           /* card id being dragged, or -1 */
    int from_col;
    float grab_x, grab_y;
    float drag_h;            /* height of the lifted card: the size of the hole it leaves and opens */
    bool pending;
    int press_card, press_col;
    float press_x, press_y;
    cui_spring lift;
    int gen;                 /* bumps on every drop: cards reset their shift */
    int settle_card;         /* the card that was just dropped, gliding into its slot */
    float settle_dx, settle_dy;
    bool settle_pending;
    float slot_x, slot_y;    /* the lifted card's own slot, for a release outside every column */
    /* keyboard moves: Space lifts the focused card, arrows choose the slot, Enter drops */
    bool kbd, kdrop;
    int kcol, kidx, ncols;
    float hole_x, hole_y;    /* last frame's slot, where a keyboard lifted card is shown */
    cui_spring kx, ky;
    bool init;
} cui__kb_state;

typedef struct { cui_spring y, sx, sy; float hover; int gen; } cui__kb_card_state;
typedef struct { float hl, grow; int gen; } cui__kb_col_state;

struct cui_kanban_build {
    cui_id id;
    cui__kb_state *st;
    int col;                 /* current column index */
    int col_node, hole_node;
    int card_pos;            /* position of the next card, the dragged card left out */
    bool passed_drag;        /* the dragged card came earlier in this column */
    int target_col, target_index;
    float hole_y, hole_x;    /* where the dragged card would land (absolute) */
    bool col_is_target;
    struct cui_kanban_build *prev;
};

#define CUI__KB_GAP 8.0f
#define CUI__KB_HEAD 50.0f   /* column padding, header and gap above the first card */

void cui_kanban_begin(cui_ctx *ui, const char *id_str) {
    struct cui_kanban_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    b->prev = (struct cui_kanban_build *)ui->kanban;
    ui->kanban = b;
    cui__kb_state *st = cui_state(ui, b->id, sizeof *st);
    if (!st->init) { st->drag_card = -1; st->settle_card = -1; st->init = true; }
    b->st = st;
    b->col = -1;
    b->target_col = -1;
    /* start a drag once the pointer moves a few points from the press */
    if (st->pending && ui->mdown[0] && st->drag_card < 0) {
        float dx = ui->mx - st->press_x, dy = ui->my - st->press_y;
        if (dx * dx + dy * dy > 25) { st->drag_card = st->press_card; st->from_col = st->press_col; st->pending = false; st->settle_card = -1; }
    }
    if (!ui->mdown[0]) st->pending = false;
    cui__spring(ui, &st->lift, st->drag_card >= 0 ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);
    if (st->kbd && st->drag_card >= 0) {
        if (cui_key_pressed(ui, CUI_KEY_UP)) { st->kidx = CUI_MAX(0, st->kidx - 1); ui->kpressed[CUI_KEY_UP] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) { st->kidx++; ui->kpressed[CUI_KEY_DOWN] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) { st->kcol = CUI_MAX(0, st->kcol - 1); ui->kpressed[CUI_KEY_LEFT] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { st->kcol = CUI_MIN(CUI_MAX(0, st->ncols - 1), st->kcol + 1); ui->kpressed[CUI_KEY_RIGHT] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE)) { st->kdrop = true; ui->kpressed[CUI_KEY_ENTER] = ui->kpressed[CUI_KEY_SPACE] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) { st->drag_card = -1; st->kbd = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
        ui->animating = true;
    } else if (st->drag_card >= 0) { ui->active = b->id; ui->animating = true; cui__set_cursor(ui, CUI_CURSOR_MOVE); }
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL;
    cui_scroll_state *ss = cui_state(ui, b->id ^ 0x5C2u, sizeof *ss);
    ss->fade = cui__backdrop(ui);   /* columns past the edge fade out */
    cui__scroll_begin_ex(ui, b->id ^ 0x5C2u, &sb, true);
    ui->nodes[cui__current(ui)].h = CUI_FIT;
    ui->nodes[cui__current(ui)].min_h = 0;
    int row = cui__node_new(ui, b->id);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 14; ui->nodes[row].align = CUI_ALIGN_START;
    ui->nodes[row].pad[2] = 8;
    cui__open(ui, row);
    cui_push_id(ui, id_str);
}

typedef struct { float hl; cui_color hl_c; } cui__kcol_paint;
static void cui__paint_kanban_col(cui_ctx *ui, cui_node *n, void *data) {
    cui__kcol_paint *p = data;
    if (p->hl > 0.01f) cui_draw_rect_border(ui, n->rect, n->radius, cui__rgba(0, 0, 0, 0), cui_alpha(p->hl_c, p->hl), 1.0f);
}

bool cui_kanban_column_begin_ex(cui_ctx *ui, const char *title, int card_count, const cui_kanban_column_opts *o) {
    cui_kanban_column_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    struct cui_kanban_build *b = (struct cui_kanban_build *)ui->kanban;
    if (!b) return false;
    cui__kb_state *st = b->st;
    b->col++;
    b->card_pos = 0;
    b->passed_drag = false;
    int c = b->col;
    cui_id cid = cui__hash(&c, sizeof c, b->id ^ 0xC01u);
    cui_rect cr = cui__prev_rect(ui, cid);
    b->col_is_target = st->drag_card >= 0 && cui__has_prev_rect(ui, cid) &&
                       (st->kbd ? c == st->kcol : (ui->mx >= cr.x - 7 && ui->mx < cr.x + cr.w + 7));
    if (b->col_is_target) { b->target_col = c; b->target_index = 0; b->hole_y = cr.y + CUI__KB_HEAD; b->hole_x = cr.x + 8; }
    cui__kb_col_state *cs = cui_state(ui, cid, sizeof *cs);
    if (cs->gen != st->gen) { cs->grow = 0; cs->gen = st->gen; }   /* the dropped card fills the room now */
    cui__tween(ui, &cs->hl, b->col_is_target ? 1.0f : 0.0f, CUI__T_STATE);
    int col = cui__node_new(ui, cid);
    cui_node *cn = &ui->nodes[col];
    cn->w = CUI_PX(o->width > 0 ? o->width : 280);
    cn->gap = CUI__KB_GAP;
    cn->pad[0] = 10; cn->pad[1] = cn->pad[3] = 8; cn->pad[2] = 10;
    cn->radius = CUI_CORNERS(ui->theme.radius_panel);
    cn->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, BACKGROUND), CUI_C(ui, SURFACE), 0.55f) : CUI_C(ui, SURFACE_MUTED);
    cn->min_h = 120;
    if (st->drag_card >= 0 && st->from_col == c) cn->flags |= CUI_NF_ON_TOP;   /* the lifted card paints above other columns */
    cui__kcol_paint *cp = cui__frame_alloc(ui, sizeof *cp);
    bool neutral = ui->accent == CUI_ACCENT_NEUTRAL;
    cp->hl = cs->hl; cp->hl_c = neutral ? CUI_C(ui, BORDER_STRONG) : cui_alpha(cui__acc(ui), 0.45f);
    cn->paint = cui__paint_kanban_col;
    cn->paint_data = cp;
    cn->flags |= CUI_NF_PAINT_AFTER;
    b->col_node = col;
    cui__open(ui, col);
    /* the slot the card would drop into; placed in column_end */
    b->hole_node = cui__absolute(ui, 0, 8, CUI__KB_HEAD, (o->width > 0 ? o->width : 280) - 16, 0);
    ui->nodes[b->hole_node].radius = CUI_CORNERS(ui->theme.radius_control - 2);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].gap = 8; ui->nodes[head].align = CUI_ALIGN_CENTER;
    ui->nodes[head].pad[1] = 2; ui->nodes[head].pad[3] = 6; ui->nodes[head].h = CUI_PX(32);
    cui__open(ui, head);
    if (cui__color_set(o->dot)) {
        int dot = cui__leaf(ui, 0, CUI_PX(8), CUI_PX(8));
        ui->nodes[dot].radius = CUI_CORNERS(4);
        ui->nodes[dot].bg = cui_color_of(ui, o->dot);
    }
    int t = cui__text_node(ui, title, cui__display_len(title), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    bool over = o->limit > 0 && card_count > o->limit;
    int cnt = cui__text_node(ui, o->limit > 0 ? cui__frame_printf(ui, "%d / %d", card_count, o->limit) : cui__frame_printf(ui, "%d", card_count), -1,
                             CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), over ? CUI_C(ui, DANGER) : CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[cnt].line_height = 1.3f;
    cui_spacer(ui);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.tooltip = "Add card";
    cui_push_id_int(ui, c);
    bool add = cui_icon_button_ex(ui, CUI_ICON_PLUS, &bo);
    cui_pop_id(ui);
    ui->nodes[ui->last_node].w = CUI_PX(26); ui->nodes[ui->last_node].h = CUI_PX(26);
    cui__close(ui);
    cui_push_id_int(ui, c);
    return add;
}

bool cui_kanban_card_begin(cui_ctx *ui, int card_id) {
    struct cui_kanban_build *b = (struct cui_kanban_build *)ui->kanban;
    if (!b) { cui__open(ui, cui__node_new(ui, 0)); cui_push_id_int(ui, card_id); return false; }
    cui__kb_state *st = b->st;
    cui_id kid = cui__hash(&card_id, sizeof card_id, b->id ^ 0xCA2Du);
    cui_interact r = cui__interact(ui, kid, CUI_IF_FOCUSABLE);
    cui_rect kr = cui__prev_rect(ui, kid);   /* the layout slot, without the shift */
    bool dragged = st->drag_card == card_id;
    cui__kb_card_state *cs = cui_state(ui, kid ^ 0x51u, sizeof *cs);
    if (cs->gen != st->gen) {
        /* a drop reordered the data: every card now sits where it was shown */
        memset(&cs->y, 0, sizeof cs->y);
        cs->gen = st->gen;
    }
    if (st->settle_pending && card_id == st->settle_card) {
        cs->sx.x = st->settle_dx; cs->sy.x = st->settle_dy; cs->sx.v = cs->sy.v = 0;
        st->settle_pending = false;
    }
    if (r.focus_visible && st->drag_card < 0 && cui_key_pressed(ui, CUI_KEY_SPACE)) {
        ui->kpressed[CUI_KEY_SPACE] = 0;
        st->drag_card = card_id; st->from_col = b->col; st->kbd = true; st->kdrop = false;
        st->kcol = b->col; st->kidx = b->card_pos; st->drag_h = kr.h;
        st->kx.x = kr.x; st->ky.x = kr.y; st->kx.v = st->ky.v = 0;
        st->hole_x = kr.x; st->hole_y = kr.y;
        st->settle_card = -1;
    }
    if (r.hovered && ui->mpressed[0] && st->drag_card < 0) {
        st->pending = true; st->press_card = card_id; st->press_col = b->col;
        st->press_x = ui->mx; st->press_y = ui->my;
        st->grab_x = ui->mx - kr.x; st->grab_y = ui->my - kr.y;
        st->drag_h = kr.h;
    }
    float shift = 0;
    if (st->drag_card >= 0 && !dragged && cui__has_prev_rect(ui, kid)) {
        float step = st->drag_h + CUI__KB_GAP;
        /* where the card shows with the lifted card's hole closed */
        float closed = (b->col == st->from_col && b->passed_drag) ? -step : 0;
        if (b->col_is_target) {
            bool below = st->kbd ? b->card_pos < st->kidx : ui->my > kr.y + closed + kr.h * 0.5f;
            if (below) {
                b->target_index = b->card_pos + 1;
                b->hole_y = kr.y + closed + kr.h + CUI__KB_GAP;
            } else {
                closed += step;   /* below the drop point: make room */
            }
        }
        shift = closed;
    }
    if (dragged) b->passed_drag = true; else b->card_pos++;
    cui__spring(ui, &cs->y, shift, CUI_SPRING_SMOOTH);
    cui__spring(ui, &cs->sx, 0, CUI_SPRING_SMOOTH);
    cui__spring(ui, &cs->sy, 0, CUI_SPRING_SMOOTH);
    cui__tween(ui, &cs->hover, (r.hovered && st->drag_card < 0) ? 1.0f : 0.0f, CUI__T_HOVER);
    bool settling = card_id == st->settle_card && (fabsf(cs->sx.x) > 0.5f || fabsf(cs->sy.x) > 0.5f);
    int n = cui__node_new(ui, kid);
    cui_node *cn = &ui->nodes[n];
    cn->w = CUI_FILL; cn->gap = 8;
    cn->pad[0] = cn->pad[2] = 12; cn->pad[1] = cn->pad[3] = 12;
    cn->radius = CUI_CORNERS(ui->theme.radius_control - 2);
    cn->bg = CUI_C(ui, SURFACE);
    cn->border = 1;
    cn->border_color = cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), cs->hover);
    cn->elevation = CUI_ELEVATION_RESTING; cn->elevation_alpha = 1;
    cn->tx = cs->sx.x;
    cn->ty = cs->y.x + cs->sy.x - 1.5f * cs->hover;   /* a hovered card rises a little */
    if (cs->hover > 0.01f) { cn->elevation = CUI_ELEVATION_RAISED; cn->elevation_alpha = 0.4f + 0.6f * cs->hover; }
    if (dragged && kr.w > 0 && st->kbd) {
        cui__spring(ui, &st->kx, st->hole_x, CUI_SPRING_SMOOTH);
        cui__spring(ui, &st->ky, st->hole_y, CUI_SPRING_SMOOTH);
        st->slot_x = kr.x; st->slot_y = kr.y;
        cn->tx = st->kx.x - kr.x;
        cn->ty = st->ky.x - kr.y;
    } else if (dragged && kr.w > 0) {
        st->slot_x = kr.x; st->slot_y = kr.y;
        cn->tx = ui->mx - st->grab_x - kr.x;
        cn->ty = ui->my - st->grab_y - kr.y;
    }
    if (dragged || settling) {
        cn->scale = 1.0f + 0.03f * st->lift.x;
        cn->elevation = CUI_ELEVATION_FLOATING;
        cn->elevation_alpha = CUI_MAX(0.25f, st->lift.x);
        cn->flags |= CUI_NF_ON_TOP;
        ui->nodes[b->col_node].flags |= CUI_NF_ON_TOP;   /* above the neighbouring columns too */
    }
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    if (r.hovered && st->drag_card < 0) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, n);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = kid;
    cui_push_id_int(ui, card_id);
    return r.clicked && st->drag_card < 0;
}

void cui_kanban_card_end(cui_ctx *ui) {
    cui_pop_id(ui);
    cui__close(ui);
}

void cui_kanban_column_end(cui_ctx *ui) {
    struct cui_kanban_build *b = (struct cui_kanban_build *)ui->kanban;
    cui_pop_id(ui);
    if (b) {
        cui__kb_state *st = b->st;
        int c = b->col;
        cui_id cid = cui__hash(&c, sizeof c, b->id ^ 0xC01u);
        float *grow = &((cui__kb_col_state *)cui_state(ui, cid, sizeof(cui__kb_col_state)))->grow;
        bool opens = b->col_is_target && st->drag_card >= 0;
        /* room for the card when it comes from another column */
        cui__tween(ui, grow, (opens && st->from_col != c) ? st->drag_h + CUI__KB_GAP : 0.0f, 0.16f);
        if (*grow > 0.5f) cui_gap(ui, *grow - CUI__KB_GAP);
        cui_node *h = &ui->nodes[b->hole_node];
        if (opens) {
            cui_rect cr = cui__prev_rect(ui, cid);
            h->fy = b->hole_y - cr.y;
            h->h = CUI_PX(st->drag_h);
            /* a flat, quiet slot: a hairline and the faintest fill, no tinted glow */
            bool neutral = ui->accent == CUI_ACCENT_NEUTRAL;
            h->bg = neutral ? cui_alpha(CUI_C(ui, FOREGROUND), 0.025f) : cui_alpha(cui__acc(ui), ui->mode == CUI_THEME_DARK ? 0.07f : 0.045f);
            h->border = 1;
            h->border_color = neutral ? CUI_C(ui, BORDER_STRONG) : cui_alpha(cui__acc(ui), 0.4f);
        }
    }
    cui__close(ui);
}

bool cui_kanban_end(cui_ctx *ui, cui_kanban_move *move) {
    struct cui_kanban_build *b = (struct cui_kanban_build *)ui->kanban;
    if (!b) return false;
    cui_pop_id(ui);
    cui__close(ui);
    cui__scroll_end_ex(ui);
    cui__kb_state *st = b->st;
    bool dropped = false;
    st->ncols = b->col + 1;
    if (st->kbd && st->drag_card >= 0) {
        if (b->target_col >= 0) { st->kidx = b->target_index; st->hole_x = b->hole_x; st->hole_y = b->hole_y; }
        if (st->kdrop && b->target_col >= 0) {
            dropped = true;
            if (move) { move->card = st->drag_card; move->from_column = st->from_col; move->to_column = b->target_col; move->to_index = b->target_index; }
            st->settle_dx = st->kx.x - b->hole_x;
            st->settle_dy = st->ky.x - b->hole_y;
            st->settle_card = st->drag_card;
            st->settle_pending = true;
            st->gen++;
            st->drag_card = -1;
            st->kbd = st->kdrop = false;
        }
    } else if (st->drag_card >= 0 && !ui->mdown[0]) {
        if (b->target_col >= 0) {
            dropped = true;
            if (move) { move->card = st->drag_card; move->from_column = st->from_col; move->to_column = b->target_col; move->to_index = b->target_index; }
            /* glide from where it was let go into its new slot */
            st->settle_dx = ui->mx - st->grab_x - b->hole_x;
            st->settle_dy = ui->my - st->grab_y - b->hole_y;
            st->gen++;
        } else {
            /* let go outside the board: glide home */
            st->settle_dx = ui->mx - st->grab_x - st->slot_x;
            st->settle_dy = ui->my - st->grab_y - st->slot_y;
        }
        st->settle_card = st->drag_card;
        st->settle_pending = true;
        st->drag_card = -1;
    }
    ui->kanban = b->prev;
    ui->last_id = b->id;
    return dropped;
}

/* ------------------------------------------------------------------------ */
/* Week calendar                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { int drag; int mode; float grab; bool moved; float press_x, press_y; } cui__week_state;   /* mode 1 move, 2 resize */

typedef struct { float h0, h1, hh, gutter, dw; int today; float now; cui_color line, faint, label, now_c; } cui__week_paint;

static void cui__paint_week_grid(cui_ctx *ui, cui_node *n, void *data) {
    cui__week_paint *p = data;
    cui_rect r = n->rect;
    float fs = cui__fs(ui, ui->theme.text_xs);
    for (int h = (int)p->h0; h <= (int)p->h1; h++) {
        float y = r.y + (h - p->h0) * p->hh;
        /* the frame's own border closes the last hour, so its rounded corners stay clean */
        if (h < (int)p->h1) cui_draw_rect(ui, (cui_rect){ r.x + p->gutter, y, r.w - p->gutter, 1 }, CUI_CORNERS(0), p->line);
        if (h < (int)p->h1) cui_draw_rect(ui, (cui_rect){ r.x + p->gutter, y + p->hh * 0.5f, r.w - p->gutter, 1 }, CUI_CORNERS(0), p->faint);
        if (h > (int)p->h0 && h < (int)p->h1) {
            const char *s = cui__frame_printf(ui, "%02d:00", h);
            float w = cui__text_width(ui, CUI_FONT_BODY, fs, s, -1, true);
            cui__draw_text_line(ui, CUI_FONT_BODY, fs, r.x + p->gutter - 10 - w, y - cui__font_line_height(ui, CUI_FONT_BODY, fs) * 0.5f, p->label, s, -1, true);
        }
    }
    for (int d = 0; d < 7; d++) cui_draw_rect(ui, (cui_rect){ r.x + p->gutter + d * p->dw, r.y, 1, r.h }, CUI_CORNERS(0), p->line);
    if (p->today >= 0 && p->now >= p->h0 && p->now <= p->h1) {
        float y = r.y + (p->now - p->h0) * p->hh;
        float x = r.x + p->gutter + p->today * p->dw;
        cui_draw_rect(ui, (cui_rect){ x, y - 1, p->dw, 2 }, CUI_CORNERS(1), p->now_c);
        cui_draw_circle(ui, (cui_vec2){ x, y }, 4.5f, p->now_c);
    }
}

typedef struct { cui_color bar; } cui__evbar_paint;
static void cui__paint_event_bar(cui_ctx *ui, cui_node *n, void *data) {
    cui__evbar_paint *p = data;
    cui_rect r = n->rect;
    cui_draw_rect(ui, (cui_rect){ r.x + 3, r.y + 4, 3, r.h - 8 }, CUI_CORNERS(1.5f), p->bar);
}

static const char *cui__wd_short[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

#define CUI__WEEK_MIN_DAY 76   /* narrowest readable day column */

int cui_week_calendar_ex(cui_ctx *ui, const char *id_str, cui_date week_start, cui_cal_event *ev, int count, const cui_week_opts *o) {
    cui_week_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float h0 = o->end_hour > o->start_hour ? o->start_hour : 7, h1 = o->end_hour > o->start_hour ? o->end_hour : 21;
    float hh = o->hour_height > 0 ? o->hour_height : 52;
    float gutter = 60;
    cui_id id = cui_make_id(ui, id_str);
    cui_id gid = id ^ 0x6E1Du;
    cui__week_state *st = cui_state(ui, id, sizeof *st);
    cui_rect gr = cui__prev_rect(ui, gid);
    float dw = gr.w > gutter ? (gr.w - gutter) / 7 : 110;
    int clicked = -1;
    int base = cui_date_to_days(week_start);
    cui_date today = cui_date_today();
    int tday = cui_date_to_days(today) - base;
    if (tday < 0 || tday > 6) tday = -1;
    time_t tn = time(NULL);
    struct tm lt;
#if defined(_WIN32)
    localtime_s(&lt, &tn);
#else
    localtime_r(&tn, &lt);
#endif
    float now = lt.tm_hour + lt.tm_min / 60.0f;
    /* drag in progress */
    if (st->drag > 0 && st->drag <= count && !o->read_only) {
        cui_cal_event *e = &ev[st->drag - 1];
        if (ui->mdown[0]) {
            if (fabsf(ui->mx - st->press_x) + fabsf(ui->my - st->press_y) > 4) st->moved = true;
            if (st->moved && gr.w > 0) {
                float gy = ui->my - gr.y;
                if (st->mode == 1) {
                    float dur = e->end - e->start;
                    int day = (int)floorf((ui->mx - gr.x - gutter) / dw);
                    e->day = CUI_CLAMP(day, 0, 6);
                    float start = roundf(((gy - st->grab) / hh + h0) * 4) / 4;
                    start = cui__clampf(start, h0, h1 - dur);
                    e->start = start; e->end = start + dur;
                } else {
                    float end = roundf((gy / hh + h0) * 4) / 4;
                    e->end = cui__clampf(end, e->start + 0.25f, h1);
                }
            }
            ui->active = id;
            cui__set_cursor(ui, st->mode == 2 ? CUI_CURSOR_RESIZE_NS : CUI_CURSOR_MOVE);
        } else {
            if (!st->moved) clicked = st->drag - 1;
            st->drag = 0;
        }
    }
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->w = CUI_FILL;
    on->radius = CUI_CORNERS(ui->theme.radius_panel);
    on->border = 1; on->border_color = CUI_C(ui, BORDER);
    on->bg = CUI_C(ui, SURFACE);
    on->flags |= CUI_NF_CLIP;
    cui__open(ui, outer);
    /* narrower than seven readable days, the week scrolls sideways */
    cui_box xb;
    memset(&xb, 0, sizeof xb);
    xb.width = CUI_FILL;
    int xs = cui__scroll_begin_ex(ui, id ^ 0x7EE4u, &xb, true);
    ui->nodes[xs].flags &= ~(uint32_t)CUI_NF_SCROLL_Y;
    int content = cui__node_new(ui, 0);
    ui->nodes[content].pref_hint = gutter + 7 * CUI__WEEK_MIN_DAY;
    ui->nodes[content].self_align = CUI_ALIGN_STRETCH;
    cui__open(ui, content);
    /* day headers */
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].w = CUI_FILL; ui->nodes[head].h = CUI_PX(56);
    ui->nodes[head].align = CUI_ALIGN_CENTER;
    cui__open(ui, head);
    cui_gap(ui, gutter);
    for (int k = 0; k < 7; k++) {
        cui_date dd = cui_date_from_days(base + k);
        bool is_today = k == tday;
        int c = cui__node_new(ui, 0);
        ui->nodes[c].w = CUI_FILL; ui->nodes[c].dir = CUI_ROW; ui->nodes[c].gap = 8;
        ui->nodes[c].align = CUI_ALIGN_CENTER; ui->nodes[c].justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, c);
        int t = cui__text_node(ui, cui__wd_short[k], -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), is_today ? cui__acc(ui) : CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[t].line_height = 1.3f;
        int nb = cui__node_new(ui, 0);
        cui_node *nn = &ui->nodes[nb];
        nn->w = CUI_PX(30); nn->h = CUI_PX(30); nn->radius = CUI_CORNERS(15);
        nn->dir = CUI_STACK; nn->align = CUI_ALIGN_CENTER; nn->justify = CUI_JUSTIFY_CENTER;
        if (is_today) nn->bg = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_ON) : CUI_C(ui, ACCENT);
        cui__open(ui, nb);
        int dn = cui__text_node(ui, cui__frame_printf(ui, "%d", dd.day), -1, CUI_FONT_DISPLAY, cui__fs(ui, 16),
                                is_today ? (ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, ACCENT_FOREGROUND)) : CUI_C(ui, FOREGROUND), false);
        ui->nodes[dn].line_height = 1.0f;
        ui->nodes[dn].flags |= CUI_NF_TABULAR;
        cui__close(ui);
        cui__close(ui);
    }
    cui__close(ui);
    int hline = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[hline].bg = CUI_C(ui, BORDER);
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL;
    sb.height = o->height.kind ? o->height : CUI_PX(460);
    cui__scroll_begin_ex(ui, id ^ 0x5C3u, &sb, false);
    float total_h = (h1 - h0) * hh;
    int grid = cui__node_new(ui, gid);
    cui_node *gn = &ui->nodes[grid];
    gn->w = CUI_FILL; gn->h = CUI_PX(total_h + 1);
    cui__week_paint *wp = cui__frame_alloc(ui, sizeof *wp);
    wp->h0 = h0; wp->h1 = h1; wp->hh = hh; wp->gutter = gutter; wp->dw = dw; wp->today = tday; wp->now = now;
    wp->line = CUI_C(ui, BORDER_SUBTLE); wp->faint = cui_alpha(CUI_C(ui, BORDER_SUBTLE), 0.5f);
    wp->label = CUI_C(ui, TEXT_MUTED); wp->now_c = CUI_C(ui, DANGER);
    gn->paint = cui__paint_week_grid;
    gn->paint_data = wp;
    cui__open(ui, grid);
    if (tday >= 0) cui__wakeup_after(ui, 60.0f - (float)lt.tm_sec);
    /* lay out overlapping events side by side within each day */
    int *colk = cui__frame_alloc(ui, sizeof(int) * (size_t)CUI_MAX(1, count));
    int *ncol = cui__frame_alloc(ui, sizeof(int) * (size_t)CUI_MAX(1, count));
    for (int k = 0; k < count; k++) { colk[k] = 0; ncol[k] = 1; }
    for (int k = 0; k < count; k++) {
        int c = 0;
        for (;;) {
            bool clash = false;
            for (int j = 0; j < k; j++)
                if (ev[j].day == ev[k].day && colk[j] == c && ev[j].start < ev[k].end && ev[k].start < ev[j].end) clash = true;
            if (!clash) break;
            c++;
        }
        colk[k] = c;
    }
    /* events that start together split the day into lanes; one that starts at least half an hour
       into another cascades over it, so the earlier title stays readable */
    bool *lanes = cui__frame_alloc(ui, sizeof(bool) * (size_t)CUI_MAX(1, count));
    for (int k = 0; k < count; k++) { lanes[k] = false; ncol[k] = colk[k] + 1; }
    for (int k = 0; k < count; k++)
        for (int j = 0; j < count; j++)
            if (j != k && ev[j].day == ev[k].day && ev[j].start < ev[k].end && ev[k].start < ev[j].end) {
                ncol[k] = CUI_MAX(ncol[k], colk[j] + 1);
                if (fabsf(ev[j].start - ev[k].start) < 0.5f) lanes[k] = true;
            }
    for (int k = 0; k < count; k++) {
        cui_cal_event *e = &ev[k];
        if (e->day < 0 || e->day > 6 || e->end <= h0 || e->start >= h1) continue;
        cui_id eid = cui__hash(&k, sizeof k, id ^ 0xE7u);
        cui_interact r = cui__interact(ui, eid, CUI_IF_DRAG | CUI_IF_FOCUSABLE);
        if (r.focus_visible && !o->read_only) {
            /* Up and Down move the event 15 minutes, Left and Right a day; Shift changes its end */
            float dur = e->end - e->start;
            float dt = cui_key_pressed(ui, CUI_KEY_DOWN) ? 0.25f : cui_key_pressed(ui, CUI_KEY_UP) ? -0.25f : 0;
            int dday = cui_key_pressed(ui, CUI_KEY_RIGHT) ? 1 : cui_key_pressed(ui, CUI_KEY_LEFT) ? -1 : 0;
            if (dt != 0) {
                if (ui->mods & CUI_MOD_SHIFT) e->end = cui__clampf(e->end + dt, e->start + 0.25f, h1);
                else { e->start = cui__clampf(e->start + dt, h0, h1 - dur); e->end = e->start + dur; }
                ui->kpressed[CUI_KEY_DOWN] = ui->kpressed[CUI_KEY_UP] = 0;
            }
            if (dday) { e->day = CUI_CLAMP(e->day + dday, 0, 6); ui->kpressed[CUI_KEY_RIGHT] = ui->kpressed[CUI_KEY_LEFT] = 0; }
            if (r.keyboard_activate) clicked = k;
        }
        cui_rect er = cui__prev_rect(ui, eid);
        if (r.hovered && ui->mpressed[0] && st->drag == 0) {
            st->drag = k + 1; st->moved = false; st->press_x = ui->mx; st->press_y = ui->my;
            st->mode = (!o->read_only && ui->my > er.y + er.h - 7) ? 2 : 1;
            st->grab = ui->my - er.y;
        }
        float lane_w = (dw - 6) / ncol[k], x;
        bool cascade = !lanes[k] && ncol[k] > 1;
        if (cascade) {
            float indent = CUI_MIN(dw * 0.3f, 32.0f) * (float)colk[k];
            x = gutter + e->day * dw + 3 + indent;
            lane_w = dw - 6 - indent;
        } else {
            x = gutter + e->day * dw + 3 + colk[k] * lane_w;
        }
        float y = (CUI_MAX(e->start, h0) - h0) * hh + 1;
        float h = (CUI_MIN(e->end, h1) - CUI_MAX(e->start, h0)) * hh - 2;
        bool short_ev = h < 30;
        cui_color tone = cui__tone_color(ui, e->tone ? e->tone : CUI_TONE_ACCENT);
        bool active = st->drag == k + 1 && st->moved;
        int en = cui__absolute(ui, eid, x, y, lane_w - 2, CUI_MAX(18.0f, h));
        cui_node *nd = &ui->nodes[en];
        nd->radius = CUI_CORNERS(CUI__R_SMALL);
        nd->bg = cui_mix(CUI_C(ui, SURFACE), tone, ui->mode == CUI_THEME_DARK ? (r.hovered ? 0.3f : 0.24f) : (r.hovered ? 0.2f : 0.14f));
        nd->pad[0] = 5; nd->pad[1] = 6; nd->pad[3] = 12; nd->pad[2] = 4;
        nd->gap = 1;
        nd->flags |= CUI_NF_CLIP;
        if (short_ev) {   /* too short for two lines: title and start time on one centered line */
            nd->pad[0] = nd->pad[2] = 0;
            nd->dir = CUI_ROW; nd->align = CUI_ALIGN_CENTER; nd->gap = 6;
        }
        if (cascade && colk[k] > 0) { nd->border = 1.5f; nd->border_color = CUI_C(ui, SURFACE); }
        if (active) { nd->elevation = CUI_ELEVATION_FLOATING; nd->elevation_alpha = 1; nd->flags |= CUI_NF_ON_TOP; }
        cui__evbar_paint *bp = cui__frame_alloc(ui, sizeof *bp);
        bp->bar = tone;
        nd->paint = cui__paint_event_bar;
        nd->paint_data = bp;
        cui__mark_interactive(ui, en, CUI_IF_DRAG | CUI_IF_FOCUSABLE);
        if (r.hovered && st->drag == 0) cui__set_cursor(ui, (!o->read_only && ui->my > er.y + er.h - 7) ? CUI_CURSOR_RESIZE_NS : CUI_CURSOR_HAND);
        cui__open(ui, en);
        cui_color fg = cui_mix(CUI_C(ui, FOREGROUND), tone, ui->mode == CUI_THEME_DARK ? 0.2f : 0.45f);
        int t = cui__text_node(ui, e->title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs + 0.5f), fg, false);
        ui->nodes[t].line_height = 1.3f;
        int sh = (int)e->start, sm = (int)roundf((e->start - sh) * 60), eh = (int)e->end, em = (int)roundf((e->end - eh) * 60);
        float meta_fs = cui__fs(ui, ui->theme.text_xs);
        if (short_ev) {
            const char *tm = cui__frame_printf(ui, "%d:%02d", sh, sm);
            float tw = cui__text_width(ui, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs + 0.5f), e->title, -1, false)
                     + cui__text_width(ui, CUI_FONT_BODY, meta_fs, tm, -1, true) + 6;
            if (tw <= lane_w - 2 - 18) {   /* the time only when the title still fits whole */
                int tt = cui__text_node(ui, tm, -1, CUI_FONT_BODY, meta_fs, cui_alpha(fg, 0.75f), false);
                ui->nodes[tt].line_height = 1.3f;
                ui->nodes[tt].flags |= CUI_NF_TABULAR;
            }
        } else if (h > 34 && lane_w > 78) {   /* narrow lanes keep just the title */
            const char *tm = cui__frame_printf(ui, "%d:%02d \xe2\x80\x93 %d:%02d", sh, sm, eh, em);
            if (cui__text_width(ui, CUI_FONT_BODY, meta_fs, tm, -1, true) > lane_w - 2 - 18) tm = cui__frame_printf(ui, "%d:%02d", sh, sm);
            int tt = cui__text_node(ui, tm, -1, CUI_FONT_BODY, meta_fs, cui_alpha(fg, 0.75f), false);
            ui->nodes[tt].line_height = 1.3f;
            ui->nodes[tt].flags |= CUI_NF_TABULAR;
            /* the place gets its own line when there is room for it, never a dangling "·…" */
            if (e->detail && h > 54) {
                int dt = cui__text_node(ui, e->detail, -1, CUI_FONT_BODY, meta_fs, cui_alpha(fg, 0.75f), false);
                ui->nodes[dt].line_height = 1.3f;
            }
        }
        cui__close(ui);
        cui__focus_ring(ui, en, r.focus_visible ? 1.0f : 0.0f);
    }
    cui__close(ui);
    cui__scroll_end_ex(ui);
    cui__close(ui);   /* content */
    cui__scroll_end_ex(ui);
    cui__close(ui);
    ui->last_id = id;
    return clicked;
}

/* ------------------------------------------------------------------------ */
/* Gantt                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct { int drag; int mode; int grab_day; int orig_start, orig_end; float press_x; bool moved; } cui__gantt_state;   /* mode 1 move, 2 left, 3 right */

typedef struct {
    cui_gantt_task *tasks;
    int count, days, today;
    float dw, rh, head_h;
    int first_weekday;
    cui_color weekend, line, today_c, dep;
} cui__gantt_paint;

static void cui__paint_gantt_body(cui_ctx *ui, cui_node *n, void *data) {
    cui__gantt_paint *p = data;
    cui_rect r = n->rect;
    for (int d = 0; d < p->days; d++) {
        int wd = (p->first_weekday + d) % 7;
        float x = r.x + d * p->dw;
        if (wd >= 5) cui_draw_rect(ui, (cui_rect){ x, r.y, p->dw, r.h }, CUI_CORNERS(0), p->weekend);
        if (wd == 0) cui_draw_rect(ui, (cui_rect){ x, r.y, 1, r.h }, CUI_CORNERS(0), p->line);
    }
    for (int k = 1; k < p->count; k++) cui_draw_rect(ui, (cui_rect){ r.x, r.y + k * p->rh, r.w, 1 }, CUI_CORNERS(0), cui_alpha(p->line, 0.5f));
    /* dependencies: elbow from the end of one bar to the start of another */
    for (int k = 0; k < p->count; k++) {
        int dep = p->tasks[k].depends_on;
        if (dep < 0 || dep >= p->count || dep == k) continue;
        cui_gantt_task *a = &p->tasks[dep], *b = &p->tasks[k];
        float x0 = r.x + a->end * p->dw, y0 = r.y + dep * p->rh + p->rh * 0.5f;
        float x1 = r.x + b->start * p->dw, y1 = r.y + k * p->rh + p->rh * 0.5f;
        float xm = x0 + 8;
        cui_draw_line(ui, (cui_vec2){ x0, y0 }, (cui_vec2){ xm, y0 }, 1.4f, p->dep);
        if (x1 - 10 >= xm) {
            cui_draw_line(ui, (cui_vec2){ xm, y0 }, (cui_vec2){ xm, y1 }, 1.4f, p->dep);
            cui_draw_line(ui, (cui_vec2){ xm, y1 }, (cui_vec2){ x1 - 2, y1 }, 1.4f, p->dep);
        } else {
            float ym = y0 + (y1 > y0 ? p->rh * 0.5f : -p->rh * 0.5f);
            cui_draw_line(ui, (cui_vec2){ xm, y0 }, (cui_vec2){ xm, ym }, 1.4f, p->dep);
            cui_draw_line(ui, (cui_vec2){ xm, ym }, (cui_vec2){ x1 - 10, ym }, 1.4f, p->dep);
            cui_draw_line(ui, (cui_vec2){ x1 - 10, ym }, (cui_vec2){ x1 - 10, y1 }, 1.4f, p->dep);
            cui_draw_line(ui, (cui_vec2){ x1 - 10, y1 }, (cui_vec2){ x1 - 2, y1 }, 1.4f, p->dep);
        }
        cui_vec2 head[3] = { { x1 - 1, y1 }, { x1 - 6, y1 - 3.5f }, { x1 - 6, y1 + 3.5f } };
        cui_draw_polygon(ui, head, 3, p->dep);
    }
    if (p->today >= 0 && p->today < p->days) {
        float x = r.x + (p->today + 0.5f) * p->dw;
        cui_draw_rect(ui, (cui_rect){ x - 0.75f, r.y, 1.5f, r.h }, CUI_CORNERS(0), p->today_c);
    }
}

typedef struct { float progress; cui_color fill, prog; bool milestone; } cui__gbar_paint;
static void cui__paint_gantt_bar(cui_ctx *ui, cui_node *n, void *data) {
    cui__gbar_paint *p = data;
    cui_rect r = n->rect;
    if (p->milestone) {
        cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
        float s = r.h * 0.5f;
        cui_vec2 pts[4] = { { c.x, c.y - s }, { c.x + s, c.y }, { c.x, c.y + s }, { c.x - s, c.y } };
        cui_draw_polygon(ui, pts, 4, p->prog);
        return;
    }
    cui_draw_rect(ui, r, n->radius, p->fill);
    if (p->progress > 0) {
        cui_push_clip(ui, (cui_rect){ r.x, r.y, r.w * cui__clampf(p->progress, 0, 1), r.h });
        cui_draw_rect(ui, r, n->radius, p->prog);
        cui_pop_clip(ui);
    }
}

int cui_gantt_ex(cui_ctx *ui, const char *id_str, cui_gantt_task *tasks, int count, cui_date origin, int days, const cui_gantt_opts *o) {
    cui_gantt_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float dw = o->day_width > 0 ? o->day_width : 30;
    float rh = o->row_height > 0 ? o->row_height : 40;
    float head_h = 52;
    cui_id id = cui_make_id(ui, id_str);
    cui_id body_id = id ^ 0xB0D7u;
    cui__gantt_state *st = cui_state(ui, id, sizeof *st);
    int clicked = -1;
    cui_rect br = cui__prev_rect(ui, body_id);
    if (st->drag > 0 && st->drag <= count) {
        cui_gantt_task *t = &tasks[st->drag - 1];
        if (ui->mdown[0]) {
            if (fabsf(ui->mx - st->press_x) > 3) st->moved = true;
            if (st->moved && !o->read_only) {
                int dd = (int)lroundf((ui->mx - st->press_x) / dw);
                int len = st->orig_end - st->orig_start;
                if (st->mode == 1) { t->start = CUI_CLAMP(st->orig_start + dd, 0, days - len); t->end = t->start + len; }
                else if (st->mode == 2) t->start = CUI_CLAMP(st->orig_start + dd, 0, st->orig_end - 1);
                else t->end = CUI_CLAMP(st->orig_end + dd, st->orig_start + 1, days);
            }
            ui->active = id;
            cui__set_cursor(ui, st->mode == 1 ? CUI_CURSOR_MOVE : CUI_CURSOR_RESIZE_EW);
        } else {
            if (!st->moved) clicked = st->drag - 1;
            st->drag = 0;
        }
    }
    (void)br;
    /* a phone: a slimmer task list without owners, so the timeline keeps most of the width */
    cui_rect opr = cui__prev_rect(ui, id);
    bool slim = opr.w > 0 && opr.w < 480;
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->dir = CUI_ROW; on->w = CUI_FILL; on->align = CUI_ALIGN_STRETCH;
    on->radius = CUI_CORNERS(ui->theme.radius_panel);
    on->border = 1; on->border_color = CUI_C(ui, BORDER);
    on->bg = CUI_C(ui, SURFACE);
    on->flags |= CUI_NF_CLIP;
    cui__open(ui, outer);
    /* task list */
    int list = cui__node_new(ui, 0);
    ui->nodes[list].w = CUI_PX(slim ? 128 : 230); ui->nodes[list].gap = 0;
    cui__open(ui, list);
    int lh = cui__node_new(ui, 0);
    ui->nodes[lh].dir = CUI_ROW; ui->nodes[lh].align = CUI_ALIGN_END; ui->nodes[lh].h = CUI_PX(head_h);
    ui->nodes[lh].pad[3] = slim ? 12 : 16; ui->nodes[lh].pad[2] = 9; ui->nodes[lh].w = CUI_FILL;
    ui->nodes[lh].bg = CUI_C(ui, SURFACE_MUTED);
    cui__open(ui, lh);
    int lt = cui__text_node(ui, "Task", 4, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[lt].line_height = 1.3f;
    cui__close(ui);
    int lline = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[lline].bg = CUI_C(ui, BORDER);
    for (int k = 0; k < count; k++) {
        int row = cui__node_new(ui, 0);
        cui_node *rn = &ui->nodes[row];
        rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER; rn->gap = 10;
        rn->h = CUI_PX(rh); rn->w = CUI_FILL; rn->pad[3] = slim ? 12 : 16; rn->pad[1] = slim ? 8 : 12;
        if (slim) rn->gap = 8;
        if (st->drag == k + 1) rn->bg = CUI_C(ui, HOVER);
        cui__open(ui, row);
        cui_color tone = cui__tone_color(ui, tasks[k].tone ? tasks[k].tone : CUI_TONE_ACCENT);
        int dot = cui__leaf(ui, 0, CUI_PX(8), CUI_PX(8));
        ui->nodes[dot].radius = CUI_CORNERS(tasks[k].milestone ? 1 : 4);
        ui->nodes[dot].bg = tone;
        int t = cui__text_node(ui, tasks[k].name, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = 1.3f;
        ui->nodes[t].w = CUI_FILL;
        if (tasks[k].owner && !slim) {
            cui_avatar_opts ao;
            memset(&ao, 0, sizeof ao);
            ao.size = 22;
            cui_avatar_ex(ui, tasks[k].owner, &ao);
        }
        cui__close(ui);
    }
    cui__close(ui);
    int vsep = cui__leaf(ui, 0, CUI_PX(1), CUI_FILL);
    ui->nodes[vsep].bg = CUI_C(ui, BORDER);
    /* timeline */
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL;
    cui_scroll_state *gss = cui_state(ui, id ^ 0x5C4u, sizeof *gss);
    gss->fade = cui__backdrop(ui);   /* days past the edge fade out */
    cui__scroll_begin_ex(ui, id ^ 0x5C4u, &sb, true);
    ui->nodes[cui__current(ui)].h = CUI_FIT;
    ui->nodes[cui__current(ui)].min_h = 0;
    int tl = cui__node_new(ui, 0);
    ui->nodes[tl].w = CUI_PX(days * dw);
    cui__open(ui, tl);
    int base = cui_date_to_days(origin);
    int first_wd = ((base + 3) % 7 + 7) % 7;
    /* header: months, then day numbers */
    int hh = cui__node_new(ui, 0);
    ui->nodes[hh].h = CUI_PX(head_h); ui->nodes[hh].w = CUI_PX(days * dw);
    ui->nodes[hh].bg = CUI_C(ui, SURFACE_MUTED);
    cui__open(ui, hh);
    int mrow = cui__node_new(ui, 0);
    ui->nodes[mrow].dir = CUI_ROW; ui->nodes[mrow].h = CUI_PX(24);
    cui__open(ui, mrow);
    static const char *mon[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    for (int dd = 0; dd < days;) {
        cui_date cd = cui_date_from_days(base + dd);
        int left = 0;
        while (dd + left < days && cui_date_from_days(base + dd + left).month == cd.month) left++;
        int mc = cui__node_new(ui, 0);
        ui->nodes[mc].w = CUI_PX(left * dw); ui->nodes[mc].pad[3] = 8; ui->nodes[mc].dir = CUI_ROW; ui->nodes[mc].align = CUI_ALIGN_END;
        ui->nodes[mc].flags |= CUI_NF_CLIP;
        cui__open(ui, mc);
        int mt = cui__text_node(ui, cui__frame_printf(ui, "%s %d", mon[cd.month - 1], cd.year), -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs),
                                CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[mt].line_height = 1.6f;
        cui__close(ui);
        dd += left;
    }
    cui__close(ui);
    int drow = cui__node_new(ui, 0);
    ui->nodes[drow].dir = CUI_ROW; ui->nodes[drow].h = CUI_PX(28); ui->nodes[drow].align = CUI_ALIGN_CENTER;
    cui__open(ui, drow);
    for (int dd = 0; dd < days; dd++) {
        cui_date cd = cui_date_from_days(base + dd);
        bool is_today = dd == o->today && o->today > 0;
        int dc = cui__node_new(ui, 0);
        ui->nodes[dc].w = CUI_PX(dw); ui->nodes[dc].dir = CUI_ROW; ui->nodes[dc].justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, dc);
        int wd = (first_wd + dd) % 7;
        int dt = cui__text_node(ui, cui__frame_printf(ui, "%d", cd.day), -1, is_today ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs),
                                is_today ? CUI_C(ui, DANGER) : (wd >= 5 ? CUI_C(ui, TEXT_MUTED) : CUI_C(ui, TEXT_SECONDARY)), false);
        ui->nodes[dt].line_height = 1.3f;
        ui->nodes[dt].flags |= CUI_NF_TABULAR;
        cui__close(ui);
    }
    cui__close(ui);
    cui__close(ui);
    int tline = cui__leaf(ui, 0, CUI_PX(days * dw), CUI_PX(1));
    ui->nodes[tline].bg = CUI_C(ui, BORDER);
    /* body */
    int body = cui__node_new(ui, body_id);
    cui_node *bn = &ui->nodes[body];
    bn->w = CUI_PX(days * dw); bn->h = CUI_PX(count * rh);
    cui__gantt_paint *gp = cui__frame_alloc(ui, sizeof *gp);
    gp->tasks = cui__frame_alloc(ui, sizeof(cui_gantt_task) * (size_t)CUI_MAX(1, count));
    memcpy(gp->tasks, tasks, sizeof(cui_gantt_task) * (size_t)count);
    gp->count = count; gp->days = days; gp->today = o->today > 0 ? o->today : -1;
    gp->dw = dw; gp->rh = rh; gp->head_h = head_h; gp->first_weekday = first_wd;
    gp->weekend = cui_alpha(CUI_C(ui, FOREGROUND), ui->mode == CUI_THEME_DARK ? 0.025f : 0.022f);
    gp->line = CUI_C(ui, BORDER_SUBTLE);
    gp->today_c = cui_alpha(CUI_C(ui, DANGER), 0.8f);
    gp->dep = CUI_C(ui, TEXT_MUTED);
    bn->paint = cui__paint_gantt_body;
    bn->paint_data = gp;
    cui__open(ui, body);
    for (int k = 0; k < count; k++) {
        cui_gantt_task *t = &tasks[k];
        cui_id tid = cui__hash(&k, sizeof k, id ^ 0x6A7u);
        cui_interact r = cui__interact(ui, tid, CUI_IF_DRAG | CUI_IF_FOCUSABLE);
        if (r.focus_visible && !o->read_only) {
            /* arrows move the task a day; with Shift they change its length */
            int dd = cui_key_pressed(ui, CUI_KEY_RIGHT) ? 1 : cui_key_pressed(ui, CUI_KEY_LEFT) ? -1 : 0;
            if (dd) {
                int len = t->end - t->start;
                if (ui->mods & CUI_MOD_SHIFT) { if (!t->milestone) t->end = CUI_CLAMP(t->end + dd, t->start + 1, days); }
                else { t->start = CUI_CLAMP(t->start + dd, 0, days - len); t->end = t->start + len; }
                ui->kpressed[CUI_KEY_RIGHT] = ui->kpressed[CUI_KEY_LEFT] = 0;
            }
            if (r.keyboard_activate) clicked = k;
        }
        cui_rect tr = cui__prev_rect(ui, tid);
        bool near_l = r.hovered && ui->mx < tr.x + 7 && !t->milestone, near_r = r.hovered && ui->mx > tr.x + tr.w - 7 && !t->milestone;
        if (r.hovered && ui->mpressed[0] && st->drag == 0) {
            st->drag = k + 1; st->moved = false; st->press_x = ui->mx;
            st->orig_start = t->start; st->orig_end = t->end;
            st->mode = near_l ? 2 : near_r ? 3 : 1;
        }
        cui_color tone = cui__tone_color(ui, t->tone ? t->tone : CUI_TONE_ACCENT);
        float x = t->start * dw + 2, w = CUI_MAX(dw - 4, (t->end - t->start) * dw - 4);
        float bh = rh - 16;
        if (t->milestone) { w = bh + 4; x = t->start * dw - w * 0.5f; }
        int bnode = cui__absolute(ui, tid, x, k * rh + 8, w, bh);
        cui_node *nd = &ui->nodes[bnode];
        nd->radius = CUI_CORNERS(CUI__R_SMALL);
        nd->dir = CUI_ROW; nd->align = CUI_ALIGN_CENTER; nd->pad[3] = 10; nd->pad[1] = 8;
        nd->flags |= CUI_NF_CLIP;
        if (st->drag == k + 1 && st->moved) { nd->elevation = CUI_ELEVATION_RAISED; nd->elevation_alpha = 1; nd->flags |= CUI_NF_ON_TOP; }
        cui__gbar_paint *bp = cui__frame_alloc(ui, sizeof *bp);
        bp->progress = t->progress;
        bp->fill = cui_mix(CUI_C(ui, SURFACE), tone, ui->mode == CUI_THEME_DARK ? (r.hovered ? 0.36f : 0.28f) : (r.hovered ? 0.3f : 0.22f));
        /* in dark mode the tones are light, so the done part is deepened until white labels read on it */
        bp->prog = ui->mode == CUI_THEME_DARK ? cui_mix(tone, CUI_C(ui, SURFACE), 0.5f) : tone;
        bp->milestone = t->milestone;
        nd->paint = cui__paint_gantt_bar;
        nd->paint_data = bp;
        cui__mark_interactive(ui, bnode, CUI_IF_DRAG | CUI_IF_FOCUSABLE);
        if (r.hovered && st->drag == 0) cui__set_cursor(ui, (near_l || near_r) ? CUI_CURSOR_RESIZE_EW : CUI_CURSOR_HAND);
        if (!t->milestone && w > 60) {
            cui__open(ui, bnode);
            bool light_text = t->progress > 0.5f;
            cui_color fg = light_text ? cui__rgba(255, 255, 255, 255) : cui_mix(CUI_C(ui, FOREGROUND), tone, 0.3f);
            int bt = cui__text_node(ui, t->name, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), fg, false);
            ui->nodes[bt].line_height = 1.2f;
            cui__close(ui);
        }
        cui__focus_ring(ui, bnode, r.focus_visible ? 1.0f : 0.0f);
    }
    cui__close(ui);   /* body */
    cui__close(ui);   /* timeline */
    cui__scroll_end_ex(ui);
    cui__close(ui);   /* outer */
    ui->last_id = id;
    return clicked;
}
