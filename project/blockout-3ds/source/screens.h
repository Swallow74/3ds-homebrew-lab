/*
  File:        screens.h
  Description: Schermate di interfaccia (intro, menu, setup, punteggi, pausa,
               fine partita, inserimento nome) sopra ui.h / render.h
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef SCREENSH
#define SCREENSH

#include <3ds.h>
#include <stdint.h>

#include "Types.h"

class Game;
class SetupManager;
class SoundManager;

/* Azioni del menu */
enum {
  SCR_NONE = 0, SCR_PLAY, SCR_PRACTICE, SCR_DEMO, SCR_SETUP, SCR_HISCORE, SCR_EXIT
};

/* Livello del cursore 3D di Setup -> disparita' in pixel */
float stereoLevelPx(int level);

/* Schermata di presentazione: ritorna quando l'utente conferma */
void runIntroScreen(void);

/* Menu principale (lista sullo schermo basso, toccabile) -> SCR_* */
int runMenuScreen(Game *game, SetupManager *sm);

/* Pagina di configurazione con anteprima del pozzo */
void runSetupScreen(Game *game, SetupManager *sm, SoundManager *snd);

/* Tabella dei punteggi (10 record dell'attuale configurazione) */
void runHiScoreScreen(SetupManager *sm);

/* Menu di pausa sopra il gioco congelato: 1 = riprendi, 2 = ricomincia, 0 = esci */
int runPauseMenu(Game *game);

/* Fine partita: tabella dei record con la nuova voce in modifica (come
   PageHallOfFame.cpp) + risultati. added = voce inserita (NULL se nessun
   record), recordPos 0..9. Ritorna 1 = retry, 2 = menu, 0 = exit. */
int runGameOverScreen(Game *game, SetupManager *sm, SCOREREC *added, int recordPos);

#endif /* SCREENSH */
