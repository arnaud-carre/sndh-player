/* Data grid, tree cells and the filter bar. */
#include "cui_internal.h"

#define cui__fs cui__fsz
#define CUI__GRID_COLS 32
#define CUI__GRID_HEAD 40.0f
#define CUI__GRID_CHECK 44.0f

typedef struct {
    float widths[CUI__GRID_COLS];
    bool init;
    int resizing;            /* column being resized, -1 none */
    float press_x, start_w;
    int cursor, anchor;
    int edit_row, edit_col;  /* cell being edited, -1 none */
    char backup[256];
    float hover_row_t;
} cui__grid_state;

struct cui_grid_build {
    cui_id id, sid;
    const cui_grid_column *cols;
    int ncols, nrows;
    cui_grid_opts o;
    cui__grid_state *st;
    float row_h, total_w, extra;
    int first, last;
    bool hidden[CUI__GRID_COLS];   /* columns hidden by priority in a narrow grid */
    int last_col;            /* the last shown column, which takes the spare width */
    bool checks;             /* the checkbox column is shown */
    int row;                 /* current row, -1 outside */
    int col;                 /* next cell index */
    bool in_row;
    bool clicked_row;
    bool row_double;         /* the current row was double clicked this frame */
    bool focused;
};


static cui_color cui__accent_or_fg_c(cui_ctx *ui) { return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT); }

typedef struct { float t, mixed; } cui__gcheck_paint;
static void cui__paint_grid_check(cui_ctx *ui, cui_node *n, void *data) {
    cui__gcheck_paint *p = data;
    cui_rect r = n->rect;
    cui_color on = CUI_C(ui, CONTROL_ON);
    float t = CUI_MAX(p->t, p->mixed);
    cui_draw_rect_border(ui, r, CUI_CORNERS(5), cui_mix(CUI_C(ui, SURFACE), on, t), cui_mix(CUI_C(ui, BORDER_STRONG), on, t), 1);
    if (p->mixed > 0.01f && p->t < 0.5f) {
        cui_draw_rect(ui, (cui_rect){ r.x + 4, r.y + r.h * 0.5f - 0.8f, r.w - 8, 1.6f }, CUI_CORNERS(0.8f), CUI_C(ui, CONTROL_GLYPH));
    } else if (p->t > 0.02f) {
        cui__draw_check(ui, cui__rect_inset(r, 3.5f), p->t, 1.6f, CUI_C(ui, CONTROL_GLYPH));
    }
}

static void cui__grid_checkbox(cui_ctx *ui, cui_id id, bool on, bool mixed) {
    float *t = cui_state(ui, id, sizeof(float) * 2);
    cui__tween(ui, &t[0], on ? 1.0f : 0.0f, CUI__T_STATE);
    cui__tween(ui, &t[1], mixed ? 1.0f : 0.0f, CUI__T_STATE);
    int c = cui__leaf(ui, id, CUI_PX(16), CUI_PX(16));
    cui__gcheck_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->t = t[0]; p->mixed = t[1];
    ui->nodes[c].paint = cui__paint_grid_check;
    ui->nodes[c].paint_data = p;
    ui->nodes[c].self_align = CUI_ALIGN_CENTER;
    cui__mark_interactive(ui, c, 0);
}

typedef struct { cui_color line; } cui__grid_row_paint;
static void cui__paint_grid_row(cui_ctx *ui, cui_node *n, void *data) {
    cui__grid_row_paint *p = data;
    cui_draw_rect(ui, (cui_rect){ n->rect.x, n->rect.y + n->rect.h - 1, n->rect.w, 1 }, CUI_CORNERS(0), p->line);
}

typedef struct { float hover; } cui__grid_head_paint;

bool cui_data_grid_begin_ex(cui_ctx *ui, const char *id_str, const cui_grid_column *cols, int ncols, int nrows,
                            int *first, int *last, const cui_grid_opts *o) {
    cui_grid_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (ncols > CUI__GRID_COLS) ncols = CUI__GRID_COLS;
    if (nrows < 0) nrows = 0;
    struct cui_grid_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    b->sid = b->id ^ 0x5C011u;
    b->cols = cols; b->ncols = ncols; b->nrows = nrows; b->o = *o;
    b->row_h = o->row_height > 0 ? o->row_height : 40;
    b->row = -1;
    cui__grid_state *st = cui_state(ui, b->id, sizeof *st);
    b->st = st;
    if (!st->init) {
        for (int k = 0; k < ncols; k++) st->widths[k] = cols[k].width > 0 ? cols[k].width : 150;
        st->resizing = -1; st->edit_row = st->edit_col = -1; st->cursor = -1; st->anchor = -1;
        st->init = true;
    }
    /* a cursor moved by the caller is scrolled into view below */
    bool cursor_set = o->cursor && *o->cursor >= 0 && *o->cursor < nrows && *o->cursor != st->cursor;
    if (o->cursor && *o->cursor >= 0) st->cursor = *o->cursor;
    if (o->anchor && *o->anchor >= 0) st->anchor = *o->anchor;
    cui_scroll_state *ss = cui_state(ui, b->sid, sizeof *ss);
    ss->bar_top_inset = CUI__GRID_HEAD;
    ss->always_show_bar = true;
    ss->bar_t = 1.0f;
    b->checks = o->selected && !o->hide_checkboxes;
    float total = b->checks ? CUI__GRID_CHECK : 0;
    if (o->proportional_columns) {
        float weight = 0;
        for (int k = 0; k < ncols; k++) weight += cols[k].width > 0 ? cols[k].width : 1;
        float available = (ss->view_w > 0 ? ss->view_w : ui->vw) - total;
        if (ss->view_w <= 0) ui->animating = true;
        for (int k = 0; k < ncols; k++) {
            float w = CUI_MAX(0, available) * (cols[k].width > 0 ? cols[k].width : 1) / CUI_MAX(1, weight);
            if (fabsf(st->widths[k] - w) > 0.01f) ui->animating = true;
            st->widths[k] = w;
        }
    }
    for (int k = 0; k < ncols; k++) total += st->widths[k];
    /* too narrow: columns with a priority hide, the highest number first, until the rest fit */
    for (int shown = ncols; ss->view_w > 0 && total > ss->view_w + 0.5f && shown > 1; shown--) {   /* one column always stays */
        int h = -1;
        for (int k = 0; k < ncols; k++)
            if (!b->hidden[k] && cols[k].priority > 0 && (h < 0 || cols[k].priority >= cols[h].priority)) h = k;
        if (h < 0) break;
        b->hidden[h] = true;
        total -= st->widths[h];
    }
    b->last_col = -1;
    for (int k = 0; k < ncols; k++) if (!b->hidden[k]) b->last_col = k;
    /* the last column takes any spare width, so rows and header reach the edge */
    b->extra = ss->view_w > total ? ss->view_w - total : 0;
    total += b->extra;
    b->total_w = total;
    float view_h = ss->view_h > 0 ? ss->view_h : 400;
    if (cursor_set) {
        /* Center caller-selected rows in the body, excluding the fixed header.
         * The scroll area clamps this target at the beginning/end of the list. */
        float body_h = CUI_MAX(0, view_h - CUI__GRID_HEAD);
        ss->ty = (st->cursor + 0.5f) * b->row_h - body_h * 0.5f;
        ui->animating = true;
    }
    /* keyboard: the grid is one tab stop; arrows move the cursor row */
    cui_interact gr = cui__interact(ui, b->id, CUI_IF_FOCUSABLE);
    b->focused = ui->focus == b->id;
    if (b->focused && nrows > 0 && st->edit_row < 0) {
        int c = st->cursor < 0 ? 0 : st->cursor;
        int page = (int)(view_h / b->row_h) - 1;
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) c++;
        if (cui_key_pressed(ui, CUI_KEY_UP)) c--;
        if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) c += page;
        if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) c -= page;
        if (cui_key_pressed(ui, CUI_KEY_HOME)) c = 0;
        if (cui_key_pressed(ui, CUI_KEY_END)) c = nrows - 1;
        c = CUI_CLAMP(c, 0, nrows - 1);
        if (c != st->cursor) {
            if ((ui->mods & CUI_MOD_SHIFT) && o->selected) {
                if (st->anchor < 0) st->anchor = st->cursor < 0 ? c : st->cursor;
                int lo = CUI_MIN(st->anchor, c), hi = CUI_MAX(st->anchor, c);
                for (int r = 0; r < nrows; r++) o->selected[r] = r >= lo && r <= hi;
            } else st->anchor = c;
            st->cursor = c;
            /* keep the cursor row in view (below the sticky header) */
            float top = CUI__GRID_HEAD + c * b->row_h, bottom = top + b->row_h;
            if (top - CUI__GRID_HEAD < ss->ty) ss->ty = top - CUI__GRID_HEAD;
            if (bottom > ss->ty + view_h) ss->ty = bottom - view_h;
            ui->animating = true;
        }
        if (o->selected && cui_key_pressed(ui, CUI_KEY_SPACE) && st->cursor >= 0) o->selected[st->cursor] = !o->selected[st->cursor];
        if (o->selected && (ui->mods & CUI_MOD_PRIMARY) && cui_key_pressed(ui, CUI_KEY_A)) for (int r = 0; r < nrows; r++) o->selected[r] = true;
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE) && o->selected) for (int r = 0; r < nrows; r++) o->selected[r] = false;
    }
    if (o->cursor) *o->cursor = st->cursor;
    if (o->anchor) *o->anchor = st->anchor;
    /* frame */
    int outer = cui__node_new(ui, b->id);
    cui_node *on = &ui->nodes[outer];
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->h = o->height.kind ? o->height : CUI_FILL;
    on->min_h = 120;
    on->radius = CUI_CORNERS(ui->theme.radius_control);
    on->border = 1; on->border_color = CUI_C(ui, BORDER);
    /* Clips round outward to physical pixels. Keep scrolling content inside
     * the border so fractional frame positions cannot expose a row above it. */
    on->pad[0] = on->pad[1] = on->pad[2] = on->pad[3] = on->border;
    on->bg = CUI_C(ui, SURFACE);
    on->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, outer, CUI_IF_FOCUSABLE);
    (void)gr;
    cui__open(ui, outer);
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL; sb.height = CUI_FILL;
    cui__scroll_begin_ex(ui, b->sid, &sb, true);
    /* Wheel input, thumb dragging and the scroll animation update the offset
     * above. Virtualize against the offset this frame will actually draw. */
    /* Snap both content and sticky header to the same pixel boundary.
     * A fractional transform otherwise exposes text above the header fill. */
    float scroll_y = roundf(ui->nodes[cui__current(ui)].scroll_y);
    ui->nodes[cui__current(ui)].scroll_y = scroll_y;
    int f = (int)floorf((scroll_y - CUI__GRID_HEAD) / b->row_h) - 2;
    if (f < 0) f = 0;
    int l = f + (int)ceilf(view_h / b->row_h) + 5;
    if (l > nrows) l = nrows;
    if (f > l) f = l;
    b->first = f; b->last = l;
    *first = f; *last = l;
    int content = cui__node_new(ui, 0);
    ui->nodes[content].w = CUI_PX(total);
    ui->nodes[content].gap = 0;
    cui__open(ui, content);
    /* sticky header: laid out at the top of the content, pushed down by the scroll offset. The
     * offset sits on a holder of no height: a node is culled by its rect before its own offset
     * applies, and one of no height never is, so the header stays drawn and clickable */
    int hold = cui__node_new(ui, 0);
    ui->nodes[hold].w = CUI_PX(total); ui->nodes[hold].h = CUI_PX(0);
    ui->nodes[hold].ty = scroll_y;
    ui->nodes[hold].flags |= CUI_NF_ON_TOP;
    cui__open(ui, hold);
    int head = cui__node_new(ui, b->id ^ 0x4EADu);
    cui_node *hn = &ui->nodes[head];
    hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER;
    hn->h = CUI_PX(CUI__GRID_HEAD); hn->w = CUI_PX(total);
    hn->bg = CUI_C(ui, SURFACE_MUTED);
    /* the header's fill follows the frame's top corners instead of poking past them */
    { float ir = CUI_MAX(0.0f, ui->theme.radius_control - 1); hn->radius = (cui_corners){ ss->x > 0.5f ? 0 : ir, ir, 0, 0 }; }
    cui__grid_row_paint *hp = cui__frame_alloc(ui, sizeof *hp);
    hp->line = CUI_C(ui, BORDER);
    hn->paint = cui__paint_grid_row;
    hn->paint_data = hp;
    hn->flags |= CUI_NF_PAINT_AFTER;
    cui__open(ui, head);
    cui_push_id(ui, id_str);
    if (b->checks) {
        int n_sel = 0;
        for (int r = 0; r < nrows; r++) n_sel += o->selected[r] ? 1 : 0;
        int cell = cui__node_new(ui, 0);
        ui->nodes[cell].w = CUI_PX(CUI__GRID_CHECK); ui->nodes[cell].h = CUI_FILL;
        ui->nodes[cell].dir = CUI_ROW; ui->nodes[cell].align = CUI_ALIGN_CENTER; ui->nodes[cell].justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, cell);
        cui_id all_id = b->id ^ 0xA11u;
        cui_interact ar = cui__interact(ui, all_id, 0);
        if (ar.clicked) { bool to = n_sel < nrows; for (int r = 0; r < nrows; r++) o->selected[r] = to; }
        cui__grid_checkbox(ui, all_id, nrows > 0 && n_sel == nrows, n_sel > 0 && n_sel < nrows);
        if (ar.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__close(ui);
    }
    for (int k = 0; k < ncols; k++) {
        if (b->hidden[k]) continue;
        const cui_grid_column *c = &cols[k];
        cui_id hid = cui__hash(&k, sizeof k, b->id ^ 0x4E1u);
        cui_interact hr = cui__interact(ui, hid, c->sortable ? CUI_IF_FOCUSABLE : 0);
        float hv_v = cui__hover_fade(ui, hid, (hr.hovered && c->sortable), CUI__T_HOVER), *hv = &hv_v;
        if (hr.clicked && c->sortable && o->sort) {
            if (o->sort->column == k) o->sort->descending = !o->sort->descending;
            else { o->sort->column = k; o->sort->descending = false; }
        }
        int cell = cui__node_new(ui, hid);
        cui_node *cn = &ui->nodes[cell];
        cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 4;
        cn->w = CUI_PX(st->widths[k] + (k == b->last_col ? b->extra : 0)); cn->h = CUI_FILL;
        cn->pad[1] = cn->pad[3] = 12;
        cn->justify = c->align == CUI_TEXT_RIGHT ? CUI_JUSTIFY_END : c->align == CUI_TEXT_CENTER ? CUI_JUSTIFY_CENTER : CUI_JUSTIFY_START;
        cn->bg = cui_alpha(CUI_C(ui, HOVER), *hv);
        cn->flags |= CUI_NF_CLIP;
        if (c->sortable) { cui__mark_interactive(ui, cell, CUI_IF_FOCUSABLE); if (hr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND); }
        cui__open(ui, cell);
        bool sorted = o->sort && o->sort->column == k;
        int t = cui__text_node(ui, c->title ? c->title : "", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm),
                               sorted ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t].line_height = 1.3f;
        if (c->sortable) {
            const char *icon = sorted ? (o->sort->descending ? CUI_ICON_ARROW_DOWN : CUI_ICON_ARROW_UP) : CUI_ICON_CHEVRONS_UP_DOWN;
            cui__icon_node(ui, icon, 12, sorted ? CUI_C(ui, FOREGROUND) : cui_alpha(CUI_C(ui, TEXT_MUTED), 0.4f + 0.6f * *hv));
        }
        /* resize handle on the right edge */
        if (!c->fixed) {
            cui_id rid = cui__hash(&k, sizeof k, b->id ^ 0x7E5u);
            cui_interact rr = cui__interact(ui, rid, CUI_IF_DRAG);
            if (rr.hovered && ui->mpressed[0]) { st->resizing = k; st->press_x = ui->mx; st->start_w = st->widths[k]; }
            bool active = st->resizing == k;
            if (active) {
                if (ui->mdown[0]) {
                    st->widths[k] = cui__clampf(st->start_w + ui->mx - st->press_x, 60, 900);
                    ui->active = rid;
                } else st->resizing = -1;
            }
            int h = cui__absolute(ui, rid, st->widths[k] - 6, 0, 12, CUI__GRID_HEAD);
            cui__mark_interactive(ui, h, CUI_IF_DRAG);
            if (rr.hovered || active) {
                cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
                int bar = cui__absolute(ui, 0, 5, 9, 2, CUI__GRID_HEAD - 18);
                ui->nodes[bar].bg = active ? cui__accent_or_fg_c(ui) : CUI_C(ui, BORDER_STRONG);
                ui->nodes[bar].radius = CUI_CORNERS(1);
                (void)bar;
            }
            (void)h;
        }
        cui__close(ui);
        cui__focus_ring(ui, cell, hr.focus_visible ? 1.0f : 0.0f);
    }
    cui_pop_id(ui);
    cui__close(ui);   /* header */
    cui__close(ui);   /* holder */
    cui__leaf(ui, 0, CUI_PX(total), CUI_PX(CUI__GRID_HEAD));   /* the header's place */
    if (f > 0) cui__leaf(ui, 0, CUI_PX(total), CUI_PX(f * b->row_h));
    if (b->focused) cui__focus_ring(ui, outer, ui->focus_visible ? 1.0f : 0.0f);
    ui->grid = b;
    return true;
}

static void cui__grid_close_row(cui_ctx *ui, struct cui_grid_build *b) {
    if (!b->in_row) return;
    if (b->col > 0) cui__close(ui);
    for (; b->col < b->ncols; b->col++)
        if (!b->hidden[b->col]) cui__leaf(ui, 0, CUI_PX(b->st->widths[b->col] + (b->col == b->last_col ? b->extra : 0)), CUI_FILL);
    cui_pop_id(ui);
    cui__close(ui);
    b->in_row = false;
}

bool cui_data_grid_row(cui_ctx *ui, int row) {
    struct cui_grid_build *b = ui->grid;
    if (!b) return false;
    cui__grid_close_row(ui, b);
    cui__grid_state *st = b->st;
    cui_id rid = cui__hash(&row, sizeof row, b->id ^ 0x20Bu);
    cui_interact r = cui__interact(ui, rid, 0);
    bool sel = b->o.selected && b->o.selected[row];
    bool clicked = false;
    b->row_double = r.double_clicked || (r.clicked && ui->click_count >= 2);
    if (r.clicked) {
        clicked = true;
        if (st->edit_row < 0) ui->focus = b->id;   /* a cell editor that just opened keeps the focus */
        if (b->o.selected) {
            if (ui->mods & CUI_MOD_SHIFT) {
                int a = st->anchor < 0 ? row : st->anchor;
                int lo = CUI_MIN(a, row), hi = CUI_MAX(a, row);
                for (int k = 0; k < b->nrows; k++) b->o.selected[k] = k >= lo && k <= hi;
            } else if (ui->mods & CUI_MOD_PRIMARY) {
                b->o.selected[row] = !b->o.selected[row];
                st->anchor = row;
            } else {
                for (int k = 0; k < b->nrows; k++) b->o.selected[k] = k == row;
                st->anchor = row;
            }
            sel = b->o.selected[row];
        }
        st->cursor = row;
        if (b->o.cursor) *b->o.cursor = row;
        if (b->o.anchor) *b->o.anchor = st->anchor;
    }
    float hv_v = cui__hover_fade(ui, rid, r.hovered, CUI__T_HIGHLIGHT), *hv = &hv_v;
    int n = cui__node_new(ui, rid);
    cui_node *rn = &ui->nodes[n];
    rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER;
    rn->h = CUI_PX(b->row_h); rn->w = CUI_PX(b->total_w);
    cui_color bg = cui__rgba(0, 0, 0, 0);
    if (b->o.striped && (row & 1)) bg = cui_alpha(CUI_C(ui, FOREGROUND), 0.02f);
    bg = cui_mix(bg, CUI_C(ui, HOVER), *hv);
    if (sel) bg = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, ACTIVE) : cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, ACCENT), ui->mode == CUI_THEME_DARK ? 0.16f : 0.09f);
    rn->bg = bg;
    cui__grid_row_paint *rp = cui__frame_alloc(ui, sizeof *rp);
    rp->line = row == b->nrows - 1 ? cui__rgba(0, 0, 0, 0) : CUI_C(ui, BORDER_SUBTLE);   /* the grid's own edge closes the last row */
    rn->paint = cui__paint_grid_row;
    rn->paint_data = rp;
    rn->flags |= CUI_NF_PAINT_AFTER;
    cui__mark_interactive(ui, n, 0);
    cui__open(ui, n);
    if (b->focused && st->cursor == row && ui->focus_visible) {
        /* keyboard cursor: an accent bar on the left edge */
        int bar = cui__absolute(ui, 0, 0, 0, 2, b->row_h);
        ui->nodes[bar].bg = cui__accent_or_fg_c(ui);
    }
    cui_push_id_int(ui, row);
    b->row = row; b->col = 0; b->in_row = true;
    if (b->checks) {
        int cell = cui__node_new(ui, 0);
        ui->nodes[cell].w = CUI_PX(CUI__GRID_CHECK); ui->nodes[cell].h = CUI_FILL;
        ui->nodes[cell].dir = CUI_ROW; ui->nodes[cell].align = CUI_ALIGN_CENTER; ui->nodes[cell].justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, cell);
        cui_id cid = rid ^ 0xC4Eu;
        cui_interact cr = cui__interact(ui, cid, 0);
        if (cr.clicked) { b->o.selected[row] = !b->o.selected[row]; st->anchor = row; }
        cui__grid_checkbox(ui, cid, b->o.selected[row], false);
        if (cr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__close(ui);
    }
    return clicked;
}

void cui_data_grid_cell(cui_ctx *ui) {
    struct cui_grid_build *b = ui->grid;
    if (!b || !b->in_row) return;
    if (b->col > 0) cui__close(ui);
    if (b->col >= b->ncols) { b->col++; int c = cui__node_new(ui, 0); cui__open(ui, c); return; }
    if (b->hidden[b->col]) {
        /* a hidden column still takes its content, in an empty clipping box */
        int c = cui__node_new(ui, 0);
        ui->nodes[c].w = CUI_PX(0); ui->nodes[c].h = CUI_PX(0);
        ui->nodes[c].flags |= CUI_NF_CLIP;
        b->col++;
        cui__open(ui, c);
        return;
    }
    const cui_grid_column *c = &b->cols[b->col];
    int cell = cui__node_new(ui, 0);
    cui_node *cn = &ui->nodes[cell];
    cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 8;
    cn->w = CUI_PX(b->st->widths[b->col] + (b->col == b->last_col ? b->extra : 0)); cn->h = CUI_FILL;
    cn->pad[1] = cn->pad[3] = 12;
    cn->justify = c->align == CUI_TEXT_RIGHT ? CUI_JUSTIFY_END : c->align == CUI_TEXT_CENTER ? CUI_JUSTIFY_CENTER : CUI_JUSTIFY_START;
    cn->flags |= CUI_NF_CLIP;
    b->col++;
    cui__open(ui, cell);
}

void cui_data_grid_cell_text(cui_ctx *ui, const char *text) {
    cui_data_grid_cell(ui);
    struct cui_grid_build *b = ui->grid;
    bool right = b && b->col - 1 < b->ncols && b->cols[b->col - 1].align == CUI_TEXT_RIGHT;
    /* single line text: cut short, it shows the whole value on hover */
    cui_text_opts to;
    memset(&to, 0, sizeof to);
    to.nowrap = true; to.tabular = right; to.line_height = 1.3f;
    cui_text_ex(ui, text ? text : "", &to);
}

bool cui_data_grid_cell_edit(cui_ctx *ui, char *buf, size_t cap) {
    struct cui_grid_build *b = ui->grid;
    if (!b || !b->in_row) return false;
    cui_data_grid_cell(ui);
    return cui_data_grid_edit(ui, buf, cap);
}

bool cui_data_grid_edit(cui_ctx *ui, char *buf, size_t cap) {
    struct cui_grid_build *b = ui->grid;
    if (!b || !b->in_row || b->col <= 0) return false;
    cui__grid_state *st = b->st;
    int col = b->col - 1;
    uint32_t key[3] = { (uint32_t)b->row, (uint32_t)col, 0xED17u };
    cui_id cid = cui__hash(key, sizeof key, b->id);
    cui_id fid = cid ^ 0xF1Du;
    bool editing = st->edit_row == b->row && st->edit_col == col;
    bool committed = false;
    if (!editing) {
        /* the row takes the clicks; a double click inside this cell starts editing */
        cui_rect cr = cui__prev_rect(ui, cid);
        bool over = cui__rect_contains(cr, ui->mx, ui->my);
        bool start = b->row_double && over;
        /* Enter on the cursor row edits its first editable cell */
        if (!start && b->focused && st->cursor == b->row && st->edit_row < 0 && cui_key_pressed(ui, CUI_KEY_ENTER)) {
            start = true;
            ui->kpressed[CUI_KEY_ENTER] = 0;
        }
        int t = cui__text_node(ui, buf, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = 1.3f;
        ui->nodes[t].id = cid;
        ui->nodes[t].w = CUI_FILL;
        if (over && ui->hot) cui__set_cursor(ui, CUI_CURSOR_TEXT);
        if (start) {
            st->edit_row = b->row; st->edit_col = col;
            snprintf(st->backup, sizeof st->backup, "%s", buf);
            ui->focus = fid;
            ui->tf_select_all = fid;
        }
        return false;
    }
    bool cancel = ui->focus == fid && cui_key_pressed(ui, CUI_KEY_ESCAPE);
    if (cancel) {
        snprintf(buf, cap, "%s", st->backup);
        ui->kpressed[CUI_KEY_ESCAPE] = 0;
        st->edit_row = st->edit_col = -1;
        ui->focus = b->id;
        int t = cui__text_node(ui, buf, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = 1.3f;
        return false;
    }
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.size = CUI_SIZE_SM;
    int f = cui__text_field(ui, fid, buf, cap, &io, false, 0, NULL);
    if ((f & CUI_SUBMITTED) || ui->focus != fid) {
        committed = strcmp(buf, st->backup) != 0 || (f & CUI_SUBMITTED);
        st->edit_row = st->edit_col = -1;
        if (f & CUI_SUBMITTED) ui->focus = b->id;
    }
    return committed;
}

void cui_data_grid_end(cui_ctx *ui) {
    struct cui_grid_build *b = ui->grid;
    if (!b) return;
    cui__grid_close_row(ui, b);
    if (b->last < b->nrows) cui__leaf(ui, 0, CUI_PX(b->total_w), CUI_PX((b->nrows - b->last) * b->row_h));
    if (b->nrows == 0) {
        int e = cui__node_new(ui, 0);
        ui->nodes[e].w = CUI_FILL; ui->nodes[e].pad[0] = ui->nodes[e].pad[2] = 36;
        ui->nodes[e].align = CUI_ALIGN_CENTER;
        cui__open(ui, e);
        cui__text_node(ui, "No rows", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
        cui__close(ui);
    }
    cui__close(ui);          /* content */
    cui__scroll_end_ex(ui);
    cui__close(ui);          /* frame */
    ui->last_id = b->id;
    ui->grid = NULL;
}

/* ------------------------------------------------------------------------ */
/* Tree cell                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring t; bool init; float hover; } cui__tcell_state;

bool cui_tree_cell(cui_ctx *ui, const char *label, int depth, bool has_children, bool *open, const char *icon) {
    cui_id id = cui_make_id(ui, label) ^ 0x7CE1u;
    cui__tcell_state *st = cui_state(ui, id, sizeof *st);
    bool is_open = open && *open;
    if (!st->init) { st->t.x = is_open ? 1.0f : 0.0f; st->init = true; }
    cui__spring(ui, &st->t, is_open ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);
    bool toggled = false;
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].align = CUI_ALIGN_CENTER; ui->nodes[row].gap = 6;
    ui->nodes[row].pad[3] = 18.0f * (float)CUI_MAX(0, depth);
    ui->nodes[row].w = CUI_FILL;
    cui__open(ui, row);
    if (has_children) {
        cui_interact r = cui__interact(ui, id, 0);
        cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        if (r.clicked && open) { *open = !*open; toggled = true; }
        int b = cui__node_new(ui, id);
        cui_node *bn = &ui->nodes[b];
        bn->w = CUI_PX(20); bn->h = CUI_PX(20); bn->radius = CUI_CORNERS(6);
        bn->bg = cui_alpha(CUI_C(ui, HOVER), st->hover * 1.2f);
        bn->dir = CUI_STACK; bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
        cui__mark_interactive(ui, b, 0);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, b);
        cui__chevron_node(ui, -CUI_PI * 0.5f + st->t.x * CUI_PI * 0.5f, 14, CUI_C(ui, TEXT_SECONDARY));
        cui__close(ui);
    } else {
        cui_gap(ui, 20);
    }
    if (icon) cui__icon_node(ui, icon, 16, CUI_C(ui, TEXT_SECONDARY));
    int t = cui__text_node(ui, label, cui__display_len(label), has_children ? CUI_FONT_MEDIUM : CUI_FONT_BODY,
                           cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    cui__close(ui);
    return toggled;
}

/* ------------------------------------------------------------------------ */
/* Filter bar                                                               */
/* ------------------------------------------------------------------------ */

static int cui__popcount(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

typedef struct { uint32_t active; } cui__fb_state;   /* fields shown as chips even with nothing picked */

bool cui_filter_bar(cui_ctx *ui, const char *id_str, const cui_filter_field *fields, int count, uint32_t *selected,
                    char *search, size_t search_cap) {
    if (count > 32) count = 32;
    cui_id id = cui_make_id(ui, id_str);
    cui__fb_state *st = cui_state(ui, id, sizeof *st);
    bool changed = false;
    cui_push_id(ui, id_str);
    int row = cui__node_new(ui, id);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 8; ui->nodes[row].align = CUI_ALIGN_CENTER;
    ui->nodes[row].w = CUI_FILL;
    ui->nodes[row].flags |= CUI_NF_ROW_WRAP;   /* filter chips continue on a new line */
    cui__open(ui, row);
    if (search) {
        cui_input_opts io;
        memset(&io, 0, sizeof io);
        io.placeholder = "Search";
        io.size = CUI_SIZE_SM;
        io.width = CUI_PX(200);
        io.clearable = true;
        if (cui__text_field(ui, cui_make_id(ui, "#search"), search, search_cap, &io, false, 0, CUI_ICON_SEARCH) & CUI_CHANGED) changed = true;
    }
    for (int k = 0; k < count; k++) {
        if (!selected[k] && !(st->active & (1u << k))) continue;
        const cui_filter_field *f = &fields[k];
        cui_push_id_int(ui, k);
        cui_id cid = cui_make_id(ui, "chip");
        cui_id pid = cid ^ 0x3E40u;
        cui_popup *p = cui__popup_get(ui, pid, true);
        cui_interact r = cui__interact(ui, cid, CUI_IF_FOCUSABLE);
        if (r.clicked && p) { cui__popup_set(ui, pid, !p->open); p->at_mouse = false; }
        float hv_v = cui__hover_fade(ui, cid, (r.hovered || (p && p->open)), CUI__T_HOVER), *hv = &hv_v;
        int chip = cui__node_new(ui, cid);
        cui_node *cn = &ui->nodes[chip];
        cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 6;
        cn->h = CUI_PX(30); cn->pad[3] = 10; cn->pad[1] = 4;
        cn->radius = CUI_CORNERS(CUI__R_CONTROL(ui, 30));
        cn->bg = cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.04f + 0.04f * *hv);
        cn->border = 1; cn->border_color = CUI_C(ui, BORDER_STRONG);
        cn->self_align = CUI__ALIGN_NOSTRETCH;
        cui__mark_interactive(ui, chip, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, chip);
        if (f->icon) cui__icon_node(ui, f->icon, 14, CUI_C(ui, TEXT_SECONDARY));
        int nt = cui__text_node(ui, f->name, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[nt].line_height = 1.2f;
        int n = cui__popcount(selected[k]);
        const char *what = "any";
        if (n == 1) { for (int o = 0; o < f->option_count && o < 32; o++) if (selected[k] & (1u << o)) what = f->options[o]; }
        else if (n > 1) what = cui__frame_printf(ui, "%d selected", n);
        int is = cui__text_node(ui, "is", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[is].line_height = 1.2f;
        int vt = cui__text_node(ui, what, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), n ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[vt].line_height = 1.2f;
        cui_button_opts xo;
        memset(&xo, 0, sizeof xo);
        xo.size = CUI_SIZE_SM; xo.tooltip = "Remove filter";
        if (cui_icon_button_ex(ui, CUI_ICON_X, &xo)) { selected[k] = 0; st->active &= ~(1u << k); changed = true; }
        ui->nodes[ui->last_node].w = CUI_PX(22); ui->nodes[ui->last_node].h = CUI_PX(22);
        cui__close(ui);
        cui__focus_ring(ui, chip, r.focus_visible ? 1.0f : 0.0f);
        if (cui__popup_begin(ui, pid, cid, CUI_PLACE_BOTTOM_START, 220, 6, 0, true)) {
            cui_menu_label(ui, f->name);
            for (int o = 0; o < f->option_count && o < 32; o++) {
                cui_push_id_int(ui, o);
                /* options toggle without closing: rebuild a check row */
                cui_id oid = cui_make_id(ui, f->options[o]);
                cui_interact orr = cui__interact(ui, oid, 0);
                bool on = (selected[k] & (1u << o)) != 0;
                if (orr.clicked) { selected[k] ^= 1u << o; changed = true; on = !on; }
                float oh_v = cui__hover_fade(ui, oid, orr.hovered, CUI__T_HIGHLIGHT), *oh = &oh_v;
                int it = cui__node_new(ui, oid);
                cui_node *in = &ui->nodes[it];
                in->dir = CUI_ROW; in->align = CUI_ALIGN_CENTER; in->gap = 10;
                in->h = CUI_PX(32); in->pad[1] = in->pad[3] = 10; in->w = CUI_FILL; in->self_align = CUI_ALIGN_STRETCH;
                in->radius = CUI_CORNERS(CUI_MAX(6.0f, ui->theme.radius_panel - 6.0f));
                in->bg = cui_alpha(CUI_C(ui, ACTIVE), *oh);
                cui__mark_interactive(ui, it, 0);
                if (orr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
                cui__open(ui, it);
                cui__grid_checkbox(ui, oid ^ 0xCBu, on, false);
                int ot = cui__text_node(ui, f->options[o], -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
                ui->nodes[ot].line_height = 1.3f;
                cui__close(ui);
                cui_pop_id(ui);
            }
            cui__popup_end(ui);
        }
        cui_pop_id(ui);
    }
    /* add a filter */
    cui_menu_opts mo;
    memset(&mo, 0, sizeof mo);
    mo.icon = CUI_ICON_FILTER; mo.variant = CUI_BUTTON_GHOST;
    int avail = 0;
    for (int k = 0; k < count; k++) if (!selected[k] && !(st->active & (1u << k))) avail++;
    if (avail > 0 && cui_menu_begin(ui, "Filter##add", &mo)) {
        for (int k = 0; k < count; k++) {
            if (selected[k] || (st->active & (1u << k))) continue;
            cui_menu_item_opts io;
            memset(&io, 0, sizeof io);
            io.icon = fields[k].icon;
            cui_push_id_int(ui, k);
            if (cui_menu_item_ex(ui, fields[k].name, &io)) {
                st->active |= 1u << k;
                /* open the new chip's value list right away */
                cui_push_id_int(ui, k);
                cui_pop_id(ui);
            }
            cui_pop_id(ui);
        }
        cui_menu_end(ui);
    }
    if (avail > 0) ui->nodes[ui->last_node].h = CUI_PX(30);
    bool any = false;
    for (int k = 0; k < count; k++) if (selected[k]) any = true;
    if (any || (search && search[0])) {
        cui_button_opts co;
        memset(&co, 0, sizeof co);
        co.variant = CUI_BUTTON_LINK; co.size = CUI_SIZE_SM;
        if (cui_button_ex(ui, "Clear all", &co)) {
            for (int k = 0; k < count; k++) selected[k] = 0;
            st->active = 0;
            if (search) search[0] = 0;
            changed = true;
        }
    }
    cui__close(ui);
    cui_pop_id(ui);
    ui->last_id = id;
    return changed;
}
