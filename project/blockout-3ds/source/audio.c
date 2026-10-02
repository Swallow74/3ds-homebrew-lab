/*
  Synthesized audio via NDSP: the same event "schedule" as
  SoundManager.cpp of BlockOut II 2.5 (GPL).  The original plays .wav/.mod
  files with SDL_mixer; on the 3DS the resources do not exist, so the
  effects are synthesized (same events, same names).

  Starting synth/NDSP: runner-3ds/source/audio.c +
  devkitPro/3ds-examples/audio/streaming.
*/

#include "audio.h"
#include "music.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>


#define SR            22050
#define NSFX          4                     /* channels 0..3: effects */
#define MUSIC_CH      4                     /* channel 4: streamed music (music.c) */
#define SFX_MAX       ((size_t)(SR * 0.80)) /* frames available per effect */

static bool g_ok = false;
static bool g_on = false;   /* SoundManager::SetEnable (effects) */
static bool g_music = true; /* music on (Setup) */
static bool g_musicOk = false;
static int  g_style = 0;    /* 0 = BlockOut II, 1 = BlockOut DOS */
static int  g_lines = 1;    /* layers removed by the last piece */

static s16        *g_sfxBuf[NSFX];
static ndspWaveBuf g_sbuf[NSFX];
static int         g_sfxNext = 0;

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

/* Accumulates a sample into an interleaved stereo PCM16 buffer.
 * 'st' points to the left sample; the right one follows immediately. */
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
		case 3: v = ph < 0.5 ? 0.55 : -0.55; break;   /* PC speaker */
		default: v = osc(ph);
		}

		if (kind == 3) dec = 1.0;                      /* no decay */
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
		double v = kind == 3 ? (ph < 0.5 ? 0.5 : -0.5)
		         : kind == 2 ? (osc(ph) + 0.333 * osc(p2)) / 1.333
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

enum {
  SX_BLUB,      /* menu: value change */
  SX_WOZZ,      /* menu: confirm */
  SX_TCHH,      /* menu: cursor movement */
  SX_LINE,      /* layers completed (BlockOut II) */
  SX_LINE2,     /* layers completed (BlockOut DOS, square wave) */
  SX_LEVEL,     /* next level (BlockOut II) */
  SX_LEVEL2,    /* next level (DOS) */
  SX_EMPTY,     /* pit cleared (BlockOut II) */
  SX_EMPTY2,    /* pit cleared (DOS) */
  SX_WELLDONE,  /* high score (BlockOut II) */
  SX_WELLDONE2, /* high score (DOS) */
  SX_HIT,       /* piece landing (BlockOut II) */
  SX_HIT2,      /* piece landing (DOS) */
  SX_OVER,      /* game over */
  SX_NUM
};

/* n = layers completed together (1..5): more layers = longer phrase */
static size_t gen_sfx(s16 *st, int type, int n)
{
	memset(st, 0, SFX_MAX * 2 * sizeof(s16));
	const size_t total = SFX_MAX;
	if (n < 1) n = 1;
	if (n > 5) n = 5;

	switch (type)
	{
	case SX_BLUB:
		add_tone(st, total, 0, 620.0, (size_t)(SR * 0.045), 0.24, 0.002, 0.02, 2, 0.0);
		add_tone(st, total, (size_t)(SR * 0.045), 930.0, (size_t)(SR * 0.050), 0.20, 0.002, 0.02, 2, 0.0);
		return (size_t)(SR * 0.10);

	case SX_WOZZ:
		add_sweep(st, total, 0, 300.0, 1250.0, 0.12, 0.24, 1, 0.0);
		add_tone(st, total, (size_t)(SR * 0.10), 1318.5, (size_t)(SR * 0.10), 0.12, 0.002, 0.06, 0, 0.0);
		return (size_t)(SR * 0.21);

	case SX_TCHH:
		add_tone(st, total, 0, 1760.0, (size_t)(SR * 0.018), 0.10, 0.001, 0.01, 2, 0.0);
		add_noise(st, total, 0, 0.035, 0.12, 90.0, 0.0);
		return (size_t)(SR * 0.05);

	case SX_LINE: {
		/* major arpeggio that rises one step per layer */
		static const double NN[8] = { 523.3, 659.3, 783.9, 1046.5, 1318.5, 1567.9, 2093.0, 2637.0 };
		int cnt = 2 + n;
		for (int k = 0; k < cnt; k++)
			add_tone(st, total, (size_t)(k * SR * 0.055), NN[k], (size_t)(SR * 0.14),
			         0.24, 0.003, 0.05, 0, -0.45 + 0.9 * k / (cnt - 1));
		add_noise(st, total, 0, 0.22, 0.10, 14.0, 0.0);          /* the layer "collapsing" */
		add_kick(st, total, 0, 0.30, 0.0);
		return (size_t)(SR * (0.20 + 0.055 * cnt));
	}

	case SX_LINE2: {
		/* PC speaker: square-wave scale, one octave higher per layer */
		double f = 440.0;
		int cnt = 3 + 2 * n;
		for (int k = 0; k < cnt; k++) {
			add_tone(st, total, (size_t)(k * SR * 0.035), f, (size_t)(SR * 0.034),
			         0.16, 0.001, 0.004, 3, 0.0);
			f *= 1.1892;   /* minor third */
		}
		return (size_t)(SR * (0.04 + 0.035 * cnt));
	}

	case SX_LEVEL:
		add_sweep(st, total, 0, 260.0, 1560.0, 0.34, 0.24, 1, 0.0);
		for (int k = 0; k < 3; k++)
			add_tone(st, total, (size_t)(SR * (0.34 + 0.09 * k)), 1046.5 * (k == 1 ? 1.26 : k == 2 ? 1.5 : 1.0),
			         (size_t)(SR * 0.22), 0.16, 0.003, 0.12, 0, -0.3 + 0.3 * k);
		return (size_t)(SR * 0.74);

	case SX_LEVEL2: {
		static const double NN[6] = { 523.3, 659.3, 783.9, 1046.5, 783.9, 1046.5 };
		static const double DD[6] = { 0.07, 0.07, 0.07, 0.14, 0.07, 0.24 };
		double t = 0.0;
		for (int k = 0; k < 6; k++) {
			add_tone(st, total, (size_t)(t * SR), NN[k], (size_t)(DD[k] * SR * 0.92),
			         0.16, 0.001, 0.005, 3, 0.0);
			t += DD[k];
		}
		return (size_t)(SR * (t + 0.02));
	}

	case SX_EMPTY: {
		static const double NN[4] = { 523.3, 659.3, 783.9, 1046.5 };
		for (int k = 0; k < 4; k++) {
			add_tone(st, total, (size_t)(k * SR * 0.075), NN[k], (size_t)(SR * 0.30),
			         0.20, 0.003, 0.12, 0, -0.5 + 0.33 * k);
			add_tone(st, total, (size_t)(k * SR * 0.075), NN[k] * 2.0, (size_t)(SR * 0.24),
			         0.07, 0.003, 0.10, 0, 0.5 - 0.33 * k);
		}
		add_noise(st, total, (size_t)(SR * 0.30), 0.35, 0.10, 9.0, 0.0);
		return (size_t)(SR * 0.72);
	}

	case SX_EMPTY2: {
		double f = 262.0;
		for (int k = 0; k < 16; k++) {
			add_tone(st, total, (size_t)(k * SR * 0.030), f, (size_t)(SR * 0.029),
			         0.15, 0.001, 0.004, 3, 0.0);
			f *= 1.0595 * ((k & 1) ? 1.0 : 1.0595);
		}
		return (size_t)(SR * 0.52);
	}

	case SX_WELLDONE: {
		static const double NN[5] = { 523.3, 659.3, 783.9, 1046.5, 1318.5 };
		for (int k = 0; k < 5; k++)
			add_tone(st, total, (size_t)(k * SR * 0.085), NN[k], (size_t)(SR * (k == 4 ? 0.30 : 0.15)),
			         0.24, 0.003, 0.06, 0, -0.40 + 0.2 * k);
		add_kick(st, total, 0, 0.35, 0.0);
		return (size_t)(SR * 0.72);
	}

	case SX_WELLDONE2:
		for (int k = 0; k < 12; k++)
			add_tone(st, total, (size_t)(k * SR * 0.040), (k & 1) ? 1046.5 : 783.9,
			         (size_t)(SR * 0.038), 0.15, 0.001, 0.004, 3, 0.0);
		return (size_t)(SR * 0.50);

	case SX_HIT:
		add_kick(st, total, 0, 0.42, 0.0);
		add_noise(st, total, 0, 0.07, 0.10, 55.0, 0.0);
		return (size_t)(SR * 0.11);

	case SX_HIT2:
		/* PC speaker "knock": two periods of low square wave */
		add_tone(st, total, 0, 110.0, (size_t)(SR * 0.030), 0.20, 0.0005, 0.004, 3, 0.0);
		add_tone(st, total, (size_t)(SR * 0.030), 82.0, (size_t)(SR * 0.025), 0.16, 0.0005, 0.004, 3, 0.0);
		return (size_t)(SR * 0.06);

	case SX_OVER: {
		static const double NN[4] = { 392.0, 370.0, 349.2, 329.6 };
		for (int k = 0; k < 4; k++)
			add_tone(st, total, (size_t)(k * SR * 0.16), NN[k], (size_t)(SR * (k == 3 ? 0.30 : 0.15)),
			         0.20, 0.002, 0.05, g_style ? 3 : 1, 0.0);
		return (size_t)(SR * 0.78);
	}

	default: break;
	}
	return (size_t)(SR * 0.1);
}

/* Free effect channel (already finished), -1 if all are busy */
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

static void sfx_play(int type, int n)
{
	if (!g_ok || !g_on) return;

	int s = free_sfx_slot();
	if (s < 0) return;

	ndspChnWaveBufClear(s);
	/* memset => status = NDSP_WBUF_FREE (0). Do NOT write QUEUED here! */
	memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
	size_t len = gen_sfx(g_sfxBuf[s], type, n);
	if (len > SFX_MAX) len = SFX_MAX;
	if (len == 0) return;
	g_sbuf[s].data_pcm16 = g_sfxBuf[s];
	g_sbuf[s].nsamples   = len;
	g_sbuf[s].looping    = false;

	/* Flush of the whole buffer: 64-byte aligned and size a multiple of 8 */
	DSP_FlushDataCache(g_sfxBuf[s], SFX_MAX * 2 * sizeof(s16));
	ndspChnWaveBufAdd(s, &g_sbuf[s]);
}

/* ------------------------------------------------------------------ API */

void audio_init(void)
{
	if (g_ok) return;

	if (R_FAILED(ndspInit())) return;

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetOutputCount(2);
	ndspSetMasterVol(0.90f);

	for (int s = 0; s < NSFX; s++)
	{
		g_sfxBuf[s] = linearMemAlign(SFX_MAX * 2 * sizeof(s16), 0x40);
		if (!g_sfxBuf[s]) { ndspExit(); return; }
		memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
	}

	osc_init();

	static float s_mixStereo[12];
	s_mixStereo[0] = 1.0f;
	s_mixStereo[1] = 1.0f;
	for (int ch = 0; ch <= MUSIC_CH; ch++)
	{
		ndspChnReset(ch);
		ndspChnSetFormat(ch, NDSP_FORMAT_STEREO_PCM16);
		ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
		ndspChnSetRate(ch, SR);
		ndspChnSetMix(ch, s_mixStereo);
	}

	g_musicOk = music_init(MUSIC_CH);
	music_enable(g_music);

	g_ok = true;
	g_on = true;
}

void audio_exit(void)
{
	if (!g_ok) return;
	if (g_musicOk) music_exit();            /* join the thread BEFORE ndspExit */
	g_musicOk = false;
	for (int ch = 0; ch < NSFX; ch++)
	{
		ndspChnWaveBufClear(ch);
		memset(&g_sbuf[ch], 0, sizeof(ndspWaveBuf));
	}
	g_ok = false;
	g_on = false;
	ndspExit();
	for (int s = 0; s < NSFX; s++)
		if (g_sfxBuf[s]) { linearFree(g_sfxBuf[s]); g_sfxBuf[s] = NULL; }
}

bool audio_ok(void) { return g_ok; }

void audio_set_enable(bool enable) { g_on = enable; }
bool audio_enabled(void) { return g_on; }
void audio_set_style(int dos) { g_style = dos ? 1 : 0; }
void audio_set_lines(int n) { g_lines = n; }

void audio_blub(void)     { sfx_play(SX_BLUB, 0); }
void audio_wozz(void)     { sfx_play(SX_WOZZ, 0); }
void audio_tchh(void)     { sfx_play(SX_TCHH, 0); }

void audio_line(void)     { sfx_play(SX_LINE, g_lines); }
void audio_level(void)    { sfx_play(SX_LEVEL, 0); }
void audio_empty(void)    { sfx_play(SX_EMPTY, 0); }
void audio_welldone(void) { sfx_play(g_style ? SX_WELLDONE2 : SX_WELLDONE, 0); }

void audio_line2(void)    { sfx_play(SX_LINE2, g_lines); }
void audio_level2(void)   { sfx_play(SX_LEVEL2, 0); }
void audio_empty2(void)   { sfx_play(SX_EMPTY2, 0); }
void audio_welldone2(void){ sfx_play(SX_WELLDONE2, 0); }

void audio_hit(void)      { sfx_play(g_style ? SX_HIT2 : SX_HIT, 0); }
void audio_over(void)     { sfx_play(SX_OVER, 0); }

/* ------------------------------------------------------------------ music */

void audio_music(int song)
{
	if (!g_ok || !g_musicOk) return;
	music_play(song);
}

void audio_stop_music(void)
{
	if (!g_ok || !g_musicOk) return;
	music_play(-1);
}

void audio_music_enable(bool on)
{
	g_music = on;
	if (g_musicOk) music_enable(on);
}

bool audio_music_enabled(void) { return g_music; }

void audio_music_tempo(float mul) { if (g_musicOk) music_tempo(mul); }
void audio_music_duck(bool on)    { if (g_musicOk) music_duck(on); }
