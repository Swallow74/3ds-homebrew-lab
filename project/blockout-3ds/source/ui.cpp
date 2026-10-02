/*
  File:        ui.cpp
  Description: Layer di UI in stile DOS sopra i primitivi di render.cpp
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Note sull'hardware: hidTouchRead() restituisce gia' le coordinate in
  pixel dello schermo basso (320x240).
*/

#include <3ds.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "ui.h"
#include "render.h"

/* ------------------------------------------------------------------ */
/* Riquadri                                                            */
/* ------------------------------------------------------------------ */

void ui_frame(float x, float y, float w, float h, uint32_t col) {
  r_rect(x, y, w, 1.0f, col);
  r_rect(x, y + h - 1.0f, w, 1.0f, col);
  r_rect(x, y, 1.0f, h, col);
  r_rect(x + w - 1.0f, y, 1.0f, h, col);
}

void ui_box(float x, float y, float w, float h, uint32_t border, uint32_t bg,
            const char *title, uint32_t titleCol) {
  r_rect(x, y, w, h, bg);
  ui_frame(x, y, w, h, border);
  ui_frame(x + 2.0f, y + 2.0f, w - 4.0f, h - 4.0f, border);
  if (title) {
    float tw = r_print_w(title, 1);
    float tx = floorf(x + (w - tw) * 0.5f);
    r_rect(tx, y, tw, 8.0f, bg);
    r_print(tx, y - 2.0f, 1, titleCol, title, 0);
  }
}

void ui_hline(float x0, float y, float x1, uint32_t col) {
  r_rect(x0, y, x1 - x0, 1.0f, col);
}

/* ------------------------------------------------------------------ */
/* Testo                                                               */
/* ------------------------------------------------------------------ */

float ui_key_hint(float x, float y, const char *keys, const char *desc) {
  r_print(x, y, 1, UI_KEY, keys, 0);
  float w = r_print_w(keys, 1) + 8.0f;
  if (desc) {
    r_print(x + w, y, 1, UI_TEXT, desc, 0);
    w += r_print_w(desc, 1);
  }
  return w;
}

int ui_wrap(float x, float y, uint32_t col, const char *str, float maxw, int maxLines) {
  int perLine = (int)(maxw / FONT_W);
  if (perLine < 4) perLine = 4;
  char line[64];
  int nLines = 0;
  const char *p = str;
  while (*p && (maxLines <= 0 || nLines < maxLines)) {
    while (*p == ' ') p++;
    int n = (int)strlen(p);
    if (n > perLine) {
      n = perLine;
      while (n > 0 && p[n] != ' ') n--;
      if (n == 0) n = perLine;
    }
    if (n > 63) n = 63;
    memcpy(line, p, (size_t)n);
    line[n] = '\0';
    r_print(x, y + nLines * 10.0f, 1, col, line, 0);
    nLines++;
    p += n;
  }
  return nLines;
}

void ui_kv(float x, float y, float w, const char *label, const char *value,
           uint32_t lc, uint32_t vc) {
  r_print(x, y, 1, lc, label, 0);
  r_print(x + w, y, 1, vc, value, 2);
  /* puntini di guida fra etichetta e valore */
  float a = x + r_print_w(label, 1) + 4.0f;
  float b = x + w - r_print_w(value, 1) - 4.0f;
  for (float d = a; d < b; d += 4.0f) r_rect(d, y + 6.0f, 1.0f, 1.0f, E_DGRAY);
}

/* ------------------------------------------------------------------ */
/* Barre / menu                                                        */
/* ------------------------------------------------------------------ */

void ui_bar(float x, float y, float w, float h, float frac, uint32_t fill) {
  if (frac < 0.0f) frac = 0.0f; else if (frac > 1.0f) frac = 1.0f;
  ui_frame(x, y, w, h, E_DGRAY);
  /* a segmenti di 4 pixel, come una barra di caratteri pieni */
  int nSeg = (int)((w - 4.0f) / 4.0f);
  int on = (int)(frac * nSeg + 0.001f);
  for (int i = 0; i < nSeg; i++)
    r_rect(x + 2.0f + i * 4.0f, y + 2.0f, 3.0f, h - 4.0f, i < on ? fill : E_BLACK);
}

void ui_menu_row(float x, float y, float w, const char *label, int selected, float now) {
  if (selected) {
    r_rect(x, y, w, 14.0f, UI_BAR);
    int ph = ((int)(now * 4.0f)) & 1;
    r_char(x + 4.0f + ph, y + 3.0f, 1, E_YELLOW, 0x10);         /* triangolo */
    r_char(x + w - 12.0f - ph, y + 3.0f, 1, E_YELLOW, 0x11);
    r_print(x + w * 0.5f, y + 3.0f, 1, E_WHITE, label, 1);
  } else {
    r_print(x + w * 0.5f, y + 3.0f, 1, UI_DIM, label, 1);
  }
}

/* ------------------------------------------------------------------ */
/* Effetti                                                             */
/* ------------------------------------------------------------------ */

void ui_fade(float alpha) {
  int a = (int)(alpha * 255.0f);
  if (a <= 0) return;
  if (a > 255) a = 255;
  r_rect_a(0.0f, 0.0f, (float)r_screen_w(), 240.0f, r_color(0, 0, 0, a));
}

int ui_blink(float now, float period) {
  if (period <= 0.0f) return 1;
  float ph = now / period;
  return (ph - floorf(ph)) < 0.6f;
}

/* ------------------------------------------------------------------ */
/* Touch                                                               */
/* ------------------------------------------------------------------ */

static int prevDown = 0;

int ui_touch(int *x, int *y, int *justDown) {
  touchPosition pos;
  memset(&pos, 0, sizeof(pos));
  hidTouchRead(&pos);
  int down = (hidKeysHeld() & KEY_TOUCH) ? 1 : 0;
  int px = (int)pos.px, py = (int)pos.py;
  if (px < 0) px = 0; else if (px > 319) px = 319;
  if (py < 0) py = 0; else if (py > 239) py = 239;
  if (justDown) *justDown = (down && !prevDown) ? 1 : 0;
  prevDown = down;
  if (x) *x = px;
  if (y) *y = py;
  return down;
}
