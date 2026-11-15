/* Software rasterizer for CUI draw data.
 *
 * Implements exactly the same shading as the GPU shader (see
 * docs/rendering.md): signed distance rounded rectangles with borders,
 * gaussian shadows, alpha-texture text, images and solid geometry, blended
 * with premultiplied alpha. Rendering can be split over several threads by
 * horizontal bands. */
#include "../cui_internal.h"
#include "cui/cui_render.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif

typedef struct cui__sw_tex {
    int w, h;
    cui_texture_format fmt;
    uint8_t *data;
    bool used;
} cui__sw_tex;

#define CUI__SW_TILE 64

struct cui_sw_renderer {
    cui__sw_tex *tex;
    int tex_count, tex_cap;
    int threads;
    /* incremental: repaint only the tiles whose triangles changed since the last frame */
    bool incremental;
    const void *last_px; int last_w, last_h, last_stride; cui_sw_format last_fmt;
    uint64_t *tiles, *prev; int tiles_cap;
    uint8_t *force;                        /* tiles that sample a part of a texture that changed */
    int damage[4];                         /* x0, y0, x1, y1 of the last frame's repaint */
    /* texture parts written since the last frame: triangles sampling them are drawn again
     * (new glyphs go to unused parts, so text already on screen stays put) */
    struct { cui_texture t; int x0, y0, x1, y1; } changed[32];
    int nchanged; bool changed_overflow;
};

static void cui__sw_note_change(cui_sw_renderer *r, cui_texture t, int x0, int y0, int x1, int y1) {
    for (int i = 0; i < r->nchanged; i++)
        if (r->changed[i].t == t) {
            if (x0 < r->changed[i].x0) r->changed[i].x0 = x0;
            if (y0 < r->changed[i].y0) r->changed[i].y0 = y0;
            if (x1 > r->changed[i].x1) r->changed[i].x1 = x1;
            if (y1 > r->changed[i].y1) r->changed[i].y1 = y1;
            return;
        }
    if (r->nchanged == 32) { r->changed_overflow = true; return; }
    r->changed[r->nchanged].t = t;
    r->changed[r->nchanged].x0 = x0; r->changed[r->nchanged].y0 = y0; r->changed[r->nchanged].x1 = x1; r->changed[r->nchanged].y1 = y1;
    r->nchanged++;
}

static void *cui__sw_xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q && n) { fprintf(stderr, "cui: out of memory\n"); abort(); }
    return q;
}

static cui_texture cui__sw_tex_create(void *user, int w, int h, cui_texture_format fmt, const void *pixels) {
    cui_sw_renderer *r = user;
    int slot = -1;
    for (int i = 0; i < r->tex_count; i++) if (!r->tex[i].used) { slot = i; break; }
    if (slot < 0) {
        if (r->tex_count == r->tex_cap) {
            r->tex_cap = r->tex_cap ? r->tex_cap * 2 : 16;
            r->tex = cui__sw_xrealloc(r->tex, sizeof(cui__sw_tex) * (size_t)r->tex_cap);
        }
        slot = r->tex_count++;
    }
    cui__sw_tex *t = &r->tex[slot];
    cui__sw_note_change(r, (cui_texture)(slot + 1), 0, 0, w, h);
    t->w = w; t->h = h; t->fmt = fmt; t->used = true;
    size_t bytes = (size_t)w * (size_t)h * (size_t)fmt;
    t->data = cui__sw_xrealloc(NULL, bytes);
    if (pixels) memcpy(t->data, pixels, bytes);
    else memset(t->data, 0, bytes);
    return (cui_texture)(slot + 1);
}

static void cui__sw_tex_update(void *user, cui_texture tex, int x, int y, int w, int h, const void *pixels, int stride) {
    cui_sw_renderer *r = user;
    int slot = (int)tex - 1;
    if (slot < 0 || slot >= r->tex_count || !r->tex[slot].used) return;
    cui__sw_tex *t = &r->tex[slot];
    cui__sw_note_change(r, tex, x, y, x + w, y + h);
    int bpp = (int)t->fmt;
    const uint8_t *src = pixels;
    for (int row = 0; row < h; row++) {
        if (y + row < 0 || y + row >= t->h) continue;
        memcpy(t->data + ((size_t)(y + row) * (size_t)t->w + (size_t)x) * (size_t)bpp,
               src + (size_t)row * (size_t)stride, (size_t)w * (size_t)bpp);
    }
}

static void cui__sw_tex_destroy(void *user, cui_texture tex) {
    cui_sw_renderer *r = user;
    int slot = (int)tex - 1;
    if (slot < 0 || slot >= r->tex_count) return;
    free(r->tex[slot].data);
    r->tex[slot].data = NULL;
    r->tex[slot].used = false;
}

static int cui__cpu_count(void) {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#else
    return 1;
#endif
}

cui_sw_renderer *cui_sw_create(void) {
    cui_sw_renderer *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    int n = cui__cpu_count();
    r->threads = n > 8 ? 8 : (n < 1 ? 1 : n);
    return r;
}

void cui_sw_destroy(cui_sw_renderer *r) {
    if (!r) return;
    for (int i = 0; i < r->tex_count; i++) free(r->tex[i].data);
    free(r->tex);
    free(r->tiles);
    free(r->prev);
    free(r->force);
    free(r);
}

void cui_sw_set_threads(cui_sw_renderer *r, int threads) { r->threads = threads < 1 ? 1 : (threads > 32 ? 32 : threads); }
void cui_sw_set_incremental(cui_sw_renderer *r, bool on) { if (r) { r->incremental = on; r->last_px = NULL; } }
bool cui_sw_last_damage(const cui_sw_renderer *r, int *x, int *y, int *w, int *h) {
    if (!r) return false;
    if (x) *x = r->damage[0];
    if (y) *y = r->damage[1];
    if (w) *w = r->damage[2] - r->damage[0];
    if (h) *h = r->damage[3] - r->damage[1];
    return r->damage[2] > r->damage[0] && r->damage[3] > r->damage[1];
}

cui_renderer_iface cui_sw_iface(cui_sw_renderer *r) {
    cui_renderer_iface i;
    memset(&i, 0, sizeof i);
    i.user = r;
    i.texture_create = cui__sw_tex_create;
    i.texture_update = cui__sw_tex_update;
    i.texture_destroy = cui__sw_tex_destroy;
    return i;
}

/* ---- shading ---- */

static inline float cui__sd_rrect(float px, float py, float hw, float hh, float tl, float tr, float br, float bl) {
    float r = px < 0 ? (py < 0 ? tl : bl) : (py < 0 ? tr : br);
    float m = hw < hh ? hw : hh;
    if (r > m) r = m;
    float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float inside = qx > qy ? qx : qy;
    if (inside > 0) inside = 0;
    return inside + sqrtf(ox * ox + oy * oy) - r;
}

static inline float cui__erf(float x) {
    /* Abramowitz-Stegun 7.1.26 */
    float s = x < 0 ? -1.0f : 1.0f;
    x = fabsf(x);
    float t = 1.0f / (1.0f + 0.3275911f * x);
    float y = 1.0f - (((((1.061405429f * t - 1.453152027f) * t) + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t * expf(-x * x);
    return s * y;
}

static inline float cui__sat(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

typedef struct { float r, g, b, a; } cui__px;  /* premultiplied */

static float cui__sample_r8(const cui__sw_tex *t, float u, float v) {
    float x = u * (float)t->w - 0.5f, y = v * (float)t->h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = CUI_CLAMP(x0, 0, t->w - 1); y0 = CUI_CLAMP(y0, 0, t->h - 1);
    x1 = CUI_CLAMP(x1, 0, t->w - 1); y1 = CUI_CLAMP(y1, 0, t->h - 1);
    const uint8_t *d = t->data;
    float a = d[y0 * t->w + x0], b = d[y0 * t->w + x1], c = d[y1 * t->w + x0], e = d[y1 * t->w + x1];
    return ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + e * fx) * fy) / 255.0f;
}

static cui__px cui__sample_rgba(const cui__sw_tex *t, float u, float v) {
    float x = u * (float)t->w - 0.5f, y = v * (float)t->h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0;
    int xs[2] = { x0, x0 + 1 }, ys[2] = { y0, y0 + 1 };
    for (int i = 0; i < 2; i++) {
        xs[i] = CUI_CLAMP(xs[i], 0, t->w - 1);
        ys[i] = CUI_CLAMP(ys[i], 0, t->h - 1);
    }
    float w[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
    cui__px o = { 0, 0, 0, 0 };
    for (int k = 0; k < 4; k++) {
        const uint8_t *p = t->data + ((size_t)ys[k >> 1] * (size_t)t->w + (size_t)xs[k & 1]) * 4;
        float a = p[3] / 255.0f;
        o.r += w[k] * p[0] / 255.0f * a;
        o.g += w[k] * p[1] / 255.0f * a;
        o.b += w[k] * p[2] / 255.0f * a;
        o.a += w[k] * a;
    }
    return o;
}

typedef struct {
    const cui_draw_data *dd;
    cui_sw_renderer *r;
    uint8_t *pixels;
    int width, height, stride;
    cui_sw_format fmt;
    int x0, y0, x1, y1;   /* the part this job paints */
} cui__sw_job;

static void cui__blend(uint8_t *p, cui__px s, bool bgra) {
    if (s.a <= 0.0005f) return;
    if (s.a >= 0.9995f) {
        int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;
        p[ri] = (uint8_t)(cui__sat(s.r) * 255.0f + 0.5f); p[1] = (uint8_t)(cui__sat(s.g) * 255.0f + 0.5f);
        p[bi] = (uint8_t)(cui__sat(s.b) * 255.0f + 0.5f); p[3] = 255;
        return;
    }
    float ia = 1.0f - s.a;
    int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;
    float dr = p[ri] / 255.0f, dg = p[1] / 255.0f, db = p[bi] / 255.0f, da = p[3] / 255.0f;
    float r = s.r + dr * ia, g = s.g + dg * ia, b = s.b + db * ia, a = s.a + da * ia;
    p[ri] = (uint8_t)(cui__sat(r) * 255.0f + 0.5f);
    p[1] = (uint8_t)(cui__sat(g) * 255.0f + 0.5f);
    p[bi] = (uint8_t)(cui__sat(b) * 255.0f + 0.5f);
    p[3] = (uint8_t)(cui__sat(a) * 255.0f + 0.5f);
}

static void cui__raster_tri(const cui__sw_job *job, const cui_vertex *a, const cui_vertex *b, const cui_vertex *c,
                            const cui__sw_tex *tex, int cx0, int cy0, int cx1, int cy1) {
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-8f) return;
    float minx = CUI_MIN(a->x, CUI_MIN(b->x, c->x)), maxx = CUI_MAX(a->x, CUI_MAX(b->x, c->x));
    float miny = CUI_MIN(a->y, CUI_MIN(b->y, c->y)), maxy = CUI_MAX(a->y, CUI_MAX(b->y, c->y));
    int x0 = CUI_MAX(cx0, (int)floorf(minx)), x1 = CUI_MIN(cx1, (int)ceilf(maxx));
    int y0 = CUI_MAX(cy0, (int)floorf(miny)), y1 = CUI_MIN(cy1, (int)ceilf(maxy));
    if (x0 >= x1 || y0 >= y1) return;
    float inv = 1.0f / area;
    /* top-left fill rule: a pixel on an edge shared by two triangles is
     * drawn by exactly one of them, so translucent meshes have no seams */
    float sgn = area > 0 ? 1.0f : -1.0f;
    const cui_vertex *ev[3][2] = { { b, c }, { c, a }, { a, b } };
    bool tl[3];
    for (int e = 0; e < 3; e++) {
        float dx = (ev[e][1]->x - ev[e][0]->x) * sgn, dy = (ev[e][1]->y - ev[e][0]->y) * sgn;
        tl[e] = dy < 0 || (dy == 0 && dx > 0);
    }
    int mode = (int)(a->mode + 0.5f);
    bool bgra = job->fmt == CUI_SW_BGRA;
    /* flat attributes come from the first vertex */
    float hw = a->hw, hh = a->hh, rtl = a->r_tl, rtr = a->r_tr, rbr = a->r_br, rbl = a->r_bl, bw = a->border;
    float bcr = a->border_color.r / 255.0f, bcg = a->border_color.g / 255.0f, bcb = a->border_color.b / 255.0f, bca = a->border_color.a / 255.0f;
    bool flat_color = memcmp(&a->color, &b->color, 4) == 0 && memcmp(&a->color, &c->color, 4) == 0;
    const cui_vertex *tv[3] = { a, b, c };
    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        uint8_t *row = job->pixels + (size_t)y * (size_t)job->stride;
        /* horizontal span of the triangle on this row */
        float sx0 = 1e30f, sx1 = -1e30f;
        for (int e = 0; e < 3; e++) {
            const cui_vertex *p0 = tv[e], *p1 = tv[(e + 1) % 3];
            float ya = p0->y, yb = p1->y;
            if ((py < ya && py < yb) || (py > ya && py > yb)) continue;
            float xi = (yb == ya) ? p0->x : p0->x + (p1->x - p0->x) * (py - ya) / (yb - ya);
            if (yb == ya) { sx0 = CUI_MIN(sx0, CUI_MIN(p0->x, p1->x)); sx1 = CUI_MAX(sx1, CUI_MAX(p0->x, p1->x)); continue; }
            sx0 = CUI_MIN(sx0, xi); sx1 = CUI_MAX(sx1, xi);
        }
        if (sx1 < sx0) continue;
        int rx0 = CUI_MAX(x0, (int)floorf(sx0) - 1), rx1 = CUI_MIN(x1, (int)ceilf(sx1) + 1);
        for (int x = rx0; x < rx1; x++) {
            float px = (float)x + 0.5f;
            float e0 = (b->x - px) * (c->y - py) - (b->y - py) * (c->x - px);
            float e1 = (c->x - px) * (a->y - py) - (c->y - py) * (a->x - px);
            float e2 = (a->x - px) * (b->y - py) - (a->y - py) * (b->x - px);
            float w0 = e0 * inv, w1 = e1 * inv, w2 = e2 * inv;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if ((w0 == 0 && !tl[0]) || (w1 == 0 && !tl[1]) || (w2 == 0 && !tl[2])) continue;
            float cr, cg, cb, ca;
            if (flat_color) {
                cr = a->color.r / 255.0f; cg = a->color.g / 255.0f; cb = a->color.b / 255.0f; ca = a->color.a / 255.0f;
            } else {
                cr = (a->color.r * w0 + b->color.r * w1 + c->color.r * w2) / 255.0f;
                cg = (a->color.g * w0 + b->color.g * w1 + c->color.g * w2) / 255.0f;
                cb = (a->color.b * w0 + b->color.b * w1 + c->color.b * w2) / 255.0f;
                ca = (a->color.a * w0 + b->color.a * w1 + c->color.a * w2) / 255.0f;
            }
            cui__px o;
            if (mode == CUI_SHADE_SOLID) {
                o.r = cr * ca; o.g = cg * ca; o.b = cb * ca; o.a = ca;
            } else if (mode == CUI_SHADE_TEXT) {
                float u = a->u * w0 + b->u * w1 + c->u * w2, v = a->v * w0 + b->v * w1 + c->v * w2;
                float cov = tex && tex->fmt == CUI_TEXTURE_R8 ? cui__sample_r8(tex, u, v) : 1.0f;
                float al = ca * cov;
                o.r = cr * al; o.g = cg * al; o.b = cb * al; o.a = al;
            } else {
                float lx = a->lx * w0 + b->lx * w1 + c->lx * w2, ly = a->ly * w0 + b->ly * w1 + c->ly * w2;
                float d = cui__sd_rrect(lx, ly, hw, hh, rtl, rtr, rbr, rbl);
                if (mode == CUI_SHADE_RRECT) {
                    float outer = cui__sat(0.5f - d);
                    if (outer <= 0) continue;
                    float inner = bw > 0 ? cui__sat(0.5f - (d + bw)) : outer;
                    float fa = ca * inner, ba = bca * (outer - inner);
                    o.r = cr * fa + bcr * ba; o.g = cg * fa + bcg * ba; o.b = cb * fa + bcb * ba; o.a = fa + ba;
                } else if (mode == CUI_SHADE_SHADOW) {
                    float sigma = bw > 0.01f ? bw : 0.01f;
                    float al = ca * 0.5f * (1.0f - cui__erf(d / (sigma * 1.41421356f)));
                    o.r = cr * al; o.g = cg * al; o.b = cb * al; o.a = al;
                } else { /* image */
                    float cov = cui__sat(0.5f - d);
                    if (cov <= 0) continue;
                    float u = a->u * w0 + b->u * w1 + c->u * w2, v = a->v * w0 + b->v * w1 + c->v * w2;
                    cui__px t = tex && tex->fmt == CUI_TEXTURE_RGBA8 ? cui__sample_rgba(tex, u, v) : (cui__px){ 1, 1, 1, 1 };
                    float k = ca * cov;
                    o.r = t.r * cr * k; o.g = t.g * cg * k; o.b = t.b * cb * k; o.a = t.a * k;
                }
            }
            cui__blend(row + (size_t)x * 4, o, bgra);
        }
    }
}


/* Fast path for axis aligned quads (almost everything CUI draws): no
 * barycentrics, linear local coordinates and early outs for the interior of
 * rounded rectangles. Returns false when the quad does not qualify. */
static bool cui__raster_quad(const cui__sw_job *job, const cui_vertex *v0, const cui_vertex *v1, const cui_vertex *v2,
                             const cui_vertex *v3, const cui__sw_tex *tex, int cx0, int cy0, int cx1, int cy1) {
    if (v0->y != v1->y || v1->x != v2->x || v2->y != v3->y || v3->x != v0->x) return false;
    if (v0->x >= v1->x || v0->y >= v3->y) return false;
    if (memcmp(&v0->color, &v1->color, 4) != 0 || memcmp(&v2->color, &v3->color, 4) != 0) return false;
    int mode = (int)(v0->mode + 0.5f);
    float qx0 = v0->x, qy0 = v0->y, qx1 = v1->x, qy1 = v3->y;
    int x0 = CUI_MAX(cx0, (int)ceilf(qx0 - 0.5f)), x1 = CUI_MIN(cx1, (int)ceilf(qx1 - 0.5f));
    int y0 = CUI_MAX(cy0, (int)ceilf(qy0 - 0.5f)), y1 = CUI_MIN(cy1, (int)ceilf(qy1 - 0.5f));
    if (x0 >= x1 || y0 >= y1) return true;
    bool bgra = job->fmt == CUI_SW_BGRA;
    int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;
    float qh = qy1 - qy0, qw = qx1 - qx0;
    bool gradient = memcmp(&v0->color, &v3->color, 4) != 0;
    float hw = v0->hw, hh = v0->hh, bw = v0->border;
    float rmax = CUI_MAX(CUI_MAX(v0->r_tl, v0->r_tr), CUI_MAX(v0->r_br, v0->r_bl));
    rmax = CUI_MIN(rmax, CUI_MIN(hw, hh));
    float bcr = v0->border_color.r / 255.0f, bcg = v0->border_color.g / 255.0f, bcb = v0->border_color.b / 255.0f, bca = v0->border_color.a / 255.0f;
    float du = (v1->u - v0->u) / qw, dv = (v3->v - v0->v) / qh;
    bool text_exact = mode == CUI_SHADE_TEXT && tex && fabsf(du * (float)tex->w - 1.0f) < 0.001f && fabsf(dv * (float)tex->h - 1.0f) < 0.001f;
    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        float ty = (py - qy0) / qh;
        float cr, cg, cb, ca;
        if (gradient) {
            cr = (v0->color.r + (v3->color.r - v0->color.r) * ty) / 255.0f;
            cg = (v0->color.g + (v3->color.g - v0->color.g) * ty) / 255.0f;
            cb = (v0->color.b + (v3->color.b - v0->color.b) * ty) / 255.0f;
            ca = (v0->color.a + (v3->color.a - v0->color.a) * ty) / 255.0f;
        } else {
            cr = v0->color.r / 255.0f; cg = v0->color.g / 255.0f; cb = v0->color.b / 255.0f; ca = v0->color.a / 255.0f;
        }
        uint8_t opaque[4];
        opaque[ri] = (uint8_t)(cr * 255.0f + 0.5f); opaque[1] = (uint8_t)(cg * 255.0f + 0.5f);
        opaque[bi] = (uint8_t)(cb * 255.0f + 0.5f); opaque[3] = 255;
        float ly = v0->ly + (py - qy0);
        float ay = fabsf(ly);
        uint8_t *row = job->pixels + (size_t)y * (size_t)job->stride;
        if (mode == CUI_SHADE_TEXT) {
            float vv = v0->v + (py - qy0) * dv;
            int tyi = (int)(vv * (float)(tex ? tex->h : 1));
            for (int x = x0; x < x1; x++) {
                float px = (float)x + 0.5f;
                float cov;
                if (!tex || tex->fmt != CUI_TEXTURE_R8) cov = 1.0f;
                else if (text_exact) {
                    int txi = (int)((v0->u + (px - qx0) * du) * (float)tex->w);
                    if (txi < 0 || txi >= tex->w || tyi < 0 || tyi >= tex->h) continue;
                    uint8_t t = tex->data[(size_t)tyi * (size_t)tex->w + (size_t)txi];
                    if (!t) continue;
                    cov = t / 255.0f;
                } else cov = cui__sample_r8(tex, v0->u + (px - qx0) * du, vv);
                float al = ca * cov;
                cui__px o = { cr * al, cg * al, cb * al, al };
                cui__blend(row + (size_t)x * 4, o, bgra);
            }
            continue;
        }
        /* The inside of a rounded rectangle (or all of a solid) is one color: find it with the
         * same test the loop below uses (it is one interval, found by walking in from both ends)
         * and do it in one go: fill it when opaque, skip it when clear (a border-only box),
         * blend one color over it otherwise. The loop does the edges. */
        int sx0 = x1, sx1 = x1;
        if (mode == CUI_SHADE_SOLID || mode == CUI_SHADE_RRECT) {
            if (mode == CUI_SHADE_SOLID) { if (ca >= 0.999f) { sx0 = x0; sx1 = x1; } }
            else {
                float inset = bw + 0.5f;
#define CUI__DEEP(xx) (fabsf(v0->lx + (((float)(xx) + 0.5f) - qx0)) <= hw - inset - rmax && ay <= hh - inset) || \
                      (fabsf(v0->lx + (((float)(xx) + 0.5f) - qx0)) <= hw - inset && ay <= hh - inset - rmax)
                if (ay <= hh - inset) {
                    int a = x0, b = x1;
                    while (a < x1 && !(CUI__DEEP(a))) a++;
                    while (b > a && !(CUI__DEEP(b - 1))) b--;
                    if (a < b) { sx0 = a; sx1 = b; }
                }
#undef CUI__DEEP
            }
            if (sx0 < sx1) {
                uint8_t *p = row + (size_t)sx0 * 4;
                int n = sx1 - sx0;
                if (ca >= 0.999f) for (int k = 0; k < n; k++) memcpy(p + (size_t)k * 4, opaque, 4);   /* any alignment */
                else if (ca > 0.0005f) {
                    cui__px o = { cr * ca, cg * ca, cb * ca, ca };
                    for (int k = 0; k < n; k++) cui__blend(p + (size_t)k * 4, o, bgra);
                }
            }
        }
        for (int x = x0; x < x1; x++) {
            if (x == sx0) { x = sx1 - 1; continue; }   /* filled above */
            float px = (float)x + 0.5f;
            float lx = v0->lx + (px - qx0);
            cui__px o;
            if (mode == CUI_SHADE_SOLID) {
                if (ca >= 0.999f) { memcpy(row + (size_t)x * 4, opaque, 4); continue; }
                o.r = cr * ca; o.g = cg * ca; o.b = cb * ca; o.a = ca;
            } else if (mode == CUI_SHADE_RRECT) {
                float ax = fabsf(lx);
                float inset = bw + 0.5f;
                bool deep = (ax <= hw - inset - rmax && ay <= hh - inset) || (ax <= hw - inset && ay <= hh - inset - rmax);
                if (deep) {
                    if (ca >= 0.999f) { memcpy(row + (size_t)x * 4, opaque, 4); continue; }
                    if (ca <= 0.0005f) continue;
                    o.r = cr * ca; o.g = cg * ca; o.b = cb * ca; o.a = ca;
                } else {
                    float d = cui__sd_rrect(lx, ly, hw, hh, v0->r_tl, v0->r_tr, v0->r_br, v0->r_bl);
                    float outer = cui__sat(0.5f - d);
                    if (outer <= 0) continue;
                    float inner = bw > 0 ? cui__sat(0.5f - (d + bw)) : outer;
                    float fa = ca * inner, ba = bca * (outer - inner);
                    o.r = cr * fa + bcr * ba; o.g = cg * fa + bcg * ba; o.b = cb * fa + bcb * ba; o.a = fa + ba;
                }
            } else if (mode == CUI_SHADE_SHADOW) {
                float d = cui__sd_rrect(lx, ly, hw, hh, v0->r_tl, v0->r_tr, v0->r_br, v0->r_bl);
                float sigma = bw > 0.01f ? bw : 0.01f;
                float z = d / (sigma * 1.41421356f);
                if (z > 2.8f) continue;
                float al = ca * 0.5f * (1.0f - cui__erf(z));
                o.r = cr * al; o.g = cg * al; o.b = cb * al; o.a = al;
            } else {
                float d = cui__sd_rrect(lx, ly, hw, hh, v0->r_tl, v0->r_tr, v0->r_br, v0->r_bl);
                float cov = cui__sat(0.5f - d);
                if (cov <= 0) continue;
                float u = v0->u + (px - qx0) * du, vv = v0->v + (py - qy0) * dv;
                cui__px t = tex && tex->fmt == CUI_TEXTURE_RGBA8 ? cui__sample_rgba(tex, u, vv) : (cui__px){ 1, 1, 1, 1 };
                float k = ca * cov;
                o.r = t.r * cr * k; o.g = t.g * cg * k; o.b = t.b * cb * k; o.a = t.a * k;
            }
            cui__blend(row + (size_t)x * 4, o, bgra);
        }
    }
    return true;
}

static void cui__sw_run(cui__sw_job *job) {
    const cui_draw_data *dd = job->dd;
    cui_sw_renderer *r = job->r;
    for (int ci = 0; ci < dd->command_count; ci++) {
        const cui_draw_cmd *cmd = &dd->commands[ci];
        int cx0 = CUI_MAX(job->x0, (int)floorf(cmd->clip.x));
        int cy0 = CUI_MAX(job->y0, (int)floorf(cmd->clip.y));
        int cx1 = CUI_MIN(job->x1, (int)ceilf(cmd->clip.x + cmd->clip.w));
        int cy1 = CUI_MIN(job->y1, (int)ceilf(cmd->clip.y + cmd->clip.h));
        if (cx0 >= cx1 || cy0 >= cy1) continue;
        cui_texture tid = cmd->texture ? cmd->texture : dd->font_atlas;
        int slot = (int)tid - 1;
        const cui__sw_tex *tex = (slot >= 0 && slot < r->tex_count && r->tex[slot].used) ? &r->tex[slot] : NULL;
        for (uint32_t k = 0; k + 2 < cmd->index_count; k += 3) {
            const uint32_t *ix = dd->indices + cmd->index_offset + k;
            if (k + 5 < cmd->index_count && ix[3] == ix[0] && ix[4] == ix[2] && ix[1] == ix[0] + 1 && ix[2] == ix[0] + 2 && ix[5] == ix[0] + 3) {
                const cui_vertex *q = &dd->vertices[ix[0]];
                if (cui__raster_quad(job, &q[0], &q[1], &q[2], &q[3], tex, cx0, cy0, cx1, cy1)) { k += 3; continue; }
            }
            cui__raster_tri(job, &dd->vertices[ix[0]], &dd->vertices[ix[1]], &dd->vertices[ix[2]], tex, cx0, cy0, cx1, cy1);
        }
    }
}

#if defined(_WIN32)
static DWORD WINAPI cui__sw_thread(LPVOID p) { cui__sw_run(p); return 0; }
#else
static void *cui__sw_thread(void *p) { cui__sw_run(p); return NULL; }
#endif

/* paints one rectangle of the frame, split in bands over the threads */
static void cui__sw_paint_rect(cui_sw_renderer *r, const cui_draw_data *dd, void *pixels, int width, int height, int stride, cui_sw_format fmt,
                               int x0, int y0, int x1, int y1) {
    cui_color cc = dd->clear_color;
    uint8_t c4[4] = { cc.r, cc.g, cc.b, 255 };
    if (fmt == CUI_SW_BGRA) { c4[0] = cc.b; c4[2] = cc.r; }
    for (int y = y0; y < y1; y++) {
        uint8_t *row = (uint8_t *)pixels + (size_t)y * (size_t)stride + (size_t)x0 * 4;
        for (int x = 0, n = x1 - x0; x < n; x++) memcpy(row + (size_t)x * 4, c4, 4);
    }
    int h = y1 - y0;
    int threads = r->threads;
    if (h < 64 || dd->index_count < 600 || (size_t)(x1 - x0) * (size_t)h < 200000) threads = 1;
    if (threads > h / 16) threads = CUI_MAX(1, h / 16);
    cui__sw_job jobs[32];
    int band = (h + threads - 1) / threads;
    for (int t = 0; t < threads; t++) {
        jobs[t].dd = dd; jobs[t].r = r; jobs[t].pixels = pixels;
        jobs[t].width = width; jobs[t].height = height; jobs[t].stride = stride; jobs[t].fmt = fmt;
        jobs[t].x0 = x0; jobs[t].x1 = x1;
        jobs[t].y0 = y0 + t * band; jobs[t].y1 = CUI_MIN(y1, y0 + (t + 1) * band);
    }
    if (threads == 1) { cui__sw_run(&jobs[0]); return; }
#if defined(_WIN32)
    HANDLE hs[32];
    for (int t = 1; t < threads; t++) hs[t] = CreateThread(NULL, 0, cui__sw_thread, &jobs[t], 0, NULL);
    cui__sw_run(&jobs[0]);
    for (int t = 1; t < threads; t++) if (hs[t]) { WaitForSingleObject(hs[t], INFINITE); CloseHandle(hs[t]); } else cui__sw_run(&jobs[t]);
#else
    pthread_t th[32];
    bool ok[32];
    for (int t = 1; t < threads; t++) ok[t] = pthread_create(&th[t], NULL, cui__sw_thread, &jobs[t]) == 0;
    cui__sw_run(&jobs[0]);
    for (int t = 1; t < threads; t++) { if (ok[t]) pthread_join(th[t], NULL); else cui__sw_run(&jobs[t]); }
#endif
}

static inline uint64_t cui__sw_mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h * 0xFF51AFD7ED558CCDull;
}
static uint64_t cui__sw_hash_bytes(const void *p, size_t n, uint64_t h) {
    const uint8_t *b = p;
    while (n >= 8) { uint64_t v; memcpy(&v, b, 8); h = cui__sw_mix(h, v); b += 8; n -= 8; }
    while (n--) h = cui__sw_mix(h, *b++);
    return h;
}

/* each tile's hash: everything drawn over it, in order, with its clip and texture;
 * force marks tiles with a triangle sampling a texture part written since last frame */
static void cui__sw_tile_hashes(cui_sw_renderer *r, const cui_draw_data *dd, uint64_t *tiles, uint8_t *force, int tw, int th) {
    uint64_t seed = cui__sw_hash_bytes(&dd->clear_color, sizeof dd->clear_color, 1469598103934665603ull);
    for (int i = 0; i < tw * th; i++) { tiles[i] = seed; force[i] = 0; }
    for (int ci = 0; ci < dd->command_count; ci++) {
        const cui_draw_cmd *cmd = &dd->commands[ci];
        cui_texture tid = cmd->texture ? cmd->texture : dd->font_atlas;
        uint64_t ch = cui__sw_hash_bytes(&cmd->clip, sizeof cmd->clip, (uint64_t)tid + 7);
        /* a changed part of this command's texture, in texture pixels */
        int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
        float tw_px = 1, th_px = 1;
        bool tex_changed = false;
        for (int k = 0; k < r->nchanged; k++)
            if (r->changed[k].t == tid) {
                int slot = (int)tid - 1;
                if (slot >= 0 && slot < r->tex_count && r->tex[slot].used) { tw_px = (float)r->tex[slot].w; th_px = (float)r->tex[slot].h; }
                cx0 = r->changed[k].x0; cy0 = r->changed[k].y0; cx1 = r->changed[k].x1; cy1 = r->changed[k].y1;
                tex_changed = true;
                break;
            }
        for (uint32_t k = 0; k + 2 < cmd->index_count; k += 3) {
            const uint32_t *ix = dd->indices + cmd->index_offset + k;
            const cui_vertex *a = &dd->vertices[ix[0]], *b = &dd->vertices[ix[1]], *c = &dd->vertices[ix[2]];
            float x0 = CUI_MIN(a->x, CUI_MIN(b->x, c->x)), x1 = CUI_MAX(a->x, CUI_MAX(b->x, c->x));
            float y0 = CUI_MIN(a->y, CUI_MIN(b->y, c->y)), y1 = CUI_MAX(a->y, CUI_MAX(b->y, c->y));
            x0 = CUI_MAX(x0, cmd->clip.x); y0 = CUI_MAX(y0, cmd->clip.y);
            x1 = CUI_MIN(x1, cmd->clip.x + cmd->clip.w); y1 = CUI_MIN(y1, cmd->clip.y + cmd->clip.h);
            if (x1 <= x0 || y1 <= y0) continue;
            uint64_t h = cui__sw_hash_bytes(a, sizeof *a, ch);
            h = cui__sw_hash_bytes(b, sizeof *b, h);
            h = cui__sw_hash_bytes(c, sizeof *c, h);
            bool forced = false;
            if (tex_changed) {
                float u0 = CUI_MIN(a->u, CUI_MIN(b->u, c->u)) * tw_px, u1 = CUI_MAX(a->u, CUI_MAX(b->u, c->u)) * tw_px;
                float v0 = CUI_MIN(a->v, CUI_MIN(b->v, c->v)) * th_px, v1 = CUI_MAX(a->v, CUI_MAX(b->v, c->v)) * th_px;
                forced = u1 >= (float)cx0 - 1 && u0 <= (float)cx1 + 1 && v1 >= (float)cy0 - 1 && v0 <= (float)cy1 + 1;
            }
            int tx0 = CUI_MAX(0, (int)floorf(x0 - 1) / CUI__SW_TILE), ty0 = CUI_MAX(0, (int)floorf(y0 - 1) / CUI__SW_TILE);
            int tx1 = CUI_MIN(tw - 1, (int)ceilf(x1 + 1) / CUI__SW_TILE), ty1 = CUI_MIN(th - 1, (int)ceilf(y1 + 1) / CUI__SW_TILE);
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++) {
                    tiles[ty * tw + tx] = cui__sw_mix(tiles[ty * tw + tx], h);
                    if (forced) force[ty * tw + tx] = 1;
                }
        }
    }
}

void cui_sw_render(cui_sw_renderer *r, const cui_draw_data *dd, void *pixels, int width, int height, int stride, cui_sw_format fmt) {
    if (!r || !dd || !pixels) return;
    if (!r->incremental) {
        r->nchanged = 0; r->changed_overflow = false;
        cui__sw_paint_rect(r, dd, pixels, width, height, stride, fmt, 0, 0, width, height);
        r->damage[0] = 0; r->damage[1] = 0; r->damage[2] = width; r->damage[3] = height;
        return;
    }
    int tw = (width + CUI__SW_TILE - 1) / CUI__SW_TILE, th = (height + CUI__SW_TILE - 1) / CUI__SW_TILE;
    if (tw * th > r->tiles_cap) {
        free(r->tiles); free(r->prev); free(r->force);
        r->tiles_cap = tw * th;
        r->tiles = malloc(sizeof(uint64_t) * (size_t)r->tiles_cap);
        r->prev = malloc(sizeof(uint64_t) * (size_t)r->tiles_cap);
        r->force = malloc((size_t)r->tiles_cap);
        r->last_px = NULL;
        if (!r->tiles || !r->prev || !r->force) {
            free(r->tiles); free(r->prev); free(r->force); r->tiles = r->prev = NULL; r->force = NULL; r->tiles_cap = 0;
            r->incremental = false; cui_sw_render(r, dd, pixels, width, height, stride, fmt); return;
        }
    }
    cui__sw_tile_hashes(r, dd, r->tiles, r->force, tw, th);
    bool full = pixels != r->last_px || width != r->last_w || height != r->last_h || stride != r->last_stride || fmt != r->last_fmt || r->changed_overflow;
    r->last_px = pixels; r->last_w = width; r->last_h = height; r->last_stride = stride; r->last_fmt = fmt;
    r->nchanged = 0; r->changed_overflow = false;
    r->damage[0] = width; r->damage[1] = height; r->damage[2] = 0; r->damage[3] = 0;
    if (full) {
        cui__sw_paint_rect(r, dd, pixels, width, height, stride, fmt, 0, 0, width, height);
        r->damage[0] = 0; r->damage[1] = 0; r->damage[2] = width; r->damage[3] = height;
    } else {
        /* one span of changed tiles per tile row; rows with overlapping spans are joined */
        int rx0 = 0, rx1 = 0, ry0 = -1, ry1 = 0, dirty_tiles = 0;
        for (int ty = 0; ty <= th; ty++) {
            int a = -1, b = -1;
            if (ty < th) for (int tx = 0; tx < tw; tx++) if (r->tiles[ty * tw + tx] != r->prev[ty * tw + tx] || r->force[ty * tw + tx]) { if (a < 0) a = tx; b = tx; dirty_tiles++; }
            if (a >= 0 && ry0 >= 0 && a <= rx1 && b >= rx0 - 1) { rx0 = CUI_MIN(rx0, a); rx1 = CUI_MAX(rx1, b); ry1 = ty; continue; }
            if (ry0 >= 0) {   /* paint the rectangle so far */
                int x0 = rx0 * CUI__SW_TILE, y0 = ry0 * CUI__SW_TILE, x1 = CUI_MIN(width, (rx1 + 1) * CUI__SW_TILE), y1 = CUI_MIN(height, (ry1 + 1) * CUI__SW_TILE);
                cui__sw_paint_rect(r, dd, pixels, width, height, stride, fmt, x0, y0, x1, y1);
                r->damage[0] = CUI_MIN(r->damage[0], x0); r->damage[1] = CUI_MIN(r->damage[1], y0);
                r->damage[2] = CUI_MAX(r->damage[2], x1); r->damage[3] = CUI_MAX(r->damage[3], y1);
                ry0 = -1;
            }
            if (a >= 0) { rx0 = a; rx1 = b; ry0 = ry1 = ty; }
        }
        (void)dirty_tiles;
    }
    uint64_t *t = r->prev; r->prev = r->tiles; r->tiles = t;
}

/* ---- PNG writer ---- */

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
#include "../../third_party/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

bool cui_write_png(const char *path, const void *rgba, int width, int height, int stride) {
    return stbi_write_png(path, width, height, 4, rgba, stride) != 0;
}
