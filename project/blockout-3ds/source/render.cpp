/*
  File:        render.cpp
  Description: Renderer software 3DS (citro2D) del port di BlockOut II.

  Il renderer riproduce il pipeline OpenGL originale:

   * proiezione gluPerspective(60.0, 1.0, 0.1, 10.0) applicata in software
     (F_PROJ = 1/tan(30deg));
   * matrice di vista gluLookAt(0,0,0 -> 0,0,10, up 0,1,0) identica
     all'originale (quindi x del mondo e' riflessa sullo schermo);
   * modello di illuminazione GL originale: ambient = materiale.ambient
     (ambient globale 1,0), diffuse = materiale.diffuse * max(0,N.L),
     luce in coordinate occhio (-15,10,10) (= (15,10,-10) transformata con
     la matrice di vista dell'originale), nessun specular (shininess 0 nei
     materiali del pozzo);
   * primitive non illuminate (griglia, cornice nera, bordi dei cubi) usano
     il colore AMBIENTE del materiale, come faceva SetMaterial() con
     glColor4f(ambient) a GL_LIGHTING spento;
   * depth-test disabilitato e ordine di disegno painter-style: i cubi del
     pozzo sono disegnati nell'ordine di orderMatrix come nell'originale,
     con il back-face culling in coordinate schermo;
   * lo clipping verso il viewport del pozzo e' fatto in software (poligoni
     Sutherland-Hodgman) perche' C2D non offre lo scissor test.

  Adattamenti richiesti dall'hardware 3DS:

   * C2D_DrawTriangle/Line/RectSolid al posto delle display list OpenGL;
   * le "big edge" cilindriche del polycube (lineWidth>0) sono rese come
     linee spesse C2D (mantengono il colore bianco/rosso);
   * le texture (background, spark, sprite dei punteggi, stili MARBLE e
     ARCADE) non sono disponibili: il rendering usa la geometria STYLE_CLASSIC
     con i palette colori degli stili, e lo sfondo e' nero;
   * stereoscopia parallela (off-axis): entrambi gli occhi condividono
   * la stessa matrice di vista; la disparita' e' uno shift orizzontale
   * proporzionale a (gConvZ/d - 1): zero a meta' pozzo, pop-out davanti,
   * dentro dietro, con cap in pixel. Niente parallasse verticale.

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
#include "Game.h"
#include "Pit.h"
#include "PolyCube.h"

/* ------------------------------------------------------------------ */
/* Costanti del pipeline originale                                      */
/* ------------------------------------------------------------------ */

#define F_PROJ    1.7320508075688772f   /* 1 / tan(60/2 gradi), aspect 1.0 */
#define LIGHT_EX -15.0f                 /* luce in coordinate occhio:      */
#define LIGHT_EY  10.0f                 /* gluLookAt applicata a (15,10,-10)*/
#define LIGHT_EZ  10.0f

#define TOP_W     400.0f
#define TOP_H     240.0f
#define BOT_W     320.0f

#define MAXOBJ    900        /* oggetti C2D per batch */

#define STEREO_MAXD 14.0f    /* cap alla disparita' totale (pixel, L-R) */

/* ------------------------------------------------------------------ */
/* Stato globale del renderer                                          */
/* ------------------------------------------------------------------ */

static C3D_RenderTarget *gLeft;
static C3D_RenderTarget *gRight;
static C3D_RenderTarget *gBottom;
static C2D_Font          gFont;
static C2D_TextBuf       gBuf;
static int               gObjCount;
static int               gScreen;          /* 0 = top, 1 = bottom */
static float             gStereoPx = 7.5f;   /* disparita' di riferimento (px totali L-R) */
static float             gEffPx = 7.5f;      /* effettiva per il frame (0 al game over) */
static int               gEye;               /* occhio corrente: 0 = sx, 1 = dx */
static float             gConvZ = 2.0f;      /* profondita' a disparita' zero (meta' pozzo) */

static float gV[16];                       /* matrice di vista corrente */
static float gPitX, gPitY, gPitW, gPitH;   /* viewport pozzo, top-left */

/* ------------------------------------------------------------------ */
/* Piccolo aiuto per i colori                                          */
/* ------------------------------------------------------------------ */

uint32_t r_color(int r, int g, int b, int a) {
  return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

/* ------------------------------------------------------------------ */
/* Primitivi 2D + batching                                              */
/* ------------------------------------------------------------------ */

static void flushIfNeeded(int n) {
  gObjCount += n;
  if (gObjCount >= MAXOBJ) { C2D_Flush(); gObjCount = 0; }
}

void r_rect(float x, float y, float w, float h, uint32_t col) {
  if (w <= 0.0f || h <= 0.0f) return;
  C2D_DrawRectSolid(x, y, 0.5f, w, h, OPAQUE(col));
  flushIfNeeded(1);
}

void r_line(float x0, float y0, float x1, float y1, float thick, uint32_t col) {
  uint32_t c = OPAQUE(col);
  C2D_DrawLine(x0, y0, c, x1, y1, c, thick, 0.5f);
  flushIfNeeded(1);
}

static void parseText(const char *s, C2D_Text *t, float *w, float *h, float hpx, float *scale) {
  if (!C2D_TextFontParseLine(t, gFont, gBuf, s, 0)) { *w = 0.0f; *h = 0.0f; *scale = 1.0f; return; }
  float sx = 1.0f, sy = 1.0f;
  C2D_TextGetDimensions(t, 1.0f, 1.0f, w, h);
  if (*h > 0.0f) sy = hpx / *h;
  sx = sy;
  *scale = sx;
  *w *= sx;
  *h = hpx;
  C2D_TextOptimize(t);
}

float r_text_width(const char *str, float hpx) {
  C2D_Text t; float w, h, sc;
  parseText(str, &t, &w, &h, hpx, &sc);
  return w;
}

/* align: 0 sx (x = bordo sinistro), 1 centro (x = centro), 2 dx */
void r_text(float x, float y, float hpx, uint32_t col, const char *str, int align, float ax) {
  C2D_Text t; float w, h, sc;
  parseText(str, &t, &w, &h, hpx, &sc);
  if (w <= 0.0f) return;
  float px = x;
  if (align == 1) px = x - w * 0.5f;
  else if (align == 2) px = x - w;
  else if (align == 3) px = x + ax * w;   /* 3 = justify-like usato nei menu */
  C2D_DrawText(&t, C2D_AlignLeft | C2D_WithColor, px, y, 0.5f, sc, sc, OPAQUE(col));
  flushIfNeeded(1);
}

int r_screen_w(void) { return (gScreen == 0) ? 400 : 320; }
int r_is_bottom(void) { return gScreen; }

/* ------------------------------------------------------------------ */
/* Frame                                                                */
/* ------------------------------------------------------------------ */

void render_init(void) {
  gfxSet3D(true);                        /* stereoscopia: prima di C3D_Init */
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();
  gLeft   = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  gRight  = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
  gBottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  gFont   = C2D_FontLoadSystem(CFG_REGION_USA);
  gBuf    = C2D_TextBufNew(8192);
  gObjCount = 0;
  gScreen = 0;
}

void render_exit(void) {
  if (gBuf) { C2D_TextBufDelete(gBuf); gBuf = NULL; }
  if (gFont) { C2D_FontFree(gFont); gFont = NULL; }
  C2D_Fini();
  C3D_Fini();
}

void render_set_stereo(float px) { gStereoPx = px; }

void render_clear(uint32_t col) {
  col = OPAQUE(col);
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  C2D_TargetClear(gLeft, col);
  C2D_TargetClear(gRight, col);
  C2D_TargetClear(gBottom, col);
  C2D_TextBufClear(gBuf);
  gObjCount = 0;
}

void render_begin_top(int eye) {
  gScreen = 0;
  C2D_SceneBegin(eye ? gRight : gLeft);
  gObjCount = 0;
}

void render_begin_bottom(void) {
  gScreen = 1;
  C2D_SceneBegin(gBottom);
  gObjCount = 0;
}

void render_flush(void) { C2D_Flush(); gObjCount = 0; }

/* Diagnostica di avvio: barre colorate + testo su entrambi gli schermi.
   Se l'utente vede le barre, il pipeline C3D/C2D funziona; se non vede
   nulla, il problema e' altrove (app non avviata / framebuffer). */
void render_boot_test(void) {
  render_clear(COL_BG);
  uint32_t red = r_color(255, 0, 0, 255), grn = r_color(0, 255, 0, 255);
  uint32_t blu = r_color(0, 0, 255, 255), wht = r_color(255, 255, 255, 255);

  render_begin_top(0);
  r_rect(0, 0, 40, 20, red);
  r_rect(40, 0, 40, 20, grn);
  r_rect(80, 0, 40, 20, blu);
  r_text(150, 4, 18, wht, "BOOT OK", 0, 0);
  render_flush();

  render_begin_top(1);
  r_rect(0, 0, 40, 20, red);
  r_rect(40, 0, 40, 20, grn);
  r_rect(80, 0, 40, 20, blu);
  r_text(150, 4, 18, wht, "BOOT OK", 0, 0);
  render_flush();

  render_begin_bottom();
  r_rect(0, 0, 320, 10, wht);
  r_text(160.0f, 110.0f, 20.0f, grn, "BlockOut 3DS", 1, 0);
  render_flush();

  render_swap(1);
}

void render_swap(int waitVsync) {
  (void)waitVsync;
  gScreen = 0;
  C2D_Flush();
  /* C3D_FrameEnd esegue gia' il flush dei comandi e lo swap dei framebuffer
     (gfx3d non esiste piu': non servono gfxFlushBuffers/gfxScreenSwapBuffers) */
  C3D_FrameEnd(0);
}

/* ------------------------------------------------------------------ */
/* Matrici / proiezione                                                */
/* ------------------------------------------------------------------ */

/* Vista per un occhio: stereo parallelo (off-axis). Entrambi gli occhi
   condividono la stessa matrice di vista; la disparita' e' applicata in
   projM come shift di schermo. Il terzo parametro e' ignorato (compat). */
static void buildEyeView(const float *base, int eye, float unused) {
  (void)unused;
  memcpy(gV, base, 16 * sizeof(float));
  gEye = eye ? 1 : 0;
}

/* Colore ambiente del materiale (primitive non illuminate) */
static uint32_t ambColor(const GLMATERIAL *mat) {
  int ri = (int)(mat->Ambient.r * 255.0f); if (ri < 0) ri = 0; if (ri > 255) ri = 255;
  int gi = (int)(mat->Ambient.g * 255.0f); if (gi < 0) gi = 0; if (gi > 255) gi = 255;
  int bi = (int)(mat->Ambient.b * 255.0f); if (bi < 0) bi = 0; if (bi > 255) bi = 255;
  /* alpha ignorato: nell'originale SetMaterial() usava glColor4f(ambient)
     e il blending era disabilitato; si usa sempre alpha pieno */
  return r_color(ri, gi, bi, 255);
}

/* ------------------------------------------------------------------ */
/* Clipping 2D delle primitive contro il viewport del pozzo            */
/* ------------------------------------------------------------------ */

typedef struct { float x, y; uint32_t c; } V2C;

/* Clippa un poligono (poli, n) contro il rettangolo del viewport;
   restituisce il numero di vertici clippati (>=3) o 0 */
static int clipPoly(const V2C *poly, int n, V2C *out,
                    float x0, float y0, float x1, float y1) {
  V2C bufA[64], bufB[64];
  int na = 0;
  for (int i = 0; i < n && na < 64; i++) bufA[na++] = poly[i];
  float edges[4] = { x0, y1, x1, y0 };   /* left, bottom, right, top */
  int axis[4]    = { 0, 1, 0, 1 };       /* 0 = x, 1 = y */
  int keepLess[4]= { 0, 1, 1, 0 };       /* 1 = tenere <= edge */
  for (int p = 0; p < 4; p++) {
    float e = edges[p];
    int ax = axis[p];
    int nd = 0;
    for (int i = 0; i < na; i++) {
      V2C a = bufA[i];
      V2C b = bufA[(i + 1) % na];
      float va = (ax == 0) ? a.x : a.y;
      float vb = (ax == 0) ? b.x : b.y;
      int ia = keepLess[p] ? (va <= e) : (va >= e);
      int ib = keepLess[p] ? (vb <= e) : (vb >= e);
      if (ia) {
        if (nd < 64) { bufB[nd++] = a; }
        if (ib != ia && nd < 64) {
          float t = (e - va) / (vb - va);
          V2C m;
          m.x = a.x + t * (b.x - a.x);
          m.y = a.y + t * (b.y - a.y);
          m.c = a.c;
          bufB[nd++] = m;
        }
      } else if (ib) {
        if (nd < 64) {
          float t = (e - va) / (vb - va);
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

/* Triangolo con clipping e ventaglio */
static void drawTriClip(float x0, float y0, uint32_t c0,
                        float x1, float y1, uint32_t c1,
                        float x2, float y2, uint32_t c2) {
  V2C poly[3] = { {x0,y0,c0}, {x1,y1,c1}, {x2,y2,c2} };
  V2C out[32];
  int n = clipPoly(poly, 3, out, gPitX - 1.0f, gPitY - 1.0f,
                   gPitX + gPitW + 1.0f, gPitY + gPitH + 1.0f);
  if (n < 3) return;
  for (int i = 1; i < n - 1; i++) {
    C2D_DrawTriangle(out[0].x, out[0].y, out[0].c,
                     out[i].x, out[i].y, out[i].c,
                     out[i+1].x, out[i+1].y, out[i+1].c, 0.5f);
    flushIfNeeded(1);
  }
}

/* Quadrilatero con clipping */
static void drawQuadClip(float x0, float y0, uint32_t c0,
                         float x1, float y1, uint32_t c1,
                         float x2, float y2, uint32_t c2,
                         float x3, float y3, uint32_t c3) {
  V2C poly[4] = { {x0,y0,c0}, {x1,y1,c1}, {x2,y2,c2}, {x3,y3,c3} };
  V2C out[32];
  int n = clipPoly(poly, 4, out, gPitX - 1.0f, gPitY - 1.0f,
                   gPitX + gPitW + 1.0f, gPitY + gPitH + 1.0f);
  if (n < 3) return;
  for (int i = 1; i < n - 1; i++) {
    C2D_DrawTriangle(out[0].x, out[0].y, out[0].c,
                     out[i].x, out[i].y, out[i].c,
                     out[i+1].x, out[i+1].y, out[i+1].c, 0.5f);
    flushIfNeeded(1);
  }
}

/* Linea con clipping segmento-rettangolo (Liang-Barsky) */
static void drawLineClip(float x0, float y0, uint32_t c0,
                         float x1, float y1, uint32_t c1, float thick) {
  float dx = x1 - x0, dy = y1 - y0;
  float t0 = 0.0f, t1 = 1.0f;
  float L = gPitX - 1.0f, R = gPitX + gPitW + 1.0f;
  float T = gPitY - 1.0f, B = gPitY + gPitH + 1.0f;
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
  float ax = x0 + t0 * dx, ay = y0 + t0 * dy;
  float bx = x0 + t1 * dx, by = y0 + t1 * dy;
  C2D_DrawLine(ax, ay, c0, bx, by, c1, thick, 0.5f);
  flushIfNeeded(1);
}

/* ------------------------------------------------------------------ */
/* Modello (matrice globale) + matrice vista/modello combinata          */
/* ------------------------------------------------------------------ */

static float gMV[16];        /* view * model  (column-major) */
static float gView[16];      /* view corrente (per il modello) */

static void setModel(const float *m) {
  if (!m) {
    for (int i = 0; i < 16; i++) gMV[i] = gView[i];
    return;
  }
  /* gMV = gView * m */
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++) s += gView[k * 4 + r] * m[c * 4 + k];
      gMV[c * 4 + r] = s;
    }
}

/* ---- sostituisce le funzioni che usavano gV con gMV ---------------- */

#undef gV_use
static int projM(float wx, float wy, float wz, float *sx, float *sy) {
  float ex = gMV[0] * wx + gMV[4] * wy + gMV[8]  * wz + gMV[12];
  float ey = gMV[1] * wx + gMV[5] * wy + gMV[9]  * wz + gMV[13];
  float ez = gMV[2] * wx + gMV[6] * wy + gMV[10] * wz + gMV[14];
  float d = -ez;
  if (d < 0.015f) return 0;
  float nx = F_PROJ * ex / d;
  float ny = F_PROJ * ey / d;
  float x = gPitX + (nx * 0.5f + 0.5f) * gPitW;
  float y = gPitY + (0.5f - ny * 0.5f) * gPitH;
  /* stereoscopia parallela: shift orizzontale a convergenza gConvZ.
     Stessa y per entrambi gli occhi -> zero parallasse verticale. */
  float disp = gEffPx * (gConvZ / d - 1.0f);
  if (disp > STEREO_MAXD) disp = STEREO_MAXD;
  else if (disp < -STEREO_MAXD) disp = -STEREO_MAXD;
  x += (gEye ? 0.5f : -0.5f) * disp;
  if (x < -6000.0f) x = -6000.0f; else if (x > 6000.0f) x = 6000.0f;
  if (y < -6000.0f) y = -6000.0f; else if (y > 6000.0f) y = 6000.0f;
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

static uint32_t shadeM(float wx, float wy, float wz,
                       float nx, float ny, float nz,
                       const GLMATERIAL *mat, int alpha) {
  float ex, ey, ez; eyePoint(wx, wy, wz, &ex, &ey, &ez);
  float lx = LIGHT_EX - ex, ly = LIGHT_EY - ey, lz = LIGHT_EZ - ez;
  float ll = sqrtf(lx * lx + ly * ly + lz * lz);
  if (ll > 0.0f) { lx /= ll; ly /= ll; lz /= ll; }
  float nex, ney, nez; eyeNormalM(nx, ny, nz, &nex, &ney, &nez);
  float nl = nex * lx + ney * ly + nez * lz;
  if (nl < 0.0f) nl = 0.0f;
  float r = mat->Ambient.r + mat->Diffuse.r * nl;
  float g = mat->Ambient.g + mat->Diffuse.g * nl;
  float b = mat->Ambient.b + mat->Diffuse.b * nl;
  int ri = (int)(r * 255.0f); if (ri < 0) ri = 0; if (ri > 255) ri = 255;
  int gi = (int)(g * 255.0f); if (gi < 0) gi = 0; if (gi > 255) gi = 255;
  int bi = (int)(b * 255.0f); if (bi < 0) bi = 0; if (bi > 255) bi = 255;
  int ai = alpha; if (ai < 0) ai = 0; if (ai > 255) ai = 255;
  return r_color(ri, gi, bi, ai);
}

/* Back-face culling in coordinate occhio: la faccia e' visibile se la sua
   normale punta verso la camera (dot(N, -P) > 0) */
static int faceVisible(float wx, float wy, float wz,
                       float nx, float ny, float nz) {
  float ex, ey, ez; eyePoint(wx, wy, wz, &ex, &ey, &ez);
  float nex, ney, nez; eyeNormalM(nx, ny, nz, &nex, &ney, &nez);
  return (nex * -ex + ney * -ey + nez * -ez) > 0.0f;
}

/* ------------------------------------------------------------------ */
/* Geometria del pozzo (STYLE_CLASSIC, vertici identici all'originale) */
/* ------------------------------------------------------------------ */

/* Face del cubo: 6 facce, each con normal + 4 vertici (coordinate 0/1) */
static const float cubeFaceV[6][4][3] = {
  { {0,1,0},{1,1,0},{1,0,0},{0,0,0} },   /* F1 n=(0,0,-1) */
  { {1,1,0},{1,1,1},{1,0,1},{1,0,0} },   /* F2 n=(1,0,0)  */
  { {1,1,1},{0,1,1},{0,0,1},{1,0,1} },   /* F3 n=(0,0,1)  */
  { {0,1,1},{0,1,0},{0,0,0},{0,0,1} },   /* F4 n=(-1,0,0) */
  { {1,0,0},{1,0,1},{0,0,1},{0,0,0} },   /* F5 n=(0,-1,0) */
  { {0,1,1},{1,1,1},{1,1,0},{0,1,0} }    /* F6 n=(0,1,0)  */
};
static const float cubeFaceN[6][3] = {
  {0,0,-1}, {1,0,0}, {0,0,1}, {-1,0,0}, {0,-1,0}, {0,1,0}
};

/* Spigoli del cubo (lcubeList[12]), coordinate 0/1 */
static const int cubeEdgeV[12][2][3] = {
  { {0,1,0},{1,1,0} }, { {1,1,0},{1,0,0} }, { {1,0,0},{0,0,0} }, { {0,0,0},{0,1,0} },
  { {0,1,0},{0,1,1} }, { {1,1,0},{1,1,1} }, { {1,0,0},{1,0,1} }, { {0,0,0},{0,0,1} },
  { {0,1,1},{1,1,1} }, { {1,1,1},{1,0,1} }, { {0,0,1},{1,0,1} }, { {0,0,1},{0,1,1} }
};

static void cornerWorld(float baseX, float baseY, float baseZ, float cSide,
                        int cx, int cy, int cz, const float *v,
                        float *wx, float *wy, float *wz) {
  *wx = baseX + ((float)cx + v[0]) * cSide;
  *wy = baseY + ((float)cy + v[1]) * cSide;
  *wz = baseZ + ((float)cz + v[2]) * cSide;
}

/* Cornice nera attorno all'apertura del pozzo (sideList: 8 triangoli,
   stessi vertici di Pit::CreateSide) */
static void drawSideRing(Pit *pit) {
  uint32_t blk = r_color(0, 0, 0, 255);
  VERTEX org = pit->GetOrigin();
  float ox = org.x, oy = org.y, oz = org.z;
  float fW = pit->GetFWidth(), fH = pit->GetFHeight(), cS = pit->GetCubeSide();
  float rx0 = ox - cS, ry0 = oy - cS;
  float rx1 = ox + fW + cS, ry1 = oy + fH + cS;

  /* 8 triangoli, nell'ordine e con le coordinate esatte della sideList
     originale (Pit::CreateSide): vertici 0..7 del rettangolo interno
     (ox,oy)..(ox+fW,oy+fH) espanso di cubeSide */
  float pts[8][3][2] = {
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
    float sa[2], sb[2], sc[2];
    if (!projM(pts[t][0][0], pts[t][0][1], oz, sa, sa + 1)) continue;
    if (!projM(pts[t][1][0], pts[t][1][1], oz, sb, sb + 1)) continue;
    if (!projM(pts[t][2][0], pts[t][2][1], oz, sc, sc + 1)) continue;
    drawTriClip(sa[0], sa[1], blk, sb[0], sb[1], blk, sc[0], sc[1], blk);
  }
}

/* 5 facce del fondo del pozzo (backList CLASSIC) illuminate backMaterial */
static void drawBack(Pit *pit, GLMATERIAL *backMat) {
  VERTEX org = pit->GetOrigin();
  float ox = org.x, oy = org.y, oz = org.z;
  float fW = pit->GetFWidth(), fH = pit->GetFHeight(), fD = pit->GetFDepth();

  struct F { float n[3]; float v[4][3]; };
  F faces[5];
  /* Vertici identici a Pit::CreateBack (STYLE_CLASSIC) */
  faces[0].n[0]=1; faces[0].n[1]=0; faces[0].n[2]=0;
  faces[0].v[0][0]=ox;      faces[0].v[0][1]=oy+fH;  faces[0].v[0][2]=oz;
  faces[0].v[1][0]=ox;      faces[0].v[1][1]=oy+fH;  faces[0].v[1][2]=oz+fD;
  faces[0].v[2][0]=ox;      faces[0].v[2][1]=oy;     faces[0].v[2][2]=oz+fD;
  faces[0].v[3][0]=ox;      faces[0].v[3][1]=oy;     faces[0].v[3][2]=oz;

  faces[1].n[0]=0; faces[1].n[1]=-1; faces[1].n[2]=0;
  faces[1].v[0][0]=ox;      faces[1].v[0][1]=oy+fH;  faces[1].v[0][2]=oz;
  faces[1].v[1][0]=ox+fW;   faces[1].v[1][1]=oy+fH;  faces[1].v[1][2]=oz;
  faces[1].v[2][0]=ox+fW;   faces[1].v[2][1]=oy+fH;  faces[1].v[2][2]=oz+fD;
  faces[1].v[3][0]=ox;      faces[1].v[3][1]=oy+fH;  faces[1].v[3][2]=oz+fD;

  faces[2].n[0]=-1; faces[2].n[1]=0; faces[2].n[2]=0;
  faces[2].v[0][0]=ox+fW;   faces[2].v[0][1]=oy+fH;  faces[2].v[0][2]=oz+fD;
  faces[2].v[1][0]=ox+fW;   faces[2].v[1][1]=oy+fH;  faces[2].v[1][2]=oz;
  faces[2].v[2][0]=ox+fW;   faces[2].v[2][1]=oy;     faces[2].v[2][2]=oz;
  faces[2].v[3][0]=ox+fW;   faces[2].v[3][1]=oy;     faces[2].v[3][2]=oz+fD;

  faces[3].n[0]=0; faces[3].n[1]=1; faces[3].n[2]=0;
  faces[3].v[0][0]=ox+fW;   faces[3].v[0][1]=oy;     faces[3].v[0][2]=oz;
  faces[3].v[1][0]=ox;      faces[3].v[1][1]=oy;     faces[3].v[1][2]=oz;
  faces[3].v[2][0]=ox;      faces[3].v[2][1]=oy;     faces[3].v[2][2]=oz+fD;
  faces[3].v[3][0]=ox+fW;   faces[3].v[3][1]=oy;     faces[3].v[3][2]=oz+fD;

  faces[4].n[0]=0; faces[4].n[1]=0; faces[4].n[2]=-1;
  faces[4].v[0][0]=ox;      faces[4].v[0][1]=oy+fH;  faces[4].v[0][2]=oz+fD;
  faces[4].v[1][0]=ox+fW;   faces[4].v[1][1]=oy+fH;  faces[4].v[1][2]=oz+fD;
  faces[4].v[2][0]=ox+fW;   faces[4].v[2][1]=oy;     faces[4].v[2][2]=oz+fD;
  faces[4].v[3][0]=ox;      faces[4].v[3][1]=oy;     faces[4].v[3][2]=oz+fD;

  for (int f = 0; f < 5; f++) {
    float cxm = 0, cym = 0, czm = 0;
    for (int k = 0; k < 4; k++) { cxm += faces[f].v[k][0]; cym += faces[f].v[k][1]; czm += faces[f].v[k][2]; }
    cxm *= 0.25f; cym *= 0.25f; czm *= 0.25f;
    if (!faceVisible(cxm, cym, czm, faces[f].n[0], faces[f].n[1], faces[f].n[2])) continue;
    float sx[4], sy[4];
    int ok = 1;
    for (int k = 0; k < 4; k++) {
      if (!projM(faces[f].v[k][0], faces[f].v[k][1], faces[f].v[k][2], &sx[k], &sy[k])) { ok = 0; break; }
    }
    if (!ok) continue;
    uint32_t c = shadeM(cxm, cym, czm, faces[f].n[0], faces[f].n[1], faces[f].n[2], backMat, 255);
    drawQuadClip(sx[0], sy[0], c, sx[1], sy[1], c, sx[2], sy[2], c, sx[3], sy[3], c);
  }
}

/* Griglia del pozzo (gridList): linee verdi, non illuminate */
static void drawGrid(Pit *pit, uint32_t gridCol, float thick) {
  VERTEX org = pit->GetOrigin();
  float ox = org.x, oy = org.y, oz = org.z;
  float cS = pit->GetCubeSide(), fD = pit->GetFDepth();
  int width = pit->GetWidth(), height = pit->GetHeight(), depth = pit->GetDepth();

  float sx0, sy0, sx1, sy1;
  for (int i = 0; i <= width; i++) {
    if (projM(ox + i * cS, -oy, oz, &sx0, &sy0) && projM(ox + i * cS, -oy, oz + fD, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
    if (projM(ox + i * cS, oy, oz, &sx0, &sy0) && projM(ox + i * cS, oy, oz + fD, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
  }
  for (int i = 1; i < height; i++) {
    if (projM(ox, oy + i * cS, oz, &sx0, &sy0) && projM(ox, oy + i * cS, oz + fD, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
    if (projM(-ox, oy + i * cS, oz, &sx0, &sy0) && projM(-ox, oy + i * cS, oz + fD, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
  }
  for (int i = 0; i <= depth; i++) {
    float z = oz + i * cS;
    if (projM(ox, -oy, z, &sx0, &sy0) && projM(-ox, -oy, z, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
    if (projM(-ox, -oy, z, &sx0, &sy0) && projM(-ox, oy, z, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
    if (projM(-ox, oy, z, &sx0, &sy0) && projM(ox, oy, z, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
    if (projM(ox, oy, z, &sx0, &sy0) && projM(ox, -oy, z, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
  }
  float zb = oz + fD;
  for (int i = 1; i < width; i++) {
    if (projM(ox + i * cS, -oy, zb, &sx0, &sy0) && projM(ox + i * cS, oy, zb, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
  }
  for (int i = 1; i < height; i++) {
    if (projM(ox, oy + i * cS, zb, &sx0, &sy0) && projM(-ox, oy + i * cS, zb, &sx1, &sy1))
      drawLineClip(sx0, sy0, gridCol, sx1, sy1, gridCol, thick);
  }
}

/* Cubi del pozzo (orderMatrix, ordine painter-style dell'originale) */
static void drawPitCubes(Pit *pit, uint32_t gridCol, uint32_t blkCol, int zbuf) {
  int width = pit->GetWidth(), height = pit->GetHeight(), depth = pit->GetDepth();
  VERTEX org = pit->GetOrigin();
  float cS = pit->GetCubeSide();
  float ox = org.x, oy = org.y, oz = org.z;

  const BLOCKITEM *om = pit->GetOrderMatrix();
  int mSize = pit->GetMatrixSize();

  for (int i = 0; i < mSize; i++) {
    int x = om[i].x, y = om[i].y, z = om[i].z;
    if (!pit->GetValue(x, y, z)) continue;
    if (zbuf ? !pit->IsVisible2(x, y, z) : !pit->IsVisible(x, y, z)) continue;

    /* --- 6 facce illuminate (GetMaterial(z)) + back-face culling --- */
    /* Pit::GetMaterial restituisce un puntatore a un static: va riletto
       ad ogni cella, esattamente come fa l'originale nel loop di Render */
    const GLMATERIAL mcell = *pit->GetMaterial(z);
    GLMATERIAL *m = const_cast<GLMATERIAL *>(&mcell);
    for (int f = 0; f < 6; f++) {
      float cxm = 0, cym = 0, czm = 0;
      float vs[4][3];
      for (int k = 0; k < 4; k++) {
        const float *v = cubeFaceV[f][k];
        cornerWorld(ox, oy, oz, cS, x, y, z, v, &vs[k][0], &vs[k][1], &vs[k][2]);
        cxm += vs[k][0]; cym += vs[k][1]; czm += vs[k][2];
      }
      cxm *= 0.25f; cym *= 0.25f; czm *= 0.25f;
      float nx = cubeFaceN[f][0], ny = cubeFaceN[f][1], nz = cubeFaceN[f][2];
      if (!faceVisible(cxm, cym, czm, nx, ny, nz)) continue;
      float sx[4], sy[4];
      int ok = 1;
      for (int k = 0; k < 4; k++)
        if (!projM(vs[k][0], vs[k][1], vs[k][2], &sx[k], &sy[k])) { ok = 0; break; }
      if (!ok) continue;
      uint32_t c = shadeM(cxm, cym, czm, nx, ny, nz, m, 255);
      drawQuadClip(sx[0], sy[0], c, sx[1], sy[1], c, sx[2], sy[2], c, sx[3], sy[3], c);
    }

    /* --- contorni (lcubeList): nero, o verde secondo il switch
         esatto di Pit::RenderEdge (STYLE_CLASSIC) --- */
    int isGreen[12];
    for (int e = 0; e < 12; e++) {
      int gg = 0;
      switch (e) {
        case 0: gg = (y == height - 1); break;
        case 1: gg = (x == width - 1); break;
        case 2: gg = (y == 0); break;
        case 3: gg = (x == 0); break;
        case 4: gg = (x == 0) || (y == height - 1); break;
        case 5: gg = (x == width - 1) || (y == height - 1); break;
        case 6: gg = (x == width - 1) || (y == 0); break;
        case 7: gg = (x == 0) || (y == 0); break;
        default: gg = (z == depth - 1); break;
      }
      isGreen[e] = gg;
    }

    int showEdges[12];
    memset(showEdges, 0, sizeof(showEdges));
    for (int e = 0; e < 4; e++) showEdges[e] = 1;
    if (x < width / 2) { showEdges[5] = 1; showEdges[9] = 1; showEdges[6] = 1; }
    if (x > width / 2) { showEdges[4] = 1; showEdges[11] = 1; showEdges[7] = 1; }
    if (y < height / 2) { showEdges[4] = 1; showEdges[8] = 1; showEdges[5] = 1; }
    if (y > height / 2) { showEdges[7] = 1; showEdges[10] = 1; showEdges[6] = 1; }

    for (int e = 0; e < 12; e++) {
      if (!showEdges[e]) continue;
      float v0[3] = { (float)cubeEdgeV[e][0][0], (float)cubeEdgeV[e][0][1], (float)cubeEdgeV[e][0][2] };
      float v1[3] = { (float)cubeEdgeV[e][1][0], (float)cubeEdgeV[e][1][1], (float)cubeEdgeV[e][1][2] };
      float ax, ay, az, bx, by, bz;
      cornerWorld(ox, oy, oz, cS, x, y, z, v0, &ax, &ay, &az);
      cornerWorld(ox, oy, oz, cS, x, y, z, v1, &bx, &by, &bz);
      float sx0, sy0, sx1, sy1;
      if (!projM(ax, ay, az, &sx0, &sy0)) continue;
      if (!projM(bx, by, bz, &sx1, &sy1)) continue;
      uint32_t c = isGreen[e] ? gridCol : blkCol;
      drawLineClip(sx0, sy0, c, sx1, sy1, c, 1.0f);
    }
  }
}

/* ------------------------------------------------------------------ */
/* Polycube corrente (pezzo) + ghost, con la matrice di Game           */
/* ------------------------------------------------------------------ */

static void drawPieceCubes(PolyCube *pc, GLMATERIAL *whiteMat, GLMATERIAL *redMat,
                           GLMATERIAL *grayMat, int redMode, int bigEdge) {
  int nb = pc->GetNbCube();
  BLOCKITEM *cubes = pc->GetCubes();
  float cS = pc->GetCubeSide();
  VERTEX org = pc->GetOrigin();

  /* facce piene (whiteMaterial o redMaterial, illuminate) */
  for (int ci = 0; ci < nb; ci++) {
    int x = cubes[ci].x, y = cubes[ci].y, z = cubes[ci].z;
    for (int f = 0; f < 6; f++) {
      /* come nell'originale: le facce interne del pezzo non sono disegnate
         (Create() le omette), ma RenderCube le ricrea; l'unico filtro che
         conta e' il back-face culling */
      float cxm = 0, cym = 0, czm = 0;
      float vs[4][3];
      for (int k = 0; k < 4; k++) {
        const float *v = cubeFaceV[f][k];
        cornerWorld(org.x, org.y, org.z, cS, x, y, z, v, &vs[k][0], &vs[k][1], &vs[k][2]);
        cxm += vs[k][0]; cym += vs[k][1]; czm += vs[k][2];
      }
      cxm *= 0.25f; cym *= 0.25f; czm *= 0.25f;
      float nx = cubeFaceN[f][0], ny = cubeFaceN[f][1], nz = cubeFaceN[f][2];
      if (!faceVisible(cxm, cym, czm, nx, ny, nz)) continue;
      float sx[4], sy[4];
      int ok = 1;
      for (int k = 0; k < 4; k++)
        if (!projM(vs[k][0], vs[k][1], vs[k][2], &sx[k], &sy[k])) { ok = 0; break; }
      if (!ok) continue;
      uint32_t c = shadeM(cxm, cym, czm, nx, ny, nz, redMode ? redMat : whiteMat, 255);
      drawQuadClip(sx[0], sy[0], c, sx[1], sy[1], c, sx[2], sy[2], c, sx[3], sy[3], c);
    }
  }

  /* bordi (lineList): NON illuminati -> colore ambiente (Bianco/Rosso) */
  uint32_t lcol = ambColor(redMode ? redMat : whiteMat);
  lcol = (lcol & 0x00FFFFFFu) | 0xFF000000u;
  if (bigEdge > 0) {
    /* bigEdgeList: rese come linee spesse (adattamento hardware) */
    int nE = pc->GetNbEdge();
    EDGE *ed = pc->GetEdges();
    float t = (float)bigEdge;
    for (int i = 0; i < nE; i++) {
      float ax = (ed[i].p1.x + 0.5f) * cS + org.x, ay = (ed[i].p1.y + 0.5f) * cS + org.y, az = (ed[i].p1.z + 0.5f) * cS + org.z;
      float bx = (ed[i].p2.x + 0.5f) * cS + org.x, by = (ed[i].p2.y + 0.5f) * cS + org.y, bz = (ed[i].p2.z + 0.5f) * cS + org.z;
      float sx0, sy0, sx1, sy1;
      if (!projM(ax, ay, az, &sx0, &sy0)) continue;
      if (!projM(bx, by, bz, &sx1, &sy1)) continue;
      uint32_t c = shadeM((ax + bx) * 0.5f, (ay + by) * 0.5f, (az + bz) * 0.5f, 0, 0, -1, grayMat, 255);
      drawLineClip(sx0, sy0, c, sx1, sy1, c, t * 2.0f);
    }
  } else {
    int nE = pc->GetNbEdge();
    EDGE *ed = pc->GetEdges();
    for (int i = 0; i < nE; i++) {
      float ax = (float)ed[i].p1.x * cS + org.x, ay = (float)ed[i].p1.y * cS + org.y, az = (float)ed[i].p1.z * cS + org.z;
      float bx = (float)ed[i].p2.x * cS + org.x, by = (float)ed[i].p2.y * cS + org.y, bz = (float)ed[i].p2.z * cS + org.z;
      float sx0, sy0, sx1, sy1;
      if (!projM(ax, ay, az, &sx0, &sy0)) continue;
      if (!projM(bx, by, bz, &sx1, &sy1)) continue;
      drawLineClip(sx0, sy0, lcol, sx1, sy1, lcol, 1.0f);
    }
  }
}

/* Ghost del pezzo corrente: facce visibili (IsFaceVisible) con ghostMaterial */
static void drawPieceGhost(PolyCube *pc, float trans, uint32_t colBase) {
  int nb = pc->GetNbCube();
  BLOCKITEM *cubes = pc->GetCubes();
  float cS = pc->GetCubeSide();
  VERTEX org = pc->GetOrigin();
  int alpha = (int)(trans * 255.0f);

  for (int ci = 0; ci < nb; ci++) {
    int x = cubes[ci].x, y = cubes[ci].y, z = cubes[ci].z;
    /* IsFaceVisible del PolyCube originale */
    int vis[6];
    vis[0] = !pc->FindCube(x, y, z - 1);
    vis[1] = !pc->FindCube(x - 1, y, z);
    vis[2] = !pc->FindCube(x, y, z + 1);
    vis[3] = !pc->FindCube(x + 1, y, z);
    vis[4] = !pc->FindCube(x, y + 1, z);
    vis[5] = !pc->FindCube(x, y - 1, z);
    for (int f = 0; f < 6; f++) {
      if (!vis[f]) continue;
      float cxm = 0, cym = 0, czm = 0;
      float vs[4][3];
      for (int k = 0; k < 4; k++) {
        const float *v = cubeFaceV[f][k];
        cornerWorld(org.x, org.y, org.z, cS, x, y, z, v, &vs[k][0], &vs[k][1], &vs[k][2]);
        cxm += vs[k][0]; cym += vs[k][1]; czm += vs[k][2];
      }
      cxm *= 0.25f; cym *= 0.25f; czm *= 0.25f;
      if (!faceVisible(cxm, cym, czm, cubeFaceN[f][0], cubeFaceN[f][1], cubeFaceN[f][2])) continue;
      float sx[4], sy[4];
      int ok = 1;
      for (int k = 0; k < 4; k++)
        if (!projM(vs[k][0], vs[k][1], vs[k][2], &sx[k], &sy[k])) { ok = 0; break; }
      if (!ok) continue;
      drawQuadClip(sx[0], sy[0], colBase, sx[1], sy[1], colBase,
                   sx[2], sy[2], colBase, sx[3], sy[3], colBase);
      (void)alpha;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Spark: flash bianco nel punto di rimbalzo (sprite 4x4 dell'orig.)   */
/* ------------------------------------------------------------------ */

static const float sparkTime = 0.5f;   /* identico alla costante di Game.cpp */

void drawSpark(Game *g) {
  float sTime = g->curTime - g->startSpark;
  if (sTime >= sparkTime) return;

  int frame = (int)((sTime * 16.0f) / sparkTime);
  if (frame > 15) frame = 15;

  float ratio = sTime / sparkTime;
  int alpha = (int)(255.0f * (1.0f - ratio));
  if (alpha <= 0) return;
  uint32_t c = r_color(255, 255, 255, alpha);

  /* Game::StartSpark() salva centro/raggio nelle coordinate bottom-left
     dell'originale (spriteView a schermo intero): conversione top-left  */
  float cx = g->sparkX;
  float cy = 240.0f - g->sparkY;
  float r  = g->sparkW * 0.5f * (0.55f + 0.45f * (float)frame / 15.0f);

  /* stella a 8 punte: stessa silhouette dello sprite, animata nei 16 frame */
  V2C poly[8];
  for (int i = 0; i < 8; i++) {
    float a = (float)i * 0.7853981634f;
    float rr = (i & 1) ? r * 0.36f : r;
    poly[i].x = cx + cosf(a) * rr;
    poly[i].y = cy - sinf(a) * rr;
    poly[i].c = c;
  }
  for (int i = 1; i < 7; i++) {
    C2D_DrawTriangle(poly[0].x, poly[0].y, c, poly[i].x, poly[i].y, c,
                     poly[i + 1].x, poly[i + 1].y, c, 0.5f);
    flushIfNeeded(1);
  }
}

/* ------------------------------------------------------------------ */
/* HUD: gli stessi valori e le stesse posizioni di Sprites.cpp         */
/* (le etichette erano "stampate" nel background.png: qui in testo)    */
/* ------------------------------------------------------------------ */

static uint32_t opaque(uint32_t c) { return (c & 0x00FFFFFFu) | 0xFF000000u; }

void renderHud(Game *g) {
  char buf[32];

  /* Pannello laterale a destra del pozzo (x 326..398, come la striscia
     libera dal viewport): etichetta sopra il valore, come nel background
     dell'originale. Disegnato identico nei due occhi = a prof. schermo. */
  const float px0 = 326.0f, px1 = 398.0f, pyc = (px0 + px1) * 0.5f;
  const float py0 = 4.0f, py1 = 234.0f;

  r_rect(px0, py0, px1 - px0, py1 - py0, r_color(14, 16, 22, 255));
  r_rect(px0, py0, px1 - px0, 1.0f, COL_GREEN);
  r_rect(px0, py1 - 1.0f, px1 - px0, 1.0f, COL_GREEN);
  r_rect(px0, py0, 1.0f, py1 - py0, COL_GREEN);
  r_rect(px1 - 1.0f, py0, 1.0f, py1 - py0, COL_GREEN);

  r_text(pyc, 8.0f, 12.0f, COL_GREEN, "PARTITA", 1, 0);
  r_line(px0 + 4.0f, 24.0f, px1 - 4.0f, 24.0f, 1.0f, COL_GRAY);

  /* riga: etichetta grigia 9px + valore bianco centrato; il valore scala
     a 11px se troppo largo per il pannello */
  struct Row { const char *label; const char *val; };
  char sScore[16], sCube[16], sHigh[16], sTime[16], sPit[16];
  snprintf(sScore, sizeof(sScore), "%d", g->score.score);
  snprintf(sCube, sizeof(sCube), "%d", g->score.nbCube);
  snprintf(sHigh, sizeof(sHigh), "%d", g->highScore);
  int secs = (int)(g->curTime - g->startGameTime);
  if (secs < 0) secs = 0;
  snprintf(sTime, sizeof(sTime), "%d:%02d", secs / 60, secs % 60);
  snprintf(sPit, sizeof(sPit), "%dx%dx%d", g->thePit.GetWidth(),
           g->thePit.GetHeight(), g->thePit.GetDepth());

  const Row rows[] = {
    { "PUNTI",  sScore },
    { "CUBI",   sCube },
    { "RECORD", sHigh },
    { "TEMPO",  sTime },
    { "POZZO",  sPit },
    { "SET",    g->setupManager->GetBlockSetName() },
  };
  float y = 29.0f;
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    r_text(pyc, y, 9.0f, COL_GRAY, rows[i].label, 1, 0);
    float vh = 13.0f;
    if (r_text_width(rows[i].val, vh) > (px1 - px0 - 8.0f)) vh = 11.0f;
    r_text(pyc, y + 9.0f, vh, COL_WHITE, rows[i].val, 1, 0);
    y += 27.0f;
  }

  r_line(px0 + 4.0f, y - 4.0f, px1 - 4.0f, y - 4.0f, 1.0f, COL_GRAY);

  /* stato: demo / pratica / pausa (l'originale: RenderDemo/RenderPractice) */
  const char *mode = NULL;
  uint32_t mc = COL_GREEN;
  if (g->demoFlag)           { mode = "DEMO";    }
  else if (g->practiceFlag)  { mode = "PRATICA"; }
  else if (g->gameMode == GAME_PAUSED) { mode = "PAUSA"; mc = COL_WHITE; }
  if (mode) r_text(pyc, y + 2.0f, 11.0f, mc, mode, 1, 0);

  /* livello nel box di sinistra (Sprites::RenderLevel) */
  snprintf(buf, sizeof(buf), "%d", g->level);
  r_text(17.0f, 8.0f, 16.0f, COL_WHITE, buf, 1, 0);
  r_text(17.0f, 24.0f, 9.0f, COL_GRAY, "LIV", 1, 0);
}

/* Pit::RenderLevel(): colonna del livello a sinistra, disegnata con gli
   stessi pixel di Pit::DrawPitLevelCubes (SetPix scrive l'ambient) */
static void levelCubes(int LX, int LT, int LH, int x, int y, int w, int h,
                       uint32_t fill, uint32_t white) {
  int f1 = (int)lroundf((float)h / 4.0f);
  int f2 = (int)lroundf((float)h / 2.0f);
  int wc = w / 3;
  int sX = h / 2;
  int eX = sX + 2 * wc;

  for (int j = 0; j < h; j++) {
    if (j > f2) eX--;
    int sy = LT + LH - 1 - (j + y);
    if (eX > sX) r_rect((float)(LX + x + sX), (float)sy, (float)(eX - sX), 1.0f, fill);
    sX--;
    if (sX < 0) sX = 0;
  }

  sX = h / 2;
  for (int j = 0; j < h; j++) {
    int sy = LT + LH - 1 - (j + y);
    if (j == 0 || j == f1 || j == f2 || j == h - 1) {
      r_rect((float)(LX + x + sX), (float)sy, (float)(2 * wc + 1), 1.0f, white);
    } else {
      r_rect((float)(LX + x + sX), (float)sy, 1.0f, 1.0f, white);
      r_rect((float)(LX + x + sX + wc), (float)sy, 1.0f, 1.0f, white);
      r_rect((float)(LX + x + sX + 2 * wc), (float)sy, 1.0f, 1.0f, white);
    }
    if (sX > 0)
      r_rect((float)(LX + x + sX + 2 * wc), (float)(LT + LH - 1 - (j + h / 2 + y)), 1.0f, 1.0f, white);
    if (j <= h / 2)
      r_rect((float)(LX + x + h / 2 + 2 * wc), (float)sy, 1.0f, 1.0f, white);
    if (j >= f1 && j <= f1 + h / 2)
      r_rect((float)(LX + x + h / 2 - f1 + 2 * wc), (float)sy, 1.0f, 1.0f, white);
    sX--;
    if (sX < 0) sX = 0;
  }
}

void drawPitLevel(Game *g) {
  Pit *pit = &g->thePit;
  int depth = pit->GetDepth();

  /* Pit::CreatePitLevel() riscaldata a 400x240 */
  const int LX = 7;     /* fround(0.0176f*400) */
  const int LW = 20;    /* fround(0.0498f*400) */
  const int LH = 198;   /* fround(0.8255f*240) */
  const int LT = 6;     /* 240 - (fround(0.1510f*240) + LH) */

  if (LW < 8) return;   /* come nell'originale */

  int cS = (LH - 2) / 20;
  int sY = LH - (cS * depth) - 2;

  uint32_t grid  = opaque(ambColor(pit->GetGridMaterial()));
  uint32_t white = opaque(ambColor(pit->GetWhiteMaterial()));

  for (int j = 0; j < depth; j++) {
    int yb = LT + LH - 1 - sY;
    r_rect((float)(LX + 1), (float)yb, 1.0f, 1.0f, grid);
    r_rect((float)(LX + 2), (float)yb, 1.0f, 1.0f, grid);
    r_rect((float)(LX + LW - 3), (float)yb, 1.0f, 1.0f, grid);
    r_rect((float)(LX + LW - 2), (float)yb, 1.0f, 1.0f, grid);
    for (int i = sY; i < sY + cS; i++) {
      int yy = LT + LH - 1 - i;
      r_rect((float)LX, (float)yy, 1.0f, 1.0f, grid);
      r_rect((float)(LX + LW - 1), (float)yy, 1.0f, 1.0f, grid);
    }
    if (!pit->IsLineEmpty(j))
      levelCubes(LX, LT, LH, 3, sY + 2, LW - 6, cS - 3,
                 opaque(ambColor(pit->GetMaterial(j))), white);
    sY += cS;
  }

  for (int i = 0; i < LW; i++)
    r_rect((float)(LX + i), (float)(LT + LH - 1 - (LH - 2)), 1.0f, 1.0f, grid);
}

/* sprites.RenderGameMode(): "PAUSE" / "GAME OVER" nel riquadro originale */
void renderGameModeOverlay(Game *g) {
  const float xGOver = 130.0f;   /* fround(0.3252f*400) */
  const float yGOver = 100.0f;   /* fround(0.4167f*240) */
  const float wGOver = 100.0f;   /* fround(0.2500f*400) */
  const float hGOver = 40.0f;    /* fround(0.1680f*240) */

  if (g->gameMode == GAME_OVER) {
    r_text(xGOver + wGOver * 0.5f, yGOver + hGOver * 0.5f - 12.0f, 24.0f, COL_RED, "GAME OVER", 1, 0);
    r_text(xGOver + wGOver * 0.5f, yGOver + hGOver * 0.5f + 16.0f, 12.0f, COL_WHITE,
           "START: menu   SELECT: esci", 1, 0);
  } else if (g->gameMode == GAME_PAUSED) {
    r_text(xGOver + wGOver * 0.5f, yGOver + hGOver * 0.5f - 12.0f, 24.0f, COL_WHITE, "PAUSA", 1, 0);
    r_text(xGOver + wGOver * 0.5f, yGOver + hGOver * 0.5f + 16.0f, 12.0f, COL_WHITE,
           "START: continua", 1, 0);
  }
}

/* ------------------------------------------------------------------ */
/* Rendering del pozzo (top screen)                                     */
/* ------------------------------------------------------------------ */

void renderPitView(Game *g, int eye) {
  Pit *pit = &g->thePit;

  /* l'originale caricava pitMatrix solo durante l'animazione finale */
  const float *viewSrc = g->endAnimStarted ? g->pitMatrix : g->matView;

  /* piano a disparita' zero: meta' del pozzo (segue la profondita' impostata) */
  gConvZ = pit->GetOrigin().z + pit->GetFDepth() * 0.5f;
  /* al game over la camera orbita: la convergenza perde senso e la scritta
     e' a profondita' schermo, quindi schermo piatto (niente stereo) */
  gEffPx = (g->gameMode == GAME_OVER) ? 0.0f : gStereoPx;

  buildEyeView(viewSrc, eye, 0.0f);
  memcpy(gView, gV, 16 * sizeof(float));
  setModel(NULL);

  int zbuf = g->endAnimStarted ? 1 : 0;
  int renderCube = g->endAnimStarted ? 1 : (g->gameMode != GAME_PAUSED ? 1 : 0);

  GLMATERIAL *backMat = pit->GetBackMaterial();
  GLMATERIAL *gridMat = pit->GetGridMaterial();
  uint32_t gridCol = opaque(ambColor(gridMat));
  uint32_t blkCol = r_color(0, 0, 0, 255);

  /* Pit::Render(STYLE_CLASSIC): sideList e backList solo senza z-buffer */
  if (!zbuf) {
    drawSideRing(pit);
    drawBack(pit, backMat);
  }
  drawGrid(pit, gridCol, 1.0f);

  if (renderCube) {
    drawPitCubes(pit, gridCol, blkCol, zbuf);
  }

  /* Il pezzo corrente non viene disegnato in pausa/game over (originale) */
  if (g->gameMode != GAME_PAUSED && g->gameMode != GAME_OVER) {

    PolyCube *pc = &g->allPolyCube[g->pIdx];
    setModel(g->matPiece);   /* g->mat include gia' matView (originale) */

    GLMATERIAL whiteMat, redMat, grayMat, ghostMat;
    memset(&whiteMat, 0, sizeof(whiteMat));
    whiteMat.Ambient.r = whiteMat.Ambient.g = whiteMat.Ambient.b = 1.0f;
    whiteMat.Diffuse.r = whiteMat.Diffuse.g = whiteMat.Diffuse.b = 1.0f;
    memset(&redMat, 0, sizeof(redMat));
    redMat.Ambient.r = 1.0f; redMat.Diffuse.r = 1.0f;
    memset(&grayMat, 0, sizeof(grayMat));
    grayMat.Ambient.r = grayMat.Ambient.g = grayMat.Ambient.b = 0.5f;
    grayMat.Diffuse.r = grayMat.Diffuse.g = grayMat.Diffuse.b = 0.8f;
    grayMat.Specular.r = grayMat.Specular.g = grayMat.Specular.b = 1.0f;
    grayMat.Power = 20.0f;
    memset(&ghostMat, 0, sizeof(ghostMat));
    ghostMat.Ambient.r = ghostMat.Ambient.g = ghostMat.Ambient.b = 0.5f;
    ghostMat.Diffuse.r = ghostMat.Diffuse.g = ghostMat.Diffuse.b = 0.5f;

    /* ghost: trans = (ghost/FTRANS_MAX)*0.4, come in PolyCube::CreateGhost */
    int trans = pc->GetGhost() ? (int)(0.4f * 255.0f) : 0;
    if (trans > 0) {
      uint32_t gc = r_color(128, 128, 128, trans);
      drawPieceGhost(pc, 0.5f, gc);
    }

    drawPieceCubes(pc, &whiteMat, &redMat, &grayMat, g->redMode != 0,
                   g->lineWidth);
  }

  /* pratica: ghost dell'AI con matAI (0.75s dopo ComputeHelp) */
  if (g->practiceFlag && g->startShowAI > 0.0f) {
    if ((g->curTime - g->startShowAI) < 0.75f) {
      PolyCube *pc = &g->allPolyCube[g->pIdx];
      setModel(g->matAIPiece);
      uint32_t gc = r_color(128, 128, 128, 102);
      drawPieceGhost(pc, 0.4f, gc);
    }
  }

  if (g->startSpark != 0.0f &&
      g->gameMode != GAME_PAUSED && g->gameMode != GAME_OVER)
    drawSpark(g);
}

/* ------------------------------------------------------------------ */
/* Schermo basso: dettagli partita (l'originale li aveva nel menu /
   nella pagina dei punteggi, qui sempre visibile)                      */
/* ------------------------------------------------------------------ */

void renderBottomUI(Game *g) {
  gScreen = 1;
  gPitX = 0; gPitY = 0; gPitW = 320; gPitH = 240;

  float cx = 160.0f;
  char buf[64];

  if (g->gameMode == GAME_OVER) {
    r_text(cx, 12, 20, COL_RED, "GAME OVER", 1, 0);
    snprintf(buf, sizeof(buf), "PUNTI %d", g->score.score);
    r_text(cx, 44, 14, COL_WHITE, buf, 1, 0);
    snprintf(buf, sizeof(buf), "CUBI %d", g->score.nbCube);
    r_text(cx, 64, 12, COL_WHITE, buf, 1, 0);
    snprintf(buf, sizeof(buf), "1x%d  2x%d  3x%d  4x%d  5x%d",
             g->score.nbLine1, g->score.nbLine2, g->score.nbLine3,
             g->score.nbLine4, g->score.nbLine5);
    r_text(cx, 84, 12, COL_GREEN, buf, 1, 0);
    snprintf(buf, sizeof(buf), "LIVELLO %d   %s", g->score.startLevel,
             g->setupManager->GetBlockSetName());
    r_text(cx, 104, 12, COL_WHITE, buf, 1, 0);
    int gsecs = (int)(g->curTime - g->startGameTime);
    if (gsecs < 0) gsecs = 0;
    snprintf(buf, sizeof(buf), "TEMPO %d:%02d", gsecs / 60, gsecs % 60);
    r_text(cx, 124, 12, COL_WHITE, buf, 1, 0);
    if (g->demoFlag || g->practiceFlag)
      r_text(cx, 160, 13, COL_GREEN, "A / START: ricomincia   SELECT: esci", 1, 0);
    else
      r_text(cx, 160, 13, COL_GREEN, "START: torna al menu   SELECT: esci", 1, 0);
    return;
  }

  r_text(cx, 8, 15, COL_GREEN, "BlockOut 3DS", 1, 0);

  snprintf(buf, sizeof(buf), "Livello %d - %s - %dx%dx%d", g->level,
           g->setupManager->GetBlockSetName(),
           g->thePit.GetWidth(), g->thePit.GetHeight(), g->thePit.GetDepth());
  r_text(cx, 28, 12, COL_WHITE, buf, 1, 0);

  snprintf(buf, sizeof(buf), "Linee: 1x%d 2x%d 3x%d 4x%d 5x%d",
           g->score.nbLine1, g->score.nbLine2, g->score.nbLine3,
           g->score.nbLine4, g->score.nbLine5);
  r_text(cx, 46, 12, COL_GREEN, buf, 1, 0);

  if (g->demoFlag)      r_text(cx, 64, 12, COL_RED, "DEMO (AI)", 1, 0);
  else if (g->practiceFlag) r_text(cx, 64, 12, COL_RED, "PRATICA", 1, 0);
  else if (g->gameMode == GAME_PAUSED) r_text(cx, 64, 12, COL_RED, "PAUSA", 1, 0);
  else r_text(cx, 64, 12, COL_WHITE, "PARTITA IN CORSO", 1, 0);

  r_line(12.0f, 82.0f, 308.0f, 82.0f, 1.0f, COL_GRAY);
  r_text(12, 88, 11, COL_GREEN, "COMANDI", 0, 0);

  /* colonna sinistra: movimento e caduta */
  r_text(12, 104, 11, COL_WHITE, "D-pad/levetta: muovi", 0, 0);
  r_text(12, 120, 11, COL_WHITE, "L + D-pad: diagonali", 0, 0);
  r_text(12, 136, 11, COL_WHITE, "A: caduta", 0, 0);
  r_text(12, 152, 11, COL_WHITE, "START: pausa   SELECT: esci", 0, 0);
  /* colonna destra: rotazioni e extra */
  r_text(172, 104, 11, COL_WHITE, "B/X/Y: ruota", 0, 0);
  r_text(172, 120, 11, COL_WHITE, "R + rotaz.: inversa", 0, 0);
  r_text(172, 136, 11, COL_WHITE, "ZL: aiuto (pratica)", 0, 0);
  r_text(172, 152, 11, COL_WHITE, "ZR: 3D  L+R+START: audio", 0, 0);

  r_text(12, 200, 11, COL_GREEN, "BlockOut II 2.5 GPL - Jean-Luc PONS", 0, 0);
  snprintf(buf, sizeof(buf), "facce %s", g->transparent ? "trasparenti" : "opache");
  r_text(308, 200, 11, COL_GRAY, buf, 2, 0);
}

/* ------------------------------------------------------------------ */
/* Entry point del gioco                                                */
/* ------------------------------------------------------------------ */

void render_game(Game *g) {

  if (!g->inited) return;

  /* Game::Create(): le stesse formule di viewport dell'originale, ma
     riferite allo schermo superiore 400x240. pitView.y resta bottom-left
     perche' Game::StartSpark() lo usa in quella convenzione.            */
  float scrW = 400.0f, scrH = 240.0f;
  float pvx  = roundf(scrW * 0.0889f);
  float pvy0 = roundf(scrH * 0.0183f);
  float pvw  = roundf(scrW * 0.7197f);
  float pvh  = roundf(scrH * 0.9596f);
  float pvy  = scrH - (pvh + pvy0);         /* glViewport: bordo inferiore */
  float pvTop = scrH - (pvy + pvh);         /* top-left per il renderer    */

  g->SetPitViewport((int)pvx, (int)pvy, (int)pvw, (int)pvh);

  for (int eye = 0; eye < 2; eye++) {
    render_begin_top(eye);
    gScreen = 0;

    gPitX = pvx; gPitY = pvTop; gPitW = pvw; gPitH = pvh;

    renderPitView(g, eye);

    /* HUD e colonna del livello (spriteView = schermo intero) */
    renderHud(g);
    drawPitLevel(g);
    renderGameModeOverlay(g);

    render_flush();
  }

  render_begin_bottom();
  renderBottomUI(g);
  render_flush();
}
