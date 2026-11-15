/* Color emoji and the emoji picker.
 *
 * A small reader for emoji fonts: cmap for code points, GSUB ligatures for
 * sequences (skin tones, joined people, flags, keycaps), and the color formats
 * in use: sbix and CBDT bitmaps (PNG) and COLR version 0 layers. A font is read
 * from its file as needed, so a big one costs only the tables kept in memory.
 * Drawn emoji go to one texture, cell by cell, and the least recently used
 * cell is reused. The picker's list (names, groups, versions) is built in,
 * compressed, and inflated the first time it is needed. */
#include "cui_internal.h"
#include "cui_emoji_data.h"
#include <stdio.h>
#include <stdlib.h>

#define CUI__EM_ATLAS_MAX 1024
#define CUI__EM_IDLE 600   /* frames without an emoji before the texture is let go */
#define CUI__EM_SLOTS_MAX 1024
#define CUI__EM_BUDGET 12   /* new emoji drawn per frame; the rest come next frame */

typedef struct { uint32_t key; uint16_t px; uint8_t state; bool mono; uint64_t used; } cui__em_slot;   /* state: 0 empty, 1 drawn, 2 missing */

typedef struct { uint32_t text, name; uint8_t len, name_len, group, version; bool tone; } cui__em_rec;

typedef struct cui__emoji {
    /* the font */
    unsigned char *mem; size_t size; bool owned;
    FILE *file;
    uint32_t base;                          /* the font inside a collection */
    uint8_t *cmap; uint32_t cmap_len, cmap_sub; int cmap_fmt;
    uint8_t *gsub; uint32_t gsub_len;
    uint8_t *cblc; uint32_t cblc_len, cbdt;
    uint32_t sbix, sbix_strike[16]; uint16_t sbix_ppem[16]; int sbix_n;
    uint8_t *colr, *cpal; uint32_t colr_len, cpal_len;
    cui__tt *tt; bool mono_font;
    int num_glyphs;
    bool loaded, tried_system;
    /* the texture */
    cui_texture tex; int atlas, cell, per_row, nslots;
    uint64_t last_draw;                     /* the frame an emoji was last drawn */
    cui__em_slot slots[CUI__EM_SLOTS_MAX];
    uint64_t budget_frame; int budget;
    /* the picker's list */
    char *data; cui__em_rec *recs; int nrec; uint8_t *avail; bool avail_font;
} cui__emoji;

static uint16_t cui__be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t cui__be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
/* bounded reads in a table held in memory: out of range reads 0 */
static int cui__t16(const uint8_t *t, uint32_t len, uint64_t off) { return t && off + 2 <= len ? cui__be16(t + off) : 0; }
static uint32_t cui__t32(const uint8_t *t, uint32_t len, uint64_t off) { return t && off + 4 <= len ? cui__be32(t + off) : 0; }
#define CUI__T16(t, len, off) cui__t16((t), (len), (uint64_t)(off))
#define CUI__T32(t, len, off) cui__t32((t), (len), (uint64_t)(off))

static cui__emoji *cui__em(cui_ctx *ui) {
    if (!ui->emoji) {
        ui->emoji = cui__alloc(ui, sizeof(cui__emoji));
        if (ui->emoji) memset(ui->emoji, 0, sizeof(cui__emoji));
    }
    return ui->emoji;
}

static bool cui__em_read(cui__emoji *e, uint32_t off, void *dst, uint32_t n) {
    if ((uint64_t)off + n > e->size) return false;
    if (e->mem) { memcpy(dst, e->mem + off, n); return true; }
    if (!e->file || fseek(e->file, (long)off, SEEK_SET)) return false;
    return fread(dst, 1, n, e->file) == n;
}

static uint8_t *cui__em_table(cui_ctx *ui, cui__emoji *e, uint32_t off, uint32_t len) {
    if (!len || len > (64u << 20)) return NULL;
    uint8_t *t = cui__alloc(ui, len);
    if (t && !cui__em_read(e, off, t, len)) { cui__free(ui, t); t = NULL; }
    return t;
}

static void cui__em_close_font(cui_ctx *ui, cui__emoji *e) {
    cui__free(ui, e->cmap); cui__free(ui, e->gsub); cui__free(ui, e->cblc); cui__free(ui, e->colr); cui__free(ui, e->cpal);
    cui__free(ui, e->tt);
    if (e->owned) cui__free(ui, e->mem);
    if (e->file) fclose(e->file);
    e->cmap = e->gsub = e->cblc = e->colr = e->cpal = NULL; e->tt = NULL; e->mem = NULL; e->file = NULL;
    e->owned = e->loaded = e->mono_font = false; e->sbix_n = 0; e->sbix = e->cbdt = 0; e->size = 0;
    for (int i = 0; i < e->nslots; i++) e->slots[i].state = 0;   /* drawn with the old font */
    e->avail_font = false;
}

void cui__emoji_drop_texture(cui_ctx *ui) {
    cui__emoji *e = ui->emoji;
    if (!e) return;
    if (e->tex && ui->renderer.texture_destroy) ui->renderer.texture_destroy(ui->renderer.user, e->tex);
    e->tex = 0;
    for (int i = 0; i < e->nslots; i++) e->slots[i].state = 0;
}

/* once no emoji has been drawn for a while (the picker closed), the texture goes;
 * it is made again, and filled as needed, the next time one is drawn */
void cui__emoji_frame(cui_ctx *ui) {
    cui__emoji *e = ui->emoji;
    if (e && e->tex && ui->frame - e->last_draw > CUI__EM_IDLE) cui__emoji_drop_texture(ui);
}

void cui__emoji_shutdown(cui_ctx *ui) {
    cui__emoji *e = ui->emoji;
    if (!e) return;
    cui__emoji_drop_texture(ui);
    cui__em_close_font(ui, e);
    cui__free(ui, e->data); cui__free(ui, e->recs); cui__free(ui, e->avail);
    cui__free(ui, e);
    ui->emoji = NULL;
}

/* ------------------------------------------------------------------------ */
/* Opening a font                                                           */
/* ------------------------------------------------------------------------ */

static bool cui__em_open(cui_ctx *ui, cui__emoji *e) {
    uint8_t h[12];
    if (!cui__em_read(e, 0, h, 12)) return false;
    e->base = 0;
    if (!memcmp(h, "ttcf", 4)) {   /* a collection: the first font */
        uint8_t o[4];
        if (!cui__em_read(e, 12, o, 4)) return false;
        e->base = cui__be32(o);
        if (!cui__em_read(e, e->base, h, 12)) return false;
    }
    int n = cui__be16(h + 4);
    if (n <= 0 || n > 200) return false;
    uint8_t dir[200 * 16];
    if (!cui__em_read(e, e->base + 12, dir, (uint32_t)n * 16)) return false;
    uint32_t cmap = 0, cmap_l = 0, gsub = 0, gsub_l = 0, cblc = 0, cblc_l = 0, colr = 0, colr_l = 0, cpal = 0, cpal_l = 0, maxp = 0;
    bool glyf = false, cff = false;
    for (int i = 0; i < n; i++) {
        const uint8_t *r = dir + i * 16;
        uint32_t off = cui__be32(r + 8), len = cui__be32(r + 12);
        if (!memcmp(r, "cmap", 4)) { cmap = off; cmap_l = len; }
        else if (!memcmp(r, "GSUB", 4)) { gsub = off; gsub_l = len; }
        else if (!memcmp(r, "CBLC", 4)) { cblc = off; cblc_l = len; }
        else if (!memcmp(r, "CBDT", 4)) e->cbdt = off;
        else if (!memcmp(r, "sbix", 4)) e->sbix = off;
        else if (!memcmp(r, "COLR", 4)) { colr = off; colr_l = len; }
        else if (!memcmp(r, "CPAL", 4)) { cpal = off; cpal_l = len; }
        else if (!memcmp(r, "maxp", 4)) maxp = off;
        else if (!memcmp(r, "glyf", 4)) glyf = true;
        else if (!memcmp(r, "CFF ", 4)) cff = true;
    }
    if (!cmap) return false;
    uint8_t mp[6];
    if (maxp && cui__em_read(e, maxp, mp, 6)) e->num_glyphs = cui__be16(mp + 4);
    e->cmap = cui__em_table(ui, e, cmap, cmap_l); e->cmap_len = cmap_l;
    if (!e->cmap) return false;
    /* the best subtable: full Unicode (format 12), else the basic plane (format 4) */
    int nsub = CUI__T16(e->cmap, cmap_l, 2);
    for (int pass = 0; pass < 2 && !e->cmap_fmt; pass++)
        for (int i = 0; i < nsub; i++) {
            uint32_t rec = 4 + (uint32_t)i * 8;
            uint16_t pid = CUI__T16(e->cmap, cmap_l, rec), eid = CUI__T16(e->cmap, cmap_l, rec + 2);
            uint32_t off = CUI__T32(e->cmap, cmap_l, rec + 4);
            uint16_t fmt = CUI__T16(e->cmap, cmap_l, off);
            bool uni = pid == 0 || (pid == 3 && (eid == 10 || eid == 1));
            if (uni && fmt == (pass == 0 ? 12 : 4)) { e->cmap_fmt = fmt; e->cmap_sub = off; break; }
        }
    if (!e->cmap_fmt) return false;
    if (gsub) { e->gsub = cui__em_table(ui, e, gsub, gsub_l); e->gsub_len = e->gsub ? gsub_l : 0; }
    if (cblc && e->cbdt) { e->cblc = cui__em_table(ui, e, cblc, cblc_l); e->cblc_len = e->cblc ? cblc_l : 0; }
    if (e->sbix && e->num_glyphs) {
        uint8_t sh[8];
        if (cui__em_read(e, e->sbix, sh, 8)) {
            int ns = (int)CUI_MIN(cui__be32(sh + 4), 16u);
            for (int i = 0; i < ns; i++) {
                uint8_t so[4], sp[2];
                if (!cui__em_read(e, e->sbix + 8 + (uint32_t)i * 4, so, 4)) break;
                uint32_t st = e->sbix + cui__be32(so);
                if (!cui__em_read(e, st, sp, 2)) break;
                e->sbix_strike[e->sbix_n] = st; e->sbix_ppem[e->sbix_n] = cui__be16(sp); e->sbix_n++;
            }
        }
    }
    if (colr && cpal) {
        e->colr = cui__em_table(ui, e, colr, colr_l); e->colr_len = e->colr ? colr_l : 0;
        e->cpal = cui__em_table(ui, e, cpal, cpal_l); e->cpal_len = e->cpal ? cpal_l : 0;
        if (e->colr && CUI__T16(e->colr, e->colr_len, 2) == 0) {   /* only version 1 paint records: not drawable here */
            cui__free(ui, e->colr); cui__free(ui, e->cpal); e->colr = e->cpal = NULL;
        }
    }
    /* layers and plain outline fonts are drawn with stb_truetype, which wants the font in memory */
    bool outlines = (glyf || cff) && !e->cblc && !e->sbix_n;
    if (outlines) {
        if (!e->mem) {
            if (e->size > (96u << 20)) return false;
            unsigned char *m = cui__alloc(ui, e->size);
            if (!m || !cui__em_read(e, 0, m, (uint32_t)e->size)) { cui__free(ui, m); return false; }
            e->mem = m; e->owned = true;
            fclose(e->file); e->file = NULL;
        }
        e->tt = cui__tt_open(ui, e->mem, (int)e->base);
        if (!e->tt) return false;
        e->mono_font = !e->colr;
    }
    if (!e->cblc && !e->sbix_n && !e->tt) return false;
    e->loaded = true;
    return true;
}

bool cui_emoji_font_load_memory(cui_ctx *ui, const void *data, size_t size, bool copy) {
    cui__emoji *e = cui__em(ui);
    if (!e || !data || size < 12 || size > 0x7FFFFFFF) return false;
    cui__em_close_font(ui, e);
    e->mem = (unsigned char *)data; e->size = size;
    if (copy) {
        e->mem = cui__alloc(ui, size);
        if (!e->mem) return false;
        memcpy(e->mem, data, size);
        e->owned = true;
    }
    if (!cui__em_open(ui, e)) { cui__em_close_font(ui, e); return false; }
    ui->settle_frames = CUI_MAX(ui->settle_frames, 2);
    return true;
}

bool cui_emoji_font_load_file(cui_ctx *ui, const char *path) {
    cui__emoji *e = cui__em(ui);
    if (!e || !path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
    long n = ftell(f);
    if (n < 12 || n > 0x7FFFFFFF) { fclose(f); return false; }
    cui__em_close_font(ui, e);
    e->file = f; e->size = (size_t)n;
    if (!cui__em_open(ui, e)) { cui__em_close_font(ui, e); return false; }
    ui->settle_frames = CUI_MAX(ui->settle_frames, 2);
    return true;
}

bool cui_emoji_font_load_system(cui_ctx *ui) {
    cui__emoji *e = cui__em(ui);
    if (!e) return false;
    e->tried_system = true;
#if defined(_WIN32)
    char p[300];
    const char *win = getenv("WINDIR");
    snprintf(p, sizeof p, "%s\\Fonts\\seguiemj.ttf", win ? win : "C:\\Windows");
    return cui_emoji_font_load_file(ui, p);
#elif defined(__APPLE__)
    return cui_emoji_font_load_file(ui, "/System/Library/Fonts/Apple Color Emoji.ttc");
#elif defined(__wasm__) || defined(__EMSCRIPTEN__) || defined(CUI_NO_FILESYSTEM)
    return false;
#else
    static const char *const paths[] = {
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", "/usr/share/fonts/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf", "/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf",
        "/usr/share/fonts/truetype/noto-color-emoji/NotoColorEmoji.ttf", "/usr/share/fonts/TTF/NotoColorEmoji.ttf",
        "/usr/share/fonts/google-noto-color-emoji-fonts/NotoColorEmoji.ttf", "/usr/local/share/fonts/NotoColorEmoji.ttf",
        "/system/fonts/NotoColorEmoji.ttf",
    };
    for (size_t i = 0; i < sizeof paths / sizeof *paths; i++)
        if (cui_emoji_font_load_file(ui, paths[i])) return true;
    return false;
#endif
}

/* ------------------------------------------------------------------------ */
/* Code points to a glyph                                                   */
/* ------------------------------------------------------------------------ */

static int cui__em_cmap(const cui__emoji *e, uint32_t cp) {
    const uint8_t *t = e->cmap;
    uint32_t L = e->cmap_len, s = e->cmap_sub;
    if (e->cmap_fmt == 12) {
        uint32_t n = CUI__T32(t, L, s + 12), lo = 0, hi = n;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2, g = s + 16 + mid * 12;
            uint32_t a = CUI__T32(t, L, g), b = CUI__T32(t, L, g + 4);
            if (cp < a) hi = mid;
            else if (cp > b) lo = mid + 1;
            else return (int)(CUI__T32(t, L, g + 8) + (cp - a));
        }
        return 0;
    }
    if (cp > 0xFFFF) return 0;
    uint32_t seg2 = CUI__T16(t, L, s + 6);
    uint32_t ends = s + 14, starts = ends + seg2 + 2, deltas = starts + seg2, ranges = deltas + seg2;
    uint32_t lo = 0, hi = seg2 / 2;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        uint32_t end = CUI__T16(t, L, ends + mid * 2), start = CUI__T16(t, L, starts + mid * 2);
        if (cp > end) lo = mid + 1;
        else if (cp < start) hi = mid;
        else {
            uint32_t ro = CUI__T16(t, L, ranges + mid * 2);
            uint16_t delta = CUI__T16(t, L, deltas + mid * 2);
            if (!ro) return (uint16_t)(cp + delta);
            uint32_t gi = CUI__T16(t, L, ranges + mid * 2 + ro + (cp - start) * 2);
            return gi ? (uint16_t)(gi + delta) : 0;
        }
    }
    return 0;
}

static int cui__em_coverage(const uint8_t *t, uint32_t L, uint32_t off, int gid) {
    uint16_t fmt = CUI__T16(t, L, off), n = CUI__T16(t, L, off + 2);
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (fmt == 1) {
            int g = CUI__T16(t, L, off + 4 + (uint32_t)mid * 2);
            if (gid < g) hi = mid; else if (gid > g) lo = mid + 1; else return mid;
        } else if (fmt == 2) {
            uint32_t r = off + 4 + (uint32_t)mid * 6;
            int a = CUI__T16(t, L, r), b = CUI__T16(t, L, r + 2);
            if (gid < a) hi = mid; else if (gid > b) lo = mid + 1; else return CUI__T16(t, L, r + 4) + gid - a;
        } else return -1;
    }
    return -1;
}

/* one ligature substitution subtable at position i; returns how many glyphs it took (0: none) */
static int cui__em_liga(const uint8_t *t, uint32_t L, uint32_t sub, const int *g, int n, int i, int *out) {
    if (CUI__T16(t, L, sub) != 1) return 0;
    int ci = cui__em_coverage(t, L, sub + CUI__T16(t, L, sub + 2), g[i]);
    if (ci < 0 || ci >= CUI__T16(t, L, sub + 4)) return 0;
    uint32_t set = sub + CUI__T16(t, L, sub + 6 + (uint32_t)ci * 2);
    int nl = CUI__T16(t, L, set);
    for (int k = 0; k < nl; k++) {
        uint32_t lig = set + CUI__T16(t, L, set + 2 + (uint32_t)k * 2);
        int comp = CUI__T16(t, L, lig + 2);
        if (comp < 1 || i + comp > n) continue;
        bool match = true;
        for (int c = 1; c < comp && match; c++) match = g[i + c] == (int)CUI__T16(t, L, lig + 4 + (uint32_t)(c - 1) * 2);
        if (match) { *out = CUI__T16(t, L, lig); return comp; }
    }
    return 0;
}

/* runs every ligature lookup over the glyphs, in order */
static int cui__em_gsub(const cui__emoji *e, int *g, int n) {
    const uint8_t *t = e->gsub;
    uint32_t L = e->gsub_len;
    if (!t || n < 2) return n;
    uint32_t list = CUI__T16(t, L, 8);
    int nlook = CUI__T16(t, L, list);
    for (int li = 0; li < nlook && n > 1; li++) {
        uint32_t look = list + CUI__T16(t, L, list + 2 + (uint32_t)li * 2);
        int type = CUI__T16(t, L, look), nsub = CUI__T16(t, L, look + 4);
        if (type != 4 && type != 7) continue;
        for (int i = 0; i < n; i++) {
            for (int si = 0; si < nsub; si++) {
                uint32_t sub = look + CUI__T16(t, L, look + 6 + (uint32_t)si * 2);
                if (type == 7) {
                    if (CUI__T16(t, L, sub + 2) != 4) continue;
                    sub += CUI__T32(t, L, sub + 4);
                }
                int rep = 0, took = cui__em_liga(t, L, sub, g, n, i, &rep);
                if (took > 0) {
                    g[i] = rep;
                    memmove(g + i + 1, g + i + took, sizeof(int) * (size_t)(n - i - took));
                    n -= took - 1;
                    break;
                }
            }
        }
    }
    return n;
}

static int cui__utf8_cps(const char *s, int len, uint32_t *cps, int cap) {
    int n = 0;
    for (int i = 0; i < len && n < cap;) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int k;
        if (c < 0x80) { cp = c; k = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; k = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; k = 3; }
        else { cp = c & 0x07; k = 4; }
        if (i + k > len) break;
        for (int j = 1; j < k; j++) cp = cp << 6 | ((unsigned char)s[i + j] & 0x3F);
        cps[n++] = cp; i += k;
    }
    return n;
}

/* the glyph for a whole sequence, or 0 */
static int cui__em_glyph(const cui__emoji *e, const char *utf8, int len) {
    uint32_t cps[24];
    int n = cui__utf8_cps(utf8, len, cps, 24);
    if (!n) return 0;
    int g[24];
    for (int pass = 0; pass < 2; pass++) {
        int m = 0;
        bool missing = false;
        for (int i = 0; i < n; i++) {
            if (pass == 1 && cps[i] == 0xFE0F) continue;   /* some fonts list sequences without it */
            g[m] = cui__em_cmap(e, cps[i]);
            missing |= g[m] == 0 && cps[i] != 0xFE0F;
            m++;
        }
        if (missing) return 0;
        /* a lone emoji with its presentation selector: the base glyph does */
        if (m == 2 && cps[n - 1] == 0xFE0F && pass == 0) { int one[2] = { g[0], g[1] }; if (cui__em_gsub(e, one, 2) == 1) return one[0]; return g[0]; }
        m = cui__em_gsub(e, g, m);
        if (m == 1 && g[0]) return g[0];
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* A glyph to pixels                                                        */
/* ------------------------------------------------------------------------ */

/* the PNG of a glyph from CBDT or sbix; *data is allocated */
static bool cui__em_png(cui_ctx *ui, cui__emoji *e, int gid, int px, uint8_t **data, uint32_t *len) {
    if (e->cblc) {
        const uint8_t *t = e->cblc;
        uint32_t L = e->cblc_len;
        int ns = (int)CUI__T32(t, L, 4), best = -1, best_ppem = 0;
        for (int i = 0; i < ns && i < 64; i++) {
            uint32_t bs = 8 + (uint32_t)i * 48;
            int s = CUI__T16(t, L, bs + 40), en = CUI__T16(t, L, bs + 42), ppem = t[CUI_MIN(bs + 44, L - 1)];
            if (gid < s || gid > en) continue;
            bool better = best < 0 || (best_ppem < px ? ppem > best_ppem : (ppem >= px && ppem < best_ppem));
            if (better) { best = i; best_ppem = ppem; }
        }
        if (best >= 0) {
            uint32_t bs = 8 + (uint32_t)best * 48, arr = CUI__T32(t, L, bs), nsub = CUI__T32(t, L, bs + 8);
            for (uint32_t k = 0; k < nsub && k < 4096; k++) {
                uint32_t ent = arr + k * 8;
                int first = CUI__T16(t, L, ent), last = CUI__T16(t, L, ent + 2);
                if (gid < first || gid > last) continue;
                uint32_t st = arr + CUI__T32(t, L, ent + 4);
                int ifmt = CUI__T16(t, L, st), imgfmt = CUI__T16(t, L, st + 2);
                uint32_t base = CUI__T32(t, L, st + 4), o0 = 0, o1 = 0;
                int rel = gid - first;
                bool found = true;
                if (ifmt == 1) { o0 = CUI__T32(t, L, st + 8 + (uint32_t)rel * 4); o1 = CUI__T32(t, L, st + 12 + (uint32_t)rel * 4); }
                else if (ifmt == 3) { o0 = CUI__T16(t, L, st + 8 + (uint32_t)rel * 2); o1 = CUI__T16(t, L, st + 10 + (uint32_t)rel * 2); }
                else if (ifmt == 2) { uint32_t sz = CUI__T32(t, L, st + 8); o0 = sz * (uint32_t)rel; o1 = o0 + sz; }
                else if (ifmt == 4 || ifmt == 5) {
                    found = false;
                    uint32_t ng = CUI__T32(t, L, st + (ifmt == 4 ? 8 : 20));
                    for (uint32_t q = 0; q < ng && q < 65536; q++) {
                        if (ifmt == 4) {
                            if (CUI__T16(t, L, st + 12 + q * 4) == gid) { o0 = CUI__T16(t, L, st + 14 + q * 4); o1 = CUI__T16(t, L, st + 18 + q * 4); found = true; break; }
                        } else if (CUI__T16(t, L, st + 24 + q * 2) == gid) { uint32_t sz = CUI__T32(t, L, st + 8); o0 = sz * q; o1 = o0 + sz; found = true; break; }
                    }
                } else found = false;
                if (!found || o1 <= o0 || o1 - o0 > (8u << 20)) return false;
                uint32_t at = e->cbdt + base + o0, skip = imgfmt == 17 ? 5 : imgfmt == 18 ? 8 : imgfmt == 19 ? 0 : 0xFFFFFFFF;
                if (skip == 0xFFFFFFFF) return false;
                uint8_t dl[4];
                if (!cui__em_read(e, at + skip, dl, 4)) return false;
                uint32_t n = cui__be32(dl);
                if (n == 0 || n > o1 - o0) return false;
                *data = cui__alloc(ui, n);
                if (!*data || !cui__em_read(e, at + skip + 4, *data, n)) { cui__free(ui, *data); *data = NULL; return false; }
                *len = n;
                return true;
            }
        }
    }
    if (e->sbix_n && gid < e->num_glyphs) {
        int best = 0;
        for (int i = 1; i < e->sbix_n; i++) {
            int a = e->sbix_ppem[best], b = e->sbix_ppem[i];
            if (a < px ? b > a : (b >= px && b < a)) best = i;
        }
        for (int hop = 0; hop < 2; hop++) {
            uint8_t o[8];
            if (!cui__em_read(e, e->sbix_strike[best] + 4 + (uint32_t)gid * 4, o, 8)) return false;
            uint32_t a = cui__be32(o), b = cui__be32(o + 4);
            if (b <= a + 8 || b - a > (8u << 20)) return false;
            uint8_t gh[10];
            uint32_t at = e->sbix_strike[best] + a;
            if (!cui__em_read(e, at, gh, 10)) return false;
            if (!memcmp(gh + 4, "dupe", 4)) { gid = cui__be16(gh + 8); continue; }
            if (memcmp(gh + 4, "png ", 4)) return false;
            uint32_t n = b - a - 8;
            *data = cui__alloc(ui, n);
            if (!*data || !cui__em_read(e, at + 8, *data, n)) { cui__free(ui, *data); *data = NULL; return false; }
            *len = n;
            return true;
        }
    }
    return false;
}

/* src (sw by sh, straight alpha) scaled into dst at (dx, dy) as dw by dh, an area average */
static void cui__em_resample(cui_ctx *ui, const uint8_t *src, int sw, int sh, uint8_t *dst, int dstride, int dx, int dy, int dw, int dh) {
    if (dw > sw || dh > sh) {   /* enlarging: bilinear, weighted by alpha so edges keep their color */
        for (int y = 0; y < dh; y++) {
            float fy = CUI_CLAMP(((float)y + 0.5f) * sh / dh - 0.5f, 0.0f, (float)(sh - 1));
            int y0 = (int)fy, y1 = CUI_MIN(y0 + 1, sh - 1);
            float ty = fy - y0;
            for (int x = 0; x < dw; x++) {
                float fx = CUI_CLAMP(((float)x + 0.5f) * sw / dw - 0.5f, 0.0f, (float)(sw - 1));
                int x0 = (int)fx, x1 = CUI_MIN(x0 + 1, sw - 1);
                float tx = fx - x0, acc[4] = { 0, 0, 0, 0 };
                const int xs[4] = { x0, x1, x0, x1 }, ys[4] = { y0, y0, y1, y1 };
                const float ws[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
                for (int k = 0; k < 4; k++) {
                    const uint8_t *q = src + ((size_t)ys[k] * sw + xs[k]) * 4;
                    float a = q[3] * ws[k];
                    acc[0] += q[0] * a; acc[1] += q[1] * a; acc[2] += q[2] * a; acc[3] += a;
                }
                uint8_t *o = dst + (size_t)(dy + y) * dstride + (size_t)(dx + x) * 4;
                if (acc[3] <= 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
                for (int ch = 0; ch < 3; ch++) o[ch] = (uint8_t)CUI_MIN(255.0f, acc[ch] / acc[3] + 0.5f);
                o[3] = (uint8_t)CUI_MIN(255.0f, acc[3] + 0.5f);
            }
        }
        return;
    }
    /* shrinking: an area average in two passes (across, then down), with each output
     * column's and row's source pixels and weights worked out once; colors weighted by alpha */
    enum { TAPS = 8 };
    if ((sw + dw - 1) / dw + 1 > TAPS || (sh + dh - 1) / dh + 1 > TAPS) return;   /* more than 7 to 1: not worth a path */
    size_t tbytes = sizeof(float) * 4 * (size_t)dw * (size_t)sh;
    size_t wbytes = (sizeof(int) + sizeof(float) * TAPS) * (size_t)(dw + dh);
    uint8_t *mem = cui__alloc(ui, tbytes + wbytes);
    if (!mem) return;
    float *tmp = (float *)mem;
    int *cs = (int *)(mem + tbytes), *rs = cs + dw;
    float *cw = (float *)(rs + dh), *rw = cw + (size_t)dw * TAPS;
    /* weights: the overlap of each source pixel with the output pixel, summing to 1 */
    for (int pass = 0; pass < 2; pass++) {
        int n = pass ? dh : dw, sn = pass ? sh : sw;
        int *st = pass ? rs : cs;
        float *wt = pass ? rw : cw;
        for (int o = 0; o < n; o++) {
            float a = (float)o * sn / n, b = (float)(o + 1) * sn / n, inv = (float)n / sn;
            st[o] = (int)a;
            for (int k = 0; k < TAPS; k++) {
                float p0 = (float)(st[o] + k), p1 = p0 + 1;
                float w = CUI_MIN(p1, b) - CUI_MAX(p0, a);
                wt[(size_t)o * TAPS + k] = (w > 0 && st[o] + k < sn) ? w * inv : 0;
            }
        }
    }
    for (int y = 0; y < sh; y++) {
        const uint8_t *row = src + (size_t)y * sw * 4;
        float *t = tmp + (size_t)y * dw * 4;
        for (int x = 0; x < dw; x++) {
            const uint8_t *p = row + (size_t)cs[x] * 4;
            const float *w = cw + (size_t)x * TAPS;
            float r = 0, g = 0, b = 0, al = 0;
            for (int k = 0; k < TAPS; k++, p += 4) {
                if (w[k] == 0) break;
                float a = p[3] * w[k];
                r += p[0] * a; g += p[1] * a; b += p[2] * a; al += a;
            }
            t[0] = r; t[1] = g; t[2] = b; t[3] = al; t += 4;
        }
    }
    for (int y = 0; y < dh; y++) {
        const float *w = rw + (size_t)y * TAPS;
        uint8_t *q = dst + (size_t)(dy + y) * dstride + (size_t)dx * 4;
        for (int x = 0; x < dw; x++, q += 4) {
            const float *t = tmp + ((size_t)rs[y] * dw + x) * 4;
            float r = 0, g = 0, b = 0, al = 0;
            for (int k = 0; k < TAPS; k++, t += (size_t)dw * 4) {
                if (w[k] == 0) break;
                r += t[0] * w[k]; g += t[1] * w[k]; b += t[2] * w[k]; al += t[3] * w[k];
            }
            if (al <= 0) { q[0] = q[1] = q[2] = q[3] = 0; continue; }
            float inv = 1.0f / al;
            q[0] = (uint8_t)CUI_MIN(255.0f, r * inv + 0.5f);
            q[1] = (uint8_t)CUI_MIN(255.0f, g * inv + 0.5f);
            q[2] = (uint8_t)CUI_MIN(255.0f, b * inv + 0.5f);
            q[3] = (uint8_t)CUI_MIN(255.0f, al + 0.5f);
        }
    }
    cui__free(ui, mem);
}

/* layers (COLR) or one outline into out, px by px; *mono when it is one color to be tinted */
static bool cui__em_outline(cui_ctx *ui, cui__emoji *e, int gid, int px, uint8_t *out, int stride, bool *mono) {
    int asc, desc;
    cui__tt_vmetrics(e->tt, &asc, &desc);
    if (asc - desc <= 0) return false;
    float scale = (float)px / (float)(asc - desc) * 0.94f;
    /* the layers, or the glyph itself */
    int first = 0, nl = 0;
    const uint8_t *c = e->colr;
    uint32_t CL = e->colr_len;
    if (c) {
        int nb = CUI__T16(c, CL, 2);
        uint32_t bo = CUI__T32(c, CL, 4);
        int lo = 0, hi = nb;
        while (lo < hi) {
            int mid = (lo + hi) / 2, g = CUI__T16(c, CL, bo + (uint32_t)mid * 6);
            if (gid < g) hi = mid; else if (gid > g) lo = mid + 1;
            else { first = CUI__T16(c, CL, bo + (uint32_t)mid * 6 + 2); nl = CUI__T16(c, CL, bo + (uint32_t)mid * 6 + 4); break; }
        }
    }
    *mono = nl == 0;
    if (nl == 0 && !e->mono_font) return false;
    int count = nl ? nl : 1;
    /* the box of all layers, centered */
    int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -(1 << 30), by1 = -(1 << 30);
    uint32_t lo_ = CUI__T32(c, CL, 8);
    for (int k = 0; k < count; k++) {
        int lg = nl ? CUI__T16(c, CL, lo_ + (uint32_t)(first + k) * 4) : gid;
        int x0, y0, x1, y1;
        if (!cui__tt_box(e->tt, lg, scale, &x0, &y0, &x1, &y1)) continue;
        bx0 = CUI_MIN(bx0, x0); by0 = CUI_MIN(by0, y0); bx1 = CUI_MAX(bx1, x1); by1 = CUI_MAX(by1, y1);
    }
    if (bx1 <= bx0) return false;
    int ox = (px - (bx1 - bx0)) / 2 - bx0, oy = (int)(asc * scale + (px - (asc - desc) * scale) * 0.5f);
    const uint8_t *p = e->cpal;
    uint32_t PL = e->cpal_len, recs = CUI__T32(p, PL, 8), pal0 = CUI__T16(p, PL, 12);
    for (int k = 0; k < count; k++) {
        int lg = nl ? CUI__T16(c, CL, lo_ + (uint32_t)(first + k) * 4) : gid;
        int pi = nl ? CUI__T16(c, CL, lo_ + (uint32_t)(first + k) * 4 + 2) : 0xFFFF;
        uint8_t col[4] = { 255, 255, 255, 255 };   /* the foreground: white, tinted when drawn */
        if (pi != 0xFFFF) {
            uint32_t at = recs + (pal0 + (uint32_t)pi) * 4;
            if (p && at + 4 <= PL) { col[0] = p[at + 2]; col[1] = p[at + 1]; col[2] = p[at]; col[3] = p[at + 3]; }
        }
        int x0, y0, x1, y1;
        if (!cui__tt_box(e->tt, lg, scale, &x0, &y0, &x1, &y1)) continue;
        int w = x1 - x0, h = y1 - y0, X = ox + x0, Y = oy + y0;
        if (w <= 0 || h <= 0) continue;
        uint8_t *gm = cui__alloc(ui, (size_t)w * h);
        if (!gm) continue;
        cui__tt_render(e->tt, lg, scale, gm, w, h, w);
        for (int yy = 0; yy < h; yy++) {
            int ty = Y + yy;
            if (ty < 0 || ty >= px) continue;
            for (int xx = 0; xx < w; xx++) {
                int tx = X + xx;
                if (tx < 0 || tx >= px) continue;
                float a = gm[yy * w + xx] / 255.0f * col[3] / 255.0f;
                if (a <= 0) continue;
                uint8_t *o = out + (size_t)ty * stride + (size_t)tx * 4;
                /* source over, straight alpha */
                float da = o[3] / 255.0f, ra = a + da * (1 - a);
                for (int ch = 0; ch < 3; ch++) o[ch] = (uint8_t)((col[ch] * a + o[ch] * da * (1 - a)) / ra + 0.5f);
                o[3] = (uint8_t)(ra * 255 + 0.5f);
            }
        }
        cui__free(ui, gm);
    }
    return true;
}

/* draws one emoji into out (px by px RGBA); false when it cannot be drawn */
static bool cui__em_raster(cui_ctx *ui, cui__emoji *e, const char *utf8, int len, int px, uint8_t *out, int stride, bool *mono) {
    *mono = false;
    if (e->loaded) {
        int gid = cui__em_glyph(e, utf8, len);
        if (gid) {
            if (e->tt) return cui__em_outline(ui, e, gid, px, out, stride, mono);
            uint8_t *png = NULL;
            uint32_t n = 0;
            if (cui__em_png(ui, e, gid, px * 2, &png, &n)) {
                int w = 0, h = 0;
                uint8_t *img = cui__png_decode(png, (int)n, &w, &h);
                cui__free(ui, png);
                if (!img) return false;
                int m = CUI_MAX(w, h), dw = CUI_MAX(1, px * w / m), dh = CUI_MAX(1, px * h / m);
                cui__em_resample(ui, img, w, h, out, stride, (px - dw) / 2, (px - dh) / 2, dw, dh);
                cui__png_free(img);
                return true;
            }
        }
        return false;
    }
    if (ui->platform.draw_emoji) {
        uint8_t *tmp = cui__alloc(ui, (size_t)px * px * 4);
        if (!tmp) return false;
        memset(tmp, 0, (size_t)px * px * 4);
        bool ok = ui->platform.draw_emoji(ui->platform.user, utf8, len, px, tmp);
        if (ok) for (int y = 0; y < px; y++) memcpy(out + (size_t)y * stride, tmp + (size_t)y * px * 4, (size_t)px * 4);
        cui__free(ui, tmp);
        return ok;
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* The texture                                                              */
/* ------------------------------------------------------------------------ */

static bool cui__em_can_draw(cui_ctx *ui) {
    cui__emoji *e = cui__em(ui);
    if (!e) return false;
    if (!e->loaded && !ui->platform.draw_emoji && !e->tried_system) cui_emoji_font_load_system(ui);
    return e->loaded || ui->platform.draw_emoji;
}

/* the slot holding this emoji at px, drawing it when there is time this frame; -1 for none yet */
static int cui__em_get_slot(cui_ctx *ui, cui__emoji *e, const char *utf8, int len, int px, bool *missing) {
    *missing = false;
    if (!ui->renderer.texture_create || !ui->renderer.texture_update) return -1;
    px = CUI_CLAMP(px, 8, 254);
    int need = (px + 2 + 7) & ~7;
    e->last_draw = ui->frame;
    if (need > e->cell) {   /* bigger cells: start over */
        cui__emoji_drop_texture(ui);
        e->cell = need;
        e->atlas = e->cell <= 40 ? 512 : CUI__EM_ATLAS_MAX;   /* small emoji fit a quarter of the texture */
        e->per_row = e->atlas / e->cell;
        e->nslots = CUI_MIN(e->per_row * e->per_row, CUI__EM_SLOTS_MAX);
        memset(e->slots, 0, sizeof e->slots);
    }
    if (!e->tex) {
        e->tex = ui->renderer.texture_create(ui->renderer.user, e->atlas, e->atlas, CUI_TEXTURE_RGBA8, NULL);
        if (!e->tex) return -1;
        for (int i = 0; i < e->nslots; i++) e->slots[i].state = 0;
    }
    uint32_t key = cui__hash_str("", 0) ^ 0x9E3779B9u;
    for (int i = 0; i < len; i++) key = (key ^ (unsigned char)utf8[i]) * 16777619u;
    key ^= (uint32_t)px * 2654435761u;
    int lru = -1;
    for (int i = 0; i < e->nslots; i++) {
        cui__em_slot *s = &e->slots[i];
        if (s->state && s->key == key && s->px == px) {
            s->used = ui->frame;
            *missing = s->state == 2;
            return s->state == 1 ? i : -1;
        }
        if (s->used != ui->frame && (lru < 0 || !s->state || (e->slots[lru].state && s->used < e->slots[lru].used))) lru = i;
    }
    if (lru < 0) return -1;   /* every cell is on screen */
    if (e->budget_frame != ui->frame) { e->budget_frame = ui->frame; e->budget = CUI__EM_BUDGET; }
    if (e->budget <= 0) { ui->animating = true; return -1; }
    e->budget--;
    int c = e->cell;
    uint8_t *buf = cui__alloc(ui, (size_t)c * c * 4);
    if (!buf) return -1;
    memset(buf, 0, (size_t)c * c * 4);
    bool mono = false;
    bool ok = cui__em_raster(ui, e, utf8, len, px, buf + (size_t)c * 4 + 4, c * 4, &mono);
    cui__em_slot *s = &e->slots[lru];
    s->key = key; s->px = (uint16_t)px; s->used = ui->frame; s->mono = mono;
    s->state = ok ? 1 : 2;
    if (ok) ui->renderer.texture_update(ui->renderer.user, e->tex, (lru % e->per_row) * c, (lru / e->per_row) * c, c, c, buf, c * 4);
    cui__free(ui, buf);
    *missing = !ok;
    return ok ? lru : -1;
}

/* draws at r; false when the emoji cannot be drawn (missing, or not ready yet) */
static bool cui__em_draw(cui_ctx *ui, const char *utf8, int len, cui_rect r, float alpha, bool *missing) {
    bool miss = false;
    if (missing) *missing = false;
    if (!cui__em_can_draw(ui)) { if (missing) *missing = true; return false; }
    cui__emoji *e = ui->emoji;
    float side = CUI_MIN(r.w, r.h);
    int px = (int)ceilf(side * ui->scale);
    int i = cui__em_get_slot(ui, e, utf8, len, px, &miss);
    if (missing) *missing = miss;
    if (i < 0) return false;
    px = e->slots[i].px;
    float c = (float)e->cell, A = (float)e->atlas;
    float x = (float)(i % e->per_row) * c + 1, y = (float)(i / e->per_row) * c + 1;
    cui_rect uv = { x / A, y / A, px / A, px / A };
    cui_rect d = { r.x + (r.w - side) * 0.5f, r.y + (r.h - side) * 0.5f, side, side };
    cui_color tint = e->slots[i].mono ? CUI_C(ui, FOREGROUND) : cui__rgba(255, 255, 255, 255);
    cui_draw_image(ui, e->tex, d, uv, CUI_CORNERS(0), cui_alpha(tint, alpha));
    return true;
}

void cui_draw_emoji(cui_ctx *ui, const char *utf8, int len, cui_rect r) {
    if (!ui || !utf8) return;
    if (len < 0) len = (int)strlen(utf8);
    cui__em_draw(ui, utf8, len, r, 1, NULL);
}

bool cui_emoji_supported(cui_ctx *ui, const char *utf8, int len) {
    if (!ui || !utf8 || !cui__em_can_draw(ui)) return false;
    if (len < 0) len = (int)strlen(utf8);
    cui__emoji *e = ui->emoji;
    return e->loaded ? cui__em_glyph(e, utf8, len) != 0 : true;
}

typedef struct { char s[32]; int len; } cui__em_one;
static void cui__paint_emoji(cui_ctx *ui, cui_node *n, void *data) {
    cui__em_one *p = data;
    cui__em_draw(ui, p->s, p->len, n->rect, 1, NULL);
}

void cui_emoji(cui_ctx *ui, const char *utf8, float size) {
    if (size <= 0) size = 20;
    int leaf = cui__leaf(ui, 0, CUI_PX(size), CUI_PX(size));
    cui__em_one *p = cui__frame_alloc(ui, sizeof *p);
    if (!p || !utf8) return;
    p->len = (int)CUI_MIN(strlen(utf8), sizeof p->s - 1);
    memcpy(p->s, utf8, (size_t)p->len);
    ui->nodes[leaf].paint = cui__paint_emoji;
    ui->nodes[leaf].paint_data = p;
}

/* ------------------------------------------------------------------------ */
/* The list                                                                 */
/* ------------------------------------------------------------------------ */

static bool cui__em_list(cui_ctx *ui, cui__emoji *e) {
    if (e->recs) return true;
    e->data = cui__alloc(ui, CUI__EMOJI_RAW_SIZE + 1);
    e->recs = cui__alloc(ui, sizeof(cui__em_rec) * CUI__EMOJI_COUNT);
    e->avail = cui__alloc(ui, CUI__EMOJI_COUNT);
    if (!e->data || !e->recs || !e->avail || cui__inflate(cui__emoji_z, sizeof cui__emoji_z, e->data, CUI__EMOJI_RAW_SIZE) != CUI__EMOJI_RAW_SIZE) {
        cui__free(ui, e->data); cui__free(ui, e->recs); cui__free(ui, e->avail);
        e->data = NULL; e->recs = NULL; e->avail = NULL;
        return false;
    }
    e->data[CUI__EMOJI_RAW_SIZE] = 0;
    int group = -1;
    e->nrec = 0;
    for (uint32_t i = 0; i < CUI__EMOJI_RAW_SIZE && e->nrec < CUI__EMOJI_COUNT;) {
        uint32_t j = i;
        while (j < CUI__EMOJI_RAW_SIZE && e->data[j] != '\n') j++;
        e->data[j] = 0;
        if (e->data[i] == '#' && (unsigned char)e->data[i + 1] < 0x80) group++;   /* "#name"; a keycap # goes on with its selector */
        else {
            cui__em_rec *r = &e->recs[e->nrec++];
            uint32_t k = i;
            r->tone = e->data[k] == '+';
            if (r->tone) k++;
            r->text = k;
            /* the emoji runs up to the version letter, the first plain ASCII letter */
            while (k < j && ((unsigned char)e->data[k] >= 0x80 || e->data[k] == '#' || e->data[k] == '*' || (e->data[k] >= '0' && e->data[k] <= '9'))) k++;
            r->len = (uint8_t)(k - r->text);
            r->version = (uint8_t)(e->data[k] - 'A');
            r->name = k + 1;
            r->name_len = (uint8_t)CUI_MIN(255u, j - k - 1);
            r->group = (uint8_t)CUI_MAX(group, 0);
        }
        i = j + 1;
    }
    memset(e->avail, 1, CUI__EMOJI_COUNT);
    return true;
}

/* which emoji the font has; checked once per font */
static void cui__em_check(cui_ctx *ui, cui__emoji *e) {
    (void)ui;
    if (!e->loaded || e->avail_font) return;
    e->avail_font = true;
    for (int i = 0; i < e->nrec; i++) e->avail[i] = cui__em_glyph(e, e->data + e->recs[i].text, e->recs[i].len) != 0;
}

static bool cui__em_tone_base(uint32_t cp) {
    for (size_t i = 0; i < sizeof cui__emoji_tone_bases / sizeof *cui__emoji_tone_bases; i++)
        if (cp >= cui__emoji_tone_bases[i][0] && cp <= cui__emoji_tone_bases[i][1]) return true;
    return false;
}

/* the emoji with a skin tone (1..5) after each person in it */
static int cui__em_toned(const char *s, int len, int tone, char *out, int cap) {
    uint32_t cps[24];
    int n = cui__utf8_cps(s, len, cps, 24), o = 0;
    uint32_t mod = 0x1F3FA + (uint32_t)tone;
    bool last_toned = false;
    for (int i = 0; i < n && o < cap - 8; i++) {
        if (cps[i] == 0xFE0F && last_toned) continue;
        uint32_t c = cps[i];
        bool toned = cui__em_tone_base(c) && !(i > 0 && c == 0x1F91D);   /* a handshake joining two people takes none */
        for (int rep = 0; rep < (toned ? 2 : 1); rep++) {
            uint32_t v = rep ? mod : c;
            if (v < 0x80) out[o++] = (char)v;
            else if (v < 0x800) { out[o++] = (char)(0xC0 | v >> 6); out[o++] = (char)(0x80 | (v & 63)); }
            else if (v < 0x10000) { out[o++] = (char)(0xE0 | v >> 12); out[o++] = (char)(0x80 | (v >> 6 & 63)); out[o++] = (char)(0x80 | (v & 63)); }
            else { out[o++] = (char)(0xF0 | v >> 18); out[o++] = (char)(0x80 | (v >> 12 & 63)); out[o++] = (char)(0x80 | (v >> 6 & 63)); out[o++] = (char)(0x80 | (v & 63)); }
        }
        last_toned = toned;
    }
    out[o] = 0;
    return o;
}

static int cui__em_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* every word of the query starts a word of the name */
static bool cui__em_match(const char *name, int nlen, const char *q) {
    while (*q) {
        while (*q == ' ') q++;
        if (!*q) break;
        const char *w = q;
        while (*q && *q != ' ') q++;
        int wl = (int)(q - w);
        bool found = false;
        for (int i = 0; i + wl <= nlen && !found; i++) {
            if (i > 0 && name[i - 1] != ' ' && name[i - 1] != '-' && name[i - 1] != ':' && name[i - 1] != '(') continue;
            int k = 0;
            while (k < wl && cui__em_lower((unsigned char)name[i + k]) == cui__em_lower((unsigned char)w[k])) k++;
            found = k == wl;
        }
        if (!found) return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* The picker                                                               */
/* ------------------------------------------------------------------------ */

#define CUI__EM_RECENT_MAX 24
enum { CUI__EM_GROUPS = 9, CUI__EM_SECTIONS = CUI__EM_GROUPS + 2 };   /* recent, nine groups, search results */

static const char *const cui__em_group_names[10] = { "Recent", "Smileys & emotion", "People & body", "Animals & nature", "Food & drink",
                                                      "Travel & places", "Activities", "Objects", "Symbols", "Flags" };
static const char *const cui__em_tab_icons[10] = { NULL, "\xf0\x9f\x98\x80", "\xf0\x9f\x91\x8b", "\xf0\x9f\x90\xbb", "\xf0\x9f\x8d\x94", "\xe2\x9c\x88\xef\xb8\x8f",
                                                   "\xe2\x9a\xbd", "\xf0\x9f\x92\xa1", "\xf0\x9f\x94\xa3", "\xf0\x9f\x8f\x81" };

typedef struct { int start, count, kind; } cui__em_section;   /* kind: 0 recent, 1..9 a group, 10 results */

typedef struct {
    char query[64], built_query[64];
    int built_tone, built_recent_n; uint32_t built_recent_hash; bool built, built_avail;
    int *items; int nitems, cap;           /* >= 0 a list entry, < 0 a recent one: -(k + 1) */
    cui__em_section sec[CUI__EM_SECTIONS]; int nsec;
    char own_recent[CUI__EM_RECENT_MAX * 32];
    int own_tone;
    float top, target; bool animating;
    int cur; bool cur_visible;              /* keyboard cursor, an index into items */
    bool pressing, dragging; float press_y, press_top; int press_item;
    bool tone_open;
    int jump;                               /* a tab clicked during a search: its kind + 1 */
} cui__em_state;

static void cui__em_state_free(cui_ctx *ui, void *data) { cui__em_state *st = data; cui__free(ui, st->items); st->items = NULL; }

typedef struct {
    float cell, size, header, pad; int cols;
    int nsec; cui__em_section sec[CUI__EM_SECTIONS];
} cui__em_geo;

static int cui__em_rows(const cui__em_geo *g, int count) { return (count + g->cols - 1) / g->cols; }
static float cui__em_sec_h(const cui__em_geo *g, const cui__em_section *s) { return (s->kind == 10 ? 0 : g->header) + cui__em_rows(g, s->count) * g->cell; }
static float cui__em_total_h(const cui__em_geo *g) { float h = 0; for (int i = 0; i < g->nsec; i++) h += cui__em_sec_h(g, &g->sec[i]); return h + g->pad; }

/* the item at a point in content coordinates, or -1 */
static int cui__em_item_at(const cui__em_geo *g, float x, float y) {
    float top = 0;
    for (int i = 0; i < g->nsec; i++) {
        const cui__em_section *s = &g->sec[i];
        float h = cui__em_sec_h(g, s), hd = s->kind == 10 ? 0 : g->header;
        if (y >= top + hd && y < top + h) {
            int row = (int)((y - top - hd) / g->cell), col = (int)((x - g->pad) / g->cell);
            if (x < g->pad || col >= g->cols) return -1;
            int k = row * g->cols + col;
            return k < s->count ? s->start + k : -1;
        }
        top += h;
    }
    return -1;
}

/* where an item sits: its section, and its cell's top-left in content coordinates */
static int cui__em_item_pos(const cui__em_geo *g, int item, float *x, float *y) {
    float top = 0;
    for (int i = 0; i < g->nsec; i++) {
        const cui__em_section *s = &g->sec[i];
        float hd = s->kind == 10 ? 0 : g->header;
        if (item >= s->start && item < s->start + s->count) {
            int k = item - s->start;
            *x = g->pad + (k % g->cols) * g->cell;
            *y = top + hd + (k / g->cols) * g->cell;
            return i;
        }
        top += cui__em_sec_h(g, s);
    }
    *x = *y = 0;
    return -1;
}

static float cui__em_sec_top(const cui__em_geo *g, int si) {
    float top = 0;
    for (int i = 0; i < si && i < g->nsec; i++) top += cui__em_sec_h(g, &g->sec[i]);
    return top;
}

typedef struct { char s[64]; int len; } cui__em_str;

/* the text of an item, toned when it takes a tone */
static cui__em_str cui__em_item_text(const cui__emoji *e, const cui__em_state *st, char recent[][32], int item_v, int tone) {
    cui__em_str r;
    if (item_v < 0) {
        snprintf(r.s, sizeof r.s, "%s", recent[-item_v - 1]);
        r.len = (int)strlen(r.s);
        return r;
    }
    (void)st;
    const cui__em_rec *rec = &e->recs[item_v];
    if (rec->tone && tone > 0) r.len = cui__em_toned(e->data + rec->text, rec->len, tone, r.s, sizeof r.s);
    else { r.len = rec->len; memcpy(r.s, e->data + rec->text, rec->len); r.s[r.len] = 0; }
    return r;
}

typedef struct {
    cui__emoji *e; cui__em_state *st; cui__em_geo g;
    int *items; char (*recent)[32];
    int hover, cur; bool focus, soft; float top; int tone;
    const char *const *names; const char *empty; bool none; cui_id wheel;
    cui_color hover_bg, header, ring;
} cui__em_paint;

static void cui__paint_emoji_grid(cui_ctx *ui, cui_node *n, void *data) {
    cui__em_paint *p = data;
    cui_rect r = n->rect;
    cui__paint_wheel_hit(ui, p->wheel, r);
    const cui__em_geo *g = &p->g;
    if (p->none || g->nsec == 0) {
        float fs = cui__fsz(ui, 13);
        const char *msg = p->none ? "No emoji font found" : p->empty;
        float w = cui__text_width(ui, CUI_FONT_BODY, fs, msg, -1, false);
        cui__draw_text_line(ui, CUI_FONT_BODY, fs, r.x + (r.w - w) * 0.5f, r.y + r.h * 0.4f, CUI_C(ui, TEXT_MUTED), msg, -1, false);
        return;
    }
    cui_push_clip(ui, r);
    float y = r.y - p->top;
    float hs = cui__fsz(ui, 12);
    int sticky = -1;
    for (int si = 0; si < g->nsec; si++) {
        const cui__em_section *s = &g->sec[si];
        float h = cui__em_sec_h(g, s), hd = s->kind == 10 ? 0 : g->header;
        if (y + h < r.y) { y += h; continue; }
        if (y > r.y + r.h) break;
        if (hd > 0 && sticky < 0) sticky = si;
        if (hd > 0 && y >= r.y) cui__draw_text_line(ui, CUI_FONT_MEDIUM, hs, r.x + g->pad + 6, y + (hd - hs * 1.3f) * 0.5f + 2, p->header, p->names[s->kind], -1, false);
        int rows = cui__em_rows(g, s->count);
        int r0 = (int)CUI_MAX(0.0f, floorf((r.y - (y + hd)) / g->cell)), r1 = CUI_MIN(rows, (int)ceilf((r.y + r.h - (y + hd)) / g->cell));
        for (int row = r0; row < r1; row++)
            for (int c = 0; c < g->cols; c++) {
                int k = row * g->cols + c;
                if (k >= s->count) break;
                int item = s->start + k;
                cui_rect cr = { r.x + g->pad + c * g->cell, y + hd + row * g->cell, g->cell, g->cell };
                if (item == p->hover || (item == p->cur && (p->focus || p->soft))) cui_draw_rect(ui, cui__rect_inset(cr, 2), CUI_CORNERS(8), p->hover_bg);
                if (item == p->cur && p->focus) cui_draw_rect_border(ui, cui__rect_inset(cr, 2), CUI_CORNERS(8), cui__rgba(0, 0, 0, 0), p->ring, 2);
                cui__em_str t = cui__em_item_text(p->e, p->st, p->recent, p->items[item], p->tone);
                bool missing = false;
                if (!cui__em_draw(ui, t.s, t.len, cui__rect_inset(cr, (g->cell - g->size) * 0.5f), 1, &missing) && !missing)
                    cui_draw_rect(ui, cui__rect_inset(cr, g->cell * 0.3f), CUI_CORNERS(g->cell), cui_alpha(p->header, 0.12f));   /* drawn next frame */
            }
        y += h;
    }
    /* the group in view keeps its name at the top */
    if (sticky >= 0) {
        float st_top = r.y - p->top + cui__em_sec_top(g, sticky);
        if (st_top < r.y) {
            float next = st_top + cui__em_sec_h(g, &g->sec[sticky]);
            float hy = CUI_MIN(r.y, next - g->header);
            cui_draw_rect(ui, (cui_rect){ r.x, hy, r.w, g->header }, CUI_CORNERS(0), n->bg.a ? n->bg : CUI_C(ui, SURFACE));
            cui__draw_text_line(ui, CUI_FONT_MEDIUM, hs, r.x + g->pad + 6, hy + (g->header - hs * 1.3f) * 0.5f + 2, p->header, p->names[g->sec[sticky].kind], -1, false);
        }
    }
    cui_pop_clip(ui);
}

typedef struct { const char *s; int len; float alpha; bool icon, active; cui_color bar; } cui__em_tab_paint;
static void cui__paint_emoji_tab(cui_ctx *ui, cui_node *n, void *data) {
    cui__em_tab_paint *p = data;
    cui_rect r = n->rect;
    float side = 18;
    cui_rect d = { r.x + (r.w - side) * 0.5f, r.y + (r.h - side) * 0.5f, side, side };
    if (p->icon) cui_draw_icon(ui, p->s, d, side - 2, cui_alpha(CUI_C(ui, FOREGROUND), p->alpha * 0.9f));
    else cui__em_draw(ui, p->s, p->len, d, p->alpha, NULL);
    if (p->active) cui_draw_rect(ui, (cui_rect){ r.x + 6, r.y + r.h - 2, r.w - 12, 2 }, CUI_CORNERS(1), p->bar);
}

typedef struct { char s[256]; } cui__em_name_paint;
static void cui__paint_emoji_name(cui_ctx *ui, cui_node *n, void *data) {
    cui__em_name_paint *p = data;
    float fs = cui__fsz(ui, 13);
    cui__draw_text_ellipsis(ui, CUI_FONT_MEDIUM, fs, n->rect.x, n->rect.y + (n->rect.h - cui__font_line_height(ui, CUI_FONT_MEDIUM, fs)) * 0.5f,
                            n->rect.w, CUI_C(ui, FOREGROUND), p->s, -1, false);
}

static void cui__em_push_recent(char *buf, size_t cap, const char *em) {
    if (!buf || cap < 8) return;
    char old[CUI__EM_RECENT_MAX * 32 + 64];
    snprintf(old, sizeof old, "%s", buf);
    size_t n = (size_t)snprintf(buf, cap, "%s", em), el = strlen(em);
    int count = 1;
    for (char *t = old; *t && count < CUI__EM_RECENT_MAX;) {
        while (*t == ' ') t++;
        char *w = t;
        while (*t && *t != ' ') t++;
        size_t wl = (size_t)(t - w);
        if (!wl || (wl == el && !memcmp(w, em, el))) continue;
        if (n + 1 + wl >= cap) break;
        buf[n++] = ' '; memcpy(buf + n, w, wl); n += wl; buf[n] = 0;
        count++;
    }
}

static void cui__em_build(cui__emoji *e, cui__em_state *st, int nrecent) {
    int cap = e->nrec + CUI__EM_RECENT_MAX;
    st->nitems = 0; st->nsec = 0;
    if (!st->items || st->cap < cap) return;
    if (st->query[0]) {
        /* results: names that start with the query come first */
        cui__em_section *s = &st->sec[st->nsec++];
        s->start = 0; s->kind = 10;
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < e->nrec; i++) {
                if (!e->avail[i]) continue;
                const cui__em_rec *rec = &e->recs[i];
                const char *name = e->data + rec->name;
                bool m = cui__em_match(name, rec->name_len, st->query);
                if (!m) continue;
                size_t ql = strlen(st->query);
                bool lead = ql <= rec->name_len;
                for (size_t k = 0; k < ql && lead; k++) lead = cui__em_lower((unsigned char)name[k]) == cui__em_lower((unsigned char)st->query[k]);
                if ((pass == 0) == lead) st->items[st->nitems++] = i;
            }
        s->count = st->nitems;
        if (!s->count) st->nsec = 0;
        return;
    }
    if (nrecent > 0) {
        cui__em_section *s = &st->sec[st->nsec++];
        s->start = 0; s->kind = 0; s->count = nrecent;
        for (int k = 0; k < nrecent; k++) st->items[st->nitems++] = -(k + 1);
    }
    int gi = -1;
    for (int i = 0; i < e->nrec; i++) {
        if (!e->avail[i]) continue;
        int kind = e->recs[i].group + 1;
        if (gi < 0 || st->sec[gi].kind != kind) {
            if (st->nsec >= CUI__EM_SECTIONS) break;
            gi = st->nsec++;
            st->sec[gi] = (cui__em_section){ st->nitems, 0, kind };
        }
        st->items[st->nitems++] = i;
        st->sec[gi].count++;
    }
}

const char *cui_emoji_picker_ex(cui_ctx *ui, const char *id_str, const cui_emoji_picker_opts *o) {
    cui_emoji_picker_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    const char *picked = NULL;
    cui_id id = cui_make_id(ui, id_str ? id_str : "emoji");
    cui_push_id(ui, id_str ? id_str : "emoji");
    cui_id gid = cui_make_id(ui, "#grid"), wid = gid ^ 0x3E1Du, sid = cui_make_id(ui, "#search");
    cui__em_state *st = cui_state(ui, id, sizeof *st);
    cui__state_set_dtor(ui, id, cui__em_state_free);
    cui__emoji *e = cui__em(ui);
    bool can = e && cui__em_can_draw(ui) && cui__em_list(ui, e);
    if (can) cui__em_check(ui, e);
    const char *const *names = o->group_names ? o->group_names : cui__em_group_names;
    int *tone = o->tone ? o->tone : &st->own_tone;
    *tone = CUI_CLAMP(*tone, 0, 5);
    char *rbuf = o->recent && o->recent_cap ? o->recent : st->own_recent;
    size_t rcap = o->recent && o->recent_cap ? o->recent_cap : sizeof st->own_recent;
    /* recent emoji, as strings */
    char recent[CUI__EM_RECENT_MAX][32];
    int nrecent = 0;
    uint32_t rhash = 2166136261u;
    for (const char *t = rbuf; *t && nrecent < CUI__EM_RECENT_MAX;) {
        while (*t == ' ') t++;
        const char *w = t;
        while (*t && *t != ' ') t++;
        size_t wl = (size_t)(t - w);
        if (!wl || wl >= 32) continue;
        memcpy(recent[nrecent], w, wl); recent[nrecent][wl] = 0;
        for (size_t k = 0; k < wl; k++) rhash = (rhash ^ (unsigned char)w[k]) * 16777619u;
        nrecent++;
    }
    /* the list, rebuilt only when what it shows changes */
    if (can) {
        int cap = e->nrec + CUI__EM_RECENT_MAX;
        if (!st->items) { st->items = cui__alloc(ui, sizeof(int) * (size_t)cap); st->cap = st->items ? cap : 0; }
        if (!st->built || strcmp(st->built_query, st->query) || st->built_recent_hash != rhash || st->built_recent_n != nrecent || st->built_avail != e->avail_font) {
            cui__em_build(e, st, nrecent);
            snprintf(st->built_query, sizeof st->built_query, "%s", st->query);
            st->built_recent_hash = rhash; st->built_recent_n = nrecent; st->built_avail = e->avail_font; st->built = true;
            st->cur = st->query[0] && st->nitems ? 0 : -1;
            if (st->query[0]) st->top = st->target = 0;
        }
    } else st->nsec = 0;

    /* geometry, from the size the grid had last frame */
    float size = o->size > 0 ? o->size : 26;
    cui__em_geo g;
    memset(&g, 0, sizeof g);
    g.size = size; g.cell = size + 12; g.header = 30; g.pad = 6;
    cui_rect pr = cui__prev_rect(ui, gid);
    float gw = pr.w > 0 ? pr.w : 340;
    g.cols = CUI_MAX(4, (int)((gw - g.pad * 2) / g.cell));
    g.pad = floorf((gw - g.cols * g.cell) * 0.5f);
    g.nsec = st->nsec;
    memcpy(g.sec, st->sec, sizeof g.sec);
    if (st->jump && !st->query[0]) {
        for (int i = 0; i < g.nsec; i++) if (g.sec[i].kind == st->jump - 1) st->top = st->target = cui__em_sec_top(&g, i);
        st->jump = 0; st->animating = false;
    }
    float view_h = o->height > 0 ? o->height : 280;
    float maxtop = CUI_MAX(0.0f, cui__em_total_h(&g) - (pr.h > 0 ? pr.h : view_h));

    /* the pointer over the grid */
    cui_interact gr = cui__interact(ui, gid, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    int hover = -1;
    if (pr.w > 0 && gr.hovered && !st->dragging) hover = cui__em_item_at(&g, ui->mx - pr.x, ui->my - pr.y + st->top);
    if (ui->scroll_target == wid && ui->scroll_dy != 0) {
        st->target = CUI_CLAMP(st->target - ui->scroll_dy * 48, 0, maxtop);
        st->top = st->target; st->animating = false;
        ui->scroll_dy = 0;
    }
    if (gr.pressed && ui->mpressed[0]) { st->pressing = true; st->dragging = false; st->press_y = ui->my; st->press_top = st->top; st->press_item = hover; st->cur_visible = false; }
    if (st->pressing && ui->mdown[0]) {
        if (!st->dragging && fabsf(ui->my - st->press_y) > 6) st->dragging = true;   /* a drag scrolls, as on a phone */
        if (st->dragging) { st->top = st->target = CUI_CLAMP(st->press_top - (ui->my - st->press_y), 0, maxtop); st->animating = false; }
    }
    int pick_item = -1;
    if (st->pressing && !ui->mdown[0]) {
        if (!st->dragging && st->press_item >= 0 && st->press_item == hover) pick_item = hover;
        st->pressing = st->dragging = false;
    }
    if (gr.hovered && hover >= 0) cui__set_cursor(ui, CUI_CURSOR_HAND);

    /* keys: on the grid, or from the search field (Up, Down and Enter) */
    bool in_search = ui->focus == sid, in_grid = ui->focus == gid;
    if ((in_grid || in_search) && st->nitems > 0 && !st->tone_open) {
        int move = 0;
        bool horiz = in_grid;
        if (horiz && cui_key_pressed(ui, CUI_KEY_LEFT)) move = -1;
        if (horiz && cui_key_pressed(ui, CUI_KEY_RIGHT)) move = 1;
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) move = 2;
        if (cui_key_pressed(ui, CUI_KEY_UP)) move = -2;
        if (in_grid && cui_key_pressed(ui, CUI_KEY_HOME)) { st->cur = 0; st->cur_visible = true; move = 0; }
        if (in_grid && cui_key_pressed(ui, CUI_KEY_END)) { st->cur = st->nitems - 1; st->cur_visible = true; move = 0; }
        if (move) {
            if (st->cur < 0) st->cur = 0;
            else if (move == 1 || move == -1) st->cur = CUI_CLAMP(st->cur + move, 0, st->nitems - 1);
            else {
                float x, y;
                int si = cui__em_item_pos(&g, st->cur, &x, &y);
                if (si >= 0) {
                    const cui__em_section *s = &g.sec[si];
                    int k = st->cur - s->start, col = k % g.cols, row = k / g.cols, rows = cui__em_rows(&g, s->count);
                    if (move > 0) {
                        if (row + 1 < rows) st->cur = s->start + CUI_MIN(s->count - 1, (row + 1) * g.cols + col);
                        else if (si + 1 < g.nsec) st->cur = g.sec[si + 1].start + CUI_MIN(col, g.sec[si + 1].count - 1);
                    } else {
                        if (row > 0) st->cur = s->start + (row - 1) * g.cols + col;
                        else if (si > 0) { const cui__em_section *p = &g.sec[si - 1]; int pr_ = cui__em_rows(&g, p->count) - 1; st->cur = p->start + CUI_MIN(p->count - 1, pr_ * g.cols + col); }
                        else if (in_grid) { ui->focus = sid; st->cur = -1; }   /* up from the first row: back to the search */
                    }
                }
            }
            if (in_search && move == 2 && !st->query[0] && st->cur >= 0) ui->focus = gid;
            st->cur_visible = true;
            ui->kpressed[CUI_KEY_UP] = ui->kpressed[CUI_KEY_DOWN] = 0;
        }
        if (cui_key_pressed(ui, CUI_KEY_ENTER) || (in_grid && cui_key_pressed(ui, CUI_KEY_SPACE))) {
            pick_item = st->cur >= 0 ? st->cur : 0;
            ui->kpressed[CUI_KEY_ENTER] = 0;
        }
    }
    if (st->tone_open && cui_key_pressed(ui, CUI_KEY_ESCAPE)) { st->tone_open = false; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
    /* keep the cursor in view */
    if (st->cur_visible && st->cur >= 0 && st->cur < st->nitems) {
        float x, y;
        float vh = pr.h > 0 ? pr.h : view_h;
        if (cui__em_item_pos(&g, st->cur, &x, &y) >= 0) {
            if (y < st->target + g.header) st->target = CUI_MAX(0.0f, y - g.header);
            else if (y + g.cell > st->target + vh) st->target = y + g.cell - vh;
            st->animating = true;
        }
        st->cur_visible = false;
    }
    if (pick_item >= 0 && pick_item < st->nitems && can) {
        cui__em_str t = cui__em_item_text(e, st, recent, st->items[pick_item], *tone);
        picked = cui__frame_strdup(ui, t.s, t.len);
        cui__em_push_recent(rbuf, rcap, t.s);   /* shows next time the list is built without a search */
        st->cur = pick_item;
    }
    /* glide to a target */
    st->target = CUI_CLAMP(st->target, 0, maxtop);
    if (st->animating) {
        cui__tween(ui, &st->top, st->target, 0.12f);
        if (fabsf(st->top - st->target) < 0.5f) { st->top = st->target; st->animating = false; }
        else ui->animating = true;
    }
    st->top = CUI_CLAMP(st->top, 0, maxtop);

    /* ---- build ---- */
    int col = cui__node_new(ui, id);
    cui_node *cn = &ui->nodes[col];
    cn->w = o->width.kind ? o->width : CUI_FILL;
    if (!o->width.kind) cn->max_w = 360;
    cn->gap = 0; cn->self_align = CUI_ALIGN_STRETCH;
    cn->bg = CUI_C(ui, SURFACE); cn->border = 1; cn->border_color = CUI_C(ui, BORDER);
    cn->radius = CUI_CORNERS(ui->theme.radius_panel);
    cn->flags |= CUI_NF_CLIP;
    cui__open(ui, col);
    /* search and tone */
    if (!o->no_search || !o->no_tone) {
        int row = cui__node_new(ui, 0);
        cui_node *rn = &ui->nodes[row];
        rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER; rn->w = CUI_FILL; rn->gap = 6;
        rn->pad[0] = rn->pad[1] = rn->pad[3] = 10; rn->pad[2] = 6;
        cui__open(ui, row);
        if (st->tone_open) {
            static const char *const hands[6] = { "\xe2\x9c\x8b", "\xe2\x9c\x8b\xf0\x9f\x8f\xbb", "\xe2\x9c\x8b\xf0\x9f\x8f\xbc", "\xe2\x9c\x8b\xf0\x9f\x8f\xbd", "\xe2\x9c\x8b\xf0\x9f\x8f\xbe", "\xe2\x9c\x8b\xf0\x9f\x8f\xbf" };
            for (int k = 0; k < 6; k++) {
                cui_push_id_int(ui, k);
                cui_id tid = cui_make_id(ui, "#tone");
                cui_interact ti = cui__interact(ui, tid, CUI_IF_FOCUSABLE);
                if (ti.clicked || ti.keyboard_activate) { *tone = k; st->tone_open = false; st->built = false; }
                int b = cui__leaf(ui, tid, CUI_PX(36), CUI_PX(36));
                cui__mark_interactive(ui, b, CUI_IF_FOCUSABLE);
                ui->nodes[b].radius = CUI_CORNERS(8);
                if (k == *tone) ui->nodes[b].bg = CUI_C(ui, HOVER);
                else if (ti.hovered) ui->nodes[b].bg = cui_alpha(CUI_C(ui, HOVER), 0.6f);
                if (ti.focus_visible) cui__focus_ring(ui, b, 1);
                cui__em_tab_paint *tp = cui__frame_alloc(ui, sizeof *tp);
                memset(tp, 0, sizeof *tp);
                tp->s = hands[k]; tp->len = (int)strlen(hands[k]); tp->alpha = 1; tp->icon = false;
                ui->nodes[b].paint = cui__paint_emoji_tab; ui->nodes[b].paint_data = tp;
                cui_pop_id(ui);
            }
        } else {
            if (!o->no_search) {
                cui_input_opts io;
                memset(&io, 0, sizeof io);
                io.width = CUI_FILL; io.clearable = true;
                io.placeholder = o->placeholder ? o->placeholder : "Search emoji";
                cui__text_field(ui, sid, st->query, sizeof st->query, &io, false, 0 /* boxed */, CUI_ICON_SEARCH);
            } else { int sp = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1)); (void)sp; }
            if (!o->no_tone) {
                cui_id tid = cui_make_id(ui, "#tones");
                cui_interact ti = cui__interact(ui, tid, CUI_IF_FOCUSABLE);
                if (ti.clicked || ti.keyboard_activate) st->tone_open = true;
                int b = cui__leaf(ui, tid, CUI_PX(36), CUI_PX(36));
                cui__mark_interactive(ui, b, CUI_IF_FOCUSABLE);
                ui->nodes[b].radius = CUI_CORNERS(8);
                if (ti.hovered) ui->nodes[b].bg = CUI_C(ui, HOVER);
                if (ti.focus_visible) cui__focus_ring(ui, b, 1);
                cui__em_tab_paint *tp = cui__frame_alloc(ui, sizeof *tp);
                memset(tp, 0, sizeof *tp);
                static const char *const hands[6] = { "\xe2\x9c\x8b", "\xe2\x9c\x8b\xf0\x9f\x8f\xbb", "\xe2\x9c\x8b\xf0\x9f\x8f\xbc", "\xe2\x9c\x8b\xf0\x9f\x8f\xbd", "\xe2\x9c\x8b\xf0\x9f\x8f\xbe", "\xe2\x9c\x8b\xf0\x9f\x8f\xbf" };
                tp->s = hands[*tone]; tp->len = (int)strlen(hands[*tone]); tp->alpha = 1; tp->icon = false;
                ui->nodes[b].paint = cui__paint_emoji_tab; ui->nodes[b].paint_data = tp;
            }
        }
        cui__close(ui);
    }
    /* group tabs */
    int cur_sec = 0;
    {
        float acc = 0;
        for (int i = 0; i < g.nsec; i++) { float h = cui__em_sec_h(&g, &g.sec[i]); if (st->top + 1 >= acc) cur_sec = i; acc += h; }
    }
    if (can) {
        int row = cui__node_new(ui, 0);
        cui_node *rn = &ui->nodes[row];
        rn->dir = CUI_ROW; rn->w = CUI_FILL; rn->justify = CUI_JUSTIFY_BETWEEN; rn->pad[1] = rn->pad[3] = 8;
        cui__open(ui, row);
        int active_kind = st->query[0] || cur_sec >= g.nsec ? -1 : g.sec[cur_sec].kind;
        for (int kind = nrecent > 0 ? 0 : 1; kind <= CUI__EM_GROUPS; kind++) {
            cui_push_id_int(ui, kind);
            cui_id tid = cui_make_id(ui, "#tab");
            cui_interact ti = cui__interact(ui, tid, CUI_IF_FOCUSABLE);
            if (ti.clicked || ti.keyboard_activate) {
                if (st->query[0]) { st->query[0] = 0; st->jump = kind + 1; }   /* out of the search, then there */
                else for (int i = 0; i < g.nsec; i++) if (g.sec[i].kind == kind) { st->target = cui__em_sec_top(&g, i); st->animating = true; }
            }
            int b = cui__leaf(ui, tid, CUI_FILL, CUI_PX(34));   /* the tabs share the width, so a phone fits all ten */
            ui->nodes[b].max_w = 40;
            cui__mark_interactive(ui, b, CUI_IF_FOCUSABLE);
            ui->nodes[b].radius = CUI_CORNERS(8);
            if (ti.hovered) ui->nodes[b].bg = CUI_C(ui, HOVER);
            if (ti.focus_visible) cui__focus_ring(ui, b, 1);
            cui__em_tab_paint *tp = cui__frame_alloc(ui, sizeof *tp);
            memset(tp, 0, sizeof *tp);
            tp->icon = kind == 0;
            tp->s = kind == 0 ? CUI_ICON_CLOCK : cui__em_tab_icons[kind];
            tp->len = (int)strlen(tp->s);
            tp->alpha = kind == active_kind || ti.hovered ? 1 : 0.5f;
            tp->active = kind == active_kind;
            tp->bar = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
            ui->nodes[b].paint = cui__paint_emoji_tab; ui->nodes[b].paint_data = tp;
            cui_tooltip(ui, names[kind]);
            cui_pop_id(ui);
        }
        cui__close(ui);
    }
    {
        int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
        ui->nodes[line].bg = CUI_C(ui, BORDER_SUBTLE);
    }
    /* the grid */
    int gn = cui__leaf(ui, gid, CUI_FILL, CUI_PX(view_h));
    cui__mark_interactive(ui, gn, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui__em_paint *pp = cui__frame_alloc(ui, sizeof *pp);
    memset(pp, 0, sizeof *pp);
    pp->e = e; pp->st = st; pp->g = g; pp->items = st->items;
    pp->recent = cui__frame_alloc(ui, sizeof recent);
    memcpy(pp->recent, recent, sizeof recent);
    pp->hover = hover; pp->cur = st->cur; pp->focus = in_grid && ui->focus_visible; pp->soft = in_search && st->query[0]; pp->top = st->top; pp->tone = *tone;
    pp->names = names; pp->empty = o->empty_text ? o->empty_text : "No emoji found"; pp->none = !can; pp->wheel = wid;
    pp->hover_bg = CUI_C(ui, HOVER); pp->header = CUI_C(ui, TEXT_MUTED); pp->ring = CUI_C(ui, FOCUS_RING);
    ui->nodes[gn].paint = cui__paint_emoji_grid; ui->nodes[gn].paint_data = pp;
    /* the preview: the emoji under the pointer, or the cursor's */
    if (!o->no_preview) {
        int line = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
        ui->nodes[line].bg = CUI_C(ui, BORDER_SUBTLE);
        int row = cui__node_new(ui, 0);
        cui_node *rn = &ui->nodes[row];
        rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER; rn->w = CUI_FILL; rn->h = CUI_PX(48); rn->gap = 10; rn->pad[1] = rn->pad[3] = 12;
        cui__open(ui, row);
        int show = hover >= 0 ? hover : st->cur;
        if (can && show >= 0 && show < st->nitems) {
            cui__em_str t = cui__em_item_text(e, st, recent, st->items[show], *tone);
            cui_emoji(ui, t.s, 28);
            const char *name = "";
            int nl = 0;
            int v = st->items[show];
            if (v >= 0) { name = e->data + e->recs[v].name; nl = e->recs[v].name_len; }
            else {   /* a recent one: find its name, without the tone */
                for (int i = 0; i < e->nrec && !nl; i++) {
                    const cui__em_rec *rec = &e->recs[i];
                    char tb[64];
                    int tl = rec->len;
                    if (tl == t.len && !memcmp(e->data + rec->text, t.s, (size_t)tl)) { name = e->data + rec->name; nl = rec->name_len; }
                    else if (rec->tone) for (int k = 1; k <= 5 && !nl; k++) { tl = cui__em_toned(e->data + rec->text, rec->len, k, tb, sizeof tb); if (tl == t.len && !memcmp(tb, t.s, (size_t)tl)) { name = e->data + rec->name; nl = rec->name_len; } }
                }
            }
            char cap[256];
            int cl = CUI_MIN(nl, 255);
            memcpy(cap, name, (size_t)cl); cap[cl] = 0;
            if (cl && cap[0] >= 'a' && cap[0] <= 'z') cap[0] = (char)(cap[0] - 32);
            int tn = cui__leaf(ui, 0, CUI_FILL, CUI_PX(20));
            cui__em_name_paint *np = cui__frame_alloc(ui, sizeof *np);
            memcpy(np->s, cap, (size_t)cl + 1);
            ui->nodes[tn].paint = cui__paint_emoji_name; ui->nodes[tn].paint_data = np;
        } else if (can) {
            int tn = cui__text_node(ui, o->hint ? o->hint : "Pick an emoji", -1, CUI_FONT_BODY, cui__fsz(ui, 13), CUI_C(ui, TEXT_MUTED), false);
            (void)tn;
        }
        cui__close(ui);
    }
    cui__close(ui);
    cui_pop_id(ui);
    ui->last_id = id;
    return picked;
}
