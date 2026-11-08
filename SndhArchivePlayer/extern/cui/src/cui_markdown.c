/* Markdown view: a CommonMark-ish subset rendered as native content.
 *
 * The text is parsed every frame straight from the caller's buffer. Blocks are
 * found line by line (containers strip their prefixes into new line lists that
 * still point into the text), inline styles come from a delimiter stack, and
 * each paragraph is wrapped by hand, since one line mixes fonts, into a single
 * painted node with an invisible hit box per link. Code blocks and tables are
 * drawn by cui_code_block and cui_table. Memory comes from the frame arena. */
#include "cui_internal.h"

#define cui__fs cui__fsz

#define MD_MAX_DEPTH 12      /* containers nested deeper show as text */
#define MD_WINDOW 64         /* how far back a closing * or _ looks for its opener */
#define MD_MAX_RUN 64        /* longer runs of * _ ~ are plain text */
#define MD_MAX_TICKS 32      /* longer runs of backticks are plain text */
#define MD_MAX_COLS 32       /* table columns beyond this are dropped */
#define MD_SHEAR 0.2f        /* slant of the synthesized italic (about 11 degrees) */

/* ------------------------------------------------------------------------ */
/* Lines                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct { const char *s; int len, off; } md_line;   /* off: byte offset of s in the text */

static bool md_sp(char c) { return c == ' ' || c == '\t'; }

static int md_indent(md_line L, int *bytes) {
    int col = 0, i = 0;
    while (i < L.len && md_sp(L.s[i])) { col = L.s[i] == '\t' ? (col / 4 + 1) * 4 : col + 1; i++; }
    if (bytes) *bytes = i;
    return col;
}
static bool md_blank(md_line L) {
    for (int i = 0; i < L.len; i++) if (!md_sp(L.s[i])) return false;
    return true;
}
static md_line md_skip(md_line L, int bytes) {
    if (bytes > L.len) bytes = L.len;
    L.s += bytes; L.len -= bytes; L.off += bytes;
    return L;
}
static md_line md_strip_cols(md_line L, int cols) {
    int col = 0, i = 0;
    while (i < L.len && col < cols && md_sp(L.s[i])) { col = L.s[i] == '\t' ? (col / 4 + 1) * 4 : col + 1; i++; }
    return md_skip(L, i);
}
static md_line md_trim(md_line L) {
    int b;
    md_indent(L, &b);
    L = md_skip(L, b);
    while (L.len > 0 && md_sp(L.s[L.len - 1])) L.len--;
    return L;
}
static bool md_has(md_line L, char c) { return L.len > 0 && memchr(L.s, c, (size_t)L.len) != NULL; }

/* ------------------------------------------------------------------------ */
/* Inline parsing                                                           */
/* ------------------------------------------------------------------------ */

enum { MS_STRONG = 1, MS_EM = 2, MS_STRIKE = 4, MS_CODE = 8, MS_BREAK = 16 };
typedef struct { const char *p; int len; uint8_t style; int link; } md_run;
typedef struct { const char *url; int len; bool www, pending, autolink; } md_link;
typedef struct { md_run *runs; int nruns; md_link *links; int nlinks; } md_inline;

enum { TK_TXT, TK_DELIM, TK_CODE, TK_BR, TK_LOPEN, TK_LCLOSE, TK_AUTO };
typedef struct {
    const char *p; int len;
    uint8_t kind, ch, can_open, can_close, hidden, image;
    int16_t count, orig;
    int8_t pre[3], post[3];   /* strong, em, strike: closes before / opens after this token */
    int link;
} md_tok;

static bool md_ws(unsigned char c) { return c == 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static bool md_punct(unsigned char c) { return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126); }
static bool md_url_lead(unsigned char c) { return md_ws(c) || c == '(' || c == '*' || c == '_' || c == '~' || c == '"' || c == '\''; }

typedef struct { md_tok *t; int n, cap; } md_toks;
static md_tok *md_add(md_toks *T, int kind, const char *p, int len) {
    if (T->n >= T->cap) return NULL;
    md_tok *t = &T->t[T->n++];
    memset(t, 0, sizeof *t);
    t->kind = (uint8_t)kind; t->p = p; t->len = len; t->link = -1;
    return t;
}

/* (destination "title") after a ]. Returns 1 and the end, 0 when it is not a
 * link, -1 when the text ends first. */
static int md_link_dest(const char *s, int n, int at, const char **url, int *ulen, int *end) {
    if (at >= n) return -1;
    if (s[at] != '(') return 0;
    int j = at + 1;
    while (j < n && md_ws((unsigned char)s[j])) j++;
    if (j >= n) return -1;
    int us = j, ue;
    if (s[j] == '<') {
        us = ++j;
        while (j < n && s[j] != '>' && s[j] != '\n') { if (s[j] == '\\' && j + 1 < n) j++; j++; }
        if (j >= n) return -1;
        if (s[j] != '>') return 0;
        ue = j++;
    } else {
        int depth = 0;
        while (j < n && !md_ws((unsigned char)s[j])) {
            if (s[j] == '\\' && j + 1 < n) { j += 2; continue; }
            if (s[j] == '(') { if (++depth > 32) return 0; }
            else if (s[j] == ')') { if (depth == 0) break; depth--; }
            j++;
        }
        if (j >= n) return -1;
        ue = j;
    }
    while (j < n && md_ws((unsigned char)s[j])) j++;
    if (j >= n) return -1;
    if (s[j] == '"' || s[j] == '\'' || s[j] == '(') {
        char q = s[j] == '(' ? ')' : s[j];
        j++;
        while (j < n && s[j] != q) { if (s[j] == '\\' && j + 1 < n) j++; j++; }
        if (j >= n) return -1;
        j++;
        while (j < n && md_ws((unsigned char)s[j])) j++;
        if (j >= n) return -1;
    }
    if (s[j] != ')') return 0;
    *url = s + us; *ulen = ue - us; *end = j + 1;
    return 1;
}

/* <scheme:...> autolink: index of the closing >, or -1 */
static int md_autolink(const char *s, int n, int i) {
    int j = i + 1, k = 0;
    if (j >= n || !((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) return -1;
    while (j < n && k < 32 && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') || (s[j] >= '0' && s[j] <= '9') ||
                               s[j] == '+' || s[j] == '.' || s[j] == '-')) { j++; k++; }
    if (k < 2 || j >= n || s[j] != ':') return -1;
    while (j < n && s[j] != '>' && s[j] != '<' && !md_ws((unsigned char)s[j])) j++;
    return j < n && s[j] == '>' ? j : -1;
}

/* <br>, <br/>, <br />: end of the tag, or -1 */
static int md_br_tag(const char *s, int n, int i) {
    if (i + 3 >= n || (s[i + 1] != 'b' && s[i + 1] != 'B') || (s[i + 2] != 'r' && s[i + 2] != 'R')) return -1;
    int j = i + 3;
    while (j < n && s[j] == ' ') j++;
    if (j < n && s[j] == '/') j++;
    return j < n && s[j] == '>' ? j + 1 : -1;
}

/* bare https://, http:// and www. links: end of the URL, or i */
static int md_bare_url(const char *s, int n, int i, bool *www) {
    int pre = 0;
    if (n - i > 8 && !strncmp(s + i, "https://", 8)) pre = 8;
    else if (n - i > 7 && !strncmp(s + i, "http://", 7)) pre = 7;
    else if (n - i > 4 && !strncmp(s + i, "www.", 4)) { pre = 4; *www = true; }
    if (!pre) return i;
    int e = i + pre;
    while (e < n && !md_ws((unsigned char)s[e]) && s[e] != '<') e++;
    while (e > i + pre && strchr("?!.,:*_~'\";", s[e - 1])) e--;
    if (e > i + pre && s[e - 1] == ')') {
        int open = 0, close = 0;
        for (int k = i; k < e; k++) { if (s[k] == '(') open++; else if (s[k] == ')') close++; }
        if (close > open) e--;
    }
    return e > i + pre ? e : i;
}

static void md_emit(md_run *runs, int *nr, const char *p, int len, int style, int link) {
    if (len <= 0 && !(style & MS_BREAK)) return;
    if (*nr > 0 && !(style & MS_BREAK)) {
        md_run *q = &runs[*nr - 1];
        if (q->style == style && q->link == link && q->p + q->len == p) { q->len += len; return; }
    }
    md_run *r = &runs[(*nr)++];
    r->p = p; r->len = len; r->style = (uint8_t)style; r->link = link;
}

/* Parses inline markdown into styled runs. tail: the text is still arriving
 * and this is its end, so unclosed markers render as if closed. */
static md_inline md_parse_inline(cui_ctx *ui, const char *s, int n, bool tail) {
    md_inline out;
    memset(&out, 0, sizeof out);
    if (n <= 0) return out;
    int specials = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '*' || c == '_' || c == '~' || c == '`' || c == '[' || c == ']' || c == '!' || c == '\\' || c == '<' || c == '&' || c == '\n') specials++;
        else if ((c == 'h' || c == 'w') && (i == 0 || md_url_lead((unsigned char)s[i - 1]))) specials++;
    }
    md_toks T;
    T.cap = specials * 2 + 2; T.n = 0;
    T.t = cui__frame_alloc(ui, sizeof(md_tok) * (size_t)T.cap);
    int link_cap = specials + 1, nl = 0;
    md_link *links = cui__frame_alloc(ui, sizeof(md_link) * (size_t)link_cap);
    int stack[32], nb = 0;            /* open [ and ![ tokens */
    int no_tick[MD_MAX_TICKS + 1];    /* no closing run of this length at or after */
    for (int k = 0; k <= MD_MAX_TICKS; k++) no_tick[k] = n + 1;
    int i = 0, ts = 0;
#define MD_FLUSH() do { if (i > ts) md_add(&T, TK_TXT, s + ts, i - ts); } while (0)
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' && i + 1 < n && md_punct((unsigned char)s[i + 1])) {
            MD_FLUSH();
            md_add(&T, TK_TXT, s + i + 1, 1);
            i += 2; ts = i;
            continue;
        }
        if (c == '\n') { MD_FLUSH(); md_add(&T, TK_BR, s + i, 0); i++; ts = i; continue; }
        if (c == '`') {
            int k = 0;
            while (i + k < n && s[i + k] == '`') k++;
            MD_FLUSH();
            int close = -1;
            if (k <= MD_MAX_TICKS && i + k < no_tick[k]) {
                int j = i + k;
                while (j < n) {
                    if (s[j] != '`') { j++; continue; }
                    int m = j;
                    while (m < n && s[m] == '`') m++;
                    if (m - j == k) { close = j; break; }
                    j = m;
                }
                if (close < 0) no_tick[k] = i + k;
            }
            int cs = i + k, ce = close >= 0 ? close : n;
            if (close >= 0 || (tail && k <= MD_MAX_TICKS)) {
                bool all_sp = true;
                for (int q = cs; q < ce; q++) if (s[q] != ' ') { all_sp = false; break; }
                if (close >= 0 && ce - cs >= 2 && s[cs] == ' ' && s[ce - 1] == ' ' && !all_sp) { cs++; ce--; }
                md_add(&T, TK_CODE, s + cs, ce - cs);
                i = close >= 0 ? close + k : n;
            } else {
                md_add(&T, TK_TXT, s + i, k);
                i += k;
            }
            ts = i;
            continue;
        }
        if (c == '*' || c == '_' || c == '~') {
            int k = 0;
            while (i + k < n && s[i + k] == (char)c) k++;
            MD_FLUSH();
            md_tok *t = md_add(&T, TK_DELIM, s + i, k);
            if (t) {
                unsigned char before = i > 0 ? (unsigned char)s[i - 1] : 0, after = i + k < n ? (unsigned char)s[i + k] : 0;
                bool lf = !md_ws(after) && (!md_punct(after) || md_ws(before) || md_punct(before));
                bool rf = !md_ws(before) && (!md_punct(before) || md_ws(after) || md_punct(after));
                t->ch = c;
                t->count = t->orig = (int16_t)CUI_MIN(k, 127);
                if (c == '_') { t->can_open = lf && (!rf || md_punct(before)); t->can_close = rf && (!lf || md_punct(after)); }
                else { t->can_open = lf; t->can_close = rf; }
                if (k > MD_MAX_RUN || (c == '~' && k != 2)) t->can_open = t->can_close = 0;
                if (tail && i + k >= n) { t->hidden = 1; t->can_open = t->can_close = 0; }   /* may still grow */
            }
            i += k; ts = i;
            continue;
        }
        if (c == '[' || (c == '!' && i + 1 < n && s[i + 1] == '[')) {
            int k = c == '!' ? 2 : 1;
            MD_FLUSH();
            md_tok *t = md_add(&T, TK_LOPEN, s + i, k);
            if (t) {
                t->image = c == '!';
                if (nb < 32) stack[nb++] = T.n - 1;
                else t->kind = TK_TXT;
            }
            i += k; ts = i;
            continue;
        }
        if (c == ']') {
            MD_FLUSH();
            const char *url = NULL;
            int ulen = 0, end = i + 1;
            int st = md_link_dest(s, n, i + 1, &url, &ulen, &end);
            int ob = nb > 0 ? stack[nb - 1] : -1;
            bool pending = tail && st < 0;
            if (ob >= 0 && nl < link_cap && (st == 1 || pending)) {
                md_link *L = &links[nl];
                L->url = url; L->len = ulen; L->pending = st != 1;
                T.t[ob].link = nl;
                md_tok *t = md_add(&T, TK_LCLOSE, s + i, 0);
                if (t) t->link = nl;
                nl++;
                bool image = T.t[ob].image;
                nb--;
                if (!image) {   /* links do not nest: earlier [ become text */
                    int keep = 0;
                    for (int q = 0; q < nb; q++) if (T.t[stack[q]].image) stack[keep++] = stack[q];
                    nb = keep;
                }
                i = st == 1 ? end : n;
            } else {
                if (ob >= 0) nb--;
                md_add(&T, TK_TXT, s + i, 1);
                i++;
            }
            ts = i;
            continue;
        }
        if (c == '<') {
            int e = md_br_tag(s, n, i);
            if (e > 0) { MD_FLUSH(); md_add(&T, TK_BR, s + i, 0); i = e; ts = i; continue; }
            e = md_autolink(s, n, i);
            if (e > 0 && nl < link_cap) {
                MD_FLUSH();
                md_tok *t = md_add(&T, TK_AUTO, s + i + 1, e - i - 1);
                if (t) {
                    links[nl].url = s + i + 1; links[nl].len = e - i - 1; links[nl].autolink = true;
                    t->link = nl++;
                }
                i = e + 1; ts = i;
                continue;
            }
        }
        if (c == '&') {
            static const char *const ent[][2] = { { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" },
                                                 { "&#39;", "'" }, { "&nbsp;", "\xc2\xa0" } };
            int hit = -1;
            for (int k = 0; k < CUI_COUNTOF(ent); k++) {
                int el = (int)strlen(ent[k][0]);
                if (n - i >= el && !strncmp(s + i, ent[k][0], (size_t)el)) { hit = k; break; }
            }
            if (hit >= 0) {
                MD_FLUSH();
                md_add(&T, TK_TXT, ent[hit][1], (int)strlen(ent[hit][1]));
                i += (int)strlen(ent[hit][0]); ts = i;
                continue;
            }
        }
        if ((c == 'h' || c == 'w') && (i == 0 || md_url_lead((unsigned char)s[i - 1]))) {
            bool www = false;
            int e = md_bare_url(s, n, i, &www);
            if (e > i && nl < link_cap) {
                MD_FLUSH();
                md_tok *t = md_add(&T, TK_AUTO, s + i, e - i);
                if (t) {
                    links[nl].url = s + i; links[nl].len = e - i; links[nl].www = www; links[nl].autolink = true;
                    t->link = nl++;
                }
                i = e; ts = i;
                continue;
            }
        }
        i++;
    }
    MD_FLUSH();
#undef MD_FLUSH
    /* an unclosed [ at the end may still become a link: hold it back */
    if (tail) for (int q = 0; q < nb; q++) T.t[stack[q]].hidden = 1;

    /* emphasis: each closer looks back for the nearest opener of its kind */
    int *dl = cui__frame_alloc(ui, sizeof(int) * (size_t)(T.n + 1)), nd = 0;
    for (int k = 0; k < T.n; k++) if (T.t[k].kind == TK_DELIM && (T.t[k].can_open || T.t[k].can_close)) dl[nd++] = k;
    for (int ci = 0; ci < nd; ci++) {
        md_tok *C = &T.t[dl[ci]];
        if (!C->can_close) continue;
        while (C->count > 0) {
            int found = -1;
            for (int oi = ci - 1; oi >= 0 && ci - oi <= MD_WINDOW; oi--) {
                md_tok *O = &T.t[dl[oi]];
                if (O->ch != C->ch || !O->can_open || O->count <= 0) continue;
                if (C->ch == '~') { if (O->count < 2 || C->count < 2) continue; }
                else if ((O->can_close || C->can_open) && (O->orig + C->orig) % 3 == 0 && !(O->orig % 3 == 0 && C->orig % 3 == 0)) continue;
                found = oi;
                break;
            }
            if (found < 0) break;
            md_tok *O = &T.t[dl[found]];
            int use = C->ch == '~' ? 2 : (O->count >= 2 && C->count >= 2 ? 2 : 1);
            int st = C->ch == '~' ? 2 : (use == 2 ? 0 : 1);
            O->post[st]++; C->pre[st]++;
            O->count = (int16_t)(O->count - use); C->count = (int16_t)(C->count - use);
            for (int k = found + 1; k < ci; k++) T.t[dl[k]].can_open = T.t[dl[k]].can_close = 0;
        }
    }
    if (tail) {   /* openers still waiting for their closer style the rest of the text */
        for (int k = 0; k < nd; k++) {
            md_tok *O = &T.t[dl[k]];
            if (!O->can_open || O->count <= 0) continue;
            if (O->ch == '~') { if (O->count >= 2) O->post[2]++; }
            else { if (O->count >= 2) O->post[0]++; if (O->count % 2 == 1) O->post[1]++; }
            O->count = 0;
        }
    }

    /* runs */
    md_run *runs = cui__frame_alloc(ui, sizeof(md_run) * (size_t)(T.n + 1));
    int nr = 0, cnt[3] = { 0, 0, 0 }, cur = -1;
    for (int k = 0; k < T.n; k++) {
        md_tok *t = &T.t[k];
        for (int q = 0; q < 3; q++) cnt[q] -= t->pre[q];
        int style = (cnt[0] > 0 ? MS_STRONG : 0) | (cnt[1] > 0 ? MS_EM : 0) | (cnt[2] > 0 ? MS_STRIKE : 0);
        switch (t->kind) {
        case TK_TXT: md_emit(runs, &nr, t->p, t->len, style, cur); break;
        case TK_DELIM: if (!t->hidden && t->count > 0) md_emit(runs, &nr, t->p, t->count, style, cur); break;
        case TK_CODE: md_emit(runs, &nr, t->p, t->len, style | MS_CODE, cur); break;
        case TK_BR: md_emit(runs, &nr, t->p, 0, MS_BREAK, -1); break;
        case TK_LOPEN:
            if (t->link >= 0) cur = t->link;
            else if (!t->hidden) md_emit(runs, &nr, t->p, t->len, style, cur);
            break;
        case TK_LCLOSE: cur = -1; break;
        case TK_AUTO: md_emit(runs, &nr, t->p, t->len, style, cur >= 0 ? cur : t->link); break;
        default: break;
        }
        for (int q = 0; q < 3; q++) cnt[q] += t->post[q];
    }
    out.runs = runs; out.nruns = nr; out.links = links; out.nlinks = nl;
    return out;
}

static char *md_plain(cui_ctx *ui, const md_inline *in) {
    int total = 0;
    for (int k = 0; k < in->nruns; k++) total += (in->runs[k].style & MS_BREAK) ? 1 : in->runs[k].len;
    char *out = cui__frame_alloc(ui, (size_t)total + 1);
    int o = 0;
    for (int k = 0; k < in->nruns; k++) {
        if (in->runs[k].style & MS_BREAK) { out[o++] = ' '; continue; }
        memcpy(out + o, in->runs[k].p, (size_t)in->runs[k].len);
        o += in->runs[k].len;
    }
    out[o] = 0;
    return out;
}

/* ------------------------------------------------------------------------ */
/* Inline layout                                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_font reg, bold, mono;
    float size, mono_size, lh;   /* points, scaled */
    float pad;                   /* inline code: padding inside its tint */
} md_font;

typedef struct { const char *p; int len; float x, w; int line, run, link; uint8_t style, lead; } md_seg;
typedef struct { md_seg *seg; int n, cap; int lines; float widest; } md_lay;

static cui_font md_font_of(const md_font *f, int style) { return (style & MS_CODE) ? f->mono : (style & MS_STRONG) ? f->bold : f->reg; }
static float md_size_of(const md_font *f, int style) { return (style & MS_CODE) ? f->mono_size : f->size; }
static bool md_space_char(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static float md_piece_w(cui_ctx *ui, const md_font *f, const md_run *R, int b, int e) {
    float w = cui__text_width(ui, md_font_of(f, R->style), md_size_of(f, R->style), R->p + b, e - b, false);
    if (R->style & MS_CODE) { if (b == 0) w += f->pad; if (e == R->len) w += f->pad; }
    return w;
}

/* One line width per hard break, nothing wrapped: the natural width. */
static float md_runs_width(cui_ctx *ui, const md_inline *in, const md_font *f) {
    float w = 0, best = 0;
    for (int k = 0; k < in->nruns; k++) {
        const md_run *R = &in->runs[k];
        if (R->style & MS_BREAK) { best = CUI_MAX(best, w); w = 0; continue; }
        w += md_piece_w(ui, f, R, 0, R->len);
    }
    return CUI_MAX(best, w);
}

static void md_seg_add(cui_ctx *ui, md_lay *L, const md_inline *in, int run, const char *p, int len, float x, float w) {
    const md_run *R = &in->runs[run];
    if (L->n > 0) {
        md_seg *q = &L->seg[L->n - 1];
        if (q->line == L->lines && q->run == run && q->p + q->len == p) { q->len += len; q->w = x + w - q->x; return; }
    }
    if (L->n == L->cap) {
        int ncap = L->cap ? L->cap * 2 : 32;
        md_seg *ns = cui__frame_alloc(ui, sizeof(md_seg) * (size_t)ncap);
        if (L->n) memcpy(ns, L->seg, sizeof(md_seg) * (size_t)L->n);
        L->seg = ns; L->cap = ncap;
    }
    md_seg *s = &L->seg[L->n++];
    s->p = p; s->len = len; s->x = x; s->w = w; s->line = L->lines; s->run = run; s->link = R->link;
    s->style = R->style; s->lead = (R->style & MS_CODE) && p == R->p;
}

static void md_finish_line(md_lay *L, int *first, float x, float W, int align) {
    if (align && W > x) {
        float dx = (W - x) * (align == CUI_TEXT_CENTER ? 0.5f : 1.0f);
        for (int k = *first; k < L->n; k++) L->seg[k].x += dx;
    }
    L->widest = CUI_MAX(L->widest, x);
    L->lines++;
    *first = L->n;
}

/* Greedy word wrap across runs. Words wider than the line break between characters. */
static void md_layout(cui_ctx *ui, const md_inline *in, const md_font *f, float W, int align, md_lay *L) {
    memset(L, 0, sizeof *L);
    float x = 0;
    int first = 0, ri = 0, bi = 0, sp_run = -1;
    const char *sp_p = NULL;
    while (ri < in->nruns) {
        const md_run *R = &in->runs[ri];
        if (R->style & MS_BREAK) { md_finish_line(L, &first, x, W, align); x = 0; sp_run = -1; ri++; bi = 0; continue; }
        if (bi >= R->len) { ri++; bi = 0; continue; }
        if (md_space_char(R->p[bi])) {
            if (sp_run < 0) { sp_run = ri; sp_p = R->p + bi; }
            while (bi < R->len && md_space_char(R->p[bi])) bi++;
            continue;
        }
        float ww = 0;
        for (int rj = ri, bj = bi; rj < in->nruns; rj++, bj = 0) {
            const md_run *Q = &in->runs[rj];
            if (Q->style & MS_BREAK) break;
            int e = bj;
            while (e < Q->len && !md_space_char(Q->p[e])) e++;
            if (e > bj) ww += md_piece_w(ui, f, Q, bj, e);
            if (e < Q->len) break;
        }
        float sw = 0;
        if (sp_run >= 0 && x > 0) {
            const md_run *S = &in->runs[sp_run];
            sw = cui__text_width(ui, md_font_of(f, S->style), md_size_of(f, S->style), " ", 1, false);
        }
        if (x > 0 && x + sw + ww > W) { md_finish_line(L, &first, x, W, align); x = 0; sw = 0; }
        if (sw > 0) { md_seg_add(ui, L, in, sp_run, *sp_p == ' ' ? sp_p : " ", 1, x, sw); x += sw; }
        sp_run = -1;
        bool split = ww > W - x;
        while (ri < in->nruns) {
            const md_run *Q = &in->runs[ri];
            if (Q->style & MS_BREAK) break;
            int e = bi;
            while (e < Q->len && !md_space_char(Q->p[e])) e++;
            if (e > bi) {
                if (!split) {
                    float pw = md_piece_w(ui, f, Q, bi, e);
                    md_seg_add(ui, L, in, ri, Q->p + bi, e - bi, x, pw);
                    x += pw;
                } else {
                    cui_font qf = md_font_of(f, Q->style);
                    float qs = md_size_of(f, Q->style);
                    bool code = (Q->style & MS_CODE) != 0;
                    float x0 = x;
                    if (code && bi == 0) x += f->pad;
                    int chunk = bi, k = bi;
                    while (k < e) {
                        int nk = cui__utf8_next(Q->p, e, k);
                        if (nk <= k) nk = k + 1;
                        float cw = cui__text_width(ui, qf, qs, Q->p + k, nk - k, false);
                        if (x + cw > W && x > 0 && k > chunk) {
                            md_seg_add(ui, L, in, ri, Q->p + chunk, k - chunk, x0, x - x0);
                            md_finish_line(L, &first, x, W, align);
                            x = 0; x0 = 0; chunk = k;
                        }
                        x += cw;
                        k = nk;
                    }
                    if (code && e == Q->len) x += f->pad;
                    md_seg_add(ui, L, in, ri, Q->p + chunk, e - chunk, x0, x - x0);
                }
            }
            if (e < Q->len) { bi = e; break; }
            ri++; bi = 0;
        }
    }
    if (L->n > first) md_finish_line(L, &first, x, W, align);
}

/* ------------------------------------------------------------------------ */
/* Painting                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct {
    const md_seg *seg; int n;
    md_font f;
    cui_color fg, link_c, code_bg;
    const float *hover, *focus;   /* per link of the block */
    bool underline_rest;          /* links keep a full underline (text on a colored surface) */
} md_paint;

/* Slants the glyphs drawn since v0 around the baseline: a synthesized italic. */
static void md_shear(cui_ctx *ui, int v0, float base) {
    cui_xform x = ui->dl.xform_stack[ui->dl.xform_depth - 1];
    float by = base * ui->scale * x.scale + x.oy;
    for (int v = v0; v < ui->dl.vtx_count; v++) ui->dl.vtx[v].x += MD_SHEAR * (by - ui->dl.vtx[v].y);
}

static void md_paint_inline(cui_ctx *ui, cui_node *n, void *data) {
    md_paint *p = data;
    const md_font *f = &p->f;
    float fh = cui__font_line_height(ui, f->reg, f->size), asc = cui__font_ascent(ui, f->reg, f->size);
    float th = CUI_MAX(1.0f, f->size / 13.0f);
    for (int k = 0; k < p->n; k++) {
        const md_seg *s = &p->seg[k];
        float top = n->rect.y + (float)s->line * f->lh;
        float base = top + (f->lh - fh) * 0.5f + asc;
        float x = n->rect.x + s->x;
        cui_font font = md_font_of(f, s->style);
        float size = md_size_of(f, s->style);
        cui_color col = s->link >= 0 ? p->link_c : p->fg;
        float hv = s->link >= 0 ? p->hover[s->link] : 0;
        if (s->style & MS_CODE) {
            float ch = f->mono_size * 1.5f;
            cui_draw_rect(ui, (cui_rect){ x, top + (f->lh - ch) * 0.5f, s->w, ch }, CUI_CORNERS(4), p->code_bg);
        }
        float tx = x + (s->lead ? f->pad : 0);
        int v0 = ui->dl.vtx_count;
        cui__draw_text_line(ui, font, size, tx, base - cui__font_ascent(ui, font, size), col, s->p, s->len, false);
        if (s->style & MS_EM) md_shear(ui, v0, base);
        if (s->style & MS_STRIKE) cui_draw_rect(ui, (cui_rect){ x, base - f->size * 0.3f, s->w, th }, CUI_CORNERS(0), col);
        if (s->link >= 0) {
            float a = p->underline_rest ? 0.8f + 0.2f * hv : 0.35f + 0.65f * hv;
            cui_draw_rect(ui, (cui_rect){ x, base + CUI_MAX(1.5f, size * 0.14f), s->w, th }, CUI_CORNERS(0), cui_alpha(col, a));
            float fo = p->focus[s->link];
            if (fo > 0.01f) cui__draw_focus_ring(ui, (cui_rect){ x - 2, top + 1, s->w + 4, f->lh - 2 }, CUI_CORNERS(4), fo);
        }
    }
}

typedef struct { int kind; int depth; cui_color c; const char *num; md_font f; } md_mark_paint;   /* kind 0 bullet, 1 number */

static void md_paint_marker(cui_ctx *ui, cui_node *n, void *data) {
    md_mark_paint *p = data;
    cui_rect r = n->rect;
    float u = p->f.size / 13.0f;
    if (p->kind == 1) {
        float w = cui__text_width(ui, p->f.reg, p->f.size, p->num, -1, true);
        float fh = cui__font_line_height(ui, p->f.reg, p->f.size);
        cui__draw_text_line(ui, p->f.reg, p->f.size, r.x + r.w - 7 * u - w, r.y + (p->f.lh - fh) * 0.5f, p->c, p->num, -1, true);
        return;
    }
    cui_vec2 c = { r.x + 6 * u, r.y + p->f.lh * 0.5f };
    int shape = p->depth % 3;
    if (shape == 0) cui_draw_circle(ui, c, 2.5f * u, p->c);
    else if (shape == 1) {
        float rr = 2.75f * u;
        cui_draw_rect_border(ui, (cui_rect){ c.x - rr, c.y - rr, rr * 2, rr * 2 }, CUI_CORNERS(rr), cui__rgba(0, 0, 0, 0), p->c, 1.1f * u);
    } else {
        float rr = 2.25f * u;
        cui_draw_rect(ui, (cui_rect){ c.x - rr, c.y - rr, rr * 2, rr * 2 }, CUI_CORNERS(0.75f), p->c);
    }
}

static void md_paint_rail(cui_ctx *ui, cui_node *n, void *data) {
    cui_color *c = data;
    float w = 3.0f * CUI_MAX(0.8f, n->pad[3] / 14.0f);
    cui_draw_rect(ui, (cui_rect){ n->rect.x, n->rect.y, w, n->rect.h }, CUI_CORNERS(w * 0.5f), *c);
}

typedef struct { float w; } md_root_paint;
static void md_paint_root(cui_ctx *ui, cui_node *n, void *data) {
    md_root_paint *p = data;
    /* laid out for another width than it got: build once more at this one */
    if (fabsf(n->rect.w - p->w) > 0.5f) ui->animating = true;
}

/* ------------------------------------------------------------------------ */
/* Blocks                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    cui_ctx *ui;
    cui_id id;
    const char *src;
    int len, content_end;
    bool streaming, compact, fit, custom_color;
    float body;                  /* unscaled body size */
    float u;                     /* spacing unit: body / 13 */
    cui_color fg, fg2, link_c, code_bg, marker_c, rule_c, rail_c;
    int link_seq, code_seq, table_seq;
    cui_markdown_result *res;
} md_ctx;

typedef struct { float w; int depth; bool tight; cui_color fg; } md_box;

enum { BK_NONE, BK_PARA, BK_HEADING, BK_CODE, BK_LIST, BK_QUOTE, BK_HR, BK_TABLE };

static md_font md_make_font(md_ctx *c, float size, cui_font reg, cui_font bold, float lh_mul) {
    md_font f;
    f.reg = reg; f.bold = bold; f.mono = CUI_FONT_MONO;
    f.size = cui__fs(c->ui, size);
    f.mono_size = f.size * (12.0f / 13.0f);
    f.lh = f.size * lh_mul;
    f.pad = 4.0f * f.size / 13.0f;
    return f;
}
static md_font md_body(md_ctx *c) { return md_make_font(c, c->body, CUI_FONT_BODY, CUI_FONT_MEDIUM, c->compact ? 1.5f : 1.6f); }

static bool md_tail(md_ctx *c, md_line last) { return c->streaming && last.off + last.len >= c->content_end; }
static bool md_open_line(md_ctx *c, md_line L) { return c->streaming && L.off + L.len >= c->len; }

static void md_space(md_ctx *c, float h) {
    if (h > 0) cui__leaf(c->ui, 0, CUI_PX(1), CUI_PX(h));
}

static float md_gap_before(md_ctx *c, const md_box *b, int prev, int kind, int level) {
    if (prev == BK_NONE) return 0;
    float g = b->tight ? 4 : (c->compact ? 8 : 12);
    if (kind == BK_HEADING) g = c->compact ? 12 : (level <= 2 ? 24 : 18);
    else if (prev == BK_HEADING) g = c->compact ? 4 : 8;
    if (kind == BK_HR || prev == BK_HR) g = c->compact ? 10 : 16;
    return g * c->u;
}

static cui_id md_link_id(md_ctx *c, int seq) { return cui__hash(&seq, sizeof seq, c->id ^ 0x3D11Au); }

/* Builds a laid out inline block: one painted node, a hit box per link piece. */
static float md_inline_emit(md_ctx *c, const md_inline *in, const md_font *f, cui_color fg, int align, float W) {
    cui_ctx *ui = c->ui;
    md_lay L;
    md_layout(ui, in, f, CUI_MAX(W, 24.0f), align, &L);
    int base = c->link_seq;
    c->link_seq += in->nlinks;
    float *hover = cui__frame_alloc(ui, sizeof(float) * (size_t)(in->nlinks * 2 + 1)), *focus = hover + in->nlinks;
    cui_id *ids = cui__frame_alloc(ui, sizeof(cui_id) * (size_t)(in->nlinks + 1));
    for (int k = 0; k < in->nlinks; k++) {
        const md_link *lk = &in->links[k];
        bool shown = false;
        for (int q = 0; q < L.n && !shown; q++) shown = L.seg[q].link == k;
        ids[k] = 0;
        if (!shown || lk->pending) continue;   /* nothing to click yet */
        cui_id id = md_link_id(c, base + k);
        ids[k] = id;
        cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
        float *st = cui_state(ui, id, sizeof(float) * 2);
        cui__tween(ui, &st[0], r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        cui__tween(ui, &st[1], r.focus_visible ? 1.0f : 0.0f, CUI__T_FOCUS);
        hover[k] = st[0]; focus[k] = st[1];
        const char *url = NULL;
        if (r.hovered || r.clicked) url = lk->www ? cui__frame_printf(ui, "http://%.*s", lk->len, lk->url) : cui__frame_strdup(ui, lk->url, lk->len);
        if (r.hovered) { cui__set_cursor(ui, CUI_CURSOR_HAND); c->res->hovered_link = url; }
        if (r.clicked) c->res->link = url;
        if (!lk->autolink) { ui->last_id = id; cui_tooltip(ui, r.hovered && url ? url : ""); }
    }
    int leaf = cui__leaf(ui, 0, CUI_FILL, CUI_PX((float)L.lines * f->lh));
    md_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->seg = L.seg; p->n = L.n; p->f = *f;
    p->fg = fg; p->link_c = c->custom_color ? fg : c->link_c; p->code_bg = c->code_bg;
    p->hover = hover; p->focus = focus;
    p->underline_rest = c->custom_color;
    ui->nodes[leaf].paint = md_paint_inline;
    ui->nodes[leaf].paint_data = p;
    if (in->nlinks > 0) {
        cui__open(ui, leaf);
        for (int q = 0; q < L.n; q++) {
            const md_seg *s = &L.seg[q];
            if (s->link < 0 || !ids[s->link]) continue;
            float x0 = s->x, x1 = s->x + s->w;
            while (q + 1 < L.n && L.seg[q + 1].link == s->link && L.seg[q + 1].line == s->line) { q++; x1 = L.seg[q].x + L.seg[q].w; }
            int a = cui__absolute(ui, ids[s->link], x0, (float)s->line * f->lh, CUI_MAX(1.0f, x1 - x0), f->lh);
            cui__mark_interactive(ui, a, CUI_IF_FOCUSABLE);
        }
        cui__close(ui);
    }
    return c->fit ? md_runs_width(ui, in, f) : 0;
}

static float md_inline_block(md_ctx *c, const char *s, int n, const md_font *f, cui_color fg, bool tail, float W) {
    md_inline in = md_parse_inline(c->ui, s, n, tail);
    return md_inline_emit(c, &in, f, fg, CUI_TEXT_LEFT, W);
}

/* Joins paragraph lines: soft breaks become spaces, hard breaks '\n'. */
static const char *md_join(md_ctx *c, const md_line *L, int n, int *out_len) {
    if (n == 1) {
        md_line T = md_trim(L[0]);
        *out_len = T.len;
        return T.s;
    }
    int total = n;
    for (int k = 0; k < n; k++) total += L[k].len;
    char *buf = cui__frame_alloc(c->ui, (size_t)total + 1);
    int o = 0;
    for (int k = 0; k < n; k++) {
        int b;
        md_indent(L[k], &b);
        md_line T = md_skip(L[k], b);
        int e = T.len, sp = 0;
        while (e > 0 && md_sp(T.s[e - 1])) { e--; sp++; }
        bool last = k == n - 1, hard = false;
        if (!last) {
            if (sp >= 2) hard = true;
            else if (sp == 0 && e > 0 && T.s[e - 1] == '\\') { hard = true; e--; }
        }
        memcpy(buf + o, T.s, (size_t)e);
        o += e;
        if (!last) buf[o++] = hard ? '\n' : ' ';
    }
    buf[o] = 0;
    *out_len = o;
    return buf;
}

static float md_paragraph(md_ctx *c, const md_box *b, const md_line *L, int n) {
    int len;
    const char *s = md_join(c, L, n, &len);
    md_font f = md_body(c);
    return md_inline_block(c, s, len, &f, b->fg, md_tail(c, L[n - 1]), b->w);
}

static float md_heading(md_ctx *c, const md_box *b, int level, const char *s, int len, bool tail) {
    cui_theme *t = &c->ui->theme;
    float scale = c->body / t->text_body;
    float size;
    if (c->compact) size = level == 1 ? t->heading[3] : level == 2 ? t->text_lg : t->text_body;
    else size = level == 1 ? t->heading[1] : level == 2 ? t->heading[2] : level == 3 ? t->heading[3] : level == 4 ? t->text_lg : t->text_body;
    size *= scale;
    cui_font font = size >= 26 ? CUI_FONT_DISPLAY : CUI_FONT_MEDIUM;
    md_font f = md_make_font(c, size, font, font, size >= 26 ? 1.2f : 1.35f);
    return md_inline_block(c, s, len, &f, level >= 6 ? c->fg2 : b->fg, tail, b->w);
}

static void md_lower(char *d, const char *s, int n) {
    for (int k = 0; k < n; k++) d[k] = (s[k] >= 'A' && s[k] <= 'Z') ? (char)(s[k] - 'A' + 'a') : s[k];
    d[n] = 0;
}

static float md_code(md_ctx *c, const md_line *L, int n, int strip, md_line lang) {
    cui_ctx *ui = c->ui;
    int total = n + 1;
    for (int k = 0; k < n; k++) total += L[k].len;
    char *buf = cui__frame_alloc(ui, (size_t)total);
    int o = 0;
    float widest = 0;
    for (int k = 0; k < n; k++) {
        md_line T = md_strip_cols(L[k], strip);
        memcpy(buf + o, T.s, (size_t)T.len);
        o += T.len;
        if (k < n - 1) buf[o++] = '\n';
        if (c->fit) widest = CUI_MAX(widest, cui__text_width(ui, CUI_FONT_MONO, cui__fs(ui, c->body), T.s, T.len, true));
    }
    buf[o] = 0;
    char *lg = cui__frame_alloc(ui, 24);
    if (lang.len > 0 && lang.len < 24) md_lower(lg, lang.s, lang.len);
    else memcpy(lg, "text", 5);
    cui_code_opts co;
    memset(&co, 0, sizeof co);
    co.lang = lg;
    co.size = c->body;
    cui_code_block_ex(ui, cui__frame_printf(ui, "code%d", c->code_seq++), buf, &co);
    return c->fit ? widest + 42 : 0;
}

/* ---- block starts ---- */

typedef struct { bool ordered; char ch; int num, w; } md_marker;

static bool md_list_marker(md_line T, md_marker *m) {
    if (T.len < 1) return false;
    char c0 = T.s[0];
    if (c0 == '-' || c0 == '*' || c0 == '+') {
        if (T.len > 1 && !md_sp(T.s[1])) return false;
        m->ordered = false; m->ch = c0; m->num = 0; m->w = 1;
        return true;
    }
    int i = 0, num = 0;
    while (i < T.len && i < 9 && T.s[i] >= '0' && T.s[i] <= '9') { num = num * 10 + (T.s[i] - '0'); i++; }
    if (i == 0 || i >= T.len || (T.s[i] != '.' && T.s[i] != ')')) return false;
    if (i + 1 < T.len && !md_sp(T.s[i + 1])) return false;
    m->ordered = true; m->ch = T.s[i]; m->num = num; m->w = i + 1;
    return true;
}

static bool md_hrule(md_line T) {
    char ch = 0;
    int k = 0;
    for (int i = 0; i < T.len; i++) {
        char x = T.s[i];
        if (md_sp(x)) continue;
        if (!ch) { if (x != '-' && x != '*' && x != '_') return false; ch = x; }
        else if (x != ch) return false;
        k++;
    }
    return k >= 3;
}

/* setext underline: 1 for ===, 2 for ---, else 0 */
static int md_setext(md_line T) {
    T = md_trim(T);
    if (T.len < 1 || (T.s[0] != '=' && T.s[0] != '-')) return 0;
    for (int i = 0; i < T.len; i++) if (T.s[i] != T.s[0]) return 0;
    return T.s[0] == '=' ? 1 : 2;
}

static bool md_atx(md_line T, int *level, md_line *content) {
    int k = 0;
    while (k < T.len && T.s[k] == '#') k++;
    if (k < 1 || k > 6 || (k < T.len && !md_sp(T.s[k]))) return false;
    md_line C = md_trim(md_skip(T, k));
    int h = C.len;
    while (h > 0 && C.s[h - 1] == '#') h--;
    if (h < C.len && (h == 0 || md_sp(C.s[h - 1]))) { C.len = h; while (C.len > 0 && md_sp(C.s[C.len - 1])) C.len--; }
    *level = k; *content = C;
    return true;
}

static bool md_fence(md_line T, char *ch, int *flen, md_line *info) {
    if (T.len < 3 || (T.s[0] != '`' && T.s[0] != '~')) return false;
    int k = 0;
    while (k < T.len && T.s[k] == T.s[0]) k++;
    if (k < 3) return false;
    md_line I = md_trim(md_skip(T, k));
    if (T.s[0] == '`' && md_has(I, '`')) return false;
    int w = 0;
    while (w < I.len && !md_sp(I.s[w])) w++;
    I.len = w;
    *ch = T.s[0]; *flen = k; *info = I;
    return true;
}

static bool md_fence_close(md_line L, char ch, int flen) {
    int b;
    if (md_indent(L, &b) >= 4) return false;
    md_line T = md_skip(L, b);
    int k = 0;
    while (k < T.len && T.s[k] == ch) k++;
    return k >= flen && md_blank(md_skip(T, k));
}

/* Does line L start a block that ends a paragraph? */
static bool md_interrupts(md_line L) {
    int b;
    if (md_blank(L) || md_indent(L, &b) >= 4) return false;
    md_line T = md_skip(L, b), I;
    char ch;
    int k;
    if (md_fence(T, &ch, &k, &I) || md_atx(T, &k, &I) || T.s[0] == '>' || md_hrule(T)) return true;
    md_marker m;
    if (md_list_marker(T, &m)) return !md_blank(md_skip(T, m.w)) && (!m.ordered || m.num == 1);
    return false;
}

static int md_cells(md_line T, md_line *cells, int max) {
    T = md_trim(T);
    int i = 0, n = T.len;
    if (i < n && T.s[i] == '|') i++;
    if (n > i && T.s[n - 1] == '|' && !(n - 2 >= i && T.s[n - 2] == '\\')) n--;
    int count = 0, st = i;
    for (int k = i; k <= n; k++) {
        if (k == n || (T.s[k] == '|' && !(k > i && T.s[k - 1] == '\\'))) {
            if (count < max) {
                md_line C = { T.s + st, k - st, T.off + st };
                cells[count] = md_trim(C);
            }
            count++;
            st = k + 1;
        }
    }
    return count;
}

/* Alignment row: column count, or 0. partial accepts an unfinished last cell. */
static int md_delim_row(md_line T, uint8_t *align, int max, bool partial) {
    md_line cells[MD_MAX_COLS];
    if (!md_has(T, '|') || (!md_has(T, '-') && !partial)) return 0;
    int n = md_cells(T, cells, MD_MAX_COLS);
    for (int k = 0; k < n && k < MD_MAX_COLS; k++) {
        md_line C = cells[k];
        bool left = C.len > 0 && C.s[0] == ':', right = C.len > 1 && C.s[C.len - 1] == ':';
        int dashes = 0;
        for (int q = left ? 1 : 0; q < C.len - (right ? 1 : 0); q++) { if (C.s[q] != '-') return 0; dashes++; }
        if (!dashes && !(partial && k == n - 1)) return 0;
        if (k < max) align[k] = (uint8_t)(left && right ? CUI_TEXT_CENTER : right ? CUI_TEXT_RIGHT : CUI_TEXT_LEFT);
    }
    return n;
}

/* A table starts at line i: header cells and their alignments. */
static int md_table_at(md_ctx *c, const md_line *L, int n, int i, uint8_t *align) {
    md_line H = L[i];
    if (md_blank(H) || md_indent(H, NULL) >= 4 || !md_has(H, '|')) return 0;
    md_line cells[MD_MAX_COLS];
    int nh = md_cells(H, cells, MD_MAX_COLS);
    for (int k = 0; k < MD_MAX_COLS; k++) align[k] = CUI_TEXT_LEFT;
    if (i + 1 < n && md_indent(L[i + 1], NULL) < 4) {
        bool partial = md_open_line(c, L[i + 1]);
        int nd = md_delim_row(L[i + 1], align, MD_MAX_COLS, partial);
        if (nd == nh || (partial && nd > 0 && nd <= nh)) return nh;
    }
    /* still arriving: a header row that waits for its alignment row */
    if (md_open_line(c, H) && md_trim(H).s[0] == '|') return nh;
    return 0;
}

static float md_blocks(md_ctx *c, const md_line *L, int n, const md_box *b);

static int md_list(md_ctx *c, const md_box *b, const md_line *L, int n, int i, float *nat) {
    cui_ctx *ui = c->ui;
    typedef struct { int start, end, content_col; md_line first; bool task, checked; int task_off; } md_item;
    md_item *items = cui__frame_alloc(ui, sizeof(md_item) * (size_t)(n - i + 1));
    int count = 0;
    bool loose = false;
    md_marker m0;
    memset(&m0, 0, sizeof m0);
    int j = i;
    while (j < n) {
        int bytes, ind = md_indent(L[j], &bytes);
        md_line T = md_skip(L[j], bytes);
        md_marker m;
        if (ind >= 4 || !md_list_marker(T, &m)) break;
        if (count > 0 && (m.ordered != m0.ordered || m.ch != m0.ch)) break;
        if (count == 0) m0 = m;
        md_item *it = &items[count++];
        md_line R = md_skip(T, m.w);
        int sb, sp = md_indent(R, &sb);
        if (md_blank(R)) { it->content_col = ind + m.w + 1; R = md_skip(R, R.len); }
        else if (sp > 4) { it->content_col = ind + m.w + 1; R = md_skip(R, 1); }
        else { it->content_col = ind + m.w + sp; R = md_skip(R, sb); }
        /* task item: [ ], [x] or [X] then a space or the end */
        it->task = false;
        if (R.len >= 3 && R.s[0] == '[' && R.s[2] == ']' && (R.s[1] == ' ' || R.s[1] == 'x' || R.s[1] == 'X') && (R.len == 3 || md_sp(R.s[3]))) {
            it->task = true; it->checked = R.s[1] != ' '; it->task_off = R.off + 1;
            R = md_skip(R, R.len > 3 ? 4 : 3);
        }
        it->first = R;
        it->start = j;
        int last = j;
        j++;
        while (j < n) {
            if (md_blank(L[j])) { j++; continue; }
            int ib, indj = md_indent(L[j], &ib);
            md_line Tj = md_skip(L[j], ib);
            md_marker mj;
            bool marker = indj < 4 + ind && md_list_marker(Tj, &mj);
            if (indj >= it->content_col) {}
            else if (marker && indj > ind) {}                 /* nested a little short of the content column */
            else if (j == last + 1 && !md_interrupts(L[j]) && !marker) {}   /* lazy continuation */
            else break;
            last = j;
            j++;
        }
        it->end = last + 1;
        for (int q = it->start + 1; q < it->end; q++) if (md_blank(L[q])) loose = true;
        /* the next item follows, maybe after blank lines */
        int k = it->end;
        while (k < n && md_blank(L[k])) k++;
        if (k >= n) { j = it->end; break; }
        int kb, indk = md_indent(L[k], &kb);
        md_marker mk;
        if (indk >= 4 || !md_list_marker(md_skip(L[k], kb), &mk) || mk.ordered != m0.ordered || mk.ch != m0.ch) { j = it->end; break; }
        if (k > it->end) loose = true;
        j = k;
    }
    if (count == 0) return i + 1;
    md_font f = md_body(c);
    float u = c->u;
    bool any_task = false;
    for (int k = 0; k < count; k++) any_task |= items[k].task;
    float colw = 18 * u;
    const char *last_num = NULL;
    if (m0.ordered) {
        last_num = cui__frame_printf(ui, "%d%c", m0.num + count - 1, m0.ch);
        colw = CUI_MAX(colw, cui__text_width(ui, f.reg, f.size, last_num, -1, true) + 9 * u);
    }
    if (any_task) colw = CUI_MAX(colw, 26.0f * CUI_MAX(1.0f, u));
    int list = cui__node_new(ui, 0);
    ui->nodes[list].w = CUI_FILL;
    ui->nodes[list].gap = (loose ? 10 : (c->compact ? 3 : 4)) * u;
    cui__open(ui, list);
    float best = 0;
    for (int k = 0; k < count; k++) {
        md_item *it = &items[k];
        int row = cui__node_new(ui, 0);
        cui_node *rn = &ui->nodes[row];
        rn->dir = CUI_ROW; rn->align = CUI_ALIGN_START; rn->w = CUI_FILL;
        cui__open(ui, row);
        int mk = cui__leaf(ui, 0, CUI_PX(colw), CUI_PX(f.lh));
        if (it->task) {
            cui_node *mn = &ui->nodes[mk];
            mn->dir = CUI_ROW; mn->align = CUI_ALIGN_CENTER;
            cui__open(ui, mk);
            bool v = it->checked;
            if (cui_checkbox_ex(ui, cui__frame_printf(ui, "##task%d", it->task_off), &v, NULL)) {
                c->res->task_offset = it->task_off;
                c->res->task_checked = v;
                int line = 1;
                for (int q = 0; q < it->task_off && q < c->len; q++) if (c->src[q] == '\n') line++;
                c->res->task_line = line;
            }
            cui__close(ui);
        } else {
            md_mark_paint *mp = cui__frame_alloc(ui, sizeof *mp);
            mp->kind = m0.ordered ? 1 : 0;
            mp->depth = b->depth;
            mp->c = c->marker_c;
            mp->f = f;
            if (m0.ordered) mp->num = cui__frame_printf(ui, "%d%c", m0.num + k, m0.ch);
            ui->nodes[mk].paint = md_paint_marker;
            ui->nodes[mk].paint_data = mp;
        }
        /* the item's lines with the list prefix removed */
        int nl = it->end - it->start;
        md_line *sub = cui__frame_alloc(ui, sizeof(md_line) * (size_t)nl);
        sub[0] = it->first;
        for (int q = 1; q < nl; q++) {
            md_line Lq = L[it->start + q];
            int qi = md_indent(Lq, NULL);
            sub[q] = md_blank(Lq) ? md_skip(Lq, Lq.len) : md_strip_cols(Lq, CUI_MIN(qi, it->content_col));
        }
        int col = cui__node_new(ui, 0);
        ui->nodes[col].w = CUI_FILL;
        cui__open(ui, col);
        md_box cb = { b->w - colw, b->depth + 1, !loose, it->task && it->checked ? c->fg2 : b->fg };
        float w = md_blocks(c, sub, nl, &cb);
        cui__close(ui);
        cui__close(ui);
        best = CUI_MAX(best, w + colw);
    }
    cui__close(ui);
    if (nat) *nat = best;
    return j;
}

static int md_quote(md_ctx *c, const md_box *b, const md_line *L, int n, int i, float *nat) {
    cui_ctx *ui = c->ui;
    md_line *sub = cui__frame_alloc(ui, sizeof(md_line) * (size_t)(n - i));
    int count = 0, j = i;
    bool prev_text = false;
    while (j < n) {
        int bytes, ind = md_indent(L[j], &bytes);
        md_line T = md_skip(L[j], bytes);
        if (ind < 4 && T.len > 0 && T.s[0] == '>') {
            T = md_skip(T, 1);
            if (T.len > 0 && md_sp(T.s[0])) T = md_skip(T, 1);
            sub[count++] = T;
            prev_text = !md_blank(T);
        } else if (prev_text && !md_blank(L[j]) && !md_interrupts(L[j])) {
            sub[count++] = L[j];   /* lazy continuation of a quoted paragraph */
        } else break;
        j++;
    }
    float u = c->u;
    int q = cui__node_new(ui, 0);
    cui_node *qn = &ui->nodes[q];
    qn->w = CUI_FILL;
    qn->pad[3] = 14 * u;
    cui_color *rc = cui__frame_alloc(ui, sizeof *rc);
    *rc = c->rail_c;
    qn->paint = md_paint_rail;
    qn->paint_data = rc;
    cui__open(ui, q);
    md_box qb = { b->w - 14 * u, b->depth + 1, false, c->fg2 };
    float w = md_blocks(c, sub, count, &qb);
    cui__close(ui);
    if (nat) *nat = w + 14 * u;
    return j;
}

static int md_table(md_ctx *c, const md_box *b, const md_line *L, int n, int i, int ncols, const uint8_t *align, float *nat) {
    cui_ctx *ui = c->ui;
    if (ncols > MD_MAX_COLS) ncols = MD_MAX_COLS;
    bool has_delim = i + 1 < n && md_delim_row(L[i + 1], (uint8_t[MD_MAX_COLS]){ 0 }, MD_MAX_COLS, md_open_line(c, L[i + 1])) > 0;
    int j = i + (has_delim ? 2 : 1);
    while (j < n && !md_blank(L[j]) && md_has(L[j], '|')) j++;
    int nrows = j - i - (has_delim ? 2 : 1);
    md_font f = md_body(c);
    md_font hf = md_make_font(c, c->ui->theme.text_sm * c->body / c->ui->theme.text_body, CUI_FONT_MEDIUM, CUI_FONT_MEDIUM, 1.3f);
    md_inline *cells = cui__frame_alloc(ui, sizeof(md_inline) * (size_t)((nrows + 1) * ncols));
    int *rowc = cui__frame_alloc(ui, sizeof(int) * (size_t)(nrows + 1));
    float *colw = cui__frame_alloc(ui, sizeof(float) * (size_t)ncols * 2), *floor_w = colw + ncols;
    md_line raw[MD_MAX_COLS];
    for (int r = 0; r <= nrows; r++) {
        md_line R = L[r == 0 ? i : i + (has_delim ? 2 : 1) + r - 1];
        int nc = md_cells(R, raw, MD_MAX_COLS);
        rowc[r] = CUI_MIN(nc, ncols);
        bool tail = r == nrows && md_tail(c, R);
        for (int k = 0; k < rowc[r]; k++) {
            md_inline *in = &cells[r * ncols + k];
            *in = md_parse_inline(ui, raw[k].s, raw[k].len, tail && k == rowc[r] - 1);
            float w = md_runs_width(ui, in, r == 0 ? &hf : &f);
            if (r == 0) { floor_w[k] = w + 16; colw[k] = w + 16; }
            else colw[k] = CUI_MAX(colw[k], w + 16);
        }
    }
    float total = 18, u = c->u;
    for (int k = 0; k < ncols; k++) {
        floor_w[k] = CUI_MAX(floor_w[k], 48.0f * u);
        colw[k] = cui__clampf(colw[k], 48.0f * u, 360.0f * u);
        total += colw[k];
    }
    float natural = total;
    float avail = b->w;
    if (total < avail) {   /* fill the width, wider columns taking more */
        float extra = avail - total, sum = total - 18;
        for (int k = 0; k < ncols; k++) colw[k] += extra * colw[k] / sum;
    } else if (total > avail) {   /* narrow: long cells wrap first, then the table scrolls */
        for (int pass = 0; pass < 4 && total > avail + 0.5f; pass++) {
            float give = 0;
            for (int k = 0; k < ncols; k++) give += CUI_MAX(0.0f, colw[k] - CUI_MAX(floor_w[k], 120.0f * u));
            if (give <= 0) break;
            float need = CUI_MIN(give, total - avail);
            total = 18;
            for (int k = 0; k < ncols; k++) {
                float g = CUI_MAX(0.0f, colw[k] - CUI_MAX(floor_w[k], 120.0f * u));
                colw[k] -= need * g / give;
                total += colw[k];
            }
        }
    }
    cui_table_column *cols = cui__frame_alloc(ui, sizeof(cui_table_column) * (size_t)ncols);
    for (int k = 0; k < ncols; k++) {
        cols[k].title = k < rowc[0] ? md_plain(ui, &cells[k]) : "";
        cols[k].width = CUI_PX(colw[k]);
        cols[k].align = (cui_text_align)align[k];
    }
    cui_table_opts to;
    memset(&to, 0, sizeof to);
    to.compact = true;
    cui_table_begin_ex(ui, cui__frame_printf(ui, "table%d", c->table_seq++), cols, ncols, &to);
    for (int r = 1; r <= nrows; r++) {
        cui_table_row(ui, false);
        for (int k = 0; k < ncols; k++) {
            cui_table_cell(ui);
            if (k < rowc[r]) md_inline_emit(c, &cells[r * ncols + k], &f, b->fg, align[k], colw[k] - 16);
        }
    }
    cui_table_end(ui);
    if (nat) *nat = natural;
    return j;
}

static float md_blocks(md_ctx *c, const md_line *L, int n, const md_box *b) {
    cui_ctx *ui = c->ui;
    float best = 0;
    if (b->depth > MD_MAX_DEPTH) {   /* too deep: the rest is text */
        md_line *keep = cui__frame_alloc(ui, sizeof(md_line) * (size_t)(n + 1));
        int k = 0;
        for (int q = 0; q < n; q++) if (!md_blank(L[q])) keep[k++] = L[q];
        if (k) best = md_paragraph(c, b, keep, k);
        return best;
    }
    int i = 0, prev = BK_NONE;
    while (i < n) {
        md_line Li = L[i];
        if (md_blank(Li)) { i++; continue; }
        int bytes, ind = md_indent(Li, &bytes);
        md_line T = md_skip(Li, bytes), info, content;
        float w = 0;
        char fch;
        int flen, level;
        uint8_t align[MD_MAX_COLS];
        md_marker m;
        if (ind >= 4) {   /* indented code */
            int j = i, last = i;
            while (j < n && (md_blank(L[j]) || md_indent(L[j], NULL) >= 4)) { if (!md_blank(L[j])) last = j; j++; }
            md_space(c, md_gap_before(c, b, prev, BK_CODE, 0));
            md_line none = { "", 0, 0 };
            w = md_code(c, L + i, last - i + 1, 4, none);
            prev = BK_CODE;
            i = last + 1;
        } else if (md_fence(T, &fch, &flen, &info)) {
            int j = i + 1;
            while (j < n && !md_fence_close(L[j], fch, flen)) j++;
            md_space(c, md_gap_before(c, b, prev, BK_CODE, 0));
            w = md_code(c, L + i + 1, j - i - 1, ind, info);
            prev = BK_CODE;
            i = j < n ? j + 1 : n;
        } else if (md_atx(T, &level, &content)) {
            md_space(c, md_gap_before(c, b, prev, BK_HEADING, level));
            w = md_heading(c, b, level, content.s, content.len, md_tail(c, Li));
            prev = BK_HEADING;
            i++;
        } else if (md_hrule(T)) {
            md_space(c, md_gap_before(c, b, prev, BK_HR, 0));
            int r = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
            ui->nodes[r].bg = c->rule_c;
            prev = BK_HR;
            i++;
        } else if (T.s[0] == '>') {
            md_space(c, md_gap_before(c, b, prev, BK_QUOTE, 0));
            i = md_quote(c, b, L, n, i, &w);
            prev = BK_QUOTE;
        } else if (md_list_marker(T, &m)) {
            md_space(c, md_gap_before(c, b, prev, BK_LIST, 0));
            i = md_list(c, b, L, n, i, &w);
            prev = BK_LIST;
        } else if ((level = md_table_at(c, L, n, i, align)) > 0) {
            md_space(c, md_gap_before(c, b, prev, BK_TABLE, 0));
            i = md_table(c, b, L, n, i, level, align, &w);
            prev = BK_TABLE;
        } else {   /* paragraph, or a setext heading */
            int j = i + 1, setext = 0;
            while (j < n && !md_blank(L[j])) {
                if (md_indent(L[j], NULL) < 4 && (setext = md_setext(L[j])) > 0) break;
                if (md_interrupts(L[j]) || md_table_at(c, L, n, j, align)) break;
                j++;
            }
            if (setext && md_open_line(c, L[j])) {   /* the underline may still be a list or a rule: wait for its line */
                md_space(c, md_gap_before(c, b, prev, BK_PARA, 0));
                w = md_paragraph(c, b, L + i, j - i);
                prev = BK_PARA;
                i = j + 1;
            } else if (setext) {
                int len;
                const char *s = md_join(c, L + i, j - i, &len);
                md_space(c, md_gap_before(c, b, prev, BK_HEADING, setext));
                w = md_heading(c, b, setext, s, len, false);
                prev = BK_HEADING;
                i = j + 1;
            } else {
                md_space(c, md_gap_before(c, b, prev, BK_PARA, 0));
                w = md_paragraph(c, b, L + i, j - i);
                prev = BK_PARA;
                i = j;
            }
        }
        best = CUI_MAX(best, w);
    }
    return best;
}

/* ------------------------------------------------------------------------ */
/* Public                                                                   */
/* ------------------------------------------------------------------------ */

cui_markdown_result cui_markdown_ex(cui_ctx *ui, const char *id_str, const char *text, const cui_markdown_opts *o) {
    cui_markdown_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_markdown_result res;
    memset(&res, 0, sizeof res);
    res.task_offset = -1;
    if (!text) text = "";
    if (!id_str) id_str = "markdown";
    int len = o->length > 0 ? o->length : (int)strlen(text);
    cui_id id = cui_make_id(ui, id_str);

    md_ctx c;
    memset(&c, 0, sizeof c);
    c.ui = ui; c.id = id; c.src = text; c.len = len; c.res = &res;
    c.streaming = o->streaming; c.compact = o->compact; c.fit = o->fit_content;
    c.body = o->size > 0 ? o->size : ui->theme.text_body;
    c.u = c.body / 13.0f;
    c.content_end = len;
    while (c.content_end > 0 && md_ws((unsigned char)text[c.content_end - 1])) c.content_end--;
    c.custom_color = cui__color_set(o->color);
    if (c.custom_color) {
        c.fg = cui_color_of(ui, o->color);
        c.fg2 = cui_alpha(c.fg, 0.78f);
        c.link_c = c.fg;
        c.code_bg = cui_alpha(c.fg, 0.14f);
        c.marker_c = cui_alpha(c.fg, 0.7f);
        c.rule_c = cui_alpha(c.fg, 0.22f);
        c.rail_c = cui_alpha(c.fg, 0.35f);
    } else {
        c.fg = CUI_C(ui, FOREGROUND);
        c.fg2 = CUI_C(ui, TEXT_SECONDARY);
        c.link_c = cui_mix(CUI_C(ui, ACCENT_STRONG), CUI_C(ui, FOREGROUND), ui->accent == CUI_ACCENT_NEUTRAL ? 1.0f : 0.0f);
        c.code_bg = CUI_C(ui, ACTIVE);
        c.marker_c = CUI_C(ui, TEXT_SECONDARY);
        c.rule_c = CUI_C(ui, BORDER);
        c.rail_c = cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), 0.45f);
    }

    /* wrap at the width of the latest layout (exact when the width is fixed) */
    float W;
    cui_rect pr = cui__prev_rect(ui, id);
    if (o->width.kind == CUI_SIZE_FIXED) W = o->width.value;
    else if (cui__has_prev_rect(ui, id) && pr.w > 0) W = pr.w;
    else { W = CUI_MIN(ui->vw, 640.0f); ui->animating = true; }
    res.height = pr.h;

    int root = cui__node_new(ui, id);
    cui_node *rn = &ui->nodes[root];
    if (c.fit) { rn->w = CUI_FIT; rn->self_align = CUI__ALIGN_NOSTRETCH; }
    else rn->w = o->width.kind != CUI_SIZE_FIT ? o->width : CUI_FILL;
    md_root_paint *rp = cui__frame_alloc(ui, sizeof *rp);
    rp->w = W;
    rn->paint = md_paint_root;
    rn->paint_data = rp;
    cui__open(ui, root);
    cui_push_id(ui, id_str);

    int nlines = 1;
    for (int k = 0; k < len; k++) if (text[k] == '\n') nlines++;
    md_line *lines = cui__frame_alloc(ui, sizeof(md_line) * (size_t)nlines);
    int n = 0, st = 0;
    for (int k = 0; k <= len; k++) {
        if (k < len && text[k] != '\n') continue;
        int e = k;
        if (e > st && text[e - 1] == '\r') e--;
        if (!(k == len && st == len && n > 0)) { lines[n].s = text + st; lines[n].len = e - st; lines[n].off = st; n++; }
        st = k + 1;
    }
    md_box box = { W, 0, false, c.fg };
    float natural = md_blocks(&c, lines, len > 0 ? n : 0, &box);

    cui_pop_id(ui);
    cui__close(ui);
    if (c.fit) ui->nodes[root].pref_hint = CUI_MAX(1.0f, CUI_MIN(natural, 1e6f));
    ui->last_id = id;
    return res;
}

bool cui_markdown_apply_task(char *text, const cui_markdown_result *r) {
    if (!text || !r || r->task_offset < 1) return false;
    int off = r->task_offset;
    for (int k = 0; k <= off; k++) if (!text[k]) return false;   /* past the end */
    if (text[off - 1] != '[' || text[off + 1] != ']') return false;
    if (text[off] != ' ' && text[off] != 'x' && text[off] != 'X') return false;
    text[off] = r->task_checked ? 'x' : ' ';
    return true;
}
