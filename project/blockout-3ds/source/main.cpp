/*
  File:        main.cpp
  Description: 3DS entry point (hardware startup, input, menus, game loop)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Adaptations required by the 3DS hardware (documented):
   * the gamepad replaces the keyboard: D-pad/circle pad (and D-pad + L
     for the historical 1/3, 3/1, 7/9 keypad diagonals) move the
     piece, A drops it, B/X/Y rotate around Z/X/Y (with R the reverse
     rotations), START pause/confirm, SELECT aborts;
   * the key codes passed to Game::Process() stay the historical ones
     (KEY_UP..KEY_PAGEDOWN + Q W E / A S D for the rotations), so the
     original HandleKey() works unchanged;
   * menus and the configuration page are drawn with the software renderer
     (the original used the SDL/OpenGL pages);
   * adjustable stereoscopic depth (ZR), practice hint (ZL);
   * real-time soundtrack (music.c): menu, game, game over.
*/

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/* Button constants (the libctru names are translated here into HK_*:
   the original's key codes live in bo_compat.h with the BO_KEY_ prefix,
   because the hid.h enum uses the same names KEY_A/KEY_L/KEY_UP...). */
static const u32 HK_A      = KEY_A;
static const u32 HK_B      = KEY_B;
static const u32 HK_X      = KEY_X;
static const u32 HK_Y      = KEY_Y;
static const u32 HK_L      = KEY_L;
static const u32 HK_R      = KEY_R;
static const u32 HK_ZL     = KEY_ZL;
static const u32 HK_ZR     = KEY_ZR;
static const u32 HK_START  = KEY_START;
static const u32 HK_SELECT = KEY_SELECT;
static const u32 HK_UP     = KEY_UP;      /* D-pad or circle pad */
static const u32 HK_DOWN   = KEY_DOWN;
static const u32 HK_LEFT   = KEY_LEFT;
static const u32 HK_RIGHT  = KEY_RIGHT;
static const u32 HK_CST_UP = KEY_CSTICK_UP;

#include "render.h"
#include "Game.h"
#include "SetupManager.h"
#include "SoundManager.h"
#include "audio.h"
#include "music.h"
#include "screens.h"
#include "autotest.h"

#define TOPW  400
#define TOTH  240

static SetupManager setupManager;
static SoundManager soundManager;
static Game         game;

static BYTE  keys[BO_KEY_LAST];
static u64   tickBase = 0;
static u32   hkPressed = 0;
static u32   hkHeld    = 0;

static float getTime(void) {
  return (float)((double)(svcGetSystemTick() - tickBase) / (double)SYSCLOCK_ARM11);
}

static void pollInput(void) {
  hidScanInput();
  hkHeld    = hidKeysHeld();
  hkPressed = hidKeysDown();
}

/* Applies the Setup options to the port's modules */
static void applySetup(void) {
  soundManager.SetEnable(setupManager.GetSound() ? TRUE : FALSE);
  audio_set_enable(setupManager.GetSound() ? true : false);
  audio_set_style(setupManager.GetSoundType() == SOUND_BLOCKOUT);
  audio_music_enable(setupManager.GetMusic() ? true : false);
  render_set_stereo(stereoLevelPx(setupManager.GetStereo()));
  render_set_piece_fill(setupManager.GetPieceFill());
}

/* ------------------------------------------------------------------ */
/* Gamepad -> original key codes translation                           */
/* ------------------------------------------------------------------ */

/* keys[] is read by Game::HandleKey(), which clears the consumed entries:
   exactly the behavior of the original keyboard (single press =
   one column). On a PC the repeat is provided by the OS (~2-3 Hz after an initial
   delay); here fillGameKeys() used to receive hkHeld and moved the piece EVERY FRAME
   at 60 Hz: even a short tap (~5 frames) crossed the whole pit.
   Now: immediate step on hkPressed + repeat with an initial delay if
   the key stays held. */
#define MOVE_DELAY_INITIAL 0.22f   /* s before the repeat starts */
#define MOVE_DELAY_REPEAT  0.11f   /* s between one step and the next while held */
#define ROT_DELAY_INITIAL  0.25f
#define ROT_DELAY_REPEAT   0.15f

static float moveNext[4] = { 0, 0, 0, 0 };  /* UP DOWN LEFT RIGHT */
static float rotNext[3]  = { 0, 0, 0 };     /* B X Y */

static bool repeatAllow(float *slot, u32 bit, u32 held, u32 pressed,
                        float now, float dInit, float dRep) {
  if (!(held & bit)) { *slot = 0.0f; return false; }
  if (pressed & bit) { *slot = now + dInit; return true; }
  if (*slot == 0.0f) *slot = now + dInit;
  if (now >= *slot) { *slot = now + dRep; return true; }
  return false;
}

static void fillGameKeys(u32 held, u32 pressed, float now) {

  int combo = (held & HK_L) ? 1 : 0;

  if (repeatAllow(&moveNext[0], HK_UP, held, pressed, now,
                  MOVE_DELAY_INITIAL, MOVE_DELAY_REPEAT))
    keys[combo ? BO_KEY_HOME : BO_KEY_UP] = 1;
  if (repeatAllow(&moveNext[1], HK_DOWN, held, pressed, now,
                  MOVE_DELAY_INITIAL, MOVE_DELAY_REPEAT))
    keys[combo ? BO_KEY_END : BO_KEY_DOWN] = 1;
  if (repeatAllow(&moveNext[2], HK_LEFT, held, pressed, now,
                  MOVE_DELAY_INITIAL, MOVE_DELAY_REPEAT))
    keys[combo ? BO_KEY_PAGEUP : BO_KEY_LEFT] = 1;
  if (repeatAllow(&moveNext[3], HK_RIGHT, held, pressed, now,
                  MOVE_DELAY_INITIAL, MOVE_DELAY_REPEAT))
    keys[combo ? BO_KEY_PAGEDOWN : BO_KEY_RIGHT] = 1;

  /* fall: only on press, not while held (otherwise holding A
     would instantly drop the next piece as well) */
  if (pressed & HK_A) keys[BO_KEY_SPACE] = 1;

  /* rotations: the historical codes Q/W/E (Rx1 Ry1 Rz1) and A/S/D (Rx2 Ry2 Rz2),
     read by Game::HandleKey() through SetupManager. With R held only the
     REVERSE rotation applies (before, both were set and the
     direct one always won, making R useless). */
  int inv = (held & HK_R) ? 1 : 0;
  if (repeatAllow(&rotNext[0], HK_B, held, pressed, now,
                  ROT_DELAY_INITIAL, ROT_DELAY_REPEAT))
    keys[inv ? setupManager.GetKRz2() : setupManager.GetKRz1()] = 1;
  if (repeatAllow(&rotNext[1], HK_X, held, pressed, now,
                  ROT_DELAY_INITIAL, ROT_DELAY_REPEAT))
    keys[inv ? setupManager.GetKRx2() : setupManager.GetKRx1()] = 1;
  if (repeatAllow(&rotNext[2], HK_Y, held, pressed, now,
                  ROT_DELAY_INITIAL, ROT_DELAY_REPEAT))
    keys[inv ? setupManager.GetKRy2() : setupManager.GetKRy1()] = 1;

  /* practice-mode hint: only on press */
  if (pressed & HK_CST_UP) keys['H'] = 1;
}

enum { ACT_NONE = 0, ACT_PLAY, ACT_PRACTICE, ACT_DEMO };

/* ------------------------------------------------------------------ */
/* Game                                                                */
/* ------------------------------------------------------------------ */

/* Returns 1 if the user wants to leave the app */
static int runGamePlay(int act) {

  int leave = 0;

  for (;;) {

    int retry = 0;
    int exitValue = 0;
    float fTime = getTime();

    switch (act) {
      case ACT_PLAY:     game.StartGame(TOPW, TOTH, fTime);     break;
      case ACT_PRACTICE: game.StartPractice(TOPW, TOTH, fTime); break;
      case ACT_DEMO:     game.StartDemo(TOPW, TOTH, fTime);     break;
      default: return 0;
    }
    int lastMode = game.GetGameMode();
    audio_music(MUS_GAME);

    while (aptMainLoop()) {

      pollInput();

      fTime = getTime();
      memset(keys, 0, sizeof(keys));
      fillGameKeys(hkHeld, hkPressed, fTime);
#if AUTOTEST
      {
        static float atOver = 0.0f;
        if (game.GetGameMode() == GAME_OVER) {
          if (atOver == 0.0f) atOver = fTime;
          else if (fTime - atOver > 9.0f) { keys[BO_KEY_RETURN] = 1; atOver = 0.0f; }
        } else atOver = 0.0f;
      }
      /* random input: fills the pit quickly (graphics test) */
      if (act != ACT_DEMO && game.GetGameMode() == GAME_PLAYING) {
        static float atNext = 0.0f;
        static int atPaused = 0;
        if (fTime >= atNext) {
          static const int kk[] = { BO_KEY_UP, BO_KEY_DOWN, BO_KEY_LEFT, BO_KEY_RIGHT, 'Q', 'W', 'E', BO_KEY_SPACE };
          keys[kk[rand() % 8]] = 1;
          atNext = fTime + 0.18f;
          if (act == ACT_PRACTICE && (rand() % 6) == 0) keys['H'] = 1;
        }
        if (!atPaused && fTime - game.GetScore()->gameTime > 0.0f && (rand() % 900) == 0) {
          atPaused = 1;
          keys['P'] = 1;
        }
      }
#endif

      /* START: pause/resume, back to the menu at the end of a game */
      if (hkPressed & HK_START) {
        int gm = game.GetGameMode();
        if (gm == GAME_PLAYING || gm == GAME_PAUSED) keys['P'] = 1;
        else if (gm == GAME_DEMO) keys[BO_KEY_ESCAPE] = 1;   /* START stops the demo */
        else keys[BO_KEY_RETURN] = 1;
      }
      /* SELECT: aborts the game (like the original ESC) */
      if (hkPressed & HK_SELECT) keys[BO_KEY_ESCAPE] = 1;

      /* ZL: practice hint */
      if (hkPressed & HK_ZL) keys['H'] = 1;

      /* ZR: stepped stereoscopic depth (OFF, 1..4), saved */
      if (hkPressed & HK_ZR) {
        setupManager.SetStereo((setupManager.GetStereo() + 1) % 5);
        render_set_stereo(stereoLevelPx(setupManager.GetStereo()));
        audio_blub();
      }

      /* soundtrack: game track, jingle at the end */
      int gmNow = game.GetGameMode();
      if (gmNow == GAME_OVER && lastMode != GAME_OVER) audio_music(MUS_OVER);
      lastMode = gmNow;

      int exitValue2 = game.Process(keys, fTime);

      render_clear(COL_BG);
      game.Render();

      /* The game froze by itself: the pause menu appears above the stopped pit
         (touch a row on the bottom screen).  Resuming
         sends 'P' again: Process() recovers the pause time from the
         timestamps, as in the original. */
      if (exitValue2 == 0 && game.GetGameMode() == GAME_PAUSED) {
        render_swap(true);
        int pr = runPauseMenu(&game);
        if (pr == 0) {
          /* exits like the original ESC: resumes and aborts, so
             Game records the time and chooses 1 (game) or 2 (practice) */
          memset(keys, 0, sizeof(keys));
          keys['P'] = 1;
          game.Process(keys, getTime());
          memset(keys, 0, sizeof(keys));
          keys[BO_KEY_ESCAPE] = 1;
          exitValue = game.Process(keys, getTime());
          if (exitValue == 0) exitValue = (act == ACT_PLAY) ? 1 : 2;
          break;
        }
        if (pr == 2) {
          if (act == ACT_PRACTICE) game.StartPractice(TOPW, TOTH, getTime());
          else game.StartGame(TOPW, TOTH, getTime());
          audio_music(MUS_GAME);
          continue;
        }
        memset(keys, 0, sizeof(keys));
        keys['P'] = 1;
        game.Process(keys, getTime());
        continue;
      }

      render_swap(true);

      if (exitValue2 != 0) { exitValue = exitValue2; break; }
      if (!game.GetInited()) break;
    }

    /* as in BlockOut.cpp:171 - only a real game enters the ranking */
    if (exitValue == 1) {
      SCOREREC *added = NULL;
      game.GetScore()->date = (uint32_t)time(NULL);
      int pos = setupManager.InsertHighScore(game.GetScore(), &added);
      if (added != NULL) {
        if (setupManager.GetSoundType() == SOUND_BLOCKOUT) audio_welldone2();
        else audio_welldone();
      }

      int r = runGameOverScreen(&game, &setupManager, added, pos);
      setupManager.SaveHighScore();

      if (r == 1) retry = 1;        /* restart the same game */
      if (r == 0) leave = 1;        /* exit */
    }

    if (!retry || leave) break;
  }

  return leave;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {

  aptInit();
  gfxInitDefault();
  hidInit();
  osSetSpeedupEnable(true);

  render_init();
  soundManager.Create();           /* audio_init: NDSP + music thread */
  applySetup();

  game.SetSetupManager(&setupManager);
  game.SetSoundManager(&soundManager);
  /* as in BlockOut.cpp: Create() only once at startup */
  game.Create(TOPW, TOTH);

  runIntroScreen();

  tickBase = svcGetSystemTick();

  bool running = true;
  while (running && aptMainLoop()) {

    int act = runMenuScreen(&game, &setupManager);

    switch (act) {
      case SCR_PLAY:     if (runGamePlay(ACT_PLAY)) running = false; break;
      case SCR_PRACTICE: if (runGamePlay(ACT_PRACTICE)) running = false; break;
      case SCR_DEMO:     if (runGamePlay(ACT_DEMO)) running = false; break;
      case SCR_SETUP:
        runSetupScreen(&game, &setupManager, &soundManager);
        applySetup();
        break;
      case SCR_HISCORE:
        runHiScoreScreen(&setupManager);
        break;
      case SCR_EXIT:
      default:
        running = false;
        break;
    }
  }

  setupManager.WriteSetup();
  setupManager.SaveHighScore();

  audio_exit();
  render_exit();

  hidExit();
  gfxExit();
  aptExit();

  return 0;
}
