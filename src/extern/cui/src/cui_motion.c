/* Motion and text effects: shimmer, typewriter, word reveal, rotating words,
 * loaders, dock, tab bar, control tiles, waveform and a now playing card. */
#include "cui_internal.h"

#define cui__fs cui__fsz

static bool cui__still(cui_ctx *ui) { return ui->theme.motion <= 0.0f; }

static cui_color cui__mo_accent(cui_ctx *ui) {
    return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
}

static int cui__fx_text_node(cui_ctx *ui, const char *text, const cui_text_fx_opts *o, cui_color c) {
    float size = o->size > 0 ? o->size : ui->theme.text_body;
    int i = cui__text_node(ui, text, -1, o->font, cui__fs(ui, size), c, !o->nowrap);
    if (o->width.kind != CUI_SIZE_FIT) ui->nodes[i].w = o->width;
    return i;
}

static float cui__ease_out(float t) { t = cui__clampf(t, 0, 1); float u = 1 - t; return 1 - u * u * u; }

/* ------------------------------------------------------------------------ */
/* Shimmer                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct { int node; float t; cui_color hi; float band; } cui__shimmer_fx;

static cui_color cui__fx_shimmer(cui_ctx *ui, void *data, int byte, float x, float line_y, float *dy, cui_color c) {
    (void)byte; (void)line_y; (void)dy;
    cui__shimmer_fx *p = data;
    cui_rect r = ui->nodes[p->node].rect;
    float travel = r.w + p->band * 2;
    float cx = r.x - p->band + travel * p->t;
    float d = (x - cx) / p->band;
    float k = expf(-d * d * 2.2f);
    return cui_mix(c, p->hi, k);
}

void cui_shimmer_text_ex(cui_ctx *ui, const char *text, const cui_text_fx_opts *o) {
    cui_text_fx_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_color base = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, TEXT_MUTED);
    int i = cui__fx_text_node(ui, text, o, base);
    if (cui__still(ui)) { ui->nodes[i].text_color = cui_mix(base, CUI_C(ui, FOREGROUND), 0.35f); return; }
    cui__shimmer_fx *p = cui__frame_alloc(ui, sizeof *p);
    float period = 1.0f / (o->speed > 0 ? o->speed : 0.6f);
    double t = fmod(ui->time, (double)period) / period;
    p->node = i;
    p->t = (float)t;
    p->hi = ui->mode == CUI_THEME_DARK ? cui__rgba(255, 255, 255, 255) : CUI_C(ui, FOREGROUND);
    p->band = cui__fs(ui, o->size > 0 ? o->size : ui->theme.text_body) * 3.2f;
    ui->nodes[i].text_fx = cui__fx_shimmer;
    ui->nodes[i].text_fx_data = p;
    ui->animating = true;
}

/* ------------------------------------------------------------------------ */
/* Typewriter                                                               */
/* ------------------------------------------------------------------------ */

typedef struct { float chars; int bytes; int prev_len; uint32_t prev_hash; double started; bool init; } cui__tw_state;
typedef struct { int visible; bool caret; bool caret_drawn; float size; cui_font font; cui_color caret_c; int len; } cui__tw_fx;

static cui_color cui__fx_typewriter(cui_ctx *ui, void *data, int byte, float x, float line_y, float *dy, cui_color c) {
    (void)dy;
    cui__tw_fx *p = data;
    if (byte < p->visible) return c;
    if (p->caret && !p->caret_drawn) {
        p->caret_drawn = true;
        float fh = cui__font_line_height(ui, p->font, p->size);
        float lh = p->size * 1.45f;
        cui_draw_rect(ui, (cui_rect){ x + 2.0f, line_y + (lh - fh) * 0.5f + fh * 0.12f, 2, fh * 0.76f }, CUI_CORNERS(1), p->caret_c);
    }
    c.a = 0;
    return c;
}

bool cui_typewriter_ex(cui_ctx *ui, const char *id_str, const char *text, const cui_text_fx_opts *o) {
    cui_text_fx_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!text) text = "";
    int len = (int)strlen(text);
    cui_id id = cui_make_id(ui, id_str);
    cui__tw_state *st = cui_state(ui, id, sizeof *st);
    /* grown from what was shown before? then keep going, otherwise start over */
    bool grew = st->init && len >= st->prev_len && cui__hash(text, (size_t)st->prev_len, 0x7E1u) == st->prev_hash;
    if (!grew) { st->chars = 0; st->bytes = 0; st->started = ui->time; st->init = true; }
    st->prev_len = len;
    st->prev_hash = cui__hash(text, (size_t)len, 0x7E1u);
    if (cui__still(ui)) st->bytes = len;
    else if (ui->time - st->started >= o->delay && st->bytes < len) {
        float speed = o->speed > 0 ? o->speed : 45;
        /* catch up faster when far behind, as with streamed text */
        int behind = len - st->bytes;
        float boost = behind > 120 ? (float)behind / 120.0f : 1.0f;
        st->chars += ui->dt * speed * boost;
        while (st->chars >= 1 && st->bytes < len) { st->bytes = cui__utf8_next(text, len, st->bytes); st->chars -= 1; }
        ui->animating = true;
    } else if (st->bytes < len) {
        cui__wakeup_after(ui, (float)(o->delay - (ui->time - st->started)));
    }
    if (st->bytes > len) st->bytes = len;
    cui_color c = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, FOREGROUND);
    int i = cui__fx_text_node(ui, text, o, c);
    bool done = st->bytes >= len;
    if (!done) {
        cui__tw_fx *p = cui__frame_alloc(ui, sizeof *p);
        memset(p, 0, sizeof *p);
        p->visible = st->bytes; p->caret = o->caret; p->len = len;
        p->size = ui->nodes[i].font_size; p->font = ui->nodes[i].font;
        p->caret_c = cui__mo_accent(ui);
        ui->nodes[i].text_fx = cui__fx_typewriter;
        ui->nodes[i].text_fx_data = p;
    }
    ui->last_id = id;
    return done;
}

/* ------------------------------------------------------------------------ */
/* Word reveal                                                              */
/* ------------------------------------------------------------------------ */

typedef struct { uint32_t hash; double start; bool init; } cui__rv_state;
typedef struct { const unsigned short *word; int len; float elapsed, stagger, dur; } cui__rv_fx;

static cui_color cui__fx_reveal(cui_ctx *ui, void *data, int byte, float x, float line_y, float *dy, cui_color c) {
    (void)ui; (void)x; (void)line_y;
    cui__rv_fx *p = data;
    int w = byte < p->len ? p->word[byte] : 0;
    float t = (p->elapsed - w * p->stagger) / p->dur;
    float e = cui__ease_out(t);
    *dy = (1 - e) * 7;
    c.a = (uint8_t)(c.a * cui__clampf(t * 1.4f, 0, 1));
    return c;
}

void cui_text_reveal_ex(cui_ctx *ui, const char *id_str, const char *text, const cui_text_fx_opts *o) {
    cui_text_fx_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!text) text = "";
    int len = (int)strlen(text);
    cui_id id = cui_make_id(ui, id_str);
    cui__rv_state *st = cui_state(ui, id, sizeof *st);
    uint32_t h = cui__hash(text, (size_t)len, 0x2E7u);
    if (!st->init || st->hash != h) { st->hash = h; st->start = ui->time; st->init = true; }
    cui_color c = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, FOREGROUND);
    int i = cui__fx_text_node(ui, text, o, c);
    ui->last_id = id;
    if (cui__still(ui)) return;
    float stagger = o->speed > 0 ? 1.0f / o->speed : 0.045f, dur = 0.55f;
    unsigned short *word = cui__frame_alloc(ui, sizeof(unsigned short) * (size_t)CUI_MAX(1, len));
    int w = 0;
    for (int k = 0; k < len; k++) {
        if (k > 0 && text[k - 1] == ' ' && text[k] != ' ') w++;
        word[k] = (unsigned short)CUI_MIN(w, 65535);
    }
    float elapsed = (float)(ui->time - st->start) - o->delay;
    if (elapsed < w * stagger + dur) {
        cui__rv_fx *p = cui__frame_alloc(ui, sizeof *p);
        p->word = word; p->len = len; p->elapsed = elapsed; p->stagger = stagger; p->dur = dur;
        ui->nodes[i].text_fx = cui__fx_reveal;
        ui->nodes[i].text_fx_data = p;
        ui->animating = true;
    }
}

/* ------------------------------------------------------------------------ */
/* Rotating words                                                           */
/* ------------------------------------------------------------------------ */

typedef struct { int index, prev; double last; cui_spring w, t; bool init; } cui__wr_state;
typedef struct { const char *cur, *prev; float t; cui_font font; float size; cui_color c; float lh; } cui__wr_paint;

static void cui__paint_word_rotate(cui_ctx *ui, cui_node *n, void *data) {
    cui__wr_paint *p = data;
    cui_rect r = n->rect;
    float fh = cui__font_line_height(ui, p->font, p->size);
    float y = r.y + (r.h - fh) * 0.5f;
    float t = cui__clampf(p->t, 0, 1.2f);
    cui_push_clip(ui, (cui_rect){ r.x - 4, r.y, r.w + 8, r.h });
    if (p->prev && t < 1) {
        cui_color c = p->c; c.a = (uint8_t)(c.a * cui__clampf(1 - t * 1.3f, 0, 1));
        cui__draw_text_line(ui, p->font, p->size, r.x, y - t * r.h * 0.9f, c, p->prev, -1, false);
    }
    cui_color c = p->c; c.a = (uint8_t)(c.a * cui__clampf(t * 1.2f, 0, 1));
    cui__draw_text_line(ui, p->font, p->size, r.x, y + (1 - t) * r.h * 0.9f, c, p->cur, -1, false);
    cui_pop_clip(ui);
}

void cui_word_rotate_ex(cui_ctx *ui, const char *id_str, const char *const *words, int count, float interval, const cui_text_fx_opts *o) {
    cui_text_fx_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__wr_state *st = cui_state(ui, id, sizeof *st);
    if (interval <= 0) interval = 2.4f;
    float size = cui__fs(ui, o->size > 0 ? o->size : ui->theme.text_body);
    cui_font font = o->font;
    if (count <= 0 || !words) { cui__leaf(ui, id, CUI_PX(0), CUI_PX(size * 1.3f)); return; }
    if (!st->init) { st->index = 0; st->prev = -1; st->last = ui->time; st->t.x = 1; st->init = true; }
    if (st->index >= count) st->index = 0;
    if (count > 1 && !cui__still(ui)) {
        double since = ui->time - st->last;
        if (since >= interval) { st->prev = st->index; st->index = (st->index + 1) % count; st->last = ui->time; st->t.x = 0; st->t.v = 0; }
        else cui__wakeup_after(ui, (float)(interval - since) + 0.01f);
    }
    cui__spring(ui, &st->t, 1.0f, 0.55f, 0.12f);
    const char *cur = words[st->index] ? words[st->index] : "";
    float tw = cui__text_width(ui, font, size, cur, -1, false);
    if (st->w.x <= 0) st->w.x = tw;
    cui__spring(ui, &st->w, tw, 0.5f, 0.0f);
    float lh = size * 1.3f;
    int n = cui__leaf(ui, id, CUI_PX(CUI_MAX(1.0f, st->w.x)), CUI_PX(lh));
    cui__wr_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->cur = cur; p->prev = st->prev >= 0 && st->prev < count ? words[st->prev] : NULL;
    p->t = st->t.x; p->font = font; p->size = size; p->lh = lh;
    p->c = cui__color_set(o->color) ? cui_color_of(ui, o->color) : cui__mo_accent(ui);
    ui->nodes[n].paint = cui__paint_word_rotate;
    ui->nodes[n].paint_data = p;
    ui->last_id = id;
}

/* ------------------------------------------------------------------------ */
/* Rolling number                                                           */
/* ------------------------------------------------------------------------ */

#define CUI__NR_MAX 24
typedef struct {
    char shown[96];
    char prefix[24], suffix[24];                    /* "$" and " ms" stay put; only the figure rolls */
    char from[CUI__NR_MAX][5], to[CUI__NR_MAX][5];   /* per slot, counted from the right */
    cui_spring t[CUI__NR_MAX];
    double start[CUI__NR_MAX];
    int slots, dir;
    cui_spring w;
    bool init;
} cui__nr_state;
typedef struct { cui__nr_state s; cui_font font; float size; cui_color c; float lh; } cui__nr_paint;

/* splits text into code points, last first */
static int cui__nr_split(const char *text, char out[CUI__NR_MAX][5]) {
    char tmp[CUI__NR_MAX][5] = { { 0 } };
    int n = 0, len = (int)strlen(text);
    for (int i = 0; i < len && n < CUI__NR_MAX;) {
        uint32_t cp;
        int k = cui__utf8_decode(text + i, len - i, &cp);
        if (k <= 0) k = 1;
        if (k > 4) k = 4;
        memcpy(tmp[n], text + i, (size_t)k); tmp[n][k] = 0;
        n++; i += k;
    }
    for (int i = 0; i < n; i++) memcpy(out[i], tmp[n - 1 - i], 5);
    return n;
}

static double cui__nr_value(const char *s) {
    char buf[64]; int w = 0; bool neg = false;
    for (; *s && w < 62; s++) {
        if (*s >= '0' && *s <= '9') buf[w++] = *s;
        else if (*s == '.' && w) buf[w++] = '.';
        else if (*s == '-' && !w) neg = true;
    }
    buf[w] = 0;
    return (neg ? -1 : 1) * atof(buf);
}

static void cui__paint_number_roll(cui_ctx *ui, cui_node *n, void *data) {
    cui__nr_paint *p = data;
    cui_rect r = n->rect;
    float fh = cui__font_line_height(ui, p->font, p->size);
    float y = r.y + (r.h - fh) * 0.5f;
    float x = r.x + r.w;
    cui_push_clip(ui, (cui_rect){ r.x - 6, r.y, r.w + 12, r.h });
    if (p->s.prefix[0]) cui__draw_text_line(ui, p->font, p->size, r.x, y, p->c, p->s.prefix, -1, true);
    if (p->s.suffix[0]) {
        x -= cui__text_width(ui, p->font, p->size, p->s.suffix, -1, true);
        cui__draw_text_line(ui, p->font, p->size, x, y, p->c, p->s.suffix, -1, true);
    }
    for (int k = 0; k < p->s.slots; k++) {
        const char *a = p->s.from[k], *b = p->s.to[k];
        float t = p->s.t[k].x;
        float wa = a[0] ? cui__text_width(ui, p->font, p->size, a, -1, true) : 0;
        float wb = b[0] ? cui__text_width(ui, p->font, p->size, b, -1, true) : 0;
        float sw = cui__lerp(wa, wb, cui__clampf(t, 0, 1));
        x -= sw;
        if (t >= 0.999f || !strcmp(a, b)) {
            if (b[0]) cui__draw_text_line(ui, p->font, p->size, x + (sw - wb) * 0.5f, y, p->c, b, -1, true);
            continue;
        }
        /* rising values roll up into place, falling ones roll down */
        float d = p->lh * 0.85f * (float)p->s.dir;
        if (a[0]) {
            cui_color c = p->c; c.a = (uint8_t)(c.a * cui__clampf(1 - t * 1.4f, 0, 1));
            cui__draw_text_line(ui, p->font, p->size, x + (sw - wa) * 0.5f, y - t * d, c, a, -1, true);
        }
        if (b[0]) {
            cui_color c = p->c; c.a = (uint8_t)(c.a * cui__clampf(t * 1.3f, 0, 1));
            cui__draw_text_line(ui, p->font, p->size, x + (sw - wb) * 0.5f, y + (1 - t) * d, c, b, -1, true);
        }
    }
    cui_pop_clip(ui);
}

void cui_number_roll_ex(cui_ctx *ui, const char *id_str, const char *text, const cui_text_fx_opts *o) {
    cui_text_fx_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!text) text = "";
    cui_id id = cui_make_id(ui, id_str);
    cui__nr_state *st = cui_state(ui, id, sizeof *st);
    float size = cui__fs(ui, o->size > 0 ? o->size : ui->theme.text_body);
    cui_font font = o->font;
    if (!st->init || strcmp(st->shown, text) != 0) {
        /* prefix up to the first digit, suffix after the last one */
        int len = (int)strlen(text), a = 0, b = len;
        while (a < len && !(text[a] >= '0' && text[a] <= '9')) a++;
        while (b > a && !(text[b - 1] >= '0' && text[b - 1] <= '9')) b--;
        if (a > 0 && text[a - 1] == '-') a--;   /* a sign rolls with the figure */
        snprintf(st->prefix, sizeof st->prefix, "%.*s", a, text);
        snprintf(st->suffix, sizeof st->suffix, "%s", text + b);
        char body[96];
        snprintf(body, sizeof body, "%.*s", b - a, text + a);
        char nw[CUI__NR_MAX][5];
        int nn = cui__nr_split(body, nw);
        if (!st->init) {
            for (int k = 0; k < nn; k++) { memcpy(st->from[k], nw[k], 5); memcpy(st->to[k], nw[k], 5); st->t[k].x = 1; st->t[k].v = 0; }
            st->slots = nn; st->dir = 1; st->init = true;
        } else {
            double ov = cui__nr_value(st->shown), nv = cui__nr_value(text);
            st->dir = nv >= ov ? 1 : -1;
            int slots = CUI_MAX(nn, st->slots);
            /* only the characters that change roll, a beat apart from the left */
            int rank = 0;
            for (int k = slots - 1; k >= 0; k--) {
                char cur[5] = "", want[5] = "";   /* copies: a slot may be rewritten from itself */
                if (k < st->slots) snprintf(cur, sizeof cur, "%s", st->t[k].x >= 0.5f ? st->to[k] : st->from[k]);
                if (k < nn) snprintf(want, sizeof want, "%s", nw[k]);
                if (strcmp(cur, want) == 0 && (k >= st->slots || st->t[k].x >= 0.999f)) {
                    memcpy(st->from[k], want, 5); memcpy(st->to[k], want, 5); st->t[k].x = 1; st->t[k].v = 0;
                    continue;
                }
                memcpy(st->from[k], cur, 5); memcpy(st->to[k], want, 5);
                st->t[k].x = 0; st->t[k].v = 0;
                st->start[k] = ui->time + 0.035 * rank++;
            }
            st->slots = slots;
        }
        snprintf(st->shown, sizeof st->shown, "%s", text);
    }
    bool still = cui__still(ui);
    for (int k = 0; k < st->slots; k++) {
        if (st->t[k].x >= 1 && st->t[k].v == 0) continue;
        if (still) { st->t[k].x = 1; st->t[k].v = 0; continue; }
        if (ui->time >= st->start[k]) cui__spring(ui, &st->t[k], 1.0f, 0.5f, 0.16f);
        else { ui->animating = true; cui__wakeup_after(ui, (float)(st->start[k] - ui->time)); }
    }
    /* a slot that is now empty on both sides is dropped once it has rolled out */
    while (st->slots > 0 && !st->to[st->slots - 1][0] && st->t[st->slots - 1].x >= 0.999f) st->slots--;
    float tw = cui__text_width(ui, font, size, text, -1, true);
    if (st->w.x <= 0) st->w.x = tw;
    cui__spring(ui, &st->w, tw, 0.45f, 0.0f);
    float lh = size * 1.3f;
    int n = cui__leaf(ui, id, CUI_PX(CUI_MAX(1.0f, st->w.x)), CUI_PX(lh));
    cui__nr_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->s = *st; p->font = font; p->size = size; p->lh = lh;
    p->c = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, FOREGROUND);
    ui->nodes[n].paint = cui__paint_number_roll;
    ui->nodes[n].paint_data = p;
    ui->last_id = id;
}

/* ------------------------------------------------------------------------ */
/* Gradient text                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { int node; cui_color a, b; float shift; } cui__grad_fx;

static cui_color cui__fx_gradient(cui_ctx *ui, void *data, int byte, float x, float line_y, float *dy, cui_color c) {
    (void)byte; (void)line_y; (void)dy;
    cui__grad_fx *p = data;
    cui_rect r = ui->nodes[p->node].rect;
    float t = r.w > 0 ? (x - r.x) / r.w : 0;
    t = t + p->shift;
    t = t - floorf(t);
    float k = t < 0.5f ? t * 2 : (1 - t) * 2;   /* there and back, so a drift loops */
    cui_color g = cui_mix(p->a, p->b, k);
    g.a = (uint8_t)(g.a * c.a / 255);
    return g;
}

void cui_gradient_text_ex(cui_ctx *ui, const char *text, const cui_gradient_text_opts *o) {
    cui_gradient_text_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_text_fx_opts to;
    memset(&to, 0, sizeof to);
    to.font = o->font; to.size = o->size; to.width = o->width;
    int i = cui__fx_text_node(ui, text, &to, CUI_C(ui, FOREGROUND));
    cui__grad_fx *p = cui__frame_alloc(ui, sizeof *p);
    p->node = i;
    p->a = cui__color_set(o->from) ? cui_color_of(ui, o->from) : cui__mo_accent(ui);
    /* by default a tonal blend within the accent, not a second hue */
    p->b = cui__color_set(o->to) ? cui_color_of(ui, o->to) : cui_mix(p->a, CUI_C(ui, FOREGROUND), 0.6f);
    p->shift = 0;
    if (o->animate && !cui__still(ui)) { p->shift = (float)fmod(ui->time * 0.12, 1.0); ui->animating = true; }
    else p->shift = 0;
    /* without a shift the left edge starts at the first color and the right ends at the second */
    if (!o->animate) p->shift = 0;
    ui->nodes[i].text_fx = cui__fx_gradient;
    ui->nodes[i].text_fx_data = p;
}

/* ------------------------------------------------------------------------ */
/* Spotlight card                                                           */
/* ------------------------------------------------------------------------ */

typedef struct { float hover; float mx, my; bool init; } cui__spot_state;
typedef struct { float hover, mx, my; cui_color c; bool beam; float t; } cui__spot_paint;

static void cui__lit_border(cui_ctx *ui, cui_rect r, cui_corners rad, float cx, float cy, float reach, cui_color c) {
    /* nested bands, each a little brighter: the edge fades out smoothly instead of in three hard steps */
    enum { B = 12 };
    for (int i = 0; i < B; i++) {
        float s = reach * (1.0f - (float)i / B);
        cui_push_clip(ui, (cui_rect){ cx - s, cy - s, s * 2, s * 2 });
        cui_draw_rect_border(ui, r, rad, cui__rgba(0, 0, 0, 0), cui_alpha(c, 1.0f / B), 1.5f);
        cui_pop_clip(ui);
    }
}

static void cui__paint_spot_under(cui_ctx *ui, cui_node *n, void *data) {
    cui__spot_paint *p = data;
    if (p->hover <= 0.01f) return;
    cui_rect r = n->rect;
    float R = CUI_MIN(220.0f, CUI_MAX(r.w, r.h) * 0.45f);
    cui_push_clip(ui, (cui_rect){ r.x + 1, r.y + 1, r.w - 2, r.h - 2 });
    cui_draw_shadow(ui, (cui_rect){ p->mx - R * 0.5f, p->my - R * 0.5f, R, R }, CUI_CORNERS(R * 0.5f), R * 0.6f, 0, (cui_vec2){ 0, 0 },
                    cui_alpha(p->c, (ui->mode == CUI_THEME_DARK ? 0.16f : 0.1f) * p->hover));
    cui_pop_clip(ui);
}

static void cui__paint_spot_over(cui_ctx *ui, cui_node *n, void *data) {
    cui__spot_paint *p = data;
    cui_rect r = n->rect;
    if (p->hover > 0.01f) cui__lit_border(ui, r, n->radius, p->mx, p->my, 120, cui_alpha(p->c, p->hover));
    if (p->beam) {
        /* a light travelling around the edge */
        float per = 2 * (r.w + r.h);
        float d = fmodf(p->t * 140.0f, per);
        float x, y;
        if (d < r.w) { x = r.x + d; y = r.y; }
        else if (d < r.w + r.h) { x = r.x + r.w; y = r.y + d - r.w; }
        else if (d < 2 * r.w + r.h) { x = r.x + r.w - (d - r.w - r.h); y = r.y + r.h; }
        else { x = r.x; y = r.y + r.h - (d - 2 * r.w - r.h); }
        cui__lit_border(ui, r, n->radius, x, y, 70, p->c);
    }
}

struct cui_spot_build { int node; struct cui_spot_build *prev; };

void cui_spotlight_begin(cui_ctx *ui, const char *id_str, const cui_spotlight_opts *o) {
    cui_spotlight_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_rect pr = cui__prev_rect(ui, id);
    bool inside = pr.w > 0 && ui->mouse_inside && ui->mx >= pr.x && ui->mx < pr.x + pr.w && ui->my >= pr.y && ui->my < pr.y + pr.h;
    cui__spot_state *st = cui_state(ui, id, sizeof *st);
    cui__tween(ui, &st->hover, inside ? 1.0f : 0.0f, CUI__T_HOVER);
    if (inside || !st->init) { st->mx = ui->mx; st->my = ui->my; st->init = true; }
    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->w = o->width.kind ? o->width : CUI_FILL;
    if (o->height.kind) nd->h = o->height;
    float pad = o->pad > 0 ? o->pad : 20;
    nd->pad[0] = nd->pad[1] = nd->pad[2] = nd->pad[3] = pad;
    nd->gap = o->gap > 0 ? o->gap : 10;
    nd->radius = CUI_CORNERS(ui->theme.radius_panel);
    nd->bg = CUI_C(ui, SURFACE);
    nd->border = 1; nd->border_color = CUI_C(ui, BORDER);   /* rests on its edge, like any card: no shadow */
    cui__spot_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->hover = st->hover; p->mx = st->mx; p->my = st->my;
    p->c = cui__color_set(o->color) ? cui_color_of(ui, o->color) : (ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT));
    p->beam = o->beam && !cui__still(ui);
    p->t = (float)fmod(ui->time, 10000.0);
    nd->paint = cui__paint_spot_over;   /* the lit edge, above the content */
    nd->paint_data = p;
    nd->flags |= CUI_NF_PAINT_AFTER;
    if (p->beam || st->hover > 0.01f) ui->animating = true;
    cui__open(ui, n);
    /* the light itself, under the content (absolute children paint first) */
    int over = cui__absolute(ui, 0, 0, 0, pr.w > 0 ? pr.w : 0, pr.h > 0 ? pr.h : 0);
    ui->nodes[over].paint = cui__paint_spot_under;
    ui->nodes[over].paint_data = p;
    struct cui_spot_build *b = cui__frame_alloc(ui, sizeof *b);
    b->node = over; b->prev = (struct cui_spot_build *)ui->spot;
    ui->spot = b;
    ui->last_id = id;
}

void cui_spotlight_end(cui_ctx *ui) {
    struct cui_spot_build *b = (struct cui_spot_build *)ui->spot;
    if (b) ui->spot = b->prev;
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Loaders                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct { cui_loader_style style; float t; cui_color c; } cui__ld_paint;

static void cui__paint_loader(cui_ctx *ui, cui_node *n, void *data) {
    cui__ld_paint *p = data;
    cui_rect r = n->rect;
    float s = CUI_MIN(r.w, r.h * (p->style == CUI_LOADER_DOTS ? 2.0f : 1.0f));
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    float t = p->t;
    switch (p->style) {
    case CUI_LOADER_DOTS: {
        float rad = s * 0.1f, sp = s * 0.3f;
        for (int k = 0; k < 3; k++) {
            float ph = t * 2 * CUI_PI * 1.2f - k * 0.7f;
            float b = CUI_MAX(0.0f, sinf(ph));
            cui_color col = p->c; col.a = (uint8_t)(col.a * (0.45f + 0.55f * b));
            cui_draw_circle(ui, (cui_vec2){ c.x + (k - 1) * sp, c.y + rad - b * s * 0.16f }, rad, col);
        }
    } break;
    case CUI_LOADER_BARS: {
        float bw = s * 0.12f, gap = s * 0.1f;
        for (int k = 0; k < 4; k++) {
            float ph = t * 2 * CUI_PI * 1.1f - k * 0.9f;
            float h = s * (0.32f + 0.5f * (0.5f + 0.5f * sinf(ph)));
            float x = c.x - (4 * bw + 3 * gap) * 0.5f + k * (bw + gap);
            cui_draw_rect(ui, (cui_rect){ x, c.y - h * 0.5f, bw, h }, CUI_CORNERS(bw * 0.5f), p->c);
        }
    } break;
    case CUI_LOADER_PULSE: {
        for (int k = 0; k < 2; k++) {
            float ph = fmodf(t * 0.9f + k * 0.5f, 1.0f);
            float rr = s * 0.5f * cui__ease_out(ph);
            cui_color col = p->c; col.a = (uint8_t)(col.a * (1 - ph) * 0.9f);
            cui_draw_arc(ui, c, CUI_MAX(1.0f, rr), 0, 2 * CUI_PI, 2, col);
        }
        cui_draw_circle(ui, c, s * 0.12f, p->c);
    } break;
    case CUI_LOADER_ORBIT: {
        float rr = s * 0.36f;
        cui_draw_arc(ui, c, rr, 0, 2 * CUI_PI, 1.5f, cui_alpha(p->c, 0.15f));
        float a0 = t * 2 * CUI_PI * 0.9f;
        for (int k = 0; k < 7; k++) {
            float a = a0 - k * 0.22f;
            cui_color col = p->c; col.a = (uint8_t)(col.a * (1 - k / 7.0f));
            cui_draw_circle(ui, (cui_vec2){ c.x + cosf(a) * rr, c.y + sinf(a) * rr }, s * 0.075f * (1 - k * 0.08f), col);
        }
    } break;
    case CUI_LOADER_GRID: {
        float cell = s * 0.24f, gap = s * 0.1f, x0 = c.x - (3 * cell + 2 * gap) * 0.5f, y0 = c.y - (3 * cell + 2 * gap) * 0.5f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                float ph = t * 2 * CUI_PI - (i + j) * 0.55f;
                float k = 0.5f + 0.5f * sinf(ph);
                float sz = cell * (0.55f + 0.45f * k);
                cui_color col = p->c; col.a = (uint8_t)(col.a * (0.3f + 0.7f * k));
                cui_draw_rect(ui, (cui_rect){ x0 + j * (cell + gap) + (cell - sz) * 0.5f, y0 + i * (cell + gap) + (cell - sz) * 0.5f, sz, sz }, CUI_CORNERS(sz * 0.25f), col);
            }
    } break;
    }
}

void cui_loader_ex(cui_ctx *ui, const cui_loader_opts *o) {
    cui_loader_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float s = o->size > 0 ? o->size : 24;
    float h = o->style == CUI_LOADER_DOTS ? s * 0.5f : s;
    int n = cui__leaf(ui, 0, CUI_PX(s), CUI_PX(h));
    cui__ld_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->style = o->style;
    p->t = (float)fmod(ui->time, 1000.0);
    p->c = cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, TEXT_SECONDARY);
    ui->nodes[n].paint = cui__paint_loader;
    ui->nodes[n].paint_data = p;
    ui->nodes[n].self_align = CUI_ALIGN_CENTER;
    ui->animating = true;
}

/* ------------------------------------------------------------------------ */
/* Dock                                                                     */
/* ------------------------------------------------------------------------ */

struct cui_dock_build { cui_id id; float size, mag, gap; bool hot; int k, seps; struct cui_dock_build *prev; };
typedef struct { int items, seps; } cui__dock_count;

typedef struct { cui_spring s, press; float hover; } cui__dock_item_state;

void cui_dock_begin(cui_ctx *ui, const char *id_str, const cui_dock_opts *o) {
    cui_dock_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    struct cui_dock_build *b = cui__frame_alloc(ui, sizeof *b);
    memset(b, 0, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    b->size = o->size > 0 ? o->size : 46;
    b->mag = o->magnify > 0 ? o->magnify : 1.55f;
    /* on a narrow screen the icons get smaller so the whole dock stays on it */
    cui__dock_count *dc = cui_state(ui, b->id ^ 0xD0C5u, sizeof *dc);
    b->gap = 8;
    if (dc->items > 0) {
        /* viewport less a page margin and the dock's own padding; tighter gaps on phones */
        float gaps = (float)(dc->items + dc->seps - 1), room = ui->vw - 32 - 16 - (float)dc->seps;
        float fit = floorf((room - 8.0f * gaps) / (float)dc->items);
        if (fit < 36) { b->gap = 4; fit = floorf((room - 4.0f * gaps) / (float)dc->items); }
        /* squeezed onto a narrow screen there is no room to magnify (and no hover on a phone) */
        if (fit < b->size) { b->size = CUI_MAX(24.0f, fit); b->mag = 1; }
    }
    b->prev = (struct cui_dock_build *)ui->dock;
    ui->dock = b;
    cui_rect pr = cui__prev_rect(ui, b->id);
    float reach = b->size * (b->mag - 1) + 16;
    b->hot = pr.w > 0 && ui->mx >= pr.x - 8 && ui->mx < pr.x + pr.w + 8 && ui->my >= pr.y - reach && ui->my < pr.y + pr.h + 4 && ui->mouse_inside;
    int n = cui__node_new(ui, b->id);
    cui_node *nd = &ui->nodes[n];
    nd->dir = CUI_ROW; nd->align = CUI_ALIGN_END; nd->gap = b->gap;
    nd->pad[0] = nd->pad[1] = nd->pad[3] = 8; nd->pad[2] = 12;
    nd->radius = CUI_CORNERS(b->size * 0.5f + 4);
    nd->bg = cui_alpha(CUI_C(ui, SURFACE_RAISED), ui->mode == CUI_THEME_DARK ? 0.9f : 0.82f);
    nd->border = 1; nd->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui__rgba(0, 0, 0, 20);
    nd->elevation = CUI_ELEVATION_FLOATING; nd->elevation_alpha = 1;
    nd->self_align = CUI_ALIGN_CENTER;
    nd->h = CUI_PX(b->size + 20);
    cui__open(ui, n);
    cui_push_id(ui, id_str);
}

typedef struct { const char *icon; cui_color bg, fg; bool running; int badge; float hover; } cui__dock_paint;

static void cui__paint_dock_item(cui_ctx *ui, cui_node *n, void *data) {
    cui__dock_paint *p = data;
    cui_rect r = n->rect;
    float rad = r.w * 0.26f;
    cui_draw_rect(ui, r, CUI_CORNERS(rad), p->bg);
    cui__draw_sheen(ui, r, CUI_CORNERS(rad), 0.9f);
    if (p->hover > 0.01f) cui_draw_rect(ui, r, CUI_CORNERS(rad), cui_alpha(cui__rgba(255, 255, 255, 255), 0.08f * p->hover));
    float is = r.w * 0.48f;
    cui_draw_icon(ui, p->icon, r, is, p->fg);
    if (p->running) cui_draw_circle(ui, (cui_vec2){ r.x + r.w * 0.5f, r.y + r.h + 6 }, 2, CUI_C(ui, TEXT_SECONDARY));
    if (p->badge > 0) {
        const char *s = p->badge > 99 ? "99+" : cui__frame_printf(ui, "%d", p->badge);
        float fs = cui__fs(ui, 11), w = CUI_MAX(18.0f, cui__text_width(ui, CUI_FONT_MEDIUM, fs, s, -1, true) + 10);
        cui_rect br = { r.x + r.w - w * 0.6f, r.y - 5, w, 18 };
        cui_draw_rect_border(ui, br, CUI_CORNERS(9), CUI_C(ui, DANGER), CUI_C(ui, SURFACE_RAISED), 1.5f);
        float fh = cui__font_line_height(ui, CUI_FONT_MEDIUM, fs);
        cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs, br.x + (w - cui__text_width(ui, CUI_FONT_MEDIUM, fs, s, -1, true)) * 0.5f, br.y + (18 - fh) * 0.5f,
                            cui__rgba(255, 255, 255, 255), s, -1, true);
    }
}

bool cui_dock_item_ex(cui_ctx *ui, const char *icon, const char *label, const cui_dock_item_opts *o) {
    cui_dock_item_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    struct cui_dock_build *b = (struct cui_dock_build *)ui->dock;
    if (!b) return false;
    int k = b->k++;
    cui_id iid = cui__hash(&k, sizeof k, b->id ^ 0xD0Cu);
    cui_interact r = cui__interact(ui, iid, CUI_IF_FOCUSABLE);
    cui_rect pr = cui__prev_rect(ui, iid);
    cui__dock_item_state *st = cui_state(ui, iid, sizeof *st);
    float target = 1;
    if (b->hot && pr.w > 0) {
        float dist = (ui->mx - (pr.x + pr.w * 0.5f)) / (b->size * 2.4f);
        if (fabsf(dist) < 1) target = 1 + (b->mag - 1) * (0.5f + 0.5f * cosf(dist * CUI_PI));
    }
    if (st->s.x <= 0 || b->mag <= 1) { st->s.x = 1; st->s.v = 0; }
    cui__spring(ui, &st->s, target, 0.28f, 0.0f);
    cui__spring(ui, &st->press, r.pressed ? 0.9f : 1.0f, CUI_SPRING_SNAPPY);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    float sz = b->size * st->s.x;
    int n = cui__leaf(ui, iid, CUI_PX(sz), CUI_PX(sz));
    cui_node *nd = &ui->nodes[n];
    nd->scale = st->press.x;
    cui__dock_paint *p = cui__frame_alloc(ui, sizeof *p);
    if (cui__color_set(o->color)) { p->bg = cui_color_of(ui, o->color); p->fg = cui__rgba(255, 255, 255, 255); }
    else { p->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE_RAISED), CUI_C(ui, FOREGROUND), 0.1f) : CUI_C(ui, SURFACE_MUTED); p->fg = CUI_C(ui, FOREGROUND); }
    p->icon = icon; p->running = o->running; p->badge = o->badge; p->hover = st->hover;
    nd->paint = cui__paint_dock_item;
    nd->paint_data = p;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = iid;
    if (label) cui_tooltip(ui, label);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    if (b->hot || fabsf(st->s.x - 1) > 0.001f) ui->animating = true;
    return r.clicked || r.keyboard_activate;
}

void cui_dock_separator(cui_ctx *ui) {
    struct cui_dock_build *b = (struct cui_dock_build *)ui->dock;
    float h = b ? b->size * 0.7f : 30;
    int n = cui__leaf(ui, 0, CUI_PX(1), CUI_PX(h));
    ui->nodes[n].bg = CUI_C(ui, BORDER);
    ui->nodes[n].self_align = CUI_ALIGN_CENTER;
    if (b) b->seps++;
}

void cui_dock_end(cui_ctx *ui) {
    struct cui_dock_build *b = (struct cui_dock_build *)ui->dock;
    if (!b) return;
    cui_pop_id(ui);
    cui__close(ui);
    cui__dock_count *dc = cui_state(ui, b->id ^ 0xD0C5u, sizeof *dc);
    dc->items = b->k; dc->seps = b->seps;
    ui->dock = b->prev;
    ui->last_id = b->id;
}

/* ------------------------------------------------------------------------ */
/* Tab bar                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring x, w; bool init; } cui__tb_state;
typedef struct { float x, w; cui_color c; } cui__tb_paint;

static void cui__paint_tab_bar(cui_ctx *ui, cui_node *n, void *data) {
    cui__tb_paint *p = data;
    if (p->w <= 0) return;
    cui_rect r = n->rect;
    cui_draw_rect(ui, (cui_rect){ p->x, r.y + 6, p->w, r.h - 12 }, CUI_CORNERS((r.h - 12) * 0.5f), p->c);
}

bool cui_tab_bar_ex(cui_ctx *ui, const char *id_str, const cui_tab_bar_item *items, int count, int *selected, const cui_tab_bar_opts *o) {
    cui_tab_bar_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__tb_state *st = cui_state(ui, id, sizeof *st);
    int sel = selected ? *selected : 0;
    bool changed = false;
    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->dir = CUI_ROW; nd->pad[0] = nd->pad[1] = nd->pad[2] = nd->pad[3] = 6; nd->gap = 4;
    nd->w = o->width.kind ? o->width : CUI_FILL;
    nd->h = CUI_PX(64);
    nd->radius = CUI_CORNERS(ui->theme.radius_surface);
    nd->bg = CUI_C(ui, SURFACE_RAISED);
    nd->border = 1; nd->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui__rgba(0, 0, 0, 18);
    nd->elevation = CUI_ELEVATION_RAISED; nd->elevation_alpha = 1;
    cui__tb_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->c = ui->accent == CUI_ACCENT_NEUTRAL ? cui_alpha(CUI_C(ui, FOREGROUND), ui->mode == CUI_THEME_DARK ? 0.12f : 0.07f) : CUI_C(ui, ACCENT_SUBTLE);
    p->w = 0;
    nd->paint = cui__paint_tab_bar;
    nd->paint_data = p;
    cui__open(ui, n);
    cui_push_id(ui, id_str);
    for (int k = 0; k < count; k++) {
        cui_id iid = cui__hash(&k, sizeof k, id ^ 0x7AB);
        cui_interact r = cui__interact(ui, iid, CUI_IF_FOCUSABLE);
        if ((r.clicked || r.keyboard_activate) && sel != k) { sel = k; changed = true; }
        if (r.focused) {
            if (cui_key_pressed(ui, CUI_KEY_RIGHT) && k + 1 < count) { sel = k + 1; changed = true; ui->focus = cui__hash(&sel, sizeof sel, id ^ 0x7AB); }
            if (cui_key_pressed(ui, CUI_KEY_LEFT) && k > 0) { sel = k - 1; changed = true; ui->focus = cui__hash(&sel, sizeof sel, id ^ 0x7AB); }
        }
        bool on = k == sel;
        float *hv = cui_state(ui, iid, sizeof(float) * 2);
        cui__tween(ui, &hv[0], on ? 1.0f : 0.0f, CUI__T_HOVER);
        cui__tween(ui, &hv[1], r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        int it = cui__node_new(ui, iid);
        cui_node *in = &ui->nodes[it];
        in->w = CUI_GROW(1); in->h = CUI_FILL;
        in->dir = CUI_COLUMN; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER; in->gap = 3;
        in->radius = CUI_CORNERS(ui->theme.radius_surface - 6);
        cui__mark_interactive(ui, it, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, it);
        cui_color fg = cui_mix(cui_mix(CUI_C(ui, TEXT_MUTED), CUI_C(ui, TEXT_SECONDARY), hv[1]), cui__mo_accent(ui), hv[0]);
        int ic = cui__node_new(ui, 0);
        ui->nodes[ic].dir = CUI_STACK; ui->nodes[ic].w = CUI_PX(22); ui->nodes[ic].h = CUI_PX(22);
        ui->nodes[ic].align = CUI_ALIGN_CENTER; ui->nodes[ic].justify = CUI_JUSTIFY_CENTER;
        ui->nodes[ic].scale = 1.0f + 0.06f * hv[0];
        cui__open(ui, ic);
        cui__icon_node(ui, items[k].icon ? items[k].icon : CUI_ICON_CIRCLE, 20, fg);
        if (items[k].badge > 0) {
            const char *bs = items[k].badge > 9 ? "9+" : cui__frame_printf(ui, "%d", items[k].badge);
            int bd = cui__absolute(ui, 0, 13, -4, 16, 16);
            cui_node *bn = &ui->nodes[bd];
            bn->radius = CUI_CORNERS(8); bn->bg = CUI_C(ui, DANGER); bn->border = 1.5f; bn->border_color = CUI_C(ui, SURFACE_RAISED);
            bn->dir = CUI_STACK; bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
            cui__open(ui, bd);
            int bt = cui__text_node(ui, bs, -1, CUI_FONT_MEDIUM, cui__fs(ui, 10), cui__rgba(255, 255, 255, 255), false);
            ui->nodes[bt].line_height = 1.0f;
            cui__close(ui);
        }
        cui__close(ui);
        if (items[k].label && (!o->labels_on_selected_only || on)) {
            int t = cui__text_node(ui, items[k].label, -1, on ? CUI_FONT_MEDIUM : CUI_FONT_BODY, cui__fs(ui, 11), fg, false);
            ui->nodes[t].line_height = 1.2f;
        }
        cui__close(ui);
        cui__focus_ring(ui, it, r.focus_visible ? 1.0f : 0.0f);
        if (on) {
            cui_rect ir = cui__prev_rect(ui, iid);
            if (ir.w > 0) {
                if (!st->init) { st->x.x = ir.x; st->w.x = ir.w; st->init = true; }
                cui__spring(ui, &st->x, ir.x, CUI_SPRING_SMOOTH);
                cui__spring(ui, &st->w, ir.w, CUI_SPRING_SMOOTH);
                p->x = st->x.x; p->w = st->w.x;
            }
        }
    }
    cui_pop_id(ui);
    cui__close(ui);
    if (selected) *selected = sel;
    ui->last_id = id;
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Control tile                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { float on, hover; cui_spring press; bool init; } cui__tile_state;

bool cui_control_tile_ex(cui_ctx *ui, const char *label, bool *on, const cui_tile_opts *o) {
    cui_tile_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, label);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    bool changed = false;
    if ((r.clicked || r.keyboard_activate) && on) { *on = !*on; changed = true; }
    bool v = on && *on;
    cui__tile_state *st = cui_state(ui, id, sizeof *st);
    if (!st->init) { st->on = v ? 1.0f : 0.0f; st->press.x = 1; st->init = true; }
    cui__tween(ui, &st->on, v ? 1.0f : 0.0f, CUI__T_STATE);
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    cui__spring(ui, &st->press, r.pressed ? CUI__PRESS : 1.0f, CUI_SPRING_SNAPPY);
    bool dark = ui->mode == CUI_THEME_DARK;
    cui_color off_bg = dark ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.08f + 0.04f * st->hover) : cui_mix(CUI_C(ui, SURFACE_MUTED), CUI_C(ui, FOREGROUND), 0.035f * st->hover);
    cui_color on_bg = cui__color_set(o->color) ? cui_color_of(ui, o->color) : (ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_ON) : CUI_C(ui, ACCENT));
    cui_color on_fg = cui__color_set(o->color) ? cui__rgba(255, 255, 255, 255) : (ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, ACCENT_FOREGROUND));
    /* a neutral tile in dark mode lights its icon, not the whole tile: a white slab glares on a dark page */
    bool lamp = dark && ui->accent == CUI_ACCENT_NEUTRAL && !cui__color_set(o->color);
    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->dir = o->wide ? CUI_ROW : CUI_COLUMN;
    nd->align = o->wide ? CUI_ALIGN_CENTER : CUI_ALIGN_START;
    nd->gap = o->wide ? 12 : 14;
    nd->pad[0] = nd->pad[1] = nd->pad[2] = nd->pad[3] = 14;
    nd->w = o->width.kind ? o->width : CUI_FILL;
    nd->min_h = o->wide ? 64 : 112;
    nd->radius = CUI_CORNERS(ui->theme.radius_surface);
    nd->bg = lamp ? cui_mix(off_bg, cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.15f + 0.03f * st->hover), st->on) : cui_mix(off_bg, on_bg, st->on);
    /* off tiles in light mode keep a hairline so they do not melt into a white page */
    nd->border = 1;
    nd->border_color = cui_alpha(dark ? cui__rgba(255, 255, 255, 255) : CUI_C(ui, BORDER), dark ? 0.0f : (1.0f - st->on));
    if (st->on > 0.01f && !lamp) {
        float *k = cui__frame_alloc(ui, sizeof *k);
        *k = 0.45f * st->on;
        nd->paint = cui__paint_sheen;
        nd->paint_data = k;
    }
    nd->scale = st->press.x;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, n);
    int ic = cui__node_new(ui, 0);
    cui_node *icn = &ui->nodes[ic];
    icn->w = CUI_PX(36); icn->h = CUI_PX(36); icn->radius = CUI_CORNERS(18);
    icn->dir = CUI_STACK; icn->align = CUI_ALIGN_CENTER; icn->justify = CUI_JUSTIFY_CENTER;
    cui_color icon_off = dark ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.14f) : CUI_C(ui, SURFACE);
    icn->bg = cui_mix(icon_off, lamp ? CUI_C(ui, CONTROL_ON) : cui_alpha(on_fg, 0.2f), st->on);
    cui__open(ui, ic);
    cui__icon_node(ui, o->icon ? o->icon : CUI_ICON_ZAP, 18, cui_mix(CUI_C(ui, FOREGROUND), lamp ? CUI_C(ui, CONTROL_GLYPH) : on_fg, st->on));
    if (lamp) on_fg = CUI_C(ui, FOREGROUND);   /* the label keeps its usual ink */
    cui__close(ui);
    int col = cui__node_new(ui, 0);
    ui->nodes[col].gap = 1;
    cui__open(ui, col);
    int t = cui__text_node(ui, label, cui__display_len(label), CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), cui_mix(CUI_C(ui, FOREGROUND), on_fg, st->on), false);
    ui->nodes[t].line_height = 1.3f;
    const char *det = o->detail ? o->detail : (v ? "On" : "Off");
    int dt = cui__text_node(ui, det, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), cui_mix(CUI_C(ui, TEXT_SECONDARY), cui_alpha(on_fg, 0.75f), st->on), false);
    ui->nodes[dt].line_height = 1.3f;
    cui__close(ui);
    cui__close(ui);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = id;
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Waveform                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct { float *peaks; int count; float progress; float hover_x; float bar, gap; cui_color played, rest, ghost; float intro; bool has_progress; } cui__wf2_paint;
typedef struct { cui_spring intro; float hover; bool dragging; } cui__wf2_state;

static void cui__paint_waveform(cui_ctx *ui, cui_node *n, void *data) {
    cui__wf2_paint *p = data;
    cui_rect r = n->rect;
    int nb = (int)floorf((r.w + p->gap) / (p->bar + p->gap));
    if (nb <= 0 || p->count <= 0) return;
    float used = nb * (p->bar + p->gap) - p->gap, x0 = r.x + (r.w - used) * 0.5f;
    for (int k = 0; k < nb; k++) {
        int a = k * p->count / nb, b = CUI_MAX(a + 1, (k + 1) * p->count / nb);
        float v = 0;
        for (int i = a; i < b && i < p->count; i++) v = CUI_MAX(v, p->peaks[i]);
        float e = cui__clampf(p->intro * 1.8f - (float)k / nb * 0.8f, 0, 1);
        float h = CUI_MAX(p->bar, r.h * cui__clampf(v, 0, 1) * e);
        float x = x0 + k * (p->bar + p->gap);
        float mid = x + p->bar * 0.5f;
        cui_color c = p->rest;
        if (p->has_progress && mid <= r.x + r.w * p->progress) c = p->played;
        else if (p->hover_x >= 0 && mid <= p->hover_x) c = p->ghost;
        cui_draw_rect(ui, (cui_rect){ x, r.y + (r.h - h) * 0.5f, p->bar, h }, CUI_CORNERS(p->bar * 0.5f), c);
    }
}

bool cui_waveform_ex(cui_ctx *ui, const char *id_str, const float *peaks, int count, float *progress, const cui_waveform_opts *o) {
    cui_waveform_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    count = CUI_MAX(0, count);
    cui_id id = cui_make_id(ui, id_str);
    cui_interact r = cui__interact(ui, id, progress ? (CUI_IF_DRAG | CUI_IF_FOCUSABLE) : 0);
    cui_rect pr = cui__prev_rect(ui, id);
    cui__wf2_state *st = cui_state(ui, id, sizeof *st);
    cui__spring(ui, &st->intro, 1.0f, 0.8f, 0.0f);
    bool changed = false;
    if (progress && pr.w > 0) {
        if (r.hovered && ui->mpressed[0]) st->dragging = true;
        if (!ui->mdown[0]) st->dragging = false;
        if (st->dragging) {
            float v = cui__clampf((ui->mx - pr.x) / pr.w, 0, 1);
            if (v != *progress) { *progress = v; changed = true; }
            ui->active = id;
        }
        if (r.focused) {
            float step = 0.05f;
            if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { *progress = cui__clampf(*progress + step, 0, 1); changed = true; }
            if (cui_key_pressed(ui, CUI_KEY_LEFT)) { *progress = cui__clampf(*progress - step, 0, 1); changed = true; }
        }
        if (r.hovered || st->dragging) cui__set_cursor(ui, CUI_CURSOR_HAND);
    }
    float h = o->height > 0 ? o->height : 40;
    int n = cui__leaf(ui, id, o->width.kind ? o->width : CUI_FILL, CUI_PX(h));
    cui__wf2_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->peaks = cui__frame_alloc(ui, sizeof(float) * (size_t)CUI_MAX(1, count));
    if (count) memcpy(p->peaks, peaks, sizeof(float) * (size_t)count);
    p->count = count; p->bar = o->bar > 0 ? o->bar : 3; p->gap = o->gap > 0 ? o->gap : 2;
    p->has_progress = progress != NULL; p->progress = progress ? cui__clampf(*progress, 0, 1) : 0;
    p->hover_x = (progress && r.hovered && !st->dragging) ? ui->mx : -1;
    p->played = cui__color_set(o->color) ? cui_color_of(ui, o->color) : cui__mo_accent(ui);
    p->rest = progress ? CUI_C(ui, BORDER_STRONG) : (cui__color_set(o->color) ? cui_color_of(ui, o->color) : CUI_C(ui, TEXT_MUTED));
    p->ghost = cui_mix(p->rest, p->played, 0.45f);
    p->intro = st->intro.x;
    ui->nodes[n].paint = cui__paint_waveform;
    ui->nodes[n].paint_data = p;
    if (progress) {
        cui__mark_interactive(ui, n, CUI_IF_DRAG | CUI_IF_FOCUSABLE);
        cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    }
    ui->last_id = id;
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Now playing                                                              */
/* ------------------------------------------------------------------------ */

static const char *cui__mmss(cui_ctx *ui, float s) {
    if (!(s >= 0)) s = 0;
    int t = (int)s;
    return cui__frame_printf(ui, "%d:%02d", t / 60, t % 60);
}

typedef struct { float frac, hover; } cui__scrub_paint;
static void cui__paint_scrub(cui_ctx *ui, cui_node *n, void *data) {
    cui__scrub_paint *p = data;
    cui_rect r = n->rect;
    float th = 4 + 2 * p->hover, y = r.y + (r.h - th) * 0.5f;
    cui_draw_rect(ui, (cui_rect){ r.x, y, r.w, th }, CUI_CORNERS(th * 0.5f), CUI_C(ui, CONTROL_TRACK));
    cui_draw_rect(ui, (cui_rect){ r.x, y, CUI_MAX(th, r.w * p->frac), th }, CUI_CORNERS(th * 0.5f), CUI_C(ui, FOREGROUND));
    if (p->hover > 0.01f) {
        cui_vec2 c = { r.x + r.w * p->frac, r.y + r.h * 0.5f };
        cui_draw_circle(ui, c, 6.5f * p->hover, CUI_C(ui, FOREGROUND));
    }
}

typedef struct { bool dragging; float hover; float drag_pos; cui_spring press; } cui__np_state;

static void cui__paint_art_color(cui_ctx *ui, cui_node *n, void *data) {
    cui_color c = *(cui_color *)data;
    cui_rect r = n->rect;
    cui_draw_rect(ui, r, n->radius, c);   /* a flat placeholder, not a glossy tile */
    cui_draw_icon(ui, CUI_ICON_MUSIC, r, r.w * 0.4f, cui_alpha(cui__rgba(255, 255, 255, 255), 0.85f));
}

static void cui__paint_art(cui_ctx *ui, cui_node *n, void *data) {
    cui_draw_image(ui, *(cui_texture *)data, n->rect, (cui_rect){ 0, 0, 1, 1 }, n->radius, cui__rgba(255, 255, 255, 255));
}

int cui_now_playing_ex(cui_ctx *ui, const char *id_str, cui_now_playing *np, const cui_now_playing_opts *o) {
    cui_now_playing_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!np) return 0;
    int result = 0;
    cui_id id = cui_make_id(ui, id_str);
    cui_id sid = id ^ 0x5C8Bu, pid = id ^ 0x91A7u;
    cui__np_state *st = cui_state(ui, id, sizeof *st);
    /* scrubber input */
    cui_interact sr = cui__interact(ui, sid, CUI_IF_DRAG);
    cui_rect srr = cui__prev_rect(ui, sid);
    if (sr.hovered && ui->mpressed[0]) st->dragging = true;
    if (st->dragging) {
        if (ui->mdown[0] && srr.w > 0) { st->drag_pos = cui__clampf((ui->mx - srr.x) / srr.w, 0, 1) * np->duration; ui->active = sid; }
        else { st->dragging = false; np->position = st->drag_pos; result |= CUI_PLAYER_SEEK; }
    }
    cui__tween(ui, &st->hover, (sr.hovered || st->dragging) ? 1.0f : 0.0f, CUI__T_HOVER);
    if (sr.hovered || st->dragging) cui__set_cursor(ui, CUI_CURSOR_HAND);
    float pos = st->dragging ? st->drag_pos : np->position;
    float frac = np->duration > 0 ? cui__clampf(pos / np->duration, 0, 1) : 0;
    bool compact = o->compact;
    /* a phone: smaller artwork and no previous button, so the title keeps some room */
    cui_rect rpr = cui__prev_rect(ui, id);
    bool narrow = !compact && rpr.w > 0 && rpr.w < 300;
    float art = compact ? 44 : narrow ? 48 : 64;
    int root = cui__node_new(ui, id);
    cui_node *rn = &ui->nodes[root];
    rn->dir = CUI_COLUMN; rn->gap = 14;
    rn->pad[0] = rn->pad[1] = rn->pad[2] = rn->pad[3] = compact ? 10 : 16;
    cui__soft_width(rn, o->width, compact ? 340 : 360);
    if (!compact) { rn->flags |= CUI_NF_ADAPTIVE; rn->min_w = CUI_MAX(rn->min_w, 224); }
    rn->radius = CUI_CORNERS(ui->theme.radius_panel);
    rn->bg = CUI_C(ui, SURFACE);
    rn->border = 1; rn->border_color = CUI_C(ui, BORDER);
    rn->elevation = CUI_ELEVATION_RESTING; rn->elevation_alpha = 1;
    cui__open(ui, root);
    cui_push_id(ui, id_str);
    int top = cui__node_new(ui, 0);
    ui->nodes[top].dir = CUI_ROW; ui->nodes[top].gap = 12; ui->nodes[top].align = CUI_ALIGN_CENTER; ui->nodes[top].w = CUI_FILL;
    cui__open(ui, top);
    int an = cui__leaf(ui, 0, CUI_PX(art), CUI_PX(art));
    cui_node *ann = &ui->nodes[an];
    ann->radius = CUI_CORNERS(art * 0.2f);
    ann->bg = cui__color_set(np->art_color) ? cui_color_of(ui, np->art_color) : CUI_C(ui, SURFACE_MUTED);
    if (!np->art) {   /* no artwork: a lit tile in its color with a note */
        cui_color *ac = cui__frame_alloc(ui, sizeof *ac);
        *ac = ann->bg;
        ann->paint = cui__paint_art_color;
        ann->paint_data = ac;
    }
    if (np->art) {
        cui_texture *tex = cui__frame_alloc(ui, sizeof *tex);
        *tex = np->art;
        ann->paint = cui__paint_art;
        ann->paint_data = tex;
    }
    int tc = cui__node_new(ui, 0);
    ui->nodes[tc].w = CUI_FILL; ui->nodes[tc].gap = 2;
    cui__open(ui, tc);
    int tt = cui__text_node(ui, np->title ? np->title : "", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm + 0.5f), CUI_C(ui, FOREGROUND), false);
    ui->nodes[tt].w = CUI_FILL; ui->nodes[tt].line_height = 1.3f;
    int at = cui__text_node(ui, np->artist ? np->artist : "", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs + 0.5f), CUI_C(ui, TEXT_SECONDARY), false);
    ui->nodes[at].w = CUI_FILL; ui->nodes[at].line_height = 1.3f;
    cui__close(ui);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.variant = CUI_BUTTON_GHOST; bo.size = CUI_SIZE_SM;
    if (!compact && !narrow) { bo.tooltip = "Previous"; if (cui_icon_button_ex(ui, CUI_ICON_SKIP_BACK, &bo)) result |= CUI_PLAYER_PREV; }
    /* play / pause */
    cui_interact pr = cui__interact(ui, pid, CUI_IF_FOCUSABLE);
    cui__spring(ui, &st->press, pr.pressed ? 0.9f : 1.0f, CUI_SPRING_SNAPPY);
    if (pr.clicked || pr.keyboard_activate) { np->playing = !np->playing; result |= CUI_PLAYER_TOGGLE; }
    float pb = compact ? 36 : 40;
    int pn = cui__node_new(ui, pid);
    cui_node *pnn = &ui->nodes[pn];
    pnn->w = CUI_PX(pb); pnn->h = CUI_PX(pb); pnn->radius = CUI_CORNERS(pb * 0.5f);
    pnn->dir = CUI_STACK; pnn->align = CUI_ALIGN_CENTER; pnn->justify = CUI_JUSTIFY_CENTER;
    pnn->bg = cui_mix(CUI_C(ui, FOREGROUND), CUI_C(ui, BACKGROUND), pr.hovered ? 0.12f : 0.0f);
    pnn->paint = cui__paint_sheen;
    pnn->paint_data = NULL;
    pnn->elevation = CUI_ELEVATION_RESTING; pnn->elevation_alpha = 1;
    pnn->scale = st->press.x;
    cui__mark_interactive(ui, pn, CUI_IF_FOCUSABLE);
    if (pr.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, pn);
    cui__icon_swap(ui, pid ^ 0x1u, np->playing ? CUI_ICON_PAUSE : CUI_ICON_PLAY, 18, CUI_C(ui, BACKGROUND));
    cui__close(ui);
    cui__focus_ring(ui, pn, pr.focus_visible ? 1.0f : 0.0f);
    bo.tooltip = "Next";
    if (cui_icon_button_ex(ui, CUI_ICON_SKIP_FORWARD, &bo)) result |= CUI_PLAYER_NEXT;
    cui__close(ui);
    /* scrubber with times */
    int sc = cui__node_new(ui, 0);
    ui->nodes[sc].dir = CUI_ROW; ui->nodes[sc].gap = 10; ui->nodes[sc].align = CUI_ALIGN_CENTER; ui->nodes[sc].w = CUI_FILL;
    cui__open(ui, sc);
    float fx = cui__fs(ui, ui->theme.text_xs);
    int t0 = cui__text_node(ui, cui__mmss(ui, pos), -1, CUI_FONT_BODY, fx, CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t0].flags |= CUI_NF_TABULAR; ui->nodes[t0].line_height = 1.2f;
    int bar = cui__leaf(ui, sid, CUI_FILL, CUI_PX(14));
    cui__scrub_paint *sp = cui__frame_alloc(ui, sizeof *sp);
    sp->frac = frac; sp->hover = st->hover;
    ui->nodes[bar].paint = cui__paint_scrub;
    ui->nodes[bar].paint_data = sp;
    cui__mark_interactive(ui, bar, CUI_IF_DRAG);
    int t1 = cui__text_node(ui, cui__frame_printf(ui, "-%s", cui__mmss(ui, np->duration - pos)), -1, CUI_FONT_BODY, fx, CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t1].flags |= CUI_NF_TABULAR; ui->nodes[t1].line_height = 1.2f;
    cui__close(ui);
    cui_pop_id(ui);
    cui__close(ui);
    ui->last_id = id;
    return result;
}
