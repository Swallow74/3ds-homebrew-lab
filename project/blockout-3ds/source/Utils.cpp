/*
  File:        Utils.cpp
  Description: Utility functions identical to the original BlockOut II
               (v, Normalize, fround, FormatTime, ZeroMemory)
  Author:      Jean-Luc PONS (GPL)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include "Utils.h"

//-----------------------------------------------------------------------------
// Name: v()
// Desc: Builds a VERTEX (static, as in the original)
//-----------------------------------------------------------------------------
VERTEX v(float x, float y, float z) {

  static VERTEX ret;
  ret.x = x;
  ret.y = y;
  ret.z = z;
  return ret;
}

//-----------------------------------------------------------------------------
// Normalize: normalizes a 3D vector
//-----------------------------------------------------------------------------
void Normalize(VERTEX *v) {

  float n = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
  v->x = v->x / n;
  v->y = v->y / n;
  v->z = v->z / n;
}

//-----------------------------------------------------------------------------
// Name: fround()
// Desc: Rounds to the nearest integer
//-----------------------------------------------------------------------------
int fround(float x) {

  int i;
  if (x < 0.0f) {
    i = (int)((-x) + 0.5f);
    i = -i;
  } else {
    i = (int)(x + 0.5f);
  }
  return i;
}

//-----------------------------------------------------------------------------
// Name: FormatTime(float seconds)
// Desc: Formats the time as "XXmin YYsec"
//-----------------------------------------------------------------------------
char *FormatTime(float seconds) {

  static char ret[32];
  int min = (int)seconds / 60;
  int sec = (int)seconds % 60;
  sprintf(ret, "%dmin %02dsec", min, sec);
  return ret;
}

//-----------------------------------------------------------------------------
void ZeroMemory(void *buff, int size) {
  memset(buff, 0, size);
}
