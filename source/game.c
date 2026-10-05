#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Static data                                                         */
/* ------------------------------------------------------------------ */

const uint8_t SYM_COLORS[SYM_COUNT][3] = {
    { 120, 200, 255 },  /* vline     */
    { 255, 210,  90 },  /* hline     */
    { 255, 120, 120 },  /* triangle  */
    { 130, 255, 160 },  /* circle    */
    { 200, 140, 255 },  /* square    */
    { 255, 160,  70 },  /* slash     */
    {  90, 230, 230 },  /* backslash */
    { 255, 255, 255 },  /* zigzag    */
};

const uint8_t ENEMY_COLORS[ET_COUNT][3] = {
    { 235,  90,  70 },  /* imp    */
    { 110, 200,  90 },  /* goblin */
    { 190, 130,  80 },  /* ogre   */
    { 120, 220, 240 },  /* wraith */
    { 240, 190,  60 },  /* boss   */
};

const EnemyDef ENEMY_DEFS[ET_COUNT] = {
    { "Imp",    1, 1, 30.0f,  10 },
    { "Goblin", 2, 2, 24.0f,  25 },
    { "Ogre",   3, 3, 16.0f,  50 },
    { "Wraith", 2, 2, 46.0f,  40 },
    { "Boss",   5, 6, 11.0f, 300 },
};

const UpgradeInfo UPGRADE_INFO[UP_COUNT] = {
    { "Heart Crystal",   "+1 max HP and heal 1",                  5, 255,  90, 110 },
    { "Frost Aura",      "Enemies walk 10% slower",               3, 140, 210, 255 },
    { "Chain Lightning", "Kills zap the next rune off a nearby foe", 2, 255, 235, 100 },
    { "Resonance",       "Spells also hit +1 foe with same rune", 2, 200, 140, 255 },
    { "Frost Nova",      "Kills freeze all enemies briefly",      3, 120, 235, 255 },
    { "Healing Draught", "Restore 2 HP now",                     99, 120, 255, 140 },
    { "Soul Siphon",     "Heal 1 HP every few kills",             3, 255, 120, 200 },
    { "Rune Shield",     "Block 1 hit per wave (+1 per level)",   3,  90, 160, 255 },
    { "Steady Hand",     "Wrong runes never push enemies forward", 1, 255, 190, 120 },
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int imin(int a, int b) { return a < b ? a : b; }

static uint32_t rnd32(Game *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static float rndf(Game *g) { return (float)(rnd32(g) >> 8) * (1.0f / 16777216.0f); }
static int   rndi(Game *g, int n) { return (int)(rnd32(g) % (uint32_t)n); }

int game_pool_size(int wave)
{
    int n = 2 + (wave + 1) / 2;
    return n > SYM_COUNT ? SYM_COUNT : n;
}

int game_enemy_count(int wave)
{
    int n = 5 + 3 * wave;
    if (wave % 5 == 0) n = n / 2 + 1;   /* boss waves: boss + fewer minions */
    return imin(n, 70);
}

static float spawn_interval(int wave)
{
    float v = 2.0f - 0.1f * (float)wave;
    return v < 0.4f ? 0.4f : v;
}

static void popup(Game *g, float x, float y, uint8_t r, uint8_t gr, uint8_t b, const char *text)
{
    Popup *p = NULL;
    for (int i = 0; i < MAX_POPUPS; i++)
        if (g->pu[i].life <= 0.0f) { p = &g->pu[i]; break; }
    if (!p) return;
    p->x = x; p->y = y; p->life = 1.0f;
    p->r = r; p->g = gr; p->b = b;
    snprintf(p->text, sizeof p->text, "%s", text);
}

static void burst(Game *g, float x, float y, const uint8_t col[3], int count, float speed)
{
    for (int k = 0; k < count; k++) {
        Particle *p = NULL;
        for (int i = 0; i < MAX_PARTICLES; i++)
            if (g->pt[i].life <= 0.0f) { p = &g->pt[i]; break; }
        if (!p) return;
        float a = rndf(g) * 6.2831853f;
        float s = speed * (0.3f + 0.7f * rndf(g));
        p->x = x; p->y = y;
        p->vx = cosf(a) * s;
        p->vy = sinf(a) * s - 25.0f;
        p->maxlife = p->life = 0.4f + 0.5f * rndf(g);
        p->size = 2.0f + 3.0f * rndf(g);
        p->r = col[0]; p->g = col[1]; p->b = col[2];
    }
}

static void add_proj(Game *g, float x0, float y0, float x1, float y1, int sym)
{
    for (int i = 0; i < MAX_PROJ; i++) {
        Projectile *p = &g->pr[i];
        if (p->dur <= 0.0f || p->t >= p->dur) {
            p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
            p->t = 0.0f; p->dur = 0.16f; p->sym = sym;
            return;
        }
    }
}

static int count_alive(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) n += g->en[i].alive;
    return n;
}

/* Nearest on-screen enemy (smallest x), or -1. */
static int nearest_enemy(const Game *g, int exclude)
{
    int best = -1;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->en[i];
        if (!e->alive || i == exclude || e->x >= TOP_W + 8) continue;
        if (best < 0 || e->x < g->en[best].x) best = i;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Run / wave flow                                                     */
/* ------------------------------------------------------------------ */

static void start_wave(Game *g)
{
    g->spawn_left = game_enemy_count(g->wave);
    g->boss_pending = (g->wave % 5 == 0);
    g->spawn_timer = 0.2f;
    g->wave_delay = 2.0f;
    g->shield = g->up_level[UP_SHIELD];
    g->freeze_timer = 0.0f;
    g->new_rune = (g->wave > 1 && game_pool_size(g->wave) > game_pool_size(g->wave - 1))
                      ? game_pool_size(g->wave) - 1 : -1;
    for (int i = 0; i < MAX_PROJ; i++) g->pr[i].dur = 0.0f;
    g->state = ST_PLAY;
}

static void reset_run(Game *g)
{
    uint32_t rng = g->rng;
    int hs = g->high_score;
    memset(g, 0, sizeof *g);
    g->rng = rng ? rng : 1u;
    g->high_score = hs;
    g->hp = g->max_hp = 5;
    g->wave = 1;
    g->speed_mult = 1.0f;
    g->new_rune = -1;
    g->last_sym = SYM_NONE;
    start_wave(g);
}

void game_init(Game *g, uint32_t seed, int high_score)
{
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 0xA341316Cu;
    g->high_score = high_score;
    g->state = ST_TITLE;
    g->ui_lock = 0.4f;
    g->speed_mult = 1.0f;
    g->new_rune = -1;
    g->last_sym = SYM_NONE;
}

static void end_run(Game *g)
{
    g->state = ST_GAMEOVER;
    g->ui_lock = 1.2f;
    if (g->score > g->high_score) {
        g->high_score = g->score;
        g->new_record = 1;
    }
}

static void wave_clear(Game *g)
{
    int elig[UP_COUNT], ne = 0;

    g->score += 50 * g->wave;
    if (g->hp < g->max_hp) g->hp++;

    for (int u = 0; u < UP_COUNT; u++) {
        if (g->up_level[u] >= UPGRADE_INFO[u].max_level) continue;
        if (u == UP_HEAL && g->hp >= g->max_hp) continue;
        elig[ne++] = u;
    }
    for (int i = ne - 1; i > 0; i--) {          /* Fisher-Yates */
        int j = rndi(g, i + 1);
        int t = elig[i]; elig[i] = elig[j]; elig[j] = t;
    }
    g->n_choices = imin(UPG_CHOICES, ne);
    for (int i = 0; i < g->n_choices; i++) g->choice[i] = elig[i];
    g->cursor = 0;
    g->ui_lock = 0.6f;

    if (g->n_choices == 0) {
        g->wave++;
        start_wave(g);
    } else {
        g->state = ST_UPGRADE;
    }
}

int game_pick_upgrade(Game *g, int idx)
{
    if (g->state != ST_UPGRADE || idx < 0 || idx >= g->n_choices) return 0;
    int u = g->choice[idx];

    switch (u) {
    case UP_HEART:
        g->max_hp++;
        g->hp++;
        break;
    case UP_FROST:
        g->speed_mult *= 0.90f;
        break;
    case UP_HEAL:
        g->hp = imin(g->max_hp, g->hp + 2);
        break;
    default:
        break;   /* levels alone drive the other upgrades */
    }
    g->up_level[u]++;
    g->wave++;
    start_wave(g);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Combat                                                              */
/* ------------------------------------------------------------------ */

static void damage_enemy(Game *g, int i, int allow_chain);

static void kill_enemy(Game *g, int i, int allow_chain)
{
    Enemy *e = &g->en[i];
    char buf[24];

    e->alive = 0;
    burst(g, e->x, e->y - 14.0f, ENEMY_COLORS[e->type], e->type == ET_BOSS ? 40 : 16, 110.0f);

    int pts = ENEMY_DEFS[e->type].score * (10 + imin(g->combo, 40)) / 10;
    g->score += pts;
    g->kills++;
    snprintf(buf, sizeof buf, "+%d", pts);
    popup(g, e->x, e->y - 50.0f, 255, 235, 120, buf);

    if (g->up_level[UP_NOVA] > 0) {
        if (g->freeze_timer <= 0.0f)
            g->freeze_timer = 0.2f * (float)g->up_level[UP_NOVA];
    }

    if (g->up_level[UP_VAMPIRE] > 0) {
        int need = 10 - 2 * g->up_level[UP_VAMPIRE];
        if (++g->vamp_count >= need) {
            g->vamp_count = 0;
            if (g->hp < g->max_hp) {
                g->hp++;
                popup(g, PLAYER_X, PLAYER_Y - 80.0f, 255, 120, 200, "+1 HP");
            }
        }
    }

    if (allow_chain && g->up_level[UP_CHAIN] > 0) {
        int j = nearest_enemy(g, i);
        if (j >= 0) {
            add_proj(g, e->x, e->y - 18.0f, g->en[j].x, g->en[j].y - 18.0f, -1);
            for (int k = 0; k < g->up_level[UP_CHAIN]; k++)
                if (g->en[j].alive) damage_enemy(g, j, 0);
        }
    }
}

static void damage_enemy(Game *g, int i, int allow_chain)
{
    Enemy *e = &g->en[i];
    e->prog++;
    e->flash = 0.18f;
    if (e->prog >= e->len) kill_enemy(g, i, allow_chain);
}

static void enemy_reaches_player(Game *g, int i)
{
    Enemy *e = &g->en[i];
    int dmg = (e->type == ET_BOSS) ? 3 : 1;

    e->alive = 0;
    burst(g, e->x, e->y - 14.0f, ENEMY_COLORS[e->type], 14, 90.0f);
    g->combo = 0;

    if (g->shield > 0) {
        g->shield--;
        popup(g, PLAYER_X, PLAYER_Y - 80.0f, 140, 190, 255, "BLOCKED");
        return;
    }
    g->hp -= dmg;
    g->hurt_flash = 0.35f;
    if (g->hp <= 0) {
        g->hp = 0;
        end_run(g);
    }
}

static int spawn_enemy(Game *g)
{
    int slot = -1;
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (!g->en[i].alive) { slot = i; break; }
    if (slot < 0) return 0;

    Enemy *e = &g->en[slot];
    int w = g->wave;
    EnemyType t;
    memset(e, 0, sizeof *e);

    if (g->boss_pending) {
        t = ET_BOSS;
        g->boss_pending = 0;
    } else {
        int r = rndi(g, 100);
        int p_gob = imin(40, 15 + 5 * w);
        int p_ogre = w >= 3 ? imin(30, 5 * (w - 2)) : 0;
        int p_wra = w >= 4 ? imin(25, 4 * (w - 3)) : 0;
        if (r < p_wra) t = ET_WRAITH;
        else if (r < p_wra + p_ogre) t = ET_OGRE;
        else if (r < p_wra + p_ogre + p_gob) t = ET_GOBLIN;
        else t = ET_IMP;
    }

    const EnemyDef *d = &ENEMY_DEFS[t];
    int pool = game_pool_size(w);
    e->type = t;
    e->alive = 1;
    e->len = d->len_min + rndi(g, d->len_max - d->len_min + 1) + (t == ET_BOSS ? 0 : w / 10);
    if (e->len > MAX_SEQ) e->len = MAX_SEQ;
    for (int k = 0; k < e->len; k++) {
        int s = rndi(g, pool);
        if (k > 0 && s == e->seq[k - 1]) s = rndi(g, pool);   /* fewer repeats */
        e->seq[k] = s;
    }
    e->x = TOP_W + 30.0f;
    e->y = LANE_MIN + rndf(g) * (LANE_MAX - LANE_MIN);
    float scale = 1.0f + 0.07f * (float)(w - 1);
    if (scale > 3.0f) scale = 3.0f;
    e->speed = d->speed * scale * (0.92f + 0.16f * rndf(g));
    e->bob = rndf(g) * 6.28f;
    return 1;
}

void game_stroke(Game *g, const Pt *pts, int n)
{
    if (g->state != ST_PLAY) return;

    float len = 0.0f;
    for (int i = 1; i < n; i++) {
        float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
        len += sqrtf(dx * dx + dy * dy);
    }
    if (len < 22.0f) return;                 /* a tap, not a rune */

    Symbol s = shapes_recognize(pts, n);
    g->last_sym = s;
    g->cast_timer = 1.0f;
    g->stroke_fade = 0.4f;

    if (s == SYM_NONE) {
        g->last_result = RES_UNKNOWN;        /* recognizer miss: no penalty */
        return;
    }

    int idx[MAX_ENEMIES], m = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->en[i];
        if (e->alive && e->x < TOP_W + 8 && e->seq[e->prog] == (int)s) idx[m++] = i;
    }
    for (int a = 1; a < m; a++) {            /* nearest first */
        int key = idx[a], b = a - 1;
        while (b >= 0 && g->en[idx[b]].x > g->en[key].x) { idx[b + 1] = idx[b]; b--; }
        idx[b + 1] = key;
    }

    if (m == 0) {
        g->last_result = RES_FIZZLE;
        if (g->up_level[UP_GRACE] == 0) {
            g->combo = 0;
            int j = nearest_enemy(g, -1);
            if (j >= 0) g->en[j].x -= 14.0f;
        }
        return;
    }

    g->last_result = RES_HIT;
    int hits = 1 + g->up_level[UP_RESONANCE];
    int done = 0;
    for (int a = 0; a < m && done < hits; a++) {
        int i = idx[a];
        Enemy *e = &g->en[i];
        if (!e->alive || e->seq[e->prog] != (int)s) continue;   /* changed by a chain */
        add_proj(g, PLAYER_X + 16.0f, PLAYER_Y - 34.0f, e->x, e->y - 18.0f, (int)s);
        damage_enemy(g, i, 1);
        done++;
    }

    g->combo++;
    if (g->combo > g->best_combo) g->best_combo = g->combo;
    if (g->combo % 10 == 0) {
        char buf[24];
        snprintf(buf, sizeof buf, "COMBO x%d", g->combo);
        popup(g, TOP_W * 0.5f, 90.0f, 255, 160, 60, buf);
    }
}

/* ------------------------------------------------------------------ */
/* Per-frame update                                                    */
/* ------------------------------------------------------------------ */

static void update_fx(Game *g, float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &g->pt[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->vy += 220.0f * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
    }
    for (int i = 0; i < MAX_PROJ; i++) {
        Projectile *p = &g->pr[i];
        if (p->dur > 0.0f && p->t < p->dur) p->t += dt;
    }
    for (int i = 0; i < MAX_POPUPS; i++) {
        Popup *p = &g->pu[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->y -= 24.0f * dt;
    }
    if (g->stroke_fade > 0.0f) g->stroke_fade -= dt;
    if (g->cast_timer > 0.0f)  g->cast_timer -= dt;
    if (g->hurt_flash > 0.0f)  g->hurt_flash -= dt;
    if (g->ui_lock > 0.0f)     g->ui_lock -= dt;
}

static void update_play(Game *g, float dt)
{
    if (g->wave_delay > 0.0f) {
        g->wave_delay -= dt;
        return;
    }

    if (g->spawn_left > 0) {
        g->spawn_timer -= dt;
        if (g->spawn_timer <= 0.0f && spawn_enemy(g)) {
            g->spawn_left--;
            g->spawn_timer = spawn_interval(g->wave) * (0.7f + 0.6f * rndf(g));
        }
    }

    float mv = 1.0f;
    if (g->freeze_timer > 0.0f) {
        g->freeze_timer -= dt;
        mv = 0.0f;
    }

    for (int i = 0; i < MAX_ENEMIES && g->state == ST_PLAY; i++) {
        Enemy *e = &g->en[i];
        if (!e->alive) continue;
        e->bob += dt * 6.0f;
        if (e->flash > 0.0f) e->flash -= dt;
        e->x -= e->speed * g->speed_mult * mv * dt;
        if (e->x <= REACH_X) enemy_reaches_player(g, i);
    }

    if (g->state == ST_PLAY && g->spawn_left == 0 && count_alive(g) == 0)
        wave_clear(g);
}

void game_update(Game *g, float dt)
{
    if (dt > 0.1f) dt = 0.1f;
    g->time += dt;
    update_fx(g, dt);
    if (g->state == ST_PLAY) update_play(g, dt);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

static void confirm_generic(Game *g)
{
    if (g->ui_lock > 0.0f) return;
    if (g->state == ST_TITLE) {
        reset_run(g);
    } else if (g->state == ST_GAMEOVER) {
        g->state = ST_TITLE;
        g->ui_lock = 0.3f;
    } else if (g->state == ST_UPGRADE) {
        game_pick_upgrade(g, g->cursor);
    }
}

static void end_stroke(Game *g)
{
    switch (g->state) {
    case ST_PLAY:
        game_stroke(g, g->stroke, g->stroke_n);
        break;
    case ST_UPGRADE:
        if (g->ui_lock <= 0.0f && g->stroke_len < 20.0f && g->stroke_n > 0) {
            Pt p = g->stroke[g->stroke_n - 1];
            for (int i = 0; i < g->n_choices; i++) {
                int y = UPG_CARD_Y0 + i * UPG_CARD_DY;
                if (p.x >= UPG_CARD_X && p.x < UPG_CARD_X + UPG_CARD_W &&
                    p.y >= y && p.y < y + UPG_CARD_H) {
                    game_pick_upgrade(g, i);
                    break;
                }
            }
        }
        break;
    default:
        if (g->stroke_len < 40.0f) confirm_generic(g);
        break;
    }
}

void game_touch(Game *g, int touching, float x, float y)
{
    if (touching) {
        if (!g->drawing) {
            g->drawing = 1;
            g->stroke_n = 0;
            g->stroke_len = 0.0f;
        }
        int add = 1;
        if (g->stroke_n > 0) {
            Pt l = g->stroke[g->stroke_n - 1];
            float dx = x - l.x, dy = y - l.y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 1.5f) add = 0;
            else g->stroke_len += d;
        }
        if (add && g->stroke_n < MAX_STROKE) {
            g->stroke[g->stroke_n].x = x;
            g->stroke[g->stroke_n].y = y;
            g->stroke_n++;
        }
    } else if (g->drawing) {
        g->drawing = 0;
        end_stroke(g);
    }
}

void game_key(Game *g, GameKey k)
{
    if (g->state == ST_UPGRADE) {
        if (k == GK_UP && g->cursor > 0) g->cursor--;
        else if (k == GK_DOWN && g->cursor < g->n_choices - 1) g->cursor++;
        else if (k == GK_CONFIRM) confirm_generic(g);
    } else if (k == GK_CONFIRM) {
        confirm_generic(g);
    }
}
