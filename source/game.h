#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "shapes.h"

/* Platform-independent game logic: no 3DS headers in here. */

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#define MAX_ENEMIES   40
#define MAX_SEQ       6
#define MAX_PARTICLES 160
#define MAX_PROJ      24
#define MAX_POPUPS    12
#define MAX_STROKE    512

#define PLAYER_X  56.0f
#define PLAYER_Y  196.0f
#define LANE_MIN  158.0f
#define LANE_MAX  214.0f
#define REACH_X   (PLAYER_X + 26.0f)

/* Upgrade-card layout on the bottom screen (shared by logic and renderer). */
#define UPG_CHOICES 3
#define UPG_CARD_X  10
#define UPG_CARD_W  300
#define UPG_CARD_H  68
#define UPG_CARD_Y0 8
#define UPG_CARD_DY 76

typedef enum { ST_TITLE, ST_PLAY, ST_UPGRADE, ST_GAMEOVER } GameState;

typedef enum { ET_IMP, ET_GOBLIN, ET_OGRE, ET_WRAITH, ET_BOSS, ET_COUNT } EnemyType;

typedef enum {
    UP_HEART, UP_FROST, UP_CHAIN, UP_RESONANCE, UP_NOVA,
    UP_HEAL, UP_VAMPIRE, UP_SHIELD, UP_GRACE, UP_COUNT
} UpgradeId;

typedef enum { RES_NONE, RES_HIT, RES_FIZZLE, RES_UNKNOWN } CastResult;

typedef enum { GK_UP, GK_DOWN, GK_CONFIRM } GameKey;

typedef struct {
    int       alive;
    EnemyType type;
    float     x, y, speed;
    int       seq[MAX_SEQ];
    int       len, prog;
    float     flash, bob;
} Enemy;

typedef struct {
    float   x, y, vx, vy, life, maxlife, size;
    uint8_t r, g, b;
} Particle;

typedef struct {
    float x0, y0, x1, y1, t, dur;
    int   sym;               /* -1 = chain lightning */
} Projectile;

typedef struct {
    float   x, y, life;
    char    text[24];
    uint8_t r, g, b;
} Popup;

typedef struct {
    const char *name;
    const char *desc;
    int         max_level;
    uint8_t     r, g, b;
} UpgradeInfo;

typedef struct {
    const char *name;
    int         len_min, len_max;
    float       speed;
    int         score;
} EnemyDef;

typedef struct {
    GameState state;
    uint32_t  rng;
    float     time;
    float     ui_lock;

    /* player */
    int   hp, max_hp, shield;
    int   wave, score, high_score, kills, combo, best_combo, new_record;
    int   up_level[UP_COUNT];
    float speed_mult, freeze_timer, hurt_flash;
    int   vamp_count;

    /* wave */
    int   spawn_left, boss_pending, new_rune;
    float spawn_timer, wave_delay;

    Enemy      en[MAX_ENEMIES];
    Particle   pt[MAX_PARTICLES];
    Projectile pr[MAX_PROJ];
    Popup      pu[MAX_POPUPS];

    /* upgrade screen */
    int choice[UPG_CHOICES], n_choices, cursor;

    /* touch drawing */
    Pt    stroke[MAX_STROKE];
    int   stroke_n, drawing;
    float stroke_len, stroke_fade;

    Symbol     last_sym;
    CastResult last_result;
    float      cast_timer;
} Game;

extern const uint8_t     SYM_COLORS[SYM_COUNT][3];
extern const uint8_t     ENEMY_COLORS[ET_COUNT][3];
extern const EnemyDef    ENEMY_DEFS[ET_COUNT];
extern const UpgradeInfo UPGRADE_INFO[UP_COUNT];

void game_init(Game *g, uint32_t seed, int high_score);
void game_update(Game *g, float dt);

/* Call once per frame with the current touch state. */
void game_touch(Game *g, int touching, float x, float y);
void game_key(Game *g, GameKey k);

/* Normally called internally when a stroke ends; exposed for tests/bots. */
void game_stroke(Game *g, const Pt *pts, int n);
int  game_pick_upgrade(Game *g, int idx);

int  game_pool_size(int wave);   /* how many runes are unlocked on this wave */
int  game_enemy_count(int wave);

#endif
