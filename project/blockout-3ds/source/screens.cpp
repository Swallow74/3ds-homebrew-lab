/*
  File:        screens.cpp
  Description: Interface screens (intro, menu, setup, scores, pause,
               game over) in MS-DOS style on top of ui.h / render.h
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Adaptations required by the 3DS hardware (documented):
   * the original drew the menu pages with its own OpenGL classes
     (PageMainMenu / PageChooseSetup / PageHallOfFame...); here the same
     pages are rebuilt on top of the software renderer, in DOS style: black
     background, 8x8 font, EGA palette, blue selection bar;
   * the "BLOCKOUT" logo is a 3D voxel sign built from the font
     glyphs (the cubes rotate slowly and stick out of the screen in 3D);
   * touch exists only on the bottom screen: the touchable lists live there.
*/

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>

#include "screens.h"
#include "render.h"
#include "ui.h"
#include "Game.h"
#include "SetupManager.h"
#include "SoundManager.h"
#include "audio.h"
#include "music.h"
#include "autotest.h"

extern "C" const u8 default_font_bin[];

#define TOPW  400
#define BOTW  320

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static float sNow(void) {
  return (float)((double)svcGetSystemTick() / (double)SYSCLOCK_ARM11);
}

static u32 pDown, pHeld;
static int tX = 0, tY = 0, tJust = 0;

static void poll(void) {
  hidScanInput();
  pDown = hidKeysDown();
  pHeld = hidKeysHeld();
  int tx = 0, ty = 0, jd = 0;
  ui_touch(&tx, &ty, &jd);
  tX = tx; tY = ty; tJust = jd;
}

/* Touch area: a single hit per frame */
static int sHit(float x, float y, float w, float h) {
  if (!tJust) return 0;
  if (tX < (int)x || tX >= (int)(x + w) || tY < (int)y || tY >= (int)(y + h)) return 0;
  tJust = 0;
  return 1;
}

/* Press with automatic repeat (menus and values) */
static int repeatKey(u32 bits) {
  static float next[32];
  static u32 last = 0;
  float now = sNow();
  int fire = 0;
  for (int b = 0; b < 32; b++) {
    u32 m = 1u << b;
    if (!(bits & m)) continue;
    if (pDown & m) { next[b] = now + 0.35f; fire = 1; }
    else if ((pHeld & m) && (last & m) && now >= next[b]) { next[b] = now + 0.09f; fire = 1; }
  }
  last = pHeld;
  return fire;
}

float stereoLevelPx(int level) {
  static const float px[5] = { 0.0f, 5.0f, 9.0f, 13.0f, 18.0f };
  if (level < 0) level = 0;
  if (level > 4) level = 4;
  return px[level];
}

/* Utils.cpp::FormatDateShort */
static const char *fmtDate(uint32 t) {
  static char ret[32];
  if (t > 0) {
    time_t tt = (time_t)t;
    struct tm *ts = localtime(&tt);
    if (ts) {
      snprintf(ret, sizeof(ret), "%02d-%02d-%04d", ts->tm_mday, ts->tm_mon + 1,
               ts->tm_year + 1900);
      return ret;
    }
  }
  strcpy(ret, "..........");
  return ret;
}

/* ------------------------------------------------------------------ */
/* 3D projection for the menus (with stereoscopy)                      */
/* ------------------------------------------------------------------ */

static float pCx = 200.0f, pCy = 120.0f, pF = 300.0f, pConv = 10.0f;

static void proj_setup(float cx, float cy, float f, float conv) {
  pCx = cx; pCy = cy; pF = f; pConv = conv;
}

/* positive disparity (behind the screen) = uncrossed images */
static int proj(float x, float y, float z, float *sx, float *sy) {
  if (z < 0.05f) return 0;
  float disp = r_stereo_px() * (1.0f - pConv / z);
  if (disp > 14.0f) disp = 14.0f;
  if (disp < -14.0f) disp = -14.0f;
  *sx = pCx + x * pF / z + (r_eye() ? 0.5f : -0.5f) * disp;
  *sy = pCy - y * pF / z;
  return 1;
}

static void rotYX(float *x, float *y, float *z, float ay, float ax) {
  float cy = cosf(ay), sy = sinf(ay);
  float x1 = *x * cy + *z * sy, z1 = -*x * sy + *z * cy;
  float cx = cosf(ax), sx = sinf(ax);
  float y1 = *y * cx - z1 * sx, z2 = *y * sx + z1 * cx;
  *x = x1; *y = y1; *z = z2;
}

static uint32_t shade(uint32_t c, float k) {
  int r = (int)((c & 0xFF) * k), g = (int)(((c >> 8) & 0xFF) * k), b = (int)(((c >> 16) & 0xFF) * k);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return r_color(r, g, b, 255);
}

/* ------------------------------------------------------------------ */
/* 3D voxel logo                                                        */
/* ------------------------------------------------------------------ */
/* "BLOCKOUT" written with the 8x8 font: every lit pixel is a cube. The
   contiguous pixels of a row are merged into a single cuboid (fewer
   polygons). The letter colors are those of the pit layers. */

typedef struct { signed char x0, x1, row, letter; } LogoRun;
static LogoRun gRuns[400];
static int gNbRuns = -1;

static void logoInit(void) {
  const char *txt = "BLOCKOUT";
  gNbRuns = 0;
  for (int l = 0; l < 8; l++) {
    const u8 *gl = &default_font_bin[(unsigned char)txt[l] * 8];
    for (int r = 0; r < 8; r++) {
      int x = 0;
      while (x < 8) {
        if (gl[r] & (0x80 >> x)) {
          int s = x;
          while (x < 8 && (gl[r] & (0x80 >> x))) x++;
          if (gNbRuns < 400) {
            gRuns[gNbRuns].x0 = (signed char)(l * 8 + s);
            gRuns[gNbRuns].x1 = (signed char)(l * 8 + x);
            gRuns[gNbRuns].row = (signed char)r;
            gRuns[gNbRuns].letter = (signed char)l;
            gNbRuns++;
          }
        } else x++;
      }
    }
  }
}

static const uint32_t kLayerCol[8] = {
  EGA(0x30, 0x40, 0xFF), EGA(0x20, 0xE0, 0x20), EGA(0x20, 0xD8, 0xD8), EGA(0xF0, 0x30, 0x30),
  EGA(0xF0, 0x30, 0xD0), EGA(0xF0, 0xA0, 0x10), EGA(0xD8, 0xD8, 0xD8), EGA(0x30, 0x40, 0xFF)
};

/* cx,cy: center on the screen; unit: side of a voxel in pixels */
static void drawLogo(float cx, float cy, float unit, float t) {
  if (gNbRuns < 0) logoInit();
  /* far camera (in voxels): soft perspective, no distortion */
  const float Z0 = 110.0f;
  proj_setup(cx, cy, unit * Z0, Z0 + 6.0f);   /* the logo sticks out of the screen */
  float ay = 0.28f * sinf(t * 0.55f);
  float ax = 0.20f + 0.08f * sinf(t * 0.37f);
  const float D = 2.0f;                       /* thickness in voxels */

  static const float fn[6][3] = {
    { 0, 0, -1 }, { 0, 1, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }
  };
  static const int F[6][4] = {
    { 0, 1, 3, 2 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 }, { 4, 6, 7, 5 }
  };
  /* two passes: sides, then front faces */
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < gNbRuns; i++) {
      const LogoRun *r = &gRuns[i];
      float x0 = (float)r->x0 - 32.0f, x1 = (float)r->x1 - 32.0f;
      float y0 = 4.0f - (float)r->row, y1 = y0 - 1.0f;
      float V[8][3], S[8][2];
      int ok = 1;
      for (int k = 0; k < 8; k++) {
        float x = (k & 1) ? x1 : x0, y = (k & 2) ? y1 : y0, z = (k & 4) ? D : 0.0f;
        rotYX(&x, &y, &z, ay, ax);
        z += Z0;
        V[k][0] = x; V[k][1] = y; V[k][2] = z;
        ok &= proj(x, y, z, &S[k][0], &S[k][1]);
      }
      if (!ok) continue;
      uint32_t base = kLayerCol[(int)r->letter];
      for (int f = (pass ? 0 : 1); f < (pass ? 1 : 5); f++) {
        float nx = fn[f][0], ny = fn[f][1], nz = fn[f][2];
        rotYX(&nx, &ny, &nz, ay, ax);
        const float *c = V[F[f][0]];
        if (nx * -c[0] + ny * -c[1] + nz * -c[2] <= 0.0f) continue;
        float light = pass ? 1.0f : (ny > 0.3f ? 0.75f : 0.45f);
        uint32_t col = shade(base, light);
        float q[8] = { S[F[f][0]][0], S[F[f][0]][1], S[F[f][1]][0], S[F[f][1]][1],
                       S[F[f][2]][0], S[F[f][2]][1], S[F[f][3]][0], S[F[f][3]][1] };
        r_quad_a(q, OPAQUE(col));
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* Rotating wireframe polycube (menu)                                   */
/* ------------------------------------------------------------------ */

/* Demo pieces (the game's polycubes only exist after the first
   StartGame): cubes in integer coordinates, outline computed like the
   original's lineList (non-coplanar/inner edges excluded). */
static const signed char kDemoPieces[][5][3] = {
  { {0,0,0}, {1,0,0}, {2,0,0}, {2,1,0}, {9,9,9} },   /* L */
  { {0,0,0}, {1,0,0}, {1,1,0}, {1,1,1}, {9,9,9} },   /* helix */
  { {0,0,0}, {1,0,0}, {0,1,0}, {0,0,1}, {9,9,9} },   /* 3D corner */
  { {0,0,0}, {1,0,0}, {2,0,0}, {1,1,0}, {9,9,9} },   /* T */
  { {0,0,0}, {1,0,0}, {1,1,0}, {2,1,0}, {9,9,9} },   /* S */
  { {0,0,0}, {1,0,0}, {0,1,0}, {1,1,0}, {0,0,1} },   /* square + 1 */
};
#define DEMO_NB 6

static int demoHas(int p, int x, int y, int z) {
  for (int i = 0; i < 5; i++) {
    const signed char *c = kDemoPieces[p][i];
    if (c[0] == 9) break;
    if (c[0] == x && c[1] == y && c[2] == z) return 1;
  }
  return 0;
}

/* Edge along the "ax" axis starting from vertex (x,y,z): it is visible if
   the 4 surrounding cubes are 1 or 3, or 2 diagonal */
static int demoEdge(int p, int ax, int x, int y, int z) {
  int o[4];
  int k = 0;
  for (int a = -1; a <= 0; a++)
    for (int b = -1; b <= 0; b++) {
      int cx = x, cy = y, cz = z;
      if (ax == 0) { cy += a; cz += b; }
      else if (ax == 1) { cx += a; cz += b; }
      else { cx += a; cy += b; }
      o[k++] = demoHas(p, cx, cy, cz);
    }
  int n = o[0] + o[1] + o[2] + o[3];
  if (n == 1 || n == 3) return 1;
  if (n == 2 && o[0] == o[3]) return 1;   /* diagonal */
  return 0;
}

static void drawSpinningPiece(int p, float cx, float cy, float size, float t, uint32_t col) {
  const float Z0 = 7.0f;
  proj_setup(cx, cy, size * Z0, Z0 + 0.5f);
  float ay = t * 0.9f, ax = 0.5f + 0.3f * sinf(t * 0.6f);
  /* center of the piece */
  float mx = 0, my = 0, mz = 0; int nc = 0;
  for (int i = 0; i < 5; i++) {
    const signed char *c = kDemoPieces[p][i];
    if (c[0] == 9) break;
    mx += c[0] + 0.5f; my += c[1] + 0.5f; mz += c[2] + 0.5f; nc++;
  }
  mx /= nc; my /= nc; mz /= nc;
  for (int ax2 = 0; ax2 < 3; ax2++)
    for (int x = -1; x <= 4; x++)
      for (int y = -1; y <= 4; y++)
        for (int z = -1; z <= 4; z++) {
          if (!demoEdge(p, ax2, x, y, z)) continue;
          float a[3] = { (float)x, (float)y, (float)z };
          float b[3] = { (float)x, (float)y, (float)z };
          b[ax2] += 1.0f;
          float ax0 = a[0] - mx, ay0 = a[1] - my, az0 = a[2] - mz;
          float bx0 = b[0] - mx, by0 = b[1] - my, bz0 = b[2] - mz;
          rotYX(&ax0, &ay0, &az0, ay, ax);
          rotYX(&bx0, &by0, &bz0, ay, ax);
          float sx0, sy0, sx1, sy1;
          if (proj(ax0, ay0, az0 + Z0, &sx0, &sy0) && proj(bx0, by0, bz0 + Z0, &sx1, &sy1))
            r_line(sx0, sy0, sx1, sy1, 1.0f, col);
        }
}

/* ------------------------------------------------------------------ */
/* Perspective preview of the pit (setup)                               */
/* ------------------------------------------------------------------ */

static void drawPitPreview(float cx, float cy, float size, int w, int h, int d) {
  float m = (float)(w > h ? w : h);
  float cs = 1.0f / m;                        /* the mouth is 1 wide */
  float hw = w * cs * 0.5f, hh = h * cs * 0.5f;
  const float Z0 = 1.0f;
  proj_setup(cx, cy, size * Z0, Z0);
  uint32_t g = EGA(0x00, 0x99, 0x00);
  float sx0, sy0, sx1, sy1;
#define PL(ax, ay, az, bx, by, bz) \
  if (proj(ax, ay, az, &sx0, &sy0) && proj(bx, by, bz, &sx1, &sy1)) r_line(sx0, sy0, sx1, sy1, 1.0f, g)
  for (int k = 0; k <= d; k++) {
    float z = Z0 + k * cs;
    PL(-hw, -hh, z, hw, -hh, z);
    PL(hw, -hh, z, hw, hh, z);
    PL(hw, hh, z, -hw, hh, z);
    PL(-hw, hh, z, -hw, -hh, z);
  }
  float zb = Z0 + d * cs;
  for (int i = 0; i <= w; i++) {
    float x = -hw + i * cs;
    PL(x, -hh, Z0, x, -hh, zb);
    PL(x, hh, Z0, x, hh, zb);
    PL(x, -hh, zb, x, hh, zb);
  }
  for (int j = 0; j <= h; j++) {
    float y = -hh + j * cs;
    PL(-hw, y, Z0, -hw, y, zb);
    PL(hw, y, Z0, hw, y, zb);
    PL(-hw, y, zb, hw, y, zb);
  }
#undef PL
}

/* Very light "starry sky" row behind the menus: fixed dots */
static void drawStars(float t) {
  static float sx[60], sy[60];
  static int init = 0;
  if (!init) {
    unsigned s = 12345u;
    for (int i = 0; i < 60; i++) {
      s = s * 1103515245u + 12345u; sx[i] = (float)((s >> 8) % 400);
      s = s * 1103515245u + 12345u; sy[i] = (float)((s >> 8) % 240);
    }
    init = 1;
  }
  float d = r_stereo_px() * 0.8f * (r_eye() ? 0.5f : -0.5f);   /* far behind */
  for (int i = 0; i < 60; i++) {
    int tw = ((int)(t * 3.0f) + i) % 9;
    uint32_t c = (tw == 0) ? E_WHITE : (i & 1) ? E_DGRAY : E_BLUE;
    r_rect(sx[i] + d, sy[i], 1.0f, 1.0f, c);
  }
}

/* ------------------------------------------------------------------ */
/* Intro                                                               */
/* ------------------------------------------------------------------ */

void runIntroScreen(void) {
  float t0 = sNow();
  audio_music(MUS_TITLE);

  while (aptMainLoop()) {
    poll();
    if (pDown & (KEY_A | KEY_START | KEY_B | KEY_X | KEY_Y)) break;
    if (tJust) { tJust = 0; break; }

    float now = sNow() - t0;
    if (AUTOTEST && now > 2.5f) break;
    render_clear(COL_BG);
    for (int eye = 0; eye < 2; eye++) {
      render_begin_top(eye);
      drawStars(now);
      drawLogo(200.0f, 92.0f, 5.0f, now);
      r_print(200.0f, 156.0f, 1, UI_LABEL, "A FALLING BLOCK PUZZLE", 1);
      r_print(200.0f, 168.0f, 1, UI_LABEL, "IN THREE DIMENSIONS", 1);
      if (ui_blink(now, 1.0f)) r_print(200.0f, 200.0f, 2, E_YELLOW, "PRESS START", 1);
    }

    render_begin_bottom();
    ui_box(4.0f, 4.0f, 312.0f, 232.0f, UI_FRAME, E_BLACK, " BLOCKOUT 3DS ", E_YELLOW);
    r_print(160.0f, 24.0f, 1, E_WHITE, "UNOFFICIAL NINTENDO 3DS PORT OF", 1);
    r_print(160.0f, 36.0f, 1, E_WHITE, "BLOCKOUT II 2.5 (GPL)", 1);
    r_print(160.0f, 48.0f, 1, UI_DIM, "BY JEAN-LUC PONS", 1);
    ui_hline(16.0f, 64.0f, 304.0f, E_BLUE);
    r_print(160.0f, 72.0f, 1, UI_DIM, "BLOCKOUT IS A TRADEMARK OF", 1);
    r_print(160.0f, 84.0f, 1, UI_DIM, "KADON ENTERPRISES, INC.", 1);
    ui_hline(16.0f, 100.0f, 304.0f, E_BLUE);
    r_print(24.0f, 110.0f, 1, UI_LABEL, "CONTROLS", 0);
    ui_key_hint(24.0f, 124.0f, "D-PAD", "MOVE THE BLOCK");
    ui_key_hint(24.0f, 136.0f, "L+D-PAD", "MOVE DIAGONALLY");
    ui_key_hint(24.0f, 148.0f, "B X Y", "ROTATE ON Z X Y");
    ui_key_hint(24.0f, 160.0f, "R+B X Y", "ROTATE BACKWARDS");
    ui_key_hint(24.0f, 172.0f, "A", "DROP");
    ui_key_hint(24.0f, 184.0f, "START", "PAUSE");
    ui_key_hint(24.0f, 196.0f, "ZR", "3D DEPTH");
    if (ui_blink(now, 1.0f)) r_print(160.0f, 216.0f, 1, E_YELLOW, "TOUCH OR PRESS START", 1);
    render_swap(true);
  }
}

/* ------------------------------------------------------------------ */
/* Main menu                                                           */
/* ------------------------------------------------------------------ */

struct MenuEntry { const char *label; const char *desc; };

static const MenuEntry menuEntries[] = {
  { "PLAY GAME",   "START A NEW GAME ON THE PIT AND BLOCK SET CHOSEN IN SETUP. COMPLETE A FACE OF THE PIT TO REMOVE IT." },
  { "PRACTICE",    "PLAY WITHOUT SCORE. PRESS ZL TO SEE WHERE THE COMPUTER WOULD PUT THE BLOCK." },
  { "DEMO",        "WATCH THE COMPUTER PLAY. PRESS START OR SELECT TO STOP." },
  { "SETUP",       "PIT SIZE, BLOCK SET, SPEED, STARTING LEVEL, 3D DEPTH, SOUND AND MUSIC." },
  { "HIGH SCORES", "THE TEN BEST SCORES FOR THE CURRENT PIT AND BLOCK SET." },
  { "QUIT",        "SAVE THE SETUP AND RETURN TO THE HOMEBREW MENU." },
};
#define MENU_NB 6

int runMenuScreen(Game *game, SetupManager *sm) {
  (void)game;
  static int sel = 0;
  int act = SCR_NONE;
  float t0 = sNow();
  float idle = t0;

  audio_music(MUS_TITLE);

  while (aptMainLoop() && act == SCR_NONE) {
    poll();
    float now = sNow() - t0;
    if (pDown) idle = sNow();

    if (repeatKey(KEY_UP | KEY_CPAD_UP))     { sel = (sel + MENU_NB - 1) % MENU_NB; audio_tchh(); }
    if (repeatKey(KEY_DOWN | KEY_CPAD_DOWN)) { sel = (sel + 1) % MENU_NB; audio_tchh(); }

    for (int i = 0; i < MENU_NB; i++)
      if (sHit(40.0f, 60.0f + i * 20.0f, 240.0f, 18.0f)) {
        sel = i; audio_wozz(); act = i + SCR_PLAY; idle = sNow();
      }

    if (pDown & (KEY_A | KEY_START)) { audio_wozz(); act = sel + SCR_PLAY; }
    if (pDown & KEY_B)               { audio_tchh(); sel = MENU_NB - 1; }

#if AUTOTEST
    {
      static int atStep = 0;
      static const int seq[] = { SCR_DEMO, SCR_PRACTICE, SCR_SETUP, SCR_HISCORE, SCR_PLAY };
      if (now > 1.0f + 0.4f * atStep && sel != (seq[atStep % 5] - SCR_PLAY)) sel = seq[atStep % 5] - SCR_PLAY;
      if (now > 3.0f) act = seq[atStep++ % 5];
    }
#endif
    /* like the original: after a while of inactivity the demo starts */
    if (act == SCR_NONE && sNow() - idle > 40.0f) act = SCR_DEMO;

    render_clear(COL_BG);
    for (int eye = 0; eye < 2; eye++) {
      render_begin_top(eye);
      drawStars(now);
      drawLogo(200.0f, 50.0f, 4.0f, now);

      drawSpinningPiece(((int)(now / 5.0f)) % DEMO_NB, 200.0f, 118.0f, 26.0f, now, E_WHITE);

      ui_box(8.0f, 164.0f, 384.0f, 70.0f, UI_FRAME, E_BLACK, menuEntries[sel].label, E_YELLOW);
      ui_wrap(18.0f, 176.0f, E_WHITE, menuEntries[sel].desc, 364.0f, 4);
    }

    render_begin_bottom();
    ui_box(4.0f, 4.0f, 312.0f, 232.0f, UI_FRAME, E_BLACK, " MAIN MENU ", E_YELLOW);
    char buf[48];
    snprintf(buf, sizeof(buf), "PIT %dx%dx%d  %s", sm->GetPitWidth(), sm->GetPitHeight(),
             sm->GetPitDepth(), sm->GetBlockSetName());
    r_print(160.0f, 22.0f, 1, UI_LABEL, buf, 1);
    snprintf(buf, sizeof(buf), "HIGH SCORE %d", sm->GetHighScore());
    r_print(160.0f, 36.0f, 1, E_YELLOW, buf, 1);
    for (int i = 0; i < MENU_NB; i++)
      ui_menu_row(40.0f, 60.0f + i * 20.0f, 240.0f, menuEntries[i].label, i == sel, now);
    ui_hline(12.0f, 186.0f, 308.0f, E_BLUE);
    ui_key_hint(20.0f, 196.0f, "A", "SELECT");
    ui_key_hint(120.0f, 196.0f, "UP/DOWN", "CHOOSE");
    r_print(160.0f, 214.0f, 1, E_DGRAY, "OR TOUCH AN ITEM", 1);
    render_swap(true);
  }

  if (act == SCR_NONE) act = SCR_EXIT;
  return act;
}

/* ------------------------------------------------------------------ */
/* Configurazione                                                      */
/* ------------------------------------------------------------------ */

struct SetupRow { const char *label; const char *desc; };

enum { SR_W, SR_H, SR_D, SR_SET, SR_LEVEL, SR_SPEED, SR_GHOST, SR_FILL, SR_3D,
       SR_SOUND, SR_STYLE, SR_MUSIC, SETUP_NB };

static const SetupRow setupRows[SETUP_NB] = {
  { "PIT WIDTH",    "WIDTH OF THE PIT: 3 TO 7 CUBES." },
  { "PIT HEIGHT",   "HEIGHT OF THE PIT: 3 TO 7 CUBES." },
  { "PIT DEPTH",    "DEPTH OF THE PIT: 6 TO 18 LAYERS. EACH PIT KEEPS ITS OWN HIGH SCORES." },
  { "BLOCK SET",    "FLAT: FLAT BLOCKS ONLY. BASIC: SIMPLE 3D BLOCKS. EXTENDED: ALL THE BLOCKS." },
  { "START LEVEL",  "STARTING LEVEL 0 TO 9. HIGHER LEVELS FALL FASTER AND SCORE MORE." },
  { "ANIMATION",    "SPEED OF THE MOVE AND ROTATION ANIMATIONS: 0 SLOW, 10 FAST." },
  { "GHOST FACES",  "0 = WIREFRAME BLOCK AS IN THE ORIGINAL. HIGHER VALUES ADD SEE-THROUGH FACES." },
  { "BLOCK FILL",   "TINTED FACES ON THE FALLING BLOCK. OFF = WHITE WIREFRAME AS ON MS-DOS." },
  { "3D DEPTH",     "STRENGTH OF THE STEREOSCOPIC EFFECT (ALSO FOLLOWS THE 3D SLIDER). ZR IN GAME." },
  { "SOUND FX",     "SOUND EFFECTS ON OR OFF." },
  { "FX STYLE",     "MS-DOS: PC SPEAKER STYLE BEEPS. BLOCKOUT II: SYNTH EFFECTS." },
  { "MUSIC",        "SOUNDTRACK IN MENUS AND DURING THE GAME. TEMPO RISES WITH THE LEVEL." },
};

static void setupValueText(SetupManager *sm, int idx, char *buf, size_t n) {
  static const char *OFFON[2] = { "OFF", "ON" };
  switch (idx) {
    case SR_W:     snprintf(buf, n, "%d", sm->GetPitWidth()); break;
    case SR_H:     snprintf(buf, n, "%d", sm->GetPitHeight()); break;
    case SR_D:     snprintf(buf, n, "%d", sm->GetPitDepth()); break;
    case SR_SET:   snprintf(buf, n, "%s", sm->GetBlockSetName()); break;
    case SR_LEVEL: snprintf(buf, n, "%d", sm->GetStartingLevel()); break;
    case SR_SPEED: snprintf(buf, n, "%d", sm->GetAnimationSpeed()); break;
    case SR_GHOST: snprintf(buf, n, "%d", sm->GetTransparentFace()); break;
    case SR_FILL:  snprintf(buf, n, "%s", OFFON[sm->GetPieceFill() ? 1 : 0]); break;
    case SR_3D:
      if (sm->GetStereo()) snprintf(buf, n, "%d", sm->GetStereo());
      else snprintf(buf, n, "OFF");
      break;
    case SR_SOUND: snprintf(buf, n, "%s", OFFON[sm->GetSound() ? 1 : 0]); break;
    case SR_STYLE: snprintf(buf, n, "%s", sm->GetSoundType() == SOUND_BLOCKOUT ? "MS-DOS" : "BLOCKOUT II"); break;
    case SR_MUSIC: snprintf(buf, n, "%s", OFFON[sm->GetMusic() ? 1 : 0]); break;
    default:       snprintf(buf, n, "-"); break;
  }
}

static void setupApply(SetupManager *sm, SoundManager *snd, int sel, int dv) {
  switch (sel) {
    case SR_W:     sm->SetPitWidth(sm->GetPitWidth() + dv); break;
    case SR_H:     sm->SetPitHeight(sm->GetPitHeight() + dv); break;
    case SR_D:     sm->SetPitDepth(sm->GetPitDepth() + dv); break;
    case SR_SET:   sm->SetBlockSet((sm->GetBlockSet() + dv + 3) % 3); break;
    case SR_LEVEL: sm->SetStartingLevel(sm->GetStartingLevel() + dv); break;
    case SR_SPEED: sm->SetAnimationSpeed(sm->GetAnimationSpeed() + dv); break;
    case SR_GHOST: sm->SetTransparentFace(sm->GetTransparentFace() + dv); break;
    case SR_FILL:  sm->SetPieceFill(!sm->GetPieceFill());
                   render_set_piece_fill(sm->GetPieceFill()); break;
    case SR_3D:    sm->SetStereo(sm->GetStereo() + dv);
                   render_set_stereo(stereoLevelPx(sm->GetStereo())); break;
    case SR_SOUND: sm->SetSound(!sm->GetSound());
                   snd->SetEnable(sm->GetSound());
                   audio_set_enable(sm->GetSound() ? true : false); break;
    case SR_STYLE: sm->SetSoundType(sm->GetSoundType() == SOUND_BLOCKOUT ? SOUND_BLOCKOUT2 : SOUND_BLOCKOUT);
                   audio_set_style(sm->GetSoundType() == SOUND_BLOCKOUT);
                   /* style preview */
                   audio_set_lines(2);
                   if (sm->GetSoundType() == SOUND_BLOCKOUT) audio_line2(); else audio_line();
                   break;
    case SR_MUSIC: sm->SetMusic(!sm->GetMusic());
                   audio_music_enable(sm->GetMusic() ? true : false); break;
  }
}

void runSetupScreen(Game *game, SetupManager *sm, SoundManager *snd) {
  (void)game;
  int sel = 0;
  bool done = false;
  float t0 = sNow();
  const float ROW0 = 22.0f, ROWH = 14.0f;

  while (aptMainLoop() && !done) {
    poll();
    float now = sNow() - t0;

    int dv = 0;
    if (repeatKey(KEY_UP | KEY_CPAD_UP))       { sel = (sel + SETUP_NB - 1) % SETUP_NB; audio_tchh(); }
    if (repeatKey(KEY_DOWN | KEY_CPAD_DOWN))   { sel = (sel + 1) % SETUP_NB; audio_tchh(); }
    if (repeatKey(KEY_LEFT | KEY_CPAD_LEFT | KEY_L))   dv = -1;
    if (repeatKey(KEY_RIGHT | KEY_CPAD_RIGHT | KEY_R)) dv = +1;
    if (pDown & KEY_A) dv = +1;

    for (int i = 0; i < SETUP_NB; i++) {
      float ry = ROW0 + i * ROWH;
      if (sHit(196.0f, ry - 2.0f, 112.0f, ROWH)) { sel = i; dv = +1; }
      else if (sHit(12.0f, ry - 2.0f, 180.0f, ROWH)) { sel = i; audio_tchh(); }
    }
    if (sHit(12.0f, 204.0f, 296.0f, 26.0f)) done = true;

    if (dv) { setupApply(sm, snd, sel, dv); if (sel != SR_STYLE) audio_blub(); }

    if (pDown & (KEY_B | KEY_START)) done = true;
#if AUTOTEST
    { static int atv = 0; int k = (int)(now * 2.0f); if (k != atv) { atv = k; sel = k % SETUP_NB; } }
    if (now > 4.0f) done = true;
#endif

    render_clear(COL_BG);
    for (int eye = 0; eye < 2; eye++) {
      render_begin_top(eye);
      drawStars(now);
      r_print(200.0f, 6.0f, 2, E_YELLOW, "SETUP", 1);

      int pw = sm->GetPitWidth(), ph = sm->GetPitHeight(), pd = sm->GetPitDepth();
      drawPitPreview(200.0f, 84.0f, 110.0f, pw, ph, pd);

      char buf[64];
      snprintf(buf, sizeof(buf), "PIT %d x %d x %d   %s", pw, ph, pd, sm->GetBlockSetName());
      r_print(200.0f, 146.0f, 1, UI_LABEL, buf, 1);

      ui_box(8.0f, 164.0f, 384.0f, 70.0f, UI_FRAME, E_BLACK, setupRows[sel].label, E_YELLOW);
      ui_wrap(18.0f, 176.0f, E_WHITE, setupRows[sel].desc, 364.0f, 4);
    }

    render_begin_bottom();
    ui_box(4.0f, 4.0f, 312.0f, 232.0f, UI_FRAME, E_BLACK, " SETUP ", E_YELLOW);
    char val[24];
    for (int i = 0; i < SETUP_NB; i++) {
      float ry = ROW0 + i * ROWH;
      int on = (i == sel);
      if (on) r_rect(10.0f, ry - 3.0f, 300.0f, ROWH, UI_BAR);
      r_print(16.0f, ry, 1, on ? E_WHITE : UI_LABEL, setupRows[i].label, 0);
      setupValueText(sm, i, val, sizeof(val));
      r_print(252.0f, ry, 1, on ? E_YELLOW : E_WHITE, val, 1);
      if (on) {
        r_char(196.0f, ry, 1, E_YELLOW, 0x11);
        r_char(300.0f, ry, 1, E_YELLOW, 0x10);
      }
    }
    ui_hline(12.0f, 194.0f, 308.0f, E_BLUE);
    ui_key_hint(16.0f, 202.0f, "LEFT/RIGHT", "CHANGE");
    ui_key_hint(16.0f, 216.0f, "B", "SAVE AND GO BACK");
    render_swap(true);
  }

  sm->WriteSetup();
  game->InvalidateDeviceObjects();
  audio_wozz();
}

/* ------------------------------------------------------------------ */
/* Hall of fame (PageHallOfFame.cpp)                                    */
/* ------------------------------------------------------------------ */

static void drawScoreTable(SCOREREC *all, int hilite, int editRow, const char *editText,
                           int editPos, float now, float y0) {
  r_print(16.0f, y0, 1, UI_LABEL, "RANK NAME        SCORE CUBES  DATE", 0);
  ui_hline(12.0f, y0 + 11.0f, 388.0f, E_BLUE);
  char buf[80];
  for (int i = 0; i < 10; i++) {
    float y = y0 + 16.0f + i * 14.0f;
    int on = (i == hilite);
    if (on) r_rect(12.0f, y - 3.0f, 376.0f, 14.0f, UI_BAR);
    uint32_t c = on ? E_WHITE : (all[i].score ? E_LGRAY : E_DGRAY);
    snprintf(buf, sizeof(buf), "%2d.", i + 1);
    r_print(24.0f, y, 1, on ? E_YELLOW : UI_LABEL, buf, 0);
    if (editRow == i && editText) {
      r_print(56.0f, y, 1, E_YELLOW, editText, 0);
      if (ui_blink(now, 0.5f)) r_rect(56.0f + editPos * 8.0f, y + 8.0f, 8.0f, 2.0f, E_YELLOW);
    } else {
      r_print(56.0f, y, 1, c, all[i].name[0] ? all[i].name : "..........", 0);
    }
    snprintf(buf, sizeof(buf), "%7ld", (long)all[i].score);
    r_print(200.0f, y, 1, on ? E_YELLOW : c, buf, 2);
    snprintf(buf, sizeof(buf), "%5ld", (long)all[i].nbCube);
    r_print(248.0f, y, 1, c, buf, 2);
    r_print(304.0f, y, 1, c, fmtDate(all[i].date), 0);
  }
}

void runHiScoreScreen(SetupManager *sm) {
  SCOREREC all[10];
  memset(all, 0, sizeof(all));
  sm->GetHighScore(all);
  int sel = 0;
  float t0 = sNow();

  while (aptMainLoop()) {
    poll();
    float now = sNow() - t0;
    if (repeatKey(KEY_UP | KEY_CPAD_UP))     { sel = (sel + 9) % 10; audio_tchh(); }
    if (repeatKey(KEY_DOWN | KEY_CPAD_DOWN)) { sel = (sel + 1) % 10; audio_tchh(); }
    if (pDown & (KEY_A | KEY_B | KEY_START)) { audio_tchh(); break; }
    if (sHit(0.0f, 0.0f, 320.0f, 240.0f)) { audio_tchh(); break; }
    if (AUTOTEST && now > 3.0f) break;

    render_clear(COL_BG);
    for (int eye = 0; eye < 2; eye++) {
      render_begin_top(eye);
      r_print(200.0f, 6.0f, 2, E_YELLOW, "HALL OF FAME", 1);
      char buf[64];
      snprintf(buf, sizeof(buf), "PIT %dx%dx%d  %s", sm->GetPitWidth(), sm->GetPitHeight(),
               sm->GetPitDepth(), sm->GetBlockSetName());
      r_print(200.0f, 26.0f, 1, UI_LABEL, buf, 1);
      drawScoreTable(all, sel, -1, NULL, 0, now, 44.0f);
    }

    render_begin_bottom();
    ui_box(4.0f, 4.0f, 312.0f, 232.0f, UI_FRAME, E_BLACK, " SCORE DETAILS ", E_YELLOW);
    SCOREREC *r = &all[sel];
    char buf[64];
    snprintf(buf, sizeof(buf), "%d. %s", sel + 1, r->name[0] ? r->name : "..........");
    r_print(160.0f, 20.0f, 1, E_WHITE, buf, 1);
    snprintf(buf, sizeof(buf), "%ld", (long)r->score);
    r_print(160.0f, 36.0f, 2, r->score ? E_YELLOW : E_DGRAY, buf, 1);
    snprintf(buf, sizeof(buf), "%ld", (long)r->nbCube);
    ui_kv(24.0f, 64.0f, 272.0f, "CUBES PLAYED", buf, UI_LABEL, E_WHITE);
    snprintf(buf, sizeof(buf), "%ld", (long)r->startLevel);
    ui_kv(24.0f, 78.0f, 272.0f, "START LEVEL", buf, UI_LABEL, E_WHITE);
    snprintf(buf, sizeof(buf), "%d:%02d", (int)r->gameTime / 60, (int)r->gameTime % 60);
    ui_kv(24.0f, 92.0f, 272.0f, "TIME", buf, UI_LABEL, E_WHITE);
    ui_kv(24.0f, 106.0f, 272.0f, "DATE", fmtDate(r->date), UI_LABEL, E_WHITE);
    static const char *ln[5] = { "1 LAYER", "2 LAYERS", "3 LAYERS", "4 LAYERS", "5 LAYERS" };
    const int32_t *v[5] = { &r->nbLine1, &r->nbLine2, &r->nbLine3, &r->nbLine4, &r->nbLine5 };
    for (int i = 0; i < 5; i++) {
      snprintf(buf, sizeof(buf), "%ld", (long)*v[i]);
      ui_kv(24.0f, 126.0f + i * 14.0f, 272.0f, ln[i], buf, UI_LABEL, E_WHITE);
    }
    ui_hline(12.0f, 200.0f, 308.0f, E_BLUE);
    ui_key_hint(24.0f, 210.0f, "UP/DOWN", "BROWSE");
    ui_key_hint(180.0f, 210.0f, "B", "BACK");
    render_swap(true);
  }
}

/* ------------------------------------------------------------------ */
/* Pause menu                                                          */
/* ------------------------------------------------------------------ */

int runPauseMenu(Game *game) {
  static const char *rows[3] = { "RESUME", "RESTART", "QUIT TO MENU" };
  int res = -1, sel = 0;
  float t0 = sNow();
  audio_music_duck(true);

  while (aptMainLoop() && res == -1) {
    poll();
    float now = sNow() - t0;
    if (repeatKey(KEY_UP | KEY_CPAD_UP))     { sel = (sel + 2) % 3; audio_tchh(); }
    if (repeatKey(KEY_DOWN | KEY_CPAD_DOWN)) { sel = (sel + 1) % 3; audio_tchh(); }
    for (int i = 0; i < 3; i++)
      if (sHit(60.0f, 90.0f + i * 22.0f, 200.0f, 18.0f)) { sel = i; res = i + 1; audio_wozz(); }
    if (pDown & KEY_A)               { audio_wozz(); res = sel + 1; }
    if (pDown & (KEY_B | KEY_START)) { audio_tchh(); res = 1; }
    if (pDown & KEY_SELECT)          { audio_tchh(); res = 0; }
    if (AUTOTEST && now > 2.5f) res = 1;

    render_clear(COL_BG);
    game->Render();
    render_begin_bottom();
    ui_fade(0.6f);
    ui_box(40.0f, 60.0f, 240.0f, 120.0f, UI_FRAME, E_BLACK, " PAUSE ", E_YELLOW);
    for (int i = 0; i < 3; i++)
      ui_menu_row(60.0f, 90.0f + i * 22.0f, 200.0f, rows[i], i == sel, now);
    ui_key_hint(60.0f, 160.0f, "A", "OK");
    ui_key_hint(150.0f, 160.0f, "B", "RESUME");
    render_swap(true);
  }
  audio_music_duck(false);
  return (res == -1) ? 1 : res;
}

/* ------------------------------------------------------------------ */
/* Game over + hall of fame                                            */
/* ------------------------------------------------------------------ */

#define CHARN 39
static const char CHARSET[CHARN + 1] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";

int runGameOverScreen(Game *game, SetupManager *sm, SCOREREC *added, int recordPos) {
  SCOREREC all[10];
  memset(all, 0, sizeof(all));
  sm->GetHighScore(all);
  SCOREREC *sc = game->GetScore();

  /* previous name as a suggestion (like the original remembers the last one) */
  static char lastName[11] = "PLAYER";
  int editing = (added != NULL) ? 1 : 0;
  char editText[11];
  memset(editText, ' ', 10);
  editText[10] = '\0';
  memcpy(editText, lastName, strlen(lastName));
  int editPos = (int)strlen(lastName);
  if (editPos > 9) editPos = 9;

  int res = -1;
  float t0 = sNow();
  audio_music(MUS_OVER);

  while (aptMainLoop() && res == -1) {
    poll();
    float now = sNow() - t0;

    if (editing) {
      int ci = 0;
      for (; ci < CHARN; ci++) if (CHARSET[ci] == editText[editPos]) break;
      if (ci >= CHARN) ci = 0;
      if (repeatKey(KEY_UP | KEY_CPAD_UP))     { editText[editPos] = CHARSET[(ci + 1) % CHARN]; audio_tchh(); }
      if (repeatKey(KEY_DOWN | KEY_CPAD_DOWN)) { editText[editPos] = CHARSET[(ci + CHARN - 1) % CHARN]; audio_tchh(); }
      if (repeatKey(KEY_RIGHT | KEY_CPAD_RIGHT)) { if (editPos < 9) editPos++; audio_tchh(); }
      if (repeatKey(KEY_LEFT | KEY_CPAD_LEFT))   { if (editPos > 0) editPos--; audio_tchh(); }
      if (pDown & KEY_Y) {                        /* delete */
        for (int i = editPos; i < 9; i++) editText[i] = editText[i + 1];
        editText[9] = ' ';
        audio_blub();
      }
      if (pDown & KEY_X) {                        /* inserts a space */
        for (int i = 9; i > editPos; i--) editText[i] = editText[i - 1];
        editText[editPos] = ' ';
        audio_blub();
      }
      if ((pDown & (KEY_A | KEY_START)) || (AUTOTEST && now > 3.0f)) {
        /* name without trailing spaces */
        char nm[11];
        memcpy(nm, editText, 11);
        for (int i = 9; i >= 0 && nm[i] == ' '; i--) nm[i] = '\0';
        memcpy(added->name, nm, 10);
        added->name[10] = '\0';
        strncpy(lastName, nm, 10);
        lastName[10] = '\0';
        sm->SaveHighScore();
        memset(all, 0, sizeof(all));
        sm->GetHighScore(all);
        editing = 0;
        audio_wozz();
      }
    } else {
      if (pDown & (KEY_A | KEY_START))  { audio_wozz(); res = 1; }
      if (AUTOTEST && now > 6.0f) res = 2;
      if (pDown & KEY_B)                { audio_tchh(); res = 2; }
      if (pDown & KEY_SELECT)           { audio_tchh(); res = 0; }
      if (sHit(16.0f, 206.0f, 90.0f, 24.0f))  { audio_wozz(); res = 1; }
      if (sHit(115.0f, 206.0f, 90.0f, 24.0f)) { audio_wozz(); res = 2; }
      if (sHit(214.0f, 206.0f, 90.0f, 24.0f)) { audio_tchh(); res = 0; }
    }

    render_clear(COL_BG);
    for (int eye = 0; eye < 2; eye++) {
      render_begin_top(eye);
      uint32_t gc = ui_blink(now, 0.8f) ? E_LRED : E_YELLOW;
      r_print(200.0f, 6.0f, 2, gc, "GAME OVER", 1);
      char buf[64];
      snprintf(buf, sizeof(buf), "HALL OF FAME  PIT %dx%dx%d  %s", sm->GetPitWidth(),
               sm->GetPitHeight(), sm->GetPitDepth(), sm->GetBlockSetName());
      r_print(200.0f, 26.0f, 1, UI_LABEL, buf, 1);
      drawScoreTable(all, added ? recordPos : -1, editing ? recordPos : -1,
                     editing ? editText : NULL, editPos, now, 44.0f);
    }

    render_begin_bottom();
    ui_box(4.0f, 4.0f, 312.0f, 232.0f, UI_FRAME, E_BLACK, " RESULTS ", E_YELLOW);
    char buf[64];
    r_print(160.0f, 18.0f, 1, UI_LABEL, "SCORE", 1);
    snprintf(buf, sizeof(buf), "%ld", (long)sc->score);
    r_print(160.0f, 30.0f, 3, E_YELLOW, buf, 1);
    snprintf(buf, sizeof(buf), "%ld", (long)sc->nbCube);
    ui_kv(24.0f, 62.0f, 272.0f, "CUBES PLAYED", buf, UI_LABEL, E_WHITE);
    snprintf(buf, sizeof(buf), "%ld", (long)(sc->nbLine1 + 2 * sc->nbLine2 + 3 * sc->nbLine3 +
                                             4 * sc->nbLine4 + 5 * sc->nbLine5));
    ui_kv(24.0f, 76.0f, 272.0f, "LAYERS CLEARED", buf, UI_LABEL, E_WHITE);
    snprintf(buf, sizeof(buf), "%d:%02d", (int)sc->gameTime / 60, (int)sc->gameTime % 60);
    ui_kv(24.0f, 90.0f, 272.0f, "TIME", buf, UI_LABEL, E_WHITE);

    if (editing) {
      r_rect(12.0f, 110.0f, 296.0f, 14.0f, UI_BAR);
      snprintf(buf, sizeof(buf), "NEW HIGH SCORE! RANK %d", recordPos + 1);
      r_print(160.0f, 113.0f, 1, ui_blink(now, 0.6f) ? E_YELLOW : E_WHITE, buf, 1);
      r_print(160.0f, 132.0f, 1, E_WHITE, "ENTER YOUR NAME", 1);
      r_print(160.0f, 146.0f, 2, E_YELLOW, editText, 1);
      if (ui_blink(now, 0.5f)) r_rect(80.0f + editPos * 16.0f, 163.0f, 16.0f, 2.0f, E_YELLOW);
      ui_key_hint(16.0f, 176.0f, "UP/DN", "LETTER");
      ui_key_hint(160.0f, 176.0f, "L/R", "MOVE");
      ui_key_hint(16.0f, 190.0f, "Y", "DELETE");
      ui_key_hint(104.0f, 190.0f, "X", "SPACE");
      ui_key_hint(192.0f, 190.0f, "A", "DONE");
    } else {
      if (added) {
        snprintf(buf, sizeof(buf), "%s - RANK %d", added->name, recordPos + 1);
        r_print(160.0f, 120.0f, 1, E_YELLOW, buf, 1);
      } else {
        r_print(160.0f, 120.0f, 1, UI_DIM, "NO HIGH SCORE THIS TIME", 1);
      }
      ui_key_hint(40.0f, 160.0f, "A", "PLAY AGAIN");
      ui_key_hint(40.0f, 174.0f, "B", "MAIN MENU");
      ui_key_hint(40.0f, 188.0f, "SELECT", "QUIT");
      static const char *btn[3] = { "RETRY", "MENU", "QUIT" };
      const float bx[3] = { 16.0f, 115.0f, 214.0f };
      for (int i = 0; i < 3; i++) {
        ui_frame(bx[i], 206.0f, 90.0f, 24.0f, UI_FRAME);
        r_print(bx[i] + 45.0f, 214.0f, 1, E_WHITE, btn[i], 1);
      }
    }
    render_swap(true);
  }
  return (res == -1) ? 2 : res;
}
