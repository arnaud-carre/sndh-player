/* Internal, allocation-free Unicode bidi for one paragraph. */
#ifndef CUI_BIDI_H
#define CUI_BIDI_H
#include <stdint.h>

typedef struct {
    uint32_t cp;
    int pair;
    uint8_t original, type, level;
} cui_bidi_char;

/* Set cp before calling. base: -1 = first strong (default LTR), otherwise 0/1.
 * Returns paragraph level. Explicit embeddings/overrides/isolates act as BN.
 * shortcut: explicit directional formatting is unsupported; add X rules when required. */
int cui__bidi_paragraph(cui_bidi_char *chars, int count, int base);
/* First-strong classification for layout: -1 neutral, 0 LTR, 1 RTL. */
int cui__bidi_strong_direction(uint32_t cp);
/* L1/L2 for [start, start+count), preserving paragraph levels. Caller supplies
 * count entries in order and levels. Order contains absolute paragraph indices,
 * including BN (joiners must survive until shaping); ignore BN for conformance. */
void cui__bidi_line(const cui_bidi_char *chars, int start, int count, int base,
                    int *order, uint8_t *levels);
/* L4 character substitution; an RTL-aware shaper can mirror glyphs instead. */
uint32_t cui__bidi_mirror(uint32_t cp, int level);
#endif
