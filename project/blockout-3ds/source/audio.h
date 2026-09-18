#pragma once
#include <3ds.h>

/* Sintetizzatore NDSP con la stessa "agenda" di eventi di SoundManager di
 * BlockOut II 2.5 (GPL).  L'originale riproduce file .wav/.mod via SDL_mixer:
 * sul 3DS non ci sono le risorse, quindi gli effetti sono sintetizzati, ma
 * gli eventi e i loro nomi sono identici a SoundManager.h.
 *
 * Regole NDSP (libctru/source/ndsp/ndsp-channel.c):
 *   - ndspChnWaveBufAdd() ignora il buffer se status==QUEUED/PLAYING o se
 *     nsamples==0: memset(wavebuf,0,...) (= NDSP_WBUF_FREE) PRIMA di ogni
 *     ndspChnWaveBufAdd().  MAI pre-impostare QUEUED.
 *   - UN ndspWaveBuf DEDICATO per canale (condividerlo corrompe buf->next).
 *   - Buffer PCM in linear memory (linearMemAlign(size,0x40)) +
 *     DSP_FlushDataCache() prima di accodare.
 */

# ifdef __cplusplus
extern "C" {
# endif

void audio_init(void);
void audio_exit(void);
bool audio_ok(void);

/* SoundManager::SetEnable */
void audio_set_enable(bool enable);
bool audio_enabled(void);

/* Suoni di menu */
void audio_blub(void);            /* cambio pagina / modifica opzione */
void audio_wozz(void);            /* il cursore "entra" */
void audio_tchh(void);            /* ritorno / movimento nel menu */

/* Suoni di gioco (SOUND_BLOCKOUT2) */
void audio_line(void);
void audio_level(void);
void audio_empty(void);
void audio_welldone(void);

/* Suoni di gioco (SOUND_BLOCKOUT) */
void audio_line2(void);
void audio_level2(void);
void audio_empty2(void);
void audio_welldone2(void);

/* Pezzo fermo contro un blocco (StartSpark) */
void audio_hit(void);

/* Musica della pagina Credits (loop) */
void audio_music(void);
void audio_stop_music(void);

# ifdef __cplusplus
}
# endif
