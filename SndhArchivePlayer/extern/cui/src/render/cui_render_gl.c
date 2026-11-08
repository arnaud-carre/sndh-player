/* OpenGL 3.3 core / OpenGL ES 3.0 renderer.
 *
 * No GL headers or loader library are needed: the few functions used are
 * resolved through the loader passed to cui_gl_create. */
#include "../cui_internal.h"
#include "cui/cui_render.h"

#if defined(_WIN32)
#  define CUI_GLAPI __stdcall
#else
#  define CUI_GLAPI
#endif

typedef unsigned int GLenum_;
typedef unsigned int GLuint_;
typedef int GLint_;
typedef int GLsizei_;
typedef float GLfloat_;
typedef unsigned char GLboolean_;
typedef char GLchar_;
typedef ptrdiff_t GLsizeiptr_;
typedef ptrdiff_t GLintptr_;
typedef unsigned int GLbitfield_;

#define GL_TRIANGLES_ 0x0004
#define GL_UNSIGNED_INT_ 0x1405
#define GL_UNSIGNED_BYTE_ 0x1401
#define GL_FLOAT_ 0x1406
#define GL_BLEND_ 0x0BE2
#define GL_SCISSOR_TEST_ 0x0C11
#define GL_DEPTH_TEST_ 0x0B71
#define GL_CULL_FACE_ 0x0B44
#define GL_ONE_ 1
#define GL_ONE_MINUS_SRC_ALPHA_ 0x0303
#define GL_FUNC_ADD_ 0x8006
#define GL_TEXTURE_2D_ 0x0DE1
#define GL_TEXTURE0_ 0x84C0
#define GL_TEXTURE_MIN_FILTER_ 0x2801
#define GL_TEXTURE_MAG_FILTER_ 0x2800
#define GL_TEXTURE_WRAP_S_ 0x2802
#define GL_TEXTURE_WRAP_T_ 0x2803
#define GL_LINEAR_ 0x2601
#define GL_CLAMP_TO_EDGE_ 0x812F
#define GL_R8_ 0x8229
#define GL_RED_ 0x1903
#define GL_RGBA_ 0x1908
#define GL_RGBA8_ 0x8058
#define GL_UNPACK_ALIGNMENT_ 0x0CF5
#define GL_UNPACK_ROW_LENGTH_ 0x0CF2
#define GL_ARRAY_BUFFER_ 0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ 0x8893
#define GL_STREAM_DRAW_ 0x88E0
#define GL_FRAGMENT_SHADER_ 0x8B30
#define GL_VERTEX_SHADER_ 0x8B31
#define GL_COMPILE_STATUS_ 0x8B81
#define GL_LINK_STATUS_ 0x8B82
#define GL_COLOR_BUFFER_BIT_ 0x4000
#define GL_VERSION_ 0x1F02

typedef struct {
    void (CUI_GLAPI *Enable)(GLenum_);
    void (CUI_GLAPI *Disable)(GLenum_);
    void (CUI_GLAPI *BlendFunc)(GLenum_, GLenum_);
    void (CUI_GLAPI *BlendEquation)(GLenum_);
    void (CUI_GLAPI *Viewport)(GLint_, GLint_, GLsizei_, GLsizei_);
    void (CUI_GLAPI *Scissor)(GLint_, GLint_, GLsizei_, GLsizei_);
    void (CUI_GLAPI *ClearColor)(GLfloat_, GLfloat_, GLfloat_, GLfloat_);
    void (CUI_GLAPI *Clear)(GLbitfield_);
    void (CUI_GLAPI *GenTextures)(GLsizei_, GLuint_ *);
    void (CUI_GLAPI *DeleteTextures)(GLsizei_, const GLuint_ *);
    void (CUI_GLAPI *BindTexture)(GLenum_, GLuint_);
    void (CUI_GLAPI *TexParameteri)(GLenum_, GLenum_, GLint_);
    void (CUI_GLAPI *TexImage2D)(GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const void *);
    void (CUI_GLAPI *TexSubImage2D)(GLenum_, GLint_, GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, const void *);
    void (CUI_GLAPI *PixelStorei)(GLenum_, GLint_);
    void (CUI_GLAPI *ActiveTexture)(GLenum_);
    void (CUI_GLAPI *DrawElements)(GLenum_, GLsizei_, GLenum_, const void *);
    const unsigned char *(CUI_GLAPI *GetString)(GLenum_);
    void (CUI_GLAPI *GenBuffers)(GLsizei_, GLuint_ *);
    void (CUI_GLAPI *DeleteBuffers)(GLsizei_, const GLuint_ *);
    void (CUI_GLAPI *BindBuffer)(GLenum_, GLuint_);
    void (CUI_GLAPI *BufferData)(GLenum_, GLsizeiptr_, const void *, GLenum_);
    void (CUI_GLAPI *GenVertexArrays)(GLsizei_, GLuint_ *);
    void (CUI_GLAPI *DeleteVertexArrays)(GLsizei_, const GLuint_ *);
    void (CUI_GLAPI *BindVertexArray)(GLuint_);
    void (CUI_GLAPI *EnableVertexAttribArray)(GLuint_);
    void (CUI_GLAPI *VertexAttribPointer)(GLuint_, GLint_, GLenum_, GLboolean_, GLsizei_, const void *);
    GLuint_ (CUI_GLAPI *CreateShader)(GLenum_);
    void (CUI_GLAPI *ShaderSource)(GLuint_, GLsizei_, const GLchar_ *const *, const GLint_ *);
    void (CUI_GLAPI *CompileShader)(GLuint_);
    void (CUI_GLAPI *GetShaderiv)(GLuint_, GLenum_, GLint_ *);
    void (CUI_GLAPI *GetShaderInfoLog)(GLuint_, GLsizei_, GLsizei_ *, GLchar_ *);
    void (CUI_GLAPI *DeleteShader)(GLuint_);
    GLuint_ (CUI_GLAPI *CreateProgram)(void);
    void (CUI_GLAPI *AttachShader)(GLuint_, GLuint_);
    void (CUI_GLAPI *LinkProgram)(GLuint_);
    void (CUI_GLAPI *GetProgramiv)(GLuint_, GLenum_, GLint_ *);
    void (CUI_GLAPI *GetProgramInfoLog)(GLuint_, GLsizei_, GLsizei_ *, GLchar_ *);
    void (CUI_GLAPI *DeleteProgram)(GLuint_);
    void (CUI_GLAPI *UseProgram)(GLuint_);
    GLint_ (CUI_GLAPI *GetUniformLocation)(GLuint_, const GLchar_ *);
    void (CUI_GLAPI *Uniform1i)(GLint_, GLint_);
    void (CUI_GLAPI *Uniform2f)(GLint_, GLfloat_, GLfloat_);
    void (CUI_GLAPI *BindAttribLocation)(GLuint_, GLuint_, const GLchar_ *);
    void (CUI_GLAPI *ReadPixels)(GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, void *);
    void (CUI_GLAPI *Finish)(void);
} cui__gl_fns;

struct cui_gl_renderer {
    cui__gl_fns gl;
    GLuint_ program, vao, vbo, ebo;
    GLint_ u_viewport, u_tex;
    bool es;
    bool software;   /* the driver draws on the CPU (llvmpipe, SwiftShader, ...) */
    struct { GLuint_ id; cui_texture_format fmt; int w, h; } *tex;
    int tex_count, tex_cap;
};

static const char *cui__vs_src =
    "in vec2 a_pos;\n"
    "in vec2 a_uv;\n"
    "in vec2 a_local;\n"
    "in vec2 a_half;\n"
    "in vec4 a_radii;\n"
    "in vec2 a_params;\n"
    "in vec4 a_color;\n"
    "in vec4 a_bcolor;\n"
    "uniform vec2 u_viewport;\n"
    "out vec2 v_uv;\n"
    "out vec2 v_local;\n"
    "flat out vec2 v_half;\n"
    "flat out vec4 v_radii;\n"
    "flat out vec2 v_params;\n"
    "out vec4 v_color;\n"
    "flat out vec4 v_bcolor;\n"
    "void main() {\n"
    "  v_uv = a_uv; v_local = a_local; v_half = a_half; v_radii = a_radii;\n"
    "  v_params = a_params; v_color = a_color; v_bcolor = a_bcolor;\n"
    "  gl_Position = vec4(a_pos.x / u_viewport.x * 2.0 - 1.0, 1.0 - a_pos.y / u_viewport.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char *cui__fs_src =
    "in vec2 v_uv;\n"
    "in vec2 v_local;\n"
    "flat in vec2 v_half;\n"
    "flat in vec4 v_radii;\n"
    "flat in vec2 v_params;\n"
    "in vec4 v_color;\n"
    "flat in vec4 v_bcolor;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "float sd_rrect(vec2 p, vec2 b, vec4 r) {\n"
    "  float rr = p.x < 0.0 ? (p.y < 0.0 ? r.x : r.w) : (p.y < 0.0 ? r.y : r.z);\n"
    "  rr = min(rr, min(b.x, b.y));\n"
    "  vec2 q = abs(p) - b + rr;\n"
    "  return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rr;\n"
    "}\n"
    "float erf_(float x) {\n"
    "  float s = sign(x); x = abs(x);\n"
    "  float t = 1.0 / (1.0 + 0.3275911 * x);\n"
    "  float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-x * x);\n"
    "  return s * y;\n"
    "}\n"
    "void main() {\n"
    "  int mode = int(v_params.y + 0.5);\n"
    "  vec4 c = v_color;\n"
    "  if (mode == 4) { frag = vec4(c.rgb * c.a, c.a); return; }\n"
    "  if (mode == 0) { float a = c.a * texture(u_tex, v_uv).r; frag = vec4(c.rgb * a, a); return; }\n"
    "  float d = sd_rrect(v_local, v_half, v_radii);\n"
    "  if (mode == 1) {\n"
    "    float outer = clamp(0.5 - d, 0.0, 1.0);\n"
    "    float bw = v_params.x;\n"
    "    float inner = bw > 0.0 ? clamp(0.5 - (d + bw), 0.0, 1.0) : outer;\n"
    "    float fa = c.a * inner, ba = v_bcolor.a * (outer - inner);\n"
    "    frag = vec4(c.rgb * fa + v_bcolor.rgb * ba, fa + ba);\n"
    "  } else if (mode == 2) {\n"
    "    float s = max(v_params.x, 0.01);\n"
    "    float a = c.a * 0.5 * (1.0 - erf_(d / (s * 1.41421356)));\n"
    "    frag = vec4(c.rgb * a, a);\n"
    "  } else {\n"
    "    float cov = clamp(0.5 - d, 0.0, 1.0);\n"
    "    vec4 t = texture(u_tex, v_uv);\n"
    "    float k = c.a * cov;\n"
    "    frag = vec4(t.rgb * t.a * c.rgb * k, t.a * k);\n"
    "  }\n"
    "}\n";

static GLuint_ cui__gl_shader(cui_gl_renderer *r, GLenum_ type, const char *src) {
    const char *prefix = r->es ? "#version 300 es\nprecision highp float;\nprecision highp int;\n" : "#version 330 core\n";
    const char *srcs[2] = { prefix, src };
    GLuint_ s = r->gl.CreateShader(type);
    r->gl.ShaderSource(s, 2, srcs, NULL);
    r->gl.CompileShader(s);
    GLint_ ok = 0;
    r->gl.GetShaderiv(s, GL_COMPILE_STATUS_, &ok);
    if (!ok) {
        char log[1024];
        r->gl.GetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "cui: shader compile failed: %s\n", log);
        r->gl.DeleteShader(s);
        return 0;
    }
    return s;
}

#define CUI__LOAD(name) do { *(void **)(&r->gl.name) = loader("gl" #name); if (!r->gl.name) { missing = "gl" #name; } } while (0)

cui_gl_renderer *cui_gl_create(cui_gl_loader loader) {
    if (!loader) return NULL;
    cui_gl_renderer *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    const char *missing = NULL;
    CUI__LOAD(Enable); CUI__LOAD(Disable); CUI__LOAD(BlendFunc); CUI__LOAD(BlendEquation);
    CUI__LOAD(Viewport); CUI__LOAD(Scissor); CUI__LOAD(ClearColor); CUI__LOAD(Clear);
    CUI__LOAD(GenTextures); CUI__LOAD(DeleteTextures); CUI__LOAD(BindTexture); CUI__LOAD(TexParameteri);
    CUI__LOAD(TexImage2D); CUI__LOAD(TexSubImage2D); CUI__LOAD(PixelStorei); CUI__LOAD(ActiveTexture);
    CUI__LOAD(DrawElements); CUI__LOAD(GetString); CUI__LOAD(GenBuffers); CUI__LOAD(DeleteBuffers);
    CUI__LOAD(BindBuffer); CUI__LOAD(BufferData); CUI__LOAD(GenVertexArrays); CUI__LOAD(DeleteVertexArrays);
    CUI__LOAD(BindVertexArray); CUI__LOAD(EnableVertexAttribArray); CUI__LOAD(VertexAttribPointer);
    CUI__LOAD(CreateShader); CUI__LOAD(ShaderSource); CUI__LOAD(CompileShader); CUI__LOAD(GetShaderiv);
    CUI__LOAD(GetShaderInfoLog); CUI__LOAD(DeleteShader); CUI__LOAD(CreateProgram); CUI__LOAD(AttachShader);
    CUI__LOAD(LinkProgram); CUI__LOAD(GetProgramiv); CUI__LOAD(GetProgramInfoLog); CUI__LOAD(DeleteProgram);
    CUI__LOAD(UseProgram); CUI__LOAD(GetUniformLocation); CUI__LOAD(Uniform1i); CUI__LOAD(Uniform2f);
    CUI__LOAD(BindAttribLocation); CUI__LOAD(ReadPixels); CUI__LOAD(Finish);
    if (missing) {
        fprintf(stderr, "cui: OpenGL function %s not available\n", missing);
        free(r);
        return NULL;
    }
    const char *ver = (const char *)r->gl.GetString(GL_VERSION_);
    const char *rend = (const char *)r->gl.GetString(0x1F01 /* GL_RENDERER */);
    if (rend) {
        static const char *const soft[] = { "llvmpipe", "softpipe", "swrast", "SwiftShader", "Software Rasterizer", "GDI Generic", "Apple Software Renderer", "Microsoft Basic Render" };
        for (size_t i = 0; i < sizeof soft / sizeof *soft; i++) if (strstr(rend, soft[i])) r->software = true;
    }
    r->es = ver && strstr(ver, "OpenGL ES") != NULL;
    if (ver && !r->es) {
        int major = 0, minor = 0;
        if (sscanf(ver, "%d.%d", &major, &minor) == 2 && (major < 3 || (major == 3 && minor < 3))) {
            fprintf(stderr, "cui: OpenGL 3.3 required, have %s\n", ver);
            free(r);
            return NULL;
        }
    }
    GLuint_ vs = cui__gl_shader(r, GL_VERTEX_SHADER_, cui__vs_src);
    GLuint_ fs = cui__gl_shader(r, GL_FRAGMENT_SHADER_, cui__fs_src);
    if (!vs || !fs) { free(r); return NULL; }
    r->program = r->gl.CreateProgram();
    r->gl.AttachShader(r->program, vs);
    r->gl.AttachShader(r->program, fs);
    static const char *attrs[8] = { "a_pos", "a_uv", "a_local", "a_half", "a_radii", "a_params", "a_color", "a_bcolor" };
    for (int i = 0; i < 8; i++) r->gl.BindAttribLocation(r->program, (GLuint_)i, attrs[i]);
    r->gl.LinkProgram(r->program);
    r->gl.DeleteShader(vs);
    r->gl.DeleteShader(fs);
    GLint_ ok = 0;
    r->gl.GetProgramiv(r->program, GL_LINK_STATUS_, &ok);
    if (!ok) {
        char log[1024];
        r->gl.GetProgramInfoLog(r->program, sizeof log, NULL, log);
        fprintf(stderr, "cui: program link failed: %s\n", log);
        r->gl.DeleteProgram(r->program);
        free(r);
        return NULL;
    }
    r->u_viewport = r->gl.GetUniformLocation(r->program, "u_viewport");
    r->u_tex = r->gl.GetUniformLocation(r->program, "u_tex");
    r->gl.GenVertexArrays(1, &r->vao);
    r->gl.GenBuffers(1, &r->vbo);
    r->gl.GenBuffers(1, &r->ebo);
    r->gl.BindVertexArray(r->vao);
    r->gl.BindBuffer(GL_ARRAY_BUFFER_, r->vbo);
    r->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_, r->ebo);
    GLsizei_ stride = (GLsizei_)sizeof(cui_vertex);
#define CUI__ATTR(i, n, type, norm, field) do { r->gl.EnableVertexAttribArray(i); \
        r->gl.VertexAttribPointer(i, n, type, norm, stride, (const void *)offsetof(cui_vertex, field)); } while (0)
    CUI__ATTR(0, 2, GL_FLOAT_, 0, x);
    CUI__ATTR(1, 2, GL_FLOAT_, 0, u);
    CUI__ATTR(2, 2, GL_FLOAT_, 0, lx);
    CUI__ATTR(3, 2, GL_FLOAT_, 0, hw);
    CUI__ATTR(4, 4, GL_FLOAT_, 0, r_tl);
    CUI__ATTR(5, 2, GL_FLOAT_, 0, border);
    CUI__ATTR(6, 4, GL_UNSIGNED_BYTE_, 1, color);
    CUI__ATTR(7, 4, GL_UNSIGNED_BYTE_, 1, border_color);
#undef CUI__ATTR
    r->gl.BindVertexArray(0);
    return r;
}

void cui_gl_destroy(cui_gl_renderer *r) {
    if (!r) return;
    for (int i = 0; i < r->tex_count; i++) if (r->tex[i].id) r->gl.DeleteTextures(1, &r->tex[i].id);
    free(r->tex);
    r->gl.DeleteBuffers(1, &r->vbo);
    r->gl.DeleteBuffers(1, &r->ebo);
    r->gl.DeleteVertexArrays(1, &r->vao);
    r->gl.DeleteProgram(r->program);
    free(r);
}

static cui_texture cui__gl_tex_create(void *user, int w, int h, cui_texture_format fmt, const void *pixels) {
    cui_gl_renderer *r = user;
    int slot = -1;
    for (int i = 0; i < r->tex_count; i++) if (!r->tex[i].id) { slot = i; break; }
    if (slot < 0) {
        if (r->tex_count == r->tex_cap) {
            r->tex_cap = r->tex_cap ? r->tex_cap * 2 : 16;
            void *n = realloc(r->tex, sizeof(*r->tex) * (size_t)r->tex_cap);
            if (!n) return 0;
            r->tex = n;
        }
        slot = r->tex_count++;
    }
    GLuint_ id = 0;
    r->gl.GenTextures(1, &id);
    r->gl.BindTexture(GL_TEXTURE_2D_, id);
    r->gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_);
    r->gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_LINEAR_);
    r->gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
    r->gl.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_T_, GL_CLAMP_TO_EDGE_);
    r->gl.PixelStorei(GL_UNPACK_ALIGNMENT_, 1);
    r->gl.PixelStorei(GL_UNPACK_ROW_LENGTH_, 0);
    if (fmt == CUI_TEXTURE_R8) r->gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_R8_, w, h, 0, GL_RED_, GL_UNSIGNED_BYTE_, pixels);
    else r->gl.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGBA8_, w, h, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, pixels);
    r->tex[slot].id = id;
    r->tex[slot].fmt = fmt;
    r->tex[slot].w = w;
    r->tex[slot].h = h;
    return (cui_texture)(slot + 1);
}

static void cui__gl_tex_update(void *user, cui_texture tex, int x, int y, int w, int h, const void *pixels, int stride) {
    cui_gl_renderer *r = user;
    int slot = (int)tex - 1;
    if (slot < 0 || slot >= r->tex_count || !r->tex[slot].id) return;
    int bpp = (int)r->tex[slot].fmt;
    r->gl.BindTexture(GL_TEXTURE_2D_, r->tex[slot].id);
    r->gl.PixelStorei(GL_UNPACK_ALIGNMENT_, 1);
    r->gl.PixelStorei(GL_UNPACK_ROW_LENGTH_, stride / bpp);
    r->gl.TexSubImage2D(GL_TEXTURE_2D_, 0, x, y, w, h, r->tex[slot].fmt == CUI_TEXTURE_R8 ? GL_RED_ : GL_RGBA_, GL_UNSIGNED_BYTE_, pixels);
    r->gl.PixelStorei(GL_UNPACK_ROW_LENGTH_, 0);
}

static void cui__gl_tex_destroy(void *user, cui_texture tex) {
    cui_gl_renderer *r = user;
    int slot = (int)tex - 1;
    if (slot < 0 || slot >= r->tex_count || !r->tex[slot].id) return;
    r->gl.DeleteTextures(1, &r->tex[slot].id);
    r->tex[slot].id = 0;
}

bool cui_gl_is_software(const cui_gl_renderer *r) { return r && r->software; }

cui_renderer_iface cui_gl_iface(cui_gl_renderer *r) {
    cui_renderer_iface i;
    memset(&i, 0, sizeof i);
    i.user = r;
    i.texture_create = cui__gl_tex_create;
    i.texture_update = cui__gl_tex_update;
    i.texture_destroy = cui__gl_tex_destroy;
    return i;
}

void cui_gl_render(cui_gl_renderer *r, const cui_draw_data *dd, bool clear) {
    if (!r || !dd) return;
    int W = dd->width_px, H = dd->height_px;
    r->gl.Viewport(0, 0, W, H);
    if (clear) {
        r->gl.ClearColor(dd->clear_color.r / 255.0f, dd->clear_color.g / 255.0f, dd->clear_color.b / 255.0f, 1.0f);
        r->gl.Clear(GL_COLOR_BUFFER_BIT_);
    }
    if (dd->index_count == 0) return;
    r->gl.Enable(GL_BLEND_);
    r->gl.BlendEquation(GL_FUNC_ADD_);
    r->gl.BlendFunc(GL_ONE_, GL_ONE_MINUS_SRC_ALPHA_);
    r->gl.Disable(GL_DEPTH_TEST_);
    r->gl.Disable(GL_CULL_FACE_);
    r->gl.Enable(GL_SCISSOR_TEST_);
    r->gl.UseProgram(r->program);
    r->gl.Uniform2f(r->u_viewport, (float)W, (float)H);
    r->gl.Uniform1i(r->u_tex, 0);
    r->gl.ActiveTexture(GL_TEXTURE0_);
    r->gl.BindVertexArray(r->vao);
    r->gl.BindBuffer(GL_ARRAY_BUFFER_, r->vbo);
    r->gl.BufferData(GL_ARRAY_BUFFER_, (GLsizeiptr_)(sizeof(cui_vertex) * (size_t)dd->vertex_count), dd->vertices, GL_STREAM_DRAW_);
    r->gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER_, r->ebo);
    r->gl.BufferData(GL_ELEMENT_ARRAY_BUFFER_, (GLsizeiptr_)(sizeof(uint32_t) * (size_t)dd->index_count), dd->indices, GL_STREAM_DRAW_);
    GLuint_ bound = (GLuint_)-1;
    for (int i = 0; i < dd->command_count; i++) {
        const cui_draw_cmd *c = &dd->commands[i];
        if (!c->index_count) continue;
        int x0 = (int)floorf(c->clip.x), y0 = (int)floorf(c->clip.y);
        int x1 = (int)ceilf(c->clip.x + c->clip.w), y1 = (int)ceilf(c->clip.y + c->clip.h);
        if (x1 <= x0 || y1 <= y0) continue;
        r->gl.Scissor(x0, H - y1, x1 - x0, y1 - y0);
        cui_texture t = c->texture ? c->texture : dd->font_atlas;
        int slot = (int)t - 1;
        GLuint_ tid = (slot >= 0 && slot < r->tex_count) ? r->tex[slot].id : 0;
        if (tid != bound) { r->gl.BindTexture(GL_TEXTURE_2D_, tid); bound = tid; }
        r->gl.DrawElements(GL_TRIANGLES_, (GLsizei_)c->index_count, GL_UNSIGNED_INT_, (const void *)(uintptr_t)(c->index_offset * sizeof(uint32_t)));
    }
    r->gl.Disable(GL_SCISSOR_TEST_);
    r->gl.BindVertexArray(0);
    r->gl.UseProgram(0);
}

void cui_gl_read_pixels(cui_gl_renderer *r, int w, int h, void *rgba) {
    if (!r || !rgba || w <= 0 || h <= 0) return;
    r->gl.Finish();
    r->gl.PixelStorei(0x0D05 /* GL_PACK_ALIGNMENT */, 1);
    r->gl.ReadPixels(0, 0, w, h, GL_RGBA_, GL_UNSIGNED_BYTE_, rgba);
    /* flip to top-down */
    uint8_t *p = rgba;
    size_t row = (size_t)w * 4;
    uint8_t *tmp = malloc(row);
    if (!tmp) return;
    for (int y = 0; y < h / 2; y++) {
        memcpy(tmp, p + (size_t)y * row, row);
        memcpy(p + (size_t)y * row, p + (size_t)(h - 1 - y) * row, row);
        memcpy(p + (size_t)(h - 1 - y) * row, tmp, row);
    }
    free(tmp);
}
