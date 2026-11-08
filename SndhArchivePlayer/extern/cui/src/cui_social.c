/* Conversation: chat bubbles, typing indicator, comments and presence. */
#include "cui_internal.h"

#define cui__fs cui__fsz

static cui_color cui__accent_bg(cui_ctx *ui) { return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_ON) : CUI_C(ui, ACCENT); }
static cui_color cui__accent_or_fg_any(cui_ctx *ui) { return cui__edge_on(ui); }
static cui_color cui__accent_text(cui_ctx *ui) { return ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, CONTROL_GLYPH) : CUI_C(ui, ACCENT_FOREGROUND); }

/* ------------------------------------------------------------------------ */
/* Chat                                                                     */
/* ------------------------------------------------------------------------ */

void cui_chat_message_ex(cui_ctx *ui, const char *text, const cui_chat_opts *o) {
    cui_chat_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    if (!text) text = "";
    /* new messages ease in */
    cui_id mid = cui__position_id(ui, cui__hash_str(text, 0xC4A7u));
    cui_id mdid = cui__position_id(ui, 0x4D44u);   /* without the text, so a streamed reply keeps its state */
    float *in = cui_state(ui, mid, sizeof(float) * 2);
    if (in[1] == 0) { in[1] = 1; in[0] = ui->frame <= 2 ? 1.0f : 0.0f; }
    cui__tween(ui, &in[0], 1.0f, 0.22f);
    int row = cui__node_new(ui, 0);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->gap = 8; rn->align = CUI_ALIGN_END;
    rn->w = CUI_FILL; rn->self_align = CUI_ALIGN_STRETCH;
    rn->justify = o->mine ? CUI_JUSTIFY_END : CUI_JUSTIFY_START;
    rn->pad[0] = o->grouped ? 2 : 10;
    rn->opacity = CUI_MAX(0.001f, in[0]);
    rn->ty = (1.0f - in[0]) * 8;
    cui__open(ui, row);
    if (!o->mine) {
        if (o->last_in_group || !o->grouped) {
            cui_avatar_opts ao;
            memset(&ao, 0, sizeof ao);
            ao.size = 28; ao.image = o->avatar;
            if (o->last_in_group) cui_avatar_ex(ui, o->author ? o->author : "?", &ao);
            else cui_gap(ui, 28);
        } else cui_gap(ui, 28);
    }
    int col = cui__node_new(ui, 0);
    cui_node *cn = &ui->nodes[col];
    cn->gap = 3; cn->align = o->mine ? CUI_ALIGN_END : CUI_ALIGN_START;
    cn->max_w = 480;
    cui__open(ui, col);
    if (!o->grouped && (o->author || o->time) && !o->mine) {
        int head = cui__node_new(ui, 0);
        ui->nodes[head].dir = CUI_ROW; ui->nodes[head].gap = 6; ui->nodes[head].pad[3] = 4;
        cui__open(ui, head);
        if (o->author) {
            int a = cui__text_node(ui, o->author, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_SECONDARY), false);
            ui->nodes[a].line_height = 1.3f;
        }
        if (o->time) {
            int t = cui__text_node(ui, o->time, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
            ui->nodes[t].line_height = 1.3f;
        }
        cui__close(ui);
    }
    int bubble = cui__node_new(ui, 0);
    cui_node *bn = &ui->nodes[bubble];
    bn->pad[0] = bn->pad[2] = 8; bn->pad[1] = bn->pad[3] = 13;
    float big = 18, small = 6;
    if (o->mine) bn->radius = (cui_corners){ big, o->grouped ? small : big, o->last_in_group ? big : small, big };
    else bn->radius = (cui_corners){ o->grouped ? small : big, big, big, o->last_in_group ? big : small };
    if (o->mine) bn->bg = cui__accent_bg(ui);
    else bn->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.08f) : CUI_C(ui, SURFACE_MUTED);
    cui__open(ui, bubble);
    if (o->markdown) {
        cui_markdown_opts mo;
        memset(&mo, 0, sizeof mo);
        mo.compact = true;
        mo.fit_content = true;
        if (o->mine) mo.color = cui__accent_text(ui);
        cui_push_id_int(ui, (int)mdid);
        cui_markdown_result mr = cui_markdown_ex(ui, "md", text, &mo);
        cui_pop_id(ui);
        if (o->markdown_result) *o->markdown_result = mr;
    } else {
        int t = cui__text_node(ui, text, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), o->mine ? cui__accent_text(ui) : CUI_C(ui, FOREGROUND), true);
        ui->nodes[t].line_height = 1.45f;
    }
    cui__close(ui);
    if (o->mine && o->status && (o->last_in_group || !o->grouped)) {
        int st = cui__node_new(ui, 0);
        ui->nodes[st].dir = CUI_ROW; ui->nodes[st].gap = 4; ui->nodes[st].align = CUI_ALIGN_CENTER; ui->nodes[st].pad[1] = 4;
        cui__open(ui, st);
        cui__icon_node(ui, CUI_ICON_CHECK_CHECK, 12, cui__accent_or_fg_any(ui));
        int s = cui__text_node(ui, o->time ? cui__frame_printf(ui, "%s %s", o->status, o->time) : o->status, -1, CUI_FONT_BODY,
                               cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[s].line_height = 1.3f;
        cui__close(ui);
    }
    cui__close(ui);
    cui__close(ui);
}

void cui_chat_divider(cui_ctx *ui, const char *label) {
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 12; ui->nodes[row].align = CUI_ALIGN_CENTER;
    ui->nodes[row].w = CUI_FILL; ui->nodes[row].pad[0] = 14; ui->nodes[row].pad[2] = 6;
    cui__open(ui, row);
    int l = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[l].bg = CUI_C(ui, BORDER);
    int t = cui__text_node(ui, label, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
    ui->nodes[t].line_height = 1.2f;
    int r = cui__leaf(ui, 0, CUI_FILL, CUI_PX(1));
    ui->nodes[r].bg = CUI_C(ui, BORDER);
    cui__close(ui);
}

typedef struct { double time; cui_color c; } cui__dots_paint;
static void cui__paint_typing(cui_ctx *ui, cui_node *n, void *data) {
    cui__dots_paint *p = data;
    cui_rect r = n->rect;
    for (int k = 0; k < 3; k++) {
        float ph = (float)p->time * 7.0f - k * 0.7f;
        float b = 0.5f + 0.5f * sinf(ph);
        float y = r.y + r.h * 0.5f - b * 3.0f;
        cui_draw_circle(ui, (cui_vec2){ r.x + 5 + k * 9.0f, y }, 3.0f, cui_alpha(p->c, 0.45f + 0.55f * b));
    }
}

void cui_typing_indicator(cui_ctx *ui, const char *name) {
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 8; ui->nodes[row].align = CUI_ALIGN_END; ui->nodes[row].pad[0] = 8;
    cui__open(ui, row);
    cui_avatar_opts ao;
    memset(&ao, 0, sizeof ao);
    ao.size = 28;
    cui_avatar_ex(ui, name ? name : "?", &ao);
    int bubble = cui__node_new(ui, 0);
    cui_node *bn = &ui->nodes[bubble];
    bn->pad[0] = bn->pad[2] = 13; bn->pad[1] = bn->pad[3] = 14;
    bn->radius = (cui_corners){ 18, 18, 18, 6 };
    bn->bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, FOREGROUND), 0.08f) : CUI_C(ui, SURFACE_MUTED);
    cui__open(ui, bubble);
    int dots = cui__leaf(ui, 0, CUI_PX(28), CUI_PX(12));
    cui__dots_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->time = ui->time; p->c = CUI_C(ui, TEXT_SECONDARY);
    ui->nodes[dots].paint = cui__paint_typing;
    ui->nodes[dots].paint_data = p;
    cui__close(ui);
    if (name) {
        int t = cui__text_node(ui, cui__frame_printf(ui, "%s is typing", name), -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[t].line_height = 1.3f;
        ui->nodes[t].self_align = CUI_ALIGN_CENTER;
    }
    cui__close(ui);
    ui->animating = true;
}

/* ------------------------------------------------------------------------ */
/* Comments                                                                 */
/* ------------------------------------------------------------------------ */

/* where each depth's last avatar ended, written as rows paint, so a reply's rail starts under its parent's avatar */
typedef struct { float bottom[8]; } cui__thread_rail;
typedef struct { cui_color c; int depth; float indent, av, parent_indent, parent_av; cui__thread_rail *rail; } cui__thread_paint;
static void cui__paint_thread(cui_ctx *ui, cui_node *n, void *data) {
    cui__thread_paint *p = data;
    cui_rect r = n->rect;
    float ay = r.y + 6, mid = ay + p->av * 0.5f;
    if (p->depth > 0 && p->depth < 8) {
        /* down from the parent's avatar, then a soft elbow into this reply's avatar */
        float x = r.x + p->parent_indent + p->parent_av * 0.5f - 0.75f;
        float top = CUI_MIN(p->rail->bottom[p->depth - 1] + 6, mid - 8);
        float end = r.x + p->indent - 6;
        cui_draw_rect(ui, (cui_rect){ x, top, 1.5f, mid - 8 - top }, CUI_CORNERS(0.75f), p->c);
        cui_draw_arc(ui, (cui_vec2){ x + 8.75f, mid - 8 }, 8, CUI_PI * 0.5f, CUI_PI, 1.5f, p->c);
        if (end > x + 8.75f) cui_draw_rect(ui, (cui_rect){ x + 8.75f, mid - 0.75f, end - x - 8.75f, 1.5f }, CUI_CORNERS(0.75f), p->c);
    }
    if (p->depth < 8) p->rail->bottom[p->depth] = ay + p->av;
}

int cui_comment_ex(cui_ctx *ui, const char *id_str, const char *author, const char *text, const cui_comment_opts *o) {
    cui_comment_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int result = 0;
    float indent = 44.0f * (float)CUI_MAX(0, o->depth);
    float av = o->depth > 0 ? 28 : 34;
    cui_push_id(ui, id_str);
    int row = cui__node_new(ui, 0);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->gap = 12; rn->align = CUI_ALIGN_START;
    rn->w = CUI_FILL; rn->pad[3] = indent; rn->pad[0] = 6; rn->pad[2] = 6;
    cui__thread_paint *tp = cui__frame_alloc(ui, sizeof *tp);
    tp->c = CUI_C(ui, BORDER);
    tp->depth = CUI_MAX(0, o->depth);
    tp->indent = indent; tp->av = av;
    tp->parent_indent = CUI_MAX(0.0f, indent - 44); tp->parent_av = o->depth > 1 ? 28 : 34;
    tp->rail = cui_state(ui, (cui_id)0x7EAD5EEDu, sizeof(cui__thread_rail));
    rn->paint = cui__paint_thread;
    rn->paint_data = tp;
    cui__open(ui, row);
    cui_avatar_opts ao;
    memset(&ao, 0, sizeof ao);
    ao.size = av;
    int avn = ui->node_count;
    cui_avatar_ex(ui, author, &ao);
    ui->nodes[avn].self_align = CUI_ALIGN_START;   /* beside the name, not the middle of a long comment */
    int col = cui__node_new(ui, 0);
    ui->nodes[col].w = CUI_FILL; ui->nodes[col].gap = 4;
    cui__open(ui, col);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].gap = 8; ui->nodes[head].align = CUI_ALIGN_CENTER;
    cui__open(ui, head);
    int an = cui__text_node(ui, author, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
    ui->nodes[an].line_height = 1.4f;
    if (o->badge) {
        cui_badge_opts bo;
        memset(&bo, 0, sizeof bo);
        bo.size = CUI_SIZE_SM;
        cui_badge_ex(ui, o->badge, &bo);
    }
    if (o->time) {
        int tt = cui__text_node(ui, o->edited ? cui__frame_printf(ui, "%s \xc2\xb7 edited", o->time) : o->time, -1, CUI_FONT_BODY,
                                cui__fs(ui, ui->theme.text_xs), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[tt].line_height = 1.4f;
    }
    cui__close(ui);
    int body = cui__text_node(ui, text, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), true);
    ui->nodes[body].line_height = 1.5f;
    int acts = cui__node_new(ui, 0);
    ui->nodes[acts].dir = CUI_ROW; ui->nodes[acts].gap = 2; ui->nodes[acts].pad[0] = 2;
    ui->nodes[acts].tx = -11;   /* the first icon lines up with the text above */
    cui__open(ui, acts);
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST; bo.icon = CUI_ICON_HEART;
    bool liked = o->liked && *o->liked;   /* liked tints the heart; no pill, so the row keeps its rhythm */
    bo.color = liked ? (ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, DANGER) : CUI_C(ui, ACCENT)) : CUI_C(ui, TEXT_SECONDARY);
    const char *lk = o->likes && *o->likes > 0 ? cui__frame_printf(ui, "%d###like", *o->likes) : "###like";
    if (cui_button_ex(ui, lk, &bo)) {
        result |= CUI_COMMENT_LIKE;
        if (o->liked) {
            *o->liked = !*o->liked;
            if (o->likes) *o->likes += *o->liked ? 1 : -1;
        }
    }
    memset(&bo, 0, sizeof bo);
    bo.size = CUI_SIZE_SM; bo.variant = CUI_BUTTON_GHOST; bo.icon = CUI_ICON_MESSAGE_SQUARE;
    bo.color = CUI_C(ui, TEXT_SECONDARY);
    if (cui_button_ex(ui, "Reply", &bo)) result |= CUI_COMMENT_REPLY;
    cui__close(ui);
    cui__close(ui);
    cui__close(ui);
    cui_pop_id(ui);
    return result;
}

/* ------------------------------------------------------------------------ */
/* Presence                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct { cui_spring spread; } cui__pres_state;

void cui_presence(cui_ctx *ui, const char *id_str, const cui_presence_user *users, int count, int max_visible, float size) {
    if (size <= 0) size = 30;
    if (max_visible <= 0) max_visible = 5;
    cui_id id = cui_make_id(ui, id_str);
    cui__pres_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, 0);
    cui__spring(ui, &st->spread, r.hovered ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);
    float t = cui__clampf(st->spread.x, 0, 1.2f);
    float ring = 2, outer = size + ring * 2 + 2;
    int row = cui__node_new(ui, id);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->align = CUI_ALIGN_CENTER;
    rn->gap = cui__lerp(-outer * 0.24f, 4, t);   /* overlapped, yet the next ring stops short of two wide initials */
    rn->self_align = CUI__ALIGN_NOSTRETCH;
    cui__mark_interactive(ui, row, 0);
    cui__open(ui, row);
    int shown = CUI_MIN(count, max_visible);
    for (int k = 0; k < shown; k++) {
        const cui_presence_user *u = &users[k];
        cui_id uid = cui__hash(&k, sizeof k, id);
        cui_interact ur = cui__interact(ui, uid, 0);
        int w = cui__node_new(ui, uid);
        cui_node *wn = &ui->nodes[w];
        wn->w = CUI_PX(outer); wn->h = CUI_PX(outer);
        wn->radius = CUI_CORNERS(outer * 0.5f);
        wn->bg = CUI_C(ui, BACKGROUND);          /* cuts the avatar out of its neighbor */
        wn->dir = CUI_STACK; wn->align = CUI_ALIGN_CENTER; wn->justify = CUI_JUSTIFY_CENTER;
        cui__mark_interactive(ui, w, 0);
        cui__open(ui, w);
        int ringn = cui__node_new(ui, 0);
        cui_node *rg = &ui->nodes[ringn];
        rg->w = CUI_PX(size + ring * 2); rg->h = CUI_PX(size + ring * 2);
        rg->radius = CUI_CORNERS((size + ring * 2) * 0.5f);
        rg->border = ring;
        rg->border_color = u->idle ? CUI_C(ui, BORDER_STRONG) : (cui__color_set(u->color) ? cui_color_of(ui, u->color) : cui__accent_bg(ui));
        rg->dir = CUI_STACK; rg->align = CUI_ALIGN_CENTER; rg->justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, ringn);
        cui_avatar_opts ao;
        memset(&ao, 0, sizeof ao);
        ao.size = size - 2; ao.image = u->avatar;
        cui_avatar_ex(ui, u->name, &ao);
        if (u->idle) ui->nodes[ui->last_node].opacity = 0.55f;
        cui__close(ui);
        cui__close(ui);
        if (ur.hovered || r.hovered) {
            ui->last_id = uid;
            cui_tooltip(ui, u->idle ? cui__frame_printf(ui, "%s (away)", u->name) : u->name);
        }
    }
    if (count > shown) {
        int m = cui__node_new(ui, 0);
        cui_node *mn = &ui->nodes[m];
        mn->w = CUI_PX(outer); mn->h = CUI_PX(outer);
        mn->radius = CUI_CORNERS(outer * 0.5f);
        mn->bg = CUI_C(ui, BACKGROUND);
        mn->dir = CUI_STACK; mn->align = CUI_ALIGN_CENTER; mn->justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, m);
        int in = cui__node_new(ui, 0);
        cui_node *inn = &ui->nodes[in];
        inn->w = CUI_PX(size + ring * 2); inn->h = CUI_PX(size + ring * 2);
        inn->radius = CUI_CORNERS((size + ring * 2) * 0.5f);
        inn->bg = CUI_C(ui, SURFACE_MUTED); inn->border = 1; inn->border_color = CUI_C(ui, BORDER);
        inn->dir = CUI_STACK; inn->align = CUI_ALIGN_CENTER; inn->justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, in);
        int t2 = cui__text_node(ui, cui__frame_printf(ui, "+%d", count - shown), -1, CUI_FONT_MEDIUM, cui__fs(ui, size * 0.36f), CUI_C(ui, TEXT_SECONDARY), false);
        ui->nodes[t2].line_height = 1.0f;
        cui__close(ui);
        cui__close(ui);
    }
    cui__close(ui);
    ui->last_id = id;
}
