/* Music: sequencer + sintetizzatore in tempo reale.  Vedi music.h.
 *
 * VOCI (22050 Hz, float, tabelle d'onda band-limited da 1024 punti):
 *   lead   dente di sega + quadra detunata, glide, vibrato ritardato
 *   bell   FM 2 operatori rapporto 3.5 (campana "DX"), indice che decade
 *   harm   seconda voce del ritornello: nota dell'accordo sotto la melodia
 *   arp    impulso 25% (o triangolo nel tema del titolo) a sedicesimi
 *   bass   FM rapporto 1 con indice che decade: il basso "slap" Mega Drive
 *   pad    3 note x 2 denti di sega detunati, L/R separati, passa-basso a
 *          2 poli con cutoff animato, "pompato" dalla cassa (sidechain)
 *   drums  cassa, rullante anni '80, charleston chiuso/aperto, piatto, tom
 *   eco    ping-pong a 3 sedicesimi con feedback filtrato (lead/bell/arp)
 *
 * SPARTITI: ogni sezione = accordi (una battuta ciascuno) + melodia in
 * notazione "E5 4 A5 4 ..." (nota, durata in sedicesimi; r = pausa).  Lo
 * stile della sezione decide basso, arpeggio, batteria e riempimenti.
 * Il brano di gioco: intro, strofa A, strofa B, ritornello, break con
 * campana e rullata, ritornello un tono sopra, poi di nuovo dalla strofa. */
#include "music.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifndef MUSIC_HOST
#include <3ds.h>
#endif

#define SRF     22050.0f
#define TBL     1024
#define TWO_PI  6.28318530718f
#define EBUF    12288                 /* eco: fino a ~0.55 s */

/* ------------------------------------------------------------ tabelle */
static float t_sin[TBL + 1], t_saw[TBL + 1], t_sqr[TBL + 1], t_pul[TBL + 1],
	t_tri[TBL + 1];
static float t_hz[128];

static float bl_saw(float ph, int nh)
{
	float v = 0.0f;
	for (int k = 1; k <= nh; k++) v += sinf(TWO_PI * (float)k * ph) / (float)k;
	return v;
}

static void tnorm(float *t)
{
	float m = 0.0f;
	for (int i = 0; i < TBL; i++) if (fabsf(t[i]) > m) m = fabsf(t[i]);
	for (int i = 0; i < TBL; i++) t[i] /= m;
	t[TBL] = t[0];
}

static void tables_init(void)
{
	for (int i = 0; i < TBL; i++) {
		float ph = (float)i / TBL;
		t_sin[i] = sinf(TWO_PI * ph);
		t_saw[i] = bl_saw(ph, 9);
		float q = 0.0f, tr = 0.0f, sg = 1.0f;
		for (int k = 1; k <= 11; k += 2) q += sinf(TWO_PI * k * ph) / k;
		for (int k = 1; k <= 9; k += 2, sg = -sg)
			tr += sg * sinf(TWO_PI * k * ph) / (float)(k * k);
		t_sqr[i] = q;
		t_tri[i] = tr;
		t_pul[i] = bl_saw(ph, 10) - bl_saw(ph + 0.25f, 10);
	}
	tnorm(t_sin); tnorm(t_saw); tnorm(t_sqr); tnorm(t_tri); tnorm(t_pul);
	for (int n = 0; n < 128; n++) t_hz[n] = 440.0f * powf(2.0f, (n - 69) / 12.0f);
}

static inline float wt(const float *t, float ph)
{
	float u = ph * TBL;
	int i = (int)u;
	float f = u - (float)i;
	return t[i] + (t[i + 1] - t[i]) * f;
}

static inline float wrapf(float p)
{
	p -= (float)(int)p;
	return p < 0.0f ? p + 1.0f : p;
}

static inline float adv(float p, float inc)
{
	p += inc;
	return p >= 1.0f ? p - 1.0f : p;
}

static inline float tc(float sec) { return expf(-1.0f / (sec * SRF)); }
static inline float lpc(float hz) { return 1.0f - expf(-TWO_PI * hz / SRF); }

static uint32_t s_rng = 0x1234567u;
static inline float noise(void)
{
	s_rng = s_rng * 1664525u + 1013904223u;
	return (float)(int32_t)s_rng * (1.0f / 2147483648.0f);
}

/* ------------------------------------------------------------ inviluppo */
typedef struct { float v, a, d, s, r; int st; } Env;   /* st: 0 off 1 A 2 D/S 3 R */

static void env_set(Env *e, float att, float dec, float sus, float rel)
{
	e->a = 1.0f / (att * SRF + 1.0f);
	e->d = 1.0f - tc(dec);
	e->s = sus;
	e->r = tc(rel);
}

static inline float env_tick(Env *e)
{
	switch (e->st) {
	case 1: e->v += e->a; if (e->v >= 1.0f) { e->v = 1.0f; e->st = 2; } break;
	case 2: e->v += (e->s - e->v) * e->d; break;
	case 3: e->v *= e->r; if (e->v < 1e-4f) { e->v = 0.0f; e->st = 0; } break;
	}
	return e->v;
}

/* ------------------------------------------------------------ voci */
enum { K_LEAD, K_BELL, K_PULSE, K_TRI, K_BASS };

typedef struct {
	int kind;
	float ph, ph2, inc, tinc, glide;
	Env e;
	int gate;
	float lp, cut, vt, vib, vel;
	float fm, fm0, fmFloor, fmK;
	float gain, pan, send;
} Voice;

static Voice vLead, vHarm, vArp, vBass;

static void voice_on(Voice *v, int midi, int gate, float vel)
{
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	v->tinc = t_hz[midi] / SRF;
	if (v->glide >= 1.0f || v->e.st == 0 || v->e.st == 3) v->inc = v->tinc;
	v->gate = gate > 1 ? gate : 1;
	v->vt = 0.0f;
	v->vel = vel;
	v->fm = v->fm0;
	if (v->kind == K_BELL || v->kind == K_BASS) v->ph2 = 0.0f;
	v->e.st = 1;
}

static inline void voice_off(Voice *v) { if (v->e.st) v->e.st = 3; }

static float voice_tick(Voice *v)
{
	if (!v->e.st) return 0.0f;
	if (v->gate > 0 && --v->gate == 0) voice_off(v);
	v->inc += (v->tinc - v->inc) * v->glide;
	float inc = v->inc, o;
	if (v->vib > 0.0f) {
		v->vt += 1.0f / SRF;
		if (v->vt > 0.18f) {
			float d = (v->vt - 0.18f) * 3.0f;
			if (d > 1.0f) d = 1.0f;
			inc *= 1.0f + d * v->vib * wt(t_sin, wrapf(v->vt * 5.6f));
		}
	}
	switch (v->kind) {
	case K_LEAD:
		v->ph = adv(v->ph, inc);
		v->ph2 = adv(v->ph2, inc * 1.0046f);
		o = 0.62f * wt(t_saw, v->ph) + 0.45f * wt(t_sqr, v->ph2);
		break;
	case K_BELL:
		v->ph = adv(v->ph, inc);
		v->ph2 = wrapf(v->ph2 + inc * 3.5f);
		o = wt(t_sin, wrapf(v->ph + v->fm * 0.159f * wt(t_sin, v->ph2)));
		v->fm *= v->fmK;
		break;
	case K_PULSE:
		v->ph = adv(v->ph, inc);
		o = 0.8f * wt(t_pul, v->ph);
		break;
	case K_TRI:
		v->ph = adv(v->ph, inc);
		o = wt(t_tri, v->ph);
		break;
	default: /* K_BASS */
		v->ph = adv(v->ph, inc);
		o = 0.8f * wt(t_sin, wrapf(v->ph + v->fm * 0.159f * wt(t_sin, v->ph))) +
			0.3f * wt(t_sin, v->ph);
		v->fm = v->fmFloor + (v->fm - v->fmFloor) * v->fmK;
		break;
	}
	v->lp += (o - v->lp) * v->cut;
	return v->lp * env_tick(&v->e) * v->vel;
}

/* pad: 3 note x 2 oscillatori (pari a sinistra, dispari a destra) */
static struct {
	float ph[6], inc[6];
	float l1, l2, r1, r2, cut, tcut, gain;
	Env e;
} pad;

/* batteria */
static struct { float ph, fe, fed, e, ed, c, cd; } kk;
static struct { float e, ed, t, td, ph, lp; } sn;
static struct { float e, ed, prev; } hh;
static struct { float e, ed, prev; } cr;
static struct { float ph, f0, fe, fed, e, ed; } tm;

/* eco ping-pong */
static float s_eL[EBUF], s_eR[EBUF], s_elp;
static int s_epos, s_edel = 6615;

/* ------------------------------------------------------------ spartiti */
enum { SY_INTRO, SY_VERSE, SY_VERSE2, SY_CHORUS, SY_BREAK, SY_TAMB, SY_TBEAT,
	SY_OVER };

typedef struct { const char *chords, *mel; int style, tr; } Sect;
typedef struct { const Sect *sec; int nsec, loop; float bpm; } Song;

/* ---- brano di gioco: 132 BPM (accelera col livello), Mi minore ----
 * Freddo e "spaziale" come il pozzo verde nel buio: strofa che sale per
 * arpeggi, seconda strofa in La minore, ritornello ampio, break con la
 * campana e ritornello un tono sopra prima di ripartire dalla strofa. */
static const char MEL_A[] =
	"E5 4 G5 4 B5 4 A5 2 G5 2 | E5 6 G5 2 E5 4 C5 4 |"
	"D5 4 G5 4 B5 4 D6 4 | C6 6 B5 2 A5 8 |"
	"E5 4 G5 4 B5 2 C6 2 B5 2 G5 2 | E6 6 C6 2 G5 4 C6 4 |"
	"A5 4 C6 4 E6 2 D6 2 C6 2 A5 2 | B5 8 D#6 4 F#6 4";
static const char MEL_B[] =
	"A5 4 G5 2 E5 2 C5 4 E5 4 | G5 6 E5 2 B4 8 |"
	"C5 4 E5 4 G5 4 F5 2 E5 2 | D5 8 B4 4 G4 4 |"
	"A4 2 C5 2 E5 4 A5 2 G5 2 E5 2 C5 2 | B4 4 E5 4 G5 6 F#5 2 |"
	"F5 8 A5 4 C6 4 | B5 8 A5 2 F#5 2 D#5 2 F#5 2";
static const char MEL_C[] =
	"G5 6 E6 6 D6 4 | F#5 6 D6 6 C6 4 | B5 4 D6 4 F#6 4 E6 2 D6 2 |"
	"E6 12 D6 2 B5 2 | C6 6 E6 6 G6 4 | F#6 6 E6 6 D6 4 |"
	"D#6 4 F#6 4 B6 4 A6 4 | G6 4 F#6 4 E6 8";
static const char MEL_BRK[] =
	"E5 8 B5 8 | C6 8 G5 8 | A5 8 E5 8 | F#5 8 D#5 8";

static const Sect RUN[] = {
	{ "Em Em C D",              NULL,    SY_INTRO,  0 },
	{ "Em C G D Em C Am B",     MEL_A,   SY_VERSE,  0 },
	{ "Am Em C G Am Em F B",    MEL_B,   SY_VERSE2, 0 },
	{ "C D Bm Em C D B Em",     MEL_C,   SY_CHORUS, 0 },
	{ "Em C Am B",              MEL_BRK, SY_BREAK,  0 },
	{ "C D Bm Em C D B Em",     MEL_C,   SY_CHORUS, 2 },   /* un tono sopra */
};

/* ---- tema del menu: 92 BPM, Re minore, campana su arpeggi ---- */
static const char MEL_T1[] =
	"D5 8 F5 4 A5 4 | Bb5 12 A5 4 | A5 8 C6 4 F6 4 | E6 8 D6 4 C6 4 |"
	"D6 12 C6 2 A5 2 | Bb5 8 D6 4 F6 4 | E6 8 G6 8 | C#6 8 E6 4 A5 4";
static const char MEL_T2[] =
	"Bb5 8 A5 4 G5 4 | A5 8 F5 4 D5 4 | F5 8 Bb5 4 D6 4 | C6 16 |"
	"Bb5 8 A5 4 G5 4 | F5 8 A5 4 D6 4 | C#6 8 E6 4 A6 4 | A6 16";

static const Sect TITLE[] = {
	{ "Dm Bb F C Dm Bb Gm A",   NULL,    SY_TAMB,  0 },
	{ "Dm Bb F C Dm Bb Gm A",   MEL_T1,  SY_TBEAT, 0 },
	{ "Gm Dm Bb F Gm Dm A A",   MEL_T2,  SY_TBEAT, 0 },
};

/* ---- game over: cadenza discendente e silenzio ---- */
static const Sect OVER[] = {
	{ "Bb A Dm Dm", "F6 4 D6 4 Bb5 4 F5 4 | E6 4 C#6 4 A5 4 E5 4 | D5 16 | r 16",
	  SY_OVER, 0 },
};

static const Song SONGS[MUS_NUM] = {
	{ TITLE, 3, 0,  92.0f },
	{ RUN,   6, 1,  132.0f },
	{ OVER,  1, -1, 96.0f },
};

/* spartito espanso: un record per sedicesimo */
typedef struct {
	uint8_t pc, minor, style, s, bar, nbar;
	uint8_t lead, llen;        /* lead: 0 niente, 1 pausa, altrimenti MIDI */
} Step;

#define MAXSTEP 768
static Step s_seq[MUS_NUM][MAXSTEP];
static int s_nstep[MUS_NUM], s_loopStep[MUS_NUM];
static int s_err;

static int pc_of(char c)
{
	static const int P[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A..G */
	return (c >= 'A' && c <= 'G') ? P[c - 'A'] : -1;
}

static void compile_song(int id)
{
	const Song *sg = &SONGS[id];
	int n = 0;
	for (int k = 0; k < sg->nsec; k++) {
		const Sect *sc = &sg->sec[k];
		if (k == sg->loop) s_loopStep[id] = n;
		uint8_t pcs[16], mins[16];
		int nb = 0;
		for (const char *p = sc->chords; *p && nb < 16;) {
			if (*p == ' ') { p++; continue; }
			int pc = pc_of(*p++);
			if (pc < 0) { s_err++; break; }
			if (*p == '#') { pc++; p++; }
			else if (*p == 'b') { pc--; p++; }
			int mi = 0;
			if (*p == 'm') { mi = 1; p++; }
			pcs[nb] = (uint8_t)((pc + sc->tr + 24) % 12);
			mins[nb++] = (uint8_t)mi;
		}
		int start = n;
		for (int b = 0; b < nb; b++)
			for (int s = 0; s < 16; s++) {
				if (n >= MAXSTEP) { s_err++; return; }
				Step st = { pcs[b], mins[b], (uint8_t)sc->style, (uint8_t)s,
					(uint8_t)b, (uint8_t)nb, 0, 0 };
				s_seq[id][n++] = st;
			}
		if (sc->mel) {
			int pos = start;
			const char *p = sc->mel;
			while (*p) {
				if (*p == ' ' || *p == '|') { p++; continue; }
				int note;
				if (*p == 'r') { note = 1; p++; }
				else {
					int pc = pc_of(*p++);
					if (pc < 0) { s_err++; break; }
					if (*p == '#') { pc++; p++; }
					else if (*p == 'b') { pc--; p++; }
					int oct = 0;
					while (*p >= '0' && *p <= '9') oct = oct * 10 + (*p++ - '0');
					note = (oct + 1) * 12 + pc + sc->tr;
				}
				while (*p == ' ') p++;
				int len = 0;
				while (*p >= '0' && *p <= '9') len = len * 10 + (*p++ - '0');
				if (len <= 0 || pos >= n) { s_err++; break; }
				s_seq[id][pos].lead = (uint8_t)note;
				s_seq[id][pos].llen = (uint8_t)len;
				pos += len;
			}
			if (pos != n) s_err++;
		}
	}
	s_nstep[id] = n;
}

int music_check(void) { return s_err; }

/* ------------------------------------------------------------ sequencer */
static int s_cur = -1, s_pos;
static bool s_playing;
static float s_ctr, s_stepLen = 2205.0f;
static float s_fade;

static volatile int  s_req = -1;     /* -1 = silenzio */
static volatile bool s_on = true, s_duckOn = false;
static volatile float s_tempo = 1.0f;

static void cfg_lead(Voice *v, int kind, float gain, float pan, float send)
{
	v->kind = kind;
	v->gain = gain; v->pan = pan; v->send = send;
	if (kind == K_BELL) {
		env_set(&v->e, 0.002f, 0.55f, 0.22f, 0.40f);
		v->glide = 1.0f; v->cut = 0.95f; v->vib = 0.0f;
		v->fm0 = 2.4f; v->fmK = tc(0.30f);
	} else {
		env_set(&v->e, 0.006f, 0.25f, 0.72f, 0.09f);
		v->glide = 1.0f - tc(0.022f); v->cut = lpc(3400.0f); v->vib = 0.011f;
	}
}

static void cfg_arp(int style)
{
	Voice *v = &vArp;
	bool title = style == SY_TAMB || style == SY_TBEAT;
	v->kind = title ? K_TRI : K_PULSE;
	env_set(&v->e, 0.002f, title ? 0.20f : 0.07f, title ? 0.35f : 0.25f, 0.05f);
	v->glide = 1.0f; v->vib = 0.0f;
	v->cut = lpc(title ? 5000.0f : 2600.0f);
	v->pan = 0.40f;
	v->send = title ? 0.45f : (style == SY_BREAK ? 0.40f : 0.22f);
	v->gain = title ? 0.11f : (style == SY_BREAK ? 0.10f :
		(style == SY_CHORUS ? 0.075f : 0.06f));
}

static void cfg_bass(int style)
{
	Voice *v = &vBass;
	bool soft = style == SY_TAMB || style == SY_TBEAT || style == SY_OVER ||
		style == SY_BREAK;
	v->kind = K_BASS;
	env_set(&v->e, 0.003f, 0.20f, 0.65f, soft ? 0.25f : 0.04f);
	v->glide = 1.0f; v->vib = 0.0f; v->pan = 0.0f; v->send = 0.0f;
	v->cut = lpc(soft ? 900.0f : 2200.0f);
	v->fm0 = soft ? 1.2f : 3.2f;
	v->fmFloor = soft ? 0.35f : 0.8f;
	v->fmK = tc(soft ? 0.15f : 0.06f);
	v->gain = soft ? 0.26f : 0.30f;
}

static void drums_init(void)
{
	kk.fed = tc(0.030f); kk.ed = tc(0.17f); kk.cd = tc(0.003f);
	sn.ed = tc(0.13f); sn.td = tc(0.045f);
	cr.ed = tc(0.90f);
	tm.fed = tc(0.09f); tm.ed = tc(0.18f);
}

static void kick(float v)  { kk.fe = 1.0f; kk.e = v; kk.c = v; }
static void snare(float v) { sn.e = v; sn.t = v; }
static void hat(float v, bool open) { hh.e = v; hh.ed = tc(open ? 0.13f : 0.022f); }
static void crash(float v) { cr.e = v; }
static void tom(float hz, float v) { tm.f0 = hz; tm.fe = 1.0f; tm.e = v; }

static void pad_chord(const Step *st)
{
	int root = 55 + (st->pc - 7 + 12) % 12;
	int n3[3] = { root, root + (st->minor ? 3 : 4), root + 7 };
	for (int k = 0; k < 3; k++) {
		float f = t_hz[n3[k]] / SRF;
		pad.inc[2 * k] = f * 0.9965f;
		pad.inc[2 * k + 1] = f * 1.0035f;
	}
	if (pad.e.st != 1 && pad.e.st != 2) pad.e.st = 1;
}

static void trigger(const Step *st)
{
	const int s = st->s, sty = st->style, bar = st->bar;
	const bool last = bar == st->nbar - 1;
	const bool secStart = bar == 0 && s == 0;
	const int third = st->minor ? 3 : 4;
	const float prog = (float)(bar * 16 + s) / (float)(st->nbar * 16);
	const int stepN = (int)s_stepLen;

	/* configurazione degli strumenti a inizio sezione */
	if (secStart) {
		cfg_arp(sty);
		cfg_bass(sty);
		bool bell = sty == SY_BREAK || sty == SY_TAMB || sty == SY_TBEAT ||
			sty == SY_OVER;
		cfg_lead(&vLead, bell ? K_BELL : K_LEAD, bell ? 0.24f : 0.20f, -0.15f,
			bell ? 0.45f : 0.28f);
		cfg_lead(&vHarm, K_LEAD, 0.085f, 0.55f, 0.20f);
		vHarm.vib = 0.008f;
		static const float PADG[] = { 0.10f, 0.10f, 0.10f, 0.12f, 0.13f, 0.15f,
			0.13f, 0.14f };
		static const float PADC[] = { 1100.0f, 1500.0f, 1500.0f, 2100.0f, 300.0f,
			800.0f, 1000.0f, 1100.0f };
		pad.gain = PADG[sty];
		pad.tcut = lpc(PADC[sty]);
	}
	if (sty == SY_BREAK)       /* filtro che si apre per tutto il break */
		pad.tcut = lpc(250.0f + 2900.0f * prog * prog);
	if (s == 0) pad_chord(st);

	/* melodia + seconda voce del ritornello */
	if (st->lead == 1) { voice_off(&vLead); voice_off(&vHarm); }
	else if (st->lead) {
		int gate = (int)(st->llen * s_stepLen * 0.94f);
		voice_on(&vLead, st->lead, gate, 1.0f);
		if (sty == SY_CHORUS) {
			int h = st->lead - 3;
			for (; h > st->lead - 10; h--) {
				int d = (h - st->pc + 120) % 12;
				if (d == 0 || d == third || d == 7) break;
			}
			voice_on(&vHarm, h, gate, 1.0f);
		}
	}

	/* basso */
	int b = 40 + (st->pc - 4 + 12) % 12;
	switch (sty) {
	case SY_INTRO:
		if (!(s & 1)) voice_on(&vBass, (bar >= 2 && (s & 2)) ? b + 12 : b,
			stepN * 11 / 10, 0.9f);
		break;
	case SY_VERSE:
		if (!(s & 1)) voice_on(&vBass, (s & 2) ? b + 12 : b, stepN * 6 / 5, 1.0f);
		break;
	case SY_VERSE2:
		voice_on(&vBass, (s & 3) == 2 ? b + 12 : b, stepN * 7 / 10,
			(s & 1) ? 0.75f : 1.0f);
		break;
	case SY_CHORUS:
		if ((s & 3) != 1) voice_on(&vBass, (s & 3) == 2 ? b + 12 : b,
			stepN * 8 / 10, (s & 3) == 0 ? 1.0f : 0.8f);
		break;
	case SY_BREAK:
		if (last) { if (!(s & 1)) voice_on(&vBass, b, stepN, 0.9f); }
		else if (s == 0) voice_on(&vBass, b, stepN * 15, 0.9f);
		break;
	case SY_TBEAT:
		if (s == 0)  voice_on(&vBass, b, stepN * 5, 0.9f);
		if (s == 6)  voice_on(&vBass, b, stepN * 3, 0.7f);
		if (s == 10) voice_on(&vBass, b + 12, stepN * 3, 0.7f);
		if (s == 14) voice_on(&vBass, b, stepN * 2, 0.6f);
		break;
	default:          /* SY_TAMB, SY_OVER */
		if (s == 0) voice_on(&vBass, b, stepN * 15, 0.8f);
		break;
	}

	/* arpeggio sulle note dell'accordo */
	if (sty != SY_OVER) {
		const int tones[7] = { 0, third, 7, 12, 12 + third, 19, 24 };
		static const uint8_t PI_[] = { 0, 1, 2, 3 };
		static const uint8_t PV[] = { 0, 1, 2, 3, 4, 3, 2, 1 };
		static const uint8_t PV2[] = { 4, 2, 3, 1, 2, 0, 1, 2 };
		static const uint8_t PC[] = { 0, 2, 4, 6, 5, 3, 4, 2 };
		static const uint8_t PB[] = { 0, 1, 2, 3, 4, 5, 6, 5 };
		static const uint8_t PT[] = { 0, 2, 1, 3, 2, 4, 3, 5 };
		int a = 57 + (st->pc - 9 + 12) % 12, idx = -1;
		float vel = 1.0f;
		switch (sty) {
		case SY_INTRO:  idx = PI_[s & 3]; vel = 0.45f + 0.55f * prog; break;
		case SY_VERSE:  idx = PV[s & 7]; break;
		case SY_VERSE2: idx = PV2[s & 7]; break;
		case SY_CHORUS: idx = PC[s & 7]; break;
		case SY_BREAK:  idx = PB[s & 7]; break;
		default:        if (!(s & 1)) { idx = PT[(s >> 1) & 7]; a += 12; } break;
		}
		if (idx >= 0) {
			if (!(s & 3)) vel *= 1.15f;
			bool title = sty == SY_TAMB || sty == SY_TBEAT;
			voice_on(&vArp, a + tones[idx], (int)(s_stepLen * (title ? 1.6f : 0.55f)),
				vel);
		}
	}

	/* batteria */
	bool fill = last && sty != SY_BREAK && sty != SY_TAMB && sty != SY_OVER;
	switch (sty) {
	case SY_INTRO:
		if (bar >= 1 && !(s & 3) && !(last && s >= 8)) kick(1.0f);
		if ((s & 3) == 2) hat(0.6f, false);
		else if (bar >= 1 && (s & 1)) hat(0.25f, false);
		if (last) {
			if (s < 8 && !(s & 1)) snare(0.3f + 0.04f * s);
			if (s >= 8) snare(0.45f + 0.07f * (s - 8));
		}
		break;
	case SY_VERSE:
	case SY_VERSE2:
	case SY_CHORUS: {
		bool ch = sty == SY_CHORUS;
		if (secStart || (ch && s == 0 && (bar & 3) == 0)) crash(0.9f);
		if (!(s & 3)) kick(1.0f);
		if (fill && ch) {
			if (s == 4) snare(0.9f);
			if (s >= 8) snare(0.4f + 0.075f * (s - 8));
		} else if (fill) {
			if (s == 4) snare(0.9f);
			if (s >= 12) tom(210.0f - 30.0f * (s - 12), 0.9f);
		} else {
			if (s == 4 || s == 12) snare(0.9f);
			if (ch && s == 15) snare(0.22f);
		}
		bool open = sty == SY_VERSE2 ? (s & 3) == 2 :
			(s == 14 && (bar & 1)) || (ch && s == 6);
		float hv = (s & 3) == 2 ? 0.7f : ((s & 1) ? 0.25f : 0.45f);
		if (ch) hv += 0.1f;
		if (!(fill && s >= 12)) hat(open ? 0.5f : hv, open);
		break;
	}
	case SY_BREAK:
		if (bar >= 1 && (s & 3) == 2) hat(0.3f, false);
		if (last) {
			if (!(s & 3)) kick(0.9f);
			snare(0.12f + 0.058f * s);
		}
		break;
	case SY_TBEAT:
		if (secStart) crash(0.5f);
		if (s == 0) kick(0.9f);
		if (s == 10) kick(0.7f);
		if (s == 8) snare(0.8f);
		if (!(s & 1)) hat((s & 3) == 2 ? 0.42f : 0.28f, false);
		if (fill && (s == 12 || s == 14)) tom(s == 12 ? 190.0f : 140.0f, 0.7f);
		break;
	case SY_OVER:
		if (secStart) crash(0.6f);
		break;
	default:
		break;
	}
}

static void song_start(int id)
{
	s_cur = id;
	s_pos = 0;
	s_ctr = 0.0f;
	s_playing = id >= 0 && id < MUS_NUM && s_nstep[id] > 0;
	if (!s_playing) return;
	s_stepLen = SRF * 15.0f / SONGS[id].bpm;
	int d = (int)(s_stepLen * 3.0f);
	s_edel = d < EBUF ? d : EBUF - 1;
	if (s_epos >= s_edel) s_epos = 0;
	vLead.e.st = vHarm.e.st = vArp.e.st = vBass.e.st = 0;
	vLead.e.v = vHarm.e.v = vArp.e.v = vBass.e.v = 0.0f;
	pad.e.st = 0; pad.e.v = 0.0f;
}

static inline float panL(float p) { return p > 0.0f ? 1.0f - p : 1.0f; }
static inline float panR(float p) { return p < 0.0f ? 1.0f + p : 1.0f; }

static inline float soft_clip(float x)
{
	if (x > 3.0f) return 1.0f;
	if (x < -3.0f) return -1.0f;
	float x2 = x * x;
	return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void music_synth_init(void)
{
	tables_init();
	drums_init();
	env_set(&pad.e, 0.15f, 1.2f, 0.85f, 0.5f);
	pad.cut = pad.tcut = lpc(1200.0f);
	s_err = 0;
	for (int i = 0; i < MUS_NUM; i++) compile_song(i);
	memset(s_eL, 0, sizeof(s_eL));
	memset(s_eR, 0, sizeof(s_eR));
	s_cur = -1;
	s_fade = 0.0f;
}

void music_render(int16_t *out, int frames)
{
	const int req = s_req;
	const bool on = s_on;
	const float duck = s_duckOn ? 0.35f : 1.0f;

	for (int i = 0; i < frames; i++) {
		float tgt = (req != s_cur || !on) ? 0.0f : duck;
		s_fade += (tgt - s_fade) * 0.0025f;
		if (req != s_cur && s_fade < 0.003f) song_start(req);
		if (!on && s_fade < 0.0005f) { out[2 * i] = out[2 * i + 1] = 0; continue; }

		if (s_playing && (s_ctr -= 1.0f) <= 0.0f) {
			s_ctr += s_stepLen / s_tempo;
			trigger(&s_seq[s_cur][s_pos]);
			if (++s_pos >= s_nstep[s_cur]) {
				if (SONGS[s_cur].loop >= 0) s_pos = s_loopStep[s_cur];
				else {
					s_playing = false;
					voice_off(&vLead); voice_off(&vHarm); voice_off(&vArp);
					voice_off(&vBass);
					if (pad.e.st) pad.e.st = 3;
				}
			}
		}

		float L = 0.0f, R = 0.0f, send = 0.0f, x;

		/* voci melodiche */
		Voice *vs[4] = { &vLead, &vHarm, &vArp, &vBass };
		for (int k = 0; k < 4; k++) {
			Voice *v = vs[k];
			if (!v->e.st) continue;
			x = voice_tick(v) * v->gain;
			L += x * panL(v->pan);
			R += x * panR(v->pan);
			send += x * v->send;
		}

		/* batteria */
		float kenv = 0.0f;
		if (kk.e > 1e-4f) {
			kk.ph = adv(kk.ph, (45.0f + 120.0f * kk.fe) / SRF);
			x = (wt(t_sin, kk.ph) * kk.e + noise() * kk.c * 0.35f) * 0.62f;
			L += x; R += x;
			kenv = kk.e;
			kk.fe *= kk.fed; kk.e *= kk.ed; kk.c *= kk.cd;
		}
		if (sn.e > 1e-4f) {
			float n = noise();
			sn.lp += (n - sn.lp) * 0.28f;
			sn.ph = adv(sn.ph, 185.0f / SRF);
			x = ((n - sn.lp) * sn.e + wt(t_sin, sn.ph) * sn.t * 0.7f) * 0.30f;
			L += x * 0.95f; R += x;
			sn.e *= sn.ed; sn.t *= sn.td;
		}
		if (hh.e > 1e-4f) {
			float n = noise();
			x = (n - hh.prev) * hh.e * 0.055f;
			hh.prev = n;
			L += x * 0.75f; R += x;
			hh.e *= hh.ed;
		}
		if (cr.e > 1e-4f) {
			float n = noise();
			x = (n - cr.prev) * cr.e * 0.075f;
			cr.prev = n;
			L += x; R += x * 0.8f;
			cr.e *= cr.ed;
		}
		if (tm.e > 1e-4f) {
			tm.ph = adv(tm.ph, tm.f0 * (0.62f + 0.38f * tm.fe) / SRF);
			x = wt(t_sin, tm.ph) * tm.e * 0.40f;
			L += x * 1.0f; R += x * 0.8f;
			tm.fe *= tm.fed; tm.e *= tm.ed;
		}

		/* pad stereo con passa-basso a 2 poli e sidechain dalla cassa */
		if (pad.e.st) {
			float sl = 0.0f, sr = 0.0f;
			for (int k = 0; k < 6; k += 2) {
				pad.ph[k] = adv(pad.ph[k], pad.inc[k]);
				pad.ph[k + 1] = adv(pad.ph[k + 1], pad.inc[k + 1]);
				sl += wt(t_saw, pad.ph[k]);
				sr += wt(t_saw, pad.ph[k + 1]);
			}
			pad.cut += (pad.tcut - pad.cut) * 0.0005f;
			pad.l1 += (sl - pad.l1) * pad.cut; pad.l2 += (pad.l1 - pad.l2) * pad.cut;
			pad.r1 += (sr - pad.r1) * pad.cut; pad.r2 += (pad.r1 - pad.r2) * pad.cut;
			float g = env_tick(&pad.e) * pad.gain * (1.0f - 0.55f * kenv) / 3.0f;
			L += pad.l2 * g;
			R += pad.r2 * g;
		}

		/* eco ping-pong con feedback filtrato */
		float dl = s_eL[s_epos], dr = s_eR[s_epos];
		s_elp += (dr - s_elp) * 0.35f;
		s_eL[s_epos] = send + s_elp * 0.40f;
		s_eR[s_epos] = dl * 0.40f;
		if (++s_epos >= s_edel) s_epos = 0;
		L += dl * 0.55f;
		R += dr * 0.55f;

		float g = s_fade * 0.95f;
		out[2 * i]     = (int16_t)(soft_clip(L * g) * 30000.0f);
		out[2 * i + 1] = (int16_t)(soft_clip(R * g) * 30000.0f);
	}
}

void music_play(int song)  { if (song >= -1 && song < MUS_NUM) s_req = song; }
void music_enable(bool on) { s_on = on; }
void music_duck(bool on)   { s_duckOn = on; }
void music_tempo(float mul)
{
	if (mul < 0.5f) mul = 0.5f;
	if (mul > 2.0f) mul = 2.0f;
	s_tempo = mul;
}

/* ------------------------------------------------------------ NDSP (3DS) */
#ifndef MUSIC_HOST
#define MB_N  3
#define MB_FR 1024                    /* ~46 ms per buffer */

static int16_t    *s_mb[MB_N];
static ndspWaveBuf s_wb[MB_N];
static int         s_next, s_ch = -1;
static Thread      s_thr;
static LightEvent  s_ev;
static volatile bool s_quit;

static void fill_ready(void)
{
	for (int k = 0; k < MB_N; k++) {
		ndspWaveBuf *w = &s_wb[s_next];
		if (w->status == NDSP_WBUF_QUEUED || w->status == NDSP_WBUF_PLAYING) break;
		music_render(s_mb[s_next], MB_FR);
		memset(w, 0, sizeof(*w));        /* status = FREE: vedi AGENTS.md */
		w->data_pcm16 = s_mb[s_next];
		w->nsamples = MB_FR;
		DSP_FlushDataCache(s_mb[s_next], MB_FR * 2 * sizeof(int16_t));
		ndspChnWaveBufAdd(s_ch, w);
		s_next = (s_next + 1) % MB_N;
	}
}

static void ndsp_cb(void *d) { (void)d; LightEvent_Signal(&s_ev); }

static void thr_main(void *arg)
{
	(void)arg;
	while (!s_quit) {
		fill_ready();
		LightEvent_Wait(&s_ev);
	}
}

bool music_init(int channel)
{
	music_synth_init();
	for (int k = 0; k < MB_N; k++) {
		s_mb[k] = linearMemAlign(MB_FR * 2 * sizeof(int16_t), 0x40);
		if (!s_mb[k]) { music_exit(); return false; }
		memset(s_mb[k], 0, MB_FR * 2 * sizeof(int16_t));
		memset(&s_wb[k], 0, sizeof(ndspWaveBuf));
	}
	s_ch = channel;
	s_next = 0;
	s_quit = false;
	LightEvent_Init(&s_ev, RESET_ONESHOT);
	ndspSetCallback(ndsp_cb, NULL);

	/* priorita' appena sopra il thread principale, stesso core: il mixer
	 * non resta mai senza dati anche se un frame di gioco va lungo */
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	prio -= 1;
	if (prio < 0x18) prio = 0x18;
	s_thr = threadCreate(thr_main, NULL, 16 * 1024, prio, -2, false);
	if (!s_thr) { music_exit(); return false; }
	return true;
}

void music_exit(void)
{
	if (s_thr) {
		s_quit = true;
		LightEvent_Signal(&s_ev);
		threadJoin(s_thr, U64_MAX);
		threadFree(s_thr);
		s_thr = NULL;
	}
	ndspSetCallback(NULL, NULL);
	if (s_ch >= 0) ndspChnWaveBufClear(s_ch);
	for (int k = 0; k < MB_N; k++)
		if (s_mb[k]) { linearFree(s_mb[k]); s_mb[k] = NULL; }
	s_ch = -1;
}
#endif
