/*
 * Blockfall 3DS — homebrew .3dsx con grafica citro2d (GPU 2D)
 *
 * Top screen    : pozzo 10x20 a blocchi colorati + pannello punteggio + next
 * Bottom screen : aiuto comandi
 *
 * Comandi:
 *   D-Pad Sx/Dx : muovi   | D-Pad Giu : caduta veloce
 *   A / Su      : ruota   | B : caduta istantanea (hard drop)
 *   START       : esci a hbmenu
 *   A su schermata GAME OVER: ricomincia
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 10
#define H 20

/* layout top screen (400x240) */
#define CELL   10
#define WELL_X 24
#define WELL_Y 18
#define PANEL_X 150

/* la board memorizza 0 = vuoto, altrimenti kind+1 (per il colore) */
static int board[H][W];

/* 7 tetramini in griglie 4x4 di spawn */
static const u8 SHAPES[7][4][4] = {
	/* I */
	{{0,0,0,0},
	 {1,1,1,1},
	 {0,0,0,0},
	 {0,0,0,0}},
	/* O */
	{{0,0,0,0},
	 {0,1,1,0},
	 {0,1,1,0},
	 {0,0,0,0}},
	/* T */
	{{0,0,0,0},
	 {1,1,1,0},
	 {0,1,0,0},
	 {0,0,0,0}},
	/* S */
	{{0,0,0,0},
	 {0,1,1,0},
	 {1,1,0,0},
	 {0,0,0,0}},
	/* Z */
	{{0,0,0,0},
	 {1,1,0,0},
	 {0,1,1,0},
	 {0,0,0,0}},
	/* J */
	{{0,0,0,0},
	 {1,1,1,0},
	 {0,0,1,0},
	 {0,0,0,0}},
	/* L */
	{{0,0,0,0},
	 {1,1,1,0},
	 {1,0,0,0},
	 {0,0,0,0}},
};

static u8 cur[4][4];
static int curKind = 0;
static int px, py;          /* top-left della 4x4 rispetto alla board */
static int score, lines, level;
static bool gameover;

/* 7-bag */
static int bag[7], bagPos = 7;

static u32 piece_color(int kind)
{
	switch (kind) {
	case 0: return C2D_Color32(0x00, 0xE5, 0xFF, 0xFF); /* I cyan */
	case 1: return C2D_Color32(0xFF, 0xE5, 0x00, 0xFF); /* O yellow */
	case 2: return C2D_Color32(0xC0, 0x00, 0xFF, 0xFF); /* T purple */
	case 3: return C2D_Color32(0x00, 0xE5, 0x00, 0xFF); /* S green */
	case 4: return C2D_Color32(0xFF, 0x30, 0x30, 0xFF); /* Z red */
	case 5: return C2D_Color32(0x30, 0x80, 0xFF, 0xFF); /* J blue */
	default: return C2D_Color32(0xFF, 0xA0, 0x00, 0xFF); /* L orange */
	}
}

static u32 piece_color_ghost(int kind)
{
	switch (kind) {
	case 0: return C2D_Color32(0x00, 0xE5, 0xFF, 0x55);
	case 1: return C2D_Color32(0xFF, 0xE5, 0x00, 0x55);
	case 2: return C2D_Color32(0xC0, 0x00, 0xFF, 0x55);
	case 3: return C2D_Color32(0x00, 0xE5, 0x00, 0x55);
	case 4: return C2D_Color32(0xFF, 0x30, 0x30, 0x55);
	case 5: return C2D_Color32(0x30, 0x80, 0xFF, 0x55);
	default: return C2D_Color32(0xFF, 0xA0, 0x00, 0x55);
	}
}

static void shuffle_bag(void)
{
	for (int i = 0; i < 7; i++) bag[i] = i;
	for (int i = 6; i > 0; i--) {
		u32 r = (u32)rand() % (u32)(i + 1);
		int t = bag[i]; bag[i] = bag[r]; bag[r] = t;
	}
	bagPos = 0;
}

static int next_piece(void)
{
	if (bagPos >= 7) shuffle_bag();
	return bag[bagPos++];
}

/* sbircia il prossimo pezzo senza consumarlo (per il preview) */
static int peek_next(void)
{
	if (bagPos >= 7) shuffle_bag();
	return bag[bagPos];
}

static bool collides(const u8 g[4][4], int ox, int oy)
{
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			if (!g[y][x]) continue;
			int bx = ox + x, by = oy + y;
			if (bx < 0 || bx >= W || by >= H) return true;
			if (by >= 0 && board[by][bx]) return true;
		}
	}
	return false;
}

static void rotate_cw(u8 g[4][4])
{
	u8 t[4][4];
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			t[x][3 - y] = g[y][x];
	memcpy(g, t, sizeof(t));
}

static bool try_rotate(void)
{
	u8 t[4][4];
	memcpy(t, cur, sizeof(t));
	rotate_cw(t);
	/* wall kick minimale: stessa pos, poi +-1, poi riga sopra */
	static const int kicks[][2] = {{0,0},{-1,0},{1,0},{0,-1},{-2,0},{2,0}};
	for (unsigned i = 0; i < sizeof(kicks)/sizeof(kicks[0]); i++) {
		if (!collides(t, px + kicks[i][0], py + kicks[i][1])) {
			memcpy(cur, t, sizeof(t));
			px += kicks[i][0]; py += kicks[i][1];
			return true;
		}
	}
	return false;
}

static void spawn(int kind)
{
	curKind = kind;
	memcpy(cur, SHAPES[kind], sizeof(cur));
	px = 3; py = -1;
	if (collides(cur, px, py)) {
		gameover = true;
	}
}

static int clear_lines(void)
{
	int n = 0;
	for (int y = H - 1; y >= 0; y--) {
		bool full = true;
		for (int x = 0; x < W; x++) if (!board[y][x]) { full = false; break; }
		if (full) {
			n++;
			for (int yy = y; yy > 0; yy--)
				memcpy(board[yy], board[yy - 1], sizeof(board[yy]));
			memset(board[0], 0, sizeof(board[0]));
			y++; /* ricontrolla la riga caduta qui */
		}
	}
	return n;
}

static void lock_piece(void)
{
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++) {
			if (!cur[y][x]) continue;
			int bx = px + x, by = py + y;
			if (by >= 0 && by < H && bx >= 0 && bx < W)
				board[by][bx] = curKind + 1;
		}
}

static const int LINE_PTS[5] = {0, 100, 300, 500, 800};

static void reset_game(void)
{
	memset(board, 0, sizeof(board));
	score = 0; lines = 0; level = 0;
	gameover = false;
	bagPos = 7;
	spawn(next_piece());
}

/* disegna un blocco con bordo scuro + highlight sopra */
static void draw_block(float x, float y, float s, u32 color)
{
	C2D_DrawRectSolid(x, y, 0.5f, s, s, color);
	/* highlight: striscia chiara in alto + bordo scuro in basso */
	C2D_DrawRectSolid(x + 1, y + 1, 0.5f, s - 2, 2,
		C2D_Color32(0xFF, 0xFF, 0xFF, 0x70));
	C2D_DrawRectSolid(x + 1, y + s - 2, 0.5f, s - 2, 1,
		C2D_Color32(0x00, 0x00, 0x00, 0x90));
}

static void draw_text(C2D_TextBuf buf, C2D_Font font, const char *str,
	float x, float y, float scale, u32 color)
{
	C2D_Text txt;
	C2D_TextFontParse(&txt, font, buf, str);
	C2D_TextOptimize(&txt);
	C2D_DrawText(&txt, C2D_AlignLeft | C2D_WithColor, x, y, 0.5f,
		scale, scale, color);
}

int main(int argc, char **argv)
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

	C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	C2D_Font font = C2D_FontLoadSystem(CFG_REGION_EUR);
	C2D_TextBuf txtBuf = C2D_TextBufNew(4096);

	const u32 colBg     = C2D_Color32(0x10, 0x10, 0x20, 0xFF);
	const u32 colWell   = C2D_Color32(0x00, 0x00, 0x00, 0xFF);
	const u32 colGrid   = C2D_Color32(0x30, 0x30, 0x48, 0xFF);
	const u32 colWhite  = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
	const u32 colDim    = C2D_Color32(0xA0, 0xA0, 0xB8, 0xFF);
	const u32 colAccent = C2D_Color32(0x00, 0xE5, 0xFF, 0xFF);

	srand(svcGetSystemTick());
	reset_game();

	int fallTick = 0;
	int moveTick = 0;   /* auto-repeat laterale */

	while (aptMainLoop()) {
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();

		if (kDown & KEY_START) break;

		if (gameover) {
			if (kDown & KEY_A) reset_game();
		} else {
			/* rotazione */
			if (kDown & (KEY_A | KEY_UP)) try_rotate();

			/* hard drop */
			if (kDown & KEY_B) {
				while (!collides(cur, px, py + 1)) py++;
				lock_piece();
				int n = clear_lines();
				if (n) {
					lines += n;
					score += LINE_PTS[n] * (level + 1);
					level = lines / 10;
				}
				spawn(next_piece());
				fallTick = 0;
			}

			/* movimento laterale con auto-repeat */
			int dir = 0;
			if (kHeld & KEY_LEFT) dir = -1;
			else if (kHeld & KEY_RIGHT) dir = 1;
			if (dir) {
				if ((kDown & (KEY_LEFT | KEY_RIGHT)) ||
				    (moveTick >= 8 && !collides(cur, px + dir, py))) {
					if (!collides(cur, px + dir, py)) px += dir;
					if (!(kDown & (KEY_LEFT | KEY_RIGHT))) moveTick = 4;
					else moveTick = 0;
				}
				moveTick++;
			} else {
				moveTick = 0;
			}

			/* gravita: intervallo in frame (60fps), soft drop accelera */
			int interval = 42 - level * 3;
			if (interval < 4) interval = 4;
			if (kHeld & KEY_DOWN) interval = 2;
			if (++fallTick >= interval) {
				fallTick = 0;
				if (!collides(cur, px, py + 1)) {
					py++;
					if (kHeld & KEY_DOWN) score += 1;
				} else {
					lock_piece();
					int n = clear_lines();
					if (n) {
						lines += n;
						score += LINE_PTS[n] * (level + 1);
						level = lines / 10;
					}
					spawn(next_piece());
				}
			}
		}

		/* ---------- rendering ---------- */
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		C2D_TextBufClear(txtBuf);

		/* ----- top screen: pozzo + pannello ----- */
		C2D_TargetClear(top, colBg);
		C2D_SceneBegin(top);

		/* sfondo pozzo + griglia */
		C2D_DrawRectSolid(WELL_X - 2, WELL_Y - 2, 0.5f,
			W * CELL + 4, H * CELL + 4, colWell);
		for (int y = 0; y <= H; y++)
			C2D_DrawRectSolid(WELL_X, WELL_Y + y * CELL, 0.5f,
				W * CELL, 1, colGrid);
		for (int x = 0; x <= W; x++)
			C2D_DrawRectSolid(WELL_X + x * CELL, WELL_Y, 0.5f,
				1, H * CELL, colGrid);

		/* blocchi fissati */
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				if (board[y][x])
					draw_block(WELL_X + x * CELL, WELL_Y + y * CELL,
						CELL, piece_color(board[y][x] - 1));

		if (!gameover) {
			/* ghost: dove atterrerebbe il pezzo */
			int gy = py;
			while (!collides(cur, px, gy + 1)) gy++;
			u32 ghost = piece_color_ghost(curKind);
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 4; x++) {
					if (!cur[y][x]) continue;
					int bx = px + x, by = gy + y;
					if (by < 0 || by >= H || bx < 0 || bx >= W) continue;
					if (board[by][bx]) continue;
					C2D_DrawRectSolid(WELL_X + bx * CELL + 1,
						WELL_Y + by * CELL + 1, 0.5f,
						CELL - 2, CELL - 2, ghost);
				}
			/* pezzo corrente */
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 4; x++) {
					if (!cur[y][x]) continue;
					int bx = px + x, by = py + y;
					if (by < 0) continue;
					draw_block(WELL_X + bx * CELL, WELL_Y + by * CELL,
						CELL, piece_color(curKind));
				}
		}

		/* pannello laterale */
		char line[64];
		draw_text(txtBuf, font, "BLOCKFALL 3DS", PANEL_X, 14, 0.7f, colAccent);
		snprintf(line, sizeof(line), "SCORE %d", score);
		draw_text(txtBuf, font, line, PANEL_X, 44, 0.55f, colWhite);
		snprintf(line, sizeof(line), "LINES %d   LV %d", lines, level);
		draw_text(txtBuf, font, line, PANEL_X, 66, 0.55f, colDim);
		draw_text(txtBuf, font, "NEXT", PANEL_X, 96, 0.55f, colDim);
		{
			int nk = peek_next();
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 4; x++)
					if (SHAPES[nk][y][x])
						draw_block(PANEL_X + x * CELL,
							112 + y * CELL, CELL, piece_color(nk));
		}

		if (gameover) {
			C2D_DrawRectSolid(WELL_X - 2, WELL_Y + 60, 0.5f,
				W * CELL + 4, 56, C2D_Color32(0x00, 0x00, 0x00, 0xC0));
			draw_text(txtBuf, font, "GAME OVER", WELL_X + 8,
				WELL_Y + 66, 0.6f, colWhite);
			draw_text(txtBuf, font, "A: retry", WELL_X + 8,
				WELL_Y + 88, 0.5f, colAccent);
		}

		/* ----- bottom screen: aiuto ----- */
		C2D_TargetClear(bot, colBg);
		C2D_SceneBegin(bot);
		draw_text(txtBuf, font, "BLOCKFALL 3DS - controls", 16, 14, 0.6f, colAccent);
		draw_text(txtBuf, font, "D-Pad Left/Right .. move piece", 16, 44, 0.5f, colWhite);
		draw_text(txtBuf, font, "D-Pad Down ........ soft drop", 16, 64, 0.5f, colWhite);
		draw_text(txtBuf, font, "A / Up ............ rotate", 16, 84, 0.5f, colWhite);
		draw_text(txtBuf, font, "B ................. hard drop", 16, 104, 0.5f, colWhite);
		draw_text(txtBuf, font, "START ............. exit to hbmenu", 16, 124, 0.5f, colWhite);
		snprintf(line, sizeof(line), "Level %d - every 10 lines the fall speeds up",
			level);
		draw_text(txtBuf, font, line, 16, 160, 0.5f, colDim);

		C3D_FrameEnd(0);
		gspWaitForVBlank(); /* C3D_FrameEnd non aspetta: senza questo si va' oltre i 60fps */
	}

	C2D_TextBufDelete(txtBuf);
	C2D_FontFree(font);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
