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
   Le primitive "opache" forzano l'alpha a 0xFF (OPAQUE), quelle con
   suffisso _a rispettano l'alpha del colore. */
#define COL_WHITE  0x00FFFFFFu
#define COL_BLACK  0x00000000u
#define COL_BG     0x00000000u

#define OPAQUE(c)  (((c) & 0x00FFFFFFu) | 0xFF000000u)

class Game;

/* Init / exit */
void render_init(void);
void render_exit(void);

/* Profondita' stereoscopica: disparita' di riferimento in pixel (0 = 2D) */
void render_set_stereo(float px);

/* Pezzo corrente: 0 = solo filo bianco (come l'originale DOS, default),
   1 = filo + facce semitrasparenti */
void render_set_piece_fill(int on);
int  render_get_piece_fill(void);

/* Frame: clear + scene + swap */
void render_clear(uint32_t col);
void render_begin_top(int eye);      /* 0 = occhio sinistro, 1 = destro */
void render_begin_bottom(void);
void render_flush(void);
void render_swap(int waitVsync);

/* Occhio corrente (0/1) e disparita' per un oggetto 2D a "profondita'"
   k (0 = piano dello schermo, >0 = dietro, <0 = davanti): usato dai menu
   per dare rilievo al logo e alle anteprime. */
int   r_eye(void);
float r_stereo_px(void);

/* Primitivi 2D nella scena corrente (schermo corrente) */
void r_rect(float x, float y, float w, float h, uint32_t col);
void r_rect_a(float x, float y, float w, float h, uint32_t col);   /* alpha preservato */
void r_line(float x0, float y0, float x1, float y1, float thick, uint32_t col);
void r_line_a(float x0, float y0, float x1, float y1, float thick, uint32_t col);
void r_tri_a(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col);
void r_quad_a(const float *xy, uint32_t col);                       /* 4 vertici x,y */

/* Testo: font bitmap 8x8 (quello della console di libctru, set CP437),
   campionato "nearest" e a scala intera: nitido come sullo schermo DOS.
   align: 0 sinistra (x = bordo sinistro), 1 centro, 2 destra. */
#define FONT_W 8
#define FONT_H 8
void  r_print(float x, float y, int scale, uint32_t col, const char *str, int align);
void  r_print_a(float x, float y, int scale, uint32_t col, const char *str, int align); /* alpha */
/* Come r_print con ombra nera di 1 pixel scalato (leggibile sopra la scena) */
void  r_print_sh(float x, float y, int scale, uint32_t col, const char *str, int align);
float r_print_w(const char *str, int scale);
/* Un solo carattere (anche CP437 >= 128) */
void  r_char(float x, float y, int scale, uint32_t col, unsigned char c);

int   r_screen_w(void);              /* 400 (top) o 320 (bottom) */
int   r_is_bottom(void);

/* Il gioco disegnato (entrambi gli occhi + schermo basso) */
void render_game(Game *game);

/* Utility colore */
uint32_t r_color(int r, int g, int b, int a);

#endif /* RENDERH */
