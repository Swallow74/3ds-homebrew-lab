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

/* Audio sintetizzato via NDSP (DSP::DSP service).
 * Buffer PCM in linear memory come richiesto dal DSP.
 * Pattern ispirato a devkitPro/3ds-examples/audio/streaming. */

void audio_init(void);
void audio_exit(void);
bool audio_ok(void);

void audio_set_music(bool on);
bool audio_music_on(void);   /* preferenza dell'utente */
void audio_music(int song);  /* MUS_*: cambia brano con dissolvenza */
void audio_duck(bool on);    /* musica a volume ridotto (pausa) */

void audio_start(void);
void audio_move(void);
void audio_jump(void);
void audio_bonus(void);
void audio_crash(void);
void audio_coin(int step);   /* step = posizione nella catena: alza il tono */
void audio_power(void);
void audio_shield(void);
void audio_select(void);
void audio_go(bool last);    /* bip del conto alla rovescia, last = "GO" */
