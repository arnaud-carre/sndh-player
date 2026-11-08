/* Image viewer (zoom and pan) and cropper. Both draw the picture with one
 * textured quad cut to the part in view, so a 16x zoom of a large image
 * costs what a fitted one does. */
#include "cui_internal.h"

#define cui__fs cui__fsz

/* draw the part of an image rect (dst, in points) that falls inside clip; where
   it reaches a corner of the clip, that corner is rounded by rad */
static void cui__draw_image_part(cui_ctx *ui, cui_texture tex, cui_rect dst, cui_rect clip, float rad, cui_color tint) {
    cui_rect v = cui__rect_intersect(dst, clip);
    if (v.w <= 0 || v.h <= 0 || dst.w <= 0 || dst.h <= 0) return;
    cui_rect uv = { (v.x - dst.x) / dst.w, (v.y - dst.y) / dst.h, v.w / dst.w, v.h / dst.h };
    bool l = v.x <= clip.x + 0.5f, t = v.y <= clip.y + 0.5f, r = v.x + v.w >= clip.x + clip.w - 0.5f, b = v.y + v.h >= clip.y + clip.h - 0.5f;
    cui_corners c = { l && t ? rad : 0, r && t ? rad : 0, r && b ? rad : 0, l && b ? rad : 0 };
    cui_draw_image(ui, tex, v, uv, c, tint);
}

static void cui__checker(cui_ctx *ui, cui_rect r) {
    cui_color a = ui->mode == CUI_THEME_DARK ? cui__rgba(46, 46, 50, 255) : cui__rgba(236, 236, 238, 255);
    cui_color b = ui->mode == CUI_THEME_DARK ? cui__rgba(58, 58, 62, 255) : cui__rgba(250, 250, 251, 255);
    cui_draw_rect(ui, r, CUI_CORNERS(0), a);
    float s = 10;
    for (float y = 0; y < r.h; y += s)
        for (float x = ((int)(y / s) % 2) * s; x < r.w; x += 2 * s)
            cui_draw_rect(ui, (cui_rect){ r.x + x, r.y + y, CUI_MIN(s, r.w - x), CUI_MIN(s, r.h - y) }, CUI_CORNERS(0), b);
}

/* ------------------------------------------------------------------------ */
/* Image viewer                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring z, x, y; float press_mx, press_my, press_cx, press_cy; bool init, dragging; } cui__iv_state;
typedef struct { cui_texture tex; float iw, ih, z, cx, cy; cui_id wheel; bool checker; } cui__iv_paint;

static void cui__paint_viewer(cui_ctx *ui, cui_node *n, void *data) {
    cui__iv_paint *p = data;
    cui_rect r = n->rect;
    cui__paint_wheel_hit(ui, p->wheel, r);
    cui_rect dst = { r.x + r.w * 0.5f - p->cx * p->z, r.y + r.h * 0.5f - p->cy * p->z, p->iw * p->z, p->ih * p->z };
    if (p->checker) cui__checker(ui, cui__rect_intersect(dst, r));
    cui__draw_image_part(ui, p->tex, dst, r, ui->theme.radius_control, cui__rgba(255, 255, 255, 255));
}

/* keep the image over the view: centered when it is smaller, edges outside when larger */
static void cui__iv_clamp(float *cx, float *cy, float z, float vw, float vh, float iw, float ih) {
    float hw = vw * 0.5f / z, hh = vh * 0.5f / z;
    *cx = iw * z <= vw ? iw * 0.5f : cui__clampf(*cx, hw, iw - hw);
    *cy = ih * z <= vh ? ih * 0.5f : cui__clampf(*cy, hh, ih - hh);
}

bool cui_image_viewer_ex(cui_ctx *ui, const char *id_str, cui_texture tex, int image_w, int image_h, cui_image_view *view, const cui_image_viewer_opts *o) {
    cui_image_viewer_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_image_view own = { 0 };
    if (!view) view = &own;
    cui_image_view before = *view;
    float iw = (float)CUI_MAX(1, image_w), ih = (float)CUI_MAX(1, image_h);
    cui_id id = cui_make_id(ui, id_str ? id_str : "viewer");
    cui_id wid = id ^ 0x1A6E1u;
    cui__iv_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui_rect pr = cui__prev_rect(ui, id);
    float vw = pr.w > 0 ? pr.w : 480, vh = pr.h > 0 ? pr.h : (o->height.kind == CUI_SIZE_FIXED ? o->height.value : 360);
    float fit = CUI_MIN(vw / iw, vh / ih);
    float zmin = CUI_MIN(fit, 1.0f), zmax = CUI_MAX(o->max_zoom > 0 ? o->max_zoom : 16, fit);
    bool fitted = view->zoom <= 0;
    float z = fitted ? fit : cui__clampf(view->zoom, zmin, zmax);
    float cx = fitted ? iw * 0.5f : view->cx, cy = fitted ? ih * 0.5f : view->cy;
    float mcx = pr.x + vw * 0.5f, mcy = pr.y + vh * 0.5f;
    /* zoom to nz keeping the image point under (ax, ay) where it is */
#define CUI__IV_ZOOM(nz, ax, ay) do { \
        float px_ = cx + ((ax) - mcx) / z, py_ = cy + ((ay) - mcy) / z; \
        z = cui__clampf((nz), zmin, zmax); cx = px_ - ((ax) - mcx) / z; cy = py_ - ((ay) - mcy) / z; fitted = false; } while (0)
    if (ui->scroll_target == wid && ui->scroll_dy != 0 && pr.w > 0) {
        CUI__IV_ZOOM(z * powf(1.2f, ui->scroll_dy), ui->mx, ui->my);
        ui->scroll_dy = 0;
    }
    if (r.double_clicked && pr.w > 0) {
        if (z < 0.999f || fitted) CUI__IV_ZOOM(1.0f, ui->mx, ui->my);
        else fitted = true;
        st->dragging = false;
    } else if (r.active && ui->mdown[0] && pr.w > 0) {
        if (ui->mpressed[0] || !st->dragging) { st->press_mx = ui->mx; st->press_my = ui->my; st->press_cx = cx; st->press_cy = cy; st->dragging = true; }
        cx = st->press_cx - (ui->mx - st->press_mx) / z;
        cy = st->press_cy - (ui->my - st->press_my) / z;
        if (cx != before.cx || cy != before.cy) fitted = fitted && cx == iw * 0.5f && cy == ih * 0.5f;
        cui__set_cursor(ui, CUI_CURSOR_MOVE);
    } else st->dragging = false;
    if (r.focused) {
        float step = 48 / z;
        if (cui_key_pressed(ui, CUI_KEY_EQUAL)) CUI__IV_ZOOM(z * 1.5f, mcx, mcy);
        if (cui_key_pressed(ui, CUI_KEY_MINUS)) CUI__IV_ZOOM(z / 1.5f, mcx, mcy);
        if (cui_key_pressed(ui, CUI_KEY_0)) fitted = true;
        if (cui_key_pressed(ui, CUI_KEY_1)) CUI__IV_ZOOM(1.0f, mcx, mcy);
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) { cx -= step; fitted = false; }
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) { cx += step; fitted = false; }
        if (cui_key_pressed(ui, CUI_KEY_UP)) { cy -= step; fitted = false; }
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) { cy += step; fitted = false; }
    }
    if (fitted) { z = fit; cx = iw * 0.5f; cy = ih * 0.5f; }
    cui__iv_clamp(&cx, &cy, z, vw, vh, iw, ih);
    if (fitted) *view = (cui_image_view){ 0, cx, cy };
    else *view = (cui_image_view){ z, cx, cy };
    /* ease toward the view, except while dragging */
    if (!st->init) { st->z.x = z; st->x.x = cx; st->y.x = cy; st->init = true; }
    if (st->dragging) { st->x.x = cx; st->y.x = cy; st->x.v = st->y.v = 0; cui__spring(ui, &st->z, z, CUI_SPRING_SMOOTH); }
    else {
        cui__spring(ui, &st->z, z, 0.28f, 0.0f);
        cui__spring(ui, &st->x, cx, 0.28f, 0.0f);
        cui__spring(ui, &st->y, cy, 0.28f, 0.0f);
    }
    if (r.hovered && !r.active) cui__set_cursor(ui, z > fit + 0.001f ? CUI_CURSOR_MOVE : CUI_CURSOR_ARROW);

    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->w = o->width.kind ? o->width : CUI_FILL;
    nd->h = o->height.kind ? o->height : CUI_PX(360);
    nd->self_align = CUI_ALIGN_STRETCH;
    nd->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, BACKGROUND), CUI_C(ui, FOREGROUND), 0.04f) : cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.035f);
    nd->radius = CUI_CORNERS(ui->theme.radius_control);
    nd->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    cui__iv_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->tex = tex; p->iw = iw; p->ih = ih; p->z = st->z.x; p->cx = st->x.x; p->cy = st->y.x; p->wheel = wid; p->checker = o->checkerboard;
    nd->paint = cui__paint_viewer;
    nd->paint_data = p;
    if (!o->no_toolbar) {
        /* zoom out, the percentage, zoom in and fit, in a pill in the corner */
        cui__open(ui, n);
        cui_push_id(ui, id_str ? id_str : "viewer");
        float bw = 172, bh = 32;
        int bar = cui__absolute(ui, id ^ 0x1A6E2u, vw - bw - 10, vh - bh - 10, bw, bh);
        cui_node *bn = &ui->nodes[bar];
        bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER; bn->pad[1] = bn->pad[3] = 3; bn->gap = 0;
        bn->bg = cui_alpha(CUI_C(ui, SURFACE_RAISED), 0.94f);
        bn->radius = CUI_CORNERS(bh * 0.5f);
        bn->border = 1; bn->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui__rgba(0, 0, 0, 22);
        bn->elevation = CUI_ELEVATION_RAISED; bn->elevation_alpha = 1;
        bn->flags |= CUI_NF_HIT_OPAQUE;
        cui__open(ui, bar);
        cui_button_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST;
        bo.tooltip = "Zoom out";
        if (cui_icon_button_ex(ui, CUI_ICON_MINUS, &bo)) { fitted = false; CUI__IV_ZOOM(z / 1.5f, mcx, mcy); cui__iv_clamp(&cx, &cy, z, vw, vh, iw, ih); *view = (cui_image_view){ z, cx, cy }; }
        int pt = cui__text_node(ui, cui__frame_printf(ui, "%.0f%%", (double)(z * 100)), -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
        ui->nodes[pt].w = CUI_FILL; ui->nodes[pt].text_align = CUI_TEXT_CENTER; ui->nodes[pt].flags |= CUI_NF_TABULAR;
        bo.tooltip = "Zoom in";
        if (cui_icon_button_ex(ui, CUI_ICON_PLUS, &bo)) { fitted = false; CUI__IV_ZOOM(z * 1.5f, mcx, mcy); cui__iv_clamp(&cx, &cy, z, vw, vh, iw, ih); *view = (cui_image_view){ z, cx, cy }; }
        bo.tooltip = "Fit";
        if (cui_icon_button_ex(ui, CUI_ICON_MAXIMIZE_2, &bo)) *view = (cui_image_view){ 0, iw * 0.5f, ih * 0.5f };
        cui__close(ui);
        cui_pop_id(ui);
        cui__close(ui);
    }
#undef CUI__IV_ZOOM
    float moving = fabsf(st->z.v) + fabsf(st->x.v) + fabsf(st->y.v);
    if (moving > 0.001f) ui->animating = true;
    ui->last_id = id;
    return memcmp(&before, view, sizeof before) != 0;
}

/* ------------------------------------------------------------------------ */
/* Cropper                                                                  */
/* ------------------------------------------------------------------------ */

enum { CUI__CR_NONE = 0, CUI__CR_MOVE, CUI__CR_NEW, CUI__CR_N, CUI__CR_S, CUI__CR_E, CUI__CR_W, CUI__CR_NE, CUI__CR_NW, CUI__CR_SE, CUI__CR_SW };
typedef struct { int mode, hover; float px, py; cui_rect start; float active_t; bool init; } cui__cr_state;
typedef struct { cui_texture tex; float iw, ih; cui_rect crop; bool round; float active; int hover; } cui__cr_paint;

typedef struct { float z, ox, oy; } cui__cr_geo;
static cui__cr_geo cui__cr_geom(cui_rect r, float iw, float ih) {
    float pad = 14;
    float z = CUI_MIN((r.w - pad * 2) / iw, (r.h - pad * 2) / ih);
    if (z <= 0) z = 0.0001f;
    cui__cr_geo g = { z, r.x + (r.w - iw * z) * 0.5f, r.y + (r.h - ih * z) * 0.5f };
    return g;
}

/* the dim outside a crop: four bands, and four filled corners for a round mask */
static void cui__cr_dim(cui_ctx *ui, cui_rect img, cui_rect h, float rr, cui_color c) {
    float sc = ui->scale > 0 ? ui->scale : 1;   /* whole device pixels: the bands meet without a seam */
    float x0 = roundf(h.x * sc) / sc, y0 = roundf(h.y * sc) / sc, x1 = roundf((h.x + h.w) * sc) / sc, y1 = roundf((h.y + h.h) * sc) / sc;
    h = (cui_rect){ x0, y0, x1 - x0, y1 - y0 };
    cui_draw_rect(ui, (cui_rect){ img.x, img.y, img.w, CUI_MAX(0.0f, y0 - img.y) }, CUI_CORNERS(0), c);
    cui_draw_rect(ui, (cui_rect){ img.x, y1, img.w, CUI_MAX(0.0f, img.y + img.h - y1) }, CUI_CORNERS(0), c);
    cui_draw_rect(ui, (cui_rect){ img.x, y0, CUI_MAX(0.0f, x0 - img.x), h.h }, CUI_CORNERS(0), c);
    cui_draw_rect(ui, (cui_rect){ x1, y0, CUI_MAX(0.0f, img.x + img.w - x1), h.h }, CUI_CORNERS(0), c);
    if (rr <= 0.5f) return;
    enum { SEG = 12 };
    cui_cvert v[SEG + 2];
    uint32_t idx[SEG * 3];
    for (int k = 0; k < SEG; k++) { idx[k * 3] = 0; idx[k * 3 + 1] = (uint32_t)(k + 1); idx[k * 3 + 2] = (uint32_t)(k + 2); }
    const float cx[4] = { x0, x1, x1, x0 }, cy[4] = { y0, y0, y1, y1 };
    const float ox[4] = { x0 + rr, x1 - rr, x1 - rr, x0 + rr }, oy[4] = { y0 + rr, y0 + rr, y1 - rr, y1 - rr };
    for (int q = 0; q < 4; q++) {
        float a0 = CUI_PI + q * CUI_PI * 0.5f;
        v[0] = (cui_cvert){ cx[q], cy[q], c };
        for (int k = 0; k <= SEG; k++) {
            float a = a0 + CUI_PI * 0.5f * (float)k / SEG;
            v[k + 1] = (cui_cvert){ ox[q] + cosf(a) * rr, oy[q] + sinf(a) * rr, c };
        }
        cui__draw_triangles(ui, v, SEG + 2, idx, SEG * 3);
    }
}

static void cui__paint_cropper(cui_ctx *ui, cui_node *n, void *data) {
    cui__cr_paint *p = data;
    cui_rect r = n->rect;
    cui__cr_geo g = cui__cr_geom(r, p->iw, p->ih);
    cui_rect img = { g.ox, g.oy, p->iw * g.z, p->ih * g.z };
    cui__draw_image_part(ui, p->tex, img, r, ui->theme.radius_control, cui__rgba(255, 255, 255, 255));
    cui_rect c = { g.ox + p->crop.x * g.z, g.oy + p->crop.y * g.z, p->crop.w * g.z, p->crop.h * g.z };
    float rr = p->round ? CUI_MIN(c.w, c.h) * 0.5f : 0;
    cui__cr_dim(ui, img, c, rr, cui__rgba(0, 0, 0, 140));
    cui_color white = cui__rgba(255, 255, 255, 255);
    /* thirds while dragging */
    if (p->active > 0.01f) {
        cui_color gl = cui__rgba(255, 255, 255, (int)(120 * p->active));
        for (int k = 1; k < 3; k++) {
            cui_draw_rect(ui, (cui_rect){ c.x + c.w * k / 3.0f - 0.5f, c.y, 1, c.h }, CUI_CORNERS(0), gl);
            cui_draw_rect(ui, (cui_rect){ c.x, c.y + c.h * k / 3.0f - 0.5f, c.w, 1 }, CUI_CORNERS(0), gl);
        }
    }
    if (p->round) cui_draw_arc(ui, (cui_vec2){ c.x + c.w * 0.5f, c.y + c.h * 0.5f }, rr, 0, 2 * CUI_PI, 1.5f, white);
    cui_draw_rect_border(ui, c, CUI_CORNERS(0), cui__rgba(0, 0, 0, 0), p->round ? cui__rgba(255, 255, 255, 110) : white, p->round ? 1 : 1.5f);
    /* corner brackets and edge bars */
    float L = CUI_MIN(18.0f, CUI_MIN(c.w, c.h) * 0.3f), T = 3;
    cui_color shade = cui__rgba(0, 0, 0, 60);
    const float qx[4] = { c.x, c.x + c.w, c.x, c.x + c.w }, qy[4] = { c.y, c.y, c.y + c.h, c.y + c.h };
    for (int k = 0; k < 4; k++) {
        float sx = k % 2 ? -1 : 1, sy = k < 2 ? 1 : -1;
        cui_rect hbar = { sx > 0 ? qx[k] - 1.5f : qx[k] - L + 1.5f, sy > 0 ? qy[k] - 1.5f : qy[k] - T + 1.5f, L, T };
        cui_rect vbar = { sx > 0 ? qx[k] - 1.5f : qx[k] - T + 1.5f, sy > 0 ? qy[k] - 1.5f : qy[k] - L + 1.5f, T, L };
        cui_draw_shadow(ui, hbar, CUI_CORNERS(1), 3, 0, (cui_vec2){ 0, 0 }, shade);
        cui_draw_rect(ui, hbar, CUI_CORNERS(1), white);
        cui_draw_rect(ui, vbar, CUI_CORNERS(1), white);
    }
    if (!p->round && c.w > 60 && c.h > 60) {
        float m = 16;
        cui_draw_rect(ui, (cui_rect){ c.x + c.w * 0.5f - m * 0.5f, c.y - 1.5f, m, T }, CUI_CORNERS(1), white);
        cui_draw_rect(ui, (cui_rect){ c.x + c.w * 0.5f - m * 0.5f, c.y + c.h - 1.5f, m, T }, CUI_CORNERS(1), white);
        cui_draw_rect(ui, (cui_rect){ c.x - 1.5f, c.y + c.h * 0.5f - m * 0.5f, T, m }, CUI_CORNERS(1), white);
        cui_draw_rect(ui, (cui_rect){ c.x + c.w - 1.5f, c.y + c.h * 0.5f - m * 0.5f, T, m }, CUI_CORNERS(1), white);
    }
    /* the size, while dragging */
    if (p->active > 0.01f) {
        const char *s = cui__frame_printf(ui, "%.0f \xc3\x97 %.0f", (double)p->crop.w, (double)p->crop.h);
        float fs = cui__fs(ui, ui->theme.text_xs);
        float tw = cui__text_width(ui, CUI_FONT_MEDIUM, fs, s, -1, true) + 14, th = 20;
        cui_rect tag = { c.x + (c.w - tw) * 0.5f, CUI_MIN(c.y + c.h + 8, r.y + r.h - th - 2), tw, th };
        cui_draw_rect(ui, tag, CUI_CORNERS(th * 0.5f), cui__rgba(0, 0, 0, (int)(170 * p->active)));
        float fh = cui__font_line_height(ui, CUI_FONT_MEDIUM, fs);
        cui__draw_text_line(ui, CUI_FONT_MEDIUM, fs, tag.x + 7, tag.y + (th - fh) * 0.5f, cui__rgba(255, 255, 255, (int)(255 * p->active)), s, -1, true);
    }
}

/* the handle under a point (screen), given the crop on screen */
static int cui__cr_hit(cui_rect c, float x, float y, bool round) {
    float t = 12;
    bool l = fabsf(x - c.x) < t, rr = fabsf(x - (c.x + c.w)) < t, tp = fabsf(y - c.y) < t, b = fabsf(y - (c.y + c.h)) < t;
    bool inx = x > c.x - t && x < c.x + c.w + t, iny = y > c.y - t && y < c.y + c.h + t;
    if (inx && iny) {
        if (tp && l) return CUI__CR_NW;
        if (tp && rr) return CUI__CR_NE;
        if (b && l) return CUI__CR_SW;
        if (b && rr) return CUI__CR_SE;
        if (!round) {
            if (tp) return CUI__CR_N;
            if (b) return CUI__CR_S;
            if (l) return CUI__CR_W;
            if (rr) return CUI__CR_E;
        }
    }
    if (x >= c.x && x <= c.x + c.w && y >= c.y && y <= c.y + c.h) return CUI__CR_MOVE;
    return CUI__CR_NONE;
}

static cui_rect cui__cr_fit_aspect(float iw, float ih, float aspect) {
    if (aspect <= 0) return (cui_rect){ iw * 0.1f, ih * 0.1f, iw * 0.8f, ih * 0.8f };
    float w = iw, h = iw / aspect;
    if (h > ih) { h = ih; w = ih * aspect; }
    w *= 0.86f; h *= 0.86f;
    return (cui_rect){ (iw - w) * 0.5f, (ih - h) * 0.5f, w, h };
}

bool cui_cropper_ex(cui_ctx *ui, const char *id_str, cui_texture tex, int image_w, int image_h, cui_rect *crop, const cui_cropper_opts *o) {
    cui_cropper_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_rect own = { 0 };
    if (!crop) crop = &own;
    float iw = (float)CUI_MAX(1, image_w), ih = (float)CUI_MAX(1, image_h);
    float aspect = o->round ? 1 : o->aspect;
    float mins = CUI_MIN(o->min_size > 0 ? o->min_size : 16, CUI_MIN(iw, ih));
    cui_id id = cui_make_id(ui, id_str ? id_str : "cropper");
    cui__cr_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_DRAG);
    cui_rect pr = cui__prev_rect(ui, id);
    if (crop->w <= 0 || crop->h <= 0) *crop = cui__cr_fit_aspect(iw, ih, aspect);
    cui_rect before = *crop;
    cui__cr_geo g = cui__cr_geom(pr.w > 0 ? pr : (cui_rect){ 0, 0, 480, 360 }, iw, ih);
    cui_rect sc = { g.ox + crop->x * g.z, g.oy + crop->y * g.z, crop->w * g.z, crop->h * g.z };
    float mx = (ui->mx - g.ox) / g.z, my = (ui->my - g.oy) / g.z;   /* the pointer in image pixels */
    int hover = r.hovered && pr.w > 0 ? cui__cr_hit(sc, ui->mx, ui->my, o->round) : CUI__CR_NONE;
    if (r.active && ui->mpressed[0] && pr.w > 0) {
        st->mode = hover != CUI__CR_NONE ? hover : CUI__CR_NEW;
        st->px = mx; st->py = my; st->start = *crop;
    }
    if (r.active && ui->mdown[0] && st->mode != CUI__CR_NONE && pr.w > 0) {
        cui_rect s0 = st->start;
        float dx = mx - st->px, dy = my - st->py;
        cui_rect c = s0;
        int m = st->mode;
        if (m == CUI__CR_MOVE) {
            c.x = cui__clampf(s0.x + dx, 0, iw - s0.w);
            c.y = cui__clampf(s0.y + dy, 0, ih - s0.h);
        } else {
            /* the fixed corner or edge, and the moving one */
            float ax = s0.x, ay = s0.y, bx = s0.x + s0.w, by = s0.y + s0.h;
            if (m == CUI__CR_NEW) { ax = st->px; ay = st->py; bx = mx; by = my; }
            if (m == CUI__CR_W || m == CUI__CR_NW || m == CUI__CR_SW) ax = s0.x + dx;
            if (m == CUI__CR_E || m == CUI__CR_NE || m == CUI__CR_SE) bx = s0.x + s0.w + dx;
            if (m == CUI__CR_N || m == CUI__CR_NW || m == CUI__CR_NE) ay = s0.y + dy;
            if (m == CUI__CR_S || m == CUI__CR_SW || m == CUI__CR_SE) by = s0.y + s0.h + dy;
            bool fix_left = m == CUI__CR_E || m == CUI__CR_NE || m == CUI__CR_SE || (m == CUI__CR_NEW && bx >= ax);
            bool fix_top = m == CUI__CR_S || m == CUI__CR_SW || m == CUI__CR_SE || (m == CUI__CR_NEW && by >= ay);
            if (m == CUI__CR_NEW) {
                float x0 = CUI_MIN(ax, bx), x1 = CUI_MAX(ax, bx), y0 = CUI_MIN(ay, by), y1 = CUI_MAX(ay, by);
                ax = x0; bx = x1; ay = y0; by = y1;
            }
            float w = CUI_MAX(bx - ax, mins), h = CUI_MAX(by - ay, mins);
            if (aspect > 0) {
                /* an edge sets one side and the other follows, around the middle; a corner takes the larger change */
                if (m == CUI__CR_N || m == CUI__CR_S) w = h * aspect;
                else if (m == CUI__CR_E || m == CUI__CR_W) h = w / aspect;
                else if (w / aspect > h) h = w / aspect;
                else w = h * aspect;
            }
            /* stay inside the image */
            float maxw = fix_left ? iw - (m == CUI__CR_NEW ? ax : s0.x) : (m == CUI__CR_N || m == CUI__CR_S ? iw : s0.x + s0.w);
            float maxh = fix_top ? ih - (m == CUI__CR_NEW ? ay : s0.y) : (m == CUI__CR_E || m == CUI__CR_W ? ih : s0.y + s0.h);
            if (w > maxw) { w = maxw; if (aspect > 0) h = w / aspect; }
            if (h > maxh) { h = maxh; if (aspect > 0) w = h * aspect; }
            if (m == CUI__CR_NEW) { c.x = st->px <= mx ? st->px : st->px - w; c.y = st->py <= my ? st->py : st->py - h; }
            else {
                c.x = fix_left ? s0.x : (m == CUI__CR_N || m == CUI__CR_S ? s0.x + (s0.w - w) * 0.5f : s0.x + s0.w - w);
                c.y = fix_top ? s0.y : (m == CUI__CR_E || m == CUI__CR_W ? s0.y + (s0.h - h) * 0.5f : s0.y + s0.h - h);
            }
            c.w = w; c.h = h;
            c.x = cui__clampf(c.x, 0, iw - c.w); c.y = cui__clampf(c.y, 0, ih - c.h);
        }
        *crop = c;
    } else st->mode = CUI__CR_NONE;
    if (r.focused) {
        float k = (ui->mods & CUI_MOD_SHIFT) ? 10 : 1;
        if (cui_key_pressed(ui, CUI_KEY_LEFT)) crop->x -= k;
        if (cui_key_pressed(ui, CUI_KEY_RIGHT)) crop->x += k;
        if (cui_key_pressed(ui, CUI_KEY_UP)) crop->y -= k;
        if (cui_key_pressed(ui, CUI_KEY_DOWN)) crop->y += k;
    }
    crop->w = cui__clampf(crop->w, mins, iw); crop->h = cui__clampf(crop->h, mins, ih);
    crop->x = cui__clampf(crop->x, 0, iw - crop->w); crop->y = cui__clampf(crop->y, 0, ih - crop->h);
    cui__tween(ui, &st->active_t, st->mode != CUI__CR_NONE ? 1.0f : 0.0f, CUI__T_STATE);
    int hv = st->mode != CUI__CR_NONE ? st->mode : hover;
    if (hv == CUI__CR_MOVE) cui__set_cursor(ui, CUI_CURSOR_MOVE);
    else if (hv == CUI__CR_N || hv == CUI__CR_S) cui__set_cursor(ui, CUI_CURSOR_RESIZE_NS);
    else if (hv == CUI__CR_E || hv == CUI__CR_W) cui__set_cursor(ui, CUI_CURSOR_RESIZE_EW);
    else if (hv >= CUI__CR_NE) cui__set_cursor(ui, CUI_CURSOR_MOVE);
    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->w = o->width.kind ? o->width : CUI_FILL;
    nd->h = o->height.kind ? o->height : CUI_PX(360);
    nd->self_align = CUI_ALIGN_STRETCH;
    nd->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, BACKGROUND), CUI_C(ui, FOREGROUND), 0.04f) : cui__rgba(24, 24, 27, 255);
    nd->radius = CUI_CORNERS(ui->theme.radius_control);
    nd->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    cui__cr_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->tex = tex; p->iw = iw; p->ih = ih; p->crop = *crop; p->round = o->round; p->active = st->active_t; p->hover = hover;
    nd->paint = cui__paint_cropper;
    nd->paint_data = p;
    ui->last_id = id;
    return memcmp(&before, crop, sizeof before) != 0;
}
