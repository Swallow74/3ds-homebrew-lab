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
/* Music: real-time sequencer + synthesizer (Mega Drive /
 * '80s-'90s arcade style).  No pre-rendered PCM: the songs are
 * compact scores (chords + melody) expanded on the fly into FM bass, two-
 * oscillator lead with glide and vibrato, FM bell, pulse arpeggio,
 * filtered stereo pad, synthetic drums and ping-pong echo.
 *
 * On the 3DS it runs in its own thread woken by the NDSP callback and fills
 * a few short buffers in rotation on a single channel: ~12 KB of linear memory
 * instead of the MBs of pre-computed loops, and songs a minute long. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { MUS_TITLE, MUS_RUN, MUS_OVER, MUS_NUM };

bool music_init(int channel);    /* after ndspInit() */
void music_exit(void);           /* before ndspExit() */
void music_play(int song);       /* song change with a short fade */
void music_enable(bool on);      /* on/off with a fade (resumes from there) */
void music_duck(bool on);        /* reduced volume (pause) */

/* pure engine, also used by the PC test (MUSIC_HOST) */
void music_synth_init(void);
void music_render(int16_t *out, int frames);   /* stereo interleaved */
int  music_check(void);          /* 0 = scores consistent */
