/*
  File:        Types.h
  Description: Shim of the original BlockOut II: in the 3DS port the types/
               constants live in bo_compat.h (identical names and values).
               Here are the structures that Types.h defines
               after the constants (SCOREREC, as in the original).
*/

#ifndef TYPESH
#define TYPESH

#include "bo_compat.h"

/* Score record (Types.h of BlockOut II, without the *next field that
   the original added in SetupManager.h) */
typedef struct SCORERECLINK {

  int32  setupId;
  int32  score;
  int32  nbCube;
  int32  nbLine1;
  int32  nbLine2;
  int32  nbLine3;
  int32  nbLine4;
  int32  nbLine5;
  int32  startLevel;
  uint32 date;
  char   name[11];
  BYTE   emptyPit;
  int32  scoreId;
  float  gameTime;

  SCORERECLINK *next;

} SCOREREC;

#endif /* TYPESH */
