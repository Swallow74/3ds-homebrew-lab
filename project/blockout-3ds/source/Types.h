/*
  File:        Types.h
  Description: Shim dell'originale BlockOut II: nel port 3DS i tipi/le
               costanti stanno in bo_compat.h (nomi e valori identici).
               Qui sopra sono riportate le strutture che Types.h definisce
               dopo le costanti (SCOREREC, come nell'originale).
*/

#ifndef TYPESH
#define TYPESH

#include "bo_compat.h"

/* Record dei punteggi (Types.h di BlockOut II, senza il campo *next che
   l'originale aggiungeva in SetupManager.h) */
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
