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
#include "rx.h"
#include <string.h>

/* EXACT copy of C2Di_Context/C2Di_Vertex from citro2d 1.7.0
 * (source/internal.h).  Only needed to read the free space in the buffers
 * and to set mode/texture as C2D_DrawImage does. */
typedef struct {
	float pos[3];
	float texcoord[2];
	float ptcoord[2];
	u32 color;
} RxVtx;

typedef struct {
	DVLB_s *shader;
	shaderProgram_s program;
	C3D_AttrInfo attrInfo;
	C3D_BufInfo bufInfo;
	C3D_ProcTex ptBlend;
	C3D_ProcTex ptCircle;
	C3D_ProcTexLut ptBlendLut;
	C3D_ProcTexLut ptCircleLut;
	u32 sceneW, sceneH;

	RxVtx *vtxBuf;
	u16 *idxBuf;

	size_t vtxBufSize;
	size_t vtxBufPos;

	size_t idxBufSize;
	size_t idxBufPos;
	size_t idxBufLastPos;

	u32 flags;
	C3D_Mtx projMtx;
	C3D_Mtx mdlvMtx;
	C3D_Tex *curTex;
	u32 fadeClr;
} RxCtx;

extern RxCtx __C2Di_Context;
void C2Di_Update(void);
void C2Di_FlushVtxBuf(void);

#define F_ACTIVE     BIT(0)
#define F_DIRTYTEX   BIT(3)
#define F_DIRTYMODE  BIT(4)
#define MODE_MASK    (0xf << 8)
#define MODE_MULT    (4 << 8)          /* C2DiF_Mode_ImageMult */

static bool     s_ok = false;
static bool     s_add = false;
static C3D_Tex *s_tex = NULL;

/* scene record for the stereo replay */
#define REC_VTX 16384
#define REC_OPS 8192
enum { OP_TRI, OP_QUAD, OP_ADD_ON, OP_ADD_OFF };
static RxV  s_rv[REC_VTX];
static u8   s_rop[REC_OPS];
static int  s_nv, s_nop;
static bool s_rec = false, s_full = false;

static bool rec_op(u8 op, int nv)
{
	if (s_nop >= REC_OPS || s_nv + nv > REC_VTX) { s_full = true; return false; }
	s_rop[s_nop++] = op;
	return true;
}

bool rx_init(size_t maxObjects)
{
	RxCtx *c = &__C2Di_Context;
	/* layout check: the fields read must have the values set by C2D_Init */
	s_ok = (c->flags & F_ACTIVE) && c->vtxBuf && c->idxBuf &&
	       c->vtxBufSize == 4 * maxObjects && c->idxBufSize == 6 * maxObjects &&
	       c->vtxBufPos <= c->vtxBufSize && c->idxBufPos <= c->idxBufSize;
	return s_ok;
}

bool rx_ok(void) { return s_ok; }

void rx_set_tex(C3D_Tex *tex) { s_tex = tex; }

/* same sequence as C2D_DrawImage: mode + texture, then C2Di_Update()
 * which only flushes if something changed (after text or rectangles) */
static bool rx_bind(unsigned nidx, unsigned nvtx)
{
	RxCtx *c = &__C2Di_Context;
	if (!(c->flags & F_ACTIVE) || !s_tex) return false;
	if (c->idxBufSize - c->idxBufPos < nidx) return false;
	if (c->vtxBufSize - c->vtxBufPos < nvtx) return false;

	if ((c->flags & MODE_MASK) != MODE_MULT)
		c->flags = F_DIRTYMODE | (c->flags & ~MODE_MASK) | MODE_MULT;
	if (c->curTex != s_tex) {
		c->flags |= F_DIRTYTEX;
		c->curTex = s_tex;
	}
	C2Di_Update();
	return true;
}

static inline void vtx(RxCtx *c, const RxV *v)
{
	RxVtx *o = &c->vtxBuf[c->vtxBufPos++];
	o->pos[0] = v->x;
	o->pos[1] = v->y;
	o->pos[2] = 0.5f;
	o->texcoord[0] = v->u;
	o->texcoord[1] = v->v;
	o->ptcoord[0] = 0.0f;
	o->ptcoord[1] = 1.0f;         /* blend = 1: color = texel * vertex */
	o->color = v->c;
}

static void tri_draw(const RxV *a, const RxV *b, const RxV *d);
static void quad_draw(const RxV *a, const RxV *b, const RxV *d, const RxV *e);

void rx_tri(const RxV *a, const RxV *b, const RxV *d)
{
	if (s_rec) {
		if (rec_op(OP_TRI, 3)) {
			s_rv[s_nv++] = *a; s_rv[s_nv++] = *b; s_rv[s_nv++] = *d;
		}
		return;
	}
	tri_draw(a, b, d);
}

void rx_quad(const RxV *a, const RxV *b, const RxV *d, const RxV *e)
{
	if (s_rec) {
		if (rec_op(OP_QUAD, 4)) {
			s_rv[s_nv++] = *a; s_rv[s_nv++] = *b;
			s_rv[s_nv++] = *d; s_rv[s_nv++] = *e;
		}
		return;
	}
	quad_draw(a, b, d, e);
}

static void tri_draw(const RxV *a, const RxV *b, const RxV *d)
{
	if (!s_ok) {
		C2D_DrawTriangle(a->x, a->y, a->c, b->x, b->y, b->c, d->x, d->y, d->c,
			0.5f);
		return;
	}
	if (!rx_bind(3, 3)) return;
	RxCtx *c = &__C2Di_Context;
	u16 base = (u16)c->vtxBufPos;
	c->idxBuf[c->idxBufPos++] = base;
	c->idxBuf[c->idxBufPos++] = base + 1;
	c->idxBuf[c->idxBufPos++] = base + 2;
	vtx(c, a); vtx(c, b); vtx(c, d);
}

static void quad_draw(const RxV *a, const RxV *b, const RxV *d, const RxV *e)
{
	if (!s_ok) {
		tri_draw(a, b, d);
		tri_draw(a, d, e);
		return;
	}
	if (!rx_bind(6, 4)) return;
	RxCtx *c = &__C2Di_Context;
	u16 base = (u16)c->vtxBufPos;
	u16 *ix = &c->idxBuf[c->idxBufPos];
	ix[0] = base; ix[1] = base + 1; ix[2] = base + 2;
	ix[3] = base; ix[4] = base + 2; ix[5] = base + 3;
	c->idxBufPos += 6;
	vtx(c, a); vtx(c, b); vtx(c, d); vtx(c, e);
}

void rx_additive(bool on)
{
	if (s_rec) { rec_op(on ? OP_ADD_ON : OP_ADD_OFF, 0); return; }
	if (on == s_add) return;
	/* blend is global GPU state: first draw what has been accumulated */
	C2D_Flush();
	if (on)
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE,
			GPU_ZERO, GPU_ONE);
	else
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA,
			GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
	s_add = on;
}

void rx_scene(void)
{
	s_add = true;          /* force the restore even if the state is unknown */
	rx_additive(false);
}

int rx_usage(void)
{
	RxCtx *c = &__C2Di_Context;
	if (!s_ok || !c->idxBufSize) return 0;
	size_t a = c->idxBufPos * 100 / c->idxBufSize;
	size_t b = c->vtxBufPos * 100 / c->vtxBufSize;
	return (int)(a > b ? a : b);
}

void rx_rec_begin(void)
{
	s_nv = s_nop = 0;
	s_full = false;
	s_rec = true;
}

bool rx_rec_end(void)
{
	s_rec = false;
	return !s_full;
}

void rx_replay(float g, float m)
{
	RxV t[4];
	const RxV *v = s_rv;
	for (int i = 0; i < s_nop; i++) {
		u8 op = s_rop[i];
		if (op == OP_ADD_ON || op == OP_ADD_OFF) {
			rx_additive(op == OP_ADD_ON);
			continue;
		}
		int n = op == OP_QUAD ? 4 : 3;
		for (int k = 0; k < n; k++) {
			t[k] = v[k];
			if (v[k].s != 0.0f) {
				float d = g * v[k].s;
				t[k].x += d < -m ? -m : (d > m ? m : d);
			}
		}
		if (n == 4) quad_draw(&t[0], &t[1], &t[2], &t[3]);
		else tri_draw(&t[0], &t[1], &t[2]);
		v += n;
	}
}
