/* Layout tree, flex style layout and the paint pass.
 *
 * Components build a tree of nodes during the frame. At the end of the frame
 * the tree is measured and laid out in a few passes (widths, text wrap,
 * heights, positions), then painted into the draw list. Interaction uses the
 * rectangles recorded during the previous frame. */
#include "cui_internal.h"

/* ------------------------------------------------------------------------ */
/* Nodes                                                                    */
/* ------------------------------------------------------------------------ */

cui_node *cui__node(cui_ctx *ui, int index) { return &ui->nodes[index]; }

int cui__current(cui_ctx *ui) { return ui->parent_depth > 0 ? ui->parents[ui->parent_depth - 1] : -1; }

int cui__node_new(cui_ctx *ui, cui_id id) {
    CUI__GROW(ui, ui->nodes, ui->node_count, ui->node_cap, 1);
    int idx = ui->node_count++;
    cui_node *n = &ui->nodes[idx];
    memset(n, 0, sizeof *n);
    n->id = id;
    n->parent = cui__current(ui);
    n->first = n->last = n->next = -1;
    n->abs_first = n->abs_last = -1;
    n->abs_anchor = -1;
    n->opacity = 1;
    n->scale = 1;
    n->line_height = 1.45f;
    n->max_w = n->max_h = 1e9f;
    if (n->parent >= 0) {
        cui_node *p = &ui->nodes[n->parent];
        if (p->last >= 0) ui->nodes[p->last].next = idx;
        else p->first = idx;
        p->last = idx;
        p->child_count++;
    }
    ui->last_node = idx;
    return idx;
}

int cui__open(cui_ctx *ui, int node) {
    if (ui->parent_depth < CUI_PARENT_STACK) ui->parents[ui->parent_depth++] = node;
    return node;
}

void cui__close(cui_ctx *ui) {
    if (ui->parent_depth > 0) ui->parent_depth--;
}

static cui_id cui__auto_id(cui_ctx *ui) {
    int d = ui->id_depth;
    int c = ui->auto_counter[d < CUI_ID_STACK ? d : CUI_ID_STACK - 1]++;
    cui_id seed = d ? ui->id_stack[d - 1] : 0x51ED270Bu;
    int cur = cui__current(ui);
    uint32_t key[2] = { (uint32_t)c, cur >= 0 ? ui->nodes[cur].id : 0 };
    return cui__hash(key, sizeof key, seed);
}

void cui__apply_box(cui_ctx *ui, cui_node *n, const cui_box *b) {
    if (!b) return;
    n->dir = (uint8_t)b->dir;
    n->w = b->width;
    n->h = b->height;
    if (b->min_width > 0) n->min_w = b->min_width;
    if (b->max_width > 0) n->max_w = b->max_width;
    if (b->min_height > 0) n->min_h = b->min_height;
    if (b->max_height > 0) n->max_h = b->max_height;
    float px = b->pad_x > 0 ? b->pad_x : b->pad;
    float py = b->pad_y > 0 ? b->pad_y : b->pad;
    n->pad[0] = b->pad_top > 0 ? b->pad_top : py;
    n->pad[1] = b->pad_right > 0 ? b->pad_right : px;
    n->pad[2] = b->pad_bottom > 0 ? b->pad_bottom : py;
    n->pad[3] = b->pad_left > 0 ? b->pad_left : px;
    n->gap = b->gap;
    n->columns = b->columns;
    if (b->min_column_width > 0) n->col_min = b->min_column_width;
    if (b->wrap && b->dir == CUI_ROW) n->flags |= CUI_NF_ROW_WRAP;
    n->align = (uint8_t)b->align;
    n->justify = (uint8_t)b->justify;
    n->bg = cui_color_of(ui, b->bg);
    n->border = b->border;
    n->border_color = cui__color_set(b->border_color) ? cui_color_of(ui, b->border_color) : CUI_C(ui, BORDER);
    float r = b->radius;
    n->radius.tl = b->radius_tl > 0 ? b->radius_tl : r;
    n->radius.tr = b->radius_tr > 0 ? b->radius_tr : r;
    n->radius.br = b->radius_br > 0 ? b->radius_br : r;
    n->radius.bl = b->radius_bl > 0 ? b->radius_bl : r;
    n->elevation = b->elevation;
    n->elevation_alpha = 1;
    if (b->clip) n->flags |= CUI_NF_CLIP;
    if (b->opacity > 0) n->opacity = b->opacity;
}

int cui__leaf(cui_ctx *ui, cui_id id, cui_size w, cui_size h) {
    int i = cui__node_new(ui, id);
    cui_node *n = &ui->nodes[i];
    n->w = w; n->h = h;
    return i;
}

void cui__soft_width(cui_node *n, cui_size requested, float def) {
    if (requested.kind) { n->w = requested; return; }
    n->w = CUI_FIT;
    n->pref_hint = def;
    n->self_align = CUI__ALIGN_NOSTRETCH;
}

int cui__text_node(cui_ctx *ui, const char *text, int len, cui_font font, float size, cui_color color, bool wrap) {
    int i = cui__node_new(ui, 0);
    cui_node *n = &ui->nodes[i];
    if (len < 0) len = text ? (int)strlen(text) : 0;
    n->flags |= CUI_NF_TEXT | (wrap ? CUI_NF_WRAP : 0);
    n->text = cui__frame_strdup(ui, text ? text : "", len);
    n->text_len = len;
    n->font = font;
    n->font_size = size;
    n->text_color = cui_color_of(ui, color);
    return i;
}

int cui__float_begin(cui_ctx *ui, cui_id id, int z, cui_id anchor, int placement, float offset) {
    /* floating roots are detached from the parent stack position */
    int saved_depth = ui->parent_depth;
    ui->parent_depth = 0;
    int i = cui__node_new(ui, id);
    ui->parent_depth = saved_depth;
    cui_node *n = &ui->nodes[i];
    n->flags |= CUI_NF_FLOATING;
    n->z = z;
    n->anchor = anchor;
    n->placement = (uint8_t)placement;
    n->offset = offset;
    CUI__GROW(ui, ui->float_roots, ui->float_count, ui->float_cap, 1);
    n->root_index = ui->float_count;
    ui->float_roots[ui->float_count++] = i;
    cui__open(ui, i);
    if (ui->layer_depth < 32) ui->layer_stack[ui->layer_depth++] = ui->current_layer;
    ui->current_layer = id;
    return i;
}

int cui__absolute(cui_ctx *ui, cui_id id, float x, float y, float w, float h) {
    /* created detached, then linked into the parent's absolute chain */
    int parent = cui__current(ui);
    int saved = ui->parent_depth;
    ui->parent_depth = 0;
    int i = cui__node_new(ui, id);
    ui->parent_depth = saved;
    cui_node *n = &ui->nodes[i];
    n->flags |= CUI_NF_ABSOLUTE;
    n->parent = parent;
    n->fx = x; n->fy = y;
    n->w = CUI_PX(w); n->h = CUI_PX(h);
    if (parent >= 0) {
        cui_node *p = &ui->nodes[parent];
        if (p->abs_last >= 0) ui->nodes[p->abs_last].next = i;
        else p->abs_first = i;
        p->abs_last = i;
    }
    return i;
}

void cui__float_end(cui_ctx *ui) {
    cui__close(ui);
    if (ui->layer_depth > 0) ui->current_layer = ui->layer_stack[--ui->layer_depth];
}

/* ------------------------------------------------------------------------ */
/* Public containers                                                        */
/* ------------------------------------------------------------------------ */

cui_id cui_box_begin(cui_ctx *ui, const cui_box *b) {
    cui_id id = (b && b->id) ? cui_make_id(ui, b->id) : 0;
    bool scroll = b && (b->scroll_x || b->scroll_y);
    if (scroll) {
        if (!id) id = cui__auto_id(ui);
        cui__scroll_begin_ex(ui, id, b, b->scroll_x);
        return id;
    }
    int i = cui__node_new(ui, id);
    cui__apply_box(ui, &ui->nodes[i], b);
    cui__open(ui, i);
    return id;
}

void cui_box_end(cui_ctx *ui) {
    int cur = cui__current(ui);
    if (cur >= 0 && (ui->nodes[cur].flags & (CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y))) { cui__scroll_end_ex(ui); return; }
    cui__close(ui);
}

cui_id cui_row_begin(cui_ctx *ui, const cui_box *b) {
    cui_box o;
    if (b) o = *b; else memset(&o, 0, sizeof o);
    o.dir = CUI_ROW;
    return cui_box_begin(ui, &o);
}

cui_id cui_column_begin(cui_ctx *ui, const cui_box *b) {
    cui_box o;
    if (b) o = *b; else memset(&o, 0, sizeof o);
    o.dir = CUI_COLUMN;
    return cui_box_begin(ui, &o);
}

void cui_spacer(cui_ctx *ui) {
    int cur = cui__current(ui);
    bool row = cur >= 0 && ui->nodes[cur].dir == CUI_ROW;
    cui__leaf(ui, 0, row ? CUI_FILL : CUI_FIT, row ? CUI_FIT : CUI_FILL);
}

void cui_gap(cui_ctx *ui, float size) {
    int cur = cui__current(ui);
    bool row = cur >= 0 && ui->nodes[cur].dir == CUI_ROW;
    int i = cui__leaf(ui, 0, row ? CUI_PX(size) : CUI_PX(0), row ? CUI_PX(0) : CUI_PX(size));
    ui->nodes[i].self_align = CUI_ALIGN_START;
}

void cui_separator(cui_ctx *ui) {
    int cur = cui__current(ui);
    bool row = cur >= 0 && ui->nodes[cur].dir == CUI_ROW;
    int i = cui__leaf(ui, 0, row ? CUI_PX(1) : CUI_FILL, row ? CUI_FILL : CUI_PX(1));
    cui_node *n = &ui->nodes[i];
    n->bg = CUI_C(ui, BORDER);
    n->self_align = CUI_ALIGN_STRETCH;
}

/* ------------------------------------------------------------------------ */
/* Layout                                                                   */
/* ------------------------------------------------------------------------ */

static float cui__clamp_size(float v, float lo, float hi) {
    if (v > hi) v = hi;
    if (v < lo) v = lo;
    return v;
}

static int cui__eff_align(cui_node *parent, cui_node *child) {
    int pa = parent->align ? parent->align
           : (parent->dir == CUI_ROW ? CUI_ALIGN_CENTER : (parent->dir == CUI_STACK ? CUI_ALIGN_START : CUI_ALIGN_STRETCH));
    if (child->self_align == CUI__ALIGN_NOSTRETCH) return pa == CUI_ALIGN_STRETCH ? CUI_ALIGN_START : pa;
    if (child->self_align) return child->self_align;
    return pa;
}

static float cui__text_line_px(cui_node *n) { return n->font_size * n->line_height; }

/* Columns of a grid in the latest width pass. */
static int cui__grid_cols(cui_node *n) { return n->grid_cols > 0 ? n->grid_cols : (n->columns > 0 ? n->columns : 1); }

/* A wrapping row that actually broke into more than one line. */
static bool cui__multiline(cui_ctx *ui, cui_node *n) {
    if (!(n->flags & CUI_NF_ROW_WRAP) || n->first < 0) return false;
    for (int c = ui->nodes[n->first].next; c >= 0; c = ui->nodes[c].next)
        if (ui->nodes[c].flags & CUI_NF_LINE_START) return true;
    return false;
}

/* Children of a wrapped line: from `c` up to the next line start. */
static int cui__line_count(cui_ctx *ui, int c) {
    int k = 0;
    for (int i = c; i >= 0 && (i == c || !(ui->nodes[i].flags & CUI_NF_LINE_START)); i = ui->nodes[i].next) k++;
    return k;
}

/* Pass 1: content widths, bottom up. */
static void cui__measure_w(cui_ctx *ui, int i) {
    cui_node *n = &ui->nodes[i];
    for (int c = n->first; c >= 0; c = ui->nodes[c].next) cui__measure_w(ui, c);
    for (int c = n->abs_first; c >= 0; c = ui->nodes[c].next) cui__measure_w(ui, c);
    n = &ui->nodes[i];
    float pref = 0, minc = 0;
    if (n->flags & CUI_NF_TEXT) {
        pref = cui__text_width(ui, n->font, n->font_size, n->text, n->text_len, (n->flags & CUI_NF_TABULAR) != 0);
        if (n->flags & CUI_NF_WRAP) minc = cui__text_min_width(ui, n->font, n->font_size, n->text, n->text_len);
        else minc = CUI_MIN(pref, n->font_size * 2.5f);   /* single lines can shrink and show an ellipsis */
        pref = ceilf(pref * 4) / 4;
        minc = ceilf(minc * 4) / 4;
    } else {
        int count = 0;
        float sum = 0, sum_min = 0, mx = 0, mx_min = 0, mx_cmin = 0;
        for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
            cui_node *ch = &ui->nodes[c];
            if (ch->flags & CUI_NF_ABSOLUTE) continue;
            float cp, cm;
            if (ch->w.kind == CUI_SIZE_FIXED) { cp = cm = ch->w.value; }
            else if (ch->w.kind == CUI_SIZE_PERCENT) { cp = 0; cm = ch->minc_w; }
            else if (ch->w.kind == CUI_SIZE_GROW) { cp = ch->pref_w; cm = ch->minc_w; }
            else { cp = ch->pref_w; cm = ch->minc_w; }
            float cc = ch->w.kind == CUI_SIZE_FIXED ? cm : CUI_MIN(ch->cmin_w, ch->max_w);
            cp = cui__clamp_size(cp, ch->min_w, ch->max_w);
            cm = cui__clamp_size(cm, ch->min_w, ch->max_w);
            sum += cp; sum_min += cm;
            if (cp > mx) mx = cp;
            if (cm > mx_min) mx_min = cm;
            if (cc > mx_cmin) mx_cmin = cc;
            count++;
        }
        float gaps = count > 1 ? n->gap * (float)(count - 1) : 0;
        if (n->dir == CUI_ROW) {
            pref = sum + gaps;
            /* a wrapping row can go down to its widest child, one per line. A child's min_width
               decides where lines break; alone on a line it may shrink to its content, so cards
               that wrap on a tablet still fit a phone */
            minc = (n->flags & CUI_NF_ROW_WRAP) ? mx_cmin : sum_min + gaps;
        } else if (n->dir == CUI_GRID) {
            int cols = n->columns > 0 ? n->columns : 1;
            if (n->col_min > 0) {
                /* auto columns: as wide as all of them at their minimum, as narrow as one */
                cols = n->columns > 0 ? n->columns : (count > 0 ? count : 1);
                float cg = cols > 1 ? n->gap * (float)(cols - 1) : 0;
                pref = CUI_MAX(mx, n->col_min) * (float)cols + cg; minc = mx_min;
            } else {
                float cg = cols > 1 ? n->gap * (float)(cols - 1) : 0;
                pref = mx * (float)cols + cg; minc = mx_min * (float)cols + cg;
            }
        } else { pref = mx; minc = mx_min; }
        float padw = n->pad[1] + n->pad[3];
        pref += padw; minc += padw;
        if (n->flags & CUI_NF_SCROLL_X) minc = padw;
    }
    if (n->pref_hint > 0 && n->w.kind == CUI_SIZE_FIT) { pref = n->pref_hint; minc = CUI_MIN(minc, pref); }
    if (n->flags & CUI_NF_KEEP_W) minc = pref;
    if (n->flags & CUI_NF_ADAPTIVE) minc = CUI_MIN(minc, n->min_w);
    if (n->w.kind == CUI_SIZE_FIXED) { pref = minc = n->w.value; }
    n->cmin_w = CUI_MIN(minc, CUI_MIN(pref, n->max_w));
    if (n->flags & CUI_NF_ADAPTIVE) n->cmin_w = CUI_MAX(n->cmin_w, CUI_MIN(n->min_w, n->max_w));
    n->pref_w = cui__clamp_size(pref, n->min_w, n->max_w);
    n->minc_w = cui__clamp_size(CUI_MIN(minc, n->pref_w), n->min_w, n->max_w);
}

/* Distributes `inner` along a main axis (flex grow / shrink). */
static void cui__distribute(cui_ctx *ui, cui_node *n, int first, int count, float inner, bool horizontal, bool can_shrink) {
    if (count <= 0) return;
    float gaps = count > 1 ? n->gap * (float)(count - 1) : 0;
    float avail = inner - gaps;
    float used = 0, grow_total = 0;
#define CUI__SZ(ch) (horizontal ? (ch)->w : (ch)->h)
#define CUI__DST(ch) (horizontal ? &(ch)->rect.w : &(ch)->rect.h)
#define CUI__MIN(ch) (horizontal ? CUI_MAX((ch)->minc_w, (ch)->min_w) : CUI_MAX((ch)->minc_h, (ch)->min_h))
#define CUI__MAXV(ch) (horizontal ? (ch)->max_w : (ch)->max_h)
    for (int c = first, k_ = 0; c >= 0 && k_ < count; c = ui->nodes[c].next, k_++) {
        cui_node *ch = &ui->nodes[c];
        cui_size sz = CUI__SZ(ch);
        float v;
        switch (sz.kind) {
        case CUI_SIZE_FIXED: v = sz.value; break;
        case CUI_SIZE_PERCENT: v = inner * sz.value / 100.0f; break;
        case CUI_SIZE_GROW: v = 0; grow_total += sz.value > 0 ? sz.value : 1; break;
        default: v = horizontal ? ch->pref_w : ch->pref_h; break;
        }
        if (sz.kind != CUI_SIZE_GROW) v = cui__clamp_size(v, horizontal ? ch->min_w : ch->min_h, CUI__MAXV(ch));
        *CUI__DST(ch) = v;
        used += v;
    }
    float free_space = avail - used;
    if (grow_total > 0) {
        /* share free space by weight; children that hit min/max freeze */
        float remaining = free_space > 0 ? free_space : 0;
        uint8_t *frozen = cui__frame_alloc(ui, (size_t)count);
        for (int pass = 0; pass < 6; pass++) {
            float per = grow_total > 0 ? remaining / grow_total : 0;
            bool any = false;
            int k = 0;
            for (int c = first; c >= 0 && k < count; c = ui->nodes[c].next, k++) {
                cui_node *ch = &ui->nodes[c];
                cui_size sz = CUI__SZ(ch);
                if (sz.kind != CUI_SIZE_GROW || frozen[k]) continue;
                float wgt = sz.value > 0 ? sz.value : 1;
                float v = per * wgt, mn = CUI__MIN(ch), mx = CUI__MAXV(ch);
                if (v < mn || v > mx) {
                    v = v < mn ? mn : mx;
                    frozen[k] = 1; any = true;
                    remaining -= v; grow_total -= wgt;
                }
                *CUI__DST(ch) = v;
            }
            if (!any) break;
            if (remaining < 0) remaining = 0;
        }
        used = 0;
        for (int c = first, k_ = 0; c >= 0 && k_ < count; c = ui->nodes[c].next, k_++) used += *CUI__DST(&ui->nodes[c]);
        free_space = avail - used;
    }
    if (free_space < -0.01f && can_shrink) {
        /* shrink flexible children toward their minimum content size */
        for (int pass = 0; pass < 3 && free_space < -0.01f; pass++) {
            float shrinkable = 0;
            for (int c = first, k_ = 0; c >= 0 && k_ < count; c = ui->nodes[c].next, k_++) {
                cui_node *ch = &ui->nodes[c];
                cui_size sz = CUI__SZ(ch);
                if (sz.kind == CUI_SIZE_FIXED || sz.kind == CUI_SIZE_PERCENT) continue;
                float cur = *CUI__DST(ch), mn = CUI__MIN(ch);
                if (cur > mn) shrinkable += cur - mn;
            }
            if (shrinkable <= 0) break;
            float k = CUI_MIN(1.0f, -free_space / shrinkable);
            for (int c = first, k_ = 0; c >= 0 && k_ < count; c = ui->nodes[c].next, k_++) {
                cui_node *ch = &ui->nodes[c];
                cui_size sz = CUI__SZ(ch);
                if (sz.kind == CUI_SIZE_FIXED || sz.kind == CUI_SIZE_PERCENT) continue;
                float *dst = CUI__DST(ch), mn = CUI__MIN(ch);
                if (*dst > mn) { float d = (*dst - mn) * k; *dst -= d; free_space += d; }
            }
        }
    }
#undef CUI__SZ
#undef CUI__DST
#undef CUI__MIN
#undef CUI__MAXV
}

/* Pass 2: final widths, top down. */
static void cui__resolve_w(cui_ctx *ui, int i) {
    cui_node *n = &ui->nodes[i];
    float inner = n->rect.w - n->pad[1] - n->pad[3];
    if (inner < 0) inner = 0;
    if (n->dir == CUI_ROW && (n->flags & CUI_NF_ROW_WRAP)) {
        /* greedy lines: each child at its basis (content size, or the minimum
         * for growing children), then every line is distributed on its own */
        int c = n->first;
        while (c >= 0) {
            int start = c, count = 0;
            float used = 0;
            for (; c >= 0; c = ui->nodes[c].next) {
                cui_node *ch = &ui->nodes[c];
                ch->flags &= ~(uint32_t)CUI_NF_LINE_START;
                float b;
                switch (ch->w.kind) {
                case CUI_SIZE_FIXED: b = ch->w.value; break;
                case CUI_SIZE_PERCENT: b = inner * ch->w.value / 100.0f; break;
                case CUI_SIZE_GROW: b = CUI_MAX(ch->minc_w, ch->min_w); break;
                default: b = ch->pref_w; break;
                }
                b = cui__clamp_size(b, ch->min_w, ch->max_w);
                float need = count ? used + n->gap + b : b;
                if (count && need > inner + 0.01f) break;
                used = need; count++;
            }
            ui->nodes[start].flags |= CUI_NF_LINE_START;
            cui__distribute(ui, n, start, count, inner, true, true);
            /* alone on its line, a child narrower than its min_width allows still fits the line */
            if (count == 1) {
                cui_node *ch = &ui->nodes[start];
                if (ch->w.kind != CUI_SIZE_FIXED && ch->rect.w > inner + 0.01f) ch->rect.w = CUI_MAX(inner, ch->cmin_w);
            }
        }
    } else if (n->dir == CUI_ROW) {
        cui__distribute(ui, n, n->first, n->child_count, inner, true, !(n->flags & CUI_NF_SCROLL_X));
    } else if (n->dir == CUI_GRID) {
        int cols = n->columns > 0 ? n->columns : 1;
        if (n->col_min > 0) {
            int most = n->columns > 0 ? n->columns : (n->child_count > 0 ? n->child_count : 1);
            cols = (int)floorf((inner + n->gap) / (n->col_min + n->gap));
            if (cols > most) cols = most;
            if (cols < 1) cols = 1;
        }
        n->grid_cols = cols;
        float cell = (inner - n->gap * (float)(cols - 1)) / (float)cols;
        if (cell < 0) cell = 0;
        for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
            cui_node *ch = &ui->nodes[c];
            if (ch->w.kind == CUI_SIZE_FIXED) ch->rect.w = ch->w.value;
            else if (ch->pref_hint > 0) ch->rect.w = CUI_MAX(ch->minc_w, CUI_MIN(ch->pref_w, cell));
            else ch->rect.w = cell;
        }
    } else {
        bool sx = (n->flags & CUI_NF_SCROLL_X) != 0;
        for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
            cui_node *ch = &ui->nodes[c];
            int al = cui__eff_align(n, ch);
            float v;
            switch (ch->w.kind) {
            case CUI_SIZE_FIXED: v = ch->w.value; break;
            case CUI_SIZE_PERCENT: v = inner * ch->w.value / 100.0f; break;
            case CUI_SIZE_GROW: v = sx ? CUI_MAX(inner, ch->pref_w) : inner; break;
            default:
                if (al == CUI_ALIGN_STRETCH) v = sx ? CUI_MAX(inner, ch->pref_w) : inner;
                else v = sx ? ch->pref_w : CUI_MAX(ch->minc_w, CUI_MIN(ch->pref_w, inner));
                break;
            }
            ch->rect.w = cui__clamp_size(v, ch->min_w, ch->max_w);
        }
    }
    for (int c = n->first; c >= 0; c = ui->nodes[c].next) cui__resolve_w(ui, c);
    for (int c = n->abs_first; c >= 0; c = ui->nodes[c].next) {
        ui->nodes[c].rect.w = ui->nodes[c].w.kind == CUI_SIZE_FIXED ? ui->nodes[c].w.value : ui->nodes[c].pref_w;
        cui__resolve_w(ui, c);
    }
}

/* Pass 3: content heights (text wraps here), bottom up. */
static void cui__measure_h(cui_ctx *ui, int i) {
    cui_node *n = &ui->nodes[i];
    for (int c = n->first; c >= 0; c = ui->nodes[c].next) cui__measure_h(ui, c);
    for (int c = n->abs_first; c >= 0; c = ui->nodes[c].next) cui__measure_h(ui, c);
    n = &ui->nodes[i];
    float pref = 0, minc = 0;
    if (n->flags & CUI_NF_TEXT) {
        int lines;
        if (n->flags & CUI_NF_WRAP) {
            lines = cui__text_wrap(ui, n->font, n->font_size, n->text, n->text_len, n->rect.w + 0.5f, NULL, 0);
            if (lines < 1) lines = 1;
            n->lines = cui__frame_alloc(ui, sizeof(cui_text_layout_line) * (size_t)lines);
            cui__text_wrap(ui, n->font, n->font_size, n->text, n->text_len, n->rect.w + 0.5f, n->lines, lines);
        } else {
            lines = 1;
            for (int k = 0; k < n->text_len; k++) if (n->text[k] == '\n') lines++;
        }
        n->text_lines = lines;
        pref = minc = cui__text_line_px(n) * (float)lines;
    } else {
        int count = 0;
        float sum = 0, sum_min = 0, mx = 0, mx_min = 0;
        if (n->dir == CUI_GRID || cui__multiline(ui, n)) {
            bool grid = n->dir == CUI_GRID;
            int cols = cui__grid_cols(n);
            int k = 0;
            float row_max = 0;
            int rows = 0;
            for (int c = n->first; c >= 0; c = ui->nodes[c].next, k++) {
                cui_node *ch = &ui->nodes[c];
                float ph = ch->h.kind == CUI_SIZE_FIXED ? ch->h.value : ch->pref_h;
                if (ph > row_max) row_max = ph;
                bool end = grid ? (k % cols == cols - 1) : (ch->next >= 0 && (ui->nodes[ch->next].flags & CUI_NF_LINE_START));
                if (end || ch->next < 0) { sum += row_max; row_max = 0; rows++; }
            }
            pref = sum + (rows > 1 ? n->gap * (float)(rows - 1) : 0);
            minc = pref;
        } else {
            for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
                cui_node *ch = &ui->nodes[c];
                if (ch->flags & CUI_NF_ABSOLUTE) continue;
                float cp, cm;
                if (ch->h.kind == CUI_SIZE_FIXED) { cp = cm = ch->h.value; }
                else if (ch->h.kind == CUI_SIZE_PERCENT) { cp = 0; cm = ch->min_h; }
                else { cp = ch->pref_h; cm = ch->minc_h; }
                cp = cui__clamp_size(cp, ch->min_h, ch->max_h);
                cm = cui__clamp_size(cm, ch->min_h, ch->max_h);
                sum += cp; sum_min += cm;
                if (cp > mx) mx = cp;
                if (cm > mx_min) mx_min = cm;
                count++;
            }
            float gaps = count > 1 ? n->gap * (float)(count - 1) : 0;
            if (n->dir == CUI_COLUMN) { pref = sum + gaps; minc = sum_min + gaps; }
            else { pref = mx; minc = mx_min; }
        }
        float padh = n->pad[0] + n->pad[2];
        pref += padh; minc += padh;
        if (n->flags & CUI_NF_SCROLL_Y) minc = padh;
    }
    if (n->h.kind == CUI_SIZE_FIXED) pref = minc = n->h.value;
    n->pref_h = cui__clamp_size(pref, n->min_h, n->max_h);
    n->minc_h = cui__clamp_size(CUI_MIN(minc, n->pref_h), n->min_h, n->max_h);
}

/* Pass 4: final heights, top down. */
static void cui__resolve_h(cui_ctx *ui, int i) {
    cui_node *n = &ui->nodes[i];
    float inner = n->rect.h - n->pad[0] - n->pad[2];
    if (inner < 0) inner = 0;
    bool sy = (n->flags & CUI_NF_SCROLL_Y) != 0;
    if (n->dir == CUI_COLUMN) {
        if (sy) {
            /* content of a scroll area keeps its natural height; grow fills the view */
            for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
                cui_node *ch = &ui->nodes[c];
                float v = ch->h.kind == CUI_SIZE_FIXED ? ch->h.value : ch->pref_h;
                if (ch->h.kind == CUI_SIZE_GROW && n->child_count == 1) v = CUI_MAX(v, inner);
                ch->rect.h = cui__clamp_size(v, ch->min_h, ch->max_h);
            }
        } else {
            cui__distribute(ui, n, n->first, n->child_count, inner, false, true);
        }
    } else if (n->dir == CUI_GRID) {
        int cols = cui__grid_cols(n);
        int first_in_row = n->first;
        int k = 0;
        float row_max = 0;
        for (int c = n->first; c >= 0; c = ui->nodes[c].next, k++) {
            cui_node *ch = &ui->nodes[c];
            float ph = ch->h.kind == CUI_SIZE_FIXED ? ch->h.value : ch->pref_h;
            if (ph > row_max) row_max = ph;
            if (k % cols == cols - 1 || ch->next < 0) {
                for (int r = first_in_row; r >= 0; r = ui->nodes[r].next) {
                    cui_node *rc = &ui->nodes[r];
                    rc->rect.h = rc->h.kind == CUI_SIZE_FIXED ? rc->h.value : row_max;
                    if (r == c) break;
                }
                first_in_row = ch->next;
                row_max = 0;
            }
        }
    } else if (cui__multiline(ui, n)) {
        /* each wrapped line is as tall as its tallest child */
        for (int c = n->first; c >= 0;) {
            int count = cui__line_count(ui, c);
            float line_h = 0;
            int k = 0;
            for (int child = c; k < count; child = ui->nodes[child].next, k++) {
                cui_node *ch = &ui->nodes[child];
                line_h = CUI_MAX(line_h, ch->h.kind == CUI_SIZE_FIXED ? ch->h.value : ch->pref_h);
            }
            for (k = 0; k < count; c = ui->nodes[c].next, k++) {
                cui_node *ch = &ui->nodes[c];
                float v;
                switch (ch->h.kind) {
                case CUI_SIZE_FIXED: v = ch->h.value; break;
                case CUI_SIZE_PERCENT: v = line_h * ch->h.value / 100.0f; break;
                case CUI_SIZE_GROW: v = line_h; break;
                default: v = cui__eff_align(n, ch) == CUI_ALIGN_STRETCH ? line_h : ch->pref_h; break;
                }
                ch->rect.h = cui__clamp_size(v, ch->min_h, ch->max_h);
            }
        }
    } else {
        for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
            cui_node *ch = &ui->nodes[c];
            int al = n->dir == CUI_STACK ? (ch->self_align == CUI_ALIGN_STRETCH ? CUI_ALIGN_STRETCH : CUI_ALIGN_START)
                                         : cui__eff_align(n, ch);
            float v;
            switch (ch->h.kind) {
            case CUI_SIZE_FIXED: v = ch->h.value; break;
            case CUI_SIZE_PERCENT: v = inner * ch->h.value / 100.0f; break;
            case CUI_SIZE_GROW: v = sy ? CUI_MAX(inner, ch->pref_h) : inner; break;
            default: v = (al == CUI_ALIGN_STRETCH && !sy) ? inner : ch->pref_h; break;
            }
            ch->rect.h = cui__clamp_size(v, ch->min_h, ch->max_h);
        }
    }
    for (int c = n->first; c >= 0; c = ui->nodes[c].next) cui__resolve_h(ui, c);
    for (int c = n->abs_first; c >= 0; c = ui->nodes[c].next) {
        ui->nodes[c].rect.h = ui->nodes[c].h.kind == CUI_SIZE_FIXED ? ui->nodes[c].h.value : ui->nodes[c].pref_h;
        cui__resolve_h(ui, c);
    }
}

static void cui__record_rect(cui_ctx *ui, cui_node *n) {
    if (!n->id) return;
    cui_state_entry *e = cui__state_entry(ui, n->id);
    e->rect = n->rect;
    e->has_rect = true;
}

/* Pass 5: positions, top down. */
static void cui__place(cui_ctx *ui, int i) {
    cui_node *n = &ui->nodes[i];
    cui__record_rect(ui, n);
    float ox = n->rect.x + n->pad[3] - n->scroll_x;
    float oy = n->rect.y + n->pad[0] - n->scroll_y;
    float iw = n->rect.w - n->pad[1] - n->pad[3];
    float ih = n->rect.h - n->pad[0] - n->pad[2];
    int count = n->child_count;
    float max_r = 0, max_b = 0;
    if (count > 0) {
        if (cui__multiline(ui, n)) {
            /* wrapped lines: justify within each line, align within its height */
            float y = 0;
            for (int c = n->first; c >= 0;) {
                int lc = cui__line_count(ui, c);
                float total = 0, line_h = 0;
                int k = 0;
                for (int child = c; k < lc; child = ui->nodes[child].next, k++) {
                    total += ui->nodes[child].rect.w;
                    line_h = CUI_MAX(line_h, ui->nodes[child].rect.h);
                }
                float gap = n->gap, pos = 0;
                total += lc > 1 ? gap * (float)(lc - 1) : 0;
                float free_space = iw - total;
                if (free_space > 0) {
                    switch (n->justify) {
                    case CUI_JUSTIFY_CENTER: pos = free_space * 0.5f; break;
                    case CUI_JUSTIFY_END: pos = free_space; break;
                    case CUI_JUSTIFY_BETWEEN: if (lc > 1) gap += free_space / (float)(lc - 1); break;
                    case CUI_JUSTIFY_AROUND: pos = free_space / (float)lc * 0.5f; gap += free_space / (float)lc; break;
                    default: break;
                    }
                }
                for (k = 0; k < lc; c = ui->nodes[c].next, k++) {
                    cui_node *ch = &ui->nodes[c];
                    int al = cui__eff_align(n, ch);
                    float cpos = 0;
                    if (al == CUI_ALIGN_CENTER) cpos = (line_h - ch->rect.h) * 0.5f;
                    else if (al == CUI_ALIGN_END) cpos = line_h - ch->rect.h;
                    ch->rect.x = ox + pos; ch->rect.y = oy + y + cpos;
                    pos += ch->rect.w + gap;
                }
                y += line_h + n->gap;
            }
        } else if (n->dir == CUI_ROW || n->dir == CUI_COLUMN) {
            bool row = n->dir == CUI_ROW;
            float total = 0;
            for (int c = n->first; c >= 0; c = ui->nodes[c].next) total += row ? ui->nodes[c].rect.w : ui->nodes[c].rect.h;
            float gap = n->gap;
            total += count > 1 ? gap * (float)(count - 1) : 0;
            float free_space = (row ? iw : ih) - total;
            float pos = 0;
            if (free_space > 0) {
                switch (n->justify) {
                case CUI_JUSTIFY_CENTER: pos = free_space * 0.5f; break;
                case CUI_JUSTIFY_END: pos = free_space; break;
                case CUI_JUSTIFY_BETWEEN: if (count > 1) gap += free_space / (float)(count - 1); break;
                case CUI_JUSTIFY_AROUND: pos = free_space / (float)count * 0.5f; gap += free_space / (float)count; break;
                default: break;
                }
            }
            for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
                cui_node *ch = &ui->nodes[c];
                int al = cui__eff_align(n, ch);
                float cross_avail = row ? ih : iw;
                float csize = row ? ch->rect.h : ch->rect.w;
                float cpos = 0;
                if (al == CUI_ALIGN_CENTER) cpos = (cross_avail - csize) * 0.5f;
                else if (al == CUI_ALIGN_END) cpos = cross_avail - csize;
                if (row) { ch->rect.x = ox + pos; ch->rect.y = oy + cpos; pos += ch->rect.w + gap; }
                else { ch->rect.x = ox + cpos; ch->rect.y = oy + pos; pos += ch->rect.h + gap; }
            }
        } else if (n->dir == CUI_STACK) {
            for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
                cui_node *ch = &ui->nodes[c];
                int al = (ch->self_align && ch->self_align != CUI__ALIGN_NOSTRETCH) ? ch->self_align : (n->align ? n->align : CUI_ALIGN_START);
                float x = ox, y = oy;
                if (al == CUI_ALIGN_CENTER) x = ox + (iw - ch->rect.w) * 0.5f;
                else if (al == CUI_ALIGN_END) x = ox + iw - ch->rect.w;
                if (n->justify == CUI_JUSTIFY_CENTER) y = oy + (ih - ch->rect.h) * 0.5f;
                else if (n->justify == CUI_JUSTIFY_END) y = oy + ih - ch->rect.h;
                ch->rect.x = x; ch->rect.y = y;
            }
        } else if (n->dir == CUI_GRID) {
            int cols = cui__grid_cols(n);
            float cell = (iw - n->gap * (float)(cols - 1)) / (float)cols;
            int k = 0;
            float y = oy, row_h = 0;
            for (int c = n->first; c >= 0; c = ui->nodes[c].next, k++) {
                cui_node *ch = &ui->nodes[c];
                int col = k % cols;
                if (col == 0 && k > 0) { y += row_h + n->gap; row_h = 0; }
                ch->rect.x = ox + (cell + n->gap) * (float)col;
                ch->rect.y = y;
                if (ch->rect.h > row_h) row_h = ch->rect.h;
            }
        }
        for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
            cui_node *ch = &ui->nodes[c];
            float r = ch->rect.x + ch->rect.w - ox, b = ch->rect.y + ch->rect.h - oy;
            if (r > max_r) max_r = r;
            if (b > max_b) max_b = b;
        }
    }
    n->content_w = max_r + n->pad[1] + n->pad[3];
    n->content_h = max_b + n->pad[0] + n->pad[2];
    if (n->flags & (CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y)) {
        cui_scroll_state *ss = cui_state(ui, n->id, sizeof(cui_scroll_state));
        ss->content_w = n->content_w;
        ss->content_h = n->content_h;
        ss->view_w = n->rect.w;
        ss->view_h = n->rect.h;
    }
    for (int c = n->first; c >= 0; c = ui->nodes[c].next) cui__place(ui, c);
    for (int c = n->abs_first; c >= 0; c = ui->nodes[c].next) {
        cui_node *ab = &ui->nodes[c];
        if (ab->abs_anchor >= 0) {
            ab->rect.x = ui->nodes[ab->abs_anchor].rect.x + ab->fx;
            ab->rect.y = ui->nodes[ab->abs_anchor].rect.y + ab->fy;
        } else {
            ab->rect.x = n->rect.x + ab->fx - n->scroll_x;
            ab->rect.y = n->rect.y + ab->fy - n->scroll_y;
        }
        cui__place(ui, c);
    }
}

static cui_rect cui__anchor_rect(cui_ctx *ui, cui_id anchor) {
    cui_state_entry *e = cui__state_entry(ui, anchor);
    return e->rect;
}

static void cui__layout_float(cui_ctx *ui, int i) {
    cui_node *n = &ui->nodes[i];
    float vw = ui->vw, vh = ui->vh, margin = 8;
    cui__measure_w(ui, i);
    n = &ui->nodes[i];
    if (n->placement == CUI__PLACE_FULL) n->rect.w = vw;
    else if (n->w.kind == CUI_SIZE_FIXED) n->rect.w = n->w.value;
    else if (n->w.kind == CUI_SIZE_PERCENT) n->rect.w = vw * n->w.value / 100.0f;
    else if (n->w.kind == CUI_SIZE_GROW) n->rect.w = vw;
    else n->rect.w = CUI_MIN(n->pref_w, vw - 2 * margin);
    n->rect.w = cui__clamp_size(n->rect.w, n->min_w, CUI_MIN(n->max_w, n->placement == CUI__PLACE_FULL ? vw : vw - 2 * margin));
    cui__resolve_w(ui, i);
    cui__measure_h(ui, i);
    n = &ui->nodes[i];
    if (n->placement == CUI__PLACE_FULL) n->rect.h = vh;
    else if (n->h.kind == CUI_SIZE_FIXED) n->rect.h = n->h.value;
    else if (n->h.kind == CUI_SIZE_PERCENT) n->rect.h = vh * n->h.value / 100.0f;
    else if (n->h.kind == CUI_SIZE_GROW) n->rect.h = vh;
    else n->rect.h = n->pref_h;
    if (n->placement != CUI__PLACE_FULL) n->rect.h = CUI_MIN(n->rect.h, CUI_MIN(n->max_h, vh - 2 * margin));
    float W = n->rect.w, H = n->rect.h, off = n->offset;
    float x = 0, y = 0;
    if (n->placement == CUI__PLACE_FULL) { x = 0; y = 0; }
    else if (n->placement == CUI__PLACE_CENTER) { x = (vw - W) * 0.5f; y = (vh - H) * 0.5f; }
    else if (n->placement == CUI__PLACE_POINT) { x = n->fx; y = n->fy; }
    else {
        cui_rect A = cui__anchor_rect(ui, n->anchor);
        switch (n->placement) {
        case CUI_PLACE_BOTTOM_START: case CUI_PLACE_BOTTOM: case CUI_PLACE_BOTTOM_END:
            y = A.y + A.h + off;
            if (y + H > vh - margin && A.y - off - H >= margin) y = A.y - off - H;
            break;
        case CUI_PLACE_TOP_START: case CUI_PLACE_TOP: case CUI_PLACE_TOP_END:
            y = A.y - off - H;
            if (y < margin && A.y + A.h + off + H <= vh - margin) y = A.y + A.h + off;
            break;
        case CUI_PLACE_RIGHT_START: case CUI_PLACE_RIGHT:
            x = A.x + A.w + off;
            if (x + W > vw - margin && A.x - off - W >= margin) x = A.x - off - W;
            break;
        case CUI_PLACE_LEFT_START: case CUI_PLACE_LEFT:
            x = A.x - off - W;
            if (x < margin && A.x + A.w + off + W <= vw - margin) x = A.x + A.w + off;
            break;
        }
        switch (n->placement) {
        case CUI_PLACE_BOTTOM_START: case CUI_PLACE_TOP_START: x = A.x; break;
        case CUI_PLACE_BOTTOM: case CUI_PLACE_TOP: x = A.x + (A.w - W) * 0.5f; break;
        case CUI_PLACE_BOTTOM_END: case CUI_PLACE_TOP_END: x = A.x + A.w - W; break;
        case CUI_PLACE_RIGHT_START: case CUI_PLACE_LEFT_START: y = A.y; break;
        case CUI_PLACE_RIGHT: case CUI_PLACE_LEFT: y = A.y + (A.h - H) * 0.5f; break;
        }
    }
    if (n->placement != CUI__PLACE_FULL) {
        x = cui__clampf(x, margin, CUI_MAX(margin, vw - margin - W));
        y = cui__clampf(y, margin, CUI_MAX(margin, vh - margin - H));
    }
    n->rect.x = roundf(x);
    n->rect.y = roundf(y);
    cui__resolve_h(ui, i);
    cui__place(ui, i);
}

void cui__layout(cui_ctx *ui) {
    if (ui->node_count == 0) return;
    cui_node *root = &ui->nodes[0];
    cui__measure_w(ui, 0);
    root = &ui->nodes[0];
    root->rect.x = 0; root->rect.y = 0;
    root->rect.w = ui->vw;
    cui__resolve_w(ui, 0);
    cui__measure_h(ui, 0);
    root = &ui->nodes[0];
    root->rect.h = ui->vh;
    cui__resolve_h(ui, 0);
    cui__place(ui, 0);
    for (int k = 0; k < ui->float_count; k++) cui__layout_float(ui, ui->float_roots[k]);
}

/* ------------------------------------------------------------------------ */
/* Paint                                                                    */
/* ------------------------------------------------------------------------ */

static void cui__add_hit(cui_ctx *ui, cui_id id, cui_rect r, uint32_t flags) {
    if (r.w <= 0 || r.h <= 0) return;
    CUI__GROW(ui, ui->hits, ui->hit_count, ui->hit_cap, 1);
    cui_hit *h = &ui->hits[ui->hit_count++];
    h->id = id; h->rect = r; h->flags = flags; h->layer = ui->current_layer; h->order = ui->hit_count;
}

static cui_rect cui__xform_rect(cui_ctx *ui, cui_rect r) {
    cui_xform x = ui->dl.xform_stack[ui->dl.xform_depth - 1];
    if (x.scale == 1 && x.ox == 0 && x.oy == 0) return r;
    float s = ui->scale;
    cui_rect o = { (r.x * s * x.scale + x.ox) / s, (r.y * s * x.scale + x.oy) / s, r.w * x.scale, r.h * x.scale };
    return o;
}

void cui__paint_scrollbars(cui_ctx *ui, cui_node *n, cui_rect clip);

static void cui__paint_text_lines(cui_ctx *ui, cui_node *n);
static void cui__paint_text(cui_ctx *ui, cui_node *n) {
    ui->text_fx = n->text_fx;
    ui->text_fx_data = n->text_fx_data;
#ifdef CUI_TEXT_SHAPING
    ui->text_paragraph_len = n->text_len;
    ui->text_paragraph = cui__text_needs_shaping(ui, n->text, n->text_len) ? n->text : NULL;
#endif
    cui__paint_text_lines(ui, n);
#ifdef CUI_TEXT_SHAPING
    ui->text_paragraph = NULL;
#endif
    ui->text_fx = NULL;
    ui->text_fx_base = 0;
}

static void cui__paint_text_lines(cui_ctx *ui, cui_node *n) {
    float lh = cui__text_line_px(n);
    float fh = cui__font_line_height(ui, n->font, n->font_size);
    float top_pad = (lh - fh) * 0.5f;
    bool tab = (n->flags & CUI_NF_TABULAR) != 0;
    if (n->flags & CUI_NF_WRAP && n->lines) {
        for (int k = 0; k < n->text_lines; k++) {
            cui_text_layout_line *L = &n->lines[k];
            float x = n->rect.x;
            /* line widths come from wrapping, which measures proportional figures */
            float lw = tab && n->text_align != CUI_TEXT_LEFT ? cui__text_width(ui, n->font, n->font_size, n->text + L->start, L->end - L->start, true) : L->width;
            if (n->text_align == CUI_TEXT_CENTER) x += (n->rect.w - lw) * 0.5f;
            else if (n->text_align == CUI_TEXT_RIGHT) x += n->rect.w - lw;
            ui->text_fx_base = L->start;
            ui->text_fx_line_y = n->rect.y + lh * (float)k;
            cui__draw_text_line(ui, n->font, n->font_size, x, n->rect.y + top_pad + lh * (float)k, n->text_color,
                                n->text + L->start, L->end - L->start, tab);
        }
        return;
    }
    /* single line (or hard newlines), ellipsis when too narrow */
    int start = 0, k = 0;
    for (int p = 0; p <= n->text_len; p++) {
        if (p == n->text_len || n->text[p] == '\n') {
            int len = p - start;
            float w = cui__text_width(ui, n->font, n->font_size, n->text + start, len, tab);
            float x = n->rect.x;
            if (w < n->rect.w) {
                if (n->text_align == CUI_TEXT_CENTER) x += (n->rect.w - w) * 0.5f;
                else if (n->text_align == CUI_TEXT_RIGHT) x += n->rect.w - w;
            }
            ui->text_fx_base = start;
            ui->text_fx_line_y = n->rect.y + lh * (float)k;
            cui__draw_text_ellipsis(ui, n->font, n->font_size, x, n->rect.y + top_pad + lh * (float)k, n->rect.w,
                                    n->text_color, n->text + start, len, tab);
            start = p + 1;
            k++;
        }
    }
}

static void cui__paint_node(cui_ctx *ui, int i, cui_rect clip) {
    cui_node *n = &ui->nodes[i];
    cui_rect r = n->rect;
    /* cull nodes outside the clip (and their subtree) */
    if (!(n->flags & CUI_NF_FLOATING)) {
        cui_rect vis = cui__rect_intersect(cui__xform_rect(ui, r), clip);
        if ((vis.w <= 0 || vis.h <= 0) && !(r.w == 0 || r.h == 0)) return;
    }
    bool xf = n->opacity < 0.999f || n->scale != 1.0f || n->tx != 0 || n->ty != 0;
    if (xf) cui__push_xform(ui, n->scale, r.x + r.w * 0.5f, r.y + r.h * 0.5f, n->opacity, n->tx, n->ty);
    if (n->flags & (CUI_NF_INTERACTIVE | CUI_NF_BLOCKER | CUI_NF_HIT_OPAQUE | CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y)) {
        cui_rect hr = (n->flags & CUI_NF_BLOCKER) ? r : cui__rect_intersect(cui__xform_rect(ui, r), clip);
        cui__add_hit(ui, n->id, hr, n->flags);
    }
    if (n->elevation) cui__draw_elevation(ui, r, n->radius, n->elevation, n->elevation_alpha);
    /* a clipping box draws its outline over its children, so a header or a
     * row background inside it never hides the edge */
    bool border_after = (n->flags & CUI_NF_CLIP) && n->border > 0 && n->border_color.a;
    if (border_after) { if (n->bg.a) cui_draw_rect(ui, r, n->radius, n->bg); }
    else if (n->bg.a || (n->border > 0 && n->border_color.a)) cui_draw_rect_border(ui, r, n->radius, n->bg, n->border_color, n->border);
    if (n->elevation && n->bg.a > 200 && ui->mode == CUI_THEME_DARK) cui__draw_top_light(ui, r, n->radius, n->elevation == CUI_ELEVATION_RESTING ? 0.045f : 0.085f);
    if (n->paint && !(n->flags & CUI_NF_PAINT_AFTER)) n->paint(ui, n, n->paint_data);
    if (n->flags & CUI_NF_TEXT) cui__paint_text(ui, n);
    bool clips = (n->flags & (CUI_NF_CLIP | CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y)) != 0;
    cui_rect child_clip = clip;
    if (clips) {
        cui_push_clip(ui, r);
        child_clip = cui__rect_intersect(clip, cui__xform_rect(ui, r));
    }
    for (int c = n->abs_first; c >= 0; c = ui->nodes[c].next) cui__paint_node(ui, c, child_clip);
    bool any_top = false;
    for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
        if (ui->nodes[c].flags & CUI_NF_ON_TOP) { any_top = true; continue; }
        cui__paint_node(ui, c, child_clip);
    }
    if (any_top)
        for (int c = n->first; c >= 0; c = ui->nodes[c].next)
            if (ui->nodes[c].flags & CUI_NF_ON_TOP) cui__paint_node(ui, c, child_clip);
    if (clips) cui_pop_clip(ui);
    n = &ui->nodes[i];
    if (border_after) cui_draw_rect_border(ui, r, n->radius, cui__rgba(0, 0, 0, 0), n->border_color, n->border);
    if (n->paint && (n->flags & CUI_NF_PAINT_AFTER)) n->paint(ui, n, n->paint_data);
    if (n->flags & (CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y)) cui__paint_scrollbars(ui, n, clip);
    if (xf) cui__pop_xform(ui);
}

void cui__paint_add_hit(cui_ctx *ui, cui_id id, cui_rect r, uint32_t flags) { cui__add_hit(ui, id, r, flags); }

void cui__paint_wheel_hit(cui_ctx *ui, cui_id id, cui_rect r) {
    cui_rect clip = ui->dl.clip_stack[ui->dl.clip_depth - 1];
    float s = ui->scale;
    cui_rect cp = { clip.x / s, clip.y / s, clip.w / s, clip.h / s };
    /* it can always scroll, so the wheel stops here instead of going on to the page */
    cui_scroll_state *ss = cui_state(ui, id, sizeof *ss);
    ss->view_w = r.w; ss->view_h = r.h;
    ss->content_w = r.w + 1e6f; ss->content_h = r.h + 1e6f;
    cui__add_hit(ui, id, cui__rect_intersect(r, cp), CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y);
}

void cui__paint(cui_ctx *ui) {
    if (ui->node_count == 0) return;
    cui_rect full = { 0, 0, ui->vw, ui->vh };
    ui->current_layer = 0;
    cui__paint_node(ui, 0, full);
    /* floating layers, by z then creation order */
    int n = ui->float_count;
    int *order = cui__frame_alloc(ui, sizeof(int) * (size_t)(n > 0 ? n : 1));
    for (int k = 0; k < n; k++) order[k] = ui->float_roots[k];
    for (int a = 1; a < n; a++) {
        int v = order[a], b = a - 1;
        while (b >= 0 && ui->nodes[order[b]].z > ui->nodes[v].z) { order[b + 1] = order[b]; b--; }
        order[b + 1] = v;
    }
    for (int k = 0; k < n; k++) {
        ui->current_layer = ui->nodes[order[k]].id;
        cui__paint_node(ui, order[k], full);
    }
    ui->current_layer = 0;
}
