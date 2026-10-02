/*
 * Copyright (C) 2026 Alessandro Del Rosso
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/* RX: poligoni texturizzati "veri" dentro il batch di citro2d.
 *
 * citro2d disegna solo immagini rettangolari (C2D_DrawImage) o triangoli a
 * tinta unita.  Per mappare una texture su una faccia proiettata (trapezio
 * qualsiasi) servono vertici con UV arbitrarie: qui si scrivono direttamente
 * nel vertex buffer di citro2d (C2Di_AppendVtx e' esportata), con modalita'
 * TintMult e l'atlas procedurale legato.  Tutto resta nello stesso batch di
 * citro2d: nessun cambio di shader, pochissime draw call.
 *
 * Colore finale = texel * colore del vertice (rgb e alpha): texture
 * "grigie" diventano colorate, texture a colori vere restano tali con
 * vertice bianco.  Gradienti di luce e nebbia per vertice sono gratis.
 *
 * Se il layout interno di citro2d non corrisponde (versione diversa dalla
 * 1.7.0 per cui e' scritto), rx_init() lo rileva e si ripiega su
 * C2D_DrawTriangle a tinta unita: niente texture, ma nessun crash. */
#pragma once
#include <citro2d.h>

/* s = disparita' stereo per unita' di parallasse (0 = piano dello schermo) */
typedef struct { float x, y, u, v; u32 c; float s; } RxV;

bool rx_init(size_t maxObjects);   /* dopo C2D_Init(maxObjects)             */
bool rx_ok(void);                  /* false = fallback senza texture        */
void rx_set_tex(C3D_Tex *tex);     /* texture (atlas) usata da rx_tri/quad  */
void rx_tri(const RxV *a, const RxV *b, const RxV *c);
void rx_quad(const RxV *a, const RxV *b, const RxV *c, const RxV *d);
void rx_additive(bool on);         /* glow additivi: flush + cambio blend   */
void rx_scene(void);               /* inizio scena: blend normale           */
int  rx_usage(void);               /* % dei buffer di citro2d gia' usati    */

/* Stereo economico: la scena si proietta UNA volta (camera centrale) e si
 * registra; poi si rigioca per ogni occhio spostando x di clamp(g*s, +-m).
 * Il costo CPU del 3D diventa quasi quello del 2D.  rx_rec_end() = false
 * se il registro e' pieno: in quel caso ridisegnare alla vecchia maniera. */
void rx_rec_begin(void);
bool rx_rec_end(void);
void rx_replay(float g, float m);
