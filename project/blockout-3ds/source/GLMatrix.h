/*
  File:        GLMatrix.h
  Description: 3x3/4x4 matrix class (ported verbatim from BlockOut II GLApp)
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef _GLMATRIXH_
#define _GLMATRIXH_

#include "bo_compat.h"

class GLMatrix {

public:

  GLMatrix();

  void Init33(float _11, float _12, float _13,
              float _21, float _22, float _23,
              float _31, float _32, float _33);

  void Identity();
  void RotateX(float angle);
  void RotateY(float angle);
  void RotateZ(float angle);
  void Translate(float x, float y, float z);
  void Transpose();

  void Multiply(GLMatrix *m1, GLMatrix *m2);
  void Multiply(GLMatrix *m2);

  void TransfomVec(float x, float y, float z, float w,
                   float *rx, float *ry, float *rz, float *rw);

  GLfloat *GetGL();
  const float *Get() const;   // matrice row-major (16 float)
  void FromGL(GLfloat *m);

  float _11; float _12; float _13; float _14;
  float _21; float _22; float _23; float _24;
  float _31; float _32; float _33; float _34;
  float _41; float _42; float _43; float _44;
};

#endif /* _GLMATRIXH_ */
