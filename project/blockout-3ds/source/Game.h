/*
  File:        Game.h
  Description: Game management
  Program:     BlockOut / BlockOut 3DS
  Author:      Jean-Luc PONS

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  3DS port: identical to the original Game.h of BlockOut II 2.5, without the
  classes tied to OpenGL/Windows (GLFont2D, Sprites, Sprite2D for the
  background and the spark). The pit and polycube objects are made
  accessible to the software renderer through the friendship on render_game().
*/

#ifndef GAMEH
#define GAMEH

#include <math.h>
#include "bo_compat.h"
#include "GLMatrix.h"
#include "Types.h"
#include "Pit.h"
#include "PolyCube.h"
#include "BotPlayer.h"
#include "SetupManager.h"
#include "SoundManager.h"

class Game {

  public:
    Game();

    // Set manager
    void SetSetupManager(SetupManager *manager);
    void SetSoundManager(SoundManager *manager);

    // Initialise device objects
    int Create(int width,int height);

    // Render the Game
    void Render();

    // Release device objects
    void InvalidateDeviceObjects();

    // Start a game
    void StartGame(int width,int height,float fTime);

    // Start a practice
    void StartPractice(int width,int height,float fTime);

    // Start a demo
    void StartDemo(int width,int height,float fTime);

    // Process game
    int Process(BYTE *keys,float fTime);

    // Ask full repaint
    void FullRepaint();

    // Get score
    SCOREREC *GetScore();

    // Set view matrix
    void SetViewMatrix(GLfloat *mView);

    /* ---- Adaptations required by the 3DS hardware ---------------------
       The original rendered with OpenGL inside Game::Render(); the software
       renderer (render.cpp) needs the pit viewport (which the
       original computed in Create()) and the position/projection
       of the spark. pitView.y stays in the original's GL bottom-left
       convention, as in Game::Create().                             */

    void SetPitViewport(int x,int y,int w,int h);

    int  GetPitViewX() { return pitView.x; }
    int  GetPitViewY() { return pitView.y; }
    int  GetPitViewW() { return pitView.width; }
    int  GetPitViewH() { return pitView.height; }

    /* accessors for main.cpp (in the original the menu read the game mode
       through friends/variants in main; here they are needed to drive the loop) */
    int  GetGameMode() { return gameMode; }
    int  GetInited()   { return inited; }

    // Parallax (gyroscope/tilt inclination, 3DS adaptation)
    float parallax;

    // Spark: position and size in screen coordinates (top-left)
    float sparkX;
    float sparkY;
    float sparkW;

    friend void render_game(Game *game);
    friend void drawSpark(Game *g);
    friend void renderHud(Game *g);
    friend void drawPitLevel(Game *g);
    friend void renderGameModeOverlay(Game *g);
    friend void renderPitView(Game *g, int eye);
    friend void renderBottomUI(Game *g);
    friend void renderPracticeHelp(Game *g);
    friend int runMenuScreen(Game *game, SetupManager *sm);

  private:

    // The pit
    Pit thePit;

    // PolyCubes
    PolyCube allPolyCube[NB_POLYCUBE]; // All polycubes (41 items)
    int   possible[NB_POLYCUBE];       // All possible polycubes for the setup
    int   nbPossible;                  // Number of possible polycube

    // Manager
    SetupManager *setupManager;
    SoundManager *soundManager;

    // Game stuff
    SCOREREC score;     // Score
    int   level;        // Current level
    int   highScore;    // High score of the current game setup
    int   gameMode;     // Game mode
    int   pIdx;         // Current polycube index (in the allPolyCube array)
    int   exitValue;    // Go back to menu when 1
    int   cubePerLevel; // Number of cube per level
    int   dropPos;      // Drop position
    int   cursorPos;    // Descent cursor

    // AI stuff (Demo mode)
    BotPlayer botPlayer;       // AI player
    AI_MOVE AIMoves[MAX_MOVE]; // AI computed moves
    int     nbAIMove;          // Number of AI moves
    int     curAIMove;         // Current AI move
    float   lastAIMoveTime;    // Last move time stamp
    BOOL    demoFlag;          // Demo flag
    BOOL    practiceFlag;      // Practice flag

    // PolyCube coordinates
    GLfloat     mat[16];     // Global transform matrix
    /* 3DS adaptation: IDENTICAL piece matrix but WITHOUT matView inside.
       In the original mat[] started from glLoadMatrixf(matView); the 3DS
       renderer applies the view (for each eye) by itself, so the
       piece-only transformation is needed too. */
    GLfloat     matPiece[16];
    GLfloat     matAIPiece[16];
    GLMatrix    matRot;      // Current rotation matrix (frame)
    GLMatrix    newMatRot;   // Virtual rotation matrix
    VERTEX      vPos;        // Current position (frame)
    VERTEX      vTransPos;   // Virtual position
    VERTEX      vOrgPos;     // Origin of translation
    int         xPos;        // Virtual position
    int         yPos;        // Virtual position
    int         zPos;        // Virtual position

    // Time stamps
    float startTranslateTime;
    float startRotateTime;
    float startRedTime;
    float startPauseTime;
    float startStepTime;
    float startGameTime;
    float startSpark;
    float startEndTime;

    // Animation time
    float animationTime;
    float stepTime;

    // Modes
    int   rotateMode;
    BOOL  dropMode;
    BOOL  dropped;
    BOOL  lastKeyMode;
    BOOL  redMode;

    // Misc
    float curTime;
    int   fullRepaint;
    int   transparent;
    BOOL  inited;
    int   style;
    int   lineWidth;
    BOOL  endAnimStarted;

    // Constant matrix
    GLMatrix matRotOx;
    GLMatrix matRotOy;
    GLMatrix matRotOz;
    GLMatrix matRotNOx;
    GLMatrix matRotNOy;
    GLMatrix matRotNOz;

    // Viewport and transformation matrix
    GLVIEWPORT   spriteView;
    GLVIEWPORT   pitView;
    GLfloat      matProj[16];
    GLfloat      matView[16];
    GLfloat      pitMatrix[16];

    // Help mode
    GLMatrix  matAI;
    float startShowAI;

    // Private methods
    void HandleKey(BYTE *keys);
    BOOL StartRotate(int rType);
    BOOL StartTranslate(int tType);
    void InitTranslate();
    void StartDrop();
    void AddPolyCube();
    void NewPolyCube(BYTE *keys);
    int  SelectPolyCube();
    void TransformCube(GLMatrix *matRot,BLOCKITEM *cubes,int nbCube,int tx,int ty,int tz);
    BOOL IsOverlap(GLMatrix *matRot,int tx,int ty,int tz);
    BOOL IsOverlap(GLMatrix *matRot,int tx,int ty,int tz,int *ox,int *oy,int *oz);
    BOOL IsOverlap(GLMatrix *matRot,int tx,int ty,int tz,BLOCKITEM *pos);
    BOOL IsOverlap(GLMatrix *matRot,int tx,int ty,int tz,int *ox,int *oy,int *oz,BLOCKITEM *pos);
    BOOL IsLower();
    int  GetBottom();
    int InitPolyCube(BOOL transparent,float wEdge);
    void ComputeScore(int nbLines,BOOL pitEmpty);
    void StartSpark(BLOCKITEM *pos);
    void RenderPracticeHelp();
    void ComputeHelp();

};

#endif
