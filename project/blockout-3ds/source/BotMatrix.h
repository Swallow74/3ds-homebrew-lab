/*
  File:        BotMatrix.h
  Description: Handle bot matrix (A.I. player)
  Program:     BlockOut
  Author:      Jean-Luc PONS

  Port 3DS: copiato quasi verbatim da BotMatrix.h di BlockOut II 2.5 (nessuna dipendenza da SDL/OpenGL).

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
*/


#ifndef _BOTMATRIXH_
#define _BOTMATRIXH_
#pragma once

class BotMatrix {

public:

  BotMatrix();

  void Init33(float _11,float _12,float _13,
              float _21,float _22,float _23,
              float _31,float _32,float _33);

  void Identity();

  void RotateOX();
  void RotateOY();
  void RotateOZ();
  void RotateNOX();
  void RotateNOY();
  void RotateNOZ();
  void Transpose();

  float _11;float _12;float _13;
  float _21;float _22;float _23;
  float _31;float _32;float _33;

};

#endif /* _BOTMATRIXH_ */
