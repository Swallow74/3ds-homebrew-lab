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
/* RX: "real" textured polygons inside the citro2d batch.
 *
 * citro2d only draws rectangular images (C2D_DrawImage) or solid-color
 * triangles.  To map a texture onto a projected face (any
 * trapezoid) you need vertices with arbitrary UVs: here they are written directly
 * into the citro2d vertex buffer (C2Di_AppendVtx is exported), with
 * TintMult mode and the procedural atlas bound.  Everything stays in the same batch of
 * citro2d: no shader changes, very few draw calls.
 *
 * Final color = texel * vertex color (rgb and alpha): "gray"
 * textures become colored, true-color textures stay as they are with a
 * white vertex.  Light and fog gradients per vertex are free.
 *
 * If citro2d's internal layout does not match (a version other than the
 * 1.7.0 it was written for), rx_init() detects it and falls back to
 * solid-color C2D_DrawTriangle: no textures, but no crash. */
#pragma once
#include <citro2d.h>

/* s = stereo disparity per unit of parallax (0 = screen plane) */
typedef struct { float x, y, u, v; u32 c; float s; } RxV;

bool rx_init(size_t maxObjects);   /* after C2D_Init(maxObjects)             */
bool rx_ok(void);                  /* false = fallback without textures      */
void rx_set_tex(C3D_Tex *tex);     /* texture (atlas) used by rx_tri/quad    */
void rx_tri(const RxV *a, const RxV *b, const RxV *c);
void rx_quad(const RxV *a, const RxV *b, const RxV *c, const RxV *d);
void rx_additive(bool on);         /* additive glows: flush + blend change   */
void rx_scene(void);               /* scene start: normal blend              */
int  rx_usage(void);               /* % of the citro2d buffers already used  */

/* Cheap stereo: the scene is projected ONCE (central camera) and
 * recorded; then it is replayed for each eye by shifting x by clamp(g*s, +-m).
 * The CPU cost of the 3D becomes almost that of the 2D.  rx_rec_end() = false
 * if the record is full: in that case redraw the old way. */
void rx_rec_begin(void);
bool rx_rec_end(void);
void rx_replay(float g, float m);
