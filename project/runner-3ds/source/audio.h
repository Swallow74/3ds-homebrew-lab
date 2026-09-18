#pragma once
#include <3ds.h>

/* Audio sintetizzato via NDSP (DSP::DSP service).
 * Buffer PCM in linear memory come richiesto dal DSP.
 * Pattern ispirato a devkitPro/3ds-examples/audio/streaming. */

void audio_init(void);
void audio_exit(void);
bool audio_ok(void);

void audio_set_music(bool on);
bool audio_music_on(void);

void audio_start(void);
void audio_move(void);
void audio_jump(void);
void audio_bonus(void);
void audio_crash(void);
