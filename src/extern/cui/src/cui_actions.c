/* More actions and inputs: split button, toggle group, confirm and status
 * buttons, reactions, speed dial, theme switcher, user menu, swipe actions,
 * inline edit, scrub field, money field and shortcut recorder. */
#include "cui_internal.h"

#define cui__fs cui__fsz


static int cui__find_node(cui_ctx *ui, cui_id id) {
    for (int k = ui->node_count - 1; k >= 0; k--) if (ui->nodes[k].id == id) return k;
    return -1;
}

static float cui__btn_font(cui_ctx *ui, cui_control_size s) {
    return cui__fs(ui, s == CUI_SIZE_SM ? ui->theme.text_sm : s == CUI_SIZE_LG ? ui->theme.text_lg - 1 : ui->theme.text_body);
}
static float cui__btn_pad(cui_control_size s) { return s == CUI_SIZE_SM ? 12 : s == CUI_SIZE_LG ? 20 : 15; }
static float cui__btn_icon(cui_control_size s) { return s == CUI_SIZE_SM ? 14 : s == CUI_SIZE_LG ? 18 : 16; }

/* ------------------------------------------------------------------------ */
/* Split button                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { cui_color c; } cui__divider_paint;

static void cui__paint_split_divider(cui_ctx *ui, cui_node *n, void *data) {
    cui__divider_paint *p = data;
    int second = n->first >= 0 ? ui->nodes[n->first].next : -1;
    if (second < 0) return;
    cui_rect r = ui->nodes[second].rect;
    cui_draw_rect(ui, (cui_rect){ r.x - 0.5f, n->rect.y + n->rect.h * 0.22f, 1, n->rect.h * 0.56f }, CUI_CORNERS(0), p->c);
}

int cui_split_button_ex(cui_ctx *ui, const char *label, const char *const *items, int count, const cui_split_button_opts *o) {
    cui_split_button_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_button_variant v = o->variant ? o->variant : CUI_BUTTON_PRIMARY;
    cui_id gid = cui_make_id(ui, label) ^ 0x5B17u;
    cui_id pid = gid ^ 0x3E40u;
    cui_popup *p = cui__popup_get(ui, pid, true);
    bool open = p && p->open;
    float h = cui__control_height(ui, o->size);
    float rad = CUI_MIN(ui->theme.radius_control, h * 0.5f);
    int g = cui__node_new(ui, gid);
    cui_node *gn = &ui->nodes[g];
    gn->dir = CUI_ROW;
    gn->self_align = CUI__ALIGN_NOSTRETCH;
    gn->radius = CUI_CORNERS(rad);
    gn->flags |= CUI_NF_CLIP | CUI_NF_PAINT_AFTER;
    cui__divider_paint *dp = cui__frame_alloc(ui, sizeof *dp);
    cui__btn_colors col = cui__button_colors(ui, v, false);
    bool filled = v == CUI_BUTTON_PRIMARY || v == CUI_BUTTON_DANGER;
    dp->c = filled ? cui_alpha(col.fg, 0.28f) : cui__quiet_edge(ui, 0);
    if (v == CUI_BUTTON_OUTLINE) { gn->border = 1; gn->border_color = cui__quiet_edge(ui, 0); }
    gn->paint = cui__paint_split_divider;
    gn->paint_data = dp;
    cui__open(ui, g);
    cui_push_id(ui, label);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.variant = v; bo.size = o->size; bo.icon = o->icon; bo.disabled = o->disabled;
    bool main = cui_button_ex(ui, label, &bo);
    cui_node *mn = &ui->nodes[ui->last_node];
    mn->radius.tr = mn->radius.br = 0;
    mn->scale = 1;
    mn->border = 0;
    mn->pad[1] = cui__btn_pad(o->size) - 2;
    cui_button_opts co = bo;
    co.icon = NULL;
    co.tooltip = "More options";
    co.selected = open;
    bool chev = cui_icon_button_ex(ui, CUI_ICON_CHEVRON_DOWN, &co);
    cui_node *cn = &ui->nodes[ui->last_node];
    cn->radius.tl = cn->radius.bl = 0;
    cn->scale = 1;
    cn->border = 0;
    cn->w = CUI_PX(h - 4);
    if (open && v != CUI_BUTTON_OUTLINE && v != CUI_BUTTON_GHOST) cn->bg = col.bg_press;
    cui_id chev_id = ui->last_id;
    cui_pop_id(ui);
    cui__close(ui);
    if (chev && p) cui__popup_set(ui, pid, !p->open);
    if (p && !p->open && ui->focus == chev_id && ui->focus_visible && cui_key_pressed(ui, CUI_KEY_DOWN)) cui__popup_set(ui, pid, true);
    int result = main ? 0 : -1;
    if (cui__popup_begin(ui, pid, gid, CUI_PLACE_BOTTOM_END, 0, 6, 0, true)) {
        for (int k = 0; k < count; k++) {
            cui_menu_item_opts mo;
            memset(&mo, 0, sizeof mo);
            mo.icon = o->item_icons ? o->item_icons[k] : NULL;
            cui_push_id_int(ui, k);
            if (cui_menu_item_ex(ui, items[k], &mo)) result = 1 + k;
            cui_pop_id(ui);
        }
        cui__popup_end(ui);
    }
    ui->last_id = gid;
    ui->last_node = g;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Toggle group                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { float t[32], hover[32]; bool init; } cui__tg_state;

bool cui_toggle_group_ex(cui_ctx *ui, const char *id_str, const char *const *labels, int count, bool *values, const cui_toggle_group_opts *o) {
    cui_toggle_group_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (count <= 0) return false;
    if (count > 32) count = 32;
    cui_id gid = cui_make_id(ui, id_str);
    cui__tg_state *st = cui_state(ui, gid, sizeof *st);
    bool changed = false;
    float h = cui__control_height(ui, o->size);
    float inner = h - 6;
    int row = cui__node_new(ui, gid);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER; rn->gap = 2;
    rn->pad[0] = rn->pad[1] = rn->pad[2] = rn->pad[3] = 3;
    rn->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, h * 0.5f));
    rn->border = 1; rn->border_color = CUI_C(ui, BORDER);
    rn->bg = CUI_C(ui, SURFACE);
    rn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    for (int k = 0; k < count; k++) {
        cui_id iid = cui__hash(&k, sizeof k, gid);
        cui_interact r = cui__interact(ui, iid, CUI_IF_FOCUSABLE);
        if (r.clicked) {
            if (o->exclusive) { for (int j = 0; j < count; j++) values[j] = j == k; }
            else values[k] = !values[k];
            changed = true;
        }
        if (r.focus_visible) {
            int to = -1;
            if (cui_key_pressed(ui, CUI_KEY_RIGHT) && k < count - 1) to = k + 1;
            if (cui_key_pressed(ui, CUI_KEY_LEFT) && k > 0) to = k - 1;
            if (to >= 0) ui->focus = cui__hash(&to, sizeof to, gid);
        }
        if (!st->init) st->t[k] = values[k] ? 1.0f : 0.0f;
        cui__tween(ui, &st->t[k], values[k] ? 1.0f : 0.0f, CUI__T_STATE);
        cui__tween(ui, &st->hover[k], r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        bool icon_only = o->icon_only && o->icons && o->icons[k];
        int it = cui__node_new(ui, iid);
        cui_node *in = &ui->nodes[it];
        in->dir = CUI_ROW; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER; in->gap = 6;
        in->h = CUI_PX(inner);
        if (icon_only) in->w = CUI_PX(inner);
        else in->pad[1] = in->pad[3] = 11;
        in->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control - 3, inner * 0.5f));
        cui_color on_bg = CUI_C(ui, ACTIVE);
        in->bg = cui_mix(cui_alpha(CUI_C(ui, HOVER), st->hover[k]), on_bg, st->t[k]);
        cui__mark_interactive(ui, it, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, it);
        cui_color fg = cui_mix(cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), st->hover[k] * 0.6f), CUI_C(ui, FOREGROUND), st->t[k]);
        if (o->icons && o->icons[k]) cui__icon_node(ui, o->icons[k], o->size == CUI_SIZE_SM ? 14 : 16, fg);
        if (!icon_only && labels && labels[k]) cui__label_text(ui, labels[k], CUI_FONT_MEDIUM, cui__btn_font(ui, o->size), fg);
        cui__close(ui);
        cui__focus_ring(ui, it, r.focus_visible ? 1.0f : 0.0f);
        if (icon_only && labels && labels[k]) {
            ui->last_id = iid;
            cui_tooltip(ui, cui__frame_strdup(ui, labels[k], cui__display_len(labels[k])));
        }
    }
    st->init = true;
    cui__close(ui);
    ui->last_id = gid;
    ui->last_node = row;
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Confirm and status buttons: shared morphing shell                         */
/* ------------------------------------------------------------------------ */

typedef struct {
    float remain;      /* countdown 0..1, <0 none */
    cui_color bar;
} cui__morph_paint;

static void cui__paint_morph(cui_ctx *ui, cui_node *n, void *data) {
    cui__morph_paint *p = data;
    if (p->remain <= 0) return;
    cui_rect r = n->rect;
    cui_draw_rect(ui, (cui_rect){ r.x, r.y + r.h - 2, r.w * p->remain, 2 }, CUI_CORNERS(1), p->bar);
}

typedef struct { cui_spring w, press; float hover, focus, t; bool init; int prev_kind; } cui__morph_state;

typedef struct {
    const char *icon;      /* NULL for none; "spin" for a spinner */
    const char *label;
    cui__btn_colors col;
} cui__morph_face;

typedef struct { cui_color c; } cui__spin_paint;
static void cui__paint_morph_spinner(cui_ctx *ui, cui_node *n, void *data) {
    cui__spin_paint *p = data;
    float r = CUI_MIN(n->rect.w, n->rect.h) * 0.5f;
    cui__draw_spinner(ui, (cui_vec2){ n->rect.x + n->rect.w * 0.5f, n->rect.y + n->rect.h * 0.5f }, r - 1, CUI_MAX(1.5f, r * 0.22f), p->c);
}

static float cui__face_width(cui_ctx *ui, const cui__morph_face *f, cui_control_size size) {
    float w = cui__btn_pad(size) * 2;
    int len = f->label ? cui__display_len(f->label) : 0;
    if (len) w += cui__text_width(ui, CUI_FONT_MEDIUM, cui__btn_font(ui, size), f->label, len, false);
    if (f->icon) w += cui__btn_icon(size) + (len ? 6 : 0);
    return w;
}

static void cui__face_content(cui_ctx *ui, const cui__morph_face *f, cui_control_size size, cui_color fg, float alpha, float ty) {
    int row = cui__node_new(ui, 0);
    cui_node *r = &ui->nodes[row];
    r->dir = CUI_ROW; r->align = CUI_ALIGN_CENTER; r->justify = CUI_JUSTIFY_CENTER; r->gap = 6;
    r->w = CUI_FILL; r->h = CUI_FILL;
    r->opacity = CUI_MAX(0.001f, alpha);
    r->ty = ty;
    cui__open(ui, row);
    float is = cui__btn_icon(size);
    if (f->icon && strcmp(f->icon, "spin") == 0) {
        int sp = cui__leaf(ui, 0, CUI_PX(is - 2), CUI_PX(is - 2));
        cui__spin_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->c = fg;
        ui->nodes[sp].paint = cui__paint_morph_spinner;
        ui->nodes[sp].paint_data = p;
        ui->animating = true;
    } else if (f->icon) {
        cui__icon_node(ui, f->icon, is, fg);
    }
    if (f->label && cui__display_len(f->label) > 0) {
        int t = cui__text_node(ui, f->label, cui__display_len(f->label), CUI_FONT_MEDIUM, cui__btn_font(ui, size), fg, false);
        ui->nodes[t].line_height = 1.2f;
    }
    cui__close(ui);
}

/* Draws a button whose content cross-fades between two faces and whose width
 * follows the active face. `t` is 0 for face a and 1 for face b. */
static void cui__morph_button(cui_ctx *ui, cui_id id, cui__morph_state *st, cui_interact r, const cui__morph_face *a,
                              const cui__morph_face *b, float t, bool width_b, cui_control_size size, float remain) {
    float h = cui__control_height(ui, size);
    float wa = cui__face_width(ui, a, size), wb = cui__face_width(ui, b, size);
    float target = width_b ? wb : wa;
    if (!st->init) { st->w.x = target; st->press.x = 1; st->init = true; }
    cui__spring(ui, &st->w, target, CUI_SPRING_MORPH);
    cui__spring(ui, &st->press, r.pressed ? CUI__PRESS : 1.0f, CUI_SPRING_SNAPPY);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    cui_color bga = cui_mix(a->col.bg, a->col.bg_hover, st->hover), bgb = cui_mix(b->col.bg, b->col.bg_hover, st->hover);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_STACK;
    n->w = CUI_PX(CUI_MAX(h, st->w.x));
    n->h = CUI_PX(h);
    n->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, h * 0.5f));
    n->bg = r.pressed ? cui_mix(a->col.bg_press, b->col.bg_press, t) : cui_mix(bga, bgb, t);
    cui_color border = cui_mix(a->col.border, b->col.border, t);
    if (border.a) { n->border = 1; n->border_color = border; }
    n->scale = st->press.x;
    n->self_align = CUI__ALIGN_NOSTRETCH;
    n->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    if (t < 0.999f) cui__face_content(ui, a, size, a->col.fg, 1.0f - t, -t * 10);
    if (t > 0.001f) cui__face_content(ui, b, size, b->col.fg, t, (1.0f - t) * 10);
    cui__close(ui);
    cui__morph_paint *mp = cui__frame_alloc(ui, sizeof *mp);
    mp->remain = remain;
    mp->bar = cui_alpha(b->col.fg, 0.45f);
    n = &ui->nodes[i];
    n->paint = cui__paint_morph;
    n->paint_data = mp;
    n->flags |= CUI_NF_PAINT_AFTER;
    if (st->focus > 0.01f) {
        /* focus ring replaces the countdown paint only while keyboard focused */
        cui__focus_ring(ui, i, st->focus);
    }
    ui->last_id = id;
    ui->last_node = i;
}

typedef struct { cui__morph_state m; bool armed; double armed_at; cui_spring arm; } cui__confirm_state;

bool cui_confirm_button_ex(cui_ctx *ui, const char *label, const cui_confirm_opts *o) {
    cui_confirm_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float timeout = o->timeout > 0 ? o->timeout : 3.0f;
    cui_id id = cui_make_id(ui, label);
    cui__confirm_state *st = cui_state(ui, id ^ 0xC0F1u, sizeof *st);
    bool disabled = cui_is_disabled(ui);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    bool fire = false;
    if (st->armed) {
        bool expired = ui->time - st->armed_at > timeout;
        bool away = (ui->mpressed[0] || ui->mpressed[1]) && ui->hot != id;
        bool esc = cui_key_pressed(ui, CUI_KEY_ESCAPE) && (ui->focus == id || r.hovered);
        if (expired || away || esc) st->armed = false;
        else cui__wakeup_after(ui, 0.05f);
        if (esc) ui->kpressed[CUI_KEY_ESCAPE] = 0;
    }
    if (r.clicked && !disabled) {
        if (st->armed) { fire = true; st->armed = false; }
        else { st->armed = true; st->armed_at = ui->time; }
    }
    if (!st->m.init) st->arm.x = st->armed ? 1.0f : 0.0f;
    cui__spring(ui, &st->arm, st->armed ? 1.0f : 0.0f, 0.3f, 0.0f);
    cui__morph_face a, b;
    a.icon = o->icon; a.label = label;
    a.col = cui__button_colors(ui, o->variant ? o->variant : CUI_BUTTON_SECONDARY, false);
    b.icon = CUI_ICON_TRIANGLE_ALERT; b.label = o->confirm_label ? o->confirm_label : "Click to confirm";
    b.col = cui__button_colors(ui, CUI_BUTTON_DANGER, false);
    float remain = st->armed ? 1.0f - (float)((ui->time - st->armed_at) / timeout) : -1.0f;
    float t = cui__clampf(st->arm.x, 0, 1);
    /* the width aims at the armed face right away; the faces cross-fade on the spring */
    cui__morph_button(ui, id, &st->m, r, &a, &b, t, st->armed, o->size, remain);
    if (disabled) ui->nodes[ui->last_node].opacity = CUI__DISABLED;
    return fire;
}

typedef struct { cui__morph_state m; cui_status shown, prev; float t; } cui__status_state;

bool cui_status_button_ex(cui_ctx *ui, const char *label, cui_status status, const cui_status_button_opts *o) {
    cui_status_button_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, label);
    cui__status_state *st = cui_state(ui, id ^ 0x57A7u, sizeof *st);
    if (!st->m.init) { st->shown = st->prev = status; st->t = 1; }
    if (status != st->shown) { st->prev = st->shown; st->shown = status; st->t = 0; }
    cui__tween(ui, &st->t, 1.0f, 0.22f);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_button_variant v = o->variant ? o->variant : CUI_BUTTON_PRIMARY;
    cui__morph_face faces[4];
    const char *busy = o->busy_label ? o->busy_label : cui__frame_printf(ui, "%.*s\xe2\x80\xa6", cui__display_len(label), label);
    faces[CUI_STATUS_IDLE] = (cui__morph_face){ o->icon, label, cui__button_colors(ui, v, false) };
    faces[CUI_STATUS_BUSY] = (cui__morph_face){ "spin", busy, cui__button_colors(ui, v, false) };
    cui__btn_colors ok = cui__button_colors(ui, v, false);
    if (v == CUI_BUTTON_PRIMARY) {
        ok.bg = ok.bg_hover = ok.bg_press = CUI_C(ui, SUCCESS);
        ok.fg = ui->mode == CUI_THEME_DARK ? cui__rgba(8, 26, 16, 255) : cui__rgba(255, 255, 255, 255);
    } else {
        ok.fg = CUI_C(ui, SUCCESS);
    }
    faces[CUI_STATUS_SUCCESS] = (cui__morph_face){ CUI_ICON_CHECK, o->success_label ? o->success_label : "Done", ok };
    cui__btn_colors bad = cui__button_colors(ui, CUI_BUTTON_DANGER, false);
    if (v != CUI_BUTTON_PRIMARY && v != CUI_BUTTON_DANGER) { bad = cui__button_colors(ui, v, false); bad.fg = CUI_C(ui, DANGER); }
    faces[CUI_STATUS_ERROR] = (cui__morph_face){ CUI_ICON_CIRCLE_ALERT, o->error_label ? o->error_label : "Failed", bad };
    int from = st->t >= 1 ? st->shown : st->prev;
    cui__morph_button(ui, id, &st->m, r, &faces[from], &faces[st->shown], st->t >= 1 ? 1.0f : st->t, true, o->size, -1);
    if (status == CUI_STATUS_BUSY) ui->nodes[ui->last_node].opacity = 0.92f;
    return r.clicked && status == CUI_STATUS_IDLE && !cui_is_disabled(ui);
}

/* ------------------------------------------------------------------------ */
/* Reactions                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { int shown, prev; float t; int dir; float on; cui_spring pop; bool init; } cui__react_state;

int cui_reactions(cui_ctx *ui, const char *id_str, cui_reaction *items, int count) {
    cui_id gid = cui_make_id(ui, id_str);
    int changed = -1;
    int row = cui__node_new(ui, gid);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 6; ui->nodes[row].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    for (int k = 0; k < count; k++) {
        cui_reaction *it = &items[k];
        cui_id iid = cui__hash(&k, sizeof k, gid);
        cui__react_state *st = cui_state(ui, iid, sizeof *st);
        if (!st->init) { st->shown = st->prev = it->count; st->t = 1; st->on = it->active; st->pop.x = 1; st->init = true; }
        cui_interact r = cui__interact(ui, iid, CUI_IF_FOCUSABLE);
        if (r.clicked) {
            it->active = !it->active;
            it->count += it->active ? 1 : -1;
            if (it->count < 0) it->count = 0;
            changed = k;
            if (it->active) st->pop.x = 1.18f;
        }
        if (it->count != st->shown) { st->dir = it->count > st->shown ? 1 : -1; st->prev = st->shown; st->shown = it->count; st->t = 0; }
        cui__tween(ui, &st->t, 1.0f, 0.2f);
        cui__tween(ui, &st->on, it->active ? 1.0f : 0.0f, CUI__T_STATE);
        cui__spring(ui, &st->pop, 1.0f, 0.35f, 0.35f);
        cui_color accent = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
        int pill = cui__node_new(ui, iid);
        cui_node *pn = &ui->nodes[pill];
        pn->dir = CUI_ROW; pn->align = CUI_ALIGN_CENTER; pn->gap = 6;
        pn->h = CUI_PX(30); pn->pad[1] = 11; pn->pad[3] = 10;
        pn->radius = CUI_CORNERS(15);
        cui_color base = r.hovered ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.06f) : CUI_C(ui, SURFACE);
        pn->bg = cui_mix(base, cui_mix(CUI_C(ui, SURFACE), accent, ui->mode == CUI_THEME_DARK ? 0.18f : 0.10f), st->on);
        pn->border = 1;
        pn->border_color = cui_mix(CUI_C(ui, BORDER), cui_alpha(accent, 0.55f), st->on);
        cui__mark_interactive(ui, pill, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, pill);
        cui__icon_node(ui, it->icon, 14, cui_mix(CUI_C(ui, TEXT_SECONDARY), accent, st->on));
        ui->nodes[ui->node_count - 1].scale = st->pop.x;
        /* rolling count */
        float fs = cui__fs(ui, ui->theme.text_sm);
        char a[16], b[16];
        snprintf(a, sizeof a, "%d", st->shown);
        snprintf(b, sizeof b, "%d", st->prev);
        float wa = cui__text_width(ui, CUI_FONT_MEDIUM, fs, a, -1, true), wb = cui__text_width(ui, CUI_FONT_MEDIUM, fs, b, -1, true);
        int box = cui__leaf(ui, 0, CUI_PX(st->t < 1 ? CUI_MAX(wa, wb) : wa), CUI_PX(18));
        cui_node *bn = &ui->nodes[box];
        bn->dir = CUI_STACK; bn->flags |= CUI_NF_CLIP; bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, box);
        cui_color fg = cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), st->on);
        if (st->t < 1) {
            int o1 = cui__text_node(ui, cui__frame_strdup(ui, b, -1), -1, CUI_FONT_MEDIUM, fs, fg, false);
            ui->nodes[o1].ty = -st->dir * 14 * st->t; ui->nodes[o1].opacity = CUI_MAX(0.001f, 1 - st->t);
            ui->nodes[o1].flags |= CUI_NF_TABULAR; ui->nodes[o1].line_height = 1.2f;
        }
        int o2 = cui__text_node(ui, cui__frame_strdup(ui, a, -1), -1, CUI_FONT_MEDIUM, fs, fg, false);
        ui->nodes[o2].ty = st->dir * 14 * (1 - st->t); ui->nodes[o2].opacity = CUI_MAX(0.001f, st->t);
        ui->nodes[o2].flags |= CUI_NF_TABULAR; ui->nodes[o2].line_height = 1.2f;
        cui__close(ui);
        cui__close(ui);
        cui__focus_ring(ui, pill, r.focus_visible ? 1.0f : 0.0f);
    }
    cui__close(ui);
    ui->last_id = gid;
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Speed dial                                                               */
/* ------------------------------------------------------------------------ */

typedef struct { bool open; cui_spring t, press; float hover; int z; } cui__dial_state;
typedef struct { float hover[16]; } cui__dial_items;

int cui_speed_dial_ex(cui_ctx *ui, const char *id_str, const cui_speed_action *actions, int count, const cui_speed_dial_opts *o) {
    cui_speed_dial_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (count > 16) count = 16;
    cui_id id = cui_make_id(ui, id_str);
    cui_id fid = id ^ 0xF10A7u;
    cui__dial_state *st = cui_state(ui, id, sizeof *st);
    cui__dial_items *its = cui_state(ui, id ^ 0x17E5u, sizeof *its);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    int result = -1;
    if (r.clicked) {
        st->open = !st->open;
        if (st->open) st->z = 1000 + ++ui->layer_seq;
    }
    if (st->open) {
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) { st->open = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
        if ((ui->mpressed[0] || ui->mpressed[1]) && ui->hot != id && ui->hot_layer != fid) st->open = false;
    }
    if (st->press.x == 0) st->press.x = 1;
    cui__spring(ui, &st->t, st->open ? 1.0f : 0.0f, st->open ? 0.42f : 0.24f, st->open ? 0.18f : 0.0f);
    cui__spring(ui, &st->press, r.pressed ? CUI__PRESS : 1.0f, CUI_SPRING_SNAPPY);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    float size = 52;
    cui__btn_colors col = cui__button_colors(ui, CUI_BUTTON_PRIMARY, false);
    int b = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[b];
    bn->w = CUI_PX(size); bn->h = CUI_PX(size);
    bn->radius = CUI_CORNERS(size * 0.5f);
    bn->bg = cui_mix(col.bg, col.bg_hover, st->hover);
    bn->dir = CUI_STACK; bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
    bn->elevation = CUI_ELEVATION_RAISED; bn->elevation_alpha = 1;
    bn->scale = st->press.x;
    bn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, b, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, b);
    cui__icon_swap(ui, id ^ 0x1C0u, st->open ? CUI_ICON_X : (o->icon ? o->icon : CUI_ICON_PLUS), 22, col.fg);
    cui__close(ui);
    cui__focus_ring(ui, b, r.focus_visible ? 1.0f : 0.0f);
    if (o->tooltip && !st->open) { ui->last_id = id; cui_tooltip(ui, o->tooltip); }
    float t = st->t.x;
    if (t > 0.01f && count > 0) {
        int place = o->placement ? o->placement : CUI_PLACE_TOP_END;
        bool below = place <= CUI_PLACE_BOTTOM_END;
        int root = cui__float_begin(ui, fid, st->z ? st->z : 1100, id, place, 12);
        cui_node *rn = &ui->nodes[root];
        rn->gap = 10; rn->align = CUI_ALIGN_END;
        rn->pad[1] = (size - 42) * 0.5f;
        if (!st->open) cui_push_disabled(ui, true);
        for (int j = 0; j < count; j++) {
            int k = below ? j : count - 1 - j;          /* k counts outward from the button */
            int dist = below ? j : count - 1 - j;
            float delay = 0.07f * (float)dist;
            float tk = cui__clampf((t - delay) / CUI_MAX(0.2f, 1.0f - delay), 0, 1.2f);
            cui_id aid = cui__hash(&k, sizeof k, id ^ 0xA0u);
            cui_interact ar = cui__interact(ui, aid, CUI_IF_FOCUSABLE);
            cui__tween(ui, &its->hover[k], ar.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
            if (ar.clicked) { result = k; st->open = false; }
            int row = cui__node_new(ui, aid);
            cui_node *an = &ui->nodes[row];
            an->dir = CUI_ROW; an->align = CUI_ALIGN_CENTER; an->gap = 10;
            an->opacity = CUI_MAX(0.001f, cui__clampf(tk, 0, 1));
            an->ty = (1.0f - tk) * (below ? -18.0f : 18.0f);
            an->scale = 0.85f + 0.15f * cui__clampf(tk, 0, 1);
            cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
            if (ar.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
            cui__open(ui, row);
            if (actions[k].label) {
                int lp = cui__node_new(ui, 0);
                cui_node *ln = &ui->nodes[lp];
                ln->pad[0] = ln->pad[2] = 5; ln->pad[1] = ln->pad[3] = 10;
                ln->radius = CUI_CORNERS(8);
                ln->bg = CUI_C(ui, SURFACE_RAISED);
                ln->border = 1; ln->border_color = CUI_C(ui, BORDER);
                ln->elevation = CUI_ELEVATION_RAISED; ln->elevation_alpha = 0.8f;
                cui__open(ui, lp);
                int tt = cui__text_node(ui, actions[k].label, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
                ui->nodes[tt].line_height = 1.3f;
                cui__close(ui);
            }
            int rb = cui__node_new(ui, 0);
            cui_node *rbn = &ui->nodes[rb];
            rbn->w = CUI_PX(42); rbn->h = CUI_PX(42); rbn->radius = CUI_CORNERS(21);
            rbn->bg = cui_mix(CUI_C(ui, SURFACE_RAISED), CUI_C(ui, FOREGROUND), 0.07f * its->hover[k]);
            rbn->border = 1; rbn->border_color = CUI_C(ui, BORDER);
            rbn->elevation = CUI_ELEVATION_RAISED; rbn->elevation_alpha = 1;
            rbn->dir = CUI_STACK; rbn->align = CUI_ALIGN_CENTER; rbn->justify = CUI_JUSTIFY_CENTER;
            cui__open(ui, rb);
            cui__icon_node(ui, actions[k].icon, 18, CUI_C(ui, FOREGROUND));
            cui__close(ui);
            cui__close(ui);
            cui__focus_ring(ui, rb, ar.focus_visible ? 1.0f : 0.0f);
        }
        if (!st->open) cui_pop_disabled(ui);
        cui__float_end(ui);
    }
    ui->last_id = id;
    ui->last_node = b;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Theme switcher                                                           */
/* ------------------------------------------------------------------------ */

bool cui_theme_switcher(cui_ctx *ui, int *mode, bool with_system) {
    static const char *items[3] = { "##theme-light", "##theme-dark", "##theme-system" };
    const char *icons[3] = { CUI_ICON_SUN, CUI_ICON_MOON, CUI_ICON_MONITOR };
    int m = *mode;
    if (m < 0 || m > (with_system ? 2 : 1)) m = cui_get_theme_mode(ui) == CUI_THEME_DARK ? 1 : 0;
    cui_segmented_opts so;
    memset(&so, 0, sizeof so);
    so.icons = icons; so.size = CUI_SIZE_SM;
    bool changed = cui_segmented_ex(ui, "##theme-switcher", items, with_system ? 3 : 2, &m, &so);
    if (changed) {
        *mode = m;
        if (m == 0) cui_set_theme(ui, CUI_THEME_LIGHT);
        if (m == 1) cui_set_theme(ui, CUI_THEME_DARK);
    } else {
        *mode = m;
    }
    return changed;
}

bool cui_theme_toggle(cui_ctx *ui) {
    bool dark = cui_get_theme_mode(ui) == CUI_THEME_DARK;
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.tooltip = dark ? "Switch to light" : "Switch to dark";
    cui_push_id(ui, "##theme-toggle");
    bool c = cui_icon_button_ex(ui, dark ? CUI_ICON_SUN : CUI_ICON_MOON, &bo);
    cui_pop_id(ui);
    if (c) cui_set_theme(ui, dark ? CUI_THEME_LIGHT : CUI_THEME_DARK);
    return c;
}

/* ------------------------------------------------------------------------ */
/* User menu                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { float hover; } cui__um_state;

bool cui_user_menu_begin(cui_ctx *ui, const char *name, const cui_user_menu_opts *o) {
    cui_user_menu_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, name) ^ 0x05E4u;
    cui_id pid = id ^ 0x3E40u;
    cui_popup *p = cui__popup_get(ui, pid, true);
    bool open = p && p->open;
    cui__um_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    bool key = ui->focus == id && ui->focus_visible && (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE));
    if ((r.clicked && !(open && key)) && p) { cui__popup_set(ui, pid, !p->open); p->at_mouse = false; }
    cui__tween(ui, &st->hover, (r.hovered || open) ? 1.0f : 0.0f, CUI__T_HOVER);
    int row = cui__node_new(ui, id);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER; rn->gap = 10;
    rn->pad[0] = rn->pad[2] = 6; rn->pad[1] = 8; rn->pad[3] = 6;
    rn->radius = CUI_CORNERS(CUI_MAX(6.0f, ui->theme.radius_control - 6));
    rn->bg = cui_alpha(CUI_C(ui, ACTIVE), st->hover);
    if (!o->compact) { rn->w = CUI_FILL; rn->self_align = CUI_ALIGN_STRETCH; }
    else rn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, row);
    cui_avatar_opts ao;
    memset(&ao, 0, sizeof ao);
    ao.size = o->compact ? 28 : 32; ao.status = o->status; ao.image = o->avatar;
    cui_avatar_ex(ui, name, &ao);
    if (!o->compact) {
        int col = cui__node_new(ui, 0);
        ui->nodes[col].w = CUI_FILL; ui->nodes[col].gap = 0;
        cui__open(ui, col);
        int t1 = cui__text_node(ui, name, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
        ui->nodes[t1].line_height = 1.35f;
        if (o->detail) {
            int t2 = cui__text_node(ui, o->detail, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[t2].line_height = 1.35f;
        }
        cui__close(ui);
        cui__icon_node(ui, CUI_ICON_CHEVRONS_UP_DOWN, 14, CUI_C(ui, TEXT_MUTED));
    }
    cui__close(ui);
    cui__focus_ring(ui, row, r.focus_visible ? 1.0f : 0.0f);
    int place = o->placement ? (int)o->placement : (o->compact ? CUI_PLACE_BOTTOM_END : CUI_PLACE_TOP_START);
    if (!cui__popup_begin(ui, pid, id, place, o->width > 0 ? o->width : 248, 6, 0, true)) return false;
    /* header: who is signed in */
    int head = cui__node_new(ui, 0);
    cui_node *hn = &ui->nodes[head];
    hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER; hn->gap = 10;
    hn->pad[0] = 6; hn->pad[1] = 8; hn->pad[2] = 8; hn->pad[3] = 8;
    cui__open(ui, head);
    ao.size = 36; ao.status = NULL;
    cui_avatar_ex(ui, name, &ao);
    int col = cui__node_new(ui, 0);
    ui->nodes[col].w = CUI_FILL;
    cui__open(ui, col);
    int t1 = cui__text_node(ui, name, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t1].line_height = 1.35f;
    if (o->detail) {
        int t2 = cui__text_node(ui, o->detail, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t2].line_height = 1.35f;
    }
    cui__close(ui);
    cui__close(ui);
    cui_menu_separator(ui);
    return true;
}

void cui_user_menu_end(cui_ctx *ui) { cui__popup_end(ui); }

/* ------------------------------------------------------------------------ */
/* Swipe actions                                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_spring x;
    float goal;              /* resting offset: 0, the left reveal or minus the right reveal */
    bool dragging, pending;
    float press_x, press_y, start_x;
    double last_wheel;
} cui__swipe_state;

struct cui_swipe_build {
    int result;
    struct cui_swipe_build *prev;
};

#define CUI__SWIPE_W 76.0f

/* the row's corners: concentric with the panel it sits in. The clip is a plain
 * rectangle, so the outermost action tile carries these corners itself. */
#define CUI__SWIPE_R(ui) CUI__R_ITEM(ui)

static void cui__swipe_actions(cui_ctx *ui, cui_id id, const cui_swipe_action *acts, int n, bool left, float reveal,
                               int *result, cui__swipe_state *st) {
    int side = cui__node_new(ui, 0);
    cui_node *sn = &ui->nodes[side];
    sn->dir = CUI_ROW; sn->h = CUI_FILL;
    sn->w = CUI_PX(CUI_MAX(0.0f, reveal));
    sn->flags |= CUI_NF_CLIP;
    sn->justify = left ? CUI_JUSTIFY_START : CUI_JUSTIFY_END;
    cui__open(ui, side);
    for (int k = 0; k < n; k++) {
        int key = left ? k : 100 + k;
        cui_id aid = cui__hash(&key, sizeof key, id);
        cui_interact r = cui__interact(ui, aid, 0);
        if (r.clicked) *result = key;
        cui_color tone = acts[k].tone ? cui__tone_color(ui, acts[k].tone) : CUI_C(ui, TEXT_MUTED);
        int b = cui__node_new(ui, aid);
        cui_node *bn = &ui->nodes[b];
        bn->w = CUI_PX(CUI__SWIPE_W); bn->h = CUI_FILL;
        bn->bg = r.hovered ? cui_mix(tone, cui__rgba(0, 0, 0, 255), 0.1f) : tone;
        float rr = CUI__SWIPE_R(ui);
        if (left && k == 0) bn->radius = (cui_corners){ rr, 0, 0, rr };
        if (!left && k == n - 1) bn->radius = (cui_corners){ 0, rr, rr, 0 };
        bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER; bn->gap = 3;
        cui__mark_interactive(ui, b, 0);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, b);
        /* label and icon at 4.5:1 on the tone: white in light mode, a deep shade of the same hue on dark mode's lighter tones */
        cui_color fg = ui->mode == CUI_THEME_DARK ? cui__legible(cui__shade(tone, 0.24f, 0.5f), bn->bg, 4.5f)
                                                  : cui__legible(cui__rgba(255, 255, 255, 255), bn->bg, 4.5f);
        if (acts[k].icon) cui__icon_node(ui, acts[k].icon, 18, fg);
        if (acts[k].label) {
            int t = cui__text_node(ui, acts[k].label, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), fg, false);
            ui->nodes[t].line_height = 1.2f;
        }
        cui__close(ui);
    }
    cui__close(ui);
}

void cui_swipe_item_begin(cui_ctx *ui, const char *id_str, const cui_swipe_action *left, int left_count,
                          const cui_swipe_action *right, int right_count) {
    struct cui_swipe_build *b = cui__frame_alloc(ui, sizeof *b);
    b->result = -1;
    b->prev = ui->swipe;
    ui->swipe = b;
    cui_id id = cui_make_id(ui, id_str);
    cui_id fid = id ^ 0x5F1Fu;
    cui__swipe_state *st = cui_state(ui, id, sizeof *st);
    cui_rect box = cui__prev_rect(ui, id);
    float lw = CUI__SWIPE_W * left_count, rw = CUI__SWIPE_W * right_count;
    float full = box.w > 0 ? box.w : 400;
    cui_interact r = cui__interact(ui, fid, CUI_IF_DRAG);
    bool settle = false;
    if (r.hovered && ui->mpressed[0]) { st->pending = true; st->press_x = ui->mx; st->press_y = ui->my; st->start_x = st->x.x; }
    if (st->pending && ui->mdown[0] && !st->dragging) {
        float dx = ui->mx - st->press_x, dy = ui->my - st->press_y;
        if (fabsf(dx) > 6 && fabsf(dx) > fabsf(dy)) st->dragging = true;
        else if (fabsf(dy) > 8) st->pending = false;
    }
    if (!ui->mdown[0]) st->pending = false;
    if (st->dragging) {
        if (ui->mdown[0]) {
            float x = st->start_x + (ui->mx - st->press_x);
            if (left_count == 0 && x > 0) x *= 0.25f;     /* rubber band where nothing waits */
            if (right_count == 0 && x < 0) x *= 0.25f;
            st->x.x = x; st->x.v = 0;
            ui->active = fid;
            ui->animating = true;
        } else {
            st->dragging = false;
            settle = true;
        }
    } else if (r.clicked && st->goal != 0) {
        st->goal = 0;   /* clicking an open row closes it */
    }
    /* trackpad: horizontal wheel moves the row and it settles when the swipe ends */
    if (r.hovered && ui->scroll_dx != 0 && (left_count || right_count)) {
        float x = st->x.x + ui->scroll_dx * 30;
        if (left_count == 0 && x > 0) x = 0;
        if (right_count == 0 && x < 0) x = 0;
        st->x.x = x; st->x.v = 0;
        st->last_wheel = ui->time;
        ui->scroll_dx = 0;
        cui__wakeup_after(ui, 0.16f);
    } else if (st->last_wheel > 0 && ui->time - st->last_wheel > 0.14) {
        st->last_wheel = 0;
        settle = true;
    }
    if (settle) {
        float x = st->x.x;
        if (x > 0 && left_count && x > full * 0.6f) { b->result = 0; st->goal = 0; }
        else if (x < 0 && right_count && -x > full * 0.6f) { b->result = 100; st->goal = 0; }
        else if (left_count && x > lw * 0.5f) st->goal = lw;
        else if (right_count && -x > rw * 0.5f) st->goal = -rw;
        else st->goal = 0;
    }
    /* a press anywhere else closes an open row */
    if (st->goal != 0 && (ui->mpressed[0] || ui->mpressed[1]) && !cui__rect_contains(box, ui->mx, ui->my)) st->goal = 0;
    if (!st->dragging && st->last_wheel == 0) cui__spring(ui, &st->x, st->goal, CUI_SPRING_SNAPPY);
    float x = st->x.x;
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->dir = CUI_STACK; on->w = CUI_FILL; on->self_align = CUI_ALIGN_STRETCH;
    on->flags |= CUI_NF_CLIP;
    on->radius = CUI_CORNERS(CUI__SWIPE_R(ui));
    cui__open(ui, outer);
    int under = cui__node_new(ui, 0);
    cui_node *un = &ui->nodes[under];
    un->dir = CUI_ROW; un->w = CUI_FILL; un->h = CUI_FILL; un->justify = CUI_JUSTIFY_BETWEEN;
    cui__open(ui, under);
    int res = -1;
    cui__swipe_actions(ui, id, left, left_count, true, x > 0 ? x : 0, &res, st);
    cui__swipe_actions(ui, id, right, right_count, false, x < 0 ? -x : 0, &res, st);
    cui__close(ui);
    if (res >= 0) { b->result = res; st->goal = 0; }
    int fg = cui__node_new(ui, fid);
    cui_node *fn = &ui->nodes[fg];
    fn->dir = CUI_ROW; fn->align = CUI_ALIGN_CENTER; fn->gap = 12;
    fn->w = CUI_FILL;
    fn->pad[0] = fn->pad[2] = 10; fn->pad[1] = fn->pad[3] = 12;
    fn->bg = CUI_C(ui, SURFACE);
    fn->radius = CUI_CORNERS(CUI__SWIPE_R(ui));
    fn->tx = x;
    cui__mark_interactive(ui, fg, CUI_IF_DRAG);
    if (st->dragging) cui__set_cursor(ui, CUI_CURSOR_MOVE);
    cui__open(ui, fg);
    cui_push_id(ui, id_str);
}

int cui_swipe_item_end(cui_ctx *ui) {
    struct cui_swipe_build *b = ui->swipe;
    if (!b) return -1;
    cui_pop_id(ui);
    cui__close(ui);   /* foreground row */
    cui__close(ui);   /* stack */
    ui->swipe = b->prev;
    return b->result;
}

/* ------------------------------------------------------------------------ */
/* Inline edit                                                              */
/* ------------------------------------------------------------------------ */

typedef struct { bool editing; char backup[512]; float hover; } cui__inline_state;

int cui_inline_edit_ex(cui_ctx *ui, const char *id_str, char *buf, size_t cap, const cui_inline_edit_opts *o) {
    cui_inline_edit_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id fid = id ^ 0x1D17u;
    cui__inline_state *st = cui_state(ui, id, sizeof *st);
    cui_font font = o->font ? o->font : CUI_FONT_MEDIUM;
    float size = o->size > 0 ? cui__fs(ui, o->size) : cui__fs(ui, ui->theme.text_body);
    float lh = size * (size > 18 ? 1.3f : 1.5f);
    int result = 0;
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->dir = CUI_ROW; on->align = CUI_ALIGN_CENTER; on->gap = 8;
    on->pad[0] = on->pad[2] = 2; on->pad[1] = 6; on->pad[3] = 6;
    on->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    on->tx = -7;   /* hang the padding and border outside, so the text lines up with the text around it */
    if (o->width.kind) on->w = o->width;
    else on->self_align = CUI__ALIGN_NOSTRETCH;
    if (!st->editing) {
        cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
        cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        bool start = r.clicked || (ui->focus == id && ui->focus_visible && cui_key_pressed(ui, CUI_KEY_ENTER));
        on->bg = cui_alpha(CUI_C(ui, HOVER), st->hover);
        on->border = 1;
        on->border_color = cui__rgba(0, 0, 0, 0);
        cui__mark_interactive(ui, outer, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_TEXT);
        cui__open(ui, outer);
        bool empty = !buf[0];
        int t = cui__text_node(ui, empty ? (o->placeholder ? o->placeholder : "Untitled") : buf, -1, font, size,
                               empty ? CUI_C(ui, TEXT_MUTED) : CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = lh / size;
        if (o->width.kind) ui->nodes[t].w = CUI_FILL;
        cui__icon_node(ui, CUI_ICON_PENCIL, CUI_MAX(12.0f, size * 0.62f), CUI_C(ui, TEXT_MUTED));
        ui->nodes[ui->node_count - 1].opacity = CUI_MAX(0.001f, st->hover);
        cui__close(ui);
        cui__focus_ring(ui, outer, r.focus_visible ? 1.0f : 0.0f);
        if (start) {
            st->editing = true;
            snprintf(st->backup, sizeof st->backup, "%s", buf);
            ui->focus = fid;
            ui->focus_visible = false;
            ui->tf_select_all = fid;
        }
        ui->last_id = id;
        return 0;
    }
    /* editing: the same type, now a field */
    bool cancel = ui->focus == fid && cui_key_pressed(ui, CUI_KEY_ESCAPE);
    if (cancel) {
        snprintf(buf, cap, "%s", st->backup);
        st->editing = false;
        ui->kpressed[CUI_KEY_ESCAPE] = 0;
        ui->focus = id;
    }
    on->border = 1;
    on->border_color = cui__edge_on(ui);
    on->bg = CUI_C(ui, SURFACE);
    cui__open(ui, outer);
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.placeholder = o->placeholder;
    ui->tf_override = true;
    ui->tf_font = font;
    ui->tf_size = size;
    int f = cui__text_field(ui, fid, buf, cap, &io, false, 1, NULL);
    int fn = cui__find_node(ui, fid);
    if (fn >= 0) {
        ui->nodes[fn].h = CUI_PX(lh);
        float tw = cui__text_width(ui, font, size, buf, -1, false);
        if (!o->width.kind) { ui->nodes[fn].w = CUI_PX(CUI_MAX(80.0f, tw + 24)); ui->nodes[fn].min_w = 0; }
    }
    cui__close(ui);
    if (!cancel && ((f & CUI_SUBMITTED) || (ui->focus != fid && st->editing))) {
        st->editing = false;
        if (strcmp(buf, st->backup) != 0) result = CUI_CHANGED;
        if (f & CUI_SUBMITTED) ui->focus = id;
    }
    ui->last_id = id;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Scrub field                                                              */
/* ------------------------------------------------------------------------ */

typedef struct { bool editing, moved; char text[48]; float hover, press_x, start; float fill; bool init; } cui__scrub_state;

typedef struct { float frac, hover; cui_color c, track; } cui__scrub_paint;
static void cui__paint_scrub(cui_ctx *ui, cui_node *n, void *data) {
    /* where the value sits in its range: a hairline along the bottom edge */
    cui__scrub_paint *p = data;
    cui_rect r = n->rect;
    float inset = CUI_MIN(n->radius.bl, r.h * 0.5f) * 0.9f + 2;
    float w = r.w - inset * 2;
    if (w <= 0) return;
    cui_rect line = { r.x + inset, r.y + r.h - 3.5f, w, 1.5f };
    cui_draw_rect(ui, line, CUI_CORNERS(0.75f), cui_alpha(p->track, 0.5f + 0.5f * p->hover));
    if (p->frac > 0) {
        line.w = w * p->frac;
        cui_draw_rect(ui, line, CUI_CORNERS(0.75f), cui_alpha(p->c, 0.55f + 0.45f * p->hover));
    }
}

bool cui_scrub_ex(cui_ctx *ui, const char *label, float *value, float min, float max, const cui_scrub_opts *o) {
    cui_scrub_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, label);
    cui_id fid = id ^ 0x5C2Bu;
    cui__scrub_state *st = cui_state(ui, id, sizeof *st);
    float step = o->step > 0 ? o->step : (max - min) / 100.0f;
    if (step <= 0) step = 1;
    const char *fmt = o->format ? o->format : "%.0f";
    float old = *value;
    float h = cui__control_height(ui, CUI_SIZE_MD);
    if (!st->editing) {
        cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
        if (r.hovered && ui->mpressed[0]) { st->press_x = ui->mx; st->start = *value; st->moved = false; }
        if (ui->active == id && ui->mdown[0]) {
            float dx = ui->mx - st->press_x;
            if (fabsf(dx) > 3) st->moved = true;
            if (st->moved) {
                float per_px = step / ((ui->mods & CUI_MOD_SHIFT) ? 10.0f : 1.0f);
                float v = st->start + dx * per_px;
                v = roundf(v / per_px) * per_px;
                *value = cui__clampf(v, min, max);
            }
        }
        if (r.clicked && !st->moved) {
            st->editing = true;
            snprintf(st->text, sizeof st->text, fmt, (double)*value);
            ui->focus = fid;
            ui->focus_visible = false;
            ui->tf_select_all = fid;
        }
        if (ui->focus == id) {
            float k = (ui->mods & CUI_MOD_SHIFT) ? 10.0f : 1.0f;
            if (cui_key_pressed(ui, CUI_KEY_RIGHT) || cui_key_pressed(ui, CUI_KEY_UP)) *value = cui__clampf(*value + step * k, min, max);
            if (cui_key_pressed(ui, CUI_KEY_LEFT) || cui_key_pressed(ui, CUI_KEY_DOWN)) *value = cui__clampf(*value - step * k, min, max);
            if (cui_key_pressed(ui, CUI_KEY_ENTER) && ui->focus_visible) {
                st->editing = true;
                snprintf(st->text, sizeof st->text, fmt, (double)*value);
                ui->focus = fid;
                ui->tf_select_all = fid;
            }
        }
        cui__tween(ui, &st->hover, (r.hovered || ui->active == id) ? 1.0f : 0.0f, CUI__T_HOVER);
        float frac = max > min ? (*value - min) / (max - min) : 0;
        if (!st->init) { st->fill = frac; st->init = true; }
        cui__tween(ui, &st->fill, frac, 0.08f);
        int box = cui__node_new(ui, id);
        cui_node *bn = &ui->nodes[box];
        bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER; bn->gap = 8;
        bn->h = CUI_PX(h); cui__soft_width(bn, o->width, 160);
        bn->pad[1] = 12; bn->pad[3] = 12;
        bn->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, h * 0.5f));
        bn->bg = CUI_C(ui, SURFACE);
        bn->border = 1;
        bn->border_color = cui__control_edge(ui, st->hover);
        cui__scrub_paint *sp = cui__frame_alloc(ui, sizeof *sp);
        sp->frac = cui__clampf(st->fill, 0, 1);
        sp->hover = st->hover;
        sp->c = cui__edge_on(ui);
        sp->track = CUI_C(ui, BORDER);
        bn->paint = cui__paint_scrub;
        bn->paint_data = sp;
        cui__mark_interactive(ui, box, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
        if (r.hovered || ui->active == id) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
        cui__open(ui, box);
        if (o->icon) cui__icon_node(ui, o->icon, 16, CUI_C(ui, TEXT_MUTED));
        else {
            int lt = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[lt].line_height = 1.3f;
        }
        cui_spacer(ui);
        const char *vs = cui__frame_printf(ui, fmt, (double)*value);
        int vt = cui__text_node(ui, vs, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
        ui->nodes[vt].flags |= CUI_NF_TABULAR;
        ui->nodes[vt].line_height = 1.3f;
        if (o->suffix) {
            int su = cui__text_node(ui, o->suffix, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[su].line_height = 1.3f;
            ui->nodes[box].gap = 4;
        }
        cui__close(ui);
        cui__focus_ring(ui, box, r.focus_visible ? 1.0f : 0.0f);
        ui->last_id = id;
        return *value != old;
    }
    /* typing a value */
    bool cancel = ui->focus == fid && cui_key_pressed(ui, CUI_KEY_ESCAPE);
    if (cancel) { st->editing = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; ui->focus = id; }
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.width = o->width.kind ? o->width : CUI_PX(160);
    io.suffix = o->suffix;
    io.prefix = o->icon ? NULL : cui__frame_strdup(ui, label, cui__display_len(label));
    int f = cui__text_field(ui, fid, st->text, sizeof st->text, &io, false, 0, NULL);
    if (!cancel && ((f & CUI_SUBMITTED) || ui->focus != fid)) {
        char *end = NULL;
        double v = strtod(st->text, &end);
        if (end != st->text) *value = cui__clampf((float)v, min, max);
        st->editing = false;
        if (f & CUI_SUBMITTED) ui->focus = id;
    }
    ui->last_id = id;
    return *value != old;
}

/* ------------------------------------------------------------------------ */
/* Money field                                                              */
/* ------------------------------------------------------------------------ */

typedef struct { char text[64]; bool had_focus; double last; bool init; } cui__money_state;

static void cui__format_money(char *out, size_t cap, double v, int decimals) {
    char raw[64];
    bool neg = v < 0;
    snprintf(raw, sizeof raw, "%.*f", decimals, neg ? -v : v);
    char *dot = strchr(raw, '.');
    int int_len = dot ? (int)(dot - raw) : (int)strlen(raw);
    size_t o = 0;
    if (neg && o + 1 < cap) out[o++] = '-';
    for (int i = 0; i < int_len && o + 1 < cap; i++) {
        out[o++] = raw[i];
        int left = int_len - i - 1;
        if (left > 0 && left % 3 == 0 && o + 1 < cap) out[o++] = ',';
    }
    if (dot) for (const char *p = dot; *p && o + 1 < cap; p++) out[o++] = *p;
    out[o] = 0;
}

bool cui_money_input_ex(cui_ctx *ui, const char *id_str, double *value, const cui_money_opts *o) {
    cui_money_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int dec = o->decimals > 0 ? o->decimals : (o->decimals < 0 ? 0 : 2);
    cui_id id = cui_make_id(ui, id_str);
    cui__money_state *st = cui_state(ui, id ^ 0x3077u, sizeof *st);
    double old = *value;
    bool focused = ui->focus == id;
    if (focused && !st->had_focus) {
        snprintf(st->text, sizeof st->text, "%.*f", dec, *value);   /* plain digits while editing */
        if (*value == 0) st->text[0] = 0;
    }
    if (!focused && (!st->init || st->had_focus || st->last != *value)) {
        if (st->had_focus) {
            char *end = NULL;
            double v = strtod(st->text, &end);
            if (end != st->text) { double m = pow(10, dec); *value = round(v * m) / m; }
            else if (!st->text[0]) *value = 0;
        }
        cui__format_money(st->text, sizeof st->text, *value, dec);
        st->init = true;
    }
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.label = o->label; io.hint = o->hint; io.width = o->width;
    io.prefix = o->symbol ? o->symbol : "$";
    io.suffix = o->code;
    io.placeholder = dec ? "0.00" : "0";
    int f = cui__text_field(ui, id, st->text, sizeof st->text, &io, false, 0, NULL);
    if ((f & CUI_CHANGED) && ui->focus == id) {
        /* keep digits and one decimal point */
        char clean[64];
        size_t n = 0;
        bool dot = false;
        int after = 0;
        for (const char *p = st->text; *p && n + 1 < sizeof clean; p++) {
            if (*p >= '0' && *p <= '9') { if (dot && after >= dec) continue; clean[n++] = *p; if (dot) after++; }
            else if ((*p == '.' || *p == ',') && !dot && dec > 0) { dot = true; clean[n++] = '.'; }
        }
        clean[n] = 0;
        if (strcmp(clean, st->text) != 0) snprintf(st->text, sizeof st->text, "%s", clean);
        char *end = NULL;
        double v = strtod(st->text, &end);
        *value = end != st->text ? v : 0;
    }
    st->had_focus = ui->focus == id;
    st->last = *value;
    return *value != old;
}

/* ------------------------------------------------------------------------ */
/* Shortcut recorder                                                        */
/* ------------------------------------------------------------------------ */

const char *cui_key_name(cui_key k) {
    static const char *names[CUI_KEY_COUNT] = {
        "", "Tab", "Enter", "Esc", "Backspace", "Delete", "Space", "Left", "Right", "Up", "Down",
        "Home", "End", "PgUp", "PgDn",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "-", "=", ",", ".", "/",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    };
    if (k <= 0 || k >= CUI_KEY_COUNT) return "";
    return names[k] ? names[k] : "";
}

const char *cui_shortcut_label(cui_ctx *ui, cui_key_combo c) {
    if (!c.key) return "";
#if defined(__APPLE__)
    return cui__frame_printf(ui, "%s%s%s%s%s", (c.mods & CUI_MOD_CTRL) ? "Ctrl " : "", (c.mods & CUI_MOD_ALT) ? "Opt " : "",
                             (c.mods & CUI_MOD_SHIFT) ? "Shift " : "", (c.mods & CUI_MOD_SUPER) ? "Cmd " : "", cui_key_name(c.key));
#else
    return cui__frame_printf(ui, "%s%s%s%s%s", (c.mods & CUI_MOD_CTRL) ? "Ctrl " : "", (c.mods & CUI_MOD_SUPER) ? "Win " : "",
                             (c.mods & CUI_MOD_ALT) ? "Alt " : "", (c.mods & CUI_MOD_SHIFT) ? "Shift " : "", cui_key_name(c.key));
#endif
}

typedef struct { bool recording; float hover, rec; } cui__rec_state;

typedef struct { float t; double phase; cui_color c; } cui__rec_dot;
static void cui__paint_rec_dot(cui_ctx *ui, cui_node *n, void *data) {
    cui__rec_dot *p = data;
    cui_vec2 c = { n->rect.x + n->rect.w * 0.5f, n->rect.y + n->rect.h * 0.5f };
    float pulse = 0.5f + 0.5f * sinf((float)p->phase * 5.0f);
    cui_draw_circle(ui, c, 3.5f + 3.0f * pulse, cui_alpha(p->c, 0.22f * p->t));
    cui_draw_circle(ui, c, 3.5f, cui_alpha(p->c, p->t));
}

bool cui_shortcut_recorder_ex(cui_ctx *ui, const char *id_str, cui_key_combo *combo, const cui_shortcut_recorder_opts *o) {
    cui_shortcut_recorder_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__rec_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_TEXT);
    bool changed = false;
    if (r.clicked && !st->recording) { st->recording = true; ui->focus = id; }
    else if (!st->recording && ui->focus == id && ui->focus_visible && (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE))) {
        st->recording = true;
        ui->kpressed[CUI_KEY_ENTER] = ui->kpressed[CUI_KEY_SPACE] = 0;
    }
    if (st->recording) {
        if (ui->focus != id || ((ui->mpressed[0] || ui->mpressed[1]) && ui->hot != id)) st->recording = false;
        else {
            ui->edit = (struct cui_edit_state *)(void *)st;   /* keep app shortcuts quiet while recording */
            for (int k = 1; k < CUI_KEY_COUNT; k++) {
                if (!ui->kpressed[k]) continue;
                ui->kpressed[k] = 0;
                if (k == CUI_KEY_ESCAPE && !ui->mods) { st->recording = false; break; }
                if ((k == CUI_KEY_BACKSPACE || k == CUI_KEY_DELETE) && !ui->mods) {
                    changed = combo->key != 0;
                    combo->key = CUI_KEY_NONE; combo->mods = 0;
                    st->recording = false;
                    break;
                }
                if (k == CUI_KEY_TAB && !ui->mods) { st->recording = false; break; }
                changed = combo->key != (cui_key)k || combo->mods != ui->mods;
                combo->key = (cui_key)k;
                combo->mods = ui->mods;
                st->recording = false;
                break;
            }
            ui->text_in_len = 0;
            cui__wakeup_after(ui, 0.05f);
            ui->animating = true;
        }
    }
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->rec, st->recording ? 1.0f : 0.0f, CUI__T_STATE);
    float h = cui__control_height(ui, CUI_SIZE_MD);
    cui_color accent = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
    int box = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[box];
    bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER; bn->gap = 8;
    bn->h = CUI_PX(h); cui__soft_width(bn, o->width, 220);
    bn->pad[1] = 8; bn->pad[3] = 12;
    bn->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, h * 0.5f));
    bn->bg = CUI_C(ui, SURFACE);
    bn->border = 1;
    bn->border_color = cui_mix(cui__control_edge(ui, st->hover), accent, st->rec);
    cui__mark_interactive(ui, box, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, box);
    if (st->recording) {
        int dot = cui__leaf(ui, 0, CUI_PX(14), CUI_PX(14));
        cui__rec_dot *p = cui__frame_alloc(ui, sizeof *p);
        p->t = st->rec; p->phase = ui->time; p->c = CUI_C(ui, DANGER);
        ui->nodes[dot].paint = cui__paint_rec_dot;
        ui->nodes[dot].paint_data = p;
        int lt = cui__text_node(ui, ui->mods ? cui__frame_printf(ui, "%s\xe2\x80\xa6", cui_shortcut_label(ui, (cui_key_combo){ ui->mods, CUI_KEY_NONE })) : "Press a shortcut",
                                -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        if (ui->mods) {
            const char *m = cui__frame_printf(ui, "%s%s%s%s", (ui->mods & CUI_MOD_CTRL) ? "Ctrl " : "", (ui->mods & CUI_MOD_SUPER) ? "Super " : "",
                                              (ui->mods & CUI_MOD_ALT) ? "Alt " : "", (ui->mods & CUI_MOD_SHIFT) ? "Shift " : "");
            ui->nodes[lt].text = m; ui->nodes[lt].text_len = (int)strlen(m);
        }
        ui->nodes[lt].line_height = 1.3f;
        ui->nodes[lt].w = CUI_FILL;
    } else if (combo->key) {
        cui_kbd(ui, cui_shortcut_label(ui, *combo));
        cui_spacer(ui);
        cui_button_opts xo;
        memset(&xo, 0, sizeof xo);
        xo.size = CUI_SIZE_SM;
        xo.tooltip = "Clear";
        cui_push_id(ui, id_str);
        if (cui_icon_button_ex(ui, CUI_ICON_X, &xo)) { combo->key = CUI_KEY_NONE; combo->mods = 0; changed = true; }
        cui_pop_id(ui);
        ui->nodes[ui->last_node].w = CUI_PX(24); ui->nodes[ui->last_node].h = CUI_PX(24);
    } else {
        int lt = cui__text_node(ui, o->placeholder ? o->placeholder : "Click to record", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm),
                                CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[lt].line_height = 1.3f;
        ui->nodes[lt].w = CUI_FILL;
        cui__icon_node(ui, CUI_ICON_KEYBOARD, 16, CUI_C(ui, TEXT_MUTED));
    }
    cui__close(ui);
    cui__focus_ring(ui, box, (r.focus_visible && !st->recording) ? 1.0f : 0.0f);
    ui->last_id = id;
    return changed;
}
