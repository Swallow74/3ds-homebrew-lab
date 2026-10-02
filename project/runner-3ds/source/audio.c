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
/* Audio NDSP (servizio DSP::DSP) - musica a loop + effetti sintetizzati.
 *
 * REGOLE FONDAMENTALI (fonte: libctru/source/ndsp/ndsp-channel.c):
 *   ndspChnWaveBufAdd() fa cos':
 *       if (!buf->nsamples) return;
 *       if (buf->status == NDSP_WBUF_QUEUED || buf->status == NDSP_WBUF_PLAYING) return;
 *   Quindi NON bisogna MAI inizializzare ndspWaveBuf::status a QUEUED prima
 *   di chiamare ndspChnWaveBufAdd(): in quel caso la funzione non accoda
 *   nulla e non stampa errori -> silenzio totale. Lo status lo scrive
 *   libctru/DSP; noi lasciamo memset(...,0,...) che vale NDSP_WBUF_FREE.
 *
 *   Inoltre ogni canale deve avere il PROPRIO ndspWaveBuf: con un solo
 *   wavebuf condiviso tra piu' canali, "buf->next = NULL" corromperebbe la
 *   lista del canale precedente.
 *
 *   I buffer PCM devono stare in linear memory (DSP::DSP legge RAM tramite
 *   MMU propria) + CacheFlush prima di avviarli.
 *
 * MUSICA: vedi music.c (sequencer + synth in tempo reale su CH_MUSIC,
 * thread proprio svegliato dalla callback NDSP).  Qui restano gli effetti.
 *
 * OSCILLATORE: tabella di 4096 punti con interpolazione lineare, invece di
 * sinf/asin chiamate per campione (lento su ARM11).  Le decadere sono rese
 * con un moltiplicatore per campione invece di exp() per campione: generare
 * ~3.6 milioni di campioni costa qualche decina di millisecondi, non secondi.
 */

#include "audio.h"
#include "music.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SR            22050                 /* SampleRate */
#define CH_MUSIC      0
#define NSFX          6                     /* canali effetti: 1..6 */
#define CH_SFX_BASE   1
#define SFX_MAX       ((size_t)(SR * 0.45)) /* frames disponibili per effetto */

static bool     g_ok = false;
static bool     g_musicPref = true;   /* scelta dell'utente (menu/SELECT) */
static int      g_sfxParam = 0;       /* parametro dell'effetto (es. nota moneta) */

static s16        *g_sfxBuf[NSFX];
static ndspWaveBuf g_sbuf[NSFX];    /* un wavebuf per canale effetto */
static int         g_sfxNext = 0;

/* Mix verso fronte sinistra/destra (mix[0] = front L, mix[1] = front R):
 * identico all'esempio devkitPro/examples/3ds/audio/streaming. */
static float s_mixStereo[12];

/* ------------------------------------------------------------------ osc */

#define OS_C 4096
static double g_sin[OS_C + 1];   /* +1 punto: l'interpolazione non puo' sortir */

static void osc_init(void)
{
	for (int i = 0; i <= OS_C; i++)
		g_sin[i] = sin((double)i * (2.0 * M_PI / (double)OS_C));
}

/* campione di sinusoide: ph in [0,1) = una fase intera */
static double osc(double ph)
{
	double u = ph * (double)OS_C;
	int i = (int)u;
	double f = u - (double)i;
	return g_sin[i] + (g_sin[i + 1] - g_sin[i]) * f;
}

/* --------------------------------------------------------------- misc */

static int clamp_s(double v)
{
	return v > 32767.0 ? 32767 : (v < -32768.0 ? -32768 : (int)v);
}

/* Accumula un campione in un buffer PCM16 stereo interleaved, con pan.
 * 'st' punta al campione sinistro; il destro e' subito dopo.
 * pan = -1 tutto a sinistra, +1 tutto a destra, 0 al centro (come prima). */
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

enum { SX_MOVE, SX_JUMP, SX_BONUS, SX_CRASH, SX_START, SX_COIN, SX_POWER,
       SX_SHIELD, SX_SELECT, SX_GO, SX_NUM };

static void gen_sfx(s16 *st, int type)
{
	memset(st, 0, SFX_MAX * 2 * sizeof(s16));
	const size_t total = SFX_MAX;

	switch (type)
	{
	case SX_MOVE:
		add_tone(st, total, 0, 1180.0, (size_t)(SR * 0.045), 0.30, 0.002,
		         0.02, 2, -0.25);
		add_tone(st, total, 0, 2360.0, (size_t)(SR * 0.030), 0.10, 0.002,
		         0.015, 0, 0.25);
		break;

	case SX_JUMP:
		add_sweep(st, total, 0, 300.0, 1180.0, 0.13, 0.30, 0, -0.15);
		add_noise(st, total, (size_t)(SR * 0.10), 0.05, 0.09, 60.0, 0.20);
		break;

	case SX_BONUS: {
		static const double n[3] = { 880.0, 1174.7, 1567.9 };
		for (int k = 0; k < 3; k++)
			add_tone(st, total, (size_t)(k * SR * 0.055), n[k],
			         (size_t)(SR * 0.10), 0.28, 0.003, 0.04, 0,
			         -0.30 + 0.30 * k);
		add_tone(st, total, (size_t)(3 * SR * 0.055), 3135.9,
		         (size_t)(SR * 0.18), 0.09, 0.003, 0.12, 0, 0.45);
		add_noise(st, total, (size_t)(3 * SR * 0.055), 0.10, 0.05, 45.0, 0.55);
		break;
	}

	case SX_CRASH:
		add_noise(st, total, 0, 0.34, 0.55, 9.0, 0.0);
		add_sweep(st, total, 0, 190.0, 55.0, 0.30, 0.45, 1, 0.0);
		add_noise(st, total, (size_t)(SR * 0.06), 0.22, 0.22, 14.0, 0.30);
		break;

	case SX_START: {
		static const double n[4] = { 659.3, 880.0, 1046.5, 1318.6 };
		for (int k = 0; k < 4; k++)
			add_tone(st, total, (size_t)(k * SR * 0.075), n[k],
			         (size_t)(SR * 0.13), 0.26, 0.003, 0.05, 0,
			         -0.40 + 0.30 * k);
		add_kick(st, total, 0, 0.4, 0.0);
		break;
	}

	case SX_COIN: {
		/* "ding" a due note (quarta sopra): la catena alza il tono di un
		 * semitono per moneta, fino a un'ottava */
		double k = pow(2.0, (double)g_sfxParam / 12.0);
		add_tone(st, total, 0, 1318.5 * k, (size_t)(SR * 0.05), 0.20, 0.001,
		         0.02, 2, -0.10);
		add_tone(st, total, (size_t)(SR * 0.045), 1760.0 * k,
		         (size_t)(SR * 0.11), 0.22, 0.001, 0.08, 0, 0.10);
		add_tone(st, total, (size_t)(SR * 0.045), 3520.0 * k,
		         (size_t)(SR * 0.07), 0.05, 0.001, 0.05, 0, 0.40);
		break;
	}

	case SX_POWER: {
		add_sweep(st, total, 0, 440.0, 1760.0, 0.22, 0.20, 2, -0.30);
		static const double n[3] = { 1046.5, 1318.5, 1568.0 };
		for (int k = 0; k < 3; k++)
			add_tone(st, total, (size_t)(SR * (0.12 + 0.05 * k)), n[k] * 2.0,
			         (size_t)(SR * 0.12), 0.14, 0.002, 0.06, 0,
			         -0.30 + 0.30 * k);
		break;
	}

	case SX_SHIELD:
		add_noise(st, total, 0, 0.25, 0.30, 12.0, 0.0);
		add_sweep(st, total, 0, 1500.0, 300.0, 0.26, 0.24, 2, 0.0);
		add_tone(st, total, 0, 196.0, (size_t)(SR * 0.25), 0.25, 0.002, 0.15,
		         1, 0.0);
		break;

	case SX_SELECT:
		add_tone(st, total, 0, 880.0, (size_t)(SR * 0.04), 0.20, 0.002, 0.02,
		         2, -0.15);
		add_tone(st, total, (size_t)(SR * 0.035), 1318.5,
		         (size_t)(SR * 0.07), 0.20, 0.002, 0.05, 2, 0.15);
		break;

	case SX_GO:
		add_tone(st, total, 0, g_sfxParam ? 1760.0 : 880.0,
		         (size_t)(SR * (g_sfxParam ? 0.30 : 0.12)), 0.24, 0.003, 0.08,
		         2, 0.0);
		break;

	default: break;
	}
}

/* durata utile (in frame) di ogni effetto */
static size_t sfx_len(int type)
{
	switch (type) {
	case SX_CRASH:  return (size_t)(SR * 0.36);
	case SX_BONUS:  return (size_t)(SR * 0.28);
	case SX_START:  return (size_t)(SR * 0.42);
	case SX_JUMP:   return (size_t)(SR * 0.16);
	case SX_COIN:   return (size_t)(SR * 0.16);
	case SX_POWER:  return (size_t)(SR * 0.34);
	case SX_SHIELD: return (size_t)(SR * 0.30);
	case SX_SELECT: return (size_t)(SR * 0.11);
	case SX_GO:     return (size_t)(SR * 0.32);
	default:        return (size_t)(SR * 0.08);
	}
}

/* Restituisce un canale effetto libero (gia' terminato), -1 se occupati tutti. */
static int free_sfx_slot(void)
{
	for (int k = 0; k < NSFX; k++)
	{
		int s = (g_sfxNext + k) % NSFX;
		ndspWaveBuf *b = &g_sbuf[s];
		if (b->status != NDSP_WBUF_QUEUED && b->status != NDSP_WBUF_PLAYING)
		{
			g_sfxNext = (s + 1) % NSFX;
			return s;
		}
	}
	return -1;
}

/* force: se tutti i canali suonano, ruba il prossimo in round-robin
 * (serve per il crash, che non deve mai sparire sotto una pioggia di monete) */
static void sfx_play_ex(int type, bool force)
{
	if (!g_ok) return;

	int s = free_sfx_slot();
	if (s < 0) {
		if (!force) return;
		s = g_sfxNext;
		g_sfxNext = (s + 1) % NSFX;
	}

	int ch = CH_SFX_BASE + s;

	/* Il canale e' fermo (o rubato): svuotarlo ripulisce lo stato DSP. */
	ndspChnWaveBufClear(ch);

	/* memset => status = NDSP_WBUF_FREE (0). NON scrivere QUEUED qui! */
	memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
	gen_sfx(g_sfxBuf[s], type);
	g_sbuf[s].data_pcm16 = g_sfxBuf[s];
	g_sbuf[s].nsamples   = sfx_len(type);
	g_sbuf[s].looping = false;

	/* Flush dell'intero buffer: 64 byte allineato e size multipla di 8,
	 * cosi' la cache del DSP non lascia righe parziali non scritte. */
	DSP_FlushDataCache(g_sfxBuf[s], SFX_MAX * 2 * sizeof(s16));
	ndspChnWaveBufAdd(ch, &g_sbuf[s]);
}

static void sfx_play(int type) { sfx_play_ex(type, false); }

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

	for (int s = 0; s < NSFX; s++)
		if (!g_sfxBuf[s]) { ndspExit(); return; }

	osc_init();                       /* prima di generare qualsiasi PCM */
	for (int s = 0; s < NSFX; s++)
	{
		memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
		g_sbuf[s].data_pcm16 = g_sfxBuf[s];
		g_sbuf[s].nsamples   = 0;
	}

	/* PCM16 stereo interleaved su tutti i canali usati */
	s_mixStereo[0] = 1.0f;   /* front left  */
	s_mixStereo[1] = 1.0f;   /* front right */
	for (int ch = 0; ch <= NSFX; ch++)
	{
		ndspChnSetFormat(ch, NDSP_FORMAT_STEREO_PCM16);
		ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
		ndspChnSetRate(ch, SR);
		ndspChnSetMix(ch, s_mixStereo);
	}

	g_ok = true;
	/* la musica parte subito (tema del titolo) se l'utente la vuole */
	music_enable(g_musicPref);
	music_play(MUS_TITLE);
	music_init(CH_MUSIC);
}

void audio_exit(void)
{
	if (!g_ok) return;
	g_ok = false;
	music_exit();            /* prima il thread della musica */
	/* ferma e svuota TUTTI i canali prima di chiudere il DSP: uscire con
	 * wavebuf ancora in coda (musica in loop) puo' bloccare la console */
	for (int ch = 0; ch <= CH_SFX_BASE + NSFX - 1; ch++) {
		ndspChnWaveBufClear(ch);
		ndspChnReset(ch);
	}
	svcSleepThread(50000000LL);     /* 50 ms: il DSP completa l'ultimo frame */
	ndspExit();
	for (int k = 0; k < NSFX; k++)
		if (g_sfxBuf[k]) { linearFree(g_sfxBuf[k]); g_sfxBuf[k] = NULL; }
}

bool audio_ok(void) { return g_ok; }

void audio_set_music(bool on)
{
	g_musicPref = on;
	music_enable(on);
}

void audio_music(int song) { music_play(song); }
void audio_duck(bool on)   { music_duck(on); }

bool audio_music_on(void) { return g_musicPref; }

void audio_start(void)
{
	if (!g_ok) return;
	sfx_play(SX_START);
}

void audio_move(void)   { sfx_play(SX_MOVE); }
void audio_jump(void)   { sfx_play(SX_JUMP); }
void audio_bonus(void)  { sfx_play(SX_BONUS); }
void audio_crash(void)  { sfx_play_ex(SX_CRASH, true); }
void audio_power(void)  { sfx_play_ex(SX_POWER, true); }
void audio_shield(void) { sfx_play_ex(SX_SHIELD, true); }
void audio_select(void) { sfx_play(SX_SELECT); }

void audio_coin(int step)
{
	g_sfxParam = step < 0 ? 0 : (step > 12 ? 12 : step);
	sfx_play(SX_COIN);
	g_sfxParam = 0;
}

void audio_go(bool last)
{
	g_sfxParam = last ? 1 : 0;
	sfx_play(SX_GO);
	g_sfxParam = 0;
}
