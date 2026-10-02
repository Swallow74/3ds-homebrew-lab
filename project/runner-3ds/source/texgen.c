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
#include "texgen.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define AW 256          /* lato dell'atlas */

static C3D_Tex g_atlas;
static TexUV   g_uv[TX_NUM];
static bool    g_ready = false;

/* posizione delle tile nell'atlas (pixel, origine in alto a sinistra) */
static const struct { short x, y, w, h; } SLOT[TX_NUM] = {
	[TX_ROAD]     = {   0,   0, 64, 64 },
	[TX_FACADE_A] = {  64,   0, 64, 64 },
	[TX_FACADE_B] = { 128,   0, 64, 64 },
	[TX_ENERGY]   = { 192,   0, 64, 64 },
	[TX_HULL]     = {   0,  64, 64, 64 },
	[TX_SUN]      = {  64,  64, 64, 64 },
	[TX_GROUND]   = { 128,  64, 64, 64 },
	[TX_HAZARD]   = { 192,  64, 64, 32 },
	[TX_ROOF]     = { 192,  96, 64, 32 },
	[TX_GLOW]     = {   0, 128, 32, 32 },
	[TX_RING]     = {  32, 128, 32, 32 },
	[TX_STAR]     = {  64, 128, 32, 32 },
	[TX_COIN]     = {  96, 128, 32, 32 },
	[TX_MAGNET]   = { 128, 128, 32, 32 },
	[TX_SHIELD]   = { 160, 128, 32, 32 },
	[TX_X2]       = { 192, 128, 32, 32 },
	[TX_MARK]     = { 224, 128, 32, 32 },
	[TX_SHADE]    = {   0, 160, 32, 16 },
	[TX_WHITE]    = {   0, 176, 16, 16 },
	[TX_FLAME]    = {  32, 160, 16, 32 },
	[TX_BEAM]     = {  48, 160, 16, 32 },
	[TX_VISOR]    = {  64, 160, 64, 16 },
	[TX_TRAIL]    = {  64, 176, 64, 16 },
	[TX_CANOPY]   = { 128, 160, 32, 32 },
	[TX_SIGN_A]   = { 160, 160, 64, 32 },
	[TX_DASH]     = { 224, 160, 32, 32 },
	[TX_SIGN_B]   = {   0, 192, 64, 32 },
};

/* ------------------------------- canvas ---------------------------------- */
/* RGBA float, alpha NON premoltiplicata.  Le funzioni mk_* scrivono in
 * coordinate locali della tile corrente (0..W-1, 0..H-1). */
static float *g_px;
static int g_ox, g_oy, W, H;

static inline float clampf(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static void P(int x, int y, float r, float g, float b, float a)
{
	if (x < 0 || y < 0 || x >= W || y >= H) return;
	float *p = &g_px[((g_oy + y) * AW + g_ox + x) * 4];
	p[0] = clampf(r); p[1] = clampf(g); p[2] = clampf(b); p[3] = clampf(a);
}

static void G(int x, int y, float l, float a) { P(x, y, l, l, l, a); }

static unsigned int h2(int x, int y, int s)
{
	unsigned int v = (unsigned int)x * 374761393u + (unsigned int)y * 1252409279u +
		(unsigned int)s * 2246822519u;
	v = (v ^ (v >> 13)) * 3266489917u;
	return v ^ (v >> 16);
}

static float nz(int x, int y, int s) { return (float)(h2(x, y, s) & 1023) / 1023.0f; }

/* copertura anti-alias: d = distanza con segno dal bordo (negativo = dentro) */
static float cov(float d, float soft)
{
	return clampf(0.5f - d / soft);
}

/* distanza dal bordo della tile (per telai e griglie) */
static float edge_d(int x, int y)
{
	float ex = fminf(x + 0.5f, W - 0.5f - x);
	float ey = fminf(y + 0.5f, H - 0.5f - y);
	return fminf(ex, ey);
}

/* --------------------------- font 5x7 minimale ---------------------------- */
static const struct { char c; unsigned char row[7]; } FONT[] = {
	{ '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
	{ 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
	{ 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
	{ 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
	{ 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
	{ 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
};

/* maschera di testo a pixel: 1 dove c'e' il glifo (scala intera) */
static void text_mask(unsigned char *m, const char *s, int x0, int y0, int sc)
{
	for (int i = 0; s[i]; i++) {
		const unsigned char *rw = NULL;
		for (unsigned k = 0; k < sizeof(FONT) / sizeof(FONT[0]); k++)
			if (FONT[k].c == s[i]) rw = FONT[k].row;
		if (!rw) continue;
		for (int r = 0; r < 7; r++)
			for (int c = 0; c < 5; c++) {
				if (!(rw[r] & (0x10 >> c))) continue;
				for (int yy = 0; yy < sc; yy++)
					for (int xx = 0; xx < sc; xx++) {
						int px = x0 + i * 6 * sc + c * sc + xx;
						int py = y0 + r * sc + yy;
						if (px >= 0 && py >= 0 && px < W && py < H)
							m[py * W + px] = 1;
					}
			}
	}
}

/* distanza (in pixel, max R) dal pixel acceso piu' vicino della maschera */
static float mask_dist(const unsigned char *m, int x, int y, int R)
{
	float best = (float)R + 1.0f;
	for (int dy = -R; dy <= R; dy++)
		for (int dx = -R; dx <= R; dx++) {
			int px = x + dx, py = y + dy;
			if (px < 0 || py < 0 || px >= W || py >= H || !m[py * W + px])
				continue;
			float d = sqrtf((float)(dx * dx + dy * dy));
			if (d < best) best = d;
		}
	return best;
}

/* ------------------------------ le tile ----------------------------------- */

static void mk_road(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float l = 0.30f + 0.05f * (nz(x, y, 1) - 0.5f) +
				0.04f * (nz(x / 4, y / 4, 2) - 0.5f);
			/* giunti fra le piastre: incavo scuro + spigolo chiaro */
			if (x == 0 || y == 0 || y == 32) l = 0.13f;
			else if (x == 1 || y == 1 || y == 33) l = 0.44f;
			else if (x == W - 1 || y == H - 1 || y == 31) l = 0.20f;
			/* rivetti */
			static const int RV[6][2] = {
				{ 5, 5 }, { 58, 5 }, { 5, 37 }, { 58, 37 }, { 31, 5 }, { 31, 37 } };
			for (int k = 0; k < 6; k++) {
				float dx = x + 0.5f - RV[k][0], dy = y + 0.5f - RV[k][1];
				float d = sqrtf(dx * dx + dy * dy);
				if (d < 1.6f) l = 0.58f - d * 0.12f;
			}
			/* usura: leggera striscia piu' chiara dove passano i pattini */
			float wear = fabsf(x + 0.5f - 32.0f) < 10.0f ? 0.03f : 0.0f;
			G(x, y, l + wear, 1.0f);
		}
}

static void mk_ground(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float e = edge_d(x, y);
			float mx = fabsf(x + 0.5f - 32.0f), my = fabsf(y + 0.5f - 32.0f);
			float sub = fminf(mx, my);
			float l = 0.09f + 0.03f * nz(x, y, 3);
			l += 1.00f * clampf(1.0f - e / 1.4f);
			l += 0.22f * clampf(1.0f - e / 6.0f);
			l += 0.28f * clampf(1.0f - sub / 0.9f);
			G(x, y, l, 1.0f);
		}
}

static void mk_facade_a(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int cx = x / 16, cy = y / 16, ox = x % 16, oy = y % 16;
			float n = 0.03f * (nz(x, y, 4) - 0.5f);
			float r = 0.30f + n, g = 0.30f + n, b = 0.35f + n;
			if (ox < 2) { r += 0.07f; g += 0.07f; b += 0.08f; }   /* lesena  */
			if (oy < 2) { r -= 0.09f; g -= 0.09f; b -= 0.08f; }   /* marcapiano */
			if (ox >= 3 && ox <= 13 && oy == 13) { r = g = b = 0.50f; } /* davanzale */
			if (ox >= 4 && ox <= 12 && oy >= 4 && oy <= 12) {
				unsigned int hv = h2(cx, cy, 7);
				if ((hv & 1023) < 640) {
					/* finestra accesa: tre temperature di luce */
					static const float WC[3][3] = {
						{ 1.00f, 0.80f, 0.45f }, { 1.00f, 0.62f, 0.36f },
						{ 0.95f, 0.90f, 0.72f } };
					const float *c = WC[(hv >> 10) % 3];
					float lvl = 0.72f + 0.28f * (float)((hv >> 12) & 255) / 255.0f;
					lvl *= 0.80f + 0.20f * (float)(oy - 4) / 8.0f;
					if (ox == 8) lvl *= 0.55f;                       /* montante */
					r = c[0] * lvl; g = c[1] * lvl; b = c[2] * lvl;
				} else {
					float sh = ((ox + oy) % 6 < 2) ? 0.07f : 0.0f;  /* riflesso */
					r = 0.08f + sh; g = 0.11f + sh; b = 0.20f + sh;
				}
			}
			P(x, y, r, g, b, 1.0f);
		}
}

static void mk_facade_b(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int oy = y % 16, pane = x / 8, fl = y / 16;
			float n = 0.03f * (nz(x, y, 5) - 0.5f);
			float r = 0.26f + n, g = 0.28f + n, b = 0.34f + n;
			if (oy == 12) { r = g = b = 0.46f; }
			if (oy >= 2 && oy <= 11) {
				if (x % 8 == 0) { r = 0.20f; g = 0.22f; b = 0.28f; }
				else {
					unsigned int hv = h2(pane, fl, 9);
					if ((hv & 1023) < 560) {
						float lvl = 0.65f + 0.35f * (float)((hv >> 10) & 255) / 255.0f;
						lvl *= 0.85f + 0.15f * (float)(11 - oy) / 9.0f;
						r = 0.55f * lvl; g = 0.86f * lvl; b = 1.00f * lvl;
					} else {
						float sh = ((x + oy * 2) % 10 < 2) ? 0.08f : 0.0f;
						r = 0.07f + sh; g = 0.10f + sh; b = 0.19f + sh;
					}
				}
			}
			P(x, y, r, g, b, 1.0f);
		}
}

/* paratia del muro: mattoni sfalsati con bordi luminosi + scanline */
static void mk_energy(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int row = y / 16;
			int xs = (x + (row & 1) * 16) % 32;
			float ex = fminf(xs + 0.5f, 32.0f - xs - 0.5f);
			float ey = fminf((y % 16) + 0.5f, 16.0f - (y % 16) - 0.5f);
			float e = fminf(ex, ey);
			float l = 0.30f + 0.10f * (1.0f - (float)(y % 16) / 16.0f);
			l += 1.00f * clampf(1.0f - e / 1.3f);
			l += 0.25f * clampf(1.0f - e / 5.0f);
			if (y % 3 == 0) l -= 0.06f;
			G(x, y, l, 1.0f);
		}
}

static void mk_hull(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float l = 0.90f - 0.14f * (float)y / (float)H + 0.03f * nz(x, y, 6);
			float fx = fmodf(x + 0.5f, 32.0f), fy = fmodf(y + 0.5f, 32.0f);
			if (fx < 1.2f || fy < 1.2f) l = 0.45f;
			else if (fx < 2.2f || fy < 2.2f) l = 0.98f;
			if (fabsf((x + 0.5f) - (y + 0.5f) * 0.5f - 40.0f) < 0.8f && y > 34)
				l = 0.50f;                                    /* taglio obliquo */
			if (y >= 44 && y <= 49) l *= 0.55f;                /* banda di colore */
			for (int k = 0; k < 4; k++) {
				float rx = (k & 1) ? 27.5f : 4.5f, ry = (k & 2) ? 27.5f : 4.5f;
				float dx = fx - rx, dy = fy - ry;
				if (dx * dx + dy * dy < 1.6f) l = 0.62f;
			}
			G(x, y, l, 1.0f);
		}
}

static void mk_sun(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - 32.0f, dy = y + 0.5f - 32.0f;
			float d = sqrtf(dx * dx + dy * dy);
			float t = clampf((float)y / 60.0f);
			t = t * t * (3.0f - 2.0f * t);
			float r = 1.00f, g = 0.93f + (0.20f - 0.93f) * t,
				b = 0.35f + (0.62f - 0.35f) * t;
			float a = cov(d - 30.0f, 1.6f);
			/* tagli orizzontali nella meta' bassa, sempre piu' larghi */
			if (y > 30) {
				int yy = y - 30;
				int gap = 1 + yy / 8;
				if (yy % 7 < gap) a = 0.0f;
			}
			P(x, y, r, g, b, a);
		}
}

static void mk_hazard(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float ph = fmodf((float)x + (float)y, 16.0f);
			float l = (ph < 8.0f) ? 1.0f : 0.14f;
			if (ph < 0.8f || (ph > 7.2f && ph < 8.8f) || ph > 15.2f) l = 0.55f;
			float e = edge_d(x, y);
			if (e < 2.0f) l = 0.10f;
			else if (e < 3.0f) l = 0.75f;
			G(x, y, l, 1.0f);
		}
}

static void mk_roof(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float l = 0.26f + 0.05f * (nz(x, y, 8) - 0.5f);
			if (x >= 8 && x <= 22 && y >= 7 && y <= 23)
				l = ((y - 7) % 3 == 0) ? 0.40f : 0.16f;     /* griglia di sfiato */
			if (x >= 34 && x <= 56 && y >= 9 && y <= 21) {
				l = 0.40f;
				if (x == 34 || x == 56 || y == 9 || y == 21) l = 0.58f;
			}
			float e = edge_d(x, y);
			if (e < 2.0f) l = 0.50f;
			G(x, y, l, 1.0f);
		}
}

static void mk_glow(void)
{
	float cx = W * 0.5f, cy = H * 0.5f, R = W * 0.5f - 0.5f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float a = clampf(1.0f - sqrtf(dx * dx + dy * dy) / R);
			a = a * a * (0.55f + 0.45f * a);
			G(x, y, 1.0f, a);
		}
}

static void mk_ring(void)
{
	float cx = W * 0.5f, cy = H * 0.5f, R = W * 0.5f - 3.0f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d = sqrtf(dx * dx + dy * dy);
			float a = clampf(1.0f - fabsf(d - R) / 2.6f);
			a = a * a;
			float inner = clampf(1.0f - d / R) * 0.16f;
			G(x, y, 1.0f, a > inner ? a : inner);
		}
}

static void mk_star(void)
{
	float cx = W * 0.5f, cy = H * 0.5f, R = W * 0.5f - 0.5f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d = sqrtf(dx * dx + dy * dy);
			float core = clampf(1.0f - d / (0.28f * R));
			core *= core;
			float thin = fminf(fabsf(dx), fabsf(dy));
			float ray = clampf(1.0f - d / R) * clampf(1.0f - thin / (0.10f * R));
			ray *= ray;
			G(x, y, 1.0f, core + ray * 0.9f);
		}
}

static void mk_coin(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - 16.0f, dy = y + 0.5f - 16.0f;
			float d = sqrtf(dx * dx + dy * dy);
			float r = d / 14.5f;
			float a = cov(d - 14.5f, 1.4f);
			/* luce da alto-sinistra */
			float lf = 0.78f + 0.22f * clampf((-dx - dy) / 20.0f + 0.5f);
			float cr = 1.00f, cg = 0.78f, cb = 0.18f;
			if (r > 0.80f) {                              /* bordo zigrinato */
				float k = ((int)(atan2f(dy, dx) * 9.0f) & 1) ? 0.85f : 1.0f;
				cr = 0.86f * k; cg = 0.56f * k; cb = 0.10f * k;
				lf = 0.70f + 0.30f * clampf((-dx - dy) / 16.0f + 0.5f);
			} else if (r > 0.62f && r < 0.70f) {          /* incisione */
				cr = 0.80f; cg = 0.52f; cb = 0.08f;
			} else if (r <= 0.62f) {
				/* stella a 5 punte in rilievo */
				float th = atan2f(dx, -dy);
				float c5 = fabsf(cosf(2.5f * th));
				float rb = 0.46f * (0.48f + 0.52f * c5 * c5 * c5);
				if (r < rb) { cr = 1.00f; cg = 0.95f; cb = 0.58f; }
			}
			/* riflesso speculare */
			float hx = dx + 5.0f, hy = dy + 6.0f;
			float spec = clampf(1.0f - sqrtf(hx * hx + hy * hy) / 5.0f);
			spec = spec * spec * 0.8f;
			P(x, y, cr * lf + spec, cg * lf + spec, cb * lf + spec, a);
		}
}

/* icone: contorno scuro di 1 pixel attorno alla maschera, colori veri */
static void mk_magnet(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float dx = px - 16.0f, dy = py - 15.0f;
			float d = sqrtf(dx * dx + dy * dy);
			/* U: meta' bassa di una corona + due gambe */
			float inU = -1.0f;
			if (py >= 15.0f) inU = fminf(d - 5.0f, 12.0f - d);
			else {
				float lg = fminf(px - 4.0f, 10.0f - px);
				float rg = fminf(px - 22.0f, 28.0f - px);
				inU = fmaxf(lg, rg);
				inU = fminf(inU, py - 4.0f);
			}
			float a = clampf(inU + 1.6f);          /* include il contorno */
			float r, g, b;
			if (inU < 0.5f) { r = 0.12f; g = 0.05f; b = 0.08f; }
			else if (py < 9.0f) {                  /* punte d'acciaio */
				r = 0.92f; g = 0.94f; b = 1.00f;
			} else {
				float lf = 0.75f + 0.25f * clampf((20.0f - px) / 16.0f);
				r = 0.98f * lf; g = 0.18f * lf; b = 0.26f * lf;
			}
			P(x, y, r, g, b, a);
		}
}

static void mk_shield(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float px = x + 0.5f, py = y + 0.5f;
			float dx = fabsf(px - 16.0f);
			float hw = (py < 15.0f) ? 12.0f :
				12.0f * powf(clampf(1.0f - (py - 15.0f) / 14.0f), 0.75f);
			float inS = fminf(hw - dx, fminf(py - 3.0f, 29.0f - py));
			float a = clampf(inS + 1.6f);
			float r, g, b;
			if (inS < 0.5f) { r = 0.03f; g = 0.07f; b = 0.16f; }
			else if (inS < 2.6f) { r = 0.55f; g = 0.92f; b = 1.00f; }
			else {
				float t = clampf((py - 5.0f) / 22.0f);
				r = 0.10f - 0.05f * t; g = 0.42f - 0.20f * t; b = 0.90f - 0.30f * t;
				if (fabsf(px - 16.0f) < 1.5f || fabsf(py - 13.0f) < 1.5f) {
					r = 0.85f; g = 0.97f; b = 1.00f;    /* croce centrale */
				}
			}
			P(x, y, r, g, b, a);
		}
}

static void mk_x2(void)
{
	unsigned char m[32 * 32];
	memset(m, 0, sizeof(m));
	text_mask(m, "2X", 5, 9, 2);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float d = m[y * W + x] ? 0.0f : mask_dist(m, x, y, 2);
			float a = clampf(2.4f - d);
			float t = (float)(y - 9) / 14.0f;
			if (d < 0.5f)
				P(x, y, 1.0f, 0.95f - 0.35f * t, 0.30f - 0.20f * t, 1.0f);
			else
				P(x, y, 0.20f, 0.08f, 0.0f, a);
		}
}

static void mk_mark(void)
{
	float rx = W * 0.5f - 1.0f, ry = H * 0.5f - 1.0f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - W * 0.5f, dy = y + 0.5f - H * 0.5f;
			float d = fabsf(dx) / rx + fabsf(dy) / ry;
			float band = clampf(1.0f - fabsf(d - 0.62f) / 0.20f);
			float fill = clampf(1.0f - d) * 0.22f;
			G(x, y, 1.0f, band * band * 0.95f + fill);
		}
}

static void mk_shade(void)
{
	float rx = W * 0.5f - 0.5f, ry = H * 0.5f - 0.5f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - W * 0.5f, dy = y + 0.5f - H * 0.5f;
			float q = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
			float a = clampf(1.0f - q);
			G(x, y, 0.0f, a * a * 0.9f);
		}
}

static void mk_white(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) G(x, y, 1.0f, 1.0f);
}

static void mk_flame(void)
{
	float rx = W * 0.5f - 0.5f, ry = H * 0.5f - 0.5f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - W * 0.5f, dy = y + 0.5f - H * 0.5f;
			float k = 1.0f - (dy * dy) / (ry * ry);
			if (k < 0.12f) k = 0.12f;
			float q = (dy * dy) / (ry * ry) + (dx * dx) / (rx * rx * k);
			float a = clampf(1.0f - q);
			float core = clampf(1.0f - q * 2.2f);
			G(x, y, 1.0f, a * a * 0.75f + core * 0.5f);
		}
}

static void mk_beam(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = fabsf(x + 0.5f - W * 0.5f) / (W * 0.5f);
			float hx = clampf(1.0f - dx);
			float vy = (float)y / (float)(H - 1);
			G(x, y, 1.0f, hx * hx * vy * vy);
		}
}

static void mk_visor(void)
{
	float rx = W * 0.5f - 1.0f, ry = H * 0.5f;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = x + 0.5f - W * 0.5f, dy = y + 0.5f - H * 0.5f;
			float ay = clampf(1.0f - fabsf(dy) / (0.42f * ry));
			float ax = clampf(1.0f - fabsf(dx) / rx);
			G(x, y, 1.0f, ay * ay * ax * (0.35f + 0.65f * ax));
		}
}

/* profilo trasversale morbido: nucleo pieno + alone (linee al neon) */
static void mk_trail(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dy = fabsf(y + 0.5f - H * 0.5f) / (H * 0.5f);
			float core = clampf(1.0f - dy / 0.30f);
			float halo = clampf(1.0f - dy);
			G(x, y, 1.0f, core * 0.7f + halo * halo * 0.45f);
		}
}

static void mk_canopy(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float t = (float)y / (float)(H - 1);
			float r = 0.50f - 0.40f * t, g = 0.88f - 0.62f * t, b = 1.00f - 0.52f * t;
			float band = fabsf((x + 0.5f) + (y + 0.5f) * 0.8f - 20.0f);
			if (band < 3.0f) { r += 0.35f; g += 0.30f; b += 0.20f; }
			if (edge_d(x, y) < 1.5f) { r = 0.10f; g = 0.14f; b = 0.20f; }
			P(x, y, r, g, b, 1.0f);
		}
}

/* insegna: tubi al neon (nucleo bianco + alone) attorno a una scritta */
static void mk_sign(const char *txt, int tx, bool arrow)
{
	unsigned char m[64 * 32];
	memset(m, 0, sizeof(m));
	text_mask(m, txt, tx, 9, 2);
	/* cornice arrotondata */
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float e = edge_d(x, y);
			if (e >= 2.0f && e < 3.0f) m[y * W + x] = 1;
		}
	if (arrow)
		for (int y = 11; y <= 20; y++) {
			int w = 5 - abs(y - 15);
			for (int x = 53; x < 53 + w + 2; x++)
				if (x < W) m[y * W + x] = 1;
		}
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float d = m[y * W + x] ? 0.0f : mask_dist(m, x, y, 3);
			float a = (d < 0.5f) ? 1.0f : 0.40f * clampf(1.0f - (d - 0.5f) / 3.0f);
			G(x, y, 1.0f, a);
		}
}

static void mk_sign_a(void) { mk_sign("NEON", 9, false); }
static void mk_sign_b(void) { mk_sign("RUSH", 5, true); }

static void mk_dash(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float dx = fabsf(x + 0.5f - 16.0f);
			float py = y + 0.5f;
			float dy = py < 6.0f ? 6.0f - py : (py > 26.0f ? py - 26.0f : 0.0f);
			float d = sqrtf(dx * dx + dy * dy);
			float core = clampf(1.0f - (d - 2.0f) / 1.5f);
			float halo = clampf(1.0f - d / 9.0f);
			G(x, y, 1.0f, core + halo * halo * 0.35f);
		}
}

/* ------------------------------ upload ------------------------------------- */

/* indice Morton dentro un blocco 8x8 */
static inline unsigned morton(unsigned x, unsigned y)
{
	return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) |
		((x & 4) << 2) | ((y & 4) << 3);
}

static void swizzle(const float *src, int w, u32 *dst)
{
	for (int y = 0; y < w; y++)
		for (int x = 0; x < w; x++) {
			const float *p = &src[(y * w + x) * 4];
			u32 r = (u32)(p[0] * 255.0f + 0.5f), g = (u32)(p[1] * 255.0f + 0.5f);
			u32 b = (u32)(p[2] * 255.0f + 0.5f), a = (u32)(p[3] * 255.0f + 0.5f);
			unsigned i = (((y >> 3) * (w >> 3) + (x >> 3)) << 6) + morton(x & 7, y & 7);
			dst[i] = (r << 24) | (g << 16) | (b << 8) | a;   /* GPU_RGBA8 */
		}
}

/* dimezza il livello: media pesata sull'alpha (niente aloni scuri) */
static void downsample(const float *src, int w, float *dst)
{
	int h = w / 2;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < h; x++) {
			float r = 0, g = 0, b = 0, a = 0, pr = 0, pg = 0, pb = 0;
			for (int k = 0; k < 4; k++) {
				const float *p = &src[((2 * y + (k >> 1)) * w + 2 * x + (k & 1)) * 4];
				r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3];
				pr += p[0]; pg += p[1]; pb += p[2];
			}
			float *o = &dst[(y * h + x) * 4];
			if (a > 1e-4f) { o[0] = r / a; o[1] = g / a; o[2] = b / a; }
			else { o[0] = pr / 4; o[1] = pg / 4; o[2] = pb / 4; }
			o[3] = a / 4;
		}
}

bool texgen_init(void)
{
	static void (*const FN[TX_NUM])(void) = {
		[TX_ROAD] = mk_road,       [TX_FACADE_A] = mk_facade_a,
		[TX_FACADE_B] = mk_facade_b, [TX_ENERGY] = mk_energy,
		[TX_HULL] = mk_hull,       [TX_SUN] = mk_sun,
		[TX_GROUND] = mk_ground,   [TX_HAZARD] = mk_hazard,
		[TX_ROOF] = mk_roof,       [TX_GLOW] = mk_glow,
		[TX_RING] = mk_ring,       [TX_STAR] = mk_star,
		[TX_COIN] = mk_coin,       [TX_MAGNET] = mk_magnet,
		[TX_SHIELD] = mk_shield,   [TX_X2] = mk_x2,
		[TX_MARK] = mk_mark,       [TX_SHADE] = mk_shade,
		[TX_WHITE] = mk_white,     [TX_FLAME] = mk_flame,
		[TX_BEAM] = mk_beam,       [TX_VISOR] = mk_visor,
		[TX_TRAIL] = mk_trail,     [TX_CANOPY] = mk_canopy,
		[TX_SIGN_A] = mk_sign_a,   [TX_DASH] = mk_dash,
		[TX_SIGN_B] = mk_sign_b,
	};

	float *lv[2];
	g_px = calloc((size_t)AW * AW * 4, sizeof(float));
	lv[0] = malloc((size_t)(AW / 2) * (AW / 2) * 4 * sizeof(float));
	lv[1] = malloc((size_t)(AW / 4) * (AW / 4) * 4 * sizeof(float));
	u32 *tmp = linearAlloc((size_t)AW * AW * 4);
	if (!g_px || !lv[0] || !lv[1] || !tmp) {
		free(g_px); free(lv[0]); free(lv[1]);
		if (tmp) linearFree(tmp);
		return false;
	}

	/* pixel non coperti: bianco trasparente */
	for (int i = 0; i < AW * AW; i++) {
		g_px[i * 4 + 0] = g_px[i * 4 + 1] = g_px[i * 4 + 2] = 1.0f;
	}
	for (int i = 0; i < TX_NUM; i++) {
		g_ox = SLOT[i].x; g_oy = SLOT[i].y; W = SLOT[i].w; H = SLOT[i].h;
		FN[i]();
		/* mezzo texel di margine: niente sbavature dalla tile accanto */
		g_uv[i].u0 = (g_ox + 0.5f) / AW;
		g_uv[i].u1 = (g_ox + W - 0.5f) / AW;
		g_uv[i].v0 = 1.0f - (g_oy + 0.5f) / AW;
		g_uv[i].v1 = 1.0f - (g_oy + H - 0.5f) / AW;
	}
	/* il bianco pieno si campiona al centro: nessun filtro ai bordi */
	g_uv[TX_WHITE].u0 = g_uv[TX_WHITE].u1 = (SLOT[TX_WHITE].x + 8.0f) / AW;
	g_uv[TX_WHITE].v0 = g_uv[TX_WHITE].v1 = 1.0f - (SLOT[TX_WHITE].y + 8.0f) / AW;

	if (!C3D_TexInitMipmap(&g_atlas, AW, AW, GPU_RGBA8)) {
		free(g_px); free(lv[0]); free(lv[1]); linearFree(tmp);
		return false;
	}

	/* livello 0, poi dimezzamenti successivi fino a maxLevel */
	const float *cur = g_px;
	int w = AW;
	for (int level = 0; level <= g_atlas.maxLevel; level++) {
		swizzle(cur, w, tmp);
		C3D_TexLoadImage(&g_atlas, tmp, GPU_TEXFACE_2D, level);
		if (level == g_atlas.maxLevel) break;
		float *nx = lv[level & 1];
		downsample(cur, w, nx);
		cur = nx;
		w /= 2;
	}
	C3D_TexFlush(&g_atlas);
	C3D_TexSetFilter(&g_atlas, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetFilterMipmap(&g_atlas, GPU_LINEAR);
	C3D_TexSetWrap(&g_atlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	free(g_px); g_px = NULL;
	free(lv[0]); free(lv[1]);
	linearFree(tmp);
	g_ready = true;
	return true;
}

void texgen_exit(void)
{
	if (!g_ready) return;
	g_ready = false;
	C3D_TexDelete(&g_atlas);
}

C3D_Tex *texgen_atlas(void) { return g_ready ? &g_atlas : NULL; }

const TexUV *texgen_uv(TexId id)
{
	if ((int)id < 0 || id >= TX_NUM) id = TX_WHITE;
	return &g_uv[id];
}
