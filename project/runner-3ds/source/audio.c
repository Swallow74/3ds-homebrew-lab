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
/* NDSP audio (DSP::DSP service) - looping music + synthesized effects.
 *
 * FUNDAMENTAL RULES (source: libctru/source/ndsp/ndsp-channel.c):
 *   ndspChnWaveBufAdd() does this:
 *       if (!buf->nsamples) return;
 *       if (buf->status == NDSP_WBUF_QUEUED || buf->status == NDSP_WBUF_PLAYING) return;
 *   So you must NEVER initialize ndspWaveBuf::status to QUEUED before
 *   calling ndspChnWaveBufAdd(): in that case the function queues
 *   nothing and prints no errors -> total silence. The status is written by
 *   libctru/DSP; we leave memset(...,0,...) which equals NDSP_WBUF_FREE.
 *
 *   Also every channel must have its OWN ndspWaveBuf: with a single
 *   wavebuf shared among several channels, "buf->next = NULL" would corrupt the
 *   previous channel's list.
 *
 *   The PCM buffers must be in linear memory (DSP::DSP reads RAM through its
 *   own MMU) + CacheFlush before starting them.
 *
 * MUSIC: see music.c (real-time sequencer + synth on CH_MUSIC,
 * its own thread woken by the NDSP callback).  Only the effects remain here.
 *
 * OSCILLATOR: 4096-point table with linear interpolation, instead of
 * sinf/asin called per sample (slow on the ARM11).  Decays are done
 * with a per-sample multiplier instead of per-sample exp(): generating
 * ~3.6 million samples costs a few tens of milliseconds, not seconds.
 */

#include "audio.h"
#include "music.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SR            22050                 /* SampleRate */
#define CH_MUSIC      0
#define NSFX          6                     /* effect channels: 1..6 */
#define CH_SFX_BASE   1
#define SFX_MAX       ((size_t)(SR * 0.45)) /* frames available per effect */

static bool     g_ok = false;
static bool     g_musicPref = true;   /* user choice (menu/SELECT) */
static int      g_sfxParam = 0;       /* effect parameter (e.g. coin note) */

static s16        *g_sfxBuf[NSFX];
static ndspWaveBuf g_sbuf[NSFX];    /* one wavebuf per effect channel */
static int         g_sfxNext = 0;

/* Mix toward front left/right (mix[0] = front L, mix[1] = front R):
 * identical to the devkitPro/examples/3ds/audio/streaming example. */
static float s_mixStereo[12];

/* ------------------------------------------------------------------ osc */

#define OS_C 4096
static double g_sin[OS_C + 1];   /* +1 point: the interpolation cannot overrun */

static void osc_init(void)
{
	for (int i = 0; i <= OS_C; i++)
		g_sin[i] = sin((double)i * (2.0 * M_PI / (double)OS_C));
}

/* sine sample: ph in [0,1) = one whole phase */
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

/* Accumulates a sample into an interleaved stereo PCM16 buffer, with pan.
 * 'st' points to the left sample; the right one follows immediately.
 * pan = -1 all left, +1 all right, 0 center (as before). */
static void mix_pan(s16 *st, double v, double gl, double gr)
{
	int l = (int)st[0] + clamp_s(v * gl);
	int r = (int)st[1] + clamp_s(v * gr);
	st[0] = (s16)(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
	st[1] = (s16)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
}

/* pan gains: move the channel without changing the perceived volume */
static inline double gainL(double vol, double pan) { return vol * (1.0 - 0.35 * pan); }
static inline double gainR(double vol, double pan) { return vol * (1.0 + 0.35 * pan); }

/* "Chiptune" tone: controlled harmonics + attack/decay/release envelope.
 * kind: 0 = lead (3 harmonics), 1 = bass, 2 = soft square. */
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
	const double dsl = exp(-8.5 / (double)SR); /* "string-like" decay per sample */

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

		e *= 0.62 + 0.38 * dec;   /* identical to exp(-t*8.5), but without exp() */
		dec *= dsl;

		mix_pan(st + 2 * i, v * e * 32000.0, gl, gr);
	}
}

/* Linear frequency sweep f0 -> f1. */
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

/* Noise with a simple high-pass and exponential decay. */
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

/* Kick drum: fast-falling sine + click. */
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

/* ------------------------------------------------------------------ effects */

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
		/* two-note "ding" (a fourth above): the chain raises the pitch by a
		 * semitone per coin, up to an octave */
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

/* useful duration (in frames) of each effect */
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

/* Returns a free effect channel (already finished), -1 if all are busy. */
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

/* force: if all channels are playing, steal the next one round-robin
 * (needed for the crash, which must never vanish under a rain of coins) */
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

	/* The channel is stopped (or stolen): clearing it resets the DSP state. */
	ndspChnWaveBufClear(ch);

	/* memset => status = NDSP_WBUF_FREE (0). Do NOT write QUEUED here! */
	memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
	gen_sfx(g_sfxBuf[s], type);
	g_sbuf[s].data_pcm16 = g_sfxBuf[s];
	g_sbuf[s].nsamples   = sfx_len(type);
	g_sbuf[s].looping = false;

	/* Flush of the whole buffer: 64-byte aligned and size a multiple of 8,
	 * so the DSP cache leaves no partial unwritten lines. */
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

	osc_init();                       /* before generating any PCM */
	for (int s = 0; s < NSFX; s++)
	{
		memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
		g_sbuf[s].data_pcm16 = g_sfxBuf[s];
		g_sbuf[s].nsamples   = 0;
	}

	/* interleaved stereo PCM16 on all the channels used */
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
	/* music starts right away (title theme) if the user wants it */
	music_enable(g_musicPref);
	music_play(MUS_TITLE);
	music_init(CH_MUSIC);
}

void audio_exit(void)
{
	if (!g_ok) return;
	g_ok = false;
	music_exit();            /* the music thread first */
	/* stop and flush ALL the channels before closing the DSP: exiting with
	 * wavebufs still queued (looping music) can hang the console */
	for (int ch = 0; ch <= CH_SFX_BASE + NSFX - 1; ch++) {
		ndspChnWaveBufClear(ch);
		ndspChnReset(ch);
	}
	svcSleepThread(50000000LL);     /* 50 ms: the DSP completes the last frame */
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
