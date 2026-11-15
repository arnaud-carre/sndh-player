/* Tree table (a tree layer over the data grid) and the path bar. */
#include "cui_internal.h"

#define CUI__TT_STEP 16.0f        /* indentation per level */
#define CUI__TT_LOAD_MAX 64       /* load requests per frame */
#define CUI__TT_PAD 12.0f         /* the grid cell's side padding */
#define CUI__TT_CHEV 20.0f        /* the chevron's box */

/* One visible row. node < 0 is the loading row of node -node - 2. */
typedef struct { int node, depth; } cui__tt_row;

typedef struct {
    int cursor, anchor;           /* nodes, -1 for none */
    char typed[64]; int typed_len; double typed_time;
    cui__tt_row *rows; int row_cap;
    bool *sel; int sel_cap;
    bool init;
} cui__tt_state;

typedef struct cui__tt_build {
    cui_id id;
    cui__tt_state *st;
    cui_tree_node *nodes;
    int count;
    cui_tree_table_opts o;
    cui__tt_row *rows;
    int nrows;
    bool *sel;
    int first, last, next, cur;
    int vcursor, vanchor;         /* visible rows, shared with the grid */
    float name_w;                 /* the name column's width last frame */
    bool plain_click, clear_all;
    cui_table_sort sort_before;
    cui_tree_table_events ev;
    int loads[CUI__TT_LOAD_MAX];
} cui__tt_build;

typedef struct { cui__tt_build *b; uint64_t frame; } cui__tt_slot;

static cui__tt_slot *cui__tt_slot_get(cui_ctx *ui) { return cui_state(ui, 0x7EE7AB1Eu, sizeof(cui__tt_slot)); }
static cui__tt_build *cui__tt_current(cui_ctx *ui) {
    cui__tt_slot *s = cui__tt_slot_get(ui);
    return s->b && s->frame == ui->frame ? s->b : NULL;
}

static void cui__tt_free(cui_ctx *ui, void *data) {
    cui__tt_state *st = data;
    cui__free(ui, st->rows);
    cui__free(ui, st->sel);
    st->rows = NULL; st->sel = NULL; st->row_cap = st->sel_cap = 0;
}

static bool cui__tt_ok(const cui__tt_build *b, int n) { return n >= 0 && n < b->count; }
static bool cui__tt_has_kids(const cui__tt_build *b, int n) {
    return (b->nodes[n].flags & CUI_TREE_HAS_CHILDREN) || cui__tt_ok(b, b->nodes[n].first_child);
}

static void cui__tt_push(cui_ctx *ui, cui__tt_build *b, int node, int depth) {
    cui__tt_state *st = b->st;
    CUI__GROW(ui, st->rows, b->nrows, st->row_cap, 1);
    st->rows[b->nrows].node = node;
    st->rows[b->nrows].depth = depth;
    b->nrows++;
}

/* Walks the open part of the tree in order. Iterative (deep trees are
 * fine) and bounded by the node count (a cycle in bad links stops). */
static void cui__tt_flatten(cui_ctx *ui, cui__tt_build *b) {
    b->nrows = 0;
    int n = b->o.root, depth = 0;
    int limit = 2 * b->count + 2;
    if (!cui__tt_ok(b, n)) n = -1;
    while (n >= 0 && b->nrows < limit) {
        cui__tt_push(ui, b, n, depth);
        const cui_tree_node *N = &b->nodes[n];
        bool open = (N->flags & CUI_TREE_OPEN) != 0;
        if (open && (N->flags & CUI_TREE_LOADING)) cui__tt_push(ui, b, -n - 2, depth + 1);
        else if (open && cui__tt_ok(b, N->first_child)) { n = N->first_child; depth++; continue; }
        for (;;) {
            int s = b->nodes[n].next_sibling;
            if (cui__tt_ok(b, s)) { n = s; break; }
            if (depth == 0) { n = -1; break; }
            n = b->nodes[n].parent; depth--;
            if (!cui__tt_ok(b, n)) { n = -1; break; }
        }
    }
    b->rows = b->st->rows;
}

static int cui__tt_find(const cui__tt_build *b, int node) {
    if (node < 0) return -1;
    for (int i = 0; i < b->nrows; i++) if (b->rows[i].node == node) return i;
    return -1;
}

/* The visible row of node, or of its nearest visible ancestor. */
static int cui__tt_visible(const cui__tt_build *b, int node) {
    for (int guard = 0; cui__tt_ok(b, node) && guard <= b->count; guard++) {
        int i = cui__tt_find(b, node);
        if (i >= 0) return i;
        node = b->nodes[node].parent;
    }
    return -1;
}

static int cui__tt_node_of(const cui__tt_build *b, int row) {
    if (row < 0 || row >= b->nrows) return -1;
    int n = b->rows[row].node;
    return n >= 0 ? n : -n - 2;
}

static void cui__tt_expand(cui_ctx *ui, cui__tt_build *b, int n) {
    if (!cui__tt_ok(b, n) || !cui__tt_has_kids(b, n)) return;
    cui_tree_node *N = &b->nodes[n];
    if (N->flags & CUI_TREE_OPEN) return;
    bool needs_load = !cui__tt_ok(b, N->first_child) && !(N->flags & CUI_TREE_LOADING);
    if (needs_load && b->ev.load_count >= CUI__TT_LOAD_MAX) return;
    N->flags |= CUI_TREE_OPEN;
    if (b->ev.expanded < 0) b->ev.expanded = n;
    if (needs_load) {
        N->flags |= CUI_TREE_LOADING;   /* asked once: the flag stays until the caller clears it */
        b->loads[b->ev.load_count++] = n;
    }
    ui->animating = true;
}

static void cui__tt_collapse(cui_ctx *ui, cui__tt_build *b, int n) {
    if (!cui__tt_ok(b, n) || !(b->nodes[n].flags & CUI_TREE_OPEN)) return;
    b->nodes[n].flags &= ~(uint32_t)CUI_TREE_OPEN;
    if (b->ev.collapsed < 0) b->ev.collapsed = n;
    ui->animating = true;
}

/* The keyboard moved the cursor to node n: the selection follows unless
 * Ctrl is held. */
static void cui__tt_move(cui_ctx *ui, cui__tt_build *b, int n) {
    if (!cui__tt_ok(b, n)) return;
    b->st->cursor = n;
    if (!(ui->mods & CUI_MOD_PRIMARY)) {
        for (int k = 0; k < b->count; k++) b->nodes[k].flags &= ~(uint32_t)CUI_TREE_SELECTED;
        b->nodes[n].flags |= CUI_TREE_SELECTED;
        b->st->anchor = n;
        b->ev.selection_changed = true;
    }
}

static bool cui__tt_prefix(const char *s, const char *p, int n) {
    if (!s) return false;
    for (int k = 0; k < n; k++) {
        char a = s[k], c = p[k];
        if (!a) return false;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (a != c) return false;
    }
    return true;
}

static void cui__tt_keys(cui_ctx *ui, cui__tt_build *b) {
    cui__tt_state *st = b->st;
    int cur = cui__tt_ok(b, st->cursor) ? st->cursor : cui__tt_node_of(b, 0);
    if (!cui__tt_ok(b, cur)) return;
    cui_tree_node *N = &b->nodes[cur];
    bool kids = cui__tt_has_kids(b, cur), open = (N->flags & CUI_TREE_OPEN) != 0;
    if (cui_key_pressed(ui, CUI_KEY_RIGHT)) {
        ui->kpressed[CUI_KEY_RIGHT] = 0;
        if (kids && !open) cui__tt_expand(ui, b, cur);
        else if (kids && !(N->flags & CUI_TREE_LOADING) && cui__tt_ok(b, N->first_child)) cui__tt_move(ui, b, N->first_child);
    }
    if (cui_key_pressed(ui, CUI_KEY_LEFT)) {
        ui->kpressed[CUI_KEY_LEFT] = 0;
        if (kids && open) cui__tt_collapse(ui, b, cur);
        else if (cui__tt_ok(b, N->parent) && cui__tt_find(b, N->parent) >= 0) cui__tt_move(ui, b, N->parent);
    }
    if (cui_key_pressed(ui, CUI_KEY_ENTER)) {
        ui->kpressed[CUI_KEY_ENTER] = 0;
        b->ev.activated = cur;
    }
    if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) b->clear_all = true;   /* the grid clears the rows in view */
    if (ui->mods & (CUI_MOD_CTRL | CUI_MOD_ALT | CUI_MOD_SUPER)) return;
    /* typing: * opens every sibling, letters jump to a name */
    bool typed = false;
    for (int i = 0; i < ui->text_in_len; i++) {
        char c = ui->text_in[i];
        if (c == '*') {
            int p = b->nodes[cur].parent;
            int s = cui__tt_ok(b, p) ? b->nodes[p].first_child : b->o.root;
            for (int guard = 0; cui__tt_ok(b, s) && guard < b->count; guard++, s = b->nodes[s].next_sibling) cui__tt_expand(ui, b, s);
            continue;
        }
        if (c == ' ') continue;   /* Space toggles the selection */
        if (ui->time - st->typed_time > 1.0) st->typed_len = 0;
        if (st->typed_len < (int)sizeof st->typed - 1) st->typed[st->typed_len++] = c;
        st->typed[st->typed_len] = 0;
        st->typed_time = ui->time;
        typed = true;
    }
    if (typed && b->o.label && b->nrows > 0) {
        int at = cui__tt_find(b, cur);
        if (at < 0) at = 0;
        int start = st->typed_len == 1 ? at + 1 : at;   /* one letter looks past the current row */
        for (int k = 0; k < b->nrows; k++) {
            int i = (start + k) % b->nrows;
            int n = b->rows[i].node;
            if (n < 0) continue;
            if (cui__tt_prefix(b->o.label(b->o.user, n), st->typed, st->typed_len)) { cui__tt_move(ui, b, n); break; }
        }
    }
}

void cui_tree_table_begin_ex(cui_ctx *ui, const char *id_str, const cui_grid_column *cols, int ncols, cui_tree_node *nodes, int count,
                             const cui_tree_table_opts *o) {
    cui_tree_table_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__tt_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    cui_id sid = b->id ^ 0x7EE5u;
    b->st = cui_state(ui, sid, sizeof *b->st);
    cui__state_set_dtor(ui, sid, cui__tt_free);
    cui__tt_state *st = b->st;
    if (!st->init) { st->cursor = st->anchor = -1; st->init = true; }
    b->nodes = nodes;
    b->count = nodes && count > 0 ? count : 0;
    b->o = *o;
    if (b->o.row_height <= 0) b->o.row_height = 32;
    b->ev.activated = b->ev.expanded = b->ev.collapsed = -1;
    b->ev.load = b->loads;
    b->cur = -1;
    if (o->cursor && cui__tt_ok(b, *o->cursor)) st->cursor = *o->cursor;
    if (!cui__tt_ok(b, st->cursor)) st->cursor = -1;
    if (!cui__tt_ok(b, st->anchor)) st->anchor = -1;
    if (cui__has_prev_rect(ui, b->id ^ 0x4A3E5u)) b->name_w = cui__prev_rect(ui, b->id ^ 0x4A3E5u).w;
    cui__tt_flatten(ui, b);
    if (ui->focus == b->id && b->nrows > 0) {
        cui__tt_keys(ui, b);
        cui__tt_flatten(ui, b);   /* the keys may have opened or closed nodes */
    }
    /* a cursor inside a closed node moves up to it */
    b->vcursor = cui__tt_visible(b, st->cursor);
    if (b->vcursor >= 0) st->cursor = cui__tt_node_of(b, b->vcursor);
    b->vanchor = cui__tt_find(b, st->anchor);
    /* the selection the grid sees: one bool per visible row */
    CUI__GROW(ui, st->sel, 0, st->sel_cap, b->nrows + 1);
    b->sel = st->sel;
    for (int i = 0; i < b->nrows; i++) b->sel[i] = b->rows[i].node >= 0 && (b->nodes[b->rows[i].node].flags & CUI_TREE_SELECTED);
    if (o->sort) b->sort_before = *o->sort;
    cui_grid_opts g;
    memset(&g, 0, sizeof g);
    g.width = o->width; g.height = o->height;
    g.row_height = b->o.row_height;
    g.selected = b->sel;
    g.hide_checkboxes = true;
    g.sort = o->sort;
    g.cursor = &b->vcursor;
    g.anchor = &b->vanchor;
    int before = b->vcursor;
    bool nav = ui->focus == b->id && (cui_key_pressed(ui, CUI_KEY_UP) || cui_key_pressed(ui, CUI_KEY_DOWN) || cui_key_pressed(ui, CUI_KEY_HOME) ||
                                      cui_key_pressed(ui, CUI_KEY_END) || cui_key_pressed(ui, CUI_KEY_PAGE_UP) || cui_key_pressed(ui, CUI_KEY_PAGE_DOWN));
    cui_data_grid_begin_ex(ui, id_str, cols, ncols, b->nrows, &b->first, &b->last, &g);
    b->next = b->first;
    /* arrows moved the grid's cursor: step over loading rows, the selection follows */
    if (nav && b->vcursor != before && b->vcursor >= 0 && b->vcursor < b->nrows) {
        int vc = b->vcursor;
        if (b->rows[vc].node < 0) {
            int dir = before >= 0 && vc < before ? -1 : 1;
            int k = vc + dir;
            if (k < 0 || k >= b->nrows) k = vc - dir;
            vc = k >= 0 && k < b->nrows ? k : 0;
            b->vcursor = vc;
        }
        int n = cui__tt_node_of(b, vc);
        st->cursor = n;
        if (!(ui->mods & (CUI_MOD_SHIFT | CUI_MOD_PRIMARY)) && cui__tt_ok(b, n)) {
            for (int i = 0; i < b->nrows; i++) b->sel[i] = i == vc;
            b->plain_click = true;   /* nodes out of view drop their selection too */
            b->vanchor = vc;
        }
    }
    cui__tt_slot *slot = cui__tt_slot_get(ui);
    slot->b = b;
    slot->frame = ui->frame;
}

typedef struct { int levels; float x0, step; cui_color color; } cui__tt_guides;
static void cui__paint_tt_guides(cui_ctx *ui, cui_node *n, void *data) {
    cui__tt_guides *p = data;
    for (int l = 0; l < p->levels; l++) {
        float x = floorf(n->rect.x + p->x0 + l * p->step);
        cui_draw_rect(ui, (cui_rect){ x, n->rect.y, 1, n->rect.h }, CUI_CORNERS(0), p->color);
    }
}

/* Deep trees keep room for the name (keep points of it): the indentation
 * stops growing once the column is full. */
static float cui__tt_indent(const cui__tt_build *b, int depth, float keep) {
    float indent = depth * CUI__TT_STEP;
    if (b->name_w > 0) {
        float room = b->name_w - 2 * CUI__TT_PAD - keep;
        if (indent > room) indent = CUI_MAX(0.0f, floorf(room / CUI__TT_STEP) * CUI__TT_STEP);
    }
    return indent;
}

static void cui__tt_add_guides(cui_ctx *ui, const cui__tt_build *b, int cell, float indent) {
    if (!b->o.guides || indent <= 0 || cell < 0) return;
    cui__tt_guides *g = cui__frame_alloc(ui, sizeof *g);
    g->levels = (int)(indent / CUI__TT_STEP + 0.5f);
    g->x0 = CUI__TT_PAD + CUI__TT_CHEV * 0.5f;
    g->step = CUI__TT_STEP;
    g->color = CUI_C(ui, BORDER_SUBTLE);
    ui->nodes[cell].paint = cui__paint_tt_guides;
    ui->nodes[cell].paint_data = g;
}

static void cui__tt_loading_cell(cui_ctx *ui, cui__tt_build *b, int depth) {
    cui_data_grid_cell(ui);
    int cell = cui__current(ui);
    if (cell >= 0) ui->nodes[cell].gap = 0;
    float indent = cui__tt_indent(b, depth, 120);
    cui__tt_add_guides(ui, b, cell, indent);
    if (indent > 0) cui__leaf(ui, 0, CUI_PX(indent), CUI_PX(1));
    cui__leaf(ui, 0, CUI_PX(CUI__TT_CHEV + 6), CUI_PX(1));
    cui_spinner(ui, 14);
    cui__leaf(ui, 0, CUI_PX(8), CUI_PX(1));
    int t = cui__text_node(ui, "Loading", -1, CUI_FONT_BODY, cui__fsz(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.3f;
}

int cui_tree_table_row(cui_ctx *ui) {
    cui__tt_build *b = cui__tt_current(ui);
    if (!b) return -1;
    while (b->next < b->last) {
        int i = b->next++;
        bool clicked = cui_data_grid_row(ui, i);
        cui__tt_row r = b->rows[i];
        if (r.node < 0) { b->cur = -1; cui__tt_loading_cell(ui, b, r.depth); continue; }
        b->cur = i;
        if (clicked) {
            b->st->cursor = r.node;
            int m = ui->mods & (CUI_MOD_SHIFT | CUI_MOD_PRIMARY);
            if (!m) b->plain_click = true;
            if (!m && ui->click_count >= 2) b->ev.activated = r.node;
        }
        return r.node;
    }
    b->cur = -1;
    return -1;
}

int cui_tree_table_depth(cui_ctx *ui) {
    cui__tt_build *b = cui__tt_current(ui);
    return b && b->cur >= 0 ? b->rows[b->cur].depth : 0;
}

typedef struct { cui_spring t; float hover; bool init; } cui__tt_chev;

void cui_tree_table_name(cui_ctx *ui, const char *label, const char *icon) {
    cui__tt_build *b = cui__tt_current(ui);
    if (!label) label = "";
    if (!b || b->cur < 0) { cui_data_grid_cell_text(ui, label); return; }
    int n = b->rows[b->cur].node, depth = b->rows[b->cur].depth;
    cui_tree_node *N = &b->nodes[n];
    cui_data_grid_cell(ui);
    int cell = cui__current(ui);
    if (cell < 0) return;
    ui->nodes[cell].gap = 0;
    if (ui->nodes[cell].w.kind == CUI_SIZE_FIXED && ui->nodes[cell].w.value > 0) ui->nodes[cell].id = b->id ^ 0x4A3E5u;   /* measures the column */
    float indent = cui__tt_indent(b, depth, CUI__TT_CHEV + 6 + (icon ? 24 : 0) + 56);
    cui__tt_add_guides(ui, b, cell, indent);
    if (indent > 0) cui__leaf(ui, 0, CUI_PX(indent), CUI_PX(1));
    bool open = (N->flags & CUI_TREE_OPEN) != 0;
    if (cui__tt_has_kids(b, n)) {
        cui_id cid = cui__hash(&n, sizeof n, b->id ^ 0xC4E7u);
        cui__tt_chev *cs = cui_state(ui, cid, sizeof *cs);
        cui_interact r = cui__interact(ui, cid, 0);
        if (r.clicked) {
            if (open) cui__tt_collapse(ui, b, n); else cui__tt_expand(ui, b, n);
            open = (N->flags & CUI_TREE_OPEN) != 0;
            ui->focus = b->id;   /* the keys carry on from the tree */
        }
        if (!cs->init) { cs->t.x = open ? 1.0f : 0.0f; cs->init = true; }
        cui__spring(ui, &cs->t, open ? 1.0f : 0.0f, CUI__SPRING_EXPAND);
        cui__tween(ui, &cs->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        int bx = cui__node_new(ui, cid);
        cui_node *bn = &ui->nodes[bx];
        bn->w = CUI_PX(CUI__TT_CHEV); bn->h = CUI_PX(CUI__TT_CHEV);
        bn->radius = CUI_CORNERS(CUI__R_SMALL);
        bn->bg = cui_alpha(CUI_C(ui, HOVER), cs->hover * 1.2f);
        bn->dir = CUI_STACK; bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
        bn->flags |= CUI_NF_KEEP_W;
        cui__mark_interactive(ui, bx, 0);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, bx);
        cui__chevron_node(ui, -CUI_PI * 0.5f + cs->t.x * CUI_PI * 0.5f, 14, cui_mix(CUI_C(ui, TEXT_MUTED), CUI_C(ui, FOREGROUND), cs->hover));
        cui__close(ui);
    } else {
        cui__leaf(ui, 0, CUI_PX(CUI__TT_CHEV), CUI_PX(1));
    }
    cui__leaf(ui, 0, CUI_PX(6), CUI_PX(1));
    if (icon) {
        cui__icon_node(ui, icon, 16, CUI_C(ui, TEXT_SECONDARY));
        cui__leaf(ui, 0, CUI_PX(8), CUI_PX(1));
    }
    cui_text_opts to;
    memset(&to, 0, sizeof to);
    to.nowrap = true; to.line_height = 1.3f;
    cui_text_ex(ui, label, &to);   /* cut short, the whole name shows on hover */
}

cui_tree_table_events cui_tree_table_end(cui_ctx *ui) {
    cui__tt_build *b = cui__tt_current(ui);
    if (!b) {
        cui_tree_table_events e;
        memset(&e, 0, sizeof e);
        e.activated = e.expanded = e.collapsed = -1;
        return e;
    }
    cui_data_grid_end(ui);
    cui__tt_state *st = b->st;
    /* write the grid's selection back to the nodes */
    bool changed = b->ev.selection_changed;
    bool *was = cui__frame_alloc(ui, (size_t)b->nrows + 1);
    int shown_sel = 0;
    for (int i = 0; i < b->nrows; i++) {
        int n = b->rows[i].node;
        was[i] = n >= 0 && (b->nodes[n].flags & CUI_TREE_SELECTED);
        shown_sel += was[i];
    }
    if (b->plain_click || b->clear_all) {
        int cleared = 0;
        for (int k = 0; k < b->count; k++)
            if (b->nodes[k].flags & CUI_TREE_SELECTED) { b->nodes[k].flags &= ~(uint32_t)CUI_TREE_SELECTED; cleared++; }
        if (cleared > shown_sel) changed = true;   /* nodes out of view were selected */
    }
    for (int i = 0; i < b->nrows; i++) {
        int n = b->rows[i].node;
        if (n < 0) continue;
        bool s = b->sel[i];
        if (s) b->nodes[n].flags |= CUI_TREE_SELECTED; else b->nodes[n].flags &= ~(uint32_t)CUI_TREE_SELECTED;
        if (s != was[i]) changed = true;
    }
    b->ev.selection_changed = changed;
    int c = cui__tt_node_of(b, b->vcursor);
    if (cui__tt_ok(b, c)) st->cursor = c;
    int a = cui__tt_node_of(b, b->vanchor);
    if (cui__tt_ok(b, a)) st->anchor = a;
    if (b->o.cursor) *b->o.cursor = st->cursor;
    if (b->o.sort) b->ev.sort_changed = b->o.sort->column != b->sort_before.column || b->o.sort->descending != b->sort_before.descending;
    cui__tt_slot *slot = cui__tt_slot_get(ui);
    slot->b = NULL;
    return b->ev;
}

/* ---- sorting: a stable merge sort of each sibling list ---- */

typedef struct { cui_tree_node *nodes; int count; int (*cmp)(void *user, int a, int b); void *user; } cui__tt_sorter;

static int cui__tt_merge(const cui__tt_sorter *s, int a, int b) {
    int head = -1, tail = -1;
    for (int guard = 0; (a >= 0 || b >= 0) && guard <= s->count; guard++) {   /* bounded: bad links cannot loop */
        int pick;
        if (b < 0 || (a >= 0 && s->cmp(s->user, b, a) >= 0)) { pick = a; a = s->nodes[a].next_sibling; }
        else { pick = b; b = s->nodes[b].next_sibling; }
        if (tail < 0) head = pick; else s->nodes[tail].next_sibling = pick;
        tail = pick;
    }
    if (tail >= 0) s->nodes[tail].next_sibling = -1;
    return head;
}

static int cui__tt_sort_list(const cui__tt_sorter *s, int count, int head) {
    int bins[32];
    for (int k = 0; k < 32; k++) bins[k] = -1;
    for (int guard = 0; head >= 0 && head < count && guard < count; guard++) {
        int n = head;
        head = s->nodes[n].next_sibling;
        if (head >= count) head = -1;
        s->nodes[n].next_sibling = -1;
        int carry = n, k = 0;
        for (; k < 31 && bins[k] >= 0; k++) { carry = cui__tt_merge(s, bins[k], carry); bins[k] = -1; }
        if (bins[k] >= 0) carry = cui__tt_merge(s, bins[k], carry);
        bins[k] = carry;
    }
    int out = -1;
    for (int k = 0; k < 32; k++) if (bins[k] >= 0) out = cui__tt_merge(s, bins[k], out);
    return out;
}

int cui_tree_sort(cui_tree_node *nodes, int count, int root, int (*cmp)(void *user, int a, int b), void *user) {
    if (!nodes || count <= 0 || !cmp) return root;
    cui__tt_sorter s = { nodes, count, cmp, user };
    for (int k = 0; k < count; k++)
        if (nodes[k].first_child >= 0 && nodes[k].first_child < count) nodes[k].first_child = cui__tt_sort_list(&s, count, nodes[k].first_child);
    return root >= 0 && root < count ? cui__tt_sort_list(&s, count, root) : root;
}

/* ------------------------------------------------------------------------ */
/* Path bar                                                                 */
/* ------------------------------------------------------------------------ */

#define CUI__PB_SEG_PAD 8.0f
#define CUI__PB_GAP 2.0f

typedef struct {
    bool editing, start_edit;
    int menu_seg;                 /* the separator whose menu shows, -1 none */
    cui_id menu_anchor, prev_focus;
    float hover;
    bool init;
    char buf[1024];
} cui__pb_state;

typedef struct { float hover; } cui__pb_item;

static cui_id cui__pb_item_id(cui_ctx *ui, const char *id_str, int k, const char *what) {
    cui_push_id(ui, id_str);
    if (k >= 0) cui_push_id_int(ui, k);
    cui_id id = cui_make_id(ui, what);
    if (k >= 0) cui_pop_id(ui);
    cui_pop_id(ui);
    return id;
}

/* A hover pill that holds a label or an icon. Returns its node (open). */
static int cui__pb_pill(cui_ctx *ui, cui_id id, float h, bool focusable, cui_interact *out) {
    cui_interact r = cui__interact(ui, id, focusable ? CUI_IF_FOCUSABLE : 0);
    cui__pb_item *it = cui_state(ui, id, sizeof *it);
    cui__tween(ui, &it->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int p = cui__node_new(ui, id);
    cui_node *pn = &ui->nodes[p];
    pn->dir = CUI_ROW; pn->align = CUI_ALIGN_CENTER; pn->justify = CUI_JUSTIFY_CENTER;
    pn->h = CUI_PX(h);
    pn->radius = CUI_CORNERS(CUI_MAX(CUI__R_SMALL, ui->theme.radius_control - 4));   /* concentric: 4 inside the bar */
    pn->bg = cui_alpha(CUI_C(ui, HOVER), it->hover * (r.pressed ? 1.6f : 1.0f));
    cui__mark_interactive(ui, p, focusable ? CUI_IF_FOCUSABLE : 0);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, p);
    *out = r;
    return p;
}

static void cui__pb_start(cui_ctx *ui, cui__pb_state *st, const char *const *segs, int count, const cui_path_bar_opts *o) {
    if (o->text) snprintf(st->buf, sizeof st->buf, "%s", o->text);
    else {
        size_t len = 0;
        st->buf[0] = 0;
        for (int k = 0; k < count; k++) {
            const char *s = segs[k] ? segs[k] : "";
            int n = snprintf(st->buf + len, sizeof st->buf - len, "%s%.*s", k ? "/" : "", cui__display_len(s), s);
            if (n < 0 || (size_t)n >= sizeof st->buf - len) break;
            len += (size_t)n;
        }
    }
    st->editing = true;
    st->start_edit = true;
    st->prev_focus = ui->focus;
}

cui_path_bar_result cui_path_bar_ex(cui_ctx *ui, const char *id_str, const char *const *segs, int count, const cui_path_bar_opts *o) {
    cui_path_bar_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!segs || count < 0) count = 0;
    cui_path_bar_result res;
    memset(&res, 0, sizeof res);
    res.clicked = -1;
    cui_id id = cui_make_id(ui, id_str);
    cui__pb_state *st = cui_state(ui, id ^ 0x9A7B0u, sizeof *st);
    if (!st->init) { st->menu_seg = -1; st->init = true; }
    float h = cui__control_height(ui, o->size);
    bool editable = !o->no_edit;
    if (!editable) st->editing = false;
    /* Ctrl+L types a path, unless a text field has the keyboard */
    if (editable && !st->editing && !ui->edit && !ui->edit_prev && cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_L)) {
        ui->kpressed[CUI_KEY_L] = 0;
        cui__pb_start(ui, st, segs, count, o);
    }
    if (st->editing) {
        cui_id fid = id ^ 0xED17u;
        bool first = st->start_edit;
        if (first) { ui->focus = fid; ui->tf_select_all = fid; ui->focus_visible = false; st->start_edit = false; }
        if (ui->focus == fid && cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
            ui->kpressed[CUI_KEY_ESCAPE] = 0;
            st->editing = false;
            ui->focus = st->prev_focus;
        } else {
            cui_input_opts io;
            memset(&io, 0, sizeof io);
            io.size = o->size;
            io.width = o->width.kind ? o->width : CUI_FILL;
            io.icon = o->icon;
            io.placeholder = o->placeholder ? o->placeholder : "Type a path";
            int wrap = cui__node_new(ui, id);   /* the bar keeps its rect while it is a field */
            ui->nodes[wrap].w = io.width;
            ui->nodes[wrap].h = CUI_PX(h);
            cui__open(ui, wrap);
            io.width = CUI_FILL;
            int f = cui__text_field(ui, fid, st->buf, sizeof st->buf, &io, false, 0, NULL);
            cui__close(ui);
            if (f & CUI_SUBMITTED) {
                res.submitted = cui__frame_strdup(ui, st->buf, -1);
                st->editing = false;
                ui->focus = st->prev_focus;
            } else if (!first && ui->focus != fid) {
                st->editing = false;   /* clicking away cancels */
            }
            res.editing = st->editing;
            ui->last_id = id;
            return res;
        }
    }
    /* how many segments fit: the first and the last stay, the middle ones
     * collapse into "..." from the start */
    float fs = cui__fsz(ui, ui->theme.text_body);
    float sep_w = o->menus ? 20.0f : 18.0f, more_w = 30.0f;
    float pad_l = o->icon ? 10.0f : 4.0f, pad_r = 4.0f;
    float avail = 1e9f;
    if (cui__has_prev_rect(ui, id)) avail = cui__prev_rect(ui, id).w - pad_l - pad_r - 2 - (o->icon ? 22.0f : 0.0f);
    float *w = cui__frame_alloc(ui, sizeof(float) * (size_t)(count + 1));
    for (int k = 0; k < count; k++) {
        const char *s = segs[k] ? segs[k] : "";
        w[k] = 2 * CUI__PB_SEG_PAD + cui__text_width(ui, k == count - 1 ? CUI_FONT_MEDIUM : CUI_FONT_BODY, fs, s, cui__display_len(s), false);
    }
    int hide_from = count, hide_to = count;   /* segments [hide_from, hide_to) are in the menu */
    {
        float total = 0;
        for (int k = 0; k < count; k++) total += w[k] + (k ? sep_w + 2 * CUI__PB_GAP : 0);
        if (count > 2 && total > avail) {
            hide_from = 1; hide_to = 1;
            float t = total + more_w + sep_w + 2 * CUI__PB_GAP;
            while (hide_to < count - 1 && t > avail) { t -= w[hide_to] + sep_w + 2 * CUI__PB_GAP; hide_to++; }
            if (t > avail) { hide_from = 0; }   /* even the first goes: "..." and the current one */
        } else if (count == 2 && total > avail) {
            hide_from = 0; hide_to = 1;
        }
    }
    bool hidden_any = hide_to > hide_from;
    cui_id more_id = cui__pb_item_id(ui, id_str, -1, "more");
    cui_id more_pid = id ^ 0x3011Eu, menu_pid = id ^ 0x5E9A0u;
    cui_interact br = cui__interact(ui, id, 0);
    cui__tween(ui, &st->hover, br.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int bar = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[bar];
    bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER; bn->gap = CUI__PB_GAP;
    bn->h = CUI_PX(h);
    bn->w = o->width.kind ? o->width : CUI_FILL;
    bn->min_w = 0;
    bn->pad[3] = pad_l; bn->pad[1] = pad_r;
    bn->radius = CUI_CORNERS(CUI__R_CONTROL(ui, h));
    bn->bg = CUI_C(ui, SURFACE);
    bn->border = 1; bn->border_color = cui__quiet_edge(ui, editable ? st->hover : 0);
    bn->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, bar, 0);
    if (br.hovered && editable) cui__set_cursor(ui, CUI_CURSOR_TEXT);
    bool start = editable && br.clicked;   /* the empty part of the bar */
    cui__open(ui, bar);
    if (o->icon) { cui__icon_node(ui, o->icon, 16, CUI_C(ui, TEXT_SECONDARY)); cui__leaf(ui, 0, CUI_PX(4), CUI_PX(1)); }
    float ph = h - 8;
    cui_popup *mp = cui__popup_get(ui, menu_pid, false);
    /* the items in view: segments, with "..." (-1) for the collapsed ones */
    int *items = cui__frame_alloc(ui, sizeof(int) * (size_t)(count + 1));
    int ni = 0;
    for (int k = 0; k < count; k++) {
        if (k >= hide_from && k < hide_to) { if (k == hide_from) items[ni++] = -1; continue; }
        items[ni++] = k;
    }
    bool menu_shown = false;
    for (int i = 0; i < ni; i++) {
        int k = items[i];
        if (i > 0) {
            /* the separator before this item; its menu lists the children of the segment before it */
            int parent = items[i - 1] >= 0 ? items[i - 1] : hide_to - 1;
            if (o->menus) {
                cui_id sep_id = cui__pb_item_id(ui, id_str, parent, "sep");
                bool showing = mp && mp->open && st->menu_seg == parent;
                if (showing) menu_shown = true;
                cui_interact r;
                int p = cui__pb_pill(ui, sep_id, ph, true, &r);
                ui->nodes[p].w = CUI_PX(sep_w);
                ui->nodes[p].flags |= CUI_NF_KEEP_W;
                cui__pb_item *it = cui_state(ui, sep_id, sizeof *it);
                if (showing) ui->nodes[p].bg = CUI_C(ui, ACTIVE);
                cui__icon_swap(ui, sep_id ^ 0x1C0u, showing ? CUI_ICON_CHEVRON_DOWN : CUI_ICON_CHEVRON_RIGHT, 14,
                               cui_mix(CUI_C(ui, TEXT_MUTED), CUI_C(ui, FOREGROUND), showing ? 1.0f : it->hover));
                cui__close(ui);
                cui__focus_ring(ui, p, r.focus_visible ? 1.0f : 0.0f);
                if (r.clicked) {
                    if (showing) cui__popup_set(ui, menu_pid, false);
                    else {
                        st->menu_seg = parent;
                        st->menu_anchor = sep_id;
                        cui__popup_set(ui, menu_pid, true);
                        mp = cui__popup_get(ui, menu_pid, false);
                        if (mp) mp->at_mouse = false;
                        menu_shown = true;
                    }
                }
            } else {
                int box = cui__node_new(ui, 0);
                ui->nodes[box].w = CUI_PX(sep_w); ui->nodes[box].dir = CUI_ROW; ui->nodes[box].justify = CUI_JUSTIFY_CENTER;
                ui->nodes[box].flags |= CUI_NF_KEEP_W;
                cui__open(ui, box);
                cui__icon_node(ui, CUI_ICON_CHEVRON_RIGHT, 14, CUI_C(ui, TEXT_MUTED));
                cui__close(ui);
            }
        }
        cui_interact r;
        if (k < 0) {
            int p = cui__pb_pill(ui, more_id, ph, true, &r);
            ui->nodes[p].w = CUI_PX(more_w);
            ui->nodes[p].flags |= CUI_NF_KEEP_W;
            cui__icon_node(ui, CUI_ICON_ELLIPSIS, 16, CUI_C(ui, TEXT_SECONDARY));
            cui__close(ui);
            cui__focus_ring(ui, p, r.focus_visible ? 1.0f : 0.0f);
            if (r.clicked) {
                cui_popup *pp = cui__popup_get(ui, more_pid, true);
                if (pp) { cui__popup_set(ui, more_pid, !pp->open); pp->at_mouse = false; }
            }
            continue;
        }
        const char *sg = segs[k] ? segs[k] : "";
        bool last = k == count - 1;
        cui_id sid = cui__pb_item_id(ui, id_str, k, "seg");
        int p = cui__pb_pill(ui, sid, ph, true, &r);
        ui->nodes[p].pad[1] = ui->nodes[p].pad[3] = CUI__PB_SEG_PAD;
        if (!last) ui->nodes[p].flags |= CUI_NF_KEEP_W;
        float hv = ((cui__pb_item *)cui_state(ui, sid, sizeof(cui__pb_item)))->hover;
        cui_text_opts to;
        memset(&to, 0, sizeof to);
        to.nowrap = true; to.line_height = 1.3f;
        to.font = last ? CUI_FONT_MEDIUM : CUI_FONT_BODY;
        to.color = last ? CUI_C(ui, FOREGROUND) : cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), hv);
        cui_text_ex(ui, cui__frame_strdup(ui, sg, cui__display_len(sg)), &to);
        cui__close(ui);
        cui__focus_ring(ui, p, r.focus_visible ? 1.0f : 0.0f);
        if (r.clicked) {
            if (!last) res.clicked = k;
            else if (editable) start = true;   /* the current place: type a path */
        }
    }
    cui__close(ui);   /* bar */
    /* a separator menu whose chevron is gone (collapsed, or the path changed) closes */
    if (mp && mp->open && !menu_shown) cui__popup_set(ui, menu_pid, false);
    if (cui__popup_begin(ui, more_pid, more_id, CUI_PLACE_BOTTOM_START, 0, 6, 0, true)) {
        for (int k = hide_from; k < hide_to && hidden_any; k++) {
            cui_push_id_int(ui, k);
            cui_menu_item_opts io;
            memset(&io, 0, sizeof io);
            io.icon = o->icon && k == 0 ? o->icon : NULL;
            if (cui_menu_item_ex(ui, segs[k] ? segs[k] : "", &io)) res.clicked = k;
            cui_pop_id(ui);
        }
        cui__popup_end(ui);
    }
    if (start) {
        cui__close_all_popups(ui);
        cui__pb_start(ui, st, segs, count, o);
        ui->animating = true;
    }
    ui->last_id = id;
    return res;
}

bool cui_path_bar_menu_begin(cui_ctx *ui, const char *id_str, int *segment) {
    cui_id id = cui_make_id(ui, id_str);
    cui__pb_state *st = cui_state(ui, id ^ 0x9A7B0u, sizeof *st);
    if (!st->init || st->menu_seg < 0 || st->editing) return false;
    if (!cui__popup_begin(ui, id ^ 0x5E9A0u, st->menu_anchor, CUI_PLACE_BOTTOM_START, 0, 6, 0, true)) return false;
    if (segment) *segment = st->menu_seg;
    return true;
}

void cui_path_bar_menu_end(cui_ctx *ui) { cui__popup_end(ui); }
