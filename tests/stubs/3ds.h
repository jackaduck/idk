/* Minimal stand-in for libctru's <3ds.h>, used ONLY for `gcc -fsyntax-only`
 * checks on a PC. It mirrors the calls this project uses. */
#ifndef STUB_3DS_H
#define STUB_3DS_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct { u16 px, py; } touchPosition;

#define KEY_A      (1u << 0)
#define KEY_START  (1u << 3)
#define KEY_DUP    (1u << 6)
#define KEY_DDOWN  (1u << 7)
#define KEY_TOUCH  (1u << 20)

typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;

void gfxInitDefault(void);
void gfxExit(void);
void gfxSet3D(bool enable);
bool aptMainLoop(void);
void hidScanInput(void);
u32  hidKeysDown(void);
u32  hidKeysHeld(void);
void hidTouchRead(touchPosition *pos);
u64  osGetTime(void);
void osSetSpeedupEnable(bool enable);

#endif
