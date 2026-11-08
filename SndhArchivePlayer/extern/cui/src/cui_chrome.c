/* App chrome: the menu bar, toolbar, status bar and document tabs of a
 * desktop style window. Menus reuse the dropdown menu machinery of
 * cui_overlay.c (popups, keyboard highlight, menu items); the tabs reuse the
 * reorder pattern of the reorderable list, laid on its side. */
#include "cui_internal.h"

#define cui__fs cui__fsz

/* the menu bar and toolbar being built (they do not nest) */
typedef struct cui__menubar_state cui__menubar_state;
typedef struct cui__toolbar_build cui__toolbar_build;
typedef struct { cui__menubar_state *bar; cui__toolbar_build *tb; } cui__chrome_slots;
static cui__chrome_slots *cui__slots(cui_ctx *ui) { return cui_state(ui, 0xC4203E5u, sizeof(cui__chrome_slots)); }

/* a hairline along one edge of a bar */
typedef struct { cui_color c; bool top; } cui__edge_paint;
static void cui__paint_edge(cui_ctx *ui, cui_node *n, void *data) {
    cui__edge_paint *p = data;
    cui_rect r = n->rect;
    cui_draw_rect(ui, (cui_rect){ r.x, p->top ? r.y : r.y + r.h - 1, r.w, 1 }, CUI_CORNERS(0), p->c);
}
static void cui__bar_edge(cui_ctx *ui, int node, bool top) {
    cui__edge_paint *p = CUI_PAINT_DATA(ui, cui__edge_paint);
    p->c = CUI_C(ui, BORDER);
    p->top = top;
    ui->nodes[node].paint = cui__paint_edge;
    ui->nodes[node].paint_data = p;
}

static int cui__bar_row(cui_ctx *ui, cui_id id, float pad_y, float pad_x, bool edge, bool top) {
    int row = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[row];
    n->dir = CUI_ROW;
    n->align = CUI_ALIGN_CENTER;
    n->gap = 2;
    n->pad[0] = n->pad[2] = pad_y;
    n->pad[1] = n->pad[3] = pad_x;
    n->w = CUI_FILL;
    n->self_align = CUI_ALIGN_STRETCH;
    n->flags |= CUI_NF_CLIP;
    if (edge) cui__bar_edge(ui, row, top);
    return row;
}

/* true when an open popup sits above the layer being built, so the keys are its */
static bool cui__keys_covered(cui_ctx *ui) {
    int my_z = 0;
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].id == ui->current_layer) my_z = ui->popups[i].z;
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].open && ui->popups[i].z > my_z) return true;
    return false;
}

static int cui__key_for_char(char c) {
    if (c >= 'a' && c <= 'z') return CUI_KEY_A + (c - 'a');
    if (c >= 'A' && c <= 'Z') return CUI_KEY_A + (c - 'A');
    if (c >= '0' && c <= '9') return CUI_KEY_0 + (c - '0');
    return 0;
}
static char cui__lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* ------------------------------------------------------------------------ */
/* Menu bar                                                                 */
/* ------------------------------------------------------------------------ */

#define CUI__BAR_MAX 16

struct cui__menubar_state {
    bool init;
    int open;                  /* open menu, -1 for none */
    int hl;                    /* keyboard highlight while the bar has focus */
    bool kbd;                  /* the bar has keyboard focus (Alt, F10, a mnemonic, Esc from a menu) */
    bool alt_prev, alt_armed;  /* Alt pressed and released on its own toggles the bar */
    bool esc;                  /* Esc arrived this frame, before an open menu ate it */
    bool alt_now;
    int count;                 /* menus built last frame */
    cui_id trig[CUI__BAR_MAX], pid[CUI__BAR_MAX];
    char mn[CUI__BAR_MAX];
    bool dis[CUI__BAR_MAX];
    cui_id saved_focus;        /* restored when the bar lets go of the keyboard */
    /* while building */
    int build;
    int cur;
    bool cur_was_open;
};

typedef struct { float hover, focus; } cui__trigger_state;
typedef struct { int at, len; cui_color c; } cui__mnemonic_paint;

static void cui__paint_mnemonic(cui_ctx *ui, cui_node *n, void *data) {
    cui__mnemonic_paint *p = data;
    float x0 = n->rect.x + cui__text_width(ui, n->font, n->font_size, n->text, p->at, false);
    float w = cui__text_width(ui, n->font, n->font_size, n->text + p->at, p->len, false);
    float fh = cui__font_line_height(ui, n->font, n->font_size);
    float base = n->rect.y + (n->rect.h - fh) * 0.5f + cui__font_ascent(ui, n->font, n->font_size);
    cui_draw_rect(ui, (cui_rect){ x0, floorf(base + 2.0f), w, 1 }, CUI_CORNERS(0), p->c);
}

static int cui__bar_step(cui__menubar_state *st, int from, int dir) {
    int n = st->count < CUI__BAR_MAX ? st->count : CUI__BAR_MAX;
    if (n <= 0) return -1;
    int k = from;
    for (int i = 0; i < n; i++) {
        k = ((k + dir) % n + n) % n;
        if (!st->dis[k]) return k;
    }
    return from;
}

static void cui__bar_take_keys(cui_ctx *ui, cui__menubar_state *st) {
    if (st->kbd) return;
    st->kbd = true;
    st->saved_focus = ui->focus;
    ui->focus = 0;
}

static void cui__bar_release_keys(cui_ctx *ui, cui__menubar_state *st) {
    if (!st->kbd) return;
    st->kbd = false;
    if (!ui->focus) ui->focus = st->saved_focus;
    st->saved_focus = 0;
}

static void cui__bar_open(cui_ctx *ui, cui__menubar_state *st, int k, bool keys) {
    if (st->open >= 0 && st->open != k) cui__popup_set(ui, st->pid[st->open], false);
    st->open = k;
    if (k < 0) return;
    st->hl = k;
    cui__popup_set(ui, st->pid[k], true);
    cui_popup *p = cui__popup_get(ui, st->pid[k], false);
    if (p) p->at_mouse = false;
    if (keys) {   /* opened from the keyboard: the first item is ready for Enter */
        cui__menu_nav *nav = cui_state(ui, st->pid[k] ^ 0x4E4156u, sizeof *nav);
        nav->highlight = 0;
        nav->keyboard = true;
    }
}

/* the open menu closed itself: Esc steps back to the bar, anything else
 * (a chosen item, a click elsewhere) gives the keyboard back */
static void cui__bar_menu_closed(cui_ctx *ui, cui__menubar_state *st, int k) {
    if (st->open != k) return;
    st->open = -1;
    if (st->esc) { cui__bar_take_keys(ui, st); st->hl = k; }
    else cui__bar_release_keys(ui, st);
}

void cui_menu_bar_begin(cui_ctx *ui, const char *id_str, const cui_menu_bar_opts *o) {
    cui_menu_bar_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__menubar_state *st = cui_state(ui, id, sizeof *st);
    if (!st->init) { st->open = -1; st->init = true; }
    int n = st->count < CUI__BAR_MAX ? st->count : CUI__BAR_MAX;
    if (st->open >= n) st->open = -1;
    if (st->hl >= n) st->hl = 0;
    st->esc = cui_key_pressed(ui, CUI_KEY_ESCAPE);
    /* the bar cleared the focus when it took the keys; anything focused since
     * (Tab, a click) means the keyboard moved on */
    if (st->kbd && ui->focus) { st->kbd = false; st->saved_focus = 0; }
    /* closed by something we did not see (another popup, a context menu) */
    if (st->open >= 0) {
        cui_popup *p = cui__popup_get(ui, st->pid[st->open], false);
        if (!p || !p->open) { st->open = -1; cui__bar_release_keys(ui, st); }
    }
    bool covered_by_other = false;   /* a popup that is not ours is on top */
    if (st->open >= 0) {
        cui_popup *mine = cui__popup_get(ui, st->pid[st->open], false);
        for (int i = 0; i < ui->popup_count; i++) {
            cui_popup *p = &ui->popups[i];
            if (mine && p != mine && p->open && p->z > mine->z) covered_by_other = true;
        }
    } else {
        covered_by_other = cui__keys_covered(ui);
    }

    /* Alt on its own (pressed and released with nothing in between) or F10 */
    int mods = ui->mods;
    bool alt = (mods & CUI_MOD_ALT) != 0;
    bool other = ui->mpressed[0] || ui->mpressed[1] || ui->mpressed[2];
    for (int k = 1; k < CUI_KEY_COUNT && !other; k++) if (ui->kpressed[k]) other = true;
    if (alt && !st->alt_prev) st->alt_armed = true;
    if (other || !ui->window_focused) st->alt_armed = false;   /* Alt+Tab away is not a tap */
    bool toggle = cui_key_pressed(ui, CUI_KEY_F10) && !(mods & (CUI_MOD_CTRL | CUI_MOD_ALT | CUI_MOD_SUPER | CUI_MOD_SHIFT));
    if (!alt && st->alt_prev && st->alt_armed) toggle = true;
    if (!alt) st->alt_armed = false;
    st->alt_prev = alt;
    st->alt_now = alt;
    if (toggle && n > 0 && !covered_by_other) {
        ui->kpressed[CUI_KEY_F10] = 0;
        if (st->open >= 0 || st->kbd) { cui__bar_open(ui, st, -1, false); cui__bar_release_keys(ui, st); }
        else { cui__bar_take_keys(ui, st); st->hl = st->dis[0] ? cui__bar_step(st, 0, 1) : 0; }
    }

    /* mnemonics: Alt+letter anywhere, the bare letter while the bar has focus */
    if (n > 0 && !covered_by_other) {
        bool alt_only = alt && !(mods & (CUI_MOD_CTRL | CUI_MOD_SUPER));
        bool bare = st->kbd && st->open < 0 && !(mods & (CUI_MOD_CTRL | CUI_MOD_ALT | CUI_MOD_SUPER));
        if (alt_only || bare) {
            for (int k = 0; k < n; k++) {
                int key = cui__key_for_char(st->mn[k]);
                if (st->dis[k] || !key || !cui_key_pressed(ui, (cui_key)key)) continue;
                ui->kpressed[key] = 0;
                st->alt_armed = false;
                cui__bar_take_keys(ui, st);
                cui__bar_open(ui, st, k, true);
                break;
            }
        }
    }

    /* keys while the bar has focus and no menu is open */
    if (st->kbd && st->open < 0 && n > 0 && !covered_by_other) {
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) { st->hl = cui__bar_step(st, st->hl, -1); ui->kpressed[CUI_KEY_LEFT] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { st->hl = cui__bar_step(st, st->hl, 1); ui->kpressed[CUI_KEY_RIGHT] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_HOME)) { st->hl = cui__bar_step(st, n - 1, 1); ui->kpressed[CUI_KEY_HOME] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_END)) { st->hl = cui__bar_step(st, 0, -1); ui->kpressed[CUI_KEY_END] = 0; }
        if (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE) ||
            cui_key_pressed(ui, CUI_KEY_DOWN) || cui_key_pressed(ui, CUI_KEY_UP)) {
            ui->kpressed[CUI_KEY_ENTER] = ui->kpressed[CUI_KEY_SPACE] = ui->kpressed[CUI_KEY_DOWN] = ui->kpressed[CUI_KEY_UP] = 0;
            if (st->hl >= 0 && !st->dis[st->hl]) cui__bar_open(ui, st, st->hl, true);
        }
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) { ui->kpressed[CUI_KEY_ESCAPE] = 0; cui__bar_release_keys(ui, st); }
        if (cui_key_pressed(ui, CUI_KEY_TAB)) cui__bar_release_keys(ui, st);
    }

    /* Left and Right in an open menu move to the neighbouring menu */
    if (st->open >= 0 && n > 1 && !covered_by_other) {
        int dir = cui_key_pressed(ui, CUI_KEY_LEFT) ? -1 : cui_key_pressed(ui, CUI_KEY_RIGHT) ? 1 : 0;
        if (dir) {
            ui->kpressed[CUI_KEY_LEFT] = ui->kpressed[CUI_KEY_RIGHT] = 0;
            cui__bar_open(ui, st, cui__bar_step(st, st->open, dir), true);
        }
    }

    /* the pointer: press opens or closes, hovering switches while one is open */
    int hov = -1;
    for (int k = 0; k < n; k++) if (st->trig[k] && ui->hot == st->trig[k]) hov = k;
    if (ui->mpressed[CUI_MOUSE_LEFT]) {
        if (hov >= 0 && !st->dis[hov]) {
            bool close = st->open == hov;
            cui__bar_release_keys(ui, st);
            cui__bar_open(ui, st, close ? -1 : hov, false);
        } else if (hov < 0 && st->kbd && st->open < 0) {
            cui__bar_release_keys(ui, st);
        }
    } else if (st->open >= 0 && hov >= 0 && hov != st->open && !st->dis[hov] && (ui->mx != ui->pmx || ui->my != ui->pmy)) {
        cui__bar_open(ui, st, hov, false);
    }

    int row = cui__bar_row(ui, id, 3, 6, !o->no_border, false);
    cui__open(ui, row);
    cui_push_id(ui, id_str);
    st->build = 0;
    cui__slots(ui)->bar = st;
}

void cui_menu_bar_end(cui_ctx *ui) {
    cui__chrome_slots *s = cui__slots(ui);
    cui__menubar_state *st = s->bar;
    if (!st) return;
    st->count = st->build < CUI__BAR_MAX ? st->build : CUI__BAR_MAX;
    for (int k = st->count; k < CUI__BAR_MAX; k++) st->trig[k] = 0;
    cui_pop_id(ui);
    cui__close(ui);
    s->bar = NULL;
}

bool cui_menu_bar_menu_begin(cui_ctx *ui, const char *label, const cui_menu_bar_menu_opts *o) {
    cui_menu_bar_menu_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__menubar_state *st = cui__slots(ui)->bar;
    if (!st || !label) return false;
    int k = st->build++;
    if (k >= CUI__BAR_MAX) return false;
    cui_id tid = cui_make_id(ui, label);
    cui_id pid = tid ^ 0x3E40B4u;
    bool disabled = o->disabled || cui_is_disabled(ui);
    int len = cui__display_len(label);
    char mn = o->mnemonic;
    if (!mn) for (int i = 0; i < len && !mn; i++) if (cui__key_for_char(label[i])) mn = label[i];
    st->trig[k] = tid; st->pid[k] = pid; st->mn[k] = mn; st->dis[k] = disabled;

    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, tid, CUI_IF_KEEP_FOCUS);
    cui_pop_disabled(ui);
    bool open = st->open == k;
    bool hl = st->kbd && st->open < 0 && st->hl == k;
    cui__trigger_state *ts = cui_state(ui, tid, sizeof *ts);
    cui__tween(ui, &ts->hover, (r.hovered && st->open < 0) || hl ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &ts->focus, hl ? 1.0f : 0.0f, CUI__T_FOCUS);

    float h = ui->theme.control_sm;
    int i = cui__node_new(ui, tid);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER;
    n->h = CUI_PX(h);
    n->pad[1] = n->pad[3] = 10;
    n->radius = CUI_CORNERS(CUI__R_CONTROL(ui, h));
    /* a trigger that anchors a menu answers with color, not scale */
    n->bg = open ? CUI_C(ui, ACTIVE) : cui_alpha(CUI_C(ui, HOVER), ts->hover);
    n->opacity = disabled ? CUI__DISABLED : 1.0f;
    n->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, i, 0);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    int t = cui__text_node(ui, label, len, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    if (mn && (st->kbd || st->alt_now) && !disabled) {
        int at = -1;
        for (int c = 0; c < len && at < 0; c++) if (cui__lower(label[c]) == cui__lower(mn)) at = c;
        if (at >= 0) {
            cui__mnemonic_paint *mp = CUI_PAINT_DATA(ui, cui__mnemonic_paint);
            mp->at = at; mp->len = 1; mp->c = CUI_C(ui, FOREGROUND);
            ui->nodes[t].paint = cui__paint_mnemonic;
            ui->nodes[t].paint_data = mp;
            ui->nodes[t].flags |= CUI_NF_PAINT_AFTER;
        }
    }
    cui__close(ui);
    cui__focus_ring(ui, i, ts->focus);
    ui->last_id = tid;
    ui->last_node = i;

    cui_popup *p = cui__popup_get(ui, pid, false);
    if (p && p->open != open) cui__popup_set(ui, pid, open);
    bool was_open = p && p->open;
    bool shown = cui__popup_begin(ui, pid, tid, CUI_PLACE_BOTTOM_START, o->width, 6, 0, true);
    if (was_open && !p->open) cui__bar_menu_closed(ui, st, k);   /* Esc or a click elsewhere */
    st->cur = k;
    st->cur_was_open = p && p->open;
    ui->last_id = tid;
    return shown;
}

void cui_menu_bar_menu_end(cui_ctx *ui) {
    cui__popup_end(ui);
    cui__menubar_state *st = cui__slots(ui)->bar;
    if (!st || !st->cur_was_open || st->cur < 0 || st->cur >= CUI__BAR_MAX) return;
    cui_popup *p = cui__popup_get(ui, st->pid[st->cur], false);
    if (!p || !p->open) cui__bar_menu_closed(ui, st, st->cur);   /* an item was chosen */
}

/* ------------------------------------------------------------------------ */
/* Toolbar                                                                  */
/* ------------------------------------------------------------------------ */

#define CUI__TOOL_GAP 2.0f
#define CUI__TOOL_OVERFLOW_MAX 64

typedef struct { float total; int fire; } cui__toolbar_state;
typedef struct { const char *label, *icon; bool disabled, checked, sep; int index; } cui__tool_entry;

struct cui__toolbar_build {
    cui_id id;
    cui__toolbar_state *st;
    float x, budget, h;
    bool hiding, any;
    int index;
    cui__tool_entry over[CUI__TOOL_OVERFLOW_MAX];
    int over_count;
};

/* Room for one more item of width w? Items are measured the same way every
 * frame, visible or not, so the total (and the overflow decision) is stable. */
static bool cui__tool_place(cui__toolbar_build *b, float w) {
    b->x += (b->any ? CUI__TOOL_GAP : 0) + w;
    b->any = true;
    if (!b->hiding && b->x > b->budget) b->hiding = true;
    return !b->hiding;
}

void cui_toolbar_begin(cui_ctx *ui, const char *id_str, const cui_toolbar_opts *o) {
    cui_toolbar_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__toolbar_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    b->st = cui_state(ui, b->id, sizeof(cui__toolbar_state));
    b->h = ui->theme.control_sm;
    float pad_x = 6;
    b->budget = 1e9f;
    if (cui__has_prev_rect(ui, b->id)) {
        float avail = cui__prev_rect(ui, b->id).w - 2 * pad_x;
        if (b->st->total > avail + 0.5f) b->budget = avail - (b->h + CUI__TOOL_GAP);   /* leave room for "more" */
    }
    int row = cui__bar_row(ui, b->id, 3, pad_x, !o->no_border, false);
    cui__open(ui, row);
    cui_push_id(ui, id_str);
    cui__slots(ui)->tb = b;
}

static void cui__tool_overflow(cui_ctx *ui, cui__toolbar_build *b, const char *label, const char *icon, bool disabled, bool checked, bool sep, int index) {
    if (b->over_count >= CUI__TOOL_OVERFLOW_MAX) return;
    cui__tool_entry *e = &b->over[b->over_count++];
    e->label = label ? cui__frame_strdup(ui, label, -1) : NULL;   /* the caller's buffer may be reused before the menu is built */
    e->icon = icon; e->disabled = disabled; e->checked = checked; e->sep = sep; e->index = index;
}

bool cui_toolbar_button_ex(cui_ctx *ui, const char *icon, const cui_tool_opts *o) {
    cui_tool_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__toolbar_build *b = cui__slots(ui)->tb;
    int index = b ? b->index++ : 0;
    bool fired = b && b->st->fire == index + 1;   /* chosen from the overflow menu last frame */
    if (fired) b->st->fire = 0;
    bool has_label = o->label && cui__display_len(o->label) > 0;
    float h = ui->theme.control_sm;
    float w = h;
    if (has_label) {
        /* the same measure as a small button: padding, icon, gap, label */
        w = 22 + cui__text_width(ui, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), o->label, cui__display_len(o->label), false);
        if (icon) w += 14 + 6;
    }
    bool on = o->toggle && *o->toggle;
    bool disabled = o->disabled || cui_is_disabled(ui);
    bool clicked = false;
    if (!b || cui__tool_place(b, w)) {
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.variant = CUI_BUTTON_GHOST;
        bo.size = CUI_SIZE_SM;
        bo.disabled = o->disabled;
        bo.selected = on;
        bo.tooltip = o->tooltip;
        bo.shortcut = o->shortcut;
        if (has_label) {
            bo.icon = icon;
            clicked = cui_button_ex(ui, o->label, &bo);
        } else {
            if (!bo.tooltip) bo.tooltip = o->label;
            clicked = cui_icon_button_ex(ui, icon ? icon : CUI_ICON_CIRCLE, &bo);
        }
    } else {
        const char *name = has_label ? o->label : o->tooltip ? o->tooltip : "";
        cui__tool_overflow(ui, b, name, icon, disabled, on, false, index);
    }
    if (fired && !disabled) clicked = true;
    if (clicked && o->toggle) *o->toggle = !*o->toggle;
    return clicked;
}

void cui_toolbar_separator(cui_ctx *ui) {
    cui__toolbar_build *b = cui__slots(ui)->tb;
    float h = ui->theme.control_sm;
    if (b && !cui__tool_place(b, 1 + 2 * 5)) { cui__tool_overflow(ui, b, NULL, NULL, false, false, true, -1); return; }
    int box = cui__node_new(ui, 0);
    ui->nodes[box].pad[1] = ui->nodes[box].pad[3] = 5;
    ui->nodes[box].self_align = CUI_ALIGN_CENTER;
    cui__open(ui, box);
    int l = cui__leaf(ui, 0, CUI_PX(1), CUI_PX(roundf(h * 0.55f)));
    ui->nodes[l].bg = CUI_C(ui, BORDER);
    cui__close(ui);
}

void cui_toolbar_spacer(cui_ctx *ui) {
    cui__toolbar_build *b = cui__slots(ui)->tb;
    if (b && b->hiding) return;
    cui_spacer(ui);
}

void cui_toolbar_end(cui_ctx *ui) {
    cui__chrome_slots *s = cui__slots(ui);
    cui__toolbar_build *b = s->tb;
    if (!b) return;
    b->st->total = b->x;
    if (b->over_count > 0) {
        cui_id pid = b->id ^ 0x0F10Fu;
        cui_popup *p = cui__popup_get(ui, pid, true);
        bool open = p && p->open;
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.variant = CUI_BUTTON_GHOST;
        bo.size = CUI_SIZE_SM;
        bo.selected = open;
        bo.tooltip = "More tools";
        bool clicked = cui_icon_button_ex(ui, CUI_ICON_ELLIPSIS, &bo);
        ui->nodes[ui->last_node].scale = 1.0f;   /* a menu trigger answers with color */
        cui_id anchor = ui->last_id;
        bool key_activate = ui->focus == anchor && ui->focus_visible && (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE));
        if (clicked && p && !(p->open && key_activate)) cui__popup_set(ui, pid, !p->open);
        if (p && !p->open && ui->focus == anchor && ui->focus_visible && cui_key_pressed(ui, CUI_KEY_DOWN)) cui__popup_set(ui, pid, true);
        if (cui__popup_begin(ui, pid, anchor, CUI_PLACE_BOTTOM_END, 0, 6, 0, true)) {
            bool pending_sep = false, any = false;
            for (int k = 0; k < b->over_count; k++) {
                cui__tool_entry *e = &b->over[k];
                if (e->sep) { pending_sep = any; continue; }
                if (pending_sep) { cui_menu_separator(ui); pending_sep = false; }
                cui_menu_item_opts mo;
                memset(&mo, 0, sizeof mo);
                mo.icon = e->icon;
                mo.disabled = e->disabled;
                mo.checked = e->checked;
                cui_push_id_int(ui, e->index);
                if (cui_menu_item_ex(ui, e->label && e->label[0] ? e->label : "Tool", &mo)) b->st->fire = e->index + 1;
                cui_pop_id(ui);
                any = true;
            }
            cui__popup_end(ui);
        }
    }
    cui_pop_id(ui);
    cui__close(ui);
    s->tb = NULL;
}

/* ------------------------------------------------------------------------ */
/* Status bar                                                               */
/* ------------------------------------------------------------------------ */

#define CUI__STATUS_ITEM_H 22.0f

/* A status bar narrower than its items (a phone, a narrow window) first shows icons without their
   labels, then leaves out items from the end, as editors do; nothing is squeezed to "Sa...".
   The budget is last frame's width; `need` is what every item asked for, also last frame. */
typedef struct { float need; } cui__status_bar_state;
struct cui_status_build { float avail, used, need; bool compact; cui__status_bar_state *st; struct cui_status_build *prev; };

void cui_status_bar_begin(cui_ctx *ui, const char *id_str, const cui_status_bar_opts *o) {
    cui_status_bar_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    struct cui_status_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->st = cui_state(ui, id ^ 0x5BA8u, sizeof *b->st);
    cui_rect pr = cui__prev_rect(ui, id);
    b->avail = pr.w > 0 ? pr.w - 12 : 1e9f;
    b->compact = b->avail + 0.5f < b->st->need;
    b->prev = (struct cui_status_build *)ui->status_bar;
    ui->status_bar = b;
    int row = cui__bar_row(ui, id, 3, 6, !o->no_border, true);
    ui->nodes[row].gap = 4;
    cui__open(ui, row);
    cui_push_id(ui, id_str);
}

void cui_status_bar_end(cui_ctx *ui) {
    struct cui_status_build *b = (struct cui_status_build *)ui->status_bar;
    if (b) { b->st->need = b->need; ui->status_bar = b->prev; }
    cui_pop_id(ui);
    cui__close(ui);
}

/* Books an item's width. *icon_only says to drop its label; false means leave the item out. */
static bool cui__status_fits(cui_ctx *ui, float label_w, bool has_icon, float extra, bool *icon_only) {
    struct cui_status_build *b = (struct cui_status_build *)ui->status_bar;
    *icon_only = false;
    if (!b) return true;
    float full = 12 + (has_icon ? 18 : 0) + label_w + extra + (b->need > 0 ? 4 : 0);
    b->need += full;
    float w = full;
    if (b->compact && has_icon && label_w > 0) { *icon_only = true; w = 12 + 12 + extra + (b->used > 0 ? 4 : 0); }
    if (b->compact && b->used + w > b->avail) return false;
    b->used += w;
    return true;
}

void cui_status_bar_spacer(cui_ctx *ui) { cui_spacer(ui); }

static int cui__status_row(cui_ctx *ui, cui_id id) {
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 6;
    n->h = CUI_PX(CUI__STATUS_ITEM_H);
    n->pad[1] = n->pad[3] = 6;
    n->radius = CUI_CORNERS(CUI__R_SMALL);
    n->self_align = CUI__ALIGN_NOSTRETCH;
    return i;
}

static void cui__status_label(cui_ctx *ui, const char *text, int len, cui_color c) {
    int t = cui__text_node(ui, text, len, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), c, false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].flags |= CUI_NF_TABULAR;   /* "Ln 12, Col 4" keeps its width as it counts */
}

void cui_status_bar_text_ex(cui_ctx *ui, const char *text, const cui_status_item_opts *o) {
    cui_status_item_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    bool icon_only;
    float tw = text ? cui__text_width(ui, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), text, cui__display_len(text), true) : 0;
    if (!cui__status_fits(ui, tw, o->icon != NULL, 0, &icon_only)) return;
    int i = cui__status_row(ui, 0);
    cui__open(ui, i);
    if (o->icon) cui__icon_node(ui, o->icon, 12, o->tone ? cui__tone_color(ui, o->tone) : CUI_C(ui, TEXT_SECONDARY));
    if (text && !icon_only) cui__status_label(ui, text, cui__display_len(text), CUI_C(ui, TEXT_SECONDARY));
    cui__close(ui);
}

typedef struct { float hover, focus; } cui__status_item_state;

bool cui_status_bar_item_ex(cui_ctx *ui, const char *label, const cui_status_item_opts *o) {
    cui_status_item_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!label) label = "";
    bool icon_only;
    float lw = label[0] ? cui__text_width(ui, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), label, cui__display_len(label), true) : 0;
    if (!cui__status_fits(ui, lw, o->icon != NULL, 0, &icon_only)) return false;
    cui_id id = cui_make_id(ui, label[0] ? label : (o->tooltip ? o->tooltip : "#status-item"));
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_pop_disabled(ui);
    cui__status_item_state *st = cui_state(ui, id, sizeof *st);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->focus, r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
    int i = cui__status_row(ui, id);
    cui_node *n = &ui->nodes[i];
    n->bg = r.pressed ? CUI_C(ui, ACTIVE) : cui_alpha(CUI_C(ui, HOVER), st->hover);
    n->opacity = disabled ? CUI__DISABLED : 1.0f;
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    cui_color fg = cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), st->hover);
    if (o->icon) cui__icon_node(ui, o->icon, 12, o->tone ? cui__tone_color(ui, o->tone) : fg);
    if (label[0] && cui__display_len(label) > 0 && !icon_only) cui__status_label(ui, label, cui__display_len(label), fg);
    cui__close(ui);
    cui__focus_ring(ui, i, st->focus);
    ui->last_id = id;
    ui->last_node = i;
    if (o->tooltip) cui_tooltip(ui, o->tooltip);
    else if (icon_only) cui_tooltip(ui, label);   /* the label it no longer shows */
    return r.clicked && !disabled;
}

typedef struct { float value; bool indeterminate; double time; float motion; cui_color track, fill; } cui__status_progress_paint;

static void cui__paint_status_progress(cui_ctx *ui, cui_node *n, void *data) {
    cui__status_progress_paint *p = data;
    cui_rect r = n->rect;
    float rad = r.h * 0.5f;
    cui_draw_rect(ui, r, CUI_CORNERS(rad), p->track);
    if (p->indeterminate) {
        float w = r.w * 0.4f;
        float x = r.x + (r.w - w) * 0.5f;   /* reduced motion: a still segment in the middle */
        if (p->motion > 0) {
            float t = (float)fmod(p->time * 0.75 / p->motion, 1.0);
            x = r.x - w + (r.w + w) * (t * t * (3 - 2 * t));
            ui->animating = true;
        }
        cui_push_clip(ui, r);
        cui_draw_rect(ui, (cui_rect){ x, r.y, w, r.h }, CUI_CORNERS(rad), p->fill);
        cui_pop_clip(ui);
    } else if (p->value > 0) {
        cui_draw_rect(ui, (cui_rect){ r.x, r.y, CUI_MAX(r.h, r.w * p->value), r.h }, CUI_CORNERS(rad), p->fill);
    }
}

void cui_status_bar_progress_ex(cui_ctx *ui, float value, const cui_status_progress_opts *o) {
    cui_status_progress_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    bool icon_only;
    float lw = o->label ? cui__text_width(ui, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), o->label, cui__display_len(o->label), true) + 8 : 0;
    if (!cui__status_fits(ui, lw, false, o->width > 0 ? o->width : 64, &icon_only)) return;
    int i = cui__status_row(ui, 0);
    ui->nodes[i].gap = 8;
    cui__open(ui, i);
    if (o->label) cui__status_label(ui, o->label, cui__display_len(o->label), CUI_C(ui, TEXT_SECONDARY));
    int bar = cui__leaf(ui, 0, CUI_PX(o->width > 0 ? o->width : 64), CUI_PX(4));
    ui->nodes[bar].self_align = CUI_ALIGN_CENTER;
    cui_id aid = cui__position_id(ui, 0x57A7u);
    cui__status_progress_paint *p = CUI_PAINT_DATA(ui, cui__status_progress_paint);
    p->value = cui_animate(ui, aid, cui__clampf(value, 0, 1));
    p->indeterminate = o->indeterminate;
    p->time = ui->time;
    p->motion = ui->theme.motion;
    p->track = CUI_C(ui, CONTROL_TRACK);
    p->fill = CUI_C(ui, CONTROL_ON);
    ui->nodes[bar].paint = cui__paint_status_progress;
    ui->nodes[bar].paint_data = p;
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Document tabs                                                            */
/* ------------------------------------------------------------------------ */

#define CUI__DTAB_H 32.0f
#define CUI__DTAB_GAP 2.0f

typedef struct {
    bool init;
    int from, to;              /* dragged tab and where it would land, -1 when idle */
    float grab, press_x;
    bool pending;
    int press_pos;
    cui_spring lift;
    int last_sel;              /* last selected + 1, to scroll a new selection into view */
    uint32_t seq;              /* bumped on a drop so every tab re-seats once */
    uint64_t seq_frame;
    cui_id dropped;
    float drop_off;
} cui__dtabs_state;

typedef struct { float hover, slot_hover, xshow, focus; cui_spring x; uint32_t seq; } cui__dtab_state;

typedef struct { cui_color bg, border; float r; bool full; } cui__folder_paint;

/* the selected tab: a raised sheet that opens into the content below, so its
 * bottom edge is left out and its fill covers the strip's baseline */
static void cui__paint_folder(cui_ctx *ui, cui_node *n, void *data) {
    cui__folder_paint *p = data;
    cui_rect r = n->rect;
    cui_corners c = { p->r, p->r, p->full ? p->r : 0, p->full ? p->r : 0 };
    if (p->full) { cui_draw_rect_border(ui, r, c, p->bg, p->border, 1); return; }
    cui_push_clip(ui, r);
    cui_draw_rect_border(ui, (cui_rect){ r.x, r.y, r.w, r.h + 4 }, c, p->bg, p->border, 1);
    cui_pop_clip(ui);
}

typedef struct { cui_color c; float alpha; } cui__dot_paint;
static void cui__paint_dirty_dot(cui_ctx *ui, cui_node *n, void *data) {
    cui__dot_paint *p = data;
    if (p->alpha <= 0.01f) return;
    cui_vec2 ctr = { n->rect.x + n->rect.w * 0.5f, n->rect.y + n->rect.h * 0.5f };
    cui_draw_circle(ui, ctr, 4.0f, cui_alpha(p->c, p->alpha));
}

cui_doc_tabs_result cui_doc_tabs_ex(cui_ctx *ui, const char *id_str, const cui_doc_tab *tabs, int count, int *selected, const cui_doc_tabs_opts *o) {
    cui_doc_tabs_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_doc_tabs_result res;
    res.changed = false; res.close = -1; res.move_from = -1; res.move_to = -1;
    int sel_local = 0;
    if (!selected) selected = &sel_local;
    if (!tabs || count < 0) count = 0;
    cui_id id = cui_make_id(ui, id_str);
    cui__dtabs_state *st = cui_state(ui, id, sizeof *st);
    if (!st->init) { st->from = -1; st->init = true; }
    if (st->from >= count) { st->from = -1; st->pending = false; }
    if (st->press_pos >= count) st->pending = false;
    cui_push_id(ui, id_str);

    /* identities and last frame's geometry */
    size_t nn = (size_t)(count > 0 ? count : 1);
    cui_id *tid = cui__frame_alloc(ui, sizeof(cui_id) * nn);
    float *left = cui__frame_alloc(ui, sizeof(float) * nn), *wid = cui__frame_alloc(ui, sizeof(float) * nn);
    bool have = count > 0;
    for (int k = 0; k < count; k++) {
        const char *t = tabs[k].title;
        tid[k] = t && t[0] ? cui_make_id(ui, t) : cui__hash(&k, sizeof k, id ^ 0xD7Bu);
        if (!cui__has_prev_rect(ui, tid[k])) { have = false; left[k] = wid[k] = 0; continue; }
        cui_rect r = cui__prev_rect(ui, tid[k]);
        left[k] = r.x; wid[k] = r.w;
    }
    int sel = *selected;
    bool valid = sel >= 0 && sel < count;

    /* window wide shortcuts (opt in) */
    if (o->shortcuts && count > 0) {
        if (cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_TAB) || cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_PAGE_DOWN)) {
            sel = valid ? (sel + 1) % count : 0;
            ui->kpressed[CUI_KEY_TAB] = ui->kpressed[CUI_KEY_PAGE_DOWN] = 0;
        }
        if (cui_shortcut(ui, CUI_MOD_CTRL | CUI_MOD_SHIFT, CUI_KEY_TAB) || cui_shortcut(ui, CUI_MOD_CTRL, CUI_KEY_PAGE_UP)) {
            sel = valid ? (sel + count - 1) % count : count - 1;
            ui->kpressed[CUI_KEY_TAB] = ui->kpressed[CUI_KEY_PAGE_UP] = 0;
        }
        if (!o->no_close && valid && cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_W)) { res.close = *selected; ui->kpressed[CUI_KEY_W] = 0; }
        if (sel != *selected && valid && ui->focus == tid[*selected]) ui->focus = tid[sel];   /* focus follows when it was on the strip */
    }

    /* keys on the focused tab (only the selected tab is in the Tab order) */
    valid = *selected >= 0 && *selected < count;
    if (valid && sel == *selected && ui->focus == tid[sel] && !cui__keys_covered(ui)) {
        int m = ui->mods & (CUI_MOD_CTRL | CUI_MOD_ALT | CUI_MOD_SUPER | CUI_MOD_SHIFT);
        int to = -1;
        bool move = false;
        if (cui_key_pressed(ui, CUI_KEY_LEFT) && sel > 0) { to = sel - 1; move = m == CUI_MOD_SHIFT; }
        if (cui_key_pressed(ui, CUI_KEY_RIGHT) && sel < count - 1) { to = sel + 1; move = m == CUI_MOD_SHIFT; }
        if (cui_key_pressed(ui, CUI_KEY_HOME) && !m) to = 0;
        if (cui_key_pressed(ui, CUI_KEY_END) && !m) to = count - 1;
        if (to >= 0 && (m == 0 || move) && to != sel) {
            if (move && !o->no_reorder) {
                /* selection follows below; the moved tab glides from its old slot */
                res.move_from = sel; res.move_to = to;
                if (have) {
                    float new_x = to > sel ? left[to] + wid[to] - wid[sel] : left[to];
                    st->dropped = tid[sel];
                    st->drop_off = left[sel] - new_x;
                    st->seq++;
                    st->seq_frame = ui->frame + 1;
                }
            } else if (!move) {
                ui->focus = tid[to];
                sel = to;
            }
            ui->focus_visible = true;
        }
        if (cui_key_pressed(ui, CUI_KEY_DELETE) && !o->no_close && !m) { res.close = sel; ui->focus_visible = true; }
        ui->kpressed[CUI_KEY_LEFT] = ui->kpressed[CUI_KEY_RIGHT] = ui->kpressed[CUI_KEY_HOME] = ui->kpressed[CUI_KEY_END] = 0;
    }

    /* dragging: the pattern of the reorderable list, along x */
    int pose_from = -1, pose_to = -1;
    float pose_x = 0;
    if (!o->no_reorder) {
        if (st->pending && ui->mdown[0] && fabsf(ui->mx - st->press_x) > 4 && have && st->press_pos < count) {
            st->pending = false;
            st->from = st->to = st->press_pos;
            st->grab = st->press_x - left[st->from];
        }
        if (!ui->mdown[0]) st->pending = false;
        if (st->from >= 0 && have) {
            int from = st->from;
            float lo = left[0], hi = left[count - 1] + wid[count - 1] - wid[from];
            float x0 = cui__clampf(ui->mx - st->grab, lo, CUI_MAX(lo, hi));
            /* tabs differ in width, so the leading edge decides: a tab is passed
             * once the dragged tab's edge crosses its middle */
            int to = from;
            for (int k = from + 1; k < count; k++) if (x0 + wid[from] > left[k] + wid[k] * 0.5f) to = k;
            for (int k = from - 1; k >= 0; k--) if (x0 < left[k] + wid[k] * 0.5f) to = k;
            st->to = to;
            /* near an edge of a strip that scrolls, keep scrolling */
            cui_id sid = id ^ 0xD0C7u;
            cui_rect vr = cui__prev_rect(ui, sid);
            cui_scroll_state *ss = cui_state(ui, sid, sizeof *ss);
            if (vr.w > 0 && ss->content_w > ss->view_w + 0.5f) {
                float edge = 28, speed = 520 * ui->dt;
                if (ui->mx < vr.x + edge) { ss->tx -= speed; ui->animating = true; }
                if (ui->mx > vr.x + vr.w - edge) { ss->tx += speed; ui->animating = true; }
            }
            pose_from = from; pose_to = to; pose_x = x0;
            if (!ui->mdown[0]) {
                cui__dtab_state *ds = cui_state(ui, tid[from] ^ 0x1u, sizeof *ds);
                if (to != from) {
                    res.move_from = from; res.move_to = to;
                    float new_x = to > from ? left[to] + wid[to] - wid[from] : left[to];
                    st->dropped = tid[from];
                    st->drop_off = x0 - new_x;
                    st->seq++;
                    st->seq_frame = ui->frame + 1;
                } else {
                    ds->x.x = x0 - left[from]; ds->x.v = 0;   /* glides back to its slot */
                    pose_from = pose_to = -1;
                }
                st->from = -1;
            }
        }
        if (st->from >= 0 && !ui->mdown[0]) st->from = -1;
        if (st->from >= 0) ui->active = id;   /* keep the pointer while dragging */
    }
    cui__spring(ui, &st->lift, st->from >= 0 ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);

    /* the strip scrolls sideways; a newly selected tab comes into view */
    int vis_sel = *selected;   /* this frame is drawn in the order the app gave us */
    cui_id sid = id ^ 0xD0C7u;
    cui_scroll_state *ss = cui_state(ui, sid, sizeof *ss);
    int shown_sel = sel;
    if (shown_sel >= 0 && shown_sel < count && st->last_sel != shown_sel + 1 && ss->content_w > ss->view_w + 0.5f && have) {
        cui_rect vr = cui__prev_rect(ui, sid);
        if (vr.w > 0) {
            float margin = 24;
            if (left[shown_sel] < vr.x) ss->tx -= vr.x - left[shown_sel] + margin;
            else if (left[shown_sel] + wid[shown_sel] > vr.x + vr.w) ss->tx += left[shown_sel] + wid[shown_sel] - (vr.x + vr.w) + margin;
        }
    }
    if (shown_sel >= 0) st->last_sel = shown_sel + 1;
    /* a plain wheel scrolls the strip sideways */
    if (ui->scroll_dy != 0 && ui->scroll_dx == 0 && ss->content_w > ss->view_w + 0.5f) {
        cui_rect vr = cui__prev_rect(ui, sid);
        if (vr.w > 0 && cui__rect_contains(vr, ui->mx, ui->my)) {
            ss->tx -= ui->scroll_dy * 44;
            ss->last_activity = ui->time;
            ui->scroll_dy = 0;
        }
    }

    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL;
    int col = cui__scroll_begin_ex(ui, sid, &sb, true);
    cui_node *cn = &ui->nodes[col];
    cn->flags &= ~(uint32_t)CUI_NF_SCROLL_Y;
    cn->self_align = CUI_ALIGN_STRETCH;
    cui__bar_edge(ui, col, false);
    ss->fade = cui__backdrop(ui);   /* the strip has no fill of its own: fade into what it sits on */
    int row = cui__node_new(ui, id ^ 0x5707u);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->align = CUI_ALIGN_END; rn->gap = CUI__DTAB_GAP;
    rn->pad[0] = 4; rn->pad[1] = rn->pad[3] = 6;
    cui__open(ui, row);

    float rad = CUI__R_ITEM(ui);
    float fs = cui__fs(ui, ui->theme.text_body);
    for (int k = 0; k < count; k++) {
        const cui_doc_tab *tb = &tabs[k];
        bool active = k == vis_sel;
        cui_id cid = tid[k] ^ 0xC105Eu;
        cui_interact sr;
        memset(&sr, 0, sizeof sr);
        if (!o->no_close) sr = cui__interact(ui, cid, CUI_IF_KEEP_FOCUS);
        cui_interact tr = cui__interact(ui, tid[k], active ? CUI_IF_FOCUSABLE : CUI_IF_KEEP_FOCUS);
        if (tr.hovered && ui->mpressed[CUI_MOUSE_LEFT]) {
            if (sel != k) { sel = k; ui->focus = tid[k]; ui->focus_visible = false; }
            if (!o->no_reorder) { st->pending = true; st->press_x = ui->mx; st->press_pos = k; }
        }
        if (!o->no_close && (ui->hot == tid[k] || ui->hot == cid) && ui->mpressed[CUI_MOUSE_MIDDLE] && st->from < 0) res.close = k;
        if (sr.clicked) res.close = k;

        cui__dtab_state *ts = cui_state(ui, tid[k] ^ 0x1u, sizeof *ts);
        if (ts->seq != st->seq && ui->frame >= st->seq_frame) {   /* after a drop: every tab is already in its new slot */
            ts->x.x = tid[k] == st->dropped ? st->drop_off : 0;
            ts->x.v = 0;
            ts->seq = st->seq;
        }
        bool dragged = pose_from == k;
        float target = 0;
        if (pose_from >= 0 && !dragged) {
            float shift = wid[pose_from] + CUI__DTAB_GAP;
            if (pose_from < pose_to && k > pose_from && k <= pose_to) target = -shift;
            if (pose_from > pose_to && k >= pose_to && k < pose_from) target = shift;
        }
        if (dragged) { ts->x.x = pose_x - left[k]; ts->x.v = 0; }
        else cui__spring(ui, &ts->x, target, CUI_SPRING_SMOOTH);

        bool hov = (tr.hovered || sr.hovered) && pose_from < 0;
        bool want_x = !o->no_close && (hov || tr.focus_visible || (active && !tb->dirty));
        cui__tween(ui, &ts->hover, hov ? 1.0f : 0.0f, CUI__T_HOVER);
        cui__tween(ui, &ts->slot_hover, sr.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        cui__tween(ui, &ts->xshow, want_x ? 1.0f : 0.0f, CUI__T_HOVER);
        cui__tween(ui, &ts->focus, tr.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);

        int i = cui__node_new(ui, tid[k]);
        cui_node *n = &ui->nodes[i];
        n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 8;
        n->h = CUI_PX(CUI__DTAB_H);
        n->pad[3] = 12;
        n->pad[1] = o->no_close ? 12 : 6;
        n->radius.tl = n->radius.tr = rad;
        n->self_align = CUI_ALIGN_END;
        n->tx = ts->x.x;
        if (active || dragged) {
            cui__folder_paint *fp = CUI_PAINT_DATA(ui, cui__folder_paint);
            fp->bg = active ? CUI_C(ui, SURFACE) : CUI_C(ui, SURFACE_RAISED);
            fp->border = CUI_C(ui, BORDER);
            fp->r = rad;
            fp->full = dragged;
            n->paint = cui__paint_folder;
            n->paint_data = fp;
        } else {
            n->bg = cui_alpha(CUI_C(ui, HOVER), ts->hover);
        }
        if (dragged) {
            n->elevation = CUI_ELEVATION_FLOATING;
            n->elevation_alpha = st->lift.x;
            n->radius.br = n->radius.bl = rad;
            n->flags |= CUI_NF_ON_TOP;
        }
        cui__mark_interactive(ui, i, active ? CUI_IF_FOCUSABLE : 0);
        if (tr.hovered || dragged) cui__set_cursor(ui, dragged ? CUI_CURSOR_MOVE : CUI_CURSOR_HAND);
        cui__open(ui, i);
        cui_color fg = active ? CUI_C(ui, FOREGROUND) : cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), ts->hover * 0.7f);
        if (tb->icon) cui__icon_node(ui, tb->icon, 14, active ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
        const char *title = tb->title ? tb->title : "";
        int t = cui__text_node(ui, title, cui__display_len(title), CUI_FONT_BODY, fs, fg, false);
        ui->nodes[t].line_height = 1.3f;
        ui->nodes[t].max_w = 200;
        if (!o->no_close) {
            int slot = cui__node_new(ui, cid);
            cui_node *s = &ui->nodes[slot];
            s->dir = CUI_STACK; s->align = CUI_ALIGN_CENTER; s->justify = CUI_JUSTIFY_CENTER;
            s->w = CUI_PX(20); s->h = CUI_PX(20);
            s->radius = CUI_CORNERS(CUI__R_SMALL);
            s->bg = sr.pressed ? CUI_C(ui, ACTIVE) : cui_alpha(CUI_C(ui, HOVER), ts->slot_hover * 1.6f);
            s->self_align = CUI_ALIGN_CENTER;
            cui__mark_interactive(ui, slot, 0);
            if (sr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
            cui__open(ui, slot);
            cui__icon_node(ui, CUI_ICON_X, 14, cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), ts->slot_hover));
            ui->nodes[ui->node_count - 1].opacity = CUI_MAX(0.001f, ts->xshow);
            int dot = cui__leaf(ui, 0, CUI_PX(20), CUI_PX(20));
            cui__dot_paint *dp = CUI_PAINT_DATA(ui, cui__dot_paint);
            dp->c = CUI_C(ui, TEXT_SECONDARY);
            dp->alpha = tb->dirty ? 1.0f - ts->xshow : 0.0f;
            ui->nodes[dot].paint = cui__paint_dirty_dot;
            ui->nodes[dot].paint_data = dp;
            cui__close(ui);
        }
        cui__close(ui);
        cui__focus_ring(ui, i, ts->focus);
        if (!o->no_close) {
            ui->last_id = cid;
            cui_tooltip(ui, o->shortcuts && active ? (CUI_MOD_PRIMARY == CUI_MOD_SUPER ? "Close  \xc2\xb7  Cmd W" : "Close  \xc2\xb7  Ctrl W") : "Close");
        }
        ui->last_id = tid[k];
        ui->last_node = i;
        if (tb->tooltip && st->from < 0) cui_tooltip(ui, tb->tooltip);
    }
    cui__close(ui);
    cui__scroll_end_ex(ui);
    cui_pop_id(ui);

    /* selection follows a move, so the app only has to move its own array */
    if (res.move_from >= 0) {
        int f = res.move_from, t = res.move_to;
        if (sel == f) sel = t;
        else if (f < t && sel > f && sel <= t) sel--;
        else if (f > t && sel >= t && sel < f) sel++;
    }
    if (sel != *selected) { *selected = sel; res.changed = true; }
    ui->last_id = id;
    return res;
}
