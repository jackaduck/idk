#include "shapes.h"

#include <math.h>
#include <string.h>

#define MAX_IN    512
#define RS_N      64
#define MIN_PATH  24.0f
#define PI_F      3.14159265f
#define MAX_CORN  12

typedef struct {
    int   n;
    int   idx[MAX_CORN];
    float peak[MAX_CORN];
    int   sign[MAX_CORN];
} Corners;

static float pdist(Pt a, Pt b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

static float path_len(const Pt *p, int n)
{
    float d = 0.0f;
    for (int i = 1; i < n; i++) d += pdist(p[i - 1], p[i]);
    return d;
}

/* Resample a polyline to `count` evenly spaced points. */
static void resample(const Pt *in, int n, Pt *out, int count)
{
    float total = path_len(in, n);
    float interval = total / (float)(count - 1);
    float acc = 0.0f;
    int outn = 0;
    Pt prev = in[0];
    int i = 1;

    out[outn++] = prev;
    while (i < n && outn < count) {
        Pt cur = in[i];
        float d = pdist(prev, cur);
        if (d < 1e-6f) { i++; continue; }
        if (acc + d >= interval) {
            float t = (interval - acc) / d;
            prev.x += t * (cur.x - prev.x);
            prev.y += t * (cur.y - prev.y);
            out[outn++] = prev;
            acc = 0.0f;
        } else {
            acc += d;
            prev = cur;
            i++;
        }
    }
    while (outn < count) out[outn++] = in[n - 1];
}

/* Turning angle (degrees, unsigned) and turn direction at each point,
 * measured between chords of k samples on either side. */
static void turning(const Pt *p, int n, int closed, int k, float *ang, int *sgn)
{
    for (int i = 0; i < n; i++) { ang[i] = 0.0f; sgn[i] = 0; }
    for (int i = 0; i < n; i++) {
        int a, b;
        if (closed) {
            a = (i - k + n) % n;
            b = (i + k) % n;
        } else {
            if (i < k || i + k >= n) continue;
            a = i - k;
            b = i + k;
        }
        float ax = p[i].x - p[a].x, ay = p[i].y - p[a].y;
        float bx = p[b].x - p[i].x, by = p[b].y - p[i].y;
        float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
        if (la < 1e-4f || lb < 1e-4f) continue;
        float c = (ax * bx + ay * by) / (la * lb);
        if (c > 1.0f) c = 1.0f;
        if (c < -1.0f) c = -1.0f;
        ang[i] = acosf(c) * 180.0f / PI_F;
        sgn[i] = (ax * by - ay * bx) >= 0.0f ? 1 : -1;
    }
}

static void find_corners(const Pt *p, int n, int closed, float thresh, Corners *out)
{
    const int k = 4;
    float ang[RS_N];
    int sgn[RS_N];

    out->n = 0;
    turning(p, n, closed, k, ang, sgn);

    for (int i = 0; i < n && out->n < MAX_CORN; i++) {
        if (ang[i] < thresh) continue;
        int is_max = 1;
        for (int d = 1; d <= k && is_max; d++) {
            int lo = i - d, hi = i + d;
            if (closed) {
                lo = (lo + n) % n;
                hi = hi % n;
            } else {
                if (lo < 0) lo = -1;
                if (hi >= n) hi = -1;
            }
            if (lo >= 0 && ang[lo] >= ang[i]) is_max = 0;
            if (hi >= 0 && ang[hi] > ang[i]) is_max = 0;
        }
        if (is_max) {
            out->idx[out->n] = i;
            out->peak[out->n] = ang[i];
            out->sign[out->n] = sgn[i];
            out->n++;
        }
    }
}

/* Total signed heading change along an open path, in degrees. */
static float total_turn(const Pt *p, int n)
{
    const int k = 4;
    float sum = 0.0f;
    for (int i = k; i + k < n; i += k) {
        float ax = p[i].x - p[i - k].x, ay = p[i].y - p[i - k].y;
        float bx = p[i + k].x - p[i].x, by = p[i + k].y - p[i].y;
        float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
        if (la < 1e-4f || lb < 1e-4f) continue;
        float c = (ax * bx + ay * by) / (la * lb);
        if (c > 1.0f) c = 1.0f;
        if (c < -1.0f) c = -1.0f;
        float a = acosf(c) * 180.0f / PI_F;
        sum += ((ax * by - ay * bx) >= 0.0f) ? a : -a;
    }
    return sum;
}

/* Re-measure open-path corner angles using whole segments between corners.
 * Much less sensitive to smoothing/rounding than the short-window estimate. */
static void refine_open(const Pt *p, int n, Corners *c)
{
    for (int i = 0; i < c->n; i++) {
        int ia = (i == 0) ? 0 : c->idx[i - 1];
        int ib = c->idx[i];
        int ic = (i == c->n - 1) ? n - 1 : c->idx[i + 1];
        int la = ib - ia, lb = ic - ib;
        if (la < 8 || lb < 8) continue;
        int ma = la / 6 > 2 ? la / 6 : 2;
        int mb = lb / 6 > 2 ? lb / 6 : 2;
        float ax = p[ib - ma].x - p[ia + ma].x, ay = p[ib - ma].y - p[ia + ma].y;
        float bx = p[ic - mb].x - p[ib + mb].x, by = p[ic - mb].y - p[ib + mb].y;
        float na = sqrtf(ax * ax + ay * ay), nb = sqrtf(bx * bx + by * by);
        if (na < 1e-4f || nb < 1e-4f) continue;
        float cs = (ax * bx + ay * by) / (na * nb);
        if (cs > 1.0f) cs = 1.0f;
        if (cs < -1.0f) cs = -1.0f;
        c->peak[i] = acosf(cs) * 180.0f / PI_F;
    }
}

static Symbol classify_closed(const Pt *sm, int rn, float diag)
{
    static Pt tmp[MAX_IN + 1];
    Pt rs[RS_N];
    static const float ths[4] = { 55.0f, 42.0f, 70.0f, 35.0f };
    const int m = RS_N - 1;

    memcpy(tmp, sm, (size_t)rn * sizeof(Pt));
    tmp[rn] = sm[0];
    resample(tmp, rn + 1, rs, RS_N);

    for (int t = 0; t < 4; t++) {
        Corners c;
        find_corners(rs, m, 1, ths[t], &c);
        if (c.n == 3) return SYM_TRIANGLE;
        if (c.n == 4) return SYM_SQUARE;
        if (t == 0 && c.n == 0) return diag >= 20.0f ? SYM_CIRCLE : SYM_NONE;
        if (t == 0 && c.n == 2) {
            /* Elongated circle: two gentle corners at opposite ends. */
            int di = c.idx[1] - c.idx[0];
            if (di < 0) di = -di;
            int off = di - m / 2;
            if (off < 0) off = -off;
            if (off < m * 15 / 100 && c.peak[0] < 85.0f && c.peak[1] < 85.0f && diag >= 20.0f)
                return SYM_CIRCLE;
        }
    }
    return SYM_NONE;
}

/* Classify from the corners of the open (unclosed) path. */
static Symbol classify_open_corners(const Corners *c)
{
    int alternates = 0;
    float sum = 0.0f;

    for (int i = 0; i < c->n; i++) {
        sum += c->peak[i];
        if (i > 0 && c->sign[i] != c->sign[i - 1]) alternates = 1;
    }

    /* Corners that alternate left/right turns: zigzag (Z, N, W ...). */
    if (c->n >= 2 && alternates) return SYM_ZIGZAG;

    /* Same-direction corners: a polygon whose last side was never closed. */
    if (c->n == 2 && sum >= 200.0f) return SYM_TRIANGLE;
    if (c->n == 3) {
        if (sum >= 300.0f) return SYM_TRIANGLE;   /* ~3 x 120 degrees */
        if (sum >= 230.0f) return SYM_SQUARE;     /* ~3 x 90 degrees  */
    }
    if (c->n == 4 && sum >= 300.0f) return SYM_SQUARE;
    return SYM_NONE;
}

Symbol shapes_recognize(const Pt *pts, int n)
{
    static Pt raw[MAX_IN];
    static Pt sm[MAX_IN + 1];

    if (!pts || n < 3) return SYM_NONE;

    /* Copy, decimating if the stroke is very long. */
    int step = (n + MAX_IN - 1) / MAX_IN;
    int rn = 0;
    for (int i = 0; i < n && rn < MAX_IN; i += step) raw[rn++] = pts[i];
    if ((n - 1) % step != 0) {
        if (rn < MAX_IN) raw[rn++] = pts[n - 1];
        else raw[rn - 1] = pts[n - 1];
    }
    if (rn < 3) return SYM_NONE;

    /* Light smoothing to take the edge off touch-screen jitter. */
    for (int i = 0; i < rn; i++) {
        int a = i > 0 ? i - 1 : i;
        int b = i < rn - 1 ? i + 1 : i;
        sm[i].x = (raw[a].x + raw[i].x + raw[b].x) / 3.0f;
        sm[i].y = (raw[a].y + raw[i].y + raw[b].y) / 3.0f;
    }

    float plen = path_len(sm, rn);
    if (plen < MIN_PATH) return SYM_NONE;

    float minx = sm[0].x, maxx = sm[0].x, miny = sm[0].y, maxy = sm[0].y;
    for (int i = 1; i < rn; i++) {
        if (sm[i].x < minx) minx = sm[i].x;
        if (sm[i].x > maxx) maxx = sm[i].x;
        if (sm[i].y < miny) miny = sm[i].y;
        if (sm[i].y > maxy) maxy = sm[i].y;
    }
    float w = maxx - minx, h = maxy - miny;
    float diag = sqrtf(w * w + h * h);
    float gap = pdist(sm[0], sm[rn - 1]);
    float straight = gap / plen;

    /* Straight lines. */
    if (straight > 0.88f) {
        float dx = sm[rn - 1].x - sm[0].x;
        float dy = sm[rn - 1].y - sm[0].y;
        float adx = fabsf(dx), ady = fabsf(dy);
        if (adx > 2.2f * ady) return SYM_HLINE;
        if (ady > 2.2f * adx) return SYM_VLINE;
        /* '/' runs bottom-left to top-right (screen y points down). */
        return ((dx > 0.0f) == (dy < 0.0f)) ? SYM_SLASH : SYM_BACKSLASH;
    }

    /* Corners and total turning along the path as drawn (not auto-closed). */
    Pt rs[RS_N];
    Corners co;
    resample(sm, rn, rs, RS_N);
    find_corners(rs, RS_N, 0, 55.0f, &co);
    refine_open(rs, RS_N, &co);

    if (co.n == 0) {
        /* Smooth curve: a circle if it wraps most of the way around. */
        float t = total_turn(rs, RS_N);
        return ((t > 200.0f || t < -200.0f) && diag >= 20.0f) ? SYM_CIRCLE : SYM_NONE;
    }

    if (straight < 0.2f) {
        /* Start and end meet: treat as a closed shape. */
        Symbol s = classify_closed(sm, rn, diag);
        if (s != SYM_NONE) return s;
    }
    return classify_open_corners(&co);
}

const char *shapes_name(Symbol s)
{
    switch (s) {
    case SYM_VLINE:     return "Vertical Line";
    case SYM_HLINE:     return "Horizontal Line";
    case SYM_TRIANGLE:  return "Triangle";
    case SYM_CIRCLE:    return "Circle";
    case SYM_SQUARE:    return "Square";
    case SYM_SLASH:     return "Slash";
    case SYM_BACKSLASH: return "Backslash";
    case SYM_ZIGZAG:    return "Zigzag";
    default:            return "?";
    }
}

const char *shapes_short_name(Symbol s)
{
    switch (s) {
    case SYM_VLINE:     return "Vert";
    case SYM_HLINE:     return "Horiz";
    case SYM_TRIANGLE:  return "Tri";
    case SYM_CIRCLE:    return "Circle";
    case SYM_SQUARE:    return "Square";
    case SYM_SLASH:     return "Slash";
    case SYM_BACKSLASH: return "Back";
    case SYM_ZIGZAG:    return "Zig";
    default:            return "?";
    }
}
