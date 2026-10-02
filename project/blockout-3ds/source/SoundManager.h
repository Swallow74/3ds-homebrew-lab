/*
  File:        SoundManager.h
  Description: Sound management - same API as SoundManager.h of BlockOut II
               (the original .wav/.mod files are replaced by the NDSP
               synthesizer in audio.c: same events, same names)
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
  void PlayOver();

  // Layers completed together (for the line sound phrase)
  void SetLineCount(int n);

  // Music (MUS_TITLE / MUS_GAME / MUS_OVER from music.h)
  void PlayMusic(int song);
  void StopMusic();
  void SetMusicTempo(float mul);

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
