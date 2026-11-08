/* Knob, fader and level meter: controls for audio, synth and mixer panels.
 * Everything is painted by one node per control, so a mixer with dozens of
 * channel strips stays cheap: the layout sees a few fixed size boxes. */
#include "cui_internal.h"

#define cui__fs cui__fsz

#define CUI__KNOB_SPAN (1.5f * CUI_PI)    /* 270 degrees, the gap at the bottom */
#define CUI__KNOB_A0   (0.75f * CUI_PI)   /* where the arc starts: down and to the left */
#define CUI__FADER_TW  30.0f              /* fader thumb, across the track */
#define CUI__FADER_TH  16.0f              /* fader thumb, along the track */
#define CUI__METER_MAXCH 8

/* ---- shared ---- */

static float cui__dl_norm(float v, float lo, float hi) { return hi > lo ? cui__clampf((v - lo) / (hi - lo), 0, 1) : 0; }

/* 0..1 along the control for a value; on a log scale equal distances multiply it */
static float cui__dl_pos(float v, float lo, float hi, bool lg) {
    if (lg && lo > 0 && hi > lo) return cui__clampf(logf(CUI_MAX(v, lo) / lo) / logf(hi / lo), 0, 1);
    return cui__dl_norm(v, lo, hi);
}

static float cui__dl_value(float t, float lo, float hi, float step, bool lg) {
    t = cui__clampf(t, 0, 1);
    float v = lg && lo > 0 && hi > lo ? lo * powf(hi / lo, t) : lo + t * (hi - lo);
    if (step > 0) v = lo + roundf((v - lo) / step) * step;
    return cui__clampf(v, CUI_MIN(lo, hi), CUI_MAX(lo, hi));
}

static float cui__dl_reset_t(float lo, float hi, float reset, bool lg) { return cui__dl_pos(reset, lo, hi, lg); }

static const char *cui__dl_text(cui_ctx *ui, const char *fmt, const char *unit, float v) {
    const char *s = cui__frame_printf(ui, fmt, (double)v);
    return unit && *unit ? cui__frame_printf(ui, "%s %s", s, unit) : s;
}

/* Keys, double-click and the wheel, shared by knobs and faders. The wheel
 * only turns a focused control, so scrolling a page past a mixer never
 * changes a level by accident. */
static float cui__dl_input(cui_ctx *ui, cui_interact r, cui_id wheel_id, float t, float lo, float hi, float step, float reset_t) {
    float k = step > 0 && hi > lo ? step / (hi - lo) : 0.01f;
    if (r.double_clicked) t = reset_t;
    if (r.focused) {
        float big = (ui->mods & CUI_MOD_SHIFT) ? 10.0f : 1.0f;
        if (cui_key_pressed(ui, CUI_KEY_UP) || cui_key_pressed(ui, CUI_KEY_RIGHT)) t += k * big;
        if (cui_key_pressed(ui, CUI_KEY_DOWN) || cui_key_pressed(ui, CUI_KEY_LEFT)) t -= k * big;
        if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) t += k * 10;
        if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) t -= k * 10;
        if (cui_key_pressed(ui, CUI_KEY_HOME)) t = 0;
        if (cui_key_pressed(ui, CUI_KEY_END)) t = 1;
        if (cui_key_pressed(ui, CUI_KEY_DELETE) || cui_key_pressed(ui, CUI_KEY_BACKSPACE)) t = reset_t;
    }
    if (ui->scroll_target == wheel_id && (ui->scroll_dy != 0 || ui->scroll_dx != 0)) {
        float d = ui->scroll_dy != 0 ? ui->scroll_dy : -ui->scroll_dx;
        t += d * k * ((ui->mods & CUI_MOD_SHIFT) ? 0.25f : 1.0f);
        ui->scroll_dy = ui->scroll_dx = 0;   /* consumed */
    }
    return cui__clampf(t, 0, 1);
}

static void cui__dl_text_at(cui_ctx *ui, cui_font f, float size, float x, float y, int align, cui_color c, const char *s) {
    float w = cui__text_width(ui, f, size, s, -1, true);
    float fh = cui__font_line_height(ui, f, size);
    float tx = align == 1 ? x - w * 0.5f : align == 2 ? x - w : x;
    cui__draw_text_line(ui, f, size, tx, y - fh * 0.5f, c, s, -1, true);
}

/* width that every value of the control fits in, so neighbours never shift as it turns */
static float cui__dl_value_w(cui_ctx *ui, float fs, const char *fmt, const char *unit, float lo, float hi) {
    float probe[5] = { lo, hi, (lo + hi) * 0.5f, lo + (hi - lo) * 0.111f, -CUI_MAX(fabsf(lo), fabsf(hi)) * 0.999f };
    float w = 0;
    for (int k = 0; k < (lo < 0 ? 5 : 4); k++)
        w = CUI_MAX(w, cui__text_width(ui, CUI_FONT_MEDIUM, fs, cui__dl_text(ui, fmt, unit, probe[k]), -1, true));
    return ceilf(w);
}

/* a label under the control, centered, cut with an ellipsis rather than widening it */
static void cui__dl_caption(cui_ctx *ui, const char *text, float size, cui_color c, cui_font f) {
    int t = cui__text_node(ui, text, cui__display_len(text), f, size, c, false);
    ui->nodes[t].text_align = CUI_TEXT_CENTER;
    ui->nodes[t].w = CUI_FILL;
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].flags |= CUI_NF_TABULAR;
}

/* ---- knob ---- */

typedef struct { cui_spring shown; float ref_y, ref_t, hover, active, focus; bool init, dragging, fine; } cui__knob_state;
typedef struct { float t, base, hover, active, focus; cui_color arc, track, body, edge, pointer; cui_id wheel; bool wheel_on; } cui__knob_paint;

static void cui__dl_cap(cui_ctx *ui, cui_vec2 c, float rad, float a, float th, cui_color col) {
    cui_draw_circle(ui, (cui_vec2){ c.x + cosf(a) * rad, c.y + sinf(a) * rad }, th * 0.5f, col);
}

static void cui__paint_knob(cui_ctx *ui, cui_node *n, void *data) {
    cui__knob_paint *p = data;
    cui_rect r = n->rect;
    if (p->wheel_on) cui__paint_wheel_hit(ui, p->wheel, r);
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    float R = CUI_MIN(r.w, r.h) * 0.5f - 1;
    float th = cui__clampf(R * 0.12f, 2.5f, 4.0f), ar = R - th * 0.5f;
    float a0 = CUI__KNOB_A0, a1 = a0 + CUI__KNOB_SPAN;
    cui_draw_arc(ui, c, ar, a0, a1, th, p->track);
    cui__dl_cap(ui, c, ar, a0, th, p->track);
    cui__dl_cap(ui, c, ar, a1, th, p->track);
    float av = a0 + CUI__KNOB_SPAN * p->t, ab = a0 + CUI__KNOB_SPAN * p->base;
    if (fabsf(av - ab) > 0.002f) {
        cui_draw_arc(ui, c, ar, CUI_MIN(ab, av), CUI_MAX(ab, av), th, p->arc);
        cui__dl_cap(ui, c, ar, ab, th, p->arc);
        cui__dl_cap(ui, c, ar, av, th, p->arc);
    }
    float rb = R - th - 4;
    if (rb < 4) return;
    cui_rect br = { c.x - rb, c.y - rb, rb * 2, rb * 2 };
    bool dark = ui->mode == CUI_THEME_DARK;
    cui_draw_shadow(ui, br, CUI_CORNERS(rb), 2, 0, (cui_vec2){ 0, 1 }, cui__rgba(0, 0, 0, dark ? 90 : 34));
    cui_draw_shadow(ui, br, CUI_CORNERS(rb), 8, 0, (cui_vec2){ 0, 3 }, cui__rgba(0, 0, 0, dark ? 60 : 18));
    cui_color body = cui_mix(p->body, CUI_C(ui, FOREGROUND), 0.035f * p->hover + 0.03f * p->active);
    cui_draw_rect_border(ui, br, CUI_CORNERS(rb), body, p->edge, 1);
    cui__draw_sheen(ui, br, CUI_CORNERS(rb), 0.8f);
    cui_vec2 d = { cosf(av), sinf(av) };
    cui_vec2 q0 = { c.x + d.x * rb * 0.3f, c.y + d.y * rb * 0.3f }, q1 = { c.x + d.x * (rb - 4), c.y + d.y * (rb - 4) };
    cui_draw_line(ui, q0, q1, 2, p->pointer);
    cui_draw_circle(ui, q1, 1, p->pointer);
    cui_draw_circle(ui, q0, 1, p->pointer);
    cui__draw_focus_ring(ui, br, CUI_CORNERS(rb), p->focus);
}

bool cui_knob_ex(cui_ctx *ui, const char *id_str, float *value, float min, float max, const cui_knob_opts *o) {
    cui_knob_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!value) return false;
    if (!(max > min)) max = min + 1;
    cui_id id = cui_make_id(ui, id_str ? id_str : "knob");
    cui_id kid = id ^ 0x4B0Bu, wid = id ^ 0x4B0Cu;
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, kid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui_pop_disabled(ui);
    cui__knob_state *st = cui_state(ui, id, sizeof *st);
    bool lg = o->log_scale && min > 0;
    float old = *value, t = cui__dl_pos(*value, min, max, lg);
    if (!st->init) { st->shown.x = t; st->init = true; }
    float reset_t = cui__dl_reset_t(min, max, o->reset, lg);
    /* drag up to turn right: 200 points for the whole range, Shift for a tenth of that */
    bool fine = (ui->mods & CUI_MOD_SHIFT) != 0;
    if (r.active && ui->mdown[0] && !disabled) {
        if (ui->mpressed[0] || !st->dragging) { st->ref_y = ui->my; st->ref_t = t; st->fine = fine; }
        else if (fine != st->fine) { st->ref_y = ui->pmy; st->ref_t = t; st->fine = fine; }   /* Shift changes the rate from here on */
        st->dragging = true;
        t = cui__clampf(st->ref_t + (st->ref_y - ui->my) / (fine ? 2000.0f : 200.0f), 0, 1);
    } else st->dragging = false;
    if (!disabled) t = cui__dl_input(ui, r, wid, t, min, max, lg ? 0 : o->step, reset_t);
    float v = cui__dl_value(t, min, max, o->step, lg);
    *value = v;
    t = cui__dl_pos(v, min, max, lg);
    if (st->dragging) { st->shown.x = t; st->shown.v = 0; }
    else cui__spring(ui, &st->shown, t, 0.26f, 0.0f);
    cui__tween(ui, &st->hover, (r.hovered || r.active) ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->active, r.active ? 1.0f : 0.0f, CUI__T_STATE);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    if ((r.hovered || r.active) && !disabled) cui__set_cursor(ui, CUI_CURSOR_RESIZE_NS);

    float size = o->size > 0 ? o->size : 48;
    const char *fmt = o->format ? o->format : "%.0f";
    const char *label = o->label ? o->label : id_str;
    float fv = cui__fs(ui, ui->theme.text_sm), fl = cui__fs(ui, ui->theme.text_xs);
    float w = size;
    if (!o->hide_value) w = CUI_MAX(w, cui__dl_value_w(ui, fv, fmt, o->unit, min, max));
    if (label) w = CUI_MAX(w, CUI_MIN(ceilf(cui__text_width(ui, CUI_FONT_BODY, fl, label, cui__display_len(label), false)), size * 2));
    int col = cui__node_new(ui, id);
    cui_node *cn = &ui->nodes[col];
    cn->gap = 2; cn->align = CUI_ALIGN_CENTER;
    cn->w = CUI_PX(w); cn->self_align = CUI__ALIGN_NOSTRETCH;
    cn->opacity = disabled ? CUI__DISABLED : 1.0f;
    cui__open(ui, col);
    int k = cui__leaf(ui, kid, CUI_PX(size), CUI_PX(size));
    cui__mark_interactive(ui, k, CUI_IF_FOCUSABLE);
    cui__knob_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->t = st->shown.x; p->base = o->bipolar ? 0.5f : 0;
    p->hover = st->hover; p->active = st->active; p->focus = st->focus;
    p->arc = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, CONTROL_ON);
    p->track = cui_mix(CUI_C(ui, CONTROL_TRACK), CUI_C(ui, CONTROL_TRACK_HOVER), st->hover);
    p->body = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE_RAISED), CUI_C(ui, FOREGROUND), 0.06f) : CUI_C(ui, SURFACE_RAISED);
    p->edge = ui->mode == CUI_THEME_DARK ? cui__rgba(255, 255, 255, 20) : cui__rgba(0, 0, 0, 24);
    p->pointer = CUI_C(ui, FOREGROUND);
    p->wheel = wid; p->wheel_on = r.focused && !disabled;
    ui->nodes[k].paint = cui__paint_knob;
    ui->nodes[k].paint_data = p;
    if (!o->hide_value) cui__dl_caption(ui, cui__dl_text(ui, fmt, o->unit, v), fv, CUI_C(ui, FOREGROUND), CUI_FONT_MEDIUM);
    if (label && cui__display_len(label)) cui__dl_caption(ui, label, fl, CUI_C(ui, TEXT_SECONDARY), CUI_FONT_BODY);
    cui__close(ui);
    ui->last_id = kid;
    return *value != old;
}

/* ---- level meter ---- */

typedef struct { float shown, peak, age; bool clip; } cui__meter_ch;
typedef struct { cui__meter_ch ch[CUI__METER_MAXCH]; bool init; } cui__meter_state;
typedef struct {
    cui__meter_ch ch[CUI__METER_MAXCH];
    int n;
    float lo, hi, th, lw;
    bool horizontal, scale, segmented;
    cui_color bg, zone[3], muted;
} cui__meter_paint;

static int cui__meter_zone(float db) { return db >= -6 ? 2 : db >= -18 ? 1 : 0; }

/* the bars (and the scale) of a meter in r */
static void cui__meter_bars(cui_ctx *ui, const cui__meter_paint *p, cui_rect r) {
    bool hz = p->horizontal;
    float len = hz ? r.w : r.h, span = p->hi - p->lo;
    if (len <= 2 || span <= 0) return;
    /* position along the bar for a level: 0 at the bottom (left), len at the top (right) */
#define CUI__MPOS(db) (cui__clampf(((db) - p->lo) / span, 0, 1) * len)
    float fx = cui__fs(ui, ui->theme.text_xs) * 0.9f;
    if (p->scale) {
        static const float marks[] = { 6, 3, 0, -3, -6, -9, -12, -18, -24, -30, -36, -42, -48, -54, -60, -72, -84, -96 };
        float last = -1e9f;
        for (int k = 0; k < (int)(sizeof marks / sizeof marks[0]); k++) {
            float db = marks[k];
            if (db > p->hi + 0.01f || db < p->lo - 0.01f) continue;
            float at = CUI__MPOS(db);
            if (fabsf(at - last) < (hz ? 26.0f : 13.0f)) continue;
            last = at;
            const char *s = cui__frame_printf(ui, "%g", (double)db);
            if (hz) cui__dl_text_at(ui, CUI_FONT_BODY, fx, r.x + at, r.y + p->n * (p->th + 2) + 8, 1, p->muted, s);
            else cui__dl_text_at(ui, CUI_FONT_BODY, fx, r.x + p->lw - 5, r.y + r.h - at, 2, p->muted, s);
        }
    }
    for (int c = 0; c < p->n; c++) {
        const cui__meter_ch *ch = &p->ch[c];
        cui_rect bar = hz ? (cui_rect){ r.x, r.y + c * (p->th + 2), r.w, p->th } : (cui_rect){ r.x + p->lw + c * (p->th + 2), r.y, p->th, r.h };
        float rad = CUI_MIN(2.0f, p->th * 0.5f);
        cui_draw_rect(ui, bar, CUI_CORNERS(rad), p->bg);
        float fill = CUI__MPOS(ch->shown);
        if (p->segmented) {
            float seg = 4;
            for (float s0 = 0; s0 + seg - 1 <= fill + 0.01f; s0 += seg) {
                float db = p->lo + (s0 + seg * 0.5f) / len * span;
                cui_rect q = hz ? (cui_rect){ bar.x + s0, bar.y, seg - 1, bar.h } : (cui_rect){ bar.x, bar.y + bar.h - s0 - (seg - 1), bar.w, seg - 1 };
                cui_draw_rect(ui, q, CUI_CORNERS(0.5f), p->zone[cui__meter_zone(db)]);
            }
        } else if (fill > 0.5f) {
            /* one rect per color zone the level reaches */
            static const float edges[4] = { -1e9f, -18, -6, 1e9f };
            for (int z = 0; z < 3; z++) {
                float z0 = CUI__MPOS(edges[z]), z1 = CUI_MIN(CUI__MPOS(edges[z + 1]), fill);
                if (z1 <= z0) continue;
                cui_rect q = hz ? (cui_rect){ bar.x + z0, bar.y, z1 - z0, bar.h } : (cui_rect){ bar.x, bar.y + bar.h - z1, bar.w, z1 - z0 };
                cui_corners cc = CUI_CORNERS(0);
                if (z0 <= 0.01f) { if (hz) { cc.tl = cc.bl = rad; } else { cc.bl = cc.br = rad; } }
                cui_draw_rect(ui, q, cc, p->zone[z]);
            }
        }
        if (ch->peak > p->lo + 0.01f) {
            float at = CUI__MPOS(ch->peak);
            cui_rect q = hz ? (cui_rect){ bar.x + CUI_MAX(0.0f, at - 2), bar.y, 2, bar.h } : (cui_rect){ bar.x, bar.y + bar.h - CUI_MAX(2.0f, at), bar.w, 2 };
            cui_draw_rect(ui, q, CUI_CORNERS(0), p->zone[cui__meter_zone(ch->peak)]);
        }
    }
#undef CUI__MPOS
}

static void cui__paint_meter(cui_ctx *ui, cui_node *n, void *data) { cui__meter_bars(ui, data, n->rect); }

/* ballistics: a bar jumps up to a new peak and falls back at decay dB/s; the peak
   mark holds, then falls. Returns whether anything is still falling. */
static bool cui__meter_update(cui_ctx *ui, cui__meter_ch *chs, const float *levels, int n, float lo, float hi, float hold, float decay) {
    bool falling = false;
    float dt = CUI_MIN(ui->dt, 0.1f);
    for (int c = 0; c < n; c++) {
        cui__meter_ch *ch = &chs[c];
        float a = levels ? levels[c] : 0;
        float db = a > 0 && a == a ? 20.0f * log10f(a) : lo - 1;
        db = cui__clampf(db, lo - 1, hi);
        if (a >= 1.0f) ch->clip = true;
        ch->shown = CUI_MAX(db, ch->shown - decay * dt);
        if (db >= ch->peak) { ch->peak = db; ch->age = 0; }
        else {
            ch->age += dt;
            if (ch->age > hold) ch->peak = CUI_MAX(ch->shown, ch->peak - decay * dt);
        }
        if (ch->shown > db + 0.01f || ch->peak > ch->shown + 0.01f) falling = true;
    }
    return falling;
}

static void cui__meter_colors(cui_ctx *ui, cui__meter_paint *p) {
    p->bg = CUI_C(ui, SURFACE_MUTED);
    p->zone[0] = CUI_C(ui, SUCCESS); p->zone[1] = CUI_C(ui, WARNING); p->zone[2] = CUI_C(ui, DANGER);
    p->muted = CUI_C(ui, TEXT_MUTED);
}

/* ---- fader ---- */

typedef struct { cui_spring shown; float ref_px, ref_t, hover, active, focus; bool init, dragging, fine, minit; cui__meter_ch mch[CUI__METER_MAXCH]; } cui__fader_state;
typedef struct {
    float t, hover, active, focus, lw;
    bool horizontal;
    const float *ticks; const char **tick_text; int ntick;
    float lo, hi;
    cui_color track, thumb, edge, line, grip, muted;
    cui_id wheel; bool wheel_on;
    cui__meter_paint *meter; float mw;
} cui__fader_paint;

/* the thumb's travel inside the node: from a (t = 0) to b (t = 1) along the track */
static void cui__fader_travel(cui_rect r, bool horizontal, float lw, float *a, float *b, float *cross) {
    if (horizontal) { *a = r.x + CUI__FADER_TH * 0.5f; *b = r.x + r.w - CUI__FADER_TH * 0.5f; *cross = r.y + CUI__FADER_TW * 0.5f; }
    else { *a = r.y + r.h - CUI__FADER_TH * 0.5f; *b = r.y + CUI__FADER_TH * 0.5f; *cross = r.x + lw + CUI__FADER_TW * 0.5f; }
}

static void cui__paint_fader(cui_ctx *ui, cui_node *n, void *data) {
    cui__fader_paint *p = data;
    cui_rect r = n->rect;
    if (p->wheel_on) cui__paint_wheel_hit(ui, p->wheel, r);
    float a, b, cr;
    cui__fader_travel(r, p->horizontal, p->lw, &a, &b, &cr);
    float pos = a + (b - a) * p->t;
    float fx = cui__fs(ui, ui->theme.text_xs) * 0.92f;
    for (int k = 0; k < p->ntick; k++) {
        float tp = a + (b - a) * cui__dl_norm(p->ticks[k], p->lo, p->hi);
        if (p->horizontal) {
            float below = cr + CUI__FADER_TW * 0.5f + (p->meter ? p->mw + 4 : 0);
            cui_draw_rect(ui, (cui_rect){ tp - 0.5f, below - 2, 1, 4 }, CUI_CORNERS(0), p->line);
            cui__dl_text_at(ui, CUI_FONT_BODY, fx, tp, below + 9, 1, p->muted, p->tick_text[k]);
        } else {
            cui_draw_rect(ui, (cui_rect){ cr - CUI__FADER_TW * 0.5f - 2, tp - 0.5f, 6, 1 }, CUI_CORNERS(0), p->line);
            cui__dl_text_at(ui, CUI_FONT_BODY, fx, cr - CUI__FADER_TW * 0.5f - 5, tp, 2, p->muted, p->tick_text[k]);
        }
    }
    if (p->meter) {
        cui_rect mr = p->horizontal ? (cui_rect){ a, cr + CUI__FADER_TW * 0.5f + 2, b - a, p->mw } : (cui_rect){ cr + CUI__FADER_TW * 0.5f + 6, b, p->mw, a - b };
        cui__meter_bars(ui, p->meter, mr);
    }
    float gw = 4;
    cui_rect groove = p->horizontal ? (cui_rect){ r.x + 2, cr - gw * 0.5f, r.w - 4, gw } : (cui_rect){ cr - gw * 0.5f, r.y + 2, gw, r.h - 4 };
    cui_draw_rect(ui, groove, CUI_CORNERS(gw * 0.5f), p->track);
    cui_rect tr = p->horizontal ? (cui_rect){ pos - CUI__FADER_TH * 0.5f, cr - CUI__FADER_TW * 0.5f, CUI__FADER_TH, CUI__FADER_TW }
                                : (cui_rect){ cr - CUI__FADER_TW * 0.5f, pos - CUI__FADER_TH * 0.5f, CUI__FADER_TW, CUI__FADER_TH };
    bool dark = ui->mode == CUI_THEME_DARK;
    float sc = 1 + 0.04f * p->hover - 0.03f * p->active;
    cui_rect ts = { tr.x + tr.w * (1 - sc) * 0.5f, tr.y + tr.h * (1 - sc) * 0.5f, tr.w * sc, tr.h * sc };
    cui_draw_shadow(ui, ts, CUI_CORNERS(4), 2, 0, (cui_vec2){ 0, 1 }, cui__rgba(0, 0, 0, dark ? 90 : 38));
    cui_draw_shadow(ui, ts, CUI_CORNERS(4), 6, 0, (cui_vec2){ 0, 2 }, cui__rgba(0, 0, 0, dark ? 50 : 16));
    cui_draw_rect_border(ui, ts, CUI_CORNERS(4), p->thumb, p->edge, 1);
    /* the grip line across the thumb marks the exact value */
    if (p->horizontal) cui_draw_rect(ui, (cui_rect){ pos - 0.75f, ts.y + 5, 1.5f, ts.h - 10 }, CUI_CORNERS(0.75f), p->grip);
    else cui_draw_rect(ui, (cui_rect){ ts.x + 5, pos - 0.75f, ts.w - 10, 1.5f }, CUI_CORNERS(0.75f), p->grip);
    cui__draw_focus_ring(ui, ts, CUI_CORNERS(4), p->focus);
}

bool cui_fader_ex(cui_ctx *ui, const char *id_str, float *value, float min, float max, const cui_fader_opts *o) {
    cui_fader_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!value) return false;
    if (!(max > min)) max = min + 1;
    cui_id id = cui_make_id(ui, id_str ? id_str : "fader");
    cui_id fid = id ^ 0xFAD0u, wid = id ^ 0xFAD1u;
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, fid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui_pop_disabled(ui);
    cui__fader_state *st = cui_state(ui, id, sizeof *st);
    float old = *value, t = cui__dl_norm(*value, min, max);
    if (!st->init) { st->shown.x = t; st->init = true; }
    bool hz = o->horizontal;
    const char *fmt = o->format ? o->format : "%.1f";
    float fx = cui__fs(ui, ui->theme.text_xs) * 0.92f;
    int ntick = o->ticks ? CUI_CLAMP(o->tick_count, 0, 32) : 0;
    const char **tick_text = cui__frame_alloc(ui, sizeof(char *) * (size_t)(ntick > 0 ? ntick : 1));
    float lw = 0;
    for (int k = 0; k < ntick; k++) {
        tick_text[k] = cui__frame_printf(ui, "%g", (double)o->ticks[k]);
        lw = CUI_MAX(lw, cui__text_width(ui, CUI_FONT_BODY, fx, tick_text[k], -1, true));
    }
    if (ntick && !hz) lw = ceilf(lw) + 10;
    else lw = 0;
    float len = o->length > 0 ? o->length : 160;
    cui_rect pr = cui__prev_rect(ui, fid);
    float a, b, cr;
    cui__fader_travel(pr, hz, lw, &a, &b, &cr);
    float reset_t = cui__dl_reset_t(min, max, o->reset, false);
    bool fine = (ui->mods & CUI_MOD_SHIFT) != 0;
    if (r.active && ui->mdown[0] && !disabled && pr.w > 0 && fabsf(b - a) > 1) {
        float px = hz ? ui->mx : ui->my;
        if (ui->mpressed[0]) {
            /* a press on the thumb takes it where it is; on the track the thumb jumps there first */
            float pos = a + (b - a) * t;
            if (fabsf(px - pos) > CUI__FADER_TH * 0.5f + 2) { t = cui__clampf((px - a) / (b - a), 0, 1); st->shown.v = 0; }
            st->ref_px = px; st->ref_t = t; st->fine = fine;
        } else if (!st->dragging) { st->ref_px = px; st->ref_t = t; st->fine = fine; }
        else if (fine != st->fine) { st->ref_px = hz ? ui->pmx : ui->pmy; st->ref_t = t; st->fine = fine; }
        st->dragging = true;
        t = cui__clampf(st->ref_t + (px - st->ref_px) / (b - a) * (fine ? 0.1f : 1.0f), 0, 1);
    } else st->dragging = false;
    if (!disabled) t = cui__dl_input(ui, r, wid, t, min, max, o->step, reset_t);
    float v = cui__dl_value(t, min, max, o->step, false);
    *value = v;
    t = cui__dl_norm(v, min, max);
    if (st->dragging) { st->shown.x = t; st->shown.v = 0; }
    else cui__spring(ui, &st->shown, t, 0.26f, 0.0f);
    cui__tween(ui, &st->hover, (r.hovered || r.active) ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->active, r.active ? 1.0f : 0.0f, CUI__T_STATE);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    if ((r.hovered || r.active) && !disabled) cui__set_cursor(ui, hz ? CUI_CURSOR_RESIZE_EW : CUI_CURSOR_RESIZE_NS);

    const char *label = o->label ? o->label : id_str;
    float fv = cui__fs(ui, ui->theme.text_sm), fl = cui__fs(ui, ui->theme.text_xs);
    const char *vt = o->hide_value ? NULL : cui__dl_text(ui, fmt, o->unit, v);
    int col = cui__node_new(ui, id);
    cui_node *cn = &ui->nodes[col];
    cn->opacity = disabled ? CUI__DISABLED : 1.0f;
    cn->self_align = CUI__ALIGN_NOSTRETCH;
    /* a meter beside the track, on the fader's own dB scale */
    cui__meter_paint *mp = NULL;
    float mw = 0;
    if (o->meter && o->meter_channels > 0) {
        int mn = CUI_MIN(o->meter_channels, CUI__METER_MAXCH);
        float mlo = min < 0 ? min : -60, mhi = min < 0 ? max : 6;
        if (!st->minit) { for (int c = 0; c < CUI__METER_MAXCH; c++) st->mch[c].shown = st->mch[c].peak = mlo - 1; st->minit = true; }
        if (cui__meter_update(ui, st->mch, o->meter, mn, mlo, mhi, 1.5f, 20)) ui->animating = true;
        mp = cui__frame_alloc(ui, sizeof *mp);
        memset(mp, 0, sizeof *mp);
        memcpy(mp->ch, st->mch, sizeof mp->ch);
        mp->n = mn; mp->lo = mlo; mp->hi = mhi; mp->th = 4; mp->horizontal = hz;
        cui__meter_colors(ui, mp);
        mw = mn * 4.0f + (mn - 1) * 2.0f;
    }
    /* the scale on the left and room to balance it on the right, so the track sits on the center line */
    float node_w = hz ? len : lw + CUI__FADER_TW + CUI_MAX(lw, mp ? mw + 6 : 0);
    float node_h = hz ? CUI__FADER_TW + (ntick ? 16 : 0) + (mp ? mw + 4 : 0) : len;
    if (hz) {
        cn->gap = 6; cn->w = CUI_PX(len);
        cui__open(ui, col);
        int row = cui__node_new(ui, 0);
        ui->nodes[row].dir = CUI_ROW; ui->nodes[row].w = CUI_FILL; ui->nodes[row].gap = 8;
        cui__open(ui, row);
        if (label && cui__display_len(label)) {
            int lt = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_MEDIUM, fv, CUI_C(ui, FOREGROUND), false);
            ui->nodes[lt].line_height = 1.3f; ui->nodes[lt].w = CUI_FILL;
        }
        if (vt) {
            int vn = cui__text_node(ui, vt, -1, CUI_FONT_BODY, fv, CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[vn].line_height = 1.3f; ui->nodes[vn].flags |= CUI_NF_TABULAR;
        }
        cui__close(ui);
    } else {
        float w = node_w;
        if (vt) w = CUI_MAX(w, cui__dl_value_w(ui, fv, fmt, o->unit, min, max));
        cn->gap = 6; cn->align = CUI_ALIGN_CENTER; cn->w = CUI_PX(w);
        cui__open(ui, col);
        if (vt) cui__dl_caption(ui, vt, fv, CUI_C(ui, FOREGROUND), CUI_FONT_MEDIUM);
    }
    int f = cui__leaf(ui, fid, CUI_PX(node_w), CUI_PX(node_h));
    cui__mark_interactive(ui, f, CUI_IF_FOCUSABLE);
    cui__fader_paint *p = cui__frame_alloc(ui, sizeof *p);
    memset(p, 0, sizeof *p);
    p->t = st->shown.x; p->hover = st->hover; p->active = st->active; p->focus = st->focus; p->lw = lw;
    p->horizontal = hz; p->ticks = NULL; p->tick_text = tick_text; p->ntick = ntick;
    if (ntick) { float *tk = cui__frame_alloc(ui, sizeof(float) * (size_t)ntick); memcpy(tk, o->ticks, sizeof(float) * (size_t)ntick); p->ticks = tk; }
    p->lo = min; p->hi = max;
    p->track = cui_mix(CUI_C(ui, CONTROL_TRACK), CUI_C(ui, CONTROL_TRACK_HOVER), st->hover);
    p->thumb = CUI_C(ui, CONTROL_THUMB);
    p->edge = ui->mode == CUI_THEME_DARK ? cui__rgba(255, 255, 255, 24) : cui__rgba(0, 0, 0, 30);
    p->line = cui_alpha(CUI_C(ui, FOREGROUND), 0.55f);
    p->grip = cui__rgba(0, 0, 0, 115);   /* the thumb is light in both modes */
    p->muted = CUI_C(ui, TEXT_MUTED);
    p->wheel = wid; p->wheel_on = r.focused && !disabled;
    p->meter = mp; p->mw = mw;
    ui->nodes[f].paint = cui__paint_fader;
    ui->nodes[f].paint_data = p;
    if (!hz && label && cui__display_len(label)) cui__dl_caption(ui, label, fl, CUI_C(ui, TEXT_SECONDARY), CUI_FONT_BODY);
    cui__close(ui);
    ui->last_id = fid;
    return *value != old;
}

typedef struct { int n; float th, lw; bool horizontal, clip[CUI__METER_MAXCH]; cui_color on, off; } cui__clip_paint;

static void cui__paint_clip(cui_ctx *ui, cui_node *n, void *data) {
    cui__clip_paint *p = data;
    cui_rect r = n->rect;
    for (int c = 0; c < p->n; c++) {
        cui_rect q = p->horizontal ? (cui_rect){ r.x, r.y + c * (p->th + 2), r.w, p->th } : (cui_rect){ r.x + p->lw + c * (p->th + 2), r.y, p->th, r.h };
        cui_draw_rect(ui, q, CUI_CORNERS(CUI_MIN(1.5f, p->th * 0.5f)), p->clip[c] ? p->on : p->off);
    }
}

bool cui_level_meter_ex(cui_ctx *ui, const char *id_str, const float *levels, int channels, const cui_level_meter_opts *o) {
    cui_level_meter_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    channels = CUI_CLAMP(channels, 1, CUI__METER_MAXCH);
    cui_id id = cui_make_id(ui, id_str ? id_str : "meter");
    cui_id clip_id = id ^ 0xC11Bu;
    cui__meter_state *st = cui_state(ui, id, sizeof *st);
    float lo = o->min_db < 0 ? o->min_db : -60, hi = o->max_db != 0 ? o->max_db : 6;
    if (hi <= lo) hi = lo + 1;
    float hold = o->hold > 0 ? o->hold : o->hold < 0 ? 0 : 1.5f;
    float decay = o->decay > 0 ? o->decay : 20;
    if (!st->init) { for (int c = 0; c < CUI__METER_MAXCH; c++) st->ch[c].shown = st->ch[c].peak = lo - 1; st->init = true; }
    cui_interact cr = cui__interact(ui, clip_id, 0);
    bool cleared = false;
    if (cr.clicked) { for (int c = 0; c < CUI__METER_MAXCH; c++) st->ch[c].clip = false; cleared = true; }
    bool falling = cui__meter_update(ui, st->ch, levels, channels, lo, hi, hold, decay);
    if (falling) ui->animating = true;

    bool hz = o->horizontal;
    float len = o->length > 0 ? o->length : 160;
    float th = o->thickness > 0 ? o->thickness : 6;
    float lw = 0, fx = cui__fs(ui, ui->theme.text_xs) * 0.9f;
    if (o->scale && !hz) lw = ceilf(cui__text_width(ui, CUI_FONT_BODY, fx, "-60", -1, true)) + 9;
    float bars = channels * th + (channels - 1) * 2;
    bool any_clip = false;
    for (int c = 0; c < channels; c++) any_clip |= st->ch[c].clip;
    int box = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[box];
    bn->dir = hz ? CUI_ROW : CUI_COLUMN; bn->gap = 3; bn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, box);
    cui__clip_paint *cp = cui__frame_alloc(ui, sizeof *cp);
    memset(cp, 0, sizeof *cp);
    cp->n = channels; cp->th = th; cp->lw = lw; cp->horizontal = hz;
    for (int c = 0; c < channels; c++) cp->clip[c] = st->ch[c].clip;
    cp->on = CUI_C(ui, DANGER); cp->off = CUI_C(ui, SURFACE_MUTED);
    cui__meter_paint *p = cui__frame_alloc(ui, sizeof *p);
    memset(p, 0, sizeof *p);
    memcpy(p->ch, st->ch, sizeof p->ch);
    p->n = channels; p->lo = lo; p->hi = hi; p->th = th; p->lw = lw;
    p->horizontal = hz; p->scale = o->scale; p->segmented = o->segmented;
    cui__meter_colors(ui, p);
    /* the clip light sits at the loud end: above a vertical meter, right of a horizontal one */
    int clip = -1;
    if (!hz) clip = cui__leaf(ui, clip_id, CUI_PX(lw + bars), CUI_PX(4));
    int m = cui__leaf(ui, 0, hz ? CUI_PX(len) : CUI_PX(lw + bars), hz ? CUI_PX(bars + (o->scale ? 16 : 0)) : CUI_PX(len));
    ui->nodes[m].paint = cui__paint_meter;
    ui->nodes[m].paint_data = p;
    if (hz) { clip = cui__leaf(ui, clip_id, CUI_PX(4), CUI_PX(bars)); ui->nodes[clip].self_align = CUI_ALIGN_START; }
    ui->nodes[clip].paint = cui__paint_clip;
    ui->nodes[clip].paint_data = cp;
    if (any_clip) {
        cui__mark_interactive(ui, clip, 0);
        ui->last_id = clip_id;
        cui_tooltip(ui, "Clipped. Click to reset");
        if (cr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    }
    cui__close(ui);
    ui->last_id = id;
    return cleared;
}
