/*
  File:        SetupManager.cpp
  Description: Setup management - logic ported from BlockOut II 2.5.
               Le opzioni hardware non esistenti sul 3DS (schermo, frame
               limit, HTTP) sono state eliminate; i file di configurazione e
               i punteggi massimi sono salvati sulla SD in /3ds/blockout con
               la stessa struttura dei file originali (versione 6).
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include "SetupManager.h"
#include "setupId_table.h"
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BD_DIR      "/3ds/blockout"
#define BD_SETUP    BD_DIR "/setup.dat"
#define BD_HSCORE   BD_DIR "/hscore.dat"

const char *BLOCKSET_NAME[] = {"FLAT", "BASIC", "EXTENDED"};

//-----------------------------------------------------------------------------

SetupManager::SetupManager() {

  // Default (identici all'originale)
  pitWidth = 5;
  pitHeight = 5;
  pitDepth = 12;
  blockSet = BLOCKSET_FLAT;
  animationSpeed = 5;
  startLevel = 0;
  playSound = TRUE;
  scoreList = NULL;
  transparentFace = 0;
  style = STYLE_CLASSIC;
  lineWidth = LINEW_MIN;
  soundType = SOUND_BLOCKOUT2;

  keyRx1 = 'Q';
  keyRy1 = 'W';
  keyRz1 = 'E';
  keyRx2 = 'A';
  keyRy2 = 'S';
  keyRz2 = 'D';

  mkdir(BD_DIR, 0777);
  LoadHighScore();
  LoadSetup();
}

//-----------------------------------------------------------------------------

int SetupManager::Saturate(int v, int min, int max) {
  if (v < min) return min;
  if (v > max) return max;
  return v;
}

//-----------------------------------------------------------------------------
// Pit dimension

void SetupManager::SetPitWidth(int width) {
  pitWidth = Saturate(width, MIN_PITWIDTH, MAX_PITWIDTH);
}
int SetupManager::GetPitWidth() { return pitWidth; }

void SetupManager::SetPitHeight(int height) {
  pitHeight = Saturate(height, MIN_PITHEIGHT, MAX_PITHEIGHT);
}
int SetupManager::GetPitHeight() { return pitHeight; }

void SetupManager::SetPitDepth(int depth) {
  pitDepth = Saturate(depth, MIN_PITDEPTH, MAX_PITDEPTH);
}
int SetupManager::GetPitDepth() { return pitDepth; }

//-----------------------------------------------------------------------------
// Block set

void SetupManager::SetBlockSet(int set) {
  blockSet = Saturate(set, BLOCKSET_FLAT, BLOCKSET_EXTENDED);
}
int SetupManager::GetBlockSet() { return blockSet; }

//-----------------------------------------------------------------------------
// Starting level

void SetupManager::SetStartingLevel(int level) {
  startLevel = Saturate(level, 0, 9);
}
int SetupManager::GetStartingLevel() { return startLevel; }

//-----------------------------------------------------------------------------
// Animation speed

void SetupManager::SetAnimationSpeed(int speed) {
  animationSpeed = Saturate(speed, ASPEED_SLOW, ASPEED_FAST);
}
int SetupManager::GetAnimationSpeed() { return animationSpeed; }

float SetupManager::GetAnimationTime() {

  float min = 0.05f;
  float max = 0.15f;
  float speed = min + (max - min) * (float)(ASPEED_FAST - animationSpeed) / ((float)ASPEED_FAST);
  return speed;
}

//-----------------------------------------------------------------------------
// Transparent face

void SetupManager::SetTransparentFace(int transparent) {
  transparentFace = Saturate(transparent, FTRANS_MIN, FTRANS_MAX);
}
int SetupManager::GetTransparentFace() { return transparentFace; }

//-----------------------------------------------------------------------------
// Style / sound type / line width

void SetupManager::SetStyle(int st) {
  style = Saturate(st, STYLE_CLASSIC, STYLE_ARCADE);
}
int SetupManager::GetStyle() { return style; }

void SetupManager::SetSoundType(int stype) {
  soundType = Saturate(stype, SOUND_BLOCKOUT2, SOUND_BLOCKOUT);
}
int SetupManager::GetSoundType() { return soundType; }

void SetupManager::SetLineWidth(int width) {
  lineWidth = Saturate(width, LINEW_MIN, LINEW_MAX);
}
int SetupManager::GetLineWidth() { return lineWidth; }

float SetupManager::GetLineRadius() {

  if (lineWidth == 0) {
    return 0.0f;
  } else {
    return (float)lineWidth * 0.002f + 0.005f;
  }
}

//-----------------------------------------------------------------------------
// Sound

void SetupManager::SetSound(BOOL play) { playSound = play; }
BOOL SetupManager::GetSound() { return playSound; }

//-----------------------------------------------------------------------------
// Control keys (codici storici QWERTY)

BYTE SetupManager::GetKRx1() { return keyRx1; }
BYTE SetupManager::GetKRy1() { return keyRy1; }
BYTE SetupManager::GetKRz1() { return keyRz1; }
BYTE SetupManager::GetKRx2() { return keyRx2; }
BYTE SetupManager::GetKRy2() { return keyRy2; }
BYTE SetupManager::GetKRz2() { return keyRz2; }

//-----------------------------------------------------------------------------

BOOL SetupManager::Check(int w, int h, int d, int s) {

  return ((w == pitWidth) && (h == pitHeight) ||
          (h == pitWidth) && (w == pitHeight)) &&
         (d == pitDepth) && (s == blockSet);
}

//-----------------------------------------------------------------------------

char *SetupManager::GetName() {

  static char ret[32];
  strcpy(ret, "");

  // Default setup
  if (Check(5, 5, 12, BLOCKSET_FLAT)) {
    return STR("[Flat Fun]");
  } else if (Check(3, 3, 10, BLOCKSET_BASIC)) {
    return STR("[3D Mania]");
  } else if (Check(5, 5, 10, BLOCKSET_EXTENDED)) {
    return STR("[Out of Control]");
  }

  // Generic name
  sprintf(ret, "[%dx%dx%d,%s]", pitWidth, pitHeight, pitDepth, GetBlockSetName());
  return ret;
}

//-----------------------------------------------------------------------------

const char *SetupManager::GetBlockSetName() {
  return BLOCKSET_NAME[blockSet];
}

//-----------------------------------------------------------------------------
// Return configuration id (975 -> 585 configurazioni)

int SetupManager::GetId() {

  int idx = (blockSet - BLOCKSET_FLAT) * 325 + (pitDepth - MIN_PITDEPTH) * 25 +
            (pitWidth - MIN_PITWIDTH) * 5 + (pitHeight - MIN_PITHEIGHT);

  return setupId[idx];
}

//-----------------------------------------------------------------------------
// High score

int SetupManager::InsertHighScore(SCOREREC *score, SCOREREC **added) {

  *added = NULL;

  if (score->score == 0) {
    // Does not insert null score
    return 10;
  }

  int pos = 0;
  int id = GetId();
  BOOL found = FALSE;

  SCOREREC *ptr = scoreList;
  SCOREREC *lastPtr = NULL;

  // Look for insertion pos
  while ((ptr != NULL) && (pos < 10) && (!found)) {
    found = score->score > ptr->score;
    if (!found) {
      if (ptr->setupId == id)
        pos++;
      lastPtr = ptr;
      ptr = ptr->next;
    }
  }

  if (pos < 10) {
    // Got a high score
    SCOREREC *nPtr = (SCOREREC *)malloc(sizeof(SCOREREC));
    memcpy(nPtr, score, sizeof(SCOREREC));
    if (lastPtr == NULL) {
      // New head
      nPtr->next = scoreList;
      scoreList = nPtr;
    } else {
      // Insert after lastPtr
      nPtr->next = ptr;
      lastPtr->next = nPtr;
    }
    *added = nPtr;
    // Keep only 10
    CleanHighScore(id);
  }

  return pos;
}

//-----------------------------------------------------------------------------

void SetupManager::GetHighScore(SCOREREC *hScore) {

  SCOREREC *ptr = scoreList;
  int id = GetId();
  int pos = 0;

  // Return all high score of the current game setup (10 MAX)
  while (ptr != NULL) {
    if (ptr->setupId == id) {
      if (pos < 10) memcpy(hScore + pos, ptr, sizeof(SCOREREC));
      pos++;
    }
    ptr = ptr->next;
  }

  // Fill with 0
  for (int i = pos; i < 10; i++) {
    memset(hScore + i, 0, sizeof(SCOREREC));
  }
}

//-----------------------------------------------------------------------------

int SetupManager::GetHighScore() {

  SCOREREC *ptr = scoreList;
  int id = GetId();
  int highScore = 0;

  while (ptr != NULL) {
    if (ptr->setupId == id) {
      if (ptr->score > highScore) highScore = ptr->score;
    }
    ptr = ptr->next;
  }

  return highScore;
}

//-----------------------------------------------------------------------------

int SetupManager::GetNbHighScore(int id) {

  int nb = 0;
  SCOREREC *ptr = scoreList;
  while (ptr != NULL) {
    if (ptr->setupId == id) nb++;
    ptr = ptr->next;
  }
  return nb;
}

//-----------------------------------------------------------------------------

void SetupManager::CleanHighScore(int id) {

  int pos = 0;
  BOOL removed;
  SCOREREC *ptr = scoreList;
  SCOREREC *lastPtr = NULL;

  // Keep the 10 best score of the given setup
  while (ptr != NULL) {
    removed = FALSE;
    if (ptr->setupId == id) {
      if (pos >= 10) {
        // Remove (REM: lastPtr cannot be null)
        lastPtr->next = ptr->next;
        free(ptr);
        // Next item
        removed = TRUE;
        ptr = lastPtr->next;
      } else {
        pos++;
      }
    }
    if (!removed) {
      lastPtr = ptr;
      ptr = ptr->next;
    }
  }
}

//-----------------------------------------------------------------------------
// Record binario da 64 byte (come WriteScoreItem/ReadScoreItem originali)

void SetupManager::ReadScoreItem(FILE *f, SCOREREC *dest) {

  int structSzie = 64;
  size_t nbRead = fread(dest, 1, structSzie, f);
  (void)nbRead;
  dest->next = NULL;
}

void SetupManager::WriteScoreItem(FILE *f, SCOREREC *dest) {

  int structSzie = 64;
  fwrite(dest, 1, structSzie, f);
}

//-----------------------------------------------------------------------------

void SetupManager::LoadHighScore() {

  SCOREREC *ptr;

  FILE *file = fopen(BD_HSCORE, "rb");

  if (file != NULL) {

    int32 nbScore;
    size_t nbRead = fread(&nbScore, 1, sizeof(int32), file);

    if ((nbRead == sizeof(int32)) && (nbScore <= 10 * 585) && (nbScore >= 1)) {

      scoreList = (SCOREREC *)malloc(sizeof(SCOREREC));
      ReadScoreItem(file, scoreList);
      ptr = scoreList;

      for (int i = 1; i < nbScore; i++) {
        SCOREREC *nPtr = (SCOREREC *)malloc(sizeof(SCOREREC));
        ReadScoreItem(file, nPtr);
        ptr->next = nPtr;
        ptr = nPtr;
      }

      ptr->next = NULL;
    }

    fclose(file);
  }
}

//-----------------------------------------------------------------------------

void SetupManager::SaveHighScore() {

  SCOREREC *ptr = scoreList;

  FILE *file = fopen(BD_HSCORE, "wb");

  if (file != NULL) {

    // Get number of score
    int32 nbScore = 0;
    while (ptr != NULL) {
      nbScore++;
      ptr = ptr->next;
    }

    // Write
    size_t nbWritten = fwrite(&nbScore, 1, sizeof(int32), file);
    if (nbWritten == sizeof(int32)) {
      ptr = scoreList;
      while (ptr != NULL) {
        WriteScoreItem(file, ptr);
        ptr = ptr->next;
      }
    }

    fclose(file);
  }
}

//-----------------------------------------------------------------------------
// File di configurazione (stesso ordine di campi di WriteSetup, versione 6,
// senza i campi hardware dell'originale)

void SetupManager::LoadSetup() {

  FILE *file = fopen(BD_SETUP, "rb");

  if (file != NULL) {

    int32 version = 0;
    size_t nbRead = fread(&version, 1, sizeof(int32), file);

    if (nbRead == sizeof(int32) && version <= SETUP_VERSION) {

      nbRead = (size_t)fread(&pitWidth, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&pitHeight, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&pitDepth, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&blockSet, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&animationSpeed, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&startLevel, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&playSound, sizeof(BOOL), 1, file);
      nbRead = (size_t)fread(&transparentFace, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&style, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&soundType, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&lineWidth, sizeof(int32), 1, file);
      nbRead = (size_t)fread(&keyRx1, sizeof(BYTE), 1, file);
      nbRead = (size_t)fread(&keyRy1, sizeof(BYTE), 1, file);
      nbRead = (size_t)fread(&keyRz1, sizeof(BYTE), 1, file);
      nbRead = (size_t)fread(&keyRx2, sizeof(BYTE), 1, file);
      nbRead = (size_t)fread(&keyRy2, sizeof(BYTE), 1, file);
      nbRead = (size_t)fread(&keyRz2, sizeof(BYTE), 1, file);

      // Saturate
      SetPitWidth(pitWidth);
      SetPitHeight(pitHeight);
      SetPitDepth(pitDepth);
      SetBlockSet(blockSet);
      SetAnimationSpeed(animationSpeed);
      SetStartingLevel(startLevel);
      SetTransparentFace(transparentFace);
      SetStyle(style);
      SetSoundType(soundType);
      SetLineWidth(lineWidth);
    }

    fclose(file);
  }
}

//-----------------------------------------------------------------------------

void SetupManager::WriteSetup() {

  FILE *file = fopen(BD_SETUP, "wb");

  if (file != NULL) {

    int32 version = SETUP_VERSION;
    size_t nbWritten = fwrite(&version, 1, sizeof(int32), file);

    if (nbWritten == sizeof(int32)) {

      fwrite(&pitWidth, sizeof(int32), 1, file);
      fwrite(&pitHeight, sizeof(int32), 1, file);
      fwrite(&pitDepth, sizeof(int32), 1, file);
      fwrite(&blockSet, sizeof(int32), 1, file);
      fwrite(&animationSpeed, sizeof(int32), 1, file);
      fwrite(&startLevel, sizeof(int32), 1, file);
      fwrite(&playSound, sizeof(BOOL), 1, file);
      fwrite(&transparentFace, sizeof(int32), 1, file);
      fwrite(&style, sizeof(int32), 1, file);
      fwrite(&soundType, sizeof(int32), 1, file);
      fwrite(&lineWidth, sizeof(int32), 1, file);
      fwrite(&keyRx1, sizeof(BYTE), 1, file);
      fwrite(&keyRy1, sizeof(BYTE), 1, file);
      fwrite(&keyRz1, sizeof(BYTE), 1, file);
      fwrite(&keyRx2, sizeof(BYTE), 1, file);
      fwrite(&keyRy2, sizeof(BYTE), 1, file);
      fwrite(&keyRz2, sizeof(BYTE), 1, file);
    }

    fclose(file);
  }
}
