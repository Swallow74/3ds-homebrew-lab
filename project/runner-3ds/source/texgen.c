#include "texgen.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static C3D_Tex            g_tex[TX_NUM];
static Tex3DS_SubTexture  g_sub[TX_NUM];
static C2D_Image          g_img[TX_NUM];
static bool               g_ready = false;

/* ------------------------------- canvas ---------------------------------- */
/* Piano di solo alpha: 255 = coperto.  RGB delle texture sara' bianco. */
typedef struct { unsigned char *a; int w, h; } Canvas;

static void putA(Canvas *c, int x, int y, float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	unsigned char v8 = (unsigned char)(v * 255.0f + 0.5f);
	unsigned char *p = &c->a[y * c->w + x];
	if (v8 > *p) *p = v8;
}

/* copertura anti-alias: d = distanza con segno dal bordo (negativo = dentro) */
static float cov(float d, float soft)
{
	float v = 1.0f - d / soft;
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float clampf(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* hash deterministica per le finestre dei palazzi */
static unsigned int h2(int x, int y)
{
	unsigned int v = (unsigned int)x * 374761393u + (unsigned int)y * 1252409279u;
	v ^= v >> 11;
	return v;
}

/* ------------------------------ le forme ---------------------------------- */

static void mk_dot(Canvas *c)
{
	float cx = c->w * 0.5f, cy = c->h * 0.5f, R = c->w * 0.5f - 0.5f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float a = 1.0f - sqrtf(dx * dx + dy * dy) / R;
			if (a < 0.0f) a = 0.0f;
			a = a * a * (0.55f + 0.45f * a);
			putA(c, x, y, a);
		}
}

static void mk_ring(Canvas *c)
{
	float cx = c->w * 0.5f, cy = c->h * 0.5f, R = c->w * 0.5f - 2.0f, T = 3.0f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d = sqrtf(dx * dx + dy * dy);
			float a = clampf(1.0f - fabsf(d - R) / T);
			a = a * a;
			/* leggero alone dentro l'anello */
			float inner = clampf(1.0f - d / R) * 0.18f;
			putA(c, x, y, a > inner ? a : inner);
		}
}

static void mk_stripe(Canvas *c)
{
	const float P = 12.0f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float ph = sinf((float)(x + y) * (M_PI / P));
			float a = clampf(ph * 2.4f);
			/* telaio piu' chiaro ai bordi, cosi' si legge anche su sfondi chiari */
			float dl = cov(2.0f - fminf(x + 0.5f, (float)c->w - 0.5f - x), 1.5f);
			float dt = cov(2.0f - fminf(y + 0.5f, (float)c->h - 0.5f - y), 1.5f);
			float fr = (dl > 0 || dt > 0) ? 0.85f : 0.0f;
			putA(c, x, y, a > fr ? a : fr);
		}
}

static void mk_panel(Canvas *c)
{
	int W = c->w, H = c->h;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float fx = fmodf(x + 0.5f, 8.0f); if (fx > 4.0f) fx = 8.0f - fx;
			float fy = fmodf(y + 0.5f, 8.0f); if (fy > 4.0f) fy = 8.0f - fy;
			float line = clampf(1.0f - fminf(fx, fy) / 1.25f) * 0.75f;

			float dEdge = fminf(fminf(x + 0.5f, (float)W - 0.5f - x),
			                    fminf(y + 0.5f, (float)H - 0.5f - y));
			float frame = clampf(1.0f - (3.0f - dEdge) / 1.6f) * 0.9f;
			/* telaio: solo la banda vicino al bordo, non il centro */
			if (dEdge > 3.0f) frame = 0.0f;

			/* bulloni nei quattro angoli (simmetria verticale rispettata) */
			float bolt = 0.0f;
			float corners[4][2] = {
				{6.0f, 6.0f}, {6.0f, (float)H - 6.0f},
				{(float)W - 6.0f, 6.0f}, {(float)W - 6.0f, (float)H - 6.0f},
			};
			for (int k = 0; k < 4; k++) {
				float dx = x + 0.5f - corners[k][0], dy = y + 0.5f - corners[k][1];
				float d = sqrtf(dx * dx + dy * dy);
				float b = clampf(1.0f - (d - 1.5f) / 1.2f);
				if (b > bolt) bolt = b;
			}

			float sheen = 0.10f * clampf(dEdge / 10.0f);
			putA(c, x, y, line + frame + bolt + sheen);
		}
}

static void mk_win(Canvas *c)
{
	int W = c->w, H = c->h;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int cx = x / 8, cy = y / 8;
			int ox = x % 8, oy = y % 8;
			float a = 0.0f;
			if (ox >= 1 && ox <= 5 && oy >= 2 && oy <= 6) {
				unsigned int r = h2(cx, cy) & 0xFF;
				if (r < 190) {                        /* finestra accesa */
					float lx = cov(0.6f - fminf(ox - 1, 5 - ox), 1.1f);
					float ly = cov(0.6f - fminf(oy - 2, 6 - oy), 1.1f);
					float lvl = 0.55f + (float)r / 500.0f;
					a = fminf(lx, ly) * lvl;
				}
			}
			putA(c, x, y, a);
		}
}

static void mk_star(Canvas *c)
{
	float cx = c->w * 0.5f, cy = c->h * 0.5f, R = c->w * 0.5f - 0.5f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float ax = fabsf(dx), ay = fabsf(dy);
			float d = sqrtf(dx * dx + dy * dy);
			float core = clampf(1.0f - d / (0.30f * R));
			core = core * core;
			float thin = fminf(ax, ay);
			float ray = clampf(1.0f - d / R) * clampf(1.0f - thin / (0.16f * R));
			ray = ray * ray;
			putA(c, x, y, core + ray * 0.9f);
		}
}

static void mk_shade(Canvas *c)
{
	float rx = c->w * 0.5f - 0.5f, ry = c->h * 0.5f - 0.5f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - c->w * 0.5f, dy = y + 0.5f - c->h * 0.5f;
			float q = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
			float a = clampf(1.0f - q);
			a = a * a * 0.85f;
			putA(c, x, y, a);
		}
}

static void mk_flame(Canvas *c)
{
	float rx = c->w * 0.5f - 0.5f, ry = c->h * 0.5f - 0.5f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - c->w * 0.5f, dy = y + 0.5f - c->h * 0.5f;
			/* lente: si restringe verso i due vertici (simmetrica) */
			float k = 1.0f - (dy * dy) / (ry * ry);
			if (k < 0.12f) k = 0.12f;
			float q = (dy * dy) / (ry * ry) + (dx * dx) / (rx * rx * k);
			float a = clampf(1.0f - q);
			a = a * a;
			float core = clampf(1.0f - q * 2.2f);
			putA(c, x, y, a * 0.75f + core * 0.5f);
		}
}

static void mk_mark(Canvas *c)
{
	float rx = c->w * 0.5f - 1.0f, ry = c->h * 0.5f - 1.0f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - c->w * 0.5f, dy = y + 0.5f - c->h * 0.5f;
			float d = fabsf(dx) / rx + fabsf(dy) / ry;   /* losanga */
			float band = clampf(1.0f - fabsf(d - 0.62f) / 0.20f);
			float fill = clampf(1.0f - d) * 0.22f;
			float a = band * band * 0.95f + fill;
			putA(c, x, y, a);
		}
}

static void mk_visor(Canvas *c)
{
	float rx = c->w * 0.5f - 1.0f, ry = c->h * 0.5f;
	for (int y = 0; y < c->h; y++)
		for (int x = 0; x < c->w; x++) {
			float dx = x + 0.5f - c->w * 0.5f, dy = y + 0.5f - c->h * 0.5f;
			float ay = clampf(1.0f - fabsf(dy) / (0.42f * ry));
			ay = ay * ay;
			float ax = clampf(1.0f - fabsf(dx) / rx);
			float a = ay * ax * (0.35f + 0.65f * ax);
			putA(c, x, y, a);
		}
}

/* piastre scafo: campo quasi pieno + linee di pannellatura + rivetti.
 * Le linee hanno alpha bassa: traspare la faccia del box sotto e la
 * pannellatura si legge "incisa". */
static void mk_hull(Canvas *c)
{
	int W = c->w, H = c->h;
	(void)W;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float lx = fmodf(x + 0.5f, 16.0f);
			float ly = fmodf(y + 0.5f, 16.0f);
			float a = 0.95f;
			if (lx < 1.6f || lx > 14.4f || ly < 1.6f || ly > 14.4f)
				a = 0.30f;
			putA(c, x, y, a);
		}
}

/* chevron di pericolo: bande diagonali alternate + cornice */
static void mk_chev(Canvas *c)
{
	int W = c->w, H = c->h;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float ph = fmodf((float)x - (float)y * 1.5f, 16.0f);
			if (ph < 0.0f) ph += 16.0f;
			float a = (ph < 8.0f) ? 1.0f : 0.40f;
			float dl = cov(1.5f - fminf(x + 0.5f, (float)W - 0.5f - x), 1.2f);
			float dt = cov(1.5f - fminf(y + 0.5f, (float)H - 0.5f - y), 1.2f);
			if (dl > 0.0f || dt > 0.0f) a = 0.95f;
			putA(c, x, y, a);
		}
}

/* paratia muro: campo medio + lame di luce verticali + traversa incisa */
static void mk_wallt(Canvas *c)
{
	int W = c->w, H = c->h;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float a = 0.55f;
			float sx = fmodf(x + 0.5f, 21.0f);
			if (sx < 3.5f) a = 1.0f;
			float my = fabsf(y + 0.5f - H * 0.5f);
			if (my < 1.4f && a > 0.35f) a = 0.30f;
			putA(c, x, y, a);
		}
}

/* ------------------------------ upload ------------------------------------- */

static bool upload(TexId id, int w, int h, Canvas *c)
{
	unsigned char *px = malloc((size_t)w * h * 4);
	if (!px) return false;
	for (int i = 0; i < w * h; i++) {
		px[i * 4 + 0] = 255;   /* R */
		px[i * 4 + 1] = 255;   /* G */
		px[i * 4 + 2] = 255;   /* B */
		px[i * 4 + 3] = c->a[i];
	}

	if (!C3D_TexInit(&g_tex[id], (u16)w, (u16)h, GPU_RGBA8)) {
		free(px);
		return false;
	}
	C3D_TexUpload(&g_tex[id], px);
	free(px);
	C3D_TexFlush(&g_tex[id]);            /* RAM di casa:flush di cache */
	C3D_TexSetFilter(&g_tex[id], GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&g_tex[id], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	/* stessa convenzione di tex3ds per le immagini 2D: dato non capovolto,
	 * top = 1 - y/H, bottom = 1 - (y+h)/H; top > bottom = nessuna rotazione */
	g_sub[id].width  = (u16)w;
	g_sub[id].height = (u16)h;
	g_sub[id].left   = 0.0f;
	g_sub[id].top    = 1.0f;
	g_sub[id].right  = 1.0f;
	g_sub[id].bottom = 0.0f;

	g_img[id].tex    = &g_tex[id];
	g_img[id].subtex = &g_sub[id];
	return true;
}

bool texgen_init(void)
{
	static const struct { int w, h; void (*fn)(Canvas *); } spec[TX_NUM] = {
		{32, 32, mk_dot},    {32, 32, mk_ring},   {32, 32, mk_stripe},
		{64, 64, mk_panel},  {32, 32, mk_win},    {16, 16, mk_star},
		{32, 16, mk_shade},  {16, 32, mk_flame},  {32, 32, mk_mark},
		{64, 16, mk_visor},  {64, 64, mk_hull},   {64, 32, mk_chev},
		{64, 64, mk_wallt},
	};

	for (int i = 0; i < TX_NUM; i++) {
		Canvas c;
		c.w = spec[i].w; c.h = spec[i].h;
		c.a = calloc((size_t)c.w * c.h, 1);
		if (!c.a) return false;
		spec[i].fn(&c);
		bool ok = upload((TexId)i, c.w, c.h, &c);
		free(c.a);
		if (!ok) return false;
	}
	g_ready = true;
	return true;
}

void texgen_exit(void)
{
	if (!g_ready) return;
	g_ready = false;
	for (int i = 0; i < TX_NUM; i++)
		C3D_TexDelete(&g_tex[i]);
}

C2D_Image texgen_get(TexId id)
{
	if ((int)id < 0 || id >= TX_NUM) id = TX_DOT;
	return g_img[id];
}
