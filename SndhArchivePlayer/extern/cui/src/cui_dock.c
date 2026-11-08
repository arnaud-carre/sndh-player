/* Docking layout: a dockspace lays panels out in a tree of splits whose
 * leaves hold panels as document tabs.
 *
 * The tree lives in the dockspace's state and is rebuilt as ordinary nodes
 * every frame: splits are the split view (cui_split_begin) and each leaf is a
 * column with a document tab strip (cui_doc_tabs) above a body. Panels are
 * submitted later in the frame, in the caller's order; a panel's content is
 * appended to its leaf's body, which is still in the node tree, by opening
 * that body as the current parent again. No layout engine change is needed.
 *
 * Tab drags start in the strip (where the document tabs reorder as usual)
 * and turn into a dock drag once the pointer leaves the strip: the strip is
 * told the press ended (so its own drag lets go) and the dockspace takes the
 * pointer, shows a ghost tab, drop targets and a preview, and re-docks the
 * panel on release. Closed panels stay in the tree, hidden, so reopening one
 * brings it back to its last place; a leaf with nothing to show is left out
 * of the layout and its split merges into the neighbour. */
#include "cui_internal.h"

#define CUI__DK_PANELS 48
#define CUI__DK_NODES  96          /* a binary tree of 48 leaves needs 95 */
#define CUI__DK_NAME   64
#define CUI__DK_LABEL  128
#define CUI__DK_DEPTH  40          /* nesting accepted by cui_dock_load */
#define CUI__DK_TILE   32.0f       /* drop target */
#define CUI__DK_STRIP  36.0f       /* fallback strip height before the first layout */

enum { DK_FREE = 0, DK_LEAF, DK_SPLIT };
enum { DK_NONE = 0, DK_CENTER, DK_LEFT, DK_RIGHT, DK_TOP, DK_BOTTOM };
enum { DK_IDLE = 0, DK_PENDING, DK_DRAG, DK_CANCELLED };

typedef struct {
    uint8_t kind;
    bool vertical;             /* split: stacked (v) rather than side by side (h) */
    float ratio;               /* split: share of the first child */
    int a, b, parent;
    int sel;                   /* leaf: selected panel slot, -1 */
    uint32_t uid;              /* stable while the node lives: strip, split and leaf ids */
} cui__dk_node;

typedef struct {
    bool used;
    bool known;                /* submitted at least once */
    bool open, closable, dirty, close_req, deferred;
    int8_t pending;            /* open state from a loaded layout, applied on the next submit; -1 none */
    int leaf;                  /* tree node, -1 when not placed */
    int order;                 /* position among the leaf's tabs */
    uint64_t seen;             /* frame of the last submit */
    char name[CUI__DK_NAME];   /* identity, as saved */
    char label[CUI__DK_LABEL]; /* the title as given, for the tab */
    char icon[8];
} cui__dk_panel;

typedef struct {
    bool init;
    uint32_t gen;              /* bumped when a load replaces the tree */
    int root;
    uint32_t next_uid;
    uint64_t built;            /* frame of the last build */
    int cur_leaf;              /* leaf with the keyboard or the last press */
    int refocus;               /* panel whose tab gets the focus back after a move, -1 */
    int compact_sel;           /* panel selected in the narrow one-stack view, -1 */
    cui__dk_node nodes[CUI__DK_NODES];
    cui__dk_panel panels[CUI__DK_PANELS];
    /* tab drag */
    int drag, drag_panel, drag_leaf;
    float grab_x, grab_y;      /* where the tab was taken, from its corner */
    int t_kind;                /* 0 none, 1 a leaf, 2 an outer edge */
    int t_leaf, t_zone, t_index;
    float pv[4], pv_alpha, tiles_alpha;
    cui_spring lift;
} cui__dk_state;

typedef struct {
    float alpha;
    int ntiles, hot;
    cui_rect tiles[9];
    uint8_t zone[9];
    bool outer[9];
    float pv_alpha;
    cui_rect pv;
    bool marker;
    cui_rect mk;
} cui__dk_paint;

typedef struct cui__dk_build {
    cui__dk_state *st;
    cui_id id, seed;
    uint64_t frame;
    uint32_t gen;
    int container;
    float min_w, min_h;
    bool shortcuts, in_panel;
    bool compact;              /* too narrow to split: every panel is a tab of one stack */
    int takeover;              /* leaf whose strip is told the press ended, this frame */
    bool vis[CUI__DK_NODES];
    int body[CUI__DK_NODES];
    int shown[CUI__DK_NODES];  /* panel shown selected in each leaf's strip */
    cui_rect leaf_rect[CUI__DK_NODES], strip_rect[CUI__DK_NODES];
    bool tab_vis[CUI__DK_PANELS];
    cui_id tab_id[CUI__DK_PANELS];
    cui__dk_paint *paint;
    struct cui__dk_build *prev;
} cui__dk_build;

typedef struct { cui__dk_build *cur; } cui__dk_reg;

static cui__dk_reg *cui__dk_registry(cui_ctx *ui) { return cui_state(ui, cui__hash_str("cui.dockspace", 0xD0C4u), sizeof(cui__dk_reg)); }
static cui__dk_build *cui__dk_cur(cui_ctx *ui) {
    cui__dk_reg *reg = cui__dk_registry(ui);
    if (reg->cur && reg->cur->frame != ui->frame) reg->cur = NULL;
    return reg->cur;
}
static void cui__dk_init(cui__dk_state *st) {
    memset(st, 0, sizeof *st);
    st->init = true;
    st->root = -1;
    st->cur_leaf = -1;
    st->refocus = -1;
    st->compact_sel = -1;
    st->next_uid = 1;
    for (int i = 0; i < CUI__DK_PANELS; i++) { st->panels[i].leaf = -1; st->panels[i].pending = -1; }
}
static cui__dk_state *cui__dk_state_of(cui_ctx *ui, const char *id) {
    return cui_state(ui, cui__hash_str(id ? id : "dock", 0xD0C5A11u), sizeof(cui__dk_state));
}

/* ------------------------------------------------------------------------ */
/* Tree                                                                     */
/* ------------------------------------------------------------------------ */

static int cui__dk_alloc(cui__dk_state *st, int kind) {
    for (int i = 0; i < CUI__DK_NODES; i++)
        if (st->nodes[i].kind == DK_FREE) {
            cui__dk_node *n = &st->nodes[i];
            memset(n, 0, sizeof *n);
            n->kind = (uint8_t)kind;
            n->a = n->b = n->parent = n->sel = -1;
            n->ratio = 0.5f;
            n->uid = st->next_uid++;
            return i;
        }
    return -1;
}
static int cui__dk_free_nodes(const cui__dk_state *st) {
    int n = 0;
    for (int i = 0; i < CUI__DK_NODES; i++) n += st->nodes[i].kind == DK_FREE;
    return n;
}
static void cui__dk_replace(cui__dk_state *st, int parent, int old, int nw) {
    if (parent < 0) st->root = nw;
    else if (st->nodes[parent].a == old) st->nodes[parent].a = nw;
    else st->nodes[parent].b = nw;
    if (nw >= 0) st->nodes[nw].parent = parent;
}
static int cui__dk_count(const cui__dk_state *st, int leaf) {
    int n = 0;
    for (int i = 0; i < CUI__DK_PANELS; i++) n += st->panels[i].used && st->panels[i].leaf == leaf;
    return n;
}
/* a leaf's panels by tab order; returns the count */
static int cui__dk_tabs(const cui__dk_state *st, int leaf, int *out, int exclude) {
    int n = 0;
    for (int i = 0; i < CUI__DK_PANELS; i++)
        if (st->panels[i].used && st->panels[i].leaf == leaf && i != exclude) {
            int k = n++;
            while (k > 0 && st->panels[out[k - 1]].order > st->panels[i].order) { out[k] = out[k - 1]; k--; }
            out[k] = i;
        }
    return n;
}
static void cui__dk_renumber(cui__dk_state *st, const int *list, int n) {
    for (int k = 0; k < n; k++) st->panels[list[k]].order = k;
}
/* takes a leaf out of the tree; its split merges into the sibling */
static void cui__dk_remove_leaf(cui__dk_state *st, int leaf) {
    int par = st->nodes[leaf].parent;
    st->nodes[leaf].kind = DK_FREE;
    if (st->cur_leaf == leaf) st->cur_leaf = -1;
    if (par < 0) { st->root = -1; return; }
    int sib = st->nodes[par].a == leaf ? st->nodes[par].b : st->nodes[par].a;
    cui__dk_replace(st, st->nodes[par].parent, par, sib);
    st->nodes[par].kind = DK_FREE;
}
static void cui__dk_detach(cui__dk_state *st, int p) {
    int leaf = st->panels[p].leaf;
    st->panels[p].leaf = -1;
    if (leaf < 0) return;
    if (st->nodes[leaf].sel == p) st->nodes[leaf].sel = -1;
    if (cui__dk_count(st, leaf) == 0) cui__dk_remove_leaf(st, leaf);
}
/* puts panel p into leaf before the tab `before` (-1: at the end) */
static void cui__dk_insert(cui__dk_state *st, int p, int leaf, int before) {
    int list[CUI__DK_PANELS + 1];
    int n = cui__dk_tabs(st, leaf, list, p), at = n;
    for (int k = 0; k < n; k++) if (list[k] == before) { at = k; break; }
    memmove(list + at + 1, list + at, sizeof(int) * (size_t)(n - at));
    list[at] = p;
    st->panels[p].leaf = leaf;
    cui__dk_renumber(st, list, n + 1);
}
static int cui__dk_first_leaf(const cui__dk_state *st, int i) {
    while (i >= 0 && st->nodes[i].kind == DK_SPLIT) i = st->nodes[i].a;
    return i;
}
/* splits `target` so a new leaf sits on `zone`'s side with `share` of the room */
static int cui__dk_split(cui__dk_state *st, int target, int zone, float share) {
    int x = cui__dk_alloc(st, DK_SPLIT);
    if (x < 0) return -1;
    int n = cui__dk_alloc(st, DK_LEAF);
    if (n < 0) { st->nodes[x].kind = DK_FREE; return -1; }
    bool first = zone == DK_LEFT || zone == DK_TOP;
    cui__dk_replace(st, st->nodes[target].parent, target, x);
    cui__dk_node *s = &st->nodes[x];
    s->vertical = zone == DK_TOP || zone == DK_BOTTOM;
    s->ratio = first ? share : 1.0f - share;
    s->a = first ? n : target;
    s->b = first ? target : n;
    st->nodes[target].parent = x;
    st->nodes[n].parent = x;
    return n;
}

/* ------------------------------------------------------------------------ */
/* Panels                                                                   */
/* ------------------------------------------------------------------------ */

static void cui__dk_name_of(const char *label, char *out) {
    const char *k = strstr(label, "###");
    const char *s = k ? k + 3 : label;
    size_t n = strlen(s);
    if (n > CUI__DK_NAME - 1) {
        n = CUI__DK_NAME - 1;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;   /* never half a code point */
    }
    memcpy(out, s, n);
    out[n] = 0;
}
static int cui__dk_find(const cui__dk_state *st, const char *name) {
    for (int i = 0; i < CUI__DK_PANELS; i++) if (st->panels[i].used && strcmp(st->panels[i].name, name) == 0) return i;
    return -1;
}
static int cui__dk_new_panel(cui__dk_state *st, const char *name) {
    for (int i = 0; i < CUI__DK_PANELS; i++)
        if (!st->panels[i].used) {
            cui__dk_panel *p = &st->panels[i];
            memset(p, 0, sizeof *p);
            p->used = true;
            p->leaf = -1;
            p->pending = -1;
            snprintf(p->name, sizeof p->name, "%s", name);
            snprintf(p->label, sizeof p->label, "%s", name);
            return i;
        }
    return -1;
}

/* ------------------------------------------------------------------------ */
/* Saved layouts                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { char *buf; size_t cap, len; } cui__dk_out;
static void cui__dk_put(cui__dk_out *o, const char *s, size_t n) {
    for (size_t k = 0; k < n; k++) {
        if (o->buf && o->len + 1 < o->cap) o->buf[o->len] = s[k];
        o->len++;
    }
}
static void cui__dk_put_ratio(cui__dk_out *o, float r) {
    int q = (int)(cui__clampf(r, 0, 1) * 10000.0f + 0.5f);
    char t[8];
    if (q >= 10000) { cui__dk_put(o, "1", 1); return; }
    if (q <= 0) { cui__dk_put(o, "0", 1); return; }
    t[0] = '0'; t[1] = '.';
    int n = 2;
    for (int d = 1000; d > 0 && q > 0; d /= 10) { t[n++] = (char)('0' + q / d); q %= d; }
    cui__dk_put(o, t, (size_t)n);
}
static bool cui__dk_special(char c) { return c == ',' || c == '[' || c == ']' || c == '(' || c == ')' || c == '\\'; }
static void cui__dk_write(const cui__dk_state *st, int i, cui__dk_out *o) {
    const cui__dk_node *n = &st->nodes[i];
    if (n->kind == DK_SPLIT) {
        cui__dk_put(o, n->vertical ? "v" : "h", 1);
        cui__dk_put_ratio(o, n->ratio);
        cui__dk_put(o, "(", 1);
        cui__dk_write(st, n->a, o);
        cui__dk_put(o, ",", 1);
        cui__dk_write(st, n->b, o);
        cui__dk_put(o, ")", 1);
        return;
    }
    int list[CUI__DK_PANELS];
    int c = cui__dk_tabs(st, i, list, -1);
    cui__dk_put(o, "[", 1);
    for (int k = 0; k < c; k++) {
        const cui__dk_panel *p = &st->panels[list[k]];
        if (k) cui__dk_put(o, ",", 1);
        if (n->sel == list[k]) cui__dk_put(o, "*", 1);
        if ((p->known && !p->open) || (!p->known && p->pending == 0)) cui__dk_put(o, "-", 1);
        for (const char *s = p->name; *s; s++) {
            bool lead = s == p->name && (*s == '*' || *s == '-');
            if (cui__dk_special(*s) || (unsigned char)*s < 0x20 || lead) cui__dk_put(o, "\\", 1);
            cui__dk_put(o, s, 1);
        }
    }
    cui__dk_put(o, "]", 1);
}

/* the parse fills a scratch tree; nothing touches the state until it all checks out */
typedef struct { uint8_t kind; bool vertical; float ratio; int a, b, parent, sel; } cui__dk_pnode;
typedef struct { char name[CUI__DK_NAME]; int leaf, order; bool closed; } cui__dk_ptab;
typedef struct {
    const char *s;
    size_t pos, len;
    cui__dk_pnode nodes[CUI__DK_NODES];
    int nn;
    cui__dk_ptab tabs[CUI__DK_PANELS];
    int nt;
} cui__dk_parser;

static bool cui__dk_parse_ratio(cui__dk_parser *P, float *out) {
    if (P->pos >= P->len) return false;
    char c = P->s[P->pos];
    if (c != '0' && c != '1') return false;
    int whole = c - '0';
    P->pos++;
    int frac = 0, digits = 0;
    if (P->pos < P->len && P->s[P->pos] == '.') {
        P->pos++;
        while (P->pos < P->len && P->s[P->pos] >= '0' && P->s[P->pos] <= '9') {
            if (++digits > 6) return false;
            frac = frac * 10 + (P->s[P->pos] - '0');
            P->pos++;
        }
        if (digits == 0) return false;
    }
    float v = (float)whole;
    if (digits) { float d = 1; for (int k = 0; k < digits; k++) d *= 10; v += (float)frac / d; }
    if (v > 1.0f) return false;
    *out = cui__clampf(v, 0.01f, 0.99f);
    return true;
}
static int cui__dk_parse_node(cui__dk_parser *P, int parent, int depth) {
    if (depth > CUI__DK_DEPTH || P->pos >= P->len || P->nn >= CUI__DK_NODES) return -1;
    int me = P->nn++;
    cui__dk_pnode *n = &P->nodes[me];
    memset(n, 0, sizeof *n);
    n->parent = parent;
    n->a = n->b = n->sel = -1;
    char c = P->s[P->pos];
    if (c == 'h' || c == 'v') {
        n->kind = DK_SPLIT;
        n->vertical = c == 'v';
        P->pos++;
        if (!cui__dk_parse_ratio(P, &n->ratio)) return -1;
        if (P->pos >= P->len || P->s[P->pos] != '(') return -1;
        P->pos++;
        int a = cui__dk_parse_node(P, me, depth + 1);
        if (a < 0 || P->pos >= P->len || P->s[P->pos] != ',') return -1;
        P->pos++;
        int b = cui__dk_parse_node(P, me, depth + 1);
        if (b < 0 || P->pos >= P->len || P->s[P->pos] != ')') return -1;
        P->pos++;
        n = &P->nodes[me];
        n->a = a; n->b = b;
        return me;
    }
    if (c != '[') return -1;
    n->kind = DK_LEAF;
    P->pos++;
    int count = 0;
    for (;;) {
        if (P->nt >= CUI__DK_PANELS) return -1;
        cui__dk_ptab *t = &P->tabs[P->nt];
        memset(t, 0, sizeof *t);
        bool star = false;
        for (int k = 0; k < 2 && P->pos < P->len; k++) {
            char f = P->s[P->pos];
            if (f == '*' && !star) { star = true; P->pos++; }
            else if (f == '-' && !t->closed) { t->closed = true; P->pos++; }
        }
        size_t nl = 0;
        while (P->pos < P->len) {
            char ch = P->s[P->pos];
            if (ch == ',' || ch == ']') break;
            if (ch == '[' || ch == '(' || ch == ')' || (unsigned char)ch < 0x20) return -1;
            if (ch == '\\') {
                if (++P->pos >= P->len) return -1;
                ch = P->s[P->pos];
            }
            if (nl + 1 >= CUI__DK_NAME) return -1;
            t->name[nl++] = ch;
            P->pos++;
        }
        if (nl == 0 || P->pos >= P->len) return -1;
        t->name[nl] = 0;
        for (int k = 0; k < P->nt; k++) if (strcmp(P->tabs[k].name, t->name) == 0) return -1;   /* names are unique */
        t->leaf = me;
        t->order = count++;
        if (star) {
            if (P->nodes[me].sel >= 0) return -1;
            P->nodes[me].sel = P->nt;
        }
        P->nt++;
        if (P->s[P->pos] == ']') { P->pos++; break; }
        P->pos++;   /* ',' */
    }
    return me;
}

static bool cui__dk_load(cui_ctx *ui, cui__dk_state *st, const char *text) {
    if (!text) return false;
    cui__dk_parser *P = cui__alloc(ui, sizeof *P);
    if (!P) return false;
    memset(P, 0, sizeof *P);
    P->s = text;
    P->len = strlen(text);
    bool ok = P->len > 6 && memcmp(text, "dock1 ", 6) == 0;
    if (ok) {
        P->pos = 5;
        while (P->pos < P->len && P->s[P->pos] == ' ') P->pos++;
        ok = cui__dk_parse_node(P, -1, 0) == 0;
        while (ok && P->pos < P->len && (P->s[P->pos] == ' ' || P->s[P->pos] == '\n' || P->s[P->pos] == '\r' || P->s[P->pos] == '\t')) P->pos++;
        ok = ok && P->pos == P->len;
    }
    if (!ok) { cui__free(ui, P); return false; }
    /* make room: panels the new layout names keep their slot, the others that
     * have been seen stay (unplaced) while there is room, the rest go */
    int keep_needed = 0;
    for (int k = 0; k < P->nt; k++) if (cui__dk_find(st, P->tabs[k].name) < 0) keep_needed++;
    int free_slots = 0;
    for (int i = 0; i < CUI__DK_PANELS; i++) {
        cui__dk_panel *p = &st->panels[i];
        bool named = false;
        if (p->used) for (int k = 0; k < P->nt && !named; k++) named = strcmp(P->tabs[k].name, p->name) == 0;
        if (p->used && !named && !p->known) p->used = false;
        if (!p->used) free_slots++;
    }
    for (int i = 0; i < CUI__DK_PANELS && free_slots < keep_needed; i++) {
        cui__dk_panel *p = &st->panels[i];
        bool named = false;
        if (p->used) for (int k = 0; k < P->nt && !named; k++) named = strcmp(P->tabs[k].name, p->name) == 0;
        if (p->used && !named) { p->used = false; free_slots++; }
    }
    /* the tree */
    for (int i = 0; i < CUI__DK_NODES; i++) st->nodes[i].kind = DK_FREE;
    for (int i = 0; i < P->nn; i++) {
        cui__dk_node *n = &st->nodes[i];
        const cui__dk_pnode *q = &P->nodes[i];
        n->kind = q->kind; n->vertical = q->vertical; n->ratio = q->kind == DK_SPLIT ? q->ratio : 0.5f;
        n->a = q->a; n->b = q->b; n->parent = q->parent; n->sel = -1;
        n->uid = st->next_uid++;
    }
    st->root = 0;
    for (int i = 0; i < CUI__DK_PANELS; i++) st->panels[i].leaf = -1;
    for (int k = 0; k < P->nt; k++) {
        const cui__dk_ptab *t = &P->tabs[k];
        int p = cui__dk_find(st, t->name);
        if (p < 0) p = cui__dk_new_panel(st, t->name);
        if (p < 0) continue;   /* cannot happen: room was made above */
        st->panels[p].leaf = t->leaf;
        st->panels[p].order = t->order;
        st->panels[p].pending = t->closed ? 0 : 1;
        st->panels[p].close_req = false;
        if (P->nodes[t->leaf].sel == k) st->nodes[t->leaf].sel = p;
    }
    st->gen++;
    st->cur_leaf = -1;
    if (st->drag != DK_IDLE) st->drag = DK_CANCELLED;
    st->t_kind = 0;
    st->refocus = -1;
    cui__free(ui, P);
    return true;
}

size_t cui_dock_save(cui_ctx *ui, const char *id, char *buf, size_t cap) {
    if (buf && cap) buf[0] = 0;
    cui__dk_state *st = cui__dk_state_of(ui, id);
    if (!st->init || st->root < 0) return 0;
    cui__dk_out o = { buf, cap, 0 };
    cui__dk_put(&o, "dock1 ", 6);
    cui__dk_write(st, st->root, &o);
    if (buf && cap) buf[o.len < cap ? o.len : cap - 1] = 0;
    return o.len;
}

bool cui_dock_load(cui_ctx *ui, const char *id, const char *text) {
    cui__dk_state *st = cui__dk_state_of(ui, id);
    if (!st->init) cui__dk_init(st);
    return cui__dk_load(ui, st, text);
}

/* ------------------------------------------------------------------------ */
/* Dockspace                                                                */
/* ------------------------------------------------------------------------ */

static bool cui__dk_present(const cui__dk_state *st, const cui__dk_panel *p) {
    return p->used && p->known && p->open && p->leaf >= 0 && p->seen == st->built && st->built > 0;
}
static bool cui__dk_vis(cui__dk_build *b, int i) {
    cui__dk_state *st = b->st;
    if (i < 0) return false;
    cui__dk_node *n = &st->nodes[i];
    bool v = false;
    if (n->kind == DK_SPLIT) {
        bool va = cui__dk_vis(b, n->a), vb = cui__dk_vis(b, n->b);
        v = va || vb;
    } else if (n->kind == DK_LEAF) {
        for (int k = 0; k < CUI__DK_PANELS && !v; k++) v = st->panels[k].leaf == i && cui__dk_present(st, &st->panels[k]);
    }
    b->vis[i] = v;
    return v;
}
static void cui__dk_compute_vis(cui__dk_build *b) {
    memset(b->vis, 0, sizeof b->vis);
    cui__dk_vis(b, b->st->root);
}
/* the visible node a split shows in its place (it skips splits with one side hidden) */
static int cui__dk_shown(const cui__dk_build *b, int i) {
    const cui__dk_state *st = b->st;
    while (i >= 0 && st->nodes[i].kind == DK_SPLIT) {
        bool va = b->vis[st->nodes[i].a], vb = b->vis[st->nodes[i].b];
        if (va && vb) break;
        i = va ? st->nodes[i].a : st->nodes[i].b;
    }
    return i;
}
static int cui__dk_visible_tabs(const cui__dk_state *st, int leaf, int *out, int exclude) {
    int all[CUI__DK_PANELS];
    int n = cui__dk_tabs(st, leaf, all, exclude), c = 0;
    for (int k = 0; k < n; k++) if (cui__dk_present(st, &st->panels[all[k]])) out[c++] = all[k];
    return c;
}
static cui_id cui__dk_leaf_id(const cui__dk_build *b, uint32_t uid) { return cui__hash(&uid, sizeof uid, b->seed ^ 0x1EAFu); }
static cui_id cui__dk_tab_id(cui_ctx *ui, uint32_t uid, const char *label) {
    cui_push_id(ui, cui__frame_printf(ui, "t%u", (unsigned)uid));
    cui_id id = cui_make_id(ui, label);
    cui_pop_id(ui);
    return id;
}
/* smallest size along one axis that the visible subtree needs */
static float cui__dk_min(const cui__dk_build *b, int i, bool vertical) {
    i = cui__dk_shown(b, i);
    const cui__dk_node *n = &b->st->nodes[i];
    if (n->kind != DK_SPLIT) return vertical ? b->min_h : b->min_w;
    float ma = cui__dk_min(b, n->a, vertical), mb = cui__dk_min(b, n->b, vertical);
    return n->vertical == vertical ? ma + mb + 1 : CUI_MAX(ma, mb);
}

static void cui__dk_reorder(cui__dk_state *st, int leaf, const int *vis, int vn, int from, int to) {
    if (from < 0 || to < 0 || from >= vn || to >= vn || from == to) return;
    int p = vis[from], before = -1;
    if (to < from) before = vis[to];
    else if (to + 1 < vn) before = vis[to + 1];
    else {
        /* past the last visible tab: right after it */
        int all[CUI__DK_PANELS];
        int n = cui__dk_tabs(st, leaf, all, p);
        for (int k = 0; k < n; k++) if (all[k] == vis[to]) { before = k + 1 < n ? all[k + 1] : -1; break; }
    }
    cui__dk_insert(st, p, leaf, before);
}

static void cui__dk_build_leaf(cui_ctx *ui, cui__dk_build *b, int i) {
    cui__dk_state *st = b->st;
    cui__dk_node *n = &st->nodes[i];
    int vis[CUI__DK_PANELS];
    int vn = cui__dk_visible_tabs(st, i, vis, -1);
    int sel = 0;
    for (int k = 0; k < vn; k++) if (vis[k] == n->sel) sel = k;
    if (vn > 0) n->sel = vis[sel];
    cui_id lid = cui__dk_leaf_id(b, n->uid);
    int ln = cui__node_new(ui, lid);
    cui_node *L = &ui->nodes[ln];
    L->dir = CUI_COLUMN;
    L->w = CUI_FILL; L->h = CUI_FILL;
    L->self_align = CUI_ALIGN_STRETCH;
    L->bg = CUI_C(ui, SURFACE_MUTED);
    L->flags |= CUI_NF_CLIP;
    cui__open(ui, ln);
    int sw = cui__node_new(ui, lid ^ 0x5717u);
    ui->nodes[sw].dir = CUI_COLUMN;
    ui->nodes[sw].w = CUI_FILL;
    ui->nodes[sw].self_align = CUI_ALIGN_STRETCH;
    cui__open(ui, sw);
    cui_doc_tab *tabs = cui__frame_alloc(ui, sizeof(cui_doc_tab) * (size_t)(vn > 0 ? vn : 1));
    bool closable = false;
    for (int k = 0; k < vn; k++) {
        cui__dk_panel *p = &st->panels[vis[k]];
        tabs[k].title = p->label;
        tabs[k].icon = p->icon[0] ? p->icon : NULL;
        tabs[k].tooltip = NULL;
        tabs[k].dirty = p->dirty;
        closable = closable || p->closable;
        b->tab_vis[vis[k]] = true;
        b->tab_id[vis[k]] = cui__dk_tab_id(ui, n->uid, p->label);
        if (st->refocus == vis[k]) { ui->focus = b->tab_id[vis[k]]; st->refocus = -1; }   /* a moved tab keeps the keyboard */
    }
    cui_doc_tabs_opts o;
    memset(&o, 0, sizeof o);
    o.shortcuts = b->shortcuts && st->cur_leaf == i && st->drag != DK_DRAG;
    o.no_close = !closable;
    bool fake = b->takeover == i, held = ui->mdown[0];
    if (fake) ui->mdown[0] = false;   /* the strip's own drag lets go: the dock drag has it */
    cui_doc_tabs_result r = cui_doc_tabs_ex(ui, cui__frame_printf(ui, "t%u", (unsigned)n->uid), tabs, vn, &sel, &o);
    if (fake) ui->mdown[0] = held;
    cui__close(ui);
    if (!fake && vn > 0) {
        if (r.close >= 0 && r.close < vn) {
            cui__dk_panel *p = &st->panels[vis[r.close]];
            if (p->closable) p->close_req = true;
        }
        if (r.move_from >= 0 && r.move_to >= 0) {
            cui__dk_reorder(st, i, vis, vn, r.move_from, r.move_to);
            vn = cui__dk_visible_tabs(st, i, vis, -1);
        }
        if (sel >= 0 && sel < vn) n->sel = vis[sel];
    }
    b->shown[i] = vn > 0 ? n->sel : -1;   /* a click shows its panel at once */
    int body = cui__node_new(ui, lid ^ 0xB0D1u);
    cui_node *B = &ui->nodes[body];
    B->dir = CUI_COLUMN;
    B->w = CUI_FILL; B->h = CUI_FILL;
    B->self_align = CUI_ALIGN_STRETCH;
    B->bg = CUI_C(ui, SURFACE);
    b->body[i] = body;
    cui__close(ui);
}

static void cui__dk_build_node(cui_ctx *ui, cui__dk_build *b, int i) {
    cui__dk_state *st = b->st;
    i = cui__dk_shown(b, i);
    if (i < 0) return;
    if (st->nodes[i].kind == DK_LEAF) { cui__dk_build_leaf(ui, b, i); return; }
    cui__dk_node *n = &st->nodes[i];
    const char *sid = cui__frame_printf(ui, "s%u", (unsigned)n->uid);
    cui_rect pr = cui__prev_rect(ui, cui_make_id(ui, sid));
    float total = n->vertical ? pr.h : pr.w;
    float ma = cui__dk_min(b, n->a, n->vertical), mb = cui__dk_min(b, n->b, n->vertical);
    float lo = 0.02f, hi = 0.98f;
    if (total > 2) {
        float avail = total - 1;
        if (ma + mb > avail) { float k = avail / (ma + mb); ma *= k; mb *= k; }   /* too narrow: share what there is */
        lo = ma / total; hi = 1.0f - mb / total;
        if (lo > hi) lo = hi = 0.5f;
    }
    float *r = cui__frame_alloc(ui, sizeof(float));
    *r = cui__clampf(n->ratio, lo, hi);
    float shown = *r;
    cui_split_opts so;
    memset(&so, 0, sizeof so);
    so.vertical = n->vertical;
    so.min_first = CUI_MAX(1.0f, ma);
    so.min_second = CUI_MAX(1.0f, mb);
    cui_split_begin(ui, sid, r, &so);
    if (*r != shown) n->ratio = cui__clampf(*r, CUI_MAX(lo, 0.01f), CUI_MIN(hi, 0.99f));   /* the divider moved */
    /* the split view gives its panes these minimums; here they come from the
     * last frame's size instead, so the dockspace never pushes its parent wider */
    ui->nodes[cui__current(ui)].min_w = ui->nodes[cui__current(ui)].min_h = 0;
    cui__dk_build_node(ui, b, n->a);
    cui_split_next(ui);
    ui->nodes[cui__current(ui)].min_w = ui->nodes[cui__current(ui)].min_h = 0;
    cui__dk_build_node(ui, b, n->b);
    cui_split_end(ui);
}

/* where a drop at the pointer would land */
static void cui__dk_tiles_for_leaf(cui_rect C, cui_rect *t, int *nt) {
    float s = CUI_MIN(CUI__DK_TILE, (CUI_MIN(C.w, C.h) - 24) / 3.4f);
    *nt = 0;
    if (s < 20) return;
    float cx = C.x + C.w * 0.5f, cy = C.y + C.h * 0.5f, d = s + 6;
    float ox[5] = { 0, -d, d, 0, 0 }, oy[5] = { 0, 0, 0, -d, d };
    for (int k = 0; k < 5; k++) t[k] = (cui_rect){ roundf(cx + ox[k] - s * 0.5f), roundf(cy + oy[k] - s * 0.5f), s, s };
    *nt = 5;
}
static int cui__dk_outer_tiles(cui_rect R, cui_rect *t) {
    float s = 28;
    if (R.w < 4 * s || R.h < 4 * s) return 0;
    float cx = R.x + R.w * 0.5f, cy = R.y + R.h * 0.5f, m = 8;
    t[0] = (cui_rect){ roundf(R.x + m), roundf(cy - s * 0.5f), s, s };
    t[1] = (cui_rect){ roundf(R.x + R.w - m - s), roundf(cy - s * 0.5f), s, s };
    t[2] = (cui_rect){ roundf(cx - s * 0.5f), roundf(R.y + m), s, s };
    t[3] = (cui_rect){ roundf(cx - s * 0.5f), roundf(R.y + R.h - m - s), s, s };
    return 4;
}
static float cui__dk_outer_share(const cui__dk_build *b, cui_rect R, int zone) {
    bool v = zone == DK_TOP || zone == DK_BOTTOM;
    float total = v ? R.h : R.w, mn = v ? b->min_h : b->min_w;
    return total > 0 ? cui__clampf(CUI_MAX(0.25f, mn / total), 0.1f, 0.5f) : 0.25f;
}
static cui_rect cui__dk_part(cui_rect r, int zone, float share) {
    switch (zone) {
    case DK_LEFT: r.w *= share; break;
    case DK_RIGHT: r.x += r.w * (1 - share); r.w *= share; break;
    case DK_TOP: r.h *= share; break;
    case DK_BOTTOM: r.y += r.h * (1 - share); r.h *= share; break;
    default: break;
    }
    return r;
}

static void cui__dk_target(cui_ctx *ui, cui__dk_build *b, cui_rect R, cui__dk_paint *pt, cui_rect *pv) {
    cui__dk_state *st = b->st;
    float mx = ui->mx, my = ui->my;
    st->t_kind = 0; st->t_leaf = -1; st->t_zone = DK_NONE; st->t_index = -1;
    pt->ntiles = 0; pt->hot = -1; pt->marker = false;
    int src = st->drag_leaf, P = st->drag_panel;
    int tmp[CUI__DK_PANELS];
    bool lone = cui__dk_visible_tabs(st, src, tmp, -1) <= 1;   /* the panel is its leaf's only tab */
    bool outer_ok = !(lone && cui__dk_shown(b, st->root) == src);
    cui_rect ot[4];
    int no = outer_ok ? cui__dk_outer_tiles(R, ot) : 0;
    static const uint8_t oz[4] = { DK_LEFT, DK_RIGHT, DK_TOP, DK_BOTTOM };
    for (int k = 0; k < no; k++) { pt->tiles[pt->ntiles] = ot[k]; pt->zone[pt->ntiles] = oz[k]; pt->outer[pt->ntiles] = true; pt->ntiles++; }
    if (!cui__rect_contains(R, mx, my)) return;
    for (int k = 0; k < no; k++)
        if (cui__rect_contains(cui__rect_inset(ot[k], -4), mx, my)) {
            st->t_kind = 2; st->t_zone = oz[k]; pt->hot = k;
            *pv = cui__dk_part(R, oz[k], cui__dk_outer_share(b, R, oz[k]));
            return;
        }
    int leaf = -1;
    for (int i = 0; i < CUI__DK_NODES; i++)
        if (st->nodes[i].kind == DK_LEAF && b->vis[i] && cui__rect_contains(b->leaf_rect[i], mx, my)) { leaf = i; break; }
    if (leaf < 0) return;
    if (leaf == src && lone) return;   /* onto its own single tab leaf: nothing would change */
    cui_rect Lr = b->leaf_rect[leaf], S = b->strip_rect[leaf];
    cui_rect C = { Lr.x, S.y + S.h, Lr.w, CUI_MAX(0, Lr.y + Lr.h - (S.y + S.h)) };
    cui_rect lt[5];
    int nl;
    cui__dk_tiles_for_leaf(C, lt, &nl);
    static const uint8_t lz[5] = { DK_CENTER, DK_LEFT, DK_RIGHT, DK_TOP, DK_BOTTOM };
    int base = pt->ntiles;
    for (int k = 0; k < nl; k++) {
        if (k == 0 && leaf == src) continue;   /* its own leaf: only the sides do something */
        pt->tiles[pt->ntiles] = lt[k]; pt->zone[pt->ntiles] = lz[k]; pt->outer[pt->ntiles] = false; pt->ntiles++;
    }
    st->t_kind = 1; st->t_leaf = leaf;
    if (cui__rect_contains(S, mx, my)) {
        /* over the strip: a tab at the slot under the pointer */
        int vis[CUI__DK_PANELS];
        int vn = cui__dk_visible_tabs(st, leaf, vis, P), at = vn;
        float edge = S.x + 6;
        for (int k = 0; k < vn; k++) {
            cui_rect tr = cui__prev_rect(ui, b->tab_id[vis[k]]);
            if (tr.w <= 0) continue;
            if (mx < tr.x + tr.w * 0.5f) { at = k; edge = tr.x - 1; break; }
            edge = tr.x + tr.w + 1;
        }
        st->t_zone = DK_CENTER;
        st->t_index = at < vn ? vis[at] : -1;
        pt->marker = true;
        pt->mk = (cui_rect){ roundf(edge - 1), S.y + 6, 2, CUI_MAX(0, S.h - 10) };
        *pv = Lr;
        return;
    }
    int zone = DK_NONE;
    for (int k = 0; k < nl; k++) if (cui__rect_contains(cui__rect_inset(lt[k], -3), mx, my)) zone = lz[k];
    if (zone == DK_NONE && C.w > 0 && C.h > 0) {
        float u = (mx - C.x) / C.w, v = (my - C.y) / C.h;
        float d[4] = { u, 1 - u, v, 1 - v };
        int best = 0;
        for (int k = 1; k < 4; k++) if (d[k] < d[best]) best = k;
        zone = d[best] >= 0.28f ? DK_CENTER : (int)oz[best];
    }
    for (int k = base; k < pt->ntiles; k++) if (pt->zone[k] == zone) pt->hot = k;
    if (zone == DK_CENTER && leaf == src) {   /* back onto its own leaf: stays where it is */
        st->t_kind = 0; st->t_leaf = -1; pt->hot = -1;
        return;
    }
    st->t_zone = zone;
    *pv = zone == DK_CENTER ? Lr : cui__dk_part(Lr, zone, 0.5f);
}

static void cui__dk_drop(cui_ctx *ui, cui__dk_build *b, cui_rect R) {
    cui__dk_state *st = b->st;
    int P = st->drag_panel;
    if (st->t_kind == 0 || P < 0 || !st->panels[P].used) return;
    if (ui->focus && ui->focus == b->tab_id[P]) st->refocus = P;
    if (st->t_kind == 1 && st->t_zone == DK_CENTER) {
        int leaf = st->t_leaf;
        if (leaf != st->panels[P].leaf) cui__dk_detach(st, P);
        if (st->nodes[leaf].kind != DK_LEAF) return;
        cui__dk_insert(st, P, leaf, st->t_index);
        st->nodes[leaf].sel = P;
        st->cur_leaf = leaf;
        return;
    }
    if (cui__dk_free_nodes(st) < 2) return;   /* no room for another split */
    int target = st->t_kind == 1 ? st->t_leaf : -1;
    float share = st->t_kind == 2 ? cui__dk_outer_share(b, R, st->t_zone) : 0.5f;
    cui__dk_detach(st, P);
    if (st->t_kind == 2) target = st->root;
    int nl;
    if (target < 0) {   /* the tree went empty: the panel is the whole layout */
        nl = cui__dk_alloc(st, DK_LEAF);
        st->root = nl;
    } else {
        nl = cui__dk_split(st, target, st->t_zone, share);
    }
    if (nl < 0) return;
    cui__dk_insert(st, P, nl, -1);
    st->nodes[nl].sel = P;
    st->cur_leaf = nl;
}

/* panels of the visible leaves, in tree order */
static void cui__dk_collect(const cui__dk_build *b, int i, int *out, int *n) {
    const cui__dk_state *st = b->st;
    if (i < 0 || !b->vis[i]) return;
    if (st->nodes[i].kind == DK_SPLIT) { cui__dk_collect(b, st->nodes[i].a, out, n); cui__dk_collect(b, st->nodes[i].b, out, n); return; }
    *n += cui__dk_visible_tabs(st, i, out + *n, -1);
}

/* Too narrow for side by side panes (a phone): one strip with every panel as a tab and one body.
   The tree is left as it is, so the panes come back when there is room again. */
static void cui__dk_build_compact(cui_ctx *ui, cui__dk_build *b) {
    cui__dk_state *st = b->st;
    int list[CUI__DK_PANELS], vn = 0;
    cui__dk_collect(b, st->root, list, &vn);
    int sel = -1;
    for (int k = 0; k < vn; k++) if (list[k] == st->compact_sel) sel = k;
    if (sel < 0 && st->cur_leaf >= 0)
        for (int k = 0; k < vn; k++) if (list[k] == st->nodes[st->cur_leaf].sel) sel = k;
    if (sel < 0) sel = 0;
    cui_id lid = b->id ^ 0xC0A7u;
    int ln = cui__node_new(ui, lid);
    cui_node *L = &ui->nodes[ln];
    L->dir = CUI_COLUMN; L->w = CUI_FILL; L->h = CUI_FILL; L->self_align = CUI_ALIGN_STRETCH;
    L->bg = CUI_C(ui, SURFACE_MUTED); L->flags |= CUI_NF_CLIP;
    cui__open(ui, ln);
    cui_doc_tab *tabs = cui__frame_alloc(ui, sizeof(cui_doc_tab) * (size_t)(vn > 0 ? vn : 1));
    bool closable = false;
    for (int k = 0; k < vn; k++) {
        cui__dk_panel *p = &st->panels[list[k]];
        tabs[k].title = p->label; tabs[k].icon = p->icon[0] ? p->icon : NULL; tabs[k].tooltip = NULL; tabs[k].dirty = p->dirty;
        closable = closable || p->closable;
        b->tab_vis[list[k]] = true;
        b->tab_id[list[k]] = cui__dk_tab_id(ui, 0xC0A7u, p->label);
    }
    cui_doc_tabs_opts o;
    memset(&o, 0, sizeof o);
    o.shortcuts = b->shortcuts; o.no_close = !closable; o.no_reorder = true;
    cui_doc_tabs_result r = cui_doc_tabs_ex(ui, cui__frame_printf(ui, "t%u", 0xC0A7u), tabs, vn, &sel, &o);   /* the ids cui__dk_tab_id gives */
    if (vn > 0 && r.close >= 0 && r.close < vn && st->panels[list[r.close]].closable) st->panels[list[r.close]].close_req = true;
    int body = cui__node_new(ui, lid ^ 0xB0D1u);
    cui_node *B = &ui->nodes[body];
    B->dir = CUI_COLUMN; B->w = CUI_FILL; B->h = CUI_FILL; B->self_align = CUI_ALIGN_STRETCH;
    B->bg = CUI_C(ui, SURFACE);
    cui__close(ui);
    if (vn <= 0) return;
    sel = CUI_CLAMP(sel, 0, vn - 1);
    int pi = list[sel], leaf = st->panels[pi].leaf;
    st->compact_sel = pi;
    for (int k = 0; k < vn; k++) b->body[st->panels[list[k]].leaf] = body;
    if (leaf >= 0) { b->shown[leaf] = pi; st->nodes[leaf].sel = pi; st->cur_leaf = leaf; }
}

void cui_dockspace_begin_ex(cui_ctx *ui, const char *id_str, const cui_dockspace_opts *o) {
    cui_dockspace_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!id_str) id_str = "dock";
    cui__dk_reg *reg = cui__dk_registry(ui);
    cui__dk_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    cui__dk_state *st = cui__dk_state_of(ui, id_str);
    if (!st->init) {
        cui__dk_init(st);
        if (o->layout) cui__dk_load(ui, st, o->layout);
    }
    b->st = st;
    b->frame = ui->frame;
    b->gen = st->gen;
    b->id = cui_make_id(ui, id_str);
    b->min_w = o->min_panel_width > 0 ? o->min_panel_width : 120;
    b->min_h = o->min_panel_height > 0 ? o->min_panel_height : 96;
    b->shortcuts = o->shortcuts;
    b->takeover = -1;
    b->prev = cui__dk_cur(ui);
    for (int i = 0; i < CUI__DK_NODES; i++) { b->body[i] = -1; b->shown[i] = -1; }
    cui_push_id(ui, id_str);
    b->seed = ui->id_stack[ui->id_depth > 0 ? ui->id_depth - 1 : 0];
    cui_rect R = cui__prev_rect(ui, b->id);
    b->compact = R.w > 0 && R.w < 2 * b->min_w + 180;
    if (b->compact && st->drag != DK_IDLE) st->drag = DK_IDLE;   /* nowhere to drop a pane */
    if (st->drag_panel >= 0 && st->drag_panel < CUI__DK_PANELS && st->drag != DK_IDLE && !cui__dk_present(st, &st->panels[st->drag_panel]))
        st->drag = DK_IDLE;   /* the dragged panel went away */

    /* last frame's geometry */
    cui__dk_compute_vis(b);
    for (int i = 0; i < CUI__DK_NODES; i++) {
        if (st->nodes[i].kind != DK_LEAF || !b->vis[i]) continue;
        cui_id lid = cui__dk_leaf_id(b, st->nodes[i].uid);
        b->leaf_rect[i] = cui__prev_rect(ui, lid);
        b->strip_rect[i] = cui__prev_rect(ui, lid ^ 0x5717u);
        if (b->strip_rect[i].h <= 0) b->strip_rect[i] = (cui_rect){ b->leaf_rect[i].x, b->leaf_rect[i].y, b->leaf_rect[i].w, CUI_MIN(CUI__DK_STRIP, b->leaf_rect[i].h) };
    }
    /* the leaf the keyboard is in: the focused control's, else the last press */
    cui_rect fr = ui->focus ? cui__prev_rect(ui, ui->focus) : (cui_rect){ 0, 0, 0, 0 };
    for (int i = 0; i < CUI__DK_NODES; i++) {
        if (st->nodes[i].kind != DK_LEAF || !b->vis[i]) continue;
        cui_rect lr = b->leaf_rect[i];
        if (ui->mpressed[0] && cui__rect_contains(lr, ui->mx, ui->my)) st->cur_leaf = i;
        if (fr.w > 0 && cui__rect_contains(lr, fr.x + fr.w * 0.5f, fr.y + fr.h * 0.5f) && !ui->mpressed[0]) st->cur_leaf = i;
        int vis[CUI__DK_PANELS];
        int vn = cui__dk_visible_tabs(st, i, vis, -1);
        for (int k = 0; k < vn; k++) {
            cui__dk_panel *p = &st->panels[vis[k]];
            cui_id tid = cui__dk_tab_id(ui, st->nodes[i].uid, p->label);
            b->tab_id[vis[k]] = tid;
            /* a press on a tab may become a dock drag */
            if (ui->mpressed[0] && st->drag == DK_IDLE && ui->hot == tid && !b->compact) {
                cui_rect tr = cui__prev_rect(ui, tid);
                st->drag = DK_PENDING;
                st->drag_panel = vis[k];
                st->drag_leaf = i;
                st->grab_x = ui->mx - tr.x; st->grab_y = ui->my - tr.y;
            }
        }
    }

    /* the dock drag */
    cui__dk_paint *pt = CUI_PAINT_DATA(ui, cui__dk_paint);
    memset(pt, 0, sizeof *pt);
    pt->hot = -1;
    b->paint = pt;
    if (st->drag == DK_PENDING) {
        if (!ui->mdown[0]) st->drag = DK_IDLE;
        else {
            cui_rect s = cui__rect_inset(b->strip_rect[st->drag_leaf], -8);
            if (!cui__rect_contains(s, ui->mx, ui->my)) {
                st->drag = DK_DRAG;
                b->takeover = st->drag_leaf;
                st->t_kind = 0;
                st->pv_alpha = 0;
                st->tiles_alpha = 0;
            }
        }
    }
    if (st->drag == DK_DRAG && cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
        st->drag = DK_CANCELLED;
        ui->kpressed[CUI_KEY_ESCAPE] = 0;
    }
    if (st->drag == DK_CANCELLED && !ui->mdown[0]) st->drag = DK_IDLE;
    bool dropped = false;
    if (st->drag == DK_DRAG) {
        cui_rect pv = { 0, 0, 0, 0 };
        cui__dk_target(ui, b, R, pt, &pv);
        if (!ui->mdown[0]) {
            cui__dk_drop(ui, b, R);
            st->drag = DK_IDLE;
            dropped = true;
        } else {
            ui->active = b->id ^ 0xD4A6u;   /* the pointer is ours until the drop */
            cui__set_cursor(ui, CUI_CURSOR_MOVE);
            bool has = st->t_kind != 0;
            if (has && st->pv_alpha <= 0.01f) { st->pv[0] = pv.x; st->pv[1] = pv.y; st->pv[2] = pv.w; st->pv[3] = pv.h; }
            if (has) {
                float tv[4] = { pv.x, pv.y, pv.w, pv.h };
                for (int k = 0; k < 4; k++) cui__tween(ui, &st->pv[k], tv[k], CUI__T_STATE);
            }
            cui__tween(ui, &st->pv_alpha, has ? 1.0f : 0.0f, CUI__T_HOVER);
            cui__tween(ui, &st->tiles_alpha, 1.0f, CUI__T_HOVER);
            pt->alpha = st->tiles_alpha;
            pt->pv_alpha = st->pv_alpha;
            pt->pv = (cui_rect){ st->pv[0], st->pv[1], st->pv[2], st->pv[3] };
        }
    }
    cui__spring(ui, &st->lift, st->drag == DK_DRAG ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);
    if (dropped) { cui__dk_compute_vis(b); ui->animating = true; }

    /* the tree */
    int c = cui__node_new(ui, b->id);
    cui_node *cn = &ui->nodes[c];
    cn->dir = CUI_COLUMN;
    cn->w = o->width.kind ? o->width : CUI_FILL;
    cn->h = o->height.kind ? o->height : CUI_FILL;
    cn->self_align = CUI_ALIGN_STRETCH;
    b->container = c;
    cui__open(ui, c);
    /* a dockspace squeezed to nothing shows nothing (a strip with no width has no tabs to show) */
    bool room = !cui__has_prev_rect(ui, b->id) || (R.w >= 16 && R.h >= 16);
    if (room && st->root >= 0 && b->vis[st->root]) {
        if (b->compact) cui__dk_build_compact(ui, b);
        else cui__dk_build_node(ui, b, st->root);
    }
    st->built = ui->frame;
    reg->cur = b;
}

/* a new or unplaced panel goes into the largest leaf on show */
static void cui__dk_place(cui__dk_build *b, int p) {
    cui__dk_state *st = b->st;
    int best = -1;
    float area = -1;
    for (int i = 0; i < CUI__DK_NODES; i++) {
        if (st->nodes[i].kind != DK_LEAF || !b->vis[i]) continue;
        float a = b->leaf_rect[i].w * b->leaf_rect[i].h;
        if (a > area) { area = a; best = i; }
    }
    if (best < 0) best = cui__dk_first_leaf(st, st->root);
    if (best < 0) {
        best = cui__dk_alloc(st, DK_LEAF);
        if (best < 0) return;
        st->root = best;
    }
    cui__dk_insert(st, p, best, -1);
}

bool cui_dock_panel_begin_ex(cui_ctx *ui, const char *title, bool *open, const cui_dock_panel_opts *o) {
    cui_dock_panel_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui__dk_build *b = cui__dk_cur(ui);
    if (!b || !title || b->in_panel) return false;
    ui->last_id = 0;
    cui__dk_state *st = b->st;
    char name[CUI__DK_NAME];
    cui__dk_name_of(title, name);
    if (!name[0]) return false;
    int pi = cui__dk_find(st, name);
    if (pi < 0) pi = cui__dk_new_panel(st, name);
    if (pi < 0) return false;   /* full */
    cui__dk_panel *p = &st->panels[pi];
    if (p->known && p->seen == ui->frame) return false;   /* submitted twice this frame */
    snprintf(p->label, sizeof p->label, "%s", title);
    snprintf(p->icon, sizeof p->icon, "%s", o->icon ? o->icon : "");
    p->dirty = o->dirty;
    p->closable = open != NULL;
    if (p->pending >= 0 && open) *open = p->pending != 0;
    p->pending = -1;
    if (p->close_req) { p->close_req = false; if (open) *open = false; }
    bool is_open = open ? *open : true;
    bool reopened = p->known && !p->open && is_open;
    p->known = true;
    p->open = is_open;
    p->seen = ui->frame;
    if (!is_open) return false;
    if (p->leaf < 0) {
        /* wait one frame while a fresh layout has nothing on screen yet, so
         * "largest" means something; then place it regardless */
        bool any = false;
        for (int i = 0; i < CUI__DK_NODES && !any; i++) any = b->vis[i];
        if (any || p->deferred || cui__dk_first_leaf(st, st->root) < 0) {
            cui__dk_place(b, pi);
            if (p->leaf >= 0 && (reopened || st->nodes[p->leaf].sel < 0)) st->nodes[p->leaf].sel = pi;
        }
        p->deferred = true;
        ui->animating = true;
        return false;
    }
    if (reopened) st->nodes[p->leaf].sel = pi;
    if (!b->tab_vis[pi] || b->gen != st->gen) { ui->animating = true; return false; }
    ui->last_id = b->tab_id[pi];
    int leaf = p->leaf, body = b->body[leaf];
    if (body < 0 || b->shown[leaf] != pi) return false;
    cui__open(ui, body);
    int w = cui__node_new(ui, cui__hash_str(name, b->seed ^ 0x9A4E1u));
    cui_node *n = &ui->nodes[w];
    /* fills the body exactly, whatever its content would like: the leaf's size comes from the splits */
    n->dir = CUI_COLUMN;
    n->w = CUI_FIT; n->pref_hint = 1;
    n->h = CUI_PCT(100);
    n->self_align = CUI_ALIGN_STRETCH;
    n->flags |= CUI_NF_CLIP;
    cui__open(ui, w);
    cui_push_id(ui, title);
    b->in_panel = true;
    ui->last_id = b->tab_id[pi];
    return true;
}

void cui_dock_panel_end(cui_ctx *ui) {
    cui__dk_build *b = cui__dk_cur(ui);
    if (!b || !b->in_panel) return;
    cui_pop_id(ui);
    cui__close(ui);   /* the panel */
    cui__close(ui);   /* the leaf body */
    b->in_panel = false;
}

static void cui__dk_glyph(cui_ctx *ui, cui_rect t, int zone, bool outer, bool hot, float a) {
    cui_rect g = { roundf(t.x + t.w * 0.5f - 8), roundf(t.y + t.h * 0.5f - 7), 16, 14 };
    cui_color edge = cui_alpha(CUI_C(ui, TEXT_MUTED), a);
    cui_color fill = hot ? cui_alpha(CUI_C(ui, ACCENT), a) : cui_alpha(CUI_C(ui, TEXT_SECONDARY), a * 0.55f);
    cui_draw_rect_border(ui, g, CUI_CORNERS(3), cui__rgba(0, 0, 0, 0), edge, 1);
    cui_rect in = cui__rect_inset(g, 2.5f);
    if (zone != DK_CENTER) in = cui__dk_part(in, zone, outer ? 0.34f : 0.5f);
    cui_draw_rect(ui, in, CUI_CORNERS(1.5f), fill);
}
static void cui__dk_paint_overlay(cui_ctx *ui, cui_node *n, void *data) {
    (void)n;
    cui__dk_paint *p = data;
    if (p->pv_alpha > 0.01f && p->pv.w > 0 && p->pv.h > 0) {
        cui_rect r = cui__rect_inset(p->pv, 3);
        cui_draw_rect_border(ui, r, CUI_CORNERS(CUI__R_ITEM(ui)), cui_alpha(CUI_C(ui, ACCENT), 0.12f * p->pv_alpha),
                             cui_alpha(CUI_C(ui, ACCENT), 0.55f * p->pv_alpha), 1);
    }
    if (p->marker && p->pv_alpha > 0.01f) cui_draw_rect(ui, p->mk, CUI_CORNERS(1), cui_alpha(CUI_C(ui, ACCENT), p->pv_alpha));
    if (p->alpha <= 0.01f) return;
    for (int k = 0; k < p->ntiles; k++) {
        cui_rect t = p->tiles[k];
        bool hot = k == p->hot;
        cui_corners c = CUI_CORNERS(8);
        cui__draw_elevation(ui, t, c, CUI_ELEVATION_FLOATING, p->alpha * 0.6f);
        cui_draw_rect_border(ui, t, c, cui_alpha(CUI_C(ui, SURFACE_RAISED), p->alpha),
                             cui_alpha(hot ? CUI_C(ui, ACCENT) : CUI_C(ui, BORDER), p->alpha), 1);
        cui__dk_glyph(ui, t, p->zone[k], p->outer[k], hot, p->alpha);
    }
}

void cui_dockspace_end(cui_ctx *ui) {
    cui__dk_build *b = cui__dk_cur(ui);
    if (!b) return;
    if (b->in_panel) cui_dock_panel_end(ui);
    cui__dk_state *st = b->st;
    cui_node *cn = &ui->nodes[b->container];
    cn->paint = cui__dk_paint_overlay;
    cn->paint_data = b->paint;
    cn->flags |= CUI_NF_PAINT_AFTER;
    /* the ghost: the dragged tab, following the pointer */
    if (st->drag == DK_DRAG && st->drag_panel >= 0) {
        cui__dk_panel *p = &st->panels[st->drag_panel];
        int g = cui__float_begin(ui, b->id ^ 0x6057u, 1900, 0, CUI__PLACE_POINT, 0);
        cui_node *n = &ui->nodes[g];
        float rad = CUI__R_ITEM(ui);
        n->fx = ui->mx - st->grab_x;
        n->fy = ui->my - st->grab_y;
        n->dir = CUI_ROW; n->align = CUI_ALIGN_CENTER; n->gap = 8;
        n->h = CUI_PX(32);
        n->pad[1] = n->pad[3] = 12;
        n->radius = CUI_CORNERS(rad);
        n->bg = CUI_C(ui, SURFACE_RAISED);
        n->border = 1;
        n->border_color = CUI_C(ui, BORDER);
        n->elevation = CUI_ELEVATION_FLOATING;
        n->elevation_alpha = st->lift.x;
        n->opacity = CUI_MAX(0.001f, CUI_MIN(1.0f, st->lift.x)) * 0.96f;
        if (p->icon[0]) cui__icon_node(ui, p->icon, 14, CUI_C(ui, FOREGROUND));
        int t = cui__text_node(ui, p->label, cui__display_len(p->label), CUI_FONT_BODY, cui__fsz(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = 1.3f;
        ui->nodes[t].max_w = 200;
        cui__float_end(ui);
    }
    cui_pop_id(ui);
    cui__close(ui);   /* the container */
    cui__dk_registry(ui)->cur = b->prev;
    ui->last_id = b->id;
    ui->last_node = b->container;
}
