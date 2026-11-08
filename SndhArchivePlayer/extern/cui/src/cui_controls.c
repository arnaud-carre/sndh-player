/* Selection controls and navigation: checkbox, switch, radio, elastic slider,
 * range slider, rating, chips, segmented control, tabs, sidebar navigation,
 * breadcrumb, pagination and stepper. */
#include "cui_internal.h"

static float cui__fs(cui_ctx *ui, float size) { return size * ui->theme.font_scale; }

typedef struct {
    float hover, focus;
    cui_spring on;      /* 0..1 checked */
    cui_spring press;   /* 0..1 pressed */
    bool init;
} cui__toggle_state;

typedef struct {
    float t, v, hover, press, focus;   /* v: the spring's speed, in widths per second */
    bool indeterminate;
    int kind;           /* 0 checkbox, 1 switch, 2 radio */
} cui__toggle_paint;

static void cui__paint_toggle(cui_ctx *ui, cui_node *n, void *data) {
    cui__toggle_paint *p = data;
    cui_rect r = n->rect;
    cui_color on = CUI_C(ui, CONTROL_ON);
    cui_color glyph = CUI_C(ui, CONTROL_GLYPH);
    float t = cui__clampf(p->t, 0, 1);
    /* a press dips the box, and the toggle's small overshoot swells it: it answers the click like a key */
    if (p->kind != 1) {
        float k = (1.0f - 0.06f * p->press) * (1.0f + 0.35f * CUI_MAX(0.0f, p->t - 1.0f));
        float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
        r = (cui_rect){ cx - r.w * 0.5f * k, cy - r.h * 0.5f * k, r.w * k, r.h * k };
    }
    if (p->kind == 0) {
        cui_corners rad = CUI_CORNERS(5);
        cui_color border = cui__control_edge(ui, p->hover);
        cui_color bg = cui_mix(CUI_C(ui, SURFACE), on, t);
        cui_draw_rect_border(ui, r, rad, bg, cui_mix(border, on, t), 1);
        cui__draw_sheen(ui, r, rad, 0.55f * t);
        if (p->indeterminate) {
            cui_rect dash = { r.x + 4.5f, r.y + r.h * 0.5f - 0.9f, r.w - 9, 1.8f };
            cui_draw_rect(ui, dash, CUI_CORNERS(0.9f), cui_alpha(glyph, t));
        } else {
            cui__draw_check(ui, r, t, 1.9f, glyph);
        }
        cui__draw_focus_ring(ui, r, rad, p->focus);
    } else if (p->kind == 1) {
        float h = r.h, w = r.w;
        cui_color track = cui_mix(CUI_C(ui, CONTROL_TRACK), CUI_C(ui, CONTROL_TRACK_HOVER), p->hover);
        cui_draw_rect(ui, r, CUI_CORNERS(h * 0.5f), cui_mix(track, on, t));
        cui__draw_sheen(ui, r, CUI_CORNERS(h * 0.5f), 0.5f * t);
        float pad = 2, d = h - pad * 2;
        float stretch = p->press * 4;
        float travel = w - pad * 2 - d - stretch;
        float x = r.x + pad + travel * t;
        /* in flight the thumb stretches with its speed and thins to keep its volume, then rounds out as it lands */
        float speed = fabsf(p->v) * travel;
        float sx = 1.0f + CUI_MIN(0.28f, speed / 900.0f), sy = 1.0f / sx;
        float tw = (d + stretch) * sx, tht = d * sy;
        float grow = tw - (d + stretch);   /* anchored at the end it leaves from, so it never pokes out of the track */
        x = cui__clampf(x - grow * t, r.x + pad, r.x + w - pad - tw);
        cui_rect th = { x, r.y + pad + (d - tht) * 0.5f, tw, tht };
        cui_vec2 o1 = { 0, 1 }, o2 = { 0, 2 };
        cui_draw_shadow(ui, th, CUI_CORNERS(d * 0.5f), 2, 0, o1, cui__rgba(0, 0, 0, ui->mode == CUI_THEME_DARK ? 90 : 36));
        cui_draw_shadow(ui, th, CUI_CORNERS(d * 0.5f), 6, 0, o2, cui__rgba(0, 0, 0, ui->mode == CUI_THEME_DARK ? 50 : 16));
        cui_color thumb = cui_mix(CUI_C(ui, CONTROL_THUMB), CUI_C(ui, CONTROL_THUMB_ON), t);
        cui_draw_rect_border(ui, th, CUI_CORNERS(d * 0.5f), thumb, cui__rgba(0, 0, 0, 18), 0.5f);
        cui__draw_focus_ring(ui, r, CUI_CORNERS(h * 0.5f), p->focus);
    } else {
        float rr = r.w * 0.5f;
        cui_color border = cui__control_edge(ui, p->hover);
        cui_draw_rect_border(ui, r, CUI_CORNERS(rr), cui_mix(CUI_C(ui, SURFACE), on, t), cui_mix(border, on, t), 1);
        cui__draw_sheen(ui, r, CUI_CORNERS(rr), 0.55f * t);
        float dot = 7.0f * CUI_MAX(0.0f, p->t);   /* the dot pops a touch past its size, then settles */
        if (dot > 0.2f) {
            cui_vec2 c = { r.x + rr, r.y + rr };
            cui_draw_circle(ui, c, dot * 0.5f, glyph);
        }
        cui__draw_focus_ring(ui, r, CUI_CORNERS(rr), p->focus);
    }
}

/* Builds the row: [control][label + description]. Returns interaction. */
static cui_interact cui__toggle_row(cui_ctx *ui, cui_id id, int kind, bool checked, const char *label,
                                    const cui_check_opts *o, bool *out_disabled) {
    bool disabled = (o && o->disabled) || cui_is_disabled(ui);
    *out_disabled = disabled;
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_pop_disabled(ui);
    cui__toggle_state *st = cui_state(ui, id, sizeof *st);
    float target = checked ? 1.0f : 0.0f;
    if (!st->init) { st->on.x = target; st->init = true; }
    if (r.clicked) target = 1.0f - target;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    if (kind == 1) cui__spring(ui, &st->on, target, CUI_SPRING_SMOOTH);
    else if (r.keyboard_activate) cui__spring(ui, &st->on, target, 0.2f, 0.0f);   /* keys toggle plainly */
    else cui__spring(ui, &st->on, target, 0.26f, target > 0.5f ? 0.22f : 0.0f);   /* checking overshoots a little; clearing does not */
    cui__spring(ui, &st->press, r.pressed ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);

    bool has_desc = o && o->description;
    int row = cui__node_new(ui, id);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW;
    rn->gap = kind == 1 ? 12 : 10;
    rn->align = has_desc ? CUI_ALIGN_START : CUI_ALIGN_CENTER;
    rn->self_align = CUI__ALIGN_NOSTRETCH;
    rn->opacity = disabled ? CUI__DISABLED : 1.0f;
    cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, row);
    float w = kind == 1 ? 36 : 18, h = kind == 1 ? 20 : 18;
    int ctl = cui__leaf(ui, 0, CUI_PX(w), CUI_PX(h));
    cui_node *cn = &ui->nodes[ctl];
    if (has_desc) cn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__toggle_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->t = st->on.x; p->v = st->on.v; p->hover = st->hover; p->press = st->press.x; p->focus = st->focus; p->kind = kind;
    p->indeterminate = o && o->indeterminate;
    cn->paint = cui__paint_toggle;
    cn->paint_data = p;
    if (has_desc) { cn->fy = 0; }
    if (label && cui__display_len(label) > 0) {
        int col = cui__node_new(ui, 0);
        cui_node *col_n = &ui->nodes[col];
        col_n->gap = 2;
        col_n->w = CUI_FIT;
        cui__open(ui, col);
        int lt = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), true);
        ui->nodes[lt].line_height = has_desc ? 1.38f : 1.3f;
        if (has_desc) {
            int dt = cui__text_node(ui, o->description, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), true);
            ui->nodes[dt].line_height = 1.4f;
        }
        cui__close(ui);
    }
    cui__close(ui);
    ui->last_id = id;
    return r;
}

bool cui_checkbox_ex(cui_ctx *ui, const char *label, bool *value, const cui_check_opts *o) {
    cui_id id = cui_make_id(ui, label) ^ 0xC0EC0001u;
    bool dis;
    cui_interact r = cui__toggle_row(ui, id, 0, *value, label, o, &dis);
    if (r.clicked && !dis) { *value = !*value; return true; }
    return false;
}

bool cui_switch_ex(cui_ctx *ui, const char *label, bool *value, const cui_check_opts *o) {
    cui_id id = cui_make_id(ui, label) ^ 0x5D17C002u;
    bool dis;
    cui_interact r = cui__toggle_row(ui, id, 1, *value, label, o, &dis);
    if (r.clicked && !dis) { *value = !*value; return true; }
    return false;
}

bool cui_radio_ex(cui_ctx *ui, const char *label, int *value, int option, const cui_check_opts *o) {
    cui_id id = cui_make_id(ui, label) ^ 0x4AD10003u;
    id = cui__hash(&option, sizeof option, id);
    bool dis;
    cui_interact r = cui__toggle_row(ui, id, 2, *value == option, label, o, &dis);
    if (r.clicked && !dis && *value != option) { *value = option; return true; }
    return false;
}

bool cui_radio_group(cui_ctx *ui, const char *id, const char *const *items, int count, int *index) {
    bool changed = false;
    cui_push_id(ui, id);
    int col = cui__node_new(ui, 0);
    ui->nodes[col].gap = 10;
    cui__open(ui, col);
    for (int k = 0; k < count; k++) {
        cui_check_opts o;
        memset(&o, 0, sizeof o);
        if (cui_radio_ex(ui, items[k], index, k, &o)) changed = true;
    }
    cui__close(ui);
    cui_pop_id(ui);
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Elastic slider                                                           */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_spring thumb;   /* 0..1 position, used when jumping to a click */
    cui_spring over;    /* rubber band stretch in points (signed) */
    float hover, focus, active;
    bool dragging;
    bool init;
} cui__slider_state;

typedef struct { float t, over, hover, active, focus; bool disabled; float lo_t; bool range; } cui__slider_paint;

static float cui__rubber(float d) {
    /* stretch that grows fast at first then saturates, like rubber */
    float s = d < 0 ? -1.0f : 1.0f;
    float a = fabsf(d);
    return s * 22.0f * (1.0f - expf(-a / 70.0f));
}

static void cui__paint_slider(cui_ctx *ui, cui_node *n, void *data) {
    cui__slider_paint *p = data;
    cui_rect r = n->rect;
    float thumb = 18;
    float pad = thumb * 0.5f;
    float x0 = r.x + pad, x1 = r.x + r.w - pad;
    float over = p->over;
    /* stretch: extend the track on the side being pulled and thin it a bit */
    float stretch = fabsf(over);
    float th = 4.0f - CUI_MIN(1.2f, stretch * 0.05f);
    float tx0 = x0 - pad + (over < 0 ? over : 0);
    float tx1 = x1 + pad + (over > 0 ? over : 0);
    float cy = r.y + r.h * 0.5f;
    cui_rect track = { tx0, cy - th * 0.5f, tx1 - tx0, th };
    cui_draw_rect(ui, track, CUI_CORNERS(th * 0.5f), cui_mix(CUI_C(ui, CONTROL_TRACK), CUI_C(ui, CONTROL_TRACK_HOVER), p->hover));
    float tpos = x0 + (x1 - x0) * cui__clampf(p->t, 0, 1);
    if (p->t >= 1.0f && over > 0) tpos += over;
    if (p->t <= 0.0f && over < 0) tpos += over;
    float fstart = tx0;
    if (p->range) {
        fstart = x0 + (x1 - x0) * cui__clampf(p->lo_t, 0, 1);
        if (p->lo_t <= 0.0f && over < 0) fstart += over;
    }
    cui_rect fill = { fstart, track.y, CUI_MAX(0, tpos - fstart), th };
    cui_draw_rect(ui, fill, CUI_CORNERS(th * 0.5f), CUI_C(ui, CONTROL_ON));
    float positions[2] = { tpos, fstart };
    int thumbs = p->range ? 2 : 1;
    for (int k = 0; k < thumbs; k++) {
        float ts = thumb * (1.0f + 0.08f * p->hover - 0.06f * p->active);
        cui_rect tr = { positions[k] - ts * 0.5f, cy - ts * 0.5f, ts, ts };
        cui_vec2 o1 = { 0, 1 }, o2 = { 0, 2 };
        cui_draw_shadow(ui, tr, CUI_CORNERS(ts * 0.5f), 2, 0, o1, cui__rgba(0, 0, 0, ui->mode == CUI_THEME_DARK ? 90 : 38));
        cui_draw_shadow(ui, tr, CUI_CORNERS(ts * 0.5f), 6, 0, o2, cui__rgba(0, 0, 0, ui->mode == CUI_THEME_DARK ? 50 : 16));
        cui_draw_rect_border(ui, tr, CUI_CORNERS(ts * 0.5f), CUI_C(ui, CONTROL_THUMB), cui__rgba(0, 0, 0, 22), 0.5f);
        if (k == 0) cui__draw_focus_ring(ui, tr, CUI_CORNERS(ts * 0.5f), p->focus);
    }
}

static void cui__slider_header(cui_ctx *ui, const char *label, const char *value_text) {
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW;
    cui__open(ui, row);
    if (label && cui__display_len(label)) cui__label_text(ui, label, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND));
    cui_spacer(ui);
    if (value_text) {
        int t = cui__text_node(ui, value_text, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t].flags |= CUI_NF_TABULAR;
        ui->nodes[t].line_height = 1.3f;
    }
    cui__close(ui);
}

bool cui_slider_ex(cui_ctx *ui, const char *label, float *value, float min, float max, const cui_slider_opts *o) {
    cui_slider_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (max <= min) max = min + 1;
    cui_id id = cui_make_id(ui, label ? label : "slider");
    cui_id track_id = id ^ 0x51u;
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, track_id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui_pop_disabled(ui);
    cui__slider_state *st = cui_state(ui, id, sizeof *st);
    float old = *value;
    float t = (*value - min) / (max - min);
    if (!st->init) { st->thumb.x = t; st->init = true; }
    cui_rect pr = cui__prev_rect(ui, track_id);
    float pad = 9;
    float x0 = pr.x + pad, x1 = pr.x + pr.w - pad;
    bool jumped = false;
    if (r.active && ui->mdown[0] && pr.w > 0) {
        float mt = (ui->mx - x0) / (x1 - x0);
        if (ui->mpressed[0]) {
            st->dragging = true;
            jumped = fabsf(mt - t) > 0.02f;
        }
        float over = 0;
        if (ui->mx < x0) over = cui__rubber(ui->mx - x0);
        else if (ui->mx > x1) over = cui__rubber(ui->mx - x1);
        st->over.x = over; st->over.v = 0;
        t = cui__clampf(mt, 0, 1);
    } else {
        st->dragging = false;
    }
    /* keyboard */
    if (r.focused) {
        float step = o->step > 0 ? o->step / (max - min) : 0.01f;
        if (cui_key_pressed(ui, CUI_KEY_LEFT) || cui_key_pressed(ui, CUI_KEY_DOWN)) t -= step * ((ui->mods & CUI_MOD_SHIFT) ? 10 : 1);
        if (cui_key_pressed(ui, CUI_KEY_RIGHT) || cui_key_pressed(ui, CUI_KEY_UP)) t += step * ((ui->mods & CUI_MOD_SHIFT) ? 10 : 1);
        if (cui_key_pressed(ui, CUI_KEY_HOME)) t = 0;
        if (cui_key_pressed(ui, CUI_KEY_END)) t = 1;
        t = cui__clampf(t, 0, 1);
    }
    float v = min + t * (max - min);
    if (o->step > 0) v = min + roundf((v - min) / o->step) * o->step;
    v = cui__clampf(v, min, max);
    *value = v;
    t = (v - min) / (max - min);
    if (!st->dragging) cui__spring(ui, &st->over, 0, 0.5f, 0.35f);
    if (jumped || !st->dragging) cui__spring(ui, &st->thumb, t, 0.3f, 0.0f);
    else { st->thumb.x = t; st->thumb.v = 0; }
    cui__tween(ui, &st->hover, (r.hovered || r.active) ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->active, r.active ? 1.0f : 0.0f, CUI__T_STATE);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    if (r.hovered || r.active) cui__set_cursor(ui, CUI_CURSOR_HAND);

    int col = cui__node_new(ui, id);
    cui_node *cn = &ui->nodes[col];
    cn->gap = 6;
    cn->w = o->width.kind ? o->width : CUI_FILL;
    cn->opacity = disabled ? CUI__DISABLED : 1.0f;
    cui__open(ui, col);
    const char *fmt = o->format ? o->format : "%.0f";
    if ((label && cui__display_len(label)) || o->show_value)
        cui__slider_header(ui, label, o->show_value ? cui__frame_printf(ui, fmt, (double)v) : NULL);
    int tr = cui__leaf(ui, track_id, CUI_FILL, CUI_PX(22));
    ui->nodes[tr].self_align = CUI_ALIGN_STRETCH;
    cui__mark_interactive(ui, tr, CUI_IF_FOCUSABLE);
    cui__slider_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->t = st->thumb.x; p->over = st->over.x; p->hover = st->hover; p->active = st->active; p->focus = st->focus;
    ui->nodes[tr].paint = cui__paint_slider;
    ui->nodes[tr].paint_data = p;
    cui__close(ui);
    ui->last_id = track_id;
    return *value != old;
}

typedef struct { int grabbed; cui_spring a, b; float hover; bool init; } cui__range_state;

bool cui_range_slider(cui_ctx *ui, const char *label, float *lo, float *hi, float min, float max) {
    if (max <= min) max = min + 1;
    cui_id id = cui_make_id(ui, label ? label : "range");
    cui_id track_id = id ^ 0x52u;
    cui_interact r = cui__interact(ui, track_id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui__range_state *st = cui_state(ui, id, sizeof *st);
    float olo = *lo, ohi = *hi;
    float ta = (*lo - min) / (max - min), tb = (*hi - min) / (max - min);
    if (!st->init) { st->a.x = ta; st->b.x = tb; st->init = true; }
    cui_rect pr = cui__prev_rect(ui, track_id);
    float x0 = pr.x + 9, x1 = pr.x + pr.w - 9;
    if (r.active && ui->mdown[0] && pr.w > 0) {
        float mt = cui__clampf((ui->mx - x0) / (x1 - x0), 0, 1);
        if (ui->mpressed[0]) st->grabbed = fabsf(mt - ta) < fabsf(mt - tb) ? 0 : 1;
        if (st->grabbed == 0) ta = CUI_MIN(mt, tb); else tb = CUI_MAX(mt, ta);
    }
    *lo = min + ta * (max - min);
    *hi = min + tb * (max - min);
    cui__spring(ui, &st->a, ta, 0.22f, 0);
    cui__spring(ui, &st->b, tb, 0.22f, 0);
    cui__tween(ui, &st->hover, (r.hovered || r.active) ? 1.0f : 0.0f, CUI__T_HOVER);
    int col = cui__node_new(ui, id);
    ui->nodes[col].gap = 6;
    ui->nodes[col].w = CUI_FILL;
    cui__open(ui, col);
    if (ui->slider_no_header) ui->slider_no_header = false;
    else cui__slider_header(ui, label, cui__frame_printf(ui, "%.0f \xe2\x80\x93 %.0f", (double)*lo, (double)*hi));
    int tr = cui__leaf(ui, track_id, CUI_FILL, CUI_PX(22));
    ui->nodes[tr].self_align = CUI_ALIGN_STRETCH;
    cui__mark_interactive(ui, tr, CUI_IF_FOCUSABLE);
    cui__slider_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->t = st->b.x; p->lo_t = st->a.x; p->range = true; p->hover = st->hover;
    ui->nodes[tr].paint = cui__paint_slider;
    ui->nodes[tr].paint_data = p;
    cui__close(ui);
    ui->last_id = track_id;
    return *lo != olo || *hi != ohi;
}

/* ------------------------------------------------------------------------ */
/* Rating                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct { float fill; float hover; float scale; } cui__star_paint;

static void cui__paint_star(cui_ctx *ui, cui_node *n, void *data) {
    cui__star_paint *p = data;
    cui_rect r = n->rect;
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f + 0.5f;
    float R = r.w * 0.47f * p->scale, rr = R * 0.45f;
    cui_vec2 pts[10];
    for (int k = 0; k < 10; k++) {
        float a = -CUI_PI / 2 + k * CUI_PI / 5;
        float rad = (k & 1) ? rr : R;
        pts[k].x = cx + cosf(a) * rad;
        pts[k].y = cy + sinf(a) * rad;
    }
    cui_color empty = CUI_C(ui, CONTROL_TRACK);
    cui_color gold = cui_oklch(0.80f, 0.16f, 80, 1);
    cui_color c = p->fill > 0.5f ? gold : cui_mix(empty, cui_alpha(gold, 0.7f), p->hover);
    cui_cvert v[11];
    uint32_t idx[30];
    v[0].x = cx; v[0].y = cy; v[0].c = c;
    for (int k = 0; k < 10; k++) { v[k + 1].x = pts[k].x; v[k + 1].y = pts[k].y; v[k + 1].c = c; }
    for (int k = 0; k < 10; k++) { idx[k * 3] = 0; idx[k * 3 + 1] = (uint32_t)(k + 1); idx[k * 3 + 2] = (uint32_t)((k + 1) % 10 + 1); }
    cui__draw_triangles(ui, v, 11, idx, 30);
    cui_draw_polyline(ui, pts, 10, 1.0f, c, true);
}

typedef struct { int hover_index; cui_spring pop[16]; } cui__rating_state;

bool cui_rating(cui_ctx *ui, const char *id_str, int *value, int max) {
    if (max <= 0) max = 5;
    if (max > 16) max = 16;
    cui_id id = cui_make_id(ui, id_str);
    cui__rating_state *st = cui_state(ui, id, sizeof *st);
    int row = cui__node_new(ui, id);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 2; ui->nodes[row].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    int hover = -1;
    bool changed = false;
    for (int k = 0; k < max; k++) {
        cui_id sid = cui__hash(&k, sizeof k, id);
        cui_interact r = cui__interact(ui, sid, 0);
        if (r.hovered) { hover = k; cui__set_cursor(ui, CUI_CURSOR_HAND); }
        if (r.clicked) { *value = (*value == k + 1) ? 0 : k + 1; changed = true; st->pop[k].v = 6; }
        (void)r;
    }
    for (int k = 0; k < max; k++) {
        cui_id sid = cui__hash(&k, sizeof k, id);
        if (st->pop[k].x == 0 && st->pop[k].v == 0) st->pop[k].x = 1;
        cui__spring(ui, &st->pop[k], 1.0f, 0.35f, 0.45f);
        int s = cui__leaf(ui, sid, CUI_PX(22), CUI_PX(22));
        cui__mark_interactive(ui, s, 0);
        cui__star_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->fill = (k < *value) ? 1.0f : 0.0f;
        p->hover = (hover >= 0 && k <= hover) ? 1.0f : 0.0f;
        p->scale = st->pop[k].x;
        ui->nodes[s].paint = cui__paint_star;
        ui->nodes[s].paint_data = p;
    }
    cui__close(ui);
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Chips                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct { float hover, sel, focus; cui_spring press; } cui__chip_state;

bool cui_chip_ex(cui_ctx *ui, const char *label, bool *selected, const cui_chip_opts *o) {
    cui_chip_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, label) ^ 0xC41B0004u;
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_pop_disabled(ui);
    cui__chip_state *st = cui_state(ui, id, sizeof *st);
    bool changed = false;
    if (r.clicked && selected) { *selected = !*selected; changed = true; }
    bool sel = selected && *selected;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->sel, sel ? 1.0f : 0.0f, CUI__T_STATE);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    if (st->press.x == 0 && st->press.v == 0) st->press.x = 1;
    cui__spring(ui, &st->press, r.pressed ? CUI__PRESS : 1.0f, CUI_SPRING_SNAPPY);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 6;
    n->h = CUI_PX(30); n->pad[1] = n->pad[3] = 12;
    n->radius = CUI_CORNERS(15);
    n->self_align = CUI__ALIGN_NOSTRETCH;
    cui_color base_bg = cui_mix(cui__rgba(0, 0, 0, 0), CUI_C(ui, HOVER), st->hover);
    n->bg = cui_mix(base_bg, CUI_C(ui, ACCENT_SUBTLE), st->sel);
    n->border = 1;
    n->border_color = cui_mix(cui__quiet_edge(ui, st->hover), cui_mix(CUI_C(ui, ACCENT), CUI_C(ui, BACKGROUND), 0.45f), st->sel);
    n->scale = st->press.x;
    n->opacity = disabled ? CUI__DISABLED : 1.0f;
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    cui_color fg = cui_mix(CUI_C(ui, FOREGROUND), CUI_C(ui, ACCENT_STRONG), st->sel);
    if (sel) cui__icon_node(ui, CUI_ICON_CHECK, 14, fg);
    else if (o->icon) cui__icon_node(ui, o->icon, 14, CUI_C(ui, TEXT_SECONDARY));
    cui__label_text(ui, label, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm + 0.5f), fg);
    cui__close(ui);
    ui->last_id = id;
    return changed || (r.clicked && !selected);
}

/* ------------------------------------------------------------------------ */
/* Segmented control and tabs                                               */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring x, w, l, r; bool init; float hover[32]; float focus; } cui__seg_state;

typedef struct { cui_color bg, ring; float radius; bool shadow; bool underline; } cui__hl_paint;

static void cui__paint_highlight(cui_ctx *ui, cui_node *n, void *data) {
    cui__hl_paint *p = data;
    if (p->underline) {
        cui_rect r = { n->rect.x, n->rect.y + n->rect.h - 2, n->rect.w, 2 };
        cui_draw_rect(ui, r, CUI_CORNERS(1), p->bg);
        return;
    }
    cui_corners rad = CUI_CORNERS(p->radius);
    if (p->shadow) {
        cui_vec2 o = { 0, 1 };
        cui_draw_shadow(ui, n->rect, rad, 3, 0, o, cui__rgba(0, 0, 0, ui->mode == CUI_THEME_DARK ? 70 : 20));
    }
    cui_draw_rect_border(ui, n->rect, rad, p->bg, p->ring, p->ring.a ? 0.5f : 0);
}

/* Shared builder for segmented controls and tabs. kind: 0 segmented, 1 tabs underline, 2 tabs pill */
static bool cui__segments(cui_ctx *ui, const char *id_str, const char *const *items, const char *const *icons, int count,
                          int *index, int kind, cui_control_size size, bool full_width) {
    if (count <= 0) return false;
    if (count > 32) count = 32;
    cui_id id = cui_make_id(ui, id_str);
    cui__seg_state *st = cui_state(ui, id, sizeof *st);
    int old = *index;
    cui_id item_ids[32];
    cui_interact ir[32];
    bool any_focus = false;
    for (int k = 0; k < count; k++) {
        item_ids[k] = cui__hash(&k, sizeof k, id);
        ir[k] = cui__interact(ui, item_ids[k], CUI_IF_FOCUSABLE);
        if (ir[k].clicked) *index = k;
        if (ir[k].focused) any_focus = true;
        if (ir[k].focus_visible) any_focus = true;
    }
    if (any_focus) {
        int focused = -1;
        for (int k = 0; k < count; k++) if (ui->focus == item_ids[k]) focused = k;
        if (focused >= 0 && ui->focus_visible) {
            if (cui_key_pressed(ui, CUI_KEY_RIGHT) && focused < count - 1) { ui->focus = item_ids[focused + 1]; *index = focused + 1; }
            if (cui_key_pressed(ui, CUI_KEY_LEFT) && focused > 0) { ui->focus = item_ids[focused - 1]; *index = focused - 1; }
        }
    }
    if (*index < 0) *index = 0;
    if (*index >= count) *index = count - 1;
    cui_rect cr = cui__prev_rect(ui, id);
    cui_rect sr = cui__prev_rect(ui, item_ids[*index]);
    bool have = cui__has_prev_rect(ui, item_ids[*index]) && cui__has_prev_rect(ui, id);
    float tx = sr.x - cr.x, tw = sr.w;
    if (kind == 1) {
        /* underline under the label, not the padding */
        tx += 10; tw -= 20;
    }
    if (have) {
        if (!st->init) { st->l.x = tx; st->r.x = tx + tw; st->init = true; }
        /* each edge has its own spring: the one in front leads and the one behind follows a beat later,
           so the indicator stretches toward its new place and draws in as it lands */
        bool right = tx + tw > st->r.x + 0.5f, left = tx < st->l.x - 0.5f;
        cui__spring(ui, &st->l, tx, left ? 0.26f : 0.42f, left ? 0.0f : 0.14f);
        cui__spring(ui, &st->r, tx + tw, right ? 0.26f : 0.42f, right ? 0.0f : 0.14f);
        if (st->r.x - st->l.x < 4) st->r.x = st->l.x + 4;
        st->x.x = st->l.x; st->w.x = st->r.x - st->l.x;
    }
    float h = cui__control_height(ui, size);
    /* squeezed (a phone): segments drop to 8px side padding before their labels truncate. The row
       keeps asking for its natural width, so it gets the roomy padding back once there is room */
    float seg_pad = kind == 0 ? 14 : 10, natural = 0, lfs = cui__fs(ui, size == CUI_SIZE_SM ? ui->theme.text_sm : ui->theme.text_body);
    bool tight = false;
    if (!full_width && kind != 1) {
        for (int k = 0; k < count; k++) {
            bool lab = items[k] && cui__display_len(items[k]) > 0, ic = icons && icons[k];
            float iw = ic ? ceilf(cui__text_width(ui, CUI_FONT_ICONS, 16, icons[k], -1, false) * 4) / 4 + 6 : 0;
            natural += lab ? 2 * seg_pad + iw + ceilf(cui__text_width(ui, CUI_FONT_MEDIUM, lfs, items[k], cui__display_len(items[k]), false) * 4) / 4 : (kind == 1 ? h : h - (kind == 0 ? 6 : 4));
        }
        natural += kind == 0 ? 6 : 4 * (float)(count - 1);
        tight = cr.w > 0 && cr.w < natural - 0.5f;
    }
    int row = cui__node_new(ui, id);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW;
    rn->align = CUI_ALIGN_CENTER;
    if (full_width) { rn->w = CUI_FILL; rn->self_align = CUI_ALIGN_STRETCH; }
    else if (kind != 1) cui__soft_width(rn, CUI_FIT, natural);
    else rn->self_align = CUI__ALIGN_NOSTRETCH;
    float inner_h = h;
    if (kind == 0) {
        rn->pad[0] = rn->pad[1] = rn->pad[2] = rn->pad[3] = 3;
        rn->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, h * 0.5f));
        rn->bg = cui_alpha(CUI_C(ui, FOREGROUND), 0.06f);
        rn->gap = 0;
        inner_h = h - 6;
    } else if (kind == 1) {
        rn->gap = 4;
    } else {
        rn->gap = 4;
        inner_h = h - 4;
    }
    cui__open(ui, row);
    if (have && st->init) {
        int hl = cui__absolute(ui, 0, st->x.x, kind == 0 ? 3 : 0, st->w.x, kind == 1 ? h : inner_h);   /* pill tabs have no inset: the highlight sits on the tab */
        cui__hl_paint *p = cui__frame_alloc(ui, sizeof *p);
        if (kind == 0) {
            /* the selected segment has to stand off the track in both modes */
            p->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE_RAISED), CUI_C(ui, FOREGROUND), 0.17f) : CUI_C(ui, SURFACE_RAISED);
            p->ring = ui->mode == CUI_THEME_DARK ? cui__rgba(255, 255, 255, 22) : cui__rgba(0, 0, 0, 16);
            p->radius = CUI_MIN(ui->theme.radius_control - 3, inner_h * 0.5f);
            p->shadow = true;
        } else if (kind == 1) {
            p->bg = CUI_C(ui, FOREGROUND);
            p->underline = true;
        } else {
            p->bg = CUI_C(ui, ACTIVE);
            p->radius = CUI_MIN(ui->theme.radius_control, inner_h * 0.5f);
        }
        ui->nodes[hl].paint = cui__paint_highlight;
        ui->nodes[hl].paint_data = p;
    }
    for (int k = 0; k < count; k++) {
        bool sel = k == *index;
        cui__tween(ui, &st->hover[k], ir[k].hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        int it = cui__node_new(ui, item_ids[k]);
        cui_node *in = &ui->nodes[it];
        in->dir = CUI_ROW; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER; in->gap = 6;
        in->h = CUI_PX(kind == 1 ? h : inner_h);
        in->pad[1] = in->pad[3] = tight ? 8 : seg_pad;
        bool icon_only = icons && icons[k] && (!items[k] || cui__display_len(items[k]) == 0);
        if (icon_only) { in->pad[1] = in->pad[3] = 0; in->w = CUI_PX(kind == 1 ? h : inner_h); }   /* square segment */
        if (full_width) in->w = CUI_FILL;
        /* segments nest inside the track (3 in), pill tabs stand alone */
        in->radius = CUI_CORNERS(CUI_MIN(kind == 0 ? ui->theme.radius_control - 3 : ui->theme.radius_control, inner_h * 0.5f));
        if (kind == 2 && !sel) in->bg = cui_alpha(CUI_C(ui, HOVER), st->hover[k]);
        cui__mark_interactive(ui, it, CUI_IF_FOCUSABLE);
        if (ir[k].hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        if (ir[k].focus_visible) {
            float *fa = cui__frame_alloc(ui, sizeof(float));
            *fa = 1;
            in->paint_data = fa;
            in->paint = NULL;
        }
        cui__open(ui, it);
        cui_color fg = sel ? CUI_C(ui, FOREGROUND) : cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), st->hover[k] * 0.7f);
        if (icons && icons[k]) cui__icon_node(ui, icons[k], 16, fg);
        if (items[k] && cui__display_len(items[k]) > 0) cui__label_text(ui, items[k], CUI_FONT_MEDIUM, cui__fs(ui, size == CUI_SIZE_SM ? ui->theme.text_sm : ui->theme.text_body), fg);
        cui__close(ui);
        if (ir[k].focus_visible) {
            ui->nodes[it].flags |= CUI_NF_PAINT_AFTER;
            extern void cui__paint_focus_ring_node(cui_ctx *ui, cui_node *n, void *data);
            ui->nodes[it].paint = cui__paint_focus_ring_node;
        }
    }
    cui__close(ui);
    ui->last_id = id;
    return *index != old;
}

/* the baseline under a tab row, painted before the tabs so the active
 * indicator sits on top of it rather than above it */
static void cui__paint_tab_baseline(cui_ctx *ui, cui_node *n, void *data) {
    cui_color *c = data;
    cui_draw_rect(ui, (cui_rect){ n->rect.x, n->rect.y + n->rect.h - 1, n->rect.w, 1 }, CUI_CORNERS(0), *c);
}

void cui__paint_focus_ring_node(cui_ctx *ui, cui_node *n, void *data) {
    (void)data;
    cui__draw_focus_ring(ui, n->rect, n->radius, 1.0f);
}

bool cui_segmented_ex(cui_ctx *ui, const char *id, const char *const *items, int count, int *index, const cui_segmented_opts *o) {
    cui_segmented_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    return cui__segments(ui, id, items, o->icons, count, index, 0, o->size, o->full_width);
}

bool cui_tabs_ex(cui_ctx *ui, const char *id, const char *const *items, int count, int *index, const cui_tabs_opts *o) {
    cui_tabs_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    bool pill = o->variant == CUI_TABS_PILL;
    if (pill && o->full_width) return cui__segments(ui, id, items, o->icons, count, index, 2, CUI_SIZE_MD, true);
    /* Tabs never truncate: when they are wider than their container the
     * strip scrolls sideways, fades at the edge with more, and brings a newly
     * selected tab into view. */
    cui_id sid = cui_make_id(ui, id) ^ 0x7AB5u;
    cui_scroll_state *ss = cui_state(ui, sid, sizeof *ss);
    int *last = cui_state(ui, sid ^ 0x1u, sizeof(int));   /* last index + 1, so 0 means unset */
    if (*index >= 0 && *index < count && last[0] != *index + 1 && ss->content_w > ss->view_w + 0.5f) {
        cui_rect vr = cui__prev_rect(ui, sid), tr = cui__prev_rect(ui, cui__hash(index, sizeof *index, cui_make_id(ui, id)));
        if (vr.w > 0 && tr.w > 0) {
            float margin = 24;
            if (tr.x < vr.x) ss->tx -= vr.x - tr.x + margin;
            else if (tr.x + tr.w > vr.x + vr.w) ss->tx += tr.x + tr.w - (vr.x + vr.w) + margin;
        }
    }
    if (*index >= 0) last[0] = *index + 1;
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL;
    int col = cui__scroll_begin_ex(ui, sid, &sb, true);
    ui->nodes[col].flags &= ~(uint32_t)CUI_NF_SCROLL_Y;
    ui->nodes[col].self_align = pill ? CUI__ALIGN_NOSTRETCH : CUI_ALIGN_STRETCH;
    if (pill) ui->nodes[col].w = CUI_FIT;
    ss->fade = cui__backdrop(ui);
    if (!pill) {
        cui_color *bc = cui__frame_alloc(ui, sizeof *bc);
        *bc = CUI_C(ui, BORDER);
        ui->nodes[col].paint = cui__paint_tab_baseline;
        ui->nodes[col].paint_data = bc;
    }
    bool r = cui__segments(ui, id, items, o->icons, count, index, pill ? 2 : 1, CUI_SIZE_MD, o->full_width);
    cui__scroll_end_ex(ui);
    return r;
}

/* ------------------------------------------------------------------------ */
/* Sidebar navigation                                                       */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint64_t frame;
    cui_id first_id;
    int first_node;
    float target_dy, target_h;
    bool has_target, init;
    cui_spring y, h;
} cui__nav_group;

typedef struct { float hover, focus; cui_spring press; } cui__nav_state;

bool cui_nav_item_ex(cui_ctx *ui, const char *label, const cui_nav_opts *o) {
    cui_nav_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id gid = cui_make_id(ui, o->group ? o->group : "#nav");
    cui__nav_group *g = cui_state(ui, gid, sizeof *g);
    cui_id id = cui_make_id(ui, label) ^ 0x4A570005u;
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui__nav_state *st = cui_state(ui, id, sizeof *st);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    if (st->press.x == 0 && st->press.v == 0) st->press.x = 1;
    cui__spring(ui, &st->press, r.pressed ? CUI__PRESS_WIDE : 1.0f, CUI_SPRING_SNAPPY);
    bool first = g->frame != ui->frame;
    if (first) {
        g->frame = ui->frame;
        g->first_id = id;
    }
    if (o->selected && cui__has_prev_rect(ui, id) && cui__has_prev_rect(ui, g->first_id)) {
        cui_rect me = cui__prev_rect(ui, id), f0 = cui__prev_rect(ui, g->first_id);
        g->target_dy = me.y - f0.y;
        g->target_h = me.h;
        g->has_target = true;
    }
    int i = cui__node_new(ui, id);
    if (first) g->first_node = i;
    if (first && g->has_target) {
        if (!g->init) { g->y.x = g->target_dy; g->h.x = g->target_h; g->init = true; }
        cui__spring(ui, &g->y, g->target_dy, CUI_SPRING_MORPH);
        cui__spring(ui, &g->h, g->target_h, CUI_SPRING_MORPH);
    }
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 10;
    n->h = CUI_PX(32);
    n->pad[1] = 10; n->pad[3] = 10;
    n->w = CUI_FILL;
    n->self_align = CUI_ALIGN_STRETCH;
    n->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    if (!o->selected) n->bg = cui_alpha(CUI_C(ui, HOVER), st->hover);
    n->scale = st->press.x;
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    if (first && g->init) {
        /* the gliding highlight, positioned relative to the first item */
        int hl = cui__absolute(ui, 0, 0, g->y.x, 0, g->h.x);
        cui_node *hn = &ui->nodes[hl];
        hn->abs_anchor = i;
        hn->w = CUI_PX(0);
        hn->flags |= CUI_NF_PAINT_AFTER;
        cui__hl_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->bg = CUI_C(ui, ACTIVE);
        p->radius = 10;
        hn->paint_data = p;
        extern void cui__paint_nav_highlight(cui_ctx *ui, cui_node *n, void *data);
        hn->paint = cui__paint_nav_highlight;
        hn->fx = 0;
        /* width follows the anchor; resolved in paint */
    }
    cui__open(ui, i);
    cui_color fg = o->selected ? CUI_C(ui, FOREGROUND) : cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), st->hover * 0.6f);
    if (o->icon) cui__icon_node(ui, o->icon, 16, o->selected ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
    int t = cui__text_node(ui, label, cui__display_len(label), o->selected ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), fg, false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].w = CUI_FILL;
    if (o->badge) {
        int b = cui__text_node(ui, o->badge, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[b].flags |= CUI_NF_TABULAR;
        ui->nodes[b].line_height = 1.2f;
    }
    cui__close(ui);
    if (st->focus > 0.01f) {
        float *fa = cui__frame_alloc(ui, sizeof(float));
        *fa = st->focus;
        ui->nodes[i].paint_data = fa;
        ui->nodes[i].paint = cui__paint_focus_ring_node;
        ui->nodes[i].flags |= CUI_NF_PAINT_AFTER;
    }
    ui->last_id = id;
    return r.clicked;
}

void cui__paint_nav_highlight(cui_ctx *ui, cui_node *n, void *data) {
    cui__hl_paint *p = data;
    int a = n->abs_anchor;
    if (a < 0) return;
    cui_rect r = n->rect;
    r.w = ui->nodes[a].rect.w;
    cui_draw_rect(ui, r, CUI_CORNERS(p->radius), p->bg);
}

void cui_nav_section(cui_ctx *ui, const char *title) {
    int box = cui__node_new(ui, 0);
    ui->nodes[box].pad[0] = 14; ui->nodes[box].pad[2] = 4; ui->nodes[box].pad[3] = 10;
    cui__open(ui, box);
    int t = cui__text_node(ui, title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.3f;
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Breadcrumb, pagination, stepper                                          */
/* ------------------------------------------------------------------------ */

int cui_breadcrumb(cui_ctx *ui, const char *const *items, int count) {
    int clicked = -1;
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 6; ui->nodes[row].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    for (int k = 0; k < count; k++) {
        bool last = k == count - 1;
        if (last) {
            cui__label_text(ui, items[k], CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND));
        } else {
            cui_id id = cui_make_id(ui, items[k]);
            cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
            float hv_v = cui__hover_fade(ui, id, r.hovered, CUI__T_HOVER), *hv = &hv_v;
            int t = cui__text_node(ui, items[k], cui__display_len(items[k]), CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body),
                                   cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), *hv), false);
            ui->nodes[t].id = id;
            ui->nodes[t].line_height = 1.3f;
            cui__mark_interactive(ui, t, CUI_IF_FOCUSABLE);
            if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
            if (r.clicked) clicked = k;
            cui__icon_node(ui, CUI_ICON_CHEVRON_RIGHT, 14, CUI_C(ui, TEXT_MUTED));
        }
    }
    cui__close(ui);
    return clicked;
}

static bool cui__page_button(cui_ctx *ui, cui_id id, const char *text, bool selected) {
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    float hv_v = cui__hover_fade(ui, id, r.hovered, CUI__T_HOVER), *hv = &hv_v;
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->w = CUI_PX(32); n->h = CUI_PX(32);
    n->min_w = 32;
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->justify = CUI_JUSTIFY_CENTER;
    n->radius = CUI_CORNERS(CUI__R_CONTROL(ui, 32));
    n->bg = selected ? CUI_C(ui, ACTIVE) : cui_alpha(CUI_C(ui, HOVER), *hv);
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    int t = cui__text_node(ui, text, -1, selected ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body),
                           selected ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY), false);
    ui->nodes[t].flags |= CUI_NF_TABULAR;
    ui->nodes[t].line_height = 1.2f;
    cui__close(ui);
    return r.clicked;
}

bool cui_pagination(cui_ctx *ui, const char *id_str, int *page, int page_count) {
    if (page_count < 1) page_count = 1;
    int old = *page;
    cui_push_id(ui, id_str);
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 2; ui->nodes[row].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_MD; bo.tooltip = "Previous page"; bo.disabled = *page <= 0;
    if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_LEFT, &bo)) (*page)--;
    int pages[9], n = 0;
    /* in a narrow parent (a phone) the pager keeps first, current and last: five slots, not seven */
    cui_node *par = row > 0 ? &ui->nodes[ui->nodes[row].parent] : NULL;
    cui_rect ppr = par && par->id ? cui__prev_rect(ui, par->id) : (cui_rect){ 0, 0, 0, 0 };
    float pavail = ppr.w > 0 ? ppr.w - (par ? par->pad[1] + par->pad[3] : 0) : cui_viewport_size(ui).x - 32;
    bool compact = page_count > 7 && pavail < 2 * 36 + 7 * 32 + 8 * 2;
    if (page_count <= 7) { for (int k = 0; k < page_count; k++) pages[n++] = k; }
    else if (compact) {
        pages[n++] = 0;
        if (*page > 1) pages[n++] = -1;
        if (*page > 0 && *page < page_count - 1) pages[n++] = *page;
        if (*page < page_count - 2) pages[n++] = -2;
        pages[n++] = page_count - 1;
    } else {
        pages[n++] = 0;
        int a = CUI_MAX(1, *page - 1), b = CUI_MIN(page_count - 2, *page + 1);
        if (*page <= 2) { a = 1; b = 3; }
        if (*page >= page_count - 3) { a = page_count - 4; b = page_count - 2; }
        if (a > 1) pages[n++] = -1;
        for (int k = a; k <= b; k++) pages[n++] = k;
        if (b < page_count - 2) pages[n++] = -2;
        pages[n++] = page_count - 1;
    }
    for (int k = 0; k < n; k++) {
        if (pages[k] < 0) {
            int e = cui__node_new(ui, 0);
            ui->nodes[e].w = CUI_PX(24); ui->nodes[e].dir = CUI_ROW; ui->nodes[e].justify = CUI_JUSTIFY_CENTER;
            cui__open(ui, e);
            cui__icon_node(ui, CUI_ICON_ELLIPSIS, 14, CUI_C(ui, TEXT_MUTED));
            cui__close(ui);
            continue;
        }
        char buf[16];
        snprintf(buf, sizeof buf, "%d", pages[k] + 1);
        cui_id pid = cui__hash(&pages[k], sizeof(int), cui_make_id(ui, "#page"));
        if (cui__page_button(ui, pid, buf, pages[k] == *page)) *page = pages[k];
    }
    bo.tooltip = "Next page"; bo.disabled = *page >= page_count - 1;
    if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_RIGHT, &bo)) (*page)++;
    cui__close(ui);
    cui_pop_id(ui);
    if (*page < 0) *page = 0;
    if (*page >= page_count) *page = page_count - 1;
    return *page != old;
}

typedef struct { int state; float t; } cui__step_paint;   /* 0 upcoming, 1 current, 2 done */

static void cui__paint_step(cui_ctx *ui, cui_node *n, void *data) {
    cui__step_paint *p = data;
    cui_rect r = n->rect;
    float rr = r.w * 0.5f;
    if (p->state == 2) {
        cui_draw_rect(ui, r, CUI_CORNERS(rr), CUI_C(ui, CONTROL_ON));
        cui_rect cr = cui__rect_inset(r, 3);
        cui__draw_check(ui, cr, 1, 1.8f, CUI_C(ui, CONTROL_GLYPH));
    } else if (p->state == 1) {
        cui_draw_rect_border(ui, r, CUI_CORNERS(rr), CUI_C(ui, SURFACE), CUI_C(ui, CONTROL_ON), 1.5f);
    } else {
        cui_draw_rect_border(ui, r, CUI_CORNERS(rr), CUI_C(ui, SURFACE), CUI_C(ui, BORDER_STRONG), 1);
    }
}

void cui_stepper(cui_ctx *ui, const char *const *steps, int count, int current) {
    /* narrower than every label needs (a phone), only the current step keeps its label;
       the others show their circle, so nothing is cut to "Ac..." */
    cui_id sid = count > 0 && steps[0] ? cui_make_id(ui, steps[0]) ^ 0x57E9u : 0;
    cui_rect pr = sid ? cui__prev_rect(ui, sid) : (cui_rect){ 0, 0, 0, 0 };
    float need = 0, fs = cui__fs(ui, ui->theme.text_body);
    for (int k = 0; k < count; k++)
        need += 32 + cui__text_width(ui, k == current ? CUI_FONT_MEDIUM : CUI_FONT_BODY, fs, steps[k] ? steps[k] : "", -1, false) + (k < count - 1 ? 36 : 0);
    bool compact = pr.w > 0 && pr.w + 0.5f < need;
    int row = cui__node_new(ui, sid);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = compact ? 6 : 10; ui->nodes[row].w = CUI_FILL; ui->nodes[row].self_align = CUI_ALIGN_STRETCH;
    cui__open(ui, row);
    for (int k = 0; k < count; k++) {
        int item = cui__node_new(ui, 0);
        ui->nodes[item].dir = CUI_ROW; ui->nodes[item].gap = 8;
        cui__open(ui, item);
        int c = cui__node_new(ui, 0);
        cui_node *cn = &ui->nodes[c];
        cn->w = CUI_PX(24); cn->h = CUI_PX(24);
        cn->dir = CUI_STACK; cn->align = CUI_ALIGN_CENTER; cn->justify = CUI_JUSTIFY_CENTER;
        cui__step_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->state = k < current ? 2 : (k == current ? 1 : 0);
        cn->paint = cui__paint_step;
        cn->paint_data = p;
        cui__open(ui, c);
        if (k >= current) {
            char buf[16];
            snprintf(buf, sizeof buf, "%d", k + 1);
            int t = cui__text_node(ui, buf, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), k == current ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[t].line_height = 1.0f;
        }
        cui__close(ui);
        if (!compact || k == current) {
            int t = cui__text_node(ui, steps[k], -1, k == current ? CUI_FONT_MEDIUM : CUI_FONT_BODY, fs,
                                   k <= current ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[t].line_height = 1.3f;
        }
        cui__close(ui);
        if (k < count - 1) {
            int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
            ui->nodes[line].bg = k < current ? CUI_C(ui, CONTROL_ON) : CUI_C(ui, BORDER);
            ui->nodes[line].min_w = compact ? 12 : 16;
        }
    }
    cui__close(ui);
}
