/* Code editor: editable, highlighted code for tool builders (shader editors,
 * script consoles, config files). It runs on the text fields' engine
 * (cui_textedit.c: caret, undo history, insertion, word motion) and the code
 * block's highlighter (cui_code.c). Lines sit on a monospace grid without
 * wrapping, and only the lines in view are tokenized and drawn, so files of
 * many thousands of lines stay fast. */
#include "cui_internal.h"

#define cui__fs cui__fsz

#define CUI__ED_SALT  0xED17C0DEu
#define CUI__ED_BAR   12.0f     /* scrollbar zones along the right and bottom edges */
#define CUI__ED_INSET 6.0f      /* above the first line and below the last */
#define CUI__ED_GAP   8.0f      /* between the gutter and the code */

typedef struct {
    cui_edit_state ed;          /* first: caret, anchor and undo history, as in every text field */
    double sy;                  /* scroll, points */
    float sx;
    int pref_col;               /* column kept by up and down, -1 none */
    int drag, drag_a, drag_b;   /* pointer drag unit (1 char, 2 word, 3 line) and the unit pressed */
    int bar_drag;               /* scrollbar thumb held: 1 vertical, 2 horizontal */
    float bar_grab, bar_t;
    double last_activity;
    int mode;                   /* bar: 0 none, 1 find, 2 go to line */
    char query[128], go[24];
    bool tab_free;              /* Esc released Tab: the next one moves focus */
    bool bar_focus;             /* give the bar's field focus this frame */
    bool pend_sel, pend_goto, pend_find, init;
    int pend_a, pend_c, pend_line, pend_col;
    float focus_t;
} cui__ed_state;

/* One frame of editing: the caller's buffer and the line index. */
typedef struct {
    cui_ctx *ui;
    cui__ed_state *st;
    char *buf;
    size_t cap;
    int len;
    int *ls, nl, maxlen;        /* line starts (ls[nl] = len + 1), count, longest line in bytes */
    int tab;
    bool use_tabs, ro;
    bool changed, refused, reveal, center;
} cui__ed;

/* ---- text model ---- */

static void cui__ed_index(cui__ed *E) {
    const char *b = E->buf, *end = b + E->len, *p = b;
    int n = 1;
    while ((p = memchr(p, '\n', (size_t)(end - p))) != NULL) { n++; p++; }
    E->ls = cui__frame_alloc(E->ui, sizeof(int) * (size_t)(n + 1));
    int k = 1, maxlen = 0;
    E->ls[0] = 0;
    for (p = b; (p = memchr(p, '\n', (size_t)(end - p))) != NULL; p++) {
        int at = (int)(p - b);
        if (at - E->ls[k - 1] > maxlen) maxlen = at - E->ls[k - 1];
        E->ls[k++] = at + 1;
    }
    if (E->len - E->ls[n - 1] > maxlen) maxlen = E->len - E->ls[n - 1];
    E->ls[n] = E->len + 1;
    E->nl = n;
    E->maxlen = maxlen;
}

static int cui__ed_end(const cui__ed *E, int line) { return E->ls[line + 1] - 1; }

static int cui__ed_line_of(const cui__ed *E, int pos) {
    int lo = 0, hi = E->nl - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (E->ls[mid] <= pos) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* Column after s[a, b) when it starts at column c: tabs go to the next stop,
 * every code point is one column. */
static int cui__ed_adv(const char *s, int a, int b, int tab, int c) {
    for (int i = a; i < b; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\t') c = (c / tab + 1) * tab;
        else if ((ch & 0xC0) != 0x80) c++;
    }
    return c;
}
/* Column of byte b in the line that starts at a. */
static int cui__ed_col(const char *s, int a, int b, int tab) { return cui__ed_adv(s, a, b, tab, 0); }

/* The byte boundary in [a, e) nearest to column x. */
static int cui__ed_at_col(const char *s, int a, int e, float x, int tab) {
    int c = 0, i = a;
    while (i < e) {
        int nc = s[i] == '\t' ? (c / tab + 1) * tab : c + 1;
        if (x < (float)(c + nc) * 0.5f) break;
        c = nc;
        i = cui__utf8_next(s, e, i);
    }
    return i;
}

static int cui__ed_pos(const cui__ed *E, int line, float col) {
    line = CUI_CLAMP(line, 0, E->nl - 1);
    return cui__ed_at_col(E->buf, E->ls[line], cui__ed_end(E, line), col, E->tab);
}

static int cui__ed_caret_col(const cui__ed *E, int pos) {
    int line = cui__ed_line_of(E, pos);
    return cui__ed_col(E->buf, E->ls[line], pos, E->tab);
}

/* first character of the line that is not a space or a tab */
static int cui__ed_first_text(const cui__ed *E, int line) {
    int i = E->ls[line], e = cui__ed_end(E, line);
    while (i < e && (E->buf[i] == ' ' || E->buf[i] == '\t')) i++;
    return i;
}

/* Replace [a, b) with t. An edit that does not fit in the buffer is refused
 * whole and reported; the caret lands after the new text. */
static bool cui__ed_replace(cui__ed *E, int a, int b, const char *t, int n, int kind) {
    if (E->ro) return false;
    int m = 0;
    for (int i = 0; i < n; i++) if (t[i] != '\r') m++;
    if ((size_t)(E->len - (b - a) + m) + 1 > E->cap) { E->refused = true; return false; }
    if (m == 0 && b == a) return false;
    cui_edit_state *e = &E->st->ed;
    cui__edit_undo_push(E->ui, e, E->buf, kind);
    cui__edit_delete(E->buf, &E->len, a, b);
    int got = cui__edit_insert(E->buf, &E->len, E->cap, a, t, n);
    e->cursor = e->anchor = e->last_cursor = a + got;
    E->changed = E->reveal = true;
    E->st->pref_col = -1;
    cui__ed_index(E);
    return true;
}

static void cui__ed_move(cui__ed *E, int to, bool extend) {
    cui_edit_state *e = &E->st->ed;
    e->cursor = CUI_CLAMP(to, 0, E->len);
    if (!extend) e->anchor = e->cursor;
    E->reveal = true;
}

static int cui__ed_class(unsigned char c) { return cui__edit_is_word(c) ? 1 : (c == ' ' || c == '\t') ? 2 : 3; }

/* The word, space run or symbol at pos (double-click). */
static void cui__ed_word_at(const cui__ed *E, int pos, int *a, int *b) {
    const char *s = E->buf;
    int at = pos;
    if (at >= E->len || s[at] == '\n') at = pos - 1;
    if (at < 0 || s[at] == '\n') { *a = *b = pos; return; }
    int k = cui__ed_class((unsigned char)s[at]);
    if (k == 3) { *a = at; *b = cui__utf8_next(s, E->len, at); return; }
    int i = at, j = at;
    while (i > 0 && s[i - 1] != '\n' && cui__ed_class((unsigned char)s[i - 1]) == k) i--;
    while (j < E->len && s[j] != '\n' && cui__ed_class((unsigned char)s[j]) == k) j++;
    *a = i; *b = j;
}

/* The whole line at pos, with its newline (triple-click, the gutter). */
static void cui__ed_line_at(const cui__ed *E, int pos, int *a, int *b) {
    int line = cui__ed_line_of(E, pos);
    *a = E->ls[line];
    *b = CUI_MIN(E->ls[line + 1], E->len);
}

/* ---- brackets ---- */

static int cui__ed_partner(char c, int *dir) {
    switch (c) {
    case '(': *dir = 1; return ')';
    case '[': *dir = 1; return ']';
    case '{': *dir = 1; return '}';
    case ')': *dir = -1; return '(';
    case ']': *dir = -1; return '[';
    case '}': *dir = -1; return '{';
    default: return 0;
    }
}

static int cui__ed_match(const char *s, int len, int at) {
    int dir = 0, other = cui__ed_partner(s[at], &dir), depth = 0;
    for (int i = at; i >= 0 && i < len; i += dir) {
        if (s[i] == s[at]) depth++;
        else if (s[i] == other && --depth == 0) return i;
    }
    return -1;
}

/* The bracket beside the caret (before it first) and its partner. */
static void cui__ed_brackets(const cui__ed *E, int caret, int *a, int *b) {
    int dir;
    *a = *b = -1;
    for (int k = 0; k < 2; k++) {
        int at = k == 0 ? caret - 1 : caret;
        if (at < 0 || at >= E->len || !cui__ed_partner(E->buf[at], &dir)) continue;
        int m = cui__ed_match(E->buf, E->len, at);
        if (m >= 0) { *a = at; *b = m; return; }
    }
}

/* ---- find ---- */

static int cui__ed_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static bool cui__ed_hit(const char *s, int len, int i, const char *q, int qn) {
    if (qn <= 0 || i < 0 || i + qn > len) return false;
    for (int k = 0; k < qn; k++) if (cui__ed_lower((unsigned char)s[i + k]) != cui__ed_lower((unsigned char)q[k])) return false;
    return true;
}

/* First match at or after from (dir > 0), or last one before from (dir < 0),
 * wrapping around the ends; -1 when there is none. Case is ignored. */
static int cui__ed_find(const char *s, int len, const char *q, int qn, int from, int dir) {
    if (qn <= 0 || qn > len) return -1;
    int last = len - qn;
    if (dir > 0) {
        for (int i = CUI_MAX(0, from); i <= last; i++) if (cui__ed_hit(s, len, i, q, qn)) return i;
        for (int i = 0; i < CUI_MIN(from, last + 1); i++) if (cui__ed_hit(s, len, i, q, qn)) return i;
    } else {
        for (int i = CUI_MIN(from - 1, last); i >= 0; i--) if (cui__ed_hit(s, len, i, q, qn)) return i;
        for (int i = last; i >= CUI_MAX(0, from); i--) if (cui__ed_hit(s, len, i, q, qn)) return i;
    }
    return -1;
}

static void cui__ed_find_go(cui__ed *E, int from, int dir) {
    const char *q = E->st->query;
    int qn = (int)strlen(q);
    int at = cui__ed_find(E->buf, E->len, q, qn, from, dir);
    if (at < 0) return;
    E->st->ed.anchor = at;
    E->st->ed.cursor = at + qn;
    E->reveal = E->center = true;
    E->st->pref_col = -1;
}

/* ---- indentation ---- */

/* Tab and Shift+Tab over the selected lines (or the caret's line). */
static void cui__ed_indent(cui__ed *E, bool out) {
    cui_edit_state *e = &E->st->ed;
    int s0 = CUI_MIN(e->cursor, e->anchor), s1 = CUI_MAX(e->cursor, e->anchor);
    int l0 = cui__ed_line_of(E, s0), l1 = cui__ed_line_of(E, s1);
    if (s1 > s0 && l1 > l0 && s1 == E->ls[l1]) l1--;   /* a selection that ends at a line start leaves that line alone */
    int lines = l1 - l0 + 1, a = E->ls[l0], b = cui__ed_end(E, l1);
    int *start = cui__frame_alloc(E->ui, sizeof(int) * (size_t)lines * 2), *delta = start + lines;
    char *t = cui__frame_alloc(E->ui, (size_t)(b - a) + (size_t)lines * (size_t)E->tab + 1);
    int n = 0, total = 0;
    for (int k = 0; k < lines; k++) {
        int ls = E->ls[l0 + k], le = cui__ed_end(E, l0 + k), i = ls;
        start[k] = ls;
        delta[k] = 0;
        if (!out && le > ls) {
            if (E->use_tabs) t[n++] = '\t';
            else for (int j = 0; j < E->tab; j++) t[n++] = ' ';
            delta[k] = E->use_tabs ? 1 : E->tab;
        } else if (out) {
            if (i < le && E->buf[i] == '\t') i++;
            else while (i < le && i - ls < E->tab && E->buf[i] == ' ') i++;
            delta[k] = -(i - ls);
        }
        memcpy(t + n, E->buf + i, (size_t)(le - i));
        n += le - i;
        if (k < lines - 1) t[n++] = '\n';
        total += delta[k];
    }
    bool any = false;
    for (int k = 0; k < lines; k++) any |= delta[k] != 0;
    if (!any) return;
    int c0 = e->cursor, a0 = e->anchor;
    if (!cui__ed_replace(E, a, b, t, n, 3)) return;
    /* keep the selection on the same text */
    int map[2], src[2] = { c0, a0 };
    for (int m = 0; m < 2; m++) {
        int p = src[m];
        if (p < a) { map[m] = p; continue; }
        if (p > b) { map[m] = p + total; continue; }
        int k = lines - 1, before = 0;
        while (k > 0 && start[k] > p) k--;
        for (int j = 0; j < k; j++) before += delta[j];
        int own = out ? -CUI_MIN(-delta[k], p - start[k]) : (p > start[k] ? delta[k] : 0);
        map[m] = p + before + own;
    }
    e->cursor = e->last_cursor = map[0];
    e->anchor = map[1];
}

/* Enter: a new line at the indentation of this one, a level deeper after an
 * opening brace (C-like languages) or a colon (Python); between a pair of
 * braces the closing one moves down a line of its own. */
static void cui__ed_newline(cui__ed *E, int lang_kind) {
    cui_edit_state *e = &E->st->ed;
    int s0 = CUI_MIN(e->cursor, e->anchor), s1 = CUI_MAX(e->cursor, e->anchor);
    int line = cui__ed_line_of(E, s0), ls = E->ls[line];
    int ind_end = CUI_MIN(cui__ed_first_text(E, line), s0);
    int k = s0;
    while (k > ls && (E->buf[k - 1] == ' ' || E->buf[k - 1] == '\t')) k--;
    char before = k > ls ? E->buf[k - 1] : 0;
    bool deeper = (lang_kind == 1 && (before == '{' || before == '[' || before == '(')) || (lang_kind == 2 && before == ':');
    int j = s1;
    while (j < E->len && (E->buf[j] == ' ' || E->buf[j] == '\t')) j++;
    char after = j < E->len ? E->buf[j] : 0;
    bool split = lang_kind == 1 && ((before == '{' && after == '}') || (before == '[' && after == ']') || (before == '(' && after == ')'));
    int ind = ind_end - ls, unit = E->use_tabs ? 1 : E->tab;
    char *t = cui__frame_alloc(E->ui, (size_t)(ind * 2 + unit + 3));
    int n = 0;
    t[n++] = '\n';
    memcpy(t + n, E->buf + ls, (size_t)ind); n += ind;
    if (deeper) { if (E->use_tabs) t[n++] = '\t'; else for (int i = 0; i < unit; i++) t[n++] = ' '; }
    int caret = n;
    if (split) { t[n++] = '\n'; memcpy(t + n, E->buf + ls, (size_t)ind); n += ind; }
    if (!cui__ed_replace(E, s0, split ? j : s1, t, n, 3)) return;
    e->cursor = e->anchor = e->last_cursor = s0 + caret;
}

/* Typing: replaces the selection. A closing brace typed on a line of only
 * indentation steps back a level. */
static void cui__ed_type(cui__ed *E, const char *text, int n, int lang_kind) {
    cui_edit_state *e = &E->st->ed;
    int s0 = CUI_MIN(e->cursor, e->anchor), s1 = CUI_MAX(e->cursor, e->anchor);
    if (lang_kind == 1 && n == 1 && (text[0] == '}' || text[0] == ']' || text[0] == ')') && s0 == s1) {
        int line = cui__ed_line_of(E, s0), ls = E->ls[line];
        if (cui__ed_first_text(E, line) == s0 && s0 > ls && text[0] == '}') {
            int i = s0;
            if (E->buf[i - 1] == '\t') i--;
            else while (i > ls && s0 - i < E->tab && E->buf[i - 1] == ' ') i--;
            char *t = cui__frame_alloc(E->ui, (size_t)(i - ls) + 2);
            memcpy(t, E->buf + ls, (size_t)(i - ls));
            t[i - ls] = '}';
            cui__ed_replace(E, ls, s0, t, i - ls + 1, 1);
            return;
        }
    }
    cui__ed_replace(E, s0, s1, text, n, s1 > s0 ? 3 : 1);
}

/* Whether a block comment is open after s[i, e) when one was open before
 * (in): a quick scan that follows the tokenizer (strings end at the line
 * end; a line without a '/' cannot open one). */
static bool cui__ed_scan_line(const cui__lang *L, const char *s, int i, int e, bool in) {
    if (!in && !memchr(s + i, '/', (size_t)(e - i))) return false;
    size_t lc = L->line_comment ? strlen(L->line_comment) : 0;
    while (i < e) {
        if (in) {
            while (i < e && !(s[i] == '*' && i + 1 < e && s[i + 1] == '/')) i++;
            if (i < e) { i += 2; in = false; }
            continue;
        }
        char c = s[i];
        if (lc && (size_t)(e - i) >= lc && strncmp(s + i, L->line_comment, lc) == 0) break;
        if (c == '/' && i + 1 < e && s[i + 1] == '*') { in = true; continue; }
        if (c == '"' || c == '\'' || (c == '`' && L->backtick)) {
            i++;
            while (i < e && s[i] != c) { if (s[i] == '\\' && i + 1 < e) i++; i++; }
            if (i < e) i++;
            continue;
        }
        i++;
    }
    return in;
}

/* Block comment open at the start of line `upto`. */
static bool cui__ed_block_state(const cui__ed *E, const cui__lang *L, int upto) {
    if (!L->block_comments) return false;
    bool in = false;
    for (int line = 0; line < upto; line++) in = cui__ed_scan_line(L, E->buf, E->ls[line], cui__ed_end(E, line), in);
    return in;
}

/* ---- painting ---- */

typedef struct {
    cui_id sid;
    const char *text;           /* a copy of the lines in view; text[0] is byte `base` */
    int base, first, count, nl;
    const int *ls;              /* starts of the lines in view and the one after */
    bool in_block, json, empty, focused, show_line, ro;
    cui__lang lang;
    int tab, cursor, anchor, caret_line, caret_col;
    int br_line[2], br_col[2];
    const char *query; int qn;
    const char *placeholder;
    float size, lh, cw, gutter, view_h;
    double sy, max_sy;
    float sx, max_sx, bar_t;
    int bar_big;                /* 1 vertical thumb held or hovered, 2 horizontal */
    double caret_phase;
    cui_color colors[CUI__TK_COUNT];
    cui_color num, num_on, band, sel, match, match_edge, brk, brk_edge, caret, muted, line, bar;
    int vc0, vc1;               /* columns in view */
} cui__ed_paint;

/* Draws s[a, b) from column col; tabs expand; only the columns in view. */
static void cui__ed_run(cui_ctx *ui, const cui__ed_paint *p, const char *s, int a, int b, int col, float x0, float y, cui_color c) {
    int i = a;
    while (i < b && col < p->vc1) {
        if (s[i] == '\t') { col = (col / p->tab + 1) * p->tab; i++; continue; }
        int j = i, cj = col;
        while (j < b && s[j] != '\t') { if (((unsigned char)s[j] & 0xC0) != 0x80) cj++; j++; }
        if (cj > p->vc0) {
            int i2 = i, c2 = col;
            while (c2 < p->vc0 && i2 < j) { i2 = cui__utf8_next(s, j, i2); c2++; }
            int j2 = i2, c3 = c2;
            while (j2 < j && c3 < p->vc1) { j2 = cui__utf8_next(s, j, j2); c3++; }
            if (j2 > i2) cui__draw_text_line(ui, CUI_FONT_MONO, p->size, x0 + (float)c2 * p->cw, y, c, s + i2, j2 - i2, true);
        }
        col = cj;
        i = j;
    }
}

static void cui__ed_thumb(float track, double view, double max, double pos, float *at, float *len) {
    double total = view + max;
    *len = CUI_MAX(28.0f, track * (float)(view / total));
    *at = max > 0 ? (float)(pos / max) * (track - *len) : 0;
}

static void cui__paint_editor(cui_ctx *ui, cui_node *n, void *data) {
    cui__ed_paint *p = data;
    cui_rect r = n->rect;
    {   /* the wheel comes here while the pointer is over the editor and it can scroll */
        cui_rect clip = ui->dl.clip_stack[ui->dl.clip_depth - 1];
        float s = ui->scale;
        cui_rect cp = { clip.x / s, clip.y / s, clip.w / s, clip.h / s };
        cui_scroll_state *ss = cui_state(ui, p->sid, sizeof *ss);
        ss->view_w = r.w; ss->view_h = r.h;
        ss->content_w = r.w + p->max_sx;
        ss->content_h = p->max_sy > 0 ? r.h + 1e6f : r.h;
        cui__paint_add_hit(ui, p->sid, cui__rect_intersect(r, cp), CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y);
    }
    if (r.h > p->view_h + 0.5f) cui_request_redraw(ui);   /* laid out taller than last frame: more lines next frame */
    bool unshaped = ui->text_unshaped;
    ui->text_unshaped = true;
    float fh = cui__font_line_height(ui, CUI_FONT_MONO, p->size);
    float ty = (p->lh - fh) * 0.5f;
    float gx = r.x + p->gutter;                     /* left edge of the code */
    float x0 = gx + CUI__ED_GAP - p->sx;            /* column 0 */
    cui_rect code = { gx, r.y, CUI_MAX(0, r.w - p->gutter), r.h };
    int s0 = CUI_MIN(p->cursor, p->anchor), s1 = CUI_MAX(p->cursor, p->anchor);
    cui_push_clip(ui, r);
    /* the caret's line, across the gutter and the code */
    if (p->show_line && s0 == s1 && p->caret_line >= p->first && p->caret_line < p->first + p->count) {
        float y = r.y + CUI__ED_INSET + (float)((double)p->caret_line * p->lh - p->sy);
        cui_draw_rect(ui, (cui_rect){ r.x, y, r.w, p->lh }, CUI_CORNERS(0), p->band);
    }
    /* line numbers */
    if (p->gutter > CUI__ED_GAP) {
        for (int k = 0; k < p->count; k++) {
            int line = p->first + k;
            float y = r.y + CUI__ED_INSET + (float)((double)line * p->lh - p->sy);
            const char *num = cui__frame_printf(ui, "%d", line + 1);
            float w = cui__text_width(ui, CUI_FONT_MONO, p->size, num, -1, true);
            bool on = p->show_line && line == p->caret_line;
            cui__draw_text_line(ui, CUI_FONT_MONO, p->size, gx - 10 - w, y + ty, on ? p->num_on : p->num, num, -1, true);
        }
        if (p->sx > 0.5f) cui_draw_rect(ui, (cui_rect){ gx, r.y, 1, r.h }, CUI_CORNERS(0), p->line);   /* code scrolled under the gutter */
    }
    cui_push_clip(ui, code);
    cui__tok *toks = NULL;
    int tok_cap = 0;
    bool block = p->in_block;
    for (int k = 0; k < p->count; k++) {
        int line = p->first + k;
        int ls = p->ls[k] - p->base, le = p->ls[k + 1] - 1 - p->base;   /* in text */
        const char *s = p->text;
        float y = r.y + CUI__ED_INSET + (float)((double)line * p->lh - p->sy);
        int als = p->ls[k], ale = p->ls[k + 1] - 1;                    /* in the buffer */
        /* selection */
        if (s1 > s0 && s1 > als && s0 <= ale) {
            int a = CUI_MAX(s0, als) - p->base, b = CUI_MIN(s1, ale) - p->base;
            float xa = (float)cui__ed_col(s, ls, a, p->tab) * p->cw, xb = (float)cui__ed_col(s, ls, b, p->tab) * p->cw;
            if (s1 > ale) xb += p->cw * 0.6f;   /* the newline is selected too */
            cui_draw_rect(ui, (cui_rect){ x0 + xa, y, xb - xa, p->lh }, CUI_CORNERS(2), p->sel);
        }
        /* find matches */
        if (p->qn > 0) {
            for (int i = ls; i + p->qn <= le; i++) {
                if (!cui__ed_hit(s, le, i, p->query, p->qn)) continue;
                float xa = (float)cui__ed_col(s, ls, i, p->tab) * p->cw, xb = (float)cui__ed_col(s, ls, i + p->qn, p->tab) * p->cw;
                cui_draw_rect_border(ui, (cui_rect){ x0 + xa - 1, y + 1, xb - xa + 2, p->lh - 2 }, CUI_CORNERS(3), p->match, p->match_edge, 1);
            }
        }
        /* the bracket pair */
        for (int m = 0; m < 2; m++)
            if (p->br_line[m] == line)
                cui_draw_rect_border(ui, (cui_rect){ x0 + (float)p->br_col[m] * p->cw - 0.5f, y + 2, p->cw + 1, p->lh - 4 }, CUI_CORNERS(3),
                                     p->brk, p->brk_edge, 1);
        /* text: tokenized up to the last column in view */
        int stop = cui__ed_at_col(s, ls, le, (float)p->vc1 + 1, p->tab);
        if (stop < le) stop = cui__utf8_next(s, le, stop);
        int nn = stop - ls;
        if (nn + 1 > tok_cap) { tok_cap = nn + 1; toks = cui__frame_alloc(ui, sizeof *toks * (size_t)tok_cap); }
        bool was_block = block;
        int nt = cui__tokenize(&p->lang, p->json, s + ls, nn, &block, toks, tok_cap);
        int pos = ls, col = 0;
        for (int t = 0; t < nt; t++) {
            int a = ls + toks[t].start, b = a + toks[t].len;
            col = cui__ed_adv(s, pos, a, p->tab, col);
            int ca = col;
            col = cui__ed_adv(s, a, b, p->tab, col);
            pos = b;
            if (ca >= p->vc1) break;
            if (toks[t].kind == 0 && (s[a] == ' ' || s[a] == '\t')) continue;   /* white space */
            cui__ed_run(ui, p, s, a, b, ca, x0, y + ty, p->colors[toks[t].kind]);
        }
        /* a block comment opened past the columns in view still carries on */
        if (stop < le && p->lang.block_comments) block = cui__ed_scan_line(&p->lang, s, ls, le, was_block);
    }
    /* placeholder */
    if (p->empty && p->placeholder)
        cui__draw_text_line(ui, CUI_FONT_MONO, p->size, x0, r.y + CUI__ED_INSET + (float)(-p->sy) + ty, p->muted, p->placeholder, -1, true);
    /* caret */
    if (p->focused && p->caret_line >= p->first && p->caret_line < p->first + p->count) {
        float blink = 0.5f + 0.5f * cosf((float)p->caret_phase * 2.0f * CUI_PI / 1.06f);
        float a = (blink > 0.35f || p->caret_phase < 0.5) ? 1.0f : 0.0f;
        float y = r.y + CUI__ED_INSET + (float)((double)p->caret_line * p->lh - p->sy);
        float cx = x0 + (float)p->caret_col * p->cw;
        cui_draw_rect(ui, (cui_rect){ roundf(cx * ui->scale) / ui->scale - 0.75f, y + ty, 1.5f, fh }, CUI_CORNERS(0.75f), cui_alpha(p->caret, a));
    }
    cui_pop_clip(ui);
    /* scrollbars */
    if (p->bar_t > 0.01f) {
        if (p->max_sy > 0) {
            float at, len, w = p->bar_big == 1 ? 8 : 6;
            cui__ed_thumb(r.h - 6, r.h, p->max_sy, p->sy, &at, &len);
            cui_draw_rect(ui, (cui_rect){ r.x + r.w - w - 3, r.y + 3 + at, w, len }, CUI_CORNERS(w * 0.5f),
                          cui_alpha(p->bar, (p->bar_big == 1 ? 0.38f : 0.26f) * p->bar_t));
        }
        if (p->max_sx > 0) {
            float at, len, h = p->bar_big == 2 ? 8 : 6, track = code.w - 6 - (p->max_sy > 0 ? CUI__ED_BAR : 0);
            cui__ed_thumb(track, code.w, p->max_sx, p->sx, &at, &len);
            cui_draw_rect(ui, (cui_rect){ gx + 3 + at, r.y + r.h - h - 3, len, h }, CUI_CORNERS(h * 0.5f),
                          cui_alpha(p->bar, (p->bar_big == 2 ? 0.38f : 0.26f) * p->bar_t));
        }
    }
    cui_pop_clip(ui);
    ui->text_unshaped = unshaped;
}

/* ---- the editor ---- */

static cui__ed_state *cui__ed_get(cui_ctx *ui, const char *id_str) {
    return cui_state(ui, cui_make_id(ui, id_str) ^ CUI__ED_SALT, sizeof(cui__ed_state));
}

void cui_code_editor_goto(cui_ctx *ui, const char *id, int line, int column) {
    cui__ed_state *s = cui__ed_get(ui, id);
    s->pend_goto = true;
    s->pend_line = line; s->pend_col = column;
    ui->animating = true;
}

void cui_code_editor_select(cui_ctx *ui, const char *id, int anchor, int cursor) {
    cui__ed_state *s = cui__ed_get(ui, id);
    s->pend_sel = true;
    s->pend_a = anchor; s->pend_c = cursor;
    ui->animating = true;
}

void cui_code_editor_find(cui_ctx *ui, const char *id, const char *query) {
    cui__ed_state *s = cui__ed_get(ui, id);
    if (!query) { s->mode = 0; return; }
    snprintf(s->query, sizeof s->query, "%s", query);
    s->mode = 1;
    s->bar_focus = s->pend_find = true;
    ui->animating = true;
}

/* how Enter indents: 1 braces (C, C++, JavaScript, TypeScript, Rust, Go,
 * JSON), 2 colons (Python), 0 neither */
static int cui__ed_lang_kind(const char *lang) {
    static const char *const braces[] = { "c", "cpp", "c++", "h", "js", "ts", "javascript", "typescript", "rust", "rs", "go", "json" };
    if (!lang) return 0;
    for (int i = 0; i < CUI_COUNTOF(braces); i++) if (!strcmp(lang, braces[i])) return 1;
    return !strcmp(lang, "python") || !strcmp(lang, "py") ? 2 : 0;
}

cui_code_editor_result cui_code_editor_ex(cui_ctx *ui, const char *id_str, char *buf, size_t cap, const cui_code_editor_opts *o) {
    cui_code_editor_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_code_editor_result res;
    memset(&res, 0, sizeof res);
    res.bracket = res.bracket_match = -1;
    static char empty[1];
    if (!buf || cap == 0) { buf = empty; cap = 1; empty[0] = 0; }
    if (!memchr(buf, 0, cap)) buf[cap - 1] = 0;   /* never read past the capacity */

    cui_id id = cui_make_id(ui, id_str);
    cui_id lid = id ^ 0xED1B0D1u, sid = id ^ 0xED15C011u;
    cui__ed_state *st = cui_state(ui, id ^ CUI__ED_SALT, sizeof *st);
    cui__edit_attach(ui, id ^ CUI__ED_SALT, &st->ed);
    if (!st->init) {
        st->init = true;
        st->pref_col = -1;
        if (o->autofocus) ui->focus = lid;
    }
    cui_id fid, gid;
    cui_push_id(ui, id_str);
    fid = cui_make_id(ui, "find");
    gid = cui_make_id(ui, "goto");
    cui_pop_id(ui);

    cui__ed E;
    memset(&E, 0, sizeof E);
    E.ui = ui; E.st = st; E.buf = buf; E.cap = cap;
    E.len = (int)strlen(buf);
    E.tab = o->tab_size > 0 ? CUI_MIN(o->tab_size, 16) : 4;
    E.use_tabs = o->use_tabs;
    E.ro = o->read_only;
    cui__ed_index(&E);
    cui_edit_state *e = &st->ed;
    e->cursor = CUI_CLAMP(e->cursor, 0, E.len);
    e->anchor = CUI_CLAMP(e->anchor, 0, E.len);
    st->drag_a = CUI_CLAMP(st->drag_a, 0, E.len);   /* the text may change during a drag */
    st->drag_b = CUI_CLAMP(st->drag_b, 0, E.len);
    int lang_kind = cui__ed_lang_kind(o->lang);
    bool json = o->lang && !strcmp(o->lang, "json");
    cui__lang L = cui__lang_for(json ? NULL : o->lang);

    float size = cui__fs(ui, o->size > 0 ? o->size : 13);
    float lh = roundf(size * 1.6f);
    float cw = cui__text_width(ui, CUI_FONT_MONO, size, "0000000000000000", 16, true) / 16.0f;
    int digits = 2;
    for (int v = E.nl / 100; v > 0; v /= 10) digits++;
    float gutter = o->no_line_numbers ? CUI__ED_GAP : 12 + (float)digits * cw + 14;
    float height = o->height > 0 ? o->height : 320;

    /* ---- requests from code ---- */
    if (st->pend_sel) {
        e->anchor = CUI_CLAMP(st->pend_a, 0, E.len);
        e->cursor = CUI_CLAMP(st->pend_c, 0, E.len);
        ui->focus = lid;
        E.reveal = E.center = true;
        st->pref_col = -1;
    }
    if (st->pend_goto) {
        int line = CUI_CLAMP(st->pend_line - 1, 0, E.nl - 1);
        e->cursor = e->anchor = cui__ed_pos(&E, line, (float)CUI_MAX(0, st->pend_col - 1));
        ui->focus = lid;
        E.reveal = E.center = true;
        st->pref_col = -1;
    }
    if (st->pend_find) cui__ed_find_go(&E, CUI_MIN(e->cursor, e->anchor), 1);
    st->pend_sel = st->pend_goto = st->pend_find = false;

    cui_rect br = cui__prev_rect(ui, lid);
    bool known = cui__has_prev_rect(ui, lid) && br.w > 0;
    if (!known) { ui->animating = true; br.w = 480; br.h = height - 2 - (st->mode ? 41 : 0); }
    float code_w = CUI_MAX(1.0f, br.w - gutter - CUI__ED_GAP);
    double view_h = CUI_MAX((double)lh, (double)br.h - 2 * CUI__ED_INSET);
    double content_h = (double)E.nl * lh;
    double max_sy = CUI_MAX(0.0, content_h - view_h);

    /* ---- the find and go to line bar: open, close ---- */
    bool mine = ui->focus == lid || ui->focus == fid || ui->focus == gid;
    int mods = ui->mods;
    bool shift = (mods & CUI_MOD_SHIFT) != 0, prim = (mods & CUI_MOD_PRIMARY) != 0;
    if (mine && prim && cui_key_pressed(ui, CUI_KEY_F)) {
        int s0 = CUI_MIN(e->cursor, e->anchor), s1 = CUI_MAX(e->cursor, e->anchor);
        if (s1 > s0 && s1 - s0 < (int)sizeof st->query && !memchr(buf + s0, '\n', (size_t)(s1 - s0)))
            snprintf(st->query, sizeof st->query, "%.*s", s1 - s0, buf + s0);
        st->mode = 1;
        st->bar_focus = true;
        ui->kpressed[CUI_KEY_F] = 0;
    }
    if (mine && prim && cui_key_pressed(ui, CUI_KEY_G)) {
        st->mode = 2;
        st->go[0] = 0;
        st->bar_focus = true;
        ui->kpressed[CUI_KEY_G] = 0;
    }
    if ((ui->focus == fid || ui->focus == gid) && cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
        st->mode = 0;
        ui->focus = lid;
        ui->kpressed[CUI_KEY_ESCAPE] = 0;
    }

    /* ---- pointer ---- */
    cui_interact lr = cui__interact(ui, lid, CUI_IF_FOCUSABLE | CUI_IF_DRAG | CUI_IF_TEXT);
    float gx = br.x + gutter, x0 = gx + CUI__ED_GAP - st->sx, y0 = br.y + CUI__ED_INSET - (float)st->sy;
    bool over_vbar = known && max_sy > 0 && ui->mx >= br.x + br.w - CUI__ED_BAR;
    float max_sx = CUI_MAX(0.0f, (float)CUI_MAX(E.maxlen, 1) * cw + CUI__ED_GAP + 24 - code_w);
    bool over_hbar = known && max_sx > 0 && ui->my >= br.y + br.h - CUI__ED_BAR && ui->mx >= gx && !over_vbar;
    if (lr.hovered) cui__set_cursor(ui, (over_vbar || over_hbar || ui->mx < gx) ? CUI_CURSOR_ARROW : CUI_CURSOR_TEXT);
    if (known) {
        int line = (int)floor(((double)ui->my - y0) / lh);
        float col = (ui->mx - x0) / cw;
        if (lr.hovered && ui->mpressed[0]) {
            st->tab_free = false;
            if (over_vbar || over_hbar) {
                float at, len;
                if (over_vbar) {
                    cui__ed_thumb(br.h - 6, br.h, max_sy, st->sy, &at, &len);
                    float ty = br.y + 3 + at;
                    st->bar_grab = ui->my >= ty && ui->my <= ty + len ? ui->my - ty : len * 0.5f;
                    st->bar_drag = 1;
                } else {
                    float track = br.w - gutter - 6 - (max_sy > 0 ? CUI__ED_BAR : 0);
                    cui__ed_thumb(track, br.w - gutter, max_sx, st->sx, &at, &len);
                    float tx = gx + 3 + at;
                    st->bar_grab = ui->mx >= tx && ui->mx <= tx + len ? ui->mx - tx : len * 0.5f;
                    st->bar_drag = 2;
                }
            } else {
                int pos = cui__ed_pos(&E, line, ui->mx < gx ? 0 : col);
                int clicks = ui->click_count;
                st->drag = ui->mx < gx ? 3 : clicks >= 3 ? 3 : clicks == 2 ? 2 : 1;
                if (st->drag == 1) {
                    e->cursor = pos;
                    if (!shift) e->anchor = pos;
                    st->drag_a = st->drag_b = e->anchor;
                } else {
                    int a, b;
                    if (st->drag == 2) cui__ed_word_at(&E, pos, &a, &b); else cui__ed_line_at(&E, pos, &a, &b);
                    st->drag_a = a; st->drag_b = b;
                    e->anchor = a; e->cursor = b;
                }
                st->pref_col = -1;
                ui->caret_time = ui->time;
            }
        }
        if (st->bar_drag && lr.active && ui->mdown[0]) {
            float at, len;
            if (st->bar_drag == 1) {
                cui__ed_thumb(br.h - 6, br.h, max_sy, st->sy, &at, &len);
                float t = (ui->my - br.y - 3 - st->bar_grab) / CUI_MAX(1.0f, br.h - 6 - len);
                st->sy = (double)cui__clampf(t, 0, 1) * max_sy;
            } else {
                float track = br.w - gutter - 6 - (max_sy > 0 ? CUI__ED_BAR : 0);
                cui__ed_thumb(track, br.w - gutter, max_sx, st->sx, &at, &len);
                float t = (ui->mx - gx - 3 - st->bar_grab) / CUI_MAX(1.0f, track - len);
                st->sx = cui__clampf(t, 0, 1) * max_sx;
            }
            st->last_activity = ui->time;
        }
        if (st->drag && lr.active && ui->mdown[0] && !ui->mpressed[0]) {
            /* past an edge the view scrolls, faster further out */
            float top = br.y, bottom = br.y + br.h;
            if (ui->my < top || ui->my > bottom) {
                float dist = ui->my < top ? top - ui->my : ui->my - bottom;
                double step = (double)ui->dt * (double)lh * (8.0 + 10.0 * dist / lh);
                st->sy = CUI_MAX(0.0, CUI_MIN(max_sy, st->sy + (ui->my < top ? -step : step)));
                ui->animating = true;
            }
            if (st->drag != 3 && (ui->mx < gx || ui->mx > br.x + br.w)) {
                float dist = ui->mx < gx ? gx - ui->mx : ui->mx - (br.x + br.w);
                float step = ui->dt * cw * (12.0f + 1.0f * dist);
                st->sx = cui__clampf(st->sx + (ui->mx < gx ? -step : step), 0, max_sx);
                ui->animating = true;
            }
            y0 = br.y + CUI__ED_INSET - (float)st->sy;
            x0 = gx + CUI__ED_GAP - st->sx;
            line = (int)floor(((double)ui->my - y0) / lh);
            col = (ui->mx - x0) / cw;
            int pos = cui__ed_pos(&E, line, st->drag == 3 ? 0 : col);
            if (line < 0) pos = 0;
            if (line >= E.nl) pos = E.len;
            int prev = e->cursor;
            if (st->drag == 1) e->cursor = pos;
            else {
                int a, b;
                if (st->drag == 2) cui__ed_word_at(&E, pos, &a, &b); else cui__ed_line_at(&E, pos, &a, &b);
                if (a < st->drag_a) { e->anchor = st->drag_b; e->cursor = a; }
                else { e->anchor = st->drag_a; e->cursor = CUI_MAX(b, st->drag_b); }
            }
            if (e->cursor != prev) { ui->caret_time = ui->time; E.reveal = true; }
        }
    }
    if (!ui->mdown[0]) st->drag = st->bar_drag = 0;
    if (ui->scroll_target == sid && (ui->scroll_dy != 0 || ui->scroll_dx != 0)) {
        float dy = ui->scroll_dy, dx = ui->scroll_dx;
        if (shift && dx == 0) { dx = dy; dy = 0; }
        st->sy -= (double)dy * lh * 3;
        st->sx -= dx * 44;
        st->last_activity = ui->time;
        ui->scroll_dy = ui->scroll_dx = 0;   /* consumed */
    }

    /* ---- keys ---- */
    bool focused = ui->focus == lid;
    if (!focused) st->tab_free = false;
    if (focused) {
        ui->edit = e;   /* cui_wants_keyboard, caret blink wakeups */
#if defined(__APPLE__)
        bool word_mod = (mods & CUI_MOD_ALT) != 0, line_mod = (mods & CUI_MOD_SUPER) != 0;
#else
        bool word_mod = (mods & CUI_MOD_CTRL) != 0, line_mod = false;
#endif
        bool far = (mods & (CUI_MOD_CTRL | CUI_MOD_SUPER)) != 0;
        int before_c = e->cursor, before_a = e->anchor;
        int view_lines = CUI_MAX(1, (int)(view_h / lh) - 1);
        for (int k = CUI_KEY_NONE + 1; k < CUI_KEY_COUNT; k++)
            if (ui->kpressed[k] && k != CUI_KEY_TAB && k != CUI_KEY_ESCAPE) st->tab_free = false;
        if (ui->text_in_len > 0) st->tab_free = false;
        int s0 = CUI_MIN(e->cursor, e->anchor), s1 = CUI_MAX(e->cursor, e->anchor);
        for (int rep = ui->kpressed[CUI_KEY_LEFT]; rep > 0; rep--) {
            s0 = CUI_MIN(e->cursor, e->anchor);
            if (!shift && e->cursor != e->anchor && !word_mod && !line_mod) { cui__ed_move(&E, s0, false); continue; }
            int line = cui__ed_line_of(&E, e->cursor);
            cui__ed_move(&E, line_mod ? cui__ed_first_text(&E, line) : word_mod ? cui__edit_word_left(buf, e->cursor) : cui__utf8_prev(buf, e->cursor), shift);
        }
        for (int rep = ui->kpressed[CUI_KEY_RIGHT]; rep > 0; rep--) {
            s1 = CUI_MAX(e->cursor, e->anchor);
            if (!shift && e->cursor != e->anchor && !word_mod && !line_mod) { cui__ed_move(&E, s1, false); continue; }
            int line = cui__ed_line_of(&E, e->cursor);
            cui__ed_move(&E, line_mod ? cui__ed_end(&E, line) : word_mod ? cui__edit_word_right(buf, E.len, e->cursor) : cui__utf8_next(buf, E.len, e->cursor), shift);
        }
        int vmove = (int)ui->kpressed[CUI_KEY_DOWN] - (int)ui->kpressed[CUI_KEY_UP];
        int vpage = (int)ui->kpressed[CUI_KEY_PAGE_DOWN] - (int)ui->kpressed[CUI_KEY_PAGE_UP];
        if (line_mod && vmove) cui__ed_move(&E, vmove < 0 ? 0 : E.len, shift);
        else if (vmove || vpage) {
            int line = cui__ed_line_of(&E, e->cursor);
            if (st->pref_col < 0) st->pref_col = cui__ed_caret_col(&E, e->cursor);
            int target = line + vmove + vpage * view_lines;
            int keep = st->pref_col;
            if (vpage) st->sy += (double)(CUI_CLAMP(target, 0, E.nl - 1) - line) * lh;
            if (target < 0) cui__ed_move(&E, 0, shift);
            else if (target >= E.nl) cui__ed_move(&E, E.len, shift);
            else cui__ed_move(&E, cui__ed_pos(&E, target, (float)keep), shift);
            st->pref_col = keep;
        }
        if (cui_key_pressed(ui, CUI_KEY_HOME)) {
            int line = cui__ed_line_of(&E, e->cursor), ft = cui__ed_first_text(&E, line);
            cui__ed_move(&E, far ? 0 : e->cursor == ft ? E.ls[line] : ft, shift);
        }
        if (cui_key_pressed(ui, CUI_KEY_END)) cui__ed_move(&E, far ? E.len : cui__ed_end(&E, cui__ed_line_of(&E, e->cursor)), shift);
        if (prim && cui_key_pressed(ui, CUI_KEY_A)) { e->anchor = 0; e->cursor = E.len; }
        /* clipboard: without a selection, copy and cut take the whole line */
        s0 = CUI_MIN(e->cursor, e->anchor); s1 = CUI_MAX(e->cursor, e->anchor);
        if (prim && (cui_key_pressed(ui, CUI_KEY_C) || cui_key_pressed(ui, CUI_KEY_X))) {
            int a = s0, b = s1;
            if (a == b) cui__ed_line_at(&E, a, &a, &b);
            if (b > a && ui->platform.set_clipboard) ui->platform.set_clipboard(ui->platform.user, cui__frame_strdup(ui, buf + a, b - a));
            if (cui_key_pressed(ui, CUI_KEY_X) && b > a) cui__ed_replace(&E, a, b, "", 0, 3);
        }
        if (!E.ro) {
            s0 = CUI_MIN(e->cursor, e->anchor); s1 = CUI_MAX(e->cursor, e->anchor);
            if (prim && cui_key_pressed(ui, CUI_KEY_V) && ui->platform.get_clipboard) {
                const char *clip = ui->platform.get_clipboard(ui->platform.user);
                if (clip && *clip) {
                    cui__ed_replace(&E, s0, s1, clip, (int)strlen(clip), 3);
                    e->last_edit = 0;   /* typing after a paste is its own step */
                }
            }
            bool undo = prim && !shift && cui_key_pressed(ui, CUI_KEY_Z);
            bool redo = prim && ((shift && cui_key_pressed(ui, CUI_KEY_Z)) || cui_key_pressed(ui, CUI_KEY_Y));
            if ((undo || redo) && cui__edit_undo_step(ui, e, buf, cap, !undo)) {
                E.len = (int)strlen(buf);
                cui__ed_index(&E);
                E.changed = E.reveal = true;
                st->pref_col = -1;
            }
            for (int rep = ui->kpressed[CUI_KEY_BACKSPACE]; rep > 0; rep--) {
                s0 = CUI_MIN(e->cursor, e->anchor); s1 = CUI_MAX(e->cursor, e->anchor);
                if (s1 > s0) { cui__ed_replace(&E, s0, s1, "", 0, 3); continue; }
                if (s0 == 0) continue;
                int line = cui__ed_line_of(&E, s0), ls = E.ls[line], to = cui__utf8_prev(buf, s0);
                if (word_mod) to = cui__edit_word_left(buf, s0);
                else if (line_mod) to = ls;
                else if (s0 > ls && cui__ed_first_text(&E, line) >= s0 && buf[s0 - 1] == ' ') {
                    /* in the indentation: back to the previous tab stop */
                    int col = cui__ed_col(buf, ls, s0, E.tab), stop = (col - 1) / E.tab * E.tab;
                    to = s0;
                    while (to > ls && buf[to - 1] == ' ' && cui__ed_col(buf, ls, to - 1, E.tab) >= stop) to--;
                }
                cui__ed_replace(&E, to, s0, "", 0, 2);
            }
            for (int rep = ui->kpressed[CUI_KEY_DELETE]; rep > 0; rep--) {
                s0 = CUI_MIN(e->cursor, e->anchor); s1 = CUI_MAX(e->cursor, e->anchor);
                if (s1 > s0) { cui__ed_replace(&E, s0, s1, "", 0, 3); continue; }
                if (s0 >= E.len) continue;
                int to = word_mod ? cui__edit_word_right(buf, E.len, s0) : cui__utf8_next(buf, E.len, s0);
                cui__ed_replace(&E, s0, to, "", 0, 2);
                e->cursor = e->anchor = e->last_cursor = s0;
            }
            for (int rep = ui->kpressed[CUI_KEY_ENTER]; rep > 0 && !prim; rep--) cui__ed_newline(&E, lang_kind);
            if (cui_key_pressed(ui, CUI_KEY_TAB) && !(mods & CUI_MOD_CTRL)) {
                s0 = CUI_MIN(e->cursor, e->anchor); s1 = CUI_MAX(e->cursor, e->anchor);
                if (shift || s1 > s0) cui__ed_indent(&E, shift);
                else {
                    int col = cui__ed_caret_col(&E, s0), n = E.use_tabs ? 1 : E.tab - col % E.tab;
                    char sp[17];
                    memset(sp, E.use_tabs ? '\t' : ' ', sizeof sp);
                    cui__ed_replace(&E, s0, s0, sp, n, 3);
                }
            }
            if (ui->text_in_len > 0 && !(prim && !(mods & CUI_MOD_ALT))) cui__ed_type(&E, ui->text_in, ui->text_in_len, lang_kind);
        }
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) {
            if (st->mode) st->mode = 0;
            else if (e->cursor != e->anchor) e->anchor = e->cursor;
            else st->tab_free = true;   /* the next Tab leaves the editor */
        }
        if (e->cursor != before_c || e->anchor != before_a || E.changed) {
            ui->caret_time = ui->time;
            if (vmove == 0 && vpage == 0) st->pref_col = -1;
        }
        if (!E.ro && !st->tab_free) ui->tab_owner = lid;
    }

    /* ---- build ---- */
    bool dark = ui->mode == CUI_THEME_DARK;
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->h = o->height < 0 ? CUI_FILL : CUI_PX(height);
    on->min_h = 80;
    on->radius = CUI_CORNERS(ui->theme.radius_control);
    cui_color bg = dark ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, BACKGROUND), 0.4f) : CUI_C(ui, SURFACE_MUTED);
    on->bg = bg;   /* the code block's surface */
    on->border = 1;
    cui__tween(ui, &st->focus_t, (ui->focus == lid || ui->focus == fid || ui->focus == gid) ? 1.0f : 0.0f, CUI__T_FOCUS);
    on->border_color = cui_mix(CUI_C(ui, BORDER), cui__edge_on(ui), st->focus_t * 0.7f);
    on->flags |= CUI_NF_CLIP;
    cui__open(ui, outer);

    if (st->mode) {
        bool narrow = known && br.w < 360;   /* a phone: a shorter count, no search icon */
        int bar = cui__node_new(ui, 0);
        cui_node *bn = &ui->nodes[bar];
        bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER; bn->gap = 4;
        bn->w = CUI_FILL;
        bn->pad[0] = bn->pad[2] = 6; bn->pad[1] = 6; bn->pad[3] = 6;
        bn->bg = CUI_C(ui, SURFACE);
        cui__open(ui, bar);
        cui_push_id(ui, id_str);
        cui_input_opts io;
        memset(&io, 0, sizeof io);
        io.size = CUI_SIZE_SM;
        io.width = CUI_FILL;
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM;
        bo.variant = CUI_BUTTON_GHOST;
        if (st->mode == 1) {
            if (st->bar_focus) { ui->focus = fid; ui->tf_select_all = fid; st->bar_focus = false; }
            io.placeholder = "Find";
            io.icon = narrow ? NULL : CUI_ICON_SEARCH;
            int f = cui_input_ex(ui, "find", st->query, sizeof st->query, &io);
            int qn = (int)strlen(st->query);
            int s0 = CUI_MIN(e->cursor, e->anchor);
            if (f & CUI_CHANGED) cui__ed_find_go(&E, s0, 1);   /* as you type, from where the selection starts */
            if (f & CUI_SUBMITTED) cui__ed_find_go(&E, shift ? s0 : s0 + (e->cursor != e->anchor ? 1 : 0), shift ? -1 : 1);
            /* the count, and which match the selection is */
            int total = 0, index = 0;
            s0 = CUI_MIN(e->cursor, e->anchor);
            int s1 = CUI_MAX(e->cursor, e->anchor);
            bool on_match = qn > 0 && s1 - s0 == qn && cui__ed_hit(buf, E.len, s0, st->query, qn);
            for (int i = 0; qn > 0 && i + qn <= E.len; i++)
                if (cui__ed_hit(buf, E.len, i, st->query, qn)) { total++; if (i < s0 || (on_match && i == s0)) index++; }
            res.find_matches = total;
            res.find_index = on_match ? index : 0;
            const char *label = qn == 0 ? "" : narrow ? (on_match ? cui__frame_printf(ui, "%d/%d", index, total) : cui__frame_printf(ui, "%d", total))
                              : total == 0 ? "No results" : on_match ? cui__frame_printf(ui, "%d of %d", index, total)
                              : cui__frame_printf(ui, "%d match%s", total, total == 1 ? "" : "es");
            if (*label) {
                int t = cui__text_node(ui, label, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), total ? CUI_C(ui, TEXT_SECONDARY) : CUI_C(ui, TEXT_MUTED), false);
                ui->nodes[t].flags |= CUI_NF_TABULAR;
                ui->nodes[t].line_height = 1.3f;
                ui->nodes[t].pad[1] = ui->nodes[t].pad[3] = 4;
            }
            bo.disabled = total == 0;
            bo.tooltip = "Previous match"; bo.shortcut = "Shift Enter";
            if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_UP, &bo)) cui__ed_find_go(&E, s0, -1);
            ui->nodes[ui->last_node].w = CUI_PX(26); ui->nodes[ui->last_node].h = CUI_PX(26);
            bo.tooltip = "Next match"; bo.shortcut = "Enter";
            if (cui_icon_button_ex(ui, CUI_ICON_CHEVRON_DOWN, &bo)) cui__ed_find_go(&E, s0 + (s1 > s0 ? 1 : 0), 1);
            ui->nodes[ui->last_node].w = CUI_PX(26); ui->nodes[ui->last_node].h = CUI_PX(26);
            bo.disabled = false;
        } else {
            if (st->bar_focus) { ui->focus = gid; ui->tf_select_all = gid; st->bar_focus = false; }
            io.placeholder = narrow ? "Go to line" : cui__frame_printf(ui, "Go to line, 1 to %d", E.nl);
            io.icon = narrow ? NULL : CUI_ICON_ARROW_DOWN;
            int f = cui_input_ex(ui, "goto", st->go, sizeof st->go, &io);
            if (f & CUI_SUBMITTED) {
                char *end = NULL;
                long line = strtol(st->go, &end, 10), col = 1;
                if (end && *end == ':') col = strtol(end + 1, NULL, 10);
                if (end != st->go) {
                    line = CUI_CLAMP(line, 1, E.nl);
                    e->cursor = e->anchor = cui__ed_pos(&E, (int)line - 1, (float)CUI_MAX(0, col - 1));
                    E.reveal = E.center = true;
                    st->pref_col = -1;
                    st->mode = 0;
                    ui->focus = lid;
                }
            }
        }
        bo.tooltip = "Close"; bo.shortcut = "Esc";
        if (cui_icon_button_ex(ui, CUI_ICON_X, &bo)) { st->mode = 0; ui->focus = lid; }
        ui->nodes[ui->last_node].w = CUI_PX(26); ui->nodes[ui->last_node].h = CUI_PX(26);
        cui_pop_id(ui);
        cui__close(ui);
        int hl = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
        ui->nodes[hl].bg = CUI_C(ui, BORDER);
    }

    /* ---- follow the caret, then keep the scroll in range ---- */
    if (E.changed) {
        content_h = (double)E.nl * lh;
        max_sy = CUI_MAX(0.0, content_h - view_h);
        max_sx = CUI_MAX(0.0f, (float)CUI_MAX(E.maxlen, 1) * cw + CUI__ED_GAP + 24 - code_w);
    }
    int caret_line = cui__ed_line_of(&E, e->cursor);
    int caret_col = cui__ed_col(buf, E.ls[caret_line], e->cursor, E.tab);
    /* a line wider than the longest in bytes (tabs) still scrolls into reach */
    max_sx = CUI_MAX(max_sx, (float)caret_col * cw + CUI__ED_GAP + 24 - code_w);
    if (E.reveal) {
        double cy = (double)caret_line * lh;
        bool out = cy < st->sy || cy + lh > st->sy + view_h;
        if (E.center && out) {   /* whole lines: none cut at the top */
            double k = floor((cy - view_h * 0.5) / lh + 0.5);
            st->sy = k > 0 ? k * lh + CUI__ED_INSET : 0;
        }
        else if (cy < st->sy) st->sy = cy;
        else if (cy + lh > st->sy + view_h) st->sy = cy + lh - view_h;
        float cx = (float)caret_col * cw, margin = CUI_MIN(cw * 4, code_w * 0.25f);
        if (cx < st->sx + margin) st->sx = cx - margin;
        if (cx > st->sx + code_w - margin) st->sx = cx - code_w + margin;
        if (E.center && (cx < st->sx || cx > st->sx + code_w)) st->sx = cx - code_w * 0.5f;
    }
    st->sy = CUI_MAX(0.0, CUI_MIN(max_sy, st->sy));
    st->sx = cui__clampf(st->sx, 0, max_sx);

    /* ---- the editor's lines ---- */
    int body = cui__node_new(ui, lid);
    cui_node *bdn = &ui->nodes[body];
    bdn->w = CUI_FILL; bdn->h = CUI_FILL;
    cui__mark_interactive(ui, body, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui__ed_paint *p = cui__frame_alloc(ui, sizeof *p);
    memset(p, 0, sizeof *p);
    p->sid = sid;
    int first = (int)CUI_MAX(0.0, floor((st->sy - CUI__ED_INSET) / lh));
    int last = (int)CUI_MIN((double)E.nl, ceil((st->sy + view_h + 2 * CUI__ED_INSET) / lh) + 1);
    if (first > last) first = last;
    p->first = first; p->count = last - first; p->nl = E.nl;
    int a = E.ls[first], b = last > first ? cui__ed_end(&E, last - 1) : a;
    p->base = a;
    p->text = cui__frame_strdup(ui, buf + a, b - a);   /* the caller may change the buffer before painting */
    int *ls = cui__frame_alloc(ui, sizeof(int) * (size_t)(p->count + 1));
    memcpy(ls, E.ls + first, sizeof(int) * (size_t)(p->count + 1));
    p->ls = ls;
    p->lang = L; p->json = json;
    p->in_block = cui__ed_block_state(&E, &L, first);
    p->tab = E.tab;
    p->cursor = e->cursor; p->anchor = e->anchor;
    p->caret_line = caret_line; p->caret_col = caret_col;
    p->focused = focused;
    p->show_line = focused || ui->focus == fid || ui->focus == gid;
    p->ro = E.ro;
    p->empty = E.len == 0;
    p->placeholder = o->placeholder;
    p->size = size; p->lh = lh; p->cw = cw; p->gutter = gutter;
    p->view_h = known ? br.h : 0;
    p->sy = st->sy; p->max_sy = max_sy;
    p->sx = st->sx; p->max_sx = max_sx;
    p->vc0 = (int)floorf(st->sx / cw) - 1;
    p->vc1 = (int)ceilf((st->sx + code_w + CUI__ED_GAP) / cw) + 1;
    p->caret_phase = ui->time - ui->caret_time;
    p->br_line[0] = p->br_line[1] = -1;
    if (e->cursor == e->anchor && p->show_line) {
        int ba, bb;
        cui__ed_brackets(&E, e->cursor, &ba, &bb);
        if (ba >= 0) {
            res.bracket = ba; res.bracket_match = bb;
            int pos[2] = { ba, bb };
            for (int m = 0; m < 2; m++) {
                p->br_line[m] = cui__ed_line_of(&E, pos[m]);
                p->br_col[m] = cui__ed_col(buf, E.ls[p->br_line[m]], pos[m], E.tab);
            }
        }
    }
    if (st->mode == 1 && st->query[0]) { p->query = cui__frame_strdup(ui, st->query, -1); p->qn = (int)strlen(p->query); }
    cui__syntax_colors(ui, p->colors);
    cui_color fg = CUI_C(ui, FOREGROUND);
    cui_color acc = ui->accent == CUI_ACCENT_NEUTRAL ? cui_oklch(0.6f, 0.13f, 250, 1) : CUI_C(ui, ACCENT);
    p->num = cui_alpha(CUI_C(ui, TEXT_MUTED), 0.7f);
    p->num_on = CUI_C(ui, TEXT_SECONDARY);
    p->band = cui_alpha(fg, dark ? 0.05f : 0.04f);
    p->sel = cui_alpha(acc, ui->accent == CUI_ACCENT_NEUTRAL ? 0.28f : 0.26f);
    if (!focused && !p->show_line) p->sel = cui_alpha(fg, dark ? 0.12f : 0.08f);   /* resting: a quiet selection */
    p->match = cui_alpha(acc, 0.12f);
    p->match_edge = cui_alpha(acc, dark ? 0.55f : 0.45f);
    p->brk = cui_alpha(fg, dark ? 0.10f : 0.06f);
    p->brk_edge = cui_alpha(CUI_C(ui, TEXT_MUTED), 0.55f);
    p->caret = ui->accent == CUI_ACCENT_NEUTRAL ? fg : CUI_C(ui, ACCENT);
    p->muted = CUI_C(ui, TEXT_MUTED);
    p->line = CUI_C(ui, BORDER);
    p->bar = ui->accent == CUI_ACCENT_NEUTRAL ? fg : CUI_C(ui, ACCENT);
    bool recent = st->last_activity > 0 && ui->time - st->last_activity < 0.9;
    bool vb = over_vbar && (lr.hovered || st->bar_drag == 1), hb = over_hbar && (lr.hovered || st->bar_drag == 2);
    bool show_bar = recent || vb || hb || st->bar_drag || (lr.hovered && (max_sy > 0 || max_sx > 0));
    if (recent) cui__wakeup_after(ui, (float)(0.9 - (ui->time - st->last_activity)) + 0.01f);
    cui__tween(ui, &st->bar_t, show_bar ? 1.0f : 0.0f, show_bar ? 0.12f : 0.3f);
    p->bar_t = st->bar_t;
    p->bar_big = (vb || st->bar_drag == 1) ? 1 : (hb || st->bar_drag == 2) ? 2 : 0;
    bdn->paint = cui__paint_editor;
    bdn->paint_data = p;
    if (focused) cui__wakeup_after(ui, 0.53f);

    if (focused && ui->platform.set_text_input_rect) {
        cui_rect cr = { gx + CUI__ED_GAP - st->sx + (float)caret_col * cw, br.y + CUI__ED_INSET + (float)((double)caret_line * lh - st->sy), 1, lh };
        ui->platform.set_text_input_rect(ui->platform.user, cr);
    }
    if (focused) cui__focus_ring(ui, outer, ui->focus_visible ? 1.0f : 0.0f);
    cui__close(ui);   /* outer */
    ui->last_id = id;

    res.changed = E.changed;
    res.refused = E.refused;
    res.focused = ui->focus == lid || ui->focus == fid || ui->focus == gid;
    res.cursor = e->cursor; res.anchor = e->anchor;
    res.line = caret_line + 1; res.column = caret_col + 1;
    res.line_count = E.nl;
    res.length = E.len;
    return res;
}
