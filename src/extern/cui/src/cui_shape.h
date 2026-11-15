/* Internal, font-unit OpenType shaping. Input is one font/script/direction run. */
#ifndef CUI_SHAPE_H
#define CUI_SHAPE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t codepoint;
    int id, cluster, advance, x, y;
    int form, ligature, component;
} cui_shape_glyph;
typedef struct {
    const unsigned char *data;
    size_t size;
    uint32_t gsub, gpos, gdef, cmap, hmtx;
    int metrics;
} cui_shape_font;
int cui__shape_font_init(cui_shape_font *f, const void *data, size_t size, int offset);
int cui__shape_map(const cui_shape_font *f, uint32_t cp);
int cui__shape(cui_shape_font *f, cui_shape_glyph *glyphs, int count, int capacity,
               uint32_t script, int rtl, int tabular);
#endif
