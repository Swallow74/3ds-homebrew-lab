/*
 * NEON RUSH 3DS - runner "dentro lo schermo" per Nintendo 3DS
 *
 * 3 corsie, ostacoli in avvicinamento, salto con gravita', stereoscopia
 * "comoda" (parallel-shift: nessun occhio ruota, disparita' sempre e solo
 * dietro il piano dello schermo), audio DSP con musica a loop + effetti,
 * protagonista a pezzi con texture procedurali, palazzine, pali della luce,
 * nebbia volumetrica in stile, particelle, scie di velocita', best su SD.
 *
 * Comandi (vedi anche schermo inferiore):
 *   MENU    : Sx/Dx = difficolta', Su/Giu' = GIOCA/DEMO, A = vai
 *   Gioco   : D-Pad Sx/Dx corsia, A/Su salto
 *   GAME OVER: torna sempre al titolo (auto o a pressione)
 *   DEMO    : qualsiasi pulsante torna al titolo
 *   SELECT  : musica on/off
 *   START   : esci a hbmenu
 *
 * Difficolta' (FACILE/NORMALE/DIFFICILE): cambia velocita' iniziale e massima,
 * frequenza degli ostacoli, quanti muri (non saltabili) compaiono e il
 * moltiplicatore dei punti.
 * DEMO: corre la CPU - salta le barriere basse e cambia corsia per i muri.
 * Punti: ogni salto dona un bonus immediato, e il salto "pulito" sopra una
 * barriera bassa da' un bonus extra.
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

/* ------------------------------- vettori ---------------------------------- */
typedef struct { float x, y, z; } V3;

static const V3 VZERO = {0.0f, 0.0f, 0.0f};

static V3 vsub(V3 a, V3 b)
{
	V3 r = {a.x - b.x, a.y - b.y, a.z - b.z};
	return r;
}

static float vdot(V3 a, V3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static V3 vcross(V3 a, V3 b)
{
	V3 r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
	        a.x * b.y - a.y * b.x};
	return r;
}

static V3 vnorm(V3 a)
{
	float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
	if (l < 1e-6f) { V3 z = {0.0f, 0.0f, 0.0f}; return z; }
	V3 r = {a.x / l, a.y / l, a.z / l};
	return r;
}

/* camera dietro il giocatore, guarda dentro lo schermo (+z = lontano) */
static const V3 CAM_BASE = {0.0f, 3.6f, -7.5f};
static const V3 CAM_TGT  = {0.0f, 1.0f, 9.0f};
#define FOCAL 300.0f
#define OX    200.0f
#define OY    150.0f

/* STEREOSCOPIA COMODA
 * Entrambi gli occhi usano la STESSA camera (quella centrale): la differenza
 * fra le due immagini e' solo uno spostamento orizzontale "sh" in pixel che
 * dipende dalla profondita' (parallel-shift + depth shear).  Il piano a
 * disparita' zero (D_SCREEN) e' messo DavANTI a tutto il contenuto 3D (la
 * prima cosa che appare e' a d ~ 4): cosi' la disparita' e' sempre e soltanto
 * "non incrociata" (oggetto dietro il vetro) e gli occhi non devono MAI
 * incrociare, che e' la causa vera di mal di testa/affaticamento.
 * Nessuna rotazione di camera: le due occhiate restano parallele, come le
 * camere vere.  HUD, flash e cielo sono in screen-space: disparita' 0, cioe'
 * esattamente sul piano del vetro, con un leggero effetto "cornice".
 */
#define D_SCREEN 3.2f     /* piano a disparita' zero, unita' mondo          */
#define EYE_BASE 0.085f   /* parallasse unitaria per occhio a slider massimo */
#define MAX_DISP 7.5f     /* budget: max pixel di spostamento per singolo occhio */

typedef struct { V3 eye, fwd, right, up; } Cam;

static Cam camMid;
static float g_s = 0.0f;                 /* +/- parallasse occhio corrente   */
static float g_shx = 0.0f, g_shy = 0.0f; /* scossa: IDENTICA per entrambi    */

static Cam make_cam(void)
{
	V3 e = CAM_BASE;
	V3 fwd = vnorm(vsub(CAM_TGT, e));
	V3 up0 = {0.0f, 1.0f, 0.0f};
	V3 right = vnorm(vcross(up0, fwd));
	V3 up = vcross(fwd, right);
	Cam c = { e, fwd, right, up };
	return c;
}

static float depth_of(V3 p)
{
	V3 v = vsub(p, camMid.eye);
	return vdot(v, camMid.fwd);
}

static bool project(V3 p, float *sx, float *sy)
{
	V3 v = vsub(p, camMid.eye);
	float d = vdot(v, camMid.fwd);
	if (d < 0.6f) return false;

	float x = OX + vdot(v, camMid.right) * FOCAL / d;
	if (g_s != 0.0f) {
		float sh = g_s * FOCAL * (1.0f / D_SCREEN - 1.0f / d);
		if (sh > MAX_DISP) sh = MAX_DISP;
		if (sh < -MAX_DISP) sh = -MAX_DISP;
		x += sh;
	}
	x += g_shx;

	*sx = x;
	*sy = OY - vdot(v, camMid.up) * FOCAL / d + g_shy;
	return true;
}

/* ------------------------------- colori ----------------------------------- */
static u32 colBg, colWhite, colDim, colAccent, colGrid, colRail, colPlayer,
	colBarr, colWall, colSkyTop, colSkyHor, colGround, colBldg, colWin,
	colFlame, colDash, colShadow, colHor, colHint, colPart;

#define FOG_NEAR 11.0f
#define FOG_FAR  46.0f
#define FOG_MAX  0.78f   /* non arriva mai a cancellare del tutto: restano le luci */

static inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static u32 shade(u32 c, float f)
{
	unsigned r = c & 0xFF, g = (c >> 8) & 0xFF,
	         b = (c >> 16) & 0xFF, a = (c >> 24) & 0xFF;
	r = (unsigned)(r * f); g = (unsigned)(g * f); b = (unsigned)(b * f);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static u32 with_a(u32 col, float a)
{
	unsigned al = (unsigned)(clampf(a, 0.0f, 1.0f) * 255.0f);
	if (al > 255) al = 255;
	return (col & 0x00FFFFFFu) | (al << 24);
}

static u32 mixc(u32 a, u32 b, float t)
{
	t = clampf(t, 0.0f, 1.0f);
	unsigned r = (unsigned)((a & 0xFF) + (((b & 0xFF) - (a & 0xFF)) * t));
	unsigned g = (unsigned)(((a >> 8) & 0xFF) +
		((((b >> 8) & 0xFF) - ((a >> 8) & 0xFF)) * t));
	unsigned bl = (unsigned)(((a >> 16) & 0xFF) +
		(((((b >> 16) & 0xFF) - ((a >> 16) & 0xFF))) * t));
	unsigned al = (unsigned)(((a >> 24) & 0xFF) +
		(((((b >> 24) & 0xFF) - ((a >> 24) & 0xFF))) * t));
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (bl > 255) bl = 255;
	if (al > 255) al = 255;
	return (al << 24) | (bl << 16) | (g << 8) | r;
}

/* nebbia: piu' un oggetto e' lontano, piu' va verso il colore del fondo.
 * E' anche il modo piu' efficace di tenere "comoda" la profondita' lontana. */
static u32 fog(u32 col, float d)
{
	float t = (d - FOG_NEAR) / (FOG_FAR - FOG_NEAR);
	if (t < 0.0f) t = 0.0f;
	if (t > FOG_MAX) t = FOG_MAX;
	return mixc(col, colBg, t);
}

/* alpha "di nebbia": 1 vicino, ~0 lontano. Per sprite/linee decorative. */
static float fogA(float d, float maxd)
{
	float t = (d - FOG_NEAR) / (maxd - FOG_NEAR);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	return 1.0f - t;
}

/* ------------------------------ primitive --------------------------------- */
static void tri(float x0, float y0, float x1, float y1,
	float x2, float y2, u32 col)
{
	C2D_DrawTriangle(x0, y0, col, x1, y1, col, x2, y2, col, 0.5f);
}

static void quad(float x0, float y0, float x1, float y1,
	float x2, float y2, float x3, float y3, u32 col)
{
	tri(x0, y0, x1, y1, x2, y2, col);
	tri(x0, y0, x2, y2, x3, y3, col);
}

/* segmento nel mondo: proiezione + nebbia sulla profondita' media */
static void seg(V3 a, V3 b, u32 col, float w)
{
	float x0, y0, x1, y1;
	if (!project(a, &x0, &y0)) return;
	if (!project(b, &x1, &y1)) return;
	V3 m = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
	u32 c = fog(col, depth_of(m));
	C2D_DrawLine(x0, y0, c, x1, y1, c, w, 0.5f);
}

static bool g_texok = false;   /* false se le texture procedurali non ci sono */

/* sprite nel mondo: una texture procedurale inquadrata a dimensione reale.
 * w/h sono in unita' mondo; il colore e' il tint, alpha = texture * tint. */
static void billboard(TexId tx, V3 p, float w, float h, u32 col, float alpha)
{
	if (!g_texok || alpha <= 0.004f || w <= 0.0f || h <= 0.0f) return;

	float d = depth_of(p);
	if (d < 0.6f) return;

	float sxp, syp;
	if (!project(p, &sxp, &syp)) return;

	float wpx = FOCAL * w / d;
	float hpx = FOCAL * h / d;
	if (wpx < 0.8f || hpx < 0.8f) return;            /* sotto il pixel: inutile */
	if (sxp < -90.0f || sxp > 490.0f) return;
	if (syp < -90.0f || syp > 330.0f) return;

	C2D_Image im = texgen_get(tx);
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, with_a(col, alpha), 1.0f);
	C2D_DrawImageAt(im, sxp - wpx * 0.5f, syp - hpx * 0.5f, 0.5f, &tint,
		wpx / (float)im.subtex->width, hpx / (float)im.subtex->height);
}

/* ------------------------------- box -------------------------------------- */
/* vertici nell'ordine usato da sempre: 0..3 in basso, 4..7 in alto.
 * top = 4,5,6,7 - front(-z) = 0,1,5,4 - left(x0) = 0,3,7,4 - right(x1) = 1,2,6,5 */
static const float CN[8][3] = {
	{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1},
	{-1,  1, -1}, {1,  1, -1}, {1,  1, 1}, {-1,  1, 1},
};

/* roll (rotazione attorno z) poi pitch (attorno x), poi traslazione pivot */
static V3 xform(float lx, float ly, float lz, float roll, float pitch, V3 piv)
{
	if (roll == 0.0f && pitch == 0.0f) {
		V3 r = {piv.x + lx, piv.y + ly, piv.z + lz};
		return r;
	}
	float cr = cosf(roll), sr = sinf(roll);
	float cp = cosf(pitch), sp = sinf(pitch);
	float x1 = lx * cr - ly * sr;
	float y1 = lx * sr + ly * cr;
	float y2 = y1 * cp - lz * sp;
	float z2 = y1 * sp + lz * cp;
	V3 r = {piv.x + x1, piv.y + y2, piv.z + z2};
	return r;
}

/* box con 3 facce visibili + bordo luminoso.  side: +1 mostra la faccia
 * destra (x1), -1 la sinistra (x0): la faccia visibile e' quella rivolta
 * verso la camera, quindi side = (x_oggetto < 0) ? +1 : -1. */
static void draw_cbox(float cx, float cy, float cz, float hx, float hy,
	float hz, u32 col, int side, float roll, float pitch, V3 piv, float dref,
	int rim)
{
	float sx[8], sy[8];
	int ok = 1;

	if (side == 0) {   /* auto: la faccia visibile e' quella verso la camera */
		V3 cc = xform(cx, cy, cz, roll, pitch, piv);
		side = (cc.x < 0.0f) ? 1 : -1;
	}

	for (int i = 0; i < 8; i++) {
		V3 loc = {cx + CN[i][0] * hx, cy + CN[i][1] * hy, cz + CN[i][2] * hz};
		V3 w = xform(loc.x, loc.y, loc.z, roll, pitch, piv);
		if (!project(w, &sx[i], &sy[i])) { ok = 0; break; }
	}
	if (!ok) return;

	u32 cTop = fog(shade(col, 1.00f), dref);
	u32 cFront = fog(shade(col, 0.62f), dref);
	u32 cSide = fog(shade(col, 0.38f), dref);

	quad(sx[4], sy[4], sx[5], sy[5], sx[6], sy[6], sx[7], sy[7], cTop);
	quad(sx[0], sy[0], sx[1], sy[1], sx[5], sy[5], sx[4], sy[4], cFront);
	if (side < 0)
		quad(sx[0], sy[0], sx[3], sy[3], sx[7], sy[7], sx[4], sy[4], cSide);
	else
		quad(sx[1], sy[1], sx[2], sy[2], sx[6], sy[6], sx[5], sy[5], cSide);

	if (rim && dref < 26.0f) {
		u32 cRim = fog(mixc(col, 0xFFFFFFFFu, 0.70f), dref * 0.75f);
		static const int E[6][2] = {
			{4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5},
		};
		for (int i = 0; i < 6; i++)
			C2D_DrawLine(sx[E[i][0]], sy[E[i][0]], cRim,
				sx[E[i][1]], sy[E[i][1]], cRim, 1.2f, 0.5f);
	}
}

/* --------------------------- stato di gioco ------------------------------- */
#define SPAWN_Z 48.0f
#define MAXOBS 24
#define MAXPART 120
static const float LANEX[3] = {-2.2f, 0.0f, 2.2f};

typedef struct {
	bool on;
	int lane;      /* 0..2 */
	float z;       /* distanza in avanti (0 = giocatore) */
	int type;      /* 0 = barriera bassa (si salta), 1 = muro (si schiva) */
	bool scored;
} Obst;

typedef struct {
	bool on;
	float x, y, z, vx, vy, vz, life, maxlife;
	u32 color;
	bool streak; /* scia di velocita' */
} Part;

static Obst obs[MAXOBS];
static Part parts[MAXPART];
static int lane;            /* corsia target 0..2 */
static float laneX, jumpY, jumpV;
static bool airborne;
static float dist, speed, score;
static int best;
static bool over;
static float flash, shake, crashT;
static float spawnT;
static float etime;
static float jumpFx;        /* countdown del "+punti salto" nell'HUD */
static int   jumpKind;      /* 1 = salto, 2 = salto pulito sopra la barriera */
static float overT;         /* secondi passati sul GAME OVER */

static bool menu = true;    /* schermo titolo: qui si scelgono modalita' e livello */
static bool demo;           /* demo: corre la CPU */
static int  g_diff = 1;     /* 0 facile, 1 normale, 2 difficile */

/* Tavole di difficolta': velocita' iniziale, velocita' massima, moltiplicatore
 * del gap fra le righe di ostacoli, probabilita' che una corsia sia un MURO
 * (i muri NON si possono saltare), moltiplicatore dei punti. */
static const float D_SPD0[3]   = { 9.0f, 11.0f, 13.0f };
static const float D_SPDMAX[3] = { 22.0f, 30.0f, 36.0f };
static const float D_GAPMUL[3] = { 1.30f, 1.0f, 0.80f };
static const int   D_WALLP[3]  = { 30, 55, 78 };
static const float D_PTS[3]    = { 1.0f, 1.6f, 2.4f };
static const char *const D_NAME[3] = { "EASY", "NORMAL", "HARD" };

static float rndf(void) { return (float)(rand() % 1000) / 1000.0f; }

/* hash deterministico: palazzine sempre uguali a se stesse mentre scorrono */
static unsigned int hsh(unsigned int v)
{
	v = v * 265465761u;
	v ^= v >> 13;
	v *= 265465761u;
	return v ^ (v >> 16);
}

/* ------------------------------ best score -------------------------------- */
static const char *BEST_PATH = "sdmc:/3ds/runner-3ds/best.txt";

static void best_load(void)
{
	best = 0;
	FILE *f = fopen(BEST_PATH, "r");
	if (f) {
		fscanf(f, "%d", &best);
		fclose(f);
	}
}

static void best_save(void)
{
	mkdir("sdmc:/3ds", 0777); /* mkdir non e' ricorsiva: entrambi i livelli */
	mkdir("sdmc:/3ds/runner-3ds", 0777);
	FILE *f = fopen(BEST_PATH, "w");
	if (f) {
		fprintf(f, "%d\n", best);
		fclose(f);
	}
}

/* ------------------------------ particelle -------------------------------- */
static void burst(float x, float y, float z, u32 color, int n, bool streak)
{
	for (int k = 0; k < n; k++) {
		Part *p = NULL;
		for (int i = 0; i < MAXPART; i++)
			if (!parts[i].on) { p = &parts[i]; break; }
		if (!p) return;
		p->on = true;
		p->streak = streak;
		p->x = x + (rndf() - 0.5f);
		p->y = y + rndf() * 1.5f;
		p->z = z;
		if (streak) {
			p->vx = 0; p->vy = 0; p->vz = -speed * 1.5f;
			p->life = p->maxlife = 0.5f;
		} else {
			p->vx = (rndf() - 0.5f) * 8.0f;
			p->vy = rndf() * 7.0f + 2.0f;
			p->vz = (rndf() - 0.5f) * 4.0f;
			p->life = p->maxlife = 0.6f + rndf() * 0.6f;
		}
		p->color = color;
	}
}

static void game_reset(void)
{
	memset(obs, 0, sizeof(obs));
	memset(parts, 0, sizeof(parts));
	lane = 1; laneX = LANEX[1];
	jumpY = 0; jumpV = 0; airborne = false;
	dist = 0; speed = D_SPD0[g_diff]; score = 0;
	over = false; flash = 0; shake = 0; crashT = 0; spawnT = 1.0f;
	jumpFx = 0; jumpKind = 0; overT = 0;
}

/* genera una riga di ostacoli garantendo almeno una via d'uscita */
static void spawn_row(void)
{
	int freeLane = rand() % 3;
	int walls = 0;
	for (int l = 0; l < 3; l++) {
		if (l == freeLane && (rand() % 100) < 70) continue; /* via libera */
		Obst *o = NULL;
		for (int i = 0; i < MAXOBS; i++)
			if (!obs[i].on) { o = &obs[i]; break; }
		if (!o) return;
		o->on = true;
		o->lane = l;
		o->z = SPAWN_Z;
		o->scored = false;
		if (l == freeLane) {
			o->type = 0; /* sulla via di fuga solo barriere saltabili */
		} else if (walls < 2 && (rand() % 100) < D_WALLP[g_diff]) {
			o->type = 1;
			walls++;
		} else {
			o->type = 0;
		}
	}
}

/* --------------------------------- testo ---------------------------------- */
static void draw_text(C2D_TextBuf buf, C2D_Font font, const char *str,
	float x, float y, float scale, u32 color)
{
	C2D_Text t;
	C2D_TextFontParse(&t, font, buf, str);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_AlignLeft | C2D_WithColor, x, y, 0.5f,
		scale, scale, color);
}

/* --------------------------------- IA demo ----------------------------------
 * Gli ostacoli arrivano in "righe" parallele (tutte alla stessa z) e ogni riga
 * ha sempre almeno una corsia libera o con barriera saltabile.  La CPU guarda
 * la riga piu' vicina e quella dopo: si piazza dove entrambe sono praticabili
 * e ci resta, perche' inseguire le righe lontane e' il modo piu' veloce per
 * finire addosso a un muro.  Dentro il box di collisione (z < 0.9) non si
 * entra mai: li' la partita e' gia' decisa.
 */
static int lane_box_blocked(int l)
{
	for (int i = 0; i < MAXOBS; i++) {
		Obst *o = &obs[i];

		if (o->on && o->lane == l && o->z > -0.9f && o->z < 0.9f) return 1;
	}
	return 0;
}

/* Tipo dell'ostacolo che la riga a zr mette nella corsia l: -1 nulla,
 * 0 = barriera saltabile, 1 = muro (il muro vince). */
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

/* La cosa piu' vicina sopra una corsia: restituisce la sua z, e in *typ il
 * tipo.  Sotto 0.6 non si guarda piu': tanto non la si puo' piu' saltare. */
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

/* Punteggio di una corsia: 3 = libera, 2 = barriera (la salti), 0 = muro.
 * La riga successiva conta meta', e c'e' una preferenza per non ballare. */
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
	return sc;
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
	if (z1 > 200.0f) return;      /* pista libera: niente da decidere */

	for (int l = 0; l < 3; l++) {
		if (lane_box_blocked(l)) continue;
		float sc = lane_score(l, z1, z2);

		if (sc > bestS) { bestS = sc; want = l; }
	}
	if (want != lane && !lane_box_blocked(want)) {
		if (lane < want) lane++; else lane--;
		audio_move();
	}

	/* Sopra la tua corsia c'e' una barriera: saltala.  Il salto fa picco a
	 * ~0.36 s e resta sopra l'altezza buona (jumpY > 0.85) fra ~0.14 e
	 * ~0.58 s, quindi la finestra utile dell'impatto e' quella. */
	int typ;
	float bz = lane_nearest(lane, &typ);

	if (typ == 0 && !airborne) {
		float tt = bz / speed;

		if (tt >= 0.18f && tt <= 0.50f) {
			airborne = true;
			jumpV = 8.0f;
			score += 3.0f * D_PTS[g_diff];
			jumpFx = 0.9f;  jumpKind = 1;
			audio_jump();
		}
	}
}

/* ------------------------------ schermo titolo ----------------------------- */
static void draw_menu(C2D_TextBuf buf, C2D_Font font)
{
	char line[64];

	/* velatura: il mondo dietro continua a scorrere, ma il titolo comanda */
	C2D_DrawRectSolid(0, 0, 0.5f, 400, 240,
		C2D_Color32(0x00, 0x00, 0x08, 0xA8));
	C2D_DrawRectSolid(0, 92, 0.5f, 400, 2,
		C2D_Color32(0x00, 0x60, 0x80, 0xC0));

	unsigned fa = (unsigned)((0.78f + 0.22f * sinf(etime * 1.7f)) * 255.0f);
	if (fa > 255) fa = 255;
	draw_text(buf, font, "NEON RUSH", 88, 20, 1.05f,
		C2D_Color32(0x00, 0xE5, 0xFF, fa));
	draw_text(buf, font, "in-screen runner", 116, 62, 0.5f, colDim);
	snprintf(line, sizeof(line), "Best %d", best);
	draw_text(buf, font, line, 300, 62, 0.5f, colDim);

	draw_text(buf, font, "<", 92, 104, 0.6f, colAccent);
	draw_text(buf, font, ">", 302, 104, 0.6f, colAccent);
	snprintf(line, sizeof(line), "DIFFICULTY: %s", D_NAME[g_diff]);
	draw_text(buf, font, line, 128, 104, 0.6f, colWhite);

	draw_text(buf, font, "MODE", 160, 130, 0.5f, colDim);
	draw_text(buf, font, "PLAY", 122, 148, 0.6f, demo ? colDim : colAccent);
	draw_text(buf, font, "DEMO", 240, 148, 0.6f, demo ? colAccent : colDim);
	if (demo)
		draw_text(buf, font, "CPU plays", 152, 172, 0.45f, colHint);
	else
		draw_text(buf, font, "barriers: jump   walls: dodge", 112, 172,
			0.45f, colHint);

	draw_text(buf, font, "Left/Right level   Up/Down mode   A start", 40, 208,
		0.45f, colDim);
}

/* --------------------------------- mondo ---------------------------------- */
/* cielo a bande: e' screen-space, quindi sta' esattamente sul piano del
 * vetro (disparita' 0).  La linea di fuga e' OY = 150. */
static void draw_sky(void)
{
	for (int i = 0; i < 10; i++) {
		float t = (float)i / 10.0f;
		u32 c = mixc(colSkyTop, colSkyHor, t * t);
		C2D_DrawRectSolid(0, i * 15.0f, 0.5f, 400.0f, 15.0f, c);
	}
	C2D_DrawRectSolid(0, 150.0f, 0.5f, 400.0f, 90.0f, colGround);
}

static void draw_horizon(void)
{
	V3 p = {0.0f, 0.55f, 62.0f};
	billboard(TX_DOT, p, 48.0f, 6.0f, colHor, 0.40f);
	billboard(TX_RING, p, 24.0f, 2.8f, colHor, 0.28f);
}

static void draw_stars(const float stars[60][3])
{
	for (int i = 0; i < 60; i++) {
		V3 p = {stars[i][0], stars[i][1], stars[i][2]};
		float tw = 0.5f + 0.5f * sinf(etime * 2.0f + (float)i * 1.7f);
		float sz = 0.6f + 0.3f * sinf(etime * 1.3f + (float)i * 0.7f);
		billboard(TX_STAR, p, sz, sz, colWhite, 0.18f + 0.28f * tw);
	}
}

/* sede stradale: griglia limitata in x (non si infilza nelle palazzine),
 * trattamenti alle corsie e rotaie laterali a chunk cosi' la nebbia lavora */
static void draw_road(float dist)
{
	static const float ZC[5] = {-4.0f, 8.0f, 20.0f, 34.0f, 50.0f};
	static const float ZR[5] = {-4.0f, 6.0f, 16.0f, 30.0f, 50.0f};
	float cell = 4.0f;
	float off = fmodf(dist, cell);

	for (int k = 0; k < 14; k++) {
		float z = k * cell - off;
		if (z < -3.0f) continue;
		seg((V3){-4.2f, 0.0f, z}, (V3){4.2f, 0.0f, z}, colGrid, 1.0f);
	}
	for (int gx = -4; gx <= 4; gx++) {
		for (int s = 0; s < 4; s++) {
			seg((V3){(float)gx, 0.0f, ZC[s]}, (V3){(float)gx, 0.0f, ZC[s + 1]},
				colGrid, 1.0f);
		}
	}

	/* trattamenti di corsia che scorrono */
	for (int i = 0; i < 2; i++) {
		float lx = (i == 0) ? -1.1f : 1.1f;
		float doff = fmodf(dist, 3.0f);
		for (int k = 0; k < 17; k++) {
			float z0 = k * 3.0f - doff - 3.0f;
			float z1 = z0 + 1.7f;
			if (z1 < -3.5f || z0 > 52.0f) continue;
			seg((V3){lx, 0.02f, z0}, (V3){lx, 0.02f, z1}, colDash, 2.0f);
		}
	}

	/* bordi della sede */
	for (int s = -1; s <= 1; s += 2) {
		float rx = s * 3.3f;
		for (int k = 0; k < 4; k++)
			seg((V3){rx, 0.0f, ZR[k]}, (V3){rx, 0.0f, ZR[k + 1]}, colRail, 2.2f);
	}
}

static void draw_buildings(float dist)
{
	float base = fmodf(dist, 7.0f);

	for (int k = 13; k >= 0; k--) {
		float z = k * 7.0f - base + 2.0f;
		if (z < 1.0f || z > 66.0f) continue;

		for (int s = -1; s <= 1; s += 2) {
			unsigned int h = hsh((unsigned int)(k * 8 + (s > 0 ? 3 : 0)));
			float hh = 2.5f + (float)(h % 1000) / 1000.0f * 5.5f;
			float bx = s * (5.2f + (float)((h >> 4) % 1000) / 1000.0f * 2.4f);
			float bw = 1.2f + (float)((h >> 9) % 1000) / 1000.0f * 0.9f;
			V3 c = {bx, hh * 0.5f, z};
			float d = depth_of(c);
			float fa = fogA(d, 62.0f);

			draw_cbox(bx, hh * 0.5f, z, bw, hh * 0.5f, bw, colBldg,
				0, 0.0f, 0.0f, VZERO, d, 0);
			billboard(TX_WIN, (V3){bx, hh * 0.5f, z - bw - 0.02f},
				bw * 1.7f, hh * 0.85f, colWin, 0.70f * fa);
			billboard(TX_DOT, (V3){bx, hh + 0.3f, z}, 1.3f, 1.3f, colWin,
				0.16f * fa);
		}
	}
}

static void draw_posts(float dist)
{
	float base = fmodf(dist, 4.0f);

	for (int k = 13; k >= 0; k--) {
		float z = k * 4.0f - base;
		if (z < -3.0f || z > 54.0f) continue;

		for (int s = -1; s <= 1; s += 2) {
			float px = s * 3.6f;
			float d = depth_of((V3){px, 0.3f, z});

			draw_cbox(px, 0.30f, z, 0.09f, 0.30f, 0.09f, colBldg,
				0, 0.0f, 0.0f, VZERO, d, 0);
			billboard(TX_DOT, (V3){px, 0.74f, z}, 0.5f, 0.5f, colAccent,
				0.55f * fogA(d, 40.0f));
			billboard(TX_DOT, (V3){px, 0.02f, z}, 1.2f, 0.38f, colAccent,
				0.18f * fogA(d, 24.0f));
		}
	}
}

static void draw_obst(Obst *o)
{
	float x = LANEX[o->lane];
	float h = (o->type == 1) ? 1.30f : 0.45f;
	u32 col = (o->type == 1) ? colWall : colBarr;
	float d = depth_of((V3){x, h, o->z});
	/* 1 quando e' vicino, 0 quando e' gia' quasi inghiottito dalla nebbia */
	float nr = clampf(1.0f - (d - 28.0f) / 22.0f, 0.0f, 1.0f);
	/* lampeggio lento: niente lampeggi a 10 Hz che affaticano */
	float pulse = 0.55f + 0.45f * sinf(etime * 3.0f - o->z * 0.35f);

	billboard(TX_SHADE, (V3){x, 0.02f, o->z}, 1.8f, 0.5f, colShadow, 0.45f * nr);
	draw_cbox(x, h, o->z, 0.9f, h, 0.45f, col, 0, 0.0f, 0.0f, VZERO, d,
		d < 24.0f);

	if (o->type == 0) {
		/* barriera bassa: faccia coperta dai chevron + piastra superiore */
		billboard(TX_CHEV, (V3){x, h, o->z - 0.47f}, 1.66f, 0.72f,
			mixc(col, 0xFFFFFFFFu, 0.25f), 0.95f * nr);
		billboard(TX_HULL, (V3){x, h * 2.0f + 0.03f, o->z}, 1.66f, 0.34f,
			mixc(col, 0xFFFFFFFFu, 0.15f), 0.50f * nr);
		billboard(TX_MARK, (V3){x, 1.30f, o->z}, 0.72f, 0.72f, colHint,
			(0.28f + 0.42f * pulse) * nr);
	} else {
		/* muro: paratia luminosa + spigolo inferiore + piastra di testa */
		billboard(TX_WALLT, (V3){x, h, o->z - 0.47f}, 1.66f, 2.4f,
			mixc(col, 0xFFFFFFFFu, 0.20f), 0.95f * nr);
		billboard(TX_STRIPE, (V3){x, 0.32f, o->z - 0.47f}, 1.66f, 0.55f, col,
			0.80f * nr);
		billboard(TX_HULL, (V3){x, h * 2.0f + 0.03f, o->z}, 1.66f, 0.40f,
			mixc(col, 0xFFFFFFFFu, 0.15f), 0.50f * nr);
		billboard(TX_MARK, (V3){x, 2.95f, o->z}, 0.60f, 0.60f, colWall,
			(0.22f + 0.30f * pulse) * nr);
	}
}

/* ----------------------------- protagonista ------------------------------- */
/* Hover-racer a pezzi: hull, naso, motore, canopy, testa del pilota, due
 * alette con punte luminose, due pod di gravita'.  Roll in curva, pitch in
 * salto, texture procedurali sopra, fiamme e glow sotto. */
static void draw_player(float px, float py, float lean, float spd)
{
	V3 piv = {px, py, 0.0f};
	V3 q;
	float dref = depth_of(piv);
	float roll = lean;
	float pitch = (py > 0.05f) ? -0.13f : 0.0f;
	u32 cDark = C2D_Color32(0x14, 0x22, 0x44, 0xFF);
	u32 cLight = C2D_Color32(0x90, 0xD0, 0xE8, 0xFF);

	/* ombra a terra: piu' e' alto piu' si stringe e schiarisce */
	float shS = 1.0f / (1.0f + py * 0.30f);
	billboard(TX_SHADE, (V3){px, 0.02f, 0.0f}, 2.0f * shS, 0.62f * shS,
		colShadow, 0.55f * shS);

	/* corpo principale */
	draw_cbox(0.0f, 0.34f, 0.05f, 0.50f, 0.18f, 0.80f, colPlayer, 0,
		roll, pitch, piv, dref, 1);
	/* naso */
	draw_cbox(0.0f, 0.30f, 1.05f, 0.30f, 0.12f, 0.35f, colPlayer, 0,
		roll, pitch, piv, dref, 0);
	/* blocco motore */
	draw_cbox(0.0f, 0.40f, -0.95f, 0.34f, 0.22f, 0.28f, cDark, 0,
		roll, pitch, piv, dref, 0);
	/* canopy */
	draw_cbox(0.0f, 0.60f, -0.05f, 0.26f, 0.18f, 0.42f, cLight, 0,
		roll, pitch, piv, dref, 1);
	/* testa del pilota */
	draw_cbox(0.0f, 0.86f, 0.00f, 0.12f, 0.12f, 0.14f, colPlayer, 0,
		roll, pitch, piv, dref, 0);

	for (int s = -1; s <= 1; s += 2) {
		/* alette + punte luminose */
		draw_cbox(s * 0.72f, 0.34f, -0.55f, 0.22f, 0.05f, 0.35f, colPlayer,
			0, roll, pitch, piv, dref, 0);
		draw_cbox(s * 0.90f, 0.34f, -0.50f, 0.05f, 0.04f, 0.12f, colAccent,
			0, roll, pitch, piv, dref, 0);
		/* pod di gravita' */
		draw_cbox(s * 0.40f, 0.14f, 0.00f, 0.14f, 0.10f, 0.45f, cDark, 0,
			roll, pitch, piv, dref, 0);
	}

	/* dettagli con texture: pannello sul dorso, visiera, emblema sul naso */
	q = xform(0.0f, 0.53f, 0.05f, roll, pitch, piv);
	billboard(TX_PANEL, q, 0.85f, 1.35f, colAccent, 0.26f);
	q = xform(0.0f, 0.62f, -0.05f, roll, pitch, piv);
	billboard(TX_VISOR, q, 0.52f, 0.13f, colWhite, 0.70f);
	q = xform(0.0f, 0.44f, 1.22f, roll, pitch, piv);
	billboard(TX_MARK, q, 0.34f, 0.34f, colAccent, 0.55f);

	/* texture delle superfici: piastre su scafo, alette e blocco motore */
	u32 cHullTx = mixc(colPlayer, 0xFFFFFFFFu, 0.25f);
	q = xform(0.0f, 0.53f, 0.05f, roll, pitch, piv);
	billboard(TX_HULL, q, 0.90f, 1.30f, cHullTx, 0.55f);
	for (int s = -1; s <= 1; s += 2) {
		q = xform(s * 0.72f, 0.40f, -0.55f, roll, pitch, piv);
		billboard(TX_HULL, q, 0.40f, 0.60f, cHullTx, 0.50f);
	}
	q = xform(0.0f, 0.63f, -0.95f, roll, pitch, piv);
	billboard(TX_HULL, q, 0.60f, 0.50f,
		mixc(cDark, 0xFFFFFFFFu, 0.30f), 0.50f);

	/* glow di gravita' sotto i pod */
	for (int s = -1; s <= 1; s += 2) {
		q = xform(s * 0.40f, 0.05f, 0.0f, roll, pitch, piv);
		billboard(TX_DOT, q, 0.6f, 0.6f, colAccent,
			0.22f + 0.10f * sinf(etime * 7.0f + (float)s));
	}

	/* propulsori: lingua di fiamma + alone, entrambi con la velocita' */
	for (int s = -1; s <= 1; s += 2) {
		float fl = 0.45f + spd * 0.018f + 0.10f * sinf(etime * 23.0f + s * 2.0f);
		if (fl < 0.18f) fl = 0.18f;
		q = xform(s * 0.20f, 0.40f, -1.25f, roll, pitch, piv);
		billboard(TX_FLAME, q, 0.24f, fl, colFlame, 0.85f);
		q = xform(s * 0.20f, 0.40f, -1.16f, roll, pitch, piv);
		billboard(TX_DOT, q, 0.45f, 0.45f, colFlame, 0.32f);
	}
	q = xform(0.0f, 0.40f, -1.20f, roll, pitch, piv);
	billboard(TX_DOT, q, 0.8f, 0.8f, colAccent, 0.28f);

	/* alone di "spinta": solo quando si va forte */
	if (spd > 17.0f) {
		q = xform(0.0f, 0.42f, -0.10f, roll, pitch, piv);
		billboard(TX_RING, q, 2.3f, 1.7f, colAccent,
			0.08f + 0.06f * sinf(etime * 9.0f));
	}
}

/* ------------------------------- particelle ------------------------------- */
static void draw_particles(void)
{
	for (int i = 0; i < MAXPART; i++) {
		Part *p = &parts[i];
		if (!p->on) continue;

		float a = p->life / p->maxlife;
		u32 col = with_a(p->color, a);

		if (p->streak) {
			/* scie di velocita': linee parallele al senso di marcia */
			V3 b = {p->x, p->y, p->z};
			V3 e = {p->x, p->y, p->z + 3.0f};
			float x0, y0, x1, y1;
			if (!project(b, &x0, &y0)) continue;
			if (!project(e, &x1, &y1)) continue;
			float d = depth_of(b);
			u32 c = fog(col, d);
			C2D_DrawLine(x0, y0, c, x1, y1, c, 2.0f, 0.5f);
		} else {
			float sz = 0.22f + 0.30f * a;
			/* colore puro + alpha di vita: billboard() decide l'alpha da qui */
			billboard(TX_DOT, (V3){p->x, p->y, p->z}, sz, sz, p->color,
				0.9f * a);
		}
	}
}

/* anello di crash che si allarga nel mondo (il flash e' invece sullo schermo) */
static void draw_crash_ring(void)
{
	if (crashT <= 0.0f || crashT > 0.9f) return;

	float t = crashT / 0.9f;
	float a = (1.0f - t) * 0.6f;
	float sz = 1.5f + t * 9.0f;
	V3 p = {laneX, 0.6f + jumpY, 0.0f};
	billboard(TX_RING, p, sz, sz * 0.55f, colWall, a);
	billboard(TX_RING, p, sz * 0.55f, sz * 0.3f, colWhite, a * 0.7f);
}

/* ---------------------------------- main ---------------------------------- */
int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();
	g_texok = texgen_init();

	C3D_RenderTarget *topL = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	C2D_Font font = C2D_FontLoadSystem(CFG_REGION_EUR);
	C2D_TextBuf buf = C2D_TextBufNew(8192);

	colBg     = C2D_Color32(0x05, 0x05, 0x18, 0xFF);
	colWhite  = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
	colDim    = C2D_Color32(0xA0, 0xA0, 0xB8, 0xFF);
	colAccent = C2D_Color32(0x00, 0xE5, 0xFF, 0xFF);
	colGrid   = C2D_Color32(0x20, 0x50, 0x90, 0xFF);
	colRail   = C2D_Color32(0x00, 0xB0, 0xD0, 0xFF);
	colPlayer = C2D_Color32(0x00, 0xE5, 0xFF, 0xFF);
	colBarr   = C2D_Color32(0xFF, 0xA0, 0x00, 0xFF);
	colWall   = C2D_Color32(0xFF, 0x30, 0x80, 0xFF);
	colHint   = C2D_Color32(0x80, 0xFF, 0x60, 0xFF);
	colShadow = C2D_Color32(0x00, 0x00, 0x00, 0xFF);
	colPart   = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
	colSkyTop = C2D_Color32(0x04, 0x04, 0x10, 0xFF);
	colSkyHor = C2D_Color32(0x2A, 0x14, 0x52, 0xFF);
	colGround = C2D_Color32(0x0A, 0x0A, 0x22, 0xFF);
	colBldg   = C2D_Color32(0x12, 0x14, 0x4A, 0xFF);
	colWin    = C2D_Color32(0xFF, 0xC0, 0x60, 0xFF);
	colFlame  = C2D_Color32(0xFF, 0x90, 0x30, 0xFF);
	colDash   = C2D_Color32(0x60, 0x80, 0xC0, 0xFF);
	colHor    = C2D_Color32(0x80, 0x40, 0xC0, 0xFF);

	srand(svcGetSystemTick());
	best_load();
	game_reset();
	audio_init();
	audio_start();

	/* stelle lontane fisse */
	static float stars[60][3];
	for (int i = 0; i < 60; i++) {
		stars[i][0] = (rndf() - 0.5f) * 60.0f;
		stars[i][1] = 2.0f + rndf() * 16.0f;
		stars[i][2] = 35.0f + rndf() * 25.0f;
	}

	etime = 0.0f;

	while (aptMainLoop()) {
		const float dt = 1.0f / 60.0f;
		hidScanInput();
		u32 kDown = hidKeysDown();

		/* DEMO: qualsiasi pulsante riporta alla schermata dei titoli.
		 * Vale sia durante la corsa demo che sul crash della CPU: l'input
		 * viene consumato qui cosi' non esce a hbmenu e non toggla la
		 * musica, si torna solo al titolo. */
		if (!menu && demo && kDown != 0) {
			menu = true;
			demo = false;
			game_reset();
			kDown = 0;
		}

		if (kDown & KEY_START) break;

		if (kDown & KEY_SELECT)
			audio_set_music(!audio_music_on());

		if (menu) {
			/* --- schermo titolo: qui si scelgono difficolta' e modalita' --- */
			if (kDown & KEY_LEFT)  { g_diff = (g_diff + 2) % 3; audio_move(); }
			if (kDown & KEY_RIGHT) { g_diff = (g_diff + 1) % 3; audio_move(); }
			if (kDown & (KEY_UP | KEY_DOWN)) { demo = !demo; audio_move(); }
			if (kDown & KEY_A) {
				menu = false;
				game_reset();
				audio_start();
			}
			dist += 7.0f * dt;  /* fondale che scorre lentamente */
		} else if (over) {
			overT += dt;
			/* Sconfitta: si torna sempre al titolo, da soli dopo ~2.5 s
			 * o subito a pressione di qualsiasi tasto (tranne SELECT che
			 * resta musica on/off e START che resta uscita a hbmenu). */
			if (overT > 2.5f ||
			    (kDown & ~(KEY_SELECT | KEY_START)) != 0) {
				menu = true;
				demo = false;
				game_reset();
			}
		} else {
			if (demo) {
				demo_control();
			} else {
				/* corsie */
				if ((kDown & KEY_LEFT) && lane > 0) { lane--; audio_move(); }
				if ((kDown & KEY_RIGHT) && lane < 2) { lane++; audio_move(); }
				/* salto: OGNI salto dona subito dei punti, proporzionati alla
				 * difficolta'; il bonus "pulito" si aggiunge a parte */
				if ((kDown & (KEY_A | KEY_UP)) && !airborne) {
					airborne = true;
					jumpV = 8.0f;
					score += 3.0f * D_PTS[g_diff];
					jumpFx = 0.9f;  jumpKind = 1;
					audio_jump();
				}
			}

			if (airborne) {
				jumpY += jumpV * dt;
				jumpV -= 22.0f * dt;
				if (jumpY <= 0.0f) {
					jumpY = 0.0f; airborne = false;
					burst(laneX, 0.1f, 0.0f,
						C2D_Color32(0x80, 0x80, 0xA0, 0xFF), 6, false);
				}
			}
			laneX += (LANEX[lane] - laneX) * 0.25f;

			speed += 0.25f * dt;
			if (speed > D_SPDMAX[g_diff]) speed = D_SPDMAX[g_diff];
			dist += speed * dt;
			score += speed * dt;

			spawnT -= dt;
			if (spawnT <= 0.0f) {
				spawn_row();
				float gap = (1.4f - speed * 0.03f) * D_GAPMUL[g_diff];
				if (gap < 0.40f) gap = 0.40f;
				spawnT = gap * (0.8f + rndf() * 0.4f);
			}

			for (int i = 0; i < MAXOBS; i++) {
				Obst *o = &obs[i];
				if (!o->on) continue;
				float prev = o->z;
				o->z -= speed * dt;
				if (o->z < -4.0f) { o->on = false; continue; }
				if (!o->scored && prev > 0.0f && o->z <= 0.0f) {
					o->scored = true;
					if (o->lane != lane) {
						score += 5.0f * D_PTS[g_diff];
					} else if (o->type == 0 && jumpY > 0.85f) {
						/* salto PULITO: bonus extra sopra i punti del salto */
						score += 20.0f * D_PTS[g_diff];
						jumpFx = 1.2f;  jumpKind = 2;
						audio_bonus();
					}
				}
				if (o->z > -0.9f && o->z < 0.9f && o->lane == lane) {
					bool hit = false;
					if (o->type == 1) hit = true;
					else if (jumpY < 0.85f) hit = true;
					if (hit) {
						over = true;
						flash = 1.0f;
						shake = 1.0f;
						crashT = 0.0001f;
						burst(laneX, 0.5f + jumpY, 0.0f, colPlayer, 40, false);
						burst(laneX, 0.5f, 0.0f, colWall, 20, false);
						audio_crash();
						if (!demo && (int)score > best) {
							best = (int)score;
							best_save();
						}
					}
				}
			}

			/* scie di velocita' ai bordi */
			if (speed > 16.0f && (rand() % 100) < (int)(speed - 14.0f)) {
				float sx = ((rand() % 100) < 50 ? -1.0f : 1.0f) *
					(4.0f + rndf() * 4.0f);
				burst(sx, 0.5f + rndf() * 5.0f, 28.0f,
					C2D_Color32(0x80, 0xC0, 0xFF, 0xFF), 1, true);
			}
		}

		/* particelle */
		for (int i = 0; i < MAXPART; i++) {
			Part *p = &parts[i];
			if (!p->on) continue;
			p->life -= dt;
			if (p->life <= 0) { p->on = false; continue; }
			if (!p->streak) {
				p->vy -= 14.0f * dt;
				p->x += p->vx * dt;
				p->y += p->vy * dt;
				if (p->y < 0.05f) { p->y = 0.05f; p->vy *= -0.4f; }
			}
			p->z += p->vz * dt;
			if (p->z < -4.0f) p->on = false;
		}
		if (flash > 0.0f) {
			flash -= dt * 1.5f;
			if (flash < 0.0f) flash = 0.0f;
		}
		if (shake > 0.0f) {
			shake -= dt * 2.0f;
			if (shake < 0.0f) shake = 0.0f;
		}
		if (crashT > 0.0f) crashT += dt;
		if (jumpFx > 0.0f) jumpFx -= dt;
		etime += dt;

		/* ---------- stereoscopia: scossa calcolata UNA volta sola ---------- */
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
		C2D_TextBufClear(buf);
		camMid = make_cam();

		int passes = use3d ? 2 : 1;
		for (int eye = 0; eye < passes; eye++) {
			C3D_RenderTarget *tgt = (eye == 1) ? topR : topL;
			/* occhio destro: +parallasse, sinistro: -.  Nessuna rotazione. */
			g_s = use3d ? ((eye == 1) ? EYE_BASE * slider : -EYE_BASE * slider)
				: 0.0f;

			C2D_TargetClear(tgt, colBg);
			C2D_SceneBegin(tgt);

			draw_sky();
			draw_horizon();
			draw_stars(stars);
			draw_buildings(dist);
			draw_posts(dist);
			draw_road(dist);

			if (!menu) {
				/* ostacoli: prima i lontani, poi i vicini (ordine pittore) */
				for (int pass = 0; pass < 2; pass++) {
					for (int i = 0; i < MAXOBS; i++) {
						Obst *o = &obs[i];
						if (!o->on) continue;
						bool far = o->z > 12.0f;
						if ((pass == 0) != far) continue;
						draw_obst(o);
					}
				}

				if (!over)
					draw_player(laneX, jumpY,
						(LANEX[lane] - laneX) * 0.35f, speed);
				draw_crash_ring();
				draw_particles();

				if (flash > 0.0f) {
					unsigned a = (unsigned)(flash * 120.0f);
					if (a > 255) a = 255;
					C2D_DrawRectSolid(0, 0, 0.5f, 400, 240,
						C2D_Color32(0xFF, 0x40, 0x40, a));
				}

				/* HUD: screen-space = esattamente sul piano del vetro */
				char line[64];
				snprintf(line, sizeof(line), "SCORE %d", (int)score);
				draw_text(buf, font, line, 12, 12, 0.6f, colWhite);
				snprintf(line, sizeof(line), "BEST %d", best > (int)score ?
					best : (int)score);
				draw_text(buf, font, line, 12, 36, 0.5f, colDim);
				snprintf(line, sizeof(line), "SPD %d", (int)(speed * 3.6f));
				draw_text(buf, font, line, 300, 12, 0.55f, colAccent);

				if (demo)
					draw_text(buf, font, "DEMO", 12, 60, 0.5f, colHint);

				/* "+punti" del salto: appare e sfuma, cosi' si vede che ogni
				 * salto porta punti davvero, e quanto */
				if (jumpFx > 0.0f) {
					unsigned a = (unsigned)(jumpFx * 220.0f);
					if (a > 255) a = 255;
					if (jumpKind == 2) {
						snprintf(line, sizeof(line), "+%d CLEAN JUMP",
							(int)(20.0f * D_PTS[g_diff]));
						draw_text(buf, font, line, 116, 82, 0.55f,
							C2D_Color32(0x80, 0xFF, 0x60, a));
					} else {
						snprintf(line, sizeof(line), "+%d JUMP",
							(int)(3.0f * D_PTS[g_diff]));
						draw_text(buf, font, line, 150, 82, 0.5f,
							C2D_Color32(0x00, 0xE5, 0xFF, a));
					}
				}

				if (over) {
					C2D_DrawRectSolid(90, 80, 0.5f, 220, 64,
						C2D_Color32(0x00, 0x00, 0x00, 0xC0));
					draw_text(buf, font, "CRASH!", 150, 88, 0.7f, colWhite);
					if (demo) {
						draw_text(buf, font, "the CPU crashed", 122, 116,
							0.5f, colAccent);
						draw_text(buf, font, "back to title...", 132, 132,
							0.45f, colDim);
					} else {
						snprintf(line, sizeof(line), "Score %d  Best %d",
							(int)score, best);
						draw_text(buf, font, line, 120, 116, 0.5f, colAccent);
						draw_text(buf, font, "back to title...", 132, 132,
							0.45f, colDim);
					}
				} else if (speed > 20.0f) {
					snprintf(line, sizeof(line), "BOOST %d",
						(int)((speed - 20.0f) * 5.0f));
					draw_text(buf, font, line, 300, 32, 0.45f,
						C2D_Color32(0xFF, 0x90, 0x30, 0xB0));
				}
			} else {
				draw_menu(buf, font);
			}
		}

		/* ----- schermo inferiore: aiuto + diagnostica ----- */
		C2D_TargetClear(bot, colBg);
		C2D_SceneBegin(bot);
		{
			char line[80];

			if (menu) {
				draw_text(buf, font, "NEON RUSH 3DS", 14, 12, 0.6f,
					colAccent);
				draw_text(buf, font, "LEFT/RIGHT    difficulty", 14, 44,
					0.5f, colWhite);
				draw_text(buf, font, "UP/DOWN       PLAY or DEMO", 14, 62,
					0.5f, colWhite);
				draw_text(buf, font, "A             start", 14, 80,
					0.5f, colWhite);
				draw_text(buf, font, "SELECT        music on/off", 14, 98,
					0.5f, colWhite);
				draw_text(buf, font, "START         exit to hbmenu", 14, 116,
					0.5f, colWhite);
				snprintf(line, sizeof(line), "%s  spd %.0f-%.0f  points x%.1f",
					D_NAME[g_diff], D_SPD0[g_diff] * 3.6f,
					D_SPDMAX[g_diff] * 3.6f, D_PTS[g_diff]);
				draw_text(buf, font, line, 14, 134, 0.45f, colDim);
			} else {
				draw_text(buf, font, "NEON RUSH 3DS - controls", 14, 12, 0.6f,
					colAccent);
				draw_text(buf, font, "D-Pad Left/Right ... change lane", 14, 40,
					0.5f, colWhite);
				draw_text(buf, font, "A / Up ........ jump (+ points)", 14,
					60, 0.5f, colWhite);
				draw_text(buf, font, "Pink walls .... dodge only, can't"
					" jump!", 14, 80, 0.5f, colWhite);
				draw_text(buf, font, "SELECT ........ music on/off", 14, 100,
					0.5f, colWhite);
				draw_text(buf, font, "START ......... exit to hbmenu", 14, 120,
					0.5f, colWhite);
				snprintf(line, sizeof(line), "Distance %dm   Best %dm",
					(int)dist, best);
				draw_text(buf, font, line, 14, 150, 0.5f, colDim);
				if (demo) {
					snprintf(line, sizeof(line),
						"DEMO: CPU plays   %s", D_NAME[g_diff]);
					draw_text(buf, font, line, 14, 170, 0.5f, colHint);
					draw_text(buf, font, "any button = title", 14, 182, 0.45f,
						colDim);
				} else {
					snprintf(line, sizeof(line), "Jump +%d   Clean +%d",
						(int)(3.0f * D_PTS[g_diff]),
						(int)(20.0f * D_PTS[g_diff]));
					draw_text(buf, font, line, 14, 170, 0.5f,
						C2D_Color32(0x80, 0xFF, 0x60, 0xFF));
				}
			}

			snprintf(line, sizeof(line), "3D depth %d%%  dropped: %d",
				(int)(slider * 100.0f), (int)ndspGetDroppedFrames());
			draw_text(buf, font, line, 14, 190, 0.5f, colDim);
			snprintf(line, sizeof(line), "Audio %s   Music %s",
				audio_ok() ? "OK" : "DSP missing",
				audio_music_on() ? "ON" : "OFF");
			draw_text(buf, font, line, 14, 210, 0.5f,
				audio_ok() ? colDim : C2D_Color32(0xFF, 0x80, 0x80, 0xFF));
			draw_text(buf, font, "Eye strain? Lower the 3D.", 14,
				226, 0.45f, C2D_Color32(0x60, 0x60, 0x80, 0xFF));
		}

		C3D_FrameEnd(0);
		gspWaitForVBlank();
	}

	if (!demo && (int)score > best) {
		best = (int)score;
		best_save();
	}
	audio_exit();
	C2D_TextBufDelete(buf);
	C2D_FontFree(font);
	texgen_exit();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
