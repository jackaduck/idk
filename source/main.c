#include <3ds.h>
#include <citro2d.h>

#include <stdio.h>
#include <sys/stat.h>

#include "game.h"
#include "render.h"

#define SAVE_DIR  "sdmc:/3ds/SigilSiege"
#define SAVE_PATH SAVE_DIR "/score.sav"

static Game g_game;

static int load_high(void)
{
    int v = 0;
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return 0;
    if (fread(&v, sizeof v, 1, f) != 1) v = 0;
    fclose(f);
    return v < 0 ? 0 : v;
}

static void save_high(int v)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);
    FILE *f = fopen(SAVE_PATH, "wb");
    if (!f) return;
    fwrite(&v, sizeof v, 1, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    osSetSpeedupEnable(true);   /* New 3DS: run at full clock; harmless on Old 3DS */
    gfxInitDefault();
    gfxSet3D(false);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    render_init();

    game_init(&g_game, (uint32_t)osGetTime() ^ 0x9E3779B9u, load_high());
    int saved_high = g_game.high_score;
    u64 last = osGetTime();

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown();
        u32 kh = hidKeysHeld();
        if (kd & KEY_START) break;

        touchPosition tp;
        hidTouchRead(&tp);
        game_touch(&g_game, (kh & KEY_TOUCH) != 0, (float)tp.px, (float)tp.py);

        if (kd & KEY_DUP)   game_key(&g_game, GK_UP);
        if (kd & KEY_DDOWN) game_key(&g_game, GK_DOWN);
        if (kd & KEY_A)     game_key(&g_game, GK_CONFIRM);

        u64 now = osGetTime();
        float dt = (float)(now - last) / 1000.0f;
        last = now;
        if (dt < 0.001f) dt = 0.001f;
        game_update(&g_game, dt);

        if (g_game.high_score != saved_high) {
            save_high(g_game.high_score);
            saved_high = g_game.high_score;
        }

        render_frame(&g_game);
    }

    render_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
