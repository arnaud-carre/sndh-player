/* Media and motion data: carousel, image compare, card stack, odometer and
 * ticker. */
#include "cui_internal.h"

#define cui__fs cui__fsz

/* ------------------------------------------------------------------------ */
/* Carousel                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_spring pos;
    bool dragging, init;
    float press_x, start, vel, last_x;
    double last_t, last_change;
    float hover;
    cui_spring dot[64];
} cui__car_state;

struct cui_carousel_build {
    cui_id id, vid;
    int count, *index;
    cui_carousel_opts o;
    cui__car_state *st;
    float slide_w;
    int row;
    struct cui_carousel_build *prev;
};

static struct cui_carousel_build *cui__car_top(cui_ctx *ui) { return (struct cui_carousel_build *)ui->carousel; }

bool cui_carousel_begin(cui_ctx *ui, const char *id_str, int count, int *index, const cui_carousel_opts *o) {
    cui_carousel_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    struct cui_carousel_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    b->vid = b->id ^ 0x71E3u;
    b->count = count < 0 ? 0 : count;
    b->index = index;
    b->o = *o;
    b->prev = (struct cui_carousel_build *)ui->carousel;
    ui->carousel = b;
    cui__car_state *st = cui_state(ui, b->id, sizeof *st);
    b->st = st;
    if (*index < 0) *index = 0;
    if (*index >= b->count) *index = b->count > 0 ? b->count - 1 : 0;
    if (!st->init) { st->pos.x = (float)*index; st->last_change = ui->time; st->init = true; }
    cui_rect vr = cui__prev_rect(ui, b->vid);
    float gap = o->gap > 0 ? o->gap : 12;
    float sw = vr.w > 0 ? vr.w : 600;
    b->slide_w = sw;
    float stride = sw + gap;
    cui_interact r = cui__interact(ui, b->vid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int target = *index;
    if (r.hovered && ui->mpressed[0]) { st->dragging = true; st->press_x = ui->mx; st->start = st->pos.x; st->vel = 0; st->last_x = ui->mx; st->last_t = ui->time; }
    if (st->dragging) {
        if (ui->mdown[0]) {
            float p = st->start - (ui->mx - st->press_x) / stride;
            if (p < 0) p *= 0.3f;
            if (p > b->count - 1) p = (b->count - 1) + (p - (b->count - 1)) * 0.3f;
            double dt = ui->time - st->last_t;
            if (dt > 0.001) { st->vel = cui__lerp(st->vel, -(ui->mx - st->last_x) / stride / (float)dt, 0.4f); st->last_t = ui->time; st->last_x = ui->mx; }
            st->pos.x = p; st->pos.v = 0;
            ui->active = b->vid;
            ui->animating = true;
        } else {
            st->dragging = false;
            float land = st->pos.x + cui__clampf(st->vel, -8, 8) * 0.12f;
            target = (int)lroundf(land);
            /* a quick flick always moves at least one slide */
            if (target == *index && fabsf(st->vel) > 1.2f) target += st->vel > 0 ? 1 : -1;
        }
    }
    if (r.hovered && ui->scroll_dx != 0 && !st->dragging) {
        target += ui->scroll_dx > 0 ? 1 : -1;
        ui->scroll_dx = 0;
    }
    if (ui->focus == b->vid) {
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) target++;
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) target--;
    }
    if (o->auto_advance > 0 && !r.hovered && !st->dragging && b->count > 1) {
        double since = ui->time - st->last_change;
        if (since >= o->auto_advance) target = *index + 1 >= b->count ? 0 : *index + 1;
        else cui__wakeup_after(ui, (float)(o->auto_advance - since) + 0.01f);
    }
    if (o->loop && b->count > 0) { if (target >= b->count) target = 0; if (target < 0) target = b->count - 1; }
    target = CUI_CLAMP(target, 0, b->count > 0 ? b->count - 1 : 0);
    if (target != *index) { *index = target; st->last_change = ui->time; }
    if (!st->dragging) cui__spring(ui, &st->pos, (float)*index, 0.5f, 0.02f);
    float h = o->height > 0 ? o->height : 220;
    int col = cui__node_new(ui, b->id);
    ui->nodes[col].gap = 12; ui->nodes[col].w = CUI_FILL; ui->nodes[col].align = CUI_ALIGN_CENTER;
    cui__open(ui, col);
    int view = cui__node_new(ui, b->vid);
    cui_node *vn = &ui->nodes[view];
    vn->w = CUI_FILL; vn->h = CUI_PX(h); vn->self_align = CUI_ALIGN_STRETCH;
    vn->flags |= CUI_NF_CLIP;
    vn->radius = CUI_CORNERS(ui->theme.radius_panel);
    cui__mark_interactive(ui, view, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    if (r.hovered) cui__set_cursor(ui, st->dragging ? CUI_CURSOR_MOVE : CUI_CURSOR_ARROW);
    cui__open(ui, view);
    /* the track of slides is a layer inside the clipped view: it never sets the view's minimum
       width, so the carousel fits a phone and every slide takes the width the view has */
    int row = cui__absolute(ui, 0, 0, 0, b->count * stride, h);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->gap = gap;
    rn->tx = -st->pos.x * stride;
    cui__open(ui, row);
    b->row = row;
    cui__focus_ring(ui, view, r.focus_visible ? 1.0f : 0.0f);
    return true;
}

void cui_carousel_slide_begin(cui_ctx *ui) {
    struct cui_carousel_build *b = cui__car_top(ui);
    int n = cui__node_new(ui, 0);
    cui_node *sn = &ui->nodes[n];
    sn->w = CUI_PX(b ? b->slide_w : 600); sn->h = CUI_FILL;
    sn->radius = CUI_CORNERS(ui->theme.radius_panel);
    sn->flags |= CUI_NF_CLIP;
    cui__open(ui, n);
}

void cui_carousel_slide_end(cui_ctx *ui) { cui__close(ui); }

void cui_carousel_end(cui_ctx *ui) {
    struct cui_carousel_build *b = cui__car_top(ui);
    if (!b) return;
    cui__car_state *st = b->st;
    cui__close(ui);   /* row */
    int *index = b->index;
    float h = b->o.height > 0 ? b->o.height : 220;
    /* arrows over the slides, shown on hover */
    if (!b->o.no_arrows && b->count > 1) {
        for (int k = 0; k < 2; k++) {
            bool next = k == 1;
            bool enabled = b->o.loop || (next ? *index < b->count - 1 : *index > 0);
            float x = next ? b->slide_w - 12 - 36 : 12;
            int a = cui__absolute(ui, 0, x, h * 0.5f - 18, 36, 36);
            (void)a;
            cui__open(ui, a);
            cui_button_opts bo;
            memset(&bo, 0, sizeof bo);
            bo.variant = CUI_BUTTON_SECONDARY;
            bo.tooltip = next ? "Next" : "Previous";
            bo.disabled = !enabled;
            cui_push_id_int(ui, (int)b->id + k);
            if (cui_icon_button_ex(ui, next ? CUI_ICON_CHEVRON_RIGHT : CUI_ICON_CHEVRON_LEFT, &bo)) {
                int t = *index + (next ? 1 : -1);
                if (b->o.loop) t = (t + b->count) % b->count;
                *index = CUI_CLAMP(t, 0, b->count - 1);
                st->last_change = ui->time;
            }
            cui_pop_id(ui);
            cui_node *bn = &ui->nodes[ui->last_node];
            bn->bg = cui_alpha(CUI_C(ui, SURFACE_RAISED), 0.92f);
            bn->border = 1; bn->border_color = CUI_C(ui, BORDER);
            bn->elevation = CUI_ELEVATION_RAISED; bn->elevation_alpha = 1;
            bn->opacity = CUI_MAX(0.001f, st->hover) * (enabled ? 1.0f : 0.4f);
            cui__close(ui);
        }
    }
    cui__close(ui);   /* viewport */
    if (!b->o.no_dots && b->count > 1) {
        int dots = cui__node_new(ui, 0);
        ui->nodes[dots].dir = CUI_ROW; ui->nodes[dots].gap = 6; ui->nodes[dots].align = CUI_ALIGN_CENTER;
        cui__open(ui, dots);
        for (int k = 0; k < b->count && k < 64; k++) {
            cui_id did = cui__hash(&k, sizeof k, b->id ^ 0xD07u);
            cui_interact dr = cui__interact(ui, did, 0);
            if (dr.clicked) { *index = k; st->last_change = ui->time; }
            cui__spring(ui, &st->dot[k], k == *index ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);
            float t = cui__clampf(st->dot[k].x, 0, 1.3f);
            int dn = cui__leaf(ui, did, CUI_PX(6 + 14 * t), CUI_PX(6));
            ui->nodes[dn].radius = CUI_CORNERS(3);
            ui->nodes[dn].bg = cui_mix(cui_mix(CUI_C(ui, BORDER_STRONG), CUI_C(ui, TEXT_MUTED), dr.hovered ? 1.0f : 0.0f), CUI_C(ui, FOREGROUND), cui__clampf(t, 0, 1));
            cui__mark_interactive(ui, dn, 0);
            if (dr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        }
        cui__close(ui);
    }
    cui__close(ui);   /* column */
    ui->carousel = b->prev;
    ui->last_id = b->id;
}

/* ------------------------------------------------------------------------ */
/* Image compare                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { cui_texture before, after; float pos, hover, aspect; const char *lb, *la; cui_color handle, line, label_bg, label_fg; } cui__cmp_paint;

static void cui__cmp_label(cui_ctx *ui, const char *s, float x, float y, bool right, cui__cmp_paint *p) {
    if (!s) return;
    float fs = cui__fs(ui, ui->theme.text_xs);
    float w = cui__text_width(ui, CUI_FONT_MEDIUM, fs, s, -1, false) + 16, h = 22;
    cui_rect r = { right ? x - w : x, y, w, h };
    cui_draw_rect(ui, r, CUI_CORNERS(6), p->label_bg);
    cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs, r.x + 8, r.y + (h - cui__font_line_height(ui, CUI_FONT_MEDIUM, fs)) * 0.5f, p->label_fg, s, -1, false);
}

static void cui__paint_compare(cui_ctx *ui, cui_node *n, void *data) {
    cui__cmp_paint *p = data;
    cui_rect r = n->rect;
    cui_corners rad = n->radius;
    cui_rect uv = { 0, 0, 1, 1 };
    if (p->aspect > 0 && r.h > 0) {
        /* cover: keep the picture's proportions and crop the overflow evenly */
        float ra = r.w / r.h;
        if (p->aspect > ra) { float u = ra / p->aspect; uv = (cui_rect){ (1 - u) * 0.5f, 0, u, 1 }; }
        else { float v = p->aspect / ra; uv = (cui_rect){ 0, (1 - v) * 0.5f, 1, v }; }
    }
    cui_color white = cui__rgba(255, 255, 255, 255);
    if (p->after) cui_draw_image(ui, p->after, r, uv, rad, white);
    float split = r.w * cui__clampf(p->pos, 0, 1);
    cui_push_clip(ui, (cui_rect){ r.x, r.y, split, r.h });
    if (p->before) cui_draw_image(ui, p->before, r, uv, rad, white);
    cui_pop_clip(ui);
    cui__cmp_label(ui, p->lb, r.x + 12, r.y + 12, false, p);
    cui__cmp_label(ui, p->la, r.x + r.w - 12, r.y + 12, true, p);
    float x = r.x + split;
    cui_draw_rect(ui, (cui_rect){ x - 1, r.y, 2, r.h }, CUI_CORNERS(0), p->line);
    float hr = 17 + 2 * p->hover;
    cui_vec2 c = { x, r.y + r.h * 0.5f };
    cui_draw_shadow(ui, (cui_rect){ c.x - hr, c.y - hr, hr * 2, hr * 2 }, CUI_CORNERS(hr), 12, 0, (cui_vec2){ 0, 3 }, cui__rgba(0, 0, 0, 60));
    cui_draw_circle(ui, c, hr, p->handle);
    cui_draw_icon(ui, CUI_ICON_CHEVRON_LEFT, (cui_rect){ c.x - 15, c.y - 8, 16, 16 }, 12, cui__rgba(30, 30, 34, 255));
    cui_draw_icon(ui, CUI_ICON_CHEVRON_RIGHT, (cui_rect){ c.x - 1, c.y - 8, 16, 16 }, 12, cui__rgba(30, 30, 34, 255));
}

bool cui_image_compare_ex(cui_ctx *ui, const char *id_str, cui_texture before, cui_texture after, float *pos, const cui_compare_opts *o) {
    cui_compare_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    float old = *pos;
    float *hv = cui_state(ui, id, sizeof(float));
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui_rect pr = cui__prev_rect(ui, id);
    if (r.active && ui->mdown[0] && pr.w > 0) *pos = cui__clampf((ui->mx - pr.x) / pr.w, 0, 1);
    if (ui->focus == id) {
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) *pos = cui__clampf(*pos - 0.05f, 0, 1);
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) *pos = cui__clampf(*pos + 0.05f, 0, 1);
    }
    cui__tween(ui, hv, (r.hovered || r.active) ? 1.0f : 0.0f, CUI__T_HOVER);
    int n = cui__leaf(ui, id, o->width.kind ? o->width : CUI_FILL, CUI_PX(o->height > 0 ? o->height : 280));
    cui_node *nd = &ui->nodes[n];
    nd->radius = CUI_CORNERS(ui->theme.radius_panel);
    nd->bg = CUI_C(ui, SURFACE_MUTED);
    nd->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    if (r.hovered || r.active) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
    cui__cmp_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->before = before; p->after = after; p->pos = *pos; p->hover = *hv; p->aspect = o->aspect;
    p->lb = o->before_label ? o->before_label : "Before";
    p->la = o->after_label ? o->after_label : "After";
    p->handle = cui__rgba(255, 255, 255, 255);
    p->line = cui__rgba(255, 255, 255, 230);
    p->label_bg = cui__rgba(0, 0, 0, 120);
    p->label_fg = cui__rgba(255, 255, 255, 255);
    nd->paint = cui__paint_compare;
    nd->paint_data = p;
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = id;
    return *pos != old;
}

/* ------------------------------------------------------------------------ */
/* Card stack                                                               */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_spring x, y, promote;
    bool dragging, flying;
    int fly_dir;
    float press_x, press_y, vel, last_x;
    double last_t;
} cui__stack_state;

struct cui_stack_build {
    cui_id id;
    int count, visible;
    float w, h;
    cui__stack_state *st;
    int result;
    struct cui_stack_build *prev;
};

void cui_card_stack_begin(cui_ctx *ui, const char *id_str, int count, const cui_card_stack_opts *o) {
    cui_card_stack_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    struct cui_stack_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    b->count = count;
    b->visible = o->visible > 0 ? o->visible : 3;
    b->h = o->height > 0 ? o->height : 320;
    b->prev = (struct cui_stack_build *)ui->card_stack;
    ui->card_stack = b;
    cui__stack_state *st = cui_state(ui, b->id, sizeof *st);
    b->st = st;
    cui_id top_id = b->id ^ 0x70Bu;
    cui_rect tr = cui__prev_rect(ui, top_id);
    b->w = tr.w > 0 ? tr.w : 300;
    cui_interact r = cui__interact(ui, top_id, CUI_IF_DRAG | CUI_IF_FOCUSABLE);
    if (!st->flying && r.hovered && ui->mpressed[0] && count > 0) {
        st->dragging = true; st->press_x = ui->mx; st->press_y = ui->my; st->vel = 0; st->last_x = ui->mx; st->last_t = ui->time;
    }
    int throw_dir = 0;
    if (st->dragging) {
        if (ui->mdown[0]) {
            st->x.x = ui->mx - st->press_x; st->x.v = 0;
            st->y.x = (ui->my - st->press_y) * 0.3f; st->y.v = 0;
            double dt = ui->time - st->last_t;
            if (dt > 0.001) { st->vel = cui__lerp(st->vel, (ui->mx - st->last_x) / (float)dt, 0.4f); st->last_t = ui->time; st->last_x = ui->mx; }
            ui->active = top_id;
            ui->animating = true;
        } else {
            st->dragging = false;
            if (fabsf(st->x.x) > b->w * 0.35f || fabsf(st->vel) > 900) throw_dir = (st->x.x + st->vel * 0.1f) > 0 ? 1 : -1;
        }
    }
    if (ui->focus == top_id && !st->flying) {
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) throw_dir = -1;
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) throw_dir = 1;
    }
    if (throw_dir && count > 0) { st->flying = true; st->fly_dir = throw_dir; }
    if (st->flying) {
        cui__spring(ui, &st->x, st->fly_dir * (b->w * 1.5f + 80), 0.32f, 0.0f);
        if (fabsf(st->x.x) > b->w * 1.3f) {
            /* gone: the next card is on top now; let the rest of the deck move up */
            b->result = st->fly_dir;
            st->flying = false;
            st->x.x = 0; st->x.v = 0; st->y.x = 0; st->y.v = 0;
            st->promote.x = 1; st->promote.v = 0;
        }
    } else if (!st->dragging) {
        cui__spring(ui, &st->x, 0, CUI_SPRING_SNAPPY);
        cui__spring(ui, &st->y, 0, CUI_SPRING_SNAPPY);
    }
    cui__spring(ui, &st->promote, 0, 0.38f, 0.0f);
    int box = cui__node_new(ui, b->id);
    cui_node *bn = &ui->nodes[box];
    bn->dir = CUI_STACK;
    cui__soft_width(bn, o->width, 320);
    bn->h = CUI_PX(b->h + 12.0f * (float)(b->visible - 1));
    bn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, box);
}

void cui_card_stack_item_begin(cui_ctx *ui, int depth) {
    struct cui_stack_build *b = (struct cui_stack_build *)ui->card_stack;
    if (!b) { int n = cui__node_new(ui, 0); cui__open(ui, n); return; }
    cui__stack_state *st = b->st;
    bool top = depth == 0;
    float dd = (float)depth + st->promote.x;   /* deeper cards slide forward after a throw */
    int n = cui__node_new(ui, top ? (b->id ^ 0x70Bu) : 0);
    cui_node *cn = &ui->nodes[n];
    cn->w = CUI_FILL; cn->h = CUI_PX(b->h);
    cn->radius = CUI_CORNERS(ui->theme.radius_panel + 4);
    cn->bg = CUI_C(ui, SURFACE_RAISED);
    /* a translucent hairline, so it reads on the surface and on whatever fills the card */
    cn->border = 1;
    cn->border_color = ui->mode == CUI_THEME_DARK ? cui__rgba(255, 255, 255, 26) : cui__rgba(0, 0, 0, 22);
    cn->elevation = top ? CUI_ELEVATION_FLOATING : CUI_ELEVATION_RESTING;
    cn->elevation_alpha = top ? 0.6f + 0.4f * cui__clampf(fabsf(st->x.x) / 120, 0, 1) : 1;
    cn->flags |= CUI_NF_CLIP;
    cn->scale = 1.0f - 0.05f * dd;
    cn->ty = 12.0f * dd;
    if (top) {
        cn->tx = st->x.x;
        cn->ty += st->y.x;
        cn->flags |= CUI_NF_ON_TOP;
        cui__mark_interactive(ui, n, CUI_IF_DRAG | CUI_IF_FOCUSABLE);
        if (ui->hot == cn->id) cui__set_cursor(ui, st->dragging ? CUI_CURSOR_MOVE : CUI_CURSOR_HAND);
    }
    cn->opacity = depth >= b->visible ? 0.001f : CUI_MAX(0.001f, 1.0f - 0.12f * dd);
    cui__open(ui, n);
    cui_push_id_int(ui, depth);
}

void cui_card_stack_item_end(cui_ctx *ui) {
    cui_pop_id(ui);
    cui__close(ui);
}

int cui_card_stack_end(cui_ctx *ui) {
    struct cui_stack_build *b = (struct cui_stack_build *)ui->card_stack;
    if (!b) return 0;
    /* paint deeper cards first: reverse the children so the top card is last */
    cui__close(ui);
    int box = -1;
    for (int k = ui->node_count - 1; k >= 0; k--) if (ui->nodes[k].id == b->id) { box = k; break; }
    if (box >= 0) {
        cui_node *bn = &ui->nodes[box];
        int prev = -1, c = bn->first;
        bn->last = c;
        while (c >= 0) { int nx = ui->nodes[c].next; ui->nodes[c].next = prev; prev = c; c = nx; }
        bn->first = prev;
    }
    ui->card_stack = b->prev;
    ui->last_id = b->id;
    return b->result;
}

/* ------------------------------------------------------------------------ */
/* Odometer                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring d[24]; char last[32]; bool init; } cui__odo_state;
typedef struct { float value; float size, lh; cui_font font; cui_color c; } cui__digit_paint;

static void cui__paint_digit(cui_ctx *ui, cui_node *n, void *data) {
    cui__digit_paint *p = data;
    cui_rect r = n->rect;
    cui_push_clip(ui, r);
    float base = floorf(p->value);
    float fh = cui__font_line_height(ui, p->font, p->size);
    for (int k = -1; k <= 1; k++) {
        float v = base + (float)k;
        int digit = ((int)v % 10 + 10) % 10;
        float y = r.y + (v - p->value) * p->lh;   /* the next digit waits below and rolls up */
        char ch[2] = { (char)('0' + digit), 0 };
        float w = cui__text_width(ui, p->font, p->size, ch, 1, true);
        float dist = fabsf(v - p->value);
        if (dist >= 1) continue;
        cui__draw_text_line(ui, p->font, p->size, r.x + (r.w - w) * 0.5f, y + (p->lh - fh) * 0.5f, cui_alpha(p->c, 1.0f - dist * 0.6f), ch, 1, true);
    }
    cui_pop_clip(ui);
}

void cui_counter_ex(cui_ctx *ui, const char *id_str, double value, const cui_counter_opts *o) {
    cui_counter_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__odo_state *st = cui_state(ui, id, sizeof *st);
    float size = cui__fs(ui, o->size > 0 ? o->size : 32);
    cui_font font = o->font ? o->font : CUI_FONT_DISPLAY;
    cui_color color = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, FOREGROUND);
    char s[48];
    snprintf(s, sizeof s, o->format ? o->format : "%.0f", value);
    if (o->group_thousands) {
        /* commas in the first run of digits: 48210 -> 48,210 */
        char g[48];
        int a = 0;
        while (s[a] && !(s[a] >= '0' && s[a] <= '9')) a++;
        int b2 = a;
        while (s[b2] >= '0' && s[b2] <= '9') b2++;
        int o2 = 0;
        for (int k = 0; k < a && o2 < 46; k++) g[o2++] = s[k];
        for (int k = a; k < b2 && o2 < 46; k++) {
            g[o2++] = s[k];
            int left = b2 - k - 1;
            if (left > 0 && left % 3 == 0 && o2 < 46) g[o2++] = ',';
        }
        for (int k = b2; s[k] && o2 < 46; k++) g[o2++] = s[k];
        g[o2] = 0;
        memcpy(s, g, (size_t)o2 + 1);
    }
    int len = (int)strlen(s);
    float lh = size * 1.15f;
    float dw = cui__text_width(ui, font, size, "0", 1, true);
    int row = cui__node_new(ui, id);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].align = CUI_ALIGN_CENTER; ui->nodes[row].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    int digit_ix = 0;
    for (int k = len - 1; k >= 0; k--) if (s[k] >= '0' && s[k] <= '9') digit_ix++;
    int pos_from_right = digit_ix;
    for (int k = 0; k < len; k++) {
        char c = s[k];
        if (c >= '0' && c <= '9') {
            pos_from_right--;
            int slot = CUI_CLAMP(pos_from_right, 0, 23);
            cui_spring *sp = &st->d[slot];
            /* keep counting in the same direction: the spring target is the digit
             * plus whole turns, so 9 to 0 rolls forward through one step */
            float target = (float)(c - '0');
            if (!st->init) sp->x = target;
            float cur = sp->x;
            float turns = floorf(cur / 10.0f) * 10.0f;
            float t1 = turns + target;
            if (t1 - cur > 5) t1 -= 10;
            if (cur - t1 > 5) t1 += 10;
            cui__spring(ui, sp, t1, 0.55f, 0.05f);
            int leaf = cui__leaf(ui, 0, CUI_PX(dw), CUI_PX(lh));
            cui__digit_paint *p = cui__frame_alloc(ui, sizeof *p);
            p->value = sp->x; p->size = size; p->lh = lh; p->font = font; p->c = color;
            ui->nodes[leaf].paint = cui__paint_digit;
            ui->nodes[leaf].paint_data = p;
        } else {
            int t = cui__text_node(ui, cui__frame_strdup(ui, &s[k], 1), 1, font, size, color, false);
            ui->nodes[t].line_height = lh / size;
        }
    }
    st->init = true;
    cui__close(ui);
    ui->last_id = id;
}

/* ------------------------------------------------------------------------ */
/* Ticker                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct { float off; float set_w; } cui__ticker_state;
typedef struct { cui_color bg; float w; } cui__edge_paint;

static void cui__paint_ticker_edges(cui_ctx *ui, cui_node *n, void *data) {
    cui__edge_paint *p = data;
    cui_rect r = n->rect;
    int slices = 16;
    float sw = p->w / slices;
    for (int k = 0; k < slices; k++) {
        float a = 1.0f - (k + 0.5f) / slices;
        a = a * a;
        cui_draw_rect(ui, (cui_rect){ r.x + k * sw, r.y, sw + 0.5f, r.h }, CUI_CORNERS(0), cui_alpha(p->bg, a));
        cui_draw_rect(ui, (cui_rect){ r.x + r.w - (k + 1) * sw - 0.5f, r.y, sw + 0.5f, r.h }, CUI_CORNERS(0), cui_alpha(p->bg, a));
    }
}

static void cui__ticker_set(cui_ctx *ui, cui_id set_id, const cui_ticker_item *items, int count) {
    int set = cui__node_new(ui, set_id);
    ui->nodes[set].dir = CUI_ROW; ui->nodes[set].align = CUI_ALIGN_CENTER;
    cui__open(ui, set);
    for (int k = 0; k < count; k++) {
        const cui_ticker_item *it = &items[k];
        int item = cui__node_new(ui, 0);
        ui->nodes[item].dir = CUI_ROW; ui->nodes[item].gap = 8; ui->nodes[item].align = CUI_ALIGN_CENTER;
        ui->nodes[item].pad[1] = ui->nodes[item].pad[3] = 18;
        cui__open(ui, item);
        int l = cui__text_node(ui, it->label, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[l].line_height = 1.3f;
        if (it->value) {
            int v = cui__text_node(ui, it->value, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
            ui->nodes[v].line_height = 1.3f;
            ui->nodes[v].flags |= CUI_NF_TABULAR;
        }
        if (it->delta != 0) {
            cui_color c = (it->lower_is_better ? it->delta < 0 : it->delta > 0) ? CUI_C(ui, SUCCESS) : CUI_C(ui, DANGER);
            cui__icon_node(ui, it->delta > 0 ? CUI_ICON_TRENDING_UP : CUI_ICON_TRENDING_DOWN, 12, c);
            int dt = cui__text_node(ui, cui__frame_printf(ui, "%+.1f%%", (double)it->delta), -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), c, false);
            ui->nodes[dt].line_height = 1.3f;
            ui->nodes[dt].flags |= CUI_NF_TABULAR;
        }
        cui__close(ui);
        int sep = cui__leaf(ui, 0, CUI_PX(4), CUI_PX(4));
        ui->nodes[sep].radius = CUI_CORNERS(2);
        ui->nodes[sep].bg = CUI_C(ui, BORDER_STRONG);
    }
    cui__close(ui);
}

void cui_ticker_ex(cui_ctx *ui, const char *id_str, const cui_ticker_item *items, int count, const cui_ticker_opts *o) {
    cui_ticker_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id set_id = id ^ 0x5E7u;
    cui__ticker_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, 0);
    cui_rect sr = cui__prev_rect(ui, set_id);
    if (sr.w > 0) st->set_w = sr.w;
    float speed = o->speed > 0 ? o->speed : 40;
    if (!r.hovered && st->set_w > 0 && ui->theme.motion > 0) {
        st->off += ui->dt * speed;
        if (st->off >= st->set_w) st->off -= st->set_w;
        ui->animating = true;
    }
    float h = o->height > 0 ? o->height : 40;
    int box = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[box];
    bn->w = CUI_FILL; bn->h = CUI_PX(h);
    bn->flags |= CUI_NF_CLIP | CUI_NF_PAINT_AFTER;
    bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER;
    cui__edge_paint *ep = cui__frame_alloc(ui, sizeof *ep);
    ep->bg = CUI_C(ui, BACKGROUND); ep->w = 48;
    bn->paint = cui__paint_ticker_edges;
    bn->paint_data = ep;
    cui__mark_interactive(ui, box, 0);
    cui__open(ui, box);
    /* a layer wider than the view, so items keep their natural width; as a layer it does not
       set the ticker's minimum width, which stays narrow enough for a phone */
    int strip = cui__absolute(ui, 0, 0, 0, st->set_w > 0 ? st->set_w * 3 + 16 : 6000, h);
    ui->nodes[strip].dir = CUI_ROW; ui->nodes[strip].align = CUI_ALIGN_CENTER;
    ui->nodes[strip].tx = -st->off;
    cui__open(ui, strip);
    /* enough copies to cover the width while one scrolls off */
    cui__ticker_set(ui, set_id, items, count);
    cui__ticker_set(ui, 0, items, count);
    cui__ticker_set(ui, 0, items, count);
    cui__close(ui);
    cui__close(ui);
    ui->last_id = id;
}
