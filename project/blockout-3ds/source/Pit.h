/*
  File:        Pit.h
  Description: Pit management (ported from BlockOut II)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef PITH
#define PITH

#include "PolyCube.h"

#define NBTCUBE 16

class Pit {

public:
  Pit();

  // Initialise pit dimension
  void SetDimension(int gWidth, int gHeight, int gDepth);

  // Get dimension
  int GetWidth();
  int GetHeight();
  int GetDepth();

  // Return the cubeSide (3D space)
  float GetCubeSide();

  // Return the Origin (3D space)
  VERTEX GetOrigin();

  // Spaziali (usati dal renderer)
  float GetFWidth();
  float GetFHeight();
  float GetFDepth();

  // Materiali (identici all'originale): struttura GLMATERIAL completa
  GLMATERIAL *GetMaterial(int idx);
  GLMATERIAL *GetGridMaterial();
  GLMATERIAL *GetBackMaterial();
  GLMATERIAL *GetWhiteMaterial();

  // Clear the pit
  void Clear();

  // Get the value at the specified coordinates
  // Return 1 when (x,y,z) is out of the pit
  int GetValue(int x, int y, int z);

  // Get the value at the specified coordinates
  // Return 0 when (x,y,z) is out of the pit
  int GetValue2(int x, int y, int z);

  // Add a value at the specified coordinates
  void AddCube(int x, int y, int z);

  // Get "out of bounds" values
  void GetOutOfBounds(int x, int y, int z, int *ox, int *oy, int *oz);

  // Remove full line
  int RemoveLines();

  // Return true if the pit is empty
  BOOL IsEmpty();

  // Accessori usati dal renderer / dall'AI
  BOOL IsVisible(int x, int y, int z);
  BOOL IsVisible2(int x, int y, int z);
  BOOL IsLineFull(int z);
  BOOL IsLineEmpty(int z);
  BLOCKITEM *GetOrderMatrix();
  int GetMatrixSize();

private:
  void SetValue(int x, int y, int z, int value);
  void InitOrderMatrix();
  void RemoveLine(int idx);

  // Materiali (gli stessi dell'originale Pit::Create)
  GLMATERIAL gridMaterial;
  GLMATERIAL blackMaterial;
  GLMATERIAL darkMaterial;
  GLMATERIAL backMaterial;
  GLMATERIAL backTexMaterial;
  GLMATERIAL whiteMaterial;

  int width;
  int height;
  int depth;
  int mSize;
  int tcubeIdx;
  float cubeSide;
  float fWidth;
  float fHeight;
  float fDepth;
  VERTEX origin;
  int *matrix;
  BLOCKITEM *orderMatrix;
};

#endif /* PITH */
