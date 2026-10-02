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
#pragma once
#include <3ds.h>
#include "music.h"   /* MUS_TITLE, MUS_RUN, MUS_OVER */

/* NDSP-synthesized audio (DSP::DSP service).
 * PCM buffers in linear memory as required by the DSP.
 * Pattern inspired by devkitPro/3ds-examples/audio/streaming. */

void audio_init(void);
void audio_exit(void);
bool audio_ok(void);

void audio_set_music(bool on);
bool audio_music_on(void);   /* user preference */
void audio_music(int song);  /* MUS_*: changes song with a fade */
void audio_duck(bool on);    /* music at reduced volume (pause) */

void audio_start(void);
void audio_move(void);
void audio_jump(void);
void audio_bonus(void);
void audio_crash(void);
void audio_coin(int step);   /* step = position in the chain: raises the pitch */
void audio_power(void);
void audio_shield(void);
void audio_select(void);
void audio_go(bool last);    /* countdown beep, last = "GO" */
