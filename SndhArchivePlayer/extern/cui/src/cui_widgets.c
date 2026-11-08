/* Core components: text, buttons, badges, avatars, cards, feedback,
 * scroll areas, split views and custom canvases. */
#include "cui_internal.h"

/* ------------------------------------------------------------------------ */
/* Shared helpers                                                           */
/* ------------------------------------------------------------------------ */

float cui__control_height(cui_ctx *ui, cui_control_size s) {
    cui_theme *t = &ui->theme;
    return s == CUI_SIZE_SM ? t->control_sm : s == CUI_SIZE_LG ? t->control_lg : t->control_md;
}

static float cui__fs(cui_ctx *ui, float size) { return size * ui->theme.font_scale; }

void cui__label_text(cui_ctx *ui, const char *label, cui_font font, float size, cui_color color) {
    int len = cui__display_len(label);
    int i = cui__text_node(ui, label, len, font, size, color, false);
    ui->nodes[i].line_height = 1.3f;
}

void cui__icon_node(cui_ctx *ui, const char *icon, float size, cui_color color) {
    if (!icon) return;
    int i = cui__text_node(ui, icon, -1, CUI_FONT_ICONS, size, color, false);
    cui_node *n = &ui->nodes[i];
    n->line_height = 1.0f;
    n->self_align = CUI_ALIGN_CENTER;
}

void cui__field_label(cui_ctx *ui, const char *label) {
    if (!label || !*label) return;
    int i = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[i].line_height = 1.35f;
}

void cui__field_message(cui_ctx *ui, const char *hint, const char *error) {
    const char *msg = error && *error ? error : hint;
    if (!msg || !*msg) return;
    cui_color c = error && *error ? CUI_C(ui, DANGER) : CUI_C(ui, TEXT_SECONDARY);
    int i = cui__text_node(ui, msg, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), c, true);
    ui->nodes[i].line_height = 1.4f;
}

static void cui__paint_focus_after(cui_ctx *ui, cui_node *n, void *data) {
    float a = *(float *)data;
    cui__draw_focus_ring(ui, n->rect, n->radius, a);
}

/* Attach a keyboard focus ring to node i (alpha animated by caller). */
void cui__focus_ring(cui_ctx *ui, int i, float alpha) {
    if (alpha <= 0.01f) return;
    cui_node *n = &ui->nodes[i];
    float *d = cui__frame_alloc(ui, sizeof(float));
    *d = alpha;
    n->paint = cui__paint_focus_after;
    n->paint_data = d;
    n->flags |= CUI_NF_PAINT_AFTER;
}

/* ------------------------------------------------------------------------ */
/* Text                                                                     */
/* ------------------------------------------------------------------------ */

void cui_text_ex(cui_ctx *ui, const char *text, const cui_text_opts *o) {
    cui_text_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float size = o->size > 0 ? o->size : ui->theme.text_body;
    cui_color c = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, FOREGROUND);
    int i = cui__text_node(ui, text, -1, o->font, cui__fs(ui, size), c, !o->nowrap);
    cui_node *n = &ui->nodes[i];
    n->text_align = (uint8_t)o->align;
    if (o->tabular) n->flags |= CUI_NF_TABULAR;
    if (o->line_height > 0) n->line_height = o->line_height;
    if (o->width.kind != CUI_SIZE_FIT) n->w = o->width;
    if (o->align != CUI_TEXT_LEFT && o->width.kind == CUI_SIZE_FIT) n->self_align = CUI_ALIGN_STRETCH;
    if (o->nowrap && text && text[0]) {
        /* a single line cut short by an ellipsis shows its whole text on hover. The
         * text stays non-interactive, so a row or card around it keeps its clicks. */
        cui_id tid = cui_make_id(ui, text) ^ ((cui_id)ui->node_count * 2654435761u);
        n->id = tid;
        cui_rect pr = cui__prev_rect(ui, tid);
        if (pr.w > 0 && !ui->mdown[0] && cui__rect_contains(pr, ui->mx, ui->my)) {
            float tw = cui__text_width(ui, n->font, n->font_size, text, -1, o->tabular);
            if (tw > pr.w + 0.5f) cui__tooltip_passive(ui, tid, text);
        }
    }
}

void cui_textf(cui_ctx *ui, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = cui__frame_vprintf(ui, fmt, ap);
    va_end(ap);
    cui_text_ex(ui, s, NULL);
}

void cui_heading(cui_ctx *ui, const char *text, int level) {
    if (level < 1) level = 1;
    if (level > 4) level = 4;
    float size = ui->theme.heading[level - 1];
    cui_font f = size >= 26 ? CUI_FONT_DISPLAY : CUI_FONT_MEDIUM;
    int i = cui__text_node(ui, text, -1, f, cui__fs(ui, size), CUI_C(ui, FOREGROUND), true);
    ui->nodes[i].line_height = size >= 26 ? 1.15f : 1.3f;
}

void cui_label(cui_ctx *ui, const char *text) {
    int i = cui__text_node(ui, text, cui__display_len(text), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), true);
    ui->nodes[i].line_height = 1.35f;
}

void cui_secondary(cui_ctx *ui, const char *text) {
    cui__text_node(ui, text, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), true);
}

void cui_muted(cui_ctx *ui, const char *text) {
    cui__text_node(ui, text, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), true);
}

void cui_code(cui_ctx *ui, const char *text) {
    cui_box b;
    memset(&b, 0, sizeof b);
    b.pad_x = 6; b.pad_y = 2; b.radius = 6;
    b.bg = CUI_C(ui, ACTIVE);
    int i = cui__node_new(ui, 0);
    cui__apply_box(ui, &ui->nodes[i], &b);
    ui->nodes[i].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, i);
    int t = cui__text_node(ui, text, -1, CUI_FONT_MONO, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.4f;
    cui__close(ui);
}

void cui_kbd(cui_ctx *ui, const char *keys) {
    /* each space separated key becomes a keycap */
    int row = cui__node_new(ui, 0);
    cui_node *r = &ui->nodes[row];
    r->dir = CUI_ROW; r->gap = 3; r->self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    const char *p = keys;
    while (p && *p) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        if (e > p) {
            int cap = cui__node_new(ui, 0);
            cui_node *c = &ui->nodes[cap];
            c->min_w = 20; c->pad[1] = c->pad[3] = 5; c->h = CUI_PX(20);
            c->radius = CUI_CORNERS(5);
            c->bg = CUI_C(ui, SURFACE);
            c->border = 1; c->border_color = cui__quiet_edge(ui, 0.35f);
            c->align = CUI_ALIGN_CENTER; c->justify = CUI_JUSTIFY_CENTER; c->dir = CUI_ROW;
            cui__open(ui, cap);
            int t = cui__text_node(ui, p, (int)(e - p), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[t].line_height = 1.2f;
            cui__close(ui);
        }
        p = e;
    }
    cui__close(ui);
}

typedef struct { float hover; cui_spring press; float focus; } cui__btn_state;

typedef struct { float hover; cui_color color; } cui__underline_data;
static void cui__paint_underline(cui_ctx *ui, cui_node *n, void *data) {
    cui__underline_data *u = data;
    if (u->hover <= 0.01f) return;
    cui_rect r = { n->rect.x + n->pad[3], n->rect.y + n->rect.h * 0.5f + n->rect.h * 0.32f, (n->rect.w - n->pad[1] - n->pad[3]) * u->hover, 1 };
    cui_draw_rect(ui, r, CUI_CORNERS(0), u->color);
}

bool cui_link(cui_ctx *ui, const char *text) {
    cui_id id = cui_make_id(ui, text) ^ 0x11AC0009u;
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui__btn_state rest;
    memset(&rest, 0, sizeof rest);
    cui__btn_state *st = cui__state_lazy(ui, id, sizeof *st, r.hovered || r.focus_visible);
    if (!st) st = &rest;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    int i = cui__text_node(ui, text, cui__display_len(text), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body),
                           cui_mix(CUI_C(ui, ACCENT_STRONG), CUI_C(ui, FOREGROUND), ui->accent == CUI_ACCENT_NEUTRAL ? 1.0f : 0.0f), false);
    cui_node *n = &ui->nodes[i];
    n->id = id;
    n->self_align = CUI__ALIGN_NOSTRETCH;
    n->radius = CUI_CORNERS(4);
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    {
        cui__underline_data *u = cui__frame_alloc(ui, sizeof *u);
        u->hover = st->hover; u->color = n->text_color;
        n->paint = cui__paint_underline;
        n->paint_data = u;
        n->flags |= CUI_NF_PAINT_AFTER;
    }
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    ui->last_id = id;
    (void)st->focus;
    return r.clicked;
}

void cui_icon(cui_ctx *ui, const char *icon, float size, cui_color color) {
    cui__icon_node(ui, icon, size > 0 ? size : 16, cui__color_set(color) ? cui_color_of(ui, color) : CUI_C(ui, FOREGROUND));
}

/* ------------------------------------------------------------------------ */
/* Buttons                                                                  */
/* ------------------------------------------------------------------------ */


void cui__paint_sheen(cui_ctx *ui, cui_node *n, void *data) {
    cui__draw_sheen(ui, n->rect, n->radius, data ? *(float *)data : 1.0f);
}

cui_color cui__quiet_edge(cui_ctx *ui, float hover) {
    return cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), 0.35f + 0.65f * cui__clampf(hover, 0, 1));
}

cui_color cui__control_edge(cui_ctx *ui, float hover) {
    return cui_mix(CUI_C(ui, BORDER_STRONG), CUI_C(ui, TEXT_MUTED), 0.5f * cui__clampf(hover, 0, 1));
}

cui_color cui__edge_on(cui_ctx *ui) {
    return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, TEXT_SECONDARY) : CUI_C(ui, ACCENT);
}

cui__btn_colors cui__button_colors(cui_ctx *ui, cui_button_variant v, bool selected) {
    cui__btn_colors c;
    cui_color clear = cui__rgba(0, 0, 0, 0);
    cui_color fg = CUI_C(ui, FOREGROUND);
    c.border = c.border_hover = clear;
    switch (v) {
    case CUI_BUTTON_PRIMARY: {
        cui_color a = CUI_C(ui, ACCENT);
        if (ui->accent == CUI_ACCENT_NEUTRAL) a = CUI_C(ui, CONTROL_ON);
        c.bg = a;
        c.bg_hover = cui_mix(a, CUI_C(ui, BACKGROUND), 0.14f);
        c.bg_press = cui_mix(a, CUI_C(ui, BACKGROUND), 0.06f);   /* pushed in: back toward rest, never lighter than hover */
        c.fg = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, ACCENT_FOREGROUND);
        break;
    }
    case CUI_BUTTON_DANGER: {
        cui_color d = CUI_C(ui, DANGER);
        c.bg = d;
        c.bg_hover = cui_mix(d, CUI_C(ui, BACKGROUND), 0.12f);
        c.bg_press = cui_mix(d, CUI_C(ui, BACKGROUND), 0.05f);
        c.fg = ui->mode == CUI_THEME_DARK ? cui__rgba(26, 10, 12, 255) : cui__rgba(255, 255, 255, 255);
        break;
    }
    case CUI_BUTTON_OUTLINE:
        c.bg = clear;
        c.bg_hover = CUI_C(ui, HOVER);
        c.bg_press = CUI_C(ui, ACTIVE);
        c.fg = fg;
        c.border = cui__quiet_edge(ui, 0);
        c.border_hover = cui__quiet_edge(ui, 1);
        break;
    case CUI_BUTTON_GHOST:
        c.bg = selected ? CUI_C(ui, ACTIVE) : clear;
        c.bg_hover = selected ? CUI_C(ui, ACTIVE) : CUI_C(ui, HOVER);
        c.bg_press = CUI_C(ui, ACTIVE);
        c.fg = fg;
        break;
    case CUI_BUTTON_LINK:
        c.bg = c.bg_hover = c.bg_press = clear;
        c.fg = ui->accent == CUI_ACCENT_NEUTRAL ? fg : CUI_C(ui, ACCENT_STRONG);
        break;
    default: {
        cui_color base = CUI_C(ui, ACTIVE);
        c.bg = selected ? cui_mix(base, fg, 0.10f) : cui_alpha(fg, 0.065f);
        c.bg_hover = cui_alpha(fg, 0.10f);
        c.bg_press = cui_alpha(fg, 0.13f);
        c.fg = fg;
        break;
    }
    }
    return c;
}

typedef struct { float t; } cui__spin_data;
static void cui__paint_spinner(cui_ctx *ui, cui_node *n, void *data) {
    cui_color c = *(cui_color *)data;
    float r = CUI_MIN(n->rect.w, n->rect.h) * 0.5f;
    cui_vec2 ctr = { n->rect.x + n->rect.w * 0.5f, n->rect.y + n->rect.h * 0.5f };
    cui__draw_spinner(ui, ctr, r - 1.0f, CUI_MAX(1.5f, r * 0.22f), c);
}

static void cui__spinner_node(cui_ctx *ui, float size, cui_color c) {
    int i = cui__leaf(ui, 0, CUI_PX(size), CUI_PX(size));
    cui_color *d = cui__frame_alloc(ui, sizeof(cui_color));
    *d = c;
    ui->nodes[i].paint = cui__paint_spinner;
    ui->nodes[i].paint_data = d;
    ui->nodes[i].self_align = CUI_ALIGN_CENTER;
}


/* Icons that change animate: the old one shrinks and fades while the new one
 * scales in. The state is keyed by position so a changing id (for example a
 * tooltip that says "Play" or "Pause") still animates. */
typedef struct { char cur[8], prev[8]; float t; } cui__swap_state;

void cui__icon_swap(cui_ctx *ui, cui_id sid, const char *icon, float size, cui_color c) {
    cui__swap_state *st = cui_state(ui, sid, sizeof *st);
    if (strncmp(st->cur, icon, sizeof st->cur - 1) != 0) {
        if (st->cur[0]) { memcpy(st->prev, st->cur, sizeof st->prev); st->t = 0; }
        else st->t = 1;
        snprintf(st->cur, sizeof st->cur, "%s", icon);
    }
    cui__tween(ui, &st->t, 1.0f, 0.2f);
    if (st->t >= 1.0f || !st->prev[0]) { cui__icon_node(ui, icon, size, c); return; }
    int box = cui__leaf(ui, 0, CUI_PX(size), CUI_PX(size));
    cui_node *b = &ui->nodes[box];
    b->dir = CUI_STACK; b->align = CUI_ALIGN_CENTER; b->justify = CUI_JUSTIFY_CENTER;
    b->self_align = CUI_ALIGN_CENTER;
    float t = st->t;
    cui__open(ui, box);
    cui__icon_node(ui, cui__frame_strdup(ui, st->prev, -1), size, c);
    ui->nodes[ui->node_count - 1].opacity = CUI_MAX(0.001f, 1.0f - t * 1.6f);
    ui->nodes[ui->node_count - 1].scale = 1.0f - 0.45f * t;
    cui__icon_node(ui, icon, size, c);
    ui->nodes[ui->node_count - 1].opacity = CUI_MAX(0.001f, t);
    ui->nodes[ui->node_count - 1].scale = 0.55f + 0.45f * t;
    cui__close(ui);
}

cui_id cui__position_id(cui_ctx *ui, uint32_t salt) {
    int par = cui__current(ui);
    uint32_t key[2] = { par >= 0 ? ui->nodes[par].id : 0, par >= 0 ? (uint32_t)ui->nodes[par].child_count : 0 };
    return cui__hash(key, sizeof key, salt);
}

static bool cui__button_impl(cui_ctx *ui, const char *label, const char *icon_only, const cui_button_opts *o) {
    cui_button_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, icon_only ? (o->tooltip ? o->tooltip : icon_only) : label);
    if (icon_only && o->tooltip) id = cui__hash_str(icon_only, id);
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_pop_disabled(ui);
    /* untouched buttons (most of a long list) keep no state */
    cui__btn_state rest;
    memset(&rest, 0, sizeof rest);
    cui__btn_state *st = cui__state_lazy(ui, id, sizeof *st, r.hovered || r.pressed || r.focused || r.focus_visible);
    if (!st) st = &rest;
    if (st->press.x == 0) st->press.x = 1;
    cui_button_variant v = o->variant;
    if (v == CUI_BUTTON_DEFAULT) v = (icon_only || ui->button_group_depth > 0) ? CUI_BUTTON_GHOST : CUI_BUTTON_SECONDARY;
    cui__btn_colors col = cui__button_colors(ui, v, o->selected);
    if (o->color.r | o->color.g | o->color.b | o->color.a) col.fg = cui_color_of(ui, o->color);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    cui__spring(ui, &st->press, r.pressed ? CUI__PRESS : 1.0f, CUI_SPRING_SNAPPY);

    float h = cui__control_height(ui, o->size);
    float fs = o->size == CUI_SIZE_SM ? ui->theme.text_sm : o->size == CUI_SIZE_LG ? ui->theme.text_lg - 1 : ui->theme.text_body;
    float icon_size = o->size == CUI_SIZE_SM ? 14 : o->size == CUI_SIZE_LG ? 18 : 16;
    float pad = o->size == CUI_SIZE_SM ? 11 : o->size == CUI_SIZE_LG ? 18 : 14;
    if (v == CUI_BUTTON_LINK) pad = 2;

    cui_id swap_id = id ^ 0x1C05A9u;   /* per button: positions repeat across unnamed containers */
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW;
    n->align = CUI_ALIGN_CENTER;
    n->justify = CUI_JUSTIFY_CENTER;
    n->gap = 6;
    n->h = CUI_PX(h);
    if (icon_only) n->w = CUI_PX(h);
    else {
        n->pad[1] = n->pad[3] = pad;
    }
    if (o->full_width) { n->w = CUI_FILL; n->self_align = CUI_ALIGN_STRETCH; }
    else if (o->width.kind != CUI_SIZE_FIT) n->w = o->width;
    else n->self_align = CUI__ALIGN_NOSTRETCH;
    float rad = CUI_MIN(ui->theme.radius_control, h * 0.5f);
    n->radius = CUI_CORNERS(rad);
    cui_color bg = cui_mix(col.bg, col.bg_hover, st->hover);
    if (r.pressed) bg = col.bg_press;
    n->bg = bg;
    if (col.border.a) { n->border = 1; n->border_color = cui_mix(col.border, col.border_hover, st->hover); }
    n->scale = st->press.x;
    n->opacity = disabled ? CUI__DISABLED : 1.0f;
    if (disabled) {
        /* flat while disabled */
    } else if (v == CUI_BUTTON_PRIMARY || v == CUI_BUTTON_DANGER) {
        /* solid buttons are flat color with a faint lit upper rim; no gloss,
         * no drop shadow: they sit in the page, not above it */
        float *k = cui__frame_alloc(ui, sizeof *k);
        *k = r.pressed ? 0.2f : 0.55f;
        n->paint = cui__paint_sheen;
        n->paint_data = k;
    }
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    if (disabled && ui->hot == id) cui__set_cursor(ui, CUI_CURSOR_NOT_ALLOWED);
    cui__open(ui, i);
    const char *icon = icon_only ? icon_only : o->icon;
    if (o->loading) cui__spinner_node(ui, icon_size - 2, col.fg);
    else if (icon) cui__icon_swap(ui, swap_id, icon, icon_size, col.fg);
    if (!icon_only && label && cui__display_len(label) > 0) {
        cui__label_text(ui, label, CUI_FONT_MEDIUM, cui__fs(ui, fs), col.fg);
    }
    if (!icon_only && o->icon_right) cui__icon_node(ui, o->icon_right, icon_size, col.fg);
    cui__close(ui);
    if (v == CUI_BUTTON_LINK) {
        cui__underline_data *u = cui__frame_alloc(ui, sizeof *u);
        u->hover = st->hover; u->color = col.fg;
        ui->nodes[i].paint = cui__paint_underline;
        ui->nodes[i].paint_data = u;
        ui->nodes[i].flags |= CUI_NF_PAINT_AFTER;
    } else {
        cui__focus_ring(ui, i, st->focus);
    }
    ui->last_id = id;
    ui->last_node = i;
    if (o->tooltip) {
        if (o->shortcut) cui_tooltip(ui, cui__frame_printf(ui, "%s  \xc2\xb7  %s", o->tooltip, o->shortcut));
        else cui_tooltip(ui, o->tooltip);
    }
    return r.clicked && !o->loading && !disabled;
}

bool cui_button_ex(cui_ctx *ui, const char *label, const cui_button_opts *o) {
    return cui__button_impl(ui, label, NULL, o);
}

bool cui_icon_button_ex(cui_ctx *ui, const char *icon, const cui_button_opts *o) {
    return cui__button_impl(ui, NULL, icon, o);
}

/* ------------------------------------------------------------------------ */
/* Badges and avatars                                                       */
/* ------------------------------------------------------------------------ */

void cui_badge_ex(cui_ctx *ui, const char *text, const cui_badge_opts *o) {
    cui_badge_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_color tone = cui__tone_color(ui, o->tone);
    bool neutral = o->tone == CUI_TONE_NEUTRAL;
    float h = o->size == CUI_SIZE_SM ? 20 : o->size == CUI_SIZE_LG ? 26 : 22;
    int i = cui__node_new(ui, 0);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 5;
    n->h = CUI_PX(h);
    n->pad[1] = n->pad[3] = o->size == CUI_SIZE_SM ? 7 : 8;
    n->radius = CUI_CORNERS(o->size == CUI_SIZE_SM ? 5 : 6);
    n->self_align = CUI__ALIGN_NOSTRETCH;
    cui_color fg;
    if (o->solid) {
        n->bg = neutral ? CUI_C(ui, FOREGROUND) : tone;
        fg = neutral ? CUI_C(ui, BACKGROUND) : (ui->mode == CUI_THEME_DARK ? cui__rgba(20, 20, 22, 255) : cui__rgba(255, 255, 255, 255));
    } else {
        /* a tint, no edge: a badge is a label, not a control */
        cui_color surf = CUI_C(ui, SURFACE);
        n->bg = neutral ? CUI_C(ui, ACTIVE) : cui_mix(surf, tone, ui->mode == CUI_THEME_DARK ? 0.18f : 0.12f);
        fg = neutral ? CUI_C(ui, TEXT_SECONDARY) : cui__legible(cui_mix(CUI_C(ui, FOREGROUND), tone, ui->mode == CUI_THEME_DARK ? 0.80f : 0.88f), n->bg, 4.5f);
    }
    cui__open(ui, i);
    if (o->dot) {
        int dot = cui__leaf(ui, 0, CUI_PX(6), CUI_PX(6));
        ui->nodes[dot].bg = o->solid ? fg : (neutral ? CUI_C(ui, TEXT_MUTED) : tone);
        ui->nodes[dot].radius = CUI_CORNERS(3);
    }
    if (o->icon) cui__icon_node(ui, o->icon, 12, fg);
    int t = cui__text_node(ui, text, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), fg, false);
    ui->nodes[t].line_height = 1.2f;
    cui__close(ui);
}

static cui_color cui__name_tone(cui_ctx *ui, const char *name) {
    cui_theme *t = &ui->theme;
    /* deep yellows turn to olive and brown, so dark mode trades amber for indigo */
    cui_color fourth = ui->mode == CUI_THEME_DARK ? cui_mix(t->tone_sky, t->tone_violet, 0.5f) : t->tone_amber;
    cui_color tones[7] = { t->tone_sky, t->tone_violet, t->tone_teal, fourth, t->tone_rose, t->tone_green, t->tone_orchid };
    uint32_t h = cui__hash_str(name ? name : "", 7);
    return tones[h % 7];
}

static int cui__initials(const char *name, char *out) {
    int n = 0;
    bool word_start = true;
    for (const char *p = name; p && *p && n < 8; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '-' || c == '.') { word_start = true; continue; }
        if (word_start) {
            uint32_t cp;
            int k = cui__utf8_decode(p, (int)strlen(p), &cp);
            if (cp < 128 && cp >= 'a' && cp <= 'z') cp -= 32;
            n += cui__utf8_encode(cp, out + n);
            p += k - 1;
            word_start = false;
            if (n >= 2 && cp < 128) break;
        }
    }
    out[n] = 0;
    return n;
}

typedef struct { cui_texture tex; float ring; cui_color ring_color; } cui__avatar_paint;

static void cui__paint_avatar_img(cui_ctx *ui, cui_node *n, void *data) {
    cui__avatar_paint *p = data;
    cui_rect uv = { 0, 0, 1, 1 };
    if (p->tex) cui_draw_image(ui, p->tex, n->rect, uv, n->radius, cui__rgba(255, 255, 255, 255));
}

void cui_avatar_ex(cui_ctx *ui, const char *name, const cui_avatar_opts *o) {
    cui_avatar_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float size = o->size > 0 ? o->size : 32;
    cui_color tone = cui__name_tone(ui, name);
    int i = cui__node_new(ui, 0);
    cui_node *n = &ui->nodes[i];
    n->w = CUI_PX(size); n->h = CUI_PX(size);
    n->dir = CUI_STACK; n->align = CUI_ALIGN_CENTER; n->justify = CUI_JUSTIFY_CENTER;
    n->radius = CUI_CORNERS(o->square ? size * 0.28f : size * 0.5f);
    n->self_align = CUI_ALIGN_CENTER;
    bool dark = ui->mode == CUI_THEME_DARK;
    if (o->image) {
        cui__avatar_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->tex = o->image;
        n->paint = cui__paint_avatar_img;
        n->paint_data = p;
    } else {
        /* dark discs are a deep, clean step of the hue rather than the hue mixed into gray */
        n->bg = dark ? cui__shade(tone, 0.35f, 0.6f) : cui_mix(CUI_C(ui, SURFACE), tone, 0.16f);
        n->border = 1;   /* a fine rim of the same hue gives the disc an edge on any surface */
        n->border_color = dark ? cui__shade(tone, 0.45f, 0.6f) : cui_alpha(tone, 0.28f);
    }
    cui__open(ui, i);
    if (!o->image) {
        char ini[16];
        cui__initials(name, ini);
        int t = cui__text_node(ui, ini, -1, CUI_FONT_MEDIUM, size * 0.38f, dark ? cui__shade(tone, 0.88f, 0.7f) : cui_mix(CUI_C(ui, FOREGROUND), tone, 0.78f), false);
        ui->nodes[t].line_height = 1.0f;
    }
    if (o->status) {
        cui_color sc = strcmp(o->status, "online") == 0 ? CUI_C(ui, SUCCESS) : strcmp(o->status, "busy") == 0 ? CUI_C(ui, DANGER)
                     : strcmp(o->status, "away") == 0 ? CUI_C(ui, WARNING) : CUI_C(ui, TEXT_MUTED);
        float ds = CUI_MAX(8, size * 0.28f);
        int dot = cui__absolute(ui, 0, size - ds + 1, size - ds + 1, ds, ds);
        cui_node *dn = &ui->nodes[dot];
        dn->bg = sc; dn->radius = CUI_CORNERS(ds * 0.5f);
        dn->border = 2; dn->border_color = CUI_C(ui, SURFACE);
    }
    cui__close(ui);
}

void cui_avatar_group(cui_ctx *ui, const char *const *names, int count, int max_visible, float size) {
    if (size <= 0) size = 32;
    if (max_visible <= 0) max_visible = 4;
    int shown = count < max_visible ? count : max_visible;
    int extra = count - shown;
    float overlap = size * 0.16f;   /* reads as a group; with the 2 pt ring the next avatar stops short of two wide initials ("DM") */
    int slots = shown + (extra > 0 ? 1 : 0);
    float total_w = slots * size - (slots - 1) * overlap;
    int row = cui__node_new(ui, 0);
    cui_node *r = &ui->nodes[row];
    r->w = CUI_PX(total_w); r->h = CUI_PX(size);
    r->self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    for (int k = 0; k < slots; k++) {
        float x = k * (size - overlap);
        int ring = cui__absolute(ui, 0, x - 2, -2, size + 4, size + 4);
        cui_node *rn = &ui->nodes[ring];
        rn->bg = CUI_C(ui, SURFACE);
        rn->radius = CUI_CORNERS(size * 0.5f + 2);
        rn->dir = CUI_STACK; rn->align = CUI_ALIGN_CENTER; rn->justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, ring);
        if (k < shown) {
            cui_avatar_opts ao;
            memset(&ao, 0, sizeof ao);
            ao.size = size;
            cui_avatar_ex(ui, names[k], &ao);
        } else {
            int b = cui__node_new(ui, 0);
            cui_node *bn = &ui->nodes[b];
            bn->w = CUI_PX(size); bn->h = CUI_PX(size);
            bn->radius = CUI_CORNERS(size * 0.5f);
            bn->bg = CUI_C(ui, SURFACE_MUTED);
            bn->dir = CUI_STACK; bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
            cui__open(ui, b);
            char buf[16];
            snprintf(buf, sizeof buf, "+%d", extra);
            int t = cui__text_node(ui, buf, -1, CUI_FONT_MEDIUM, size * 0.36f, CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[t].line_height = 1.0f;
            cui__close(ui);
        }
        cui__close(ui);
    }
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Cards                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct { float hover; float sel; cui_spring lift; } cui__card_state;

bool cui_card_begin(cui_ctx *ui, const cui_card_opts *o) {
    cui_card_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = o->id ? cui_make_id(ui, o->id) : 0;
    bool clicked = false;
    float hover = 0;
    cui__card_state *st = NULL;
    cui_interact r;
    memset(&r, 0, sizeof r);
    if (o->hoverable) {
        if (!id) id = cui__hash_str("card", (cui_id)ui->node_count * 2654435761u);
        r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
        st = cui_state(ui, id, sizeof *st);
        cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        cui__spring(ui, &st->lift, r.hovered ? -2.0f : 0.0f, CUI_SPRING_SMOOTH);
        hover = st->hover;
        clicked = r.clicked;
    }
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    float pad = o->pad > 0 ? o->pad : 24;
    float px = o->pad_x > 0 ? o->pad_x : pad, py = o->pad_y > 0 ? o->pad_y : pad;
    n->pad[0] = n->pad[2] = py;
    n->pad[1] = n->pad[3] = px;
    n->gap = o->gap > 0 ? o->gap : 12;
    n->dir = (uint8_t)o->dir;
    n->align = (uint8_t)o->align;
    n->w = o->width;
    n->h = o->height;
    if (o->min_width > 0) n->min_w = o->min_width;
    if (o->max_width > 0) n->max_w = o->max_width;
    if (o->wrap && o->dir == CUI_ROW) n->flags |= CUI_NF_ROW_WRAP;
    n->radius = CUI_CORNERS(ui->theme.radius_surface);
    if (o->muted) n->bg = CUI_C(ui, SURFACE_MUTED);
    else {
        n->bg = CUI_C(ui, SURFACE);
        n->border = 1;
        n->border_color = o->selected ? CUI_C(ui, ACCENT) : cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), hover);
    }
    if (o->hoverable) {
        n->elevation = CUI_ELEVATION_RAISED;
        n->elevation_alpha = hover;
        n->ty = st->lift.x;
        cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    }
    cui__open(ui, i);
    if (id) ui->last_id = id;
    return clicked;
}

void cui_card_end(cui_ctx *ui) { cui__close(ui); }

/* ------------------------------------------------------------------------ */
/* Feedback                                                                 */
/* ------------------------------------------------------------------------ */

void cui_spinner(cui_ctx *ui, float size) {
    cui__spinner_node(ui, size > 0 ? size : 20, CUI_C(ui, TEXT_SECONDARY));
}

typedef struct { float value; bool indeterminate; cui_color track, fill; double time; } cui__progress_paint;

static void cui__paint_progress(cui_ctx *ui, cui_node *n, void *data) {
    cui__progress_paint *p = data;
    cui_rect r = n->rect;
    float rad = r.h * 0.5f;
    cui_draw_rect(ui, r, CUI_CORNERS(rad), p->track);
    if (p->indeterminate) {
        float t = (float)fmod(p->time * 0.75, 1.0);
        float w = r.w * 0.35f;
        float x = r.x - w + (r.w + w) * (t * t * (3 - 2 * t));
        cui_push_clip(ui, r);
        cui_rect f = { x, r.y, w, r.h };
        cui_draw_rect(ui, f, CUI_CORNERS(rad), p->fill);
        cui_pop_clip(ui);
        ui->animating = true;
    } else if (p->value > 0) {
        cui_rect f = { r.x, r.y, CUI_MAX(r.h, r.w * cui__clampf(p->value, 0, 1)), r.h };
        cui_draw_rect(ui, f, CUI_CORNERS(rad), p->fill);
    }
}

void cui_progress_ex(cui_ctx *ui, float value, const cui_progress_opts *o) {
    cui_progress_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, o->label ? o->label : "progress");
    id = cui__hash(&ui->node_count, sizeof ui->node_count, id);
    float shown = cui_animate(ui, id, cui__clampf(value, 0, 1));
    bool col = o->label || o->show_value;
    if (col) {
        int c = cui__node_new(ui, 0);
        ui->nodes[c].gap = 8;
        ui->nodes[c].w = o->width;
        cui__open(ui, c);
        int row = cui__node_new(ui, 0);
        ui->nodes[row].dir = CUI_ROW;
        cui__open(ui, row);
        if (o->label) cui__label_text(ui, o->label, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND));
        cui_spacer(ui);
        if (o->show_value && !o->indeterminate) {
            int t = cui__text_node(ui, cui__frame_printf(ui, "%d%%", (int)(value * 100 + 0.5f)), -1, CUI_FONT_BODY,
                                   cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[t].flags |= CUI_NF_TABULAR;
            ui->nodes[t].line_height = 1.3f;
        }
        cui__close(ui);
    }
    float h = o->height > 0 ? o->height : 6;
    int i = cui__leaf(ui, 0, col ? CUI_FILL : (o->width.kind ? o->width : CUI_FILL), CUI_PX(h));
    /* full width bars stretch; a bar with its own width centers in a row like its siblings */
    ui->nodes[i].self_align = (!col && o->width.kind) ? CUI__ALIGN_NOSTRETCH : CUI_ALIGN_STRETCH;
    cui__progress_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->value = shown;
    p->indeterminate = o->indeterminate;
    p->time = ui->time;
    p->track = CUI_C(ui, CONTROL_TRACK);
    p->fill = o->tone ? cui__tone_color(ui, o->tone) : CUI_C(ui, CONTROL_ON);
    ui->nodes[i].paint = cui__paint_progress;
    ui->nodes[i].paint_data = p;
    if (col) cui__close(ui);
}

typedef struct { double time; cui_color base, shine; } cui__skel_paint;
static void cui__paint_skeleton(cui_ctx *ui, cui_node *n, void *data) {
    cui__skel_paint *p = data;
    cui_rect r = n->rect;
    cui_draw_rect(ui, r, n->radius, p->base);
    if (ui->theme.motion > 0) {
        float t = (float)fmod(p->time / 1.6, 1.0);
        float w = CUI_MAX(80.0f, r.w * 0.4f);
        float x = r.x - w + (r.w + 2 * w) * t;
        cui_push_clip(ui, r);
        cui_cvert v[6];
        cui_color clear = p->shine; clear.a = 0;
        float xs[3] = { x, x + w * 0.5f, x + w };
        cui_color cs[3] = { clear, p->shine, clear };
        for (int k = 0; k < 3; k++) { v[k * 2].x = xs[k]; v[k * 2].y = r.y; v[k * 2].c = cs[k]; v[k * 2 + 1].x = xs[k]; v[k * 2 + 1].y = r.y + r.h; v[k * 2 + 1].c = cs[k]; }
        uint32_t idx[12] = { 0, 2, 3, 0, 3, 1, 2, 4, 5, 2, 5, 3 };
        cui__draw_triangles(ui, v, 6, idx, 12);
        cui_pop_clip(ui);
        ui->animating = true;
    }
}

void cui_skeleton(cui_ctx *ui, cui_size width, float height) {
    int i = cui__leaf(ui, 0, width.kind == CUI_SIZE_FIT ? CUI_FILL : width, CUI_PX(height > 0 ? height : 14));
    cui_node *n = &ui->nodes[i];
    n->radius = CUI_CORNERS(CUI_MIN(8.0f, (height > 0 ? height : 14) * 0.5f));
    if (width.kind != CUI_SIZE_FIT && width.kind != CUI_SIZE_GROW) n->self_align = CUI__ALIGN_NOSTRETCH;
    cui__skel_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->time = ui->time;
    p->base = CUI_C(ui, ACTIVE);
    p->shine = cui_alpha(CUI_C(ui, FOREGROUND), ui->mode == CUI_THEME_DARK ? 0.06f : 0.05f);
    n->paint = cui__paint_skeleton;
    n->paint_data = p;
}

typedef struct { bool dismissed; float t; } cui__alert_state;

bool cui_alert_ex(cui_ctx *ui, const char *title, const cui_alert_opts *o) {
    cui_alert_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, title) ^ 0xA1E70010u;
    cui__alert_state *st = cui_state(ui, id, sizeof *st);
    if (st->dismissed) return false;
    cui_color tone = o->tone == CUI_TONE_NEUTRAL ? CUI_C(ui, TEXT_SECONDARY) : cui__tone_color(ui, o->tone);
    const char *icon = o->icon;
    if (!icon) {
        switch (o->tone) {
        case CUI_TONE_SUCCESS: icon = CUI_ICON_CIRCLE_CHECK; break;
        case CUI_TONE_WARNING: icon = CUI_ICON_TRIANGLE_ALERT; break;
        case CUI_TONE_DANGER: icon = CUI_ICON_CIRCLE_ALERT; break;
        default: icon = CUI_ICON_INFO; break;
        }
    }
    bool dark = ui->mode == CUI_THEME_DARK;
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_START; n->gap = 12;
    n->pad[0] = n->pad[2] = 14; n->pad[1] = 14; n->pad[3] = 16;
    n->radius = CUI_CORNERS(ui->theme.radius_control);
    n->bg = o->tone == CUI_TONE_NEUTRAL ? CUI_C(ui, SURFACE_MUTED) : cui_mix(CUI_C(ui, SURFACE), tone, dark ? 0.12f : 0.07f);
    n->border = 1;
    n->border_color = o->tone == CUI_TONE_NEUTRAL ? CUI_C(ui, BORDER) : cui_mix(CUI_C(ui, BORDER), tone, dark ? 0.2f : 0.12f);   /* the tint carries the tone; the edge stays quiet */
    cui__open(ui, i);
    int ib = cui__node_new(ui, 0);
    ui->nodes[ib].pad[0] = 1;
    cui__open(ui, ib);
    cui__icon_node(ui, icon, 16, tone);
    cui__close(ui);
    int col = cui__node_new(ui, 0);
    ui->nodes[col].gap = 3;
    ui->nodes[col].w = CUI_FILL;
    cui__open(ui, col);
    int t = cui__text_node(ui, title, cui__display_len(title), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), true);
    ui->nodes[t].line_height = 1.4f;
    if (o->description) {
        /* on a tinted surface, the body text leans toward the same hue instead of a neutral gray */
        cui_color body = o->tone == CUI_TONE_NEUTRAL ? CUI_C(ui, TEXT_SECONDARY) : cui_mix(CUI_C(ui, TEXT_SECONDARY), tone, dark ? 0.18f : 0.22f);
        cui__text_node(ui, o->description, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), body, true);
    }
    cui__close(ui);
    bool keep = true;
    if (o->dismissible) {
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM;
        bo.tooltip = "Dismiss";
        cui_push_id(ui, title);
        if (cui_icon_button_ex(ui, CUI_ICON_X, &bo)) { st->dismissed = true; keep = false; }
        cui_pop_id(ui);
        ui->nodes[ui->last_node].h = CUI_PX(24);
        ui->nodes[ui->last_node].w = CUI_PX(24);
    }
    cui__close(ui);
    return keep;
}

bool cui_empty_state_ex(cui_ctx *ui, const char *title, const cui_empty_opts *o) {
    cui_empty_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int i = cui__node_new(ui, 0);
    cui_node *n = &ui->nodes[i];
    n->align = CUI_ALIGN_CENTER; n->gap = 6;
    n->pad[0] = n->pad[2] = 40; n->pad[1] = n->pad[3] = 24;
    n->w = CUI_FILL;
    cui__open(ui, i);
    if (o->icon) {
        int ic = cui__node_new(ui, 0);
        cui_node *in = &ui->nodes[ic];
        in->w = CUI_PX(32); in->h = CUI_PX(32);   /* the icon alone, larger and quiet; no tile */
        in->dir = CUI_STACK; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, ic);
        cui__icon_node(ui, o->icon, 28, CUI_C(ui, TEXT_MUTED));
        cui__close(ui);
        cui_gap(ui, 8);
    }
    int t = cui__text_node(ui, title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_lg), CUI_C(ui, FOREGROUND), true);
    ui->nodes[t].text_align = CUI_TEXT_CENTER;
    ui->nodes[t].self_align = CUI_ALIGN_CENTER;
    if (o->description) {
        int dsc = cui__text_node(ui, o->description, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), true);
        ui->nodes[dsc].text_align = CUI_TEXT_CENTER;
        ui->nodes[dsc].max_w = 360;
        ui->nodes[dsc].self_align = CUI_ALIGN_CENTER;
    }
    bool clicked = false;
    if (o->action) {
        cui_gap(ui, 10);
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.variant = CUI_BUTTON_PRIMARY;
        bo.icon = CUI_ICON_PLUS;
        clicked = cui_button_ex(ui, o->action, &bo);
        ui->nodes[ui->last_node].self_align = CUI_ALIGN_CENTER;
    }
    cui__close(ui);
    return clicked;
}

void cui_sparkline(cui_ctx *ui, const float *values, int count, cui_size width, float height, cui_color color);

void cui_metric_ex(cui_ctx *ui, const char *label, const char *value, const cui_metric_opts *o) {
    cui_metric_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_card_opts co;
    memset(&co, 0, sizeof co);
    co.pad = 20; co.gap = 10;
    co.width = o->width.kind ? o->width : CUI_FILL;
    cui_card_begin(ui, &co);
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 8;
    cui__open(ui, row);
    if (o->icon) cui__icon_node(ui, o->icon, 16, CUI_C(ui, TEXT_SECONDARY));
    int lt = cui__text_node(ui, label, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), false);
    ui->nodes[lt].line_height = 1.3f;
    cui__close(ui);
    int vr = cui__node_new(ui, 0);
    ui->nodes[vr].dir = CUI_ROW; ui->nodes[vr].align = CUI_ALIGN_END; ui->nodes[vr].gap = 10;
    cui__open(ui, vr);
    int vt = cui__text_node(ui, value, -1, CUI_FONT_DISPLAY, cui__fs(ui, 28), CUI_C(ui, FOREGROUND), false);
    ui->nodes[vt].line_height = 1.1f;
    ui->nodes[vt].flags |= CUI_NF_TABULAR;
    if (o->delta != 0 || o->delta_label) {
        bool up = o->delta >= 0;
        bool good = o->lower_is_better ? o->delta <= 0 : o->delta >= 0;   /* the arrow shows direction, the color shows meaning */
        cui_color tc = good ? CUI_C(ui, SUCCESS) : CUI_C(ui, DANGER);
        int dr = cui__node_new(ui, 0);
        ui->nodes[dr].dir = CUI_ROW; ui->nodes[dr].gap = 3; ui->nodes[dr].pad[2] = 4;
        cui__open(ui, dr);
        cui__icon_node(ui, up ? CUI_ICON_TRENDING_UP : CUI_ICON_TRENDING_DOWN, 14, tc);
        const char *txt = o->delta_label ? o->delta_label : cui__frame_printf(ui, "%+.1f%%", (double)o->delta);
        int dt = cui__text_node(ui, txt, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), tc, false);
        ui->nodes[dt].line_height = 1.2f;
        ui->nodes[dt].flags |= CUI_NF_TABULAR;
        cui__close(ui);
    }
    cui__close(ui);
    if (o->spark && o->spark_count > 1) {
        cui_gap(ui, 2);
        cui_sparkline(ui, o->spark, o->spark_count, CUI_FILL, 36, ((o->lower_is_better ? o->delta > 0 : o->delta < 0)) ? CUI_COLOR_DANGER : CUI_COLOR_SERIES(0));
    }
    cui_card_end(ui);
}

void cui_meter(cui_ctx *ui, const char *label, float used, float total, const char *unit) {
    float frac = total > 0 ? used / total : 0;
    int c = cui__node_new(ui, 0);
    ui->nodes[c].gap = 8;
    cui__open(ui, c);
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 6;
    cui__open(ui, row);
    cui__label_text(ui, label, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND));
    cui_spacer(ui);
    int t = cui__text_node(ui, cui__frame_printf(ui, "%g / %g %s", (double)used, (double)total, unit ? unit : ""), -1,
                           CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), false);
    ui->nodes[t].flags |= CUI_NF_TABULAR;
    ui->nodes[t].line_height = 1.3f;
    cui__close(ui);
    cui_progress_opts po;
    memset(&po, 0, sizeof po);
    po.tone = frac > 0.9f ? CUI_TONE_DANGER : frac > 0.75f ? CUI_TONE_WARNING : CUI_TONE_NEUTRAL;
    cui_push_id(ui, label);
    cui_progress_ex(ui, frac, &po);
    cui_pop_id(ui);
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Images and canvas                                                        */
/* ------------------------------------------------------------------------ */

cui_texture cui_image_create(cui_ctx *ui, int w, int h, const void *rgba) {
    if (!ui->renderer.texture_create) return 0;
    return ui->renderer.texture_create(ui->renderer.user, w, h, CUI_TEXTURE_RGBA8, rgba);
}

void cui_image_destroy(cui_ctx *ui, cui_texture tex) {
    if (tex && ui->renderer.texture_destroy) ui->renderer.texture_destroy(ui->renderer.user, tex);
}

typedef struct { cui_texture tex; cui_color tint; } cui__img_paint;
static void cui__paint_image(cui_ctx *ui, cui_node *n, void *data) {
    cui__img_paint *p = data;
    cui_rect uv = { 0, 0, 1, 1 };
    cui_draw_image(ui, p->tex, n->rect, uv, n->radius, p->tint);
}

void cui_image_ex(cui_ctx *ui, cui_texture tex, const cui_image_opts *o) {
    cui_image_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int i = cui__leaf(ui, 0, o->width.kind ? o->width : CUI_PX(64), o->height.kind ? o->height : CUI_PX(64));
    cui_node *n = &ui->nodes[i];
    n->radius = CUI_CORNERS(o->radius);
    n->self_align = CUI__ALIGN_NOSTRETCH;
    cui__img_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->tex = tex;
    p->tint = cui__color_set(o->tint) ? cui_color_of(ui, o->tint) : cui__rgba(255, 255, 255, 255);
    n->paint = cui__paint_image;
    n->paint_data = p;
}

typedef struct { cui_paint_fn fn; void *user; } cui__canvas_paint;
static void cui__paint_canvas(cui_ctx *ui, cui_node *n, void *data) {
    cui__canvas_paint *p = data;
    cui_push_clip(ui, n->rect);
    p->fn(ui, n->rect, p->user);
    cui_pop_clip(ui);
}

cui_canvas_result cui_canvas(cui_ctx *ui, const char *id_str, cui_size w, cui_size h, cui_paint_fn paint, const void *user, size_t user_size) {
    cui_canvas_result res;
    memset(&res, 0, sizeof res);
    cui_id id = cui_make_id(ui, id_str ? id_str : "canvas");
    cui_interact r = cui__interact(ui, id, CUI_IF_DRAG);
    res.rect = cui__prev_rect(ui, id);
    res.hovered = r.hovered; res.pressed = r.pressed; res.clicked = r.clicked;
    res.mouse.x = ui->mx - res.rect.x; res.mouse.y = ui->my - res.rect.y;
    int i = cui__leaf(ui, id, w, h);
    cui__mark_interactive(ui, i, 0);
    if (paint) {
        cui__canvas_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->fn = paint;
        if (user && user_size) { p->user = cui__frame_alloc(ui, user_size); memcpy(p->user, user, user_size); }
        else p->user = (void *)user;
        ui->nodes[i].paint = cui__paint_canvas;
        ui->nodes[i].paint_data = p;
    }
    ui->last_id = id;
    return res;
}

/* ------------------------------------------------------------------------ */
/* Scroll areas                                                             */
/* ------------------------------------------------------------------------ */

#define CUI__BAR_ZONE 12.0f

int cui__scroll_begin_ex(cui_ctx *ui, cui_id id, const cui_box *box, bool horizontal) {
    cui_scroll_state *st = cui_state(ui, id, sizeof *st);
    cui_rect pr = cui__prev_rect(ui, id);
    float max_y = CUI_MAX(0, st->content_h - st->view_h);
    float max_x = CUI_MAX(0, st->content_w - st->view_w);
    if (ui->scroll_target == id && (ui->scroll_dy != 0 || ui->scroll_dx != 0)) {
        float line = 44;
        float dy = ui->scroll_dy, dx = ui->scroll_dx;
        if ((ui->mods & CUI_MOD_SHIFT) && horizontal && dx == 0) { dx = dy; dy = 0; }
        if (max_y > 0) st->ty -= dy * line;
        if (horizontal && max_x > 0) st->tx -= dx * line;
        st->last_activity = ui->time;
        ui->scroll_dy = ui->scroll_dx = 0;   /* consumed */
    }
    /* scrollbar dragging */
    cui_id bar_id = id ^ 0x5C5C5C5Cu;
    cui_interact br = cui__interact(ui, bar_id, CUI_IF_DRAG | CUI_IF_KEEP_FOCUS);
    st->hover_bar = br.hovered;
    cui_rect bar = pr;
    bar.y += st->bar_top_inset;
    bar.h = CUI_MAX(0, bar.h - st->bar_top_inset);
    if (max_y > 0 && bar.h > 6) {
        float thumb_h = CUI_MIN(bar.h - 6, CUI_MAX(28.0f, bar.h * bar.h / CUI_MAX(1, st->content_h - st->bar_top_inset)));
        float track = bar.h - thumb_h - 6;
        float thumb_y = bar.y + 3 + (max_y > 0 ? st->y / max_y * track : 0);
        if (br.pressed && ui->mpressed[0]) {
            st->drag_y = true;
            st->drag_grab = (ui->my >= thumb_y && ui->my <= thumb_y + thumb_h) ? ui->my - thumb_y : thumb_h * 0.5f;
        }
        if (st->drag_y && ui->mdown[0]) {
            float ny = (ui->my - bar.y - 3 - st->drag_grab) / (track > 1 ? track : 1) * max_y;
            st->ty = st->y = cui__clampf(ny, 0, max_y);
            st->last_activity = ui->time;
        }
    }
    if (!ui->mdown[0]) st->drag_y = false;
    st->ty = cui__clampf(st->ty, 0, max_y);
    st->tx = cui__clampf(st->tx, 0, max_x);
    cui__tween(ui, &st->y, st->ty, 0.14f);
    cui__tween(ui, &st->x, st->tx, 0.14f);
    if (fabsf(st->y - st->ty) < 0.25f) st->y = st->ty;
    if (fabsf(st->x - st->tx) < 0.25f) st->x = st->tx;
    bool recent = st->last_activity > 0 && ui->time - st->last_activity < 0.9;
    bool show = st->always_show_bar || recent || st->hover_bar || st->drag_y;
    if (recent) cui__wakeup_after(ui, (float)(0.9 - (ui->time - st->last_activity)) + 0.01f);
    cui__tween(ui, &st->bar_t, show ? 1.0f : 0.0f, show ? 0.12f : 0.3f);

    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    cui__apply_box(ui, n, box);
    n->flags |= CUI_NF_SCROLL_Y | (horizontal ? CUI_NF_SCROLL_X : 0);
    float s = ui->scale;
    n->scroll_y = roundf(st->y * s) / s;
    n->scroll_x = roundf(st->x * s) / s;
    cui__open(ui, i);
    return i;
}

void cui__scroll_end_ex(cui_ctx *ui) { cui__close(ui); }

void cui__paint_scrollbars(cui_ctx *ui, cui_node *n, cui_rect clip) {
    cui_scroll_state *st = cui_state(ui, n->id, sizeof *st);
    cui_rect r = n->rect;
    float max_y = st->content_h - r.h;
    if (st->fade.a > 0 && max_y > 0.5f) {
        /* each edge fades in as content moves past it */
        float fh = CUI_MIN(36.0f, r.h * 0.2f);
        float top = cui__clampf(st->y / fh, 0, 1), bot = cui__clampf((max_y - st->y) / fh, 0, 1);
        cui_color clear = st->fade; clear.a = 0;
        cui_corners rc = n->radius;
        if (top > 0) cui_draw_rect_gradient(ui, (cui_rect){ r.x, r.y, r.w, fh }, (cui_corners){ rc.tl, rc.tr, 0, 0 }, cui_alpha(st->fade, top), clear);
        if (bot > 0) cui_draw_rect_gradient(ui, (cui_rect){ r.x, r.y + r.h - fh, r.w, fh }, (cui_corners){ 0, 0, rc.br, rc.bl }, clear, cui_alpha(st->fade, bot));
    }
    float max_x = st->content_w - r.w;
    if (st->fade.a > 0 && max_x > 0.5f && (n->flags & CUI_NF_SCROLL_X)) {
        /* sideways: vertex colors make a smooth eased ramp. Where the area
         * meets the rounded corners of the box it sits in, the ramp stops short
         * of them so it never paints outside the rounded outline. */
        float in_top = 0, in_bot = 0;
        for (int a = n->parent; a >= 0; a = ui->nodes[a].parent) {
            cui_node *an = &ui->nodes[a];
            if (!(an->flags & CUI_NF_CLIP) || an->radius.bl + an->radius.br + an->radius.tl + an->radius.tr <= 0) continue;
            if (fabsf(an->rect.y + an->rect.h - (r.y + r.h)) < 2.0f) in_bot = CUI_MAX(an->radius.bl, an->radius.br);
            if (fabsf(an->rect.y - r.y) < 2.0f) in_top = CUI_MAX(an->radius.tl, an->radius.tr);
            break;
        }
        float fy0 = r.y + in_top, fy1 = r.y + r.h - in_bot;
        if (fy1 <= fy0) { fy0 = r.y; fy1 = r.y + r.h; }
        float fw = CUI_MIN(36.0f, r.w * 0.15f);
        /* a scroll area squeezed to no width has no edges to fade (and dividing by 0 gives NaN) */
        float lt = fw > 0 ? cui__clampf(st->x / fw, 0, 1) : 0, rt = fw > 0 ? cui__clampf((max_x - st->x) / fw, 0, 1) : 0;
        cui_color clear = st->fade; clear.a = 0;
        for (int side = 0; side < 2; side++) {
            float k = side ? rt : lt;
            if (k <= 0) continue;
            enum { S = 6 };
            cui_cvert v[(S + 1) * 2];
            uint32_t idx[S * 6];
            for (int q = 0; q <= S; q++) {
                float u = (float)q / S;                      /* 0 at the edge, 1 inside */
                float a = 1 - u * u * (3 - 2 * u);
                float x = side ? r.x + r.w - u * fw : r.x + u * fw;
                if (q == 0) x += side ? 1.0f : -1.0f;   /* past the edge pixel the clip rounds out to */
                cui_color c = cui_alpha(st->fade, a * k);
                if (c.a == 0) c = clear;
                v[q * 2] = (cui_cvert){ x, fy0, c };
                v[q * 2 + 1] = (cui_cvert){ x, fy1, c };
            }
            for (int q = 0; q < S; q++) {
                uint32_t b = (uint32_t)(q * 2);
                uint32_t *t = idx + q * 6;
                t[0] = b; t[1] = b + 1; t[2] = b + 2; t[3] = b + 1; t[4] = b + 3; t[5] = b + 2;
            }
            cui__draw_triangles(ui, v, (S + 1) * 2, idx, S * 6);
        }
    }
    cui_rect bar = r;
    bar.y += st->bar_top_inset;
    bar.h = CUI_MAX(0, bar.h - st->bar_top_inset);
    if (max_y > 0.5f && bar.h > 6) {
        cui_rect zone = { r.x + r.w - CUI__BAR_ZONE, bar.y, CUI__BAR_ZONE, bar.h };
        cui__paint_add_hit(ui, n->id ^ 0x5C5C5C5Cu, cui__rect_intersect(zone, clip), CUI_NF_INTERACTIVE);
        if (st->bar_t > 0.01f) {
            float thumb_h = CUI_MIN(bar.h - 6, CUI_MAX(28.0f, bar.h * bar.h / CUI_MAX(1, st->content_h - st->bar_top_inset)));
            float track = bar.h - thumb_h - 6;
            float y = bar.y + 3 + cui__clampf(st->y / max_y, 0, 1) * track;
            bool big = st->hover_bar || st->drag_y;
            float w = big ? 8 : 6;
            cui_rect th = { r.x + r.w - w - 3, y, w, thumb_h };
            cui_color c = cui_alpha(CUI_C(ui, FOREGROUND), (big ? 0.38f : 0.26f) * st->bar_t);
            cui_draw_rect(ui, th, CUI_CORNERS(w * 0.5f), c);
        }
    }
    if ((n->flags & CUI_NF_SCROLL_X) && max_x > 0.5f && st->bar_t > 0.01f) {
        float thumb_w = CUI_MAX(28.0f, r.w * r.w / st->content_w);
        float track = r.w - thumb_w - 6;
        float x = r.x + 3 + cui__clampf(st->x / max_x, 0, 1) * track;
        cui_rect th = { x, r.y + r.h - 9, thumb_w, 6 };
        cui_draw_rect(ui, th, CUI_CORNERS(3), cui_alpha(CUI_C(ui, FOREGROUND), 0.26f * st->bar_t));
    }
}

void cui_scroll_begin(cui_ctx *ui, const cui_scroll_opts *o) {
    cui_scroll_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = o->id ? cui_make_id(ui, o->id) : cui_make_id(ui, cui__frame_printf(ui, "#scroll%d", ui->node_count));
    cui_box b;
    memset(&b, 0, sizeof b);
    b.width = o->width;
    b.height = o->height.kind == CUI_SIZE_FIT ? CUI_FILL : o->height;
    b.pad = o->pad;
    b.gap = o->gap;
    cui_scroll_state *st = cui_state(ui, id, sizeof *st);
    st->fade = o->fade_edges ? (cui__color_set(o->fade_color) ? cui_color_of(ui, o->fade_color) : cui__backdrop(ui)) : cui__rgba(0, 0, 0, 0);
    cui__scroll_begin_ex(ui, id, &b, o->horizontal);
}

/* the color behind whatever is built next: the nearest opaque ancestor */
cui_color cui__backdrop(cui_ctx *ui) {
    for (int i = cui__current(ui); i >= 0; i = ui->nodes[i].parent)
        if (ui->nodes[i].bg.a == 255) return ui->nodes[i].bg;
    return CUI_C(ui, BACKGROUND);
}

void cui_scroll_end(cui_ctx *ui) { cui__scroll_end_ex(ui); }

void cui_scroll_to(cui_ctx *ui, const char *scroll_id, float y) {
    cui_scroll_state *st = cui_state(ui, cui_make_id(ui, scroll_id), sizeof *st);
    st->ty = y < 0 ? 0 : y;
}

/* ------------------------------------------------------------------------ */
/* Split view                                                               */
/* ------------------------------------------------------------------------ */

struct cui_split_build { cui_id id; float *ratio; bool vertical; int container; float min1, min2; struct cui_split_build *prev; float hover; };
typedef struct { float hover; bool drag; } cui__split_state;

void cui_split_begin(cui_ctx *ui, const char *id_str, float *ratio, const cui_split_opts *o) {
    cui_split_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    struct cui_split_build *b = cui__frame_alloc(ui, sizeof *b);
    b->id = id; b->ratio = ratio; b->vertical = o->vertical;
    b->min1 = o->min_first > 0 ? o->min_first : 120;
    b->min2 = o->min_second > 0 ? o->min_second : 120;
    b->prev = ui->split;
    ui->split = b;
    cui_rect pr = cui__prev_rect(ui, id);
    cui_id hid = id ^ 0x77u;
    cui_interact r = cui__interact(ui, hid, CUI_IF_DRAG);
    cui__split_state *st = cui_state(ui, hid, sizeof *st);
    float total = o->vertical ? pr.h : pr.w;
    if (r.active && ui->mdown[0] && total > 0) {
        float pos = o->vertical ? ui->my - pr.y : ui->mx - pr.x;
        pos = cui__clampf(pos, b->min1, total - b->min2);
        *ratio = pos / total;
    }
    if (r.hovered || r.active) cui__set_cursor(ui, o->vertical ? CUI_CURSOR_RESIZE_NS : CUI_CURSOR_RESIZE_EW);
    cui__tween(ui, &st->hover, (r.hovered || r.active) ? 1.0f : 0.0f, CUI__T_HOVER);
    b->hover = st->hover;
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = o->vertical ? CUI_COLUMN : CUI_ROW;
    n->w = o->width.kind ? o->width : CUI_FILL;
    n->h = o->height.kind ? o->height : CUI_FILL;
    n->self_align = CUI_ALIGN_STRETCH;
    b->container = i;
    cui__open(ui, i);
    float rr = cui__clampf(*ratio, 0.02f, 0.98f);
    int p1 = cui__node_new(ui, 0);
    cui_node *pn = &ui->nodes[p1];
    pn->flags |= CUI_NF_CLIP;
    if (o->vertical) { pn->h = CUI_GROW(rr * 1000); pn->w = CUI_FILL; }
    else { pn->w = CUI_GROW(rr * 1000); pn->h = CUI_FILL; }
    pn->min_w = o->vertical ? 0 : b->min1;
    pn->min_h = o->vertical ? b->min1 : 0;
    cui__open(ui, p1);
}

typedef struct { float hover; bool vertical; } cui__handle_paint;
static void cui__paint_split_handle(cui_ctx *ui, cui_node *n, void *data) {
    cui__handle_paint *p = data;
    cui_rect r = n->rect;
    if (p->hover > 0.01f) {
        cui_rect line = p->vertical ? (cui_rect){ r.x, r.y + r.h * 0.5f - 0.5f, r.w, 1 } : (cui_rect){ r.x + r.w * 0.5f - 0.5f, r.y, 1, r.h };
        cui_draw_rect(ui, line, CUI_CORNERS(0), cui_alpha(CUI_C(ui, ACCENT), p->hover));
        cui_rect g = p->vertical ? (cui_rect){ r.x + r.w * 0.5f - 16, r.y + r.h * 0.5f - 2, 32, 4 } : (cui_rect){ r.x + r.w * 0.5f - 2, r.y + r.h * 0.5f - 16, 4, 32 };
        cui_draw_rect(ui, g, CUI_CORNERS(2), cui_alpha(CUI_C(ui, ACCENT), p->hover));
    }
}

void cui_split_next(cui_ctx *ui) {
    struct cui_split_build *b = ui->split;
    if (!b) return;
    cui__close(ui);
    /* the divider takes one point of layout, so panes with their own background meet at the line;
       the grab area is a wider strip laid over it */
    cui_id lid = b->id ^ 0x78u;
    int line = cui__leaf(ui, lid, b->vertical ? CUI_FILL : CUI_PX(1), b->vertical ? CUI_PX(1) : CUI_FILL);
    ui->nodes[line].bg = CUI_C(ui, BORDER);
    cui_rect lr = cui__prev_rect(ui, lid), cr = cui__prev_rect(ui, b->id);
    if (lr.w > 0 && lr.h > 0) {
        int h = b->vertical ? cui__absolute(ui, b->id ^ 0x77u, lr.x - cr.x, lr.y - cr.y - 4, lr.w, 9)
                            : cui__absolute(ui, b->id ^ 0x77u, lr.x - cr.x - 4, lr.y - cr.y, 9, lr.h);
        cui__mark_interactive(ui, h, 0);
        cui__handle_paint *hp = cui__frame_alloc(ui, sizeof *hp);
        hp->hover = b->hover; hp->vertical = b->vertical;
        ui->nodes[h].paint = cui__paint_split_handle;
        ui->nodes[h].paint_data = hp;
        ui->nodes[h].flags |= CUI_NF_ON_TOP;
    }
    float rr = cui__clampf(*b->ratio, 0.02f, 0.98f);
    int p2 = cui__node_new(ui, 0);
    cui_node *pn = &ui->nodes[p2];
    pn->flags |= CUI_NF_CLIP;
    if (b->vertical) { pn->h = CUI_GROW((1 - rr) * 1000); pn->w = CUI_FILL; pn->min_h = b->min2; }
    else { pn->w = CUI_GROW((1 - rr) * 1000); pn->h = CUI_FILL; pn->min_w = b->min2; }
    cui__open(ui, p2);
}

void cui_split_end(cui_ctx *ui) {
    struct cui_split_build *b = ui->split;
    if (!b) return;
    cui__close(ui);   /* second pane */
    cui__close(ui);   /* container */
    ui->split = b->prev;
}
