/* UAX #9: P2/P3, W1-W7, N0-N2, I1/I2 and per-line L1/L2/L4.
 * Explicit directional formatting is intentionally treated as boundary neutral. */
#include "cui_bidi.h"
#include <stddef.h>

enum { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON, LRE };
#include "cui_bidi_data.h"
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int bidi_class(uint32_t cp) {
    int lo = 0, hi = COUNT(cui__bidi_classes);
    while (lo + 1 < hi) {
        int m = (lo + hi) / 2;
        if ((cui__bidi_classes[m] >> 5) <= cp) lo = m;
        else hi = m;
    }
    int type = cui__bidi_classes[lo] & 31;
    return type >= LRE ? BN : type;
}

int cui__bidi_strong_direction(uint32_t cp) {
    int t = bidi_class(cp);
    return t == L ? 0 : (t == R || t == AL ? 1 : -1);
}

static int bracket(uint32_t cp) {
    int lo = 0, hi = COUNT(cui__bidi_brackets);
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (cui__bidi_brackets[m].cp < cp) lo = m + 1;
        else hi = m;
    }
    return lo < COUNT(cui__bidi_brackets) && cui__bidi_brackets[lo].cp == cp ? lo : -1;
}

static int strong(int t) { return t == EN || t == AN ? R : t; }
static int neutral(int t) { return t == B || t == S || t == WS || t == ON; }
static uint32_t canonical(uint32_t cp) { return cp == 0x232A ? 0x3009 : cp; }

int cui__bidi_paragraph(cui_bidi_char *c, int n, int base) {
    int i, j, k, prev, last, stack[63], top = 0, overflow = 0;
    for (i = 0; i < n; i++) {
        c[i].original = c[i].type = (uint8_t)bidi_class(c[i].cp);
        c[i].pair = -1;
    }
    if (base < 0) {
        base = 0;
        for (i = 0; i < n; i++) {
            int t = c[i].type;
            if (t == L || t == R || t == AL) { base = t != L; break; }
        }
    }
    int sos = base ? R : L;
    /* W1-W3: BN does not interrupt context, including Arabic before EN. */
    prev = last = sos;
    for (i = 0; i < n; i++) {
        int t = c[i].type;
        if (t == BN) continue;
        if (t == NSM) t = prev;
        prev = t;
        if (t == EN && last == AL) t = AN;
        if (t == L || t == R || t == AL) last = t;
        if (t == AL) t = R;
        c[i].type = (uint8_t)t;
    }
    /* W4 */
    prev = -1;
    for (i = 0; i < n; i++) {
        int t = c[i].type;
        if (t == BN) continue;
        j = i + 1;
        while (j < n && c[j].type == BN) j++;
        if (prev >= 0 && j < n && c[prev].type == c[j].type &&
            ((t == ES && c[j].type == EN) || (t == CS && (c[j].type == EN || c[j].type == AN))))
            c[i].type = c[j].type;
        prev = i;
    }
    /* W5 */
    prev = sos;
    for (i = 0; i < n; i++) {
        if (c[i].type == BN) continue;
        if (c[i].type == ET) {
            j = i;
            while (j < n && (c[j].type == ET || c[j].type == BN)) j++;
            if (prev == EN || (j < n && c[j].type == EN))
                for (k = i; k < j; k++) if (c[k].type != BN) c[k].type = EN;
            i = j - 1;
        } else prev = c[i].type;
    }
    /* W6/W7 */
    last = sos;
    for (i = 0; i < n; i++) {
        int t = c[i].type;
        if (t == ES || t == ET || t == CS) t = ON;
        if (t == EN && last == L) t = L;
        if (t == L || t == R) last = t;
        c[i].type = (uint8_t)t;
    }
    /* BD16: stack capacity and canonical equivalents are prescribed by UAX #9. */
    for (i = 0; i < n && !overflow; i++) {
        if (c[i].type != ON) continue;
        int b = bracket(c[i].cp);
        if (b < 0) continue;
        if (cui__bidi_brackets[b].open) {
            if (top == COUNT(stack)) { overflow = 1; break; }
            stack[top++] = i;
        } else {
            for (j = top - 1; j >= 0; j--) {
                int open = stack[j];
                int ob = bracket(c[open].cp);
                if (canonical(cui__bidi_brackets[ob].pair) == canonical(c[i].cp)) {
                    c[open].pair = i;
                    top = j;
                    break;
                }
            }
        }
    }
    /* N0, in opening-bracket order; nested brackets see earlier resolutions. */
    for (i = 0; i < n; i++) {
        j = c[i].pair;
        if (j < 0) continue;
        int found = -1;
        for (k = i + 1; k < j; k++) {
            int t = strong(c[k].type);
            if (t == sos) { found = sos; break; }
            if (t == L || t == R) found = t;
        }
        if (found < 0) continue;
        if (found != sos) {
            found = sos;
            for (k = i - 1; k >= 0; k--) {
                int t = strong(c[k].type);
                if (t == L || t == R) { found = t; break; }
            }
        }
        c[i].type = c[j].type = (uint8_t)found;
        for (k = i + 1; k < n && (c[k].original == NSM || c[k].original == BN); k++)
            if (c[k].original == NSM) c[k].type = (uint8_t)found;
        for (k = j + 1; k < n && (c[k].original == NSM || c[k].original == BN); k++)
            if (c[k].original == NSM) c[k].type = (uint8_t)found;
    }
    /* N1/N2 */
    prev = sos;
    for (i = 0; i < n; i++) {
        if (c[i].type == BN) continue;
        if (neutral(c[i].type)) {
            j = i;
            while (j < n && (neutral(c[j].type) || c[j].type == BN)) j++;
            int after = j < n ? strong(c[j].type) : sos;
            int resolved = prev == after ? prev : sos;
            for (k = i; k < j; k++) if (c[k].type != BN) c[k].type = (uint8_t)resolved;
            i = j - 1;
        } else prev = strong(c[i].type);
    }
    /* I1/I2. Retained BN follows its preceding level for shaping. */
    prev = base;
    for (i = 0; i < n; i++) {
        int t = c[i].type;
        c[i].level = (uint8_t)(t == BN ? prev : base + (base ? t != R : t == R ? 1 : t == EN || t == AN ? 2 : 0));
        prev = c[i].level;
    }
    return base;
}

void cui__bidi_line(const cui_bidi_char *c, int start, int n, int base, int *order, uint8_t *levels) {
    int i, j, k, max = base;
    for (i = 0; i < n; i++) { order[i] = start + i; levels[i] = c[start+i].level; }
    /* L1 uses original classes, including retained BN in reset sequences. */
    for (i = 0; i <= n; i++) {
        if (i == n || c[start+i].original == B || c[start+i].original == S) {
            if (i < n) levels[i] = (uint8_t)base;
            for (j = i - 1; j >= 0 && (c[start+j].original == WS || c[start+j].original == BN); j--)
                levels[j] = (uint8_t)base;
        }
    }
    for (i = 0; i < n; i++) if (levels[i] > max) max = levels[i];
    /* L2 */
    for (k = max; k >= 1; k--) {
        for (i = 0; i < n; i++) {
            if (levels[order[i]-start] < k) continue;
            j = i + 1;
            while (j < n && levels[order[j]-start] >= k) j++;
            int end = j;
            for (j--; i < j; i++, j--) { int t = order[i]; order[i] = order[j]; order[j] = t; }
            i = end - 1;
        }
    }
}

uint32_t cui__bidi_mirror(uint32_t cp, int level) {
    if (!(level & 1)) return cp;
    int lo = 0, hi = COUNT(cui__bidi_mirrors);
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (cui__bidi_mirrors[m][0] < cp) lo = m + 1;
        else hi = m;
    }
    return lo < COUNT(cui__bidi_mirrors) && cui__bidi_mirrors[lo][0] == cp ? cui__bidi_mirrors[lo][1] : cp;
}
