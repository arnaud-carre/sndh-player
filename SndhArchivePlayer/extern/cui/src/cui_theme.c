/* Design tokens, light/dark themes, accents and OKLab color math.
 *
 * The values follow a calm, mostly neutral palette: one accent spent with
 * intent, status colors only for status, hairline borders and soft layered
 * shadows on floating layers only. */
#include "cui_internal.h"

/* ---- color math ---- */

static float cui__srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static float cui__linear_to_srgb(float c) {
    c = cui__clampf(c, 0, 1);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

typedef struct { float L, a, b; } cui__lab;

static cui__lab cui__to_oklab(cui_color c) {
    float r = cui__srgb_to_linear(c.r / 255.0f), g = cui__srgb_to_linear(c.g / 255.0f), b = cui__srgb_to_linear(c.b / 255.0f);
    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;
    l = cbrtf(l); m = cbrtf(m); s = cbrtf(s);
    cui__lab o = {
        0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
        1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
        0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s,
    };
    return o;
}

static cui_color cui__from_oklab(cui__lab o, float alpha) {
    float l = o.L + 0.3963377774f * o.a + 0.2158037573f * o.b;
    float m = o.L - 0.1055613458f * o.a - 0.0638541728f * o.b;
    float s = o.L - 0.0894841775f * o.a - 1.2914855480f * o.b;
    l = l * l * l; m = m * m * m; s = s * s * s;
    float r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    cui_color c = {
        (uint8_t)(cui__linear_to_srgb(r) * 255.0f + 0.5f),
        (uint8_t)(cui__linear_to_srgb(g) * 255.0f + 0.5f),
        (uint8_t)(cui__linear_to_srgb(b) * 255.0f + 0.5f),
        (uint8_t)(cui__clampf(alpha, 0, 1) * 255.0f + 0.5f),
    };
    return c;
}

cui_color cui_oklch(float l, float c, float h, float alpha) {
    float hr = h * CUI_PI / 180.0f;
    cui__lab o = { l, c * cosf(hr), c * sinf(hr) };
    return cui__from_oklab(o, alpha);
}

cui_color cui_mix(cui_color a, cui_color b, float t) {
    t = cui__clampf(t, 0, 1);
    if (t <= 0) return a;
    if (t >= 1) return b;
    float aa = a.a / 255.0f, ba = b.a / 255.0f;
    cui__lab la = cui__to_oklab(a), lb = cui__to_oklab(b);
    float alpha = aa + (ba - aa) * t;
    cui__lab o;
    if (alpha <= 0.0001f) return cui__rgba(0, 0, 0, 0);
    /* premultiplied interpolation, like CSS color-mix */
    o.L = (la.L * aa * (1 - t) + lb.L * ba * t) / alpha;
    o.a = (la.a * aa * (1 - t) + lb.a * ba * t) / alpha;
    o.b = (la.b * aa * (1 - t) + lb.b * ba * t) / alpha;
    return cui__from_oklab(o, alpha);
}

static float cui__rel_lum(cui_color c) {
    return 0.2126f * cui__srgb_to_linear(c.r / 255.0f) + 0.7152f * cui__srgb_to_linear(c.g / 255.0f) + 0.0722f * cui__srgb_to_linear(c.b / 255.0f);
}

/* fg, stepped in OKLab lightness away from bg until it reaches the WCAG ratio: tinted text keeps its hue and stays readable */
cui_color cui__legible(cui_color fg, cui_color bg, float ratio) {
    float lb = cui__rel_lum(bg);
    bool darken = lb > 0.18f;
    cui__lab o = cui__to_oklab(fg);
    for (int k = 0; k < 24; k++) {
        float lf = cui__rel_lum(fg);
        float cr = (CUI_MAX(lf, lb) + 0.05f) / (CUI_MIN(lf, lb) + 0.05f);
        if (cr >= ratio) break;
        o.L = cui__clampf(o.L + (darken ? -0.025f : 0.025f), 0, 1);
        o.a *= 0.97f; o.b *= 0.97f;
        fg = cui__from_oklab(o, fg.a / 255.0f);
    }
    return fg;
}

/* c at OKLab lightness L with its chroma scaled: clean deep or pale steps of one hue */
cui_color cui__shade(cui_color c, float L, float chroma_mul) {
    cui__lab o = cui__to_oklab(c);
    o.L = L; o.a *= chroma_mul; o.b *= chroma_mul;
    return cui__from_oklab(o, c.a / 255.0f);
}

cui_color cui_alpha(cui_color c, float m) {
    c.a = (uint8_t)cui__clampf(c.a * m + 0.5f, 0, 255);
    return c;
}

static cui_color cui__hexa(uint32_t rgb, uint8_t a) { cui_color c = { (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a }; return c; }
static cui_color cui__gray(float l) { return cui_oklch(l, 0, 0, 1); }

/* ---- accents ---- */

typedef struct {
    const char *name;
    uint32_t accent, strong; uint8_t subtle_a; uint32_t fg;        /* light */
    uint32_t d_accent, d_strong; uint8_t d_subtle_a; uint32_t d_fg; /* dark */
    uint32_t control_on, control_glyph, d_control_on, d_control_glyph;
} cui__accent_def;

static const cui__accent_def cui__accents[CUI_ACCENT_COUNT] = {
    { "neutral", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "violet", 0x7747ff, 0x5528ce, 0x21, 0xffffff, 0xa78bff, 0xc7b7ff, 0x38, 0x15102b, 0x7747ff, 0xffffff, 0x8a66ff, 0xffffff },
    { "blue",   0x0562ef, 0x074db7, 0x1f, 0xffffff, 0x6ba3ff, 0xa4c7ff, 0x38, 0x0b1526, 0x0562ef, 0xffffff, 0x3b82f6, 0xffffff },
    { "green",  0x00864b, 0x006c36, 0x21, 0xffffff, 0x53dca6, 0x93efd0, 0x33, 0x102016, 0x00864b, 0xffffff, 0x1fae78, 0xffffff },
    { "amber",  0xf3ad20, 0x9c6300, 0x29, 0x20190d, 0xffcb62, 0xffe0a1, 0x33, 0x20190d, 0xeda11a, 0x2a1c03, 0xe8a52a, 0x241803 },
    { "orange", 0xc25300, 0xa43d00, 0x21, 0xffffff, 0xffad5e, 0xffd0a0, 0x33, 0x17130f, 0xc25300, 0xffffff, 0xef873a, 0xffffff },
    { "coral",  0xd1403a, 0xb12926, 0x24, 0xffffff, 0xff9386, 0xffc1ba, 0x33, 0x21110f, 0xd1403a, 0xffffff, 0xec6a60, 0xffffff },
    { "rose",   0xd23286, 0xb2186e, 0x24, 0xffffff, 0xff8bc5, 0xffc0df, 0x33, 0x250f1b, 0xd23286, 0xffffff, 0xe457a0, 0xffffff },
};

const char *cui_accent_name(cui_accent a) { return (a >= 0 && a < CUI_ACCENT_COUNT) ? cui__accents[a].name : "neutral"; }

void cui__theme_build(cui_theme *t, cui_theme_mode mode, cui_accent accent) {
    memset(t, 0, sizeof *t);
    bool dark = mode == CUI_THEME_DARK;
    cui_color *c = t->colors;
    if ((int)accent < 0 || accent >= CUI_ACCENT_COUNT) accent = CUI_ACCENT_NEUTRAL;

    if (!dark) {
        c[CUI_TOKEN_BACKGROUND] = cui__gray(1.0f);
        c[CUI_TOKEN_SURFACE] = cui__gray(1.0f);
        c[CUI_TOKEN_SURFACE_RAISED] = cui__gray(1.0f);
        c[CUI_TOKEN_SURFACE_MUTED] = cui__gray(0.978f);
        c[CUI_TOKEN_FOREGROUND] = cui__gray(0.15f);
        c[CUI_TOKEN_TEXT_SECONDARY] = cui__gray(0.46f);
        c[CUI_TOKEN_TEXT_MUTED] = cui__gray(0.565f);
        c[CUI_TOKEN_BORDER] = cui__gray(0.915f);
        c[CUI_TOKEN_BORDER_SUBTLE] = cui__gray(0.955f);
        c[CUI_TOKEN_BORDER_STRONG] = cui__gray(0.665f);
        c[CUI_TOKEN_CONTROL_ON] = cui__gray(0.23f);
        c[CUI_TOKEN_CONTROL_GLYPH] = cui__gray(1.0f);
        c[CUI_TOKEN_CONTROL_TRACK] = cui__gray(0.895f);
        c[CUI_TOKEN_CONTROL_TRACK_HOVER] = cui__gray(0.86f);
        c[CUI_TOKEN_CONTROL_THUMB] = cui__gray(1.0f);
        c[CUI_TOKEN_CONTROL_THUMB_ON] = cui__gray(1.0f);
        t->tone_teal = cui_oklch(0.50f, 0.095f, 182, 1);
        t->tone_sky = cui_oklch(0.51f, 0.11f, 240, 1);
        t->tone_orchid = cui_oklch(0.52f, 0.17f, 322, 1);
        t->tone_violet = cui_oklch(0.51f, 0.16f, 290, 1);
        t->tone_rose = cui_oklch(0.53f, 0.18f, 10, 1);
        t->tone_amber = cui_oklch(0.55f, 0.12f, 68, 1);
        t->tone_green = cui_oklch(0.50f, 0.12f, 162, 1);
        c[CUI_TOKEN_SCRIM] = cui__rgba(10, 10, 12, 82);
    } else {
        c[CUI_TOKEN_BACKGROUND] = cui__gray(0.18f);
        c[CUI_TOKEN_SURFACE] = cui__gray(0.21f);
        c[CUI_TOKEN_SURFACE_RAISED] = cui__gray(0.24f);
        c[CUI_TOKEN_SURFACE_MUTED] = cui__gray(0.26f);
        c[CUI_TOKEN_FOREGROUND] = cui__gray(0.96f);
        c[CUI_TOKEN_TEXT_SECONDARY] = cui__gray(0.77f);
        c[CUI_TOKEN_TEXT_MUTED] = cui__gray(0.64f);
        c[CUI_TOKEN_BORDER] = cui__gray(0.29f);
        c[CUI_TOKEN_BORDER_SUBTLE] = cui__gray(0.25f);
        c[CUI_TOKEN_BORDER_STRONG] = cui__gray(0.515f);
        c[CUI_TOKEN_CONTROL_ON] = cui__gray(0.93f);
        c[CUI_TOKEN_CONTROL_GLYPH] = cui__gray(0.19f);
        c[CUI_TOKEN_CONTROL_TRACK] = cui__gray(0.33f);
        c[CUI_TOKEN_CONTROL_TRACK_HOVER] = cui__gray(0.37f);
        c[CUI_TOKEN_CONTROL_THUMB] = cui__gray(0.97f);
        c[CUI_TOKEN_CONTROL_THUMB_ON] = cui__gray(0.19f);
        t->tone_teal = cui_oklch(0.79f, 0.13f, 182, 1);
        t->tone_sky = cui_oklch(0.79f, 0.095f, 236, 1);
        t->tone_orchid = cui_oklch(0.76f, 0.15f, 322, 1);
        t->tone_violet = cui_oklch(0.74f, 0.125f, 290, 1);
        t->tone_rose = cui_oklch(0.71f, 0.155f, 8, 1);
        t->tone_amber = cui_oklch(0.83f, 0.12f, 78, 1);
        t->tone_green = cui_oklch(0.79f, 0.14f, 162, 1);
        c[CUI_TOKEN_SCRIM] = cui__rgba(0, 0, 0, 140);
    }
    c[CUI_TOKEN_SUCCESS] = t->tone_green;
    c[CUI_TOKEN_WARNING] = t->tone_amber;
    /* danger is its own red, not the rose label tone: it has to read as a warning at a glance */
    c[CUI_TOKEN_DANGER] = dark ? cui_oklch(0.70f, 0.16f, 24, 1) : cui_oklch(0.55f, 0.19f, 27, 1);
    c[CUI_TOKEN_INFO] = t->tone_sky;
    c[CUI_TOKEN_HOVER] = cui_alpha(c[CUI_TOKEN_FOREGROUND], 0.05f);
    c[CUI_TOKEN_ACTIVE] = cui_alpha(c[CUI_TOKEN_FOREGROUND], 0.075f);

    const cui__accent_def *a = &cui__accents[accent];
    float series_l = dark ? 0.73f : 0.58f;
    float series_h = 250;
    if (accent == CUI_ACCENT_NEUTRAL) {
        c[CUI_TOKEN_ACCENT] = cui__gray(dark ? 0.80f : 0.33f);
        c[CUI_TOKEN_ACCENT_STRONG] = cui__gray(dark ? 0.91f : 0.24f);
        c[CUI_TOKEN_ACCENT_SUBTLE] = cui_oklch(dark ? 0.80f : 0.33f, 0, 0, dark ? 0.13f : 0.10f);
        c[CUI_TOKEN_ACCENT_FOREGROUND] = c[CUI_TOKEN_BACKGROUND];
    } else {
        c[CUI_TOKEN_ACCENT] = cui__hexa(dark ? a->d_accent : a->accent, 255);
        c[CUI_TOKEN_ACCENT_STRONG] = cui__hexa(dark ? a->d_strong : a->strong, 255);
        c[CUI_TOKEN_ACCENT_SUBTLE] = cui__hexa(a->accent, dark ? a->d_subtle_a : a->subtle_a);
        c[CUI_TOKEN_ACCENT_FOREGROUND] = cui__hexa(dark ? a->d_fg : a->fg, 255);
        c[CUI_TOKEN_CONTROL_ON] = cui__hexa(dark ? a->d_control_on : a->control_on, 255);
        c[CUI_TOKEN_CONTROL_GLYPH] = cui__hexa(dark ? a->d_control_glyph : a->control_glyph, 255);
        c[CUI_TOKEN_CONTROL_THUMB_ON] = cui__hexa(0xffffff, 255);
        /* series start at the accent hue */
        cui__lab lab = cui__to_oklab(c[CUI_TOKEN_ACCENT]);
        series_h = atan2f(lab.b, lab.a) * 180.0f / CUI_PI;
    }
    c[CUI_TOKEN_FOCUS_RING] = cui_mix(c[CUI_TOKEN_ACCENT], c[CUI_TOKEN_BACKGROUND], dark ? 0.35f : 0.45f);
    if (accent == CUI_ACCENT_NEUTRAL) c[CUI_TOKEN_FOCUS_RING] = cui_oklch(series_l, 0.15f, 250, 0.9f);

    /* Series palette: blue, orange, aqua, violet, yellow, magenta. Each mode
     * has its own steps, and the order keeps every neighbour (the last and
     * first too) apart under color blindness, so any rotation is safe. It is
     * rotated so the first series sits nearest the accent hue. */
    static const uint32_t sp[2][6] = { { 0x2a78d6, 0xeb6834, 0x1baf7a, 0x6250d6, 0xeda100, 0xe87ba4 },
                                       { 0x4892ed, 0xe8672e, 0x20ab7a, 0x8e7fe6, 0xd17f00, 0xdd6197 } };
    static const float sh[6] = { 252, 45, 165, 285, 80, 350 };
    int first = 0;
    if (accent != CUI_ACCENT_NEUTRAL) {
        float best = 1e9f;
        for (int i = 0; i < 6; i++) {
            float d = fabsf(fmodf(sh[i] - series_h + 540.0f, 360.0f) - 180.0f);
            if (d < best) { best = d; first = i; }
        }
    }
    for (int i = 0; i < 6; i++) {
        uint32_t v = sp[dark][(first + i) % 6];
        c[CUI_TOKEN_SERIES_1 + i] = cui__rgba((uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v, 255);
    }

    /* one scale, nested concentrically: a menu item (8) sits 6 inside its
     * panel (14); controls are soft rectangles, not pills */
    t->radius_control = 10;
    t->radius_panel = 14;
    t->radius_surface = 18;
    t->control_sm = 30;
    t->control_md = 36;
    t->control_lg = 44;
    t->text_xs = 11;
    t->text_sm = 12;
    t->text_body = 13;
    t->text_lg = 15;
    t->heading[0] = 36; t->heading[1] = 28; t->heading[2] = 22; t->heading[3] = 17;

    if (!dark) {
        t->shadow_resting[0] = (cui_shadow){ 1, 2, 0, 0.035f };
        t->shadow_raised[0] = (cui_shadow){ 6, 18, 0, 0.065f };
        t->shadow_raised[1] = (cui_shadow){ 1, 3, 0, 0.035f };
        /* close and crisp rather than a wide haze: a tight contact shadow and
         * a short drop pulled in at the sides */
        t->shadow_floating[0] = (cui_shadow){ 12, 32, -4, 0.12f };
        t->shadow_floating[1] = (cui_shadow){ 1, 3, 0, 0.06f };
    } else {
        t->shadow_resting[0] = (cui_shadow){ 1, 2, 0, 0.18f };
        t->shadow_raised[0] = (cui_shadow){ 8, 22, 0, 0.22f };
        t->shadow_floating[0] = (cui_shadow){ 14, 36, -4, 0.40f };
        t->shadow_floating[1] = (cui_shadow){ 1, 3, 0, 0.24f };
    }
    t->motion = 1;
    t->font_scale = 1;
}

void cui__theme_init(cui_ctx *ui, cui_theme_mode mode, cui_accent accent) {
    ui->mode = mode;
    ui->accent = accent;
    cui__theme_build(&ui->target_theme, mode, accent);
    ui->theme = ui->target_theme;
    ui->from_theme = ui->theme;
    ui->theme_t = 1;
}

static void cui__theme_retarget(cui_ctx *ui) {
    cui_theme keep = ui->theme;
    ui->from_theme = ui->theme;
    cui__theme_build(&ui->target_theme, ui->mode, ui->accent);
    /* keep user customizations of the non-color fields */
    memcpy(ui->target_theme.shadow_resting, keep.shadow_resting, sizeof keep.shadow_resting);
    ui->target_theme.radius_control = keep.radius_control;
    ui->target_theme.radius_panel = keep.radius_panel;
    ui->target_theme.radius_surface = keep.radius_surface;
    ui->target_theme.motion = keep.motion;
    ui->target_theme.font_scale = keep.font_scale;
    ui->theme_t = 0;
}

void cui_set_theme(cui_ctx *ui, cui_theme_mode mode) {
    if (mode == ui->mode) return;
    ui->mode = mode;
    cui__theme_retarget(ui);
}
cui_theme_mode cui_get_theme_mode(cui_ctx *ui) { return ui->mode; }

void cui_set_accent(cui_ctx *ui, cui_accent accent) {
    if (accent == ui->accent) return;
    ui->accent = accent;
    cui__theme_retarget(ui);
}
cui_accent cui_get_accent(cui_ctx *ui) { return ui->accent; }

cui_theme *cui_theme_get(cui_ctx *ui) { return &ui->theme; }

void cui_set_reduced_motion(cui_ctx *ui, bool reduced) {
    ui->reduced_motion = reduced;
    ui->theme.motion = reduced ? 0.0f : 1.0f;
    ui->target_theme.motion = ui->theme.motion;
}

void cui__theme_update(cui_ctx *ui) {
    if (ui->theme_t >= 1) return;
    float dur = 0.32f * (ui->theme.motion > 0 ? ui->theme.motion : 0);
    ui->theme_t = dur > 0 ? CUI_MIN(1.0f, ui->theme_t + ui->dt / dur) : 1.0f;
    float t = ui->theme_t;
    t = 1 - (1 - t) * (1 - t) * (1 - t);   /* ease out */
    for (int i = 0; i < CUI_TOKEN_COUNT; i++)
        ui->theme.colors[i] = cui_mix(ui->from_theme.colors[i], ui->target_theme.colors[i], t);
    cui_color *tf = &ui->from_theme.tone_sky, *tt = &ui->target_theme.tone_sky, *td = &ui->theme.tone_sky;
    for (int i = 0; i < 7; i++) td[i] = cui_mix(tf[i], tt[i], t);
    for (int i = 0; i < 2; i++) {
        ui->theme.shadow_resting[i] = ui->target_theme.shadow_resting[i];
        ui->theme.shadow_raised[i] = ui->target_theme.shadow_raised[i];
        ui->theme.shadow_floating[i] = ui->target_theme.shadow_floating[i];
    }
    if (ui->theme_t < 1) ui->animating = true;
}

cui_color cui_token_color(cui_ctx *ui, cui_token token) {
    if ((int)token <= 0 || token >= CUI_TOKEN_COUNT) return cui__rgba(0, 0, 0, 0);
    return ui->theme.colors[token];
}

cui_color cui_color_of(cui_ctx *ui, cui_color c) {
    if (cui__is_token(c)) return cui_token_color(ui, (cui_token)c.r);
    return c;
}

cui_color cui__tone_color(cui_ctx *ui, cui_tone tone) {
    cui_theme *t = &ui->theme;
    switch (tone) {
    case CUI_TONE_INFO: return t->tone_sky;
    case CUI_TONE_SUCCESS: return t->colors[CUI_TOKEN_SUCCESS];
    case CUI_TONE_WARNING: return t->colors[CUI_TOKEN_WARNING];
    case CUI_TONE_DANGER: return t->colors[CUI_TOKEN_DANGER];
    case CUI_TONE_VIOLET: return t->tone_violet;
    case CUI_TONE_TEAL: return t->tone_teal;
    case CUI_TONE_ROSE: return t->tone_orchid;
    case CUI_TONE_ACCENT: return t->colors[CUI_TOKEN_ACCENT];
    default: return t->colors[CUI_TOKEN_TEXT_SECONDARY];
    }
}
