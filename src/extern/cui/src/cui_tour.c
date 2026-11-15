/* Guided tour: a dimmed window with a rounded spotlight on one control and
 * a card beside it. The scrim is painted in one pass (four bands and four
 * filled corners around the hole) and catches the pointer everywhere but
 * the hole, so the spotlighted control can still be tried. */
#include "cui_internal.h"

#define cui__fs cui__fsz

typedef struct { cui_spring x, y, w, h, show; int shown_step, hole_step; bool init, gliding; } cui__tour_state;
typedef struct { cui_rect hole; float radius, alpha; bool has_hole; cui_color scrim, ring; } cui__tour_paint;

/* the scrim around a rounded hole */
static void cui__paint_tour(cui_ctx *ui, cui_node *n, void *data) {
    cui__tour_paint *p = data;
    cui_rect r = n->rect;
    cui_color c = cui_alpha(p->scrim, p->alpha);
    if (!p->has_hole) { cui_draw_rect(ui, r, CUI_CORNERS(0), c); return; }
    /* on whole device pixels, so the bands meet without a seam */
    float sc = ui->scale > 0 ? ui->scale : 1;
    float x0 = roundf(p->hole.x * sc) / sc, y0 = roundf(p->hole.y * sc) / sc;
    float x1 = roundf((p->hole.x + p->hole.w) * sc) / sc, y1 = roundf((p->hole.y + p->hole.h) * sc) / sc;
    cui_rect h = { x0, y0, x1 - x0, y1 - y0 };
    cui_draw_rect(ui, (cui_rect){ r.x, r.y, r.w, CUI_MAX(0.0f, y0 - r.y) }, CUI_CORNERS(0), c);
    cui_draw_rect(ui, (cui_rect){ r.x, y1, r.w, CUI_MAX(0.0f, r.y + r.h - y1) }, CUI_CORNERS(0), c);
    cui_draw_rect(ui, (cui_rect){ r.x, y0, CUI_MAX(0.0f, x0 - r.x), h.h }, CUI_CORNERS(0), c);
    cui_draw_rect(ui, (cui_rect){ x1, y0, CUI_MAX(0.0f, r.x + r.w - x1), h.h }, CUI_CORNERS(0), c);
    /* each corner of the hole: the square corner less a quarter circle */
    float rr = CUI_MIN(p->radius, CUI_MIN(h.w, h.h) * 0.5f);
    if (rr > 0.5f) {
        enum { SEG = 8 };
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
    cui_draw_rect_border(ui, cui__rect_inset(h, -1.5f), CUI_CORNERS(rr + 1.5f), cui__rgba(0, 0, 0, 0), cui_alpha(p->ring, p->alpha), 1.5f);
}

int cui_tour_ex(cui_ctx *ui, const char *id_str, const cui_tour_step *steps, int count, int *step, const cui_tour_opts *o) {
    cui_tour_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!step || !steps || count <= 0 || *step < 0) return CUI_TOUR_NONE;
    if (*step >= count) *step = count - 1;
    int result = CUI_TOUR_NONE;
    cui_id id = cui_make_id(ui, id_str ? id_str : "tour");
    cui__tour_state *st = cui_state(ui, id, sizeof *st);
    const cui_tour_step *s = &steps[*step];
    float pad = o->padding > 0 ? o->padding : 6;
    cui_rect tr = s->target && cui__has_prev_rect(ui, s->target) ? cui__prev_rect(ui, s->target) : (cui_rect){ 0, 0, 0, 0 };
    bool has = tr.w > 0 && tr.h > 0;
    cui_rect hole = { tr.x - pad, tr.y - pad, tr.w + pad * 2, tr.h + pad * 2 };
    if (!st->init || !has) {
        /* the first step, or a card in the middle: the spotlight starts where it is going */
        st->x.x = hole.x; st->y.x = hole.y; st->w.x = hole.w; st->h.x = hole.h;
        st->x.v = st->y.v = st->w.v = st->h.v = 0;
        if (!st->init) { st->show.x = 0; st->shown_step = st->hole_step = *step; }
        st->init = true;
    }
    /* the spotlight glides to a new step's control, and otherwise sticks to it (scrolling, resizing) */
    if (st->hole_step != *step) { st->hole_step = *step; st->gliding = true; }
    if (st->gliding) {
        cui__spring(ui, &st->x, hole.x, CUI_SPRING_MORPH);
        cui__spring(ui, &st->y, hole.y, CUI_SPRING_MORPH);
        cui__spring(ui, &st->w, hole.w, CUI_SPRING_MORPH);
        cui__spring(ui, &st->h, hole.h, CUI_SPRING_MORPH);
        float off = fabsf(st->x.x - hole.x) + fabsf(st->y.x - hole.y) + fabsf(st->w.x - hole.w) + fabsf(st->h.x - hole.h);
        if (off < 0.5f && fabsf(st->x.v) + fabsf(st->y.v) + fabsf(st->w.v) + fabsf(st->h.v) < 0.5f) st->gliding = false;
    }
    if (!st->gliding) { st->x.x = hole.x; st->y.x = hole.y; st->w.x = hole.w; st->h.x = hole.h; st->x.v = st->y.v = st->w.v = st->h.v = 0; }
    cui__spring(ui, &st->show, 1, CUI__SPRING_ENTER);
    float alpha = cui__clampf(st->show.x, 0, 1);
    int z = 1900;   /* above dialogs and popups, which a tour may point into */

    /* the scrim, catching the pointer around the hole */
    int root = cui__float_begin(ui, id, z, 0, CUI__PLACE_FULL, 0);
    cui_node *rn = &ui->nodes[root];
    rn->dir = CUI_STACK;
    cui__tour_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->hole = (cui_rect){ st->x.x, st->y.x, st->w.x, st->h.x };
    p->has_hole = has; p->radius = 10; p->alpha = alpha;
    p->scrim = CUI_C(ui, SCRIM);
    p->ring = ui->mode == CUI_THEME_DARK ? cui__rgba(255, 255, 255, 200) : cui__rgba(255, 255, 255, 235);
    rn->paint = cui__paint_tour; rn->paint_data = p;
    {
        cui_rect h = p->hole;
        float vw = ui->vw, vh = ui->vh;
        cui_rect bands[4] = { { 0, 0, vw, h.y }, { 0, h.y + h.h, vw, vh - h.y - h.h }, { 0, h.y, h.x, h.h }, { h.x + h.w, h.y, vw - h.x - h.w, h.h } };
        int nb = has && !o->block_target ? 4 : 0;
        if (!nb) { bands[0] = (cui_rect){ 0, 0, vw, vh }; nb = 1; }
        for (int k = 0; k < nb; k++) {
            if (bands[k].w <= 0 || bands[k].h <= 0) continue;
            cui_id bid = id ^ (0x7B0u + (unsigned)k);
            cui__interact(ui, bid, 0);
            int b = cui__absolute(ui, bid, bands[k].x, bands[k].y, bands[k].w, bands[k].h);
            ui->nodes[b].flags |= CUI_NF_HIT_OPAQUE;
            cui__mark_interactive(ui, b, 0);
        }
    }
    cui__float_end(ui);

    /* the card: beside the target, or in the middle */
    cui_id cid = id ^ 0xCA2Du;
    int card;
    if (has) card = cui__float_begin(ui, cid, z + 1, s->target, s->placement, pad + 10);
    else card = cui__float_begin(ui, cid, z + 1, 0, CUI__PLACE_CENTER, 0);
    cui_node *cn = &ui->nodes[card];
    cn->bg = CUI_C(ui, SURFACE_RAISED);
    cn->border = 1;
    cn->border_color = ui->mode == CUI_THEME_DARK ? CUI_C(ui, BORDER) : cui_alpha(cui__rgba(0, 0, 0, 255), 0.06f);
    cn->radius = CUI_CORNERS(ui->theme.radius_panel);
    cn->elevation = CUI_ELEVATION_FLOATING; cn->elevation_alpha = 1;
    cn->pad[0] = cn->pad[2] = 16; cn->pad[1] = cn->pad[3] = 18;
    cn->gap = 8;
    cn->w = CUI_PX(CUI_MIN(o->width > 0 ? o->width : 300, ui->vw - 24));
    cn->flags |= CUI_NF_HIT_OPAQUE;
    cn->opacity = alpha;
    if (st->shown_step != *step) { st->shown_step = *step; st->show.x = CUI_MIN(st->show.x, 0.6f); }   /* a new step fades the card in again */
    cn->ty = (1 - alpha) * 6;
    cui_push_id(ui, id_str ? id_str : "tour");
    float fs = cui__fs(ui, ui->theme.text_sm), fx = cui__fs(ui, ui->theme.text_xs);
    /* top: the step count and a close button */
    int top = cui__node_new(ui, 0);
    ui->nodes[top].dir = CUI_ROW; ui->nodes[top].align = CUI_ALIGN_CENTER; ui->nodes[top].w = CUI_FILL;
    cui__open(ui, top);
    int sc = cui__text_node(ui, cui__frame_printf(ui, o->count_format ? o->count_format : "%d of %d", *step + 1, count), -1, CUI_FONT_MEDIUM, fx, CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[sc].w = CUI_FILL; ui->nodes[sc].flags |= CUI_NF_TABULAR; ui->nodes[sc].line_height = 1.4f;
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST; bo.tooltip = "End the tour";
    if (cui_icon_button_ex(ui, CUI_ICON_X, &bo)) result = CUI_TOUR_SKIPPED;
    ui->nodes[ui->last_node].w = CUI_PX(24); ui->nodes[ui->last_node].h = CUI_PX(24);
    cui__close(ui);
    if (s->title && *s->title) {
        int t = cui__text_node(ui, s->title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body + 1), CUI_C(ui, FOREGROUND), true);
        ui->nodes[t].line_height = 1.35f; ui->nodes[t].w = CUI_FILL;
    }
    if (s->body && *s->body) {
        int b = cui__text_node(ui, s->body, -1, CUI_FONT_BODY, fs, CUI_C(ui, TEXT_SECONDARY), true);
        ui->nodes[b].line_height = 1.5f; ui->nodes[b].w = CUI_FILL;
    }
    /* footer: progress dots, Back and Next */
    int foot = cui__node_new(ui, 0);
    ui->nodes[foot].dir = CUI_ROW; ui->nodes[foot].align = CUI_ALIGN_CENTER; ui->nodes[foot].gap = 6; ui->nodes[foot].w = CUI_FILL;
    ui->nodes[foot].pad[0] = 6;
    cui__open(ui, foot);
    if (count <= 12)
        for (int k = 0; k < count; k++) {
            int dot = cui__leaf(ui, 0, CUI_PX(k == *step ? 14 : 6), CUI_PX(6));
            ui->nodes[dot].radius = CUI_CORNERS(3);
            ui->nodes[dot].bg = k == *step ? CUI_C(ui, FOREGROUND) : CUI_C(ui, BORDER_STRONG);
        }
    cui_spacer(ui);
    if (*step > 0) {
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST;
        if (cui_button_ex(ui, o->back_label ? o->back_label : "Back", &bo)) result = CUI_TOUR_BACK;
    }
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_PRIMARY;
    bool last = *step == count - 1;
    if (cui_button_ex(ui, last ? (o->done_label ? o->done_label : "Done") : (o->next_label ? o->next_label : "Next"), &bo))
        result = last ? CUI_TOUR_DONE : CUI_TOUR_NEXT;
    cui__close(ui);
    cui_pop_id(ui);
    cui__float_end(ui);

    /* keys, unless a text field or a focused button has them */
    bool button_keys = ui->focus && (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE)) && ui->focus_visible;
    if (result == CUI_TOUR_NONE && !ui->edit_prev) {
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) { result = CUI_TOUR_SKIPPED; ui->kpressed[CUI_KEY_ESCAPE] = 0; }
        else if (cui_key_pressed(ui, CUI_KEY_RIGHT) || (cui_key_pressed(ui, CUI_KEY_ENTER) && !button_keys)) result = last ? CUI_TOUR_DONE : CUI_TOUR_NEXT;
        else if (cui_key_pressed(ui, CUI_KEY_LEFT) && *step > 0) result = CUI_TOUR_BACK;
    }
    if (result == CUI_TOUR_NEXT) (*step)++;
    else if (result == CUI_TOUR_BACK) (*step)--;
    else if (result == CUI_TOUR_DONE || result == CUI_TOUR_SKIPPED) { *step = -1; st->init = false; }
    if (alpha < 0.999f || fabsf(st->x.v) + fabsf(st->y.v) + fabsf(st->w.v) + fabsf(st->h.v) > 0.01f) ui->animating = true;
    return result;
}
