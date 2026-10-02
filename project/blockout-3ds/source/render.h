/*
  File:        render.h
  Description: 3DS software renderer (citro2D) for the BlockOut II port
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

/* Colors: packing identical to C2D_Color32 (R | G<<8 | B<<16 | A<<24).
   The "opaque" primitives force alpha to 0xFF (OPAQUE), those with the
   _a suffix respect the color's alpha. */
#define COL_WHITE  0x00FFFFFFu
#define COL_BLACK  0x00000000u
#define COL_BG     0x00000000u

#define OPAQUE(c)  (((c) & 0x00FFFFFFu) | 0xFF000000u)

class Game;

/* Init / exit */
void render_init(void);
void render_exit(void);

/* Stereoscopic depth: reference disparity in pixels (0 = 2D) */
void render_set_stereo(float px);

/* Current piece: 0 = white wireframe only (like the DOS original, default),
   1 = wireframe + semi-transparent faces */
void render_set_piece_fill(int on);
int  render_get_piece_fill(void);

/* Frame: clear + scene + swap */
void render_clear(uint32_t col);
void render_begin_top(int eye);      /* 0 = left eye, 1 = right */
void render_begin_bottom(void);
void render_flush(void);
void render_swap(int waitVsync);

/* Current eye (0/1) and disparity for a 2D object at "depth"
   k (0 = screen plane, >0 = behind, <0 = in front): used by the menus
   to give relief to the logo and the previews. */
int   r_eye(void);
float r_stereo_px(void);

/* 2D primitives in the current scene (current screen) */
void r_rect(float x, float y, float w, float h, uint32_t col);
void r_rect_a(float x, float y, float w, float h, uint32_t col);   /* alpha preserved */
void r_line(float x0, float y0, float x1, float y1, float thick, uint32_t col);
void r_line_a(float x0, float y0, float x1, float y1, float thick, uint32_t col);
void r_tri_a(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t col);
void r_quad_a(const float *xy, uint32_t col);                       /* 4 vertices x,y */

/* Text: 8x8 bitmap font (the libctru console one, CP437 set),
   sampled "nearest" and at integer scale: sharp as on the DOS screen.
   align: 0 left (x = left edge), 1 center, 2 right. */
#define FONT_W 8
#define FONT_H 8
void  r_print(float x, float y, int scale, uint32_t col, const char *str, int align);
void  r_print_a(float x, float y, int scale, uint32_t col, const char *str, int align); /* alpha */
/* Like r_print with a 1-pixel scaled black shadow (readable over the scene) */
void  r_print_sh(float x, float y, int scale, uint32_t col, const char *str, int align);
float r_print_w(const char *str, int scale);
/* A single character (also CP437 >= 128) */
void  r_char(float x, float y, int scale, uint32_t col, unsigned char c);

int   r_screen_w(void);              /* 400 (top) o 320 (bottom) */
int   r_is_bottom(void);

/* The drawn game (both eyes + bottom screen) */
void render_game(Game *game);

/* Color utilities */
uint32_t r_color(int r, int g, int b, int a);

#endif /* RENDERH */
