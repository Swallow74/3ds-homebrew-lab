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
 * MUSICA: 8 battute (64 ottavi, ~13.7 s) per traccia, due tracce.  Ogni
 * traccia e' in due sezioni: A (battute 0-3, arpeggio registro basso) e B
 * (battute 4-7, arpeggio in registro alto + armonica di terza, basso con
 * rimbusso, cassa in piu', piatti su tutti gli ottavi).  In piu' il pad
 * dell'accordo (3 note lunghe), un shimmer d'ottava e un fill di tom+rullante
 * negli ultimi tre ottavi di ogni mezzo giro.  Gli strati sono spostati a
 * sinistra/destra (pan) per avere un mix largo: lead centro-sinistra,
 * shimmer e piatti a destra, pad e armonica spalleggiati, cassa al centro.
 *
 * OSCILLATORE: tabella di 4096 punti con interpolazione lineare, invece di
 * sinf/asin chiamate per campione (lento su ARM11).  Le decadere sono rese
 * con un moltiplicatore per campione invece di exp() per campione: generare
 * ~3.6 milioni di campioni costa qualche decina di millisecondi, non secondi.
 */

#include "audio.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SR            22050                 /* SampleRate */
#define CH_MUSIC      0
#define NSFX          4                     /* canali effetti: 1..4 */
#define CH_SFX_BASE   1
#define BPM           140
#define EIGHTH        ((double)SR * 60.0 / (double)BPM / 2.0)  /* ~4725 frames */
#define MEL_NOTES     64                    /* ottavi del loop: 8 battute */
#define MUSIC_FRAMES  ((size_t)(MEL_NOTES * EIGHTH))
#define SFX_MAX       ((size_t)(SR * 0.45)) /* frames disponibili per effetto */

static bool     g_ok = false;
static bool     g_music = false;
static int      g_track = 0;

static s16        *g_musicBuf[2];
static s16        *g_sfxBuf[NSFX];
static ndspWaveBuf g_mbuf[2];       /* un wavebuf per traccia: 0 e 1 */
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

static double freq_of(int semitoni)
{
	return 440.0 * pow(2.0, (double)semitoni / 12.0);
}

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

/* ------------------------------------------------------------------ musica */

/* Triadi: radice in semitoni dal LA (A = -12 = LA sotto il middle C). */
typedef struct { s8 root; s8 third; s8 fifth; } Chord;
#define CH_MIN(r) { r, 3, 7 }
#define CH_MAJ(r) { r, 4, 7 }

/* Traccia 0: Am F C G | Am F G Am    Traccia 1: Dm Bb F C | F C Bb Am */
static const Chord prog0[8] = {
	CH_MIN(-12), CH_MAJ(-4), CH_MAJ(-9), CH_MAJ(-2),
	CH_MIN(-12), CH_MAJ(-4), CH_MAJ(-2), CH_MIN(-12),
};
static const Chord prog1[8] = {
	CH_MIN(-7),  CH_MAJ(-2), CH_MAJ(-4), CH_MAJ(-9),
	CH_MAJ(-4),  CH_MAJ(-9), CH_MAJ(-2), CH_MIN(-12),
};

/* Contorni d'arpeggio sugli 8 ottavi della battuta: sezione A in registro
 * basso, sezione B un'ottava piu' su' (piu' energia, si sente il cambio). */
static const s8 arpA[8] = { 0, 3, 7, 12, 12, 7, 3, 0 };
static const s8 arpM[8] = { 0, 4, 7, 12, 12, 7, 4, 0 };
static const s8 brpA[8] = { 12, 15, 19, 15, 12, 7, 3, 0 };
static const s8 brpM[8] = { 12, 16, 19, 16, 12, 7, 4, 0 };

static void render_track(s16 *st, int track)
{
	const size_t total = MUSIC_FRAMES;
	const Chord *prog = track ? prog1 : prog0;

	memset(st, 0, total * 2 * sizeof(s16));

	for (int i = 0; i < MEL_NOTES; i++)
	{
		const int bar = i / 8, s = i % 8;
		const Chord *c = &prog[bar];
		const int secB = (bar >= 4);
		const int min  = (c->third == 3);
		const s8 *arp = secB ? (min ? brpA : brpM) : (min ? arpA : arpM);
		const size_t start = (size_t)(i * EIGHTH);
		if (start >= total) break;

		const int note = c->root + arp[s];

		/* lead (centro-leggero sinistra) + shimmer d'ottava (destra) */
		add_tone(st, total, start, freq_of(note),
		         (size_t)(EIGHTH * (secB ? 0.72 : 0.80)),
		         0.235, 0.004, 0.030, 0, -0.25);
		add_tone(st, total, start, freq_of(note + 12),
		         (size_t)(EIGHTH * 0.40), secB ? 0.085 : 0.068,
		         0.004, 0.020, 2, 0.45);

		/* armonica di terza sopra il lead: solo sezione B, spalleggiata */
		if (secB)
			add_tone(st, total, start, freq_of(note + c->third),
			         (size_t)(EIGHTH * 0.45), 0.075, 0.004, 0.030, 0, 0.60);

		/* pad dell'accordo: tre note lunghe, attaccate al primo ottavo */
		if (s == 0) {
			static const double padPan[3] = { -0.40, 0.15, 0.55 };
			s8 n3[3];
			n3[0] = (s8)(c->root - 12);
			n3[1] = (s8)(c->root - 12 + c->third);
			n3[2] = (s8)(c->root - 12 + c->fifth);
			for (int k = 0; k < 3; k++)
				add_tone(st, total, start, freq_of(n3[k]),
				         (size_t)(EIGHTH * 7.4), secB ? 0.050 : 0.038,
				         0.06, 0.25, 1, padPan[k]);
		}

		/* basso sulle crome, con rimbusso d'ottava in coda a sezione B */
		if (i % 2 == 0)
			add_tone(st, total, start, freq_of(c->root - 24),
			         (size_t)(EIGHTH * 1.6), 0.27, 0.006, 0.060, 1, 0.0);
		if (secB && s == 7)
			add_tone(st, total, start, freq_of(c->root - 12),
			         (size_t)(EIGHTH * 0.55), 0.14, 0.004, 0.030, 1, -0.30);

		/* batteria: A = cassa su 1 e 3, rullante su 2 e 4, hi-hat in levare;
		 * B = in piu' la cassa sull'ultimo ottavo, ghost note e hi-hat su
		 * tutti gli ottavi (accentsi in levare) */
		if (!secB) {
			if (s == 0 || s == 4) add_kick(st, total, start, 0.40, 0.0);
			if (s == 2 || s == 6)
				add_noise(st, total, start, 0.06, 0.13, 70.0, 0.25);
			if (s % 2 == 1)
				add_noise(st, total, start, 0.025, 0.075, 190.0, 0.55);
		} else {
			if (s == 0 || s == 4) add_kick(st, total, start, 0.42, 0.0);
			if (s == 7)           add_kick(st, total, start, 0.24, -0.20);
			if (s == 2 || s == 6)
				add_noise(st, total, start, 0.07, 0.155, 65.0, 0.25);
			if (s == 7)
				add_noise(st, total, start, 0.05, 0.055, 120.0, 0.35);
			if (s % 2 == 1)
				add_noise(st, total, start, 0.025, 0.085, 190.0, 0.55);
			else
				add_noise(st, total, start, 0.020, 0.042, 260.0, 0.55);
		}

		/* fill di tom + rullante negli ultimi tre ottavi di ogni mezzo giro */
		if ((i % 32) >= 29) {
			add_sweep(st, total, start,
			          320.0 - 40.0 * (double)(i % 32 - 29), 150.0, 0.07,
			          0.18, 1, 0.20);
			add_noise(st, total, start, 0.04, 0.10, 90.0, 0.30);
		}

		/* piatto in apertura di ogni meta' del loop */
		if (i == 0 || i == 32)
			add_noise(st, total, start, 0.18, 0.09, 22.0, 0.50);
	}
}

/* ------------------------------------------------------------------ effetti */

enum { SX_MOVE, SX_JUMP, SX_BONUS, SX_CRASH, SX_START, SX_NUM };

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

	default: break;
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

static void sfx_play(int type)
{
	if (!g_ok) return;

	int s = free_sfx_slot();
	if (s < 0) return;

	int ch = CH_SFX_BASE + s;

	/* Il canale e' fermo: svuotarlo per ripulire lo stato DSP del canale. */
	ndspChnWaveBufClear(ch);

	/* memset => status = NDSP_WBUF_FREE (0). NON scrivere QUEUED qui! */
	memset(&g_sbuf[s], 0, sizeof(ndspWaveBuf));
	gen_sfx(g_sfxBuf[s], type);
	g_sbuf[s].data_pcm16 = g_sfxBuf[s];
	g_sbuf[s].nsamples   = (type == SX_CRASH) ? (size_t)(SR * 0.36) :
	                       (type == SX_BONUS) ? (size_t)(SR * 0.28) :
	                       (type == SX_START) ? (size_t)(SR * 0.42) :
	                       (type == SX_JUMP)  ? (size_t)(SR * 0.16) :
	                                            (size_t)(SR * 0.08);
	g_sbuf[s].looping = false;

	/* Flush dell'intero buffer: 64 byte allineato e size multipla di 8,
	 * cosi' la cache del DSP non lascia righe parziali non scritte. */
	DSP_FlushDataCache(g_sfxBuf[s], SFX_MAX * 2 * sizeof(s16));
	ndspChnWaveBufAdd(ch, &g_sbuf[s]);
}

/* ------------------------------------------------------------------ API */

static void music_start_now(void)
{
	int t = g_track;

	ndspChnWaveBufClear(CH_MUSIC);

	memset(&g_mbuf[t], 0, sizeof(ndspWaveBuf));
	g_mbuf[t].data_pcm16 = g_musicBuf[t];
	g_mbuf[t].nsamples   = MUSIC_FRAMES;
	g_mbuf[t].looping    = true;      /* loop ininterrotto gestito dal DSP */

	ndspChnSetRate(CH_MUSIC, SR);
	ndspChnSetMix(CH_MUSIC, s_mixStereo);
	ndspChnWaveBufAdd(CH_MUSIC, &g_mbuf[t]);
}

void audio_init(void)
{
	if (g_ok) return;

	if (R_FAILED(ndspInit())) return;

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetOutputCount(2);
	ndspSetMasterVol(0.85f);

	for (int t = 0; t < 2; t++)
		g_musicBuf[t] = linearMemAlign(MUSIC_FRAMES * 2 * sizeof(s16), 0x40);
	for (int s = 0; s < NSFX; s++)
		g_sfxBuf[s] = linearMemAlign(SFX_MAX * 2 * sizeof(s16), 0x40);

	if (!g_musicBuf[0] || !g_musicBuf[1])
	{ ndspExit(); return; }
	for (int s = 0; s < NSFX; s++)
		if (!g_sfxBuf[s]) { ndspExit(); return; }

	osc_init();                       /* prima di generare qualsiasi PCM */
	render_track(g_musicBuf[0], 0);
	render_track(g_musicBuf[1], 1);
	for (int t = 0; t < 2; t++)
	{
		DSP_FlushDataCache(g_musicBuf[t], MUSIC_FRAMES * 2 * sizeof(s16));

		memset(&g_mbuf[t], 0, sizeof(ndspWaveBuf));
		g_mbuf[t].data_pcm16 = g_musicBuf[t];
		g_mbuf[t].nsamples   = MUSIC_FRAMES;
		g_mbuf[t].looping    = true;
	}

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
	g_music = false;
	g_track = 0;
}

void audio_exit(void)
{
	if (!g_ok) return;
	g_ok = false;
	g_music = false;
	ndspExit();
}

bool audio_ok(void) { return g_ok; }

void audio_set_music(bool on)
{
	if (!g_ok || on == g_music) return;
	g_music = on;

	if (on)
		music_start_now();
	else
		ndspChnWaveBufClear(CH_MUSIC);
}

bool audio_music_on(void) { return g_music; }

void audio_start(void)
{
	if (!g_ok) return;
	sfx_play(SX_START);
	if (!g_music) { g_music = true; music_start_now(); }
}

void audio_move(void)  { sfx_play(SX_MOVE); }
void audio_jump(void)  { sfx_play(SX_JUMP); }
void audio_bonus(void) { sfx_play(SX_BONUS); }
void audio_crash(void) { sfx_play(SX_CRASH); }
