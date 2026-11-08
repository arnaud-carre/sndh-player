/* Diff viewer: a line diff of two texts (Myers' O(ND) algorithm in linear
 * space, with a cost limit), cached until the texts change, shown side by
 * side or inline with word-level marks inside changed lines. */
#include "cui_internal.h"

#define cui__fs cui__fsz

#define CUI__DIFF_SALT        0xD1FF0u
#define CUI__DIFF_BUDGET      (1LL << 24)   /* search steps for the line diff, then coarse */
#define CUI__DIFF_WORD_BUDGET (1LL << 22)   /* search steps for every word diff together */
#define CUI__DIFF_WORD_LINE   2000          /* longer lines get no word marks */
#define CUI__DIFF_WORD_TOKENS 512           /* nor lines with more words than this */
#define CUI__DIFF_WORD_RANGES 64            /* marks kept per line */
#define CUI__DIFF_DRAW_MAX    1024          /* bytes of a line that are drawn */
#define CUI__DIFF_FOLD_MIN    3             /* a stretch folds when it hides this many lines */
#define CUI__DIFF_SPLIT_MIN   560.0f        /* narrower than this, split shows inline */
#define CUI__DIFF_INSET       4.0f          /* above the first row and below the last */
#define CUI__DIFF_MAX_LEN     0x3FFFFFF0u   /* bytes of each text that are compared (1 GiB) */

/* ------------------------------------------------------------------------ */
/* Memory                                                                   */
/* ------------------------------------------------------------------------ */

static void *cui__df_std_alloc(size_t n, void *u) { (void)u; return malloc(n); }
static void *cui__df_std_realloc(void *p, size_t n, void *u) { (void)u; return realloc(p, n); }
static void cui__df_std_free(void *p, void *u) { (void)u; free(p); }
static const cui_allocator cui__df_std = { cui__df_std_alloc, cui__df_std_realloc, cui__df_std_free, NULL };

static void *cui__df_alloc(const cui_allocator *A, size_t n) { return A->alloc(n ? n : 1, A->user); }
static void *cui__df_calloc(const cui_allocator *A, size_t n) {
    void *p = cui__df_alloc(A, n);
    if (p) memset(p, 0, n ? n : 1);
    return p;
}
static void cui__df_free(const cui_allocator *A, void *p) { if (p) A->free(p, A->user); }
/* grows *p to hold need items of size sz; false when out of memory */
static bool cui__df_reserve(const cui_allocator *A, void **p, int *cap, int need, size_t sz) {
    if (need <= *cap) return true;
    int ncap = *cap ? *cap : 64;
    while (ncap < need) ncap = ncap > 0x3FFFFFFF ? need : ncap * 2;
    void *np = A->realloc(*p, (size_t)ncap * sz, A->user);
    if (!np) return false;
    *p = np; *cap = ncap;
    return true;
}

static uint32_t cui__df_hash(const char *s, int n, uint32_t h) {
    for (int i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

/* ------------------------------------------------------------------------ */
/* Myers                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    const cui_allocator *A;
    int *v; int vcap;              /* the forward and reverse frontiers */
    int *stack; int sn, scap;      /* ranges still to compare, 4 ints each */
    long long work, budget;
    bool approx;
} cui__dmyers;

static void cui__df_myers_free(cui__dmyers *M) {
    cui__df_free(M->A, M->v);
    cui__df_free(M->A, M->stack);
    M->v = M->stack = NULL;
    M->vcap = M->scap = M->sn = 0;
}

/* The middle snake of a[0..n) and b[0..m) (Myers 1986, section 4b): returns 1
 * with the split point, 0 when the two share nothing, -1 when the cost limit
 * stopped the search (the split is then the furthest point either end reached). */
static int cui__df_bisect(cui__dmyers *M, const int *a, int n, const int *b, int m, int dlim, int *sx, int *sy) {
    int max_d = (n + m + 1) / 2;
    int lim = max_d < dlim ? max_d : dlim;
    int off = lim + 1, len = 2 * lim + 3;
    int *v1 = M->v, *v2 = M->v + len;
    for (int i = 0; i < len; i++) v1[i] = v2[i] = -1;
    v1[off + 1] = 0;
    v2[off + 1] = 0;
    int delta = n - m;
    bool front = delta % 2 != 0;
    int k1s = 0, k1e = 0, k2s = 0, k2e = 0;
    bool cut = false;
    for (int d = 0; d < lim; d++) {
        for (int k1 = -d + k1s; k1 <= d - k1e; k1 += 2) {
            int ko = off + k1, x1;
            if (k1 == -d || (k1 != d && v1[ko - 1] < v1[ko + 1])) x1 = v1[ko + 1];
            else x1 = v1[ko - 1] + 1;
            int y1 = x1 - k1, x0 = x1;
            while (x1 < n && y1 < m && a[x1] == b[y1]) { x1++; y1++; }
            M->work += 1 + (x1 - x0);
            v1[ko] = x1;
            if (x1 > n) k1e += 2;
            else if (y1 > m) k1s += 2;
            else if (front) {
                int k2o = off + delta - k1;
                if (k2o >= 0 && k2o < len && v2[k2o] != -1 && x1 >= n - v2[k2o]) { *sx = x1; *sy = y1; return 1; }
            }
        }
        for (int k2 = -d + k2s; k2 <= d - k2e; k2 += 2) {
            int ko = off + k2, x2;
            if (k2 == -d || (k2 != d && v2[ko - 1] < v2[ko + 1])) x2 = v2[ko + 1];
            else x2 = v2[ko - 1] + 1;
            int y2 = x2 - k2, x0 = x2;
            while (x2 < n && y2 < m && a[n - x2 - 1] == b[m - y2 - 1]) { x2++; y2++; }
            M->work += 1 + (x2 - x0);
            v2[ko] = x2;
            if (x2 > n) k2e += 2;
            else if (y2 > m) k2s += 2;
            else if (!front) {
                int k1o = off + delta - k2;
                if (k1o >= 0 && k1o < len && v1[k1o] != -1) {
                    int x1 = v1[k1o], y1 = off + x1 - k1o;
                    if (x1 >= n - x2) { *sx = x1; *sy = y1; return 1; }
                }
            }
        }
        if (M->work > M->budget) { cut = true; break; }
    }
    if (!cut && lim == max_d) return 0;
    /* the cost limit: split where a search got furthest. Any split is a valid
     * diff, just not always the shortest. */
    int best = -1, bx = 0, by = 0;
    for (int i = 0; i < len; i++) {
        int k = i - off;
        if (v1[i] >= 0) {
            int x = v1[i], y = x - k;
            if (x <= n && y >= 0 && y <= m && x + y > best) { best = x + y; bx = x; by = y; }
        }
        if (v2[i] >= 0) {
            int x = v2[i], y = x - k;
            if (x <= n && y >= 0 && y <= m && x + y > best) { best = x + y; bx = n - x; by = m - y; }
        }
    }
    *sx = bx; *sy = by;
    return -1;
}

static bool cui__df_push(cui__dmyers *M, int a0, int a1, int b0, int b1) {
    if (!cui__df_reserve(M->A, (void **)&M->stack, &M->scap, M->sn + 4, sizeof(int))) return false;
    int *s = M->stack + M->sn;
    s[0] = a0; s[1] = a1; s[2] = b0; s[3] = b1;
    M->sn += 4;
    return true;
}

/* Marks (ca, cb) the items of a and b that are not in a longest common
 * subsequence. Past the budget the rest is marked as replaced. */
static bool cui__df_myers(cui__dmyers *M, const int *a, int na, const int *b, int nb, uint8_t *ca, uint8_t *cb) {
    int dlim = 256;   /* per split, as in xdiff: at least 256 steps, more for long inputs */
    while ((long long)dlim * dlim < (long long)na + nb) dlim *= 2;
    int full = (na + nb + 1) / 2;
    if (dlim > full) dlim = full;
    if (dlim < 1) dlim = 1;
    if (!cui__df_reserve(M->A, (void **)&M->v, &M->vcap, 2 * (2 * dlim + 3), sizeof(int))) return false;
    M->sn = 0;
    if (!cui__df_push(M, 0, na, 0, nb)) return false;
    while (M->sn > 0) {
        M->sn -= 4;
        int *s = M->stack + M->sn;
        int a0 = s[0], a1 = s[1], b0 = s[2], b1 = s[3];
        int t0 = a0;
        while (a0 < a1 && b0 < b1 && a[a0] == b[b0]) { a0++; b0++; }
        while (a0 < a1 && b0 < b1 && a[a1 - 1] == b[b1 - 1]) { a1--; b1--; }
        M->work += 1 + (a0 - t0);
        if (a0 == a1) { if (b1 > b0) memset(cb + b0, 1, (size_t)(b1 - b0)); continue; }
        if (b0 == b1) { memset(ca + a0, 1, (size_t)(a1 - a0)); continue; }
        int x = 0, y = 0, r = 0;
        if (M->work < M->budget) r = cui__df_bisect(M, a + a0, a1 - a0, b + b0, b1 - b0, dlim, &x, &y);
        else M->approx = true;
        if (r < 0) M->approx = true;
        if (r == 0 || (x == 0 && y == 0) || (x == a1 - a0 && y == b1 - b0)) {
            memset(ca + a0, 1, (size_t)(a1 - a0));
            memset(cb + b0, 1, (size_t)(b1 - b0));
            continue;
        }
        if (!cui__df_push(M, a0 + x, a1, b0 + y, b1) || !cui__df_push(M, a0, a0 + x, b0, b0 + y)) return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Change blocks                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { int a0, na, b0, nb; } cui__dblock;   /* old lines [a0, a0+na) became new lines [b0, b0+nb) */

/* the change blocks of the marks; out may be NULL to count them */
static int cui__df_blocks(const uint8_t *ca, int na, const uint8_t *cb, int nb, cui__dblock *out) {
    int i = 0, j = 0, count = 0;
    while (i < na || j < nb) {
        if ((i < na && ca[i]) || (j < nb && cb[j]) || i >= na || j >= nb) {
            int i0 = i, j0 = j;
            while (i < na && ca[i]) i++;
            while (j < nb && cb[j]) j++;
            if (i == i0 && j == j0) {   /* unbalanced marks (never from the diff): the rest is a change */
                i = na; j = nb;
            }
            if (out) { out[count].a0 = i0; out[count].na = i - i0; out[count].b0 = j0; out[count].nb = j - j0; }
            count++;
        } else { i++; j++; }
    }
    return count;
}

/* Insertions and deletions next to equal lines can slide along them without
 * changing the diff (an added function and the blank line after it). Each
 * goes as low as it can, and back up to the lowest spot that ends on a blank
 * line, the way people read a hunk. Replacements stay. */
static void cui__df_slide(cui__dblock *bl, int nbl, const int *ids[2], const uint8_t *blank[2], int n0) {
    for (int k = 0; k < nbl; k++) {
        cui__dblock *B = &bl[k];
        if ((B->na > 0) == (B->nb > 0)) continue;
        int side = B->na ? 0 : 1, cnt = side ? B->nb : B->na;
        const int *X = ids[side];
        int prev_end = k ? bl[k - 1].a0 + bl[k - 1].na : 0;
        int next_start = k + 1 < nbl ? bl[k + 1].a0 : n0;
        int before = B->a0 - prev_end, after = next_start - (B->a0 + B->na);
#define S_ (side ? B->b0 : B->a0)
        while (before > 0 && X[S_ - 1] == X[S_ + cnt - 1]) { B->a0--; B->b0--; before--; after++; }
        int step = 0, best = blank[side][S_ + cnt - 1] ? 0 : -1;
        while (after > 0 && X[S_] == X[S_ + cnt]) {
            B->a0++; B->b0++; before++; after--; step++;
            if (blank[side][S_ + cnt - 1]) best = step;
        }
#undef S_
        if (best >= 0) { B->a0 -= step - best; B->b0 -= step - best; }
    }
}

/* ------------------------------------------------------------------------ */
/* Lines                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct { uint32_t off, len; uint8_t eol, mark; } cui__dline;   /* eol: 0 none, 1 "\n", 2 "\r\n"; len without it */
enum { CUI__DL_EOL_TAG = 1 };   /* mark: the pair differs only in its line ending */

static int cui__df_count_lines(const char *s, size_t n) {
    int count = 0;
    const char *p = s, *end = s + n;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        count++;
        if (!nl) break;
        p = nl + 1;
    }
    return count;
}

static void cui__df_fill_lines(const char *s, size_t n, cui__dline *out) {
    size_t i = 0;
    int k = 0;
    while (i < n) {
        const char *nl = memchr(s + i, '\n', n - i);
        size_t e = nl ? (size_t)(nl - s) : n;
        cui__dline *L = &out[k++];
        L->off = (uint32_t)i;
        L->eol = nl ? 1 : 0;
        L->mark = 0;
        size_t len = e - i;
        if (nl && len > 0 && s[e - 1] == '\r') { len--; L->eol = 2; }
        L->len = (uint32_t)len;
        i = nl ? e + 1 : n;
    }
}

/* Equal lines get equal ids (a hash table of the distinct lines). */
static bool cui__df_intern(const cui_allocator *A, const char *const t[2], const cui__dline *const L[2], const int n[2],
                           bool ignore_eol, int *const ids[2]) {
    size_t total = (size_t)n[0] + (size_t)n[1];
    size_t cap = 64;
    while (cap < total * 2) cap *= 2;
    int *slot = cui__df_alloc(A, cap * sizeof(int) * 2);   /* line reference (side << 31 | index) + 1, and its id */
    uint32_t *hs = cui__df_alloc(A, cap * sizeof(uint32_t));
    if (!slot || !hs) { cui__df_free(A, slot); cui__df_free(A, hs); return false; }
    for (size_t i = 0; i < cap; i++) slot[2 * i] = 0;
    int next_id = 0;
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < n[side]; i++) {
            const cui__dline *l = &L[side][i];
            const char *s = t[side] + l->off;
            uint8_t eol = ignore_eol ? 0 : l->eol;
            uint32_t h = cui__df_hash(s, (int)l->len, 2166136261u) ^ (uint32_t)eol * 0x9E3779B1u;
            size_t at = h & (cap - 1);
            for (;;) {
                int ref = slot[2 * at];
                if (!ref) {
                    slot[2 * at] = (int)(((unsigned)side << 30) | (unsigned)i) + 1;
                    slot[2 * at + 1] = next_id;
                    hs[at] = h;
                    ids[side][i] = next_id++;
                    break;
                }
                if (hs[at] == h) {
                    int rs = (ref - 1) >> 30, ri = (ref - 1) & 0x3FFFFFFF;
                    const cui__dline *o = &L[rs][ri];
                    uint8_t oe = ignore_eol ? 0 : o->eol;
                    if (o->len == l->len && oe == eol && memcmp(t[rs] + o->off, s, l->len) == 0) { ids[side][i] = slot[2 * at + 1]; break; }
                }
                at = (at + 1) & (cap - 1);
            }
        }
    }
    cui__df_free(A, slot);
    cui__df_free(A, hs);
    return true;
}

static bool cui__df_blank(const char *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return false;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Words inside a changed line                                              */
/* ------------------------------------------------------------------------ */

static bool cui__df_word_char(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80; }

/* Words, runs of spaces and single punctuation. Bytes from 0x80 join words,
 * so a mark never splits a character (valid UTF-8 or not). */
static int cui__df_tokenize(const char *s, int n, int *start, int cap) {
    int count = 0, i = 0;
    while (i < n) {
        if (count >= cap) return -1;
        start[count++] = i;
        uint8_t c = (uint8_t)s[i];
        if (cui__df_word_char(c)) { while (i < n && cui__df_word_char((uint8_t)s[i])) i++; }
        else if (c == ' ' || c == '\t') { while (i < n && (s[i] == ' ' || s[i] == '\t')) i++; }
        else i++;
    }
    start[count] = n;
    return count;
}

typedef struct { int start, len; } cui__drange;

/* Word marks for a pair of lines. Returns false when the lines have too little
 * in common for marks to help (the line tint says it all). */
static bool cui__df_words(cui__dmyers *M, const char *a, int an, const char *b, int bn,
                          cui__drange *ra, int *nra, cui__drange *rb, int *nrb, int cap) {
    *nra = *nrb = 0;
    if (an > CUI__DIFF_WORD_LINE || bn > CUI__DIFF_WORD_LINE) return false;
    int sa[CUI__DIFF_WORD_TOKENS + 1], sb[CUI__DIFF_WORD_TOKENS + 1];
    int ta = cui__df_tokenize(a, an, sa, CUI__DIFF_WORD_TOKENS), tb = cui__df_tokenize(b, bn, sb, CUI__DIFF_WORD_TOKENS);
    if (ta < 0 || tb < 0) return false;
    int ia[CUI__DIFF_WORD_TOKENS], ib[CUI__DIFF_WORD_TOKENS];
    uint8_t ca[CUI__DIFF_WORD_TOKENS], cb[CUI__DIFF_WORD_TOKENS];
    /* ids: equal words get equal ids (a small open table of token references) */
    enum { TCAP = 2048 };
    int tref[TCAP];
    uint32_t th[TCAP];
    memset(tref, 0, sizeof tref);
    int next_id = 0;
    for (int side = 0; side < 2; side++) {
        const char *s = side ? b : a;
        const int *st = side ? sb : sa;
        int nt = side ? tb : ta;
        int *ids = side ? ib : ia;
        for (int k = 0; k < nt; k++) {
            int len = st[k + 1] - st[k];
            uint32_t h = cui__df_hash(s + st[k], len, 2166136261u);
            int at = (int)(h & (TCAP - 1));
            for (;;) {
                if (!tref[at]) { tref[at] = ((side << 15) | k) + 1; th[at] = h; ids[k] = next_id++; break; }
                if (th[at] == h) {
                    int rs = (tref[at] - 1) >> 15, rk = (tref[at] - 1) & 0x7FFF;
                    const char *os = rs ? b : a;
                    const int *ost = rs ? sb : sa;
                    int olen = ost[rk + 1] - ost[rk];
                    if (olen == len && memcmp(os + ost[rk], s + st[k], (size_t)len) == 0) { ids[k] = (rs ? ib : ia)[rk]; break; }
                }
                at = (at + 1) & (TCAP - 1);
            }
        }
    }
    memset(ca, 0, sizeof ca);
    memset(cb, 0, sizeof cb);
    long long budget = M->budget;
    M->budget = M->work + 200000;   /* one pair never takes long */
    if (M->budget > budget) M->budget = budget;
    bool ok = cui__df_myers(M, ia, ta, ib, tb, ca, cb);
    M->budget = budget;
    if (!ok) return false;
    /* how much is shared, ignoring spaces */
    int same = 0, tot = 0;
    for (int side = 0; side < 2; side++) {
        const char *s = side ? b : a;
        const int *st = side ? sb : sa;
        const uint8_t *cf = side ? cb : ca;
        int nt = side ? tb : ta;
        for (int k = 0; k < nt; k++) {
            if (s[st[k]] == ' ' || s[st[k]] == '\t') continue;
            int len = st[k + 1] - st[k];
            tot += len;
            if (!cf[k]) same += len;
        }
    }
    if (tot > 0 && same * 10 < tot * 3) return false;
    for (int side = 0; side < 2; side++) {
        const char *s = side ? b : a;
        const int *st = side ? sb : sa;
        const uint8_t *cf = side ? cb : ca;
        int nt = side ? tb : ta;
        cui__drange *out = side ? rb : ra;
        int *no = side ? nrb : nra;
        for (int k = 0; k < nt;) {
            if (!cf[k]) { k++; continue; }
            int k0 = k;
            while (k < nt && cf[k]) k++;
            int r0 = st[k0], r1 = st[k];
            /* two marks with only a space between read as one */
            if (*no > 0) {
                cui__drange *last = &out[*no - 1];
                int gap0 = last->start + last->len;
                bool spaces = gap0 < r0;
                for (int i = gap0; i < r0 && spaces; i++) if (s[i] != ' ' && s[i] != '\t') spaces = false;
                if (spaces && r0 - gap0 <= 1) { last->len = r1 - last->start; continue; }
            }
            if (*no >= cap) { out[*no - 1].len = st[nt] - out[*no - 1].start; break; }   /* the rest in the last mark */
            out[*no].start = r0;
            out[*no].len = r1 - r0;
            (*no)++;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* The diff of two texts                                                    */
/* ------------------------------------------------------------------------ */

typedef struct {
    const cui_allocator *A;
    char *text[2]; size_t len[2];          /* copies of the texts */
    cui__dline *lines[2]; int n[2];
    cui__dblock *blocks; int nblocks;
    int added, removed;
    bool approx, failed, ignore_eol;
    /* word marks: line i of a side has ranges [rfirst, rfirst + rcount) */
    int *rfirst[2]; uint8_t *rcount[2];
    cui__drange *ranges; int nranges, rcap;
    int maxcols[2];                         /* the widest line, in columns */
    /* syntax: whether line i starts inside a block comment */
    uint8_t *inblock[2]; uint32_t lang_hash; bool lang_ready;
} cui__diff;

static void cui__df_clear(cui__diff *D) {
    const cui_allocator *A = D->A;
    for (int s = 0; s < 2; s++) {
        cui__df_free(A, D->text[s]);
        cui__df_free(A, D->lines[s]);
        cui__df_free(A, D->rfirst[s]);
        cui__df_free(A, D->rcount[s]);
        cui__df_free(A, D->inblock[s]);
    }
    cui__df_free(A, D->blocks);
    cui__df_free(A, D->ranges);
    memset(D, 0, sizeof *D);
    D->A = A;
}

static int cui__df_columns(const char *s, uint32_t n) {
    int col = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c == '\t') col = (col / 4 + 1) * 4;
        else if ((c & 0xC0) != 0x80) col++;
    }
    return col;
}

/* Lines with no equal line anywhere in the other text are in no common
 * subsequence: they are marked changed up front and the search runs on the
 * rest, which keeps the diff the shortest and makes most large diffs (where
 * changed lines are new) small problems. */
static bool cui__df_myers_lines(cui__dmyers *M, int *const ids[2], const int n[2], uint8_t *const chg[2]) {
    const cui_allocator *A = M->A;
    int nid = 0;
    for (int s = 0; s < 2; s++) for (int i = 0; i < n[s]; i++) if (ids[s][i] >= nid) nid = ids[s][i] + 1;
    uint8_t *seen = cui__df_calloc(A, (size_t)nid);
    int *f[2] = { NULL, NULL }, *map[2] = { NULL, NULL }, k[2] = { 0, 0 };
    uint8_t *fc[2] = { NULL, NULL };
    bool ok = seen != NULL;
    for (int s = 0; s < 2 && ok; s++) {
        for (int i = 0; i < n[s]; i++) seen[ids[s][i]] |= (uint8_t)(1 << s);
        f[s] = cui__df_alloc(A, sizeof(int) * (size_t)n[s]);
        map[s] = cui__df_alloc(A, sizeof(int) * (size_t)n[s]);
        fc[s] = cui__df_calloc(A, (size_t)n[s]);
        ok = f[s] && map[s] && fc[s];
    }
    if (ok) {
        for (int s = 0; s < 2; s++)
            for (int i = 0; i < n[s]; i++) {
                if (seen[ids[s][i]] == 3) { f[s][k[s]] = ids[s][i]; map[s][k[s]] = i; k[s]++; }
                else chg[s][i] = 1;
            }
        ok = cui__df_myers(M, f[0], k[0], f[1], k[1], fc[0], fc[1]);
        for (int s = 0; s < 2 && ok; s++) for (int t = 0; t < k[s]; t++) if (fc[s][t]) chg[s][map[s][t]] = 1;
    }
    cui__df_free(A, seen);
    for (int s = 0; s < 2; s++) { cui__df_free(A, f[s]); cui__df_free(A, map[s]); cui__df_free(A, fc[s]); }
    return ok;
}

/* Diffs a and b into D (cleared first). False only when out of memory. */
static bool cui__df_compute(cui__diff *D, const char *a, size_t an, const char *b, size_t bn, bool ignore_eol, bool words) {
    cui__df_clear(D);
    const cui_allocator *A = D->A;
    D->ignore_eol = ignore_eol;
    const char *src[2] = { a ? a : "", b ? b : "" };
    size_t sl[2] = { a ? an : 0, b ? bn : 0 };
    bool ok = true;
    int *ids[2] = { NULL, NULL };
    uint8_t *chg[2] = { NULL, NULL }, *blank[2] = { NULL, NULL };
    cui__dmyers M;
    memset(&M, 0, sizeof M);
    M.A = A;
    for (int s = 0; s < 2 && ok; s++) {
        if (sl[s] > CUI__DIFF_MAX_LEN) sl[s] = CUI__DIFF_MAX_LEN;
        D->len[s] = sl[s];
        D->text[s] = cui__df_alloc(A, sl[s] + 1);
        if (!D->text[s]) { ok = false; break; }
        memcpy(D->text[s], src[s], sl[s]);
        D->text[s][sl[s]] = 0;
        D->n[s] = cui__df_count_lines(D->text[s], sl[s]);
        int n = D->n[s];
        D->lines[s] = cui__df_alloc(A, sizeof(cui__dline) * (size_t)n);
        ids[s] = cui__df_alloc(A, sizeof(int) * (size_t)n);
        chg[s] = cui__df_calloc(A, (size_t)n);
        blank[s] = cui__df_alloc(A, (size_t)n);
        D->rfirst[s] = cui__df_alloc(A, sizeof(int) * (size_t)n);
        D->rcount[s] = cui__df_calloc(A, (size_t)n);
        if (!D->lines[s] || !ids[s] || !chg[s] || !blank[s] || !D->rfirst[s] || !D->rcount[s]) { ok = false; break; }
        cui__df_fill_lines(D->text[s], sl[s], D->lines[s]);
        for (int i = 0; i < n; i++) {
            const cui__dline *l = &D->lines[s][i];
            blank[s][i] = (uint8_t)cui__df_blank(D->text[s] + l->off, l->len);
            int cols = cui__df_columns(D->text[s] + l->off, l->len);
            if (cols > D->maxcols[s]) D->maxcols[s] = cols;
            D->rfirst[s][i] = -1;
        }
    }
    const char *const tt[2] = { D->text[0], D->text[1] };
    const cui__dline *const ll[2] = { D->lines[0], D->lines[1] };
    if (ok) ok = cui__df_intern(A, tt, ll, D->n, ignore_eol, ids);
    if (ok) {
        M.budget = CUI__DIFF_BUDGET;
        ok = cui__df_myers_lines(&M, ids, D->n, chg);
        D->approx = M.approx;
    }
    if (ok) {
        int nb = cui__df_blocks(chg[0], D->n[0], chg[1], D->n[1], NULL);
        D->blocks = cui__df_alloc(A, sizeof(cui__dblock) * (size_t)nb);
        if (!D->blocks) ok = false;
        else {
            cui__df_blocks(chg[0], D->n[0], chg[1], D->n[1], D->blocks);
            const int *cid[2] = { ids[0], ids[1] };
            const uint8_t *cbl[2] = { blank[0], blank[1] };
            cui__df_slide(D->blocks, nb, cid, cbl, D->n[0]);
            /* slid blocks may now touch: mark again and merge */
            memset(chg[0], 0, (size_t)D->n[0]);
            memset(chg[1], 0, (size_t)D->n[1]);
            for (int k = 0; k < nb; k++) {
                if (D->blocks[k].na) memset(chg[0] + D->blocks[k].a0, 1, (size_t)D->blocks[k].na);
                if (D->blocks[k].nb) memset(chg[1] + D->blocks[k].b0, 1, (size_t)D->blocks[k].nb);
            }
            D->nblocks = cui__df_blocks(chg[0], D->n[0], chg[1], D->n[1], D->blocks);
            for (int k = 0; k < D->nblocks; k++) { D->removed += D->blocks[k].na; D->added += D->blocks[k].nb; }
        }
    }
    /* word marks for the lines paired inside each block */
    if (ok && words) {
        cui__dmyers W;
        memset(&W, 0, sizeof W);
        W.A = A;
        W.budget = CUI__DIFF_WORD_BUDGET;
        cui__drange ra[CUI__DIFF_WORD_RANGES], rb[CUI__DIFF_WORD_RANGES];
        for (int k = 0; k < D->nblocks && ok && W.work < W.budget; k++) {
            const cui__dblock *B = &D->blocks[k];
            int pairs = CUI_MIN(B->na, B->nb);
            for (int t = 0; t < pairs && ok && W.work < W.budget; t++) {
                cui__dline *la = &D->lines[0][B->a0 + t], *lb = &D->lines[1][B->b0 + t];
                const char *sa = D->text[0] + la->off, *sb = D->text[1] + lb->off;
                if (la->len == lb->len && memcmp(sa, sb, la->len) == 0) {   /* only the line ending differs */
                    la->mark |= CUI__DL_EOL_TAG;
                    lb->mark |= CUI__DL_EOL_TAG;
                    continue;
                }
                int na_ = 0, nb_ = 0;
                if (!cui__df_words(&W, sa, (int)la->len, sb, (int)lb->len, ra, &na_, rb, &nb_, CUI__DIFF_WORD_RANGES)) continue;
                if (!cui__df_reserve(A, (void **)&D->ranges, &D->rcap, D->nranges + na_ + nb_, sizeof(cui__drange))) { ok = false; break; }
                D->rfirst[0][B->a0 + t] = D->nranges;
                D->rcount[0][B->a0 + t] = (uint8_t)na_;
                memcpy(D->ranges + D->nranges, ra, sizeof(cui__drange) * (size_t)na_);
                D->nranges += na_;
                D->rfirst[1][B->b0 + t] = D->nranges;
                D->rcount[1][B->b0 + t] = (uint8_t)nb_;
                memcpy(D->ranges + D->nranges, rb, sizeof(cui__drange) * (size_t)nb_);
                D->nranges += nb_;
            }
        }
        cui__df_myers_free(&W);
    }
    cui__df_myers_free(&M);
    for (int s = 0; s < 2; s++) { cui__df_free(A, ids[s]); cui__df_free(A, chg[s]); cui__df_free(A, blank[s]); }
    if (!ok) {
        cui__df_clear(D);
        D->failed = true;
    }
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Public, without a view                                                   */
/* ------------------------------------------------------------------------ */

int cui_diff_compute(const char *old_text, size_t old_len, const char *new_text, size_t new_len, unsigned flags,
                     cui_diff_hunk *hunks, int cap, cui_diff_stats *stats) {
    cui__diff D;
    memset(&D, 0, sizeof D);
    D.A = &cui__df_std;
    if (stats) memset(stats, 0, sizeof *stats);
    if (!cui__df_compute(&D, old_text, old_len, new_text, new_len, (flags & CUI_DIFF_IGNORE_EOL) != 0, false)) return -1;
    for (int k = 0; k < D.nblocks && k < cap && hunks; k++) {
        hunks[k].old_start = D.blocks[k].a0; hunks[k].old_count = D.blocks[k].na;
        hunks[k].new_start = D.blocks[k].b0; hunks[k].new_count = D.blocks[k].nb;
    }
    int count = D.nblocks;
    if (stats) {
        stats->added = D.added;
        stats->removed = D.removed;
        stats->hunks = D.nblocks;
        stats->approximate = D.approx;
        stats->old_lines = D.n[0];
        stats->new_lines = D.n[1];
    }
    cui__df_clear(&D);
    return count;
}

bool cui_diff_words(const char *old_line, int old_len, const char *new_line, int new_len,
                    cui_diff_range *old_ranges, int *old_count, cui_diff_range *new_ranges, int *new_count, int cap) {
    int dummy_a = 0, dummy_b = 0;
    if (!old_count) old_count = &dummy_a;
    if (!new_count) new_count = &dummy_b;
    *old_count = *new_count = 0;
    if (!old_line) old_len = 0;
    if (!new_line) new_len = 0;
    if (old_len < 0) old_len = old_line ? (int)strlen(old_line) : 0;
    if (new_len < 0) new_len = new_line ? (int)strlen(new_line) : 0;
    cui__dmyers M;
    memset(&M, 0, sizeof M);
    M.A = &cui__df_std;
    M.budget = CUI__DIFF_WORD_BUDGET;
    cui__drange ra[CUI__DIFF_WORD_RANGES], rb[CUI__DIFF_WORD_RANGES];
    int na = 0, nb = 0;
    if (cap > CUI__DIFF_WORD_RANGES) cap = CUI__DIFF_WORD_RANGES;
    bool ok = cap > 0 && cui__df_words(&M, old_line ? old_line : "", old_len, new_line ? new_line : "", new_len, ra, &na, rb, &nb, cap);
    cui__df_myers_free(&M);
    if (!ok) return false;
    for (int i = 0; i < na && old_ranges; i++) { old_ranges[i].start = ra[i].start; old_ranges[i].length = ra[i].len; }
    for (int i = 0; i < nb && new_ranges; i++) { new_ranges[i].start = rb[i].start; new_ranges[i].length = rb[i].len; }
    *old_count = na;
    *new_count = nb;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Rows                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct { int a, b, change, gap, hidden; uint8_t kind; } cui__drow;   /* a, b: line indexes or -1 */

typedef struct {
    cui__diff D;
    cui__drow *rows; int nrows, rcap;
    int *change_row;                 /* first row of each change */
    uint8_t *expanded; int nexpanded;   /* per equal stretch (gap), shown in full */
    bool rows_valid, rows_split, rows_nofold;
    int rows_context;
    int folds, hidden;               /* fold rows and the lines they hide */
} cui__diff_cache;

typedef struct {
    cui__diff_cache *c;
    int current, reported;           /* the selected change, and the value last written to *current */
    bool init, goto_pending, has_reported;
    int hover_row;
    float hover;
    double max_w;
} cui__diff_state;

static void cui__diff_dtor(cui_ctx *ui, void *data) {
    cui__diff_state *s = data;
    if (!s->c) return;
    cui__df_clear(&s->c->D);
    cui__free(ui, s->c->rows);
    cui__free(ui, s->c->change_row);
    cui__free(ui, s->c->expanded);
    cui__free(ui, s->c);
    s->c = NULL;
}

static bool cui__dr_push(cui_ctx *ui, cui__diff_cache *c, int kind, int a, int b, int change) {
    if (c->nrows >= c->rcap) {
        int ncap = c->rcap ? c->rcap * 2 : 256;
        cui__drow *nr = cui__realloc(ui, c->rows, sizeof(cui__drow) * (size_t)ncap);
        if (!nr) return false;
        c->rows = nr; c->rcap = ncap;
    }
    cui__drow *r = &c->rows[c->nrows++];
    r->kind = (uint8_t)kind; r->a = a; r->b = b; r->change = change; r->gap = -1; r->hidden = 0;
    return true;
}

static void cui__dr_build(cui_ctx *ui, cui__diff_cache *c, bool split, int context, bool nofold) {
    const cui__diff *D = &c->D;
    c->nrows = 0;
    c->folds = c->hidden = 0;
    cui__free(ui, c->change_row);
    c->change_row = D->nblocks ? cui__alloc(ui, sizeof(int) * (size_t)D->nblocks) : NULL;
    bool ok = !D->nblocks || c->change_row;
    int i = 0, j = 0;
    for (int g = 0; g <= D->nblocks && ok; g++) {
        /* the equal stretch before block g (or after the last one) */
        int ea1 = g < D->nblocks ? D->blocks[g].a0 : D->n[0];
        int len = ea1 - i;
        int keep_top = g > 0 ? context : 0, keep_bot = g < D->nblocks ? context : 0;
        int hide = len - keep_top - keep_bot;
        bool fold = !nofold && hide >= CUI__DIFF_FOLD_MIN && !(g < c->nexpanded && c->expanded[g]);
        for (int t = 0; t < len && ok; t++) {
            if (fold && t == keep_top) {
                ok = cui__dr_push(ui, c, CUI_DIFF_ROW_FOLD, i + t, j + t, -1);
                if (ok) { c->rows[c->nrows - 1].gap = g; c->rows[c->nrows - 1].hidden = hide; c->folds++; c->hidden += hide; }
                t += hide - 1;
                continue;
            }
            ok = cui__dr_push(ui, c, CUI_DIFF_ROW_SAME, i + t, j + t, -1);
        }
        i += len; j += len;
        if (g == D->nblocks || !ok) break;
        const cui__dblock *B = &D->blocks[g];
        c->change_row[g] = c->nrows;
        if (split) {
            int nr = CUI_MAX(B->na, B->nb);
            for (int t = 0; t < nr && ok; t++) {
                int kind = t < B->na && t < B->nb ? CUI_DIFF_ROW_CHANGED : t < B->na ? CUI_DIFF_ROW_REMOVED : CUI_DIFF_ROW_ADDED;
                ok = cui__dr_push(ui, c, kind, t < B->na ? B->a0 + t : -1, t < B->nb ? B->b0 + t : -1, g);
            }
        } else {
            for (int t = 0; t < B->na && ok; t++) ok = cui__dr_push(ui, c, CUI_DIFF_ROW_REMOVED, B->a0 + t, -1, g);
            for (int t = 0; t < B->nb && ok; t++) ok = cui__dr_push(ui, c, CUI_DIFF_ROW_ADDED, -1, B->b0 + t, g);
        }
        i += B->na; j += B->nb;
    }
    if (!ok) c->nrows = c->folds = c->hidden = 0;
    c->rows_valid = true;
    c->rows_split = split;
    c->rows_context = context;
    c->rows_nofold = nofold;
}

/* ------------------------------------------------------------------------ */
/* Painting                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct {
    float size, row_h, cw, gutter;    /* gutter: a line number column */
    bool split, syntax;
    cui_color fg, muted, sign_c[2], num_c[2], tint[2], gut[2], word[2], filler, fold, hover, marker, line, bg;
    cui_color colors[CUI__CODE_KINDS];
} cui__diff_common;

typedef struct {
    const char *text; int len;        /* display text: tabs expanded */
    const cui__code_tok *toks; int ntok;
    const cui__drange *marks; int nmarks;   /* in display bytes */
    const char *tag;                  /* "No newline at end of file", "CRLF" */
    int num;                          /* 1 based, 0 none */
    int tone;                         /* 0 none, 1 removed, 2 added, 3 filler */
} cui__diff_side;

typedef struct {
    const cui__diff_common *c;
    uint8_t kind;
    bool current;
    float hover;
    int hidden;
    cui__diff_side side[2];           /* inline: [0] is the line, side[1].num the new number */
} cui__diff_rowp;

/* the scroll area the rows sit in: rows are wider than it when lines scroll sideways */
static cui_rect cui__diff_view_rect(cui_ctx *ui, cui_node *n, float *hx) {
    cui_node *content = &ui->nodes[n->parent];
    cui_node *sc = &ui->nodes[content->parent];
    *hx = sc->scroll_x;
    return sc->rect;
}

/* x of the parts of a side, relative to the view */
typedef struct { float left, right, num, sign, text; } cui__diff_cols;
static void cui__diff_side_cols(const cui__diff_common *c, float w, int side, cui__diff_cols *o) {
    if (c->split) {
        float half = floorf(w * 0.5f);
        o->left = side ? half + 1 : 0;
        o->right = side ? w : half;
        o->num = o->left + 10 + c->gutter;
    } else {
        o->left = 0; o->right = w;
        o->num = 10 + c->gutter + (side ? 10 + c->gutter : 0);
    }
    o->sign = (c->split ? o->num : 10 + c->gutter + 10 + c->gutter) + 8;
    o->text = o->sign + c->cw + 8;
}

static void cui__diff_paint_text(cui_ctx *ui, const cui__diff_common *c, const cui__diff_side *sd, float x, float ty, float y) {
    float size = c->size;
    for (int m = 0; m < sd->nmarks; m++) {
        float x0 = cui__text_width(ui, CUI_FONT_MONO, size, sd->text, sd->marks[m].start, true);
        float x1 = cui__text_width(ui, CUI_FONT_MONO, size, sd->text, sd->marks[m].start + sd->marks[m].len, true);
        cui_draw_rect(ui, (cui_rect){ x + x0 - 1, y + 2, x1 - x0 + 2, c->row_h - 4 }, CUI_CORNERS(3), c->word[sd->tone == 1 ? 0 : 1]);
    }
    if (c->syntax && sd->ntok > 0) {
        float px = x;
        for (int k = 0; k < sd->ntok; k++) {
            const char *s = sd->text + sd->toks[k].start;
            int len = sd->toks[k].len;
            if (s[0] != ' ') cui__draw_text_line(ui, CUI_FONT_MONO, size, px, ty, c->colors[sd->toks[k].kind], s, len, true);
            px += cui__text_width(ui, CUI_FONT_MONO, size, s, len, true);
        }
        /* text past the last token (more tokens than the tokenizer keeps) */
        int done = sd->toks[sd->ntok - 1].start + sd->toks[sd->ntok - 1].len;
        if (done < sd->len) cui__draw_text_line(ui, CUI_FONT_MONO, size, px, ty, c->fg, sd->text + done, sd->len - done, true);
    } else if (sd->len > 0) {
        cui__draw_text_line(ui, CUI_FONT_MONO, size, x, ty, c->fg, sd->text, sd->len, true);
    }
    if (sd->tag) {
        float w = cui__text_width(ui, CUI_FONT_MONO, size, sd->text, sd->len, true);
        float ts = size * 0.92f;
        float tfh = cui__font_line_height(ui, CUI_FONT_BODY, ts);
        cui__draw_text_line(ui, CUI_FONT_BODY, ts, x + w + (sd->len ? c->cw * 1.5f : 0), y + (c->row_h - tfh) * 0.5f, c->muted, sd->tag, -1, false);
    }
}

static void cui__diff_paint_number(cui_ctx *ui, const cui__diff_common *c, int num, float right, float ty, cui_color col) {
    if (num <= 0) return;
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%d", num);
    if (n <= 0 || n >= (int)sizeof buf) return;
    float w = cui__text_width(ui, CUI_FONT_MONO, c->size, buf, n, true);
    cui__draw_text_line(ui, CUI_FONT_MONO, c->size, right - w, ty, col, buf, n, true);
}

static void cui__paint_diff_row(cui_ctx *ui, cui_node *n, void *data) {
    cui__diff_rowp *p = data;
    const cui__diff_common *c = p->c;
    float hx;
    cui_rect v = cui__diff_view_rect(ui, n, &hx);
    float x0 = n->rect.x + hx, y = n->rect.y, h = c->row_h, w = v.w;
    float fh = cui__font_line_height(ui, CUI_FONT_MONO, c->size);
    float ty = y + (h - fh) * 0.5f;
    if (p->kind == CUI_DIFF_ROW_FOLD) {   /* over the line between the sides */
        cui_draw_rect(ui, (cui_rect){ x0, y, w, h }, CUI_CORNERS(0), c->bg);
        cui_draw_rect(ui, (cui_rect){ x0, y, w, h }, CUI_CORNERS(0), c->fold);
        if (p->hover > 0) cui_draw_rect(ui, (cui_rect){ x0, y, w, h }, CUI_CORNERS(0), cui_alpha(c->hover, p->hover));
        cui__diff_cols cl;
        cui__diff_side_cols(c, w, 0, &cl);
        float is = 12;
        cui_draw_icon(ui, CUI_ICON_CHEVRONS_UP_DOWN, (cui_rect){ x0 + cl.sign + c->cw * 0.5f - is * 0.5f, y, is, h }, is, c->muted);
        const char *label = cui__frame_printf(ui, "%d unchanged line%s", p->hidden, p->hidden == 1 ? "" : "s");
        float ls = c->size * 0.95f;
        float lfh = cui__font_line_height(ui, CUI_FONT_BODY, ls);
        cui__draw_text_line(ui, CUI_FONT_BODY, ls, x0 + cl.text, y + (h - lfh) * 0.5f, c->muted, label, -1, false);
        return;
    }
    int sides = c->split ? 2 : 1;
    for (int s = 0; s < sides; s++) {
        const cui__diff_side *sd = &p->side[s];
        cui__diff_cols cl;
        cui__diff_side_cols(c, w, s, &cl);
        int t = sd->tone;
        if (t == 3) cui_draw_rect(ui, (cui_rect){ x0 + cl.left, y, cl.right - cl.left, h }, CUI_CORNERS(0), c->filler);
        else if (t) {
            cui_draw_rect(ui, (cui_rect){ x0 + cl.left, y, cl.right - cl.left, h }, CUI_CORNERS(0), c->tint[t - 1]);
            cui_draw_rect(ui, (cui_rect){ x0 + cl.left, y, cl.sign + c->cw + 4 - cl.left, h }, CUI_CORNERS(0), c->gut[t - 1]);
        }
        cui_color nc = t == 1 || t == 2 ? c->num_c[t - 1] : c->muted;
        if (c->split) cui__diff_paint_number(ui, c, sd->num, x0 + cl.num, ty, nc);
        else {
            cui__diff_paint_number(ui, c, sd->num, x0 + 10 + c->gutter, ty, nc);
            cui__diff_paint_number(ui, c, p->side[1].num, x0 + 20 + 2 * c->gutter, ty, nc);
        }
        if (t == 1 || t == 2) cui__draw_text_line(ui, CUI_FONT_MONO, c->size, x0 + cl.sign, ty, c->sign_c[t - 1], t == 1 ? "-" : "+", 1, true);
        if (t != 3) {
            cui_push_clip(ui, (cui_rect){ x0 + cl.text - 2, y, cl.right - cl.text + 2, h });
            cui__diff_paint_text(ui, c, sd, x0 + cl.text - hx, ty, y);
            cui_pop_clip(ui);
        }
    }
    if (p->current) cui_draw_rect(ui, (cui_rect){ x0, y, 2, h }, CUI_CORNERS(0), c->marker);
}

/* the line between the sides, under the rows */
static void cui__paint_diff_divider(cui_ctx *ui, cui_node *n, void *data) {
    const cui__diff_common *c = data;
    cui_node *sc = &ui->nodes[n->parent];
    cui_rect v = sc->rect;
    cui_draw_rect(ui, (cui_rect){ v.x + floorf(v.w * 0.5f), v.y, 1, v.h }, CUI_CORNERS(0), c->line);
}

typedef struct {
    const cui__diff_common *c;
    const char *title[2];
    int added, removed;
    bool sum;
} cui__diff_headp;

static void cui__paint_diff_head(cui_ctx *ui, cui_node *n, void *data) {
    cui__diff_headp *p = data;
    const cui__diff_common *c = p->c;
    cui_rect r = n->rect;
    float fs = cui__fs(ui, ui->theme.text_sm);
    float fh = cui__font_line_height(ui, CUI_FONT_MEDIUM, fs);
    float ty = r.y + (r.h - 1 - fh) * 0.5f;
    cui_color tc = CUI_C(ui, TEXT_SECONDARY);
    /* the summary at the right end: "+12 -4" */
    float sum_w = 0;
    if (p->sum) {
        const char *plus = cui__frame_printf(ui, "+%d", p->added);
        const char *minus = cui__frame_printf(ui, "\xe2\x88\x92%d", p->removed);
        float wp = cui__text_width(ui, CUI_FONT_MEDIUM, fs, plus, -1, true), wm = cui__text_width(ui, CUI_FONT_MEDIUM, fs, minus, -1, true);
        float x = r.x + r.w - 12 - wm;
        cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs, x, ty, c->sign_c[0], minus, -1, true);
        x -= 8 + wp;
        cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs, x, ty, c->sign_c[1], plus, -1, true);
        sum_w = wp + wm + 8 + 20;
    }
    if (c->split) {
        float half = floorf(r.w * 0.5f);
        for (int s = 0; s < 2; s++) {
            if (!p->title[s]) continue;
            float x = r.x + (s ? half + 1 : 0) + 12;
            float max_w = (s ? r.w - half - 1 - sum_w : half) - 24;
            cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fs, x, ty, max_w, tc, p->title[s], -1, false);
        }
        cui_draw_rect(ui, (cui_rect){ r.x + half, r.y, 1, r.h }, CUI_CORNERS(0), c->line);
    } else {
        float x = r.x + 12, avail = r.w - 24 - sum_w;
        const char *a = p->title[0], *b = p->title[1];
        if (a && b && strcmp(a, b) != 0) {
            float wa = cui__text_width(ui, CUI_FONT_MEDIUM, fs, a, -1, false);
            float part = CUI_MIN(wa, (avail - 28) * 0.5f);
            cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fs, x, ty, part, c->muted, a, -1, false);
            x += part + 6;
            cui_draw_icon(ui, CUI_ICON_ARROW_RIGHT, (cui_rect){ x, r.y, 14, r.h - 1 }, 14, c->muted);
            x += 20;
            cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fs, x, ty, r.x + r.w - 12 - sum_w - x, tc, b, -1, false);
        } else if (a || b) {
            cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fs, x, ty, avail, tc, b ? b : a, -1, false);
        }
    }
    cui_draw_rect(ui, (cui_rect){ r.x, r.y + r.h - 1, r.w, 1 }, CUI_CORNERS(0), c->line);
}

/* ------------------------------------------------------------------------ */
/* The view                                                                 */
/* ------------------------------------------------------------------------ */

/* Display copy of a line for one side of a row: tabs to 4 column stops,
 * control characters to spaces, the marks moved along. */
static void cui__diff_side_fill(cui_ctx *ui, const cui__diff *D, int side, int line, const char *lang, cui__diff_side *out) {
    const cui__dline *l = &D->lines[side][line];
    const char *src = D->text[side] + l->off;
    int n = (int)l->len;
    if (n > CUI__DIFF_DRAW_MAX) {
        n = CUI__DIFF_DRAW_MAX;
        for (int k = 0; k < 3 && n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80; k++) n--;   /* at a character boundary */
    }
    int tabs = 0;
    for (int i = 0; i < n; i++) if (src[i] == '\t') tabs++;
    char *dst = cui__frame_alloc(ui, (size_t)n + (size_t)tabs * 3 + 1);
    int *map = cui__frame_alloc(ui, sizeof(int) * ((size_t)n + 1));
    int o = 0, col = 0;
    for (int i = 0; i < n; i++) {
        map[i] = o;
        uint8_t ch = (uint8_t)src[i];
        if (ch == '\t') { int to = (col / 4 + 1) * 4; while (col < to) { dst[o++] = ' '; col++; } continue; }
        dst[o++] = ch < 0x20 || ch == 0x7F ? ' ' : (char)ch;
        if ((ch & 0xC0) != 0x80) col++;
    }
    map[n] = o;
    dst[o] = 0;
    out->text = dst;
    out->len = o;
    out->num = line + 1;
    /* marks */
    int rf = D->rfirst[side][line], rc = D->rcount[side][line];
    if (rf >= 0 && rc > 0) {
        cui__drange *mk = cui__frame_alloc(ui, sizeof *mk * (size_t)rc);
        int nm = 0;
        for (int k = 0; k < rc; k++) {
            const cui__drange *r = &D->ranges[rf + k];
            if (r->start >= n) break;
            int e = CUI_MIN(n, r->start + r->len);
            mk[nm].start = map[r->start];
            mk[nm].len = map[e] - map[r->start];
            nm++;
        }
        out->marks = mk;
        out->nmarks = nm;
    }
    /* line ending tags */
    if (l->mark & CUI__DL_EOL_TAG) out->tag = l->eol == 2 ? "CRLF" : l->eol == 1 ? "LF" : "No newline at end of file";
    /* syntax */
    if (lang && o > 0) {
        bool block = D->inblock[side] && D->inblock[side][line];
        cui__code_tok tk[256];
        int nt = cui__code_tokens(lang, dst, o, &block, tk, 256);
        cui__code_tok *keep = cui__frame_alloc(ui, sizeof *keep * (size_t)(nt > 0 ? nt : 1));
        memcpy(keep, tk, sizeof *keep * (size_t)nt);
        out->toks = keep;
        out->ntok = nt;
    }
}

/* which lines start inside a block comment, for the language */
static void cui__diff_lang(cui_ctx *ui, cui__diff *D, const char *lang) {
    uint32_t h = lang ? cui__df_hash(lang, (int)strlen(lang), 2166136261u) : 0;
    if (D->lang_ready && D->lang_hash == h) return;
    D->lang_ready = true;
    D->lang_hash = h;
    if (!lang) return;
    cui__code_tok tk[256];
    for (int s = 0; s < 2; s++) {
        if (!D->inblock[s]) D->inblock[s] = cui__df_alloc(D->A, (size_t)D->n[s]);
        if (!D->inblock[s]) return;
        bool block = false;
        for (int i = 0; i < D->n[s]; i++) {
            D->inblock[s][i] = block;
            const cui__dline *l = &D->lines[s][i];
            int n = (int)CUI_MIN(l->len, (uint32_t)CUI__DIFF_DRAW_MAX);
            cui__code_tokens(lang, D->text[s] + l->off, n, &block, tk, 256);   /* the copy ends in a terminator */
        }
    }
    (void)ui;
}

static int cui__diff_row_of_change(const cui__diff_cache *c, int k) { return c->change_row && k >= 0 && k < c->D.nblocks ? c->change_row[k] : 0; }

static int cui__diff_row_end(const cui__diff_cache *c, int k) {
    const cui__dblock *B = &c->D.blocks[k];
    return cui__diff_row_of_change(c, k) + (c->rows_split ? CUI_MAX(B->na, B->nb) : B->na + B->nb);
}

int cui_diff_view_rows(cui_ctx *ui, const char *id_str, cui_diff_row *rows, int first, int cap) {
    cui_id id = cui_make_id(ui, id_str);
    cui__diff_state *s = cui_state(ui, id ^ CUI__DIFF_SALT, sizeof *s);
    if (!s->c || !s->c->rows_valid) return 0;
    const cui__diff_cache *c = s->c;
    for (int i = 0; rows && i < cap && first >= 0 && first + i < c->nrows; i++) {
        const cui__drow *r = &c->rows[first + i];
        rows[i].kind = (cui_diff_row_kind)r->kind;
        rows[i].old_line = r->a;
        rows[i].new_line = r->b;
        rows[i].change = r->change;
        rows[i].hidden = r->hidden;
    }
    return c->nrows;
}

cui_diff_result cui_diff_view_ex(cui_ctx *ui, const char *id_str, const char *old_text, const char *new_text, const cui_diff_opts *o) {
    cui_diff_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_diff_result res;
    memset(&res, 0, sizeof res);
    res.current = -1;
    cui_id id = cui_make_id(ui, id_str);
    cui_id lid = id ^ 0xD1FF11u, sid = id ^ 0xD1FF5Cu;
    cui__diff_state *s = cui_state(ui, id ^ CUI__DIFF_SALT, sizeof *s);
    cui__state_set_dtor(ui, id ^ CUI__DIFF_SALT, cui__diff_dtor);
    if (!s->init) {
        s->init = true;
        s->current = -1;
        s->hover_row = -1;
    }
    if (!s->c) {
        s->c = cui__alloc(ui, sizeof *s->c);
        if (s->c) { memset(s->c, 0, sizeof *s->c); s->c->D.A = &ui->alloc; }
    }
    cui__diff_cache *c = s->c;
    if (!c) return res;
    cui__diff *D = &c->D;

    /* ---- diff again only when a text (or how lines compare) changed ---- */
    if (!old_text) old_text = "";
    if (!new_text) new_text = "";
    size_t ol = strlen(old_text), nl = strlen(new_text);
    if (ol > CUI__DIFF_MAX_LEN) ol = CUI__DIFF_MAX_LEN;
    if (nl > CUI__DIFF_MAX_LEN) nl = CUI__DIFF_MAX_LEN;
    bool same = D->text[0] && D->text[1] && D->len[0] == ol && D->len[1] == nl && D->ignore_eol == o->ignore_eol &&
                memcmp(D->text[0], old_text, ol) == 0 && memcmp(D->text[1], new_text, nl) == 0;
    if (!same) {
        cui__df_compute(D, old_text, ol, new_text, nl, o->ignore_eol, true);
        res.recomputed = true;
        c->rows_valid = false;
        cui__free(ui, c->expanded);
        c->expanded = D->nblocks + 1 > 0 ? cui__alloc(ui, (size_t)D->nblocks + 1) : NULL;
        c->nexpanded = c->expanded ? D->nblocks + 1 : 0;
        if (c->expanded) memset(c->expanded, 0, (size_t)c->nexpanded);
        if (s->current >= D->nblocks) s->current = D->nblocks - 1;
        s->max_w = 0;
    }
    const char *lang = o->language && o->language[0] ? o->language : NULL;
    if (lang) cui__diff_lang(ui, D, lang);

    /* ---- geometry ---- */
    float size = cui__fs(ui, o->size > 0 ? o->size : 12);
    float row_h = roundf(size * 1.75f);
    float cw = cui__text_width(ui, CUI_FONT_MONO, size, "0000000000000000", 16, true) / 16.0f;
    int digits = 2;
    for (int v = CUI_MAX(D->n[0], D->n[1]); v >= 100; v /= 10) digits++;
    float gutter = (float)digits * cw;
    cui_rect prev = cui__prev_rect(ui, id);
    bool known = cui__has_prev_rect(ui, id);
    if (!known) ui->animating = true;   /* lay out again once the width is known */
    float split_min = o->split_min_width > 0 ? o->split_min_width : CUI__DIFF_SPLIT_MIN;
    bool split = o->mode == CUI_DIFF_SPLIT && (!known || prev.w >= split_min);
    int context = o->context > 0 ? o->context : 3;
    bool nofold = o->no_fold;

    /* ---- rows (rebuilt when the diff, the mode or a fold changes) ---- */
    cui_scroll_state *ss = cui_state(ui, sid, sizeof *ss);
    if (!c->rows_valid || c->rows_split != split || c->rows_context != context || c->rows_nofold != nofold) {
        /* keep the line at the top in view across a change of mode */
        int top_a = -1, top_b = -1;
        if (c->rows_valid && c->nrows > 0 && !res.recomputed) {
            int ti = CUI_CLAMP((int)((ss->y - CUI__DIFF_INSET) / row_h), 0, c->nrows - 1);
            top_a = c->rows[ti].a; top_b = c->rows[ti].b;
        }
        cui__dr_build(ui, c, split, context, nofold);
        if (top_a >= 0 || top_b >= 0) {
            int ti = 0;
            for (; ti < c->nrows; ti++) {
                const cui__drow *r = &c->rows[ti];
                if ((top_a >= 0 && r->a >= top_a) || (top_a < 0 && r->b >= top_b)) break;
            }
            ss->y = ss->ty = (float)CUI_MIN(ti, CUI_MAX(0, c->nrows - 1)) * row_h;
        }
    }
    int nrows = c->nrows;

    /* ---- the selected change ---- */
    int nchanges = D->nblocks;
    if (s->current >= nchanges) s->current = nchanges - 1;
    int before_current = s->current;
    if (o->current) {
        int want = *o->current;
        if (want < -1) want = -1;
        if (want >= nchanges) want = nchanges - 1;
        if (!s->has_reported || want != s->reported) {
            if (want != s->current || !s->has_reported) {
                s->current = want;
                if (want >= 0) s->goto_pending = true;
            }
        }
    }

    float header_h = o->old_title || o->new_title ? 32 : 0;
    float content_h = (float)nrows * row_h + 2 * CUI__DIFF_INSET;
    float height = o->height > 0 ? o->height : CUI_MIN(420.0f, header_h + CUI_MAX(content_h, 2 * row_h + 2 * CUI__DIFF_INSET) + 2);
    float view_h = ss->view_h > 0 ? ss->view_h : height - header_h - 2;
    float view_w = ss->view_w > 0 ? ss->view_w : (known ? prev.w - 2 : 600);

    /* ---- pointer ---- */
    cui_interact lr = cui__interact(ui, lid, CUI_IF_FOCUSABLE);
    cui_rect cr = cui__prev_rect(ui, lid);
    int hover_row = -1;
    if (cr.h > 0 && lr.hovered) {
        float fy = (ui->my - cr.y - CUI__DIFF_INSET) / row_h;
        int ri = (int)floorf(fy);
        if (fy >= 0 && ri < nrows) hover_row = ri;
    }
    bool rebuild = false;
    if (hover_row >= 0 && c->rows[hover_row].kind == CUI_DIFF_ROW_FOLD) cui__set_cursor(ui, CUI_CURSOR_HAND);
    if (lr.clicked && hover_row >= 0) {
        const cui__drow *r = &c->rows[hover_row];
        if (r->kind == CUI_DIFF_ROW_FOLD && r->gap >= 0 && r->gap < c->nexpanded) { c->expanded[r->gap] = 1; rebuild = true; }
        else if (r->change >= 0) s->current = r->change;
    }

    /* ---- keys ---- */
    if (ui->focus == lid) {
        int mods = ui->mods;
        bool alt = (mods & CUI_MOD_ALT) != 0, cmd = (mods & (CUI_MOD_CTRL | CUI_MOD_SUPER)) != 0;
        int step = 0;
        if (!cmd && ((!alt && cui_key_pressed(ui, CUI_KEY_N)) || (alt && cui_key_pressed(ui, CUI_KEY_DOWN)))) step = 1;
        if (!cmd && ((!alt && cui_key_pressed(ui, CUI_KEY_P)) || (alt && cui_key_pressed(ui, CUI_KEY_UP)))) step = -1;
        if (step && nchanges > 0) {
            int to;
            if (s->current < 0) {
                /* from where the view is: the first change at or below its top, or the last above it */
                int top = (int)floorf((ss->ty - CUI__DIFF_INSET) / row_h + 0.5f);
                to = step > 0 ? nchanges - 1 : 0;
                if (step > 0) { for (int k = 0; k < nchanges; k++) if (cui__diff_row_of_change(c, k) >= top) { to = k; break; } }
                else { for (int k = nchanges - 1; k >= 0; k--) if (cui__diff_row_of_change(c, k) < top) { to = k; break; } }
            } else to = CUI_CLAMP(s->current + step, 0, nchanges - 1);
            s->current = to;
            s->goto_pending = true;
        }
        if (!alt && !cmd) {
            float dy = 0;
            if (cui_key_pressed(ui, CUI_KEY_DOWN)) dy = row_h;
            if (cui_key_pressed(ui, CUI_KEY_UP)) dy = -row_h;
            if (cui_key_pressed(ui, CUI_KEY_PAGE_DOWN)) dy = CUI_MAX(row_h, view_h - row_h);
            if (cui_key_pressed(ui, CUI_KEY_PAGE_UP)) dy = -CUI_MAX(row_h, view_h - row_h);
            if (cui_key_pressed(ui, CUI_KEY_HOME)) dy = -1e9f;
            if (cui_key_pressed(ui, CUI_KEY_END)) dy = 1e9f;
            if (dy != 0) { ss->ty = cui__clampf(ss->ty + dy, 0, CUI_MAX(0, content_h - view_h)); ui->animating = true; }
            float dx = 0;
            if (cui_key_pressed(ui, CUI_KEY_RIGHT)) dx = cw * 8;
            if (cui_key_pressed(ui, CUI_KEY_LEFT)) dx = -cw * 8;
            if (dx != 0) { ss->tx = CUI_MAX(0, ss->tx + dx); ui->animating = true; }
            if (cui_key_pressed(ui, CUI_KEY_E) && c->nexpanded > 0) {
                memset(c->expanded, (mods & CUI_MOD_SHIFT) ? 0 : 1, (size_t)c->nexpanded);   /* Shift+E folds again */
                rebuild = true;
            }
        }
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE) && s->current >= 0) { s->current = -1; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
    }
    if (rebuild) {
        cui__dr_build(ui, c, split, context, nofold);
        nrows = c->nrows;
        content_h = (float)nrows * row_h + 2 * CUI__DIFF_INSET;
        hover_row = -1;
    }

    /* ---- go to the selected change ---- */
    /* once the scroll area has laid out these rows (it clamps to its last layout) */
    bool laid_out = ss->view_h > 0 && fabsf(ss->content_h - CUI_MAX(content_h, ss->view_h)) < 1.0f;
    if (s->goto_pending && s->current >= 0 && s->current < nchanges && !laid_out) ui->animating = true;
    else if (s->goto_pending && s->current >= 0 && s->current < nchanges) {
        int r0 = cui__diff_row_of_change(c, s->current), r1 = cui__diff_row_end(c, s->current);
        float top = CUI__DIFF_INSET + (float)r0 * row_h, bottom = CUI__DIFF_INSET + (float)r1 * row_h;
        bool seen = top >= ss->ty && bottom <= ss->ty + view_h;
        if (!seen) {
            float t = top - CUI_MIN(view_h * 0.25f, 3 * row_h);
            if (bottom - top > view_h * 0.75f) t = top - row_h;
            ss->ty = cui__clampf(t, 0, CUI_MAX(0, content_h - view_h));
            ui->animating = true;
        }
        s->goto_pending = false;
    } else if (s->current < 0) s->goto_pending = false;
    cui__tween(ui, &s->hover, hover_row >= 0 && c->rows[hover_row].kind == CUI_DIFF_ROW_FOLD ? 1.0f : 0.0f, CUI__T_HIGHLIGHT);
    if (hover_row >= 0) s->hover_row = hover_row;

    /* ---- build ---- */
    bool dark = ui->mode == CUI_THEME_DARK;
    int outer = cui__node_new(ui, id);
    cui_node *on = &ui->nodes[outer];
    on->w = o->width.kind ? o->width : CUI_FILL;
    on->h = o->height < 0 ? CUI_FILL : CUI_PX(height);
    on->radius = CUI_CORNERS(ui->theme.radius_control);
    on->border = 1; on->border_color = CUI_C(ui, BORDER);
    cui_color bg = dark ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, BACKGROUND), 0.4f) : CUI_C(ui, SURFACE_MUTED);
    on->bg = bg;   /* the code block's surface */
    on->flags |= CUI_NF_CLIP;
    cui__open(ui, outer);

    cui__diff_common *cm = cui__frame_alloc(ui, sizeof *cm);
    memset(cm, 0, sizeof *cm);
    cm->size = size; cm->row_h = row_h; cm->cw = cw; cm->gutter = gutter;
    cm->split = split; cm->syntax = lang != NULL;
    cui_color fg = CUI_C(ui, FOREGROUND), danger = CUI_C(ui, DANGER), success = CUI_C(ui, SUCCESS);
    cm->fg = fg;
    cm->muted = CUI_C(ui, TEXT_MUTED);
    cui_color tone[2] = { danger, success };
    for (int t = 0; t < 2; t++) {
        cm->tint[t] = cui_alpha(tone[t], dark ? 0.13f : 0.085f);
        cm->gut[t] = cui_alpha(tone[t], dark ? 0.08f : 0.07f);
        cm->word[t] = cui_alpha(tone[t], dark ? 0.30f : 0.22f);
        cm->sign_c[t] = cui_mix(fg, tone[t], dark ? 0.75f : 0.85f);
        cm->num_c[t] = cui_mix(CUI_C(ui, TEXT_MUTED), tone[t], 0.55f);
    }
    cm->filler = cui_alpha(fg, dark ? 0.035f : 0.03f);
    cm->fold = cui_alpha(ui->accent == CUI_ACCENT_NEUTRAL ? fg : CUI_C(ui, ACCENT), dark ? 0.06f : 0.045f);
    cm->hover = CUI_C(ui, HOVER);
    cm->marker = ui->accent == CUI_ACCENT_NEUTRAL ? fg : CUI_C(ui, ACCENT);
    cm->line = CUI_C(ui, BORDER);
    cm->bg = bg;
    if (lang) cui__code_colors(ui, cm->colors);

    if (header_h > 0) {
        int head = cui__leaf(ui, 0, CUI_FILL, CUI_PX(header_h));
        cui__diff_headp *hp = cui__frame_alloc(ui, sizeof *hp);
        hp->c = cm;
        hp->title[0] = o->old_title ? cui__frame_strdup(ui, o->old_title, -1) : NULL;
        hp->title[1] = o->new_title ? cui__frame_strdup(ui, o->new_title, -1) : NULL;
        hp->added = D->added; hp->removed = D->removed;
        hp->sum = nchanges > 0;
        ui->nodes[head].bg = CUI_C(ui, SURFACE);
        ui->nodes[head].paint = cui__paint_diff_head;
        ui->nodes[head].paint_data = hp;
    }

    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL; sb.height = CUI_FILL;
    int scroll = cui__scroll_begin_ex(ui, sid, &sb, true);
    float y = ui->nodes[scroll].scroll_y;
    int first = (int)floorf((y - CUI__DIFF_INSET) / row_h) - 2;
    int last = (int)ceilf((y + view_h) / row_h) + 2;
    if (first < 0) first = 0;
    if (last > nrows) last = nrows;
    if (first > last) first = last;

    /* sideways: the widest text past the narrowest text column */
    cui__diff_cols c0, c1;
    cui__diff_side_cols(cm, view_w, 0, &c0);
    cui__diff_side_cols(cm, view_w, 1, &c1);
    float text_w = split ? CUI_MIN(c0.right - c0.text, c1.right - c1.text) : c0.right - c0.text;
    double widest = (double)CUI_MAX(D->maxcols[0], D->maxcols[1]) * cw;
    if (widest > CUI__DIFF_DRAW_MAX * cw) widest = CUI__DIFF_DRAW_MAX * cw;
    widest = CUI_MAX(widest, s->max_w);
    float extra = CUI_MAX(0.0f, (float)widest + cw * 3 - text_w);

    int content = cui__node_new(ui, lid);
    cui_node *cn = &ui->nodes[content];
    cn->w = CUI_FILL;
    cn->min_w = extra > 0 ? view_w + extra : 0;
    cn->h = CUI_PX(CUI_MAX(content_h, view_h));
    cui__mark_interactive(ui, content, CUI_IF_FOCUSABLE);
    if (split && nrows > 0) {
        cn->paint = cui__paint_diff_divider;
        cn->paint_data = cm;
    }
    cui__open(ui, content);
    if (nrows == 0) {
        int e = cui__node_new(ui, 0);
        ui->nodes[e].w = CUI_FILL; ui->nodes[e].h = CUI_FILL;
        ui->nodes[e].align = CUI_ALIGN_CENTER; ui->nodes[e].justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, e);
        const char *msg = D->failed ? "Not enough memory to compare these texts" : "Both texts are empty";
        cui__text_node(ui, msg, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_MUTED), false);
        cui__close(ui);
    } else {
        cui__leaf(ui, 0, CUI_FILL, CUI_PX(CUI__DIFF_INSET + (float)first * row_h));
        for (int i = first; i < last; i++) {
            const cui__drow *r = &c->rows[i];
            cui__diff_rowp *p = cui__frame_alloc(ui, sizeof *p);
            memset(p, 0, sizeof *p);
            p->c = cm;
            p->kind = r->kind;
            p->current = r->change >= 0 && r->change == s->current;
            p->hidden = r->hidden;
            p->hover = i == hover_row ? s->hover : 0;
            if (r->kind != CUI_DIFF_ROW_FOLD) {
                if (split) {
                    if (r->a >= 0) cui__diff_side_fill(ui, D, 0, r->a, lang, &p->side[0]);
                    if (r->b >= 0) cui__diff_side_fill(ui, D, 1, r->b, lang, &p->side[1]);
                    p->side[0].tone = r->a < 0 ? 3 : r->kind == CUI_DIFF_ROW_SAME ? 0 : 1;
                    p->side[1].tone = r->b < 0 ? 3 : r->kind == CUI_DIFF_ROW_SAME ? 0 : 2;
                    for (int t = 0; t < 2; t++)
                        if (p->side[t].len > 0) s->max_w = CUI_MAX(s->max_w, (double)cui__text_width(ui, CUI_FONT_MONO, size, p->side[t].text, p->side[t].len, true));
                } else {
                    int sd = r->a >= 0 ? 0 : 1;
                    cui__diff_side_fill(ui, D, sd, sd ? r->b : r->a, lang, &p->side[0]);
                    p->side[0].num = r->a >= 0 ? r->a + 1 : 0;
                    p->side[1].num = r->b >= 0 ? r->b + 1 : 0;
                    p->side[0].tone = r->kind == CUI_DIFF_ROW_REMOVED ? 1 : r->kind == CUI_DIFF_ROW_ADDED ? 2 : 0;
                    if (p->side[0].len > 0) s->max_w = CUI_MAX(s->max_w, (double)cui__text_width(ui, CUI_FONT_MONO, size, p->side[0].text, p->side[0].len, true));
                }
            }
            int rn = cui__leaf(ui, 0, CUI_FILL, CUI_PX(row_h));
            ui->nodes[rn].paint = cui__paint_diff_row;
            ui->nodes[rn].paint_data = p;
        }
    }
    cui__close(ui);   /* content */
    cui__scroll_end_ex(ui);
    if (ui->focus == lid) cui__focus_ring(ui, outer, ui->focus_visible ? 1.0f : 0.0f);
    cui__close(ui);   /* outer */
    ui->last_id = id;

    /* ---- result ---- */
    if (o->current) *o->current = s->current;
    s->reported = s->current;
    s->has_reported = true;
    res.added = D->added;
    res.removed = D->removed;
    res.changes = nchanges;
    res.current = s->current;
    res.current_changed = s->current != before_current;
    res.mode = split ? CUI_DIFF_SPLIT : CUI_DIFF_INLINE;
    res.approximate = D->approx;
    res.folds = c->folds;
    res.hidden = c->hidden;
    return res;
}
