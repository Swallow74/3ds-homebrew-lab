/*
  File:        ui.h
  Description: DOS-style UI layer (EGA palette, double-line
               frames, selection bars, touch) on top of the primitives of
               render.cpp
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef UIH
#define UIH

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* 16-color EGA palette (C2D packing: R | G<<8 | B<<16)                  */
/* The MS-DOS BlockOut ran in 16-color EGA/VGA: the whole UI uses        */
/* only these tones.                                                     */
/* ------------------------------------------------------------------ */

#define EGA(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))

#define E_BLACK    EGA(0x00, 0x00, 0x00)
#define E_BLUE     EGA(0x00, 0x00, 0xAA)
#define E_GREEN    EGA(0x00, 0xAA, 0x00)
#define E_CYAN     EGA(0x00, 0xAA, 0xAA)
#define E_RED      EGA(0xAA, 0x00, 0x00)
#define E_MAGENTA  EGA(0xAA, 0x00, 0xAA)
#define E_BROWN    EGA(0xAA, 0x55, 0x00)
#define E_LGRAY    EGA(0xAA, 0xAA, 0xAA)
#define E_DGRAY    EGA(0x55, 0x55, 0x55)
#define E_LBLUE    EGA(0x55, 0x55, 0xFF)
#define E_LGREEN   EGA(0x55, 0xFF, 0x55)
#define E_LCYAN    EGA(0x55, 0xFF, 0xFF)
#define E_LRED     EGA(0xFF, 0x55, 0x55)
#define E_LMAGENTA EGA(0xFF, 0x55, 0xFF)
#define E_YELLOW   EGA(0xFF, 0xFF, 0x55)
#define E_WHITE    EGA(0xFF, 0xFF, 0xFF)

/* Roles */
#define UI_LABEL   E_LCYAN     /* labels                              */
#define UI_VALUE   E_YELLOW    /* numeric values                      */
#define UI_TEXT    E_WHITE     /* normal text                         */
#define UI_DIM     E_LGRAY     /* secondary text                      */
#define UI_FRAME   E_LBLUE     /* frame borders                       */
#define UI_BAR     E_BLUE      /* selection bar (white text)          */
#define UI_KEY     E_LGREEN    /* key names                           */
#define UI_ALERT   E_LRED

/* ------------------------------------------------------------------ */
/* Riquadri                                                            */
/* ------------------------------------------------------------------ */

/* Thin border (1px) */
void ui_frame(float x, float y, float w, float h, uint32_t col);

/* Double-line frame (like the CP437 box-drawing of DOS programs).
   The background is filled with "bg" (E_BLACK for solid black). If title is not
   NULL, the title is engraved in the top border, centered. */
void ui_box(float x, float y, float w, float h, uint32_t border, uint32_t bg,
            const char *title, uint32_t titleCol);

/* Thin horizontal line */
void ui_hline(float x0, float y, float x1, uint32_t col);

/* ------------------------------------------------------------------ */
/* Text (8x8 bitmap font, integer scale)                                */
/* ------------------------------------------------------------------ */

/* "A: drop" with the key in UI_KEY. Returns the width in pixels. */
float ui_key_hint(float x, float y, const char *keys, const char *desc);

/* Word-wrapped text; returns the number of lines. */
int ui_wrap(float x, float y, uint32_t col, const char *str, float maxw, int maxLines);

/* Label on the left + value right-aligned on the same line */
void ui_kv(float x, float y, float w, const char *label, const char *value,
           uint32_t lc, uint32_t vc);

/* ------------------------------------------------------------------ */
/* Barre / menu                                                        */
/* ------------------------------------------------------------------ */

/* Block progress bar (character style 0xDB) */
void ui_bar(float x, float y, float w, float h, float frac, uint32_t fill);

/* Menu row: selected = white text on a blue bar with arrows */
void ui_menu_row(float x, float y, float w, const char *label, int selected,
                 float now);

/* ------------------------------------------------------------------ */
/* Effects                                                             */
/* ------------------------------------------------------------------ */

/* Full-screen black with alpha (0..1) */
void ui_fade(float alpha);

/* 1 during the "on" half of the blink */
int ui_blink(float now, float period);

/* ------------------------------------------------------------------ */
/* Touch (bottom screen only)                                          */
/* ------------------------------------------------------------------ */

int ui_touch(int *x, int *y, int *justDown);

#endif /* UIH */
