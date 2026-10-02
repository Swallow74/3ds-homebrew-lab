/*
  File:        screens.h
  Description: Interface screens (intro, menu, setup, scores, pause,
               game over, name entry) on top of ui.h / render.h
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

/* Menu actions */
enum {
  SCR_NONE = 0, SCR_PLAY, SCR_PRACTICE, SCR_DEMO, SCR_SETUP, SCR_HISCORE, SCR_EXIT
};

/* Setup 3D slider level -> disparity in pixels */
float stereoLevelPx(int level);

/* Title screen: returns when the user confirms */
void runIntroScreen(void);

/* Main menu (list on the bottom screen, touchable) -> SCR_* */
int runMenuScreen(Game *game, SetupManager *sm);

/* Configuration page with a preview of the pit */
void runSetupScreen(Game *game, SetupManager *sm, SoundManager *snd);

/* Score table (10 records of the current configuration) */
void runHiScoreScreen(SetupManager *sm);

/* Pause menu above the frozen game: 1 = resume, 2 = restart, 0 = quit */
int runPauseMenu(Game *game);

/* Game over: record table with the new entry being edited (like
   PageHallOfFame.cpp) + results. added = inserted entry (NULL if no
   record), recordPos 0..9. Returns 1 = retry, 2 = menu, 0 = exit. */
int runGameOverScreen(Game *game, SetupManager *sm, SCOREREC *added, int recordPos);

#endif /* SCREENSH */
