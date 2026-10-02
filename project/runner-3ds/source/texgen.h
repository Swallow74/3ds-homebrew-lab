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
/* TexGen: a single 256x256 RGBA8 atlas generated at runtime (no files,
 * no romfs), with a full mipmap chain (no flickering of
 * distant windows).  A single atlas = a single texture bound for the whole
 * 3D world: citro2d merges almost everything into very few draw calls.
 *
 * The tiles are of two families:
 *  - gray/white "masks": the color is decided by the vertex (TintMult);
 *  - true-color tiles (sun, coin, icons, glass): white vertex.
 *
 * Upload: the texels go in the PICA's native format, i.e. in 8x8 blocks
 * in Morton order and with A,B,G,R bytes.  The first row of the image is
 * v = 1 (top), as for tex3ds images. */
#pragma once
#include <citro2d.h>

typedef enum {
	TX_ROAD,      /* road slabs (gray)                           64x64 */
	TX_FACADE_A,  /* facade, warm windows (true color)           64x64 */
	TX_FACADE_B,  /* facade, cold ribbon windows                 64x64 */
	TX_ENERGY,    /* hexagon bulkhead of the wall (gray)         64x64 */
	TX_HULL,      /* hull panels (gray)                          64x64 */
	TX_SUN,       /* striped synthwave sun (true color)          64x64 */
	TX_GROUND,    /* tile with neon grid (gray)                  64x64 */
	TX_HAZARD,    /* hazard stripes (gray)                       64x32 */
	TX_ROOF,      /* roof / technical sides (gray)               64x32 */
	TX_GLOW,      /* soft halo                                   32x32 */
	TX_RING,      /* ring                                        32x32 */
	TX_STAR,      /* 4-point sparkle                             32x32 */
	TX_COIN,      /* gold coin (true color)                      32x32 */
	TX_MAGNET,    /* magnet icon (true color)                    32x32 */
	TX_SHIELD,    /* shield icon (true color)                    32x32 */
	TX_X2,        /* 2X icon (true color)                        32x32 */
	TX_MARK,      /* marker diamond                              32x32 */
	TX_SHADE,     /* elliptical shadow                           32x16 */
	TX_WHITE,     /* solid white (solid-color polygons)          16x16 */
	TX_FLAME,     /* thruster flame                              16x32 */
	TX_BEAM,      /* faded vertical beam                         16x32 */
	TX_VISOR,     /* glowing slit                                64x16 */
	TX_TRAIL,     /* soft profile for lines and trails           64x16 */
	TX_CANOPY,    /* cockpit glass (true color)                  32x32 */
	TX_SIGN_A,    /* neon sign                                   64x32 */
	TX_DASH,      /* glowing lane dash                           32x32 */
	TX_SIGN_B,    /* neon sign (arrow)                           64x32 */
	TX_NUM
} TexId;

typedef struct { float u0, v0, u1, v1; } TexUV;   /* u0,v0 = top-left */

bool         texgen_init(void);   /* false if the atlas cannot be allocated */
void         texgen_exit(void);
C3D_Tex     *texgen_atlas(void);
const TexUV *texgen_uv(TexId id);
