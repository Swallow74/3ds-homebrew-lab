/*
  File:        render.h
  Description: Renderer software 3DS (citro2D) per il port di BlockOut II
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef RENDERH
#define RENDERH

#include <stdint.h>

/* Colori: packing identico a C2D_Color32 (R | G<<8 | B<<16 | A<<24).
   ATTENZIONE: l'alpha a 0 rende tutto trasparente (schermo vuoto), quindi
   ogni colore disegnato deve avere A = 0xFF. */
#define COL_WHITE  0x00FFFFFFu
#define COL_BLACK  0x00000000u
#define COL_GRAY   0x00808080u
#define COL_RED    0x000000FFu
#define COL_GREEN  0x0000FF00u
#define COL_BG     0x00000000u

#define OPAQUE(c)  (((c) & 0x00FFFFFFu) | 0xFF000000u)

class Game;

/* Init / exit */
void render_init(void);
void render_exit(void);

/* Profondita stereoscopica (disparita' di riferimento in pixel, 0 = 2D) */
void render_set_stereo(float yaw);

/* Frame: clear + scene + swap */
void render_clear(uint32_t col);
void render_begin_top(int eye);      /* 0 = occhio sinistro, 1 = destro */
void render_begin_bottom(void);
void render_flush(void);
void render_swap(int waitVsync);
void render_boot_test(void);   /* diagnostica: barre + testo sul primo frame */

/* Primitivi 2D nella scena corrente (schermo corrente) */
void r_rect(float x, float y, float w, float h, uint32_t col);
void r_line(float x0, float y0, float x1, float y1, float thick, uint32_t col);
void r_text(float x, float y, float hpx, uint32_t col, const char *str, int align, float ax);
float r_text_width(const char *str, float hpx);
int   r_screen_w(void);              /* 400 (top) o 320 (bottom) */
int   r_is_bottom(void);

/* Il gioco disegnato (entrambi gli occhi) */
void render_game(Game *game);

/* Utility colore */
uint32_t r_color(int r, int g, int b, int a);

#endif /* RENDERH */
