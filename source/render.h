#ifndef RENDER_H
#define RENDER_H

#include "game.h"

/* Call after gfxInitDefault / C3D_Init / C2D_Init / C2D_Prepare. */
void render_init(void);
void render_exit(void);

/* Draws both screens and presents the frame. */
void render_frame(const Game *g);

#endif
