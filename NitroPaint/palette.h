#pragma once
#include <Windows.h>

typedef struct RGB_ {
	BYTE r;
	BYTE g;
	BYTE b;
	BYTE a;
} RGB;

DWORD reduce(DWORD col);

void createPalette_(DWORD * img, int width, int height, DWORD * pal, int nColors);

closestpalette(RGB rgb, RGB * palette, int paletteSize, RGB * error);

void doDiffuse(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt);

void createPalettes(DWORD *img, int width, int height, int chunkSize, DWORD *palette, int nPalettes, int paletteSize);