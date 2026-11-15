/* Fonts and text: TrueType rasterization with stb_truetype, a glyph atlas
 * with subpixel positioning, kerning, font fallback (icons live in their own
 * font and work inside any string), measuring and word wrap. */
#include "cui_internal.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#include "../third_party/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef CUI_TEXT_SHAPING
#include "cui_shape.h"
#include "cui_bidi.h"
typedef struct { int font, id, start; float x, y; } cui_shaped_glyph;
typedef struct cui_shaped_line {
    struct cui_shaped_line *next;
    int start, end, font, count;
    float size, width;
    bool tabular;
    cui_shaped_glyph *glyphs;
} cui_shaped_line;
typedef struct cui_shaped_paragraph {
    struct cui_shaped_paragraph *next;
    char *text;
    int len, base, requested_base, count, line_count;
    int *offsets;
    cui_bidi_char *chars;
    cui_shaped_line *lines;
    cui_text_layout_line *wrap_lines;
    int wrap_count, wrap_font;
    float wrap_size, wrap_width;
} cui_shaped_paragraph;
static void cui__shape_lines_free(cui_ctx *ui, cui_shaped_paragraph *p) {
    while (p->lines) {
        cui_shaped_line *l = p->lines; p->lines = l->next;
        cui__free(ui, l->glyphs); cui__free(ui, l);
    }
    p->line_count = 0;
}
static void cui__shape_cache_clear(cui_ctx *ui) {
    while (ui->shape_cache) {
        cui_shaped_paragraph *p = ui->shape_cache; ui->shape_cache = p->next;
        cui__shape_lines_free(ui, p);
        cui__free(ui, p->wrap_lines);
        cui__free(ui, p->text); cui__free(ui, p->offsets); cui__free(ui, p->chars); cui__free(ui, p);
    }
    ui->shape_cache_count = 0;
}
static cui_shaped_paragraph *cui__paragraph(cui_ctx *ui, const char *text, int len, int base) {
    cui_shaped_paragraph **link = &ui->shape_cache;
    while (*link) {
        cui_shaped_paragraph *p = *link;
        if (p->len == len && p->requested_base == base && !memcmp(p->text, text, (size_t)len)) {
            *link = p->next; p->next = ui->shape_cache; ui->shape_cache = p; return p;
        }
        link = &p->next;
    }
    /* Bound retained text; evict the least recently used paragraph. */
    if (ui->shape_cache_count == 64) {
        link = &ui->shape_cache;
        while ((*link)->next) link = &(*link)->next;
        cui_shaped_paragraph *p = *link; *link = NULL;
        cui__free(ui, p->wrap_lines);
        cui__shape_lines_free(ui, p); cui__free(ui, p->text); cui__free(ui, p->offsets);
        cui__free(ui, p->chars); cui__free(ui, p); ui->shape_cache_count--;
    }
    cui_shaped_paragraph *p = cui__alloc(ui, sizeof *p);
    if (!p) return NULL;
    memset(p, 0, sizeof *p);
    p->text = cui__alloc(ui, (size_t)len + 1);
    p->offsets = cui__alloc(ui, ((size_t)len + 1) * sizeof *p->offsets);
    p->chars = cui__alloc(ui, ((size_t)len + 1) * sizeof *p->chars);
    if (!p->text || !p->offsets || !p->chars) {
        cui__free(ui, p->text); cui__free(ui, p->offsets); cui__free(ui, p->chars); cui__free(ui, p); return NULL;
    }
    memcpy(p->text, text, (size_t)len); p->text[len] = 0;
    for (int at = 0; at < len;) {
        p->offsets[p->count] = at;
        at += cui__utf8_decode(text + at, len - at, &p->chars[p->count++].cp);
    }
    p->offsets[p->count] = len; p->len = len; p->requested_base = base;
    p->base = cui__bidi_paragraph(p->chars, p->count, base);
    p->next = ui->shape_cache; ui->shape_cache = p; ui->shape_cache_count++;
    return p;
}
#endif

#ifndef CUI_NO_DEFAULT_FONTS
#define CUI__EMBEDDED(n) extern const unsigned char n[]; extern const unsigned int n##_size, n##_packed;
CUI__EMBEDDED(cui_font_inter_regular)
CUI__EMBEDDED(cui_font_inter_medium)
CUI__EMBEDDED(cui_font_geist_medium)
CUI__EMBEDDED(cui_font_geist_mono)
CUI__EMBEDDED(cui_font_lucide)
#endif

struct cui_font_data {
    stbtt_fontinfo info;
    unsigned char *data;
    bool owned;
#ifdef CUI_TEXT_SHAPING
    size_t data_size;
    cui_shape_font shaped;
#endif
    float upem;
    float ascent, descent, line_gap;   /* em units (fractions of the font size) */
    float cap_height;
    bool is_icon;
    int fallbacks[4];
    int fallback_count;
    int gi_ascii[128];
    float adv_ascii[128];              /* em units */
    float tab_adv;                     /* widest digit, em units */
    float *kern_ascii;                 /* 95*95, em units, lazily built */
};

#define CUI_ATLAS_MIN 1024
#define CUI_ATLAS_MAX 4096

static cui_font_data *cui__fd(cui_ctx *ui, cui_font f) {
    if (f < 0 || f >= ui->font_count) return NULL;
    return &ui->fonts[f];
}

/* Public font ids: 0..4 are the roles (body, medium, display, mono, icons),
 * loaded fonts are 16 + index. Internally fonts are plain indices. */
#define CUI__USER_FONT_BASE 16
cui_font cui__resolve_font(cui_ctx *ui, cui_font f) {
    if (f >= 0 && f < 5) f = ui->roles[f];
    else if (f >= CUI__USER_FONT_BASE) f -= CUI__USER_FONT_BASE;
    else f = -1;
    if (f < 0 || f >= ui->font_count) f = ui->font_count > 0 ? 0 : -1;
    return f;
}

static void cui__font_prepare(cui_font_data *fd) {
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fd->info, &asc, &desc, &gap);
    int ta, td, tg;
    if (stbtt_GetFontVMetricsOS2(&fd->info, &ta, &td, &tg)) { asc = ta; desc = td; gap = tg; }
    float em = 1.0f / stbtt_ScaleForMappingEmToPixels(&fd->info, 1.0f);
    fd->upem = em;
    fd->ascent = asc / em;
    fd->descent = desc / em;
    fd->line_gap = gap / em;
    int x0, y0, x1, y1;
    int gH = stbtt_FindGlyphIndex(&fd->info, 'H');
    if (gH && stbtt_GetGlyphBox(&fd->info, gH, &x0, &y0, &x1, &y1)) fd->cap_height = y1 / em;
    else fd->cap_height = fd->ascent * 0.72f;
    fd->tab_adv = 0;
    for (int c = 0; c < 128; c++) {
        int gi = stbtt_FindGlyphIndex(&fd->info, c);
        fd->gi_ascii[c] = gi;
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&fd->info, gi, &adv, &lsb);
        fd->adv_ascii[c] = gi ? adv / em : 0;
        if (c >= '0' && c <= '9' && fd->adv_ascii[c] > fd->tab_adv) fd->tab_adv = fd->adv_ascii[c];
    }
}

cui_font cui_font_load_memory(cui_ctx *ui, const void *data, size_t size, bool copy) {
    if (!data || size < 12) return -1;
    unsigned char *bytes = (unsigned char *)data;
    if (copy) {
        bytes = cui__alloc(ui, size);
        memcpy(bytes, data, size);
    }
    stbtt_fontinfo info;
    int off = stbtt_GetFontOffsetForIndex(bytes, 0);
    if (off < 0 || !stbtt_InitFont(&info, bytes, off)) {
        if (copy) cui__free(ui, bytes);
        return -1;
    }
#ifdef CUI_TEXT_SHAPING
    cui__shape_cache_clear(ui);
#endif
    CUI__GROW(ui, ui->fonts, ui->font_count, ui->font_cap, 1);
    cui_font_data *fd = &ui->fonts[ui->font_count];
    memset(fd, 0, sizeof *fd);
    fd->info = info;
    fd->data = bytes;
#ifdef CUI_TEXT_SHAPING
    fd->data_size = size;
    cui__shape_font_init(&fd->shaped, bytes, size, off);
#endif
    fd->owned = copy;
    cui__font_prepare(fd);
    return CUI__USER_FONT_BASE + ui->font_count++;
}

cui_font cui_font_load_file(cui_ctx *ui, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return -1; }
    unsigned char *buf = cui__alloc(ui, (size_t)n);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { cui__free(ui, buf); return -1; }
    cui_font id = cui_font_load_memory(ui, buf, (size_t)n, false);
    if (id < 0) { cui__free(ui, buf); return -1; }
    ui->fonts[id - CUI__USER_FONT_BASE].owned = true;
    return id;
}

void cui_font_set_role(cui_ctx *ui, cui_font role, cui_font font) {
    if (role < 0 || role >= 5) return;
    int real = cui__resolve_font(ui, font);
    if (real < 0) return;
#ifdef CUI_TEXT_SHAPING
    if (ui->roles[role] != real) cui__shape_cache_clear(ui);
#endif
    ui->roles[role] = real;
    ui->fonts[real].is_icon = role == CUI_FONT_ICONS;
}

void cui_font_add_fallback(cui_ctx *ui, cui_font font, cui_font fallback) {
    cui_font_data *fd = cui__fd(ui, cui__resolve_font(ui, font));
    int fb = cui__resolve_font(ui, fallback);
    if (!fd || fd->fallback_count >= 4 || fb < 0) return;
#ifdef CUI_TEXT_SHAPING
    cui__shape_cache_clear(ui);
#endif
    fd->fallbacks[fd->fallback_count++] = fb;
}

void cui__fonts_init(cui_ctx *ui, bool defaults) {
    for (int i = 0; i < 4096; i++) ui->glyph_buckets[i] = -1;
    ui->atlas_w = ui->atlas_h = CUI_ATLAS_MIN;
    ui->atlas = cui__alloc(ui, (size_t)ui->atlas_w * (size_t)ui->atlas_h);
    memset(ui->atlas, 0, (size_t)ui->atlas_w * (size_t)ui->atlas_h);
    ui->pack_x = 1; ui->pack_y = 1; ui->pack_row_h = 0;
    for (int i = 0; i < 5; i++) ui->roles[i] = -1;
#ifndef CUI_NO_DEFAULT_FONTS
    if (defaults) {
        const unsigned char *data[5] = { cui_font_inter_regular, cui_font_inter_medium, cui_font_geist_medium,
                                         cui_font_geist_mono, cui_font_lucide };
        unsigned int size[5] = { cui_font_inter_regular_size, cui_font_inter_medium_size, cui_font_geist_medium_size,
                                 cui_font_geist_mono_size, cui_font_lucide_size };
        unsigned int packed[5] = { cui_font_inter_regular_packed, cui_font_inter_medium_packed, cui_font_geist_medium_packed,
                                   cui_font_geist_mono_packed, cui_font_lucide_packed };
        for (int i = 0; i < 5; i++) {
            /* the embedded fonts are compressed; unpack each into a buffer the font owns */
            unsigned char *buf = cui__alloc(ui, size[i]);
            cui_font f = -1;
            if (cui__inflate(data[i], packed[i], buf, size[i]) == size[i])
                f = cui_font_load_memory(ui, buf, size[i], false);
            if (f >= 0) ui->fonts[f - CUI__USER_FONT_BASE].owned = true;
            else cui__free(ui, buf);
            ui->roles[i] = f >= 0 ? f - CUI__USER_FONT_BASE : -1;
        }
        if (ui->roles[4] >= 0) ui->fonts[ui->roles[4]].is_icon = true;
        /* Geist and Geist Mono fall back to Inter for characters they lack */
        cui_font_add_fallback(ui, CUI_FONT_DISPLAY, CUI_FONT_MEDIUM);
        cui_font_add_fallback(ui, CUI_FONT_MONO, CUI_FONT_BODY);
    }
#else
    (void)defaults;
#endif
}

void cui__fonts_shutdown(cui_ctx *ui) {
#ifdef CUI_TEXT_SHAPING
    cui__shape_cache_clear(ui);
#endif
    for (int i = 0; i < ui->font_count; i++) {
        if (ui->fonts[i].owned) cui__free(ui, ui->fonts[i].data);
        cui__free(ui, ui->fonts[i].kern_ascii);
    }
    cui__free(ui, ui->fonts);
    cui__free(ui, ui->glyphs);
    cui__free(ui, ui->atlas);
    if (ui->atlas_tex && ui->renderer.texture_destroy) ui->renderer.texture_destroy(ui->renderer.user, ui->atlas_tex);
    ui->atlas_tex = 0;
}

/* ---- outlines for color layer emoji fonts (cui_emoji.c) ---- */

struct cui__tt { stbtt_fontinfo info; };
cui__tt *cui__tt_open(cui_ctx *ui, const unsigned char *data, int offset) {
    cui__tt *t = cui__alloc(ui, sizeof *t);
    if (!t) return NULL;
    if (!stbtt_InitFont(&t->info, data, offset)) { cui__free(ui, t); return NULL; }
    return t;
}
float cui__tt_scale(cui__tt *t, float px_per_em) { return stbtt_ScaleForMappingEmToPixels(&t->info, px_per_em); }
void cui__tt_vmetrics(cui__tt *t, int *ascent, int *descent) { int gap; stbtt_GetFontVMetrics(&t->info, ascent, descent, &gap); }
bool cui__tt_box(cui__tt *t, int gid, float scale, int *x0, int *y0, int *x1, int *y1) {
    stbtt_GetGlyphBitmapBox(&t->info, gid, scale, scale, x0, y0, x1, y1);
    return *x1 > *x0 && *y1 > *y0;
}
void cui__tt_render(cui__tt *t, int gid, float scale, unsigned char *out, int w, int h, int stride) {
    stbtt_MakeGlyphBitmap(&t->info, out, w, h, stride, scale, scale, gid);
}

/* ---- glyph lookup with fallback ---- */

static int cui__glyph_index(cui_font_data *fd, uint32_t cp) {
    if (cp < 128) return fd->gi_ascii[cp];
    return stbtt_FindGlyphIndex(&fd->info, (int)cp);
}

static void cui__find_glyph(cui_ctx *ui, cui_font font, uint32_t cp, cui_font *out_font, int *out_gi) {
    cui_font_data *fd = cui__fd(ui, font);
    if (!fd) { *out_font = -1; *out_gi = 0; return; }
    int gi = cui__glyph_index(fd, cp);
    if (gi || cp == ' ') { *out_font = font; *out_gi = gi; return; }
    for (int i = 0; i < fd->fallback_count; i++) {
        cui_font_data *f2 = cui__fd(ui, fd->fallbacks[i]);
        int g2 = f2 ? cui__glyph_index(f2, cp) : 0;
        if (g2) { *out_font = fd->fallbacks[i]; *out_gi = g2; return; }
    }
    cui_font icons = ui->roles[CUI_FONT_ICONS];
    if (icons >= 0 && icons != font && cp >= 0xE000 && cp <= 0xF8FF) {
        int g2 = cui__glyph_index(&ui->fonts[icons], cp);
        if (g2) { *out_font = icons; *out_gi = g2; return; }
    }
    for (int i = 0; i < ui->font_count; i++) {
        if (i == font || ui->fonts[i].is_icon) continue;
        int g2 = cui__glyph_index(&ui->fonts[i], cp);
        if (g2) { *out_font = i; *out_gi = g2; return; }
    }
    *out_font = font;
    *out_gi = 0;
}

/* Effective em size of a glyph from font gf when the text asked for font f. */
static float cui__glyph_size(cui_ctx *ui, cui_font f, cui_font gf, float size) {
    if (gf != f && ui->fonts[gf].is_icon) return size * 1.15f;
    return size;
}

static float cui__advance_em(cui_font_data *fd, int gi, uint32_t cp) {
    if (cp < 128 && fd->gi_ascii[cp] == gi) return fd->adv_ascii[cp];
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&fd->info, gi, &adv, &lsb);
    return adv / fd->upem;
}

static float cui__kern_em(cui_ctx *ui, cui_font_data *fd, uint32_t a, uint32_t b, int ga, int gb) {
    if (fd->is_icon) return 0;
    if (a >= 32 && a < 127 && b >= 32 && b < 127) {
        if (!fd->kern_ascii) {
            fd->kern_ascii = cui__alloc(ui, sizeof(float) * 95 * 95);
            for (int i = 0; i < 95; i++)
                for (int j = 0; j < 95; j++) {
                    int g1 = fd->gi_ascii[i + 32], g2 = fd->gi_ascii[j + 32];
                    fd->kern_ascii[i * 95 + j] = (g1 && g2) ? stbtt_GetGlyphKernAdvance(&fd->info, g1, g2) / fd->upem : 0;
                }
        }
        return fd->kern_ascii[(a - 32) * 95 + (b - 32)];
    }
    return stbtt_GetGlyphKernAdvance(&fd->info, ga, gb) / fd->upem;
}

/* Iterates glyphs with their pen positions (points). */
typedef struct {
    cui_ctx *ui;
    cui_font font;
    float size;
    bool tabular;
    const char *s;
    int len, pos;
    float x;
    uint32_t prev_cp;
    int prev_gi;
    cui_font prev_font;
    /* current glyph */
    uint32_t cp;
    int gi;
    cui_font gfont;
    float gx;        /* pen x of this glyph (points) */
    float gsize;     /* em size used to rasterize */
    float adv;       /* advance (points) */
    float center_off;/* tabular centering offset */
    int start, end;  /* byte range */
    float track;     /* extra advance per glyph (points) */
} cui__glyph_iter;

static void cui__iter_init(cui__glyph_iter *it, cui_ctx *ui, cui_font font, float size, const char *s, int len, bool tab) {
    memset(it, 0, sizeof *it);
    it->ui = ui; it->font = font; it->size = size; it->s = s; it->len = len; it->tabular = tab;
    it->prev_font = -1;
    it->track = size * (-0.0223f + 0.185f * expf(-0.1745f * size));
}

static bool cui__iter_next(cui__glyph_iter *it) {
    if (it->pos >= it->len) return false;
    cui_ctx *ui = it->ui;
    uint32_t cp;
    int n = cui__utf8_decode(it->s + it->pos, it->len - it->pos, &cp);
    it->start = it->pos;
    it->end = it->pos + n;
    it->pos += n;
    cui_font gf; int gi;
    cui__find_glyph(ui, it->font, cp, &gf, &gi);
    if (gf < 0) return false;
    cui_font_data *fd = &ui->fonts[gf];
    float gs = cui__glyph_size(ui, it->font, gf, it->size);
    /* tabular figures sit in fixed cells: no kerning against a digit, or "3:18" and "3:42" would differ in width */
    bool tab_pair = it->tabular && ((cp >= '0' && cp <= '9') || (it->prev_cp >= '0' && it->prev_cp <= '9'));
    if (it->prev_font == gf && it->prev_gi && gi && !tab_pair) it->x += cui__kern_em(ui, fd, it->prev_cp, cp, it->prev_gi, gi) * gs;
    float adv = cui__advance_em(fd, gi, cp) * gs;
    it->center_off = 0;
    if (it->tabular && cp >= '0' && cp <= '9' && fd->tab_adv > 0) {
        float ta = fd->tab_adv * gs;
        it->center_off = (ta - adv) * 0.5f;
        adv = ta;
    }
    /* optical tracking: a touch looser when small, tighter as text grows */
    if (!fd->is_icon) { adv += it->track; it->center_off += it->track * 0.5f; }
    it->cp = cp; it->gi = gi; it->gfont = gf; it->gsize = gs;
    it->gx = it->x;
    it->adv = adv;
    it->x += adv;
    it->prev_cp = cp; it->prev_gi = gi; it->prev_font = gf;
    return true;
}

#ifdef CUI_TEXT_SHAPING
bool cui__text_needs_shaping(cui_ctx *ui, const char *text, int len) {
    if (ui->text_unshaped) return false;
    if (ui->text_paragraph) return true;
    for (int p = 0; p < len;) {
        uint32_t cp;
        p += cui__utf8_decode(text + p, len - p, &cp);
        if ((cp >= 0x590 && cp <= 0x8ff) || (cp >= 0xfb1d && cp <= 0xfdff) ||
            (cp >= 0xfe70 && cp <= 0xfeff) || (cp >= 0x200c && cp <= 0x200f) ||
            (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069)) return true;
    }
    return false;
}

static float cui__shape_run(cui_ctx *ui, cui_font font, float size, const char *text, int len,
                             bool tabular, bool draw, float x, float y, cui_color color, float max_x);
#endif

static float cui__text_width_raw(cui_ctx *ui, cui_font font, float size, const char *text, int len, bool tabular) {
    if (font < 0 || !text) return 0;
    if (len < 0) len = (int)strlen(text);
#ifdef CUI_TEXT_SHAPING
    if (cui__text_needs_shaping(ui, text, len)) {
        float maxw = 0;
        for (int start = 0; start <= len;) {
            int end = start;
            while (end < len && text[end] != '\n') end++;
            float w = cui__shape_run(ui, font, size, text + start, end - start, tabular, false, 0, 0, (cui_color){0}, 1e30f);
            maxw = CUI_MAX(maxw, w);
            start = end + 1;
        }
        return maxw;
    }
#endif
    cui__glyph_iter it;
    cui__iter_init(&it, ui, font, size, text, len, tabular);
    float maxw = 0;
    while (cui__iter_next(&it)) {
        if (it.cp == '\n') { maxw = CUI_MAX(maxw, it.gx); it.x = 0; it.prev_gi = 0; }
    }
    return CUI_MAX(maxw, it.x);
}

float cui__text_width(cui_ctx *ui, cui_font font, float size, const char *text, int len, bool tabular) {
    return cui__text_width_raw(ui, cui__resolve_font(ui, font), size, text, len, tabular);
}

float cui__font_line_height(cui_ctx *ui, cui_font font, float size) {
    font = cui__resolve_font(ui, font);
    cui_font_data *fd = cui__fd(ui, font);
    if (!fd) return size;
    if (fd->is_icon) return size;
    return (fd->ascent - fd->descent) * size;
}

float cui__font_ascent(cui_ctx *ui, cui_font font, float size) {
    font = cui__resolve_font(ui, font);
    cui_font_data *fd = cui__fd(ui, font);
    return fd ? fd->ascent * size : size;
}

cui_text_metrics cui_measure_text(cui_ctx *ui, cui_font font, float size, const char *text, int len) {
    cui_text_metrics m;
    font = cui__resolve_font(ui, font);
    cui_font_data *fd = cui__fd(ui, font);
    if (len < 0 && text) len = (int)strlen(text);
    m.width = cui__text_width_raw(ui, font, size, text, len, false);
    m.ascent = fd ? fd->ascent * size : size;
    m.descent = fd ? fd->descent * size : 0;
    m.line_height = size * 1.45f;
    int lines = 1;
    for (int i = 0; text && i < len; i++) if (text[i] == '\n') lines++;
    m.height = m.line_height * (float)lines;
    return m;
}

int cui__text_hit(cui_ctx *ui, cui_font font, float size, const char *text, int len, float x) {
    font = cui__resolve_font(ui, font);
    if (font < 0 || x <= 0) return 0;
    cui__glyph_iter it;
    cui__iter_init(&it, ui, font, size, text, len, false);
    while (cui__iter_next(&it)) {
        if (x < it.gx + it.adv * 0.5f) return it.start;
    }
    return len;
}

/* ---- word wrap ---- */

static bool cui__is_space(uint32_t c) { return c == ' ' || c == '\t'; }

static int cui__text_wrap_greedy(cui_ctx *ui, cui_font font, float size, const char *text, int len, float width,
                                 cui_text_layout_line *lines, int max_lines) {
    font = cui__resolve_font(ui, font);
    if (font < 0) return 0;
    int count = 0;
    int line_start = 0;
    float line_w = 0;
    int last_break = -1;        /* byte index of the last space in line */
    float width_at_break = 0;
    cui__glyph_iter it;
    cui__iter_init(&it, ui, font, size, text, len, false);
    float base_x = 0;
#define CUI__EMIT(s, e, w) do { if (count < max_lines && lines) { lines[count].start = (s); lines[count].end = (e); lines[count].width = (w); } count++; } while (0)
    while (cui__iter_next(&it)) {
        if (it.cp == '\n') {
            CUI__EMIT(line_start, it.start, it.gx - base_x);
            line_start = it.end; base_x = it.x; last_break = -1; it.prev_gi = 0;
            continue;
        }
        float right = it.gx + it.adv - base_x;
        if (cui__is_space(it.cp)) {
            last_break = it.start;
            width_at_break = it.gx - base_x;
            continue;
        }
        if (right > width + 0.01f && it.start > line_start) {
            if (last_break > line_start) {
                CUI__EMIT(line_start, last_break, width_at_break);
                /* restart after the space(s) */
                int p = last_break;
                while (p < len && (text[p] == ' ' || text[p] == '\t')) p++;
                line_start = p;
                /* recompute base: measure from line_start to current glyph start */
                base_x = it.gx - cui__text_width_raw(ui, font, size, text + line_start, it.start - line_start, false);
            } else {
                /* break inside a long word */
                CUI__EMIT(line_start, it.start, it.gx - base_x);
                line_start = it.start;
                base_x = it.gx;
            }
            last_break = -1;
        }
        line_w = it.x - base_x;
    }
    (void)line_w;
    /* last line, trimmed of trailing spaces */
    int e = len;
    while (e > line_start && (text[e - 1] == ' ' || text[e - 1] == '\t')) e--;
    CUI__EMIT(line_start, e, cui__text_width_raw(ui, font, size, text + line_start, e - line_start, false));
#undef CUI__EMIT
    return count;
}

static bool cui__span_has_space(const char *text, int s, int e) {
    for (int i = s; i < e; i++) if (text[i] == ' ' || text[i] == '\t') return true;
    return false;
}
static int cui__span_spaces(const char *text, int s, int e) {
    int n = 0;
    for (int i = s; i < e; i++) n += text[i] == ' ';
    return n;
}

/* Greedy wrapping, then a "pretty" pass: when the last line would be a single
 * word under a line of three or more, wrap a little narrower so a second word
 * comes down with it. The line count never changes, so neither does the height. */
int cui__text_wrap(cui_ctx *ui, cui_font font, float size, const char *text, int len, float width,
                   cui_text_layout_line *lines, int max_lines) {
#ifdef CUI_TEXT_SHAPING
    if (cui__text_needs_shaping(ui, text, len)) {
        font = cui__resolve_font(ui, font);
        if (font < 0) return 0;
        int requested = -1;
        cui_shaped_paragraph *p = cui__paragraph(ui, text, len, requested);
        if (p && p->wrap_lines && p->wrap_font == font && p->wrap_size == size && p->wrap_width == width) {
            if (lines && max_lines > 0)
                memcpy(lines, p->wrap_lines, (size_t)CUI_MIN(max_lines, p->wrap_count) * sizeof *lines);
            return p->wrap_count;
        }
        const char *saved = ui->text_paragraph;
        int saved_len = ui->text_paragraph_len;
        ui->text_paragraph = text; ui->text_paragraph_len = len;
        int n = 0, start = 0;
        for (;;) {
            int end = start, best = start, next = start;
            float best_width = 0;
            while (end < len && text[end] != '\n') {
                while (end < len && text[end] != ' ' && text[end] != '\t' && text[end] != '\n') end++;
                float w = cui__shape_run(ui, font, size, text + start, end - start, false, false, 0, 0, (cui_color){0}, 1e30f);
                if (w > width + 0.01f && best > start) break;
                best = end; best_width = w;
                next = end;
                while (next < len && (text[next] == ' ' || text[next] == '\t')) next++;
                if (next == len || text[next] == '\n') break;
                end = next;
            }
            if (lines && n < max_lines) lines[n] = (cui_text_layout_line){start, best, best_width};
            n++;
            if (next == len) break;
            start = next;
            if (text[start] == '\n') start++;
        }
        ui->text_paragraph = saved; ui->text_paragraph_len = saved_len;
        /* Retain completed breaks, so next frame never remeasures wrap candidates. */
        if (lines && n <= max_lines) {
            p = cui__paragraph(ui, text, len, requested);
            cui_text_layout_line *copy = p ? cui__alloc(ui, (size_t)n * sizeof *copy) : NULL;
            if (copy) {
                memcpy(copy, lines, (size_t)n * sizeof *copy);
                cui__free(ui, p->wrap_lines); p->wrap_lines = copy;
                p->wrap_count = n; p->wrap_font = font; p->wrap_size = size; p->wrap_width = width;
            }
        }
        return n;
    }
#endif
    enum { CAP = 96 };
    cui_text_layout_line tmp[CAP];
    int n = cui__text_wrap_greedy(ui, font, size, text, len, width, tmp, CAP);
    bool orphan = false;
    if (n >= 2 && n <= CAP) {
        const cui_text_layout_line *a = &tmp[n - 2], *b = &tmp[n - 1];
        bool same_paragraph = true;
        for (int i = a->end; i < b->start; i++) if (text[i] == '\n') { same_paragraph = false; break; }
        orphan = same_paragraph && b->end > b->start && !cui__span_has_space(text, b->start, b->end) && cui__span_spaces(text, a->start, a->end) >= 2;
    }
    /* headings balance their lines: the narrowest width that keeps the same
     * number of lines, so a title breaks into even halves, not a full line
     * and a stub */
    if (size >= 20 && n >= 2 && n <= 4) {
        bool para = true;
        for (int i = 0; i < len; i++) if (text[i] == '\n') { para = false; break; }
        if (para) {
            cui_text_layout_line alt[CAP];
            float lo = width * 0.5f, hi = width, best = width;
            for (int k = 0; k < 9; k++) {
                float mid = (lo + hi) * 0.5f;
                if (cui__text_wrap_greedy(ui, font, size, text, len, mid, alt, CAP) == n) { best = mid; hi = mid; }
                else lo = mid;
            }
            if (best < width) {
                int m = cui__text_wrap_greedy(ui, font, size, text, len, best + 0.5f, alt, CAP);
                if (m == n) {
                    if (lines) for (int i = 0; i < m && i < max_lines; i++) lines[i] = alt[i];
                    return m;
                }
            }
        }
    }
    if (orphan) {
        cui_text_layout_line alt[CAP];
        for (int k = 1; k <= 10; k++) {
            float w = width * (1.0f - 0.03f * (float)k);
            int m = cui__text_wrap_greedy(ui, font, size, text, len, w, alt, CAP);
            if (m != n) break;   /* would add a line: keep the original */
            if (cui__span_has_space(text, alt[m - 1].start, alt[m - 1].end)) {
                if (lines) for (int i = 0; i < m && i < max_lines; i++) lines[i] = alt[i];
                return m;
            }
        }
    }
    if (lines) for (int i = 0; i < n && i < max_lines && i < CAP; i++) lines[i] = tmp[i];
    if (n > CAP && lines) return cui__text_wrap_greedy(ui, font, size, text, len, width, lines, max_lines);
    return n;
}

float cui__text_min_width(cui_ctx *ui, cui_font font, float size, const char *text, int len) {
    /* widest word */
    font = cui__resolve_font(ui, font);
    float best = 0;
    int ws = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == ' ' || text[i] == '\n' || text[i] == '\t') {
            if (i > ws) {
                float w = cui__text_width_raw(ui, font, size, text + ws, i - ws, false);
                if (w > best) best = w;
            }
            ws = i + 1;
        }
    }
    return best;
}

/* ---- atlas ---- */

static uint8_t cui__gamma_lut[256];
static bool cui__gamma_ready;

static void cui__atlas_grow(cui_ctx *ui) {
    int nw = ui->atlas_w * 2, nh = ui->atlas_h * 2;
    uint8_t *na = cui__alloc(ui, (size_t)nw * (size_t)nh);
    memset(na, 0, (size_t)nw * (size_t)nh);
    for (int y = 0; y < ui->atlas_h; y++) memcpy(na + (size_t)y * (size_t)nw, ui->atlas + (size_t)y * (size_t)ui->atlas_w, (size_t)ui->atlas_w);
    cui__free(ui, ui->atlas);
    ui->atlas = na;
    /* glyph uvs are computed from atlas size at draw time, so cached glyphs stay valid */
    ui->atlas_w = nw; ui->atlas_h = nh;
    ui->atlas_dirty = true;
    ui->dirty_x0 = 0; ui->dirty_y0 = 0; ui->dirty_x1 = nw; ui->dirty_y1 = nh;
}

static bool cui__atlas_pack(cui_ctx *ui, int w, int h, int *ox, int *oy) {
    for (;;) {
        if (ui->pack_x + w + 1 > ui->atlas_w) { ui->pack_x = 1; ui->pack_y += ui->pack_row_h + 1; ui->pack_row_h = 0; }
        if (ui->pack_y + h + 1 <= ui->atlas_h) break;
        if (ui->atlas_w >= CUI_ATLAS_MAX) return false;
        cui__atlas_grow(ui);
    }
    *ox = ui->pack_x; *oy = ui->pack_y;
    ui->pack_x += w + 1;
    if (h > ui->pack_row_h) ui->pack_row_h = h;
    return true;
}

static cui_glyph *cui__glyph_get(cui_ctx *ui, cui_font font, int gi, float px_size, int subpx) {
    uint16_t sq = (uint16_t)CUI_MIN(65535.0f, px_size * 4.0f + 0.5f);
    uint32_t key[3] = { (uint32_t)gi, (uint32_t)font | ((uint32_t)sq << 16), (uint32_t)subpx };
    uint32_t h = cui__hash(key, sizeof key, 77) & 4095;
    for (int i = ui->glyph_buckets[h]; i >= 0; i = ui->glyphs[i].next) {
        cui_glyph *g = &ui->glyphs[i];
        if (g->key_cp == (uint32_t)gi && g->font == font && g->size_q == sq && g->subpx == subpx) { g->stamp = (uint32_t)ui->frame; return g; }
    }
    if (!cui__gamma_ready) {
        for (int i = 0; i < 256; i++) cui__gamma_lut[i] = (uint8_t)(powf(i / 255.0f, 0.82f) * 255.0f + 0.5f);
        cui__gamma_ready = true;
    }
    cui_font_data *fd = &ui->fonts[font];
    float scale = stbtt_ScaleForMappingEmToPixels(&fd->info, sq / 4.0f);
    float shift = subpx / 3.0f;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&fd->info, gi, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
    int w = x1 - x0, hgt = y1 - y0;
    int ax = 0, ay = 0;
    if (w > 0 && hgt > 0) {
        if (!cui__atlas_pack(ui, w, hgt, &ax, &ay)) { ui->atlas_reset_pending = true; return NULL; }
        uint8_t *dst = ui->atlas + (size_t)ay * (size_t)ui->atlas_w + (size_t)ax;
        stbtt_MakeGlyphBitmapSubpixel(&fd->info, dst, w, hgt, ui->atlas_w, scale, scale, shift, 0, gi);
        if (!fd->is_icon) {
            for (int y = 0; y < hgt; y++) {
                uint8_t *row = dst + (size_t)y * (size_t)ui->atlas_w;
                for (int x = 0; x < w; x++) row[x] = cui__gamma_lut[row[x]];
            }
        }
        if (!ui->atlas_dirty) { ui->dirty_x0 = ax; ui->dirty_y0 = ay; ui->dirty_x1 = ax + w; ui->dirty_y1 = ay + hgt; }
        else {
            ui->dirty_x0 = CUI_MIN(ui->dirty_x0, ax); ui->dirty_y0 = CUI_MIN(ui->dirty_y0, ay);
            ui->dirty_x1 = CUI_MAX(ui->dirty_x1, ax + w); ui->dirty_y1 = CUI_MAX(ui->dirty_y1, ay + hgt);
        }
        ui->atlas_dirty = true;
    }
    CUI__GROW(ui, ui->glyphs, ui->glyph_count, ui->glyph_cap, 1);
    cui_glyph *g = &ui->glyphs[ui->glyph_count];
    g->key_cp = (uint32_t)gi; g->font = (uint16_t)font; g->size_q = sq; g->subpx = (uint8_t)subpx; g->stamp = (uint32_t)ui->frame;
    g->x0 = (int16_t)x0; g->y0 = (int16_t)y0; g->w = (uint16_t)CUI_MAX(w, 0); g->h = (uint16_t)CUI_MAX(hgt, 0);
    g->ax = (uint16_t)ax; g->ay = (uint16_t)ay;
    g->next = ui->glyph_buckets[h];
    ui->glyph_buckets[h] = ui->glyph_count;
    return &ui->glyphs[ui->glyph_count++];
}

/* The atlas grows when it fills up, and the glyphs of sizes no longer shown
 * (after a zoom, a change of display scale, a page that is gone) would keep it
 * big. Every few seconds, before anything is drawn, the glyphs drawn in the
 * last five seconds move into the smallest atlas they fill no more than half
 * of, and the rest are dropped; they are drawn again if they come back. */
#define CUI__ATLAS_KEEP 300   /* frames */
typedef struct { uint16_t h; int i; } cui__glyph_ord;
static int cui__glyph_ord_cmp(const void *a, const void *b) {
    const cui__glyph_ord *x = a, *y = b;
    return x->h != y->h ? (int)y->h - (int)x->h : x->i - y->i;
}
static void cui__atlas_compact(cui_ctx *ui) {
    uint32_t now = (uint32_t)ui->frame;
    size_t area = 0;
    int live = 0;
    for (int i = 0; i < ui->glyph_count; i++) {
        cui_glyph *g = &ui->glyphs[i];
        if (now - g->stamp > CUI__ATLAS_KEEP) continue;
        area += (size_t)(g->w + 1) * (size_t)(g->h + 1);
        live++;
    }
    int nw = ui->atlas_w;
    while (nw > CUI_ATLAS_MIN && area * 2 <= (size_t)(nw / 2) * (size_t)(nw / 2)) nw /= 2;
    if (nw == ui->atlas_w) return;
    cui__glyph_ord *ord = cui__alloc(ui, sizeof *ord * (size_t)CUI_MAX(live, 1));
    cui_glyph *ng = cui__alloc(ui, sizeof *ng * (size_t)CUI_MAX(live, 1));
    uint8_t *na = cui__alloc(ui, (size_t)nw * (size_t)nw);
    if (!ord || !ng || !na) { cui__free(ui, ord); cui__free(ui, ng); cui__free(ui, na); return; }
    int n = 0;
    for (int i = 0; i < ui->glyph_count; i++)
        if (now - ui->glyphs[i].stamp <= CUI__ATLAS_KEEP) ord[n++] = (cui__glyph_ord){ ui->glyphs[i].h, i };
    qsort(ord, (size_t)n, sizeof *ord, cui__glyph_ord_cmp);   /* tall first: shelves waste less */
    memset(na, 0, (size_t)nw * (size_t)nw);
    int px = 1, py = 1, row = 0;
    for (int k = 0; k < n; k++) {
        cui_glyph g = ui->glyphs[ord[k].i];
        if (g.w > 0 && g.h > 0) {
            if (px + g.w + 1 > nw) { px = 1; py += row + 1; row = 0; }
            if (py + g.h + 1 > nw) { cui__free(ui, ord); cui__free(ui, ng); cui__free(ui, na); return; }   /* does not fit after all */
            for (int y = 0; y < g.h; y++)
                memcpy(na + (size_t)(py + y) * (size_t)nw + (size_t)px, ui->atlas + (size_t)(g.ay + y) * (size_t)ui->atlas_w + g.ax, g.w);
            g.ax = (uint16_t)px; g.ay = (uint16_t)py;
            px += g.w + 1;
            if (g.h > row) row = g.h;
        }
        ng[k] = g;
    }
    cui__free(ui, ord);
    cui__free(ui, ui->glyphs);
    cui__free(ui, ui->atlas);
    ui->glyphs = ng; ui->glyph_count = n; ui->glyph_cap = CUI_MAX(n, 1);
    ui->atlas = na; ui->atlas_w = ui->atlas_h = nw;
    ui->pack_x = px; ui->pack_y = py; ui->pack_row_h = row;
    for (int i = 0; i < 4096; i++) ui->glyph_buckets[i] = -1;
    for (int i = 0; i < n; i++) {
        cui_glyph *g = &ui->glyphs[i];
        uint32_t key[3] = { g->key_cp, (uint32_t)g->font | ((uint32_t)g->size_q << 16), (uint32_t)g->subpx };
        uint32_t h = cui__hash(key, sizeof key, 77) & 4095;
        g->next = ui->glyph_buckets[h];
        ui->glyph_buckets[h] = i;
    }
    ui->atlas_dirty = true;
    ui->dirty_x0 = 0; ui->dirty_y0 = 0; ui->dirty_x1 = nw; ui->dirty_y1 = nw;
}

void cui__fonts_begin_frame(cui_ctx *ui) {
    if (!ui->atlas_reset_pending && ui->atlas_w > CUI_ATLAS_MIN && ui->frame - ui->atlas_check_frame >= CUI__ATLAS_KEEP) {
        ui->atlas_check_frame = ui->frame;
        cui__atlas_compact(ui);
    }
    if (!ui->atlas_reset_pending) return;
    ui->atlas_reset_pending = false;
    ui->glyph_count = 0;
    for (int i = 0; i < 4096; i++) ui->glyph_buckets[i] = -1;
    memset(ui->atlas, 0, (size_t)ui->atlas_w * (size_t)ui->atlas_h);
    ui->pack_x = 1; ui->pack_y = 1; ui->pack_row_h = 0;
    ui->atlas_dirty = true;
    ui->dirty_x0 = 0; ui->dirty_y0 = 0; ui->dirty_x1 = ui->atlas_w; ui->dirty_y1 = ui->atlas_h;
    ui->settle_frames = CUI_MAX(ui->settle_frames, 1);
}

void cui__fonts_upload(cui_ctx *ui) {
    cui_renderer_iface *r = &ui->renderer;
    if (!r->texture_create) return;
    if (ui->atlas_tex && (ui->atlas_tex_w != ui->atlas_w || ui->atlas_tex_h != ui->atlas_h)) {
        if (r->texture_destroy) r->texture_destroy(r->user, ui->atlas_tex);
        ui->atlas_tex = 0;
    }
    if (!ui->atlas_tex) {
        ui->atlas_tex = r->texture_create(r->user, ui->atlas_w, ui->atlas_h, CUI_TEXTURE_R8, ui->atlas);
        ui->atlas_tex_w = ui->atlas_w; ui->atlas_tex_h = ui->atlas_h;
        ui->atlas_dirty = false;
        return;
    }
    if (ui->atlas_dirty && r->texture_update) {
        int x0 = ui->dirty_x0, y0 = ui->dirty_y0;
        int w = ui->dirty_x1 - x0, h = ui->dirty_y1 - y0;
        if (w > 0 && h > 0)
            r->texture_update(r->user, ui->atlas_tex, x0, y0, w, h, ui->atlas + (size_t)y0 * (size_t)ui->atlas_w + (size_t)x0, ui->atlas_w);
    }
    ui->atlas_dirty = false;
}

#ifdef CUI_TEXT_SHAPING
/* Script boundaries needed by the supported Arabic, Hebrew and Latin runs.
 * shortcut: other scripts use latn; extend script data with their shaping support. */
static uint32_t cui__shape_script(uint32_t cp) {
    if ((cp >= 0x600 && cp <= 0x8ff) || (cp >= 0xfb50 && cp <= 0xfdff) ||
        (cp >= 0xfe70 && cp <= 0xfeff)) return 0x61726162;
    if ((cp >= 0x590 && cp <= 0x5ff) || (cp >= 0xfb1d && cp <= 0xfb4f)) return 0x68656272;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= 0xc0 && cp <= 0x2af)) return 0x6c61746e;
    return 0;
}

static float cui__shape_run(cui_ctx *ui, cui_font font, float size, const char *text, int len,
                             bool tabular, bool draw, float x, float y, cui_color color, float max_x) {
    cui_font_data *base = &ui->fonts[font];
    const char *paragraph = text;
    int plen = len, line_start = 0;
    if (ui->text_paragraph && (uintptr_t)text >= (uintptr_t)ui->text_paragraph &&
        (uintptr_t)text + len <= (uintptr_t)ui->text_paragraph + ui->text_paragraph_len) {
        paragraph = ui->text_paragraph; plen = ui->text_paragraph_len;
        line_start = (int)(text - paragraph);
        int ps = line_start, pe = line_start + len;
        while (ps > 0 && paragraph[ps - 1] != '\n') ps--;
        while (pe < plen && paragraph[pe] != '\n') pe++;
        paragraph += ps; plen = pe - ps; line_start -= ps;
    }
    int requested = -1;
    cui_shaped_paragraph *p = cui__paragraph(ui, paragraph, plen, requested);
    if (!p) return 0;
    cui_shaped_line *cached = p->lines;
    while (cached && !(cached->start == line_start && cached->end == line_start + len &&
        cached->font == font && cached->size == size && cached->tabular == tabular)) cached = cached->next;
    if (cached) goto paint;
    if (p->line_count == 128) cui__shape_lines_free(ui, p);
    cached = cui__alloc(ui, sizeof *cached);
    if (!cached) return 0;
    *cached = (cui_shaped_line){0};
    cached->start = line_start; cached->end = line_start + len;
    cached->font = font; cached->size = size; cached->tabular = tabular;
    cui_bidi_char *chars = p->chars;
    int *offsets = p->offsets;
    int first_char = 0, count = 0;
    while (first_char < p->count && offsets[first_char] < line_start) first_char++;
    while (first_char + count < p->count && offsets[first_char + count] < line_start + len) count++;
    int *order = cui__alloc(ui, ((size_t)count + 1) * sizeof *order);
    uint8_t *levels = cui__alloc(ui, (size_t)count + 1);
    if (!order || !levels) { cui__free(ui, order); cui__free(ui, levels); cui__free(ui, cached); return 0; }
    cui__bidi_line(chars, first_char, count, p->base, order, levels);
    text = paragraph;
    typedef cui_shaped_glyph shaped_glyph;
    typedef struct { int first, count; float width; } shaped_span;
    shaped_glyph *glyphs = NULL;
    shaped_span *spans = NULL;
    int glyph_count = 0, glyph_cap = 0, span_count = 0, span_cap = 0;
    float pen = 0;
    int result_cap = 0;
    for (int b = 0; b < count;) {
        int first = order[b], last = first;
        bool rtl = (levels[first - first_char] & 1) != 0;
        int step = rtl ? -1 : 1;
        b++;
        while (b < count && levels[order[b] - first_char] == levels[first - first_char] && order[b] == last + step)
            last = order[b++];
        int run_first = CUI_MIN(first, last), run_last = CUI_MAX(first, last) + 1;
        glyph_count = span_count = 0;
        for (int si = run_first; si < run_last;) {
            uint32_t tag = 0;
            int se = si;
            for (; se < run_last; se++) {
                uint32_t next = cui__shape_script(chars[se].cp);
                if (tag && next && tag != next) break;
                if (next) tag = next;
            }
            if (!tag) tag = 0x6c61746e; /* latn */
            int start = offsets[si], end = offsets[se];
            si = se;
            while (start < end) {
                uint32_t cp;
                cui_font gf; int gi;
                cui__utf8_decode(text + start, end - start, &cp);
                cui__find_glyph(ui, font, cp, &gf, &gi);
                int cap = (end - start) * 4 + 16, input_count = 0, at = start;
                cui_shape_glyph *input = cui__alloc(ui, (size_t)cap * sizeof *input);
                if (!input) break;
                while (at < end) {
                    int bytes = cui__utf8_decode(text + at, end - at, &cp);
                    cui_font cf; int cg;
                    cui__find_glyph(ui, font, cp, &cf, &cg);
                    bool control = cp >= 0x200b && cp <= 0x200f;
                    if (input_count && !control && cf != gf) break;
                    uint32_t mirror = cui__bidi_mirror(cp, rtl);
                    input[input_count++] = (cui_shape_glyph){ mirror ? mirror : cp, 0, at, 0, 0, 0, 0, 0, 0 };
                    at += bytes;
                }
                input_count = cui__shape(&ui->fonts[gf].shaped, input, input_count, cap, tag, rtl, tabular);
                CUI__GROW(ui, spans, span_count, span_cap, 1);
                shaped_span *span = &spans[span_count++];
                *span = (shaped_span){ glyph_count, 0, 0 };
                float em = size / ui->fonts[gf].upem;
                for (int k = 0; k < input_count; k++) {
                    cui_shape_glyph *g = &input[k];
                    CUI__GROW(ui, glyphs, glyph_count, glyph_cap, 1);
                    glyphs[glyph_count++] = (shaped_glyph){ gf, g->id, g->cluster,
                        span->width + g->x * em, g->y * em };
                    span->width += g->advance * em;
                    span->count++;
                }
                cui__free(ui, input);
                start = at;
            }
        }
        /* Script/font spans are logical; UBA orders the directional runs. */
        for (int i = 0; i < span_count; i++) {
            shaped_span *span = &spans[rtl ? span_count - 1 - i : i];
            for (int j = 0; j < span->count; j++) {
                CUI__GROW(ui, cached->glyphs, cached->count, result_cap, 1);
                cui_shaped_glyph g = glyphs[span->first + j];
                g.x += pen; g.start -= line_start;
                cached->glyphs[cached->count++] = g;
            }
            pen += span->width;
        }
    }
    cui__free(ui, glyphs);
    cui__free(ui, spans);
    cui__free(ui, order); cui__free(ui, levels);
    cached->width = pen; cached->next = p->lines; p->lines = cached; p->line_count++;
paint:
    if (draw) {
        float scale = ui->scale;
        float baseline = roundf((y + base->ascent * size) * scale);
        for (int j = 0; j < cached->count; j++) {
            cui_shaped_glyph *sg = &cached->glyphs[j];
            float gx = sg->x;
            if (!sg->id || x + gx > max_x) continue;
            float px = (x + gx) * scale, fl = floorf(px);
            int subpx = CUI_MIN(2, (int)((px - fl) * 3));
            float gy = baseline - sg->y * scale;
            cui_color gc = color;
            if (ui->text_fx) {
                float dy = 0;
                gc = ui->text_fx(ui, ui->text_fx_data, ui->text_fx_base + sg->start,
                    x + gx, ui->text_fx_line_y, &dy, color);
                gy += roundf(dy * scale);
            }
            cui_glyph *g = cui__glyph_get(ui, sg->font, sg->id, size * scale, subpx);
            if (g && g->w && gc.a) {
                float iw = 1.0f / ui->atlas_w, ih = 1.0f / ui->atlas_h;
                float qx = fl + g->x0, qy = gy + g->y0;
                cui__draw_glyph_quad(ui, qx, qy, qx + g->w, qy + g->h,
                    g->ax * iw, g->ay * ih, (g->ax + g->w) * iw, (g->ay + g->h) * ih, gc);
            }
        }
    }
    return cached->width;
}
#endif

/* ---- drawing ---- */

static void cui__draw_run(cui_ctx *ui, cui_font font, float size, float x, float y, cui_color color,
                          const char *text, int len, bool tabular, float max_x) {
    font = cui__resolve_font(ui, font);
    if (font < 0 || !text || len <= 0) return;
    color = cui_color_of(ui, color);
    if (color.a == 0) return;
#ifdef CUI_TEXT_SHAPING
    if (cui__text_needs_shaping(ui, text, len)) {
        cui__shape_run(ui, font, size, text, len, tabular, true, x, y, color, max_x);
        return;
    }
#endif
    float s = ui->scale;
    cui_font_data *base = &ui->fonts[font];
    /* baseline: center the font box in the line box (line box = size * 1.0 here;
     * callers pass the top of the font box) */
    float baseline = y + base->ascent * size;
    if (base->is_icon) baseline = y + size;
    float by_px = roundf(baseline * s);
    float inv_w = 1.0f / (float)ui->atlas_w, inv_h = 1.0f / (float)ui->atlas_h;
    cui__glyph_iter it;
    cui__iter_init(&it, ui, font, size, text, len, tabular);
    while (cui__iter_next(&it)) {
        if (it.gx + x > max_x) break;
        if (it.cp == ' ' || it.cp == '\n' || it.cp == '\t' || !it.gi) continue;
        float gs_px = it.gsize * s;
        float pen = (x + it.gx + it.center_off) * s;
        float fl = floorf(pen);
        int subpx = (int)((pen - fl) * 3.0f);
        if (subpx > 2) subpx = 2;
        float gy = by_px;
        cui_font_data *gfd = &ui->fonts[it.gfont];
        if (gfd->is_icon && it.gfont != font) {
            /* inline icon: vertically center on cap height */
            float center = baseline - base->cap_height * size * 0.5f;
            gy = roundf((center + it.gsize * 0.5f) * s);
        } else if (gfd->is_icon) {
            gy = roundf((y + size) * s);
        }
        if (gs_px < 1) continue;
        cui_color gc = color;
        if (ui->text_fx) {
            float dy = 0;
            gc = ui->text_fx(ui, ui->text_fx_data, ui->text_fx_base + it.start, x + it.gx, ui->text_fx_line_y, &dy, color);
            if (gc.a == 0) continue;
            gy += roundf(dy * s);
        }
        cui_glyph *g = cui__glyph_get(ui, it.gfont, it.gi, gs_px, subpx);
        if (!g || !g->w) continue;
        float qx0 = fl + g->x0, qy0 = gy + g->y0;
        cui__draw_glyph_quad(ui, qx0, qy0, qx0 + g->w, qy0 + g->h,
                             g->ax * inv_w, g->ay * inv_h, (g->ax + g->w) * inv_w, (g->ay + g->h) * inv_h, gc);
    }
}

void cui__draw_text_line(cui_ctx *ui, cui_font font, float size, float x, float y, cui_color color,
                         const char *text, int len, bool tabular) {
    if (len < 0 && text) len = (int)strlen(text);
    cui__draw_run(ui, font, size, x, y, color, text, len, tabular, 1e30f);
}

void cui__draw_text_ellipsis(cui_ctx *ui, cui_font font, float size, float x, float y, float max_w,
                             cui_color color, const char *text, int len, bool tabular) {
    if (len < 0 && text) len = (int)strlen(text);
    float w = cui__text_width(ui, font, size, text, len, tabular);
    if (w <= max_w + 0.5f) { cui__draw_run(ui, font, size, x, y, color, text, len, tabular, 1e30f); return; }
    const char *ell = "\xe2\x80\xa6";
    float ew = cui__text_width(ui, font, size, ell, 3, false);
    float avail = max_w - ew;
    if (avail <= 0) return;
    /* find the longest prefix that fits */
    cui_font rf = cui__resolve_font(ui, font);
    cui__glyph_iter it;
    cui__iter_init(&it, ui, rf, size, text, len, tabular);
    int cut = 0;
    float cut_w = 0;
    while (cui__iter_next(&it)) {
        if (it.x > avail) break;
        cut = it.end;
        cut_w = it.x;
    }
    while (cut > 0 && text[cut - 1] == ' ') { cut--; cut_w = cui__text_width(ui, font, size, text, cut, tabular); }
    /* right-to-left text ends on the left, so its ellipsis goes there */
    bool rtl = false;
#ifdef CUI_TEXT_SHAPING
    for (int at = 0; at < len;) {
        uint32_t cp;
        at += cui__utf8_decode(text + at, len - at, &cp);
        int dir = cui__bidi_strong_direction(cp);
        if (dir >= 0) { rtl = dir == 1; break; }
    }
#endif
    cui__draw_run(ui, font, size, rtl ? x + ew : x, y, color, text, cut, tabular, 1e30f);
    cui__text_fx_fn fx = ui->text_fx;
    ui->text_fx = NULL;   /* the ellipsis is not part of the text */
    cui__draw_run(ui, font, size, rtl ? x : x + cut_w, y, color, ell, 3, false, 1e30f);
    ui->text_fx = fx;
}

void cui_draw_text(cui_ctx *ui, cui_font font, float size, cui_vec2 pos, cui_color color, const char *text, int len) {
    cui__draw_text_line(ui, font, size, pos.x, pos.y, color, text, len, false);
}

void cui_draw_icon(cui_ctx *ui, const char *icon, cui_rect r, float size, cui_color color) {
    if (!icon) return;
    float x = r.x + (r.w - size) * 0.5f, y = r.y + (r.h - size) * 0.5f;
    cui__draw_text_line(ui, CUI_FONT_ICONS, size, x, y, color, icon, (int)strlen(icon), false);
}
