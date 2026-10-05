/* Host-side tests: build with  gcc -O2 -o host_test tests/host_test.c source/shapes.c source/game.c -lm */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/game.h"

static uint32_t trng = 12345u;
static float urand(void)
{
    trng = trng * 1664525u + 1013904223u;
    return (float)(trng >> 8) * (1.0f / 16777216.0f);
}
static float urange(float a, float b) { return a + (b - a) * urand(); }

typedef struct { float x, y; } V2;

static int build_poly(int sym, V2 *v, int *closed)
{
    *closed = 0;
    switch (sym) {
    case SYM_VLINE:     v[0] = (V2){ 0, -1 }; v[1] = (V2){ 0, 1 }; return 2;
    case SYM_HLINE:     v[0] = (V2){ -1, 0 }; v[1] = (V2){ 1, 0 }; return 2;
    case SYM_SLASH:     v[0] = (V2){ -1, 1 }; v[1] = (V2){ 1, -1 }; return 2;
    case SYM_BACKSLASH: v[0] = (V2){ -1, -1 }; v[1] = (V2){ 1, 1 }; return 2;
    case SYM_ZIGZAG:
        v[0] = (V2){ -1, -0.8f }; v[1] = (V2){ 1, -0.8f };
        v[2] = (V2){ -1, 0.8f };  v[3] = (V2){ 1, 0.8f };
        return 4;
    case SYM_TRIANGLE:
        *closed = 1;
        v[0] = (V2){ 0, -1 }; v[1] = (V2){ 0.9f, 0.8f }; v[2] = (V2){ -0.9f, 0.8f };
        return 3;
    case SYM_SQUARE:
        *closed = 1;
        v[0] = (V2){ -1, -1 }; v[1] = (V2){ 1, -1 }; v[2] = (V2){ 1, 1 }; v[3] = (V2){ -1, 1 };
        return 4;
    case SYM_CIRCLE:
        *closed = 1;
        for (int i = 0; i < 48; i++) {
            float a = (float)i * 6.2831853f / 48.0f;
            v[i] = (V2){ cosf(a), sinf(a) };
        }
        return 48;
    }
    return 0;
}

/* mode 0 = normal, 1 = sloppy (bigger gaps / unclosed shapes) */
static int gen_stroke(int sym, Pt *out, int maxn, int mode)
{
    V2 v[64], t[64];
    int closed;
    int nv = build_poly(sym, v, &closed);

    float half = urange(25.0f, 100.0f);
    float sx = half * urange(0.8f, 1.2f), sy = half * urange(0.8f, 1.2f);
    float rot = urange(-0.2f, 0.2f);
    float cs = cosf(rot), sn = sinf(rot);
    float cx = 160.0f, cy = 120.0f;

    int rev = urand() < 0.5f;
    int start = closed ? (int)(urand() * nv) % nv : 0;
    for (int i = 0; i < nv; i++) {
        int k = rev ? (start - i + nv * 2) % nv : (start + i) % nv;
        if (!closed && rev) k = nv - 1 - i;
        float x = v[k].x * sx, y = v[k].y * sy;
        t[i].x = cx + x * cs - y * sn;
        t[i].y = cy + x * sn + y * cs;
    }

    int ne = closed ? nv : nv - 1;
    float L = 0.0f;
    float el[64];
    for (int i = 0; i < ne; i++) {
        V2 a = t[i], b = t[(i + 1) % nv];
        el[i] = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        L += el[i];
    }

    float total = L;
    if (closed) {
        float o = mode == 0 ? urange(-0.15f, 0.08f) : urange(-0.30f, -0.20f);
        total = L * (1.0f + o);
    }

    int n = 0;
    float s = 0.0f;
    while (s <= total && n < maxn) {
        float ss = closed ? fmodf(s, L) : s;
        int e = 0;
        while (e < ne - 1 && ss > el[e]) { ss -= el[e]; e++; }
        V2 a = t[e], b = t[(e + 1) % nv];
        float f = el[e] > 0 ? ss / el[e] : 0;
        out[n].x = a.x + (b.x - a.x) * f + urange(-1.0f, 1.0f);
        out[n].y = a.y + (b.y - a.y) * f + urange(-1.0f, 1.0f);
        n++;
        s += urange(2.5f, 6.0f);
    }

    if (urand() < 0.5f) {   /* round the corners a little, like a real hand */
        for (int i = 1; i < n - 1; i++) {
            out[i].x = (out[i - 1].x + out[i].x + out[i + 1].x) / 3.0f;
            out[i].y = (out[i - 1].y + out[i].y + out[i + 1].y) / 3.0f;
        }
    }
    return n;
}

static int test_recognizer(void)
{
    int fail = 0;
    const int trials = 600;
    Pt buf[MAX_STROKE];

    printf("== Recognizer accuracy (%d noisy strokes per rune) ==\n", trials);
    for (int mode = 0; mode < 2; mode++) {
        printf(mode == 0 ? "-- normal drawing --\n" : "-- sloppy (triangle / circle left open) --\n");
        for (int sym = 0; sym < SYM_COUNT; sym++) {
            if (mode == 1 && sym != SYM_TRIANGLE && sym != SYM_CIRCLE) continue;
            int ok = 0, none = 0, conf[SYM_COUNT + 1];
            memset(conf, 0, sizeof conf);
            for (int t = 0; t < trials; t++) {
                int n = gen_stroke(sym, buf, MAX_STROKE, mode);
                Symbol r = shapes_recognize(buf, n);
                if (r == (Symbol)sym) ok++;
                else if (r == SYM_NONE) none++;
                else conf[r]++;
            }
            printf("  %-16s %5.1f%%   (none %d", shapes_name((Symbol)sym), 100.0 * ok / trials, none);
            for (int c = 0; c < SYM_COUNT; c++)
                if (conf[c]) printf(", ->%s %d", shapes_short_name((Symbol)c), conf[c]);
            printf(")\n");
            double need = mode == 0 ? 0.93 : 0.75;
            if ((double)ok / trials < need) {
                printf("    ** below %.0f%% **\n", need * 100);
                fail = 1;
            }
        }
    }


    /* Shapes that are NOT runes must not turn into a different rune. */
    {
        struct { const char *name; V2 v[6]; int n; } neg_shapes[] = {
            { "U shape (3 sides of a square)", { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } }, 4 },
            { "V shape",                       { { -1, -1 }, { 0, 1 }, { 1, -1 } }, 3 },
            { "L shape",                       { { -1, -1 }, { -1, 1 }, { 1, 1 } }, 3 },
        };
        for (int k = 0; k < 3; k++) {
            int bad = 0;
            for (int t = 0; t < 200; t++) {
                float half = urange(30.0f, 90.0f);
                int n = 0;
                for (int e = 0; e + 1 < neg_shapes[k].n; e++) {
                    V2 a = neg_shapes[k].v[e], b = neg_shapes[k].v[e + 1];
                    for (int s = 0; s < 12; s++) {
                        float f = (float)s / 12.0f;
                        buf[n].x = 160 + (a.x + (b.x - a.x) * f) * half + urange(-1, 1);
                        buf[n].y = 120 + (a.y + (b.y - a.y) * f) * half + urange(-1, 1);
                        n++;
                    }
                }
                V2 last = neg_shapes[k].v[neg_shapes[k].n - 1];
                buf[n].x = 160 + last.x * half; buf[n].y = 120 + last.y * half; n++;
                Symbol r = shapes_recognize(buf, n);
                if (r != SYM_NONE) bad++;
            }
            printf("  %-32s misread as a rune in %d/200\n", neg_shapes[k].name, bad);
            if (bad > 20) fail = 1;
        }
    }

    /* Degenerate input must never crash and must return NONE. */
    Pt one[1] = { { 5, 5 } };
    Pt same[20];
    for (int i = 0; i < 20; i++) same[i] = (Pt){ 50, 50 };
    if (shapes_recognize(NULL, 10) != SYM_NONE) fail = 1;
    if (shapes_recognize(one, 1) != SYM_NONE) fail = 1;
    if (shapes_recognize(same, 20) != SYM_NONE) fail = 1;
    Pt tiny[5] = { { 0, 0 }, { 3, 0 }, { 6, 1 }, { 9, 1 }, { 12, 0 } };
    if (shapes_recognize(tiny, 5) != SYM_NONE) fail = 1;
    printf("  degenerate input: %s\n", fail ? "see above" : "ok");
    return fail;
}

/* ---- bot that plays whole runs through the real stroke pipeline ---- */

static int run_bot(uint32_t seed, float casts_per_sec, int *score_out)
{
    static Game g;
    Pt buf[MAX_STROKE];
    game_init(&g, seed, 0);
    g.ui_lock = 0;
    game_key(&g, GK_CONFIRM);               /* start */
    if (g.state != ST_PLAY) { printf("failed to start\n"); exit(1); }

    float cool = 0.0f, pick_wait = 0.0f;
    const float dt = 1.0f / 60.0f;
    for (long f = 0; f < 60L * 60 * 30 && g.state != ST_GAMEOVER; f++) {   /* max 30 min */
        game_update(&g, dt);
        cool -= dt;
        if (g.state == ST_PLAY && cool <= 0.0f) {
            int best = -1;
            for (int i = 0; i < MAX_ENEMIES; i++) {
                const Enemy *e = &g.en[i];
                if (!e->alive || e->x >= TOP_W + 8) continue;
                if (best < 0 || e->x < g.en[best].x) best = i;
            }
            if (best >= 0) {
                int sym = g.en[best].seq[g.en[best].prog];
                int n = gen_stroke(sym, buf, MAX_STROKE, 0);
                game_stroke(&g, buf, n);
                cool = (1.0f / casts_per_sec) * urange(0.8f, 1.2f);
            }
        }
        if (g.state == ST_UPGRADE) {
            pick_wait += dt;
            if (pick_wait > 0.8f && g.n_choices > 0) {
                game_pick_upgrade(&g, (int)(urand() * g.n_choices) % g.n_choices);
                pick_wait = 0.0f;
            }
        }
    }
    *score_out = g.score;
    return g.wave;
}

static int test_game(void)
{
    printf("\n== Bot playthroughs (perfect rune choice, noisy strokes, random upgrades) ==\n");
    const float rates[] = { 1.0f, 1.6f, 2.4f };
    int fail = 0;
    for (int r = 0; r < 3; r++) {
        int sum = 0, mn = 999, mx = 0, ssum = 0;
        const int runs = 20;
        for (int k = 0; k < runs; k++) {
            int sc;
            int w = run_bot(1000u + (uint32_t)k * 7919u + (uint32_t)r, rates[r], &sc);
            sum += w; ssum += sc;
            if (w < mn) mn = w;
            if (w > mx) mx = w;
        }
        printf("  %.1f casts/s : died on wave avg %.1f (min %d, max %d), avg score %d\n",
               rates[r], (double)sum / runs, mn, mx, ssum / runs);
        if (mx >= 80) { printf("    ** never dies - difficulty not scaling **\n"); fail = 1; }
    }
    if (!fail) printf("  difficulty scales: every bot eventually dies\n");
    return fail;
}

int main(void)
{
    int fail = 0;
    fail |= test_recognizer();
    fail |= test_game();
    printf("\n%s\n", fail ? "FAILED" : "ALL OK");
    return fail;
}
