/*
  File:        render.cpp
  Description: 3DS software renderer (citro2D) for the BlockOut II port,
               with the look of the original MS-DOS BlockOut.

  The renderer reproduces the BlockOut II OpenGL pipeline:

   * gluPerspective(60.0, 1.0, 0.1, 10.0) projection applied in software
     (F_PROJ = 1/tan(30deg)) on a SQUARE viewport, like the original
     (0.7197*W x 0.9596*H at 4:3 = square): a non-square viewport with
     the projection's 1.0 aspect stretched the pit;
   * gluLookAt(0,0,0 -> 0,0,10, up 0,1,0) view matrix;
   * GL lighting: ambient = material.ambient, diffuse =
     material.diffuse * max(0,N.L), light in eye coordinates (-15,10,10);
   * unlit primitives (grid, cube edges, piece) in the material's
     AMBIENT color, like SetMaterial() with GL_LIGHTING off;
   * painter-style in orderMatrix order during the game (like
     the original, which drew without a z-buffer); in the GAME OVER orbit
     the original turned the z-buffer on: here the faces are sorted by
     depth (the orderMatrix order only holds for the fixed camera);
   * clipping against the near plane (NEARZ) and the pit rectangle.

  MS-DOS style: black background, 1-pixel green grid, falling piece as a
  white wireframe only, layers colored by depth, level column on the
  left and info column on the right, 8x8 bitmap font.

  Adaptations required by the 3DS hardware:

   * C2D_DrawTriangle/Line/RectSolid instead of OpenGL display lists;
   * parallel stereoscopy: both eyes share the view, the
     disparity is a horizontal shift proportional to (gConvZ/d - 1),
     zero at mid-pit, capped in pixels. No vertical parallax.

  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include <citro2d.h>
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "render.h"
#include "ui.h"
#include "Game.h"
#include "Pit.h"
#include "PolyCube.h"

/* libctru console font (8x8, 256 CP437 characters, 1 bit/pixel) */
extern "C" const u8 default_font_bin[];

/* ------------------------------------------------------------------ */
/* Constants of the original pipeline                                    */
/* ------------------------------------------------------------------ */

#define F_PROJ    1.7320508075688772f   /* 1 / tan(60/2 degrees), aspect 1.0 */
#define LIGHT_EX -15.0f
#define LIGHT_EY  10.0f
#define LIGHT_EZ  10.0f

/* C2D objects per FRAME (the citro2d vertex buffer is only emptied at the end of the
   frame): with two eyes, the bottom screen and a full pit the default 4096 is
   far exceeded and the excess primitives used to vanish. */
#define C2D_OBJECTS 24000

#define STEREO_MAXD 14.0f    /* cap on the total disparity (pixels, L-R) */

/* Top screen layout (400x240) */
#define PIT_X   44.0f
#define PIT_Y    4.0f
#define PIT_S  232.0f
#define COL_X  284.0f        /* info column: 284..396 */
#define COL_W  112.0f

/* ------------------------------------------------------------------ */
/* Global renderer state                                               */
/* ------------------------------------------------------------------ */

static C3D_RenderTarget *gLeft;
static C3D_RenderTarget *gRight;
static C3D_RenderTarget *gBottom;
static int               gScreen;            /* 0 = top, 1 = bottom */
static float             gStereoPx = 10.0f;   /* reference disparity */
static float             gEffPx = 10.0f;     /* effective for the frame */
static int               gEye;               /* 0 = left, 1 = right */
static float             gConvZ = 2.0f;      /* depth at zero disparity */

static float gPitX, gPitY, gPitW, gPitH;     /* pit viewport, top-left */
static float gFlash = 0.0f;                  /* grid flash 1..0 */
static int   gPieceFill = 0;

/* ------------------------------------------------------------------ */
/* Colors                                                               */
/* ------------------------------------------------------------------ */

uint32_t r_color(int r, int g, int b, int a) {
  return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static uint32_t scaleCol(uint32_t c, float k) {
  int r = (int)((c & 0xFF) * k), g = (int)(((c >> 8) & 0xFF) * k), b = (int)(((c >> 16) & 0xFF) * k);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;
  return r_color(r, g, b, 255);
}

/* ------------------------------------------------------------------ */
/* Primitivi 2D                                                         */
/* ------------------------------------------------------------------ */

void r_rect(float x, float y, float w, float h, uint32_t col) {
  if (w <= 0.0f || h <= 0.0f) return;
  C2D_DrawRectSolid(x, y, 0.5f, w, h, OPAQUE(col));
}

void r_rect_a(float x, float y, float w, float h, uint32_t col) {
  if (w <= 0.0f || h <= 0.0f) return;
  C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
}

void r_line(float x0, float y0, float x1, float y1, float thick, uint32_t col) {
  uint32_t c = OPAQUE(col);
  C2D_DrawLine(x0, y0, c, x1, y1, c, thick, 0.5f);
}

void r_line_a(float x0, float y0, float x1, float y1, float thick, uint32_t col) {
  C2D_DrawLine(x0, y0, col, x1, y1, col, thick, 0.5f);
}

void r_tri_a(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col) {
  C2D_DrawTriangle(x0, y0, col, x1, y1, col, x2, y2, col, 0.5f);
}

void r_quad_a(const float *p, uint32_t col) {
  C2D_DrawTriangle(p[0], p[1], col, p[2], p[3], col, p[4], p[5], col, 0.5f);
  C2D_DrawTriangle(p[0], p[1], col, p[4], p[5], col, p[6], p[7], col, 0.5f);
}

/* ------------------------------------------------------------------ */
/* Bitmap font                                                          */
/* ------------------------------------------------------------------ */
/* The 256 8x8 glyphs are copied once into a 128x128 RGBA8 texture
   (white + alpha) in the PICA's 8x8 "morton" tile format; the color
   comes from the tint. Nearest filter + integer coordinates = crisp pixels. */

/* One texture per color: the citro2d image tint is not applied
   (the text stayed white in every mode), so each requested color
   has its own copy of the font, created on first request. The UI
   only uses the EGA palette: a few 64 KB textures. */
#define FONT_CACHE 24
static C3D_Tex           gFontTex[FONT_CACHE];
static u32               gFontCol[FONT_CACHE];
static int               gFontNb = 0;
static Tex3DS_SubTexture gGlyph[256];
static int               gFontOk = 0;

static u32 morton8(u32 x, u32 y) {
  u32 i = 0;
  for (int b = 0; b < 3; b++) {
    i |= ((x >> b) & 1u) << (2 * b);
    i |= ((y >> b) & 1u) << (2 * b + 1);
  }
  return i;
}

static void fontInit(void) {
  for (int c = 0; c < 256; c++) {
    int gx = (c & 15) * 8, gy = (c >> 4) * 8;
    Tex3DS_SubTexture *s = &gGlyph[c];
    s->width  = 8;
    s->height = 8;
    s->left   = (float)gx / 128.0f;
    s->right  = (float)(gx + 8) / 128.0f;
    s->top    = 1.0f - (float)gy / 128.0f;
    s->bottom = 1.0f - (float)(gy + 8) / 128.0f;
  }
  gFontNb = 0;
  gFontOk = 1;
}

/* Font texture in the color col (C2D packing, alpha respected) */
static C3D_Tex *fontTex(u32 col) {
  for (int i = 0; i < gFontNb; i++) if (gFontCol[i] == col) return &gFontTex[i];
  int slot = gFontNb;
  if (slot >= FONT_CACHE) {             /* cache full: recycle the last one */
    slot = FONT_CACHE - 1;
    C3D_TexDelete(&gFontTex[slot]);
  } else gFontNb++;
  C3D_Tex *t = &gFontTex[slot];
  if (!C3D_TexInit(t, 128, 128, GPU_RGBA8)) { if (slot == gFontNb - 1) gFontNb--; return NULL; }
  /* PICA RGBA8: u32 = R<<24 | G<<16 | B<<8 | A */
  u32 texel = ((col & 0xFF) << 24) | (((col >> 8) & 0xFF) << 16) |
              (((col >> 16) & 0xFF) << 8) | ((col >> 24) & 0xFF);
  u32 *px = (u32 *)t->data;
  memset(px, 0, 128 * 128 * 4);
  for (int c = 0; c < 256; c++) {
    int gx = (c & 15) * 8, gy = (c >> 4) * 8;
    for (int r = 0; r < 8; r++) {
      u8 bits = default_font_bin[c * 8 + r];
      for (int x = 0; x < 8; x++) {
        if (!(bits & (0x80 >> x))) continue;
        u32 tx = (u32)(gx + x);
        u32 ty = (u32)(gy + r);            /* memory row 0 = v 1.0 (top) */
        px[((ty >> 3) * 16 + (tx >> 3)) * 64 + morton8(tx & 7, ty & 7)] = texel;
      }
    }
  }
  C3D_TexFlush(t);
  C3D_TexSetFilter(t, GPU_NEAREST, GPU_NEAREST);
  gFontCol[slot] = col;
  return t;
}

float r_print_w(const char *str, int scale) {
  if (scale < 1) scale = 1;
  return (float)(strlen(str) * FONT_W * scale);
}

static void printRaw(float x, float y, int scale, uint32_t col, const char *str, int align) {
  if (!gFontOk || !str) return;
  if (scale < 1) scale = 1;
  C3D_Tex *tex = fontTex(col);
  if (!tex) return;
  float w = r_print_w(str, scale);
  if (align == 1) x -= w * 0.5f;
  else if (align == 2) x -= w;
  x = floorf(x + 0.5f);
  y = floorf(y + 0.5f);
  float adv = (float)(FONT_W * scale);
  for (const unsigned char *p = (const unsigned char *)str; *p; p++, x += adv) {
    if (*p == ' ') continue;
    C2D_Image img = { tex, &gGlyph[*p] };
    C2D_DrawImageAt(img, x, y, 0.5f, NULL, (float)scale, (float)scale);
  }
}

void r_print(float x, float y, int scale, uint32_t col, const char *str, int align) {
  printRaw(x, y, scale, OPAQUE(col), str, align);
}

void r_print_a(float x, float y, int scale, uint32_t col, const char *str, int align) {
  printRaw(x, y, scale, col, str, align);
}

void r_print_sh(float x, float y, int scale, uint32_t col, const char *str, int align) {
  float o = (float)(scale < 1 ? 1 : scale);
  printRaw(x + o, y + o, scale, OPAQUE(COL_BLACK), str, align);
  printRaw(x, y, scale, OPAQUE(col), str, align);
}

void r_char(float x, float y, int scale, uint32_t col, unsigned char c) {
  char s[2] = { (char)c, 0 };
  printRaw(x, y, scale, OPAQUE(col), s, 0);
}

int r_screen_w(void) { return (gScreen == 0) ? 400 : 320; }
int r_is_bottom(void) { return gScreen; }
int r_eye(void) { return gEye; }
/* effective disparity: chosen value (ZR / Setup) scaled by the 3D slider */
float r_stereo_px(void) { return gStereoPx * osGet3DSliderState(); }

/* ------------------------------------------------------------------ */
/* Frame                                                                */
/* ------------------------------------------------------------------ */

void render_init(void) {
  gfxSet3D(true);
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
  C2D_Init(C2D_OBJECTS);
  C2D_Prepare();
  gLeft   = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  gRight  = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
  gBottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  fontInit();
  gScreen = 0;
}

void render_exit(void) {
  for (int i = 0; i < gFontNb; i++) C3D_TexDelete(&gFontTex[i]);
  gFontNb = 0;
  gFontOk = 0;
  C2D_Fini();
  C3D_Fini();
}

void render_set_stereo(float px) { gStereoPx = px; }
void render_set_piece_fill(int on) { gPieceFill = on; }
int  render_get_piece_fill(void) { return gPieceFill; }

void render_clear(uint32_t col) {
  col = OPAQUE(col);
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  C2D_TargetClear(gLeft, col);
  C2D_TargetClear(gRight, col);
  C2D_TargetClear(gBottom, col);
}

void render_begin_top(int eye) {
  gScreen = 0;
  gEye = eye ? 1 : 0;
  C2D_SceneBegin(eye ? gRight : gLeft);
}

void render_begin_bottom(void) {
  gScreen = 1;
  gEye = 0;
  C2D_SceneBegin(gBottom);
}

void render_flush(void) { C2D_Flush(); }

void render_swap(int waitVsync) {
  (void)waitVsync;
  gScreen = 0;
  C2D_Flush();
  C3D_FrameEnd(0);
}

/* ------------------------------------------------------------------ */
/* 2D clipping against the pit viewport                                */
/* ------------------------------------------------------------------ */

typedef struct { float x, y; uint32_t c; } V2C;

/* Sutherland-Hodgman against the rectangle; t is clamped to [0,1]: with an
   edge nearly parallel to the border roundoff would blow it up and the
   vertex would end up at 1e12 ("giant strip" on the PICA). */
static int clipPoly(const V2C *poly, int n, V2C *out,
                    float x0, float y0, float x1, float y1) {
  V2C bufA[40], bufB[40];
  int na = 0;
  for (int i = 0; i < n && na < 40; i++) bufA[na++] = poly[i];
  const float edges[4] = { x0, y1, x1, y0 };
  const int axis[4]    = { 0, 1, 0, 1 };
  const int keepLess[4]= { 0, 1, 1, 0 };
  for (int p = 0; p < 4; p++) {
    float e = edges[p];
    int ax = axis[p];
    int nd = 0;
    for (int i = 0; i < na; i++) {
      V2C a = bufA[i];
      V2C b = bufA[(i + 1) % na];
      float va = ax ? a.y : a.x;
      float vb = ax ? b.y : b.x;
      int ia = keepLess[p] ? (va <= e) : (va >= e);
      int ib = keepLess[p] ? (vb <= e) : (vb >= e);
      if (ia && nd < 40) bufB[nd++] = a;
      if (ia != ib && nd < 40) {
        float denom = vb - va;
        if (denom != 0.0f) {
          float t = (e - va) / denom;
          if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
          V2C m;
          m.x = a.x + t * (b.x - a.x);
          m.y = a.y + t * (b.y - a.y);
          m.c = a.c;
          bufB[nd++] = m;
        }
      }
    }
    na = nd;
    for (int i = 0; i < na; i++) bufA[i] = bufB[i];
    if (na < 3) return 0;
  }
  for (int i = 0; i < na; i++) out[i] = bufA[i];
  return na;
}

/* Line with segment-rectangle clipping (Liang-Barsky) */
static void drawLineClip(float x0, float y0, uint32_t c0,
                         float x1, float y1, uint32_t c1, float thick) {
  float dx = x1 - x0, dy = y1 - y0;
  float t0 = 0.0f, t1 = 1.0f;
  float L = gPitX, R = gPitX + gPitW;
  float T = gPitY, B = gPitY + gPitH;
  float p[4] = { -dx, dx, -dy, dy };
  float q[4] = { x0 - L, R - x0, y0 - T, B - y0 };
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0.0f) {
      if (q[i] < 0.0f) return;
    } else {
      float r = q[i] / p[i];
      if (p[i] < 0.0f) { if (r > t1) return; if (r > t0) t0 = r; }
      else             { if (r < t0) return; if (r < t1) t1 = r; }
    }
  }
  C2D_DrawLine(x0 + t0 * dx, y0 + t0 * dy, c0, x0 + t1 * dx, y0 + t1 * dy, c1, thick, 0.5f);
}

/* ------------------------------------------------------------------ */
/* Matrici                                                              */
/* ------------------------------------------------------------------ */

static float gMV[16];        /* view * model  (column-major) */
static float gView[16];      /* current view */

static void setModel(const float *m) {
  if (!m) { memcpy(gMV, gView, sizeof(gMV)); return; }
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++) s += gView[k * 4 + r] * m[c * 4 + k];
      gMV[c * 4 + r] = s;
    }
}

#define NEARZ 0.1f   /* gluPerspective(60, 1, 0.1, FAR) */

typedef struct { float x, y, z; uint32_t c; } EV;

static int clipNear(const EV *poly, int n, EV *out) {
  int nd = 0;
  for (int i = 0; i < n; i++) {
    EV a = poly[i];
    EV b = poly[(i + 1 == n) ? 0 : i + 1];
    int ka = (-a.z >= NEARZ), kb = (-b.z >= NEARZ);
    if (ka != kb) {
      float dd = a.z - b.z;
      if (dd != 0.0f && nd < 16) {
        float t = (NEARZ + a.z) / dd;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        EV m;
        m.x = a.x + t * (b.x - a.x);
        m.y = a.y + t * (b.y - a.y);
        m.z = a.z + t * (b.z - a.z);
        m.c = a.c;
        out[nd++] = m;
      }
    }
    if (kb && nd < 16) out[nd++] = b;
  }
  return nd;
}

static int projEye(float ex, float ey, float ez, float *sx, float *sy) {
  float d = -ez;
  if (!(d >= NEARZ * 0.999f)) return 0;
  float nx = F_PROJ * ex / d;
  float ny = F_PROJ * ey / d;
  float x = gPitX + (nx * 0.5f + 0.5f) * gPitW;
  float y = gPitY + (0.5f - ny * 0.5f) * gPitH;
  /* positive disparity = behind the screen = uncrossed images:
     the left eye sees the point further left, the right eye further
     right (the sign used to be inverted and the pit looked "inside
     out": bottom in front, mouth behind) */
  float disp = gEffPx * (1.0f - gConvZ / d);
  if (disp > STEREO_MAXD) disp = STEREO_MAXD;
  else if (disp < -STEREO_MAXD) disp = -STEREO_MAXD;
  x += (gEye ? 0.5f : -0.5f) * disp;
  if (!(x > -1e6f && x < 1e6f)) x = 0.0f;
  if (!(y > -1e6f && y < 1e6f)) y = 0.0f;
  *sx = x; *sy = y;
  return 1;
}

static void eyeNormalM(float nx, float ny, float nz, float *ox, float *oy, float *oz) {
  *ox = gMV[0] * nx + gMV[4] * ny + gMV[8]  * nz;
  *oy = gMV[1] * nx + gMV[5] * ny + gMV[9]  * nz;
  *oz = gMV[2] * nx + gMV[6] * ny + gMV[10] * nz;
}

static void eyePoint(float wx, float wy, float wz, float *ox, float *oy, float *oz) {
  *ox = gMV[0] * wx + gMV[4] * wy + gMV[8]  * wz + gMV[12];
  *oy = gMV[1] * wx + gMV[5] * wy + gMV[9]  * wz + gMV[13];
  *oz = gMV[2] * wx + gMV[6] * wy + gMV[10] * wz + gMV[14];
}

/* Polygon already in EYE coordinates: near-clip, projection, 2D clip */
static void drawPolyE(const EV *poly, int n) {
  EV clip[16];
  int m = clipNear(poly, n, clip);
  if (m < 3) return;
  V2C scr[16];
  int ns = 0;
  for (int i = 0; i < m; i++) {
    float sx, sy;
    if (!projEye(clip[i].x, clip[i].y, clip[i].z, &sx, &sy)) continue;
    scr[ns].x = sx; scr[ns].y = sy; scr[ns].c = clip[i].c; ns++;
  }
  if (ns < 3) return;
  V2C out[40];
  int k = clipPoly(scr, ns, out, gPitX, gPitY, gPitX + gPitW, gPitY + gPitH);
  for (int i = 1; i < k - 1; i++)
    C2D_DrawTriangle(out[0].x, out[0].y, out[0].c,
                     out[i].x, out[i].y, out[i].c,
                     out[i + 1].x, out[i + 1].y, out[i + 1].c, 0.5f);
}

/* Poligono in coordinate MONDO */
static void drawPolyW(const float v[][3], int n, uint32_t col) {
  if (n < 3 || n > 8) return;
  EV poly[8];
  for (int i = 0; i < n; i++) {
    eyePoint(v[i][0], v[i][1], v[i][2], &poly[i].x, &poly[i].y, &poly[i].z);
    poly[i].c = col;
  }
  drawPolyE(poly, n);
}

/* Segment in WORLD coordinates: near-clip, projection, 2D clip */
static void drawLineW(float ax, float ay, float az, float bx, float by, float bz,
                      uint32_t col, float thick) {
  float ax0, ay0, az0, bx0, by0, bz0;
  eyePoint(ax, ay, az, &ax0, &ay0, &az0);
  eyePoint(bx, by, bz, &bx0, &by0, &bz0);
  float da = -az0, db = -bz0;
  float tlo = 0.0f, thi = 1.0f;
  if (da < NEARZ || db < NEARZ) {
    float dd = db - da;
    if (dd == 0.0f) return;
    float t = (NEARZ - da) / dd;
    if (dd > 0.0f) { if (t > tlo) tlo = t; } else { if (t < thi) thi = t; }
    if (!(tlo < thi)) return;
  }
  float sx0, sy0, sx1, sy1;
  if (!projEye(ax0 + tlo * (bx0 - ax0), ay0 + tlo * (by0 - ay0), az0 + tlo * (bz0 - az0), &sx0, &sy0)) return;
  if (!projEye(ax0 + thi * (bx0 - ax0), ay0 + thi * (by0 - ay0), az0 + thi * (bz0 - az0), &sx1, &sy1)) return;
  drawLineClip(sx0, sy0, col, sx1, sy1, col, thick);
}

static uint32_t shadeM(float wx, float wy, float wz, float nx, float ny, float nz,
                       const GLMATERIAL *mat, int alpha) {
  float ex, ey, ez; eyePoint(wx, wy, wz, &ex, &ey, &ez);
  float lx = LIGHT_EX - ex, ly = LIGHT_EY - ey, lz = LIGHT_EZ - ez;
  float ll = sqrtf(lx * lx + ly * ly + lz * lz);
  if (ll > 0.0f) { lx /= ll; ly /= ll; lz /= ll; }
  float nex, ney, nez; eyeNormalM(nx, ny, nz, &nex, &ney, &nez);
  float nl = nex * lx + ney * ly + nez * lz;
  if (nl < 0.0f) nl = 0.0f;
  int ri = (int)((mat->Ambient.r + mat->Diffuse.r * nl) * 255.0f);
  int gi = (int)((mat->Ambient.g + mat->Diffuse.g * nl) * 255.0f);
  int bi = (int)((mat->Ambient.b + mat->Diffuse.b * nl) * 255.0f);
  if (ri > 255) ri = 255;
  if (gi > 255) gi = 255;
  if (bi > 255) bi = 255;
  if (ri < 0) ri = 0;
  if (gi < 0) gi = 0;
  if (bi < 0) bi = 0;
  return r_color(ri, gi, bi, alpha);
}

static uint32_t ambColor(const GLMATERIAL *mat) {
  int ri = (int)(mat->Ambient.r * 255.0f), gi = (int)(mat->Ambient.g * 255.0f);
  int bi = (int)(mat->Ambient.b * 255.0f);
  if (ri > 255) ri = 255;
  if (gi > 255) gi = 255;
  if (bi > 255) bi = 255;
  return r_color(ri < 0 ? 0 : ri, gi < 0 ? 0 : gi, bi < 0 ? 0 : bi, 255);
}

/* Back-face culling in eye coordinates */
static int faceVisible(float wx, float wy, float wz, float nx, float ny, float nz) {
  float ex, ey, ez; eyePoint(wx, wy, wz, &ex, &ey, &ez);
  float nex, ney, nez; eyeNormalM(nx, ny, nz, &nex, &ney, &nez);
  return (nex * -ex + ney * -ey + nez * -ez) > 0.0f;
}

/* ------------------------------------------------------------------ */
/* Cube geometry                                                        */
/* ------------------------------------------------------------------ */

static const float cubeFaceV[6][4][3] = {
  { {0,1,0},{1,1,0},{1,0,0},{0,0,0} },   /* n=(0,0,-1) */
  { {1,1,0},{1,1,1},{1,0,1},{1,0,0} },   /* n=(1,0,0)  */
  { {1,1,1},{0,1,1},{0,0,1},{1,0,1} },   /* n=(0,0,1)  */
  { {0,1,1},{0,1,0},{0,0,0},{0,0,1} },   /* n=(-1,0,0) */
  { {1,0,0},{1,0,1},{0,0,1},{0,0,0} },   /* n=(0,-1,0) */
  { {0,1,1},{1,1,1},{1,1,0},{0,1,0} }    /* n=(0,1,0)  */
};
static const float cubeFaceN[6][3] = {
  {0,0,-1}, {1,0,0}, {0,0,1}, {-1,0,0}, {0,-1,0}, {0,1,0}
};

/* Cube edges (lcubeList[12]) */
static const int cubeEdgeV[12][2][3] = {
  { {0,1,0},{1,1,0} }, { {1,1,0},{1,0,0} }, { {1,0,0},{0,0,0} }, { {0,0,0},{0,1,0} },
  { {0,1,0},{0,1,1} }, { {1,1,0},{1,1,1} }, { {1,0,0},{1,0,1} }, { {0,0,0},{0,0,1} },
  { {0,1,1},{1,1,1} }, { {1,1,1},{1,0,1} }, { {0,0,1},{1,0,1} }, { {0,0,1},{0,1,1} }
};

/* ------------------------------------------------------------------ */
/* Pit                                                                */
/* ------------------------------------------------------------------ */

/* Black frame around the opening (sideList of Pit::CreateSide) */
static void drawSideRing(Pit *pit) {
  uint32_t blk = r_color(0, 0, 0, 255);
  VERTEX org = pit->GetOrigin();
  float ox = org.x, oy = org.y, oz = org.z;
  float fW = pit->GetFWidth(), fH = pit->GetFHeight(), cS = pit->GetCubeSide();
  float rx0 = ox - cS, ry0 = oy - cS, rx1 = ox + fW + cS, ry1 = oy + fH + cS;
  const float pts[8][3][2] = {
    { {ox + fW, oy + fH}, {rx0, ry1}, {rx1, ry1} },
    { {ox + fW, oy + fH}, {ox,  oy + fH}, {rx0, ry1} },
    { {ox,      oy + fH}, {rx0, ry0}, {rx0, ry1} },
    { {ox,      oy + fH}, {ox,  oy},     {rx0, ry0} },
    { {ox,      oy},      {rx1, ry0}, {rx0, ry0} },
    { {ox,      oy},      {ox + fW, oy}, {rx1, ry0} },
    { {rx1,     ry0},     {ox + fW, oy}, {ox + fW, oy + fH} },
    { {rx1,     ry0},     {ox + fW, oy + fH}, {rx1, ry1} },
  };
  for (int t = 0; t < 8; t++) {
    float tri[3][3];
    for (int k = 0; k < 3; k++) { tri[k][0] = pts[t][k][0]; tri[k][1] = pts[t][k][1]; tri[k][2] = oz; }
    drawPolyW(tri, 3, blk);
  }
}

/* 5 inner faces of the pit (CLASSIC backList) */
static void drawBack(Pit *pit, GLMATERIAL *backMat) {
  VERTEX org = pit->GetOrigin();
  float ox = org.x, oy = org.y, oz = org.z;
  float fW = pit->GetFWidth(), fH = pit->GetFHeight(), fD = pit->GetFDepth();
  const float n[5][3] = { {1,0,0}, {0,-1,0}, {-1,0,0}, {0,1,0}, {0,0,-1} };
  const float v[5][4][3] = {
    { {ox, oy+fH, oz}, {ox, oy+fH, oz+fD}, {ox, oy, oz+fD}, {ox, oy, oz} },
    { {ox, oy+fH, oz}, {ox+fW, oy+fH, oz}, {ox+fW, oy+fH, oz+fD}, {ox, oy+fH, oz+fD} },
    { {ox+fW, oy+fH, oz+fD}, {ox+fW, oy+fH, oz}, {ox+fW, oy, oz}, {ox+fW, oy, oz+fD} },
    { {ox+fW, oy, oz}, {ox, oy, oz}, {ox, oy, oz+fD}, {ox+fW, oy, oz+fD} },
    { {ox, oy+fH, oz+fD}, {ox+fW, oy+fH, oz+fD}, {ox+fW, oy, oz+fD}, {ox, oy, oz+fD} },
  };
  for (int f = 0; f < 5; f++) {
    float cx = 0, cy = 0, cz = 0;
    for (int k = 0; k < 4; k++) { cx += v[f][k][0]; cy += v[f][k][1]; cz += v[f][k][2]; }
    cx *= 0.25f; cy *= 0.25f; cz *= 0.25f;
    if (!faceVisible(cx, cy, cz, n[f][0], n[f][1], n[f][2])) continue;
    drawPolyW(v[f], 4, shadeM(cx, cy, cz, n[f][0], n[f][1], n[f][2], backMat, 255));
  }
}

/* 1-pixel green grid, like the DOS screen: walls (longitudinal
   lines), rings at every level and the bottom grid. */
static void drawGrid(Pit *pit, uint32_t gridCol) {
  VERTEX org = pit->GetOrigin();
  float ox = org.x, oy = org.y, oz = org.z;
  float cS = pit->GetCubeSide(), fD = pit->GetFDepth();
  int width = pit->GetWidth(), height = pit->GetHeight(), depth = pit->GetDepth();

  for (int i = 0; i <= width; i++) {
    drawLineW(ox + i * cS, -oy, oz, ox + i * cS, -oy, oz + fD, gridCol, 1.0f);
    drawLineW(ox + i * cS,  oy, oz, ox + i * cS,  oy, oz + fD, gridCol, 1.0f);
  }
  for (int i = 1; i < height; i++) {
    drawLineW( ox, oy + i * cS, oz,  ox, oy + i * cS, oz + fD, gridCol, 1.0f);
    drawLineW(-ox, oy + i * cS, oz, -ox, oy + i * cS, oz + fD, gridCol, 1.0f);
  }
  for (int i = 0; i <= depth; i++) {
    float z = oz + i * cS;
    drawLineW( ox, -oy, z, -ox, -oy, z, gridCol, 1.0f);
    drawLineW(-ox, -oy, z, -ox,  oy, z, gridCol, 1.0f);
    drawLineW(-ox,  oy, z,  ox,  oy, z, gridCol, 1.0f);
    drawLineW( ox,  oy, z,  ox, -oy, z, gridCol, 1.0f);
  }
  float zb = oz + fD;
  for (int i = 1; i < width; i++)
    drawLineW(ox + i * cS, -oy, zb, ox + i * cS, oy, zb, gridCol, 1.0f);
  for (int i = 1; i < height; i++)
    drawLineW(ox, oy + i * cS, zb, -ox, oy + i * cS, zb, gridCol, 1.0f);
}

/* Edges of a pit cube: black, or grid green if the edge
   lies on a wall (Pit::RenderEdge switch, STYLE_CLASSIC) */
static int edgeGreen(int e, int x, int y, int z, int width, int height, int depth) {
  switch (e) {
    case 0: return y == height - 1;
    case 1: return x == width - 1;
    case 2: return y == 0;
    case 3: return x == 0;
    case 4: return (x == 0) || (y == height - 1);
    case 5: return (x == width - 1) || (y == height - 1);
    case 6: return (x == width - 1) || (y == 0);
    case 7: return (x == 0) || (y == 0);
    default: return z == depth - 1;
  }
}

static void drawCubeEdges(Pit *pit, int x, int y, int z, uint32_t gridCol, int all) {
  int width = pit->GetWidth(), height = pit->GetHeight(), depth = pit->GetDepth();
  VERTEX org = pit->GetOrigin();
  float cS = pit->GetCubeSide();
  int show[12];
  if (all) {
    for (int e = 0; e < 12; e++) show[e] = 1;
  } else {
    memset(show, 0, sizeof(show));
    for (int e = 0; e < 4; e++) show[e] = 1;
    if (x < width / 2)  { show[5] = 1; show[9] = 1; show[6] = 1; }
    if (x > width / 2)  { show[4] = 1; show[11] = 1; show[7] = 1; }
    if (y < height / 2) { show[4] = 1; show[8] = 1; show[5] = 1; }
    if (y > height / 2) { show[7] = 1; show[10] = 1; show[6] = 1; }
  }
  uint32_t blk = r_color(0, 0, 0, 255);
  for (int e = 0; e < 12; e++) {
    if (!show[e]) continue;
    const int *a = cubeEdgeV[e][0], *b = cubeEdgeV[e][1];
    drawLineW(org.x + (x + a[0]) * cS, org.y + (y + a[1]) * cS, org.z + (z + a[2]) * cS,
              org.x + (x + b[0]) * cS, org.y + (y + b[1]) * cS, org.z + (z + b[2]) * cS,
              edgeGreen(e, x, y, z, width, height, depth) ? gridCol : blk, 1.0f);
  }
}

static void cubeFace(Pit *pit, int x, int y, int z, int f, float vs[4][3], float *c) {
  VERTEX org = pit->GetOrigin();
  float cS = pit->GetCubeSide();
  c[0] = c[1] = c[2] = 0.0f;
  for (int k = 0; k < 4; k++) {
    vs[k][0] = org.x + (x + cubeFaceV[f][k][0]) * cS;
    vs[k][1] = org.y + (y + cubeFaceV[f][k][1]) * cS;
    vs[k][2] = org.z + (z + cubeFaceV[f][k][2]) * cS;
    c[0] += vs[k][0]; c[1] += vs[k][1]; c[2] += vs[k][2];
  }
  c[0] *= 0.25f; c[1] *= 0.25f; c[2] *= 0.25f;
}

/* Pit cubes during the game: orderMatrix order (original) */
static void drawPitCubes(Pit *pit, uint32_t gridCol) {
  const BLOCKITEM *om = pit->GetOrderMatrix();
  int mSize = pit->GetMatrixSize();
  for (int i = 0; i < mSize; i++) {
    int x = om[i].x, y = om[i].y, z = om[i].z;
    if (!pit->GetValue(x, y, z)) continue;
    if (!pit->IsVisible(x, y, z)) continue;
    /* GetMaterial returns a pointer to a static: copy per cell */
    const GLMATERIAL m = *pit->GetMaterial(z);
    for (int f = 0; f < 6; f++) {
      float vs[4][3], c[3];
      cubeFace(pit, x, y, z, f, vs, c);
      const float *n = cubeFaceN[f];
      if (!faceVisible(c[0], c[1], c[2], n[0], n[1], n[2])) continue;
      drawPolyW(vs, 4, shadeM(c[0], c[1], c[2], n[0], n[1], n[2], &m, 255));
    }
    drawCubeEdges(pit, x, y, z, gridCol, 0);
  }
}

/* GAME OVER orbit: the original turned the z-buffer on. Here the visible
   faces of all the cubes are collected and drawn from the farthest
   to the nearest, each with its own outline. */
typedef struct { float d; short x, y, z; unsigned char f; } SortFace;
static SortFace gFaces[7 * 7 * 18 * 3];

static int cmpFace(const void *a, const void *b) {
  float da = ((const SortFace *)a)->d, db = ((const SortFace *)b)->d;
  return (da < db) ? 1 : (da > db) ? -1 : 0;
}

static void drawPitCubesSorted(Pit *pit, uint32_t gridCol) {
  int width = pit->GetWidth(), height = pit->GetHeight(), depth = pit->GetDepth();
  int nf = 0;
  const int maxF = (int)(sizeof(gFaces) / sizeof(gFaces[0]));
  for (int z = 0; z < depth; z++)
    for (int y = 0; y < height; y++)
      for (int x = 0; x < width; x++) {
        if (!pit->GetValue(x, y, z)) continue;
        for (int f = 0; f < 6 && nf < maxF; f++) {
          static const int nb[6][3] = { {0,0,-1}, {1,0,0}, {0,0,1}, {-1,0,0}, {0,-1,0}, {0,1,0} };
          int ax = x + nb[f][0], ay = y + nb[f][1], az = z + nb[f][2];
          if (ax >= 0 && ax < width && ay >= 0 && ay < height && az >= 0 && az < depth &&
              pit->GetValue(ax, ay, az)) continue;          /* inner face */
          float vs[4][3], c[3];
          cubeFace(pit, x, y, z, f, vs, c);
          const float *n = cubeFaceN[f];
          if (!faceVisible(c[0], c[1], c[2], n[0], n[1], n[2])) continue;
          float ex, ey, ez; eyePoint(c[0], c[1], c[2], &ex, &ey, &ez);
          gFaces[nf].d = ex * ex + ey * ey + ez * ez;
          gFaces[nf].x = (short)x; gFaces[nf].y = (short)y; gFaces[nf].z = (short)z;
          gFaces[nf].f = (unsigned char)f;
          nf++;
        }
      }
  qsort(gFaces, nf, sizeof(SortFace), cmpFace);
  static const int faceEdges[6][4] = {
    {0,1,2,3}, {5,9,6,1}, {8,11,10,9}, {4,3,7,11}, {6,10,7,2}, {8,5,0,4}
  };
  uint32_t blk = r_color(0, 0, 0, 255);
  VERTEX org = pit->GetOrigin();
  float cS = pit->GetCubeSide();
  for (int i = 0; i < nf; i++) {
    int x = gFaces[i].x, y = gFaces[i].y, z = gFaces[i].z, f = gFaces[i].f;
    const GLMATERIAL m = *pit->GetMaterial(z);
    float vs[4][3], c[3];
    cubeFace(pit, x, y, z, f, vs, c);
    const float *n = cubeFaceN[f];
    drawPolyW(vs, 4, shadeM(c[0], c[1], c[2], n[0], n[1], n[2], &m, 255));
    for (int k = 0; k < 4; k++) {
      int e = faceEdges[f][k];
      const int *a = cubeEdgeV[e][0], *b = cubeEdgeV[e][1];
      drawLineW(org.x + (x + a[0]) * cS, org.y + (y + a[1]) * cS, org.z + (z + a[2]) * cS,
                org.x + (x + b[0]) * cS, org.y + (y + b[1]) * cS, org.z + (z + b[2]) * cS,
                edgeGreen(e, x, y, z, width, height, depth) ? gridCol : blk, 1.0f);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Current piece                                                       */
/* ------------------------------------------------------------------ */

/* PolyCube::Render(): the falling piece is ONLY the wireframe (lineList, unlit,
   white or red when rotation is blocked), like in
   DOS BlockOut. The semi-transparent fill is optional (Setup). */
static void drawPieceCubes(PolyCube *pc, int redMode, int bigEdge) {
  int nb = pc->GetNbCube();
  BLOCKITEM *cubes = pc->GetCubes();
  float cS = pc->GetCubeSide();
  VERTEX org = pc->GetOrigin();

  if (gPieceFill) {
    GLMATERIAL fm;
    memset(&fm, 0, sizeof(fm));
    fm.Ambient.r = 0.45f;
    fm.Ambient.g = fm.Ambient.b = redMode ? 0.0f : 0.45f;
    fm.Diffuse.r = 0.5f;
    fm.Diffuse.g = fm.Diffuse.b = redMode ? 0.0f : 0.5f;
    for (int ci = 0; ci < nb; ci++) {
      int x = cubes[ci].x, y = cubes[ci].y, z = cubes[ci].z;
      for (int f = 0; f < 6; f++) {
        float vs[4][3], c[3] = { 0, 0, 0 };
        for (int k = 0; k < 4; k++) {
          vs[k][0] = org.x + (x + cubeFaceV[f][k][0]) * cS;
          vs[k][1] = org.y + (y + cubeFaceV[f][k][1]) * cS;
          vs[k][2] = org.z + (z + cubeFaceV[f][k][2]) * cS;
          c[0] += vs[k][0]; c[1] += vs[k][1]; c[2] += vs[k][2];
        }
        c[0] *= 0.25f; c[1] *= 0.25f; c[2] *= 0.25f;
        const float *n = cubeFaceN[f];
        if (!faceVisible(c[0], c[1], c[2], n[0], n[1], n[2])) continue;
        drawPolyW(vs, 4, shadeM(c[0], c[1], c[2], n[0], n[1], n[2], &fm, 70));
      }
    }
  }

  uint32_t lcol = redMode ? r_color(255, 0, 0, 255) : r_color(255, 255, 255, 255);
  int nE = pc->GetNbEdge();
  EDGE *ed = pc->GetEdges();
  float off = (bigEdge > 0) ? 0.5f : 0.0f;   /* bigEdgeList: cube centers */
  float th = (bigEdge > 0) ? 2.0f : 1.0f;
  for (int i = 0; i < nE; i++) {
    drawLineW((ed[i].p1.x + off) * cS + org.x, (ed[i].p1.y + off) * cS + org.y,
              (ed[i].p1.z + off) * cS + org.z,
              (ed[i].p2.x + off) * cS + org.x, (ed[i].p2.y + off) * cS + org.y,
              (ed[i].p2.z + off) * cS + org.z, lcol, th);
  }
}

/* Piece faces with ghostMaterial (transparent face > 0, and AI hint in
   practice): back faces first, then front ones. */
static void drawPieceGhost(PolyCube *pc, uint32_t colFront, uint32_t colBack) {
  int nb = pc->GetNbCube();
  BLOCKITEM *cubes = pc->GetCubes();
  float cS = pc->GetCubeSide();
  VERTEX org = pc->GetOrigin();
  for (int pass = 0; pass < 2; pass++) {
    for (int ci = 0; ci < nb; ci++) {
      int x = cubes[ci].x, y = cubes[ci].y, z = cubes[ci].z;
      int vis[6];
      vis[0] = !pc->FindCube(x, y, z - 1);
      vis[1] = !pc->FindCube(x + 1, y, z);
      vis[2] = !pc->FindCube(x, y, z + 1);
      vis[3] = !pc->FindCube(x - 1, y, z);
      vis[4] = !pc->FindCube(x, y - 1, z);
      vis[5] = !pc->FindCube(x, y + 1, z);
      for (int f = 0; f < 6; f++) {
        if (!vis[f]) continue;
        float vs[4][3], c[3] = { 0, 0, 0 };
        for (int k = 0; k < 4; k++) {
          vs[k][0] = org.x + (x + cubeFaceV[f][k][0]) * cS;
          vs[k][1] = org.y + (y + cubeFaceV[f][k][1]) * cS;
          vs[k][2] = org.z + (z + cubeFaceV[f][k][2]) * cS;
          c[0] += vs[k][0]; c[1] += vs[k][1]; c[2] += vs[k][2];
        }
        c[0] *= 0.25f; c[1] *= 0.25f; c[2] *= 0.25f;
        int front = faceVisible(c[0], c[1], c[2], cubeFaceN[f][0], cubeFaceN[f][1], cubeFaceN[f][2]);
        if ((pass == 0) == (front != 0)) continue;
        drawPolyW(vs, 4, front ? colFront : colBack);
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* Spark                                                                */
/* ------------------------------------------------------------------ */

static const float sparkTime = 0.5f;   /* identical to the constant in Game.cpp */

void drawSpark(Game *g) {
  float sTime = g->curTime - g->startSpark;
  if (sTime >= sparkTime || sTime < 0.0f) return;
  float ratio = sTime / sparkTime;
  int alpha = (int)(255.0f * (1.0f - ratio));
  if (alpha <= 0) return;

  /* StartSpark() saves the center in bottom-left coordinates of the
     sprite viewport (full screen) */
  float cx = g->sparkX;
  float cy = 240.0f - g->sparkY;
  float r  = g->sparkW * 0.5f * (0.55f + 0.45f * ratio);
  if (!isfinite(cx) || !isfinite(cy) || !isfinite(r)) return;
  if (cx < gPitX || cx > gPitX + gPitW || cy < gPitY || cy > gPitY + gPitH) return;
  if (r < 1.0f) return;
  if (r > 60.0f) r = 60.0f;

  /* 8-ray star (the original's sprite) */
  uint32_t c = r_color(255, 255, 255, alpha);
  uint32_t h = r_color(255, 255, 160, alpha / 2);
  for (int i = 0; i < 8; i++) {
    float a = (float)i * 0.7853981634f + ratio * 0.6f;
    float rr = (i & 1) ? r * 0.55f : r;
    C2D_DrawLine(cx, cy, c, cx + cosf(a) * rr, cy - sinf(a) * rr, h, (i & 1) ? 1.0f : 2.0f, 0.5f);
  }
  float cr = 2.0f + 3.0f * (1.0f - ratio);
  C2D_DrawRectSolid(cx - cr * 0.5f, cy - cr * 0.5f, 0.5f, cr, cr, c);
}

/* ------------------------------------------------------------------ */
/* Level column (Pit::RenderLevel)                                      */
/* ------------------------------------------------------------------ */
/* As in DOS: a narrow column to the left of the pit, one compartment
   for each depth level; occupied layers light up in the
   color of their level, from the bottom up. */

void drawPitLevel(Game *g) {
  Pit *pit = &g->thePit;
  int depth = pit->GetDepth();
  const float x0 = 12.0f, w = 22.0f;
  const float yTop = PIT_Y + 2.0f, yBot = PIT_Y + PIT_S - 2.0f;
  float cell = floorf((yBot - yTop) / (float)depth);
  if (cell > 16.0f) cell = 16.0f;
  float h = cell * depth;
  float y0 = yBot - h;

  uint32_t grid = ambColor(pit->GetGridMaterial());
  /* frame */
  r_rect(x0 - 3.0f, y0 - 3.0f, w + 6.0f, 1.0f, grid);
  r_rect(x0 - 3.0f, yBot + 2.0f, w + 6.0f, 1.0f, grid);
  r_rect(x0 - 3.0f, y0 - 3.0f, 1.0f, h + 6.0f, grid);
  r_rect(x0 + w + 2.0f, y0 - 3.0f, 1.0f, h + 6.0f, grid);

  for (int z = 0; z < depth; z++) {
    /* z = depth-1 is the bottom of the pit: lowest compartment */
    float yy = yBot - (float)(depth - z) * cell;
    if (!pit->IsLineEmpty(z)) {
      const GLMATERIAL m = *pit->GetMaterial(z);
      uint32_t c = ambColor(&m);
      r_rect(x0, yy + 1.0f, w, cell - 1.0f, c);
      r_rect(x0, yy + 1.0f, w, 1.0f, scaleCol(c, 1.45f));
      r_rect(x0, yy + cell - 1.0f, w, 1.0f, scaleCol(c, 0.55f));
    } else {
      r_rect(x0 + w * 0.5f - 1.0f, yy + cell * 0.5f, 2.0f, 1.0f, scaleCol(grid, 0.6f));
    }
  }
}

/* ------------------------------------------------------------------ */
/* Info column (Sprites::RenderInfo / RenderScore)                      */
/* ------------------------------------------------------------------ */

static void infoRow(float y, const char *label, const char *val, int vscale, uint32_t vc) {
  r_print(COL_X + 4.0f, y, 1, UI_LABEL, label, 0);
  r_print(COL_X + COL_W - 4.0f, y + 10.0f, vscale, vc, val, 2);
}

void renderHud(Game *g) {
  char buf[32];

  ui_box(COL_X, PIT_Y, COL_W, PIT_S, UI_FRAME, E_BLACK, NULL, 0);

  snprintf(buf, sizeof(buf), "%d", g->level);
  infoRow(9.0f, "LEVEL", buf, 2, E_WHITE);

  snprintf(buf, sizeof(buf), "%ld", (long)g->score.score);
  infoRow(42.0f, "SCORE", buf, (strlen(buf) <= 6) ? 2 : 1, UI_VALUE);

  snprintf(buf, sizeof(buf), "%ld", (long)g->score.nbCube);
  infoRow(76.0f, "CUBES PLAYED", buf, 1, UI_VALUE);

  snprintf(buf, sizeof(buf), "%ld", (long)g->highScore);
  infoRow(102.0f, "HIGH SCORE", buf, 1, UI_VALUE);

  snprintf(buf, sizeof(buf), "%dx%dx%d", g->thePit.GetWidth(),
           g->thePit.GetHeight(), g->thePit.GetDepth());
  infoRow(128.0f, "PIT", buf, 1, E_WHITE);

  infoRow(154.0f, "BLOCK SET", g->setupManager->GetBlockSetName(), 1, E_WHITE);

  /* progress toward the next level */
  r_print(COL_X + 4.0f, 182.0f, 1, UI_LABEL, "NEXT LEVEL", 0);
  int cpl = g->cubePerLevel > 0 ? g->cubePerLevel : 1;
  float frac = (g->level >= 10) ? 1.0f :
               (float)(g->score.nbCube - cpl * g->level) / (float)cpl;
  ui_bar(COL_X + 4.0f, 192.0f, COL_W - 8.0f, 8.0f, frac, E_LGREEN);

  /* state (the original's RenderDemo / RenderPractice), blinking */
  const char *mode = NULL;
  uint32_t mc = E_LGREEN;
  if (g->demoFlag)                     { mode = "DEMO"; mc = E_LMAGENTA; }
  else if (g->practiceFlag)            { mode = "PRACTICE"; mc = E_YELLOW; }
  if (g->gameMode == GAME_PAUSED)      { mode = "PAUSE"; mc = E_WHITE; }
  if (mode && (ui_blink(g->curTime, 1.0f) || g->gameMode == GAME_PAUSED)) {
    r_rect(COL_X + 4.0f, 208.0f, COL_W - 8.0f, 14.0f, E_BLUE);
    r_print(COL_X + COL_W * 0.5f, 211.0f, 1, mc, mode, 1);
  }
}

/* PAUSE / GAME OVER in the center of the pit (Sprites::RenderGameMode) */
void renderGameModeOverlay(Game *g) {
  float cx = PIT_X + PIT_S * 0.5f, cy = PIT_Y + PIT_S * 0.5f;
  if (g->gameMode == GAME_OVER) {
    ui_box(cx - 84.0f, cy - 18.0f, 168.0f, 36.0f, E_LRED, E_BLACK, NULL, 0);
    uint32_t c = ui_blink(g->curTime, 0.8f) ? E_YELLOW : E_LRED;
    r_print(cx, cy - 8.0f, 2, c, "GAME OVER", 1);
  } else if (g->gameMode == GAME_PAUSED) {
    ui_box(cx - 52.0f, cy - 18.0f, 104.0f, 36.0f, UI_FRAME, E_BLACK, NULL, 0);
    r_print(cx, cy - 8.0f, 2, E_WHITE, "PAUSE", 1);
  }
}

/* Practice: "Press [H] to see suggested position" in the first 3 seconds
   (Game::RenderPracticeHelp) */
void renderPracticeHelp(Game *g) {
  if (!g->practiceFlag || g->demoFlag || g->gameMode != GAME_PLAYING) return;
  if (g->curTime - g->startGameTime >= 3.0f) return;
  float cx = PIT_X + PIT_S * 0.5f, cy = PIT_Y + PIT_S * 0.5f;
  r_rect(cx - 104.0f, cy - 8.0f, 208.0f, 16.0f, E_BLUE);
  r_print(cx, cy - 4.0f, 1, E_WHITE, "PRESS ZL FOR A HINT", 1);
}

/* ------------------------------------------------------------------ */
/* Pit (top screen)                                                     */
/* ------------------------------------------------------------------ */

void renderPitView(Game *g, int eye) {
  Pit *pit = &g->thePit;
  const float *viewSrc = g->endAnimStarted ? g->pitMatrix : g->matView;

  /* zero disparity at the pit mouth (screen plane, like
     the HUD): the pit sinks behind the glass */
  gConvZ = pit->GetOrigin().z;
  gEffPx = (g->gameMode == GAME_OVER) ? 0.0f : r_stereo_px();
  gEye = eye ? 1 : 0;

  memcpy(gView, viewSrc, sizeof(gView));
  setModel(NULL);

  int orbit = g->endAnimStarted ? 1 : 0;
  int renderCube = orbit || g->gameMode != GAME_PAUSED;

  GLMATERIAL *backMat = pit->GetBackMaterial();
  uint32_t gridCol = ambColor(pit->GetGridMaterial());
  /* grid flash when layers are completed (green -> white) */
  if (gFlash > 0.0f) {
    float k = gFlash;
    int r = (int)((gridCol & 0xFF) + (255 - (int)(gridCol & 0xFF)) * k);
    int gg = (int)(((gridCol >> 8) & 0xFF) + (255 - (int)((gridCol >> 8) & 0xFF)) * k);
    int b = (int)(((gridCol >> 16) & 0xFF) + (255 - (int)((gridCol >> 16) & 0xFF)) * k);
    gridCol = r_color(r, gg, b, 255);
  }

  /* Pit::Render(STYLE_CLASSIC): sideList and backList only without z-buffer */
  if (!orbit) {
    drawSideRing(pit);
    drawBack(pit, backMat);
  }
  drawGrid(pit, gridCol);

  if (renderCube) {
    if (orbit) drawPitCubesSorted(pit, gridCol);
    else       drawPitCubes(pit, gridCol);
  }

  if (g->gameMode != GAME_PAUSED && g->gameMode != GAME_OVER) {
    PolyCube *pc = &g->allPolyCube[g->pIdx];
    setModel(g->matPiece);

    /* ghost (transparent face): trans = (ghost/FTRANS_MAX)*0.4 */
    if (pc->GetGhost()) {
      int a = (int)(0.4f * 255.0f * (float)g->transparent / (float)FTRANS_MAX);
      if (a < 30) a = 30;
      drawPieceGhost(pc, r_color(128, 128, 128, a), r_color(80, 80, 80, a / 2));
    }
    drawPieceCubes(pc, g->redMode != 0, g->lineWidth);

    /* practice: AI hint for 0.75 s */
    if (g->practiceFlag && g->startShowAI > 0.0f && (g->curTime - g->startShowAI) < 0.75f) {
      setModel(g->matAIPiece);
      drawPieceGhost(pc, r_color(80, 255, 80, 90), r_color(40, 140, 40, 60));
      int nE = pc->GetNbEdge();
      EDGE *ed = pc->GetEdges();
      float cS = pc->GetCubeSide();
      VERTEX org = pc->GetOrigin();
      for (int i = 0; i < nE; i++)
        drawLineW(ed[i].p1.x * cS + org.x, ed[i].p1.y * cS + org.y, ed[i].p1.z * cS + org.z,
                  ed[i].p2.x * cS + org.x, ed[i].p2.y * cS + org.y, ed[i].p2.z * cS + org.z,
                  r_color(85, 255, 85, 255), 1.0f);
    }

    if (g->startSpark != 0.0f) drawSpark(g);
  }
}

/* ------------------------------------------------------------------ */
/* Bottom screen during the game                                       */
/* ------------------------------------------------------------------ */

void renderBottomUI(Game *g) {
  char buf[64];
  const float cx = 160.0f;

  if (g->gameMode == GAME_OVER) {
    ui_box(4.0f, 4.0f, 312.0f, 232.0f, E_LRED, E_BLACK, " GAME OVER ", E_YELLOW);
    r_print(cx, 30.0f, 1, UI_LABEL, "FINAL SCORE", 1);
    snprintf(buf, sizeof(buf), "%ld", (long)g->score.score);
    r_print(cx, 46.0f, 3, UI_VALUE, buf, 1);
    snprintf(buf, sizeof(buf), "%ld", (long)g->score.nbCube);
    ui_kv(40.0f, 90.0f, 240.0f, "CUBES PLAYED", buf, UI_LABEL, E_WHITE);
    snprintf(buf, sizeof(buf), "%d", g->level);
    ui_kv(40.0f, 104.0f, 240.0f, "LEVEL", buf, UI_LABEL, E_WHITE);
    int gsecs = (int)(g->score.gameTime);
    if (gsecs <= 0) gsecs = (int)(g->curTime - g->startGameTime);
    if (gsecs < 0) gsecs = 0;
    snprintf(buf, sizeof(buf), "%d:%02d", gsecs / 60, gsecs % 60);
    ui_kv(40.0f, 118.0f, 240.0f, "TIME", buf, UI_LABEL, E_WHITE);
    if (ui_blink(g->curTime, 1.0f))
      r_print(cx, 200.0f, 1, E_WHITE,
              (g->demoFlag || g->practiceFlag) ? "PRESS A OR START" : "PRESS START", 1);
    return;
  }

  ui_box(4.0f, 4.0f, 312.0f, 232.0f, UI_FRAME, E_BLACK, " BLOCKOUT ", E_YELLOW);

  /* game: time + lines by number of layers removed together */
  int secs = (int)(g->curTime - g->startGameTime);
  if (secs < 0) secs = 0;
  snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);
  ui_kv(16.0f, 18.0f, 288.0f, "TIME", buf, UI_LABEL, E_WHITE);

  const int32_t *nb[5] = { &g->score.nbLine1, &g->score.nbLine2, &g->score.nbLine3,
                           &g->score.nbLine4, &g->score.nbLine5 };
  static const char *ln[5] = { "SINGLE", "DOUBLE", "TRIPLE", "QUAD", "PENTA" };
  static const uint32_t lc[5] = { E_LBLUE, E_LGREEN, E_LCYAN, E_LRED, E_LMAGENTA };
  long tot = 0;
  for (int i = 0; i < 5; i++) tot += (long)(*nb[i]) * (i + 1);
  snprintf(buf, sizeof(buf), "%ld", tot);
  ui_kv(16.0f, 32.0f, 288.0f, "LAYERS CLEARED", buf, UI_LABEL, E_WHITE);
  for (int i = 0; i < 5; i++) {
    float x = 16.0f + i * 58.0f;
    r_print(x + 24.0f, 50.0f, 1, lc[i], ln[i], 1);
    snprintf(buf, sizeof(buf), "%ld", (long)*nb[i]);
    r_print(x + 24.0f, 62.0f, 1, (*nb[i] > 0) ? E_WHITE : E_DGRAY, buf, 1);
  }

  ui_hline(12.0f, 80.0f, 308.0f, E_BLUE);

  r_print(16.0f, 88.0f, 1, UI_LABEL, "CONTROLS", 0);
  ui_key_hint(16.0f, 102.0f, "D-PAD", "MOVE");
  ui_key_hint(164.0f, 102.0f, "A", "DROP");
  ui_key_hint(16.0f, 116.0f, "B X Y", "ROTATE Z X Y");
  ui_key_hint(164.0f, 116.0f, "R+BXY", "REVERSE");
  ui_key_hint(16.0f, 130.0f, "L+PAD", "DIAGONAL");
  ui_key_hint(164.0f, 130.0f, "START", "PAUSE");
  ui_key_hint(16.0f, 144.0f, "ZR", "3D DEPTH");
  ui_key_hint(164.0f, 144.0f, "SELECT", "QUIT");
  if (g->practiceFlag) ui_key_hint(16.0f, 158.0f, "ZL", "HINT");

  ui_hline(12.0f, 176.0f, 308.0f, E_BLUE);

  snprintf(buf, sizeof(buf), "%s  %dx%dx%d", g->setupManager->GetBlockSetName(),
           g->thePit.GetWidth(), g->thePit.GetHeight(), g->thePit.GetDepth());
  r_print(cx, 186.0f, 1, E_WHITE, buf, 1);
  snprintf(buf, sizeof(buf), "3D %s   SOUND %s",
           gStereoPx > 0.0f ? "ON" : "OFF", g->setupManager->GetSound() ? "ON" : "OFF");
  r_print(cx, 202.0f, 1, UI_DIM, buf, 1);
  const char *mode = g->demoFlag ? "DEMO" : g->practiceFlag ? "PRACTICE" : "GAME";
  r_print(cx, 218.0f, 1, g->demoFlag ? E_LMAGENTA : E_LGREEN, mode, 1);
}

void render_game(Game *g) {
  if (!g->inited) return;

  /* square pit viewport like the original; pitView.y is in
     bottom-left convention because Game::StartSpark() uses it that way */
  g->SetPitViewport((int)PIT_X, (int)(240.0f - (PIT_Y + PIT_S)), (int)PIT_S, (int)PIT_S);

  /* layers completed since the last frame -> 0.35 s flash */
  static long lastLayers = 0;
  static float flashStart = -10.0f;
  long layers = g->score.nbLine1 + 2L * g->score.nbLine2 + 3L * g->score.nbLine3 +
                4L * g->score.nbLine4 + 5L * g->score.nbLine5;
  if (layers > lastLayers) flashStart = g->curTime;
  lastLayers = layers;
  float ft = g->curTime - flashStart;
  gFlash = (ft >= 0.0f && ft < 0.35f && g->gameMode != GAME_PAUSED) ? 1.0f - ft / 0.35f : 0.0f;

  for (int eye = 0; eye < 2; eye++) {
    render_begin_top(eye);
    gPitX = PIT_X; gPitY = PIT_Y; gPitW = PIT_S; gPitH = PIT_S;
    renderPitView(g, eye);
    renderPracticeHelp(g);
    drawPitLevel(g);
    renderHud(g);
    renderGameModeOverlay(g);
  }

  render_begin_bottom();
  renderBottomUI(g);
}
