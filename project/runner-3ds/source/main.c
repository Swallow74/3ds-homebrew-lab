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
/*
 * NEON RUSH 3DS - "into the screen" runner for the Nintendo 3DS
 *
 * 3 lanes, oncoming obstacles, jumping with gravity, coins and
 * power-ups, comfortable stereoscopy (parallel-shift: no eye rotates,
 * disparity is always and only behind the screen plane), DSP audio with
 * looping music + effects, best score and coins saved to the SD card.
 *
 * Graphics: polygons with procedural textures mapped onto the projected faces
 * (single atlas with mipmaps, see texgen.c / rx.c), per-face directional light,
 * ambient occlusion toward the ground and per-vertex fog, additive
 * glows.  Everything is generated at runtime: no external assets.
 *
 * Controls (see also the bottom screen):
 *   TITLE   : Up/Down item, Left/Right change value, A confirm, START exit
 *   Game    : Left/Right lane, A/B/Up jump, Down in the air = dive,
 *             START (or touch PAUSE) = pause, SELECT = music on/off
 *   CRASH   : A retry, B title
 *   DEMO    : any button returns to the title
 *
 * Score (everything multiplied by difficulty and multiplier):
 *   distance 1/m, coin 10, clean jump 25, dodge 2, power-up 50.
 *   The multiplier rises by 1 every 20 coins of the run (max x5) and the
 *   2X power-up doubles it for 10 seconds.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "audio.h"
#include "texgen.h"
#include "rx.h"

#define MAX_OBJECTS 12288      /* primitives per frame: 2 eyes + bottom screen */

/* ------------------------------- vectors ---------------------------------- */
typedef struct { float x, y, z; } V3;

static inline V3 v3(float x, float y, float z) { V3 r = {x, y, z}; return r; }

static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }

static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static V3 vcross(V3 a, V3 b)
{
	return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

static V3 vnorm(V3 a)
{
	float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
	if (l < 1e-6f) return v3(0.0f, 0.0f, 0.0f);
	return v3(a.x / l, a.y / l, a.z / l);
}

static inline V3 vlerp(V3 a, V3 b, float t)
{
	return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

static inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* camera behind the player, looking into the screen (+z = far) */
static const V3 CAM_BASE = {0.0f, 3.6f, -7.5f};
static const V3 CAM_TGT  = {0.0f, 1.0f, 9.0f};
#define FOCAL 300.0f
#define OX    200.0f
#define OY    132.0f    /* higher than the center: the ship stays fully in view */

/* COMFORTABLE STEREOSCOPY
 * Both eyes use the SAME camera (the central one): the difference
 * between the two images is only a horizontal shift "sh" in pixels that
 * depends on depth (parallel-shift + depth shear).  The zero-disparity
 * plane (D_SCREEN) is IN FRONT of all the 3D content: disparity is
 * always "uncrossed" (object behind the glass), the eyes must NEVER
 * cross.  HUD and flashes are in screen-space (on the glass); sky, sun and
 * mountains use the maximum shift (at infinity). */
#define D_SCREEN 3.2f
#define EYE_BASE 0.085f
#define MAX_DISP 7.5f

typedef struct { V3 eye, fwd, right, up; } Cam;

static Cam camMid;
static float g_s = 0.0f;                 /* +/- parallax of the current eye */
static float g_shx = 0.0f, g_shy = 0.0f; /* shake: IDENTICAL for both */
static float g_horY = 100.0f;            /* horizon in pixels */

static Cam make_cam(void)
{
	V3 e = CAM_BASE;
	V3 fwd = vnorm(vsub(CAM_TGT, e));
	V3 right = vnorm(vcross(v3(0.0f, 1.0f, 0.0f), fwd));
	V3 up = vcross(fwd, right);
	Cam c = { e, fwd, right, up };
	return c;
}

static float depth_of(V3 p) { return vdot(vsub(p, camMid.eye), camMid.fwd); }

static float eye_shift(float d)
{
	if (g_s == 0.0f) return 0.0f;
	float sh = g_s * FOCAL * (1.0f / D_SCREEN - 1.0f / d);
	return clampf(sh, -MAX_DISP, MAX_DISP);
}

/* disparity per unit of parallax of the last projected point: world
 * vertices carry it with them (RxV.s) for the stereo replay */
static float g_psh = 0.0f;

static bool project(V3 p, float *sx, float *sy)
{
	V3 v = vsub(p, camMid.eye);
	float d = vdot(v, camMid.fwd);
	if (d < 0.6f) return false;
	g_psh = FOCAL * (1.0f / D_SCREEN - 1.0f / d);
	*sx = OX + vdot(v, camMid.right) * FOCAL / d + eye_shift(d) + g_shx;
	*sy = OY - vdot(v, camMid.up) * FOCAL / d + g_shy;
	return true;
}

/* ------------------------------- colors ----------------------------------- */
#define RGBA(r, g, b, a) C2D_Color32(r, g, b, a)

static u32 colBg, colWhite, colDim, colAccent, colPink, colGold, colHint,
	colSkyTop, colSkyMid, colFog, colGroundNear, colMountTop, colMountBase,
	colRoad, colGrid, colRail, colDash, colBarr, colWall, colPlayer,
	colPlayer2, colShadow, colPanel, colPanelEdge;

/* building palette: light tints (the texture already has its own colors) and
 * neon trim */
static const u32 BLD_TINT[4] = {
	0xFFFFD8E0u, 0xFFFFE4D0u, 0xFFECD8FFu, 0xFFF4FFD8u };  /* ABGR */
static const u32 BLD_NEON[4] = {
	0xFFA03FFFu, 0xFFFFE500u, 0xFFFF60C0u, 0xFF60FFA0u };

#define FOG_NEAR 12.0f
#define FOG_FAR  60.0f
#define FOG_MAX  0.86f
#define FADE0    50.0f     /* beyond: alpha fade (no pop-in) */
#define FADE1    62.0f

static u32 shade(u32 c, float f)
{
	unsigned r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	r = (unsigned)(r * f); g = (unsigned)(g * f); b = (unsigned)(b * f);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (c & 0xFF000000u) | (b << 16) | (g << 8) | r;
}

static u32 with_a(u32 col, float a)
{
	unsigned al = (unsigned)(clampf(a, 0.0f, 1.0f) * 255.0f);
	return (col & 0x00FFFFFFu) | (al << 24);
}

static u32 mul_a(u32 col, float f)
{
	return with_a(col, (float)(col >> 24) / 255.0f * f);
}

/* mixes rgb and alpha */
static u32 mixc(u32 a, u32 b, float t)
{
	t = clampf(t, 0.0f, 1.0f);
	u32 r = 0;
	for (int s = 0; s < 32; s += 8) {
		float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
		r |= ((u32)(ca + (cb - ca) * t) & 0xFF) << s;
	}
	return r;
}

/* fog: rgb toward the horizon color, alpha untouched; very far away
 * also an alpha fade, so objects appear without "pop" */
static u32 fogv(u32 col, float d)
{
	float t = clampf((d - FOG_NEAR) / (FOG_FAR - FOG_NEAR), 0.0f, FOG_MAX);
	u32 c = (mixc(col, colFog, t) & 0x00FFFFFFu) | (col & 0xFF000000u);
	if (d > FADE0) c = mul_a(c, clampf((FADE1 - d) / (FADE1 - FADE0), 0.0f, 1.0f));
	return c;
}

/* "fog" alpha for additive glows: 1 near, 0 far */
static float fogA(float d)
{
	return clampf(1.0f - (d - FOG_NEAR) / (FADE1 - FOG_NEAR), 0.0f, 1.0f);
}

/* directional light: from above, from the right and from behind the camera */
static V3 LIGHT;
#define AMB 0.46f
#define DIF 0.58f

/* ---------------------------- primitive rx -------------------------------- */
static inline RxV rv(float x, float y, float u, float v, u32 c)
{
	RxV r = {x, y, u, v, c, 0.0f};
	return r;
}

static inline RxV rvs(float x, float y, float u, float v, u32 c, float s)
{
	RxV r = {x, y, u, v, c, s};
	return r;
}

/* rectangle in screen-space, vertical gradient (top -> bottom) */
static void sq2(TexId t, float x, float y, float w, float h, u32 ct, u32 cb)
{
	const TexUV *uv = texgen_uv(t);
	RxV a = rv(x, y, uv->u0, uv->v0, ct), b = rv(x + w, y, uv->u1, uv->v0, ct),
		c = rv(x + w, y + h, uv->u1, uv->v1, cb), d = rv(x, y + h, uv->u0, uv->v1, cb);
	rx_quad(&a, &b, &c, &d);
}

static void sq(TexId t, float x, float y, float w, float h, u32 c)
{
	sq2(t, x, y, w, h, c, c);
}

static void rect(float x, float y, float w, float h, u32 c) { sq(TX_WHITE, x, y, w, h, c); }

/* soft line in screen-space (neon profile of TX_TRAIL);
 * s0/s1 = disparity of the endpoints (0 = on the glass) */
static void sline_s(float x0, float y0, float s0, float x1, float y1, float s1,
	float w, u32 c0, u32 c1)
{
	float dx = x1 - x0, dy = y1 - y0;
	float l = sqrtf(dx * dx + dy * dy);
	if (l < 0.01f) return;
	float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
	const TexUV *uv = texgen_uv(TX_TRAIL);
	RxV a = rvs(x0 + nx, y0 + ny, uv->u0, uv->v0, c0, s0),
		b = rvs(x1 + nx, y1 + ny, uv->u1, uv->v0, c1, s1),
		c = rvs(x1 - nx, y1 - ny, uv->u1, uv->v1, c1, s1),
		d = rvs(x0 - nx, y0 - ny, uv->u0, uv->v1, c0, s0);
	rx_quad(&a, &b, &c, &d);
}

/* segment in the world: width in pixels, fog per endpoint */
static void wline(V3 a, V3 b, float wpx, u32 col)
{
	float x0, y0, x1, y1, s0;
	if (!project(a, &x0, &y0)) return;
	s0 = g_psh;
	if (!project(b, &x1, &y1)) return;
	sline_s(x0, y0, s0, x1, y1, g_psh, wpx, fogv(col, depth_of(a)),
		fogv(col, depth_of(b)));
}

/* camera-facing sprite: w/h in world units */
static void bbr(TexId t, V3 p, float w, float h, float ang, u32 col)
{
	float sx, sy;
	if (((col >> 24) & 0xFF) < 2) return;
	if (!project(p, &sx, &sy)) return;
	float d = depth_of(p), ps = g_psh;
	float hw = FOCAL * w / d * 0.5f, hh = FOCAL * h / d * 0.5f;
	if (hw < 0.25f && hh < 0.25f) return;
	if (sx + hw < -20.0f || sx - hw > 420.0f || sy + hh < -20.0f || sy - hh > 260.0f)
		return;
	const TexUV *uv = texgen_uv(t);
	float c = 1.0f, s = 0.0f;
	if (ang != 0.0f) { c = cosf(ang); s = sinf(ang); }
	float ax = -hw * c + hh * s, ay = -hw * s - hh * c;   /* top-left */
	float bx = hw * c + hh * s, by = hw * s - hh * c;     /* top-right  */
	RxV A = rvs(sx + ax, sy + ay, uv->u0, uv->v0, col, ps),
		B = rvs(sx + bx, sy + by, uv->u1, uv->v0, col, ps),
		C = rvs(sx - ax, sy - ay, uv->u1, uv->v1, col, ps),
		D = rvs(sx - bx, sy - by, uv->u0, uv->v1, col, ps);
	rx_quad(&A, &B, &C, &D);
}

static void bb(TexId t, V3 p, float w, float h, u32 col) { bbr(t, p, w, h, 0.0f, col); }

/* Flat textured face, vertices seen from outside:
 * p0 bottom-left, p1 bottom-right, p2 top-right, p3 top-left.
 * Subdivided into su x sv cells: rep = tile repeated per cell, otherwise
 * stretched over the whole face (cells are only needed for perspective).
 * ao < 1: vertices near the ground are darkened (ambient occlusion).
 * cull: skips back-facing faces.  Returns true if it was drawn. */
#define GRID_MAX 8
static bool wface(TexId t, V3 p0, V3 p1, V3 p2, V3 p3, int su, int sv, bool rep,
	u32 col, float ao, bool cull)
{
	float gx[GRID_MAX + 1][GRID_MAX + 1], gy[GRID_MAX + 1][GRID_MAX + 1];
	float gs[GRID_MAX + 1][GRID_MAX + 1];
	u32 gc[GRID_MAX + 1][GRID_MAX + 1];

	if (cull) {
		float x0, y0, x1, y1, x3, y3;
		if (!project(p0, &x0, &y0) || !project(p1, &x1, &y1) ||
		    !project(p3, &x3, &y3))
			return false;
		/* signed area (y down): face toward us = negative */
		if ((x1 - x0) * (y3 - y0) - (y1 - y0) * (x3 - x0) >= 0.0f) return false;
	}
	su = su < 1 ? 1 : (su > GRID_MAX ? GRID_MAX : su);
	sv = sv < 1 ? 1 : (sv > GRID_MAX ? GRID_MAX : sv);

	for (int j = 0; j <= sv; j++) {
		float fv = (float)j / (float)sv;
		V3 l = vlerp(p0, p3, fv), r = vlerp(p1, p2, fv);
		for (int i = 0; i <= su; i++) {
			V3 p = vlerp(l, r, (float)i / (float)su);
			if (!project(p, &gx[j][i], &gy[j][i])) return false;
			gs[j][i] = g_psh;
			float f = 1.0f;
			if (ao < 1.0f) f = ao + (1.0f - ao) * clampf(p.y / 1.6f, 0.0f, 1.0f);
			gc[j][i] = fogv(f < 1.0f ? shade(col, f) : col, depth_of(p));
		}
	}

	const TexUV *uv = texgen_uv(t);
	float du = uv->u1 - uv->u0, dv = uv->v0 - uv->v1;
	for (int j = 0; j < sv; j++)
		for (int i = 0; i < su; i++) {
			float ua, ub, va, vb;       /* va = basso, vb = alto */
			if (rep) { ua = uv->u0; ub = uv->u1; va = uv->v1; vb = uv->v0; }
			else {
				ua = uv->u0 + du * (float)i / su;
				ub = uv->u0 + du * (float)(i + 1) / su;
				va = uv->v1 + dv * (float)j / sv;
				vb = uv->v1 + dv * (float)(j + 1) / sv;
			}
			RxV a = rvs(gx[j][i], gy[j][i], ua, va, gc[j][i], gs[j][i]),
				b = rvs(gx[j][i + 1], gy[j][i + 1], ub, va, gc[j][i + 1],
					gs[j][i + 1]),
				c = rvs(gx[j + 1][i + 1], gy[j + 1][i + 1], ub, vb,
					gc[j + 1][i + 1], gs[j + 1][i + 1]),
				d = rvs(gx[j + 1][i], gy[j + 1][i], ua, vb, gc[j + 1][i],
					gs[j + 1][i]);
			rx_quad(&a, &b, &c, &d);
		}
	return true;
}

/* flat strip on the ground from a to b, 2*hw wide, n cells long.
 * alongU = false: the tile is "vertical" in the direction of travel (v along
 * the strip, e.g. TX_DASH); true: u along the strip (profiles like TX_TRAIL). */
static void wstrip(TexId t, V3 a, V3 b, float hw, int n, bool alongU, u32 col)
{
	V3 dir = vnorm(vsub(b, a));
	V3 sd = v3(dir.z * hw, 0.0f, -dir.x * hw);        /* perpendicular in xz */
	V3 al = v3(a.x - sd.x, a.y, a.z - sd.z), ar = v3(a.x + sd.x, a.y, a.z + sd.z);
	V3 bl = v3(b.x - sd.x, b.y, b.z - sd.z), br = v3(b.x + sd.x, b.y, b.z + sd.z);
	if (alongU) wface(t, al, bl, br, ar, n, 1, false, col, 1.0f, false);
	else        wface(t, al, ar, br, bl, 1, n, false, col, 1.0f, false);
}

/* ------------------------------- box -------------------------------------- */
/* corners: 0..3 at the bottom, 4..7 at the top */
static const float CN[8][3] = {
	{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1},
	{-1,  1, -1}, {1,  1, -1}, {1,  1, 1}, {-1,  1, 1},
};

/* rigid transform: roll (around z) then pitch (around x) */
typedef struct { V3 piv; float cr, sr, cp, sp; bool rot; } Xf;

static Xf xf_make(V3 piv, float roll, float pitch)
{
	Xf x = { piv, cosf(roll), sinf(roll), cosf(pitch), sinf(pitch),
		roll != 0.0f || pitch != 0.0f };
	return x;
}

static V3 xf_apply(const Xf *x, V3 l)
{
	if (!x) return l;
	if (!x->rot) return v3(x->piv.x + l.x, x->piv.y + l.y, x->piv.z + l.z);
	float x1 = l.x * x->cr - l.y * x->sr;
	float y1 = l.x * x->sr + l.y * x->cr;
	float y2 = y1 * x->cp - l.z * x->sp;
	float z2 = y1 * x->sp + l.z * x->cp;
	return v3(x->piv.x + x1, x->piv.y + y2, x->piv.z + z2);
}

typedef struct {
	TexId tf, ts, tt;          /* textures: front, sides, roof */
	u8 fu, fv, su, sv, tu, tv; /* cells per face */
	bool rep;                  /* tile repeated per cell */
	u32 col;                   /* base tint */
	float ao;                  /* ground occlusion (1 = none) */
	u32 rim;                   /* neon edge color (alpha 0 = none) */
	float rimw;                /* edge thickness in pixels */
} BoxSty;

static float face_light(V3 p0, V3 p1, V3 p3)
{
	V3 n = vnorm(vcross(vsub(p3, p0), vsub(p1, p0)));
	float l = vdot(n, LIGHT);
	return AMB + DIF * (l > 0.0f ? l : 0.0f);
}

static void wbox(V3 c, V3 h, const BoxSty *s, const Xf *xf)
{
	V3 w[8];
	for (int i = 0; i < 8; i++)
		w[i] = xf_apply(xf, v3(c.x + CN[i][0] * h.x, c.y + CN[i][1] * h.y,
			c.z + CN[i][2] * h.z));

	/* front (0,1,5,4), left (3,0,4,7), right (1,2,6,5), roof (4,5,6,7) */
	static const u8 F[4][4] = { {0, 1, 5, 4}, {3, 0, 4, 7}, {1, 2, 6, 5},
		{4, 5, 6, 7} };
	bool vis[4];
	for (int f = 0; f < 4; f++) {
		const u8 *k = F[f];
		TexId t = f == 0 ? s->tf : (f == 3 ? s->tt : s->ts);
		int u = f == 0 ? s->fu : (f == 3 ? s->tu : s->su);
		int v = f == 0 ? s->fv : (f == 3 ? s->tv : s->sv);
		u32 col = shade(s->col, face_light(w[k[0]], w[k[1]], w[k[3]]));
		vis[f] = wface(t, w[k[0]], w[k[1]], w[k[2]], w[k[3]], u, v, s->rep, col,
			f == 3 ? 1.0f : s->ao, true);
	}

	if ((s->rim >> 24) == 0) return;
	float rw = s->rimw;
	wline(w[4], w[5], rw, s->rim);
	wline(w[0], w[4], rw * 0.8f, mul_a(s->rim, 0.7f));
	wline(w[1], w[5], rw * 0.8f, mul_a(s->rim, 0.7f));
	if (vis[2] || vis[3]) wline(w[5], w[6], rw, s->rim);
	if (vis[1] || vis[3]) wline(w[7], w[4], rw, s->rim);
	if (vis[3]) wline(w[6], w[7], rw, s->rim);
}

/* --------------------------- game state ------------------------------- */
#define SPAWN_Z 48.0f
#define MAXOBS 24
#define MAXCOIN 72
#define MAXPOW 3
#define MAXPART 160
#define MAXPOP 5
#define TRAIL_N 14
static const float LANEX[3] = {-2.2f, 0.0f, 2.2f};

enum { ST_TITLE, ST_PLAY, ST_PAUSE, ST_OVER };
enum { PW_MAGNET, PW_SHIELD, PW_X2, PW_NUM };
enum { PK_GLOW, PK_STREAK, PK_STAR, PK_DEBRIS };

typedef struct {
	bool on;
	int lane;      /* 0..2 */
	float z;       /* distance ahead (0 = player) */
	int type;      /* 0 = low barrier (jumpable), 1 = wall (dodge) */
	bool scored;
} Obst;

typedef struct {
	bool on, mag;  /* mag = attracted by the magnet */
	float x, y, z, ph;
} Coin;

typedef struct {
	bool on;
	int kind;
	float x, z, ph;
} Power;

typedef struct {
	bool on;
	int kind;
	float x, y, z, vx, vy, vz, life, maxlife, size, rot;
	u32 color;
} Part;

typedef struct {
	float t;
	char txt[28];
	u32 col;
} Popup;

static Obst obs[MAXOBS];
static Coin coins[MAXCOIN];
static Power pows[MAXPOW];
static Part parts[MAXPART];
static Popup pops[MAXPOP];

static int state = ST_TITLE;
static bool demo;
static int g_diff = 1;         /* 0 easy, 1 normal, 2 hard */
static int menuSel, pauseSel, overSel;

static int lane;               /* target lane 0..2 */
static float laneX, jumpY, jumpV;
static bool airborne;
static float dist, speed, score;
static float flash, flashR, shake, crashT, etime, overT, introT, goT;
static float spawnT, powerDist;
static int coinsRun, chain;
static float chainT, multPulse;
static float pwT[PW_NUM];      /* remaining time of the power-ups (shield: active) */
static float invulT;
static float trailX[TRAIL_N], trailY[TRAIL_N];
static bool newBest;
static int g_gpuUse;           /* % of the vertex buffer used in the previous frame */

/* saving */
static int best, bank, bestDist;

/* Difficulty tables: initial speed, maximum speed, multiplier
 * of the gap between obstacle rows, probability that a lane is a WALL
 * (walls can NOT be jumped), points multiplier. */
static const float D_SPD0[3]   = { 9.0f, 11.0f, 13.0f };
static const float D_SPDMAX[3] = { 22.0f, 30.0f, 36.0f };
static const float D_GAPMUL[3] = { 1.30f, 1.0f, 0.80f };
static const int   D_WALLP[3]  = { 30, 55, 78 };
static const float D_PTS[3]    = { 1.0f, 1.6f, 2.4f };
static const char *const D_NAME[3] = { "EASY", "NORMAL", "HARD" };

#define PW_MAGNET_T 8.0f
#define PW_SHIELD_T 20.0f
#define PW_X2_T     10.0f
static const char *const PW_NAME[PW_NUM] = { "MAGNET", "SHIELD", "2X SCORE" };
static const TexId PW_TEX[PW_NUM] = { TX_MAGNET, TX_SHIELD, TX_X2 };
static const u32 PW_COL[PW_NUM] = { 0xFF6040FFu, 0xFFFFC040u, 0xFF40D0FFu };
static const float PW_MAX[PW_NUM] = { PW_MAGNET_T, PW_SHIELD_T, PW_X2_T };

static float rndf(void) { return (float)(rand() % 1000) / 1000.0f; }

/* deterministic hash: buildings always identical to themselves as they scroll */
static unsigned int hsh(unsigned int v)
{
	v = v * 265465761u;
	v ^= v >> 13;
	v *= 265465761u;
	return v ^ (v >> 16);
}

static int mult_base(void)
{
	int m = 1 + coinsRun / 20;
	return m > 5 ? 5 : m;
}

static float mult_total(void)
{
	return (float)mult_base() * (pwT[PW_X2] > 0.0f ? 2.0f : 1.0f);
}

static void add_points(float base)
{
	score += base * D_PTS[g_diff] * mult_total();
}

/* ------------------------------ saving ------------------------------- */
static const char *SAVE_PATH = "sdmc:/3ds/runner-3ds/best.txt";

static void save_load(void)
{
	best = bank = bestDist = 0;
	FILE *f = fopen(SAVE_PATH, "r");
	if (f) {
		/* format: best, total coins, best distance (the last two
		 * are missing in old saves: they stay at 0) */
		if (fscanf(f, "%d", &best) == 1 && fscanf(f, "%d", &bank) == 1)
			if (fscanf(f, "%d", &bestDist) != 1) bestDist = 0;
		fclose(f);
	}
}

static void save_store(void)
{
	mkdir("sdmc:/3ds", 0777); /* mkdir is not recursive: both levels */
	mkdir("sdmc:/3ds/runner-3ds", 0777);
	FILE *f = fopen(SAVE_PATH, "w");
	if (f) {
		fprintf(f, "%d\n%d\n%d\n", best, bank, bestDist);
		fclose(f);
	}
}

/* ------------------------------ particles -------------------------------- */
static Part *part_new(void)
{
	for (int i = 0; i < MAXPART; i++)
		if (!parts[i].on) return &parts[i];
	return NULL;
}

static void burst(float x, float y, float z, u32 color, int n, int kind)
{
	for (int k = 0; k < n; k++) {
		Part *p = part_new();
		if (!p) return;
		memset(p, 0, sizeof(*p));
		p->on = true;
		p->kind = kind;
		p->color = color;
		p->x = x + (rndf() - 0.5f) * 0.8f;
		p->y = y + rndf() * 0.8f;
		p->z = z;
		p->rot = rndf() * 6.28f;
		switch (kind) {
		case PK_STREAK:
			p->vz = -speed * 1.5f;
			p->life = p->maxlife = 0.5f;
			break;
		case PK_STAR:
			p->vx = (rndf() - 0.5f) * 3.0f;
			p->vy = rndf() * 3.0f + 1.0f;
			p->vz = (rndf() - 0.5f) * 2.0f;
			p->size = 0.35f + rndf() * 0.3f;
			p->life = p->maxlife = 0.35f + rndf() * 0.3f;
			break;
		case PK_DEBRIS:
			p->vx = (rndf() - 0.5f) * 9.0f;
			p->vy = rndf() * 8.0f + 3.0f;
			p->vz = rndf() * 6.0f;
			p->size = 0.12f + rndf() * 0.16f;
			p->life = p->maxlife = 0.9f + rndf() * 0.6f;
			break;
		default:
			p->vx = (rndf() - 0.5f) * 8.0f;
			p->vy = rndf() * 7.0f + 2.0f;
			p->vz = (rndf() - 0.5f) * 4.0f;
			p->size = 0.25f + rndf() * 0.25f;
			p->life = p->maxlife = 0.6f + rndf() * 0.6f;
			break;
		}
	}
}

/* pop-up text: the new one enters at the top, the others scroll */
static void popup_s(u32 col, const char *s)
{
	for (int i = MAXPOP - 1; i > 0; i--) pops[i] = pops[i - 1];
	pops[0].t = 1.3f;
	pops[0].col = col;
	snprintf(pops[0].txt, sizeof(pops[0].txt), "%s", s);
}

static void popup_n(u32 col, const char *pre, int v, const char *post)
{
	char b[28];
	snprintf(b, sizeof(b), "%s%d%s", pre, v, post);
	popup_s(col, b);
}

/* ------------------------------ generation ------------------------------- */
static void game_reset(void)
{
	memset(obs, 0, sizeof(obs));
	memset(coins, 0, sizeof(coins));
	memset(pows, 0, sizeof(pows));
	memset(parts, 0, sizeof(parts));
	memset(pops, 0, sizeof(pops));
	memset(pwT, 0, sizeof(pwT));
	lane = 1; laneX = LANEX[1];
	jumpY = 0; jumpV = 0; airborne = false;
	dist = 0; speed = D_SPD0[g_diff]; score = 0;
	flash = 0; flashR = 0; shake = 0; crashT = 0; overT = 0;
	introT = demo ? 0.0f : 1.8f; goT = 0.0f;
	spawnT = demo ? 1.0f : 2.4f;
	powerDist = 220.0f + rndf() * 120.0f;
	coinsRun = 0; chain = 0; chainT = 0; multPulse = 0;
	invulT = 0; newBest = false;
	for (int i = 0; i < TRAIL_N; i++) { trailX[i] = laneX; trailY[i] = 0.0f; }
}

static Coin *coin_new(void)
{
	for (int i = 0; i < MAXCOIN; i++)
		if (!coins[i].on) return &coins[i];
	return NULL;
}

static void coin_at(float x, float y, float z)
{
	Coin *c = coin_new();
	if (!c) return;
	c->on = true; c->mag = false;
	c->x = x; c->y = y; c->z = z;
	c->ph = z * 0.35f;
}

/* Generates a row of obstacles guaranteeing at least one way out, then
 * fills the space up to the next row with coins (or a power-up),
 * along gapDist units: the track is free there by construction. */
static void spawn_row(float gapDist)
{
	int types[3] = { -1, -1, -1 };
	int freeLane = rand() % 3;
	int walls = 0;

	for (int l = 0; l < 3; l++) {
		if (l == freeLane && (rand() % 100) < 70) continue; /* clear way */
		Obst *o = NULL;
		for (int i = 0; i < MAXOBS; i++)
			if (!obs[i].on) { o = &obs[i]; break; }
		if (!o) break;
		o->on = true;
		o->lane = l;
		o->z = SPAWN_Z;
		o->scored = false;
		if (l == freeLane) {
			o->type = 0; /* only jumpable barriers on the escape path */
		} else if (walls < 2 && (rand() % 100) < D_WALLP[g_diff]) {
			o->type = 1;
			walls++;
		} else {
			o->type = 0;
		}
		types[l] = o->type;
	}

	/* power-up: every 220-340 m, in the middle of the free space */
	powerDist -= gapDist;
	if (powerDist <= 0.0f && gapDist > 9.0f) {
		for (int i = 0; i < MAXPOW; i++) {
			if (pows[i].on) continue;
			pows[i].on = true;
			pows[i].kind = rand() % PW_NUM;
			pows[i].x = LANEX[rand() % 3];
			pows[i].z = SPAWN_Z + gapDist * 0.5f;
			pows[i].ph = 0.0f;
			break;
		}
		powerDist = 220.0f + rndf() * 120.0f;
		return;
	}

	int r = rand() % 100;
	if (r < 30) {
		/* arc over a barrier: follows the trajectory of a well-timed
		 * jump (peak above the barrier) */
		int cand[3], nc = 0;
		for (int l = 0; l < 3; l++)
			if (types[l] == 0) cand[nc++] = l;
		if (nc > 0) {
			int l = cand[rand() % nc];
			for (int k = 0; k < 5; k++) {
				float t = 0.10f + 0.13f * (float)k;
				float y = 0.45f + 8.0f * t - 11.0f * t * t;
				coin_at(LANEX[l], y, SPAWN_Z + speed * (t - 0.364f));
			}
			return;
		}
	}
	if (r < 85) {
		/* row in the free corridor, sometimes with a lane change halfway */
		int n = (int)((gapDist - 5.0f) / 1.8f);
		if (n > 10) n = 10;
		if (n < 3) return;
		int l = rand() % 3;
		int l2 = l;
		if ((rand() % 3) == 0) l2 = (l == 1) ? ((rand() & 1) ? 0 : 2) : 1;
		for (int k = 0; k < n; k++) {
			int ll = (k < n / 2) ? l : l2;
			coin_at(LANEX[ll], 0.65f, SPAWN_Z + 2.5f + 1.8f * (float)k);
		}
	}
}

/* --------------------------------- text ---------------------------------- */
static C2D_TextBuf g_buf;
static C2D_Font g_font;

static void txt(const char *s, float x, float y, float sc, u32 col, int align)
{
	C2D_Text t;
	C2D_TextFontParse(&t, g_font, g_buf, s);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, align | C2D_WithColor, x, y, 0.5f, sc, sc, col);
}

/* text with shadow: readable over anything */
static void txs(const char *s, float x, float y, float sc, u32 col, int align)
{
	txt(s, x + 1.2f, y + 1.2f, sc, with_a(0xFF000000u, (float)(col >> 24) / 380.0f),
		align);
	txt(s, x, y, sc, col, align);
}

#define AL C2D_AlignLeft
#define AC C2D_AlignCenter
#define AR C2D_AlignRight

/* panel: translucent dark background, glowing line on top, accent bar */
static void panel(float x, float y, float w, float h, u32 accent)
{
	sq2(TX_WHITE, x, y, w, h, colPanel, mul_a(colPanel, 0.75f));
	rect(x, y, w, 1.0f, mul_a(colPanelEdge, 0.9f));
	rect(x, y + h - 1.0f, w, 1.0f, mul_a(colPanelEdge, 0.35f));
	rect(x, y, 2.0f, h, accent);
}

static void bar(float x, float y, float w, float h, float f, u32 col)
{
	rect(x, y, w, h, 0x60000000u);
	rect(x, y, w * clampf(f, 0.0f, 1.0f), h, col);
}

/* --------------------------------- demo AI ----------------------------------
 * Obstacles arrive in parallel "rows" and every row always has at least one
 * free lane or one with a jumpable barrier.  The CPU looks at the nearest row
 * and the one after it and positions itself where both are passable (with an eye
 * on the coins).  It never enters the collision box (z < 0.9). */
static int lane_box_blocked(int l)
{
	for (int i = 0; i < MAXOBS; i++) {
		Obst *o = &obs[i];
		if (o->on && o->lane == l && o->z > -0.9f && o->z < 0.9f) return 1;
	}
	return 0;
}

static int lane_type_at(int l, float zr)
{
	int t = -1;
	for (int i = 0; i < MAXOBS; i++) {
		Obst *o = &obs[i];
		if (!o->on || o->lane != l) continue;
		if (o->z <= zr - 1.5f || o->z >= zr + 1.5f) continue;
		if (o->type == 1) return 1;
		t = 0;
	}
	return t;
}

static float lane_nearest(int l, int *typ)
{
	float bz = 1e9f;
	int bt = -1;
	for (int i = 0; i < MAXOBS; i++) {
		Obst *o = &obs[i];
		if (!o->on || o->lane != l || o->z <= 0.6f) continue;
		if (o->z < bz) { bz = o->z; bt = o->type; }
	}
	if (typ) *typ = bt;
	return bz;
}

static float lane_score(int l, float z1, float z2)
{
	int t1 = lane_type_at(l, z1);
	float sc = (t1 < 0) ? 3.0f : (t1 == 0 ? 2.0f : 0.0f);

	if (z2 < 200.0f) {
		int t2 = lane_type_at(l, z2);
		sc += 0.6f * ((t2 < 0) ? 3.0f : (t2 == 0 ? 2.0f : 0.0f));
	}
	int d = (l > lane) ? (l - lane) : (lane - l);
	sc -= 0.4f * (float)d;
	if (l == lane) sc += 0.30f;
	/* coins in the lane, within the next row */
	for (int i = 0; i < MAXCOIN; i++) {
		Coin *c = &coins[i];
		if (c->on && c->z > 0.5f && c->z < z1 && fabsf(c->x - LANEX[l]) < 0.3f)
			sc += 0.08f;
	}
	for (int i = 0; i < MAXPOW; i++) {
		Power *pw = &pows[i];
		if (pw->on && pw->z > 0.5f && pw->z < z1 + 12.0f &&
		    fabsf(pw->x - LANEX[l]) < 0.3f)
			sc += 0.9f;
	}
	return sc;
}

static void do_jump(void)
{
	airborne = true;
	jumpV = 8.0f;
	audio_jump();
}

static void demo_control(void)
{
	float z1 = 1e9f, z2 = 1e9f;
	float bestS = -1e9f;
	int want = lane;

	for (int i = 0; i < MAXOBS; i++) {
		Obst *o = &obs[i];
		if (!o->on || o->z <= 0.6f) continue;
		if (o->z < z1) { z2 = z1; z1 = o->z; }
		else if (o->z > z1 + 3.0f && o->z < z2) z2 = o->z;
	}
	if (z1 > 200.0f) return;

	for (int l = 0; l < 3; l++) {
		if (lane_box_blocked(l)) continue;
		float sc = lane_score(l, z1, z2);
		if (sc > bestS) { bestS = sc; want = l; }
	}
	if (want != lane && !lane_box_blocked(want)) {
		if (lane < want) lane++; else lane--;
		audio_move();
	}

	/* The jump peaks at ~0.36 s and stays above the good height
	 * (jumpY > 0.85) between ~0.14 and ~0.58 s: useful window for the impact. */
	int typ;
	float bz = lane_nearest(lane, &typ);
	if (typ == 0 && !airborne) {
		float tt = bz / speed;
		if (tt >= 0.18f && tt <= 0.50f) do_jump();
	}
}

/* ------------------------------ backdrop ----------------------------------- */
#define NSTAR 48
#define NMOUNT 33
static float stars[NSTAR][3];
static float mountH[NMOUNT];
#define MOUNT_Z 105.0f
#define MOUNT_X0 -130.0f
#define MOUNT_DX 8.125f

static void far_init(void)
{
	for (int i = 0; i < NSTAR; i++) {
		stars[i][0] = (rndf() - 0.5f) * 180.0f;
		stars[i][1] = 8.0f + rndf() * 38.0f;
		stars[i][2] = 112.0f + rndf() * 10.0f;
	}
	/* two octaves of sines + a bit of randomness, valley in the center for the sun */
	for (int i = 0; i < NMOUNT; i++) {
		float x = MOUNT_X0 + MOUNT_DX * (float)i;
		float h = 5.0f + 5.0f * sinf((float)i * 0.83f + 1.3f) +
			2.5f * sinf((float)i * 2.1f) + 3.0f * rndf();
		float v = clampf((fabsf(x) - 12.0f) / 30.0f, 0.18f, 1.0f);
		mountH[i] = clampf(h, 1.5f, 14.0f) * v;
	}
}

/* the backdrop sits "at infinity": maximum stereo shift, no fog */
static void draw_sky(void)
{
	float hy = g_horY;

	/* sky: two gradients, then flat ground all the way down */
	sq2(TX_WHITE, -10, -10, 420, hy * 0.55f + 10, colSkyTop, colSkyMid);
	sq2(TX_WHITE, -10, hy * 0.55f - 0.5f, 420, hy * 0.45f + 1.0f, colSkyMid, colFog);
	sq2(TX_WHITE, -10, hy, 420, 250 - hy, colFog, colGroundNear);

	/* synthwave sun in the valley, halo and stars additive */
	V3 sunP = v3(0.0f, 13.0f, 130.0f);
	rx_additive(true);
	bb(TX_GLOW, sunP, 110.0f, 110.0f, with_a(colPink, 0.55f));
	for (int i = 0; i < NSTAR; i++) {
		V3 p = v3(stars[i][0], stars[i][1], stars[i][2]);
		float tw = 0.5f + 0.5f * sinf(etime * 2.0f + (float)i * 1.7f);
		bb(TX_STAR, p, 1.4f + 0.6f * tw, 1.4f + 0.6f * tw,
			with_a(colWhite, 0.25f + 0.45f * tw));
	}
	rx_additive(false);
	bb(TX_SUN, sunP, 46.0f, 46.0f, colWhite);

	/* mountains: gradient walls + neon ridges (wireframe) */
	for (int i = 0; i + 1 < NMOUNT; i++) {
		float x0 = MOUNT_X0 + MOUNT_DX * i, x1 = x0 + MOUNT_DX;
		float ax, ay, bx, by, cx, cy, dx, dy;
		if (!project(v3(x0, -1.0f, MOUNT_Z), &ax, &ay) ||
		    !project(v3(x1, -1.0f, MOUNT_Z), &bx, &by) ||
		    !project(v3(x1, mountH[i + 1], MOUNT_Z), &cx, &cy) ||
		    !project(v3(x0, mountH[i], MOUNT_Z), &dx, &dy))
			continue;
		float ms = g_psh;          /* same z plane: same disparity */
		const TexUV *uv = texgen_uv(TX_WHITE);
		RxV A = rvs(ax, ay, uv->u0, uv->v0, colMountBase, ms),
			B = rvs(bx, by, uv->u0, uv->v0, colMountBase, ms),
			C = rvs(cx, cy, uv->u0, uv->v0, colMountTop, ms),
			D = rvs(dx, dy, uv->u0, uv->v0, colMountTop, ms);
		rx_quad(&A, &B, &C, &D);
		sline_s(dx, dy, ms, ax, ay, ms, 1.4f, with_a(colPink, 0.30f),
			with_a(colPink, 0.0f));
		sline_s(dx, dy, ms, cx, cy, ms, 2.6f, with_a(colPink, 0.95f),
			with_a(colPink, 0.95f));
	}

	/* haze on the horizon */
	rx_additive(true);
	sq2(TX_WHITE, -10, hy - 16.0f, 420, 16.0f, with_a(colPink, 0.0f),
		with_a(colPink, 0.30f));
	sq2(TX_WHITE, -10, hy, 420, 12.0f, with_a(colPink, 0.30f), with_a(colPink, 0.0f));
	rx_additive(false);
}

/* --------------------------------- world ---------------------------------- */
/* road bed and tiled scrolling ground, neon edges,
 * lane dashes and pools of light from the street lamps */
static void draw_ground(void)
{
	const float cell = 4.0f;
	float off = fmodf(dist, cell);

	for (int k = 15; k >= 0; k--) {
		float z0 = k * cell - off - 4.0f, z1 = z0 + cell;
		if (z1 < -4.5f) continue;
		int sv = (k < 3) ? 3 : 2;           /* more cells near: less distortion */

		/* ground on the sides: the tile has the grid on its edges */
		for (int s = -1; s <= 1; s += 2)
			for (int t = 0; t < 3; t++) {
				float xa = s * (3.3f + 3.9f * t), xb = s * (3.3f + 3.9f * (t + 1));
				float xl = s < 0 ? xb : xa, xr = s < 0 ? xa : xb;
				wface(TX_GROUND, v3(xl, 0, z0), v3(xr, 0, z0), v3(xr, 0, z1),
					v3(xl, 0, z1), 1, sv, false, colGrid, 1.0f, false);
			}
		/* roadway: one tile per lane, two in depth */
		for (int l = 0; l < 3; l++) {
			float xl = LANEX[l] - 1.1f, xr = LANEX[l] + 1.1f;
			wface(TX_ROAD, v3(xl, 0, z0), v3(xr, 0, z0), v3(xr, 0, z1),
				v3(xl, 0, z1), 1, 2, true, colRoad, 1.0f, false);
		}
	}

	/* edges: solid strip + neon wire */
	for (int s = -1; s <= 1; s += 2) {
		float rx = s * 3.3f;
		wstrip(TX_WHITE, v3(rx, 0.01f, -4.0f), v3(rx, 0.01f, 56.0f), 0.08f, 8,
			false, shade(colRail, 0.9f));
	}

	/* lane dashes */
	float doff = fmodf(dist, 3.0f);
	for (int i = 0; i < 2; i++) {
		float lx = (i == 0) ? -1.1f : 1.1f;
		for (int k = 0; k < 19; k++) {
			float z0 = k * 3.0f - doff - 3.0f;
			if (z0 < -4.5f || z0 > 54.0f) continue;
			wstrip(TX_DASH, v3(lx, 0.015f, z0), v3(lx, 0.015f, z0 + 1.8f), 0.16f,
				2, false, colDash);
		}
	}

	/* additive glows: edges and pools of light from the street lamps */
	rx_additive(true);
	for (int s = -1; s <= 1; s += 2) {
		float rx = s * 3.3f;
		wstrip(TX_TRAIL, v3(rx, 0.02f, -4.0f), v3(rx, 0.02f, 56.0f), 0.45f, 8,
			true, with_a(colRail, 0.55f));
	}
	float poff = fmodf(dist, 8.0f);
	for (int k = 7; k >= 0; k--) {
		float z = k * 8.0f - poff;
		if (z < -4.0f || z > 54.0f) continue;
		int id = (int)floorf(dist / 8.0f) + k;
		for (int s = -1; s <= 1; s += 2) {
			u32 lc = ((id + (s > 0)) & 1) ? 0xFF60B0FFu : 0xFFFFD040u;
			float a = 0.28f * fogA(depth_of(v3(s * 2.6f, 0, z)));
			float cx = s * 2.4f;
			wface(TX_GLOW, v3(cx - 1.9f, 0.02f, z - 2.3f), v3(cx + 1.9f, 0.02f, z - 2.3f),
				v3(cx + 1.9f, 0.02f, z + 2.3f), v3(cx - 1.9f, 0.02f, z + 2.3f),
				1, 2, false, with_a(lc, a), 1.0f, false);
		}
	}
	rx_additive(false);
}

static void draw_buildings(void)
{
	const float SP = 7.0f;
	int seg0 = (int)floorf(dist / SP);
	float base = dist - (float)seg0 * SP;

	for (int k = 8; k >= 0; k--) {
		float z = k * SP - base + 3.0f;
		if (z < -2.0f) continue;

		for (int s = -1; s <= 1; s += 2) {
			unsigned int h = hsh((unsigned int)((seg0 + k) * 2 + (s > 0)));
			float hh = 3.0f + (float)(h % 1000) / 1000.0f * 8.5f;
			float bw = 1.1f + (float)((h >> 4) % 100) / 100.0f * 0.8f;
			float bd = 1.2f + (float)((h >> 11) % 100) / 100.0f * 1.2f;
			float bx = s * (4.9f + bw + (float)((h >> 7) % 100) / 100.0f * 1.4f);
			int pal = (h >> 17) & 3;
			bool facB = (h >> 19) & 1;

			BoxSty st = {
				facB ? TX_FACADE_B : TX_FACADE_A, facB ? TX_FACADE_B : TX_FACADE_A,
				TX_ROOF,
				(u8)clampf(roundf(2.0f * bw / 1.4f), 1, 8),
				(u8)clampf(roundf(hh / 1.4f), 1, 8),
				(u8)clampf(roundf(2.0f * bd / 1.4f), 1, 8),
				(u8)clampf(roundf(hh / 1.4f), 1, 8),
				1, 1, true, BLD_TINT[pal], 0.55f,
				with_a(BLD_NEON[pal], 0.85f), 2.4f,
			};
			wbox(v3(bx, hh * 0.5f, z), v3(bw, hh * 0.5f, bd), &st, NULL);

			/* neon sign on the facade (rarely blinks) */
			if (((h >> 21) % 3) == 0) {
				float fz = z - bd - 0.04f;
				float sy = hh - 1.1f;
				bool on = ((int)(etime * 7.0f + (float)(h & 63)) % 23) != 0;
				u32 nc = with_a(BLD_NEON[(pal + 1) & 3], on ? 1.0f : 0.35f);
				wface(((h >> 23) & 1) ? TX_SIGN_A : TX_SIGN_B,
					v3(bx - 0.8f, sy - 0.4f, fz), v3(bx + 0.8f, sy - 0.4f, fz),
					v3(bx + 0.8f, sy + 0.4f, fz), v3(bx - 0.8f, sy + 0.4f, fz),
					2, 1, false, nc, 1.0f, false);
			}
			/* antenna with a blinking red light */
			if (((h >> 25) & 1) && hh > 6.0f) {
				wline(v3(bx, hh, z), v3(bx, hh + 1.6f, z), 1.5f, 0xFF605060u);
				float bl = 0.5f + 0.5f * sinf(etime * 3.0f + (float)(h & 15));
				bb(TX_GLOW, v3(bx, hh + 1.65f, z), 0.9f, 0.9f,
					fogv(with_a(0xFF3030FFu, 0.4f + 0.6f * bl), depth_of(v3(bx, hh, z))));
			}
		}
	}
}

/* street lamps: pole, arm toward the road, glowing head */
static void draw_posts(void)
{
	static const BoxSty pole = {
		TX_ROOF, TX_ROOF, TX_ROOF, 1, 3, 1, 3, 1, 1, false,
		0xFF6A5048u, 0.6f, 0x00000000u, 0.0f };
	static const BoxSty head = {
		TX_WHITE, TX_WHITE, TX_WHITE, 1, 1, 1, 1, 1, 1, false,
		0xFFE0F0FFu, 1.0f, 0x00000000u, 0.0f };
	float poff = fmodf(dist, 8.0f);
	int id0 = (int)floorf(dist / 8.0f);

	for (int k = 7; k >= 0; k--) {
		float z = k * 8.0f - poff;
		if (z < -4.0f || z > 54.0f) continue;
		for (int s = -1; s <= 1; s += 2) {
			float px = s * 3.75f;
			wbox(v3(px, 1.35f, z), v3(0.07f, 1.35f, 0.07f), &pole, NULL);
			wbox(v3(px - s * 0.55f, 2.70f, z), v3(0.55f, 0.05f, 0.06f), &pole, NULL);
			u32 lc = ((id0 + k + (s > 0)) & 1) ? 0xFF60B0FFu : 0xFFFFD040u;
			float d = depth_of(v3(px, 2.6f, z));
			wbox(v3(px - s * 1.0f, 2.62f, z), v3(0.20f, 0.05f, 0.10f), &head, NULL);
			rx_additive(true);
			bb(TX_GLOW, v3(px - s * 1.0f, 2.55f, z), 1.3f, 1.0f,
				with_a(lc, 0.75f * fogA(d)));
			rx_additive(false);
		}
	}
}

/* ------------------------------ objects ----------------------------------- */
static void shadow_at(float x, float z, float w, float dpt, float a)
{
	wface(TX_SHADE, v3(x - w, 0.02f, z - dpt), v3(x + w, 0.02f, z - dpt),
		v3(x + w, 0.02f, z + dpt), v3(x - w, 0.02f, z + dpt), 1, 1, false,
		with_a(colShadow, a), 1.0f, false);
}

static void draw_obst(const Obst *o)
{
	float x = LANEX[o->lane];
	float pulse = 0.5f + 0.5f * sinf(etime * 3.0f - o->z * 0.35f);

	shadow_at(x, o->z + 0.15f, 1.25f, 0.55f, 0.55f);
	if (o->type == 0) {
		BoxSty st = { TX_HAZARD, TX_HAZARD, TX_ROOF, 1, 1, 1, 1, 1, 1, false,
			colBarr, 0.70f, with_a(0xFF80F0FFu, 0.95f), 2.2f };
		wbox(v3(x, 0.45f, o->z), v3(0.95f, 0.45f, 0.28f), &st, NULL);
		bb(TX_MARK, v3(x, 1.55f, o->z), 0.62f, 0.62f,
			fogv(with_a(colHint, 0.30f + 0.45f * pulse), depth_of(v3(x, 1, o->z))));
	} else {
		u32 wc = shade(colWall, 0.85f + 0.30f * pulse);
		BoxSty st = { TX_ENERGY, TX_HULL, TX_ROOF, 1, 2, 1, 2, 1, 1, true,
			wc, 0.80f, with_a(0xFFD0A0FFu, 0.95f), 2.4f };
		wbox(v3(x, 1.3f, o->z), v3(0.95f, 1.3f, 0.30f), &st, NULL);
		/* striped plinth */
		float fz = o->z - 0.31f;
		wface(TX_HAZARD, v3(x - 0.95f, 0.0f, fz), v3(x + 0.95f, 0.0f, fz),
			v3(x + 0.95f, 0.42f, fz), v3(x - 0.95f, 0.42f, fz), 2, 1, false,
			shade(colWall, 0.9f), 1.0f, false);
		bb(TX_MARK, v3(x, 3.0f, o->z), 0.5f, 0.5f,
			fogv(with_a(colWall, 0.25f + 0.35f * pulse), depth_of(v3(x, 2, o->z))));
	}
}

static void draw_coin(const Coin *c)
{
	float bob = c->mag ? 0.0f : 0.08f * sinf(etime * 4.0f + c->ph);
	V3 p = v3(c->x, c->y + bob, c->z);
	float d = depth_of(p);
	float sp = cosf(etime * 4.5f + c->ph);
	float w = 0.62f * fmaxf(fabsf(sp), 0.12f);

	if (c->y < 1.0f && !c->mag) shadow_at(c->x, c->z, 0.28f, 0.14f, 0.35f);
	bb(TX_GLOW, p, 1.1f, 1.1f, fogv(with_a(colGold, 0.30f), d));
	bb(TX_COIN, p, w, 0.62f, fogv(colWhite, d));
	/* flash when the coin is face-on */
	if (fabsf(sp) > 0.97f)
		bb(TX_STAR, v3(p.x - 0.12f, p.y + 0.14f, p.z - 0.05f), 0.45f, 0.45f,
			fogv(with_a(colWhite, 0.8f), d));
}

static void draw_power(const Power *pw)
{
	float bob = 0.12f * sinf(etime * 3.0f + pw->ph);
	V3 p = v3(pw->x, 1.05f + bob, pw->z);
	float d = depth_of(p);
	u32 c = PW_COL[pw->kind];

	shadow_at(pw->x, pw->z, 0.5f, 0.25f, 0.4f);
	rx_additive(true);
	bb(TX_BEAM, v3(pw->x, 1.8f, pw->z), 1.0f, 3.6f, with_a(c, 0.45f * fogA(d)));
	bb(TX_GLOW, p, 2.0f, 2.0f, with_a(c, 0.55f * fogA(d)));
	rx_additive(false);
	bbr(TX_RING, p, 1.25f, 1.25f, etime * 2.0f, fogv(with_a(c, 0.9f), d));
	bb(TX_GLOW, p, 0.95f, 0.95f, fogv(with_a(0xFF201010u, 0.7f), d));
	bb(PW_TEX[pw->kind], p, 0.80f, 0.80f, fogv(colWhite, d));
}

/* ----------------------------- protagonist ------------------------------- */
/* Hover-racer: hull, nose, engine, glass cockpit, two-tone fins,
 * gravity pods.  Roll in turns, pitch in jumps; parts in order
 * from far to near (the camera sits behind and above). */
static void draw_player(float px, float py, float lean)
{
	V3 piv = v3(px, py, 0.0f);
	float pitch = airborne ? -0.10f - jumpV * 0.012f : 0.0f;
	Xf xf = xf_make(piv, lean, pitch);
	u32 rim = with_a(0xFFFFF0A0u, 0.85f);

	float shS = 1.0f / (1.0f + py * 0.35f);
	shadow_at(px, 0.1f, 1.1f * shS, 1.2f * shS, 0.60f * shS);

	/* magnet: pulsing ring on the ground */
	if (pwT[PW_MAGNET] > 0.0f) {
		float r = 1.6f + 0.25f * sinf(etime * 8.0f);
		rx_additive(true);
		wface(TX_RING, v3(px - r, 0.03f, -r), v3(px + r, 0.03f, -r),
			v3(px + r, 0.03f, r), v3(px - r, 0.03f, r), 1, 1, false,
			with_a(PW_COL[PW_MAGNET], 0.8f), 1.0f, false);
		rx_additive(false);
	}

	BoxSty hull = { TX_HULL, TX_HULL, TX_HULL, 1, 1, 1, 1, 1, 1, false,
		colPlayer, 0.85f, rim, 1.6f };
	BoxSty dark = { TX_ROOF, TX_ROOF, TX_ROOF, 1, 1, 1, 1, 1, 1, false,
		0xFF503830u, 1.0f, 0, 0.0f };
	BoxSty wing = hull;
	wing.col = colPlayer2;
	wing.rim = with_a(colPlayer2, 0.9f);
	BoxSty glass = { TX_CANOPY, TX_CANOPY, TX_CANOPY, 1, 1, 1, 1, 1, 1, false,
		colWhite, 1.0f, with_a(colWhite, 0.6f), 1.2f };

	wbox(v3(0.0f, 0.30f, 1.05f), v3(0.30f, 0.12f, 0.35f), &hull, &xf);   /* nose */
	for (int s = -1; s <= 1; s += 2)
		wbox(v3(s * 0.40f, 0.14f, 0.0f), v3(0.14f, 0.10f, 0.45f), &dark, &xf);
	hull.tu = 2; hull.tv = 2; hull.rep = true;
	wbox(v3(0.0f, 0.34f, 0.05f), v3(0.50f, 0.18f, 0.80f), &hull, &xf);   /* hull */
	for (int s = -1; s <= 1; s += 2) {
		wbox(v3(s * 0.72f, 0.34f, -0.55f), v3(0.22f, 0.05f, 0.35f), &wing, &xf);
		V3 tip = xf_apply(&xf, v3(s * 0.95f, 0.36f, -0.50f));
		bb(TX_WHITE, tip, 0.07f, 0.07f, colAccent);
	}
	wbox(v3(0.0f, 0.60f, -0.05f), v3(0.26f, 0.16f, 0.42f), &glass, &xf); /* glass */
	wbox(v3(0.0f, 0.40f, -0.95f), v3(0.34f, 0.22f, 0.28f), &dark, &xf);  /* engine */
	/* nozzles */
	for (int s = -1; s <= 1; s += 2) {
		V3 q = xf_apply(&xf, v3(s * 0.18f, 0.40f, -1.24f));
		bb(TX_GLOW, q, 0.30f, 0.30f, colWhite);
	}

	/* additive glows: pods, flames, speed halo, shield */
	rx_additive(true);
	for (int s = -1; s <= 1; s += 2) {
		V3 q = xf_apply(&xf, v3(s * 0.40f, 0.02f, 0.0f));
		bb(TX_GLOW, q, 0.9f, 0.5f,
			with_a(colAccent, 0.35f + 0.15f * sinf(etime * 7.0f + (float)s)));
		float fl = 0.50f + speed * 0.02f + 0.10f * sinf(etime * 23.0f + s * 2.0f);
		q = xf_apply(&xf, v3(s * 0.18f, 0.40f, -1.30f - fl * 0.25f));
		bb(TX_FLAME, q, 0.26f, fl, with_a(0xFF3090FFu, 0.95f));
		q = xf_apply(&xf, v3(s * 0.18f, 0.40f, -1.25f));
		bb(TX_GLOW, q, 0.55f, 0.55f, with_a(0xFF40A0FFu, 0.55f));
	}
	if (speed > 17.0f) {
		V3 q = xf_apply(&xf, v3(0.0f, 0.42f, -0.10f));
		bb(TX_RING, q, 2.5f, 1.8f,
			with_a(colAccent, 0.07f + 0.05f * sinf(etime * 9.0f)));
	}
	if (pwT[PW_SHIELD] > 0.0f) {
		V3 q = v3(px, py + 0.5f, 0.1f);
		float pu = 0.5f + 0.5f * sinf(etime * 5.0f);
		bb(TX_GLOW, q, 3.0f, 2.4f, with_a(PW_COL[PW_SHIELD], 0.22f + 0.10f * pu));
		bbr(TX_RING, q, 2.8f, 2.2f, etime, with_a(PW_COL[PW_SHIELD], 0.55f + 0.2f * pu));
	}
	rx_additive(false);
}

/* engine trail: ribbon in screen-space along the past positions */
static void draw_trail(void)
{
	float sx[TRAIL_N], sy[TRAIL_N], sw[TRAIL_N], ss[TRAIL_N];
	float step = speed / 60.0f;
	int n = 0;

	for (int k = 0; k < TRAIL_N; k++) {
		V3 p = v3(trailX[k], trailY[k] + 0.40f, -1.30f - (float)k * step);
		if (!project(p, &sx[n], &sy[n])) break;
		ss[n] = g_psh;
		sw[n] = FOCAL * 0.28f / depth_of(p) * (1.0f - (float)k / TRAIL_N);
		n++;
	}
	if (n < 2) return;
	const TexUV *uv = texgen_uv(TX_TRAIL);
	for (int k = 0; k + 1 < n; k++) {
		float dx = sx[k + 1] - sx[k], dy = sy[k + 1] - sy[k];
		float l = sqrtf(dx * dx + dy * dy);
		if (l < 0.01f) { dx = 0.0f; dy = 1.0f; l = 1.0f; }
		float nx = -dy / l, ny = dx / l;
		float a0 = 0.55f * (1.0f - (float)k / (n - 1));
		float a1 = 0.55f * (1.0f - (float)(k + 1) / (n - 1));
		u32 c0 = with_a(mixc(colAccent, colPink, (float)k / n), a0);
		u32 c1 = with_a(mixc(colAccent, colPink, (float)(k + 1) / n), a1);
		RxV A = rvs(sx[k] + nx * sw[k], sy[k] + ny * sw[k], uv->u0, uv->v0, c0,
				ss[k]),
			B = rvs(sx[k + 1] + nx * sw[k + 1], sy[k + 1] + ny * sw[k + 1], uv->u1,
				uv->v0, c1, ss[k + 1]),
			C = rvs(sx[k + 1] - nx * sw[k + 1], sy[k + 1] - ny * sw[k + 1], uv->u1,
				uv->v1, c1, ss[k + 1]),
			D = rvs(sx[k] - nx * sw[k], sy[k] - ny * sw[k], uv->u0, uv->v1, c0,
				ss[k]);
		rx_quad(&A, &B, &C, &D);
	}
}

static void draw_particles(void)
{
	for (int i = 0; i < MAXPART; i++) {
		Part *p = &parts[i];
		if (!p->on) continue;
		float a = p->life / p->maxlife;
		V3 q = v3(p->x, p->y, p->z);

		switch (p->kind) {
		case PK_STREAK:
			wline(q, v3(p->x, p->y, p->z + 3.0f), 2.2f, with_a(p->color, a * 0.8f));
			break;
		case PK_STAR:
			bbr(TX_STAR, q, p->size, p->size, p->rot + etime * 4.0f,
				with_a(p->color, a));
			break;
		case PK_DEBRIS:
			bbr(TX_WHITE, q, p->size, p->size, p->rot + etime * 9.0f,
				with_a(p->color, a));
			break;
		default:
			bb(TX_GLOW, q, p->size + 0.3f * a, p->size + 0.3f * a,
				with_a(p->color, 0.9f * a));
			break;
		}
	}
}

/* crash ring that expands in the world */
static void draw_crash_ring(void)
{
	if (crashT <= 0.0f || crashT > 0.9f) return;
	float t = crashT / 0.9f;
	float a = (1.0f - t) * 0.7f;
	float sz = 1.5f + t * 9.0f;
	V3 p = v3(laneX, 0.6f + jumpY, 0.0f);
	bb(TX_RING, p, sz, sz * 0.55f, with_a(colWall, a));
	bb(TX_RING, p, sz * 0.55f, sz * 0.3f, with_a(colWhite, a * 0.7f));
}

/* world objects in painter's order: far -> near.  Everything
 * in front of the player, then the player, then what has passed him. */
typedef struct { float z; short kind, idx; } DrawItem;

static void draw_items(bool front)
{
	DrawItem it[MAXOBS + MAXCOIN + MAXPOW];
	int n = 0;

	for (int i = 0; i < MAXOBS; i++)
		if (obs[i].on && ((obs[i].z > 0.0f) == front))
			it[n++] = (DrawItem){ obs[i].z, 0, (short)i };
	for (int i = 0; i < MAXCOIN; i++)
		if (coins[i].on && ((coins[i].z > 0.0f) == front))
			it[n++] = (DrawItem){ coins[i].z, 1, (short)i };
	for (int i = 0; i < MAXPOW; i++)
		if (pows[i].on && ((pows[i].z > 0.0f) == front))
			it[n++] = (DrawItem){ pows[i].z, 2, (short)i };

	/* descending insertion sort: few elements and almost already in order */
	for (int i = 1; i < n; i++) {
		DrawItem v = it[i];
		int j = i - 1;
		while (j >= 0 && it[j].z < v.z) { it[j + 1] = it[j]; j--; }
		it[j + 1] = v;
	}
	for (int i = 0; i < n; i++) {
		if (it[i].kind == 0) draw_obst(&obs[it[i].idx]);
		else if (it[i].kind == 1) draw_coin(&coins[it[i].idx]);
		else draw_power(&pows[it[i].idx]);
	}
}

static void draw_world(bool withItems)
{
	draw_sky();
	draw_ground();
	draw_buildings();
	draw_posts();
	if (!withItems) return;

	draw_items(true);
	bool blink = invulT > 0.0f && ((int)(etime * 16.0f) & 1);
	if (state != ST_OVER && !blink) {
		draw_player(laneX, jumpY, (LANEX[lane] - laneX) * 0.35f);
		rx_additive(true);
		draw_trail();
		rx_additive(false);
	}
	draw_items(false);

	rx_additive(true);
	draw_particles();
	draw_crash_ring();
	rx_additive(false);
}

/* --------------------------------- HUD ------------------------------------ */
static void draw_logo(float cx, float y, float sc)
{
	float gl = 0.55f + 0.25f * sinf(etime * 1.7f);
	/* offset pink halo + main cyan text */
	txt("NEON RUSH", cx + 2.0f, y + 2.0f, sc, with_a(colPink, gl), AC);
	txt("NEON RUSH", cx - 1.0f, y - 1.0f, sc, with_a(colPink, gl * 0.5f), AC);
	txt("NEON RUSH", cx, y, sc, colAccent, AC);
}

static void draw_hud(void)
{
	char line[64];

	/* score */
	panel(6, 6, 124, 42, colAccent);
	txt("SCORE", 13, 8, 0.40f, colDim, AL);
	snprintf(line, sizeof(line), "%d", (int)score);
	txs(line, 13, 20, 0.78f, colWhite, AL);

	/* coins + multiplier */
	panel(136, 6, 92, 24, colGold);
	sq(TX_COIN, 142, 9, 18, 18, colWhite);
	snprintf(line, sizeof(line), "%d", coinsRun);
	txs(line, 164, 9, 0.58f, colGold, AL);
	float m = mult_total();
	if (m > 1.0f) {
		float pu = 1.0f + 0.35f * multPulse;
		u32 mc = pwT[PW_X2] > 0.0f ? colGold : colHint;
		snprintf(line, sizeof(line), "x%d", (int)m);
		txs(line, 222, 8 - 4.0f * multPulse, 0.62f * pu, mc, AR);
	}
	/* progress toward the next multiplier */
	if (mult_base() < 5)
		bar(138, 30, 88, 2, (float)(coinsRun % 20) / 20.0f, with_a(colHint, 0.9f));

	/* speed */
	panel(318, 6, 76, 30, colPink);
	snprintf(line, sizeof(line), "%d", (int)(speed * 3.6f));
	txs(line, 386, 7, 0.58f, colWhite, AR);
	txt("km/h", 324, 12, 0.38f, colDim, AL);
	bar(324, 29, 64, 3, (speed - D_SPD0[g_diff]) / (D_SPDMAX[g_diff] - D_SPD0[g_diff]),
		speed > 20.0f ? 0xFF3090FFu : colAccent);

	/* active power-ups */
	float py = 42;
	for (int k = 0; k < PW_NUM; k++) {
		if (pwT[k] <= 0.0f) continue;
		bool warn = pwT[k] < 2.0f && ((int)(etime * 8.0f) & 1);
		panel(318, py, 76, 22, PW_COL[k]);
		sq(PW_TEX[k], 322, py + 2, 18, 18, warn ? with_a(colWhite, 0.4f) : colWhite);
		bar(344, py + 9, 46, 4, pwT[k] / PW_MAX[k], PW_COL[k]);
		py += 26;
	}

	if (demo) {
		panel(6, 54, 60, 18, colHint);
		txt("DEMO", 14, 55, 0.48f, colHint, AL);
	}

	/* texts that rise and fade */
	for (int i = 0; i < MAXPOP; i++) {
		if (pops[i].t <= 0.0f) continue;
		float a = clampf(pops[i].t / 0.5f, 0.0f, 1.0f);
		float y = 62.0f + (float)i * 17.0f - (1.3f - pops[i].t) * 12.0f;
		txs(pops[i].txt, 200, y, 0.55f, with_a(pops[i].col, a), AC);
	}

	/* countdown */
	if (introT > 0.0f) {
		int n = (int)ceilf(introT / 0.6f);
		float f = fmodf(introT, 0.6f) / 0.6f;
		snprintf(line, sizeof(line), "%d", n);
		txs(line, 200, 76 - 10.0f * (1.0f - f), 1.4f + 0.5f * f,
			with_a(colWhite, 0.4f + 0.6f * f), AC);
		txs("GET READY", 200, 60, 0.5f, colAccent, AC);
	} else if (goT > 0.0f) {
		txs("GO!", 200, 70, 1.5f + (0.6f - goT), with_a(colHint, goT / 0.6f), AC);
	}
}

static void draw_menu(void)
{
	static const char *const ITEMS[5] = { "PLAY", "DEMO", "DIFFICULTY", "MUSIC",
		"EXIT" };
	char line[64];

	/* light veil: the city keeps scrolling behind */
	sq2(TX_WHITE, 0, 0, 400, 240, with_a(colBg, 0.55f), with_a(colBg, 0.30f));
	draw_logo(200, 14, 1.30f);
	txt("an in-screen 3D runner", 200, 56, 0.45f, colDim, AC);

	for (int i = 0; i < 5; i++) {
		float y = 84.0f + 25.0f * i;
		bool sel = i == menuSel;
		if (sel) {
			float pu = 0.75f + 0.25f * sinf(etime * 6.0f);
			panel(96, y - 2, 208, 22, with_a(colAccent, pu));
			txt(">", 104, y, 0.55f, colAccent, AL);
		}
		u32 c = sel ? colWhite : colDim;
		if (i == 2) snprintf(line, sizeof(line), "%s  %s%s%s", ITEMS[i],
			sel ? "< " : "", D_NAME[g_diff], sel ? " >" : "");
		else if (i == 3) snprintf(line, sizeof(line), "%s  %s%s%s", ITEMS[i],
			sel ? "< " : "", audio_music_on() ? "ON" : "OFF", sel ? " >" : "");
		else snprintf(line, sizeof(line), "%s", ITEMS[i]);
		txs(line, 200, y, 0.58f, c, AC);
	}
	txt("D-Pad: choose    A: confirm", 200, 214, 0.42f, colDim, AC);
}

static void draw_pause(void)
{
	static const char *const ITEMS[3] = { "RESUME", "RESTART", "QUIT TO TITLE" };

	rect(0, 0, 400, 240, with_a(colBg, 0.6f));
	panel(110, 58, 180, 118, colAccent);
	txs("PAUSED", 200, 64, 0.8f, colAccent, AC);
	for (int i = 0; i < 3; i++) {
		float y = 100.0f + 24.0f * i;
		if (i == pauseSel) {
			rect(118, y - 1, 164, 20, with_a(colAccent, 0.22f));
			rect(118, y - 1, 2, 20, colAccent);
		}
		txt(ITEMS[i], 200, y, 0.55f, i == pauseSel ? colWhite : colDim, AC);
	}
}

static void draw_results(void)
{
	char line[64];
	float t = clampf((overT - 0.7f) / 0.35f, 0.0f, 1.0f);   /* entrance */
	if (t <= 0.0f) {
		txs("CRASH!", 200, 90, 1.1f, with_a(colWall, clampf(overT * 3.0f, 0, 1)), AC);
		return;
	}
	float ease = 1.0f - (1.0f - t) * (1.0f - t);
	float y0 = 40.0f + (1.0f - ease) * 60.0f;
	u32 a = with_a(colWhite, ease);

	rect(0, 0, 400, 240, with_a(colBg, 0.45f * ease));
	panel(80, y0, 240, 164, with_a(colWall, ease));
	txs(demo ? "CPU CRASHED" : "RUN OVER", 200, y0 + 6, 0.75f, with_a(colWall, ease), AC);

	if (demo) {
		txt("back to title...", 200, y0 + 70, 0.5f, with_a(colDim, ease), AC);
		return;
	}
	static const char *const LBL[3] = { "DISTANCE", "COINS", "MULTIPLIER" };
	int val[3] = { (int)dist, coinsRun, mult_base() };
	for (int i = 0; i < 3; i++) {
		float y = y0 + 38 + 18.0f * i;
		txt(LBL[i], 96, y, 0.48f, with_a(colDim, ease), AL);
		snprintf(line, sizeof(line), i == 0 ? "%d m" : (i == 2 ? "x%d" : "%d"), val[i]);
		txt(line, 304, y, 0.50f, a, AR);
	}
	rect(96, y0 + 94, 208, 1, with_a(colPanelEdge, 0.5f * ease));
	txt("SCORE", 96, y0 + 100, 0.55f, with_a(colAccent, ease), AL);
	snprintf(line, sizeof(line), "%d", (int)score);
	txs(line, 304, y0 + 97, 0.80f, a, AR);
	if (newBest && ((int)(etime * 4.0f) & 1))
		txs("NEW BEST!", 200, y0 + 124, 0.55f, with_a(colGold, ease), AC);
	else {
		snprintf(line, sizeof(line), "best %d", best);
		txt(line, 200, y0 + 124, 0.45f, with_a(colDim, ease), AC);
	}
	txt(overSel == 0 ? "> A: RETRY <     B: TITLE" : "A: RETRY     > B: TITLE <",
		200, y0 + 144, 0.45f, with_a(colWhite, ease), AC);
}

/* ------------------------- bottom screen ------------------------------ */
#define PAUSE_BX 238
#define PAUSE_BY 196
#define PAUSE_BW 74
#define PAUSE_BH 36

static void bot_bg(void)
{
	sq2(TX_WHITE, 0, 0, 320, 240, 0xFF200A0Cu, 0xFF100406u);
	for (int x = 0; x <= 320; x += 20) rect((float)x, 0, 1, 240, 0x18FF60A0u);
	for (int y = 0; y <= 240; y += 20) rect(0, (float)y, 320, 1, 0x18FF60A0u);
}

static void card(float x, float y, float w, float h, const char *lbl,
	const char *val, u32 accent, u32 vc)
{
	panel(x, y, w, h, accent);
	txt(lbl, x + 8, y + 3, 0.40f, colDim, AL);
	txs(val, x + w - 8, y + h - 24, 0.72f, vc, AR);
}

static void draw_bottom(float slider)
{
	char line[80], val[32];

	bot_bg();
	if (state == ST_TITLE) {
		txs("NEON RUSH 3DS", 160, 8, 0.62f, colAccent, AC);
		snprintf(val, sizeof(val), "%d", best);
		card(8, 34, 148, 44, "BEST SCORE", val, colAccent, colWhite);
		snprintf(val, sizeof(val), "%d", bank);
		card(164, 34, 148, 44, "COINS", val, colGold, colGold);
		sq(TX_COIN, 170, 52, 20, 20, colWhite);
		snprintf(line, sizeof(line), "best distance %d m   %s  x%.1f pts",
			bestDist, D_NAME[g_diff], D_PTS[g_diff]);
		txt(line, 160, 82, 0.40f, colDim, AC);

		panel(8, 100, 304, 92, colPink);
		static const char *const K[5][2] = {
			{ "L / R", "change lane" }, { "A  B  UP", "jump" },
			{ "DOWN", "dive (in the air)" }, { "START", "pause" },
			{ "SELECT", "music on/off" } };
		for (int i = 0; i < 5; i++) {
			float y = 104.0f + 17.0f * i;
			rect(16, y + 1, 62, 14, 0x40FFFFFFu);
			txt(K[i][0], 47, y, 0.40f, colWhite, AC);
			txt(K[i][1], 88, y, 0.44f, colWhite, AL);
		}
		sq(TX_COIN, 222, 106, 14, 14, colWhite);
		txt("+10", 240, 105, 0.42f, colGold, AL);
		for (int k = 0; k < PW_NUM; k++) {
			sq(PW_TEX[k], 222, 124 + 20.0f * k, 16, 16, colWhite);
			txt(PW_NAME[k], 242, 124 + 20.0f * k, 0.40f, PW_COL[k], AL);
		}
	} else {
		txs("NEON RUSH 3DS", 8, 6, 0.52f, colAccent, AL);
		snprintf(val, sizeof(val), "%d", (int)score);
		card(8, 28, 148, 44, "SCORE", val, colAccent, colWhite);
		snprintf(val, sizeof(val), "%d", coinsRun);
		card(164, 28, 148, 44, "COINS", val, colGold, colGold);
		sq(TX_COIN, 170, 46, 20, 20, colWhite);
		snprintf(val, sizeof(val), "x%d", (int)mult_total());
		card(8, 78, 96, 44, "MULTI", val, colHint, colHint);
		snprintf(val, sizeof(val), "%d m", (int)dist);
		card(110, 78, 202, 44, "DISTANCE", val, colPink, colWhite);

		/* power-up */
		panel(8, 128, 304, 62, PW_COL[PW_SHIELD]);
		txt("POWER-UPS", 16, 130, 0.40f, colDim, AL);
		for (int k = 0; k < PW_NUM; k++) {
			float x = 16.0f + 98.0f * k;
			bool on = pwT[k] > 0.0f;
			sq(PW_TEX[k], x, 146, 22, 22, on ? colWhite : with_a(colWhite, 0.25f));
			txt(PW_NAME[k], x + 26, 146, 0.36f, on ? PW_COL[k] : colDim, AL);
			if (on) bar(x + 26, 162, 60, 4, pwT[k] / PW_MAX[k], PW_COL[k]);
		}

		if (demo) {
			txt("DEMO - any button: title", 8, 204, 0.45f, colHint, AL);
		} else if (state == ST_OVER) {
			txt("A: retry   B: title", 8, 200, 0.45f, colHint, AL);
		} else {
			/* PAUSE button on the touch screen */
			bool p = state == ST_PAUSE;
			panel(PAUSE_BX, PAUSE_BY, PAUSE_BW, PAUSE_BH, p ? colHint : colAccent);
			txs(p ? "RESUME" : "PAUSE", PAUSE_BX + PAUSE_BW / 2, PAUSE_BY + 9,
				0.5f, colWhite, AC);
			snprintf(line, sizeof(line), "%s   best %d", D_NAME[g_diff], best);
			txt(line, 8, 200, 0.42f, colDim, AL);
		}
	}

	snprintf(line, sizeof(line), "3D %d%%  %s  audio %s  drop %d  gpu %d%%",
		(int)(slider * 100.0f), audio_music_on() ? "music ON" : "music OFF",
		audio_ok() ? "OK" : "n/a", (int)ndspGetDroppedFrames(), g_gpuUse);
	txt(line, 8, 222, 0.38f,
		audio_ok() ? with_a(colDim, 0.8f) : 0xFF8080FFu, AL);
}

/* ------------------------------ logic ------------------------------------ */
static void start_run(bool asDemo)
{
	demo = asDemo;
	state = ST_PLAY;
	game_reset();
	audio_start();
	audio_music(MUS_RUN);
}

static void to_title(void)
{
	state = ST_TITLE;
	demo = false;
	game_reset();
	introT = 0.0f;
	audio_music(MUS_TITLE);
}

static void crash(void)
{
	state = ST_OVER;
	overT = 0.0f;
	overSel = 0;
	flash = 1.0f; flashR = 1.0f;
	shake = 1.0f;
	crashT = 0.0001f;
	burst(laneX, 0.5f + jumpY, 0.0f, colPlayer, 30, PK_GLOW);
	burst(laneX, 0.5f + jumpY, 0.0f, colPlayer2, 18, PK_DEBRIS);
	burst(laneX, 0.5f, 0.0f, colWall, 16, PK_GLOW);
	audio_crash();
	audio_music(MUS_OVER);
	if (!demo) {
		bank += coinsRun;
		if ((int)score > best) { best = (int)score; newBest = true; }
		if ((int)dist > bestDist) bestDist = (int)dist;
		save_store();
	}
}

static void collect_coin(Coin *c)
{
	c->on = false;
	coinsRun++;
	chain = (chainT > 0.0f) ? chain + 1 : 0;
	chainT = 0.45f;
	int m0 = mult_base();
	add_points(10.0f);
	audio_coin(chain);
	burst(c->x, c->y - 0.3f, c->z, colGold, 3, PK_STAR);
	if (!demo && coinsRun % 20 == 0 && mult_base() > m0) {
		multPulse = 1.0f;
		popup_n(colHint, "MULTIPLIER x", mult_base(), "!");
		audio_bonus();
	}
}

static void collect_power(Power *pw)
{
	pw->on = false;
	pwT[pw->kind] = PW_MAX[pw->kind];
	add_points(50.0f);
	burst(pw->x, 0.8f, pw->z, PW_COL[pw->kind], 16, PK_STAR);
	flash = 0.35f; flashR = 0.0f;
	audio_power();
	if (!demo) {
		char b[28];
		snprintf(b, sizeof(b), "%s!", PW_NAME[pw->kind]);
		popup_s(PW_COL[pw->kind], b);
	}
}

static void update_play(float dt, u32 kDown)
{
	if (introT > 0.0f) {
		float prev = introT;
		introT -= dt;
		if (ceilf(prev / 0.6f) != ceilf(introT / 0.6f) && introT > 0.0f)
			audio_go(false);
		if (introT <= 0.0f) { introT = 0.0f; goT = 0.6f; audio_go(true); }
	}
	if (goT > 0.0f) goT -= dt;

	if (demo) {
		demo_control();
	} else {
		if ((kDown & KEY_LEFT) && lane > 0) { lane--; audio_move(); }
		if ((kDown & KEY_RIGHT) && lane < 2) { lane++; audio_move(); }
		if ((kDown & (KEY_A | KEY_B | KEY_UP)) && !airborne) do_jump();
		/* dive: come back down immediately to regain control */
		if ((kDown & KEY_DOWN) && airborne && jumpV > -12.0f) jumpV = -12.0f;
	}

	if (airborne) {
		jumpY += jumpV * dt;
		jumpV -= 22.0f * dt;
		if (jumpY <= 0.0f) {
			jumpY = 0.0f; airborne = false;
			burst(laneX, 0.1f, 0.0f, RGBA(0x90, 0x80, 0xC0, 0xFF), 6, PK_GLOW);
		}
	}
	laneX += (LANEX[lane] - laneX) * 0.25f;

	/* trail: record of past positions */
	for (int i = TRAIL_N - 1; i > 0; i--) { trailX[i] = trailX[i - 1]; trailY[i] = trailY[i - 1]; }
	trailX[0] = laneX; trailY[0] = jumpY;

	float dz = speed * dt;
	if (introT <= 0.0f) {
		speed += 0.25f * dt;
		if (speed > D_SPDMAX[g_diff]) speed = D_SPDMAX[g_diff];
	}
	dist += dz;
	add_points(dz);

	spawnT -= dt;
	if (spawnT <= 0.0f) {
		float gap = (1.4f - speed * 0.03f) * D_GAPMUL[g_diff];
		if (gap < 0.40f) gap = 0.40f;
		spawnT = gap * (0.8f + rndf() * 0.4f);
		spawn_row(speed * spawnT);
	}

	/* obstacles */
	for (int i = 0; i < MAXOBS && state == ST_PLAY; i++) {
		Obst *o = &obs[i];
		if (!o->on) continue;
		float prev = o->z;
		o->z -= dz;
		if (o->z < -4.0f) { o->on = false; continue; }
		if (!o->scored && prev > 0.0f && o->z <= 0.0f) {
			o->scored = true;
			if (o->lane != lane) {
				add_points(2.0f);
			} else if (o->type == 0 && jumpY > 0.85f) {
				int pts = (int)(25.0f * D_PTS[g_diff] * mult_total());
				add_points(25.0f);
				if (!demo) popup_n(colHint, "CLEAN JUMP +", pts, "");
				audio_bonus();
			}
		}
		if (o->z > -0.9f && o->z < 0.9f && o->lane == lane && invulT <= 0.0f) {
			bool hit = (o->type == 1) || (jumpY < 0.85f);
			if (!hit) continue;
			if (pwT[PW_SHIELD] > 0.0f) {
				/* the shield is used up and the obstacle shatters */
				pwT[PW_SHIELD] = 0.0f;
				invulT = 1.0f;
				o->on = false;
				burst(LANEX[o->lane], 0.8f, o->z, o->type ? colWall : colBarr, 20,
					PK_DEBRIS);
				burst(laneX, 0.8f, 0.0f, PW_COL[PW_SHIELD], 16, PK_GLOW);
				flash = 0.6f; flashR = 0.0f; shake = 0.5f;
				audio_shield();
				if (!demo) popup_s(PW_COL[PW_SHIELD], "SHIELD BROKEN");
			} else {
				crash();
			}
		}
	}
	if (state != ST_PLAY) return;

	/* coins */
	bool mag = pwT[PW_MAGNET] > 0.0f;
	V3 pc = v3(laneX, jumpY + 0.45f, 0.0f);
	for (int i = 0; i < MAXCOIN; i++) {
		Coin *c = &coins[i];
		if (!c->on) continue;
		if (mag && c->z < 16.0f && c->z > -1.0f) c->mag = true;
		if (c->mag) {
			float k = clampf(10.0f * dt, 0.0f, 1.0f);
			c->x += (pc.x - c->x) * k;
			c->y += (pc.y - c->y) * k;
			c->z += (pc.z - c->z) * k - dz * 0.35f;
			V3 dv = v3(c->x - pc.x, c->y - pc.y, c->z - pc.z);
			if (vdot(dv, dv) < 0.8f * 0.8f) { collect_coin(c); continue; }
		} else {
			float prev = c->z;
			c->z -= dz;
			if (prev >= -0.6f && c->z <= 0.6f && fabsf(c->x - laneX) < 0.9f &&
			    fabsf(c->y - pc.y) < 0.9f) {
				collect_coin(c);
				continue;
			}
		}
		if (c->z < -4.0f) c->on = false;
	}

	/* power-up */
	for (int i = 0; i < MAXPOW; i++) {
		Power *pw = &pows[i];
		if (!pw->on) continue;
		float prev = pw->z;
		pw->z -= dz;
		pw->ph += dt;
		if (prev >= -0.8f && pw->z <= 0.8f && fabsf(pw->x - laneX) < 1.0f &&
		    jumpY < 1.6f) {
			collect_power(pw);
			continue;
		}
		if (pw->z < -4.0f) pw->on = false;
	}

	for (int k = 0; k < PW_NUM; k++)
		if (pwT[k] > 0.0f) {
			pwT[k] -= dt;
			if (pwT[k] < 0.0f) pwT[k] = 0.0f;
		}
	if (invulT > 0.0f) invulT -= dt;
	if (chainT > 0.0f) chainT -= dt;

	/* speed streaks at the edges */
	if (speed > 16.0f && (rand() % 100) < (int)(speed - 14.0f)) {
		float sx = ((rand() % 100) < 50 ? -1.0f : 1.0f) * (4.0f + rndf() * 4.0f);
		burst(sx, 0.5f + rndf() * 5.0f, 28.0f, RGBA(0x80, 0xC0, 0xFF, 0xFF), 1,
			PK_STREAK);
	}
}

static void update_fx(float dt)
{
	for (int i = 0; i < MAXPART; i++) {
		Part *p = &parts[i];
		if (!p->on) continue;
		p->life -= dt;
		if (p->life <= 0) { p->on = false; continue; }
		if (p->kind != PK_STREAK) {
			p->vy -= (p->kind == PK_STAR ? 4.0f : 14.0f) * dt;
			p->x += p->vx * dt;
			p->y += p->vy * dt;
			if (p->y < 0.05f) { p->y = 0.05f; p->vy *= -0.4f; }
		}
		p->z += p->vz * dt;
		if (p->z < -4.0f) p->on = false;
	}
	for (int i = 0; i < MAXPOP; i++)
		if (pops[i].t > 0.0f) pops[i].t -= dt;
	if (flash > 0.0f) { flash -= dt * 1.5f; if (flash < 0.0f) flash = 0.0f; }
	if (shake > 0.0f) { shake -= dt * 2.0f; if (shake < 0.0f) shake = 0.0f; }
	if (crashT > 0.0f) crashT += dt;
	if (multPulse > 0.0f) multPulse -= dt * 2.0f;
	etime += dt;
}

/* ---------------------------------- main ---------------------------------- */
int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	osSetSpeedupEnable(true);   /* New 3DS: 804 MHz + cache L2 */
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(MAX_OBJECTS);
	C2D_Prepare();
	rx_init(MAX_OBJECTS);
	if (texgen_init()) rx_set_tex(texgen_atlas());

	C3D_RenderTarget *topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	g_font = C2D_FontLoadSystem(CFG_REGION_EUR);
	g_buf = C2D_TextBufNew(8192);

	colBg         = RGBA(0x05, 0x03, 0x14, 0xFF);
	colWhite      = RGBA(0xFF, 0xFF, 0xFF, 0xFF);
	colDim        = RGBA(0xA8, 0xA0, 0xC8, 0xFF);
	colAccent     = RGBA(0x00, 0xE5, 0xFF, 0xFF);
	colPink       = RGBA(0xFF, 0x3F, 0xA0, 0xFF);
	colGold       = RGBA(0xFF, 0xD0, 0x40, 0xFF);
	colHint       = RGBA(0x80, 0xFF, 0x60, 0xFF);
	colSkyTop     = RGBA(0x04, 0x02, 0x12, 0xFF);
	colSkyMid     = RGBA(0x22, 0x0A, 0x44, 0xFF);
	colFog        = RGBA(0x5A, 0x1E, 0x74, 0xFF);
	colGroundNear = RGBA(0x08, 0x04, 0x16, 0xFF);
	colMountTop   = RGBA(0x30, 0x0C, 0x52, 0xFF);
	colMountBase  = RGBA(0x4E, 0x1A, 0x6A, 0xFF);
	colRoad       = RGBA(0x78, 0x80, 0xC0, 0xFF);
	colGrid       = RGBA(0xB0, 0x40, 0xFF, 0xFF);
	colRail       = RGBA(0x00, 0xE5, 0xFF, 0xFF);
	colDash       = RGBA(0x90, 0xE8, 0xFF, 0xFF);
	colBarr       = RGBA(0xFF, 0xA0, 0x10, 0xFF);
	colWall       = RGBA(0xFF, 0x30, 0x90, 0xFF);
	colPlayer     = RGBA(0x70, 0xE8, 0xFF, 0xFF);
	colPlayer2    = RGBA(0xFF, 0x50, 0xB0, 0xFF);
	colShadow     = RGBA(0x00, 0x00, 0x00, 0xFF);
	colPanel      = RGBA(0x0A, 0x06, 0x22, 0xC8);
	colPanelEdge  = RGBA(0x80, 0xE8, 0xFF, 0xFF);
	LIGHT = vnorm(v3(0.40f, 0.80f, -0.45f));

	srand(svcGetSystemTick());
	save_load();
	far_init();
	game_reset();
	audio_init();
	audio_start();
	etime = 0.0f;

	bool quit = false;
	u64 lastTick = svcGetSystemTick();
	while (!quit && aptMainLoop()) {
		/* fixed-step logic at 1/60 s, repeated for the elapsed vsyncs: if
		 * rendering drops a frame the game does NOT slow down (trail, inertia and
		 * spawns stay identical to 60 fps) */
		const float dt = 1.0f / 60.0f;
		u64 now = svcGetSystemTick();
		int steps = (int)((float)(now - lastTick) * 60.0f / SYSCLOCK_ARM11 + 0.5f);
		lastTick = now;
		if (steps < 1) steps = 1;
		if (steps > 4) steps = 4;      /* after long pauses (HOME): no jumps */
		hidScanInput();
		u32 kDown = hidKeysDown();

		/* touch on the PAUSE button of the bottom screen = START */
		if (kDown & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			if (tp.px >= PAUSE_BX && tp.px < PAUSE_BX + PAUSE_BW &&
			    tp.py >= PAUSE_BY && tp.py < PAUSE_BY + PAUSE_BH &&
			    (state == ST_PLAY || state == ST_PAUSE) && !demo)
				kDown |= KEY_START;
			kDown &= ~KEY_TOUCH;
		}

		/* DEMO: any button returns to the title (input consumed) */
		if (state != ST_TITLE && demo && kDown != 0) {
			to_title();
			kDown = 0;
		}

		if (kDown & KEY_SELECT)
			audio_set_music(!audio_music_on());

		switch (state) {
		case ST_TITLE:
			if (kDown & KEY_START) { quit = true; break; }
			if (kDown & KEY_UP)   { menuSel = (menuSel + 4) % 5; audio_move(); }
			if (kDown & KEY_DOWN) { menuSel = (menuSel + 1) % 5; audio_move(); }
			if (kDown & (KEY_LEFT | KEY_RIGHT)) {
				int dir = (kDown & KEY_LEFT) ? 2 : 1;
				if (menuSel == 2) { g_diff = (g_diff + dir) % 3; audio_move(); }
				if (menuSel == 3) { audio_set_music(!audio_music_on()); audio_move(); }
			}
			if (kDown & KEY_A) {
				audio_select();
				if (menuSel == 0) start_run(false);
				else if (menuSel == 1) start_run(true);
				else if (menuSel == 2) g_diff = (g_diff + 1) % 3;
				else if (menuSel == 3) audio_set_music(!audio_music_on());
				else quit = true;
			}
			dist += 7.0f * dt * steps;     /* slowly scrolling backdrop */
			break;

		case ST_PLAY:
			if ((kDown & KEY_START) && !demo) {
				state = ST_PAUSE;
				pauseSel = 0;
				audio_select();
				break;
			}
			for (int k = 0; k < steps && state == ST_PLAY; k++)
				update_play(dt, k == 0 ? kDown : 0);
			break;

		case ST_PAUSE:
			if (kDown & KEY_UP)   { pauseSel = (pauseSel + 2) % 3; audio_move(); }
			if (kDown & KEY_DOWN) { pauseSel = (pauseSel + 1) % 3; audio_move(); }
			if (kDown & (KEY_START | KEY_B)) { state = ST_PLAY; audio_select(); }
			else if (kDown & KEY_A) {
				audio_select();
				if (pauseSel == 0) state = ST_PLAY;
				else if (pauseSel == 1) start_run(false);
				else to_title();
			}
			break;

		case ST_OVER:
			overT += dt * steps;
			if (demo) {
				if (overT > 2.5f) to_title();
			} else if (overT > 1.0f) {
				if (kDown & (KEY_LEFT | KEY_RIGHT)) { overSel ^= 1; audio_move(); }
				if (kDown & KEY_A) {
					audio_select();
					if (overSel == 0) start_run(false); else to_title();
				} else if (kDown & KEY_B) {
					audio_select();
					to_title();
				}
			}
			break;
		}
		if (quit) break;
		audio_duck(state == ST_PAUSE);
		if (state != ST_PAUSE)
			for (int k = 0; k < steps; k++) update_fx(dt);

		/* ---------- stereoscopy: shake computed ONCE ---------- */
		float slider = osGet3DSliderState();
		bool use3d = slider > 0.02f;
		gfxSet3D(use3d);

		if (shake > 0.0f) {
			g_shx = (rndf() - 0.5f) * shake * 9.0f;
			g_shy = (rndf() - 0.5f) * shake * 9.0f;
		} else {
			g_shx = 0.0f; g_shy = 0.0f;
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TextBufClear(g_buf);
		camMid = make_cam();
		g_horY = OY - FOCAL * camMid.up.z / camMid.fwd.z + g_shy;

		/* 3D: the world is projected once (central camera) and
		 * replayed for the two eyes with per-vertex disparity */
		bool replay = false;
		if (use3d) {
			g_s = 0.0f;
			rx_rec_begin();
			draw_world(state != ST_TITLE);
			replay = rx_rec_end();
		}

		int passes = use3d ? 2 : 1;
		for (int eye = 0; eye < passes; eye++) {
			C3D_RenderTarget *tgt = (eye == 1) ? topR : topL;
			/* right eye: +parallax, left: -.  No rotation. */
			g_s = use3d ? ((eye == 1) ? EYE_BASE * slider : -EYE_BASE * slider)
				: 0.0f;

			C2D_TargetClear(tgt, colBg);
			C2D_SceneBegin(tgt);
			rx_scene();

			if (replay) rx_replay(g_s, MAX_DISP);
			else draw_world(state != ST_TITLE);

			if (flash > 0.0f)
				rect(0, 0, 400, 240, flashR > 0.5f ?
					with_a(RGBA(0xFF, 0x40, 0x60, 0xFF), flash * 0.5f) :
					with_a(colWhite, flash * 0.45f));

			/* HUD: screen-space = exactly on the glass plane */
			if (state == ST_TITLE) draw_menu();
			else {
				draw_hud();
				if (state == ST_PAUSE) draw_pause();
				if (state == ST_OVER) draw_results();
			}
		}

		/* ----- bottom screen ----- */
		C2D_TargetClear(bot, colBg);
		C2D_SceneBegin(bot);
		rx_scene();
		g_s = 0.0f;
		draw_bottom(slider);

		g_gpuUse = rx_usage();
		C3D_FrameEnd(0);
		/* no gspWaitForVBlank(): C3D_FrameBegin(SYNCDRAW) already waits for the
		 * vblank, a second wait halved the frame rate to 30 fps */
	}

	/* exit (EXIT or closing from the HOME menu): first wait for the GPU
	 * to finish the last frame, then free everything in reverse order */
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C3D_FrameEnd(0);
	gfxSet3D(false);
	audio_exit();
	C2D_TextBufDelete(g_buf);
	if (g_font) C2D_FontFree(g_font);
	texgen_exit();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
