/* Floating layers: popovers, dropdown and context menus, select and
 * combobox, dialogs, sheets, toasts, tooltips, calendar and date picker,
 * color picker and the command palette. */
#include "cui_internal.h"

static float cui__fs(cui_ctx *ui, float size) { return size * ui->theme.font_scale; }
int cui__text_field(cui_ctx *ui, cui_id id, char *buf, size_t cap, const cui_input_opts *o, bool multiline, int style, const char *search_icon);
void cui__paint_focus_ring_node(cui_ctx *ui, cui_node *n, void *data);

/* ------------------------------------------------------------------------ */
/* Popup bookkeeping                                                        */
/* ------------------------------------------------------------------------ */



cui_popup *cui__popup_get(cui_ctx *ui, cui_id id, bool create) {
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].id == id) return &ui->popups[i];
    if (!create) return NULL;
    /* reuse a slot that is closed and finished animating */
    for (int i = 0; i < ui->popup_count; i++) {
        cui_popup *p = &ui->popups[i];
        if (!p->open && p->s.x <= 0.001f && ui->frame - p->last_frame > 2) { memset(p, 0, sizeof *p); p->id = id; return p; }
    }
    if (ui->popup_count >= 32) return NULL;
    cui_popup *p = &ui->popups[ui->popup_count++];
    memset(p, 0, sizeof *p);
    p->id = id;
    return p;
}

void cui__popup_set(cui_ctx *ui, cui_id id, bool open) {
    cui_popup *p = cui__popup_get(ui, id, true);
    if (!p) return;
    if (open && !p->open) {
        p->opened_frame = ui->frame;
        p->z = 1000 + ++ui->layer_seq;
        cui__menu_nav *nav = cui_state(ui, id ^ 0x4E4156u, sizeof *nav);
        nav->highlight = -1;
        nav->keyboard = false;
    }
    p->open = open;
}

void cui__close_all_popups(cui_ctx *ui) {
    for (int i = 0; i < ui->popup_count; i++) ui->popups[i].open = false;
}

static bool cui__popup_is_topmost(cui_ctx *ui, cui_popup *me) {
    for (int i = 0; i < ui->popup_count; i++) {
        cui_popup *p = &ui->popups[i];
        if (p != me && p->open && p->opened_frame > me->opened_frame) return false;
    }
    return ui->dialog_depth == 0 || true;
}

static bool cui__layer_is_open_popup_after(cui_ctx *ui, cui_id layer, cui_popup *me) {
    for (int i = 0; i < ui->popup_count; i++) {
        cui_popup *p = &ui->popups[i];
        if (p->id == layer && p->open && p->opened_frame >= me->opened_frame) return true;
    }
    return false;
}

void cui_popover_open(cui_ctx *ui, const char *id) { cui__popup_set(ui, cui_make_id(ui, id), true); }
void cui_popover_close(cui_ctx *ui, const char *id) { cui__popup_set(ui, cui_make_id(ui, id), false); }
void cui_popover_toggle(cui_ctx *ui, const char *id) {
    cui_id pid = cui_make_id(ui, id);
    cui_popup *p = cui__popup_get(ui, pid, true);
    if (p) cui__popup_set(ui, pid, !p->open);
}
bool cui_popover_is_open(cui_ctx *ui, const char *id) {
    cui_popup *p = cui__popup_get(ui, cui_make_id(ui, id), false);
    return p && p->open;
}

static void cui__panel_style(cui_ctx *ui, cui_node *n, float pad) {
    n->bg = CUI_C(ui, SURFACE_RAISED);
    n->border = 1;
    n->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui_alpha(cui__rgba(0, 0, 0, 255), 0.07f);
    n->radius = CUI_CORNERS(ui->theme.radius_panel);
    n->elevation = CUI_ELEVATION_FLOATING;
    n->elevation_alpha = 1;
    n->pad[0] = n->pad[1] = n->pad[2] = n->pad[3] = pad;
}

bool cui__popup_begin(cui_ctx *ui, cui_id id, cui_id anchor, int placement, float width, float pad, float gap, bool menu) {
    cui_popup *p = cui__popup_get(ui, id, false);
    if (!p) return false;
    p->last_frame = ui->frame;
    if (p->open) {
        bool top = cui__popup_is_topmost(ui, p);
        if (top && cui_key_pressed(ui, CUI_KEY_ESCAPE)) { p->open = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
        if (ui->mpressed[0] || ui->mpressed[1]) {
            bool inside = ui->hot_layer == id || cui__layer_is_open_popup_after(ui, ui->hot_layer, p);
            bool on_anchor = anchor && ui->hot == anchor;
            if (!inside && !on_anchor && ui->frame > p->opened_frame) p->open = false;
        }
    }
    float target = p->open ? 1.0f : 0.0f;
    if (p->open) cui__spring(ui, &p->s, target, CUI__SPRING_ENTER);
    else cui__spring(ui, &p->s, target, CUI__SPRING_EXIT);
    if (!p->open && p->s.x <= 0.01f) { p->s.x = 0; p->s.v = 0; return false; }
    float t = cui__clampf(p->s.x, 0, 1);
    p->anchor = anchor;
    int z = p->z ? p->z : 1000 + ++ui->layer_seq;
    p->z = z;
    int root;
    if (p->at_mouse) {
        root = cui__float_begin(ui, id, z, 0, CUI__PLACE_POINT, 0);
        ui->nodes[root].fx = p->mouse.x;
        ui->nodes[root].fy = p->mouse.y;
    } else {
        root = cui__float_begin(ui, id, z, anchor, placement, 6);
    }
    cui_node *n = &ui->nodes[root];
    cui__panel_style(ui, n, pad);
    n->gap = gap;
    n->flags |= CUI_NF_HIT_OPAQUE;
    if (width > 0) n->w = CUI_PX(width);
    else if (anchor && cui__has_prev_rect(ui, anchor)) n->min_w = cui__prev_rect(ui, anchor).w;
    n->min_w = CUI_MAX(n->min_w, menu ? 180.0f : 0.0f);
    n->max_h = 420;
    n->opacity = t;
    n->scale = CUI__ENTER_SCALE + (1.0f - CUI__ENTER_SCALE) * t;
    bool up = placement >= CUI_PLACE_TOP_START && placement <= CUI_PLACE_TOP_END;
    n->ty = (1.0f - t) * (up ? 6.0f : -6.0f);
    cui__popup_build *b = &ui->pb_stack[ui->pb_depth < 16 ? ui->pb_depth : 15];
    if (ui->pb_depth < 16) ui->pb_depth++;
    b->id = id;
    b->menu = menu;
    b->item_index = 0;
    b->disabled_pushed = !p->open;
    if (!p->open) cui_push_disabled(ui, true);
    cui_push_id_int(ui, (int)id);
    if (menu) {
        cui__menu_nav *nav = cui_state(ui, id ^ 0x4E4156u, sizeof *nav);
        nav->prev_item_count = nav->item_count;
        nav->item_count = 0;
        if (p->open && cui__popup_is_topmost(ui, p) && nav->prev_item_count > 0) {
            if (cui_key_pressed(ui, CUI_KEY_DOWN)) { nav->highlight = (nav->highlight + 1) % nav->prev_item_count; nav->keyboard = true; }
            if (cui_key_pressed(ui, CUI_KEY_UP)) { nav->highlight = nav->highlight <= 0 ? nav->prev_item_count - 1 : nav->highlight - 1; nav->keyboard = true; }
        }
    }
    return true;
}

void cui__popup_end(cui_ctx *ui) {
    if (ui->pb_depth <= 0) return;
    cui__popup_build *b = &ui->pb_stack[--ui->pb_depth];
    cui_pop_id(ui);
    if (b->disabled_pushed) cui_pop_disabled(ui);
    cui__float_end(ui);
}

bool cui_popover_begin(cui_ctx *ui, const char *id, const cui_popover_opts *o) {
    cui_popover_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id pid = cui_make_id(ui, id);
    if (!cui__popup_get(ui, pid, false)) return false;
    cui_popup *p = cui__popup_get(ui, pid, false);
    cui_id anchor = p->anchor ? p->anchor : ui->last_id;
    if (p->open && p->opened_frame == ui->frame) anchor = ui->last_id;
    if (!p->anchor || p->opened_frame + 1 >= ui->frame) p->anchor = ui->last_id;
    anchor = p->anchor;
    return cui__popup_begin(ui, pid, anchor, o->placement, o->width, o->pad > 0 ? o->pad : 16, o->gap > 0 ? o->gap : 10, false);
}

void cui_popover_end(cui_ctx *ui) { cui__popup_end(ui); }

/* ------------------------------------------------------------------------ */
/* Menus                                                                    */
/* ------------------------------------------------------------------------ */

bool cui_menu_begin(cui_ctx *ui, const char *label, const cui_menu_opts *o) {
    cui_menu_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id pid = cui_make_id(ui, label) ^ 0x3E40u;
    cui_popup *p = cui__popup_get(ui, pid, true);
    bool open = p && p->open;
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.variant = o->variant;
    bo.icon = o->icon;
    bo.selected = open;
    bool clicked;
    if (o->icon_only) {
        bo.tooltip = label;
        clicked = cui_icon_button_ex(ui, o->icon ? o->icon : CUI_ICON_ELLIPSIS, &bo);
    } else {
        bo.icon_right = CUI_ICON_CHEVRON_DOWN;
        clicked = cui_button_ex(ui, label, &bo);
    }
    /* menu triggers answer presses with color, not scale: the menu anchors to them */
    ui->nodes[ui->last_node].scale = 1.0f;
    cui_id anchor = ui->last_id;
    bool key_activate = ui->focus == anchor && ui->focus_visible && (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE));
    if (clicked && p && !(p->open && key_activate)) {   /* Enter on an open menu picks the item */
        cui__popup_set(ui, pid, !p->open);
        p->at_mouse = false;
    }
    if (p && !p->open && ui->focus == anchor && ui->focus_visible && cui_key_pressed(ui, CUI_KEY_DOWN)) cui__popup_set(ui, pid, true);
    return cui__popup_begin(ui, pid, anchor, o->placement, o->width, 6, 0, true);
}

void cui_menu_end(cui_ctx *ui) { cui__popup_end(ui); }

bool cui_context_menu_begin(cui_ctx *ui, const char *id) {
    cui_id pid = cui_make_id(ui, id) ^ 0xC7Cu;
    cui_id anchor = ui->last_id;
    if (anchor && ui->hot == anchor && ui->mpressed[CUI_MOUSE_RIGHT]) {
        cui__close_all_popups(ui);
        cui__popup_set(ui, pid, true);
        cui_popup *p = cui__popup_get(ui, pid, false);
        if (p) { p->at_mouse = true; p->mouse.x = ui->mx; p->mouse.y = ui->my; }
    }
    if (!cui__popup_get(ui, pid, false)) return false;
    return cui__popup_begin(ui, pid, anchor, CUI_PLACE_BOTTOM_START, 0, 6, 0, true);
}

void cui_context_menu_end(cui_ctx *ui) { cui__popup_end(ui); }

typedef struct { float hover; } cui__item_state;

bool cui_menu_item_ex(cui_ctx *ui, const char *label, const cui_menu_item_opts *o) {
    cui_menu_item_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__popup_build *b = ui->pb_depth > 0 ? &ui->pb_stack[ui->pb_depth - 1] : NULL;
    int index = b ? b->item_index++ : 0;
    cui__menu_nav *nav = b ? cui_state(ui, b->id ^ 0x4E4156u, sizeof *nav) : NULL;
    if (nav) nav->item_count++;
    cui_id id = cui_make_id(ui, label);
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, o->disabled);
    cui_interact r = cui__interact(ui, id, 0);
    cui_pop_disabled(ui);
    if (r.hovered && nav && (ui->mx != ui->pmx || ui->my != ui->pmy || !nav->keyboard)) { nav->highlight = index; nav->keyboard = false; }
    bool hl = nav && nav->highlight == index && !disabled;
    cui__item_state *st = cui_state(ui, id, sizeof *st);
    cui__tween(ui, &st->hover, hl ? 1.0f : 0.0f, CUI__T_HIGHLIGHT);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 10;
    n->h = CUI_PX(32);
    n->pad[1] = 10; n->pad[3] = 10;
    n->w = CUI_FILL;
    n->self_align = CUI_ALIGN_STRETCH;
    n->radius = CUI_CORNERS(CUI_MAX(6.0f, ui->theme.radius_panel - 6.0f));
    cui_color hover_bg = o->danger ? cui_alpha(CUI_C(ui, DANGER), 0.1f) : CUI_C(ui, ACTIVE);
    n->bg = cui_alpha(hover_bg, st->hover);
    n->opacity = disabled ? CUI__DISABLED : 1.0f;
    cui__mark_interactive(ui, i, 0);
    if (r.hovered && !disabled) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    cui_color fg = o->danger ? CUI_C(ui, DANGER) : CUI_C(ui, FOREGROUND);
    if (o->icon) cui__icon_node(ui, o->icon, 16, o->danger ? fg : CUI_C(ui, TEXT_SECONDARY));
    int t = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), fg, false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].w = CUI_FILL;
    if (o->shortcut) {
        int s = cui__text_node(ui, o->shortcut, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[s].line_height = 1.3f;
    }
    if (o->checked) cui__icon_node(ui, CUI_ICON_CHECK, 16, CUI_C(ui, FOREGROUND));
    cui__close(ui);
    bool chosen = !disabled && (r.clicked || (hl && cui_key_pressed(ui, CUI_KEY_ENTER)));
    if (chosen) {
        cui__close_all_popups(ui);
        ui->kpressed[CUI_KEY_ENTER] = 0;
    }
    ui->last_id = id;
    ui->last_node = i;
    if (o->tooltip) cui_tooltip(ui, o->tooltip);
    return chosen;
}

void cui_menu_separator(cui_ctx *ui) {
    int box = cui__node_new(ui, 0);
    ui->nodes[box].pad[0] = ui->nodes[box].pad[2] = 4;
    ui->nodes[box].pad[1] = ui->nodes[box].pad[3] = 4;
    cui__open(ui, box);
    int l = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[l].bg = CUI_C(ui, BORDER);
    cui__close(ui);
}

void cui_menu_label(cui_ctx *ui, const char *text) {
    int box = cui__node_new(ui, 0);
    ui->nodes[box].pad[0] = 8; ui->nodes[box].pad[2] = 4; ui->nodes[box].pad[3] = 10;
    cui__open(ui, box);
    int t = cui__text_node(ui, text, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.3f;
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Select / combobox                                                        */
/* ------------------------------------------------------------------------ */

typedef struct { float hover, focus; char query[96]; } cui__select_state;

static bool cui__contains_ci(const char *hay, const char *needle) {
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

bool cui_select_ex(cui_ctx *ui, const char *id_str, const char *const *items, int count, int *index, const cui_select_opts *o) {
    cui_select_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id pid = id ^ 0x5E1Eu;
    cui__select_state *st = cui_state(ui, id, sizeof *st);
    cui_popup *p = cui__popup_get(ui, pid, true);
    bool disabled = o->disabled || cui_is_disabled(ui);
    cui_push_disabled(ui, disabled);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui_pop_disabled(ui);
    bool open = p && p->open;
    if (r.clicked && p && !(open && r.keyboard_activate)) {   /* Enter on an open list picks the item */
        cui__popup_set(ui, pid, !open);
        p->at_mouse = false;
        st->query[0] = 0;
        if (!open) {
            cui__menu_nav *nav = cui_state(ui, pid ^ 0x4E4156u, sizeof *nav);
            nav->highlight = *index;
        }
    }
    if (p && !p->open && r.focus_visible && (cui_key_pressed(ui, CUI_KEY_DOWN) || cui_key_pressed(ui, CUI_KEY_UP))) {
        cui__popup_set(ui, pid, true);
        cui__menu_nav *nav = cui_state(ui, pid ^ 0x4E4156u, sizeof *nav);
        nav->highlight = *index;
        ui->kpressed[CUI_KEY_DOWN] = ui->kpressed[CUI_KEY_UP] = 0;   /* the key opened the list; it does not also move the highlight */
    }
    open = p && p->open;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__tween(ui, &st->focus, (r.focus_visible || open) ? 1.0f : 0.0f, CUI__T_FOCUS);
    int old = *index;
    int col = -1;
    if (o->label) {
        col = cui__node_new(ui, 0);
        ui->nodes[col].gap = 6;
        ui->nodes[col].w = o->width.kind ? o->width : CUI_FILL;
        cui__open(ui, col);
        cui__field_label(ui, o->label);
    }
    float h = cui__control_height(ui, CUI_SIZE_MD);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 8;
    n->h = CUI_PX(h);
    if (col >= 0) n->w = CUI_FILL; else cui__soft_width(n, o->width, 220);
    n->pad[1] = CUI__FIELD_PAD_ICON; n->pad[3] = CUI__FIELD_PAD;
    n->radius = CUI_CORNERS(CUI__R_CONTROL(ui, h));
    n->bg = CUI_C(ui, SURFACE);
    n->border = 1;
    cui_color border = cui__control_edge(ui, st->hover);
    n->border_color = cui_mix(border, cui__edge_on(ui), open ? 1.0f : 0.0f);
    n->opacity = disabled ? CUI__DISABLED : 1.0f;
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    bool has = *index >= 0 && *index < count;
    if (has && o->icons && o->icons[*index]) cui__icon_node(ui, o->icons[*index], 16, CUI_C(ui, TEXT_SECONDARY));
    const char *shown = has ? items[*index] : (o->placeholder ? o->placeholder : "Select");
    int t = cui__text_node(ui, shown, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), has ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].w = CUI_FILL;
    cui__icon_node(ui, CUI_ICON_CHEVRONS_UP_DOWN, 16, CUI_C(ui, TEXT_MUTED));
    cui__close(ui);
    if (r.focus_visible) {
        float *fa = cui__frame_alloc(ui, sizeof(float));
        *fa = 1;
        ui->nodes[i].paint_data = fa;
        ui->nodes[i].paint = cui__paint_focus_ring_node;
        ui->nodes[i].flags |= CUI_NF_PAINT_AFTER;
    }
    if (col >= 0) cui__close(ui);
    ui->last_id = id;

    if (cui__popup_begin(ui, pid, id, CUI_PLACE_BOTTOM_START, 0, 6, 0, true)) {
        if (o->searchable) {
            cui_input_opts io;
            memset(&io, 0, sizeof io);
            io.placeholder = "Search";
            io.autofocus = true;
            io.size = CUI_SIZE_SM;
            cui_id sid = cui_make_id(ui, "#q");
            if (p && p->opened_frame == ui->frame - 1) ui->focus = sid;
            int box = cui__node_new(ui, 0);
            ui->nodes[box].pad[0] = 2; ui->nodes[box].pad[2] = 4;
            cui__open(ui, box);
            cui__text_field(ui, sid, st->query, sizeof st->query, &io, false, 0, CUI_ICON_SEARCH);
            cui__close(ui);
        }
        cui__menu_nav *nav = cui_state(ui, pid ^ 0x4E4156u, sizeof *nav);
        int shown_count = 0;
        cui_box sb;
        memset(&sb, 0, sizeof sb);
        sb.height = CUI_FIT;
        sb.max_height = 300;
        cui_id scid = cui_make_id(ui, "#list");
        cui__scroll_begin_ex(ui, scid, &sb, false);
        ui->nodes[cui__current(ui)].h = CUI_FIT;
        ui->nodes[cui__current(ui)].min_h = 0;
        for (int k = 0; k < count; k++) {
            if (o->searchable && !cui__contains_ci(items[k], st->query)) continue;
            cui_menu_item_opts mo;
            memset(&mo, 0, sizeof mo);
            mo.checked = k == *index;
            mo.icon = o->icons ? o->icons[k] : NULL;
            cui_push_id_int(ui, k);
            if (cui_menu_item_ex(ui, items[k], &mo)) *index = k;
            cui_pop_id(ui);
            shown_count++;
        }
        if (shown_count == 0) {
            int e = cui__node_new(ui, 0);
            ui->nodes[e].pad[0] = ui->nodes[e].pad[2] = 10; ui->nodes[e].pad[3] = 10;
            cui__open(ui, e);
            cui__text_node(ui, "No results", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
            cui__close(ui);
        }
        (void)nav;
        cui__scroll_end_ex(ui);
        cui__popup_end(ui);
    }
    return *index != old;
}

/* ------------------------------------------------------------------------ */
/* Tooltip                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct { char *text; size_t text_size; cui_id anchor, shown; cui_spring t, ox, oy; double last_shown; bool visible, passive; } cui__tooltip_state;
static cui__tooltip_state *cui__tip(cui_ctx *ui) {
    cui__tooltip_state *tt = cui_state(ui, 0x7001717u, sizeof *tt);
    if (tt->text_size) tt->text = cui_state(ui, 0x7001719u, tt->text_size);
    return tt;
}
static void cui__tooltip_set_text(cui_ctx *ui, cui__tooltip_state *tt, const char *text) {
    tt->text_size = strlen(text) + 1;
    tt->text = cui_state(ui, 0x7001719u, tt->text_size);
    memcpy(tt->text, text, tt->text_size);
}

void cui_tooltip(cui_ctx *ui, const char *text) {
    cui_id anchor = ui->last_id;
    if (!anchor || !text) return;
    cui__tooltip_state *tt = cui__tip(ui);
    bool hovered = ui->hot == anchor && ui->active == 0 && !ui->mdown[0];
    if (hovered) {
        if (ui->tooltip_id != anchor) {
            ui->tooltip_id = anchor;
            ui->hover_start = ui->time;
        }
        bool warm = ui->time - tt->last_shown < 0.4;
        float delay = warm ? 0.0f : 0.55f;
        if (ui->time - ui->hover_start >= delay) {
            cui__tooltip_set_text(ui, tt, text);
            tt->anchor = anchor;
            tt->visible = true;
            tt->passive = false;
        } else {
            cui__wakeup_after(ui, (float)(delay - (ui->time - ui->hover_start)) + 0.01f);
        }
    } else if (ui->tooltip_id == anchor) {
        ui->tooltip_id = 0;
    }
}

/* Like cui_tooltip, for a rect that is not interactive (a truncated label). A
 * widget's own tooltip wins if both ask in the same frame. */
void cui__tooltip_passive(cui_ctx *ui, cui_id anchor, const char *text) {
    cui__tooltip_state *tt = cui__tip(ui);
    if (tt->visible && !tt->passive) return;
    if (ui->active != 0) return;
    /* callers test the pointer against a plain rect; a menu, popover or dialog
     * open above that rect owns the pointer, so its label stays quiet */
    if (ui->hot_layer != ui->current_layer) return;
    if (ui->tooltip_id != anchor) { ui->tooltip_id = anchor; ui->hover_start = ui->time; }
    bool warm = ui->time - tt->last_shown < 0.4;
    float delay = warm ? 0.0f : 0.7f;
    if (ui->time - ui->hover_start >= delay) {
        cui__tooltip_set_text(ui, tt, text);
        tt->anchor = anchor;
        tt->visible = true;
        tt->passive = true;
    } else {
        cui__wakeup_after(ui, (float)(delay - (ui->time - ui->hover_start)) + 0.01f);
    }
}

static void cui__tooltip_render(cui_ctx *ui) {
    cui__tooltip_state *tt = cui__tip(ui);
    bool show = tt->visible && (tt->passive || ui->hot == tt->anchor);
    cui__spring(ui, &tt->t, show ? 1.0f : 0.0f, show ? 0.2f : 0.12f, 0.0f);
    if (show) tt->last_shown = ui->time;
    tt->visible = false;
    if (!show) tt->passive = false;
    if (tt->t.x <= 0.01f || !tt->anchor) { tt->shown = 0; tt->ox.x = tt->oy.x = 0; return; }
    /* moving to a neighbour while warm, the tooltip travels from where it was instead of popping again */
    if (tt->anchor != tt->shown) {
        if (tt->shown && tt->t.x > 0.3f && cui__has_prev_rect(ui, tt->shown) && cui__has_prev_rect(ui, tt->anchor)) {
            cui_rect a = cui__prev_rect(ui, tt->shown), b = cui__prev_rect(ui, tt->anchor);
            tt->ox.x += (a.x + a.w * 0.5f) - (b.x + b.w * 0.5f);
            tt->oy.x += a.y - b.y;
        }
        tt->shown = tt->anchor;
    }
    cui__spring(ui, &tt->ox, 0, 0.32f, 0.08f);
    cui__spring(ui, &tt->oy, 0, 0.32f, 0.08f);
    float t = cui__clampf(tt->t.x, 0, 1);
    int root = cui__float_begin(ui, 0x7001718u, 0x7FFFFFF0, tt->anchor, CUI_PLACE_TOP, 6);
    cui_node *n = &ui->nodes[root];
    n->bg = ui->mode == CUI_THEME_DARK ? cui_oklch(0.93f, 0, 0, 1) : cui_oklch(0.20f, 0, 0, 1);
    n->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    n->pad[0] = n->pad[2] = 5; n->pad[1] = n->pad[3] = 9;
    n->opacity = t;
    n->scale = CUI__ENTER_SCALE + (1.0f - CUI__ENTER_SCALE) * t;
    n->tx = tt->ox.x;
    n->ty = (1 - t) * 3 + tt->oy.x;
    n->max_w = 280;
    n->elevation = CUI_ELEVATION_RAISED;
    bool dark = ui->mode == CUI_THEME_DARK;
    cui_color fg = dark ? cui_oklch(0.18f, 0, 0, 1) : cui_oklch(0.98f, 0, 0, 1);
    /* "Label  ·  Ctrl B": the shortcut sits beside the label as quiet key caps */
    const char *sep = strstr(tt->text, "  \xc2\xb7  ");
    if (sep) {
        n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 8;
        /* the caps sit as far from the right edge as from the top and bottom */
        n->pad[1] = 5; n->pad[3] = 8;
        int tx = cui__text_node(ui, tt->text, (int)(sep - tt->text), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), fg, false);
        ui->nodes[tx].line_height = 1.35f;
        const char *keys = sep + 6;
        int row = cui__node_new(ui, 0);
        ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 3;
        cui__open(ui, row);
        while (*keys) {
            const char *e = strchr(keys, ' ');
            int len = e ? (int)(e - keys) : (int)strlen(keys);
            if (len > 0) {
                int k = cui__node_new(ui, 0);
                cui_node *kn = &ui->nodes[k];
                kn->pad[1] = kn->pad[3] = 5; kn->pad[0] = kn->pad[2] = 1;
                kn->radius = CUI_CORNERS(4);
                kn->bg = cui_alpha(fg, 0.14f);
                kn->min_w = 18;
                kn->dir = CUI_ROW; kn->justify = CUI_JUSTIFY_CENTER;
                cui__open(ui, k);
                int kt = cui__text_node(ui, keys, len, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), cui_alpha(fg, 0.75f), false);
                ui->nodes[kt].line_height = 1.4f;
                cui__close(ui);
            }
            if (!e) break;
            keys = e + 1;
        }
        cui__close(ui);
    } else {
        int tx = cui__text_node(ui, tt->text, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), fg, true);
        ui->nodes[tx].line_height = 1.35f;
    }
    cui__float_end(ui);
}

/* ------------------------------------------------------------------------ */
/* Dialogs and sheets                                                       */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring t; uint64_t opened; bool was_open; int z; } cui__modal_state;


static bool cui__modal_begin(cui_ctx *ui, cui_id id, const char *title, bool *open, const char *description, float width,
                             bool persistent, bool close_button, bool center_title, int kind, cui_side side) {
    cui__modal_state *st = cui_state(ui, id, sizeof *st);
    bool is_open = open && *open;
    if (is_open && !st->was_open) { st->opened = ui->frame; st->z = 1000 + ++ui->layer_seq; }
    st->was_open = is_open;
    float target = is_open ? 1.0f : 0.0f;
    if (is_open) {
        if (kind == 0) cui__spring(ui, &st->t, target, CUI__SPRING_DIALOG);
        else cui__spring(ui, &st->t, target, 0.42f, 0.0f);   /* sheets travel the height of the screen */
    } else cui__spring(ui, &st->t, target, CUI__SPRING_EXIT);
    if (!is_open && st->t.x <= 0.01f) { st->t.x = 0; st->t.v = 0; return false; }
    float t = cui__clampf(st->t.x, 0, 1);
    cui_id scrim_id = id ^ 0x5C819u;
    int z = st->z ? st->z : 1000 + ++ui->layer_seq;
    st->z = z;
    /* Esc and click outside: only the topmost modal reacts, and only when no popup inside it is open */
    bool topmost = ui->prev_modal_layer == 0 || ui->prev_modal_layer == id;
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].open) topmost = false;
    if (is_open && !persistent && topmost) {
        /* while a text field has focus, the first Esc leaves the field */
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE) && !ui->edit_prev) { *open = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
        if (ui->mreleased[0] && ui->hot == scrim_id && ui->active == scrim_id) *open = false;
    }
    if (is_open && (!ui->modal_layer || z > ui->modal_z)) {
        ui->modal_layer = id;
        ui->modal_z = z;
        /* move keyboard focus into the dialog when it opens */
        if (st->opened == ui->frame && ui->focus_visible) ui->focus = 0;
    }
    cui__interact(ui, scrim_id, 0);
    int root = cui__float_begin(ui, id, z, 0, CUI__PLACE_FULL, 0);
    cui_node *rn = &ui->nodes[root];
    rn->id = scrim_id;
    ui->current_layer = id;
    rn->bg = cui_alpha(CUI_C(ui, SCRIM), t);
    rn->flags |= CUI_NF_BLOCKER | CUI_NF_INTERACTIVE;
    rn->dir = CUI_STACK;
    if (kind == 0) { rn->align = CUI_ALIGN_CENTER; rn->justify = CUI_JUSTIFY_CENTER; }
    else {
        rn->pad[0] = rn->pad[1] = rn->pad[2] = rn->pad[3] = 8;
        rn->align = side == CUI_SIDE_LEFT ? CUI_ALIGN_START : CUI_ALIGN_END;
        rn->justify = side == CUI_SIDE_TOP ? CUI_JUSTIFY_START : (side == CUI_SIDE_BOTTOM ? CUI_JUSTIFY_END : CUI_JUSTIFY_START);
        if (side == CUI_SIDE_TOP || side == CUI_SIDE_BOTTOM) rn->align = CUI_ALIGN_START;
    }
    int panel = cui__node_new(ui, id ^ 0x9A4Eu);
    cui_node *pn = &ui->nodes[panel];
    pn->bg = CUI_C(ui, SURFACE_RAISED);
    pn->border = 1;
    pn->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui_alpha(cui__rgba(0, 0, 0, 255), 0.06f);
    pn->radius = CUI_CORNERS(ui->theme.radius_surface);
    pn->elevation = CUI_ELEVATION_FLOATING;
    pn->elevation_alpha = 1;
    pn->pad[0] = pn->pad[1] = pn->pad[2] = pn->pad[3] = 24;
    pn->gap = 16;
    pn->flags |= CUI_NF_HIT_OPAQUE;
    if (kind == 0) {
        pn->w = CUI_PX(CUI_MIN(width, ui->vw - 32));
        pn->max_h = ui->vh - 48;
        pn->opacity = t;
        pn->scale = CUI__ENTER_SCALE + (1.0f - CUI__ENTER_SCALE) * t;
        pn->ty = (1 - t) * 10;
    } else {
        bool horiz = side == CUI_SIDE_LEFT || side == CUI_SIDE_RIGHT;
        if (horiz) { pn->w = CUI_PX(CUI_MIN(width, ui->vw - 16)); pn->h = CUI_PX(ui->vh - 16); }
        else { pn->w = CUI_PX(ui->vw - 16); pn->h = CUI_PX(CUI_MIN(width, ui->vh - 16)); }
        float dist = (horiz ? width : width) + 24;
        float off = (1 - t) * dist;
        if (side == CUI_SIDE_RIGHT) pn->tx = off;
        else if (side == CUI_SIDE_LEFT) pn->tx = -off;
        else if (side == CUI_SIDE_BOTTOM) pn->ty = off;
        else pn->ty = -off;
    }
    cui__open(ui, panel);
    cui__modal_build *b = &ui->mb_stack[ui->mb_depth < 8 ? ui->mb_depth : 7];
    if (ui->mb_depth < 8) ui->mb_depth++;
    b->id = id;
    b->disabled_pushed = !is_open;
    if (!is_open) cui_push_disabled(ui, true);
    if (ui->dialog_depth < 8) ui->dialog_stack[ui->dialog_depth++] = id;
    cui_push_id_int(ui, (int)id);
    /* header */
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].align = CUI_ALIGN_START; ui->nodes[head].gap = 12;
    cui__open(ui, head);
    if (center_title && close_button) cui__leaf(ui, 0, CUI_PX(ui->theme.control_sm), CUI_PX(ui->theme.control_sm));
    int hc = cui__node_new(ui, 0);
    ui->nodes[hc].gap = 6; ui->nodes[hc].w = CUI_FILL;
    cui__open(ui, hc);
    int tt = cui__text_node(ui, title, cui__display_len(title), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.heading[3]), CUI_C(ui, FOREGROUND), true);
    ui->nodes[tt].line_height = 1.3f;
    if (center_title) {
        ui->nodes[tt].w = CUI_FILL;
        ui->nodes[tt].text_align = CUI_TEXT_CENTER;
    }
    if (description) {
        int dt = cui__text_node(ui, description, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), true);
        ui->nodes[dt].line_height = 1.5f;
    }
    cui__close(ui);
    if (close_button) {
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM;
        bo.tooltip = "Close";
        if (cui_icon_button_ex(ui, CUI_ICON_X, &bo) && open) *open = false;
        ui->nodes[ui->last_node].ty = -4;
        ui->nodes[ui->last_node].tx = 6;
    }
    cui__close(ui);
    if (kind == 1) {
        /* sheet content scrolls */
        cui_box sb;
        memset(&sb, 0, sizeof sb);
        sb.height = CUI_FILL;
        sb.gap = 16;
        cui__scroll_begin_ex(ui, cui_make_id(ui, "#sheet-body"), &sb, false);
    }
    return true;
}

static void cui__modal_end(cui_ctx *ui, int kind) {
    if (kind == 1) cui__scroll_end_ex(ui);
    cui_pop_id(ui);
    cui__close(ui);           /* panel */
    cui__float_end(ui);       /* root */
    if (ui->dialog_depth > 0) ui->dialog_depth--;
    if (ui->mb_depth > 0) {
        cui__modal_build *b = &ui->mb_stack[--ui->mb_depth];
        if (b->disabled_pushed) cui_pop_disabled(ui);
    }
}

bool cui_dialog_begin(cui_ctx *ui, const char *title, bool *open, const cui_dialog_opts *o) {
    cui_dialog_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, title) ^ 0xD1A106u;
    return cui__modal_begin(ui, id, title, open, o->description, o->width > 0 ? o->width : 440, o->persistent, !o->no_close_button, o->center_title, 0, CUI_SIDE_RIGHT);
}

void cui_dialog_end(cui_ctx *ui) { cui__modal_end(ui, 0); }

void cui_dialog_footer_begin(cui_ctx *ui) {
    int row = cui__node_new(ui, 0);
    cui_node *n = &ui->nodes[row];
    n->dir = CUI_ROW; n->gap = 8; n->justify = CUI_JUSTIFY_END;
    n->pad[0] = 8;
    cui__open(ui, row);
}

void cui_dialog_footer_end(cui_ctx *ui) { cui__close(ui); }

bool cui_sheet_begin(cui_ctx *ui, const char *title, bool *open, const cui_sheet_opts *o) {
    cui_sheet_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, title) ^ 0x5EE7u;
    return cui__modal_begin(ui, id, title, open, o->description, o->size > 0 ? o->size : 400, false, true, false, 1, o->side);
}

void cui_sheet_end(cui_ctx *ui) { cui__modal_end(ui, 1); }

/* ------------------------------------------------------------------------ */
/* Toasts                                                                   */
/* ------------------------------------------------------------------------ */

int cui_toast_ex(cui_ctx *ui, const char *title, const cui_toast_opts *o) {
    cui_toast_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (ui->toast_count >= CUI_MAX_TOASTS) {
        memmove(&ui->toasts[0], &ui->toasts[1], sizeof(cui_toast_item) * (CUI_MAX_TOASTS - 1));
        ui->toast_count--;
    }
    cui_toast_item *t = &ui->toasts[ui->toast_count++];
    memset(t, 0, sizeof *t);
    t->handle = ui->toast_next_handle++;
    snprintf(t->title, sizeof t->title, "%s", title ? title : "");
    if (o->description) snprintf(t->desc, sizeof t->desc, "%s", o->description);
    if (o->action) snprintf(t->action, sizeof t->action, "%s", o->action);
    t->tone = o->tone;
    t->show_timer = o->show_timer;
    t->duration = o->duration != 0 ? o->duration : (o->show_timer ? 6.0f : 4.0f);
    t->height = 64;
    ui->animating = true;
    return t->handle;
}

bool cui_toast_action_clicked(cui_ctx *ui, int handle) {
    for (int i = 0; i < ui->toast_count; i++)
        if (ui->toasts[i].handle == handle && ui->toasts[i].action_clicked) { ui->toasts[i].action_clicked = false; return true; }
    return false;
}

void cui_toast_dismiss(cui_ctx *ui, int handle) {
    for (int i = 0; i < ui->toast_count; i++) if (ui->toasts[i].handle == handle) ui->toasts[i].dismissed = true;
}

typedef struct { float hover; } cui__toasts_state;

typedef struct { float frac; cui_color c; } cui__toast_timer;
static void cui__paint_toast_timer(cui_ctx *ui, cui_node *n, void *data) {
    cui__toast_timer *p = data;
    cui_rect r = n->rect;
    float inset = n->radius.bl;
    float w = (r.w - inset * 2) * cui__clampf(p->frac, 0, 1);
    if (w > 0) cui_draw_rect(ui, (cui_rect){ r.x + inset, r.y + r.h - 3, w, 2 }, CUI_CORNERS(1), p->c);
}

static void cui__toasts_render(cui_ctx *ui) {
    if (ui->toast_count == 0) return;
    cui_id root_id = 0x70A57u;
    cui__toasts_state *ts = cui_state(ui, root_id, sizeof *ts);
    bool hovered = ui->hot_layer == root_id;
    cui__tween(ui, &ts->hover, hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    float W = CUI_MIN(360.0f, ui->vw - 32);
    /* age, dismissal, removal */
    for (int i = 0; i < ui->toast_count; i++) {
        cui_toast_item *t = &ui->toasts[i];
        if (!hovered && t->duration > 0 && !t->dismissed) {
            t->age += ui->dt;
            if (t->age >= t->duration) t->dismissed = true;
            else cui__wakeup_after(ui, t->duration - t->age + 0.01f);
        }
        cui__spring(ui, &t->enter, t->dismissed ? 0.0f : 1.0f, t->dismissed ? 0.22f : 0.38f, 0.0f);
    }
    for (int i = 0; i < ui->toast_count;) {
        cui_toast_item *rt = &ui->toasts[i];
        bool gone = rt->sx.x > 0.5f ? rt->sx.x >= W + 40 : rt->enter.x <= 0.01f;
        if (rt->dismissed && gone) {
            memmove(&ui->toasts[i], &ui->toasts[i + 1], sizeof(cui_toast_item) * (size_t)(ui->toast_count - i - 1));
            ui->toast_count--;
        } else i++;
    }
    if (ui->toast_count == 0) return;
    /* layout: newest at the bottom front; older ones peek behind when collapsed */
    float gap = 8, peek = 10;
    bool expanded = ts->hover > 0.5f;
    float total = 0;
    int visible = 0;
    for (int i = ui->toast_count - 1; i >= 0; i--) {
        cui_toast_item *t = &ui->toasts[i];
        if (t->dismissed) continue;
        total += t->height + (visible ? gap : 0);
        visible++;
    }
    float front_h = ui->toasts[ui->toast_count - 1].height;
    float collapsed_h = front_h + peek * (float)CUI_MIN(visible - 1, 2);
    float H = expanded ? total : collapsed_h;
    if (H < 1) H = front_h;
    int root = cui__float_begin(ui, root_id, 0x7FFFFF00, 0, CUI__PLACE_POINT, 0);
    cui_node *rn = &ui->nodes[root];
    rn->fx = ui->vw - W - 16;
    rn->fy = ui->vh - 16 - H;
    rn->w = CUI_PX(W);
    rn->h = CUI_PX(H);
    rn->flags |= CUI_NF_HIT_OPAQUE;
    /* pass 1: positions, newest first */
    float ty[CUI_MAX_TOASTS], tsc[CUI_MAX_TOASTS], talpha[CUI_MAX_TOASTS];
    int tlevel[CUI_MAX_TOASTS];
    {
        float y_expanded = H;
        int depth = 0;
        for (int i = ui->toast_count - 1; i >= 0; i--) {
            cui_toast_item *t = &ui->toasts[i];
            int level = depth;
            float target_y_exp = y_expanded - t->height;
            float target_y_col = H - front_h - peek * (float)CUI_MIN(level, 3) + (front_h - t->height);
            if (level == 0) target_y_col = H - t->height;
            float target = cui__lerp(target_y_col, target_y_exp, ts->hover);
            if (t->y.x == 0 && t->y.v == 0) t->y.x = target;
            cui__spring(ui, &t->y, target, CUI_SPRING_SMOOTH);
            ty[i] = t->y.x;
            tsc[i] = cui__lerp(1.0f - 0.05f * (float)CUI_MIN(level, 3), 1.0f, ts->hover);
            talpha[i] = level >= 3 && !expanded ? 0.0f : 1.0f;
            tlevel[i] = level;
            if (!t->dismissed) { y_expanded -= t->height + gap; depth++; }
        }
    }
    /* pass 2: build oldest first so the newest paints on top */
    for (int i = 0; i < ui->toast_count; i++) {
        cui_toast_item *t = &ui->toasts[i];
        float sc = tsc[i], alpha = talpha[i];
        float e = cui__clampf(t->enter.x, 0, 1);
        cui_id tid = cui__hash(&t->handle, sizeof t->handle, root_id);
        /* swipe right to dismiss: the toast follows the pointer, resists to the left like rubber,
           and a fling or a long drag carries its speed out of the screen */
        if (!t->dismissed) {
            cui_interact sr = cui__interact(ui, tid, CUI_IF_DRAG);
            if (sr.pressed && !t->swiping && ui->mpressed[0]) { t->swiping = true; t->grab_x = ui->mx - t->sx.x; t->vel = 0; }
            if (t->swiping) {
                if (ui->mdown[0]) {
                    float raw = ui->mx - t->grab_x;
                    float dx = raw >= 0 ? raw : raw * W * 0.55f / (W + 0.55f * -raw) * 0.35f;
                    if (ui->dt > 0) t->vel = cui__lerp(t->vel, (dx - t->sx.x) / ui->dt, 0.5f);
                    t->sx.x = dx; t->sx.v = 0;
                    if (fabsf(raw) > 3) cui__set_cursor(ui, CUI_CURSOR_MOVE);
                    ui->animating = true;
                } else {
                    t->swiping = false;
                    if (t->sx.x > W * 0.35f || t->vel > 600) { t->dismissed = true; t->sx.v = CUI_MIN(CUI_MAX(t->vel, 900.0f), 2400.0f); }
                    else t->sx.v = CUI_CLAMP(t->vel, -1500.0f, 1500.0f);
                }
            }
        }
        if (!t->swiping) {
            bool flung = t->dismissed && t->sx.x > 0.5f;
            cui__spring(ui, &t->sx, flung ? W + 48 : 0.0f, flung ? 0.3f : 0.42f, flung ? 0.0f : 0.2f);
        }
        float swipe_fade = 1.0f - 0.7f * cui__clampf(t->sx.x / W, 0, 1);
        int tn = cui__absolute(ui, tid, t->sx.x, ty[i] + (t->sx.x > 0.5f ? 0 : (1 - e) * 24), W, t->height);
        cui__mark_interactive(ui, tn, 0);
        cui_node *n = &ui->nodes[tn];
        n->h = CUI_FIT;
        n->dir = CUI_ROW; n->gap = 12; n->align = CUI_ALIGN_START;
        n->pad[0] = n->pad[2] = 14; n->pad[1] = 12; n->pad[3] = 16;
        n->bg = CUI_C(ui, SURFACE_RAISED);
        n->border = 1;
        n->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui_alpha(cui__rgba(0, 0, 0, 255), 0.07f);
        n->radius = CUI_CORNERS(ui->theme.radius_control);
        n->elevation = CUI_ELEVATION_FLOATING;
        n->elevation_alpha = 0.8f;
        n->opacity = (t->sx.x > 0.5f && t->dismissed ? 1.0f : e) * alpha * swipe_fade;
        n->scale = sc;
        if (t->show_timer && t->duration > 0) {
            /* a draining line: how long the action stays available */
            cui__toast_timer *tp = cui__frame_alloc(ui, sizeof *tp);
            tp->frac = 1.0f - t->age / t->duration;
            tp->c = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, TEXT_MUTED) : CUI_C(ui, ACCENT);
            n->paint = cui__paint_toast_timer;
            n->paint_data = tp;
            n->flags |= CUI_NF_PAINT_AFTER;
            if (!hovered && !t->dismissed) ui->animating = true;
        }
        cui_push_id_int(ui, t->handle);
        cui__open(ui, tn);
        const char *icon = NULL;
        cui_color ic = CUI_C(ui, TEXT_SECONDARY);
        switch (t->tone) {
        case CUI_TONE_SUCCESS: icon = CUI_ICON_CIRCLE_CHECK; ic = CUI_C(ui, SUCCESS); break;
        case CUI_TONE_WARNING: icon = CUI_ICON_TRIANGLE_ALERT; ic = CUI_C(ui, WARNING); break;
        case CUI_TONE_DANGER: icon = CUI_ICON_CIRCLE_ALERT; ic = CUI_C(ui, DANGER); break;
        case CUI_TONE_INFO: icon = CUI_ICON_INFO; ic = ui->theme.tone_sky; break;
        default: break;
        }
        if (icon) {
            int ib = cui__node_new(ui, 0);
            ui->nodes[ib].pad[0] = 1;
            cui__open(ui, ib);
            cui__icon_node(ui, icon, 16, ic);
            cui__close(ui);
        }
        int col = cui__node_new(ui, 0);
        ui->nodes[col].gap = 2; ui->nodes[col].w = CUI_FILL;
        cui__open(ui, col);
        int tt = cui__text_node(ui, t->title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), true);
        ui->nodes[tt].line_height = 1.4f;
        if (t->desc[0]) cui__text_node(ui, t->desc, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body - 0.5f), CUI_C(ui, TEXT_SECONDARY), true);
        cui__close(ui);
        if (t->action[0]) {
            cui_button_opts bo;
            memset(&bo, 0, sizeof bo);
            bo.size = CUI_SIZE_SM;
            if (cui_button_ex(ui, t->action, &bo)) { t->action_clicked = true; t->dismissed = true; }
        }
        cui_button_opts xo;
        memset(&xo, 0, sizeof xo);
        xo.size = CUI_SIZE_SM;
        xo.color = CUI_COLOR_TEXT_MUTED;
        if (cui_icon_button_ex(ui, CUI_ICON_X, &xo)) t->dismissed = true;
        ui->nodes[ui->last_node].w = CUI_PX(24); ui->nodes[ui->last_node].h = CUI_PX(24);
        ui->nodes[ui->last_node].opacity = 0.35f + 0.65f * ts->hover;
        cui__close(ui);
        cui_pop_id(ui);
        if (tlevel[i] > 0) {
            /* cards stacked behind show only their edge */
            for (int c = ui->nodes[tn].first; c >= 0; c = ui->nodes[c].next) ui->nodes[c].opacity = CUI_MAX(0.001f, ts->hover);
        }
        /* remember the measured height for next frame */
        cui_rect pr = cui__prev_rect(ui, tid);
        if (pr.h > 0) t->height = pr.h;
    }
    cui__float_end(ui);
}

/* ------------------------------------------------------------------------ */
/* Calendar and date picker                                                 */
/* ------------------------------------------------------------------------ */

#include <time.h>

cui_date cui_date_today(void) {
    time_t now = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    cui_date d = { tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday };
    return d;
}

static int cui__days_in_month(int y, int m) {
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[(m - 1) % 12];
}

/* 0 = Monday */
static int cui__weekday(int y, int m, int d) {
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;  /* 0 = Sunday */
    return (w + 6) % 7;
}

static const char *cui__month_names[12] = { "January", "February", "March", "April", "May", "June", "July",
                                            "August", "September", "October", "November", "December" };

typedef struct { int view_y, view_m; bool init; } cui__cal_state;

bool cui_calendar(cui_ctx *ui, const char *id_str, cui_date *value) {
    cui_id id = cui_make_id(ui, id_str);
    cui__cal_state *st = cui_state(ui, id, sizeof *st);
    cui_date today = cui_date_today();
    if (!st->init) {
        cui_date base = (value && value->year) ? *value : today;
        st->view_y = base.year; st->view_m = base.month; st->init = true;
    }
    bool changed = false;
    cui_push_id(ui, id_str);
    /* 34px day cells, down to 28 when the parent is narrow (phones) */
    cui_rect cpr = cui__prev_rect(ui, id);
    float cs = cpr.w > 0 ? floorf((cpr.w - 12) / 7) : 34;
    cs = cs > 34 ? 34 : cs < 28 ? 28 : cs;
    int col = cui__node_new(ui, id);
    ui->nodes[col].gap = 8;
    cui__soft_width(&ui->nodes[col], CUI_FIT, 7 * 34 + 6 * 2);
    ui->nodes[col].min_w = 7 * 28 + 6 * 2;
    ui->nodes[col].flags |= CUI_NF_ADAPTIVE;
    cui__open(ui, col);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].gap = 2; ui->nodes[head].pad[3] = 6;
    cui__open(ui, head);
    int ht = cui__text_node(ui, cui__frame_printf(ui, "%s %d", cui__month_names[st->view_m - 1], st->view_y), -1,
                            CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body + 1), CUI_C(ui, FOREGROUND), false);
    ui->nodes[ht].line_height = 1.3f;
    ui->nodes[ht].w = CUI_FILL;
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM;
    bo.tooltip = "Previous month";
    if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_LEFT, &bo)) { if (--st->view_m < 1) { st->view_m = 12; st->view_y--; } }
    bo.tooltip = "Next month";
    if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_RIGHT, &bo)) { if (++st->view_m > 12) { st->view_m = 1; st->view_y++; } }
    cui__close(ui);
    cui_box gb;
    memset(&gb, 0, sizeof gb);
    gb.dir = CUI_GRID; gb.columns = 7; gb.gap = 2;
    int grid = cui__node_new(ui, 0);
    cui__apply_box(ui, &ui->nodes[grid], &gb);
    cui__open(ui, grid);
    static const char *wd[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    for (int k = 0; k < 7; k++) {
        int t = cui__text_node(ui, wd[k], -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[t].text_align = CUI_TEXT_CENTER;
        ui->nodes[t].line_height = 2.0f;
    }
    int first = cui__weekday(st->view_y, st->view_m, 1);
    int dim = cui__days_in_month(st->view_y, st->view_m);
    int pm = st->view_m == 1 ? 12 : st->view_m - 1, py = st->view_m == 1 ? st->view_y - 1 : st->view_y;
    int pdim = cui__days_in_month(py, pm);
    for (int cell = 0; cell < 42; cell++) {
        int day = cell - first + 1;
        int y = st->view_y, m = st->view_m;
        bool outside = false;
        if (day < 1) { day += pdim; m = pm; y = py; outside = true; }
        else if (day > dim) { day -= dim; m = st->view_m == 12 ? 1 : st->view_m + 1; y = st->view_m == 12 ? st->view_y + 1 : st->view_y; outside = true; }
        int key = y * 10000 + m * 100 + day;
        cui_id did = cui__hash(&key, sizeof key, id);
        cui_interact r = cui__interact(ui, did, CUI_IF_FOCUSABLE);
        float hv_v = cui__hover_fade(ui, did, r.hovered, CUI__T_HOVER), *hv = &hv_v;
        bool sel = value && value->year == y && value->month == m && value->day == day;
        bool is_today = today.year == y && today.month == m && today.day == day;
        int c = cui__node_new(ui, did);
        cui_node *cn = &ui->nodes[c];
        cn->h = CUI_PX(cs);
        cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->justify = CUI_JUSTIFY_CENTER;
        cn->radius = CUI_CORNERS(cs / 2);
        cn->bg = sel ? CUI_C(ui, CONTROL_ON) : cui_alpha(CUI_C(ui, HOVER), *hv * 1.4f);
        if (is_today && !sel) { cn->border = 1; cn->border_color = CUI_C(ui, BORDER_STRONG); }
        cui__mark_interactive(ui, c, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, c);
        char buf[4];
        snprintf(buf, sizeof buf, "%d", day);
        cui_color fg = sel ? CUI_C(ui, CONTROL_GLYPH) : outside ? CUI_C(ui, TEXT_MUTED) : CUI_C(ui, FOREGROUND);
        int t = cui__text_node(ui, buf, -1, (sel || is_today) ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), fg, false);
        ui->nodes[t].line_height = 1.0f;
        ui->nodes[t].flags |= CUI_NF_TABULAR;
        cui__close(ui);
        if (r.focus_visible) {
            float *fa = cui__frame_alloc(ui, sizeof(float));
            *fa = 1;
            ui->nodes[c].paint_data = fa;
            ui->nodes[c].paint = cui__paint_focus_ring_node;
            ui->nodes[c].flags |= CUI_NF_PAINT_AFTER;
        }
        if (r.clicked && value) {
            value->year = y; value->month = m; value->day = day;
            if (outside) { st->view_y = y; st->view_m = m; }
            changed = true;
        }
    }
    cui__close(ui);
    cui__close(ui);
    cui_pop_id(ui);
    return changed;
}

bool cui_date_picker_ex(cui_ctx *ui, const char *id_str, cui_date *value, const cui_date_opts *o) {
    cui_date_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id pid = id ^ 0xDA7Eu;
    cui_popup *p = cui__popup_get(ui, pid, true);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    if (r.clicked && p) { cui__popup_set(ui, pid, !p->open); p->at_mouse = false; }
    float hv_v = cui__hover_fade(ui, id, r.hovered, CUI__T_HOVER), *hv = &hv_v;
    int col = -1;
    if (o->label) {
        col = cui__node_new(ui, 0);
        ui->nodes[col].gap = 6;
        ui->nodes[col].w = o->width.kind ? o->width : CUI_FILL;
        cui__open(ui, col);
        cui__field_label(ui, o->label);
    }
    float h = cui__control_height(ui, CUI_SIZE_MD);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 8;
    n->h = CUI_PX(h);
    if (col >= 0) n->w = CUI_FILL; else cui__soft_width(n, o->width, 220);
    n->pad[1] = CUI__FIELD_PAD; n->pad[3] = CUI__FIELD_PAD_ICON;
    n->radius = CUI_CORNERS(CUI__R_CONTROL(ui, h));
    n->bg = CUI_C(ui, SURFACE);
    n->border = 1;
    bool open = p && p->open;
    n->border_color = cui_mix(cui__control_edge(ui, *hv),
                              cui__edge_on(ui), open ? 1.0f : 0.0f);
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    cui__icon_node(ui, CUI_ICON_CALENDAR, 16, CUI_C(ui, TEXT_MUTED));
    bool has = value && value->year > 0;
    const char *txt = has ? cui__frame_printf(ui, "%.3s %d, %d", cui__month_names[(value->month - 1) % 12], value->day, value->year)
                          : (o->placeholder ? o->placeholder : "Pick a date");
    int t = cui__text_node(ui, txt, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), has ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].w = CUI_FILL;
    cui__close(ui);
    if (col >= 0) cui__close(ui);
    ui->last_id = id;
    bool changed = false;
    if (cui__popup_begin(ui, pid, id, CUI_PLACE_BOTTOM_START, 0, 12, 0, false)) {
        if (cui_calendar(ui, "#cal", value)) { changed = true; cui__popup_set(ui, pid, false); }
        cui__popup_end(ui);
    }
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Color picker                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { float h, s, v; bool init; char hex[16]; } cui__color_state;

static void cui__rgb_to_hsv(cui_color c, float *h, float *s, float *v) {
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = CUI_MAX(r, CUI_MAX(g, b)), mn = CUI_MIN(r, CUI_MIN(g, b)), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0) { *h = 0; return; }
    float hh;
    if (mx == r) hh = fmodf((g - b) / d, 6.0f);
    else if (mx == g) hh = (b - r) / d + 2;
    else hh = (r - g) / d + 4;
    hh *= 60;
    if (hh < 0) hh += 360;
    *h = hh;
}

static cui_color cui__hsv(float h, float s, float v) {
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c;
    float r, g, b;
    if (h < 60) { r = c; g = x; b = 0; } else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; } else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; } else { r = c; g = 0; b = x; }
    return cui__rgba((int)((r + m) * 255 + 0.5f), (int)((g + m) * 255 + 0.5f), (int)((b + m) * 255 + 0.5f), 255);
}

typedef struct { float h, s, v; } cui__sv_paint;

static void cui__paint_sv(cui_ctx *ui, cui_node *n, void *data) {
    cui__sv_paint *p = data;
    cui_rect r = n->rect;
    cui_color hue = cui__hsv(p->h, 1, 1), white = cui__rgba(255, 255, 255, 255);
    cui_cvert v[4] = { { r.x, r.y, white }, { r.x + r.w, r.y, hue }, { r.x + r.w, r.y + r.h, hue }, { r.x, r.y + r.h, white } };
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    cui__draw_triangles(ui, v, 4, idx, 6);
    cui_color clear = cui__rgba(0, 0, 0, 0), black = cui__rgba(0, 0, 0, 255);
    cui_cvert v2[4] = { { r.x, r.y, clear }, { r.x + r.w, r.y, clear }, { r.x + r.w, r.y + r.h, black }, { r.x, r.y + r.h, black } };
    cui__draw_triangles(ui, v2, 4, idx, 6);
    cui_draw_rect_border(ui, r, CUI_CORNERS(0), cui__rgba(0, 0, 0, 0), cui__rgba(0, 0, 0, 30), 1);
    cui_vec2 c = { r.x + p->s * r.w, r.y + (1 - p->v) * r.h };
    cui_draw_circle(ui, c, 7, cui__rgba(255, 255, 255, 255));
    cui_draw_circle(ui, c, 5, cui__hsv(p->h, p->s, p->v));
}

static void cui__paint_hue(cui_ctx *ui, cui_node *n, void *data) {
    float h = *(float *)data;
    cui_rect r = n->rect;
    cui_cvert v[14];
    uint32_t idx[36];
    for (int k = 0; k <= 6; k++) {
        cui_color c = cui__hsv(fminf(k * 60.0f, 359.9f), 1, 1);
        v[k * 2].x = r.x + r.w * k / 6.0f; v[k * 2].y = r.y + 2; v[k * 2].c = c;
        v[k * 2 + 1].x = r.x + r.w * k / 6.0f; v[k * 2 + 1].y = r.y + r.h - 2; v[k * 2 + 1].c = c;
    }
    for (int k = 0; k < 6; k++) {
        uint32_t a = (uint32_t)k * 2;
        uint32_t q[6] = { a, a + 2, a + 3, a, a + 3, a + 1 };
        memcpy(idx + k * 6, q, sizeof q);
    }
    cui_push_clip(ui, r);
    cui__draw_triangles(ui, v, 14, idx, 36);
    cui_pop_clip(ui);
    cui_rect th = { r.x + r.w * h / 360.0f - 4, r.y, 8, r.h };
    cui_draw_rect_border(ui, th, CUI_CORNERS(4), cui__hsv(h, 1, 1), cui__rgba(255, 255, 255, 255), 2);
}

/* Opens the popover of the color picker with this id from code (a double
 * click on a gradient stop), in sync with the color it will edit. */
void cui__color_picker_open(cui_ctx *ui, cui_id id, cui_color value) {
    cui__color_state *st = cui_state(ui, id, sizeof *st);
    cui__rgb_to_hsv(value, &st->h, &st->s, &st->v);
    st->init = true;
    cui__popup_set(ui, id ^ 0xC010Eu, true);
    cui_popup *p = cui__popup_get(ui, id ^ 0xC010Eu, false);
    if (p) p->at_mouse = false;
}

bool cui_color_picker(cui_ctx *ui, const char *id_str, cui_color *value) {
    cui_id id = cui_make_id(ui, id_str);
    cui_id pid = id ^ 0xC010Eu;
    cui__color_state *st = cui_state(ui, id, sizeof *st);
    cui_color old = *value;
    if (!st->init) { cui__rgb_to_hsv(*value, &st->h, &st->s, &st->v); st->init = true; }
    cui_popup *p = cui__popup_get(ui, pid, true);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    if (r.clicked && p) {
        cui__popup_set(ui, pid, !p->open);
        p->at_mouse = false;
        cui__rgb_to_hsv(*value, &st->h, &st->s, &st->v);
    }
    float *hv = cui_state(ui, id ^ 1u, sizeof(float));
    cui__tween(ui, hv, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 8;
    n->h = CUI_PX(36); n->pad[1] = CUI__FIELD_PAD; n->pad[3] = 6;
    n->radius = CUI_CORNERS(CUI__R_CONTROL(ui, 36));
    n->bg = CUI_C(ui, SURFACE);
    n->border = 1;
    n->border_color = cui__control_edge(ui, *hv);
    n->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, i, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, i);
    int sw = cui__leaf(ui, 0, CUI_PX(24), CUI_PX(24));
    ui->nodes[sw].bg = *value;
    ui->nodes[sw].radius = CUI_CORNERS(12);
    ui->nodes[sw].border = 1;
    ui->nodes[sw].border_color = cui__rgba(0, 0, 0, 30);
    int t = cui__text_node(ui, cui__frame_printf(ui, "#%02X%02X%02X", value->r, value->g, value->b), -1, CUI_FONT_MONO,
                           cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    cui__close(ui);
    ui->last_id = id;
    if (cui__popup_begin(ui, pid, id, CUI_PLACE_BOTTOM_START, 240, 12, 12, false)) {
        cui_id svid = cui_make_id(ui, "#sv"), hid = cui_make_id(ui, "#hue");
        cui_interact sr = cui__interact(ui, svid, CUI_IF_DRAG);
        cui_interact hr = cui__interact(ui, hid, CUI_IF_DRAG);
        cui_rect svr = cui__prev_rect(ui, svid), hrr = cui__prev_rect(ui, hid);
        if (sr.active && ui->mdown[0] && svr.w > 0) {
            st->s = cui__clampf((ui->mx - svr.x) / svr.w, 0, 1);
            st->v = 1 - cui__clampf((ui->my - svr.y) / svr.h, 0, 1);
        }
        if (hr.active && ui->mdown[0] && hrr.w > 0) st->h = cui__clampf((ui->mx - hrr.x) / hrr.w, 0, 0.9999f) * 360.0f;
        if ((sr.active || hr.active) && ui->mdown[0]) *value = cui__hsv(st->h, st->s, st->v);
        int svn = cui__leaf(ui, svid, CUI_FILL, CUI_PX(150));
        cui__mark_interactive(ui, svn, 0);
        cui__sv_paint *sp = cui__frame_alloc(ui, sizeof *sp);
        sp->h = st->h; sp->s = st->s; sp->v = st->v;
        ui->nodes[svn].paint = cui__paint_sv;
        ui->nodes[svn].paint_data = sp;
        ui->nodes[svn].radius = CUI_CORNERS(10);
        int hn = cui__leaf(ui, hid, CUI_FILL, CUI_PX(14));
        cui__mark_interactive(ui, hn, 0);
        float *hp = cui__frame_alloc(ui, sizeof(float));
        *hp = st->h;
        ui->nodes[hn].paint = cui__paint_hue;
        ui->nodes[hn].paint_data = hp;
        static const uint32_t presets[8] = { 0x111111, 0x7747ff, 0x0562ef, 0x00864b, 0xf3ad20, 0xc25300, 0xd1403a, 0xd23286 };
        int row = cui__node_new(ui, 0);
        ui->nodes[row].dir = CUI_ROW; ui->nodes[row].justify = CUI_JUSTIFY_BETWEEN;
        cui__open(ui, row);
        for (int k = 0; k < 8; k++) {
            cui_id cid = cui__hash(&k, sizeof k, pid);
            cui_interact cr = cui__interact(ui, cid, 0);
            int c = cui__leaf(ui, cid, CUI_PX(22), CUI_PX(22));
            cui_color pc = CUI_RGB(presets[k]);
            ui->nodes[c].bg = pc;
            ui->nodes[c].radius = CUI_CORNERS(11);
            bool same = pc.r == value->r && pc.g == value->g && pc.b == value->b;
            if (same) { ui->nodes[c].border = 2; ui->nodes[c].border_color = CUI_C(ui, SURFACE_RAISED); ui->nodes[c].elevation = CUI_ELEVATION_RESTING; }
            cui__mark_interactive(ui, c, 0);
            if (cr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
            if (cr.clicked) { *value = pc; cui__rgb_to_hsv(pc, &st->h, &st->s, &st->v); }
        }
        cui__close(ui);
        cui__popup_end(ui);
    }
    return memcmp(&old, value, sizeof old) != 0;
}

/* ------------------------------------------------------------------------ */
/* Command palette                                                          */
/* ------------------------------------------------------------------------ */

typedef struct { char query[128]; int highlight; bool was_open; cui_spring t; } cui__palette_state;

static int cui__fuzzy(const char *text, const char *q) {
    /* subsequence match; higher is better, -1 = no match */
    if (!q || !*q) return 1;
    int score = 0, run = 0;
    const char *t = text;
    for (const char *c = q; *c; c++) {
        char qc = (*c >= 'A' && *c <= 'Z') ? *c + 32 : *c;
        if (qc == ' ') continue;
        bool found = false;
        while (*t) {
            char tc = (*t >= 'A' && *t <= 'Z') ? *t + 32 : *t;
            bool word_start = t == text || t[-1] == ' ';
            t++;
            if (tc == qc) { found = true; run++; score += 1 + run + (word_start ? 3 : 0); break; }
            run = 0;
        }
        if (!found) return -1;
    }
    return score;
}

int cui_command_palette(cui_ctx *ui, bool *open, const cui_command *cmds, int count) {
    cui_id id = cui_make_id(ui, "#command-palette");
    cui__palette_state *st = cui_state(ui, id, sizeof *st);
    bool is_open = open && *open;
    if (is_open && !st->was_open) { st->query[0] = 0; st->highlight = 0; }
    st->was_open = is_open;
    if (is_open) cui__spring(ui, &st->t, 1.0f, CUI__SPRING_ENTER);
    else cui__spring(ui, &st->t, 0.0f, CUI__SPRING_EXIT);
    if (!is_open && st->t.x <= 0.01f) { st->t.x = 0; st->t.v = 0; return -1; }
    float t = cui__clampf(st->t.x, 0, 1);
    int chosen = -1;
    cui_id scrim_id = id ^ 0x5C819u;
    if (is_open) {
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) { *open = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
        if (ui->mreleased[0] && ui->hot == scrim_id && ui->active == scrim_id) *open = false;
    }
    cui__interact(ui, scrim_id, 0);
    if (is_open) { ui->modal_layer = id; ui->modal_z = 0x7FFF0000; }
    int root = cui__float_begin(ui, id, 0x7FFF0000, 0, CUI__PLACE_FULL, 0);
    cui_node *rn = &ui->nodes[root];
    rn->id = scrim_id;
    rn->bg = cui_alpha(CUI_C(ui, SCRIM), t * 0.7f);
    rn->flags |= CUI_NF_BLOCKER | CUI_NF_INTERACTIVE;
    rn->dir = CUI_STACK; rn->align = CUI_ALIGN_CENTER; rn->justify = CUI_JUSTIFY_START;
    rn->pad[0] = CUI_MAX(24.0f, ui->vh * 0.14f);
    if (!is_open) cui_push_disabled(ui, true);
    cui_push_id(ui, "#palette");
    int panel = cui__node_new(ui, id ^ 0x9A4Eu);
    cui_node *pn = &ui->nodes[panel];
    pn->w = CUI_PX(CUI_MIN(580.0f, ui->vw - 32));
    pn->bg = CUI_C(ui, SURFACE_RAISED);
    pn->border = 1;
    pn->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui_alpha(cui__rgba(0, 0, 0, 255), 0.07f);
    pn->radius = CUI_CORNERS(ui->theme.radius_panel);
    pn->elevation = CUI_ELEVATION_FLOATING;
    pn->flags |= CUI_NF_HIT_OPAQUE | CUI_NF_CLIP;
    pn->opacity = t;
    pn->scale = CUI__ENTER_SCALE + (1.0f - CUI__ENTER_SCALE) * t;
    pn->ty = (1 - t) * -8;
    cui__open(ui, panel);
    /* search */
    int sr = cui__node_new(ui, 0);
    ui->nodes[sr].pad[0] = ui->nodes[sr].pad[2] = 8; ui->nodes[sr].pad[1] = ui->nodes[sr].pad[3] = 10;
    cui__open(ui, sr);
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.placeholder = "Type a command or search";
    io.size = CUI_SIZE_LG;
    cui_id qid = cui_make_id(ui, "#query");
    if (is_open) ui->focus = qid;
    int f = cui__text_field(ui, qid, st->query, sizeof st->query, &io, false, 1, CUI_ICON_SEARCH);
    if (f & CUI_CHANGED) st->highlight = 0;
    cui__close(ui);
    int sep = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[sep].bg = CUI_C(ui, BORDER);
    /* filtered list */
    int *order = cui__frame_alloc(ui, sizeof(int) * (size_t)(count > 0 ? count : 1));
    int *scores = cui__frame_alloc(ui, sizeof(int) * (size_t)(count > 0 ? count : 1));
    int n = 0;
    for (int k = 0; k < count; k++) {
        int s = cui__fuzzy(cmds[k].title, st->query);
        if (s < 0) continue;
        order[n] = k; scores[n] = s; n++;
    }
    if (st->query[0]) {
        for (int a = 1; a < n; a++) {
            int o2 = order[a], s2 = scores[a], b = a - 1;
            while (b >= 0 && scores[b] < s2) { order[b + 1] = order[b]; scores[b + 1] = scores[b]; b--; }
            order[b + 1] = o2; scores[b + 1] = s2;
        }
    }
    if (n > 0) {
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) st->highlight = (st->highlight + 1) % n;
        if (cui_key_pressed(ui, CUI_KEY_UP)) st->highlight = (st->highlight + n - 1) % n;
        if (st->highlight >= n) st->highlight = n - 1;
    }
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.max_height = 340;
    sb.pad = 6;
    cui_id list_id = cui_make_id(ui, "#list");
    cui__scroll_begin_ex(ui, list_id, &sb, false);
    ui->nodes[cui__current(ui)].h = CUI_FIT;
    const char *last_group = NULL;
    for (int j = 0; j < n; j++) {
        const cui_command *c = &cmds[order[j]];
        if (!st->query[0] && c->group && (!last_group || strcmp(last_group, c->group) != 0)) {
            int gl = cui__node_new(ui, 0);
            ui->nodes[gl].pad[0] = j == 0 ? 4 : 10; ui->nodes[gl].pad[2] = 4; ui->nodes[gl].pad[3] = 10;
            cui__open(ui, gl);
            int gt = cui__text_node(ui, c->group, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[gt].line_height = 1.3f;
            cui__close(ui);
            last_group = c->group;
        }
        cui_id cid = cui__hash(&order[j], sizeof(int), id);
        cui_interact r = cui__interact(ui, cid, 0);
        if (r.hovered && (ui->mx != ui->pmx || ui->my != ui->pmy)) st->highlight = j;
        bool hl = st->highlight == j;
        int it = cui__node_new(ui, cid);
        cui_node *in = &ui->nodes[it];
        in->dir = CUI_ROW; in->align = CUI_ALIGN_CENTER; in->gap = 12;
        in->h = CUI_PX(40); in->pad[1] = 12; in->pad[3] = 12;
        in->radius = CUI_CORNERS(ui->theme.radius_panel - 6);
        in->bg = hl ? CUI_C(ui, ACTIVE) : cui__rgba(0, 0, 0, 0);
        cui__mark_interactive(ui, it, 0);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, it);
        if (c->icon) cui__icon_node(ui, c->icon, 16, hl ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
        int tt = cui__text_node(ui, c->title, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body + 0.5f), CUI_C(ui, FOREGROUND), false);
        ui->nodes[tt].line_height = 1.3f;
        ui->nodes[tt].w = CUI_FILL;
        if (c->shortcut) cui_kbd(ui, c->shortcut);
        cui__close(ui);
        if (hl && cui__has_prev_rect(ui, cid)) {
            /* keep the highlighted row visible */
            cui_scroll_state *ss = cui_state(ui, list_id, sizeof *ss);
            cui_rect rr = cui__prev_rect(ui, cid), lr = cui__prev_rect(ui, list_id);
            if (cui_key_pressed(ui, CUI_KEY_DOWN) || cui_key_pressed(ui, CUI_KEY_UP)) {
                if (rr.y + rr.h > lr.y + lr.h) ss->ty += rr.y + rr.h - (lr.y + lr.h) + 6;
                if (rr.y < lr.y) ss->ty -= lr.y - rr.y + 6;
            }
        }
        if (r.clicked || (hl && cui_key_pressed(ui, CUI_KEY_ENTER))) chosen = order[j];
    }
    if (n == 0) {
        int e = cui__node_new(ui, 0);
        ui->nodes[e].pad[0] = ui->nodes[e].pad[2] = 28;
        ui->nodes[e].align = CUI_ALIGN_CENTER;
        cui__open(ui, e);
        cui__text_node(ui, "No results found", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
        cui__close(ui);
    }
    cui__scroll_end_ex(ui);
    /* footer hints */
    int sep2 = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[sep2].bg = CUI_C(ui, BORDER);
    int foot = cui__node_new(ui, 0);
    ui->nodes[foot].dir = CUI_ROW; ui->nodes[foot].gap = 16;
    ui->nodes[foot].flags |= CUI_NF_ROW_WRAP;   /* a phone: the hints take two lines rather than cut to "Na..." */
    ui->nodes[foot].pad[0] = ui->nodes[foot].pad[2] = 10; ui->nodes[foot].pad[3] = ui->nodes[foot].pad[1] = 14;
    ui->nodes[foot].bg = CUI_C(ui, SURFACE_MUTED);
    ui->nodes[foot].radius.bl = ui->nodes[foot].radius.br = ui->theme.radius_panel - 1;
    cui__open(ui, foot);
    const char *hints[3][2] = { { "\xe2\x86\x91 \xe2\x86\x93", "Navigate" }, { "\xe2\x86\xb5", "Select" }, { "Esc", "Close" } };
    for (int k = 0; k < 3; k++) {
        int hrow = cui__node_new(ui, 0);
        ui->nodes[hrow].dir = CUI_ROW; ui->nodes[hrow].gap = 6;
        cui__open(ui, hrow);
        cui_kbd(ui, hints[k][0]);
        int ht = cui__text_node(ui, hints[k][1], -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[ht].line_height = 1.3f;
        cui__close(ui);
    }
    cui__close(ui);
    cui__close(ui);  /* panel */
    cui_pop_id(ui);
    if (!is_open) cui_pop_disabled(ui);
    cui__float_end(ui);
    if (chosen >= 0 && open) { *open = false; ui->kpressed[CUI_KEY_ENTER] = 0; }
    return chosen;
}

/* ------------------------------------------------------------------------ */
/* Frame hooks                                                              */
/* ------------------------------------------------------------------------ */

void cui__widgets_begin_frame(cui_ctx *ui) {
    ui->pb_depth = 0;
    ui->mb_depth = 0;
    ui->vl_depth = 0;
    ui->acc_depth = 0;
    ui->tree_depth = 0;
    ui->button_group_depth = 0;
    ui->reorder = NULL;
    ui->dialog_depth = 0;
    ui->split = NULL;
    ui->table = NULL;
}

void cui__widgets_end_frame(cui_ctx *ui) {
    /* close any scopes left open by early returns */
    while (ui->pb_depth > 0) cui__popup_end(ui);
    cui__toasts_render(ui);
    cui__tooltip_render(ui);
    ui->pmx = ui->mx;
    ui->pmy = ui->my;
}
