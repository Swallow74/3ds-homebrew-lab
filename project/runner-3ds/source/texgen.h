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
/* TexGen: un unico atlas RGBA8 256x256 generato a runtime (niente file,
 * niente romfs), con catena completa di mipmap (niente sfarfallio delle
 * finestre lontane).  Un solo atlas = una sola texture legata per tutto il
 * mondo 3D: citro2d accorpa quasi tutto in pochissime draw call.
 *
 * Le tile sono di due famiglie:
 *  - "maschere" grigie/bianche: il colore lo decide il vertice (TintMult);
 *  - tile a colori veri (sole, moneta, icone, vetro): vertice bianco.
 *
 * Upload: i texel vanno nel formato nativo della PICA, cioe' a blocchi 8x8
 * in ordine Morton e con byte A,B,G,R.  La prima riga dell'immagine e' la
 * v = 1 (alto), come per le immagini di tex3ds. */
#pragma once
#include <citro2d.h>

typedef enum {
	TX_ROAD,      /* piastre della carreggiata (grigio)          64x64 */
	TX_FACADE_A,  /* facciata, finestre calde (colori veri)      64x64 */
	TX_FACADE_B,  /* facciata, finestre a nastro fredde          64x64 */
	TX_ENERGY,    /* paratia a esagoni del muro (grigio)         64x64 */
	TX_HULL,      /* pannelli scafo (grigio)                     64x64 */
	TX_SUN,       /* sole synthwave a strisce (colori veri)      64x64 */
	TX_GROUND,    /* piastrella con griglia al neon (grigio)     64x64 */
	TX_HAZARD,    /* strisce di pericolo (grigio)                64x32 */
	TX_ROOF,      /* tetto / fianchi tecnici (grigio)            64x32 */
	TX_GLOW,      /* alone morbido                               32x32 */
	TX_RING,      /* anello                                      32x32 */
	TX_STAR,      /* scintilla a 4 punte                         32x32 */
	TX_COIN,      /* moneta d'oro (colori veri)                  32x32 */
	TX_MAGNET,    /* icona magnete (colori veri)                 32x32 */
	TX_SHIELD,    /* icona scudo (colori veri)                   32x32 */
	TX_X2,        /* icona 2X (colori veri)                      32x32 */
	TX_MARK,      /* rombo di segnalazione                       32x32 */
	TX_SHADE,     /* ombra ellittica                             32x16 */
	TX_WHITE,     /* bianco pieno (poligoni a tinta unita)       16x16 */
	TX_FLAME,     /* fiamma del propulsore                       16x32 */
	TX_BEAM,      /* fascio verticale sfumato                    16x32 */
	TX_VISOR,     /* feritoia luminosa                           64x16 */
	TX_TRAIL,     /* profilo morbido per linee e scie            64x16 */
	TX_CANOPY,    /* vetro dell'abitacolo (colori veri)          32x32 */
	TX_SIGN_A,    /* insegna al neon                             64x32 */
	TX_DASH,      /* tratteggio di corsia luminoso               32x32 */
	TX_SIGN_B,    /* insegna al neon (freccia)                   64x32 */
	TX_NUM
} TexId;

typedef struct { float u0, v0, u1, v1; } TexUV;   /* u0,v0 = alto-sinistra */

bool         texgen_init(void);   /* false se l'atlas non e' allocabile */
void         texgen_exit(void);
C3D_Tex     *texgen_atlas(void);
const TexUV *texgen_uv(TexId id);
