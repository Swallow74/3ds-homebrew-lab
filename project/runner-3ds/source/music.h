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
/* Music: sequencer + sintetizzatore in tempo reale (stile Mega Drive /
 * arcade anni '80-'90).  Niente PCM pre-renderizzato: le canzoni sono
 * spartiti compatti (accordi + melodia) espansi al volo in basso FM, lead
 * a due oscillatori con glide e vibrato, campana FM, arpeggio a impulsi,
 * pad stereo filtrato, batteria sintetica ed eco ping-pong.
 *
 * Sul 3DS gira in un thread proprio svegliato dalla callback NDSP e riempie
 * a rotazione pochi buffer corti su un solo canale: ~12 KB di linear memory
 * invece dei MB dei loop pre-calcolati, e canzoni lunghe un minuto. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { MUS_TITLE, MUS_RUN, MUS_OVER, MUS_NUM };

bool music_init(int channel);    /* dopo ndspInit() */
void music_exit(void);           /* prima di ndspExit() */
void music_play(int song);       /* cambio brano con dissolvenza breve */
void music_enable(bool on);      /* on/off con dissolvenza (riprende da li') */
void music_duck(bool on);        /* volume ridotto (pausa) */

/* motore puro, usato anche dal test su PC (MUSIC_HOST) */
void music_synth_init(void);
void music_render(int16_t *out, int frames);   /* stereo interleaved */
int  music_check(void);          /* 0 = spartiti coerenti */
