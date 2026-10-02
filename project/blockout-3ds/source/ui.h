/*
  File:        ui.h
  Description: Layer di UI in stile DOS (palette EGA, riquadri a doppia
               linea, barre di selezione, touch) sopra i primitivi di
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
/* Palette EGA a 16 colori (packing C2D: R | G<<8 | B<<16)               */
/* Il BlockOut per MS-DOS girava in EGA/VGA 16 colori: tutta la UI usa   */
/* solo questi toni.                                                     */
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

/* Ruoli */
#define UI_LABEL   E_LCYAN     /* etichette                           */
#define UI_VALUE   E_YELLOW    /* valori numerici                     */
#define UI_TEXT    E_WHITE     /* testo normale                       */
#define UI_DIM     E_LGRAY     /* testo secondario                    */
#define UI_FRAME   E_LBLUE     /* bordi dei riquadri                  */
#define UI_BAR     E_BLUE      /* barra di selezione (testo bianco)   */
#define UI_KEY     E_LGREEN    /* nomi dei tasti                      */
#define UI_ALERT   E_LRED

/* ------------------------------------------------------------------ */
/* Riquadri                                                            */
/* ------------------------------------------------------------------ */

/* Bordo sottile (1px) */
void ui_frame(float x, float y, float w, float h, uint32_t col);

/* Riquadro a doppia linea (come i box-drawing CP437 dei programmi DOS).
   Il fondo e' riempito con "bg" (E_BLACK per il nero pieno). Se title non
   e' NULL, il titolo e' inciso nel bordo superiore, centrato. */
void ui_box(float x, float y, float w, float h, uint32_t border, uint32_t bg,
            const char *title, uint32_t titleCol);

/* Linea orizzontale sottile */
void ui_hline(float x0, float y, float x1, uint32_t col);

/* ------------------------------------------------------------------ */
/* Testo (font bitmap 8x8, scala intera)                                */
/* ------------------------------------------------------------------ */

/* "A: drop" con il tasto in UI_KEY. Ritorna la larghezza in pixel. */
float ui_key_hint(float x, float y, const char *keys, const char *desc);

/* Testo con a capo sulle parole; ritorna il numero di righe. */
int ui_wrap(float x, float y, uint32_t col, const char *str, float maxw, int maxLines);

/* Etichetta a sinistra + valore allineato a destra sulla stessa riga */
void ui_kv(float x, float y, float w, const char *label, const char *value,
           uint32_t lc, uint32_t vc);

/* ------------------------------------------------------------------ */
/* Barre / menu                                                        */
/* ------------------------------------------------------------------ */

/* Barra di avanzamento a blocchi pieni (stile carattere 0xDB) */
void ui_bar(float x, float y, float w, float h, float frac, uint32_t fill);

/* Riga di menu: selezionata = testo bianco su barra blu con frecce */
void ui_menu_row(float x, float y, float w, const char *label, int selected,
                 float now);

/* ------------------------------------------------------------------ */
/* Effetti                                                             */
/* ------------------------------------------------------------------ */

/* Nero a tutto schermo con alpha (0..1) */
void ui_fade(float alpha);

/* 1 durante la meta' "accesa" del lampeggio */
int ui_blink(float now, float period);

/* ------------------------------------------------------------------ */
/* Touch (solo schermo basso)                                          */
/* ------------------------------------------------------------------ */

int ui_touch(int *x, int *y, int *justDown);

#endif /* UIH */
