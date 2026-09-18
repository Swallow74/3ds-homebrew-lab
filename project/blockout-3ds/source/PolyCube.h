/*
  File:        PolyCube.h
  Description: Polycube management (logic ported from BlockOut II)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef POLYCUBEH
#define POLYCUBEH

#include "bo_compat.h"

class PolyCube {

public:
  PolyCube();
  ~PolyCube();

  // Add a cube to the polycube
  void AddCube(int x, int y, int z);

  // Set polycube info
  void SetInfo(int highScore, int lowScore, BOOL flat, BOOL basic);

  // Geometry used by the 3DS renderer (replaces Create()/Render())
  void SetGeometry(float cubeSide, VERTEX origin, int ghost);

  // Accessors for the renderer
  float  GetCubeSide();
  VERTEX GetOrigin();
  int    GetGhost();
  BLOCKITEM *GetCubes();

  // Spigoli del polycube (Create() originale: edges[] con IsEdgeVisible)
  EDGE *GetEdges();
  int   GetNbEdge();

  // Return the rotation center
  VERTEX   GetRCenter();
  BLOCKITEM GetICenter();

  // Copy the polycube cube coordinates
  void CopyCube(BLOCKITEM *c, int *nb);

  // Get number of cube
  int GetNbCube();

  // Get High score
  int GetHighScore();

  // Get Low score
  int GetLowScore();

  // Check if the polycube belongs to the set
  BOOL IsInSet(int set);

  // Get dimension
  int GetWidth();
  int GetHeight();
  int GetDepth();
  int GetMaxDim();

  // Initialise rotation center
  void InitRotationCenter();

  // Orientation stuff (Used by AI player)
  void AddOrientation(int r0, int r1, int r2);
  ORIENTATION *GetOrientationAt(int idx);
  int GetNbOrientation();

  // Check if a cube exists (used by renderer edge/face culling)
  BOOL FindCube(int x, int y, int z);

private:
  // Calcolo degli spigoli visibili (identico all'originale PolyCube.cpp)
  BOOL EdgeExist(EDGE e);
  BOOL EdgeEqual(EDGE e1, EDGE e2);
  BOOL IsEdgeVisible(int cubeIdx, int edge);
  void ComputeEdges();

  int   hScore;      // score (dropped from top position)
  int   lScore;      // score (non dropped)
  BOOL  isFlat;      // Into the flat set
  BOOL  isBasic;     // Into the basic set
  BOOL  hasGhost;    // PolyCube transparent side
  float cubeSide;    // Cube side length
  VERTEX origin;     // Origin
  VERTEX center;     // Rotation center (space coordinates)
  BLOCKITEM iCenter; // Rotation center (cube coordinates)

  BLOCKITEM cubes[MAX_CUBE];
  int nbCube;

  EDGE *edges;       // Visible edges of the polycube
  int nbEdge;

  ORIENTATION *allRot;
  int nbOrientation;
};

#endif /* POLYCUBEH */
