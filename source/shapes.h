#ifndef SHAPES_H
#define SHAPES_H

/* Single-stroke shape recognizer for touch-screen runes.
 * Pure C, no platform dependencies, so it can be unit-tested on a PC. */

typedef enum {
    SYM_NONE = -1,
    SYM_VLINE = 0,   /*  |  */
    SYM_HLINE,       /*  -  */
    SYM_TRIANGLE,    /*  ^  closed, 3 corners */
    SYM_CIRCLE,      /*  O  */
    SYM_SQUARE,      /*  [] closed, 4 corners */
    SYM_SLASH,       /*  /  */
    SYM_BACKSLASH,   /*  \  */
    SYM_ZIGZAG,      /*  Z / N / W  */
    SYM_COUNT
} Symbol;

typedef struct {
    float x, y;
} Pt;

/* Returns the recognized symbol, or SYM_NONE if the stroke is too short or
 * does not look like any rune. */
Symbol shapes_recognize(const Pt *pts, int n);

const char *shapes_name(Symbol s);
const char *shapes_short_name(Symbol s);

#endif
