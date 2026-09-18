/*
  File:        bo_compat.h
  Description: Compat types/constants for the 3DS port of BlockOut II
  Program:     BlockOut 3DS (port of BlockOut II 2.5, GPL)
  Author:      Jean-Luc PONS (original), port on devkitARM

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Questa testata sostituisce Types.h dell'originale (Windows/SDL/OpenGL) con
  equivalenti per devkitARM + citro2d.  Nomi e valori delle costanti sono
  IDENTICI a Types.h di BlockOut II 2.5: cosi' Game.cpp / Pit.cpp /
  PolyCube.cpp / BotPlayer*.cpp restano invariati.
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

/* STR(x) dell'originale (stringhe "localizzate"): qui e' passante. */
#define STR(x) ((char *)x)

/* --- tipi base ----------------------------------------------------------- */

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

/* Valori identici a GLApp.h dell'originale (GL_OK = 1, GL_FAIL = 0):
   i chiamanti del tipo "if( !Create(...) ) exit(0);" dipendono da questo. */
#define GL_OK   1
#define GL_FAIL 0

/* --- costanti (identiche a Types.h) -------------------------------------- */

#define PI              3.1415926535f

#define STARTZ          0.87f
#define FAR_DISTANCE    10.0f
#define MAX_CUBE        50
#define NB_POLYCUBE     41

/* dimensioni del pozzo */
#define MAX_PITWIDTH    7
#define MAX_PITHEIGHT   7
#define MAX_PITDEPTH    18
#define MIN_PITWIDTH    3
#define MIN_PITHEIGHT   3
#define MIN_PITDEPTH    6

/* set di blocchi */
#define BLOCKSET_FLAT      0
#define BLOCKSET_BASIC     1
#define BLOCKSET_EXTENDED  2
#define NB_BLOCKSET        3

/* velocita' di animazione */
#define ASPEED_SLOW    0
#define ASPEED_FAST    10
#define NB_ASPEED      11

/* trasparenza delle facce */
#define FTRANS_MIN     0
#define FTRANS_MAX     10

/* stati del gioco  (1..4 nell'originale) */
#define GAME_PLAYING   1
#define GAME_PAUSED    2
#define GAME_OVER      3
#define GAME_DEMO      4

/* stile grafico */
#define STYLE_CLASSIC  0
#define STYLE_MARBLE   1
#define STYLE_ARCADE   2
#define NB_STYLE       3

/* tipo di suono */
#define SOUND_BLOCKOUT2  0
#define SOUND_BLOCKOUT   1
#define NB_SOUND_TYPE    2

/* larghezza linea (stile ARCADE) */
#define LINEW_MIN      0
#define LINEW_MAX      10

/* --- strutture di base --------------------------------------------------- */

typedef struct { int x; int y; } POINT2D;

typedef struct { float x; float y; float z; } VERTEX;

/* GLApp.h: viewport e materiale (usati da Game.h / Pit.h) */
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

/* --- util (Utils.cpp dell'originale) ------------------------------------- */

extern VERTEX v(float x, float y, float z);
extern void   Normalize(VERTEX *v);
extern int    fround(float x);
extern char  *FormatTime(float seconds);
extern void   ZeroMemory(void *buff, int size);

/* --- codici dei tasti -----------------------------------------------------
   L'originale indicizza keys[] (BYTE keys[512]) con i codici SDL. Nel port
   main.cpp converte hidRead() in questi codici; i caratteri ASCII ('P','p',
   le cifre 0..9 = 48..57) restano invariati perche' Game.cpp li confronta
   direttamente. I codici SDL storici sono conservati per i tasti non ASCII.
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
#define BO_KEY_KP0       320    /* tastierino numerico: 0..9 = 320..329 */
#define BO_KEY_LAST      512    /* dimensione dell'array keys[] */

/* tasti del 3DS tradotti nei codici sopra; questi sono extra (mappatura) */
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
