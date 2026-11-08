/* Context, memory, identity, state, input and the frame loop. */
#include "cui_internal.h"

#include <time.h>

/* ------------------------------------------------------------------------ */
/* Memory                                                                   */
/* ------------------------------------------------------------------------ */

static void *cui__default_alloc(size_t size, void *user) { (void)user; return malloc(size); }
static void *cui__default_realloc(void *p, size_t size, void *user) { (void)user; return realloc(p, size); }
static void cui__default_free(void *p, void *user) { (void)user; free(p); }

void *cui__alloc(cui_ctx *ui, size_t size) {
    void *p = ui->alloc.alloc(size, ui->alloc.user);
    if (!p && size) { fprintf(stderr, "cui: out of memory\n"); abort(); }
    return p;
}
void *cui__realloc(cui_ctx *ui, void *p, size_t size) {
    void *np = ui->alloc.realloc(p, size, ui->alloc.user);
    if (!np && size) { fprintf(stderr, "cui: out of memory\n"); abort(); }
    return np;
}
void cui__free(cui_ctx *ui, void *p) { if (p) ui->alloc.free(p, ui->alloc.user); }

/* Frame arena: a main block that grows between frames, plus overflow blocks
 * for the current frame so pointers handed out stay valid. */
void *cui__frame_alloc(cui_ctx *ui, size_t size) {
    size = (size + 15) & ~(size_t)15;
    if (ui->arena_used + size <= ui->arena_cap) {
        void *p = ui->arena + ui->arena_used;
        ui->arena_used += size;
        memset(p, 0, size);
        return p;
    }
    /* overflow block, freed at next frame; arena grows for next frame */
    struct cui_arena_block *b = cui__alloc(ui, 32 + size);
    b->next = ui->arena_overflow;
    ui->arena_overflow = b;
    ui->arena_used += size;  /* remember the demand so the arena grows */
    void *p = (void *)(((uintptr_t)b + sizeof(*b) + 15) & ~(uintptr_t)15);
    memset(p, 0, size);
    return p;
}

/* Per-frame buffers grow to the biggest frame. A page that needed a lot (a long
 * table, a big chart) would otherwise keep them big after it is gone: every 600
 * frames, a buffer more than four times what any of those frames used shrinks
 * to twice that. Done at the start of a frame, when last frame's draw data has
 * been rendered and nothing points into these buffers. */
#define CUI__TRIM_FRAMES 600
static void *cui__trim(cui_ctx *ui, void *p, int *cap, int peak, size_t elem) {
    int want = CUI_MAX(peak * 2, (int)(16384 / elem));
    if (*cap <= want * 2 || (size_t)*cap * elem < 65536) return p;
    void *np = cui__realloc(ui, p, (size_t)want * elem);
    *cap = want;
    return np;
}
static void cui__trim_buffers(cui_ctx *ui) {
    ui->peak_vtx = CUI_MAX(ui->peak_vtx, ui->dl.vtx_count);
    ui->peak_idx = CUI_MAX(ui->peak_idx, ui->dl.idx_count);
    ui->peak_cmd = CUI_MAX(ui->peak_cmd, ui->dl.cmd_count);
    ui->peak_nodes = CUI_MAX(ui->peak_nodes, ui->node_count);
    ui->peak_arena = CUI_MAX(ui->peak_arena, ui->arena_used);
    if (ui->frame - ui->peak_since < CUI__TRIM_FRAMES) return;
    ui->dl.vtx = cui__trim(ui, ui->dl.vtx, &ui->dl.vtx_cap, ui->peak_vtx, sizeof *ui->dl.vtx);
    ui->dl.idx = cui__trim(ui, ui->dl.idx, &ui->dl.idx_cap, ui->peak_idx, sizeof *ui->dl.idx);
    ui->dl.cmd = cui__trim(ui, ui->dl.cmd, &ui->dl.cmd_cap, ui->peak_cmd, sizeof *ui->dl.cmd);
    ui->nodes = cui__trim(ui, ui->nodes, &ui->node_cap, ui->peak_nodes, sizeof *ui->nodes);
    if (ui->arena_cap > 65536 && ui->arena_cap > ui->peak_arena * 4 && !ui->arena_overflow) {
        size_t cap = 1 << 16;
        while (cap < ui->peak_arena * 2) cap *= 2;
        cui__free(ui, ui->arena);
        ui->arena = cui__alloc(ui, cap);
        ui->arena_cap = cap;
    }
    ui->peak_vtx = ui->peak_idx = ui->peak_cmd = ui->peak_nodes = 0;
    ui->peak_arena = 0;
    ui->peak_since = ui->frame;
}

static void cui__frame_arena_reset(cui_ctx *ui) {
    size_t demand = ui->arena_used;
    while (ui->arena_overflow) {
        struct cui_arena_block *n = ui->arena_overflow->next;
        cui__free(ui, ui->arena_overflow);
        ui->arena_overflow = n;
    }
    if (demand > ui->arena_cap) {
        size_t cap = ui->arena_cap ? ui->arena_cap : 1 << 16;
        while (cap < demand) cap *= 2;
        cui__free(ui, ui->arena);
        ui->arena = cui__alloc(ui, cap);
        ui->arena_cap = cap;
    }
    ui->arena_used = 0;
}

char *cui__frame_strdup(cui_ctx *ui, const char *s, int len) {
    if (!s) return NULL;
    if (len < 0) len = (int)strlen(s);
    char *d = cui__frame_alloc(ui, (size_t)len + 1);
    memcpy(d, s, (size_t)len);
    d[len] = 0;
    return d;
}

char *cui__frame_vprintf(cui_ctx *ui, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) n = 0;
    char *d = cui__frame_alloc(ui, (size_t)n + 1);
    vsnprintf(d, (size_t)n + 1, fmt, ap);
    return d;
}

char *cui__frame_printf(cui_ctx *ui, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = cui__frame_vprintf(ui, fmt, ap);
    va_end(ap);
    return s;
}

/* ------------------------------------------------------------------------ */
/* UTF-8                                                                    */
/* ------------------------------------------------------------------------ */

int cui__utf8_decode(const char *s, int len, uint32_t *cp) {
    const unsigned char *u = (const unsigned char *)s;
    if (len <= 0) { *cp = 0; return 1; }
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    if ((u[0] & 0xE0) == 0xC0 && len >= 2 && (u[1] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0 && len >= 3 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        return 3;
    }
    if ((u[0] & 0xF8) == 0xF0 && len >= 4 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
        *cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) |
              ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

int cui__utf8_encode(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int cui__utf8_prev(const char *s, int pos) {
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

int cui__utf8_next(const char *s, int len, int pos) {
    if (pos >= len) return len;
    pos++;
    while (pos < len && ((unsigned char)s[pos] & 0xC0) == 0x80) pos++;
    return pos;
}

int cui__display_len(const char *label) {
    if (!label) return 0;
    const char *p = strstr(label, "##");
    return p ? (int)(p - label) : (int)strlen(label);
}

/* ------------------------------------------------------------------------ */
/* Hashing (FNV-1a with a final avalanche)                                  */
/* ------------------------------------------------------------------------ */

uint32_t cui__hash(const void *data, size_t len, uint32_t seed) {
    const unsigned char *p = data;
    uint32_t h = 2166136261u ^ seed;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    h ^= h >> 16; h *= 0x7feb352d; h ^= h >> 15; h *= 0x846ca68b; h ^= h >> 16;
    return h ? h : 1;
}

uint32_t cui__hash_str(const char *s, uint32_t seed) {
    return cui__hash(s, s ? strlen(s) : 0, seed);
}

/* ------------------------------------------------------------------------ */
/* Context                                                                  */
/* ------------------------------------------------------------------------ */

cui_ctx *cui_create(const cui_ctx_desc *desc) {
    cui_ctx_desc d;
    memset(&d, 0, sizeof d);
    if (desc) d = *desc;
    cui_allocator a = d.allocator;
    if (!a.alloc) { a.alloc = cui__default_alloc; a.realloc = cui__default_realloc; a.free = cui__default_free; a.user = NULL; }
    cui_ctx *ui = a.alloc(sizeof(cui_ctx), a.user);
    if (!ui) return NULL;
    memset(ui, 0, sizeof *ui);
    ui->alloc = a;
    ui->renderer = d.renderer;
    ui->vw = 800; ui->vh = 600; ui->scale = 1;
    ui->bucket_count = 1024;
    ui->state_buckets = cui__alloc(ui, sizeof(int) * (size_t)ui->bucket_count);
    for (int i = 0; i < ui->bucket_count; i++) ui->state_buckets[i] = -1;
    ui->state_free = -1;
    ui->window_focused = true;
    ui->wakeup = -1;
    ui->settle_frames = 2;
    ui->toast_next_handle = 1;
    cui__theme_init(ui, d.theme, d.accent);
    cui__fonts_init(ui, !d.no_default_fonts);
    return ui;
}

void cui_destroy(cui_ctx *ui) {
    if (!ui) return;
    cui__emoji_shutdown(ui);
    cui__fonts_shutdown(ui);
    for (int i = 0; i < ui->state_count; i++) {
        if (ui->states[i].dtor && ui->states[i].data) ui->states[i].dtor(ui, ui->states[i].data);
        cui__free(ui, ui->states[i].data);
    }
    cui__free(ui, ui->states);
    cui__free(ui, ui->drop_buf);
    cui__free(ui, ui->state_buckets);
    cui__free(ui, ui->nodes);
    cui__free(ui, ui->float_roots);
    cui__free(ui, ui->hits);
    cui__free(ui, ui->prev_hits);
    cui__free(ui, ui->focus_order);
    cui__free(ui, ui->prev_focus_order);
    cui__free(ui, ui->focus_layers);
    cui__free(ui, ui->prev_focus_layers);
    cui__free(ui, ui->dl.vtx);
    cui__free(ui, ui->dl.idx);
    cui__free(ui, ui->dl.cmd);
    ui->arena_used = 0;
    cui__frame_arena_reset(ui);
    cui__free(ui, ui->arena);
    cui_allocator a = ui->alloc;
    a.free(ui, a.user);
}

void cui_set_renderer(cui_ctx *ui, const cui_renderer_iface *r) {
    cui__emoji_drop_texture(ui);   /* made by the old renderer */
    if (ui->atlas_tex && ui->renderer.texture_destroy) ui->renderer.texture_destroy(ui->renderer.user, ui->atlas_tex);
    ui->atlas_tex = 0;
    if (r) ui->renderer = *r; else memset(&ui->renderer, 0, sizeof ui->renderer);
    ui->atlas_dirty = true;
    ui->dirty_x0 = 0; ui->dirty_y0 = 0; ui->dirty_x1 = ui->atlas_w; ui->dirty_y1 = ui->atlas_h;
}

void cui_set_platform(cui_ctx *ui, const cui_platform_iface *p) {
    if (p) ui->platform = *p; else memset(&ui->platform, 0, sizeof ui->platform);
}

void cui_set_viewport(cui_ctx *ui, float w, float h, float scale) {
    if (scale <= 0) scale = 1;
    if (w != ui->vw || h != ui->vh || scale != ui->scale) ui->settle_frames = CUI_MAX(ui->settle_frames, 2);
    ui->vw = w; ui->vh = h; ui->scale = scale;
}
cui_vec2 cui_viewport_size(cui_ctx *ui) { cui_vec2 v = { ui->vw, ui->vh }; return v; }
float cui_dpi_scale(cui_ctx *ui) { return ui->scale; }
double cui_time(cui_ctx *ui) { return ui->time; }
float cui_delta_time(cui_ctx *ui) { return ui->dt; }
uint64_t cui_frame_count(cui_ctx *ui) { return ui->frame; }
void cui_request_redraw(cui_ctx *ui) { ui->animating = true; }
bool cui_needs_redraw(cui_ctx *ui) { return ui->animating || ui->settle_frames > 0; }
float cui_next_wakeup(cui_ctx *ui) { return ui->wakeup; }
cui_cursor cui_get_cursor(cui_ctx *ui) { return ui->cursor; }

static void cui__wake_in(cui_ctx *ui, float seconds) {
    if (seconds < 0) seconds = 0;
    if (ui->wakeup < 0 || seconds < ui->wakeup) ui->wakeup = seconds;
}

/* ------------------------------------------------------------------------ */
/* State store                                                              */
/* ------------------------------------------------------------------------ */

static void cui__state_rehash(cui_ctx *ui, int nb) {
    cui__free(ui, ui->state_buckets);
    ui->bucket_count = nb;
    ui->state_buckets = cui__alloc(ui, sizeof(int) * (size_t)nb);
    for (int i = 0; i < nb; i++) ui->state_buckets[i] = -1;
    for (int i = 0; i < ui->state_count; i++) {
        cui_state_entry *e = &ui->states[i];
        if (!e->id) continue;
        int b = (int)(e->id & (uint32_t)(nb - 1));
        e->next = ui->state_buckets[b];
        ui->state_buckets[b] = i;
    }
}

static cui_state_entry *cui__state_find(cui_ctx *ui, cui_id id) {
    int b = (int)(id & (uint32_t)(ui->bucket_count - 1));
    for (int i = ui->state_buckets[b]; i >= 0; i = ui->states[i].next)
        if (ui->states[i].id == id) return &ui->states[i];
    return NULL;
}

cui_state_entry *cui__state_entry(cui_ctx *ui, cui_id id) {
    cui_state_entry *e = cui__state_find(ui, id);
    if (e) { e->last_frame = ui->frame; return e; }
    int idx;
    if (ui->state_free >= 0) {
        idx = ui->state_free;
        ui->state_free = ui->states[idx].next;
    } else {
        CUI__GROW(ui, ui->states, ui->state_count, ui->state_cap, 1);
        idx = ui->state_count++;
        if (ui->state_count > ui->bucket_count) cui__state_rehash(ui, ui->bucket_count * 2);
    }
    e = &ui->states[idx];
    memset(e, 0, sizeof *e);
    e->id = id;
    e->last_frame = ui->frame;
    int b = (int)(id & (uint32_t)(ui->bucket_count - 1));
    e->next = ui->state_buckets[b];
    ui->state_buckets[b] = idx;
    return e;
}

/* The state of id when it has some or want is true, else NULL: for components
 * whose state is all rest values until touched (a button in a long list). */
void *cui__state_lazy(cui_ctx *ui, cui_id id, size_t size, bool want) {
    cui_state_entry *e = cui__state_find(ui, id);
    if (!want && (!e || !e->data || e->size < size)) return NULL;
    return cui_state(ui, id, size);
}

/* A 0..1 fade toward on, kept in state only while it is above zero, so rows that
 * scroll past without being hovered cost no allocation. */
float cui__hover_fade(cui_ctx *ui, cui_id id, bool on, float duration) {
    cui_state_entry *e = cui__state_find(ui, id);
    if (!on && (!e || !e->data || e->size < sizeof(float) || *(float *)e->data <= 0.0f)) return 0.0f;
    float *v = cui_state(ui, id, sizeof(float));
    cui__tween(ui, v, on ? 1.0f : 0.0f, duration);
    return *v;
}

void *cui_state(cui_ctx *ui, cui_id id, size_t size) {
    cui_state_entry *e = cui__state_entry(ui, id);
    if (e->size < size) {
        e->data = cui__realloc(ui, e->data, size);
        memset((char *)e->data + e->size, 0, size - e->size);
        e->size = size;
    }
    return e->data;
}

void cui__state_set_dtor(cui_ctx *ui, cui_id id, void (*dtor)(cui_ctx *ui, void *data)) {
    cui__state_entry(ui, id)->dtor = dtor;
}
void cui_state_on_free(cui_ctx *ui, cui_id id, void (*fn)(cui_ctx *ui, void *data)) { if (ui) cui__state_set_dtor(ui, id, fn); }

cui_rect cui__prev_rect(cui_ctx *ui, cui_id id) {
    cui_state_entry *e = cui__state_find(ui, id);
    if (e && e->has_rect) return e->rect;
    cui_rect r = { 0, 0, 0, 0 };
    return r;
}

bool cui__has_prev_rect(cui_ctx *ui, cui_id id) {
    cui_state_entry *e = cui__state_find(ui, id);
    return e && e->has_rect;
}

static void cui__state_gc(cui_ctx *ui) {
    /* drop state not touched for a while (about 5 seconds at 60 fps) */
    if (ui->frame % 60) return;
    for (int i = 0; i < ui->state_count; i++) {
        cui_state_entry *e = &ui->states[i];
        /* an entry with only last frame's rectangle is needed for a frame or two; state
         * someone keeps (data) stays for about five seconds unused */
        uint64_t keep = (e->data || e->dtor) ? 300 : 30;
        if (!e->id || ui->frame - e->last_frame < keep) continue;
        int b = (int)(e->id & (uint32_t)(ui->bucket_count - 1));
        int *link = &ui->state_buckets[b];
        while (*link >= 0 && *link != i) link = &ui->states[*link].next;
        if (*link == i) *link = e->next;
        if (e->dtor && e->data) e->dtor(ui, e->data);
        cui__free(ui, e->data);
        memset(e, 0, sizeof *e);
        e->next = ui->state_free;
        ui->state_free = i;
    }
    /* after a burst (a long list scrolled through) most slots are free: pack the
     * live ones so the table shrinks back; their data does not move */
    int live = 0;
    for (int i = 0; i < ui->state_count; i++) live += ui->states[i].id != 0;
    if (ui->state_count > 2048 && live * 4 < ui->state_count) {
        int cap = CUI_MAX(live * 2, 256);
        cui_state_entry *ns = cui__alloc(ui, sizeof *ns * (size_t)cap);
        int n = 0;
        for (int i = 0; i < ui->state_count; i++) if (ui->states[i].id) ns[n++] = ui->states[i];
        cui__free(ui, ui->states);
        ui->states = ns; ui->state_count = n; ui->state_cap = cap; ui->state_free = -1;
        int nb = 1024;
        while (nb < n) nb *= 2;
        cui__state_rehash(ui, nb);
    }
}

/* ------------------------------------------------------------------------ */
/* Identity                                                                 */
/* ------------------------------------------------------------------------ */

static cui_id cui__seed(cui_ctx *ui) { return ui->id_depth ? ui->id_stack[ui->id_depth - 1] : 0x9E3779B9u; }

cui_id cui_make_id(cui_ctx *ui, const char *str) {
    const char *keep = str ? strstr(str, "###") : NULL;   /* "Label###id": the id ignores the visible part */
    return cui__hash_str(keep ? keep : str, cui__seed(ui));
}

void cui_push_id(cui_ctx *ui, const char *str) {
    cui_id id = cui_make_id(ui, str);
    if (ui->id_depth < CUI_ID_STACK) { ui->auto_counter[ui->id_depth] = 0; ui->id_stack[ui->id_depth++] = id; }
}

void cui_push_id_int(cui_ctx *ui, int v) {
    cui_id id = cui__hash(&v, sizeof v, cui__seed(ui));
    if (ui->id_depth < CUI_ID_STACK) { ui->auto_counter[ui->id_depth] = 0; ui->id_stack[ui->id_depth++] = id; }
}

void cui_pop_id(cui_ctx *ui) { if (ui->id_depth > 0) ui->id_depth--; }

cui_id cui_last_id(cui_ctx *ui) { return ui->last_id; }
cui_rect cui_last_rect(cui_ctx *ui) { return cui__prev_rect(ui, ui->last_id); }
cui_rect cui_get_rect(cui_ctx *ui, cui_id id) { return cui__prev_rect(ui, id); }
bool cui_last_hovered(cui_ctx *ui) { return ui->last_id && ui->hot == ui->last_id; }

bool cui_is_disabled(cui_ctx *ui) { return ui->disabled_depth > 0; }
void cui_push_disabled(cui_ctx *ui, bool d) {
    if (ui->disabled_n < 32) ui->disabled_stack[ui->disabled_n++] = d;
    if (d) ui->disabled_depth++;
}
void cui_pop_disabled(cui_ctx *ui) {
    if (ui->disabled_n > 0 && ui->disabled_stack[--ui->disabled_n]) ui->disabled_depth--;
}

/* ------------------------------------------------------------------------ */
/* Input                                                                    */
/* ------------------------------------------------------------------------ */

static void cui__poke(cui_ctx *ui) { ui->settle_frames = CUI_MAX(ui->settle_frames, 2); ui->input_this_frame = true; }

void cui_input_mouse_move(cui_ctx *ui, float x, float y) {
    if (x != ui->mx || y != ui->my || !ui->mouse_inside) cui__poke(ui);
    ui->mx = x; ui->my = y; ui->mouse_inside = true;
}

void cui_input_mouse_leave(cui_ctx *ui) { ui->mouse_inside = false; ui->mx = -1e6f; ui->my = -1e6f; cui__poke(ui); }

void cui_input_mouse_button(cui_ctx *ui, cui_mouse_button b, bool down) {
    if ((int)b < 0 || (int)b > 2) return;
    cui__poke(ui);
    if (down && !ui->mdown[b]) {
        ui->mpressed[b] = true;
        if (b == CUI_MOUSE_LEFT) {
            double t = ui->time;
            float dx = ui->mx - ui->last_click_x, dy = ui->my - ui->last_click_y;
            if (t - ui->last_click_time < 0.4 && dx * dx + dy * dy < 25) ui->click_count++;
            else ui->click_count = 1;
            ui->last_click_time = t; ui->last_click_x = ui->mx; ui->last_click_y = ui->my;
        }
    }
    if (!down && ui->mdown[b]) ui->mreleased[b] = true;
    ui->mdown[b] = down;
}

void cui_input_scroll(cui_ctx *ui, float dx, float dy) { ui->scroll_dx += dx; ui->scroll_dy += dy; cui__poke(ui); }

void cui_input_key(cui_ctx *ui, cui_key key, bool down, int mods) {
    /* modifiers are sampled at key presses (and modifier-only changes), so a
     * quick shortcut tap released before the next frame still registers */
    if (down || key == CUI_KEY_NONE) ui->mods = mods;
    cui__poke(ui);
    if ((int)key <= 0 || key >= CUI_KEY_COUNT) return;
    if (down && ui->kpressed[key] < 255) ui->kpressed[key]++;
    ui->kdown[key] = down;
}

void cui_input_file_hover(cui_ctx *ui, bool hovering, float x, float y) {
    ui->drag_hover = hovering;
    ui->drag_x = x; ui->drag_y = y;
    if (hovering) { ui->mx = x; ui->my = y; ui->mouse_inside = true; }
    cui__poke(ui);
}

void cui_input_file_drop(cui_ctx *ui, const char *const *paths, int count, float x, float y) {
    ui->drag_hover = false;
    for (int i = 0; i < count && ui->drop_count < 64; i++) {
        if (!paths[i]) continue;
        size_t n = strlen(paths[i]) + 1;
        if (ui->drop_used + n > ui->drop_cap) {
            size_t cap = ui->drop_cap ? ui->drop_cap * 2 : 1024;
            while (cap < ui->drop_used + n) cap *= 2;
            ui->drop_buf = cui__realloc(ui, ui->drop_buf, cap);
            ui->drop_cap = cap;
        }
        memcpy(ui->drop_buf + ui->drop_used, paths[i], n);
        ui->drop_off[ui->drop_count++] = (int)ui->drop_used;
        ui->drop_used += n;
    }
    ui->drop_x = x; ui->drop_y = y;
    ui->mx = x; ui->my = y;
    ui->drop_claimed = false;
    cui__poke(ui);
}

int cui_dropped_file_count(cui_ctx *ui) { return ui->drop_claimed ? 0 : ui->drop_count; }
const char *cui_dropped_file(cui_ctx *ui, int index) {
    if (index < 0 || index >= ui->drop_count) return NULL;
    return ui->drop_buf + ui->drop_off[index];
}
bool cui_file_drag_active(cui_ctx *ui) { return ui->drag_hover; }

void cui_input_text(cui_ctx *ui, const char *utf8) {
    if (!utf8) return;
    int n = (int)strlen(utf8);
    if (ui->text_in_len + n >= (int)sizeof(ui->text_in)) return;
    /* drop control characters; keys handle those */
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)utf8[i];
        if (c < 32 || c == 127) continue;
        ui->text_in[ui->text_in_len++] = (char)c;
    }
    ui->text_in[ui->text_in_len] = 0;
    cui__poke(ui);
}

void cui_input_focus(cui_ctx *ui, bool focused) {
    ui->window_focused = focused;
    if (!focused) {
        memset(ui->kdown, 0, sizeof ui->kdown);
        for (int i = 0; i < 3; i++) { if (ui->mdown[i]) ui->mreleased[i] = true; ui->mdown[i] = false; }
        ui->mods = 0;
    }
    cui__poke(ui);
}

bool cui_key_pressed(cui_ctx *ui, cui_key k) { return k > 0 && k < CUI_KEY_COUNT && ui->kpressed[k] > 0; }
bool cui_key_down(cui_ctx *ui, cui_key k) { return k > 0 && k < CUI_KEY_COUNT && ui->kdown[k]; }
int cui_key_mods(cui_ctx *ui) { return ui->mods; }
bool cui_shortcut(cui_ctx *ui, int mods, cui_key key) {
    int relevant = CUI_MOD_SHIFT | CUI_MOD_CTRL | CUI_MOD_ALT | CUI_MOD_SUPER;
    return cui_key_pressed(ui, key) && (ui->mods & relevant) == mods;
}
cui_vec2 cui_mouse_pos(cui_ctx *ui) { cui_vec2 v = { ui->mx, ui->my }; return v; }
bool cui_mouse_down(cui_ctx *ui, cui_mouse_button b) { return (int)b >= 0 && (int)b < 3 && ui->mdown[b]; }
bool cui_mouse_clicked(cui_ctx *ui, cui_mouse_button b) { return (int)b >= 0 && (int)b < 3 && ui->mpressed[b]; }
bool cui_wants_keyboard(cui_ctx *ui) { return ui->edit != NULL; }
bool cui_wants_mouse(cui_ctx *ui) { return ui->hot != 0 || ui->active != 0; }

void cui__set_cursor(cui_ctx *ui, cui_cursor c) { ui->cursor = c; }

/* ------------------------------------------------------------------------ */
/* Interaction                                                              */
/* ------------------------------------------------------------------------ */

static void cui__compute_hot(cui_ctx *ui) {
    ui->hot = 0; ui->hot_layer = 0; ui->scroll_target = 0;
    if (!ui->mouse_inside) return;
    bool found_hot = false, fits = false;
    /* the wheel goes to the innermost scroll area that can move that way, so a
     * sideways strip (tabs, a board) inside a page does not swallow the page scroll */
    bool want_x = ui->scroll_dx != 0 || ((ui->mods & CUI_MOD_SHIFT) && ui->scroll_dy != 0), want_y = ui->scroll_dy != 0 && !want_x;
    for (int i = ui->prev_hit_count - 1; i >= 0; i--) {
        cui_hit *h = &ui->prev_hits[i];
        if (!cui__rect_contains(h->rect, ui->mx, ui->my)) continue;
        if ((h->flags & (CUI_NF_SCROLL_X | CUI_NF_SCROLL_Y)) && !fits) {
            cui_scroll_state *ss = cui_state(ui, h->id, sizeof *ss);
            bool can = (want_x && (h->flags & CUI_NF_SCROLL_X) && ss->content_w > ss->view_w + 0.5f) ||
                       (want_y && (h->flags & CUI_NF_SCROLL_Y) && ss->content_h > ss->view_h + 0.5f);
            if (!ui->scroll_target || can) ui->scroll_target = h->id;
            fits = can || (!want_x && !want_y);
        }
        if (!found_hot && (h->flags & (CUI_NF_INTERACTIVE | CUI_NF_BLOCKER | CUI_NF_HIT_OPAQUE))) {
            if (h->flags & CUI_NF_INTERACTIVE) ui->hot = h->id;
            ui->hot_layer = h->layer;
            found_hot = true;
        }
        if (h->flags & CUI_NF_BLOCKER) break;
        if (found_hot && fits) break;
    }
}

cui_interact cui__interact(cui_ctx *ui, cui_id id, int flags) {
    cui_interact r;
    memset(&r, 0, sizeof r);
    if (cui_is_disabled(ui)) {
        if (ui->focus == id) ui->focus = 0;
        if (ui->active == id) ui->active = 0;
        return r;
    }
    if (flags & CUI_IF_FOCUSABLE) {
        CUI__GROW(ui, ui->focus_order, ui->focus_count, ui->focus_cap, 1);
        CUI__GROW(ui, ui->focus_layers, ui->focus_count, ui->focus_layers_cap, 1);
        ui->focus_layers[ui->focus_count] = ui->current_layer;
        ui->focus_order[ui->focus_count++] = id;
        if (ui->focus_next_request == id) { ui->focus = id; ui->focus_next_request = 0; }
    }
    bool hovered = ui->hot == id && (ui->active == 0 || ui->active == id);
    r.hovered = hovered;
    if (hovered && ui->mpressed[CUI_MOUSE_LEFT]) {
        ui->active = id;
        if (flags & CUI_IF_FOCUSABLE) { ui->focus = id; ui->focus_visible = false; }
        else if (!(flags & CUI_IF_KEEP_FOCUS)) { ui->focus = 0; }
        if (ui->click_count >= 2) r.double_clicked = true;
    }
    if (hovered && ui->mpressed[CUI_MOUSE_RIGHT]) r.right_clicked = true;
    r.active = ui->active == id;
    r.pressed = r.active && (ui->mdown[CUI_MOUSE_LEFT] || ui->mpressed[CUI_MOUSE_LEFT]) && (hovered || (flags & CUI_IF_DRAG));
    if (ui->active == id && ui->mreleased[CUI_MOUSE_LEFT]) {
        r.released = true;
        if (hovered) r.clicked = true;
    }
    r.focused = ui->focus == id;
    /* a click gives focus without a ring; using the keyboard next shows the
     * ring and the keys act on that control, as in a browser. A trigger whose
     * popup is open leaves the keys to the popup. */
    int my_z = 0;
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].id == ui->current_layer) my_z = ui->popups[i].z;
    bool covered = false;   /* an open popup above this control owns the keyboard */
    for (int i = 0; i < ui->popup_count; i++) if (ui->popups[i].open && ui->popups[i].z > my_z) { covered = true; break; }
    if (r.focused && !ui->focus_visible && !covered && !(flags & CUI_IF_TEXT) &&
        (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE) || cui_key_pressed(ui, CUI_KEY_LEFT) ||
         cui_key_pressed(ui, CUI_KEY_RIGHT) || cui_key_pressed(ui, CUI_KEY_UP) || cui_key_pressed(ui, CUI_KEY_DOWN) ||
         cui_key_pressed(ui, CUI_KEY_HOME) || cui_key_pressed(ui, CUI_KEY_END)))
        ui->focus_visible = true;
    r.focus_visible = r.focused && ui->focus_visible;
    if (r.focused && ui->focus_visible && !(flags & CUI_IF_TEXT)) {
        if (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE)) {
            r.clicked = true;
            r.keyboard_activate = true;
        }
    }
    if (hovered && ui->active == 0) ui->hover_id_for_tooltip = id;
    return r;
}

void cui__mark_interactive(cui_ctx *ui, int node, int flags) {
    cui_node *n = cui__node(ui, node);
    n->flags |= CUI_NF_INTERACTIVE;
    if (flags & CUI_IF_FOCUSABLE) n->flags |= CUI_NF_FOCUSABLE;
}

/* ------------------------------------------------------------------------ */
/* Animation                                                                */
/* ------------------------------------------------------------------------ */

float cui__spring(cui_ctx *ui, cui_spring *s, float target, float duration, float bounce) {
    float motion = ui->theme.motion;
    if (motion <= 0.0f || ui->frame <= 1) { s->x = target; s->v = 0; return target; }
    duration *= motion;
    float w = 2.0f * CUI_PI / duration;
    float k = w * w;
    float c = 2.0f * (1.0f - bounce) * w;
    float dt = ui->dt;
    if (dt > 0.1f) dt = 0.1f;
    int steps = (int)ceilf(dt / (1.0f / 240.0f));
    if (steps < 1) steps = 1;
    float h = dt / (float)steps;
    for (int i = 0; i < steps; i++) {
        float a = -k * (s->x - target) - c * s->v;
        s->v += a * h;
        s->x += s->v * h;
    }
    float eps = 0.002f + 0.0005f * fabsf(target);
    if (fabsf(s->x - target) < eps && fabsf(s->v) < eps * 10.0f) { s->x = target; s->v = 0; }
    else ui->animating = true;
    return s->x;
}

float cui__tween(cui_ctx *ui, float *x, float target, float duration) {
    float motion = ui->theme.motion;
    if (motion <= 0.0f || ui->frame <= 1) { *x = target; return target; }
    duration *= motion;
    float a = 1.0f - expf(-ui->dt * 4.6f / duration);
    *x += (target - *x) * a;
    if (fabsf(*x - target) < 0.002f) *x = target;
    else ui->animating = true;
    return *x;
}

typedef struct { cui_spring s; bool init; } cui__anim_state;

float cui_animate(cui_ctx *ui, cui_id id, float target) {
    return cui_animate_spring(ui, id, target, 0.4f, 0.0f);
}

float cui_animate_spring(cui_ctx *ui, cui_id id, float target, float duration, float bounce) {
    cui__anim_state *st = cui_state(ui, id ^ 0xA11A11u, sizeof *st);
    if (!st->init) { st->s.x = target; st->init = true; }
    return cui__spring(ui, &st->s, target, duration, bounce);
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                    */
/* ------------------------------------------------------------------------ */

void cui_begin_frame(cui_ctx *ui, float dt) {
    if (ui->in_frame) cui_end_frame(ui);
    if (dt < 0) dt = 0;
    if (dt > 0.25f) dt = 0.25f;
    ui->in_frame = true;
    ui->dt = dt;
    ui->time += dt;
    ui->frame++;
    ui->animating = false;
    ui->wakeup = -1;
    ui->cursor = CUI_CURSOR_ARROW;
    if (ui->settle_frames > 0) ui->settle_frames--;

    cui__trim_buffers(ui);   /* before anything of this frame is in them */
    cui__frame_arena_reset(ui);

    /* swap hit lists and focus order */
    { cui_hit *t = ui->prev_hits; ui->prev_hits = ui->hits; ui->hits = t;
      int c = ui->prev_hit_cap; ui->prev_hit_cap = ui->hit_cap; ui->hit_cap = c;
      ui->prev_hit_count = ui->hit_count; ui->hit_count = 0; }
    { cui_id *t = ui->prev_focus_order; ui->prev_focus_order = ui->focus_order; ui->focus_order = t;
      int c = ui->prev_focus_cap; ui->prev_focus_cap = ui->focus_cap; ui->focus_cap = c;
      cui_id *tl = ui->prev_focus_layers; ui->prev_focus_layers = ui->focus_layers; ui->focus_layers = tl;
      int cl = ui->prev_focus_layers_cap; ui->prev_focus_layers_cap = ui->focus_layers_cap; ui->focus_layers_cap = cl;
      ui->prev_focus_count = ui->focus_count; ui->focus_count = 0; }
    ui->prev_modal_layer = ui->modal_layer;
    ui->modal_layer = 0;
    ui->modal_z = 0;

    cui__compute_hot(ui);
    if (ui->active && !ui->mdown[0] && !ui->mreleased[0] && !ui->mpressed[0]) ui->active = 0;

    /* Tab navigation (uses last frame's focus order; stays inside an open modal).
     * Ctrl+Tab is left to the app: it switches documents, as in a browser. */
    bool tab_kept = ui->tab_owner && ui->tab_owner == ui->focus;   /* a code editor types the Tab */
    ui->tab_owner = 0;
    if (cui_key_pressed(ui, CUI_KEY_TAB) && !(ui->mods & CUI_MOD_CTRL) && ui->prev_focus_count > 0 && !tab_kept) {
        int n = 0, cur = -1;
        cui_id *cand = cui__frame_alloc(ui, sizeof(cui_id) * (size_t)ui->prev_focus_count);
        for (int i = 0; i < ui->prev_focus_count; i++) {
            if (ui->prev_modal_layer && ui->prev_focus_layers[i] != ui->prev_modal_layer) continue;
            if (ui->prev_focus_order[i] == ui->focus) cur = n;
            cand[n++] = ui->prev_focus_order[i];
        }
        if (n > 0) {
            int dir = (ui->mods & CUI_MOD_SHIFT) ? -1 : 1;
            int idx = cur < 0 ? (dir > 0 ? 0 : n - 1) : (cur + dir + n) % n;
            ui->focus = cand[idx];
            ui->focus_visible = true;
        }
        ui->kpressed[CUI_KEY_TAB] = 0;
    }
    if (ui->mpressed[0] && ui->hot == 0) { ui->focus = 0; }

    cui__theme_update(ui);
    cui__fonts_begin_frame(ui);
    if (ui->emoji) cui__emoji_frame(ui);
    cui__draw_reset(ui);

    ui->node_count = 0;
    ui->float_count = 0;
    ui->parent_depth = 0;
    ui->id_depth = 0;
    ui->auto_counter[0] = 0;
    ui->disabled_depth = 0;
    ui->disabled_n = 0;
    ui->layer_depth = 0;
    ui->current_layer = 0;
    ui->last_node = -1;
    ui->last_id = 0;
    ui->hover_id_for_tooltip = 0;
    ui->edit = NULL;

    int root = cui__node_new(ui, cui__hash_str("#root", 0));
    cui_node *r = cui__node(ui, root);
    r->w = CUI_PX(ui->vw);
    r->h = CUI_PX(ui->vh);
    r->dir = CUI_COLUMN;
    r->flags |= CUI_NF_CLIP;
    cui__open(ui, root);

    cui__widgets_begin_frame(ui);
}

void cui_end_frame(cui_ctx *ui) {
    if (!ui->in_frame) return;
    cui__widgets_end_frame(ui);
    while (ui->parent_depth > 0) cui__close(ui);

    cui__layout(ui);
    cui__paint(ui);

    ui->edit_prev = ui->edit != NULL;
    /* caret blink, tooltips and toasts ask for wakeups through ui->wakeup */
    if (ui->edit) cui__wake_in(ui, 0.53f - (float)fmod(ui->time - ui->caret_time, 0.53));

    /* clear per frame input */
    for (int i = 0; i < 3; i++) { ui->mpressed[i] = false; ui->mreleased[i] = false; }
    ui->scroll_dx = ui->scroll_dy = 0;
    memset(ui->kpressed, 0, sizeof ui->kpressed);
    ui->text_in_len = 0;
    ui->text_in[0] = 0;
    ui->input_this_frame = false;
    ui->drop_count = 0;
    ui->drop_used = 0;
    ui->drop_claimed = false;

    if (ui->platform.set_cursor) ui->platform.set_cursor(ui->platform.user, ui->cursor);

    cui__state_gc(ui);
    cui__fonts_upload(ui);

    cui_draw_data *dd = &ui->draw_data;
    dd->vertices = ui->dl.vtx;
    dd->vertex_count = ui->dl.vtx_count;
    dd->indices = ui->dl.idx;
    dd->index_count = ui->dl.idx_count;
    dd->commands = ui->dl.cmd;
    dd->command_count = ui->dl.cmd_count;
    dd->width_px = (int)(ui->vw * ui->scale + 0.5f);
    dd->height_px = (int)(ui->vh * ui->scale + 0.5f);
    dd->clear_color = ui->theme.colors[CUI_TOKEN_BACKGROUND];
    dd->font_atlas = ui->atlas_tex;
    ui->in_frame = false;
}

const cui_draw_data *cui_get_draw_data(cui_ctx *ui) { return &ui->draw_data; }

void cui__wakeup_after(cui_ctx *ui, float seconds) { cui__wake_in(ui, seconds); }
void cui_request_wakeup(cui_ctx *ui, float seconds) { if (ui) cui__wake_in(ui, seconds); }
