/*
  File:        Pit.cpp
  Description: Pit management - logic ported verbatim from BlockOut II.
               La parte OpenGL (display list / glDrawPixels) e' stata sostituita
               dagli accessors geometrici + materiali, usati dal renderer 3DS
               che disegna la stessa identica geometria in software.
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include "Pit.h"

Pit::Pit() {

  matrix = NULL;
  orderMatrix = NULL;
  width = 0;
  height = 0;
  depth = 0;
  mSize = 0;
  tcubeIdx = 1;
  cubeSide = 0.0f;
  fWidth = 0.0f;
  fHeight = 0.0f;
  fDepth = 0.0f;
  origin.x = 0.0f;
  origin.y = 0.0f;
  origin.z = 0.0f;
}

//-----------------------------------------------------------------------------
// SetDimension + geometria (cubeSide/origin: identico a Pit::Create)
//-----------------------------------------------------------------------------

void Pit::SetDimension(int gWidth, int gHeight, int gDepth) {

  width = gWidth;
  height = gHeight;
  depth = gDepth;

  // Init pit matrix
  if (matrix) {
    free(matrix);
    matrix = NULL;
  }
  if (orderMatrix) {
    free(orderMatrix);
    orderMatrix = NULL;
  }

  mSize = width * height * depth;
  if (mSize == 0) {
    // Invalid dimension
    return;
  }

  // Geometria 3D (Pit::Create nell'originale)
  float startX;
  float startY;
  if (width > height) {
    cubeSide = 1.0f / (float)width;
    startX = -0.5f;
    startY = -0.5f + ((float)(width - height) * cubeSide / 2.0f);
  } else {
    cubeSide = 1.0f / (float)height;
    startX = -0.5f + ((float)(height - width) * cubeSide / 2.0f);
    startY = -0.5f;
  }

  fWidth = (float)width * cubeSide;
  fHeight = (float)height * cubeSide;
  fDepth = (float)depth * cubeSide;

  origin.x = startX;
  origin.y = startY;
  origin.z = STARTZ;

  // Init material (Pit::Create originale)
  memset (&blackMaterial, 0, sizeof (GLMATERIAL));

  memset (&gridMaterial, 0, sizeof (GLMATERIAL));
  gridMaterial.Diffuse.r = 1.0f;
  gridMaterial.Diffuse.g = 1.0f;
  gridMaterial.Diffuse.b = 1.0f;
  gridMaterial.Ambient.r = 0.0f;
  gridMaterial.Ambient.g = 0.6f;
  gridMaterial.Ambient.b = 0.0f;
  memset (&darkMaterial, 0, sizeof (GLMATERIAL));
  darkMaterial.Diffuse.r = 0.1f;
  darkMaterial.Diffuse.g = 0.1f;
  darkMaterial.Diffuse.b = 0.1f;
  darkMaterial.Ambient.r = 0.1f;
  darkMaterial.Ambient.g = 0.1f;
  darkMaterial.Ambient.b = 0.1f;
  memset (&backMaterial, 0, sizeof (GLMATERIAL));
  backMaterial.Diffuse.r = 0.05f;
  backMaterial.Diffuse.g = 0.05f;
  backMaterial.Diffuse.b = 0.1f;
  backMaterial.Ambient.r = 0.05f;
  backMaterial.Ambient.g = 0.05f;
  backMaterial.Ambient.b = 0.1f;
  memset (&backTexMaterial, 0, sizeof (GLMATERIAL));
  backTexMaterial.Diffuse.r = 0.35f;
  backTexMaterial.Diffuse.g = 0.35f;
  backTexMaterial.Diffuse.b = 0.35f;
  backTexMaterial.Ambient.r = 0.35f;
  backTexMaterial.Ambient.g = 0.35f;
  backTexMaterial.Ambient.b = 0.35f;
  memset (&whiteMaterial, 0, sizeof (GLMATERIAL));
  whiteMaterial.Diffuse.r = 1.0f;
  whiteMaterial.Diffuse.g = 1.0f;
  whiteMaterial.Diffuse.b = 1.0f;
  whiteMaterial.Ambient.r = 1.0f;
  whiteMaterial.Ambient.g = 1.0f;
  whiteMaterial.Ambient.b = 1.0f;

  matrix = (int *)malloc(mSize * sizeof(int));
  Clear();
  InitOrderMatrix();
}

//-----------------------------------------------------------------------------

void Pit::Clear() {

  if (matrix)
    memset(matrix, 0, mSize * sizeof(int));
  tcubeIdx = 1;
}

//-----------------------------------------------------------------------------

int Pit::GetWidth() { return width; }
int Pit::GetHeight() { return height; }
int Pit::GetDepth() { return depth; }
float Pit::GetCubeSide() { return cubeSide; }
VERTEX Pit::GetOrigin() { return origin; }
float Pit::GetFWidth() { return fWidth; }
float Pit::GetFHeight() { return fHeight; }
float Pit::GetFDepth() { return fDepth; }
int Pit::GetMatrixSize() { return mSize; }
BLOCKITEM *Pit::GetOrderMatrix() { return orderMatrix; }

//-----------------------------------------------------------------------------

void Pit::SetValue(int x, int y, int z, int value) {

  if (x >= 0 && x < width && y >= 0 && y < height && z >= 0 && z < depth) {
    matrix[x + y * width + z * width * height] = value;
  }
}

//-----------------------------------------------------------------------------

int Pit::GetValue(int x, int y, int z) {

  if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth)
    return 1;
  else
    return matrix[x + y * width + z * width * height];
}

//-----------------------------------------------------------------------------

int Pit::GetValue2(int x, int y, int z) {

  if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= depth)
    return 0;
  else
    return matrix[x + y * width + z * width * height];
}

//-----------------------------------------------------------------------------

void Pit::AddCube(int x, int y, int z) {

  tcubeIdx++;
  if (tcubeIdx > NBTCUBE) tcubeIdx = 1;
  SetValue(x, y, z, tcubeIdx);
}

//-----------------------------------------------------------------------------

void Pit::GetOutOfBounds(int x, int y, int z, int *ox, int *oy, int *oz) {

  *ox = 0;
  *oy = 0;
  *oz = 0;

  if (x < 0) *ox = -x;
  if (x >= width) *ox = width - x - 1;

  if (y < 0) *oy = -y;
  if (y >= height) *oy = height - y - 1;

  if (z < 0) *oz = -z;
  if (z >= depth) *oz = depth - z - 1;
}

//-----------------------------------------------------------------------------

BOOL Pit::IsLineFull(int z) {

  BOOL full = TRUE;
  for (int i = 0; i < width && full; i++)
    for (int j = 0; j < height && full; j++)
      full = full && (GetValue(i, j, z) >= 1);
  return full;
}

//-----------------------------------------------------------------------------

BOOL Pit::IsLineEmpty(int z) {

  BOOL empty = TRUE;
  for (int i = 0; i < width && empty; i++)
    for (int j = 0; j < height && empty; j++)
      empty = empty && (GetValue(i, j, z) == 0);
  return empty;
}

//-----------------------------------------------------------------------------

void Pit::RemoveLine(int idx) {

  for (int k = idx; k > 0; k--) {
    for (int i = 0; i < width; i++)
      for (int j = 0; j < height; j++)
        SetValue(i, j, k, GetValue(i, j, k - 1));
  }

  // Clear last line
  for (int i = 0; i < width; i++)
    for (int j = 0; j < height; j++)
      SetValue(i, j, 0, 0);
}

//-----------------------------------------------------------------------------

int Pit::RemoveLines() {

  int nbRemoved = 0;
  int k = depth - 1;

  while (k >= 0) {
    if (IsLineFull(k)) {
      RemoveLine(k);
      nbRemoved++;
    } else {
      k--;
    }
  }

  return nbRemoved;
}

//-----------------------------------------------------------------------------

BOOL Pit::IsEmpty() {

  int i = 0;
  BOOL empty = TRUE;
  while (empty && i < depth) {
    empty = IsLineEmpty(i);
    i++;
  }
  return empty;
}

//-----------------------------------------------------------------------------

BOOL Pit::IsVisible(int x, int y, int z) {

  if (z == 0) return TRUE;

  return (GetValue(x + 1, y, z) == 0 ||
          GetValue(x - 1, y, z) == 0 ||
          GetValue(x, y + 1, z) == 0 ||
          GetValue(x, y - 1, z) == 0 ||
          GetValue(x, y, z - 1) == 0);
}

//-----------------------------------------------------------------------------

BOOL Pit::IsVisible2(int x, int y, int z) {

  if (z == 0) return TRUE;

  return (GetValue2(x + 1, y, z) == 0 ||
          GetValue2(x - 1, y, z) == 0 ||
          GetValue2(x, y + 1, z) == 0 ||
          GetValue2(x, y - 1, z) == 0 ||
          GetValue2(x, y, z - 1) == 0);
}

//-----------------------------------------------------------------------------
// Pit::InitOrderMatrix - ordina le celle dal piu' lontano al piu' vicino
// (painter's algorithm), identico all'originale.
//-----------------------------------------------------------------------------

void Pit::InitOrderMatrix() {

  double *dist;
  double xOrg = (double)width / 2.0;
  double yOrg = (double)height / 2.0;
  double zOrg = 0.0;

  orderMatrix = (BLOCKITEM *)malloc(sizeof(BLOCKITEM) * mSize);
  dist = (double *)malloc(sizeof(double) * mSize);

  // Init distance
  int l = 0;
  for (int k = 0; k < depth; k++) {
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        double xc = double(2 * i + 1) / 2.0;
        double yc = double(2 * j + 1) / 2.0;
        double zc = double(2 * k + 1) / 2.0;
        dist[l] = (xc - xOrg) * (xc - xOrg) +
                  (yc - yOrg) * (yc - yOrg) +
                  (zc - zOrg) * (zc - zOrg);
        orderMatrix[l].x = i;
        orderMatrix[l].y = j;
        orderMatrix[l].z = k;
        l++;
      }
    }
  }

  // Sort
  BOOL end = FALSE;
  int i = 0;
  int j = mSize - 1;

  while (!end) {
    end = TRUE;
    for (i = 0; i < j; i++) {
      if (dist[i] < dist[i + 1]) {

        // Swap dist
        double tmp = dist[i];
        dist[i] = dist[i + 1];
        dist[i + 1] = tmp;
        // Swap orderMatrix
        BLOCKITEM tmp2 = orderMatrix[i];
        orderMatrix[i] = orderMatrix[i + 1];
        orderMatrix[i + 1] = tmp2;

        end = FALSE;
      }
    }
    j--;
  }

  free(dist);
}

//-----------------------------------------------------------------------------
// Materiali: valori identici a Pit::GetMaterial / Pit::Create di BlockOut II
//-----------------------------------------------------------------------------

GLMATERIAL *Pit::GetMaterial(int level) {

  static GLMATERIAL ret;
  memset (&ret, 0, sizeof (GLMATERIAL));

  switch((depth-level-1) % 7) {

    case 0:
      ret.Diffuse.r = 0.0f;
      ret.Diffuse.g = 0.0f;
      ret.Diffuse.b = 1.0f;
      break;

    case 1:
      ret.Diffuse.r = 0.0f;
      ret.Diffuse.g = 1.0f;
      ret.Diffuse.b = 0.0f;
      break;

    case 2:
      ret.Diffuse.r = 0.0f;
      ret.Diffuse.g = 0.9f;
      ret.Diffuse.b = 0.9f;
      break;

    case 3:
      ret.Diffuse.r = 1.0f;
      ret.Diffuse.g = 0.0f;
      ret.Diffuse.b = 0.0f;
      break;

    case 4:
      ret.Diffuse.r = 1.0f;
      ret.Diffuse.g = 0.1f;
      ret.Diffuse.b = 0.8f;
      break;

    case 5:
      ret.Diffuse.r = 0.9f;
      ret.Diffuse.g = 0.6f;
      ret.Diffuse.b = 0.0f;
      break;

    case 6:
      ret.Diffuse.r = 0.85f;
      ret.Diffuse.g = 0.85f;
      ret.Diffuse.b = 0.85f;
      break;

  }

  ret.Ambient = ret.Diffuse;

  ret.Diffuse.r *= 0.3f;
  ret.Diffuse.g *= 0.3f;
  ret.Diffuse.b *= 0.3f;

  ret.Ambient.r *= 0.7f;
  ret.Ambient.g *= 0.7f;
  ret.Ambient.b *= 0.7f;

  return &ret;

}

//-----------------------------------------------------------------------------

GLMATERIAL *Pit::GetGridMaterial() { return &gridMaterial; }

//-----------------------------------------------------------------------------

GLMATERIAL *Pit::GetBackMaterial() { return &backMaterial; }

//-----------------------------------------------------------------------------

GLMATERIAL *Pit::GetWhiteMaterial() { return &whiteMaterial; }

