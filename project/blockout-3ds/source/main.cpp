/*
  File:        main.cpp
  Description: Entry point 3DS (avvio hardware, input, menu, loop di gioco)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Adattamenti richiesti dall'hardware 3DS (documentati):
   * il gamepad sostituisce la tastiera: crostiera/levetta (e crostiera + L
     per le diagonali storiche 1/3, 3/1, 7/9 del tastierino) muovono il
     pezzo, A lo fa cadere, B/X/Y ruotano attorno a Z/X/Y (con R le rotazioni
     inverse), START pausa/conferma, SELECT interrompe;
   * i codici tasto passati a Game::Process() restano quelli storici
     (KEY_UP..KEY_PAGEDOWN + Q W E / A S D per le rotazioni), cosi' il
     HandleKey() dell'originale funziona immutato;
   * menu e pagina di configurazione disegnati con il renderer software
     (l'originale usava le pagine SDL/OpenGL);
   * profondita' stereoscopica regolabile (ZR), audio regolabile (L+R+START).
*/

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

/* Costanti dei pulsanti (i nomi di libctru vengono qui tradotti in HK_*:
   i codici tasto dell'originale vivono in bo_compat.h con prefisso BO_KEY_,
   perche' l'enum di hid.h usa gli stessi nomi KEY_A/KEY_L/KEY_UP...). */
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
static const u32 HK_UP     = KEY_UP;      /* crostiera o levetta */
static const u32 HK_DOWN   = KEY_DOWN;
static const u32 HK_LEFT   = KEY_LEFT;
static const u32 HK_RIGHT  = KEY_RIGHT;
static const u32 HK_DUP    = KEY_DUP;     /* sola crostiera (menu) */
static const u32 HK_DDOWN  = KEY_DDOWN;
static const u32 HK_DLEFT  = KEY_DLEFT;
static const u32 HK_DRIGHT = KEY_DRIGHT;
static const u32 HK_CST_UP = KEY_CSTICK_UP;

#include "render.h"
#include "Game.h"
#include "SetupManager.h"
#include "SoundManager.h"
#include "audio.h"

/* Diagnostica di avvio: barre colorate + "BOOT OK" per 2.2 s.
   Metti 0 per rimuoverla dalla build finale. */
#define BOOT_SELFTEST 1

#define TOPW  400
#define TOTH  240
#define BOTW  320

static SetupManager setupManager;
static SoundManager soundManager;
static Game         game;

static BYTE  keys[BO_KEY_LAST];
static float stereoPx = 7.5f;

static u64   tickBase = 0;

static u32 hkPressed = 0;
static u32 hkHeld    = 0;

static float getTime(void) {
  return (float)((double)(svcGetSystemTick() - tickBase) / (double)SYSCLOCK_ARM11);
}

static void pollInput(void) {
  hidScanInput();
  hkHeld    = hidKeysHeld();
  hkPressed = hidKeysDown();
}

/* ------------------------------------------------------------------ */
/* Traduzione gamepad -> codici tasto dell'originale                    */
/* ------------------------------------------------------------------ */

/* keys[] viene letta da Game::HandleKey(), che azzera le voci consumate:
   esattamente il comportamento della tastiera originale (pressione singola =
   una colonna). Su PC la ripetizione e' data dall'OS (~2-3 Hz dopo un ritardo
   iniziale); qui fillGameKeys() riceveva hkHeld e muoveva il pezzo OGNI FRAME
   a 60 Hz: anche un tocco breve (~5 frame) attraversava tutto il pozzo.
   Ora: scatto immediato su hkPressed + ripetizione con ritardo iniziale se
   il tasto resta tenuto. */
#define MOVE_DELAY_INITIAL 0.22f   /* s prima che parta la ripetizione */
#define MOVE_DELAY_REPEAT  0.11f   /* s tra uno scatto e l'altro da tenuto */
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

  /* caduta: solo su pressione, non da tenuto (altrimenti restando su A
     cadrebbe istantaneamente anche il pezzo successivo) */
  if (pressed & HK_A) keys[BO_KEY_SPACE] = 1;

  /* rotazioni: i codici storici Q/W/E (Rx1 Ry1 Rz1) e A/S/D (Rx2 Ry2 Rz2),
     letti da Game::HandleKey() tramite SetupManager. Con R premuto vale
     SOLO la rotazione inversa (prima entrambe venivano impostate e la
     diretta vinceva sempre, rendendo R inutile). */
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

  /* aiuto nella modalita' pratica: solo su pressione */
  if (pressed & HK_CST_UP) keys['H'] = 1;
}

/* ------------------------------------------------------------------ */
/* Menu                                                                 */
/* ------------------------------------------------------------------ */

enum {
  ACT_NONE = 0, ACT_PLAY, ACT_PRACTICE, ACT_DEMO, ACT_SETUP, ACT_HISCORE, ACT_EXIT
};

static const char *menuItems[] = {
  "Nuova partita",
  "Pratica",
  "Demo",
  "Configurazione",
  "Migliori punteggi",
  "Esci"
};
#define MENU_NB 6

static void drawMenu(int sel) {
  render_clear(COL_BG);
  for (int eye = 0; eye < 2; eye++) {
    render_begin_top(eye);
    r_text(200.0f, 24.0f, 34.0f, COL_GREEN, "BLOCKOUT", 1, 0);
    r_text(200.0f, 66.0f, 13.0f, COL_GRAY,  "dal DOS originale (BlockOut II 2.5)", 1, 0);
    for (int i = 0; i < MENU_NB; i++) {
      uint32_t c = (i == sel) ? COL_WHITE : COL_GRAY;
      float yy = 100.0f + i * 22.0f;
      r_text(200.0f, yy, 17.0f, c, menuItems[i], 1, 0);
      if (i == sel) {
        float w = r_text_width(menuItems[i], 17.0f);
        r_line(200.0f - w * 0.5f - 16.0f, yy + 8.0f,
               200.0f - w * 0.5f - 6.0f,  yy + 8.0f, 2.0f, COL_GREEN);
      }
    }
    render_flush();
  }

  render_begin_bottom();
  r_rect(0.0f, 0.0f, BOTW, 240.0f, COL_BLACK);
  r_text(160.0f, 18.0f, 15.0f, COL_GREEN, "BlockOut 3DS", 1, 0);
  r_text(160.0f, 46.0f, 12.0f, COL_WHITE, "crostiera: scegli", 1, 0);
  r_text(160.0f, 64.0f, 12.0f, COL_WHITE, "A: conferma   B: esci", 1, 0);
  r_text(160.0f, 104.0f, 12.0f, COL_GREEN, "Comandi di gioco", 0, 0);
  r_text(8.0f, 124.0f, 12.0f, COL_WHITE, "crostiera/levetta: muovi il pezzo", 0, 0);
  r_text(8.0f, 142.0f, 12.0f, COL_WHITE, "L + crostiera: diagonali", 0, 0);
  r_text(8.0f, 160.0f, 12.0f, COL_WHITE, "A: fa' cadere   B/X/Y: ruota", 0, 0);
  r_text(8.0f, 178.0f, 12.0f, COL_WHITE, "R + B/X/Y: rotazione inversa", 0, 0);
  r_text(8.0f, 196.0f, 12.0f, COL_WHITE, "START: pausa   SELECT: esci", 0, 0);
  r_text(8.0f, 214.0f, 12.0f, COL_WHITE, "ZR: 3D   L+R+START: audio", 0, 0);
  render_flush();
  render_swap(true);
}

static int runMainMenu(void) {
  int sel = 0;
  int act = ACT_NONE;

  while (aptMainLoop()) {
    drawMenu(sel);
    pollInput();

    if (hkPressed & HK_DUP)    { sel = (sel + MENU_NB - 1) % MENU_NB; audio_tchh(); }
    if (hkPressed & HK_DDOWN)  { sel = (sel + 1) % MENU_NB;            audio_tchh(); }

    if (hkPressed & (HK_A | HK_START)) {
      act = sel + ACT_PLAY;
      audio_wozz();
      break;
    }
    if (hkPressed & HK_B) { act = ACT_EXIT; break; }
    if (hkPressed & HK_X) { act = ACT_PLAY; break; }   /* scorciatoia */
  }
  return act;
}

/* ------------------------------------------------------------------ */
/* Configurazione                                                      */
/* ------------------------------------------------------------------ */

/* Righe della configurazione: etichetta, sezione, descrizione */
struct SetupRow {
  const char *label;
  const char *section;   /* intestazione mostrata sopra la prima riga del gruppo */
  const char *desc;
};
static const SetupRow setupRows[] = {
  { "Larghezza",   "POZZO",   "Larghezza del pozzo (3-7)" },
  { "Altezza",     NULL,      "Altezza del pozzo (3-7)" },
  { "Profondita'", NULL,      "Profondita' del pozzo (6-18)" },
  { "Set di pezzi","BLOCCHI", "FLAT, BASIC o EXTENDED" },
  { "Facce",       NULL,      "Facce trasparenti (0 = opache)" },
  { "Velocita'",   "PARTITA", "Velocita' di caduta (0-10)" },
  { "Livello",     NULL,      "Livello di partenza (0-9)" },
  { "Suono",       "AUDIO",   "Musica ed effetti (SI/NO)" },
};
#define SETUP_NB 8

static void setupValueText(int idx, char *buf, size_t n) {
  switch (idx) {
    case 0: snprintf(buf, n, "%d", setupManager.GetPitWidth()); break;
    case 1: snprintf(buf, n, "%d", setupManager.GetPitHeight()); break;
    case 2: snprintf(buf, n, "%d", setupManager.GetPitDepth()); break;
    case 3: snprintf(buf, n, "%s", setupManager.GetBlockSetName()); break;
    case 4: snprintf(buf, n, "%d", setupManager.GetTransparentFace()); break;
    case 5: snprintf(buf, n, "%d", setupManager.GetAnimationSpeed()); break;
    case 6: snprintf(buf, n, "%d", setupManager.GetStartingLevel()); break;
    case 7: snprintf(buf, n, "%s", setupManager.GetSound() ? "SI" : "NO"); break;
    default: snprintf(buf, n, "-"); break;
  }
}

static void drawSetupPage(int sel) {
  render_clear(COL_BG);
  for (int eye = 0; eye < 2; eye++) {
    render_begin_top(eye);
    r_text(200.0f, 6.0f, 22.0f, COL_GREEN, "CONFIGURAZIONE", 1, 0);

    float y = 40.0f;
    char val[24];
    for (int i = 0; i < SETUP_NB; i++) {
      if (setupRows[i].section) {
        r_text(64.0f, y, 11.0f, COL_GREEN, setupRows[i].section, 0, 0);
        y += 13.0f;
      }
      if (i == sel)
        r_rect(40.0f, y - 3.0f, 320.0f, 15.0f, r_color(20, 40, 24, 255));

      uint32_t lc = (i == sel) ? COL_WHITE : COL_GRAY;
      r_text(64.0f, y, 13.0f, lc, setupRows[i].label, 0, 0);

      /* pill del valore */
      setupValueText(i, val, sizeof(val));
      uint32_t pb = (i == sel) ? COL_GREEN : COL_GRAY;
      r_rect(252.0f, y - 3.0f, 96.0f, 15.0f, r_color(10, 12, 16, 255));
      r_rect(252.0f, y - 3.0f, 96.0f, 1.0f, pb);
      r_rect(252.0f, y + 11.0f, 96.0f, 1.0f, pb);
      r_rect(252.0f, y - 3.0f, 1.0f, 15.0f, pb);
      r_rect(347.0f, y - 3.0f, 1.0f, 15.0f, pb);
      if (i == sel) {
        r_text(260.0f, y, 13.0f, COL_GREEN, "<", 0, 0);
        r_text(340.0f, y, 13.0f, COL_GREEN, ">", 0, 0);
      }
      r_text(300.0f, y, 13.0f, lc, val, 1, 0);
      y += 15.0f;
    }
    render_flush();
  }
  render_begin_bottom();
  r_rect(0.0f, 0.0f, BOTW, 240.0f, COL_BLACK);
  r_text(160.0f, 20.0f, 15.0f, COL_GREEN, "Configurazione", 1, 0);
  r_text(160.0f, 48.0f, 12.0f, COL_WHITE, "D-pad su/giu: voce", 1, 0);
  r_text(160.0f, 66.0f, 12.0f, COL_WHITE, "D-pad sx/dx: valore", 1, 0);
  r_text(160.0f, 84.0f, 12.0f, COL_WHITE, "A: salva    B: annulla", 1, 0);
  r_line(24.0f, 112.0f, 296.0f, 112.0f, 1.0f, COL_GRAY);
  r_text(160.0f, 124.0f, 13.0f, COL_WHITE, setupRows[sel].label, 1, 0);
  r_text(160.0f, 144.0f, 12.0f, COL_GREEN, setupRows[sel].desc, 1, 0);
  render_flush();
  render_swap(true);
}

static void runSetupPage(void) {
  /* istantanea per il vero annulla con B (i Set* mutano i valori live) */
  int snap[SETUP_NB] = {
    setupManager.GetPitWidth(), setupManager.GetPitHeight(),
    setupManager.GetPitDepth(), setupManager.GetBlockSet(),
    setupManager.GetTransparentFace(), setupManager.GetAnimationSpeed(),
    setupManager.GetStartingLevel(), setupManager.GetSound() ? 1 : 0
  };

  int sel = 0;

  while (aptMainLoop()) {
    drawSetupPage(sel);
    pollInput();

    int dv = 0;
    if (hkPressed & HK_DUP)    { sel = (sel + SETUP_NB - 1) % SETUP_NB; audio_tchh(); }
    if (hkPressed & HK_DDOWN)  { sel = (sel + 1) % SETUP_NB;             audio_tchh(); }
    if (hkPressed & HK_DLEFT)  { dv = -1; }
    if (hkPressed & HK_DRIGHT) { dv = +1; }

    if (dv) {
      switch (sel) {
        case 0: setupManager.SetPitWidth(setupManager.GetPitWidth() + dv); break;
        case 1: setupManager.SetPitHeight(setupManager.GetPitHeight() + dv); break;
        case 2: setupManager.SetPitDepth(setupManager.GetPitDepth() + dv); break;
        case 3: setupManager.SetBlockSet(setupManager.GetBlockSet() + dv); break;
        case 4: setupManager.SetTransparentFace(setupManager.GetTransparentFace() + dv); break;
        case 5: setupManager.SetAnimationSpeed(setupManager.GetAnimationSpeed() + dv); break;
        case 6: setupManager.SetStartingLevel(setupManager.GetStartingLevel() + dv); break;
        case 7: {
          int s = (setupManager.GetSound() ? 1 : 0) + dv;
          if (s < 0) s = 0;
          if (s > 1) s = 1;
          setupManager.SetSound(s ? TRUE : FALSE);
          soundManager.SetEnable(s ? TRUE : FALSE);
          audio_set_enable(s ? true : false);
          if (s) soundManager.PlayMusic();
          break;
        }
      }
      audio_blub();
    }

    if (hkPressed & HK_A) {
      setupManager.WriteSetup();
      game.InvalidateDeviceObjects();
      audio_wozz();
      break;
    }
    if (hkPressed & HK_B) {
      /* annulla: ripristina l'istantanea */
      setupManager.SetPitWidth(snap[0]);
      setupManager.SetPitHeight(snap[1]);
      setupManager.SetPitDepth(snap[2]);
      setupManager.SetBlockSet(snap[3]);
      setupManager.SetTransparentFace(snap[4]);
      setupManager.SetAnimationSpeed(snap[5]);
      setupManager.SetStartingLevel(snap[6]);
      setupManager.SetSound(snap[7] ? TRUE : FALSE);
      soundManager.SetEnable(snap[7] ? TRUE : FALSE);
      audio_set_enable(snap[7] ? true : false);
      audio_tchh();
      break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Hall of fame                                                        */
/* ------------------------------------------------------------------ */

static void runHiScorePage(void) {
  SCOREREC best;
  memset(&best, 0, sizeof(best));
  setupManager.GetHighScore(&best);

  while (aptMainLoop()) {
    render_clear(COL_BG);
    for (int eye = 0; eye < 2; eye++) {
      render_begin_top(eye);
      r_text(200.0f, 30.0f, 26.0f, COL_GREEN, "MIGLIORI PUNTEGGI", 1, 0);
      char buf[80];
      snprintf(buf, sizeof(buf), "%s", best.name[0] ? best.name : "(nessun punteggio)");
      r_text(200.0f, 90.0f, 18.0f, COL_WHITE, buf, 1, 0);
      snprintf(buf, sizeof(buf), "%d punti  -  livello %d", (int)best.score, (int)best.startLevel);
      r_text(200.0f, 120.0f, 15.0f, COL_WHITE, buf, 1, 0);
      snprintf(buf, sizeof(buf), "%d cubi  -  %s", (int)best.nbCube,
               setupManager.GetBlockSetName());
      r_text(200.0f, 146.0f, 15.0f, COL_GRAY, buf, 1, 0);
      render_flush();
    }
    render_begin_bottom();
    r_rect(0.0f, 0.0f, BOTW, 240.0f, COL_BLACK);
    r_text(160.0f, 100.0f, 14.0f, COL_WHITE, "A o B: indietro", 1, 0);
    render_flush();
    render_swap(true);

    pollInput();
    if (hkPressed & (HK_A | HK_B | HK_START | HK_X)) break;
  }
}

/* ------------------------------------------------------------------ */
/* Partita                                                             */
/* ------------------------------------------------------------------ */

static void runGamePlay(int act) {

  float fTime = getTime();

  switch (act) {
    case ACT_PLAY:     game.StartGame(TOPW, TOTH, fTime);     break;
    case ACT_PRACTICE: game.StartPractice(TOPW, TOTH, fTime); break;
    case ACT_DEMO:     game.StartDemo(TOPW, TOTH, fTime);     break;
    default: return;
  }

  while (aptMainLoop()) {

    pollInput();

    fTime = getTime();
    memset(keys, 0, sizeof(keys));
    fillGameKeys(hkHeld, hkPressed, fTime);

    /* START: pausa/ripresa, a fine partita torna al menu */
    if (hkPressed & HK_START) {
      int gm = game.GetGameMode();
      if (gm == GAME_PLAYING || gm == GAME_PAUSED) keys['P'] = 1;
      else keys[BO_KEY_RETURN] = 1;
    }
    /* SELECT: interrompe la partita (come ESC originale) */
    if (hkPressed & HK_SELECT) keys[BO_KEY_ESCAPE] = 1;

    /* ZL: aiuto in pratica */
    if (hkPressed & HK_ZL) keys['H'] = 1;

    /* ZR: profondita' stereoscopica a scatti (0 = 2D), con cap anti-sdoppiamento */
    if (hkPressed & HK_ZR) {
      stereoPx += 2.5f;
      if (stereoPx > 12.0f) stereoPx = 0.0f;
      render_set_stereo(stereoPx);
      audio_blub();
    }
    /* L+R+START: audio on/off */
    if ((hkPressed & HK_START) && (hkHeld & HK_L) && (hkHeld & HK_R)) {
      BOOL en = soundManager.GetEnable() ? FALSE : TRUE;
      soundManager.SetEnable(en);
      audio_set_enable(en ? true : false);
      audio_wozz();
    }

    int exitValue = game.Process(keys, fTime);

    render_clear(COL_BG);
    game.Render();
    render_swap(true);

    if (exitValue != 0) break;
    if (!game.GetInited()) break;
  }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {

  aptInit();
  gfxInitDefault();
  hidInit();
  osSetSpeedupEnable(true);

  audio_init();
  render_init();
  render_set_stereo(stereoPx);

#if BOOT_SELFTEST
  /* Diagnostica: se non compare "BOOT OK" con le barre colorate, il problema
     NON e' il gioco ma l'avvio / il pipeline grafico di base. */
  render_boot_test();
  usleep(2200000);
#endif

  soundManager.Create();
  soundManager.SetEnable(setupManager.GetSound() ? TRUE : FALSE);
  audio_set_enable(setupManager.GetSound() ? true : false);

  game.SetSetupManager(&setupManager);
  game.SetSoundManager(&soundManager);

  /* come nell'originale BlockOut.cpp: Create() una sola volta all'avvio */
  game.Create(TOPW, TOTH);

  tickBase = svcGetSystemTick();

  bool running = true;
  while (running && aptMainLoop()) {

    /* sottofondo del menu (no-op se gia' in play o con audio spento);
       resta anche in setup/punteggi e nella demo ("demo music" originale) */
    soundManager.PlayMusic();

    int act = runMainMenu();

    switch (act) {
      case ACT_PLAY:
      case ACT_PRACTICE:
        soundManager.StopMusic();   /* in partita solo effetti, come l'originale */
        runGamePlay(act);
        break;
      case ACT_DEMO:
        runGamePlay(act);
        break;
      case ACT_SETUP:
        runSetupPage();
        break;
      case ACT_HISCORE:
        runHiScorePage();
        break;
      case ACT_EXIT:
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
