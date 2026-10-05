#include "render.h"

#include <3ds.h>
#include <citro2d.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define Z 0.5f

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bot;
static C2D_TextBuf       s_buf;

enum { AL_LEFT, AL_CENTER, AL_RIGHT };

/* ------------------------------------------------------------------ */
/* Primitive helpers                                                   */
/* ------------------------------------------------------------------ */

static u32 rgba(int r, int g, int b, int a) { return C2D_Color32(r, g, b, a); }

static u32 sym_col(int s, int a)
{
    return rgba(SYM_COLORS[s][0], SYM_COLORS[s][1], SYM_COLORS[s][2], a);
}

static void line(float x0, float y0, float x1, float y1, float th, u32 c)
{
    C2D_DrawLine(x0, y0, c, x1, y1, c, th, Z);
}

static void rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, Z, w, h, c);
}

static void disc(float x, float y, float r, u32 c)
{
    C2D_DrawCircleSolid(x, y, Z, r, c);
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c)
{
    C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, Z);
}

static void ring(float cx, float cy, float r, float th, u32 c)
{
    const int N = 16;
    float px = cx + r, py = cy;
    for (int i = 1; i <= N; i++) {
        float a = (float)i * (6.2831853f / (float)N);
        float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        line(px, py, x, y, th, c);
        px = x;
        py = y;
    }
}

static void box_outline(float x, float y, float w, float h, float th, u32 c)
{
    line(x, y, x + w, y, th, c);
    line(x + w, y, x + w, y + h, th, c);
    line(x + w, y + h, x, y + h, th, c);
    line(x, y + h, x, y, th, c);
}

static u32 mix(const uint8_t col[3], float white, int a)
{
    if (white < 0.0f) white = 0.0f;
    if (white > 1.0f) white = 1.0f;
    return rgba((int)(col[0] + (255 - col[0]) * white),
                (int)(col[1] + (255 - col[1]) * white),
                (int)(col[2] + (255 - col[2]) * white), a);
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

static void text(float x, float y, float scale, u32 color, int align, const char *str)
{
    C2D_Text t;
    float w = 0.0f, h = 0.0f;

    C2D_TextParse(&t, s_buf, str);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    if (align == AL_CENTER) x -= w * 0.5f;
    else if (align == AL_RIGHT) x -= w;

    C2D_DrawText(&t, C2D_WithColor, x + 1.0f, y + 1.0f, Z, scale, scale, rgba(0, 0, 0, 200));
    C2D_DrawText(&t, C2D_WithColor, x, y, Z, scale, scale, color);
}

static void textf(float x, float y, float scale, u32 color, int align, const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    text(x, y, scale, color, align, buf);
}

/* ------------------------------------------------------------------ */
/* Runes                                                               */
/* ------------------------------------------------------------------ */

static void draw_symbol(int s, float cx, float cy, float sz, u32 c, float th)
{
    float h = sz * 0.5f;
    switch (s) {
    case SYM_VLINE:     line(cx, cy - h, cx, cy + h, th, c); break;
    case SYM_HLINE:     line(cx - h, cy, cx + h, cy, th, c); break;
    case SYM_SLASH:     line(cx - h, cy + h, cx + h, cy - h, th, c); break;
    case SYM_BACKSLASH: line(cx - h, cy - h, cx + h, cy + h, th, c); break;
    case SYM_TRIANGLE: {
        float ax = cx, ay = cy - h;
        float bx = cx + h, by = cy + h * 0.8f;
        float dx = cx - h, dy = cy + h * 0.8f;
        line(ax, ay, bx, by, th, c);
        line(bx, by, dx, dy, th, c);
        line(dx, dy, ax, ay, th, c);
        break;
    }
    case SYM_SQUARE:
        box_outline(cx - h * 0.85f, cy - h * 0.85f, h * 1.7f, h * 1.7f, th, c);
        break;
    case SYM_CIRCLE:
        ring(cx, cy, h * 0.95f, th, c);
        break;
    case SYM_ZIGZAG:
        line(cx - h, cy - h * 0.8f, cx + h, cy - h * 0.8f, th, c);
        line(cx + h, cy - h * 0.8f, cx - h, cy + h * 0.8f, th, c);
        line(cx - h, cy + h * 0.8f, cx + h, cy + h * 0.8f, th, c);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Characters                                                          */
/* ------------------------------------------------------------------ */

static float body_height(EnemyType t)
{
    switch (t) {
    case ET_IMP:    return 30.0f;
    case ET_GOBLIN: return 42.0f;
    case ET_OGRE:   return 58.0f;
    case ET_WRAITH: return 48.0f;
    default:        return 84.0f;
    }
}

static void draw_enemy(const Enemy *e)
{
    const uint8_t *col = ENEMY_COLORS[e->type];
    float white = e->flash > 0.0f ? e->flash / 0.18f : 0.0f;
    u32 body = mix(col, white, 255);
    u32 dark = rgba(col[0] / 2, col[1] / 2, col[2] / 2, 255);
    u32 eye = rgba(255, 255, 255, 255);
    u32 pupil = rgba(20, 20, 30, 255);
    float bob = sinf(e->bob) * 2.0f;
    float x = e->x, y = e->y;

    disc(x, y + 1.0f, 12.0f, rgba(0, 0, 0, 70));   /* shadow */

    switch (e->type) {
    case ET_IMP: {
        float cy = y - 13.0f + bob;
        tri(x - 9, cy - 6, x - 3, cy - 9, x - 10, cy - 20, dark);
        tri(x + 9, cy - 6, x + 3, cy - 9, x + 10, cy - 20, dark);
        disc(x, cy, 12.0f, body);
        disc(x - 4.5f, cy - 2, 3.0f, eye);
        disc(x + 4.5f, cy - 2, 3.0f, eye);
        disc(x - 5.0f, cy - 2, 1.4f, pupil);
        disc(x + 4.0f, cy - 2, 1.4f, pupil);
        line(x - 5, cy + 5, x + 5, cy + 5, 2.0f, dark);
        break;
    }
    case ET_GOBLIN: {
        float cy = y - 26.0f + bob;
        rect(x - 8, cy + 6, 16, 16, dark);
        rect(x - 7, y - 6, 5, 6, dark);
        rect(x + 2, y - 6, 5, 6, dark);
        tri(x - 8, cy - 2, x - 18, cy - 6, x - 8, cy - 9, body);
        tri(x + 8, cy - 2, x + 18, cy - 6, x + 8, cy - 9, body);
        disc(x, cy, 10.0f, body);
        disc(x - 3.5f, cy - 1, 2.6f, eye);
        disc(x + 3.5f, cy - 1, 2.6f, eye);
        disc(x - 3.5f, cy - 1, 1.2f, pupil);
        disc(x + 3.5f, cy - 1, 1.2f, pupil);
        break;
    }
    case ET_OGRE: {
        float cy = y - 30.0f + bob;
        disc(x, cy + 4, 20.0f, body);
        rect(x - 12, y - 10, 9, 10, dark);
        rect(x + 3, y - 10, 9, 10, dark);
        disc(x, cy - 18, 11.0f, body);
        disc(x - 4, cy - 20, 2.8f, eye);
        disc(x + 4, cy - 20, 2.8f, eye);
        disc(x - 4, cy - 20, 1.3f, pupil);
        disc(x + 4, cy - 20, 1.3f, pupil);
        tri(x - 5, cy - 12, x - 2, cy - 7, x - 8, cy - 7, eye);
        tri(x + 5, cy - 12, x + 2, cy - 7, x + 8, cy - 7, eye);
        line(x - 14, cy + 2, x - 24, cy + 14, 4.0f, dark);
        break;
    }
    case ET_WRAITH: {
        float cy = y - 30.0f + bob * 2.0f;
        u32 ghost = mix(col, white, 215);
        disc(x, cy - 6, 13.0f, ghost);
        rect(x - 13, cy - 6, 26, 22, ghost);
        for (int i = 0; i < 3; i++) {
            float sx = x - 13 + i * 9.0f;
            tri(sx, cy + 16, sx + 9.0f, cy + 16, sx + 4.5f, cy + 26 + sinf(e->bob + (float)i) * 3.0f, ghost);
        }
        disc(x - 5, cy - 7, 3.0f, rgba(20, 30, 60, 255));
        disc(x + 5, cy - 7, 3.0f, rgba(20, 30, 60, 255));
        break;
    }
    default: { /* boss */
        float cy = y - 44.0f + bob;
        u32 gold = mix(col, white, 255);
        disc(x, cy + 6, 32.0f, body);
        rect(x - 20, y - 14, 14, 14, dark);
        rect(x + 6, y - 14, 14, 14, dark);
        tri(x - 22, cy - 22, x - 14, cy - 40, x - 6, cy - 22, gold);
        tri(x - 8, cy - 24, x, cy - 46, x + 8, cy - 24, gold);
        tri(x + 6, cy - 22, x + 14, cy - 40, x + 22, cy - 22, gold);
        disc(x - 10, cy - 2, 6.0f, eye);
        disc(x + 10, cy - 2, 6.0f, eye);
        disc(x - 10, cy - 2, 3.0f, rgba(200, 30, 30, 255));
        disc(x + 10, cy - 2, 3.0f, rgba(200, 30, 30, 255));
        line(x - 12, cy + 16, x + 12, cy + 16, 3.0f, dark);
        break;
    }
    }
}

static void draw_enemy_runes(const Enemy *e)
{
    int rem = e->len - e->prog;
    float sz = e->type == ET_BOSS ? 22.0f : 18.0f;
    float gap = sz + 7.0f;
    float total = (float)rem * gap;
    float px = e->x - total * 0.5f;
    float py = e->y - body_height(e->type) - 18.0f;

    if (px < 4.0f) px = 4.0f;
    if (px + total > TOP_W - 4.0f) px = TOP_W - 4.0f - total;

    rect(px - 2, py - sz * 0.5f - 4, total + 4, sz + 8, rgba(10, 10, 25, 170));
    for (int k = 0; k < rem; k++) {
        int s = e->seq[e->prog + k];
        float cx = px + gap * 0.5f + (float)k * gap;
        if (k == 0) {
            draw_symbol(s, cx, py, sz, sym_col(s, 255), 3.0f);
            line(cx - sz * 0.5f, py + sz * 0.5f + 2, cx + sz * 0.5f, py + sz * 0.5f + 2, 2.0f,
                 rgba(255, 220, 90, 255));
        } else {
            draw_symbol(s, cx, py, sz * 0.8f, sym_col(s, 140), 2.0f);
        }
    }
}

static void draw_wizard(const Game *g)
{
    float x = PLAYER_X, y = PLAYER_Y;
    float cast = g->cast_timer > 0.7f ? 1.0f : 0.0f;

    disc(x, y + 1.0f, 15.0f, rgba(0, 0, 0, 80));
    tri(x - 15, y, x + 15, y, x, y - 36, rgba(70, 90, 200, 255));
    tri(x - 8, y, x + 8, y, x, y - 28, rgba(90, 115, 225, 255));
    disc(x, y - 42, 7.5f, rgba(245, 205, 170, 255));
    tri(x - 11, y - 45, x + 11, y - 45, x + 3, y - 70, rgba(60, 60, 170, 255));
    rect(x - 12, y - 47, 24, 4, rgba(60, 60, 170, 255));
    line(x + 10, y - 22, x + 28, y - 36, 3.0f, rgba(120, 80, 40, 255));
    disc(x + 29, y - 37, cast > 0.0f ? 6.0f : 3.5f,
         cast > 0.0f ? rgba(255, 255, 160, 255) : rgba(160, 220, 255, 255));
}

/* ------------------------------------------------------------------ */
/* Top screen                                                          */
/* ------------------------------------------------------------------ */

static void draw_background(const Game *g)
{
    u32 sky_top = rgba(18, 14, 48, 255);
    u32 sky_bot = rgba(92, 54, 110, 255);
    C2D_DrawRectangle(0, 0, Z, TOP_W, 150, sky_top, sky_top, sky_bot, sky_bot);

    for (int i = 0; i < 28; i++) {
        float x = (float)((i * 73) % TOP_W);
        float y = (float)((i * 37) % 110);
        int a = 120 + (int)(110.0f * sinf(g->time * 1.5f + (float)i));
        rect(x, y, 2, 2, rgba(255, 255, 255, a));
    }

    u32 far_m = rgba(46, 32, 82, 255);
    tri(-40, 150, 70, 70, 180, 150, far_m);
    tri(120, 150, 250, 50, 380, 150, far_m);
    tri(300, 150, 400, 85, 480, 150, far_m);

    rect(0, 150, TOP_W, 90, rgba(34, 52, 44, 255));
    rect(0, 150, TOP_W, 3, rgba(70, 110, 80, 255));
    for (int i = 0; i < 14; i++) {
        float x = (float)((i * 59) % TOP_W);
        float y = 160.0f + (float)((i * 23) % 70);
        line(x, y, x + 10, y, 1.0f, rgba(52, 78, 62, 255));
    }
}

static void draw_heart(float x, float y, u32 c)
{
    disc(x - 3.5f, y - 2.0f, 4.0f, c);
    disc(x + 3.5f, y - 2.0f, 4.0f, c);
    tri(x - 7.2f, y - 0.5f, x + 7.2f, y - 0.5f, x, y + 8.0f, c);
}

static void draw_hud(const Game *g)
{
    for (int i = 0; i < g->max_hp; i++)
        draw_heart(14.0f + (float)i * 18.0f, 14.0f,
                   i < g->hp ? rgba(235, 60, 80, 255) : rgba(70, 40, 60, 255));
    for (int i = 0; i < g->shield; i++) {
        disc(14.0f + (float)i * 18.0f, 33.0f, 6.0f, rgba(110, 170, 255, 255));
        ring(14.0f + (float)i * 18.0f, 33.0f, 6.0f, 1.5f, rgba(220, 240, 255, 255));
    }

    textf(TOP_W - 8, 2, 0.55f, rgba(255, 255, 255, 255), AL_RIGHT, "Score %d", g->score);
    textf(TOP_W - 8, 17, 0.42f, rgba(200, 200, 230, 255), AL_RIGHT, "Best %d", g->high_score);
    textf(TOP_W * 0.5f, 2, 0.6f, rgba(255, 220, 120, 255), AL_CENTER, "Wave %d", g->wave);
    if (g->combo >= 2)
        textf(TOP_W * 0.5f, 20, 0.5f, rgba(255, 160, 60, 255), AL_CENTER, "Combo x%d", g->combo);
}

static void draw_scene(const Game *g)
{
    int order[MAX_ENEMIES], n = 0;

    draw_background(g);

    for (int i = 0; i < MAX_ENEMIES; i++)
        if (g->en[i].alive) order[n++] = i;
    for (int a = 1; a < n; a++) {                   /* painter's order by y */
        int key = order[a], b = a - 1;
        while (b >= 0 && g->en[order[b]].y > g->en[key].y) { order[b + 1] = order[b]; b--; }
        order[b + 1] = key;
    }
    for (int a = 0; a < n; a++) draw_enemy(&g->en[order[a]]);
    draw_wizard(g);
    for (int a = 0; a < n; a++)
        if (g->en[order[a]].x < TOP_W + 10) draw_enemy_runes(&g->en[order[a]]);

    for (int i = 0; i < MAX_PROJ; i++) {
        const Projectile *p = &g->pr[i];
        if (p->dur <= 0.0f || p->t >= p->dur) continue;
        float f = p->t / p->dur;
        float x = p->x0 + (p->x1 - p->x0) * f;
        float y = p->y0 + (p->y1 - p->y0) * f;
        if (p->sym < 0) {
            line(p->x0, p->y0, x, y, 3.0f, rgba(255, 245, 140, 230));
        } else {
            disc(x, y, 7.0f, sym_col(p->sym, 90));
            disc(x, y, 4.0f, sym_col(p->sym, 255));
        }
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *p = &g->pt[i];
        if (p->life <= 0.0f) continue;
        int a = (int)(255.0f * p->life / p->maxlife);
        rect(p->x - p->size * 0.5f, p->y - p->size * 0.5f, p->size, p->size, rgba(p->r, p->g, p->b, a));
    }

    for (int i = 0; i < MAX_POPUPS; i++) {
        const Popup *p = &g->pu[i];
        if (p->life <= 0.0f) continue;
        int a = (int)(255.0f * (p->life > 1.0f ? 1.0f : p->life));
        text(p->x, p->y, 0.5f, rgba(p->r, p->g, p->b, a), AL_CENTER, p->text);
    }

    if (g->freeze_timer > 0.0f) rect(0, 0, TOP_W, TOP_H, rgba(140, 220, 255, 60));
    if (g->hurt_flash > 0.0f)
        rect(0, 0, TOP_W, TOP_H, rgba(255, 30, 30, (int)(160.0f * g->hurt_flash / 0.35f)));
}

static void draw_top(const Game *g)
{
    draw_scene(g);

    switch (g->state) {
    case ST_PLAY:
        draw_hud(g);
        if (g->wave_delay > 0.0f) {
            float a = g->wave_delay > 0.4f ? 255.0f : g->wave_delay / 0.4f * 255.0f;
            textf(TOP_W * 0.5f, 62, 1.2f, rgba(255, 230, 130, (int)a), AL_CENTER, "WAVE %d", g->wave);
            if (g->wave % 5 == 0)
                text(TOP_W * 0.5f, 112, 0.65f, rgba(255, 110, 110, (int)a), AL_CENTER, "BOSS INCOMING!");
            else if (g->new_rune >= 0)
                textf(TOP_W * 0.5f, 112, 0.6f, rgba(150, 255, 190, (int)a), AL_CENTER,
                      "New rune unlocked: %s", shapes_name((Symbol)g->new_rune));
        }
        break;
    case ST_UPGRADE:
        draw_hud(g);
        rect(0, 0, TOP_W, TOP_H, rgba(0, 0, 20, 120));
        textf(TOP_W * 0.5f, 60, 1.0f, rgba(130, 255, 160, 255), AL_CENTER, "WAVE %d CLEARED", g->wave);
        text(TOP_W * 0.5f, 110, 0.55f, rgba(230, 230, 255, 255), AL_CENTER,
             "Choose an upgrade on the bottom screen");
        break;
    case ST_TITLE: {
        rect(0, 0, TOP_W, TOP_H, rgba(0, 0, 20, 110));
        text(TOP_W * 0.5f, 38, 1.5f, rgba(255, 215, 100, 255), AL_CENTER, "SIGIL SIEGE");
        text(TOP_W * 0.5f, 96, 0.6f, rgba(220, 220, 255, 255), AL_CENTER, "Draw the runes. Slay the horde.");
        for (int i = 0; i < 3; i++)
            draw_symbol(i, 130.0f + (float)i * 70.0f, 150.0f, 34.0f, sym_col(i, 255), 3.0f);
        if (((int)(g->time * 2.0f)) % 2 == 0)
            text(TOP_W * 0.5f, 188, 0.55f, rgba(255, 255, 255, 255), AL_CENTER,
                 "Touch the bottom screen to begin");
        textf(TOP_W * 0.5f, 214, 0.42f, rgba(180, 180, 220, 255), AL_CENTER,
              "Best score: %d      START = quit", g->high_score);
        break;
    }
    case ST_GAMEOVER:
        rect(0, 0, TOP_W, TOP_H, rgba(40, 0, 0, 150));
        text(TOP_W * 0.5f, 38, 1.4f, rgba(255, 90, 90, 255), AL_CENTER, "DEFEATED");
        textf(TOP_W * 0.5f, 100, 0.7f, rgba(255, 255, 255, 255), AL_CENTER,
              "You fell on wave %d", g->wave);
        textf(TOP_W * 0.5f, 128, 0.6f, rgba(255, 230, 130, 255), AL_CENTER,
              "Score %d   Kills %d   Best combo %d", g->score, g->kills, g->best_combo);
        if (g->new_record)
            text(TOP_W * 0.5f, 160, 0.7f, rgba(130, 255, 160, 255), AL_CENTER, "NEW RECORD!");
        else
            textf(TOP_W * 0.5f, 160, 0.5f, rgba(200, 200, 230, 255), AL_CENTER, "Best %d", g->high_score);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Bottom screen                                                       */
/* ------------------------------------------------------------------ */

static void draw_legend(const Game *g)
{
    int pool = game_pool_size(g->state == ST_TITLE ? 1 : g->wave);
    float cell = 36.0f;
    float x0 = BOT_W * 0.5f - (float)pool * cell * 0.5f;

    rect(0, 196, BOT_W, 44, rgba(10, 10, 30, 200));
    for (int i = 0; i < pool; i++) {
        float cx = x0 + cell * 0.5f + (float)i * cell;
        draw_symbol(i, cx, 212.0f, 18.0f, sym_col(i, 255), 2.0f);
        text(cx, 222.0f, 0.3f, rgba(190, 190, 220, 255), AL_CENTER, shapes_short_name((Symbol)i));
    }
}

static void draw_stroke(const Game *g)
{
    if (g->stroke_n < 2) return;
    u32 c;
    if (g->drawing) {
        c = rgba(240, 250, 255, 255);
    } else {
        if (g->stroke_fade <= 0.0f) return;
        int a = (int)(255.0f * g->stroke_fade / 0.4f);
        if (g->last_result == RES_HIT) c = rgba(120, 255, 150, a);
        else if (g->last_result == RES_FIZZLE) c = rgba(255, 90, 90, a);
        else c = rgba(255, 220, 120, a);
    }
    for (int i = 1; i < g->stroke_n; i++)
        line(g->stroke[i - 1].x, g->stroke[i - 1].y, g->stroke[i].x, g->stroke[i].y, 4.0f, c);
}

static void draw_cards(const Game *g)
{
    for (int i = 0; i < g->n_choices; i++) {
        const UpgradeInfo *u = &UPGRADE_INFO[g->choice[i]];
        float y = (float)(UPG_CARD_Y0 + i * UPG_CARD_DY);
        int sel = (i == g->cursor);
        u32 edge = rgba(u->r, u->g, u->b, 255);

        rect(UPG_CARD_X, y, UPG_CARD_W, UPG_CARD_H, rgba(24, 24, 52, 255));
        box_outline(UPG_CARD_X, y, UPG_CARD_W, UPG_CARD_H, sel ? 4.0f : 2.0f, sel ? rgba(255, 255, 255, 255) : edge);
        rect(UPG_CARD_X, y, 8, UPG_CARD_H, edge);
        text(UPG_CARD_X + 18, y + 6, 0.62f, edge, AL_LEFT, u->name);
        text(UPG_CARD_X + 18, y + 30, 0.42f, rgba(225, 225, 245, 255), AL_LEFT, u->desc);

        int lvl = g->up_level[g->choice[i]];
        if (g->choice[i] == UP_HEAL) {
            text(UPG_CARD_X + 18, y + 49, 0.38f, rgba(160, 160, 200, 255), AL_LEFT, "Instant");
        } else {
            for (int k = 0; k < u->max_level && k < 8; k++)
                disc(UPG_CARD_X + 24.0f + (float)k * 12.0f, y + 56.0f, 4.0f,
                     k < lvl ? edge : (k == lvl ? rgba(255, 255, 255, 255) : rgba(60, 60, 90, 255)));
            textf(UPG_CARD_X + UPG_CARD_W - 8, y + 47, 0.38f, rgba(160, 160, 200, 255), AL_RIGHT,
                  "Level %d -> %d", lvl, lvl + 1);
        }
    }
}

static void draw_bottom(const Game *g)
{
    u32 top = rgba(14, 12, 36, 255), bot = rgba(30, 22, 60, 255);
    C2D_DrawRectangle(0, 0, Z, BOT_W, BOT_H, top, top, bot, bot);

    switch (g->state) {
    case ST_PLAY: {
        box_outline(3, 3, BOT_W - 6, 190, 2.0f, rgba(70, 60, 130, 255));
        if (g->stroke_n == 0 && !g->drawing)
            text(BOT_W * 0.5f, 80, 0.55f, rgba(110, 100, 170, 255), AL_CENTER, "Draw runes here");
        draw_stroke(g);

        if (g->cast_timer > 0.0f) {
            int a = (int)(255.0f * (g->cast_timer > 0.5f ? 1.0f : g->cast_timer / 0.5f));
            if (g->last_result == RES_HIT)
                textf(BOT_W * 0.5f, 8, 0.7f, sym_col(g->last_sym, a), AL_CENTER, "%s!", shapes_name(g->last_sym));
            else if (g->last_result == RES_FIZZLE)
                textf(BOT_W * 0.5f, 8, 0.6f, rgba(255, 100, 100, a), AL_CENTER, "%s - fizzle!", shapes_name(g->last_sym));
            else if (g->last_result == RES_UNKNOWN)
                text(BOT_W * 0.5f, 8, 0.6f, rgba(255, 220, 120, a), AL_CENTER, "Not a rune - try again");
        }
        draw_legend(g);
        break;
    }
    case ST_UPGRADE:
        draw_cards(g);
        break;
    case ST_TITLE:
        text(BOT_W * 0.5f, 18, 0.7f, rgba(255, 215, 100, 255), AL_CENTER, "How to play");
        text(BOT_W * 0.5f, 56, 0.45f, rgba(230, 230, 255, 255), AL_CENTER, "Enemies carry runes above their heads.");
        text(BOT_W * 0.5f, 78, 0.45f, rgba(230, 230, 255, 255), AL_CENTER, "Draw the first rune with your stylus");
        text(BOT_W * 0.5f, 100, 0.45f, rgba(230, 230, 255, 255), AL_CENTER, "to strike. Clear the whole chain to win.");
        text(BOT_W * 0.5f, 130, 0.45f, rgba(255, 190, 120, 255), AL_CENTER, "Draw each rune in ONE stroke.");
        text(BOT_W * 0.5f, 152, 0.45f, rgba(160, 200, 255, 255), AL_CENTER, "Don't let them reach you!");
        draw_legend(g);
        break;
    case ST_GAMEOVER:
        text(BOT_W * 0.5f, 90, 0.7f, rgba(255, 255, 255, 255), AL_CENTER, "Touch to continue");
        break;
    }

    if (g->state == ST_PLAY && g->drawing) {
        /* finger indicator is the stroke itself; nothing extra */
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void render_init(void)
{
    s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_buf = C2D_TextBufNew(4096);
}

void render_exit(void)
{
    C2D_TextBufDelete(s_buf);
}

void render_frame(const Game *g)
{
    C2D_TextBufClear(s_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(s_top, rgba(0, 0, 0, 255));
    C2D_SceneBegin(s_top);
    draw_top(g);

    C2D_TargetClear(s_bot, rgba(0, 0, 0, 255));
    C2D_SceneBegin(s_bot);
    draw_bottom(g);

    C3D_FrameEnd(0);
}
