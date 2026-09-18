/*
  File:        SoundManager.cpp
  Description: Sound management - implementa la stessa API di SoundManager.cpp
               di BlockOut II sopra il sintetizzatore NDSP (audio.c)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include "SoundManager.h"

// ------------------------------------------------

SoundManager::SoundManager() {

  enabled = FALSE;
  ZeroMemory(errMsg, sizeof(errMsg));
}

// ------------------------------------------------

int SoundManager::Create() {

  audio_init();

  if (!audio_ok()) {
    sprintf(errMsg, "NDSP init failed");
    enabled = FALSE;
    return GL_FAIL;
  }

  enabled = TRUE;
  return GL_OK;
}

// ------------------------------------------------

BOOL SoundManager::GetEnable() {
  return enabled;
}

void SoundManager::SetEnable(BOOL enable) {
  enabled = enable;
  audio_set_enable(enable ? true : false);
  if (!enable) audio_stop_music();
}

// ------------------------------------------------

char *SoundManager::GetErrorMsg() { return errMsg; }

// ------------------------------------------------

void SoundManager::PlayBlub() { audio_blub(); }
void SoundManager::PlayWozz() { audio_wozz(); }
void SoundManager::PlayTchh() { audio_tchh(); }

void SoundManager::PlayLine() { audio_line(); }
void SoundManager::PlayLevel() { audio_level(); }
void SoundManager::PlayEmpty() { audio_empty(); }
void SoundManager::PlayWellDone() { audio_welldone(); }

void SoundManager::PlayLine2() { audio_line2(); }
void SoundManager::PlayLevel2() { audio_level2(); }
void SoundManager::PlayEmpty2() { audio_empty2(); }
void SoundManager::PlayWellDone2() { audio_welldone2(); }

void SoundManager::PlayHit() { audio_hit(); }

void SoundManager::PlayMusic() { if (enabled) audio_music(); }
void SoundManager::StopMusic() { audio_stop_music(); }
