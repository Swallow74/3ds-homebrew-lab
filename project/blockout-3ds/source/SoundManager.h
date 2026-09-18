/*
  File:        SoundManager.h
  Description: Sound management - stessa API di SoundManager.h di BlockOut II
               (i file .wav/.mod originali sono sostituiti dal sintetizzatore
               NDSP in audio.c: stessi eventi, stessi nomi)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef SOUNDMANAGERH
#define SOUNDMANAGERH

#include "bo_compat.h"
#include "audio.h"

class SoundManager {

public:
  SoundManager();

  // Initialise the sound manager
  int Create();

  // Play sounds
  void PlayBlub();
  void PlayWozz();
  void PlayTchh();

  // Game sounds
  void PlayLine();
  void PlayLevel();
  void PlayEmpty();
  void PlayWellDone();
  void PlayLine2();
  void PlayLevel2();
  void PlayEmpty2();
  void PlayWellDone2();
  void PlayHit();

  // Demo music
  void PlayMusic();
  void StopMusic();

  // Get error message
  char *GetErrorMsg();

  // Enable/Disable sound
  void SetEnable(BOOL enable);
  BOOL GetEnable();

private:
  BOOL enabled;
  char errMsg[1024];
};

#endif /* SOUNDMANAGERH */
