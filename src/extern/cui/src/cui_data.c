/* Data display: accordion, tree view, list items, virtual lists and tables. */
#include "cui_internal.h"

static float cui__fs(cui_ctx *ui, float size) { return size * ui->theme.font_scale; }
void cui__paint_focus_ring_node(cui_ctx *ui, cui_node *n, void *data);

/* ------------------------------------------------------------------------ */
/* Accordion                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring t; float hover; float content_h; bool init; } cui__acc_state;

typedef struct { float angle; cui_color color; float size; const char *icon; } cui__rot_icon;

static void cui__paint_rot_icon(cui_ctx *ui, cui_node *n, void *data) {
    cui__rot_icon *p = data;
    /* rotate a chevron by drawing it as a path: 0 = pointing down */
    cui_rect r = n->rect;
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    float s = p->size * 0.22f;
    float ca = cosf(p->angle), sa = sinf(p->angle);
    cui_vec2 pts[3] = { { -s * 1.6f, -s * 0.8f }, { 0, s * 0.8f }, { s * 1.6f, -s * 0.8f } };
    for (int k = 0; k < 3; k++) {
        float x = pts[k].x, y = pts[k].y;
        pts[k].x = cx + x * ca - y * sa;
        pts[k].y = cy + x * sa + y * ca;
    }
    cui_draw_polyline(ui, pts, 3, 1.6f, p->color, false);
}

void cui__chevron_node(cui_ctx *ui, float angle, float size, cui_color color) {
    int i = cui__leaf(ui, 0, CUI_PX(size), CUI_PX(size));
    cui__rot_icon *p = cui__frame_alloc(ui, sizeof *p);
    p->angle = angle; p->color = color; p->size = size;
    ui->nodes[i].paint = cui__paint_rot_icon;
    ui->nodes[i].paint_data = p;
    ui->nodes[i].self_align = CUI_ALIGN_CENTER;
}

bool cui_accordion_begin(cui_ctx *ui, const char *title, bool *open) {
    cui_id id = cui_make_id(ui, title) ^ 0xACC00006u;
    cui__acc_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    if (r.clicked) *open = !*open;
    if (!st->init) { st->t.x = *open ? 1.0f : 0.0f; st->init = true; }
    cui__spring(ui, &st->t, *open ? 1.0f : 0.0f, CUI_SPRING_SMOOTH);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    float t = cui__clampf(st->t.x, 0, 1);
    bool first = ui->nodes[cui__current(ui)].child_count == 0;
    int wrap = cui__node_new(ui, 0);
    cui_node *wn = &ui->nodes[wrap];
    wn->w = CUI_FILL; wn->self_align = CUI_ALIGN_STRETCH;
    cui__open(ui, wrap);
    if (!first) {   /* hairlines between items, none above the first or below the last */
        int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
        ui->nodes[line].bg = CUI_C(ui, BORDER_SUBTLE);
    }
    int head = cui__node_new(ui, id);
    cui_node *hn = &ui->nodes[head];
    hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER; hn->gap = 12;
    hn->pad[0] = hn->pad[2] = 14;
    hn->w = CUI_FILL;
    hn->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    cui__mark_interactive(ui, head, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, head);
    int tt = cui__text_node(ui, title, cui__display_len(title), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body + 1), CUI_C(ui, FOREGROUND), true);
    ui->nodes[tt].w = CUI_FILL;
    ui->nodes[tt].line_height = 1.35f;
    cui__chevron_node(ui, t * CUI_PI, 16, cui_mix(CUI_C(ui, TEXT_MUTED), CUI_C(ui, FOREGROUND), st->hover));
    cui__close(ui);
    if (r.focus_visible) {
        float *fa = cui__frame_alloc(ui, sizeof(float));
        *fa = 1;
        ui->nodes[head].paint_data = fa;
        ui->nodes[head].paint = cui__paint_focus_ring_node;
        ui->nodes[head].flags |= CUI_NF_PAINT_AFTER;
    }
    if (t <= 0.001f && !*open) {
        cui__close(ui);
        return false;
    }
    /* clip box whose height animates; content measured from the previous frame */
    cui_id cid = id ^ 0xC0u;
    cui_rect cr = cui__prev_rect(ui, cid);
    if (cr.h > 0) st->content_h = cr.h;
    int clip = cui__node_new(ui, 0);
    cui_node *cn = &ui->nodes[clip];
    cn->flags |= CUI_NF_CLIP;
    cn->w = CUI_FILL;
    if (t < 0.999f && st->content_h > 0) cn->h = CUI_PX(st->content_h * t);
    cn->opacity = 0.25f + 0.75f * t;
    cui__open(ui, clip);
    int content = cui__node_new(ui, cid);
    cui_node *ct = &ui->nodes[content];
    ct->gap = 8; ct->pad[2] = 16;
    ct->w = CUI_FILL;
    cui__open(ui, content);
    bool closing = !*open;
    if (ui->acc_depth < 32) ui->acc_dis[ui->acc_depth] = closing;
    ui->acc_depth++;
    if (closing) cui_push_disabled(ui, true);
    return true;
}

void cui_accordion_end(cui_ctx *ui) {
    if (ui->acc_depth > 0) {
        ui->acc_depth--;
        if (ui->acc_depth < 32 && ui->acc_dis[ui->acc_depth]) cui_pop_disabled(ui);
    }
    cui__close(ui);   /* content */
    cui__close(ui);   /* clip */
    cui__close(ui);   /* wrap */
}

/* ------------------------------------------------------------------------ */
/* Tree                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring t; float hover; bool init; } cui__tree_state;

bool cui_tree_begin(cui_ctx *ui, const char *label, bool *open, const cui_tree_opts *o) {
    cui_tree_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, label) ^ 0x7EEE0007u;
    cui__tree_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    bool is_open = open && *open && !o->leaf;
    if (r.clicked && open && !o->leaf) { *open = !*open; is_open = *open; }
    if (r.focus_visible && open && !o->leaf) {
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { *open = true; is_open = true; }
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) { *open = false; is_open = false; }
    }
    ui->tree_clicked = r.clicked;
    if (!st->init) { st->t.x = is_open ? 1.0f : 0.0f; st->init = true; }
    cui__spring(ui, &st->t, is_open ? 1.0f : 0.0f, CUI__SPRING_EXPAND);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int row = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[row];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 6;
    n->h = CUI_PX(30);
    n->pad[3] = 6 + 16.0f * (float)ui->tree_depth; n->pad[1] = 8;
    n->w = CUI_FILL; n->self_align = CUI_ALIGN_STRETCH;
    n->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    n->bg = o->selected ? CUI_C(ui, ACTIVE) : cui_alpha(CUI_C(ui, HOVER), st->hover);
    cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, row);
    if (o->leaf) cui_gap(ui, 14);
    else cui__chevron_node(ui, -CUI_PI * 0.5f + st->t.x * CUI_PI * 0.5f, 14, CUI_C(ui, TEXT_MUTED));
    const char *icon = o->icon ? o->icon : (o->leaf ? CUI_ICON_FILE : (is_open ? CUI_ICON_FOLDER_OPEN : CUI_ICON_FOLDER));
    cui__icon_node(ui, icon, 16, o->selected ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
    int t = cui__text_node(ui, label, cui__display_len(label), o->selected ? CUI_FONT_MEDIUM : CUI_FONT_BODY,
                           cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].w = CUI_FILL;
    cui__close(ui);
    if (r.focus_visible) {
        float *fa = cui__frame_alloc(ui, sizeof(float));
        *fa = 1;
        ui->nodes[row].paint_data = fa;
        ui->nodes[row].paint = cui__paint_focus_ring_node;
        ui->nodes[row].flags |= CUI_NF_PAINT_AFTER;
    }
    ui->last_id = id;
    if (!is_open) return false;
    cui_push_id(ui, label);
    ui->tree_depth++;
    int kids = cui__node_new(ui, 0);
    ui->nodes[kids].gap = 1;
    ui->nodes[kids].w = CUI_FILL;
    ui->nodes[kids].opacity = 0.3f + 0.7f * cui__clampf(st->t.x, 0, 1);
    ui->nodes[kids].ty = (1 - cui__clampf(st->t.x, 0, 1)) * -4;
    cui__open(ui, kids);
    return true;
}

void cui_tree_end(cui_ctx *ui) {
    cui__close(ui);
    if (ui->tree_depth > 0) ui->tree_depth--;
    cui_pop_id(ui);
}

bool cui_tree_clicked(cui_ctx *ui) { return ui->tree_clicked; }

/* ------------------------------------------------------------------------ */
/* List item                                                                */
/* ------------------------------------------------------------------------ */

bool cui_list_item_ex(cui_ctx *ui, const char *title, const cui_list_item_opts *o) {
    cui_list_item_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, title) ^ 0x115E0008u;
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    float hv_v = cui__hover_fade(ui, id, r.hovered, CUI__T_HOVER), *hv = &hv_v;
    int row = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[row];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 12;
    n->pad[0] = n->pad[2] = o->subtitle ? 10 : 8; n->pad[1] = n->pad[3] = 12;
    n->min_h = o->subtitle ? 52 : 38;
    n->w = CUI_FILL; n->self_align = CUI_ALIGN_STRETCH;
    n->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    n->bg = o->selected ? CUI_C(ui, ACTIVE) : cui_alpha(CUI_C(ui, HOVER), *hv);
    cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, row);
    if (o->avatar) {
        cui_avatar_opts ao;
        memset(&ao, 0, sizeof ao);
        ao.size = o->subtitle ? 34 : 26;
        cui_avatar_ex(ui, o->avatar, &ao);
    } else if (o->icon) cui__icon_node(ui, o->icon, 16, CUI_C(ui, TEXT_SECONDARY));
    int col = cui__node_new(ui, 0);
    ui->nodes[col].gap = 1; ui->nodes[col].w = CUI_FILL;
    cui__open(ui, col);
    int t = cui__text_node(ui, title, cui__display_len(title), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.35f;
    if (o->subtitle) {
        int s = cui__text_node(ui, o->subtitle, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[s].line_height = 1.4f;
    }
    cui__close(ui);
    if (o->trailing) {
        int tr = cui__text_node(ui, o->trailing, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[tr].line_height = 1.3f;
        ui->nodes[tr].flags |= CUI_NF_TABULAR;
    }
    cui__close(ui);
    if (r.focus_visible) {
        float *fa = cui__frame_alloc(ui, sizeof(float));
        *fa = 1;
        ui->nodes[row].paint_data = fa;
        ui->nodes[row].paint = cui__paint_focus_ring_node;
        ui->nodes[row].flags |= CUI_NF_PAINT_AFTER;
    }
    ui->last_id = id;
    return r.clicked;
}

/* ------------------------------------------------------------------------ */
/* Virtual list                                                             */
/* ------------------------------------------------------------------------ */


void cui_virtual_list_begin(cui_ctx *ui, const char *id_str, int count, float row_h, cui_size height, int *first, int *last) {
    cui_id id = cui_make_id(ui, id_str);
    cui_scroll_state *st = cui_state(ui, id, sizeof *st);
    cui_box b;
    memset(&b, 0, sizeof b);
    b.height = height.kind == CUI_SIZE_FIT ? CUI_FILL : height;
    b.width = CUI_FILL;
    if (row_h <= 0) row_h = 36;
    float view_h = st->view_h > 0 ? st->view_h : 400;
    /* figure out the visible window from the target offset so fast scrolls do not flash */
    cui__scroll_begin_ex(ui, id, &b, false);
    float y = ui->nodes[cui__current(ui)].scroll_y;
    int f = (int)floorf(y / row_h) - 2;
    int l = (int)ceilf((y + view_h) / row_h) + 2;
    if (f < 0) f = 0;
    if (l > count) l = count;
    if (l < f) l = f;
    *first = f;
    *last = l;
    /* spacer above */
    if (f > 0) cui__leaf(ui, 0, CUI_FILL, CUI_PX(f * row_h));
    cui__vlist_build *vb = &ui->vl_stack[ui->vl_depth < 8 ? ui->vl_depth : 7];
    if (ui->vl_depth < 8) ui->vl_depth++;
    vb->count = count; vb->row_h = row_h; vb->first = f;
    /* rows are placed in a column of fixed-height rows */
    int rows = cui__node_new(ui, 0);
    ui->nodes[rows].w = CUI_FILL;
    ui->nodes[rows].h = CUI_PX((l - f) * row_h);
    cui__open(ui, rows);
    (void)l;
}

void cui_virtual_list_end(cui_ctx *ui) {
    cui__close(ui);
    if (ui->vl_depth > 0) {
        cui__vlist_build *vb = &ui->vl_stack[--ui->vl_depth];
        int rows = ui->nodes[cui__current(ui)].last;
        float shown = rows >= 0 ? ui->nodes[rows].h.value : 0;
        float rest = vb->count * vb->row_h - vb->first * vb->row_h - shown;
        if (rest > 0) cui__leaf(ui, 0, CUI_FILL, CUI_PX(rest));
    }
    cui__scroll_end_ex(ui);
}

/* ------------------------------------------------------------------------ */
/* Table                                                                    */
/* ------------------------------------------------------------------------ */

#define CUI__TABLE_MIN_COL 96   /* narrowest a flexible column gets before the table scrolls */

struct cui_table_build {
    cui_id id;
    const cui_table_column *cols;
    int ncols;
    int col;              /* next cell index */
    int row_index;
    bool in_row;
    bool compact, striped, selectable;
    bool scroll;
    struct cui_table_build *prev;
};

static void cui__table_close_row(cui_ctx *ui, struct cui_table_build *t) {
    if (!t->in_row) return;
    if (t->col > 0) cui__close(ui);   /* last cell */
    while (t->col < t->ncols) { /* missing cells */
        int c = cui__node_new(ui, 0);
        ui->nodes[c].w = t->cols[t->col].width.kind ? t->cols[t->col].width : CUI_FILL;
        t->col++;
    }
    cui__close(ui);   /* row */
    t->in_row = false;
}

bool cui_table_begin_ex(cui_ctx *ui, const char *id_str, const cui_table_column *cols, int ncols, const cui_table_opts *o) {
    cui_table_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    struct cui_table_build *t = cui__frame_alloc(ui, sizeof *t);
    t->id = id; t->cols = cols; t->ncols = ncols;
    t->compact = o->compact; t->striped = o->striped; t->selectable = o->selectable;
    t->prev = ui->table;
    ui->table = t;
    cui_push_id(ui, id_str);
    int wrap = cui__node_new(ui, id);
    cui_node *wn = &ui->nodes[wrap];
    wn->w = CUI_FILL; wn->self_align = CUI_ALIGN_STRETCH;
    wn->border = 1; wn->border_color = CUI_C(ui, BORDER);
    wn->radius = CUI_CORNERS(ui->theme.radius_control);
    wn->bg = CUI_C(ui, SURFACE);
    wn->flags |= CUI_NF_CLIP;
    if (o->height.kind) wn->h = o->height;
    cui__open(ui, wrap);
    /* Narrower than its columns need, the table scrolls sideways instead of
     * squeezing cells: flexible columns keep at least CUI__TABLE_MIN_COL. */
    float min_w = 16;
    for (int k = 0; k < ncols; k++) min_w += cols[k].width.kind == CUI_SIZE_FIXED ? cols[k].width.value : CUI__TABLE_MIN_COL;
    cui_box xb;
    memset(&xb, 0, sizeof xb);
    xb.width = CUI_FILL;
    if (o->height.kind) xb.height = CUI_FILL;
    int xs = cui__scroll_begin_ex(ui, id ^ 0x7AB1E5u, &xb, true);
    ui->nodes[xs].flags &= ~(uint32_t)CUI_NF_SCROLL_Y;
    /* the edge with more columns past it fades, so a phone shows the table goes on */
    ((cui_scroll_state *)cui_state(ui, id ^ 0x7AB1E5u, sizeof(cui_scroll_state)))->fade = ui->nodes[wrap].bg;
    int content = cui__node_new(ui, 0);
    cui_node *ct = &ui->nodes[content];
    ct->pref_hint = min_w;
    ct->self_align = CUI_ALIGN_STRETCH;
    if (o->height.kind) ct->h = CUI_FILL;
    cui__open(ui, content);
    /* header */
    int head = cui__node_new(ui, 0);
    cui_node *hn = &ui->nodes[head];
    hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER;
    hn->h = CUI_PX(38);
    hn->pad[1] = hn->pad[3] = 8;
    hn->bg = CUI_C(ui, SURFACE_MUTED);
    hn->w = CUI_FILL;
    hn->radius.tl = hn->radius.tr = ui->theme.radius_control - 1;
    cui__open(ui, head);
    for (int k = 0; k < ncols; k++) {
        const cui_table_column *c = &cols[k];
        cui_id hid = cui__hash(&k, sizeof k, id ^ 0x4EADu);
        cui_interact r;
        memset(&r, 0, sizeof r);
        if (c->sortable && o->sort) {
            r = cui__interact(ui, hid, CUI_IF_FOCUSABLE);
            if (r.clicked) {
                if (o->sort->column == k) o->sort->descending = !o->sort->descending;
                else { o->sort->column = k; o->sort->descending = false; }
            }
        }
        int cell = cui__node_new(ui, hid);
        cui_node *cn = &ui->nodes[cell];
        cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 4;
        cn->w = c->width.kind ? c->width : CUI_FILL;
        cn->h = CUI_FILL;
        cn->pad[1] = cn->pad[3] = 8;
        cn->justify = c->align == CUI_TEXT_RIGHT ? CUI_JUSTIFY_END : c->align == CUI_TEXT_CENTER ? CUI_JUSTIFY_CENTER : CUI_JUSTIFY_START;
        if (c->sortable && o->sort) {
            cui__mark_interactive(ui, cell, CUI_IF_FOCUSABLE);
            if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        }
        cui__open(ui, cell);
        bool sorted = o->sort && o->sort->column == k && c->sortable;
        int tx = cui__text_node(ui, c->title ? c->title : "", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm),
                                (sorted || r.hovered) ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[tx].line_height = 1.3f;
        if (c->sortable && o->sort) {
            const char *ic = sorted ? (o->sort->descending ? CUI_ICON_ARROW_DOWN : CUI_ICON_ARROW_UP) : CUI_ICON_CHEVRONS_UP_DOWN;
            cui__icon_node(ui, ic, 12, sorted ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_MUTED));
        }
        cui__close(ui);
    }
    cui__close(ui);
    int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[line].bg = CUI_C(ui, BORDER);
    t->scroll = o->height.kind != CUI_SIZE_FIT;
    if (t->scroll) {
        cui_box sb;
        memset(&sb, 0, sizeof sb);
        sb.height = CUI_FILL;
        cui__scroll_begin_ex(ui, cui_make_id(ui, "#body"), &sb, false);
    }
    return true;
}

bool cui_table_row(cui_ctx *ui, bool selected) {
    struct cui_table_build *t = ui->table;
    if (!t) return false;
    cui__table_close_row(ui, t);
    if (t->row_index > 0) {
        int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
        ui->nodes[line].bg = CUI_C(ui, BORDER_SUBTLE);
    }
    int ri = t->row_index++;
    cui_id rid = cui__hash(&ri, sizeof ri, t->id ^ 0x20Bu);
    cui_interact r = cui__interact(ui, rid, t->selectable ? CUI_IF_FOCUSABLE : 0);
    float hv_v = cui__hover_fade(ui, rid, r.hovered, CUI__T_HIGHLIGHT), *hv = &hv_v;
    int row = cui__node_new(ui, rid);
    cui_node *n = &ui->nodes[row];
    n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER;
    n->min_h = t->compact ? 36 : 44;
    n->pad[1] = n->pad[3] = 8;
    n->w = CUI_FILL;
    cui_color bg = cui__rgba(0, 0, 0, 0);
    if (t->striped && (ri & 1)) bg = cui_alpha(CUI_C(ui, FOREGROUND), 0.018f);
    if (selected) bg = CUI_C(ui, ACCENT_SUBTLE);
    else bg = cui_mix(bg, CUI_C(ui, HOVER), *hv);
    n->bg = bg;
    cui__mark_interactive(ui, row, t->selectable ? CUI_IF_FOCUSABLE : 0);
    if (r.hovered && t->selectable) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, row);
    t->in_row = true;
    t->col = 0;
    cui_push_id_int(ui, ri);
    cui_pop_id(ui);
    return r.clicked;
}

void cui_table_cell(cui_ctx *ui) {
    struct cui_table_build *t = ui->table;
    if (!t || !t->in_row) return;
    if (t->col > 0) cui__close(ui);
    if (t->col >= t->ncols) { t->col++; int c = cui__node_new(ui, 0); cui__open(ui, c); return; }
    const cui_table_column *c = &t->cols[t->col++];
    int cell = cui__node_new(ui, 0);
    cui_node *cn = &ui->nodes[cell];
    cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 8;
    cn->w = c->width.kind ? c->width : CUI_FILL;
    cn->pad[0] = cn->pad[2] = 6; cn->pad[1] = cn->pad[3] = 8;
    cn->justify = c->align == CUI_TEXT_RIGHT ? CUI_JUSTIFY_END : c->align == CUI_TEXT_CENTER ? CUI_JUSTIFY_CENTER : CUI_JUSTIFY_START;
    cn->flags |= CUI_NF_CLIP;
    cui__open(ui, cell);
}

void cui_table_cell_text(cui_ctx *ui, const char *text) {
    cui_table_cell(ui);
    struct cui_table_build *t = ui->table;
    bool right = t && t->col - 1 < t->ncols && t->cols[t->col - 1].align == CUI_TEXT_RIGHT;
    int tx = cui__text_node(ui, text, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[tx].line_height = 1.3f;
    if (right) ui->nodes[tx].flags |= CUI_NF_TABULAR;
}

void cui_table_cell_textf(cui_ctx *ui, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = cui__frame_vprintf(ui, fmt, ap);
    va_end(ap);
    cui_table_cell_text(ui, s);
}

void cui_table_end(cui_ctx *ui) {
    struct cui_table_build *t = ui->table;
    if (!t) return;
    cui__table_close_row(ui, t);
    if (t->scroll) cui__scroll_end_ex(ui);
    cui__close(ui);   /* content */
    cui__scroll_end_ex(ui);
    cui__close(ui);   /* wrap */
    cui_pop_id(ui);
    ui->table = t->prev;
}
