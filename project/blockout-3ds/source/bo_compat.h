/*
  File:        bo_compat.h
  Description: Compat types/constants for the 3DS port of BlockOut II
  Program:     BlockOut 3DS (port of BlockOut II 2.5, GPL)
  Author:      Jean-Luc PONS (original), port on devkitARM

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This header replaces the original's Types.h (Windows/SDL/OpenGL) with
  equivalents for devkitARM + citro2d.  Constant names and values are
  IDENTICAL to Types.h of BlockOut II 2.5: this way Game.cpp / Pit.cpp /
  PolyCube.cpp / BotPlayer*.cpp stay unchanged.
*/

#ifndef _BO_COMPAT_H_
#define _BO_COMPAT_H_

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/* the original's STR(x) ("localized" strings): a pass-through here. */
#define STR(x) ((char *)x)

/* --- base types ----------------------------------------------------------- */

typedef unsigned char  BYTE;
typedef unsigned char  BOOL;
typedef unsigned short WORD;
typedef unsigned int   DWORD;
typedef unsigned long  ULONG;
typedef float          GLfloat;      /* GLApp/GLMatrix.h */

typedef int32_t        int32;
typedef uint32_t       uint32;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Values identical to the original's GLApp.h (GL_OK = 1, GL_FAIL = 0):
   callers of the "if( !Create(...) ) exit(0);" kind depend on this. */
#define GL_OK   1
#define GL_FAIL 0

/* --- constants (identical to Types.h) -------------------------------------- */

#define PI              3.1415926535f

#define STARTZ          0.87f
#define FAR_DISTANCE    10.0f
#define MAX_CUBE        50
#define NB_POLYCUBE     41

/* pit dimensions */
#define MAX_PITWIDTH    7
#define MAX_PITHEIGHT   7
#define MAX_PITDEPTH    18
#define MIN_PITWIDTH    3
#define MIN_PITHEIGHT   3
#define MIN_PITDEPTH    6

/* block sets */
#define BLOCKSET_FLAT      0
#define BLOCKSET_BASIC     1
#define BLOCKSET_EXTENDED  2
#define NB_BLOCKSET        3

/* animation speed */
#define ASPEED_SLOW    0
#define ASPEED_FAST    10
#define NB_ASPEED      11

/* face transparency */
#define FTRANS_MIN     0
#define FTRANS_MAX     10

/* game states (1..4 in the original) */
#define GAME_PLAYING   1
#define GAME_PAUSED    2
#define GAME_OVER      3
#define GAME_DEMO      4

/* graphic style */
#define STYLE_CLASSIC  0
#define STYLE_MARBLE   1
#define STYLE_ARCADE   2
#define NB_STYLE       3

/* sound type */
#define SOUND_BLOCKOUT2  0
#define SOUND_BLOCKOUT   1
#define NB_SOUND_TYPE    2

/* line width (ARCADE style) */
#define LINEW_MIN      0
#define LINEW_MAX      10

/* --- base structures --------------------------------------------------- */

typedef struct { int x; int y; } POINT2D;

typedef struct { float x; float y; float z; } VERTEX;

/* GLApp.h: viewport and material (used by Game.h / Pit.h) */
typedef struct { int x; int y; int width; int height; } GLVIEWPORT;
typedef struct { float r; float g; float b; float a; } GLCOLOR;
typedef struct {
  GLCOLOR Diffuse;
  GLCOLOR Ambient;
  GLCOLOR Specular;
  GLCOLOR Emissive;
  float   Power;
} GLMATERIAL;

typedef struct { int x; int y; int z; } BLOCKITEM;

typedef struct { BLOCKITEM p1; BLOCKITEM p2; int orientation; } EDGE;

typedef struct { VERTEX p; VERTEX o; } CORNER;

typedef struct {
  int r0;
  int r1;
  int r2;
} ORIENTATION;

typedef struct {
  int32 rotate;
  int32 tx;
  int32 ty;
  int32 tz;
} AI_MOVE;

typedef struct {
  char  name[11];
  int32 rank;
  int32 highScore;
} PLAYER_INFO;

/* --- util (the original's Utils.cpp) ------------------------------------- */

extern VERTEX v(float x, float y, float z);
extern void   Normalize(VERTEX *v);
extern int    fround(float x);
extern char  *FormatTime(float seconds);
extern void   ZeroMemory(void *buff, int size);

/* --- key codes -----------------------------------------------------
   The original indexes keys[] (BYTE keys[512]) with SDL codes. In the port
   main.cpp converts hidRead() into these codes; the ASCII characters ('P','p',
   the digits 0..9 = 48..57) stay unchanged because Game.cpp compares them
   directly. The historical SDL codes are kept for the non-ASCII keys.
*/

#define BO_KEY_SPACE      32
#define BO_KEY_RETURN     13
#define BO_KEY_ESCAPE     27
#define BO_KEY_PAGEUP      9
#define BO_KEY_PAGEDOWN   12
#define BO_KEY_UP        273
#define BO_KEY_DOWN      274
#define BO_KEY_LEFT      275
#define BO_KEY_RIGHT     276
#define BO_KEY_END       277
#define BO_KEY_HOME      278
#define BO_KEY_KP0       320    /* keypad: 0..9 = 320..329 */
#define BO_KEY_LAST      512    /* size of the keys[] array */

/* 3DS keys translated into the codes above; these are extras (mapping) */
#define BO_KEY_CST_UP     340
#define BO_KEY_CST_DOWN   341
#define BO_KEY_CST_LEFT   342
#define BO_KEY_CST_RIGHT  343
#define BO_KEY_L          344
#define BO_KEY_R          345
#define BO_KEY_A          346
#define BO_KEY_B          347
#define BO_KEY_X          348
#define BO_KEY_Y          349
#define BO_KEY_START      350
#define BO_KEY_SELECT     351

#endif /* _BO_COMPAT_H_ */
