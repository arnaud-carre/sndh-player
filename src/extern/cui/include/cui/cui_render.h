/*
 * CUI renderers.
 *
 * CUI produces a plain list of vertices, indices and draw commands
 * (cui_draw_data). Two renderers ship with the library:
 *
 *   - OpenGL 3.3 core / OpenGL ES 3.0 (cui_gl_*)
 *   - A portable software rasterizer (cui_sw_*) that renders into any 32 bit
 *     pixel buffer. It needs no GPU, which makes it useful for tests,
 *     screenshots, servers and as a fallback.
 *
 * Writing your own renderer (Vulkan, Metal, Direct3D, WebGPU, sokol, bgfx)
 * takes one shader. See docs/rendering.md.
 */
#ifndef CUI_RENDER_H
#define CUI_RENDER_H

#include "cui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- software renderer ---- */

typedef enum cui_sw_format {
    CUI_SW_RGBA = 0,   /* bytes R, G, B, A in memory */
    CUI_SW_BGRA = 1    /* bytes B, G, R, A in memory (Win32 DIBs, X11, Cairo) */
} cui_sw_format;

typedef struct cui_sw_renderer cui_sw_renderer;

CUI_API cui_sw_renderer *cui_sw_create(void);
CUI_API void cui_sw_destroy(cui_sw_renderer *r);
/* Texture callbacks to pass to cui_create / cui_set_renderer. */
CUI_API cui_renderer_iface cui_sw_iface(cui_sw_renderer *r);
/* Renders draw data into a pixel buffer of width x height. stride is in bytes. */
CUI_API void cui_sw_render(cui_sw_renderer *r, const cui_draw_data *dd, void *pixels,
                           int width, int height, int stride, cui_sw_format format);
/* Number of worker threads (1 = single threaded, default: number of cores up to 8). */
CUI_API void cui_sw_set_threads(cui_sw_renderer *r, int threads);
/* Incremental rendering: when the same buffer is passed again, with what the last
 * call drew still in it, only the parts that changed are drawn again. A frame
 * where only a button's hover changed costs that button. Off by default.
 * cui_sw_last_damage gives the rectangle the last call drew (in pixels), so only
 * that part needs copying to the screen; it returns false when nothing changed. */
CUI_API void cui_sw_set_incremental(cui_sw_renderer *r, bool on);
CUI_API bool cui_sw_last_damage(const cui_sw_renderer *r, int *x, int *y, int *w, int *h);

/* Writes a PNG file (RGBA pixels). Returns false on failure. */
CUI_API bool cui_write_png(const char *path, const void *rgba, int width, int height, int stride);

/* ---- OpenGL renderer ---- */

typedef void *(*cui_gl_loader)(const char *name);
typedef struct cui_gl_renderer cui_gl_renderer;

/* Create after the GL context is current. `loader` resolves GL functions
 * (wglGetProcAddress, glXGetProcAddress, SDL_GL_GetProcAddress,
 * glfwGetProcAddress, ...). Returns NULL if the context lacks GL 3.3 / ES 3. */
CUI_API cui_gl_renderer *cui_gl_create(cui_gl_loader loader);
CUI_API void cui_gl_destroy(cui_gl_renderer *r);
CUI_API cui_renderer_iface cui_gl_iface(cui_gl_renderer *r);
/* Renders into the currently bound framebuffer. Set clear to false when
 * drawing the UI on top of your own scene. */
CUI_API void cui_gl_render(cui_gl_renderer *r, const cui_draw_data *dd, bool clear);
/* Reads the bound framebuffer into a top-down RGBA buffer. */
CUI_API void cui_gl_read_pixels(cui_gl_renderer *r, int width, int height, void *rgba);
/* True when the OpenGL driver draws on the CPU (Mesa llvmpipe, SwiftShader and the
 * like, as in virtual machines and remote sessions). CUI's own software renderer,
 * which redraws only what changed, is then much cheaper. */
CUI_API bool cui_gl_is_software(const cui_gl_renderer *r);

#ifdef __cplusplus
}
#endif

#endif /* CUI_RENDER_H */
