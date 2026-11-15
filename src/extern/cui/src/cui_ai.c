/* AI interface pieces: reasoning, tool calls, plans, status, sources,
 * context meter, response actions, prompt suggestions, model picker, chat
 * composer and a voice orb. */
#include "cui_internal.h"

#define cui__fs cui__fsz

static cui_color cui__ai_accent(cui_ctx *ui) {
    return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
}

/* A row that opens a body below it with a height spring (shared by the
 * reasoning block and tool calls). */
typedef struct { cui_spring t; float hover, content_h; bool init; } cui__fold_state;

static bool cui__fold_body_begin(cui_ctx *ui, cui_id id, cui__fold_state *st, bool open, float indent) {
    float t = cui__clampf(st->t.x, 0, 1);
    if (t <= 0.001f && !open) return false;
    cui_id cid = id ^ 0xF01Du;
    cui_rect cr = cui__prev_rect(ui, cid);
    if (cr.h > 0) st->content_h = cr.h;
    int clip = cui__node_new(ui, 0);
    cui_node *cn = &ui->nodes[clip];
    cn->flags |= CUI_NF_CLIP;
    cn->w = CUI_FILL;
    if (t < 0.999f && st->content_h > 0) cn->h = CUI_PX(st->content_h * t);
    cn->opacity = CUI_MAX(0.001f, t);
    cui__open(ui, clip);
    int content = cui__node_new(ui, cid);
    cui_node *ct = &ui->nodes[content];
    ct->w = CUI_FILL; ct->gap = 8;
    ct->pad[3] = indent; ct->pad[0] = 6; ct->pad[2] = 4;
    ct->ty = (1.0f - t) * -4;
    cui__open(ui, content);
    if (!open) cui_push_disabled(ui, true);
    return true;
}

static void cui__fold_body_end(cui_ctx *ui, bool open) {
    if (!open) cui_pop_disabled(ui);
    cui__close(ui);
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Reasoning                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { cui__fold_state f; bool open, was_thinking, user_set; } cui__rs_state;

struct cui_reasoning_build { cui_id id; bool open; struct cui_reasoning_build *prev; };

typedef struct { float w; cui_color c; } cui__rail_paint;
static void cui__paint_rail(cui_ctx *ui, cui_node *n, void *data) {
    cui__rail_paint *p = data;
    cui_draw_rect(ui, (cui_rect){ n->rect.x + 7, n->rect.y + 4, 1.5f, CUI_MAX(0.0f, n->rect.h - 8) }, CUI_CORNERS(0.75f), p->c);
}

bool cui_reasoning_begin(cui_ctx *ui, const char *id_str, const cui_reasoning_opts *o) {
    cui_reasoning_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__rs_state *st = cui_state(ui, id, sizeof *st);
    /* open while thinking, fold away once it is done, unless the user chose */
    if (!st->f.init) { st->open = o->thinking; st->f.t.x = st->open ? 1.0f : 0.0f; st->f.init = true; }
    if (st->was_thinking && !o->thinking && !st->user_set) st->open = false;
    if (!st->was_thinking && o->thinking && !st->user_set) st->open = true;
    st->was_thinking = o->thinking;
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    if (r.clicked) { st->open = !st->open; st->user_set = true; }
    cui__spring(ui, &st->f.t, st->open ? 1.0f : 0.0f, CUI__SPRING_EXPAND);
    cui__tween(ui, &st->f.hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int wrap = cui__node_new(ui, 0);
    ui->nodes[wrap].w = CUI_FILL; ui->nodes[wrap].gap = 0;
    cui__open(ui, wrap);
    int head = cui__node_new(ui, id);
    cui_node *hn = &ui->nodes[head];
    hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER; hn->gap = 8;
    hn->pad[0] = hn->pad[2] = 4; hn->pad[1] = 8;
    hn->radius = CUI_CORNERS(CUI__R_ITEM(ui));
    hn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, head, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, head);
    cui_color fg = cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), st->f.hover);
    const char *title;
    if (o->title) title = o->title;
    else if (o->thinking) title = o->seconds >= 1 ? cui__frame_printf(ui, "Thinking for %.0fs", (double)o->seconds) : "Thinking";
    else title = o->seconds >= 1 ? cui__frame_printf(ui, "Thought for %.0fs", (double)o->seconds) : "Thought for a moment";
    if (o->thinking) {
        cui_text_fx_opts so;
        memset(&so, 0, sizeof so);
        so.size = ui->theme.text_sm; so.font = CUI_FONT_MEDIUM; so.nowrap = true;
        cui_shimmer_text_ex(ui, title, &so);
    } else {
        int t = cui__text_node(ui, title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), fg, false);
        ui->nodes[t].line_height = 1.3f;
    }
    cui__chevron_node(ui, cui__clampf(st->f.t.x, 0, 1) * CUI_PI * 0.5f - CUI_PI * 0.5f, 14, cui_mix(CUI_C(ui, TEXT_MUTED), CUI_C(ui, FOREGROUND), st->f.hover));
    cui__close(ui);
    cui__focus_ring(ui, head, r.focus_visible ? 1.0f : 0.0f);
    if (!cui__fold_body_begin(ui, id, &st->f, st->open, 20)) { cui__close(ui); return false; }
    /* a quiet rail on the left of the thoughts */
    cui_node *ct = &ui->nodes[cui__current(ui)];
    cui__rail_paint *rp = cui__frame_alloc(ui, sizeof *rp);
    rp->c = CUI_C(ui, BORDER);
    ct->paint = cui__paint_rail;
    ct->paint_data = rp;
    ui->nodes[cui__current(ui)].gap = 6;
    struct cui_reasoning_build *b = cui__frame_alloc(ui, sizeof *b);
    b->id = id; b->open = st->open; b->prev = (struct cui_reasoning_build *)ui->reasoning;
    ui->reasoning = b;
    /* thoughts read quieter than the answer */
    cui_push_id(ui, id_str);
    return true;
}

void cui_reasoning_end(cui_ctx *ui) {
    struct cui_reasoning_build *b = (struct cui_reasoning_build *)ui->reasoning;
    if (!b) return;
    cui_pop_id(ui);
    cui__fold_body_end(ui, b->open);
    cui__close(ui);
    ui->reasoning = b->prev;
}

/* ------------------------------------------------------------------------ */
/* Status icons                                                             */
/* ------------------------------------------------------------------------ */

typedef struct { cui_task_status s; float t; cui_color c, ring; } cui__st_paint;

static void cui__paint_status(cui_ctx *ui, cui_node *n, void *data) {
    cui__st_paint *p = data;
    cui_rect r = n->rect;
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    float rad = r.w * 0.5f - 1;
    switch (p->s) {
    case CUI_TASK_PENDING:
        cui_draw_arc(ui, c, rad - 0.5f, 0, 2 * CUI_PI, 1.5f, p->ring);
        break;
    case CUI_TASK_RUNNING: {
        cui_draw_arc(ui, c, rad - 0.5f, 0, 2 * CUI_PI, 1.5f, cui_alpha(p->c, 0.2f));
        float a0 = p->t * 2 * CUI_PI * 1.1f;
        cui_draw_arc(ui, c, rad - 0.5f, a0, a0 + CUI_PI * 0.6f, 1.8f, p->c);
    } break;
    case CUI_TASK_DONE:
        cui_draw_circle(ui, c, rad, p->c);
        cui__draw_check(ui, (cui_rect){ c.x - rad * 0.62f, c.y - rad * 0.62f, rad * 1.24f, rad * 1.24f }, 1, 1.7f, CUI_C(ui, SURFACE));
        break;
    case CUI_TASK_FAILED: {
        cui_draw_circle(ui, c, rad, CUI_C(ui, DANGER));
        float k = rad * 0.38f;
        cui_draw_line(ui, (cui_vec2){ c.x - k, c.y - k }, (cui_vec2){ c.x + k, c.y + k }, 1.7f, cui__rgba(255, 255, 255, 255));
        cui_draw_line(ui, (cui_vec2){ c.x + k, c.y - k }, (cui_vec2){ c.x - k, c.y + k }, 1.7f, cui__rgba(255, 255, 255, 255));
    } break;
    case CUI_TASK_SKIPPED:
        cui_draw_arc(ui, c, rad - 0.5f, 0, 2 * CUI_PI, 1.5f, p->ring);
        cui_draw_line(ui, (cui_vec2){ c.x - rad * 0.4f, c.y }, (cui_vec2){ c.x + rad * 0.4f, c.y }, 1.5f, p->ring);
        break;
    }
}

static void cui__status_node(cui_ctx *ui, cui_task_status s, float size) {
    int n = cui__leaf(ui, 0, CUI_PX(size), CUI_PX(size));
    cui__st_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->s = s; p->t = (float)fmod(ui->time, 1000.0);
    p->c = s == CUI_TASK_DONE ? (ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, SUCCESS) : CUI_C(ui, ACCENT)) : cui__ai_accent(ui);
    p->ring = CUI_C(ui, BORDER_STRONG);
    ui->nodes[n].paint = cui__paint_status;
    ui->nodes[n].paint_data = p;
    ui->nodes[n].self_align = CUI_ALIGN_CENTER;
    if (s == CUI_TASK_RUNNING) ui->animating = true;
}

/* ------------------------------------------------------------------------ */
/* Tool call                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { cui__fold_state f; bool open; } cui__tc_state;

static bool cui__looks_json(const char *s) {
    while (s && (*s == ' ' || *s == '\n' || *s == '\t')) s++;
    return s && (*s == '{' || *s == '[');
}

static void cui__tool_payload(cui_ctx *ui, const char *label, const char *body, const char *id) {
    int t = cui__text_node(ui, label, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.3f;
    cui_code_opts co;
    memset(&co, 0, sizeof co);
    co.lang = cui__looks_json(body) ? "json" : NULL;
    co.max_height = 180;
    co.size = 12;
    co.no_header = true;
    cui_code_block_ex(ui, id, body, &co);
}

void cui_tool_call_ex(cui_ctx *ui, const char *id_str, const char *name, const cui_tool_call_opts *o) {
    cui_tool_call_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui__tc_state *st = cui_state(ui, id, sizeof *st);
    bool expandable = o->input || o->output;
    cui_interact r = cui__interact(ui, id, expandable ? CUI_IF_FOCUSABLE : 0);
    if (r.clicked && expandable) st->open = !st->open;
    if (!st->f.init) { st->f.t.x = st->open ? 1.0f : 0.0f; st->f.init = true; }
    cui__spring(ui, &st->f.t, st->open ? 1.0f : 0.0f, CUI__SPRING_EXPAND);
    cui__tween(ui, &st->f.hover, r.hovered && expandable ? 1.0f : 0.0f, CUI__T_HOVER);
    int wrap = cui__node_new(ui, 0);
    cui_node *wn = &ui->nodes[wrap];
    wn->w = CUI_FILL;
    wn->radius = CUI_CORNERS(ui->theme.radius_control - 4);
    wn->border = 1;
    wn->border_color = o->status == CUI_TASK_FAILED ? cui_alpha(CUI_C(ui, DANGER), 0.45f) : cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), st->f.hover);
    wn->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.03f) : CUI_C(ui, SURFACE);
    wn->flags |= CUI_NF_CLIP;
    cui__open(ui, wrap);
    int head = cui__node_new(ui, id);
    cui_node *hn = &ui->nodes[head];
    hn->dir = CUI_ROW; hn->align = CUI_ALIGN_CENTER; hn->gap = 10;
    hn->pad[0] = hn->pad[2] = 9; hn->pad[1] = hn->pad[3] = 12;
    hn->w = CUI_FILL;
    if (expandable) { cui__mark_interactive(ui, head, CUI_IF_FOCUSABLE); if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND); }
    cui__open(ui, head);
    int ib = cui__node_new(ui, 0);
    cui_node *in = &ui->nodes[ib];
    in->w = CUI_PX(16); in->h = CUI_PX(24);   /* a bare icon: no tile behind it */
    in->dir = CUI_STACK; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER;
    cui__open(ui, ib);
    cui__icon_node(ui, o->icon ? o->icon : CUI_ICON_TERMINAL, 14, CUI_C(ui, TEXT_SECONDARY));
    cui__close(ui);
    int nt = cui__text_node(ui, name, -1, CUI_FONT_MONO, cui__fs(ui, ui->theme.text_sm - 0.5f), CUI_C(ui, FOREGROUND), false);
    ui->nodes[nt].line_height = 1.3f;
    if (o->detail) {
        int dt = cui__text_node(ui, o->detail, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[dt].line_height = 1.3f;
        ui->nodes[dt].w = CUI_FILL;
    } else cui_spacer(ui);
    if (o->seconds > 0 && o->status != CUI_TASK_PENDING) {
        int tt = cui__text_node(ui, o->seconds < 10 ? cui__frame_printf(ui, "%.1fs", (double)o->seconds) : cui__frame_printf(ui, "%.0fs", (double)o->seconds), -1,
                                CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[tt].flags |= CUI_NF_TABULAR; ui->nodes[tt].line_height = 1.3f;
    }
    cui__status_node(ui, o->status, 16);
    if (expandable) cui__chevron_node(ui, cui__clampf(st->f.t.x, 0, 1) * CUI_PI, 14, CUI_C(ui, TEXT_MUTED));
    cui__close(ui);
    cui__focus_ring(ui, head, r.focus_visible ? 1.0f : 0.0f);
    if (expandable && cui__fold_body_begin(ui, id, &st->f, st->open, 12)) {
        cui_node *ct = &ui->nodes[cui__current(ui)];
        ct->pad[1] = 12; ct->pad[2] = 12; ct->pad[0] = 2;
        cui_push_id(ui, id_str);
        if (o->input) cui__tool_payload(ui, "Input", o->input, "in");
        if (o->output) cui__tool_payload(ui, o->status == CUI_TASK_FAILED ? "Error" : "Output", o->output, "out");
        cui_pop_id(ui);
        cui__fold_body_end(ui, st->open);
    }
    cui__close(ui);
    ui->last_id = id;
}

/* ------------------------------------------------------------------------ */
/* Agent plan                                                               */
/* ------------------------------------------------------------------------ */

typedef struct { float y0, y1; float x; cui_color done, rest; float split; } cui__plan_rail;
static void cui__paint_plan_rail(cui_ctx *ui, cui_node *n, void *data) {
    (void)n;
    cui__plan_rail *p = data;
    if (p->y1 <= p->y0) return;
    float split = cui__clampf(p->split, p->y0, p->y1);
    if (split > p->y0) cui_draw_rect(ui, (cui_rect){ p->x - 0.75f, p->y0, 1.5f, split - p->y0 }, CUI_CORNERS(0.75f), p->done);
    if (p->y1 > split) cui_draw_rect(ui, (cui_rect){ p->x - 0.75f, split, 1.5f, p->y1 - split }, CUI_CORNERS(0.75f), p->rest);
}

int cui_agent_plan_ex(cui_ctx *ui, const char *id_str, const cui_plan_step *steps, int count, const cui_agent_plan_opts *o) {
    cui_agent_plan_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    int done = 0, clicked = -1;
    for (int k = 0; k < count; k++) if (steps[k].status == CUI_TASK_DONE || steps[k].status == CUI_TASK_SKIPPED) done++;
    float *prog = cui_state(ui, id, sizeof(float));
    cui__tween(ui, prog, count ? (float)done / count : 0, 0.4f);
    int card = cui__node_new(ui, id);
    cui_node *cn = &ui->nodes[card];
    cn->w = CUI_FILL; cn->gap = 2;
    cn->pad[0] = 14; cn->pad[2] = 10; cn->pad[1] = cn->pad[3] = 16;
    cn->radius = CUI_CORNERS(ui->theme.radius_panel);
    cn->bg = CUI_C(ui, SURFACE); cn->border = 1; cn->border_color = CUI_C(ui, BORDER);
    cui__open(ui, card);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].align = CUI_ALIGN_CENTER; ui->nodes[head].gap = 10; ui->nodes[head].w = CUI_FILL;
    ui->nodes[head].pad[2] = 8;
    cui__open(ui, head);
    int tt = cui__text_node(ui, o->title ? o->title : "Plan", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
    ui->nodes[tt].line_height = 1.3f; ui->nodes[tt].w = CUI_FILL;
    int ct = cui__text_node(ui, cui__frame_printf(ui, "%d of %d", done, count), -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[ct].flags |= CUI_NF_TABULAR; ui->nodes[ct].line_height = 1.3f;
    cui_progress_opts po;
    memset(&po, 0, sizeof po);
    po.width = CUI_PX(64); po.height = 4;
    cui_progress_ex(ui, *prog, &po);
    cui__close(ui);
    /* steps on a rail */
    int list = cui__node_new(ui, 0);
    ui->nodes[list].w = CUI_FILL; ui->nodes[list].gap = 0;
    cui__plan_rail *rail = cui__frame_alloc(ui, sizeof *rail);
    memset(rail, 0, sizeof *rail);
    rail->done = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, SUCCESS) : CUI_C(ui, ACCENT); rail->rest = CUI_C(ui, BORDER);
    ui->nodes[list].paint = cui__paint_plan_rail;
    ui->nodes[list].paint_data = rail;
    cui__open(ui, list);
    cui_push_id(ui, id_str);
    float first_y = 0, last_y = 0, split = 0;
    for (int k = 0; k < count; k++) {
        const cui_plan_step *s = &steps[k];
        cui_id sid = cui__hash(&k, sizeof k, id ^ 0x57E9u);
        cui_interact r = cui__interact(ui, sid, 0);
        if (r.clicked) clicked = k;
        cui_rect pr = cui__prev_rect(ui, sid);
        float cy = pr.y + 9 + 10;
        if (k == 0) first_y = cy;
        if (k == count - 1) last_y = cy;
        if (s->status == CUI_TASK_DONE || s->status == CUI_TASK_SKIPPED || s->status == CUI_TASK_RUNNING) split = cy;
        rail->x = pr.x + 10;
        int row = cui__node_new(ui, sid);
        cui_node *rn = &ui->nodes[row];
        rn->dir = CUI_ROW; rn->align = CUI_ALIGN_START; rn->gap = 12; rn->w = CUI_FILL;
        rn->pad[0] = rn->pad[2] = 9;
        rn->radius = CUI_CORNERS(10);
        cui__mark_interactive(ui, row, 0);
        cui__open(ui, row);
        int sb = cui__node_new(ui, 0);
        ui->nodes[sb].w = CUI_PX(20); ui->nodes[sb].h = CUI_PX(20);
        ui->nodes[sb].dir = CUI_STACK; ui->nodes[sb].align = CUI_ALIGN_CENTER; ui->nodes[sb].justify = CUI_JUSTIFY_CENTER;
        ui->nodes[sb].bg = CUI_C(ui, SURFACE); ui->nodes[sb].radius = CUI_CORNERS(10);   /* masks the rail behind the icon */
        cui__open(ui, sb);
        cui__status_node(ui, s->status, 18);
        cui__close(ui);
        int col = cui__node_new(ui, 0);
        ui->nodes[col].w = CUI_FILL; ui->nodes[col].gap = 2; ui->nodes[col].pad[0] = 1;
        cui__open(ui, col);
        bool running = s->status == CUI_TASK_RUNNING;
        cui_color tc = s->status == CUI_TASK_PENDING || s->status == CUI_TASK_SKIPPED ? CUI_C(ui, TEXT_SECONDARY) : CUI_C(ui, FOREGROUND);
        if (running) {
            cui_text_fx_opts so;
            memset(&so, 0, sizeof so);
            so.size = ui->theme.text_sm; so.font = CUI_FONT_MEDIUM;
            so.color = CUI_C(ui, TEXT_SECONDARY);
            cui_shimmer_text_ex(ui, s->title, &so);
        } else {
            int t = cui__text_node(ui, s->title, -1, s->status == CUI_TASK_DONE ? CUI_FONT_BODY : CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), tc, true);
            ui->nodes[t].line_height = 1.4f;
        }
        if (s->detail) {
            int dt = cui__text_node(ui, s->detail, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), s->status == CUI_TASK_FAILED ? CUI_C(ui, DANGER) : CUI_C(ui, TEXT_MUTED), true);
            ui->nodes[dt].line_height = 1.4f;
        }
        cui__close(ui);
        cui__close(ui);
    }
    rail->y0 = first_y; rail->y1 = last_y; rail->split = split;
    cui_pop_id(ui);
    cui__close(ui);
    cui__close(ui);
    ui->last_id = id;
    return clicked;
}

/* ------------------------------------------------------------------------ */
/* Agent status                                                             */
/* ------------------------------------------------------------------------ */

void cui_agent_status_ex(cui_ctx *ui, const char *text, const cui_agent_status_opts *o) {
    cui_agent_status_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int n = cui__node_new(ui, 0);
    cui_node *nd = &ui->nodes[n];
    nd->dir = CUI_ROW; nd->align = CUI_ALIGN_CENTER; nd->gap = 8;
    nd->pad[0] = nd->pad[2] = 6; nd->pad[1] = 14; nd->pad[3] = 10;
    nd->radius = CUI_CORNERS(16);
    nd->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.06f) : CUI_C(ui, SURFACE_MUTED);
    nd->border = 1; nd->border_color = CUI_C(ui, BORDER_SUBTLE);
    nd->self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, n);
    bool working = o->status == CUI_TASK_RUNNING || o->status == CUI_TASK_PENDING;
    if (o->status == CUI_TASK_RUNNING) {
        cui_loader_opts lo;
        memset(&lo, 0, sizeof lo);
        lo.style = CUI_LOADER_DOTS; lo.size = 18; lo.color = cui__ai_accent(ui);
        cui_loader_ex(ui, &lo);
    } else if (o->icon) cui__icon_node(ui, o->icon, 14, CUI_C(ui, TEXT_SECONDARY));
    else cui__status_node(ui, o->status, 15);
    if (working) {
        cui_text_fx_opts so;
        memset(&so, 0, sizeof so);
        so.size = ui->theme.text_sm; so.nowrap = true; so.color = CUI_C(ui, TEXT_SECONDARY);
        cui_shimmer_text_ex(ui, text, &so);
    } else {
        int t = cui__text_node(ui, text, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), o->status == CUI_TASK_FAILED ? CUI_C(ui, DANGER) : CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t].line_height = 1.3f;
    }
    if (o->seconds > 0) {
        int s = (int)o->seconds;
        const char *ts = s >= 60 ? cui__frame_printf(ui, "%dm %02ds", s / 60, s % 60) : cui__frame_printf(ui, "%ds", s);
        int t = cui__text_node(ui, ts, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[t].flags |= CUI_NF_TABULAR; ui->nodes[t].line_height = 1.3f;
    }
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Sources and citations                                                    */
/* ------------------------------------------------------------------------ */

static const char *cui__domain(cui_ctx *ui, const char *url) {
    if (!url) return "";
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    if (!strncmp(p, "www.", 4)) p += 4;
    int n = 0;
    while (p[n] && p[n] != '/' && p[n] != '?' && p[n] != '#') n++;
    return cui__frame_strdup(ui, p, n);
}

static void cui__favicon(cui_ctx *ui, const char *domain, float size) {
    uint32_t h = cui__hash_str(domain, 0xFA7u);
    cui_color c = ui->theme.colors[CUI_TOKEN_SERIES_1 + (h % 6)];
    int n = cui__node_new(ui, 0);
    cui_node *nd = &ui->nodes[n];
    nd->w = CUI_PX(size); nd->h = CUI_PX(size); nd->radius = CUI_CORNERS(size * 0.3f);
    nd->bg = cui_alpha(c, ui->mode == CUI_THEME_DARK ? 0.3f : 0.16f);
    nd->dir = CUI_STACK; nd->align = CUI_ALIGN_CENTER; nd->justify = CUI_JUSTIFY_CENTER;
    cui__open(ui, n);
    char letter[2] = { domain && domain[0] ? (char)(domain[0] >= 'a' && domain[0] <= 'z' ? domain[0] - 32 : domain[0]) : '?', 0 };
    int t = cui__text_node(ui, cui__frame_strdup(ui, letter, 1), 1, CUI_FONT_MEDIUM, cui__fs(ui, size * 0.56f), ui->mode == CUI_THEME_DARK ? cui_mix(c, cui__rgba(255, 255, 255, 255), 0.4f) : cui_mix(c, cui__rgba(0, 0, 0, 255), 0.25f), false);
    ui->nodes[t].line_height = 1.0f;
    cui__close(ui);
}

int cui_source_list(cui_ctx *ui, const char *id_str, const cui_source *sources, int count) {
    cui_id id = cui_make_id(ui, id_str);
    int clicked = -1;
    cui_box sb;
    memset(&sb, 0, sizeof sb);
    sb.width = CUI_FILL;
    cui_scroll_state *ss = cui_state(ui, id, sizeof *ss);
    ss->fade = cui__backdrop(ui);   /* cards that run past the edge fade out instead of being cut */
    cui__scroll_begin_ex(ui, id, &sb, true);
    ui->nodes[cui__current(ui)].h = CUI_FIT;
    ui->nodes[cui__current(ui)].min_h = 0;
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 8; ui->nodes[row].pad[2] = 4; ui->nodes[row].align = CUI_ALIGN_STRETCH;
    cui__open(ui, row);
    for (int k = 0; k < count; k++) {
        const cui_source *s = &sources[k];
        cui_id cid = cui__hash(&k, sizeof k, id ^ 0x50Cu);
        cui_interact r = cui__interact(ui, cid, CUI_IF_FOCUSABLE);
        if (r.clicked) clicked = k;
        float hv_v = cui__hover_fade(ui, cid, r.hovered, CUI__T_HOVER), *hv = &hv_v;
        int c = cui__node_new(ui, cid);
        cui_node *cn = &ui->nodes[c];
        cn->w = CUI_PX(196); cn->gap = 6;
        cn->pad[0] = cn->pad[2] = 10; cn->pad[1] = cn->pad[3] = 12;
        cn->radius = CUI_CORNERS(ui->theme.radius_panel);
        cn->bg = cui_mix(ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.04f) : CUI_C(ui, SURFACE_MUTED), CUI_C(ui, FOREGROUND), 0.04f * *hv);
        cn->border = 1; cn->border_color = cui_mix(CUI_C(ui, BORDER_SUBTLE), CUI_C(ui, BORDER), *hv);
        cui__mark_interactive(ui, c, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, c);
        int top = cui__node_new(ui, 0);
        ui->nodes[top].dir = CUI_ROW; ui->nodes[top].gap = 6; ui->nodes[top].align = CUI_ALIGN_CENTER; ui->nodes[top].w = CUI_FILL;
        cui__open(ui, top);
        const char *dom = cui__domain(ui, s->url);
        cui__favicon(ui, dom, 16);
        int dt = cui__text_node(ui, dom, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[dt].w = CUI_FILL; ui->nodes[dt].line_height = 1.3f;
        int nt = cui__text_node(ui, cui__frame_printf(ui, "%d", k + 1), -1, CUI_FONT_MEDIUM, cui__fs(ui, 10.5f), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[nt].line_height = 1.3f; ui->nodes[nt].flags |= CUI_NF_TABULAR;
        cui__close(ui);
        cui_text_opts to;
        memset(&to, 0, sizeof to);
        int t = cui__text_node(ui, s->title ? s->title : dom, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm - 0.5f), CUI_C(ui, FOREGROUND), true);
        ui->nodes[t].line_height = 1.35f;
        ui->nodes[t].max_h = cui__fs(ui, ui->theme.text_sm - 0.5f) * 1.35f * 2 + 1;
        ui->nodes[t].flags |= CUI_NF_CLIP;
        (void)to;
        cui__close(ui);
        cui__focus_ring(ui, c, r.focus_visible ? 1.0f : 0.0f);
        if (s->snippet || s->url) {
            ui->last_id = cid;
            cui_tooltip(ui, s->url);
        }
    }
    cui__close(ui);
    cui__scroll_end_ex(ui);
    ui->last_id = id;
    return clicked;
}

bool cui_citation(cui_ctx *ui, int number, const cui_source *s) {
    char key[24];
    snprintf(key, sizeof key, "cite%d", number);
    cui_id id = cui_make_id(ui, key);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    float hv_v = cui__hover_fade(ui, id, r.hovered, CUI__T_HOVER), *hv = &hv_v;
    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->dir = CUI_ROW; nd->align = CUI_ALIGN_CENTER; nd->gap = 4;
    nd->pad[0] = nd->pad[2] = 1; nd->pad[1] = nd->pad[3] = 7;
    nd->radius = CUI_CORNERS(9);
    nd->bg = cui_mix(ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.1f) : CUI_C(ui, SURFACE_MUTED), cui__ai_accent(ui), 0.16f * *hv);
    nd->self_align = CUI_ALIGN_CENTER;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, n);
    const char *dom = s ? cui__domain(ui, s->url) : "";
    const char *lab = dom[0] ? dom : cui__frame_printf(ui, "%d", number);
    int t = cui__text_node(ui, lab, -1, CUI_FONT_MEDIUM, cui__fs(ui, 11), cui_mix(CUI_C(ui, TEXT_SECONDARY), CUI_C(ui, FOREGROUND), *hv), false);
    ui->nodes[t].line_height = 1.5f;
    cui__close(ui);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = id;
    if (s) {
        cui_hover_card_opts ho;
        memset(&ho, 0, sizeof ho);
        ho.width = 300; ho.delay = 0.3f; ho.placement = CUI_PLACE_TOP_START;
        if (cui_hover_card_begin(ui, key, &ho)) {
            int top = cui__node_new(ui, 0);
            ui->nodes[top].dir = CUI_ROW; ui->nodes[top].gap = 8; ui->nodes[top].align = CUI_ALIGN_CENTER;
            cui__open(ui, top);
            cui__favicon(ui, dom, 18);
            int dt = cui__text_node(ui, dom, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[dt].line_height = 1.3f;
            cui__close(ui);
            if (s->title) { int tt = cui__text_node(ui, s->title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), true); ui->nodes[tt].line_height = 1.35f; }
            if (s->snippet) { int st = cui__text_node(ui, s->snippet, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs + 0.5f), CUI_C(ui, TEXT_SECONDARY), true); ui->nodes[st].line_height = 1.45f; }
            cui_hover_card_end(ui);
        }
    }
    ui->last_id = id;
    return r.clicked;
}

/* ------------------------------------------------------------------------ */
/* Context meter                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { float frac; cui_color c, track; } cui__cm_paint;
static void cui__paint_context_ring(cui_ctx *ui, cui_node *n, void *data) {
    cui__cm_paint *p = data;
    cui_vec2 c = { n->rect.x + n->rect.w * 0.5f, n->rect.y + n->rect.h * 0.5f };
    float r = n->rect.w * 0.5f - 1.5f;
    cui_draw_arc(ui, c, r, 0, 2 * CUI_PI, 2.5f, p->track);
    if (p->frac > 0.001f) cui_draw_arc(ui, c, r, -CUI_PI * 0.5f, -CUI_PI * 0.5f + 2 * CUI_PI * cui__clampf(p->frac, 0, 1), 2.5f, p->c);
}

void cui_context_meter_ex(cui_ctx *ui, const char *id_str, float used, float limit, const cui_context_opts *o) {
    cui_context_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    float frac = limit > 0 ? used / limit : 0;
    float *anim = cui_state(ui, id, sizeof(float));
    cui__tween(ui, anim, frac, 0.3f);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->dir = CUI_ROW; nd->align = CUI_ALIGN_CENTER; nd->gap = 6;
    nd->pad[0] = nd->pad[2] = 4; nd->pad[1] = nd->pad[3] = 8;
    nd->radius = CUI_CORNERS(12);
    nd->bg = r.hovered ? CUI_C(ui, HOVER) : cui__rgba(0, 0, 0, 0);
    nd->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    cui__open(ui, n);
    int ring = cui__leaf(ui, 0, CUI_PX(16), CUI_PX(16));
    cui__cm_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->frac = *anim;
    p->c = frac >= 0.95f ? CUI_C(ui, DANGER) : frac >= 0.8f ? CUI_C(ui, WARNING) : cui__ai_accent(ui);
    p->track = CUI_C(ui, CONTROL_TRACK);
    ui->nodes[ring].paint = cui__paint_context_ring;
    ui->nodes[ring].paint_data = p;
    int t = cui__text_node(ui, cui__frame_printf(ui, "%.0f%%", (double)(frac * 100)), -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_SECONDARY), false);
    ui->nodes[t].flags |= CUI_NF_TABULAR; ui->nodes[t].line_height = 1.3f;
    cui__close(ui);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    ui->last_id = id;
    cui_hover_card_opts ho;
    memset(&ho, 0, sizeof ho);
    ho.width = 260; ho.delay = 0.2f; ho.placement = CUI_PLACE_TOP;
    if (cui_hover_card_begin(ui, id_str, &ho)) {
        const char *unit = o->unit ? o->unit : "tokens";
        int head = cui__node_new(ui, 0);
        ui->nodes[head].dir = CUI_ROW; ui->nodes[head].w = CUI_FILL; ui->nodes[head].align = CUI_ALIGN_END;
        cui__open(ui, head);
        int a = cui__text_node(ui, "Context", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
        ui->nodes[a].w = CUI_FILL; ui->nodes[a].line_height = 1.3f;
        int b = cui__text_node(ui, cui__frame_printf(ui, "%s / %s %s", cui__fmt_value(ui, NULL, used), cui__fmt_value(ui, NULL, limit), unit), -1, CUI_FONT_BODY,
                               cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[b].flags |= CUI_NF_TABULAR; ui->nodes[b].line_height = 1.3f;
        cui__close(ui);
        cui_split_bar_opts so;
        memset(&so, 0, sizeof so);
        so.no_legend = true; so.height = 8;
        if (o->parts && o->part_count > 0) {
            cui_viz_item *items = cui__frame_alloc(ui, sizeof(cui_viz_item) * (size_t)(o->part_count + 1));
            memcpy(items, o->parts, sizeof(cui_viz_item) * (size_t)o->part_count);
            float sum = 0;
            for (int k = 0; k < o->part_count; k++) sum += items[k].value;
            items[o->part_count] = (cui_viz_item){ "Free", CUI_MAX(0.0f, limit - sum), cui_alpha(CUI_C(ui, CONTROL_TRACK), 1) };
            cui_split_bar_ex(ui, "ctx-bar", items, o->part_count + 1, &so);
            for (int k = 0; k < o->part_count; k++) {
                int row = cui__node_new(ui, 0);
                ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 8; ui->nodes[row].align = CUI_ALIGN_CENTER; ui->nodes[row].w = CUI_FILL;
                cui__open(ui, row);
                int sw = cui__leaf(ui, 0, CUI_PX(8), CUI_PX(8));
                ui->nodes[sw].radius = CUI_CORNERS(2.5f);
                ui->nodes[sw].bg = cui__color_set(items[k].color) ? cui_color_of(ui, items[k].color) : ui->theme.colors[CUI_TOKEN_SERIES_1 + (k % 6)];
                int l = cui__text_node(ui, items[k].label ? items[k].label : "", -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs + 0.5f), CUI_C(ui, TEXT_SECONDARY), false);
                ui->nodes[l].w = CUI_FILL; ui->nodes[l].line_height = 1.4f;
                int v = cui__text_node(ui, cui__fmt_value(ui, NULL, items[k].value), -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs + 0.5f), CUI_C(ui, FOREGROUND), false);
                ui->nodes[v].flags |= CUI_NF_TABULAR; ui->nodes[v].line_height = 1.4f;
                cui__close(ui);
            }
        }
        if (frac >= 0.8f) {
            int w = cui__text_node(ui, frac >= 0.95f ? "Nearly full: older messages will be summarized." : "Filling up: consider starting a new conversation.", -1,
                                   CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), frac >= 0.95f ? CUI_C(ui, DANGER) : CUI_C(ui, WARNING), true);
            ui->nodes[w].line_height = 1.4f;
        }
        cui_hover_card_end(ui);
    }
    ui->last_id = id;
}

/* ------------------------------------------------------------------------ */
/* Response actions                                                         */
/* ------------------------------------------------------------------------ */

int cui_response_actions_ex(cui_ctx *ui, const char *id_str, const cui_response_actions_opts *o) {
    cui_response_actions_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int result = 0;
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 2; ui->nodes[row].align = CUI_ALIGN_CENTER;
    ui->nodes[row].self_align = CUI__ALIGN_NOSTRETCH;
    cui__open(ui, row);
    cui_push_id(ui, id_str);
    if (o->copy_text && cui_copy_button(ui, "copy", o->copy_text)) result |= CUI_RESPONSE_COPY;
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.variant = CUI_BUTTON_GHOST; bo.size = CUI_SIZE_SM;
    if (!o->no_regenerate) { bo.tooltip = "Regenerate"; if (cui_icon_button_ex(ui, CUI_ICON_REFRESH_CW, &bo)) result |= CUI_RESPONSE_REGENERATE; }
    int vote = o->vote ? *o->vote : 0;
    bo.tooltip = "Good response"; bo.selected = vote > 0;
    bo.color = vote > 0 ? cui__ai_accent(ui) : cui__rgba(0, 0, 0, 0);
    if (cui_icon_button_ex(ui, CUI_ICON_THUMBS_UP, &bo)) { result |= CUI_RESPONSE_UP; if (o->vote) *o->vote = vote > 0 ? 0 : 1; }
    bo.tooltip = "Bad response"; bo.selected = vote < 0;
    bo.color = vote < 0 ? cui__ai_accent(ui) : cui__rgba(0, 0, 0, 0);
    if (cui_icon_button_ex(ui, CUI_ICON_THUMBS_DOWN, &bo)) { result |= CUI_RESPONSE_DOWN; if (o->vote) *o->vote = vote < 0 ? 0 : -1; }
    bo.selected = false; bo.color = cui__rgba(0, 0, 0, 0);
    if (!o->no_share) { bo.tooltip = "Share"; if (cui_icon_button_ex(ui, CUI_ICON_SHARE_2, &bo)) result |= CUI_RESPONSE_SHARE; }
    cui_pop_id(ui);
    cui__close(ui);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Prompt suggestions                                                       */
/* ------------------------------------------------------------------------ */

int cui_prompt_suggestions_ex(cui_ctx *ui, const char *id_str, const cui_prompt_suggestion *items, int count, const cui_prompt_suggestions_opts *o) {
    cui_prompt_suggestions_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    int clicked = -1;
    int grid = cui__node_new(ui, id);
    ui->nodes[grid].dir = CUI_GRID; ui->nodes[grid].columns = o->columns > 0 ? o->columns : 2; ui->nodes[grid].gap = 10; ui->nodes[grid].w = CUI_FILL;
    ui->nodes[grid].col_min = 200;   /* one column on a phone, where two would wrap every word */
    cui__open(ui, grid);
    for (int k = 0; k < count; k++) {
        cui_id cid = cui__hash(&k, sizeof k, id ^ 0x9A0u);
        cui_interact r = cui__interact(ui, cid, CUI_IF_FOCUSABLE);
        if (r.clicked) clicked = k;
        float *st = cui_state(ui, cid, sizeof(float) * 3);
        cui__tween(ui, &st[0], r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        cui_spring *press = (cui_spring *)&st[1];
        if (press->x <= 0) press->x = 1;
        cui__spring(ui, press, r.pressed ? CUI__PRESS_WIDE : 1.0f, CUI_SPRING_SNAPPY);
        int c = cui__node_new(ui, cid);
        cui_node *cn = &ui->nodes[c];
        cn->dir = CUI_ROW; cn->gap = 12; cn->align = CUI_ALIGN_START;
        cn->pad[0] = cn->pad[2] = 12; cn->pad[1] = cn->pad[3] = 14;
        cn->radius = CUI_CORNERS(ui->theme.radius_panel);
        cn->bg = cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), (ui->mode == CUI_THEME_DARK ? 0.04f : 0.0f) + 0.03f * st[0]);
        cn->border = 1; cn->border_color = cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), st[0]);
        cn->scale = press->x;
        cui__mark_interactive(ui, c, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, c);
        if (items[k].icon) {
            int ib = cui__node_new(ui, 0);
            cui_node *in = &ui->nodes[ib];
            in->w = CUI_PX(18); in->h = CUI_PX(20);   /* a bare icon, aligned with the title's first line */
            in->dir = CUI_STACK; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER;
            cui__open(ui, ib);
            cui__icon_node(ui, items[k].icon, 16, CUI_C(ui, TEXT_SECONDARY));
            cui__close(ui);
        }
        int col = cui__node_new(ui, 0);
        ui->nodes[col].w = CUI_FILL; ui->nodes[col].gap = 2;
        cui__open(ui, col);
        int t = cui__text_node(ui, items[k].title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), true);
        ui->nodes[t].line_height = 1.35f;
        if (items[k].detail) {
            int dt = cui__text_node(ui, items[k].detail, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs + 0.5f), CUI_C(ui, TEXT_MUTED), true);
            ui->nodes[dt].line_height = 1.4f;
        }
        cui__close(ui);
        cui_rect cpr = cui__prev_rect(ui, cid);
        int ar = cui__absolute(ui, 0, cpr.w > 0 ? cpr.w - 26 : -100, 12, 14, 14);   /* top right, out of the text's way */
        ui->nodes[ar].tx = 3 * st[0];
        ui->nodes[ar].ty = -3 * st[0];
        ui->nodes[ar].opacity = CUI_MAX(0.001f, st[0]);   /* appears with the hover, not repeated on every card at rest */
        cui__open(ui, ar);
        cui__icon_node(ui, CUI_ICON_ARROW_UP_RIGHT, 14, CUI_C(ui, TEXT_MUTED));
        cui__close(ui);
        cui__close(ui);
        cui__focus_ring(ui, c, r.focus_visible ? 1.0f : 0.0f);
    }
    cui__close(ui);
    ui->last_id = id;
    return clicked;
}

/* ------------------------------------------------------------------------ */
/* Model picker                                                             */
/* ------------------------------------------------------------------------ */

static bool cui__model_item(cui_ctx *ui, const cui_model *m, bool checked) {
    cui__popup_build *b = ui->pb_depth > 0 ? &ui->pb_stack[ui->pb_depth - 1] : NULL;
    int index = b ? b->item_index++ : 0;
    cui__menu_nav *nav = b ? cui_state(ui, b->id ^ 0x4E4156u, sizeof *nav) : NULL;
    if (nav) nav->item_count++;
    cui_id id = cui_make_id(ui, m->name ? m->name : "model");
    cui_interact r = cui__interact(ui, id, 0);
    if (r.hovered && nav && (ui->mx != ui->pmx || ui->my != ui->pmy || !nav->keyboard)) { nav->highlight = index; nav->keyboard = false; }
    bool hl = nav && nav->highlight == index;
    float hv_v = cui__hover_fade(ui, id, hl, CUI__T_HIGHLIGHT), *hv = &hv_v;
    int n = cui__node_new(ui, id);
    cui_node *nd = &ui->nodes[n];
    nd->dir = CUI_ROW; nd->align = CUI_ALIGN_CENTER; nd->gap = 10;
    nd->pad[0] = nd->pad[2] = 8; nd->pad[1] = nd->pad[3] = 10;
    nd->w = CUI_FILL; nd->self_align = CUI_ALIGN_STRETCH;
    nd->radius = CUI_CORNERS(CUI_MAX(6.0f, ui->theme.radius_panel - 6.0f));
    nd->bg = cui_alpha(CUI_C(ui, ACTIVE), *hv);
    cui__mark_interactive(ui, n, 0);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, n);
    if (m->icon) cui__icon_node(ui, m->icon, 16, CUI_C(ui, TEXT_SECONDARY));
    int col = cui__node_new(ui, 0);
    ui->nodes[col].w = CUI_FILL; ui->nodes[col].gap = 1;
    cui__open(ui, col);
    int top = cui__node_new(ui, 0);
    ui->nodes[top].dir = CUI_ROW; ui->nodes[top].gap = 6; ui->nodes[top].align = CUI_ALIGN_CENTER;
    cui__open(ui, top);
    int t = cui__text_node(ui, m->name ? m->name : "", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.35f;
    if (m->badge) {
        cui_badge_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM; bo.tone = CUI_TONE_ACCENT;
        cui_badge_ex(ui, m->badge, &bo);
    }
    cui__close(ui);
    if (m->detail) {
        int dt = cui__text_node(ui, m->detail, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), true);
        ui->nodes[dt].line_height = 1.4f;
    }
    cui__close(ui);
    if (checked) cui__icon_node(ui, CUI_ICON_CHECK, 16, CUI_C(ui, FOREGROUND));
    cui__close(ui);
    bool chosen = r.clicked || (hl && cui_key_pressed(ui, CUI_KEY_ENTER));
    if (chosen) { cui__close_all_popups(ui); ui->kpressed[CUI_KEY_ENTER] = 0; }
    return chosen;
}

bool cui_model_picker_ex(cui_ctx *ui, const char *id_str, const cui_model *models, int count, int *selected, const cui_model_picker_opts *o) {
    cui_model_picker_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id pid = id ^ 0x30DEu;
    cui_popup *p = cui__popup_get(ui, pid, true);
    bool open = p && p->open;
    int sel = selected ? *selected : 0;
    if (sel < 0 || sel >= count) sel = 0;
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    bool key = ui->focus == id && ui->focus_visible && (cui_key_pressed(ui, CUI_KEY_ENTER) || cui_key_pressed(ui, CUI_KEY_SPACE));
    if (r.clicked && !(open && key) && p) { cui__popup_set(ui, pid, !p->open); p->at_mouse = false; }
    float hv_v = cui__hover_fade(ui, id, (r.hovered || open), CUI__T_HOVER), *hv = &hv_v;
    int b = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[b];
    bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER; bn->gap = 6;
    bn->h = CUI_PX(30); bn->pad[1] = 8; bn->pad[3] = 10;
    bn->radius = CUI_CORNERS(CUI__R_CONTROL(ui, 30));
    bn->bg = o->ghost ? cui_alpha(CUI_C(ui, HOVER), *hv * 1.4f) : cui_mix(CUI_C(ui, SURFACE_MUTED), CUI_C(ui, FOREGROUND), 0.04f * *hv);
    if (!o->ghost) { bn->border = 1; bn->border_color = CUI_C(ui, BORDER); }
    bn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, b, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, b);
    if (count > 0 && models[sel].icon) cui__icon_node(ui, models[sel].icon, 14, CUI_C(ui, TEXT_SECONDARY));
    int t = cui__text_node(ui, count > 0 && models[sel].name ? models[sel].name : "Model", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
    ui->nodes[t].line_height = 1.3f;
    cui__chevron_node(ui, open ? CUI_PI : 0, 13, CUI_C(ui, TEXT_MUTED));
    cui__close(ui);
    cui__focus_ring(ui, b, r.focus_visible ? 1.0f : 0.0f);
    bool changed = false;
    int place = o->placement ? (int)o->placement : CUI_PLACE_TOP_START;
    if (cui__popup_begin(ui, pid, id, place, 300, 6, 0, true)) {
        for (int k = 0; k < count; k++) {
            cui_push_id_int(ui, k);
            if (cui__model_item(ui, &models[k], k == sel) && k != sel) { sel = k; changed = true; }
            cui_pop_id(ui);
        }
        cui__popup_end(ui);
    }
    if (selected) *selected = sel;
    ui->last_id = id;
    return changed;
}

/* ------------------------------------------------------------------------ */
/* Chat composer                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { float focus; cui_spring send; } cui__cmp_state;

int cui_chat_composer_ex(cui_ctx *ui, const char *id_str, char *buf, size_t cap, const cui_composer_opts *o) {
    cui_composer_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!buf || cap == 0) return 0;
    int result = 0;
    cui_id id = cui_make_id(ui, id_str);
    cui_id fid = id ^ 0xF1E1Du;
    cui__cmp_state *st = cui_state(ui, id, sizeof *st);
    bool empty = true;
    for (const char *c = buf; *c; c++) if (*c != ' ' && *c != '\n' && *c != '\t') { empty = false; break; }
    /* Enter sends; Shift+Enter is a new line */
    if (ui->focus == fid && cui_key_pressed(ui, CUI_KEY_ENTER) && !(ui->mods & (CUI_MOD_SHIFT | CUI_MOD_CTRL | CUI_MOD_SUPER))) {
        ui->kpressed[CUI_KEY_ENTER] = 0;
        if (!empty && !o->busy) result |= CUI_COMPOSER_SEND;
    }
    cui__tween(ui, &st->focus, ui->focus == fid ? 1.0f : 0.0f, CUI__T_FOCUS);
    int box = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[box];
    bn->w = o->width.kind ? o->width : CUI_FILL; bn->gap = 4;
    bn->pad[0] = 10; bn->pad[1] = bn->pad[3] = 10; bn->pad[2] = 10;
    bn->radius = CUI_CORNERS(ui->theme.radius_surface);
    bn->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.04f) : CUI_C(ui, SURFACE);
    bn->border = 1;
    bn->border_color = cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), st->focus);
    bn->elevation = CUI_ELEVATION_RAISED; bn->elevation_alpha = 0.6f + 0.4f * st->focus;
    cui__open(ui, box);
    cui_push_id(ui, id_str);
    /* attachments */
    if (o->attachment_count > 0 && o->attachments) {
        int row = cui__node_new(ui, 0);
        ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 6; ui->nodes[row].flags |= CUI_NF_WRAP; ui->nodes[row].w = CUI_FILL;
        ui->nodes[row].pad[1] = ui->nodes[row].pad[3] = 2; ui->nodes[row].pad[2] = 4;
        cui__open(ui, row);
        for (int k = 0; k < o->attachment_count; k++) {
            int chip = cui__node_new(ui, 0);
            cui_node *cn = &ui->nodes[chip];
            cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 6;
            cn->pad[0] = cn->pad[2] = 4; cn->pad[3] = 6; cn->pad[1] = 4;
            cn->radius = CUI_CORNERS(CUI__R_ITEM(ui));
            cn->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.08f) : CUI_C(ui, SURFACE_MUTED);
            cn->border = 1; cn->border_color = CUI_C(ui, BORDER_SUBTLE);
            cui__open(ui, chip);
            int ib = cui__node_new(ui, 0);
            ui->nodes[ib].w = CUI_PX(24); ui->nodes[ib].h = CUI_PX(24); ui->nodes[ib].radius = CUI_CORNERS(CUI__R_SMALL);
            ui->nodes[ib].bg = CUI_C(ui, SURFACE); ui->nodes[ib].dir = CUI_STACK; ui->nodes[ib].align = CUI_ALIGN_CENTER; ui->nodes[ib].justify = CUI_JUSTIFY_CENTER;
            cui__open(ui, ib);
            cui__icon_node(ui, CUI_ICON_FILE_TEXT, 14, CUI_C(ui, TEXT_SECONDARY));
            cui__close(ui);
            int t = cui__text_node(ui, o->attachments[k], -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs + 0.5f), CUI_C(ui, FOREGROUND), false);
            ui->nodes[t].line_height = 1.3f; ui->nodes[t].max_w = 160;
            cui_button_opts xo;
            memset(&xo, 0, sizeof xo);
            xo.variant = CUI_BUTTON_GHOST; xo.size = CUI_SIZE_SM; xo.tooltip = "Remove";
            cui_push_id_int(ui, k);
            if (cui_icon_button_ex(ui, CUI_ICON_X, &xo)) { result |= CUI_COMPOSER_REMOVE; if (o->removed) *o->removed = k; }
            cui_pop_id(ui);
            ui->nodes[ui->last_node].w = CUI_PX(22); ui->nodes[ui->last_node].h = CUI_PX(22);
            cui__close(ui);
        }
        cui__close(ui);
    }
    /* the text grows with its lines */
    float size = cui__fs(ui, ui->theme.text_body);
    float line_h = size * 1.5f;
    cui_rect fr = cui__prev_rect(ui, fid ^ 0xED17u);
    int lines = 1;
    if (fr.w > 0) {
        cui_text_layout_line *wl = NULL;
        lines = cui__edit_lines(ui, CUI_FONT_BODY, size, buf, (int)strlen(buf), fr.w, &wl);
    }
    int maxl = o->max_lines > 0 ? o->max_lines : 8;
    lines = CUI_CLAMP(lines, 1, maxl);
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.placeholder = o->placeholder ? o->placeholder : "Ask anything";
    io.height = lines * line_h + 12;
    io.width = CUI_FILL;
    cui__text_field(ui, fid, buf, cap, &io, true, 1, NULL);
    /* toolbar */
    int bar = cui__node_new(ui, 0);
    ui->nodes[bar].dir = CUI_ROW; ui->nodes[bar].align = CUI_ALIGN_CENTER; ui->nodes[bar].gap = 6; ui->nodes[bar].w = CUI_FILL;
    cui__open(ui, bar);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.variant = CUI_BUTTON_OUTLINE; bo.size = CUI_SIZE_SM; bo.tooltip = "Attach files";
    if (cui_icon_button_ex(ui, CUI_ICON_PLUS, &bo)) result |= CUI_COMPOSER_ATTACH;
    ui->nodes[ui->last_node].radius = CUI_CORNERS(ui->theme.radius_surface - 10);   /* concentric with the composer (pad 10) */
    if (o->models && o->model_count > 0) {
        cui_model_picker_opts mo;
        memset(&mo, 0, sizeof mo);
        mo.ghost = true;
        if (cui_model_picker_ex(ui, "model", o->models, o->model_count, o->model, &mo)) result |= CUI_COMPOSER_MODEL;
    }
    cui_spacer(ui);
    /* send / stop */
    cui_id sid = id ^ 0x5E7Du;
    bool can = o->busy || !empty;
    cui_interact sr = cui__interact(ui, sid, CUI_IF_FOCUSABLE);
    if ((sr.clicked || sr.keyboard_activate) && can) result |= o->busy ? CUI_COMPOSER_STOP : CUI_COMPOSER_SEND;
    if (st->send.x <= 0) st->send.x = 1;
    cui__spring(ui, &st->send, sr.pressed ? 0.88f : 1.0f, CUI_SPRING_SNAPPY);
    int sb = cui__node_new(ui, sid);
    cui_node *sn = &ui->nodes[sb];
    sn->w = CUI_PX(32); sn->h = CUI_PX(32); sn->radius = CUI_CORNERS(ui->theme.radius_surface - 10);
    sn->dir = CUI_STACK; sn->align = CUI_ALIGN_CENTER; sn->justify = CUI_JUSTIFY_CENTER;
    cui_color on_bg = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
    cui_color on_fg = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, BACKGROUND) : CUI_C(ui, ACCENT_FOREGROUND);
    sn->bg = can ? cui_mix(on_bg, CUI_C(ui, BACKGROUND), sr.hovered ? 0.12f : 0.0f) : CUI_C(ui, CONTROL_TRACK);
    if (can) { sn->paint = cui__paint_sheen; sn->paint_data = NULL; sn->elevation = CUI_ELEVATION_RESTING; sn->elevation_alpha = 1; }
    sn->scale = st->send.x;
    cui__mark_interactive(ui, sb, CUI_IF_FOCUSABLE);
    if (sr.hovered && can) cui__set_cursor(ui, CUI_CURSOR_HAND);
    cui__open(ui, sb);
    if (o->busy) {
        int sq = cui__leaf(ui, 0, CUI_PX(10), CUI_PX(10));
        ui->nodes[sq].radius = CUI_CORNERS(2.5f);
        ui->nodes[sq].bg = on_fg;
    } else cui__icon_node(ui, CUI_ICON_ARROW_UP, 16, can ? on_fg : CUI_C(ui, TEXT_MUTED));
    cui__close(ui);
    cui__focus_ring(ui, sb, sr.focus_visible ? 1.0f : 0.0f);
    ui->last_id = sid;
    cui_tooltip(ui, o->busy ? "Stop" : "Send");
    cui__close(ui);
    cui_pop_id(ui);
    cui__close(ui);
    ui->last_id = id;
    return result;
}

/* ------------------------------------------------------------------------ */
/* Voice orb                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring level, scale; float state_t[4]; bool init; } cui__orb_state;
typedef struct { float t, level, w[4]; cui_color a, b, c; float hover; } cui__orb_paint;

static void cui__orb_blob(cui_ctx *ui, cui_vec2 c, float r, float amp, float t, float seed, cui_color inner, cui_color outer, bool edged) {
    enum { N = 72 };
    cui_cvert v[N + 1];
    uint32_t idx[N * 3];
    cui_vec2 edge[N];
    v[0] = (cui_cvert){ c.x, c.y, inner };
    for (int i = 0; i < N; i++) {
        float a = 2 * CUI_PI * i / N;
        float k = 1 + amp * (0.55f * sinf(a * 3 + t * 1.7f + seed) + 0.3f * sinf(a * 5 - t * 2.3f + seed * 2) + 0.15f * sinf(a * 2 + t * 0.9f));
        edge[i] = (cui_vec2){ c.x + cosf(a) * r * k, c.y + sinf(a) * r * k };
        v[i + 1] = (cui_cvert){ edge[i].x, edge[i].y, outer };
    }
    for (int i = 0; i < N; i++) { idx[i * 3] = 0; idx[i * 3 + 1] = (uint32_t)(i + 1); idx[i * 3 + 2] = (uint32_t)((i + 1) % N + 1); }
    cui__draw_triangles(ui, v, N + 1, idx, N * 3);
    if (edged) cui_draw_polyline(ui, edge, N, 1.2f, cui_alpha(outer, 0.8f), true);   /* a soft edge */
}

static void cui__paint_orb(cui_ctx *ui, cui_node *n, void *data) {
    cui__orb_paint *p = data;
    cui_rect r = n->rect;
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    float R = r.w * 0.34f;
    float idle = p->w[CUI_VOICE_IDLE], listen = p->w[CUI_VOICE_LISTENING], think = p->w[CUI_VOICE_THINKING], speak = p->w[CUI_VOICE_SPEAKING];
    float breath = 1 + 0.025f * sinf(p->t * 1.6f) * idle;
    float pulse = 1 + (0.1f * speak + 0.05f * listen) * p->level;
    float amp = 0.03f + 0.05f * idle * 0.3f + (0.09f * listen + 0.06f * speak) * (0.3f + p->level);
    /* glow */
    float gr = R * 1.05f * pulse;
    cui_draw_shadow(ui, (cui_rect){ c.x - gr, c.y - gr, gr * 2, gr * 2 }, CUI_CORNERS(gr), R * 0.55f, 0, (cui_vec2){ 0, 0 },
                    cui_alpha(p->a, 0.14f + 0.1f * p->hover + 0.3f * p->level * (listen + speak)));   /* the glow reports the level */
    float rr = R * breath * pulse;
    /* one silhouette; the color inside drifts in swirls that fade out before they reach the edge */
    cui__orb_blob(ui, c, rr, amp, p->t, 0.0f, p->b, cui_mix(p->a, cui__rgba(0, 0, 0, 255), 0.08f), true);
    float sa = p->t * 0.7f, sb = p->t * 0.55f + 2.4f;
    cui__orb_blob(ui, (cui_vec2){ c.x + cosf(sa) * R * 0.2f, c.y + sinf(sa) * R * 0.2f }, rr * 0.56f, amp * 1.6f, p->t * 1.2f, 2.1f,
                  cui_alpha(p->c, 0.85f), cui_alpha(p->c, 0), false);
    cui__orb_blob(ui, (cui_vec2){ c.x + cosf(sb) * R * 0.22f, c.y + sinf(sb) * R * 0.22f }, rr * 0.5f, amp * 1.4f, p->t * 0.9f, 4.2f,
                  cui_alpha(cui_mix(p->a, cui__rgba(0, 0, 0, 255), 0.18f), 0.55f), cui_alpha(p->a, 0), false);
    cui__orb_blob(ui, (cui_vec2){ c.x - R * 0.08f, c.y - R * 0.1f }, rr * 0.62f, amp, p->t * 0.8f, 5.3f,
                  cui_alpha(cui__rgba(255, 255, 255, 255), 0.42f), cui_alpha(cui__rgba(255, 255, 255, 255), 0), false);
    /* a soft specular light, upper left, like glass catching a lamp */
    float hs = rr * 0.42f;
    cui_rect hl = { c.x - rr * 0.42f - hs * 0.5f, c.y - rr * 0.46f - hs * 0.5f, hs, hs * 0.8f };
    cui_draw_shadow(ui, hl, CUI_CORNERS(hs * 0.45f), hs * 0.55f, 0, (cui_vec2){ 0, 0 }, cui_alpha(cui__rgba(255, 255, 255, 255), 0.38f));
    /* thinking: a ring of dots sweeping around */
    if (think > 0.01f) {
        for (int k = 0; k < 12; k++) {
            float a = p->t * 2.4f + k * (2 * CUI_PI / 12);
            float fade = 0.5f + 0.5f * sinf(p->t * 4 - k * 0.6f);
            cui_draw_circle(ui, (cui_vec2){ c.x + cosf(a) * R * 1.28f, c.y + sinf(a) * R * 1.28f }, 2.2f, cui_alpha(p->a, think * (0.25f + 0.6f * fade)));
        }
    }
}

bool cui_voice_orb_ex(cui_ctx *ui, const char *id_str, cui_voice_state state, float level, const cui_voice_orb_opts *o) {
    cui_voice_orb_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE);
    cui__orb_state *st = cui_state(ui, id, sizeof *st);
    if (!st->init) { st->state_t[state & 3] = 1; st->scale.x = 1; st->init = true; }
    for (int k = 0; k < 4; k++) cui__tween(ui, &st->state_t[k], k == (int)state ? 1.0f : 0.0f, CUI__T_STATE);
    cui__spring(ui, &st->level, cui__clampf(level, 0, 1), 0.18f, 0.0f);
    cui__spring(ui, &st->scale, r.pressed ? CUI__PRESS : 1.0f, CUI_SPRING_SNAPPY);
    float size = o->size > 0 ? o->size : 160;
    int n = cui__leaf(ui, id, CUI_PX(size), CUI_PX(size));
    cui_node *nd = &ui->nodes[n];
    nd->scale = st->scale.x;
    nd->self_align = CUI_ALIGN_CENTER;
    cui__orb_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->t = ui->theme.motion > 0 ? (float)fmod(ui->time, 10000.0) : 0;
    p->level = st->level.x;
    for (int k = 0; k < 4; k++) p->w[k] = st->state_t[k];
    cui_color base = cui__color_set(o->color) ? cui_color_of(ui, o->color) : (ui->accent == CUI_ACCENT_NEUTRAL ? cui__rgba(0x4f, 0x7c, 0xf6, 255) : CUI_C(ui, ACCENT));
    p->a = base;
    /* every tone comes from the accent: a lighter core and a deeper edge, no borrowed hues */
    p->b = cui_mix(base, cui__rgba(255, 255, 255, 255), 0.28f);
    p->c = cui_mix(base, cui__rgba(255, 255, 255, 255), 0.5f);
    p->hover = r.hovered ? 1.0f : 0.0f;
    nd->paint = cui__paint_orb;
    nd->paint_data = p;
    cui__mark_interactive(ui, n, CUI_IF_FOCUSABLE);
    cui__focus_ring(ui, n, r.focus_visible ? 1.0f : 0.0f);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
    if (ui->theme.motion > 0) ui->animating = true;
    ui->last_id = id;
    return r.clicked || r.keyboard_activate;
}
