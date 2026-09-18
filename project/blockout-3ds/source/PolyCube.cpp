/*
  File:        PolyCube.cpp
  Description: Polycube management - logic ported verbatim from BlockOut II
               (the OpenGL device-object code Create/Render/ghost/cylinder
               edges was replaced by the 3DS renderer, which reads the cube
               list and the geometry accessors below)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include "PolyCube.h"

PolyCube::PolyCube() {

  nbCube = 0;
  hScore = 0;
  lScore = 0;
  isFlat = FALSE;
  isBasic = FALSE;
  hasGhost = FALSE;
  nbOrientation = 0;
  cubeSide = 0.0f;
  origin.x = 0.0f;
  origin.y = 0.0f;
  origin.z = 0.0f;
  center.x = 0.0f;
  center.y = 0.0f;
  center.z = 0.0f;
  iCenter.x = 0;
  iCenter.y = 0;
  iCenter.z = 0;
  allRot = (ORIENTATION *)malloc(24 * sizeof(ORIENTATION));
  edges = (EDGE *)malloc(MAX_CUBE * 12 * sizeof(EDGE));
  nbEdge = 0;
}

PolyCube::~PolyCube() {
  if (allRot) free(allRot);
  if (edges) free(edges);
}

//-----------------------------------------------------------------------------

void PolyCube::AddCube(int x, int y, int z) {

  if (nbCube < MAX_CUBE) {
    cubes[nbCube].x = x;
    cubes[nbCube].y = y;
    cubes[nbCube].z = z;
    nbCube++;
  }
}

//-----------------------------------------------------------------------------

void PolyCube::SetInfo(int highScore, int lowScore, BOOL flat, BOOL basic) {

  hScore = highScore;
  lScore = lowScore;
  isFlat = flat;
  isBasic = basic;
}

//-----------------------------------------------------------------------------
// Geometry for the renderer: called by InitPolyCubes() once the pit geometry
// is known (same role as Create(cubeSide,origin,ghost,wEdge) in the original)
//-----------------------------------------------------------------------------

void PolyCube::SetGeometry(float cs, VERTEX org, int ghost) {

  cubeSide = cs;
  origin   = org;
  hasGhost = (ghost > 0) ? TRUE : FALSE;
  ComputeEdges();
  InitRotationCenter();
}

//-----------------------------------------------------------------------------
// Calcolo degli spigoli del polycube: identico a PolyCube::Create() di
// BlockOut II 2.5 (tabela edgeOrg[12], IsEdgeVisible, dedup EdgeExist).
//-----------------------------------------------------------------------------

#define DIR_OX 1
#define DIR_OY 2
#define DIR_OZ 3

const EDGE edgeOrg[] = {

  {{0,1,0} , {1,1,0} , DIR_OX} , // 0
  {{1,1,0} , {1,0,0} , DIR_OY} , // 1
  {{0,0,0} , {1,0,0} , DIR_OX} , // 2
  {{0,1,0} , {0,0,0} , DIR_OY} , // 3

  {{0,1,0} , {0,1,1} , DIR_OZ} , // 4
  {{1,1,0} , {1,1,1} , DIR_OZ} , // 5
  {{1,0,0} , {1,0,1} , DIR_OZ} , // 6
  {{0,0,0} , {0,0,1} , DIR_OZ} , // 7

  {{0,1,1} , {1,1,1} , DIR_OX} , // 8
  {{1,1,1} , {1,0,1} , DIR_OY} , // 9
  {{0,0,1} , {1,0,1} , DIR_OX} , // 10
  {{0,1,1} , {0,0,1} , DIR_OY}   // 11

};

//-----------------------------------------------------------------------------

BOOL PolyCube::EdgeEqual(EDGE e1,EDGE e2) {

  return ((e1.p1.x == e2.p1.x && e1.p1.y == e2.p1.y && e1.p1.z == e2.p1.z) &&
          (e1.p2.x == e2.p2.x && e1.p2.y == e2.p2.y && e1.p2.z == e2.p2.z) ) ||
         ((e1.p1.x == e2.p2.x && e1.p1.y == e2.p2.y && e1.p1.z == e2.p2.z) &&
          (e1.p2.x == e2.p1.x && e1.p2.y == e2.p1.y && e1.p2.z == e2.p1.z) );

}

//-----------------------------------------------------------------------------

BOOL PolyCube::EdgeExist(EDGE e) {

  BOOL found = FALSE;
  int i = 0;
  while(i<nbEdge && !found) {
    found = EdgeEqual(edges[i],e);
    if(!found) i++;
  }
  return found;

}

//-----------------------------------------------------------------------------

BOOL PolyCube::IsEdgeVisible(int cubeIdx,int edge) {

  BOOL e1,e2,e3;

  int x = cubes[cubeIdx].x;
  int y = cubes[cubeIdx].y;
  int z = cubes[cubeIdx].z;

  switch(edge) {

    case 0:
      e1 = FindCube(x,y,z-1);e2 = FindCube(x,y+1,z-1);e3 = FindCube(x,y+1,z);
      break;
    case 1:
      e1 = FindCube(x,y,z-1);e2 = FindCube(x+1,y,z-1);e3 = FindCube(x+1,y,z);
      break;
    case 2:
      e1 = FindCube(x,y,z-1);e2 = FindCube(x,y-1,z-1);e3 = FindCube(x,y-1,z);
      break;
    case 3:
      e1 = FindCube(x,y,z-1);e2 = FindCube(x-1,y,z-1);e3 = FindCube(x-1,y,z);
      break;

    case 4:
      e1 = FindCube(x,y+1,z);e2 = FindCube(x-1,y+1,z);e3 = FindCube(x-1,y,z);
      break;
    case 5:
      e1 = FindCube(x,y+1,z);e2 = FindCube(x+1,y+1,z);e3 = FindCube(x+1,y,z);
      break;
    case 6:
      e1 = FindCube(x+1,y,z);e2 = FindCube(x+1,y-1,z);e3 = FindCube(x,y-1,z);
      break;
    case 7:
      e1 = FindCube(x,y-1,z);e2 = FindCube(x-1,y-1,z);e3 = FindCube(x-1,y,z);
      break;

    case 8:
      e1 = FindCube(x,y+1,z);e2 = FindCube(x,y+1,z+1);e3 = FindCube(x,y,z+1);
      break;
    case 9:
      e1 = FindCube(x+1,y,z);e2 = FindCube(x+1,y,z+1);e3 = FindCube(x,y,z+1);
      break;
    case 10:
      e1 = FindCube(x,y-1,z);e2 = FindCube(x,y-1,z+1);e3 = FindCube(x,y,z+1);
      break;
    case 11:
      e1 = FindCube(x-1,y,z);e2 = FindCube(x-1,y,z+1);e3 = FindCube(x,y,z+1);
      break;

  }

  return !( (!e1 && !e2 && e3) || (e1 && !e2 && !e3) || (e1 && e2 && e3) );

}

//-----------------------------------------------------------------------------

void PolyCube::ComputeEdges() {

  nbEdge = 0;
  for(int i=0;i<nbCube;i++) {
    for(int j=0;j<12;j++) {
      if( IsEdgeVisible(i,j) ) {

        EDGE e;
        e.p1.x = cubes[i].x + edgeOrg[j].p1.x;
        e.p1.y = cubes[i].y + edgeOrg[j].p1.y;
        e.p1.z = cubes[i].z + edgeOrg[j].p1.z;

        e.p2.x = cubes[i].x + edgeOrg[j].p2.x;
        e.p2.y = cubes[i].y + edgeOrg[j].p2.y;
        e.p2.z = cubes[i].z + edgeOrg[j].p2.z;

        e.orientation = edgeOrg[j].orientation;

        if(!EdgeExist(e)) edges[nbEdge++] = e;

      }
    }
  }

}

//-----------------------------------------------------------------------------

EDGE *PolyCube::GetEdges() { return edges; }

int PolyCube::GetNbEdge() { return nbEdge; }

//-----------------------------------------------------------------------------

float PolyCube::GetCubeSide() {
  return cubeSide;
}

VERTEX PolyCube::GetOrigin() {
  return origin;
}

int PolyCube::GetGhost() {
  return hasGhost ? 1 : 0;
}

BLOCKITEM *PolyCube::GetCubes() {
  return cubes;
}

//-----------------------------------------------------------------------------

int PolyCube::GetHighScore() {
  return hScore;
}

int PolyCube::GetLowScore() {
  return lScore;
}

//-----------------------------------------------------------------------------

BOOL PolyCube::IsInSet(int set) {

  switch (set) {
  case BLOCKSET_EXTENDED:
    return TRUE;
  case BLOCKSET_BASIC:
    return isBasic;
  case BLOCKSET_FLAT:
    return isFlat;
  }

  return FALSE;
}

//-----------------------------------------------------------------------------

int PolyCube::GetNbCube() {
  return nbCube;
}

int PolyCube::GetWidth() {
  int maxW = 0;
  for (int i = 0; i < nbCube; i++)
    if (cubes[i].x > maxW) maxW = cubes[i].x;
  return maxW + 1;
}

int PolyCube::GetHeight() {
  int maxH = 0;
  for (int i = 0; i < nbCube; i++)
    if (cubes[i].y > maxH) maxH = cubes[i].y;
  return maxH + 1;
}

int PolyCube::GetDepth() {
  int maxD = 0;
  for (int i = 0; i < nbCube; i++)
    if (cubes[i].z > maxD) maxD = cubes[i].z;
  return maxD + 1;
}

int PolyCube::GetMaxDim() {
  int w = GetWidth();
  int h = GetHeight();
  int d = GetDepth();
  int maxDim = w;
  if (h > maxDim) maxDim = h;
  if (d > maxDim) maxDim = d;
  return maxDim;
}

//-----------------------------------------------------------------------------

void PolyCube::CopyCube(BLOCKITEM *c, int *nb) {

  for (int i = 0; i < nbCube; i++) {
    c[i] = cubes[i];
  }
  *nb = nbCube;
}

//-----------------------------------------------------------------------------

VERTEX PolyCube::GetRCenter() {
  return center;
}

BLOCKITEM PolyCube::GetICenter() {
  return iCenter;
}

//-----------------------------------------------------------------------------
// Emulate "BlockOut original" rotation center
//-----------------------------------------------------------------------------

void PolyCube::InitRotationCenter() {

  iCenter.x = GetWidth() - 1;
  iCenter.y = 1;
  iCenter.z = GetDepth() - 1;

  center.x = iCenter.x * cubeSide + origin.x;
  center.y = iCenter.y * cubeSide + origin.y;
  center.z = iCenter.z * cubeSide + origin.z;
}

//-----------------------------------------------------------------------------
// Orientation stuff (used by the AI player)

void PolyCube::AddOrientation(int r0, int r1, int r2) {
  allRot[nbOrientation].r0 = r0;
  allRot[nbOrientation].r1 = r1;
  allRot[nbOrientation].r2 = r2;
  nbOrientation++;
}

ORIENTATION *PolyCube::GetOrientationAt(int idx) {
  return &(allRot[idx]);
}

int PolyCube::GetNbOrientation() {
  return nbOrientation;
}

//-----------------------------------------------------------------------------

BOOL PolyCube::FindCube(int x, int y, int z) {

  BOOL found = FALSE;
  int i = 0;
  while (i < nbCube && !found) {
    found = (cubes[i].x == x) && (cubes[i].y == y) && (cubes[i].z == z);
    if (!found) i++;
  }

  return found;
}
