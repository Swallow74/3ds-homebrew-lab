#pragma once
#include <3ds.h>

/* NDSP synthesizer with the same event "schedule" as SoundManager of
 * BlockOut II 2.5 (GPL).  The original plays .wav/.mod files via SDL_mixer:
 * on the 3DS the resources are not there, so the effects are synthesized, but
 * the events and their names are identical to SoundManager.h.
 *
 * NDSP rules (libctru/source/ndsp/ndsp-channel.c):
 *   - ndspChnWaveBufAdd() ignores the buffer if status==QUEUED/PLAYING or if
 *     nsamples==0: memset(wavebuf,0,...) (= NDSP_WBUF_FREE) BEFORE every
 *     ndspChnWaveBufAdd().  NEVER pre-set QUEUED.
 *   - ONE DEDICATED ndspWaveBuf per channel (sharing it corrupts buf->next).
 *   - PCM buffers in linear memory (linearMemAlign(size,0x40)) +
 *     DSP_FlushDataCache() before queueing.
 */

# ifdef __cplusplus
extern "C" {
# endif

void audio_init(void);
void audio_exit(void);
bool audio_ok(void);

/* Effects on/off (SoundManager::SetEnable) */
void audio_set_enable(bool enable);
bool audio_enabled(void);

/* Effects style: 0 = BlockOut II, 1 = BlockOut DOS (square wave) */
void audio_set_style(int dos);
/* Layers completed by the last piece (1..5): used by audio_line*() */
void audio_set_lines(int n);

/* Menu sounds */
void audio_blub(void);            /* value change */
void audio_wozz(void);            /* confirm */
void audio_tchh(void);            /* cursor movement */

/* Game sounds (SOUND_BLOCKOUT2) */
void audio_line(void);
void audio_level(void);
void audio_empty(void);
void audio_welldone(void);

/* Game sounds (SOUND_BLOCKOUT) */
void audio_line2(void);
void audio_level2(void);
void audio_empty2(void);
void audio_welldone2(void);

/* Piece resting against a block (StartSpark) / game over */
void audio_hit(void);
void audio_over(void);

/* Real-time music (music.c): MUS_TITLE / MUS_GAME / MUS_OVER */
void audio_music(int song);
void audio_stop_music(void);
void audio_music_enable(bool on);
bool audio_music_enabled(void);
void audio_music_tempo(float mul);
void audio_music_duck(bool on);

# ifdef __cplusplus
}
# endif
