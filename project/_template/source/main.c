/*
 * Hello 3DS — template base libctru
 * Schermo superiore: console testuale. Premi START per uscire.
 */
#include <3ds.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	printf("\x1b[10;12HHello 3DS Homebrew!");
	printf("\x1b[12;8HBuild: .3dsx (hbmenu) + .cia (HOME Menu)");
	printf("\x1b[30;16HPress START to exit.");

	while (aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break;

		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}

	gfxExit();
	return 0;
}
