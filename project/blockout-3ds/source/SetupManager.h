/*
  File:        SetupManager.h
  Description: Setup management (ported from BlockOut II, trimmed for 3DS)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  Le opzioni "hardware" dell'originale (risoluzione, fullscreen, frame limit,
  HTTP/proxy) non esistono sul 3DS e sono state eliminate.  Il formato dei
  file su SD (setup.dat / hscore.dat) segue la struttura dell'originale.
*/

#ifndef SETUPMANAGERH
#define SETUPMANAGERH

#include "Types.h"

#define APP_VERSION  "BlockOut 3DS 1.0"
#define SETUP_VERSION 6

class SetupManager {

public:
  SetupManager();

  // Pit dimension
  void SetPitWidth(int width);
  int GetPitWidth();
  void SetPitHeight(int height);
  int GetPitHeight();
  void SetPitDepth(int depth);
  int GetPitDepth();

  // Block set
  void SetBlockSet(int set);
  int GetBlockSet();

  // Starting level
  void SetStartingLevel(int level);
  int GetStartingLevel();

  // Animation speed
  void SetAnimationSpeed(int speed);
  int GetAnimationSpeed();
  float GetAnimationTime(); /* in seconds */

  // Names
  char *GetName();
  const char *GetBlockSetName();

  // Sound
  void SetSound(BOOL play);
  BOOL GetSound();

  // High score
  int InsertHighScore(SCOREREC *score, SCOREREC **added);
  void GetHighScore(SCOREREC *hScore);
  int GetHighScore();

  // Transparent face
  void SetTransparentFace(int transparent);
  int GetTransparentFace();

  // Style
  void SetStyle(int style);
  int GetStyle();

  // Sound type
  void SetSoundType(int stype);
  int GetSoundType();

  // Line width (stile ARCADE)
  void SetLineWidth(int width);
  int GetLineWidth();
  float GetLineRadius();

  // Return configuration id
  int GetId();

  // Save high score / setup (SD)
  void SaveHighScore();
  void WriteSetup();

  // Control keys (i codici sono quelli storici QWERTY: Q/W/E A/S/D)
  BYTE GetKRx1();
  BYTE GetKRy1();
  BYTE GetKRz1();
  BYTE GetKRx2();
  BYTE GetKRy2();
  BYTE GetKRz2();

  // Per il 3DS: numero di setup memorizzabili nella Hall of Fame
  int GetNbHighScore(int id);

private:
  int Saturate(int v, int min, int max);
  BOOL Check(int w, int h, int d, int s);
  void CleanHighScore(int id);
  void LoadHighScore();
  void LoadSetup();
  void ReadScoreItem(FILE *f, SCOREREC *dest);
  void WriteScoreItem(FILE *f, SCOREREC *dest);

  int32 pitWidth;
  int32 pitHeight;
  int32 pitDepth;
  int32 blockSet;
  int32 animationSpeed;
  int32 startLevel;
  BOOL playSound;
  int32 transparentFace;
  int32 style;
  int32 soundType;
  int32 lineWidth;
  BYTE keyRx1;
  BYTE keyRy1;
  BYTE keyRz1;
  BYTE keyRx2;
  BYTE keyRy2;
  BYTE keyRz2;

  SCOREREC *scoreList;
};

#endif /* SETUPMANAGERH */
