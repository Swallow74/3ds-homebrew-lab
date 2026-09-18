/*
  Audio sintetizzato via NDSP: la stessa "agenda" di eventi di
  SoundManager.cpp di BlockOut II 2.5 (GPL).  L'originale riproduce file
  .wav/.mod con SDL_mixer; sul 3DS le risorse non esistono, quindi gli
  effetti sono sintetizzati (stessi eventi, stessi nomi).

  Synth/NDSP di partenza: runner-3ds/source/audio.c +
  devkitPro/3ds-examples/audio/streaming.
*/

#include "audio.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>


#define SR            22050
#define NSFX          5                     /* 0..3 effetti, 4 musica */
#define MUSIC_CH      4
#define SFX_MAX       ((size_t)(SR * 0.45)) /* frames disponibili per effetto */
#define MUSIC_MAX     ((size_t)(SR * 16.0)) /* loop di 16 s: 8 battute a 120 bpm */

static bool g_ok = false;
static bool g_on = false;   /* SoundManager::SetEnable */

static s16        *g_sfxBuf[NSFX];
static s16        *g_musicBuf = NULL;
static ndspWaveBuf g_sbuf[NSFX];
static int         g_sfxNext = 0;

static float s_mixStereo[12];

/* ------------------------------------------------------------------ osc */

#define OS_C 4096
static double g_sin[OS_C + 1];

static void osc_init(void)
{
	for (int i = 0; i <= OS_C; i++)
		g_sin[i] = sin((double)i * (2.0 * M_PI / (double)OS_C));
}

static double osc(double ph)
{
	double u = ph * (double)OS_C;
	int i = (int)u;
	double f = u - (double)i;
	return g_sin[i] + (g_sin[i + 1] - g_sin[i]) * f;
}

/* ------------------------------------------------------------------ misc */

static int clamp_s(double v)
{
	return v > 32767.0 ? 32767 : (v < -32768.0 ? -32768 : (int)v);
}

/* Accumula un campione in un buffer PCM16 stereo interleaved.
 * 'st' punta al campione sinistro; il destro e' subito dopo. */
static void mix_pan(s16 *st, double v, double gl, double gr)
{
	int l = (int)st[0] + clamp_s(v * gl);
	int r = (int)st[1] + clamp_s(v * gr);
	st[0] = (s16)(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
	st[1] = (s16)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
}

/* guadagni di pan: spostano il canale senza cambiare il volume percepito */
static inline double gainL(double vol, double pan) { return vol * (1.0 - 0.35 * pan); }
static inline double gainR(double vol, double pan) { return vol * (1.0 + 0.35 * pan); }

/* Tono "chiptune": armoniche controllate + inviluppo attack/decay/release.
 * kind: 0 = lead (3 armoniche), 1 = basso, 2 = quadra morbida. */
static void add_tone(s16 *st, size_t total, size_t start, double hz,
                     size_t len, double vol, double att, double rel,
                     int kind, double pan)
{
	size_t end = start + len;
	if (end > total) end = total;
	if (hz < 25.0) hz = 25.0;

	const double dph = hz / (double)SR;
	const double gl = gainL(vol, pan);
	const double gr = gainR(vol, pan);
	const double dsl = exp(-8.5 / (double)SR); /* decay "a corda" per campione */

	double ph = 0.0, dec = 1.0;

	for (size_t i = start; i < end; i++)
	{
		double t  = (double)(i - start) / (double)SR;
		double e  = 1.0;
		if (t < att) e = t / att;
		double left = (double)(end - i) / (double)SR;
		if (left < rel) e *= left / rel;

		ph += dph;
		if (ph >= 1.0) ph -= 1.0;
		double p2 = ph + ph; if (p2 >= 1.0) p2 -= 1.0;
		double p3 = p2 + ph; if (p3 >= 1.0) p3 -= 1.0;

		double v;
		switch (kind)
		{
		case 0: v = 0.55 * osc(ph) + 0.30 * osc(p2) + 0.15 * osc(p3); break;
		case 1: v = 0.75 * osc(ph) + 0.25 * osc(p2); break;
		case 2: {
			double p5 = p3 + p2; if (p5 >= 1.0) p5 -= 1.0;
			v = (osc(ph) + 0.333 * osc(p3) + 0.200 * osc(p5)) / 1.533;
			break;
		}
		default: v = osc(ph);
		}

		e *= 0.62 + 0.38 * dec;   /* identico a exp(-t*8.5), ma senza exp() */
		dec *= dsl;

		mix_pan(st + 2 * i, v * e * 32000.0, gl, gr);
	}
}

/* Spazzata di frequenza lineare f0 -> f1. */
static void add_sweep(s16 *st, size_t total, size_t start, double f0, double f1,
                      double dur, double vol, int kind, double pan)
{
	size_t len = (size_t)(dur * SR);
	size_t end = start + len;
	if (end > total) end = total;

	const double gl = gainL(vol, pan);
	const double gr = gainR(vol, pan);

	double ph = 0.0;
	for (size_t i = start; i < end; i++)
	{
		double t  = (double)(i - start) / (double)SR;
		double hz = f0 + (f1 - f0) * (t / dur);
		ph += hz / (double)SR;
		if (ph >= 1.0) ph -= 1.0;

		double left = (double)(end - i) / (double)SR;
		double e = left < 0.045 ? left / 0.045 : 1.0;
		if (t < 0.004) e *= t / 0.004;

		double p2 = ph + ph; if (p2 >= 1.0) p2 -= 1.0;
		double v = kind == 2 ? (osc(ph) + 0.333 * osc(p2)) / 1.333
		                     : 0.55 * osc(ph) + 0.30 * osc(p2);
		mix_pan(st + 2 * i, v * e * 32000.0, gl, gr);
	}
}

/* Rumore con passa-alto semplice e decadimento esponenziale. */
static void add_noise(s16 *st, size_t total, size_t start, double dur,
                      double vol, double decay, double pan)
{
	size_t len = (size_t)(dur * SR);
	size_t end = start + len;
	if (end > total) end = total;

	unsigned rnd = 0x9E3779B1u + (unsigned)start * 2654435761u;
	const double ek = exp(-decay / (double)SR);
	const double gl = gainL(vol, pan);
	const double gr = gainR(vol, pan);

	double prev = 0.0, e = 1.0;

	for (size_t i = start; i < end; i++)
	{
		rnd = rnd * 1103515245u + 12345u;
		double n = (double)(int)(rnd >> 8) / 8388608.0 - 1.0;   /* [-1,1) */
		double hp = (n - prev) * 0.8;
		prev = n;
		mix_pan(st + 2 * i, hp * e * 32000.0, gl, gr);
		e *= ek;
	}
}

/* Cassa: sinuside che scende rapida + click. */
static void add_kick(s16 *st, size_t total, size_t start, double vol, double pan)
{
	size_t len = (size_t)(SR * 0.10);
	size_t end = start + len;
	if (end > total) end = total;

	const double gl = gainL(vol, pan);
	const double gr = gainR(vol, pan);
	const double eK = exp(-22.0 / (double)SR);
	const double cK = exp(-30.0 / (double)SR);
	const double kK = exp(-300.0 / (double)SR);

	double ph = 0.0, e = 1.0, cl = 1.0, ck = 1.0;

	for (size_t i = start; i < end; i++)
	{
		double hz = 40.0 + 130.0 * cl;
		ph += hz / (double)SR;
		if (ph >= 1.0) ph -= 1.0;

		double v = osc(ph) * e + 0.15 * ck;
		mix_pan(st + 2 * i, v * 32000.0, gl, gr);
		e *= eK; cl *= cK; ck *= kK;
	}
}

/* ------------------------------------------------------------------ effetti */

enum {
  SX_BLUB,      /* cambio pagina: due note brevi discendenti */
  SX_WOZZ,      /* sweep corto ascendente */
  SX_TCHH,      /* tick di rumore */
  SX_LINE,      /* arpeggio ascendente (BLOCKOUT2) */
  SX_LINE2,     /* arcade: scende e risale (BLOCKOUT) */
  SX_LEVEL,     /* glissato lungo + tono finale (BLOCKOUT2) */
  SX_LEVEL2,    /* arpeggio a quadra d'ufficio (BLOCKOUT) */
  SX_EMPTY,     /* fanfara di accordo (pozzo vuoto) */
  SX_EMPTY2,    /* rumore + arpeggio rapido */
  SX_WELLDONE,  /* 4 note ascendenti + cassa */
  SX_WELLDONE2, /* trillo rapido */
  SX_HIT,       /* tono sordo: pezzo fermo */
  SX_NUM
};

/* n = numero di linee completate (1..4): piu' alto => piu' festoso */
static void gen_sfx(s16 *st, int type, int n)
{
	memset(st, 0, SFX_MAX * 2 * sizeof(s16));
	const size_t total = SFX_MAX;

	switch (type)
	{
	case SX_BLUB:
		add_tone(st, total, 0, 620.0, (size_t)(SR * 0.055), 0.26, 0.002,
		         0.02, 2, 0.0);
		add_tone(st, total, (size_t)(SR * 0.055), 465.0, (size_t)(SR * 0.055),
		         0.22, 0.002, 0.02, 2, 0.0);
		break;

	case SX_WOZZ:
		add_sweep(st, total, 0, 420.0, 1400.0, 0.13, 0.26, 1, 0.0);
		break;

	case SX_TCHH:
		add_noise(st, total, 0, 0.055, 0.22, 60.0, 0.0);
		break;

	case SX_LINE: {
		/* arpeggio ascendente: piu' alto piu' linee; "fanfara" finale */
		static const double NN[4] = { 660.0, 880.0, 1174.7, 1567.9 };
		int upto = n < 1 ? 1 : (n > 4 ? 4 : n);
		for (int k = 0; k < upto; k++)
			add_tone(st, total, (size_t)(k * SR * 0.060), NN[k],
			         (size_t)(SR * 0.12), 0.30, 0.003, 0.04, 0,
			         -0.35 + 0.25 * k);
		add_tone(st, total, (size_t)(upto * SR * 0.060), 2093.0,
		         (size_t)(SR * 0.20), 0.10, 0.003, 0.12, 0, 0.50);
		add_noise(st, total, (size_t)(upto * SR * 0.060), 0.10, 0.06,
		         40.0, 0.55);
		break;
	}

	case SX_LINE2: {
		/* style "BlockOut": scende e risale, piu' linee = piu' a lungo */
		int upto = n < 1 ? 1 : (n > 4 ? 4 : n);
		add_sweep(st, total, 0, 900.0, 240.0, 0.09, 0.30, 1, 0.0);
		add_sweep(st, total, (size_t)(SR * 0.09), 240.0,
		         900.0 + 260.0 * upto, 0.10 + 0.05 * upto, 0.30, 1, 0.0);
		break;
	}

	case SX_LEVEL:
		add_sweep(st, total, 0, 380.0, 1600.0, 0.40, 0.30, 1, 0.0);
		add_tone(st, total, (size_t)(SR * 0.36), 2093.0,
		         (size_t)(SR * 0.30), 0.16, 0.004, 0.18, 0, 0.0);
		break;

	case SX_LEVEL2: {
		static const double NN[5] = { 523.3, 659.3, 783.9, 1046.5, 1318.5 };
		for (int k = 0; k < 5; k++)
			add_tone(st, total, (size_t)(k * SR * 0.055), NN[k],
			         (size_t)(SR * 0.10), 0.24, 0.002, 0.03, 2, 0.0);
		break;
	}

	case SX_EMPTY: {
		/* pozzo vuoto: accordo ascendente + piatto */
		static const double NN[4] = { 523.3, 659.3, 783.9, 1046.5 };
		for (int k = 0; k < 4; k++) {
			add_tone(st, total, (size_t)(k * SR * 0.075), NN[k],
			         (size_t)(SR * 0.22), 0.22, 0.003, 0.10, 0,
			         -0.5 + 0.33 * k);
			add_tone(st, total, (size_t)(k * SR * 0.075), NN[k] * 2.0,
			         (size_t)(SR * 0.18), 0.08, 0.003, 0.08, 0,
			         0.5 - 0.33 * k);
		}
		add_noise(st, total, (size_t)(SR * 0.30), 0.16, 0.10, 22.0, 0.0);
		break;
	}

	case SX_EMPTY2:
		add_noise(st, total, 0, 0.10, 0.30, 25.0, 0.0);
		add_sweep(st, total, 0, 200.0, 1900.0, 0.30, 0.32, 1, 0.0);
		break;

	case SX_WELLDONE: {
		static const double n[4] = { 523.3, 659.3, 880.0, 1046.5 };
		for (int k = 0; k < 4; k++)
			add_tone(st, total, (size_t)(k * SR * 0.085), n[k],
			         (size_t)(SR * 0.15), 0.28, 0.003, 0.05, 0,
			         -0.40 + 0.30 * k);
		add_kick(st, total, 0, 0.40, 0.0);
		break;
	}

	case SX_WELLDONE2: {
		for (int k = 0; k < 8; k++)
			add_tone(st, total, (size_t)(k * SR * 0.038),
			         (k & 1) ? 1046.5 : 783.9, (size_t)(SR * 0.07), 0.22,
			         0.002, 0.02, 2, 0.0);
		break;
	}

	case SX_HIT:
		add_kick(st, total, 0, 0.45, 0.0);
		add_noise(st, total, 0, 0.09, 0.12, 45.0, 0.0);
		break;

	default: break;
	}
}


/* Restituisce un canale effetto libero (gia' terminato), -1 se occupati tutti.
 * Canale 4 e' riservato al loop di musica. */
static int free_sfx_slot(void)
{
	for (int k = 0; k < NSFX; k++)
	{
		int s = (g_sfxNext + k) % NSFX;
		if (s == MUSIC_CH) continue;
		ndspWaveBuf *b = &g_sbuf[s];
		if (b->status != NDSP_WBUF_QUEUED && b->status != NDSP_WBUF_PLAYING)
		{
			g_sfxNext = (s + 1) % NSFX;
			return s;
		}
	}
	return -1;
}

static void sfx_play(int type, int n)
{
	if (!g_ok || !g_on) return;

	int s = free_sfx_slot();
	if (s < 0) return;

	/* Il canale e' fermo: svuotarlo per ripulire lo stato DSP del canale. */
	ndspChnWaveBufClear(s);

	/* memset => status = NDSP_WBUF_FREE (0). NON scrivere QUEUED qui! */
	memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
	gen_sfx(g_sfxBuf[s], type, n);
	g_sbuf[s].data_pcm16 = g_sfxBuf[s];
	g_sbuf[s].nsamples   = (type == SX_LEVEL)    ? (size_t)(SR * 0.68) :
	                       (type == SX_EMPTY)    ? (size_t)(SR * 0.50) :
	                       (type == SX_EMPTY2)   ? (size_t)(SR * 0.32) :
	                       (type == SX_LINE)     ? (size_t)(SR * 0.30) :
	                       (type == SX_LINE2)    ? (size_t)(SR * 0.32) :
	                       (type == SX_LEVEL2)   ? (size_t)(SR * 0.34) :
	                       (type == SX_WELLDONE) ? (size_t)(SR * 0.42) :
	                       (type == SX_WELLDONE2)? (size_t)(SR * 0.36) :
	                       (type == SX_WOZZ)     ? (size_t)(SR * 0.14) :
	                       (type == SX_HIT)      ? (size_t)(SR * 0.12) :
	                                            (size_t)(SR * 0.12);
	g_sbuf[s].looping = false;

	/* Flush dell'intero buffer: 64 byte allineato e size multipla di 8. */
	DSP_FlushDataCache(g_sfxBuf[s], SFX_MAX * 2 * sizeof(s16));
	ndspChnWaveBufAdd(s, &g_sbuf[s]);
}

/* Loop di musica del menu: 8 battute a 120 bpm (Am F C G / Am F Dm E),
 * basso + pad + arpeggio di semicrome spartito + melodia + cassa/hat.
 * Le frequenze sono date in MIDI (69 = La 440) per scrivere la melodia. */
static double noteHz(int m) { return 440.0 * pow(2.0, (double)(m - 69) / 12.0); }

static void gen_music(s16 *st)
{
	memset(st, 0, MUSIC_MAX * 2 * sizeof(s16));
	const size_t total = MUSIC_MAX;
	const double beat = 0.5;                 /* 120 bpm */
	const double bar = 4.0 * beat;           /* 2 s */

	static const int triad[8][3] = {         /* Am F C G Am F Dm E */
		{ 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 },
		{ 57, 60, 64 }, { 53, 57, 60 }, { 50, 53, 57 }, { 52, 56, 59 }
	};
	static const int bass[8] = { 45, 41, 48, 43, 45, 41, 50, 40 };

	/* melodia: una frase per battuta (midi, durata in battiti) */
	static const int leadM[8][4] = {
		{ 76, 74, 72, 74 }, { 72, 69, 67, 69 },
		{ 67, 64, 67, 72 }, { 71, 69, 67, 69 },
		{ 69, 72, 76, 81 }, { 81, 79, 76, 77 },
		{ 77, 76, 74, 76 }, { 71, 71, 68, 68 }
	};
	static const double leadD[8][4] = {
		{ 1, 0.5, 0.5, 2 }, { 1, 0.5, 0.5, 2 },
		{ 1, 0.5, 0.5, 2 }, { 1, 0.5, 0.5, 2 },
		{ 1, 0.5, 0.5, 2 }, { 1, 0.5, 0.5, 2 },
		{ 1, 0.5, 0.5, 2 }, { 2, 0, 0, 2 }
	};

	for (int b = 0; b < 8; b++) {
		double t0 = b * bar;

		/* pad: triade tenuta per tutta la battuta */
		for (int k = 0; k < 3; k++)
			add_tone(st, total, (size_t)(t0 * SR), noteHz(triad[b][k]),
			         (size_t)(bar * SR), 0.050, 0.40, 0.80, 0,
			         -0.3 + 0.3 * k);

		/* basso: fondamentale sui 4 quarti (quinto sull'ultimo) */
		for (int q = 0; q < 4; q++) {
			int m = (q == 3) ? bass[b] + 7 : bass[b];
			add_tone(st, total, (size_t)((t0 + q * beat) * SR),
			         noteHz(m), (size_t)(0.42 * SR), 0.17, 0.006,
			         0.10, 1, 0.0);
		}

		/* arpeggio di crome: su/gio' sulla triade, pan alternato */
		for (int i = 0; i < 8; i++) {
			int m = triad[b][i % 4 == 3 ? 1 : i % 4];
			if (i >= 4) m += 12;
			add_tone(st, total, (size_t)((t0 + i * beat * 0.5) * SR),
			         noteHz(m), (size_t)(0.20 * SR), 0.075, 0.004,
			         0.08, 2, (i & 1) ? 0.4 : -0.4);
		}

		/* melodia */
		double tl = t0;
		for (int k = 0; k < 4; k++) {
			if (leadD[b][k] <= 0.0) continue;
			add_tone(st, total, (size_t)(tl * SR), noteHz(leadM[b][k]),
			         (size_t)(leadD[b][k] * beat * SR), 0.16, 0.008,
			         0.12, 0, 0.1);
			tl += leadD[b][k] * beat;
		}

		/* groove: cassa sui quarti, hat sulle crome deboli */
		for (int q = 0; q < 4; q++)
			add_kick(st, total, (size_t)((t0 + q * beat) * SR), 0.30, 0.0);
		for (int i = 1; i < 8; i += 2)
			add_noise(st, total, (size_t)((t0 + i * beat * 0.5) * SR),
			          0.030, 0.05, 90.0, 0.2);
	}
}

static void music_play(void)
{
	if (!g_ok || !g_on) return;
	if (g_sbuf[MUSIC_CH].status == NDSP_WBUF_QUEUED ||
	    g_sbuf[MUSIC_CH].status == NDSP_WBUF_PLAYING) return;

	ndspChnWaveBufClear(MUSIC_CH);
	memset(&g_sbuf[MUSIC_CH], 0, sizeof(ndspWaveBuf));
	/* il PCM e' gia' generato una volta in audio_init: qui solo accodamento */
	g_sbuf[MUSIC_CH].data_pcm16 = g_musicBuf;
	g_sbuf[MUSIC_CH].nsamples   = MUSIC_MAX;
	g_sbuf[MUSIC_CH].looping    = true;
	DSP_FlushDataCache(g_musicBuf, MUSIC_MAX * 2 * sizeof(s16));
	ndspChnWaveBufAdd(MUSIC_CH, &g_sbuf[MUSIC_CH]);
}


/* ------------------------------------------------------------------ API */

void audio_init(void)
{
	if (g_ok) return;

	if (R_FAILED(ndspInit())) return;

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetOutputCount(2);
	ndspSetMasterVol(0.85f);

	for (int s = 0; s < NSFX; s++)
		g_sfxBuf[s] = linearMemAlign(SFX_MAX * 2 * sizeof(s16), 0x40);
	g_musicBuf = linearMemAlign(MUSIC_MAX * 2 * sizeof(s16), 0x40);

	for (int s = 0; s < NSFX; s++)
		if (!g_sfxBuf[s]) { ndspExit(); return; }
	if (!g_musicBuf) { ndspExit(); return; }

	osc_init();

	/* loop generato una volta qui (il boot test da 2.2 s copre il tempo);
	 * music_play() poi accoda soltanto, senza scatti al ritorno nel menu */
	gen_music(g_musicBuf);

	for (int s = 0; s < NSFX; s++)
	{
		memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
		g_sbuf[s].data_pcm16 = g_sfxBuf[s];
		g_sbuf[s].nsamples   = 0;
	}
	memset(&g_sbuf[MUSIC_CH], 0, sizeof(ndspWaveBuf));
	g_sbuf[MUSIC_CH].data_pcm16 = g_musicBuf;
	g_sbuf[MUSIC_CH].nsamples   = 0;

	static float s_mixStereo[12];
	s_mixStereo[0] = 1.0f;
	s_mixStereo[1] = 1.0f;
	for (int ch = 0; ch < NSFX; ch++)
	{
		ndspChnSetFormat(ch, NDSP_FORMAT_STEREO_PCM16);
		ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
		ndspChnSetRate(ch, SR);
		ndspChnSetMix(ch, s_mixStereo);
	}

	g_ok = true;
	g_on = true;
}

void audio_exit(void)
{
	if (!g_ok) return;
	/* ferma tutto prima di ndspExit: buffer accodati che suonano
	 * durante lo shutdown appendono l'uscita al ritorno all'HB menu */
	for (int ch = 0; ch < NSFX; ch++)
	{
		ndspChnWaveBufClear(ch);
		memset(&g_sbuf[ch], 0, sizeof(ndspWaveBuf));
	}
	g_ok = false;
	g_on = false;
	ndspExit();
}

bool audio_ok(void) { return g_ok; }

/* SoundManager::SetEnable */
void audio_set_enable(bool enable) { g_on = enable; }
bool audio_enabled(void) { return g_on; }

void audio_blub(void)     { sfx_play(SX_BLUB, 0); }
void audio_wozz(void)     { sfx_play(SX_WOZZ, 0); }
void audio_tchh(void)     { sfx_play(SX_TCHH, 0); }

void audio_line(void)     { sfx_play(SX_LINE, 4); }
void audio_level(void)    { sfx_play(SX_LEVEL, 0); }
void audio_empty(void)    { sfx_play(SX_EMPTY, 0); }
void audio_welldone(void) { sfx_play(SX_WELLDONE, 0); }

void audio_line2(void)    { sfx_play(SX_LINE2, 4); }
void audio_level2(void)   { sfx_play(SX_LEVEL2, 0); }
void audio_empty2(void)   { sfx_play(SX_EMPTY2, 0); }
void audio_welldone2(void){ sfx_play(SX_WELLDONE2, 0); }

void audio_hit(void)      { sfx_play(SX_HIT, 0); }

void audio_music(void)       { music_play(); }
void audio_stop_music(void)
{
	if (!g_ok) return;
	ndspChnWaveBufClear(MUSIC_CH);
	memset(&g_sbuf[MUSIC_CH], 0, sizeof(ndspWaveBuf));
}

