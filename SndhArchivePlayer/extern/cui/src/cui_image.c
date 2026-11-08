/* Image loading through stb_image: PNG and JPEG by default. Define
 * CUI_IMAGE_ALL_FORMATS to also accept BMP, GIF and TGA (about 7 KB more),
 * or CUI_NO_STB_IMAGE to leave image decoding out entirely. */
#include "cui_internal.h"

#ifndef CUI_NO_STB_IMAGE
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#ifdef CUI_IMAGE_ALL_FORMATS
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#endif
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
/* stb_image declares stbi_load_gif_from_memory even with GIF off; that warning
 * is reported at the end of the file, so it stays silenced for the whole file */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996 4244 4456 4457)
#endif
#include "../third_party/stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* color emoji bitmaps (sbix, CBDT) are PNG */
unsigned char *cui__png_decode(const unsigned char *data, int len, int *w, int *h) {
    int comp = 0;
    return stbi_load_from_memory(data, len, w, h, &comp, 4);
}
void cui__png_free(unsigned char *px) { stbi_image_free(px); }

cui_texture cui_image_load(cui_ctx *ui, const char *path, int *w, int *h) {
    int iw = 0, ih = 0, comp = 0;
    unsigned char *px = stbi_load(path, &iw, &ih, &comp, 4);
    if (!px) return 0;
    cui_texture t = cui_image_create(ui, iw, ih, px);
    stbi_image_free(px);
    if (w) *w = iw;
    if (h) *h = ih;
    return t;
}
#else
unsigned char *cui__png_decode(const unsigned char *data, int len, int *w, int *h) { (void)data; (void)len; (void)w; (void)h; return NULL; }
void cui__png_free(unsigned char *px) { (void)px; }
cui_texture cui_image_load(cui_ctx *ui, const char *path, int *w, int *h) {
    (void)ui; (void)path; (void)w; (void)h;
    return 0;
}
#endif
