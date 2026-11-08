/* Tool panels: a property inspector and a log console. */
#include "cui_internal.h"

#define cui__fs cui__fsz

static cui_color cui__tools_accent(cui_ctx *ui) { return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT); }

/* ======================================================================== */
/* Property inspector                                                       */
/* ======================================================================== */

#define CUI__INSP_GAP 12.0f      /* between the label and the editor column */
#define CUI__INSP_ROW 36.0f      /* rows are as tall as the controls in them */
#define CUI__INSP_STACK 380.0f   /* narrower than this, rows stack */
#define CUI__INSP_SLOT 0x1D5EC7A1u

typedef struct { float ratio, press_x, start_w, hover; } cui__insp_state;

typedef struct cui__insp_build {
    cui_id id, resize_id;
    cui__insp_state *st;
    float label_w, width, height;
    bool stacked;            /* too narrow for two columns: labels sit above their editors */
    bool group_open;         /* a group body is open and must be closed */
    bool group_disabled;     /* it pushed a disabled scope (animating closed) */
    int groups;
} cui__insp_build;

typedef struct { cui__insp_build *b; uint64_t frame; } cui__insp_slot;

static cui__insp_build *cui__insp_current(cui_ctx *ui) {
    cui__insp_slot *s = cui_state(ui, CUI__INSP_SLOT, sizeof *s);
    return s->b && s->frame == ui->frame ? s->b : NULL;
}

typedef struct { float hover, active; cui_color line, on; } cui__insp_line_paint;
static void cui__paint_insp_line(cui_ctx *ui, cui_node *n, void *data) {
    cui__insp_line_paint *p = data;
    float a = CUI_MAX(p->hover, p->active);
    if (a <= 0.01f) return;
    cui_rect r = n->rect;
    cui_color c = cui_mix(p->line, p->on, p->active);
    cui_draw_rect(ui, (cui_rect){ r.x + r.w * 0.5f - 1, r.y + 4, 2, CUI_MAX(0, r.h - 8) }, CUI_CORNERS(1), cui_alpha(c, a));
}

bool cui_inspector_begin_ex(cui_ctx *ui, const char *id_str, const cui_inspector_opts *o) {
    cui_inspector_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__insp_state *st = cui_state(ui, id ^ 0x15EC0u, sizeof *st);
    cui_rect pr = cui__prev_rect(ui, id);
    cui__insp_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = id;
    b->resize_id = id ^ 0x1E5A7u;
    b->st = st;
    b->width = pr.w;
    b->height = pr.h;
    /* the label column keeps its share of the width; dragging the line between
     * the columns changes that share */
    float w = pr.w;
    if (st->ratio <= 0 && w > 0) st->ratio = o->label_width > 0 ? o->label_width / w : 0.38f;
    float lw = w > 0 ? st->ratio * w : (o->label_width > 0 ? o->label_width : 120);
    cui_interact rr = cui__interact(ui, b->resize_id, CUI_IF_DRAG);
    if (rr.hovered && ui->mpressed[0]) { st->press_x = ui->mx; st->start_w = lw; }
    if (rr.active && ui->mdown[0] && w > 0) lw = st->start_w + ui->mx - st->press_x;
    if (w > 0) {
        float lo = CUI_MIN(56.0f, w * 0.3f), hi = CUI_MAX(lo, w - 160);
        lw = cui__clampf(lw, lo, hi);
        if (rr.active) st->ratio = lw / w;   /* a narrow moment does not change the share */
    }
    b->stacked = w > 0 && w < CUI__INSP_STACK;
    if (rr.hovered || rr.active) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
    cui__tween(ui, &st->hover, (rr.hovered || rr.active) ? 1.0f : 0.0f, CUI__T_HOVER);
    b->label_w = roundf(lw);
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->gap = 0;
    cui__open(ui, outer);
    cui_push_id(ui, id_str);
    cui__insp_slot *s = cui_state(ui, CUI__INSP_SLOT, sizeof *s);
    s->b = b;
    s->frame = ui->frame;
    return true;
}

static void cui__insp_close_group(cui_ctx *ui, cui__insp_build *b) {
    if (!b->group_open) return;
    if (b->group_disabled) cui_pop_disabled(ui);
    cui_pop_id(ui);
    cui__close(ui);   /* content */
    cui__close(ui);   /* clip */
    cui__close(ui);   /* group */
    b->group_open = false;
    b->group_disabled = false;
}

typedef struct { cui_spring t; float hover, content_h; bool init; } cui__insp_group_state;

bool cui_inspector_group(cui_ctx *ui, const char *title, bool *open) {
    cui__insp_build *b = cui__insp_current(ui);
    if (b) cui__insp_close_group(ui, b);
    cui_id id = cui_make_id(ui, title) ^ 0x6A0B9u;
    cui__insp_group_state *st = cui_state(ui, id, sizeof *st);
    bool is_open = open ? *open : true;
    cui_interact r = cui__interact(ui, id, open ? CUI_IF_FOCUSABLE : 0);
    if (open && r.clicked) { *open = !*open; is_open = *open; }
    if (open && r.focus_visible) {
        /* arrows open and close, as in a tree */
        if (cui_key_pressed(ui, CUI_KEY_RIGHT) && !*open) { *open = true; is_open = true; }
        if (cui_key_pressed(ui, CUI_KEY_LEFT) && *open) { *open = false; is_open = false; }
    }
    if (!st->init) { st->t.x = is_open ? 1.0f : 0.0f; st->init = true; }
    cui__spring(ui, &st->t, is_open ? 1.0f : 0.0f, CUI__SPRING_EXPAND);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    float t = cui__clampf(st->t.x, 0, 1);
    bool first = b ? b->groups == 0 : ui->nodes[cui__current(ui)].child_count == 0;
    if (b) b->groups++;
    int wrap = cui__node_new(ui, 0);
    ui->nodes[wrap].w = CUI_FILL;
    ui->nodes[wrap].self_align = CUI_ALIGN_STRETCH;
    cui__open(ui, wrap);
    if (!first) {   /* hairlines between groups, none above the first */
        int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
        ui->nodes[line].bg = CUI_C(ui, BORDER_SUBTLE);
    }
    int head = cui__node_new(ui, id);
    cui_node *hn = &ui->nodes[head];
    hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER; hn->gap = 6;
    hn->w = CUI_FILL; hn->h = CUI_PX(40);
    hn->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    if (open) {
        cui__mark_interactive(ui, head, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    }
    cui__open(ui, head);
    if (open) cui__chevron_node(ui, (t - 1) * CUI_PI * 0.5f, 14, cui_mix(CUI_C(ui, TEXT_MUTED), CUI_C(ui, FOREGROUND), st->hover));
    int tt = cui__text_node(ui, title, cui__display_len(title), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[tt].line_height = 1.3f;
    ui->nodes[tt].w = CUI_FILL;
    cui__close(ui);
    cui__focus_ring(ui, head, r.focus_visible ? 1.0f : 0.0f);
    if (t <= 0.001f && !is_open) {
        cui__close(ui);   /* group */
        return false;
    }
    /* the body's height follows the spring; its natural height comes from the
     * previous frame */
    cui_id cid = id ^ 0xB0D7u;
    cui_rect cr = cui__prev_rect(ui, cid);
    if (cr.h > 0) st->content_h = cr.h;
    int clip = cui__node_new(ui, 0);
    cui_node *cn = &ui->nodes[clip];
    cn->w = CUI_FILL;
    /* clip only while moving, so focus rings at the edges show when it rests */
    if (t < 0.999f && st->content_h > 0) { cn->h = CUI_PX(st->content_h * t); cn->flags |= CUI_NF_CLIP; }
    cn->opacity = 0.25f + 0.75f * t;
    cui__open(ui, clip);
    int content = cui__node_new(ui, cid);
    cui_node *ct = &ui->nodes[content];
    ct->gap = 6; ct->pad[2] = 14;
    ct->w = CUI_FILL;
    cui__open(ui, content);
    cui_push_id(ui, title);
    bool closing = !is_open;
    if (closing) cui_push_disabled(ui, true);
    if (b) { b->group_open = true; b->group_disabled = closing; }
    else {
        /* outside an inspector nothing closes the group later, so the header
         * stands alone and the rows follow it */
        if (closing) cui_pop_disabled(ui);
        cui_pop_id(ui);
        cui__close(ui); cui__close(ui); cui__close(ui);
    }
    return true;
}

void cui_inspector_end(cui_ctx *ui) {
    cui__insp_build *b = cui__insp_current(ui);
    if (!b) return;
    cui__insp_close_group(ui, b);
    if (b->width > 0 && b->height > 0 && !b->stacked) {
        /* the line between the columns: drag it to resize the labels */
        int h = cui__absolute(ui, b->resize_id, b->label_w + CUI__INSP_GAP * 0.5f - 4, 0, 8, b->height);
        cui__mark_interactive(ui, h, CUI_IF_DRAG);
        cui__insp_line_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->hover = b->st->hover;
        p->active = ui->active == b->resize_id ? 1.0f : 0.0f;
        p->line = CUI_C(ui, BORDER_STRONG);
        p->on = cui__tools_accent(ui);
        ui->nodes[h].paint = cui__paint_insp_line;
        ui->nodes[h].paint_data = p;
    }
    cui_pop_id(ui);
    cui__close(ui);
    cui__insp_slot *s = cui_state(ui, CUI__INSP_SLOT, sizeof *s);
    s->b = NULL;
    ui->last_id = b->id;
}

/* ---- rows ---- */

typedef struct { float press_x, start; bool moved; } cui__prop_drag;

/* Opens [label][editor] and pushes the row's id scope. lid receives the
 * label's id (it can scrub number rows). */
static cui_id cui__prop_row_begin(cui_ctx *ui, const char *label, const cui_prop_opts *o, bool scrub_label) {
    cui__insp_build *b = cui__insp_current(ui);
    float lw = b ? b->label_w : 120;
    bool stacked = b && b->stacked;
    int row = cui__node_new(ui, 0);
    cui_node *rn = &ui->nodes[row];
    rn->w = CUI_FILL;
    if (stacked) { rn->dir = CUI_COLUMN; rn->gap = 6; rn->pad[2] = 4; }
    else { rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER; rn->gap = CUI__INSP_GAP; rn->min_h = CUI__INSP_ROW; }
    cui__open(ui, row);
    cui_push_id(ui, label);
    if (o->disabled) cui_push_disabled(ui, true);
    cui_id lid = cui_make_id(ui, "#label");
    int ln = cui__node_new(ui, lid);
    cui_node *lnn = &ui->nodes[ln];
    lnn->dir = CUI_ROW; lnn->align = CUI_ALIGN_CENTER;
    if (stacked) lnn->w = CUI_FILL;
    else { lnn->w = CUI_PX(lw); lnn->h = CUI_PX(CUI__INSP_ROW); lnn->flags |= CUI_NF_KEEP_W; }
    if (cui_is_disabled(ui)) lnn->opacity = CUI__DISABLED;
    if (scrub_label) cui__mark_interactive(ui, ln, 0);
    cui__open(ui, ln);
    int t = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), false);
    ui->nodes[t].line_height = 1.3f;
    cui__close(ui);
    if (o->tooltip && cui__rect_contains(cui__prev_rect(ui, lid), ui->mx, ui->my) && ui->mouse_inside)
        cui__tooltip_passive(ui, lid, o->tooltip);
    int ed = cui__node_new(ui, 0);
    cui_node *en = &ui->nodes[ed];
    en->dir = CUI_ROW; en->align = CUI_ALIGN_CENTER; en->gap = 6;
    en->w = CUI_FILL;
    cui__open(ui, ed);
    return lid;
}

static void cui__prop_row_end(cui_ctx *ui, const cui_prop_opts *o) {
    cui__close(ui);   /* editor */
    if (o->disabled) cui_pop_disabled(ui);
    cui_pop_id(ui);
    cui__close(ui);   /* row */
}

/* The label of a number row scrubs like the field: drag sideways. */
static bool cui__prop_label_scrub(cui_ctx *ui, cui_id lid, float *v, float min, float max, float step) {
    if (cui_is_disabled(ui)) return false;
    cui__prop_drag *dr = cui_state(ui, lid ^ 0xD4A6u, sizeof *dr);
    cui_interact r = cui__interact(ui, lid, CUI_IF_DRAG);
    float old = *v;
    if (r.hovered && ui->mpressed[0]) { dr->press_x = ui->mx; dr->start = *v; dr->moved = false; }
    if (r.active && ui->mdown[0]) {
        float dx = ui->mx - dr->press_x;
        if (fabsf(dx) > 3) dr->moved = true;
        if (dr->moved) {
            float per_px = step / ((ui->mods & CUI_MOD_SHIFT) ? 10.0f : 1.0f);
            float nv = roundf((dr->start + dx * per_px) / per_px) * per_px;
            *v = cui__clampf(nv, min, max);
        }
    }
    if (r.hovered || r.active) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
    return *v != old;
}

static void cui__prop_range(const cui_prop_opts *o, float def_step, float *min, float *max, float *step, bool *bounded) {
    *bounded = o->max > o->min;
    *min = *bounded ? o->min : -1e30f;
    *max = *bounded ? o->max : 1e30f;
    *step = o->step > 0 ? o->step : (*bounded ? (o->max - o->min) / 100.0f : def_step);
    if (*step <= 0) *step = def_step;
}

static const char *cui__prop_format(const cui_prop_opts *o, float step) {
    if (o->format) return o->format;
    if (step >= 0.999f) return "%.0f";
    if (step >= 0.0999f) return "%.1f";
    if (step >= 0.00999f) return "%.2f";
    return "%.3f";
}

/* After cui_scrub_ex: the field node it built, or -1 while it is being typed in. */
static int cui__prop_scrub_node(cui_ctx *ui, cui_id sid) {
    int last = ui->nodes[cui__current(ui)].last;
    return last >= 0 && ui->nodes[last].id == sid ? last : -1;
}

static bool cui__prop_scrub(cui_ctx *ui, const char *sid_str, float *v, float min, float max, float step, bool bounded,
                            const char *format, const char *suffix, cui_color tint, bool compact) {
    cui_scrub_opts so;
    memset(&so, 0, sizeof so);
    so.step = step;
    so.format = format;
    so.suffix = suffix;
    so.width = CUI_FILL;
    cui_id sid = cui_make_id(ui, sid_str);
    bool disabled = cui_is_disabled(ui);
    bool changed = cui_scrub_ex(ui, sid_str, v, min, max, &so);
    int n = cui__prop_scrub_node(ui, sid);
    if (n >= 0) {
        cui_node *bn = &ui->nodes[n];
        bn->min_w = 0;
        /* an unbounded value has no place in a range: drop the range line
         * (the focus ring, painted after the children, stays) */
        if (!bounded && !(bn->flags & CUI_NF_PAINT_AFTER)) bn->paint = NULL;
        if (compact) { bn->pad[1] = bn->pad[3] = 9; bn->gap = 4; }
        if (tint.a && bn->first >= 0 && (ui->nodes[bn->first].flags & CUI_NF_TEXT)) ui->nodes[bn->first].text_color = tint;
        if (disabled) bn->opacity = CUI__DISABLED;
    }
    return changed;
}

bool cui_prop_float_ex(cui_ctx *ui, const char *label, float *value, const cui_prop_opts *o) {
    cui_prop_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float min, max, step;
    bool bounded;
    cui__prop_range(o, 0.1f, &min, &max, &step, &bounded);
    float old = *value;
    cui_id lid = cui__prop_row_begin(ui, label, o, true);
    cui__prop_label_scrub(ui, lid, value, min, max, step);
    cui__prop_scrub(ui, "##v", value, min, max, step, bounded, cui__prop_format(o, step), o->suffix, CUI_TRANSPARENT, false);
    cui__prop_row_end(ui, o);
    return *value != old;
}

bool cui_prop_int_ex(cui_ctx *ui, const char *label, int *value, const cui_prop_opts *o) {
    cui_prop_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float min, max, step;
    bool bounded;
    cui__prop_range(o, 1.0f, &min, &max, &step, &bounded);
    step = CUI_MAX(1.0f, roundf(step));
    if (!bounded) { min = -2147483520.0f; max = 2147483520.0f; }   /* the int range a float holds */
    int old = *value;
    float fv = (float)*value;
    cui_id lid = cui__prop_row_begin(ui, label, o, true);
    cui__prop_label_scrub(ui, lid, &fv, min, max, step);
    cui__prop_scrub(ui, "##v", &fv, min, max, step, bounded, o->format ? o->format : "%.0f", o->suffix, CUI_TRANSPARENT, false);
    cui__prop_row_end(ui, o);
    if (fv != (float)old) *value = (int)lroundf(cui__clampf(fv, min, max));
    return *value != old;
}

bool cui_prop_text_ex(cui_ctx *ui, const char *label, char *buf, size_t cap, const cui_prop_opts *o) {
    cui_prop_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__prop_row_begin(ui, label, o, false);
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.width = CUI_FILL;
    io.placeholder = o->placeholder;
    io.suffix = o->suffix;
    int f = buf && cap > 0 ? cui_input_ex(ui, "##v", buf, cap, &io) : 0;
    cui__prop_row_end(ui, o);
    return (f & CUI_CHANGED) != 0;
}

bool cui_prop_bool_ex(cui_ctx *ui, const char *label, bool *value, const cui_prop_opts *o) {
    cui_prop_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__prop_row_begin(ui, label, o, false);
    cui_check_opts co;
    memset(&co, 0, sizeof co);
    bool changed = o->checkbox ? cui_checkbox_ex(ui, "##v", value, &co) : cui_switch_ex(ui, "##v", value, &co);
    cui__prop_row_end(ui, o);
    return changed;
}

bool cui_prop_enum_ex(cui_ctx *ui, const char *label, const char *const *items, int count, int *index, const cui_prop_opts *o) {
    cui_prop_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__prop_row_begin(ui, label, o, false);
    cui_select_opts so;
    memset(&so, 0, sizeof so);
    so.width = CUI_FILL;
    so.placeholder = o->placeholder;
    bool changed = cui_select_ex(ui, "##v", items, count, index, &so);
    cui__prop_row_end(ui, o);
    return changed;
}

bool cui_prop_color_ex(cui_ctx *ui, const char *label, cui_color *value, const cui_prop_opts *o) {
    cui_prop_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__prop_row_begin(ui, label, o, false);
    cui_id cid = cui_make_id(ui, "##v");
    bool changed = cui_color_picker(ui, "##v", value);
    int n = ui->nodes[cui__current(ui)].last;
    if (n >= 0 && ui->nodes[n].id == cid) {
        cui_node *cn = &ui->nodes[n];
        cn->w = CUI_FILL;   /* the trigger lines up with the fields above and below */
        if (cui_is_disabled(ui)) cn->opacity = CUI__DISABLED;
        cui__focus_ring(ui, n, ui->focus == cid && ui->focus_visible ? 1.0f : 0.0f);
    }
    cui__prop_row_end(ui, o);
    return changed;
}

bool cui_prop_vec_ex(cui_ctx *ui, const char *label, float *value, int components, const cui_prop_opts *o) {
    static const char *const axes[4] = { "X##0", "Y##1", "Z##2", "W##3" };
    cui_prop_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (components < 1) components = 1;
    if (components > 4) components = 4;
    float min, max, step;
    bool bounded;
    cui__prop_range(o, 0.1f, &min, &max, &step, &bounded);
    const char *fmt = cui__prop_format(o, step);
    /* axis letters carry the usual axis hues, from the theme's tones */
    cui_color tints[4] = { ui->theme.tone_rose, ui->theme.tone_green, ui->theme.tone_sky, ui->theme.tone_violet };
    bool changed = false;
    cui__prop_row_begin(ui, label, o, false);
    for (int k = 0; k < components; k++)
        changed |= cui__prop_scrub(ui, axes[k], &value[k], min, max, step, bounded, fmt, o->suffix, tints[k], true);
    cui__prop_row_end(ui, o);
    return changed;
}

/* ======================================================================== */
/* Log buffer                                                               */
/* ======================================================================== */

void cui_log_init(cui_log_buffer *b, cui_log_line *lines, int line_cap, char *text, size_t text_cap) {
    if (!b) return;
    memset(b, 0, sizeof *b);
    if (!lines || line_cap <= 0 || !text || text_cap == 0) return;   /* nothing can be stored */
    b->lines = lines;
    b->line_cap = line_cap;
    b->text = text;
    b->text_cap = text_cap > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)text_cap;
}

static void cui__log_drop_oldest(cui_log_buffer *b) {
    cui_log_line *l = &b->lines[b->first];
    if (l->level < CUI_LOG_LEVEL_COUNT && b->level_count[l->level] > 0) b->level_count[l->level]--;
    b->first = (b->first + 1) % b->line_cap;
    b->count--;
}

/* Room for a line of len bytes plus its terminator. Every line's text is
 * contiguous; when it does not fit before the end it starts over at 0.
 * Lines whose text the new one overwrites are dropped, oldest first: in age
 * order the stored text runs from the oldest line's offset around to
 * text_end, so the oldest line is always the next one in the way. */
static uint32_t cui__log_reserve(cui_log_buffer *b, uint32_t len) {
    uint32_t need = len + 1;
    if (b->count >= b->line_cap) cui__log_drop_oldest(b);
    uint32_t start = b->text_end;
    if (need > b->text_cap - start) {
        /* the tail past text_end is abandoned; any lines there are the oldest */
        while (b->count > 0 && b->lines[b->first].offset >= start) cui__log_drop_oldest(b);
        start = 0;
    }
    while (b->count > 0) {
        const cui_log_line *o = &b->lines[b->first];
        if (o->offset < start + need && o->offset + o->len + 1 > start) cui__log_drop_oldest(b);
        else break;
    }
    b->text_end = start + need;
    return start;
}

static void cui__log_commit(cui_log_buffer *b, cui_log_level level, double time, uint32_t off, uint32_t len) {
    char *s = b->text + off;
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) len--;
    for (uint32_t i = 0; i < len; i++) if ((unsigned char)s[i] < 0x20 || s[i] == 0x7F) s[i] = ' ';
    s[len] = 0;
    if ((unsigned)level >= CUI_LOG_LEVEL_COUNT) level = CUI_LOG_INFO;
    cui_log_line *l = &b->lines[(b->first + b->count) % b->line_cap];
    l->offset = off;
    l->len = len;
    l->time = time;
    l->level = (uint8_t)level;
    b->count++;
    b->total++;
    b->level_count[level]++;
}

void cui_log_append_text(cui_log_buffer *b, cui_log_level level, double time, const char *text, int len) {
    if (!b || !b->lines || b->text_cap == 0) return;
    if (!text) text = "";
    size_t n = len < 0 ? strlen(text) : (size_t)len;
    if (n > b->text_cap - 1) n = b->text_cap - 1;   /* a line longer than the whole buffer is cut */
    uint32_t off = cui__log_reserve(b, (uint32_t)n);
    memmove(b->text + off, text, n);   /* the source may be an older line of this buffer */
    cui__log_commit(b, level, time, off, (uint32_t)n);
}

void cui_log_appendv(cui_log_buffer *b, cui_log_level level, const char *fmt, va_list ap) {
    if (!b || !b->lines || b->text_cap == 0 || !fmt) return;
    char small[512];
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(small, sizeof small, fmt, aq);
    va_end(aq);
    if (n < 0) return;
    if ((size_t)n < sizeof small) { cui_log_append_text(b, level, b->now, small, n); return; }
    /* long lines are formatted straight into the ring, cut to fit */
    uint32_t len = (uint32_t)n > b->text_cap - 1 ? b->text_cap - 1 : (uint32_t)n;
    uint32_t off = cui__log_reserve(b, len);
    va_copy(aq, ap);
    vsnprintf(b->text + off, (size_t)len + 1, fmt, aq);
    va_end(aq);
    cui__log_commit(b, level, b->now, off, len);
}

void cui_log_append(cui_log_buffer *b, cui_log_level level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    cui_log_appendv(b, level, fmt, ap);
    va_end(ap);
}

void cui_log_clear(cui_log_buffer *b) {
    if (!b) return;
    b->first = 0;
    b->count = 0;
    b->text_end = 0;
    memset(b->level_count, 0, sizeof b->level_count);
}

const cui_log_line *cui_log_get(const cui_log_buffer *b, int i, const char **text) {
    if (!b || i < 0 || i >= b->count) { if (text) *text = NULL; return NULL; }
    const cui_log_line *l = &b->lines[(b->first + i) % b->line_cap];
    if (text) *text = b->text + l->offset;
    return l;
}

/* line number (total - count .. total - 1) to its record */
static const cui_log_line *cui__log_at(const cui_log_buffer *b, uint64_t seq) {
    uint64_t oldest = b->total - (uint64_t)b->count;
    if (seq < oldest || seq >= b->total) return NULL;
    return &b->lines[(b->first + (int)(seq - oldest)) % b->line_cap];
}

/* ======================================================================== */
/* Log view                                                                 */
/* ======================================================================== */

#define CUI__LOG_DRAW_MAX 1024   /* bytes of a line that are measured and drawn; copy gets them all */

static const char *const cui__log_tags[CUI_LOG_LEVEL_COUNT] = { "trace", "debug", "info", "warn", "error" };

typedef struct {
    bool init, follow, dragging;
    bool levels[CUI_LOG_LEVEL_COUNT];
    char search[128];
    /* the lines that pass the filter, as line numbers in ascending order */
    const cui_log_buffer *buf;
    uint64_t scanned;          /* next line number to look at */
    unsigned mask;
    char applied[128];
    uint64_t *rows; int nrows, cap;
    /* selection and keyboard cursor, as line numbers */
    bool has_sel, has_cursor;
    uint64_t anchor, cursor;
    float max_w, hover;
    double copied_at;
} cui__logv_state;

static void cui__logv_dtor(cui_ctx *ui, void *data) {
    cui__logv_state *s = data;
    cui__free(ui, s->rows);
    s->rows = NULL;
    s->nrows = s->cap = 0;
}

static int cui__lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* case-insensitive (ASCII) search; the index of the first match or -1 */
static int cui__ci_find(const char *h, int hn, const char *q, int qn) {
    if (qn <= 0) return 0;
    for (int i = 0; i + qn <= hn; i++) {
        int k = 0;
        while (k < qn && cui__lower((unsigned char)h[i + k]) == cui__lower((unsigned char)q[k])) k++;
        if (k == qn) return i;
    }
    return -1;
}

/* Brings the filtered index up to date: drops lines that left the buffer,
 * scans new ones. Returns how many shown rows were dropped from the top. */
static int cui__logv_refresh(cui_ctx *ui, cui__logv_state *s, const cui_log_buffer *b) {
    unsigned mask = 0;
    for (int k = 0; k < CUI_LOG_LEVEL_COUNT; k++) if (s->levels[k]) mask |= 1u << k;
    uint64_t oldest = b->total - (uint64_t)b->count;
    int dropped = 0;
    if (s->buf != b || s->mask != mask || strcmp(s->applied, s->search) != 0 || s->scanned > b->total) {
        s->buf = b;
        s->mask = mask;
        memcpy(s->applied, s->search, sizeof s->applied);
        s->nrows = 0;
        s->scanned = oldest;
        s->max_w = 0;
    } else {
        int k = 0;
        while (k < s->nrows && s->rows[k] < oldest) k++;
        if (k > 0) {
            memmove(s->rows, s->rows + k, sizeof(uint64_t) * (size_t)(s->nrows - k));
            s->nrows -= k;
            dropped = k;
        }
    }
    if (s->scanned < oldest) s->scanned = oldest;
    int qn = (int)strlen(s->applied);
    for (uint64_t seq = s->scanned; seq < b->total; seq++) {
        const cui_log_line *l = cui__log_at(b, seq);
        if (!l || !(mask & (1u << l->level))) continue;
        if (qn && cui__ci_find(b->text + l->offset, (int)l->len, s->applied, qn) < 0) continue;
        if (s->nrows >= s->cap) {
            /* at most one entry per line the buffer can hold */
            int ncap = s->cap ? s->cap * 2 : 256;
            if (ncap > b->line_cap) ncap = b->line_cap;
            if (ncap <= s->nrows) ncap = s->nrows + 1;
            s->rows = cui__realloc(ui, s->rows, sizeof(uint64_t) * (size_t)ncap);
            s->cap = ncap;
        }
        s->rows[s->nrows++] = seq;
    }
    s->scanned = b->total;
    return dropped;
}

/* first shown row at or after line number seq */
static int cui__logv_lower(const cui__logv_state *s, uint64_t seq) {
    int lo = 0, hi = s->nrows;
    while (lo < hi) { int m = (lo + hi) / 2; if (s->rows[m] < seq) lo = m + 1; else hi = m; }
    return lo;
}

static void cui__log_time(char *out, size_t cap, double t) {
    if (!(t >= 0) || t > 1e12) t = 0;   /* also NaN; unsigned keeps every field in range */
    unsigned long long ms = (unsigned long long)(t * 1000.0 + 0.5);
    unsigned long long sec = ms / 1000;
    snprintf(out, cap, "%02u:%02u:%02u.%03u", (unsigned)((sec / 3600) % 24), (unsigned)((sec / 60) % 60), (unsigned)(sec % 60), (unsigned)(ms % 1000));
}

/* Copies the selected shown lines (or every shown line) as text. */
static void cui__logv_copy(cui_ctx *ui, const cui__logv_state *s, const cui_log_buffer *b, bool timestamps) {
    if (!ui->platform.set_clipboard || s->nrows == 0) return;
    uint64_t lo = 0, hi = UINT64_MAX;
    if (s->has_sel) { lo = CUI_MIN(s->anchor, s->cursor); hi = CUI_MAX(s->anchor, s->cursor); }
    int a = cui__logv_lower(s, lo), e = s->nrows;
    size_t total = 1;
    for (int i = a; i < e && s->rows[i] <= hi; i++) {
        const cui_log_line *l = cui__log_at(b, s->rows[i]);
        if (l) total += l->len + 20;
    }
    char *out = cui__alloc(ui, total);
    size_t n = 0;
    for (int i = a; i < e && s->rows[i] <= hi; i++) {
        const cui_log_line *l = cui__log_at(b, s->rows[i]);
        if (!l) continue;
        char head[24];
        int hn;
        if (timestamps) {
            char tm[16];
            cui__log_time(tm, sizeof tm, l->time);
            hn = snprintf(head, sizeof head, "%s %-5s ", tm, cui__log_tags[l->level]);
        } else hn = snprintf(head, sizeof head, "%-5s ", cui__log_tags[l->level]);
        if (hn < 0) hn = 0;
        if (n + (size_t)hn + l->len + 2 > total) break;
        memcpy(out + n, head, (size_t)hn); n += (size_t)hn;
        memcpy(out + n, b->text + l->offset, l->len); n += l->len;
        out[n++] = '\n';
    }
    if (n > 0) n--;   /* no newline after the last line */
    out[n] = 0;
    ui->platform.set_clipboard(ui->platform.user, out);
    cui__free(ui, out);
}

typedef struct {
    float size, row_h, x_time, x_tag, x_text;
    bool timestamps;
    const char *query; int qn;
    cui_color text[CUI_LOG_LEVEL_COUNT], tag[CUI_LOG_LEVEL_COUNT], tint[CUI_LOG_LEVEL_COUNT];
    cui_color muted, sel, hover, match, bar;
} cui__logv_common;

typedef struct {
    const cui__logv_common *c;
    const char *text; int len, more;
    char time[16];
    uint8_t level;
    bool selected, cursor;
    float hover;
} cui__logv_row;

static void cui__paint_log_row(cui_ctx *ui, cui_node *n, void *data) {
    cui__logv_row *p = data;
    const cui__logv_common *c = p->c;
    cui_rect r = n->rect;
    if (p->selected) cui_draw_rect(ui, r, CUI_CORNERS(0), c->sel);
    else {
        if (c->tint[p->level].a) cui_draw_rect(ui, r, CUI_CORNERS(0), c->tint[p->level]);
        if (p->hover > 0) cui_draw_rect(ui, r, CUI_CORNERS(0), cui_alpha(c->hover, p->hover));
    }
    if (p->cursor) cui_draw_rect(ui, (cui_rect){ r.x, r.y, 2, r.h }, CUI_CORNERS(0), c->bar);
    float fh = cui__font_line_height(ui, CUI_FONT_MONO, c->size);
    float ty = r.y + (r.h - fh) * 0.5f;
    if (c->timestamps) cui__draw_text_line(ui, CUI_FONT_MONO, c->size, r.x + c->x_time, ty, c->muted, p->time, -1, true);
    cui__draw_text_line(ui, CUI_FONT_MONO, c->size, r.x + c->x_tag, ty, c->tag[p->level], cui__log_tags[p->level], -1, true);
    float x = r.x + c->x_text;
    if (c->qn > 0) {
        /* every match gets a quiet mark behind it */
        int from = 0;
        while (from < p->len) {
            int at = cui__ci_find(p->text + from, p->len - from, c->query, c->qn);
            if (at < 0) break;
            at += from;
            int end = CUI_MIN(p->len, at + c->qn);
            float x0 = cui__text_width(ui, CUI_FONT_MONO, c->size, p->text, at, true);
            float x1 = cui__text_width(ui, CUI_FONT_MONO, c->size, p->text, end, true);
            cui_draw_rect(ui, (cui_rect){ x + x0 - 1, r.y + 3, x1 - x0 + 2, r.h - 6 }, CUI_CORNERS(3), c->match);
            from = end > at ? end : at + 1;
        }
    }
    cui__draw_text_line(ui, CUI_FONT_MONO, c->size, x, ty, c->text[p->level], p->text, p->len, true);
    if (p->more > 0) {
        float w = cui__text_width(ui, CUI_FONT_MONO, c->size, p->text, p->len, true);
        const char *m = cui__frame_printf(ui, "  ... %d more bytes", p->more);
        cui__draw_text_line(ui, CUI_FONT_MONO, c->size, x + w, ty, c->muted, m, -1, true);
    }
}

/* cut at a character boundary */
static int cui__utf8_cut(const char *s, int len, int max) {
    if (len <= max) return len;
    int n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

void cui_log_view_ex(cui_ctx *ui, const char *id_str, cui_log_buffer *buf, const cui_log_view_opts *o) {
    cui_log_view_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    static const cui_log_buffer empty_buf = {0};   /* stands in for a NULL buffer: never written */
    const cui_log_buffer *b = buf ? buf : &empty_buf;
    cui_id id = cui_make_id(ui, id_str);
    cui_id lid = id ^ 0x11575u, sid = id ^ 0x5C2011u;
    cui__logv_state *s = cui_state(ui, id ^ 0x106Bu, sizeof *s);
    cui__state_set_dtor(ui, id ^ 0x106Bu, cui__logv_dtor);
    if (!s->init) {
        for (int k = 0; k < CUI_LOG_LEVEL_COUNT; k++) s->levels[k] = true;
        s->follow = true;
        s->init = true;
    }
    float size = cui__fs(ui, o->size > 0 ? o->size : 12);
    float row_h = roundf(size * 1.8f);
    float pad_y = 6;
    cui_scroll_state *ss = cui_state(ui, sid, sizeof *ss);
    float view_h = ss->view_h > 0 ? ss->view_h : 300;

    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->gap = 8;
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->h = o->height < 0 ? CUI_FILL : CUI_PX(o->height > 0 ? o->height : 320);
    cui__open(ui, outer);
    cui_push_id(ui, id_str);

    /* ---- toolbar ---- */
    bool copy_now = false;
    if (!o->no_toolbar) {
        cui_box tb;
        memset(&tb, 0, sizeof tb);
        tb.dir = CUI_ROW; tb.align = CUI_ALIGN_CENTER; tb.gap = 8; tb.wrap = true;
        tb.width = CUI_FILL;
        cui_box_begin(ui, &tb);
        /* narrow (a phone): initials, so the counts stay readable instead of
         * every label being cut to "Tra..." */
        cui_rect opr = cui__prev_rect(ui, id);
        bool narrow = opr.w > 0 && opr.w < 460;
        const char *labels[CUI_LOG_LEVEL_COUNT] = { "Trace", "Debug", "Info", "Warn", "Error" };
        if (narrow) { labels[0] = "T"; labels[1] = "D"; labels[2] = "I"; labels[3] = "W"; labels[4] = "E"; }
        if (b->level_count[CUI_LOG_WARN] > 0) labels[CUI_LOG_WARN] = cui__frame_printf(ui, "%s %d", labels[CUI_LOG_WARN], b->level_count[CUI_LOG_WARN]);
        if (b->level_count[CUI_LOG_ERROR] > 0) labels[CUI_LOG_ERROR] = cui__frame_printf(ui, "%s %d", labels[CUI_LOG_ERROR], b->level_count[CUI_LOG_ERROR]);
        cui_toggle_group_opts go;
        memset(&go, 0, sizeof go);
        go.size = CUI_SIZE_SM;
        cui_toggle_group_ex(ui, "levels", labels, CUI_LOG_LEVEL_COUNT, s->levels, &go);
        cui_input_opts io;
        memset(&io, 0, sizeof io);
        io.size = CUI_SIZE_SM;
        io.width = CUI_FILL;
        io.placeholder = "Filter lines";
        if (s->search[0] && s->applied[0] && !strcmp(s->search, s->applied)) io.suffix = cui__frame_printf(ui, "%d found", s->nrows);
        cui_search_ex(ui, "search", s->search, sizeof s->search, &io);
        ui->nodes[ui->last_node].min_w = 140;
        /* the actions stay together when the toolbar wraps */
        cui_box ab;
        memset(&ab, 0, sizeof ab);
        ab.dir = CUI_ROW; ab.align = CUI_ALIGN_CENTER; ab.gap = 4;
        cui_box_begin(ui, &ab);
        bool follow_was = s->follow;
        static const char *const follow_label[1] = { "Follow" };
        static const char *const follow_icon[1] = { CUI_ICON_ARROW_DOWN };
        go.icons = follow_icon;
        cui_toggle_group_ex(ui, "follow", follow_label, 1, &s->follow, &go);
        if (s->follow && !follow_was) ss->ty = 1e9f;   /* clamped to the bottom next frame */
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM;
        bool copied = s->copied_at > 0 && ui->time - s->copied_at < 1.2;
        if (copied) cui__wakeup_after(ui, (float)(1.2 - (ui->time - s->copied_at)) + 0.01f);
        bo.tooltip = "Copy lines";   /* the selection, or every line shown */
        bo.disabled = s->nrows == 0;
        if (cui_icon_button_ex(ui, CUI_ICON_COPY, &bo)) copy_now = true;
        if (copied) {
            /* confirm with a green check in place, keeping the button (and its focus) */
            cui_node *bn = &ui->nodes[ui->last_node];
            for (int k = bn->first; k >= 0; k = ui->nodes[k].next)
                if (ui->nodes[k].font == CUI_FONT_ICONS) {
                    ui->nodes[k].text = CUI_ICON_CHECK;
                    ui->nodes[k].text_len = (int)strlen(CUI_ICON_CHECK);
                    ui->nodes[k].text_color = CUI_C(ui, SUCCESS);
                }
        }
        bo.tooltip = "Clear log";
        bo.disabled = b->count == 0 || !buf;
        if (cui_icon_button_ex(ui, CUI_ICON_TRASH_2, &bo) && buf) {
            cui_log_clear(buf);
            s->has_sel = s->has_cursor = false;
        }
        cui_box_end(ui);
        cui_box_end(ui);
    }

    /* ---- filter ---- */
    int dropped = cui__logv_refresh(ui, s, b);
    if (s->has_sel && s->nrows > 0 && CUI_MAX(s->anchor, s->cursor) < s->rows[0]) s->has_sel = false;
    if (dropped > 0 && !s->follow) {
        /* lines left the top of the buffer: keep the ones in view still */
        ss->y = CUI_MAX(0, ss->y - dropped * row_h);
        ss->ty = CUI_MAX(0, ss->ty - dropped * row_h);
    }

    /* ---- input on the list ---- */
    cui_interact lr = cui__interact(ui, lid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui_rect cr = cui__prev_rect(ui, lid);
    int hover_row = -1;
    if (cr.h > 0 && (lr.hovered || lr.active)) {
        float fy = (ui->my - cr.y - pad_y) / row_h;
        int ri = (int)floorf(fy);
        if (fy >= 0 && ri < s->nrows) hover_row = ri;
        if (lr.active && s->nrows > 0) {   /* dragging past the ends picks the end rows */
            if (ri < 0) ri = 0;
            if (ri >= s->nrows) ri = s->nrows - 1;
        }
        if (lr.hovered && ui->mpressed[0]) {
            if (hover_row >= 0) {
                uint64_t seq = s->rows[hover_row];
                if ((ui->mods & CUI_MOD_SHIFT) && s->has_cursor) { if (!s->has_sel) s->anchor = s->cursor; }
                else s->anchor = seq;
                s->cursor = seq;
                s->has_sel = s->has_cursor = true;
                s->dragging = true;
            } else s->has_sel = false;
        }
        if (s->dragging && lr.active && ui->mdown[0] && s->nrows > 0 && ri >= 0 && ri < s->nrows && s->rows[ri] != s->cursor) {
            s->cursor = s->rows[ri];
            s->has_sel = true;
        }
    }
    if (!ui->mdown[0]) s->dragging = false;
    float wheel = ui->scroll_target == sid ? ui->scroll_dy : 0;
    bool key_up = false, key_end = false;
    if (ui->focus == lid && s->nrows > 0) {
        int ci = s->has_cursor ? cui__logv_lower(s, s->cursor) : s->nrows;
        if (ci >= s->nrows) ci = s->nrows - 1;
        int page = CUI_MAX(1, (int)(view_h / row_h) - 1);
        int to = ci;
        bool moved = false;
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) { to++; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_UP)) { to--; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) { to += page; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) { to -= page; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_HOME)) { to = 0; moved = true; }
        if (cui_key_pressed(ui, CUI_KEY_END)) { to = s->nrows - 1; moved = true; key_end = true; }
        if (moved) {
            to = CUI_CLAMP(to, 0, s->nrows - 1);
            uint64_t prev = s->has_cursor ? s->cursor : s->rows[to];
            s->cursor = s->rows[to];
            if (ui->mods & CUI_MOD_SHIFT) { if (!s->has_sel) s->anchor = prev; }
            else s->anchor = s->cursor;
            s->has_sel = s->has_cursor = true;
            if (to < s->nrows - 1) key_up = true;
            /* keep the cursor row in view */
            float top = pad_y + to * row_h, bottom = top + row_h;
            if (top - pad_y < ss->ty) ss->ty = top - pad_y;
            if (bottom + pad_y > ss->ty + view_h) ss->ty = bottom + pad_y - view_h;
            ui->animating = true;
        }
        if ((ui->mods & CUI_MOD_PRIMARY) && cui_key_pressed(ui, CUI_KEY_A)) {
            s->anchor = s->rows[0];
            s->cursor = s->rows[s->nrows - 1];
            s->has_sel = s->has_cursor = true;
        }
        if ((ui->mods & CUI_MOD_PRIMARY) && cui_key_pressed(ui, CUI_KEY_C)) copy_now = true;
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE) && s->has_sel) { s->has_sel = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
    }
    if (copy_now) { cui__logv_copy(ui, s, b, o->timestamps); s->copied_at = ui->time; }
    cui__tween(ui, &s->hover, hover_row >= 0 && !lr.active ? 1.0f : 0.0f, CUI__T_HIGHLIGHT);

    /* ---- the list ---- */
    float total_h = s->nrows * row_h + pad_y * 2;
    int frame = cui__node_new(ui, 0);
    cui_node *fn = &ui->nodes[frame];
    fn->w = CUI_FILL; fn->h = CUI_FILL; fn->min_h = 80;
    fn->radius = CUI_CORNERS(ui->theme.radius_control);
    fn->border = 1; fn->border_color = CUI_C(ui, BORDER);
    cui_color list_bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, BACKGROUND), 0.4f) : CUI_C(ui, SURFACE_MUTED);
    fn->bg = list_bg;   /* the code block's surface */
    fn->flags |= CUI_NF_CLIP;
    cui__open(ui, frame);
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL; sb.height = CUI_FILL;
    int scroll = cui__scroll_begin_ex(ui, sid, &sb, true);
    /* follow: scrolling up turns it off, coming back to the bottom turns it on */
    float max_y = CUI_MAX(0, ss->content_h - ss->view_h);
    if (s->follow && (wheel > 0 || key_up || (ss->drag_y && ss->ty < max_y - 1))) s->follow = false;
    else if (!s->follow && (wheel < 0 || ss->drag_y || key_end) && ss->ty >= max_y - 1) s->follow = true;
    if (s->follow) {
        float bottom = CUI_MAX(0, total_h - view_h);
        if (ss->ty != bottom) { ss->ty = bottom; ui->animating = true; }
    }
    float y = ui->nodes[scroll].scroll_y;
    int first = (int)floorf((y - pad_y) / row_h) - 2;
    int last = (int)ceilf((y + view_h) / row_h) + 2;
    if (first < 0) first = 0;
    if (last > s->nrows) last = s->nrows;
    if (first > last) first = last;

    cui__logv_common *c = cui__frame_alloc(ui, sizeof *c);
    memset(c, 0, sizeof *c);
    c->size = size; c->row_h = row_h; c->timestamps = o->timestamps;
    float pad_x = 12;
    float time_w = cui__text_width(ui, CUI_FONT_MONO, size, "00:00:00.000", 12, true);
    float tag_w = cui__text_width(ui, CUI_FONT_MONO, size, "error", 5, true);
    c->x_time = pad_x;
    c->x_tag = o->timestamps ? pad_x + time_w + 14 : pad_x;
    c->x_text = c->x_tag + tag_w + 14;
    c->query = s->applied[0] ? cui__frame_strdup(ui, s->applied, -1) : NULL;
    c->qn = c->query ? (int)strlen(c->query) : 0;
    bool dark = ui->mode == CUI_THEME_DARK;
    cui_color fg = CUI_C(ui, FOREGROUND);
    c->text[CUI_LOG_TRACE] = CUI_C(ui, TEXT_MUTED);
    c->text[CUI_LOG_DEBUG] = CUI_C(ui, TEXT_SECONDARY);
    c->text[CUI_LOG_INFO] = fg;
    c->text[CUI_LOG_WARN] = cui_mix(fg, CUI_C(ui, WARNING), dark ? 0.45f : 0.35f);
    c->text[CUI_LOG_ERROR] = cui_mix(fg, CUI_C(ui, DANGER), dark ? 0.5f : 0.4f);
    c->tag[CUI_LOG_TRACE] = CUI_C(ui, TEXT_MUTED);
    c->tag[CUI_LOG_DEBUG] = CUI_C(ui, TEXT_MUTED);
    c->tag[CUI_LOG_INFO] = CUI_C(ui, INFO);
    c->tag[CUI_LOG_WARN] = CUI_C(ui, WARNING);
    c->tag[CUI_LOG_ERROR] = CUI_C(ui, DANGER);
    c->tint[CUI_LOG_WARN] = cui_alpha(CUI_C(ui, WARNING), dark ? 0.10f : 0.08f);
    c->tint[CUI_LOG_ERROR] = cui_alpha(CUI_C(ui, DANGER), dark ? 0.12f : 0.07f);
    c->muted = CUI_C(ui, TEXT_MUTED);
    c->sel = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, ACTIVE) : cui_mix(list_bg, CUI_C(ui, ACCENT), dark ? 0.18f : 0.11f);
    c->hover = CUI_C(ui, HOVER);
    c->match = cui_alpha(cui__tools_accent(ui), dark ? 0.32f : 0.20f);
    c->bar = cui__tools_accent(ui);

    int content = cui__node_new(ui, lid);
    cui_node *cn = &ui->nodes[content];
    cn->w = CUI_FILL;
    cn->h = CUI_PX(CUI_MAX(total_h, view_h));
    cn->gap = 0;
    cui__mark_interactive(ui, content, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui__open(ui, content);
    if (s->nrows == 0) {
        int e = cui__node_new(ui, 0);
        ui->nodes[e].w = CUI_FILL; ui->nodes[e].h = CUI_FILL;
        ui->nodes[e].align = CUI_ALIGN_CENTER; ui->nodes[e].justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, e);
        const char *msg = b->count == 0 ? "No log lines yet" : "No lines match the filter";
        cui__text_node(ui, msg, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
        cui__close(ui);
    } else {
        cui__leaf(ui, 0, CUI_FILL, CUI_PX(pad_y + first * row_h));
        uint64_t sel_lo = CUI_MIN(s->anchor, s->cursor), sel_hi = CUI_MAX(s->anchor, s->cursor);
        bool show_cursor = ui->focus == lid && ui->focus_visible && s->has_cursor;
        for (int i = first; i < last; i++) {
            uint64_t seq = s->rows[i];
            const cui_log_line *l = cui__log_at(b, seq);
            if (!l) continue;
            cui__logv_row *p = cui__frame_alloc(ui, sizeof *p);
            memset(p, 0, sizeof *p);
            p->c = c;
            const char *src = b->text + l->offset;
            int len = cui__utf8_cut(src, (int)l->len, CUI__LOG_DRAW_MAX);
            /* a copy: the app may append (and overwrite this text) before painting */
            p->text = cui__frame_strdup(ui, src, len);
            p->len = len;
            p->more = (int)l->len - len;
            p->level = l->level;
            if (o->timestamps) cui__log_time(p->time, sizeof p->time, l->time);
            p->selected = s->has_sel && seq >= sel_lo && seq <= sel_hi;
            p->cursor = show_cursor && seq == s->cursor;
            p->hover = i == hover_row ? s->hover : 0;
            float w = cui__text_width(ui, CUI_FONT_MONO, size, p->text, len, true);
            if (p->more > 0) w += cui__text_width(ui, CUI_FONT_MONO, size, "  ... 000000 more bytes", -1, true);
            if (w > s->max_w) s->max_w = w;
            int rn = cui__leaf(ui, 0, CUI_FILL, CUI_PX(row_h));
            ui->nodes[rn].paint = cui__paint_log_row;
            ui->nodes[rn].paint_data = p;
        }
    }
    cui__close(ui);   /* content */
    ui->nodes[content].min_w = c->x_text + s->max_w + pad_x;
    cui__scroll_end_ex(ui);
    cui__close(ui);   /* frame */
    if (ui->focus == lid) cui__focus_ring(ui, frame, ui->focus_visible ? 1.0f : 0.0f);
    cui_pop_id(ui);
    cui__close(ui);   /* outer */
    ui->last_id = id;
}
