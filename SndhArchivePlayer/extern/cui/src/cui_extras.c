/* More components: radio cards, tag input, one time code input, button
 * groups, copy and hold to confirm buttons, password strength, timeline and
 * progress ring. */
#include "cui_internal.h"

static float cui__fs(cui_ctx *ui, float size) { return size * ui->theme.font_scale; }
int cui__text_field(cui_ctx *ui, cui_id id, char *buf, size_t cap, const cui_input_opts *o, bool multiline, int style, const char *search_icon);
void cui__paint_focus_ring_node(cui_ctx *ui, cui_node *n, void *data);

static void cui__focus_after(cui_ctx *ui, int node, bool show) {
    if (!show) return;
    float *fa = cui__frame_alloc(ui, sizeof(float));
    *fa = 1;
    ui->nodes[node].paint_data = fa;
    ui->nodes[node].paint = cui__paint_focus_ring_node;
    ui->nodes[node].flags |= CUI_NF_PAINT_AFTER;
}

/* ------------------------------------------------------------------------ */
/* Radio cards                                                              */
/* ------------------------------------------------------------------------ */

typedef struct { float t, hover; bool init; } cui__rc_state;
typedef struct { float t, hover; } cui__dot_paint;

static void cui__paint_radio_dot(cui_ctx *ui, cui_node *n, void *data) {
    cui__dot_paint *p = data;
    cui_rect r = n->rect;
    float rr = r.w * 0.5f;
    cui_color on = CUI_C(ui, CONTROL_ON);
    cui_color border = cui__control_edge(ui, p->hover);
    cui_draw_rect_border(ui, r, CUI_CORNERS(rr), cui_mix(CUI_C(ui, SURFACE), on, p->t), cui_mix(border, on, p->t), 1);
    if (p->t > 0.02f) {
        cui_vec2 c = { r.x + rr, r.y + rr };
        cui_draw_circle(ui, c, 3.5f * p->t, CUI_C(ui, CONTROL_GLYPH));
    }
}

bool cui_radio_cards_ex(cui_ctx *ui, const char *id_str, const cui_choice *choices, int count, int *index, const cui_radio_cards_opts *o) {
    cui_radio_cards_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    int old = *index;
    cui_id gid = cui_make_id(ui, id_str);
    cui_push_id(ui, id_str);
    int cols = o->columns > 0 ? o->columns : (count <= 3 ? count : 2);
    cui_box gb;
    memset(&gb, 0, sizeof gb);
    gb.dir = CUI_GRID; gb.columns = cols; gb.gap = 12;
    gb.min_column_width = 220;   /* fewer columns, down to one, in a narrow container: room for a title and its badge */
    int grid = cui__node_new(ui, gid);
    cui__apply_box(ui, &ui->nodes[grid], &gb);
    cui__open(ui, grid);
    for (int k = 0; k < count; k++) {
        cui_id cid = cui__hash(&k, sizeof k, gid);
        cui_interact r = cui__interact(ui, cid, CUI_IF_FOCUSABLE);
        if (r.clicked) *index = k;
        if (r.focus_visible) {
            if ((cui_key_pressed(ui, CUI_KEY_RIGHT) || cui_key_pressed(ui, CUI_KEY_DOWN)) && k < count - 1) {
                *index = k + 1; ui->focus = cui__hash(&(int){ k + 1 }, sizeof(int), gid);
            }
            if ((cui_key_pressed(ui, CUI_KEY_LEFT) || cui_key_pressed(ui, CUI_KEY_UP)) && k > 0) {
                *index = k - 1; ui->focus = cui__hash(&(int){ k - 1 }, sizeof(int), gid);
            }
        }
        bool sel = *index == k;
        cui__rc_state *st = cui_state(ui, cid, sizeof *st);
        if (!st->init) { st->t = sel ? 1.0f : 0.0f; st->init = true; }
        cui__tween(ui, &st->t, sel ? 1.0f : 0.0f, CUI__T_STATE);
        cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
        int c = cui__node_new(ui, cid);
        cui_node *n = &ui->nodes[c];
        n->dir = CUI_ROW; n->gap = 12; n->align = CUI_ALIGN_START;
        n->pad[0] = n->pad[1] = n->pad[2] = n->pad[3] = 16;
        n->radius = CUI_CORNERS(ui->theme.radius_control);
        cui_color sel_border = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
        n->bg = cui_mix(CUI_C(ui, SURFACE), cui_mix(CUI_C(ui, SURFACE), CUI_C(ui, ACCENT_SUBTLE), 1.0f), st->t * 0.6f);
        n->border = 1 + 0.5f * st->t;
        n->border_color = cui_mix(cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), st->hover), sel_border, st->t);
        cui__mark_interactive(ui, c, CUI_IF_FOCUSABLE);
        if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_HAND);
        cui__open(ui, c);
        if (choices[k].icon) {
            int ib = cui__node_new(ui, 0);
            cui_node *in = &ui->nodes[ib];
            in->w = CUI_PX(20); in->h = CUI_PX(22);   /* a bare icon beside the title */
            in->dir = CUI_STACK; in->align = CUI_ALIGN_CENTER; in->justify = CUI_JUSTIFY_CENTER;
            cui__open(ui, ib);
            cui__icon_node(ui, choices[k].icon, 18, sel ? CUI_C(ui, FOREGROUND) : CUI_C(ui, TEXT_SECONDARY));
            cui__close(ui);
        }
        int col = cui__node_new(ui, 0);
        ui->nodes[col].gap = 3; ui->nodes[col].w = CUI_FILL;
        cui__open(ui, col);
        int tr = cui__node_new(ui, 0);
        ui->nodes[tr].dir = CUI_ROW; ui->nodes[tr].gap = 8; ui->nodes[tr].w = CUI_FILL; ui->nodes[tr].align = CUI_ALIGN_START;
        cui__open(ui, tr);
        /* the title wraps rather than truncates, so a badge beside it never squeezes either */
        int t = cui__text_node(ui, choices[k].title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), true);
        ui->nodes[t].line_height = 1.35f;
        ui->nodes[t].w = CUI_FILL;
        if (choices[k].badge) {
            cui_badge_opts bo;
            memset(&bo, 0, sizeof bo);
            bo.tone = CUI_TONE_ACCENT; bo.size = CUI_SIZE_SM;
            cui_badge_ex(ui, choices[k].badge, &bo);
        }
        cui__close(ui);
        if (choices[k].description)
            cui__text_node(ui, choices[k].description, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_SECONDARY), true);
        cui__close(ui);
        int dot = cui__leaf(ui, 0, CUI_PX(18), CUI_PX(18));
        cui__dot_paint *dp = cui__frame_alloc(ui, sizeof *dp);
        dp->t = st->t; dp->hover = st->hover;
        ui->nodes[dot].paint = cui__paint_radio_dot;
        ui->nodes[dot].paint_data = dp;
        cui__close(ui);
        cui__focus_after(ui, c, r.focus_visible);
    }
    cui__close(ui);
    cui_pop_id(ui);
    return *index != old;
}

/* ------------------------------------------------------------------------ */
/* Tag input                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { char typing[96]; float hover; } cui__tag_state;

static int cui__tag_split(const char *buf, int *starts, int *lens, int max) {
    int n = 0, i = 0, len = (int)strlen(buf);
    while (i < len && n < max) {
        while (i < len && (buf[i] == ',' || buf[i] == ' ')) i++;
        int s = i;
        while (i < len && buf[i] != ',') i++;
        int e = i;
        while (e > s && buf[e - 1] == ' ') e--;
        if (e > s) { starts[n] = s; lens[n] = e - s; n++; }
    }
    return n;
}

static void cui__tag_rebuild(char *buf, size_t cap, const char *src, const int *starts, const int *lens, int n, int skip) {
    char tmp[2048];
    size_t o = 0;
    for (int k = 0; k < n; k++) {
        if (k == skip) continue;
        if (o && o + 2 < sizeof tmp) { tmp[o++] = ','; tmp[o++] = ' '; }
        for (int j = 0; j < lens[k] && o + 1 < sizeof tmp; j++) tmp[o++] = src[starts[k] + j];
    }
    tmp[o] = 0;
    snprintf(buf, cap, "%s", tmp);
}

static void cui__tag_add(char *buf, size_t cap, const char *tag) {
    while (*tag == ' ') tag++;
    size_t tl = strlen(tag);
    while (tl > 0 && tag[tl - 1] == ' ') tl--;
    if (tl == 0) return;
    size_t len = strlen(buf);
    if (len + tl + 3 >= cap) return;
    if (len) { buf[len++] = ','; buf[len++] = ' '; }
    memcpy(buf + len, tag, tl);
    buf[len + tl] = 0;
}

bool cui_tag_input_ex(cui_ctx *ui, const char *id_str, char *buf, size_t cap, const cui_tag_opts *o) {
    cui_tag_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_id id = cui_make_id(ui, id_str);
    cui_id fid = id ^ 0x7A6u;
    cui__tag_state *st = cui_state(ui, id, sizeof *st);
    bool changed = false;
    int starts[64], lens[64];
    int n = cui__tag_split(buf, starts, lens, 64);
    bool full = o->max_tags > 0 && n >= o->max_tags;
    if (ui->focus == fid && cui_key_pressed(ui, CUI_KEY_BACKSPACE) && st->typing[0] == 0 && n > 0) {
        cui__tag_rebuild(buf, cap, buf, starts, lens, n, n - 1);
        n = cui__tag_split(buf, starts, lens, 64);
        changed = true;
    }
    cui_interact r = cui__interact(ui, id, 0);
    if (r.clicked) ui->focus = fid;
    cui__tween(ui, &st->hover, r.hovered ? 1.0f : 0.0f, CUI__T_HOVER);
    int outer = -1;
    if (o->label) {
        outer = cui__node_new(ui, 0);
        ui->nodes[outer].gap = 6;
        ui->nodes[outer].w = o->width.kind ? o->width : CUI_FILL;
        cui__open(ui, outer);
        cui__field_label(ui, o->label);
    }
    float h = cui__control_height(ui, CUI_SIZE_MD);
    int box = cui__node_new(ui, id);
    cui_node *bn = &ui->nodes[box];
    bn->dir = CUI_ROW; bn->align = CUI_ALIGN_CENTER; bn->gap = 6;
    bn->flags |= CUI_NF_ROW_WRAP;   /* on a narrow screen tags take more lines instead of overflowing */
    bn->min_h = h;
    bn->w = outer >= 0 ? CUI_FILL : (o->width.kind ? o->width : CUI_FILL);
    bn->pad[3] = 6; bn->pad[1] = CUI__FIELD_PAD;
    bn->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, h * 0.5f));
    bn->bg = CUI_C(ui, SURFACE);
    bn->border = 1;
    bool focused = ui->focus == fid;
    cui_color border = cui__control_edge(ui, st->hover);
    bn->border_color = focused ? cui__edge_on(ui) : border;
    bn->flags |= CUI_NF_CLIP;
    cui__mark_interactive(ui, box, 0);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_TEXT);
    cui__open(ui, box);
    if (n == 0) cui_gap(ui, 6);
    int remove = -1;
    cui_push_id(ui, id_str);
    for (int k = 0; k < n; k++) {
        int chip = cui__node_new(ui, 0);
        cui_node *cn = &ui->nodes[chip];
        cn->dir = CUI_ROW; cn->align = CUI_ALIGN_CENTER; cn->gap = 2;
        cn->h = CUI_PX(24); cn->pad[3] = 9; cn->pad[1] = 3;
        cn->radius = CUI_CORNERS(12);
        cn->bg = CUI_C(ui, ACTIVE);
        cn->self_align = CUI__ALIGN_NOSTRETCH;
        cui__open(ui, chip);
        int t = cui__text_node(ui, buf + starts[k], lens[k], CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, FOREGROUND), false);
        ui->nodes[t].line_height = 1.2f;
        cui_button_opts xo;
        memset(&xo, 0, sizeof xo);
        xo.size = CUI_SIZE_SM;
        cui_push_id_int(ui, k);
        if (cui_icon_button_ex(ui, CUI_ICON_X, &xo)) remove = k;
        cui_pop_id(ui);
        ui->nodes[ui->last_node].w = CUI_PX(18); ui->nodes[ui->last_node].h = CUI_PX(18);
        cui__close(ui);
    }
    cui_pop_id(ui);
    cui_input_opts io;
    memset(&io, 0, sizeof io);
    io.placeholder = n == 0 ? (o->placeholder ? o->placeholder : "Add a tag") : NULL;
    io.read_only = full;
    int f = cui__text_field(ui, fid, st->typing, sizeof st->typing, &io, false, 1, NULL);
    ui->nodes[ui->last_node].min_w = 60;
    cui__close(ui);
    if (outer >= 0) cui__close(ui);
    if (remove >= 0) {
        cui__tag_rebuild(buf, cap, buf, starts, lens, n, remove);
        changed = true;
    }
    if ((f & CUI_CHANGED) && strchr(st->typing, ',')) {
        char *p = st->typing, *c;
        while ((c = strchr(p, ',')) != NULL) {
            *c = 0;
            if (!full) cui__tag_add(buf, cap, p);
            p = c + 1;
        }
        memmove(st->typing, p, strlen(p) + 1);
        changed = true;
    }
    if ((f & CUI_SUBMITTED) && st->typing[0] && !full) {
        cui__tag_add(buf, cap, st->typing);
        st->typing[0] = 0;
        changed = true;
    }
    ui->last_id = id;
    return changed;
}

/* ------------------------------------------------------------------------ */
/* One time code                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { float focus; bool was_complete; } cui__otp_state;

bool cui_otp_input(cui_ctx *ui, const char *id_str, char *code, int length) {
    if (length <= 0) return false;
    if (length > 12) length = 12;
    cui_id id = cui_make_id(ui, id_str);
    cui__otp_state *st = cui_state(ui, id, sizeof *st);
    cui_interact r = cui__interact(ui, id, CUI_IF_FOCUSABLE | CUI_IF_TEXT);
    bool focused = ui->focus == id;
    int len = (int)strnlen(code, (size_t)length);
    code[len] = 0;
    if (focused) {
        ui->edit = (struct cui_edit_state *)(void *)st;   /* tells the app a text field has focus */
        for (int i = 0; i < ui->text_in_len && len < length; i++) {
            char c = ui->text_in[i];
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) { code[len++] = c; code[len] = 0; }
        }
        if (cui_key_pressed(ui, CUI_KEY_BACKSPACE) && len > 0) code[--len] = 0;
        if ((ui->mods & CUI_MOD_PRIMARY) && cui_key_pressed(ui, CUI_KEY_V) && ui->platform.get_clipboard) {
            const char *clip = ui->platform.get_clipboard(ui->platform.user);
            len = 0;
            for (const char *p = clip; p && *p && len < length; p++)
                if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) code[len++] = *p;
            code[len] = 0;
        }
        if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) ui->focus = 0;
        cui__wakeup_after(ui, 0.53f);
    }
    cui__tween(ui, &st->focus, focused ? 1.0f : 0.0f, CUI__T_FOCUS);
    /* boxes are 44 points wide when there is room and give way down to 30 on a phone */
    int row = cui__node_new(ui, id);
    float natural = length * 44.0f + (length - 1) * 8.0f + (length >= 6 ? 18.0f : 0.0f);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 8;
    ui->nodes[row].w = CUI_FILL; ui->nodes[row].max_w = natural; ui->nodes[row].self_align = CUI_ALIGN_START;
    cui__mark_interactive(ui, row, CUI_IF_FOCUSABLE);
    if (r.hovered) cui__set_cursor(ui, CUI_CURSOR_TEXT);
    cui__open(ui, row);
    cui_color accent = ui->accent == CUI_ACCENT_NEUTRAL ? CUI_C(ui, FOREGROUND) : CUI_C(ui, ACCENT);
    double phase = ui->time - ui->caret_time;
    for (int k = 0; k < length; k++) {
        bool current = focused && k == CUI_MIN(len, length - 1) && (len < length || k == length - 1);
        int b = cui__node_new(ui, 0);
        cui_node *bn = &ui->nodes[b];
        bn->w = CUI_GROW(1); bn->min_w = 30; bn->max_w = 44; bn->h = CUI_PX(52);
        bn->radius = CUI_CORNERS(ui->theme.radius_control);
        bn->bg = CUI_C(ui, SURFACE);
        bn->border = current ? 1.5f : 1;
        bn->border_color = current ? accent : (k < len ? CUI_C(ui, TEXT_MUTED) : CUI_C(ui, BORDER_STRONG));
        bn->dir = CUI_STACK; bn->align = CUI_ALIGN_CENTER; bn->justify = CUI_JUSTIFY_CENTER;
        cui__open(ui, b);
        if (k < len) {
            char ch[2] = { code[k], 0 };
            int t = cui__text_node(ui, ch, 1, CUI_FONT_DISPLAY, cui__fs(ui, 22), CUI_C(ui, FOREGROUND), false);
            ui->nodes[t].line_height = 1.0f;
        } else if (current && (phase < 0.5 || fmod(phase, 1.06) < 0.6)) {
            int caret = cui__leaf(ui, 0, CUI_PX(1.5f), CUI_PX(22));
            ui->nodes[caret].bg = accent;
        }
        cui__close(ui);
        if (k == length / 2 - 1 && length >= 6) {
            int dash = cui__leaf(ui, 0, CUI_PX(10), CUI_PX(2));
            ui->nodes[dash].bg = CUI_C(ui, BORDER_STRONG);
            ui->nodes[dash].radius = CUI_CORNERS(1);
        }
    }
    cui__close(ui);
    cui__focus_after(ui, row, r.focus_visible);
    bool complete = len == length;
    bool fire = complete && !st->was_complete;
    st->was_complete = complete;
    ui->last_id = id;
    return fire;
}

/* ------------------------------------------------------------------------ */
/* Button group                                                             */
/* ------------------------------------------------------------------------ */

static void cui__paint_group_dividers(cui_ctx *ui, cui_node *n, void *data) {
    (void)data;
    for (int c = n->first; c >= 0; c = ui->nodes[c].next) {
        cui_node *ch = &ui->nodes[c];
        if (c == n->first) continue;
        cui_rect line = { ch->rect.x - 0.5f, n->rect.y + 1, 1, n->rect.h - 2 };
        cui_draw_rect(ui, line, CUI_CORNERS(0), CUI_C(ui, BORDER_STRONG));
    }
}

void cui_button_group_begin(cui_ctx *ui) {
    int g = cui__node_new(ui, 0);
    cui_node *n = &ui->nodes[g];
    n->dir = CUI_ROW;
    n->self_align = CUI__ALIGN_NOSTRETCH;
    n->border = 1;
    n->border_color = cui__quiet_edge(ui, 0);
    n->radius = CUI_CORNERS(CUI_MIN(ui->theme.radius_control, ui->theme.control_md * 0.5f));
    n->flags |= CUI_NF_CLIP | CUI_NF_PAINT_AFTER;
    n->paint = cui__paint_group_dividers;
    cui__open(ui, g);
    ui->button_group_depth++;
}

void cui_button_group_end(cui_ctx *ui) {
    int g = cui__current(ui);
    if (ui->button_group_depth > 0) ui->button_group_depth--;
    if (g < 0) return;
    cui_node *gn = &ui->nodes[g];
    float r = gn->radius.tl;
    /* squeezed (a phone): the buttons drop to 8px side padding before their labels truncate.
       The group keeps asking for its natural width, so the padding comes back with the room */
    if (gn->first >= 0 && ui->nodes[gn->first].id && !gn->id) {
        gn->id = ui->nodes[gn->first].id ^ 0xB6A0u;
        float natural = 0;
        int nb = 0;
        for (int c = gn->first; c >= 0; c = ui->nodes[c].next, nb++) {
            cui_node *ch = &ui->nodes[c];
            if (ch->w.kind == CUI_SIZE_FIXED) { natural += ch->w.value; continue; }
            if (ch->w.kind != CUI_SIZE_FIT) { natural = -1; break; }
            float w = ch->pad[1] + ch->pad[3];
            int kids = 0;
            for (int t = ch->first; t >= 0; t = ui->nodes[t].next, kids++) {
                cui_node *tn = &ui->nodes[t];
                if (tn->w.kind == CUI_SIZE_FIXED) w += tn->w.value;
                else if ((tn->flags & CUI_NF_TEXT) && tn->w.kind == CUI_SIZE_FIT) w += ceilf(cui__text_width(ui, tn->font, tn->font_size, tn->text, tn->text_len, (tn->flags & CUI_NF_TABULAR) != 0) * 4) / 4;
                else { natural = -1; break; }
            }
            if (natural < 0) break;
            natural += w + (kids > 1 ? ch->gap * (float)(kids - 1) : 0);
        }
        if (natural > 0) {
            cui_rect pr = cui__prev_rect(ui, gn->id);
            gn->w = CUI_FIT; gn->pref_hint = natural + gn->pad[1] + gn->pad[3];
            if (pr.w > 0 && pr.w < gn->pref_hint - 0.5f)
                for (int c = gn->first; c >= 0; c = ui->nodes[c].next)
                    if (ui->nodes[c].w.kind == CUI_SIZE_FIT && ui->nodes[c].pad[1] > 8) ui->nodes[c].pad[1] = ui->nodes[c].pad[3] = 8;
        }
        gn = &ui->nodes[g];
    }
    for (int c = gn->first; c >= 0; c = ui->nodes[c].next) {
        cui_node *ch = &ui->nodes[c];
        bool first = c == gn->first, last = ch->next < 0;
        ch->radius.tl = ch->radius.bl = first ? r : 0;
        ch->radius.tr = ch->radius.br = last ? r : 0;
        ch->border = 0;
        ch->scale = 1;   /* no press scale inside a group, the shape is shared */
    }
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Copy and hold buttons                                                    */
/* ------------------------------------------------------------------------ */

typedef struct { double copied_at; } cui__copy_state;

bool cui_copy_button(cui_ctx *ui, const char *id_str, const char *text) {
    cui_id sid = cui_make_id(ui, id_str) ^ 0xC0B7u;
    cui__copy_state *st = cui_state(ui, sid, sizeof *st);
    bool recent = st->copied_at > 0 && ui->time - st->copied_at < 1.6;
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.tooltip = recent ? "Copied" : "Copy";
    cui_push_id(ui, id_str);
    bool clicked = cui_icon_button_ex(ui, recent ? CUI_ICON_CHECK : CUI_ICON_COPY, &bo);
    cui_pop_id(ui);
    if (recent) {
        /* the check is green while it confirms */
        cui_node *bn = &ui->nodes[ui->last_node];
        for (int c = bn->first; c >= 0; c = ui->nodes[c].next) ui->nodes[c].text_color = CUI_C(ui, SUCCESS);
        cui__wakeup_after(ui, (float)(1.6 - (ui->time - st->copied_at)) + 0.01f);
    }
    if (clicked) {
        if (ui->platform.set_clipboard && text) ui->platform.set_clipboard(ui->platform.user, text);
        st->copied_at = ui->time;
    }
    return clicked;
}

typedef struct { float progress; cui_spring back; bool fired; } cui__hold_state;
typedef struct { float t; cui_color c; } cui__hold_paint;

static void cui__paint_hold(cui_ctx *ui, cui_node *n, void *data) {
    cui__hold_paint *p = data;
    cui_rect r = n->rect;
    cui__draw_sheen(ui, r, n->radius, 1.0f);   /* the same light as every solid button */
    if (p->t <= 0.001f) return;
    /* the fill follows the button's own corners as it sweeps across */
    cui_push_clip(ui, (cui_rect){ r.x, r.y, r.w * p->t, r.h });
    cui_draw_rect(ui, r, n->radius, p->c);
    cui_pop_clip(ui);
}

bool cui_hold_button_ex(cui_ctx *ui, const char *label, const cui_hold_opts *o) {
    cui_hold_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    float duration = o->duration > 0 ? o->duration : 1.2f;
    cui_id id = cui_make_id(ui, label);
    cui__hold_state *st = cui_state(ui, id ^ 0x401Du, sizeof *st);
    bool held = ui->active == id && ui->mdown[0] && ui->hot == id;
    if (ui->focus == id && ui->focus_visible && (cui_key_down(ui, CUI_KEY_SPACE) || cui_key_down(ui, CUI_KEY_ENTER))) held = true;
    bool fire = false;
    if (held && !st->fired) {
        st->progress += ui->dt / duration;
        if (st->progress >= 1) { st->progress = 1; st->fired = true; fire = true; }
        ui->animating = true;
        st->back.x = st->progress;
        st->back.v = 0;
    } else {
        if (!held) st->fired = false;
        cui__spring(ui, &st->back, 0, 0.35f, 0.0f);
        st->progress = st->back.x;
    }
    cui_button_opts bo;
    memset(&bo, 0, sizeof bo);
    bo.variant = o->variant ? o->variant : CUI_BUTTON_DANGER;
    bo.icon = o->icon;
    bo.tooltip = "Press and hold";
    cui_button_ex(ui, label, &bo);
    int bnode = -1;
    for (int k = ui->node_count - 1; k >= 0; k--) if (ui->nodes[k].id == id) { bnode = k; break; }
    if (bnode >= 0) {
        cui_node *bn = &ui->nodes[bnode];
        bn->flags |= CUI_NF_CLIP;
        cui__hold_paint *p = cui__frame_alloc(ui, sizeof *p);
        p->t = st->progress;
        p->c = cui_alpha(ui->mode == CUI_THEME_DARK ? cui__rgba(0, 0, 0, 255) : cui__rgba(0, 0, 0, 255), 0.22f);
        bn->paint = cui__paint_hold;
        bn->paint_data = p;
        bn->flags &= ~(uint32_t)CUI_NF_PAINT_AFTER;
    }
    return fire;
}

/* ------------------------------------------------------------------------ */
/* Password strength                                                        */
/* ------------------------------------------------------------------------ */

int cui_password_strength(cui_ctx *ui, const char *pw) {
    int len = pw ? (int)strlen(pw) : 0;
    bool lower = false, upper = false, digit = false, symbol = false;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)pw[i];
        if (c >= 'a' && c <= 'z') lower = true;
        else if (c >= 'A' && c <= 'Z') upper = true;
        else if (c >= '0' && c <= '9') digit = true;
        else symbol = true;
    }
    int score = 0;
    if (len >= 8) score++;
    if (len >= 12) score++;
    if (lower && upper) score++;
    if (digit) score++;
    if (symbol) score++;
    if (len < 8 && score > 1) score = 1;
    if (score > 4) score = 4;
    if (len == 0) score = 0;
    static const char *names[5] = { "Too short", "Weak", "Fair", "Good", "Strong" };
    cui_color colors[5] = { CUI_C(ui, TEXT_MUTED), CUI_C(ui, DANGER), CUI_C(ui, WARNING), CUI_C(ui, SUCCESS), CUI_C(ui, SUCCESS) };   /* status colors only; the bar count tells Good from Strong */
    cui_id id = cui_make_id(ui, "#strength");
    id = cui__hash(&ui->node_count, sizeof ui->node_count, id);
    int row = cui__node_new(ui, 0);
    ui->nodes[row].dir = CUI_ROW; ui->nodes[row].gap = 4; ui->nodes[row].align = CUI_ALIGN_CENTER;
    cui__open(ui, row);
    for (int k = 0; k < 4; k++) {
        float target = (len > 0 && k < score) ? 1.0f : 0.0f;
        float t = cui_animate(ui, id + (cui_id)k, target);
        int seg = cui__leaf(ui, 0, CUI_FILL, CUI_PX(4));
        ui->nodes[seg].radius = CUI_CORNERS(2);
        ui->nodes[seg].bg = cui_mix(CUI_C(ui, CONTROL_TRACK), colors[score], t);
    }
    cui_gap(ui, 6);
    int t = cui__text_node(ui, len ? names[score] : "", -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_sm), colors[score], false);
    ui->nodes[t].line_height = 1.3f;
    ui->nodes[t].min_w = 60;
    ui->nodes[t].text_align = CUI_TEXT_RIGHT;
    cui__close(ui);
    return score;
}

/* ------------------------------------------------------------------------ */
/* Timeline                                                                 */
/* ------------------------------------------------------------------------ */

void cui_timeline_item_ex(cui_ctx *ui, const char *title, const cui_timeline_opts *o) {
    cui_timeline_opts d;
    memset(&d, 0, sizeof d);
    if (!o) o = &d;
    cui_color tone = o->tone ? cui__tone_color(ui, o->tone) : CUI_C(ui, TEXT_SECONDARY);
    int row = cui__node_new(ui, 0);
    cui_node *rn = &ui->nodes[row];
    rn->dir = CUI_ROW; rn->gap = 14; rn->align = CUI_ALIGN_STRETCH;
    cui__open(ui, row);
    int rail = cui__node_new(ui, 0);
    cui_node *ra = &ui->nodes[rail];
    ra->w = CUI_PX(28); ra->align = CUI_ALIGN_CENTER;
    /* the connector stops short of both dots so each event reads as its own mark */
    ra->gap = 4; ra->pad[2] = o->last ? 0 : 4;
    cui__open(ui, rail);
    int dot = cui__node_new(ui, 0);
    cui_node *dn = &ui->nodes[dot];
    dn->w = CUI_PX(28); dn->h = CUI_PX(28); dn->radius = CUI_CORNERS(14);
    dn->bg = o->tone ? cui_mix(CUI_C(ui, SURFACE), tone, ui->mode == CUI_THEME_DARK ? 0.22f : 0.12f) : CUI_C(ui, SURFACE_MUTED);
    dn->border = 1; dn->border_color = o->tone ? cui_mix(CUI_C(ui, BORDER), tone, 0.25f) : CUI_C(ui, BORDER);
    dn->dir = CUI_STACK; dn->align = CUI_ALIGN_CENTER; dn->justify = CUI_JUSTIFY_CENTER;
    cui__open(ui, dot);
    cui__icon_node(ui, o->icon ? o->icon : CUI_ICON_DOT, 14, tone);
    cui__close(ui);
    if (!o->last) {
        int line = cui__leaf(ui, 0, CUI_PX(1), CUI_FILL);
        ui->nodes[line].bg = ui->mode == CUI_THEME_DARK ? cui_mix(CUI_C(ui, BORDER), CUI_C(ui, BORDER_STRONG), 0.45f) : CUI_C(ui, BORDER);
        ui->nodes[line].min_h = 12;
        ui->nodes[line].self_align = CUI_ALIGN_CENTER;
    }
    cui__close(ui);
    int col = cui__node_new(ui, 0);
    ui->nodes[col].gap = 3; ui->nodes[col].w = CUI_FILL;
    ui->nodes[col].pad[0] = 4; ui->nodes[col].pad[2] = o->last ? 0 : 18;
    cui__open(ui, col);
    int head = cui__node_new(ui, 0);
    ui->nodes[head].dir = CUI_ROW; ui->nodes[head].gap = 8;
    cui__open(ui, head);
    int t = cui__text_node(ui, title, -1, CUI_FONT_MEDIUM, cui__fs(ui, ui->theme.text_body), CUI_C(ui, FOREGROUND), true);
    ui->nodes[t].w = CUI_FILL;
    ui->nodes[t].line_height = 1.4f;
    if (o->time) {
        int tm = cui__text_node(ui, o->time, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_sm), CUI_C(ui, TEXT_MUTED), false);
        ui->nodes[tm].line_height = 1.6f;
    }
    cui__close(ui);
    if (o->description) cui__text_node(ui, o->description, -1, CUI_FONT_BODY, cui__fs(ui, ui->theme.text_body), CUI_C(ui, TEXT_SECONDARY), true);
    cui__close(ui);
    cui__close(ui);
}

/* ------------------------------------------------------------------------ */
/* Progress ring                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { float value; const char *label; } cui__ring_paint;

static void cui__paint_ring(cui_ctx *ui, cui_node *n, void *data) {
    cui__ring_paint *p = data;
    cui_rect r = n->rect;
    float size = CUI_MIN(r.w, r.h);
    cui_vec2 c = { r.x + r.w * 0.5f, r.y + r.h * 0.5f };
    float thick = CUI_MAX(4.0f, size * 0.08f), rad = size * 0.5f - thick * 0.5f - 1;
    cui_draw_arc(ui, c, rad, 0, 2 * CUI_PI, thick, CUI_C(ui, CONTROL_TRACK));
    float v = cui__clampf(p->value, 0, 1);
    float a0 = -CUI_PI * 0.5f, a1 = a0 + 2 * CUI_PI * v;
    cui_color col = CUI_C(ui, CONTROL_ON);
    if (v > 0.001f) {
        cui_draw_arc(ui, c, rad, a0, a1, thick, col);
        cui_vec2 e0 = { c.x + cosf(a0) * rad, c.y + sinf(a0) * rad }, e1 = { c.x + cosf(a1) * rad, c.y + sinf(a1) * rad };
        cui_draw_circle(ui, e0, thick * 0.5f, col);
        cui_draw_circle(ui, e1, thick * 0.5f, col);
    }
    const char *vs = cui__frame_printf(ui, "%.0f%%", (double)(v * 100));
    float fs = size * 0.22f;
    float w = cui__text_width(ui, CUI_FONT_DISPLAY, fs, vs, -1, true), h = cui__font_line_height(ui, CUI_FONT_DISPLAY, fs);
    float yo = p->label ? -h * 0.62f : -h * 0.5f;
    cui__draw_text_line(ui, CUI_FONT_DISPLAY, fs, c.x - w * 0.5f, c.y + yo, CUI_C(ui, FOREGROUND), vs, -1, true);
    if (p->label) {
        float ls = CUI_MAX(10.0f, size * 0.09f);
        float lw = cui__text_width(ui, CUI_FONT_BODY, ls, p->label, -1, false);
        cui__draw_text_line(ui, CUI_FONT_BODY, ls, c.x - lw * 0.5f, c.y + h * 0.38f, CUI_C(ui, TEXT_SECONDARY), p->label, -1, false);
    }
}

void cui_progress_ring(cui_ctx *ui, const char *label, float value, float size) {
    if (size <= 0) size = 96;
    cui_id id = cui_make_id(ui, label ? label : "#ring");
    float shown = cui_animate(ui, id, cui__clampf(value, 0, 1));
    int i = cui__leaf(ui, 0, CUI_PX(size), CUI_PX(size));
    ui->nodes[i].self_align = CUI__ALIGN_NOSTRETCH;
    cui__ring_paint *p = cui__frame_alloc(ui, sizeof *p);
    p->value = shown;
    /* "##key" keeps the id but shows no caption, like other components */
    p->label = label && cui__display_len(label) > 0 ? cui__frame_strdup(ui, label, cui__display_len(label)) : NULL;
    ui->nodes[i].paint = cui__paint_ring;
    ui->nodes[i].paint_data = p;
}

/* ------------------------------------------------------------------------ */
/* Reorderable list                                                         */
/* ------------------------------------------------------------------------ */

#define CUI__REORDER_MAX 256

typedef struct {
    int from;               /* dragged position, -1 when idle */
    int to;                 /* drop position */
    float grab;             /* mouse offset inside the dragged row */
    float press_y;
    bool pending;           /* pressed, waiting to cross the drag threshold */
    int press_pos;
    cui_spring lift;        /* 0..1 dragged look */
    bool init;
} cui__reorder_state;

typedef struct { cui_spring y; } cui__reorder_item_state;

struct cui_reorder_build {
    cui_id id;
    int *order;
    int count;
    cui__reorder_state *st;
    float slot_top[CUI__REORDER_MAX], slot_h[CUI__REORDER_MAX];
    bool have_rects;
    bool changed;
    int item_node;
    struct cui_reorder_build *prev;
};

static cui_id cui__reorder_item_id(struct cui_reorder_build *b, int item) { return cui__hash(&item, sizeof item, b->id ^ 0x5E0Du); }

void cui_reorder_begin(cui_ctx *ui, const char *id_str, int *order, int count) {
    struct cui_reorder_build *b = cui__frame_alloc(ui, sizeof *b);
    b->id = cui_make_id(ui, id_str);
    b->order = order;
    b->count = count < CUI__REORDER_MAX ? count : CUI__REORDER_MAX;
    b->st = cui_state(ui, b->id, sizeof(cui__reorder_state));
    b->prev = ui->reorder;
    ui->reorder = b;
    cui__reorder_state *st = b->st;
    if (!st->init) { st->from = -1; st->init = true; }   /* state memory starts zeroed */
    if (st->from >= b->count) st->from = -1;
    /* slot geometry from last frame */
    b->have_rects = true;
    for (int k = 0; k < b->count; k++) {
        cui_id iid = cui__reorder_item_id(b, order[k]);
        if (!cui__has_prev_rect(ui, iid)) { b->have_rects = false; break; }
        cui_rect r = cui__prev_rect(ui, iid);
        b->slot_top[k] = r.y;
        b->slot_h[k] = r.h;
    }
    /* start a drag once the pointer moves a few points */
    if (st->pending && ui->mdown[0] && fabsf(ui->my - st->press_y) > 4 && b->have_rects) {
        st->pending = false;
        st->from = st->to = st->press_pos;
        st->grab = st->press_y - b->slot_top[st->from];
    }
    if (!ui->mdown[0]) st->pending = false;
    if (st->from >= 0 && b->have_rects) {
        /* drop position: moving down, the last row whose middle we passed;
         * moving up, the first row whose middle we are above */
        float mid = ui->my - st->grab + b->slot_h[st->from] * 0.5f;
        int to = st->from;
        for (int k = st->from + 1; k < b->count; k++) if (mid > b->slot_top[k] + b->slot_h[k] * 0.5f) to = k;
        for (int k = st->from - 1; k >= 0; k--) if (mid < b->slot_top[k] + b->slot_h[k] * 0.5f) to = k;
        st->to = to;
        if (!ui->mdown[0]) {
            /* commit: move the item, then let every row glide from where it was drawn */
            int from = st->from;
            if (to != from) {
                int item = order[from];
                if (to > from) memmove(&order[from], &order[from + 1], sizeof(int) * (size_t)(to - from));
                else memmove(&order[to + 1], &order[to], sizeof(int) * (size_t)(from - to));
                order[to] = item;
                b->changed = true;
                /* rows that made room are already drawn at their new slots */
                for (int k = 0; k < b->count; k++) {
                    cui__reorder_item_state *is = cui_state(ui, cui__reorder_item_id(b, order[k]) ^ 0x1u, sizeof *is);
                    is->y.x = 0; is->y.v = 0;
                }
                /* the dropped row starts where the pointer left it */
                cui__reorder_item_state *ds = cui_state(ui, cui__reorder_item_id(b, item) ^ 0x1u, sizeof *ds);
                ds->y.x = (ui->my - st->grab) - b->slot_top[to];
            } else {
                cui__reorder_item_state *ds = cui_state(ui, cui__reorder_item_id(b, order[from]) ^ 0x1u, sizeof *ds);
                ds->y.x = (ui->my - st->grab) - b->slot_top[from];
            }
            st->from = -1;
        }
    }
    if (st->from >= 0 && !ui->mdown[0]) st->from = -1;   /* released without layout: just end the drag */
    cui__spring(ui, &st->lift, st->from >= 0 ? 1.0f : 0.0f, CUI_SPRING_SNAPPY);
    int col = cui__node_new(ui, b->id);
    ui->nodes[col].gap = 6;
    ui->nodes[col].w = CUI_FILL;
    cui__open(ui, col);
    if (st->from >= 0) ui->active = b->id;   /* keep the pointer captured while dragging */
}

void cui_reorder_item_begin(cui_ctx *ui, int pos) {
    struct cui_reorder_build *b = ui->reorder;
    if (!b || pos < 0 || pos >= b->count) {
        /* outside a list or past its capacity: a plain row, kept balanced with item_end */
        int n = cui__node_new(ui, 0);
        ui->nodes[n].dir = CUI_ROW;
        cui__open(ui, n);
        cui_push_id_int(ui, pos);
        return;
    }
    cui__reorder_state *st = b->st;
    int item = b->order[pos];
    cui_id iid = cui__reorder_item_id(b, item);
    cui_interact r = cui__interact(ui, iid, 0);
    if (r.hovered && ui->mpressed[0]) { st->pending = true; st->press_y = ui->my; st->press_pos = pos; }
    cui__reorder_item_state *is = cui_state(ui, iid ^ 0x1u, sizeof *is);
    bool dragged = st->from == pos;
    float target = 0;
    if (st->from >= 0 && !dragged && b->have_rects) {
        float shift = b->slot_h[st->from] + (b->count > 1 ? b->slot_top[1] - (b->slot_top[0] + b->slot_h[0]) : 0);
        if (st->from < st->to && pos > st->from && pos <= st->to) target = -shift;
        if (st->from > st->to && pos >= st->to && pos < st->from) target = shift;
    }
    if (dragged && b->have_rects) { is->y.x = ui->my - st->grab - b->slot_top[pos]; is->y.v = 0; ui->animating = true; }
    else cui__spring(ui, &is->y, target, CUI_SPRING_SMOOTH);
    int n = cui__node_new(ui, iid);
    cui_node *nd = &ui->nodes[n];
    nd->dir = CUI_ROW; nd->align = CUI_ALIGN_CENTER; nd->gap = 10;
    nd->pad[0] = nd->pad[2] = 10; nd->pad[1] = 14; nd->pad[3] = 8;
    nd->radius = CUI_CORNERS(ui->theme.radius_control - 4);
    nd->bg = CUI_C(ui, SURFACE);
    nd->border = 1;
    nd->border_color = CUI_C(ui, BORDER);
    nd->ty = is->y.x;
    if (dragged) {
        nd->elevation = CUI_ELEVATION_FLOATING;
        nd->elevation_alpha = st->lift.x;
        nd->scale = 1.0f + 0.015f * st->lift.x;
        nd->flags |= CUI_NF_ON_TOP;   /* painted after its siblings */
    }
    cui__mark_interactive(ui, n, 0);
    if (r.hovered || dragged) cui__set_cursor(ui, dragged ? CUI_CURSOR_MOVE : CUI_CURSOR_HAND);
    b->item_node = n;
    cui__open(ui, n);
    cui__icon_node(ui, CUI_ICON_GRIP_VERTICAL, 16, CUI_C(ui, TEXT_MUTED));
    cui_push_id_int(ui, item);
}

void cui_reorder_item_end(cui_ctx *ui) {
    cui_pop_id(ui);
    cui__close(ui);
}

bool cui_reorder_end(cui_ctx *ui) {
    struct cui_reorder_build *b = ui->reorder;
    if (!b) return false;
    cui__close(ui);
    ui->reorder = b->prev;
    return b->changed;
}
