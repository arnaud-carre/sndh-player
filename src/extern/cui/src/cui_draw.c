/* Draw list: every shape becomes vertices of one format. Rounded rectangles
 * and shadows are signed distance fields evaluated per pixel, so corners
 * stay smooth at any size and DPI. Paths use feathered geometry. */
#include "cui_internal.h"

static cui_draw_cmd *cui__cmd(cui_ctx *ui) {
    cui_draw_list *dl = &ui->dl;
    cui_rect clip = dl->clip_stack[dl->clip_depth - 1];
    if (dl->cmd_count > 0) {
        cui_draw_cmd *c = &dl->cmd[dl->cmd_count - 1];
        if (c->texture == dl->cur_tex && memcmp(&c->clip, &clip, sizeof clip) == 0) return c;
        if (c->index_count == 0) { c->texture = dl->cur_tex; c->clip = clip; return c; }
    }
    CUI__GROW(ui, dl->cmd, dl->cmd_count, dl->cmd_cap, 1);
    cui_draw_cmd *c = &dl->cmd[dl->cmd_count++];
    c->clip = clip;
    c->texture = dl->cur_tex;
    c->index_offset = (uint32_t)dl->idx_count;
    c->index_count = 0;
    return c;
}

void cui__draw_reset(cui_ctx *ui) {
    cui_draw_list *dl = &ui->dl;
    dl->vtx_count = dl->idx_count = dl->cmd_count = 0;
    dl->clip_depth = 1;
    cui_rect full = { 0, 0, ui->vw * ui->scale, ui->vh * ui->scale };
    dl->clip_stack[0] = full;
    dl->xform_depth = 1;
    cui_xform id = { 1, 0, 0, 1, 0, 0 };
    dl->xform_stack[0] = id;
    dl->cur_tex = 0;
}

/* Composite transform: p' = p * scale + (tx, ty) (stored in ox/oy), alpha. */
void cui__push_xform(cui_ctx *ui, float s, float ox, float oy, float alpha, float tx, float ty) {
    cui_draw_list *dl = &ui->dl;
    if (dl->xform_depth >= 32) return;
    cui_xform p = dl->xform_stack[dl->xform_depth - 1];
    float sc = ui->scale;
    ox *= sc; oy *= sc; tx *= sc; ty *= sc;
    float bx = ox - ox * s + tx, by = oy - oy * s + ty;
    cui_xform n;
    n.scale = s * p.scale;
    n.ox = bx * p.scale + p.ox;
    n.oy = by * p.scale + p.oy;
    n.alpha = alpha * p.alpha;
    n.tx = n.ty = 0;
    dl->xform_stack[dl->xform_depth++] = n;
}

void cui__pop_xform(cui_ctx *ui) { if (ui->dl.xform_depth > 1) ui->dl.xform_depth--; }

static inline cui_xform cui__xf(cui_ctx *ui) { return ui->dl.xform_stack[ui->dl.xform_depth - 1]; }
static inline bool cui__xf_identity(cui_xform x) { return x.scale == 1 && x.ox == 0 && x.oy == 0 && x.alpha == 1; }

static inline cui_color cui__xa(cui_color c, float a) {
    if (a >= 1) return c;
    c.a = (uint8_t)(c.a * a + 0.5f);
    return c;
}

void cui_push_clip(cui_ctx *ui, cui_rect r) {
    cui_draw_list *dl = &ui->dl;
    cui_xform x = cui__xf(ui);
    float s = ui->scale;
    cui_rect px = { r.x * s * x.scale + x.ox, r.y * s * x.scale + x.oy, r.w * s * x.scale, r.h * s * x.scale };
    /* snap to whole pixels outward */
    float x0 = floorf(px.x), y0 = floorf(px.y), x1 = ceilf(px.x + px.w), y1 = ceilf(px.y + px.h);
    px.x = x0; px.y = y0; px.w = x1 - x0; px.h = y1 - y0;
    cui_rect cur = dl->clip_stack[dl->clip_depth - 1];
    if (dl->clip_depth < 64) dl->clip_stack[dl->clip_depth++] = cui__rect_intersect(cur, px);
}

void cui_pop_clip(cui_ctx *ui) { if (ui->dl.clip_depth > 1) ui->dl.clip_depth--; }

static void cui__set_texture(cui_ctx *ui, cui_texture t) { ui->dl.cur_tex = t; }

/* Reserve vertices/indices in the current command. */
static cui_vertex *cui__reserve(cui_ctx *ui, int nv, int ni, uint32_t *base) {
    cui_draw_list *dl = &ui->dl;
    cui_draw_cmd *c = cui__cmd(ui);
    CUI__GROW(ui, dl->vtx, dl->vtx_count, dl->vtx_cap, nv);
    CUI__GROW(ui, dl->idx, dl->idx_count, dl->idx_cap, ni);
    *base = (uint32_t)dl->vtx_count;
    cui_vertex *v = &dl->vtx[dl->vtx_count];
    memset(v, 0, sizeof(cui_vertex) * (size_t)nv);
    dl->vtx_count += nv;
    c->index_count += (uint32_t)ni;
    return v;
}

static uint32_t *cui__idx(cui_ctx *ui, int ni) {
    cui_draw_list *dl = &ui->dl;
    uint32_t *p = &dl->idx[dl->idx_count];
    dl->idx_count += ni;
    return p;
}

static bool cui__clip_rejects(cui_ctx *ui, float x0, float y0, float x1, float y1) {
    cui_rect c = ui->dl.clip_stack[ui->dl.clip_depth - 1];
    return x1 < c.x || y1 < c.y || x0 > c.x + c.w || y0 > c.y + c.h || c.w <= 0 || c.h <= 0;
}

/* Emits one SDF quad in pixel space (before transform). */
static void cui__sdf_quad(cui_ctx *ui, float cx, float cy, float hw, float hh, float expand,
                          cui_corners rad, float border, int mode,
                          cui_color c_tl, cui_color c_tr, cui_color c_br, cui_color c_bl, cui_color bc,
                          cui_texture tex, float u0, float v0, float u1, float v1) {
    cui_xform x = cui__xf(ui);
    float s = x.scale;
    float ncx = cx * s + x.ox, ncy = cy * s + x.oy;
    float nhw = hw * s, nhh = hh * s, ne = expand * s;
    if (mode == CUI_SHADE_RRECT || mode == CUI_SHADE_IMAGE) ne = CUI_MAX(ne, 1.0f);
    float x0 = ncx - nhw - ne, y0 = ncy - nhh - ne, x1 = ncx + nhw + ne, y1 = ncy + nhh + ne;
    if (cui__clip_rejects(ui, x0, y0, x1, y1)) return;
    float a = x.alpha;
    cui__set_texture(ui, tex);
    uint32_t base;
    cui_vertex *v = cui__reserve(ui, 4, 6, &base);
    float px[4] = { x0, x1, x1, x0 }, py[4] = { y0, y0, y1, y1 };
    float uu[4] = { u0, u1, u1, u0 }, vv[4] = { v0, v0, v1, v1 };
    cui_color cc[4] = { c_tl, c_tr, c_br, c_bl };
    float maxr = CUI_MIN(nhw, nhh);
    for (int i = 0; i < 4; i++) {
        v[i].x = px[i]; v[i].y = py[i];
        v[i].lx = px[i] - ncx; v[i].ly = py[i] - ncy;
        v[i].hw = nhw; v[i].hh = nhh;
        v[i].r_tl = CUI_MIN(rad.tl * s, maxr); v[i].r_tr = CUI_MIN(rad.tr * s, maxr);
        v[i].r_br = CUI_MIN(rad.br * s, maxr); v[i].r_bl = CUI_MIN(rad.bl * s, maxr);
        if (mode == CUI_SHADE_SHADOW) { v[i].r_tl = rad.tl * s; v[i].r_tr = rad.tr * s; v[i].r_br = rad.br * s; v[i].r_bl = rad.bl * s; }
        v[i].border = border * s;
        v[i].mode = (float)mode;
        v[i].color = cui__xa(cc[i], a);
        v[i].border_color = cui__xa(bc, a);
        v[i].u = uu[i]; v[i].v = vv[i];
    }
    /* the uvs above map to the expanded quad; correct them for images */
    if (mode == CUI_SHADE_IMAGE && ne > 0) {
        float du = (u1 - u0) / (2 * nhw), dv = (v1 - v0) / (2 * nhh);
        for (int i = 0; i < 4; i++) {
            v[i].u = u0 + (v[i].lx + nhw) * du;
            v[i].v = v0 + (v[i].ly + nhh) * dv;
        }
    }
    uint32_t *ix = cui__idx(ui, 6);
    ix[0] = base; ix[1] = base + 1; ix[2] = base + 2; ix[3] = base; ix[4] = base + 2; ix[5] = base + 3;
}

static inline cui_corners cui__scale_corners(cui_corners c, float s) { c.tl *= s; c.tr *= s; c.br *= s; c.bl *= s; return c; }

void cui_draw_rect(cui_ctx *ui, cui_rect r, cui_corners rad, cui_color fill) {
    fill = cui_color_of(ui, fill);
    if (fill.a == 0 || r.w <= 0 || r.h <= 0) return;
    float s = ui->scale;
    cui__sdf_quad(ui, (r.x + r.w * 0.5f) * s, (r.y + r.h * 0.5f) * s, r.w * 0.5f * s, r.h * 0.5f * s, 1,
                  cui__scale_corners(rad, s), 0, CUI_SHADE_RRECT, fill, fill, fill, fill, fill, 0, 0, 0, 0, 0);
}

void cui_draw_rect_border(cui_ctx *ui, cui_rect r, cui_corners rad, cui_color fill, cui_color border, float bw) {
    fill = cui_color_of(ui, fill);
    border = cui_color_of(ui, border);
    if (r.w <= 0 || r.h <= 0) return;
    if (bw <= 0 || border.a == 0) { cui_draw_rect(ui, r, rad, fill); return; }
    float s = ui->scale;
    /* hairlines stay at least one device pixel wide */
    float bpx = CUI_MAX(bw * s, 1.0f);
    cui__sdf_quad(ui, (r.x + r.w * 0.5f) * s, (r.y + r.h * 0.5f) * s, r.w * 0.5f * s, r.h * 0.5f * s, 1,
                  cui__scale_corners(rad, s), bpx, CUI_SHADE_RRECT, fill, fill, fill, fill, border, 0, 0, 0, 0, 0);
}

void cui_draw_rect_gradient(cui_ctx *ui, cui_rect r, cui_corners rad, cui_color top, cui_color bottom) {
    top = cui_color_of(ui, top);
    bottom = cui_color_of(ui, bottom);
    if (r.w <= 0 || r.h <= 0) return;
    float s = ui->scale;
    /* vertex colors interpolate over the expanded quad; expansion is 1px so
     * the error is invisible */
    cui__sdf_quad(ui, (r.x + r.w * 0.5f) * s, (r.y + r.h * 0.5f) * s, r.w * 0.5f * s, r.h * 0.5f * s, 1,
                  cui__scale_corners(rad, s), 0, CUI_SHADE_RRECT, top, top, bottom, bottom, top, 0, 0, 0, 0, 0);
}

void cui_draw_shadow(cui_ctx *ui, cui_rect r, cui_corners rad, float blur, float spread, cui_vec2 off, cui_color color) {
    color = cui_color_of(ui, color);
    if (color.a == 0) return;
    float s = ui->scale;
    float sigma = CUI_MAX(blur * 0.5f, 0.5f) * s;
    float hw = (r.w * 0.5f + spread) * s, hh = (r.h * 0.5f + spread) * s;
    if (hw <= 0 || hh <= 0) return;
    cui_corners rr = cui__scale_corners(rad, s);
    rr.tl += spread * s; rr.tr += spread * s; rr.br += spread * s; rr.bl += spread * s;
    float cx = (r.x + r.w * 0.5f + off.x) * s, cy = (r.y + r.h * 0.5f + off.y) * s;
    cui__sdf_quad(ui, cx, cy, hw, hh, sigma * 3.0f, rr, sigma, CUI_SHADE_SHADOW, color, color, color, color, color, 0, 0, 0, 0, 0);
}

void cui_draw_circle(cui_ctx *ui, cui_vec2 c, float radius, cui_color color) {
    cui_rect r = { c.x - radius, c.y - radius, radius * 2, radius * 2 };
    cui_draw_rect(ui, r, CUI_CORNERS(radius), color);
}

void cui_draw_image(cui_ctx *ui, cui_texture tex, cui_rect r, cui_rect uv, cui_corners rad, cui_color tint) {
    tint = cui_color_of(ui, tint);
    if (!tint.a && !cui__is_token(tint)) tint = cui__rgba(255, 255, 255, 255);
    if (!tex || r.w <= 0 || r.h <= 0) return;
    float s = ui->scale;
    cui__sdf_quad(ui, (r.x + r.w * 0.5f) * s, (r.y + r.h * 0.5f) * s, r.w * 0.5f * s, r.h * 0.5f * s, 0,
                  cui__scale_corners(rad, s), 0, CUI_SHADE_IMAGE, tint, tint, tint, tint, tint, tex,
                  uv.x, uv.y, uv.x + uv.w, uv.y + uv.h);
}

void cui__draw_glyph_quad(cui_ctx *ui, float x0, float y0, float x1, float y1,
                          float u0, float v0, float u1, float v1, cui_color c) {
    /* coordinates are already in pixels */
    cui_xform x = cui__xf(ui);
    if (!cui__xf_identity(x)) {
        x0 = x0 * x.scale + x.ox; y0 = y0 * x.scale + x.oy;
        x1 = x1 * x.scale + x.ox; y1 = y1 * x.scale + x.oy;
        c = cui__xa(c, x.alpha);
    }
    if (cui__clip_rejects(ui, x0, y0, x1, y1)) return;
    cui__set_texture(ui, 0);
    uint32_t base;
    cui_vertex *v = cui__reserve(ui, 4, 6, &base);
    float px[4] = { x0, x1, x1, x0 }, py[4] = { y0, y0, y1, y1 };
    float uu[4] = { u0, u1, u1, u0 }, vv[4] = { v0, v0, v1, v1 };
    for (int i = 0; i < 4; i++) {
        v[i].x = px[i]; v[i].y = py[i]; v[i].u = uu[i]; v[i].v = vv[i];
        v[i].mode = CUI_SHADE_TEXT; v[i].color = c;
    }
    uint32_t *ix = cui__idx(ui, 6);
    ix[0] = base; ix[1] = base + 1; ix[2] = base + 2; ix[3] = base; ix[4] = base + 2; ix[5] = base + 3;
}

/* ---- paths (pixel space after transform) ---- */

static void cui__solid_vertex(cui_vertex *v, float x, float y, cui_color c) {
    v->x = x; v->y = y; v->mode = CUI_SHADE_SOLID; v->color = c;
}

static void cui__polyline_px(cui_ctx *ui, const cui_vec2 *pts, int n, float thick, cui_color col, bool closed) {
    if (n < 2) return;
    cui__set_texture(ui, 0);
    int segs = closed ? n : n - 1;
    float aa = 1.0f;
    float hw = thick * 0.5f;
    bool thin = hw < 0.5f;
    if (thin) { col.a = (uint8_t)(col.a * (thick > 0 ? thick : 1)); hw = 0.5f; }
    cui_color trans = col; trans.a = 0;
    /* normals per point */
    cui_vec2 *nrm = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)n);
    for (int i = 0; i < segs; i++) {
        int j = (i + 1) % n;
        float dx = pts[j].x - pts[i].x, dy = pts[j].y - pts[i].y;
        float l = sqrtf(dx * dx + dy * dy);
        if (l > 0) { dx /= l; dy /= l; }
        nrm[i].x = dy; nrm[i].y = -dx;
    }
    if (!closed) nrm[n - 1] = nrm[n - 2];
    uint32_t base;
    int caps = closed ? 0 : 2;   /* open lines get a soft fringe across each end too, so cut ends do not stair-step */
    cui_vertex *v = cui__reserve(ui, n * 4 + caps * 4, segs * 18 + caps * 18, &base);
    for (int i = 0; i < n; i++) {
        cui_vec2 na = (i == 0 && !closed) ? nrm[0] : nrm[(i - 1 + n) % n];
        cui_vec2 nb = nrm[i];
        if (!closed && i == n - 1) nb = na;
        float mx = (na.x + nb.x) * 0.5f, my = (na.y + nb.y) * 0.5f;
        float d2 = mx * mx + my * my;
        if (d2 > 0.000001f) { float inv = 1.0f / d2; if (inv > 25.0f) inv = 25.0f; mx *= inv; my *= inv; }
        float p = hw, o = hw + aa;
        cui__solid_vertex(&v[i * 4 + 0], pts[i].x + mx * o, pts[i].y + my * o, trans);
        cui__solid_vertex(&v[i * 4 + 1], pts[i].x + mx * p, pts[i].y + my * p, col);
        cui__solid_vertex(&v[i * 4 + 2], pts[i].x - mx * p, pts[i].y - my * p, col);
        cui__solid_vertex(&v[i * 4 + 3], pts[i].x - mx * o, pts[i].y - my * o, trans);
    }
    uint32_t *ix = cui__idx(ui, segs * 18 + caps * 18);
    for (int i = 0; i < segs; i++) {
        uint32_t a = base + (uint32_t)i * 4, b = base + (uint32_t)((i + 1) % n) * 4;
        uint32_t q[18] = { a + 0, b + 0, b + 1, a + 0, b + 1, a + 1,
                           a + 1, b + 1, b + 2, a + 1, b + 2, a + 2,
                           a + 2, b + 2, b + 3, a + 2, b + 3, a + 3 };
        memcpy(ix + i * 18, q, sizeof q);
    }
    if (caps) {
        for (int e = 0; e < 2; e++) {
            int end = e == 0 ? 0 : n - 1;
            /* the outward tangent at this end */
            cui_vec2 tn = e == 0 ? (cui_vec2){ -nrm[0].y, nrm[0].x } : (cui_vec2){ nrm[n - 2].y, -nrm[n - 2].x };
            cui_vec2 t = { -tn.x * aa, -tn.y * aa };
            uint32_t a = base + (uint32_t)end * 4, c = base + (uint32_t)(n * 4 + e * 4);
            for (int k = 0; k < 4; k++) {
                cui_vertex *src = &v[end * 4 + k];
                cui__solid_vertex(&v[n * 4 + e * 4 + k], src->x + t.x, src->y + t.y, trans);
            }
            uint32_t q[18] = { a + 0, c + 0, c + 1, a + 0, c + 1, a + 1,
                               a + 1, c + 1, c + 2, a + 1, c + 2, a + 2,
                               a + 2, c + 2, c + 3, a + 2, c + 3, a + 3 };
            memcpy(ix + segs * 18 + e * 18, q, sizeof q);
        }
    }
}

static cui_vec2 *cui__xf_points(cui_ctx *ui, const cui_vec2 *pts, int n) {
    cui_xform x = cui__xf(ui);
    float s = ui->scale * x.scale;
    cui_vec2 *o = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)n);
    for (int i = 0; i < n; i++) { o[i].x = pts[i].x * s + x.ox; o[i].y = pts[i].y * s + x.oy; }
    return o;
}

void cui_draw_polyline(cui_ctx *ui, const cui_vec2 *pts, int n, float thick, cui_color col, bool closed) {
    col = cui_color_of(ui, col);
    if (n < 2 || col.a == 0) return;
    cui_xform x = cui__xf(ui);
    col = cui__xa(col, x.alpha);
    cui__polyline_px(ui, cui__xf_points(ui, pts, n), n, thick * ui->scale * x.scale, col, closed);
}

void cui_draw_line(cui_ctx *ui, cui_vec2 a, cui_vec2 b, float thick, cui_color col) {
    cui_vec2 p[2] = { a, b };
    cui_draw_polyline(ui, p, 2, thick, col, false);
}

void cui_draw_polygon(cui_ctx *ui, const cui_vec2 *pts, int n, cui_color col) {
    col = cui_color_of(ui, col);
    if (n < 3 || col.a == 0) return;
    cui_xform x = cui__xf(ui);
    col = cui__xa(col, x.alpha);
    cui_vec2 *p = cui__xf_points(ui, pts, n);
    cui__set_texture(ui, 0);
    cui_color trans = col; trans.a = 0;
    /* orientation so that normals point outward */
    float area = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += p[i].x * p[j].y - p[j].x * p[i].y; }
    float sign = area < 0 ? -1.0f : 1.0f;
    cui_vec2 *nrm = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)n);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float dx = p[j].x - p[i].x, dy = p[j].y - p[i].y;
        float l = sqrtf(dx * dx + dy * dy);
        if (l > 0) { dx /= l; dy /= l; }
        nrm[i].x = dy * sign; nrm[i].y = -dx * sign;
    }
    uint32_t base;
    cui_vertex *v = cui__reserve(ui, n * 2, (n - 2) * 3 + n * 6, &base);
    for (int i = 0; i < n; i++) {
        cui_vec2 na = nrm[(i - 1 + n) % n], nb = nrm[i];
        float mx = (na.x + nb.x) * 0.5f, my = (na.y + nb.y) * 0.5f;
        float d2 = mx * mx + my * my;
        if (d2 > 0.000001f) { float inv = 1.0f / d2; if (inv > 25.0f) inv = 25.0f; mx *= inv; my *= inv; }
        cui__solid_vertex(&v[i * 2 + 0], p[i].x - mx * 0.5f, p[i].y - my * 0.5f, col);
        cui__solid_vertex(&v[i * 2 + 1], p[i].x + mx * 0.5f, p[i].y + my * 0.5f, trans);
    }
    uint32_t *ix = cui__idx(ui, (n - 2) * 3 + n * 6);
    int k = 0;
    for (int i = 2; i < n; i++) { ix[k++] = base; ix[k++] = base + (uint32_t)(i - 1) * 2; ix[k++] = base + (uint32_t)i * 2; }
    for (int i = 0; i < n; i++) {
        uint32_t a = base + (uint32_t)i * 2, b = base + (uint32_t)((i + 1) % n) * 2;
        ix[k++] = a; ix[k++] = b; ix[k++] = b + 1;
        ix[k++] = a; ix[k++] = b + 1; ix[k++] = a + 1;
    }
}

void cui_draw_arc(cui_ctx *ui, cui_vec2 c, float r, float a0, float a1, float thick, cui_color col) {
    float span = fabsf(a1 - a0);
    if (span <= 0.0001f) return;
    int seg = (int)CUI_CLAMP(span * r * ui->scale / 3.0f, 8, 256);
    cui_vec2 *p = cui__frame_alloc(ui, sizeof(cui_vec2) * (size_t)(seg + 1));
    for (int i = 0; i <= seg; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)seg;
        p[i].x = c.x + cosf(a) * r;
        p[i].y = c.y + sinf(a) * r;
    }
    bool closed = span >= 2 * CUI_PI - 0.001f;
    cui_draw_polyline(ui, p, closed ? seg : seg + 1, thick, col, closed);
}

/* ---- composite helpers used by components ---- */

/* A lit top edge: dark surfaces read as raised when their upper rim catches
 * a little light. Fades out down the sides. */
void cui__draw_top_light(cui_ctx *ui, cui_rect r, cui_corners rad, float alpha) {
    if (r.w < 4 || r.h < 4 || alpha <= 0) return;
    /* ten nested bands, so the light thins out smoothly instead of in visible steps */
    enum { B = 10 };
    float reach = CUI_MIN(r.h * 0.5f, 60.0f);
    for (int k = 1; k <= B; k++) {
        cui_push_clip(ui, (cui_rect){ r.x - 1, r.y - 1, r.w + 2, reach * (float)k / B + 1 });
        cui_draw_rect_border(ui, r, rad, cui__rgba(0, 0, 0, 0), cui__rgba(255, 255, 255, (int)(255 * alpha / B + 0.5f)), 1);
        cui_pop_clip(ui);
    }
}

/* Sheen for solid controls: a lit upper rim that fades down the sides. */
void cui__draw_sheen(cui_ctx *ui, cui_rect r, cui_corners rad, float k) {
    if (r.w < 2 || r.h < 2 || k <= 0) return;
    /* the lit rim fades out down the sides instead of stopping at a line */
    enum { B = 8 };
    for (int i = 1; i <= B; i++) {
        cui_push_clip(ui, (cui_rect){ r.x - 1, r.y - 1, r.w + 2, r.h * 0.42f * (float)i / B + 1 });
        cui_draw_rect_border(ui, r, rad, cui__rgba(0, 0, 0, 0), cui__rgba(255, 255, 255, (int)(255 * 0.16f * k / B + 0.5f)), 1);
        cui_pop_clip(ui);
    }
}


void cui__draw_check(cui_ctx *ui, cui_rect r, float t, float thick, cui_color c) {
    if (t <= 0.001f) return;
    cui_vec2 a = { r.x + r.w * 0.22f, r.y + r.h * 0.53f };
    cui_vec2 b = { r.x + r.w * 0.42f, r.y + r.h * 0.72f };
    cui_vec2 d = { r.x + r.w * 0.78f, r.y + r.h * 0.32f };
    float l1 = hypotf(b.x - a.x, b.y - a.y), l2 = hypotf(d.x - b.x, d.y - b.y);
    float len = (l1 + l2) * cui__clampf(t, 0, 1);
    cui_vec2 pts[3] = { a, b, d };
    int n = 3;
    if (len < l1) {
        float k = len / l1;
        pts[1].x = a.x + (b.x - a.x) * k; pts[1].y = a.y + (b.y - a.y) * k;
        n = 2;
    } else {
        float k = (len - l1) / l2;
        pts[2].x = b.x + (d.x - b.x) * k; pts[2].y = b.y + (d.y - b.y) * k;
    }
    cui_draw_polyline(ui, pts, n, thick, c, false);
}

void cui__draw_spinner(cui_ctx *ui, cui_vec2 c, float radius, float thick, cui_color col) {
    float t = (float)ui->time;
    col = cui_color_of(ui, col);
    cui_color track = cui_alpha(col, 0.18f);
    cui_draw_arc(ui, c, radius, 0, 2 * CUI_PI, thick, track);
    float head = t * 5.2f;
    float len = 1.2f + 0.9f * sinf(t * 2.4f);
    cui_draw_arc(ui, c, radius, head, head + len, thick, col);
    ui->animating = true;
}

void cui__draw_focus_ring(cui_ctx *ui, cui_rect r, cui_corners rad, float alpha) {
    if (alpha <= 0.01f) return;
    float o = 3;
    cui_rect rr = { r.x - o, r.y - o, r.w + 2 * o, r.h + 2 * o };
    cui_corners cr = { rad.tl + o, rad.tr + o, rad.br + o, rad.bl + o };
    cui_color c = cui_alpha(CUI_C(ui, FOCUS_RING), alpha);
    cui_draw_rect_border(ui, rr, cr, cui__rgba(0, 0, 0, 0), c, 2);
}

void cui__draw_elevation(cui_ctx *ui, cui_rect r, cui_corners rad, cui_elevation e, float alpha) {
    if (e == CUI_ELEVATION_NONE || alpha <= 0) return;
    cui_theme *t = &ui->theme;
    const cui_shadow *sh = e == CUI_ELEVATION_RESTING ? t->shadow_resting : e == CUI_ELEVATION_RAISED ? t->shadow_raised : t->shadow_floating;
    for (int i = 1; i >= 0; i--) {
        if (sh[i].alpha <= 0) continue;
        cui_vec2 off = { 0, sh[i].y };
        cui_draw_shadow(ui, r, rad, sh[i].blur, sh[i].spread, off, cui__rgba(0, 0, 0, (int)(255 * sh[i].alpha * alpha)));
    }
}

void cui__draw_triangles(cui_ctx *ui, const cui_cvert *src, int nv, const uint32_t *idx, int ni) {
    if (nv <= 0 || ni <= 0) return;
    cui_xform x = cui__xf(ui);
    float s = ui->scale * x.scale;
    cui__set_texture(ui, 0);
    uint32_t base;
    cui_vertex *v = cui__reserve(ui, nv, ni, &base);
    for (int i = 0; i < nv; i++) {
        v[i].x = src[i].x * s + x.ox;
        v[i].y = src[i].y * s + x.oy;
        v[i].mode = CUI_SHADE_SOLID;
        v[i].color = cui__xa(cui_color_of(ui, src[i].c), x.alpha);
    }
    uint32_t *ix = cui__idx(ui, ni);
    for (int i = 0; i < ni; i++) ix[i] = base + idx[i];
}
